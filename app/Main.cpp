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
#include "native/SessionAutosave.h"
#include "native/SessionState.h"
#include "native/SessionStore.h"
#include "native/SampleLibrary.h"
#include "native/DevicePlan.h"
#include "native/MicPermission.h"
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

        // The drum sounds. Set once, after the library has loaded: it is how a strip's chosen
        // sound is written down by name and found again by name.
        void setSampleLibrary (SampleLibrary& library) { samples = &library; }

        // The sounds this session carries. A session folder holds its own Samples/, so a kick
        // somebody imported travels with the service rather than living in one Mac's ~/Music.
        void reloadSamplesForSession()
        {
            if (samples == nullptr) return;
            samples->setSessionFolder (dawEngine.getProject().folder);
            samples->load();
            controller.setSampleBanks (samples->table());
        }

        juce::String importSample (RoleFamily family, const juce::File& file) override
        {
            if (samples == nullptr) return "Sounds are not available here.";
            samples->setSessionFolder (dawEngine.getProject().folder);
            juce::String problem;
            const auto name = samples->importSound (family, file, problem);
            if (name.isEmpty()) return problem;
            controller.setSampleBanks (samples->table());
            touchSession();
            return "\"" + name + "\" is in this session's sounds"
                   + (dawEngine.getProject().folder == juce::File()
                          ? juce::String (" (in your own folder - save the session and it will travel with it).")
                          : juce::String (", so it travels with it. Pick it on the strip and press HEAR IT."));
        }

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
        // Opening a device no longer has anything to carry the mix across: MixController owns
        // the session's state and prepare() only builds the audio graph for it. What used to be
        // hold() / applyPendingMix() around every one of these calls was a copy of the session
        // taken and pushed back by hand, and it is what lost the reference mix every time.
        juce::String openDevices (const juce::String& input, const juce::String& output) override
        {
            forgetPairing();
            return host.open (input, output);
        }
        juce::String openOutputOnly (const juce::String& output) override
        {
            forgetPairing();
            return host.openOutputOnly (output);
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

            const juce::String err = host.setOutputDevice (output);
            if (err.isEmpty()) { broadcastDevice = {}; touchSession(); }
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
        double cpuLoad() override { return host.isOpen() ? host.getDeviceManager().getCpuUsage() : -1.0; }
        bool deviceStopped() override { return host.deviceStoppedUnexpectedly(); }
        DeviceState deviceState() override { return host.state(); }
        void askForInputPermission (std::function<void (bool)> done) override { MicPermission::request (std::move (done)); }
        void reconfigure() override
        {
            // The assignments are what changed, so the timeline hears about them first: every
            // track follows its own input, and the clips stay with the source they were
            // recorded from instead of sliding under the next one's name. The mix follows its
            // own inputs inside MixController::rebuild(), which setSession() already called.
            dawEngine.setSession (controller.getSession());
            host.reconfigure();
        }
        juce::String currentInputDevice() override { return consoleInput(); }
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
            touchSession();
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
            options.from = job->from;
            options.to = job->to;
            options.onProgress = std::move (progress);
            return MixBounce::renderProject (job->session, job->params, job->project, dest,
                                             format == ExportFormat::Mp3 ? MixBounce::Format::Mp3 : MixBounce::Format::Wav,
                                             options);
        }

        void newSession() override
        {
            SessionState fresh;
            fresh.session.name = "Untitled";
            applySession (fresh, controller, dawEngine);
            panelWidth = 0;
            dawEngine.locate (0);
            if (host.isOpen()) host.reconfigure();
        }

        void touchSession() override { controller.touch(); }
        unsigned long long sessionRevision() override { return controller.getRevision(); }
        unsigned long long sessionMilestone() override { return controller.getMilestone(); }

        // The snapshot is taken here, on the message thread, and written by the autosave's own
        // worker: a thirty-two channel document with a morning of history is a real serialise
        // and has no business inside a 30 Hz tick.
        void autosaveNow (bool immediately) override
        {
            if (controller.getSession().inputs.empty()) return;
            autosave.open (documentFileOrDefault());     // cheap and idempotent once the file is the same
            auto state = captureSession (controller, dawEngine, deviceChoice(), panelWidth);
            if (samples != nullptr) readSampleChoices (controller, *samples, state.samples);
            autosave.note (state, immediately);
        }

        void saveSession() override
        {
            if (controller.getSession().inputs.empty()) return;
            // Anything the autosave still owes goes first, so the document is never written
            // from behind an autosave that is about to land on top of it.
            autosave.flush();
            writeDocument (documentFileOrDefault());
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
            SessionState state;
            if (! SessionStore::load (file, state)) return "That file is not a DLIVE session.";
            openState (state);
            lastSessionPointer().replaceWithText (file.getFullPathName());
            return {};
        }

        // The whole of opening a session: the document into the controller and the engine, the
        // drum sounds resolved by name, then whatever devices this Mac has. In that order,
        // because the session is the document and the device is a preference - which is why it
        // opens at all with the console unplugged, and why saving it then cannot lose anything.
        void openState (const SessionState& state)
        {
            // Whatever was open is closed cleanly first: its marker and its autosave go, so a
            // session that was left properly is never offered back as unsaved work.
            autosave.closeCleanly();
            applySession (state, controller, dawEngine);
            panelWidth = state.trackPanelWidth;
            forgetPairing();
            recoveryNote.clear();
            // The session's own sounds, before the choices are resolved against them: a sound
            // that travelled with the session is one of the ones a choice can name.
            reloadSamplesForSession();
            if (samples != nullptr)
                for (const auto& gone : resolveSampleChoices (state.samples, *samples, controller))
                    recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + juce::String (gone);

            // What could not be opened becomes a sentence on the toast, never a refusal: the
            // whole point of a recording is to be able to open it somewhere else.
            const auto err = openDevicesFor (state);
            restoreSolo (state, err);
            for (const auto& take : dawEngine.recoverUnfinishedTakes())
                recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + take.note;
            dawEngine.locate (0);
            autosave.open (documentFileOrDefault());
        }

        // Opens the devices a session asks for, or the nearest thing this Mac has (DevicePlan.h),
        // and leaves the sentence about it in the recovery note. Returns the device error when
        // even the planned device would not open (the note carries it too).
        juce::String openDevicesFor (const SessionState& doc)
        {
            recoveryNote.clear();
            juce::StringArray ins, outs;
            for (const auto& d : host.listInputDevices()) ins.add (d.name);
            for (const auto& d : host.listOutputDevices()) outs.add (d.name);
            const auto plan = planDevicesForSession (doc.devices.consoleInput, doc.devices.broadcastOutput,
                                                    doc.project.hasAudio(), ins, outs,
                                                     host.getInputDeviceName(), host.getOutputDeviceName(), host.isOpen());
            juce::String err;
            switch (plan.action)
            {
                case DevicePlan::Action::OpenBoth:       err = host.open (plan.input, plan.output); break;
                case DevicePlan::Action::OpenOutputOnly: err = host.openOutputOnly (plan.output); break;
                case DevicePlan::Action::KeepOpen:       host.reconfigure(); break;
                case DevicePlan::Action::None:           break;
            }
            recoveryNote = plan.note;
            if (err.isNotEmpty())
                recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + juce::String ("Its audio device could not be opened (") + err + "). Pick one under Audio device.";
            return err;
        }

        juce::String takeRecoveryNote() override { auto n = recoveryNote; recoveryNote.clear(); return n; }
        juce::String recoveryNote;

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

        // Derived, never remembered. `soloDevice` records which device was chosen, but whether
        // solo actually goes anywhere is a property of the routing - and the routing can be
        // changed from the feed rows underneath, which is how this came to claim solo was set
        // up long after the monitor feed had been turned into something else. A second copy of
        // a truth is a second copy that will one day disagree.
        juce::String soloOutputDevice() override
        {
            return hasMonitorFeed (controller.getOutputFeeds()) ? soloDevice : juce::String();
        }

        MonitorSetup setSoloOutputDevice (const juce::String& wanted) override
        {
            const auto broadcast = broadcastOutputDevice();
            // The console's own device, taken now: a failed open closes the device and forgets
            // it, and while the built device is open the host's input *is* the built device.
            // Putting the console back afterwards is the whole point of the fallback path.
            const auto input = consoleInput();

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
                    // Off the combined device *before* it is destroyed, for the same reason.
                    const auto err = openWith (broadcast, input);
                    consoleInputDevice = {};
                    MonitorDevice::removeDliveDevice();
                    host.rescanDevices();
                    if (err.isNotEmpty()) return { false, err };
                }
                broadcastDevice = {};
                consoleInputDevice = {};
                touchSession();
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
                touchSession();
                return { true, "Solo goes to outputs 3-4 of " + broadcast + ". The stream is on 1-2 and never changes." };
            }

            // ---- two devices: DLIVE builds the combined one
            if (! MonitorDevice::available())
                return { false, "This Mac will not let DLIVE join two output devices. "
                                "You can make an Aggregate Device yourself in Audio MIDI Setup and choose it above." };

            MonitorDevice::Device broadcastDev, soloDev, inputDev;
            for (const auto& d : MonitorDevice::allDevices())
            {
                if (d.isDliveBuilt) continue;
                if (d.name == broadcast && d.outputChannels > 0) broadcastDev = d;
                if (d.name == wanted && d.outputChannels > 0) soloDev = d;
                if (d.name == input && d.inputChannels > 0) inputDev = d;
            }
            if (broadcastDev.uid.isEmpty() || soloDev.uid.isEmpty())
                return { false, "One of those devices is no longer connected." };
            // The console goes inside the built device too, so it is opened once, for both
            // directions (see MonitorDevice::Layout). A combined device the user built cannot
            // go inside another; that one stays a separate input, glued on by JUCE as before.
            const bool foldInput = inputDev.uid.isNotEmpty() && ! inputDev.isAggregate;

            // Never destroy the device the audio is running on. Rebuilding the pairing - which
            // is what choosing a solo device does when one is already set up - starts by
            // removing the combined device, and if that is the open one, CoreAudio is being
            // asked to delete the interface underneath a running stream. Step back onto the
            // plain broadcast device first.
            if (MonitorDevice::dliveDeviceExists())
            {
                openWith (broadcast, input);
                consoleInputDevice = {};
                host.rescanDevices();
            }

            const auto built = MonitorDevice::combine (broadcastDev, soloDev, foldInput ? &inputDev : nullptr);
            // Back where we started, with the broadcast still playing.
            if (! built.ok) return { false, built.error };

            // Only the pairs the feeds need are opened: the engine addresses at most kMaxOutputs
            // channels, and behind sixty-four Dante outputs the headphone pair sits well past
            // that. Opening the first sixteen - which is what happened - left solo pointing at
            // a channel that was never open.
            const auto channels = MonitorDevice::outputChannelsToOpen ({ {}, built.broadcastChannel, built.headphoneChannel, built.carriesInput, {} },
                                                                       broadcastDev.outputChannels, kMaxOutputs);

            // The device exists in CoreAudio the moment it is created, but it is published
            // asynchronously and JUCE caches a device list per type - so without waiting for it
            // and asking again, opening it fails with "No such device" on the device DLIVE has
            // just built. This is the whole reason the first attempt at this did not work.
            const bool appeared = host.waitForOutputDevice (built.deviceName);
            const auto err = appeared ? host.open (built.carriesInput ? built.deviceName : input, built.deviceName, 48000.0, 64, channels)
                                      : juce::String ("this Mac did not publish it in time");
            if (err.isEmpty()) consoleInputDevice = built.carriesInput ? input : juce::String();
            const int broadcastSlot = err.isEmpty() ? host.slotForOutputChannel (built.broadcastChannel) : -1;
            const int soloSlot = err.isEmpty() ? host.slotForOutputChannel (built.headphoneChannel) : -1;
            if (err.isNotEmpty() || broadcastSlot < 0 || soloSlot < 0)
            {
                // It would not open, or came back without the pairs. Leave nothing behind and put
                // the old device back - waiting for it, because it was just pulled out of a device
                // that is being destroyed - and say what happened rather than going quiet.
                MonitorDevice::removeDliveDevice();
                consoleInputDevice = {};
                host.rescanDevices();
                host.waitForOutputDevice (broadcast);
                const auto back = openWith (broadcast, input);
                juce::String why = err.isNotEmpty() ? err : juce::String ("it came back without a separate pair for solo");
                if (back.isNotEmpty()) why += "; and " + broadcast + " could not be reopened afterwards: " + back + " - choose it again under Set-up";
                return { false, "Those two could not be joined (" + why + "). Nothing has been changed. "
                                "Some devices - Bluetooth especially - refuse to be combined; try a wired one." };
            }

            broadcastDevice = broadcast;
            soloDevice = wanted;
            routeOutputs (broadcastSlot, soloSlot);
            touchSession();
            return { true, built.summary };
        }

        juce::String headphonesSummary() override
        {
            const auto solo = soloOutputDevice();          // derived: it cannot claim what is not routed
            if (solo.isEmpty()) return {};
            return "Solo goes to " + solo + ". The stream stays on " + broadcastOutputDevice()
                 + " and never changes.";
        }

        int trackPanelWidth() override { return panelWidth; }
        void setTrackPanelWidth (int px) override { panelWidth = px; }

        // The session remembers which device solo went to; the pairing is rebuilt from that
        // after the console is open, so a Mac that lost the built device still comes back right.
        // A pairing that cannot be rebuilt is not an error opening the session: solo has nowhere
        // to go, which the Outputs sheet and the LIVE page say.
        void restoreSolo (const SessionState& doc, const juce::String& openError)
        {
            if (openError.isNotEmpty() || doc.devices.soloOutput.isEmpty() || ! host.isOpen()) return;
            setSoloOutputDevice (doc.devices.soloOutput);
        }
    private:
        juce::File documentFile() const
        {
            const auto folder = dawEngine.getProject().folder;
            if (folder == juce::File()) return {};
            return folder.getChildFile (folder.getFileName() + ".dlive.json");
        }

    public:
        // Where this session's document belongs, whether or not it has been saved there yet.
        // The autosave and the marker hang off it, so an unsaved session still leaves enough
        // behind to be recovered.
        juce::File documentFileOrDefault() const
        {
            const auto file = documentFile();
            return file != juce::File() ? file : SessionStore::fileFor (juce::String (controller.getSession().name));
        }
        SessionAutosave& autosaveWriter() { return autosave; }

        juce::Time lastAutosave() override { return autosave.lastWrite(); }
        bool autosavePending() override { return ! autosave.isIdle(); }
    private:

        // One line, and it is the tested one: SessionState.cpp reads the session out of the
        // controller and the engine. What was here was fourteen getters and a dead fallback
        // that meant a session opened without its console saved itself empty.
        bool writeDocument (const juce::File& file)
        {
            auto state = captureSession (controller, dawEngine, deviceChoice(), panelWidth);
            if (samples != nullptr) readSampleChoices (controller, *samples, state.samples);
            if (! SessionStore::save (state, file)) return false;
            dawEngine.getProject().folder = file.getParentDirectory();
            lastSessionPointer().replaceWithText (file.getFullPathName());
            return true;
        }

        // The devices as the engineer chose them, never the one DLIVE built around them.
        DeviceChoice deviceChoice()
        {
            return { consoleInput(), broadcastOutputDevice(), soloOutputDevice() };
        }

        // The console's own input device. While DLIVE's built device carries the console's
        // inputs, the host's input device *is* the built one, and nothing outside this class
        // should ever see that name.
        juce::String consoleInput() const
        {
            return consoleInputDevice.isNotEmpty() ? consoleInputDevice : host.getInputDeviceName();
        }
        // Opening devices from Set-up is a fresh start: whatever pairing was in place is over.
        void forgetPairing() { broadcastDevice = {}; soloDevice = {}; consoleInputDevice = {}; controller.touch(); }

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
        // The drum sounds, for turning each strip's `replaceSound` index into a name on save and
        // back into an index on open. Null in a context that has no library (a tool, a test).
        SampleLibrary* samples = nullptr;
        SessionAutosave autosave;
        int panelWidth = 0;             // TRACKS channel panel; 0 = the page's own default
        // The two devices the user chose. While a combined device is open, the *open* device is
        // DLIVE's own and these are what the user actually picked; consoleInputDevice is the
        // console's input device while the built device carries it (see consoleInput()).
        juce::String broadcastDevice, soloDevice, consoleInputDevice;
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
        // The drum sounds, decoded once. The library outlives the controller (declared before
        // it), so the engine never reads a bank that has gone.
        samples = std::make_unique<SampleLibrary>();
        samples->load();
        controller->setSampleBanks (samples->table());
        dawEngine = std::make_unique<DawEngine> (*controller);
        host = std::make_unique<AudioHost> (*controller, *dawEngine);
        services = std::make_unique<HostServices> (*controller, *dawEngine, *host);
        services->setSampleLibrary (*samples);

        SessionState state;
        const auto pointer = lastSessionPointer();
        const juce::File lastDocument = pointer.existsAsFile() ? juce::File (pointer.loadFileAsString().trim()) : juce::File();
        const bool restored = lastDocument != juce::File() && SessionStore::load (lastDocument, state);

        window = std::make_unique<MainWindow> (getApplicationName(), *controller, *services);

        // HOT-PLUG. A console pulled out in the middle of a service is a sentence, not silence:
        // AudioHost notices, says so, and opens it again by itself the moment it comes back -
        // on the same channels, with the same mix, because the session is a document and
        // reopening a device only rebuilds the audio graph for it. The workspaces re-read the
        // device the way they do after any device change.
        host->onDeviceLost = [this] (DeviceState device)
        {
            if (window == nullptr) return;
            window->view().showToast (deviceLostSentence (device, services != nullptr && services->daw().isRecording()));
        };
        host->onDeviceReturned = [this] (DeviceState device)
        {
            if (window == nullptr) return;
            window->view().showToast (deviceBackSentence (device.input.isNotEmpty() ? device.input : device.output,
                                                          device.inputChannels, device.outputChannels));
        };
        // Something was plugged in or pulled out. The chrome and the status foot follow the
        // device on their own tick; the one thing that does not is the list of devices on the
        // set-up page, and only when somebody is looking at it.
        host->onDeviceListChanged = [this]
        {
            if (window == nullptr) return;
            auto& page = window->view().getDevicePage();
            if (page.isVisible()) page.refresh();
        };

        if (restored)
        {
            // Exactly the same path as opening it from the library, so there is one way a
            // session comes back: the document first, then whatever devices this Mac has.
            services->openState (state);
            window->view().showPage (controller->getSession().inputs.empty() ? MainView::Page::Assign
                                                                             : MainView::Page::Tracks);
            const auto note = services->takeRecoveryNote();
            if (note.isNotEmpty()) window->view().showToast (note);
        }

        // DLIVE did not get to say goodbye last time, and the autosave holds work the document
        // does not. Asked once, after the window is up, in the words of what was lost rather
        // than in the words of what went wrong.
        if (const auto found = SessionAutosave::check (lastDocument); found.offer)
            juce::MessageManager::callAsync ([this, found, lastDocument] { offerRecovery (found, lastDocument); });
    }

    // Recover / Open last saved / Keep both. Nothing is deleted by any of the three: "keep
    // both" writes the recovered work as its own session, and the other two only remove the
    // autosave once the choice has been carried out.
    void offerRecovery (const SessionAutosave::Recovery& found, const juce::File& document)
    {
        if (window == nullptr || services == nullptr) return;
        auto* alert = new juce::AlertWindow ("Recover session?",
                                             found.sentence + "\n\nThe session on disk was last saved at "
                                             + found.documentWhen.toString (true, true, false, true) + ".",
                                             juce::MessageBoxIconType::NoIcon);
        alert->addButton ("Recover", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->addButton ("Open last saved", 2, juce::KeyPress (juce::KeyPress::escapeKey));
        alert->addButton ("Keep both", 3);
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert, found, document] (int r)
        {
            std::unique_ptr<juce::AlertWindow> closer (alert);
            if (r == 2) { SessionAutosave::discard (document); return; }

            SessionState recovered;
            if (! SessionStore::load (found.autosave, recovered))
            {
                window->view().showToast ("That autosave could not be read, so the session on disk is the one you have.");
                SessionAutosave::discard (document);
                return;
            }
            SessionAutosave::discard (document);      // the choice has been made; stop offering it

            if (r == 3)
            {
                // Keep both: the recovered work becomes a session of its own, beside the one
                // that was saved, and the takes stay where they are (saveSessionAs makes their
                // clips absolute for exactly this).
                const auto name = juce::String (recovered.session.name) + " (recovered)";
                services->openState (recovered);
                const auto err = services->saveSessionAs (name);
                window->view().sessionReplaced();
                window->view().showToast (err.isEmpty()
                    ? "Both are here: the recovered work is now \"" + name + "\", and the session you saved is untouched."
                    : err);
                return;
            }

            services->openState (recovered);
            services->saveSession();                  // the recovery is committed, not left in a sidecar
            window->view().sessionReplaced();
            window->view().showToast ("Recovered. The work from " + found.when.toString (false, true, false, true)
                                      + " is back, and the session has been saved.");
        }), true);
    }

    void shutdown() override
    {
        if (dawEngine != nullptr) dawEngine->stop();
        // A clean goodbye: the document is written, and the marker and the autosave go with
        // it. One that is still there on the next launch is how DLIVE knows it was killed.
        if (services != nullptr && controller != nullptr && ! controller->getSession().inputs.empty()) services->saveSession();
        if (services != nullptr) services->autosaveWriter().closeCleanly();
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
    std::unique_ptr<SampleLibrary> samples;      // before the controller: destroyed after it
    std::unique_ptr<MixController> controller;
    std::unique_ptr<DawEngine> dawEngine;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<HostServices> services;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (DLiveApplication)
