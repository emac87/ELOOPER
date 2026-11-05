cmake_minimum_required(VERSION 3.22)
project(ELooper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

option(ELOOPER_BUILD_APP   "Build the JUCE application" ON)
option(ELOOPER_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# Real-time core: pure C++17, no JUCE. Unit-testable anywhere.
# ---------------------------------------------------------------------------
add_library(elooper_core STATIC
    Source/Core/Automation.cpp
    Source/Core/GraphicEq.cpp
    Source/Core/LoopCore.cpp
    Source/Core/MidiClip.cpp
    Source/Core/Routing.cpp
)
target_include_directories(elooper_core PUBLIC Source)
if(NOT MSVC)
    target_compile_options(elooper_core PRIVATE -Wall -Wextra)
endif()

if(ELOOPER_BUILD_TESTS)
    enable_testing()
    add_executable(elooper_core_tests Tests/TestMain.cpp Tests/CoreTests.cpp Tests/CoreExtraTests.cpp)
    target_link_libraries(elooper_core_tests PRIVATE elooper_core)
    add_test(NAME core COMMAND elooper_core_tests)
endif()

if(NOT ELOOPER_BUILD_APP)
    return()
endif()

include(cmake/Dependencies.cmake)

juce_add_gui_app(ELooper
    PRODUCT_NAME "ELooper"
    COMPANY_NAME "ELooper"
    BUNDLE_ID "com.elooper.app"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ELooper needs audio input access to record loops."
    PLUGINHOST_AU TRUE
)

file(GLOB ELOOPER_APP_SOURCES CONFIGURE_DEPENDS
    Source/Model/*.cpp Source/Engine/*.cpp Source/UI/*.cpp Source/Main.cpp)
target_sources(ELooper PRIVATE ${ELOOPER_APP_SOURCES})

target_compile_definitions(ELooper PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ELooper,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ELooper,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)
if(APPLE)
    target_compile_definitions(ELooper PRIVATE JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(ELooper PRIVATE
    elooper_core
    Ableton::Link
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Headless engine integration test (renders the real engine offline, no device).
if(ELOOPER_BUILD_TESTS)
    juce_add_console_app(elooper_engine_tests PRODUCT_NAME "elooper_engine_tests")
    file(GLOB ELOOPER_ENGINE_SOURCES CONFIGURE_DEPENDS Source/Model/*.cpp Source/Engine/*.cpp)
    target_sources(elooper_engine_tests PRIVATE ${ELOOPER_ENGINE_SOURCES} Tests/EngineTests.cpp)
    target_compile_definitions(elooper_engine_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_LV2=0
        ELOOPER_HEADLESS=1)
    if(APPLE)
        target_compile_definitions(elooper_engine_tests PRIVATE JUCE_PLUGINHOST_AU=1)
    endif()
    target_link_libraries(elooper_engine_tests PRIVATE
        elooper_core Ableton::Link
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags)
    add_test(NAME engine COMMAND elooper_engine_tests)
endif()
