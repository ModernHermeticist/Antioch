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
            fixB.onClick = [this] { if (const auto* c = selected()) { if (c->fix != suite::Fix::None) suite::FixBus::send (c->fix, c->fixData); else if (c->action.startsWith ("director:")) processor.director.chat (c->action.fromFirstOccurrenceOf (":", false, false).trim()), processor.directorDirty = true; note ("Sent: " + c->name); } };
            safeB.onClick = [this] { note (juce::String (processor.labelReady.fixAllSafe()) + " safe fix(es) sent; the scores update as the song plays."); };
            for (auto* b : { &fixB, &safeB, &profileB, &refsB, &milestoneB, &handoffB, &saveB }) addAndMakeVisible (b);
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
            g.drawText (juce::String (r.overall), head.removeFromLeft (90), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.setColour (Colours::text);
            auto right = head;
            g.drawText (r.ready ? "READY TO SEND" : "Not ready yet (" + juce::String (processor.labelReady.profile.readyThreshold) + " needed, nothing blocking)",
                        right.removeFromTop (22), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (13.0f));
            g.setColour (Colours::dimText);
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
                g.drawText (juce::String (areaName (a)) + (r.areaMeasured[(size_t) a] ? "  " + juce::String (juce::roundToInt (s * 100.0f)) : juce::String ("  -")), cell, juce::Justification::centredLeft, true);
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
            auto bottom = r.removeFromBottom (30);
            for (auto* b : { &fixB, &safeB, &profileB, &refsB, &milestoneB, &handoffB, &saveB }) { b->setBounds (bottom.removeFromLeft (150).reduced (2, 0)); }
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
        juce::TextButton fixB { "Fix this" }, safeB { "Fix all safe ones" }, profileB { "Label profile..." }, refsB { "References..." },
                         milestoneB { "Milestones..." }, handoffB { "Hand-off pack..." }, saveB { "Save the report..." };
        juce::Label status;
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
            if (processor.labelReady.analysing.isNotEmpty()) note ("Measuring \"" + processor.labelReady.analysing + "\"...");
            else if (processor.labelReady.lastAdded.isNotEmpty()) { note (processor.labelReady.lastAdded); processor.labelReady.lastAdded = {}; }
            repaint();
        }
        void showSelected()
        {
            const auto* c = selected();
            fixB.setEnabled (c != nullptr && (c->fix != suite::Fix::None || c->action.startsWith ("director:")));
            if (c == nullptr) { detail.setText ("Click a check to see what it found, where, and what would move it.\n\nBlocking:\n" + processor.labelReady.report.blockers.joinIntoString ("\n")); return; }
            juce::StringArray t;
            t.add (c->name + (c->blocking ? "  (blocks the gate when low)" : ""));
            t.add (c->measured ? "Score: " + juce::String (juce::roundToInt (c->score * 100.0f)) + "%" : juce::String ("Not measured yet"));
            t.add ("");
            t.add ("Found: " + c->detail);
            if (c->bar > 0) t.add ("Where: bar " + juce::String (c->bar) + " (FL's playhead can't be moved by a plugin: go there in the playlist)");
            if (c->explain.isNotEmpty()) t.add ("To move it: " + c->explain);
            if (c->fix != suite::Fix::None) t.add (juce::String ("One click: ") + (c->safeFix ? "a safe fix (in Fix all safe ones)" : "a fix (undoable in Mentalism's History)"));
            else if (c->action == "write stronger") t.add ("In Correspondence: Song page > Write it stronger (three stronger hooks, scored).");
            else if (c->action.startsWith ("director:")) t.add ("One click: tells the director \"" + c->action.fromFirstOccurrenceOf (":", false, false).trim() + "\"");
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
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&milestoneB), [this] (int r)
            {
                auto& lr = processor.labelReady;
                if (r == 1) askText ("Save a milestone", "What it is, e.g. \"sent to vocalist\", \"A&R round 1\"", [this] (juce::String n) { processor.labelReady.saveMilestone (n); note ("Milestone saved: " + n); });
                else if (r >= 300) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "What changed between the milestones", lr.diffMilestones (r - 301, r - 300).joinIntoString ("\n"));
                else if (r >= 100) { lr.restoreMilestone (r - 100); note ("Restoring \"" + lr.milestones[(size_t) r - 100].name + "\" across the suite"); }
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
