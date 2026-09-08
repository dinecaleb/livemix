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
}

namespace SessionStore
{

juce::var toVar (const Document& d)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("app", "DINELIVE");
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
    return juce::var (obj);
}

bool fromVar (const juce::var& v, Document& d)
{
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || obj->getProperty ("app").toString() != "DINELIVE") return false;
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
    return true;
}

juce::File sessionsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DINELIVE").getChildFile ("Sessions");
}

juce::File fileFor (const juce::String& sessionName)
{
    return sessionsFolder().getChildFile (juce::File::createLegalFileName (sessionName.isEmpty() ? "Session" : sessionName) + ".dinelive.json");
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
    return fromVar (v, d);
}

juce::Array<Listing> listSessions()
{
    juce::Array<Listing> out;
    const auto folder = sessionsFolder();
    if (! folder.isDirectory()) return out;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.dinelive.json"))
    {
        Document d;
        if (! load (f, d)) continue;
        Listing L;
        L.name = juce::String (d.session.name);
        if (L.name.isEmpty()) L.name = f.getFileNameWithoutExtension();
        L.file = f;
        L.modified = f.getLastModificationTime();
        out.add (L);
    }
    std::sort (out.begin(), out.end(), [] (const Listing& a, const Listing& b) { return a.modified > b.modified; });
    return out;
}

} // namespace SessionStore
} // namespace livemix
