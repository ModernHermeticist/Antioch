#include "PluginEditor.h"
#include "SceneUI.h"
#include "Music.h"

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
    auto m = preview.has_value() ? *preview : processor.getMap();
    m.finalLift = (int) processor.apvts.getRawParameterValue (OuroborosProcessor::liftId)->load();
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
            if (const auto shift = m.keyShiftOf (i); shift != 0)
            {
                g.setColour (Colours::brass);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawFittedText ("KEY +" + juce::String (shift), block.reduced (4.0f, 6.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
            }
        }
    }

    if (preview.has_value())
    {
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("PROPOSAL FROM LISTENING: Apply or Discard", r.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
    }

    // Energy curve: a line per section at its energy, joined with ramps; drag a line to change it.
    {
        juce::Path curve;
        for (int i = 0; i < m.count; ++i)
        {
            const auto x0 = xForBar (m.sections[(size_t) i].startBar) + 4.0f, x1 = xForBar (m.endBar (i)) - 4.0f;
            const auto y = yForEnergy (m.energyOf (i));
            if (i == 0) curve.startNewSubPath (x0, y); else curve.lineTo (x0, y);
            curve.lineTo (x1, y);
            if (m.sections[(size_t) i].energy >= 0)   // drawn by hand: mark it
            {
                g.setColour (Colours::brass);
                g.fillEllipse ((x0 + x1) * 0.5f - 3.0f, y - 3.0f, 6.0f, 6.0f);
            }
        }
        g.setColour (Colours::brass.withAlpha (0.85f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
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

float Timeline::yForEnergy (float energy) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedBottom (18.0f).reduced (0.0f, 22.0f);
    return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, energy);
}

int Timeline::energyLineNear (float x, float y) const
{
    if (preview.has_value()) return -1;
    const auto m = processor.getMap();
    const auto index = arrangement::sectionAt (m, (int) std::floor (barForX (x)));
    return index >= 0 && std::abs (y - yForEnergy (m.energyOf (index))) < 6.0f ? index : -1;
}

void Timeline::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (boundaryNear ((float) e.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                    : energyLineNear ((float) e.x, (float) e.y) >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                                                     : juce::MouseCursor::PointingHandCursor);
}

void Timeline::mouseDown (const juce::MouseEvent& e)
{
    if (preview.has_value()) return;   // no editing while a proposal is shown
    const auto m = processor.getMap();
    dragging = boundaryNear ((float) e.x);
    if (dragging >= 0)
        return;
    energyDragging = e.mods.isPopupMenu() ? -1 : energyLineNear ((float) e.x, (float) e.y);
    if (energyDragging >= 0)
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
        menu.addItem (101, "Energy: back to the usual for " + juce::String (suite::sectionName (m.sections[(size_t) index].type)),
                      m.sections[(size_t) index].energy >= 0);
        menu.addItem (100, "Delete section", m.count > 1);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this, index] (int result)
                            {
                                auto map = processor.getMap();
                                if (result == 100) arrangement::remove (map, index);
                                else if (result == 101) map.sections[(size_t) index].energy = -1;
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
    if (energyDragging >= 0)
    {
        auto m = processor.getMap();
        const auto area = getLocalBounds().toFloat().withTrimmedBottom (18.0f).reduced (0.0f, 22.0f);
        const auto energy = juce::jlimit (0.0f, 1.0f, (area.getBottom() - (float) e.y) / area.getHeight());
        m.sections[(size_t) energyDragging].energy = juce::roundToInt (energy * 20.0f) * 5;   // 5% steps
        processor.setMap (m);
        repaint();
        return;
    }
    if (dragging < 0)
        return;
    auto m = processor.getMap();
    const auto snap = OuroborosProcessor::snapBars ((int) processor.apvts.getRawParameterValue (OuroborosProcessor::snapId)->load());
    arrangement::moveStart (m, dragging, arrangement::snapBar (barForX ((float) e.x), e.mods.isShiftDown() ? 1 : snap));
    processor.setMap (m);
    repaint();
}

void Timeline::mouseUp (const juce::MouseEvent&) { dragging = -1; energyDragging = -1; }

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
void SuitePanel::refresh()
{
    entries = suite::Registry::list();
    std::sort (entries.begin(), entries.end(), [] (const auto& a, const auto& b)
               { return a.plugin != b.plugin ? a.plugin < b.plugin : a.track < b.track; });
    advice = suitecheck::check (entries);
    repaint();
}

void SuitePanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    auto r = getLocalBounds().reduced (14, 10);
    g.setColour (accent);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("SUITE: " + juce::String ((int) entries.size()) + " PLUGINS LOADED", r.removeFromTop (22), juce::Justification::centredLeft);

    // Two columns of plugin / track.
    const auto listHeight = juce::jmin (240, ((int) entries.size() + 1) / 2 * 20 + 6);
    auto list = r.removeFromTop (listHeight);
    const auto colW = list.getWidth() / 2;
    g.setFont (juce::FontOptions (13.0f));
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        auto row = juce::Rectangle<int> (list.getX() + (int) (i % 2) * colW, list.getY() + (int) (i / 2) * 20, colW - 8, 20);
        if (row.getBottom() > list.getBottom()) break;
        g.setColour (Colours::text);
        g.drawText (e.plugin, row.removeFromLeft (130), juce::Justification::centredLeft);
        g.setColour (Colours::dimText);
        g.drawText (e.track.isNotEmpty() ? e.track : juce::String ("(track name unknown)"), row, juce::Justification::centredLeft);
    }

    r.removeFromTop (8);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
    r.removeFromTop (8);
    g.setFont (juce::FontOptions (13.0f));
    for (const auto& a : advice)
    {
        auto row = r.removeFromTop (36);
        if (row.getHeight() < 20) break;
        g.setColour (a.problem ? Colours::ember : Colours::moss);
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getY() + 6.0f, 6.0f, 6.0f);
        g.setColour (Colours::text);
        g.drawFittedText (a.text, row.withTrimmedLeft (14), juce::Justification::topLeft, 2, 0.9f);
    }
}

//==============================================================================
RecipePanel::RecipePanel()
{
    names.setEditableText (true);
    names.setTextWhenNothingSelected ("Recipe name (type a new one to save)");
    const auto existing = suite::Recipes::list();
    for (int i = 0; i < existing.size(); ++i) names.addItem (existing[i], i + 1);
    caption.setText ("Every suite plugin saves or loads its own settings, matched by track name. "
                     "Recipes live in " + suite::Recipes::folder().getFullPathName(), juce::dontSendNotification);
    caption.setFont (juce::FontOptions (12.0f));
    caption.setColour (juce::Label::textColourId, Colours::dimText);
    caption.setJustificationType (juce::Justification::topLeft);
    result.setFont (juce::FontOptions (13.0f));
    result.setColour (juce::Label::textColourId, Colours::moss);
    save.onClick = [this] { send (suite::RecipeAction::Save); };
    load.onClick = [this] { send (suite::RecipeAction::Load); };
    for (auto* c : std::initializer_list<juce::Component*> { &names, &save, &load, &caption, &result })
        addAndMakeVisible (c);
    setSize (460, 190);
    startTimerHz (4);
}

void RecipePanel::send (suite::RecipeAction action)
{
    const auto name = names.getText().trim();
    if (name.isEmpty()) { result.setText ("Type a name first", juce::dontSendNotification); return; }
    sentSeq = suite::RecipeBus::send (action, name);
    sentWhat = (action == suite::RecipeAction::Save ? "Saved '" : "Loaded '") + name + "' in ";
    timerCallback();
}

void RecipePanel::timerCallback()
{
    if (sentSeq == 0) return;
    const auto n = suite::RecipeBus::acknowledgements (sentSeq);
    result.setText (sentWhat + juce::String (n) + (n == 1 ? " plugin" : " plugins"), juce::dontSendNotification);
}

void RecipePanel::paint (juce::Graphics& g) { g.fillAll (Colours::panel); }

void RecipePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row = r.removeFromTop (30);
    load.setBounds (row.removeFromRight (70).reduced (2, 0));
    save.setBounds (row.removeFromRight (70).reduced (2, 0));
    names.setBounds (row.withTrimmedRight (6));
    r.removeFromTop (8);
    result.setBounds (r.removeFromBottom (22));
    caption.setBounds (r);
}

//==============================================================================
SessionPanel::SessionPanel (OuroborosProcessor& p) : processor (p)
{
    for (const auto& s : suite::sessionStyles()) style.addItem (s.name, style.getNumItems() + 1);
    style.setSelectedId (1, juce::dontSendNotification);
    for (int n = 0; n < 12; ++n) root.addItem (suite::music::noteNames()[n], n + 1);
    root.setSelectedId (10, juce::dontSendNotification);   // A
    for (const auto& sc : suite::music::scales()) scale.addItem (sc.name, scale.getNumItems() + 1);
    scale.setSelectedId (1, juce::dontSendNotification);
    style.onChange = [this] { updateInfo(); };
    for (auto* l : { &styleLabel, &keyLabel, &info, &result })
    {
        l->setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (l);
    }
    styleLabel.setText ("Style", juce::dontSendNotification);
    keyLabel.setText ("Key", juce::dontSendNotification);
    info.setJustificationType (juce::Justification::topLeft);
    info.setColour (juce::Label::textColourId, Colours::dimText);
    result.setColour (juce::Label::textColourId, Colours::moss);
    go.onClick = [this]
    {
        sentSeq = processor.startSession (style.getSelectedId() - 1, root.getSelectedId() - 1, scale.getSelectedId() - 1);
        timerCallback();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &style, &root, &scale, &go })
        addAndMakeVisible (c);
    updateInfo();
    setSize (460, 250);
    startTimerHz (4);
}

void SessionPanel::updateInfo()
{
    const auto& s = suite::sessionStyles()[(size_t) juce::jmax (0, style.getSelectedId() - 1)];
    const auto bars = suite::sessionBars (s);
    const auto seconds = bars * 4.0 * 60.0 / s.bpm;
    info.setText (juce::String (bars) + " bars (" + juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2)
                      + "), " + juce::String (s.drops) + (s.drops == 1 ? " drop" : " drops")
                      + (s.finalLift > 0 ? ", key up for the last drop" : "") + ".\nSet FL's tempo to " + juce::String (juce::roundToInt (s.bpm))
                      + " BPM. This replaces the song map and the other plugins' key, progression, loudness target and scenes.",
                  juce::dontSendNotification);
}

void SessionPanel::timerCallback()
{
    if (sentSeq == 0) return;
    const auto n = suite::SessionBus::acknowledgements (sentSeq);
    result.setText ("Song map ready; " + juce::String (n) + (n == 1 ? " other plugin" : " other plugins") + " set up.", juce::dontSendNotification);
}

void SessionPanel::paint (juce::Graphics& g) { g.fillAll (Colours::panel); }

void SessionPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row1 = r.removeFromTop (28);
    styleLabel.setBounds (row1.removeFromLeft (60));
    style.setBounds (row1);
    r.removeFromTop (8);
    auto row2 = r.removeFromTop (28);
    keyLabel.setBounds (row2.removeFromLeft (60));
    root.setBounds (row2.removeFromLeft (90).reduced (0, 0));
    row2.removeFromLeft (8);
    scale.setBounds (row2);
    r.removeFromTop (8);
    result.setBounds (r.removeFromBottom (22));
    auto bottom = r.removeFromBottom (32);
    go.setBounds (bottom.removeFromRight (160));
    info.setBounds (r);
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
      lift (p.apvts, OuroborosProcessor::liftId, "Final drop key"),
      finalSnap (p.apvts, OuroborosProcessor::finalSnapId, "Final drop snapshot"),
      dropGap (p.apvts, OuroborosProcessor::dropGapId, "Before every drop"),
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

    for (int s = 0; s < (int) slotButtons.size(); ++s)
    {
        auto& b = slotButtons[(size_t) s];
        b.setButtonText (suite::SuiteProcessor::slotName (s));
        b.setTooltip ("Snapshot " + suite::SuiteProcessor::slotName (s) + ": click to recall every suite plugin's settings "
                      "(or to choose this empty slot), right-click to clear");
        b.setColour (juce::TextButton::buttonOnColourId, accent);
        b.onClick = [this, s]
        {
            if (slotButtons[(size_t) s].rightClick)
            {
                snapshotCommand (suite::SnapshotAction::Clear, s);
                return;
            }
            currentSlot = s;
            if (processor.hasSnapshot (s))
                snapshotCommand (suite::SnapshotAction::Recall, s);
            refreshSnapshots();
        };
        addAndMakeVisible (b);
    }
    saveSnapshotButton.setTooltip ("Save the current settings of every suite plugin into the chosen slot");
    saveSnapshotButton.onClick = [this] { snapshotCommand (suite::SnapshotAction::Capture, currentSlot); };
    snapshotStatus.setFont (juce::FontOptions (12.0f));
    snapshotStatus.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (saveSnapshotButton);
    addAndMakeVisible (snapshotStatus);
    arrangeButton.onClick = [this] { showArrange(); };
    referenceButton.setTooltip ("Copy the structure of a reference track: choose an audio file (and its tempo); "
                                "its sections are proposed as a map to Apply or Discard");
    referenceButton.onClick = [this] { chooseReference(); };
    addAndMakeVisible (referenceButton);
    sessionButton.setTooltip ("Start a new song: a style and a key set up the map, key, progression, loudness target and scenes across the suite");
    sessionButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<SessionPanel> (processor), sessionButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (sessionButton);
    recipesButton.setTooltip ("Save the whole suite's settings as a named recipe, or load one, in any project");
    recipesButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<RecipePanel>(), recipesButton.getScreenBounds(), nullptr);
    };
    versionsButton.setTooltip ("Keeps this song in snapshot A and makes a radio edit in B and an extended mix in C "
                               "(map and loudness target); then click the slots to compare");
    versionsButton.onClick = [this] { versionsSeq = processor.makeVersions(); lastSnapshotSeq = versionsSeq; lastSnapshotVerb = "Made A/B/C (back on A)"; refreshSnapshots(); };
    addAndMakeVisible (recipesButton);
    addAndMakeVisible (versionsButton);
    suiteButton.setTooltip ("Every suite plugin loaded, on which track, and what still needs setting up");
    suiteButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<SuitePanel>(), suiteButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (suiteButton);
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
                                                             &selectionLabel, &legend, &broadcast, &snap, &lift, &finalSnap, &dropGap, &timeline, &deleteButton })
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

void OuroborosEditor::snapshotCommand (suite::SnapshotAction action, int slot)
{
    lastSnapshotSeq = suite::SnapshotBus::send (action, slot);
    lastSnapshotVerb = action == suite::SnapshotAction::Capture ? "Saved" : action == suite::SnapshotAction::Recall ? "Recalled" : "Cleared";
    lastSnapshotVerb << " " << suite::SuiteProcessor::slotName (slot);
    processor.pollSnapshots();   // this plugin answers straight away
    refreshSnapshots();
}

void OuroborosEditor::refreshSnapshots()
{
    for (int s = 0; s < (int) slotButtons.size(); ++s)
    {
        auto& b = slotButtons[(size_t) s];
        const auto filled = processor.hasSnapshot (s);
        b.setToggleState (s == currentSlot, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, filled ? accent.withAlpha (0.35f) : Colours::panel);
        b.setColour (juce::TextButton::textColourOffId, filled ? Colours::text : Colours::dimText);
    }
    saveSnapshotButton.setButtonText ("Save " + suite::SuiteProcessor::slotName (currentSlot));
    if (lastSnapshotSeq != 0)
    {
        const auto n = suite::SnapshotBus::acknowledgements (lastSnapshotSeq);
        snapshotStatus.setText (lastSnapshotVerb + " in " + juce::String (n) + (n == 1 ? " plugin" : " plugins"), juce::dontSendNotification);
    }
    else
        snapshotStatus.setText ("Suite snapshots", juce::dontSendNotification);
}

void OuroborosEditor::chooseReference()
{
    // Tempo first (defaults to the project's), then the file.
    juce::PopupMenu menu;
    const auto projectBpm = processor.bpm.load();
    menu.addSectionHeader ("Reference tempo");
    menu.addItem (1, "Same as the project (" + juce::String (projectBpm, 1) + " BPM)");
    const int presets[] = { 128, 130, 132, 134, 136, 138, 140, 142 };
    for (int i = 0; i < 8; ++i)
        menu.addItem (10 + i, juce::String (presets[i]) + " BPM");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (referenceButton), [this, projectBpm, presets] (int result)
    {
        if (result == 0) return;
        referenceBpm = result == 1 ? projectBpm : (double) presets[result - 10];
        referenceChooser = std::make_unique<juce::FileChooser> ("Choose a reference track",
                                                                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                                "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        referenceChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [this] (const juce::FileChooser& fc)
                                       {
                                           if (fc.getResult().existsAsFile())
                                               processor.analyseReference (fc.getResult(), referenceBpm);
                                       });
    });
}

void OuroborosEditor::propose()
{
    const auto proposal = processor.proposeFromListening();
    if (proposal.count > 0)
    {
        timeline.preview = proposal;
        previewNote = {};
    }
    else
        listenStatus.setText ("Not enough heard yet: play at least 16 bars with Listen on", juce::dontSendNotification);
}

void OuroborosEditor::timerCallback()
{
    refreshSnapshots();
    if (++suiteTicks % 10 == 0)
    {
        const auto entries = suite::Registry::list();
        const auto advice = suitecheck::check (entries);
        const auto problems = (int) std::count_if (advice.begin(), advice.end(), [] (const auto& a) { return a.problem; });
        suiteButton.setButtonText ("Suite (" + juce::String ((int) entries.size()) + ")" + (problems > 0 ? juce::String (" !") : juce::String()));
        suiteButton.setColour (juce::TextButton::textColourOffId, problems > 0 ? Colours::ember : Colours::text);
    }
    // Listen: when playback stops after a decent listen, propose a map automatically.
    const auto isPlaying = processor.playing.load();
    const auto heard = processor.barsHeard();
    if (wasPlaying && ! isPlaying && processor.listening.load() && heard >= 16 && ! timeline.preview.has_value())
        propose();
    wasPlaying = isPlaying;
    applyButton.setVisible (timeline.preview.has_value());
    listenStatus.setBounds (timeline.preview.has_value() ? statusNarrow : statusWide);
    discardButton.setVisible (timeline.preview.has_value());
    proposeButton.setEnabled (heard >= 16);
    if (! timeline.preview.has_value())
        listenStatus.setText (processor.listening.load() ? "Listening: heard " + juce::String (heard) + " bars (stop playback to get a proposal)"
                                                         : heard > 0 ? "Heard " + juce::String (heard) + " bars" : "Turn on Listen and play the song",
                              juce::dontSendNotification);
    else
        listenStatus.setText (previewNote, juce::dontSendNotification);
    if (processor.analysingReference.load())
        listenStatus.setText ("Analysing the reference...", juce::dontSendNotification);
    if (auto proposal = processor.takeReferenceProposal())
    {
        if (proposal->count > 0)
        {
            timeline.preview = *proposal;
            previewNote = "From '" + processor.referenceName() + "' at " + juce::String (juce::roundToInt (referenceBpm)) + " BPM";
        }
        else
            listenStatus.setText ("Couldn't find a structure in that file (is the tempo right?)", juce::dontSendNotification);
    }

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
    drawPanel (g, songArea, "Song map  (drag edges or the energy line, double-click to split, right-click for options)", accent);
    drawPanel (g, markArea, "Mark at playhead  (click while the song plays)", Colours::brass);
    drawPanel (g, selectArea, "Selected section", Colours::verdigris);
}

void OuroborosEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto header = bounds.removeFromTop (48);
    broadcast.setBounds (header.removeFromRight (190).reduced (4, 8));
    header.removeFromRight (12);
    saveSnapshotButton.setBounds (header.removeFromRight (80).reduced (2, 8));
    for (int s = (int) slotButtons.size() - 1; s >= 0; --s)
        slotButtons[(size_t) s].setBounds (header.removeFromRight (38).reduced (2, 8));
    snapshotStatus.setBounds (header.removeFromRight (170).reduced (4, 8));
    suiteButton.setBounds (header.removeFromRight (110).reduced (4, 8));

    nowArea = bounds.removeFromTop (84);
    auto n = nowArea.reduced (14, 6).withTrimmedTop (14);
    dropGap.setBounds (n.removeFromRight (190).withTrimmedTop (-10));
    sessionButton.setBounds (n.removeFromRight (122).withSizeKeepingCentre (116, 30));
    versionsButton.setBounds (n.removeFromRight (132).withSizeKeepingCentre (126, 30));
    recipesButton.setBounds (n.removeFromRight (106).withSizeKeepingCentre (100, 30));
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
    referenceButton.setBounds (bar.removeFromLeft (100).reduced (4, 2));
    listenButton.setBounds (bar.removeFromLeft (80).reduced (0, 2));
    proposeButton.setBounds (bar.removeFromLeft (110).reduced (4, 2));
    statusWide = bar.reduced (6, 0);
    auto actions = bar.removeFromRight (170);
    applyButton.setBounds (actions.removeFromLeft (80).reduced (2));
    discardButton.setBounds (actions.reduced (2));
    statusNarrow = bar.reduced (6, 0);
    listenStatus.setBounds (timeline.preview.has_value() ? statusNarrow : statusWide);
    s.removeFromTop (8);
    timeline.setBounds (s);
    bounds.removeFromTop (10);

    auto bottom = bounds.removeFromTop (130);
    markArea = bottom.removeFromLeft (bottom.getWidth() * 58 / 100);
    bottom.removeFromLeft (10);
    selectArea = bottom;

    auto ma = markArea.reduced (12, 6).withTrimmedTop (16);
    {
        auto column = ma.removeFromRight (150);
        lift.setBounds (column.removeFromTop (column.getHeight() / 2));
        finalSnap.setBounds (column);
    }
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
    const auto tw = sa.getWidth() / 3, th = juce::jmin (34, sa.getHeight() / 2);
    for (int t = 0; t < numTypes; ++t)
        typeButtons[(size_t) t]->setBounds (juce::Rectangle<int> (sa.getX() + (t % 3) * tw, sa.getY() + (t / 3) * th, tw, th).reduced (2));

    bounds.removeFromTop (8);
    legend.setBounds (bounds);
}
