// Offline tests for Quintessence. Usage: QuintessenceTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ActivityBus.h"
#include "Analysis.h"
#include "Registry.h"
#include "MoveLog.h"
#include "StyleLog.h"
#include "LabelReadyBus.h"
#include "MixFixBus.h"
#include "TestUtils.h"
#include "RoleCatalog.h"
#include "Roles.h"
#include "ConductorBus.h"
#include "ReferenceBus.h"
#include "AutomationBus.h"
#include "ChordBus.h"
#include "FixBus.h"
#include "AutopilotBus.h"
#include "KeyBus.h"
#include "TrackTraits.h"
#include "SectionBus.h"

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
    suite::Worker::detail::thread().inlineMode = true;   // background jobs run straight away in the tests
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
        // The same sound (just one gain), at the loudness the processed track had
        const auto g = out.getSample (0, 1000) / in.getSample (0, 1000);
        bool same = std::isfinite (g) && g > 0.0f;
        for (int i = 0; i < out.getNumSamples() && same; ++i) same = std::abs (out.getSample (0, i) - g * in.getSample (0, i)) < 1.0e-4f;
        Player loud { bands (heavyPad) };
        suite::AutomationBus::setBypassed (false);
        juce::AudioBuffer<float> processed;
        player.play (*p, 2.0, &processed);
        suite::AutomationBus::setBypassed (true);
        player.play (*p, 0.5, &out);
        const auto diff = juce::Decibels::gainToDecibels (rmsBetween (out, 0.1, 0.5) / rmsBetween (processed, 1.0, 2.0));
        check (same && p->bypassedNow.load() && std::abs (diff) < 0.5f, "hear without the automation: the pad as it comes in (its moves kept for later), at the loudness it had with them ("
                   + juce::String (diff, 2) + " dB)");
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
        // A 700 Hz line (the mid band), loud 30% of the time (18 dB over the rest)
        Player player { [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto loud = std::fmod (t, 0.5) < 0.15;
                const auto x = (loud ? 0.4f : 0.05f) * (float) std::sin (juce::MathConstants<double>::twoPi * 700.0 * t);
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
        } };
        player.play (*p, QuintessenceProcessor::passSeconds + 0.3);
        const auto first = p->measurement();
        const auto swingBefore = first.swingPost (3);
        p->autoPass();
        check (v (*p, qn::ratioId (3)) > 1.5f, "the mid band comes in at the vocal's ratio (" + juce::String (v (*p, qn::ratioId (3)), 1) + ":1)");
        passes (*p, player, 8);
        player.play (*p, QuintessenceProcessor::passSeconds + 0.3);
        const auto swingAfter = p->measurement().swingPost (3);
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


    section ("Mixing helpers: kick and bass by note, layers spread, the kick in key, drum bus, squashed input");
    {
        suite::Autopilot::reset();
        auto run = [] (qn::LiveMix& m, qn::LiveMix::Context c, double seconds, std::function<std::pair<float, float> (int64_t)> sig,
                       std::function<void()> eachBlock = {}, bool kicks = false)
        {
            m.prepare (sampleRate);
            std::vector<float> l, r;
            const auto total = (int64_t) (seconds * sampleRate);
            for (int64_t start = 0; start < total; start += 256)
            {
                if (eachBlock) eachBlock();
                auto cc = c;
                if (kicks) { cc.kickMs = (float) std::fmod ((double) start / sampleRate * 1000.0, 500.0); cc.msPerSample = (float) (1000.0 / sampleRate); }
                const auto n = (int) std::min<int64_t> (256, total - start);
                m.beginBlock (cc, n);
                for (int i = 0; i < n; ++i) { auto [a, b] = sig (start + i); m.process (a, b, true); l.push_back (a); r.push_back (b); }
                m.endBlock();
            }
            return std::make_pair (l, r);
        };
        auto rms = [] (const std::vector<float>& v, double a, double b)
        {
            double sum = 0.0; int n = 0;
            for (auto i = (size_t) (a * sampleRate); i < (size_t) (b * sampleRate) && i < v.size(); ++i) { sum += (double) v[i] * v[i]; ++n; }
            return (float) std::sqrt (sum / juce::jmax (1, n));
        };
        {
            qn::LiveMix m;
            qn::LiveMix::Context c; c.role = 2; c.kickBodyHz = 55.0f;
            const auto [l, r] = run (m, c, 3.0, [] (int64_t i) { const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * (double) i / sampleRate); return std::make_pair (v, v); }, {}, true);
            const auto onHit = rms (l, 2.505, 2.53), between = rms (l, 2.8, 2.95);
            check (juce::Decibels::gainToDecibels (onHit / between) < -3.0f, "the bass makes room at the kick's note just after each hit ("
                       + juce::String (juce::Decibels::gainToDecibels (onHit / between), 1) + " dB), then comes back");
        }
        {
            qn::LiveMix m;
            qn::LiveMix::Context c; c.role = 4; c.id = 0x321u;
            std::array<float, 10> pad; pad.fill (-100.0f); pad[5] = pad[6] = -20.0f;
            const auto [l, r] = run (m, c, 2.0, [] (int64_t i) { const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) i / sampleRate); return std::make_pair (v, v); },
                                     [pad] { suite::Autopilot::postLive (0x555u, 4, pad.data()); });
            const auto lr = juce::Decibels::gainToDecibels (rms (l, 1.5, 2.0) / rms (r, 1.5, 2.0));
            check (std::abs (lr) > 1.5f && std::abs (m.spreadNow.load()) > 0.1f, "with another pad playing, this one sits to one side (" + juce::String (lr, 1) + " dB left/right)");
            suite::Autopilot::reset();
        }
        {
            // A kick whose body sits between A#1 and B1 in A minor: it's tuned down to A (the root)
            auto kick = [] (int64_t i)
            {
                const auto t = std::fmod ((double) i / sampleRate, 0.5);
                const auto body = 0.6 * std::exp (-t / 0.15) * std::sin (juce::MathConstants<double>::twoPi * 60.0 * t);
                const auto click = t < 0.003 ? 0.3 * std::sin (juce::MathConstants<double>::twoPi * 3000.0 * t) : 0.0;
                const auto v = (float) (body + click);
                return std::make_pair (v, v);
            };
            qn::LiveMix m;
            qn::LiveMix::Context c; c.role = 1; c.keyValid = true; c.keyRoot = 9; c.keyScale = 0;
            const auto [l, r] = run (m, c, 4.0, kick);
            // The body's pitch in the last hit, 20-120 ms in, from its zero crossings
            int crossings = 0;
            const auto a = (size_t) (3.52 * sampleRate), b = (size_t) (3.62 * sampleRate);
            for (auto i = a + 1; i < b; ++i) crossings += l[i - 1] <= 0.0f && l[i] > 0.0f ? 1 : 0;
            const auto hz = crossings / 0.1;
            check (std::abs (m.tuneRatioNow.load() - std::pow (2.0f, -1.5f / 12.0f)) < 0.01f && std::abs (hz - 55.0) < 6.0,
                   "a kick at 60 Hz in A minor is tuned down to A (ratio " + juce::String (m.tuneRatioNow.load(), 3) + ", body now about " + juce::String (hz, 0) + " Hz)");
        }
        {
            auto drums = make (3);
            juce::AudioProcessor::TrackProperties props; props.name = "Drums"; drums->updateTrackProperties (props);
            suite::Conductor::reset();
            Player music { bands (heavyPad) };
            passes (*drums, music, 10);
            check (v (*drums, qn::glueThrId) < -0.05f, "a track named Drums gets bus glue (threshold " + juce::String (v (*drums, qn::glueThrId), 1) + " dB)");
        }
        {
            auto vox = make (6);
            Player flat { sine (300.0f, 0.4f) };   // a sine's crest is 3 dB: nothing stands over its level
            flat.play (*vox, 2.0);
            check ((vox->suiteStatus().flags & suite::Status::squashedInput) != 0, "a vocal arriving with no peaks left is flagged as squashed ("
                       + juce::String (vox->inCrestDb.load(), 1) + " dB crest)");
        }
        suite::Autopilot::reset();
    }


    section ("More automatic fixes: the sub before the kick, vocal pops and breaths, clicks, left/right, FX re-pitched");
    {
        suite::Autopilot::reset();
        auto run = [] (qn::LiveMix& m, qn::LiveMix::Context c, double seconds, std::function<std::pair<float, float> (int64_t)> sig, bool kicks = false)
        {
            m.prepare (sampleRate);
            std::vector<float> l, r;
            const auto total = (int64_t) (seconds * sampleRate);
            for (int64_t start = 0; start < total; start += 256)
            {
                auto cc = c;
                if (kicks)
                {
                    const auto ms = std::fmod ((double) start / sampleRate * 1000.0, 500.0);
                    cc.kickMs = (float) ms; cc.kickNextMs = (float) (500.0 - ms); cc.msPerSample = (float) (1000.0 / sampleRate);
                }
                const auto n = (int) std::min<int64_t> (256, total - start);
                m.beginBlock (cc, n);
                for (int i = 0; i < n; ++i) { auto [a, b] = sig (start + i); m.process (a, b, true); l.push_back (a); r.push_back (b); }
                m.endBlock();
            }
            return std::make_pair (l, r);
        };
        auto rms = [] (const std::vector<float>& v, double a, double b)
        {
            double sum = 0.0; int n = 0;
            for (auto i = (size_t) (a * sampleRate); i < (size_t) (b * sampleRate) && i < v.size(); ++i) { sum += (double) v[i] * v[i]; ++n; }
            return (float) std::sqrt (sum / juce::jmax (1, n));
        };
        auto sine = [] (double hz, float amp) { return [hz, amp] (int64_t i) { const auto v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate); return std::make_pair (v, v); }; };
        {
            qn::LiveMix m; qn::LiveMix::Context c; c.role = 2;
            const auto [l, r] = run (m, c, 2.0, sine (45.0, 0.3f), true);
            const auto before = rms (l, 1.4965, 1.4995), between = rms (l, 1.2, 1.4);
            check (juce::Decibels::gainToDecibels (before / between) < -6.0f, "the bass's sub clears just before each kick ("
                       + juce::String (juce::Decibels::gainToDecibels (before / between), 1) + " dB)");
        }
        {
            // A vowel with a pop (a low thump) every second
            auto vox = [] (int64_t i)
            {
                const auto t = (double) i / sampleRate, ph = std::fmod (t, 1.0);
                auto v = 0.15 * std::sin (juce::MathConstants<double>::twoPi * 300.0 * t) + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 900.0 * t);
                if (ph > 0.5 && ph < 0.53) v += 0.6 * std::exp (-(ph - 0.5) / 0.008) * std::sin (juce::MathConstants<double>::twoPi * 60.0 * (ph - 0.5));
                return std::make_pair ((float) v, (float) v);
            };
            qn::LiveMix m; qn::LiveMix::Context c; c.role = 6;
            const auto [l, r] = run (m, c, 3.0, vox);
            qn::LiveMix plain; qn::LiveMix::Context c0; c0.role = 4;
            const auto [pl, pr] = run (plain, c0, 3.0, vox);
            const auto d = juce::Decibels::gainToDecibels (rms (l, 2.5, 2.53) / rms (pl, 2.5, 2.53));
            check (d < -2.0f && suite::Autopilot::countOf (suite::Autopilot::plosivesCut) > 0, "a vocal pop is cut (" + juce::String (d, 1) + " dB)");
        }
        {
            // Phrases of a vowel, with a breath (quiet air) between them
            juce::Random rng (21);
            auto vox = [&rng] (int64_t i)
            {
                const auto t = (double) i / sampleRate, ph = std::fmod (t, 2.0);
                float v = ph < 1.4 ? 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 250.0 * t) : 0.0f;
                if (ph > 1.6 && ph < 1.9) v = 0.01f * (rng.nextFloat() * 2.0f - 1.0f);   // a breath: -30 dB, all air
                return std::make_pair (v, v);
            };
            qn::LiveMix m; qn::LiveMix::Context c; c.role = 6;
            const auto [l, r] = run (m, c, 6.0, vox);
            check (rms (l, 5.7, 5.85) < 0.6f * (0.01f / std::sqrt (3.0f)) && std::abs (rms (l, 4.5, 5.2) / (0.3f / std::sqrt (2.0f)) - 1.0f) < 0.05f,
                   "a breath between phrases comes down, the phrases don't");
        }
        {
            auto clip = [] (int64_t i) { const auto t = (double) i / sampleRate; const auto v = t < 0.5 ? 0.0f : 0.5f; return std::make_pair (v, v); };   // a jump out of silence
            qn::LiveMix m; qn::LiveMix::Context c; c.role = 5;
            const auto [l, r] = run (m, c, 0.6, clip);
            qn::LiveMix k; qn::LiveMix::Context ck; ck.role = 1;   // a kick: its attack is meant (no guard)
            const auto [kl, kr] = run (k, ck, 0.6, clip);
            const auto start = (size_t) (0.5 * sampleRate), later = start + (size_t) (0.002 * sampleRate);
            check (std::abs (l[start]) < 0.1f * std::abs (kl[start]) && std::abs (l[later] / kl[later] - 1.0f) < 0.02f,
                   "a clip starting out of silence fades in over 1 ms (no click); a kick keeps its attack");
        }
        {
            auto lean = [] (int64_t i) { const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 500.0 * (double) i / sampleRate); return std::make_pair (v, 0.5f * v); };   // 6 dB to the left
            qn::LiveMix m; qn::LiveMix::Context c; c.role = 4;
            const auto [l, r] = run (m, c, 12.0, lean);
            const auto lr = juce::Decibels::gainToDecibels (rms (l, 11.0, 12.0) / rms (r, 11.0, 12.0));
            check (lr < 3.5f, "a track leaning 6 dB to one side is pulled back towards the centre (" + juce::String (lr, 1) + " dB left of centre)");
        }
        {
            suite::KeyBus::publish (0, 1);   // C major
            auto fx = make (7);
            set (*fx, qn::autoMbId, 0.0f);
            juce::Random rng (9);
            Player player { [&rng] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 369.99 * (double) (pos + i) / sampleRate) + 0.02f * (rng.nextFloat() * 2.0f - 1.0f);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            } };
            for (int k = 0; k < 8; ++k) { suite::KeyBus::publish (0, 1); passes (*fx, player, 1); }
            suite::KeyBus::withdraw();
            check (std::abs (std::abs (fx->fxShiftSemis.load()) - 1.0f) < 0.1f, "an FX holding F# in C major is re-pitched a semitone, to G or F (" + juce::String (fx->fxShiftSemis.load(), 2) + ")");
        }
        suite::Autopilot::reset();
    }


    section ("Round B: hum, clip ends, layered parts in time");
    {
        suite::Autopilot::reset();
        auto rms = [] (const std::vector<float>& v, double a, double b)
        {
            double sum = 0.0; int n = 0;
            for (auto i = (size_t) (a * sampleRate); i < (size_t) (b * sampleRate) && i < v.size(); ++i) { sum += (double) v[i] * v[i]; ++n; }
            return (float) std::sqrt (sum / juce::jmax (1, n));
        };
        auto runOne = [] (qn::LiveMix& m, qn::LiveMix::Context c, double seconds, std::function<float (int64_t)> sig)
        {
            m.prepare (sampleRate);
            std::vector<float> out;
            const auto total = (int64_t) (seconds * sampleRate);
            for (int64_t start = 0; start < total; start += 256)
            {
                const auto n = (int) std::min<int64_t> (256, total - start);
                m.beginBlock (c, n);
                for (int i = 0; i < n; ++i) { auto a = sig (start + i), b = a; m.process (a, b, true); out.push_back (a); }
                m.endBlock();
            }
            return out;
        };
        {
            // A vocal take with 50 Hz hum under it: phrases, then gaps where only the hum is left
            auto take = [] (int64_t i)
            {
                const auto t = (double) i / sampleRate;
                const auto hum = 0.003f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * t);
                const auto phrase = std::fmod (t, 2.0) < 1.0 ? 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * t) : 0.0f;
                return hum + phrase;
            };
            qn::LiveMix m; qn::LiveMix::Context c; c.role = 6;
            const auto out = runOne (m, c, 8.0, take);
            const auto gap = rms (out, 7.3, 7.9) / (0.003f / std::sqrt (2.0f));
            check (m.humNow.load() == 50.0f && juce::Decibels::gainToDecibels (gap) < -15.0f, "50 Hz hum heard in the gaps is found and notched ("
                       + juce::String (juce::Decibels::gainToDecibels (gap), 1) + " dB in the gaps)");
            const auto phrase = rms (out, 6.2, 6.8) / (0.2f / std::sqrt (2.0f));
            check (std::abs (juce::Decibels::gainToDecibels (phrase)) < 0.5f, "the voice itself is untouched");
        }
        {
            auto clip = [] (int64_t i) { const auto t = (double) i / sampleRate; return t < 0.5 ? 0.5f : 0.0f; };   // stops dead
            qn::LiveMix m; qn::LiveMix::Context c; c.role = 5;
            const auto out = runOne (m, c, 0.6, clip);
            const auto stop = (size_t) (0.5 * sampleRate);
            check (out[stop] > 0.3f && out[stop + 24] > 0.1f && std::abs (out[stop + 60]) < 1.0e-6f, "a clip that stops dead gets a 1 ms tail instead of a step");
        }
        {
            // Two kick layers, the second 3 ms behind the first: the first waits for it
            qn::LiveMix a, b;
            a.prepare (sampleRate); b.prepare (sampleRate);
            auto hit = [] (double t, double delayMs)
            {
                const auto ph = std::fmod (t - delayMs / 1000.0 + 10.0, 0.5);
                return (float) (std::exp (-ph / 0.03) * std::sin (juce::MathConstants<double>::twoPi * 60.0 * ph));
            };
            const auto samplesPerQ = sampleRate * 60.0 / 120.0;
            for (int64_t start = 0; start < (int64_t) (8.0 * sampleRate); start += 256)
            {
                for (int k = 0; k < 2; ++k)
                {
                    auto& m = k == 0 ? a : b;
                    qn::LiveMix::Context c; c.role = 1; c.id = k == 0 ? 0x601u : 0x602u;
                    c.ppq = (double) start / samplesPerQ; c.samplesPerQ = samplesPerQ;
                    m.beginBlock (c, 256);
                    for (int i = 0; i < 256; ++i) { auto v = hit ((double) (start + i) / sampleRate, k == 0 ? 0.0 : 3.0); auto w = v; m.process (v, w, true); }
                    m.endBlock();
                }
            }
            if (std::getenv ("QDEBUG"))
            {
                auto& sh = suite::Autopilot::detail::shared();
                for (int i = 0; i < suite::Autopilot::maxLive; ++i)
                    if (sh.liveId[i].load() == 0x601u || sh.liveId[i].load() == 0x602u)
                    {
                        std::cout << std::hex << sh.liveId[i].load() << std::dec << ":";
                        for (int k = 0; k < 16; ++k) std::cout << " " << sh.liveOnset[i][k].load();
                        std::cout << std::endl;
                    }
            }
            check (std::abs (a.alignNowMs.load() - 3.0f) < 0.6f && b.alignNowMs.load() < 0.3f, "two kick layers 3 ms apart: the earlier waits " + juce::String (a.alignNowMs.load(), 2)
                       + " ms, the later stays put");
        }
        suite::Autopilot::reset();
    }


    section ("12 EQ bands, 7 compressor bands; the dynamics get their turn");
    {
        {
            suite::Autopilot::reset();
            auto hi = make (4);
            set (*hi, qn::autoEqId, 0.0f); set (*hi, qn::autoMbId, 0.0f);
            juce::AudioBuffer<float> a;
            Player player { sine (8000.0f, 0.3f) };
            player.play (*hi, 0.4, &a);
            const auto db = juce::Decibels::gainToDecibels (rmsBetween (a, 0.3, 0.4) / (0.3f / std::sqrt (2.0f)));
            check (std::abs (db) < 0.05f, "the click guard leaves a high note's zero crossings alone (8 kHz: " + juce::String (db, 2) + " dB)");
        }
        auto p = make (4);
        int eq = 0, mb = 0;
        for (auto* prm : p->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                if (r->getParameterID().startsWith ("b") && r->getParameterID().endsWith ("_freq")) ++eq;
                if (r->getParameterID().startsWith ("mb") && r->getParameterID().endsWith ("_thr")) ++mb;
            }
        check (eq == 12 && mb == 7, "12 EQ bands and 7 compressor bands (" + juce::String (eq) + ", " + juce::String (mb) + ")");

        // With nothing compressing the tree isn't in the path (and costs nothing)
        set (*p, qn::autoEqId, 0.0f); set (*p, qn::autoMbId, 0.0f);
        juce::Random rng (3);
        std::vector<float> noise ((size_t) (4 * sampleRate));
        for (auto& x : noise) x = 0.2f * (rng.nextFloat() * 2.0f - 1.0f);
        Player np { [&noise] (juce::AudioBuffer<float>& b, int64_t pos) { for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, noise[(size_t) (pos + i) % noise.size()]); b.setSample (1, i, noise[(size_t) (pos + i) % noise.size()]); } } };
        np.play (*p, 0.5);
        check (! p->multibandRunning.load(), "with no band compressing, the multiband is out of the path");

        // A band compressing with nothing over its threshold: the seven bands add back up flat
        set (*p, qn::thrId (6), 0.0f); set (*p, qn::ratioId (6), 2.0f);
        auto flatAt = [&] (double hz)
        {
            Player sp { [hz] (juce::AudioBuffer<float>& b, int64_t pos) { for (int i = 0; i < b.getNumSamples(); ++i) { const auto x = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate); b.setSample (0, i, x); b.setSample (1, i, x); } } };
            juce::AudioBuffer<float> out;
            sp.play (*p, 0.6, &out);
            return juce::Decibels::gainToDecibels (rmsBetween (out, 0.3, 0.6) / (0.1f / std::sqrt (2.0f)));
        };
        float worst = 0.0f;
        for (const auto hz : { 40.0, 60.0, 100.0, 150.0, 250.0, 400.0, 700.0, 1000.0, 1700.0, 2500.0, 4000.0, 6000.0, 10000.0 })
            worst = juce::jmax (worst, std::abs (flatAt (hz)));
        check (p->multibandRunning.load() && worst < 0.3f, "with a band compressing, the multiband is in, and its bands add up flat from 40 Hz to 10 kHz (within "
               + juce::String (worst, 2) + " dB)");

        // A track whose only problem is its dynamics still gets the conductor's turn
        suite::Conductor::reset();
        auto voc = make (6);
        set (*voc, qn::autoEqId, 0.0f);
        Player loud { [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto x = (std::fmod (t, 0.5) < 0.15 ? 0.4f : 0.05f) * (float) std::sin (juce::MathConstants<double>::twoPi * 700.0 * t);
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
        } };
        loud.play (*voc, QuintessenceProcessor::passSeconds + 0.3);
        suite::Conductor::request (0x1111u, 0.0f);   // one track has the turn...
        suite::Conductor::request (0xabcdu, 1.0f);   // ...another waits for it with a little need (1 dB of tone)
        suite::Conductor::release (0x1111u);
        voc->autoPass();
        check (v (*voc, qn::ratioId (3)) > 1.5f, "a vocal whose only problem is its dynamics goes ahead of a track with a 1 dB tone need");
        suite::Conductor::reset();

        // Projects saved with three compressor bands keep their settings, spread over the seven
        juce::ValueTree old ("QuintessenceState");
        auto param = [&old] (const juce::String& id, float val) { juce::ValueTree c ("PARAM"); c.setProperty ("id", id, nullptr); c.setProperty ("value", val, nullptr); old.appendChild (c, nullptr); };
        param (qn::thrId (0), -20.0f); param (qn::ratioId (0), 4.0f);
        param (qn::thrId (1), -15.0f); param (qn::ratioId (1), 2.0f); param (qn::mbLockId (1), 1.0f);
        param (qn::thrId (2), -10.0f); param (qn::ratioId (2), 3.0f);
        QuintessenceProcessor::migrateBands (old);
        auto get = [&old] (const juce::String& id) { for (int i = 0; i < old.getNumChildren(); ++i) if (old.getChild (i)["id"].toString() == id) return (float) (double) old.getChild (i)["value"]; return -999.0f; };
        check (get (qn::thrId (2)) == -20.0f && get (qn::ratioId (0)) == 4.0f && get (qn::thrId (4)) == -15.0f && get (qn::mbLockId (3)) == 1.0f
                   && get (qn::thrId (5)) == -10.0f && get (qn::ratioId (6)) == 3.0f,
               "an old project's low / mid / high settings move to sub-low-mid / mid-upper-mid / presence-air");
    }


    section ("The suite director: the mix around each moment; the premaster's headroom");
    {
        namespace dr = suite::director;
        dr::Plan plan;
        dr::Moment m;
        m.key = 1; m.kind = dr::kIntoDrop; m.atQ = 64.0f; m.startQ = 48.0f; m.endQ = 68.0f;
        m.hero = dr::pAzoth; m.heroGesture = dr::gAzImpact; m.gesture[dr::pAzoth] = dr::gAzImpact;
        m.premixDb = -2.0f; m.hookDipDb = -1.5f; m.buildHoldDb = 1.0f; m.narrowBefore = 0.5f; m.wideOnHit = 0.6f; m.lowClearBeats = 1.0f;
        m.silence = dr::gSiSnap; m.gesture[dr::pSilence] = dr::gSiSnap; m.silenceStartQ = 63.5f; m.silenceEndQ = 64.0f;
        plan.moments.push_back (m);
        plan.tension.assign (32, 0.5f);
        plan.hash = 0x1234u;
        suite::DirectorBus::publish (plan, 4.0, 138.0);
        suite::DirectorBus::Follower f;
        check (f.refresh(), "the plan reaches the track");
        using St = qn::DirectorStage;
        const auto pads = St::targetsAt (f, 62.0, 4), kick = St::targetsAt (f, 63.7, 1), hook = St::targetsAt (f, 64.5, 4), bass = St::targetsAt (f, 64.5, 2);
        check (pads.presenceDb < -1.0f && pads.gainDb < -0.4f && pads.side < 1.0f, "before an FX-led drop the pads make room, the build is held (a level ride), the image narrows");
        check (kick.silent && St::targetsAt (f, 63.7, 4).silent, "the director's snap: every track falls silent");
        check (hook.presenceDb <= -1.4f && hook.side > 1.0f && hook.gainDb == 0.0f, "the hook's first bar back: the pads step aside, wide, the build released");
        check (bass.subClear > 0.5f && ! St::targetsAt (f, 64.5, 1).silent, "the bass's sub clears for the impact");
        // the audio: silence in the snap, back after it
        St stage;
        stage.prepare (sampleRate);
        juce::AudioBuffer<float> buf (2, 4800);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4800; ++i) buf.setSample (c, i, 0.5f * std::sin (0.05f * (float) i));
        uint32_t silentTicks = 0;
        float* ch[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
        const auto qPerSample = 138.0 / 60.0 / sampleRate;
        stage.process (ch, 2, 4800, 63.55, qPerSample, f, 4, silentTicks);   // 0.1 s from inside the snap
        check (silentTicks > 0 && buf.getMagnitude (0, 2400, 400) < 0.01f, "the snap is silent on the track");
        suite::DirectorBus::withdraw();

        // the headroom keeper: over -6 dBFS the trim comes down, with room to spare it comes back
        using namespace suite::premaster;
        auto trim = headroom::step (-3.0f, 0.0f);
        check (trim < -0.2f, "the master peaking at -3 dBFS: every track comes down (" + juce::String (trim, 2) + " dB)");
        float t2 = trim;
        for (int i = 0; i < 40; ++i) t2 = headroom::step (-10.0f, t2);
        check (t2 > trim && t2 <= 0.0f, "plenty of room: it eases back towards 0 dB");
        headroom::publish (-3.0f);
        auto q = make (4);
        juce::AudioBuffer<float> trimmed;
        render (*q, 0.5, [] (juce::AudioBuffer<float>& b, int64_t pos) { for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.5f * std::sin (0.03f * (float) (pos + i))); }, &trimmed);
        struct { float peak; } st { peakBetween (trimmed, 0.25, 0.5) };
        check (std::abs (q->headroomTrimNow.load() + 3.0f) < 0.05f && st.peak < 0.45f, "every Quintessence applies the keeper's trim (-3 dB): no limiter, the same balance ("
                   + juce::String (q->headroomTrimNow.load(), 2) + " dB, peak " + juce::String (st.peak, 3) + ")");
        headroom::withdraw();
    }


    section ("Label-ready: each track measured and shared; a polarity flip and a level per section");
    {
        suite::FixBus::reset();
        suite::TrackTraits::reset();
        auto tone = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto v = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * t);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
        // the polarity flip: the same track, upside down
        auto a = make (4), b = make (4);
        b->setPlayHead (nullptr);
        suite::FixBus::send (suite::Fix::TrackPolarity, b->getInstanceId() & 0xfffu);
        b->fixes->poll();
        juce::AudioBuffer<float> outA, outB;
        render (*a, 0.4, tone, &outA);
        render (*b, 0.4, tone, &outB);
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (int i = (int) (0.2 * sampleRate); i < outA.getNumSamples() && i < outB.getNumSamples(); ++i)
        {
            ab += (double) outA.getSample (0, i) * outB.getSample (0, i); aa += (double) outA.getSample (0, i) * outA.getSample (0, i); bb += (double) outB.getSample (0, i) * outB.getSample (0, i);
        }
        const auto corr = aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
        check (b->polarityFlipped.load() && corr < -0.95, "a polarity flip turns the track upside down (correlation " + juce::String (corr, 3) + ")");
        // saved with the project
        juce::MemoryBlock mb;
        b->getStateInformation (mb);
        auto c = make (4);
        c->setStateInformation (mb.getData(), (int) mb.getSize());
        check (c->polarityFlipped.load(), "the flip is saved with the project");

        // a level for one kind of section (the lead kept on top)
        suite::SectionMap map;
        map.count = 2;
        map.sections[0].type = suite::SectionType::Drop; map.sections[0].startBar = 1;
        map.sections[1].type = suite::SectionType::Breakdown; map.sections[1].startBar = 9;
        map.totalBars = 16;
        suite::SectionBus::publish (map);
        auto d = make (4), e = make (4);
        suite::FixBus::send (suite::Fix::SectionTrim, suite::packSectionTrim (e->getInstanceId(), (int) suite::SectionType::Drop, -6.0f));
        e->fixes->poll();
        FakePlayHead head; head.bpm = 120.0; head.ppq = 0.0;
        d->setPlayHead (&head); e->setPlayHead (&head);
        juce::AudioBuffer<float> outD, outE;
        render (*d, 1.2, tone, &outD, &head);
        head.ppq = 0.0;
        render (*e, 1.2, tone, &outE, &head);
        const auto diff = juce::Decibels::gainToDecibels (rmsBetween (outE, 0.5, 1.0) / juce::jmax (1.0e-9f, rmsBetween (outD, 0.5, 1.0)));
        check (std::abs (diff + 6.0f) < 0.5f && std::abs (e->sectionTrimDb[(size_t) suite::SectionType::Drop].load() + 6.0f) < 0.01f,
               "a section trim moves the track's level in that kind of section only (" + juce::String (diff, 2) + " dB in the drop)");

        // what the track is like, shared for the label-ready checks
        e->traitsTick = 4;
        e->pollTick();
        bool found = false;
        for (const auto& t : suite::TrackTraits::list())
            if (t.id == e->getInstanceId())
                found = t.role == 4 && t.inPeakDb > -12.0f && t.inPeakDb < -6.0f && t.sectionDb[0] > -40.0f && t.sectionDb[1] < -90.0f && t.lowMidDb > t.lowSideDb + 20.0f;
        check (found, "each Quintessence shares what its track is like: the input's peak, the level in each section, the stereo lows");
        suite::SectionBus::withdraw();
        suite::TrackTraits::reset();
    }


    section ("Label-ready: punch, the vocal onto the grid, pad motion, the breakdown low cut, input staging");
    {
        suite::FixBus::reset();
        auto p = make (3);
        const auto before = p->apvts.getRawParameterValue (qn::punchId)->load();
        suite::FixBus::send (suite::Fix::TrackPunch, suite::packMasterTone (p->getInstanceId(), 0, 1.0f));
        p->fixes->poll();
        check (std::abs (p->apvts.getRawParameterValue (qn::punchId)->load() - before - 0.15f) < 0.01f, "a punch step on this track's transient shaper");
        auto v = make (6);
        suite::FixBus::send (suite::Fix::VocalDelay, suite::packMasterTone (v->getInstanceId(), 0, 12.0f));
        v->fixes->poll();
        render (*v, 1.5, [] (juce::AudioBuffer<float>& b, int64_t pos) { for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.2f * std::sin (0.02f * (float) (pos + i))); });
        check (std::abs (v->labelDsp.delayInUseMs() - 12.0f) < 0.5f, "an early vocal delayed 12 ms (" + juce::String (v->labelDsp.delayInUseMs(), 2) + ")");
        // the DSP on its own: the breakdown low cut, input staging, pad motion
        qn::LabelDsp d;
        d.prepare (sampleRate);
        auto lowRms = [&] (int sectionType, bool cut)
        {
            d.breakdownLowCut.store (cut);
            juce::AudioBuffer<float> b (2, (int) sampleRate);
            double p2 = 0.0;
            for (int rep = 0; rep < 3; ++rep)
            {
                for (int i = 0; i < b.getNumSamples(); ++i) { const auto x = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 50.0f * (float) i / (float) sampleRate); b.setSample (0, i, x); b.setSample (1, i, x); }
                d.output (b.getArrayOfWritePointers(), 2, b.getNumSamples(), 0.0, 120.0, 4.0, sectionType, 4, true);
                if (rep == 2) for (int i = 0; i < b.getNumSamples(); ++i) p2 += (double) b.getSample (0, i) * b.getSample (0, i);
            }
            return std::sqrt (p2 / sampleRate);
        };
        const auto inDrop = lowRms (4, true), inBreak = lowRms (2, true);
        check (inBreak < inDrop * 0.5, "the breakdown low cut takes the 50 Hz out in the breakdowns only (" + juce::String (inBreak / inDrop, 2) + " of the drop's)");
        qn::LabelDsp g;
        g.prepare (sampleRate);
        g.inputStage.store (true); g.stageDb.store (9.0f);
        juce::AudioBuffer<float> b (2, 512);
        float worst = 0.0f;
        for (int blk = 0; blk < 200; ++blk)
        {
            for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.01f); b.setSample (1, i, 0.01f); }
            g.input (b.getArrayOfWritePointers(), 2, 512);
            const auto staged = b.getSample (0, 0);
            g.output (b.getArrayOfWritePointers(), 2, 512, 0.0, 120.0, 4.0, 4, 4, true);
            if (blk == 199) worst = std::abs (b.getSample (0, 0) - 0.01f) + (staged > 0.02f ? 0.0f : 1.0f);
        }
        check (worst < 1.0e-4f, "input staging: the processing sees +9 dB, the level going out is unchanged");

        // latched where Ouroboros' checks ask: a pad that never moves gets pad motion
        suite::SectionBus::withdraw();
        auto pad = make (4);
        suite::LabelReadyBus::Needs needs {};
        needs[(size_t) suite::Autopilot::padMotionF / 32] |= 1u << (suite::Autopilot::padMotionF % 32);
        suite::LabelReadyBus::publishNeeds (needs);
        FakePlayHead head; head.bpm = 120.0; head.ppq = 0.0;
        pad->setPlayHead (&head);
        render (*pad, 4.0, [] (juce::AudioBuffer<float>& bb, int64_t pos) { for (int c = 0; c < 2; ++c) for (int i = 0; i < bb.getNumSamples(); ++i) bb.setSample (c, i, 0.2f * std::sin (0.05f * (float) (pos + i))); }, nullptr, &head);
        for (int k = 0; k < 5; ++k) pad->pollTick();
        check ((pad->labelLatched & 1u) != 0 && pad->labelDsp.padMotion.load(), "a pad that never moves gets a slow filter movement (latched)");
        juce::MemoryBlock mb;
        pad->getStateInformation (mb);
        auto again = make (4);
        again->setStateInformation (mb.getData(), (int) mb.getSize());
        check ((again->labelLatched & 1u) != 0, "and keeps it with the project");
        suite::LabelReadyBus::reset();
        suite::FixBus::reset();
    }


    section ("Label-ready 4: a tone and a width per section; kick tail, sub hand-over, choke, subsonics, drive");
    {
        suite::FixBus::reset();
        auto q = make (4);
        suite::FixBus::send (suite::Fix::SectionTone, suite::packSectionTone (q->getInstanceId(), (int) suite::SectionType::Drop, 6, -1.5f));
        q->fixes->poll();
        suite::FixBus::send (suite::Fix::SectionWidth, suite::packSectionTrim (q->getInstanceId(), (int) suite::SectionType::Breakdown, 2.0f));
        q->fixes->poll();
        check (std::abs (q->labelDsp2.toneDb[(size_t) suite::SectionType::Drop][6].load() + 1.5f) < 0.01f && std::abs (q->labelDsp2.widthOffset[(size_t) suite::SectionType::Breakdown].load() - 0.2f) < 0.01f,
               "a tone step in the drops only, a width step in the breakdowns only");
        juce::MemoryBlock mb;
        q->getStateInformation (mb);
        auto q2 = make (4);
        q2->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q2->labelDsp2.toneDb[(size_t) suite::SectionType::Drop][6].load() + 1.5f) < 0.01f, "kept with the project");
        // the DSP on its own
        auto runRms = [] (qn::LabelDsp2& d, qn::LabelDsp2::Context cx, std::function<float (int)> sig, int from, int to)
        {
            juce::AudioBuffer<float> b (2, 512);
            double p = 0.0; int n = 0, pos = 0;
            for (int blk = 0; blk < (int) (2.0 * sampleRate / 512); ++blk)
            {
                for (int i = 0; i < 512; ++i) { const auto v = sig (pos + i); b.setSample (0, i, v); b.setSample (1, i, v); }
                cx.ppq = (double) pos / sampleRate * 2.0;
                d.process (b.getArrayOfWritePointers(), 2, 512, cx);
                for (int i = 0; i < 512; ++i) if (pos + i >= from && pos + i < to) { p += (double) b.getSample (0, i) * b.getSample (0, i); ++n; }
                pos += 512;
            }
            return n > 0 ? (float) std::sqrt (p / n) : 0.0f;
        };
        const auto sr = (int) sampleRate;
        {
            qn::LabelDsp2 d; d.prepare (sampleRate);
            d.toneDb[4][6].store (-6.0f); d.sectionTone.store (true);
            qn::LabelDsp2::Context cx; cx.sectionType = 4; cx.role = 4;
            const auto tone2k = [] (int i) { return 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 2000.0f * (float) i / (float) sampleRate); };
            const auto cut = runRms (d, cx, tone2k, sr, 2 * sr);
            qn::LabelDsp2 flat; flat.prepare (sampleRate);
            const auto plain = runRms (flat, cx, tone2k, sr, 2 * sr);
            check (cut < plain * 0.7f, "the drop's tone step takes 2 kHz down (" + juce::String (juce::Decibels::gainToDecibels (cut / plain), 1) + " dB)");
        }
        {
            qn::LabelDsp2 d; d.prepare (sampleRate);
            d.kickTail.store (true);
            qn::LabelDsp2::Context cx; cx.sectionType = 4; cx.role = 1;
            const auto beat = sr / 2;
            const auto kick = [beat] (int i) { const auto k = i % beat; return 0.6f * std::exp (-(float) k / (0.15f * (float) sampleRate)) * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * (float) k / (float) sampleRate); };
            qn::LabelDsp2 flat; flat.prepare (sampleRate);
            const auto lateTail = [&] (qn::LabelDsp2& x) { double p = 0.0; juce::ignoreUnused (p); return runRms (x, cx, kick, sr + (int) (0.25 * sampleRate), sr + (int) (0.45 * sampleRate)); };
            const auto shortened = lateTail (d), full = lateTail (flat);
            check (shortened < full * 0.75f, "the kick's tail shortened in the drops (" + juce::String (juce::Decibels::gainToDecibels (shortened / full), 1) + " dB late in the beat)");
        }
        {
            qn::LabelDsp2 d; d.prepare (sampleRate);
            d.choke.store (true);
            qn::LabelDsp2::Context cx; cx.sectionType = 3; cx.role = 3; cx.nextIsDrop = true; cx.playing = true; cx.sectionEndQ = 3.0;   // the drop at 1.5 s
            const auto hiss = [] (int i) { return 0.2f * std::sin (0.7f * (float) i); };
            const auto inChoke = runRms (d, cx, hiss, (int) (1.42 * sampleRate), (int) (1.49 * sampleRate));
            check (inChoke < 0.02f, "hats cut for the last 16th before the drop (" + juce::String (inChoke, 3) + ")");
        }
        {
            qn::LabelDsp2 d; d.prepare (sampleRate);
            d.subsonic.store (true);
            qn::LabelDsp2::Context cx; cx.sectionType = 4; cx.role = 4;
            const auto rumble = [] (int i) { return 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 8.0f * (float) i / (float) sampleRate); };
            check (runRms (d, cx, rumble, sr, 2 * sr) < 0.3f * 0.707f * 0.5f, "8 Hz rumble filtered out");
            qn::LabelDsp2 h; h.prepare (sampleRate);
            h.subHandover.store (true);
            qn::LabelDsp2::Context b; b.sectionType = 3; b.role = 2; b.buildProgress = 1.0f;
            const auto sub = [] (int i) { return 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 50.0f * (float) i / (float) sampleRate); };
            check (runRms (h, b, sub, sr, 2 * sr) < 0.3f * 0.707f * 0.7f, "at the end of a build the bass's sub is handed over (lower)");
            qn::LabelDsp2 dr; dr.prepare (sampleRate);
            dr.drive.store (true);
            const auto quiet = [] (int i) { return 0.05f * std::sin (0.05f * (float) i); };
            const auto out = runRms (dr, cx, quiet, sr, 2 * sr);
            check (std::abs (out / (0.05f * 0.707f) - 1.0f) < 0.05f, "drive keeps a quiet part's level (it only rounds the peaks)");
        }
        suite::FixBus::reset();
    }


    section ("Mix fixes: phrase rides, pan, latching, and the DSP (LabelDsp3)");
    {
        namespace AP = suite::Autopilot;
        suite::FixBus::reset();
        auto q = make (4);
        suite::FixBus::send (suite::Fix::PhraseTrim, suite::packPhraseTrim (q->getInstanceId(), 3, 1.5f));
        q->fixes->poll();
        suite::FixBus::send (suite::Fix::TrackPan, suite::packMasterTone (q->getInstanceId(), 0, 2.0f));
        q->fixes->poll();
        check (std::abs (q->labelDsp3.phraseTrimDb[3].load() - 1.5f) < 0.01f && std::abs (q->labelDsp3.panOffset.load() - 0.2f) < 0.01f, "a phrase ride on bars 13-16 and a pan step from Ouroboros");
        {
            uint32_t low; int ph; float d;
            suite::unpackPhraseTrim (suite::packPhraseTrim (0xabc, 63, -3.5f), low, ph, d);
            check (low == 0xabc && ph == 63 && std::abs (d + 3.5f) < 0.01f, "phrase rides pack into a fix message (track, phrase, half dB)");
        }
        juce::MemoryBlock mb;
        q->getStateInformation (mb);
        auto q2 = make (4);
        q2->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q2->labelDsp3.phraseTrimDb[3].load() - 1.5f) < 0.01f && std::abs (q2->labelDsp3.panOffset.load() - 0.2f) < 0.01f, "kept with the project");
        // latched once Ouroboros asks, only where it applies (a bass fix on a bass, not on a pad)
        {
            suite::LabelReadyBus::Needs needs {};
            needs[(size_t) AP::mxBassLowDuckF / 32] |= 1u << (AP::mxBassLowDuckF % 32);
            suite::LabelReadyBus::publishNeeds (needs);
            auto bass = make (2);
            const auto t = bass->traits.snapshot (bass->getInstanceId(), 2, "Bass");
            bass->latchMixFixes (t);
            q->latchMixFixes (q->traits.snapshot (q->getInstanceId(), 4, "Pad"));
            check (bass->labelDsp3.on[qn::LabelDsp3::bassLowDuck].load() && ! q->labelDsp3.on[qn::LabelDsp3::bassLowDuck].load(), "the bass's low duck latched on the bass only");
            AP::set (AP::mxBassLowDuckF, false);
            bass->latchMixFixes (t);
            check (! bass->labelDsp3.on[qn::LabelDsp3::bassLowDuck].load(), "its switch turns it off");
            AP::set (AP::mxBassLowDuckF, true);
            suite::LabelReadyBus::reset();
            check (AP::numFeatures > 200 && juce::String (AP::featureName (AP::mxDcBlockF)).startsWith ("Mix:"), "every mix fix has its own switch");
        }
        // the DSP on its own
        const auto sr = (int) sampleRate;
        auto run = [] (qn::LabelDsp3& d, qn::LabelDsp3::Context cx, std::function<float (int, int)> sig, int from, int to, int channel = 0)
        {
            juce::AudioBuffer<float> b (2, 512);
            double p = 0.0; int n = 0, pos = 0;
            for (int blk = 0; blk < (int) (2.0 * sampleRate / 512); ++blk)
            {
                for (int i = 0; i < 512; ++i) { b.setSample (0, i, sig (pos + i, 0)); b.setSample (1, i, sig (pos + i, 1)); }
                cx.ppq = (double) pos / sampleRate * 2.0;
                d.process (b.getArrayOfWritePointers(), 2, 512, cx);
                for (int i = 0; i < 512; ++i) if (pos + i >= from && pos + i < to) { p += (double) b.getSample (channel, i) * b.getSample (channel, i); ++n; }
                pos += 512;
            }
            return n > 0 ? (float) std::sqrt (p / n) : 0.0f;
        };
        auto sine = [] (float hz, float a) { return [hz, a] (int i, int) { return a * std::sin (2.0f * juce::MathConstants<float>::pi * hz * (float) i / (float) sampleRate); }; };
        auto with = [] (int fix) { auto d = std::make_unique<qn::LabelDsp3>(); d->prepare (sampleRate); d->on[(size_t) fix].store (true); return d; };
        auto plain = [] { auto d = std::make_unique<qn::LabelDsp3>(); d->prepare (sampleRate); return d; };
        auto ratioDb = [] (float a, float b) { return juce::Decibels::gainToDecibels (a / juce::jmax (1.0e-9f, b)); };
        {
            qn::LabelDsp3::Context cx; cx.role = 2; cx.playing = true; cx.sectionType = 4;
            auto d = with (qn::LabelDsp3::bassLowDuck);
            // 120 BPM: a beat every 0.5 s; just after the beat against the middle of it
            const auto near = run (*d, cx, sine (50.0f, 0.3f), sr + 441, sr + 2205), mid = run (*with (qn::LabelDsp3::bassLowDuck), cx, sine (50.0f, 0.3f), sr + 8820, sr + 10584);
            check (ratioDb (near, mid) < -3.0f, "the bass's lows duck just after each kick (" + juce::String (ratioDb (near, mid), 1) + " dB)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4;
            auto d = with (qn::LabelDsp3::dcBlock);
            const auto out = run (*d, cx, [] (int, int) { return 0.2f; }, sr, 2 * sr);
            check (out < 0.01f, "DC removed (" + juce::String (out, 4) + " left)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 3; cx.sectionType = 2;
            const auto cut = run (*with (qn::LabelDsp3::breakdownDrumHighCut), cx, sine (12000.0f, 0.3f), sr, 2 * sr);
            const auto lowCut = run (*with (qn::LabelDsp3::breakdownDrumLowCut), cx, sine (60.0f, 0.3f), sr, 2 * sr);
            cx.sectionType = 4;
            const auto dropTop = run (*with (qn::LabelDsp3::breakdownDrumHighCut), cx, sine (12000.0f, 0.3f), sr, 2 * sr);
            check (ratioDb (cut, dropTop) < -6.0f && lowCut < 0.3f * 0.707f * 0.5f, "drums thinned in the breakdowns only (top " + juce::String (ratioDb (cut, dropTop), 1) + " dB, lows cut)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4; cx.sectionType = 3; cx.buildProgress = 1.0f;
            const auto hp = run (*with (qn::LabelDsp3::buildHighPass), cx, sine (100.0f, 0.3f), sr, 2 * sr), flat = run (*plain(), cx, sine (100.0f, 0.3f), sr, 2 * sr);
            check (ratioDb (hp, flat) < -6.0f, "the high-pass rises through the build (100 Hz " + juce::String (ratioDb (hp, flat), 1) + " dB at its end)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4; cx.dropHit = 1.0f;
            const auto ducked = run (*with (qn::LabelDsp3::impactDuck), cx, sine (300.0f, 0.3f), sr, 2 * sr), flat = run (*plain(), cx, sine (300.0f, 0.3f), sr, 2 * sr);
            check (ratioDb (ducked, flat) < -0.7f && ratioDb (ducked, flat) > -1.5f, "the pads dip 1 dB under the drop's impact (" + juce::String (ratioDb (ducked, flat), 2) + " dB)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4;
            auto anti = [] (int i, int ch) { const auto v = 0.3f * std::sin (0.05f * (float) i); return ch == 0 ? v : -v; };
            const auto guarded = run (*with (qn::LabelDsp3::correlationGuard), cx, anti, sr, 2 * sr), flat = run (*plain(), cx, anti, sr, 2 * sr);
            check (ratioDb (guarded, flat) < -1.5f, "an image that cancels in mono is narrowed (" + juce::String (ratioDb (guarded, flat), 1) + " dB of side)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4;
            auto d = plain();
            d->panOffset.store (0.5f);
            const auto left = run (*d, cx, sine (300.0f, 0.3f), sr, 2 * sr, 0);
            auto d2 = plain(); d2->panOffset.store (0.5f);
            const auto right = run (*d2, cx, sine (300.0f, 0.3f), sr, 2 * sr, 1);
            check (left < right * 0.6f, "a pan offset moves the part to the right");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4; cx.leadActivity = 1.0f; cx.leadHz = 440.0f;
            const auto dipped = run (*with (qn::LabelDsp3::padUnderLead), cx, sine (440.0f, 0.3f), sr, 2 * sr), flat = run (*plain(), cx, sine (440.0f, 0.3f), sr, 2 * sr);
            const auto other = run (*with (qn::LabelDsp3::padUnderLead), cx, sine (2000.0f, 0.3f), sr, 2 * sr), otherFlat = run (*plain(), cx, sine (2000.0f, 0.3f), sr, 2 * sr);
            check (ratioDb (dipped, flat) < -1.5f && std::abs (ratioDb (other, otherFlat)) < 0.5f, "the pad dips at the lead's note only (" + juce::String (ratioDb (dipped, flat), 1) + " dB at 440 Hz)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4;
            auto d = with (qn::LabelDsp3::truePeak);
            run (*d, cx, sine (11000.0f, 0.99f), 0, sr);
            d->tick();
            check (d->truePeakTrimDb.load() < -0.1f, "peaks between the samples trimmed at the source (" + juce::String (d->truePeakTrimDb.load(), 1) + " dB)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 7; cx.sectionType = 3;
            cx.energyRatio = 1.5f;
            const auto up = run (*with (qn::LabelDsp3::riserEnergy), cx, sine (2000.0f, 0.2f), sr, 2 * sr), flat = run (*plain(), cx, sine (2000.0f, 0.2f), sr, 2 * sr);
            check (ratioDb (up, flat) > 1.0f, "the riser follows the build's energy (" + juce::String (ratioDb (up, flat), 1) + " dB)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4;
            auto d = with (qn::LabelDsp3::expander);
            d->expThreshold = 0.05f;
            const auto quiet = run (*d, cx, sine (300.0f, 0.01f), sr, 2 * sr);
            check (quiet < 0.01f * 0.707f * 0.5f, "the expander pulls the noise floor down");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4; cx.playing = true;
            auto d = plain();
            d->on[qn::LabelDsp3::phraseTrim].store (true);
            d->phraseTrimDb[0].store (-6.0f);
            const auto rode = run (*d, cx, sine (300.0f, 0.3f), sr, 2 * sr), flat = run (*plain(), cx, sine (300.0f, 0.3f), sr, 2 * sr);
            check (std::abs (ratioDb (rode, flat) + 6.0f) < 0.5f, "a phrase ride moves its phrase (" + juce::String (ratioDb (rode, flat), 1) + " dB)");
        }
        suite::FixBus::reset();
    }


    section ("Mix fixes 2: the shared latch, LabelDsp4, the budgets, the fail-safe, and the stack test");
    {
        namespace AP = suite::Autopilot;
        AP::reset(); suite::LabelReadyBus::reset(); suite::FixBus::reset(); suite::MixFixBus::reset();
        // the band-pass at unity at its centre (the keyed dips were cutting far deeper than they said)
        {
            suite::Svf f; f.set (1000.0f, 4.0f, (float) sampleRate);
            double in = 0.0, out = 0.0;
            for (int i = 0; i < (int) sampleRate; ++i)
            {
                const auto x = std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / (float) sampleRate);
                const auto y = f.bandpassNorm (x);
                if (i > (int) sampleRate / 2) { in += (double) x * x; out += (double) y * y; }
            }
            check (std::abs (juce::Decibels::gainToDecibels ((float) std::sqrt (out / in))) < 0.2f, "the normalised band-pass is unity at its centre");
        }
        // the shared latch: asked for, applies, latched; revoked by the self-check; off for a song; reset and undo; frozen while rendering
        {
            auto q = make (2);
            suite::MixLatch& ml = q->mixLatch;
            suite::LabelReadyBus::Needs n {};
            suite::LabelReadyBus::setBit (n, (int) AP::mx2SubPerNoteF);
            suite::LabelReadyBus::publishNeeds (n);
            ml.beginTick (true);
            check (! ml.update (AP::mx2SubPerNoteF, true), "nothing switches on while FL renders");
            ml.beginTick (false);
            check (ml.update (AP::mx2SubPerNoteF, true) && ml.isLatched (AP::mx2SubPerNoteF), "asked for and it applies: latched");
            check (! ml.update (AP::mx2BassChokeF, true), "not asked for: off");
            suite::LabelReadyBus::publishRevoked (n);
            ml.beginTick (false);
            check (! ml.update (AP::mx2SubPerNoteF, true) && ml.stateOf (AP::mx2SubPerNoteF) == suite::MixLatch::offForSong, "revoked by the self-check: off for this song");
            suite::LabelReadyBus::publishRevoked ({});
            ml.beginTick (false);
            check (! ml.update (AP::mx2SubPerNoteF, true), "and stays off (asked again or not)");
            auto undo = ml.resetAll();
            check (ml.stateOf (AP::mx2SubPerNoteF) == suite::MixLatch::idle, "reset: every fix starts again");
            undo();
            check (ml.stateOf (AP::mx2SubPerNoteF) == suite::MixLatch::offForSong, "undo puts them back");
            suite::FixBus::send (suite::Fix::MixFixReset);
            q->fixes->poll();
            ml.beginTick (false);
            check (ml.update (AP::mx2SubPerNoteF, true), "a reset from Ouroboros: it latches again where it's asked for");
            suite::FixBus::send (suite::Fix::MixFixOff, suite::packMixFix (q->getInstanceId(), (int) AP::mx2SubPerNoteF));
            q->fixes->poll();
            ml.beginTick (false);
            check (! ml.update (AP::mx2SubPerNoteF, true), "switched off for this track from Ouroboros' list");
            ml.beginTick (false);
            AP::setMixBypassed (true);
            suite::LabelReadyBus::Needs n2 {}; suite::LabelReadyBus::setBit (n2, (int) AP::mx2BassChokeF); suite::LabelReadyBus::publishNeeds (n2);
            check (! ml.update (AP::mx2BassChokeF, true) && ml.isLatched (AP::mx2BassChokeF), "the whole-mix A/B: latched but bypassed");
            AP::setMixBypassed (false);
            ml.publish (q->getInstanceId(), "Quintessence", "Bass");
            const auto all = suite::MixFixBus::all();
            check (! all.empty() && all.front().trackName() == "Bass", "what's on is published for Ouroboros' list");
            suite::LabelReadyBus::reset();
        }
        // ownership: a knob a fix set and you then moved is yours
        {
            auto q = make (4);
            q->mixLatch.setParam (qn::widthId, 1.3f);
            q->mixLatch.watchOwnership();
            check (! q->mixLatch.owned (qn::widthId), "a knob the fix set is still the fix's");
            set (*q, qn::widthId, 0.8f);
            q->mixLatch.watchOwnership();
            check (q->mixLatch.owned (qn::widthId) && ! q->mixLatch.setParam (qn::widthId, 1.5f), "moved by you: yours, the fix leaves it alone");
        }
        // the DSP
        const auto sr = (int) sampleRate;
        auto run4 = [] (qn::LabelDsp4& d, qn::LabelDsp3::Context cx, std::function<float (int, int)> sig, int from, int to, int channel = -1, std::function<void (int)> each = {})
        {
            juce::AudioBuffer<float> b (2, 512);
            double p = 0.0; int n = 0, pos = 0;
            qn::LabelDsp4::Extra x;
            for (int blk = 0; blk < (int) (2.0 * sampleRate / 512); ++blk)
            {
                for (int i = 0; i < 512; ++i) { b.setSample (0, i, sig (pos + i, 0)); b.setSample (1, i, sig (pos + i, 1)); }
                cx.ppq = (double) pos / sampleRate * 2.0;
                if (each) each (pos);
                d.process (b.getArrayOfWritePointers(), 2, 512, cx, x);
                for (int i = 0; i < 512; ++i)
                    if (pos + i >= from && pos + i < to)
                        for (int c = 0; c < 2; ++c) if (channel < 0 || channel == c) { p += (double) b.getSample (c, i) * b.getSample (c, i); ++n; }
                pos += 512;
            }
            return n > 0 ? (float) std::sqrt (p / n) : 0.0f;
        };
        auto sine = [] (float hz, float a) { return [hz, a] (int i, int) { return a * std::sin (2.0f * juce::MathConstants<float>::pi * hz * (float) i / (float) sampleRate); }; };
        auto with4 = [] (int fix) { auto d = std::make_unique<qn::LabelDsp4>(); d->prepare (sampleRate); d->on[(size_t) fix].store (true); return d; };
        auto ratioDb = [] (float a, float b) { return juce::Decibels::gainToDecibels (a / juce::jmax (1.0e-9f, b)); };
        {
            qn::LabelDsp3::Context cx; cx.role = 2;
            auto lop = [] (int i, int) { const auto w = 2.0f * juce::MathConstants<float>::pi * 60.0f * (float) i / (float) sampleRate; return 0.3f * (std::sin (w) + 0.5f * std::cos (2.0f * w)); };
            auto flat = std::make_unique<qn::LabelDsp4>(); flat->prepare (sampleRate);
            const auto rotated = run4 (*with4 (qn::LabelDsp4::phaseRotate), cx, lop, sr, 2 * sr), plain = run4 (*flat, cx, lop, sr, 2 * sr);
            check (std::abs (ratioDb (rotated, plain)) < 0.3f, "phase rotation keeps the level (an all-pass: " + juce::String (ratioDb (rotated, plain), 2) + " dB)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 2; cx.playing = true; cx.nextIsDrop = true; cx.sectionType = 3; cx.sectionEndQ = 3.0;   // the drop at 1.5 s
            const auto choked = run4 (*with4 (qn::LabelDsp4::bassChoke), cx, sine (50.0f, 0.3f), (int) (1.45 * sampleRate), (int) (1.49 * sampleRate));
            check (choked < 0.3f * 0.707f * 0.2f && choked > 0.0f, "the bass steps out for the last 16th before the drop (never silent: " + juce::String (ratioDb (choked, 0.3f * 0.707f), 1) + " dB)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 7; cx.sectionType = 3; cx.buildProgress = 1.0f;
            auto wide = [] (int i, int ch) { const auto v = 0.2f * std::sin (0.03f * (float) i); return ch == 0 ? v : 0.5f * v; };
            auto side = [&] (qn::LabelDsp4& d) { juce::ignoreUnused (d); return 0.0f; };
            juce::ignoreUnused (side);
            auto flat = std::make_unique<qn::LabelDsp4>(); flat->prepare (sampleRate);
            const auto w = run4 (*with4 (qn::LabelDsp4::riserWiden), cx, wide, sr, 2 * sr, 0), p = run4 (*flat, cx, wide, sr, 2 * sr, 0);
            check (w > p * 1.05f, "the riser widens toward the end of the build");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4;
            auto d = with4 (qn::LabelDsp4::haas);
            const auto lag = (int) (0.002 * sampleRate);
            auto noise = std::make_shared<std::vector<float>> ((size_t) (3 * sampleRate));
            juce::Random rnd (7);
            for (auto& v : *noise) v = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f);
            auto delayed = [lag, noise] (int i, int ch) { const auto k = ch == 1 ? i - lag : i; return k < 0 || k >= (int) noise->size() ? 0.0f : (*noise)[(size_t) k]; };
            run4 (*d, cx, delayed, 0, 1);
            d->tick();
            check (std::abs (d->haasMs.load() - 2.0f) < 0.4f, "a 2 ms left/right delay found (" + juce::String (d->haasMs.load(), 2) + " ms)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 4;
            const auto right = run4 (*with4 (qn::LabelDsp4::panLimit), cx, [] (int i, int ch) { return ch == 0 ? 0.3f * std::sin (0.05f * (float) i) : 0.0f; }, sr, 2 * sr, 1);
            check (right > 0.01f, "a part panned hard left gets some of itself on the right (nothing past 70%)");
        }
        {
            qn::LabelDsp3::Context cx; cx.role = 6; cx.sectionType = 4; cx.leadActivity = 1.0f;
            auto flat = std::make_unique<qn::LabelDsp4>(); flat->prepare (sampleRate);
            const auto dipped = run4 (*with4 (qn::LabelDsp4::vocalVsLead), cx, sine (500.0f, 0.2f), sr, 2 * sr), p = run4 (*flat, cx, sine (500.0f, 0.2f), sr, 2 * sr);
            check (std::abs (ratioDb (dipped, p) + 1.5f) < 0.3f, "in the drop the lead leads: the vocal 1.5 dB under it (" + juce::String (ratioDb (dipped, p), 2) + " dB)");
            cx.strength = 0.5f;
            const auto half = run4 (*with4 (qn::LabelDsp4::vocalVsLead), cx, sine (500.0f, 0.2f), sr, 2 * sr);
            check (std::abs (ratioDb (half, p) + 0.75f) < 0.3f, "at 50% strength, half the move (" + juce::String (ratioDb (half, p), 2) + " dB)");
        }
        // the budgets and the headroom forecast (LabelDsp3)
        {
            qn::LabelDsp3 d; d.prepare (sampleRate);
            d.on[qn::LabelDsp3::phraseTrim].store (true);
            d.phraseTrimDb[0].store (-6.0f);
            qn::LabelDsp3::Context cx; cx.role = 4; cx.playing = true; cx.levelBudgetDb = 2.0f;
            juce::AudioBuffer<float> b (2, 512);
            for (int k = 0; k < 200; ++k) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, 0.1f); d.process (b.getArrayOfWritePointers(), 2, 512, cx); }
            check (std::abs (d.lastDb + 2.0f) < 0.01f && d.budgetHit, "the level budget holds a -6 dB ride to the 2 dB left");
            qn::LabelDsp3 h; h.prepare (sampleRate);
            h.on[qn::LabelDsp3::hookLift].store (true);
            qn::LabelDsp3::Context hx; hx.role = 5; hx.sectionType = 4; hx.liftsAllowed = 0.0f;
            h.process (b.getArrayOfWritePointers(), 2, 512, hx);
            check (std::abs (h.lastDb) < 0.01f, "with the headroom short, no lift");
        }
        // the fail-safe: a runaway level puts the dry signal back and switches the fixes off
        {
            auto q = make (4);
            q->labelDsp3.truePeakTrimDb.store (40.0f);   // a fix gone wrong: +40 dB
            juce::AudioBuffer<float> out;
            const auto st = render (*q, 1.0, [] (juce::AudioBuffer<float>& b, int64_t pos) { for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.1f * std::sin (0.03f * (float) (pos + i))); }, &out);
            check (q->failsafeOff.load() && st.peak < 0.5f, "a runaway fix: caught, the dry signal back (peak " + juce::String (st.peak, 2) + "), the fixes off for this song");
            q->latchMixFixes (q->traits.snapshot (q->getInstanceId(), 4, "Pad"));
            suite::FixBus::send (suite::Fix::MixFixReset);
            q->fixes->poll();
            check (! q->failsafeOff.load(), "a reset from Ouroboros lets them back on");
        }
        // hear what the fixes remove: with nothing changing it, silence
        {
            auto q = make (4);
            suite::FixBus::send (suite::Fix::DeltaListen, suite::packMixFix (q->getInstanceId(), 1));
            q->fixes->poll();
            juce::AudioBuffer<float> out;
            render (*q, 1.0, [] (juce::AudioBuffer<float>& b, int64_t pos) { for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.1f * std::sin (0.03f * (float) (pos + i))); }, &out);
            check (q->deltaListen.load() && rmsBetween (out, 0.5, 1.0) < 1.0e-4f, "hear what the fixes remove: nothing to hear when they change nothing");
            suite::FixBus::send (suite::Fix::DeltaListen, suite::packMixFix (0, 0));
            q->fixes->poll();
            check (! q->deltaListen.load(), "and off again");
        }
        // the stack test: a made-up song, every part through every mix fix at once, at three sample rates
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            struct Part { int role; float hz; float amp; bool hits; };
            const Part parts[] = { { 1, 55.0f, 0.5f, true }, { 2, 45.0f, 0.3f, false }, { 3, 9000.0f, 0.1f, true }, { 4, 330.0f, 0.15f, false },
                                   { 5, 880.0f, 0.15f, false }, { 6, 440.0f, 0.15f, false }, { 7, 3000.0f, 0.08f, false } };
            const auto bpm = 128.0, barQ = 4.0, secs = 4.0 * 2.0 * barQ * 60.0 / bpm;   // four sections of two bars
            const auto total = (int) (secs * rate);
            const int types[4] = { 0, 3, 4, 2 };   // intro, build, drop, breakdown
            juce::AudioBuffer<float> sumIn (2, total), sumOut (2, total);
            sumIn.clear(); sumOut.clear();
            bool finite = true; float worstLevel = 0.0f; int worstRole = 0;
            for (const auto& part : parts)
            {
                qn::LabelDsp3 d3; d3.prepare (rate);
                qn::LabelDsp4 d4; d4.prepare (rate);
                for (auto& o : d3.on) o.store (true);
                for (auto& o : d4.on) o.store (true);
                d3.isBus = false; d3.monoInput = part.role == 4; d3.expThreshold = 0.0f; d3.lowCutHz.store (30.0f);
                juce::AudioBuffer<float> b (2, 512);
                double pin = 0.0, pout = 0.0;
                for (int pos = 0; pos < total; pos += 512)
                {
                    const auto n = juce::jmin (512, total - pos);
                    for (int i = 0; i < n; ++i)
                    {
                        const auto t = (double) (pos + i) / rate;
                        auto env = 1.0f;
                        if (part.hits) { const auto beat = std::fmod (t * bpm / 60.0, 1.0); env = (float) std::exp (-beat * 8.0); }
                        const auto v = part.amp * env * (float) std::sin (2.0 * juce::MathConstants<double>::pi * part.hz * t);
                        b.setSample (0, i, v); b.setSample (1, i, part.role >= 4 ? v * 0.8f : v);
                        sumIn.addSample (0, pos + i, v); sumIn.addSample (1, pos + i, part.role >= 4 ? v * 0.8f : v);
                        pin += (double) v * v;
                    }
                    qn::LabelDsp3::Context cx;
                    cx.ppq = (double) pos / rate * bpm / 60.0; cx.bpm = bpm; cx.barQ = barQ; cx.role = part.role; cx.playing = true;
                    const auto sec = juce::jlimit (0, 3, (int) (cx.ppq / (2.0 * barQ)));
                    cx.sectionType = types[sec]; cx.sectionStartQ = sec * 2.0 * barQ; cx.sectionEndQ = (sec + 1) * 2.0 * barQ;
                    cx.nextIsDrop = sec == 1; cx.buildProgress = sec == 1 ? (float) ((cx.ppq - cx.sectionStartQ) / (2.0 * barQ)) : 0.0f;
                    cx.leadActivity = 0.8f; cx.vocalActivity = 0.6f; cx.dropHit = sec == 2 && cx.ppq - cx.sectionStartQ < 1.0 ? 1.0f : 0.0f; cx.dropCount = 1;
                    cx.leadHz = 880.0f; cx.sibilance = 0.3f; cx.phraseEnd = 0.0f;
                    qn::LabelDsp4::Extra x; x.pitchHz = d3.pitchHz(); x.kickBodyHz = 55.0f;
                    float* chp[2] = { b.getWritePointer (0), b.getWritePointer (1) };
                    d3.process (chp, 2, n, cx);
                    d4.process (chp, 2, n, cx, x);
                    for (int i = 0; i < n; ++i)
                        for (int c = 0; c < 2; ++c)
                        {
                            const auto v = b.getSample (c, i);
                            if (! std::isfinite (v)) finite = false;
                            sumOut.addSample (c, pos + i, v);
                            if (c == 0) pout += (double) v * v;
                        }
                }
                const auto change = (float) (10.0 * std::log10 ((pout + 1.0e-12) / (pin + 1.0e-12)));
                if (std::abs (change) > std::abs (worstLevel)) { worstLevel = change; worstRole = part.role; }
            }
            const auto peakIn = juce::jmax (sumIn.getMagnitude (0, 0, total), sumIn.getMagnitude (1, 0, total));
            const auto peakOut = juce::jmax (sumOut.getMagnitude (0, 0, total), sumOut.getMagnitude (1, 0, total));
            double st = 0.0, mono = 0.0;
            for (int i = 0; i < total; ++i) { const auto l = sumOut.getSample (0, i), r = sumOut.getSample (1, i); st += 0.5 * ((double) l * l + (double) r * r); mono += 0.25 * (double) (l + r) * (l + r); }
            const auto monoDb = (float) (10.0 * std::log10 ((mono + 1.0e-12) / (st + 1.0e-12)));
            const auto label = juce::String ((int) (rate / 1000.0)) + " kHz: ";
            check (finite, label + "every fix at once stays finite");
            check (std::abs (worstLevel) < 9.0f, label + "no part moved more than 9 dB in all (the most: role " + juce::String (worstRole) + ", " + juce::String (worstLevel, 1) + " dB)");
            check (juce::Decibels::gainToDecibels (peakOut / peakIn) < 3.0f, label + "the mix's peak stays within 3 dB of the dry mix's (" + juce::String (juce::Decibels::gainToDecibels (peakOut / peakIn), 1) + " dB)");
            check (monoDb > -3.0f, label + "it stays mono-safe (the mono sum " + juce::String (monoDb, 1) + " dB against stereo)");
        }
        AP::reset(); suite::LabelReadyBus::reset(); suite::FixBus::reset(); suite::MixFixBus::reset();
    }

    section ("Track roles: 36, in nine families");
    {
        namespace R = suite::roles;
        std::array<int, R::numFamilies> perFamily {};
        for (int r = 0; r < R::numRoles; ++r) ++perFamily[(size_t) R::info (r).family];
        check (R::numRoles == 36 && perFamily == std::array<int, R::numFamilies> { 3, 4, 4, 4, 7, 4, 4, 4, 2 },
               "kick 3, bass 4, drums 4, chords 4, keys & orchestral 7, leads 4, vocals 4, FX 4, buses 2");
        auto named = [] (const char* n) { return R::fromTrackName (n).role; };
        check (named ("Sub Bass") == R::subBass && named ("Acid 303") == R::acidBass && named ("Reese") == R::acidBass && named ("Rolling Bass") == R::midBass
               && named ("Kick Top") == R::kickTop && named ("Kick") == R::kick && named ("Vox Chop") == R::chops && named ("Lead Vox") == R::leadVocal
               && named ("BVs") == R::backing && named ("Supersaw Chords") == R::supersaw && named ("Pluck 2") == R::plucks && named ("Strings") == R::strings
               && named ("Choir") == R::choir && named ("Glock") == R::bells && named ("Duduk") == R::guitar && named ("Arp") == R::arps
               && named ("Riser FX") == R::risers && named ("Impact") == R::impacts && named ("Crash") == R::cymbals && named ("Drum Bus") == R::drumBus,
               "the track's name picks the role");
        check (R::fromTrackName ("Bass").onlyFamily && R::fromTrackName ("Chords").onlyFamily && ! R::fromTrackName ("Sub Bass").onlyFamily,
               "a name that only names the family leaves the role to the sound");
        check (suite::roleFromTrackName ("Acid") == suite::roleBass && suite::roleFromTrackName ("Choir") == suite::rolePads && suite::roleFromTrackName ("Brass") == suite::rolePads,
               "the suite's broad roles know the new names too");
        check ((int) R::encode (2, R::midBass) == 2 && R::decode (R::encode (2, R::midBass)) == R::midBass && R::decode (2.0f) == -1,
               "the registry carries the detailed role in the broad role's fraction (old readers see the broad role)");
        check (R::rank (R::leadVocal) > R::rank (R::mainLead) && R::rank (R::chops) == R::rank (R::mainLead) && R::rank (R::mainLead) > R::rank (R::subBass)
               && R::rank (R::subBass) > R::rank (R::supersaw) && R::rank (R::supersaw) > R::rank (R::pads) && R::rank (R::pads) > R::rank (R::atmos),
               "who gives way: lead vocal > main lead and chops > kick and sub > supersaw and arps > pads > FX and atmos");

        // guessing from the sound, inside the family
        R::Sound sub, top;
        sub.bandsDb = { 0, 0, -8, -20, -30, -35, -40, -45, -50, -55 };
        top.bandsDb = { -30, -20, -6, -2, 0, -2, -4, -8, -14, -20 };
        check (R::guessFromSound (R::famBass, sub) == R::subBass && R::guessFromSound (R::famBass, top) == R::bassTop, "a bass is told sub from top layer by its sound");
        R::Sound pluck; pluck.bandsDb = { -30, -20, -10, -4, 0, -2, -4, -8, -12, -18 }; pluck.punchDb = 12.0f;
        check (R::guessFromSound (R::famChords, pluck) == R::plucks, "a chord part with sharp attacks is plucks & stabs");

        // Quintessence: the detailed role refines the broad role's targets
        auto q = make (2);
        if (auto* p = q->apvts.getParameter (qn::subRoleId)) p->setValueNotifyingHost (p->convertTo0to1 ((float) (R::bassTop + 1)));
        q->updateSubRole();
        const auto topTarget = q->target (2);
        if (auto* p = q->apvts.getParameter (qn::subRoleId)) p->setValueNotifyingHost (p->convertTo0to1 ((float) (R::subBass + 1)));
        q->updateSubRole();
        const auto subTarget = q->target (2);
        check (q->subRole() == R::subBass && topTarget.lowCutHz > subTarget.lowCutHz + 100.0f && topTarget.sideDb > subTarget.sideDb
               && topTarget.shape[0] < subTarget.shape[0] - 10.0f,
               "a bass top layer is steered higher, wider and without the sub; a sub bass keeps it");
        check (q->prima().getSubRole() == R::subBass
               && q->prima().apvts.getRawParameterValue (pm::levelId ((int) suite::SectionType::Breakdown))->load() <= pm::offDb + 0.5f,
               "the section mixer inside follows the role (a sub bass sits out the breakdown)");
        juce::MemoryBlock saved;
        q->getStateInformation (saved);
        auto again = make (0);
        again->setStateInformation (saved.getData(), (int) saved.getSize());
        again->updateSubRole();
        check (again->subRole() == R::subBass, "the role comes back with the project");
        check (juce::String (R::fullName (R::acidBass)) == "Bass > Acid / 303 & reese", "roles show as family > role");
    }

    return finish();
}
