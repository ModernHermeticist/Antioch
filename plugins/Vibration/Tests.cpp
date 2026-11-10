// Offline tests for Vibration. Usage: VibrationTests [output-dir]

#include "PluginProcessor.h"
#include "TestUtils.h"
#include "MorphBus.h"
#include "SceneUI.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<VibrationProcessor> makeNeutral()
    {
        suite::SectionBus::withdraw();   // no leftover arrangement from another test
        auto p = std::make_unique<VibrationProcessor>();
        p->applyPreset ((int) Params::presets().size() - 1); // Clean (neutral)
        set (*p, Params::autoGain, 0.0f);
        prepare (*p);
        return p;
    }

    /** Stereo signal: `mid` sine in both channels plus `side` sine in opposite polarity. */
    InputFn signal (float hz, float mid, float side = 0.0f, float sideHz = 0.0f)
    {
        return [=] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto m = mid * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * t);
                const auto s = side * (float) std::sin (2.0 * juce::MathConstants<double>::pi * (sideHz > 0.0f ? sideHz : hz) * t);
                b.setSample (0, i, m + s);
                b.setSample (1, i, m - s);
            }
        };
    }

    float gainDb (VibrationProcessor& p, float hz)
    {
        juce::AudioBuffer<float> audio;
        render (p, 1.0, signal (hz, 0.1f), &audio);
        return juce::Decibels::gainToDecibels (rmsBetween (audio, 0.5, 1.0) / (0.1f / std::sqrt (2.0f)));
    }

    float sideRms (const juce::AudioBuffer<float>& a, double t0, double t1)
    {
        const auto s0 = (int) (t0 * sampleRate), s1 = (int) (t1 * sampleRate);
        double sum = 0.0;
        for (int i = s0; i < s1; ++i) { const auto s = 0.5 * (a.getSample (0, i) - a.getSample (1, i)); sum += s * s; }
        return (float) std::sqrt (sum / (s1 - s0));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Neutral settings are flat");
    {
        bool flat = true;
        juce::String detail;
        for (float hz : { 60.0f, 250.0f, 1000.0f, 3000.0f, 8000.0f, 14000.0f })
        {
            auto p = makeNeutral();
            const auto g = gainDb (*p, hz);
            detail << juce::String ((int) hz) << " Hz " << juce::String (g, 2) << " dB  ";
            if (std::abs (g) > 0.3f) flat = false;
        }
        check (flat, "within 0.3 dB from 60 Hz to 14 kHz (" + detail.trim() + ")");
    }

    section ("Latency is reported correctly");
    {
        auto p = makeNeutral();
        juce::AudioBuffer<float> audio;
        render (*p, 0.1, [] (juce::AudioBuffer<float>& b, int64_t pos) { if (pos == 0) { b.setSample (0, 100, 1.0f); b.setSample (1, 100, 1.0f); } }, &audio);
        int peakAt = 0; float peak = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (std::abs (audio.getSample (0, i)) > peak) { peak = std::abs (audio.getSample (0, i)); peakAt = i; }
        const auto measured = peakAt - 100, reported = p->getLatencySamples();
        check (std::abs (measured - reported) <= 2, "impulse delay " + juce::String (measured) + " vs reported " + juce::String (reported) + " samples");
    }

    section ("Tone controls and the Euphoria macro");
    {
        auto p = makeNeutral();
        set (*p, Params::air, 6.0f);
        const auto airGain = gainDb (*p, 14000.0f);
        check (airGain > 5.0f && airGain < 7.0f, "Air +6 dB lifts 14 kHz (" + juce::String (airGain, 2) + " dB)");

        auto q = makeNeutral();
        set (*q, Params::mud, -6.0f);
        const auto mudGain = gainDb (*q, 320.0f);
        check (mudGain < -5.0f, "Mud -6 dB cuts 320 Hz (" + juce::String (mudGain, 2) + " dB)");
        check (std::abs (gainDb (*q, 3000.0f)) < 0.5f, "... and leaves 3 kHz alone");

        auto c = makeNeutral();
        set (*c, Params::lowCut, 200.0f);
        const auto cutGain = gainDb (*c, 50.0f);
        check (cutGain < -18.0f, "Low cut 200 Hz removes 50 Hz (" + juce::String (cutGain, 1) + " dB)");

        auto m = makeNeutral();
        set (*m, Params::euphoria, 1.0f);
        const auto e = m->effective();
        check (std::abs (e.air - Params::Macro::air) < 0.01f && std::abs (e.mbDepth - Params::Macro::mbDepth) < 0.01f,
               "Euphoria at 100% adds its documented amounts");
    }

    section ("Width: mono lows, wider highs");
    {
        auto p = makeNeutral();
        set (*p, Params::monoBelow, 200.0f);
        juce::AudioBuffer<float> audio;
        render (*p, 1.0, signal (80.0f, 0.0f, 0.2f), &audio);
        check (sideRms (audio, 0.5, 1.0) < 0.2f * 0.2f / std::sqrt (2.0f), "side content at 80 Hz is made mono");

        auto q = makeNeutral();
        set (*q, Params::width, 2.0f);
        render (*q, 1.0, signal (1000.0f, 0.0f, 0.1f), &audio);
        const auto w = juce::Decibels::gainToDecibels (sideRms (audio, 0.5, 1.0) / (0.1f / std::sqrt (2.0f)));
        check (std::abs (w - 6.0f) < 0.5f, "Width 200% doubles the sides (" + juce::String (w, 2) + " dB)");
    }

    section ("Multiband brings quiet parts up and loud parts down");
    {
        auto p = makeNeutral();
        set (*p, Params::mbDepth, 1.0f);
        juce::AudioBuffer<float> audio;
        render (*p, 8.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto amp = std::fmod (t, 4.0) < 2.0 ? 0.5f : 0.01f;   // 2 s loud, 2 s quiet (-34 dB)
                const auto v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        }, &audio);
        const auto loud = rmsBetween (audio, 5.0, 5.9), quiet = rmsBetween (audio, 7.0, 7.9); // settled sections
        const auto range = juce::Decibels::gainToDecibels (loud / quiet);
        check (range < 15.0f, "34 dB of dynamics squeezed to " + juce::String (range, 1) + " dB");
    }

    section ("Auto gain keeps the level matched");
    {
        auto p = makeNeutral();
        set (*p, Params::autoGain, 1.0f);
        set (*p, Params::drive, 24.0f);
        set (*p, Params::satMix, 1.0f);
        set (*p, Params::air, 9.0f);
        juce::AudioBuffer<float> audio;
        render (*p, 4.0, signal (440.0f, 0.2f), &audio);
        const auto diff = juce::Decibels::gainToDecibels (rmsBetween (audio, 3.0, 4.0) / (0.2f / std::sqrt (2.0f)));
        check (std::abs (diff) < 1.0f, "output within 1 dB of input despite heavy drive (" + juce::String (diff, 2) + " dB)");
    }

    section ("New song (session preset)");
    {
        auto p = makeNeutral();
        set (*p, suite::SceneEngine::enabledId, 0.0f);
        p->scenes.clearAll();
        suite::SessionBus::send ({ 0, 9, 0 });
        p->pollSession();
        bool anyOffset = false;
        for (int t = 0; t < suite::numSceneSlots; ++t)
            for (int i = 0; i < p->scenes.getNumParams(); ++i) anyOffset = anyOffset || std::abs (p->scenes.getOffset (t, i)) > 1.0e-6f;
        check (p->apvts.getRawParameterValue (suite::SceneEngine::enabledId)->load() > 0.5f && anyOffset, "turns scenes on with the suggested offsets");

        const auto drop = (int) suite::SectionType::Drop;
        const auto usual = p->scenes.getOffset (drop, VibrationProcessor::sEuphoria);
        suite::SessionBus::send ({ 0, 9, 0, 1 });   // Euphoric
        p->pollSession();
        const auto euphoric = p->scenes.getOffset (drop, VibrationProcessor::sEuphoria);
        suite::SessionBus::send ({ 0, 9, 0, 4 });   // Dark
        p->pollSession();
        const auto dark = p->scenes.getOffset (drop, VibrationProcessor::sEuphoria);
        check (euphoric > usual + 0.1f && dark < usual - 0.1f, "the mood pushes the drop's euphoria up (Euphoric) or down (Dark): "
                   + juce::String (dark, 2) + " < " + juce::String (usual, 2) + " < " + juce::String (euphoric, 2));
    }

    section ("Final-drop snapshot morph");
    {
        suite::MorphBus::withdraw();
        auto p = makeNeutral();
        auto norm = [&p] (const juce::String& id) { return p->apvts.getParameter (id)->getValue(); };
        set (*p, Params::euphoria, 80.0f);
        set (*p, Params::autoGain, 0.0f);
        p->captureSnapshot (1);                       // B: big Euphoria, auto gain off
        const auto target = norm (Params::euphoria);
        set (*p, Params::euphoria, 0.0f);
        set (*p, Params::autoGain, 1.0f);              // the user's own settings
        const auto base = norm (Params::euphoria);

        suite::MorphBus::publish (1, 0.5f);           // half-way through the build
        p->updateMorph();
        check (std::abs (norm (Params::euphoria) - 0.5f * (base + target)) < 0.01f, "half-way through the build: half-way to the snapshot");
        check (norm (Params::autoGain) > 0.5f, "switches wait for the drop");

        juce::MemoryBlock midMorph;
        p->getStateInformation (midMorph);            // a save during the morph

        suite::MorphBus::publish (1, 1.0f);           // the drop
        p->updateMorph();
        check (std::abs (norm (Params::euphoria) - target) < 0.01f && norm (Params::autoGain) < 0.5f, "in the drop: exactly the snapshot");

        suite::MorphBus::publish (1, 0.0f);           // playhead back before the build
        p->updateMorph();
        check (std::abs (norm (Params::euphoria) - base) < 0.001f && norm (Params::autoGain) > 0.5f && ! p->isMorphing(),
               "before the build: the user's own settings come back");

        auto q = makeNeutral();
        q->setStateInformation (midMorph.getData(), (int) midMorph.getSize());
        check (std::abs (q->apvts.getParameter (Params::euphoria)->getValue() - base) < 0.001f,
               "a project saved mid-morph reloads with the user's settings");

        suite::MorphBus::publish (2, 1.0f);           // an empty slot: nothing happens
        p->updateMorph();
        check (std::abs (norm (Params::euphoria) - base) < 0.001f, "morphing to an empty slot changes nothing");
        suite::MorphBus::withdraw();
    }

    section ("Stress: everything maxed on full-scale noise");
    {
        auto p = std::make_unique<VibrationProcessor>();
        for (auto* id : { &Params::euphoria, &Params::satMix, &Params::mbDepth })
            set (*p, *id, 1.0f);
        set (*p, Params::drive, 24.0f);
        set (*p, Params::air, 9.0f);
        set (*p, Params::width, 2.0f);
        set (*p, Params::autoGain, 0.0f);
        prepare (*p);
        juce::Random rng (5);
        const auto s = render (*p, 4.0, [&] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        });
        check (s.finite, "output is finite");
        check (s.peak < 20.0f, "bounded peak (" + juce::String (s.peak, 2) + ")");
    }

    if (outDir != juce::File())
    {
        auto p = std::make_unique<VibrationProcessor>();
        prepare (*p);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        // Feed some audio so the spectrum shows something.
        juce::Random rng (9);
        juce::AudioBuffer<float> audio;
        render (*p, 0.5, [&] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                float v = 0.0f;
                for (int k = 0; k < 7; ++k)   // a detuned saw-ish stack
                    v += 0.05f * (float) (2.0 * std::fmod (t * 440.0 * (1.0 + 0.004 * (k - 3)), 1.0) - 1.0);
                v += 0.02f * (rng.nextFloat() * 2.0f - 1.0f);
                b.setSample (0, i, v); b.setSample (1, i, v * 0.9f);
            }
        }, &audio);
        for (auto* child : editor->getChildren())
            if (auto* spec = dynamic_cast<suite::SpectrumView*> (child))
                spec->update();
        snapshot (*editor, outDir.getChildFile ("vibration.png"));
        if (auto* tabs = dynamic_cast<suite::ui::TabBar*> (editor->findChildWithID ("tabs")))
        {
            tabs->setSelected (1);
            snapshot (*editor, outDir.getChildFile ("vibration_scenes.png"));
        }
        editor.reset();
    }

    return finish();
}
