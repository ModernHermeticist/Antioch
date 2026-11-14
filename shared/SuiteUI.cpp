#include "SuiteUI.h"
#include "Watchdog.h"

namespace suite::ui
{
    namespace
    {
        juce::Font font (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
        constexpr float radius = 6.0f;
    }

    LookAndFeel::LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, Colours::brass.withAlpha (0.35f));
        setColour (juce::Slider::rotarySliderFillColourId, Colours::brass);
        setColour (juce::Slider::backgroundColourId, Colours::raised);
        setColour (juce::Slider::trackColourId, Colours::brass);
        setColour (juce::Label::textColourId, Colours::dimText);
        setColour (juce::ComboBox::backgroundColourId, Colours::raised);
        setColour (juce::ComboBox::outlineColourId, Colours::outline);
        setColour (juce::ComboBox::textColourId, Colours::text);
        setColour (juce::ComboBox::arrowColourId, Colours::dimText);
        setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::raised);
        setColour (juce::PopupMenu::textColourId, Colours::text);
        setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
        setColour (juce::PopupMenu::headerTextColourId, Colours::dimText);
        setColour (juce::ToggleButton::textColourId, Colours::text);
        setColour (juce::ToggleButton::tickColourId, Colours::brass);
        setColour (juce::TextButton::buttonColourId, Colours::raised);
        setColour (juce::TextButton::buttonOnColourId, Colours::brass);
        setColour (juce::TextButton::textColourOffId, Colours::text);
        setColour (juce::TextButton::textColourOnId, Colours::background);
        setColour (juce::TextEditor::backgroundColourId, Colours::raised);
        setColour (juce::TextEditor::textColourId, Colours::text);
        setColour (juce::TextEditor::outlineColourId, Colours::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, Colours::brass.withAlpha (0.7f));
        setColour (juce::TextEditor::highlightColourId, Colours::brass.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, Colours::text);
        setColour (juce::ListBox::backgroundColourId, Colours::panel);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId, Colours::outline.brighter (0.3f));
        setColour (juce::TooltipWindow::backgroundColourId, Colours::raised);
        setColour (juce::TooltipWindow::textColourId, Colours::text);
        setColour (juce::TooltipWindow::outlineColourId, Colours::outline);
        setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
        setColour (juce::AlertWindow::textColourId, Colours::text);
        setColour (juce::AlertWindow::outlineColourId, Colours::outline);
        setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
        setDefaultSansSerifTypefaceName ({});
    }

    void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        if (r < 6.0f)
            return;

        const auto centre = bounds.getCentre();
        const auto lineW  = juce::jmax (2.5f, r * 0.12f);
        const auto arcR   = r - lineW * 0.5f;
        const auto angle  = startAngle + pos * (endAngle - startAngle);
        const auto hover  = slider.isMouseOverOrDragging() && slider.isEnabled();
        const auto fill   = slider.findColour (juce::Slider::rotarySliderFillColourId)
                                .withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.35f);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colours::outline);
        g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Bipolar parameters (pitch, pan, gain) fill outwards from the centre.
        const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (fill);
        g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // the cap: a soft vertical gradient, lighter when you hover
        const auto knobR = arcR - lineW * 1.5f;
        const auto cap = juce::Rectangle<float> (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
        juce::ColourGradient grad (Colours::raised.brighter (hover ? 0.25f : 0.15f), cap.getX(), cap.getY(),
                                   Colours::panel.darker (0.1f), cap.getX(), cap.getBottom(), false);
        g.setGradientFill (grad);
        g.fillEllipse (cap);
        g.setColour (Colours::background.withAlpha (0.6f));
        g.drawEllipse (cap, 1.0f);

        g.setColour (Colours::text);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.3f, angle),
                      centre.getPointOnCircumference (knobR * 0.82f, angle) }, 2.2f);

        // Ghost ring: the value actually in use (e.g. base + section scene offset).
        const auto ghost = (float) (double) slider.getProperties().getWithDefault ("ghost", -1.0);
        if (ghost >= 0.0f && std::abs (ghost - pos) > 0.004f)
        {
            const auto ghostAngle = startAngle + ghost * (endAngle - startAngle);
            const auto ghostR = arcR + lineW * 0.95f;
            juce::Path ring;
            ring.addCentredArc (centre.x, centre.y, ghostR, ghostR, 0.0f, juce::jmin (angle, ghostAngle), juce::jmax (angle, ghostAngle), true);
            g.setColour (Colours::text.withAlpha (0.7f));
            g.strokePath (ring, juce::PathStrokeType (1.5f));
            const auto dot = centre.getPointOnCircumference (ghostR, ghostAngle);
            g.fillEllipse (dot.x - 3.0f, dot.y - 3.0f, 6.0f, 6.0f);
        }
    }

    juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (Type::value, true));
        l->setColour (juce::Label::textColourId, Colours::text);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }

    void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
    {
        // a pill: a dot that lights in the accent when it's on, the label beside it
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto on = button.getToggleState();
        const auto colour = button.findColour (juce::ToggleButton::tickColourId);
        const auto enabled = button.isEnabled();

        g.setColour (on ? colour.withAlpha (0.16f) : (highlighted ? Colours::raised.brighter (0.06f) : Colours::raised));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (on ? colour.withAlpha (enabled ? 0.85f : 0.4f) : (highlighted ? Colours::outline.brighter (0.4f) : Colours::outline));
        g.drawRoundedRectangle (bounds, radius, down ? 1.6f : 1.0f);

        const auto dotD = 8.0f;
        const auto textArea = bounds.toNearestInt().reduced (8, 0);
        const auto textW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (Type::button, true), button.getButtonText()));
        const auto contentW = juce::jmin (textArea.getWidth(), (int) dotD + 7 + textW);
        auto content = textArea.withSizeKeepingCentre (contentW, textArea.getHeight());
        const auto dot = content.removeFromLeft ((int) dotD).toFloat().withSizeKeepingCentre (dotD, dotD);
        content.removeFromLeft (7);
        g.setColour (on ? colour : Colours::faintText);
        g.fillEllipse (dot);
        g.setColour (on ? Colours::text : Colours::dimText);
        if (! enabled) g.setColour (Colours::faintText);
        g.setFont (font (Type::button, true));
        g.drawFittedText (button.getButtonText(), content, juce::Justification::centredLeft, 2, 0.85f);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                            bool highlighted, bool down)
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto primary = (bool) button.getProperties().getWithDefault ("primary", false);
        auto c = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : colour;
        if (primary && ! button.getToggleState()) c = colour.withAlpha (0.9f);
        if (! button.isEnabled()) c = c.withMultipliedAlpha (0.5f);
        else if (down)        c = c.brighter (0.18f);
        else if (highlighted) c = c.brighter (0.07f);
        g.setColour (c);
        g.fillRoundedRectangle (bounds, radius);
        if (! primary)
        {
            g.setColour (highlighted && button.isEnabled() ? Colours::outline.brighter (0.45f) : Colours::outline);
            g.drawRoundedRectangle (bounds, radius, 1.0f);
        }
        if (button.hasKeyboardFocus (true))
        {
            g.setColour (Colours::text.withAlpha (0.35f));
            g.drawRoundedRectangle (bounds.reduced (1.5f), radius - 1.0f, 1.0f);
        }
    }

    void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
    {
        const auto primary = (bool) button.getProperties().getWithDefault ("primary", false);
        const auto on = button.getToggleState();
        auto colour = primary || on ? Colours::background : button.findColour (juce::TextButton::textColourOffId);
        if (! button.isEnabled()) colour = Colours::faintText;
        g.setColour (colour);
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 2), juce::Justification::centred, 2, 0.85f);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
    {
        const auto primary = (bool) b.getProperties().getWithDefault ("primary", false);
        return font (juce::jmin (Type::button + 1.0f, (float) h * 0.5f), primary);
    }

    void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down, int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        const auto hover = box.isMouseOver (true) && box.isEnabled();
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).brighter (hover ? 0.06f : 0.0f));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (box.hasKeyboardFocus (true) || down ? Colours::dimText : (hover ? Colours::outline.brighter (0.45f) : box.findColour (juce::ComboBox::outlineColourId)));
        g.drawRoundedRectangle (bounds, radius, 1.0f);

        juce::Path arrow;
        const auto ax = (float) width - 15.0f, ay = (float) height * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box) { return font (juce::jmin (Type::body + 1.0f, (float) box.getHeight() * 0.55f)); }

    void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos,
                                        float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }

        // Bar that fills from zero (the centre, for bipolar ranges) towards the value.
        const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);

        const auto zero = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                              ? (float) slider.getPositionOfValue (0.0) : (float) x;
        const auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zero, sliderPos), r.getY() + 2.0f,
                                                                      juce::jmax (zero, sliderPos), r.getBottom() - 2.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (fill, 2.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        if (zero > (float) x)
        {
            g.setColour (Colours::dimText.withAlpha (0.6f));
            g.drawVerticalLine ((int) zero, r.getY(), r.getBottom());
        }
    }

    void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (Colours::panel);
        g.setColour (Colours::outline);
        g.drawRect (0, 0, width, height, 1);
    }

    void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                         bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                         const juce::Drawable*, const juce::Colour* textColour)
    {
        if (isSeparator)
        {
            g.setColour (Colours::outline);
            g.fillRect (area.reduced (10, 0).withSizeKeepingCentre (area.getWidth() - 20, 1));
            return;
        }
        auto r = area.reduced (4, 1);
        if (isHighlighted && isActive)
        {
            g.setColour (Colours::raised.brighter (0.08f));
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
        }
        r.removeFromLeft (8);
        const auto tick = r.removeFromLeft (16);
        if (isTicked)
        {
            g.setColour (Colours::brass);
            g.fillEllipse (tick.toFloat().withSizeKeepingCentre (7.0f, 7.0f));
        }
        if (hasSubMenu)
        {
            const auto a = r.removeFromRight (14).toFloat();
            juce::Path p;
            p.startNewSubPath (a.getCentreX() - 2.0f, a.getCentreY() - 4.0f);
            p.lineTo (a.getCentreX() + 2.0f, a.getCentreY());
            p.lineTo (a.getCentreX() - 2.0f, a.getCentreY() + 4.0f);
            g.setColour (Colours::dimText);
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }
        g.setColour (textColour != nullptr ? *textColour : (isActive ? Colours::text : Colours::faintText));
        g.setFont (getPopupMenuFont());
        g.drawFittedText (text, r.withTrimmedRight (6), juce::Justification::centredLeft, 1);
        if (shortcutKeyText.isNotEmpty())
        {
            g.setColour (Colours::dimText);
            g.setFont (font (Type::label));
            g.drawText (shortcutKeyText, r, juce::Justification::centredRight);
        }
    }

    juce::Font LookAndFeel::getPopupMenuFont() { return font (Type::body + 1.0f); }

    void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
    {
        if (isSeparator) { w = 50; h = 9; return; }
        h = 28;
        w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 56;
    }

    void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto r = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (Colours::raised);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Colours::outline.brighter (0.2f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, font (Type::body), Colours::text);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, (float) width - 20.0f);
        tl.draw (g, r.reduced (10.0f, 6.0f));
    }

    juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos, juce::Rectangle<int> parent)
    {
        juce::AttributedString s;
        s.append (tipText, font (Type::body), Colours::text);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 380.0f);
        const auto w = (int) std::ceil (tl.getWidth()) + 22, h = (int) std::ceil (tl.getHeight()) + 14;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 16, w, h).constrainedWithin (parent);
    }

    void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                     int thumbStart, int thumbSize, bool over, bool down)
    {
        const auto thumb = vertical ? juce::Rectangle<int> (x, y + thumbStart, width, thumbSize) : juce::Rectangle<int> (x + thumbStart, y, thumbSize, height);
        g.setColour (Colours::outline.brighter (down ? 0.6f : over ? 0.45f : 0.25f));
        g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
    }

    void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& e)
    {
        g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), radius);
    }

    void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
    {
        if (! e.isEnabled()) return;
        g.setColour (e.hasKeyboardFocus (true) && ! e.isReadOnly() ? e.findColour (juce::TextEditor::focusedOutlineColourId) : e.findColour (juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), radius, 1.0f);
    }

    void LookAndFeel::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image&)
    {
        juce::ignoreUnused (box);
        g.setColour (Colours::panel);
        g.fillPath (path);
        g.setColour (Colours::outline.brighter (0.2f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
    {
        g.fillAll (Colours::panel);
        g.setColour (Colours::outline);
        g.drawRect (alert.getLocalBounds(), 1);
        layout.draw (g, textArea.toFloat());
    }

    //==============================================================================
    Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text, juce::Colour colour)
        : paramId (id), attachment (state, id, slider)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 18);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        addAndMakeVisible (slider);

        caption.setText (text, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (font (Type::label));
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);
        if (auto* p = state.getParameter (id)) slider.setTooltip (p->getName (60) + " (double-click: back to the default)");
        if (auto* p = state.getParameter (id)) slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void Knob::setGhost (float normalised)
    {
        const auto previous = (double) slider.getProperties().getWithDefault ("ghost", -1.0);
        if (std::abs (previous - (double) normalised) < 1.0e-4)
            return;
        slider.getProperties().set ("ghost", (double) normalised);
        slider.repaint();
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
        caption.setFont (font (Type::label));
        addAndMakeVisible (caption);
    }

    void Choice::resized()
    {
        auto area = getLocalBounds();
        if (area.getHeight() > 130) // match Knob's layout so captions line up in a row
            area = area.withSizeKeepingCentre (area.getWidth(), 130);
        caption.setBounds (area.removeFromTop (16));
        box.setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 8, 220), 28));
    }

    //==============================================================================
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour titleColour)
    {
        const auto r = area.toFloat();
        juce::ColourGradient grad (Colours::panel.brighter (0.03f), r.getX(), r.getY(), Colours::panel, r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        if (title.isNotEmpty())
        {
            auto t = area.reduced (14, 8).removeFromTop (15);
            g.setColour (titleColour);
            g.fillRoundedRectangle (t.removeFromLeft (3).toFloat().withTrimmedTop (2).withTrimmedBottom (2), 1.5f);
            t.removeFromLeft (7);
            g.setColour (titleColour == Colours::dimText ? Colours::dimText : titleColour.interpolatedWith (Colours::text, 0.15f));
            g.setFont (juce::Font (juce::FontOptions (Type::section, juce::Font::bold)).withExtraKerningFactor (0.06f));
            g.drawText (title.toUpperCase(), t, juce::Justification::centredLeft);
        }
    }

    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, const juce::String& tagline, juce::Colour colour)
    {
        const auto titleFont = juce::Font (juce::FontOptions (Type::title, juce::Font::bold)).withExtraKerningFactor (0.04f);
        g.setColour (colour);
        g.setFont (titleFont);
        const auto titleW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont, name)) + 16;
        g.drawText (name, area.removeFromLeft (titleW), juce::Justification::centredLeft);

        g.setColour (Colours::dimText);
        g.setFont (font (Type::tagline));
        g.drawText (tagline, area.withTrimmedTop (3), juce::Justification::centredLeft);
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
        const auto track = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Colours::background);
        g.fillRoundedRectangle (track, 8.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (track, 8.0f, 1.0f);
        const auto w = track.getWidth() / (float) juce::jmax (1, names.size());
        for (int i = 0; i < names.size(); ++i)
        {
            const auto r = juce::Rectangle<float> (track.getX() + w * (float) i, track.getY(), w, track.getHeight()).reduced (3.0f);
            const auto colour = colours.empty() ? Colours::brass : colours[(size_t) i % colours.size()];
            const auto on = i == selected;
            if (on)
            {
                g.setColour (colour.withAlpha (0.2f));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (colour.withAlpha (0.8f));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
            else if (i == hovered)
            {
                g.setColour (Colours::raised);
                g.fillRoundedRectangle (r, 6.0f);
            }
            g.setColour (on ? colour.interpolatedWith (Colours::text, 0.2f) : (i == hovered ? Colours::text : Colours::dimText));
            g.setFont (juce::Font (juce::FontOptions (Type::body, juce::Font::bold)).withExtraKerningFactor (0.05f));
            g.drawFittedText (names[i], r.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    void TabBar::mouseDown (const juce::MouseEvent& e)
    {
        const auto w = getWidth() / juce::jmax (1, names.size());
        setSelected (juce::jlimit (0, names.size() - 1, e.x / juce::jmax (1, w)));
    }

    void TabBar::mouseMove (const juce::MouseEvent& e)
    {
        const auto w = getWidth() / juce::jmax (1, names.size());
        const auto h = juce::jlimit (0, names.size() - 1, e.x / juce::jmax (1, w));
        if (h != hovered) { hovered = h; repaint(); }
    }

    void TabBar::mouseExit (const juce::MouseEvent&) { hovered = -1; repaint(); }

    void TabBar::setSelected (int index)
    {
        if (index == selected)
            return;
        selected = index;
        repaint();
        if (onChange)
            onChange (selected);
    }

    //==============================================================================
    CommandPalette::CommandPalette (std::vector<Action> actions, juce::Colour a) : all (std::move (actions)), accent (a)
    {
        field.setTextToShowWhenEmpty ("Search every action...", Colours::faintText);
        field.setFont (font (Type::body + 2.0f));
        field.setIndents (10, 8);
        field.addListener (this);
        field.addKeyListener (this);
        addAndMakeVisible (field);
        list.setModel (this);
        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, Colours::panel);
        addAndMakeVisible (list);
        refilter();
        setSize (560, 420);
    }

    void CommandPalette::show (std::vector<Action> actions, juce::Component& anchor, juce::Colour accent)
    {
        auto palette = std::make_unique<CommandPalette> (std::move (actions), accent);
        auto* raw = palette.get();
        auto& box = juce::CallOutBox::launchAsynchronously (std::move (palette), anchor.getScreenBounds(), nullptr);
        box.setLookAndFeel (&anchor.getLookAndFeel());
        raw->field.grabKeyboardFocus();
    }

    void CommandPalette::paint (juce::Graphics& g) { g.fillAll (Colours::panel); }

    void CommandPalette::resized()
    {
        auto r = getLocalBounds().reduced (8);
        field.setBounds (r.removeFromTop (36));
        r.removeFromTop (6);
        list.setBounds (r);
    }

    void CommandPalette::refilter()
    {
        filtered.clear();
        const auto words = juce::StringArray::fromTokens (field.getText().toLowerCase(), " ", "");
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto hay = (all[(size_t) i].name + " " + all[(size_t) i].detail + " " + all[(size_t) i].group).toLowerCase();
            bool ok = true;
            for (const auto& w : words) if (w.isNotEmpty() && ! hay.contains (w)) { ok = false; break; }
            if (ok) filtered.push_back (i);
        }
        // names that start with the query first
        const auto q = field.getText().toLowerCase().trim();
        if (q.isNotEmpty())
            std::stable_sort (filtered.begin(), filtered.end(), [this, &q] (int a, int b)
            {
                return all[(size_t) a].name.toLowerCase().startsWith (q) && ! all[(size_t) b].name.toLowerCase().startsWith (q);
            });
        list.updateContent();
        list.selectRow (filtered.empty() ? -1 : 0);
        list.repaint();
    }

    void CommandPalette::textEditorTextChanged (juce::TextEditor&) { refilter(); }

    bool CommandPalette::keyPressed (const juce::KeyPress& k, juce::Component*)
    {
        if (k == juce::KeyPress::downKey) { list.selectRow (juce::jmin ((int) filtered.size() - 1, list.getSelectedRow() + 1)); list.scrollToEnsureRowIsOnscreen (list.getSelectedRow()); return true; }
        if (k == juce::KeyPress::upKey) { list.selectRow (juce::jmax (0, list.getSelectedRow() - 1)); list.scrollToEnsureRowIsOnscreen (list.getSelectedRow()); return true; }
        return false;
    }

    void CommandPalette::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= (int) filtered.size()) return;
        const auto& a = all[(size_t) filtered[(size_t) row]];
        auto r = juce::Rectangle<int> (width, height).reduced (2, 1);
        if (selected)
        {
            g.setColour (Colours::raised.brighter (0.06f));
            g.fillRoundedRectangle (r.toFloat(), 5.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (r.removeFromLeft (3).toFloat().reduced (0.0f, 6.0f), 1.5f);
        }
        r.removeFromLeft (10);
        if (a.group.isNotEmpty())
        {
            g.setColour (Colours::faintText);
            g.setFont (font (Type::label - 1.0f, true));
            g.drawText (a.group.toUpperCase(), r.removeFromRight (150).withTrimmedRight (8), juce::Justification::centredRight);
        }
        g.setColour (a.enabled ? Colours::text : Colours::faintText);
        g.setFont (font (Type::body + 1.0f, true));
        g.drawText (a.name, r.removeFromTop (height / 2 + 2).withTrimmedTop (3), juce::Justification::bottomLeft);
        g.setColour (Colours::dimText);
        g.setFont (font (Type::label));
        g.drawText (a.detail, r, juce::Justification::topLeft);
    }

    void CommandPalette::listBoxItemClicked (int row, const juce::MouseEvent&) { list.selectRow (row); runSelected(); }

    bool CommandPalette::runSelected()
    {
        const auto row = list.getSelectedRow();
        if (row < 0 || row >= (int) filtered.size()) return false;
        const auto a = all[(size_t) filtered[(size_t) row]];
        if (! a.enabled || ! a.run) return false;
        close();
        juce::MessageManager::callAsync ([run = a.run] { run(); });
        return true;
    }

    void CommandPalette::close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    }

    //==============================================================================
    HeaderBar::HeaderBar (juce::String n, juce::String t, juce::Colour a) : name (std::move (n)), tagline (std::move (t)), accent (a), statusDot (Colours::moss)
    {
        searchButton.setButtonText ("Search  (Ctrl+K)");
        searchButton.setTooltip ("Find and run any action in this plugin by typing (Ctrl+K, or Cmd+K on a Mac)");
        searchButton.onClick = [this] { openSearch(); };
        addAndMakeVisible (searchButton);
        advancedButton.setButtonText ("Advanced");
        advancedButton.setClickingTogglesState (false);
        advancedButton.setTooltip ("Show the manual controls (the plugin runs itself without them)");
        advancedButton.onClick = [this] { setAdvanced (! advancedOn); };
        addChildComponent (advancedButton);
        setOpaque (false);
    }

    HeaderBar::~HeaderBar() { if (keyTarget != nullptr) keyTarget->removeKeyListener (this); }

    void HeaderBar::enableAdvanced (juce::ValueTree state, std::function<void (bool)> changed)
    {
        advState = state; onAdvanced = std::move (changed); hasAdvanced = true;
        advancedButton.setVisible (true);
        advancedOn = advState.isValid() && (bool) advState.getProperty ("uiAdvanced", false);
        advancedButton.setToggleState (advancedOn, juce::dontSendNotification);
        advancedButton.setColour (juce::TextButton::buttonOnColourId, accent);
        resized();
    }

    void HeaderBar::setAdvanced (bool on)
    {
        advancedOn = on;
        advancedButton.setToggleState (on, juce::dontSendNotification);
        if (advState.isValid()) advState.setProperty ("uiAdvanced", on, nullptr);
        if (onAdvanced) onAdvanced (on);
    }

    void HeaderBar::setStatus (const juce::String& text, juce::Colour dot)
    {
        if (text == status && dot == statusDot) return;
        status = text; statusDot = dot;
        repaint();
    }

    void HeaderBar::addRight (juce::Component& c, int width) { rights.push_back ({ &c, width }); addAndMakeVisible (c); resized(); }

    void HeaderBar::openSearch()
    {
        if (! actions) return;
        auto list = actions();
        if (hasAdvanced)
            list.push_back ({ advancedOn ? "Hide the advanced controls" : "Show the advanced controls", "the manual controls under the simple view", "View",
                              [safe = juce::Component::SafePointer<HeaderBar> (this)] { if (safe != nullptr) safe->setAdvanced (! safe->advancedOn); } });
        list.push_back ({ "Freeze log", "anything in the suite that held FL's interface for more than 50 ms this session", "Help",
                          [] { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Freeze log", suite::Watchdog::text()); } });
        CommandPalette::show (std::move (list), searchButton, accent);
    }

    void HeaderBar::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().withTrimmedLeft (4);
        const auto titleFont = juce::Font (juce::FontOptions (Type::title, juce::Font::bold)).withExtraKerningFactor (0.04f);
        const auto titleW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont, name)) + 14;
        g.setColour (accent);
        g.setFont (titleFont);
        g.drawText (name, r.removeFromLeft (titleW), juce::Justification::centredLeft);
        g.setColour (Colours::dimText);
        g.setFont (font (Type::tagline));
        // the tagline, and the status pill after it, stop where the buttons start (left out if there's no room)
        // When it's tight the live status matters more than the tagline: the tagline goes first.
        const auto limit = (buttonsLeft > 0 ? buttonsLeft : r.getRight()) - 8 - r.getX();
        const auto tagFull = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (Type::tagline), tagline)) + 16;
        const auto statusFull = status.isEmpty() ? 0 : (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (Type::label, true), status)) + 46;
        const auto showTagline = tagFull + juce::jmin (statusFull, 220) <= limit;
        if (showTagline) g.drawText (tagline, r.removeFromLeft (tagFull).withTrimmedTop (3), juce::Justification::centredLeft, true);
        // the status pill, after the tagline
        if (status.isNotEmpty())
        {
            const auto f = font (Type::label, true);
            const auto room = (buttonsLeft > 0 ? buttonsLeft : r.getRight()) - 12 - (r.getX() + 4);
            const auto w = juce::jmin (room, (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, status)) + 34);
            if (w > 110)
            {
                const auto pill = juce::Rectangle<int> (r.getX() + 4, r.getCentreY() - 12, w, 24).toFloat();
                g.setColour (Colours::raised);
                g.fillRoundedRectangle (pill, 12.0f);
                g.setColour (Colours::outline);
                g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);
                g.setColour (statusDot);
                g.fillEllipse (pill.getX() + 10.0f, pill.getCentreY() - 4.0f, 8.0f, 8.0f);
                g.setColour (Colours::text);
                g.setFont (f);
                g.drawText (status, pill.toNearestInt().withTrimmedLeft (24).withTrimmedRight (8), juce::Justification::centredLeft, true);
            }
        }
        g.setColour (Colours::outline);
        g.fillRect (getLocalBounds().removeFromBottom (1));
    }

    void HeaderBar::resized()
    {
        auto r = getLocalBounds().reduced (0, 10);
        if (hasAdvanced) { advancedButton.setBounds (r.removeFromRight (104)); r.removeFromRight (8); }
        searchButton.setBounds (r.removeFromRight (150)); r.removeFromRight (8);
        for (auto& [c, w] : rights) { c->setBounds (r.removeFromRight (w)); r.removeFromRight (8); }
        buttonsLeft = r.getRight() + 8;
    }

    void HeaderBar::parentHierarchyChanged()
    {
        auto* top = getTopLevelComponent();
        if (top == keyTarget) return;
        if (keyTarget != nullptr) keyTarget->removeKeyListener (this);
        keyTarget = top;
        if (keyTarget != nullptr && keyTarget != this) keyTarget->addKeyListener (this);
    }

    bool HeaderBar::keyPressed (const juce::KeyPress& k, juce::Component*)
    {
        if (k.getKeyCode() == 'K' && k.getModifiers().isCommandDown()) { openSearch(); return true; }
        return false;
    }

    void addMenuActions (std::vector<Action>& into, const juce::PopupMenu& menu, const juce::String& group, std::function<void (int)> pick)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.isSeparator || item.text.isEmpty()) continue;
            if (item.subMenu != nullptr) { addMenuActions (into, *item.subMenu, group.isEmpty() ? item.text : group + " > " + item.text, pick); continue; }
            if (item.itemID == 0) continue;
            into.push_back ({ item.text, group, group.upToFirstOccurrenceOf (" > ", false, false), [pick, id = item.itemID] { pick (id); }, item.isEnabled });
        }
    }
}
