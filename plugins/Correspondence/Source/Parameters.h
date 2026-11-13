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
    inline const juce::String songCounter  { "song_counter" };
    inline const juce::String songPhrase   { "song_phrase" };
    inline const juce::String leadExpression { "lead_expr" };    // exported leads: off / glide / glide + scoops (bend 2 or 12)
    inline const juce::String songClimb    { "song_climb" };     // drop 2+ chords: top note an octave up
    inline const juce::String songDouble   { "song_double" };
    inline const juce::String macroPlan    { "macro_plan" };     // how the Spire macros move: uplifting, progressive, big breakdowns
    inline const juce::String songMacros   { "song_macros" };    // Spire macro rides in song clips: off, CC 20-23, CC 102-105    // the final drop: lead doubled an octave up, bass a sub octave
    inline const juce::String songGhost    { "song_ghost" };
    inline const juce::String libOn        { "lib_on" };         // the pattern library writes the song's parts
    inline const juce::String libMood      { "lib_mood" };
    inline const juce::String libStyle     { "lib_style" };
    inline const juce::String libSeed      { "lib_seed" };       // which of the fitting families (New patterns rolls it)
    inline const juce::String libLead      { "lib_lead" };       // the lead hook from the library too
    inline const juce::String libKits      { "lib_kits" };       // kits: families chosen to go together
    /** The library's layers, from Piano on (SongPart order): each on or off. */
    inline const juce::StringArray& layerNames() { static const juce::StringArray n { "Piano", "Strings", "Brass", "Choir", "Timpani", "Acid", "Chops", "Candy" }; return n; }
    inline juce::String layerId (int i) { return "lib_layer_" + layerNames()[i].toLowerCase(); }
    /** The Writing menu's groups (each on or off). */
    inline const juce::StringArray& writingNames()
    {
        static const juce::StringArray n { "Phrase shaping (arc, climax, cadences, singable)", "Development (per section, foreshadowing, signatures)",
                                           "Harmony (tensions, passing chords, pedals, mixture, voicings)", "Feel (micro-timing, grooves, dynamics, articulation)",
                                           "Parts together (registers, counterpoint, answers, doublings)", "Arrangement (energy curves, entries, transitions, tutti)",
                                           "Ornaments (grace notes, mordents, trills)", "Your edits and favourites" };
        return n;
    }
    inline juce::String writingId (int i) { static const char* ids[] { "write_phrase", "write_develop", "write_harmony", "write_feel", "write_parts", "write_arrange", "write_ornaments", "write_learned" }; return ids[juce::jlimit (0, 7, i)]; }     // ghost notes in bass and arp exports
    inline const juce::String songFills    { "song_fills" };     // drum fill clip
    inline const juce::String songVocalRoom { "song_vocalroom" }; // the lead thins out where the vocal sings
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
