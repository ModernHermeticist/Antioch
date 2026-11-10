#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    // ---- Build control ----
    inline const juce::String build      { "build" };      // manual macro 0..1
    inline const juce::String trigger    { "trigger" };    // on = start a timed build
    inline const juce::String length     { "length" };     // timed build length in bars
    inline const juce::String curve      { "curve" };      // shape of the ramp
    inline const juce::String dropCut    { "dropcut" };    // cut tails at the drop

    // ---- Effects ----
    inline const juce::String filterMax  { "filtermax" };  // highest high-pass cutoff
    inline const juce::String filterRes  { "filterres" };
    inline const juce::String wash       { "wash" };       // reverb send amount
    inline const juce::String washDecay  { "washdecay" };
    inline const juce::String noise      { "noise" };
    inline const juce::String riser      { "riser" };
    inline const juce::String riserRange { "riserrange" };
    inline const juce::String stutter    { "stutter" };
    inline const juce::String stutterMax { "stuttermax" };
    inline const juce::String widen      { "widen" };

    // ---- Key & output ----
    inline const juce::String rootNote   { "root" };
    inline const juce::String followKey  { "followkey" };
    inline const juce::String output     { "output" };
    inline const juce::String follow     { "follow" };     // follow Ouroboros Build sections

    int lengthBars (int index);
    int riserOctaves (int index);
    double stutterMaxQuarters (int index);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
