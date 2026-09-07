#pragma once
#include <cstdint>
#include <string>
#include "Analysis/KitAnalysis.h"
#include "Recommendations/Recommendation.h"

namespace livemix
{

// What a registered instance exposes to the rest of the kit. Every call is
// made on the message thread by whichever instance is acting as the Kit
// Controller; nothing here is ever invoked from the audio thread.
class IKitEndpoint
{
public:
    virtual ~IKitEndpoint() = default;
    virtual void kitStartAnalyze() = 0;                 // begins this instance's capture (honours Live Safe)
    virtual bool kitIsAnalyzing() const = 0;            // capture or interpretation still running
    virtual KitMember kitGetMember() const = 0;         // latest analysis + recommendations
    virtual void kitApplySafeChanges() = 0;
    virtual void kitApplyOutputTrimDelta (float deltaDb) = 0;
};

} // namespace livemix
