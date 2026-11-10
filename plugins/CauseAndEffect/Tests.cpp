// Offline tests for Cause & Effect. Usage: CauseAndEffectTests [output-dir]

#include "PluginProcessor.h"
#include "KeyBus.h"
#include "Music.h"
#include "TestUtils.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<CauseEffectProcessor> make()
    {
        auto p = std::make_unique<CauseEffectProcessor>();
        set (*p, Params::followKey, 0.0f);
        prepare (*p);
        return p;
    }

    // Steady "track" signal: a 110 Hz bass + 880 Hz lead, so the HP sweep is measurable.
    void music (juce::AudioBuffer<float>& b, int64_t pos)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = (double) (pos + i) / sampleRate;
            const auto v = (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t)
                                  + 0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 880.0 * t));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    }

    // RMS of the first difference: a cheap proxy for high-frequency energy (noise, risers, wash).
    float highBandRms (const juce::AudioBuffer<float>& a, double t0, double t1)
    {
        double sum = 0.0;
        const auto s0 = (int) (t0 * sampleRate) + 1, s1 = (int) (t1 * sampleRate);
        for (int i = s0; i < s1; ++i)
        {
            const auto d = a.getSample (0, i) - a.getSample (0, i - 1);
            sum += d * d;
        }
        return (float) std::sqrt (sum / (s1 - s0));
    }

    /** Fundamental frequency by autocorrelation (smallest lag within 90% of the best peak). */
    double pitchOf (const juce::AudioBuffer<float>& a, double t0, double t1)
    {
        const auto s0 = (int) (t0 * sampleRate), s1 = (int) (t1 * sampleRate);
        auto x = [&] (int i) { return a.getSample (0, i) + a.getSample (1, i); };
        const int minLag = (int) (sampleRate / 1200.0), maxLag = (int) (sampleRate / 150.0);
        std::vector<double> r ((size_t) maxLag + 1, 0.0);
        double best = 0.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double sum = 0.0;
            for (int i = s0; i < s1 - maxLag; ++i)
                sum += x (i) * x (i + lag);
            r[(size_t) lag] = sum;
            best = std::max (best, sum);
        }
        for (int lag = minLag + 1; lag < maxLag; ++lag)
            if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
                return sampleRate / lag;
        return 0.0;
    }

    float lowBandRms (const juce::AudioBuffer<float>& a, double t0, double t1)
    {
        // Correlate with a 110 Hz sinusoid to measure how much of the bass survives.
        double sumS = 0.0, sumC = 0.0;
        const auto s0 = (int) (t0 * sampleRate), s1 = (int) (t1 * sampleRate);
        for (int i = s0; i < s1; ++i)
        {
            const auto t = (double) i / sampleRate;
            sumS += a.getSample (0, i) * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t);
            sumC += a.getSample (0, i) * std::cos (2.0 * juce::MathConstants<double>::pi * 110.0 * t);
        }
        return (float) (2.0 * std::sqrt (sumS * sumS + sumC * sumC) / (s1 - s0));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Idle: transparent when Build is 0");
    {
        auto p = make();
        juce::AudioBuffer<float> audio, reference;
        render (*p, 1.0, music, &audio);
        reference.setSize (2, audio.getNumSamples());
        reference.clear();
        music (reference, 0);
        float maxDiff = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::abs (audio.getSample (0, i) - reference.getSample (0, i)));
        check (maxDiff < 1.0e-4f, "output equals input (max diff " + juce::String (maxDiff, 6) + ")");
    }

    section ("Timed build: 4 bars at 138 BPM, then the drop");
    {
        auto p = make();
        FakePlayHead head;               // 138 BPM, 4/4, playing
        p->setPlayHead (&head);
        set (*p, Params::length, 0.0f);  // 4 bars
        const auto barSec = 4.0 * 60.0 / head.bpm;
        const auto dropSec = 1.0 + 4.0 * barSec;   // trigger flips on at 1 s... aligned to nearest bar

        juce::AudioBuffer<float> audio;
        bool triggered = false;
        float levelAtHalf = -1.0f, levelNearEnd = -1.0f;
        int barsAtHalf = 0;
        render (*p, dropSec + 2.0, music, &audio, &head, [&] (int64_t pos)
        {
            const auto t = (double) pos / sampleRate;
            if (! triggered && t >= barSec)        // switch on exactly at bar 2
            {
                set (*p, Params::trigger, 1.0f);
                triggered = true;
            }
            if (levelAtHalf < 0.0f && t >= barSec + 2.0 * barSec)
            {
                levelAtHalf = p->buildLevel.load();
                barsAtHalf = (int) (p->progress.load() * 4.0) + 1;
            }
            if (levelNearEnd < 0.0f && t >= barSec + 3.9 * barSec)
                levelNearEnd = p->buildLevel.load();
        });
        p->setPlayHead (nullptr);

        const auto buildStart = barSec, drop = barSec + 4.0 * barSec;
        check (levelAtHalf > 0.15f && levelAtHalf < 0.4f, "half-way build level follows the curve (" + juce::String (levelAtHalf, 3) + ")");
        check (barsAtHalf == 3, "reports bar 3 of 4 half-way through (" + juce::String (barsAtHalf) + ")");
        check (levelNearEnd > 0.85f, "near the end the build is almost full (" + juce::String (levelNearEnd, 3) + ")");
        check (p->dropCount.load() == 1, "exactly one drop fired");
        check (p->buildLevel.load() < 0.001f, "build snaps back to 0 after the drop");

        const auto bassBefore = lowBandRms (audio, 0.2, buildStart - 0.1);
        const auto bassLate   = lowBandRms (audio, drop - 0.5, drop - 0.05);
        const auto bassAfter  = lowBandRms (audio, drop + 0.3, drop + 1.0);
        check (bassLate < 0.25f * bassBefore, "high-pass removes the bass late in the build ("
                                                  + juce::String (juce::Decibels::gainToDecibels (bassLate / bassBefore), 1) + " dB)");
        check (std::abs (bassAfter - bassBefore) < 0.05f * bassBefore, "full bass returns after the drop");

        const auto hfBefore = highBandRms (audio, 0.2, buildStart - 0.1), hfLate = highBandRms (audio, drop - 0.6, drop - 0.1);
        check (hfLate > 2.0f * hfBefore, "risers and wash add top-end energy during the build ("
                                             + juce::String (juce::Decibels::gainToDecibels (hfLate / hfBefore), 1) + " dB)");
        check (rmsBetween (audio, drop + 0.3, drop + 1.0) < 1.2f * rmsBetween (audio, 0.2, buildStart - 0.1),
               "drop cut leaves a clean drop (no wash/riser tail)");

        if (outDir != juce::File()) writeWav (outDir.getChildFile ("cause_effect_build.wav"), audio);
    }

    section ("Manual Build knob and Trigger release both count as drops");
    {
        auto p = make();
        set (*p, Params::build, 0.8f);
        render (*p, 0.5, music);
        check (p->buildLevel.load() > 0.5f, "manual knob drives the build");
        set (*p, Params::build, 0.0f);
        render (*p, 0.2, music);
        check (p->dropCount.load() == 1 && p->buildLevel.load() < 0.001f, "knob snapping to 0 is a drop");

        set (*p, Params::trigger, 1.0f);
        render (*p, 3.0, music);         // stopped transport: internal clock still runs
        check (p->progress.load() > 0.0, "timed build runs from the internal clock when stopped");
        set (*p, Params::trigger, 0.0f);
        render (*p, 0.1, music);
        check (p->dropCount.load() == 2, "switching Trigger off mid-build drops early");
    }

    section ("Tonal riser lands on the root key");
    {
        auto p = make();
        suite::KeyBus::publish (7, 0); // G minor from Correspondence
        set (*p, Params::followKey, 1.0f);
        for (auto* id : { &Params::noise, &Params::wash, &Params::stutter, &Params::widen })
            set (*p, *id, 0.0f);
        set (*p, Params::riser, 1.0f);
        set (*p, Params::build, 1.0f);
        set (*p, Params::curve, 1.0f);
        juce::AudioBuffer<float> audio;
        render (*p, 1.5, {}, &audio);
        check (p->effectiveRoot.load() == 7, "follows the broadcast key (G)");

        const auto hz = pitchOf (audio, 1.0, 1.1);
        const auto g4 = suite::music::midiToHz (67.0f);
        check (std::abs (hz - g4) < 8.0, "riser ends on G4 (" + juce::String (hz, 1) + " Hz, expected " + juce::String (g4, 1) + ")");
        suite::KeyBus::withdraw();
    }

    section ("Stress: full build on full-scale noise");
    {
        auto p = make();
        for (auto* id : { &Params::wash, &Params::noise, &Params::riser, &Params::stutter, &Params::widen, &Params::filterRes })
            set (*p, *id, 1.0f);
        set (*p, Params::washDecay, 15.0f);
        set (*p, Params::build, 1.0f);
        juce::Random rng (3);
        const auto s = render (*p, 8.0, [&] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        });
        check (s.finite, "output is finite");
        check (s.peak < 12.0f, "bounded peak (" + juce::String (s.peak, 2) + ")");
    }

    section ("BuildClock edge cases");
    {
        BuildClock clock;
        suite::HostInfo h;
        h.playing = true; h.hasPpq = true; h.bpm = 120.0;
        h.ppq = 15.9;                             // automation point slightly before bar 5 (ppq 16)
        clock.beginBlock (h, 48000.0, 4);
        bool drop = false;
        clock.next (true, drop);
        check (std::abs (clock.getProgress()) < 1.0e-9, "early trigger snaps to the next bar line");

        h.ppq = 4.0;                               // playback looped back before the start
        clock.beginBlock (h, 48000.0, 4);
        clock.next (true, drop);
        check (std::abs (clock.getProgress()) < 0.001, "looping back restarts the build from that bar");
    }

    if (outDir != juce::File())
    {
        auto p = make();
        set (*p, Params::build, 0.62f);
        render (*p, 0.3, music);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("cause_effect.png"));
        editor.reset();
    }

    return finish();
}
