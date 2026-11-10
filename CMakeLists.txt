cmake_minimum_required(VERSION 3.22)

project(Antioch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build universal binaries on macOS, and use the static MSVC runtime on Windows
# so the plugin has no runtime DLL dependencies on the user's machine.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Antioch
    COMPANY_NAME            "ModernHermeticist"
    PLUGIN_MANUFACTURER_CODE Mhrm
    PLUGIN_CODE             Anth
    PRODUCT_NAME            "Antioch"
    DESCRIPTION             "Harmonic echo: pitch-shifted, tempo-synced, key-tuned delay"
    FORMATS                 VST3 Standalone
    VST3_CATEGORIES         Fx Delay
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Antioch PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp)

target_include_directories(Antioch PRIVATE Source)

target_compile_definitions(Antioch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Antioch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline tests (DSP stability, state round-trip, UI snapshot) ----
option(ANTIOCH_BUILD_TESTS "Build the offline test runner" OFF)
if(ANTIOCH_BUILD_TESTS)
    juce_add_console_app(AntiochTests PRODUCT_NAME "AntiochTests")
    target_sources(AntiochTests PRIVATE Tests/RenderTest.cpp)
    target_include_directories(AntiochTests PRIVATE Source)
    target_compile_definitions(AntiochTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AntiochTests PRIVATE
        Antioch
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)
endif()
