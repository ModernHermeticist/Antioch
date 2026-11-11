#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace Params
{
    constexpr int numSteps = 16;

    // ---- Master ----
    inline const juce::String gateOn   { "gate_on" };
    inline const juce::String duckOn   { "duck_on" };
    inline const juce::String output   { "output" };
    inline const juce::String follow   { "follow" };   // Breakdown during Ouroboros breakdown/build sections

    // ---- Rhythm (gate) ----
    namespace Gate
    {
        inline const juce::String rate     { "g_rate" };
        inline const juce::String length   { "g_length" };
        inline const juce::String attack   { "g_attack" };
        inline const juce::String release  { "g_release" };
        inline const juce::String depth    { "g_depth" };
        inline const juce::String swing    { "g_swing" };
        inline const juce::String stereo   { "g_stereo" };
        inline const juce::String filter   { "g_filter" };
        inline const juce::String res      { "g_res" };
        inline const juce::String dynamic  { "g_dynamic" };   // the gate deepens through each drop (with Ouroboros)

        inline juce::String step (int i) { return "g_step" + juce::String (i + 1); }
        double rateQuarters (int index);

        struct Pattern { const char* name; std::array<float, numSteps> steps; };
        const std::vector<Pattern>& patterns();
    }

    // ---- Polarity (ducking) ----
    namespace Fill
    {
        inline const juce::String when  { "g_fills" };       // Off, before drops, before drops & breakdowns
        inline const juce::String style { "g_fillstyle" };   // Auto, Stutter, Half-time, Gap, Roll
        enum When { Off = 0, BeforeDrops, BeforeDropsAndBreakdowns };
        inline const juce::String chords { "g_chords" };     // follow Correspondence's chord changes: off / accent / restart
        enum Chords { ChordsOff = 0, ChordsAccent, ChordsRestart };
    }

    namespace Duck
    {
        inline const juce::String source    { "d_source" };
        inline const juce::String rate      { "d_rate" };
        inline const juce::String nudge     { "d_nudge" };
        inline const juce::String depth     { "d_depth" };
        inline const juce::String attack    { "d_attack" };
        inline const juce::String hold      { "d_hold" };
        inline const juce::String release   { "d_release" };
        inline const juce::String curve     { "d_curve" };
        inline const juce::String split     { "d_split" };
        inline const juce::String threshold { "d_threshold" };
        inline const juce::String breakdown { "d_breakdown" };
        inline const juce::String shareKick { "d_sharekick" };   // on the kick track: share its pattern with the suite

        enum Source { Tempo = 0, Sidechain = 1, SuiteKick = 2 };
        double rateQuarters (int index);
        constexpr float fullBandBelow = 30.0f; // split values under this mean "full band"

        struct Preset { const char* name; float depth, attack, hold, release, curve, split; };
        const std::vector<Preset>& presets();
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
