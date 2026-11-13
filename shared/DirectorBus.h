#pragma once

// The director's plan, shared: Ouroboros publishes it (about once a second, and on every change);
// every plugin follows it on its audio thread through a Follower, which copies the plan only when
// it changes. A plan older than 5 s counts as gone, so every plugin falls back to its own
// behaviour when Ouroboros (or its director) is off.
//
// Also carried: what each plugin actually did at each moment (the render fidelity check) and
// what Mentalism measured per section (so the next plan learns from it).

#include "SharedMemory.h"
#include "SuiteDirector.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <vector>

namespace suite::DirectorBus
{
    constexpr int maxMoments = 128, maxBars = 512, maxSections = 32;

    struct Header
    {
        uint32_t hash = 0;
        int32_t count = 0, bars = 0;
        float barQ = 4.0f, bpm = 138.0f;
        uint8_t safe = 0, story = 0, genre = 0, abPrevious = 0;
        int32_t optOutCount = 0;
        uint32_t optOut[16] {};   // plugin instances you told to ignore the director (their own transitions)
    };

    namespace detail
    {
        struct Shared
        {
            std::atomic<uint32_t> seq, stampMs;
            Header header;
            director::Moment moments[maxMoments];
            float tension[maxBars];
            std::atomic<uint32_t> actedHash;
            std::atomic<uint32_t> acted[maxMoments];          // bit per player: it played its part here
            std::atomic<uint32_t> extras[director::numPlayers]; // transition gestures played outside the plan
            std::atomic<uint32_t> feedbackStamp;
            std::atomic<int32_t> feedbackCount;
            std::atomic<float> feedback[maxSections][3];
            std::atomic<int32_t> feedbackHeard[maxSections];
            std::atomic<double> playhead;   // Ouroboros: where the song is (for the strips)
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_director_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowMs() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count() | 1u;
        }
    }

    /** Ouroboros (message thread): the plan everyone follows. */
    inline void publish (const director::Plan& plan, double barQ, double bpm, bool abPrevious = false, const std::vector<uint32_t>& optOut = {}) noexcept
    {
        auto& s = detail::shared();
        s.seq.fetch_add (1, std::memory_order_acq_rel);   // odd: being written
        std::atomic_thread_fence (std::memory_order_release);
        Header h;
        h.hash = plan.hash;
        h.count = (int32_t) std::min<size_t> (plan.moments.size(), (size_t) maxMoments);
        h.bars = (int32_t) std::min<size_t> (plan.tension.size(), (size_t) maxBars);
        h.barQ = (float) barQ; h.bpm = (float) bpm;
        h.safe = plan.safe ? 1 : 0; h.story = (uint8_t) plan.story; h.genre = (uint8_t) plan.genre; h.abPrevious = abPrevious ? 1 : 0;
        h.optOutCount = (int32_t) std::min<size_t> (optOut.size(), 16);
        for (int i = 0; i < h.optOutCount; ++i) h.optOut[i] = optOut[(size_t) i];
        std::memcpy (&s.header, &h, sizeof (Header));
        if (h.count > 0) std::memcpy (s.moments, plan.moments.data(), sizeof (director::Moment) * (size_t) h.count);
        if (h.bars > 0) std::memcpy (s.tension, plan.tension.data(), sizeof (float) * (size_t) h.bars);
        std::atomic_thread_fence (std::memory_order_release);
        if (s.actedHash.load() != plan.hash)
        {
            for (auto& a : s.acted) a.store (0);
            for (auto& e : s.extras) e.store (0);
            s.actedHash.store (plan.hash);
        }
        s.stampMs.store (detail::nowMs());
        s.seq.fetch_add (1, std::memory_order_acq_rel);
    }
    /** Keeps a published plan alive without changing it. */
    inline void touch() noexcept { detail::shared().stampMs.store (detail::nowMs()); }
    inline void withdraw() noexcept { detail::shared().stampMs.store (0); }
    inline bool live (uint32_t maxAgeMs = 5000) noexcept
    {
        const auto st = detail::shared().stampMs.load();
        return st != 0 && detail::nowMs() - st < maxAgeMs;
    }

    /** A plugin played its part at moment `index` of plan `hash`. */
    inline void reportActed (uint32_t hash, int index, int player) noexcept
    {
        auto& s = detail::shared();
        if (index < 0 || index >= maxMoments || player < 0 || player >= director::numPlayers || s.actedHash.load() != hash) return;
        s.acted[index].fetch_or (1u << player);
    }
    /** A plugin played a transition gesture the plan didn't ask for. */
    inline void reportExtra (int player) noexcept
    {
        if (player >= 0 && player < director::numPlayers) detail::shared().extras[player].fetch_add (1);
    }
    inline uint32_t acted (int index) noexcept { return index >= 0 && index < maxMoments ? detail::shared().acted[index].load() : 0u; }
    inline uint32_t extras (int player) noexcept { return player >= 0 && player < director::numPlayers ? detail::shared().extras[player].load() : 0u; }
    inline void clearActed() noexcept { auto& s = detail::shared(); for (auto& a : s.acted) a.store (0); for (auto& e : s.extras) e.store (0); }

    /** Mentalism: what it measured in each section of the map. */
    inline void publishFeedback (const std::vector<director::SectionFeedback>& f) noexcept
    {
        auto& s = detail::shared();
        const auto n = (int) std::min<size_t> (f.size(), (size_t) maxSections);
        for (int i = 0; i < n; ++i)
        {
            s.feedback[i][0].store (f[(size_t) i].lufs); s.feedback[i][1].store (f[(size_t) i].first); s.feedback[i][2].store (f[(size_t) i].second);
            s.feedbackHeard[i].store (f[(size_t) i].heard ? 1 : 0);
        }
        s.feedbackCount.store (n);
        s.feedbackStamp.store (detail::nowMs());
    }
    inline std::vector<director::SectionFeedback> feedback (uint32_t maxAgeMs = 3600000u)
    {
        std::vector<director::SectionFeedback> out;
        auto& s = detail::shared();
        const auto st = s.feedbackStamp.load();
        if (st == 0 || detail::nowMs() - st > maxAgeMs) return out;
        for (int i = 0; i < std::min ((int) s.feedbackCount.load(), maxSections); ++i)
            out.push_back ({ s.feedback[i][0].load(), s.feedback[i][1].load(), s.feedback[i][2].load(), s.feedbackHeard[i].load() != 0 });
        return out;
    }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.stampMs.store (0); s.feedbackStamp.store (0); s.feedbackCount.store (0);
        clearActed();
    }

    //==============================================================================
    /** A plugin's copy of the plan (audio thread safe: refresh() once per block). */
    class Follower
    {
    public:
        /** Copies the plan if it changed; returns true while a plan is live. */
        bool refresh() noexcept
        {
            auto& s = detail::shared();
            const auto st = s.stampMs.load (std::memory_order_relaxed);
            if (st == 0 || detail::nowMs() - st > 5000u) { active = false; return false; }
            const auto before = s.seq.load (std::memory_order_acquire);
            if (before == seenSeq && active) return true;
            if ((before & 1u) != 0) return active;
            std::atomic_thread_fence (std::memory_order_acquire);
            std::memcpy (&header, &s.header, sizeof (Header));
            const auto n = juce::jlimit (0, maxMoments, (int) header.count), bars = juce::jlimit (0, maxBars, (int) header.bars);
            std::memcpy (moments, s.moments, sizeof (director::Moment) * (size_t) n);
            std::memcpy (tension, s.tension, sizeof (float) * (size_t) bars);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (s.seq.load (std::memory_order_acquire) != before) return active;   // written meanwhile: try next block
            count = n; barCount = bars;
            seenSeq = before;
            active = true;
            return true;
        }
        bool isActive() const noexcept { return active; }
        /** True when you told this plugin instance to ignore the director (it plays its own transitions). */
        bool optedOut (uint32_t instanceId) const noexcept
        {
            for (int i = 0; i < juce::jlimit (0, 16, (int) header.optOutCount); ++i) if (header.optOut[i] == instanceId) return true;
            return false;
        }
        /** Refresh, and live for this instance (not opted out). */
        bool refreshFor (uint32_t instanceId) noexcept { return refresh() && ! optedOut (instanceId); }
        int size() const noexcept { return active ? count : 0; }
        const director::Moment& operator[] (int i) const noexcept { return moments[juce::jlimit (0, maxMoments - 1, i)]; }
        const Header& info() const noexcept { return header; }
        uint32_t hash() const noexcept { return header.hash; }

        /** The moment where `player` acts at `q` (inside its gesture's window), or -1. */
        int actingAt (double q, int player) const noexcept
        {
            if (! active) return -1;
            for (int i = 0; i < count; ++i)
            {
                const auto& m = moments[i];
                if (! m.acts (player) || q < m.startQ - 8.0 * header.barQ || q > m.endQ + 1.0e-6) continue;
                const auto w = director::windowOf (m, player, m.gesture[(size_t) player], header.barQ, header.bpm);
                if (q >= w.first - 1.0e-9 && q < w.second) return i;
            }
            return -1;
        }
        /** The moment whose hit is at the bar line `q` falls on or just after (within `before` quarters ahead), or -1. */
        int momentNear (double q, double before, double after, uint16_t kinds = director::kAny) const noexcept
        {
            if (! active) return -1;
            for (int i = 0; i < count; ++i)
                if ((moments[i].kind & kinds) && q >= moments[i].atQ - before - 1.0e-9 && q < moments[i].atQ + after) return i;
            return -1;
        }
        /** The director's silence covers `q` (a snap, a breath, a kick-only bar or a fake drop's gap). */
        int silenceAt (double q) const noexcept
        {
            if (! active) return -1;
            for (int i = 0; i < count; ++i)
                if (moments[i].silence != director::gNone && q >= moments[i].silenceStartQ - 1.0e-9 && q < moments[i].silenceEndQ) return i;
            return -1;
        }
        /** The next moment with someone leading it, at or after `q` (-1 if none). */
        int nextAfter (double q) const noexcept
        {
            if (! active) return -1;
            for (int i = 0; i < count; ++i) if (moments[i].atQ >= q - 1.0e-6 && (moments[i].hero != director::pNone || moments[i].silence != director::gNone)) return i;
            return -1;
        }
        float tensionAt (double q) const noexcept
        {
            const auto b = (int) std::floor (q / juce::jmax (0.25f, header.barQ));
            return active && b >= 0 && b < barCount ? tension[b] : 0.0f;
        }
        /** Every plugin's line in the director strip: what it does at the next moment. */
        juce::String cueFor (double q, int player) const
        {
            if (! active) return {};
            // what it played last ("bar 81: played gate roll"), then what's next
            juce::String last;
            for (int i = count - 1; i >= 0; --i)
            {
                const auto& m = moments[i];
                if (m.endQ >= q || ! m.acts (player)) continue;
                const auto bar = (int) std::floor (m.atQ / juce::jmax (0.25f, header.barQ) + 1.0e-6) + 1;
                const auto did = (acted (i) & (1u << player)) != 0;
                last = "bar " + juce::String (bar) + (did ? ": played " : ": missed ") + juce::String (director::gesture (m.gesture[(size_t) player]).name).toLowerCase() + (did ? juce::String (juce::CharPointer_UTF8 (" \xe2\x9c\x93")) : juce::String()) + "   ";
                break;
            }
            const auto next = nextCueFor (q, player);
            return last + next;
        }
        juce::String nextCueFor (double q, int player) const
        {
            if (! active) return {};
            for (int i = 0; i < count; ++i)
            {
                const auto& m = moments[i];
                if (m.endQ < q) continue;
                if (! m.acts (player)) continue;
                const auto bar = (int) std::floor (m.atQ / juce::jmax (0.25f, header.barQ) + 1.0e-6) + 1;
                return "bar " + juce::String (bar) + ": " + (m.hero == player ? "you lead, " : "you support, ") + juce::String (director::gesture (m.gesture[(size_t) player]).name).toLowerCase();
            }
            return "no more moves for you in this song";
        }

    private:
        Header header;
        director::Moment moments[maxMoments];
        float tension[maxBars] {};
        int count = 0, barCount = 0;
        uint32_t seenSeq = 0xffffffffu;
        bool active = false;
    };

    /** Ouroboros (audio thread): where the song is now. */
    inline void setPlayhead (double ppq) noexcept { detail::shared().playhead.store (ppq, std::memory_order_relaxed); }
    inline double playhead() noexcept { return detail::shared().playhead.load (std::memory_order_relaxed); }

    namespace detail
    {
        inline Follower& messageFollower() { static Follower f; return f; }   // message thread only
    }
    /** Message thread: this player's line for its director strip ("bar 33: you lead, tape stop"). */
    inline juce::String cue (int player)
    {
        auto& f = detail::messageFollower();
        if (! f.refresh()) return {};
        return f.cueFor (playhead(), player);
    }
    enum MixCue { cueGeneral, cueRoom, cueStereo, cueSilence, cueLevel };
    /** Message thread: the next moment, as a plugin that only shapes the mix around it sees it. */
    inline juce::String mixCue (int what)
    {
        namespace dr = suite::director;
        auto& f = detail::messageFollower();
        if (! f.refresh()) return {};
        const auto q = playhead();
        for (int i = 0; i < f.size(); ++i)
        {
            const auto& m = f[i];
            if (m.endQ < q || (m.hero == dr::pNone && m.silence == dr::gNone)) continue;
            const auto bar = juce::String ((int) std::floor (m.atQ / juce::jmax (0.25f, f.info().barQ) + 1.0e-6) + 1);
            switch (what)
            {
                case cueRoom:
                    if (m.premixDb < -0.05f || m.hookDipDb < -0.05f || m.buildHoldDb > 0.05f)
                        return "bar " + bar + ": makes room for " + dr::playerName (m.hero) + " (" + juce::String (m.premixDb, 1) + " dB)"
                               + (m.hookDipDb < -0.05f ? ", then the hook's first bar back" : "") + (m.buildHoldDb > 0.05f ? ", build held " + juce::String (m.buildHoldDb, 1) + " dB" : "");
                    break;
                case cueStereo:
                    if (m.narrowBefore > 0.01f || m.wideOnHit > 0.01f)
                        return "bar " + bar + ": narrows " + juce::String (juce::roundToInt (m.narrowBefore * 100.0f)) + "% into it, wide on the one";
                    break;
                case cueSilence:
                    if (m.silence != dr::gNone) return "bar " + bar + ": " + juce::String (dr::gesture (m.silence).name).toLowerCase() + " (the director's silence)";
                    if (m.edit != dr::eNone) return "bar " + bar + ": " + dr::editName (m.edit) + " (suggested)";
                    break;
                case cueLevel:
                    if (m.buildHoldDb > 0.05f) return "bar " + bar + ": build held " + juce::String (m.buildHoldDb, 1) + " dB so the drop lands";
                    break;
                default:
                    return "bar " + bar + ", " + dr::kindName (m.kind) + ": " + (m.hero != dr::pNone ? juce::String (dr::playerName (m.hero)) + " leads" : juce::String ("silence"));
            }
        }
        return {};
    }

    /** Message thread: the director's tempo moves as tempo changes (quarters, BPM): a 2% ease
        through a breakdown that climbs back through the build. Empty without a plan. */
    inline std::vector<std::pair<double, double>> tempoChanges (double bpm)
    {
        namespace dr = suite::director;
        std::vector<std::pair<double, double>> out;
        auto& f = detail::messageFollower();
        if (! f.refresh()) return out;
        const auto barQ = (double) f.info().barQ;
        for (int i = 0; i < f.size(); ++i)
        {
            const auto& m = f[i];
            if (m.tempo != dr::tRamp || m.kind != dr::kIntoBreak) continue;
            double buildQ = -1.0, dropQ = -1.0;
            for (int k = i + 1; k < f.size(); ++k)
            {
                if (buildQ < 0.0 && f[k].kind == dr::kIntoBuild) buildQ = f[k].atQ;
                if (f[k].kind == dr::kIntoDrop) { dropQ = f[k].atQ; break; }
            }
            if (dropQ < 0.0) continue;
            if (buildQ < 0.0) buildQ = juce::jmax ((double) m.atQ + barQ, dropQ - 8.0 * barQ);
            const auto low = bpm * 0.98, easeEnd = m.atQ + 0.5 * (buildQ - m.atQ);
            for (auto q = (double) m.atQ; q < easeEnd - 1.0e-6; q += barQ) out.push_back ({ q, bpm + (low - bpm) * ((q - m.atQ) / juce::jmax (barQ, easeEnd - m.atQ)) });
            out.push_back ({ easeEnd, low });
            for (auto q = buildQ; q < dropQ - 1.0e-6; q += barQ) out.push_back ({ q, low + (bpm - low) * ((q - buildQ) / juce::jmax (barQ, dropQ - buildQ)) });
            out.push_back ({ dropQ, bpm });
        }
        return out;
    }
    /** Message thread: the plan's moments (a copy), for writing parts around them. */
    inline std::vector<director::Moment> moments (float* barQ = nullptr, uint32_t instanceId = 0)
    {
        std::vector<director::Moment> out;
        auto& f = detail::messageFollower();
        if (! f.refresh() || (instanceId != 0 && f.optedOut (instanceId))) return out;
        if (barQ != nullptr) *barQ = f.info().barQ;
        for (int i = 0; i < f.size(); ++i) out.push_back (f[i]);
        return out;
    }

    /** The window of a moment's gesture for `player` (from a Follower). */
    inline std::pair<double, double> windowFor (const Follower& f, int index, int player)
    {
        const auto& m = f[index];
        return director::windowOf (m, player, m.gesture[(size_t) juce::jlimit (0, director::numPlayers - 1, player)], f.info().barQ, f.info().bpm);
    }
}
