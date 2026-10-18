#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace ts
{
struct SampleData
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    juce::String name;
    juce::File file;
    juce::String pack;
};
}
