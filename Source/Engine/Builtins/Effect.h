#pragma once

#include "Engine/Builtins/BuiltinProcessor.h"

#include <juce_dsp/juce_dsp.h>

namespace ts::Builtins
{
class Effect : public BuiltinProcessor
{
public:
    explicit Effect(const juce::String& effectName, bool sidechain = false) : BuiltinProcessor(effectName, false, sidechain) {}

protected:
    static juce::dsp::ProcessSpec specFor(double sampleRate, int blockSize)
    {
        return { sampleRate, juce::uint32(juce::jmax(1, blockSize)), 2 };
    }

    double tempo() const
    {
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
                if (const auto bpm = position->getBpm())
                    return *bpm;
        return 130.0;
    }

    double beats() const
    {
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
                if (const auto ppq = position->getPpqPosition())
                    return *ppq;
        return 0.0;
    }
};

std::unique_ptr<juce::AudioProcessor> createCharacterEffect(const juce::String& name);
std::unique_ptr<juce::AudioProcessor> createEqualizer();
const juce::StringArray& characterEffects();
std::unique_ptr<juce::AudioProcessor> createMotionEffect(const juce::String& name);
const juce::StringArray& motionEffects();
}
