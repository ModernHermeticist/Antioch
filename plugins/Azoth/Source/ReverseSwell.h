#pragma once

// Reverse swells made from the track's own audio.
//
// Azoth runs with a fixed lookahead (reported as latency, so the host realigns the
// track). When playback passes the first beat of a target section, that beat of
// audio is captured, run through a reverb in the background (a few blocks' worth
// of samples per audio block, so there are no CPU spikes), reversed, and scheduled
// so it swells up and ends exactly on the section's downbeat in the delayed output.
//
// Timeline: "input index" counts samples arriving; the output heard at input index k
// is the audio of input index k - latency.

#include "SuiteDSP.h"
#include <vector>

class ReverseSwell
{
public:
    void prepare (double sampleRate, int latencySamples)
    {
        sr = sampleRate;
        latency = latencySamples;
        const auto historySize = (size_t) (latencySamples + (int) (2.0 * sampleRate));
        for (auto* v : { &historyL, &historyR }) v->assign (historySize, 0.0f);
        const auto maxSwell = (size_t) latencySamples;
        for (auto* v : { &wetL, &wetR, &outL, &outR }) v->assign (maxSwell, 0.0f);
        for (auto* v : { &capL, &capR }) v->assign ((size_t) (sampleRate * 1.5), 0.0f);
        reverb.prepare (sampleRate);
        reverb.setParameters (4.0f, 0.9f, 9000.0f, 0.3f);
        reset();
    }

    void reset()
    {
        state = State::Idle;
        writePos = 0;
        for (auto* v : { &historyL, &historyR }) std::fill (v->begin(), v->end(), 0.0f);
    }

    /** The longest swell that can be rendered in time for a capture of `captureSamples`. */
    int maxSwellSamples (int captureSamples) const noexcept
    {
        // Rendering runs at renderSpeed x real time: capture + swell/renderSpeed must fit in the lookahead.
        const auto margin = (int) (0.1 * sr);
        return juce::jlimit (0, (int) wetL.size(), (int) ((latency - captureSamples - margin) * renderSpeed / (renderSpeed + 1.0)));
    }

    /** Audio thread, every sample: remember the incoming audio. */
    void pushInput (float l, float r) noexcept
    {
        historyL[(size_t) writePos] = l;
        historyR[(size_t) writePos] = r;
        writePos = (writePos + 1) % (int) historyL.size();
    }

    /** Called right after pushing the sample at `nowIndex`: the section started at
        `boundaryIndex`, and the `captureSamples` since then are in the history. */
    void start (int64_t nowIndex, int64_t boundaryIndex, int captureSamples, int swellSamples, float level) noexcept
    {
        captureLength = juce::jlimit (1, (int) capL.size(), captureSamples);
        swellLength = juce::jlimit (0, maxSwellSamples (captureLength), swellSamples);
        if (swellLength < (int) (0.05 * sr))
            return;

        // Copy the captured audio out of the history ring.
        const auto size = (int) historyL.size();
        for (int i = 0; i < captureLength; ++i)
        {
            const auto age = (int) (nowIndex - (boundaryIndex + i));         // samples ago
            const auto idx = ((writePos - 1 - age) % size + size) % size;
            capL[(size_t) i] = historyL[(size_t) idx];
            capR[(size_t) i] = historyR[(size_t) idx];
        }

        boundary = boundaryIndex;
        gain = level;
        rendered = 0;
        reverb.reset();
        state = State::Rendering;
    }

    /** Audio thread, once per block: render part of the reverb tail in the background. */
    void renderStep (int blockSize) noexcept
    {
        if (state != State::Rendering)
            return;

        const auto budget = (int) (blockSize * renderSpeed);
        const auto end = std::min (swellLength, rendered + budget);
        for (; rendered < end; ++rendered)
        {
            const auto inL = rendered < captureLength ? capL[(size_t) rendered] : 0.0f;
            const auto inR = rendered < captureLength ? capR[(size_t) rendered] : 0.0f;
            float wl, wr;
            reverb.process (inL, inR, wl, wr);
            wetL[(size_t) rendered] = wl;
            wetR[(size_t) rendered] = wr;
        }

        if (rendered >= swellLength)
        {
            // Reverse, with a gentle fade-in at the start and a click-free 3 ms end.
            const auto fadeIn = std::max (1, swellLength / 4), fadeOut = std::max (1, (int) (0.003 * sr));
            for (int j = 0; j < swellLength; ++j)
            {
                const auto env = std::min (1.0f, (float) j / (float) fadeIn) * std::min (1.0f, (float) (swellLength - j) / (float) fadeOut);
                outL[(size_t) j] = wetL[(size_t) (swellLength - 1 - j)] * env * gain * 2.0f;
                outR[(size_t) j] = wetR[(size_t) (swellLength - 1 - j)] * env * gain * 2.0f;
            }
            state = State::Ready;
        }
    }

    /** The swell sample for output index `outIndex` (0 when none is playing). */
    void output (int64_t outIndex, float& l, float& r) noexcept
    {
        l = r = 0.0f;
        const auto start = boundary - swellLength;
        if (state == State::Rendering && outIndex >= start)
        {
            state = State::Idle;   // not ready in time (shouldn't happen): skip rather than play half a swell
            return;
        }
        if (state != State::Ready || outIndex < start)
            return;
        if (outIndex >= boundary)
        {
            state = State::Idle;
            return;
        }
        l = outL[(size_t) (outIndex - start)];
        r = outR[(size_t) (outIndex - start)];
        playedSamples.fetch_add (1, std::memory_order_relaxed);
    }

    void cancel() noexcept { state = State::Idle; }
    bool busy() const noexcept { return state != State::Idle; }

    std::atomic<int64_t> playedSamples { 0 };   // for tests/UI

private:
    enum class State { Idle, Rendering, Ready };
    static constexpr double renderSpeed = 8.0;

    suite::FdnReverb reverb;
    std::vector<float> historyL, historyR, capL, capR, wetL, wetR, outL, outR;
    State state = State::Idle;
    int writePos = 0, latency = 0, captureLength = 0, swellLength = 0, rendered = 0;
    int64_t boundary = 0;
    float gain = 1.0f;
    double sr = 48000.0;
};
