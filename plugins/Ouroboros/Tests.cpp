// Offline tests for Ouroboros and the SectionBus. Usage: OuroborosTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestUtils.h"
#include "MorphBus.h"

using namespace suite::test;
using suite::SectionType;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Arrangement editing");
    {
        auto m = arrangement::fromTemplate (arrangement::templates().front());
        check (arrangement::toString (m) == "1:Intro 33:Groove 65:Breakdown 97:Build 113:Drop 177:Outro /208", "classic template loads");
        check (arrangement::sectionAt (m, 100) == 3 && arrangement::sectionAt (m, 209) == -1, "finds the section for a bar");

        arrangement::insertAt (m, 145, SectionType::Breakdown);
        check (arrangement::toString (m).contains ("113:Drop 145:Breakdown 177:Outro"), "inserting splits the drop");
        arrangement::insertAt (m, 145, SectionType::Build);
        check (arrangement::toString (m).contains ("145:Build"), "inserting on an existing start retypes it");

        arrangement::moveStart (m, 4, 90);
        check (m.sections[4].startBar == 98, "moving a boundary keeps at least 1 bar in the previous section");
        arrangement::moveStart (m, 4, 200);
        check (m.sections[4].startBar == 144, "... and at least 1 bar in the section itself");

        arrangement::remove (m, 0);
        check (m.sections[0].startBar == 1 && m.sections[0].type == SectionType::Groove, "removing the first section: the next one starts at bar 1");

        arrangement::setTotalBars (m, 100);
        check (m.sections[(size_t) m.count - 1].startBar <= 100, "shortening the song drops sections past the end");

        check (arrangement::snapBar (36.4, 8) == 33 && arrangement::snapBar (37.5, 8) == 41 && arrangement::snapBar (36.4, 1) == 36,
               "snapping rounds to the 8-bar grid from bar 1");
    }

    section ("Arrange: generated maps");
    {
        auto valid = [] (const suite::SectionMap& m)
        {
            if (m.count < 1 || m.sections[0].startBar != 1) return false;
            for (int i = 0; i < m.count; ++i)
            {
                const auto end = i + 1 < m.count ? m.sections[(size_t) i + 1].startBar : m.totalBars + 1;
                if (end - m.sections[(size_t) i].startBar < 8 || (m.sections[(size_t) i].startBar - 1) % 8 != 0) return false;
            }
            return true;
        };
        auto countOf = [] (const suite::SectionMap& m, SectionType t) { int n = 0; for (int i = 0; i < m.count; ++i) n += m.sections[(size_t) i].type == t; return n; };
        auto lengthOf = [] (const suite::SectionMap& m, int i) { return (i + 1 < m.count ? m.sections[(size_t) i + 1].startBar : m.totalBars + 1) - m.sections[(size_t) i].startBar; };

        arrangement::GenSettings g;   // 208 bars, 2 drops, DJ intro, long breakdown
        auto m = arrangement::generate (g);
        std::cout << "    " << arrangement::toString (m) << std::endl;
        check (m.totalBars == 208 && valid (m), "208 bars, every section a whole number of 8-bar phrases");
        check (countOf (m, SectionType::Drop) == 2 && countOf (m, SectionType::Breakdown) == 2 && countOf (m, SectionType::Build) == 2,
               "two drops, each with its own breakdown and build");
        check (m.sections[0].type == SectionType::Intro && m.sections[(size_t) m.count - 1].type == SectionType::Outro, "starts with an intro, ends with an outro");
        bool dropsFull = true, buildBeforeDrop = true;
        for (int i = 0; i < m.count; ++i)
            if (m.sections[(size_t) i].type == SectionType::Drop)
            {
                dropsFull = dropsFull && lengthOf (m, i) >= 32;
                buildBeforeDrop = buildBeforeDrop && i > 0 && m.sections[(size_t) i - 1].type == SectionType::Build;
            }
        check (dropsFull, "drops keep their full 32 bars");
        check (buildBeforeDrop, "every drop comes straight after a build");

        g.drops = 3; g.totalBars = 288;
        m = arrangement::generate (g);
        std::cout << "    " << arrangement::toString (m) << std::endl;
        check (m.totalBars == 288 && valid (m) && countOf (m, SectionType::Drop) == 3, "288 bars with three drops");

        g.drops = 1; g.totalBars = 96; g.djIntro = false; g.longBreakdown = false;
        m = arrangement::generate (g);
        std::cout << "    " << arrangement::toString (m) << std::endl;
        check (m.totalBars == 96 && valid (m) && countOf (m, SectionType::Drop) == 1, "a 96-bar radio edit with one drop");

        g.drops = 3; g.totalBars = 64; g.djIntro = true; g.longBreakdown = true;
        m = arrangement::generate (g);
        std::cout << "    " << arrangement::toString (m) << std::endl;
        check (m.totalBars == 64 && valid (m) && countOf (m, SectionType::Drop) >= 1 && countOf (m, SectionType::Drop) < 3,
               "too short for three drops: fewer drops, still fits exactly");

        g.drops = 2; g.totalBars = 203;
        m = arrangement::generate (g);
        check (m.totalBars == 200 && valid (m), "lengths round to whole phrases (203 -> 200)");

        g.drops = 1; g.totalBars = 400;
        m = arrangement::generate (g);
        std::cout << "    " << arrangement::toString (m) << std::endl;
        check (m.totalBars == 400 && valid (m), "a long song still fits exactly");
    }

    section ("SectionBus: publish, read, locate");
    {
        auto m = arrangement::fromTemplate (arrangement::templates()[3]); // 16-bar loop test: 1 Groove, 5 Breakdown, 9 Build, 13 Drop
        suite::SectionBus::publish (m);
        suite::SectionMap read;
        check (suite::SectionBus::read (read) && read == m, "map round-trips through shared memory");

        suite::SectionFollower f;
        check (f.refresh(), "follower sees the map");
        const auto barQ = 4.0;
        const auto p = f.locate (8.0 * barQ + 2.0, barQ);       // bar 9, beat 3
        check (p.valid && p.type == SectionType::Build && std::abs (p.startQ - 32.0) < 1e-9 && std::abs (p.endQ - 48.0) < 1e-9,
               "bar 9 is the Build (quarters 32-48)");
        check (std::abs (p.progress (40.0) - 0.5) < 1e-9 && p.hasNext && p.next == SectionType::Drop, "progress and next section");
        check (! f.locate (-1.0, barQ).valid && ! f.locate (16.0 * barQ + 1.0, barQ).valid, "before the start / after the end is 'no section'");

        // Occurrences: in the classic template the drop at bar 113 is the first; add a second one.
        auto two = arrangement::fromTemplate (arrangement::templates()[1]);   // two breakdowns, two drops
        suite::SectionBus::publish (two);
        suite::SectionFollower f2;
        f2.refresh();
        check (! f2.locate (4.0 * 95.0, 4.0).isLaterDrop() && f2.locate (4.0 * 175.0, 4.0).isLaterDrop(),
               "knows the first drop from the second (occurrence " + juce::String (f2.locate (4.0 * 175.0, 4.0).occurrence) + ")");
        suite::SectionBus::publish (m);
        f.refresh();

        // A follower in 3/4 uses its own bar length.
        check (f.locate (8.0 * 3.0, 3.0).type == SectionType::Build, "bar maths follows the host time signature");

        suite::SectionBus::withdraw();
        suite::SectionMap after;
        check (! suite::SectionBus::read (after), "withdraw empties the bus");
    }

    section ("Suite snapshots");
    {
        suite::SnapshotBus::reset();
        auto mapOf = [] (int drops) { arrangement::GenSettings g; g.drops = drops; return arrangement::generate (g); };
        auto a = std::make_unique<OuroborosProcessor>();
        auto b = std::make_unique<OuroborosProcessor>();   // stands in for a second suite plugin
        a->setMap (mapOf (1));
        b->setMap (mapOf (3));

        const auto save = suite::SnapshotBus::send (suite::SnapshotAction::Capture, 1);
        a->pollSnapshots(); b->pollSnapshots();
        check (suite::SnapshotBus::acknowledgements (save) == 2 && a->hasSnapshot (1) && ! a->hasSnapshot (0), "Save B: both plugins store slot B");

        a->setMap (mapOf (2));
        b->setMap (mapOf (2));
        const auto recall = suite::SnapshotBus::send (suite::SnapshotAction::Recall, 1);
        a->pollSnapshots(); b->pollSnapshots();
        check (a->getMap() == mapOf (1) && b->getMap() == mapOf (3) && suite::SnapshotBus::acknowledgements (recall) == 2,
               "Recall B: each plugin goes back to its own saved settings");

        const auto empty = suite::SnapshotBus::send (suite::SnapshotAction::Recall, 2);
        a->pollSnapshots(); b->pollSnapshots();
        check (suite::SnapshotBus::acknowledgements (empty) == 0 && a->getMap() == mapOf (1), "recalling an empty slot changes nothing");

        // Snapshots travel with the project.
        juce::MemoryBlock state;
        a->getStateInformation (state);
        auto c = std::make_unique<OuroborosProcessor>();
        c->setStateInformation (state.getData(), (int) state.getSize());
        check (c->hasSnapshot (1) && c->getMap() == mapOf (1), "slots are saved with the project");
        c->setMap (mapOf (2));
        check (c->recallSnapshot (1) && c->getMap() == mapOf (1), "... and recall after loading");

        // A project saved before snapshots existed still loads.
        juce::MemoryBlock legacy;
        a->getPluginState (legacy);
        auto d = std::make_unique<OuroborosProcessor>();
        d->setStateInformation (legacy.getData(), (int) legacy.getSize());
        check (d->getMap() == mapOf (1) && ! d->hasSnapshot (1), "old projects load as before");

        // Commands sent before a plugin loads don't apply to it.
        auto e = std::make_unique<OuroborosProcessor>();
        e->pollSnapshots();
        check (! e->hasSnapshot (1), "a plugin loaded later doesn't replay old commands");

        suite::SnapshotBus::send (suite::SnapshotAction::Clear, 1);
        a->pollSnapshots();
        check (! a->hasSnapshot (1), "right-click clears a slot");
        suite::SectionBus::withdraw();
    }

    section ("Final-drop key change");
    {
        auto p = std::make_unique<OuroborosProcessor>();
        p->setMap (arrangement::fromTemplate (arrangement::templates()[1]));   // two breakdowns, two drops
        set (*p, OuroborosProcessor::liftId, 2.0f);
        p->setMap (p->getMap());   // publishes
        suite::SectionMap shared;
        check (suite::SectionBus::read (shared) && shared.finalLift == 2, "the key change is shared with the suite");
        const auto last = shared.lastDropIndex();
        check (last >= 0 && shared.keyShiftOf (last) == 2 && shared.keyShiftOf (last - 1) == 0 && shared.keyShiftOf (shared.count - 1) == 2,
               "it starts at the last drop and carries on to the end");
        set (*p, OuroborosProcessor::liftId, 0.0f);
        p->setMap (p->getMap());
        check (suite::SectionBus::read (shared) && shared.finalLift == 0, "and can be turned off");
        suite::SectionBus::withdraw();
    }

    section ("Final-drop snapshot morph amount");
    {
        suite::SectionMap map;   // Drop 1-2, Breakdown 3-4, Build 5-6, Drop 7-8, Outro 9-10
        map.totalBars = 10; map.count = 5;
        map.sections[0] = { 1, suite::SectionType::Drop };
        map.sections[1] = { 3, suite::SectionType::Breakdown };
        map.sections[2] = { 5, suite::SectionType::Build };
        map.sections[3] = { 7, suite::SectionType::Drop };
        map.sections[4] = { 9, suite::SectionType::Outro };
        suite::SectionBus::publish (map);
        suite::SectionFollower f;
        f.refresh();
        auto at = [&] (double bar) { const auto ppq = (bar - 1.0) * 4.0; return OuroborosProcessor::finalMorphAmount (f.locate (ppq, 4.0), f.getMap(), ppq); };
        check (at (1.5) < 1.0e-6f && at (3.5) < 1.0e-6f, "nothing in the first drop and the breakdown");
        check (std::abs (at (6.0) - 0.5f) < 0.01f, "half-way through the build into the last drop: 0.5");
        check (at (7.0) > 0.999999f && at (9.5) > 0.999999f, "fully in from the last drop to the end");

        auto p = std::make_unique<OuroborosProcessor>();
        set (*p, OuroborosProcessor::finalSnapId, 2.0f);   // B
        p->setMap (map);
        FakePlayHead head;
        head.ppq = 5.0 * 4.0;   // bar 6
        p->setPlayHead (&head);
        render (*p, 0.02, {});
        p->setPlayHead (nullptr);
        const auto m = suite::MorphBus::read();
        check (m.valid && m.slot == 1 && std::abs (m.amount - 0.5f) < 0.02f, "Ouroboros shares 'snapshot B, 50%'");
        suite::SectionBus::withdraw();
        suite::MorphBus::withdraw();
    }

    section ("Suite overview");
    {
        using namespace suite;
        Registry::reset();
        auto o = std::make_unique<OuroborosProcessor>();
        o->setMap (arrangement::fromTemplate (arrangement::templates().front()));
        o->announce();
        Registry::announce (11, "Correspondence", "Chords", 0, {});
        Registry::announce (12, "Kybalion", "Pads", Status::sidechainWanted, { 1.0f, 0, 0, 0 });
        Registry::announce (13, "Prima Materia", "Insert 7", Status::followsOuroboros, { -1.0f, 0, 0, 0 });
        Registry::announce (14, "Lapis", "Lead", 0, {});
        Registry::announce (15, "Kybalion", "Bass", 0, { 2.0f, 0, 0, 0 });   // ducks to the suite kick
        Registry::announce (16, "Solve et Coagula", "Music bus", Status::sidechainWanted | Status::sidechainConnected, {});

        const auto entries = Registry::list();
        check (entries.size() == 7, "lists every plugin that announced itself (" + juce::String ((int) entries.size()) + ")");
        bool namesKept = false;
        for (auto& e : entries) if (e.plugin == "Kybalion" && e.track == "Pads") namesKept = true;
        check (namesKept, "with its track name");

        const auto advice = suitecheck::check (entries);
        juce::StringArray texts;
        for (auto& a : advice) texts.add (a.text);
        const auto all = texts.joinIntoString ("\n");
        std::cout << "    " << all.replace ("\n", "\n    ") << std::endl;
        check (all.contains ("Kybalion on 'Pads' is set to duck from its sidechain"), "flags a sidechain that isn't routed");
        check (all.contains ("Prima Materia on 'Insert 7' has no role"), "flags a Prima Materia without a role");
        check (all.contains ("Lapis is on 'Lead'"), "flags Lapis away from the master");
        check (all.contains ("no kick is shared"), "flags Suite kick with nothing shared");
        check (! all.contains ("Solve et Coagula") && ! all.contains ("No key source") && ! all.contains ("no song map"),
               "doesn't complain about what's fine");

        Registry::reset();
        o->announce();
        Registry::announce (11, "Correspondence", "Chords", 0, {});
        const auto fine = suitecheck::check (Registry::list());
        check (fine.size() == 1 && ! fine[0].problem, "a tidy session: 'Everything is set up'");

        Registry::remove (11);
        check (Registry::list().size() == 1, "a plugin that's removed disappears from the list");

        if (outDir != juce::File())
        {
            Registry::announce (11, "Correspondence", "Chords", 0, {});
            Registry::announce (12, "Kybalion", "Pads", Status::sidechainWanted, { 1.0f, 0, 0, 0 });
            Registry::announce (13, "Prima Materia", "Insert 7", Status::followsOuroboros, { -1.0f, 0, 0, 0 });
            Registry::announce (17, "Prima Materia", "Kick", Status::roleSet, { 1.0f, -6.0f, 0, 0 });
            Registry::announce (18, "Azoth", "FX", 0, {});
            Registry::announce (19, "Lapis", "Master", 0, {});
            Registry::announce (20, "Mentalism", "Master", 0, {});
            SuitePanel panel;
            snapshot (panel, outDir.getChildFile ("ouroboros_suite.png"));
        }
        Registry::reset();
    }

    section ("Plugin: broadcast, marking, state");
    {
        auto p = std::make_unique<OuroborosProcessor>();
        prepare (*p);
        suite::SectionMap read;
        check (suite::SectionBus::read (read) && read == p->getMap(), "publishes its map on load");

        FakePlayHead head;
        head.bpm = 138.0;
        head.ppq = 4.0 * 52.3;  // bar 53.3
        p->setPlayHead (&head);
        render (*p, 0.05, {});
        set (*p, OuroborosProcessor::snapId, 1.0f); // 4 bars
        p->markAtPlayhead (SectionType::Breakdown);
        p->setPlayHead (nullptr);
        check (arrangement::toString (p->getMap()).contains ("53:Breakdown"), "Mark at playhead snaps to the 4-bar grid (bar 53)");
        check (suite::SectionBus::read (read) && read == p->getMap(), "edits are published straight away");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = std::make_unique<OuroborosProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getMap() == p->getMap(), "arrangement survives save/restore");

        set (*q, OuroborosProcessor::broadcastId, 0.0f);
        q->publish();
        check (! suite::SectionBus::read (read), "switching Broadcast off withdraws the map");

        juce::AudioBuffer<float> audio;
        render (*q, 0.1, pluck(), &audio);
        juce::AudioBuffer<float> ref (2, audio.getNumSamples());
        ref.clear(); pluck() (ref, 0);
        float diff = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i) diff = std::max (diff, std::abs (audio.getSample (0, i) - ref.getSample (0, i)));
        check (diff < 1.0e-9f, "audio passes through untouched");
    }

    section ("Listen: proposes the structure of a song it hears");
    {
        // A synthetic 104-bar trance arrangement at 140 BPM.
        //  1-16 Intro (kick, pad, quiet hats)   17-32 Groove (kick, pad, hats)
        // 33-48 Breakdown (pad, soft lead)      49-56 Build (pad, rising noise riser)
        // 57-88 Drop (kick, hats, bright lead)  89-104 Outro (kick only)
        auto p = std::make_unique<OuroborosProcessor>();
        prepare (*p);
        FakePlayHead head;
        head.bpm = 140.0;
        p->setPlayHead (&head);
        p->listening.store (true);
        p->clearListening();

        const auto barSec = 4.0 * 60.0 / head.bpm, beatSec = barSec / 4.0;
        juce::Random rng (21);
        auto song = [&] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto bar = (int) (t / barSec) + 1;
                const auto inBeat = std::fmod (t, beatSec);
                const bool intro = bar <= 16, groove = bar >= 17 && bar <= 32, breakdown = bar >= 33 && bar <= 48;
                const bool build = bar >= 49 && bar <= 56, drop = bar >= 57 && bar <= 88, outro = bar >= 89 && bar <= 104;
                const auto twoPi = juce::MathConstants<double>::twoPi;

                double v = 0.0;
                if (intro || groove || drop || outro)   // kick: pitch-swept thump
                    v += 0.6 * std::sin (twoPi * (50.0 * inBeat + 70.0 / 30.0 * (1.0 - std::exp (-inBeat * 30.0)))) * std::exp (-inBeat * 10.0);
                if (! outro && bar <= 104)              // pad
                    for (double f : { 220.0, 277.2, 329.6 }) v += 0.06 * std::sin (twoPi * f * t);
                const auto offbeat = std::fmod (t + beatSec * 0.5, beatSec) < 0.03;
                if (offbeat && (groove || drop)) v += 0.08 * (rng.nextFloat() * 2.0f - 1.0f);
                if (offbeat && intro)            v += 0.03 * (rng.nextFloat() * 2.0f - 1.0f);
                if (drop)      v += 0.08 * (2.0 * std::fmod (t * 440.0, 1.0) - 1.0);   // bright saw lead
                if (breakdown) v += 0.04 * std::sin (twoPi * 440.0 * t);
                if (build)     v += 0.15 * ((t - 48.0 * barSec) / (8.0 * barSec)) * (rng.nextFloat() * 2.0f - 1.0f);
                if (bar > 104) v = 0.0;
                b.setSample (0, i, (float) v);
                b.setSample (1, i, (float) v);
            }
        };
        render (*p, 104.0 * barSec + 0.5, song, nullptr, &head);
        p->setPlayHead (nullptr);

        check (p->barsHeard() >= 104, "heard every bar (" + juce::String (p->barsHeard()) + ")");
        const auto proposal = p->proposeFromListening();
        const auto text = arrangement::toString (proposal);
        std::cout << "    proposed: " << text << std::endl;
        check (text == "1:Intro 17:Groove 33:Breakdown 49:Build 57:Drop 89:Outro /104", "matches the real structure");

        p->listening.store (false);
        p->clearListening();
        render (*p, 0.05, {});
        check (p->barsHeard() == 0 && p->proposeFromListening().count == 0, "clearing forgets what was heard");
    }

    if (outDir != juce::File())
    {
        auto p = std::make_unique<OuroborosProcessor>();
        prepare (*p);
        FakePlayHead head;
        head.ppq = 4.0 * 104.5;  // inside the build
        p->setPlayHead (&head);
        render (*p, 0.05, {});
        p->setPlayHead (nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("ouroboros.png"));
        editor.reset();

        ArrangePanel panel ({}, 138.0, {});
        snapshot (panel, outDir.getChildFile ("ouroboros_arrange.png"));
    }

    return finish();
}
