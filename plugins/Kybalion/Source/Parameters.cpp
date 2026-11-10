#include "Parameters.h"

namespace Params
{
    using namespace juce;

    namespace
    {
        using Attr = AudioParameterFloatAttributes;
        ParameterID id (const String& s) { return { s, 1 }; }

        Attr percent() { return Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }); }
        Attr millis()  { return Attr().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; }); }

        NormalisableRange<float> skewed (float lo, float hi, float centre)
        {
            NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return r;
        }

        std::unique_ptr<AudioParameterFloat> floatParam (const String& pid, const String& name, NormalisableRange<float> r, float def, Attr a = {})
        {
            return std::make_unique<AudioParameterFloat> (id (pid), name, r, def, a);
        }
    }

    namespace Gate
    {
        double rateQuarters (int index) { static const double q[] = { 0.5, 0.25, 0.125 }; return q[jlimit (0, 2, index)]; }

        const std::vector<Pattern>& patterns()
        {
            static const std::vector<Pattern> p
            {
                { "Straight 16ths", { 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1 } },
                { "Trance classic", { 1,0,1,1, 0,1,1,0, 1,0,1,1, 0,1,1,1 } },
                { "3-3-2",          { 1,0,0,1, 0,0,1,0, 1,0,0,1, 0,0,1,0 } },
                { "Offbeat 8ths",   { 0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0 } },
                { "Gallop",         { 1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1 } },
                { "Stutter",        { 1,1,1,1, 0,0,1,1, 1,1,0,0, 1,0,1,0 } },
                { "Accents",        { 1,.35f,.7f,.35f, 1,.35f,.7f,.35f, 1,.35f,.7f,.35f, 1,.35f,.7f,.35f } },
                { "Half-time",      { 1,0,0,0, 0,0,1,0, 1,0,0,0, 0,0,1,0 } },
                { "Ramp up",        { .2f,.2f,.3f,.3f, .4f,.4f,.5f,.5f, .6f,.6f,.7f,.7f, .85f,.85f,1,1 } },
            };
            return p;
        }
    }

    namespace Duck
    {
        double rateQuarters (int index) { static const double q[] = { 2.0, 1.0, 0.5 }; return q[jlimit (0, 2, index)]; }

        const std::vector<Preset>& presets()
        {
            static const std::vector<Preset> p
            {
                //  name              depth  attack hold release curve split
                { "Bass (tight)",     0.95f, 1.0f, 25.0f, 130.0f, 1.6f,   0.0f },
                { "Pads (deep)",      0.85f, 2.0f, 30.0f, 330.0f, 1.0f,   0.0f },
                { "Leads (gentle)",   0.45f, 5.0f,  0.0f, 200.0f, 0.8f,   0.0f },
                { "Classic pump",     0.80f, 0.5f,  0.0f, 380.0f, 0.5f,   0.0f },
                { "Reverb & delays",  0.90f, 3.0f, 40.0f, 260.0f, 1.2f,   0.0f },
                { "Lows only",        0.95f, 1.0f, 20.0f, 160.0f, 1.4f, 180.0f },
            };
            return p;
        }
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterBool> (id (gateOn), "Gate On", true));
        layout.add (std::make_unique<AudioParameterBool> (id (duckOn), "Duck On", true));
        layout.add (floatParam (output, "Output", { -24.0f, 12.0f }, 0.0f,
                                Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })));

        // ---- Gate ----
        const auto& def = Gate::patterns()[1].steps;
        for (int i = 0; i < numSteps; ++i)
            layout.add (floatParam (Gate::step (i), "Gate Step " + String (i + 1), { 0.0f, 1.0f }, def[(size_t) i], percent()));
        layout.add (std::make_unique<AudioParameterChoice> (id (Gate::rate), "Gate Rate", StringArray { "1/8", "1/16", "1/32" }, 1));
        layout.add (floatParam (Gate::length,  "Gate Length",  { 0.05f, 1.0f }, 0.6f, percent()));
        layout.add (floatParam (Gate::attack,  "Gate Attack",  skewed (0.5f, 30.0f, 5.0f), 2.0f, millis()));
        layout.add (floatParam (Gate::release, "Gate Release", skewed (1.0f, 200.0f, 30.0f), 15.0f, millis()));
        layout.add (floatParam (Gate::depth,   "Gate Depth",   { 0.0f, 1.0f }, 1.0f, percent()));
        layout.add (floatParam (Gate::swing,   "Gate Swing",   { 0.0f, 0.6f }, 0.0f, percent()));
        layout.add (std::make_unique<AudioParameterInt> (id (Gate::stereo), "Gate Stereo Offset", 0, 8, 0));
        layout.add (floatParam (Gate::filter,  "Gate Filter",  { 0.0f, 1.0f }, 0.0f, percent()));
        layout.add (floatParam (Gate::res,     "Gate Resonance", { 0.0f, 1.0f }, 0.25f, percent()));

        // ---- Duck ----
        layout.add (std::make_unique<AudioParameterChoice> (id (Duck::source), "Duck Source", StringArray { "Tempo", "Sidechain" }, 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (Duck::rate), "Duck Rate", StringArray { "1/2", "1/4", "1/8" }, 1));
        layout.add (floatParam (Duck::nudge, "Duck Nudge", { -30.0f, 30.0f }, 0.0f, millis()));
        layout.add (floatParam (Duck::depth, "Duck Depth", { 0.0f, 1.0f }, 0.85f,
                                Attr().withStringFromValueFunction ([] (float v, int)
                                {
                                    return v >= 0.999f ? String ("-inf dB") : String (Decibels::gainToDecibels (1.0f - v), 1) + " dB";
                                })));
        layout.add (floatParam (Duck::attack,  "Duck Attack",  skewed (0.0f, 20.0f, 3.0f), 1.0f, millis()));
        layout.add (floatParam (Duck::hold,    "Duck Hold",    skewed (0.0f, 200.0f, 40.0f), 20.0f, millis()));
        layout.add (floatParam (Duck::release, "Duck Release", skewed (20.0f, 1000.0f, 250.0f), 250.0f, millis()));
        layout.add (floatParam (Duck::curve,   "Duck Curve",   skewed (0.3f, 3.0f, 1.0f), 1.0f,
                                Attr().withStringFromValueFunction ([] (float v, int)
                                {
                                    return v < 0.8f ? String ("Snappy") : v < 1.25f ? String ("Linear") : String ("Smooth");
                                })));
        layout.add (floatParam (Duck::split, "Duck Split", skewed (0.0f, 500.0f, 120.0f), 0.0f,
                                Attr().withStringFromValueFunction ([] (float v, int)
                                {
                                    return v < Duck::fullBandBelow ? String ("Full band") : "Below " + String (roundToInt (v)) + " Hz";
                                })));
        layout.add (floatParam (Duck::threshold, "Sidechain Threshold", { -48.0f, 0.0f }, -24.0f,
                                Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
        layout.add (std::make_unique<AudioParameterBool> (id (Duck::breakdown), "Breakdown (no ducking)", false));

        return layout;
    }
}
