// Offline tests for Quintessence. Usage: QuintessenceTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ActivityBus.h"
#include "Analysis.h"
#include "Registry.h"
#include "MoveLog.h"
#include "StyleLog.h"
#include "TestUtils.h"
#include "ConductorBus.h"
#include "ReferenceBus.h"
#include "AutomationBus.h"
#include "ChordBus.h"
#include "FixBus.h"
#include "AutopilotBus.h"
#include "KeyBus.h"

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

    section ("Your reference colours the targets");
    {
        auto p = make (4);
        const auto plain = p->shapeFor (4);
        // A reference 3 dB brighter than the guide from 4 kHz up (and the same elsewhere)
        auto ref = suite::ReferenceBus::guide();
        ref[7] += 3.0f; ref[8] += 3.0f; ref[9] += 3.0f;
        suite::ReferenceBus::publish (ref);
        const auto coloured = p->shapeFor (4);
        const auto lift = (coloured[8] - coloured[4]) - (plain[8] - plain[4]);
        check (std::abs (lift - 3.0f) < 0.1f && p->referenceShown.load(), "a brighter reference makes every pad target brighter by as much ("
                                                                            + juce::String (lift, 2) + " dB at 8 kHz against 500 Hz)");
        suite::ReferenceBus::withdraw();
        const auto back = p->shapeFor (4);
        check (std::abs (back[8] - plain[8]) < 1.0e-4f, "without a reference the targets are the role's own");
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

    section ("Width towards the role's");
    {
        auto p = make (5);   // lead: the side usually sits about 9 dB under the middle
        set (*p, qn::autoMbId, 0.0f);
        // As much side as middle: far too wide for a lead
        Player player { [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto mid = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 500.0 * t);
                const auto side = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 750.0 * t);
                b.setSample (0, i, mid + side); b.setSample (1, i, mid - side);
            }
        } };
        passes (*p, player, 16);
        check (v (*p, qn::widthId) < 0.6f && p->moveLog().joinIntoString ("\n").contains ("Kept: width"),
               "it narrows the lead step by step, each step checked (width " + juce::String (juce::roundToInt (v (*p, qn::widthId) * 100.0f)) + "%)");
        juce::AudioBuffer<float> mono;
        Player monoPlayer { sine (500.0f, 0.3f) };
        auto q = make (5);
        set (*q, qn::autoMbId, 0.0f);
        passes (*q, monoPlayer, 8);
        check (std::abs (v (*q, qn::widthId) - 1.0f) < 0.01f, "a mono part isn't widened: there's no side to bring up");
    }

    section ("Ringing peaks");
    {
        auto p = make (4);
        set (*p, qn::autoMbId, 0.0f);
        // Noise with a fixed ring at 1234 Hz (a resonant filter's peak)
        Player player { [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            juce::Random rng (pos);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto x = 0.05f * (rng.nextFloat() * 2.0f - 1.0f) + 0.02f * (float) std::sin (juce::MathConstants<double>::twoPi * 1234.0 * t);
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
        } };
        passes (*p, player, 10);
        if (std::getenv ("QDEBUG")) { std::cout << p->moveLog().joinIntoString ("\n") << std::endl; for (auto hz : p->lastResonances()) std::cout << "res " << hz << std::endl; }
        int slot = -1;
        for (int sl = 0; sl < qn::numSlots; ++sl)
            if ((int) v (*p, qn::reasonId (sl)) == qn::resonance && std::abs (v (*p, qn::freqId (sl)) - 1234.0f) < 15.0f) slot = sl;
        check (slot >= 0 && v (*p, qn::gainId (slot)) <= -2.9f && v (*p, qn::qId (slot)) >= 5.9f,
               "a ring that's there pass after pass gets a narrow dip, marked 'Resonance' ("
                   + (slot >= 0 ? juce::String (v (*p, qn::freqId (slot)), 0) + " Hz, " + juce::String (v (*p, qn::gainId (slot)), 1) + " dB" : juce::String ("none")) + ")");
        check (p->moveLog().joinIntoString ("\n").contains ("Kept: band " + juce::String (slot + 1)), "and the dip was checked and kept");
        const auto tips = p->spireTips();
        check (tips.joinIntoString (" ").contains ("ringing"), "Spire tips point at the filter's resonance");
    }

    section ("Punch, glue and the de-esser");
    {
        // A soft kick: a 55 Hz thump whose attack swells in over 25 ms
        auto p = make (1);
        set (*p, qn::autoEqId, 0.0f);
        Player kick { [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = std::fmod ((double) (pos + i) / sampleRate, 0.5);
                const auto env = juce::jmin (1.0, t / 0.025) * std::exp (-t * 6.0);
                const auto x = (float) (0.5 * env * std::sin (juce::MathConstants<double>::twoPi * 55.0 * t));
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
        } };
        kick.play (*p, QuintessenceProcessor::passSeconds + 0.3);
        const auto punchBefore = p->measurement().punchDb;
        p->autoPass();
        passes (*p, kick, 10);
        if (std::getenv ("QDEBUG")) std::cout << p->moveLog().joinIntoString ("\n") << std::endl;
        kick.play (*p, QuintessenceProcessor::passSeconds + 0.3);
        const auto punchAfter = p->measurement().punchDb;
        check (v (*p, qn::punchId) > 0.3f && punchAfter > punchBefore + 1.0f,
               "a soft kick gets punch, checked step by step (" + juce::String (punchBefore, 1) + " -> " + juce::String (punchAfter, 1)
                   + " dB; punch " + juce::String (juce::roundToInt (v (*p, qn::punchId) * 100.0f)) + "%)");

        // The music bus: glue to about 1.5 dB of reduction (the kick's last move no longer holds the turn)
        suite::Conductor::forget (p->getInstanceId());
        auto bus = make (qn::targets::musicBus);
        set (*bus, qn::autoEqId, 0.0f);
        Player music { bands (heavyPad) };
        passes (*bus, music, 10);
        music.play (*bus, QuintessenceProcessor::passSeconds + 0.3);
        const auto gr = bus->measurement().glueGrDb;
        if (std::getenv ("QDEBUG")) std::cout << "BUS\n" << bus->moveLog().joinIntoString ("\n") << " role " << bus->roleShown.load() << " passes " << bus->passes.load() << std::endl;
        check (v (*bus, qn::glueThrId) < -0.05f && std::abs (gr - qn::targets::glueTargetDb) < 0.6f,
               "on the music bus the glue settles near 1.5 dB of reduction (" + juce::String (gr, 2) + " dB at " + juce::String (v (*bus, qn::glueThrId), 1) + " dB)");
        check (v (*bus, qn::ratioId (1)) < 1.01f && std::abs (v (*bus, qn::gainId (1))) < 0.05f, "and leaves the EQ and multiband alone");

        // A vocal with spiky 's' sounds around 7.5 kHz
        suite::Conductor::forget (bus->getInstanceId());
        auto vox = make (6);
        set (*vox, qn::autoEqId, 0.0f);
        auto essFilter = std::make_shared<suite::Svf>();
        essFilter->set (7500.0f, 2.0f, (float) sampleRate);
        Player voice { [essFilter] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            juce::Random rng (pos + 7);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto vowel = 0.15 * (std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t));
                const auto noise = essFilter->bandpass (rng.nextFloat() * 2.0f - 1.0f);   // band-limited around 7.5 kHz
                const auto ess = std::fmod (t, 0.5) < 0.08 ? 0.5 * noise : 0.0;
                const auto x = (float) (vowel + ess + 0.002 * (rng.nextFloat() * 2.0 - 1.0));
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
        } };
        passes (*vox, voice, 10);
        check (v (*vox, qn::deessDepthId) >= 6.0f && std::abs (v (*vox, qn::deessFreqId) - 7500.0f) < 1.0f
                   && vox->moveLog().joinIntoString ("\n").contains ("Kept: de-esser"),
               "it finds the vocal's sibilance at 7.5 kHz and de-esses only the spikes, checked (depth " + juce::String (v (*vox, qn::deessDepthId), 0) + " dB)");
    }

    section ("The conductor: one move at a time across the mix");
    {
        suite::Conductor::reset();
        auto a = make (4), b = make (4);
        Player pa { bands (heavyPad) }, pb { bands (heavyPad) };
        pa.play (*a, QuintessenceProcessor::passSeconds + 0.3);
        pb.play (*b, QuintessenceProcessor::passSeconds + 0.3);
        const auto aMoved = a->autoPass(), bMoved = b->autoPass();
        check (aMoved && ! bMoved && suite::Conductor::holder() == a->getInstanceId(), "one Quintessence moves; the other waits its turn");
        suite::Conductor::judge (suite::Conductor::worse);   // Mentalism: the master moved away from its target
        pa.play (*a, QuintessenceProcessor::passSeconds + 0.3);
        pb.play (*b, QuintessenceProcessor::passSeconds + 0.3);
        a->autoPass();
        check (a->undone.load() == 1 && a->moveLog()[0].contains ("moved the master away"), "a move that made the master worse is undone, even though it helped the track");
        check (b->autoPass() && suite::Conductor::holder() == b->getInstanceId(), "then the one that waited gets the next turn");
        suite::Conductor::reset();
    }

    section ("The master's tone: Mentalism's share, and turns by need");
    {
        suite::FixBus::reset();
        auto p = make (4);
        set (*p, qn::autoEqId, 0.0f);
        set (*p, qn::autoMbId, 0.0f);
        suite::FixBus::send (suite::Fix::MasterTone, suite::packMasterTone (p->getInstanceId(), 3, -1.5f));
        p->fixes->poll();
        const auto s = slotNear (*p, 250.0f);
        check (s >= 0 && std::abs (v (*p, qn::gainId (s)) + 1.5f) < 0.05f && (int) v (*p, qn::reasonId (s)) == qn::masterBalance,
               "Mentalism's share lands in a band: 250 Hz -1.5 dB, 'Master balance'");
        const auto second = suite::FixBus::send (suite::Fix::MasterTone, suite::packMasterTone (p->getInstanceId(), 3, -1.0f));
        p->fixes->poll();
        check (slotNear (*p, 250.0f) == s && std::abs (v (*p, qn::gainId (s)) + 2.5f) < 0.05f, "the next round adds to the same band");
        suite::FixBus::sendUndo (second);
        p->fixes->poll();
        check (std::abs (v (*p, qn::gainId (s)) + 1.5f) < 0.05f, "a round that didn't help is undone");
        suite::FixBus::send (suite::Fix::MasterTone, suite::packMasterTone (p->getInstanceId() + 1, 5, -2.0f));
        p->fixes->poll();
        check (slotNear (*p, 1000.0f) < 0, "another track's share isn't taken");
        suite::FixBus::reset();

        suite::Conductor::reset();
        check (suite::Conductor::request (11u, 1.0f), "(one plugin moves)");
        check (! suite::Conductor::request (12u, 1.0f) && ! suite::Conductor::request (13u, 5.0f), "(two wait: one 1 dB off, one 5 dB off)");
        suite::Conductor::release (11u);
        check (! suite::Conductor::request (12u, 1.0f) && suite::Conductor::request (13u, 5.0f), "the turn goes to the neediest, not the first to ask");
        suite::Conductor::release (13u);
        check (suite::Conductor::request (12u, 1.0f), "then to the other");
        suite::Conductor::reset();
    }

    section ("Tone budget: room for the vocal");
    {
        suite::Conductor::reset();
        suite::Registry::announce (0x6a1u, "Prima Materia", "Vox", suite::Status::roleSet, { 6.0f, 0.0f, 0.0f, 0.0f });
        auto p = make (4);
        set (*p, qn::autoMbId, 0.0f);
        juce::AudioProcessor::TrackProperties props;
        props.name = "Pads";
        p->updateTrackProperties (props);
        Player player { bands (heavyPad) };
        passes (*p, player, 12);
        int slot = -1;
        for (int sl = 0; sl < qn::numSlots; ++sl)
            if ((int) v (*p, qn::reasonId (sl)) == qn::masking && std::abs (v (*p, qn::freqId (sl)) - 2000.0f) < 1.0f) slot = sl;
        check (slot >= 0 && std::abs (v (*p, qn::gainId (slot)) + 1.5f) < 0.05f && (int) v (*p, qn::whenId (slot)) == 1,
               "the pads give the vocal room at 2 kHz: 1.5 dB, only while it sings");
        suite::Registry::remove (0x6a1u);
        suite::Conductor::reset();
    }

    section ("Builds tighten and widen; the drop lets go");
    {
        auto p = make (4);
        set (*p, qn::autoEqId, 0.0f);
        set (*p, qn::autoMbId, 0.0f);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        suite::SectionMap map;
        map.totalBars = 16; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Build };
        map.sections[1] = { 9, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        Player player { [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto mid = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 500.0 * t);
                const auto side = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 750.0 * t);
                b.setSample (0, i, mid + side); b.setSample (1, i, mid - side);
            }
        } };
        player.head = &head;
        juce::AudioBuffer<float> a;
        player.play (*p, 20.0, &a);
        auto sideRms = [&] (double t0, double t1)
        {
            double sum = 0.0; int n = 0;
            for (int i = (int) (t0 * sampleRate); i < (int) (t1 * sampleRate); ++i, ++n) { const auto sd = 0.5 * (a.getSample (0, i) - a.getSample (1, i)); sum += sd * sd; }
            return std::sqrt (sum / n);
        };
        const auto early = sideRms (0.5, 1.5), late = sideRms (15.0, 15.9), drop = sideRms (17.0, 19.0);
        const auto wider = juce::Decibels::gainToDecibels ((float) (late / early)), back = juce::Decibels::gainToDecibels ((float) (drop / early));
        check (wider > 1.5f && std::abs (back) < 0.3f, "the image widens through the build (+" + juce::String (wider, 1) + " dB of side) and is back on the drop ("
                                                     + juce::String (back, 1) + " dB)");
        suite::SectionBus::withdraw();
        p->setPlayHead (nullptr);
    }

    section ("Listening to the suite: bypass, riser room, chord room, moods");
    {
        // B2: hear without the automation: the track exactly as it comes in
        auto p = make (4);
        Player player { bands (heavyPad) };
        passes (*p, player, 6);
        suite::AutomationBus::setBypassed (true);
        juce::AudioBuffer<float> out;
        player.play (*p, 0.5, &out);
        juce::AudioBuffer<float> in (2, out.getNumSamples());
        bands (heavyPad) (in, player.at - (int64_t) (0.5 * sampleRate));
        bool same = true;
        for (int i = 0; i < out.getNumSamples() && same; ++i) same = std::abs (out.getSample (0, i) - in.getSample (0, i)) < 1.0e-6f;
        check (same && p->bypassedNow.load(), "hear without the automation: the pad passes untouched (its moves are kept for later)");
        suite::AutomationBus::setBypassed (false);

        // A5: while Azoth's riser plays, pads make room for it around 4 kHz
        auto q = make (4);
        set (*q, qn::autoEqId, 0.0f);
        set (*q, qn::autoMbId, 0.0f);
        q->autoPass();   // (sets the role it shows)
        passes (*q, player, 1);
        auto level = [&] (bool riser)
        {
            juce::AudioBuffer<float> a;
            Player tone { sine (4000.0f, 0.2f) };
            tone.play (*q, 1.0, &a, [riser] (int64_t) { if (riser) suite::ActivityBus::write (suite::ActivityBus::riser, 1.0f); });
            return rmsBetween (a, 0.5, 1.0);
        };
        const auto plain = level (false), withRiser = level (true);
        const auto room = juce::Decibels::gainToDecibels (withRiser / plain);
        check (room < -2.5f && room > -3.5f, "while the riser plays, the pad gives way at 4 kHz (" + juce::String (room, 1) + " dB)");
        suite::ActivityBus::clear();

        // A4: chord-aware: room at the chord's root, down where the bass plays it
        suite::Progression prog;
        prog.valid = true; prog.root = 9; prog.scale = 0; prog.count = 1; prog.quartersPerChord = 4.0; prog.chordRoots[0] = 0;
        suite::ChordBus::publish (prog);
        suite::SectionMap map;
        map.totalBars = 8; map.count = 1;
        map.sections[0] = { 1, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        FakePlayHead head;
        head.bpm = 128.0;
        q->setPlayHead (&head);
        Player pad { bands (heavyPad) };
        pad.head = &head;
        pad.play (*q, 0.3);
        q->setPlayHead (nullptr);
        check (std::abs (q->chordHzNow.load() - 110.0f) < 0.5f, "on an A chord, the pad leaves a little room at 110 Hz, where the bass plays A (" + juce::String (q->chordHzNow.load(), 1) + " Hz)");
        suite::SectionBus::withdraw();
        suite::ChordBus::withdraw();

        // B3: mood: wider
        const auto widthBefore = v (*q, qn::widthId);
        suite::FixBus::send (suite::Fix::Wider);
        q->fixes->poll();
        check (std::abs (v (*q, qn::widthId) - widthBefore * 1.15f) < 0.01f, "the mood 'wider' widens the pad 15%");
    }

    section ("New notes written: the part re-learns");
    {
        auto pad = make (4), bassP = make (2);
        suite::FixBus::send (suite::Fix::Relearn, 1u << 4);   // Correspondence exported new chords (the pads' part)
        pad->fixes->poll();
        bassP->fixes->poll();
        check (pad->moveLog().joinIntoString ("\n").contains ("Correspondence wrote new notes") && ! bassP->moveLog().joinIntoString ("\n").contains ("Correspondence wrote"),
               "new chords exported: the pads re-learn, the bass doesn't");
    }

    section ("Spire tips");
    {
        qn::spire::Reading dull;
        dull.role = 5;
        dull.toneDiff[7] = -5.0f; dull.toneDiff[8] = -4.0f;
        dull.sideMinusMidDb = -20.0f; dull.sideTargetDb = -9.0f;
        const auto t = qn::spire::tips (dull);
        check (t.size() == 2 && t[0].startsWith ("Dull") && t[1].contains ("unison"), "a dull, narrow lead: open the filter, more unison");
        qn::spire::Reading bass;
        bass.role = 2;
        bass.sideMinusMidDb = -8.0f;
        check (qn::spire::tips (bass).joinIntoString (" ").contains ("Mono"), "a wide bass: Mono");
        qn::spire::Reading vocal;
        vocal.role = 6;
        vocal.toneDiff[7] = -6.0f;
        check (qn::spire::tips (vocal).isEmpty() && qn::spire::startingPoint (5, false).contains ("Supersaw"), "no Spire tips for a vocal; a lead starts as a supersaw");
    }

    section ("A move that doesn't help is undone");
    {
        auto p = make (5);   // lead
        set (*p, qn::autoMbId, 0.0f);
        if (std::getenv ("QOFF")) suite::Autopilot::set (suite::Autopilot::mixing, false);
        // A bare sine: no EQ move can change its shape (there's nothing else there to bring up or down)
        Player player { sine (500.0f, 0.3f) };
        passes (*p, player, 10);
        bool flat = true;
        for (int sl = 0; sl < qn::numSlots; ++sl) flat = flat && std::abs (v (*p, qn::gainId (sl))) < 0.05f;
        if (std::getenv ("QDEBUG")) std::cout << p->moveLog().joinIntoString ("\n") << "\nlive: mask " << p->live.maskNow.load() << " lift " << p->live.liftNow.load()
                                              << " soothe " << p->live.sootheNow.load() << " floor " << p->live.floorNowDb.load() << " low " << p->live.lowOwnerNow.load() << std::endl;
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

    section ("The problem watcher: narrower, and drops that breathe");
    {
        suite::FixBus::reset();
        auto p = make (4);
        set (*p, qn::widthId, 1.2f);
        const auto seq = suite::FixBus::send (suite::Fix::Narrower, p->getInstanceId() & 0xfffu);
        p->fixes->poll();
        check (std::abs (v (*p, qn::widthId) - 0.9f) < 0.01f && std::abs (p->sideOffset() + 3.0f) < 0.01f,
               "a part too wide for mono: 25% narrower, and its width target 3 dB narrower so it stays");
        suite::FixBus::sendUndo (seq);
        p->fixes->poll();
        check (std::abs (v (*p, qn::widthId) - 1.2f) < 0.01f && std::abs (p->sideOffset()) < 0.01f, "undone: back");

        set (*p, qn::autoMbId, 1.0f);
        set (*p, qn::thrId (1), -20.0f);
        set (*p, qn::ratioId (1), 3.0f);
        const auto squash = suite::FixBus::send (suite::Fix::LessSquash);
        p->fixes->poll();
        check (std::abs (v (*p, qn::thrId (1)) + 18.0f) < 0.01f && std::abs (p->swingOffset() - 2.0f) < 0.01f && p->glueTarget() < qn::targets::glueTargetDb,
               "squashed drops: the compressing band eases off 2 dB, and the targets let 2 dB more through");
        suite::FixBus::sendUndo (squash);
        p->fixes->poll();
        check (std::abs (v (*p, qn::thrId (1)) + 20.0f) < 0.01f && std::abs (p->swingOffset()) < 0.01f, "undone: back");
        auto q = make (4);
        const auto none = suite::FixBus::send (suite::Fix::LessSquash);
        q->fixes->poll();
        check (suite::FixBus::acknowledgements (none) == 0, "nothing compressing: nothing to ease off");
        suite::FixBus::reset();
    }


    section ("Autopilot (harshness): a track the master names dips in the harsh band while it lasts");
    {
        auto run = [] (bool harsh, bool culprit)
        {
            suite::Autopilot::reset();
            auto p = make (4);
            set (*p, qn::autoEqId, 0.0f);
            set (*p, qn::autoMbId, 0.0f);
            const uint32_t id = culprit ? p->getInstanceId() : p->getInstanceId() + 1;
            juce::AudioBuffer<float> a;
            Player player { sine (4000.0f, 0.3f) };
            if (harsh) suite::Autopilot::publishHarsh (1.0f, 7, &id, 1);
            player.play (*p, 0.4, &a);
            suite::Autopilot::reset();
            return juce::Decibels::gainToDecibels (rmsBetween (a, 0.3, 0.4) / (0.3f / std::sqrt (2.0f)));
        };
        const auto dipped = run (true, true), other = run (true, false), calm = run (false, true);
        check (dipped < -2.0f, "the named track dips at 4 kHz (" + juce::String (dipped, 1) + " dB)");
        check (std::abs (other) < 0.3f && std::abs (calm) < 0.3f, "other tracks, and calm moments, are untouched (" + juce::String (other, 2) + ", " + juce::String (calm, 2) + " dB)");
    }


    section ("Autopilot (mixing): tracks making room for each other");
    {
        suite::Autopilot::reset();
        suite::SectionBus::withdraw();
        // Another track, as the bus would hear it: its role and its ten bands (dB)
        auto other = [] (int role, std::array<float, 10> db) { return [role, db] (int64_t) { suite::Autopilot::postLive (0x777u, role, db.data()); }; };
        auto levelDb = [] (QuintessenceProcessor& p, InputFn in, std::function<void (int64_t)> before, double seconds = 1.5)
        {
            juce::AudioBuffer<float> a;
            Player player { in };
            player.play (p, seconds, &a, before);
            return juce::Decibels::gainToDecibels (rmsBetween (a, seconds - 0.5, seconds));
        };
        std::array<float, 10> quiet; quiet.fill (-100.0f);
        auto leadAt2k = quiet; leadAt2k[6] = -10.0f;
        {
            auto pad = make (4); set (*pad, qn::autoEqId, 0.0f); set (*pad, qn::autoMbId, 0.0f);
            const auto alone = levelDb (*pad, sine (2000.0f, 0.3f), {});
            auto pad2 = make (4); set (*pad2, qn::autoEqId, 0.0f); set (*pad2, qn::autoMbId, 0.0f);
            const auto withLead = levelDb (*pad2, sine (2000.0f, 0.3f), other (5, leadAt2k));
            check (withLead < alone - 1.0f, "a pad dips where the lead plays at the same time (" + juce::String (withLead - alone, 1) + " dB at 2 kHz)");
        }
        {
            auto withNote = [&] (float padHz)
            {
                auto pad = make (4); set (*pad, qn::autoEqId, 0.0f); set (*pad, qn::autoMbId, 0.0f);
                const auto alone = levelDb (*pad, sine (padHz, 0.3f), {});
                auto pad2 = make (4); set (*pad2, qn::autoEqId, 0.0f); set (*pad2, qn::autoMbId, 0.0f);
                auto lead = quiet; lead[5] = -10.0f;
                const auto post = other (5, lead);
                return levelDb (*pad2, sine (padHz, 0.3f), [post] (int64_t t) { post (t); suite::Autopilot::publishLeadPitch (880.0f); }) - alone;
            };
            const auto onNote = withNote (880.0f), offNote = withNote (1300.0f);
            check (onNote < -1.5f && std::abs (offNote) < 0.6f, "with the lead's note known, the pad dips at that note only (" + juce::String (onNote, 1) + " dB at the note, "
                       + juce::String (offNote, 1) + " dB beside it)");
        }
        {
            auto lead = make (5); set (*lead, qn::autoEqId, 0.0f); set (*lead, qn::autoMbId, 0.0f);
            const auto alone = levelDb (*lead, sine (2500.0f, 0.1f), {});
            auto lead2 = make (5); set (*lead2, qn::autoEqId, 0.0f); set (*lead2, qn::autoMbId, 0.0f);
            auto busyPad = quiet; busyPad[5] = busyPad[6] = busyPad[7] = -5.0f;
            const auto masked = levelDb (*lead2, sine (2500.0f, 0.1f), other (4, busyPad));
            check (masked > alone + 1.0f, "a lead under a louder pad lifts its presence (" + juce::String (masked - alone, 1) + " dB)");
        }
        {
            auto pad = make (4); set (*pad, qn::autoEqId, 0.0f); set (*pad, qn::autoMbId, 0.0f);
            const auto alone = levelDb (*pad, sine (40.0f, 0.3f), {}, 2.0);
            auto pad2 = make (4); set (*pad2, qn::autoEqId, 0.0f); set (*pad2, qn::autoMbId, 0.0f);
            auto bass = quiet; bass[0] = bass[1] = -10.0f;
            const auto owned = levelDb (*pad2, sine (40.0f, 0.3f), other (2, bass), 2.0);
            check (owned < alone - 4.0f, "with a bass playing, a pad's 40 Hz goes (" + juce::String (owned - alone, 1) + " dB)");
        }
        suite::Autopilot::reset();
        {
            auto pad = make (4); set (*pad, qn::autoEqId, 0.0f); set (*pad, qn::autoMbId, 0.0f);
            suite::Autopilot::set (suite::Autopilot::mixing, false);
            const auto off = levelDb (*pad, sine (2000.0f, 0.3f), other (5, leadAt2k));
            suite::Autopilot::reset();
            check (std::abs (off) - std::abs (juce::Decibels::gainToDecibels (0.3f / std::sqrt (2.0f))) < 0.2f, "switched off: no dip");
        }

        // The live moves on their own (LiveMix, as Quintessence runs it)
        struct Out { std::vector<float> l, r; };
        auto liveRun = [] (int role, double seconds, std::function<std::pair<float, float> (int64_t)> sig, bool kicks = false, qn::LiveMix* keep = nullptr)
        {
            auto lm = std::make_unique<qn::LiveMix>();
            auto& m = keep != nullptr ? *keep : *lm;
            m.prepare (sampleRate);
            Out o;
            const auto total = (int64_t) (seconds * sampleRate);
            for (int64_t start = 0; start < total; start += 256)
            {
                qn::LiveMix::Context c;
                c.role = role;
                if (kicks) { c.kickMs = (float) std::fmod ((double) start / sampleRate * 1000.0, 500.0); c.msPerSample = (float) (1000.0 / sampleRate); }
                const auto n = (int) std::min<int64_t> (256, total - start);
                m.beginBlock (c, n);
                for (int i = 0; i < n; ++i)
                {
                    auto [l, r] = sig (start + i);
                    m.process (l, r, true);
                    o.l.push_back (l); o.r.push_back (r);
                }
                m.endBlock();
            }
            return o;
        };
        auto rmsOf = [] (const std::vector<float>& v, double a, double b)
        {
            double sum = 0.0; int n = 0;
            for (auto i = (size_t) (a * sampleRate); i < (size_t) (b * sampleRate) && i < v.size(); ++i) { sum += (double) v[i] * v[i]; ++n; }
            return (float) std::sqrt (sum / juce::jmax (1, n));
        };
        auto mono = [] (std::function<float (double)> f) { return [f] (int64_t i) { const auto x = f ((double) i / sampleRate); return std::make_pair (x, x); }; };
        // A short decaying click every half second (on the kick's beats)
        auto clicks = [] (double t, float amp) { const auto ph = std::fmod (t, 0.5); return amp * (float) (std::exp (-ph / 0.02) * std::sin (juce::MathConstants<double>::twoPi * 3000.0 * t)); };
        {
            const auto onKick = liveRun (3, 3.0, mono ([&] (double t) { return clicks (t, 0.3f); }), true);
            const auto free = liveRun (3, 3.0, mono ([&] (double t) { return clicks (t, 0.3f); }), false);
            const auto d = juce::Decibels::gainToDecibels (rmsOf (onKick.l, 2.5, 2.52) / rmsOf (free.l, 2.5, 2.52));
            check (d < -1.5f, "a percussion attack landing on the kick is softened (" + juce::String (d, 1) + " dB)");
        }
        {
            // Even hits: one in eight four times as loud
            auto hits = [&] (double t) { const auto k = (int) std::floor (t / 0.5); return clicks (t, k % 8 == 7 ? 0.8f : 0.2f); };
            const auto evened = liveRun (3, 4.0, mono (hits));
            const auto plain = liveRun (0, 4.0, mono (hits));
            const auto d = juce::Decibels::gainToDecibels (rmsOf (evened.l, 3.51, 3.56) / rmsOf (plain.l, 3.51, 3.56));
            check (d < -3.0f, "a drum hit far louder than the others is evened out (" + juce::String (d, 1) + " dB)");
            const auto normal = juce::Decibels::gainToDecibels (rmsOf (evened.l, 3.01, 3.06) / rmsOf (plain.l, 3.01, 3.06));
            check (std::abs (normal) < 0.5f, "the ordinary hits are untouched (" + juce::String (normal, 2) + " dB)");
        }
        {
            // Bass notes: A1 loud, E2 quiet, half a second each
            auto notes = [] (double t)
            {
                const auto loud = std::fmod (t, 1.0) < 0.5;
                return (loud ? 0.3f : 0.1f) * (float) std::sin (juce::MathConstants<double>::twoPi * (loud ? 55.0 : 82.41) * t);
            };
            qn::LiveMix keep;
            const auto o = liveRun (2, 12.0, mono (notes), false, &keep);
            const auto spread = juce::Decibels::gainToDecibels (rmsOf (o.l, 11.1, 11.45) / rmsOf (o.l, 11.6, 11.95));
            check (spread < 6.5f, "bass notes that boom and vanish are evened (9.5 dB apart in, " + juce::String (spread, 1) + " dB out)");
        }
        {
            qn::LiveMix keep;
            const auto o = liveRun (2, 6.0, mono ([] (double t) { return 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * t); }), false, &keep);
            double diff = 0.0; int n = 0;
            for (auto i = (size_t) (5.0 * sampleRate); i < o.l.size(); ++i)
            {
                const auto in = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * (double) i / sampleRate);
                diff += (double) (o.l[i] - in) * (o.l[i] - in); ++n;
            }
            if (std::getenv ("QDEBUG")) std::cout << "harm " << keep.harmonicsNow.load() << " bands " << keep.bands()[1] << " " << keep.bands()[2] << " " << keep.bands()[3] << " " << keep.bands()[4] << std::endl;
            check (keep.harmonicsNow.load() > 0.5f && std::sqrt (diff / n) > 0.003, "a bare sub bass gets harmonics for small speakers");
        }
        {
            juce::Random rng (5);
            qn::LiveMix ring, plain;
            // A resonance that rings now and then (a tenth of a second in every half), over a steady noise
            liveRun (4, 6.0, mono ([&] (double t) { return 0.3f * (rng.nextFloat() * 2.0f - 1.0f)
                                                       + (std::fmod (t, 0.5) < 0.1 ? 0.12f * (float) std::sin (juce::MathConstants<double>::twoPi * 2520.0 * t) : 0.0f); }), false, &ring);
            liveRun (4, 6.0, mono ([&] (double) { return 0.3f * (rng.nextFloat() * 2.0f - 1.0f); }), false, &plain);
            check (ring.sootheDeepest.load() < -1.0f && plain.sootheDeepest.load() > -0.5f, "a resonance ringing now and then in a pad is soothed while it rings (" + juce::String (ring.sootheDeepest.load(), 1)
                       + " dB; plain noise " + juce::String (plain.sootheDeepest.load(), 1) + " dB)");
        }
        {
            juce::Random rng (6);
            const auto o = liveRun (4, 2.0, mono ([&] (double t) { return t < 1.0 ? 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * t)
                                                                                    : 0.0003f * (rng.nextFloat() * 2.0f - 1.0f); }));
            const auto tail = juce::Decibels::gainToDecibels (rmsOf (o.l, 1.6, 2.0) / (0.0003f / std::sqrt (3.0f)));
            const auto body = juce::Decibels::gainToDecibels (rmsOf (o.l, 0.5, 0.9) / (0.3f / std::sqrt (2.0f)));
            check (tail < -6.0f && std::abs (body) < 0.1f, "hiss under -60 dBFS is pulled down (" + juce::String (tail, 1) + " dB), the music untouched");
        }
        {
            const auto o = liveRun (4, 1.0, [] (int64_t i)
            {
                const auto t = (double) i / sampleRate;
                const auto lo = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * t), hi = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 2000.0 * t);
                return std::make_pair (lo + hi, -lo - hi);   // all side
            });
            std::vector<float> side (o.l.size());
            for (size_t i = 0; i < side.size(); ++i) side[i] = 0.5f * (o.l[i] - o.r[i]);
            const auto all = rmsOf (side, 0.5, 1.0);
            check (all < 0.09f && all > 0.06f, "mono lows: the stereo part under 120 Hz goes, above it stays (side " + juce::String (all, 3) + ", 2 kHz alone 0.071)");
        }

        // An FX holding a note outside the key
        {
            suite::KeyBus::publish (0, 1);   // C major
            auto run = [] (float hz)
            {
                auto fx = make (7);
                set (*fx, qn::autoMbId, 0.0f);
                juce::Random rng (9);
                Player player { [hz, &rng] (juce::AudioBuffer<float>& b, int64_t pos)
                {
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate) + 0.02f * (rng.nextFloat() * 2.0f - 1.0f);
                        b.setSample (0, i, v); b.setSample (1, i, v);
                    }
                } };
                for (int k = 0; k < 8; ++k) { suite::KeyBus::publish (0, 1); passes (*fx, player, 1); }
                if (std::getenv ("QDEBUG")) { std::cout << "FX role " << fx->effectiveRole() << " res:"; for (auto r : fx->lastResonances()) std::cout << " " << r; std::cout << "\n" << fx->moveLog().joinIntoString ("\n") << std::endl; }
                return fx->keyNotchHz.load();
            };
            const auto outside = run (369.99f), inside = run (392.0f);
            suite::KeyBus::withdraw();
            check (std::abs (outside - 370.0f) < 8.0f && inside == 0.0f, "an FX holding F# in C major is notched there (" + juce::String (outside, 0) + " Hz); G is left alone");
        }

        // Grooves get their own correction on top of the drop's
        {
            suite::Conductor::reset();
            auto pad = make (4);
            set (*pad, qn::autoMbId, 0.0f);
            suite::SectionMap map;
            map.totalBars = 999; map.count = 1;
            map.sections[0] = { 1, suite::SectionType::Groove };
            suite::SectionBus::publish (map);
            FakePlayHead head; head.bpm = 120.0;
            pad->setPlayHead (&head);
            Player player { bands (heavyPad) };
            player.head = &head;
            passes (*pad, player, 6);
            pad->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            const auto slot = toneSlotAt (*pad, 250.0f);
            check (slot >= 0 && v (*pad, qn::grId (slot)) < -0.5f && std::abs (v (*pad, qn::gainId (slot))) < 0.05f,
                   "in a groove the move goes into the groove's own gain, the drop's stays");
        }

        // An effect return
        {
            auto ret = make (4);
            juce::AudioProcessor::TrackProperties props;
            props.name = "Reverb Return";
            ret->updateTrackProperties (props);
            Player player { bands (heavyPad) };
            passes (*ret, player, 2);
            check (v (*ret, qn::lowCutId) >= 179.0f && v (*ret, qn::highCutId) <= 12001.0f, "a reverb return gets a 180 Hz low cut and a 12 kHz high cut");
        }
        suite::Autopilot::reset();
    }

    return finish();
}
