// Real-device soak check for the DINELIVE audio host: opens a device through AudioHost with a
// small session, runs the callback for a few seconds and reports what the engine saw.
//   dinelive_device_check [seconds=5] [input device name] [output device name] [buffer=64]
// Without names the device with the most inputs is used for input and the default output for output.
#include <juce_events/juce_events.h>
#include "native/MixController.h"
#include "native/AudioHost.h"
#include "native/MultitrackSource.h"
#include <cstdio>
#include <thread>

using namespace livemix;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double seconds = argc > 1 ? juce::String (argv[1]).getDoubleValue() : 5.0;
    juce::String inputName = argc > 2 ? juce::String (argv[2]) : juce::String();
    juce::String outputName = argc > 3 ? juce::String (argv[3]) : juce::String();
    const int buffer = argc > 4 ? juce::String (argv[4]).getIntValue() : 64;

    MixController controller;
    AudioHost host (controller);
    MultitrackSource recording;

    // A folder as the second argument plays that multitrack as the inputs.
    if (inputName.isNotEmpty() && juce::File (inputName).isDirectory())
    {
        const juce::String err = recording.load (juce::File (inputName));
        if (err.isNotEmpty()) { std::printf ("recording: %s\n", err.toRawUTF8()); return 1; }
        std::printf ("RECORDING %s: %d files, %d inputs, %.0f Hz, %.0f s\n", recording.getFolder().getFileName().toRawUTF8(), int (recording.getTracks().size()),
                     recording.getTotalChannels(), recording.getFileSampleRate(), recording.getLengthSeconds());
        controller.setSession (recording.suggestedSession (MixSession {}));
        for (const auto& in : controller.getSession().inputs)
            std::printf ("  in %2d%s %-14s -> %s%s\n", in.inputA + 1, in.inputB >= 0 ? "/" : " ", in.name.c_str(), channelRoleName (in.role), in.enabled ? "" : "  (not recognised, left unassigned)");
        const auto outs = host.listOutputDevices();
        if (outputName.isEmpty() && ! outs.isEmpty()) outputName = outs[0].name;
        const juce::String openErr = host.openPlayback (recording, outputName, buffer);
        if (openErr.isNotEmpty()) { std::printf ("open failed: %s\n", openErr.toRawUTF8()); return 1; }
        std::printf ("playing through '%s': %.0f Hz, %d samples, %d strips, latency %d\n", outputName.toRawUTF8(), host.getSampleRate(), host.getBufferSize(),
                     controller.getEngine().getNumStrips(), controller.getEngine().getLatencySamples());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        std::vector<float> peaks (size_t (controller.getEngine().getNumStrips()), -120.0f);
        float outPeak = -120.0f;
        while (juce::Time::getMillisecondCounterHiRes() - t0 < seconds * 1000.0)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            controller.poll();
            for (size_t i = 0; i < peaks.size(); ++i) peaks[i] = std::max (peaks[i], controller.getEngine().getStrip (int (i)).getInputMeter().consumeMaxPeakDb());
            outPeak = std::max (outPeak, controller.getEngine().getBus (MixBus::Master).getOutputMeter().consumeMaxPeakDb());
        }
        const auto st = controller.getEngine().getStats();
        std::printf ("after %.1f s: %d blocks, peak %.0f us, %d dropouts, position %.1f s, master out peak %.1f dBFS\n", seconds, st.blocks, double (st.peakBlockMicros), host.getXRunCount(),
                     recording.getPositionSeconds(), double (outPeak));
        int heard = 0;
        for (size_t i = 0; i < peaks.size(); ++i) { std::printf ("  strip %2d %-14s input peak %6.1f dBFS\n", int (i) + 1, controller.getGraph().strips[i].name.c_str(), double (peaks[i])); if (peaks[i] > -60.0f) ++heard; }
        host.close();
        std::printf ("closed. %d of %d strips carried audio. %s\n", heard, int (peaks.size()), st.blocks > 0 && heard > 0 ? "OK" : "PROBLEM");
        return st.blocks > 0 && heard > 0 ? 0 : 2;
    }

    std::printf ("INPUT DEVICES\n");
    const auto inputs = host.listInputDevices();
    for (const auto& d : inputs) std::printf ("  %-40s %d inputs\n", d.name.toRawUTF8(), d.inputChannels);
    std::printf ("OUTPUT DEVICES\n");
    const auto outputs = host.listOutputDevices();
    for (const auto& d : outputs) std::printf ("  %-40s %d outputs\n", d.name.toRawUTF8(), d.outputChannels);
    if (inputs.isEmpty() || outputs.isEmpty()) { std::printf ("no devices\n"); return 1; }

    if (inputName.isEmpty())
    {
        int best = 0;
        for (int i = 1; i < inputs.size(); ++i) if (inputs[i].inputChannels > inputs[best].inputChannels) best = i;
        inputName = inputs[best].name;
    }
    if (outputName.isEmpty()) outputName = outputs[0].name;

    // A session that uses whatever inputs the device has (up to 8), so strips see real audio.
    MixSession s;
    const ChannelRole roles[] = { ChannelRole::LeadVocal, ChannelRole::KickIn, ChannelRole::BassDI, ChannelRole::Piano, ChannelRole::BackingVocal, ChannelRole::SnareTop, ChannelRole::Room, ChannelRole::Speech };
    int channels = 1;
    for (const auto& d : inputs) if (d.name == inputName) channels = std::max (1, d.inputChannels);
    for (int i = 0; i < std::min (8, channels); ++i) s.inputs.push_back ({ "In " + std::to_string (i + 1), roles[i], i, -1 });
    controller.setSession (s);

    std::printf ("\nopening input '%s', output '%s', %d samples...\n", inputName.toRawUTF8(), outputName.toRawUTF8(), buffer);
    const juce::String err = host.open (inputName, outputName, 48000.0, buffer);
    if (err.isNotEmpty()) { std::printf ("open failed: %s\n", err.toRawUTF8()); return 1; }
    std::printf ("running: %.0f Hz, %d samples, %d input channels, engine prepared %s, %d strips, latency %d samples\n",
                 host.getSampleRate(), host.getBufferSize(), host.getNumInputChannels(), controller.isPrepared() ? "yes" : "NO",
                 controller.getEngine().getNumStrips(), controller.getEngine().getLatencySamples());

    const auto start = juce::Time::getMillisecondCounterHiRes();
    float inputPeak = -120.0f;
    while (juce::Time::getMillisecondCounterHiRes() - start < seconds * 1000.0)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        controller.poll();
        if (controller.isPrepared() && controller.getEngine().getNumStrips() > 0)
            inputPeak = std::max (inputPeak, controller.getEngine().getStrip (0).getInputMeter().consumeMaxPeakDb());
    }
    const auto stats = controller.getEngine().getStats();
    const double budget = host.getBufferSize() / host.getSampleRate() * 1.0e6;
    std::printf ("after %.1f s: %d blocks, last %.0f us, peak %.0f us (%.0f%% of the %.0f us budget), %d dropouts reported by the device, input 1 peak %.1f dBFS\n",
                 seconds, stats.blocks, double (stats.lastBlockMicros), double (stats.peakBlockMicros), 100.0 * stats.peakBlockMicros / budget, budget,
                 host.getXRunCount(), double (inputPeak));

    // Reconfigure while running: a changed session rebuilds the graph with the callback stopped.
    s.inputs.push_back ({ "Extra", ChannelRole::Organ, 0, -1 });
    controller.setSession (s);
    host.reconfigure();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    std::printf ("after reconfigure: %d strips, prepared %s, blocks since %d\n", controller.getEngine().getNumStrips(), controller.isPrepared() ? "yes" : "NO", controller.getEngine().getStats().blocks);

    host.close();
    std::printf ("closed. %s\n", stats.blocks > 0 ? "OK" : "NO CALLBACKS");
    return stats.blocks > 0 ? 0 : 2;
}
