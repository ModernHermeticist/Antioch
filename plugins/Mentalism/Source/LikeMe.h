#pragma once

// "Sounds like me": how close this song is to the way you usually make trance, from the
// style log (keys, tempo, structure and length of the songs you exported; loudness of the
// masters you finished) and your mix DNA (tonal balance). A score out of 100 and a line per
// thing compared, saying where it's like you and where it isn't. Different isn't wrong:
// it's there so you know when a song leaves your sound, and by how much.

#include "MySound.h"
#include "StyleLog.h"
#include <cmath>
#include <map>

namespace likeme
{
    struct Song
    {
        juce::String key;           // "Am", "F" (as Correspondence logs it); empty = unknown
        float bpm = 0.0f;           // 0 = unknown
        juce::String structure;     // "Intro-Breakdown-Drop-Outro"; empty = no map
        int bars = 0;
        float lufs = -100.0f;       // integrated, while it plays
        bool haveBands = false;
        advisor::Bands bands {};
    };

    struct Result
    {
        bool enough = false;        // two or more songs or masters to compare with
        int score = 0;              // 0..100
        juce::StringArray lines;
        juce::String text() const
        {
            if (! enough) return lines.joinIntoString ("\n");
            return "SOUNDS LIKE YOU: " + juce::String (score) + "%\n\n" + lines.joinIntoString ("\n") + "\n";
        }
    };

    inline Result compare (const std::vector<suite::StyleLog::Entry>& entries, const Song& song, const MySound& mine)
    {
        Result r;
        std::vector<const suite::StyleLog::Entry*> songs, masters;
        for (const auto& e : entries)
        {
            if (e.kind == "song") songs.push_back (&e);
            else if (e.kind == "master") masters.push_back (&e);
        }
        if (songs.size() < 2 && masters.size() < 2)
        {
            r.lines.add ("Not enough history yet: export two songs with Correspondence's Export all, or finish two with "
                         "Finish the song, and this compares each new song with them.");
            return r;
        }
        r.enough = true;
        float total = 0.0f;
        int parts = 0;
        auto add = [&] (float score, const juce::String& line)
        {
            total += juce::jlimit (0.0f, 1.0f, score);
            ++parts;
            r.lines.add (juce::String (score >= 0.75f ? "like you   " : score >= 0.4f ? "a bit new  " : "different  ") + line);
        };
        auto values = [&] (const char* field)
        {
            std::vector<float> v;
            for (auto* e : songs) if (e->get (field).getFloatValue() > 0.0f) v.push_back (e->get (field).getFloatValue());
            return v;
        };

        if (song.key.isNotEmpty() && ! songs.empty())
        {
            int same = 0, sameMode = 0;
            for (auto* e : songs)
            {
                same += e->get ("key") == song.key;
                sameMode += e->get ("key").endsWithChar ('m') == song.key.endsWithChar ('m');
            }
            const auto n = (int) songs.size();
            const auto mode = song.key.endsWithChar ('m') ? "minor" : "major";
            if (same > 0) add (juce::jmin (1.0f, 0.5f + (float) same / (float) n), "Key " + song.key + ": you've used it in " + juce::String (same) + " of " + juce::String (n) + " songs");
            else if (sameMode * 2 >= n) add (0.6f, "Key " + song.key + ": a new key for you, but you usually write in " + mode + " too ("
                                                    + suite::StyleLog::topValues (entries, "song", "key", 2) + ")");
            else add (0.2f, "Key " + song.key + ": you rarely write in " + mode + " (usually " + suite::StyleLog::topValues (entries, "song", "key", 2) + ")");
        }
        if (const auto bpms = values ("bpm"); song.bpm > 0.0f && ! bpms.empty())
        {
            const auto usual = suite::StyleLog::median (bpms);
            const auto d = song.bpm - usual;
            add (1.0f - std::abs (d) / 8.0f, "Tempo " + juce::String (song.bpm, 0) + " BPM: "
                     + (std::abs (d) < 1.5f ? juce::String ("your usual") : juce::String (std::abs (d), 0) + (d > 0 ? " faster" : " slower") + " than your usual " + juce::String (usual, 0)));
        }
        if (song.structure.isNotEmpty() && ! songs.empty())
        {
            std::map<juce::String, int> counts;
            juce::String top; int topCount = 0;
            for (auto* e : songs)
                if (e->get ("structure").isNotEmpty())
                    if (const auto c = ++counts[e->get ("structure")]; c > topCount) { topCount = c; top = e->get ("structure"); }
            if (topCount > 0)
            {
                if (song.structure == top) add (1.0f, "Structure: your usual one");
                else if (counts.count (song.structure) > 0) add (0.75f, "Structure: one you've used before (" + juce::String (counts[song.structure]) + " songs)");
                else add (0.3f, "Structure: new for you (you usually go " + top + ")");
            }
        }
        if (const auto bars = values ("bars"); song.bars > 0 && ! bars.empty())
        {
            const auto usual = suite::StyleLog::median (bars);
            const auto d = (float) song.bars - usual;
            add (1.0f - std::abs (d) / juce::jmax (16.0f, 0.4f * usual), "Length " + juce::String (song.bars) + " bars: "
                     + (std::abs (d) < 8.0f ? juce::String ("your usual") : juce::String (std::abs (d), 0) + (d > 0 ? " bars longer" : " bars shorter") + " than your usual " + juce::String (usual, 0)));
        }
        if (masters.size() >= 2 && song.lufs > -70.0f)
        {
            std::vector<float> lufs;
            for (auto* e : masters) lufs.push_back (e->get ("lufs").getFloatValue());
            const auto usual = suite::StyleLog::median (lufs);
            const auto d = song.lufs - usual;
            add (1.0f - std::abs (d) / 4.0f, "Loudness " + juce::String (song.lufs, 1) + " LUFS: "
                     + (std::abs (d) < 1.0f ? juce::String ("your usual") : juce::String (std::abs (d), 1) + (d > 0 ? " LU louder" : " LU quieter") + " than your usual " + juce::String (usual, 1)));
        }
        if (mine.valid && song.haveBands)
        {
            // Shape only: both curves taken relative to their own average.
            float meanSong = 0.0f, meanMine = 0.0f;
            const auto n = (float) song.bands.size();
            for (size_t b = 0; b < song.bands.size(); ++b) { meanSong += song.bands[b] / n; meanMine += mine.bands[b] / n; }
            float sum = 0.0f, worst = 0.0f; size_t worstBand = 0;
            for (size_t b = 0; b < song.bands.size(); ++b)
            {
                const auto d = (song.bands[b] - meanSong) - (mine.bands[b] - meanMine);
                sum += std::abs (d);
                if (std::abs (d) > std::abs (worst)) { worst = d; worstBand = b; }
            }
            const auto avg = sum / n;
            static const char* names[] = { "31 Hz", "63 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz" };
            add (1.0f - avg / 4.0f, "Tone: " + (avg < 1.0f ? juce::String ("your balance")
                                                         : juce::String (std::abs (worst), 1) + " dB " + (worst > 0 ? "more" : "less") + " at "
                                                               + names[juce::jmin ((size_t) 9, worstBand)] + " than your finished songs"));
        }
        r.score = parts > 0 ? juce::roundToInt (100.0f * total / (float) parts) : 0;
        if (parts == 0) r.lines.add ("Nothing to compare yet: play the song (with Correspondence and Ouroboros loaded).");
        return r;
    }
}
