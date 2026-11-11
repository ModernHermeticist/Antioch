#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    inline const juce::String euphoria  { "euphoria" };
    inline const juce::String lowCut    { "lowcut" };
    inline const juce::String mud       { "mud" };
    inline const juce::String presence  { "presence" };
    inline const juce::String air       { "air" };
    inline const juce::String drive     { "drive" };
    inline const juce::String satMix    { "satmix" };
    inline const juce::String mbDepth   { "mbdepth" };
    inline const juce::String mbTime    { "mbtime" };
    inline const juce::String width     { "width" };
    inline const juce::String monoBelow { "monobelow" };
    inline const juce::String autoGain  { "autogain" };
    inline const juce::String output    { "output" };
    inline const juce::String tensionWidth { "tensionwidth" };   // wider on tension chords (from Correspondence)
    inline const juce::String pitchBright  { "pitchbright" };    // air and presence follow the lead's pitch
    inline const juce::String autoDrive    { "auto_drive" };     // saturation by ear: the drive sets itself from how rich the part sounds
    inline const juce::String driveTrim    { "drive_trim" };     // ... by this much (dB, on top of Drive)

    constexpr float lowCutOffBelow = 25.0f;   // low cut values under this mean "off"
    constexpr float mudHz = 320.0f, presenceHz = 3200.0f, airHz = 10000.0f;
    constexpr float bandSplitLow = 250.0f, bandSplitHigh = 3000.0f;

    /** What the Euphoria knob adds at 100%, on top of the manual settings. */
    namespace Macro
    {
        constexpr float air = 6.0f, presence = 3.0f, mud = -3.0f, drive = 9.0f, mbDepth = 0.5f, width = 0.5f;
    }

    struct Preset
    {
        const char* name;
        float euphoria, lowCut, mud, presence, air, drive, satMix, mbDepth, mbTime, width, monoBelow;
    };
    const std::vector<Preset>& presets();

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
