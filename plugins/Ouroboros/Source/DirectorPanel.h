#pragma once

// The director's page in Ouroboros: one timeline of everything (every plugin's part at every
// moment, over the tension and energy curves), a moment inspector (lock, re-roll, freeze, undo,
// rate, teach, templates, who leads), steering (story, genre, safe mode, bigger / smaller, painted
// intent per section, no-move zones, pins, chat) and trust (summary, lint, what changed, explain,
// history, taste, legend, fidelity, structure, the transitions render list, A/B, version lock).

#include "PluginProcessor.h"
#include "SuiteUI.h"

namespace ouro
{
    namespace Colours = suite::ui::Colours;

    class DirectorTimeline : public juce::Component, private juce::Timer
    {
    public:
        explicit DirectorTimeline (OuroborosProcessor& p) : processor (p) { startTimerHz (8); }
        std::function<void (int)> onSelect;             // a moment's index
        std::function<void (double)> onExplain;         // a song position (quarters)
        std::function<void (int, const juce::MouseEvent&)> onSectionMenu;   // a section, right-clicked
        std::function<void (int bar, const juce::MouseEvent&)> onLaneMenu;  // empty lane space, right-clicked
        int selected = -1;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Colours::background);
            const auto map = processor.getMap();
            const auto& d = processor.director;
            const auto& plan = d.abPrevious && ! d.previous.moments.empty() ? d.previous : d.plan;
            const auto barQ = processor.barQuarters.load();
            auto r = getLocalBounds().reduced (4);
            const auto totalQ = juce::jmax (64.0, map.totalBars * barQ);
            const auto x0 = (float) (r.getX() + labelW), w = (float) (r.getWidth() - labelW);
            auto xOf = [&] (double q) { return x0 + w * (float) (q / totalQ); };

            // sections, with painted intent
            auto strip = r.removeFromTop (22);
            for (int i = 0; i < map.count; ++i)
            {
                const auto s = (map.sections[(size_t) i].startBar - 1) * barQ, e = (map.endBar (i) - 1) * barQ;
                const auto box = juce::Rectangle<float> (xOf (s), (float) strip.getY(), xOf (e) - xOf (s), (float) strip.getHeight()).reduced (1.0f, 0.0f);
                const auto type = map.sections[(size_t) i].type;
                g.setColour ((type == suite::SectionType::Drop ? Colours::ember : type == suite::SectionType::Build ? Colours::brass
                              : type == suite::SectionType::Breakdown ? Colours::amethyst : Colours::outline).withAlpha (0.4f));
                g.fillRoundedRectangle (box, 3.0f);
                g.setColour (Colours::text);
                g.setFont (juce::FontOptions (11.0f));
                juce::String label = suite::sectionName (type);
                const auto intent = i < (int) d.settings.intent.size() ? d.settings.intent[(size_t) i] : (uint8_t) dr::iAuto;
                if (intent != dr::iAuto) label << " [" << dr::intentName (intent) << "]";
                if (i < (int) d.settings.sectionSpice.size() && std::abs (d.settings.sectionSpice[(size_t) i] - 1.0f) > 0.01f)
                    label << " " << juce::String (juce::roundToInt (d.settings.sectionSpice[(size_t) i] * 100.0f)) << "%";
                if (i < (int) plan.mood.size()) label << "  " << dr::moodName (plan.mood[(size_t) i]);
                g.drawText (label, box.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
            }
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Sections", strip.withWidth (labelW - 6), juce::Justification::centredLeft);

            // tension (brass) and energy (dim) curves
            auto curve = r.removeFromTop (46).withTrimmedLeft (labelW);
            g.setColour (Colours::panel);
            g.fillRect (curve);
            auto drawCurve = [&] (const std::vector<float>& v, juce::Colour c)
            {
                if (v.empty()) return;
                juce::Path p;
                for (size_t b = 0; b < v.size(); ++b)
                {
                    const auto x = xOf ((double) b * barQ), y = (float) curve.getBottom() - 2.0f - v[b] * (float) (curve.getHeight() - 4);
                    if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (c);
                g.strokePath (p, juce::PathStrokeType (1.5f));
            };
            drawCurve (plan.energy, Colours::dimText);
            drawCurve (plan.tension, Colours::brass);
            g.setColour (Colours::dimText);
            g.drawText ("Tension / energy", juce::Rectangle<int> (r.getX(), curve.getY(), labelW - 6, curve.getHeight()), juce::Justification::centredLeft);
            // no-move zones
            for (const auto& [a, b] : d.settings.noMoves)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRect (juce::Rectangle<float> (xOf ((a - 1) * barQ), (float) curve.getY(), xOf (b * barQ) - xOf ((a - 1) * barQ), (float) (getHeight() - curve.getY())));
            }

            // a lane per player
            r.removeFromTop (4);
            const auto laneH = juce::jmax (14, (r.getHeight() - 4) / dr::numPlayers);
            lanesTop = r.getY(); laneHeight = laneH;
            for (int p = 0; p < dr::numPlayers; ++p)
            {
                auto lane = r.removeFromTop (laneH);
                g.setColour (Colours::panel.withAlpha (p % 2 == 0 ? 0.6f : 0.35f));
                g.fillRect (lane.withTrimmedLeft (labelW));
                g.setColour (dr::playerColour (p));
                g.drawText (dr::playerName (p), lane.withWidth (labelW - 6), juce::Justification::centredLeft);
                for (int i = 0; i < (int) plan.moments.size(); ++i)
                {
                    const auto& m = plan.moments[(size_t) i];
                    if (! m.acts (p)) continue;
                    auto win = dr::windowOf (m, p, m.gesture[(size_t) p], barQ, processor.bpm.load());
                    if (p == dr::pSilence) win = { m.silenceStartQ, m.silenceEndQ };
                    auto box = juce::Rectangle<float> (xOf (win.first), (float) lane.getY() + 2.0f, juce::jmax (3.0f, xOf (win.second) - xOf (win.first)), (float) laneH - 4.0f);
                    g.setColour (dr::playerColour (p).withAlpha (m.hero == p ? 0.95f : 0.4f));
                    g.fillRoundedRectangle (box, 2.0f);
                    if (i == selected) { g.setColour (Colours::text); g.drawRoundedRectangle (box.expanded (1.0f), 2.0f, 1.5f); }
                    if (m.flags & (dr::fLocked | dr::fFrozen | dr::fPinned))
                    {
                        g.setColour (Colours::text);
                        g.fillEllipse (box.getX() + 1.0f, box.getY() + 1.0f, 4.0f, 4.0f);
                    }
                }
            }
            // the handoffs: arrows between legs, and low confidence marks
            for (int i = 0; i < (int) plan.moments.size(); ++i)
            {
                const auto& m = plan.moments[(size_t) i];
                const dr::Leg* prev = nullptr;
                for (const auto& l : m.legs)
                {
                    if (l.player == dr::pNone) continue;
                    if (prev != nullptr)
                    {
                        const auto y1 = (float) lanesTop + ((float) prev->player + 0.5f) * (float) laneH, y2 = (float) lanesTop + ((float) l.player + 0.5f) * (float) laneH;
                        g.setColour (Colours::text.withAlpha (0.7f));
                        g.drawArrow (juce::Line<float> (xOf (prev->atQ), y1, xOf (l.atQ), y2), 1.2f, 6.0f, 6.0f);
                    }
                    prev = &l;
                }
                if (m.confidence < 0.6f && (m.hero != dr::pNone || m.silence != dr::gNone))
                {
                    g.setColour (Colours::ember);
                    g.drawText ("?", juce::Rectangle<float> (xOf (m.atQ) - 5.0f, (float) lanesTop - 14.0f, 10.0f, 12.0f), juce::Justification::centred);
                }
            }
            // the playhead and the explained position
            g.setColour (Colours::text.withAlpha (0.8f));
            const auto ph = xOf (processor.ppq.load());
            g.drawVerticalLine ((int) ph, 0.0f, (float) getHeight());
            if (explainQ >= 0.0)
            {
                g.setColour (Colours::verdigris);
                g.drawVerticalLine ((int) xOf (explainQ), (float) lanesTop, (float) getHeight());
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto map = processor.getMap();
            const auto barQ = processor.barQuarters.load();
            const auto totalQ = juce::jmax (64.0, map.totalBars * barQ);
            const auto x0 = (float) (4 + labelW), w = (float) (getWidth() - 8 - labelW);
            const auto q = juce::jlimit (0.0, totalQ, (double) ((e.position.x - x0) / w) * totalQ);
            const auto bar = (int) std::floor (q / barQ) + 1;
            if (e.position.y < 26.0f)
            {
                int sec = -1;
                for (int i = 0; i < map.count; ++i) if (map.sections[(size_t) i].startBar <= bar) sec = i;
                if (sec >= 0 && onSectionMenu) onSectionMenu (sec, e);
                return;
            }
            const auto& plan = processor.director.plan;
            int best = -1;
            float bestDist = 1.0e9f;
            for (int i = 0; i < (int) plan.moments.size(); ++i)
            {
                const auto& m = plan.moments[(size_t) i];
                if (q < m.startQ - barQ * 0.5 || q > m.endQ + barQ * 0.5) continue;
                const auto dist = (float) std::abs (q - m.atQ);
                if (dist < bestDist) { bestDist = dist; best = i; }
            }
            if (e.mods.isPopupMenu())
            {
                if (onLaneMenu) onLaneMenu (bar, e);
                return;
            }
            explainQ = q;
            if (onExplain) onExplain (q);
            if (best >= 0) { selected = best; if (onSelect) onSelect (best); }
            repaint();
        }

        double explainQ = -1.0;

    private:
        void timerCallback() override { repaint(); }
        OuroborosProcessor& processor;
        static constexpr int labelW = 104;
        int lanesTop = 0, laneHeight = 14;
    };

    //==============================================================================
    class DirectorPanel : public juce::Component, private juce::Timer
    {
    public:
        explicit DirectorPanel (OuroborosProcessor& p) : processor (p), timeline (p)
        {
            auto& d = processor.director;
            d.loadTemplates();
            enabled.setButtonText ("Director on");
            enabled.setToggleState (d.settings.enabled, juce::dontSendNotification);
            enabled.setTooltip ("On: Ouroboros plans every transition and every plugin plays its part. Off: each plugin plays its own transitions as before.");
            enabled.onClick = [this] { edit ("Director " + juce::String (enabled.getToggleState() ? "on" : "off"), [this] { processor.director.settings.enabled = enabled.getToggleState(); }); };
            for (int s = 0; s < dr::numStories; ++s) story.addItem (juce::String ("Story: ") + dr::storyName (s), s + 1);
            story.setSelectedId (d.settings.story + 1, juce::dontSendNotification);
            story.setTooltip ("Restrained: few moves, rarely silent. Classic: the usual trance arc. Festival: big, with silences and a fake drop. Experimental: surprises, tempo moves.");
            story.onChange = [this] { edit ("Story", [this] { processor.director.settings.story = story.getSelectedId() - 1; }); };
            for (int g = 0; g < dr::numGenres; ++g) genre.addItem (juce::String ("Profile: ") + dr::genreName (g), g + 1);
            genre.setSelectedId (d.settings.genre + 1, juce::dontSendNotification);
            genre.onChange = [this] { edit ("Genre profile", [this] { processor.director.settings.genre = genre.getSelectedId() - 1; }); };
            safe.setButtonText ("Safe mode");
            safe.setToggleState (d.settings.safe, juce::dontSendNotification);
            safe.setTooltip ("Only low-risk moves (filters, levels, space): for a radio edit or a client mix");
            safe.onClick = [this] { edit ("Safe mode", [this] { processor.director.settings.safe = safe.getToggleState(); }); };
            silences.setButtonText ("Silences before drops");
            silences.setToggleState (d.settings.silencesBeforeDrops, juce::dontSendNotification);
            silences.setTooltip ("Off (the default): never a full silence before a drop (no Snap, no Breath). The kick-only bar and the rare fake drop are unaffected.");
            silences.onClick = [this] { edit ("Silences before drops", [this] { processor.director.settings.silencesBeforeDrops = silences.getToggleState(); }); };
            bigger.setSliderStyle (juce::Slider::LinearHorizontal);
            bigger.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            bigger.setRange (-2.0, 2.0, 0.5);
            bigger.setValue (d.settings.bigger, juce::dontSendNotification);
            bigger.setTooltip ("Smaller (left) or bigger (right): every moment's budget, the whole song");
            bigger.onDragEnd = [this] { edit ("Bigger / smaller", [this] { processor.director.settings.bigger = (float) bigger.getValue(); }); };
            reroll.onClick = [this] { processor.director.rerollSong(); changed(); };
            reroll.setTooltip ("New dice for the whole song (locked, frozen and pinned moments stay)");
            versionLock.setButtonText ("Version lock");
            versionLock.setToggleState (d.settings.versionLock, juce::dontSendNotification);
            versionLock.setTooltip ("Keep this exact plan: it reproduces later, whatever else changes (for a render you may need to redo)");
            versionLock.onClick = [this] { processor.director.setVersionLock (versionLock.getToggleState()); changed(); };
            ab.setButtonText ("Hear the previous plan (A/B)");
            ab.setToggleState (d.abPrevious, juce::dontSendNotification);
            ab.onClick = [this] { processor.director.abPrevious = ab.getToggleState(); changed(); };
            for (auto* c : std::initializer_list<juce::Component*> { &enabled, &story, &genre, &safe, &silences, &bigger, &reroll, &versionLock, &ab }) addAndMakeVisible (c);

            addAndMakeVisible (timeline);
            timeline.onSelect = [this] (int i) { selectMoment (i); };
            timeline.onExplain = [this] (double q) { explainAt = q; show (pExplain); };
            timeline.onSectionMenu = [this] (int sec, const juce::MouseEvent&) { sectionMenu (sec); };
            timeline.onLaneMenu = [this] (int bar, const juce::MouseEvent&) { laneMenu (bar); };

            // inspector
            for (auto* b : { &lockB, &rerollB, &freezeB, &undoB, &upB, &downB, &teachB, &saveTemplateB, &applyTemplateB, &editB })
            {
                addAndMakeVisible (b);
                b->setEnabled (false);
            }
            lockB.onClick = [this] { if (auto* m = current()) { auto& d = processor.director; (m->flags & dr::fLocked) ? d.unlock (m->key) : d.lock (m->key); changed(); } };
            rerollB.onClick = [this] { if (auto* m = current()) { processor.director.reroll (m->key); changed(); } };
            freezeB.onClick = [this] { if (auto* m = current()) { processor.director.freeze (m->key, ! (m->flags & dr::fFrozen)); changed(); } };
            undoB.onClick = [this] { if (auto* m = current()) { processor.director.undoMoment (m->key); changed(); } };
            upB.onClick = [this] { if (auto* m = current()) { processor.director.rate (m->key, 1); changed(); } };
            downB.onClick = [this] { if (auto* m = current()) { processor.director.rate (m->key, -1); changed(); } };
            teachB.setTooltip ("Teach by example: this moment, as it is now (shape it with Edit... first), becomes one of your gestures for this kind of moment");
            teachB.onClick = [this] { askName ("Teach the director", "A name for this gesture", [this] (juce::String n) { if (auto* m = current()) { processor.director.teach (m->key, n); changed(); } }); };
            saveTemplateB.onClick = [this] { askName ("Save as a template", "A name for this transition", [this] (juce::String n) { if (auto* m = current()) { processor.director.saveTemplate (n, m->key); changed(); } }); };
            applyTemplateB.onClick = [this]
            {
                auto* m = current();
                if (m == nullptr) return;
                juce::PopupMenu menu;
                const auto& t = processor.director.templates;
                for (int i = 0; i < (int) t.size(); ++i) menu.addItem (i + 1, t[(size_t) i].name);
                if (t.empty()) menu.addItem (-1, "No templates yet (Save as template first)", false);
                const auto key = m->key;
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&applyTemplateB), [this, key] (int r)
                {
                    if (r > 0 && r <= (int) processor.director.templates.size()) { processor.director.applyTemplate (processor.director.templates[(size_t) r - 1].name, key); changed(); }
                });
            };
            editB.setTooltip ("Who leads, what each plugin plays here, the silence, which track leads");
            editB.onClick = [this] { editMenu(); };
            inspector.setMultiLine (true);
            inspector.setReadOnly (true);
            inspector.setFont (juce::FontOptions (12.5f));
            addAndMakeVisible (inspector);

            // pages
            static const char* names[] = { "Summary", "Lint", "What changed", "Explain", "History", "Taste", "Legend", "Fidelity", "Structure", "Render list" };
            for (int i = 0; i < numPages; ++i)
            {
                pageButtons[(size_t) i].setButtonText (names[i]);
                pageButtons[(size_t) i].setClickingTogglesState (true);
                pageButtons[(size_t) i].setRadioGroupId (4711);
                pageButtons[(size_t) i].onClick = [this, i] { show (i); };
                addAndMakeVisible (pageButtons[(size_t) i]);
            }
            page.setMultiLine (true);
            page.setReadOnly (true);
            page.setFont (juce::FontOptions (12.5f));
            addAndMakeVisible (page);
            pageAction.onClick = [this] { pageActionClicked(); };
            addAndMakeVisible (pageAction);

            chat.setTextToShowWhenEmpty ("Tell the director: \"make the second drop hit harder\", \"less glitch in the breakdown\", \"darker\", \"undo\"...", Colours::dimText);
            chat.onReturnKey = [this] { sendChat(); };
            addAndMakeVisible (chat);
            send.onClick = [this] { sendChat(); };
            addAndMakeVisible (send);
            reply.setColour (juce::Label::textColourId, Colours::verdigris);
            addAndMakeVisible (reply);
            profiles.setTextWhenNothingSelected ("Profiles...");
            profiles.onChange = [this] { profileChosen(); };
            addAndMakeVisible (profiles);
            fillProfiles();
            tickerLabel.setColour (juce::Label::textColourId, Colours::brass);
            addAndMakeVisible (tickerLabel);

            show (pSummary);
            setSize (1240, 820);
            startTimerHz (4);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Colours::panel);
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText ("The director: every transition, one plan", getLocalBounds().reduced (14).removeFromTop (22), juce::Justification::centredLeft);
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Click a moment to inspect it, anywhere to explain that second; right-click a section to paint its intent, a lane to pin or block moves.",
                        getLocalBounds().reduced (14).withTrimmedTop (22).removeFromTop (16), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (14);
            r.removeFromTop (42);
            auto top = r.removeFromTop (28);
            enabled.setBounds (top.removeFromLeft (110)); top.removeFromLeft (6);
            story.setBounds (top.removeFromLeft (170)); top.removeFromLeft (6);
            genre.setBounds (top.removeFromLeft (180)); top.removeFromLeft (6);
            safe.setBounds (top.removeFromLeft (100)); top.removeFromLeft (6);
            silences.setBounds (top.removeFromLeft (170)); top.removeFromLeft (6);
            bigger.setBounds (top.removeFromLeft (150)); top.removeFromLeft (6);
            reroll.setBounds (top.removeFromLeft (130)); top.removeFromLeft (6);
            versionLock.setBounds (top.removeFromLeft (120)); top.removeFromLeft (6);
            ab.setBounds (top);
            r.removeFromTop (4);
            tickerLabel.setBounds (r.removeFromTop (22));
            r.removeFromTop (2);
            timeline.setBounds (r.removeFromTop (290));
            r.removeFromTop (8);
            auto bottom = r.removeFromBottom (30);
            profiles.setBounds (bottom.removeFromRight (200));
            bottom.removeFromRight (8);
            send.setBounds (bottom.removeFromRight (70));
            chat.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 + 120));
            bottom.removeFromLeft (8);
            reply.setBounds (bottom);
            r.removeFromBottom (6);
            auto right = r.removeFromRight (420);
            auto buttons = right.removeFromBottom (60);
            auto row1 = buttons.removeFromTop (28), row2 = buttons.withTrimmedTop (4);
            const auto bw = row1.getWidth() / 5;
            for (auto* b : { &lockB, &rerollB, &freezeB, &undoB, &editB }) b->setBounds (row1.removeFromLeft (bw).reduced (2, 0));
            for (auto* b : { &upB, &downB, &teachB, &saveTemplateB, &applyTemplateB }) b->setBounds (row2.removeFromLeft (bw).reduced (2, 0));
            inspector.setBounds (right.withTrimmedBottom (4));
            r.removeFromRight (10);
            auto tabs = r.removeFromTop (26);
            const auto tw = tabs.getWidth() / numPages;
            for (auto& b : pageButtons) b.setBounds (tabs.removeFromLeft (tw).reduced (1, 0));
            r.removeFromTop (4);
            pageAction.setBounds (r.removeFromBottom (26).removeFromRight (260));
            r.removeFromBottom (4);
            page.setBounds (r);
        }

    private:
        enum Page { pSummary, pLint, pDiff, pExplain, pHistory, pTaste, pLegend, pFidelity, pStructure, pRender, numPages };
        OuroborosProcessor& processor;
        DirectorTimeline timeline;
        juce::ToggleButton enabled, safe, versionLock, ab, silences;
        juce::ComboBox story, genre, profiles;
        juce::Slider bigger;
        juce::TextButton reroll { "Re-roll the song" };
        juce::TextButton lockB { "Lock" }, rerollB { "Re-roll" }, freezeB { "Freeze" }, undoB { "Undo" }, editB { "Edit..." },
                         upB { "Rate up" }, downB { "Rate down" }, teachB { "Like this" }, saveTemplateB { "Save template" }, applyTemplateB { "Template..." };
        juce::TextEditor inspector, page, chat;
        juce::TextButton send { "Tell" }, pageAction;
        juce::Label reply, tickerLabel;
        std::array<juce::TextButton, numPages> pageButtons;
        int currentPage = pSummary, selectedIndex = -1;
        uint32_t selectedKey = 0, shownHash = 0;
        double explainAt = 0.0;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::AlertWindow> nameWindow;
        std::vector<std::pair<int, dr::Moment>> altChoices;
        juce::StringArray optTracks;

        const dr::Moment* current() const
        {
            const auto i = processor.director.plan.indexOf (selectedKey);
            return i >= 0 ? &processor.director.plan.moments[(size_t) i] : nullptr;
        }
        void changed()
        {
            processor.replan();
            refresh();
        }
        void edit (const juce::String& label, std::function<void()> fn)
        {
            processor.director.pushHistory (label);
            fn();
            changed();
        }
        void timerCallback() override
        {
            tickerLabel.setText (processor.director.ticker (processor.ppq.load(), processor.barQuarters.load()), juce::dontSendNotification);
            if (processor.director.plan.hash != shownHash) refresh();
        }
        void refresh()
        {
            shownHash = processor.director.plan.hash;
            const auto i = processor.director.plan.indexOf (selectedKey);
            timeline.selected = i;
            showInspector();
            show (currentPage);
            timeline.repaint();
        }
        void selectMoment (int index)
        {
            const auto& plan = processor.director.plan;
            if (index < 0 || index >= (int) plan.moments.size()) return;
            selectedKey = plan.moments[(size_t) index].key;
            selectedIndex = index;
            showInspector();
        }
        void showInspector()
        {
            const auto* m = current();
            for (auto* b : { &lockB, &rerollB, &freezeB, &undoB, &upB, &downB, &teachB, &saveTemplateB, &applyTemplateB, &editB }) b->setEnabled (m != nullptr);
            if (m == nullptr) { inspector.setText ("Click a moment on the timeline."); return; }
            const auto barQ = processor.barQuarters.load();
            const auto& plan = processor.director.plan;
            lockB.setButtonText ((m->flags & dr::fLocked) ? "Unlock" : "Lock");
            freezeB.setButtonText ((m->flags & dr::fFrozen) ? "Unfreeze" : "Freeze");
            undoB.setEnabled (processor.director.undoDepth (m->key) > 0);
            auto bar = [barQ] (float q) { return juce::String ((int) std::floor (q / barQ + 1.0e-6) + 1); };
            juce::StringArray t;
            t.add (juce::String ("Bar ") + bar (m->atQ) + ": " + dr::kindName (m->kind) + "  (bars " + bar (m->startQ) + "-" + bar (m->endQ) + ")");
            t.add (juce::String ("Mood ") + dr::moodName (m->mood) + (m->dropOrdinal >= 0 && (m->kind & (dr::kIntoDrop | dr::kPhrase)) ? juce::String (", drop character ") + dr::characterName (m->character) : juce::String())
                   + ", tension " + juce::String (m->tension, 2));
            t.add ("Budget " + juce::String (m->budget, 1) + ", spent " + juce::String (m->spent, 1) + ", confidence " + juce::String (juce::roundToInt (m->confidence * 100.0f)) + "%"
                   + (m->confidence < 0.6f ? "  (low: worth a listen)" : ""));
            t.add ("");
            t.add (m->hero != dr::pNone ? juce::String ("Hero: ") + dr::playerName (m->hero) + ", " + dr::gesture (m->heroGesture).name : juce::String ("Hero: nobody"));
            if (m->heroRole != 0) t.add ("  on the " + juce::String (dr::roleName (m->heroRole)).toLowerCase() + " track");
            for (int p = 0; p < dr::numPlayers; ++p)
                if (m->supports (p)) t.add (juce::String ("Support: ") + dr::playerName (p) + ", " + dr::gesture (m->gesture[(size_t) p]).name);
            juce::StringArray legs;
            for (const auto& l : m->legs) if (l.player != dr::pNone) legs.add (juce::String (dr::playerName (l.player)) + (l.gesture != dr::gNone ? juce::String (" ") + dr::gesture (l.gesture).name : juce::String (" re-entry")));
            if (! legs.isEmpty()) t.add ("Handoff: " + legs.joinIntoString (" -> "));
            if (m->harmony != dr::hNone) t.add (juce::String ("Harmony: ") + dr::harmonyName (m->harmony));
            if (m->tempo != dr::tNone) t.add (juce::String ("Tempo: ") + dr::tempoName (m->tempo));
            if (m->edit != dr::eNone) t.add (juce::String ("Arrangement: ") + dr::editName (m->edit));
            if (m->evenDb < -0.05f) t.add ("Loudness: the first 2 bars ride " + juce::String (m->evenDb, 1) + " dB to even out a measured jump");
            if (m->kind == dr::kIntoDrop)
                t.add ("Mix: others dip " + juce::String (m->premixDb, 1) + " dB for the hero, build held " + juce::String (m->buildHoldDb, 1) + " dB (a level ride: no limiter), dry " + juce::String (juce::roundToInt (m->dryBefore * 100)) + "% then wide " + juce::String (juce::roundToInt (m->wideOnHit * 100))
                       + "%, hook dip " + juce::String (m->hookDipDb, 1) + " dB" + (m->lowClearBeats > 0.0f ? ", sub cleared " + juce::String (m->lowClearBeats, 1) + " beats" : juce::String()));
            juce::StringArray flags;
            if (m->flags & dr::fLocked) flags.add ("locked"); if (m->flags & dr::fFrozen) flags.add ("frozen"); if (m->flags & dr::fPinned) flags.add ("pinned");
            if (m->flags & dr::fMotif) flags.add ("motif"); if (m->flags & dr::fRecall) flags.add ("recall"); if (m->flags & dr::fSurprise) flags.add ("surprise");
            if (m->flags & dr::fSignature) flags.add ("signature"); if (m->flags & dr::fLearned) flags.add ("taught"); if (m->flags & dr::fVocal) flags.add ("under the vocal");
            if (m->flags & dr::fAudio) flags.add ("from what was heard"); if (m->flags & dr::fTrimmed) flags.add ("stepped back");
            if (! flags.isEmpty()) t.add ("Flags: " + flags.joinIntoString (", "));
            const auto i = plan.indexOf (m->key);
            if (i >= 0 && i < (int) plan.why.size())
            {
                t.add ("");
                t.add ("Why:");
                for (const auto& w : plan.why[(size_t) i]) t.add ("  " + w);
            }
            inspector.setText (t.joinIntoString ("\n"));
        }
        void show (int which)
        {
            currentPage = which;
            pageButtons[(size_t) which].setToggleState (true, juce::dontSendNotification);
            auto& d = processor.director;
            const auto map = processor.getMap();
            const auto barQ = processor.barQuarters.load(), bpm = processor.bpm.load();
            juce::String text, action;
            switch (which)
            {
                case pSummary:
                {
                    text = d.havePlan ? dr::summary (d.plan, map, barQ) : juce::String ("No plan: the director needs a section map (and to be on).");
                    const auto present = d.presentOverride != 0 ? d.presentOverride : DirectorHost::presentPlayers();
                    juce::StringArray who, missing;
                    for (int p = 0; p < dr::pScene; ++p) ((present & (1u << p)) ? who : missing).add (dr::playerName (p));
                    text << "\n\nCan play: " << who.joinIntoString (", ") << (missing.isEmpty() ? juce::String() : ". Not in the session (no parts for them): " + missing.joinIntoString (", ")) << ".";
                    if (! d.settings.optOut.isEmpty()) text << "\nIgnoring the director (their own transitions): " << d.settings.optOut.joinIntoString (", ") << ".";
                    action = "Save the plan / export its moves...";
                    break;
                }
                case pLint:
                {
                    juce::StringArray l;
                    for (const auto& x : d.plan.lints) l.add ((x.severity >= 2 ? "PROBLEM  " : "note  ") + x.text);
                    text = l.isEmpty() ? "Nothing to fix: no collisions, no drop without contrast, no gesture under a sung word." : l.joinIntoString ("\n");
                    if (std::any_of (d.plan.lints.begin(), d.plan.lints.end(), [] (const dr::Lint& x) { return x.severity >= 2 && x.moment >= 0; })) action = "Re-roll the moments with problems";
                    break;
                }
                case pDiff: text = d.lastDiff.isEmpty() ? "No changes since the last plan." : "Since the last plan:\n" + d.lastDiff.joinIntoString ("\n"); break;
                case pExplain: text = dr::explain (d.plan, map, explainAt, barQ, bpm).joinIntoString ("\n"); break;
                case pHistory:
                {
                    juce::StringArray l;
                    for (int i = (int) d.history.size() - 1; i >= 0; --i) l.add (juce::String (i + 1) + ". " + d.history[(size_t) i].when.formatted ("%H:%M:%S  ") + d.history[(size_t) i].label);
                    text = l.isEmpty() ? "No plans yet: every change you make is kept here." : "Every plan you've heard (newest first). Step back with the button:\n" + l.joinIntoString ("\n");
                    action = "Step back to the one before...";
                    break;
                }
                case pTaste:
                {
                    text = dr::tasteReport (d.settings.taste);
                    juce::StringArray learned;
                    for (const auto& l : d.settings.learned) learned.add (l.name + " (" + dr::kindName (l.kind) + ")");
                    if (! learned.isEmpty()) text << "\n\nTaught by you: " << learned.joinIntoString (", ");
                    if (! d.settings.requests.empty())
                    {
                        juce::StringArray r;
                        for (const auto& q : d.settings.requests) r.add (dr::describe (q));
                        text << "\n\nYour requests: " << r.joinIntoString ("; ");
                    }
                    action = "Profiles, taught gestures, templates...";
                    break;
                }
                case pLegend: text = dr::legend().joinIntoString ("\n"); break;
                case pFidelity: text = d.fidelityReport (barQ) + "\n\nPlay the song through, then check here: every planned part should have happened, and nothing extra."; action = "Start counting again"; break;
                case pStructure:
                {
                    const auto& a = d.plan.structure;
                    text = a.any ? "Suggested with the moves:\n" + a.reasons.joinIntoString ("\n") : juce::String ("The structure suits the story: nothing to change.");
                    if (d.reference.valid)
                        text << "\n\nReference \"" << d.reference.name << "\": " << juce::String (d.reference.movesPerMinute, 1) << " moves a minute, silence at "
                             << juce::roundToInt (d.reference.silenceShare * 100.0f) << "% of its changes, big changes at " << juce::roundToInt (d.reference.bigShare * 100.0f) << "%: the plan is scored against it.";
                    if (d.rehearsalBars > 0) text << "\n\nRehearsal: " << d.rehearsalBars << " bars heard (how loud and busy each is): the plan takes it into account.";
                    if (a.any) action = "Apply the structure";
                    break;
                }
                case pRender: text = "Render just the transitions (2 bars either side of each), to hear every move quickly:\n" + d.renderList (barQ, bpm).joinIntoString ("\n");
                              action = "Export the transition markers..."; break;
                default: break;
            }
            page.setText (text);
            pageAction.setButtonText (action);
            pageAction.setVisible (action.isNotEmpty());
        }
        void pageActionClicked()
        {
            auto& d = processor.director;
            switch (currentPage)
            {
                case pHistory:
                {
                    juce::PopupMenu menu;
                    for (int i = (int) d.history.size() - 1; i >= juce::jmax (0, (int) d.history.size() - 30); --i) menu.addItem (i + 1, d.history[(size_t) i].label);
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pageAction), [this] (int r) { if (r > 0) { processor.director.stepBack (r - 1); changed(); } });
                    break;
                }
                case pTaste:
                {
                    juce::PopupMenu menu, learned, templates;
                    for (int i = 0; i < (int) d.settings.learned.size(); ++i) learned.addItem (100 + i, d.settings.learned[(size_t) i].name);
                    for (int i = 0; i < (int) d.templates.size(); ++i) templates.addItem (300 + i, d.templates[(size_t) i].name);
                    menu.addItem (1, "Save my profile...");
                    menu.addSubMenu ("Forget a taught gesture", learned, ! d.settings.learned.empty());
                    menu.addSubMenu ("Delete a template", templates, ! d.templates.empty());
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pageAction), [this] (int r)
                    {
                        auto& dd = processor.director;
                        if (r == 1) askName ("Save a director profile", "A name for your profile (story, genre, taste, requests, taught gestures)", [this] (juce::String n)
                                             { processor.director.saveProfile (DirectorHost::profileFolder().getChildFile (juce::File::createLegalFileName (n) + ".xml")); fillProfiles(); });
                        else if (r >= 300) { dd.deleteTemplate (r - 300); show (pTaste); }
                        else if (r >= 100) { dd.forgetLearned (r - 100); changed(); }
                    });
                    break;
                }
                case pLint: { const auto n = d.rerollProblems(); changed(); reply.setText ("Re-rolled " + juce::String (n) + " moment(s) the lint flagged.", juce::dontSendNotification); break; }
                case pSummary:
                {
                    juce::PopupMenu menu;
                    menu.addItem (1, "Save the plan as a sheet (text)...");
                    menu.addItem (2, "Export the director's moves for FL (MIDI: a track per plugin)...");
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pageAction), [this] (int r)
                    {
                        if (r <= 0) return;
                        const auto midi = r == 2;
                        chooser = std::make_unique<juce::FileChooser> (midi ? "Export the director's moves" : "Save the plan sheet",
                                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (midi ? "Director moves.mid" : "Director plan.txt"),
                                                                       midi ? "*.mid" : "*.txt");
                        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, midi] (const juce::FileChooser& fc)
                        {
                            const auto f = fc.getResult();
                            if (f == juce::File()) return;
                            const auto barQ = processor.barQuarters.load(), bpm = processor.bpm.load();
                            f.deleteFile();
                            if (midi) { juce::FileOutputStream out (f); if (out.openedOk()) processor.director.movesMidi (barQ, bpm).writeTo (out); }
                            else f.replaceWithText (processor.director.planSheet (processor.getMap(), barQ));
                            f.revealToUser();
                        });
                    });
                    break;
                }
                case pFidelity: suite::DirectorBus::clearActed(); show (pFidelity); break;
                case pStructure: if (d.plan.structure.any) { processor.setMap (d.plan.structure.suggested); processor.addNote ("Director: structure applied (" + d.plan.structure.reasons.joinIntoString ("; ") + ")"); changed(); } break;
                case pRender:
                {
                    chooser = std::make_unique<juce::FileChooser> ("Save the transition markers", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Director transitions.mid"), "*.mid");
                    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                    {
                        const auto f = fc.getResult();
                        if (f == juce::File()) return;
                        f.deleteFile();
                        juce::FileOutputStream out (f);
                        const auto barQ = processor.barQuarters.load(), bpm = processor.bpm.load();
                        if (out.openedOk() && processor.director.renderMarkers (barQ, bpm).writeTo (out))
                        {
                            f.withFileExtension ("txt").replaceWithText (processor.director.renderList (barQ, bpm).joinIntoString ("\n"));
                            f.revealToUser();
                        }
                    });
                    break;
                }
                default: break;
            }
        }
        void sendChat()
        {
            const auto text = chat.getText();
            if (text.trim().isEmpty()) return;
            auto said = processor.director.chat (text);
            chat.clear();
            safe.setToggleState (processor.director.settings.safe, juce::dontSendNotification);
            silences.setToggleState (processor.director.settings.silencesBeforeDrops, juce::dontSendNotification);
            story.setSelectedId (processor.director.settings.story + 1, juce::dontSendNotification);
            const auto before = processor.director.plan.hash;
            changed();
            // what it changed, in a line
            if (processor.director.plan.hash != before && ! processor.director.lastDiff.isEmpty())
                said << "  Changed: " << processor.director.lastDiff.size() << " moment(s), e.g. " << processor.director.lastDiff[0] << ".";
            else if (processor.director.plan.hash == before) said << "  (The plan didn't change.)";
            reply.setText (said, juce::dontSendNotification);
        }
        void sectionMenu (int sec)
        {
            juce::PopupMenu menu, intent, spice, mood;
            auto& d = processor.director;
            const auto cur = sec < (int) d.settings.intent.size() ? d.settings.intent[(size_t) sec] : (uint8_t) dr::iAuto;
            for (int i = 0; i < dr::numIntents; ++i) intent.addItem (100 + i, dr::intentName (i), true, cur == i);
            for (int v : { 0, 50, 100, 150, 200 }) spice.addItem (500 + v, juce::String (v) + "%");
            mood.addItem (300, "The director's");
            for (int i = 0; i < dr::numMoods; ++i) mood.addItem (301 + i, dr::moodName (i));
            menu.addSubMenu ("Intent (paint)", intent);
            menu.addSubMenu ("Spice in this section", spice);
            menu.addSubMenu ("Mood", mood);
            menu.showMenuAsync ({}, [this, sec] (int r)
            {
                auto& dd = processor.director;
                if (r >= 100 && r < 100 + dr::numIntents) edit ("Intent", [&dd, sec, r] { dd.setIntent (sec, r - 100); });
                else if (r >= 500 && r <= 700) edit ("Section spice", [&dd, sec, r] { dd.setSectionSpice (sec, (float) (r - 500) / 100.0f); });
                else if (r >= 300 && r <= 300 + dr::numMoods) edit ("Mood", [&dd, sec, r] { dd.setSectionMood (sec, r - 301); });
            });
        }
        void laneMenu (int bar)
        {
            juce::PopupMenu menu, pin;
            for (int p = 0; p < dr::pSilence; ++p) pin.addItem (10 + p, dr::playerName (p));
            pin.addItem (10 + dr::pSilence, "Silence");
            menu.addSubMenu ("Pin a hero at bar " + juce::String (bar), pin);
            menu.addItem (1, "Remove the pin at bar " + juce::String (bar));
            menu.addItem (2, "No moves for 8 bars from bar " + juce::String (bar));
            menu.addItem (3, "Allow moves everywhere again");
            juce::PopupMenu opt;
            optTracks.clear();
            for (const auto& e : suite::Registry::list())
                if (e.track.isNotEmpty() && ! optTracks.contains (e.track) && juce::String ("Transmutation Kybalion Antioch Azoth Quintessence Prima Materia").contains (e.plugin)) optTracks.add (e.track);
            optTracks.sort (true);
            for (int t = 0; t < optTracks.size(); ++t) opt.addItem (900 + t, optTracks[t], true, processor.director.settings.optOut.contains (optTracks[t]));
            menu.addSubMenu ("Tracks that ignore the director (their own transitions)", opt, ! optTracks.isEmpty());
            menu.showMenuAsync ({}, [this, bar] (int r)
            {
                auto& d = processor.director;
                if (r >= 10 && r < 10 + dr::numPlayers) edit ("Pin at bar " + juce::String (bar), [&d, bar, r] { d.pin ({ bar, (uint8_t) (r - 10), dr::gNone }); });
                else if (r == 1) edit ("Unpin bar " + juce::String (bar), [&d, bar] { d.pin ({ bar, dr::pNone, dr::gNone }); });
                else if (r == 2) edit ("No moves", [&d, bar] { d.addNoMoves (bar, bar + 7); });
                else if (r == 3) edit ("Moves everywhere", [&d] { d.clearNoMoves(); });
                else if (r >= 900 && r - 900 < optTracks.size())
                {
                    const auto t = optTracks[r - 900];
                    edit ((d.settings.optOut.contains (t) ? "Follow the director again: " : "Ignore the director: ") + t, [&d, t]
                    {
                        if (d.settings.optOut.contains (t)) d.settings.optOut.removeString (t); else d.settings.optOut.add (t);
                    });
                }
            });
        }
        /** Edit a moment by hand: the hero, each player's gesture, the silence, the lead track. */
        void editMenu()
        {
            const auto* cur = current();
            if (cur == nullptr) return;
            juce::PopupMenu menu;
            std::array<juce::PopupMenu, dr::numPlayers> per;
            for (const auto& g : dr::gestures())
                if (g.player != dr::pNone && (g.kinds & cur->kind))
                    per[(size_t) g.player].addItem (1000 + g.id, juce::String (g.name) + (cur->gesture[(size_t) g.player] == g.id ? "  (playing)" : ""));
            for (int p = 0; p < dr::numPlayers; ++p)
            {
                per[(size_t) p].addSeparator();
                per[(size_t) p].addItem (2000 + p, "Rest here");
                per[(size_t) p].addItem (3000 + p, "Make it the hero", cur->acts (p) && cur->hero != p);
                menu.addSubMenu (dr::playerName (p), per[(size_t) p]);
            }
            juce::PopupMenu roles, lockOne, rerollOne, alternatives;
            for (int r = 0; r <= 7; ++r) roles.addItem (4000 + r, juce::String (dr::roleName (r)));
            menu.addSubMenu ("The track that leads (Transmutation, Antioch)", roles);
            menu.addSeparator();
            const auto& d0 = processor.director;
            const auto locked = d0.settings.playerLocks.count (cur->key) ? d0.settings.playerLocks.at (cur->key) : std::array<uint8_t, dr::numPlayers> {};
            for (int p = 0; p < dr::numPlayers; ++p)
                if (cur->acts (p))
                {
                    lockOne.addItem (5000 + p, juce::String (dr::playerName (p)) + ": " + dr::gesture (cur->gesture[(size_t) p]).name, true, locked[(size_t) p] != dr::gNone);
                    rerollOne.addItem (6000 + p, juce::String ("A different ") + dr::playerName (p) + " move (the rest stay)");
                }
            menu.addSubMenu ("Lock one player here", lockOne);
            menu.addSubMenu ("Re-roll one player here", rerollOne);
            alternatives = juce::PopupMenu();
            altChoices = d0.alternatives (cur->key, processor.getMap(), processor.bpm.load(), processor.barQuarters.load());
            for (int a = 0; a < (int) altChoices.size(); ++a)
            {
                const auto& m = altChoices[(size_t) a].second;
                alternatives.addItem (7000 + a, (m.hero != dr::pNone ? juce::String (dr::playerName (m.hero)) + " " + dr::gesture (m.heroGesture).name : juce::String ("nobody leads"))
                                                    + "  (" + juce::String (m.supporters()) + " supporting)");
            }
            menu.addSubMenu ("Alternatives for this moment", alternatives, ! altChoices.empty());
            menu.addItem (8000, "Lock every other moment (only this one moves)");
            const auto key = cur->key;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editB), [this, key] (int r)
            {
                auto& d = processor.director;
                const auto i = d.plan.indexOf (key);
                if (i < 0 || r <= 0) return;
                if (r == 8000) { d.lockAllExcept (key); changed(); return; }
                if (r >= 7000) { if (r - 7000 < (int) altChoices.size()) d.useAlternative (key, altChoices[(size_t) (r - 7000)].first); changed(); return; }
                if (r >= 6000) { d.rerollPlayer (key, r - 6000); changed(); return; }
                if (r >= 5000) { const auto p = r - 5000; const auto has = d.settings.playerLocks.count (key) && d.settings.playerLocks.at (key)[(size_t) p] != dr::gNone; d.lockPlayer (key, p, ! has); changed(); return; }
                if (r >= 4000) { d.setHeroRole (key, r - 4000); changed(); return; }
                auto m = d.plan.moments[(size_t) i];
                if (r >= 3000) { const auto p = r - 3000; m.hero = (uint8_t) p; m.heroGesture = m.gesture[(size_t) p]; }
                else if (r >= 2000)
                {
                    const auto p = r - 2000;
                    m.gesture[(size_t) p] = dr::gNone;
                    if (p == dr::pSilence) m.silence = dr::gNone;
                    if (m.hero == p) { m.hero = dr::pNone; m.heroGesture = dr::gNone; }
                }
                else if (r >= 1000)
                {
                    const auto& g = dr::gesture (r - 1000);
                    m.gesture[(size_t) g.player] = (uint8_t) g.id;
                    if (g.player == dr::pSilence)
                    {
                        m.silence = (uint8_t) g.id;
                        const auto beats = g.id == dr::gSiSnap ? 0.5f : g.id == dr::gSiKickOnly ? (float) processor.barQuarters.load() : 1.0f;
                        if (g.id == dr::gSiFakeDrop) { m.silenceStartQ = m.atQ + 1.0f; m.silenceEndQ = m.atQ + (float) processor.barQuarters.load(); }
                        else { m.silenceStartQ = m.atQ - beats; m.silenceEndQ = m.atQ; }
                    }
                    if (m.hero == dr::pNone || m.hero == g.player) { m.hero = g.player; m.heroGesture = (uint8_t) g.id; }
                }
                double lo = m.atQ, hi = m.atQ;
                for (int p = 0; p < dr::numPlayers; ++p)
                    if (m.acts (p) && p != dr::pSilence)
                    {
                        const auto w = dr::windowOf (m, p, m.gesture[(size_t) p], processor.barQuarters.load(), processor.bpm.load());
                        lo = juce::jmin (lo, w.first); hi = juce::jmax (hi, w.second);
                    }
                m.startQ = (float) lo; m.endQ = (float) hi;
                d.setMoment (m);
                changed();
            });
        }
        void askName (const juce::String& title, const juce::String& prompt, std::function<void (juce::String)> done)
        {
            nameWindow = std::make_unique<juce::AlertWindow> (title, prompt, juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", "", "Name");
            nameWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            nameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, done] (int r)
            {
                if (r == 1 && nameWindow != nullptr) done (nameWindow->getTextEditorContents ("name").trim());
                nameWindow.reset();
            }));
        }
        void fillProfiles()
        {
            profiles.clear (juce::dontSendNotification);
            const auto names = DirectorHost::profiles();
            for (int i = 0; i < names.size(); ++i) profiles.addItem ("Load: " + names[i], i + 1);
            profiles.addSeparator();
            profiles.addItem ("Save my profile...", 900);
            profiles.addItem ("Export a profile to share...", 901);
            profiles.addItem ("Import a friend's profile...", 902);
        }
        void profileChosen()
        {
            const auto id = profiles.getSelectedId();
            profiles.setSelectedId (0, juce::dontSendNotification);
            auto& d = processor.director;
            const auto names = DirectorHost::profiles();
            if (id >= 1 && id <= names.size()) { d.loadProfile (DirectorHost::profileFolder().getChildFile (names[id - 1] + ".xml")); changed(); return; }
            if (id == 900) { currentPage = pTaste; pageActionClicked(); return; }
            const auto save = id == 901;
            if (id != 901 && id != 902) return;
            chooser = std::make_unique<juce::FileChooser> (save ? "Export the director profile" : "Import a director profile",
                                                           juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Director profile.xml"), "*.xml");
            chooser->launchAsync (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                       : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, save] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File()) return;
                if (save) processor.director.saveProfile (f);
                else
                {
                    processor.director.loadProfile (f);
                    processor.director.saveProfile (DirectorHost::profileFolder().getChildFile (f.getFileName()));
                    fillProfiles();
                    changed();
                }
            });
        }
    };
}
