// Offline tests for Solve et Coagula. Usage: SolveEtCoagulaTests [output-dir]

#include "PluginProcessor.h"
#include "LabelReadyBus.h"
#include "TestUtils.h"
#include "PluginEditor.h"
#include "SectionBus.h"
#include "KeyBus.h"
#include "WritingBus.h"

using namespace suite::test;

namespace
{
    using P = SolveEtCoagulaProcessor;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    std::unique_ptr<P> make (bool withSidechain)
    {
        auto p = std::make_unique<P>();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (withSidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        p->setBusesLayout (layout);
        p->prepareToPlay (sampleRate, blockSize);
        return p;
    }

    using Signal = std::function<float (int64_t)>;
    Signal sine (double hz, float rms) { return [=] (int64_t i) { return (float) (rms * 1.4142 * std::sin (twoPi * hz * (double) i / sampleRate)); }; }
    Signal silence() { return [] (int64_t) { return 0.0f; }; }
    Signal noise (uint32_t seed = 3)
    {
        auto rng = std::make_shared<juce::Random> ((juce::int64) seed);
        return [rng] (int64_t) { return 0.2f * (rng->nextFloat() * 2.0f - 1.0f); };
    }

    /** Runs main + key through p; returns the main output (left) and the input (left). */
    std::pair<std::vector<float>, std::vector<float>> run (P& p, double seconds, Signal main, Signal key)
    {
        const auto total = (int64_t) (seconds * sampleRate);
        const auto channels = p.getTotalNumInputChannels();
        juce::AudioBuffer<float> buffer (std::max (2, channels), blockSize);
        juce::MidiBuffer midi;
        std::vector<float> out, in;
        for (int64_t pos = 0; pos < total; pos += blockSize)
        {
            const auto n = (int) std::min<int64_t> (blockSize, total - pos);
            buffer.setSize (buffer.getNumChannels(), n, false, false, true);
            buffer.clear();
            for (int i = 0; i < n; ++i)
            {
                const auto m = main (pos + i);
                buffer.setSample (0, i, m);
                buffer.setSample (1, i, m);
                in.push_back (m);
                if (channels >= 4)
                {
                    const auto k = key (pos + i);
                    buffer.setSample (2, i, k);
                    buffer.setSample (3, i, k);
                }
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i) out.push_back (buffer.getSample (0, i));
        }
        return { out, in };
    }

    float rmsDb (const std::vector<float>& v, double t0, double t1)
    {
        double sum = 0.0;
        const auto a = (size_t) (t0 * sampleRate), b = std::min (v.size(), (size_t) (t1 * sampleRate));
        for (auto i = a; i < b; ++i) sum += (double) v[i] * v[i];
        return juce::Decibels::gainToDecibels ((float) std::sqrt (sum / (double) std::max<size_t> (1, b - a)), -200.0f);
    }

    float changeDb (P& p, double mainHz, Signal key, double t0 = 1.0, double t1 = 1.5)
    {
        auto [out, in] = run (p, t1, sine (mainHz, 0.1f), key);
        return rmsDb (out, t0, t1) - rmsDb (in, t0, t1);
    }

    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::memcmp (&a[i], &b[i], sizeof (float)) != 0) return false;   // exact on purpose
        return true;
    }
}

int main (int argc, char** argv)
{
    suite::Worker::detail::thread().inlineMode = true;   // background jobs run straight away in the tests
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Transparent without a vocal");
    {
        auto p = make (false);
        auto [out, in] = run (*p, 1.0, noise(), silence());
        check (identical (out, in), "no sidechain: the music passes through bit-for-bit");
        check (! p->sidechainConnected.load(), "reports that no sidechain is connected");

        auto q = make (true);
        auto [out2, in2] = run (*q, 1.0, noise(), silence());
        check (identical (out2, in2), "silent sidechain: bit-for-bit pass-through");
        check (q->sidechainConnected.load(), "reports the connected sidechain");
    }

    section ("Dips the music where the vocal is");
    {
        const auto key = sine (1000.0, 0.1f);   // -20 dBFS vocal at 1 kHz
        auto p = make (true);
        const auto at1k = changeDb (*p, 1000.0, key);
        check (at1k < -4.0f && at1k > -12.0f, "1 kHz music under a 1 kHz vocal is cut (" + juce::String (at1k, 1) + " dB)");
        auto q = make (true);
        const auto at250 = changeDb (*q, 250.0, key);
        check (std::abs (at250) < 1.0f, "250 Hz is left alone (" + juce::String (at250, 2) + " dB)");
        auto r = make (true);
        const auto at4k = changeDb (*r, 4000.0, key);
        check (std::abs (at4k) < 1.0f, "4 kHz is left alone (" + juce::String (at4k, 2) + " dB)");
        check (r->unmasker.getCutDb (3) < -4.0f && r->unmasker.getCutDb (0) > -0.5f && r->unmasker.getCutDb (7) > -0.5f,
               "band display: the 1k band cuts, the outer bands don't");

        auto deep = make (true);
        set (*deep, P::depthId, 12.0f);
        const auto deeper = changeDb (*deep, 1000.0, key);
        check (deeper < at1k - 3.0f, "more depth cuts more (" + juce::String (deeper, 1) + " dB)");

        auto off = make (true);
        set (*off, P::mixId, 0.0f);
        auto [o, i] = run (*off, 1.0, noise(), key);
        check (identical (o, i), "mix 0%: untouched even with a vocal");
    }

    section ("Follows the vocal");
    {
        // A vocal at 2.5 kHz for 1 s, then gone.
        auto p = make (true);
        Signal key = [k = sine (2500.0, 0.1f)] (int64_t i) { return i < (int64_t) sampleRate ? k (i) : 0.0f; };
        auto [out, in] = run (*p, 2.5, sine (2500.0, 0.1f), key);
        const auto during = rmsDb (out, 0.6, 1.0) - rmsDb (in, 0.6, 1.0);
        const auto after = rmsDb (out, 2.0, 2.5) - rmsDb (in, 2.0, 2.5);
        check (during < -4.0f, "cuts while the vocal sings (" + juce::String (during, 1) + " dB)");
        check (std::abs (after) < 0.3f, "lets go once it stops (" + juce::String (after, 2) + " dB)");

        auto quiet = make (true);
        const auto whisper = changeDb (*quiet, 1000.0, sine (1000.0, 0.0005f));   // -66 dBFS
        check (std::abs (whisper) < 0.5f, "ignores a vocal below the sensitivity threshold (" + juce::String (whisper, 2) + " dB)");
        auto keen = make (true);
        set (*keen, P::sensitivityId, 100.0f);
        const auto keenCut = changeDb (*keen, 1000.0, sine (1000.0, 0.003f));     // -50 dBFS
        check (keenCut < -2.0f, "full sensitivity catches a quiet vocal (" + juce::String (keenCut, 1) + " dB)");
    }

    section ("Focus: cut only the vocal's main bands");
    {
        // Vocal with a strong 630 Hz body and a weaker (-9 dB) 2.5 kHz presence.
        Signal key = [a = sine (630.0, 0.1f), b = sine (2500.0, 0.0355f)] (int64_t i) { return a (i) + b (i); };
        auto wide = make (true);
        set (*wide, P::focusId, 24.0f);
        const auto wideCut = changeDb (*wide, 2500.0, key);
        auto narrow = make (true);
        set (*narrow, P::focusId, 6.0f);
        const auto narrowCut = changeDb (*narrow, 2500.0, key);
        check (wideCut < narrowCut - 2.0f && narrowCut > -1.0f, "narrow focus leaves the weaker band alone (wide "
                   + juce::String (wideCut, 1) + " dB, narrow " + juce::String (narrowCut, 1) + " dB)");
    }

    section ("Section scenes");
    {
        suite::SectionMap map;   // Breakdown bars 1-4, Drop bars 5-8
        map.totalBars = 8; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 5, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        const auto key = sine (1000.0, 0.1f);

        auto cutIn = [&] (double ppq)
        {
            auto p = make (true);
            FakePlayHead head;
            head.ppq = ppq;
            p->setPlayHead (&head);
            const auto db = changeDb (*p, 1000.0, key);
            p->setPlayHead (nullptr);
            return db;
        };
        const auto inBreakdown = cutIn (2.0), inDrop = cutIn (18.0);
        check (inDrop < inBreakdown - 3.0f, "dips deeper in the drop (" + juce::String (inDrop, 1) + " dB) than in the breakdown ("
                                                + juce::String (inBreakdown, 1) + " dB)");

        auto off = make (true);
        set (*off, suite::SceneEngine::enabledId, 0.0f);
        FakePlayHead head;
        head.ppq = 18.0;
        off->setPlayHead (&head);
        auto plainDrop = changeDb (*off, 1000.0, key);
        off->setPlayHead (nullptr);
        auto none = make (true);
        const auto noMap = changeDb (*none, 1000.0, key);
        check (std::abs (plainDrop - noMap) < 0.2f, "scenes off: the knobs alone decide (" + juce::String (plainDrop, 1) + " dB)");

        auto a = make (true);
        a->scenes.setOffset ((int) suite::SectionType::Drop, P::sDepth, 0.4f);
        juce::MemoryBlock state;
        a->getStateInformation (state);
        auto b = make (true);
        b->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (b->scenes.getOffset ((int) suite::SectionType::Drop, P::sDepth) - 0.4f) < 1.0e-3f, "scenes are saved with the project");

        if (outDir != juce::File())
        {
            auto q = make (true);
            head.ppq = 18.0;
            q->setPlayHead (&head);
            run (*q, 0.3, noise(), key);
            std::unique_ptr<juce::AudioProcessorEditor> editor (q->createEditor());
            if (auto* tabs = dynamic_cast<suite::ui::TabBar*> (editor->findChildWithID ("tabs"))) tabs->setSelected (1);
            snapshot (*editor, outDir.getChildFile ("solveetcoagula_scenes.png"));
            editor.reset();
            q->setPlayHead (nullptr);
        }
        suite::SectionBus::withdraw();
    }

    section ("Vocal chops");
    {
        // 120 BPM (2 s bars): the vocal sings in the breakdown (bars 1-4), the build (bars 5-6) is chopped.
        suite::SectionMap map;
        map.totalBars = 8; map.count = 3;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 5, suite::SectionType::Build };
        map.sections[2] = { 7, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        auto p = make (true);
        set (*p, P::chopsOnId, 1.0f);
        set (*p, P::chopsLevelId, 0.0f);
        set (*p, suite::SceneEngine::enabledId, 0.0f);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        // The vocal: a different note on each beat (300, 400, 500, 600 Hz), only in the breakdown.
        auto vocal = [] (int64_t i)
        {
            const auto t = (double) i / sampleRate;
            if (t >= 8.0) return 0.0f;
            const auto beat = (int) std::fmod (t / 0.5, 4.0);
            return 0.2f * (float) std::sin (twoPi * (300.0 + 100.0 * beat) * t);
        };
        std::vector<float> out;
        const auto total = (int64_t) (12.0 * sampleRate);
        juce::AudioBuffer<float> buffer (4, blockSize);
        juce::MidiBuffer midi;
        for (int64_t pos = 0; pos < total; pos += blockSize)
        {
            const auto n = (int) std::min<int64_t> (blockSize, total - pos);
            buffer.setSize (4, n, false, false, true);
            buffer.clear();
            for (int i = 0; i < n; ++i) { buffer.setSample (2, i, vocal (pos + i)); buffer.setSample (3, i, vocal (pos + i)); }
            p->processBlock (buffer, midi);
            for (int i = 0; i < n; ++i) out.push_back (buffer.getSample (0, i));
            head.ppq += n * head.bpm / 60.0 / sampleRate;
        }
        p->setPlayHead (nullptr);
        auto band = [&] (double hz, double t0, double t1)
        {
            double sn = 0.0, cs = 0.0;
            for (auto i = (size_t) (t0 * sampleRate); i < (size_t) (t1 * sampleRate); ++i)
            {
                sn += out[i] * std::sin (twoPi * hz * (double) i / sampleRate);
                cs += out[i] * std::cos (twoPi * hz * (double) i / sampleRate);
            }
            return (float) (2.0 * std::sqrt (sn * sn + cs * cs) / ((t1 - t0) * sampleRate));
        };
        check (rmsDb (out, 0.5, 7.9) < -100.0f, "nothing added while it listens (the music bus here is silent)");
        check (p->chopsReady.load() && rmsDb (out, 8.0, 12.0) > -30.0f, "the build plays chops of the vocal's last bar");
        // Stutter: beat 1's first 1/16 four times, then beat 2's... so 300 Hz in the first beat, 400 Hz in the second.
        check (band (300.0, 8.01, 8.1) > 4.0f * band (400.0, 8.01, 8.1) && band (400.0, 8.51, 8.6) > 4.0f * band (300.0, 8.51, 8.6),
               "on the beat grid: beat 1 gets the 300 Hz slice, beat 2 the 400 Hz one");
        check (rmsDb (out, 8.106, 8.124) < rmsDb (out, 8.02, 8.09) - 20.0f, "each chop is gated before the next 1/16");
        suite::SectionBus::withdraw();
    }

    section ("Tells the suite where the vocal sings");
    {
        suite::VocalBus::clear();
        auto p = make (true);
        FakePlayHead head;
        head.bpm = 120.0;   // 2 s bars: vocal in bars 1-2, silence in 3-4
        p->setPlayHead (&head);
        const auto total = (int64_t) (8.2 * sampleRate);
        juce::AudioBuffer<float> buffer (4, blockSize);
        juce::MidiBuffer midi;
        for (int64_t pos = 0; pos < total; pos += blockSize)
        {
            const auto n = (int) std::min<int64_t> (blockSize, total - pos);
            buffer.setSize (4, n, false, false, true);
            buffer.clear();
            for (int i = 0; i < n; ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto v = t < 4.0 ? 0.2f * (float) std::sin (twoPi * 500.0 * t) : 0.0f;
                buffer.setSample (2, i, v); buffer.setSample (3, i, v);
            }
            p->processBlock (buffer, midi);
            head.ppq += n * head.bpm / 60.0 / sampleRate;
        }
        p->setPlayHead (nullptr);
        const auto bars = suite::VocalBus::read();
        check (bars.size() == 2 && bars[0] && bars[1], "bars 1 and 2 are marked as vocal, 3 and 4 aren't (" + juce::String ((int) bars.size()) + " bars marked)");
        suite::VocalBus::clear();
        check (suite::VocalBus::read().empty(), "and the marks can be cleared");
    }

    section ("Live check: a buried vocal");
    {
        auto run = [] (float musicAmp)
        {
            auto p = make (true);
            set (*p, P::mixId, 0.0f);   // no dips: measure what's there
            FakePlayHead head;
            head.bpm = 120.0;   // 2 s bars, the vocal sings all 6 bars
            p->setPlayHead (&head);
            const auto total = (int64_t) (12.2 * sampleRate);
            juce::AudioBuffer<float> buffer (4, blockSize);
            juce::MidiBuffer midi;
            juce::Random rng (4);
            for (int64_t pos = 0; pos < total; pos += blockSize)
            {
                const auto n = (int) std::min<int64_t> (blockSize, total - pos);
                buffer.setSize (4, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const auto t = (double) (pos + i) / sampleRate;
                    const auto v = 0.1f * (float) std::sin (twoPi * 2000.0 * t);
                    const auto m = musicAmp * (rng.nextFloat() * 2.0f - 1.0f);
                    buffer.setSample (0, i, m); buffer.setSample (1, i, m);
                    buffer.setSample (2, i, v); buffer.setSample (3, i, v);
                }
                p->processBlock (buffer, midi);
                head.ppq += n * head.bpm / 60.0 / sampleRate;
            }
            p->setPlayHead (nullptr);
            return std::make_pair (p->vocalBuried.load(), (p->suiteStatus().flags & suite::Status::vocalBuried) != 0);
        };
        const auto [buried, flagged] = run (0.9f);
        const auto [clear, clearFlag] = run (0.02f);
        check (buried && flagged, "loud music over a quiet vocal for 4+ bars is flagged for Mentalism");
        check (! clear && ! clearFlag, "a vocal that sits on top isn't");
        suite::VocalBus::clear();
    }

    section ("Vocal melody to MIDI, and a harmony");
    {
        auto p = make (true);
        suite::KeyBus::publish (9, 0);   // A minor
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        // A3, C4, E4 a beat each, then a beat's rest, twice
        auto vocal = [] (int64_t i)
        {
            const auto t = (double) i / sampleRate;
            const auto beat = (int) std::floor (t / 0.5) % 4;
            if (beat == 3) return 0.0f;
            const double hz[] = { 220.0, 261.63, 329.63 };
            const auto env = std::min (1.0, std::fmod (t, 0.5) / 0.01);
            return (float) (0.2 * env * (std::sin (twoPi * hz[beat] * t) + 0.4 * std::sin (2.0 * twoPi * hz[beat] * t)));
        };
        const auto total = (int64_t) (4.0 * sampleRate);
        juce::AudioBuffer<float> buffer (4, blockSize);
        juce::MidiBuffer midi;
        for (int64_t pos = 0; pos < total; pos += blockSize)
        {
            const auto n = (int) std::min<int64_t> (blockSize, total - pos);
            buffer.setSize (4, n, false, false, true);
            buffer.clear();
            for (int i = 0; i < n; ++i) { buffer.setSample (2, i, vocal (pos + i)); buffer.setSample (3, i, vocal (pos + i)); }
            p->processBlock (buffer, midi);
            head.ppq += n * head.bpm / 60.0 / sampleRate;
            p->collectMelody();
        }
        p->setPlayHead (nullptr);
        p->collectMelody();
        const auto& notes = p->melody.notes();
        juce::StringArray seen;
        for (const auto& nt : notes) seen.add (juce::String (nt.pitch) + "@" + juce::String (nt.start, 2));
        auto has = [&] (int pitch, double start) { return std::any_of (notes.begin(), notes.end(), [&] (const VocalMelody::Note& nt) { return nt.pitch == pitch && std::abs (nt.start - start) < 0.3; }); };
        check (notes.size() == 6 && has (57, 0.0) && has (60, 1.0) && has (64, 2.0) && has (57, 4.0) && has (64, 6.0),
               "the sung notes come out as MIDI on the beat: A3, C4, E4, twice (" + seen.joinIntoString (" ") + ")");
        check (suite::WritingBus::sung().size() == notes.size(), "what the vocal sang is shared, so Correspondence can write around it");
        const auto harmony = VocalMelody::harmony (notes, 9, 0);
        check (harmony.size() == notes.size() && harmony[0].pitch == 60 && harmony[1].pitch == 64 && harmony[2].pitch == 67,
               "the harmony is a diatonic third above, in A minor: C4, E4, G4");
        const auto folder = juce::File::createTempFile ("vocalmidi");
        const auto result = p->saveVocalMidi (folder);
        check (folder.getChildFile ("Vocal melody.mid").existsAsFile() && folder.getChildFile ("Vocal harmony.mid").existsAsFile(), "both written as MIDI files (" + result + ")");
        check (folder.getChildFile ("Vocal double (guide).mid").existsAsFile() && folder.getChildFile ("Vocal harmony below.mid").existsAsFile(),
               "and a guide for a double and a harmony a sixth below");
        const auto dbl = VocalMelody::doubles (notes, 120.0);
        bool offset = dbl.size() == notes.size();
        for (size_t i = 0; offset && i < dbl.size(); ++i) { const auto ms = std::abs (dbl[i].start - notes[i].start) * 500.0; offset = (ms >= 9.0 && ms <= 26.0) || notes[i].start < 0.03; }
        check (offset && VocalMelody::harmonyBelow (notes, 9, 0).front().pitch == 48, "the double sits 10-25 ms off each note; the low harmony a sixth below (C3 under A3)");
        // 7: the check: a sharp, late note is listed; the in-tune notes heard here are not
        check (VocalMelody::check (notes, 120.0).empty(), "the check finds nothing wrong with an in-tune, on-time vocal");
        std::vector<VocalMelody::Note> off { { 60, 4.0, 1.0, 100, 38.0f, 4.2 } };
        const auto issues = VocalMelody::check (off, 120.0);
        check (issues.size() == 1 && issues[0].text.contains ("38 cents sharp") && issues[0].text.contains ("57 ms late") && issues[0].text.contains ("bar 2"),
               "the check lists a note 38 cents sharp and 57 ms late (" + (issues.empty() ? juce::String() : issues[0].text) + ")");
        folder.deleteRecursively();
    }

    section ("Chops on the sung words, with the suite's swing");
    {
        VocalChops chops;
        chops.prepare (sampleRate);
        const auto barQ = 4.0, qPerSample = 2.0 / sampleRate;   // 120 BPM
        // A bar with four sung words (a quarter of a beat each, then silence)
        for (int i = 0; i < (int) (2.0 * sampleRate); ++i)
        {
            const auto t = i / sampleRate;
            const auto inBeat = std::fmod (t, 0.5);
            const auto word = (int) (t / 0.5);
            const auto x = inBeat < 0.25 ? 0.3f * (float) std::sin (twoPi * (200.0 + 100.0 * word) * t) : 0.0f;
            chops.listen (x, i * qPerSample, barQ, qPerSample);
        }
        chops.finishBar (barQ, qPerSample);
        check (chops.wordsHeard() == 4, "it hears the four sung words in the bar (" + juce::String (chops.wordsHeard()) + ")");
        // The Words pattern: one word per 8th, in order; the 2nd 8th starts the 2nd word (300 Hz)
        double sn = 0.0, cs = 0.0;
        for (int i = 0; i < (int) (0.2 * sampleRate); ++i)
        {
            const auto pos = 0.5 + 0.01 + i * qPerSample;
            const auto v = chops.play (pos, barQ, qPerSample, VocalChops::Words, false);
            sn += v * std::sin (twoPi * 300.0 * i / sampleRate); cs += v * std::cos (twoPi * 300.0 * i / sampleRate);
        }
        check (std::sqrt (sn * sn + cs * cs) / (0.2 * sampleRate) > 0.05, "the Words pattern plays the second word on the second 8th");
        check (std::abs (VocalChops::unswing (0.25 * 1.3, 4.0, 0.3) - 0.25) < 1.0e-9 && std::abs (VocalChops::unswing (0.5, 4.0, 0.3) - 0.5) < 1.0e-9,
               "16th chops land on the suite's swing");
    }

    section ("Layouts and state");
    {
        P p;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::mono());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.checkBusesLayoutSupported (l), "mono sidechain supported");
        l.inputBuses.getReference (1) = juce::AudioChannelSet::create5point1();
        check (! p.checkBusesLayoutSupported (l), "5.1 sidechain refused");

        auto a = make (true);
        set (*a, P::depthId, 9.0f);
        juce::MemoryBlock state;
        a->getStateInformation (state);
        auto b = make (true);
        b->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (b->apvts.getRawParameterValue (P::depthId)->load() - 9.0f) < 1.0e-3f, "settings are saved with the project");
    }

    if (outDir != juce::File())
    {
        auto p = make (true);
        Signal key = [a = sine (630.0, 0.1f), b = sine (1000.0, 0.07f), c = sine (2500.0, 0.03f)] (int64_t i) { return a (i) + b (i) + c (i); };
        run (*p, 0.8, noise(), key);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("solveetcoagula.png"));
        editor.reset();
    }


    section ("Mix fixes: the unmasking depth per section");
    {
        auto ask = [] (std::initializer_list<suite::Autopilot::Feature> fs)
        {
            suite::LabelReadyBus::Needs n {};
            for (auto f : fs) n[(size_t) f / 32] |= 1u << (f % 32);
            suite::LabelReadyBus::publishNeeds (n);
        };
        check (P::depthScale (4) > 1.0f && P::depthScale (2) < 0.7f, "deeper unmasking in the drops, lighter in the breakdowns");
        P p;
        ask ({ suite::Autopilot::mxDepthSectionF });
        p.pollTick();
        check (p.depthPerSection.load(), "latched once Ouroboros asks for it");
        suite::LabelReadyBus::reset();
    }

    return finish();
}
