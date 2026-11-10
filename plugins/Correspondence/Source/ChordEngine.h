#pragma once

// Pure music logic for Correspondence: chords from scale degrees, voicing with
// voice leading, rhythm patterns for chords/bass/arp, presets and MIDI export.
// No audio or UI code here so it can be unit-tested directly.

#include <juce_audio_basics/juce_audio_basics.h>
#include "Music.h"
#include "SectionBus.h"
#include <vector>
#include <algorithm>

namespace chords
{
    constexpr int maxSlots = 8;

    enum class Quality { Auto, Major, Minor, Sus2, Sus4, Seventh, Add9, Power };
    enum class Voicing { Close, Open, Wide };
    enum class ChordPattern { Sustain, OffbeatStabs, Pluck332, Gate16 };
    enum class BassPattern { Offbeat, Rolling, Galloping, Sustain, Walking };
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

            switch (s.bassPattern)
            {
                case BassPattern::Offbeat:   forEachHit (L, emit, { 2, 6, 10, 14 }, 0.4); break;
                case BassPattern::Rolling:   forEachHit (L, emit, { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }, 0.2); break;
                case BassPattern::Galloping: forEachHit (L, emit, { 2, 3, 6, 7, 10, 11, 14, 15 }, 0.2); break;
                case BassPattern::Sustain:   emit (0.0, L - 0.02, true); break;
                case BassPattern::Walking:   break;   // handled above
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

    enum class SongPart { Chords, Bass, Arp, Lead, Harmony, Counter };

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
        int breakdownStart = 1;       // breakdowns: 0 = same progression, 1 = start on VI, 2 = start on IV
        bool counter = true;          // a counter-melody wherever the full lead plays (SongPart::Counter)
    };

    inline const juce::StringArray& songChoiceNames (SongPart part)
    {
        static const juce::StringArray chords { "None", "Sustain", "Offbeat stabs", "Pluck 3-3-2", "16th gate" };
        static const juce::StringArray bass   { "None", "Offbeat", "Rolling", "Galloping", "Sustain", "Walking" };
        static const juce::StringArray arp    { "None", "Up", "Down", "Up-down", "Bounce" };
        static const juce::StringArray lead   { "None", "Melody", "Teaser" };
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

            int earlierSame = 0;
            for (int k = 0; k < i; ++k) if (map.sections[(size_t) k].type == map.sections[(size_t) i].type) ++earlierSame;
            const auto laterDrop = map.sections[(size_t) i].type == suite::SectionType::Drop && earlierSame > 0;
            const auto varied = laterDrop && patterns.varyLaterDrops && choice == 1;
            if (part == SongPart::Harmony && ! varied)
                continue;   // the harmony line only plays in later drops
            if (part == SongPart::Counter && (choice != 1 || ! patterns.counter))
                continue;   // the counter-melody answers the full lead only

            auto s = base;
            if (map.sections[(size_t) i].type == suite::SectionType::Breakdown && patterns.breakdownStart > 0)
                s = startingOn (base, patterns.breakdownStart == 1 ? 6 : 4);   // the drop's return to i lands harder
            std::vector<Note> cycle;
            if (part == SongPart::Chords)    { s.chordPattern = (ChordPattern) (choice - 1); cycle = chordClip (s); }
            else if (part == SongPart::Bass) { s.bassPattern = (BassPattern) (choice - 1);   cycle = bassClip (s); }
            else if (part == SongPart::Arp)  { s.arpPattern = (ArpPattern) (choice - 1);     cycle = arpClip (s); }
            else
            {
                cycle = melody (s, patterns.melody);
                if (choice == 2) cycle = teaser (cycle);
                if (varied) cycle = embellish (cycle, s);
                if (part == SongPart::Harmony) cycle = harmony (cycle, s);
                if (part == SongPart::Counter) cycle = counterMelody (cycle, s);
            }

            const auto lift = (part == SongPart::Lead || part == SongPart::Harmony) && patterns.liftLaterDrops && laterDrop ? 12 : 0;
            const auto keyChange = map.keyShiftOf (i);   // final-drop key change: everything from the last drop on moves up

            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0;
            const auto endQ = (map.endBar (i) - 1) * 4.0;
            for (auto offset = startQ; offset < endQ - 1.0e-9; offset += progressionQ)
                for (const auto& n : cycle)
                {
                    const auto start = offset + n.start;
                    if (start >= endQ - 1.0e-9) continue;
                    out.push_back ({ n.pitch + lift + keyChange, start, std::min (n.length, endQ - start - 0.01), n.velocity });
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
