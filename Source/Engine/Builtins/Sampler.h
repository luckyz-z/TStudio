#pragma once

#include "Engine/Builtins/BuiltinProcessor.h"
#include "Engine/SampleData.h"

#include <memory>

namespace ts
{
class Sampler final : public BuiltinProcessor
{
public:
    static constexpr const char* typeName = "Sampler";

    Sampler();
    ~Sampler() override;

    void setSample(std::shared_ptr<const SampleData>);
    std::shared_ptr<const SampleData> getSample() const;
    bool loadFile(const juce::File&);
    static std::shared_ptr<const SampleData> readFile(const juce::File&);
    static bool isAudioFile(const juce::File&);

    void prepareToPlay(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

protected:
    void saveExtra(juce::XmlElement&) const override;
    void loadExtra(const juce::XmlElement&) override;

private:
    class Voice;
    class Editor;

    juce::Synthesiser synth;
    mutable juce::SpinLock sampleLock;
    std::shared_ptr<const SampleData> sample;
    juce::AudioParameterInt& root;
    juce::AudioParameterFloat& pitch;
    juce::AudioParameterFloat& attack;
    juce::AudioParameterFloat& release;
    juce::AudioParameterBool& cut;
    juce::AudioParameterBool& reverse;
};
}
