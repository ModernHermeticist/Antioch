// Offline sanity tests for the Antioch DSP and state handling.
// Usage: AntiochTests [output-dir]   (writes example WAVs and a UI snapshot there)

#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! ok) ++failures;
    }

    void set (AntiochProcessor& p, const juce::String& id, float plainValue)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    struct Stats { float peak = 0.0f; float tailPeak = 0.0f; bool finite = true; };

    // Renders `seconds` of audio; `input` fills each block before processing.
    Stats render (AntiochProcessor& p, double seconds, std::function<void (juce::AudioBuffer<float>&, int64_t)> input,
                  juce::AudioBuffer<float>* capture = nullptr)
    {
        constexpr int block = 512;
        const auto total = (int64_t) (seconds * 48000.0);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        Stats s;

        if (capture != nullptr)
            capture->setSize (2, (int) total);

        for (int64_t pos = 0; pos < total; pos += block)
        {
            const auto n = (int) std::min<int64_t> (block, total - pos);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            input (buffer, pos);
            p.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const auto v = buffer.getSample (ch, i);
                    if (! std::isfinite (v)) s.finite = false;
                    s.peak = std::max (s.peak, std::abs (v));
                    if (pos + i > total - 48000) s.tailPeak = std::max (s.tailPeak, std::abs (v));
                }

            if (capture != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    capture->copyFrom (ch, (int) pos, buffer, ch, 0, n);
        }
        return s;
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (auto* writer = wav.createWriterFor (stream.get(), 48000.0, 2, 24, {}, 0))
        {
            stream.release();
            std::unique_ptr<juce::AudioFormatWriter> w (writer);
            w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }
    }

    std::unique_ptr<AntiochProcessor> makeProcessor()
    {
        auto p = std::make_unique<AntiochProcessor>();
        p->setPlayConfigDetails (2, 2, 48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        return p;
    }

    // A short plucked A3 note, 0.5 s long.
    auto pluck = [] (juce::AudioBuffer<float>& b, int64_t pos)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = (double) (pos + i) / 48000.0;
            if (t > 0.5) return;
            const auto v = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) * std::exp (-t * 8.0));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    std::cout << "Default patch, plucked note" << std::endl;
    {
        auto p = makeProcessor();
        juce::AudioBuffer<float> audio;
        const auto s = render (*p, 8.0, pluck, &audio);
        check (s.finite, "output is finite");
        check (s.peak > 0.05f && s.peak < 2.0f, "peak in sane range (" + juce::String (s.peak, 3) + ")");
        check (s.tailPeak < 0.01f, "tail decays (last second peak " + juce::String (s.tailPeak, 5) + ")");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("default_pluck.wav"), audio);
    }

    std::cout << "Worst case: max feedback/resonance, +12 st shimmer, full-scale noise" << std::endl;
    {
        auto p = makeProcessor();
        for (int t = 0; t < Params::numTaps; ++t)
        {
            set (*p, Params::tapId (t, Params::Tap::on), 1.0f);
            set (*p, Params::tapId (t, Params::Tap::feedback), 0.95f);
            set (*p, Params::tapId (t, Params::Tap::pitch), t % 2 == 0 ? 12.0f : -24.0f);
            set (*p, Params::tapId (t, Params::Tap::level), 1.0f);
            set (*p, Params::tapId (t, Params::Tap::harmonic), 16.0f);
        }
        set (*p, Params::resonance, 0.97f);
        set (*p, Params::resMix, 1.0f);
        set (*p, Params::damping, 20000.0f);
        set (*p, Params::lowCut, 20.0f);
        set (*p, Params::mix, 1.0f);
        set (*p, Params::output, 12.0f);

        juce::Random rng (1);
        const auto s = render (*p, 20.0, [&] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            if (pos > 48000 * 2) return;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        });
        check (s.finite, "output is finite");
        check (s.peak < 40.0f, "bounded peak (" + juce::String (s.peak, 2) + ")");
    }

    std::cout << "Free-time mode, parameter changes mid-stream" << std::endl;
    {
        auto p = makeProcessor();
        set (*p, Params::sync, 0.0f);
        juce::AudioBuffer<float> audio;
        int64_t lastChange = -1;
        const auto s = render (*p, 6.0, [&] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            pluck (b, pos % 48000);
            if (pos / 24000 != lastChange)
            {
                lastChange = pos / 24000;
                for (int t = 0; t < Params::numTaps; ++t)
                {
                    set (*p, Params::tapId (t, Params::Tap::timeMs), 50.0f + (float) ((lastChange * 137 + t * 311) % 1500));
                    set (*p, Params::tapId (t, Params::Tap::pitch), (float) ((lastChange + t) % 3 == 0 ? 0 : 7));
                }
            }
        }, &audio);
        check (s.finite, "output is finite");
        check (s.peak < 4.0f, "peak in sane range (" + juce::String (s.peak, 3) + ")");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("free_time_automation.wav"), audio);
    }

    std::cout << "State save/restore" << std::endl;
    {
        auto a = makeProcessor();
        set (*a, Params::tapId (2, Params::Tap::pitch), -5.0f);
        set (*a, Params::rootNote, 9.0f);
        juce::MemoryBlock state;
        a->getStateInformation (state);

        auto b = makeProcessor();
        b->setStateInformation (state.getData(), (int) state.getSize());
        check (b->apvts.getRawParameterValue (Params::tapId (2, Params::Tap::pitch))->load() == -5.0f, "tap pitch restored");
        check ((int) b->apvts.getRawParameterValue (Params::rootNote)->load() == 9, "root key restored");
    }

    if (outDir != juce::File())
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::PNGImageFormat png;
        auto file = outDir.getChildFile ("ui.png");
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            png.writeImageToStream (image, *stream);
        editor.reset();
        std::cout << "Wrote example audio and UI snapshot to " << outDir.getFullPathName() << std::endl;
    }

    std::cout << (failures == 0 ? "All tests passed" : juce::String (failures) + " test(s) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
