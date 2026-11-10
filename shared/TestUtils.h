#pragma once

// Helpers for the offline test runners (not compiled into the plugins).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace suite::test
{
    inline int failures = 0;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    inline void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! ok) ++failures;
    }

    inline void section (const juce::String& name) { std::cout << name << std::endl; }

    inline void set (juce::AudioProcessor& p, const juce::String& id, float plainValue)
    {
        for (auto* param : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                if (r->getParameterID() == id)
                {
                    r->setValueNotifyingHost (r->convertTo0to1 (plainValue));
                    return;
                }
        std::cout << "  !! unknown parameter " << id << std::endl;
        ++failures;
    }

    /** A host play-head that can be driven by the tests (tempo, position, playing). */
    struct FakePlayHead : juce::AudioPlayHead
    {
        double bpm = 138.0, ppq = 0.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }
    };

    struct Stats
    {
        float peak = 0.0f, tailPeak = 0.0f;
        bool finite = true;
    };

    using InputFn = std::function<void (juce::AudioBuffer<float>&, int64_t samplePos)>;

    /** Renders `seconds` of audio through p, advancing the play head if given. */
    inline Stats render (juce::AudioProcessor& p, double seconds, InputFn input,
                         juce::AudioBuffer<float>* capture = nullptr, FakePlayHead* head = nullptr,
                         std::function<void (int64_t)> beforeBlock = {})
    {
        const auto total = (int64_t) (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        Stats s;

        if (capture != nullptr)
            capture->setSize (2, (int) total);

        for (int64_t pos = 0; pos < total; pos += blockSize)
        {
            const auto n = (int) std::min<int64_t> (blockSize, total - pos);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            if (input) input (buffer, pos);
            if (beforeBlock) beforeBlock (pos);
            p.processBlock (buffer, midi);

            if (head != nullptr && head->playing)
                head->ppq += n * head->bpm / 60.0 / sampleRate;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const auto v = buffer.getSample (ch, i);
                    if (! std::isfinite (v)) s.finite = false;
                    s.peak = std::max (s.peak, std::abs (v));
                    if (pos + i > total - (int64_t) sampleRate) s.tailPeak = std::max (s.tailPeak, std::abs (v));
                }

            if (capture != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    capture->copyFrom (ch, (int) pos, buffer, ch, 0, n);
        }
        return s;
    }

    /** Peak level of a capture between two times (seconds). */
    inline float peakBetween (const juce::AudioBuffer<float>& b, double t0, double t1)
    {
        const auto s0 = juce::jlimit (0, b.getNumSamples(), (int) (t0 * sampleRate));
        const auto s1 = juce::jlimit (0, b.getNumSamples(), (int) (t1 * sampleRate));
        float peak = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            peak = std::max (peak, b.getMagnitude (ch, s0, juce::jmax (0, s1 - s0)));
        return peak;
    }

    inline float rmsBetween (const juce::AudioBuffer<float>& b, double t0, double t1)
    {
        const auto s0 = juce::jlimit (0, b.getNumSamples(), (int) (t0 * sampleRate));
        const auto s1 = juce::jlimit (0, b.getNumSamples(), (int) (t1 * sampleRate));
        return 0.5f * (b.getRMSLevel (0, s0, juce::jmax (0, s1 - s0)) + b.getRMSLevel (1, s0, juce::jmax (0, s1 - s0)));
    }

    inline void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (stream == nullptr)
            return;
        if (auto* writer = wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0))
        {
            stream.release();
            std::unique_ptr<juce::AudioFormatWriter> w (writer);
            w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }
    }

    inline void snapshot (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto stream = file.createOutputStream())
            png.writeImageToStream (image, *stream);
    }

    inline void prepare (juce::AudioProcessor& p)
    {
        p.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);
    }

    /** A short plucked note at `hz`, 0.5 s long, starting at `startSeconds`. */
    inline InputFn pluck (float hz = 220.0f, double startSeconds = 0.0)
    {
        return [hz, startSeconds] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate - startSeconds;
                if (t < 0.0 || t > 0.5) continue;
                const auto v = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t) * std::exp (-t * 8.0));
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
        };
    }

    inline int finish()
    {
        std::cout << (failures == 0 ? juce::String ("All tests passed") : juce::String (failures) + " test(s) FAILED") << std::endl;
        return failures == 0 ? 0 : 1;
    }
}
