#include "SuiteUI.h"

namespace suite::ui
{
    LookAndFeel::LookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::rotarySliderFillColourId, Colours::brass);
        setColour (juce::Label::textColourId, Colours::dimText);
        setColour (juce::ComboBox::backgroundColourId, Colours::background);
        setColour (juce::ComboBox::outlineColourId, Colours::outline);
        setColour (juce::ComboBox::textColourId, Colours::text);
        setColour (juce::ComboBox::arrowColourId, Colours::dimText);
        setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::brass.withAlpha (0.3f));
        setColour (juce::PopupMenu::textColourId, Colours::text);
        setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
        setColour (juce::ToggleButton::textColourId, Colours::text);
        setColour (juce::ToggleButton::tickColourId, Colours::brass);
        setColour (juce::TextButton::buttonColourId, Colours::background);
        setColour (juce::TextButton::buttonOnColourId, Colours::brass);
        setColour (juce::TextButton::textColourOffId, Colours::text);
        setColour (juce::TextButton::textColourOnId, Colours::background);
    }

    void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        if (radius < 6.0f)
            return;

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

        g.setColour (Colours::text);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.25f, angle),
                      centre.getPointOnCircumference (knobR * 0.8f, angle) }, 2.0f);
    }

    void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
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
        g.drawFittedText (button.getButtonText(), bounds.toNearestInt().reduced (4, 0), juce::Justification::centred, 2);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                            bool highlighted, bool down)
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto c = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : colour;
        if (down)             c = c.brighter (0.2f);
        else if (highlighted) c = c.brighter (0.08f);
        g.setColour (c);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (Colours::outline.brighter (highlighted ? 0.4f : 0.0f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.2f);
    }

    void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        juce::Path arrow;
        const auto ax = (float) width - 16.0f, ay = (float) height * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }

    //==============================================================================
    Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text, juce::Colour colour)
        : attachment (state, paramId, slider)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);

        caption.setText (text, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (12.0f));
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);
    }

    void Knob::resized()
    {
        // Keep knobs a consistent size: tall cells centre the knob instead of inflating it.
        auto area = getLocalBounds();
        if (area.getHeight() > maxHeight)
            area = area.withSizeKeepingCentre (area.getWidth(), maxHeight);
        caption.setBounds (area.removeFromTop (16));
        slider.setBounds (area);
    }

    Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label, juce::Colour colour)
        : juce::ToggleButton (label), attachment (state, paramId, *this)
    {
        setColour (juce::ToggleButton::tickColourId, colour);
    }

    Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (p->choices, 1);
        addAndMakeVisible (box);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);

        caption.setText (text, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (caption);
    }

    void Choice::resized()
    {
        auto area = getLocalBounds();
        if (area.getHeight() > 130) // match Knob's layout so captions line up in a row
            area = area.withSizeKeepingCentre (area.getWidth(), 130);
        caption.setBounds (area.removeFromTop (16));
        box.setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 8, 220), 26));
    }

    //==============================================================================
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour titleColour)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (area.toFloat(), 8.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (area.toFloat(), 8.0f, 1.0f);

        if (title.isNotEmpty())
        {
            g.setColour (titleColour);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (title.toUpperCase(), area.reduced (12, 6).removeFromTop (14), juce::Justification::topLeft);
        }
    }

    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, const juce::String& tagline, juce::Colour colour)
    {
        juce::Font titleFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.setColour (colour);
        g.setFont (titleFont);
        const auto titleW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont, name)) + 16;
        g.drawText (name, area.removeFromLeft (titleW), juce::Justification::centredLeft);

        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (tagline, area.withTrimmedTop (4), juce::Justification::centredLeft);
    }

    void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items)
    {
        if (items.size() == 0)
            return;
        const auto w = area.getWidth() / (int) items.size();
        for (auto* c : items)
            if (c != nullptr)
                c->setBounds (area.removeFromLeft (w));
    }

    //==============================================================================
    TabBar::TabBar (juce::StringArray n, std::vector<juce::Colour> c) : names (std::move (n)), colours (std::move (c)) {}

    void TabBar::paint (juce::Graphics& g)
    {
        const auto w = getWidth() / juce::jmax (1, names.size());
        for (int i = 0; i < names.size(); ++i)
        {
            const auto r = juce::Rectangle<int> (i * w, 0, w, getHeight()).reduced (3, 0).toFloat();
            const auto colour = colours.empty() ? Colours::brass : colours[(size_t) i % colours.size()];
            const auto on = i == selected;

            g.setColour (on ? colour.withAlpha (0.18f) : Colours::panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (on ? colour : Colours::outline);
            g.drawRoundedRectangle (r, 6.0f, 1.2f);
            g.setColour (on ? colour : Colours::dimText);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (names[i], r, juce::Justification::centred);
        }
    }

    void TabBar::mouseDown (const juce::MouseEvent& e)
    {
        const auto w = getWidth() / juce::jmax (1, names.size());
        setSelected (juce::jlimit (0, names.size() - 1, e.x / juce::jmax (1, w)));
    }

    void TabBar::setSelected (int index)
    {
        if (index == selected)
            return;
        selected = index;
        repaint();
        if (onChange)
            onChange (selected);
    }
}
