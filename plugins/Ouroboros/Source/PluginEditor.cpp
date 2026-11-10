#include "PluginEditor.h"
#include "SceneUI.h"

using namespace suite::ui;
using suite::SectionType;
using suite::ui::sectionColour;

namespace
{
    constexpr int numTypes = (int) SectionType::count;
    const juce::Colour accent = Colours::ember;
}

//==============================================================================
Timeline::Timeline (OuroborosProcessor& p) : processor (p) {}

float Timeline::xForBar (double bar) const
{
    const auto total = juce::jmax (1, processor.getMap().totalBars);
    return (float) ((bar - 1.0) / total) * (float) getWidth();
}

double Timeline::barForX (float x) const
{
    const auto total = juce::jmax (1, processor.getMap().totalBars);
    return 1.0 + (double) x / juce::jmax (1, getWidth()) * total;
}

int Timeline::boundaryNear (float x) const
{
    const auto m = processor.getMap();
    for (int i = 1; i < m.count; ++i)
        if (std::abs (xForBar (m.sections[(size_t) i].startBar) - x) < 6.0f)
            return i;
    return -1;
}

void Timeline::paint (juce::Graphics& g)
{
    const auto m = preview.has_value() ? *preview : processor.getMap();
    auto r = getLocalBounds().toFloat();
    auto ruler = r.removeFromBottom (18.0f);
    g.setColour (Colours::background);
    g.fillRoundedRectangle (r, 6.0f);

    for (int i = 0; i < m.count; ++i)
    {
        const auto& s = m.sections[(size_t) i];
        const auto x0 = xForBar (s.startBar), x1 = xForBar (m.endBar (i));
        const auto block = juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()).reduced (1.0f, 4.0f);
        const auto c = sectionColour (s.type);
        const auto isSel = i == selected && ! preview.has_value();
        g.setColour (c.withAlpha (isSel ? 0.55f : 0.32f));
        g.fillRoundedRectangle (block, 4.0f);
        g.setColour (isSel ? Colours::text : c);
        if (preview.has_value())
        {
            juce::Path outline;
            outline.addRoundedRectangle (block, 4.0f);
            juce::Path dashed;
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
            g.fillPath (dashed);
        }
        else
        {
            g.drawRoundedRectangle (block, 4.0f, isSel ? 2.0f : 1.0f);
        }

        if (block.getWidth() > 34.0f)
        {
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawFittedText (juce::String (suite::sectionName (s.type)).toUpperCase(), block.reduced (4.0f, 6.0f).toNearestInt(),
                              juce::Justification::centredTop, 1, 0.6f);
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (juce::String (m.endBar (i) - s.startBar) + " bars", block.reduced (4.0f, 6.0f).toNearestInt(),
                              juce::Justification::centredBottom, 1, 0.6f);
        }
    }

    if (preview.has_value())
    {
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("PROPOSAL FROM LISTENING: Apply or Discard", r.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
    }

    // Ruler: a tick every 8 bars, labels every 32.
    g.setFont (juce::FontOptions (10.0f));
    for (int bar = 1; bar <= m.totalBars + 1; bar += 8)
    {
        const auto x = xForBar (bar);
        g.setColour (Colours::outline.brighter (0.2f));
        g.drawVerticalLine ((int) x, ruler.getY(), ruler.getY() + ((bar - 1) % 32 == 0 ? 8.0f : 4.0f));
        if ((bar - 1) % 32 == 0)
        {
            g.setColour (Colours::dimText);
            g.drawText (juce::String (bar), juce::Rectangle<float> (x + 2.0f, ruler.getY() + 4.0f, 40.0f, 12.0f), juce::Justification::left);
        }
    }

    // Playhead
    const auto bar = processor.ppq.load() / processor.barQuarters.load() + 1.0;
    if (bar >= 1.0 && bar <= m.totalBars + 1)
    {
        const auto x = xForBar (bar);
        g.setColour (Colours::text);
        g.drawLine (x, r.getY(), x, r.getBottom(), 2.0f);
    }
}

void Timeline::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (boundaryNear ((float) e.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
}

void Timeline::mouseDown (const juce::MouseEvent& e)
{
    if (preview.has_value()) return;   // no editing while a proposal is shown
    const auto m = processor.getMap();
    dragging = boundaryNear ((float) e.x);
    if (dragging >= 0)
        return;

    const auto index = arrangement::sectionAt (m, (int) std::floor (barForX ((float) e.x)));
    if (index < 0)
        return;
    selected = index;
    if (onSelectionChanged) onSelectionChanged();
    repaint();

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        for (int t = 0; t < numTypes; ++t)
            menu.addItem (t + 1, juce::String ("Make it ") + suite::sectionName ((SectionType) t));
        menu.addSeparator();
        menu.addItem (100, "Delete section", m.count > 1);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this, index] (int result)
                            {
                                auto map = processor.getMap();
                                if (result == 100) arrangement::remove (map, index);
                                else if (result >= 1 && result <= numTypes) map.sections[(size_t) index].type = (SectionType) (result - 1);
                                else return;
                                processor.setMap (map);
                                selected = juce::jmin (selected, map.count - 1);
                                if (onSelectionChanged) onSelectionChanged();
                                repaint();
                            });
    }
}

void Timeline::mouseDrag (const juce::MouseEvent& e)
{
    if (preview.has_value()) return;   // no editing while a proposal is shown
    if (dragging < 0)
        return;
    auto m = processor.getMap();
    const auto snap = OuroborosProcessor::snapBars ((int) processor.apvts.getRawParameterValue (OuroborosProcessor::snapId)->load());
    arrangement::moveStart (m, dragging, arrangement::snapBar (barForX ((float) e.x), e.mods.isShiftDown() ? 1 : snap));
    processor.setMap (m);
    repaint();
}

void Timeline::mouseUp (const juce::MouseEvent&) { dragging = -1; }

void Timeline::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (preview.has_value()) return;   // no editing while a proposal is shown
    // Split the section here: the new part takes the next type in the usual order.
    auto m = processor.getMap();
    const auto snap = OuroborosProcessor::snapBars ((int) processor.apvts.getRawParameterValue (OuroborosProcessor::snapId)->load());
    const auto bar = arrangement::snapBar (barForX ((float) e.x), snap);
    const auto index = arrangement::sectionAt (m, bar);
    if (index < 0 || bar <= m.sections[(size_t) index].startBar)
        return;
    const auto nextType = (SectionType) (((int) m.sections[(size_t) index].type + 1) % numTypes);
    selected = arrangement::insertAt (m, bar, nextType);
    processor.setMap (m);
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

//==============================================================================
ArrangePanel::ArrangePanel (arrangement::GenSettings initial, double tempo, std::function<void (arrangement::GenSettings)> onGenerate)
    : bpm (tempo), generate (std::move (onGenerate))
{
    auto caption = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (l);
    };
    caption (barsLabel, "Length (bars)");
    caption (dropsLabel, "Drops");
    caption (introLabel, "Intro and outro");
    caption (breakdownLabel, "First breakdown");
    duration.setFont (juce::FontOptions (12.0f));
    duration.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (duration);

    bars.setRange (32, 400, 8);
    bars.setValue (initial.totalBars, juce::dontSendNotification);
    bars.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    bars.onValueChange = [this] { updateDuration(); };
    drops.addItemList ({ "1", "2", "3" }, 1);
    drops.setSelectedId (juce::jlimit (1, 3, initial.drops), juce::dontSendNotification);
    intro.addItemList ({ "DJ-friendly (32 bars)", "Short (16 bars)" }, 1);
    intro.setSelectedId (initial.djIntro ? 1 : 2, juce::dontSendNotification);
    breakdown.addItemList ({ "Long (32 + 16-bar build)", "Short (16 + 8-bar build)" }, 1);
    breakdown.setSelectedId (initial.longBreakdown ? 1 : 2, juce::dontSendNotification);
    go.onClick = [this] { if (generate) generate (current()); };
    for (auto* c : std::initializer_list<juce::Component*> { &bars, &drops, &intro, &breakdown, &go })
        addAndMakeVisible (c);
    updateDuration();
    setSize (420, 230);
}

arrangement::GenSettings ArrangePanel::current() const
{
    arrangement::GenSettings g;
    g.totalBars = (int) bars.getValue();
    g.drops = drops.getSelectedId();
    g.djIntro = intro.getSelectedId() == 1;
    g.longBreakdown = breakdown.getSelectedId() == 1;
    return g;
}

void ArrangePanel::updateDuration()
{
    const auto seconds = bars.getValue() * 4.0 * 60.0 / juce::jmax (40.0, bpm);
    duration.setText (juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2)
                          + " at " + juce::String (juce::roundToInt (bpm)) + " BPM", juce::dontSendNotification);
}

void ArrangePanel::paint (juce::Graphics& g)
{
    g.fillAll (suite::ui::Colours::panel);
}

void ArrangePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row = [&r] { auto x = r.removeFromTop (30); r.removeFromTop (6); return x; };
    auto a = row();
    barsLabel.setBounds (a.removeFromLeft (130));
    bars.setBounds (a);
    duration.setBounds (row().withTrimmedLeft (130).withHeight (18));
    for (auto [label, box] : { std::pair<juce::Label*, juce::ComboBox*> { &dropsLabel, &drops }, { &introLabel, &intro }, { &breakdownLabel, &breakdown } })
    {
        auto x = row();
        label->setBounds (x.removeFromLeft (130));
        box->setBounds (x);
    }
    go.setBounds (r.removeFromRight (120));
}

//==============================================================================
OuroborosEditor::OuroborosEditor (OuroborosProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      broadcast (p.apvts, OuroborosProcessor::broadcastId, "Broadcast to suite", accent),
      snap (p.apvts, OuroborosProcessor::snapId, "Snap"),
      timeline (p)
{
    setLookAndFeel (&lnf);

    templateBox.setTextWhenNothingSelected ("Start from a template...");
    for (size_t i = 0; i < arrangement::templates().size(); ++i)
        templateBox.addItem (arrangement::templates()[i].name, (int) i + 1);
    templateBox.onChange = [this]
    {
        if (templateBox.getSelectedId() > 0)
        {
            processor.setMap (arrangement::fromTemplate (arrangement::templates()[(size_t) templateBox.getSelectedId() - 1]));
            timeline.selected = 0;
            refreshSelection();
        }
    };

    lengthCaption.setText ("Bars", juce::dontSendNotification);
    lengthCaption.setFont (juce::FontOptions (12.0f));
    lengthCaption.setJustificationType (juce::Justification::centredRight);
    lengthSlider.setRange (8, 1024, 1);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    lengthSlider.onValueChange = [this]
    {
        auto m = processor.getMap();
        if ((int) lengthSlider.getValue() == m.totalBars) return;
        arrangement::setTotalBars (m, (int) lengthSlider.getValue());
        processor.setMap (m);
    };

    for (auto* l : { &nowLabel, &nextLabel, &selectionLabel, &legend })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setFont (juce::FontOptions (13.0f));
    }
    nowLabel.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    legend.setFont (juce::FontOptions (12.0f));
    legend.setJustificationType (juce::Justification::topLeft);
    legend.setText (juce::String::fromUTF8 ("What the suite does: Cause & Effect ramps through every Build and drops on its last bar \xc2\xb7 "
                                            "Kybalion stops ducking in Breakdowns and Builds \xc2\xb7 Antioch throws at the end of every section "
                                            "(each plugin has a Follow Ouroboros switch)."),
                    juce::dontSendNotification);

    for (int t = 0; t < numTypes; ++t)
    {
        const auto type = (SectionType) t;
        auto mark = std::make_unique<juce::TextButton> (juce::String (suite::sectionName (type)).toUpperCase());
        mark->setColour (juce::TextButton::buttonColourId, sectionColour (type).withAlpha (0.35f));
        mark->onClick = [this, type] { processor.markAtPlayhead (type); };
        addAndMakeVisible (*mark);
        markButtons.push_back (std::move (mark));

        auto set = std::make_unique<juce::TextButton> (suite::sectionName (type));
        set->setColour (juce::TextButton::buttonColourId, sectionColour (type).withAlpha (0.25f));
        set->setColour (juce::TextButton::buttonOnColourId, sectionColour (type));
        set->onClick = [this, type]
        {
            auto m = processor.getMap();
            if (juce::isPositiveAndBelow (timeline.selected, m.count))
            {
                m.sections[(size_t) timeline.selected].type = type;
                processor.setMap (m);
            }
        };
        addAndMakeVisible (*set);
        typeButtons.push_back (std::move (set));
    }
    deleteButton.onClick = [this]
    {
        auto m = processor.getMap();
        arrangement::remove (m, timeline.selected);
        processor.setMap (m);
        timeline.selected = juce::jmin (timeline.selected, m.count - 1);
    };
    timeline.onSelectionChanged = [this] { refreshSelection(); };

    listenButton.setClickingTogglesState (true);
    listenButton.setColour (juce::TextButton::buttonOnColourId, accent);
    listenButton.setToggleState (processor.listening.load(), juce::dontSendNotification);
    listenButton.onClick = [this]
    {
        const auto on = listenButton.getToggleState();
        if (on && ! processor.listening.load())
            processor.clearListening();   // a fresh listen each time
        processor.listening.store (on);
    };
    proposeButton.onClick = [this] { propose(); };
    arrangeButton.onClick = [this] { showArrange(); };
    arrangeButton.setTooltip ("Generate a song map from a length and a few choices");
    applyButton.onClick = [this]
    {
        if (timeline.preview.has_value()) processor.setMap (*timeline.preview);
        timeline.preview.reset();
        timeline.selected = 0;
        refreshSelection();
    };
    discardButton.onClick = [this] { timeline.preview.reset(); };
    listenStatus.setFont (juce::FontOptions (12.0f));
    listenStatus.setJustificationType (juce::Justification::centredLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &listenButton, &proposeButton, &applyButton, &discardButton, &listenStatus, &arrangeButton })
        addAndMakeVisible (c);

    for (auto* c : std::initializer_list<juce::Component*> { &templateBox, &lengthSlider, &lengthCaption, &nowLabel, &nextLabel,
                                                             &selectionLabel, &legend, &broadcast, &snap, &timeline, &deleteButton })
        addAndMakeVisible (c);

    setSize (1060, 560);
    refreshSelection();
    timerCallback();
    startTimerHz (20);
}

OuroborosEditor::~OuroborosEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OuroborosEditor::refreshSelection()
{
    const auto m = processor.getMap();
    timeline.selected = juce::jlimit (0, juce::jmax (0, m.count - 1), timeline.selected);
    if (m.count == 0) return;
    const auto& s = m.sections[(size_t) timeline.selected];
    selectionLabel.setText (juce::String (suite::sectionName (s.type)) + ": bars " + juce::String (s.startBar) + "-"
                                + juce::String (m.endBar (timeline.selected) - 1) + "  (" + juce::String (m.endBar (timeline.selected) - s.startBar)
                                + " bars)",
                            juce::dontSendNotification);
    for (int t = 0; t < numTypes; ++t)
        typeButtons[(size_t) t]->setToggleState ((int) s.type == t, juce::dontSendNotification);
    deleteButton.setEnabled (m.count > 1);
}

void OuroborosEditor::showArrange()
{
    {
        const auto m = processor.getMap();
        if (m.totalBars >= 32) lastArrange.totalBars = m.totalBars;
    }
    juce::Component::SafePointer<OuroborosEditor> safe (this);
    auto panel = std::make_unique<ArrangePanel> (lastArrange, processor.bpm.load(), [safe] (arrangement::GenSettings g)
    {
        if (safe == nullptr) return;
        safe->lastArrange = g;
        safe->timeline.preview = arrangement::generate (g);
        safe->timeline.repaint();
    });
    juce::CallOutBox::launchAsynchronously (std::move (panel), arrangeButton.getScreenBounds(), nullptr);
}

void OuroborosEditor::propose()
{
    const auto proposal = processor.proposeFromListening();
    if (proposal.count > 0)
        timeline.preview = proposal;
    else
        listenStatus.setText ("Not enough heard yet: play at least 16 bars with Listen on", juce::dontSendNotification);
}

void OuroborosEditor::timerCallback()
{
    // Listen: when playback stops after a decent listen, propose a map automatically.
    const auto isPlaying = processor.playing.load();
    const auto heard = processor.barsHeard();
    if (wasPlaying && ! isPlaying && processor.listening.load() && heard >= 16 && ! timeline.preview.has_value())
        propose();
    wasPlaying = isPlaying;
    applyButton.setVisible (timeline.preview.has_value());
    discardButton.setVisible (timeline.preview.has_value());
    proposeButton.setEnabled (heard >= 16);
    if (! timeline.preview.has_value())
        listenStatus.setText (processor.listening.load() ? "Listening: heard " + juce::String (heard) + " bars (stop playback to get a proposal)"
                                                         : heard > 0 ? "Heard " + juce::String (heard) + " bars" : "Turn on Listen and play the song",
                              juce::dontSendNotification);
    else
        listenStatus.setText ({}, juce::dontSendNotification);

    const auto m = processor.getMap();
    if ((int) lengthSlider.getValue() != m.totalBars)
        lengthSlider.setValue (m.totalBars, juce::dontSendNotification);

    const auto barQ = processor.barQuarters.load();
    const auto barPos = processor.ppq.load() / barQ + 1.0;
    const auto bar = (int) std::floor (barPos);
    const auto index = arrangement::sectionAt (m, bar);
    if (index >= 0)
    {
        const auto& s = m.sections[(size_t) index];
        nowLabel.setColour (juce::Label::textColourId, sectionColour (s.type));
        nowLabel.setText (juce::String (suite::sectionName (s.type)).toUpperCase() + "   bar " + juce::String (bar)
                              + " (" + juce::String (bar - s.startBar + 1) + " of " + juce::String (m.endBar (index) - s.startBar) + ")",
                          juce::dontSendNotification);
        const auto barsLeft = m.endBar (index) - bar;
        nextLabel.setText (index + 1 < m.count ? juce::String ("Next: ") + suite::sectionName (m.sections[(size_t) index + 1].type)
                                                     + " in " + juce::String (barsLeft) + (barsLeft == 1 ? " bar" : " bars")
                                               : "Last section: song ends in " + juce::String (barsLeft) + " bars",
                           juce::dontSendNotification);
    }
    else
    {
        nowLabel.setColour (juce::Label::textColourId, Colours::dimText);
        nowLabel.setText (bar > m.totalBars ? "After the end" : "Before the start", juce::dontSendNotification);
        nextLabel.setText ({}, juce::dontSendNotification);
    }
    refreshSelection();
    timeline.repaint();
}

void OuroborosEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "OUROBOROS", "arrangement conductor", accent);
    drawPanel (g, nowArea, "Now playing");
    drawPanel (g, songArea, "Song map  (drag edges, double-click to split, right-click for options)", accent);
    drawPanel (g, markArea, "Mark at playhead  (click while the song plays)", Colours::brass);
    drawPanel (g, selectArea, "Selected section", Colours::verdigris);
}

void OuroborosEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto header = bounds.removeFromTop (48);
    broadcast.setBounds (header.removeFromRight (190).reduced (4, 8));

    nowArea = bounds.removeFromTop (84);
    auto n = nowArea.reduced (14, 6).withTrimmedTop (14);
    nowLabel.setBounds (n.removeFromLeft (n.getWidth() / 2));
    nextLabel.setBounds (n);
    bounds.removeFromTop (10);

    songArea = bounds.removeFromTop (210);
    auto s = songArea.reduced (12, 6).withTrimmedTop (16);
    auto bar = s.removeFromTop (30);
    templateBox.setBounds (bar.removeFromLeft (210).reduced (0, 2));
    arrangeButton.setBounds (bar.removeFromLeft (100).reduced (4, 2));
    lengthSlider.setBounds (bar.removeFromRight (120).reduced (0, 2));
    lengthCaption.setBounds (bar.removeFromRight (50));
    bar.removeFromLeft (12);
    listenButton.setBounds (bar.removeFromLeft (80).reduced (0, 2));
    proposeButton.setBounds (bar.removeFromLeft (110).reduced (4, 2));
    auto actions = bar.removeFromRight (170);
    applyButton.setBounds (actions.removeFromLeft (80).reduced (2));
    discardButton.setBounds (actions.reduced (2));
    listenStatus.setBounds (bar.reduced (6, 0));
    s.removeFromTop (8);
    timeline.setBounds (s);
    bounds.removeFromTop (10);

    auto bottom = bounds.removeFromTop (130);
    markArea = bottom.removeFromLeft (bottom.getWidth() / 2 - 5);
    bottom.removeFromLeft (10);
    selectArea = bottom;

    auto ma = markArea.reduced (12, 6).withTrimmedTop (16);
    auto snapCell = ma.removeFromRight (110);
    snap.setBounds (snapCell);
    const auto bw = ma.getWidth() / 3;
    for (int t = 0; t < numTypes; ++t)
    {
        const auto row = t / 3, col = t % 3;
        markButtons[(size_t) t]->setBounds (juce::Rectangle<int> (ma.getX() + col * bw, ma.getY() + row * (ma.getHeight() / 2), bw, ma.getHeight() / 2).reduced (3));
    }

    auto sa = selectArea.reduced (12, 6).withTrimmedTop (16);
    auto info = sa.removeFromTop (24);
    deleteButton.setBounds (info.removeFromRight (130));
    selectionLabel.setBounds (info);
    sa.removeFromTop (6);
    const auto tw = sa.getWidth() / numTypes;
    auto typeRow = sa.removeFromTop (34);
    for (int t = 0; t < numTypes; ++t)
        typeButtons[(size_t) t]->setBounds (typeRow.removeFromLeft (tw).reduced (2));

    bounds.removeFromTop (8);
    legend.setBounds (bounds);
}
