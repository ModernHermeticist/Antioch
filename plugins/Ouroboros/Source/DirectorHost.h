#pragma once

// Ouroboros' side of the suite director: your choices (story, genre, safe mode, the bigger /
// smaller knob, locks, re-rolls, freezes, pins, painted intent, requests, ratings, gestures you
// taught it, templates), the plans it has made (history, A/B), what it heard (the rehearsal pass,
// Mentalism's measurements, a reference) and what the suite actually did (the fidelity check).
// Message thread only. The plan itself is made by suite::director::makePlan.

#include "SuiteDirector.h"
#include "DirectorBus.h"
#include "Registry.h"
#include "WritingBus.h"
#include "KitBus.h"
#include "AutopilotBus.h"
#include "StyleLog.h"
#include "Alias.h"
#include "MoveLog.h"
#include "Listen.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ouro
{
    namespace dr = suite::director;

    class DirectorHost
    {
    public:
        struct Settings
        {
            bool enabled = true, safe = false, versionLock = false;
            bool silencesBeforeDrops = false;   // the Snap and the Breath: off by default
            int story = dr::storyClassic, genre = dr::genreAuto;
            float bigger = 0.0f;
            uint32_t seed = 1;
            std::map<uint32_t, int> rerolls;
            std::map<uint32_t, std::pair<uint8_t, uint8_t>> locks;
            std::map<uint32_t, dr::Moment> frozen;
            std::vector<uint8_t> intent;
            std::vector<float> sectionSpice;
            std::vector<std::pair<int, int>> noMoves;
            std::vector<dr::Pin> pins;
            std::map<uint32_t, int> heroRole;
            std::vector<int> sectionMood;
            std::vector<dr::Request> requests;
            std::array<float, dr::numGestures> taste {};
            std::vector<dr::Learned> learned;
        };
        struct HistoryEntry { juce::String label; juce::Time when; Settings settings; uint32_t hash = 0; };
        struct Template { juce::String name; dr::Moment moment; };

        Settings settings;
        dr::Plan plan, previous, lockedPlan;
        bool havePlan = false, abPrevious = false;
        std::vector<HistoryEntry> history;
        juce::StringArray lastDiff;
        std::vector<Template> templates;
        dr::ReferenceStyle reference;
        std::vector<float> barLoudness, barDensity;
        int rehearsalBars = 0;
        uint8_t presentOverride = 0;   // tests: the players to treat as present (0 = from the registry)

        //==============================================================================
        /** The planner's input from the map, your choices and what the suite hears. */
        dr::Input inputFor (const suite::SectionMap& map, double bpm, double barQ) const
        {
            dr::Input in;
            in.map = map;
            in.bpm = bpm; in.barQ = barQ;
            in.story = settings.story; in.genre = settings.genre; in.safe = settings.safe; in.bigger = settings.bigger;
            in.silencesBeforeDrops = settings.silencesBeforeDrops;
            in.seed = settings.seed;
            in.present = presentOverride != 0 ? presentOverride : presentPlayers();
            in.rerolls = settings.rerolls; in.locks = settings.locks; in.frozen = settings.frozen;
            in.intent = settings.intent; in.sectionSpice = settings.sectionSpice; in.noMoves = settings.noMoves; in.pins = settings.pins;
            in.heroRole = settings.heroRole; in.sectionMood = settings.sectionMood; in.requests = settings.requests;
            in.taste = settings.taste; in.learned = settings.learned;
            if (rehearsalBars >= 8) { in.barLoudness = barLoudness; in.barDensity = barDensity; }
            in.feedback = suite::DirectorBus::feedback();
            in.reference = reference;
            // the vocal's phrases (notes closer than a beat joined) and the hook's rests
            std::vector<std::pair<double, double>> phrases;
            for (const auto& n : suite::WritingBus::sung())
            {
                if (! phrases.empty() && n.start - phrases.back().second < 1.0) phrases.back().second = juce::jmax (phrases.back().second, n.start + n.length);
                else phrases.push_back ({ n.start, n.start + n.length });
            }
            in.vocal = phrases;
            for (const auto& [s, e] : suite::KitBus::rests()) in.hookRests.push_back ({ (double) s, (double) e });
            return in;
        }

        /** The players whose plugins are in the session (Scenes and Silence always are). */
        static uint8_t presentPlayers()
        {
            const auto entries = suite::Registry::list();
            uint8_t bits = (uint8_t) ((1u << dr::pScene) | (1u << dr::pSilence));
            bool any = false;
            for (const auto& e : entries)
                for (int p = 0; p < dr::pScene; ++p)
                    if (e.plugin == dr::pluginOf (p)) { bits = (uint8_t) (bits | (1u << p)); any = true; }
            return any ? bits : (uint8_t) 0x7f;   // nothing announced (a fresh session or a test): plan for everyone
        }

        /** Plans (unless the version is locked) and publishes. True if the plan changed. */
        bool update (const suite::SectionMap& map, double bpm, double barQ)
        {
            if (! settings.enabled || map.count == 0)
            {
                if (havePlan) suite::DirectorBus::withdraw();
                havePlan = false;
                return false;
            }
            dr::Plan fresh;
            if (settings.versionLock && ! lockedPlan.moments.empty()) fresh = lockedPlan;
            else fresh = dr::makePlan (inputFor (map, bpm, barQ));
            const auto changed = ! havePlan || fresh.hash != plan.hash;
            if (changed)
            {
                if (havePlan)
                {
                    lastDiff = dr::diff (plan, fresh, barQ);
                    previous = plan;
                }
                plan = std::move (fresh);
                havePlan = true;
            }
            const auto& out = abPrevious && ! previous.moments.empty() ? previous : plan;
            suite::DirectorBus::publish (out, barQ, bpm, abPrevious);
            return changed;
        }

        //==============================================================================
        // Per moment: lock, re-roll, freeze, undo
        void remember (uint32_t key, const juce::String& label)
        {
            MomentState s;
            s.hadLock = settings.locks.count (key) > 0; if (s.hadLock) s.lock = settings.locks[key];
            s.hadFrozen = settings.frozen.count (key) > 0; if (s.hadFrozen) s.frozen = settings.frozen[key];
            s.reroll = settings.rerolls.count (key) > 0 ? settings.rerolls[key] : 0;
            s.hadHero = settings.heroRole.count (key) > 0; if (s.hadHero) s.heroRole = settings.heroRole[key];
            undo[key].push_back (s);
            if (undo[key].size() > 32) undo[key].erase (undo[key].begin());
            pushHistory (label);
        }
        bool lock (uint32_t key)
        {
            const auto i = plan.indexOf (key);
            if (i < 0) return false;
            remember (key, "Lock bar " + barText (key));
            settings.locks[key] = { plan.moments[(size_t) i].hero, plan.moments[(size_t) i].heroGesture };
            return true;
        }
        bool unlock (uint32_t key) { if (settings.locks.count (key) == 0) return false; remember (key, "Unlock bar " + barText (key)); settings.locks.erase (key); return true; }
        bool reroll (uint32_t key)
        {
            if (plan.indexOf (key) < 0) return false;
            remember (key, "Re-roll bar " + barText (key));
            settings.locks.erase (key);
            settings.frozen.erase (key);
            ++settings.rerolls[key];
            return true;
        }
        bool freeze (uint32_t key, bool on)
        {
            const auto i = plan.indexOf (key);
            if (i < 0) return false;
            remember (key, (on ? "Freeze bar " : "Unfreeze bar ") + barText (key));
            if (on) settings.frozen[key] = plan.moments[(size_t) i];
            else settings.frozen.erase (key);
            return true;
        }
        /** Your own version of a moment (from the inspector): kept as is, like a freeze. */
        bool setMoment (const dr::Moment& m, const juce::String& label = "Edit")
        {
            remember (m.key, label + " bar " + barText (m.key));
            auto copy = m;
            copy.flags = (uint16_t) (copy.flags | dr::fFrozen);
            settings.frozen[m.key] = copy;
            return true;
        }
        bool undoMoment (uint32_t key)
        {
            auto it = undo.find (key);
            if (it == undo.end() || it->second.empty()) return false;
            const auto s = it->second.back();
            it->second.pop_back();
            if (s.hadLock) settings.locks[key] = s.lock; else settings.locks.erase (key);
            if (s.hadFrozen) settings.frozen[key] = s.frozen; else settings.frozen.erase (key);
            if (s.reroll > 0) settings.rerolls[key] = s.reroll; else settings.rerolls.erase (key);
            if (s.hadHero) settings.heroRole[key] = s.heroRole; else settings.heroRole.erase (key);
            pushHistory ("Undo bar " + barText (key));
            return true;
        }
        int undoDepth (uint32_t key) const { const auto it = undo.find (key); return it != undo.end() ? (int) it->second.size() : 0; }
        void rerollSong() { pushHistory ("Re-roll the song"); settings.seed = settings.seed * 1664525u + 1013904223u; settings.rerolls.clear(); }
        bool setHeroRole (uint32_t key, int role)
        {
            remember (key, "Lead track at bar " + barText (key));
            if (role <= 0) settings.heroRole.erase (key); else settings.heroRole[key] = role;
            return true;
        }

        //==============================================================================
        // History: every plan you've heard, to step back to
        void pushHistory (const juce::String& label)
        {
            history.push_back ({ label, juce::Time::getCurrentTime(), settings, plan.hash });
            if (history.size() > 64) history.erase (history.begin());
        }
        bool stepBack (int index)
        {
            if (index < 0 || index >= (int) history.size()) return false;
            const auto target = history[(size_t) index].settings;
            pushHistory ("Back to: " + history[(size_t) index].label);
            settings = target;
            return true;
        }

        //==============================================================================
        // Steering
        void setIntent (int section, int intent) { grow (settings.intent, section, (uint8_t) dr::iAuto); settings.intent[(size_t) section] = (uint8_t) juce::jlimit (0, (int) dr::numIntents - 1, intent); }
        void setSectionSpice (int section, float v) { grow (settings.sectionSpice, section, 1.0f); settings.sectionSpice[(size_t) section] = juce::jlimit (0.0f, 2.0f, v); }
        void setSectionMood (int section, int mood) { grow (settings.sectionMood, section, -1); settings.sectionMood[(size_t) section] = juce::jlimit (-1, (int) dr::numMoods - 1, mood); }
        void addNoMoves (int fromBar, int toBar) { settings.noMoves.push_back ({ juce::jmin (fromBar, toBar), juce::jmax (fromBar, toBar) }); }
        void clearNoMoves() { settings.noMoves.clear(); }
        void pin (const dr::Pin& p)
        {
            settings.pins.erase (std::remove_if (settings.pins.begin(), settings.pins.end(), [&] (const dr::Pin& x) { return x.bar == p.bar; }), settings.pins.end());
            if (p.player != dr::pNone) settings.pins.push_back (p);
        }

        /** Director chat: a request in plain words. Returns the reply. "undo" takes the last one back, "reset" clears them. */
        juce::String chat (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            if (t.isEmpty()) return {};
            if (t == "undo" || t == "take that back")
            {
                if (settings.requests.empty()) return "Nothing to take back.";
                pushHistory ("Chat: undo");
                const auto r = settings.requests.back();
                settings.requests.pop_back();
                return "Took back: " + dr::describe (r);
            }
            if (t == "reset" || t == "forget it" || t == "clear")
            {
                pushHistory ("Chat: reset");
                settings.requests.clear();
                return "Forgot every request.";
            }
            if (t.contains ("re-roll") || t.contains ("reroll") || t.contains ("something else"))
            {
                rerollSong();
                return "New dice for the whole song (locked and frozen moments stay).";
            }
            if (t.contains ("silence before") || t.contains ("silences before") || t.contains ("allow silence") || t.contains ("no silence"))
            {
                pushHistory ("Chat: silences before drops");
                settings.silencesBeforeDrops = ! (t.contains ("no ") || t.contains (" off") || t.contains ("without"));
                return settings.silencesBeforeDrops ? "Silences before drops on: the Snap and the Breath can be used again." : "No full silence before any drop (Snap and Breath off).";
            }
            if (t.contains ("safe mode")) { pushHistory ("Chat: safe mode"); settings.safe = ! t.contains ("off"); return settings.safe ? "Safe mode: only filters, levels and space." : "Safe mode off."; }
            for (int s = 0; s < dr::numStories; ++s)
                if (t.contains (juce::String (dr::storyName (s)).toLowerCase()))
                { pushHistory ("Chat: story"); settings.story = s; return juce::String ("Story: ") + dr::storyName (s) + "."; }
            const auto r = dr::parseRequest (text);
            if (r.words == 0) return "I didn't catch a request. Try \"darker\", \"hit harder\", \"less glitch in the breakdown\", \"more silence before the final drop\", \"simpler\" or \"wider\".";
            pushHistory ("Chat: " + text.trim());
            settings.requests.erase (std::remove (settings.requests.begin(), settings.requests.end(), r), settings.requests.end());
            settings.requests.push_back (r);
            return "OK: " + dr::describe (r) + ". Re-planned.";
        }

        //==============================================================================
        // Taste: ratings, teach by example, templates
        bool rate (uint32_t key, int delta)
        {
            const auto i = plan.indexOf (key);
            if (i < 0) return false;
            const auto& m = plan.moments[(size_t) i];
            if (m.hero == dr::pNone && m.silence == dr::gNone) return false;
            for (int p = 0; p < dr::numPlayers; ++p)
                if (m.acts (p) && p != dr::pScene)
                    settings.taste[m.gesture[(size_t) p]] = juce::jlimit (-8.0f, 8.0f, settings.taste[m.gesture[(size_t) p]] + (m.hero == p ? 1.0f : 0.5f) * (float) delta);
            std::map<juce::String, juce::String> f;
            f["gesture"] = dr::gesture (m.heroGesture != dr::gNone ? m.heroGesture : m.silence).name;
            f["kind"] = dr::kindName (m.kind);
            f["rating"] = juce::String (delta);
            suite::StyleLog::add ("director-rating", f);
            return true;
        }
        /** Teach by example: this moment, as it is now, becomes a gesture for this kind of moment. */
        bool teach (uint32_t key, const juce::String& name)
        {
            const auto i = plan.indexOf (key);
            if (i < 0) return false;
            dr::Learned l;
            l.kind = plan.moments[(size_t) i].kind == dr::kPin ? (uint16_t) dr::kIntoDrop : plan.moments[(size_t) i].kind;
            l.recipe = plan.moments[(size_t) i];
            l.name = name.isNotEmpty() ? name : juce::String ("My ") + dr::kindName (l.kind);
            settings.learned.push_back (l);
            pushHistory ("Taught: " + l.name);
            return true;
        }
        void saveTemplate (const juce::String& name, uint32_t key)
        {
            const auto i = plan.indexOf (key);
            if (i < 0 || name.isEmpty()) return;
            templates.erase (std::remove_if (templates.begin(), templates.end(), [&] (const Template& t) { return t.name == name; }), templates.end());
            templates.push_back ({ name, plan.moments[(size_t) i] });
            saveTemplates();
        }
        bool applyTemplate (const juce::String& name, uint32_t key)
        {
            const auto i = plan.indexOf (key);
            for (const auto& t : templates)
                if (t.name == name && i >= 0)
                {
                    auto m = t.moment;
                    const auto& target = plan.moments[(size_t) i];
                    const auto shift = target.atQ - m.atQ;
                    m.key = target.key; m.kind = target.kind; m.section = target.section; m.dropOrdinal = target.dropOrdinal;
                    m.atQ += shift; m.startQ += shift; m.endQ += shift; m.silenceStartQ += shift; m.silenceEndQ += shift; m.editStartQ += shift; m.editEndQ += shift;
                    for (auto& l : m.legs) if (l.player != dr::pNone) l.atQ += shift;
                    return setMoment (m, "Template \"" + name + "\" at");
                }
            return false;
        }
        static juce::File directorFolder() { return suite::Alias::file ("Director"); }
        void saveTemplates() const
        {
            juce::ValueTree t ("Templates");
            for (const auto& x : templates) { juce::ValueTree c ("Template"); c.setProperty ("name", x.name, nullptr); c.setProperty ("m", dr::momentToString (x.moment), nullptr); t.appendChild (c, nullptr); }
            directorFolder().createDirectory();
            if (auto xml = t.createXml()) xml->writeTo (directorFolder().getChildFile ("Templates.xml"));
        }
        void loadTemplates()
        {
            templates.clear();
            if (auto xml = juce::parseXML (directorFolder().getChildFile ("Templates.xml")))
                for (const auto* c : xml->getChildWithTagNameIterator ("Template"))
                {
                    Template x; x.name = c->getStringAttribute ("name");
                    if (dr::momentFromString (c->getStringAttribute ("m"), x.moment)) templates.push_back (x);
                }
        }

        //==============================================================================
        // Style profiles and gesture libraries: save, share, import
        juce::ValueTree profileTree (bool withMoments) const
        {
            juce::ValueTree t ("DirectorProfile");
            t.setProperty ("story", settings.story, nullptr);
            t.setProperty ("genre", settings.genre, nullptr);
            t.setProperty ("safe", settings.safe, nullptr);
            t.setProperty ("silences", settings.silencesBeforeDrops, nullptr);
            t.setProperty ("bigger", settings.bigger, nullptr);
            juce::StringArray taste;
            for (auto v : settings.taste) taste.add (juce::String (v, 2));
            t.setProperty ("taste", taste.joinIntoString (","), nullptr);
            juce::StringArray req;
            for (const auto& r : settings.requests) req.add (juce::String (r.words) + "/" + juce::String (r.sectionType) + "/" + juce::String (r.dropOrdinal));
            t.setProperty ("requests", req.joinIntoString (","), nullptr);
            for (const auto& l : settings.learned)
            {
                juce::ValueTree c ("Learned");
                c.setProperty ("name", l.name, nullptr); c.setProperty ("kind", (int) l.kind, nullptr); c.setProperty ("m", dr::momentToString (l.recipe), nullptr);
                t.appendChild (c, nullptr);
            }
            if (withMoments)
            {
                t.setProperty ("enabled", settings.enabled, nullptr);
                t.setProperty ("seed", (juce::int64) settings.seed, nullptr);
                t.setProperty ("versionLock", settings.versionLock, nullptr);
                t.setProperty ("ab", abPrevious, nullptr);
                auto mapText = [] (const auto& m, auto&& value) { juce::StringArray a; for (const auto& [k, v] : m) a.add (juce::String ((juce::int64) k) + ":" + value (v)); return a.joinIntoString (";"); };
                t.setProperty ("rerolls", mapText (settings.rerolls, [] (int v) { return juce::String (v); }), nullptr);
                t.setProperty ("locks", mapText (settings.locks, [] (const std::pair<uint8_t, uint8_t>& v) { return juce::String (v.first) + "/" + juce::String (v.second); }), nullptr);
                t.setProperty ("heroRole", mapText (settings.heroRole, [] (int v) { return juce::String (v); }), nullptr);
                juce::StringArray intent, spice, mood, nm, pins;
                for (auto v : settings.intent) intent.add (juce::String (v));
                for (auto v : settings.sectionSpice) spice.add (juce::String (v, 2));
                for (auto v : settings.sectionMood) mood.add (juce::String (v));
                for (const auto& [a, b] : settings.noMoves) nm.add (juce::String (a) + "-" + juce::String (b));
                for (const auto& p : settings.pins) pins.add (juce::String (p.bar) + "/" + juce::String (p.player) + "/" + juce::String (p.gesture));
                t.setProperty ("intent", intent.joinIntoString (","), nullptr);
                t.setProperty ("spice", spice.joinIntoString (","), nullptr);
                t.setProperty ("mood", mood.joinIntoString (","), nullptr);
                t.setProperty ("noMoves", nm.joinIntoString (","), nullptr);
                t.setProperty ("pins", pins.joinIntoString (","), nullptr);
                for (const auto& [k, m] : settings.frozen) { juce::ValueTree c ("Frozen"); c.setProperty ("m", dr::momentToString (m), nullptr); t.appendChild (c, nullptr); }
                for (const auto& m : lockedPlan.moments) { juce::ValueTree c ("Locked"); c.setProperty ("m", dr::momentToString (m), nullptr); t.appendChild (c, nullptr); }
                if (! lockedPlan.moments.empty()) t.setProperty ("lockedHash", (juce::int64) lockedPlan.hash, nullptr);
            }
            return t;
        }
        void applyProfileTree (const juce::ValueTree& t, bool withMoments)
        {
            if (! t.hasType ("DirectorProfile")) return;
            settings.story = juce::jlimit (0, (int) dr::numStories - 1, (int) t.getProperty ("story", settings.story));
            settings.genre = juce::jlimit (0, (int) dr::numGenres - 1, (int) t.getProperty ("genre", settings.genre));
            settings.safe = (bool) t.getProperty ("safe", settings.safe);
            settings.silencesBeforeDrops = (bool) t.getProperty ("silences", false);
            settings.bigger = (float) t.getProperty ("bigger", settings.bigger);
            const auto taste = juce::StringArray::fromTokens (t["taste"].toString(), ",", "");
            for (int i = 0; i < dr::numGestures && i < taste.size(); ++i) settings.taste[(size_t) i] = taste[i].getFloatValue();
            settings.requests.clear();
            for (const auto& r : juce::StringArray::fromTokens (t["requests"].toString(), ",", ""))
            {
                const auto x = juce::StringArray::fromTokens (r, "/", "");
                if (x.size() >= 3) settings.requests.push_back ({ (uint16_t) x[0].getIntValue(), x[1].getIntValue(), x[2].getIntValue() });
            }
            settings.learned.clear();
            for (const auto& c : t)
                if (c.hasType ("Learned"))
                {
                    dr::Learned l; l.name = c["name"].toString(); l.kind = (uint16_t) (int) c["kind"];
                    if (dr::momentFromString (c["m"].toString(), l.recipe)) settings.learned.push_back (l);
                }
            if (! withMoments) return;
            settings.enabled = (bool) t.getProperty ("enabled", true);
            settings.seed = (uint32_t) (juce::int64) t.getProperty ("seed", 1);
            settings.versionLock = (bool) t.getProperty ("versionLock", false);
            abPrevious = (bool) t.getProperty ("ab", false);
            auto readMap = [] (const juce::String& text, auto&& put) { for (const auto& e : juce::StringArray::fromTokens (text, ";", "")) put ((uint32_t) e.upToFirstOccurrenceOf (":", false, false).getLargeIntValue(), e.fromFirstOccurrenceOf (":", false, false)); };
            settings.rerolls.clear(); settings.locks.clear(); settings.heroRole.clear(); settings.frozen.clear();
            readMap (t["rerolls"].toString(), [this] (uint32_t k, const juce::String& v) { settings.rerolls[k] = v.getIntValue(); });
            readMap (t["locks"].toString(), [this] (uint32_t k, const juce::String& v) { settings.locks[k] = { (uint8_t) v.upToFirstOccurrenceOf ("/", false, false).getIntValue(), (uint8_t) v.fromFirstOccurrenceOf ("/", false, false).getIntValue() }; });
            readMap (t["heroRole"].toString(), [this] (uint32_t k, const juce::String& v) { settings.heroRole[k] = v.getIntValue(); });
            settings.intent.clear(); settings.sectionSpice.clear(); settings.sectionMood.clear(); settings.noMoves.clear(); settings.pins.clear();
            for (const auto& v : juce::StringArray::fromTokens (t["intent"].toString(), ",", "")) settings.intent.push_back ((uint8_t) v.getIntValue());
            for (const auto& v : juce::StringArray::fromTokens (t["spice"].toString(), ",", "")) settings.sectionSpice.push_back (v.getFloatValue());
            for (const auto& v : juce::StringArray::fromTokens (t["mood"].toString(), ",", "")) settings.sectionMood.push_back (v.getIntValue());
            for (const auto& v : juce::StringArray::fromTokens (t["noMoves"].toString(), ",", ""))
                settings.noMoves.push_back ({ v.upToFirstOccurrenceOf ("-", false, false).getIntValue(), v.fromFirstOccurrenceOf ("-", false, false).getIntValue() });
            for (const auto& v : juce::StringArray::fromTokens (t["pins"].toString(), ",", ""))
            {
                const auto x = juce::StringArray::fromTokens (v, "/", "");
                if (x.size() >= 3) settings.pins.push_back ({ x[0].getIntValue(), (uint8_t) x[1].getIntValue(), (uint8_t) x[2].getIntValue() });
            }
            lockedPlan = {};
            for (const auto& c : t)
            {
                dr::Moment m;
                if (c.hasType ("Frozen") && dr::momentFromString (c["m"].toString(), m)) settings.frozen[m.key] = m;
                if (c.hasType ("Locked") && dr::momentFromString (c["m"].toString(), m)) lockedPlan.moments.push_back (m);
            }
            lockedPlan.hash = (uint32_t) (juce::int64) t.getProperty ("lockedHash", 0);
            lockedPlan.story = settings.story;
            lockedPlan.safe = settings.safe;
        }
        bool saveProfile (const juce::File& f) const
        {
            f.getParentDirectory().createDirectory();
            if (auto xml = profileTree (false).createXml()) return xml->writeTo (f);
            return false;
        }
        bool loadProfile (const juce::File& f)
        {
            auto xml = juce::parseXML (f);
            if (xml == nullptr) return false;
            pushHistory ("Profile: " + f.getFileNameWithoutExtension());
            applyProfileTree (juce::ValueTree::fromXml (*xml), false);
            return true;
        }
        static juce::File profileFolder() { return directorFolder().getChildFile ("Profiles"); }
        static juce::StringArray profiles()
        {
            juce::StringArray out;
            for (const auto& f : profileFolder().findChildFiles (juce::File::findFiles, false, "*.xml")) out.add (f.getFileNameWithoutExtension());
            out.sort (true);
            return out;
        }

        /** Version lock: the plan as it is now is kept and reproduced exactly, whatever changes. */
        void setVersionLock (bool on)
        {
            pushHistory (on ? "Version locked" : "Version unlocked");
            settings.versionLock = on;
            lockedPlan = on ? plan : dr::Plan {};
        }

        //==============================================================================
        // The rehearsal pass: per bar, how loud and how busy the song is (from the Quintessences)
        void hearBar (int bar, float loudnessDb, float density)
        {
            if (bar < 1 || bar > 1024) return;
            if ((int) barLoudness.size() < bar) { barLoudness.resize ((size_t) bar, -100.0f); barDensity.resize ((size_t) bar, 0.5f); }
            auto& l = barLoudness[(size_t) bar - 1];
            auto& d = barDensity[(size_t) bar - 1];
            if (l <= -99.0f) { ++rehearsalBars; l = loudnessDb; d = density; }
            else { l = 0.7f * l + 0.3f * loudnessDb; d = 0.7f * d + 0.3f * density; }
        }
        /** Samples the live tracks now (call a few times a second while playing). */
        void sampleLive (int bar)
        {
            int tracks = 0, busy = 0;
            double power = 0.0;
            suite::Autopilot::forEachLive (0u, [&] (int, const float* bands)
            {
                ++tracks;
                float top = -100.0f;
                for (int b = 0; b < suite::Autopilot::liveBands; ++b) { top = juce::jmax (top, bands[b]); power += std::pow (10.0, bands[b] / 10.0); }
                if (top > -40.0f) ++busy;
            });
            if (tracks == 0) return;
            hearBar (bar, (float) (10.0 * std::log10 (power + 1.0e-12)), (float) busy / (float) juce::jmax (6, tracks));
        }
        void forgetRehearsal() { barLoudness.clear(); barDensity.clear(); rehearsalBars = 0; }

        /** A reference's transition style, from its bars (Listen) and its proposed map. */
        static dr::ReferenceStyle referenceStyle (const std::vector<listen::BarFeature>& bars, const suite::SectionMap& map, double bpm, const juce::String& name)
        {
            dr::ReferenceStyle r;
            if (bars.size() < 16 || map.count < 2) return r;
            int boundaries = 0, gaps = 0, big = 0, phraseMoves = 0;
            auto db = [&] (int b) { return b >= 0 && b < (int) bars.size() && bars[(size_t) b].heard ? bars[(size_t) b].fullDb : -100.0f; };
            auto hi = [&] (int b) { return b >= 0 && b < (int) bars.size() && bars[(size_t) b].heard ? bars[(size_t) b].highDb : -100.0f; };
            for (int i = 1; i < map.count; ++i)
            {
                const auto b = map.sections[(size_t) i].startBar - 1;
                ++boundaries;
                if (db (b - 1) < 0.5f * (db (b - 2) + db (b)) - 3.0f) ++gaps;                 // a dip in the bar before: silence
                if (std::abs (hi (b) - hi (b - 1)) > 6.0f || db (b) - db (b - 1) > 6.0f) ++big; // a big change in the top end
            }
            for (int b = 8; b + 1 < (int) bars.size(); b += 8)
                if (std::abs (hi (b - 1) - 0.5f * (hi (b - 2) + hi (b))) > 4.0f) ++phraseMoves;
            const auto minutes = bars.size() * 4.0 / juce::jmax (60.0, bpm);
            r.valid = true;
            r.movesPerMinute = (float) ((boundaries + phraseMoves) / juce::jmax (0.5, minutes));
            r.silenceShare = (float) gaps / (float) juce::jmax (1, boundaries);
            r.bigShare = (float) big / (float) juce::jmax (1, boundaries);
            r.name = name;
            return r;
        }

        //==============================================================================
        // Trust: fidelity, the render list, the ticker
        /** After a play-through: every planned part that happened, what didn't, and anything extra. */
        juce::String fidelityReport (double barQ) const
        {
            if (! havePlan) return "No plan yet.";
            const auto present = presentOverride != 0 ? presentOverride : presentPlayers();
            int planned = 0, done = 0;
            juce::StringArray missed;
            for (size_t i = 0; i < plan.moments.size() && i < (size_t) suite::DirectorBus::maxMoments; ++i)
            {
                const auto& m = plan.moments[i];
                const auto acted = suite::DirectorBus::acted ((int) i);
                for (int p = 0; p < dr::pScene; ++p)
                {
                    if (! m.acts (p) || (present & (1u << p)) == 0) continue;
                    ++planned;
                    if (acted & (1u << p)) ++done;
                    else missed.add ("bar " + juce::String ((int) std::floor (m.atQ / barQ + 1.0e-6) + 1) + " " + dr::playerName (p) + " (" + dr::gesture (m.gesture[(size_t) p]).name + ")");
                }
            }
            juce::String out = juce::String (done) + " of " + juce::String (planned) + " planned parts happened.";
            if (! missed.isEmpty()) out << "\nNot heard yet: " << missed.joinIntoString (", ") << ".";
            juce::StringArray extra;
            for (int p = 0; p < dr::numPlayers; ++p) if (const auto n = suite::DirectorBus::extras (p); n > 0) extra.add (juce::String (dr::playerName (p)) + " " + juce::String ((int) n));
            out << (extra.isEmpty() ? juce::String ("\nNothing extra played.") : "\nExtra, outside the plan: " + extra.joinIntoString (", ") + ".");
            return out;
        }
        /** The dry-run render: just the transitions, as bar ranges to render (2 bars either side). */
        juce::StringArray renderList (double barQ, double bpm) const
        {
            juce::StringArray out;
            for (const auto& m : plan.moments)
            {
                if (m.hero == dr::pNone && m.silence == dr::gNone) continue;
                const auto from = juce::jmax (1, (int) std::floor (m.startQ / barQ) + 1 - 2), to = (int) std::ceil (m.endQ / barQ) + 2;
                auto clock = [&] (int bar) { const auto s = (bar - 1) * barQ * 60.0 / juce::jmax (1.0, bpm); return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2); };
                out.add ("bars " + juce::String (from) + "-" + juce::String (to) + "  (" + clock (from) + "-" + clock (to + 1) + ")  " + dr::kindName (m.kind) + ": "
                         + (m.hero != dr::pNone ? juce::String (dr::playerName (m.hero)) + " " + dr::gesture (m.heroGesture).name : juce::String ("silence")));
            }
            return out;
        }
        /** The markers for that render (one MIDI marker per range: drag into FL, render each). */
        juce::MidiFile renderMarkers (double barQ, double bpm) const
        {
            juce::MidiFile file;
            file.setTicksPerQuarterNote (960);
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (60.0, 200.0, bpm))), 0.0);
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Director transitions"), 0.0);
            for (const auto& m : plan.moments)
            {
                if (m.hero == dr::pNone && m.silence == dr::gNone) continue;
                const auto from = juce::jmax (0.0, (std::floor (m.startQ / barQ) - 2.0) * barQ), to = (std::ceil (m.endQ / barQ) + 2.0) * barQ;
                seq.addEvent (juce::MidiMessage::textMetaEvent (6, "Render from: " + juce::String (dr::kindName (m.kind)) + " bar " + juce::String ((int) std::floor (m.atQ / barQ) + 1)), from * 960.0);
                seq.addEvent (juce::MidiMessage::textMetaEvent (6, "Render to"), to * 960.0);
            }
            seq.sort();
            file.addTrack (seq);
            return file;
        }
        /** The "what's next" ticker. */
        juce::String ticker (double ppq, double barQ) const
        {
            if (! havePlan) return settings.enabled ? "Director: waiting for a section map" : "Director off: every plugin plays its own transitions";
            const auto& p = abPrevious && ! previous.moments.empty() ? previous : plan;
            for (const auto& m : p.moments)
            {
                if (m.atQ < ppq - 1.0e-6 || (m.hero == dr::pNone && m.silence == dr::gNone)) continue;
                const auto beats = m.atQ - ppq;
                juce::String s = juce::String (abPrevious ? "B (previous plan)  " : "") + "Next: bar " + juce::String ((int) std::floor (m.atQ / barQ + 1.0e-6) + 1)
                                 + " (in " + juce::String ((int) std::ceil (beats)) + " beats), " + dr::kindName (m.kind) + ": ";
                if (m.hero != dr::pNone) s << dr::playerName (m.hero) << " " << juce::String (dr::gesture (m.heroGesture).name).toLowerCase();
                if (m.silence != dr::gNone) s << (m.hero != dr::pNone ? ", " : "") << juce::String (dr::gesture (m.silence).name).toLowerCase();
                const auto sup = m.supporters() - (m.silence != dr::gNone ? 1 : 0) - (m.acts (dr::pScene) && m.hero != dr::pScene ? 1 : 0);
                if (sup > 0) s << " (+" << sup << " supporting)";
                return s;
            }
            return "No more moves in this song";
        }

    private:
        struct MomentState
        {
            bool hadLock = false, hadFrozen = false, hadHero = false;
            std::pair<uint8_t, uint8_t> lock {};
            dr::Moment frozen;
            int reroll = 0, heroRole = 0;
        };
        std::map<uint32_t, std::vector<MomentState>> undo;

        template <typename T> static void grow (std::vector<T>& v, int index, T fill) { if (index >= 0 && (int) v.size() <= index) v.resize ((size_t) index + 1, fill); }
        juce::String barText (uint32_t key) const
        {
            const auto i = plan.indexOf (key);
            return i >= 0 ? juce::String ((int) std::floor (plan.moments[(size_t) i].atQ / 4.0f + 1.0e-6f) + 1) : juce::String ("?");
        }
    };
}
