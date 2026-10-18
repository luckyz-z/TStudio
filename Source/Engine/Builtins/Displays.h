#pragma once

#include "Engine/Builtins/BuiltinProcessor.h"

#include <juce_dsp/juce_dsp.h>

namespace ts::display
{
inline const juce::Colour background { 0xff121618 };
inline const juce::Colour grid { 0xff252b2f };
inline const juce::Colour trace { 0xffe8483b };
inline const juce::Colour secondary { 0xff7be0a0 };
inline const juce::Colour label { 0xff7f8b90 };

class Display : public juce::Component, private juce::Timer
{
public:
    Display();
    void paint(juce::Graphics&) final;

protected:
    virtual void draw(juce::Graphics&, juce::Rectangle<float> area) = 0;
    virtual void tick() {}

private:
    void timerCallback() override;
};

class Scope final : public Display
{
public:
    explicit Scope(BuiltinProcessor&);

private:
    void draw(juce::Graphics&, juce::Rectangle<float>) override;
    void tick() override;

    BuiltinProcessor& processor;
    std::array<float, 2048> samples {};
};

class Spectrum final : public Display
{
public:
    Spectrum(BuiltinProcessor&, std::function<float(float)> responseDb = {}, float responseRange = 18.0f);
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> overlay;

    float xForFrequency(float frequency, juce::Rectangle<float> area) const;
    float frequencyForX(float x, juce::Rectangle<float> area) const;
    float yForGain(float decibels, juce::Rectangle<float> area) const;
    float gainForY(float y, juce::Rectangle<float> area) const;

private:
    static constexpr int order = 12;
    static constexpr int fftSize = 1 << order;

    void draw(juce::Graphics&, juce::Rectangle<float>) override;
    void tick() override;

    BuiltinProcessor& processor;
    std::function<float(float)> response;
    float range;
    juce::dsp::FFT fft { order };
    juce::dsp::WindowingFunction<float> window { size_t(fftSize), juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> buffer {};
    std::array<float, fftSize / 2> levels {};
};

class Curve final : public Display
{
public:
    Curve(std::function<float(float)> function, juce::Range<float> xRange, juce::Range<float> yRange, std::function<float()> marker = {},
          juce::String xLabel = {}, juce::String yLabel = {});

    bool identity = false;

private:
    void draw(juce::Graphics&, juce::Rectangle<float>) override;

    std::function<float(float)> function;
    juce::Range<float> xRange, yRange;
    std::function<float()> marker;
    juce::String xLabel, yLabel;
};

class Meters final : public Display
{
public:
    struct Meter
    {
        juce::String name;
        std::function<float()> value;
        juce::Colour colour;
    };

    explicit Meters(std::vector<Meter>);

private:
    void draw(juce::Graphics&, juce::Rectangle<float>) override;
    void tick() override;

    std::vector<Meter> meters;
    std::vector<float> levels;
};

class Row final : public juce::Component
{
public:
    void add(std::unique_ptr<juce::Component>, float weight);
    void resized() override;

private:
    std::vector<std::pair<std::unique_ptr<juce::Component>, float>> parts;
};

std::unique_ptr<juce::Component> row(std::unique_ptr<juce::Component> left, float leftWeight, std::unique_ptr<juce::Component> right, float rightWeight);
float decibelsToUnit(float decibels, float floor = -60.0f);
}
