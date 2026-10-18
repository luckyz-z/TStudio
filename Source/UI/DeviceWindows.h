#pragma once

#include "Engine/Project.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class DeviceWindows final
{
public:
    explicit DeviceWindows(juce::Component& owner);
    ~DeviceWindows();

    void show(Device&, const juce::String& title);
    void close(juce::AudioProcessor*);
    void closeAll();

private:
    class Window;

    juce::Component& owner;
    std::vector<std::unique_ptr<Window>> windows;
};
}
