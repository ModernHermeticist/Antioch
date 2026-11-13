#pragma once

// Delivering the premaster: label profiles (what each wants: versions, stems, format, naming),
// the mastering brief, the delivery package's contents, and turning mastering / label notes
// ("the bass is too loud in the drop", "a bit harsh") into the suite's own fixes.
// Pure logic and plain files; Ouroboros and Mentalism use it.

#include "Premaster.h"
#include "FixBus.h"
#include "SectionBus.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace suite::delivery
{
    struct Profile
    {
        const char* name;
        bool stems, instrumental, extended, radio, clubEdit;
        int sampleRate;            // 0 = the project's
        const char* format;
        const char* naming;        // <Artist>, <Title>, <Version>
        const char* notes;
    };
    inline const std::vector<Profile>& profiles()
    {
        static const std::vector<Profile> p
        {
            { "Generic label", false, false, true, true, false, 0, "24-bit WAV, no dither", "<Artist> - <Title> (<Version>) [premaster]",
              "Peaks at or under -6 dBFS, nothing on the master, a second of silence at the end." },
            { "Label with stems", true, true, true, true, false, 0, "24-bit WAV, no dither", "<Artist> - <Title> (<Version>) [premaster]",
              "Premasters plus stems (drums, bass, music, vocal, FX), every stem at the same headroom, all starting at bar 1." },
            { "Club and DJ promo", false, false, true, false, true, 0, "24-bit WAV, no dither", "<Artist> - <Title> (<Version>) [premaster]",
              "The extended mix and a club edit with beat-only 32-bar intro and outro." },
            { "Mastering engineer (full pack)", true, true, true, true, true, 0, "32-bit float WAV", "<Title> - <Version> - premaster",
              "Every version, stems, instrumental; 32-bit float, untouched; a reference track and the brief." },
        };
        return p;
    }
    inline const Profile& profile (int i) { return profiles()[(size_t) juce::jlimit (0, (int) profiles().size() - 1, i)]; }

    /** The versions and files a profile asks for. */
    inline juce::StringArray fileList (const Profile& p, const juce::String& artist, const juce::String& title)
    {
        juce::StringArray versions { "Original Mix" };
        if (p.extended) versions.add ("Extended Mix");
        if (p.radio) versions.add ("Radio Edit");
        if (p.clubEdit) versions.add ("Club Edit");
        if (p.instrumental) versions.add ("Instrumental");
        juce::StringArray out;
        for (const auto& v : versions)
            out.add (juce::String (p.naming).replace ("<Artist>", artist.isNotEmpty() ? artist : "Artist").replace ("<Title>", title.isNotEmpty() ? title : "Title")
                         .replace ("<Version>", v) + ".wav");
        if (p.stems)
            for (const auto* s : { "Drums", "Bass", "Music", "Vocal", "FX" })
                out.add ((title.isNotEmpty() ? title : juce::String ("Title")) + " - Stem - " + s + ".wav");
        return out;
    }

    struct Brief
    {
        juce::String artist, title, key, label, platform = "Club and streaming", references, notes;
        double bpm = 138.0;
        juce::String readiness;     // the readiness report
        juce::StringArray versions;
    };
    /** The mastering brief: what the engineer needs to know. */
    inline juce::String briefText (const Brief& b)
    {
        juce::StringArray l;
        l.add ("MASTERING BRIEF");
        l.add ("");
        l.add ("Artist:      " + (b.artist.isNotEmpty() ? b.artist : juce::String ("-")));
        l.add ("Title:       " + (b.title.isNotEmpty() ? b.title : juce::String ("-")));
        l.add ("Key / BPM:   " + (b.key.isNotEmpty() ? b.key : juce::String ("-")) + " / " + juce::String (b.bpm, 1));
        l.add ("Label:       " + (b.label.isNotEmpty() ? b.label : juce::String ("-")));
        l.add ("For:         " + b.platform);
        l.add ("Premasters:  peaks at or under " + juce::String (premaster::peakDbfs, 0) + " dBFS, no limiting, clipping or loudness gain on the master, 24-bit or 32-bit float, no dither");
        if (! b.versions.isEmpty()) { l.add (""); l.add ("Files:"); for (const auto& v : b.versions) l.add ("  " + v); }
        l.add ("");
        l.add ("References:  " + (b.references.isNotEmpty() ? b.references : juce::String ("(add the tracks this should sit with)")));
        l.add ("Notes:       " + (b.notes.isNotEmpty() ? b.notes : juce::String ("Keep the breakdowns' dynamics; the drops are meant to land bigger than the builds.")));
        if (b.readiness.isNotEmpty()) { l.add (""); l.add (b.readiness); }
        return l.joinIntoString ("\n") + "\n";
    }

    /** One note from mastering or the label, turned into a fix the suite can make. */
    struct NoteFix { juce::String heard; Fix fix = Fix::None; uint32_t data = 0; juce::String does; };
    inline std::vector<NoteFix> notesToFixes (const juce::String& notes)
    {
        std::vector<NoteFix> out;
        for (auto line : juce::StringArray::fromTokens (notes, "\n.;", ""))
        {
            const auto t = " " + line.trim().toLowerCase() + " ";
            if (t.trim().isEmpty()) continue;
            int section = (int) SectionType::Drop;
            if (t.contains ("breakdown")) section = (int) SectionType::Breakdown;
            else if (t.contains ("build")) section = (int) SectionType::Build;
            else if (t.contains ("intro")) section = (int) SectionType::Intro;
            else if (t.contains ("outro")) section = (int) SectionType::Outro;
            auto has = [&t] (std::initializer_list<const char*> w) { for (auto* x : w) if (t.contains (x)) return true; return false; };
            const auto where = juce::String (suite::sectionName ((SectionType) section)).toLowerCase();
            const auto lows = has ({ "bass", " sub", "low end", "lows", "low-end" }), tooMuch = has ({ "too loud", "too much", "heavy", "boomy", "overpowering", "too big", "loud" });
            if ((lows && tooMuch) || has ({ "boomy" }))
                out.push_back ({ line.trim(), Fix::SectionLowsDown, (uint32_t) section, "the bass 2 dB down in the " + where });
            else if (has ({ "too loud" }))
                out.push_back ({ line.trim(), Fix::SectionDown, (uint32_t) section, "every track 1 dB down in the " + where });
            if (has ({ "harsh", "too bright", "sharp", "piercing", "fizzy", "brittle" })) out.push_back ({ line.trim(), Fix::Darker, 0, "the master's tilt a dB darker, shared out" });
            if (has ({ "dull", "too dark", "no air", "muffled", "lacks top" })) out.push_back ({ line.trim(), Fix::Brighter, 0, "the master's tilt a dB brighter, shared out" });
            if (has ({ "squashed", "over-compressed", "overcompressed", "no punch", "flat dynamics", "lifeless" })) out.push_back ({ line.trim(), Fix::LessSquash, 0, "every Quintessence lets the drops breathe" });
            if (has ({ "muddy", "mud", "cloudy", "woolly" })) out.push_back ({ line.trim(), Fix::ReverbLowCut, 0, "the reverb returns' low cut up" });
            if (has ({ "distorted", "too much drive", "crunchy", "gritty lead" })) out.push_back ({ line.trim(), Fix::DriveSofter, 0, "every Vibration's drive down" });
            if (has ({ "vocal buried", "vocal too quiet", "vocal low", "can't hear the vocal", "vocal is lost" })) out.push_back ({ line.trim(), Fix::VocalRide, 0, "the vocal ridden over the music" });
            if (has ({ "phasey", "phasing", "lows wide", "wide bass", "mono low", "bass not mono" })) out.push_back ({ line.trim(), Fix::MonoLows, 0, "mono lows on every track" });
            if (has ({ "too wide", "not mono compatible", "falls apart in mono" })) out.push_back ({ line.trim(), Fix::None, 0, "Mentalism's watcher narrows the widest track when it hears the mono loss (play the song)" });
        }
        return out;
    }
}
