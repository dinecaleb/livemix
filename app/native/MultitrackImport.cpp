#include "MultitrackImport.h"
#include "StemNames.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace livemix
{

namespace
{
    bool isSeparator (juce::juce_wchar c) noexcept { return c == ' ' || c == '-' || c == '_' || c == '.' || c == ')' || c == '('; }
    bool isDigit (juce::juce_wchar c) noexcept { return c >= '0' && c <= '9'; }

    juce::String trimSeparators (juce::String s)
    {
        s = s.trim();
        while (s.isNotEmpty() && isSeparator (s.getLastCharacter())) s = s.dropLastCharacters (1).trim();
        while (s.isNotEmpty() && isSeparator (s[0]) && s[0] != '(') s = s.substring (1).trim();
        return s;
    }

    // Digits at the end of `s`, counted back from `end`.
    int digitsBefore (const juce::String& s, int end) noexcept
    {
        int n = 0;
        while (end - n - 1 >= 0 && isDigit (s[end - n - 1])) ++n;
        return n;
    }

    // "-240927_2117", "_20240927-211700", " 240927T2117": a desk's time stamp at the end of a name.
    juce::String withoutTimeStamp (const juce::String& s)
    {
        const int len = s.length();
        const int time = digitsBefore (s, len);
        if (time < 4 || time > 6) return s;
        int at = len - time;
        if (at < 1 || ! (s[at - 1] == '-' || s[at - 1] == '_' || s[at - 1] == 'T')) return s;
        const int date = digitsBefore (s, at - 1);
        if (date != 6 && date != 8) return s;
        at = at - 1 - date;
        if (at > 0 && ! isSeparator (s[at - 1])) return s;
        const auto rest = trimSeparators (s.substring (0, at));
        return rest.isEmpty() ? s : rest;
    }

    // "Kick_002", "OH _001": a recorder's take number. A bare digit ("bgv1", "Tom 1") is the name's own.
    juce::String withoutTakeNumber (const juce::String& s)
    {
        const int len = s.length();
        const int n = digitsBefore (s, len);
        if (n < 2 || n > 3 || len - n < 2 || s[len - n - 1] != '_') return s;
        const auto rest = trimSeparators (s.substring (0, len - n - 1));
        return rest.isEmpty() ? s : rest;
    }

    // Lower case, letters and digits only: "Lead Vox" and "LEAD_VOX" are one name.
    juce::String normalised (const juce::String& s)
    {
        juce::String out;
        for (auto c : s.toLowerCase()) if (juce::CharacterFunctions::isLetterOrDigit (c)) out += juce::String::charToString (c);
        return out;
    }

    juce::String listed (const juce::StringArray& names, int most = 3)
    {
        juce::StringArray shown;
        for (int i = 0; i < names.size() && i < most; ++i) shown.add (names[i]);
        auto text = shown.joinIntoString (", ");
        if (names.size() > most) text += " and " + juce::String (names.size() - most) + " more";
        return text;
    }

    juce::String count (int n, const char* one, const char* many)
    {
        return juce::String (n) + " " + (n == 1 ? one : many);
    }

    // One file, or one channel of a desk's multichannel file.
    struct Source
    {
        juce::File file;
        int channel = 0;
        int fileChannels = 1;
        juce::int64 length = 0;         // in the file's samples
        double rate = 0.0;
        double start = 0.0;             // seconds, on whatever clock `precise` says
        bool hasStamp = false, hasDate = false;
        double stamp = 0.0;             // broadcast-WAV time
        double made = 0.0;              // when the file was made
        juce::String display, key;
        int number = -1;
        int poly = 0;                   // channels of the multichannel file it came from, 0 = an ordinary file
        int pass = 0;
        juce::String identity;
        double offsetInPass = 0.0;

        double seconds() const noexcept { return rate > 0.0 ? double (length) / rate : 0.0; }
    };

    const char* const kSkipFolders[] = { "bounces", "bounced files", "freeze files", "fade files", "rendered files" };

    void collect (const juce::File& f, juce::Array<juce::File>& out, juce::AudioFormatManager& formats,
                  juce::StringArray& skipped, int depth)
    {
        if (f.getFileName().startsWith (".")) return;
        if (f.isDirectory())
        {
            if (depth > 4) return;
            const auto name = f.getFileName().toLowerCase();
            for (auto* skip : kSkipFolders) if (name == skip) return;
            auto children = f.findChildFiles (juce::File::findFilesAndDirectories, false, "*");
            std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getFileName().compareNatural (b.getFileName()) < 0; });
            for (const auto& c : children) collect (c, out, formats, skipped, depth + 1);
            return;
        }
        if (formats.findFormatForFileExtension (f.getFileExtension()) == nullptr)
        {
            // A folder is full of things that are not audio; only a file handed over by name is worth a word.
            if (depth == 0) skipped.add (f.getFileName() + " (not an audio file)");
            return;
        }
        out.addIfNotAlreadyThere (f);
    }

    double localMidnight (const juce::String& date)   // "yyyy-mm-dd"
    {
        if (date.length() < 10) return 0.0;
        const int y = date.substring (0, 4).getIntValue(), m = date.substring (5, 7).getIntValue(), d = date.substring (8, 10).getIntValue();
        if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0.0;
        return double (juce::Time (y, m - 1, d, 0, 0, 0, 0, true).toMilliseconds()) / 1000.0;
    }
}

// ------------------------------------------------------------------ names
MultitrackImport::ParsedName MultitrackImport::parseName (const juce::String& raw)
{
    ParsedName out;
    juce::String s = trimSeparators (StemNames::cleanName (raw));   // "Kick#03" -> "Kick"
    s = withoutTakeNumber (withoutTimeStamp (s));

    // A leading channel number: "01-KICK", "3 Bass", "Ch 05 - Pastor", "CH12_Keys".
    juce::String rest = s;
    const auto lower = s.toLowerCase();
    for (auto* prefix : { "channel", "chan", "ch", "track", "trk" })
        if (lower.startsWith (prefix) && s.length() > int (strlen (prefix)))
        {
            auto after = s.substring (int (strlen (prefix))).trimStart();
            if (after.isNotEmpty() && isDigit (after[0])) { rest = after; break; }
        }
    int digits = 0;
    while (digits < rest.length() && digits < 3 && isDigit (rest[digits])) ++digits;
    if (digits > 0 && (digits == rest.length() || isSeparator (rest[digits])))
    {
        const auto name = trimSeparators (rest.substring (digits));
        out.number = rest.substring (0, digits).getIntValue();
        out.display = name.isNotEmpty() ? name : s;        // "Ch 5" on its own is still called that
    }
    else
    {
        out.display = s;
    }
    if (out.display.isEmpty()) out.display = raw.trim();
    return out;
}

int MultitrackImport::sideOf (const juce::String& display, juce::String& base)
{
    const auto name = display.trim();
    const auto lower = name.toLowerCase();
    auto cut = [&] (int chars, int side) -> int
    {
        base = trimSeparators (name.dropLastCharacters (chars));
        return base.isNotEmpty() ? side : 0;
    };
    if (lower.endsWith ("(l)")) return cut (3, -1);
    if (lower.endsWith ("(r)")) return cut (3, 1);
    if (lower.endsWith ("left") && lower.length() > 4) return cut (4, -1);
    if (lower.endsWith ("right") && lower.length() > 5) return cut (5, 1);
    if (lower.length() >= 3)
    {
        const auto last = lower.getLastCharacter();
        if (last == 'l' || last == 'r')
        {
            // "Keys L", "Piano.R", and glued: "ohL", "keys1r". A name that merely ends in an l or
            // an r ("Vocal", "Guitar") is only ever paired with a partner that has the same base,
            // which no real name has.
            return cut (1, last == 'l' ? -1 : 1);
        }
    }
    return 0;
}

// ------------------------------------------------------------------ plan
MultitrackImport::Plan MultitrackImport::plan (const juce::Array<juce::File>& filesOrFolders)
{
    Plan result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    juce::Array<juce::File> files;
    for (const auto& f : filesOrFolders) collect (f, files, formats, result.skipped, 0);

    std::vector<Source> sources;
    for (const auto& file : files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr) { result.skipped.add (file.getFileName() + " (it will not open)"); continue; }
        if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        {
            result.skipped.add (file.getFileName() + " (empty)");
            continue;
        }
        ++result.files;
        if (result.sampleRate <= 0.0) result.sampleRate = reader->sampleRate;

        Source s;
        s.file = file;
        s.fileChannels = int (reader->numChannels);
        s.length = reader->lengthInSamples;
        s.rate = reader->sampleRate;
        s.made = double (file.getCreationTime().toMilliseconds()) / 1000.0;
        const auto ref = reader->metadataValues.getValue (juce::WavAudioFormat::bwavTimeReference, {});
        if (ref.isNotEmpty())
        {
            const auto date = reader->metadataValues.getValue (juce::WavAudioFormat::bwavOriginationDate, {});
            s.hasStamp = true;
            s.hasDate = localMidnight (date) > 0.0;
            s.stamp = localMidnight (date) + double (ref.getLargeIntValue()) / reader->sampleRate;
        }
        const auto parsed = parseName (file.getFileNameWithoutExtension());
        s.display = parsed.display;
        s.number = parsed.number;

        if (s.fileChannels > 2)
        {
            // A desk's card recording: every channel is a track of its own, named by its channel.
            s.poly = s.fileChannels;
            for (int c = 0; c < s.fileChannels; ++c)
            {
                Source ch = s;
                ch.channel = c;
                ch.fileChannels = 1;
                ch.number = c + 1;
                ch.display = "Ch " + juce::String (c + 1);
                sources.push_back (ch);
            }
            continue;
        }
        sources.push_back (s);
    }
    if (sources.empty()) return result;

    for (auto& s : sources) s.key = normalised (s.display);

    // WHICH CLOCK. A broadcast-WAV stamp is sample-accurate, but only if every file has one on
    // the same footing; otherwise the moment each file was made is the clock, and only good
    // enough to tell one pass from the next.
    bool stamped = true, dated = true, undated = true;
    for (const auto& s : sources)
    {
        if (s.poly > 0) continue;
        stamped = stamped && s.hasStamp;
        dated = dated && s.hasDate;
        undated = undated && ! s.hasDate;
    }
    const bool precise = stamped && (dated || undated);
    for (auto& s : sources) s.start = precise ? s.stamp : s.made;

    // ------------------------------------------------ passes (ordinary files)
    std::vector<int> order;
    for (int i = 0; i < int (sources.size()); ++i) if (sources[size_t (i)].poly == 0) order.push_back (i);
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b)
    {
        const auto& x = sources[size_t (a)]; const auto& y = sources[size_t (b)];
        if (std::abs (x.start - y.start) > 0.5) return x.start < y.start;
        return x.file.getFileName().compareNatural (y.file.getFileName()) < 0;
    });

    struct Pass { double start = 0.0, end = 0.0; std::vector<int> members; };
    std::vector<Pass> passes;
    for (int i : order)
    {
        auto& s = sources[size_t (i)];
        bool fresh = passes.empty();
        if (! fresh)
        {
            // Overlapping the pass: part of it. After it, and a take of a channel the pass already
            // has: the next pass. After it but a channel of its own: part of it still, so a folder
            // of stems copied one by one stays one recording.
            const auto& p = passes.back();
            const bool overlaps = s.start < p.end - 0.5;
            bool again = false;
            for (int m : p.members)
            {
                const auto& o = sources[size_t (m)];
                if (s.number >= 0 ? o.number == s.number : (o.number < 0 && o.key == s.key)) { again = true; break; }
            }
            fresh = ! overlaps && again;
        }
        if (fresh) passes.push_back ({ s.start, s.start, {} });
        auto& p = passes.back();
        p.members.push_back (i);
        p.end = juce::jmax (p.end, s.start + s.seconds());
    }

    // Each pass after the last one: its real gap when that is under a minute, else two seconds
    // (a service recorded in two halves has nothing worth an hour of empty timeline between them).
    double at = 0.0;
    for (size_t k = 0; k < passes.size(); ++k)
    {
        auto& p = passes[k];
        if (k > 0)
        {
            const auto& before = passes[k - 1];
            const double gap = p.start - before.end;
            at += (before.end - before.start) + (gap >= 0.0 && gap <= 60.0 ? gap : 2.0);
        }
        // Within a pass, files of the same name are told apart by their order: LEAD_001, _002, _003.
        std::map<juce::String, int> seen;
        std::vector<int> byName (p.members);
        std::stable_sort (byName.begin(), byName.end(), [&] (int a, int b)
                          { return sources[size_t (a)].file.getFileName().compareNatural (sources[size_t (b)].file.getFileName()) < 0; });
        for (int m : byName)
        {
            auto& s = sources[size_t (m)];
            s.pass = int (k);
            s.offsetInPass = at + (precise ? juce::jmax (0.0, s.start - p.start) : 0.0);
            s.identity = s.number >= 0 ? "#" + juce::String (s.number) + ":" + s.key
                                       : s.key + "/" + juce::String (seen[s.key]++);
        }
    }
    result.passes = juce::jmax (1, int (passes.size()));

    // ------------------------------------------------ multichannel files, chunk after chunk
    {
        std::vector<int> poly;
        for (int i = 0; i < int (sources.size()); ++i) if (sources[size_t (i)].poly > 0) poly.push_back (i);
        std::stable_sort (poly.begin(), poly.end(), [&] (int a, int b)
        {
            const auto& x = sources[size_t (a)]; const auto& y = sources[size_t (b)];
            const auto fx = x.file.getParentDirectory().getFullPathName(), fy = y.file.getParentDirectory().getFullPathName();
            if (fx != fy) return fx.compareNatural (fy) < 0;
            return x.file.getFileName().compareNatural (y.file.getFileName()) < 0;
        });
        std::map<int, double> nextStart;             // per channel count: where the next chunk begins
        std::map<juce::String, double> chunkStart;   // per file
        juce::String lastFolder;
        for (int i : poly)
        {
            auto& s = sources[size_t (i)];
            const auto path = s.file.getFullPathName();
            const auto folder = s.file.getParentDirectory().getFullPathName();
            if (chunkStart.find (path) == chunkStart.end())
            {
                double& next = nextStart[s.poly];
                if (folder != lastFolder && next > 0.0) next += 2.0;    // another recording on the same card
                lastFolder = folder;
                chunkStart[path] = next;
                next += s.seconds();
            }
            s.offsetInPass = chunkStart[path];
            s.identity = "poly" + juce::String (s.poly) + ":" + juce::String (s.channel);
        }
    }

    // ------------------------------------------------ tracks
    // In the order a person reads the folder: numbered files by their number, the rest by name.
    std::vector<int> byReading (sources.size());
    for (size_t i = 0; i < sources.size(); ++i) byReading[i] = int (i);
    std::stable_sort (byReading.begin(), byReading.end(), [&] (int a, int b)
    {
        const auto& x = sources[size_t (a)]; const auto& y = sources[size_t (b)];
        if ((x.poly > 0) != (y.poly > 0)) return x.poly > 0;
        if (x.poly > 0) return x.channel < y.channel;
        if ((x.number >= 0) != (y.number >= 0)) return x.number >= 0;
        if (x.number >= 0 && x.number != y.number) return x.number < y.number;
        return x.file.getFileName().compareNatural (y.file.getFileName()) < 0;
    });

    std::map<juce::String, size_t> trackOf;
    for (int i : byReading)
    {
        const auto& s = sources[size_t (i)];
        auto found = trackOf.find (s.identity);
        if (found == trackOf.end())
        {
            PlannedTrack t;
            t.name = s.display;
            t.stereo = s.fileChannels == 2;
            t.deviceChannel = s.poly > 0 ? s.channel : -1;
            result.tracks.push_back (t);
            found = trackOf.emplace (s.identity, result.tracks.size() - 1).first;
        }
        PlannedClip pc;
        pc.clip.name = s.display;
        pc.clip.file = s.file.getFullPathName();
        pc.clip.fileChannel = s.channel;
        pc.clip.offset = 0;
        pc.clip.length = s.length;
        pc.clip.fileSampleRate = s.rate;
        pc.startSeconds = s.offsetInPass;
        pc.pass = s.poly > 0 ? -1 : s.pass;
        auto& t = result.tracks[found->second];
        t.stereo = t.stereo || s.fileChannels == 2;
        t.clips.push_back (pc);
    }

    // ------------------------------------------------ split stereo, joined
    auto guess = [] (const juce::String& name, ChannelRole& role) { return StemNames::guessRole (name, role); };
    for (size_t l = 0; l < result.tracks.size(); ++l)
    {
        auto& left = result.tracks[l];
        if (left.stereo || left.deviceChannel >= 0) continue;
        juce::String baseL;
        if (sideOf (left.name, baseL) != -1) continue;
        for (size_t r = 0; r < result.tracks.size(); ++r)
        {
            auto& right = result.tracks[r];
            if (r == l || right.stereo || right.deviceChannel >= 0 || right.clips.size() != left.clips.size()) continue;
            juce::String baseR;
            if (sideOf (right.name, baseR) != 1 || normalised (baseR) != normalised (baseL)) continue;

            // Two toms named by the side of the kit they sit on are two drums.
            ChannelRole rl = ChannelRole::Count, rr = ChannelRole::Count;
            const bool gl = guess (left.name, rl), gr = guess (right.name, rr);
            const bool overheads = rl == ChannelRole::OverheadLeft && rr == ChannelRole::OverheadRight;
            if (gl != gr || (gl && rl != rr && ! overheads)) continue;

            bool same = true;
            for (size_t c = 0; c < left.clips.size() && same; ++c)
            {
                const auto& a = left.clips[c]; const auto& b = right.clips[c];
                same = a.pass == b.pass && std::abs (a.startSeconds - b.startSeconds) < 0.001
                    && std::abs (double (a.clip.length) / a.clip.fileSampleRate - double (b.clip.length) / b.clip.fileSampleRate) < 0.01;
            }
            if (! same) continue;

            for (size_t c = 0; c < left.clips.size(); ++c)
            {
                left.clips[c].clip.fileRight = right.clips[c].clip.file;
                left.clips[c].clip.fileRightChannel = right.clips[c].clip.fileChannel;
                left.clips[c].clip.name = baseL;
            }
            left.name = baseL;
            left.stereo = true;
            left.joinedPair = true;
            result.tracks.erase (result.tracks.begin() + std::ptrdiff_t (r));
            if (r < l) --l;
            break;
        }
    }

    // ------------------------------------------------ names and sources
    // Two tracks of one name are numbered: LEAD 1, LEAD 2, LEAD 3.
    {
        std::map<juce::String, int> total, given;
        for (const auto& t : result.tracks) ++total[normalised (t.name)];
        for (auto& t : result.tracks)
        {
            const auto key = normalised (t.name);
            if (total[key] > 1) t.name = t.name + " " + juce::String (++given[key]);
        }
    }
    int kicks = 0;
    for (auto& t : result.tracks)
    {
        ChannelRole role = ChannelRole::SynthPad;
        t.recognised = guess (t.name, role);
        if (! t.recognised) role = ChannelRole::SynthPad;   // heard, in the Music group, until somebody says
        if (t.stereo && (role == ChannelRole::OverheadLeft || role == ChannelRole::OverheadRight)) role = ChannelRole::Overhead;
        t.role = role;
        if (role == ChannelRole::KickIn) ++kicks;
    }
    // Two kick microphones that do not say which is which: the second is the outside one.
    if (kicks == 2)
    {
        bool first = true;
        for (auto& t : result.tracks)
            if (t.role == ChannelRole::KickIn) { if (! first) t.role = ChannelRole::KickOut; first = false; }
    }
    return result;
}

// ------------------------------------------------------------------ apply
MultitrackImport::Applied MultitrackImport::apply (const Plan& plan, MixSession& session, Project& project,
                                                   Destination dest, int firstTrack, juce::int64 at)
{
    Applied out;
    project.syncTracks (session);
    if (! project.hasAudio() && plan.sampleRate > 0.0) project.sampleRate = plan.sampleRate;
    const double rate = project.sampleRate > 0.0 ? project.sampleRate : 48000.0;
    const juce::int64 origin = dest == Destination::Match ? 0 : juce::jmax ((juce::int64) 0, at);

    auto toClip = [&] (const PlannedClip& pc)
    {
        AudioClip c = pc.clip;
        c.start = origin + juce::int64 (std::llround (pc.startSeconds * rate));
        if (c.fileSampleRate > 0.0) c.length = juce::int64 (double (c.length) * rate / c.fileSampleRate);
        return c;
    };

    const int existing = int (session.inputs.size());
    std::vector<int> target (plan.tracks.size(), -1);
    std::vector<bool> consumed (plan.tracks.size(), false);
    std::vector<bool> taken (size_t (existing), false);

    if (dest == Destination::OntoTracks)
    {
        for (size_t k = 0; k < plan.tracks.size(); ++k)
            if (firstTrack >= 0 && firstTrack + int (k) < existing) target[k] = firstTrack + int (k);
    }
    else if (dest == Destination::Match)
    {
        auto open = [&] (int t) { return ! taken[size_t (t)] && project.tracks[size_t (t)].clips.empty(); };
        // By name.
        for (size_t k = 0; k < plan.tracks.size(); ++k)
            for (int t = 0; t < existing; ++t)
                if (open (t) && normalised (juce::String (session.inputs[size_t (t)].name)) == normalised (plan.tracks[k].name))
                    { target[k] = t; taken[size_t (t)] = true; break; }
        // A card recording by the console channel it came in on.
        for (size_t k = 0; k < plan.tracks.size(); ++k)
        {
            if (target[k] >= 0 || plan.tracks[k].deviceChannel < 0) continue;
            for (int t = 0; t < existing; ++t)
                if (open (t) && session.inputs[size_t (t)].inputA == plan.tracks[k].deviceChannel)
                    { target[k] = t; taken[size_t (t)] = true; break; }
        }
        // The only file of its source onto the only empty track of that source.
        for (size_t k = 0; k < plan.tracks.size(); ++k)
        {
            const auto& p = plan.tracks[k];
            if (target[k] >= 0 || ! p.recognised) continue;
            int files = 0, tracks = 0, which = -1;
            for (size_t j = 0; j < plan.tracks.size(); ++j)
                if (target[j] < 0 && plan.tracks[j].recognised && plan.tracks[j].role == p.role) ++files;
            for (int t = 0; t < existing; ++t)
                if (open (t) && session.inputs[size_t (t)].enabled && session.inputs[size_t (t)].role == p.role
                    && session.inputs[size_t (t)].isStereo() == p.stereo) { ++tracks; which = t; }
            if (files == 1 && tracks == 1) { target[k] = which; taken[size_t (which)] = true; }
        }
    }

    int nextChannel = 0;
    for (const auto& in : session.inputs) nextChannel = juce::jmax (nextChannel, juce::jmax (in.inputA, in.inputB) + 1);

    std::vector<bool> touched (size_t (existing), false);
    for (size_t k = 0; k < plan.tracks.size(); ++k)
    {
        if (consumed[k]) continue;
        const auto& p = plan.tracks[k];
        int t = target[k];
        if (t >= 0)
        {
            auto& clips = project.tracks[size_t (t)].clips;
            const auto& in = session.inputs[size_t (t)];
            for (const auto& pc : p.clips)
            {
                auto c = toClip (pc);
                // A stereo console input fed from a card: its right side is the card's next channel.
                if (in.isStereo() && p.deviceChannel >= 0 && c.fileRight.isEmpty())
                {
                    c.fileRight = c.file;
                    c.fileRightChannel = c.fileChannel + 1;
                }
                clips.push_back (c);
                ++out.clips;
            }
            if (in.isStereo() && p.deviceChannel >= 0)
                for (size_t j = 0; j < plan.tracks.size(); ++j)
                    if (target[j] < 0 && plan.tracks[j].deviceChannel == p.deviceChannel + 1) consumed[j] = true;
            touched[size_t (t)] = true;
            ++out.onExisting;
            continue;
        }

        const int channels = p.stereo ? 2 : 1;
        if (nextChannel + channels > kMaxInputs || int (session.inputs.size()) >= kMaxStrips)
        {
            out.leftOut.add (p.name);
            continue;
        }
        InputAssignment in;
        in.name = p.name.toStdString();
        in.inputA = nextChannel;
        in.inputB = p.stereo ? nextChannel + 1 : -1;
        in.enabled = true;
        in.role = p.role;
        nextChannel += channels;
        session.inputs.push_back (in);

        TrackState track;
        track.monitor = MonitorMode::Auto;
        for (const auto& pc : p.clips) { track.clips.push_back (toClip (pc)); ++out.clips; }
        std::sort (track.clips.begin(), track.clips.end(), [] (const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
        project.tracks.push_back (track);
        ++out.added;
        if (p.joinedPair) ++out.pairs;
        if (! p.recognised) out.unrecognised.add (p.name);
    }
    for (int t = 0; t < existing; ++t)
        if (touched[size_t (t)])
        {
            auto& clips = project.tracks[size_t (t)].clips;
            std::sort (clips.begin(), clips.end(), [] (const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
        }
    for (size_t k = 0; k < plan.tracks.size(); ++k)
        if (target[k] >= 0 && plan.tracks[k].joinedPair) ++out.pairs;

    // ------------------------------------------------ what to say
    juce::String said;
    const int placed = out.added + out.onExisting;
    if (dest == Destination::OntoTracks)
    {
        said = "Added " + count (out.clips, "clip", "clips");
        if (out.added > 0) said += " (" + count (out.added, "new track", "new tracks") + ")";
    }
    else
    {
        said = "Imported " + count (plan.files, "file", "files") + " as " + count (placed, "track", "tracks");
        if (out.onExisting > 0 && existing > 0)
            said += ", " + juce::String (out.onExisting) + " of them onto tracks that were already set up";
    }
    said += ".";
    if (out.pairs > 0) said += " " + count (out.pairs, "left/right pair was", "left/right pairs were") + " joined into stereo.";
    if (plan.passes > 1) said += " The " + juce::String (plan.passes) + " recording passes follow one another on the timeline.";
    if (! out.unrecognised.isEmpty())
        // Worded without "could not": the toast reads that as a refusal, and this is a success.
        said += " The name" + juce::String (out.unrecognised.size() == 1 ? " does" : "s do") + " not say what "
              + listed (out.unrecognised) + (out.unrecognised.size() == 1 ? " is, so it plays" : " are, so they play")
              + " in the Music group until you set its source: right-click the track's name.";
    if (! out.leftOut.isEmpty())
        said += " " + juce::String (out.leftOut.size()) + " did not fit (a session takes " + juce::String (kMaxInputs)
              + " input channels): " + listed (out.leftOut) + ".";
    if (! plan.skipped.isEmpty()) said += " Left out: " + listed (plan.skipped) + ".";
    out.summary = said;
    return out;
}

// ------------------------------------------------------------------ a folder, fresh
MultitrackImport::Result MultitrackImport::fromFolder (const juce::File& folder, const MixSession& base)
{
    Result result;
    result.session = base;
    result.session.inputs.clear();
    result.session.name = folder.getFileName().toStdString();
    result.project = Project {};
    // Imported audio stays where it is (the clips hold absolute paths). The project has no
    // folder of its own until it is saved, which is also when recording becomes possible.

    if (! folder.isDirectory())
    {
        result.error = "That is not a folder.";
        return result;
    }

    const auto planned = plan ({ folder });
    result.files = planned.files;
    if (planned.tracks.empty())
    {
        result.error = planned.skipped.isEmpty() ? "No audio files in " + folder.getFileName() + "."
                                                 : "None of the files in " + folder.getFileName() + " could be read.";
        return result;
    }
    const auto applied = apply (planned, result.session, result.project, Destination::Match);
    result.sampleRate = result.project.sampleRate;
    result.summary = applied.summary;

    // No loop is marked: a loop over the whole folder is no loop at all, and it filled the loop
    // strip so a range could only be made by dragging its two far corners.
    result.project.loopStart = 0;
    result.project.loopEnd = 0;
    return result;
}

} // namespace livemix
