#pragma once

// Before and after: when a fix is made from the label-ready page (or by its autopilot), the
// master's last eight seconds are kept as "before"; the next time the playhead plays the same bar
// (you loop it, or play the song again), the eight seconds from there are kept as "after". If the
// bar doesn't come back within 20 seconds, "after" is the eight seconds that followed instead
// (said so). The pairs save as WAVs, to compare away from the session.

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <vector>

namespace lab
{
    class Snippets
    {
    public:
        struct Pair { juce::String what; juce::AudioBuffer<float> before, after; bool sameBar = false; };

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            ring.setSize (2, (int) (sampleRate * ringSeconds) + 1);
            ring.clear();
            writePos.store (0);
        }
        /** Audio thread: the master as it plays. */
        void push (const float* l, const float* r, int n) noexcept
        {
            const auto size = ring.getNumSamples();
            if (size <= 1) return;
            auto w = writePos.load();
            for (int i = 0; i < n; ++i)
            {
                ring.setSample (0, w, l[i]); ring.setSample (1, w, r[i]);
                if (++w >= size) w = 0;
            }
            writePos.store (w);
        }
        /** Message thread: a fix was made. */
        void markFix (const juce::String& what, double ppq, double barQ)
        {
            Pending p;
            p.what = what;
            p.before = last (clipSeconds);
            p.bar = (int) std::floor (ppq / juce::jmax (1.0, barQ));
            p.markMs = juce::Time::currentTimeMillis();
            pending = std::move (p);
            havePending = true;
        }
        /** Message thread, a few times a second. */
        void tick (bool playing, double ppq, double barQ)
        {
            if (! havePending) return;
            const auto now = juce::Time::currentTimeMillis();
            if (pending.dueMs > 0)
            {
                if (now >= pending.dueMs) finish();
                return;
            }
            if (! playing) return;
            const auto bar = (int) std::floor (ppq / juce::jmax (1.0, barQ));
            if (bar == pending.bar && now - pending.markMs > 2000) { pending.sameBar = true; pending.dueMs = now + (juce::int64) (clipSeconds * 1000.0); }
            else if (now - pending.markMs > 20000) { pending.sameBar = false; finish(); }
        }
        const std::vector<Pair>& pairs() const { return done; }
        /** Writes each pair as two WAVs into `folder`. Returns how many pairs. */
        int save (const juce::File& folder) const
        {
            folder.createDirectory();
            int k = 0;
            for (const auto& p : done)
            {
                ++k;
                const auto name = juce::File::createLegalFileName (juce::String (k) + " " + p.what.substring (0, 60));
                for (auto [buf, label] : { std::pair<const juce::AudioBuffer<float>*, const char*> { &p.before, " - before.wav" }, { &p.after, p.sameBar ? " - after.wav" : " - after (later in the song).wav" } })
                {
                    const auto f = folder.getChildFile (name + label);
                    f.deleteFile();
                    juce::WavAudioFormat fmt;
                    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
                    if (w != nullptr) w->writeFromAudioSampleBuffer (*buf, 0, buf->getNumSamples());
                }
            }
            return k;
        }
        double sampleRate() const { return sr; }

    private:
        struct Pending { juce::String what; juce::AudioBuffer<float> before; int bar = 0; juce::int64 markMs = 0, dueMs = 0; bool sameBar = false; };
        static constexpr double ringSeconds = 12.0, clipSeconds = 8.0;
        juce::AudioBuffer<float> ring;
        std::atomic<int> writePos { 0 };
        double sr = 44100.0;
        Pending pending;
        bool havePending = false;
        std::vector<Pair> done;

        juce::AudioBuffer<float> last (double seconds) const
        {
            const auto size = ring.getNumSamples();
            const auto n = juce::jlimit (0, juce::jmax (0, size - 1), (int) (seconds * sr));
            juce::AudioBuffer<float> out (2, juce::jmax (1, n));
            out.clear();
            if (size <= 1) return out;
            const auto end = writePos.load();
            for (int i = 0; i < n; ++i)
            {
                const auto idx = ((end - n + i) % size + size) % size;
                out.setSample (0, i, ring.getSample (0, idx)); out.setSample (1, i, ring.getSample (1, idx));
            }
            return out;
        }
        void finish()
        {
            Pair p;
            p.what = pending.what; p.before = pending.before; p.after = last (clipSeconds); p.sameBar = pending.sameBar;
            done.push_back (std::move (p));
            if (done.size() > 6) done.erase (done.begin());
            havePending = false;
            pending = {};
        }
    };
}
