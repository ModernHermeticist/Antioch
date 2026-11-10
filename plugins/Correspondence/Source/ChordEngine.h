#pragma once

// Pure music logic for Correspondence: chords from scale degrees, voicing with
// voice leading, rhythm patterns for chords/bass/arp, presets and MIDI export.
// No audio or UI code here so it can be unit-tested directly.

#include <juce_audio_basics/juce_audio_basics.h>
#include "Music.h"
#include "SectionBus.h"
#include <vector>

namespace chords
{
    constexpr int maxSlots = 8;

    enum class Quality { Auto, Major, Minor, Sus2, Sus4, Seventh, Add9, Power };
    enum class Voicing { Close, Open, Wide };
    enum class ChordPattern { Sustain, OffbeatStabs, Pluck332, Gate16 };
    enum class BassPattern { Offbeat, Rolling, Galloping, Sustain };
    enum class ArpPattern { Up, Down, UpDown, Bounce };

    inline const juce::StringArray& qualityNames()
    {
        static const juce::StringArray n { "Auto", "Major", "Minor", "Sus2", "Sus4", "7th", "Add9", "Power" };
        return n;
    }

    struct Slot { int degree = 1; Quality quality = Quality::Auto; }; // degree 1..7

    struct Settings
    {
        int root = 9, scale = 0, count = 4;
        std::array<Slot, maxSlots> slots {};
        double quartersPerChord = 4.0;
        Voicing voicing = Voicing::Open;
        bool voiceLead = true, addBass = false;
        int registerOctave = 4;                 // chords centre around this octave
        ChordPattern chordPattern = ChordPattern::Sustain;
        BassPattern bassPattern = BassPattern::Offbeat;
        int bassOctave = 2;
        ArpPattern arpPattern = ArpPattern::Up;
        int arpOctaves = 2;
    };

    struct Note
    {
        int pitch;
        double start, length;   // quarter notes
        int velocity;
    };

    //==============================================================================
    /** Chord as semitone offsets: `root` from the tonic, `intervals` from the chord root. */
    struct Chord
    {
        int root = 0;
        std::vector<int> intervals;
    };

    inline Chord buildChord (const Settings& s, const Slot& slot)
    {
        using suite::music::scaleStep;
        const auto d = juce::jlimit (1, 7, slot.degree) - 1;
        Chord c;
        c.root = scaleStep (s.scale, d);
        auto diatonic = [&] (int step) { return scaleStep (s.scale, d + step) - c.root; };
        const auto fifth = diatonic (4);

        switch (slot.quality)
        {
            case Quality::Auto:    c.intervals = { 0, diatonic (2), fifth }; break;
            case Quality::Major:   c.intervals = { 0, 4, 7 }; break;
            case Quality::Minor:   c.intervals = { 0, 3, 7 }; break;
            case Quality::Sus2:    c.intervals = { 0, 2, 7 }; break;
            case Quality::Sus4:    c.intervals = { 0, 5, 7 }; break;
            case Quality::Seventh: c.intervals = { 0, diatonic (2), fifth, diatonic (6) }; break;
            case Quality::Add9:    c.intervals = { 0, diatonic (2), fifth, diatonic (8) }; break;
            case Quality::Power:   c.intervals = { 0, 7, 12 }; break;
        }
        return c;
    }

    inline juce::String chordName (const Settings& s, const Slot& slot)
    {
        const auto c = buildChord (s, slot);
        const auto name = suite::music::noteNames()[(s.root + c.root) % 12];
        const auto& iv = c.intervals;

        if (slot.quality == Quality::Power) return name + "5";
        if (slot.quality == Quality::Sus2)  return name + "sus2";
        if (slot.quality == Quality::Sus4)  return name + "sus4";

        const auto third = iv[1], fifthIv = iv[2];
        juce::String triad = third == 4 ? (fifthIv == 8 ? "aug" : "") : (fifthIv == 6 ? "dim" : "m");

        if (slot.quality == Quality::Seventh)
        {
            const auto sev = iv[3];
            if (triad == "dim") return name + (sev == 10 ? "m7b5" : "dim7");
            if (triad == "m")   return name + (sev == 11 ? "m(maj7)" : "m7");
            return name + (sev == 11 ? "maj7" : "7");
        }
        if (slot.quality == Quality::Add9)
            return name + triad + "add9";
        return name + triad;
    }

    /** Roman numeral for the slot: upper case = major, lower = minor, ° = diminished. */
    inline juce::String romanNumeral (const Settings& s, const Slot& slot)
    {
        static const char* numerals[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        juce::String r = numerals[juce::jlimit (1, 7, slot.degree) - 1];
        const auto c = buildChord (s, slot);
        const auto minorish = c.intervals[1] == 3 && slot.quality != Quality::Power && slot.quality != Quality::Sus2
                              && slot.quality != Quality::Sus4;
        if (minorish) r = r.toLowerCase();
        if (c.intervals[1] == 3 && c.intervals[2] == 6) r += juce::String::fromUTF8 ("\xc2\xb0");
        return r;
    }

    //==============================================================================
    namespace detail
    {
        /** Stacks pitch classes upward starting at `firstPitch`. */
        inline std::vector<int> stack (const std::vector<int>& pcs, int firstPitch)
        {
            std::vector<int> v { firstPitch };
            for (size_t i = 1; i < pcs.size(); ++i)
            {
                auto p = v.back() + 1;
                while (((p % 12) + 12) % 12 != pcs[i]) ++p;
                v.push_back (p);
            }
            return v;
        }

        inline double centroid (const std::vector<int>& v)
        {
            double sum = 0.0;
            for (auto p : v) sum += p;
            return v.empty() ? 0.0 : sum / (double) v.size();
        }

        inline double movement (const std::vector<int>& a, const std::vector<int>& b)
        {
            // Sum of distances from each note to the nearest note of the other chord.
            double total = 0.0;
            for (auto p : a)
            {
                int best = 1000;
                for (auto q : b) best = std::min (best, std::abs (p - q));
                total += best;
            }
            return total;
        }
    }

    /** Voiced pitches for every active slot, applying voice leading between chords. */
    inline std::vector<std::vector<int>> voiceProgression (const Settings& s)
    {
        std::vector<std::vector<int>> result;
        std::vector<int> previous;
        const auto centre = 12 * (s.registerOctave + 1) + 4; // around E of the register octave

        for (int i = 0; i < juce::jlimit (1, maxSlots, s.count); ++i)
        {
            const auto chord = buildChord (s, s.slots[(size_t) i]);

            // Unique pitch classes in chord order (root first).
            std::vector<int> pcs;
            for (auto iv : chord.intervals)
            {
                const auto pc = (s.root + chord.root + iv) % 12;
                if (std::find (pcs.begin(), pcs.end(), pc) == pcs.end())
                    pcs.push_back (pc);
            }

            std::vector<int> best;
            double bestScore = 1.0e9;

            // Voice leading off = every chord in root position, placed nearest the register.
            const auto inversions = s.voiceLead ? pcs.size() : (size_t) 1;
            for (size_t inv = 0; inv < inversions; ++inv)
            {
                std::vector<int> rotated (pcs.begin() + (long) inv, pcs.end());
                rotated.insert (rotated.end(), pcs.begin(), pcs.begin() + (long) inv);

                for (int octave = -2; octave <= 2; ++octave)
                {
                    auto first = centre - 7 + 12 * octave;
                    while (((first % 12) + 12) % 12 != rotated[0]) ++first;
                    const auto v = detail::stack (rotated, first);

                    auto score = std::abs (detail::centroid (v) - centre);
                    if (s.voiceLead && ! previous.empty())
                        score = detail::movement (v, previous) + 0.3 * score;

                    if (score < bestScore)
                    {
                        bestScore = score;
                        best = v;
                    }
                }
            }

            previous = best;
            auto voiced = best;

            if (s.voicing == Voicing::Open && voiced.size() >= 3)
            {
                voiced[voiced.size() - 2] -= 12; // drop-2: airy, pad-like spread
                std::sort (voiced.begin(), voiced.end());
            }
            else if (s.voicing == Voicing::Wide)
            {
                // Chord root an octave below the voicing plus the voicing itself: big supersaw stack.
                auto low = voiced.front() - 12;
                while (((low % 12) + 12) % 12 != (s.root + chord.root) % 12) --low;
                voiced.insert (voiced.begin(), low);
            }

            if (s.addBass)
            {
                auto bass = 12 * (s.bassOctave + 1) + (s.root + chord.root) % 12;
                while (bass >= voiced.front()) bass -= 12;
                voiced.insert (voiced.begin(), bass);
            }

            result.push_back (voiced);
        }
        return result;
    }

    inline int chordRootPitchClass (const Settings& s, int slot)
    {
        return (s.root + buildChord (s, s.slots[(size_t) slot]).root) % 12;
    }

    //==============================================================================
    /** Calls fn (startWithinSlot, length, accent) for each hit of a 16th-grid pattern. */
    template <typename Fn>
    void forEachHit (double slotLength, Fn&& fn, std::initializer_list<int> stepsPerBar, double noteLength)
    {
        for (double bar = 0.0; bar < slotLength - 1.0e-9; bar += 4.0)
            for (auto step : stepsPerBar)
            {
                const auto t = bar + step * 0.25;
                if (t < slotLength - 1.0e-9)
                    fn (t, noteLength, step % 4 == 0);
            }
    }

    inline std::vector<Note> chordClip (const Settings& s)
    {
        std::vector<Note> notes;
        const auto voiced = voiceProgression (s);
        const auto L = s.quartersPerChord;

        for (size_t i = 0; i < voiced.size(); ++i)
        {
            const auto base = (double) i * L;
            auto emit = [&] (double t, double len, bool accent)
            {
                for (auto p : voiced[i])
                    notes.push_back ({ p, base + t, len, accent ? 110 : 92 });
            };

            switch (s.chordPattern)
            {
                case ChordPattern::Sustain:      emit (0.0, L - 0.02, true); break;
                case ChordPattern::OffbeatStabs: forEachHit (L, emit, { 2, 6, 10, 14 }, 0.25); break;
                case ChordPattern::Pluck332:     forEachHit (L, emit, { 0, 3, 6, 8, 11, 14 }, 0.2); break;
                case ChordPattern::Gate16:       forEachHit (L, emit, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, 0.18); break;
            }
        }
        return notes;
    }

    inline std::vector<Note> bassClip (const Settings& s)
    {
        std::vector<Note> notes;
        const auto L = s.quartersPerChord;

        for (int i = 0; i < juce::jlimit (1, maxSlots, s.count); ++i)
        {
            const auto pitch = 12 * (s.bassOctave + 1) + chordRootPitchClass (s, i);
            const auto base = (double) i * L;
            auto emit = [&] (double t, double len, bool) { notes.push_back ({ pitch, base + t, len, 110 }); };

            switch (s.bassPattern)
            {
                case BassPattern::Offbeat:   forEachHit (L, emit, { 2, 6, 10, 14 }, 0.4); break;
                case BassPattern::Rolling:   forEachHit (L, emit, { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }, 0.2); break;
                case BassPattern::Galloping: forEachHit (L, emit, { 2, 3, 6, 7, 10, 11, 14, 15 }, 0.2); break;
                case BassPattern::Sustain:   emit (0.0, L - 0.02, true); break;
            }
        }
        return notes;
    }

    inline std::vector<Note> arpClip (const Settings& s)
    {
        std::vector<Note> notes;
        auto plain = s;
        plain.voicing = Voicing::Close;
        plain.addBass = false;
        const auto voiced = voiceProgression (plain);
        const auto L = s.quartersPerChord;

        for (size_t i = 0; i < voiced.size(); ++i)
        {
            std::vector<int> tones;
            for (int o = 0; o < juce::jlimit (1, 3, s.arpOctaves); ++o)
                for (auto p : voiced[i])
                    tones.push_back (p + 12 * o);

            std::vector<int> order;
            const auto n = (int) tones.size();
            switch (s.arpPattern)
            {
                case ArpPattern::Up:     for (int k = 0; k < n; ++k) order.push_back (k); break;
                case ArpPattern::Down:   for (int k = n - 1; k >= 0; --k) order.push_back (k); break;
                case ArpPattern::UpDown:
                    for (int k = 0; k < n; ++k) order.push_back (k);
                    for (int k = n - 2; k > 0; --k) order.push_back (k);
                    break;
                case ArpPattern::Bounce: for (int k = 1; k < n; ++k) { order.push_back (0); order.push_back (k); } break;
            }
            if (order.empty()) order.push_back (0);

            const auto steps = (int) std::round (L * 4.0);
            for (int k = 0; k < steps; ++k)
                notes.push_back ({ tones[(size_t) order[(size_t) k % order.size()]], (double) i * L + k * 0.25, 0.2,
                                   k % 4 == 0 ? 110 : 88 });
        }
        return notes;
    }

    //==============================================================================
    // Melodia: seeded trance lead melodies over the progression.

    enum class MelodyStyle { Driving, ThreeThreeTwo, Anthem, Syncopated };

    inline const juce::StringArray& melodyStyleNames()
    {
        static const juce::StringArray n { "Driving 8ths", "Trance 3-3-2", "Anthem", "Syncopated" };
        return n;
    }

    struct MelodySettings
    {
        MelodyStyle style = MelodyStyle::ThreeThreeTwo;
        int seed = 7;
        int octave = 5;   // the melody sits around this octave
    };

    namespace detail
    {
        struct Hit { int step, length; };   // 16th-note grid within a 2-bar (32-step) phrase

        inline const std::vector<Hit>& rhythm (MelodyStyle style)
        {
            static const std::vector<Hit> driving   { { 0, 2 }, { 2, 2 }, { 4, 2 }, { 6, 2 }, { 8, 2 }, { 10, 2 }, { 12, 4 },
                                                      { 16, 2 }, { 18, 2 }, { 20, 2 }, { 22, 2 }, { 24, 6 }, { 30, 2 } };
            static const std::vector<Hit> threeTwo  { { 0, 3 }, { 3, 3 }, { 6, 2 }, { 8, 3 }, { 11, 3 }, { 14, 2 },
                                                      { 16, 3 }, { 19, 3 }, { 22, 2 }, { 24, 8 } };
            static const std::vector<Hit> anthem    { { 0, 4 }, { 4, 2 }, { 6, 2 }, { 8, 6 }, { 14, 2 },
                                                      { 16, 4 }, { 20, 4 }, { 24, 8 } };
            static const std::vector<Hit> syncopate { { 0, 2 }, { 3, 3 }, { 6, 4 }, { 10, 2 }, { 12, 3 }, { 15, 1 },
                                                      { 16, 2 }, { 18, 1 }, { 19, 3 }, { 22, 2 }, { 24, 8 } };
            switch (style)
            {
                case MelodyStyle::Driving:       return driving;
                case MelodyStyle::ThreeThreeTwo: return threeTwo;
                case MelodyStyle::Anthem:        return anthem;
                case MelodyStyle::Syncopated:    return syncopate;
            }
            return threeTwo;
        }

        inline bool inScale (int pitch, int root, int scale)
        {
            const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, scale)].steps;
            const auto pc = ((pitch - root) % 12 + 12) % 12;
            return std::find (steps.begin(), steps.end(), pc) != steps.end();
        }

        /** Moves `steps` scale degrees from `pitch` (which should be in the scale). */
        inline int scaleStepFrom (int pitch, int steps, int root, int scale)
        {
            auto p = pitch;
            const auto dir = steps > 0 ? 1 : -1;
            for (int i = 0; i < std::abs (steps); ++i)
            {
                p += dir;
                while (! inScale (p, root, scale)) p += dir;
            }
            return p;
        }

        inline int nearestChordTone (int pitch, const std::vector<int>& pcs, int lo, int hi)
        {
            int best = pitch, bestDist = 1000;
            for (int p = lo; p <= hi; ++p)
                if (std::find (pcs.begin(), pcs.end(), ((p % 12) + 12) % 12) != pcs.end() && std::abs (p - pitch) < bestDist)
                {
                    best = p;
                    bestDist = std::abs (p - pitch);
                }
            return best;
        }
    }

    /** Chord tones (pitch classes) of the chord sounding at quarter `q`. */
    inline std::vector<int> chordTonesAt (const Settings& s, double q)
    {
        const auto count = juce::jlimit (1, maxSlots, s.count);
        const auto slot = juce::jlimit (0, count - 1, (int) std::floor (std::fmod (q, s.quartersPerChord * count) / s.quartersPerChord));
        const auto c = buildChord (s, s.slots[(size_t) slot]);
        std::vector<int> pcs;
        for (auto iv : c.intervals) pcs.push_back ((s.root + c.root + iv) % 12);
        return pcs;
    }

    /** One pass of the progression as a lead melody: a 2-bar "call" and a matching "answer"
        (same rhythm and contour, re-fitted to the chords), chord tones on the beats, steps in
        between, ending on the root of the last chord. Deterministic for a given seed. */
    inline std::vector<Note> melody (const Settings& s, const MelodySettings& m)
    {
        using namespace detail;
        std::vector<Note> out;
        const auto totalQ = s.quartersPerChord * juce::jlimit (1, maxSlots, s.count);
        const auto centre = 12 * (m.octave + 1) + s.root + 4;
        const auto lo = centre - 9, hi = centre + 10;
        const auto& hits = rhythm (m.style);
        juce::Random rng ((juce::int64) m.seed * 7919 + 13);

        // The call: a contour of scale-step moves between hits, chosen once.
        std::vector<int> contour;
        int direction = rng.nextBool() ? 1 : -1;
        for (size_t i = 0; i < hits.size(); ++i)
        {
            if (rng.nextFloat() < 0.3f) direction = -direction;
            const auto size = rng.nextFloat() < 0.75f ? 1 : 2 + rng.nextInt (2);   // mostly steps, some leaps
            contour.push_back (direction * size);
        }

        auto previous = nearestChordTone (centre, chordTonesAt (s, 0.0), lo, hi);
        for (double phrase = 0.0; phrase < totalQ - 1.0e-9; phrase += 8.0)
        {
            for (size_t i = 0; i < hits.size(); ++i)
            {
                const auto start = phrase + hits[i].step * 0.25;
                if (start >= totalQ - 1.0e-9) break;
                const auto length = std::min (hits[i].length * 0.25 * 0.9, totalQ - start);
                const auto strong = hits[i].step % 4 == 0;

                const auto firstNote = out.empty();
                auto pitch = firstNote ? previous : scaleStepFrom (previous, contour[i], s.root, s.scale);
                if (pitch < lo || pitch > hi) pitch = scaleStepFrom (previous, -contour[i], s.root, s.scale);   // bounce off the edges
                if (strong) pitch = nearestChordTone (pitch, chordTonesAt (s, start), lo, hi);

                out.push_back ({ pitch, start, length, strong ? 110 : 92 });
                previous = pitch;
            }
        }

        // Resolve: the last note lands on the root of the last chord and rings to the end.
        if (! out.empty())
        {
            auto& last = out.back();
            const auto lastChord = buildChord (s, s.slots[(size_t) juce::jlimit (0, maxSlots - 1, s.count - 1)]);
            const auto rootPc = (s.root + lastChord.root) % 12;
            last.pitch = nearestChordTone (last.pitch, { rootPc }, lo, hi);
            last.length = totalQ - last.start - 0.02;
        }
        return out;
    }

    /** The call half of every 2-bar answer pair: a sparser "teaser" for breakdowns. */
    inline std::vector<Note> teaser (const std::vector<Note>& melodyNotes)
    {
        std::vector<Note> out;
        for (const auto& n : melodyNotes)
            if (std::fmod (n.start, 8.0) < 4.0) out.push_back (n);
        return out;
    }

    //==============================================================================
    // Full-song clips: one pattern per section type, following the Ouroboros map.

    enum class SongPart { Chords, Bass, Arp, Lead };

    /** Pattern choice per section type. 0 = none, otherwise (pattern enum value + 1). */
    struct SongPatterns
    {
        std::array<int, (int) suite::SectionType::count> chords { 0, 0, 1, 4, 3, 0 };   // Intro..Outro
        std::array<int, (int) suite::SectionType::count> bass   { 1, 2, 0, 0, 2, 1 };
        std::array<int, (int) suite::SectionType::count> arp    { 0, 1, 0, 1, 1, 0 };
        std::array<int, (int) suite::SectionType::count> lead   { 0, 0, 2, 0, 1, 0 };   // 1 = melody, 2 = teaser
        MelodySettings melody;
    };

    inline const juce::StringArray& songChoiceNames (SongPart part)
    {
        static const juce::StringArray chords { "None", "Sustain", "Offbeat stabs", "Pluck 3-3-2", "16th gate" };
        static const juce::StringArray bass   { "None", "Offbeat", "Rolling", "Galloping", "Sustain" };
        static const juce::StringArray arp    { "None", "Up", "Down", "Up-down", "Bounce" };
        static const juce::StringArray lead   { "None", "Melody", "Teaser" };
        return part == SongPart::Chords ? chords : part == SongPart::Bass ? bass : part == SongPart::Arp ? arp : lead;
    }

    /** Song-length notes: each section plays its own pattern, restarting the progression at
        the section start and never spilling into the next section. Assumes 4/4 bars. */
    inline std::vector<Note> songClip (const Settings& base, const suite::SectionMap& map, const SongPatterns& patterns, SongPart part)
    {
        std::vector<Note> out;
        const auto progressionQ = base.quartersPerChord * juce::jlimit (1, maxSlots, base.count);

        for (int i = 0; i < map.count; ++i)
        {
            const auto type = (size_t) map.sections[(size_t) i].type;
            const auto choice = part == SongPart::Chords ? patterns.chords[type] : part == SongPart::Bass ? patterns.bass[type]
                              : part == SongPart::Arp ? patterns.arp[type] : patterns.lead[type];
            if (choice <= 0)
                continue;

            auto s = base;
            std::vector<Note> cycle;
            if (part == SongPart::Chords)    { s.chordPattern = (ChordPattern) (choice - 1); cycle = chordClip (s); }
            else if (part == SongPart::Bass) { s.bassPattern = (BassPattern) (choice - 1);   cycle = bassClip (s); }
            else if (part == SongPart::Arp)  { s.arpPattern = (ArpPattern) (choice - 1);     cycle = arpClip (s); }
            else                             { cycle = melody (s, patterns.melody); if (choice == 2) cycle = teaser (cycle); }

            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0;
            const auto endQ = (map.endBar (i) - 1) * 4.0;
            for (auto offset = startQ; offset < endQ - 1.0e-9; offset += progressionQ)
                for (const auto& n : cycle)
                {
                    const auto start = offset + n.start;
                    if (start >= endQ - 1.0e-9) continue;
                    out.push_back ({ n.pitch, start, std::min (n.length, endQ - start - 0.01), n.velocity });
                }
        }
        return out;
    }

    //==============================================================================
    inline juce::MidiFile toMidiFile (const std::vector<Note>& notes, double bpm, const juce::String& trackName)
    {
        constexpr int ppq = 960;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0);
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0.0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

        for (const auto& n : notes)
        {
            const auto on = n.start * ppq, off = (n.start + n.length) * ppq;
            seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), off);
        }
        seq.sort();
        seq.updateMatchedPairs();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (seq);
        return file;
    }

    //==============================================================================
    struct Preset
    {
        const char* name;
        int scale;   // -1 = keep the current scale
        int count;
        std::array<Slot, maxSlots> slots;
    };

    inline const std::vector<Preset>& presets()
    {
        using Q = Quality;
        static const std::vector<Preset> p
        {
            { "Anthem (i VI III VII)",       -1, 4, { { { 1 }, { 6 }, { 3 }, { 7 }, { 1 }, { 6 }, { 3 }, { 7 } } } },
            { "Classic Uplift (i VII VI VII)", -1, 4, { { { 1 }, { 7 }, { 6 }, { 7 }, { 1 }, { 7 }, { 6 }, { 7 } } } },
            { "Euphoria (VI VII i i)",       -1, 4, { { { 6 }, { 7 }, { 1 }, { 1 }, { 6 }, { 7 }, { 1 }, { 1 } } } },
            { "Emotional (i iv VI V)",       -1, 4, { { { 1 }, { 4 }, { 6 }, { 5, Q::Major }, { 1 }, { 4 }, { 6 }, { 5, Q::Major } } } },
            { "Heartbreak (i III VII iv)",   -1, 4, { { { 1 }, { 3 }, { 7 }, { 4 }, { 1 }, { 3 }, { 7 }, { 4 } } } },
            { "Ascension (iv VI VII i)",     -1, 4, { { { 4 }, { 6 }, { 7 }, { 1 }, { 4 }, { 6 }, { 7 }, { 1 } } } },
            { "Driving (i i VI VII)",        -1, 4, { { { 1 }, { 1 }, { 6 }, { 7 }, { 1 }, { 1 }, { 6 }, { 7 } } } },
            { "Suspended (sus & add9)",      -1, 4, { { { 1, Q::Sus2 }, { 6, Q::Add9 }, { 3 }, { 7, Q::Sus4 }, { 1 }, { 6 }, { 3 }, { 7 } } } },
            { "Epic 8 (i VI III VII i VI iv V)", -1, 8, { { { 1 }, { 6 }, { 3 }, { 7 }, { 1 }, { 6 }, { 4 }, { 5, Q::Major } } } },
            { "Major Uplift (I V vi IV)",     1, 4, { { { 1 }, { 5 }, { 6 }, { 4 }, { 1 }, { 5 }, { 6 }, { 4 } } } },
        };
        return p;
    }
}
