#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChordEngine.h"

namespace Params
{
    inline const juce::String rootNote     { "root" };
    inline const juce::String scale        { "scale" };
    inline const juce::String broadcast    { "broadcast" };
    inline const juce::String count        { "count" };
    inline const juce::String perChord     { "perchord" };
    inline const juce::String voicing      { "voicing" };
    inline const juce::String voiceLead    { "voicelead" };
    inline const juce::String registerOct  { "register" };
    inline const juce::String addBass      { "addbass" };
    inline const juce::String chordPattern { "chordpat" };
    inline const juce::String bassPattern  { "basspat" };
    inline const juce::String bassOctave   { "bassoct" };
    inline const juce::String arpPattern   { "arppat" };
    inline const juce::String arpOctaves   { "arpoct" };
    inline const juce::String preview      { "preview" };

    inline juce::String slotDegree (int slot)  { return "slot" + juce::String (slot + 1) + "_deg"; }
    inline juce::String slotQuality (int slot) { return "slot" + juce::String (slot + 1) + "_qual"; }

    double quartersPerChord (int index);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Reads all parameters into a chords::Settings snapshot. */
    chords::Settings readSettings (const juce::AudioProcessorValueTreeState&);
}
