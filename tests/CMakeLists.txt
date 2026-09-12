# Unit + integration tests (engine only, no JUCE) and the performance benchmark.
add_executable(livemix_tests
    TestMain.cpp
    AllocationTracker.cpp
    DSP/BiquadTests.cpp
    DSP/FilterTests.cpp
    DSP/ParametricEQTests.cpp
    DSP/GateTests.cpp
    DSP/CompressorTests.cpp
    DSP/TransientTests.cpp
    DSP/MeterTests.cpp
    DSP/ChannelProcessorTests.cpp
    DSP/FastMathTests.cpp
    Analysis/AnalysisEngineTests.cpp
    Analysis/RecommendationTests.cpp
    Integration/ProfileAndMacroTests.cpp
    Integration/ParameterVisitorTests.cpp
    Integration/InstanceRegistryTests.cpp
    Integration/ParameterSpecTests.cpp
    Intelligence/SafetyValidatorTests.cpp
    Intelligence/AnalyzeCoordinatorTests.cpp
    DSP/EqResponseTests.cpp
    DSP/LoudnessMatchTests.cpp
    Analysis/KitAnalysisTests.cpp
    Analysis/MixGainTests.cpp
    Regression/RegressionRenderTests.cpp
    Tune/TuneEngineTests.cpp
    DSP/DetectorFilterTests.cpp
    Analysis/TuneCaptureTests.cpp
    FX/FXFoundationTests.cpp
    FX/ReverbTests.cpp
    FX/DelayTests.cpp
    FX/FxChainTests.cpp
    FX/FxParameterTests.cpp
    DSP/NewStageTests.cpp
    Tune/ProductTuneTests.cpp
    Analysis/VoiceAnalysisTests.cpp
    Mix/RoutingGraphTests.cpp
    Mix/MixEngineTests.cpp
    Mix/MixCaptureTests.cpp
    Mix/MixPlannerTests.cpp
    Mix/MixMacrosTests.cpp
    Mix/MixEngineMatrixTests.cpp
    MixAI/TuneLiveTests.cpp
)
target_link_libraries(livemix_tests PRIVATE livemix_engine livemix_warnings)
target_compile_definitions(livemix_tests PRIVATE LIVEMIX_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
target_include_directories(livemix_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME livemix_tests COMMAND livemix_tests)

add_executable(livemix_benchmark Performance/Benchmark.cpp)
target_link_libraries(livemix_benchmark PRIVATE livemix_engine)
target_include_directories(livemix_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
