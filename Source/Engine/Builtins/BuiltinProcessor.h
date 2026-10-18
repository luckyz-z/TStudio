#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace ts
{
class ScopeBuffer final
{
public:
    static constexpr int size = 8192;

    void push(const juce::AudioBuffer<float>& buffer) noexcept;
    void copyLatest(float* destination, int count) const noexcept;
    float takePeak(int channel) noexcept { return peaks[size_t(channel)].exchange(0.0f, std::memory_order_relaxed); }

private:
    std::array<float, size> data {};
    std::atomic<int> write { 0 };
    std::array<std::atomic<float>, 2> peaks {};
};

class BuiltinProcessor : public juce::AudioProcessor
{
public:
    BuiltinProcessor(juce::String name, bool instrument, bool sidechain = false);

    const juce::String getName() const override { return name; }
    bool acceptsMidi() const override { return instrument; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return int(presets.size()) + 1; }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    void releaseResources() override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int size) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) final;

    virtual std::unique_ptr<juce::Component> createDisplay() { return nullptr; }
    virtual bool showsInPanel(const juce::String&) const { return true; }
    ScopeBuffer& analysis() noexcept { return scope; }
    juce::RangedAudioParameter* parameter(const juce::String& id) const;

protected:
    virtual void process(juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;
    virtual void saveExtra(juce::XmlElement&) const {}
    virtual void loadExtra(const juce::XmlElement&) {}

    juce::AudioParameterFloat& addFloat(const juce::String& id, const juce::String& label, juce::NormalisableRange<float> range, float value,
                                        const juce::String& unit = {});
    juce::AudioParameterChoice& addChoice(const juce::String& id, const juce::String& label, const juce::StringArray& choices, int value);
    juce::AudioParameterBool& addBool(const juce::String& id, const juce::String& label, bool value);
    juce::AudioParameterInt& addInt(const juce::String& id, const juce::String& label, int min, int max, int value);
    void addPreset(const juce::String& presetName, std::initializer_list<std::pair<const char*, double>> values);
    void addPresetValues(const juce::String& presetName, const std::vector<std::pair<juce::String, double>>& values);

    static BusesProperties busesFor(bool instrument, bool sidechain = false);
    const float* sidechainChannel(const juce::AudioBuffer<float>&, int channel) const;
    static juce::NormalisableRange<float> skewed(float min, float max, float centre);

private:
    struct Preset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    juce::String name;
    bool instrument;
    ScopeBuffer scope;
    std::vector<Preset> presets;
    int currentPreset = 0;
};
}
