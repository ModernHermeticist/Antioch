// Offline tests for Prima Materia. Usage: PrimaMateriaTests [output-dir]

#include "PluginProcessor.h"
#include "TestUtils.h"
#include "Registry.h"
#include "FixBus.h"

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
    return finish();
}
