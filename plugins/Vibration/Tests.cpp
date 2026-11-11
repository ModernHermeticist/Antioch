// Offline tests for Vibration. Usage: VibrationTests [output-dir]

#include "PluginProcessor.h"
#include "TestUtils.h"
#include "ChordBus.h"
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

    section ("Lead fingerprint");
    {
        // Noise as a stand-in lead: a bright one to save, a dark one (one-pole low-passed) to match.
        auto noise = [] (float lowpass)
        {
            auto rng = std::make_shared<juce::Random> (7);
            auto state = std::make_shared<float> (0.0f);
            return [rng, state, lowpass] (juce::AudioBuffer<float>& b, int64_t)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto x = 0.2f * (rng->nextFloat() * 2.0f - 1.0f);
                    *state += lowpass * (x - *state);
                    for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, lowpass >= 1.0f ? x : *state);
                }
            };
        };
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VibrationFingerprints");
        dir.deleteRecursively();

        auto p = makeNeutral();
        set (*p, Params::drive, 12.0f);
        set (*p, Params::autoGain, 0.0f);
        render (*p, 1.0, noise (1.0f));
        check (! p->fingerprintReady() && ! p->saveFingerprint (dir, "Bright"), "needs about 5 s of the lead first");
        render (*p, 5.5, noise (1.0f));
        check (p->saveFingerprint (dir, "Bright"), "saves a fingerprint");
        const auto saved = fingerprint::loadAll (dir);
        check (saved.size() == 1 && saved[0].name == "Bright" && std::abs (saved[0].character.at (Params::drive) - 12.0f) < 0.01f,
               "it loads back with its character (drive 12 dB)");

        auto q = makeNeutral();
        set (*q, Params::autoGain, 0.0f);
        render (*q, 6.0, noise (0.08f));   // a dark lead from another synth
        const auto airBefore = q->apvts.getRawParameterValue (Params::air)->load();
        const auto line = q->matchFingerprint (saved[0]);
        const auto airAfter = q->apvts.getRawParameterValue (Params::air)->load();
        const auto presenceAfter = q->apvts.getRawParameterValue (Params::presence)->load();
        check (std::abs (q->apvts.getRawParameterValue (Params::drive)->load() - 12.0f) < 0.01f, "matching recalls the character");
        check (airAfter > airBefore + 2.0f && presenceAfter > 0.5f, "and opens the dark lead's top towards the saved tone (" + line + ")");
        check (! q->fingerprintReady(), "then listens afresh for the next refinement");
        dir.deleteRecursively();
    }

    section ("Print scenes to FL automation");
    {
        suite::SectionMap map;   // 120 BPM: Groove 0-4 s, Drop 4-8 s
        map.totalBars = 4; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Drop };
        auto p = makeNeutral();   // (withdraws any map, so publish after)
        suite::SectionBus::publish (map);
        set (*p, suite::SceneEngine::enabledId, 1.0f);
        p->scenes.clearAll();
        p->scenes.setOffset ((int) suite::SectionType::Drop, VibrationProcessor::sEuphoria, 0.3f);
        auto* euphoria = p->apvts.getParameter (Params::euphoria);
        const auto base = euphoria->getValue();
        FakePlayHead head;
        head.bpm = 120.0;
        head.playing = false;
        p->setPlayHead (&head);
        render (*p, 0.05, {}, nullptr, &head);

        p->scenes.armPrint();
        p->scenes.printTick();
        check (p->scenes.getPrintState() == suite::SceneEngine::PrintState::Armed, "armed: waits for playback");
        head.playing = true;
        render (*p, 2.0, {}, nullptr, &head);
        p->scenes.printTick();
        check (p->scenes.getPrintState() == suite::SceneEngine::PrintState::Printing && std::abs (euphoria->getValue() - base) < 1.0e-3f,
               "printing: the groove leaves the knob where it was");
        render (*p, 4.0, {}, nullptr, &head);
        p->scenes.printTick();
        check (std::abs (euphoria->getValue() - (base + 0.3f)) < 0.01f && std::abs (p->scenes.getEffective (VibrationProcessor::sEuphoria) - euphoria->getValue()) < 1.0e-4f,
               "in the drop the knob itself moves (so FL records it), and what plays is the same value (" + juce::String (base, 3) + " -> "
                   + juce::String (euphoria->getValue(), 3) + ", effective " + juce::String (p->scenes.getEffective (VibrationProcessor::sEuphoria), 3)
                   + ", type " + juce::String (p->scenes.getActiveType()) + ")");
        head.playing = false;
        render (*p, 0.1, {}, nullptr, &head);
        p->scenes.printTick();
        render (*p, 0.5, {}, nullptr, &head);
        check (p->scenes.getPrintState() == suite::SceneEngine::PrintState::Idle && p->scenes.getPrintsDone() == 1
                   && p->apvts.getRawParameterValue (suite::SceneEngine::enabledId)->load() < 0.5f
                   && std::abs (p->scenes.getEffective (VibrationProcessor::sEuphoria) - euphoria->getValue()) < 1.0e-4f,
               "stopping finishes it: scenes switch off and the knob (now automated) is what plays");

        auto q = makeNeutral();
        suite::SectionBus::publish (map);
        set (*q, suite::SceneEngine::enabledId, 1.0f);
        const auto qBase = q->apvts.getParameter (Params::euphoria)->getValue();
        q->scenes.armPrint();
        q->apvts.getParameter (Params::euphoria)->setValueNotifyingHost (0.9f);
        q->scenes.cancelPrint();
        check (q->scenes.getPrintState() == suite::SceneEngine::PrintState::Idle && std::abs (q->apvts.getParameter (Params::euphoria)->getValue() - qBase) < 1.0e-4f
                   && q->apvts.getRawParameterValue (suite::SceneEngine::enabledId)->load() > 0.5f,
               "cancel puts the knobs back and keeps scenes on");
        p->setPlayHead (nullptr);
        suite::SectionBus::withdraw();
    }

    section ("Tension width and pitch-tracking brightness");
    {
        suite::Progression prog;   // i for a bar, then VI for a bar (120 BPM: 2 s each)
        prog.root = 9; prog.scale = 0; prog.count = 2; prog.quartersPerChord = 4.0;
        prog.chordRoots[0] = 0; prog.chordRoots[1] = 8;
        auto p = makeNeutral();
        set (*p, Params::width, 1.0f);
        set (*p, Params::tensionWidth, 1.0f);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        suite::ChordBus::publish (prog);
        render (*p, 1.8, signal (440.0f, 0.2f), nullptr, &head);
        const auto home = p->effective().width;
        render (*p, 1.8, signal (440.0f, 0.2f), nullptr, &head);   // into the VI
        const auto away = p->effective().width;
        suite::ChordBus::withdraw();
        p->setPlayHead (nullptr);
        check (away > home * 1.4f, "wider on the tension chord (" + juce::String (home, 2) + " -> " + juce::String (away, 2) + ")");

        auto q = makeNeutral();
        set (*q, Params::pitchBright, 1.0f);
        render (*q, 4.0, signal (220.0f, 0.3f));
        const auto low = q->effective().air;
        const auto lowHz = q->pitchHz.load();
        render (*q, 1.0, signal (440.0f, 0.3f));   // the lead jumps an octave
        const auto high = q->effective().air;
        check (std::abs (lowHz - 220.0f) < 8.0f && std::abs (q->pitchHz.load() - 440.0f) < 15.0f && high > low + 2.0f,
               "tracks the lead's pitch (" + juce::String (lowHz, 0) + " -> " + juce::String (q->pitchHz.load(), 0) + " Hz) and adds air above its usual pitch ("
                   + juce::String (low, 1) + " -> " + juce::String (high, 1) + " dB)");
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

    section ("Role-aware setup");
    {
        auto named = [] (suite::SuiteProcessor& p, const char* name)
        {
            juce::AudioProcessor::TrackProperties props;
            props.name = juce::String (name);
            p.updateTrackProperties (props);
        };
        auto presetOn = [&] (const char* name)
        {
            VibrationProcessor p;
            p.captureFreshValues();
            named (p, name);
            p.tryRoleSetup();
            return p.roleSetupApplied >= 0 ? (float) p.apvts.getRawParameterValue (Params::euphoria)->load() : -1.0f;
        };
        check (std::abs (presetOn ("Lead Saw") - 0.5f) < 0.01f && std::abs (presetOn ("Pluck 2") - 0.4f) < 0.01f && presetOn ("FX") < 0.0f,
               "a new Vibration on a lead track loads the supersaw lead, on a pluck the pluck; FX tracks are left alone");
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
