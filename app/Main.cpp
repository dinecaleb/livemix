// DINELIVE: the standalone live/broadcast mixing application.
//   Console / interface / Dante -> DINELIVE -> OBS / Ecamm / recording
// One MixController owns the mix, one AudioHost owns the device, MainView shows one
// page at a time. Mixes are saved as named .dinelive.json documents; the last one
// reloads on launch. Output can be changed any time without wiping the mix.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "native/MixController.h"
#include "native/AudioHost.h"
#include "native/SessionStore.h"
#include "native/MultitrackSource.h"
#include "native/MixBounce.h"
#include "ui/MainView.h"
#include <optional>

using namespace livemix;

namespace
{
    juce::File lastSessionPointer()
    {
        return SessionStore::sessionsFolder().getParentDirectory().getChildFile ("last-session.txt");
    }

    class HostServices : public AppServices
    {
    public:
        HostServices (MixController& c, AudioHost& h) : controller (c), host (h) {}

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
        juce::String openDevices (const juce::String& input, const juce::String& output) override { return host.open (input, output); }

        juce::String changeOutput (const juce::String& output) override
        {
            // Snapshot the mix, swap the device (prepare rebuilds the graph), then put the mix back.
            SessionStore::Document snap;
            snap.session = controller.getSession();
            snap.macros = controller.getMacros();
            snap.tuneCount = controller.getTuneCount();
            snap.hasMix = controller.isPrepared() && controller.hasKeptMix();
            if (snap.hasMix) snap.mix = controller.getKept();
            holdMix (snap);
            const juce::String err = host.setOutputDevice (output);
            applyPendingMix();
            if (err.isEmpty()) saveSession();
            return err;
        }

        bool isAudioRunning() override { return host.isOpen(); }
        int numInputChannels() override { return host.getNumInputChannels(); }
        double sampleRate() override { return host.getSampleRate(); }
        int bufferSize() override { return host.getBufferSize(); }
        int xrunCount() override { return host.getXRunCount(); }
        bool deviceStopped() override { return host.deviceStoppedUnexpectedly(); }
        void reconfigure() override
        {
            host.reconfigure();
            applyPendingMix();
        }

        void holdMix (const SessionStore::Document& doc) { pending = doc; }
        void applyPendingMix()
        {
            if (! pending.has_value() || ! controller.isPrepared()) return;
            const auto& now = controller.getSession().inputs;
            const auto& then = pending->session.inputs;
            bool same = now.size() == then.size();
            for (size_t i = 0; same && i < now.size(); ++i) same = now[i].role == then[i].role && now[i].name == then[i].name;
            if (same && pending->hasMix)
            {
                controller.restoreKept (pending->mix, pending->tuneCount);
                for (int i = 0; i < int (MixMacro::Count); ++i) controller.setMacro (MixMacro (i), pending->macros.get (MixMacro (i)));
            }
            pending.reset();
        }
        juce::String currentInputDevice() override { return host.getInputDeviceName(); }
        juce::String currentOutputDevice() override { return host.getOutputDeviceName(); }
        juce::String currentSessionName() override { return juce::String (controller.getSession().name); }

        juce::String openRecording (const juce::File& folder, const juce::String& outputDevice) override
        {
            const juce::String err = recording.load (folder);
            if (err.isNotEmpty()) return err;
            juce::String out = outputDevice;
            if (out.isEmpty()) { const auto outs = host.listOutputDevices(); if (! outs.isEmpty()) out = outs[0].name; }
            return host.openPlayback (recording, out);
        }
        bool isPlayingRecording() override { return host.isPlayback(); }
        juce::File recordingFolder() const override { return recording.isLoaded() ? recording.getFolder() : juce::File(); }
        MixSession recordingSuggestion (const MixSession& base) override { return recording.suggestedSession (base); }

        juce::String exportMix (const MixSession& session,
                                const MixParameters& params,
                                const juce::File& stemsFolder,
                                const juce::File& dest,
                                ExportFormat format) override
        {
            const auto folder = stemsFolder.isDirectory() ? stemsFolder : recordingFolder();
            if (! folder.isDirectory())
                return "Export needs a folder of stems. Use Play a recording… on the Audio device page first.";
            return MixBounce::renderToFile (session,
                                            params,
                                            folder,
                                            dest,
                                            format == ExportFormat::Mp3 ? MixBounce::Format::Mp3 : MixBounce::Format::Wav);
        }

        void saveSession() override
        {
            const auto file = SessionStore::fileFor (juce::String (controller.getSession().name));
            writeDocument (file);
        }

        juce::String saveSessionAs (const juce::String& name) override
        {
            juce::String n = name.trim();
            if (n.isEmpty()) return "Give the mix a name.";
            controller.setSessionName (n.toStdString());
            const auto file = SessionStore::fileFor (n);
            if (! writeDocument (file)) return "Could not save the mix.";
            return {};
        }

        juce::String loadSession (const juce::File& file) override
        {
            SessionStore::Document doc;
            if (! SessionStore::load (file, doc)) return "That file is not a DINELIVE mix.";
            controller.setSession (doc.session);
            holdMix (doc);
            juce::String err;
            if (doc.inputDevice.isNotEmpty())
                err = host.open (doc.inputDevice, doc.outputDevice.isNotEmpty() ? doc.outputDevice : doc.inputDevice);
            else if (host.isOpen())
                host.reconfigure();
            applyPendingMix();
            if (err.isEmpty()) lastSessionPointer().replaceWithText (file.getFullPathName());
            return err;
        }

        juce::Array<SessionStore::Listing> listSessions() override { return SessionStore::listSessions(); }

    private:
        bool writeDocument (const juce::File& file)
        {
            SessionStore::Document d;
            d.session = controller.getSession();
            d.inputDevice = host.getInputDeviceName();
            d.outputDevice = host.getOutputDeviceName();
            d.macros = controller.getMacros();
            d.tuneCount = controller.getTuneCount();
            d.hasMix = controller.isPrepared() && controller.hasKeptMix();
            if (d.hasMix) d.mix = controller.getKept();
            else if (pending.has_value() && pending->hasMix) { d.hasMix = true; d.mix = pending->mix; d.tuneCount = pending->tuneCount; }
            if (! SessionStore::save (d, file)) return false;
            lastSessionPointer().replaceWithText (file.getFullPathName());
            return true;
        }

        MixController& controller;
        AudioHost& host;
        MultitrackSource recording;
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
            setResizeLimits (1120, 720, 4000, 3000);
            centreWithSize (1400, 920);
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainView& view() { return *dynamic_cast<MainView*> (getContentComponent()); }
    };
}

class DineLiveApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DINELIVE"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        controller = std::make_unique<MixController>();
        host = std::make_unique<AudioHost> (*controller);
        services = std::make_unique<HostServices> (*controller, *host);

        SessionStore::Document doc;
        const auto pointer = lastSessionPointer();
        if (pointer.existsAsFile() && SessionStore::load (juce::File (pointer.loadFileAsString().trim()), doc))
        {
            controller->setSession (doc.session);
            restored = doc;
        }
        window = std::make_unique<MainWindow> (getApplicationName(), *controller, *services);
        if (restored.has_value())
        {
            services->holdMix (*restored);
            if (restored->inputDevice.isNotEmpty() && host->open (restored->inputDevice, restored->outputDevice).isEmpty())
            {
                services->applyPendingMix();
                window->view().showPage (controller->getSession().inputs.empty() ? MainView::Page::Assign : MainView::Page::Mix);
            }
        }
    }

    void shutdown() override
    {
        if (services != nullptr && controller != nullptr && ! controller->getSession().inputs.empty()) services->saveSession();
        window.reset();
        host.reset();
        services.reset();
        controller.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MixController> controller;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<HostServices> services;
    std::unique_ptr<MainWindow> window;
    std::optional<SessionStore::Document> restored;
};

START_JUCE_APPLICATION (DineLiveApplication)
