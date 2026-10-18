#include "Engine/Builtins/Displays.h"

#include <cmath>

namespace ts::display
{
namespace
{
constexpr float minimumFrequency = 20.0f;
constexpr float maximumFrequency = 20000.0f;

void frame(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(background);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
}
}

float decibelsToUnit(float decibels, float floor)
{
    return juce::jlimit(0.0f, 1.0f, (decibels - floor) / -floor);
}

Display::Display()
{
    setOpaque(false);
    startTimerHz(30);
}

void Display::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    frame(g, area);
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area.toNearestInt());
    draw(g, area.reduced(6.0f, 4.0f));
}

void Display::timerCallback()
{
    if (!isShowing())
        return;
    tick();
    repaint();
}

Scope::Scope(BuiltinProcessor& p) : processor(p) {}

void Scope::tick()
{
    processor.analysis().copyLatest(samples.data(), int(samples.size()));
}

void Scope::draw(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(grid);
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine(juce::roundToInt(area.getY() + area.getHeight() * float(i) / 4.0f), area.getX(), area.getRight());
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine(juce::roundToInt(area.getX() + area.getWidth() * float(i) / 8.0f), area.getY(), area.getBottom());

    constexpr int shown = 1024;
    auto start = 0;
    for (int i = 1; i < int(samples.size()) - shown; ++i)
    {
        if (samples[size_t(i - 1)] < 0.0f && samples[size_t(i)] >= 0.0f)
        {
            start = i;
            break;
        }
    }
    juce::Path path;
    for (int i = 0; i < shown; ++i)
    {
        const auto x = area.getX() + area.getWidth() * float(i) / float(shown - 1);
        const auto y = area.getCentreY() - juce::jlimit(-1.0f, 1.0f, samples[size_t(start + i)]) * area.getHeight() * 0.48f;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(trace.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(4.0f));
    g.setColour(trace);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

Spectrum::Spectrum(BuiltinProcessor& p, std::function<float(float)> responseDb, float responseRange)
    : processor(p), response(std::move(responseDb)), range(responseRange)
{
    std::fill(levels.begin(), levels.end(), -100.0f);
}

float Spectrum::xForFrequency(float frequency, juce::Rectangle<float> area) const
{
    const auto proportion = std::log(frequency / minimumFrequency) / std::log(maximumFrequency / minimumFrequency);
    return area.getX() + area.getWidth() * proportion;
}

float Spectrum::frequencyForX(float x, juce::Rectangle<float> area) const
{
    const auto proportion = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    return minimumFrequency * std::pow(maximumFrequency / minimumFrequency, proportion);
}

float Spectrum::yForGain(float decibels, juce::Rectangle<float> area) const
{
    return area.getCentreY() - decibels / range * area.getHeight() * 0.5f;
}

float Spectrum::gainForY(float y, juce::Rectangle<float> area) const
{
    return (area.getCentreY() - y) / (area.getHeight() * 0.5f) * range;
}

void Spectrum::tick()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    processor.analysis().copyLatest(buffer.data(), fftSize);
    window.multiplyWithWindowingTable(buffer.data(), size_t(fftSize));
    fft.performFrequencyOnlyForwardTransform(buffer.data());
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto decibels = juce::Decibels::gainToDecibels(buffer[i] * 4.0f / float(fftSize), -100.0f);
        levels[i] = decibels > levels[i] ? decibels : levels[i] * 0.85f + decibels * 0.15f;
    }
}

void Spectrum::draw(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setFont(juce::FontOptions(10.0f));
    for (const auto frequency : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const auto x = xForFrequency(frequency, area);
        g.setColour(grid);
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
        g.setColour(label);
        g.drawText(frequency >= 1000.0f ? juce::String(int(frequency / 1000.0f)) + "k" : juce::String(int(frequency)), juce::Rectangle<float>(x + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f),
                   juce::Justification::left);
    }
    if (response)
    {
        g.setColour(grid);
        for (const auto decibels : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
            g.drawHorizontalLine(juce::roundToInt(yForGain(decibels, area)), area.getX(), area.getRight());
    }

    const auto rate = juce::jmax(1.0, processor.getSampleRate());
    juce::Path spectrum;
    spectrum.startNewSubPath(area.getX(), area.getBottom());
    for (auto x = area.getX(); x <= area.getRight(); x += 2.0f)
    {
        const auto frequency = frequencyForX(x, area);
        const auto bin = juce::jlimit(1, int(levels.size()) - 1, int(frequency / float(rate) * float(fftSize)));
        const auto level = juce::jlimit(0.0f, 1.0f, (levels[size_t(bin)] + 90.0f) / 90.0f);
        spectrum.lineTo(x, area.getBottom() - level * area.getHeight());
    }
    spectrum.lineTo(area.getRight(), area.getBottom());
    spectrum.closeSubPath();
    g.setGradientFill(juce::ColourGradient(secondary.withAlpha(0.45f), 0.0f, area.getY(), secondary.withAlpha(0.05f), 0.0f, area.getBottom(), false));
    g.fillPath(spectrum);

    if (response)
    {
        juce::Path curve;
        for (auto x = area.getX(); x <= area.getRight(); x += 1.5f)
        {
            const auto y = juce::jlimit(area.getY(), area.getBottom(), yForGain(response(frequencyForX(x, area)), area));
            if (x == area.getX())
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
        }
        g.setColour(trace.withAlpha(0.25f));
        g.strokePath(curve, juce::PathStrokeType(5.0f));
        g.setColour(trace);
        g.strokePath(curve, juce::PathStrokeType(2.0f));
    }
    if (overlay)
        overlay(g, area);
}

Curve::Curve(std::function<float(float)> f, juce::Range<float> x, juce::Range<float> y, std::function<float()> m, juce::String xName, juce::String yName)
    : function(std::move(f)), xRange(x), yRange(y), marker(std::move(m)), xLabel(std::move(xName)), yLabel(std::move(yName))
{
}

void Curve::draw(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto toX = [&](float value) { return area.getX() + (value - xRange.getStart()) / xRange.getLength() * area.getWidth(); };
    const auto toY = [&](float value) { return area.getBottom() - (value - yRange.getStart()) / yRange.getLength() * area.getHeight(); };

    g.setColour(grid);
    for (int i = 1; i < 4; ++i)
    {
        g.drawHorizontalLine(juce::roundToInt(area.getY() + area.getHeight() * float(i) / 4.0f), area.getX(), area.getRight());
        g.drawVerticalLine(juce::roundToInt(area.getX() + area.getWidth() * float(i) / 4.0f), area.getY(), area.getBottom());
    }
    g.setColour(grid.brighter(0.2f));
    if (identity)
        g.drawLine(toX(xRange.getStart()), toY(juce::jlimit(yRange.getStart(), yRange.getEnd(), xRange.getStart())), toX(xRange.getEnd()),
               toY(juce::jlimit(yRange.getStart(), yRange.getEnd(), xRange.getEnd())), 1.0f);

    juce::Path path;
    for (int i = 0; i <= 200; ++i)
    {
        const auto x = xRange.getStart() + xRange.getLength() * float(i) / 200.0f;
        const auto px = toX(x);
        const auto py = juce::jlimit(area.getY(), area.getBottom(), toY(function(x)));
        if (i == 0)
            path.startNewSubPath(px, py);
        else
            path.lineTo(px, py);
    }
    g.setColour(trace.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(5.0f));
    g.setColour(trace);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    if (marker)
    {
        const auto x = juce::jlimit(xRange.getStart(), xRange.getEnd(), marker());
        const auto point = juce::Point<float>(toX(x), juce::jlimit(area.getY(), area.getBottom(), toY(function(x))));
        g.setColour(secondary);
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(point));
    }
    g.setColour(label);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(xLabel, area.removeFromBottom(12.0f), juce::Justification::right);
    g.drawText(yLabel, area.removeFromTop(12.0f), juce::Justification::left);
}

Meters::Meters(std::vector<Meter> list) : meters(std::move(list)), levels(meters.size(), 0.0f) {}

void Meters::tick()
{
    for (size_t i = 0; i < meters.size(); ++i)
        levels[i] = juce::jmax(meters[i].value(), levels[i] * 0.86f);
}

void Meters::draw(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto width = area.getWidth() / float(juce::jmax<size_t>(1, meters.size()));
    for (size_t i = 0; i < meters.size(); ++i)
    {
        auto column = juce::Rectangle<float>(area.getX() + width * float(i), area.getY(), width, area.getHeight()).reduced(5.0f, 0.0f);
        const auto text = column.removeFromBottom(14.0f);
        g.setColour(grid);
        g.fillRoundedRectangle(column, 2.0f);
        g.setColour(meters[i].colour);
        g.fillRoundedRectangle(column.withTrimmedTop(column.getHeight() * (1.0f - juce::jlimit(0.0f, 1.0f, levels[i]))), 2.0f);
        g.setColour(label);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(meters[i].name, text, juce::Justification::centred);
    }
}

void Row::add(std::unique_ptr<juce::Component> component, float weight)
{
    addAndMakeVisible(*component);
    parts.emplace_back(std::move(component), weight);
}

void Row::resized()
{
    auto total = 0.0f;
    for (const auto& part : parts)
        total += part.second;
    auto area = getLocalBounds();
    const auto width = float(area.getWidth());
    for (auto& [component, weight] : parts)
        component->setBounds(area.removeFromLeft(juce::roundToInt(width * weight / total)));
}

std::unique_ptr<juce::Component> row(std::unique_ptr<juce::Component> left, float leftWeight, std::unique_ptr<juce::Component> right, float rightWeight)
{
    auto result = std::make_unique<Row>();
    result->add(std::move(left), leftWeight);
    result->add(std::move(right), rightWeight);
    return result;
}
}
