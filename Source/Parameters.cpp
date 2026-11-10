#include "Parameters.h"

namespace Params
{
    namespace
    {
        struct Division { const char* name; double quarters; };

        constexpr Division divisions[] =
        {
            { "1/32",  0.125 },
            { "1/16T", 1.0 / 6.0 },
            { "1/16",  0.25 },
            { "1/16D", 0.375 },
            { "1/8T",  1.0 / 3.0 },
            { "1/8",   0.5 },
            { "1/8D",  0.75 },
            { "1/4T",  2.0 / 3.0 },
            { "1/4",   1.0 },
            { "1/4D",  1.5 },
            { "1/2T",  4.0 / 3.0 },
            { "1/2",   2.0 },
            { "1/2D",  3.0 },
            { "1 Bar", 4.0 },
        };

        constexpr int numDivisions = (int) std::size (divisions);

        int divisionIndex (const char* name)
        {
            for (int i = 0; i < numDivisions; ++i)
                if (juce::String (divisions[i].name) == name)
                    return i;
            jassertfalse;
            return 0;
        }

        juce::String semitoneText (float value, int)
        {
            const auto st = juce::roundToInt (value);
            static const char* intervals[] = { "Unison", "m2", "M2", "m3", "M3", "4th", "Tritone",
                                               "5th", "m6", "M6", "m7", "M7" };
            const auto absSt = std::abs (st);
            juce::String name = absSt % 12 == 0 && absSt > 0
                                    ? juce::String (absSt / 12) + " Oct"
                                    : juce::String (intervals[absSt % 12]);
            return (st > 0 ? "+" : "") + juce::String (st) + " st (" + name + ")";
        }
    }

    const juce::StringArray& divisionNames()
    {
        static const juce::StringArray names = []
        {
            juce::StringArray s;
            for (const auto& d : divisions)
                s.add (d.name);
            return s;
        }();
        return names;
    }

    double divisionInQuarterNotes (int index)
    {
        return divisions[juce::jlimit (0, numDivisions - 1, index)].quarters;
    }

    const juce::StringArray& noteNames()
    {
        static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F",
                                               "F#", "G", "G#", "A", "A#", "B" };
        return names;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;
        constexpr int version = 1;

        auto percent = AudioParameterFloatAttributes()
                           .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
        auto hertz = AudioParameterFloatAttributes()
                         .withStringFromValueFunction ([] (float v, int)
                         {
                             return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz";
                         });

        // ---- Global ----
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { sync, version }, "Tempo Sync", true));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { rootNote, version }, "Root Key", noteNames(), 0));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { rootOct, version }, "Root Octave", 1, 6, 2));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { resonance, version }, "Resonance",
                                                           NormalisableRange<float> (0.0f, 0.97f), 0.75f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { resMix, version }, "Harmonic Blend",
                                                           NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { damping, version }, "Damping",
                                                           NormalisableRange<float> (800.0f, 20000.0f, 0.0f, 0.3f), 7000.0f, hertz));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { lowCut, version }, "Low Cut",
                                                           NormalisableRange<float> (20.0f, 1500.0f, 0.0f, 0.4f), 150.0f, hertz));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, version }, "Mix",
                                                           NormalisableRange<float> (0.0f, 1.0f), 0.35f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { output, version }, "Output",
                                                           NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                           {
                                                               return (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
                                                           })));

        // ---- Taps ----
        struct TapDefaults { bool on; const char* div; float timeMs; int pitch; float fb; int harm; float level; float pan; };
        const TapDefaults defaults[numTaps] =
        {
            { true,  "1/4",  375.0f,   0, 0.35f, 1, 0.75f,  0.0f },
            { true,  "1/8D", 280.0f,   7, 0.30f, 3, 0.60f, -0.6f },
            { true,  "1/2",  750.0f,  12, 0.40f, 2, 0.50f,  0.6f },
            { false, "1/8T", 165.0f, -12, 0.25f, 4, 0.45f,  0.0f },
        };

        for (int t = 0; t < numTaps; ++t)
        {
            const auto& d = defaults[t];
            const auto prefix = "Tap " + String (t + 1) + " ";

            layout.add (std::make_unique<AudioParameterBool> (ParameterID { tapId (t, Tap::on), version },
                                                              prefix + "On", d.on));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { tapId (t, Tap::division), version },
                                                                prefix + "Division", divisionNames(), divisionIndex (d.div)));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tapId (t, Tap::timeMs), version }, prefix + "Time",
                                                               NormalisableRange<float> (10.0f, 2500.0f, 0.0f, 0.4f), d.timeMs,
                                                               AudioParameterFloatAttributes().withLabel ("ms")));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tapId (t, Tap::pitch), version }, prefix + "Pitch",
                                                               NormalisableRange<float> (-24.0f, 24.0f, 1.0f), (float) d.pitch,
                                                               AudioParameterFloatAttributes().withStringFromValueFunction (semitoneText)));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tapId (t, Tap::feedback), version }, prefix + "Feedback",
                                                               NormalisableRange<float> (0.0f, 0.95f), d.fb, percent));
            layout.add (std::make_unique<AudioParameterInt> (ParameterID { tapId (t, Tap::harmonic), version },
                                                             prefix + "Harmonic", 1, 16, d.harm));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tapId (t, Tap::level), version }, prefix + "Level",
                                                               NormalisableRange<float> (0.0f, 1.0f), d.level, percent));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tapId (t, Tap::pan), version }, prefix + "Pan",
                                                               NormalisableRange<float> (-1.0f, 1.0f), d.pan,
                                                               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                               {
                                                                   const auto p = roundToInt (v * 100.0f);
                                                                   return p == 0 ? String ("C") : String (std::abs (p)) + (p < 0 ? "L" : "R");
                                                               })));
        }

        return layout;
    }
}
