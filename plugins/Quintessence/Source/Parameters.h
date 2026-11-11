#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace qn
{
    constexpr int numSlots = 6, numMbBands = 3;

    inline const juce::String autoEqId { "autoeq" }, autoMbId { "automb" }, roleId { "role" };
    inline const juce::String widthId { "width" }, widthLockId { "width_lock" };
    inline const juce::String lowCutId { "lowcut" }, highCutId { "highcut" }, lowCutLockId { "lowcut_lock" }, highCutLockId { "highcut_lock" };

    inline juce::String slotId (int slot, const char* what) { return "b" + juce::String (slot + 1) + "_" + what; }
    inline juce::String freqId (int s)   { return slotId (s, "freq"); }
    inline juce::String gainId (int s)   { return slotId (s, "gain"); }
    inline juce::String qId (int s)      { return slotId (s, "q"); }
    inline juce::String reasonId (int s) { return slotId (s, "reason"); }
    inline juce::String whenId (int s)   { return slotId (s, "when"); }
    inline juce::String lockId (int s)   { return slotId (s, "lock"); }
    inline juce::String bdId (int s)     { return slotId (s, "bd"); }     // extra gain in breakdowns (the breakdown's own correction)

    inline juce::String mbId (int band, const char* what) { return "mb" + juce::String (band + 1) + "_" + what; }
    inline juce::String thrId (int b)    { return mbId (b, "thr"); }
    inline juce::String ratioId (int b)  { return mbId (b, "ratio"); }
    inline juce::String mbLockId (int b) { return mbId (b, "lock"); }

    /** Why a band is where it is. */
    enum Reason { free = 0, roleTone, masking, mud, harshness, yours, resonance, numReasons };
    inline const juce::StringArray& reasonNames()
    {
        static const juce::StringArray n { "Free", "Role tone", "Masking", "Mud", "Harshness", "Your setting", "Resonance" };
        return n;
    }

    constexpr float lowCutOff = 20.5f, highCutOff = 19900.0f;   // at or past these the cuts are off
    constexpr float maxToneDb = 4.0f;                           // role-tone moves stay within +-4 dB
    constexpr float crossLow = 200.0f, crossHigh = 3000.0f;     // multiband split
    inline const juce::StringArray& mbBandNames() { static const juce::StringArray n { "Low", "Mid", "High" }; return n; }

    inline const std::array<float, numSlots>& defaultFreqs()
    {
        static const std::array<float, numSlots> f { 80.0f, 200.0f, 500.0f, 1200.0f, 3000.0f, 8000.0f };
        return f;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout layout();
}
