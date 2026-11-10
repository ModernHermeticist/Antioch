#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    inline const juce::String loudnessTarget { "loudtarget" };
    inline const juce::String tonalTarget    { "tonaltarget" };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
