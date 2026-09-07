# Dine Drums: AU + Standalone (VST3/AAX can be added to FORMATS later). Target/library names stay LiveMix*.
dine_add_channel_product(LiveMixDrums
    PRODUCT Drums
    NAME "Dine Drums"
    BUNDLE_ID "com.dine.drums"
    PLUGIN_CODE Lmdr
    SOURCES DrumsProcessor.cpp)

# Drums-specific headless tests (kit intelligence, AI provider parsing) and the original snapshot walk.
if (LIVEMIX_BUILD_TESTS)
    add_executable(livemix_plugin_tests
        ${CMAKE_SOURCE_DIR}/tests/Integration/PluginStateTests.cpp
        ${CMAKE_SOURCE_DIR}/tests/AllocationTracker.cpp)
    target_link_libraries(livemix_plugin_tests PRIVATE
        LiveMixDrums livemix_engine
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
    target_compile_definitions(livemix_plugin_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_include_directories(livemix_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/modules/Common ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    add_test(NAME livemix_plugin_tests COMMAND livemix_plugin_tests)

    add_executable(livemix_ui_snapshots ${CMAKE_SOURCE_DIR}/tools/UISnapshots.cpp)
    target_link_libraries(livemix_ui_snapshots PRIVATE
        LiveMixDrums livemix_engine
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
    target_compile_definitions(livemix_ui_snapshots PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_include_directories(livemix_ui_snapshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/modules/Common ${CMAKE_SOURCE_DIR}/src)

    # Offline check of Tune against recorded stems (tools/TuneStems.cpp): livemix_tune_stems <role> <file.aif|wav> [seconds]
    add_executable(livemix_tune_stems ${CMAKE_SOURCE_DIR}/tools/TuneStems.cpp)
    target_link_libraries(livemix_tune_stems PRIVATE livemix_engine juce::juce_audio_formats juce::juce_core juce::juce_recommended_config_flags)
    target_compile_definitions(livemix_tune_stems PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_include_directories(livemix_tune_stems PRIVATE ${CMAKE_SOURCE_DIR}/src)
endif()
