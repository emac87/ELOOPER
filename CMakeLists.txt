cmake_minimum_required(VERSION 3.22)
project(ELooper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT IOS AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

option(ELOOPER_BUILD_APP   "Build the JUCE application" ON)
option(ELOOPER_BUILD_TESTS "Build unit tests" ON)
option(CHIMERA_BUILD_PLUGIN "Build Chimera as its own AU / VST3 / Standalone" ON)

# iPad/iPhone (cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS): only Chimera, as an AUv3 inside a Standalone app,
# for TestFlight. tools/ship_chimera_ios.sh drives it on the Mac.
set(CHIMERA_TEAM_ID "" CACHE STRING "Apple Development Team ID (iOS signing)")
set(CHIMERA_BUILD "1" CACHE STRING "Build number (CFBundleVersion); must increase for each TestFlight upload")
if(IOS)
    set(ELOOPER_BUILD_APP OFF CACHE BOOL "" FORCE)
    set(ELOOPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CHIMERA_BUILD_PLUGIN ON CACHE BOOL "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Real-time core: pure C++17, no JUCE. Unit-testable anywhere.
# ---------------------------------------------------------------------------
add_library(elooper_core STATIC
    Source/Core/Automation.cpp
    Source/Core/GraphicEq.cpp
    Source/Core/LoopCore.cpp
    Source/Core/MidiClip.cpp
    Source/Core/Pdc.cpp
    Source/Core/Routing.cpp
)
target_include_directories(elooper_core PUBLIC Source)
if(NOT MSVC)
    target_compile_options(elooper_core PRIVATE -Wall -Wextra)
endif()

# Chimera (built-in synth) DSP: pure C++17, no JUCE.
file(GLOB CHIMERA_DSP_SOURCES CONFIGURE_DEPENDS Source/Synth/dsp/*.cpp)
add_library(chimera_dsp STATIC ${CHIMERA_DSP_SOURCES})
set_target_properties(chimera_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)   # linked into the plug-in .so/.dll too
target_include_directories(chimera_dsp PUBLIC Source)
if(NOT MSVC)
    target_compile_options(chimera_dsp PRIVATE -Wall -Wextra)
endif()

if(ELOOPER_BUILD_TESTS)
    enable_testing()
    add_executable(elooper_core_tests Tests/TestMain.cpp Tests/CoreTests.cpp Tests/CoreExtraTests.cpp)
    target_link_libraries(elooper_core_tests PRIVATE elooper_core)
    add_test(NAME core COMMAND elooper_core_tests)
    file(GLOB CHIMERA_TEST_SOURCES CONFIGURE_DEPENDS Tests/Synth*Tests.cpp)
    add_executable(elooper_synth_tests Tests/TestMain.cpp ${CHIMERA_TEST_SOURCES})
    target_link_libraries(elooper_synth_tests PRIVATE chimera_dsp)
    add_test(NAME synth COMMAND elooper_synth_tests)
endif()

if(NOT ELOOPER_BUILD_APP AND NOT CHIMERA_BUILD_PLUGIN)
    return()
endif()

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# Chimera on its own: AU (macOS), VST3 and a Standalone app, from the same processor and editor
# that ELooper hosts as a built-in instrument.
# ---------------------------------------------------------------------------
if(CHIMERA_BUILD_PLUGIN)
    set(CHIMERA_EXTRA "")
    if(IOS)
        set(CHIMERA_FORMATS AUv3 Standalone)
        set(CHIMERA_BUNDLE "com.evanmcintosh.chimera")   # App Store bundle ID (team 4YRKXPJ3P5)
        set(CHIMERA_EXTRA
            BUILD_VERSION "${CHIMERA_BUILD}"
            DEVELOPMENT_TEAM_ID "${CHIMERA_TEAM_ID}"
            ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ChimeraIcon.png"
            TARGETED_DEVICE_FAMILY "1,2"
            BACKGROUND_AUDIO_ENABLED TRUE
            FILE_SHARING_ENABLED TRUE
            DOCUMENT_BROWSER_ENABLED TRUE
            REQUIRES_FULL_SCREEN TRUE    # landscape-only on iPad needs this (App Store rule)
            IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
            IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight)
    else()
        set(CHIMERA_FORMATS VST3 Standalone)
        if(APPLE)
            list(APPEND CHIMERA_FORMATS AU)
        endif()
        set(CHIMERA_BUNDLE "com.elooper.chimera")
    endif()
    juce_add_plugin(Chimera
        PRODUCT_NAME "Chimera"
        COMPANY_NAME "ELooper"
        BUNDLE_ID "${CHIMERA_BUNDLE}"
        ${CHIMERA_EXTRA}
        PLUGIN_MANUFACTURER_CODE Elpr
        PLUGIN_CODE Chmr
        FORMATS ${CHIMERA_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Synth
        DESCRIPTION "Multi-engine synth: analog, wavetable, DX7-style FM, sampler, pluck")
    file(GLOB CHIMERA_PLUGIN_SOURCES CONFIGURE_DEPENDS Source/Synth/plugin/*.cpp Source/Synth/ui/*.cpp)
    target_sources(Chimera PRIVATE ${CHIMERA_PLUGIN_SOURCES}
        Source/Synth/plugin/standalone/ChimeraPluginEntry.cpp
        Source/UI/ValueBar.cpp Source/UI/LookAndFeel.cpp)
    target_include_directories(Chimera PRIVATE Source)
    target_compile_definitions(Chimera PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
    target_link_libraries(Chimera PRIVATE
        chimera_dsp
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
    if(IOS)   # only the Standalone app goes into the archive (the AUv3 extension is embedded in it)
        set_target_properties(Chimera Chimera_AUv3 PROPERTIES XCODE_ATTRIBUTE_SKIP_INSTALL YES)
        set_target_properties(Chimera_Standalone PROPERTIES XCODE_ATTRIBUTE_SKIP_INSTALL NO
                              XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)")
    endif()
endif()

if(NOT ELOOPER_BUILD_APP)
    return()
endif()

# App icon (generated by tools/make_icon.py).
set(ELOOPER_ICON "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png)
    set(ELOOPER_ICON ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png)
endif()

juce_add_gui_app(ELooper
    PRODUCT_NAME "ELooper"
    ${ELOOPER_ICON}
    COMPANY_NAME "ELooper"
    BUNDLE_ID "com.elooper.app"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ELooper needs audio input access to record loops."
    PLUGINHOST_AU TRUE
)

file(GLOB ELOOPER_APP_SOURCES CONFIGURE_DEPENDS
    Source/Model/*.cpp Source/Engine/*.cpp Source/UI/*.cpp Source/Platform/*.cpp Source/Main.cpp)
# Chimera is no longer built into the app: it is loaded as its own Audio Unit (target Chimera above).
target_sources(ELooper PRIVATE ${ELOOPER_APP_SOURCES})
if(APPLE)
    file(GLOB ELOOPER_MAC_SOURCES CONFIGURE_DEPENDS Source/Platform/*.mm)
    target_sources(ELooper PRIVATE ${ELOOPER_MAC_SOURCES})
    target_link_libraries(ELooper PRIVATE "-framework AVFoundation" "-framework AudioToolbox" "-framework AppKit")
endif()

# Build identity for the auto-updater: CI builds carry their commit; local builds are "dev"
# (never auto-updated).
if(DEFINED ENV{GITHUB_SHA})
    set(ELOOPER_BUILD_SHA "$ENV{GITHUB_SHA}")
else()
    set(ELOOPER_BUILD_SHA "dev")
endif()

target_compile_definitions(ELooper PRIVATE
    ELOOPER_BUILD_SHA="${ELOOPER_BUILD_SHA}"
    ELOOPER_REPO="emac87/ELOOPER"
    ELOOPER_UPDATE_BRANCH="elooper-initial"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ELooper,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ELooper,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=0   # Audio Units only
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
    file(GLOB ELOOPER_ENGINE_SOURCES CONFIGURE_DEPENDS Source/Model/*.cpp Source/Engine/*.cpp Source/Synth/plugin/*.cpp)
    target_sources(elooper_engine_tests PRIVATE ${ELOOPER_ENGINE_SOURCES} Tests/EngineTests.cpp)
    target_compile_definitions(elooper_engine_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_LV2=0
        ELOOPER_HEADLESS=1)
    if(APPLE)
        target_compile_definitions(elooper_engine_tests PRIVATE JUCE_PLUGINHOST_AU=1)
    endif()
    target_link_libraries(elooper_engine_tests PRIVATE
        elooper_core chimera_dsp Ableton::Link
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags)
    add_test(NAME engine COMMAND elooper_engine_tests)
endif()
