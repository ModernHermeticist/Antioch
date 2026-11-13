#pragma once

// Kick pattern sharing: a Kybalion on the kick track learns where the kick hits within
// the bar (per section type) and shares that pattern; every other Kybalion can duck to it
// with no sidechain routing. Because followers duck from the pattern and the song position
// rather than live audio, it doesn't matter which order the host processes the tracks in.

#include "SectionBus.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace suite
{
    struct KickPattern
    {
        static constexpr int maxHits = 16;
        int count = 0;                       // hits per bar
        std::array<float, maxHits> positions {};   // quarter notes from the bar start, ascending
        int kickBars = 0, silentBars = 0;    // bars heard with / without a kick
        bool heard() const noexcept { return kickBars + silentBars > 0; }
        bool kicks() const noexcept { return kickBars > 0 && kickBars >= silentBars; }
    };

    struct KickPatternSet
    {
        static constexpr int anySection = (int) SectionType::count;   // also used when there's no map
        bool valid = false;
        std::array<KickPattern, (size_t) SectionType::count + 1> bySection {};
        float bodyHz = 0.0f;   // the kick's body pitch, measured on the kick track (0 = not known yet)

        /** The pattern to duck to in section `type` (-1 = unknown): that section's if it was heard, else any. */
        const KickPattern* patternFor (int type) const noexcept
        {
            if (! valid) return nullptr;
            if (type >= 0 && type < anySection && bySection[(size_t) type].heard())
                return &bySection[(size_t) type];
            return bySection[(size_t) anySection].heard() ? &bySection[(size_t) anySection] : nullptr;
        }

        /** Milliseconds until the next hit after `phaseQ` within the bar, or 1e9 when there's no kick. */
        float msUntilHit (int type, double phaseQ, double barQ, double msPerQ) const noexcept
        {
            const auto* p = patternFor (type);
            if (p == nullptr || ! p->kicks() || p->count == 0) return 1.0e9f;
            for (int i = 0; i < p->count; ++i)
                if (p->positions[(size_t) i] > phaseQ + 1.0e-9) return (float) ((p->positions[(size_t) i] - phaseQ) * msPerQ);
            return (float) ((p->positions[0] + barQ - phaseQ) * msPerQ);   // the next bar's first hit
        }
        /** Milliseconds since the most recent hit at `phaseQ` within the bar, or 1e9 when there's no kick. */
        float msSinceHit (int type, double phaseQ, double barQ, double msPerQ) const noexcept
        {
            const auto* p = patternFor (type);
            if (p == nullptr || ! p->kicks() || p->count == 0) return 1.0e9f;
            double last = -1.0;
            for (int i = 0; i < p->count; ++i)
                if (p->positions[(size_t) i] <= phaseQ + 1.0e-9) last = p->positions[(size_t) i];
            if (last < 0.0) last = p->positions[(size_t) p->count - 1] - barQ;   // the previous bar's last hit
            return (float) ((phaseQ - last) * msPerQ);
        }
    };

    namespace KickBus
    {
        void publish (const KickPatternSet&) noexcept;
        KickPatternSet read (int maxAgeSeconds = 30) noexcept;
        void withdraw() noexcept;
    }

    /** Audio thread, on the kick track: finds kick onsets and turns each finished bar into
        the pattern for that bar's section type. */
    class KickLearner
    {
    public:
        void prepare (double sampleRate)
        {
            attack = 1.0f - std::exp (-1.0f / (0.0002f * (float) sampleRate));
            release = 1.0f - std::exp (-1.0f / (0.06f * (float) sampleRate));
            env = 0.0f;
            armed = true;
            currentBar = -1;
            pending.clear();
            pending.reserve (64);
        }

        /** One sample of the kick track at song position `ppq` in section `type` (-1 = no map). */
        void process (float x, double ppq, double barQ, int type, float thresholdGain) noexcept
        {
            const auto a = std::abs (x);
            env += (a > env ? attack : release) * (a - env);

            const auto bar = (int64_t) std::floor (ppq / barQ + 1.0e-9);
            if (bar != currentBar)
            {
                if (currentBar >= 0 && bar == currentBar + 1)
                    finishBar();
                pending.clear();
                currentBar = bar;
                currentType = type;
            }

            if (armed && env > thresholdGain)
            {
                armed = false;
                if (pending.size() < (size_t) KickPattern::maxHits)
                    pending.push_back ((float) (ppq - (double) bar * barQ));
            }
            else if (! armed && env < thresholdGain * 0.5f)
                armed = true;
        }

        /** Playback stopped or jumped: forget the bar in progress. */
        void interrupt() noexcept { currentBar = -1; pending.clear(); }

        const KickPatternSet& patterns() const noexcept { return set; }
        int barsLearned() const noexcept { return learned; }

        /** The kick's measured body pitch, shared with the pattern from the next bar on. */
        void setBodyHz (float hz) noexcept { set.bodyHz = hz; }

    private:
        void finishBar() noexcept
        {
            auto store = [this] (int slot)
            {
                auto& p = set.bySection[(size_t) slot];
                if (pending.empty()) { ++p.silentBars; return; }
                ++p.kickBars;
                p.count = (int) pending.size();
                for (size_t i = 0; i < pending.size(); ++i) p.positions[i] = pending[i];
            };
            store (currentType >= 0 && currentType < KickPatternSet::anySection ? currentType : KickPatternSet::anySection);
            if (currentType >= 0 && currentType < KickPatternSet::anySection && ! pending.empty())
                store (KickPatternSet::anySection);
            set.valid = true;
            ++learned;
            KickBus::publish (set);
        }

        float attack = 0.1f, release = 0.001f, env = 0.0f;
        bool armed = true;
        int64_t currentBar = -1;
        int currentType = -1, learned = 0;
        std::vector<float> pending;
        KickPatternSet set;
    };
}
