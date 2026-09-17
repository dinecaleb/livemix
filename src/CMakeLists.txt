# livemix_engine: framework-agnostic core. No JUCE, no allocation in process().
add_library(livemix_engine STATIC
    Core/FFT.cpp
    Core/Json.cpp
    DSP/Biquad.cpp
    DSP/FilterProcessor.cpp
    DSP/ParametricEQ.cpp
    DSP/GateExpander.cpp
    DSP/Compressor.cpp
    DSP/TransientProcessor.cpp
    DSP/Saturator.cpp
    DSP/LevelMeter.cpp
    DSP/DeEsser.cpp
    DSP/StereoWidth.cpp
    DSP/Limiter.cpp
    DSP/LoudnessMeter.cpp
    DSP/ChannelProcessor.cpp
    DSP/EqResponse.cpp
    Analysis/AnalysisAccumulator.cpp
    Analysis/AnalysisEngine.cpp
    Analysis/KitAnalysis.cpp
    Recommendations/RecommendationEngine.cpp
    Tune/StrategyToolkit.cpp
    Tune/DrumStrategies.cpp
    Tune/VocalStrategies.cpp
    Tune/KeysStrategies.cpp
    Tune/GuitarStrategies.cpp
    Tune/BassStrategies.cpp
    Tune/MasterStrategy.cpp
    Tune/AmbienceStrategies.cpp
    Tune/TuneEngine.cpp
    Profiles/ProfileData.cpp
    Profiles/ProductData.cpp
    Profiles/MacroMapping.cpp
    Profiles/MixProfileData.cpp
    Mix/RoutingGraph.cpp
    Mix/MixEngine.cpp
    Mix/MixCapture.cpp
    Mix/OfflineCapture.cpp
    Mix/MixPlanner.cpp
    Mix/ReferenceMix.cpp
    Mix/MixMacros.cpp
    MixAI/RelationshipEngine.cpp
    MixAI/MixContext.cpp
    MixAI/DspCapabilityRegistry.cpp
    MixAI/MixIntent.cpp
    MixAI/ProcessingPlan.cpp
    MixAI/CapabilityResolver.cpp
    MixAI/MixSafetyValidator.cpp
    MixAI/MixRequestParser.cpp
    MixAI/MixReasoningProvider.cpp
    MixAI/TuneLiveCoordinator.cpp
    Communication/InstanceRegistry.cpp
    Communication/KitController.cpp
    Intelligence/SafetyValidator.cpp
    Intelligence/AnalyzeCoordinator.cpp
    State/ParameterSpecs.cpp
    FX/FxParameterSpecs.cpp
    FX/ReverbAlgorithm.cpp
    FX/DelayAlgorithm.cpp
    FX/FxChain.cpp
    FX/FxMacroMapping.cpp
    FX/FxProfiles.cpp
)
target_include_directories(livemix_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(livemix_engine PRIVATE livemix_warnings)
target_compile_features(livemix_engine PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(livemix_engine PUBLIC Threads::Threads)
