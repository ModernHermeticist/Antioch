#pragma once

// The suite's 36 track roles, in 9 families. Each role sits in a family whose old broad role (the
// "legacy" number every plugin already knows: 1 kick, 2 bass, 3 hats & percussion, 4 pads & chords,
// 5 lead, 6 vocal, 7 FX, 8 bus) keeps all the existing behaviour working; the role then refines it:
// its tone, low cut and width (Quintessence), its sections (the section mixer), its ducking, and who
// gives way to whom in a clash (lead vocal > main lead and chops > kick and sub > supersaw and arps
// > pads > FX and atmos).
//
// A role is found from the track's name; when the name only names the family ("Bass", "Chords",
// "Lead"), Quintessence guesses from the sound. The role travels with the legacy number in the suite
// registry as its fraction (2.02 = bass, the second bass role): (int) gives the old role unchanged.

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace suite::roles
{
    enum Family { famKick, famBass, famDrums, famChords, famKeys, famLeads, famVocals, famFx, famBuses, numFamilies };

    enum Role
    {
        kick, kickTop, kickTail,
        subBass, midBass, acidBass, bassTop,
        clap, hats, percussion, cymbals,
        pads, supersaw, plucks, organ,
        piano, strings, brass, choir, timpani, bells, guitar,
        mainLead, arps, leadLayers, counter,
        leadVocal, backing, chops, spoken,
        risers, impacts, atmos, earCandy,
        drumBus, musicBus,
        numRoles
    };

    constexpr float keep = 99.0f;   // a section level left as the family has it
    constexpr float off = -100.0f;  // silent in that section

    struct Info
    {
        const char* name;
        Family family;
        int legacy;                       // the old broad role (see above)
        int priority;                     // 1 = everything gives way to it .. 9 = takes no part
        std::array<float, 10> tone;       // dB added to the family's tone shape per octave band (31.5 Hz .. 16 kHz)
        float lowCutHz;                   // below this it has no job (-1: the family's)
        float sideDb;                     // its width: the side under the middle (dB; 0: the family's)
        float duck;                       // how much it ducks to the kick (0..1)
        std::array<float, 6> sections;    // its level per section: intro, groove, breakdown, build, drop, outro (keep / off / dB)
        const char* keywords;             // in the track name (comma separated, lower case)
    };

    inline const std::array<Info, numRoles>& all()
    {
        constexpr float k = keep, o = off;
        static const std::array<Info, numRoles> r {{
            // name                         family     leg pri  tone (31 .. 16k)                                   low cut side  duck  sections: in  groove bd   build drop out
            { "Kick",                       famKick,    1, 3, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                   -1.0f, 0.0f,   0.0f, { k, k, k, k, k, k }, "kick,bassdrum, bd " },
            { "Kick top / click",           famKick,    1, 4, { -12, -10, -6, 0, 3, 6, 8, 6, 2, 0 },               120.0f, -30.0f, 0.0f, { k, k, k, k, k, k }, "kick top,kick click,click,kick punch,top kick" },
            { "Kick tail / rumble",         famKick,    1, 7, { 2, 2, 0, -4, -8, -10, -12, -12, -12, -12 },        -1.0f, -40.0f, 0.0f, { k, k, o, o, k, k }, "kick tail,rumble,kick sub,kick rumble" },
            { "Sub bass",                   famBass,    2, 3, { 2, 2, -2, -10, -15, -18, -20, -20, -20, -20 },     -1.0f, -40.0f, 0.8f, { o, k, o, o, k, k }, "sub bass,subbass,sub" },
            { "Mid / rolling bass",         famBass,    2, 4, { -8, -3, 2, 2, 0, -2, -4, -6, -8, -8 },              45.0f, -20.0f, 0.6f, { k, k, o, k, k, k }, "rolling,mid bass,offbeat bass,psy bass,roller" },
            { "Acid / 303 & reese",         famBass,    2, 4, { -6, -2, 0, 2, 3, 4, 3, 0, -3, -6 },                 60.0f, -14.0f, 0.5f, { k, k, -6, k, k, k }, "acid,303,reese,wobble" },
            { "Bass top layer",             famBass,    2, 6, { -20, -15, -4, 2, 4, 6, 6, 4, 0, -4 },               150.0f, -6.0f, 0.5f,  { o, k, o, k, k, o }, "bass top,top bass,dist bass,bass layer,bass dist,bass mid" },
            { "Clap / snare",               famDrums,   3, 4, { 0, 0, 10, 18, 20, 14, 8, 2, -2, -4 },               150.0f, -10.0f, 0.0f, { k, k, o, k, k, k }, "clap,snare,snr,rimshot, rim " },
            { "Hats",                       famDrums,   3, 6, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     300.0f, -8.0f, 0.0f,  { k, k, k, k, k, k }, "hat,hh,hihat,open hat,closed hat" },
            { "Percussion / tops",          famDrums,   3, 7, { 0, 0, 0, 6, 8, 6, 2, 0, 0, -2 },                    200.0f, -6.0f, 0.0f,  { k, k, -6, k, k, k }, "perc,shaker,conga,bongo,tamb,top loop,tops,loop" },
            { "Cymbals / crashes / rides",  famDrums,   3, 7, { 0, 0, 0, 0, 0, -2, -2, 0, 2, 4 },                   400.0f, -4.0f, 0.0f,  { k, k, k, k, k, k }, "crash,cymbal,ride,splash,china" },
            { "Pads",                       famChords,  4, 7, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     100.0f, -6.0f, 0.6f,  { k, k, k, k, k, k }, "pad" },
            { "Supersaw chords",            famChords,  4, 5, { -6, -4, -2, 0, 0, 1, 2, 2, 1, 0 },                  150.0f, -3.0f, 0.5f,  { o, -6, -3, -2, 0, o }, "supersaw,super saw,saw chord,chord saw,hoover" },
            { "Plucks & stabs",             famChords,  4, 5, { -6, -3, 0, 0, 0, 1, 2, 1, -1, -3 },                 150.0f, -6.0f, 0.3f,  { k, k, -4, k, k, k }, "pluck,stab" },
            { "Organ / drones",             famChords,  4, 7, { 0, 2, 3, 2, 0, -1, -2, -4, -6, -8 },                60.0f, -10.0f, 0.5f,  { k, k, 0, k, k, k }, "organ,drone,reese pad" },
            { "Piano / keys",               famKeys,    4, 5, { 0, 2, 2, 1, 0, 0, -1, -2, -3, -5 },                 50.0f, -8.0f, 0.0f,   { k, k, 2, k, -4, k }, "piano,keys,rhodes,e-piano,epiano, ep " },
            { "Strings",                    famKeys,    4, 6, { -6, -2, 0, 0, 0, 0, 0, -1, -2, -4 },                80.0f, -4.0f, 0.3f,   { k, k, 2, k, -3, k }, "string,violin,cello,viola,orchestra,ensemble" },
            { "Brass",                      famKeys,    4, 6, { -6, -2, 2, 3, 3, 2, 0, -2, -5, -8 },                100.0f, -8.0f, 0.0f,  { k, k, -2, k, 0, k }, "brass,horn,trumpet,trombone,tuba" },
            { "Choir",                      famKeys,    4, 6, { -8, -4, 0, 1, 1, 1, 1, 0, -2, -4 },                 120.0f, -5.0f, 0.3f,  { k, k, 0, k, -2, k }, "choir,choral,aah,ooh,voices" },
            { "Timpani / orchestral perc",  famKeys,    4, 6, { 20, 18, 10, 2, -4, -8, -12, -14, -14, -14 },        0.0f, -20.0f, 0.0f,   { k, k, -3, 0, -2, k }, "timpani,taiko,orch perc,orchestral perc,war drum, tom " },
            { "Bells / mallets",            famKeys,    4, 6, { -10, -8, -6, -4, -2, 0, 2, 3, 3, 2 },               250.0f, -6.0f, 0.0f,  { k, k, 1, k, -4, k }, "bell,mallet,celesta,glock,marimba,kalimba,music box,vibraphone" },
            { "Guitar / ethnic / woodwind", famKeys,    4, 6, { -8, -2, 2, 3, 2, 1, 0, -2, -4, -6 },                90.0f, -8.0f, 0.0f,   { k, k, 1, k, -6, k }, "guitar,gtr,flute,duduk,sitar,ethnic,woodwind,oboe,clarinet,erhu,shakuhachi" },
            { "Main lead / hook",           famLeads,   5, 2, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     150.0f, -9.0f, 0.0f,  { k, k, k, k, k, k }, "main lead,hook,topline,lead,melody,synth" },
            { "Arps & sequences",           famLeads,   5, 5, { -4, -2, 0, 0, 0, 0, 1, 1, 0, -1 },                  200.0f, -4.0f, 0.3f,  { k, k, k, k, k, k }, "arp,seq,sequence,gated" },
            { "Lead layers / octaves",      famLeads,   5, 5, { -6, -4, -2, 0, 0, 0, 1, 1, 1, 0 },                  200.0f, -4.0f, 0.0f,  { o, o, o, k, 0, o }, "lead layer,layer,octave,lead 2,lead2,double,lead dbl" },
            { "Counter-melody",             famLeads,   5, 6, { -4, -2, 0, 1, 1, 0, -1, -2, -3, -4 },               180.0f, -7.0f, 0.0f,  { k, k, -2, k, k, k }, "counter,answer,second melody,call" },
            { "Lead vocal",                 famVocals,  6, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     100.0f, -16.0f, 0.0f, { k, k, k, k, k, k }, "lead vox,lead vocal,vox,vocal,voc,acap,singer,topline vox" },
            { "Backing / harmonies",        famVocals,  6, 5, { -4, -2, 0, 0, 0, 0, -1, -2, -1, 0 },                150.0f, -4.0f, 0.0f,  { k, k, k, k, k, k }, "backing,bvs,bv,harmony,harm,double vox,bgv,vox dbl" },
            { "Vocal chops",                famVocals,  6, 2, { -4, -2, 0, 0, 1, 2, 2, 1, 0, -2 },                  150.0f, -8.0f, 0.0f,  { k, k, k, k, k, k }, "chop,vox chop,vocal chop" },
            { "Spoken / vocal FX",          famVocals,  6, 6, { -6, -4, 0, 1, 1, 0, 0, -1, -2, -2 },                120.0f, -10.0f, 0.0f, { k, k, k, k, -4, k }, "spoken,whisper,speech,talk,vocal fx,vox fx,voice fx" },
            { "Risers / sweeps",            famFx,      7, 7, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     120.0f, 0.0f, 0.0f,   { k, k, k, k, k, k }, "riser,sweep,uplift,upsweep,whoosh,noise up,white noise" },
            { "Impacts / downlifters",      famFx,      7, 6, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     0.0f, 0.0f, 0.0f,     { k, k, k, k, k, k }, "impact,downlift,down lift,boom,hit fx,reverse crash" },
            { "Atmos / textures",           famFx,      7, 8, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     150.0f, 0.0f, 0.6f,   { k, k, k, k, -3, k }, "atmos,texture,ambience,ambient,background,field,fx" },
            { "Ear candy",                  famFx,      7, 7, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     200.0f, 0.0f, 0.0f,   { k, k, k, k, k, k }, "candy,zap,blip,one shot,oneshot" },
            { "Drum bus",                   famBuses,   8, 9, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     0.0f, 0.0f, 0.0f,     { k, k, k, k, k, k }, "drum bus,drums bus,drum group,drum grp,drums" },
            { "Music bus",                  famBuses,   8, 9, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                     0.0f, 0.0f, 0.0f,     { k, k, k, k, k, k }, "music bus,synth bus,music group,bus,group,grp" },
        }};
        return r;
    }

    inline const Info& info (int role) { return all()[(size_t) juce::jlimit (0, (int) numRoles - 1, role)]; }
    inline bool valid (int role) noexcept { return role >= 0 && role < numRoles; }

    inline const char* familyName (Family f)
    {
        static const char* n[] = { "Kick", "Bass", "Drums", "Chords", "Keys & orchestral", "Leads", "Vocals", "FX", "Buses" };
        return n[(size_t) juce::jlimit (0, (int) numFamilies - 1, (int) f)];
    }
    /** Each family's role when nothing says more (and the one old projects start from). */
    inline Role familyDefault (Family f)
    {
        static const Role d[] = { kick, subBass, hats, pads, piano, mainLead, leadVocal, atmos, musicBus };
        return d[(size_t) juce::jlimit (0, (int) numFamilies - 1, (int) f)];
    }
    /** "Bass > Sub bass". */
    inline juce::String fullName (int role) { return valid (role) ? juce::String (familyName (info (role).family)) + " > " + info (role).name : juce::String ("No role"); }

    /** The default role for an old broad role (1 kick .. 8 bus), for old projects and old readers. */
    inline int fromLegacy (int legacy)
    {
        switch (legacy)
        {
            case 1: return kick;      case 2: return subBass;   case 3: return hats;     case 4: return pads;
            case 5: return mainLead;  case 6: return leadVocal; case 7: return atmos;    case 8: return musicBus;
            default: return -1;
        }
    }

    /** From a track's name: the role, and whether the name only named its family ("Bass", "Chords",
        "Lead"), so the sound should decide. -1 if the name says nothing. */
    struct NameMatch { int role = -1; bool onlyFamily = false; };
    inline NameMatch fromTrackName (const juce::String& trackName)
    {
        const auto n = " " + trackName.toLowerCase().replaceCharacters ("_-.0123456789", "             ") + " ";
        if (n.trim().isEmpty()) return {};
        // most specific first: two-word keywords before one-word ones, then the order of the list
        NameMatch best;
        int bestLen = 0;
        for (int r = 0; r < numRoles; ++r)
            for (const auto& raw : juce::StringArray::fromTokens (info (r).keywords, ",", ""))
            {
                const auto w = raw;   // keywords keep their own spaces (" bd " needs the gaps)
                if (w.trim().isEmpty() || ! n.contains (w)) continue;
                const auto len = w.trim().length() + (w.containsChar (' ') ? 100 : 0);
                if (len > bestLen) { bestLen = len; best.role = r; }
            }
        if (best.role < 0)
        {
            // only the family's word
            if (n.contains ("bass")) return { subBass, true };
            if (n.contains ("chord") || n.contains ("synth")) return { pads, true };
            if (n.contains ("drum")) return { hats, true };
            return {};
        }
        // the family's own plain word (no finer word): let the sound decide inside the family
        const auto r = best.role;
        best.onlyFamily = (r == mainLead && (n.contains (" lead ") || n.contains (" synth ") || n.contains (" melody ")))
                       || (r == pads && n.contains (" chord"))
                       || (r == atmos && n.contains (" fx "));
        if (n.contains (" bass ") && info (r).family == famBass && r == subBass && ! n.contains ("sub")) best.onlyFamily = true;
        return best;
    }

    /** What Quintessence measures, for guessing a role inside a family from the sound. */
    struct Sound
    {
        std::array<float, 10> bandsDb {};   // its level per octave band
        float punchDb = 0.0f;               // attacks over the body
        float sideMinusMidDb = -100.0f;     // its width
        float movementDb = 0.0f;            // how much its tone moves (0 = unknown)
        float decayMs = 0.0f;               // how fast a hit dies away (0 = unknown)
    };
    /** The role in `family` the sound fits best (the family's default when it can't tell). */
    inline int guessFromSound (Family family, const Sound& s)
    {
        const auto& b = s.bandsDb;
        auto avg = [&b] (int from, int to) { float sum = 0.0f; for (int i = from; i <= to; ++i) sum += b[(size_t) i]; return sum / (float) (to - from + 1); };
        const auto heard = avg (0, 9) > -90.0f;
        if (! heard) return familyDefault (family);
        switch (family)
        {
            case famBass:
            {
                const auto low = avg (0, 1), mid = avg (2, 4), top = avg (5, 8);
                if (s.movementDb > 3.0f && top > low - 18.0f) return acidBass;
                if (low < mid - 6.0f) return bassTop;
                if (low > mid + 9.0f && top < low - 24.0f) return subBass;
                return midBass;
            }
            case famChords:
            {
                if (s.punchDb > 9.0f || (s.decayMs > 0.0f && s.decayMs < 250.0f)) return plucks;
                if (s.sideMinusMidDb > -6.0f && avg (6, 7) > avg (3, 4) - 6.0f) return supersaw;
                if (s.movementDb > 0.0f && s.movementDb < 0.6f && s.sideMinusMidDb < -12.0f) return organ;
                return pads;
            }
            case famLeads:
                if (s.punchDb > 8.0f && s.decayMs > 0.0f && s.decayMs < 200.0f) return arps;
                return mainLead;
            case famDrums:
            {
                const auto top = avg (7, 9), body = avg (3, 5);
                if (top > body + 6.0f) return s.decayMs > 600.0f ? cymbals : hats;
                if (body > top + 3.0f && s.punchDb > 8.0f) return clap;
                return percussion;
            }
            case famFx:
                if (s.punchDb > 12.0f) return impacts;
                if (s.movementDb > 3.0f) return risers;
                return atmos;
            default:
                return familyDefault (family);
        }
    }

    /** Who gives way in a clash, 0..90 (higher keeps the space). */
    inline int rank (int role) { return valid (role) ? 100 - 10 * info (role).priority : 0; }
    /** The same for an old broad role (when no finer role is known). */
    inline int legacyRank (int legacy)
    {
        switch (legacy) { case 6: return 90; case 5: return 80; case 1: case 2: return 70; case 3: return 40; case 4: return 30; case 7: return 20; default: return 0; }
    }

    /** The role in the registry's role number: legacy + (role + 1) / 100. (int) gives the legacy role. */
    inline float encode (int legacy, int role) { return (float) legacy + (valid (role) ? (float) (role + 1) / 100.0f : 0.0f); }
    inline int decode (float metric)
    {
        const auto frac = (int) std::lround ((metric - std::floor (metric)) * 100.0f);
        return frac >= 1 && frac <= numRoles ? frac - 1 : -1;
    }

    /** The pattern library's roles (Correspondence) as track roles: what a dragged clip becomes. */
    inline int forLibraryRole (const juce::String& libraryRole)
    {
        const auto l = libraryRole.toLowerCase();
        if (l.contains ("sub"))     return subBass;
        if (l.contains ("acid"))    return acidBass;
        if (l.contains ("bass"))    return midBass;
        if (l.contains ("arp"))     return arps;
        if (l.contains ("pluck") || l.contains ("stab")) return plucks;
        if (l.contains ("counter")) return counter;
        if (l.contains ("harmony")) return leadLayers;
        if (l.contains ("lead") || l.contains ("melody") || l.contains ("hook")) return mainLead;
        if (l.contains ("piano"))   return piano;
        if (l.contains ("string"))  return strings;
        if (l.contains ("brass"))   return brass;
        if (l.contains ("choir"))   return choir;
        if (l.contains ("timpani")) return timpani;
        if (l.contains ("chop"))    return chops;
        if (l.contains ("candy"))   return earCandy;
        if (l.contains ("fill") || l.contains ("drum")) return percussion;
        if (l.contains ("chord") || l.contains ("gate")) return supersaw;
        if (l.contains ("pad"))     return pads;
        return -1;
    }
}
