#pragma once

// Track roles, shared by the suite: from a track's name (and Prima Materia's choice).
// 0 none, 1 kick, 2 bass, 3 hats & percussion, 4 pads & chords, 5 lead, 6 vocal, 7 FX.

#include <juce_core/juce_core.h>
#include "RoleCatalog.h"

namespace suite
{
    enum Role { roleNone = 0, roleKick, roleBass, roleHats, rolePads, roleLead, roleVocal, roleFx };

    /** A role from a track name ("Kick", "Sub Bass", "Lead 2", "Vox chop"...), or -1. */
    inline int roleFromTrackName (const juce::String& trackName)
    {
        const auto n = " " + trackName.toLowerCase().replaceCharacters ("_-.", "   ") + " ";
        auto any = [&n] (std::initializer_list<const char*> words)
        {
            for (auto* w : words) if (n.contains (w)) return true;
            return false;
        };
        if (any ({ "kick", " bd ", "bassdrum" }))                                              return roleKick;
        if (any ({ "vox", "vocal", "voc ", "acap", "voice", "chop" }))                        return roleVocal;
        if (any ({ "bass", " sub", "reese" }))                                                 return roleBass;
        if (any ({ "hat", " hh", "perc", "clap", "snare", "ride", "shaker", "top", "cymbal", "tamb" })) return roleHats;
        if (any ({ "fx", "atmos", "riser", "sweep", "noise", "impact", "crash", "downlift", "texture" })) return roleFx;
        if (any ({ "lead", "saw", "melody", "arp", "hook", "synth" }))                        return roleLead;
        if (any ({ "pad", "chord", "string", "keys", "piano", "pluck", "stab" }))             return rolePads;
        // the detailed roles' names (acid, organ, brass, choir, bells, guitar...): their broad role
        if (const auto m = suite::roles::fromTrackName (trackName); m.role >= 0 && suite::roles::info (m.role).legacy <= roleFx)
            return suite::roles::info (m.role).legacy;
        return -1;
    }
    /** Plucks and arps sit in the lead and pad roles; some plugins treat them on their own. */
    inline bool isPluckName (const juce::String& n) { return n.toLowerCase().contains ("pluck") || n.toLowerCase().contains ("stab"); }
    inline bool isArpName (const juce::String& n) { return n.toLowerCase().contains ("arp"); }
}
