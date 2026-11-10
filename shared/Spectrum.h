#pragma once

// Real-time spectrum display shared by the suite.
// AudioTap: lock-free hand-off of samples from the audio thread to the UI.
// SpectrumView: pulls from one or two taps, runs an FFT and draws smoothed curves.

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SuiteUI.h"

namespace suite
{
    class AudioTap
    {
    public:
        /** Audio thread: push a mono sample. Silently drops if the UI isn't reading. */
        void push (const float* samples, int numSamples) noexcept
        {
            int s1, n1, s2, n2;
            fifo.prepareToWrite (numSamples, s1, n1, s2, n2);
            if (n1 > 0) std::copy (samples, samples + n1, data.begin() + s1);
            if (n2 > 0) std::copy (samples + n1, samples + n1 + n2, data.begin() + s2);
            fifo.finishedWrite (n1 + n2);
        }

        /** UI thread: read up to `max` samples into dest; returns the count. */
        int pull (float* dest, int max) noexcept
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (juce::jmin (max, fifo.getNumReady()), s1, n1, s2, n2);
            if (n1 > 0) std::copy (data.begin() + s1, data.begin() + s1 + n1, dest);
            if (n2 > 0) std::copy (data.begin() + s2, data.begin() + s2 + n2, dest + n1);
            fifo.finishedRead (n1 + n2);
            return n1 + n2;
        }

    private:
        static constexpr int capacity = 1 << 15;
        juce::AbstractFifo fifo { capacity };
        std::array<float, capacity> data {};
    };

    //==============================================================================
    class SpectrumView : public juce::Component
    {
    public:
        static constexpr int order = 12, size = 1 << order;

        struct Trace { AudioTap* tap; juce::Colour colour; bool filled; };

        SpectrumView (std::vector<Trace> t, double sr = 48000.0) : traces (std::move (t)), sampleRate (sr)
        {
            for (auto& tr : traces)
            {
                juce::ignoreUnused (tr);
                states.push_back ({});
                states.back().history.assign ((size_t) size, 0.0f);
                states.back().smoothed.assign ((size_t) size / 2, -100.0f);
            }
        }

        void setSampleRate (double sr) { sampleRate = sr; }

        /** Call from a UI timer: pulls new audio and recomputes the curves. */
        void update()
        {
            std::vector<float> incoming ((size_t) size);
            for (size_t t = 0; t < traces.size(); ++t)
            {
                auto& st = states[t];
                const auto got = traces[t].tap->pull (incoming.data(), size);
                if (got == 0) continue;

                // Slide the history window and append the new samples.
                std::rotate (st.history.begin(), st.history.begin() + got, st.history.end());
                std::copy (incoming.begin(), incoming.begin() + got, st.history.end() - got);

                std::fill (fftData.begin(), fftData.end(), 0.0f);
                std::copy (st.history.begin(), st.history.end(), fftData.begin());
                window.multiplyWithWindowingTable (fftData.data(), size);
                fft.performFrequencyOnlyForwardTransform (fftData.data());

                for (size_t i = 0; i < (size_t) size / 2; ++i)
                {
                    const auto db = juce::Decibels::gainToDecibels (fftData[i] * 4.0f / (float) size, -100.0f);
                    st.smoothed[i] += (db > st.smoothed[i] ? 0.5f : 0.15f) * (db - st.smoothed[i]);
                }
            }
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            using namespace ui;
            const auto r = getLocalBounds().toFloat();
            g.setColour (Colours::background);
            g.fillRoundedRectangle (r, 6.0f);

            g.setFont (juce::FontOptions (10.0f));
            for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                const auto x = xFor (f, r);
                g.setColour (Colours::outline);
                g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
                g.setColour (Colours::dimText);
                g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                            juce::Rectangle<float> (x + 2.0f, r.getBottom() - 14.0f, 30.0f, 12.0f), juce::Justification::left);
            }

            for (size_t t = 0; t < traces.size(); ++t)
            {
                juce::Path p;
                bool started = false;
                const auto& sm = states[t].smoothed;
                for (int px = 0; px < (int) r.getWidth(); px += 2)
                {
                    const auto f = 20.0f * std::pow (1000.0f, (float) px / r.getWidth());
                    const auto bin = juce::jlimit (1, size / 2 - 1, (int) (f / (float) sampleRate * (float) size));
                    // +4.5 dB/oct tilt so a balanced mix reads roughly flat.
                    const auto db = sm[(size_t) bin] + 4.5f * std::log2 (f / 1000.0f);
                    const auto y = juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, r.getBottom(), r.getY());
                    if (! started) { p.startNewSubPath (r.getX() + (float) px, y); started = true; }
                    else p.lineTo (r.getX() + (float) px, y);
                }

                if (traces[t].filled)
                {
                    juce::Path fill (p);
                    fill.lineTo (r.getRight(), r.getBottom());
                    fill.lineTo (r.getX(), r.getBottom());
                    fill.closeSubPath();
                    g.setColour (traces[t].colour.withAlpha (0.18f));
                    g.fillPath (fill);
                }
                g.setColour (traces[t].colour.withAlpha (traces[t].filled ? 0.9f : 0.6f));
                g.strokePath (p, juce::PathStrokeType (1.5f));
            }
        }

    private:
        static float xFor (float hz, juce::Rectangle<float> r)
        {
            return r.getX() + r.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
        }

        struct State { std::vector<float> history, smoothed; };

        std::vector<Trace> traces;
        std::vector<State> states;
        double sampleRate;
        juce::dsp::FFT fft { order };
        juce::dsp::WindowingFunction<float> window { (size_t) size, juce::dsp::WindowingFunction<float>::hann };
        std::array<float, size * 2> fftData {};
    };
}
