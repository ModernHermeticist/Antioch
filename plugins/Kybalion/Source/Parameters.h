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
        inline const juce::String pan      { "g_pan" };       // steps move around the stereo field: off, ping-pong, L-C-R, bounce
        enum PanMode { PanOff = 0, PanPingPong, PanLCR, PanBounce };
        /** Where step `i` sits (-1 left .. +1 right) for a pan mode (at full width). */
        inline float stepPan (int mode, int i)
        {
            static const float lcr[] = { -1.0f, 0.0f, 1.0f }, bounce[] = { -1.0f, 0.0f, 1.0f, 0.0f };
            switch (mode)
            {
                case PanPingPong: return i % 2 == 0 ? -1.0f : 1.0f;
                case PanLCR:      return lcr[i % 3];
                case PanBounce:   return bounce[i % 4];
                default:          return 0.0f;
            }
        }

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
        inline const juce::String flip      { "d_flip" };        // flip this track's polarity (kick and bass phase check)
        inline const juce::String focus     { "d_focus" };       // duck the whole sound, only around the kick's note, or decide (Auto)
        enum Focus { FocusWhole = 0, FocusKickNote = 1, FocusAuto = 2 };

        enum Source { Tempo = 0, Sidechain = 1, SuiteKick = 2 };
        double rateQuarters (int index);
        constexpr float fullBandBelow = 30.0f; // split values under this mean "full band"

        struct Preset { const char* name; float depth, attack, hold, release, curve, split; };
        const std::vector<Preset>& presets();
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
