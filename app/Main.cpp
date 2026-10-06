// DINE: the live recording and broadcast DAW.
//   Console / interface / Dante -> DINE -> OBS / Ecamm / recording
// One MixController owns the mix, one DawEngine owns the timeline and the recorder, one
// AudioHost owns the device, MainView shows one workspace at a time. A session is a folder
// with its recordings inside; the last one reloads on launch.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "native/AppFolders.h"
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
#include "native/Telemetry.h"
#include "native/UsageIds.h"
#include "ui/MainView.h"
#include <optional>

using namespace livemix;

#if JUCE_MAC
// app/native/WindowChrome.mm: the three macOS window buttons, put inside DINE's own toolbar.
namespace livemix
{
    void putWindowButtonsInTheToolbar (juce::Component&);
    void dragWindowFromToolbar (juce::Component&);
    bool systemPrefersReducedMotion();
    void toolbarDoubleClicked (juce::Component&);
}
#endif

// The Supabase project the usage events go to, from the build (-DDINE_SUPABASE_URL=...) or,
// for a developer, the environment. Neither set: nothing leaves the Mac. docs/ANALYTICS.md.
#ifndef DINE_SUPABASE_URL
 #define DINE_SUPABASE_URL ""
#endif
#ifndef DINE_SUPABASE_ANON_KEY
 #define DINE_SUPABASE_ANON_KEY ""
#endif

namespace
{
    juce::File lastSessionPointer()
    {
        return AppFolders::library().getChildFile ("last-session.txt");
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

        const SampleLibrary* sampleLibrary() override { return samples; }

        juce::String importSample (RoleFamily family, const juce::File& file) override
        {
            if (samples == nullptr) return "Sounds are not available here.";
            samples->setSessionFolder (dawEngine.getProject().folder);
            // What every drum strip plays, by name, before the reload re-sorts the slots: a
            // sound filed alphabetically ahead of the kick's must not change the kick mid-service.
            std::array<SampleChoice, kMaxStrips> playing {};
            readSampleChoices (controller, *samples, playing);
            juce::String problem;
            const auto name = samples->importSound (family, file, problem);
            if (name.isEmpty())
            {
                trackError ("samples", "import_failed", true, { { "instrument", roleFamilyId (family) } });
                return problem;
            }
            // The new table and every strip's new place in it reach the audio in one publish.
            controller.setSampleBanks (samples->table(), false);
            resolveSampleChoices (playing, *samples, controller);
            touchSession();
            return "\"" + name + "\" is in this session's sounds"
                   + (dawEngine.getProject().folder == juce::File()
                          ? juce::String (" on this Mac only, because this session has not been saved yet. Save it, then import the "
                                          "sound again, and it travels with the session.")
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
            if (const auto locked = deviceChangeLocked(); locked.isNotEmpty()) return locked;
            standingIn = false;                          // the engineer's own choice now
            forgetPairing();
            return host.open (input, output, runningRate(), runningBlock());
        }
        juce::String openOutputOnly (const juce::String& output) override
        {
            if (const auto locked = deviceChangeLocked(); locked.isNotEmpty()) return locked;
            standingIn = false;                          // the engineer's own choice now
            forgetPairing();
            return host.openOutputOnly (output, runningRate(), runningBlock());
        }

        // LIVE SAFE: re-opening the audio device stops the broadcast for a moment. It was named
        // in the policy and checked nowhere, so every device picker could still do it mid-service.
        juce::String deviceChangeLocked()
        {
            const auto v = controller.checkLiveSafe (LiveAction::DeviceChange);
            return v.allowed ? juce::String() : juce::String (v.reason);
        }
        // A device re-opened for DINE's own reasons (solo joined or parted, the broadcast moved)
        // keeps the rate and the buffer it was running at. Re-opening at 48 kHz / 64 re-clocked a
        // 44.1 or 96 kHz rig mid-service, split the take, and left a laptop on the smallest buffer.
        double runningRate() const { return host.isOpen() && host.getSampleRate() > 0.0 ? host.getSampleRate() : 48000.0; }
        int runningBlock() const { return host.isOpen() && host.getBufferSize() > 0 ? host.getBufferSize() : 64; }

        juce::String changeOutput (const juce::String& output) override
        {
            if (const auto locked = deviceChangeLocked(); locked.isNotEmpty()) return locked;
            standingIn = false;                          // the engineer's own choice now
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
        // DINE's own; what the user picked is the broadcast, and that is what they are told.
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
        juce::Array<int> bufferSizes() override
        {
            juce::Array<int> out;
            if (auto* d = host.getDeviceManager().getCurrentAudioDevice())
                for (int n : d->getAvailableBufferSizes())
                    if (n >= 32 && n <= 2048) out.add (n);
            return out;
        }
        juce::String setBufferSize (int samples) override
        {
            if (! host.isOpen()) return "Open an audio device first.";
            if (const auto locked = deviceChangeLocked(); locked.isNotEmpty()) return locked;
            // Changing it restarts the audio for a moment, and a take would get a hole in it.
            if (dawEngine.isRecording()) return "Recording is running. Stop recording first - changing the buffer restarts the audio for a moment.";
            auto& dm = host.getDeviceManager();
            auto setup = dm.getAudioDeviceSetup();
            if (setup.bufferSize == samples) return {};
            setup.bufferSize = samples;
            const auto err = dm.setAudioDeviceSetup (setup, true);
            return err.isEmpty() && host.getBufferSize() == samples ? juce::String()
                 : "The device would not run at " + juce::String (samples) + " samples" + (err.isNotEmpty() ? " (" + err + ")." : ".");
        }
        int xrunCount() override { return host.getXRunCount(); }
        double cpuLoad() override { return host.isOpen() ? host.getDeviceManager().getCpuUsage() : -1.0; }
        bool deviceStopped() override { return host.deviceStoppedUnexpectedly(); }
        DeviceState deviceState() override { return host.state(); }
        void askForInputPermission (std::function<void (bool)> done) override { MicPermission::request (std::move (done)); }
        InputAccess inputHeldBack() override { return host.inputHeldBack(); }
        // Reopening the device drops the broadcast for a moment, so LIVE SAFE holds both of these
        // back exactly as it holds back a device picked by hand (deviceChangeLocked).
        bool retryHeldInput() override
        {
            return deviceChangeLocked().isEmpty() && host.retryHeldInput (dawEngine.isRecording());
        }
        juce::String openWantedConsoleIfBack() override
        {
            // Only the console this session was set up on, only while it is standing in for it,
            // and never mid-take: the same rule as a device that comes back (deviceReturned).
            if (! standingIn || wantedDevices.consoleInput.isEmpty() || dawEngine.isRecording()) return {};
            if (deviceChangeLocked().isNotEmpty()) return {};
            if (host.isOpen() && host.getInputDeviceName() == wantedDevices.consoleInput) return {};
            bool here = false;
            for (const auto& d : host.listInputDevices()) if (d.name == wantedDevices.consoleInput) here = true;
            if (! here) return {};
            SessionState doc;
            doc.devices = wantedDevices;
            doc.project = dawEngine.getProject();
            const auto before = recoveryNote;
            recoveryNote.clear();
            const auto err = openDevicesFor (doc);
            restoreSolo (doc, err);
            recoveryNote = before;
            if (err.isNotEmpty()) return {};
            const auto st = host.state();
            return wantedDevices.consoleInput + " is here now, and DINE has opened it."
                 + (st.stage == DeviceStage::InputRefused ? " " + st.why : juce::String());
        }
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

        ImportOutcome importAudio (const juce::Array<juce::File>& filesOrFolders,
                                   MultitrackImport::Destination where, int firstTrack, juce::int64 at) override
        {
            ImportOutcome out;
            const auto plan = MultitrackImport::plan (filesOrFolders);
            if (plan.tracks.empty())
            {
                out.error = plan.skipped.isEmpty() ? juce::String ("There is no audio in that.")
                                                   : "Nothing there could be played: " + plan.skipped.joinIntoString (", ") + ".";
                return out;
            }

            auto session = controller.getSession();
            auto project = dawEngine.getProject();
            // A session nothing has been recorded or imported into, and never saved, is named
            // after what came in - the folder, or the folder the files are in.
            const bool fresh = ! project.hasAudio() && project.folder == juce::File();
            const auto applied = MultitrackImport::apply (plan, session, project, where, firstTrack, at);
            if (applied.added + applied.onExisting == 0)
            {
                out.error = applied.summary;
                return out;
            }
            if (fresh && where == MultitrackImport::Destination::Match && ! filesOrFolders.isEmpty())
            {
                const auto& first = filesOrFolders.getReference (0);
                session.name = (first.isDirectory() ? first : first.getParentDirectory()).getFileName().toStdString();
            }

            if (applied.added > 0 || session.name != controller.getSession().name)
                controller.setSession (session);
            dawEngine.setSession (session);
            dawEngine.setProject (project);

            // Imported audio plays through the same graph as a console, so an output is all that is needed.
            if (applied.added > 0)
            {
                if (! host.isOpen())
                {
                    juce::String output = host.getOutputDeviceName();
                    if (output.isEmpty()) { const auto outs = host.listOutputDevices(); if (! outs.isEmpty()) output = outs[0].name; }
                    if (output.isNotEmpty()) host.openOutputOnly (output, project.sampleRate > 0.0 ? project.sampleRate : 48000.0);
                }
                else
                {
                    host.reconfigure();
                }
                dawEngine.setSession (session);
                dawEngine.setProject (project);
            }
            touchSession();
            out.summary = applied.summary;
            out.added = applied.added;
            return out;
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
                                ExportFormat format, ExportProgress& progress) override
        {
            if (job == nullptr) return "There is nothing to export.";
            MixBounce::Options options;
            options.from = job->from;
            options.to = job->to;
            options.what = job->what == ExportWhat::GroupStems ? MixBounce::What::GroupStems
                         : job->what == ExportWhat::RawMultitrack ? MixBounce::What::RawMultitrack
                                                                  : MixBounce::What::StereoMix;
            options.loudness = job->loudness == ExportLoudness::Stream14 ? MixBounce::Loudness::Stream14
                             : job->loudness == ExportLoudness::Podcast16 ? MixBounce::Loudness::Podcast16
                                                                          : MixBounce::Loudness::AsMixed;
            options.onProgress = [&progress] (float f) { return progress.report (f); };
            options.onStage = [&progress] (MixBounce::Stage s, bool measurable) { progress.beginStage (s, measurable); };
            const auto bounceFormat = format == ExportFormat::Mp3 ? MixBounce::Format::Mp3
                                    : format == ExportFormat::Aiff ? MixBounce::Format::Aiff
                                                                   : MixBounce::Format::Wav;
            return MixBounce::renderProject (job->session, job->params, job->project, dest, bounceFormat, options);
        }

        void newSession() override
        {
            // The one that was open goes cleanly (MainView saved it first): its marker and its
            // autosave with it, so it is not offered back as a crash next launch.
            autosave.closeCleanly();
            unresolvedSounds = {};
            SessionState fresh;
            fresh.session.name = SessionStore::unusedName ("Untitled").toStdString();
            applySession (fresh, controller, dawEngine);
            trackEvent ("session_created");
            panelWidth = 0;
            dawEngine.locate (0);
            if (host.isOpen()) host.reconfigure();
        }

        void touchSession() override { controller.touch(); }
        void forgetDeviceUids() { uidCache.clear(); }     // the device list changed (see uidFor)
        unsigned long long sessionRevision() override { return controller.getRevision(); }
        unsigned long long sessionMilestone() override { return controller.getMilestone(); }

        // The snapshot is taken here, on the message thread, and written by the autosave's own
        // worker: a thirty-two channel document with a morning of history is a real serialise
        // and has no business inside a 30 Hz tick.
        void autosaveNow (bool immediately) override
        {
            if (controller.getSession().inputs.empty()) return;
            const auto doc = documentFileOrDefault();
            autosave.open (doc);                         // cheap and idempotent once the file is the same
            // A session that has never been saved is still the one a crash has to find: the
            // next launch looks where this pointer says, and finds its autosave beside it.
            if (! doc.existsAsFile() && pointedAt != doc)
            {
                SessionStore::writeTextAtomically (lastSessionPointer(), doc.getFullPathName());
                pointedAt = doc;
            }
            auto state = captureSession (controller, dawEngine, deviceChoice(), panelWidth);
            if (samples != nullptr) readSampleChoices (controller, *samples, state.samples);
            keepUnresolvedSampleChoices (unresolvedSounds, controller, state.samples);
            autosave.note (state, immediately);
        }

        bool saveSession() override
        {
            if (controller.getSession().inputs.empty()) return true;
            // Anything the autosave still owes goes first, so the document is never written
            // from behind an autosave that is about to land on top of it.
            autosave.flush();
            if (writeDocument (documentFileOrDefault())) return true;
            trackError ("session", "save_failed", false);
            return false;
        }

        // The last write before quitting. It says whether the document landed, because a clean
        // goodbye deletes the autosave - and when the document could not be written (a full
        // disk, a drive that has gone) the autosave is the only copy of the morning's work.
        bool saveForQuit()
        {
            if (controller.getSession().inputs.empty()) return true;
            autosaveNow (true);
            autosave.flush();
            if (writeDocument (documentFileOrDefault())) return true;
            trackError ("session", "save_failed", false);
            return false;
        }

        juce::String saveSessionAs (const juce::String& name) override
        {
            juce::String n = name.trim();
            if (n.isEmpty()) return "Give the session a name.";
            // The takes being written live in this session's folder; moving the document away
            // from under them mid-take left the new session pointing at files that are not there.
            if (dawEngine.isRecording()) return "Recording is running into this session's folder. Stop recording first, then save it under another name.";
            const auto file = SessionStore::fileFor (n);
            // Another session's name is another session: saving over it would throw its mix away
            // (and orphan its takes). Saving under the name already open is just a save.
            if ((file.existsAsFile() || file.getParentDirectory().exists()) && file != documentFile())
                return "There is already a session called \"" + n + "\". Choose another name, or open that one.";
            controller.setSessionName (n.toStdString());
            // Moving to a new folder: the takes stay where they are, and their clips keep absolute paths.
            const auto oldFolder = dawEngine.getProject().folder;
            if (oldFolder != juce::File() && oldFolder != file.getParentDirectory())
                for (auto& track : dawEngine.getProject().tracks)
                    for (auto& clip : track.clips)
                        if (! juce::File::isAbsolutePath (clip.file))
                            clip.file = oldFolder.getChildFile ("Audio Files").getChildFile (clip.file).getFullPathName();
            // The session's own drum sounds go with it: they are what its strips name.
            const auto oldSounds = oldFolder.getChildFile ("Samples");
            const auto newSounds = file.getParentDirectory().getChildFile ("Samples");
            if (oldFolder != juce::File() && oldSounds.isDirectory() && ! newSounds.exists())
                oldSounds.copyDirectoryTo (newSounds);
            const auto oldDocument = documentFile();
            dawEngine.getProject().folder = file.getParentDirectory();
            if (! writeDocument (file))
            {
                trackError ("session", "save_failed", false, { { "save_as", true } });
                return "Could not save the session.";
            }
            // The session lives here now: the old folder's autosave and "open" marker go, so the
            // copy it left behind is never offered back as a crash.
            if (oldDocument != juce::File() && oldDocument != file)
            {
                autosave.closeCleanly();
                SessionAutosave::discard (oldDocument);
            }
            return {};
        }

        juce::String loadSession (const juce::File& file) override
        {
            SessionState state;
            if (dawEngine.isRecording())
                return "Recording is running. Stop recording first - opening another session would close the one it is recording into.";
            if (SessionStore::savedByNewerBuild (file))
                return "That session was saved by a newer DINE. Update DINE to open it - opening it here would lose what the newer version added.";
            if (! SessionStore::load (file, state))
            {
                trackError ("session", "load_failed", true);
                return "That file is not a DINE session.";
            }
            // WHAT IS OPEN IS SAVED BEFORE ANYTHING REPLACES IT. Opening closes the current
            // session cleanly, and a clean close throws its autosave away - so without this a
            // morning of unsaved work went with one click in the sessions list. If it cannot be
            // written it stays open, and nothing has been lost.
            if (! controller.getSession().inputs.empty() && ! writeDocument (documentFileOrDefault()))
            {
                trackError ("session", "save_failed", false);
                return "\"" + juce::String (controller.getSession().name) + "\" could not be saved, so it is still open "
                       "and nothing was opened in its place. Check the disk, then try again.";
            }
            openState (state);
            SessionStore::writeTextAtomically (lastSessionPointer(), file.getFullPathName());
            return {};
        }

        // The whole of opening a session: the document into the controller and the engine, the
        // drum sounds resolved by name, then whatever devices this Mac has. In that order,
        // because the session is the document and the device is a preference - which is why it
        // opens at all with the console unplugged, and why saving it then cannot lose anything.
        // `allowInputs` is false only when macOS has never been asked about the microphone and
        // the engineer said Not now: the output opens on its own and nothing listens.
        // `recoverTakes` is false only at a launch that is about to ask Recover / Open last saved /
        // Keep both: a take the crash left unfinished is put back on the session the engineer
        // chooses, after the answer, not on the one that happens to open first (recoverTakesNow).
        void openState (const SessionState& state, const char* source = "user", bool allowInputs = true, bool recoverTakes = true)
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
                for (const auto& gone : resolveSampleChoices (state.samples, *samples, controller, &unresolvedSounds))
                    recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + juce::String (gone);
            {
                // Audio that is not where the session says it is plays as silence: said, by name,
                // rather than left for the engineer to wonder why the keys are gone.
                const auto gone = missingAudio (state.project, state.session);
                if (! gone.empty())
                {
                    juce::String names;
                    for (size_t k = 0; k < gone.size() && k < 4; ++k) names += (k == 0 ? "" : ", ") + juce::String (gone[k]);
                    if (gone.size() > 4) names += " and " + juce::String (int (gone.size()) - 4) + " more";
                    recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + juce::String (int (gone.size()))
                                  + (gone.size() == 1 ? " track plays" : " tracks play") + " silence because its audio is not where the session left it ("
                                  + names + "). Moved or renamed? Put the files back, or import the folder again.";
                }
            }

            // What could not be opened becomes a sentence on the toast, never a refusal: the
            // whole point of a recording is to be able to open it somewhere else.
            const auto err = openDevicesFor (state, allowInputs);
            restoreSolo (state, err);
            int takes = 0;
            if (recoverTakes)
                for (const auto& take : dawEngine.recoverUnfinishedTakes())
                {
                    recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + take.note;
                    ++takes;
                }
            dawEngine.locate (0);
            autosave.open (documentFileOrDefault());
            trackEvent ("session_opened", { { "source", source },
                                            { "inputs", int (state.session.inputs.size()) },
                                            { "tracks", int (dawEngine.getProject().tracks.size()) },
                                            { "takes_repaired", takes },
                                            { "device_fallback", err.isNotEmpty() } });
        }

        // Opens the devices a session asks for, or the nearest thing this Mac has (DevicePlan.h),
        // and leaves the sentence about it in the recovery note. Returns the device error when
        // even the planned device would not open (the note carries it too).
        juce::String openDevicesFor (const SessionState& doc, bool allowInputs = true)
        {
            juce::StringArray ins, outs;
            for (const auto& d : host.listInputDevices()) ins.add (d.name);
            for (const auto& d : host.listOutputDevices()) outs.add (d.name);
            // The Mac's own speakers, by CoreAudio transport: where a mix goes when its own output
            // is missing, rather than whatever output happens to be first (DevicePlan.h).
            juce::String safeOutput;
            for (const auto& d : MonitorDevice::outputDevices())
                if (d.kind == MonitorDevice::Device::Kind::BuiltIn) { safeOutput = d.name; break; }
            forgetDeviceUids();
            const auto plan = planDevicesForSession (doc.devices.consoleInput, doc.devices.broadcastOutput,
                                                    doc.project.hasAudio(), ins, outs,
                                                     host.getInputDeviceName(), host.getOutputDeviceName(), host.isOpen(),
                                                     safeOutput);
            juce::String err;
            juce::String note = plan.note;
            // The same name, another unit: opened (it is what is plugged in), and said, because its
            // channels may carry other sources than the ones this session was built on.
            if (plan.action == DevicePlan::Action::OpenBoth && ! sameUnit (doc.devices.consoleInputUid, uidFor (plan.input)))
                note += juce::String (note.isEmpty() ? "" : " ") + "This " + plan.input + " is not the unit this session was set up on "
                        "(another of the same model). Run CHECK INPUTS before the service to be sure every input is what it was.";
            switch (plan.action)
            {
                case DevicePlan::Action::OpenBoth:
                    // The input opens only when macOS has already said yes (AudioHost::open);
                    // "Not now" asks for nothing at all. Either way the input is remembered, so
                    // it opens the moment macOS agrees (retryHeldInput).
                    err = host.open (plan.input, plan.output, 48000.0, 64, {}, allowInputs);
                    if (err.isEmpty())
                        if (const auto st = host.state(); st.stage == DeviceStage::InputRefused && st.why.isNotEmpty())
                            note += juce::String (note.isEmpty() ? "" : " ") + st.why;
                    break;
                case DevicePlan::Action::OpenOutputOnly: err = host.openOutputOnly (plan.output); break;
                case DevicePlan::Action::KeepOpen:       host.reconfigure(); break;
                case DevicePlan::Action::None:           break;
            }
            // A plan that is not the session's own devices is a stand-in (see deviceChoice).
            wantedDevices = doc.devices;
            standingIn = plan.action != DevicePlan::Action::None
                      && (plan.input != doc.devices.consoleInput || plan.output != doc.devices.broadcastOutput)
                      && (doc.devices.consoleInput.isNotEmpty() || doc.devices.broadcastOutput.isNotEmpty());
            // Added to what openState has already said - a drum sound that did not travel is as
            // much news as a device that is missing, and clearing here used to throw it away.
            if (note.isNotEmpty()) recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + note;
            if (err.isNotEmpty())
                recoveryNote += (recoveryNote.isEmpty() ? "" : " ") + juce::String ("Its audio device could not be opened (") + err + "). Pick one under Audio device.";
            return err;
        }

        juce::String takeRecoveryNote() override { auto n = recoveryNote; recoveryNote.clear(); return n; }
        juce::File pointedAt;                            // the unsaved document the last-session pointer names
        std::array<SampleChoice, kMaxStrips> unresolvedSounds {};   // stored drum sounds this Mac does not have

        // The takes a crash left unfinished, repaired and put back on the session that is open
        // now - called once the recovery question has been answered. Returns what to say.
        juce::String recoverTakesNow()
        {
            juce::String note;
            bool any = false;
            for (const auto& take : dawEngine.recoverUnfinishedTakes())
            {
                note += (note.isEmpty() ? "" : " ") + take.note;
                any = any || take.repaired;
            }
            if (any) touchSession();
            return note;
        }
        juce::String recoveryNote;

        juce::Array<SessionStore::Listing> listSessions() override { return SessionStore::listSessions(); }

        // ---- Two outputs: one for the broadcast, one for the engineer ----
        //
        // From the outside this is two pickers. Underneath there are three cases, and the
        // point of doing it here rather than in the UI is that none of them reach the user:
        //
        //   solo on the same device   - it has spare outputs, so solo takes a second pair
        //   solo on another device    - macOS opens one device at a time, so DINE builds the
        //                               combined device, reopens on it, and routes a pair each
        //   solo turned off           - the combined device is removed and the Mac is put back
        //
        // The mix survives all three the way it survives any other device change: held before,
        // put back after.
        bool canCombineOutputs() override { return MonitorDevice::available(); }

        juce::String broadcastOutputDevice() override
        {
            // While the combined device is open, the broadcast is the device inside it that
            // the user actually chose - not the one DINE built around it.
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
            const double rate = runningRate();
            const int block = runningBlock();
            // Solo on another pair of the device already open moves only the engineer's listen;
            // anything else re-opens the device the broadcast is playing through.
            if (wanted != broadcast || MonitorDevice::dineDeviceExists())
                if (const auto locked = deviceChangeLocked(); locked.isNotEmpty()) return { false, locked };
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
                if (MonitorDevice::dineDeviceExists())
                {
                    // Off the combined device *before* it is destroyed, for the same reason.
                    const auto err = openWith (broadcast, input);
                    consoleInputDevice = {};
                    MonitorDevice::removeDineDevice();
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
                                    "solo to go. Choose a different device - DINE will join the two for you." };
                if (const auto* taken = feedUsing (2, 3))
                    return { false, "Outputs 3-4 of " + broadcast + " already carry " + juce::String (outputFeedSourceName (taken->source))
                                    + ". Move that feed under Outputs first, or choose a different device for solo." };
                soloDevice = wanted;
                broadcastDevice = broadcast;
                if (! routeOutputs (0, 2))
                    return { false, "Solo could not be routed without moving the stream, and LIVE SAFE keeps the stream where it is." };
                touchSession();
                return { true, "Solo goes to outputs 3-4 of " + broadcast + ". The stream is on 1-2 and never changes." };
            }

            // ---- two devices: DINE builds the combined one
            if (! MonitorDevice::available())
                return { false, "This Mac will not let DINE join two output devices. "
                                "You can make an Aggregate Device yourself in Audio MIDI Setup and choose it above." };

            MonitorDevice::Device broadcastDev, soloDev, inputDev;
            for (const auto& d : MonitorDevice::allDevices())
            {
                if (d.isDineBuilt) continue;
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
            if (MonitorDevice::dineDeviceExists())
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
            // and asking again, opening it fails with "No such device" on the device DINE has
            // just built. This is the whole reason the first attempt at this did not work.
            const bool appeared = host.waitForOutputDevice (built.deviceName);
            const auto err = appeared ? host.open (built.carriesInput ? built.deviceName : input, built.deviceName, rate, block, channels)
                                      : juce::String ("this Mac did not publish it in time");
            if (err.isEmpty()) consoleInputDevice = built.carriesInput ? input : juce::String();
            const int broadcastSlot = err.isEmpty() ? host.slotForOutputChannel (built.broadcastChannel) : -1;
            const int soloSlot = err.isEmpty() ? host.slotForOutputChannel (built.headphoneChannel) : -1;
            if (err.isNotEmpty() || broadcastSlot < 0 || soloSlot < 0)
            {
                // It would not open, or came back without the pairs. Leave nothing behind and put
                // the old device back - waiting for it, because it was just pulled out of a device
                // that is being destroyed - and say what happened rather than going quiet.
                MonitorDevice::removeDineDevice();
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
        void setTrackPanelWidth (int px) override { if (panelWidth != px) { panelWidth = px; touchSession(); } }

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
            return folder.getChildFile (folder.getFileName() + ".dine.json");
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
        bool autosaveFailing() override { return autosave.isFailing(); }
    private:

        // One line, and it is the tested one: SessionState.cpp reads the session out of the
        // controller and the engine. What was here was fourteen getters and a dead fallback
        // that meant a session opened without its console saved itself empty.
        bool writeDocument (const juce::File& file)
        {
            auto state = captureSession (controller, dawEngine, deviceChoice(), panelWidth);
            if (samples != nullptr) readSampleChoices (controller, *samples, state.samples);
            keepUnresolvedSampleChoices (unresolvedSounds, controller, state.samples);
            if (! SessionStore::save (state, file)) return false;
            dawEngine.getProject().folder = file.getParentDirectory();
            SessionStore::writeTextAtomically (lastSessionPointer(), file.getFullPathName());
            return true;
        }

        // The devices as the engineer chose them, never the one DINE built around them.
        //
        // While a session is running on a stand-in - it opened with its console unplugged and the
        // mix went to the Mac's speakers - what it asked for is still what it is saved with. The
        // stand-in used to be written into the document by the first autosave, and next Sunday at
        // church the console was never opened. It stops standing in when the engineer picks a
        // device, or when the one asked for is what is open again.
        DeviceChoice deviceChoice()
        {
            DeviceChoice d { consoleInput(), broadcastOutputDevice(), soloOutputDevice() };
            if (standingIn)
            {
                if (d.consoleInput == wantedDevices.consoleInput && d.broadcastOutput == wantedDevices.broadcastOutput)
                    standingIn = false;
                else
                    return wantedDevices;
            }
            d.consoleInputUid = uidFor (d.consoleInput);
            d.broadcastOutputUid = uidFor (d.broadcastOutput);
            return d;
        }
        DeviceChoice wantedDevices;
        bool standingIn = false;

        // CoreAudio's permanent name for a device, cached by name: this is read on every autosave,
        // and enumerating the devices is not free. The cache is dropped whenever the list changes.
        juce::String uidFor (const juce::String& name)
        {
            if (name.isEmpty()) return {};
            auto it = uidCache.find (name);
            if (it != uidCache.end()) return it->second;
            return uidCache[name] = MonitorDevice::findDevice (name).uid;
        }
        std::map<juce::String, juce::String> uidCache;

        // The console's own input device. While DINE's built device carries the console's
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
            return inputDevice.isNotEmpty() ? host.open (inputDevice, outputDevice, runningRate(), runningBlock())
                                            : host.openOutputOnly (outputDevice, runningRate(), runningBlock());
        }

        // A feed other than the broadcast and the engineer's listen that uses either channel.
        const OutputFeed* feedUsing (int left, int right) const
        {
            const auto& feeds = controller.getOutputFeeds();
            for (int i = 1; i < juce::jmin (feeds.count, int (kMaxOutputFeeds)); ++i)
            {
                const auto& f = feeds.feeds[size_t (i)];
                if (f.monitor || ! f.routed()) continue;
                for (int c : { f.left, f.right })
                    if (c == left || c == right) return &f;
            }
            return nullptr;
        }

        // Feed 0 is the broadcast, and the engineer's listen is the monitor feed (feed 1 unless
        // the session put it elsewhere). Written in one place so the three ways of setting solo up
        // cannot end up disagreeing about which pair is which. Every other feed - a room, a
        // hearing loop, a record feed - is left exactly where it was: this used to cut the list
        // back to two and delete them. False when the controller refused it (LIVE SAFE).
        bool routeOutputs (int broadcastChannel, int soloChannel)
        {
            auto feeds = controller.getOutputFeeds();
            int listen = -1;
            for (int i = 1; i < juce::jmin (feeds.count, int (kMaxOutputFeeds)); ++i)
                if (feeds.feeds[size_t (i)].monitor) { listen = i; break; }
            if (listen < 0)
            {
                listen = juce::jmax (1, juce::jmin (feeds.count, int (kMaxOutputFeeds) - 1));
                feeds.feeds[size_t (listen)] = {};
                feeds.count = juce::jmax (feeds.count, listen + 1);
            }
            feeds.feeds[0].monitor = false;
            feeds.feeds[0].source = MixBus::Master;
            feeds.feeds[0].left = broadcastChannel;
            feeds.feeds[0].right = broadcastChannel + 1;
            feeds.feeds[0].mono = false;          // the broadcast is never summed
            feeds.feeds[0].mute = false;
            auto& solo = feeds.feeds[size_t (listen)];
            solo.monitor = true;
            solo.left = soloChannel;
            solo.right = soloChannel + 1;
            solo.mono = false;
            solo.mute = false;
            controller.setOutputFeeds (feeds);
            const auto& now = controller.getOutputFeeds();
            return now.feeds[0].left == broadcastChannel && now.feeds[size_t (listen)].left == soloChannel;
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
        // DINE's own and these are what the user actually picked; consoleInputDevice is the
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
            // The three window buttons belong inside the toolbar (app/native/WindowChrome.mm);
            // the peer exists only once the window is on screen, so this is asked for here.
            putWindowButtonsInTheToolbar (*this);
            view().onToolbarPressed = [this] { dragWindowFromToolbar (*this); };
            view().prefersReducedMotion = [] { return systemPrefersReducedMotion(); };
            view().onToolbarDoubleClicked = [this] { toolbarDoubleClicked (*this); };
           #endif
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

class DineApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DINE"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        // Before anything reads a folder: the DLIVE ones become DINE's (see AppFolders.h).
        AppFolders::migrateFromDlive();
        // First, so a run that ends badly from here on is one the next launch can report.
        {
            Telemetry::Config tc;
            const auto env = [] (const char* name, const char* fallback)
            {
                const auto v = juce::SystemStats::getEnvironmentVariable (name, {});
                return v.isNotEmpty() ? v : juce::String (fallback);
            };
            tc.url = env ("DINE_SUPABASE_URL", DINE_SUPABASE_URL);
            tc.anonKey = env ("DINE_SUPABASE_ANON_KEY", DINE_SUPABASE_ANON_KEY);
            tc.folder = lastSessionPointer().getParentDirectory();
            tc.appVersion = getApplicationVersion();
            telemetry = std::make_unique<Telemetry> (std::move (tc));
            telemetry->start();
        }

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

        // A pointer at a document that was never written is a session that was never saved -
        // its autosave is what the recovery below finds - not a failure to read one.
        if (lastDocument.existsAsFile() && ! restored) trackError ("session", "restore_failed", true);

        window = std::make_unique<MainWindow> (getApplicationName(), *controller, *services);
        wireTelemetry();

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
            // Checked against what the session uses, not assumed: a device can come back in a
            // different channel mode.
            int inputsNeeded = 0, outputsNeeded = 0;
            for (const auto& in : controller->getSession().inputs)
                inputsNeeded = std::max ({ inputsNeeded, in.inputA + 1, in.inputB + 1 });
            const auto& feeds = controller->getOutputFeeds();
            for (int f = 0; f < feeds.count; ++f)
                outputsNeeded = std::max ({ outputsNeeded, feeds.feeds[size_t (f)].left + 1, feeds.feeds[size_t (f)].right + 1 });
            window->view().showToast (deviceBackSentence (device.input.isNotEmpty() ? device.input : device.output,
                                                          device.inputChannels, device.outputChannels,
                                                          device.inputChannels > 0 ? inputsNeeded : 0, outputsNeeded));
        };
        host->onDifferentUnitReturned = [this] (juce::String name)
        {
            if (window == nullptr) return;
            window->view().showToast ("A " + name + " was plugged in, but it is not the one this session was running on - "
                                      "it is another unit with the same name. DINE has not opened it. If it is the right "
                                      "one, choose it under Audio device and check the inputs.");
        };
        // Something was plugged in or pulled out. The chrome and the status foot follow the
        // device on their own tick; the one thing that does not is the list of devices on the
        // set-up page, and only when somebody is looking at it.
        host->onDeviceListChanged = [this]
        {
            if (services != nullptr) services->forgetDeviceUids();
            if (window == nullptr) return;
            if (services != nullptr)
                if (const auto said = services->openWantedConsoleIfBack(); said.isNotEmpty())
                    window->view().showToast (said);
            auto& page = window->view().getDevicePage();
            if (page.isVisible()) page.refresh();
        };

        // Found before the session is restored, because the two sheets are one at a time and a
        // recovery is the bigger question: when there is one, it is what the window opens with.
        auto found = SessionAutosave::check (lastDocument);
        // A DOCUMENT THAT CANNOT BE READ IS NOT THE END OF THE MIX. A clean quit deletes the
        // autosave, so one still here means the work in it is newer than anything that quit
        // wrote - and with the document unreadable, it is the only copy there is.
        const bool newer = SessionStore::savedByNewerBuild (lastDocument);
        if (! restored && ! newer && lastDocument.existsAsFile() && ! found.offer)
        {
            const auto a = SessionAutosave::autosaveFor (lastDocument);
            SessionState probe;
            if (a.existsAsFile() && SessionStore::load (a, probe))
            {
                found.offer = true;
                found.autosave = a;
                found.when = a.getLastModificationTime();
                found.sentence = "The saved session could not be read, but its autosave could.";
            }
        }
        if (newer && window != nullptr)
            juce::MessageManager::callAsync ([this] { if (window != nullptr) window->view().showToast (
                "The last session was saved by a newer DINE, so it was not opened. Update DINE to open it."); });

        if (restored)
        {
            // Exactly the same path as opening it from the library, so there is one way a
            // session comes back: the document first, then whatever devices this Mac has.
            const bool asking = found.offer;
            auto restore = [this, state, asking] (bool withInputs)
            {
                services->openState (state, "launch", withInputs, ! asking);
                window->view().showPage (controller->getSession().inputs.empty() ? MainView::Page::Assign
                                                                                 : MainView::Page::Tracks);
                const auto note = services->takeRecoveryNote();
                if (note.isNotEmpty()) window->view().showToast (note);
            };

            // THE MICROPHONE PROMPT, SAID FIRST AND IN DINE'S OWN WORDS.
            //
            // macOS puts its prompt up the moment a process starts listening, and restoring a
            // session starts listening a second after launch - before anybody has asked for
            // anything and with nothing on screen to explain it. DINE reads a console, not the
            // room's microphone, but macOS has one switch for every audio input and no way to
            // tell them apart, so the only thing that can be done about it is to say so before
            // it happens. Once: after macOS has an answer this is never seen again.
            const bool askFirst = ! found.offer
                               && state.devices.consoleInput.isNotEmpty()
                               && MicPermission::check() == MicPermission::State::Undetermined;
            if (askFirst)
            {
                MainView::MicrophoneAsk ask;
                ask.device = state.devices.consoleInput;
                ask.onContinue = [this, restore] { services->askForInputPermission ([restore] (bool) { restore (true); }); };
                ask.onNotNow   = [restore] { restore (false); };
                window->view().explainMicrophone (std::move (ask));
            }
            else restore (true);
        }

        // DINE did not get to say goodbye last time, and the autosave holds work the document
        // does not. Asked once, after the window is up, in the words of what was lost rather
        // than in the words of what went wrong.
        if (found.offer)
        {
            trackEvent ("recovery_offered", { { "after_crash", telemetry->previousRunEndedBadly() } });
            juce::MessageManager::callAsync ([this, found, lastDocument] { offerRecovery (found, lastDocument); });
        }
    }

    // What the usage events and the stability reports read, and the one thing they say back:
    // a milestone, as a toast, the moment it is earned. docs/ANALYTICS.md.
    void wireTelemetry()
    {
        controller->onUsage = [] (const MixController::UsageEvent& e)
        {
            juce::NamedValueSet p;
            for (const auto& [k, v] : e.words)
                p.set (juce::Identifier (k), v == "true" ? juce::var (true) : v == "false" ? juce::var (false) : juce::var (juce::String (v)));
            for (const auto& [k, v] : e.numbers)
                p.set (juce::Identifier (k), v == std::floor (v) && std::abs (v) < 1.0e9 ? juce::var (int (v)) : juce::var (v));
            trackEvent (e.name, p);
        };
        telemetry->onMilestone = [this] (const Telemetry::Milestone& m)
        {
            if (window != nullptr) window->view().showToast ("Milestone: " + juce::String (m.title) + ". " + m.sentence);
        };
        telemetry->probe = [this]
        {
            Telemetry::Probe p;
            if (host == nullptr || controller == nullptr || dawEngine == nullptr) return p;
            p.audioRunning = host->isOpen();
            p.deviceStopped = host->deviceStoppedUnexpectedly();
            p.sampleRate = host->getSampleRate();
            p.bufferSize = host->getBufferSize();
            p.xruns = host->getXRunCount();
            p.cpu = p.audioRunning ? host->getDeviceManager().getCpuUsage() : -1.0;
            p.deviceInputs = host->getNumInputChannels();
            p.deviceOutputs = host->getNumOutputChannels();
            p.inputs = int (controller->getSession().inputs.size());
            p.tracks = int (dawEngine->getProject().tracks.size());
            // What kind of device, from CoreAudio's transport type, looked up once per device.
            // The model is only kept for hardware whose name the maker gave it: an aggregate or
            // a pair of headphones is often called after its owner.
            const auto device = host->state();
            const auto name = device.input.isNotEmpty() ? device.input : device.output;
            if (name != probedDevice)
            {
                probedDevice = name;
                probedKind.clear();
                probedModel.clear();
                if (name.isNotEmpty() && MonitorDevice::available())
                {
                    const auto d = MonitorDevice::findDevice (name);
                    using Kind = MonitorDevice::Device::Kind;
                    probedKind = d.uid.isEmpty() ? "unknown"
                               : d.isAggregate ? "aggregate"
                               : d.kind == Kind::Interface ? "interface"
                               : d.kind == Kind::BuiltIn ? "builtin"
                               : d.kind == Kind::Bluetooth ? "bluetooth"
                               : d.kind == Kind::Display ? "display" : "virtual";
                    if (probedKind == "interface" || probedKind == "builtin") probedModel = name;
                }
            }
            p.deviceKind = probedKind;
            p.deviceModel = probedModel;
            p.recording = dawEngine->isRecording();
            p.armed = dawEngine->getProject().numArmed();
            p.recordingSeconds = p.recording ? dawEngine->getRecordingSeconds() : 0.0;
            p.recordError = dawEngine->getRecorder().getErrorCode();
            const auto stage = controller->getStage();
            p.activity = p.recording ? "recording"
                       : controller->isTuningLive() ? "tune_live"
                       : stage == MixController::Stage::Listening || stage == MixController::Stage::Planning ? "tuning"
                       : controller->isAutopilotOn() ? "autopilot"
                       : p.audioRunning ? "mixing" : "idle";
            return p;
        };
    }

    // An exception the message loop caught and carried on from. The type and where, never
    // what(): its words can be anything, a file name included.
    void unhandledException (const std::exception* e, const juce::String& sourceFile, int line) override
    {
        trackError ("app", "unhandled_exception", true,
                    { { "type", e != nullptr ? juce::String (typeid (*e).name()) : juce::String ("unknown") },
                      { "where", sourceFile.fromLastOccurrenceOf ("/", false, false) + ":" + juce::String (line) } });
    }

    // Recover / Open last saved / Keep both. Nothing is deleted by any of the three: "keep
    // both" writes the recovered work as its own session, and the other two only remove the
    // autosave once the choice has been carried out.
    void offerRecovery (const SessionAutosave::Recovery& found, const juce::File& document)
    {
        if (window == nullptr || services == nullptr) return;

        // What each one holds, so the choice is between two mixes rather than two clocks.
        const auto facts = [] (const juce::File& file)
        {
            juce::StringArray out;
            const auto sum = SessionStore::summarise (file);
            if (! sum.valid) return out;
            int groups = 0;
            for (int b = 0; b < int (MixBus::Master); ++b) if (sum.perBus[size_t (b)] > 0) ++groups;
            out.add ("Mix: " + juce::String (sum.inputs) + " channels, " + juce::String (groups)
                         + (groups == 1 ? " group" : " groups"));
            out.add (sum.tuneCount > 0 ? "Tuned " + juce::String (sum.tuneCount) + (sum.tuneCount == 1 ? " time" : " times")
                                       : juce::String ("Not tuned yet"));
            out.add (sum.tracks > 0 ? juce::String (sum.tracks) + (sum.tracks == 1 ? " track recorded" : " tracks recorded")
                                    : juce::String ("Nothing recorded"));
            if (sum.inputDevice.isNotEmpty()) out.add (sum.inputDevice);
            return out;
        };

        MainView::RecoveryOffer offer;
        offer.sentence = found.sentence;
        offer.autosaveWhen = found.when.toString (false, true, false, true);
        offer.documentWhen = found.documentWhen.toString (false, true, false, true);
        offer.autosaveFacts = facts (found.autosave);
        offer.documentFacts = facts (document);

        const auto autosave = found.autosave;
        const auto when = found.when;
        auto* view = &window->view();
        auto* srv = services.get();

        const bool afterCrash = telemetry != nullptr && telemetry->previousRunEndedBadly();
        offer.onOpenSaved = [view, srv, document, afterCrash]
        {
            trackEvent ("session_recovery", { { "choice", "open_saved" }, { "ok", true }, { "after_crash", afterCrash } });
            SessionAutosave::dismissRecovery (document);
            const auto takes = srv->recoverTakesNow();
            if (takes.isNotEmpty()) view->showToast (takes);
        };
        offer.onRecover = [this, view, srv, autosave, document, when, afterCrash]
        {
            SessionState recovered;
            const bool ok = SessionStore::load (autosave, recovered);
            trackEvent ("session_recovery", { { "choice", "recover" }, { "ok", ok }, { "after_crash", afterCrash } });
            if (! ok)
            {
                trackError ("session", "autosave_unreadable", false);
                SessionAutosave::dismissRecovery (document);
                const auto takes = srv->recoverTakesNow();
                view->showToast ("That autosave could not be read, so the session on disk is the one you have."
                                 + (takes.isEmpty() ? juce::String() : " " + takes));
                return;
            }
            srv->openState (recovered, "recovery");
            view->sessionReplaced();
            // The copy that was offered goes only once the recovery is on the disk as the
            // document: until then it is the only copy, and it is offered again next launch.
            if (srv->saveSession())
            {
                SessionAutosave::dismissRecovery (document);
                const auto more = srv->takeRecoveryNote();
                view->showToast ("Recovered. The work from " + when.toString (false, true, false, true)
                                 + " is back, and the session has been saved." + (more.isEmpty() ? juce::String() : " " + more));
            }
            else
            {
                view->showToast ("Recovered, but it could not be saved - check the disk and press Save. "
                                 "Until it is saved, DINE keeps offering it back.");
            }
        };
        offer.onKeepBoth = [this, view, srv, autosave, document, afterCrash]
        {
            SessionState recovered;
            const bool ok = SessionStore::load (autosave, recovered);
            trackEvent ("session_recovery", { { "choice", "keep_both" }, { "ok", ok }, { "after_crash", afterCrash } });
            if (! ok)
            {
                trackError ("session", "autosave_unreadable", false);
                SessionAutosave::dismissRecovery (document);
                const auto takes = srv->recoverTakesNow();
                view->showToast ("That autosave could not be read, so the session on disk is the one you have."
                                 + (takes.isEmpty() ? juce::String() : " " + takes));
                return;
            }
            // Keep both: the recovered work becomes a session of its own, beside the one that
            // was saved, and the takes stay where they are (saveSessionAs makes their clips
            // absolute for exactly this). The offered copy goes once that has landed.
            const auto name = SessionStore::unusedName (juce::String (recovered.session.name) + " (recovered)");
            srv->openState (recovered, "recovery");
            const auto err = srv->saveSessionAs (name);
            if (err.isEmpty()) SessionAutosave::dismissRecovery (document);
            view->sessionReplaced();
            view->showToast (err.isEmpty()
                ? "Both are here: the recovered work is now \"" + name + "\", and the session you saved is untouched."
                : err);
        };
        // Never saved, or saved and now unreadable: there is no "last saved" to choose instead,
        // so the only answer that does not lose the work is the one given without asking.
        SessionState readable;
        if (! document.existsAsFile() || ! SessionStore::load (document, readable))
        {
            offer.onRecover();
            return;
        }
        window->view().offerRecovery (std::move (offer));
    }

    void shutdown() override
    {
        // An export's worker renders through `services`; it is stopped, and waited for, before
        // anything it reads is destroyed. Its half-written files are deleted by MixBounce.
        if (window != nullptr) window->view().stopExportAndWait (15000);
        if (dawEngine != nullptr) dawEngine->stop();
        // A clean goodbye: the document is written, and the marker and the autosave go with
        // it. One that is still there on the next launch is how DINE knows it was killed.
        // A document that could not be written leaves the autosave and the marker where they
        // are, so the next launch offers the work back instead of finding nothing.
        if (services != nullptr)
        {
            if (services->saveForQuit()) services->autosaveWriter().closeCleanly();
            else                         services->autosaveWriter().flush();
        }
        if (telemetry != nullptr) telemetry->end();
        if (controller != nullptr) controller->onUsage = nullptr;
        window.reset();
        host.reset();
        services.reset();
        dawEngine.reset();
        controller.reset();
        telemetry.reset();
    }

    // Quitting mid-take would end the service's recording without a word. The take is
    // always flushed (shutdown() stops the engine first), but ending it has to be a
    // decision, not an accident - so the question says exactly what happens either way.
    void systemRequestedQuit() override
    {
        // An export quits the same way a take does: by a decision. Stopping it deletes what it
        // had written; nothing half-made is left looking like a finished file.
        if (window != nullptr && window->view().isExporting() && (dawEngine == nullptr || ! dawEngine->isRecording()))
        {
            auto* alert = new juce::AlertWindow ("DINE is exporting",
                                                 "Quitting stops the export, and the files it had started are deleted. "
                                                 "The session and its recordings are not touched.",
                                                 juce::MessageBoxIconType::NoIcon);
            alert->addButton ("Keep Exporting", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            alert->addButton ("Stop and Quit", 1, juce::KeyPress (juce::KeyPress::returnKey));
            alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert] (int r)
            {
                std::unique_ptr<juce::AlertWindow> closer (alert);
                if (r == 1) quit();
            }), true);
            return;
        }
        if (dawEngine == nullptr || ! dawEngine->isRecording()) { quit(); return; }

        auto* alert = new juce::AlertWindow ("DINE is recording",
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
    std::unique_ptr<Telemetry> telemetry;        // first in, last out: it sees the whole run
    juce::String probedDevice, probedKind, probedModel;
    std::unique_ptr<SampleLibrary> samples;      // before the controller: destroyed after it
    std::unique_ptr<MixController> controller;
    std::unique_ptr<DawEngine> dawEngine;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<HostServices> services;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (DineApplication)
