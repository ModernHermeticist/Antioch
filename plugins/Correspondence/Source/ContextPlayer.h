#pragma once

// Hear it in context: a generated part (or a take you're auditioning) played by a small synth in
// time with FL's playhead, under the song, while FL plays: no export, no drag. And the hum
// capture: the input recorded (decimated) into a FIFO for the message thread to pitch-track.

#include "ChordEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace chords
{
    class ContextPlayer
    {
    public:
        /** Message thread: what to play (song quarters), or nothing. */
        void setNotes (std::vector<Note> notes)
        {
            auto next = std::make_shared<const std::vector<Note>> (std::move (notes));
            const juce::SpinLock::ScopedLockType sl (lock);
            pending = next;
        }
        void clear() { setNotes ({}); }
        std::atomic<float> level { 0.5f };
        std::atomic<bool> enabled { false };

        void prepare (double sampleRate) { sr = sampleRate; for (auto& v : voices) v = {}; lastPpq = -1.0; }

        /** Audio thread: adds the part at the playhead (ppq at the block's start, quarters per sample). */
        void render (float* left, float* right, int n, bool playing, double ppq, double qPerSample) noexcept
        {
            {
                const juce::SpinLock::ScopedTryLockType sl (lock);
                if (sl.isLocked() && pending != nullptr) { current = pending; pending.reset(); }
            }
            if (! enabled.load() || current == nullptr || current->empty())
            {
                for (auto& v : voices) v.on = false;
                lastPpq = -1.0;
                return;
            }
            if (! playing) { for (auto& v : voices) v.released = true; lastPpq = -1.0; }
            else
            {
                if (lastPpq >= 0.0 && ppq < lastPpq - 0.01) for (auto& v : voices) v.released = true;   // a loop or a jump: let go
                const auto end = ppq + qPerSample * n;
                for (const auto& note : *current)
                {
                    if (note.start >= ppq - 1.0e-9 && note.start < end) start (note, (int) ((note.start - ppq) / juce::jmax (1.0e-12, qPerSample)), qPerSample);
                }
                lastPpq = end;
            }
            const auto lvl = level.load() * 0.25f;
            for (int i = 0; i < n; ++i)
            {
                float out = 0.0f;
                for (auto& v : voices)
                {
                    if (! v.on) continue;
                    if (v.delay > 0) { --v.delay; continue; }
                    if (! v.released && --v.samplesLeft <= 0) v.released = true;
                    v.env += v.released ? -v.env * 0.0008f : (1.0f - v.env) * 0.01f;
                    if (v.released && v.env < 1.0e-4f) { v.on = false; continue; }
                    v.phase += v.inc; if (v.phase >= 1.0f) v.phase -= 1.0f;
                    const auto saw = 2.0f * v.phase - 1.0f;
                    v.lp += 0.18f * (saw - v.lp);   // a soft saw
                    out += v.lp * v.env * v.gain;
                }
                out *= lvl;
                left[i] += out;
                if (right != nullptr) right[i] += out;
            }
        }

    private:
        struct Voice { bool on = false, released = false; int delay = 0, samplesLeft = 0; float phase = 0.0f, inc = 0.0f, env = 0.0f, lp = 0.0f, gain = 0.0f; };
        std::array<Voice, 16> voices {};
        std::shared_ptr<const std::vector<Note>> current, pending;
        juce::SpinLock lock;
        double sr = 44100.0, lastPpq = -1.0;
        void start (const Note& n, int offset, double qPerSample) noexcept
        {
            Voice* v = nullptr;
            for (auto& x : voices) if (! x.on) { v = &x; break; }
            if (v == nullptr) v = &voices[0];
            *v = {};
            v->on = true; v->delay = juce::jmax (0, offset);
            v->samplesLeft = (int) (n.length / juce::jmax (1.0e-12, qPerSample));
            v->inc = (float) (440.0 * std::pow (2.0, (n.pitch - 69) / 12.0) / sr);
            v->gain = (float) n.velocity / 127.0f;
        }
    };

    /** The hum capture: the input decimated to about 22 kHz into a FIFO the message thread reads. */
    class HumCapture
    {
    public:
        std::atomic<bool> armed { false };
        void prepare (double sampleRate) { sr = sampleRate; decim = juce::jmax (1, (int) std::round (sampleRate / 22050.0)); count = 0; acc = 0.0f; }
        double rate() const { return sr / decim; }
        /** Audio thread. */
        void push (const float* x, int n) noexcept
        {
            if (! armed.load() || x == nullptr) return;
            for (int i = 0; i < n; ++i)
            {
                acc += x[i];
                if (++count >= decim)
                {
                    const auto v = acc / (float) decim;
                    acc = 0.0f; count = 0;
                    int s1, n1, s2, n2;
                    fifo.prepareToWrite (1, s1, n1, s2, n2);
                    if (n1 > 0) data[(size_t) s1] = v; else if (n2 > 0) data[(size_t) s2] = v;
                    fifo.finishedWrite (n1 + n2);
                }
            }
        }
        /** Message thread: everything captured since the last call. */
        void drain (std::vector<float>& into)
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
            into.insert (into.end(), data.begin() + s1, data.begin() + s1 + n1);
            if (n2 > 0) into.insert (into.end(), data.begin() + s2, data.begin() + s2 + n2);
            fifo.finishedRead (n1 + n2);
        }
    private:
        static constexpr int capacity = 1 << 18;   // about 12 s at 22 kHz between drains
        juce::AbstractFifo fifo { capacity };
        std::vector<float> data = std::vector<float> ((size_t) capacity, 0.0f);
        double sr = 44100.0;
        int decim = 2, count = 0;
        float acc = 0.0f;
    };
}
