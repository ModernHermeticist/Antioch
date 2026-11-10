#pragma once

// Session presets: one choice sets up a new song across the suite. Ouroboros sends
// "style + key" on the SessionBus; every suite plugin applies its part (SuiteProcessor::
// applySession): Correspondence the key and a progression, Mentalism the loudness
// target, the scene plugins their suggested scenes; Ouroboros builds the map itself.

#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>

namespace suite
{
    struct SessionStyle
    {
        const char* name;
        double bpm;
        int drops;
        double minutes;
        bool djIntro, longBreakdown;
        int progression;      // Correspondence preset index
        int loudnessTarget;   // Mentalism loudness target index
        int finalLift;        // Ouroboros final drop key change (semitones)
    };

    inline const std::vector<SessionStyle>& sessionStyles()
    {
        static const std::vector<SessionStyle> s
        {
            { "Uplifting anthem",     138.0, 2, 7.0, true,  true,  8, 0, 1 },   // Epic 8, club master, key up for the last drop
            { "Uplifting radio edit", 138.0, 1, 3.5, false, false, 0, 2, 0 },   // Anthem, streaming
            { "Tech trance",          140.0, 2, 6.5, true,  false, 6, 1, 0 },   // Driving, loud club
            { "Progressive / melodic", 132.0, 2, 7.0, true, true,  3, 0, 0 },   // Emotional, club master
            { "Emotional / vocal",    136.0, 2, 6.5, true,  true,  4, 0, 1 },   // Heartbreak, club master
        };
        return s;
    }

    /** Song length in bars for a style, rounded to 8-bar phrases. */
    inline int sessionBars (const SessionStyle& s) { return juce::jmax (32, (int) std::round (s.minutes * s.bpm / 4.0 / 8.0) * 8); }

    /** Mood: one dial from dark to euphoric that colours a new song across the suite -
        Correspondence's progression, Ouroboros' intensity and breakdown depth, Vibration's
        drop euphoria. Index 0 leaves everything to the style. */
    struct SessionMood
    {
        const char* name;
        int progression;        // Correspondence preset index, -1 = the style's
        int intensity;          // Ouroboros intensity (%)
        int breakdownEnergy;    // drawn breakdown energy (0..100), -1 = default
        float dropEuphoria;     // Vibration: added to the Drop scene's euphoria offset
    };

    inline const std::vector<SessionMood>& sessionMoods()
    {
        static const std::vector<SessionMood> m
        {
            { "As the style", -1, 100, -1,  0.0f },
            { "Euphoric",      2, 120, -1,  0.15f },   // Euphoria (VI VII i i)
            { "Hopeful",       5, 105, -1,  0.05f },   // Ascension (iv VI VII i)
            { "Melancholic",   4,  90, 25, -0.05f },   // Heartbreak (i III VII iv)
            { "Dark",          6, 100, 20, -0.15f },   // Driving (i i VI VII)
        };
        return m;
    }

    inline const SessionMood& sessionMood (int index)
    {
        return sessionMoods()[(size_t) juce::jlimit (0, (int) sessionMoods().size() - 1, index)];
    }

    struct SessionRequest { int style = 0, root = 9, scale = 0, mood = 0; };

    namespace SessionBus
    {
        uint32_t send (const SessionRequest&) noexcept;
        uint32_t latest() noexcept;
        bool read (uint32_t seq, SessionRequest& out) noexcept;
        void acknowledge (uint32_t seq) noexcept;
        int acknowledgements (uint32_t seq) noexcept;
        void reset() noexcept;
    }
}
