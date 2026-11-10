#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SectionBus.h"

namespace pm
{
    constexpr int numTypes = (int) suite::SectionType::count;
    constexpr float offDb = -60.0f;          // level at or below this = muted
    constexpr float lowCutOff = 20.0f;       // low cut at this = off
    constexpr float highCutOff = 20000.0f;   // high cut at this = off

    inline juce::String levelId (int t)   { return "lvl_" + juce::String (t); }
    inline juce::String lowCutId (int t)  { return "lc_" + juce::String (t); }
    inline juce::String highCutId (int t) { return "hc_" + juce::String (t); }
    inline const juce::String followId { "follow" }, morphId { "morph" }, rampId { "rampbuilds" }, gapId { "gap" };

    inline double morphQuarters (int i) { static const double q[] = { 0.0, 0.25, 1.0, 4.0 }; return q[juce::jlimit (0, 3, i)]; }
    inline double gapQuarters (int i)   { static const double q[] = { 0.0, 0.5, 1.0 }; return q[juce::jlimit (0, 2, i)]; }

    struct Setting { float levelDb = 0.0f, lowCut = lowCutOff, highCut = highCutOff; };

    /** One-click arrangement moves for common track roles (Intro, Groove, Breakdown, Build, Drop, Outro). */
    struct Role { const char* name; std::array<Setting, numTypes> settings; bool ramp; int gap; };

    inline const std::vector<Role>& roles()
    {
        constexpr float off = offDb, lo = lowCutOff, hi = highCutOff;
        static const std::vector<Role> r
        {
            { "Neutral (no changes)", { { { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi } } }, false, 0 },
            { "Kick",                 { { { 0, lo, hi }, { 0, lo, hi }, { off, lo, hi }, { off, lo, hi }, { 0, lo, hi }, { 0, lo, hi } } }, false, 0 },
            { "Bass",                 { { { off, lo, hi }, { 0, lo, hi }, { off, lo, hi }, { -6, 400, hi }, { 0, lo, hi }, { -3, lo, hi } } }, true, 2 },
            { "Hats & percussion",    { { { -6, lo, hi }, { 0, lo, hi }, { off, lo, hi }, { -4, lo, hi }, { 0, lo, hi }, { -3, lo, hi } } }, false, 2 },
            { "Pads & chords",        { { { -6, lo, hi }, { -3, lo, hi }, { 2, lo, hi }, { 0, 900, hi }, { -2, lo, hi }, { -6, lo, 8000 } } }, true, 0 },
            { "Lead",                 { { { off, lo, hi }, { off, lo, hi }, { -6, lo, 6000 }, { -3, 300, hi }, { 0, lo, hi }, { off, lo, hi } } }, true, 1 },
            { "Vocal",                { { { off, lo, hi }, { -3, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { -2, lo, hi }, { off, lo, hi } } }, false, 0 },
            { "Atmos & FX",           { { { 0, lo, hi }, { -3, lo, hi }, { 3, lo, hi }, { 0, lo, hi }, { -6, lo, hi }, { 0, lo, hi } } }, false, 0 },
        };
        return r;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        using Attr = AudioParameterFloatAttributes;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto id = [] (const String& s) { return ParameterID { s, 1 }; };
        auto hzText = [] (float off) { return Attr().withStringFromValueFunction ([off] (float v, int)
        {
            if (std::abs (v - off) < 0.5f || (off > 10000.0f && v > 19900.0f)) return String ("Off");
            return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz";
        }); };

        for (int t = 0; t < numTypes; ++t)
        {
            const String name = suite::sectionName ((suite::SectionType) t);
            l.add (std::make_unique<AudioParameterFloat> (id (levelId (t)), name + " Level", NormalisableRange<float> (offDb, 6.0f), 0.0f,
                                                          Attr().withStringFromValueFunction ([] (float v, int)
                                                          {
                                                              return v <= offDb + 0.05f ? String ("Off") : (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
                                                          })));
            NormalisableRange<float> lc (lowCutOff, 2000.0f); lc.setSkewForCentre (200.0f);
            NormalisableRange<float> hc (1000.0f, highCutOff); hc.setSkewForCentre (5000.0f);
            l.add (std::make_unique<AudioParameterFloat> (id (lowCutId (t)), name + " Low Cut", lc, lowCutOff, hzText (lowCutOff)));
            l.add (std::make_unique<AudioParameterFloat> (id (highCutId (t)), name + " High Cut", hc, highCutOff, hzText (highCutOff)));
        }
        l.add (std::make_unique<AudioParameterBool> (id (followId), "Follow Ouroboros", true));
        l.add (std::make_unique<AudioParameterChoice> (id (morphId), "Morph", StringArray { "Instant", "1/4 beat", "1 beat", "1 bar" }, 1));
        l.add (std::make_unique<AudioParameterBool> (id (rampId), "Ramp Through Builds", true));
        l.add (std::make_unique<AudioParameterChoice> (id (gapId), "Gap Before Drop", StringArray { "None", "1/2 beat", "1 beat" }, 0));
        return l;
    }
}
