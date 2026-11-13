#pragma once

// The song's drums (General MIDI, for FPC or any drum kit): kick, clap, hats, ride and percussion
// in each style's language, following the map. The percussion fills the bass's gaps (the same
// interlock idea as the kits), builds roll their snare from quarters to 32nds, and the drop's
// last beat before it is left clear. Fills and crashes come from the Fills clip; these are the
// grooves between them.
//   36 kick, 39 clap, 38 snare, 42 closed hat, 46 open hat, 51 ride, 37 side stick, 75 claves

#include "ChordEngine.h"

namespace chords::drums
{
    enum : int { kick = 36, clap = 39, snare = 38, closedHat = 42, openHat = 46, ride = 51, stick = 37, claves = 75 };

    /** The drums for a song: `bass` (the song's bass clip) lets the percussion sit in its gaps. */
    inline std::vector<Note> drumClip (const suite::SectionMap& map, const SongPatterns& sp, const std::vector<Note>& bass)
    {
        using T = suite::SectionType;
        using St = patterns::Style;
        const auto style = (St) juce::jlimit (0, 6, sp.libStyle);
        std::vector<Note> out;
        auto bassAt = [&bass] (double q) { return std::any_of (bass.begin(), bass.end(), [q] (const Note& n) { return std::abs (n.start - q) < 0.02; }); };
        for (int i = 0; i < map.count; ++i)
        {
            const auto type = map.sections[(size_t) i].type;
            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
            const auto len = endQ - startQ;
            const auto intoDrop = i + 1 < map.count && map.sections[(size_t) i + 1].type == T::Drop;
            const auto clearFrom = intoDrop && sp.write.arrange ? endQ - 1.0 : endQ;   // the breath before the drop
            auto add = [&] (int note, double q, double l, int v) { if (q < clearFrom - 1.0e-9 && q >= startQ - 1.0e-9) out.push_back ({ note, q, l, juce::jlimit (1, 127, v) }); };

            if (type == T::Breakdown) continue;   // the breakdown has no drums (its fill comes from the Fills clip)
            if (type == T::Build)
            {
                // the kick for the first three quarters, then the snare roll carries it: quarters,
                // 8ths, 16ths, 32nds in the last bar, rising
                for (auto q = startQ; q < startQ + len * 0.75 - 1.0e-9; q += 1.0) add (kick, q, 0.25, 110);
                for (auto q = startQ; q < endQ - 1.0e-9;)
                {
                    const auto t = (q - startQ) / juce::jmax (1.0, len);
                    const auto step = endQ - q <= 4.0 + 1.0e-9 ? 0.125 : t < 0.5 ? 1.0 : t < 0.75 ? 0.5 : 0.25;
                    add (snare, q, step * 0.8, 50 + (int) std::lround (70.0 * t));
                    q += step;
                }
                continue;
            }
            const auto full = type == T::Drop || type == T::Groove;
            // intros and outros build up and thin out: kick only for the first 16 bars of an intro / last 16 of an outro
            auto bare = [&] (double q) { return (type == T::Intro && q - startQ < 64.0) || (type == T::Outro && endQ - q <= 64.0); };
            for (auto q = startQ; q < endQ - 1.0e-9; q += 0.25)
            {
                const auto inBeat = std::fmod (q - startQ, 1.0);
                const auto beat = (int) std::floor (std::fmod (q - startQ, 4.0) + 1.0e-9);
                const auto onBeat = inBeat < 1.0e-6, offbeat = std::abs (inBeat - 0.5) < 1.0e-6;
                const auto sixteenth = (int) std::lround (inBeat * 4.0);
                if (onBeat) add (kick, q, 0.25, 118);
                if (bare (q)) continue;
                if (onBeat && (beat == 1 || beat == 3) && (full || type == T::Outro || q - startQ >= 64.0)) add (clap, q, 0.25, 104);
                switch (style)
                {
                    case St::Psy:      // rolling 16th hats, the "a" accented
                        add (closedHat, q, 0.1, sixteenth == 3 ? 100 : sixteenth == 2 ? 82 : 64);
                        break;
                    case St::Tech:     // offbeat hats with ghosted 16ths, a ride on the beats in drops
                        if (offbeat) add (closedHat, q, 0.12, 100);
                        else if (! onBeat) add (closedHat, q, 0.08, 50);
                        if (type == T::Drop && onBeat) add (ride, q, 0.5, 80);
                        break;
                    case St::Progressive:
                        if (offbeat) add (closedHat, q, 0.12, 92);
                        if (type == T::Drop && sixteenth == 3) add (closedHat, q, 0.08, 58);
                        break;
                    default:           // uplifting, classic, vocal, orchestral: the open hat on the offbeat in drops
                        if (offbeat) add (type == T::Drop ? openHat : closedHat, q, type == T::Drop ? 0.2 : 0.12, 98);
                        if (type == T::Drop && (sixteenth == 1 || sixteenth == 3)) add (closedHat, q, 0.08, 56);
                        if (type == T::Drop && onBeat && style == St::Classic) add (ride, q, 0.5, 72);
                        break;
                }
                // percussion in the bass's gaps (drops and grooves): one per beat at most, never on the beat
                if (full && (sixteenth == 1 || sixteenth == 3) && ! bassAt (q))
                {
                    const auto pick = patterns::hash ((uint32_t) std::lround (q * 4.0) * 2654435761u ^ sp.libSeed) % 5u;
                    if (pick == 0u) add (style == St::Psy || style == St::Tech ? stick : claves, q, 0.1, 70);
                }
            }
        }
        std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return out;
    }

    //==============================================================================
    /** 2: what each part should sound like: a patch to reach for in Spire (or any synth), which of
        Spire's macros to ride, and what to layer, from the part's family, the style and the mood. */
    inline juce::String soundGuide (const SongPatterns& sp)
    {
        using T = suite::SectionType;
        using St = patterns::Style;
        const auto drop = (size_t) T::Drop;
        const auto style = (St) juce::jlimit (0, 6, sp.libStyle);
        const auto dark = sp.libMood == (int) patterns::Mood::Dark || sp.libMood == (int) patterns::Mood::Driving;
        juce::StringArray lines { "Sound guide (" + patterns::moodNames()[juce::jlimit (0, 6, sp.libMood)] + " " + patterns::styleNames()[(int) style] + ")",
                                  "Spire macros as the song clips ride them: 1 brightness, 2 drive, 3 space, 4 movement.", "" };
        auto family = [&] (SongPart p, int choice) -> juce::String
        {
            const auto k = lib::kindFor (p, choice, T::Drop);
            if (k < 0) return {};
            return juce::String (songFamily (p, k, sp, -1).name);
        };
        auto entry = [&] (const juce::String& part, const juce::String& fam, const juce::String& patch, const juce::String& macros, const juce::String& layer)
        {
            lines.add (part + (fam.isNotEmpty() ? " (" + fam + ")" : juce::String()));
            lines.add ("  Patch:  " + patch);
            lines.add ("  Ride:   " + macros);
            lines.add ("  Layer:  " + layer);
            lines.add ("");
        };
        const auto bassFam = family (SongPart::Bass, sp.bass[drop]);
        const auto rolling = bassFam.containsIgnoreCase ("roll") || bassFam.containsIgnoreCase ("gallop");
        entry ("Bass", bassFam,
               style == St::Psy ? "a short, punchy saw or square bass: filter envelope with a fast decay (80-120 ms), no sustain, mono, 1 voice"
               : rolling ? "a rolling saw bass: low-pass around 400 Hz with a short envelope, mono, little or no release"
                         : "an offbeat saw bass: two detuned saws, low-pass 600-800 Hz, a quick pluck envelope, mono",
               "1 brightness opens in drops; 2 drive up a little in the final drop",
               "a clean sine sub an octave down (Song Sub clip), the bass high-passed at 100 Hz above it");
        const auto arpFam = family (SongPart::Arp, sp.arp[drop]);
        entry ("Arp / pluck", arpFam,
               dark ? "a hard pluck: saw with a fast filter envelope (decay 150 ms), a touch of resonance, a 1/8 dotted delay"
                    : "a bright supersaw pluck: 7-9 voices with light detune, filter envelope decay 200-300 ms, reverb 1.5 s",
               "1 brightness rises through builds; 4 movement for the filter in breakdowns",
               "a softer sine or triangle pluck an octave up at -10 dB for sparkle");
        const auto chordFam = family (SongPart::Chords, sp.chords[drop]);
        entry ("Chords / pads", chordFam,
               style == St::Orchestral ? "a string ensemble or a slow supersaw pad, attack 200-400 ms, long release"
               : chordFam.containsIgnoreCase ("gate") ? "a supersaw pad with the trance gate (Kybalion: From the song's chords)"
                                                      : "a wide supersaw pad: 9 voices, detune 25-35%, low-pass 4-6 kHz, slow attack in breakdowns",
               "3 space up in breakdowns; 1 brightness opens through builds",
               "an airy noise or choir pad high-passed at 2 kHz under the breakdown");
        const auto hookFam = juce::String (songFamily (SongPart::Lead, 0, sp, -1).name);
        entry ("Lead (the hook)", hookFam,
               style == St::Psy ? "a squelchy mono lead: saw through a resonant band-pass, glide 40 ms"
                                : "the big trance lead: supersaw, 9 voices, detune 20-30%, light chorus, reverb 2-3 s and a 1/4 dotted delay",
               "1 brightness and 2 drive up in the final drop; 3 space up for breakdown phrases",
               "the Song Lead Octave and Song Lead Pluck clips on their own Spires (an octave up, and a pluck for the attack)");
        if (sp.libLayers != 0)
        {
            lines.add ("Layers: piano (a bright grand, a little compression), strings (ensemble, legato), brass (stabs with a fast attack), "
                       "choir (aahs, slow attack), timpani (an orchestral kit), acid (a 303-style squelch), chops (vocal slices), candy (a bell or glass pluck).");
            lines.add ("");
        }
        lines.add ("Drums (Song Drums and Song Fills, General MIDI): FPC or any kit. " + juce::String (style == St::Psy ? "A tight, short psy kick tuned to the key."
                   : style == St::Tech ? "A punchy kick with a long tail, a dry clap, a ride."
                                       : "A big trance kick tuned to the key (Kybalion and Quintessence help), an open hat on the offbeat."));
        return lines.joinIntoString ("\n");
    }
}
