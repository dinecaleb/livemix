#include "SessionStore.h"
#include "DSP/ChannelParameters.h"
#include "FX/FxParameters.h"
#include <algorithm>

namespace livemix
{

namespace
{
    juce::var channelToVar (const ChannelParameters& p)
    {
        auto* obj = new juce::DynamicObject();
        ChannelParameters copy = p;
        forEachDspParameter (copy, [&] (const std::string& id, auto& v) { obj->setProperty (juce::Identifier (id), juce::var (double (v))); });
        return juce::var (obj);
    }

    void channelFromVar (const juce::var& v, ChannelParameters& p)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        forEachDspParameter (p, [&] (const std::string& id, auto& field)
        {
            const juce::Identifier key (id);
            if (! obj->hasProperty (key)) return;
            using T = std::remove_reference_t<decltype (field)>;
            const double value = double (obj->getProperty (key));
            if constexpr (std::is_same_v<T, bool>) field = value >= 0.5;
            else field = T (value);
        });
    }

    juce::var fxToVar (const FxParameters& p)
    {
        auto* obj = new juce::DynamicObject();
        FxParameters copy = p;
        forEachFxParameter (copy, [&] (const std::string& id, auto& v) { obj->setProperty (juce::Identifier (id), juce::var (double (v))); });
        return juce::var (obj);
    }

    void fxFromVar (const juce::var& v, FxParameters& p)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        forEachFxParameter (p, [&] (const std::string& id, auto& field)
        {
            const juce::Identifier key (id);
            if (! obj->hasProperty (key)) return;
            using T = std::remove_reference_t<decltype (field)>;
            const double value = double (obj->getProperty (key));
            if constexpr (std::is_same_v<T, bool>) field = value >= 0.5;
            else field = T (value);
        });
    }

    juce::var mixToVar (const MixParameters& m)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("numStrips", m.numStrips);
        obj->setProperty ("tempoBpm", m.tempoBpm);   // what the synced delays are in time with
        juce::Array<juce::var> strips;
        for (int i = 0; i < m.numStrips; ++i)
        {
            const auto& s = m.strips[size_t (i)];
            auto* so = new juce::DynamicObject();
            so->setProperty ("channel", channelToVar (s.channel));
            so->setProperty ("inputGainDb", s.inputGainDb);
            so->setProperty ("faderDb", s.faderDb);
            so->setProperty ("pan", s.pan);
            so->setProperty ("mute", s.mute);
            so->setProperty ("solo", s.solo);
            if (s.linkGroup != 0) so->setProperty ("linkGroup", s.linkGroup);   // linked faders; absent = not linked
            juce::Array<juce::var> sends;
            for (float db : s.sendDb) sends.add (db);
            so->setProperty ("sendDb", sends);
            strips.add (juce::var (so));
        }
        obj->setProperty ("strips", strips);
        juce::Array<juce::var> buses;
        for (const auto& b : m.buses)
        {
            auto* bo = new juce::DynamicObject();
            bo->setProperty ("channel", channelToVar (b.channel));
            bo->setProperty ("faderDb", b.faderDb);
            bo->setProperty ("mute", b.mute);
            bo->setProperty ("solo", b.solo);
            buses.add (juce::var (bo));
        }
        obj->setProperty ("buses", buses);
        juce::Array<juce::var> fx;
        for (const auto& f : m.fx)
        {
            auto* fo = new juce::DynamicObject();
            fo->setProperty ("fx", fxToVar (f.fx));
            fo->setProperty ("returnDb", f.returnDb);
            fo->setProperty ("enabled", f.enabled);
            fo->setProperty ("solo", f.solo);
            fx.add (juce::var (fo));
        }
        obj->setProperty ("fx", fx);
        obj->setProperty ("fxReturnDb", m.fxReturnDb);
        obj->setProperty ("fxMute", m.fxMute);
        // The engineer's own listen. Monitoring, never mix - but it is worth reopening a
        // service with the headphones set the way they were left.
        auto* mon = new juce::DynamicObject();
        mon->setProperty ("mode", int (m.monitor.mode));
        mon->setProperty ("point", int (m.monitor.point));
        mon->setProperty ("gainDb", m.monitor.gainDb);
        mon->setProperty ("mute", m.monitor.mute);
        mon->setProperty ("dim", m.monitor.dim);
        mon->setProperty ("dimDb", m.monitor.dimDb);
        mon->setProperty ("source", int (m.monitor.source));
        obj->setProperty ("monitor", juce::var (mon));
        return juce::var (obj);
    }

    // Every group bus DLIVE has added went in immediately before MASTER - SPEECH in version 3,
    // AMBIENCE in version 4 - because everything that walks the groups uses `b < Master`. That
    // moves the master's stored index each time and nothing else's, which is the whole of the
    // migration: a stored slot is the same group it always was, except the last one, which was
    // the master then and is the master now.
    //
    // A session saved before a group existed opens with that group empty and everything else
    // exactly where it was. Nothing is guessed and nothing is dropped.
    constexpr int storedBusCount (int fileVersion) noexcept
    {
        if (fileVersion >= 4) return int (MixBus::Count);       // ... DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE MASTER
        if (fileVersion == 3) return int (MixBus::Count) - 1;   // no AMBIENCE
        return int (MixBus::Count) - 2;                         // no SPEECH either
    }

    MixBus busFromStoredIndex (int stored, int storedCount) noexcept
    {
        if (stored < 0) return MixBus::Master;
        if (storedCount <= 0 || storedCount > int (MixBus::Count)) storedCount = int (MixBus::Count);
        if (stored >= storedCount) return MixBus::Master;
        // The last stored slot has always been the master, wherever it happened to sit.
        if (stored == storedCount - 1) return MixBus::Master;
        return MixBus (stored);
    }

    void mixFromVar (const juce::var& v, MixParameters& m)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        m.numStrips = juce::jlimit (0, kMaxStrips, int (obj->getProperty ("numStrips")));
        // A session saved before DLIVE measured the tempo keeps the engine default until the next Tune Mix.
        if (obj->hasProperty ("tempoBpm")) m.tempoBpm = juce::jlimit (20.0f, 300.0f, float (double (obj->getProperty ("tempoBpm"))));
        if (auto* strips = obj->getProperty ("strips").getArray())
            for (int i = 0; i < std::min (m.numStrips, strips->size()); ++i)
            {
                auto* so = strips->getReference (i).getDynamicObject();
                if (so == nullptr) continue;
                auto& s = m.strips[size_t (i)];
                channelFromVar (so->getProperty ("channel"), s.channel);
                s.inputGainDb = float (double (so->getProperty ("inputGainDb")));
                s.faderDb = float (double (so->getProperty ("faderDb")));
                s.pan = float (double (so->getProperty ("pan")));
                s.mute = bool (so->getProperty ("mute"));
                s.solo = bool (so->getProperty ("solo"));
                s.linkGroup = so->hasProperty ("linkGroup") ? juce::jmax (0, int (so->getProperty ("linkGroup"))) : 0;
                if (auto* sends = so->getProperty ("sendDb").getArray())
                    for (int f = 0; f < std::min (int (FxSlot::Count), sends->size()); ++f) s.sendDb[size_t (f)] = float (double (sends->getReference (f)));
            }
        if (auto* buses = obj->getProperty ("buses").getArray())
            for (int b = 0; b < std::min (int (MixBus::Count), buses->size()); ++b)
            {
                auto* bo = buses->getReference (b).getDynamicObject();
                if (bo == nullptr) continue;
                auto& bus = m.buses[size_t (busFromStoredIndex (b, buses->size()))];
                channelFromVar (bo->getProperty ("channel"), bus.channel);
                bus.faderDb = float (double (bo->getProperty ("faderDb")));
                bus.mute = bool (bo->getProperty ("mute"));
                bus.solo = bool (bo->getProperty ("solo"));
            }
        // The FX group's own fader and mute: a session saved before it existed has neither, and
        // 0 dB / not muted is exactly what it sounded like.
        if (obj->hasProperty ("fxReturnDb")) m.fxReturnDb = juce::jlimit (-60.0f, 12.0f, float (double (obj->getProperty ("fxReturnDb"))));
        m.fxMute = bool (obj->getProperty ("fxMute"));
        if (auto* fx = obj->getProperty ("fx").getArray())
            for (int f = 0; f < std::min (int (FxSlot::Count), fx->size()); ++f)
            {
                auto* fo = fx->getReference (f).getDynamicObject();
                if (fo == nullptr) continue;
                fxFromVar (fo->getProperty ("fx"), m.fx[size_t (f)].fx);
                m.fx[size_t (f)].returnDb = float (double (fo->getProperty ("returnDb")));
                m.fx[size_t (f)].enabled = bool (fo->getProperty ("enabled"));
                m.fx[size_t (f)].solo = bool (fo->getProperty ("solo"));
            }
        // Absent before the monitor bus existed. The defaults are the safe ones - solo goes to
        // the monitor and the live output never changes - so an older session opens safer than
        // it was saved, which is the right direction for this particular default to move.
        if (auto* mon = obj->getProperty ("monitor").getDynamicObject())
        {
            const int mode = int (mon->getProperty ("mode"));
            const int point = int (mon->getProperty ("point"));
            if (mode >= 0 && mode < int (SoloMode::Count)) m.monitor.mode = SoloMode (mode);
            if (point >= 0 && point < int (SoloPoint::Count)) m.monitor.point = SoloPoint (point);
            m.monitor.gainDb = juce::jlimit (-60.0f, 12.0f, float (double (mon->getProperty ("gainDb"))));
            m.monitor.mute = bool (mon->getProperty ("mute"));
            m.monitor.dim = bool (mon->getProperty ("dim"));
            if (mon->hasProperty ("dimDb")) m.monitor.dimDb = juce::jlimit (-40.0f, 0.0f, float (double (mon->getProperty ("dimDb"))));
            const int src = int (mon->getProperty ("source"));
            if (src >= 0 && src < int (MixBus::Count)) m.monitor.source = MixBus (src);
        }
    }

    juce::var projectToVar (const Project& p)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("sampleRate", p.sampleRate);
        obj->setProperty ("tempo", p.tempo);
        obj->setProperty ("liveSafe", p.liveSafe);
        obj->setProperty ("loopEnabled", p.loopEnabled);
        obj->setProperty ("loopStart", double (p.loopStart));
        obj->setProperty ("loopEnd", double (p.loopEnd));
        juce::Array<juce::var> tracks;
        for (const auto& t : p.tracks)
        {
            auto* to = new juce::DynamicObject();
            to->setProperty ("armed", t.armed);
            to->setProperty ("monitor", int (t.monitor));
            to->setProperty ("height", t.height);
            juce::Array<juce::var> clips;
            for (const auto& c : t.clips)
            {
                auto* co = new juce::DynamicObject();
                co->setProperty ("name", c.name);
                co->setProperty ("file", c.file);
                co->setProperty ("start", double (c.start));
                co->setProperty ("offset", double (c.offset));
                co->setProperty ("length", double (c.length));
                co->setProperty ("fileSampleRate", c.fileSampleRate);
                clips.add (juce::var (co));
            }
            to->setProperty ("clips", clips);
            tracks.add (juce::var (to));
        }
        obj->setProperty ("tracks", tracks);
        juce::Array<juce::var> markers;
        for (const auto& m : p.markers)
        {
            auto* mo = new juce::DynamicObject();
            mo->setProperty ("name", m.name);
            mo->setProperty ("position", double (m.position));
            markers.add (juce::var (mo));
        }
        obj->setProperty ("markers", markers);
        return juce::var (obj);
    }

    void projectFromVar (const juce::var& v, Project& p)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        if (obj->hasProperty ("sampleRate")) p.sampleRate = double (obj->getProperty ("sampleRate"));
        if (obj->hasProperty ("tempo")) p.tempo = double (obj->getProperty ("tempo"));
        p.liveSafe = bool (obj->getProperty ("liveSafe"));
        p.loopEnabled = bool (obj->getProperty ("loopEnabled"));
        p.loopStart = juce::int64 (double (obj->getProperty ("loopStart")));
        p.loopEnd = juce::int64 (double (obj->getProperty ("loopEnd")));
        p.tracks.clear();
        if (auto* tracks = obj->getProperty ("tracks").getArray())
            for (const auto& tv : *tracks)
            {
                TrackState t;
                if (auto* to = tv.getDynamicObject())
                {
                    t.armed = bool (to->getProperty ("armed"));
                    const int m = int (to->getProperty ("monitor"));
                    t.monitor = (m >= 0 && m < int (MonitorMode::Count)) ? MonitorMode (m) : MonitorMode::Auto;
                    if (to->hasProperty ("height")) t.height = juce::jlimit (34, 320, int (to->getProperty ("height")));
                    if (auto* clips = to->getProperty ("clips").getArray())
                        for (const auto& cv : *clips)
                            if (auto* co = cv.getDynamicObject())
                            {
                                AudioClip c;
                                c.name = co->getProperty ("name").toString();
                                c.file = co->getProperty ("file").toString();
                                c.start = juce::int64 (double (co->getProperty ("start")));
                                c.offset = juce::int64 (double (co->getProperty ("offset")));
                                c.length = juce::int64 (double (co->getProperty ("length")));
                                c.fileSampleRate = double (co->getProperty ("fileSampleRate"));
                                if (c.length > 0 && c.file.isNotEmpty()) t.clips.push_back (c);
                            }
                }
                p.tracks.push_back (t);
            }
        p.markers.clear();
        if (auto* markers = obj->getProperty ("markers").getArray())
            for (const auto& mv : *markers)
                if (auto* mo = mv.getDynamicObject())
                    p.markers.push_back ({ mo->getProperty ("name").toString(), juce::int64 (double (mo->getProperty ("position"))) });
    }

    // ---- REFERENCE MIX: the measurement of a finished recording (schema v1) ----
    juce::var referenceToVar (const ReferenceProfile& r)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("schema", r.version);
        obj->setProperty ("name", juce::String (r.name));
        obj->setProperty ("path", juce::String (r.path));   // where it was; a note for the user, never re-read on its own
        obj->setProperty ("seconds", r.seconds);
        obj->setProperty ("channels", r.channels);
        juce::Array<juce::var> bands, thirds;
        for (float v : r.bandEnergyDb) bands.add (v);
        for (float v : r.thirdOctaveDb) thirds.add (v);
        obj->setProperty ("bandEnergyDb", bands);
        obj->setProperty ("thirdOctaveDb", thirds);
        obj->setProperty ("crestFactorDb", r.crestFactorDb);
        obj->setProperty ("loudnessLufs", r.loudnessLufs);
        obj->setProperty ("truePeakDb", r.truePeakDb);
        obj->setProperty ("stereoCorrelation", r.stereoCorrelation);
        obj->setProperty ("spectralCentroidHz", r.spectralCentroidHz);
        obj->setProperty ("highFrequencyRatioDb", r.highFrequencyRatioDb);
        obj->setProperty ("tempoBpm", r.tempoBpm);
        obj->setProperty ("tempoConfidence", r.tempoConfidence);
        return juce::var (obj);
    }

    void referenceFromVar (const juce::var& v, ReferenceProfile& r)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        // A document written by a later schema is not guessed at: an unknown reference is no
        // reference, and the mix simply goes back to the profile's own target.
        const int schema = obj->hasProperty ("schema") ? int (obj->getProperty ("schema")) : 1;
        if (schema != kReferenceSchemaVersion) return;
        r.version = schema;
        r.name = obj->getProperty ("name").toString().toStdString();
        r.path = obj->getProperty ("path").toString().toStdString();
        r.seconds = float (double (obj->getProperty ("seconds")));
        r.channels = obj->hasProperty ("channels") ? int (obj->getProperty ("channels")) : 2;
        if (auto* bands = obj->getProperty ("bandEnergyDb").getArray())
            for (int i = 0; i < std::min (int (Band::Count), bands->size()); ++i)
                r.bandEnergyDb[size_t (i)] = float (double (bands->getReference (i)));
        if (auto* thirds = obj->getProperty ("thirdOctaveDb").getArray())
            for (int i = 0; i < std::min (kNumThirdOctaveBands, thirds->size()); ++i)
                r.thirdOctaveDb[size_t (i)] = float (double (thirds->getReference (i)));
        r.crestFactorDb = float (double (obj->getProperty ("crestFactorDb")));
        r.loudnessLufs = float (double (obj->getProperty ("loudnessLufs")));
        r.truePeakDb = float (double (obj->getProperty ("truePeakDb")));
        r.stereoCorrelation = float (double (obj->getProperty ("stereoCorrelation")));
        r.spectralCentroidHz = float (double (obj->getProperty ("spectralCentroidHz")));
        r.highFrequencyRatioDb = float (double (obj->getProperty ("highFrequencyRatioDb")));
        r.tempoBpm = float (double (obj->getProperty ("tempoBpm")));
        r.tempoConfidence = float (double (obj->getProperty ("tempoConfidence")));
        r.valid = ! r.name.empty() && r.seconds > 0.0f;
    }
}

namespace SessionStore
{

juce::var toVar (const Document& d)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("app", "DLIVE");
    obj->setProperty ("version", kVersion);
    obj->setProperty ("name", juce::String (d.session.name));
    obj->setProperty ("profile", int (d.session.profile));
    obj->setProperty ("purpose", int (d.session.purpose));
    obj->setProperty ("delivery", int (d.session.delivery));   // how loud the finished mix should be
    obj->setProperty ("voicing", int (d.session.voicing));     // who the finished mix is for
    if (d.trackPanelWidth > 0) obj->setProperty ("trackPanelWidth", d.trackPanelWidth);
    obj->setProperty ("inputDevice", d.inputDevice);
    obj->setProperty ("outputDevice", d.outputDevice);
    if (d.soloDevice.isNotEmpty()) obj->setProperty ("soloDevice", d.soloDevice);
    juce::Array<juce::var> inputs;
    for (const auto& in : d.session.inputs)
    {
        auto* io = new juce::DynamicObject();
        io->setProperty ("name", juce::String (in.name));
        io->setProperty ("role", int (in.role));
        io->setProperty ("roleName", channelRoleName (in.role));   // for humans reading the file; the index is authoritative
        if (! in.icon.empty()) io->setProperty ("icon", juce::String (in.icon));   // absent = drawn from the role
        io->setProperty ("inputA", in.inputA);
        io->setProperty ("inputB", in.inputB);
        io->setProperty ("enabled", in.enabled);
        inputs.add (juce::var (io));
    }
    obj->setProperty ("inputs", inputs);
    juce::Array<juce::var> macros;
    for (float v : d.macros.v) macros.add (v);
    obj->setProperty ("macros", macros);
    obj->setProperty ("tuneCount", d.tuneCount);
    obj->setProperty ("hasMix", d.hasMix);
    if (d.hasMix) obj->setProperty ("mix", mixToVar (d.mix));
    obj->setProperty ("project", projectToVar (d.project));
    // Stored for REVIEW CHANGES and for the record. Nothing reads it back into the mix: the
    // parameters that actually run are in "mix", which is the only thing the engine is given.
    if (! d.tuneLive.isVoid()) obj->setProperty ("tuneLive", d.tuneLive);
    if (d.reference.valid) obj->setProperty ("reference", referenceToVar (d.reference));
    juce::Array<juce::var> feeds;
    for (int i = 0; i < d.outputs.count && i < kMaxOutputFeeds; ++i)
    {
        const auto& f = d.outputs.feeds[size_t (i)];
        auto* fo = new juce::DynamicObject();
        fo->setProperty ("left", f.left);
        fo->setProperty ("right", f.right);
        fo->setProperty ("source", int (f.source));
        fo->setProperty ("gainDb", f.gainDb);
        fo->setProperty ("mute", f.mute);
        fo->setProperty ("mono", f.mono);
        fo->setProperty ("monitor", f.monitor);
        feeds.add (juce::var (fo));
    }
    obj->setProperty ("outputs", feeds);
    return juce::var (obj);
}

bool fromVar (const juce::var& v, Document& d)
{
    auto* obj = v.getDynamicObject();
    // Sessions written before the app was renamed say DINELIVE; they are the same document.
    const juce::String app = obj == nullptr ? juce::String() : obj->getProperty ("app").toString();
    if (app != "DLIVE" && app != "DINELIVE") return false;
    const int fileVersion = obj->hasProperty ("version") ? int (obj->getProperty ("version")) : 1;
    d = Document {};
    d.session.name = obj->getProperty ("name").toString().toStdString();
    d.session.profile = styleProfileFromIndex (int (obj->getProperty ("profile")));
    const int purpose = int (obj->getProperty ("purpose"));
    d.session.purpose = purpose >= 0 && purpose < int (MixPurpose::Count) ? MixPurpose (purpose) : MixPurpose::ChurchBroadcast;
    // Absent before the delivery loudness was a setting, and FromPurpose is exactly what those
    // sessions did: an older mix opens aiming where it always aimed.
    const int delivery = obj->hasProperty ("delivery") ? int (obj->getProperty ("delivery")) : 0;
    d.session.delivery = delivery > 0 && delivery < int (DeliveryLoudness::Count) ? DeliveryLoudness (delivery) : DeliveryLoudness::FromPurpose;
    // Absent before the master had a voicing; Neutral is what those sessions sounded like.
    const int voicing = obj->hasProperty ("voicing") ? int (obj->getProperty ("voicing")) : 0;
    d.session.voicing = voicing > 0 && voicing < int (MasterVoicing::Count) ? MasterVoicing (voicing) : MasterVoicing::Neutral;
    d.trackPanelWidth = int (obj->getProperty ("trackPanelWidth"));
    d.inputDevice = obj->getProperty ("inputDevice").toString();
    d.outputDevice = obj->getProperty ("outputDevice").toString();
    d.soloDevice = obj->getProperty ("soloDevice").toString();
    if (auto* inputs = obj->getProperty ("inputs").getArray())
        for (const auto& iv : *inputs)
        {
            auto* io = iv.getDynamicObject();
            if (io == nullptr) continue;
            InputAssignment in;
            in.name = io->getProperty ("name").toString().toStdString();
            in.role = channelRoleFromIndex (int (io->getProperty ("role")));
            in.icon = io->getProperty ("icon").toString().toStdString();
            in.inputA = int (io->getProperty ("inputA"));
            in.inputB = io->hasProperty ("inputB") ? int (io->getProperty ("inputB")) : -1;
            in.enabled = io->hasProperty ("enabled") ? bool (io->getProperty ("enabled")) : true;
            d.session.inputs.push_back (in);
        }
    if (auto* macros = obj->getProperty ("macros").getArray())
        for (int i = 0; i < std::min (int (MixMacro::Count), macros->size()); ++i) d.macros.set (MixMacro (i), float (double (macros->getReference (i))));
    d.tuneCount = int (obj->getProperty ("tuneCount"));
    d.hasMix = bool (obj->getProperty ("hasMix"));
    if (d.hasMix) mixFromVar (obj->getProperty ("mix"), d.mix);
    projectFromVar (obj->getProperty ("project"), d.project);      // absent in version 1: no timeline yet
    d.tuneLive = obj->getProperty ("tuneLive");                     // absent until a live run has been made
    referenceFromVar (obj->getProperty ("reference"), d.reference);  // absent unless the mix is aimed at a recording
    d.outputs = OutputFeeds::mainOnly();                            // absent before the outputs feature: the main pair
    if (auto* feeds = obj->getProperty ("outputs").getArray())
    {
        int n = 0;
        for (const auto& fv : *feeds)
        {
            if (n >= kMaxOutputFeeds) break;
            auto* fo = fv.getDynamicObject();
            if (fo == nullptr) continue;
            auto& f = d.outputs.feeds[size_t (n)];
            f.left = int (fo->getProperty ("left"));
            f.right = int (fo->getProperty ("right"));
            // Before version 3 there was no speech group, so a feed's source index above
            // VOCALS meant one bus lower than it does now.
            f.source = busFromStoredIndex (int (fo->getProperty ("source")), storedBusCount (fileVersion));
            f.gainDb = float (double (fo->getProperty ("gainDb")));
            f.mute = bool (fo->getProperty ("mute"));
            f.mono = bool (fo->getProperty ("mono"));
            // A feed that carries the engineer's listen rather than a bus. Absent before the
            // monitor bus existed, which is exactly right: those sessions had no monitor feed.
            f.monitor = bool (fo->getProperty ("monitor"));
            ++n;
        }
        if (n > 0) d.outputs.count = n;
    }
    d.project.syncTracks (d.session);
    return true;
}

juce::File sessionsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DLIVE");
}

juce::File legacyFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DLIVE").getChildFile ("Sessions");
}

// Where the app kept its sessions before it was called DLIVE. Nothing is written here and
// nothing is moved: the sessions that are in it are simply still listed and still open.
juce::File formerNameFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DINELIVE");
}

juce::File folderFor (const juce::String& sessionName)
{
    return sessionsFolder().getChildFile (juce::File::createLegalFileName (sessionName.isEmpty() ? "Session" : sessionName));
}

juce::File fileFor (const juce::String& sessionName)
{
    const auto folder = folderFor (sessionName);
    return folder.getChildFile (folder.getFileName() + ".dlive.json");
}

bool save (const Document& d, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (toVar (d), false));
}

bool load (const juce::File& file, Document& d)
{
    if (! file.existsAsFile()) return false;
    const juce::var v = juce::JSON::parse (file);
    if (! fromVar (v, d)) return false;
    // Recorded takes are named relative to the document, so the folder comes from where it was found.
    const auto parent = file.getParentDirectory();
    d.project.folder = parent == legacyFolder() ? juce::File() : parent;
    return true;
}

// Reads only the header of the document: the assignments tell us how the inputs fall
// across the groups, and everything else is one property. The clips and the kept mix are
// parsed by JSON::parse along with the rest, but nothing on disk beside the file is read.
Summary summarise (const juce::File& file)
{
    Summary out;
    if (! file.existsAsFile()) return out;
    const auto v = juce::JSON::parse (file);
    auto* obj = v.getDynamicObject();
    if (obj == nullptr) return out;
    const juce::String app = obj->getProperty ("app").toString();
    if (app != "DLIVE" && app != "DINELIVE") return out;
    out.valid = true;
    out.profile = styleProfileFromIndex (int (obj->getProperty ("profile")));
    const int purpose = int (obj->getProperty ("purpose"));
    out.purpose = purpose >= 0 && purpose < int (MixPurpose::Count) ? MixPurpose (purpose) : MixPurpose::ChurchBroadcast;
    out.inputDevice = obj->getProperty ("inputDevice").toString();
    out.tuneCount = int (obj->getProperty ("tuneCount"));
    out.hasMix = bool (obj->getProperty ("hasMix"));
    if (auto* inputs = obj->getProperty ("inputs").getArray())
        for (const auto& iv : *inputs)
        {
            auto* io = iv.getDynamicObject();
            if (io == nullptr) continue;
            if (io->hasProperty ("enabled") && ! bool (io->getProperty ("enabled"))) continue;
            ++out.inputs;
            const auto bus = mixBusForRole (channelRoleFromIndex (int (io->getProperty ("role"))));
            if (int (bus) < int (MixBus::Master)) ++out.perBus[size_t (bus)];
        }
    if (auto* project = obj->getProperty ("project").getDynamicObject())
        if (auto* tracks = project->getProperty ("tracks").getArray())
            for (const auto& tv : *tracks)
                if (auto* to = tv.getDynamicObject())
                    if (auto* clips = to->getProperty ("clips").getArray())
                        if (! clips->isEmpty()) ++out.tracks;
    return out;
}

juce::Array<Listing> listSessions()
{
    // Names come from the file, not from its contents: a session folder can hold hours of
    // audio, and opening the list must not read (or walk past) any of it.
    juce::Array<Listing> out;
    auto add = [&out] (const juce::File& f)
    {
        Listing L;
        L.name = f.getFileName().upToLastOccurrenceOf (".dlive.json", false, false)
                                .upToLastOccurrenceOf (".dinelive.json", false, false);
        if (L.name.isEmpty()) L.name = f.getFileNameWithoutExtension();
        L.file = f;
        L.modified = f.getLastModificationTime();
        out.add (L);
    };
    // Both extensions are listed: a session saved under the old name opens as it is, and is
    // written back beside its audio the next time it is saved.
    const char* patterns[] = { "*.dlive.json", "*.dinelive.json" };
    auto scan = [&] (const juce::File& root, bool withSubfolders)
    {
        if (! root.isDirectory()) return;
        for (const char* pattern : patterns)
        {
            if (withSubfolders)
                for (const auto& dir : root.findChildFiles (juce::File::findDirectories, false))
                    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, pattern))
                        add (f);
            for (const auto& f : root.findChildFiles (juce::File::findFiles, false, pattern))
                add (f);
        }
    };
    scan (sessionsFolder(), true);
    scan (formerNameFolder(), true);
    scan (legacyFolder(), false);
    std::sort (out.begin(), out.end(), [] (const Listing& a, const Listing& b) { return a.modified > b.modified; });
    return out;
}

} // namespace SessionStore
} // namespace livemix
