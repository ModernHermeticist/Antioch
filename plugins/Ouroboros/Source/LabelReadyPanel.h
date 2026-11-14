#pragma once

// The label-ready dashboard: the overall score and the gate, the verdict and the next action,
// every area and check (click one for what it found, where, what moving it would take, and its
// fix), "Fix all safe ones", the label profile and its reference catalogue, the score's history,
// and the milestones (save, restore, compare) with the hand-off pack.

#include "PluginProcessor.h"
#include "SuiteUI.h"

namespace lab
{
    namespace Colours = suite::ui::Colours;

    class LabelReadyPanel : public juce::Component, private juce::Timer, private juce::ListBoxModel
    {
    public:
        explicit LabelReadyPanel (OuroborosProcessor& p) : processor (p)
        {
            list.setModel (this);
            list.setRowHeight (22);
            list.setColour (juce::ListBox::backgroundColourId, Colours::background);
            addAndMakeVisible (list);
            detail.setMultiLine (true); detail.setReadOnly (true); detail.setFont (juce::FontOptions (12.5f));
            addAndMakeVisible (detail);
            fixB.onClick = [this] { if (const auto* c = selected()) { const auto copy = *c; if (processor.applyLabelCheck (copy)) note ("Done: " + copy.name + " (the Score log shows what it did)"); } };
            safeB.onClick = [this] { processor.labelReady.logAction ("Fix all safe ones"); note (juce::String (processor.labelReady.fixAllSafe()) + " safe fix(es) sent; the scores update as the song plays."); };
            for (auto* b : { &fixB, &safeB, &toolsB, &profileB, &refsB, &milestoneB, &handoffB, &saveB }) addAndMakeVisible (b);
            toolsB.onClick = [this] { toolsMenu(); };
            listenLabel.setColour (juce::Label::textColourId, Colours::brass);
            addAndMakeVisible (listenLabel);
            profileB.onClick = [this] { profileMenu(); };
            refsB.onClick = [this] { refsMenu(); };
            milestoneB.onClick = [this] { milestoneMenu(); };
            handoffB.onClick = [this] { handoff(); };
            saveB.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Save the label-ready report", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Label-ready report.txt"), "*.txt");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult() != juce::File()) { fc.getResult().replaceWithText (processor.labelReady.reportText (processor.getMap())); fc.getResult().revealToUser(); }
                });
            };
            status.setColour (juce::Label::textColourId, Colours::verdigris);
            addAndMakeVisible (status);
            setSize (1180, 760);
            startTimerHz (2);
            refresh();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Colours::panel);
            const auto& r = processor.labelReady.report;
            auto area = getLocalBounds().reduced (14);
            auto head = area.removeFromTop (70);
            g.setColour (r.ready ? Colours::moss : Colours::ember);
            g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
            g.drawText (processor.labelReady.freshEars ? juce::String ("--") : juce::String (r.overall), head.removeFromLeft (90), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.setColour (Colours::text);
            auto right = head;
            g.drawText (r.ready ? "READY TO SEND" : "Not ready yet (" + juce::String (processor.labelReady.profile.readyThreshold) + " needed, nothing blocking)",
                        right.removeFromTop (22), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.0f));
            g.setColour (Colours::dimText);
            {
                const auto spread = processor.labelReady.scoreSpread();
                const auto extra = "heard " + juce::String (juce::roundToInt (r.coverage * 100.0f)) + "% of the song" + (spread >= 0.0f ? ", +-" + juce::String (spread * 0.5f, 1) + " between play-throughs" : juce::String());
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (extra, right.withTrimmedLeft (right.getWidth() - 300).removeFromTop (16), juce::Justification::centredRight);
                g.setFont (juce::FontOptions (13.0f));
            }
            g.drawText ("Profile: " + processor.labelReady.profile.name + (processor.labelReady.profileFromCatalogue ? " (" + juce::String ((int) processor.labelReady.profile.refs.size()) + " references)" : juce::String (" (generic norms: add references)")),
                        right.removeFromTop (18), juce::Justification::centredLeft);
            g.setColour (Colours::text);
            g.drawText (r.verdict, right.removeFromTop (18), juce::Justification::centredLeft, true);
            g.setColour (Colours::brass);
            g.drawText ("Next: " + r.nextAction, right.removeFromTop (16), juce::Justification::centredLeft, true);
            // the areas as bars
            auto areas = area.removeFromTop (44);
            const auto w = areas.getWidth() / numAreas;
            for (int a = 0; a < numAreas; ++a)
            {
                auto cell = areas.removeFromLeft (w).reduced (3, 2);
                const auto s = r.areaScore[(size_t) a];
                g.setColour (Colours::outline);
                g.fillRoundedRectangle (cell.removeFromBottom (8).toFloat(), 3.0f);
                g.setColour (! r.areaMeasured[(size_t) a] ? Colours::outline : s >= 0.8f ? Colours::moss : s >= 0.5f ? Colours::brass : Colours::ember);
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) cell.getX(), (float) cell.getBottom(), (float) cell.getWidth() * (r.areaMeasured[(size_t) a] ? s : 0.0f), 8.0f), 3.0f);
                g.setColour (Colours::text);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (juce::String (areaName (a)) + (processor.labelReady.signOff.count (a) ? " (signed)" : "")
                                + (r.areaMeasured[(size_t) a] && ! processor.labelReady.freshEars ? "  " + juce::String (juce::roundToInt (s * 100.0f)) : juce::String ("  -")), cell, juce::Justification::centredLeft, true);
            }
            // the heat map: each section's score
            {
                const auto map = processor.getMap();
                const auto scores = processor.labelReady.sectionScores (map);
                auto strip = juce::Rectangle<int> (area.getX(), area.getY() + 2, area.getWidth(), 6);
                if (map.totalBars > 0)
                    for (int i = 0; i < map.count; ++i)
                    {
                        const auto x0 = strip.getX() + strip.getWidth() * (map.sections[(size_t) i].startBar - 1) / map.totalBars;
                        const auto x1 = strip.getX() + strip.getWidth() * (map.endBar (i) - 1) / map.totalBars;
                        const auto s = scores[(size_t) i];
                        g.setColour (s < 0.0f ? Colours::outline : s >= 0.8f ? Colours::moss : s >= 0.5f ? Colours::brass : Colours::ember);
                        g.fillRect (x0, strip.getY(), juce::jmax (1, x1 - x0 - 1), strip.getHeight());
                    }
            }
            // the history graph
            const auto& h = processor.labelReady.history;
            if (h.size() >= 2)
            {
                g.setColour (Colours::verdigris);
                juce::Path p;
                for (size_t i = 0; i < h.size(); ++i)
                {
                    const auto x = (float) historyArea.getX() + (float) historyArea.getWidth() * (float) i / (float) (h.size() - 1);
                    const auto y = (float) historyArea.getBottom() - (float) historyArea.getHeight() * (float) h[i].second / 100.0f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.strokePath (p, juce::PathStrokeType (1.5f));
            }
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Score over time", historyArea.withHeight (14), juce::Justification::topLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (14);
            r.removeFromTop (70 + 44 + 8);
            listenLabel.setBounds (r.removeFromTop (20));
            r.removeFromTop (4);
            auto bottom = r.removeFromBottom (30);
            for (auto* b : { &fixB, &safeB, &toolsB, &profileB, &refsB, &milestoneB, &handoffB, &saveB }) { b->setBounds (bottom.removeFromLeft (132).reduced (2, 0)); }
            status.setBounds (bottom);
            r.removeFromBottom (6);
            auto rightCol = r.removeFromRight (440);
            historyArea = rightCol.removeFromBottom (90);
            rightCol.removeFromBottom (6);
            detail.setBounds (rightCol);
            r.removeFromRight (8);
            list.setBounds (r);
        }

    private:
        OuroborosProcessor& processor;
        juce::ListBox list;
        juce::TextEditor detail;
        juce::TextButton fixB { "Fix this" }, safeB { "Fix all safe ones" }, toolsB { "Tools..." }, profileB { "Label profile..." }, refsB { "References..." },
                         milestoneB { "Milestones..." }, handoffB { "Hand-off pack..." }, saveB { "Save the report..." };
        juce::Label status, listenLabel;
        juce::Rectangle<int> historyArea;
        std::vector<int> rows;   // check indices, by area then score
        int selectedRow = -1;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::AlertWindow> ask;

        const Check* selected() const
        {
            const auto& c = processor.labelReady.report.checks;
            return selectedRow >= 0 && selectedRow < (int) rows.size() && rows[(size_t) selectedRow] < (int) c.size() ? &c[(size_t) rows[(size_t) selectedRow]] : nullptr;
        }
        void note (const juce::String& t) { status.setText (t, juce::dontSendNotification); }
        void refresh()
        {
            const auto& c = processor.labelReady.report.checks;
            rows.clear();
            for (int a = 0; a < numAreas; ++a)
            {
                std::vector<int> idx;
                for (int i = 0; i < (int) c.size(); ++i) if (c[(size_t) i].area == a) idx.push_back (i);
                std::stable_sort (idx.begin(), idx.end(), [&c] (int x, int y) { return (c[(size_t) x].measured ? c[(size_t) x].score : 2.0f) < (c[(size_t) y].measured ? c[(size_t) y].score : 2.0f); });
                rows.push_back (-1 - a);   // an area heading
                for (auto i : idx) rows.push_back (i);
            }
            list.updateContent();
            list.repaint();
            showSelected();
            // the listen-through: what to listen for where the playhead is
            {
                auto& lr = processor.labelReady;
                const auto bar = (int) std::floor (processor.ppq.load() / juce::jmax (1.0, processor.barQuarters.load())) + 1;
                juce::String line = processor.playing.load() ? "Listen: " + lr.listenNow (bar) : juce::String ("Listen-through: play the song; flagged bars come up here as you reach them (" + juce::String ((int) lr.listenQueue().size()) + " flagged)");
                if (lr.watching && lr.watchBaseline >= 0) line = "WATCHING: " + juce::String (lr.report.overall - lr.watchBaseline >= 0 ? "+" : "") + juce::String (lr.report.overall - lr.watchBaseline) + " since you started.  " + line;
                if (lr.abOn) line = "A/B: " + (lr.abShowing >= 0 ? lr.milestones[(size_t) lr.abShowing].name : juce::String ("switching")) + " (level-matched " + juce::String (lr.abGainDb.load(), 1) + " dB).  " + line;
                if (lr.translationLoop) line << "  |  Translation loop: " << lr.loopNote;
                if (lr.autoFixOn && lr.autoFix.note.isNotEmpty() && lr.autoFix.note != "nothing to do") line << "  |  Autopilot: " << lr.autoFix.note;
                if (processor.monitor.playingReference.load()) line = "HEARING THE REFERENCE: " + processor.monitor.referenceName() + ".  " + line;
                if (processor.monitor.preview.load() != lab::Monitor::none) line = juce::String ("PREVIEW (") + (processor.monitor.preview.load() == lab::Monitor::mono ? "mono" : processor.monitor.preview.load() == lab::Monitor::phone ? "phone" : "car") + ").  " + line;
                if (lr.regressionNote.isNotEmpty()) line << "  |  " << lr.regressionNote;
                if (lr.preRenderNote.isNotEmpty() && processor.monitor.rendering.load()) line = lr.preRenderNote;
                if (lr.freshEars && lr.minutesListening() > 0 && lr.minutesListening() % 45 == 0) line = "FRESH EARS: " + juce::String (lr.minutesListening()) + " minutes in. Rest your ears for ten minutes, then listen again.  " + line;
                if (lr.abOn && lr.abBlind) line = line.replace (lr.abShowing >= 0 ? lr.milestones[(size_t) lr.abShowing].name : juce::String ("~~"), lr.abShowing == lr.abA ? "A" : "B");
                listenLabel.setText (line, juce::dontSendNotification);
            }
            if (processor.labelReady.analysing.isNotEmpty()) note ("Measuring \"" + processor.labelReady.analysing + "\"...");
            else if (processor.labelReady.lastAdded.isNotEmpty()) { note (processor.labelReady.lastAdded); processor.labelReady.lastAdded = {}; }
            repaint();
        }
        void showSelected()
        {
            const auto* c = selected();
            fixB.setEnabled (c != nullptr && (c->fix != suite::Fix::None || c->action.startsWith ("director:") || c->action.startsWith ("autopilot:") || c->action.startsWith ("lift:")));
            if (c == nullptr) { detail.setText ("Click a check to see what it found, where, and what would move it.\n\nBlocking:\n" + processor.labelReady.report.blockers.joinIntoString ("\n")); return; }
            juce::StringArray t;
            t.add (c->name + (c->blocking ? "  (blocks the gate when low)" : ""));
            t.add (c->measured ? (processor.labelReady.freshEars ? juce::String ("Score hidden (fresh ears)") : "Score: " + juce::String (juce::roundToInt (c->score * 100.0f)) + "%"
                                      + (c->score < 0.99f ? "  (fixing it fully: about +" + juce::String (processor.labelReady.predictedGain (*c), 1) + " points overall)" : juce::String()))
                               : juce::String ("Not measured yet"));
            t.add ("");
            t.add ("Found: " + c->detail);
            if (c->bar > 0) t.add ("Where: bar " + juce::String (c->bar) + " (FL's playhead can't be moved by a plugin: go there in the playlist)");
            if (c->explain.isNotEmpty()) t.add ("To move it: " + c->explain);
            if (c->fix != suite::Fix::None) t.add (juce::String ("One click: ") + (c->safeFix ? "a safe fix (in Fix all safe ones)" : "a fix (undoable in Mentalism's History)"));
            else if (c->action == "write stronger") t.add ("In Correspondence: Song page > Write it stronger (three stronger hooks, scored).");
            else if (c->action.startsWith ("director:")) t.add ("One click: tells the director \"" + c->action.fromFirstOccurrenceOf (":", false, false).trim() + "\"");
            else if (c->action.startsWith ("autopilot:")) t.add (juce::String ("One click: turns on the autopilot's \"") + suite::Autopilot::featureName (c->action.fromFirstOccurrenceOf (":", false, false).getIntValue()) + "\"");
            else if (c->action.startsWith ("lift:")) t.add ("One click: the final-drop key change becomes +" + c->action.fromFirstOccurrenceOf (":", false, false) + " semitones");
            if (const auto it = processor.labelReady.profile.minScore.find (c->name); it != processor.labelReady.profile.minScore.end())
                t.add ("This label insists: at least " + juce::String (juce::roundToInt (it->second * 100.0f)) + "% (Tools > This check is a must)");
            detail.setText (t.joinIntoString ("\n"));
        }
        int getNumRows() override { return (int) rows.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
        {
            if (row < 0 || row >= (int) rows.size()) return;
            const auto i = rows[(size_t) row];
            if (i < 0)
            {
                g.setColour (Colours::text);
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawText (areaName (-1 - i), 6, 0, w - 12, h, juce::Justification::centredLeft);
                return;
            }
            const auto& c = processor.labelReady.report.checks[(size_t) i];
            if (sel) { g.setColour (Colours::outline); g.fillRect (0, 0, w, h); }
            const auto col = ! c.measured ? Colours::outline : c.score >= 0.8f ? Colours::moss : c.score >= 0.5f ? Colours::brass : Colours::ember;
            g.setColour (col);
            g.fillEllipse (14.0f, (float) h * 0.5f - 4.0f, 8.0f, 8.0f);
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (c.name, 30, 0, 230, h, juce::Justification::centredLeft, true);
            g.setColour (Colours::dimText);
            g.drawText (c.measured ? c.detail : juce::String ("not measured yet"), 266, 0, w - 270, h, juce::Justification::centredLeft, true);
        }
        void selectedRowsChanged (int row) override { selectedRow = row; showSelected(); }
        void timerCallback() override { refresh(); }

        void profileMenu()
        {
            juce::PopupMenu m, use;
            const auto names = Profile::list();
            for (int i = 0; i < names.size(); ++i) use.addItem (100 + i, names[i], true, names[i] == processor.labelReady.profile.name);
            m.addSubMenu ("Use a label profile", use, ! names.isEmpty());
            m.addItem (1, "New label profile...");
            juce::PopupMenu thr;
            for (int v : { 70, 75, 80, 85, 90 }) thr.addItem (200 + v, juce::String (v) + "/100", true, processor.labelReady.profile.readyThreshold == v);
            m.addSubMenu ("Ready means at least", thr);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&profileB), [this, names] (int r)
            {
                auto& lr = processor.labelReady;
                if (r >= 200) { const juce::ScopedLock sl (lr.lock); lr.profile.readyThreshold = r - 200; lr.profile.save(); }
                else if (r >= 100) { const juce::ScopedLock sl (lr.lock); lr.useProfile (names[r - 100]); }
                else if (r == 1) askText ("New label profile", "The label (and sub-style), e.g. \"Armind uplifting\"", [this] (juce::String n)
                {
                    const juce::ScopedLock sl (processor.labelReady.lock);
                    auto p = genericProfile(); p.name = n; p.refs.clear(); p.save();
                    processor.labelReady.useProfile (n);
                });
                refresh();
            });
        }
        void refsMenu()
        {
            juce::PopupMenu m, remove;
            m.addItem (1, "Add a reference track (the label's release)...");
            m.addItem (2, "Add one of my tracks the label signed...");
            const auto& refs = processor.labelReady.profile.refs;
            for (int i = 0; i < (int) refs.size(); ++i) remove.addItem (100 + i, refs[(size_t) i].name + (refs[(size_t) i].yours ? " (yours)" : ""));
            m.addSubMenu ("Remove a reference", remove, ! refs.empty());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&refsB), [this] (int r)
            {
                if (r >= 100) { processor.labelReady.removeReference (r - 100); refresh(); return; }
                if (r != 1 && r != 2) return;
                const auto yours = r == 2;
                chooser = std::make_unique<juce::FileChooser> ("A reference track", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, yours] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    processor.labelReady.addReference (f, processor.bpm.load(), yours);   // measured at the project's tempo
                    note ("Measuring \"" + f.getFileNameWithoutExtension() + "\" (a few seconds)...");
                });
            });
        }
        void milestoneMenu()
        {
            juce::PopupMenu m, restore, compare;
            const auto& ms = processor.labelReady.milestones;
            for (int i = 0; i < (int) ms.size(); ++i)
            {
                restore.addItem (100 + i, ms[(size_t) i].name + "  (" + juce::String (ms[(size_t) i].overall) + ")");
                if (i > 0) compare.addItem (300 + i, ms[(size_t) i - 1].name + " -> " + ms[(size_t) i].name);
            }
            m.addItem (1, "Save a milestone (the whole suite's state)...");
            m.addSubMenu ("Restore a milestone", restore, ! ms.empty());
            m.addSubMenu ("Compare with the one before", compare, ms.size() >= 2);
            juce::PopupMenu ab;
            for (int i = 0; i < (int) ms.size(); ++i)
                for (int j = i + 1; j < (int) ms.size(); ++j)
                    if (i * 50 + j < 2000) ab.addItem (1000 + i * 50 + j, ms[(size_t) i].name + "  vs  " + ms[(size_t) j].name);
            m.addSubMenu ("A/B two milestones (level-matched, switching on the bar)", ab, ms.size() >= 2 && ! processor.labelReady.abOn);
            m.addItem (4, "Blind A/B (the names hidden: A and B, in a random order)", ! processor.labelReady.abOn, processor.labelReady.abBlind);
            m.addItem (2, "Switch the A/B at the next bar", processor.labelReady.abOn);
            m.addItem (3, "Stop the A/B", processor.labelReady.abOn);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&milestoneB), [this] (int r)
            {
                auto& lr = processor.labelReady;
                if (r >= 1000 && lr.abBlind && juce::Random::getSystemRandom().nextBool()) { lr.abStart ((r - 1000) % 50, (r - 1000) / 50); note ("Blind A/B: listen, then Stop the A/B to see which was which."); }
                else if (r >= 1000) { lr.abStart ((r - 1000) / 50, (r - 1000) % 50); note ("A/B: the first milestone loads at the next bar; Switch flips at the bar after. A monitor level match only: stop the A/B before you render."); }
                else if (r == 2) lr.abToggle();
                else if (r == 4) lr.abBlind = ! lr.abBlind;
                else if (r == 3)
                {
                    const auto last = lr.abShowing >= 0 ? lr.milestones[(size_t) lr.abShowing].name : juce::String();
                    lr.abStop();
                    note (lr.abBlind ? "A was \"" + lr.milestones[(size_t) juce::jmax (0, lr.abA)].name + "\", B was \"" + lr.milestones[(size_t) juce::jmax (0, lr.abB)].name + "\" (\"" + last + "\" stays loaded)."
                                     : juce::String ("A/B stopped (the last one loaded stays)."));
                }
                else if (r == 1) askText ("Save a milestone", "What it is, e.g. \"sent to vocalist\", \"A&R round 1\"", [this] (juce::String n) { processor.labelReady.saveMilestone (n); note ("Milestone saved: " + n); });
                else if (r >= 300) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "What changed between the milestones", lr.diffMilestones (r - 301, r - 300).joinIntoString ("\n"));
                else if (r >= 100) { lr.restoreMilestone (r - 100); note ("Restoring \"" + lr.milestones[(size_t) r - 100].name + "\" across the suite"); }
            });
        }
        void toolsMenu()
        {
            auto& lr = processor.labelReady;
            juce::PopupMenu m, mix, writing, iterate, focus;
            mix.addItem (10, "Match the label: one step (the furthest-out check with a fix)");
            mix.addItem (11, "Steer every Quintessence toward the label's tone", lr.profileFromCatalogue, lr.steerToLabel);
            mix.addItem (12, "Translation loop: try a fix, keep it only if it helped", true, lr.translationLoop);
            m.addSubMenu ("The mix toward the label", mix);
            writing.addItem (20, "Drop-impact designer (3 ways, ranked)...");
            writing.addItem (21, "Ear candy where nothing new happens (a MIDI clip)...");
            writing.addItem (22, "Write in Correspondence: Writing > Write for the label (breakdown, drop 2, counter, bass, teaser)", false);
            m.addSubMenu ("Writing", writing);
            const auto map = processor.getMap();
            focus.addItem (300, "The whole song", true, lr.focusSection < 0);
            for (int i = 0; i < map.count; ++i)
                focus.addItem (301 + i, juce::String (suite::sectionName (map.sections[(size_t) i].type)) + " at bar " + juce::String (map.sections[(size_t) i].startBar), true, lr.focusSection == i);
            iterate.addSubMenu ("Score one section only", focus, map.count > 0);
            iterate.addItem (30, "Watch mode: score faster while you loop, against where you started", true, lr.watching);
            iterate.addItem (31, "Score log: what each change did...");
            iterate.addItem (32, "Score the label's own tracks...", ! lr.profile.refs.empty());
            iterate.addItem (33, "... and set the ready threshold from them", ! lr.profile.refs.empty());
            if (const auto* c = selected())
            {
                const auto must = lr.profile.minScore.count (c->name) > 0;
                iterate.addItem (34, must ? "\"" + c->name + "\" is no longer a must" : "\"" + c->name + "\" is a must for this label (at least 80%)");
            }
            m.addSubMenu ("Iterate", iterate);
            juce::PopupMenu decide;
            decide.addItem (50, "Today's plan: the three changes worth the most...");
            decide.addItem (51, "Why not 100: the points missing, area by area...");
            decide.addItem (52, "A&R notes (one page, plain words)...");
            decide.addItem (53, "The song against each reference...", ! lr.profile.refs.empty());
            decide.addItem (54, "Against your best signed track...");
            m.addSubMenu ("Decide what's next", decide);
            juce::PopupMenu ears;
            ears.addItem (60, "Fresh ears: hide the scores, remind me to rest", true, lr.freshEars);
            if (const auto* c = selected())
                ears.addItem (61, juce::String (lr.signOff.count (c->area) ? "Take back the sign-off of " : "Sign off ") + areaName (c->area) + " (I've listened, it's done)");
            ears.addItem (62, "The gate waits for every area's sign-off", true, lr.requireSignOff);
            ears.addItem (63, "Save the before/after snippets (" + juce::String ((int) processor.snippets.pairs().size()) + ")...", ! processor.snippets.pairs().empty());
            m.addSubMenu ("Listening", ears);
            juce::PopupMenu hear;
            hear.addItem (70, "Hear the reference (same section, level-matched)...", ! lr.profile.refs.empty(), processor.monitor.referenceOn.load());
            hear.addItem (71, "Preview: mono", true, processor.monitor.preview.load() == lab::Monitor::mono);
            hear.addItem (72, "Preview: a phone", true, processor.monitor.preview.load() == lab::Monitor::phone);
            hear.addItem (73, "Preview: a car", true, processor.monitor.preview.load() == lab::Monitor::car);
            hear.addItem (74, "Preview off", processor.monitor.preview.load() != lab::Monitor::none);
            m.addSubMenu ("Hear (listen-only: never in a render)", hear);
            juce::PopupMenu more;
            more.addItem (80, "Front to back, and who ducks...");
            more.addItem (81, "Propose the label's structure (a map like its closest release)...", lr.profileFromCatalogue);
            more.addItem (82, "Is it done? (the stage of the last milestone)...", ! lr.milestones.empty());
            more.addItem (83, "Start new projects like my last song", true, lab::LabelReadyHost::startLikeLast());
            more.addItem (84, "What the autopilot has learned...");
            m.addSubMenu ("More", more);
            m.addItem (64, "The label autopilot (" + juce::String (lr.autoFix.kept) + " kept, " + juce::String (lr.autoFix.undone) + " undone; each fix has its switch on the Autopilot page)", true, lr.autoFixOn);
            m.addItem (40, "Blind spots: what couldn't be measured, and why...");
            m.addItem (41, "The listen-through queue (every flagged bar, in order)...");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsB), [this] (int r)
            {
                auto& host = processor.labelReady;
                if (r == 10) note (host.matchLabelStep());
                else if (r == 11) { host.steerToLabel = ! host.steerToLabel; if (! host.steerToLabel) suite::ReferenceBus::withdraw(); note (host.steerToLabel ? "Every Quintessence now steers toward the label's tone (Mentalism's loaded reference, if any, takes over)." : "Steering toward the label off."); }
                else if (r == 12) { host.translationLoop = ! host.translationLoop; host.tried.clear(); note (host.translationLoop ? "Translation loop on: one fix at a time, about half a minute each, undone if it didn't help." : "Translation loop off."); }
                else if (r == 20) impactMenu();
                else if (r == 21)
                {
                    chooser = std::make_unique<juce::FileChooser> ("Where to put the ear candy clip", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                    {
                        if (! fc.getResult().isDirectory()) return;
                        const auto f = processor.labelReady.writeEarCandy (fc.getResult(), processor.getMap(), processor.barQuarters.load());
                        note (juce::String (processor.labelReady.earCandySpots) + " spot(s) where nothing new happens for 4 bars: one-shots in key there. Drag " + f.getFileName() + " onto a candy track.");
                        f.revealToUser();
                    });
                }
                else if (r >= 300 && r < 400) { host.focusSection = r - 301; note (host.focusSection < 0 ? juce::String ("Scoring the whole song.") : "Scoring one section only: the checks that point inside it."); }
                else if (r == 30) { host.setWatching (! host.watching); note (host.watching ? "Watch mode: scoring every half second; the line above shows the change since now." : "Watch mode off."); }
                else if (r == 31) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Score log", host.scoreLogText());
                else if (r == 32 || r == 33) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "The label's tracks, scored", host.scoreReferences (r == 33));
                else if (r == 34) if (const auto* c = selected()) { const auto must = host.profile.minScore.count (c->name) > 0; host.setMinimum (c->name, must ? 0.0f : 0.8f); }
                if (r == 50) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Today's plan", host.sessionPlan());
                else if (r == 51) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Why not 100", host.whyNot100());
                else if (r == 52) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "A&R notes", host.arSummary());
                else if (r == 53) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Against each reference", host.scoreByReference());
                else if (r == 54) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Against your best signed track", host.overlayBestSigned());
                else if (r == 60) { host.freshEars = ! host.freshEars; host.sessionStartMs = juce::Time::currentTimeMillis(); }
                else if (r == 61) { if (const auto* c = selected()) host.signOffArea (c->area, host.signOff.count (c->area) == 0); }
                else if (r == 62) host.requireSignOff = ! host.requireSignOff;
                else if (r == 63)
                {
                    chooser = std::make_unique<juce::FileChooser> ("Where to put the before/after snippets", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                    {
                        if (! fc.getResult().isDirectory()) return;
                        const auto dir = fc.getResult().getChildFile ("Before and after");
                        note (juce::String (processor.snippets.save (dir)) + " pair(s) saved");
                        dir.revealToUser();
                    });
                }
                else if (r == 64) { host.autoFixOn = ! host.autoFixOn; if (! host.autoFixOn) host.autoFix.reset(); }
                else if (r == 70)
                {
                    if (processor.monitor.referenceOn.load()) { processor.monitor.referenceOn.store (false); note ("Back to the song."); }
                    else
                    {
                        juce::PopupMenu pick;
                        for (int i = 0; i < (int) host.profile.refs.size(); ++i) pick.addItem (1 + i, host.profile.refs[(size_t) i].name);
                        pick.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsB), [this] (int k)
                        {
                            auto& h = processor.labelReady;
                            if (k <= 0 || k > (int) h.profile.refs.size()) return;
                            if (processor.monitor.loadReference (h.profile.refs[(size_t) k - 1])) { processor.monitor.referenceOn.store (true); note ("Hearing \"" + processor.monitor.referenceName() + "\" at the same place in the same section, level-matched. Tools > Hear again to come back."); }
                            else note ("Couldn't open that reference's file (moved?).");
                        });
                    }
                }
                else if (r >= 71 && r <= 74) processor.monitor.preview.store (r == 74 ? lab::Monitor::none : r - 70);
                else if (r == 80) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Front to back", host.depthMap());
                else if (r == 81)
                {
                    const auto proposal = host.proposeMap();
                    if (proposal.count == 0) { note ("No reference to take a structure from."); return; }
                    juce::StringArray l;
                    for (int i = 0; i < proposal.count; ++i) l.add (juce::String (suite::sectionName (proposal.sections[(size_t) i].type)) + " at bar " + juce::String (proposal.sections[(size_t) i].startBar));
                    auto* aw = new juce::AlertWindow ("The label's structure", "The closest release is laid out like this:\n" + l.joinIntoString (", ") + "\n\nUse it as the map? (FL's playlist doesn't change: arrange your clips to it.)", juce::MessageBoxIconType::NoIcon);
                    aw->addButton ("Use it", 1); aw->addButton ("Cancel", 0);
                    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, proposal] (int k) { if (k == 1) { processor.setMap (proposal); note ("The map now follows the label's structure."); } }), true);
                }
                else if (r == 82)
                {
                    const auto& last = host.milestones.back();
                    const auto stage = last.stage >= 0 ? last.stage : 3;
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Is it done?",
                        juce::String ("As a ") + lab::LabelReadyHost::stageName (stage) + " (name a milestone \"sketch\", \"arrangement\", \"mix\" or \"premaster\" to set its stage): " + host.stageDone (stage));
                }
                else if (r == 83) lab::LabelReadyHost::setStartLikeLast (! lab::LabelReadyHost::startLikeLast());
                else if (r == 84)
                {
                    juce::StringArray l { "WHAT THE AUTOPILOT HAS LEARNED (across your songs)", "" };
                    for (const auto& [name, rec] : host.autoFix.learning.all())
                        l.add (name + ": kept " + juce::String (rec.kept) + ", undone " + juce::String (rec.undone) + (host.autoFix.learning.retired (name) ? "  (left alone now)" : host.autoFix.learning.favoured (name) ? "  (tried first)" : ""));
                    int n = 0; const auto taste = lab::Learning::tasteOffset (n);
                    l.add (""); l.add ("Your taste against the label (" + juce::String (n) + " milestones): " + (n >= 3 ? juce::String (taste[1], 1) + " dB lows, " + juce::String (taste[5], 1) + " dB mids, " + juce::String (taste[8], 1) + " dB top" : juce::String ("needs three milestones")));
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Learned", l.joinIntoString ("\n"));
                }
                if (r == 40) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Blind spots", host.blindSpots());
                else if (r == 41)
                {
                    juce::StringArray l;
                    for (const auto& q : host.listenQueue()) l.add ("bar " + juce::String (q.bar) + ": " + q.what);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Listen-through", l.isEmpty() ? juce::String ("Nothing flagged at a bar.") : l.joinIntoString ("\n"));
                }
                refresh();
            });
        }
        void impactMenu()
        {
            const auto options = processor.labelReady.dropImpactOptions (processor.getMap());
            juce::PopupMenu m;
            m.addSectionHeader ("The drops' impact (estimates of what each adds)");
            if (options.empty()) m.addItem (-1, "Play the song through first", false);
            for (size_t i = 0; i < options.size(); ++i) m.addItem ((int) i + 1, options[i].name + "  (" + options[i].detail + ")");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsB), [this, options] (int r)
            {
                if (r <= 0 || r > (int) options.size()) return;
                if (processor.applyLabelCheck (options[(size_t) r - 1])) note ("Done: " + options[(size_t) r - 1].name);
                refresh();
            });
        }
        void handoff()
        {
            askText ("Hand-off pack", "Who it's for (a mix engineer, a vocalist, a co-producer)", [this] (juce::String who)
            {
                chooser = std::make_unique<juce::FileChooser> ("Where to put the hand-off pack", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, who] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (! f.isDirectory()) return;
                    const auto map = processor.getMap();
                    processor.labelReady.writeHandoff (f, who, processor.getNotes().joinIntoString ("\n"),
                                                       processor.director.havePlan ? processor.director.planSheet (map, processor.barQuarters.load()) : juce::String(),
                                                       processor.renderPlan(), map).revealToUser();
                });
            });
        }
        void askText (const juce::String& title, const juce::String& prompt, std::function<void (juce::String)> done)
        {
            ask = std::make_unique<juce::AlertWindow> (title, prompt, juce::MessageBoxIconType::NoIcon);
            ask->addTextEditor ("t", "", "");
            ask->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            ask->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            ask->enterModalState (true, juce::ModalCallbackFunction::create ([this, done] (int r)
            {
                if (r == 1 && ask != nullptr && ask->getTextEditorContents ("t").trim().isNotEmpty()) done (ask->getTextEditorContents ("t").trim());
                ask.reset();
                refresh();
            }));
        }
    };
}
