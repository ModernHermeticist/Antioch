// Offline tests for Transmutation. Usage: TransmutationTests [output-dir]

#include "PluginProcessor.h"
#include "SectionBus.h"
#include "TransmuteBus.h"
#include "StyleLog.h"
#include "KeyBus.h"
#include "DirectorBus.h"
#include "KitBus.h"
#include "WritingBus.h"
#include "TestUtils.h"
#include <set>

using namespace suite::test;

namespace
{
    using T = suite::SectionType;

    /** Intro 8, breakdown 8, build 8, drop 16, breakdown 8, build 8, drop 16, outro 8 (88 bars). */
    suite::SectionMap testMap()
    {
        suite::SectionMap m;
        const std::pair<int, T> s[] = { { 1, T::Intro }, { 9, T::Breakdown }, { 17, T::Build }, { 25, T::Drop },
                                        { 41, T::Breakdown }, { 49, T::Build }, { 57, T::Drop }, { 73, T::Outro } };
        m.count = 8;
        for (int i = 0; i < 8; ++i) m.sections[(size_t) i] = { s[i].first, s[i].second };
        m.totalBars = 80;
        return m;
    }

    trm::DirectorInput baseInput (int role)
    {
        trm::DirectorInput in;
        in.map = testMap(); in.haveMap = true;
        in.role = role; in.bpm = 138.0;
        in.hear.known = true;
        in.hear.pitched = role != 1 && role != 3;
        in.hear.sustained = role == 4 || role == 5;
        in.modes = trm::defaultModes();
        in.songSeed = 1234u; in.trackSeed = 77u + (uint32_t) role;
        return in;
    }

    std::unique_ptr<TransmutationProcessor> make()
    {
        auto p = std::make_unique<TransmutationProcessor>();
        prepare (*p);
        return p;
    }

    /** Steady material: a 220 Hz saw-ish tone plus a little noise (so every stage has something to work on). */
    InputFn music (float level = 0.25f)
    {
        return [level] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto ph = std::fmod (t * 220.0, 1.0);
                const auto v = level * (float) (0.7 * (2.0 * ph - 1.0) + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 3.0 * 220.0 * t));
                b.setSample (0, i, v);
                b.setSample (1, i, v * 0.9f);
            }
        };
    }

    float diffRms (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double t0, double t1)
    {
        double sum = 0.0;
        const auto s0 = (int) (t0 * sampleRate), s1 = juce::jmin (a.getNumSamples(), b.getNumSamples(), (int) (t1 * sampleRate));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = s0; i < s1; ++i) { const auto d = a.getSample (ch, i) - b.getSample (ch, i); sum += d * d; }
        return (float) std::sqrt (sum / juce::jmax (1, 2 * (s1 - s0)));
    }

    void everythingNeverBut (TransmutationProcessor& p, std::initializer_list<int> keep, uint8_t keepMode = trm::mAlways)
    {
        for (int id = 1; id <= trm::numEffects; ++id)
        {
            if (trm::effect (id).category == trm::cDirector) continue;
            const auto wanted = std::find (keep.begin(), keep.end(), id) != keep.end();
            if (p.modeOf (id) != (wanted ? keepMode : trm::mNever)) p.setMode (id, wanted ? keepMode : trm::mNever);
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File()) outDir.createDirectory();
    const auto testStyleLog = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TransmutationTestsStyleLog.txt");
    testStyleLog.deleteFile();
    suite::StyleLog::setFileForTests (testStyleLog);
    suite::TransmuteBus::reset();
    suite::Autopilot::reset();
    // other plugins' test runs can leave their shared state behind (same machine): start clean
    suite::DirectorBus::reset();
    suite::KitBus::withdraw();
    suite::WritingBus::clearSung();
    suite::KeyBus::publish (9, 0);

    section ("The catalogue: all 118 moves");
    {
        std::set<int> ids;
        bool named = true, placed = true, roled = true;
        for (const auto& e : trm::effects())
        {
            ids.insert (e.id);
            named = named && juce::String (e.name).isNotEmpty() && juce::String (e.what).isNotEmpty();
            const auto special = e.id == 34 || e.id == 100 || e.id == 103 || e.id == 116 || e.category == trm::cDirector;
            placed = placed && (e.slots != 0 || special);
            roled = roled && e.roles != 0;
        }
        check (ids.size() == 118 && *ids.begin() == 1 && *ids.rbegin() == 118, "118 different moves, numbered 1 to 118 (" + juce::String ((int) ids.size()) + ")");
        check (named, "every move has a name and a description");
        check (placed, "every move has a place in the song (or is placed by the director itself)");
        check (roled, "every move suits at least one role");
        int perCategory[trm::numCategories] {};
        for (const auto& e : trm::effects()) ++perCategory[e.category];
        check (perCategory[trm::cBuild] == 16 && perCategory[trm::cRhythm] == 13 && perCategory[trm::cPitch] == 14 && perCategory[trm::cSpace] == 20
                   && perCategory[trm::cExpression] == 12 && perCategory[trm::cWild] == 15 && perCategory[trm::cDirector] == 8
                   && perCategory[trm::cTransition] == 5 && perCategory[trm::cRole] == 7 && perCategory[trm::cPhysics] == 8,
               "the ten groups (A-J) hold the moves of the list");
        check (trm::defaultModes()[78] == trm::mNever && trm::defaultModes()[79] == trm::mNever && trm::defaultModes()[1] == trm::mAuto,
               "the two moves that borrow another track's sound are opt-in; the rest start on Auto");
    }

    section ("The director");
    {
        const auto lead = baseInput (5);
        const auto a = trm::makePlan (lead), b = trm::makePlan (lead);
        bool same = a.moves.size() == b.moves.size();
        for (size_t i = 0; same && i < a.moves.size(); ++i) same = a.moves[i].effect == b.moves[i].effect && a.moves[i].startQ == b.moves[i].startQ;
        check (! a.moves.empty() && same, "plans the same moves from the same song every time (" + juce::String ((int) a.moves.size()) + " moves for a lead)");

        auto movesIn = [] (const trm::Plan& p, double s, double e)
        {
            int n = 0;
            for (const auto& m : p.moves) if (m.startQ < e && m.endQ > s && (m.slot & trm::sSong) == 0) ++n;
            return n;
        };
        check (movesIn (a, 16 * 4.0, 24 * 4.0) > 0 && movesIn (a, 48 * 4.0, 56 * 4.0) > 0, "every build gets moves");

        auto zero = lead; zero.spice = 0.0f;
        check (trm::makePlan (zero).moves.empty(), "spice at zero: nothing happens");
        auto wild = lead; wild.spice = 2.0f;
        check (trm::makePlan (wild).moves.size() > a.moves.size(), "more spice: more moves (" + juce::String ((int) trm::makePlan (wild).moves.size()) + ")");

        // Never and Always
        auto never = lead;
        std::set<int> used;
        for (const auto& m : a.moves) used.insert (m.effect);
        for (auto id : used) never.modes[(size_t) id] = trm::mNever;
        const auto n = trm::makePlan (never);
        check (std::none_of (n.moves.begin(), n.moves.end(), [&] (const trm::Move& m) { return used.count (m.effect) > 0; }), "a move set to Never never happens");
        auto always = lead; always.modes[8] = trm::mAlways;
        const auto al = trm::makePlan (always);
        int phraseEnds = 0, reversed = 0;
        for (const auto& m : al.moves) if (m.effect == 8) ++reversed;
        for (int i = 0; i < 8; ++i) ++phraseEnds;   // (at least one per section change)
        check (reversed >= 6, "a move set to Always happens at every moment it fits (" + juce::String (reversed) + " reverses)");

        // Lock and re-roll
        trm::Move target;
        for (const auto& m : a.moves) if ((m.slot & trm::bodySlots) == 0) { target = m; break; }
        auto locked = lead; locked.locks[target.key] = target.effect;
        locked.rerolls[target.key] = 5;
        const auto lp = trm::makePlan (locked);
        check (std::any_of (lp.moves.begin(), lp.moves.end(), [&] (const trm::Move& m) { return m.key == target.key && m.effect == target.effect && (m.flags & trm::mfLocked); }),
               "a locked moment keeps its move, even re-rolled");
        std::set<int> picks;
        for (int r = 0; r < 12; ++r)
        {
            auto rr = lead; rr.rerolls[target.key] = r;
            for (const auto& m : trm::makePlan (rr).moves) if (m.key == target.key) picks.insert (m.effect);
        }
        check (picks.size() >= 2, "re-rolling a moment gives it other moves (" + juce::String ((int) picks.size()) + " different ones in 12 rolls)");

        // roles and what the audio is
        const auto kick = trm::makePlan (baseInput (1));
        check (std::all_of (kick.moves.begin(), kick.moves.end(), [] (const trm::Move& m)
                            { const auto& e = trm::effect (m.effect); return (e.roles & trm::rK) != 0 && (e.needs & trm::nPitched) == 0; }),
               "the kick only gets moves made for kicks, and nothing that needs pitch");
        auto unpitched = baseInput (4); unpitched.hear.pitched = false;
        const auto up = trm::makePlan (unpitched);
        check (std::none_of (up.moves.begin(), up.moves.end(), [] (const trm::Move& m) { return (trm::effect (m.effect).needs & trm::nPitched) != 0; }),
               "a pad heard as unpitched (noise, a riser) gets no harmony or pitch moves");
        auto pluck = baseInput (5); pluck.hear.sustained = false; pluck.hear.plucky = true; pluck.hear.busy = true;
        bool sustainedOnly = false;
        for (const auto& m : trm::makePlan (pluck).moves) if ((trm::effect (m.effect).needs & trm::nSustained) != 0) sustainedOnly = true;
        check (! sustainedOnly, "a plucky part gets no moves that need held notes");

        // the story arc
        auto amountIn = [] (const trm::Plan& p, double s, double e)
        {
            float sum = 0.0f; int k = 0;
            for (const auto& m : p.moves) if (m.startQ >= s && m.startQ < e && (m.slot & trm::sSong) == 0) { sum += m.amount; ++k; }
            return k > 0 ? sum / (float) k : 0.0f;
        };
        check (amountIn (a, 56 * 4.0, 72 * 4.0) > amountIn (a, 24 * 4.0, 40 * 4.0), "the story arc: the final drop's moves go further than the first's");
        auto flat = lead; flat.modes[92] = trm::mNever;
        const auto fp = trm::makePlan (flat);
        check (std::abs (amountIn (fp, 56 * 4.0, 72 * 4.0) - amountIn (fp, 24 * 4.0, 40 * 4.0)) < 0.01f, "(with the story arc off, every drop is the same)");

        // vocal priority
        auto withVocal = baseInput (4); withVocal.spice = 1.5f;
        withVocal.vocalBars.assign (90, true);
        auto withoutVocal = withVocal; withoutVocal.vocalBars.clear();
        auto events = [] (const trm::Plan& p) { int k = 0; for (const auto& m : p.moves) if ((m.slot & trm::bodySlots) == 0) ++k; return k; };
        check (events (trm::makePlan (withVocal)) < events (trm::makePlan (withoutVocal)), "vocal priority: fewer moves on other tracks while the vocal sings ("
               + juce::String (events (trm::makePlan (withVocal))) + " vs " + juce::String (events (trm::makePlan (withoutVocal))) + ")");

        // genres
        auto psy = lead; psy.genre = trm::gPsy;
        auto orch = lead; orch.genre = trm::gOrchestral;
        int psyRhythm = 0, orchRhythm = 0;
        for (uint32_t s = 0; s < 6; ++s)
        {
            psy.songSeed = orch.songSeed = 900u + s;
            for (const auto& m : trm::makePlan (psy).moves) if (trm::effect (m.effect).category == trm::cRhythm) ++psyRhythm;
            for (const auto& m : trm::makePlan (orch).moves) if (trm::effect (m.effect).category == trm::cRhythm) ++orchRhythm;
        }
        check (psyRhythm > orchRhythm, "genre profiles: psy reaches for rhythm and glitch more than orchestral (" + juce::String (psyRhythm) + " vs " + juce::String (orchRhythm) + ")");
        check (trm::genreFromTempo (138.0) == trm::gUplifting && trm::genreFromTempo (145.0) == trm::gPsy && trm::genreFromTempo (126.0) == trm::gProgressive,
               "Auto picks the genre from the tempo");

        // motifs
        int motifMoves = 0;
        for (const auto& m : a.moves) if (m.flags & trm::mfMotif) ++motifMoves;
        check (a.motifFamilies[0] >= 0 && a.motifFamilies[1] >= 0 && a.motifFamilies[2] >= 0 && motifMoves >= 2,
               "motifs: three kinds of move per song, coming back (" + juce::String (motifMoves) + " moves)");

        // the fake drop: a song-wide choice every track agrees on
        int fakeSeeds = 0; bool agree = true, placedFake = true;
        for (uint32_t s = 0; s < 20; ++s)
        {
            auto x = baseInput (5); x.songSeed = s * 7919u;
            auto y = baseInput (4); y.songSeed = s * 7919u; y.trackSeed = 999u;
            const auto px = trm::makePlan (x), py = trm::makePlan (y);
            agree = agree && px.fakeDrop == py.fakeDrop;
            if (px.fakeDrop >= 0)
            {
                ++fakeSeeds;
                placedFake = placedFake && std::any_of (px.moves.begin(), px.moves.end(), [] (const trm::Move& m) { return m.effect == 34 && std::abs (m.startQ - 56 * 4.0f) < 0.01f; });
            }
        }
        check (fakeSeeds > 0 && fakeSeeds < 20 && agree && placedFake, "the fake drop: chosen for some songs, on the second drop, the same on every track ("
               + juce::String (fakeSeeds) + " of 20 songs)");

        // the countdown: each role takes its beat
        auto hats = baseInput (3); hats.modes[100] = trm::mAlways;
        auto pads = baseInput (4); pads.modes[100] = trm::mAlways;
        auto beatOf = [] (const trm::Plan& p) { for (const auto& m : p.moves) if (m.effect == 100 && m.startQ < 25 * 4.0f) return (double) m.startQ; return -1.0; };
        check (std::abs (beatOf (trm::makePlan (hats)) - (24 * 4.0 - 4.0)) < 0.01 && std::abs (beatOf (trm::makePlan (pads)) - (24 * 4.0 - 3.0)) < 0.01,
               "the drop countdown: hats on the first of the last 4 beats, pads on the second");

        // contagion and call and response
        auto infected = baseInput (4);
        trm::OtherTrack leadTrack; leadTrack.role = 5; leadTrack.id = 1;
        trm::Move stutter; stutter.startQ = 17 * 4.0f; stutter.endQ = 18 * 4.0f; stutter.effect = 48; stutter.slot = trm::sBuildBody;
        leadTrack.moves.push_back (stutter);
        infected.others.push_back (leadTrack);
        const auto ip = trm::makePlan (infected);
        check (std::any_of (ip.moves.begin(), ip.moves.end(), [] (const trm::Move& m) { return m.effect == 116 && std::abs (m.startQ - 17 * 4.0f - 4.0f) < 0.01f; }),
               "contagion: the lead's stutter in the build reaches the pads a bar later");
        auto answering = baseInput (4); answering.spice = 2.0f;
        trm::OtherTrack caller; caller.role = 5; caller.id = 2;
        for (int bar : { 33, 65 })
        {
            trm::Move rev; rev.startQ = bar * 4.0f - 1.0f; rev.endQ = bar * 4.0f; rev.effect = 8; rev.slot = trm::sPhraseEnd;
            rev.key = (uint32_t) bar;
            caller.moves.push_back (rev);
        }
        answering.others.push_back (caller);
        const auto ap = trm::makePlan (answering);
        check (std::any_of (ap.moves.begin(), ap.moves.end(), [] (const trm::Move& m) { return (m.flags & trm::mfAnswer) != 0; }),
               "call and response: the pads answer the lead's reverse a bar later");

        // signature moves
        auto learned = lead;
        int before = 0, after = 0;
        for (uint32_t s = 0; s < 10; ++s)
        {
            learned.songSeed = s; learned.signature = {};
            for (const auto& m : trm::makePlan (learned).moves) if (m.effect == 62) ++before;
            learned.signature[62] = 8.0f;
            for (const auto& m : trm::makePlan (learned).moves) if (m.effect == 62) ++after;
        }
        check (after > before, "signature moves: a move you keep locking comes up more often (" + juce::String (before) + " -> " + juce::String (after) + ")");

        // no map: still something at phrase ends
        auto noMap = lead; noMap.haveMap = false;
        check (! trm::makePlan (noMap).moves.empty(), "without an Ouroboros map it still works on 8-bar phrases");
    }

    section ("The engine: neutral passes the sound through untouched");
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        set (*p, TParams::direct, 0.0f);
        FakePlayHead head; head.bpm = 120.0; p->setPlayHead (&head);
        juce::AudioBuffer<float> out, in;
        render (*p, 2.0, music(), &out, &head);
        juce::AudioBuffer<float> dry (2, out.getNumSamples());
        dry.clear();
        music() (dry, 0);
        check (diffRms (out, dry, 0.1, 2.0) < 1.0e-6f, "with nothing planned, the track comes out exactly as it went in");
    }

    section ("Every move plays (auditioned one at a time over a steady part)");
    {
        suite::SectionBus::publish (testMap());
        suite::Autopilot::reset();
        int ok = 0, audible = 0, quietOnes = 0;
        juce::StringArray bad, silent;
        // moves that need something this test doesn't have (a remembered drop, another track, the kick,
        // a key change, a vocal, a lead note) or change very little by design
        const std::set<int> needsMore { 17, 47, 53, 65, 68, 70, 76, 78, 79, 104, 114, 117, 118, 15, 85, 86, 72, 69, 102, 59, 57,
                                        64, 66, 75, 113 };   // (width on a near-mono part, air with no top end, chords, 2-bar fades)
        for (const auto& e : trm::effects())
        {
            if (e.category == trm::cDirector) continue;
            auto p = make();
            set (*p, TParams::direct, 0.0f);
            set (*p, TParams::role, e.roles & trm::rL ? 5.0f : e.roles & trm::rP ? 4.0f : e.roles & trm::rB ? 2.0f : e.roles & trm::rH ? 3.0f : e.roles & trm::rK ? 1.0f : 7.0f);
            FakePlayHead head; head.bpm = 120.0; head.ppq = 4.0 * 20;   // in the build (bar 21)
            p->setPlayHead (&head);
            juce::AudioBuffer<float> warm, out;
            render (*p, 1.0, music(), &warm, &head);
            p->audition (e.id);
            const auto stats = render (*p, 3.0, [] (juce::AudioBuffer<float>& b, int64_t pos) { music() (b, pos + 48000); }, &out, &head);
            juce::AudioBuffer<float> dry (2, out.getNumSamples());
            dry.clear();
            music() (dry, 48000);
            const auto d = diffRms (out, dry, 0.0, 2.2);
            const auto fine = stats.finite && stats.peak < 1.5f;
            if (fine) ++ok; else bad.add (juce::String (e.id));
            if (d > 0.004f) ++audible;
            else if (needsMore.count (e.id) == 0) silent.add (juce::String (e.id) + " (" + juce::String (d, 4) + ")");
            else ++quietOnes;
        }
        check (bad.isEmpty(), "all 110 playable moves stay finite and under control (" + juce::String (ok) + " fine" + (bad.isEmpty() ? juce::String() : ", trouble: " + bad.joinIntoString (" ")) + ")");
        check (silent.isEmpty(), "every move changes the sound when it plays (" + juce::String (audible) + " audible; " + juce::String (quietOnes) + " need the song around them"
               + (silent.isEmpty() ? juce::String() : "; silent: " + silent.joinIntoString (", ")) + ")");
    }

    section ("Moves that must be exact");
    {
        suite::SectionBus::publish (testMap());
        // snap to silence: every track silent for the last 16th before the drop (bar 25 starts at 48 s at 120 BPM)
        {
            auto p = make();
            everythingNeverBut (*p, { 101 });
            set (*p, TParams::role, 5.0f);
            p->replan (true);
            FakePlayHead head; head.bpm = 120.0; head.ppq = 4.0 * 23;   // bar 24
            p->setPlayHead (&head);
            juce::AudioBuffer<float> out;
            render (*p, 3.0, music(), &out, &head);
            // the drop is 4 quarters (2 s) in; the last 16th is 1.875-2.0 s
            const auto gap = rmsBetween (out, 1.885, 1.99), before = rmsBetween (out, 1.5, 1.8), after = rmsBetween (out, 2.02, 2.3);
            check (gap < 0.01f * before && after > 0.5f * before, "snap to silence: the last 16th before the drop is silent and the drop hits ("
                   + juce::String (juce::Decibels::gainToDecibels (gap / before), 1) + " dB)");
        }
        // beat-repeat: the last beat of a phrase repeats in eighths
        {
            auto p = make();
            everythingNeverBut (*p, { 7 });
            set (*p, TParams::role, 5.0f);
            p->replan (true);
            const auto plan = p->getPlan();
            const trm::Move* roll = nullptr;
            for (const auto& m : plan.moves) if (m.effect == 7 && m.slot == trm::sPhraseEnd) { roll = &m; break; }
            check (roll != nullptr, "beat-repeat rolls are planned at phrase ends");
            if (roll != nullptr)
            {
                FakePlayHead head; head.bpm = 120.0; head.ppq = roll->startQ - 2.0;
                p->setPlayHead (&head);
                juce::AudioBuffer<float> out;
                juce::Random rng (3);
                std::vector<float> noise ((size_t) (4 * sampleRate));
                for (auto& v : noise) v = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                render (*p, 3.0, [&] (juce::AudioBuffer<float>& b, int64_t pos)
                {
                    for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, noise[(size_t) (pos + i)]); b.setSample (1, i, noise[(size_t) (pos + i)]); }
                }, &out, &head);
                // the roll starts 1 s in; its first 1/8 (0.25 s) is live, the second repeats it
                double sum = 0.0, ref = 0.0;
                const auto s0 = (int) (1.0 * sampleRate) + 300, len = (int) (0.25 * sampleRate) - 600;
                for (int i = 0; i < len; ++i)
                {
                    const auto a = out.getSample (0, s0 + i), b = out.getSample (0, s0 + i + (int) (0.25 * sampleRate));
                    sum += (a - b) * (a - b); ref += a * a;
                }
                check (sum < 0.01 * ref, "beat-repeat: the second eighth repeats the first exactly");
            }
        }
        // reverse: the phrase's last beat plays the beat before it backwards
        {
            auto p = make();
            everythingNeverBut (*p, { 8 });
            set (*p, TParams::role, 5.0f);
            p->replan (true);
            const auto plan = p->getPlan();
            const trm::Move* rev = nullptr;
            for (const auto& m : plan.moves) if (m.effect == 8) { rev = &m; break; }
            if (rev != nullptr)
            {
                FakePlayHead head; head.bpm = 120.0; head.ppq = rev->startQ - 2.0;
                p->setPlayHead (&head);
                juce::AudioBuffer<float> out;
                juce::Random rng (5);
                std::vector<float> noise ((size_t) (4 * sampleRate));
                for (auto& v : noise) v = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                render (*p, 2.0, [&] (juce::AudioBuffer<float>& b, int64_t pos)
                {
                    for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, noise[(size_t) (pos + i)]); b.setSample (1, i, noise[(size_t) (pos + i)]); }
                }, &out, &head);
                const auto start = (int) (1.0 * sampleRate);
                double sum = 0.0, ref = 0.0;
                for (int i = 500; i < (int) (0.5 * sampleRate) - 500; ++i)
                {
                    const auto a = out.getSample (0, start + i), b = noise[(size_t) (start - 1 - i)];
                    sum += (a - b) * (a - b); ref += b * b;
                }
                check (sum < 0.01 * ref, "reverse: the last beat is the beat before it, backwards");
            }
            else check (false, "reverses are planned");
        }
    }

    section ("Level-matched: the moves change the sound, never the level");
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        set (*p, TParams::spice, 2.0f);
        set (*p, TParams::role, 4.0f);
        p->replan (true);
        FakePlayHead head; head.bpm = 120.0; head.ppq = 4.0 * 14;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> out;
        juce::Random rng (11);
        const auto stats = render (*p, 40.0, [&rng] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto v = 0.15f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.08f * (rng.nextFloat() * 2.0f - 1.0f);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        }, &out, &head);
        float worst = -100.0f;
        const auto inRms = std::sqrt (0.15f * 0.15f / 2.0f + 0.08f * 0.08f / 3.0f);
        for (double t = 1.0; t < 39.0; t += 1.0) worst = juce::jmax (worst, juce::Decibels::gainToDecibels (rmsBetween (out, t, t + 1.0) / inRms));
        check (stats.finite && worst < 1.0f, "40 s through a build and a drop at full spice: never more than 1 dB over the input (worst second " + juce::String (worst, 2) + " dB)");
    }

    section ("Cost");
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        set (*p, TParams::role, 4.0f);
        p->replan (true);
        FakePlayHead head; head.bpm = 138.0; head.ppq = 0.0;
        p->setPlayHead (&head);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (*p, 60.0, music(), nullptr, &head);
        const auto playing = 60.0 / ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
        auto idle = make();
        set (*idle, TParams::direct, 0.0f);
        FakePlayHead h2; h2.bpm = 138.0;
        idle->setPlayHead (&h2);
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        render (*idle, 60.0, music(), nullptr, &h2);
        const auto resting = 60.0 / ((juce::Time::getMillisecondCounterHiRes() - t1) / 1000.0);
        check (playing > 8.0 && resting > 20.0, "a minute of song: " + juce::String (juce::roundToInt (playing)) + "x real time with its plan, "
               + juce::String (juce::roundToInt (resting)) + "x with nothing planned (one core)");
    }

    section ("Remembering the first drop");
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        set (*p, TParams::direct, 0.0f);
        FakePlayHead head; head.bpm = 120.0; head.ppq = 4.0 * 24 - 1.0;
        p->setPlayHead (&head);
        render (*p, 10.0, music(), nullptr, &head);
        check (p->memoryBars.load() == 4, "the first drop's first 4 bars are kept for hook memories and the mirror drop (" + juce::String (p->memoryBars.load()) + " bars)");
    }

    section ("Autopilot switch, Mentalism's fixes, the bus and saved state");
    {
        suite::SectionBus::publish (testMap());
        // the Autopilot page's switch stops the planned moves
        {
            auto p = make();
            everythingNeverBut (*p, { 101 });
            p->replan (true);
            suite::Autopilot::set (suite::Autopilot::transmuteF, false);
            FakePlayHead head; head.bpm = 120.0; head.ppq = 4.0 * 23;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> out;
            render (*p, 3.0, music(), &out, &head);
            check (rmsBetween (out, 1.885, 1.99) > 0.05f, "with its Autopilot switch off, the planned moves don't play");
            suite::Autopilot::reset();
        }
        {
            suite::FixBus::reset();
            auto p = make();
            auto v = [&] (const juce::String& id) { return p->apvts.getRawParameterValue (id)->load(); };
            const auto b0 = v (TParams::builds), d0 = v (TParams::drops);
            const auto seq = suite::FixBus::send (suite::Fix::BuildRise);
            p->fixes->poll();
            check (v (TParams::builds) > b0 && suite::FixBus::acknowledgements (seq) == 1, "Mentalism's 'build rise' fix: the builds work harder");
            const auto seq2 = suite::FixBus::send (suite::Fix::BuildImpact, 0u);
            p->fixes->poll();
            check (v (TParams::drops) > d0, "'build impact': the drops do more");
            const auto gap = suite::FixBus::send (suite::Fix::BuildImpact, 1u);
            p->fixes->poll();
            check (suite::FixBus::acknowledgements (gap) == 0, "(the gap before the drop is Prima Materia's suggestion)");
            suite::FixBus::sendUndo (seq2); p->fixes->poll();
            suite::FixBus::sendUndo (seq); p->fixes->poll();
            check (std::abs (v (TParams::builds) - b0) < 0.001f && std::abs (v (TParams::drops) - d0) < 0.001f, "undo puts them back");
            suite::FixBus::reset();
        }
        {
            suite::TransmuteBus::reset();
            auto p = make();
            set (*p, TParams::role, 5.0f);
            p->replan (true);
            const auto plans = suite::TransmuteBus::read();
            check (plans.size() == 1 && plans[0].role == 5 && ! plans[0].moves.empty(), "each track's plan is shared (Ouroboros shows them all)");
            const auto before = p->getPlan().moves.size();
            suite::TransmuteBus::setSongSpice (0.0f);
            p->replan (false);
            check (p->getPlan().moves.empty() && before > 0, "Ouroboros' song spice at zero quiets every track");
            suite::TransmuteBus::setSongSpice (1.0f);
            const auto first = p->getPlan();
            suite::TransmuteBus::setSongSeed (4242u);
            p->replan (false);
            const auto second = p->getPlan();
            bool differs = first.moves.size() != second.moves.size();
            for (size_t i = 0; ! differs && i < first.moves.size(); ++i) differs = first.moves[i].effect != second.moves[i].effect;
            check (differs, "re-rolling the song (Ouroboros) gives every track new moves");
            suite::TransmuteBus::reset();
        }
        {
            auto p = make();
            set (*p, TParams::role, 5.0f);
            p->replan (true);
            p->setMode (12, trm::mAlways);
            p->setMode (21, trm::mNever);
            const auto plan = p->getPlan();
            trm::Move first;
            for (const auto& m : plan.moves) if ((m.slot & trm::bodySlots) == 0) { first = m; break; }
            p->lockMove (first);
            p->rerollMoment (first.key ^ 1u);
            juce::MemoryBlock state;
            p->getStateInformation (state);
            auto q = make();
            q->setStateInformation (state.getData(), (int) state.getSize());
            check (q->modeOf (12) == trm::mAlways && q->modeOf (21) == trm::mNever && q->isLocked (first.key) && q->modeOf (1) == trm::mAuto,
                   "Auto / Always / Never, locks and re-rolls are saved with the project");
            const auto log = suite::StyleLog::read();
            check (std::any_of (log.begin(), log.end(), [] (const suite::StyleLog::Entry& e) { return e.kind == "transmute" && e.get ("how") == "lock"; }),
                   "locks and Always go in the style log (signature moves in later songs)");
        }
    }


    section ("Following the real phrases, and printing the plan");
    {
        // 4: a phrase-end move sits in the hook's rest nearest the phrase's end, and steps off the vocal
        auto lead = baseInput (4); lead.spice = 2.0f;
        const auto plain = trm::makePlan (lead);
        lead.hookRests = { { 30 * 4.0 + 1.0, 30 * 4.0 + 3.0 } };   // a rest in bar 31, two beats long
        const auto placed = trm::makePlan (lead);
        bool inRest = false;
        for (const auto& m : placed.moves)
            if (m.slot == trm::sPhraseEnd && m.startQ >= 30 * 4.0f && m.startQ < 33 * 4.0f)
                inRest = inRest || (m.startQ >= 30 * 4.0f + 1.0f - 1.0e-3f && m.endQ <= 30 * 4.0f + 3.0f + 1.0e-3f);
        bool hadOne = false;
        for (const auto& m : plain.moves) hadOne = hadOne || (m.slot == trm::sPhraseEnd && m.startQ >= 31 * 4.0f && m.startQ < 33 * 4.0f);
        check (! hadOne || inRest, "a phrase-end move moves into the hook's rest near the phrase's end");
        auto sung = baseInput (4); sung.spice = 2.0f;
        for (double q = 0.0; q < 90 * 4.0; q += 1.0) sung.vocalNotes.push_back ({ q, q + 0.8 });   // the vocal on every beat
        const auto quiet = trm::makePlan (sung);
        float under = 0.0f, free = 0.0f; int nu = 0, nf = 0;
        for (const auto& m : quiet.moves) if (m.slot == trm::sPhraseEnd) { under += m.amount; ++nu; }
        for (const auto& m : trm::makePlan ([&] { auto x = sung; x.vocalNotes.clear(); return x; }()).moves) if (m.slot == trm::sPhraseEnd) { free += m.amount; ++nf; }
        check (nu == 0 || nf == 0 || under / (float) nu < free / (float) nf, "where it can't step off the vocal, a phrase-end move stays smaller");
        // 5: printing
        suite::SectionBus::publish (testMap());
        auto p = make();
        set (*p, TParams::role, 5.0f);
        p->replan (true);
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TransmutationPlan.mid");
        check (p->exportPlan (file), "the plan exports as MIDI");
        juce::MidiFile mf;
        int notes = 0, markers = 0;
        if (auto in = file.createInputStream(); in != nullptr && mf.readFrom (*in))
            for (int t = 0; t < mf.getNumTracks(); ++t)
                for (const auto* e : *mf.getTrack (t)) { notes += e->message.isNoteOn(); markers += e->message.isTextMetaEvent() && e->message.getMetaEventType() == 6; }
        int planned = 0;
        for (const auto& m : p->getPlan().moves) planned += (m.slot & trm::sSong) == 0;
        check (notes == planned && markers == planned && planned > 0, "a note and a named marker per move (" + juce::String (notes) + ")");
        file.deleteFile();
    }

    if (outDir != juce::File())
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        set (*p, TParams::role, 5.0f);
        FakePlayHead head; head.bpm = 138.0; head.ppq = 4.0 * 22;
        p->setPlayHead (&head);
        p->replan (true);
        render (*p, 1.0, music(), nullptr, &head);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("transmutation.png"));
        editor.reset();
    }

    suite::SectionBus::withdraw();
    testStyleLog.deleteFile();

    section ("Under the suite director: lead, support or rest as Ouroboros says");
    {
        auto free = baseInput (5);
        const auto own = trm::makePlan (free);
        int ownTransitions = 0;
        for (const auto& m : own.moves) if ((m.slot & trm::bodySlots) == 0) ++ownTransitions;
        auto rests = baseInput (5);
        rests.suiteActive = true;   // a live plan that gives this track no part anywhere
        const auto quiet = trm::makePlan (rests);
        int quietTransitions = 0;
        for (const auto& m : quiet.moves) if ((m.slot & trm::bodySlots) == 0) ++quietTransitions;
        check (ownTransitions > 0 && quietTransitions == 0, "with no part in the director's plan, no transition moves of its own ("
                                                            + juce::String (ownTransitions) + " without the director)");
        check (quiet.fakeDrop < 0, "the fake drop is the director's (its silences)");
        // a lead at the first drop, in the tape family, and a snap the moves stay out of
        auto lead = baseInput (5);
        lead.suiteActive = true;
        const auto map = lead.map;
        double dropQ = 0.0;
        for (int i = 0; i < map.count; ++i) if (map.sections[(size_t) i].type == suite::SectionType::Drop) { dropQ = (map.sections[(size_t) i].startBar - 1) * 4.0; break; }
        lead.suiteCues.push_back ({ dropQ - 4.0, dropQ, 1, trm::fTape, 0 });
        lead.suiteSilences.push_back ({ dropQ - 0.5, dropQ });
        const auto led = trm::makePlan (lead);
        bool tape = false, intoSnap = false;
        for (const auto& m : led.moves)
        {
            if ((m.slot & trm::bodySlots) != 0) continue;
            if (m.startQ >= dropQ - 4.01 && m.startQ < dropQ + 4.0 && (m.flags & trm::mfSuite)) tape = true;
            if (m.startQ < dropQ && m.endQ > dropQ - 0.5 + 1.0e-3) intoSnap = true;
        }
        check (tape, "the lead it's given: its own move at the first drop, marked as the director's");
        check (! intoSnap, "nothing plays into the director's snap");
        // you chose another track to lead: this one supports with one small move
        auto support = lead;
        support.suiteCues[0].heroRole = 6;
        bool big = false;
        for (const auto& m : trm::makePlan (support).moves) if ((m.slot & trm::bodySlots) == 0 && trm::effect (m.effect).size > 1) big = true;
        check (! big, "another track leads: only small supporting moves");
    }

    return finish();
}
