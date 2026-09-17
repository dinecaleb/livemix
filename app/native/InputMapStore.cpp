#include "InputMapStore.h"
#include "SessionStore.h"

namespace livemix
{

namespace
{
    juce::String legalName (const juce::String& n)
    {
        return juce::File::createLegalFileName (n.trim().isEmpty() ? "Input Map" : n.trim());
    }
}

namespace InputMapStore
{

juce::File folder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("DLIVE").getChildFile ("Input Maps");
}

juce::File fileFor (const juce::String& mapName)
{
    return folder().getChildFile (legalName (mapName) + ".dlivemap.json");
}

InputMap fromSession (const MixSession& s, const juce::String& name, const juce::String& deviceName,
                      int deviceInputCount, bool includeSound)
{
    InputMap m;
    m.name = name.trim().isEmpty() ? juce::String (s.name) : name.trim();
    m.modified = juce::Time::getCurrentTime();
    m.deviceName = deviceName;
    m.deviceInputCount = deviceInputCount;
    m.inputs = s.inputs;
    m.hasSound = includeSound;
    m.profile = s.profile;
    m.purpose = s.purpose;
    m.delivery = s.delivery;
    return m;
}

juce::var toVar (const InputMap& m)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("app", "DLIVE");
    obj->setProperty ("kind", "inputMap");
    obj->setProperty ("schema", InputMap::kSchemaVersion);
    obj->setProperty ("name", m.name);
    obj->setProperty ("note", m.note);
    obj->setProperty ("device", m.deviceName);
    obj->setProperty ("deviceInputs", m.deviceInputCount);
    obj->setProperty ("hasSound", m.hasSound);
    obj->setProperty ("profile", int (m.profile));
    obj->setProperty ("purpose", int (m.purpose));
    obj->setProperty ("delivery", int (m.delivery));
    juce::Array<juce::var> inputs;
    for (const auto& in : m.inputs)
    {
        auto* io = new juce::DynamicObject();
        io->setProperty ("name", juce::String (in.name));
        io->setProperty ("role", int (in.role));
        io->setProperty ("inputA", in.inputA);
        io->setProperty ("inputB", in.inputB);
        io->setProperty ("enabled", in.enabled);
        if (! in.icon.empty()) io->setProperty ("icon", juce::String (in.icon));
        inputs.add (juce::var (io));
    }
    obj->setProperty ("inputs", inputs);
    return juce::var (obj);
}

bool fromVar (const juce::var& v, InputMap& m)
{
    auto* obj = v.getDynamicObject();
    if (obj == nullptr) return false;
    // A document from a schema this build does not know is ignored rather than half-read.
    const int schema = obj->hasProperty ("schema") ? int (obj->getProperty ("schema")) : 0;
    if (schema <= 0 || schema > InputMap::kSchemaVersion) return false;
    if (obj->getProperty ("kind").toString() != "inputMap") return false;

    m = InputMap {};
    m.name = obj->getProperty ("name").toString();
    m.note = obj->getProperty ("note").toString();
    m.deviceName = obj->getProperty ("device").toString();
    m.deviceInputCount = int (obj->getProperty ("deviceInputs"));
    m.hasSound = bool (obj->getProperty ("hasSound"));
    const int profile = int (obj->getProperty ("profile"));
    const int purpose = int (obj->getProperty ("purpose"));
    const int delivery = int (obj->getProperty ("delivery"));
    if (profile >= 0 && profile < int (StyleProfileId::Count)) m.profile = StyleProfileId (profile);
    if (purpose >= 0 && purpose < int (MixPurpose::Count)) m.purpose = MixPurpose (purpose);
    if (delivery > 0 && delivery < int (DeliveryLoudness::Count)) m.delivery = DeliveryLoudness (delivery);

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
            in.icon = io->getProperty ("icon").toString().toStdString();
            m.inputs.push_back (in);
            if (int (m.inputs.size()) >= kMaxStrips) break;
        }
    return m.valid();
}

bool save (const InputMap& m)
{
    auto copy = m;
    copy.modified = juce::Time::getCurrentTime();
    return saveAs (copy, fileFor (copy.name));
}

bool saveAs (const InputMap& m, const juce::File& destination)
{
    if (! m.valid()) return false;
    destination.getParentDirectory().createDirectory();
    return destination.replaceWithText (juce::JSON::toString (toVar (m), false));
}

bool load (const juce::File& file, InputMap& m)
{
    if (! file.existsAsFile()) return false;
    if (! fromVar (juce::JSON::parse (file), m)) return false;
    m.modified = file.getLastModificationTime();
    if (m.name.isEmpty()) m.name = file.getFileNameWithoutExtension().upToLastOccurrenceOf (".dlivemap", false, false);
    return true;
}

juce::Array<Listing> list()
{
    juce::Array<Listing> out;
    const auto dir = folder();
    if (! dir.isDirectory()) return out;
    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.dlivemap.json"))
    {
        InputMap m;
        if (! load (file, m)) continue;
        Listing l;
        l.name = m.name;
        l.note = m.note;
        l.file = file;
        l.modified = m.modified;
        l.inputCount = int (m.inputs.size());
        l.channelsNeeded = m.channelsNeeded();
        l.deviceName = m.deviceName;
        for (const auto& in : m.inputs)
            if (in.enabled) ++l.perBus[size_t (mixBusForRole (in.role))];
        out.add (l);
    }
    // Newest first, the way the session library lists.
    struct Newest
    {
        static int compareElements (const Listing& a, const Listing& b)
        {
            return a.modified > b.modified ? -1 : (a.modified < b.modified ? 1 : 0);
        }
    };
    Newest sorter;
    out.sort (sorter, true);
    return out;
}

bool remove (const juce::String& mapName)
{
    const auto f = fileFor (mapName);
    return f.existsAsFile() && f.deleteFile();
}

juce::String rename (const juce::String& from, const juce::String& to)
{
    if (to.trim().isEmpty()) return "A map needs a name.";
    InputMap m;
    if (! load (fileFor (from), m)) return "That map could not be read.";
    const auto dest = fileFor (to);
    if (dest.existsAsFile() && dest != fileFor (from)) return "A map called \"" + to.trim() + "\" already exists.";
    m.name = to.trim();
    if (! saveAs (m, dest)) return "That map could not be saved.";
    if (dest != fileFor (from)) fileFor (from).deleteFile();
    return {};
}

juce::String duplicate (const juce::String& from, const juce::String& to)
{
    InputMap m;
    if (! load (fileFor (from), m)) return "That map could not be read.";
    if (fileFor (to).existsAsFile()) return "A map called \"" + to.trim() + "\" already exists.";
    m.name = to.trim().isEmpty() ? from + " copy" : to.trim();
    return saveAs (m, fileFor (m.name)) ? juce::String() : "That map could not be saved.";
}

// ---------------------------------------------------------------------------
// Applying a map
// ---------------------------------------------------------------------------
ApplyResult apply (const InputMap& m, const MixSession& current, int availableInputs,
                   const juce::String& deviceName, bool applySound)
{
    ApplyResult r;
    r.session = current;
    r.session.inputs.clear();

    if (applySound && m.hasSound)
    {
        r.session.profile = m.profile;
        r.session.purpose = m.purpose;
        r.session.delivery = m.delivery;
    }

    // Two inputs on one device channel is a map that was edited by hand or built on a desk
    // whose channels have since been re-used. It is reported, and the second one loses.
    std::array<bool, kMaxInputs> claimed {};
    auto claim = [&] (int ch) -> bool
    {
        if (ch < 0 || ch >= kMaxInputs) return true;
        if (claimed[size_t (ch)]) return false;
        claimed[size_t (ch)] = true;
        return true;
    };

    for (const auto& in : m.inputs)
    {
        if (int (r.session.inputs.size()) >= kMaxStrips) break;
        InputAssignment out = in;
        const int needed = juce::jmax (in.inputA, in.inputB) + 1;

        // The device cannot provide this channel. The input is kept - name, source and all -
        // and switched off, so the ASSIGN page shows exactly what is missing instead of the
        // audio arriving from whatever happens to be on that channel number.
        if (availableInputs > 0 && needed > availableInputs)
        {
            out.enabled = false;
            ++r.unavailable;
        }
        else if (! claim (in.inputA) || (in.isStereo() && ! claim (in.inputB)))
        {
            out.enabled = false;
            ++r.conflicts;
        }
        else
        {
            ++r.restored;
        }
        r.session.inputs.push_back (out);
    }

    if (r.unavailable > 0)
        r.problems.push_back (juce::String (r.unavailable) + (r.unavailable == 1 ? " input needs" : " inputs need")
                              + " a device channel this one does not have. This map wants "
                              + juce::String (m.channelsNeeded()) + " channels"
                              + (m.deviceName.isNotEmpty() ? " (it was made on \"" + m.deviceName + "\")" : juce::String())
                              + "; \"" + (deviceName.isNotEmpty() ? deviceName : juce::String ("this device")) + "\" has "
                              + juce::String (availableInputs) + ". Those inputs are switched off rather than moved, "
                              "so nothing is routed to the wrong place.");
    if (r.conflicts > 0)
        r.problems.push_back (juce::String (r.conflicts) + (r.conflicts == 1 ? " input wants" : " inputs want")
                              + " a device channel another input in this map has already taken. "
                              "The later one is switched off; put it right on the INPUTS page.");
    if (availableInputs <= 0)
        r.problems.push_back ("No audio device is open, so DLIVE cannot check that these channels exist. "
                              "Open the device and check the INPUTS page before the service.");

    return r;
}

} // namespace InputMapStore
} // namespace livemix
