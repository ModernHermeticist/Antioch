#pragma once

// Spire tips: what to change in the synth so the EQ has less to do. From what Quintessence
// measures on the track (tone against the role's target, width, dynamics, ringing peaks), in
// Spire's own terms. Fixing it at the source sounds better than fixing it after.

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace qn::spire
{
    struct Reading
    {
        int role = 0;                        // 1 kick .. 7 FX
        std::array<float, 10> toneDiff {};   // measured minus target per octave band (0 = not judged)
        float sideMinusMidDb = -100.0f, sideTargetDb = 0.0f;   // width now and the role's target (0 = none)
        float midSwingDb = 0.0f, midSwingTargetDb = 0.0f;
        std::vector<float> resonancesHz;
        bool breakdown = false;
    };

    /** Where to start a Spire patch for this role. */
    inline juce::String startingPoint (int role, bool breakdown)
    {
        switch (role)
        {
            case 2: return "Bass: Mono on, 1 voice, OSC 1 saw + OSC 2 sine an octave down; Filter 1 LP24 at 300-800 Hz with a short "
                           "Env 2 on cutoff; no unison, no stereo FX (Kybalion ducks it, Prima Materia keeps it mono).";
            case 4: return breakdown ? "Breakdown pad: 5-7 voices, detune 20-30, Env 1 attack 300-600 ms and release 1-2 s, "
                                       "Filter 1 LP open; Antioch adds the space, so keep Spire's reverb off."
                                     : "Drop chords: 5 voices, detune 20, Env 1 attack under 10 ms, Filter 1 LP with a little Env 2 "
                                       "for movement; Kybalion's gate does the rhythm.";
            case 5: return "Supersaw lead: OSC 1 and 2 Hyper saw, 7-9 voices, detune 25-35, wide; Filter 1 LP24 open with a short Env 2 "
                           "on cutoff for the bite; Spire's delay and reverb off (Antioch does space, Vibration the polish).";
            case 3: return "Hats from Spire: noise oscillator, Filter 1 HP at 6-8 kHz, Env 1 decay 40-80 ms.";
            case 7: return "FX: noise or Hyper saw with Filter 1 swept by an LFO or Env; Azoth places the transitions.";
            default: return {};
        }
    }

    /** Tips for the sound as measured, most important first. */
    inline juce::StringArray tips (const Reading& r)
    {
        juce::StringArray out;
        const auto& d = r.toneDiff;
        auto avg = [&] (int a, int b) { float s = 0.0f; int n = 0; for (int k = a; k <= b; ++k) if (d[(size_t) k] != 0.0f) { s += d[(size_t) k]; ++n; } return n > 0 ? s / (float) n : 0.0f; };
        const auto synthRole = r.role == 2 || r.role == 4 || r.role == 5 || r.role == 3 || r.role == 7;
        if (! synthRole) return out;

        const auto highs = avg (7, 8), lowMids = avg (3, 4), lows = avg (2, 3);
        if (highs < -3.0f)
            out.add ("Dull (" + juce::String (-highs, 1) + " dB short at 4-8 kHz): open Filter 1's cutoff or raise Env 2's amount on it; "
                     "or add OSC 3 an octave up (saw) about 10 dB down.");
        if (highs > 3.0f)
            out.add ("Harsh or fizzy (" + juce::String (highs, 1) + " dB over at 4-8 kHz): bring Filter 1's cutoff down, lower its resonance, "
                     "or swap a Digital/FM oscillator for a saw.");
        if (lowMids > 3.0f && r.role != 2)
            out.add ("Muddy (" + juce::String (lowMids, 1) + " dB over at 250-500 Hz): set Filter 2 to HP at 150-250 Hz, "
                     "or drop the octave-down oscillator.");
        if (lows < -3.0f && (r.role == 4 || r.role == 5))
            out.add ("Thin (" + juce::String (-lows, 1) + " dB short at 125-250 Hz): add OSC 4 an octave down about 12 dB under, "
                     "or open Filter 1's key tracking.");
        if (r.role == 2 && r.sideMinusMidDb > -14.0f)
            out.add ("The bass is wide: turn unison down to 1 voice on the low oscillators (or press Mono) and take Spire's chorus off it.");
        else if (r.sideTargetDb != 0.0f && r.sideMinusMidDb > -90.0f && r.sideMinusMidDb < r.sideTargetDb - 4.0f)
            out.add ("Narrow for a " + juce::String (r.role == 4 ? "pad" : r.role == 5 ? "lead" : "part") + ": unison 7-9 voices with detune 25-35 "
                     "and full stereo spread, or Spire's chorus at 30-40% mix.");
        else if (r.sideTargetDb != 0.0f && r.sideMinusMidDb > r.sideTargetDb + 4.0f)
            out.add ("Very wide: fewer unison voices or less spread, so it holds up in mono (club systems, phones).");
        if (r.midSwingTargetDb > 0.0f && r.midSwingDb > r.midSwingTargetDb + 4.0f)
            out.add ("Jumpy levels (" + juce::String (r.midSwingDb, 1) + " dB between quiet and loud): less velocity on Filter 1 and amp, "
                     "or a shorter Env 1 release so notes don't stack up.");
        for (auto hz : r.resonancesHz)
            out.add ("A ringing peak at " + juce::String (juce::roundToInt (hz)) + " Hz: usually Filter 1's resonance. Lower it a little, "
                     "or move the cutoff off that spot.");
        return out;
    }
}
