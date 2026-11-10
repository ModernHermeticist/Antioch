#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SectionBus.h"

namespace pm
{
    constexpr int numTypes = (int) suite::SectionType::count;
    constexpr int laterDrop = numTypes;          // extra column: settings for the 2nd, 3rd... drop
    constexpr int numColumns = numTypes + 1;
    constexpr float offDb = -60.0f;          // level at or below this = muted
    constexpr float lowCutOff = 20.0f;       // low cut at this = off
    constexpr float highCutOff = 20000.0f;   // high cut at this = off

    inline juce::String levelId (int t)   { return "lvl_" + juce::String (t); }
    inline juce::String lowCutId (int t)  { return "lc_" + juce::String (t); }
    inline juce::String highCutId (int t) { return "hc_" + juce::String (t); }
    inline const juce::String roleId { "role" }, trimId { "trim" }, lowManagerId { "lowmgr" };   // role: 0 = not set, k + 1 = roles()[k]
    inline const juce::String followId { "follow" }, morphId { "morph" }, rampId { "rampbuilds" }, gapId { "gap" }, laterOnId { "lateron" };

    inline double morphQuarters (int i) { static const double q[] = { 0.0, 0.25, 1.0, 4.0 }; return q[juce::jlimit (0, 3, i)]; }
    inline double gapQuarters (int i)   { static const double q[] = { 0.0, 0.5, 1.0, 4.0 }; return q[juce::jlimit (0, 3, i)]; }
    /** Ouroboros' suite-wide "Before drops": quarters of silence (a kick-only bar spares the Kick role). */
    inline double suiteGapQuarters (int dropGap, double barQ, bool isKick)
    {
        if (dropGap == 3) return isKick ? 0.0 : barQ;
        return dropGap == 1 ? 0.5 : dropGap == 2 ? 1.0 : 0.0;
    }

    struct Setting { float levelDb = 0.0f, lowCut = lowCutOff, highCut = highCutOff; };

    /** One-click arrangement moves for common track roles (Intro, Groove, Breakdown, Build, Drop, Outro). */
    struct Role { const char* name; std::array<Setting, numTypes> settings; bool ramp; int gap; bool useLater = false; Setting later {}; };

    inline const std::vector<Role>& roles()
    {
        constexpr float off = offDb, lo = lowCutOff, hi = highCutOff;
        static const std::vector<Role> r
        {
            { "Neutral (no changes)", { { { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { 0, lo, hi } } }, false, 0, false, { 0, lo, hi } },
            { "Kick",                 { { { 0, lo, hi }, { 0, lo, hi }, { off, lo, hi }, { off, lo, hi }, { 0, lo, hi }, { 0, lo, hi } } }, false, 0, false, { 0, lo, hi } },
            { "Bass",                 { { { off, lo, hi }, { 0, lo, hi }, { off, lo, hi }, { -6, 400, hi }, { 0, lo, hi }, { -3, lo, hi } } }, true, 2, false, { 0, lo, hi } },
            { "Hats & percussion",    { { { -6, lo, hi }, { 0, lo, hi }, { off, lo, hi }, { -4, lo, hi }, { 0, lo, hi }, { -3, lo, hi } } }, false, 2, true, { 1, lo, hi } },
            { "Pads & chords",        { { { -6, lo, hi }, { -3, lo, hi }, { 2, lo, hi }, { 0, 900, hi }, { -2, lo, hi }, { -6, lo, 8000 } } }, true, 0, true, { 0, lo, hi } },
            { "Lead",                 { { { off, lo, hi }, { off, lo, hi }, { -6, lo, 6000 }, { -3, 300, hi }, { 0, lo, hi }, { off, lo, hi } } }, true, 1, true, { 2, lo, hi } },
            { "Vocal",                { { { off, lo, hi }, { -3, lo, hi }, { 0, lo, hi }, { 0, lo, hi }, { -2, lo, hi }, { off, lo, hi } } }, false, 0, true, { 0, lo, hi } },
            { "Atmos & FX",           { { { 0, lo, hi }, { -3, lo, hi }, { 3, lo, hi }, { 0, lo, hi }, { -6, lo, hi }, { 0, lo, hi } } }, false, 0, true, { -3, lo, hi } },
        };
        return r;
    }

    /** Auto-role: a role index from a track name ("Kick", "Sub Bass", "Lead 2", "Vox chop"...), or -1. */
    inline int roleFromTrackName (const juce::String& trackName)
    {
        const auto n = " " + trackName.toLowerCase().replaceCharacters ("_-.", "   ") + " ";
        auto any = [&n] (std::initializer_list<const char*> words)
        {
            for (auto* w : words) if (n.contains (w)) return true;
            return false;
        };
        if (any ({ "kick", " bd ", "bassdrum" }))                                              return 1;
        if (any ({ "vox", "vocal", "voc ", "acap", "voice", "chop" }))                        return 6;
        if (any ({ "bass", " sub", "reese" }))                                                 return 2;
        if (any ({ "hat", " hh", "perc", "clap", "snare", "ride", "shaker", "top", "cymbal", "tamb" })) return 3;
        if (any ({ "fx", "atmos", "riser", "sweep", "noise", "impact", "crash", "downlift", "texture" })) return 7;
        if (any ({ "lead", "saw", "melody", "arp", "hook", "synth" }))                        return 5;
        if (any ({ "pad", "chord", "string", "keys", "piano", "pluck", "stab" }))             return 4;
        return -1;
    }

    /** Low-end manager: the low cut a role gets wherever a kick or bass track is playing
        (0 = the role owns the low end, or isn't managed). */
    inline float lowEndCut (int role)
    {
        static const float cuts[] = { 0.0f, 0.0f, 0.0f, 250.0f, 150.0f, 200.0f, 120.0f, 150.0f };
        return juce::isPositiveAndBelow (role, 8) ? cuts[role] : 0.0f;
    }

    /** Rough mix: where each role sits relative to the kick, in LU (drop loudness). */
    inline float roughMixOffset (int role)
    {
        static const float offsets[] = { 0.0f, 0.0f, -2.0f, -10.0f, -7.0f, -4.0f, -1.0f, -12.0f };
        return juce::isPositiveAndBelow (role, 8) ? offsets[role] : 0.0f;
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

        for (int t = 0; t < numColumns; ++t)
        {
            const String name = t == laterDrop ? String ("Drop 2+") : String (suite::sectionName ((suite::SectionType) t));
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
        {
            StringArray roleNames { "Not set" };
            for (const auto& r : roles()) roleNames.add (r.name);
            l.add (std::make_unique<AudioParameterChoice> (id (roleId), "Track Role", roleNames, 0));
        }
        l.add (std::make_unique<AudioParameterBool> (id (lowManagerId), "Low-End Manager", true));
        l.add (std::make_unique<AudioParameterFloat> (id (trimId), "Trim", NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })));
        l.add (std::make_unique<AudioParameterBool> (id (laterOnId), "Separate Settings for Drop 2+", false));
        l.add (std::make_unique<AudioParameterChoice> (id (morphId), "Morph", StringArray { "Instant", "1/4 beat", "1 beat", "1 bar" }, 1));
        l.add (std::make_unique<AudioParameterBool> (id (rampId), "Ramp Through Builds", true));
        l.add (std::make_unique<AudioParameterChoice> (id (gapId), "Gap Before Drop", StringArray { "None", "1/2 beat", "1 beat", "1 bar" }, 0));
        return l;
    }
}
