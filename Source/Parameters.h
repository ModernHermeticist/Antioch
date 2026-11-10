#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    constexpr int numTaps = 4;

    // Global parameter IDs
    inline const juce::String sync      { "sync" };
    inline const juce::String rootNote  { "root" };
    inline const juce::String rootOct   { "octave" };
    inline const juce::String resonance { "resonance" };
    inline const juce::String resMix    { "resmix" };
    inline const juce::String damping   { "damp" };
    inline const juce::String lowCut    { "lowcut" };
    inline const juce::String mix       { "mix" };
    inline const juce::String output    { "output" };

    // Per-tap parameter IDs, e.g. tapId (2, "pitch") -> "tap2_pitch"
    inline juce::String tapId (int tapIndex, const char* name)
    {
        return "tap" + juce::String (tapIndex + 1) + "_" + name;
    }

    namespace Tap
    {
        constexpr const char* on       = "on";
        constexpr const char* division = "div";
        constexpr const char* timeMs   = "time";
        constexpr const char* pitch    = "pitch";
        constexpr const char* feedback = "fb";
        constexpr const char* harmonic = "harm";
        constexpr const char* level    = "level";
        constexpr const char* pan      = "pan";
    }

    const juce::StringArray& divisionNames();
    double divisionInQuarterNotes (int divisionIndex);

    const juce::StringArray& noteNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
