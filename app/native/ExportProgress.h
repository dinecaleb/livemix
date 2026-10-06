#pragma once
#include <atomic>
#include <juce_core/juce_core.h>
#include "MixBounce.h"

namespace livemix
{

// WHERE AN EXPORT IS, for the status foot (2026-10-05).
//
// An export runs on a worker for minutes; the window has to say so for every one of them, on
// every workspace, even after the Export sheet has gone. The worker writes these atomics and
// nothing else; the message thread reads them on its 30 Hz tick. `MixBounce` reports progress
// once a block (~47 times a second of audio), which is why it lands in an atomic rather than in
// a message per call.
//
// Nothing here is on the audio thread: an export is a worker and the message thread.
struct ExportProgress
{
    enum class State { Idle = 0, Running, Done, Failed, Cancelled };

    std::atomic<int> state { int (State::Idle) };
    std::atomic<int> stage { int (MixBounce::Stage::Mixing) };
    std::atomic<float> fraction { -1.0f };   // 0..1 for the stage under way; < 0 = no number for it
    std::atomic<bool> cancel { false };      // asked for by a person, or by quitting
    std::atomic<bool> workerBusy { false };  // true from launch until exportMix has returned

    // The worker's side. A stage with no number of its own (the MP3 encoder) says so with -1.
    void beginStage (MixBounce::Stage s, bool measurable) noexcept
    {
        stage.store (int (s));
        fraction.store (measurable ? 0.0f : -1.0f);
    }
    bool report (float f) noexcept   // the MixBounce callback: false = stop; f < 0 only asks
    {
        if (f >= 0.0f) fraction.store (juce::jmin (1.0f, f));
        return ! cancel.load();
    }

    State getState() const noexcept { return State (state.load()); }
    bool isRunning() const noexcept { return getState() == State::Running; }
};

// What the status foot says. Sentence case, a verb in the -ing form while it works, and a
// number only where the stage has one. Pure, so dine_app_tests checks every case.
inline juce::String exportStageWords (MixBounce::Stage s, MixBounce::What what)
{
    switch (s)
    {
        case MixBounce::Stage::Mixing:
            return what == MixBounce::What::GroupStems ? "writing the stems"
                 : what == MixBounce::What::RawMultitrack ? "writing the multitrack"
                                                          : "mixing";
        case MixBounce::Stage::Loudness: return "setting the loudness";
        case MixBounce::Stage::Encoding: return "making the MP3";
        case MixBounce::Stage::Finishing: return "finishing";
    }
    return "working";
}

inline juce::String exportStatusText (const ExportProgress& p, MixBounce::What what)
{
    switch (p.getState())
    {
        case ExportProgress::State::Idle:      return {};
        case ExportProgress::State::Done:      return "done";
        case ExportProgress::State::Failed:    return "failed";
        case ExportProgress::State::Cancelled: return "stopped";
        case ExportProgress::State::Running:   break;
    }
    const auto words = exportStageWords (MixBounce::Stage (p.stage.load()), what);
    const float f = p.fraction.load();
    if (f < 0.0f) return words;
    // Never "100 %" while the worker is still inside the render: the files are not closed yet.
    return words + " " + juce::String (juce::jmin (99, int (f * 100.0f))) + "%";
}

} // namespace livemix
