#pragma once

// Label profiles: what a label (and a sub-style within it) sounds like, learned from reference
// tracks and from your own tracks it signed. Each reference is measured bar by bar (SongLab),
// its sections proposed (Listen), and summed: length, intro, breakdowns, drops, the drop's impact,
// the breakdown's depth, the tone and width per kind of section, the pacing. A profile keeps the
// median and the spread (so a check knows what's normal and what's an outlier), with recent
// references weighted more. Saved as XML in <Antioch Suite>/Label profiles.

#include "SongLab.h"
#include "Alias.h"
#include <juce_data_structures/juce_data_structures.h>
#include <algorithm>
#include <map>
#include <vector>

namespace lab
{
    using T = suite::SectionType;
    constexpr int numTypes = (int) T::count;

    /** A median with its spread (the 20th and 80th percentiles), from weighted values. */
    struct Stat
    {
        float median = 0.0f, lo = 0.0f, hi = 0.0f;
        int n = 0;
        bool valid() const { return n > 0; }
        /** How far `v` is outside the normal range, in "spreads" (0 = inside). */
        float outside (float v) const
        {
            if (! valid()) return 0.0f;
            const auto half = juce::jmax (0.5f * (hi - lo), 0.25f * std::abs (median) * 0.1f + 0.5f);
            return v < lo ? (lo - v) / half : v > hi ? (v - hi) / half : 0.0f;
        }
        juce::String text (int decimals = 1) const { return valid() ? juce::String (median, decimals) + " (" + juce::String (lo, decimals) + " to " + juce::String (hi, decimals) + ")" : juce::String ("-"); }
    };
    inline Stat statOf (std::vector<std::pair<float, float>> valueWeight)
    {
        Stat s;
        if (valueWeight.empty()) return s;
        std::sort (valueWeight.begin(), valueWeight.end());
        float total = 0.0f;
        for (const auto& [v, w] : valueWeight) total += w;
        auto at = [&] (float q)
        {
            float acc = 0.0f;
            for (const auto& [v, w] : valueWeight) { acc += w; if (acc >= q * total - 1.0e-6f) return v; }
            return valueWeight.back().first;
        };
        s.median = at (0.5f); s.lo = at (0.2f); s.hi = at (0.8f);
        if (valueWeight.size() == 1) { s.lo = s.median - 1.0f; s.hi = s.median + 1.0f; }
        s.n = (int) valueWeight.size();
        return s;
    }

    /** One reference track, measured. */
    struct Reference
    {
        juce::String name, file;
        juce::int64 addedMs = 0;
        bool yours = false;           // your own track the label signed
        double bpm = 138.0, seconds = 0.0;
        suite::SectionMap map;
        std::vector<SectionStat> sections;
        // the summary every check compares with
        float introBars = 0, breakdownBars = 0, drops = 0, impactDb = 0, breakdownDepthDb = 0, buildRiseDb = 0, finalLiftDb = 0, onsetsPerMinute = 0;
        std::array<Bands, numTypes> tone {};
        std::array<float, numTypes> width {}, level {};
        std::array<bool, numTypes> has {};
        juce::String dropProgression;   // the first drop's chords, as steps between roots (SongLab::progressionPrint)

        void summarise()
        {
            int nDrops = 0;
            float dropBars = 0.0f, bdBars = 0.0f;
            std::vector<float> impacts, depths, rises;
            float firstDropDb = -100.0f, lastDropDb = -100.0f, loudestDrop = -100.0f;
            for (size_t i = 0; i < sections.size(); ++i)
            {
                const auto& s = sections[i];
                if (! s.heard()) continue;
                const auto t = (size_t) s.type;
                has[t] = true;
                tone[t] = s.band; width[t] = s.width(); level[t] = s.db;
                if (s.type == T::Intro && i == 0) introBars = (float) s.bars;
                if (s.type == T::Breakdown) { bdBars = juce::jmax (bdBars, (float) s.bars); }
                if (s.type == T::Build) rises.push_back (s.lastDb - s.firstDb);
                if (s.type == T::Drop)
                {
                    ++nDrops; dropBars += (float) s.bars;
                    if (firstDropDb < -99.0f) firstDropDb = s.db;
                    lastDropDb = s.db;
                    loudestDrop = juce::jmax (loudestDrop, s.db);
                    if (i > 0 && sections[i - 1].heard()) impacts.push_back (s.firstDb - sections[i - 1].lastDb);
                }
            }
            for (const auto& s : sections) if (s.type == T::Breakdown && s.heard() && loudestDrop > -99.0f) depths.push_back (loudestDrop - s.db);
            drops = (float) nDrops; breakdownBars = bdBars;
            auto mean = [] (const std::vector<float>& v) { float a = 0; for (auto x : v) a += x; return v.empty() ? 0.0f : a / (float) v.size(); };
            impactDb = mean (impacts); breakdownDepthDb = mean (depths); buildRiseDb = mean (rises);
            finalLiftDb = nDrops >= 2 ? lastDropDb - firstDropDb : 0.0f;
            float onsets = 0.0f; int bars = 0;
            for (const auto& s : sections) { onsets += s.onsetsPerBar * (float) s.heardBars; bars += s.heardBars; }
            onsetsPerMinute = bars > 0 && bpm > 0.0 ? onsets / (float) bars * (float) (bpm / 4.0) : 0.0f;
            juce::ignoreUnused (dropBars);
        }
        /** Measure a file: its bars, its proposed map, its sections. False if it can't be read. */
        bool analyse (const juce::File& f, double referenceBpm)
        {
            const auto bars = analyseFile (f, referenceBpm, seconds);
            if (bars.size() < 16) return false;
            name = f.getFileNameWithoutExtension(); file = f.getFullPathName(); bpm = referenceBpm;
            map = listen::propose (features (bars));
            if (map.count == 0) return false;
            sections = lab::sections (bars, map);
            for (const auto& sec : sections)
                if (sec.type == T::Drop) { dropProgression = progressionPrint (progression (bars, sec.startBar - 1, sec.startBar - 1 + juce::jmin (16, sec.bars))); break; }
            summarise();
            if (addedMs == 0) addedMs = juce::Time::currentTimeMillis();
            return true;
        }
    };

    struct Profile
    {
        juce::String name = "Generic trance", subStyle;
        std::vector<Reference> refs;
        int readyThreshold = 80;      // the overall score that counts as "ready" for this label
        // the norms (from the references)
        Stat minutes, introBars, breakdownBars, drops, impactDb, breakdownDepthDb, buildRiseDb, finalLiftDb, onsetsPerMinute;
        std::array<std::array<Stat, numBands>, numTypes> tone {};   // the tone shape per kind of section
        std::array<Stat, numTypes> width {};
        /** Per check (by name): the lowest score this label accepts; under it the check blocks the gate. */
        std::map<juce::String, float> minScore;

        /** Recent references count more (a label's sound drifts): half the weight every two years. Yours count double. */
        static float weightOf (const Reference& r)
        {
            const auto years = (double) (juce::Time::currentTimeMillis() - r.addedMs) / (365.25 * 24 * 3600 * 1000.0);
            return (float) (std::pow (0.5, juce::jmax (0.0, years) / 2.0) * (r.yours ? 2.0 : 1.0));
        }
        void learn()
        {
            std::vector<std::pair<float, float>> m, ib, bb, dr, im, bd, br, fl, op;
            std::array<std::array<std::vector<std::pair<float, float>>, numBands>, numTypes> tn;
            std::array<std::vector<std::pair<float, float>>, numTypes> wd;
            for (const auto& r : refs)
            {
                const auto w = weightOf (r);
                m.push_back ({ (float) (r.seconds / 60.0), w });
                if (r.introBars > 0) ib.push_back ({ r.introBars, w });
                if (r.breakdownBars > 0) bb.push_back ({ r.breakdownBars, w });
                dr.push_back ({ r.drops, w });
                if (r.drops > 0) { im.push_back ({ r.impactDb, w }); bd.push_back ({ r.breakdownDepthDb, w }); }
                if (std::abs (r.buildRiseDb) > 1.0e-6f) br.push_back ({ r.buildRiseDb, w });
                if (r.drops >= 2) fl.push_back ({ r.finalLiftDb, w });
                op.push_back ({ r.onsetsPerMinute, w });
                for (int t = 0; t < numTypes; ++t)
                {
                    if (! r.has[(size_t) t]) continue;
                    const auto sh = shape (r.tone[(size_t) t]);
                    for (int k = 0; k < numBands; ++k) tn[(size_t) t][(size_t) k].push_back ({ sh[(size_t) k], w });
                    wd[(size_t) t].push_back ({ r.width[(size_t) t], w });
                }
            }
            minutes = statOf (m); introBars = statOf (ib); breakdownBars = statOf (bb); drops = statOf (dr); impactDb = statOf (im);
            breakdownDepthDb = statOf (bd); buildRiseDb = statOf (br); finalLiftDb = statOf (fl); onsetsPerMinute = statOf (op);
            for (int t = 0; t < numTypes; ++t)
            {
                for (int k = 0; k < numBands; ++k) tone[(size_t) t][(size_t) k] = statOf (tn[(size_t) t][(size_t) k]);
                width[(size_t) t] = statOf (wd[(size_t) t]);
            }
        }
        bool empty() const { return refs.empty(); }
        /** The tone shape's median for a kind of section. */
        Bands toneOf (T type) const { Bands b {}; for (int k = 0; k < numBands; ++k) b[(size_t) k] = tone[(size_t) type][(size_t) k].median; return b; }

        /** The closest reference to a song (its sections), and how far (0 = same). */
        std::pair<int, float> closest (const std::vector<SectionStat>& song, double bpm) const
        {
            int best = -1; float bestD = 1.0e9f;
            for (size_t i = 0; i < refs.size(); ++i)
            {
                const auto& r = refs[i];
                float d = (float) std::abs (r.bpm - bpm) * 0.2f, n = 0.0f;
                for (const auto& s : song)
                {
                    if (! s.heard() || ! r.has[(size_t) s.type]) continue;
                    d += distance (s.band, r.tone[(size_t) s.type]); n += 1.0f;
                }
                if (n <= 0.0f) continue;
                d /= n;
                if (d < bestD) { bestD = d; best = (int) i; }
            }
            return { best, bestD };
        }

        juce::ValueTree toTree() const
        {
            juce::ValueTree t ("LabelProfile");
            t.setProperty ("name", name, nullptr); t.setProperty ("subStyle", subStyle, nullptr); t.setProperty ("ready", readyThreshold, nullptr);
            juce::StringArray mins;
            for (const auto& [check, v] : minScore) mins.add (check.replaceCharacters ("=;", "  ") + "=" + juce::String (v, 2));
            t.setProperty ("minScores", mins.joinIntoString (";"), nullptr);
            for (const auto& r : refs)
            {
                juce::ValueTree c ("Reference");
                c.setProperty ("name", r.name, nullptr); c.setProperty ("file", r.file, nullptr); c.setProperty ("added", r.addedMs, nullptr);
                c.setProperty ("yours", r.yours, nullptr); c.setProperty ("bpm", r.bpm, nullptr); c.setProperty ("seconds", r.seconds, nullptr);
                juce::StringArray secs;
                for (const auto& s : r.sections)
                {
                    juce::StringArray f { juce::String ((int) s.type), juce::String (s.startBar), juce::String (s.bars), juce::String (s.heardBars),
                                          juce::String (s.db, 2), juce::String (s.sideDb, 2), juce::String (s.onsetsPerBar, 2), juce::String (s.firstDb, 2), juce::String (s.lastDb, 2) };
                    for (auto b : s.band) f.add (juce::String (b, 2));
                    secs.add (f.joinIntoString (","));
                }
                c.setProperty ("sections", secs.joinIntoString (";"), nullptr);
                c.setProperty ("progression", r.dropProgression, nullptr);
                t.appendChild (c, nullptr);
            }
            return t;
        }
        static Profile fromTree (const juce::ValueTree& t)
        {
            Profile p;
            p.name = t["name"].toString(); p.subStyle = t["subStyle"].toString(); p.readyThreshold = (int) t.getProperty ("ready", 80);
            for (const auto& m : juce::StringArray::fromTokens (t["minScores"].toString(), ";", ""))
                if (m.contains ("=")) p.minScore[m.upToLastOccurrenceOf ("=", false, false)] = juce::jlimit (0.0f, 1.0f, m.fromLastOccurrenceOf ("=", false, false).getFloatValue());
            for (const auto& c : t)
            {
                if (! c.hasType ("Reference")) continue;
                Reference r;
                r.name = c["name"].toString(); r.file = c["file"].toString(); r.addedMs = (juce::int64) c["added"];
                r.yours = (bool) c["yours"]; r.bpm = (double) c["bpm"]; r.seconds = (double) c["seconds"];
                r.dropProgression = c["progression"].toString();
                int idx = 0;
                for (const auto& sec : juce::StringArray::fromTokens (c["sections"].toString(), ";", ""))
                {
                    const auto f = juce::StringArray::fromTokens (sec, ",", "");
                    if (f.size() < 9 + numBands) continue;
                    SectionStat s;
                    s.index = idx++; s.type = (T) juce::jlimit (0, numTypes - 1, f[0].getIntValue()); s.startBar = f[1].getIntValue(); s.bars = f[2].getIntValue();
                    s.heardBars = f[3].getIntValue(); s.db = f[4].getFloatValue(); s.sideDb = f[5].getFloatValue(); s.onsetsPerBar = f[6].getFloatValue();
                    s.firstDb = f[7].getFloatValue(); s.lastDb = f[8].getFloatValue();
                    for (int k = 0; k < numBands; ++k) s.band[(size_t) k] = f[9 + k].getFloatValue();
                    r.sections.push_back (s);
                }
                r.summarise();
                p.refs.push_back (r);
            }
            p.learn();
            return p;
        }
        static juce::File folder() { return suite::Alias::file ("Label profiles"); }
        bool save() const
        {
            folder().createDirectory();
            if (auto xml = toTree().createXml()) return xml->writeTo (folder().getChildFile (juce::File::createLegalFileName (name) + ".xml"));
            return false;
        }
        static Profile load (const juce::String& name)
        {
            if (auto xml = juce::parseXML (folder().getChildFile (juce::File::createLegalFileName (name) + ".xml"))) return fromTree (juce::ValueTree::fromXml (*xml));
            Profile p; p.name = name; return p;
        }
        static juce::StringArray list()
        {
            juce::StringArray out;
            for (const auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.xml")) out.add (f.getFileNameWithoutExtension());
            out.sort (true);
            return out;
        }
    };

    /** Generic trance norms, for when a profile has no references yet. */
    inline Profile genericProfile()
    {
        Profile p;
        p.name = "Generic trance (no references yet)";
        auto st = [] (float m, float lo, float hi) { Stat s; s.median = m; s.lo = lo; s.hi = hi; s.n = 1; return s; };
        p.minutes = st (7.0f, 6.0f, 8.5f); p.introBars = st (32, 16, 32); p.breakdownBars = st (32, 16, 48); p.drops = st (2, 2, 3);
        p.impactDb = st (5.0f, 3.0f, 8.0f); p.breakdownDepthDb = st (7.0f, 5.0f, 10.0f); p.buildRiseDb = st (3.0f, 1.5f, 6.0f);
        p.finalLiftDb = st (0.8f, 0.0f, 2.0f); p.onsetsPerMinute = st (420.0f, 300.0f, 560.0f);
        // tone shapes (dB relative to the band average): a trance drop and breakdown, roughly
        const float drop[numBands] = { 2.5f, 7.0f, 6.0f, 3.0f, 0.5f, -1.0f, -2.0f, -3.5f, -5.5f, -11.0f };
        const float bd[numBands] = { -6.0f, 0.0f, 3.5f, 4.0f, 2.0f, 0.0f, -1.0f, -2.5f, -4.0f, -9.0f };
        for (int t = 0; t < numTypes; ++t)
            for (int k = 0; k < numBands; ++k)
            {
                const auto v = (t == (int) T::Breakdown || t == (int) T::Intro) ? bd[k] : drop[k];
                p.tone[(size_t) t][(size_t) k] = st (v, v - 2.5f, v + 2.5f);
            }
        for (int t = 0; t < numTypes; ++t) p.width[(size_t) t] = st (t == (int) T::Breakdown ? -7.0f : -10.0f, -14.0f, -5.0f);
        return p;
    }
}
