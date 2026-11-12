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
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoCoach, 1 }, "Auto-coach", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { compareAB, 1 }, "Compare with Reference (A/B)", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoMasking, 1 }, "Resolve Masking Automatically", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoBalance, 1 }, "Balance the Mix Automatically", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { problemWatch, 1 }, "Watch for Problems", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { shareTone, 1 }, "Share the Master's Tone Across Quintessences", true));
        return layout;
    }
}
