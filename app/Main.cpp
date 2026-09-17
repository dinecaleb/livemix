// DLIVE: the live recording and broadcast DAW.
//   Console / interface / Dante -> DLIVE -> OBS / Ecamm / recording
// One MixController owns the mix, one DawEngine owns the timeline and the recorder, one
// AudioHost owns the device, MainView shows one workspace at a time. A session is a folder
// with its recordings inside; the last one reloads on launch.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "native/AudioHost.h"
#include "native/DawEngine.h"
#include "native/MixBounce.h"
#include "native/MixController.h"
#include "native/MultitrackImport.h"
#include "native/SessionStore.h"
#include "native/MonitorDevice.h"
#include "ui/MainView.h"
#include <optional>

using namespace livemix;

namespace
{
    juce::File lastSessionPointer()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("DLIVE").getChildFile ("last-session.txt");
    }

    class HostServices : public AppServices
    {
    public:
        HostServices (MixController& c, DawEngine& d, AudioHost& h) : controller (c), dawEngine (d), host (h) {}

        DawEngine& daw() override { return dawEngine; }

        juce::Array<Device> inputDevices() override
        {
            juce::Array<Device> out;
            for (const auto& d : host.listInputDevices()) out.add ({ d.name, d.inputChannels, d.outputChannels });
            return out;
        }
        juce::Array<Device> outputDevices() override
        {
            juce::Array<Device> out;
            for (const auto& d : host.listOutputDevices()) out.add ({ d.name, d.inputChannels, d.outputChannels });
            return out;
        }
        juce::String openDevices (const juce::String& input, const juce::String& output) override
        {
            hold();
            const auto err = host.open (input, output);
            applyPendingMix();
            return err;
        }
        juce::String openOutputOnly (const juce::String& output) override
        {
            hold();
            const auto err = host.openOutputOnly (output);
            applyPendingMix();
            return err;
        }

        juce::String changeOutput (const juce::String& output) override
        {
            // Changing the broadcast while solo is set up is not a device swap - it is the same
            // pairing with a different half, so the joined device has to be rebuilt around it.
            // Without this, choosing a new broadcast from the toolbar would silently drop the
            // engineer's listen (or, worse, keep pointing it at the old device's channels).
            if (soloDevice.isNotEmpty() && output != soloDevice)
            {
                broadcastDevice = output;
                const auto again = setSoloOutputDevice (soloDevice);
                return again.ok ? juce::String() : again.message;
            }

            // Snapshot the mix, swap the device (prepare rebuilds the graph), then put the mix back.
            hold();
            const juce::String err = host.setOutputDevice (output);
            applyPendingMix();
            if (err.isEmpty()) { broadcastDevice = {}; saveSession(); }
            return err;
        }

        // The name every piece of chrome shows. While two devices are joined the open device is
        // DLIVE's own; what the user picked is the broadcast, and that is what they are told.
        juce::String outputDisplayName() override
        {
            const auto broadcast = broadcastOutputDevice();
            if (soloDevice.isEmpty() || soloDevice == broadcast) return broadcast;
            return broadcast + "  +  " + soloDevice;
        }

        bool isAudioRunning() override { return host.isOpen(); }
        int numInputChannels() override { return host.getNumInputChannels(); }
        int numOutputChannels() override { return host.getNumOutputChannels(); }
        juce::StringArray outputChannelNames() override { return host.getOutputChannelNames(); }
        double sampleRate() override { return host.getSampleRate(); }
        int bufferSize() override { return host.getBufferSize(); }
        int xrunCount() override { return host.getXRunCount(); }
        bool deviceStopped() override { return host.deviceStoppedUnexpectedly(); }
        void reconfigure() override
        {
            hold();
            // The assignments are what changed, so the timeline hears about them first: every
            // track follows its own input, and the clips stay with the source they were
            // recorded from instead of sliding under the next one's name.
            dawEngine.setSession (controller.getSession());
            host.reconfigure();
            applyPendingMix();
        }
        juce::String currentInputDevice() override { return host.getInputDeviceName(); }
        juce::String currentOutputDevice() override { return host.getOutputDeviceName(); }
        juce::String currentSessionName() override { return juce::String (controller.getSession().name); }
        juce::File sessionFolder() override { return dawEngine.getProject().folder; }

        juce::String importMultitrack (const juce::File& folder) override
        {
            auto result = MultitrackImport::fromFolder (folder, controller.getSession());
            if (result.error.isNotEmpty()) return result.error;

            controller.setSession (result.session);
            dawEngine.setSession (result.session);
            result.project.folder = dawEngine.getProject().folder;   // keep the session's own folder, if it has one
            dawEngine.setProject (result.project);

            // Imported audio plays through the same graph as a console, so an output is all that is needed.
            if (! host.isOpen())
            {
                juce::String output = host.getOutputDeviceName();
                if (output.isEmpty()) { const auto outs = host.listOutputDevices(); if (! outs.isEmpty()) output = outs[0].name; }
                if (output.isNotEmpty()) host.openOutputOnly (output, result.sampleRate > 0.0 ? result.sampleRate : 48000.0);
            }
            else
            {
                host.reconfigure();
            }
            dawEngine.setSession (result.session);
            dawEngine.setProject (result.project);
            saveSession();
            return {};
        }

        std::shared_ptr<const ExportJob> snapshotExport() override
        {
            auto job = std::make_shared<ExportJob>();
            job->session = controller.getSession();
            job->params = controller.getRunning();
            job->project = dawEngine.getProject();
            return job;
        }

        juce::String exportMix (std::shared_ptr<const ExportJob> job, const juce::File& dest,
                                ExportFormat format, std::function<bool (float)> progress) override
        {
            if (job == nullptr) return "There is nothing to export.";
            MixBounce::Options options;
            options.onProgress = std::move (progress);
            return MixBounce::renderProject (job->session, job->params, job->project, dest,
                                             format == ExportFormat::Mp3 ? MixBounce::Format::Mp3 : MixBounce::Format::Wav,
                                             options);
        }

        void newSession() override
        {
            MixSession fresh;
            fresh.name = "Untitled";
            controller.setSession (fresh);
            controller.clearReference();
            dawEngine.setSession (fresh);
            dawEngine.setProject (Project {});
            dawEngine.locate (0);
            if (host.isOpen()) host.reconfigure();
        }

        void saveSession() override
        {
            if (controller.getSession().inputs.empty()) return;
            auto file = documentFile();
            if (file == juce::File()) file = SessionStore::fileFor (juce::String (controller.getSession().name));
            writeDocument (file);
        }

        juce::String saveSessionAs (const juce::String& name) override
        {
            juce::String n = name.trim();
            if (n.isEmpty()) return "Give the session a name.";
            controller.setSessionName (n.toStdString());
            const auto file = SessionStore::fileFor (n);
            // Moving to a new folder: the takes stay where they are, and their clips keep absolute paths.
            const auto oldFolder = dawEngine.getProject().folder;
            if (oldFolder != juce::File() && oldFolder != file.getParentDirectory())
                for (auto& track : dawEngine.getProject().tracks)
                    for (auto& clip : track.clips)
                        if (! juce::File::isAbsolutePath (clip.file))
                            clip.file = oldFolder.getChildFile ("Audio Files").getChildFile (clip.file).getFullPathName();
            dawEngine.getProject().folder = file.getParentDirectory();
            if (! writeDocument (file)) return "Could not save the session.";
            return {};
        }

        juce::String loadSession (const juce::File& file) override
        {
            SessionStore::Document doc;
            if (! SessionStore::load (file, doc)) return "That file is not a DLIVE session.";
            controller.setSession (doc.session);
            dawEngine.setSession (doc.session);
            dawEngine.setProject (doc.project);
            panelWidth = doc.trackPanelWidth;
            pending = doc;

            juce::String err;
            if (doc.inputDevice.isNotEmpty())
                err = host.open (doc.inputDevice, doc.outputDevice.isNotEmpty() ? doc.outputDevice : doc.inputDevice);
            else if (doc.project.hasAudio() && doc.outputDevice.isNotEmpty())
                err = host.openOutputOnly (doc.outputDevice);
            else if (host.isOpen())
                host.reconfigure();
            applyPendingMix();
            dawEngine.setSession (doc.session);
            dawEngine.setProject (doc.project);
            dawEngine.locate (0);
            if (err.isEmpty()) lastSessionPointer().replaceWithText (file.getFullPathName());
            return err;
        }

        juce::Array<SessionStore::Listing> listSessions() override { return SessionStore::listSessions(); }

        // ---- Two outputs: one for the broadcast, one for the engineer ----
        //
        // From the outside this is two pickers. Underneath there are three cases, and the
        // point of doing it here rather than in the UI is that none of them reach the user:
        //
        //   solo on the same device   - it has spare outputs, so solo takes a second pair
        //   solo on another device    - macOS opens one device at a time, so DLIVE builds the
        //                               combined device, reopens on it, and routes a pair each
        //   solo turned off           - the combined device is removed and the Mac is put back
        //
        // The mix survives all three the way it survives any other device change: held before,
        // put back after.
        bool canCombineOutputs() override { return MonitorDevice::available(); }

        juce::String broadcastOutputDevice() override
        {
            // While the combined device is open, the broadcast is the device inside it that
            // the user actually chose - not the one DLIVE built around it.
            return broadcastDevice.isNotEmpty() ? broadcastDevice : host.getOutputDeviceName();
        }

        juce::String soloOutputDevice() override { return soloDevice; }

        MonitorSetup setSoloOutputDevice (const juce::String& wanted) override
        {
            const auto broadcast = broadcastOutputDevice();
            // Taken now, because a failed open closes the device and forgets it - and putting
            // the console back afterwards is the whole point of the fallback path.
            const auto input = host.getInputDeviceName();

            // ---- turn it off: put the Mac back the way it was found
            if (wanted.isEmpty())
            {
                soloDevice = {};
                auto feeds = controller.getOutputFeeds();
                for (int i = 0; i < juce::jmin (feeds.count, int (kMaxOutputFeeds)); ++i)
                    if (feeds.feeds[size_t (i)].monitor) feeds.feeds[size_t (i)].left = feeds.feeds[size_t (i)].right = -1;
                controller.setOutputFeeds (feeds);
                if (MonitorDevice::dliveDeviceExists())
                {
                    hold();
                    MonitorDevice::removeDliveDevice();
                    host.rescanDevices();          // the device it was open on has just gone
                    const auto err = openWith (broadcast, input);
                    applyPendingMix();
                    if (err.isNotEmpty()) return { false, err };
                }
                broadcastDevice = {};
                saveSession();
                return { true, "Solo is switched off. Everything goes out of " + broadcast + " as before." };
            }

            // ---- same device: solo just takes another pair of it
            if (wanted == broadcast)
            {
                if (numOutputChannels() < 4)
                    return { false, broadcast + " has only one pair of outputs, so there is nowhere separate for "
                                    "solo to go. Choose a different device - DLIVE will join the two for you." };
                soloDevice = wanted;
                broadcastDevice = broadcast;
                routeOutputs (0, 2);
                saveSession();
                return { true, "Solo goes to outputs 3-4 of " + broadcast + ". The stream is on 1-2 and never changes." };
            }

            // ---- two devices: DLIVE builds the combined one
            if (! MonitorDevice::available())
                return { false, "This Mac will not let DLIVE join two output devices. "
                                "You can make an Aggregate Device yourself in Audio MIDI Setup and choose it above." };

            MonitorDevice::Device broadcastDev, soloDev;
            for (const auto& d : MonitorDevice::outputDevices())
            {
                if (d.isDliveBuilt) continue;
                if (d.name == broadcast) broadcastDev = d;
                if (d.name == wanted) soloDev = d;
            }
            if (broadcastDev.uid.isEmpty() || soloDev.uid.isEmpty())
                return { false, "One of those devices is no longer connected." };

            const auto built = MonitorDevice::combine (broadcastDev, soloDev);
            if (! built.ok) return { false, built.error };

            hold();
            // The device exists in CoreAudio the moment it is created, but it is published
            // asynchronously and JUCE caches a device list per type - so without waiting for it
            // and asking again, opening it fails with "No such device" on the device DLIVE has
            // just built. This is the whole reason the first attempt at this did not work.
            const bool appeared = host.waitForOutputDevice (built.deviceName);
            const auto err = appeared ? openWith (built.deviceName, input)
                                      : juce::String ("this Mac did not publish it in time");
            if (err.isNotEmpty())
            {
                // It would not open. Leave nothing behind and put the old device back.
                MonitorDevice::removeDliveDevice();
                host.rescanDevices();
                openWith (broadcast, input);
                applyPendingMix();
                return { false, "Those two could not be joined (" + err + "). Nothing has been changed. "
                                "Some devices - Bluetooth especially - refuse to be combined; try a wired one." };
            }
            applyPendingMix();

            // It opened - but if it came back without a second pair there is nowhere for solo
            // to go, and a silent solo with no explanation is worse than a refusal.
            if (numOutputChannels() < built.headphoneChannel + 2)
            {
                MonitorDevice::removeDliveDevice();
                host.rescanDevices();
                openWith (broadcast, input);
                applyPendingMix();
                return { false, "The two were joined but came back with only "
                                + juce::String (numOutputChannels()) + " outputs, so there is no separate pair "
                                "for solo. Nothing has been changed." };
            }

            broadcastDevice = broadcast;
            soloDevice = wanted;
            routeOutputs (built.broadcastChannel, built.headphoneChannel);
            saveSession();
            return { true, built.summary };
        }

        juce::String headphonesSummary() override
        {
            if (soloDevice.isEmpty()) return {};
            return "Solo goes to " + soloDevice + ". The stream stays on " + broadcastOutputDevice()
                 + " and never changes.";
        }

        int trackPanelWidth() override { return panelWidth; }
        void setTrackPanelWidth (int px) override { panelWidth = px; }

        // Restoring a mix that a device change is about to wipe.
        void holdMix (const SessionStore::Document& doc) { pending = doc; }
        void applyPendingMix()
        {
            if (! pending.has_value() || ! controller.isPrepared()) return;
            controller.setOutputFeeds (pending->outputs);   // routing belongs to the device, not the mix
            controller.setReference (pending->reference);   // always, so one session's reference never follows another
            if (pending->hasMix)
            {
                // The mix follows its input across a rebuild, the way the timeline's clips
                // already do: a channel moved, dropped or added on ASSIGN or on TRACKS leaves
                // every other channel's chain, gain, fader and sends exactly where they were.
                // getKept() here is the rebuilt session's baselines, so an input that is new to
                // the session - or one that became a different source - starts from its own.
                controller.carryKept (pending->mix, pending->session, pending->tuneCount);
                for (int i = 0; i < int (MixMacro::Count); ++i) controller.setMacro (MixMacro (i), pending->macros.get (MixMacro (i)));
            }
            pending.reset();
        }

    private:
        void hold()
        {
            SessionStore::Document snap;
            // The session the snapshotted mix belongs to - the one the graph still runs - not
            // the document, which may already have been replaced by the change we are holding
            // the mix across.
            snap.session = controller.getPreparedSession();
            snap.macros = controller.getMacros();
            snap.outputs = controller.getOutputFeeds();
            snap.tuneCount = controller.getTuneCount();
            // The kept mix outlives setSession (only prepare() clears it), so a snapshot taken
            // after the assignments changed still has the faders and chains to put back.
            snap.hasMix = controller.hasKeptMix();
            if (snap.hasMix) snap.mix = controller.getKept();
            else if (pending.has_value() && pending->hasMix) snap = *pending;
            pending = snap;
        }

        juce::File documentFile() const
        {
            const auto folder = dawEngine.getProject().folder;
            if (folder == juce::File()) return {};
            return folder.getChildFile (folder.getFileName() + ".dlive.json");
        }

        bool writeDocument (const juce::File& file)
        {
            SessionStore::Document d;
            d.session = controller.getSession();
            d.project = dawEngine.getProject();
            d.inputDevice = host.getInputDeviceName();
            d.outputDevice = host.getOutputDeviceName();
            d.macros = controller.getMacros();
            d.outputs = controller.getOutputFeeds();
            d.tuneCount = controller.getTuneCount();
            d.hasMix = controller.isPrepared() && controller.hasKeptMix();
            if (d.hasMix) d.mix = controller.getKept();
            d.reference = controller.getReference();      // what the mix is aimed at, already measured
            d.trackPanelWidth = panelWidth;
            if (controller.getTuneLive().getState() == TuneLiveCoordinator::State::Ready)
            {
                auto record = juce::JSON::parse (juce::String (controller.getTuneLive().toJson().write()));
                if (auto* o = record.getDynamicObject())
                {
                    juce::Array<juce::var> review;
                    for (const auto& line : controller.getTuneLive().getReviewLines()) review.add (juce::String (line));
                    o->setProperty ("review", review);
                }
                d.tuneLive = record;
            }
            else if (pending.has_value()) d.tuneLive = pending->tuneLive;
            else if (pending.has_value() && pending->hasMix) { d.hasMix = true; d.mix = pending->mix; d.tuneCount = pending->tuneCount; }
            if (! SessionStore::save (d, file)) return false;
            dawEngine.getProject().folder = file.getParentDirectory();
            lastSessionPointer().replaceWithText (file.getFullPathName());
            return true;
        }

        // Reopen on an output device, keeping whatever input is already in use. A session built
        // from imported stems has no console attached at all, and opening with an empty input
        // name is a different call - getting that wrong is how "it just would not open" happens
        // to the person who has no desk plugged in.
        juce::String openWith (const juce::String& outputDevice, const juce::String& inputDevice)
        {
            return inputDevice.isNotEmpty() ? host.open (inputDevice, outputDevice)
                                            : host.openOutputOnly (outputDevice);
        }

        // Feed 0 is the broadcast, feed 1 is the engineer's listen. Written in one place so the
        // three ways of setting solo up cannot end up disagreeing about which pair is which.
        void routeOutputs (int broadcastChannel, int soloChannel)
        {
            auto feeds = controller.getOutputFeeds();
            feeds.count = 2;
            feeds.feeds[0].monitor = false;
            feeds.feeds[0].source = MixBus::Master;
            feeds.feeds[0].left = broadcastChannel;
            feeds.feeds[0].right = broadcastChannel + 1;
            feeds.feeds[0].mono = false;          // the broadcast is never summed
            feeds.feeds[0].mute = false;
            feeds.feeds[1].monitor = true;
            feeds.feeds[1].left = soloChannel;
            feeds.feeds[1].right = soloChannel + 1;
            feeds.feeds[1].mono = false;
            feeds.feeds[1].mute = false;
            controller.setOutputFeeds (feeds);
        }

        MixController& controller;
        DawEngine& dawEngine;
        AudioHost& host;
        std::optional<SessionStore::Document> pending;
        int panelWidth = 0;             // TRACKS channel panel; 0 = the page's own default
        // The two devices the user chose. While a combined device is open, the *open* device is
        // DLIVE's own and these are what the user actually picked.
        juce::String broadcastDevice, soloDevice;
    };

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, MixController& c, AppServices& s)
            : juce::DocumentWindow (name, Dine::desk, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainView (c, s), true);
            setResizable (true, true);
            setResizeLimits (1180, 760, 6000, 4000);
            centreWithSize (1520, 960);
            setVisible (true);
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (view().getMenuModel());
           #endif
        }
        ~MainWindow() override
        {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (nullptr);
           #endif
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainView& view() { return *dynamic_cast<MainView*> (getContentComponent()); }
    };
}

class DLiveApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DLIVE"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        controller = std::make_unique<MixController>();
        dawEngine = std::make_unique<DawEngine> (*controller);
        host = std::make_unique<AudioHost> (*controller, *dawEngine);
        services = std::make_unique<HostServices> (*controller, *dawEngine, *host);

        SessionStore::Document doc;
        const auto pointer = lastSessionPointer();
        const bool restored = pointer.existsAsFile()
                              && SessionStore::load (juce::File (pointer.loadFileAsString().trim()), doc);
        if (restored)
        {
            controller->setSession (doc.session);
            dawEngine->setSession (doc.session);
            dawEngine->setProject (doc.project);
        }

        window = std::make_unique<MainWindow> (getApplicationName(), *controller, *services);

        if (restored)
        {
            services->holdMix (doc);
            const bool opened = doc.inputDevice.isNotEmpty()
                                    ? host->open (doc.inputDevice, doc.outputDevice).isEmpty()
                                    : (doc.project.hasAudio() && doc.outputDevice.isNotEmpty()
                                           && host->openOutputOnly (doc.outputDevice).isEmpty());
            if (opened)
            {
                services->applyPendingMix();
                dawEngine->setSession (doc.session);
                dawEngine->setProject (doc.project);
                window->view().showPage (controller->getSession().inputs.empty() ? MainView::Page::Assign
                                                                                 : MainView::Page::Tracks);
            }
        }
    }

    void shutdown() override
    {
        if (dawEngine != nullptr) dawEngine->stop();
        if (services != nullptr && controller != nullptr && ! controller->getSession().inputs.empty()) services->saveSession();
        window.reset();
        host.reset();
        services.reset();
        dawEngine.reset();
        controller.reset();
    }

    // Quitting mid-take would end the service's recording without a word. The take is
    // always flushed (shutdown() stops the engine first), but ending it has to be a
    // decision, not an accident - so the question says exactly what happens either way.
    void systemRequestedQuit() override
    {
        if (dawEngine == nullptr || ! dawEngine->isRecording()) { quit(); return; }

        auto* alert = new juce::AlertWindow ("DLIVE is recording",
                                             "Quitting stops the take and closes the session. Everything recorded so far "
                                             "is written to the session's Audio Files folder and kept on the timeline.",
                                             juce::MessageBoxIconType::NoIcon);
        alert->addButton ("Keep Recording", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        alert->addButton ("Stop and Quit", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert] (int r)
        {
            std::unique_ptr<juce::AlertWindow> closer (alert);
            if (r == 1) quit();
        }), true);
    }

private:
    std::unique_ptr<MixController> controller;
    std::unique_ptr<DawEngine> dawEngine;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<HostServices> services;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (DLiveApplication)
