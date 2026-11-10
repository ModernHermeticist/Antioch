#pragma once

// A deliberately simple audition voice (soft detuned sine pad) so chords can be
// previewed inside the plugin. It is not meant as a production sound.

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

class PreviewSynth
{
public:
    /** Message thread: queue a new chord (replaces whatever is sounding). */
    void playChord (const std::vector<int>& pitches)
    {
        push ({ Command::AllOff, 0 });
        for (auto p : pitches)
            push ({ Command::NoteOn, p });
    }

    void stop() { push ({ Command::AllOff, 0 }); }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        attack = 1.0f / (0.02f * sr);
        release = 1.0f - std::exp (-1.0f / (0.2f * sr));
        for (auto& v : voices) v = {};
    }

    /** Audio thread: adds the preview into the buffer. */
    void render (float* left, float* right, int numSamples, float volume) noexcept
    {
        drainCommands();

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int n = 0; n < numSamples; ++n)
            {
                if (v.releasing) v.env -= release * v.env;
                else             v.env = std::min (1.0f, v.env + attack);

                const auto s = (std::sin (v.phase1) + 0.6f * std::sin (v.phase2)) * v.env * volume * 0.08f;
                v.phase1 += v.inc1; if (v.phase1 > twoPi) v.phase1 -= twoPi;
                v.phase2 += v.inc2; if (v.phase2 > twoPi) v.phase2 -= twoPi;
                left[n] += s;
                if (right != nullptr) right[n] += s;
            }

            if (v.releasing && v.env < 1.0e-4f)
                v.active = false;
        }
    }

private:
    struct Command { enum Type { NoteOn, AllOff } type; int note; };
    struct Voice
    {
        bool active = false, releasing = false;
        float env = 0.0f, phase1 = 0.0f, phase2 = 0.0f, inc1 = 0.0f, inc2 = 0.0f;
    };

    void push (Command c)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0) { commands[(size_t) start1] = c; fifo.finishedWrite (1); }
    }

    void drainCommands() noexcept
    {
        int start1, size1, start2, size2;
        const auto ready = fifo.getNumReady();
        fifo.prepareToRead (ready, start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i) apply (commands[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i) apply (commands[(size_t) (start2 + i)]);
        fifo.finishedRead (size1 + size2);
    }

    void apply (const Command& c) noexcept
    {
        if (c.type == Command::AllOff)
        {
            for (auto& v : voices) v.releasing = true;
            return;
        }

        for (auto& v : voices)
            if (! v.active || (v.releasing && v.env < 0.05f))
            {
                const auto hz = 440.0f * std::exp2 ((float) (c.note - 69) / 12.0f);
                v = {};
                v.active = true;
                v.inc1 = twoPi * hz / sr;
                v.inc2 = twoPi * hz * 1.004f / sr;
                return;
            }
    }

    static constexpr float twoPi = 6.28318530718f;
    std::array<Voice, 24> voices;
    std::array<Command, 128> commands;
    juce::AbstractFifo fifo { 128 };
    float sr = 44100.0f, attack = 0.001f, release = 0.001f;
};
