# DINE: the standalone mixing application and its offline tools. Everything here is a
# host around the engine (src/, no JUCE); the engine never depends on this directory.

# dine_mix_stems: the vertical-slice success test. Loads a folder of recorded stems,
# assigns them by file name, builds the automatic routing, listens (Tune Mix) and renders
# RAW / BEFORE / AFTER stereo mixes so the result can be heard. See app/Tools/MixStems.cpp.
add_executable(dine_mix_stems Tools/MixStems.cpp native/ReferenceAudio.cpp)
target_link_libraries(dine_mix_stems PRIVATE
    livemix_engine
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_recommended_config_flags)
target_compile_definitions(dine_mix_stems PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_include_directories(dine_mix_stems PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# dine_trigger_check: sample replacement's Phase 0 - the trigger over real drum takes, fitted
# as TUNE fits it, reporting hits, levels, gaps and coincidences. See app/Tools/TriggerCheck.cpp.
add_executable(dine_trigger_check Tools/TriggerCheck.cpp)
target_link_libraries(dine_trigger_check PRIVATE livemix_engine juce::juce_audio_formats juce::juce_audio_basics juce::juce_core juce::juce_recommended_config_flags)
target_compile_definitions(dine_trigger_check PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_include_directories(dine_trigger_check PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# DINE standalone application (JUCE GUI app) and its headless UI snapshot tool.
# The UI code is shared by both targets; only the entry point and the device host differ.
# ---------------------------------------------------------------------------
# The application layer with no device in it: the mix controller, the DAW (transport,
# recorder, timeline player), the session document, import and offline bounce.
set(DINE_APP_SOURCES
    native/MixController.cpp
    native/OpenAiMixProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/Intelligence/AISettings.cpp
    ${CMAKE_SOURCE_DIR}/src/Intelligence/HttpJson.cpp
    native/SessionAutosave.cpp
    native/SessionState.cpp
    native/BroadcastReadiness.cpp
    native/Telemetry.cpp
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
    native/SampleLibrary.cpp
    native/DrumKits.cpp)

# dine_mix_compare: a session finished by hand against a fresh TUNE MIX of the same audio -
# where every channel and group lands against the lead in each, and the masters' tone.
# See app/Tools/MixCompare.cpp.
add_executable(dine_mix_compare Tools/MixCompare.cpp ${DINE_APP_SOURCES})
target_include_directories(dine_mix_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(dine_mix_compare PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0 DINE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")
target_link_libraries(dine_mix_compare PRIVATE livemix_engine juce::juce_core juce::juce_audio_formats
    juce::juce_audio_basics juce::juce_events juce::juce_recommended_config_flags)

set(DINE_UI_SOURCES
    ${DINE_APP_SOURCES}
    ui/AppTheme.cpp
    ui/SetupPages.cpp
    ui/FavouritesPage.cpp
    ui/TracksPage.cpp
    ui/MixPage.cpp
    ui/MacroPad.cpp
    ui/MixerPage.cpp
    ui/OutputsSheet.cpp
    ui/ExportSheet.cpp
    ui/ChoiceSheet.cpp
    ui/EffectSheet.cpp
    ui/AboutSheet.cpp
    ui/RoutingPage.cpp
    ui/CheckSheet.cpp
    ui/SetlistSheet.cpp
    ui/ReadySheet.cpp
    ui/HistorySheet.cpp
    ui/BroadcastReadinessSheet.cpp
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
    ui/WorkspaceGuide.cpp
    ui/MainView.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/LiveMixLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/Widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/MeterComponent.cpp)

set(DINE_DEFINES
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

set(DINE_LIBS
    livemix_engine LiveMixFonts
    juce::juce_audio_devices juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats
    juce::juce_gui_basics juce::juce_gui_extra)

juce_add_gui_app(DineApp
    PRODUCT_NAME "DINE"
    COMPANY_NAME "Dine"
    BUNDLE_ID "com.dine.app"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.png"
    HARDENED_RUNTIME_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DINE mixes the inputs of your audio device.")
target_sources(DineApp PRIVATE Main.cpp native/AudioHost.cpp native/MonitorDevice.cpp native/MicPermission.mm native/WindowChrome.mm ${DINE_UI_SOURCES})
target_include_directories(DineApp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
# The Supabase project the usage events go to (docs/ANALYTICS.md). Empty: nothing is sent.
# The anon key is public by design - it can call public.ingest_events and nothing else - but
# it is kept out of the repository: a git-ignored telemetry.local.cmake at the top of the tree
# sets both, so every build on a Mac that has one (build.sh, dine.sh, package.sh) reports.
include ("${CMAKE_SOURCE_DIR}/telemetry.local.cmake" OPTIONAL)
set(DINE_SUPABASE_URL "" CACHE STRING "Supabase project URL for DINE usage events (empty = off)")
set(DINE_SUPABASE_ANON_KEY "" CACHE STRING "Supabase anon key for DINE usage events")
# An exception on the message thread is caught by the loop, reported (app/unhandled_exception)
# and the app carries on - the audio thread never sees it. Without this JUCE lets it terminate
# the process, which in the middle of a service is the one outcome worse than carrying on.
target_compile_definitions(DineApp PRIVATE ${DINE_DEFINES} JUCE_CATCH_UNHANDLED_EXCEPTIONS=1
    DINE_SUPABASE_URL="${DINE_SUPABASE_URL}"
    DINE_SUPABASE_ANON_KEY="${DINE_SUPABASE_ANON_KEY}"
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DineApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DineApp,JUCE_VERSION>")
if (APPLE)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
endif()
target_link_libraries(DineApp PRIVATE ${DINE_LIBS} ${AVFOUNDATION_FRAMEWORK}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
# The built-in drum samples (app/Samples, with their licence) ride in the bundle; every tool
# and test, which run from the tree, reads the source folder instead.
add_custom_command(TARGET DineApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Samples" "$<TARGET_BUNDLE_CONTENT_DIR:DineApp>/Resources/Samples"
    COMMENT "Copying the built-in drum samples into the DINE bundle")
# THE APP ICON is the brand's own iconset (docs/design/brand/app-icon/DINE.iconset), whose
# small sizes are drawn for their size rather than scaled down from the 1024 - JUCE's generated
# icon would resample ICON_BIG. It goes in before the signing step below, so it is signed with it.
add_custom_command(TARGET DineApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/DINE.icns" "$<TARGET_BUNDLE_CONTENT_DIR:DineApp>/Resources/Icon.icns"
    COMMENT "Putting the DINE iconset into the bundle")
# The app reads its own bundle's copy and nothing else: the source folder's path is not
# compiled into what ships (the tools and the tests, which run from the tree, keep it).

# A STABLE IDENTITY FOR THE MICROPHONE GRANT (2026-10-05). Ninja ignores JUCE's Xcode-only
# signing attributes, so a plain build carries the linker's ad-hoc signature, whose designated
# requirement is the build's own cdhash: every rebuild is a new app to macOS, and the
# microphone question comes back (docs/SESSION-STATE.md section 4). Signed with a certificate,
# the requirement names the bundle id and the certificate instead, so a grant given once holds
# across rebuilds. scripts/dine.sh finds an "Apple Development" identity on its own; set it by
# hand with -DDINE_SIGN_IDENTITY=<name or SHA-1>. Empty = the linker's ad-hoc signature, as before.
# No hardened runtime here: that is the Developer ID path in scripts/package.sh.
set(DINE_SIGN_IDENTITY "" CACHE STRING "codesign identity for the DINE.app a build makes (empty = ad-hoc)")
if (APPLE AND DINE_SIGN_IDENTITY)
    add_custom_command(TARGET DineApp POST_BUILD
        COMMAND codesign --force --sign "${DINE_SIGN_IDENTITY}" --identifier com.dine.app "$<TARGET_BUNDLE_DIR:DineApp>"
        COMMENT "Signing DINE.app with a stable identity, so macOS keeps its microphone answer across builds"
        VERBATIM)
endif()

if (LIVEMIX_BUILD_TESTS)
    add_executable(dine_ui_snapshots Tools/AppSnapshots.cpp ${DINE_UI_SOURCES})
    target_include_directories(dine_ui_snapshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(dine_ui_snapshots PRIVATE ${DINE_DEFINES} JUCE_STANDALONE_APPLICATION=1 DINE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")
    target_link_libraries(dine_ui_snapshots PRIVATE ${DINE_LIBS} juce::juce_recommended_config_flags)
endif()

if (LIVEMIX_BUILD_TESTS)
    # Application-layer tests: controller state machine and session document (no device, no UI).
    add_executable(dine_app_tests
        Tests/AppTests.cpp
        Tests/DawTests.cpp
        Tests/RecordingTests.cpp
        Tests/ThemeTests.cpp
        Tests/TelemetryTests.cpp
        Tests/ReliabilityPassTests.cpp
        Tests/BroadcastReadinessTests.cpp
        ${DINE_APP_SOURCES}
        ${CMAKE_SOURCE_DIR}/tests/TestMain.cpp)
    target_include_directories(dine_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(dine_app_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0 DINE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")
    target_link_libraries(dine_app_tests PRIVATE livemix_engine juce::juce_core juce::juce_audio_formats
        juce::juce_audio_basics juce::juce_events juce::juce_recommended_config_flags)
    add_test(NAME dine_app_tests COMMAND dine_app_tests)
endif()

if (LIVEMIX_BUILD_TESTS)
    # dine_ui_tests: the window itself. Separate from dine_app_tests because it links the
    # whole UI and every JUCE GUI module with it, and dine_app_tests is the fast one that runs
    # on every engine change. ctest runs both, so the reachability guard is in CI either way.
    add_executable(dine_ui_tests
        Tests/ReachabilityTests.cpp
        Tests/TextCacheTests.cpp
        ${DINE_UI_SOURCES}
        ${CMAKE_SOURCE_DIR}/tests/TestMain.cpp)
    target_include_directories(dine_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(dine_ui_tests PRIVATE ${DINE_DEFINES} JUCE_STANDALONE_APPLICATION=1
        DINE_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples")
    target_link_libraries(dine_ui_tests PRIVATE ${DINE_LIBS} juce::juce_recommended_config_flags)
    add_test(NAME dine_ui_tests COMMAND dine_ui_tests)
endif()

# Real-device soak check (opens an audio device through AudioHost, runs the engine, reports).
add_executable(dine_device_check Tools/DeviceCheck.cpp native/AudioHost.cpp native/MonitorDevice.cpp native/MicPermission.mm ${DINE_APP_SOURCES})
target_include_directories(dine_device_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(dine_device_check PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(dine_device_check PRIVATE livemix_engine juce::juce_audio_devices juce::juce_audio_formats juce::juce_events juce::juce_recommended_config_flags ${AVFOUNDATION_FRAMEWORK})
target_compile_definitions(dine_device_check PRIVATE JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
