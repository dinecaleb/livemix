cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(LIVEMIX_UNIVERSAL_BINARY "Build arm64 + x86_64 universal binaries" OFF)
if (LIVEMIX_UNIVERSAL_BINARY)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

project(LiveMix VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LIVEMIX_BUILD_PLUGIN "Build the Dine Drums / Vocals / Keys / Master / Guitar / Bass / FX AU+Standalone plugins (requires JUCE)" ON)
option(LIVEMIX_BUILD_TESTS  "Build unit, integration and performance tests" ON)
option(LIVEMIX_COPY_PLUGIN_AFTER_BUILD "Copy the AU into ~/Library/Audio/Plug-Ins after building" ON)

# Warnings for our own code only (JUCE is added with its own flags).
add_library(livemix_warnings INTERFACE)
target_compile_options(livemix_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# Engine: pure C++20, no JUCE dependency. DSP, analysis, recommendations,
# profiles, inter-instance registry. Everything that must be unit-testable
# and host/framework agnostic lives here.
# ---------------------------------------------------------------------------
add_subdirectory(src)

if (LIVEMIX_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Plugin (JUCE)
# ---------------------------------------------------------------------------
if (LIVEMIX_BUILD_PLUGIN)
    set(LIVEMIX_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
    if (EXISTS "${LIVEMIX_JUCE_DIR}/CMakeLists.txt")
        add_subdirectory("${LIVEMIX_JUCE_DIR}" EXCLUDE_FROM_ALL)
    else()
        message(STATUS "external/JUCE not found; fetching JUCE 8.0.8 (run scripts/bootstrap.sh to vendor it)")
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.8
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
    # Embedded UI fonts shared by every Dine product (Barlow, Barlow Condensed, IBM Plex Mono - SIL OFL, see assets/fonts).
    juce_add_binary_data(LiveMixFonts
        NAMESPACE LiveMixFonts
        SOURCES
            ${CMAKE_SOURCE_DIR}/assets/fonts/Barlow-Regular.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/Barlow-Medium.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/Barlow-SemiBold.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/BarlowCondensed-Medium.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/BarlowCondensed-SemiBold.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/BarlowCondensed-Bold.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Regular.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Medium.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-SemiBold.ttf)
    include(modules/Common/DineChannelProduct.cmake)
    add_subdirectory(modules/Drums)
    add_subdirectory(modules/Vocals)
    add_subdirectory(modules/Keys)
    add_subdirectory(modules/Master)
    add_subdirectory(modules/Guitar)
    add_subdirectory(modules/Bass)
    add_subdirectory(modules/FX)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if (LIVEMIX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
