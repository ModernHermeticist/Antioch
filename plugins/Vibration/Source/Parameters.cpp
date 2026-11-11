#include "Parameters.h"
#include "Scenes.h"

namespace Params
{
    using namespace juce;

    namespace
    {
        using Attr = AudioParameterFloatAttributes;
        ParameterID id (const String& s) { return { s, 1 }; }

        Attr percent() { return Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }); }
        Attr db()      { return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }); }
        Attr hz (float offBelow)
        {
            return Attr().withStringFromValueFunction ([offBelow] (float v, int)
            {
                return v < offBelow ? String ("Off") : String (roundToInt (v)) + " Hz";
            });
        }

        NormalisableRange<float> skewed (float lo, float hi, float centre)
        {
            NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return r;
        }

        std::unique_ptr<AudioParameterFloat> param (const String& pid, const String& name, NormalisableRange<float> r, float def, Attr a)
        {
            return std::make_unique<AudioParameterFloat> (id (pid), name, r, def, a);
        }
    }

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> p
        {
            //  name             euph  lowcut  mud   pres  air  drive mix   mb    time  width mono
            { "Supersaw lead",   0.5f, 180.0f, -2.0f, 1.5f, 3.0f, 6.0f, 0.5f, 0.35f, 1.0f, 1.3f, 200.0f },
            { "Pluck",           0.4f, 250.0f, -1.5f, 2.0f, 2.0f, 3.0f, 0.4f, 0.45f, 0.6f, 1.2f, 220.0f },
            { "Pad",             0.4f, 150.0f, -3.0f, 0.0f, 4.0f, 2.0f, 0.3f, 0.25f, 1.6f, 1.5f, 180.0f },
            { "Vocal chop",      0.3f, 120.0f, -1.0f, 2.5f, 3.0f, 4.0f, 0.4f, 0.30f, 0.8f, 1.1f, 150.0f },
            { "Arp",             0.4f, 220.0f, -1.5f, 1.5f, 2.5f, 4.0f, 0.4f, 0.40f, 0.7f, 1.25f, 200.0f },
            { "Clean (neutral)", 0.0f,  20.0f,  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.00f, 1.0f, 1.0f,  20.0f },
        };
        return p;
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;
        const auto& d = presets().front();

        layout.add (param (euphoria,  "Euphoria",       { 0.0f, 1.0f }, d.euphoria, percent()));
        layout.add (param (lowCut,    "Low Cut",        skewed (20.0f, 600.0f, 150.0f), d.lowCut, hz (lowCutOffBelow)));
        layout.add (param (mud,       "Mud (320 Hz)",   { -9.0f, 3.0f }, d.mud, db()));
        layout.add (param (presence,  "Presence (3.2 kHz)", { -6.0f, 6.0f }, d.presence, db()));
        layout.add (param (air,       "Air (10 kHz)",   { -6.0f, 9.0f }, d.air, db()));
        layout.add (param (drive,     "Drive",          { 0.0f, 24.0f }, d.drive, db()));
        layout.add (param (satMix,    "Saturation Mix", { 0.0f, 1.0f }, d.satMix, percent()));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoDrive, 1 }, "Auto Drive", false));
        layout.add (param (driveTrim, "Drive Trim", { -12.0f, 12.0f }, 0.0f, db()));
        layout.add (param (mbDepth,   "Multiband Depth",{ 0.0f, 1.0f }, d.mbDepth, percent()));
        layout.add (param (mbTime,    "Multiband Time", skewed (0.25f, 3.0f, 1.0f), d.mbTime,
                           Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + "x"; })));
        layout.add (param (width,     "Width",          { 0.0f, 2.0f }, d.width, percent()));
        layout.add (param (monoBelow, "Mono Below",     skewed (20.0f, 400.0f, 150.0f), d.monoBelow, hz (lowCutOffBelow)));
        layout.add (std::make_unique<AudioParameterBool> (id (autoGain), "Auto Gain", true));
        layout.add (param (output,    "Output",         { -24.0f, 12.0f }, 0.0f, db()));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { tensionWidth, 1 }, "Tension Width",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pitchBright, 1 }, "Pitch-tracking Brightness",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        suite::SceneEngine::addParameters (layout);
        return layout;
    }
}
