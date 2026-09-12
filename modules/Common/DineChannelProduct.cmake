# dine_add_channel_product(<Target> PRODUCT <Drums|Vocals|Keys|Master> NAME "<Product name>" BUNDLE_ID <id>
#                          PLUGIN_CODE <4cc> SOURCES <product-specific sources...>)
# One AU + Standalone plugin built from the shared channel-plugin base (modules/Common) and
# the shared Dine UI, plus a headless plugin test and a UI snapshot tool for that product.
function(dine_add_channel_product TARGET)
    cmake_parse_arguments(DINE "" "PRODUCT;NAME;BUNDLE_ID;PLUGIN_CODE" "SOURCES" ${ARGN})

    juce_add_plugin(${TARGET}
        PRODUCT_NAME "${DINE_NAME}"
        COMPANY_NAME "Dine"
        BUNDLE_ID "${DINE_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE Lvmx
        PLUGIN_CODE ${DINE_PLUGIN_CODE}
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
        MICROPHONE_PERMISSION_TEXT "${DINE_NAME} Standalone needs microphone/interface input to Tune."
        VERSION ${PROJECT_VERSION})

    target_sources(${TARGET} PRIVATE
        ${DINE_SOURCES}
        ${CMAKE_SOURCE_DIR}/modules/Common/ChannelPluginProcessor.cpp
        ${CMAKE_SOURCE_DIR}/modules/Common/ChannelPluginEditor.cpp
        ${CMAKE_SOURCE_DIR}/src/State/ParameterLayout.cpp
        ${CMAKE_SOURCE_DIR}/src/State/ParameterBridge.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/LiveMixLookAndFeel.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/MeterComponent.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/SimplePanel.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/AdvancedPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/Widgets.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/ShellBars.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/InputPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/ChainStrip.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/AnalyzeOverlay.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/EqCurveComponent.cpp
        ${CMAKE_SOURCE_DIR}/src/UI/KitPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/State/PresetManager.cpp
        ${CMAKE_SOURCE_DIR}/src/Intelligence/AISettings.cpp
        ${CMAKE_SOURCE_DIR}/src/Intelligence/OpenAIProvider.cpp
        ${CMAKE_SOURCE_DIR}/src/Intelligence/HttpJson.cpp)

    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/modules/Common ${CMAKE_SOURCE_DIR}/src)

    target_compile_definitions(${TARGET} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        LIVEMIX_PRODUCT=${DINE_PRODUCT})

    target_link_libraries(${TARGET}
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
        string(TOLOWER "${DINE_PRODUCT}" product_lower)
        # Headless plugin tests shared by every channel product (state, latency, allocations, Tune, presets).
        add_executable(livemix_${product_lower}_plugin_tests
            ${CMAKE_SOURCE_DIR}/tests/Integration/ChannelPluginTests.cpp
            ${CMAKE_SOURCE_DIR}/tests/AllocationTracker.cpp)
        target_link_libraries(livemix_${product_lower}_plugin_tests PRIVATE
            ${TARGET} livemix_engine
            juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
        target_compile_definitions(livemix_${product_lower}_plugin_tests PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
            JUCE_MODAL_LOOPS_PERMITTED=1 LIVEMIX_PRODUCT=${DINE_PRODUCT})
        target_include_directories(livemix_${product_lower}_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/modules/Common ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
        add_test(NAME livemix_${product_lower}_plugin_tests COMMAND livemix_${product_lower}_plugin_tests)

        # Headless UI snapshots: renders every editor state of this product to PNG (tools/ChannelUISnapshots.cpp).
        add_executable(livemix_${product_lower}_ui_snapshots ${CMAKE_SOURCE_DIR}/tools/ChannelUISnapshots.cpp)
        target_link_libraries(livemix_${product_lower}_ui_snapshots PRIVATE
            ${TARGET} livemix_engine
            juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
        target_compile_definitions(livemix_${product_lower}_ui_snapshots PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
            JUCE_MODAL_LOOPS_PERMITTED=1 LIVEMIX_PRODUCT=${DINE_PRODUCT})
        target_include_directories(livemix_${product_lower}_ui_snapshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/modules/Common ${CMAKE_SOURCE_DIR}/src)
    endif()
endfunction()
