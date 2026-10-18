#include "Engine/Builtins/BuiltinProcessor.h"
#include "Engine/Builtins/ParameterPanel.h"

namespace ts
{
juce::AudioProcessor::BusesProperties BuiltinProcessor::busesFor(bool instrument, bool sidechain)
{
    const auto stereo = juce::AudioChannelSet::stereo();
    if (instrument)
        return juce::AudioProcessor::BusesProperties().withOutput("Output", stereo, true);
    auto buses = juce::AudioProcessor::BusesProperties().withInput("Input", stereo, true).withOutput("Output", stereo, true);
    return sidechain ? buses.withInput("Sidechain", stereo, true) : buses;
}

BuiltinProcessor::BuiltinProcessor(juce::String processorName, bool isInstrument, bool sidechain)
    : AudioProcessor(busesFor(isInstrument, sidechain)), name(std::move(processorName)), instrument(isInstrument)
{
}

const float* BuiltinProcessor::sidechainChannel(const juce::AudioBuffer<float>& buffer, int channel) const
{
    return buffer.getNumChannels() >= 4 ? buffer.getReadPointer(2 + juce::jlimit(0, 1, channel)) : nullptr;
}

void ScopeBuffer::push(const juce::AudioBuffer<float>& buffer) noexcept
{
    const auto channels = buffer.getNumChannels();
    if (channels == 0)
        return;
    const auto* left = buffer.getReadPointer(0);
    const auto* right = buffer.getReadPointer(juce::jmin(1, channels - 1));
    auto position = write.load(std::memory_order_relaxed);
    auto peakLeft = 0.0f, peakRight = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        data[size_t(position)] = (left[i] + right[i]) * 0.5f;
        position = (position + 1) % size;
        peakLeft = juce::jmax(peakLeft, std::abs(left[i]));
        peakRight = juce::jmax(peakRight, std::abs(right[i]));
    }
    write.store(position, std::memory_order_release);
    if (peakLeft > peaks[0].load(std::memory_order_relaxed))
        peaks[0].store(peakLeft, std::memory_order_relaxed);
    if (peakRight > peaks[1].load(std::memory_order_relaxed))
        peaks[1].store(peakRight, std::memory_order_relaxed);
}

void ScopeBuffer::copyLatest(float* destination, int count) const noexcept
{
    count = juce::jmin(count, size);
    const auto end = write.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        destination[i] = data[size_t((end - count + i + size) % size)];
}

void BuiltinProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    process(buffer, midi);
    scope.push(buffer);
}

juce::RangedAudioParameter* BuiltinProcessor::parameter(const juce::String& id) const
{
    for (auto* candidate : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(candidate); ranged != nullptr && ranged->getParameterID() == id)
            return ranged;
    return nullptr;
}

void BuiltinProcessor::addPreset(const juce::String& presetName, std::initializer_list<std::pair<const char*, double>> values)
{
    Preset preset { presetName, {} };
    for (const auto& [id, value] : values)
        preset.values.emplace_back(id, float(value));
    presets.push_back(std::move(preset));
}

void BuiltinProcessor::addPresetValues(const juce::String& presetName, const std::vector<std::pair<juce::String, double>>& values)
{
    Preset preset { presetName, {} };
    for (const auto& [id, value] : values)
        preset.values.emplace_back(id, float(value));
    presets.push_back(std::move(preset));
}

void BuiltinProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index > int(presets.size()))
        return;
    currentPreset = index;
    for (auto* candidate : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(candidate))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
    if (index == 0)
        return;
    for (const auto& [id, value] : presets[size_t(index - 1)].values)
        if (auto* ranged = parameter(id))
            ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
}

const juce::String BuiltinProcessor::getProgramName(int index)
{
    if (index <= 0 || index > int(presets.size()))
        return "Init";
    return presets[size_t(index - 1)].name;
}

juce::AudioProcessorEditor* BuiltinProcessor::createEditor()
{
    return new BuiltinEditor(*this);
}

void BuiltinProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    juce::XmlElement state("State");
    for (auto* parameter : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            auto* entry = state.createNewChildElement("Parameter");
            entry->setAttribute("id", ranged->getParameterID());
            entry->setAttribute("value", ranged->getValue());
        }
    }
    saveExtra(state);
    copyXmlToBinary(state, destination);
}

void BuiltinProcessor::setStateInformation(const void* data, int size)
{
    const auto state = getXmlFromBinary(data, size);
    if (state == nullptr)
        return;
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if (auto* saved = state->getChildByAttribute("id", ranged->getParameterID()))
                ranged->setValueNotifyingHost(float(saved->getDoubleAttribute("value", ranged->getValue())));
    loadExtra(*state);
}

juce::AudioParameterFloat& BuiltinProcessor::addFloat(const juce::String& id, const juce::String& label, juce::NormalisableRange<float> range,
                                                     float value, const juce::String& unit)
{
    auto* parameter = new juce::AudioParameterFloat(juce::ParameterID { id, 1 }, label, range, value, juce::AudioParameterFloatAttributes().withLabel(unit));
    addParameter(parameter);
    return *parameter;
}

juce::AudioParameterChoice& BuiltinProcessor::addChoice(const juce::String& id, const juce::String& label, const juce::StringArray& choices, int value)
{
    auto* parameter = new juce::AudioParameterChoice(juce::ParameterID { id, 1 }, label, choices, value);
    addParameter(parameter);
    return *parameter;
}

juce::AudioParameterBool& BuiltinProcessor::addBool(const juce::String& id, const juce::String& label, bool value)
{
    auto* parameter = new juce::AudioParameterBool(juce::ParameterID { id, 1 }, label, value);
    addParameter(parameter);
    return *parameter;
}

juce::AudioParameterInt& BuiltinProcessor::addInt(const juce::String& id, const juce::String& label, int min, int max, int value)
{
    auto* parameter = new juce::AudioParameterInt(juce::ParameterID { id, 1 }, label, min, max, value);
    addParameter(parameter);
    return *parameter;
}

juce::NormalisableRange<float> BuiltinProcessor::skewed(float min, float max, float centre)
{
    juce::NormalisableRange<float> range(min, max);
    range.setSkewForCentre(centre);
    return range;
}
}
