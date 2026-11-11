// Offline tests for Quintessence. Usage: QuintessenceTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ActivityBus.h"
#include "Analysis.h"
#include "Registry.h"
#include "MoveLog.h"
#include "StyleLog.h"
#include "TestUtils.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<QuintessenceProcessor> make (int role)
    {
        auto p = std::make_unique<QuintessenceProcessor>();
        prepare (*p);
        set (*p, qn::roleId, (float) role);
        return p;
    }

    /** Steady sines at the ten band centres, at these levels (dB; 0 dB = 0.1 amplitude). */
    InputFn bands (std::array<float, 10> db)
    {
        return [db] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                double v = 0.0;
                for (int k = 0; k < 10; ++k)
                    if (db[(size_t) k] > -60.0f)
                        v += 0.1 * std::pow (10.0, db[(size_t) k] / 20.0) * std::sin (juce::MathConstants<double>::twoPi * analysis::bandCentres()[(size_t) k] * t + k);
                b.setSample (0, i, (float) v);
                b.setSample (1, i, (float) v);
            }
        };
    }

    InputFn sine (float hz, float amp)
    {
        return [hz, amp] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
    }

    /** Plays `input` continuously across calls (the position carries on). */
    struct Player
    {
        InputFn input;
        int64_t at = 0;
        FakePlayHead* head = nullptr;
        void play (juce::AudioProcessor& p, double seconds, juce::AudioBuffer<float>* capture = nullptr, std::function<void (int64_t)> before = {})
        {
            const auto start = at;
            render (p, seconds, [&] (juce::AudioBuffer<float>& b, int64_t pos) { input (b, start + pos); }, capture, head, before);
            at += (int64_t) (seconds * sampleRate);
        }
    };

    /** Listens and runs a pass, `count` times. */
    void passes (QuintessenceProcessor& p, Player& player, int count, double seconds = QuintessenceProcessor::passSeconds + 0.3)
    {
        for (int i = 0; i < count; ++i)
        {
            player.play (p, seconds);
            p.applyLocks();
            p.autoPass();
        }
    }

    float v (QuintessenceProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

    const std::array<float, 10> heavyPad { -12.0f, -12.0f, -8.0f, 4.0f, -1.0f, 0.0f, -2.0f, -5.0f, -9.0f, -14.0f };   // 8 dB too much 250 Hz

    /** The role-tone band working at `hz`, or -1. */
    int toneSlotAt (QuintessenceProcessor& p, float hz)
    {
        for (int s = 0; s < qn::numSlots; ++s)
            if (std::abs (std::log2 (v (p, qn::freqId (s)) / hz)) < 0.2f && (int) v (p, qn::reasonId (s)) == qn::roleTone) return s;
        return -1;
    }

    int slotNear (QuintessenceProcessor& p, float hz)
    {
        for (int s = 0; s < qn::numSlots; ++s)
            if (std::abs (std::log2 (v (p, qn::freqId (s)) / hz)) < 0.2f && std::abs (v (p, qn::gainId (s))) > 0.05f) return s;
        return -1;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();
    suite::ActivityBus::clear();
    suite::SectionBus::withdraw();
    const auto logFile = juce::File::createTempFile ("quintessence-style.txt");
    suite::StyleLog::setFileForTests (logFile);
    suite::MoveLog::clear();

    section ("Untouched, it's transparent");
    {
        auto p = make (4);
        set (*p, qn::autoEqId, 0.0f);
        set (*p, qn::autoMbId, 0.0f);
        juce::AudioBuffer<float> a;
        Player player { sine (1000.0f, 0.3f) };
        player.play (*p, 1.0, &a);
        const auto db = juce::Decibels::gainToDecibels (rmsBetween (a, 0.5, 1.0) / (0.3f / std::sqrt (2.0f)));
        check (std::abs (db) < 0.1f, "with nothing set, a 1 kHz tone passes at the same level (" + juce::String (db, 2) + " dB)");
    }

    section ("Role tone: one checked move at a time");
    {
        auto p = make (4);   // pads
        // A pad with too much 250 Hz (8 dB over the usual shape) and rumble it has no job making
        Player player { bands ({ -12.0f, -12.0f, -8.0f, 4.0f, -1.0f, 0.0f, -2.0f, -5.0f, -9.0f, -14.0f }) };
        player.play (*p, QuintessenceProcessor::passSeconds + 0.3);
        const auto before = p->toneError (p->measurement(), 4);
        p->autoPass();
        check (std::abs (v (*p, qn::lowCutId) - 100.0f) < 1.0f, "first move: a low cut at 100 Hz (lows that aren't a pad's job)");
        passes (*p, player, 14);
        const auto after = p->toneError (p->measurement(), 4);
        const auto s = slotNear (*p, 250.0f);
        check (s >= 0 && v (*p, qn::gainId (s)) <= -2.5f && v (*p, qn::gainId (s)) >= -qn::maxToneDb - 0.01f
                   && (int) v (*p, qn::reasonId (s)) == qn::roleTone,
               "the 250 Hz excess is dipped, within the 4 dB limit, marked 'Role tone' ("
                   + (s >= 0 ? juce::String (v (*p, qn::gainId (s)), 1) : juce::String ("none")) + " dB)");
        check (after >= 0.0f && after < before - 1.0f, "the track ends closer to a pad's usual tone (" + juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB)");
        if (std::getenv ("QDEBUG")) std::cout << p->moveLog().joinIntoString ("\n") << std::endl;
        check (p->kept.load() >= 3 && std::abs (v (*p, qn::lowCutId) - 100.0f) < 1.0f, "the moves that helped were kept (" + juce::String (p->kept.load()) + "), the low cut too");
        check (! p->moveLog().isEmpty() && p->moveLog().joinIntoString ("\n").contains ("a pad"), "each move is explained in the log");
        const auto shared = suite::MoveLog::read();
        const auto inShared = std::count_if (shared.begin(), shared.end(), [] (const suite::MoveLog::Entry& e) { return e.plugin == "Quintessence"; });
        check (inShared >= 6 && shared.front().kind == suite::MoveLog::kept, "and in the suite's What changed (" + juce::String ((int) inShared) + " lines, newest first)");

        p->relearn();
        bool clear = true;
        for (int sl = 0; sl < qn::numSlots; ++sl) clear = clear && std::abs (v (*p, qn::gainId (sl))) < 0.05f;
        check (clear && v (*p, qn::lowCutId) <= qn::lowCutOff, "Re-learn clears the automatic moves");
    }

    section ("A new track settles quickly");
    {
        auto p = make (4);
        Player player { bands (heavyPad) };
        passes (*p, player, 5, QuintessenceProcessor::firstPassSeconds + 0.3);
        const auto s = toneSlotAt (*p, 250.0f);
        check (s >= 0 && v (*p, qn::gainId (s)) <= -3.5f, "within five short passes (about 9 s) the 250 Hz excess is dipped ("
                                                             + (s >= 0 ? juce::String (v (*p, qn::gainId (s)), 1) : juce::String ("none")) + " dB)");
    }

    section ("Breakdowns get their own correction");
    {
        auto p = make (4);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        Player player { bands (heavyPad) };
        player.head = &head;
        suite::SectionMap map;
        map.totalBars = 999; map.count = 1;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        suite::SectionBus::publish (map);
        passes (*p, player, 8);
        auto s = toneSlotAt (*p, 250.0f);
        check (s >= 0 && v (*p, qn::bdId (s)) <= -2.5f && std::abs (v (*p, qn::gainId (s))) < 0.05f,
               "in a breakdown, the dip goes into the band's breakdown gain ("
                   + (s >= 0 ? juce::String (v (*p, qn::bdId (s)), 1) + " dB; main " + juce::String (v (*p, qn::gainId (s)), 1) : juce::String ("none")) + ")");
        check (std::abs (v (*p, qn::lowCutId) - 20.0f) < 0.5f && v (*p, qn::ratioId (1)) < 1.01f, "low cut and dynamics wait for the main sections");

        map.sections[0] = { 1, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        passes (*p, player, 10);
        s = toneSlotAt (*p, 250.0f);
        check (s >= 0 && v (*p, qn::gainId (s)) <= -1.5f && v (*p, qn::bdId (s)) <= -0.5f,
               "in the drop it makes the main correction, and the breakdown keeps its own on top");
        suite::SectionBus::withdraw();
        p->setPlayHead (nullptr);
    }

    section ("Your own sound: targets from the songs you finished");
    {
        // Three finished songs whose pads all had this shape: that's what a pad sounds like, for you
        juce::StringArray shape;
        for (auto db : heavyPad) shape.add (juce::String (db - 4.0f, 1));
        for (int i = 0; i < 3; ++i) suite::StyleLog::add ("tone", { { "role", "4" }, { "bands", shape.joinIntoString (",") } });
        auto p = make (4);
        check (p->learnedSongs (4) == 3, "it reads your pads' tone from the style log (3 songs)");
        Player player { bands (heavyPad) };
        passes (*p, player, 8);
        const auto s = toneSlotAt (*p, 250.0f);
        check (s < 0 || std::abs (v (*p, qn::gainId (s))) < 1.0f, "so the 250 Hz that suits your sound stays");
        logFile.deleteFile();
        auto q = make (4);
        check (q->learnedSongs (4) == 0, "with no songs logged it uses the built-in targets");
    }

    section ("What changed: the search");
    {
        suite::MoveLog::clear();
        suite::MoveLog::add ("Prima Materia", "Pads", "Breakdowns quieter: Breakdown level -2.0 dB -> -4.0 dB", suite::MoveLog::fix, 60);
        suite::MoveLog::add ("Quintessence", "Lead", "Band 2: 250 Hz -1.5 dB", suite::MoveLog::info, 120);
        const auto all = suite::MoveLog::read();
        int at64 = 0, lead = 0;
        for (const auto& e : all) { at64 += suite::MoveLog::matches (e, "why did it get quieter at bar 64?") ? 1 : 0; lead += suite::MoveLog::matches (e, "lead") ? 1 : 0; }
        check (all.size() == 2 && all.front().plugin == "Quintessence", "newest first");
        check (at64 == 1 && lead == 1, "'bar 64' finds the change at bar 60; 'lead' finds the lead's");
    }

    section ("A move that doesn't help is undone");
    {
        auto p = make (5);   // lead
        set (*p, qn::autoMbId, 0.0f);
        // A bare sine: no EQ move can change its shape (there's nothing else there to bring up or down)
        Player player { sine (500.0f, 0.3f) };
        passes (*p, player, 10);
        bool flat = true;
        for (int sl = 0; sl < qn::numSlots; ++sl) flat = flat && std::abs (v (*p, qn::gainId (sl))) < 0.05f;
        check (p->undone.load() >= 2 && p->kept.load() == 0, "every move was checked and undone (" + juce::String (p->undone.load()) + " undone, " + juce::String (p->kept.load()) + " kept)");
        check (flat, "and the EQ ends where it started");
    }

    section ("Dynamics: loud moments held towards the role's target, level-neutral");
    {
        auto p = make (6);   // vocal
        set (*p, qn::autoEqId, 0.0f);
        // A 1 kHz line, loud 30% of the time (18 dB over the rest)
        Player player { [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto loud = std::fmod (t, 0.5) < 0.15;
                const auto x = (loud ? 0.4f : 0.05f) * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t);
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
        } };
        player.play (*p, QuintessenceProcessor::passSeconds + 0.3);
        const auto first = p->measurement();
        const auto swingBefore = first.swingPost (1);
        p->autoPass();
        check (v (*p, qn::ratioId (1)) > 1.5f, "the mid band comes in at the vocal's ratio (" + juce::String (v (*p, qn::ratioId (1)), 1) + ":1)");
        passes (*p, player, 8);
        player.play (*p, QuintessenceProcessor::passSeconds + 0.3);
        const auto swingAfter = p->measurement().swingPost (1);
        if (std::getenv ("QDEBUG")) std::cout << p->moveLog().joinIntoString ("\n") << std::endl;
        check (swingAfter < swingBefore - 4.0f, "the loud moments stand out less (" + juce::String (swingBefore, 1) + " -> " + juce::String (swingAfter, 1) + " dB)");
        check (p->kept.load() >= 1, "and the moves were checked and kept (" + juce::String (p->kept.load()) + ")");

        // Level-neutral: the average level stays about where it was
        juce::AudioBuffer<float> a;
        player.play (*p, 2.0, &a);
        const auto outRms = rmsBetween (a, 0.0, 2.0);
        const auto inRms = std::sqrt ((0.3f * 0.16f + 0.7f * 0.0025f) * 0.5f);
        const auto change = juce::Decibels::gainToDecibels (outRms / inRms);
        check (std::abs (change) < 3.0f, "make-up keeps the level near where it was (" + juce::String (change, 1) + " dB)");
    }

    section ("Masking fixes come here, and open only while the vocal sings");
    {
        auto p = make (4);
        set (*p, qn::autoEqId, 0.0f);
        set (*p, qn::autoMbId, 0.0f);
        // Prima Materia on the same track: fixes addressed to it are taken here
        juce::AudioProcessor::TrackProperties props;
        props.name = "Pads";
        p->updateTrackProperties (props);
        suite::Registry::announce (0x5a3u, "Prima Materia", "Pads", suite::Status::roleSet, { 4.0f, 0.0f, 0.0f, 0.0f });
        const auto seq = suite::FixBus::send (suite::Fix::Carve, suite::packCarve (0x5a3u, 5, 1));   // 1 kHz, while the vocal sings
        p->fixes->poll();
        const auto s = slotNear (*p, 1000.0f);
        check (s >= 0 && std::abs (v (*p, qn::gainId (s)) + 3.0f) < 0.05f && (int) v (*p, qn::reasonId (s)) == qn::masking && (int) v (*p, qn::whenId (s)) == 1,
               "the carve for this track's Prima Materia lands in a band: 1 kHz -3 dB, 'Masking', while the vocal sings");

        auto level = [&] (bool singing)
        {
            juce::AudioBuffer<float> a;
            Player player { sine (1000.0f, 0.3f) };
            player.play (*p, 1.0, &a, [singing] (int64_t) { if (singing) suite::ActivityBus::write (suite::ActivityBus::vocal, 1.0f); });
            return rmsBetween (a, 0.5, 1.0);
        };
        const auto open = level (false), dipped = level (true);
        const auto dip = juce::Decibels::gainToDecibels (dipped / open);
        check (dip < -2.5f && dip > -3.5f, "the dip is there only while the vocal sings (" + juce::String (dip, 1) + " dB)");
        suite::ActivityBus::clear();

        suite::FixBus::send (suite::Fix::Carve, suite::packCarve (0x777u, 5, 0));   // another track's
        p->fixes->poll();
        check (std::abs (v (*p, qn::gainId (s)) + 3.0f) < 0.05f, "a fix for another track is ignored");

        suite::FixBus::sendUndo (seq);
        p->fixes->poll();
        check (std::abs (v (*p, qn::gainId (s))) < 0.05f && (int) v (*p, qn::reasonId (s)) == qn::free, "Undo puts the band back");
        suite::Registry::remove (0x5a3u);
    }

    section ("Touch a band and it's yours");
    {
        auto p = make (4);
        set (*p, qn::gainId (2), 3.0f);   // a manual edit
        p->applyLocks();
        check (v (*p, qn::lockId (2)) > 0.5f && (int) v (*p, qn::reasonId (2)) == qn::yours, "a manual edit locks the band and marks it 'Your setting'");
        Player player { bands ({ -60.0f, -60.0f, -8.0f, -4.0f, 6.0f, 0.0f, -2.0f, -5.0f, -9.0f, -14.0f }) };
        passes (*p, player, 6);
        check (std::abs (v (*p, qn::gainId (2)) - 3.0f) < 0.05f, "and the automation leaves it alone");
        check (p->kept.load() + p->undone.load() >= 1, "while it carries on with the other bands");

        // Loading a project isn't a manual edit
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make (4);
        q->setStateInformation (state.getData(), (int) state.getSize());
        q->applyLocks();
        bool locks = false;
        for (int sl = 0; sl < qn::numSlots; ++sl) if (sl != 2) locks = locks || v (*q, qn::lockId (sl)) > 0.5f;
        check (! locks && v (*q, qn::lockId (2)) > 0.5f && ! q->moveLog().isEmpty(), "a reloaded project keeps its locks, gains none, and keeps its log");
    }

    if (outDir != juce::File())
    {
        auto p = make (4);
        Player player { bands ({ -12.0f, -12.0f, -8.0f, 4.0f, -1.0f, 0.0f, -2.0f, -5.0f, -9.0f, -14.0f }) };
        passes (*p, player, 6);
        suite::FixBus::send (suite::Fix::Carve, suite::packCarve (p->getInstanceId(), 7, 1));
        p->fixes->poll();
        set (*p, qn::autoMbId, 1.0f);
        set (*p, qn::thrId (1), -24.0f);
        set (*p, qn::ratioId (1), 2.5f);
        p->applyLocks();
        player.play (*p, 0.5);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("Quintessence.png"));
    }

    return finish();
}
