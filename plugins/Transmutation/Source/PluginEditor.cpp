#include "PluginEditor.h"

using namespace suite::ui;

namespace
{
    juce::Colour categoryColour (int c)
    {
        static const juce::Colour cols[] = { Colours::ember, Colours::brass, Colours::verdigris, Colours::amethyst, Colours::moss,
                                             juce::Colour (0xffe85aa8), Colours::dimText, juce::Colour (0xff5a9be8), juce::Colour (0xffd8c27d), juce::Colour (0xff9be85a) };
        return cols[juce::jlimit (0, trm::numCategories - 1, c)];
    }
    juce::String barText (float q) { return "bar " + juce::String ((int) std::floor (q / 4.0f) + 1); }
    juce::String roleName (int r)
    {
        static const char* n[] = { "unknown role", "kick", "bass", "hats & percussion", "pads & chords", "lead", "vocal", "FX" };
        return n[juce::jlimit (0, 7, r)];
    }
}

//==============================================================================
double PlanTimeline::totalQ() const
{
    double t = haveMap && map.totalBars > 0 ? map.totalBars * 4.0 : 64.0 * 4.0;
    if (! haveMap)
        for (const auto& m : plan.moves) if ((m.slot & trm::sSong) == 0) t = juce::jmax (t, (double) m.endQ);
    return juce::jmax (16.0, t);
}

void PlanTimeline::refresh()
{
    plan = processor.getPlan();
    const auto in = processor.lastInput();
    map = in.map; haveMap = in.haveMap;
    pos = processor.songPos.load();
    // lanes: greedy, so moves never overlap on screen (whole-song moves are listed, not drawn)
    lanes.assign (plan.moves.size(), -1);
    std::vector<double> laneEnd;
    for (size_t i = 0; i < plan.moves.size(); ++i)
    {
        const auto& m = plan.moves[i];
        if (m.slot & trm::sSong) continue;
        int lane = 0;
        while (lane < (int) laneEnd.size() && laneEnd[(size_t) lane] > m.startQ + 1.0e-3) ++lane;
        if (lane == (int) laneEnd.size()) laneEnd.push_back (0.0);
        laneEnd[(size_t) lane] = m.endQ;
        lanes[i] = lane;
    }
    laneCount = juce::jmax (1, (int) laneEnd.size());
    if (selected >= (int) plan.moves.size()) selected = -1;
    repaint();
}

juce::Rectangle<float> PlanTimeline::rectFor (int index) const
{
    const auto& m = plan.moves[(size_t) index];
    const auto area = getLocalBounds().toFloat().withTrimmedTop (26.0f).reduced (2.0f);
    const auto laneH = juce::jmin (16.0f, area.getHeight() / (float) laneCount);
    const auto x0 = area.getX() + area.getWidth() * (float) (m.startQ / totalQ());
    const auto x1 = area.getX() + area.getWidth() * (float) (m.endQ / totalQ());
    return { x0, area.getY() + laneH * (float) lanes[(size_t) index], juce::jmax (3.0f, x1 - x0), laneH - 2.0f };
}

void PlanTimeline::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (bounds, 6.0f);
    const auto total = totalQ();
    auto strip = bounds.withHeight (22.0f).reduced (2.0f);
    if (haveMap)
    {
        for (int i = 0; i < map.count; ++i)
        {
            const auto s = (map.sections[(size_t) i].startBar - 1) * 4.0, e = (map.endBar (i) - 1) * 4.0;
            const auto r = juce::Rectangle<float> (strip.getX() + strip.getWidth() * (float) (s / total), strip.getY(),
                                                   strip.getWidth() * (float) ((e - s) / total), strip.getHeight()).reduced (1.0f, 0.0f);
            const auto type = map.sections[(size_t) i].type;
            const auto col = type == suite::SectionType::Drop ? Colours::ember : type == suite::SectionType::Build ? Colours::brass
                           : type == suite::SectionType::Breakdown ? Colours::amethyst : Colours::outline;
            g.setColour (col.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (suite::sectionName (type), r.reduced (3.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }
    else
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("No Ouroboros map: moves at every 8-bar phrase", strip, juce::Justification::centredLeft);
    }
    for (int i = 0; i < (int) plan.moves.size(); ++i)
    {
        if (lanes[(size_t) i] < 0) continue;
        const auto& m = plan.moves[(size_t) i];
        const auto r = rectFor (i);
        const auto& e = trm::effect (m.effect);
        g.setColour (categoryColour (e.category).withAlpha (i == selected ? 1.0f : 0.75f));
        g.fillRoundedRectangle (r, 2.5f);
        if (m.flags & trm::mfLocked) { g.setColour (Colours::text); g.drawRoundedRectangle (r, 2.5f, 1.5f); }
        if (m.flags & trm::mfMotif) { g.setColour (Colours::text); g.fillEllipse (r.getX() + 1.5f, r.getY() + 1.5f, 3.0f, 3.0f); }
        if (r.getWidth() > 40.0f)
        {
            g.setColour (Colours::background);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (e.name, r.reduced (3.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }
    if (processor.playingNow.load() && pos > 0.0)
    {
        const auto x = bounds.getX() + 2.0f + (bounds.getWidth() - 4.0f) * (float) (pos / total);
        g.setColour (Colours::text);
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
    }
}

void PlanTimeline::mouseDown (const juce::MouseEvent& ev)
{
    selected = -1;
    for (int i = 0; i < (int) plan.moves.size(); ++i)
        if (lanes[(size_t) i] >= 0 && rectFor (i).expanded (1.0f).contains (ev.position)) selected = i;
    repaint();
    if (onSelect) onSelect (selected);
}

//==============================================================================
EffectList::EffectList (TransmutationProcessor& p) : processor (p)
{
    list.setModel (this);
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, Colours::panel);
    addAndMakeVisible (list);
    setCategory (-1);
}

void EffectList::resized() { list.setBounds (getLocalBounds()); }

void EffectList::setCategory (int c)
{
    rows.clear();
    for (int cat = 0; cat < trm::numCategories; ++cat)
        for (const auto& e : trm::effects())
            if (e.category == cat && (c < 0 || c == cat)) rows.push_back (e.id);
    list.updateContent();
    list.repaint();
}

void EffectList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& e = trm::effect (rows[(size_t) row]);
    if (selected) g.fillAll (Colours::outline);
    g.setColour (categoryColour (e.category));
    g.fillRect (2, 4, 4, height - 8);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Colours::dimText);
    g.drawText (juce::String (e.id), 10, 0, 30, height, juce::Justification::centredLeft);
    g.setColour (Colours::text);
    g.drawText (e.name, 40, 0, width - 120, height, juce::Justification::centredLeft, true);
    const auto mode = processor.modeOf (e.id);
    g.setColour (mode == trm::mAlways ? Colours::moss : mode == trm::mNever ? Colours::ember : Colours::dimText);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (mode == trm::mAlways ? "Always" : mode == trm::mNever ? "Never" : "Auto", width - 76, 0, 70, height, juce::Justification::centredRight);
}

void EffectList::listBoxItemClicked (int row, const juce::MouseEvent& ev)
{
    if (row < 0 || row >= (int) rows.size() || ev.x < getWidth() - 90) return;
    const auto id = rows[(size_t) row];
    processor.setMode (id, (uint8_t) ((processor.modeOf (id) + 1) % 3));
    list.repaintRow (row);
}

void EffectList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size() && onAudition) onAudition (rows[(size_t) row]);
}

juce::String EffectList::getTooltipForRow (int row)
{
    if (row < 0 || row >= (int) rows.size()) return {};
    const auto& e = trm::effect (rows[(size_t) row]);
    return juce::String (e.what) + ". (Click the right-hand word: Auto / Always / Never. Double-click: play it now.)";
}

//==============================================================================
TransmutationEditor::TransmutationEditor (TransmutationProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      spice (p.apvts, TParams::spice, "Spice", Colours::ember),
      builds (p.apvts, TParams::builds, "Builds", Colours::brass),
      drops (p.apvts, TParams::drops, "Drops", Colours::ember),
      amount (p.apvts, TParams::amount, "Amount", Colours::verdigris),
      output (p.apvts, TParams::output, "Output", Colours::text),
      genre (p.apvts, TParams::genre, "Genre"),
      role (p.apvts, TParams::role, "Role"),
      director (p.apvts, TParams::direct, "Director plans the moves", Colours::ember),
      timeline (p), effectList (p)
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<juce::Component*> { &spice, &builds, &drops, &amount, &output, &genre, &role, &director, &status, &nowPlaying,
                                                             &timeline, &moveInfo, &lockButton, &rerollButton, &neverButton, &playButton, &rerollAllButton,
                                                             &category, &effectList, &autopilot })
        addAndMakeVisible (c);
    for (auto* l : { &status, &nowPlaying, &moveInfo })
    {
        l->setFont (juce::FontOptions (12.5f));
        l->setJustificationType (juce::Justification::topLeft);
        l->setColour (juce::Label::textColourId, Colours::text);
    }
    category.addItem ("All 118 moves", 1);
    for (int c = 0; c < trm::numCategories; ++c) category.addItem (trm::categoryName (c), c + 2);
    category.setSelectedId (1, juce::dontSendNotification);
    category.onChange = [this] { effectList.setCategory (category.getSelectedId() - 2); };

    timeline.onSelect = [this] (int) { showSelected(); };
    effectList.onAudition = [this] (int id) { processor.audition (id); };
    lockButton.onClick = [this]
    {
        const auto& plan = timeline.shownPlan();
        if (timeline.selected < 0 || timeline.selected >= (int) plan.moves.size()) return;
        const auto m = plan.moves[(size_t) timeline.selected];
        if (processor.isLocked (m.key)) processor.unlockMoment (m.key); else processor.lockMove (m);
        timeline.refresh(); showSelected();
    };
    rerollButton.onClick = [this]
    {
        const auto& plan = timeline.shownPlan();
        if (timeline.selected < 0 || timeline.selected >= (int) plan.moves.size()) return;
        processor.rerollMoment (plan.moves[(size_t) timeline.selected].key);
        timeline.refresh(); showSelected();
    };
    neverButton.onClick = [this]
    {
        const auto& plan = timeline.shownPlan();
        if (timeline.selected < 0 || timeline.selected >= (int) plan.moves.size()) return;
        processor.setMode (plan.moves[(size_t) timeline.selected].effect, trm::mNever);
        timeline.selected = -1; timeline.refresh(); showSelected(); effectList.refresh();
    };
    playButton.onClick = [this]
    {
        const auto& plan = timeline.shownPlan();
        if (timeline.selected >= 0 && timeline.selected < (int) plan.moves.size()) processor.audition (plan.moves[(size_t) timeline.selected].effect);
    };
    rerollAllButton.onClick = [this] { processor.rerollAll(); timeline.refresh(); };

    setResizable (true, true);
    setResizeLimits (980, 600, 1800, 1200);
    setSize (1180, 700);
    timerCallback();
    showSelected();
    startTimerHz (8);
}

TransmutationEditor::~TransmutationEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TransmutationEditor::showSelected()
{
    const auto& plan = timeline.shownPlan();
    const auto ok = timeline.selected >= 0 && timeline.selected < (int) plan.moves.size();
    for (auto* b : { &lockButton, &rerollButton, &neverButton, &playButton }) b->setEnabled (ok);
    if (! ok)
    {
        moveInfo.setText ("Click a move on the timeline to lock it, re-roll it, play it, or switch it off for this track. "
                          "Double-click a move in the list to play it now over whatever is playing.", juce::dontSendNotification);
        lockButton.setButtonText ("Lock this moment");
        return;
    }
    const auto& m = plan.moves[(size_t) timeline.selected];
    const auto& e = trm::effect (m.effect);
    juce::String why;
    if (m.flags & trm::mfLocked) why << " - locked";
    if (m.flags & trm::mfAlways) why << " - set to Always";
    if (m.flags & trm::mfMotif) why << " - one of this song's motifs";
    if (m.flags & trm::mfAnswer) why << " - answering the lead";
    if (m.flags & trm::mfSignature) why << " - one of your signature moves";
    moveInfo.setText (juce::String (m.effect) + ". " + e.name + "  (" + barText (m.startQ) + ", " + trm::slotName (m.slot) + why + ")\n" + e.what,
                      juce::dontSendNotification);
    lockButton.setButtonText (processor.isLocked (m.key) ? "Unlock this moment" : "Lock this moment");
}

void TransmutationEditor::timerCallback()
{
    timeline.refresh();
    const auto plan = timeline.shownPlan();
    const auto in = processor.lastInput();
    const auto hear = processor.hearing();
    juce::StringArray hears;
    if (hear.known)
    {
        hears.add (hear.pitched ? "pitched" : "unpitched");
        if (hear.sustained) hears.add ("sustained");
        if (hear.plucky) hears.add ("plucky");
        if (hear.busy) hears.add ("busy");
    }
    juce::StringArray motifs;
    static const char* fam[] = { "repeats", "reverses", "pitch", "filters", "space", "grains", "stereo", "tape", "level", "colour", "harmony", "" };
    for (auto f : plan.motifFamilies) if (f >= 0) motifs.add (fam[f]);
    juce::String s;
    s << "Role: " << roleName (processor.effectiveRole()) << ((int) processor.apvts.getRawParameterValue (TParams::role)->load() == 0 ? " (auto)" : "") << "\n"
      << "Hears: " << (hears.isEmpty() ? juce::String ("listening...") : hears.joinIntoString (", ")) << "\n"
      << "Genre: " << trm::genreName (plan.genre) << ((int) processor.apvts.getRawParameterValue (TParams::genre)->load() == 0 ? " (from the tempo)" : "") << "\n"
      << "Song spice (Ouroboros): " << juce::String (juce::roundToInt (in.songSpice * 100.0f)) << "%\n"
      << "Motifs this song: " << (motifs.isEmpty() ? juce::String ("-") : motifs.joinIntoString (", ")) << "\n"
      << "Fake drop: " << (plan.fakeDrop >= 0 ? "drop " + juce::String (plan.fakeDrop + 1) : juce::String ("none")) << "\n"
      << juce::String ((int) plan.moves.size()) << " moves planned" << (processor.mapFound.load() ? "" : " (no Ouroboros map yet)") << "\n"
      << "First drop remembered: " << (processor.memoryBars.load() > 0 ? juce::String (processor.memoryBars.load()) + " bars" : juce::String ("not yet"));
    juce::StringArray whole;
    for (const auto& m : plan.moves) if (m.slot & trm::sSong) whole.add (trm::effect (m.effect).name);
    if (! whole.isEmpty()) s << "\nAll song: " << whole.joinIntoString (", ");
    if (processor.runawaysCaught.load() > 0) s << "\nRunaways caught: " << juce::String (processor.runawaysCaught.load());
    status.setText (s, juce::dontSendNotification);

    juce::StringArray now;
    for (auto& a : processor.activeNow) if (const auto id = a.load(); id > 0) now.add (trm::effect (id).name);
    nowPlaying.setText (now.isEmpty() ? juce::String ("Now: -") : "Now: " + now.joinIntoString (" + "), juce::dontSendNotification);
    nowPlaying.setColour (juce::Label::textColourId, now.isEmpty() ? Colours::dimText : Colours::ember);
}

void TransmutationEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "TRANSMUTATION", "creative moves, directed for each track", Colours::ember);
    drawPanel (g, leftArea, "This track", Colours::ember);
    drawPanel (g, planArea, "The plan", Colours::brass);
    drawPanel (g, listArea, "Moves", Colours::verdigris);
}

void TransmutationEditor::resized()
{
    autopilot.setBounds (getLocalBounds().removeFromBottom (AutopilotStrip::height + 8).reduced (16, 4));
    auto bounds = getLocalBounds().withTrimmedBottom (AutopilotStrip::height + 4).reduced (16);
    bounds.removeFromTop (44);
    leftArea = bounds.removeFromLeft (290);
    bounds.removeFromLeft (12);
    listArea = bounds.removeFromRight (340);
    bounds.removeFromRight (12);
    planArea = bounds;

    auto l = leftArea.reduced (10, 8).withTrimmedTop (16);
    layoutRow (l.removeFromTop (110), { &spice, &builds, &drops });
    layoutRow (l.removeFromTop (110), { &amount, &output });
    auto choices = l.removeFromTop (58);
    genre.setBounds (choices.removeFromLeft (choices.getWidth() / 2).reduced (2, 0));
    role.setBounds (choices.reduced (2, 0));
    l.removeFromTop (4);
    director.setBounds (l.removeFromTop (28));
    l.removeFromTop (6);
    status.setBounds (l);

    auto p = planArea.reduced (10, 8).withTrimmedTop (16);
    nowPlaying.setBounds (p.removeFromTop (22));
    auto buttons = p.removeFromBottom (30);
    const auto bw = buttons.getWidth() / 5;
    for (auto* b : { &lockButton, &rerollButton, &neverButton, &playButton, &rerollAllButton })
        b->setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
    p.removeFromBottom (6);
    moveInfo.setBounds (p.removeFromBottom (48));
    p.removeFromBottom (6);
    timeline.setBounds (p);

    auto r = listArea.reduced (10, 8).withTrimmedTop (16);
    category.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);
    effectList.setBounds (r);
}
