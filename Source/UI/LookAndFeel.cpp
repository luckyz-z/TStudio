#include "UI/LookAndFeel.h"
#include "UI/Theme.h"

#include <cmath>

namespace ts
{
LookAndFeel::LookAndFeel()
{
    setColourScheme(juce::LookAndFeel_V4::ColourScheme(theme::window, theme::panel, theme::panelDark, theme::outline, theme::text,
                                                       theme::accent, theme::panelDark, theme::accent, theme::text));
    setColour(juce::TextButton::buttonColourId, theme::button);
    setColour(juce::TextButton::textColourOnId, theme::panelDark);
    setColour(juce::ResizableWindow::backgroundColourId, theme::window);
    setColour(juce::PopupMenu::backgroundColourId, theme::panel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::accent);
    setColour(juce::PopupMenu::highlightedTextColourId, theme::outline);
    setColour(juce::TreeView::backgroundColourId, theme::panelDark);
    setColour(juce::TreeView::selectedItemBackgroundColourId, theme::title);
    setColour(juce::ScrollBar::thumbColourId, theme::button);
    setColour(juce::ComboBox::backgroundColourId, theme::lcd);
    setColour(juce::ComboBox::outlineColourId, theme::outline);
    setColour(juce::Slider::thumbColourId, theme::accent);
    setColour(juce::Slider::trackColourId, theme::accent);
    setColour(juce::Slider::backgroundColourId, theme::lcd);
    setColour(juce::Label::textColourId, theme::text);
    setColour(juce::TooltipWindow::backgroundColourId, theme::panelDark);
    setColour(juce::TooltipWindow::textColourId, theme::text);
    setColour(juce::DocumentWindow::textColourId, theme::text);
    const auto installed = juce::Font::findAllTypefaceNames();
    for (const auto* name : { "Segoe UI", "Noto Sans", "Ubuntu", "Liberation Sans", "Arial" })
        if (installed.contains(name))
        {
            setDefaultSansSerifTypefaceName(name);
            break;
        }
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
    const auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto radius = size * 0.5f;
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const auto compact = size < 30.0f;
    const auto ring = compact ? 1.6f : juce::jmax(2.5f, radius * 0.13f);
    const auto arcRadius = radius - ring * 0.5f;
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto origin = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    const juce::PathStrokeType stroke(ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(theme::lcd);
    g.strokePath(track, stroke);

    if (std::abs(angle - origin) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin(origin, angle), juce::jmax(origin, angle), true);
        if (!compact)
        {
            g.setColour(fill.withAlpha(0.22f));
            g.strokePath(value, juce::PathStrokeType(ring * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(slider.isEnabled() ? fill : fill.withSaturation(0.0f));
        g.strokePath(value, stroke);
    }

    const auto bodyRadius = radius - ring - (compact ? 1.0f : radius * 0.07f);
    const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
    if (!compact)
    {
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillEllipse(body.translated(0.0f, 1.5f).expanded(0.5f));
    }
    g.setGradientFill(juce::ColourGradient(theme::knobTop, body.getCentreX(), body.getY(), theme::knobBottom, body.getCentreX(), body.getBottom(), false));
    g.fillEllipse(body);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.16f), body.getCentreX(), body.getY(), juce::Colours::transparentWhite,
                                           body.getCentreX(), body.getCentreY(), false));
    g.fillEllipse(body.reduced(1.0f));
    g.setColour(theme::outline);
    g.drawEllipse(body, 1.0f);

    const auto tip = centre.getPointOnCircumference(bodyRadius * 0.82f, angle);
    const auto tail = centre.getPointOnCircumference(bodyRadius * (compact ? 0.1f : 0.32f), angle);
    g.setColour(theme::text);
    g.drawLine({ tail, tip }, compact ? 1.6f : juce::jmax(2.0f, bodyRadius * 0.11f));
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position, float minPosition, float maxPosition,
                                   juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, position, minPosition, maxPosition, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto track = bounds.withSizeKeepingCentre(4.0f, bounds.getHeight());
    g.setColour(theme::lcd);
    g.fillRoundedRectangle(track, 2.0f);
    const auto handle = juce::Rectangle<float>(juce::jmin(bounds.getWidth(), 22.0f), 10.0f).withCentre({ bounds.getCentreX(), position });
    g.setGradientFill(juce::ColourGradient(theme::knobTop.brighter(0.2f), handle.getTopLeft(), theme::knobBottom, handle.getBottomLeft(), false));
    g.fillRoundedRectangle(handle, 2.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(handle, 2.0f, 1.0f);
    g.setColour(theme::accent);
    g.drawHorizontalLine(juce::roundToInt(handle.getCentreY()), handle.getX() + 3.0f, handle.getRight() - 3.0f);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto labelled = button.getButtonText().isNotEmpty();
    auto bounds = button.getLocalBounds().toFloat();
    const auto led = (labelled ? bounds.removeFromLeft(18.0f) : bounds).withSizeKeepingCentre(10.0f, 10.0f);
    const auto on = button.getToggleState();
    if (labelled)
    {
        g.setColour(on ? theme::text : theme::textDim);
        g.setFont(theme::font(12.0f, juce::Font::bold));
        g.drawText(button.getButtonText(), bounds.withTrimmedLeft(4.0f), juce::Justification::centredLeft, true);
    }
    if (on)
    {
        g.setColour(theme::led.withAlpha(0.3f));
        g.fillEllipse(led.expanded(2.5f));
    }
    g.setColour(on ? theme::led : theme::ledOff);
    g.fillEllipse(led);
    g.setColour(highlighted ? theme::text : theme::outline);
    g.drawEllipse(led, 1.0f);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto base = button.getToggleState() ? theme::accent : background;
    if (down)
        base = base.darker(0.2f);
    else if (highlighted)
        base = base.brighter(0.08f);

    g.setGradientFill(juce::ColourGradient(base.brighter(0.06f), bounds.getTopLeft(), base.darker(0.12f), bounds.getBottomLeft(), false));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

void LookAndFeel::drawMenuBarBackground(juce::Graphics&, int, int, bool, juce::MenuBarComponent&)
{
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(theme::panel);
    g.setColour(theme::outline);
    g.drawRect(0, 0, width, height);
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return theme::font(13.0f);
}

juce::Font LookAndFeel::getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&)
{
    return theme::font(12.0f, juce::Font::bold);
}

int LookAndFeel::getMenuBarItemWidth(juce::MenuBarComponent&, int, const juce::String& itemText)
{
    return juce::GlyphArrangement::getStringWidthInt(theme::font(12.0f, juce::Font::bold), itemText) + 16;
}

juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return theme::font(juce::jmin(13.0f, float(box.getHeight()) * 0.55f), juce::Font::bold);
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return theme::font(14.0f);
}
}
