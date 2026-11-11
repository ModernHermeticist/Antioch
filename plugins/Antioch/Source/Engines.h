#pragma once

// The three sections of Antioch. Each is a self-contained, allocation-free
// processor; PluginProcessor reads parameters once per block and feeds them in.

#include "SuiteDSP.h"
#include "Parameters.h"
#include <array>

//==============================================================================
/** Four pitch-shifting, key-tuned echo taps. Mono in, stereo out. */
class HarmonicEcho
{
public:
    struct TapSettings
    {
        bool on = false;
        float delaySamples = 1000.0f, semitones = 0.0f, resonatorHz = 110.0f;
        float feedback = 0.0f, level = 0.0f, pan = 0.0f;
    };

    struct Settings
    {
        std::array<TapSettings, Params::numTaps> taps;
        float resonance = 0.0f, blend = 0.0f, dampHz = 8000.0f, lowCutHz = 100.0f;
    };

    void prepare (double sampleRate, double maxDelaySeconds)
    {
        sr = (float) sampleRate;
        const auto maxDelay = (int) std::ceil ((maxDelaySeconds + 0.1) * sampleRate);
        for (auto& tap : taps)
        {
            tap.delay.prepare (maxDelay);
            tap.shifter.prepare (sampleRate);
            tap.comb.prepare (sampleRate);
            tap.damp.reset();
            tap.lowCut.reset();
            for (auto* s : { &tap.gainL, &tap.gainR, &tap.feedback, &tap.enabled })
                s->reset (sampleRate, 0.03);
        }
        blend.reset (sampleRate, 0.03);
        glide = 1.0f - std::exp (-1.0f / (0.08f * sr)); // tape-style glide when times change
        first = true;
    }

    void update (const Settings& s)
    {
        const auto combDamp = 1.0f - std::exp (-2.0f * suite::pi * s.dampHz / sr);

        for (size_t t = 0; t < taps.size(); ++t)
        {
            auto& tap = taps[t];
            const auto& ts = s.taps[t];
            tap.targetDelay = juce::jlimit (1.0f, (float) tap.delay.capacity() - 1.0f, ts.delaySamples);
            tap.shifter.setSemitones (ts.semitones);
            tap.comb.setFrequency (ts.resonatorHz);
            tap.comb.setFeedback (s.resonance);
            tap.comb.setDamping (combDamp);
            tap.damp.setCutoff (s.dampHz, sr);
            tap.lowCut.setCutoff (s.lowCutHz, sr);
            tap.feedback.setTargetValue (ts.feedback);
            tap.enabled.setTargetValue (ts.on ? 1.0f : 0.0f);
            const auto [l, r] = suite::panGains (ts.pan);
            tap.gainL.setTargetValue (l * ts.level);
            tap.gainR.setTargetValue (r * ts.level);

            if (first)
            {
                tap.delaySamples = tap.targetDelay;
                for (auto* sm : { &tap.gainL, &tap.gainR, &tap.feedback, &tap.enabled })
                    sm->setCurrentAndTargetValue (sm->getTargetValue());
            }
        }

        blend.setTargetValue (s.blend);
        if (first)
            blend.setCurrentAndTargetValue (s.blend);
        first = false;
    }

    void process (float monoIn, float& outL, float& outR) noexcept
    {
        const auto b = blend.getNextValue();
        outL = outR = 0.0f;

        for (auto& tap : taps)
        {
            const auto enabled = tap.enabled.getNextValue();
            tap.delaySamples += glide * (tap.targetDelay - tap.delaySamples);

            // Echo -> pitch shift -> tuned resonance -> tone shaping
            auto x = tap.delay.read (tap.delaySamples - tap.shifter.latency());
            x = tap.shifter.process (x);
            x += b * (tap.comb.process (x) - x);
            x = tap.lowCut.highpass (x);
            x = tap.damp.lowpass (x);

            // Feed the processed echo back so each repeat is shifted/tuned again.
            tap.delay.push (monoIn * enabled + suite::softClip (x * tap.feedback.getNextValue()));

            const auto out = x * enabled;
            outL += out * tap.gainL.getNextValue();
            outR += out * tap.gainR.getNextValue();
        }
    }

private:
    struct Tap
    {
        suite::DelayLine delay;
        suite::PitchShifter shifter;
        suite::CombResonator comb;
        suite::OnePole damp, lowCut;
        float delaySamples = 0.0f, targetDelay = 0.0f;
        juce::SmoothedValue<float> gainL, gainR, feedback, enabled;
    };

    std::array<Tap, Params::numTaps> taps;
    juce::SmoothedValue<float> blend;
    float sr = 44100.0f, glide = 0.001f;
    bool first = true;
};

//==============================================================================
/** Big, ducking, optionally shimmering FDN reverb with built-in EQ. Stereo. */
class EuphoricReverb
{
public:
    struct Settings
    {
        float decay = 6.0f, size = 0.75f, predelayMs = 30.0f, dampHz = 9000.0f;
        float lowCutHz = 300.0f, highCutHz = 12000.0f;
        float shimmer = 0.0f, shimmerSemitones = 12.0f, width = 1.0f, modulation = 0.4f;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        fdn.prepare (sampleRate);
        for (auto* d : { &preL, &preR })
            d->prepare ((int) (0.35 * sampleRate));
        shimL.prepare (sampleRate, 0.08f);
        shimR.prepare (sampleRate, 0.08f);
        for (auto* f : { &lowL, &lowR, &highL, &highR })
            f->reset();
        shimFbL = shimFbR = 0.0f;
        for (auto* s : { &predelay, &shimmer, &width })
            s->reset (sampleRate, 0.05);
    }

    void update (const Settings& s)
    {
        fdn.setParameters (s.decay, s.size, s.dampHz, s.modulation);
        predelay.setTargetValue (juce::jmax (1.0f, s.predelayMs * 0.001f * sr));
        // The longer the decay, the more the reverb amplifies what is fed back into it,
        // so the shimmer feedback is scaled down to keep the loop gain below 1.
        shimmer.setTargetValue (s.shimmer * 0.5f / std::sqrt (juce::jmax (1.0f, s.decay / 1.5f)));
        width.setTargetValue (s.width);
        shimL.setSemitones (s.shimmerSemitones);
        shimR.setSemitones (s.shimmerSemitones);
        for (auto* f : { &lowL, &lowR })
            f->set (s.lowCutHz, 0.707f, sr);
        for (auto* f : { &highL, &highR })
            f->set (s.highCutHz, 0.707f, sr);
    }

    /** duckGain is applied to the output (computed from the dry signal by the caller). */
    void process (float inL, float inR, float duckGain, float& outL, float& outR) noexcept
    {
        const auto pd = predelay.getNextValue();
        preL.push (lowL.highpass (inL));
        preR.push (lowR.highpass (inR));
        const auto xL = preL.read (pd) + shimFbL;
        const auto xR = preR.read (pd) + shimFbR;

        float rl, rr;
        fdn.process (xL, xR, rl, rr);

        // Shimmer: pitch-shifted tail fed back in, so each pass climbs again.
        const auto sh = shimmer.getNextValue();
        if (sh > 0.0001f)
        {
            shimFbL = suite::softClip (shimL.process (rl) * sh);
            shimFbR = suite::softClip (shimR.process (rr) * sh);
        }
        else
        {
            shimFbL = shimFbR = 0.0f;
        }

        rl = highL.lowpass (rl);
        rr = highR.lowpass (rr);

        const auto w = width.getNextValue();
        const auto mid = 0.5f * (rl + rr), side = 0.5f * (rl - rr) * w;
        outL = (mid + side) * duckGain;
        outR = (mid - side) * duckGain;
    }

private:
    suite::FdnReverb fdn;
    suite::DelayLine preL, preR;
    suite::PitchShifter shimL, shimR;
    suite::Svf lowL, lowR, highL, highR;
    juce::SmoothedValue<float> predelay, shimmer, width;
    float shimFbL = 0.0f, shimFbR = 0.0f, sr = 44100.0f;
};

//==============================================================================
/** Reverb freeze: keeps the last half second of the reverb and, when triggered, plays it as
    an endless wash (two crossfading, sin²-windowed read heads) that fades out over a set time. */
class FreezeWash
{
public:
    void prepare (double sampleRate)
    {
        grain = juce::jmax (64, (int) (0.5 * sampleRate));
        for (auto* b : { &histL, &histR, &frozenL, &frozenR }) b->assign ((size_t) grain, 0.0f);
        write = 0; pos = 0; total = 0; fadeIn = juce::jmax (1, (int) (0.02 * sampleRate));
    }

    void push (float l, float r) noexcept
    {
        histL[(size_t) write] = l; histR[(size_t) write] = r;
        write = (write + 1) % grain;
    }

    void trigger (int lengthSamples, float level) noexcept
    {
        for (int i = 0; i < grain; ++i)
        {
            const auto src = (size_t) ((write + i) % grain);   // oldest first
            frozenL[(size_t) i] = histL[src]; frozenR[(size_t) i] = histR[src];
        }
        pos = 0; total = juce::jmax (1, lengthSamples); gain = level;
    }

    void stop() noexcept { total = 0; }
    bool active() const noexcept { return pos < total; }

    void process (float& outL, float& outR) noexcept
    {
        outL = outR = 0.0f;
        if (pos >= total) return;
        const auto a = pos % grain, b = (pos + grain / 2) % grain;
        const auto wa = std::sin (juce::MathConstants<float>::pi * (float) a / (float) grain);
        const auto wb = std::sin (juce::MathConstants<float>::pi * (float) b / (float) grain);
        const auto env = gain * juce::jmin (1.0f, (float) pos / (float) fadeIn) * (1.0f - (float) pos / (float) total);
        outL = (frozenL[(size_t) a] * wa * wa + frozenL[(size_t) b] * wb * wb) * env;
        outR = (frozenR[(size_t) a] * wa * wa + frozenR[(size_t) b] * wb * wb) * env;
        ++pos;
    }

private:
    std::vector<float> histL, histR, frozenL, frozenR;
    int grain = 24000, write = 0, pos = 0, total = 0, fadeIn = 960;
    float gain = 0.0f;
};

//==============================================================================
/** Dub-style throw delay: only audio arriving while the throw gate is open
    enters the delay, and its tail keeps ringing after the gate closes. */
class ThrowDelay
{
public:
    struct Settings
    {
        float delaySamples = 10000.0f, feedback = 0.6f, pingPong = 1.0f;
        float lowCutHz = 350.0f, highCutHz = 6500.0f, drive = 0.25f;
    };

    void prepare (double sampleRate, double maxDelaySeconds)
    {
        sr = (float) sampleRate;
        for (auto* l : { &lineL, &lineR })
            l->prepare ((int) std::ceil ((maxDelaySeconds + 0.1) * sampleRate));
        for (auto* f : { &lowL, &lowR, &highL, &highR })
            f->reset();
        gate.reset (sampleRate, 0.008);  // fast but click-free gate
        for (auto* s : { &feedback, &pingPong, &drive })
            s->reset (sampleRate, 0.05);
        glide = 1.0f - std::exp (-1.0f / (0.1f * sr));
        first = true;
    }

    void update (const Settings& s)
    {
        targetDelay = juce::jlimit (1.0f, (float) lineL.capacity() - 1.0f, s.delaySamples);
        if (first) { delaySamples = targetDelay; first = false; }
        feedback.setTargetValue (s.feedback);
        pingPong.setTargetValue (s.pingPong);
        drive.setTargetValue (s.drive);
        for (auto* f : { &lowL, &lowR })
            f->set (s.lowCutHz, 0.707f, sr);
        for (auto* f : { &highL, &highR })
            f->set (s.highCutHz, 0.707f, sr);
    }

    void process (float inL, float inR, bool gateOpen, float& outL, float& outR) noexcept
    {
        gate.setTargetValue (gateOpen ? 1.0f : 0.0f);
        const auto g = gate.getNextValue();
        const auto pp = pingPong.getNextValue();
        const auto fb = feedback.getNextValue();
        const auto dr = 1.0f + 4.0f * drive.getNextValue();

        delaySamples += glide * (targetDelay - delaySamples);

        auto dL = lineL.read (delaySamples);
        auto dR = lineR.read (delaySamples);
        dL = highL.lowpass (lowL.highpass (dL));
        dR = highR.lowpass (lowR.highpass (dR));

        // Ping-pong: input enters the left line only and feedback crosses sides.
        const auto mono = 0.5f * (inL + inR);
        const auto sendL = g * (inL + pp * (mono - inL));
        const auto sendR = g * inR * (1.0f - pp);
        const auto fbL = fb * (dL + pp * (dR - dL));
        const auto fbR = fb * (dR + pp * (dL - dR));

        lineL.push (suite::softClip ((sendL + fbL) * dr) / dr);
        lineR.push (suite::softClip ((sendR + fbR) * dr) / dr);

        outL = dL;
        outR = dR;
    }

    float gateLevel() const noexcept { return gate.getCurrentValue(); }

private:
    suite::DelayLine lineL, lineR;
    suite::Svf lowL, lowR, highL, highR;
    juce::SmoothedValue<float> gate, feedback, pingPong, drive;
    float sr = 44100.0f, delaySamples = 0.0f, targetDelay = 0.0f, glide = 0.001f;
    bool first = true;
};
