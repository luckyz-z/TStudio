#pragma once

#include "Engine/Studio.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class ChannelSettings final : public juce::Component
{
public:
    ChannelSettings(Studio&, int channelId);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ChannelOptions* options() const;
    void refresh();
    void changed();

    Studio& studio;
    const int id;
    juce::Slider shift, swing, portaTime, gate;
    juce::ToggleButton mono { "Mono" }, porta { "Porta" };
    juce::ComboBox cutGroup, cutBy, arp, arpTime, arpRange;
};
}
