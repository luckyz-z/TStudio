#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace ts::Builtins
{
juce::StringArray generators();
juce::StringArray effects();
std::unique_ptr<juce::AudioProcessor> createEffect(const juce::String& name);
std::unique_ptr<juce::AudioProcessor> createInstrument(const juce::String& name);
std::unique_ptr<juce::AudioProcessor> create(const juce::String& name);
}
