#pragma once

#include "Engine/Project.h"

#include <functional>

namespace ts
{
struct Actions
{
    std::function<void(int channelId)> openPianoRoll;
    std::function<void(Device&, const juce::String& title)> showDevice;
    std::function<void(int track)> showMixerTrack;
    std::function<void()> showChannelRack;
    std::function<void(int channelId)> showChannelSettings;
    std::function<bool()> typingKeyboardActive;
    std::function<void(const juce::String& builtinScript)> runScript;
    std::function<void(const AutomationTarget&, float value)> createAutomation;
};
}
