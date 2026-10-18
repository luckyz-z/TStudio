#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ts::NativePlugins
{
juce::StringArray names(bool instruments);
std::unique_ptr<juce::AudioProcessor> create(const juce::String& name);
juce::File userFolder();
juce::String scanReport();
}
