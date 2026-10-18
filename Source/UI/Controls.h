#pragma once

#include "Engine/Studio.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts::ui
{
bool startupSoundEnabled();
void setStartupSoundEnabled(bool);
void playStartupSound();
void writeStartupSound(const juce::File&);
int processMemoryMegabytes();

class RightClick final : public juce::MouseListener
{
public:
    RightClick(juce::Component& c, std::function<void()> f) : component(c), action(std::move(f)) { component.addMouseListener(this, true); }
    ~RightClick() override { component.removeMouseListener(this); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            action();
    }

private:
    juce::Component& component;
    std::function<void()> action;
};

struct SnapOption
{
    const char* name;
    int ticks;
};

inline constexpr std::array<SnapOption, 9> snapOptions { {
    { "None", 1 },
    { "1/6 step", 4 },
    { "1/4 step", 6 },
    { "1/3 step", 8 },
    { "1/2 step", 12 },
    { "Step", ticksPerStep },
    { "1/2 beat", ppq / 2 },
    { "Beat", ppq },
    { "Bar", ticksPerBar },
} };

void setupKnob(juce::Slider&, double min, double max, double value, double reset, std::function<void(double)> onChange, const juce::String& tooltip);
void paintLcd(juce::Graphics&, juce::Rectangle<int>, const juce::String& value, const juce::String& label);
void askText(const juce::String& title, const juce::String& current, std::function<void(const juce::String&)> done);
void pickColour(juce::Component& anchor, juce::Colour current, std::function<void(juce::Colour)> done);
void showMessage(const juce::String& title, const juce::String& message);
juce::String keyName(int key);
bool isBlackKey(int key);
int snapDown(double tick, int snap);
int snapNearest(double tick, int snap);
double patternPlayhead(const Studio&);
void addDeviceItems(juce::PopupMenu&, Studio&, bool instruments, std::function<void(std::unique_ptr<Device>)> picked);
void attachToOwner(juce::Component& window, juce::Component& owner);
void registerFileType();
}
