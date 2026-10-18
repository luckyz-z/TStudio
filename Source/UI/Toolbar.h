#pragma once

#include "Engine/Studio.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class Toolbar final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    static constexpr int rowHeight = 32;
    int menuWidth = 344;

    Toolbar(Studio&, juce::ApplicationCommandManager&);
    ~Toolbar() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void(int snapIndex)> onGlobalSnap;

private:
    class Icon;
    class TempoDisplay;
    class PatternBox;
    class ModeSwitch;
    class Oscilloscope;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    Icon& addIcon(std::unique_ptr<Icon>);

    Studio& studio;
    juce::ApplicationCommandManager& commands;
    std::unique_ptr<ModeSwitch> mode;
    std::unique_ptr<TempoDisplay> tempo;
    std::unique_ptr<PatternBox> patterns;
    std::unique_ptr<Oscilloscope> scope;
    std::vector<std::unique_ptr<Icon>> icons;
    Icon *play = nullptr, *stop = nullptr, *record = nullptr, *metronome = nullptr, *typing = nullptr, *newPattern = nullptr;
    std::vector<Icon*> transport, toggles, views, quick;
    juce::Slider master, pitch;
    juce::ComboBox snap;
    juce::Image logo;
    juce::Rectangle<int> logoArea, hintArea, positionArea, meterArea, statsArea, infoArea;
    juce::String hint, position, cpu;
    float meterLeft = 0.0f, meterRight = 0.0f;
};
}
