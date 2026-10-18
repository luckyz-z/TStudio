#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position, float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float position, float minPosition, float maxPosition,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    void drawMenuBarBackground(juce::Graphics&, int width, int height, bool mouseOverBar, juce::MenuBarComponent&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getMenuBarFont(juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    juce::Font getPopupMenuFont() override;
    int getMenuBarItemWidth(juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};
}
