#include "Parameters.h"
#include "Advisor.h"

namespace Params
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::StringArray loud, tonal;
        for (auto& t : advisor::loudnessTargets()) loud.add (t.name);
        for (auto& t : advisor::tonalTargets()) tonal.add (t.name);

        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { loudnessTarget, 1 }, "Loudness Target", loud, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { tonalTarget, 1 }, "Tonal Target", tonal, 0));
        return layout;
    }
}
