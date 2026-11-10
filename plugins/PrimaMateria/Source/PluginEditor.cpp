#include "PluginEditor.h"

using namespace suite::ui;

namespace { const juce::Colour accent { 0xffb98b5e }; }

PrimaMateriaEditor::PrimaMateriaEditor (PrimaMateriaProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      follow (p.apvts, pm::followId, "Follow Ouroboros", Colours::ember),
      ramp (p.apvts, pm::rampId, "Ramp through builds", Colours::brass),
      laterOn (p.apvts, pm::laterOnId, "Separate Drop 2+", Colours::ember),
      lowManager (p.apvts, pm::lowManagerId, "Low-end manager", Colours::verdigris),
      morph (p.apvts, pm::morphId, "Morph"),
      gap (p.apvts, pm::gapId, "Gap before drop")
{
    setLookAndFeel (&lnf);

    roleBox.setTextWhenNothingSelected ("Track role preset...");
    for (size_t i = 0; i < pm::roles().size(); ++i)
        roleBox.addItem (pm::roles()[i].name, (int) i + 1);
    roleBox.onChange = [this] { if (roleBox.getSelectedId() > 0) processor.applyRole (roleBox.getSelectedId() - 1); };

    auto columnColour = [] (int t) { return t == pm::laterDrop ? sectionColour (suite::SectionType::Drop).brighter (0.35f) : sectionColour ((suite::SectionType) t); };
    for (int t = 0; t < pm::numColumns; ++t)
    {
        const auto c = columnColour (t);
        knobs.push_back (std::make_unique<Knob> (p.apvts, pm::levelId (t), "Level", c));
        knobs.push_back (std::make_unique<Knob> (p.apvts, pm::lowCutId (t), "Low cut", c));
        knobs.push_back (std::make_unique<Knob> (p.apvts, pm::highCutId (t), "High cut", c));
    }
    for (auto& k : knobs)
    {
        k->maxHeight = 110;
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        addAndMakeVisible (*k);
    }

    trim = std::make_unique<Knob> (p.apvts, pm::trimId, "Trim", accent);
    trim->maxHeight = 56;
    trim->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 16);
    trim->slider.setTooltip ("Track level after everything else. Mentalism's Rough mix sets it for the track's role.");
    addAndMakeVisible (*trim);

    status.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    lowManager.setTooltip ("Where a kick or bass track is playing, this track leaves the low end to them (an automatic low cut for its role)");
    for (auto* c : std::initializer_list<juce::Component*> { &roleBox, &follow, &ramp, &laterOn, &lowManager, &morph, &gap, &status })
        addAndMakeVisible (c);

    setSize (1120, 600);
    timerCallback();
    startTimerHz (15);
}

PrimaMateriaEditor::~PrimaMateriaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PrimaMateriaEditor::timerCallback()
{
    const auto t = processor.currentType.load();
    if (t != shownType)
    {
        shownType = t;
        repaint (gridArea);
    }

    auto hz = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    const auto lvl = processor.nowLevelDb.load();
    juce::String text;
    if (t >= 0)
    {
        text << "Now: " << (t == pm::laterDrop ? juce::String ("Drop 2+") : juce::String (suite::sectionName ((suite::SectionType) t)))
             << (processor.ramping.load() ? " (ramping)" : "") << (processor.gapping.load() ? " (gap)" : "")
             << juce::String::fromUTF8 ("  \xc2\xb7  level ") << (lvl <= pm::offDb + 0.05f ? juce::String ("off") : juce::String (lvl, 1) + " dB");
        if (processor.nowLowCut.load() > pm::lowCutOff + 1.0f) text << ", low cut " << hz (processor.nowLowCut.load());
        if (processor.nowHighCut.load() < pm::highCutOff - 100.0f) text << ", high cut " << hz (processor.nowHighCut.load());
    }
    else
    {
        text = processor.mapAvailable.load() ? "Ouroboros found: waiting for playback (passing audio unchanged)"
                                             : "No Ouroboros: passing audio unchanged";
    }
    status.setColour (juce::Label::textColourId, t >= 0 ? sectionColour ((suite::SectionType) juce::jmin (t, pm::numTypes - 1)) : Colours::dimText);
    if (const auto cut = processor.managerCutNow.load(); cut > 0.0f)
        text << "   |   Low end left to the kick/bass (cut " << juce::String (juce::roundToInt (cut)) << " Hz)";
    const auto lufs = processor.measuredLufs();
    if (lufs > -70.0f)
        text << "   |   Drop loudness " << juce::String (lufs, 1) << " LUFS";
    status.setText (text, juce::dontSendNotification);

    // The role (also set automatically from the track's name).
    const auto role = processor.getRole();
    if (role >= 0 && roleBox.getSelectedId() != role + 1)
        roleBox.setSelectedId (role + 1, juce::dontSendNotification);
}

void PrimaMateriaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "PRIMA MATERIA", "section mixer", accent);
    drawPanel (g, gridArea, {});

    for (int t = 0; t < pm::numColumns; ++t)
    {
        const auto col = columns[(size_t) t];
        const auto c = t == pm::laterDrop ? sectionColour (suite::SectionType::Drop).brighter (0.35f) : sectionColour ((suite::SectionType) t);
        const auto live = t == shownType;
        g.setColour (c.withAlpha (live ? 0.22f : 0.08f));
        g.fillRoundedRectangle (col.toFloat().reduced (3.0f), 6.0f);
        if (live)
        {
            g.setColour (c);
            g.drawRoundedRectangle (col.toFloat().reduced (3.0f), 6.0f, 2.0f);
        }
        g.setColour (c.brighter (0.3f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (t == pm::laterDrop ? juce::String ("DROP 2+") : juce::String (suite::sectionName ((suite::SectionType) t)).toUpperCase(),
                    col.withHeight (30).translated (0, 6),
                    juce::Justification::centred);
    }
}

void PrimaMateriaEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto header = bounds.removeFromTop (48);
    follow.setBounds (header.removeFromRight (180).reduced (4, 8));
    header.removeFromRight (12);
    trim->setBounds (header.removeFromRight (150).withTrimmedTop (-10).withTrimmedBottom (-6));
    trim->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 16);

    auto controls = bounds.removeFromTop (60);
    roleBox.setBounds (controls.removeFromLeft (220).withSizeKeepingCentre (220, 28).translated (0, 8));
    controls.removeFromLeft (12);
    ramp.setBounds (controls.removeFromLeft (180).withSizeKeepingCentre (172, 30).translated (0, 8));
    laterOn.setBounds (controls.removeFromLeft (160).withSizeKeepingCentre (152, 30).translated (0, 8));
    lowManager.setBounds (controls.removeFromLeft (160).withSizeKeepingCentre (152, 30).translated (0, 8));
    morph.setBounds (controls.removeFromLeft (150));
    gap.setBounds (controls.removeFromLeft (150));


    status.setBounds (bounds.removeFromBottom (30));
    bounds.removeFromBottom (6);
    gridArea = bounds;

    auto grid = gridArea.reduced (8);
    const auto w = grid.getWidth() / pm::numColumns;
    for (int t = 0; t < pm::numColumns; ++t)
    {
        columns[(size_t) t] = juce::Rectangle<int> (grid.getX() + t * w, grid.getY(), w, grid.getHeight());
        auto col = columns[(size_t) t].reduced (6).withTrimmedTop (34);
        const auto rowH = col.getHeight() / 3;
        for (int r = 0; r < 3; ++r)
            knobs[(size_t) (t * 3 + r)]->setBounds (col.removeFromTop (rowH));
    }
}
