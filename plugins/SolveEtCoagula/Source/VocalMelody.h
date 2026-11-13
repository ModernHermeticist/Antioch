#pragma once

// Vocal melody to MIDI: Solve et Coagula hears the vocal on its sidechain, so it can write
// down what it sings. The audio thread tracks the pitch (YIN, on the vocal decimated to 12 kHz,
// every ~21 ms) and hands each reading to the message thread with its song position; the
// message thread turns the readings into notes (a steady pitch for 60 ms or more), puts them
// on the 1/16 grid, in key, and keeps them by song position, so playing a part again
// replaces what was heard there before.
//
// From the notes: the melody itself, and a harmony a diatonic third above (in key), as MIDI
// for a lead or pad to double or answer the vocal.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SuiteDSP.h"
#include "Music.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

class VocalMelody
{
public:
    struct Reading { double q = 0.0; float midi = -1.0f; float level = 0.0f; };   // midi < 0: not a sung pitch
    struct Note
    {
        int pitch = 60; double start = 0.0, length = 0.25; int velocity = 100;
        float cents = 0.0f;       // how far the sung pitch sat from the note written (+ sharp)
        double sungStart = -1.0;  // the first pitch reading (quarters; ~43 ms after the note really started)
    };
    struct Issue { double q = 0.0; juce::String text; };

    /** 7: the vocal's tuning and timing, note by note: more than `centsLimit` off the note in
        key, or starting more than `msLimit` off the 16th grid. A check only: nothing is moved. */
    static std::vector<Issue> check (const std::vector<Note>& notes, double bpm, float centsLimit = 25.0f, double msLimit = 30.0)
    {
        std::vector<Issue> out;
        const auto msPerQ = 60000.0 / juce::jmax (1.0, bpm);
        for (const auto& n : notes)
        {
            juce::StringArray what;
            if (std::abs (n.cents) > centsLimit) what.add (juce::String (juce::roundToInt (std::abs (n.cents))) + " cents " + (n.cents > 0 ? "sharp" : "flat"));
            if (n.sungStart >= 0.0)
            {
                const auto ms = (n.sungStart - n.start) * msPerQ - 1000.0 * window / 12000.0;   // less the analysis window (~43 ms)
                if (std::abs (ms) > msLimit) what.add (juce::String (juce::roundToInt (std::abs (ms))) + " ms " + (ms > 0 ? "late" : "early"));
            }
            if (what.isEmpty()) continue;
            const auto bar = (int) std::floor (n.start / 4.0) + 1, beat = (int) std::floor (std::fmod (n.start, 4.0)) + 1;
            out.push_back ({ n.start, "bar " + juce::String (bar) + " beat " + juce::String (beat) + ", " + juce::MidiMessage::getMidiNoteName (n.pitch, true, true, 4)
                                      + ": " + what.joinIntoString (", ") });
        }
        return out;
    }

    /** 8: a guide for doubling the vocal: the same notes a little early or late (10-25 ms,
        alternating), a little softer, so a double sung to it sounds like a second take. */
    static std::vector<Note> doubles (const std::vector<Note>& melody, double bpm)
    {
        std::vector<Note> out;
        const auto qPerMs = juce::jmax (1.0, bpm) / 60000.0;
        int k = 0;
        for (auto n : melody)
        {
            const auto ms = (k++ % 2 == 0 ? 1.0 : -1.0) * (10.0 + 15.0 * std::fmod (k * 0.618, 1.0));
            n.start = juce::jmax (0.0, n.start + ms * qPerMs);
            n.velocity = juce::jmax (1, n.velocity - 10);
            out.push_back (n);
        }
        return out;
    }
    /** 8: a harmony a diatonic sixth below (the third above, an octave down): for a low double. */
    static std::vector<Note> harmonyBelow (const std::vector<Note>& melody, int root, int scale)
    {
        auto out = harmony (melody, root, scale);
        for (auto& n : out) n.pitch -= 12;
        return out;
    }

    static constexpr int decimation = 4, window = 512, maxTau = 180, minTau = 12;   // at 12 kHz: 66 Hz-1 kHz

    void prepare (double sampleRate)
    {
        rate = sampleRate / decimation;
        aa.set ((float) (rate * 0.4), 0.707f, (float) sampleRate);
        aa.reset();
        ring.assign ((size_t) window + maxTau + 1, 0.0f);
        written = 0; phase = 0; sinceHop = 0;
        diff.assign ((size_t) maxTau + 1, 0.0f);
    }

    /** Audio thread: one sample of vocal at song position `q` (quarters). */
    void process (float x, double q) noexcept
    {
        const auto y = aa.lowpass (x);
        if (++phase < decimation) return;
        phase = 0;
        ring[(size_t) (written % (int64_t) ring.size())] = y;
        ++written;
        if (++sinceHop < hop || written < (int64_t) ring.size()) return;
        sinceHop = 0;
        push ({ q, detect(), level });
    }

    /** Message thread: turns new readings into notes (call regularly; tests call it directly).
        `root`/`scale` (-1: no key known) put each note in key. */
    void collect (int root, int scale)
    {
        int s1, n1, s2, n2;
        const auto ready = fifo.getNumReady();
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) take (readings[(size_t) (s1 + i)], root, scale);
        for (int i = 0; i < n2; ++i) take (readings[(size_t) (s2 + i)], root, scale);
        fifo.finishedRead (n1 + n2);
    }

    const std::vector<Note>& notes() const { return heard; }
    void clear() { heard.clear(); current.clear(); }

    /** The diatonic third above each note, in key (needs a key; empty without one). */
    static std::vector<Note> harmony (const std::vector<Note>& melody, int root, int scale)
    {
        std::vector<Note> out;
        if (root < 0) return out;
        const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, scale)].steps;
        auto degreeOf = [&] (int pc) { for (size_t d = 0; d < steps.size(); ++d) if (steps[d] == pc) return (int) d; return -1; };
        for (auto n : melody)
        {
            const auto pc = ((n.pitch - root) % 12 + 12) % 12;
            const auto d = degreeOf (pc);
            if (d < 0) continue;
            const auto up = (d + 2) % (int) steps.size();
            auto interval = steps[(size_t) up] - steps[(size_t) d];
            if (interval <= 0) interval += 12;
            n.pitch += interval;
            n.velocity = juce::jmax (1, n.velocity - 12);
            out.push_back (n);
        }
        return out;
    }

    static juce::MidiFile toMidi (const std::vector<Note>& notes, double bpm, const juce::String& name)
    {
        constexpr int ppq = 960;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / juce::jmax (1.0, bpm))), 0.0);
        for (const auto& n : notes)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), n.start * ppq);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), (n.start + n.length) * ppq);
        }
        seq.updateMatchedPairs();
        juce::MidiFile f;
        f.setTicksPerQuarterNote (ppq);
        f.addTrack (seq);
        return f;
    }

private:
    static constexpr int hop = 256;

    float detect() noexcept
    {
        // YIN: cumulative mean normalised difference over the last `window` samples
        const auto size = (int64_t) ring.size();
        auto at = [&] (int64_t back) { return ring[(size_t) (((written - 1 - back) % size + size) % size)]; };
        double energy = 0.0;
        for (int i = 0; i < window; ++i) { const auto v = at (i); energy += (double) v * v; }
        level = (float) std::sqrt (energy / window);
        if (level < 0.005f) return -1.0f;   // too quiet to be singing
        float running = 0.0f;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double d = 0.0;
            for (int i = 0; i < window; ++i) { const auto e = at (i) - at (i + tau); d += (double) e * e; }
            running += (float) d;
            diff[(size_t) tau] = running > 0.0f ? (float) d * (float) tau / running : 1.0f;
        }
        for (int tau = minTau; tau < maxTau; ++tau)
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxTau && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                // parabolic interpolation around the dip
                const auto a = diff[(size_t) tau - 1], b = diff[(size_t) tau], c = diff[(size_t) tau + 1];
                const auto den = a - 2.0f * b + c;
                const auto shift = std::abs (den) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f;
                const auto hz = (float) rate / ((float) tau + shift);
                return 69.0f + 12.0f * std::log2 (hz / 440.0f);
            }
        return -1.0f;
    }

    void push (const Reading& r) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) readings[(size_t) s1] = r;
        else if (n2 > 0) readings[(size_t) s2] = r;
        fifo.finishedWrite (n1 + n2);
    }

    void take (const Reading& r, int root, int scale)
    {
        const auto steady = ! current.empty() && r.midi >= 0.0f && std::abs (r.midi - median()) < 0.8f && r.q >= current.back().q;
        if (steady) { current.push_back (r); return; }
        finish (root, scale);
        if (r.midi >= 0.0f) current.push_back (r);
    }

    float median() const
    {
        std::vector<float> m;
        for (const auto& c : current) m.push_back (c.midi);
        std::nth_element (m.begin(), m.begin() + (std::ptrdiff_t) (m.size() / 2), m.end());
        return m[m.size() / 2];
    }

    void finish (int root, int scale)
    {
        if (current.size() >= 3)   // about 60 ms of a steady pitch: a sung note
        {
            auto pitch = (int) std::lround (median());
            if (root >= 0)
            {
                const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, scale)].steps;
                auto inKey = [&] (int p) { return std::find (steps.begin(), steps.end(), ((p - root) % 12 + 12) % 12) != steps.end(); };
                if (! inKey (pitch)) pitch = inKey (pitch - 1) ? pitch - 1 : pitch + 1;
            }
            // The first reading comes ~43 ms into the note (the analysis window): start a little earlier
            const auto start = std::round (juce::jmax (0.0, current.front().q - 0.05) * 4.0) / 4.0;
            const auto end = std::round (current.back().q * 4.0) / 4.0;
            float peak = 0.0f;
            for (const auto& c : current) peak = juce::jmax (peak, c.level);
            Note n { pitch, start, juce::jmax (0.25, end - start), juce::jlimit (40, 127, (int) (60.0f + 300.0f * peak)) };
            n.cents = 100.0f * (median() - (float) pitch);
            n.sungStart = current.front().q;   // the first reading (the detector's latency is taken off in check())
            // Heard again: replaces what was there
            heard.erase (std::remove_if (heard.begin(), heard.end(), [&] (const Note& h) { return h.start < n.start + n.length - 1.0e-6 && h.start + h.length > n.start + 1.0e-6; }),
                         heard.end());
            if (heard.size() < 8000) heard.push_back (n);
            std::sort (heard.begin(), heard.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        }
        current.clear();
    }

    double rate = 12000.0;
    suite::Svf aa;
    std::vector<float> ring, diff;
    int64_t written = 0;
    int phase = 0, sinceHop = 0;
    float level = 0.0f;
    juce::AbstractFifo fifo { 4096 };
    std::array<Reading, 4096> readings {};
    std::vector<Reading> current;
    std::vector<Note> heard;
};
