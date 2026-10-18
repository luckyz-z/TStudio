#pragma once

#include "Engine/Builtins/BuiltinProcessor.h"

#include <array>

namespace ts
{
class Synth final : public BuiltinProcessor
{
public:
    static constexpr const char* typeName = "Tomato Synth";
    static constexpr int oscillatorCount = 3;
    static constexpr int maxUnison = 7;

    Synth();
    ~Synth() override;

    void prepareToPlay(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    std::unique_ptr<juce::Component> createDisplay() override;

private:
    class Voice;

    struct Oscillator
    {
        juce::AudioParameterChoice* shape = nullptr;
        juce::AudioParameterFloat* coarse = nullptr;
        juce::AudioParameterFloat* fine = nullptr;
        juce::AudioParameterFloat* level = nullptr;
    };

    struct Settings
    {
        std::array<int, oscillatorCount> shape {};
        std::array<double, oscillatorCount> ratio {};
        std::array<float, oscillatorCount> level {};
        juce::ADSR::Parameters envelope;
        float cutoff = 20000.0f;
        float resonance = 0.0f;
        float filterEnvelope = 0.0f;
        float volume = 0.6f;
        float glide = 0.0f;
        float vibratoRate = 5.0f;
        float vibratoDepth = 0.0f;
        int unison = 1;
        std::array<double, maxUnison> detune {};
        std::array<float, maxUnison> left {}, right {};
    };

    std::array<Oscillator, oscillatorCount> oscillators;
    juce::AudioParameterFloat* attack;
    juce::AudioParameterFloat* decay;
    juce::AudioParameterFloat* sustain;
    juce::AudioParameterFloat* release;
    juce::AudioParameterFloat* cutoff;
    juce::AudioParameterFloat* resonance;
    juce::AudioParameterFloat* filterEnvelope;
    juce::AudioParameterFloat* volume;
    juce::AudioParameterFloat* glide;
    juce::AudioParameterFloat* vibratoRate;
    juce::AudioParameterFloat* vibratoDepth;
    juce::AudioParameterInt* unison;
    juce::AudioParameterFloat* unisonDetune;
    juce::AudioParameterFloat* unisonSpread;
    Settings settings;
    double lastFrequency = 0.0;
    juce::Synthesiser synth;
    juce::MidiBuffer chunk;
    Voice* lastStarted = nullptr;
    int slideSource = -1;
    double slideMilliseconds = 100.0;
};
}
