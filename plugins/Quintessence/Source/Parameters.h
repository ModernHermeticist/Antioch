#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace qn
{
    constexpr int numSlots = 12, numMbBands = 7, numCross = numMbBands - 1;

    inline const juce::String autoEqId { "autoeq" }, autoMbId { "automb" }, roleId { "role" }, subRoleId { "subrole" };
    inline const juce::String widthId { "width" }, widthLockId { "width_lock" };
    inline const juce::String transitionsId { "transitions" };
    inline const juce::String chordAwareId { "chord_aware" };   // pads and leads leave room at the chord's root, where the bass plays it   // builds tighten the glue and widen the image; the drop lets go
    inline const juce::String punchId { "punch" }, glueThrId { "glue_thr" }, deessFreqId { "deess_freq" }, deessDepthId { "deess_depth" };
    constexpr std::array<float, 4> sibilanceCentres { 4500.0f, 6000.0f, 7500.0f, 9000.0f };
    inline const juce::String lowCutId { "lowcut" }, highCutId { "highcut" }, lowCutLockId { "lowcut_lock" }, highCutLockId { "highcut_lock" };

    inline juce::String slotId (int slot, const char* what) { return "b" + juce::String (slot + 1) + "_" + what; }
    inline juce::String freqId (int s)   { return slotId (s, "freq"); }
    inline juce::String gainId (int s)   { return slotId (s, "gain"); }
    inline juce::String qId (int s)      { return slotId (s, "q"); }
    inline juce::String reasonId (int s) { return slotId (s, "reason"); }
    inline juce::String whenId (int s)   { return slotId (s, "when"); }
    inline juce::String lockId (int s)   { return slotId (s, "lock"); }
    inline juce::String bdId (int s)     { return slotId (s, "bd"); }     // extra gain in breakdowns (the breakdown's own correction)
    inline juce::String grId (int s)     { return slotId (s, "gr"); }     // extra gain in grooves, intro and outro (their own correction, with the autopilot)

    inline juce::String mbId (int band, const char* what) { return "mb" + juce::String (band + 1) + "_" + what; }
    inline juce::String thrId (int b)    { return mbId (b, "thr"); }
    inline juce::String ratioId (int b)  { return mbId (b, "ratio"); }
    inline juce::String mbLockId (int b) { return mbId (b, "lock"); }

    /** Why a band is where it is. */
    enum Reason { free = 0, roleTone, masking, mud, harshness, yours, resonance, masterBalance, numReasons };
    inline const juce::StringArray& reasonNames()
    {
        static const juce::StringArray n { "Free", "Role tone", "Masking", "Mud", "Harshness", "Your setting", "Resonance", "Master balance" };
        return n;
    }

    constexpr float lowCutOff = 20.5f, highCutOff = 19900.0f;   // at or past these the cuts are off
    constexpr float maxToneDb = 4.0f;                           // role-tone moves stay within +-4 dB
    /** The multiband's six splits (4th-order Linkwitz-Riley; the seven bands add back up flat). */
    inline const std::array<float, numCross>& crossovers() { static const std::array<float, numCross> c { 60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f }; return c; }
    inline const juce::StringArray& mbBandNames() { static const juce::StringArray n { "Sub", "Low", "Low-mid", "Mid", "Upper-mid", "Presence", "Air" }; return n; }
    /** Old projects had three compressor bands (low < 200 Hz, mid, high > 3 kHz): which of those each new band takes its settings from. */
    inline int legacyBandFor (int band) { return band <= 2 ? 0 : band <= 4 ? 1 : 2; }

    inline const std::array<float, numSlots>& defaultFreqs()
    {
        static const std::array<float, numSlots> f { 40.0f, 80.0f, 140.0f, 250.0f, 400.0f, 650.0f, 1000.0f, 1600.0f, 2500.0f, 4000.0f, 6500.0f, 11000.0f };
        return f;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout layout();
}
