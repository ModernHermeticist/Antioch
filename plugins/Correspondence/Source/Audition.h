#pragma once

// 52: an audition renderer: any set of parts (a kit's drop, a family) played by a few plain
// voices into a WAV, for reviewing the library offline. Sketch sounds, not production ones:
// a sub sine for the bass, a soft saw for the arp and lead, a detuned pad for the chords.

#include <juce_audio_formats/juce_audio_formats.h>
#include "ChordEngine.h"

namespace chords::audition
{
    enum Voice { sub, pluck, pad, lead };

    /** Adds `notes` (quarters) at `bpm` into `out` with a voice. */
    inline void addPart (juce::AudioBuffer<float>& out, const std::vector<Note>& notes, double bpm, double sampleRate, Voice voice, float level)
    {
        const auto samplesPerQ = sampleRate * 60.0 / bpm;
        for (const auto& n : notes)
        {
            const auto start = (int) std::lround (n.start * samplesPerQ);
            const auto length = (int) std::lround (n.length * samplesPerQ);
            const auto hz = 440.0 * std::exp2 ((n.pitch - 69) / 12.0);
            const auto vel = level * (float) n.velocity / 127.0f;
            const auto release = (int) (0.03 * sampleRate);
            for (int k = 0; k < length + release; ++k)
            {
                const auto i = start + k;
                if (i < 0 || i >= out.getNumSamples()) break;
                const auto t = (double) k / sampleRate;
                const auto env = (float) (std::min (1.0, t / 0.004) * (k < length ? 1.0 : 1.0 - (double) (k - length) / release)
                                          * (voice == pluck ? std::exp (-t * 6.0) : voice == lead ? 0.9 : 1.0));
                const auto ph = std::fmod (t * hz, 1.0);
                float v;
                switch (voice)
                {
                    case sub:   v = (float) std::sin (juce::MathConstants<double>::twoPi * ph); break;
                    case pad:   v = 0.5f * (float) (std::sin (juce::MathConstants<double>::twoPi * ph) + std::sin (juce::MathConstants<double>::twoPi * std::fmod (t * hz * 1.004, 1.0))); break;
                    case pluck: case lead: default: v = (float) (2.0 * ph - 1.0) * 0.6f; break;
                }
                out.addSample (0, i, v * env * 0.8f);
                out.addSample (1, i, v * env * (voice == pad ? 0.6f : 0.8f));
            }
        }
    }

    /** A kit's (or the library's) drop for the progression in `s`: 8 bars of bass, arp, chords and
        the hook, written to `file` (24-bit WAV). Returns false if nothing could be written. */
    inline bool renderDrop (const juce::File& file, const Settings& s, const SongPatterns& sp, double bpm = 138.0, double sampleRate = 44100.0)
    {
        using T = suite::SectionType;
        const auto drop = (size_t) T::Drop;
        const auto lengthQ = 32.0;
        juce::AudioBuffer<float> out (2, (int) (lengthQ * 60.0 / bpm * sampleRate + sampleRate));
        out.clear();
        auto part = [&] (SongPart p, int choice) -> std::vector<Note>
        {
            const auto kind = lib::kindFor (p, choice, T::Drop);
            if (kind < 0) return {};
            const auto& f = songFamily (p, kind, sp, -1);
            if (f.name.empty()) return {};
            return lib::render (f, 2, false, s, lengthQ, sp.melody.octave);
        };
        addPart (out, part (SongPart::Bass, sp.bass[drop]), bpm, sampleRate, sub, 0.5f);
        addPart (out, part (SongPart::Arp, sp.arp[drop]), bpm, sampleRate, pluck, 0.25f);
        addPart (out, part (SongPart::Chords, sp.chords[drop]), bpm, sampleRate, pad, 0.15f);
        addPart (out, part (SongPart::Lead, 1), bpm, sampleRate, lead, 0.25f);
        const auto peak = out.getMagnitude (0, out.getNumSamples());
        if (peak <= 1.0e-6f) return false;
        out.applyGain (0.8f / peak);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (stream == nullptr) return false;
        auto* writer = wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0);
        if (writer == nullptr) return false;
        stream.release();
        std::unique_ptr<juce::AudioFormatWriter> w (writer);
        return w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    }
}
