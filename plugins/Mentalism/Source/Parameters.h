#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    inline const juce::String loudnessTarget { "loudtarget2" };   // (was "loudtarget", with "Match reference track" at 3: see setPluginState)
    inline const juce::String tonalTarget    { "tonaltarget" };
    inline const juce::String autoCoach      { "autocoach" };   // apply the coach's fixes by itself after each play-through
    inline const juce::String compareAB      { "compare_ab" };
    inline const juce::String problemWatch   { "problem_watch" };
    inline const juce::String shareTone      { "share_tone" };   // the master's tone shared out across the Quintessences, by contribution   // the problem watcher: safe fixes by themselves, the rest offered
    inline const juce::String autoBalance    { "auto_balance" };   // the balance loop: a checked step towards the rough-mix balance each pass
    inline const juce::String autoMasking    { "auto_masking" };   // resolve lasting masking by itself (carves that move with the vocal or lead)  // hear the reference's loudest part at the mix's loudness (A/B)

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
