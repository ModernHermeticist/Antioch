#include "RoleCatalog.h"
#include "Parameters.h"

namespace qn
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        auto hz = [] (float lo, float hi) { juce::NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi)); return r; };
        const auto notAutomatable = juce::AudioParameterChoiceAttributes().withAutomatable (false);
        const auto boolNotAutomatable = juce::AudioParameterBoolAttributes().withAutomatable (false);
        const auto hzAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (v)) + " Hz";
        });
        const auto dbAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });

        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoEqId, 1 }, "Auto EQ", true));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoMbId, 1 }, "Auto dynamics", true));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { transitionsId, 1 }, "Transitions", true));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { chordAwareId, 1 }, "Chord-aware", true));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { roleId, 1 }, "Role",
                   juce::StringArray { "From the track", "Kick", "Bass", "Hats", "Pads", "Lead", "Vocal", "FX", "Music bus" }, 0, notAutomatable));
        {
            // the finer role (36, in families): 0 = from the track's name, or guessed from the sound
            juce::StringArray subNames { "From the track" };
            for (int r = 0; r < suite::roles::numRoles; ++r) subNames.add (suite::roles::fullName (r));
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { subRoleId, 1 }, "Track role", subNames, 0, notAutomatable));
        }

        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { lowCutId, 1 }, "Low cut", hz (20.0f, 1000.0f), 20.0f,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= lowCutOff ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; })));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { highCutId, 1 }, "High cut", hz (1000.0f, 20000.0f), 20000.0f,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= highCutOff ? juce::String ("Off") : juce::String (v / 1000.0f, 1) + " kHz"; })));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { widthId, 1 }, "Width", juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { punchId, 1 }, "Punch", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.005f ? juce::String ("Off") : (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { glueThrId, 1 }, "Glue threshold", juce::NormalisableRange<float> (-40.0f, 0.0f, 0.1f), 0.0f,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v > -0.05f ? juce::String ("Off") : juce::String (v, 1) + " dB"; })));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { deessFreqId, 1 }, "De-ess frequency", hz (3000.0f, 12000.0f), 7000.0f, hzAttr));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { deessDepthId, 1 }, "De-ess depth", juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), 0.0f,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? juce::String ("Off") : juce::String (v, 1) + " dB"; })));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { widthLockId, 1 }, "Width locked", false, boolNotAutomatable));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { lowCutLockId, 1 }, "Low cut locked", false, boolNotAutomatable));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { highCutLockId, 1 }, "High cut locked", false, boolNotAutomatable));

        for (int s = 0; s < numSlots; ++s)
        {
            const auto n = "Band " + juce::String (s + 1) + " ";
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { freqId (s), 1 }, n + "frequency", hz (20.0f, 20000.0f),
                       defaultFreqs()[(size_t) s], hzAttr));
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { gainId (s), 1 }, n + "gain",
                       juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr));
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { qId (s), 1 }, n + "Q",
                       juce::NormalisableRange<float> (0.3f, 6.0f, 0.01f, 0.5f), 1.0f));
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { reasonId (s), 1 }, n + "reason", reasonNames(), 0, notAutomatable));
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { whenId (s), 1 }, n + "when",
                       juce::StringArray { "Always", "While the vocal sings", "While the lead plays" }, 0, notAutomatable));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { lockId (s), 1 }, n + "locked", false, boolNotAutomatable));
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bdId (s), 1 }, n + "breakdown gain",
                       juce::NormalisableRange<float> (-8.0f, 8.0f, 0.1f), 0.0f, dbAttr));
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { grId (s), 1 }, n + "groove gain",
                       juce::NormalisableRange<float> (-8.0f, 8.0f, 0.1f), 0.0f, dbAttr));
        }

        for (int b = 0; b < numMbBands; ++b)
        {
            const auto n = mbBandNames()[b] + " band ";
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { thrId (b), 1 }, n + "threshold",
                       juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), 0.0f, dbAttr));
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ratioId (b), 1 }, n + "ratio",
                       juce::NormalisableRange<float> (1.0f, 10.0f, 0.1f, 0.5f), 1.0f,
                       juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= 1.01f ? juce::String ("Off") : juce::String (v, 1) + ":1"; })));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { mbLockId (b), 1 }, n + "locked", false, boolNotAutomatable));
        }
        return l;
    }
}
