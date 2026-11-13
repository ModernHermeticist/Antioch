#pragma once

// Transmutation's catalogue: all 118 moves, what each needs (role, the kind of audio) and where
// in a song it can happen. The director (Director.h) picks from this; the engine (Recipes.h)
// plays each one. Numbers match the list the suite was designed from.

#include <array>
#include <cstdint>

namespace trm
{
    // ---- Roles (as suite::Role) as bits: bit 0 = unknown ----
    enum : uint8_t { rU = 1, rK = 2, rB = 4, rH = 8, rP = 16, rL = 32, rV = 64, rF = 128 };
    constexpr uint8_t rAll = 255, rNoKick = 253, rMel = rP | rL | rV | rF | rU, rPitched = rB | rP | rL | rV | rU,
                      rSing = rP | rL | rV | rU, rNoDrums = rB | rP | rL | rV | rF | rU;
    inline uint8_t roleBit (int role) noexcept { return role >= 1 && role <= 7 ? (uint8_t) (1u << role) : (uint8_t) rU; }

    // ---- What the audio has to be ----
    enum : uint8_t { nPitched = 1, nSustained = 2, nPlucky = 4, nBusy = 8, nSparse = 16 };

    // ---- Where in the song (slot kinds) ----
    enum Slot : uint32_t
    {
        sBuildBody = 1u << 0,   // a whole build
        sBuildEnd = 1u << 1,    // the last bar of a build
        sDropStart = 1u << 2,   // the first bar of a drop
        sDropBody = 1u << 3,    // a whole drop
        sBreakStart = 1u << 4,  // the first bars of a breakdown
        sBreakPeak = 1u << 5,   // the last 2 bars of a breakdown (its peak, before the build)
        sBreakBody = 1u << 6,   // a whole breakdown
        sPhraseEnd = 1u << 7,   // the last bar of an 8/16-bar phrase inside a section
        sSectionEnd = 1u << 8,  // the last bar before a section change (not a build's)
        sOutro = 1u << 9,       // the outro
        sFinalDrop = 1u << 10,  // the whole last drop
        sKeyChange = 1u << 11,  // the last beat before the lifted (key-change) drop
        sGrooveBody = 1u << 12, // grooves and the intro
        sIntoBreak = 1u << 13,  // the last beat before a breakdown
        sFinalEnd = 1u << 14,   // the last phrase of the final drop
        sBuildStart = 1u << 15, // the first 2 bars of a build
        sSong = 1u << 16,       // the whole song (a track's character)
        sNone = 0u
    };
    constexpr uint32_t bodySlots = sBuildBody | sDropBody | sBreakBody | sOutro | sFinalDrop | sGrooveBody | sSong;

    enum Category { cBuild, cRhythm, cPitch, cSpace, cExpression, cWild, cDirector, cTransition, cRole, cPhysics, numCategories };
    inline const char* categoryName (int c)
    {
        static const char* n[] = { "Builds & drops", "Rhythm & glitch", "Pitch & harmony", "Space & texture", "Expression",
                                   "No limits", "The director", "Transitions", "Per role", "Physics" };
        return c >= 0 && c < numCategories ? n[c] : "";
    }

    // ---- Families: what kind of gesture (variety, call and response, contagion, motifs) ----
    enum Family : uint8_t { fRepeat, fReverse, fPitch, fFilter, fSpace, fGrain, fStereo, fTape, fLevel, fColour, fHarmony, fDirector, numFamilies };

    // ---- Genres ----
    enum Genre { gAuto, gUplifting, gTech, gPsy, gProgressive, gOrchestral, numGenres };
    inline const char* genreName (int g)
    {
        static const char* n[] = { "Auto (from the tempo)", "Uplifting", "Tech trance", "Psy", "Progressive", "Orchestral" };
        return g >= 0 && g < numGenres ? n[g] : "";
    }
    // tags: which genres love a move
    enum : uint8_t { tUp = 1, tTech = 2, tPsy = 4, tProg = 8, tOrch = 16 };

    struct Effect
    {
        int id;
        const char* name;
        const char* what;
        int category;
        uint8_t roles, needs;
        uint32_t slots;
        float weight;      // how often the director reaches for it (1 = normal)
        uint8_t size;      // 1 small, 2 medium, 3 big gesture
        uint8_t family;
        uint8_t tags;
        bool optIn;        // off (Never) until you switch it to Auto or Always
        uint8_t group;     // moves in the same group never overlap on a track (0 = none)
    };

    constexpr int numEffects = 118;

    inline const std::array<Effect, numEffects>& effects()
    {
        static const std::array<Effect, numEffects> e { {
            // ---- A. Builds, drops and breakdowns ----
            { 1, "Role-aware build", "Each role builds its own way: bass steps aside, pads open and widen, the lead's echoes grow, drums roll, FX rise", cBuild, rAll, 0, sBuildBody, 3.0f, 2, fFilter, tUp | tTech | tPsy | tProg | tOrch, false, 1 },
            { 2, "Layered emptying", "High-pass rising at a different rate on each track, so the build drains from the bottom up", cBuild, rAll, 0, sBuildBody, 1.6f, 2, fFilter, tUp | tProg, false, 1 },
            { 3, "Shepard rise", "An endless rise made from the track's own audio, tuned to the key, under the build's last bars", cBuild, rPitched, nPitched, sBuildBody | sBuildEnd, 0.9f, 3, fPitch, tUp | tOrch, false, 0 },
            { 4, "Staggered tape-stop", "The bass winds down first, then the pads, then the lead, a 16th apart, into the breakdown", cBuild, rB | rP | rL | rF | rU, 0, sIntoBreak, 1.2f, 3, fTape, tTech | tPsy | tUp, false, 0 },
            { 5, "The drop's first 16th", "Everything but kick and bass sits out the drop's first 16th, then hits", cBuild, rH | rP | rL | rV | rF | rU, 0, sDropStart, 1.0f, 1, fLevel, tTech | tUp, false, 2 },
            { 6, "Stereo bloom", "The drop's first hit flares wide, then settles", cBuild, rH | rMel, 0, sDropStart, 1.3f, 2, fStereo, tUp | tProg, false, 0 },
            { 31, "Inhale", "Each track pulls back in level and brightness over the last beat, then exhales on the one", cBuild, rAll, 0, sBuildEnd, 1.2f, 2, fLevel, tUp | tProg | tOrch, false, 0 },
            { 32, "Rewind", "The breakdown's last bar scrubs backwards at speed, like a DJ rewind", cBuild, rH | rMel, 0, sBreakPeak, 0.6f, 3, fTape, tTech | tPsy, false, 3 },
            { 33, "Build squeeze", "Compression tightens bar by bar through the build and lets go on the drop", cBuild, rAll, 0, sBuildBody, 1.0f, 1, fColour, tTech | tUp, false, 0 },
            { 34, "Fake drop", "On one chosen drop, a bar of the breakdown's texture plays where the drop should hit, then the real drop lands", cBuild, rAll, 0, sNone, 1.0f, 3, fLevel, tUp | tTech | tPsy, false, 2 },
            { 35, "Snare-roll sync", "The track's tremolo locks to the snare roll's speed (1/8, 1/16, 1/32) through the build", cBuild, rB | rL | rV | rF | rU, 0, sBuildBody, 0.8f, 2, fRepeat, tUp | tTech, false, 4 },
            { 36, "Riser from the chord", "Pads turn the current chord into their own riser, rising in pitch and brightness", cBuild, rP | rU, nPitched, sBuildBody, 1.0f, 2, fPitch, tUp | tOrch, false, 0 },
            { 37, "Drop echo", "The drop's first hit echoes out across the first bar, filtered and fading", cBuild, rH | rMel, 0, sDropStart, 1.1f, 2, fSpace, tUp | tProg | tPsy, false, 0 },
            { 38, "Breakdown swallow", "The breakdown's first beat is sucked into a reverse swell it fades in out of", cBuild, rNoKick, 0, sBreakStart, 0.9f, 2, fReverse, tUp | tProg, false, 3 },
            { 39, "Final-drop overdrive", "In the last drop only: a little extra saturation, width and air, everything at 110%", cBuild, rAll, 0, sFinalDrop, 1.4f, 2, fColour, tUp | tTech | tPsy, false, 0 },
            { 40, "Outro dissolve", "The outro breaks the track into grains and echoes, thinning out bar by bar", cBuild, rH | rMel, 0, sOutro, 1.4f, 2, fGrain, tProg | tUp, false, 0 },

            // ---- B. Rhythm and glitch ----
            { 7, "Beat-repeat roll", "The phrase's last beat repeats in 1/8, 1/16, then 1/32 notes", cRhythm, rAll, 0, sPhraseEnd | sBuildEnd | sSectionEnd, 1.6f, 2, fRepeat, tTech | tPsy | tUp, false, 3 },
            { 8, "Reverse the last beat", "The phrase's last beat plays backwards into the next", cRhythm, rH | rMel, 0, sPhraseEnd | sSectionEnd, 1.3f, 2, fReverse, tTech | tProg | tUp, false, 3 },
            { 9, "Polyrhythmic gate", "Gates run 3-against-4 or Euclidean, different on each track, locking back together on the drop", cRhythm, rH | rMel, 0, sBuildBody | sBreakBody, 0.8f, 2, fRepeat, tTech | tPsy | tProg, false, 4 },
            { 10, "Kick-aware slicer", "On fills, slices reshuffle but anything on a kick stays put", cRhythm, rH | rMel, nBusy, sPhraseEnd | sSectionEnd, 0.8f, 2, fRepeat, tTech | tPsy, false, 3 },
            { 11, "Halftime bar", "The last bar before the drop plays at half speed", cRhythm, rB | rH | rMel, 0, sBuildEnd, 0.7f, 3, fTape, tTech | tPsy, false, 3 },
            { 41, "Swing drift", "The groove gains swing through the breakdown and snaps straight on the drop", cRhythm, rH | rP | rL | rU, nBusy, sBreakBody, 0.6f, 1, fRepeat, tProg | tTech, false, 0 },
            { 42, "Hook stutter", "The stutter's rhythm copies the track's own hook", cRhythm, rP | rL | rU, nBusy, sPhraseEnd | sBuildEnd, 0.9f, 2, fRepeat, tTech | tPsy | tUp, false, 4 },
            { 43, "Buffer shuffle", "1/16 slices of the last bar reordered into a new fill, always landing on the downbeat", cRhythm, rH | rMel, 0, sPhraseEnd | sSectionEnd, 1.0f, 2, fRepeat, tTech | tPsy, false, 3 },
            { 44, "Off-beat crush", "Bit-crush accents that hit only on the off-beats in drops", cRhythm, rH | rP | rL | rU, 0, sDropBody, 0.6f, 1, fColour, tTech | tPsy, false, 0 },
            { 45, "Ghost triplets", "A quiet triplet echo under the straight groove in builds", cRhythm, rH | rP | rL | rU, 0, sBuildBody | sGrooveBody, 0.7f, 1, fSpace, tProg | tTech, false, 0 },
            { 46, "Glitch dropouts", "Micro-silences (1/32) over a phrase's last two beats, never on the downbeat", cRhythm, rNoKick, 0, sPhraseEnd, 0.8f, 1, fLevel, tTech | tPsy, false, 3 },
            { 47, "Kick-shaped tremolo", "The track pulses with the kick's real pattern and shape", cRhythm, rNoDrums, 0, sDropBody | sGrooveBody, 0.9f, 1, fLevel, tTech | tUp | tPsy, false, 0 },
            { 48, "Accelerating gate", "The gate's rate rises smoothly (not in steps) through the build", cRhythm, rH | rMel, 0, sBuildBody, 1.0f, 2, fRepeat, tUp | tTech | tPsy, false, 4 },

            // ---- C. Pitch and harmony ----
            { 12, "Climbing echoes", "Each echo steps up to the next chord tone, so one note becomes an arpeggio", cPitch, rSing, nPitched, sPhraseEnd | sSectionEnd | sBreakStart, 1.3f, 2, fHarmony, tUp | tOrch, false, 5 },
            { 13, "Harmonic delays", "In-key echoes a third and a fifth above build a chord from one line", cPitch, rSing, nPitched, sBreakBody | sDropBody, 0.9f, 2, fHarmony, tUp | tOrch | tProg, false, 5 },
            { 14, "Octave bloom", "Held notes grow a sub-octave and an octave above", cPitch, rP | rL | rV | rU, nPitched | nSustained, sDropBody | sFinalDrop, 1.1f, 2, fHarmony, tUp | tOrch, false, 0 },
            { 15, "Chord resonator", "Hats and percussion ring at the current chord's notes", cPitch, rH | rF, 0, sBuildBody | sBreakBody | sDropBody | sGrooveBody, 0.9f, 1, fHarmony, tUp | tProg | tPsy, false, 0 },
            { 16, "Frequency-shift sweep", "Key-locked frequency shifting through the transition", cPitch, rH | rMel, 0, sBuildEnd | sSectionEnd | sBreakPeak, 0.7f, 2, fPitch, tPsy | tTech, false, 0 },
            { 49, "Chord-follower harmonizer", "A harmony voice that changes interval with each chord, always in key", cPitch, rP | rL | rV | rU, nPitched, sBreakBody | sDropBody, 0.7f, 1, fHarmony, tUp | tOrch, false, 0 },
            { 50, "Last-note drop", "The phrase's last note slides down an octave as it fades", cPitch, rPitched, nPitched, sPhraseEnd | sSectionEnd, 1.0f, 2, fPitch, tUp | tProg | tPsy, false, 3 },
            { 51, "Glide to the drop", "The last note before the drop glides up to the drop's first note", cPitch, rL | rV | rU, nPitched, sBuildEnd, 0.8f, 2, fPitch, tUp | tPsy, false, 3 },
            { 52, "Detune bloom", "Detune spreads wider through the build, then snaps to focus at the drop", cPitch, rP | rL | rU, nPitched, sBuildBody, 0.9f, 1, fHarmony, tUp | tTech, false, 0 },
            { 53, "Key-change sweep", "Every pitched track sweeps up into the final drop's new key", cPitch, rPitched, nPitched, sKeyChange, 2.0f, 2, fPitch, tUp | tOrch, false, 3 },
            { 54, "Root drone", "A low drone of the section's root note gives the breakdown weight", cPitch, rB | rP | rF | rU, 0, sBreakBody, 0.7f, 1, fHarmony, tUp | tOrch | tProg, false, 0 },
            { 55, "Arpeggiated echo", "A held chord's echoes play its notes one by one", cPitch, rP | rL | rU, nPitched | nSustained, sBreakBody | sPhraseEnd, 0.8f, 2, fHarmony, tUp | tOrch | tProg, false, 5 },
            { 56, "In-key shimmer", "Shimmer locked to octaves and fifths, so it never clashes", cPitch, rP | rL | rV | rF | rU, 0, sBreakBody | sBreakPeak, 1.0f, 2, fSpace, tUp | tOrch, false, 6 },
            { 57, "Bass slide", "At phrase ends the bass slides into the next root from a semitone below", cPitch, rB, nPitched, sPhraseEnd | sSectionEnd, 0.9f, 1, fPitch, tTech | tPsy | tUp, false, 0 },

            // ---- D. Space and texture ----
            { 17, "Hook memories", "The hook recorded in the first drop comes back in the breakdown as distant, filtered fragments", cSpace, rSing, nPitched, sBreakBody, 1.0f, 2, fGrain, tUp | tProg | tOrch, false, 7 },
            { 18, "Underwater", "The breakdown starts muffled and wobbling, then clears over 4 bars", cSpace, rB | rH | rMel, 0, sBreakStart, 1.0f, 2, fFilter, tUp | tProg, false, 0 },
            { 19, "Freeze to pad", "The last chord becomes a sustained granular pad under the breakdown", cSpace, rP | rL | rV | rU, nSustained, sBreakBody, 1.1f, 2, fGrain, tUp | tOrch | tProg, false, 7 },
            { 20, "Reverse-delay lead-in", "Echoes play backwards into the next phrase", cSpace, rSing, 0, sPhraseEnd | sSectionEnd, 1.0f, 2, fReverse, tUp | tProg | tPsy, false, 5 },
            { 21, "Grain cloud", "The part dissolves into grains at the phrase's end", cSpace, rSing, 0, sPhraseEnd | sSectionEnd, 0.9f, 2, fGrain, tProg | tPsy | tUp, false, 3 },
            { 22, "Vowel morph", "The part morphs from 'aah' to 'ooh' with the energy curve", cSpace, rP | rL | rU, nSustained, sBreakBody | sBuildBody | sDropBody, 0.7f, 1, fFilter, tUp | tOrch | tPsy, false, 0 },
            { 23, "Room by energy", "Small and tight in drops, huge in breakdowns, swelling through builds", cSpace, rH | rMel, 0, sSong, 1.3f, 1, fSpace, tUp | tProg | tOrch, false, 6 },
            { 24, "Doppler fly-by", "The part swoops past with pitch, pan and tone", cSpace, rF | rL | rU, 0, sPhraseEnd | sSectionEnd | sBuildEnd, 0.8f, 2, fStereo, tPsy | tTech, false, 3 },
            { 25, "360 rotation", "The part circles the stereo field faster and faster through the build", cSpace, rP | rL | rF | rU, 0, sBuildBody, 0.8f, 2, fStereo, tPsy | tUp, false, 0 },
            { 26, "Tape warmth", "Gentle wow, flutter and saturation in breakdowns, clearing for the drop", cSpace, rNoKick, 0, sBreakBody, 0.8f, 1, fTape, tProg | tOrch, false, 0 },
            { 58, "Distance", "The part steps back in breakdowns (darker, wetter) and forward for drops", cSpace, rH | rMel, 0, sBreakBody, 1.0f, 1, fSpace, tProg | tUp, false, 0 },
            { 59, "Chord-change smear", "A frozen smear of the outgoing chord bleeds into the new one", cSpace, rP | rL | rU, nSustained, sBreakBody | sGrooveBody, 0.6f, 1, fGrain, tUp | tOrch, false, 7 },
            { 60, "Ambient bed", "A long, dark reverb-only bed under the whole breakdown", cSpace, rP | rL | rV | rU, 0, sBreakBody, 0.9f, 1, fSpace, tUp | tProg | tOrch, false, 6 },
            { 61, "Room swap", "Plate in grooves, hall in breakdowns, shimmer at the peak, a tight room in drops", cSpace, rH | rMel, 0, sSong, 1.0f, 1, fSpace, tUp | tTech | tProg, false, 6 },
            { 62, "Echo dub-out", "The echo's feedback runs up into a dub spiral at the section's end, then is caught", cSpace, rH | rMel, 0, sSectionEnd | sPhraseEnd, 1.1f, 2, fSpace, tTech | tProg | tPsy, false, 5 },
            { 63, "Telephone", "A band-passed, lo-fi version of the part for the breakdown's first bar", cSpace, rL | rV | rU, 0, sBreakStart, 0.8f, 2, fFilter, tTech | tProg, false, 0 },
            { 64, "Breathing width", "Width opens on long notes and narrows on fast runs", cSpace, rP | rL | rU, 0, sDropBody | sGrooveBody, 0.6f, 1, fStereo, tProg | tUp, false, 0 },
            { 65, "Space follows the vocal", "The part's echoes and reverb step back while the vocal sings and bloom in its gaps", cSpace, rH | rP | rL | rF | rU, 0, sSong, 1.0f, 1, fSpace, tUp | tProg, false, 0 },
            { 66, "Air on the climax", "The very top end lifts over the last phrase before each drop", cSpace, rH | rMel, 0, sBuildEnd | sBuildBody, 0.9f, 1, fColour, tUp | tTech, false, 0 },
            { 67, "Cassette fade", "The breakdown's last bar sags in pitch and rolls off like a tired cassette", cSpace, rMel, 0, sBreakPeak, 0.7f, 2, fTape, tProg | tTech, false, 3 },

            // ---- E. Expression and movement ----
            { 27, "Living long notes", "Held notes grow vibrato and a filter swell after half a beat", cExpression, rL | rV | rU, nPitched | nSustained, sDropBody | sBreakBody, 1.0f, 1, fPitch, tUp | tOrch, false, 0 },
            { 28, "Key-tracked filter", "The filter follows the notes: brighter on high ones", cExpression, rP | rL | rU, nPitched, sDropBody | sGrooveBody, 0.7f, 1, fFilter, tTech | tPsy, false, 0 },
            { 29, "Scream", "The lead's saturation ramps up through the final drop's last phrase", cExpression, rL | rU, 0, sFinalEnd, 1.3f, 2, fColour, tUp | tTech | tPsy, false, 0 },
            { 30, "Pads pulse with the roll", "Pads pulse in time with the snare roll as it speeds up", cExpression, rP, 0, sBuildBody, 1.2f, 2, fRepeat, tUp, false, 4 },
            { 68, "Phrase dynamics", "Each phrase swells to its middle and relaxes at its end, like a player breathing", cExpression, rNoKick, 0, sSong, 0.8f, 1, fLevel, tOrch | tProg | tUp, false, 0 },
            { 69, "Downbeat accents", "The first beat of each bar a touch brighter and punchier", cExpression, rAll, 0, sDropBody | sGrooveBody, 0.6f, 1, fColour, tTech | tPsy, false, 0 },
            { 70, "Humanised filter", "Tiny random filter and level changes per note, so loops never sound frozen", cExpression, rH | rMel, 0, sSong, 0.8f, 1, fFilter, tProg | tOrch, false, 0 },
            { 71, "Energy brightness", "Brightness follows the energy curve smoothly, not in section steps", cExpression, rH | rMel, 0, sSong, 1.0f, 1, fFilter, tUp | tProg, false, 0 },
            { 72, "Velocity colour", "Louder notes come out brighter and a little driven, like a real instrument", cExpression, rB | rH | rP | rL | rU, 0, sDropBody | sGrooveBody, 0.6f, 1, fColour, tTech | tOrch, false, 0 },
            { 73, "Call-and-response pan", "Phrase halves answer each other, left then right", cExpression, rP | rL | rF | rU, 0, sBreakBody | sGrooveBody, 0.6f, 1, fStereo, tProg | tPsy, false, 0 },
            { 74, "Chord pluck", "Each chord change re-triggers a pluck-like filter on the pads", cExpression, rP | rU, nSustained, sBreakBody | sBuildBody | sGrooveBody, 0.8f, 1, fFilter, tUp | tTech, false, 0 },
            { 75, "Chord-weighted width", "Wider on open, tense chords; narrower on home chords", cExpression, rP | rU, 0, sBreakBody | sDropBody | sGrooveBody, 0.6f, 1, fStereo, tUp | tProg, false, 0 },

            // ---- F. No limits ----
            { 76, "Reverse song memory", "The outro plays fragments of the first drop backwards, buried in reverb", cWild, rSing, 0, sOutro, 1.0f, 2, fReverse, tProg | tUp, false, 7 },
            { 77, "Crowd of copies", "For a bar, the part becomes a crowd of detuned, delayed copies", cWild, rL | rV | rU, nPitched, sDropStart, 0.7f, 2, fHarmony, tUp | tOrch, false, 0 },
            { 78, "Morph into the lead", "Through the breakdown the pad slowly takes on the lead's tone (opt-in)", cWild, rP, 0, sBreakBody, 0.7f, 2, fFilter, tUp | tProg, true, 0 },
            { 79, "Vocal-shaped pads", "The pad takes on the vocal's live tone as it sings (opt-in)", cWild, rP, 0, sBreakBody | sDropBody, 0.7f, 2, fFilter, tUp | tProg, true, 0 },
            { 80, "Rhythm borrowing", "The pad takes the lead's rhythm for the bar before the drop", cWild, rP | rU, 0, sBuildEnd, 0.8f, 2, fRepeat, tTech | tUp, false, 4 },
            { 81, "Breathing silence", "One beat at the breakdown's peak where only the reverb tails remain", cWild, rAll, 0, sBreakPeak, 0.8f, 3, fLevel, tUp | tOrch, false, 2 },
            { 82, "Granular stretch", "The build's last bar stretches to twice its length in grains", cWild, rMel, 0, sBuildEnd, 0.7f, 3, fGrain, tProg | tPsy, false, 3 },
            { 83, "Overtone sweep", "A resonator climbs the root's harmonic series through the build", cWild, rP | rF | rL | rU, 0, sBuildBody, 0.8f, 2, fHarmony, tPsy | tUp, false, 0 },
            { 84, "Spectral blur", "The build's last beat smears into a wash that the drop cuts out of", cWild, rMel, 0, sBuildEnd, 0.9f, 3, fGrain, tUp | tProg, false, 3 },
            { 85, "Reverb stutter", "Only the reverb tail stutters in 1/16s, locked to the kick", cWild, rH | rMel, 0, sDropBody, 0.6f, 1, fSpace, tTech | tPsy, false, 6 },
            { 86, "Sidechained shimmer", "Shimmer that blooms only between kicks, so the drop sparkles in its gaps", cWild, rP | rL | rF | rU, 0, sDropBody | sFinalDrop, 0.8f, 1, fSpace, tUp, false, 6 },
            { 87, "Tape loop", "The breakdown's chord caught in a 1-bar tape loop that slowly detunes and wears out", cWild, rMel, 0, sBreakBody, 0.6f, 2, fTape, tProg | tOrch, false, 7 },
            { 88, "Euphoria cascade", "At the drop each track peaks in brightness and width one after another, like a wave", cWild, rH | rMel, 0, sDropStart, 1.2f, 2, fColour, tUp, false, 0 },
            { 89, "Inverted drop", "For a bar the drop plays inside out (pads high-passed, lead low-passed), then snaps right", cWild, rH | rP | rL | rU, 0, sDropStart, 0.6f, 3, fFilter, tTech | tPsy, false, 2 },
            { 90, "Self-remix fill", "The drop is recorded and reassembled into a new fill at phrase ends", cWild, rH | rMel, 0, sPhraseEnd | sSectionEnd, 0.8f, 2, fRepeat, tTech | tPsy, false, 3 },

            // ---- G. The director (behaviours: Never switches them off) ----
            { 91, "Signature moves", "Learns which moves you lock and keep, and favours them in later songs", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },
            { 92, "Story arc", "Restrained first drop, more in the second, everything in the final", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },
            { 93, "Call and response", "When the lead or vocal makes a move, another track answers a bar later", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },
            { 94, "Motifs", "Two or three signature moves per song come back at the same kinds of moments", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },
            { 95, "Rest", "After a busy transition, the next one stays plain", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },
            { 96, "Vocal priority", "While the vocal sings, other tracks keep their moves small", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },
            { 97, "Preview and audition", "The plan on a timeline here and in Ouroboros; play any move now", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },
            { 98, "Genre profiles", "Uplifting, tech, psy, progressive and orchestral each favour their own moves", cDirector, rAll, 0, sNone, 1.0f, 1, fDirector, 0, false, 0 },

            // ---- H. Transitions ----
            { 99, "Continuity", "The breakdown's last chord carries into the build as a frozen layer", cTransition, rP | rL | rV | rU, nSustained, sBuildStart, 1.0f, 1, fGrain, tUp | tOrch, false, 7 },
            { 100, "Drop countdown", "The last 4 beats before a drop each go to a different track's move", cTransition, rAll, 0, sNone, 1.0f, 2, fRepeat, tUp | tTech, false, 3 },
            { 101, "Snap to silence", "A sample-accurate silence on every track for the last 16th before the drop", cTransition, rAll, 0, sBuildEnd, 1.4f, 1, fLevel, tTech | tUp | tPsy, false, 2 },
            { 102, "Echo bridge", "The section's last note echoes across the gap into the next", cTransition, rH | rMel, 0, sSectionEnd, 1.0f, 1, fSpace, tUp | tProg, false, 5 },
            { 103, "Sweep handoff", "A filter sweep travels from track to track across the build", cTransition, rH | rP | rL | rF | rU, 0, sNone, 1.0f, 2, fFilter, tUp | tProg, false, 0 },

            // ---- I. Per role ----
            { 104, "Kick: boom and punch", "The kick's tail lengthens into breakdowns and builds, and tightens for drops", cRole, rK, 0, sDropBody | sBuildBody, 1.5f, 1, fColour, tUp | tTech | tPsy, false, 0 },
            { 105, "Bass: flips and growl", "Octave flips at phrase ends, a growl in the final drop, the sub fading in builds", cRole, rB, 0, sPhraseEnd | sFinalDrop | sBuildBody, 1.3f, 2, fPitch, tTech | tPsy | tUp, false, 0 },
            { 106, "Hats: open, roll, choke", "Hats open up through builds, roll into 1/32 at the end, and choke on the drop's first beat", cRole, rH, 0, sBuildBody | sBuildEnd | sDropStart, 1.5f, 2, fRepeat, tUp | tTech | tPsy, false, 0 },
            { 107, "Pads: swell, drift, gate", "Chord-change swells, slow movement in breakdowns, a sharp gate in drops", cRole, rP, 0, sBreakBody | sDropBody | sGrooveBody, 1.2f, 1, fLevel, tUp | tTech, false, 0 },
            { 108, "Plucks: stepped filter, caught echo", "The filter opens in steps through the build; the echo spiral is caught on the drop", cRole, rP | rL, nPlucky, sBuildBody | sBuildEnd, 1.3f, 2, fFilter, tUp | tTech | tPsy, false, 0 },
            { 109, "Vocals: throws and freezes", "The last word thrown into the reverb, formant shifts on chops, the breakdown's last syllable frozen", cRole, rV, 0, sPhraseEnd | sSectionEnd | sBreakPeak, 1.5f, 2, fSpace, tUp | tProg, false, 0 },
            { 110, "FX: swells and fly-bys", "Reverse swells timed to the drop, fly-bys, impact tails stretched to the beat", cRole, rF, 0, sBuildEnd | sPhraseEnd | sDropStart, 1.5f, 2, fReverse, tUp | tTech | tPsy, false, 3 },

            // ---- J. Physics and wild ----
            { 111, "Gravity", "After the drop's first hit, the echoes fall in pitch and brightness", cPhysics, rH | rMel, 0, sDropStart, 0.8f, 2, fSpace, tPsy | tProg, false, 5 },
            { 112, "Elastic time", "Echo times stretch with the energy curve and snap back on the drop", cPhysics, rH | rMel, 0, sBuildBody | sBreakBody, 0.6f, 2, fSpace, tPsy | tProg, false, 5 },
            { 113, "Weather", "Slow wind and rain textures made from the song's chords, in key, under breakdowns", cPhysics, rF | rP | rU, 0, sBreakBody, 0.6f, 1, fSpace, tProg | tOrch | tUp, false, 0 },
            { 114, "Spectral chase", "The pad's brightest band chases the lead's note", cPhysics, rP | rU, 0, sDropBody | sBreakBody, 0.6f, 1, fFilter, tUp | tPsy, false, 0 },
            { 115, "Echoes in key", "Echoes that climb the scale, each repeat corrected into the key", cPhysics, rSing, nPitched, sBreakBody | sPhraseEnd, 0.8f, 2, fHarmony, tUp | tOrch | tPsy, false, 5 },
            { 116, "Contagion", "A stutter spreads from the lead to the next track and the next, building to the drop", cPhysics, rH | rP | rF | rU, 0, sNone, 1.0f, 2, fRepeat, tTech | tPsy | tUp, false, 4 },
            { 117, "Song breathing", "The whole mix inhales and exhales with the phrase, so the song feels alive", cPhysics, rAll, 0, sSong, 0.8f, 1, fLevel, tProg | tOrch | tUp, false, 0 },
            { 118, "Mirror drop", "The last drop's first bar replays the first drop's, transformed, before the new one takes over", cPhysics, rSing | rH, 0, sDropStart, 1.0f, 3, fGrain, tUp | tPsy | tTech, false, 7 },
        } };
        return e;
    }

    /** The catalogue entry for move `id` (1-118). */
    inline const Effect& effect (int id)
    {
        static const auto index = []
        {
            std::array<int, numEffects + 1> ix {};
            for (int i = 0; i < numEffects; ++i) ix[(size_t) effects()[(size_t) i].id] = i;
            return ix;
        }();
        return effects()[(size_t) index[(size_t) (id >= 1 && id <= numEffects ? id : 1)]];
    }

    /** How the genre weighs a category, and moves tagged for it. */
    inline float genreWeight (int genre, const Effect& e)
    {
        static const float cat[numGenres][numCategories] = {
            // build rhythm pitch space expr wild dir trans role physics
            { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
            { 1.2f, 0.8f, 1.3f, 1.2f, 1.1f, 0.9f, 1, 1.2f, 1, 0.9f },   // uplifting
            { 1.1f, 1.4f, 0.7f, 0.9f, 1.0f, 1.0f, 1, 1.1f, 1, 0.9f },   // tech
            { 1.0f, 1.4f, 0.9f, 0.9f, 0.9f, 1.3f, 1, 1.0f, 1, 1.3f },   // psy
            { 0.9f, 0.9f, 0.9f, 1.3f, 1.3f, 1.0f, 1, 1.0f, 1, 1.1f },   // progressive
            { 1.1f, 0.5f, 1.4f, 1.4f, 1.3f, 0.7f, 1, 1.1f, 1, 0.8f },   // orchestral
        };
        static const uint8_t tag[numGenres] = { 0, tUp, tTech, tPsy, tProg, tOrch };
        const auto g = genre >= 0 && genre < numGenres ? genre : 0;
        auto w = cat[g][e.category];
        if (g != gAuto && (e.tags & tag[g]) != 0) w *= 1.6f;
        return w;
    }

    /** Genre from the tempo (Auto). */
    inline int genreFromTempo (double bpm)
    {
        return bpm < 128.0 ? gProgressive : bpm < 134.0 ? gTech : bpm < 141.0 ? gUplifting : gPsy;
    }

    // Per-effect user modes
    enum Mode : uint8_t { mAuto = 0, mAlways = 1, mNever = 2 };
}
