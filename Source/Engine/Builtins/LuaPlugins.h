#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ts::LuaPlugins
{
struct Entry
{
    juce::String name;
    juce::String description;
    juce::String code;
    juce::String source;
    bool instrument = false;
};

const std::vector<Entry>& all();
void rescan();
juce::StringArray names(bool instruments);
std::unique_ptr<juce::AudioProcessor> create(const juce::String& name);
juce::File userFolder();
juce::String scanReport();
}
