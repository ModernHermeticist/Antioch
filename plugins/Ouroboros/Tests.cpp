// Offline tests for Ouroboros and the SectionBus. Usage: OuroborosTests [output-dir]

#include "PluginProcessor.h"
#include "TestUtils.h"

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

        // A follower in 3/4 uses its own bar length.
        check (f.locate (8.0 * 3.0, 3.0).type == SectionType::Build, "bar maths follows the host time signature");

        suite::SectionBus::withdraw();
        suite::SectionMap after;
        check (! suite::SectionBus::read (after), "withdraw empties the bus");
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
    }

    return finish();
}
