#include "Parameters.h"

namespace Params
{
    using namespace juce;

    namespace
    {
        ParameterID id (const String& s) { return { s, 1 }; }

        const StringArray perChordNames { "1/2 bar", "1 bar", "2 bars" };
        const StringArray degreeNames { "I", "II", "III", "IV", "V", "VI", "VII" };
        const StringArray voicingNames { "Close", "Open", "Wide" };
        const StringArray chordPatternNames { "Sustain", "Offbeat stabs", "Pluck 3-3-2", "16th gate" };
        const StringArray bassPatternNames { "Offbeat", "Rolling", "Galloping", "Sustain" };
        const StringArray arpPatternNames { "Up", "Down", "Up-down", "Bounce" };

        int choiceIndex (const AudioProcessorValueTreeState& s, const String& pid)
        {
            return roundToInt (s.getRawParameterValue (pid)->load());
        }
    }

    double quartersPerChord (int index) { static const double q[] = { 2.0, 4.0, 8.0 }; return q[jlimit (0, 2, index)]; }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterChoice> (id (rootNote), "Key", suite::music::noteNames(), 9));
        layout.add (std::make_unique<AudioParameterChoice> (id (scale), "Scale", suite::music::scaleNames(), 0));
        layout.add (std::make_unique<AudioParameterBool> (id (broadcast), "Broadcast Key", true));
        layout.add (std::make_unique<AudioParameterInt> (id (count), "Chords", 2, chords::maxSlots, 4));
        layout.add (std::make_unique<AudioParameterChoice> (id (perChord), "Length per Chord", perChordNames, 1));

        const auto& anthem = chords::presets().front();
        for (int i = 0; i < chords::maxSlots; ++i)
        {
            const auto& slot = anthem.slots[(size_t) i];
            layout.add (std::make_unique<AudioParameterChoice> (id (slotDegree (i)), "Chord " + String (i + 1) + " Degree",
                                                                degreeNames, slot.degree - 1));
            layout.add (std::make_unique<AudioParameterChoice> (id (slotQuality (i)), "Chord " + String (i + 1) + " Quality",
                                                                chords::qualityNames(), (int) slot.quality));
        }

        layout.add (std::make_unique<AudioParameterChoice> (id (voicing), "Voicing", voicingNames, 1));
        layout.add (std::make_unique<AudioParameterBool> (id (voiceLead), "Voice Leading", true));
        layout.add (std::make_unique<AudioParameterInt> (id (registerOct), "Register", 3, 5, 4));
        layout.add (std::make_unique<AudioParameterBool> (id (addBass), "Bass Note in Chords", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (chordPattern), "Chord Pattern", chordPatternNames, 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (bassPattern), "Bass Pattern", bassPatternNames, 0));
        layout.add (std::make_unique<AudioParameterInt> (id (bassOctave), "Bass Octave", 1, 3, 2));
        layout.add (std::make_unique<AudioParameterChoice> (id (arpPattern), "Arp Pattern", arpPatternNames, 0));
        layout.add (std::make_unique<AudioParameterInt> (id (arpOctaves), "Arp Octaves", 1, 3, 2));
        layout.add (std::make_unique<AudioParameterFloat> (id (preview), "Preview Volume", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                           {
                                                               return String (roundToInt (v * 100.0f)) + "%";
                                                           })));
        return layout;
    }

    chords::Settings readSettings (const AudioProcessorValueTreeState& state)
    {
        chords::Settings s;
        s.root = choiceIndex (state, rootNote);
        s.scale = choiceIndex (state, scale);
        s.count = choiceIndex (state, count);
        s.quartersPerChord = quartersPerChord (choiceIndex (state, perChord));
        for (int i = 0; i < chords::maxSlots; ++i)
        {
            s.slots[(size_t) i].degree = choiceIndex (state, slotDegree (i)) + 1;
            s.slots[(size_t) i].quality = (chords::Quality) choiceIndex (state, slotQuality (i));
        }
        s.voicing = (chords::Voicing) choiceIndex (state, voicing);
        s.voiceLead = state.getRawParameterValue (voiceLead)->load() > 0.5f;
        s.registerOctave = choiceIndex (state, registerOct);
        s.addBass = state.getRawParameterValue (addBass)->load() > 0.5f;
        s.chordPattern = (chords::ChordPattern) choiceIndex (state, chordPattern);
        s.bassPattern = (chords::BassPattern) choiceIndex (state, bassPattern);
        s.bassOctave = choiceIndex (state, bassOctave);
        s.arpPattern = (chords::ArpPattern) choiceIndex (state, arpPattern);
        s.arpOctaves = choiceIndex (state, arpOctaves);
        return s;
    }
}
