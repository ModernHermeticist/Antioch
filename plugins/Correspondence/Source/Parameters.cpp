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
        const StringArray bassPatternNames { "Offbeat", "Rolling", "Galloping", "Sustain", "Walking" };
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
        layout.add (std::make_unique<AudioParameterBool> (id (acceptDetected), "Accept Detected Key", true));
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
        const chords::SongPatterns songDefaults;
        for (int t = 0; t < (int) suite::SectionType::count; ++t)
            for (auto part : { chords::SongPart::Chords, chords::SongPart::Bass, chords::SongPart::Arp, chords::SongPart::Lead })
            {
                const auto& defaults = part == chords::SongPart::Chords ? songDefaults.chords : part == chords::SongPart::Bass ? songDefaults.bass
                                     : part == chords::SongPart::Arp ? songDefaults.arp : songDefaults.lead;
                const String partName = part == chords::SongPart::Chords ? "Chords" : part == chords::SongPart::Bass ? "Bass"
                                      : part == chords::SongPart::Arp ? "Arp" : "Lead";
                layout.add (std::make_unique<AudioParameterChoice> (id (songId (part, t)),
                                                                    String ("Song ") + suite::sectionName ((suite::SectionType) t) + " " + partName,
                                                                    chords::songChoiceNames (part), defaults[(size_t) t]));
            }

        layout.add (std::make_unique<AudioParameterChoice> (id (melodyStyle), "Melody Style", chords::melodyStyleNames(), 1));
        layout.add (std::make_unique<AudioParameterInt> (id (melodySeed), "Melody Seed", 1, 999, 7));
        layout.add (std::make_unique<AudioParameterInt> (id (melodyOctave), "Melody Octave", 4, 6, 5));
        layout.add (std::make_unique<AudioParameterBool> (id (songLift), "Lead Up an Octave in Later Drops", true));
        layout.add (std::make_unique<AudioParameterBool> (id (songVary), "Vary the Lead and Add Harmony in Later Drops", true));
        layout.add (std::make_unique<AudioParameterFloat> (id (humanise), "Humanise Exports", NormalisableRange<float> (0.0f, 1.0f), 0.3f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
        layout.add (std::make_unique<AudioParameterBool> (id (songCounter), "Counter-Melody", true));
        layout.add (std::make_unique<AudioParameterChoice> (id (songBreakdown), "Breakdown Progression",
                                                            StringArray { "Same as the drop", "Start on VI", "Start on IV" }, 1));

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

    chords::SongPatterns readSongPatterns (const AudioProcessorValueTreeState& state)
    {
        chords::SongPatterns p;
        for (int t = 0; t < (int) suite::SectionType::count; ++t)
        {
            p.chords[(size_t) t] = choiceIndex (state, songId (chords::SongPart::Chords, t));
            p.bass[(size_t) t]   = choiceIndex (state, songId (chords::SongPart::Bass, t));
            p.arp[(size_t) t]    = choiceIndex (state, songId (chords::SongPart::Arp, t));
            p.lead[(size_t) t]   = choiceIndex (state, songId (chords::SongPart::Lead, t));
        }
        p.melody = readMelody (state);
        p.liftLaterDrops = state.getRawParameterValue (songLift)->load() > 0.5f;
        p.varyLaterDrops = state.getRawParameterValue (songVary)->load() > 0.5f;
        p.breakdownStart = (int) state.getRawParameterValue (songBreakdown)->load();
        p.counter = state.getRawParameterValue (songCounter)->load() > 0.5f;
        return p;
    }

    chords::MelodySettings readMelody (const AudioProcessorValueTreeState& state)
    {
        chords::MelodySettings m;
        m.style = (chords::MelodyStyle) choiceIndex (state, melodyStyle);
        m.seed = choiceIndex (state, melodySeed);
        m.octave = choiceIndex (state, melodyOctave);
        return m;
    }
}
