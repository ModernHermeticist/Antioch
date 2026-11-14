#pragma once

// Style log: a line per finished thing, written by the suite as you work, so it can tell you
// how you make trance. Correspondence logs each song it exports (key, tempo, progression,
// length, structure); Mentalism logs each finished master (loudness, peak). Ouroboros reads
// it for the style report. Plain text in <user app data>/Antioch Suite/Style log.txt.

#include "Alias.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace suite::StyleLog
{
    inline juce::File& fileOverride() { static juce::File f; return f; }
    inline void setFileForTests (const juce::File& f) { fileOverride() = f; }
    inline juce::File file()
    {
        if (fileOverride() != juce::File()) return fileOverride();
        return suite::Alias::file ("Style log.txt");
    }

    struct Entry
    {
        juce::Time when;
        juce::String kind;   // "song" or "master"
        std::map<juce::String, juce::String> fields;
        juce::String get (const juce::String& k) const { const auto it = fields.find (k); return it != fields.end() ? it->second : juce::String(); }
    };

    inline void forgetCache();

    /** Appends an entry (tabs and line breaks in values are replaced). */
    inline bool add (const juce::String& kind, const std::map<juce::String, juce::String>& fields)
    {
        juce::StringArray parts { juce::String (juce::Time::getCurrentTime().toMilliseconds()), kind };
        for (const auto& [k, v] : fields)
            parts.add (k + "=" + v.replaceCharacters ("\t\r\n", "   "));
        const auto f = file();
        f.getParentDirectory().createDirectory();
        const auto ok = f.appendText (parts.joinIntoString ("\t") + "\n");
        forgetCache();
        return ok;
    }

    inline std::vector<Entry> parse (const juce::String& text)
    {
        std::vector<Entry> out;
        for (const auto& line : juce::StringArray::fromLines (text))
        {
            const auto parts = juce::StringArray::fromTokens (line, "\t", "");
            if (parts.size() < 2) continue;
            Entry e;
            e.when = juce::Time (parts[0].getLargeIntValue());
            e.kind = parts[1];
            for (int i = 2; i < parts.size(); ++i)
                e.fields[parts[i].upToFirstOccurrenceOf ("=", false, false)] = parts[i].fromFirstOccurrenceOf ("=", false, false);
            out.push_back (std::move (e));
        }
        return out;
    }

    namespace detail
    {
        struct Cache
        {
            std::mutex m;
            juce::File file;
            juce::int64 size = -1;
            juce::Time modified;
            double checkedAt = -1.0e9;
            std::shared_ptr<const std::vector<Entry>> entries = std::make_shared<const std::vector<Entry>>();
        };
        inline Cache& cache() { static Cache c; return c; }
    }

    /** The log, parsed: kept in memory and re-read only when the file changes (looked at no more
        than once a second), so asking for it is cheap on any thread. */
    inline std::shared_ptr<const std::vector<Entry>> readShared()
    {
        auto& c = detail::cache();
        const std::lock_guard<std::mutex> g (c.m);
        const auto f = file();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (f != c.file || now - c.checkedAt > (fileOverride() != juce::File() ? 0.0 : 1000.0))
        {
            c.checkedAt = now;
            const auto size = f.getSize();
            const auto modified = f.getLastModificationTime();
            if (f != c.file || size != c.size || modified != c.modified)
            {
                c.file = f; c.size = size; c.modified = modified;
                c.entries = std::make_shared<const std::vector<Entry>> (size > 0 ? parse (f.loadFileAsString()) : std::vector<Entry>());
            }
        }
        return c.entries;
    }
    /** Forgets the cached copy (after writing to the log). */
    inline void forgetCache() { const std::lock_guard<std::mutex> g (detail::cache().m); detail::cache().checkedAt = -1.0e9; detail::cache().size = -2; }

    /** A copy of the log (see readShared). */
    inline std::vector<Entry> read() { return *readShared(); }

    /** "Am (5), Em (3), F#m (1)": the most common values of a field, with counts. */
    inline juce::String topValues (const std::vector<Entry>& entries, const juce::String& kind, const juce::String& field, int howMany = 3)
    {
        std::map<juce::String, int> counts;
        for (const auto& e : entries)
            if (e.kind == kind && e.get (field).isNotEmpty()) ++counts[e.get (field)];
        std::vector<std::pair<juce::String, int>> sorted (counts.begin(), counts.end());
        std::stable_sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
        juce::StringArray out;
        for (int i = 0; i < juce::jmin (howMany, (int) sorted.size()); ++i)
            out.add (sorted[(size_t) i].first + " (" + juce::String (sorted[(size_t) i].second) + ")");
        return out.joinIntoString (", ");
    }

    inline float median (std::vector<float> v)
    {
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    }

    /** A plain-text summary of your style from the log (and anything the caller adds). */
    inline juce::String report (const std::vector<Entry>& entries, const juce::StringArray& extra = {})
    {
        std::vector<float> bpms, bars, lufs, peaks;
        int songs = 0, minor = 0, masters = 0;
        for (const auto& e : entries)
        {
            if (e.kind == "song")
            {
                ++songs;
                if (e.get ("bpm").isNotEmpty()) bpms.push_back (e.get ("bpm").getFloatValue());
                if (e.get ("bars").getIntValue() > 0) bars.push_back ((float) e.get ("bars").getIntValue());
                if (e.get ("key").endsWithChar ('m')) ++minor;
            }
            else if (e.kind == "master")
            {
                ++masters;
                lufs.push_back (e.get ("lufs").getFloatValue());
                peaks.push_back (e.get ("truepeak").getFloatValue());
            }
        }
        juce::StringArray lines;
        lines.add ("HOW YOU MAKE TRANCE");
        lines.add ("Songs logged: " + juce::String (songs) + "; finished masters: " + juce::String (masters));
        if (songs > 0)
        {
            lines.add ("");
            lines.add ("Keys: " + topValues (entries, "song", "key") + " (" + juce::String (juce::roundToInt (100.0 * minor / songs)) + "% minor)");
            if (! bpms.empty())
                lines.add ("Tempo: usually " + juce::String (median (bpms), 0) + " BPM (" + juce::String (*std::min_element (bpms.begin(), bpms.end()), 0)
                           + "-" + juce::String (*std::max_element (bpms.begin(), bpms.end()), 0) + ")");
            lines.add ("Progressions: " + topValues (entries, "song", "degrees"));
            if (! bars.empty() && ! bpms.empty())
            {
                const auto seconds = median (bars) * 4.0f * 60.0f / juce::jmax (1.0f, median (bpms));
                lines.add ("Length: usually " + juce::String (median (bars), 0) + " bars (" + juce::String ((int) seconds / 60) + ":"
                           + juce::String ((int) seconds % 60).paddedLeft ('0', 2) + ")");
            }
            lines.add ("Structures: " + topValues (entries, "song", "structure", 2));
        }
        if (masters > 0)
            lines.add ("Masters: usually " + juce::String (median (lufs), 1) + " LUFS integrated, peaks at " + juce::String (median (peaks), 1) + " dBTP");
        if (songs == 0 && masters == 0)
            lines.add ("Nothing logged yet: songs are logged when Correspondence exports them (Export all), masters when "
                       "Mentalism's Finish the song completes.");
        if (! extra.isEmpty()) { lines.add (""); lines.addArray (extra); }
        return lines.joinIntoString ("\n") + "\n";
    }
}
