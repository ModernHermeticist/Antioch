#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour background { 0xff15161c };
    const juce::Colour panel      { 0xff1f2129 };
    const juce::Colour outline    { 0xff2e313c };
    const juce::Colour text       { 0xffd9dbe3 };
    const juce::Colour dimText    { 0xff8a8e9c };
    const juce::Colour accent     { 0xffe0b25a }; // brass
    const juce::Colour accent2    { 0xff6fc3c9 }; // verdigris

    const juce::Colour tapColours[] = { juce::Colour (0xffe0b25a), juce::Colour (0xff6fc3c9),
                                        juce::Colour (0xffc77dd8), juce::Colour (0xff8fd17a) };
}

//==============================================================================
AntiochLookAndFeel::AntiochLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    setColour (juce::Label::textColourId, Colours::dimText);
    setColour (juce::ComboBox::backgroundColourId, Colours::background);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::arrowColourId, Colours::dimText);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.3f));
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::ToggleButton::textColourId, Colours::text);
}

void AntiochLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto lineW  = juce::jmax (2.5f, radius * 0.14f);
    const auto arcR   = radius - lineW * 0.5f;
    const auto angle  = startAngle + pos * (endAngle - startAngle);
    const auto fill   = slider.findColour (juce::Slider::rotarySliderFillColourId)
                            .withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.35f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::outline);
    g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    // Bipolar parameters (pitch, pan) fill outwards from the centre.
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (fill);
    g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    const auto knobR = arcR - lineW * 1.4f;
    g.setColour (Colours::panel.brighter (0.15f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    const auto tip = centre.getPointOnCircumference (knobR * 0.8f, angle);
    g.setColour (Colours::text);
    g.drawLine ({ centre.getPointOnCircumference (knobR * 0.25f, angle), tip }, 2.0f);
}

void AntiochLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto on = button.getToggleState();
    const auto colour = button.findColour (juce::ToggleButton::tickColourId);

    g.setColour (on ? colour.withAlpha (0.25f) : Colours::background);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (on ? colour : (highlighted ? Colours::dimText : Colours::outline));
    g.drawRoundedRectangle (bounds, 4.0f, 1.2f);

    g.setColour (on ? Colours::text : Colours::dimText);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
}

//==============================================================================
AntiochEditor::AntiochEditor (AntiochProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);
    auto& state = processor.apvts;

    syncButton.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    addAndMakeVisible (syncButton);
    syncAttachment = std::make_unique<ButtonAttachment> (state, Params::sync, syncButton);

    bpmLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (bpmLabel);

    rootBox.addItemList (Params::noteNames(), 1);
    addAndMakeVisible (rootBox);
    rootAttachment = std::make_unique<ComboAttachment> (state, Params::rootNote, rootBox);
    rootLabel.setText ("Key", juce::dontSendNotification);
    rootLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (rootLabel);

    setupKnob (octave,    Params::rootOct,   "Octave");
    setupKnob (resonance, Params::resonance, "Resonance");
    setupKnob (resMix,    Params::resMix,    "Harmonics");
    setupKnob (damping,   Params::damping,   "Damping");
    setupKnob (lowCut,    Params::lowCut,    "Low Cut");
    setupKnob (mix,       Params::mix,       "Mix");
    setupKnob (output,    Params::output,    "Output");

    for (auto* k : { &resonance, &resMix })
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent2);

    for (int t = 0; t < Params::numTaps; ++t)
    {
        auto& row = rows[(size_t) t];
        const auto colour = Colours::tapColours[t];

        row.title.setText ("TAP " + juce::String (t + 1), juce::dontSendNotification);
        row.title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        row.title.setColour (juce::Label::textColourId, colour);
        addAndMakeVisible (row.title);

        row.on.setButtonText ("ON");
        row.on.setColour (juce::ToggleButton::tickColourId, colour);
        addAndMakeVisible (row.on);
        row.onAttachment = std::make_unique<ButtonAttachment> (state, Params::tapId (t, Params::Tap::on), row.on);

        row.division.addItemList (Params::divisionNames(), 1);
        addAndMakeVisible (row.division);
        row.divisionAttachment = std::make_unique<ComboAttachment> (state, Params::tapId (t, Params::Tap::division), row.division);

        row.harmonicNote.setJustificationType (juce::Justification::centred);
        row.harmonicNote.setFont (juce::FontOptions (14.0f));
        addAndMakeVisible (row.harmonicNote);

        setupKnob (row.time,     Params::tapId (t, Params::Tap::timeMs),   "Time");
        setupKnob (row.pitch,    Params::tapId (t, Params::Tap::pitch),    "Pitch");
        setupKnob (row.feedback, Params::tapId (t, Params::Tap::feedback), "Feedback");
        setupKnob (row.harmonic, Params::tapId (t, Params::Tap::harmonic), "Harmonic");
        setupKnob (row.level,    Params::tapId (t, Params::Tap::level),    "Level");
        setupKnob (row.pan,      Params::tapId (t, Params::Tap::pan),      "Pan");

        for (auto* k : { &row.time, &row.pitch, &row.feedback, &row.harmonic, &row.level, &row.pan })
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        row.harmonic.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent2);
        row.harmonic.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        row.harmonicNote.setColour (juce::Label::textColourId, Colours::text);
    }

    setSize (940, 640);
    timerCallback();
    startTimerHz (15);
}

AntiochEditor::~AntiochEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AntiochEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 16);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.apvts, paramId, knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (knob.label);
}

void AntiochEditor::timerCallback()
{
    const auto synced = processor.apvts.getRawParameterValue (Params::sync)->load() > 0.5f;
    bpmLabel.setText (synced ? juce::String (processor.currentBpm.load(), 1) + " BPM" : "Free time (ms)",
                      juce::dontSendNotification);

    for (auto& row : rows)
    {
        row.division.setVisible (synced);
        row.time.slider.setVisible (! synced);
        row.time.label.setText (synced ? "Division" : "Time", juce::dontSendNotification);
    }

    updateTapLabels();
}

void AntiochEditor::updateTapLabels()
{
    auto& state = processor.apvts;
    const auto root = (int) state.getRawParameterValue (Params::rootNote)->load();
    const auto oct  = (int) state.getRawParameterValue (Params::rootOct)->load();
    const auto rootHz = juce::MidiMessage::getMidiNoteInHertz (12 * (oct + 1) + root);

    for (int t = 0; t < Params::numTaps; ++t)
    {
        const auto harm = (int) state.getRawParameterValue (Params::tapId (t, Params::Tap::harmonic))->load();
        const auto hz = rootHz * harm;

        // Nearest note name for the resonance frequency, e.g. "≈ E4  330 Hz".
        const auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
        const auto text = "H" + juce::String (harm) + juce::String::fromUTF8 ("  \xe2\x89\x88 ")
                        + juce::MidiMessage::getMidiNoteName (midi, true, true, 4)
                        + "  " + juce::String (juce::roundToInt (hz)) + " Hz";
        rows[(size_t) t].harmonicNote.setText (text, juce::dontSendNotification);
    }
}

void AntiochEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Header
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("ANTIOCH", 20, 12, 200, 32, juce::Justification::centredLeft);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("harmonic echo", 160, 20, 200, 20, juce::Justification::centredLeft);

    for (auto area : { globalArea, tapArea })
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (area.toFloat(), 8.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (area.toFloat(), 8.0f, 1.0f);
    }

    // Row separators
    g.setColour (Colours::outline);
    const auto rowH = tapArea.getHeight() / Params::numTaps;
    for (int i = 1; i < Params::numTaps; ++i)
        g.drawHorizontalLine (tapArea.getY() + i * rowH, (float) tapArea.getX() + 12.0f, (float) tapArea.getRight() - 12.0f);
}

void AntiochEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);

    auto header = bounds.removeFromTop (40);
    bpmLabel.setBounds (header.removeFromRight (120));
    syncButton.setBounds (header.removeFromRight (120).reduced (4, 6));

    bounds.removeFromTop (8);
    globalArea = bounds.removeFromTop (120);
    bounds.removeFromTop (12);
    tapArea = bounds;

    auto placeKnob = [] (Knob& k, juce::Rectangle<int> area)
    {
        k.label.setBounds (area.removeFromTop (16));
        k.slider.setBounds (area);
    };

    // ---- Global row ----
    {
        auto area = globalArea.reduced (12, 10);
        const auto cellW = area.getWidth() / 8;

        auto keyCell = area.removeFromLeft (cellW);
        rootLabel.setBounds (keyCell.removeFromTop (16));
        rootBox.setBounds (keyCell.withSizeKeepingCentre (cellW - 24, 26));

        for (auto* k : { &octave, &resonance, &resMix, &damping, &lowCut, &mix, &output })
            placeKnob (*k, area.removeFromLeft (cellW));
    }

    // ---- Tap rows ----
    const auto rowH = tapArea.getHeight() / Params::numTaps;
    for (int t = 0; t < Params::numTaps; ++t)
    {
        auto& row = rows[(size_t) t];
        auto area = juce::Rectangle<int> (tapArea.getX(), tapArea.getY() + t * rowH, tapArea.getWidth(), rowH).reduced (12, 6);
        const auto cellW = area.getWidth() / 7;

        auto titleCell = area.removeFromLeft (cellW);
        row.title.setBounds (titleCell.removeFromTop (titleCell.getHeight() / 2));
        row.on.setBounds (titleCell.withSizeKeepingCentre (56, 24).withX (titleCell.getX() + 4));

        auto timeCell = area.removeFromLeft (cellW);
        placeKnob (row.time, timeCell);
        row.division.setBounds (timeCell.withTrimmedTop (16).withSizeKeepingCentre (cellW - 16, 26));

        placeKnob (row.pitch,    area.removeFromLeft (cellW));
        placeKnob (row.feedback, area.removeFromLeft (cellW));

        auto harmCell = area.removeFromLeft (cellW);
        row.harmonic.label.setBounds (harmCell.removeFromTop (16));
        row.harmonicNote.setBounds (harmCell.removeFromBottom (16).expanded (24, 0));
        row.harmonic.slider.setBounds (harmCell);

        placeKnob (row.level, area.removeFromLeft (cellW));
        placeKnob (row.pan,   area.removeFromLeft (cellW));
    }
}
