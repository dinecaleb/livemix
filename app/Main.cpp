// DINELIVE: the standalone live/broadcast mixing application.
//   Console / interface / Dante -> DINELIVE -> OBS / Ecamm / recording
// One MixController owns the mix, one AudioHost owns the device, MainView shows one
// page at a time. The last session (device, assignments, purpose, sound, macros and
// the kept mix) is saved on quit and reloaded on launch.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "native/MixController.h"
#include "native/AudioHost.h"
#include "native/SessionStore.h"
#include "native/MultitrackSource.h"
#include "ui/MainView.h"

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
            for (const auto& d : host.listInputDevices()) out.add ({ d.name, d.inputChannels });
            return out;
        }
        juce::Array<Device> outputDevices() override
        {
            juce::Array<Device> out;
            for (const auto& d : host.listOutputDevices()) out.add ({ d.name, d.outputChannels });
            return out;
        }
        juce::String openDevices (const juce::String& input, const juce::String& output) override { return host.open (input, output); }
        bool isAudioRunning() override { return host.isOpen(); }
        int numInputChannels() override { return host.getNumInputChannels(); }
        double sampleRate() override { return host.getSampleRate(); }
        int bufferSize() override { return host.getBufferSize(); }
        int xrunCount() override { return host.getXRunCount(); }
        void reconfigure() override { host.reconfigure(); }
        juce::String currentInputDevice() override { return host.getInputDeviceName(); }
        juce::String currentOutputDevice() override { return host.getOutputDeviceName(); }

        juce::String openRecording (const juce::File& folder, const juce::String& outputDevice) override
        {
            const juce::String err = recording.load (folder);
            if (err.isNotEmpty()) return err;
            juce::String out = outputDevice;
            if (out.isEmpty()) { const auto outs = host.listOutputDevices(); if (! outs.isEmpty()) out = outs[0].name; }
            return host.openPlayback (recording, out);
        }
        bool isPlayingRecording() override { return host.isPlayback(); }
        MixSession recordingSuggestion (const MixSession& base) override { return recording.suggestedSession (base); }

        void saveSession() override
        {
            SessionStore::Document d;
            d.session = controller.getSession();
            d.inputDevice = host.getInputDeviceName();
            d.outputDevice = host.getOutputDeviceName();
            d.macros = controller.getMacros();
            d.tuneCount = controller.getTuneCount();
            d.hasMix = controller.isPrepared() && controller.getTuneCount() > 0;
            if (d.hasMix) d.mix = controller.getKept();
            const auto file = SessionStore::fileFor (juce::String (d.session.name));
            if (SessionStore::save (d, file)) lastSessionPointer().replaceWithText (file.getFullPathName());
        }

    private:
        MixController& controller;
        AudioHost& host;
        MultitrackSource recording;
    };

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, MixController& c, AppServices& s)
            : juce::DocumentWindow (name, Tokens::ground, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainView (c, s), true);
            setResizable (true, true);
            setResizeLimits (900, 620, 4000, 3000);
            centreWithSize (1160, 780);
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

        // Last session: assignments, purpose and sound come back; the device is confirmed on the first page.
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
            // Reopen the stored device when it is still there; the page shows it either way.
            if (restored->inputDevice.isNotEmpty() && host->open (restored->inputDevice, restored->outputDevice).isEmpty())
            {
                if (restored->hasMix) { controller->setKept (restored->mix); }
                for (int i = 0; i < int (MixMacro::Count); ++i) controller->setMacro (MixMacro (i), restored->macros.get (MixMacro (i)));
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
