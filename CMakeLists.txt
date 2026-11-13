cmake_minimum_required(VERSION 3.22)

project(AntiochSuite VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binaries on macOS; static MSVC runtime on Windows so the plugins
# have no runtime DLL dependencies.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SUITE_BUILD_TESTS "Build the offline test runners" OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(shared)

# suite_add_plugin(<Target> CODE <4 chars> NAME <name> DESCRIPTION <text> CATEGORIES <...>
#                  SOURCES <...> [TESTS <file>])
function(suite_add_plugin target)
    cmake_parse_arguments(ARG "MIDI_INPUT" "CODE;NAME;DESCRIPTION;TESTS" "CATEGORIES;SOURCES" ${ARGN})
    if(ARG_MIDI_INPUT)
        set(needs_midi TRUE)
    else()
        set(needs_midi FALSE)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME            "ModernHermeticist"
        PLUGIN_MANUFACTURER_CODE Mhrm
        PLUGIN_CODE             ${ARG_CODE}
        PRODUCT_NAME            "${ARG_NAME}"
        DESCRIPTION             "${ARG_DESCRIPTION}"
        FORMATS                 VST3 Standalone
        VST3_CATEGORIES         ${ARG_CATEGORIES}
        IS_SYNTH                FALSE
        NEEDS_MIDI_INPUT        ${needs_midi}
        NEEDS_MIDI_OUTPUT       FALSE
        IS_MIDI_EFFECT          FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${target} PRIVATE ${ARG_SOURCES})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            suite_shared
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(SUITE_BUILD_TESTS AND ARG_TESTS)
        juce_add_console_app(${target}Tests PRODUCT_NAME "${target}Tests")
        target_sources(${target}Tests PRIVATE ${ARG_TESTS})
        target_include_directories(${target}Tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
            ${PROJECT_SOURCE_DIR}/shared)
        target_compile_definitions(${target}Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${target}Tests PRIVATE
            ${target}
            juce::juce_audio_formats
            juce::juce_recommended_config_flags)
    endif()
endfunction()

add_subdirectory(plugins/Antioch)
add_subdirectory(plugins/Transmutation)
add_subdirectory(plugins/Correspondence)
add_subdirectory(plugins/Kybalion)
add_subdirectory(plugins/Vibration)
add_subdirectory(plugins/Mentalism)
add_subdirectory(plugins/Ouroboros)
add_subdirectory(plugins/Lapis)
add_subdirectory(plugins/EmeraldTablet)
add_subdirectory(plugins/Azoth)
add_subdirectory(plugins/PrimaMateria)
add_subdirectory(plugins/SolveEtCoagula)
add_subdirectory(plugins/Quintessence)
