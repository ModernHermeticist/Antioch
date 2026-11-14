// Offline tests for Emerald Tablet. Usage: EmeraldTabletTests [output-dir]

#include "PluginProcessor.h"
#include "TestUtils.h"

using namespace suite::test;

namespace
{
    /** A chord progression with bass, played with harmonic-rich tones, 2 s per chord. */
    InputFn progression (std::vector<std::vector<int>> chords)
    {
        return [chords] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto& chord = chords[(size_t) ((int) (t / 2.0) % (int) chords.size())];
                double v = 0.0;
                for (size_t n = 0; n < chord.size(); ++n)
                {
                    const auto hz = 440.0 * std::pow (2.0, (chord[n] - 69) / 12.0);
                    for (int h = 1; h <= 4; ++h)
                        v += (n == 0 ? 0.12 : 0.06) / h * std::sin (juce::MathConstants<double>::twoPi * hz * h * t);
                }
                b.setSample (0, i, (float) v);
                b.setSample (1, i, (float) v);
            }
        };
    }

    std::unique_ptr<EmeraldTabletProcessor> make()
    {
        auto p = std::make_unique<EmeraldTabletProcessor>();
        prepare (*p);
        return p;
    }

    /** Renders in 1 s chunks, calling update() like the 2 Hz timer would. */
    void listen (EmeraldTabletProcessor& p, InputFn input, double seconds)
    {
        for (double t = 0.0; t < seconds; t += 1.0)
        {
            render (p, 1.0, [&, t] (juce::AudioBuffer<float>& b, int64_t pos) { input (b, pos + (int64_t) (t * sampleRate)); });
            p.update();
        }
    }
}

int main (int argc, char** argv)
{
    suite::Worker::detail::thread().inlineMode = true;   // background jobs run straight away in the tests
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    // MIDI notes: bass note first, then the chord.
    const std::vector<std::vector<int>> aMinor { { 45, 57, 60, 64 }, { 50, 62, 65, 69 }, { 40, 64, 68, 71 }, { 45, 57, 60, 64 } };  // Am Dm E Am
    const std::vector<std::vector<int>> cMajor { { 48, 60, 64, 67 }, { 41, 65, 69, 72 }, { 43, 67, 71, 74 }, { 48, 60, 64, 67 } };  // C F G C

    section ("Detection");
    {
        auto p = make();
        listen (*p, progression (aMinor), 30.0);
        const auto best = p->detector.best.load();
        check (best == 9 + 12, "Am Dm E Am is A minor (got " + juce::String (best % 12) + (best >= 12 ? " minor)" : " major)"));
        check (p->detector.confidence.load() >= EmeraldTabletProcessor::minConfidence,
               "confident (" + juce::String (juce::roundToInt (p->detector.confidence.load() * 100.0f)) + "%)");

        auto q = make();
        listen (*q, progression (cMajor), 30.0);
        const auto cBest = q->detector.best.load();
        check (cBest == 0, "C F G C is C major (got " + juce::String (cBest % 12) + (cBest >= 12 ? " minor)" : " major)"));

        // Same chords, different keys: E minor (Em Am B7-ish Em) and F major (F Bb C F).
        auto e = make();
        listen (*e, progression ({ { 40, 64, 67, 71 }, { 45, 64, 69, 72 }, { 47, 63, 66, 71 }, { 40, 64, 67, 71 } }), 30.0);
        check (e->detector.best.load() == 4 + 12, "Em Am B Em is E minor");
        auto f = make();
        listen (*f, progression ({ { 41, 65, 69, 72 }, { 46, 65, 70, 74 }, { 48, 64, 67, 72 }, { 41, 65, 69, 72 } }), 30.0);
        check (f->detector.best.load() == 5, "F Bb C F is F major");

        auto s = make();
        listen (*s, [] (juce::AudioBuffer<float>&, int64_t) {}, 10.0);
        check (s->detector.best.load() < 0 && s->detector.seconds.load() < 0.01f, "silence teaches it nothing");
    }

    section ("Sending the key");
    {
        suite::KeyBus::withdraw();
        const auto before = suite::KeyBus::readSuggestion (100000).sequence;

        auto p = make();
        listen (*p, progression (aMinor), 10.0);
        check (p->lastSent.load() < 0, "doesn't send before it has heard enough");
        listen (*p, progression (aMinor), 25.0);
        const auto s = suite::KeyBus::readSuggestion();
        check (s.valid && s.sequence != before && s.root == 9 && s.scale == 0, "suggests A minor once stable and confident");

        const auto k = suite::KeyBus::read();
        check (k.valid && k.fromDetector && k.root == 9 && k.scale == 0, "with no Correspondence running it broadcasts A minor to the suite");

        // If Correspondence is broadcasting, Emerald Tablet stays out of its way.
        suite::KeyBus::publish (2, 1);  // D major from "Correspondence"
        p->update();
        const auto after = suite::KeyBus::read();
        check (after.root == 2 && ! after.fromDetector, "doesn't overwrite Correspondence's key");
        suite::KeyBus::withdraw();
    }

    section ("Pass-through");
    {
        auto p = make();
        juce::AudioBuffer<float> audio, ref;
        render (*p, 0.2, pluck(), &audio);
        ref.setSize (2, audio.getNumSamples()); ref.clear(); pluck() (ref, 0);
        float diff = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i) diff = std::max (diff, std::abs (audio.getSample (0, i) - ref.getSample (0, i)));
        check (diff < 1.0e-9f, "audio is untouched");
    }

    if (outDir != juce::File())
    {
        auto p = make();
        listen (*p, progression (aMinor), 25.0);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("emerald_tablet.png"));
        editor.reset();
        suite::KeyBus::withdraw();
    }

    return finish();
}
