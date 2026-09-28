# DLIVE: the standalone mixing application and its offline tools. Everything here is a
# host around the engine (src/, no JUCE); the engine never depends on this directory.

# dlive_mix_stems: the vertical-slice success test. Loads a folder of recorded stems,
# assigns them by file name, builds the automatic routing, listens (Tune Mix) and renders
# RAW / BEFORE / AFTER stereo mixes so the result can be heard. See app/Tools/MixStems.cpp.
add_executable(dlive_mix_stems Tools/MixStems.cpp native/ReferenceAudio.cpp)
target_link_libraries(dlive_mix_stems PRIVATE
    livemix_engine
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_recommended_config_flags)
target_compile_definitions(dlive_mix_stems PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_include_directories(dlive_mix_stems PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# dlive_trigger_check: sample replacement's Phase 0 - the trigger over real drum takes, fitted
# as TUNE fits it, reporting hits, levels, gaps and coincidences. See app/Tools/TriggerCheck.cpp.
add_executable(dlive_trigger_check Tools/TriggerCheck.cpp)
target_link_libraries(dlive_trigger_check PRIVATE livemix_engine juce::juce_audio_formats juce::juce_audio_basics juce::juce_core juce::juce_recommended_config_flags)
target_compile_definitions(dlive_trigger_check PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_include_directories(dlive_trigger_check PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# DLIVE standalone application (JUCE GUI app) and its headless UI snapshot tool.
# The UI code is shared by both targets; only the entry point and the device host differ.
# ---------------------------------------------------------------------------
# The application layer with no device in it: the mix controller, the DAW (transport,
# recorder, timeline player), the session document, import and offline bounce.
set(DLIVE_APP_SOURCES
    native/MixController.cpp
    native/OpenAiMixProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/Intelligence/AISettings.cpp
    ${CMAKE_SOURCE_DIR}/src/Intelligence/HttpJson.cpp
    native/SessionAutosave.cpp
    native/SessionState.cpp
    native/SessionStore.cpp
    native/InputMapStore.cpp
    native/ThemeStore.cpp
    native/MonitorDevicePick.cpp
    native/MixBounce.cpp
    native/ClipSource.cpp
    native/DawEngine.cpp
    native/Recorder.cpp
    native/TimelinePlayer.cpp
    native/MultitrackImport.cpp
    native/ReferenceAudio.cpp
    native/SampleLibrary.cpp)

set(DLIVE_UI_SOURCES
    ${DLIVE_APP_SOURCES}
    ui/AppTheme.cpp
    ui/SetupPages.cpp
    ui/TracksPage.cpp
    ui/MixPage.cpp
    ui/MacroPad.cpp
    ui/MixerPage.cpp
    ui/OutputsSheet.cpp
    ui/RoutingPage.cpp
    ui/CheckSheet.cpp
    ui/HistorySheet.cpp
    ui/ChannelTuneSheet.cpp
    ui/ReferenceSheet.cpp
    ui/ChatSheet.cpp
    ui/ThemeSheet.cpp
    ui/LivePage.cpp
    ui/AdvancedPage.cpp
    ui/ChainEditor.cpp
    ui/ChainStrip.cpp
    ui/TransportBar.cpp
    ui/Tutorial.cpp
    ui/MainView.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/LiveMixLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/Widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/MeterComponent.cpp)

set(DLIVE_DEFINES
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

set(DLIVE_LIBS
    livemix_engine LiveMixFonts
    juce::juce_audio_devices juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats
    juce::juce_gui_basics juce::juce_gui_extra)

juce_add_gui_app(DLive
    PRODUCT_NAME "DLIVE"
    COMPANY_NAME "Dine"
    BUNDLE_ID "com.dine.dlive"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.png"
    HARDENED_RUNTIME_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DLIVE mixes the inputs of your audio device.")
target_sources(DLive PRIVATE Main.cpp native/AudioHost.cpp native/MonitorDevice.cpp native/MicPermission.mm ${DLIVE_UI_SOURCES})
target_include_directories(DLive PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(DLive PRIVATE ${DLIVE_DEFINES}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DLive,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DLive,JUCE_VERSION>")
if (APPLE)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
endif()
target_link_libraries(DLive PRIVATE ${DLIVE_LIBS} ${AVFOUNDATION_FRAMEWORK}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
# The built-in drum samples (app/Samples, with their licence) ride in the bundle; a build that
# is run from the tree, and every tool, reads the source folder instead.
add_custom_command(TARGET DLive POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Samples" "$<TARGET_BUNDLE_CONTENT_DIR:DLive>/Resources/Samples"
    COMMENT "Copying the built-in drum samples into the DLIVE bundle")
target_compile_definitions(DLive PRIVATE DLIVE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")

if (LIVEMIX_BUILD_TESTS)
    add_executable(dlive_ui_snapshots Tools/AppSnapshots.cpp ${DLIVE_UI_SOURCES})
    target_include_directories(dlive_ui_snapshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(dlive_ui_snapshots PRIVATE ${DLIVE_DEFINES} JUCE_STANDALONE_APPLICATION=1 DLIVE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")
    target_link_libraries(dlive_ui_snapshots PRIVATE ${DLIVE_LIBS} juce::juce_recommended_config_flags)
endif()

if (LIVEMIX_BUILD_TESTS)
    # Application-layer tests: controller state machine and session document (no device, no UI).
    add_executable(dlive_app_tests
        Tests/AppTests.cpp
        Tests/DawTests.cpp
        Tests/ThemeTests.cpp
        ${DLIVE_APP_SOURCES}
        ${CMAKE_SOURCE_DIR}/tests/TestMain.cpp)
    target_include_directories(dlive_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(dlive_app_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0 DLIVE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")
    target_link_libraries(dlive_app_tests PRIVATE livemix_engine juce::juce_core juce::juce_audio_formats
        juce::juce_audio_basics juce::juce_events juce::juce_recommended_config_flags)
    add_test(NAME dlive_app_tests COMMAND dlive_app_tests)
endif()

if (LIVEMIX_BUILD_TESTS)
    # dlive_ui_tests: the window itself. Separate from dlive_app_tests because it links the
    # whole UI and every JUCE GUI module with it, and dlive_app_tests is the fast one that runs
    # on every engine change. ctest runs both, so the reachability guard is in CI either way.
    add_executable(dlive_ui_tests
        Tests/ReachabilityTests.cpp
        Tests/TextCacheTests.cpp
        ${DLIVE_UI_SOURCES}
        ${CMAKE_SOURCE_DIR}/tests/TestMain.cpp)
    target_include_directories(dlive_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(dlive_ui_tests PRIVATE ${DLIVE_DEFINES} JUCE_STANDALONE_APPLICATION=1
        DLIVE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")
    target_link_libraries(dlive_ui_tests PRIVATE ${DLIVE_LIBS} juce::juce_recommended_config_flags)
    add_test(NAME dlive_ui_tests COMMAND dlive_ui_tests)
endif()

# Real-device soak check (opens an audio device through AudioHost, runs the engine, reports).
add_executable(dlive_device_check Tools/DeviceCheck.cpp native/AudioHost.cpp native/MonitorDevice.cpp native/MicPermission.mm ${DLIVE_APP_SOURCES})
target_include_directories(dlive_device_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(dlive_device_check PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(dlive_device_check PRIVATE livemix_engine juce::juce_audio_devices juce::juce_audio_formats juce::juce_events juce::juce_recommended_config_flags ${AVFOUNDATION_FRAMEWORK})
target_compile_definitions(dlive_device_check PRIVATE JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
