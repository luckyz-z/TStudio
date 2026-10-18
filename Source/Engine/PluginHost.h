#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ts
{
class PluginHost final : private juce::ChangeListener
{
public:
    PluginHost();
    ~PluginHost() override;

    juce::PropertiesFile& settings() { return *properties.getUserSettings(); }
    juce::File deadMansPedal();
    juce::Array<juce::PluginDescription> list(bool instruments) const;
    std::unique_ptr<juce::AudioPluginInstance> create(const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    juce::ApplicationProperties properties;
};
}
