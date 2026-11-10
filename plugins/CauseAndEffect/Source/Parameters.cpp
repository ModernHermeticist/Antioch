#include "Parameters.h"
#include "Music.h"

namespace Params
{
    using namespace juce;

    namespace
    {
        using Attr = AudioParameterFloatAttributes;
        ParameterID id (const String& s) { return { s, 1 }; }

        Attr percent() { return Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }); }
        Attr hertz()
        {
            return Attr().withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz";
            });
        }

        NormalisableRange<float> skewed (float lo, float hi, float centre)
        {
            NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return r;
        }

        const StringArray lengthNames { "4 bars", "8 bars", "16 bars", "32 bars" };
        const StringArray riserNames { "1 oct", "2 oct", "3 oct" };
        const StringArray stutterNames { "1/16", "1/32" };
    }

    int lengthBars (int index)         { static const int b[] = { 4, 8, 16, 32 }; return b[jlimit (0, 3, index)]; }
    int riserOctaves (int index)       { return jlimit (0, 2, index) + 1; }
    double stutterMaxQuarters (int index) { return index == 0 ? 0.25 : 0.125; }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterFloat> (id (build), "Build", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent()));
        layout.add (std::make_unique<AudioParameterBool> (id (trigger), "Trigger", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (length), "Build Length", lengthNames, 2));
        layout.add (std::make_unique<AudioParameterFloat> (id (curve), "Curve", skewed (0.5f, 4.0f, 1.5f), 2.0f,
                                                           Attr().withStringFromValueFunction ([] (float v, int)
                                                           {
                                                               return v < 0.85f ? String ("Early") : v < 1.2f ? String ("Linear")
                                                                    : v < 2.5f ? String ("Late") : String ("Very late");
                                                           })));
        layout.add (std::make_unique<AudioParameterBool> (id (dropCut), "Drop Cut", true));

        layout.add (std::make_unique<AudioParameterFloat> (id (filterMax), "Filter Sweep", skewed (40.0f, 3000.0f, 500.0f), 900.0f, hertz()));
        layout.add (std::make_unique<AudioParameterFloat> (id (filterRes), "Filter Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.35f, percent()));
        layout.add (std::make_unique<AudioParameterFloat> (id (wash), "Wash", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent()));
        layout.add (std::make_unique<AudioParameterFloat> (id (washDecay), "Wash Decay", skewed (1.0f, 15.0f, 5.0f), 5.0f,
                                                           Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; })));
        layout.add (std::make_unique<AudioParameterFloat> (id (noise), "Noise Riser", NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent()));
        layout.add (std::make_unique<AudioParameterFloat> (id (riser), "Tonal Riser", NormalisableRange<float> (0.0f, 1.0f), 0.25f, percent()));
        layout.add (std::make_unique<AudioParameterChoice> (id (riserRange), "Riser Range", riserNames, 1));
        layout.add (std::make_unique<AudioParameterFloat> (id (stutter), "Stutter", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent()));
        layout.add (std::make_unique<AudioParameterChoice> (id (stutterMax), "Stutter Max Rate", stutterNames, 0));
        layout.add (std::make_unique<AudioParameterFloat> (id (widen), "Widen", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent()));

        layout.add (std::make_unique<AudioParameterChoice> (id (rootNote), "Root Key", suite::music::noteNames(), 9));
        layout.add (std::make_unique<AudioParameterBool> (id (followKey), "Follow Key", true));
        layout.add (std::make_unique<AudioParameterBool> (id (follow), "Follow Ouroboros", true));
        layout.add (std::make_unique<AudioParameterFloat> (id (output), "Output", NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                                           Attr().withStringFromValueFunction ([] (float v, int)
                                                           {
                                                               return (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
                                                           })));
        return layout;
    }
}
