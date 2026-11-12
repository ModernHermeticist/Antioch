// Offline tests for Ouroboros and the SectionBus. Usage: OuroborosTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AutopilotBus.h"
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
        check (plan.contains ("1. Original mix") && plan.contains ("2. Radio edit") && plan.contains ("-14 LUFS") && plan.contains ("4. Club edit"),
               "after Make versions: four renders, with their loudness");

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

    return finish();
}
