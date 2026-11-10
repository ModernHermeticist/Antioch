#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChordEngine.h"

namespace Params
{
    inline const juce::String rootNote     { "root" };
    inline const juce::String scale        { "scale" };
    inline const juce::String broadcast    { "broadcast" };
    inline const juce::String acceptDetected { "acceptdetected" };   // adopt keys suggested by Emerald Tablet
    inline const juce::String count        { "count" };
    inline const juce::String perChord     { "perchord" };
    inline const juce::String voicing      { "voicing" };
    inline const juce::String voiceLead    { "voicelead" };
    inline const juce::String registerOct  { "register" };
    inline const juce::String addBass      { "addbass" };
    inline const juce::String chordPattern { "chordpat" };
    inline const juce::String bassPattern  { "basspat" };
    inline const juce::String bassOctave   { "bassoct" };
    inline const juce::String arpPattern   { "arppat" };
    inline const juce::String arpOctaves   { "arpoct" };
    inline const juce::String preview      { "preview" };
    inline const juce::String melodyStyle  { "mel_style" };
    inline const juce::String melodySeed   { "mel_seed" };
    inline const juce::String melodyOctave { "mel_octave" };
    inline const juce::String songLift     { "song_lift" };
    inline const juce::String songVary     { "song_vary" };   // later drops: embellished lead + harmony line
    inline const juce::String songBreakdown { "song_bdstart" }; // breakdown progression: same / start on VI / start on IV
    inline const juce::String humanise     { "humanise" };
    inline const juce::String songCounter  { "song_counter" };   // counter-melody wherever the full lead plays
    inline const juce::String groove       { "groove" };         // exported MIDI feel: straight / trance shuffle / hard shuffle
    inline const juce::String accentCC     { "accent_cc" };      // write accents as CC 74 too
    inline float grooveSwing (int index) { static const float s[] = { 0.0f, 0.15f, 0.3f }; return s[juce::jlimit (0, 2, index)]; }    // exported MIDI: accents and small timing/velocity variation

    inline juce::String slotDegree (int slot)  { return "slot" + juce::String (slot + 1) + "_deg"; }
    inline juce::String slotQuality (int slot) { return "slot" + juce::String (slot + 1) + "_qual"; }

    double quartersPerChord (int index);

    /** Full-song pattern choices, per section type (0..5) and part. */
    inline juce::String songId (chords::SongPart part, int sectionType)
    {
        static const char* names[] = { "song_chords_", "song_bass_", "song_arp_", "song_lead_" };
        return names[(int) part] + juce::String (sectionType);
    }
    chords::SongPatterns readSongPatterns (const juce::AudioProcessorValueTreeState&);
    chords::MelodySettings readMelody (const juce::AudioProcessorValueTreeState&);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Reads all parameters into a chords::Settings snapshot. */
    chords::Settings readSettings (const juce::AudioProcessorValueTreeState&);
}
