#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ts
{
class ParameterPanel final : public juce::Component
{
public:
    static inline std::function<void(juce::AudioProcessor&, int parameterIndex)> createAutomation;

    explicit ParameterPanel(juce::AudioProcessor&);
    ~ParameterPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Control;

    juce::AudioProcessor& processor;

    std::vector<std::unique_ptr<Control>> controls;
    std::vector<juce::Rectangle<int>> cells;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> headers;
};

class BuiltinEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BuiltinEditor(juce::AudioProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ParameterPanel panel;
    std::unique_ptr<juce::Component> display;
    juce::ComboBox presets;
    int displayArea = 0;
};
}
