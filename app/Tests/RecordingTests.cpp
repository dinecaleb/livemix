// RECORDING UNDER PRESSURE: a disk that falls behind, and a list of inputs that changes while a
// take runs. The ordinary take is tested in DawTests.cpp; these are the service-day cases.
#include "TestFramework.h"
#include "native/DawEngine.h"
#include "native/Recorder.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;

    juce::File scratch (const char* name)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dine-tests").getChildFile (name);
        f.deleteRecursively();
        f.createDirectory();
        return f;
    }

    juce::int64 lengthOf (const juce::File& f)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        return r != nullptr ? r->lengthInSamples : -1;
    }
}

TEST_CASE ("Recorder: a disk that falls behind leaves a counted gap, and every track still lines up")
{
    // A FIFO small enough that a burst of callbacks with no time for the disk overflows it.
    const auto folder = scratch ("behind");
    Recorder recorder (20.0, 15.0, 1024);
    std::vector<Recorder::Spec> specs { { 0, "Kick", 0, -1 }, { 1, "Keys", 1, 2 }, { 2, "Lead", 3, -1 } };
    REQUIRE (recorder.start (folder, specs, kSr, 0).isEmpty());

    std::vector<std::vector<float>> in (4, std::vector<float> (size_t (kBlock), 0.25f));
    std::vector<const float*> ip;
    for (auto& c : in) ip.push_back (c.data());
    const int blocks = 400;
    for (int b = 0; b < blocks; ++b) recorder.write (ip.data(), 4, kBlock);    // no pause: the disk cannot keep up

    CHECK (recorder.getDroppedSeconds() > 0.0);        // it fell behind, and it says so
    CHECK (recorder.getError().isEmpty());             // a stall is ridden out, not the end of the take
    const auto takes = recorder.stop();
    REQUIRE (takes.size() == 3);
    // Every file is exactly as long as the take: the gap is silence in the same place on every
    // track, never one track short and early against the others.
    for (const auto& t : takes)
    {
        CHECK (t.length == juce::int64 (blocks) * kBlock);
        CHECK (lengthOf (folder.getChildFile (t.fileName)) == juce::int64 (blocks) * kBlock);
    }
    folder.deleteRecursively();
}

TEST_CASE ("DawEngine: a take lands on the input it recorded, however the list of inputs moved while it ran")
{
    const auto folder = scratch ("moved");
    MixController controller;
    DawEngine daw (controller);
    MixSession s;
    s.name = "Moved";
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Lead", ChannelRole::LeadVocal, 4, -1 } };
    controller.setSession (s);
    daw.setSession (s);
    controller.prepare (kSr, kBlock);
    daw.prepare (kSr, kBlock);
    auto project = daw.getProject();
    project.folder = folder;
    project.tracks[1].armed = true;                     // the lead
    daw.setProject (project);
    REQUIRE (daw.startRecording().isEmpty());

    std::vector<std::vector<float>> in (8, std::vector<float> (size_t (kBlock), 0.2f));
    std::vector<const float*> ip;
    for (auto& c : in) ip.push_back (c.data());
    std::vector<float> l (static_cast<size_t> (kBlock)), r (static_cast<size_t> (kBlock));
    float* op[2] = { l.data(), r.data() };
    for (int b = 0; b < 40; ++b) daw.processBlock (ip.data(), 8, op, 2, kBlock);

    // An input is added ahead of the lead mid-take: index 1 is now the pastor.
    auto moved = s;
    moved.inputs.insert (moved.inputs.begin() + 1, { "Pastor", ChannelRole::Speech, 5, -1 });
    daw.setSession (moved);
    for (int b = 0; b < 40; ++b) daw.processBlock (ip.data(), 8, op, 2, kBlock);
    daw.stopRecording();

    const auto& after = daw.getProject();
    REQUIRE (after.tracks.size() == 3);
    CHECK (after.tracks[1].clips.empty());             // not on the pastor
    CHECK (after.tracks[2].clips.size() == 1);         // on the lead, where it belongs

    // An input taken away entirely: its take is kept on disk and the person is told, never dropped.
    auto p2 = daw.getProject();
    p2.tracks[0].armed = true;
    daw.setProject (p2);
    REQUIRE (daw.startRecording().isEmpty());
    for (int b = 0; b < 20; ++b) daw.processBlock (ip.data(), 8, op, 2, kBlock);
    auto without = moved;
    without.inputs.erase (without.inputs.begin());      // the kick goes
    daw.setSession (without);
    daw.stopRecording();
    CHECK (daw.takeStopNotice().contains ("Kick"));
    folder.deleteRecursively();
}

TEST_CASE ("DawEngine: a device that hands over a bigger callback than it was prepared for is processed in pieces")
{
    // The playback buffers are twice the prepared block; a bigger callback used to read past them.
    MixController controller;
    DawEngine daw (controller);
    MixSession s;
    s.name = "Big";
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Lead", ChannelRole::LeadVocal, 1, -1 } };
    controller.setSession (s);
    daw.setSession (s);
    controller.prepare (kSr, 64);
    daw.prepare (kSr, 64);
    const int big = 64 * 8;
    std::vector<std::vector<float>> in (2, std::vector<float> (size_t (big), 0.0f));
    for (int i = 0; i < big; ++i) in[1][size_t (i)] = 0.3f * std::sin (2.0f * float (M_PI) * 440.0f * float (i) / float (kSr));
    std::vector<const float*> ip { in[0].data(), in[1].data() };
    std::vector<float> l (static_cast<size_t> (big)), r (static_cast<size_t> (big));
    float* op[2] = { l.data(), r.data() };
    float peakLate = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        daw.processBlock (ip.data(), 2, op, 2, big);
        for (int i = big / 2; i < big; ++i) peakLate = std::max (peakLate, std::fabs (l[size_t (i)]));
    }
    bool finite = true;
    for (float x : l) finite = finite && std::isfinite (x);
    CHECK (finite);
    CHECK (peakLate > 0.01f);             // the second half of every callback was processed too
}

TEST_CASE ("Recorder: a take says when it began, so it lines up with the video beside it")
{
    const auto folder = scratch ("bwf");
    Recorder recorder;
    std::vector<Recorder::Spec> specs { { 0, "Pastor", 0, -1 } };
    REQUIRE (recorder.start (folder, specs, kSr, 0).isEmpty());
    std::vector<float> in (static_cast<size_t> (kBlock), 0.1f);
    const float* ip[1] = { in.data() };
    for (int b = 0; b < 20; ++b) recorder.write (ip, 1, kBlock);
    const auto takes = recorder.stop();
    REQUIRE (takes.size() == 1u);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (folder.getChildFile (takes[0].fileName)));
    REQUIRE (r != nullptr);
    CHECK (r->metadataValues[juce::WavAudioFormat::bwavOriginationDate].isNotEmpty());
    CHECK (r->metadataValues[juce::WavAudioFormat::bwavOriginator] == "DINE");
    CHECK (r->metadataValues[juce::WavAudioFormat::bwavTimeReference].isNotEmpty());
    folder.deleteRecursively();
}
