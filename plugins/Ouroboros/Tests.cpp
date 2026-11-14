// Offline tests for Ouroboros and the SectionBus. Usage: OuroborosTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AutopilotBus.h"
#include "PlanBus.h"
#include "PluginEditor.h"
#include "TestUtils.h"
#include "SuiteCheck.h"
#include "Alias.h"
#include "TempoBus.h"
#include "KeyBus.h"
#include "MorphBus.h"

using namespace suite::test;
using suite::SectionType;
using T = suite::SectionType;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    // Never the real style log (Make versions reads your version lengths from it).
    const auto testStyleLog = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroborosTestsStyleLog.txt");
    testStyleLog.deleteFile();
    suite::StyleLog::setFileForTests (testStyleLog);
    lab::Learning::fileForTests() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroLearningTest.txt");
    lab::Learning::fileForTests().deleteFile();
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

    section ("New song (session preset)");
    {
        suite::SessionBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        const auto seq = p->startSession (0, 2, 0);   // Uplifting anthem in D minor
        const auto m = p->getMap();
        int drops = 0;
        for (int i = 0; i < m.count; ++i) drops += m.sections[(size_t) i].type == suite::SectionType::Drop;
        check (m.totalBars == suite::sessionBars (suite::sessionStyles()[0]) && m.totalBars == 240 && drops == 2,
               "Uplifting anthem: a 240-bar (7:00 at 138) map with two drops (" + arrangement::toString (m) + ")");
        check ((int) p->apvts.getRawParameterValue (OuroborosProcessor::liftId)->load() == 1, "with the key up a semitone for the last drop");
        suite::SessionRequest r;
        check (suite::SessionBus::read (seq, r) && r.style == 0 && r.root == 2 && r.scale == 0, "and asks the suite to set up D minor");

        const auto darkSeq = p->startSession (0, 2, 0, 4);   // Dark
        const auto dark = p->getMap();
        bool deep = false;
        for (int i = 0; i < dark.count; ++i)
            if (dark.sections[(size_t) i].type == suite::SectionType::Breakdown) deep = dark.sections[(size_t) i].energy == 20;
        check (deep && suite::SessionBus::read (darkSeq, r) && r.mood == 4, "a Dark mood draws deeper breakdowns and tells the suite");
        p->startSession (0, 2, 0, 1);   // Euphoric
        check ((int) p->apvts.getRawParameterValue (OuroborosProcessor::intensityId)->load() == 120, "a Euphoric mood turns the intensity up");
    }

    section ("Energy curve");
    {
        auto p = std::make_unique<OuroborosProcessor>();
        auto m = arrangement::fromTemplate (arrangement::templates().front());
        const auto drop = m.lastDropIndex();
        check (std::abs (m.energyOf (drop) - 1.0f) < 1.0e-6f && std::abs (m.energyOf (0) - suite::defaultEnergy (suite::SectionType::Intro)) < 1.0e-6f,
               "untouched sections have their type's usual energy");
        m.sections[(size_t) drop].energy = 70;
        p->setMap (m);
        suite::SectionMap shared;
        check (suite::SectionBus::read (shared) && shared.sections[(size_t) drop].energy == 70 && shared.sections[0].energy == -1,
               "a drawn energy is shared with the suite (and untouched ones stay 'usual')");
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = std::make_unique<OuroborosProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getMap().sections[(size_t) drop].energy == 70, "and saved with the project");
        suite::SectionFollower f;
        f.refresh();
        const auto pos = f.locate ((shared.sections[(size_t) drop].startBar - 1) * 4.0 + 1.0, 4.0);
        check (pos.valid && std::abs (pos.energy - 0.7f) < 1.0e-6f && std::abs (pos.energyRatio - 0.7f) < 1.0e-6f, "followers see energy 0.7, 70% of a drop's usual");
        suite::SectionBus::withdraw();
    }

    section ("Intensity, live mode, templates, notes");
    {
        suite::SectionBus::withdraw();
        auto p = std::make_unique<OuroborosProcessor>();
        set (*p, OuroborosProcessor::intensityId, 150.0f);
        p->setMap (p->getMap());
        suite::SectionMap shared;
        check (suite::SectionBus::read (shared) && shared.intensity == 150 && std::abs (shared.intensityScale() - 1.5f) < 1.0e-6f,
               "Intensity 150% is shared with the suite");

        // Live: a G (drop) at bar 3.5 starts the drop on bar 5; a C before that stays the intro until then.
        set (*p, OuroborosProcessor::liveId, 1.0f);
        prepare (*p);
        FakePlayHead head;
        head.ppq = 2.0 * 4.0 + 2.0;   // bar 3, beat 3
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio (2, 512);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);   // C: intro
        p->processBlock (audio, midi);
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);   // G: drop
        p->processBlock (audio, midi);
        p->setPlayHead (nullptr);
        p->setMap (p->getMap());
        check (suite::SectionBus::read (shared) && shared.liveType == 4 && shared.livePrevType == 0 && std::abs (shared.liveStartQ - 12.0) < 1.0e-6,
               "live: the drop is queued for the next bar line (bar 4)");
        suite::SectionFollower f;
        f.refresh();
        const auto before = f.locate (11.0, 4.0), after = f.locate (12.5, 4.0);
        check (before.valid && before.type == suite::SectionType::Intro && before.hasNext && before.next == suite::SectionType::Drop
                   && after.valid && after.type == suite::SectionType::Drop && after.index != before.index,
               "followers: intro until the bar line, then the drop (a new section, so transitions fire)");
        check (OuroborosProcessor::liveTypeForNote (65) == 3 && OuroborosProcessor::liveTypeForNote (61) == -1, "F plays the build; black keys do nothing");
        check (std::abs (OuroborosProcessor::liveStartFor (15.5, 4.0) - 20.0) < 1.0e-9, "a note just before a bar line waits for the one after");
        set (*p, OuroborosProcessor::liveId, 0.0f);
        render (*p, 0.02, {});
        p->setMap (p->getMap());
        check (suite::SectionBus::read (shared) && ! shared.isLive(), "live off: back to the song map");

        // Your own templates, with their energy curve.
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AntiochTemplatesTest");
        folder.deleteRecursively();
        auto m = arrangement::fromTemplate (arrangement::templates().front());
        m.sections[2].energy = 40;
        check (arrangement::saveUserTemplate (folder, "My anthem form", m), "saves a song map as a template");
        const auto loaded = arrangement::loadUserTemplates (folder);
        check (loaded.size() == 1 && loaded[0].first == "My anthem form" && loaded[0].second == m, "and loads it back, energy curve included");
        folder.deleteRecursively();

        // Session notes.
        suite::FixBus::send (suite::Fix::RoughMix);
        p->logActivity();
        check (p->getNotes().joinIntoString (" ").contains ("rough mix"), "notes what the suite did (a rough mix)");
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = std::make_unique<OuroborosProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getNotes().joinIntoString (" ").contains ("rough mix"), "and keeps the notes with the project");
        suite::SectionBus::withdraw();
    }

    section ("Suite collisions");
    {
        using namespace suite;
        SectionMap m = arrangement::fromTemplate (arrangement::templates()[1]);
        m.finalLift = 1; m.dropGap = 3; m.intensity = 20;
        m.sections[(size_t) m.lastDropIndex()].energy = 10;
        std::vector<SuiteEntry> entries (2);
        entries[0].plugin = "Prima Materia"; entries[0].track = "Vox"; entries[0].flags = Status::roleSet; entries[0].metrics = { 6.0f, 0, 0, 0 };
        entries[1].plugin = "Kybalion"; entries[1].track = "Pads"; entries[1].flags = Status::fillsOn;
        std::vector<suitecheck::Advice> out;
        suitecheck::collisions (entries, m, out);
        juce::StringArray texts;
        for (auto& a : out) texts.add (a.text);
        const auto all = texts.joinIntoString ("\n");
        check (all.contains ("vocal track can't follow"), "warns that the vocal won't follow the key change");
        check (all.contains ("their fill won't be heard"), "warns about a kick-only bar on top of fills");
        check (all.contains ("drawn no higher than the breakdown"), "warns about a drop drawn below its breakdown");
        check (all.contains ("Intensity is at 20%"), "warns that Intensity is nearly off");
    }

    section ("Intensity ride, arrangement DNA and section markers");
    {
        auto p = std::make_unique<OuroborosProcessor>();
        p->prepareToPlay (48000.0, 512);
        auto sendCC = [&] (int cc, int value)
        {
            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 0);
            p->processBlock (buffer, midi);
            p->applyRide();
            return p->apvts.getRawParameterValue (OuroborosProcessor::intensityId)->load();
        };
        check (std::abs (sendCC (1, 20) - 100.0f) < 0.01f, "ride off: the mod wheel doesn't touch Intensity");
        set (*p, OuroborosProcessor::rideId, 1.0f);   // mod wheel
        check (std::abs (sendCC (1, 0)) < 0.5f && std::abs (sendCC (1, 64) - 100.0f) < 0.5f && std::abs (sendCC (1, 127) - 200.0f) < 0.5f,
               "the mod wheel rides Intensity: 0 -> 0%, centre -> 100%, top -> 200%");
        check (std::abs (sendCC (11, 0) - 200.0f) < 0.5f, "other controllers are ignored");

        // Arrangement DNA: two songs share a structure, one doesn't.
        auto map = [] (std::vector<std::pair<int, suite::SectionType>> secs, int total, int dropEnergy = -1)
        {
            suite::SectionMap m;
            m.totalBars = total;
            for (const auto& [bar, type] : secs) m.sections[(size_t) m.count++] = { bar, type, type == T::Drop ? dropEnergy : -1 };
            return m;
        };
        const std::vector<suite::SectionMap> mine {
            map ({ { 1, T::Intro }, { 17, T::Breakdown }, { 49, T::Build }, { 57, T::Drop }, { 89, T::Outro } }, 104, 90),
            map ({ { 1, T::Intro }, { 17, T::Breakdown }, { 41, T::Build }, { 49, T::Drop }, { 81, T::Outro } }, 96, 90),
            map ({ { 1, T::Intro }, { 33, T::Drop } }, 64) };
        const auto dna = arrangement::learnDna (mine);
        check (dna.valid && dna.songs == 2 && dna.structure.size() == 5 && dna.structure[1] == T::Breakdown,
               "learns your usual structure (intro, breakdown, build, drop, outro: 2 of 3 songs)");
        const auto like = arrangement::generateLike (dna, 160);
        bool phrases = like.totalBars == 160 && like.count == 5;
        for (int i = 0; phrases && i < like.count; ++i) phrases = (like.endBar (i) - like.sections[(size_t) i].startBar) % 8 == 0;
        check (phrases && like.sections[3].type == T::Drop && like.sections[3].energy == 90,
               "lays it out at 160 bars in whole phrases, with your usual drop energy (" + arrangement::toString (like) + ")");
        check (arrangement::generateLike (dna).totalBars == 104, "or at your usual length");
        check (! arrangement::learnDna ({ mine[0] }).valid, "needs at least two songs");

        // Section markers.
        p->setMap (like);
        const auto midi = arrangement::markerMidi (like, 120.0);
        juce::StringArray markers;
        for (auto* e : *midi.getTrack (0))
            if (e->message.isTextMetaEvent() && e->message.getMetaEventType() == 6)
                markers.add (e->message.getTextFromTextMetaEvent() + "@" + juce::String ((int) (e->message.getTimeStamp() / 960.0 / 4.0) + 1));
        check (markers.size() == 5 && markers[0] == "Intro@1" && markers[1].startsWith ("Breakdown@"), "a MIDI marker at every section: " + markers.joinIntoString (" "));
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroborosMarkers");
        dir.deleteRecursively();
        check (p->exportMarkers (dir.getChildFile ("Song sections.mid")) && dir.getChildFile ("Song sections.txt").loadFileAsString().contains ("Bar    1  0:00  Intro"),
               "exported with a cue sheet (bars and times)");
        dir.deleteRecursively();
    }

    section ("Recipes");
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AntiochRecipesTest");
        folder.deleteRecursively();
        suite::Recipes::setFolderForTests (folder);
        suite::RecipeBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        juce::AudioProcessor::TrackProperties props;
        props.name = juce::String ("Master");
        p->updateTrackProperties (props);
        const auto original = arrangement::fromTemplate (arrangement::templates()[1]);
        p->setMap (original);
        const auto save = suite::RecipeBus::send (suite::RecipeAction::Save, "My anthem");
        p->pollRecipes();
        check (suite::RecipeBus::acknowledgements (save) == 1 && suite::Recipes::list().contains ("My anthem"), "saves a named recipe");

        auto q = std::make_unique<OuroborosProcessor>();   // another project, track named differently
        props.name = juce::String ("Insert 1");
        q->updateTrackProperties (props);
        q->setMap (arrangement::fromTemplate (arrangement::templates()[2]));
        const auto load = suite::RecipeBus::send (suite::RecipeAction::Load, "My anthem");
        q->pollRecipes();
        check (q->getMap() == original && suite::RecipeBus::acknowledgements (load) == 1,
               "loads it in another project (the only Ouroboros in the recipe, so the track name doesn't have to match)");
        const auto missing = suite::RecipeBus::send (suite::RecipeAction::Load, "No such recipe");
        q->pollRecipes();
        check (suite::RecipeBus::acknowledgements (missing) == 0 && q->getMap() == original, "an unknown recipe changes nothing");

        // Your signature: New song loads it into the suite first, then the style and key.
        const auto sig = suite::RecipeBus::send (suite::RecipeAction::Save, suite::Recipes::signature);
        p->pollRecipes();
        check (suite::RecipeBus::acknowledgements (sig) == 1, "Save as my signature stores a recipe called '" + suite::Recipes::signature + "'");
        const auto beforeSession = suite::RecipeBus::latest();
        const auto sessionSeq = q->startSession (0, 2, 0, 0, true);
        suite::RecipeAction action {};
        juce::String name;
        check (suite::RecipeBus::latest() == beforeSession + 1 && suite::RecipeBus::read (beforeSession + 1, action, name)
                   && action == suite::RecipeAction::Load && name == suite::Recipes::signature && sessionSeq != 0,
               "New song from my signature: the suite loads the signature, then gets the session");
        const auto sessionMap = q->getMap();
        q->pollRecipes();
        check (q->getMap() == sessionMap && sessionMap.totalBars == 240, "Ouroboros keeps the new song's map (it skips its own signature)");
        folder.deleteRecursively();
        suite::Recipes::setFolderForTests ({});
    }

    section ("Signature chains");
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AntiochChainTest");
        folder.deleteRecursively();
        suite::Recipes::setFolderForTests (folder);
        juce::AudioProcessor::TrackProperties props;
        auto p = std::make_unique<OuroborosProcessor>();
        props.name = juce::String ("Lead 1");
        p->updateTrackProperties (props);
        auto q = std::make_unique<OuroborosProcessor>();
        props.name = juce::String ("Lead 2");
        q->updateTrackProperties (props);
        const auto mine = arrangement::fromTemplate (arrangement::templates()[2]);
        p->setMap (mine);
        const auto save = suite::RecipeBus::send (suite::RecipeAction::Save, "Chain Lead@Lead 1");
        p->pollRecipes();
        q->pollRecipes();
        check (suite::RecipeBus::acknowledgements (save) == 1, "only the plugins on the chain's track save it");
        const auto load = suite::RecipeBus::send (suite::RecipeAction::Load, "Chain Lead@Lead 2");
        p->pollRecipes();
        q->pollRecipes();
        check (suite::RecipeBus::acknowledgements (load) == 1 && q->getMap() == mine, "and it loads onto another track, whatever its name");
        folder.deleteRecursively();
        suite::Recipes::setFolderForTests ({});
    }

    section ("Versions: radio edit and extended mix");
    {
        suite::SnapshotBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        const auto original = arrangement::fromTemplate (arrangement::templates()[1]);   // 232 bars, two drops
        p->setMap (original);
        p->makeVersions();
        check (p->hasSnapshot (0) && p->hasSnapshot (1) && p->hasSnapshot (2) && p->getMap() == original, "A, B and C made, back on A");
        p->recallSnapshot (1);
        const auto radio = p->getMap();
        p->recallSnapshot (2);
        const auto extended = p->getMap();
        std::cout << "    radio edit: " << arrangement::toString (radio) << "\n    extended: " << arrangement::toString (extended) << std::endl;
        check (radio.totalBars < 140 && radio.totalBars >= 100 && radio.sections[0].type == suite::SectionType::Intro,
               "B is a radio edit of about 3:30 (" + juce::String (radio.totalBars) + " bars)");
        check (extended.totalBars == original.totalBars + 64, "C is an extended mix, 64 bars longer");
        check (p->hasSnapshot (3), "D made too");
        p->recallSnapshot (3);
        const auto club = p->getMap();
        const auto last = club.count - 1;
        check (club.sections[0].type == T::Intro && club.endBar (0) - 1 == 32 && club.sections[(size_t) last].type == T::Outro
                   && club.totalBars - club.sections[(size_t) last].startBar + 1 == 32
                   && club.totalBars - 64 == original.totalBars - (original.endBar (0) - 1) - (original.totalBars - original.sections[(size_t) original.count - 1].startBar + 1),
               "D is a club edit: 32-bar intro and outro around the same body (" + arrangement::toString (club) + ")");
        p->recallSnapshot (0);
        check (p->getMap() == original, "A is the song as it was");
    }

    section ("Your version lengths, and artist aliases");
    {
        testStyleLog.deleteFile();
        suite::SnapshotBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        const auto original = arrangement::fromTemplate (arrangement::templates()[1]);
        p->setMap (original);
        p->makeVersions();
        check (p->snapshotMap (1).totalBars == [&] { p->recallSnapshot (1); return p->getMap().totalBars; }(), "a snapshot's map can be read back");
        p->recallSnapshot (0);
        check (p->logVersionLengths() && ! p->logVersionLengths(), "the render plan logs the versions' lengths, once");
        const auto logged = suite::StyleLog::read();
        check (logged.size() == 1 && logged[0].get ("extendedextra") == "64" && logged[0].get ("radioseconds").getIntValue() > 150,
               "the radio edit's length and the extended mix's extra bars (" + logged[0].get ("radioseconds") + " s, +" + logged[0].get ("extendedextra") + ")");
        check (OuroborosProcessor::usualVersionLengths (logged).radioSeconds == 0.0f, "one song isn't enough to change Make versions");

        testStyleLog.deleteFile();
        suite::StyleLog::add ("versions", { { "radioseconds", "240" }, { "extendedextra", "32" } });
        suite::StyleLog::add ("versions", { { "radioseconds", "246" }, { "extendedextra", "32" } });
        suite::SnapshotBus::reset();
        auto q = std::make_unique<OuroborosProcessor>();
        q->setMap (original);
        q->makeVersions();
        q->recallSnapshot (1);
        const auto radio = q->getMap();
        q->recallSnapshot (2);
        const auto extended = q->getMap();
        const auto radioSeconds = radio.totalBars * 4.0 * 60.0 / 138.0;
        check (std::abs (radioSeconds - 243.0) < 10.0 && extended.totalBars == original.totalBars + 32,
               "Make versions uses your usual lengths: a " + juce::String ((int) radioSeconds / 60) + ":" + juce::String ((int) radioSeconds % 60).paddedLeft ('0', 2)
               + " radio edit, an extended mix 32 bars longer");
        testStyleLog.deleteFile();

        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SuiteAliasTest");
        root.deleteRecursively();
        suite::Alias::rootOverride() = root;
        check (suite::Alias::current().isEmpty() && suite::Alias::file ("Recipes") == root.getChildFile ("Recipes"), "the main alias uses the suite folder");
        suite::Alias::setCurrent ("Solar Wind");
        check (suite::Alias::current() == "Solar Wind" && suite::Alias::file ("Style log.txt") == root.getChildFile ("Aliases/Solar Wind/Style log.txt")
                   && suite::Alias::list() == juce::StringArray { "Solar Wind" },
               "an alias keeps everything in its own folder");
        suite::Recipes::setFolderForTests ({});
        check (suite::Recipes::folder() == root.getChildFile ("Aliases/Solar Wind/Recipes"), "recipes (and every other saved thing) follow the alias");
        suite::Alias::setCurrent ("");
        check (suite::Recipes::folder() == root.getChildFile ("Recipes"), "and back to the main name");
        suite::Alias::rootOverride() = juce::File();
        root.deleteRecursively();
    }

    section ("Song draft in one click");
    {
        suite::FixBus::reset();
        suite::SessionBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        const auto before = suite::RecipeBus::latest();
        const auto seq = p->draftSong (0, 9, 0);
        p->continueDraft();
        check (seq != 0 && suite::FixBus::latest() == 0, "it sets up the new song first");
        p->continueDraft (true);
        suite::FixBus::Message m;
        suite::RecipeAction action {};
        juce::String name;
        check (suite::FixBus::read (suite::FixBus::latest(), m) && m.fix == suite::Fix::DraftSong
                   && suite::RecipeBus::read (before + 1, action, name) && action == suite::RecipeAction::Save && name.startsWith ("Draft "),
               "then asks Correspondence for the whole song's MIDI and saves the setup as a recipe (" + name + ")");
        p->continueDraft (true);
        check (suite::RecipeBus::latest() == before + 1, "once");
    }

    section ("Moments");
    {
        auto p = std::make_unique<OuroborosProcessor>();
        check (p->setMomentsText ("64 stop\n65 impact\nnonsense\n 33 Throw ") == 3 && p->momentsText() == "33 throw\n64 stop\n65 impact",
               "moments are typed as 'bar what', sorted, nonsense ignored");
        suite::SectionMap shared;
        check (suite::SectionBus::read (shared) && shared.momentCount == 3 && shared.hasMoment (65, suite::momentImpact) && ! shared.hasMoment (64, suite::momentImpact),
               "and shared with the suite in the song map");
        p->setMap (arrangement::fromTemplate (arrangement::templates()[1]));
        check (p->momentsText().contains ("65 impact"), "a new arrangement keeps them");
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = std::make_unique<OuroborosProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->momentsText() == p->momentsText(), "and they're saved with the project");
        q->setMomentsText ("");
        suite::SectionBus::withdraw();
    }

    section ("Learning pass");
    {
        suite::FixBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        suite::SectionMap m;
        m.totalBars = 4; m.count = 2;
        m.sections[0] = { 1, T::Groove };
        m.sections[1] = { 3, T::Drop };
        p->setMap (m);
        p->prepareToPlay (48000.0, 512);
        p->startLearningPass();
        suite::FixBus::Message msg;
        check (suite::FixBus::read (suite::FixBus::latest(), msg) && msg.fix == suite::Fix::LearnStart, "starting it tells every plugin to measure afresh");
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        render (*p, 4.0, {}, nullptr, &head);   // bars 1-2
        check (! p->continueLearningPass() && p->learning.load(), "half the song: still listening");
        render (*p, 4.0, {}, nullptr, &head);   // bars 3-4
        p->setPlayHead (nullptr);
        check (p->learnBarsHeard() == 4 && p->continueLearningPass() && ! p->learning.load(), "the whole song heard: the pass ends by itself");
        check (suite::FixBus::read (suite::FixBus::latest(), msg) && msg.fix == suite::Fix::FinishSong && p->learnSeqs.size() == 2,
               "and sends the kick fit, the phase check and Finish the song");
        check (p->undoLearningPass() == 2, "undo sends back the Kybalion moves");
    }

    section ("Tempo changes");
    {
        auto p = std::make_unique<OuroborosProcessor>();   // a new project clears the tempo map
        suite::SectionMap m;
        m.totalBars = 4; m.count = 2;
        m.sections[0] = { 1, T::Groove };
        m.sections[1] = { 3, T::Breakdown };
        p->setMap (m);
        p->prepareToPlay (48000.0, 512);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        // Bars 1-2 at 120 BPM, then the breakdown slows to 100 BPM.
        render (*p, 6.3, {}, nullptr, &head, [&head] (int64_t) { if (head.ppq >= 8.0) head.bpm = 100.0; });
        p->setPlayHead (nullptr);
        check (std::abs (suite::TempoBus::bpmOf (1, 0.0f) - 120.0f) < 0.01f && std::abs (suite::TempoBus::bpmOf (3, 0.0f) - 100.0f) < 0.01f
                   && suite::TempoBus::bpmOf (4, 0.0f) == 0.0f,
               "each bar's tempo is noted as it plays (bar 4 not reached yet)");
        const auto cue = arrangement::cueSheet (m, 100.0);
        std::cout << "    " << cue.replace ("\n", "\n    ") << std::endl;
        check (cue.contains ("3  0:04  Breakdown") && cue.contains ("End    5  0:08") && cue.contains ("Tempo changes: bar 1: 120.0 BPM, bar 3: 100.0 BPM"),
               "the cue sheet's times follow the tempo change (bar 3 at 0:04, not 0:04.8)");
        juce::MemoryOutputStream midi;
        arrangement::markerMidi (m, 120.0).writeTo (midi);
        juce::MidiFile file;
        juce::MemoryInputStream in (midi.getData(), midi.getDataSize(), false);
        file.readFrom (in);
        juce::MidiMessageSequence tempos;
        file.findAllTempoEvents (tempos);
        check (tempos.getNumEvents() == 2 && std::abs (tempos.getEventPointer (1)->message.getTempoSecondsPerQuarterNote() - 0.6) < 1.0e-6,
               "and the marker MIDI carries the tempo change");
        auto q = std::make_unique<OuroborosProcessor>();
        check (suite::TempoBus::bpmOf (1, 0.0f) == 0.0f, "a new project starts with no tempo map");
    }

    section ("DJ phrasing fix, autosave and style report");
    {
        suite::FixBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        suite::SectionMap m;
        m.totalBars = 100; m.count = 4;
        m.sections[0] = { 1, T::Intro };
        m.sections[1] = { 21, T::Breakdown };   // a 20-bar intro, off the grid
        m.sections[2] = { 45, T::Drop };
        m.sections[3] = { 77, T::Outro };       // a 24-bar outro
        p->setMap (m);
        const auto issues = suite::dj::phrasingIssues (m);
        check (issues.size() == 5 && issues[0].contains ("intro is 20") && issues[1].contains ("outro is 24"),
               "finds the 20-bar intro, the 24-bar outro and the three sections it pushes off the 8-bar grid (" + issues.joinIntoString ("; ") + ")");
        const auto seq = suite::FixBus::send (suite::Fix::DjPhrasing);
        p->fixes->poll();
        const auto fixed = p->getMap();
        check (suite::dj::phrasingIssues (fixed).isEmpty() && fixed.endBar (0) - 1 == 16 && fixed.count == 4 && suite::FixBus::acknowledgements (seq) == 1,
               "the fix: whole 16-bar phrases, every section on the grid (" + arrangement::toString (fixed) + ")");
        suite::FixBus::sendUndo (seq);
        p->fixes->poll();
        check (p->getMap() == m, "undo puts the map back");

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroborosAutosave");
        folder.deleteRecursively();
        suite::Recipes::setFolderForTests (folder);
        for (int i = 0; i < 14; ++i)
            folder.getChildFile (OuroborosProcessor::autosavePrefix + "2026-01-" + juce::String (10 + i) + " 10-00-00").createDirectory();
        const auto name = p->autosaveNow();
        const auto saves = OuroborosProcessor::autosaves();
        check (saves.size() == OuroborosProcessor::autosavesKept && saves[0] == name && folder.getChildFile (name).getNumberOfChildFiles (juce::File::findFiles) == 1,
               "an autosave saves the suite as a recipe and keeps the 12 newest (" + saves[0] + " ... " + saves[saves.size() - 1] + ")");
        check (! p->playedSinceAutosave.load(), "and waits for playback before the next one");
        folder.deleteRecursively();
        suite::Recipes::setFolderForTests ({});

        const auto log = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StyleLogTest.txt");
        log.deleteFile();
        suite::StyleLog::setFileForTests (log);
        suite::StyleLog::add ("song", { { "key", "Am" }, { "bpm", "138" }, { "degrees", "i-VI-III-VII" }, { "bars", "240" }, { "structure", "Intro-Breakdown-Drop-Outro" } });
        suite::StyleLog::add ("song", { { "key", "Am" }, { "bpm", "140" }, { "degrees", "i-VI-III-VII" }, { "bars", "224" }, { "structure", "Intro-Breakdown-Drop-Outro" } });
        suite::StyleLog::add ("song", { { "key", "F#m" }, { "bpm", "136" }, { "degrees", "i-iv-VI-V" }, { "bars", "208" }, { "structure", "Intro-Drop-Outro" } });
        suite::StyleLog::add ("master", { { "lufs", "-8.2" }, { "truepeak", "-0.4" } });
        const auto report = suite::StyleLog::report (suite::StyleLog::read());
        std::cout << "    " << report.replace ("\n", "\n    ") << std::endl;
        check (report.contains ("Songs logged: 3") && report.contains ("Am (2)") && report.contains ("100% minor") && report.contains ("usually 138 BPM (136-140)")
                   && report.contains ("i-VI-III-VII (2)") && report.contains ("usually 224 bars") && report.contains ("-8.2 LUFS"),
               "the style report: keys, tempo, progressions, length and loudness from the log");
        log.deleteFile();
        suite::StyleLog::setFileForTests (testStyleLog);
    }

    section ("Next step, key clashes, unused plugins, longer sections, render plan, section snapshots");
    {
        using namespace suite;
        auto entry = [] (uint32_t id, const char* plugin, const char* track, uint32_t flags, std::array<float, 4> metrics)
        {
            SuiteEntry e; e.id = id; e.plugin = plugin; e.track = track; e.flags = flags; e.metrics = metrics; return e;
        };
        const auto map = arrangement::fromTemplate (arrangement::templates().front());
        check (suitecheck::nextStep ({}, nullptr).text.contains ("Start the song"), "no map: start the song");
        std::vector<SuiteEntry> es { entry (1, "Ouroboros", "Master", Status::hasMap, {}) };
        check (suitecheck::nextStep (es, &map).text.contains ("Load Correspondence"), "no key source: load Correspondence");
        es.push_back (entry (2, "Correspondence", "Chords", 0, {}));
        es.push_back (entry (3, "Prima Materia", "Insert 4", 0, {}));
        check (suitecheck::nextStep (es, &map).text.contains ("Give Prima Materia on 'Insert 4' a role"), "a track without a role");
        es.back().flags = Status::roleSet;
        check (suitecheck::nextStep (es, &map).text.contains ("Put Mentalism"), "no Mentalism: put it on the master");
        es.push_back (entry (4, "Mentalism", "Master", 0, { 0.0f, 0.0f, 5.0f, 0.0f }));
        check (suitecheck::nextStep (es, &map).text.contains ("play the whole song"), "not measured: play it through");
        es.back().metrics = { 2.0f, 0.0f, 300.0f, 0.0f };
        check (suitecheck::nextStep (es, &map).text.contains ("2 arrangement note(s)"), "coach notes next");
        es.back().metrics = { 0.0f, 1.0f, 300.0f, 0.0f };
        check (suitecheck::nextStep (es, &map).text.contains ("checklist has 1"), "then the checklist");
        es.push_back (entry (5, "Azoth", "FX", Status::highLatency, {}));
        es[3].metrics = { 0.0f, 0.0f, 300.0f, 1.0f };
        check (suitecheck::nextStep (es, &map).text.contains ("3 s latency") && ! suitecheck::nextStep (es, &map).problem, "ready, with a latency warning");
        es.pop_back();
        check (suitecheck::nextStep (es, &map).text.contains ("Ready to export"), "then: ready to export");

        KeyBus::publish (9, 0);   // the song: A minor
        es.push_back (entry (6, "Emerald Tablet", "Vox loop", 0, { (float) (6 + 12), 0.8f, 20.0f, 0.0f }));   // F# minor
        es.push_back (entry (7, "Emerald Tablet", "Pad loop", 0, { 0.0f, 0.8f, 20.0f, 0.0f }));               // C major: relative major, fine
        const auto clashText = suitecheck::nextStep (es, &map).text;
        check (clashText.contains ("'Vox loop' sounds in F# minor") && ! clashText.contains ("Pad loop"),
               "key clash: a loop in F# minor is flagged; C major (A minor's relative) isn't");
        es.push_back (entry (8, "Kybalion", "Spare", Status::idle, {}));
        check (suitecheck::unused (es).joinIntoString ("") == "Kybalion on 'Spare'", "unused plugins are listed");
        {
            std::vector<suite::SuiteEntry> st { entry (20, "Prima Materia", "Kick", 0, { 1.0f, 0, 0, 0 }), entry (21, "Prima Materia", "Big Pads", 0, { 4.0f, 0, 0, 0 }),
                                                entry (22, "Kybalion", "Big Pads", 0, {}), entry (23, "Vibration", "Lead Saw", 0, {}),
                                                entry (24, "Prima Materia", "Insert 9", 0, { 0.0f, 0, 0, 0 }), entry (25, "Lapis", "Master", 0, {}) };
            const auto plan = suitecheck::stemPlan (st, "Sunrise");
            std::cout << "    " << plan.replace ("\n", "\n    ") << std::endl;
            check (plan.contains ("Drums: Kick") && plan.contains ("Music: Big Pads") && plan.contains ("Leads: Lead Saw") && plan.contains ("Unassigned: Insert 9")
                       && plan.contains ("Sunrise - Drums.wav") && ! plan.contains ("Master"),
                   "stem plan: tracks grouped into stems by role (or name), with file names");
            st.push_back (entry (26, "Kybalion", "Spare", Status::idle, {}));
            st.push_back (entry (27, "Vibration", "Lead Saw", 0, {}));
            const auto tidy = suitecheck::tidyPlan (st);
            check (tidy.contains ("Kybalion on 'Spare'") && tidy.contains ("Vibration twice on 'Lead Saw'"), "project tidy: idle and doubled-up plugins");
            check (suitecheck::tidyPlan ({ entry (30, "Lapis", "Master", 0, {}) }).contains ("Nothing to remove"), "a tidy project says so");
            st.push_back (entry (28, "Azoth", "FX", Status::missingFiles | Status::olderState, { 2.0f, 0, 0, 0 }));
            const auto h = suitecheck::health (st);
            check (h.size() == 2 && h[0].text.contains ("can't find 2 of its samples") && h[1].text.contains ("older version") && suitecheck::needsSessionFix (st),
                   "session health: missing samples and an older saved state");
        }

        suite::FixBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        suite::SectionMap m;
        m.totalBars = 48; m.count = 3;
        m.sections[0] = { 1, T::Breakdown };
        m.sections[1] = { 17, T::Build };   // only 4 bars
        m.sections[2] = { 21, T::Drop };
        p->setMap (m);
        const auto seq = suite::FixBus::send (suite::Fix::ExtendSection, suite::packArc (1, 8.0f));
        p->fixes->poll();
        const auto longer = p->getMap();
        check (longer.endBar (1) - longer.sections[1].startBar == 12 && longer.sections[2].startBar == 29 && longer.totalBars == 56,
               "the coach's fix makes the build 8 bars longer and moves the drop along");
        suite::FixBus::sendUndo (seq);
        p->fixes->poll();
        check (p->getMap() == m, "undo restores it");

        check (p->renderPlan().contains ("No versions yet"), "render plan before versions: make them first");
        p->setMap (arrangement::fromTemplate (arrangement::templates()[1]));
        p->makeVersions();
        const auto plan = p->renderPlan();
        check (plan.contains ("1. Original mix") && plan.contains ("2. Radio edit") && plan.contains ("-6 dBFS") && plan.contains ("4. Club edit")
                   && plan.contains ("premaster") && ! plan.contains ("-8 LUFS"),
               "after Make versions: four label-ready premaster renders (peaks -6 dBFS, no loudness target)");

        // Section snapshots: entering a breakdown recalls snapshot B for the rest of the suite.
        suite::SnapshotBus::reset();
        auto o = std::make_unique<OuroborosProcessor>();
        o->prepareToPlay (48000.0, 512);
        suite::SectionMap sm;
        sm.totalBars = 8; sm.count = 2;
        sm.sections[0] = { 1, T::Drop };
        sm.sections[1] = { 5, T::Breakdown };
        o->setMap (sm);
        o->captureSnapshot (1);
        set (*o, OuroborosProcessor::sectionSnapId ((int) T::Breakdown), 2.0f);   // B
        FakePlayHead head;
        head.bpm = 120.0;
        o->setPlayHead (&head);
        for (double t : { 1.0, 7.0, 9.0 })   // in the drop, still in the drop, then the breakdown (bar 5 = 8 s)
        {
            head.ppq = t * 2.0;
            juce::AudioBuffer<float> b (2, 64);
            juce::MidiBuffer midi;
            o->processBlock (b, midi);
            o->followSectionSnapshots();
        }
        o->setPlayHead (nullptr);
        suite::SnapshotAction action {};
        int slot = -1;
        check (o->sectionSnapsSent.load() == 1 && suite::SnapshotBus::read (suite::SnapshotBus::latest(), action, slot)
                   && action == suite::SnapshotAction::Recall && slot == 1 && o->getMap() == sm,
               "entering the breakdown recalls snapshot B across the suite, and Ouroboros keeps its map");
        KeyBus::withdraw();
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

        // The same song as a reference file, starting after 0.37 s of silence (so the bar grid
        // has to be found from the audio).
        {
            const auto lead = (int) (0.37 * sampleRate);
            const auto total = (int) ((104.0 * barSec + 0.5) * sampleRate);
            juce::AudioBuffer<float> file (2, lead + total);
            file.clear();
            juce::AudioBuffer<float> chunk (2, 4096);
            for (int pos = 0; pos < total; pos += 4096)
            {
                const auto n = std::min (4096, total - pos);
                chunk.setSize (2, n, false, false, true);
                song (chunk, pos);
                for (int ch = 0; ch < 2; ++ch) file.copyFrom (ch, lead + pos, chunk, ch, 0, n);
            }
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ouroboros_reference.wav");
            writeWav (wav, file);
            const auto fromFile = OuroborosProcessor::proposeFromFile (wav, 140.0);
            const auto fileText = arrangement::toString (fromFile);
            std::cout << "    from the file: " << fileText << std::endl;
            check (fileText == "1:Intro 17:Groove 33:Breakdown 49:Build 57:Drop 89:Outro /104", "a reference file gives the same structure");
            check (OuroborosProcessor::proposeFromFile (wav.getSiblingFile ("does_not_exist.wav"), 140.0).count == 0, "a missing file gives nothing");
            wav.deleteFile();
        }
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
        SessionPanel session (*p);
        snapshot (session, outDir.getChildFile ("ouroboros_session.png"));
    }

    section ("What changed");
    {
        suite::MoveLog::clear();
        auto p = std::make_unique<OuroborosProcessor>();
        prepare (*p);
        FakePlayHead head;
        head.bpm = 138.0;
        head.ppq = 4.0 * 63.5;   // bar 64
        p->setPlayHead (&head);
        render (*p, 0.05, {}, nullptr, &head);
        p->setPlayHead (nullptr);
        check (suite::MoveLog::currentBar() == 64, "the play head's bar is shared, so moves say where they happened (" + juce::String (suite::MoveLog::currentBar()) + ")");
        suite::MoveLog::add ("Prima Materia", "Pads", "Breakdowns quieter: Breakdown level -2.0 dB -> -4.0 dB", suite::MoveLog::fix);
        suite::MoveLog::add ("Quintessence", "Pads", "Kept: band 2 250 Hz -1.5 dB (distance from a pad's usual tone 2.0 -> 1.6 dB)", suite::MoveLog::kept, 40);
        suite::MoveLog::add ("Mentalism", "Master", "Undid a balance step on 3 tracks: the master moved away from its target (1.1 -> 1.8 dB)", suite::MoveLog::undone, 96);
        suite::MoveLog::add ("Quintessence", "Lead", "Dynamics, mid band: -24.0 dB, 3.0:1 (its loud moments stand 18.0 dB out; a lead wants about 6)", suite::MoveLog::info, 100);
        ChangesPanel panel;
        check (suite::MoveLog::read().size() == 4 && suite::MoveLog::read()[3].bar == 64, "a move logged without a bar gets the current one");
        panel.search.setText ("why did the pads get quieter at bar 64?");
        int matching = 0;
        for (const auto& e : suite::MoveLog::read()) matching += suite::MoveLog::matches (e, panel.search.getText()) ? 1 : 0;
        check (matching == 1, "asking about bar 64 finds the move made there");
        panel.search.setText ({});
        if (outDir != juce::File()) snapshot (panel, outDir.getChildFile ("ouroboros_changes.png"));
    }

    section ("Finish everything");
    {
        suite::FixBus::reset();
        auto p = std::make_unique<OuroborosProcessor>();
        prepare (*p);
        const auto report = p->finishEverything();
        int mix = 0, exported = 0;
        for (uint32_t s = 1; s <= suite::FixBus::latest(); ++s)
        {
            suite::FixBus::Message m;
            if (suite::FixBus::read (s, m)) { mix += m.fix == suite::Fix::MixItAll ? 1 : 0; exported += m.fix == suite::Fix::ExportAll ? 1 : 0; }
        }
        check (mix == 1 && exported == 1 && report.contains ("render"), "one click: Mix it and Export all go out, with the render plan to follow");
        suite::FixBus::reset();
    }


    section ("Autopilot page: one switch per group, saved with the project");
    {
        suite::Autopilot::reset();
        AutopilotPanel panel;
        check (panel.switches[0].getToggleState() && panel.switches[1].getToggleState() && panel.switches[2].getToggleState() && panel.switches[3].getToggleState(), "every group starts on");
        panel.switches[2].setToggleState (false, juce::sendNotificationSync);
        check (! suite::Autopilot::on (suite::Autopilot::arrangement) && suite::Autopilot::on (suite::Autopilot::harshness), "switching one off affects only that group");
        auto p = std::make_unique<OuroborosProcessor>();
        juce::MemoryBlock state;
        p->getStateInformation (state);
        suite::Autopilot::reset();
        auto q = std::make_unique<OuroborosProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (! suite::Autopilot::on (suite::Autopilot::arrangement) && suite::Autopilot::on (suite::Autopilot::stability), "a saved project brings its switches back");
        suite::Autopilot::count (suite::Autopilot::peaksCaught, 3);
        check (suite::Autopilot::countOf (suite::Autopilot::peaksCaught) == 3 && juce::String (suite::Autopilot::counterName (suite::Autopilot::peaksCaught)) == "peaks caught",
               "the readout counts what was done");
        suite::Autopilot::reset();
    }


    section ("Mixer page: every track's suggested moves, and one export");
    {
        suite::PlanBus::reset();
        suite::PlanBus::Plan kick, pad;
        kick.id = 0x501; kick.track = "Kick"; kick.role = 1;
        kick.levelDb[(size_t) suite::SectionType::Breakdown] = suite::PlanBus::offDb;
        pad.id = 0x502; pad.track = "Pads"; pad.role = 4; pad.ramp = true;
        pad.levelDb[(size_t) suite::SectionType::Drop] = -2.0f; pad.highCutHz[(size_t) suite::SectionType::Outro] = 8000.0f; pad.gapQuarters = 1.0;
        suite::PlanBus::publish (kick); suite::PlanBus::publish (pad);
        suite::SectionMap map;
        map.totalBars = 32; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove }; map.sections[1] = { 9, suite::SectionType::Breakdown };
        map.sections[2] = { 17, suite::SectionType::Build }; map.sections[3] = { 25, suite::SectionType::Drop };
        const auto table = MixerPanel::tableText (map).joinIntoString ("\n");
        check (table.contains ("Kick") && table.contains ("mute") && table.contains ("Pads") && table.contains ("-2.0") && table.contains ("rising"),
               "the table has each track's moves per section (" + table.replace ("\n", " | ").replace ("\t", " ") + ")");
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ouro-mixer-test");
        dir.deleteRecursively();
        const auto mid = MixerPanel::exportAll (dir, map, 138.0);
        juce::MidiFile f;
        juce::FileInputStream in (mid);
        check (in.openedOk() && f.readFrom (in) && f.getNumTracks() == 3 && dir.getChildFile ("Mixer moves.txt").existsAsFile(),
               "one MIDI file: a tempo track and a track per FL track, plus the table as text");
        dir.deleteRecursively();
        suite::PlanBus::reset();
    }


    section ("Autopilot: each fix has its own switch, saved with the project");
    {
        suite::Autopilot::reset();
        AutopilotPanel panel;
        check ((int) panel.fixes.size() == suite::Autopilot::numFeatures && panel.fixes[(size_t) suite::Autopilot::humF]->getToggleState(), "every fix is listed, on");
        panel.fixes[(size_t) suite::Autopilot::humF]->setToggleState (false, juce::sendNotificationSync);
        check (! suite::Autopilot::on (suite::Autopilot::humF) && suite::Autopilot::on (suite::Autopilot::maskingF), "switching one fix off leaves the rest of its group on");
        suite::Autopilot::set (suite::Autopilot::harshness, false);
        check (! suite::Autopilot::on (suite::Autopilot::spikeTamerF) && suite::Autopilot::switchedOn (suite::Autopilot::spikeTamerF), "a fix runs only when its group is on too");
        auto p = std::make_unique<OuroborosProcessor>();
        juce::MemoryBlock state;
        p->getStateInformation (state);
        suite::Autopilot::reset();
        auto q = std::make_unique<OuroborosProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (! suite::Autopilot::switchedOn (suite::Autopilot::humF) && suite::Autopilot::switchedOn (suite::Autopilot::maskingF) && ! suite::Autopilot::on (suite::Autopilot::harshness),
               "a saved project brings back each fix's switch");
        suite::Autopilot::reset();
    }


    section ("The suite director: one plan for every transition");
    {
        namespace dr = suite::director;
        suite::Registry::reset();
        suite::DirectorBus::reset();
        const auto map = arrangement::fromTemplate (arrangement::templates()[1]);   // two breakdowns, two drops
        dr::Input in;
        in.map = map; in.bpm = 138.0; in.seed = 7;
        const auto plan = dr::makePlan (in);
        int intoDrops = 0, ledDrops = 0, heroes = 0;
        bool pileUp = false;
        const dr::Moment* lastHero = nullptr;
        for (const auto& m : plan.moments)
        {
            if (m.kind == dr::kIntoDrop) { ++intoDrops; if (m.hero != dr::pNone || m.silence != dr::gNone) ++ledDrops; }
            if (m.hero != dr::pNone)
            {
                ++heroes;
                if (lastHero != nullptr && m.atQ - lastHero->atQ < 8.0f - 1.0e-3f) pileUp = true;
                lastHero = &m;
                check (m.gesture[(size_t) m.hero] == m.heroGesture && dr::gesture (m.heroGesture).player == m.hero, "the hero plays its own gesture");
            }
        }
        check (intoDrops == 2 && ledDrops == 2, "both drops get a lead-in (a hero or the director's silence): " + juce::String (ledDrops));
        check (heroes >= 4 && ! pileUp, "the transitions have heroes, never two within two bars (" + juce::String (heroes) + " led)");
        check (dr::makePlan (in).hash == plan.hash, "the same inputs give the same plan");
        auto other = in; other.seed = 8;
        check (dr::makePlan (other).hash != plan.hash, "new dice, a new plan");
        check (plan.candidatesTried == 6 && plan.score.total == plan.score.total, "the best of six scored candidates: " + plan.score.text());
        check ((int) plan.tension.size() == map.totalBars && plan.tension[(size_t) (88 - 1)] > plan.tension[(size_t) (81 - 1)], "the tension rises through a build");
        check (plan.character.size() == 2 && plan.character[1] == dr::chHuge, "drop identities: the last drop is the Huge one");
        check (plan.breakdowns.size() == 2 && plan.motif != dr::gNone, "two breakdown stories, and a motif");
        const auto firstDrop = std::find_if (plan.moments.begin(), plan.moments.end(), [] (const dr::Moment& m) { return m.kind == dr::kIntoDrop; });
        check (firstDrop != plan.moments.end() && firstDrop->buildHoldDb > 0.0f && firstDrop->cutTails && firstDrop->wideOnHit > 0.0f && firstDrop->evenDb == 0.0f,
               "into a drop: the build held back (a level ride, no limiter), the tails cut, wide on the hit");

        // no full silence before a drop unless you allow it (the Snap and the Breath)
        auto noSnaps = [] (const dr::Plan& p) { for (const auto& m : p.moments) if (m.silence == dr::gSiSnap || m.silence == dr::gSiBreath) return false; return true; };
        bool allOff = true, anyOn = false;
        for (uint32_t sd = 1; sd <= 20; ++sd)
        {
            auto off = in; off.seed = sd; off.story = dr::storyFestival;
            allOff = allOff && noSnaps (dr::makePlan (off));
            auto on = off; on.silencesBeforeDrops = true;
            anyOn = anyOn || ! noSnaps (dr::makePlan (on));
        }
        check (allOff && anyOn, "by default no Snap or Breath before any drop (20 Festival plans); switched on, they come back");
        ouro::DirectorHost silenceHost;
        check (! silenceHost.settings.silencesBeforeDrops && silenceHost.chat ("allow silence before drops").startsWith ("Silences before drops on")
                   && silenceHost.settings.silencesBeforeDrops && silenceHost.chat ("no silence before drops").startsWith ("No full silence") && ! silenceHost.settings.silencesBeforeDrops,
               "the switch: off by default, on and off from the chat too");

        // stories, safe mode, the plugins in the session
        auto restrained = in; restrained.story = dr::storyRestrained;
        auto festival = in; festival.story = dr::storyFestival;
        auto spentOf = [] (const dr::Plan& p) { float t = 0.0f; for (const auto& m : p.moments) t += m.spent; return t; };
        check (spentOf (dr::makePlan (restrained)) < spentOf (dr::makePlan (festival)), "a Festival story does more than a Restrained one");
        auto safe = in; safe.safe = true;
        bool risky = false;
        for (const auto& m : dr::makePlan (safe).moments) for (auto g : m.gesture) if (g != dr::gNone && ! dr::gesture (g).safe) risky = true;
        check (! risky, "safe mode: only filters, levels and space");
        auto onlyAzoth = in; onlyAzoth.present = (uint8_t) ((1u << dr::pAzoth) | (1u << dr::pScene) | (1u << dr::pSilence));
        bool foreign = false;
        for (const auto& m : dr::makePlan (onlyAzoth).moments) if (m.hero != dr::pNone && m.hero != dr::pAzoth && m.hero != dr::pSilence) foreign = true;
        check (! foreign, "only plugins in the session are given parts");

        // steering: intent, pins, requests
        auto calm = in;
        calm.intent.assign ((size_t) map.count, (uint8_t) dr::iAuto);
        calm.intent[4] = dr::iNoMoves;   // the first drop
        bool movedInDrop = false;
        for (const auto& m : dr::makePlan (calm).moments)
            if (m.section == 4 && m.kind != dr::kIntoDrop && m.hero != dr::pNone) movedInDrop = true;
        check (! movedInDrop, "painted 'No moves': the first drop's phrases are left alone");
        auto pinned = in;
        pinned.pins.push_back ({ 50, dr::pKybalion, dr::gNone });
        const auto pp = dr::makePlan (pinned);
        const auto pinAt = std::find_if (pp.moments.begin(), pp.moments.end(), [] (const dr::Moment& m) { return m.kind == dr::kPin; });
        check (pinAt != pp.moments.end() && pinAt->hero == dr::pKybalion && (pinAt->flags & dr::fPinned), "a pin: Kybalion leads at bar 50");
        const auto r1 = dr::parseRequest ("make the second drop hit harder");
        const auto r2 = dr::parseRequest ("less glitch in the breakdown");
        check (r1.words == dr::wHarder && r1.sectionType == (int) suite::SectionType::Drop && r1.dropOrdinal == 1, "chat: 'the second drop hit harder'");
        check ((r2.words & dr::wLessGlitch) && r2.sectionType == (int) suite::SectionType::Breakdown, "chat: 'less glitch in the breakdown'");
        auto darker = in; darker.requests.push_back (dr::parseRequest ("darker"));
        check (dr::makePlan (darker).character[0] == dr::chDark, "'darker': the drops turn Dark");

        // what was heard: a drop that doesn't land gets more
        auto heard = in;
        heard.feedback.assign ((size_t) map.count, {});
        heard.feedback[3] = { -10.0f, -11.0f, -9.5f, true };   // the build
        heard.feedback[4] = { -9.0f, -9.0f, -9.0f, true };     // the drop, only 0.5 LU over the build's end
        const auto hp = dr::makePlan (heard);
        const auto into = std::find_if (hp.moments.begin(), hp.moments.end(), [] (const dr::Moment& m) { return m.kind == dr::kIntoDrop; });
        check (into != hp.moments.end() && (into->flags & dr::fAudio) && into->buildHoldDb >= 1.5f, "a drop Mentalism heard not landing gets more contrast");
        heard.feedback[1] = { -14.0f, -14.0f, -14.0f, true };   // the groove ...
        heard.feedback[2] = { -8.0f, -8.0f, -8.0f, true };      // ... jumps 6 LU into the breakdown
        const auto jump = dr::makePlan (heard);
        const auto br = std::find_if (jump.moments.begin(), jump.moments.end(), [] (const dr::Moment& m) { return m.kind == dr::kIntoBreak; });
        check (br != jump.moments.end() && br->evenDb < -1.0f, "an unplanned jump is evened out (a level ride over two bars)");

        // trust: lint, summary, explain, structure
        check (std::none_of (plan.lints.begin(), plan.lints.end(), [] (const dr::Lint& l) { return l.text.startsWith ("Two heroes"); }), "the lint finds no two heroes at once");
        check (dr::summary (plan, map, 4.0).contains ("Drop 2 is Huge"), "the plan in words");
        check (dr::explain (plan, map, (89 - 1) * 4.0 - 0.5, 4.0, 138.0).size() >= 3, "explain any second");
        auto shortBuild = arrangement::fromTemplate (arrangement::templates()[1]);
        shortBuild.sections[4].startBar = 84;   // a 3-bar build
        auto sb = in; sb.map = shortBuild;
        check (dr::adviseStructure (sb).any, "the structure: a build too short to climb is lengthened");

        // Ouroboros' host: lock, re-roll, freeze, undo, version lock, history, chat, teach, profile
        ouro::DirectorHost host;
        host.presentOverride = 0x7f;
        host.update (map, 138.0, 4.0);
        check (host.havePlan && suite::DirectorBus::live(), "the plan is published");
        const auto& led = *std::find_if (host.plan.moments.begin(), host.plan.moments.end(), [] (const dr::Moment& m) { return m.hero != dr::pNone && m.kind == dr::kIntoDrop; });
        const auto key = led.key;
        const auto heroBefore = led.heroGesture;
        host.lock (key);
        host.rerollSong();
        host.update (map, 138.0, 4.0);
        const auto li = host.plan.indexOf (key);
        check (li >= 0 && host.plan.moments[(size_t) li].heroGesture == heroBefore && (host.plan.moments[(size_t) li].flags & dr::fLocked), "a locked moment keeps its hero through a re-roll");
        host.undoMoment (key);
        host.update (map, 138.0, 4.0);
        check ((host.plan.moments[(size_t) host.plan.indexOf (key)].flags & dr::fLocked) == 0, "undo takes the lock back");
        host.freeze (key, true);
        const auto frozen = host.plan.moments[(size_t) host.plan.indexOf (key)];
        host.rerollSong(); host.rerollSong();
        host.update (map, 138.0, 4.0);
        check (host.plan.moments[(size_t) host.plan.indexOf (key)].same (frozen), "a frozen moment stays exactly as it was");
        const auto reply = host.chat ("less glitch in the breakdown");
        check (reply.startsWith ("OK") && host.settings.requests.size() == 1, "chat: a request is kept");
        check (host.chat ("undo").startsWith ("Took back") && host.settings.requests.empty(), "chat: undo");
        host.setVersionLock (true);
        const auto lockedHash = host.plan.hash;
        host.rerollSong();
        host.update (map, 138.0, 4.0);
        check (host.plan.hash == lockedHash, "version lock: the plan reproduces exactly");
        host.setVersionLock (false);
        check (host.history.size() >= 6 && host.stepBack (0), "every plan is in the history, to step back to");
        host.update (map, 138.0, 4.0);
        host.teach (host.plan.moments[(size_t) host.plan.indexOf (key)].key, "My drop");
        auto saved = host.profileTree (true);
        ouro::DirectorHost again;
        again.applyProfileTree (saved, true);
        check (again.settings.learned.size() == 1 && again.settings.learned[0].name == "My drop" && again.settings.frozen.size() == host.settings.frozen.size()
                   && again.settings.seed == host.settings.seed, "the director's state saves with the project (taught gestures, freezes, the dice)");
        bool learnedUsed = false;
        auto taught = host.inputFor (map, 138.0, 4.0);
        for (uint32_t sd = 1; sd < 12 && ! learnedUsed; ++sd)
        {
            taught.seed = sd; taught.frozen.clear();
            for (const auto& m : dr::makePlan (taught).moments) learnedUsed = learnedUsed || (m.flags & dr::fLearned);
        }
        check (learnedUsed, "a taught gesture comes back at that kind of moment");
        check (! host.renderList (4.0, 138.0).isEmpty() && host.renderMarkers (4.0, 138.0).getNumTracks() == 1, "the transitions render list and its markers");
        check (host.ticker (0.0, 4.0).startsWith ("Next: bar"), "the ticker: what's next");

        // the bus: what each plugin sees, and the fidelity check
        suite::DirectorBus::Follower f;
        check (f.refresh() && f.size() == (int) host.plan.moments.size(), "every plugin reads the same plan");
        const auto hi = host.plan.indexOf (key);
        const auto& hm = host.plan.moments[(size_t) hi];
        const auto w = suite::DirectorBus::windowFor (f, hi, hm.hero);
        check (hm.hero == dr::pSilence || f.actingAt (0.5 * (w.first + w.second), hm.hero) >= 0, "the hero finds its moment on the audio thread");
        suite::DirectorBus::reportActed (host.plan.hash, hi, hm.hero);
        check (host.fidelityReport (4.0).contains ("1 of"), "the fidelity check counts what happened: " + host.fidelityReport (4.0).upToFirstOccurrenceOf ("\n", false, false));
        suite::DirectorBus::withdraw();
        check (! f.refresh(), "no plan published: every plugin plays its own transitions");
    }


    section ("Round 1: per-player control, opt-outs, alternatives, the plan for FL; the delivery package");
    {
        namespace dr = suite::director;
        suite::Registry::reset();
        suite::DirectorBus::reset();
        const auto map = arrangement::fromTemplate (arrangement::templates()[1]);
        ouro::DirectorHost h;
        h.presentOverride = 0x7f;
        h.update (map, 138.0, 4.0);
        // supporters never outlive a trimmed hero
        bool orphan = false;
        for (const auto& m : h.plan.moments)
            if ((m.flags & dr::fTrimmed) && m.supporters() > (m.acts (dr::pScene) ? 1 : 0) + (m.silence != dr::gNone ? 1 : 0)) orphan = true;
        check (! orphan, "a hero stepping back takes its supporters with it");
        // re-plan only when something changed
        const auto hash = h.plan.hash;
        check (! h.update (map, 138.0, 4.0) && h.plan.hash == hash, "nothing changed: no re-plan");
        // lock one player, re-roll another
        const dr::Moment* rich = nullptr;
        for (const auto& m : h.plan.moments) if (m.hero != dr::pNone && m.supporters() >= 1 && ! (m.flags & dr::fTrimmed)) { rich = &m; break; }
        if (rich != nullptr)
        {
            const auto key = rich->key;
            const auto hero = rich->hero, heroGesture = rich->heroGesture;
            h.rerollPlayer (key, dr::pScene);   // everything else stays
            h.update (map, 138.0, 4.0);
            const auto& after = h.plan.moments[(size_t) h.plan.indexOf (key)];
            check (after.hero == hero && after.heroGesture == heroGesture, "re-roll one player: the hero and the others keep what they play");
            h.lockPlayer (key, hero, true);
            h.rerollSong(); h.rerollSong();
            h.update (map, 138.0, 4.0);
            check (h.plan.moments[(size_t) h.plan.indexOf (key)].gesture[(size_t) hero] == heroGesture, "lock one player: it keeps its gesture through re-rolls");
            const auto alts = h.alternatives (key, map, 138.0, 4.0);
            check (! alts.empty(), "alternatives for a moment: " + juce::String ((int) alts.size()));
        }
        else check (false, "a led moment with supporters to test on");
        h.lockAllExcept (h.plan.moments.front().key);
        check (h.settings.locks.size() >= 3, "lock every other moment");
        // opt-out: a track that ignores the director
        suite::Registry::announce (0xabcu, "Kybalion", "Pad Bus", 0, {});
        h.settings.optOut.add ("Pad Bus");
        h.update (map, 138.0, 4.0);
        h.publishNow (4.0, 138.0);
        suite::DirectorBus::Follower f;
        check (f.refresh() && f.optedOut (0xabcu) && ! f.refreshFor (0xabcu) && f.refreshFor (0x123u), "a track told to ignore the director plays its own transitions");
        // the plan for collaborators and for FL
        check (h.planSheet (map, 4.0).contains ("DIRECTOR PLAN") && h.movesMidi (4.0, 138.0).getNumTracks() == 1 + dr::numPlayers, "the plan sheet, and the moves as MIDI (a track per plugin)");
        auto saved = h.profileTree (true);
        ouro::DirectorHost back;
        back.applyProfileTree (saved, true);
        check ((int) saved["version"] == 2 && back.settings.optOut.contains ("Pad Bus") && back.settings.playerLocks.size() == h.settings.playerLocks.size(),
               "saved with a format version: opt-outs and player locks come back");
        // the delivery package
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroDelivery");
        dir.deleteRecursively();
        dir.createDirectory();
        auto o = std::make_unique<OuroborosProcessor>();
        o->setMap (map);
        o->replan();
        const auto pkg = o->writeDeliveryPackage (dir, 1, "Artist", "Song");
        check (pkg.getChildFile ("Mastering brief.txt").existsAsFile() && pkg.getChildFile ("Render plan.txt").existsAsFile()
                   && pkg.getChildFile ("Files to deliver.txt").loadFileAsString().contains ("Stem - Drums") && pkg.getChildFile ("Section markers.mid").existsAsFile()
                   && pkg.getChildFile ("Director moves.mid").existsAsFile(),
               "the delivery package: brief, render plan, the label's file list with stems, markers, the director's moves");
        check (o->renderPlan().contains ("Gate:"), "the render plan ends with Mentalism's gate");
        dir.deleteRecursively();
        suite::Registry::reset();
        suite::DirectorBus::withdraw();
    }


    section ("Label-ready: the song measured, the label's norms, the checks and the gate");
    {
        using T = suite::SectionType;
        // a song: intro, build, drop, breakdown, build, drop, outro (96 bars)
        suite::SectionMap map;
        const std::pair<T, int> form[] = { { T::Intro, 1 }, { T::Build, 17 }, { T::Drop, 25 }, { T::Breakdown, 41 }, { T::Build, 57 }, { T::Drop, 65 }, { T::Outro, 81 } };
        for (const auto& [type, bar] : form) { map.sections[(size_t) map.count].type = type; map.sections[(size_t) map.count].startBar = bar; ++map.count; }
        map.totalBars = 96;

        // the analyser, on a file: a quiet bar of pad, then a loud bar with four kicks
        {
            const double fs = 44100.0, bpm = 120.0;
            const auto barSamples = (int) (fs * 2.0);
            juce::AudioBuffer<float> audio (2, barSamples * 4);
            audio.clear();
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const auto bar = i / barSamples, inBar = i % barSamples;
                const auto t = (double) i / fs;
                float v = 0.02f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t);
                if (bar % 2 == 1)
                {
                    const auto beatPos = inBar % (barSamples / 4);
                    v += 0.6f * (float) std::exp (-beatPos / (0.05 * fs)) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * beatPos / fs);
                }
                audio.setSample (0, i, v); audio.setSample (1, i, v);
            }
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LabLabTest.wav");
            wav.deleteFile();
            {
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), fs, 2, 24, {}, 0));
                if (w != nullptr) w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
            double seconds = 0.0;
            const auto bars = lab::analyseFile (wav, bpm, seconds);
            check (bars.size() == 4 && std::abs (seconds - 8.0) < 0.01, "a file is measured bar by bar (" + juce::String ((int) bars.size()) + " bars, " + juce::String (seconds, 2) + " s)");
            if (bars.size() == 4)
            {
                check (bars[1].db > bars[0].db + 10.0f && bars[3].db > bars[2].db + 10.0f, "the kick bars measure louder than the pad bars");
                check (bars[1].onsets >= 3.0f && bars[1].onsets <= 5.0f && bars[0].onsets <= 1.0f,
                       "four kicks count as about four onsets (" + juce::String (bars[1].onsets, 0) + "), the steady pad as none (" + juce::String (bars[0].onsets, 0) + ")");
                check (bars[1].bandDb (1) > bars[1].bandDb (6) + 10.0f, "the kick's energy sits in the low bands");
                check (bars[0].sideDb < bars[0].db - 40.0f, "a mono file has no side");
            }
            wav.deleteFile();
        }

        // a song's bars, made directly: levels per section, onsets in the drops
        auto makeBars = [&map] (bool good)
        {
            std::vector<lab::Bar> bars ((size_t) map.totalBars);
            for (int b = 0; b < map.totalBars; ++b)
            {
                int i = 0;
                for (int k = 0; k < map.count; ++k) if (map.sections[(size_t) k].startBar <= b + 1) i = k;
                const auto type = map.sections[(size_t) juce::jmax (0, i)].type;
                const auto start = map.sections[(size_t) juce::jmax (0, i)].startBar - 1, len = map.endBar (i) - 1 - start;
                const auto pos = (float) (b - start) / (float) juce::jmax (1, len - 1);
                float db = -10.0f;
                if (good)
                    db = type == T::Intro ? -15.0f : type == T::Build ? -17.0f + 6.0f * pos : type == T::Drop ? -8.0f : type == T::Breakdown ? -18.0f : -12.0f - 6.0f * pos;
                auto& x = bars[(size_t) b];
                x.heard = true; x.db = db; x.sideDb = db - (good ? 8.0f : 30.0f);
                x.onsets = good ? (type == T::Drop ? 8.0f : type == T::Breakdown ? 1.0f : 4.0f) : 4.0f;
                x.lowFlux = type == T::Drop ? 4.0f : 1.0f;
                for (int k = 0; k < lab::numBands; ++k) x.band[(size_t) k] = db - 3.0f * (float) std::abs (k - 3) - (good ? 0.0f : (k == 4 ? -8.0f : 0.0f));
            }
            return bars;
        };

        // statistics: a weighted median and its spread; recent references count more, yours double
        {
            const auto st = lab::statOf ({ { 1.0f, 1.0f }, { 2.0f, 1.0f }, { 3.0f, 1.0f }, { 4.0f, 1.0f }, { 100.0f, 1.0f } });
            check (std::abs (st.median - 3.0f) < 1.0e-4f && st.lo <= 2.0f && st.hi >= 4.0f && st.outside (3.0f) == 0.0f && st.outside (100.0f) > 1.0f,
                   "the label's norms: a median and a 20-80% range (" + st.text() + "), an outlier is outside it");
            lab::Reference fresh, old, mine;
            fresh.addedMs = juce::Time::currentTimeMillis();
            old.addedMs = fresh.addedMs - (juce::int64) (2.0 * 365.25 * 24 * 3600 * 1000.0);
            mine.addedMs = fresh.addedMs; mine.yours = true;
            check (std::abs (lab::Profile::weightOf (old) - 0.5f) < 0.02f && std::abs (lab::Profile::weightOf (mine) - 2.0f) < 0.02f,
                   "a reference two years old counts half; your own signed track counts double");
        }

        // a profile from two references; the song is closest to the one that sounds like it
        lab::Profile prof;
        prof.name = "Test label";
        {
            for (int r = 0; r < 2; ++r)
            {
                lab::Reference ref;
                ref.name = r == 0 ? "Bright" : "Dark"; ref.bpm = 138.0; ref.seconds = 96 * 4 * 60.0 / 138.0;
                ref.addedMs = juce::Time::currentTimeMillis();
                ref.map = map;
                auto bars = makeBars (true);
                for (auto& b : bars) for (int k = 0; k < lab::numBands; ++k) b.band[(size_t) k] += (r == 0 ? 1.5f : -1.5f) * (float) k;
                ref.sections = lab::sections (bars, map);
                ref.summarise();
                prof.refs.push_back (ref);
            }
            prof.learn();
            check (prof.drops.valid() && std::abs (prof.drops.median - 2.0f) < 1.0e-4f && prof.introBars.valid() && std::abs (prof.introBars.median - 16.0f) < 1.0e-4f,
                   "the profile learns the references' form (2 drops, a 16-bar intro)");
            check (prof.impactDb.valid() && prof.impactDb.median > 2.0f && prof.breakdownDepthDb.median > 6.0f,
                   "and their drop impact (" + prof.impactDb.text() + " dB) and breakdown depth (" + prof.breakdownDepthDb.text() + " dB)");
            auto songBars = makeBars (true);
            for (auto& b : songBars) for (int k = 0; k < lab::numBands; ++k) b.band[(size_t) k] -= 1.4f * (float) k;
            const auto [best, dist] = prof.closest (lab::sections (songBars, map), 138.0);
            check (best == 1, "a dark song's closest reference is the dark one (" + juce::String (best) + ", " + juce::String (dist, 2) + " dB)");
            const auto back = lab::Profile::fromTree (prof.toTree());
            check (back.name == prof.name && back.refs.size() == 2 && back.drops.valid() && std::abs (back.impactDb.median - prof.impactDb.median) < 0.01f,
                   "a profile saves and loads with its references");
        }

        // the checks: a well-shaped, clean song scores higher than a flat, clipping one, and the gate holds it back
        auto inputsFor = [&] (bool good)
        {
            lab::Inputs in;
            in.map = map; in.bpm = 138.0;
            in.bars = makeBars (good);
            in.sections = lab::sections (in.bars, map);
            in.profile = &prof;
            in.master.valid = true;
            in.master.lufs = good ? -14.0f : -7.0f;
            in.master.truePeakDb = good ? -6.2f : 0.4f;
            in.master.correlation = good ? 0.6f : -0.3f;
            in.master.lowCorrelation = good ? 0.98f : 0.2f;
            in.master.monoLossDb = good ? 0.8f : 6.0f;
            in.master.measuredSeconds = 200.0f;
            in.master.clippedSamples = good ? 0 : 500;
            in.master.readiness = good ? 92 : 30;
            return in;
        };
        const auto goodIn = inputsFor (true), badIn = inputsFor (false);
        const auto good = lab::evaluate (goodIn), bad = lab::evaluate (badIn);
        bool inRange = true, named = true;
        std::set<int> areas;
        for (const auto* r : { &good, &bad })
            for (const auto& c : r->checks)
            {
                inRange = inRange && c.score >= 0.0f && c.score <= 1.0f;
                named = named && c.name.isNotEmpty() && c.detail.isNotEmpty();
                if (c.measured) areas.insert (c.area);
            }
        check (inRange && named, "every check has a name, says what it found, and scores 0..1 (" + juce::String ((int) good.checks.size()) + " checks)");
        check (areas.size() >= 6, "the checks cover the areas the suite can hear (" + juce::String ((int) areas.size()) + " of 8 measured without the parts)");
        check (good.overall > bad.overall + 10, "a well-shaped clean song scores higher (" + juce::String (good.overall) + ") than a flat clipping one (" + juce::String (bad.overall) + ")");
        check (! bad.ready && ! bad.blockers.isEmpty(), "the flat clipping song is held back, with what blocks it (" + bad.blockers.joinIntoString ("; ") + ")");
        check (good.verdict.isNotEmpty() && bad.nextAction.isNotEmpty(), "a one-line verdict and the next thing to do (" + bad.nextAction + ")");
        bool barred = false;
        for (const auto& c : bad.checks) barred = barred || (c.measured && c.score < 0.8f && c.bar > 0);
        check (barred, "problems say where they are (a bar)");
        {
            auto strict = prof; strict.readyThreshold = 101;
            auto in = goodIn; in.profile = &strict;
            check (! lab::evaluate (in).ready, "the ready threshold is the profile's");
        }

        // the writing checks need Correspondence's parts: a hook with shape scores higher than one note repeated
        {
            suite::SongBus::Song song;
            song.valid = true; song.root = 9; song.scale = 0; song.hookStartQ = 96.0;
            const int shapeNotes[] = { 69, 72, 76, 74, 72, 76, 79, 81, 69, 72, 76, 74, 72, 74, 76, 69 };
            for (int i = 0; i < 16; ++i) song.hook.push_back ({ shapeNotes[i], (float) (96.0 + i * 2.0), i % 2 == 0 ? 1.5f : 0.5f, 100 });
            std::vector<suite::SongBus::Note> flat;
            for (int i = 0; i < 16; ++i) flat.push_back ({ 70, (float) (96.0 + i * 2.0), 0.5f, 100 });
            const auto strong = suite::SongBus::hookScore (song.hook, 96.0, 9, 0), weak = suite::SongBus::hookScore (flat, 96.0, 9, 0);
            check (strong > weak + 0.2f, "a hook with range, a late peak and a resolution scores higher (" + juce::String (strong, 2) + ") than one note repeated (" + juce::String (weak, 2) + ")");
            for (int i = 0; i < 64; ++i) song.bass.push_back ({ 33, (float) (i * 4.0), 1.0f, 100 });
            for (int t = 0; t < suite::SongBus::numTypes; ++t) { song.chordCount[(size_t) t] = 4; song.chordRoots[(size_t) t] = { 0, 8, 3, 10, -1, -1, -1, -1 }; }
            suite::SongBus::publish (song);
            const auto got = suite::SongBus::read();
            check (got.valid && got.hook.size() == 16 && got.bass.size() == 64 && got.hook[6].pitch == 79 && std::abs (got.hookStartQ - 96.0) < 1.0e-6 && got.chordRoots[4][1] == 8,
                   "Correspondence's parts reach Ouroboros (the song bus)");
            auto in = goodIn; in.song = got;
            const auto withParts = lab::evaluate (in);
            bool writing = false;
            for (const auto& c : withParts.checks) writing = writing || (c.area == lab::aWriting && c.measured && c.name.contains ("hook"));
            check (writing && withParts.areaMeasured[(size_t) lab::aWriting], "with the parts, the writing checks run");
            suite::SongBus::withdraw();
        }

        // the bus: the score and the next step reach Mentalism's strip
        suite::LabelReadyBus::reset();
        check (suite::LabelReadyBus::overallLine().isEmpty(), "nothing shown before a score");
        suite::LabelReadyBus::publishOverall (73, "make the first drop hit harder");
        check (suite::LabelReadyBus::overallLine().contains ("73") && suite::LabelReadyBus::overallLine().contains ("first drop"), "Mentalism's strip line: " + suite::LabelReadyBus::overallLine());
        suite::LabelReadyBus::reset();

        // the host: the report, the hand-off pack, saved with the project
        {
            lab::LabelReadyHost host;
            host.report = bad;
            host.history = { { 1000, 40 }, { 70000, 55 } };
            const auto text = host.reportText (map);
            check (text.contains ("LABEL-READY REPORT") && text.contains ("Blocking:") && text.contains ("Low end"), "the report lists every area and what blocks it");
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroHandoff");
            dir.deleteRecursively(); dir.createDirectory();
            const auto pack = host.writeHandoff (dir, "Mix engineer", "Notes", "Plan", "Render plan", map);
            check (pack.getChildFile ("Label-ready report.txt").existsAsFile() && pack.getChildFile ("Known issues.txt").loadFileAsString().contains ("-")
                       && pack.getChildFile ("Re-render instructions.txt").loadFileAsString().contains ("Render plan"),
                   "the hand-off pack: the report, the known issues, the session notes, how to render it again");
            dir.deleteRecursively();
            lab::LabelReadyHost other;
            other.fromTree (host.toTree());
            check (other.history.size() == 2 && other.history[1].second == 55, "the score's history is saved with the project");
        }
    }


    section ("Label-ready 2: each track measured, chords heard, the new checks and the tools");
    {
        using T = suite::SectionType;
        const double fs = 44100.0;
        // the track meter: a pad ducked by a kick, coming back in about 120 ms
        {
            suite::TrackTraits::TraitsMeter m;
            m.prepare (fs);
            const auto n = (int) (fs * 4.0);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            const auto beat = (int) (fs * 0.5);
            for (int i = 0; i < n; ++i)
            {
                const auto inBeat = i % beat;
                const auto duck = inBeat < (int) (0.12 * fs) ? 0.15f + 0.85f * (float) inBeat / (float) (0.12 * fs) : 1.0f;
                const auto v = 0.3f * duck * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / fs);
                l[(size_t) i] = v + 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / fs);
                r[(size_t) i] = v - 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / fs);   // the 60 Hz is all side
            }
            for (int i = 0; i < n; i += 512)
            {
                m.input (l.data() + i, r.data() + i, juce::jmin (512, n - i));
                m.output (l.data() + i, r.data() + i, juce::jmin (512, n - i), (double) i / fs * 2.0, 120.0, i < n / 2 ? 0 : 1);
            }
            const auto t = m.snapshot (7, 4, "Pad");
            check (t.duckCount >= 5 && t.recoveryMs > 50.0f && t.recoveryMs < 200.0f, "sidechain recovery: ducked " + juce::String (t.duckCount) + " times, back in " + juce::String (juce::roundToInt (t.recoveryMs)) + " ms (made: 120)");
            check (t.lowSideDb > t.lowMidDb - 6.0f && t.lowSideDb > -40.0f, "stereo lows: the 60 Hz that's all side shows under 150 Hz (side " + juce::String (t.lowSideDb, 1) + " dB, middle " + juce::String (t.lowMidDb, 1) + " dB)");
            check (t.inPeakDb > -10.0f && t.inPeakDb < -6.0f && t.sectionDb[0] > -40.0f && t.sectionDb[1] > -40.0f && t.sectionDb[2] < -90.0f,
                   "the input's peak (" + juce::String (t.inPeakDb, 1) + " dB) and the level in each section");
        }
        // tails: hits that die away (tau 60 ms: 20 dB in about 140 ms)
        {
            suite::TrackTraits::TraitsMeter m;
            m.prepare (fs);
            const auto n = (int) (fs * 4.0);
            std::vector<float> x ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const auto k = i % (int) fs;
                x[(size_t) i] = 0.5f * (float) std::exp (-(double) k / (0.06 * fs)) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 400.0 * i / fs);
            }
            for (int i = 0; i < n; i += 512) m.output (x.data() + i, x.data() + i, juce::jmin (512, n - i), -1.0, 120.0, -1);
            const auto t = m.snapshot (8, 7, "FX");
            check (t.decayCount >= 3 && t.decayMs > 90.0f && t.decayMs < 200.0f, "reverb sizes: a hit's tail falls 20 dB in " + juce::String (juce::roundToInt (t.decayMs)) + " ms (made: about 140)");
        }
        // layers: the same part, upside down, and a little late
        {
            suite::TrackTraits::Traits a, b, c;
            a.id = 1; b.id = 2; c.id = 3; a.role = b.role = c.role = 5;
            juce::Random rng (3);
            std::vector<float> w ((size_t) suite::TrackTraits::ringSize + 8);
            for (auto& v : w) v = rng.nextFloat() * 2.0f - 1.0f;
            for (int i = 0; i < suite::TrackTraits::ringSize; ++i)
            {
                a.ring[(size_t) i] = w[(size_t) i + 4];
                b.ring[(size_t) i] = -w[(size_t) i + 4];
                c.ring[(size_t) i] = w[(size_t) i];   // c lags a by 4 samples (1 ms)
            }
            a.ringPos = b.ringPos = c.ringPos = 0;
            a.ringPpq = b.ringPpq = c.ringPpq = 10.0; a.ringBpm = b.ringBpm = c.ringBpm = 120.0;
            const auto flipped = suite::TrackTraits::layerPhase (a, b), late = suite::TrackTraits::layerPhase (a, c);
            check (flipped.valid && flipped.correlation < -0.95f, "layer phase: a layer upside down correlates " + juce::String (flipped.correlation, 2));
            check (late.valid && late.bestCorrelation > 0.95f && std::abs (std::abs (late.lagMs) - 1.0f) < 0.3f && late.correlation < 0.5f,
                   "and a layer 1 ms late is found (" + juce::String (late.lagMs, 2) + " ms, " + juce::String (late.correlation, 2) + " as it is)");
        }
        // chords from the audio: Am, F, C, G a bar each
        {
            const double bpm = 120.0;
            const auto barN = (int) (fs * 2.0);
            juce::AudioBuffer<float> audio (2, barN * 4);
            const int chordsMidi[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const auto bar = i / barN;
                float v = 0.0f;
                for (int k = 0; k < 3; ++k) v += 0.15f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * std::pow (2.0, (chordsMidi[bar][k] - 69) / 12.0) * i / fs);
                audio.setSample (0, i, v); audio.setSample (1, i, v);
            }
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LabChords.wav");
            wav.deleteFile();
            {
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), fs, 2, 24, {}, 0));
                if (w != nullptr) w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
            double seconds = 0.0;
            const auto bars = lab::analyseFile (wav, bpm, seconds);
            juce::StringArray roots;
            for (const auto& b : bars) roots.add (juce::String (lab::barRoot (b)));
            const auto prog = lab::progression (bars, 0, 4);
            const auto print = lab::progressionPrint (prog);
            check (prog.size() == 4 && prog[0] == 9 && prog[1] == 5 && prog[2] == 0 && prog[3] == 7, "the chords heard from the audio: A, F, C, G (" + roots.joinIntoString (" ") + ")");
            check (print == "8,7,7,2", "as a print that doesn't depend on the key: " + print);
            check (lab::progressionSimilarity ("8,7,7,2", "7,7,2,8") > 0.99f && lab::progressionSimilarity ("8,7,7,2", "5,2,5,0") < 0.5f, "the same loop starting elsewhere counts as the same");
            wav.deleteFile();
        }

        // the new checks, from what the tracks share
        suite::SectionMap map;
        const std::pair<T, int> form[] = { { T::Intro, 1 }, { T::Build, 17 }, { T::Drop, 25 }, { T::Breakdown, 41 }, { T::Build, 57 }, { T::Drop, 65 }, { T::Outro, 81 } };
        for (const auto& [type, bar] : form) { map.sections[(size_t) map.count].type = type; map.sections[(size_t) map.count].startBar = bar; ++map.count; }
        map.totalBars = 96;
        std::vector<lab::Bar> bars ((size_t) map.totalBars);
        for (int b = 0; b < map.totalBars; ++b)
        {
            int i = 0; for (int k = 0; k < map.count; ++k) if (map.sections[(size_t) k].startBar <= b + 1) i = k;
            const auto type = map.sections[(size_t) i].type;
            auto& x = bars[(size_t) b];
            x.heard = true; x.db = type == T::Drop ? -8.0f : type == T::Breakdown ? -18.0f : -14.0f; x.sideDb = x.db - 8.0f; x.onsets = 4.0f;
            for (int k = 0; k < lab::numBands; ++k) x.band[(size_t) k] = x.db - 3.0f * (float) std::abs (k - 3);
        }
        lab::Inputs in;
        in.map = map; in.bpm = 138.0; in.bars = bars; in.sections = lab::sections (bars, map);
        auto trackOf = [] (uint32_t id, int role, const char* name)
        {
            suite::TrackTraits::Traits t;
            t.id = id; t.role = role; juce::String (name).copyToUTF8 (t.track, sizeof (t.track) - 1);
            t.outRmsDb = -20.0f; t.sideDb = -35.0f; t.lowMidDb = -30.0f; t.lowSideDb = -60.0f; t.crestDb = 10.0f; t.inPeakDb = -8.0f; t.inRmsDb = -20.0f;
            for (auto& v : t.sectionDb) v = -100.0f;
            return t;
        };
        auto lead1 = trackOf (0x101, 5, "Lead A"), lead2 = trackOf (0x102, 5, "Lead B"), pad = trackOf (0x103, 4, "Pad"), hot = trackOf (0x104, 3, "Hats");
        juce::Random rng (9);
        for (int i = 0; i < suite::TrackTraits::ringSize; ++i) { const auto v = rng.nextFloat() - 0.5f; lead1.ring[(size_t) i] = v; lead2.ring[(size_t) i] = -v; }
        lead1.ringPpq = lead2.ringPpq = 100.0;
        pad.lowSideDb = -28.0f;                       // stereo lows on the pad
        hot.inPeakDb = 0.3f;                          // clipping into its chain
        lead1.sectionDb[2] = -20.0f; lead1.sectionDb[5] = -29.0f;   // the second drop's lead 9 dB down: automation left behind
        in.traits = { lead1, lead2, pad, hot };
        const auto rep = lab::evaluate (in);
        auto find = [&rep] (const juce::String& name) -> const lab::Check* { for (const auto& c : rep.checks) if (c.name == name) return &c; return nullptr; };
        const auto* phase = find ("Layers in phase");
        check (phase != nullptr && phase->measured && phase->score < 0.5f && phase->fix == suite::Fix::TrackPolarity && phase->fixData == 0x102u,
               "two leads cancelling: flip one (" + (phase != nullptr ? phase->detail : juce::String ("missing")) + ")");
        const auto* lows = find ("Stereo lows per track");
        check (lows != nullptr && lows->measured && lows->score < 0.5f && lows->detail.contains ("Pad") && lows->action.startsWith ("autopilot:"), "the pad's stereo lows, with the autopilot's mono lows as the fix");
        const auto* clip = find ("Clip gain");
        check (clip != nullptr && clip->measured && clip->score < 0.3f && clip->detail.contains ("Hats"), "clip gain: the hats come in clipping");
        const auto* leftover = find ("Automation leftovers");
        check (leftover != nullptr && leftover->measured && leftover->score < 0.5f && leftover->bar == 65, "an automation leftover: the lead 9 dB down in the second drop, at bar 65");
        const auto* tension = find ("Build tension at bar 17");
        check (tension != nullptr && tension->measured && tension->score < 0.5f, "a flat build has no tension (" + (tension != nullptr ? tension->detail : juce::String ("missing")) + ")");
        bool noTraitsUnmeasured = true;
        {
            auto bare = in; bare.traits.clear();
            for (const auto& c : lab::evaluate (bare).checks) if (c.name == "Sidechain recovery" || c.name == "Layers in phase") noTraitsUnmeasured = noTraitsUnmeasured && ! c.measured;
        }
        check (noTraitsUnmeasured, "without Quintessences those checks say so instead of guessing");
        // a label's must
        {
            lab::Profile strict = lab::genericProfile();
            strict.minScore["Clip gain"] = 0.8f;
            auto in2 = in; in2.profile = &strict;
            const auto r2 = lab::evaluate (in2);
            check (r2.blockers.joinIntoString (";").contains ("Clip gain") && r2.blockers.joinIntoString (";").contains ("minimum"), "a check this label insists on blocks the gate");
            const auto back = lab::Profile::fromTree (strict.toTree());
            check (back.minScore.count ("Clip gain") == 1 && std::abs (back.minScore.at ("Clip gain") - 0.8f) < 0.01f, "and is saved with the profile");
        }
        // one section only
        {
            auto in3 = in; in3.focusSection = 5;
            int counted = 0, outside = 0;
            for (const auto& c : lab::evaluate (in3).checks) if (c.measured) { ++counted; outside += (c.bar < 65 || c.bar >= 81) ? 1 : 0; }
            check (counted > 0 && outside == 0, "scoring one section counts only the checks inside it (" + juce::String (counted) + ")");
        }
        // writing intelligence: singability, the key change, chord freshness
        {
            auto in4 = in;
            in4.song.valid = true; in4.song.root = 9; in4.song.scale = 0; in4.song.hookStartQ = 96.0; in4.song.finalLift = 7;
            const int notes[] = { 69, 88, 70, 89, 71, 90, 69, 91 };   // leaps everywhere, no breath
            for (int i = 0; i < 8; ++i) in4.song.hook.push_back ({ notes[i], (float) (96.0 + i * 0.5), 0.5f, 100 });
            for (int i = 0; i < 16; ++i) in4.song.bass.push_back ({ 45, (float) (i * 4.0), 1.0f, 100 });
            in4.song.chordCount[(size_t) T::Drop] = 4; in4.song.chordRoots[(size_t) T::Drop] = { 0, 8, 3, 10, -1, -1, -1, -1 };
            in4.otherProgressions = { "8,7,7,2" };
            const auto r4 = lab::evaluate (in4);
            auto find4 = [&r4] (const juce::String& name) -> const lab::Check* { for (const auto& c : r4.checks) if (c.name == name) return &c; return nullptr; };
            const auto* sing = find4 ("Singable hook");
            check (sing != nullptr && sing->score < 0.5f, "a hook of big leaps with no breath isn't singable (" + (sing != nullptr ? sing->detail : juce::String ("missing")) + ")");
            const auto* key = find4 ("The key change fits every part");
            check (key != nullptr && key->score < 0.8f && key->action.startsWith ("lift:"), "a +7 lift pushes the hook too high: a smaller one suggested (" + (key != nullptr ? key->explain : juce::String()) + ")");
            const auto* fresh = find4 ("Chord freshness");
            check (fresh != nullptr && fresh->measured && fresh->score < 0.3f, "i-VI-III-VII again: the same progression as your last song (" + (fresh != nullptr ? fresh->detail : juce::String()) + ")");
        }

        // the host's tools
        {
            lab::LabelReadyHost host;
            host.report = rep;
            host.lastSections = in.sections;
            const auto q = host.listenQueue();
            bool ordered = ! q.empty();
            for (size_t i = 1; i < q.size(); ++i) ordered = ordered && q[i - 1].bar <= q[i].bar;
            check (ordered && host.listenNow (65).contains ("bar 65"), "the listen-through queue, in bar order; at bar 65 it says what to listen for");
            check (host.blindSpots().contains ("BLIND SPOTS") && host.blindSpots().contains ("generic trance"), "blind spots: what couldn't be measured and why");
            const auto impact = host.dropImpactOptions (map);
            check (impact.size() == 3 && impact[0].score >= impact[1].score && (impact[0].fix != suite::Fix::None || impact[0].action.isNotEmpty()),
                   "the drop-impact designer: three ways, ranked (" + (impact.empty() ? juce::String() : impact[0].name) + ")");
            host.logAction ("test change");
            host.report.overall = rep.overall + 5;
            host.update (map, 138.0, 4.0, nullptr);
            host.update (map, 138.0, 4.0, nullptr);
            check (host.scoreLog.size() == 1 && host.scoreLog[0].after >= 0 && host.scoreLogText().contains ("test change"), "the score log: each change with the score before and after");
            // the label's own tracks scored
            lab::Reference r;
            r.name = "Release"; r.bpm = 138.0; r.map = map; r.sections = in.sections; r.addedMs = juce::Time::currentTimeMillis();
            r.summarise();
            host.profile.refs = { r, r };
            host.profile.learn();
            const auto text = host.scoreReferences (true);
            check (text.contains ("Release") && host.profile.readyThreshold >= 50 && host.profile.readyThreshold <= 95, "the label's tracks scored, and the threshold set from them (" + juce::String (host.profile.readyThreshold) + ")");
            // A/B: switching on the bar, level-matched
            host.milestones.push_back ({ "one", "Milestone - one", 0, 70, {}, -10.0f });
            host.milestones.push_back ({ "two", "Milestone - two", 0, 75, {}, -13.0f });
            host.abStart (0, 1);
            host.abTick();
            const auto g1 = host.abGainDb.load();
            host.abToggle();
            host.abTick();
            const auto g2 = host.abGainDb.load();
            check (host.abShowing == 1 && std::abs (g1 + 3.0f) < 0.01f && std::abs (g2) < 0.01f, "milestone A/B: the louder one is turned down 3 dB to match; they switch on the bar");
            host.abStop();
            // saved with the project
            lab::LabelReadyHost other;
            other.fromTree (host.toTree());
            check (other.scoreLog.size() == 1 && std::abs (other.milestones[0].lufs + 10.0f) < 0.01f, "the score log and the milestones' loudness are saved with the project");
            // ear candy where nothing new happens
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroCandy");
            dir.deleteRecursively();
            lab::Accumulator acc;
            for (int b = 0; b < 32; ++b) { acc = {}; acc.samples = 1000; acc.full = 1000 * 0.01; for (auto& v : acc.band) v = 1000 * 0.001; host.live.add (b, acc); }
            const auto f = host.writeEarCandy (dir, map, 4.0);
            juce::MidiFile mf;
            juce::FileInputStream is (f);
            check (f.existsAsFile() && mf.readFrom (is) && host.earCandySpots >= 4 && mf.getTrack (0)->getNumEvents() >= 4 * 6,
                   "ear candy: one-shots in key wherever 4 bars go by with nothing new (" + juce::String (host.earCandySpots) + " spots)");
            dir.deleteRecursively();
        }
        // the processor: the key change, the drop entrance
        {
            auto o = std::make_unique<OuroborosProcessor>();
            o->setMap (map);
            lab::Check c; c.name = "key"; c.action = "lift:2";
            check (o->applyLabelCheck (c) && o->getMap().finalLift == 2, "one click sets the suggested final-drop key change");
            c.action = "entrance:2";
            check (o->applyLabelCheck (c) && o->getMap().dropEntrance == 2, "and the drop's entrance");
            suite::SectionBus::withdraw();
        }
    }


    section ("Label-ready 3: the sounds, arrangement detail, vocal, a simulated master, groove and harmony; the label autopilot");
    {
        using T = suite::SectionType;
        namespace AP = suite::Autopilot;
        const double fs = 44100.0;
        // the meter: a static tone against one that moves; a noise floor; even and uneven hits; the render
        {
            auto run = [fs] (bool moving, bool offline)
            {
                suite::TrackTraits::TraitsMeter m;
                m.prepare (fs);
                const auto n = (int) (fs * 6.0);
                std::vector<float> x ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    const auto t = (double) i / fs;
                    const auto bright = moving ? 0.5f + 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1.5 * t) : 0.5f;
                    x[(size_t) i] = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * t) + 0.2f * bright * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 5000.0 * t);
                }
                for (int i = 0; i < n; i += 512) m.output (x.data() + i, x.data() + i, juce::jmin (512, n - i), (double) i / fs * 2.0, 120.0, 0, offline);
                return m.snapshot (1, 4, "Pad");
            };
            const auto still = run (false, false), moving = run (true, false), rendered = run (false, true);
            check (still.movementCount > 20 && still.movementDb < 0.3f && moving.movementDb > still.movementDb + 1.0f,
                   "a pad that never changes reads " + juce::String (still.movementDb, 2) + " dB of movement, a moving one " + juce::String (moving.movementDb, 2));
            check (rendered.sectionDbOffline[0] > -40.0f && rendered.sectionDb[0] < -90.0f && still.sectionDbOffline[0] < -90.0f, "levels heard while FL renders are kept apart from playback");
            check (still.sectionPeakDb[0] > -12.0f && still.sectionPeakDb[0] < -6.0f, "each section's peak (" + juce::String (still.sectionPeakDb[0], 1) + " dB)");
            suite::TrackTraits::TraitsMeter m;
            m.prepare (fs);
            const auto n = (int) (fs * 6.0);
            std::vector<float> h ((size_t) n);
            juce::Random rng (4);
            for (int i = 0; i < n; ++i)
            {
                const auto k = i % (int) (fs / 4), hit = i / (int) (fs / 4);
                const auto amp = hit % 2 == 0 ? 0.6f : 0.15f;   // loud and soft notes in turn
                h[(size_t) i] = amp * (float) std::exp (-k / (0.03 * fs)) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / fs) + 0.003f * (rng.nextFloat() - 0.5f);
            }
            for (int i = 0; i < n; i += 512) m.output (h.data() + i, h.data() + i, juce::jmin (512, n - i), (double) i / fs * 2.0, 120.0, 0);
            const auto b = m.snapshot (2, 2, "Bass");
            check (b.hitCount >= 8 && b.hitSpreadDb > 9.0f, "notes 12 dB apart spread " + juce::String (b.hitSpreadDb, 1) + " dB");
            check (b.noiseFloorDb > -100.0f && b.noiseFloorDb < b.outRmsDb - 10.0f, "the quietest moments (" + juce::String (b.noiseFloorDb, 1) + " dB) under its level");
        }
        // the bars: peak and lean
        {
            lab::Analyser an; an.prepare (fs);
            lab::Accumulator acc;
            for (int i = 0; i < 44100; ++i) an.process (0.5f * std::sin (0.05f * i), 0.25f * std::sin (0.05f * i), acc);
            const auto bar = acc.finish();
            check (std::abs (bar.peakDb + 6.0f) < 0.3f && std::abs (bar.leanDb - 6.0f) < 0.3f, "each bar's peak (" + juce::String (bar.peakDb, 1) + ") and lean (" + juce::String (bar.leanDb, 1) + " dB left)");
        }

        // a song with problems everywhere
        suite::SectionMap map;
        const std::pair<T, int> form[] = { { T::Intro, 1 }, { T::Build, 17 }, { T::Drop, 25 }, { T::Breakdown, 41 }, { T::Build, 57 }, { T::Drop, 65 }, { T::Outro, 81 } };
        for (const auto& [type, bar] : form) { map.sections[(size_t) map.count].type = type; map.sections[(size_t) map.count].startBar = bar; ++map.count; }
        map.totalBars = 96;
        std::vector<lab::Bar> bars ((size_t) map.totalBars);
        for (int b = 0; b < map.totalBars; ++b)
        {
            auto& x = bars[(size_t) b];
            x.heard = true; x.db = -12.0f; x.sideDb = -20.0f; x.onsets = 4.0f; x.peakDb = b >= 24 && b < 40 ? -5.0f : -9.0f; x.leanDb = b >= 40 && b < 56 ? 2.5f : 0.0f;
            for (int k = 0; k < lab::numBands; ++k) x.band[(size_t) k] = x.db - 3.0f * (float) std::abs (k - 3);
        }
        lab::Inputs in;
        in.map = map; in.bpm = 138.0; in.bars = bars; in.sections = lab::sections (bars, map);
        auto track = [] (uint32_t id, int role, const char* name)
        {
            suite::TrackTraits::Traits t;
            t.id = id; t.role = role; juce::String (name).copyToUTF8 (t.track, sizeof (t.track) - 1);
            t.outRmsDb = -20.0f; t.sideDb = -35.0f; t.lowMidDb = -30.0f; t.lowSideDb = -60.0f; t.crestDb = 10.0f; t.inPeakDb = -8.0f; t.inRmsDb = -20.0f;
            for (auto& v : t.sectionDb) v = -100.0f;
            for (auto& v : t.sectionPeakDb) v = -100.0f;
            for (auto& v : t.sectionDbOffline) v = -100.0f;
            return t;
        };
        auto pad = track (0x201, 4, "Pad"), lead = track (0x202, 5, "Lead"), kick = track (0x203, 1, "Kick"), hats = track (0x204, 3, "Hats"), vox = track (0x205, 6, "Vox");
        pad.movementCount = 40; pad.movementDb = 0.1f;
        kick.sectionPeakDb[2] = -5.5f;   // the kick makes the loudest peak in the drop
        pad.sectionDb[2] = -20.0f; pad.sectionDbOffline[2] = -17.0f;   // louder in the render
        hats.inRmsDb = -42.0f;
        vox.sectionDb[2] = -14.0f; vox.sectionDb[3] = -24.0f;   // the vocal sinks in the breakdown
        in.traits = { pad, lead, kick, hats, vox };
        auto bandsOf = [] (std::initializer_list<float> v) { lab::Bands b {}; int k = 0; for (auto x : v) b[(size_t) k++] = x; return b; };
        in.tracks = { { 1, "Kick", -10.0f, bandsOf ({ -10, -6, -8, -14, -20, -26, -40, -42, -50, -60 }) },          // no click
                      { 3, "Hats", -20.0f, bandsOf ({ -80, -70, -60, -50, -40, -30, -20, -12, -30, -40 }) },        // 4 kHz bite, no air
                      { 6, "Vox", -18.0f, bandsOf ({ -80, -50, -30, -22, -20, -18, -24, -26, -16, -40 }) },         // sibilant, buried at 2-4 kHz
                      { 4, "Pad", -16.0f, bandsOf ({ -60, -30, -20, -16, -14, -14, -12, -12, -30, -40 }) } };
        in.entries.push_back ({}); in.entries.back().plugin = "Antioch"; in.entries.back().track = "Return"; in.entries.back().flags = suite::Status::idle;
        in.entries.push_back ({}); in.entries.back().plugin = "Antioch"; in.entries.back().track = "Return 2"; in.entries.back().flags = suite::Status::idle;
        in.song.valid = true; in.song.root = 9; in.song.scale = 0; in.song.quartersPerChord = 4.0;
        for (int i = 0; i < 32; ++i) in.song.bass.push_back ({ i % 4 == 0 ? 44 : 45, (float) (96.0 + i * 0.5), 1.5f, i % 3 == 0 ? 20 : 100 });   // long, a note out of key, flat accents, quiet ghosts
        for (int t = 0; t < suite::SongBus::numTypes; ++t) { in.song.chordCount[(size_t) t] = 4; in.song.chordRoots[(size_t) t] = { 0, 8, 3, 10, -1, -1, -1, -1 }; }
        for (int i = 0; i < 12; ++i) { in.vocal.push_back ({ 69, (float) (i * 2.0 - 0.06), 1.0f, 100 }); in.vocalCents.push_back (i % 3 == 0 ? 45.0f : 5.0f); }   // early, a third out of tune
        in.swingMs = { { 3, 2.0f }, { 4, 18.0f } };
        in.flamMs = 9.0f;
        const auto rep = lab::evaluate (in);
        auto find = [&rep] (const juce::String& name) -> const lab::Check* { for (const auto& c : rep.checks) if (c.name == name) return &c; return nullptr; };
        auto flagged = [&] (const juce::String& name, AP::Feature f)
        {
            const auto* c = find (name);
            check (c != nullptr && c->measured && c->score < 0.75f && c->action == "autopilot:" + juce::String ((int) f),
                   name + " flagged, with its switch (" + (c == nullptr ? juce::String ("missing") : c->detail) + ")");
        };
        flagged ("Kick quality", AP::kickShapeF);
        flagged ("Pads that move", AP::padMotionF);
        flagged ("Bass notes stop before the kick", AP::bassClearKickF);
        flagged ("The bass stays in key", AP::bassInKeyF);
        flagged ("Accents line up", AP::accentsF);
        flagged ("Ghost notes heard", AP::ghostLiftF);
        flagged ("Vocal timing", AP::vocalTimingF);
        flagged ("The vocal is clear", AP::vocalClarityF);
        flagged ("Sibilance", AP::sibilanceF);
        flagged ("The vocal stays steady", AP::vocalSteadyF);
        flagged ("Attacks that bite", AP::harshAttackF);
        flagged ("The loudest peak", AP::peakTrimF);
        flagged ("Balanced left and right", AP::balanceF);
        flagged ("Nothing loaded doing nothing", AP::idleSleepF);
        flagged ("Gain staging into the plugins", AP::inputStageF);
        flagged ("One swing", AP::tightSwingF);
        flagged ("Kick and bass start together", AP::layerAlignF);
        flagged ("No 8 bars repeated too often", AP::loopVariationF);
        flagged ("Phrase ends change something", AP::phraseFillsF);
        {
            const auto* tune = find ("Vocal tuning");
            check (tune != nullptr && tune->score < 0.5f && tune->explain.contains ("pitch editor"), "vocal tuning: the notes listed (no pitch correction in the suite: said so)");
            const auto* peak = find ("The loudest peak");
            check (peak != nullptr && peak->fix == suite::Fix::SectionTrim && peak->detail.contains ("Kick"), "the loudest peak names the kick and trims it there (" + (peak != nullptr ? peak->detail : juce::String()) + ")");
            const auto* render = find ("The render sounds like playback");
            check (render != nullptr && render->measured && render->score < 0.5f && render->detail.contains ("Pad"), "the render against playback names the pad (+3 dB rendered)");
            const auto* timing = find ("Vocal timing");
            check (timing != nullptr && timing->fix == suite::Fix::VocalDelay, "an early vocal: delayed onto the grid by its Quintessence");
        }

        // the autopilot: what the plugins are told, and Ouroboros' own fixes tried and kept or undone
        {
            const auto needs = lab::AutoFixer::needsFrom (rep);
            auto has = [&needs] (AP::Feature f) { return (needs[(size_t) f / 32] & (1u << (f % 32))) != 0; };
            check (has (AP::padMotionF) && has (AP::bassClearKickF) && has (AP::idleSleepF), "the plugins are told which of their fixes are needed");
            suite::LabelReadyBus::publishNeeds (needs);
            check (suite::LabelReadyBus::needed ((int) AP::padMotionF) && ! suite::LabelReadyBus::needed ((int) AP::stressFixF) == ! has (AP::stressFixF), "through the shared bus");
            suite::FixBus::reset();
            AP::reset();
            lab::AutoFixer fixer;
            fixer.step (rep, in.traits, true);
            const auto first = suite::FixBus::latest();
            check (first > 0, "the autopilot sends one fix (" + fixer.note + ")");
            for (int k = 0; k < 4; ++k) fixer.step (rep, in.traits, true);   // the same score: no better
            check (fixer.undone == 1 && suite::FixBus::latest() > first, "no better after listening: undone (" + fixer.note + ")");
            AP::set (AP::kickShapeF, false); AP::set (AP::leadTimbreF, false);
            lab::AutoFixer quiet;
            for (int f = AP::kickShapeF; f < AP::numFeatures; ++f) AP::set ((AP::Feature) f, false);
            const auto before = suite::FixBus::latest();
            quiet.step (rep, in.traits, true);
            check (suite::FixBus::latest() == before, "switched off, nothing is sent");
            AP::reset();
            suite::FixBus::reset();
        }

        // deciding and listening
        {
            lab::LabelReadyHost host;
            host.report = rep;
            host.lastSections = in.sections;
            host.lastInputs = in;
            host.lastInputs.profile = nullptr;
            check (host.sessionPlan().contains ("1. ") && host.sessionPlan().contains ("points"), "today's plan: the top changes, with the points each is worth");
            check (host.whyNot100().contains ("points missing"), "why not 100, area by area");
            check (host.arSummary().contains ("A&R NOTES") && host.arSummary().contains ("Verdict"), "A&R notes in plain words");
            const auto* worst = find ("Kick quality");
            check (worst != nullptr && host.predictedGain (*worst) > 0.1f, "a check knows what fixing it is worth (+" + juce::String (worst != nullptr ? host.predictedGain (*worst) : 0.0f, 1) + ")");
            lab::Reference mine;
            mine.name = "My signed one"; mine.yours = true; mine.bpm = 138.0; mine.sections = in.sections; mine.addedMs = juce::Time::currentTimeMillis(); mine.summarise();
            host.profile.refs = { mine };
            host.profile.learn();
            check (host.overlayBestSigned().contains ("My signed one") && host.scoreByReference().contains ("My signed one"), "against your best signed track, and against each reference");
            host.requireSignOff = true;
            host.report.ready = true;
            host.update (map, 138.0, 4.0, nullptr);
            check (! host.report.ready, "with sign-off required, the gate waits for it");
            lab::LabelReadyHost back;
            host.signOff[lab::aAR] = "today";
            back.fromTree (host.toTree());
            check (back.requireSignOff && back.signOff.count (lab::aAR) == 1, "sign-offs are saved with the project");
            // before and after
            lab::Snippets sn;
            sn.prepare (8000.0);
            std::vector<float> loud (8000, 0.5f), quietB (8000, 0.1f);
            for (int k = 0; k < 10; ++k) sn.push (loud.data(), loud.data(), 8000);
            sn.markFix ("test", 8.0, 4.0);
            for (int k = 0; k < 10; ++k) sn.push (quietB.data(), quietB.data(), 8000);
            sn.tick (true, 100.0, 4.0);   // the bar never comes back...
            check (sn.pairs().empty(), "the after waits for the same bar");
        }
    }


    section ("Label-ready 4: tone and width per section, the low end over time, the arrangement's edges, learning, listening");
    {
        using T = suite::SectionType;
        namespace AP = suite::Autopilot;
        AP::reset();
        suite::SectionMap map;
        const std::pair<T, int> form[] = { { T::Intro, 1 }, { T::Build, 17 }, { T::Drop, 25 }, { T::Breakdown, 41 }, { T::Build, 57 }, { T::Drop, 65 }, { T::Outro, 81 } };
        for (const auto& [type, bar] : form) { map.sections[(size_t) map.count].type = type; map.sections[(size_t) map.count].startBar = bar; ++map.count; }
        map.totalBars = 96;
        // a flat song: same everywhere, low end cancelling in the drops, a riser brighter than the drop
        std::vector<lab::Bar> bars ((size_t) map.totalBars);
        for (int b = 0; b < map.totalBars; ++b)
        {
            auto& x = bars[(size_t) b];
            x.heard = true; x.db = -12.0f; x.sideDb = -16.0f; x.onsets = 4.0f; x.peakDb = -10.0f;
            x.lowCorrCount = 1; x.lowCorr = (b >= 24 && b < 40) ? 0.2f : 0.95f;
            for (int k = 0; k < lab::numBands; ++k) x.band[(size_t) k] = x.db - 3.0f * (float) std::abs (k - 3) + (k >= 8 && b == 23 ? 8.0f : 0.0f);
        }
        lab::Inputs in;
        in.map = map; in.bpm = 138.0; in.bars = bars; in.sections = lab::sections (bars, map);
        auto track = [] (uint32_t id, int role, const char* name)
        {
            suite::TrackTraits::Traits t;
            t.id = id; t.role = role; juce::String (name).copyToUTF8 (t.track, sizeof (t.track) - 1);
            t.outRmsDb = -20.0f; t.sideDb = -35.0f; t.lowMidDb = -30.0f; t.lowSideDb = -60.0f; t.crestDb = 10.0f; t.inPeakDb = -8.0f; t.inRmsDb = -20.0f;
            for (auto& v : t.sectionDb) v = -20.0f;
            for (auto& v : t.sectionPeakDb) v = -15.0f;
            for (auto& v : t.sectionDbOffline) v = -100.0f;
            return t;
        };
        auto kick = track (0x301, 1, "Kick"), bass = track (0x302, 2, "Bass"), hats = track (0x303, 3, "Hats"), lead = track (0x304, 5, "Lead"), fx = track (0x305, 7, "Riser");
        kick.decayCount = 6; kick.decayMs = 250.0f;          // rings well over a beat
        bass.subsonicDb = -25.0f;                            // energy under 20 Hz
        lead.movementCount = 40; lead.movementDb = 0.2f;     // never changes
        lead.crestDb = 22.0f;                                // clean, no edge
        hats.crestDb = 22.0f;                                // much spikier than the kick
        fx.decayCount = 5; fx.decayMs = 400.0f;
        kick.cpuPercent = 30.0f; bass.cpuPercent = 30.0f;
        in.traits = { kick, bass, hats, lead, fx };
        auto bandsOf = [] (std::initializer_list<float> v) { lab::Bands b {}; int k = 0; for (auto x : v) b[(size_t) k++] = x; return b; };
        in.tracks = { { 2, "Bass", -12.0f, bandsOf ({ -30, -10, -14, -20, -30, -40, -50, -60, -70, -80 }) },
                      { 5, "Lead", -16.0f, bandsOf ({ -80, -60, -30, -20, -14, -16, -30, -34, -40, -50 }) } };
        const auto rep = lab::evaluate (in);
        auto find = [&rep] (const juce::String& name) -> const lab::Check* { for (const auto& c : rep.checks) if (c.name == name) return &c; return nullptr; };
        auto flagged = [&] (const juce::String& name, AP::Feature f)
        {
            const auto* c = find (name);
            check (c != nullptr && c->measured && c->score < 0.75f && c->action == "autopilot:" + juce::String ((int) f),
                   name + " flagged, with its switch (" + (c == nullptr ? juce::String ("missing") : c->detail) + ")");
        };
        flagged ("Kick tail in the drops", AP::kickTailF);
        flagged ("The sub hands over into the drop", AP::subHandoverF);
        flagged ("Sub phase bar by bar", AP::subPhaseF);
        flagged ("Sub under the bass", AP::subFillF);
        flagged ("No subsonics", AP::subsonicF);
        flagged ("The riser peaks under the drop", AP::riserTopF);
        flagged ("Tails cleared before the drop", AP::chokeF);
        flagged ("Section openings and endings", AP::sectionEdgesF);
        flagged ("The breakdown has a bed", AP::breakdownBedF);
        flagged ("Parts that move", AP::motionF);
        flagged ("The lead cuts through on its own", AP::driveF);
        flagged ("Drum attacks against each other", AP::clapTransientF);
        flagged ("Every phrase breathes", AP::phraseDynamicsF);
        {
            const auto* cpu = find ("The suite's CPU");
            check (cpu != nullptr && cpu->measured && cpu->score < 0.75f && cpu->action.isEmpty(), "the suite's CPU measured (no automatic fix: said so)");
            check (rep.coverage > 0.99f, "the report says how much of the song was heard (" + juce::String (rep.coverage * 100.0f, 0) + "%)");
        }

        // the autopilot: journey and width go to every track
        {
            suite::FixBus::reset();
            lab::AutoFixer fixer;
            lab::Check c; c.name = "The loudness journey"; c.measured = true; c.score = 0.3f; c.fix = suite::Fix::SectionTrim;
            c.fixData = suite::packSectionTrim (0, (int) T::Breakdown, -1.0f); c.action = "autopilot:" + juce::String ((int) AP::journeyF);
            const auto seqs = fixer.apply (c, in.traits);
            check (seqs.size() == in.traits.size(), "the loudness journey: a step on every track (" + juce::String ((int) seqs.size()) + ")");
            c.name = "Width section by section"; c.fix = suite::Fix::SectionWidth; c.action = "autopilot:" + juce::String ((int) AP::sectionWidthF);
            check (fixer.apply (c, in.traits).size() == in.traits.size() - 2, "width: every track but the kick and bass");
            suite::FixBus::reset();
        }
        // learning: undone three times and never kept, left alone; the steps smaller each time
        {
            lab::Learning::fileForTests().deleteFile();
            lab::Learning l;
            check (std::abs (l.stepScale ("Kick quality") - 1.0f) < 1.0e-4f && ! l.retired ("Kick quality"), "nothing learned yet: full steps");
            l.record ("Kick quality", false);
            const auto once = l.stepScale ("Kick quality");
            l.record ("Kick quality", false); l.record ("Kick quality", false);
            check (once < 1.0f && l.retired ("Kick quality"), "undone three times: smaller steps, then left alone");
            l.record ("Build tension at bar 17", true); l.record ("Build tension at bar 57", true);
            lab::Learning again;
            check (again.favoured ("Build tension at bar 99") && again.retired ("Kick quality"), "kept twice (any bar): tried first; remembered across songs");
            lab::Learning::fileForTests().deleteFile();
        }
        // listening: previews change the sound only when listening, never in a render
        {
            lab::Monitor mon;
            mon.prepare (44100.0);
            juce::AudioBuffer<float> b (2, 512);
            for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.5f); b.setSample (1, i, -0.5f); }
            mon.preview.store (lab::Monitor::mono);
            auto r = b;
            mon.process (r, false, 0.0, 4.0, 4, 0.0, -10.0f, true);
            check (std::abs (r.getSample (0, 100)) < 1.0e-6f && std::abs (r.getSample (1, 100)) < 1.0e-6f, "the mono preview sums left and right (here they cancel)");
            auto off = b;
            mon.process (off, true, 0.0, 4.0, 4, 0.0, -10.0f, true);
            check (std::abs (off.getSample (0, 100) - 0.5f) < 1.0e-6f && mon.rendering.load(), "while FL renders, the previews stay off");
        }
        // the host: stages, the structure proposal, the heat map, the maps, ear candy, the render check, steadiness, the regression guard
        {
            lab::LabelReadyHost host;
            host.report = rep;
            host.lastSections = in.sections;
            host.lastTraits = in.traits;
            check (lab::LabelReadyHost::stageOf ("A&R mix v2") == 2 && lab::LabelReadyHost::stageOf ("sketch") == 0, "a milestone's stage from its name");
            check (host.stageDone (0).isNotEmpty() && host.stageDone (3).contains ("gate"), "what a stage still needs (" + host.stageDone (3).substring (0, 60) + ")");
            const auto scores = host.sectionScores (map);
            bool some = false; for (auto v : scores) some = some || v >= 0.0f;
            check ((int) scores.size() == map.count && some, "the heat map: a score per section");
            check (host.depthMap().contains ("FRONT TO BACK") && host.depthMap().contains ("Lead"), "front to back, track by track");
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OuroCandyShots");
            dir.deleteRecursively();
            check (host.writeEarCandyShots (dir) && dir.getChildFile ("Candy pluck.wav").existsAsFile() && dir.getChildFile ("Candy riser.wav").existsAsFile(), "ear-candy one-shots written in the key");
            dir.deleteRecursively();
            check (host.preRenderReport (true).contains ("A/B"), "when FL renders: what's in the way (" + host.preRenderReport (true).substring (0, 60) + ")");
            host.playthroughs = { 70, 74, 72 };
            check (std::abs (host.scoreSpread() - 4.0f) < 1.0e-4f, "how steady the score is between play-throughs");
            lab::Profile p = lab::genericProfile();
            lab::Reference r; r.name = "Release"; r.bpm = 138.0; r.map = map; r.sections = in.sections; r.addedMs = juce::Time::currentTimeMillis(); r.summarise();
            host.profile.refs = { r }; host.profile.learn();
            check (host.proposeMap().count == map.count, "the label's structure proposed from its closest release");
            // the regression guard: a signed-off area falling after a change
            suite::FixBus::reset();
            AP::reset();
            host.report.areaScore[(size_t) lab::aLowEnd] = 0.9f; host.report.areaMeasured[(size_t) lab::aLowEnd] = true;
            host.signOffArea (lab::aLowEnd, true);
            suite::FixBus::send (suite::Fix::LessSquash);
            host.logAction ("test change");
            const auto before = suite::FixBus::latest();
            host.update (map, 138.0, 4.0, nullptr);   // the live report: the area unmeasured or lower
            check (host.signOffScore.count (lab::aLowEnd) == 1, "the sign-off keeps the area's score");
            juce::ignoreUnused (before);
            suite::FixBus::reset();
        }
        AP::reset();
    }

    return finish();
}
