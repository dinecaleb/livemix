# Dine FX: AU + Standalone. Reverb and delay for aux/send use, composed from Dine Core (src/FX).
juce_add_plugin(LiveMixFX
    PRODUCT_NAME "Dine FX"
    COMPANY_NAME "Dine"
    BUNDLE_ID "com.dine.fx"
    PLUGIN_MANUFACTURER_CODE Lvmx
    PLUGIN_CODE Lmfx
    FORMATS AU Standalone
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${LIVEMIX_COPY_PLUGIN_AFTER_BUILD}
    HARDENED_RUNTIME_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Dine FX Standalone needs microphone/interface input."
    VERSION ${PROJECT_VERSION})

target_sources(LiveMixFX PRIVATE
    FxProcessor.cpp
    FxEditor.cpp
    FxPanels.cpp
    FxPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/State/ParameterLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/State/FxParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/LiveMixLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/MeterComponent.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/Widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/ShellBars.cpp)

target_include_directories(LiveMixFX PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(LiveMixFX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(LiveMixFX
    PRIVATE
        livemix_engine
        LiveMixFonts
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (LIVEMIX_BUILD_TESTS)
    add_executable(livemix_fx_plugin_tests
        ${CMAKE_SOURCE_DIR}/tests/Integration/FxPluginTests.cpp
        ${CMAKE_SOURCE_DIR}/tests/AllocationTracker.cpp)
    target_link_libraries(livemix_fx_plugin_tests PRIVATE
        LiveMixFX livemix_engine
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
    target_compile_definitions(livemix_fx_plugin_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_include_directories(livemix_fx_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    add_test(NAME livemix_fx_plugin_tests COMMAND livemix_fx_plugin_tests)

    add_executable(livemix_fx_ui_snapshots ${CMAKE_SOURCE_DIR}/tools/FxUISnapshots.cpp)
    target_link_libraries(livemix_fx_ui_snapshots PRIVATE
        LiveMixFX livemix_engine
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
    target_compile_definitions(livemix_fx_ui_snapshots PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_include_directories(livemix_fx_ui_snapshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
endif()
