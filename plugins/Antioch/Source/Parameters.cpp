#include "Parameters.h"
#include "Music.h"
#include "Scenes.h"

namespace Params
{
    using namespace juce;

    namespace
    {
        using Attr = AudioParameterFloatAttributes;

        Attr percent()
        {
            return Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
        }

        Attr hertz()
        {
            return Attr().withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz";
            });
        }

        Attr millis()
        {
            return Attr().withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (roundToInt (v)) + " ms";
            });
        }

        Attr seconds()
        {
            return Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; });
        }

        Attr decibels()
        {
            return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });
        }

        String semitoneText (float value, int)
        {
            const auto st = roundToInt (value);
            static const char* intervals[] = { "Unison", "m2", "M2", "m3", "M3", "4th", "Tritone",
                                               "5th", "m6", "M6", "m7", "M7" };
            const auto absSt = std::abs (st);
            const String name = absSt % 12 == 0 && absSt > 0 ? String (absSt / 12) + " Oct" : String (intervals[absSt % 12]);
            return (st > 0 ? "+" : "") + String (st) + " st (" + name + ")";
        }

        ParameterID id (const String& s) { return { s, 1 }; }

        std::unique_ptr<AudioParameterFloat> floatParam (const String& pid, const String& name, NormalisableRange<float> range,
                                                         float def, Attr attr = {})
        {
            return std::make_unique<AudioParameterFloat> (id (pid), name, range, def, attr);
        }

        NormalisableRange<float> skewed (float lo, float hi, float centre)
        {
            NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return r;
        }
    }

    namespace Verb
    {
        const StringArray& shimmerIntervalNames()
        {
            static const StringArray n { "+5th", "+Octave", "+Oct +5th", "+2 Oct", "-Octave" };
            return n;
        }

        float shimmerSemitones (int index)
        {
            static const float st[] = { 7.0f, 12.0f, 19.0f, 24.0f, -12.0f };
            return st[jlimit (0, 4, index)];
        }
    }

    namespace Throw
    {
        static const StringArray autoNames { "Off", "Every 2 bars", "Every 4 bars", "Every 8 bars", "Every 16 bars", "At section ends" };
        static const StringArray autoLenNames { "Last 1/16", "Last 1/8", "Last beat", "Last 2 beats", "Last bar" };

        int autoBars (int index)
        {
            static const int bars[] = { 0, 2, 4, 8, 16, 0 };   // "At section ends" (Ouroboros) has no fixed length
            return bars[jlimit (0, 5, index)];
        }

        double autoLengthQuarters (int index)
        {
            static const double q[] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
            return q[jlimit (0, 4, index)];
        }
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;
        using namespace suite::music;

        // ---- Global ----
        layout.add (std::make_unique<AudioParameterBool> (id (sync), "Tempo Sync", true));
        layout.add (std::make_unique<AudioParameterChoice> (id (rootNote), "Root Key", noteNames(), 0));
        layout.add (std::make_unique<AudioParameterInt> (id (rootOct), "Root Octave", 1, 6, 2));
        layout.add (std::make_unique<AudioParameterBool> (id (followKey), "Follow Key", true));
        layout.add (floatParam (dry,        "Dry",          { 0.0f, 1.0f }, 1.0f, percent()));
        layout.add (floatParam (echoLevel,  "Echo Level",   { 0.0f, 1.0f }, 0.5f, percent()));
        layout.add (floatParam (verbLevel,  "Reverb Level", { 0.0f, 1.0f }, 0.3f, percent()));
        layout.add (floatParam (throwLevel, "Throw Level",  { 0.0f, 1.0f }, 0.7f, percent()));
        layout.add (floatParam (output,     "Output",       { -24.0f, 12.0f }, 0.0f, decibels()));

        // ---- Echo ----
        layout.add (floatParam (resonance,  "Echo Resonance",     { 0.0f, 0.97f }, 0.75f, percent()));
        layout.add (floatParam (resMix,     "Echo Harmonic Blend",{ 0.0f, 1.0f }, 0.4f, percent()));
        layout.add (floatParam (damping,    "Echo Damping",       skewed (800.0f, 20000.0f, 5000.0f), 7000.0f, hertz()));
        layout.add (floatParam (lowCut,     "Echo Low Cut",       skewed (20.0f, 1500.0f, 250.0f), 150.0f, hertz()));
        layout.add (floatParam (echoToVerb, "Echo to Reverb",     { 0.0f, 1.0f }, 0.25f, percent()));

        struct TapDefaults { bool on; const char* div; float timeMs; int pitch; float fb; int harm; float level; float pan; };
        const TapDefaults defaults[numTaps] =
        {
            { true,  "1/4",  375.0f,   0, 0.35f, 1, 0.75f,  0.0f },
            { true,  "1/8D", 280.0f,   7, 0.30f, 3, 0.60f, -0.6f },
            { true,  "1/2",  750.0f,  12, 0.40f, 2, 0.50f,  0.6f },
            { false, "1/8T", 165.0f, -12, 0.25f, 4, 0.45f,  0.0f },
        };

        for (int t = 0; t < numTaps; ++t)
        {
            const auto& d = defaults[t];
            const auto prefix = "Tap " + String (t + 1) + " ";

            layout.add (std::make_unique<AudioParameterBool> (id (tapId (t, Tap::on)), prefix + "On", d.on));
            layout.add (std::make_unique<AudioParameterChoice> (id (tapId (t, Tap::division)), prefix + "Division",
                                                                divisionNames(), divisionIndex (d.div)));
            layout.add (floatParam (tapId (t, Tap::timeMs), prefix + "Time", skewed (10.0f, 2500.0f, 400.0f), d.timeMs, millis()));
            layout.add (floatParam (tapId (t, Tap::pitch), prefix + "Pitch", { -24.0f, 24.0f, 1.0f }, (float) d.pitch,
                                    Attr().withStringFromValueFunction (semitoneText)));
            layout.add (floatParam (tapId (t, Tap::feedback), prefix + "Feedback", { 0.0f, 0.95f }, d.fb, percent()));
            layout.add (std::make_unique<AudioParameterInt> (id (tapId (t, Tap::harmonic)), prefix + "Harmonic", 1, 16, d.harm));
            layout.add (floatParam (tapId (t, Tap::level), prefix + "Level", { 0.0f, 1.0f }, d.level, percent()));
            layout.add (floatParam (tapId (t, Tap::pan), prefix + "Pan", { -1.0f, 1.0f }, d.pan,
                                    Attr().withStringFromValueFunction ([] (float v, int)
                                    {
                                        const auto p = roundToInt (v * 100.0f);
                                        return p == 0 ? String ("C") : String (std::abs (p)) + (p < 0 ? "L" : "R");
                                    })));
        }

        // ---- Euphoric reverb ----
        layout.add (floatParam (Verb::decay,    "Reverb Decay",     skewed (0.5f, 20.0f, 4.0f), 6.0f, seconds()));
        layout.add (floatParam (Verb::size,     "Reverb Size",      { 0.0f, 1.0f }, 0.75f, percent()));
        layout.add (floatParam (Verb::predelay, "Reverb Pre-delay", skewed (0.0f, 300.0f, 60.0f), 30.0f, millis()));
        layout.add (floatParam (Verb::damping,  "Reverb Damping",   skewed (1000.0f, 20000.0f, 6000.0f), 9000.0f, hertz()));
        layout.add (floatParam (Verb::lowCut,   "Reverb Low Cut",   skewed (20.0f, 1500.0f, 300.0f), 350.0f, hertz()));
        layout.add (floatParam (Verb::highCut,  "Reverb High Cut",  skewed (1000.0f, 20000.0f, 6000.0f), 12000.0f, hertz()));
        layout.add (floatParam (Verb::shimmer,  "Shimmer",          { 0.0f, 1.0f }, 0.2f, percent()));
        layout.add (std::make_unique<AudioParameterChoice> (id (Verb::shimInt), "Shimmer Interval", Verb::shimmerIntervalNames(), 1));
        layout.add (floatParam (Verb::duck,     "Reverb Duck",      { 0.0f, 1.0f }, 0.5f, percent()));
        layout.add (floatParam (Verb::duckRel,  "Reverb Duck Release", skewed (50.0f, 2000.0f, 300.0f), 350.0f, millis()));
        layout.add (floatParam (Verb::width,    "Reverb Width",     { 0.0f, 1.5f }, 1.2f, percent()));
        layout.add (floatParam (Verb::mod,      "Reverb Modulation",{ 0.0f, 1.0f }, 0.4f, percent()));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { Verb::freeze, 1 }, "Reverb Freeze Into Breakdowns",
                                                                  juce::StringArray { "Off", "1 bar", "2 bars", "4 bars" }, 0));
        layout.add (floatParam (Verb::freezeLevel, "Reverb Freeze Level", { 0.0f, 1.0f }, 0.7f, percent()));
        layout.add (floatParam (Verb::tension, "Reverb Tension Width", { 0.0f, 1.0f }, 0.3f, percent()));

        // ---- Throw ----
        layout.add (std::make_unique<AudioParameterBool> (id (Throw::on), "Throw", false));
        layout.add (std::make_unique<AudioParameterBool> (id (Throw::latch), "Throw Latch", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (Throw::autoMode), "Auto Throw", Throw::autoNames, 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (Throw::autoLen), "Auto Throw Length", Throw::autoLenNames, 2));
        layout.add (std::make_unique<AudioParameterChoice> (id (Throw::division), "Throw Time", divisionNames(), divisionIndex ("1/8D")));
        layout.add (floatParam (Throw::feedback, "Throw Feedback",  { 0.0f, 0.95f }, 0.6f, percent()));
        layout.add (floatParam (Throw::pingPong, "Throw Ping-Pong", { 0.0f, 1.0f }, 1.0f, percent()));
        layout.add (floatParam (Throw::lowCut,   "Throw Low Cut",   skewed (20.0f, 2000.0f, 400.0f), 350.0f, hertz()));
        layout.add (floatParam (Throw::highCut,  "Throw High Cut",  skewed (1000.0f, 20000.0f, 6000.0f), 6500.0f, hertz()));
        layout.add (floatParam (Throw::drive,    "Throw Drive",     { 0.0f, 1.0f }, 0.25f, percent()));
        layout.add (floatParam (Throw::duck,     "Throw Duck",      { 0.0f, 1.0f }, 0.4f, percent()));
        layout.add (floatParam (Throw::toVerb,   "Throw to Reverb", { 0.0f, 1.0f }, 0.35f, percent()));

        suite::SceneEngine::addParameters (layout);

        return layout;
    }
}
