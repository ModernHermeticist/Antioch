// Offline tests for Prima Materia. Usage: PrimaMateriaTests [output-dir]

#include "PluginProcessor.h"
#include "ActivityBus.h"
#include "TestUtils.h"
#include "StyleLog.h"
#include "Recipes.h"
#include "Registry.h"
#include "KickBus.h"
#include "FixBus.h"
#include "AutopilotBus.h"

using namespace suite::test;
using T = suite::SectionType;

namespace
{
    // 120 BPM, 2 s per bar: Groove 1-2 (0-4 s), Breakdown 3-4 (4-8 s), Build 5-6 (8-12 s), Drop 7-8 (12-16 s).
    suite::SectionMap testMap()
    {
        suite::SectionMap m;
        m.totalBars = 8; m.count = 4;
        m.sections[0] = { 1, T::Groove };
        m.sections[1] = { 3, T::Breakdown };
        m.sections[2] = { 5, T::Build };
        m.sections[3] = { 7, T::Drop };
        return m;
    }

    InputFn tone (float hz)
    {
        return [hz] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
    }

    juce::AudioBuffer<float> play (PrimaMateriaProcessor& p, InputFn input, double seconds = 16.0)
    {
        suite::SectionBus::publish (testMap());
        FakePlayHead head;
        head.bpm = 120.0;
        p.setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (p, seconds, input, &audio, &head);
        p.setPlayHead (nullptr);
        return audio;
    }

    std::unique_ptr<PrimaMateriaProcessor> make (int role)
    {
        auto p = std::make_unique<PrimaMateriaProcessor>();
        p->applyRole (role);
        prepare (*p);
        return p;
    }

    int roleIndex (const char* name)
    {
        for (size_t i = 0; i < pm::roles().size(); ++i)
            if (juce::String (pm::roles()[i].name).startsWith (name)) return (int) i;
        return 0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Kick role: out in the breakdown and build, back for the drop");
    {
        auto p = make (roleIndex ("Kick"));
        const auto a = play (*p, tone (60.0f));
        check (rmsBetween (a, 1.0, 3.5) > 0.3f, "plays in the groove");
        check (rmsBetween (a, 4.3, 11.7) < 1.0e-3f, "silent through breakdown and build");
        check (rmsBetween (a, 12.3, 15.5) > 0.3f, "back in the drop");
    }

    section ("Bass role: fades back in thin through the build, gap, full at the drop");
    {
        auto p = make (roleIndex ("Bass"));
        const auto a = play (*p, tone (80.0f));
        const auto early = rmsBetween (a, 8.2, 8.8), late = rmsBetween (a, 11.0, 11.4);
        check (rmsBetween (a, 4.3, 7.7) < 1.0e-3f, "out in the breakdown");
        const auto drop = rmsBetween (a, 12.3, 15.5);
        check (late > 1.5f * early, "fades in across the build (" + juce::String (early, 4) + " -> " + juce::String (late, 4) + ")");
        check (late < 0.1f * drop, "but stays thin (400 Hz low cut) until the drop");
        check (rmsBetween (a, 11.55, 11.95) < 1.0e-3f, "a beat of silence before the drop");
        check (rmsBetween (a, 12.3, 15.5) > 0.3f, "full in the drop");
    }

    section ("Pads role: high cut and level per section");
    {
        auto p = make (roleIndex ("Pads"));
        const auto a = play (*p, tone (440.0f));
        const auto breakdown = rmsBetween (a, 5.0, 7.5), drop = rmsBetween (a, 13.0, 15.5);
        const auto diff = juce::Decibels::gainToDecibels (breakdown / drop);
        check (std::abs (diff - 4.0f) < 0.5f, "breakdown +2 dB vs drop -2 dB (" + juce::String (diff, 2) + " dB apart)");
    }

    section ("Morph glides, instant switches");
    {
        auto p = make (roleIndex ("Kick"));
        set (*p, pm::morphId, 3.0f);   // 1 bar glide
        const auto a = play (*p, tone (60.0f), 6.0);
        check (rmsBetween (a, 4.05, 4.25) > 0.05f, "with a 1-bar Morph the kick fades out rather than cutting");
        auto q = make (roleIndex ("Kick"));
        set (*q, pm::morphId, 0.0f);
        const auto b = play (*q, tone (60.0f), 6.0);
        check (rmsBetween (b, 4.02, 4.25) < 1.0e-3f, "Instant cuts on the bar line");
    }

    section ("Drop 2+ column");
    {
        suite::SectionMap twoDrops;   // Drop 1-2 (0-4 s), Breakdown 3-4, Drop 5-6 (8-12 s)
        twoDrops.totalBars = 6; twoDrops.count = 3;
        twoDrops.sections[0] = { 1, T::Drop };
        twoDrops.sections[1] = { 3, T::Breakdown };
        twoDrops.sections[2] = { 5, T::Drop };
        auto p = make (roleIndex ("Lead"));   // lead: 0 dB in drops, +2 dB in Drop 2+
        suite::SectionBus::publish (twoDrops);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> a;
        render (*p, 12.0, tone (440.0f), &a, &head);
        p->setPlayHead (nullptr);
        const auto diff = juce::Decibels::gainToDecibels (rmsBetween (a, 9.0, 11.5) / rmsBetween (a, 1.0, 3.5));
        check (std::abs (diff - 2.0f) < 0.3f, "the lead is 2 dB louder in the second drop (" + juce::String (diff, 2) + " dB)");
    }

    section ("Fix-it buttons from Mentalism");
    {
        suite::FixBus::reset();
        auto lvl = [] (PrimaMateriaProcessor& p, int column) { return p.apvts.getRawParameterValue (pm::levelId (column))->load(); };
        auto pads = make (roleIndex ("Pads"));
        auto kick = make (roleIndex ("Kick"));   // breakdown muted: stays muted
        const auto padsBefore = lvl (*pads, (int) T::Breakdown);

        const auto seq = suite::FixBus::send (suite::Fix::BreakdownQuieter);
        pads->fixes->poll();
        kick->fixes->poll();
        check (std::abs (lvl (*pads, (int) T::Breakdown) - (padsBefore - 2.0f)) < 0.05f, "breakdown 2 dB quieter ("
                   + juce::String (padsBefore, 1) + " -> " + juce::String (lvl (*pads, (int) T::Breakdown), 1) + " dB)");
        check (lvl (*kick, (int) T::Breakdown) <= pm::offDb + 0.5f, "a muted breakdown stays muted");
        check (suite::FixBus::acknowledgements (seq) == 1, "one plugin acknowledges it");

        pads->fixes->poll();
        check (std::abs (lvl (*pads, (int) T::Breakdown) - (padsBefore - 2.0f)) < 0.05f, "each fix is applied once");

        const auto undo = suite::FixBus::sendUndo (seq);
        pads->fixes->poll();
        kick->fixes->poll();
        check (std::abs (lvl (*pads, (int) T::Breakdown) - padsBefore) < 0.05f && suite::FixBus::acknowledgements (undo) == 1, "undo restores it");

        // Undo history: two fixes in a row, undone in any order.
        {
            const auto base = lvl (*pads, (int) T::Breakdown);
            const auto first = suite::FixBus::send (suite::Fix::BreakdownQuieter);
            pads->fixes->poll();
            const auto second = suite::FixBus::send (suite::Fix::BreakdownQuieter);
            pads->fixes->poll();
            check (std::abs (lvl (*pads, (int) T::Breakdown) - (base - 4.0f)) < 0.05f, "two fixes: 4 dB quieter");
            suite::FixBus::sendUndo (first);
            pads->fixes->poll();
            check (std::abs (lvl (*pads, (int) T::Breakdown) - (base - 4.0f)) < 0.05f,
                   "undoing the older fix leaves a parameter the newer one changed since");
            suite::FixBus::sendUndo (second);
            pads->fixes->poll();
            check (std::abs (lvl (*pads, (int) T::Breakdown) - (base - 2.0f)) < 0.05f, "undoing the newer one steps back to -2 dB");
            const auto third = suite::FixBus::send (suite::Fix::IntroQuieter);
            pads->fixes->poll();
            const auto intro = lvl (*pads, (int) T::Intro);
            const auto fourth = suite::FixBus::send (suite::Fix::BreakdownLouder);
            pads->fixes->poll();
            suite::FixBus::sendUndo (third);   // an older fix on a different parameter: undone
            pads->fixes->poll();
            check (std::abs (lvl (*pads, (int) T::Intro) - (intro + 2.0f)) < 0.05f && std::abs (lvl (*pads, (int) T::Breakdown) - base) < 0.05f,
                   "an older fix can be undone on its own when nothing touched it since");
            suite::FixBus::sendUndo (fourth);
            pads->fixes->poll();
        }

        // DJ-friendly fix: melodic roles leave the intro and outro, the kick stays.
        {
            auto pad = make (roleIndex ("Pads")), kickTrack = make (roleIndex ("Kick"));
            suite::FixBus::send (suite::Fix::DjIntroOutro);
            pad->fixes->poll(); kickTrack->fixes->poll();
            check (lvl (*pad, (int) T::Intro) <= pm::offDb + 0.5f && lvl (*pad, (int) T::Outro) <= pm::offDb + 0.5f
                       && lvl (*kickTrack, (int) T::Intro) > pm::offDb + 0.5f, "DJ fix: the pads leave the intro and outro; the kick stays");
        }

        // Masking fix: only the addressed track carves, in the asked band; again = deeper.
        {
            auto padTrack = make (roleIndex ("Pads")), other = make (roleIndex ("Lead"));
            const auto band = 6;   // 2 kHz
            suite::FixBus::send (suite::Fix::Carve, suite::packCarve (padTrack->getInstanceId(), band));
            padTrack->fixes->poll(); other->fixes->poll();
            auto carveOf = [] (PrimaMateriaProcessor& p) { return (int) p.apvts.getRawParameterValue (pm::carveBandId)->load() - 1; };
            check (carveOf (*padTrack) == band && std::abs (padTrack->apvts.getRawParameterValue (pm::carveDepthId)->load() + 3.0f) < 0.01f
                       && carveOf (*other) == -1, "carve: the addressed pad gets a 3 dB dip at 2 kHz, the lead is untouched");
            suite::FixBus::send (suite::Fix::Carve, suite::packCarve (padTrack->getInstanceId(), band));
            padTrack->fixes->poll();
            check (std::abs (padTrack->apvts.getRawParameterValue (pm::carveDepthId)->load() + 4.5f) < 0.01f, "asking again carves deeper (4.5 dB)");
            set (*padTrack, pm::followId, 0.0f);
            const auto at2k = [&padTrack] (float hz)
            {
                juce::AudioBuffer<float> a;
                render (*padTrack, 0.5, tone (hz), &a);
                return juce::Decibels::gainToDecibels (rmsBetween (a, 0.2, 0.5));
            };
            const auto dip = at2k (2000.0f) - at2k (250.0f);
            check (dip < -3.5f && dip > -5.5f, "and you hear it: 2 kHz down " + juce::String (-dip, 1) + " dB against 250 Hz");
        }

        // Later drops: turns on the Drop 2+ column, starting from the drop's settings.
        auto lead = make (roleIndex ("Pads"));
        const auto drop = lvl (*lead, (int) T::Drop);
        set (*lead, pm::laterOnId, 0.0f);
        suite::FixBus::send (suite::Fix::LaterDropBigger);
        lead->fixes->poll();
        check (lead->apvts.getRawParameterValue (pm::laterOnId)->load() > 0.5f && std::abs (lvl (*lead, pm::laterDrop) - (drop + 1.5f)) < 0.05f,
               "bigger later drops: Drop 2+ on, 1.5 dB above the drop");

        auto late = make (roleIndex ("Pads"));   // made after the fixes were sent: ignores them
        late->fixes->poll();
        check (late->fixes->appliedCount() == 0, "a plugin loaded later doesn't replay old fixes");
    }

    section ("Before every drop (from Ouroboros)");
    {
        // 120 BPM: Groove bars 1-2 (0-4 s), Drop bars 3-4 (4-8 s).
        auto run = [] (int role, int dropGap, int trackGap = 0)
        {
            suite::SectionMap m;
            m.totalBars = 4; m.count = 2; m.dropGap = dropGap;
            m.sections[0] = { 1, T::Groove };
            m.sections[1] = { 3, T::Drop };
            auto p = make (role);
            set (*p, pm::gapId, (float) trackGap);
            set (*p, pm::morphId, 0.0f);
            suite::SectionBus::publish (m);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> a;
            render (*p, 6.0, tone (440.0f), &a, &head);
            p->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            return a;
        };
        const auto pad = run (roleIndex ("Pads"), 3), kick = run (roleIndex ("Kick"), 3);
        check (rmsBetween (pad, 2.1, 3.9) < 0.01f * rmsBetween (pad, 0.5, 1.5) && rmsBetween (pad, 4.2, 5.0) > 0.1f,
               "kick-only bar: the pads drop out for the whole bar before the drop, then return");
        check (rmsBetween (kick, 2.1, 3.9) > 0.9f * rmsBetween (kick, 0.5, 1.5), "... and the kick keeps playing");
        const auto beat = run (roleIndex ("Kick"), 2);
        check (rmsBetween (beat, 3.55, 3.95) < 0.01f && rmsBetween (beat, 2.5, 3.4) > 0.1f, "1 beat of silence: everything, the kick too");
        const auto bar = run (roleIndex ("Lead"), 0, 3);
        check (rmsBetween (bar, 2.1, 3.9) < 0.01f * rmsBetween (bar, 4.2, 5.0), "per track: a 1-bar gap");
        const auto none = run (roleIndex ("Pads"), 0);
        check (rmsBetween (none, 3.55, 3.95) > 0.1f, "'Per track' with no gap: plays through");
    }

    section ("Expression: drop entrances, breath, call and response, pump, gain staging");
    {
        // 120 BPM (2 s bars, 0.5 s beats).
        auto run = [] (int role, std::vector<std::pair<int, T>> sections, int totalBars, int entrance, std::function<void (PrimaMateriaProcessor&)> setup,
                       float hz = 440.0f, double seconds = 8.0)
        {
            suite::SectionMap m;
            m.totalBars = totalBars; m.dropEntrance = entrance;
            for (auto& [bar, type] : sections) m.sections[(size_t) m.count++] = { bar, type };
            auto p = make (role);
            set (*p, pm::morphId, 0.0f);
            if (setup) setup (*p);
            suite::SectionBus::publish (m);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> a;
            render (*p, seconds, tone (hz), &a, &head);
            p->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            return a;
        };
        const std::vector<std::pair<int, T>> grooveDrop { { 1, T::Groove }, { 3, T::Drop } };
        const auto kick = run (roleIndex ("Kick"), grooveDrop, 4, 1, {}), bass = run (roleIndex ("Bass"), grooveDrop, 4, 1, {}),
                   lead = run (roleIndex ("Lead"), grooveDrop, 4, 1, {});
        check (rmsBetween (kick, 4.05, 4.45) > 0.1f, "staggered drop: the kick comes in on beat 1");
        check (rmsBetween (bass, 4.05, 4.45) < 0.01f && rmsBetween (bass, 4.55, 4.95) > 0.1f, "... the bass on beat 2");
        check (rmsBetween (lead, 4.55, 4.95) < 0.01f && rmsBetween (lead, 5.05, 5.45) > 0.1f, "... and the rest on beat 3");
        const auto open = run (roleIndex ("Lead"), grooveDrop, 4, 2, {}, 6000.0f);
        check (rmsBetween (open, 4.02, 4.3) < 0.3f * rmsBetween (open, 6.0, 7.0), "filter opening: the drop starts dark and opens over the bar");

        const std::vector<std::pair<int, T>> longBreakdown { { 1, T::Breakdown } };
        {
            const auto swept = run (0, longBreakdown, 8, 0, [] (PrimaMateriaProcessor& p) { set (p, pm::sweepId, 1.0f); }, 3000.0f, 16.0);
            const auto plain = run (0, longBreakdown, 8, 0, {}, 3000.0f, 16.0);
            const auto early = rmsBetween (swept, 0.5, 1.5) / rmsBetween (plain, 0.5, 1.5);
            const auto middle = rmsBetween (swept, 7.5, 8.5) / rmsBetween (plain, 7.5, 8.5);
            const auto late = rmsBetween (swept, 15.0, 15.8) / rmsBetween (plain, 15.0, 15.8);
            check (early < 0.05f && middle > early * 2.0f && middle < 0.9f && late > 0.95f,
                   "breakdown sweep: a 3 kHz tone starts filtered away (" + juce::String (early, 3) + "), opens through the middle ("
                   + juce::String (middle, 2) + ") and is fully open before the end (" + juce::String (late, 2) + ")");
        }
        const auto breath = run (0, longBreakdown, 8, 0, [] (PrimaMateriaProcessor& p) { set (p, pm::breathId, 1.0f); }, 440.0f, 14.0);
        const auto swell = juce::Decibels::gainToDecibels (rmsBetween (breath, 3.6, 4.4) / rmsBetween (breath, 11.6, 12.4));
        check (swell > 2.5f && swell < 3.5f, "breath: an 8-bar swell, 3 dB from peak to trough (" + juce::String (swell, 2) + " dB)");

        const auto call = run (0, longBreakdown, 8, 0, [] (PrimaMateriaProcessor& p) { set (p, pm::answerId, (float) pm::answerCall); });
        const auto response = run (0, longBreakdown, 8, 0, [] (PrimaMateriaProcessor& p) { set (p, pm::answerId, (float) pm::answerResponse); });
        const auto callTurn = juce::Decibels::gainToDecibels (rmsBetween (call, 1.0, 3.5) / rmsBetween (call, 5.0, 7.5));
        const auto responseTurn = juce::Decibels::gainToDecibels (rmsBetween (response, 5.0, 7.5) / rmsBetween (response, 1.0, 3.5));
        check (std::abs (callTurn - 5.0f) < 0.3f && std::abs (responseTurn - 5.0f) < 0.3f, "call and response: they take turns every 2 bars (5 dB)");

        suite::KickPatternSet kp;
        kp.valid = true;
        auto& any = kp.bySection[(size_t) suite::KickPatternSet::anySection];
        any.count = 4; any.positions = { 0.0f, 1.0f, 2.0f, 3.0f }; any.kickBars = 4;
        suite::KickBus::publish (kp);
        const auto pumped = run (0, grooveDrop, 4, 0, [] (PrimaMateriaProcessor& p) { set (p, pm::pumpId, 1.0f); });
        suite::KickBus::withdraw();
        const auto duck = juce::Decibels::gainToDecibels (rmsBetween (pumped, 5.01, 5.06) / rmsBetween (pumped, 5.4, 5.48));
        check (duck < -5.0f, "pump: ducks right after each shared kick (" + juce::String (duck, 1) + " dB)");

        suite::FixBus::reset();
        auto staged = make (0);
        set (*staged, pm::followId, 0.0f);
        FakePlayHead head;
        staged->setPlayHead (&head);
        render (*staged, 1.0, tone (440.0f), nullptr, &head);   // 0.5 amplitude sine: -9 dBFS RMS
        staged->setPlayHead (nullptr);
        check (std::abs (staged->inputLevelDb() + 9.03f) < 0.2f, "gain staging measures the input (" + juce::String (staged->inputLevelDb(), 1) + " dBFS RMS)");
        suite::FixBus::send (suite::Fix::GainStage);
        staged->fixes->poll();
        check (std::abs (staged->apvts.getRawParameterValue (pm::inputGainId)->load() + 8.97f) < 0.2f, "and sets Input gain to bring it to -18 dBFS");
    }

    section ("Stop moments");
    {
        suite::SectionMap m;
        m.totalBars = 4; m.count = 1;
        m.sections[0] = { 1, T::Groove };
        m.momentCount = 1;
        m.moments[0] = { 3, suite::momentStop };   // bar 3 starts at 4 s (120 BPM): silence from 3.5 s
        auto p = make (roleIndex ("Kick"));
        set (*p, pm::morphId, 0.0f);
        suite::SectionBus::publish (m);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> a;
        render (*p, 5.0, tone (440.0f), &a, &head);
        p->setPlayHead (nullptr);
        suite::SectionBus::withdraw();
        check (rmsBetween (a, 3.0, 3.45) > 0.1f && rmsBetween (a, 3.55, 3.95) < 0.01f && rmsBetween (a, 4.1, 4.9) > 0.1f,
               "a stop moment silences the beat before its bar, even on the kick, then everything returns");
    }

    section ("Vocal-aware levels and moving carves");
    {
        auto run = [] (int role, float vocal, std::function<void (PrimaMateriaProcessor&)> setup = {}, float hz = 440.0f)
        {
            suite::SectionBus::withdraw();
            auto p = make (role);
            set (*p, pm::followId, 0.0f);
            if (setup) setup (*p);
            juce::AudioBuffer<float> a;
            render (*p, 1.5, tone (hz), &a, nullptr, [vocal] (int64_t) { suite::ActivityBus::write (suite::ActivityBus::vocal, vocal); });
            suite::ActivityBus::clear();
            return rmsBetween (a, 1.0, 1.5);
        };
        const auto padsQuiet = run (roleIndex ("Pads"), 0.0f), padsSinging = run (roleIndex ("Pads"), 1.0f);
        const auto kickQuiet = run (roleIndex ("Kick"), 0.0f), kickSinging = run (roleIndex ("Kick"), 1.0f);
        const auto stepBack = juce::Decibels::gainToDecibels (padsSinging / padsQuiet);
        check (std::abs (stepBack + 1.5f) < 0.2f && std::abs (kickSinging - kickQuiet) < 1.0e-4f,
               "while the vocal sings, the pads step back 1.5 dB (" + juce::String (stepBack, 2) + "); the kick doesn't move");

        auto carve = [] (PrimaMateriaProcessor& p)
        {
            set (p, pm::carveBandId, 6.0f);     // 1 kHz
            set (p, pm::carveDepthId, -9.0f);
            set (p, pm::carveWhenId, 1.0f);     // only while the vocal sings
        };
        const auto open = run (roleIndex ("Kick"), 0.0f, carve, 1000.0f), dipped = run (roleIndex ("Kick"), 1.0f, carve, 1000.0f);
        const auto dip = juce::Decibels::gainToDecibels (dipped / open);
        check (dip < -7.0f, "a carve made for the vocal opens only while it sings (" + juce::String (dip, 1) + " dB at 1 kHz then, none without it)");
    }

    section ("The vocal leads; section lows; carves the breakdowns don't need");
    {
        auto stepBack = [] (const char* name)
        {
            auto p = make (roleIndex ("Pads"));
            juce::AudioProcessor::TrackProperties props;
            props.name = name;
            p->updateTrackProperties (props);
            p->announce();   // (it reads its name)
            auto level = [&] (bool singing)
            {
                juce::AudioBuffer<float> a;
                render (*p, 1.0, tone (440.0f), &a, nullptr, [singing] (int64_t) { if (singing) suite::ActivityBus::write (suite::ActivityBus::vocal, 1.0f); });
                return rmsBetween (a, 0.6, 1.0);
            };
            const auto quiet = level (false), sung = level (true);
            suite::ActivityBus::clear();
            return juce::Decibels::gainToDecibels (sung / quiet);
        };
        const auto pads = stepBack ("Pads"), arp = stepBack ("Arp");
        check (std::abs (pads + 1.5f) < 0.2f && std::abs (arp + 3.75f) < 0.3f,
               "while the vocal sings, the pads step back " + juce::String (-pads, 1) + " dB and the busy arp " + juce::String (-arp, 1) + " dB: the vocal leads");

        auto bass = make (roleIndex ("Bass"));
        const auto introBefore = bass->apvts.getRawParameterValue (pm::levelId ((int) T::Intro))->load();
        suite::FixBus::send (suite::Fix::SectionLowsDown, (uint32_t) T::Intro);
        bass->fixes->poll();
        check (std::abs (bass->apvts.getRawParameterValue (pm::levelId ((int) T::Intro))->load() - (introBefore - 2.0f)) < 0.05f || introBefore <= pm::offDb + 0.5f,
               "the intro's lows too heavy: the bass 2 dB down in the intro");

        auto pad = make (roleIndex ("Pads"));
        suite::FixBus::send (suite::Fix::Carve, suite::packCarve (pad->getInstanceId(), 5, 0, true));
        pad->fixes->poll();
        check (pad->apvts.getRawParameterValue (pm::carveWhereId)->load() > 0.5f, "a carve for a clash heard in the drops is set 'not in breakdowns'");
    }

    section ("Phrase ride");
    {
        auto p = make (roleIndex ("Pads"));
        suite::SectionMap longDrop;
        longDrop.totalBars = 32; longDrop.count = 1;
        longDrop.sections[0] = { 1, T::Drop };
        suite::SectionBus::publish (longDrop);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 64.0, tone (440.0f), &audio, &head);
        p->setPlayHead (nullptr);
        const auto first = rmsBetween (audio, 4.0, 14.0), last = rmsBetween (audio, 52.0, 62.0);
        const auto rise = juce::Decibels::gainToDecibels (last / first);
        check (std::abs (rise - 1.0f) < 0.15f, "across a 32-bar drop each phrase sits a little higher: the last 1 dB over the first (" + juce::String (rise, 2) + " dB)");
        suite::SectionBus::withdraw();
    }

    section ("Mono lows and the vocal rider");
    {
        auto antiPhase = [] (float hz)
        {
            return [hz] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate);
                    b.setSample (0, i, v); b.setSample (1, i, -v);   // all side, no mid
                }
            };
        };
        auto run = [&] (float hz, int mono)
        {
            suite::SectionBus::withdraw();
            auto p = make (roleIndex ("Pads"));
            set (*p, pm::followId, 0.0f);
            set (*p, pm::monoLowsId, (float) mono);
            juce::AudioBuffer<float> a;
            render (*p, 1.0, antiPhase (hz), &a);
            return rmsBetween (a, 0.5, 1.0);
        };
        check (run (40.0f, 2) < 0.15f * run (40.0f, 0), "mono lows: stereo-only content at 40 Hz is folded away below 120 Hz");
        check (run (2000.0f, 2) > 0.95f * run (2000.0f, 0), "and the stereo image above it is untouched");

        auto ride = [] (float over)
        {
            auto p = make (roleIndex ("Vocal"));
            set (*p, pm::followId, 0.0f);
            set (*p, pm::rideVocalId, 3.0f);
            render (*p, 2.0, tone (440.0f), nullptr, nullptr, [over] (int64_t)
            {
                if (over > -100.0f) suite::ActivityBus::writeValue (suite::ActivityBus::vocalOverMusic, over);
            });
            suite::ActivityBus::clear();
            return p->riderDbNow.load();
        };
        const auto buried = ride (-3.0f), proud = ride (8.0f), silent = ride (-1000.0f);
        check (buried > 1.7f && buried < 2.3f && proud < -1.7f && std::abs (silent) < 0.01f,
               "vocal rider: up ~1 dB a second when the vocal is under its target (+" + juce::String (buried, 1) + " dB in 2 s), down when it's over ("
               + juce::String (proud, 1) + "), and still while it doesn't sing");
    }

    section ("Arrange from the loop");
    {
        suite::FixBus::reset();
        auto lead = make (roleIndex ("Lead"));
        auto none = make (0);
        for (auto* p : { lead.get(), none.get() })
            for (int t = 0; t < pm::numTypes; ++t) set (*p, pm::levelId (t), 0.0f);   // the loop: everything plays everywhere
        const auto seq = suite::FixBus::send (suite::Fix::ArrangeFromLoop);
        lead->fixes->poll();
        none->fixes->poll();
        const auto introLevel = lead->apvts.getRawParameterValue (pm::levelId ((int) T::Intro))->load();
        const auto dropLevel = lead->apvts.getRawParameterValue (pm::levelId ((int) T::Drop))->load();
        check (introLevel <= pm::offDb + 0.5f && dropLevel > -1.0f, "the lead sits out the intro and plays the drop, from its role");
        check (none->apvts.getRawParameterValue (pm::levelId ((int) T::Intro))->load() == 0.0f, "a track with no role is left alone");
        suite::FixBus::sendUndo (seq);
        lead->fixes->poll();
        check (lead->apvts.getRawParameterValue (pm::levelId ((int) T::Intro))->load() == 0.0f, "undo puts the loop back");
    }

    section ("Energy curve");
    {
        // 120 BPM: Groove bars 1-2, Drop bars 3-4; the drop drawn at 75% energy.
        auto run = [] (int dropEnergy)
        {
            suite::SectionMap m;
            m.totalBars = 4; m.count = 2;
            m.sections[0] = { 1, T::Groove };
            m.sections[1] = { 3, T::Drop, dropEnergy };
            auto p = make (0);   // neutral role: 0 dB everywhere
            set (*p, pm::morphId, 0.0f);
            suite::SectionBus::publish (m);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> a;
            render (*p, 8.0, tone (440.0f), &a, &head);
            p->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            return juce::Decibels::gainToDecibels (rmsBetween (a, 5.0, 7.5) / rmsBetween (a, 1.0, 3.5));
        };
        const auto usual = run (-1), lower = run (75);
        check (std::abs (usual) < 0.1f && std::abs (lower + 3.0f) < 0.2f, "a drop drawn at 75% plays 3 dB lower (" + juce::String (lower, 2)
                   + " dB); untouched, no change");
    }

    section ("Arc match from Mentalism");
    {
        suite::FixBus::reset();
        suite::SectionMap m;
        m.totalBars = 4; m.count = 2;
        m.sections[0] = { 1, T::Groove };
        m.sections[1] = { 3, T::Drop };
        auto p = make (0);
        set (*p, pm::morphId, 0.0f);
        auto level = [&] (PrimaMateriaProcessor& proc)
        {
            suite::SectionBus::publish (m);
            FakePlayHead head;
            head.bpm = 120.0;
            proc.setPlayHead (&head);
            juce::AudioBuffer<float> a;
            render (proc, 8.0, tone (440.0f), &a, &head);
            proc.setPlayHead (nullptr);
            return juce::Decibels::gainToDecibels (rmsBetween (a, 5.0, 7.5) / rmsBetween (a, 1.0, 3.5));
        };
        const auto usual = level (*p);
        const auto seq = suite::FixBus::send (suite::Fix::ArcMatch, suite::packArc (1, -3.0f));
        p->fixes->poll();
        const auto trimmed = level (*p);
        check (std::abs (usual) < 0.1f && std::abs (trimmed + 3.0f) < 0.2f && suite::FixBus::acknowledgements (seq) == 1,
               "the drop (section 2) comes down 3 dB (" + juce::String (trimmed, 2) + " dB)");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make (0);
        set (*q, pm::morphId, 0.0f);
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (level (*q) + 3.0f) < 0.2f, "it's saved with the project");

        auto moved = m;
        moved.sections[1].startBar = 2;   // the map changed: that section isn't there any more
        std::swap (m, moved);
        check (std::abs (level (*q)) < 0.1f, "and forgotten when the map moves the section");
        std::swap (m, moved);

        suite::FixBus::sendUndo (seq);
        p->fixes->poll();
        check (std::abs (level (*p)) < 0.1f, "undo puts it back");
        suite::SectionBus::withdraw();
    }

    section ("Club edit");
    {
        auto lvl = [] (PrimaMateriaProcessor& p, T t) { return p.apvts.getRawParameterValue (pm::levelId ((int) t))->load(); };
        auto pads = make (roleIndex ("Pads"));
        auto kick = make (roleIndex ("Kick"));
        set (*pads, pm::levelId ((int) T::Intro), -3.0f);
        set (*kick, pm::levelId ((int) T::Intro), pm::offDb);
        check (pads->applyVersion (suite::clubEdit) && kick->applyVersion (suite::clubEdit), "both tracks take part in the club edit");
        check (lvl (*pads, T::Intro) <= pm::offDb + 0.5f && lvl (*pads, T::Outro) <= pm::offDb + 0.5f, "pads out of the intro and outro");
        check (lvl (*kick, T::Intro) > pm::offDb + 0.5f, "the kick in the intro");
        check (! pads->applyVersion (suite::radioEdit), "other versions leave the mixer alone");
    }

    section ("Your own role settings");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PrimaMateriaRoles");
        dir.deleteRecursively();
        PrimaMateriaProcessor::roleFolderOverride() = dir;
        const auto pads = roleIndex ("Pads");
        auto p = make (pads);
        const auto builtIn = p->apvts.getRawParameterValue (pm::levelId ((int) T::Breakdown))->load();
        set (*p, pm::levelId ((int) T::Breakdown), -9.0f);
        set (*p, pm::breathId, 0.5f);
        check (p->saveMyRole() && PrimaMateriaProcessor::hasMyRole (pads), "Save as my role keeps this track's settings for Pads");
        auto q = make (pads);   // a new pads track (applyRole runs, as when it's named "Pads")
        check (std::abs (q->apvts.getRawParameterValue (pm::levelId ((int) T::Breakdown))->load() + 9.0f) < 0.05f
                   && std::abs (q->apvts.getRawParameterValue (pm::breathId)->load() - 0.5f) < 0.01f,
               "a new Pads track starts from your settings (breakdown -9 dB, breath 50%)");
        auto kick = make (roleIndex ("Kick"));
        check (std::abs (kick->apvts.getRawParameterValue (pm::breathId)->load()) < 0.01f, "other roles are untouched");
        PrimaMateriaProcessor::forgetMyRole (pads);
        auto r = make (pads);
        check (std::abs (r->apvts.getRawParameterValue (pm::levelId ((int) T::Breakdown))->load() - builtIn) < 0.05f, "forget it and the built-in Pads come back");
        PrimaMateriaProcessor::roleFolderOverride() = juce::File();
        dir.deleteRecursively();
    }

    section ("Auto-role from the sound");
    {
        using B = std::array<float, analysis::numBands>;
        //                31   63  125  250  500   1k   2k   4k   8k  16k
        const B lowHeavy { -20, -12, -18, -36, -42, -48, -55, -60, -65, -70 };
        const B hats     { -80, -80, -70, -60, -50, -40, -30, -22, -16, -24 };
        const B lead     { -70, -60, -45, -35, -28, -22, -16, -18, -26, -40 };
        const B pads     { -60, -50, -34, -22, -20, -26, -32, -38, -46, -58 };
        const B flat     { -30, -30, -30, -30, -30, -30, -30, -30, -30, -30 };
        check (PrimaMateriaProcessor::roleFromSound (lowHeavy, 14.0f) == 1 && PrimaMateriaProcessor::roleFromSound (lowHeavy, 5.0f) == 2,
               "low and punchy is a kick; low and sustained is a bass");
        check (PrimaMateriaProcessor::roleFromSound (hats, 10.0f) == 3 && PrimaMateriaProcessor::roleFromSound (lead, 6.0f) == 5
                   && PrimaMateriaProcessor::roleFromSound (pads, 5.0f) == 4, "bright top is hats, forward 2-4 kHz is a lead, warm mids are pads");
        check (PrimaMateriaProcessor::roleFromSound (flat, 6.0f) == -1, "and it says nothing when unsure");

        // A real kick: decaying 55 Hz thumps at 138 BPM, on a track with no role and no telling name.
        auto p = std::make_unique<PrimaMateriaProcessor>();
        prepare (*p);
        FakePlayHead head;
        p->setPlayHead (&head);
        render (*p, 9.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate, beat = std::fmod (t, 60.0 / 138.0);
                const auto v = 0.8f * (float) (std::sin (juce::MathConstants<double>::twoPi * 55.0 * beat) * std::exp (-beat * 14.0));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        }, nullptr, &head);
        p->setPlayHead (nullptr);
        p->suiteStatus();
        check (p->getRole() == 1 && p->roleGuessed.load(), "after 8 s of a kick with no role, it becomes the Kick (crest " + juce::String (p->averageCrestDb(), 1) + " dB)");
    }

    section ("Your rough-mix balance and signature chain");
    {
        const auto log = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PrimaMateriaBalance.txt");
        log.deleteFile();
        suite::StyleLog::setFileForTests (log);
        const auto pads = roleIndex ("Pads");
        const auto builtIn = PrimaMateriaProcessor::roughMixOffsetFor (pads);
        suite::StyleLog::add ("balance", { { "role", juce::String (pads) }, { "offset", "-7.0" } });
        check (std::abs (PrimaMateriaProcessor::roughMixOffsetFor (pads) - builtIn) < 0.01f, "one finished song isn't enough to change the rough mix");
        suite::StyleLog::add ("balance", { { "role", juce::String (pads) }, { "offset", "-6.0" } });
        check (std::abs (PrimaMateriaProcessor::roughMixOffsetFor (pads) + 6.0f) < 0.6f, "after two, the rough mix puts Pads where you do (" + juce::String (PrimaMateriaProcessor::roughMixOffsetFor (pads), 1) + " LU against the kick)");
        suite::StyleLog::setFileForTests ({});
        log.deleteFile();

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PrimaMateriaChain");
        dir.deleteRecursively();
        PrimaMateriaProcessor::roleFolderOverride() = dir;
        auto p = make (pads);
        juce::AudioProcessor::TrackProperties props;
        props.name = juce::String ("Big Pad");
        p->updateTrackProperties (props);
        const auto before = suite::RecipeBus::latest();
        p->saveMyRole();
        suite::RecipeAction action {};
        juce::String name;
        check (suite::RecipeBus::latest() == before + 1 && suite::RecipeBus::read (before + 1, action, name) && action == suite::RecipeAction::Save
                   && name == PrimaMateriaProcessor::chainName (pads) + "@Big Pad",
               "Save as my role also saves the track's chain of suite plugins (" + name + ")");
        PrimaMateriaProcessor::roleFolderOverride() = juce::File();
        dir.deleteRecursively();
    }

    section ("Continuous gain staging");
    {
        auto p = make (roleIndex ("Pads"));
        set (*p, pm::autoStageId, 1.0f);
        FakePlayHead head;
        p->setPlayHead (&head);
        render (*p, 3.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = 0.0447f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) (pos + i) / sampleRate);   // -30 dBFS RMS
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        }, nullptr, &head);
        p->setPlayHead (nullptr);
        for (int k = 0; k < 49; ++k) p->suiteStatus();
        check (std::abs (p->apvts.getRawParameterValue (pm::inputGainId)->load()) < 0.01f, "nothing moves before 10 s of updates");
        p->suiteStatus();
        check (std::abs (p->apvts.getRawParameterValue (pm::inputGainId)->load() - 12.0f) < 0.5f && p->autoStageMoves.load() == 1,
               "then the input gain brings a -30 dBFS track to -18 (" + juce::String (p->apvts.getRawParameterValue (pm::inputGainId)->load(), 1) + " dB)");
        for (int k = 0; k < 50; ++k) p->suiteStatus();
        check (p->autoStageMoves.load() == 1, "and leaves it alone once it's there");
    }

    section ("Low-end manager");
    {
        // 120 BPM: Breakdown bars 1-2 (0-4 s, no bass), Drop bars 3-4 (4-8 s, the bass plays).
        suite::SectionMap m;
        m.totalBars = 4; m.count = 2;
        m.sections[0] = { 1, T::Breakdown };
        m.sections[1] = { 3, T::Drop };
        auto run = [&m] (float hz, bool manager, bool bassLoaded)
        {
            suite::Registry::reset();
            if (bassLoaded)   // a Bass track elsewhere in the project that plays in drops only
                suite::Registry::announce (999, "Prima Materia", "Bass", suite::Status::roleSet, { 2.0f, -10.0f, 0.0f, (float) (1u << (int) T::Drop) });
            auto pad = make (roleIndex ("Pads"));
            set (*pad, pm::morphId, 0.0f);
            set (*pad, pm::lowManagerId, manager ? 1.0f : 0.0f);
            pad->refreshLowOwners();
            suite::SectionBus::publish (m);
            FakePlayHead head;
            head.bpm = 120.0;
            pad->setPlayHead (&head);
            juce::AudioBuffer<float> a;
            render (*pad, 8.0, tone (hz), &a, &head);
            pad->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            suite::Registry::reset();
            return juce::Decibels::gainToDecibels (rmsBetween (a, 5.0, 7.5) / rmsBetween (a, 1.0, 3.5));   // drop vs breakdown
        };
        // The Pads role is +2 dB in breakdowns and -2 dB in drops: 4 dB down anyway.
        const auto low = run (60.0f, true, true), high = run (1000.0f, true, true);
        check (std::abs (high + 4.0f) < 0.5f, "1 kHz: just the role's 4 dB (" + juce::String (high, 1) + " dB)");
        check (low < -14.0f, "60 Hz: the pad leaves the low end to the bass in the drop (" + juce::String (low, 1) + " dB)");
        check (std::abs (run (60.0f, false, true) + 4.0f) < 0.5f, "manager off: untouched");
        check (std::abs (run (60.0f, true, false) + 4.0f) < 0.5f, "no kick or bass track loaded: untouched");

        auto bass = make (roleIndex ("Bass"));
        check ((bass->soundingMask() & (1u << (int) T::Drop)) != 0 && (bass->soundingMask() & (1u << (int) T::Breakdown)) == 0,
               "a Bass track reports that it plays in drops, not breakdowns");
    }

    section ("Auto-role from the track name");
    {
        const std::pair<const char*, const char*> names[] = { { "Kick", "Kick" }, { "Sub Bass", "Bass" }, { "HH open", "Hats" },
                                                               { "Supersaw Lead", "Lead" }, { "Pad 2", "Pads" }, { "Vox chop", "Vocal" },
                                                               { "Riser FX", "Atmos" }, { "Pluck", "Pads" }, { "Insert 12", "" } };
        bool allRight = true;
        juce::StringArray got;
        for (const auto& [name, expected] : names)
        {
            const auto r = pm::roleFromTrackName (name);
            const auto roleName = r >= 0 ? juce::String (pm::roles()[(size_t) r].name) : juce::String();
            got.add (juce::String (name) + "=" + (roleName.isEmpty() ? "-" : roleName));
            allRight = allRight && roleName.startsWith (expected) && (juce::String (expected).isNotEmpty() || r < 0);
        }
        check (allRight, "track names map to roles: " + got.joinIntoString (", "));

        auto fresh = std::make_unique<PrimaMateriaProcessor>();
        prepare (*fresh);
        juce::AudioProcessor::TrackProperties props;
        props.name = juce::String ("Sub Bass");
        fresh->updateTrackProperties (props);
        check (fresh->getRole() == roleIndex ("Bass") && fresh->apvts.getRawParameterValue (pm::levelId ((int) T::Breakdown))->load() <= pm::offDb + 0.5f,
               "a new Prima Materia on 'Sub Bass' sets itself up as Bass");
        props.name = juce::String ("Lead");
        fresh->updateTrackProperties (props);
        check (fresh->getRole() == roleIndex ("Bass"), "renaming the track later doesn't override a role");
    }

    section ("Rough mix");
    {
        suite::Registry::reset();
        suite::FixBus::reset();
        suite::SectionMap allDrop;
        allDrop.totalBars = 8; allDrop.count = 1;
        allDrop.sections[0] = { 1, T::Drop };
        auto measure = [&] (PrimaMateriaProcessor& p, float amp, float hz)
        {
            suite::SectionBus::publish (allDrop);
            FakePlayHead head;
            head.bpm = 120.0;
            p.setPlayHead (&head);
            render (p, 4.0, [amp, hz] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, nullptr, &head);
            p.setPlayHead (nullptr);
            p.announce();
        };
        auto kick = make (roleIndex ("Kick"));
        auto pad = make (roleIndex ("Pads"));
        measure (*kick, 0.5f, 1000.0f);
        measure (*pad, 0.5f, 1000.0f);   // as loud as the kick: far too loud for a pad
        const auto padBefore = pad->measuredLufs();
        check (kick->measuredLufs() > -20.0f && std::abs (padBefore - (kick->measuredLufs() - 2.0f)) < 0.2f,
               "measures each track's drop loudness, after its section level (kick " + juce::String (kick->measuredLufs(), 1)
                   + ", pad " + juce::String (padBefore, 1) + " LUFS: the Pads role sits 2 dB down in drops)");

        const auto seq = suite::FixBus::send (suite::Fix::RoughMix);
        kick->fixes->poll();
        pad->fixes->poll();
        const auto padTrim = pad->apvts.getRawParameterValue (pm::trimId)->load();
        check (std::abs (padTrim - (kick->measuredLufs() - 7.0f - padBefore)) < 0.3f, "the pad is trimmed to sit 7 LU under the kick ("
                   + juce::String (padTrim, 1) + " dB)");
        check (std::abs (kick->apvts.getRawParameterValue (pm::trimId)->load()) < 0.01f && suite::FixBus::acknowledgements (seq) == 1,
               "the kick is the reference and stays put");

        measure (*pad, 0.5f, 1000.0f);
        check (std::abs (pad->measuredLufs() - (kick->measuredLufs() - 7.0f)) < 0.5f, "after the rough mix the pad measures 7 LU under the kick ("
                   + juce::String (pad->measuredLufs(), 1) + " LUFS)");

        suite::FixBus::sendUndo (seq);
        pad->fixes->poll();
        check (std::abs (pad->apvts.getRawParameterValue (pm::trimId)->load()) < 0.01f, "Undo puts the trim back");

        // Mentalism's balance loop: a 1 dB step at a time, measured afresh after each
        measure (*pad, 0.5f, 1000.0f);
        auto step = suite::FixBus::send (suite::Fix::BalanceStep);
        kick->fixes->poll();
        pad->fixes->poll();
        check (std::abs (pad->apvts.getRawParameterValue (pm::trimId)->load() + 1.0f) < 0.01f && suite::FixBus::acknowledgements (step) == 1,
               "a balance step moves the pad 1 dB towards its balance (the kick stays)");
        step = suite::FixBus::send (suite::Fix::BalanceStep);
        pad->fixes->poll();
        check (std::abs (pad->apvts.getRawParameterValue (pm::trimId)->load() + 1.0f) < 0.01f && suite::FixBus::acknowledgements (step) == 0,
               "and waits to hear the new level before the next one");
        measure (*pad, 0.5f, 1000.0f);
        step = suite::FixBus::send (suite::Fix::BalanceStep);
        pad->fixes->poll();
        check (std::abs (pad->apvts.getRawParameterValue (pm::trimId)->load() + 2.0f) < 0.01f, "then takes the next step");

        // The problem watcher's fixes
        auto vox = make (roleIndex ("Vocal"));
        suite::FixBus::send (suite::Fix::VocalRide);
        vox->fixes->poll();
        pad->fixes->poll();
        check (std::abs (vox->apvts.getRawParameterValue (pm::rideVocalId)->load() - 6.0f) < 0.01f && pad->apvts.getRawParameterValue (pm::rideVocalId)->load() < 0.01f,
               "a buried vocal: the vocal track rides itself 6 dB over the music (the pad doesn't)");
        set (*pad, pm::monoLowsId, 0.0f);
        suite::FixBus::send (suite::Fix::MonoLows);
        pad->fixes->poll();
        check ((int) pad->apvts.getRawParameterValue (pm::monoLowsId)->load() == 2, "mono lows back on (below 120 Hz) where they were off");

        auto loner = make (roleIndex ("Lead"));
        suite::Registry::reset();
        measure (*loner, 0.3f, 800.0f);
        check (! loner->roughMixTrim().has_value(), "without a kick track there's no reference: nothing changes");
        suite::SectionBus::withdraw();
    }

    section ("Pass-through when not following");
    {
        auto p = make (roleIndex ("Kick"));
        set (*p, pm::followId, 0.0f);
        const auto a = play (*p, tone (60.0f), 6.0);
        check (rmsBetween (a, 4.3, 5.5) > 0.3f, "Follow off: unchanged");
        suite::SectionBus::withdraw();
        auto q = make (roleIndex ("Kick"));
        juce::AudioBuffer<float> b;
        render (*q, 1.0, tone (60.0f), &b);
        check (std::abs (rmsBetween (b, 0.2, 0.9) - 0.5f / std::sqrt (2.0f)) < 0.01f, "no map: unchanged");
    }

    if (outDir != juce::File())
    {
        auto p = make (roleIndex ("Pads"));
        play (*p, tone (220.0f), 10.0);   // stop inside the build so a column is lit
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("prima_materia.png"));
        editor.reset();
    }

    suite::SectionBus::withdraw();
    section ("The problem watcher's ladders");
    {
        suite::FixBus::reset();
        suite::Registry::reset();
        auto lvl = [] (PrimaMateriaProcessor& p, int column) { return p.apvts.getRawParameterValue (pm::levelId (column))->load(); };
        auto pv = [] (PrimaMateriaProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); };
        auto pads = make (roleIndex ("Pads"));
        juce::AudioProcessor::TrackProperties props;
        props.name = "Pads";
        pads->updateTrackProperties (props);
        const auto drop = lvl (*pads, (int) T::Drop);
        suite::FixBus::send (suite::Fix::SectionDown, (uint32_t) T::Drop);
        pads->fixes->poll();
        check (std::abs (lvl (*pads, (int) T::Drop) - (drop - 1.0f)) < 0.05f, "Lapis works hardest in the drops: 1 dB down there");
        suite::FixBus::send (suite::Fix::MasterTone, suite::packMasterTone (pads->getInstanceId(), 3, -1.5f));
        pads->fixes->poll();
        check ((int) pv (*pads, pm::carveBandId) - 1 == 3 && std::abs (pv (*pads, pm::carveDepthId) + 1.5f) < 0.05f,
               "no Quintessence on the track: its share of the master's tone lands in the carve band (250 Hz -1.5 dB)");
        suite::FixBus::send (suite::Fix::MasterTone, suite::packMasterTone (pads->getInstanceId(), 3, 1.0f));
        pads->fixes->poll();
        check (std::abs (pv (*pads, pm::carveDepthId) + 1.5f) < 0.05f, "(only cuts)");
        suite::Registry::announce (0x7e1u, "Quintessence", "Pads", suite::Status::roleSet, { 4.0f, 0, 0, 0 });
        suite::FixBus::send (suite::Fix::MasterTone, suite::packMasterTone (pads->getInstanceId(), 3, -1.0f));
        pads->fixes->poll();
        check (std::abs (pv (*pads, pm::carveDepthId) + 1.5f) < 0.05f, "with a Quintessence on the track, the Quintessence takes it");
        suite::Registry::reset();
        pads->apvts.getParameter (pm::gapId)->setValueNotifyingHost (0.0f);
        suite::FixBus::send (suite::Fix::BuildImpact, 1u);
        pads->fixes->poll();
        check ((int) pv (*pads, pm::gapId) == 1, "a drop that doesn't land: half a beat of silence before it");
        suite::FixBus::reset();
    }


    section ("Autopilot (stability): DC out, a one-off peak held, level changes left alone");
    {
        // A steady 200 Hz part with a DC offset and a 2 ms spike at 3 s, in the groove (Kick role plays there)
        const InputFn spiky = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                auto v = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * t) + 0.1f;
                if (t >= 3.0 && t < 3.002) v = 0.95f;
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
        auto measure = [&spiky] (bool on, float& dc, float& spike, int& caught)
        {
            suite::Autopilot::reset();
            suite::Autopilot::set (suite::Autopilot::stability, on);
            auto p = make (roleIndex ("Kick"));
            const auto a = play (*p, spiky, 3.5);
            double sum = 0.0; int n = 0; spike = 0.0f;
            for (int i = (int) (2.0 * sampleRate); i < (int) (2.9 * sampleRate); ++i) { sum += a.getSample (0, i); ++n; }
            for (int i = (int) (2.99 * sampleRate); i < (int) (3.01 * sampleRate); ++i) spike = juce::jmax (spike, std::abs (a.getSample (0, i)));
            dc = (float) (sum / n);
            caught = p->peaksCaught.load();
        };
        float dcOn, spikeOn, dcOff, spikeOff; int caughtOn, caughtOff;
        measure (true, dcOn, spikeOn, caughtOn);
        measure (false, dcOff, spikeOff, caughtOff);
        check (std::abs (dcOn) < 0.01f && std::abs (dcOff) > 0.05f, "the DC offset is filtered out (" + juce::String (dcOn, 3) + " vs " + juce::String (dcOff, 3) + " off)");
        check (caughtOn >= 1 && spikeOn < 0.7f * spikeOff, "the spike is held (" + juce::String (spikeOn, 2) + " vs " + juce::String (spikeOff, 2) + " off)");
        check (caughtOff == 0, "switched off: nothing caught");
        suite::Autopilot::reset();
    }

    return finish();
}
