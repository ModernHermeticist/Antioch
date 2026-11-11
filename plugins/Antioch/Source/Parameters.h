#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    constexpr int numTaps = 4;

    // ---- Global ----
    inline const juce::String sync      { "sync" };
    inline const juce::String rootNote  { "root" };
    inline const juce::String rootOct   { "octave" };
    inline const juce::String followKey { "followkey" };
    inline const juce::String followChords { "followchords" };   // the resonators move with the chords (Correspondence)
    inline const juce::String dry       { "dry" };
    inline const juce::String echoLevel { "echo" };
    inline const juce::String verbLevel { "verb" };
    inline const juce::String throwLevel{ "throwlvl" };
    inline const juce::String output    { "output" };
    inline const juce::String autoSpace { "auto_space" };   // the echo and reverb level set itself: the tail sits where the role wants it
    inline const juce::String spaceTrim { "space_trim" };   // ... by this much (dB, on the echo, reverb and freeze)

    // ---- Echo section ----
    inline const juce::String resonance { "resonance" };
    inline const juce::String resMix    { "resmix" };
    inline const juce::String damping   { "damp" };
    inline const juce::String lowCut    { "lowcut" };
    inline const juce::String echoToVerb{ "echoverb" };

    // Per-tap parameter IDs, e.g. tapId (1, Tap::pitch) -> "tap2_pitch"
    inline juce::String tapId (int tapIndex, const char* name)
    {
        return "tap" + juce::String (tapIndex + 1) + "_" + name;
    }

    namespace Tap
    {
        constexpr const char* on       = "on";
        constexpr const char* division = "div";
        constexpr const char* timeMs   = "time";
        constexpr const char* pitch    = "pitch";
        constexpr const char* feedback = "fb";
        constexpr const char* harmonic = "harm";
        constexpr const char* level    = "level";
        constexpr const char* pan      = "pan";
    }

    // ---- Euphoric reverb section ----
    namespace Verb
    {
        inline const juce::String decay    { "rv_decay" };
        inline const juce::String size     { "rv_size" };
        inline const juce::String predelay { "rv_predelay" };
        inline const juce::String damping  { "rv_damp" };
        inline const juce::String lowCut   { "rv_lowcut" };
        inline const juce::String highCut  { "rv_highcut" };
        inline const juce::String shimmer  { "rv_shimmer" };
        inline const juce::String shimInt  { "rv_shimint" };
        inline const juce::String duck     { "rv_duck" };
        inline const juce::String duckRel  { "rv_duckrel" };
        inline const juce::String width    { "rv_width" };
        inline const juce::String mod      { "rv_mod" };
        inline const juce::String freeze   { "rv_freeze" };        // hold the drop's last moment as a wash into breakdowns
        inline const juce::String freezeLevel { "rv_freezelvl" };
        inline const juce::String tension  { "rv_tension" };     // wider on tension chords (Correspondence's progression)
        inline const juce::String pump     { "rv_pump" };        // the tail ducks to the shared kick pattern
        inline const juce::String leadRoom { "rv_leadroom" };    // reverb, echo and throws duck while the lead plays (Vibration)
        inline int freezeBars (int index) { static const int b[] = { 0, 1, 2, 4 }; return b[juce::jlimit (0, 3, index)]; }

        const juce::StringArray& shimmerIntervalNames();
        float shimmerSemitones (int index);
    }

    // ---- Throw delay section ----
    namespace Throw
    {
        inline const juce::String on       { "th_on" };
        inline const juce::String latch    { "th_latch" };
        inline const juce::String autoMode { "th_auto" };
        inline const juce::String autoLen  { "th_autolen" };
        inline const juce::String division { "th_div" };
        inline const juce::String feedback { "th_fb" };
        inline const juce::String pingPong { "th_pingpong" };
        inline const juce::String lowCut   { "th_lowcut" };
        inline const juce::String highCut  { "th_highcut" };
        inline const juce::String drive    { "th_drive" };
        inline const juce::String duck     { "th_duck" };
        inline const juce::String toVerb   { "th_verb" };

        /** Auto-throw phrase length in bars for each choice (0 = off or section mode). */
        int autoBars (int index);
        constexpr int autoAtSectionEnds = 5;   // choice index: throw at the end of every Ouroboros section
        constexpr int autoAtVocalEnds = 6;     // choice index: throw the end of every vocal phrase (Solve et Coagula's marks)
        /** Auto-throw window length in quarter notes for each choice. */
        double autoLengthQuarters (int index);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
