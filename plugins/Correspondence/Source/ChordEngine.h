#pragma once

// Pure music logic for Correspondence: chords from scale degrees, voicing with
// voice leading, rhythm patterns for chords/bass/arp, presets and MIDI export.
// No audio or UI code here so it can be unit-tested directly.

#include "DirectorBus.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "Music.h"
#include "TempoBus.h"
#include "SectionBus.h"
#include <vector>
#include <algorithm>

namespace chords
{
    constexpr int maxSlots = 8;

    enum class Quality { Auto, Major, Minor, Sus2, Sus4, Seventh, Add9, Power };
    enum class Voicing { Close, Open, Wide };
    enum class ChordPattern { Sustain, OffbeatStabs, Pluck332, Gate16 };
    enum class BassPattern { Offbeat, Rolling, Galloping, Sustain, Walking, Mine, OctaveOffbeat, Pumping };

    /** Bassline DNA: where in the bar you put bass notes (16 steps) and where you jump up an
        octave, learned from your own bass clips. */
    struct BassDna
    {
        bool valid = false;
        std::array<float, 16> onset {}, octave {};
        int clips = 0, notes = 0;
    };
    enum class ArpPattern { Up, Down, UpDown, Bounce, Broken, OctaveJumps, Gated, ThreeThreeTwo };

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
        BassDna bassDna;   // for BassPattern::Mine
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

        const auto count = juce::jlimit (1, maxSlots, s.count);
        const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        auto inKey = [&] (int p) { return std::find (steps.begin(), steps.end(), ((p - s.root) % 12 + 12) % 12) != steps.end(); };
        auto scaleStep = [&] (int p, int dir) { do p += dir; while (! inKey (p)); return p; };

        for (int i = 0; i < count; ++i)
        {
            const auto pitch = 12 * (s.bassOctave + 1) + chordRootPitchClass (s, i);
            const auto base = (double) i * L;
            auto emit = [&] (double t, double len, bool) { notes.push_back ({ pitch, base + t, len, 110 }); };

            if (s.bassPattern == BassPattern::Walking)
            {
                // Rolling 16ths, but the last two before the change walk through the scale to the next root.
                auto next = 12 * (s.bassOctave + 1) + chordRootPitchClass (s, (i + 1) % count);
                if (next - pitch > 6) next -= 12;
                if (pitch - next > 6) next += 12;
                const auto dir = next > pitch ? 1 : next < pitch ? -1 : 0;
                const auto approach = dir != 0 ? scaleStep (next, -dir) : pitch;                       // one step before the target
                const auto passing = dir != 0 && std::abs (approach - pitch) > 2 ? scaleStep (approach, -dir) : pitch;
                forEachHit (L, [&] (double t, double len, bool)
                {
                    const auto lastBeat = t >= L - 1.0 - 1.0e-9;
                    const auto step = (int) std::lround ((t - std::floor (t)) * 4.0);
                    const auto p = lastBeat && step == 3 ? approach : lastBeat && step == 2 ? passing : pitch;
                    notes.push_back ({ p, base + t, len, lastBeat && step >= 2 ? 100 : 110 });
                }, { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }, 0.2);
                continue;
            }

            if (s.bassPattern == BassPattern::Mine && s.bassDna.valid)
            {
                // Your bassline: the steps you usually play, with your octave jumps.
                std::vector<int> steps;
                for (int k = 0; k < 16; ++k) if (s.bassDna.onset[(size_t) k] >= 0.5f) steps.push_back (k);
                for (double bar = 0.0; bar < L - 1.0e-9; bar += 4.0)
                    for (size_t k = 0; k < steps.size(); ++k)
                    {
                        const auto t = bar + steps[k] * 0.25;
                        if (t >= L - 1.0e-9) continue;
                        const auto nextStep = k + 1 < steps.size() ? steps[k + 1] : steps.front() + 16;
                        const auto len = juce::jlimit (0.1, 0.9, (nextStep - steps[k]) * 0.25 * 0.85);
                        const auto up = s.bassDna.octave[(size_t) steps[k]] >= 0.5f ? 12 : 0;
                        notes.push_back ({ pitch + up, base + t, len, steps[k] % 4 == 0 ? 110 : 100 });
                    }
                continue;
            }

            switch (s.bassPattern)
            {
                case BassPattern::Mine:      // no DNA learned yet: offbeat
                case BassPattern::Offbeat:   forEachHit (L, emit, { 2, 6, 10, 14 }, 0.4); break;
                case BassPattern::Rolling:   forEachHit (L, emit, { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }, 0.2); break;
                case BassPattern::Galloping: forEachHit (L, emit, { 2, 3, 6, 7, 10, 11, 14, 15 }, 0.2); break;
                case BassPattern::Sustain:   emit (0.0, L - 0.02, true); break;
                case BassPattern::Walking:   break;   // handled above
                case BassPattern::OctaveOffbeat:
                    // Offbeats, every second one an octave up: the bounce of a lot of uplifting basslines
                    forEachHit (L, [&] (double t, double len, bool)
                    {
                        const auto up = ((int) std::lround (t * 4.0) % 8) == 6;
                        notes.push_back ({ pitch + (up ? 12 : 0), base + t, len, up ? 100 : 110 });
                    }, { 2, 6, 10, 14 }, 0.4);
                    break;
                case BassPattern::Pumping:
                    // Starts a 16th after each beat and holds to the next: leaves the kick alone without any ducking
                    forEachHit (L, emit, { 1, 5, 9, 13 }, 0.7);
                    break;
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
            const auto chordSize = (int) voiced[i].size();
            switch (s.arpPattern)
            {
                case ArpPattern::Up:
                case ArpPattern::Gated:
                case ArpPattern::ThreeThreeTwo: for (int k = 0; k < n; ++k) order.push_back (k); break;
                case ArpPattern::Down:   for (int k = n - 1; k >= 0; --k) order.push_back (k); break;
                case ArpPattern::UpDown:
                    for (int k = 0; k < n; ++k) order.push_back (k);
                    for (int k = n - 2; k > 0; --k) order.push_back (k);
                    break;
                case ArpPattern::Bounce: for (int k = 1; k < n; ++k) { order.push_back (0); order.push_back (k); } break;
                case ArpPattern::Broken:   // 1-3-2-4: skip up a note, step back
                    for (int k = 0; k + 2 < n; ++k) { order.push_back (k); order.push_back (k + 2); }
                    if (order.empty()) for (int k = 0; k < n; ++k) order.push_back (k);
                    break;
                case ArpPattern::OctaveJumps:   // each chord tone, then the same an octave up
                    for (int k = 0; k < chordSize; ++k) { order.push_back (k); order.push_back (k + chordSize < n ? k + chordSize : k); }
                    break;
            }
            if (order.empty()) order.push_back (0);

            // Rhythm: 16ths, a gated "x x - x" 16th pattern, or dotted 8ths in 3-3-2
            const auto steps = (int) std::round (L * 4.0);
            int played = 0;
            for (int k = 0; k < steps; ++k)
            {
                auto len = 0.2;
                auto vel = k % 4 == 0 ? 110 : 88;
                if (s.arpPattern == ArpPattern::Gated && k % 4 == 2) continue;
                if (s.arpPattern == ArpPattern::ThreeThreeTwo)
                {
                    const auto inEight = k % 8;
                    if (inEight != 0 && inEight != 3 && inEight != 6) continue;
                    len = inEight == 6 ? 0.45 : 0.7;
                    vel = inEight == 0 ? 115 : 100;
                }
                notes.push_back ({ tones[(size_t) order[(size_t) played++ % order.size()]], (double) i * L + k * 0.25, len, vel });
            }
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

    /** Melodic DNA: how your leads tend to move, learned from several MIDI clips. */
    struct MelodyDna
    {
        bool valid = false;
        std::array<float, 32> onset {};   // chance of a note starting on each 16th of a 2-bar phrase
        std::array<float, 9> steps {};    // how often the next note moves -4..+4 scale steps
        int clips = 0, notes = 0;
    };

    struct MelodySettings
    {
        MelodyStyle style = MelodyStyle::ThreeThreeTwo;
        int seed = 7;
        int octave = 5;   // the melody sits around this octave
        std::vector<Note> motif;   // a learned 2-bar motif (from a dropped MIDI clip); empty = generate
        MelodyDna dna;             // learned style (from several clips): used when there's no motif
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

        /** Signed number of scale steps from a to b. */
        inline int degreesBetween (int a, int b, int root, int scale)
        {
            int n = 0;
            if (b > a) { for (int p = a + 1; p <= b; ++p) if (inScale (p, root, scale)) ++n; }
            else       { for (int p = a - 1; p >= b; --p) if (inScale (p, root, scale)) --n; }
            return n;
        }

        inline int snapToScale (int pitch, int root, int scale)
        {
            for (int d = 0; d < 6; ++d)
            {
                if (inScale (pitch - d, root, scale)) return pitch - d;
                if (inScale (pitch + d, root, scale)) return pitch + d;
            }
            return pitch;
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
        std::vector<Hit> hits;
        std::vector<int> contour, motifPitches;

        if (! m.motif.empty())
        {
            // Learned motif: its rhythm, its notes (fitted to the key and register) and its shape.
            double mean = 0.0;
            for (const auto& n : m.motif) mean += n.pitch;
            mean /= (double) m.motif.size();
            const auto shift = 12 * (int) std::round ((centre - mean) / 12.0);
            for (const auto& n : m.motif)
            {
                const auto step = (int) std::round (n.start * 4.0);
                if (step >= 32) continue;
                hits.push_back ({ step, std::max (1, (int) std::round (n.length * 4.0)) });
                motifPitches.push_back (snapToScale (n.pitch + shift, s.root, s.scale));
            }
            for (size_t i = 0; i < motifPitches.size(); ++i)
                contour.push_back (i == 0 ? 0 : degreesBetween (motifPitches[i - 1], motifPitches[i], s.root, s.scale));
        }
        else if (m.dna.valid)
        {
            // Your melodic DNA: the rhythm positions you use most, and steps drawn from how you move.
            juce::Random rng ((juce::int64) m.seed * 7919 + 13);
            std::vector<int> starts;
            for (int st = 0; st < 32; ++st) if (m.dna.onset[(size_t) st] >= 0.4f) starts.push_back (st);
            if (starts.size() < 4)
            {
                std::vector<int> order (32);
                for (int st = 0; st < 32; ++st) order[(size_t) st] = st;
                std::sort (order.begin(), order.end(), [&m] (int a, int b) { return m.dna.onset[(size_t) a] > m.dna.onset[(size_t) b]; });
                starts.assign (order.begin(), order.begin() + 6);
                std::sort (starts.begin(), starts.end());
            }
            for (size_t k = 0; k < starts.size(); ++k)
            {
                const auto next = k + 1 < starts.size() ? starts[k + 1] : 32;
                hits.push_back ({ starts[k], std::max (1, next - starts[k]) });
            }
            float total = 0.0f;
            for (auto v : m.dna.steps) total += v;
            for (size_t i = 0; i < hits.size(); ++i)
            {
                auto pick = rng.nextFloat() * std::max (1.0e-6f, total);
                int step = 1;
                for (int k = 0; k < 9; ++k) { pick -= m.dna.steps[(size_t) k]; if (pick <= 0.0f) { step = k - 4; break; } }
                contour.push_back (step);
            }
        }
        else
        {
            hits = rhythm (m.style);
            juce::Random rng ((juce::int64) m.seed * 7919 + 13);

            // The call: a contour of scale-step moves between hits, chosen once.
            int direction = rng.nextBool() ? 1 : -1;
            for (size_t i = 0; i < hits.size(); ++i)
            {
                if (rng.nextFloat() < 0.3f) direction = -direction;
                const auto size = rng.nextFloat() < 0.75f ? 1 : 2 + rng.nextInt (2);   // mostly steps, some leaps
                contour.push_back (direction * size);
            }
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

                int pitch;
                if (! motifPitches.empty() && phrase < 1.0e-9)
                {
                    pitch = motifPitches[i];   // the call is your motif as played
                }
                else if (! motifPitches.empty() && i == 0)
                {
                    pitch = nearestChordTone (motifPitches[0], chordTonesAt (s, start), lo - 3, hi + 3);   // answers start from a chord tone
                }
                else
                {
                    const auto firstNote = out.empty();
                    pitch = firstNote ? previous : scaleStepFrom (previous, contour[i], s.root, s.scale);
                    if (pitch < lo || pitch > hi) pitch = scaleStepFrom (previous, -contour[i], s.root, s.scale);   // bounce off the edges
                    if (strong) pitch = nearestChordTone (pitch, chordTonesAt (s, start), lo, hi);
                }

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

    /** Breakdown hook hint: the hook's opening, twice as slow and softer, so the breakdown
        sings what the drop will play without giving it away. */
    inline std::vector<Note> hookHint (const std::vector<Note>& melodyNotes, double cycleQ)
    {
        std::vector<Note> out;
        for (const auto& n : melodyNotes)
            if (n.start * 2.0 < cycleQ - 1.0e-9)
                out.push_back ({ n.pitch, n.start * 2.0, std::min (n.length * 2.0, cycleQ - n.start * 2.0), juce::jmax (1, n.velocity - 14) });
        return out;
    }

    /** Call and response: the melody's first half is the call; the second half answers with the
        same rhythm, moved with the chords (a sequence), ending on the chord's root. */
    inline std::vector<Note> callResponse (const std::vector<Note>& mel, const Settings& s)
    {
        const auto count = juce::jlimit (1, maxSlots, s.count);
        const auto L = s.quartersPerChord;
        const auto half = L * count / 2.0;
        if (count < 2 || mel.empty()) return mel;
        const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        auto inKey = [&] (int p) { return std::find (steps.begin(), steps.end(), ((p - s.root) % 12 + 12) % 12) != steps.end(); };
        std::vector<Note> out;
        std::vector<Note> call;
        for (const auto& n : mel) if (n.start < half - 1.0e-9) { out.push_back (n); call.push_back (n); }
        for (size_t k = 0; k < call.size(); ++k)
        {
            auto n = call[k];
            const auto slot = (int) std::floor (n.start / L + 1.0e-9);
            const auto answerSlot = juce::jlimit (0, count - 1, slot + count / 2);
            auto shift = chordRootPitchClass (s, answerSlot) - chordRootPitchClass (s, slot);
            if (shift > 6) shift -= 12;
            if (shift < -6) shift += 12;
            n.pitch += shift;
            while (! inKey (n.pitch)) --n.pitch;
            n.start += half;
            if (k + 1 == call.size())
            {
                // The answer ends home: the nearest root of its chord
                const auto root = chordRootPitchClass (s, answerSlot);
                auto best = n.pitch;
                for (int d = 0; d <= 6; ++d)
                {
                    if (((n.pitch - d) % 12 + 12) % 12 == root) { best = n.pitch - d; break; }
                    if (((n.pitch + d) % 12 + 12) % 12 == root) { best = n.pitch + d; break; }
                }
                n.pitch = best;
            }
            out.push_back (n);
        }
        return out;
    }

    /** Pushed: notes on the 8th-note grid (except each bar's downbeat) come a 16th early
        (anticipations), for a later drop that drives harder with the same tune. */
    inline std::vector<Note> pushed (std::vector<Note> mel)
    {
        for (auto& n : mel)
        {
            const auto inBar = std::fmod (n.start, 4.0);
            const auto eighths = inBar * 2.0;
            if (inBar > 1.0e-6 && std::abs (eighths - std::round (eighths)) < 1.0e-6 && n.start >= 0.25)
            {
                n.start -= 0.25;
                n.length += 0.25;
            }
        }
        return mel;
    }

    //==============================================================================
    // Full-song clips: one pattern per section type, following the Ouroboros map.

    /** Later drops: the same melody, embellished. A scale passing note fills each leap of a
        third or more across a dotted 8th or longer, and the last phrase's first strong-beat note
        jumps up an octave for a climax. */
    inline std::vector<Note> embellish (const std::vector<Note>& mel, const Settings& s)
    {
        using namespace detail;
        std::vector<Note> out;
        if (mel.empty()) return out;
        double totalQ = 0.0;
        for (const auto& n : mel) totalQ = std::max (totalQ, n.start + n.length);

        for (size_t i = 0; i < mel.size(); ++i)
        {
            out.push_back (mel[i]);
            if (i + 1 >= mel.size()) break;
            const auto& n = mel[i];
            const auto& next = mel[i + 1];
            const auto gap = next.start - n.start;
            const auto leap = next.pitch - n.pitch;
            if (gap >= 0.75 - 1.0e-9 && std::abs (leap) >= 3 && std::abs (leap) <= 9)
            {
                const auto half = n.start + std::round (gap * 0.5 / 0.25) * 0.25;   // on the 16th grid
                out.back().length = std::min (out.back().length, half - n.start - 0.02);
                out.push_back ({ scaleStepFrom (next.pitch, leap > 0 ? -1 : 1, s.root, s.scale), half,
                                 std::max (0.1, next.start - half - 0.02), 88 });
            }
        }

        // Climax: the first on-the-beat note of the last phrase (after its first bar), an octave up.
        const auto phraseStart = std::max (0.0, totalQ - 8.0) + 4.0;
        for (auto& n : out)
            if (n.start >= phraseStart - 1.0e-9 && std::abs (std::fmod (n.start, 1.0)) < 1.0e-9)
            {
                n.pitch += 12;
                n.velocity = std::min (127, n.velocity + 10);
                break;
            }
        return out;
    }

    /** A harmony line: each melody note a diatonic third below (in key), a little softer. */
    inline std::vector<Note> harmony (const std::vector<Note>& mel, const Settings& s)
    {
        std::vector<Note> out;
        for (auto n : mel)
        {
            n.pitch = detail::scaleStepFrom (n.pitch, -2, s.root, s.scale);
            n.velocity = std::max (1, n.velocity - 15);
            out.push_back (n);
        }
        return out;
    }

    /** Counter-melody: short answers wherever the lead holds or rests for a beat or more (its
        next note is at least a beat away), starting an 8th after the lead note, a fifth or so
        below it. Each answer moves against the lead's last direction, puts chord
        tones on the beat, and steps towards the lead's next note. Velocity 84 (softer). */
    inline std::vector<Note> counterMelody (const std::vector<Note>& mel, const Settings& s)
    {
        using namespace detail;
        std::vector<Note> out;
        if (mel.size() < 2) return out;
        double totalQ = 0.0;
        for (const auto& n : mel) totalQ = std::max (totalQ, n.start + n.length);

        for (size_t i = 0; i < mel.size(); ++i)
        {
            const auto& n = mel[i];
            const auto gapStart = std::floor (n.start * 2.0 + 1.0e-9) / 2.0 + 0.5;   // the 8th after the lead note starts
            const auto gapEnd = i + 1 < mel.size() ? mel[i + 1].start : totalQ;
            const auto count = std::min (4, (int) std::floor ((gapEnd - gapStart) / 0.5 + 1.0e-9));
            if (count < 1 || gapEnd - n.start < 1.0 - 1.0e-9) continue;

            // Contrary motion: the lead went up into this note, so the answer comes down (and vice versa).
            const auto leadDir = i > 0 ? (n.pitch > mel[i - 1].pitch ? 1 : n.pitch < mel[i - 1].pitch ? -1 : 0) : 1;
            const auto dir = leadDir >= 0 ? -1 : 1;
            const auto lo = n.pitch - 14, hi = n.pitch - 2;
            auto pitch = nearestChordTone (n.pitch - 7, chordTonesAt (s, gapStart), lo, hi);
            for (int k = 0; k < count; ++k)
            {
                const auto start = gapStart + 0.5 * k;
                if (k > 0)
                {
                    pitch = scaleStepFrom (pitch, dir, s.root, s.scale);
                    if (pitch < lo || pitch > hi) pitch = scaleStepFrom (pitch, -2 * dir, s.root, s.scale);
                }
                if (std::abs (std::fmod (start, 1.0)) < 1.0e-9)
                    pitch = nearestChordTone (pitch, chordTonesAt (s, start), lo, hi);
                const auto last = k == count - 1;
                const auto length = last ? std::min (0.5, gapEnd - start) * 0.9 : 0.45;
                out.push_back ({ pitch, start, length, 84 });
            }
        }
        return out;
    }

    enum class SongPart { Chords, Bass, Arp, Lead, Harmony, Counter,
                          Piano, Strings, Brass, Choir, Timpani, Acid, Chops, Candy };   // the pattern library's layers

    //==============================================================================
    /** Learns bassline DNA from bass clips (each rebased to the bar of its first note). */
    inline BassDna learnBassDna (const std::vector<std::vector<Note>>& clips)
    {
        BassDna d;
        int bars = 0;
        for (const auto& clip : clips)
        {
            if (clip.size() < 3) continue;
            int lowest = 127;
            for (const auto& n : clip) lowest = std::min (lowest, n.pitch);
            const auto first = std::floor (std::min_element (clip.begin(), clip.end(), [] (const Note& a, const Note& b) { return a.start < b.start; })->start / 4.0) * 4.0;
            double last = first;
            for (const auto& n : clip)
            {
                const auto t = n.start - first;
                const auto step = (int) std::lround (std::fmod (t, 4.0) * 4.0) % 16;
                d.onset[(size_t) step] += 1.0f;
                if (n.pitch >= lowest + 10) d.octave[(size_t) step] += 1.0f;
                last = std::max (last, n.start);
                ++d.notes;
            }
            bars += std::max (1, (int) std::ceil ((last - first + 1.0e-6) / 4.0));
            ++d.clips;
        }
        if (d.notes < 4) return {};
        for (size_t k = 0; k < 16; ++k)
        {
            d.octave[k] = d.onset[k] > 0.0f ? d.octave[k] / d.onset[k] : 0.0f;
            d.onset[k] /= (float) std::max (1, bars);
        }
        d.valid = true;
        return d;
    }

    /** True when a clip is a bassline: one note at a time, low (median below E3). */
    inline bool looksLikeBass (const std::vector<Note>& notes)
    {
        if (notes.size() < 3) return false;
        std::vector<int> pitches;
        for (const auto& n : notes) pitches.push_back (n.pitch);
        std::nth_element (pitches.begin(), pitches.begin() + (long) pitches.size() / 2, pitches.end());
        if (pitches[pitches.size() / 2] >= 52) return false;
        int together = 0;
        for (size_t i = 0; i < notes.size(); ++i)
            for (size_t k = i + 1; k < notes.size(); ++k)
                together += std::abs (notes[i].start - notes[k].start) < 0.06;
        return together * 4 < (int) notes.size();
    }

    /** How you voice chords, from a chord clip: the voicing that matches your spread, your
        register and whether you put the root underneath. */
    struct VoicingGuess { bool valid = false; Voicing voicing = Voicing::Open; int registerOctave = 4; bool addBass = false; float spread = 0.0f; };

    inline VoicingGuess voicingFromNotes (const std::vector<Note>& notes)
    {
        VoicingGuess g;
        std::vector<std::vector<int>> groups;
        std::vector<double> starts;
        for (const auto& n : notes)
        {
            size_t k = 0;
            while (k < starts.size() && std::abs (starts[k] - n.start) >= 0.06) ++k;
            if (k == starts.size()) { starts.push_back (n.start); groups.push_back ({}); }
            groups[k].push_back (n.pitch);
        }
        float spread = 0.0f, centre = 0.0f;
        int counted = 0, bassUnder = 0;
        for (auto& gr : groups)
        {
            if (gr.size() < 3) continue;
            std::sort (gr.begin(), gr.end());
            spread += (float) (gr.back() - gr.front());
            float sum = 0.0f;
            for (auto p : gr) sum += (float) p;
            centre += sum / (float) gr.size();
            if (gr[1] - gr[0] >= 10) ++bassUnder;   // a low root well under the rest
            ++counted;
        }
        if (counted == 0) return g;
        g.spread = spread / (float) counted;
        g.addBass = bassUnder * 2 > counted;
        const auto upper = g.addBass ? g.spread - 12.0f : g.spread;
        g.voicing = upper <= 12.5f ? Voicing::Close : upper <= 19.5f ? Voicing::Open : Voicing::Wide;
        g.registerOctave = juce::jlimit (3, 5, (int) std::floor ((centre / (float) counted) / 12.0f) - 1);
        g.valid = true;
        return g;
    }

    //==============================================================================
    /** Later drops climb: each chord gains its top note an octave higher, wider and brighter. */
    inline std::vector<Note> climbVoicing (const std::vector<Note>& notes)
    {
        auto out = notes;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            bool top = true;
            for (const auto& o : notes)
                if (std::abs (o.start - notes[i].start) < 1.0e-6 && o.pitch > notes[i].pitch) { top = false; break; }
            if (top) out.push_back ({ notes[i].pitch + 12, notes[i].start, notes[i].length, juce::jmax (1, notes[i].velocity - 8) });
        }
        return out;
    }

    /** Ghost notes: soft in-between 16ths (the last pitch again) where a part leaves a gap,
        more of them the higher the section's energy. Deterministic. */
    inline std::vector<Note> addGhostNotes (std::vector<Note> notes, double startQ, double endQ, float amount, int seed)
    {
        if (amount <= 0.0f || notes.empty()) return notes;
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        std::vector<Note> ghosts;
        size_t k = 0;
        for (auto t = std::ceil (startQ * 4.0 - 1.0e-9) / 4.0; t < endQ - 0.25 + 1.0e-9; t += 0.25)
        {
            while (k < notes.size() && notes[k].start < t - 1.0e-6) ++k;
            if (k < notes.size() && std::abs (notes[k].start - t) < 1.0e-6) continue;   // a note already here
            if (k == 0) continue;
            auto& prev = notes[k - 1];
            if (t - prev.start > 0.5 + 1.0e-6 || t - prev.start < 0.2) continue;   // only just after a note
            const auto onBeat = std::abs (t - std::round (t)) < 1.0e-6;
            if (onBeat) continue;
            const auto h = (uint32_t) ((int64_t) std::lround (t * 4.0) * 2654435761u + (uint32_t) seed * 40503u);
            if ((float) (h % 1000u) / 1000.0f < amount)
            {
                prev.length = std::min (prev.length, t - prev.start - 0.02);   // the ghost cuts the note before it short
                ghosts.push_back ({ prev.pitch, t, 0.1, 38 });
            }
        }
        notes.insert (notes.end(), ghosts.begin(), ghosts.end());
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return notes;
    }

    /** Breakdown piano: in every breakdown, each chord as a low root plus its chord tones
        broken upwards in 8ths and held, with the sustain pedal (CC 64) down for each chord.
        `pedal` receives (time, down) pairs. */
    inline std::vector<Note> pianoClip (const Settings& base, const suite::SectionMap& map, std::vector<std::pair<double, bool>>& pedal)
    {
        std::vector<Note> out;
        auto s = base;
        s.voicing = Voicing::Close;
        s.addBass = false;
        s.registerOctave = 4;
        const auto voiced = voiceProgression (s);
        const auto L = s.quartersPerChord;
        const auto progressionQ = L * juce::jlimit (1, maxSlots, s.count);
        for (int i = 0; i < map.count; ++i)
        {
            if (map.sections[(size_t) i].type != suite::SectionType::Breakdown) continue;
            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
            const auto shift = map.keyShiftOf (i);
            for (auto offset = startQ; offset < endQ - 1.0e-9; offset += progressionQ)
                for (size_t c = 0; c < voiced.size(); ++c)
                {
                    const auto t = offset + (double) c * L;
                    if (t >= endQ - 1.0e-9) break;
                    const auto len = std::min (L, endQ - t);
                    out.push_back ({ 12 * 3 + chordRootPitchClass (s, (int) c) + shift, t, len - 0.05, 80 });   // the root, low
                    auto tones = voiced[c];
                    tones.push_back (tones.front() + 12);
                    for (size_t k = 0; k < tones.size(); ++k)
                    {
                        const auto at = t + 0.5 * (double) k;
                        if (at >= t + len - 0.1) break;
                        out.push_back ({ tones[k] + shift, at, t + len - at - 0.05, 72 - (int) k * 2 });
                    }
                    pedal.push_back ({ t + 0.02, true });
                    pedal.push_back ({ t + len - 0.06, false });
                }
        }
        return out;
    }

    /** Drum fills (General MIDI: 38 snare, 43/45/48/50 toms, 49 crash) for every section change
        and, smaller, the end of each 8-bar phrase inside a section. */
    inline std::vector<Note> fillsClip (const suite::SectionMap& map)
    {
        using T = suite::SectionType;
        std::vector<Note> out;
        for (int i = 0; i < map.count; ++i)
        {
            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
            const auto last = i + 1 >= map.count;
            // Small fills at phrase ends inside the section (drops and grooves).
            const auto type = map.sections[(size_t) i].type;
            if (type == T::Drop || type == T::Groove)
                for (auto phraseEnd = startQ + 32.0; phraseEnd < endQ - 1.0e-9; phraseEnd += 32.0)
                    for (auto t : { phraseEnd - 0.5, phraseEnd - 0.25 })
                        out.push_back ({ 38, t, 0.2, 90 });
            if (last) continue;
            const auto next = map.sections[(size_t) i + 1].type;
            const auto barStart = endQ - 4.0;
            if (next == T::Drop)
            {
                // A roll that speeds up through the last bar: 8ths, 16ths, then 32nds.
                for (auto t = barStart; t < barStart + 2.0 - 1.0e-9; t += 0.5) out.push_back ({ 38, t, 0.2, 80 });
                for (auto t = barStart + 2.0; t < barStart + 3.0 - 1.0e-9; t += 0.25) out.push_back ({ 38, t, 0.15, 95 });
                for (auto t = barStart + 3.0; t < endQ - 1.0e-9; t += 0.125) out.push_back ({ 38, t, 0.1, (int) (100 + 20 * (t - barStart - 3.0)) });
            }
            else if (next == T::Breakdown)
            {
                const int toms[] = { 50, 50, 48, 48, 45, 45, 43, 43 };
                for (int k = 0; k < 8; ++k) out.push_back ({ toms[k], endQ - 2.0 + k * 0.25, 0.2, 100 });
            }
            else
                for (int k = 0; k < 4; ++k) out.push_back ({ 38, endQ - 1.0 + k * 0.25, 0.15, 90 + 5 * k });
            out.push_back ({ 49, endQ, 1.0, 120 });   // crash into the next section
        }
        std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return out;
    }

    //==============================================================================
    /** Learns melodic DNA from several clips (each rebased to the bar of its first note). */
    inline MelodyDna learnDna (const std::vector<std::vector<Note>>& clips, int root, int scale)
    {
        MelodyDna d;
        int windows = 0;
        for (const auto& clip : clips)
        {
            if (clip.size() < 3) continue;
            auto notes = clip;
            std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            const auto barStart = std::floor (notes.front().start / 4.0) * 4.0;
            double last = 0.0;
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const auto t = notes[i].start - barStart;
                last = std::max (last, t);
                const auto step = (int) std::lround (std::fmod (t, 8.0) * 4.0) % 32;
                d.onset[(size_t) step] += 1.0f;
                if (i > 0)
                {
                    const auto moved = juce::jlimit (-4, 4, detail::degreesBetween (notes[i - 1].pitch, notes[i].pitch, root, scale));
                    d.steps[(size_t) (moved + 4)] += 1.0f;
                }
                ++d.notes;
            }
            windows += std::max (1, (int) std::ceil ((last + 1.0e-6) / 8.0));
            ++d.clips;
        }
        if (d.notes < 6) return {};
        for (auto& o : d.onset) o /= (float) std::max (1, windows);
        d.valid = true;
        return d;
    }

    //==============================================================================
    /** A chord progression read from MIDI: the key, chords as degrees (with qualities where
        they're not the key's own), and how long each chord lasts. */
    struct ProgressionGuess
    {
        bool valid = false;
        int root = 9, scale = 0, count = 0;
        std::array<Slot, maxSlots> slots {};
        double quartersPerChord = 4.0;
    };

    inline ProgressionGuess progressionFromNotes (const std::vector<Note>& notes)
    {
        ProgressionGuess g;
        // Chord onsets: three or more notes starting together.
        std::vector<double> onsets;
        for (const auto& n : notes)
        {
            int together = 0;
            for (const auto& o : notes) together += std::abs (o.start - n.start) < 0.06;
            if (together >= 3 && (onsets.empty() || std::none_of (onsets.begin(), onsets.end(), [&n] (double t) { return std::abs (t - n.start) < 0.06; })))
                onsets.push_back (n.start);
        }
        std::sort (onsets.begin(), onsets.end());
        if (onsets.size() < 2) return g;

        std::vector<std::array<float, 12>> chroma;
        std::array<float, 12> total {};
        std::vector<double> lengths;
        for (size_t i = 0; i < onsets.size() && (int) chroma.size() < maxSlots; ++i)
        {
            std::array<float, 12> c {};
            for (const auto& n : notes)
                if (n.start < onsets[i] + 0.06 && n.start + n.length > onsets[i] + 0.06)
                    c[(size_t) (((n.pitch % 12) + 12) % 12)] += 1.0f;
            chroma.push_back (c);
            for (int k = 0; k < 12; ++k) total[(size_t) k] += c[(size_t) k];
            lengths.push_back (i + 1 < onsets.size() ? onsets[i + 1] - onsets[i] : 4.0);
        }

        // Key: the major or minor scale that covers the notes best (minor wins ties, trance-style),
        // with a bonus when the first chord is its tonic.
        float best = -1.0f;
        for (int scale : { 0, 1 })
            for (int root = 0; root < 12; ++root)
            {
                const auto& steps = suite::music::scales()[(size_t) scale].steps;
                float score = 0.0f;
                for (int k = 0; k < 12; ++k)
                    if (std::find (steps.begin(), steps.end(), ((k - root) % 12 + 12) % 12) != steps.end()) score += total[(size_t) k];
                if (chroma[0][(size_t) root] > 0.0f) score += 0.5f;
                if (scale == 0) score += 0.01f;
                if (score > best) { best = score; g.root = root; g.scale = scale; }
            }

        // Each chord: the triad root that explains it best, as a degree of the key.
        const auto& steps = suite::music::scales()[(size_t) g.scale].steps;
        for (size_t i = 0; i < chroma.size(); ++i)
        {
            const auto& c = chroma[i];
            int bestRoot = 0, bestThird = 4;
            float bestFit = -1.0f;
            for (int r = 0; r < 12; ++r)
                for (int third : { 3, 4 })
                {
                    const auto fit = c[(size_t) r] * 1.2f + c[(size_t) ((r + third) % 12)] + c[(size_t) ((r + 7) % 12)];
                    if (fit > bestFit) { bestFit = fit; bestRoot = r; bestThird = third; }
                }
            const auto rel = ((bestRoot - g.root) % 12 + 12) % 12;
            int degree = 1;
            for (int d = 0; d < 7; ++d) if (steps[(size_t) d] == rel) degree = d + 1;
            // The key's own triad on that degree: a minor third when the scale's 3rd above is 3 semitones.
            const auto diatonicThird = ((steps[(size_t) ((degree + 1) % 7)] - steps[(size_t) (degree - 1)]) % 12 + 12) % 12;
            Slot slot { degree, Quality::Auto };
            if (diatonicThird != bestThird) slot.quality = bestThird == 4 ? Quality::Major : Quality::Minor;
            g.slots[i] = slot;
        }
        g.count = (int) chroma.size();
        std::sort (lengths.begin(), lengths.end());
        const auto typical = lengths[lengths.size() / 2];
        g.quartersPerChord = typical < 3.0 ? 2.0 : typical < 6.0 ? 4.0 : 8.0;
        g.valid = true;
        return g;
    }

    /** True when a clip looks like chords (most onsets have three or more notes) rather than a melody. */
    inline bool looksLikeChords (const std::vector<Note>& notes)
    {
        int chordal = 0, onsets = 0;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            bool first = true;
            for (size_t k = 0; k < i; ++k) if (std::abs (notes[k].start - notes[i].start) < 0.06) first = false;
            if (! first) continue;
            ++onsets;
            int together = 0;
            for (const auto& o : notes) together += std::abs (o.start - notes[i].start) < 0.06;
            chordal += together >= 3;
        }
        return onsets > 0 && chordal * 2 >= onsets;
    }

}
#include "KitBus.h"
#include "Writing.h"
#include "PatternRender.h"
namespace chords
{
    //==============================================================================
    /** Groove: delays the off 16ths by `swing` of a 16th (0 = straight). */
    inline std::vector<Note> applyGroove (std::vector<Note> notes, float swing)
    {
        if (swing <= 0.0f) return notes;
        for (auto& n : notes)
        {
            const auto inBeat = n.start - std::floor (n.start + 1.0e-9);
            const auto sixteenth = (int) std::lround (inBeat * 4.0);
            if (std::abs (inBeat * 4.0 - sixteenth) < 1.0e-3 && (sixteenth == 1 || sixteenth == 3))
                n.start += swing * 0.25;
        }
        return notes;
    }

    /** Pattern choice per section type. 0 = none, otherwise (pattern enum value + 1). */
    struct SongPatterns
    {
        std::array<int, (int) suite::SectionType::count> chords { 0, 0, 1, 4, 3, 0 };   // Intro..Outro
        std::array<int, (int) suite::SectionType::count> bass   { 1, 2, 0, 0, 2, 1 };
        std::array<int, (int) suite::SectionType::count> arp    { 0, 1, 0, 1, 1, 0 };
        std::array<int, (int) suite::SectionType::count> lead   { 0, 0, 2, 0, 1, 0 };   // 1 = melody, 2 = teaser
        MelodySettings melody;
        bool liftLaterDrops = true;   // the lead goes up an octave in the 2nd, 3rd... drop
        bool varyLaterDrops = true;   // ... and is embellished, with a harmony line (SongPart::Harmony)
        int breakdownStart = 1;       // breakdowns: 0 = same progression, 1 = start on VI, 2 = start on IV, 3 = lift to III halfway
        bool answerVocal = true;      // where there's no lead, short lead answers in the bar after each vocal phrase
        bool counter = true;          // a counter-melody wherever the full lead plays (SongPart::Counter)
        bool phraseVariation = true;  // the last bar of each 8-bar phrase (inside a section) turns around: bass bounces octaves, arp lifts
        bool climbLaterDrops = true;  // drop 2+ chords gain their top note an octave up
        bool doubleFinalDrop = true;  // the last of several drops: the lead doubled an octave up, the bass a sub octave down
        float ghostNotes = 0.35f;     // in-between soft notes in bass and arp (0 = none), scaled by section energy
        bool leaveRoomForVocal = true;
        std::vector<bool> vocalBars;  // bars (index 0 = bar 1) the vocal sings in, from Solve et Coagula
        // The pattern library: each part plays one family for the whole song, chosen by mood,
        // style and seed, at the energy each section asks for, with a fill at each phrase end
        bool library = false;
        int libMood = 0, libStyle = 0;
        uint32_t libSeed = 1;
        bool libLead = true;          // the lead hook from the library too (a learned motif or style always wins)
        bool libKits = true;          // a kit (families chosen to go together) where one fits the mood and style
        uint32_t libLayers = 0xffffffffu;   // which layers play (bit per SongPart from Piano on)
        // The autopilot's arrangement group: too many parts at once thins the least important layers
        // for that section; chops and candy step back while the hook is moving
        bool manageDensity = true;
        bool takeTurns = true;
        writing::Options write;       // the Writing menu (each group on or off)
    };

    /** Where a library layer plays in section i before the density manager (kind, or -1). */
    inline int layerKindAt (SongPart part, const suite::SectionMap& map, const SongPatterns& patterns, int i)
    {
        if (! lib::isLayerPart (part) || ! patterns.library || ((patterns.libLayers >> ((int) part - (int) SongPart::Piano)) & 1u) == 0) return -1;
        int before = 0;
        for (int k = 0; k < i; ++k) before += map.sections[(size_t) k].type == map.sections[(size_t) i].type ? 1 : 0;
        const auto isDrop = map.sections[(size_t) i].type == suite::SectionType::Drop;
        const auto style = (patterns::Style) juce::jlimit (0, 6, patterns.libStyle);
        auto kind = lib::placement (part, map.sections[(size_t) i].type, isDrop && before > 0, isDrop && i == map.lastDropIndex(), style);
        if (kind >= 0 && lib::styleGated (part)
            && ! patterns::pick (lib::libraryPart (part), kind, (patterns::Mood) juce::jlimit (0, 6, patterns.libMood), style, patterns.libSeed).tagged (style))
            kind = -1;   // this style doesn't use it
        return kind;
    }

    /** The density manager: the layers that sit out section i because too many parts would play at
        once. Each section type has a budget of parts (the core parts count first), and the layers
        that matter least there go first: in a breakdown the piano, strings and choir stay, in a build
        the timpani and strings, in a drop the strings, acid and brass. */
    inline uint32_t thinnedLayers (const suite::SectionMap& map, const SongPatterns& patterns, int i)
    {
        if (! patterns.manageDensity || ! patterns.library || i < 0 || i >= map.count) return 0;
        using ST = suite::SectionType;
        const auto type = map.sections[(size_t) i].type;
        const auto t = (size_t) type;
        int core = (patterns.chords[t] > 0) + (patterns.bass[t] > 0) + (patterns.arp[t] > 0) + (patterns.lead[t] > 0);
        if (patterns.lead[t] == 1 && patterns.counter) ++core;
        auto budget = type == ST::Drop ? 8 : type == ST::Breakdown || type == ST::Build ? 6 : 5;
        // 45: tutti and thinning: the first of several drops plays fewer layers, the final drop all
        // of them, a breakdown a few (the kit's extra layers sit out first)
        uint32_t kitExtras = 0;
        if (patterns.write.arrange)
        {
            int drops = 0, ordinal = 0;
            for (int k = 0; k < map.count; ++k) if (map.sections[(size_t) k].type == ST::Drop) { if (k < i) ++ordinal; ++drops; }
            if (type == ST::Drop && drops > 1) budget = ordinal == 0 ? budget - 2 : i == map.lastDropIndex() ? 99 : budget;
            if (type == ST::Breakdown) budget = core + 3;
            if (patterns.libKits)
                if (const auto* kit = patterns::pickKit ((patterns::Mood) juce::jlimit (0, 6, patterns.libMood), (patterns::Style) juce::jlimit (0, 6, patterns.libStyle), patterns.libSeed))
                    kitExtras = kit->extras;
        }
        auto priority = [type, kitExtras] (SongPart p)
        {
            //                              Piano Strings Brass Choir Timpani Acid Chops Candy
            static const int breakdown[] { 9,    8,      5,    7,    2,      1,   2,    3 };
            static const int build[]     { 3,    7,      3,    3,    8,      5,   3,    3 };
            static const int drop[]      { 4,    7,      6,    5,    5,      7,   3,    2 };
            static const int other[]     { 6,    5,      3,    4,    3,      5,   3,    2 };
            const auto k = (size_t) ((int) p - (int) SongPart::Piano);
            const auto extra = ((kitExtras >> (int) lib::libraryPart (p)) & 1u) != 0;
            return (type == ST::Breakdown ? breakdown[k] : type == ST::Build ? build[k] : type == ST::Drop ? drop[k] : other[k]) - (extra ? 10 : 0);
        };
        std::vector<SongPart> playing;
        for (int p = (int) SongPart::Piano; p <= (int) SongPart::Candy; ++p)
            if (layerKindAt ((SongPart) p, map, patterns, i) >= 0) playing.push_back ((SongPart) p);
        std::stable_sort (playing.begin(), playing.end(), [&] (SongPart a, SongPart b) { return priority (a) > priority (b); });
        uint32_t out = 0;
        for (size_t k = 0; k < playing.size(); ++k)
            if (core + (int) k + 1 > budget) out |= 1u << ((int) playing[k] - (int) SongPart::Piano);
        return out;
    }

    /** A note ringing on past a chord change into a chord it clashes with (its note isn't in the
        new chord) ends at the change; notes that still fit ring on. Returns how many were trimmed. */
    inline int trimChordHangovers (std::vector<Note>& notes, const Settings& s, const suite::SectionMap& map)
    {
        const auto voiced = voiceProgression (s);
        if (voiced.empty() || map.count == 0) return 0;
        const auto L = s.quartersPerChord, P = L * (double) voiced.size();
        int trimmed = 0;
        for (auto& n : notes)
        {
            const auto end = n.start + n.length;
            for (int i = 0; i < map.count; ++i)
            {
                const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
                if (n.start < startQ - 1.0e-9 || n.start >= endQ - 1.0e-9) continue;
                for (auto b = startQ + std::ceil ((n.start - startQ) / L + 1.0e-9) * L; b < end - 0.25 && b < endQ - 1.0e-9; b += L)
                {
                    const auto slot = (size_t) std::floor (std::fmod (b - startQ, P) / L + 1.0e-9) % voiced.size();
                    const auto pc = ((n.pitch - map.keyShiftOf (i)) % 12 + 12) % 12;
                    const auto& chord = voiced[slot];
                    if (std::none_of (chord.begin(), chord.end(), [pc] (int p) { return (p % 12 + 12) % 12 == pc; }))
                    {
                        n.length = juce::jmax (0.125, b - n.start - 0.01);
                        ++trimmed;
                        break;
                    }
                }
                break;
            }
        }
        return trimmed;
    }

    /** Bass notes under C1 (MIDI 24, 33 Hz) move up an octave: below that they're felt as rumble, not
        heard as notes, and eat headroom. Returns how many moved. */
    inline int foldLowBass (std::vector<Note>& notes, int lowest = 24)
    {
        int moved = 0;
        for (auto& n : notes)
            if (n.pitch < lowest) { while (n.pitch < lowest) n.pitch += 12; ++moved; }
        return moved;
    }

    inline const juce::StringArray& songChoiceNames (SongPart part)
    {
        static const juce::StringArray chords { "None", "Sustain", "Offbeat stabs", "Pluck 3-3-2", "16th gate" };
        static const juce::StringArray bass   { "None", "Offbeat", "Rolling", "Galloping", "Sustain", "Walking", "Mine (learned)", "Octave offbeat", "Pumping" };
        static const juce::StringArray arp    { "None", "Up", "Down", "Up-down", "Bounce", "Broken", "Octave jumps", "Gated", "3-3-2" };
        static const juce::StringArray lead   { "None", "Melody", "Teaser", "Hook hint" };
        return part == SongPart::Chords ? chords : part == SongPart::Bass ? bass : part == SongPart::Arp ? arp : lead;
    }

    /** The progression starting on `degree`: rotated to the first slot on that degree, or with
        that degree replacing the first chord if it isn't in the progression. */
    inline Settings startingOn (Settings s, int degree)
    {
        const auto n = juce::jlimit (1, maxSlots, s.count);
        for (int k = 0; k < n; ++k)
            if (s.slots[(size_t) k].degree == degree)
            {
                std::rotate (s.slots.begin(), s.slots.begin() + k, s.slots.begin() + n);
                return s;
            }
        s.slots[0] = { degree, Quality::Auto };
        return s;
    }

    /** The library family a part plays in a song: the best fit for the mood and style, chosen by
        the seed, preferring rhythms that interlock with the parts chosen before it (the arp with
        the bass; the chords, acid and piano with those), and a section figure (a build riser, an
        intro or outro version) where the library has one for that section. */
    inline const patterns::Family& songFamily (SongPart part, int kind, const SongPatterns& sp, int section, int dropOrdinal = 0)
    {
        const auto mood = (patterns::Mood) juce::jlimit (0, 6, sp.libMood);
        const auto style = (patterns::Style) juce::jlimit (0, 6, sp.libStyle);
        using T = suite::SectionType;
        const auto drop = (size_t) T::Drop;
        std::vector<const patterns::Family*> others;
        if (part == SongPart::Arp || part == SongPart::Chords || part == SongPart::Acid || part == SongPart::Piano)
            if (const auto k = lib::kindFor (SongPart::Bass, sp.bass[drop], T::Drop); k >= 0) others.push_back (&songFamily (SongPart::Bass, k, sp, -1));
        if (part == SongPart::Chords)
            if (const auto k = lib::kindFor (SongPart::Arp, sp.arp[drop], T::Drop); k >= 0) others.push_back (&songFamily (SongPart::Arp, k, sp, -1));
        const auto figures = section == (int) T::Intro || section == (int) T::Build || section == (int) T::Outro;
        const auto melodicPart = part == SongPart::Lead || part == SongPart::Harmony || part == SongPart::Counter;
        // A kit first: its family for this role and kind (and its section figure, where it lists one)
        if (sp.libKits)
            if (const auto* kit = patterns::pickKit (mood, style, sp.libSeed))
                if (const auto* f = patterns::kitFamily (*kit, lib::libraryPart (part), kind, figures && ! melodicPart ? section : -1))
                {
                    // 20: drop 2 and later keep the kit's identity but play a new arp (same kind, same feel)
                    if (sp.write.arrange && dropOrdinal > 0 && part == SongPart::Arp)
                    {
                        std::vector<const patterns::Family*> ref { f };
                        for (uint32_t t = 0; t < 6; ++t)
                        {
                            const auto& alt = patterns::pickFitted (patterns::Part::Arp, kind, mood, style, sp.libSeed + 101u * (uint32_t) dropOrdinal + 7919u * t, ref, -1, f->character);
                            if (! alt.name.empty() && alt.name != f->name && patterns::feelOf (alt) == patterns::feelOf (*f)) return alt;
                        }
                    }
                    return *f;
                }
        // The song's character comes from its hook; the other parts lean the same way
        const auto character = part == SongPart::Lead ? 0u : songFamily (SongPart::Lead, 0, sp, -1).character;
        return patterns::pickFitted (lib::libraryPart (part), kind, mood, style, sp.libSeed, others, figures && ! melodicPart ? section : -1, character);
    }

    /** The song's drop rhythm for Kybalion's gate: its chord part's and arp's 16 steps (accents 1,
        hits 0.8, ghosts 0.4, a hold keeps the step open at 0.6). */
    inline suite::KitBus::Rhythm kitRhythm (const SongPatterns& sp)
    {
        using T = suite::SectionType;
        const auto drop = (size_t) T::Drop;
        suite::KitBus::Rhythm r;
        auto steps = [] (const patterns::Family& f)
        {
            std::array<float, 16> st {};
            if (f.name.empty()) return st;
            const auto v = patterns::voices (f.level[2]).front();
            const auto& bar = v.bars.front();
            const auto& marks = v.marks.front();
            for (size_t k = 0; k < bar.size(); ++k)
            {
                const auto i = (k * 16) / bar.size();
                const auto c = bar[k];
                float val = c == '.' || c == '~' ? 0.0f : c == '-' ? 0.6f : c == ',' ? 0.4f : marks[k] == '!' ? 1.0f : marks[k] == '\'' ? 0.5f : 0.8f;
                st[i] = std::max (st[i], val);
            }
            return st;
        };
        const auto ck = lib::kindFor (SongPart::Chords, sp.chords[drop] > 0 ? sp.chords[drop] : 4, T::Drop);
        const auto ak = lib::kindFor (SongPart::Arp, sp.arp[drop] > 0 ? sp.arp[drop] : 1, T::Drop);
        const auto& cf = songFamily (SongPart::Chords, ck >= 0 ? ck : 1, sp, -1);
        const auto& af = songFamily (SongPart::Arp, ak >= 0 ? ak : 1, sp, -1);
        r.chords = steps (cf);
        r.arp = steps (af);
        r.valid = std::any_of (r.chords.begin(), r.chords.end(), [] (float v) { return v > 0.0f; });
        juce::String name = juce::String (cf.name);
        if (sp.libKits)
            if (const auto* kit = patterns::pickKit ((patterns::Mood) juce::jlimit (0, 6, sp.libMood), (patterns::Style) juce::jlimit (0, 6, sp.libStyle), sp.libSeed))
                name = juce::String (kit->name);
        name.copyToUTF8 (r.name, sizeof (r.name) - 1);
        return r;
    }

    /** 11: the push budget: at most two parts anticipate the next chord; past that, a part's
        pushes land on the chord (in order: chords, bass, arp, piano, strings, brass, choir, acid). */
    inline bool overPushBudget (SongPart part, const SongPatterns& sp)
    {
        if (! sp.write.parts || ! sp.library) return false;
        using T = suite::SectionType;
        const auto drop = (size_t) T::Drop;
        int pushers = 0;
        for (auto p : { SongPart::Chords, SongPart::Bass, SongPart::Arp, SongPart::Piano, SongPart::Strings, SongPart::Brass, SongPart::Choir, SongPart::Acid })
        {
            const auto choice = p == SongPart::Chords ? sp.chords[drop] : p == SongPart::Bass ? sp.bass[drop] : p == SongPart::Arp ? sp.arp[drop] : 1;
            const auto kind = lib::isLayerPart (p) ? 1 : lib::kindFor (p, choice, T::Drop);
            if (kind < 0) continue;
            const auto& f = songFamily (p, kind, sp, -1);
            if (f.name.empty() || ! patterns::pushes (f)) continue;
            if (++pushers > 2 && p == part) return true;
            if (p == part) return false;
        }
        return false;
    }

    /** Song-length notes: each section plays its own pattern, restarting the progression at
        the section start and never spilling into the next section. Assumes 4/4 bars. */
    inline std::vector<Note> songClip (const Settings& base, const suite::SectionMap& map, const SongPatterns& patterns, SongPart part, int* thinned = nullptr, int* turned = nullptr)
    {
        std::vector<Note> out;
        // Turn-taking: chops and candy fill the hook's gaps (where the hook plays at all)
        const auto turns = patterns.takeTurns && patterns.library && (part == SongPart::Chops || part == SongPart::Candy);
        const auto& W = patterns.write;
        const auto melodicSelf = part == SongPart::Lead || part == SongPart::Harmony;
        const auto needHook = turns || (! melodicSelf && W.any() && patterns.library);
        const auto hook = needHook ? songClip (base, map, patterns, SongPart::Lead) : std::vector<Note> {};
        const auto progressionQ = base.quartersPerChord * juce::jlimit (1, maxSlots, base.count);
        const auto libPart = lib::libraryPart (part);
        const auto wStyle = (patterns::Style) juce::jlimit (0, 6, patterns.libStyle);
        auto hookIn = [&hook] (double from, double to) { std::vector<Note> v; for (const auto& n : hook) if (n.start >= from - 1.0e-6 && n.start < to - 1.0e-6) v.push_back (n); return v; };
        // The hook's first 8 bars in the first drop, from 0 (for development, breakdown voices, signatures)
        std::vector<Note> hookCell;
        for (int k = 0; k < map.count && hookCell.empty(); ++k)
            if (map.sections[(size_t) k].type == suite::SectionType::Drop)
            {
                const auto s0 = (map.sections[(size_t) k].startBar - 1) * 4.0;
                for (auto n : hookIn (s0, s0 + 32.0)) { n.start -= s0; n.pitch -= map.keyShiftOf (k); hookCell.push_back (n); }
            }
        int dropsSoFar = 0;

        for (int i = 0; i < map.count; ++i)
        {
            const auto type = (size_t) map.sections[(size_t) i].type;
            // The library's layers (piano, orchestra, electronic specifics) have their own places in the song
            int layerKind = layerKindAt (part, map, patterns, i);
            if (layerKind >= 0 && ((thinnedLayers (map, patterns, i) >> ((int) part - (int) SongPart::Piano)) & 1u) != 0)
            {
                layerKind = -1;   // too much at once here: this layer sits the section out
                if (thinned != nullptr) ++*thinned;
            }
            const auto choice = lib::isLayerPart (part) ? (layerKind >= 0 ? 1 : 0)
                              : part == SongPart::Chords ? patterns.chords[type] : part == SongPart::Bass ? patterns.bass[type]
                              : part == SongPart::Arp ? patterns.arp[type] : patterns.lead[type];
            if (choice <= 0)
            {
                // The lead answers the vocal: in sections without a lead, the bar after each vocal
                // phrase gets the melody's first two beats there, a little softer.
                if (part == SongPart::Lead && patterns.answerVocal && ! patterns.vocalBars.empty())
                {
                    const auto answer = melody (base, patterns.melody);
                    const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
                    auto sung = [&] (int bar) { return bar >= 0 && bar < (int) patterns.vocalBars.size() && patterns.vocalBars[(size_t) bar]; };
                    for (auto barQ = startQ; barQ < endQ - 1.0e-9; barQ += 4.0)
                    {
                        const auto bar = (int) std::lround (barQ / 4.0);
                        if (sung (bar) || ! sung (bar - 1)) continue;
                        const auto inCycle = std::fmod (barQ - startQ, progressionQ);
                        for (const auto& n : answer)
                            if (n.start >= inCycle - 1.0e-9 && n.start < inCycle + 2.0 - 1.0e-9)
                                out.push_back ({ n.pitch + map.keyShiftOf (i), barQ + (n.start - inCycle), std::min (n.length, 1.0), juce::jmax (1, n.velocity - 10) });
                    }
                }
                continue;
            }

            int earlierSame = 0;
            for (int k = 0; k < i; ++k) if (map.sections[(size_t) k].type == map.sections[(size_t) i].type) ++earlierSame;
            const auto laterDrop = map.sections[(size_t) i].type == suite::SectionType::Drop && earlierSame > 0;
            const auto varied = laterDrop && patterns.varyLaterDrops && choice == 1;
            if (part == SongPart::Harmony && ! varied)
                continue;   // the harmony line only plays in later drops
            if (part == SongPart::Counter && (choice != 1 || ! patterns.counter))
                continue;   // the counter-melody answers the full lead only

            auto s = base;
            const auto isBreakdown = map.sections[(size_t) i].type == suite::SectionType::Breakdown;
            if (isBreakdown && (patterns.breakdownStart == 1 || patterns.breakdownStart == 2))
                s = startingOn (base, patterns.breakdownStart == 1 ? 6 : 4);   // the drop's return to i lands harder
            if (isBreakdown && W.harmony) s = writing::modalMixture (s);         // 38: the borrowed lift
            // The pattern library: this part's family for the song, at the section's energy
            const auto secType = map.sections[(size_t) i].type;
            const auto kind = lib::isLayerPart (part) ? layerKind : patterns.library ? lib::kindFor (part, choice, secType) : -1;
            const auto libLead = kind >= 0 && (part == SongPart::Lead || part == SongPart::Harmony || part == SongPart::Counter)
                                 && patterns.libLead && patterns.melody.motif.empty() && ! patterns.melody.dna.valid;
            const auto useLib = kind >= 0 && (libLead || (part != SongPart::Lead && part != SongPart::Harmony && part != SongPart::Counter));
            const auto libMood = (patterns::Mood) juce::jlimit (0, 6, patterns.libMood);
            const auto libStyle = (patterns::Style) juce::jlimit (0, 6, patterns.libStyle);
            const auto dropOrdinal = secType == suite::SectionType::Drop ? dropsSoFar : 0;
            const auto* family = useLib ? &songFamily (libLead ? SongPart::Lead : part, kind, patterns, (int) secType, dropOrdinal) : nullptr;
            lib::RenderOptions ropt;
            ropt.colourFit = W.harmony;
            ropt.flattenPush = overPushBudget (part, patterns);
            const auto* counterFamily = libLead && part == SongPart::Counter ? &patterns::pick (patterns::Part::Counter, 0, libMood, libStyle, patterns.libSeed) : nullptr;
            const auto cycleQ = libLead ? progressionQ * std::ceil (32.0 / progressionQ - 1.0e-9) : progressionQ;   // a lead hook: 8-bar phrases
            // Register: an arp sitting on top of the library's hook moves an octave away from it (one
            // decision for the section, so its fills and variations move with it)
            int arpShift = 0;
            if (part == SongPart::Arp && family != nullptr && ! libLead && patterns.library && patterns.libLead && patterns.melody.motif.empty() && ! patterns.melody.dna.valid)
            {
                const auto& hookFamily = songFamily (SongPart::Lead, 0, patterns, -1);
                if (! hookFamily.name.empty())
                {
                    auto median = [] (std::vector<Note> v) { std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.pitch < b.pitch; }); return v.empty() ? 0 : v[v.size() / 2].pitch; };
                    const auto hook = median (lib::render (hookFamily, 1, false, s, 32.0, patterns.melody.octave));
                    const auto arp = median (lib::render (*family, 2, false, s, progressionQ));
                    if (hook > 0 && arp > 0 && std::abs (arp - hook) < 5) arpShift = arp > hook || arp - 12 < 48 ? 12 : -12;
                }
            }
            auto makeCycle = [&] (Settings cs, int level = 1, bool fill = false, int variant = 0)
            {
                std::vector<Note> c;
                if (family != nullptr && ! libLead)
                {
                    c = lib::render (*family, level, fill, cs, progressionQ, 5, variant, ropt);
                    if (part == SongPart::Chords && laterDrop && patterns.climbLaterDrops) c = climbVoicing (c);
                    for (auto& n : c) n.pitch = juce::jlimit (0, 127, n.pitch + arpShift);   // clear of the hook (below)
                    return c;
                }
                if (part == SongPart::Chords)
                {
                    cs.chordPattern = (ChordPattern) (choice - 1);
                    c = chordClip (cs);
                    if (laterDrop && patterns.climbLaterDrops) c = climbVoicing (c);   // drop 2+: higher and wider
                }
                else if (part == SongPart::Bass) { cs.bassPattern = (BassPattern) (choice - 1);   c = bassClip (cs); }
                else if (part == SongPart::Arp)  { cs.arpPattern = (ArpPattern) (choice - 1);     c = arpClip (cs); }
                else
                {
                    c = libLead ? lib::render (*family, laterDrop ? 2 : 1, false, cs, cycleQ, patterns.melody.octave, earlierSame % 2) : melody (cs, patterns.melody);
                    if (W.phrase && libLead) c = writing::singable (writing::phraseArc (c, cs, cycleQ, patterns.libSeed + (uint32_t) earlierSame), cs);   // 1, 6, 26-29
                    if (choice == 2) c = teaser (c);
                    if (choice == 3) c = hookHint (c, progressionQ);
                    // Each later drop varies the hook its own way: embellished, then answered (call and
                    // response), then pushed ahead of the beat (and embellished again)
                    if (varied)
                    {
                        if (earlierSame == 2) c = callResponse (c, cs);
                        else if (earlierSame >= 3) c = pushed (embellish (c, cs));
                        else c = embellish (c, cs);
                    }
                    if (part == SongPart::Harmony) c = harmony (c, cs);
                    if (part == SongPart::Counter)
                    {
                        if (counterFamily != nullptr && ! counterFamily->name.empty())
                        {
                            // The library's counter-line answers the hook: a note that would start with one of the
                            // hook's slides off its attack (an 8th or a beat later, keeping the rest of its length)
                            const auto hook = c;
                            auto hookStarts = [&hook] (double t) { return std::any_of (hook.begin(), hook.end(), [t] (const Note& h) { return std::abs (h.start - t) < 1.0e-6; }); };
                            std::vector<Note> line;
                            for (auto n : lib::render (*counterFamily, laterDrop ? 2 : 1, false, cs, cycleQ))
                            {
                                if (hookStarts (n.start))
                                {
                                    bool moved = false;
                                    for (const auto d : { 0.5, 0.25, 0.75, 1.0 })
                                        if (n.length - d > 0.2 && ! hookStarts (n.start + d)) { n.start += d; n.length -= d; moved = true; break; }
                                    if (! moved) continue;
                                }
                                line.push_back (n);
                            }
                            c = line;
                        }
                        else c = counterMelody (c, cs);
                    }
                }
                return c;
            };
            // The section's energy: quiet sections low, grooves mid, drops high, builds climbing
            using ST = suite::SectionType;
            const auto sectionLevel = secType == ST::Drop ? 2 : secType == ST::Groove ? 1
                                    : secType == ST::Breakdown && lib::isLayerPart (part) && part != SongPart::Candy ? 1 : 0;   // the breakdown is the orchestra's moment
            const auto cycle = makeCycle (s, sectionLevel);
            const auto useLevels = family != nullptr && ! libLead;
            std::array<std::vector<Note>, 3> levels;
            std::vector<Note> fillCycle, fillCycle2, highCycle2;
            if (useLevels)
            {
                for (int lv = 0; lv < 3; ++lv) levels[(size_t) lv] = lv == sectionLevel ? cycle : makeCycle (s, lv);
                fillCycle = makeCycle (s, sectionLevel, true);
                fillCycle2 = makeCycle (s, sectionLevel, true, 1);    // every other phrase: the second fill
                highCycle2 = makeCycle (s, 2, false, 1);              // ...and the second high energy
            }
            auto phraseOf = [&] (double q) { return (int) std::floor ((q - (map.sections[(size_t) i].startBar - 1) * 4.0) / 32.0 + 1.0e-9); };
            // Breakdown lift: the second half of the breakdown moves to the relative major's home (III).
            const auto lifting = isBreakdown && patterns.breakdownStart == 3;
            const auto liftCycle = lifting ? makeCycle (startingOn (base, 3), sectionLevel) : std::vector<Note> {};

            const auto lift = (part == SongPart::Lead || part == SongPart::Harmony) && patterns.liftLaterDrops && laterDrop ? 12 : 0;
            const auto keyChange = map.keyShiftOf (i);   // final-drop key change: everything from the last drop on moves up

            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0;
            const auto endQ = (map.endBar (i) - 1) * 4.0;
            std::vector<Note> section;
            const auto liftFrom = startQ + std::floor ((endQ - startQ) / 2.0 / cycleQ + 1.0e-9) * cycleQ;   // a whole progression in
            auto phraseEnd = [&] (double start)
            {
                const auto bar = (int) std::floor ((start - startQ) / 4.0 + 1.0e-9);
                return patterns.phraseVariation && bar % 8 == 7 && start < endQ - 4.0 - 1.0e-9;
            };
            for (auto offset = startQ; offset < endQ - 1.0e-9; offset += cycleQ)
            {
                int bounce = 0;
                // Builds climb through the energies: low, mid, then high for the last third
                auto buildLevel = secType == ST::Build ? juce::jlimit (0, 2, (int) std::floor (3.0 * (offset - startQ) / juce::jmax (1.0, endQ - startQ))) : sectionLevel;
                // 16: energy inside a section: a long groove starts a level down, a breakdown's last quarter a level up
                if (W.arrange && useLevels)
                {
                    if (secType == ST::Groove && endQ - startQ >= 64.0 && offset - startQ < (endQ - startQ) / 2.0) buildLevel = juce::jmax (0, sectionLevel - 1);
                    if (secType == ST::Breakdown && offset >= startQ + 0.75 * (endQ - startQ)) buildLevel = juce::jmin (2, sectionLevel + 1);
                }
                const auto alternate = useLevels && buildLevel == 2 && phraseOf (offset) % 2 == 1;   // the drop's second phrase plays the second high
                const auto& thisCycle = lifting && offset >= liftFrom - 1.0e-9 && liftFrom > startQ ? liftCycle
                                      : alternate ? highCycle2 : useLevels ? levels[(size_t) buildLevel] : cycle;
                if (useLevels)
                    for (const auto* fills : { &fillCycle, &fillCycle2 })   // the phrase-end bars play the family's fills, taking turns
                        for (const auto& n : *fills)
                    {
                        const auto start = offset + n.start;
                        if (start < endQ - 1.0e-9 && phraseEnd (start) && (phraseOf (start) % 2 == 1) == (fills == &fillCycle2))
                            section.push_back ({ n.pitch + keyChange, start, std::min (n.length, endQ - start - 0.01), n.velocity });
                    }
                for (const auto& n : thisCycle)
                {
                    const auto start = offset + n.start;
                    if (start >= endQ - 1.0e-9) continue;
                    if (useLevels)
                    {
                        if (phraseEnd (start)) continue;
                        // A little life: the fourth bar of some phrases lifts its last beat an octave (bass and arp), by the seed
                        auto p = n.pitch;
                        const auto bar = (int) std::floor ((start - startQ) / 4.0 + 1.0e-9);
                        if ((part == SongPart::Bass || part == SongPart::Arp) && bar % 8 == 3 && start - (startQ + bar * 4.0) >= 3.0 - 1.0e-9
                            && (patterns::hash (patterns.libSeed + (uint32_t) (i * 131 + bar)) & 1u) != 0 && p + 12 <= (part == SongPart::Bass ? 60 : 96))
                            p += 12;
                        section.push_back ({ p + lift + keyChange, start, std::min (n.length, endQ - start - 0.01), n.velocity });
                        continue;
                    }
                    // Phrase variation: the last bar of every 8-bar phrase that the section carries on
                    // past is a turnaround (the section's own last bar is left to the transition).
                    int turn = 0;
                    const auto bar = (int) std::floor ((start - startQ) / 4.0 + 1.0e-9);
                    if (patterns.phraseVariation && bar % 8 == 7 && start < endQ - 4.0 - 1.0e-9)
                    {
                        if (part == SongPart::Bass && start - (startQ + bar * 4.0) >= 2.0 - 1.0e-9) turn = (bounce++ % 2 == 1) ? 12 : 0;   // octave bounce on beats 3-4
                        if (part == SongPart::Arp) turn = 12;                                                                            // the arp lifts an octave
                    }
                    section.push_back ({ n.pitch + lift + keyChange + turn, start, std::min (n.length, endQ - start - 0.01), n.velocity });
                }
            }

            // ---- Writing (the Writing menu) ----
            {
                const auto seed = patterns.libSeed * 31u + (uint32_t) i;
                const auto hasNext = i + 1 < map.count;
                const auto nextType = hasNext ? map.sections[(size_t) i + 1].type : ST::Outro;
                const auto finalDrop = secType == ST::Drop && i == map.lastDropIndex() && earlierSame > 0;
                const auto hookHere = hookIn (startQ, endQ);
                auto tile = [&] (const std::vector<Note>& cell, double from, double to, double len)
                {
                    std::vector<Note> v;
                    if (cell.empty() || len <= 0.0) return v;
                    for (auto q = from; q < to - 1.0e-9; q += len)
                        for (auto n : cell) { n.start += q; n.pitch += keyChange; if (n.start < to - 1.0e-9) { n.length = std::min (n.length, to - n.start - 0.01); v.push_back (n); } }
                    return v;
                };
                auto fillBar = [&] (double q) { const auto bar = (int) std::floor ((q - startQ) / 4.0 + 1.0e-9); return bar % 8 == 7 && q < endQ - 4.0 - 1.0e-9; };
                const auto chordal = lib::chordal (libPart);
                auto sk = s;   // the key this section is in (after a final-drop key change)
                sk.root = s.root + keyChange;

                // A. The hook: developed per section (3, 46), its climax kept for the drops (2), tension (4, 5), ornaments (30)
                if (part == SongPart::Lead && ! section.empty())
                {
                    std::vector<Note> base0;
                    for (const auto& n : cycle) base0.push_back (n);
                    if (W.develop && isBreakdown && ! base0.empty())
                    {
                        const auto mid = startQ + std::floor ((endQ - startQ) / 2.0 / 8.0) * 8.0;
                        std::vector<Note> v;
                        for (const auto& n : writing::foreshadow (section)) if (n.start < mid) v.push_back (n);
                        for (const auto& n : tile (writing::develop (base0, writing::devAugment, s, cycleQ), mid, endQ, cycleQ)) v.push_back (n);
                        section = v;
                    }
                    else if (W.develop && secType == ST::Build && ! base0.empty())
                        section = tile (writing::develop (base0, writing::devFragment, s, cycleQ), startQ, endQ, cycleQ);
                    if (W.phrase)
                    {
                        const auto top = writing::maxPitch (hookCell);
                        writing::climax (section, sk, top, isBreakdown, finalDrop, startQ);
                    }
                    if (W.harmony) section = writing::tension (section, sk, startQ, seed);
                    if (W.ornaments && secType == ST::Drop) section = writing::ornament (section, sk, seed, laterDrop);
                }
                // C. The bass: a pedal through intros and the first half of builds (37), a walk on thirds in the quiet sections (15), the hook in the final drop's fills (48)
                if (part == SongPart::Bass && ! section.empty())
                {
                    if (W.harmony && (secType == ST::Intro || secType == ST::Build))
                    {
                        const auto until = secType == ST::Build ? startQ + (endQ - startQ) / 2.0 : endQ;
                        std::vector<Note> ped;
                        for (const auto& n : section) if (n.start < until) ped.push_back (n);
                        writing::pedal (ped, sk);
                        size_t k = 0;
                        for (auto& n : section) if (n.start < until && k < ped.size()) n.pitch = ped[k++].pitch;
                    }
                    if (W.harmony && (isBreakdown || secType == ST::Intro || secType == ST::Outro)) writing::walkOnThirds (section, sk, startQ);
                    if (W.develop && finalDrop && ! hookHere.empty())
                        for (auto bq = startQ; bq < endQ - 1.0e-9; bq += 4.0)
                            if (fillBar (bq))
                            {
                                const auto line = writing::hookInBass (hookHere, bq);
                                if (line.empty()) continue;
                                section.erase (std::remove_if (section.begin(), section.end(), [bq] (const Note& n) { return n.start >= bq - 1.0e-6 && n.start < bq + 4.0 - 1.0e-6; }), section.end());
                                section.insert (section.end(), line.begin(), line.end());
                            }
                }
                // E. Shared motifs: the arp walks the hook's notes in later drops (21)
                if (part == SongPart::Arp && W.arrange && laterDrop && ! hookHere.empty()) section = writing::arpFromHook (section, hookHere, sk, startQ);
                // C. Passing chords in the fills (13), the turnaround twice as fast (39), the key change prepared (40), voicings (41)
                if (W.harmony && (chordal || part == SongPart::Bass) && useLevels)
                    for (auto bq = startQ; bq < endQ - 1.0e-9; bq += 4.0)
                        if (fillBar (bq)) writing::passingChord (section, sk, bq, bq + 4.0 - startQ, part == SongPart::Bass);
                if (W.harmony && secType == ST::Drop && useLevels && family != nullptr && (chordal || part == SongPart::Bass || part == SongPart::Arp))
                {
                    auto fs = s;
                    fs.quartersPerChord = s.quartersPerChord / 2.0;
                    const auto fastCycle = lib::render (*family, 2, false, fs, progressionQ, 5, 0, ropt);
                    writing::accelerate (section, tile (fastCycle, startQ, endQ, progressionQ), startQ, endQ);
                }
                if (W.harmony && secType == ST::Build && hasNext && map.keyShiftOf (i + 1) > map.keyShiftOf (i) && part != SongPart::Timpani)
                    writing::prepareKeyChange (section, ((s.root + map.keyShiftOf (i + 1)) % 12 + 12) % 12, endQ - 4.0, part == SongPart::Bass);
                if (W.harmony && chordal) writing::voiceByEnergy (section, isBreakdown, W.parts && ! hookHere.empty() ? &hookHere : nullptr);
                // J. Counterpoint (14, 43), answers in the hook's rests (17), doublings (44)
                if (W.parts && part == SongPart::Counter && ! hookHere.empty()) section = writing::counterpoint (section, hookHere, sk, startQ);
                if (W.parts && (part == SongPart::Counter || part == SongPart::Candy || part == SongPart::Chops || part == SongPart::Brass) && ! hookHere.empty())
                    writing::answerInRests (section, hookHere);
                if (W.parts && part == SongPart::Strings && finalDrop && ! hookHere.empty())
                    for (const auto& n : writing::thirdsBelow (hookHere, sk)) section.push_back (n);
                if (W.parts && part == SongPart::Brass && secType == ST::Drop && ! section.empty())
                    for (const auto& n : writing::brassOnBassHits (songClip (base, map, patterns, SongPart::Bass), sk, startQ, endQ)) section.push_back (n);
                // D. The breakdown's voice sings the hook, slowed (22)
                if (W.arrange && isBreakdown && lib::isLayerPart (part) && ! section.empty() && ! hookCell.empty())
                {
                    int voice = wStyle == patterns::Style::Orchestral ? (int) patterns::Part::Strings : (int) patterns::Part::Piano;
                    if (patterns.libKits)
                        if (const auto* kit = patterns::pickKit ((patterns::Mood) juce::jlimit (0, 6, patterns.libMood), wStyle, patterns.libSeed); kit != nullptr && kit->breakdownVoice >= 0)
                            voice = kit->breakdownVoice;
                    if ((int) libPart == voice)
                    {
                        const auto mid = startQ + std::floor ((endQ - startQ) / 2.0 / 8.0) * 8.0;
                        auto line = tile (writing::develop (hookCell, writing::devAugment, s, 32.0), mid, endQ, 32.0);
                        for (auto& n : line) { while (n.pitch > 84) n.pitch -= 12; while (n.pitch < 67) n.pitch += 12; n.velocity = std::max (1, n.velocity - 8); }
                        section.insert (section.end(), line.begin(), line.end());
                    }
                }
                // D. Entries (18) and a DJ-friendly intro and outro (47)
                if (W.arrange && lib::isLayerPart (part))
                {
                    int enterBar = 0, fromDrop = 0;
                    if (part == SongPart::Strings && isBreakdown && endQ - startQ >= 64.0) enterBar = 9;
                    if (part == SongPart::Choir && secType == ST::Drop) fromDrop = 2;
                    if (patterns.libKits)
                        if (const auto* kit = patterns::pickKit ((patterns::Mood) juce::jlimit (0, 6, patterns.libMood), wStyle, patterns.libSeed))
                        {
                            if (kit->enterBar[(size_t) libPart] > 0) enterBar = kit->enterBar[(size_t) libPart];
                            if (kit->fromDrop[(size_t) libPart] > 0) fromDrop = kit->fromDrop[(size_t) libPart];
                        }
                    if (secType == ST::Drop && fromDrop > 0 && dropOrdinal + 1 < fromDrop) section.clear();
                    if (enterBar > 1)
                        section.erase (std::remove_if (section.begin(), section.end(), [&] (const Note& n) { return n.start < startQ + (enterBar - 1) * 4.0 - 1.0e-6; }), section.end());
                }
                if (W.arrange && (secType == ST::Intro || secType == ST::Outro) && part != SongPart::Bass)
                {
                    const auto len = endQ - startQ;
                    const auto bare = std::min (64.0, len / 2.0), half = std::min (128.0, len) / 2.0;
                    section.erase (std::remove_if (section.begin(), section.end(), [&] (const Note& n)
                    {
                        const auto fromEdge = secType == ST::Intro ? n.start - startQ : endQ - n.start;
                        if (fromEdge < bare) return true;                                    // bass only
                        return fromEdge < half * 2.0 && fromEdge < 128.0 && part != SongPart::Arp && fromEdge < bare * 2.0;   // then bass and arp
                    }), section.end());
                }
                // B. Triplet turns in every other fill (36)
                if (W.feel && (part == SongPart::Lead || part == SongPart::Arp || part == SongPart::Candy))
                    for (auto bq = startQ; bq < endQ - 1.0e-9; bq += 4.0)
                        if (fillBar (bq) && (int) std::floor ((bq - startQ) / 32.0) % 2 == 1) writing::tripletTurn (section, bq);
                // D. The section's last bar by what comes next (19), and a breath before the drop (35)
                if (W.arrange && hasNext && useLevels && part != SongPart::Timpani) writing::transitionBar (section, endQ - 4.0, nextType);
                if (W.arrange && hasNext && nextType == ST::Drop && part != SongPart::Timpani) writing::breathBeforeDrop (section, endQ);
                // B. Articulation by energy (9) and velocity contours (8, 33)
                if (W.feel && (part == SongPart::Arp || part == SongPart::Chords || part == SongPart::Acid || part == SongPart::Candy || part == SongPart::Chops || part == SongPart::Piano))
                    section = writing::articulate (section, sectionLevel);
                if (W.feel) section = writing::velocityContour (section, startQ, endQ, secType == ST::Build, wStyle);
                // J. Registers (42)
                if (W.parts && patterns.library) writing::keepInBand (section, libPart, ! melodicSelf);
                // F. Your edits (24)
                if (W.learned) section = writing::applyLearned (section, libPart);
                writing::sortByStart (section);
            }

            // Ghost notes in the bass and arp, more where the energy is higher (by section type, with the Writing menu's feel).
            if (part == SongPart::Bass || part == SongPart::Arp)
                section = addGhostNotes (section, startQ, endQ, patterns.ghostNotes * juce::jlimit (0.0f, 1.0f, map.energyOf (i)) * (W.feel ? writing::ghostAmount (secType) : 1.0f), i * 7 + (int) part);

            // Final drop doubling: in the last of two or more drops, the lead gains an octave-up double
            // (an octave down if that would go too high) and the bass a sub octave, so the end is the biggest.
            if (patterns.doubleFinalDrop && i == map.lastDropIndex() && earlierSame > 0 && (part == SongPart::Lead || part == SongPart::Bass))
            {
                const auto count = section.size();
                for (size_t k = 0; k < count; ++k)
                {
                    auto d = section[k];
                    if (part == SongPart::Lead && W.ornaments && writing::maxPitch (section) + 12 > 96) break;   // 31: no room above: no double
                    if (part == SongPart::Lead) d.pitch += d.pitch + 12 <= 96 ? 12 : -12;
                    else if (d.pitch - 12 >= 24) d.pitch -= 12;   // never under C1 (see foldLowBass)
                    else continue;
                    d.velocity = juce::jmax (1, d.velocity - 15);
                    section.push_back (d);
                }
            }

            // Who's speaking: while the hook is moving (a hook note starting half a beat before to a
            // 16th after), chops and candy hold back, so they answer in its gaps
            if (turns && ! hook.empty())
            {
                std::vector<Note> kept;
                int lastBar = -1;
                for (const auto& n : section)
                {
                    const auto busy = std::any_of (hook.begin(), hook.end(), [&n] (const Note& h) { return h.start >= n.start - 0.5 - 1.0e-6 && h.start <= n.start + 0.25 + 1.0e-6; });
                    if (! busy) { kept.push_back (n); continue; }
                    const auto bar = (int) std::floor (n.start / 4.0 + 1.0e-9);
                    if (turned != nullptr && bar != lastBar) ++*turned;
                    lastBar = bar;
                }
                section = std::move (kept);
            }

            // Room for the vocal: where it sings, the lead keeps only its beat-1 and beat-3 notes
            // (softer), and the harmony and counter-melody step out.
            if (patterns.leaveRoomForVocal && ! patterns.vocalBars.empty() && part == SongPart::Lead && W.arrange && family != nullptr)
            {
                // 49: while the vocal sings the hook waits (only a pickup in a sung bar's last beat before
                // a gap); in the bar after a sung phrase it answers with its fill
                auto sungBar = [&] (int bar) { return bar >= 0 && bar < (int) patterns.vocalBars.size() && patterns.vocalBars[(size_t) bar]; };
                const auto answerCycle = lib::render (*family, laterDrop ? 2 : 1, true, s, cycleQ, patterns.melody.octave);
                std::vector<Note> kept;
                for (const auto& n : section)
                {
                    const auto bar = (int) std::floor (n.start / 4.0 + 1.0e-9);
                    if (! sungBar (bar) && ! sungBar (bar - 1)) { kept.push_back (n); continue; }
                    if (sungBar (bar) && ! sungBar (bar + 1) && n.start - bar * 4.0 >= 3.0 - 1.0e-6) kept.push_back (n);   // a pickup into the gap
                }
                for (auto barQ = startQ; barQ < endQ - 1.0e-9; barQ += 4.0)
                {
                    const auto bar = (int) std::lround (barQ / 4.0);
                    if (sungBar (bar) || ! sungBar (bar - 1)) continue;
                    const auto inCycle = std::fmod (barQ - startQ, cycleQ);
                    for (const auto& n : answerCycle)
                        if (n.start >= inCycle - 1.0e-9 && n.start < inCycle + 4.0 - 1.0e-9)
                            kept.push_back ({ n.pitch + keyChange + lift, barQ + (n.start - inCycle), std::min (n.length, 4.0), n.velocity });
                }
                section = std::move (kept);
            }
            else if (patterns.leaveRoomForVocal && ! patterns.vocalBars.empty() && (part == SongPart::Lead || part == SongPart::Harmony || part == SongPart::Counter || part == SongPart::Chops))
            {
                std::vector<Note> kept;
                for (auto n : section)
                {
                    const auto bar = (size_t) std::floor (n.start / 4.0 + 1.0e-9);
                    const auto sung = bar < patterns.vocalBars.size() && patterns.vocalBars[bar];
                    if (! sung) { kept.push_back (n); continue; }
                    if (part != SongPart::Lead || std::abs (std::fmod (n.start, 2.0)) > 1.0e-6) continue;
                    n.velocity = juce::jmax (1, n.velocity - 20);
                    kept.push_back (n);
                }
                section = std::move (kept);
            }
            out.insert (out.end(), section.begin(), section.end());
            if (secType == suite::SectionType::Drop) ++dropsSoFar;
        }
        // 50: the song's signature: the hook's first three notes on the arp at the start of each
        // intro, breakdown and build, so the sections sound related
        if (part == SongPart::Arp && W.develop && patterns.library && ! hookCell.empty())
            for (int i = 0; i < map.count; ++i)
            {
                const auto t = map.sections[(size_t) i].type;
                if (t != suite::SectionType::Intro && t != suite::SectionType::Breakdown && t != suite::SectionType::Build) continue;
                const auto at = (map.sections[(size_t) i].startBar - 1) * 4.0;
                out.erase (std::remove_if (out.begin(), out.end(), [at] (const Note& n) { return n.start >= at - 1.0e-6 && n.start < at + 1.5 - 1.0e-6; }), out.end());
                for (auto n : writing::signatureFigure (hookCell, at, 74)) { n.pitch += map.keyShiftOf (i); out.push_back (n); }
            }
        writing::sortByStart (out);
        return out;
    }

    /** Vocal answer: a pluck or pad line in the vocal's gaps. In the bar after each sung phrase,
        the chord there is arpeggiated in 8ths (root, third, fifth, octave and back) above the
        vocal's usual range; if the next bar is a gap too, the chord's third holds through it.
        Nothing plays while the vocal sings. */
    inline std::vector<Note> vocalAnswer (const Settings& s, const suite::SectionMap& map, const std::vector<bool>& vocalBars)
    {
        std::vector<Note> out;
        if (vocalBars.empty() || map.count == 0) return out;
        auto plain = s;
        plain.voicing = Voicing::Close;
        plain.addBass = false;
        const auto voiced = voiceProgression (plain);
        if (voiced.empty()) return out;
        const auto L = s.quartersPerChord;
        const auto progressionQ = L * (double) voiced.size();
        auto sung = [&] (int bar) { return bar >= 0 && bar < (int) vocalBars.size() && vocalBars[(size_t) bar]; };
        for (int i = 0; i < map.count; ++i)
        {
            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
            for (auto barQ = startQ; barQ < endQ - 1.0e-9; barQ += 4.0)
            {
                const auto bar = (int) std::lround (barQ / 4.0);
                if (sung (bar) || ! sung (bar - 1)) continue;
                const auto slot = (size_t) std::floor (std::fmod (barQ - startQ, progressionQ) / L + 1.0e-9) % voiced.size();
                auto tones = voiced[slot];
                std::sort (tones.begin(), tones.end());
                if (tones.size() < 3) continue;
                auto lift = 0;
                while (tones[0] + lift < 72) lift += 12;   // above the vocal (C5 and up)
                const int shift = map.keyShiftOf (i);
                const int order[] = { 0, 1, 2, 3, 2, 1, 2, 3 };
                for (int k = 0; k < 8; ++k)
                {
                    const auto idx = order[k];
                    const auto pitch = (idx < 3 ? tones[(size_t) idx] : tones[0] + 12) + lift + shift;
                    out.push_back ({ pitch, barQ + k * 0.5, 0.45, 92 - 3 * k });
                }
                if (! sung (bar + 1) && barQ + 4.0 < endQ - 1.0e-9)
                    out.push_back ({ tones[1] + lift + shift, barQ + 4.0, 3.8, 70 });   // the third holds through a second gap bar
            }
        }
        return out;
    }

    //==============================================================================
    inline juce::MidiFile toMidiFile (const std::vector<Note>& notes, double bpm, const juce::String& trackName, int accentCC = 0)
    {
        constexpr int ppq = 960;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0);
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0.0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

        for (const auto& n : notes)
        {
            const auto on = n.start * ppq, off = (n.start + n.length) * ppq;
            if (accentCC > 0)   // the note's accent as a controller too (e.g. CC 74), so synths can open up on accents
                seq.addEvent (juce::MidiMessage::controllerEvent (1, accentCC, juce::jlimit (0, 127, n.velocity)), std::max (0.0, on - 1.0));
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

    /** Humanised MIDI: beats accented, off-beats a little, the rest softer, plus small
        repeatable variations of velocity and timing (`amount` 0..1, the same `seed` gives
        the same result). Starts stay at or after 0. */
    inline std::vector<Note> humanise (std::vector<Note> notes, float amount, int seed)
    {
        if (amount <= 0.0f) return notes;
        juce::Random rng ((juce::int64) seed * 104729 + 7);
        for (auto& n : notes)
        {
            const auto inBeat = n.start - std::floor (n.start + 1.0e-9);
            const auto accent = inBeat < 1.0e-6 ? 6.0f : std::abs (inBeat - 0.5) < 1.0e-6 ? 3.0f : -4.0f;
            const auto jitter = (rng.nextFloat() * 2.0f - 1.0f) * 10.0f;
            n.velocity = juce::jlimit (1, 127, (int) std::lround ((float) n.velocity + amount * (accent + jitter)));
            const auto shift = (rng.nextDouble() * 2.0 - 1.0) * 0.012 * amount;   // about +-5 ms at 138 BPM, full amount
            n.start = std::max (0.0, n.start + shift);
        }
        return notes;
    }

    /** One MIDI file with a part per track (each on its own channel) and the song's sections
        as markers on the first track, for importing everything into FL in one go. */
    inline juce::MidiFile toMultiTrackMidi (const std::vector<std::pair<juce::String, std::vector<Note>>>& parts, double bpm,
                                            const std::vector<std::pair<double, juce::String>>& markers, int accentCC = 0)
    {
        constexpr int ppq = 960;
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);

        juce::MidiMessageSequence conductor;
        conductor.addEvent (juce::MidiMessage::textMetaEvent (3, "Sections"), 0.0);
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0.0);
        conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        for (const auto& [quarters, name] : markers)
            conductor.addEvent (juce::MidiMessage::textMetaEvent (6, name), quarters * ppq);
        // Tempo changes the song played with (Ouroboros notes them bar by bar).
        if (! markers.empty())
            for (const auto& [bar, tempo] : suite::TempoBus::changes ((int) std::ceil (markers.back().first / 4.0) + 64, (float) bpm))
                if (bar > 1) conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / tempo)), (bar - 1) * 4.0 * ppq);
        // The suite director's tempo moves (a 2% ease through a breakdown, climbing back in the build)
        for (const auto& [q, tempo] : suite::DirectorBus::tempoChanges (bpm))
            conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / tempo)), q * ppq);
        conductor.sort();
        file.addTrack (conductor);

        int channel = 1;
        for (const auto& [name, notes] : parts)
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
            for (const auto& n : notes)
            {
                if (accentCC > 0)
                    seq.addEvent (juce::MidiMessage::controllerEvent (channel, accentCC, juce::jlimit (0, 127, n.velocity)), std::max (0.0, n.start * ppq - 1.0));
                seq.addEvent (juce::MidiMessage::noteOn (channel, n.pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), n.start * ppq);
                seq.addEvent (juce::MidiMessage::noteOff (channel, n.pitch), (n.start + n.length) * ppq);
            }
            seq.sort();
            seq.updateMatchedPairs();
            file.addTrack (seq);
            channel = channel % 16 + 1;
        }
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
