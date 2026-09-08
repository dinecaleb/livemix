# DINELIVE: the standalone mixing application and its offline tools. Everything here is a
# host around the engine (src/, no JUCE); the engine never depends on this directory.

# dinelive_mix_stems: the vertical-slice success test. Loads a folder of recorded stems,
# assigns them by file name, builds the automatic routing, listens (Tune Mix) and renders
# RAW / BEFORE / AFTER stereo mixes so the result can be heard. See app/Tools/MixStems.cpp.
add_executable(dinelive_mix_stems Tools/MixStems.cpp)
target_link_libraries(dinelive_mix_stems PRIVATE
    livemix_engine
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_recommended_config_flags)
target_compile_definitions(dinelive_mix_stems PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_include_directories(dinelive_mix_stems PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# DINELIVE standalone application (JUCE GUI app) and its headless UI snapshot tool.
# The UI code is shared by both targets; only the entry point and the device host differ.
# ---------------------------------------------------------------------------
set(DINELIVE_UI_SOURCES
    native/MixController.cpp
    native/SessionStore.cpp
    native/MixBounce.cpp
    ui/AppTheme.cpp
    ui/SetupPages.cpp
    ui/MixPage.cpp
    ui/MixerPage.cpp
    ui/AdvancedPage.cpp
    ui/MainView.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/LiveMixLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/Widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/MeterComponent.cpp)

set(DINELIVE_DEFINES
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

set(DINELIVE_LIBS
    livemix_engine LiveMixFonts
    juce::juce_audio_devices juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats
    juce::juce_gui_basics juce::juce_gui_extra)

juce_add_gui_app(DineLive
    PRODUCT_NAME "DINELIVE"
    COMPANY_NAME "Dine"
    BUNDLE_ID "com.dine.live"
    VERSION ${PROJECT_VERSION}
    HARDENED_RUNTIME_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DINELIVE mixes the inputs of your audio device.")
target_sources(DineLive PRIVATE Main.cpp native/AudioHost.cpp native/MultitrackSource.cpp ${DINELIVE_UI_SOURCES})
target_include_directories(DineLive PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(DineLive PRIVATE ${DINELIVE_DEFINES}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DineLive,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DineLive,JUCE_VERSION>")
target_link_libraries(DineLive PRIVATE ${DINELIVE_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if (LIVEMIX_BUILD_TESTS)
    add_executable(dinelive_ui_snapshots Tools/AppSnapshots.cpp ${DINELIVE_UI_SOURCES})
    target_include_directories(dinelive_ui_snapshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(dinelive_ui_snapshots PRIVATE ${DINELIVE_DEFINES} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(dinelive_ui_snapshots PRIVATE ${DINELIVE_LIBS} juce::juce_recommended_config_flags)
endif()

if (LIVEMIX_BUILD_TESTS)
    # Application-layer tests: controller state machine and session document (no device, no UI).
    add_executable(dinelive_app_tests
        Tests/AppTests.cpp
        native/MixController.cpp
        native/SessionStore.cpp
        ${CMAKE_SOURCE_DIR}/tests/TestMain.cpp)
    target_include_directories(dinelive_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(dinelive_app_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
    target_link_libraries(dinelive_app_tests PRIVATE livemix_engine juce::juce_core juce::juce_recommended_config_flags)
    add_test(NAME dinelive_app_tests COMMAND dinelive_app_tests)
endif()

# Real-device soak check (opens an audio device through AudioHost, runs the engine, reports).
add_executable(dinelive_device_check Tools/DeviceCheck.cpp native/MixController.cpp native/AudioHost.cpp native/MultitrackSource.cpp)
target_include_directories(dinelive_device_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(dinelive_device_check PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(dinelive_device_check PRIVATE livemix_engine juce::juce_audio_devices juce::juce_audio_formats juce::juce_events juce::juce_recommended_config_flags)
