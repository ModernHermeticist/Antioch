#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    inline const juce::String loudnessTarget { "loudtarget" };
    inline const juce::String tonalTarget    { "tonaltarget" };
    inline const juce::String autoCoach      { "autocoach" };   // apply the coach's fixes by itself after each play-through

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
