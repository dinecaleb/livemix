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
            // Snapshot the mix, swap the device (prepare rebuilds the graph), then put the mix back.
            hold();
            const juce::String err = host.setOutputDevice (output);
            applyPendingMix();
            if (err.isEmpty()) saveSession();
            return err;
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

        juce::String exportMix (const juce::File& dest, ExportFormat format, std::function<bool (float)> progress) override
        {
            MixBounce::Options options;
            options.onProgress = std::move (progress);
            return MixBounce::renderProject (controller.getSession(),
                                             controller.getRunning(),
                                             dawEngine.getProject(),
                                             dest,
                                             format == ExportFormat::Mp3 ? MixBounce::Format::Mp3 : MixBounce::Format::Wav,
                                             options);
        }

        void newSession() override
        {
            MixSession fresh;
            fresh.name = "Untitled";
            controller.setSession (fresh);
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

        // Restoring a mix that a device change is about to wipe.
        void holdMix (const SessionStore::Document& doc) { pending = doc; }
        void applyPendingMix()
        {
            if (! pending.has_value() || ! controller.isPrepared()) return;
            const auto& now = controller.getSession().inputs;
            const auto& then = pending->session.inputs;
            // The graph, not the labels: a source renamed still routes and sounds the same, so
            // a rename never costs the mix. A different source, channel or count does rebuild it.
            bool same = now.size() == then.size();
            for (size_t i = 0; same && i < now.size(); ++i)
                same = now[i].role == then[i].role && now[i].inputA == then[i].inputA && now[i].inputB == then[i].inputB;
            controller.setOutputFeeds (pending->outputs);   // routing belongs to the device, not the mix
            if (same && pending->hasMix)
            {
                controller.restoreKept (pending->mix, pending->tuneCount);
                for (int i = 0; i < int (MixMacro::Count); ++i) controller.setMacro (MixMacro (i), pending->macros.get (MixMacro (i)));
            }
            pending.reset();
        }

    private:
        void hold()
        {
            SessionStore::Document snap;
            snap.session = controller.getSession();
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
            else if (pending.has_value() && pending->hasMix) { d.hasMix = true; d.mix = pending->mix; d.tuneCount = pending->tuneCount; }
            if (! SessionStore::save (d, file)) return false;
            dawEngine.getProject().folder = file.getParentDirectory();
            lastSessionPointer().replaceWithText (file.getFullPathName());
            return true;
        }

        MixController& controller;
        DawEngine& dawEngine;
        AudioHost& host;
        std::optional<SessionStore::Document> pending;
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

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MixController> controller;
    std::unique_ptr<DawEngine> dawEngine;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<HostServices> services;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (DLiveApplication)
