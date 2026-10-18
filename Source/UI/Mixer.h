#pragma once

#include "Engine/Studio.h"
#include "UI/Actions.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class Mixer final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    Mixer(Studio&, Actions&);
    ~Mixer() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void selectTrack(int track);
    bool keyPressed(const juce::KeyPress&) override;

private:
    class Strip;
    class Slots;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    Studio& studio;
    Actions& actions;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Strip> strips;
    std::unique_ptr<Slots> slots;
    int selected = 0;
};
}
