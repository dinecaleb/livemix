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
            fx.add (juce::var (fo));
        }
        obj->setProperty ("fx", fx);
        return juce::var (obj);
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
                if (auto* sends = so->getProperty ("sendDb").getArray())
                    for (int f = 0; f < std::min (int (FxSlot::Count), sends->size()); ++f) s.sendDb[size_t (f)] = float (double (sends->getReference (f)));
            }
        if (auto* buses = obj->getProperty ("buses").getArray())
            for (int b = 0; b < std::min (int (MixBus::Count), buses->size()); ++b)
            {
                auto* bo = buses->getReference (b).getDynamicObject();
                if (bo == nullptr) continue;
                channelFromVar (bo->getProperty ("channel"), m.buses[size_t (b)].channel);
                m.buses[size_t (b)].faderDb = float (double (bo->getProperty ("faderDb")));
                m.buses[size_t (b)].mute = bool (bo->getProperty ("mute"));
                m.buses[size_t (b)].solo = bool (bo->getProperty ("solo"));
            }
        if (auto* fx = obj->getProperty ("fx").getArray())
            for (int f = 0; f < std::min (int (FxSlot::Count), fx->size()); ++f)
            {
                auto* fo = fx->getReference (f).getDynamicObject();
                if (fo == nullptr) continue;
                fxFromVar (fo->getProperty ("fx"), m.fx[size_t (f)].fx);
                m.fx[size_t (f)].returnDb = float (double (fo->getProperty ("returnDb")));
                m.fx[size_t (f)].enabled = bool (fo->getProperty ("enabled"));
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
    obj->setProperty ("inputDevice", d.inputDevice);
    obj->setProperty ("outputDevice", d.outputDevice);
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
    d = Document {};
    d.session.name = obj->getProperty ("name").toString().toStdString();
    d.session.profile = styleProfileFromIndex (int (obj->getProperty ("profile")));
    const int purpose = int (obj->getProperty ("purpose"));
    d.session.purpose = purpose >= 0 && purpose < int (MixPurpose::Count) ? MixPurpose (purpose) : MixPurpose::ChurchBroadcast;
    d.inputDevice = obj->getProperty ("inputDevice").toString();
    d.outputDevice = obj->getProperty ("outputDevice").toString();
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
            const int src = int (fo->getProperty ("source"));
            f.source = src >= 0 && src < int (MixBus::Count) ? MixBus (src) : MixBus::Master;
            f.gainDb = float (double (fo->getProperty ("gainDb")));
            f.mute = bool (fo->getProperty ("mute"));
            f.mono = bool (fo->getProperty ("mono"));
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
