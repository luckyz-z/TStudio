#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace ts
{
class Mp3Format final : public juce::AudioFormat
{
public:
    Mp3Format();

    juce::Array<int> getPossibleSampleRates() override { return { 32000, 44100, 48000 }; }
    juce::Array<int> getPossibleBitDepths() override { return { 16, 24, 32 }; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    bool isCompressed() override { return true; }
    juce::StringArray getQualityOptions() override { return { "128 kbps", "192 kbps", "256 kbps", "320 kbps" }; }

    juce::AudioFormatReader* createReaderFor(juce::InputStream*, bool) override { return nullptr; }
    juce::AudioFormatWriter* createWriterFor(juce::OutputStream*, double sampleRate, unsigned int numChannels, int bitsPerSample,
                                             const juce::StringPairArray& metadata, int qualityOptionIndex) override;
    using AudioFormat::createWriterFor;

private:
    class Writer;
};
}
