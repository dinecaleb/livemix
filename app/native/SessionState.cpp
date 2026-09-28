#include "SessionState.h"
#include "DawEngine.h"
#include "MixController.h"
#include "SampleLibrary.h"
#include <juce_core/juce_core.h>

namespace livemix
{

// ---------------------------------------------------------------------------
// captureSession / applySession: the only two functions that move a session between the
// file and the live objects. Both are in DLIVE_APP_SOURCES, so both are tested. The code
// they replace lived in app/Main.cpp, which no test compiles - see docs/SESSION-STATE.md.
// ---------------------------------------------------------------------------

SessionState captureSession (const MixController& controller, const DawEngine& daw,
                             const DeviceChoice& devices, int trackPanelWidth)
{
    SessionState s;
    s.session = controller.getSession();
    s.project = daw.getProject();
    s.devices = devices;

    // A mix exists as soon as the inputs are assigned, whether or not a device is open. The
    // old gate here was `isPrepared() && hasKeptMix()`, which is why opening a session with
    // the interface unplugged and quitting was enough to erase the mix from the file.
    s.hasMix = controller.isBuilt() && controller.getGraph().numStrips() > 0;
    if (s.hasMix) s.mix = controller.getKept();
    s.macros = controller.getMacros();
    s.tuneCount = controller.getTuneCount();

    s.outputs = controller.getOutputFeeds();
    s.safety = controller.getLiveSafePolicy();
    s.reference = controller.getReference();          // what the mix is aimed at, already measured
    s.scenes = controller.getScenes();
    s.history = controller.getAllStripHistory();
    s.checkpoints = controller.getCheckpoints();
    s.trackPanelWidth = trackPanelWidth;

    // The TUNE LIVE MIX record, when there is one to keep. It is read-only: the mix itself is
    // in `mix`, so reopening the session sounds as it did without contacting any provider.
    if (controller.getTuneLive().getState() == TuneLiveCoordinator::State::Ready)
    {
        auto record = juce::JSON::parse (juce::String (controller.getTuneLive().toJson().write()));
        if (auto* o = record.getDynamicObject())
        {
            juce::Array<juce::var> review;
            for (const auto& line : controller.getTuneLive().getReviewLines()) review.add (juce::String (line));
            o->setProperty ("review", review);
        }
        s.tuneLive = record;
    }
    return s;
}

void applySession (const SessionState& s, MixController& controller, DawEngine& daw)
{
    // A different document: nothing carries across from the one that was open.
    controller.resetDocument();

    // The assignments first, because everything below is indexed by them. setSession() calls
    // rebuild(), which builds the routing graph and the mix's baselines - no device needed.
    controller.setSession (s.session);
    daw.setSession (s.session);
    daw.setProject (s.project);      // also sets LIVE SAFE's on/off, which lives on the project

    // LIVE SAFE off while the document is being put back, and armed at the end. It bounds what
    // a person may move in the middle of a service - a fader step, how far a macro may lean -
    // and restoring a session is not a person moving anything. Arming it first meant a session
    // saved under LIVE SAFE came back with its macros pulled to the edge of the fence and its
    // output feeds refused, which is a lock rewriting the document it is supposed to protect.
    controller.setLiveSafePolicy (LiveSafePolicy::off());

    controller.setOutputFeeds (s.outputs);        // routing belongs to the device, not the mix
    controller.setReference (s.reference);        // never a previous session's: resetDocument cleared it
    controller.restoreScenes (s.scenes);          // recall checks the inputs by name

    if (s.hasMix)
    {
        // The mix follows its input: a channel that moved, was dropped or is new to the
        // session leaves every other channel's chain, gain, fader and sends where they were.
        // The baselines rebuild() just built are what a genuinely new input starts from.
        controller.carryKept (s.mix, s.session, s.tuneCount);
        controller.carryStripHistory (s.history, s.session);
        // The mix history comes back whole. A checkpoint made on a different set of inputs is
        // not remapped - it is refused when somebody tries to go back to it, with a sentence -
        // because a mix is a balance between the sources that were there, not a row of numbers.
        controller.restoreCheckpoints (s.checkpoints);
        for (int i = 0; i < int (MixMacro::Count); ++i)
            controller.setMacro (MixMacro (i), s.macros.get (MixMacro (i)));
    }

    // `project.liveSafe` is the one authority for whether it is on; the document carries the
    // limits, which used to go back to their defaults on every launch because nothing stored them.
    auto policy = s.safety;
    policy.on = s.project.liveSafe;
    controller.setLiveSafePolicy (policy);
}

// ---------------------------------------------------------------------------
// Which drum sound each strip plays, by name rather than by slot.
// ---------------------------------------------------------------------------

namespace
{
    // Every strip whose role has a Sample stage, with the family its sounds are filed under.
    template <typename Fn>
    void forEachSampledStrip (const MixController& controller, Fn&& fn)
    {
        const auto& graph = controller.getGraph();
        for (int i = 0; i < graph.numStrips() && i < kMaxStrips; ++i)
        {
            const auto family = roleFamily (graph.strips[size_t (i)].role);
            if (! sampleReplacementAppropriate (family)) continue;
            fn (i, family);
        }
    }
}

void readSampleChoices (const MixController& controller, const SampleLibrary& library,
                        std::array<SampleChoice, kMaxStrips>& out)
{
    out = {};
    const auto& mix = controller.getKept();
    forEachSampledStrip (controller, [&] (int strip, RoleFamily family)
    {
        const int slot = mix.strips[size_t (strip)].channel.replaceSound;
        const auto& sounds = library.sounds (family);
        if (slot < 0 || slot >= int (sounds.size())) return;   // an index that no longer resolves names nothing
        out[size_t (strip)] = { sampleFamilyFolder (family), sounds[size_t (slot)].name,
                                sounds[size_t (slot)].user, sounds[size_t (slot)].path };
    });
}

std::vector<std::string> resolveSampleChoices (const std::array<SampleChoice, kMaxStrips>& choices,
                                               const SampleLibrary& library, MixController& controller)
{
    std::vector<std::string> missing;
    forEachSampledStrip (controller, [&] (int strip, RoleFamily family)
    {
        const auto& want = choices[size_t (strip)];
        if (! want.set()) return;                     // nothing was stored: leave the index alone
        const int slot = library.slotFor (family, want.name, want.user, want.path);
        auto params = controller.getKept().strips[size_t (strip)].channel;
        if (slot >= 0)
        {
            if (params.replaceSound == slot) return;
            params.replaceSound = slot;
            controller.setStripChannel (strip, params);
            return;
        }
        // Gone. Saying so and switching the stage off is the only honest answer: leaving the
        // index where it is would play whatever sound has moved into that slot instead, which
        // is how a session comes back with the wrong drum and nothing to explain it.
        if (! params.replaceEnabled) return;
        params.replaceEnabled = false;
        controller.setStripChannel (strip, params);
        missing.push_back ("The " + std::string (sampleFamilyFolder (family)) + " sound \""
                           + want.name + "\" is not in your samples any more, so "
                           + controller.getGraph().strips[size_t (strip)].name
                           + " is back on its own microphone.");
    });
    return missing;
}

} // namespace livemix
