#pragma once

// My sound: a loudness and tonal target learned from your own finished tracks, used in
// place of the genre targets. It's the average of the tracks' integrated loudness, true
// peak and long-term octave-band balance, kept in <user app data>/Antioch Suite.

#include "Alias.h"
#include "Advisor.h"
#include <juce_core/juce_core.h>
#include "StyleLog.h"

struct MySound
{
    bool valid = false;
    int tracks = 0;
    float lufs = -100.0f, truePeakDb = -100.0f;
    advisor::Bands bands {};
    juce::StringArray names;

    static juce::File& fileOverride() { static juce::File f; return f; }   // tests
    static juce::File file()
    {
        if (fileOverride() != juce::File()) return fileOverride();
        return suite::Alias::file ("My sound.txt");
    }

    /** Average of several analysed tracks (any with a valid measurement count). */
    template <typename Profile>
    static MySound average (const std::vector<Profile>& profiles)
    {
        MySound m;
        double lufs = 0.0, tp = 0.0;
        advisor::Bands sum {};
        for (const auto& p : profiles)
        {
            if (! p.valid) continue;
            lufs += p.lufs; tp += p.truePeakDb;
            for (size_t b = 0; b < sum.size(); ++b) sum[b] += p.bands[b];
            m.names.add (p.name);
            ++m.tracks;
        }
        if (m.tracks == 0) return m;
        m.lufs = (float) (lufs / m.tracks);
        m.truePeakDb = (float) (tp / m.tracks);
        for (size_t b = 0; b < sum.size(); ++b) m.bands[b] = sum[b] / (float) m.tracks;
        m.valid = true;
        return m;
    }

    /** The ceiling to aim for: your tracks' typical true peak, kept between -1.0 and -0.1 dBTP. */
    float ceiling() const { return juce::jlimit (-1.0f, -0.1f, truePeakDb); }

    juce::String toText() const
    {
        juce::StringArray b;
        for (auto v : bands) b.add (juce::String (v, 2));
        return "tracks " + juce::String (tracks) + "\nlufs " + juce::String (lufs, 2) + "\ntruepeak " + juce::String (truePeakDb, 2)
             + "\nbands " + b.joinIntoString (",") + "\nnames " + names.joinIntoString ("|") + "\n";
    }

    static MySound fromText (const juce::String& text)
    {
        MySound m;
        bool haveBands = false;
        for (const auto& line : juce::StringArray::fromLines (text))
        {
            const auto key = line.upToFirstOccurrenceOf (" ", false, false), value = line.fromFirstOccurrenceOf (" ", false, false);
            if (key == "tracks") m.tracks = value.getIntValue();
            else if (key == "lufs") m.lufs = value.getFloatValue();
            else if (key == "truepeak") m.truePeakDb = value.getFloatValue();
            else if (key == "names") m.names = juce::StringArray::fromTokens (value, "|", "");
            else if (key == "bands")
            {
                const auto v = juce::StringArray::fromTokens (value, ",", "");
                if (v.size() == (int) m.bands.size())
                {
                    for (size_t b = 0; b < m.bands.size(); ++b) m.bands[b] = v[(int) b].getFloatValue();
                    haveBands = true;
                }
            }
        }
        m.valid = haveBands && m.tracks > 0 && m.lufs > -70.0f;
        return m;
    }

    bool save (const juce::File& f) const { f.getParentDirectory().createDirectory(); return f.replaceWithText (toText()); }
    static MySound load (const juce::File& f) { return f.existsAsFile() ? fromText (f.loadFileAsString()) : MySound {}; }

    /** Mix DNA: your sound learned by itself from the masters you finished (Finish the song logs
        each one's loudness, peak and tonal balance in the style log). Two or more are needed. */
    static constexpr const char* fromSongsName = "(your finished songs)";
    bool learnedFromSongs() const { return names.size() == 1 && names[0] == fromSongsName; }
    static MySound fromFinishedSongs (const std::vector<suite::StyleLog::Entry>& entries)
    {
        MySound m;
        std::vector<float> lufs, peaks;
        std::vector<advisor::Bands> shapes;
        for (const auto& e : entries)
        {
            if (e.kind != "master") continue;
            const auto v = juce::StringArray::fromTokens (e.get ("bands"), ",", "");
            if (v.size() != (int) m.bands.size()) continue;
            advisor::Bands b {};
            for (size_t i = 0; i < b.size(); ++i) b[i] = v[(int) i].getFloatValue();
            shapes.push_back (b);
            lufs.push_back (e.get ("lufs").getFloatValue());
            peaks.push_back (e.get ("truepeak").getFloatValue());
        }
        if (shapes.size() < 2) return m;
        // The median of each, so one odd master doesn't pull your sound around.
        for (size_t i = 0; i < m.bands.size(); ++i)
        {
            std::vector<float> col;
            for (const auto& b : shapes) col.push_back (b[i]);
            m.bands[i] = suite::StyleLog::median (col);
        }
        m.lufs = suite::StyleLog::median (lufs);
        m.truePeakDb = suite::StyleLog::median (peaks);
        m.tracks = (int) shapes.size();
        m.names = { fromSongsName };
        m.valid = m.lufs > -70.0f;
        return m;
    }
};
