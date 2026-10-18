#include "Engine/Builtins/Effect.h"
#include "Engine/Builtins/Displays.h"

#include <array>
#include <cmath>

namespace ts::Builtins
{
namespace
{
enum class BandType
{
    lowCut,
    lowShelf,
    peak,
    highShelf,
    highCut
};

struct BandSpec
{
    BandType type;
    float frequency;
    float q;
};

constexpr std::array<BandSpec, 7> specs { {
    { BandType::lowCut, 30.0f, 0.71f },
    { BandType::lowShelf, 90.0f, 0.71f },
    { BandType::peak, 250.0f, 1.0f },
    { BandType::peak, 1000.0f, 1.0f },
    { BandType::peak, 3500.0f, 1.0f },
    { BandType::highShelf, 9000.0f, 0.71f },
    { BandType::highCut, 18000.0f, 0.71f },
} };

const std::array<juce::Colour, 7> bandColours {
    juce::Colour(0xffe8483b), juce::Colour(0xffe8903b), juce::Colour(0xffe8d03b), juce::Colour(0xff7be0a0),
    juce::Colour(0xff4fb3e8), juce::Colour(0xff8f7be8), juce::Colour(0xffe87bc8),
};

bool isCut(BandType type)
{
    return type == BandType::lowCut || type == BandType::highCut;
}

class Equalizer final : public Effect
{
public:
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    struct Band
    {
        BandType type;
        juce::AudioParameterBool* on;
        juce::AudioParameterFloat* frequency;
        juce::AudioParameterFloat* gain;
        juce::AudioParameterFloat* q;
    };

    Equalizer() : Effect("Tomato EQ")
    {
        for (size_t i = 0; i < specs.size(); ++i)
        {
            const auto& spec = specs[i];
            const auto prefix = "band" + juce::String(i + 1);
            const auto label = "Band " + juce::String(i + 1) + " ";
            Band band { spec.type, nullptr, nullptr, nullptr, nullptr };
            band.on = &addBool(prefix + "On", label + "on", !isCut(spec.type));
            band.frequency = &addFloat(prefix + "Freq", label + "freq", skewed(20.0f, 20000.0f, 1000.0f), spec.frequency, "Hz");
            if (!isCut(spec.type))
                band.gain = &addFloat(prefix + "Gain", label + "gain", { -18.0f, 18.0f }, 0.0f, "dB");
            band.q = &addFloat(prefix + "Q", label + "Q", skewed(0.1f, 18.0f, 1.0f), spec.q);
            bands[i] = band;
        }
        output = &addFloat("output", "Output", { -18.0f, 18.0f }, 0.0f, "dB");

        addPreset("Low cut 80 Hz", { { "band1On", 1.0f }, { "band1Freq", 80.0f } });
        addPreset("Kill the mud", { { "band1On", 1.0f }, { "band1Freq", 40.0f }, { "band3Freq", 320.0f }, { "band3Gain", -4.5f }, { "band3Q", 1.3f } });
        addPreset("Air", { { "band6Freq", 11000.0f }, { "band6Gain", 4.0f } });
        addPreset("Vocal presence", { { "band1On", 1.0f }, { "band1Freq", 100.0f }, { "band3Freq", 300.0f }, { "band3Gain", -2.5f }, { "band5Freq", 3500.0f },
                                      { "band5Gain", 3.0f }, { "band6Freq", 12000.0f }, { "band6Gain", 2.0f } });
        addPreset("808 punch", { { "band2Freq", 55.0f }, { "band2Gain", 3.5f }, { "band3Freq", 280.0f }, { "band3Gain", -3.0f }, { "band5Freq", 2000.0f }, { "band5Gain", 2.0f } });
        addPreset("Telephone", { { "band1On", 1.0f }, { "band1Freq", 450.0f }, { "band1Q", 1.2f }, { "band7On", 1.0f }, { "band7Freq", 2800.0f }, { "band7Q", 1.2f },
                                 { "band4Freq", 1400.0f }, { "band4Gain", 5.0f } });
        addPreset("Smile", { { "band2Freq", 80.0f }, { "band2Gain", 3.0f }, { "band4Freq", 900.0f }, { "band4Gain", -2.0f }, { "band6Freq", 9000.0f }, { "band6Gain", 3.0f } });
    }

    Coefficients::Ptr coefficients(const Band& band, double sampleRate) const
    {
        const auto frequency = juce::jmin(band.frequency->get(), float(sampleRate) * 0.45f);
        const auto q = band.q->get();
        switch (band.type)
        {
            case BandType::lowCut: return Coefficients::makeHighPass(sampleRate, frequency, q);
            case BandType::highCut: return Coefficients::makeLowPass(sampleRate, frequency, q);
            case BandType::lowShelf: return Coefficients::makeLowShelf(sampleRate, frequency, q, juce::Decibels::decibelsToGain(band.gain->get()));
            case BandType::highShelf: return Coefficients::makeHighShelf(sampleRate, frequency, q, juce::Decibels::decibelsToGain(band.gain->get()));
            case BandType::peak: return Coefficients::makePeakFilter(sampleRate, frequency, q, juce::Decibels::decibelsToGain(band.gain->get()));
        }
        return nullptr;
    }

    float responseDb(float frequency) const
    {
        const auto currentRate = juce::jmax(8000.0, getSampleRate());
        auto magnitude = 1.0;
        for (const auto& band : bands)
            if (band.on->get())
                magnitude *= coefficients(band, currentRate)->getMagnitudeForFrequency(frequency, currentRate);
        return float(juce::Decibels::gainToDecibels(magnitude, -60.0)) + output->get();
    }

    const std::array<Band, 7>& getBands() const noexcept { return bands; }

    std::unique_ptr<juce::Component> createDisplay() override;

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        cached.fill(-1.0f);
        for (auto& channel : filters)
            for (auto& filter : channel)
                filter.reset();
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        update();
        juce::dsp::AudioBlock<float> block(buffer);
        for (size_t c = 0; c < filters.size(); ++c)
        {
            auto channel = block.getSingleChannelBlock(c);
            for (size_t b = 0; b < bands.size(); ++b)
                if (bands[b].on->get())
                    filters[c][b].process(juce::dsp::ProcessContextReplacing<float>(channel));
        }
        buffer.applyGain(juce::Decibels::decibelsToGain(output->get()));
    }

private:
    void update()
    {
        for (size_t b = 0; b < bands.size(); ++b)
        {
            const auto& band = bands[b];
            const std::array<float, 3> values { band.frequency->get(), band.gain != nullptr ? band.gain->get() : 0.0f, band.q->get() };
            if (std::equal(values.begin(), values.end(), cached.begin() + std::ptrdiff_t(b * 3)))
                continue;
            std::copy(values.begin(), values.end(), cached.begin() + std::ptrdiff_t(b * 3));
            const auto next = coefficients(band, rate);
            for (auto& channel : filters)
                *channel[b].coefficients = *next;
        }
    }

    std::array<Band, 7> bands {};
    juce::AudioParameterFloat* output = nullptr;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 7>, 2> filters;
    std::array<float, 21> cached {};
    double rate = 44100.0;
};

class EqDisplay final : public juce::Component
{
public:
    explicit EqDisplay(Equalizer& e) : eq(e), spectrum(e, [&e](float f) { return e.responseDb(f); })
    {
        spectrum.overlay = [this](juce::Graphics& g, juce::Rectangle<float>) { drawNodes(g); };
        spectrum.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(spectrum);
        setTooltip("Drag the dots to set frequency and gain, mouse wheel for Q, right-click to switch a band on/off, double-click to reset");
    }

    void resized() override { spectrum.setBounds(getLocalBounds()); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging = nodeAt(e.position);
        if (dragging < 0)
            return;
        auto& band = eq.getBands()[size_t(dragging)];
        if (e.mods.isPopupMenu())
        {
            band.on->setValueNotifyingHost(band.on->get() ? 0.0f : 1.0f);
            dragging = -1;
            return;
        }
        band.frequency->beginChangeGesture();
        if (band.gain != nullptr)
            band.gain->beginChangeGesture();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        const auto& band = eq.getBands()[size_t(dragging)];
        const auto area = plotArea();
        band.frequency->setValueNotifyingHost(band.frequency->convertTo0to1(juce::jlimit(20.0f, 20000.0f, spectrum.frequencyForX(e.position.x, area))));
        if (band.gain != nullptr)
            band.gain->setValueNotifyingHost(band.gain->convertTo0to1(juce::jlimit(-18.0f, 18.0f, spectrum.gainForY(e.position.y, area))));
        if (!band.on->get())
            band.on->setValueNotifyingHost(1.0f);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging < 0)
            return;
        const auto& band = eq.getBands()[size_t(dragging)];
        band.frequency->endChangeGesture();
        if (band.gain != nullptr)
            band.gain->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (const auto index = nodeAt(e.position); index >= 0)
        {
            const auto& band = eq.getBands()[size_t(index)];
            if (band.gain != nullptr)
                band.gain->setValueNotifyingHost(band.gain->convertTo0to1(0.0f));
            band.q->setValueNotifyingHost(band.q->convertTo0to1(specs[size_t(index)].q));
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (const auto index = nodeAt(e.position); index >= 0)
        {
            const auto& band = eq.getBands()[size_t(index)];
            const auto q = juce::jlimit(0.1f, 18.0f, band.q->get() * (wheel.deltaY > 0.0f ? 1.15f : 1.0f / 1.15f));
            band.q->setValueNotifyingHost(band.q->convertTo0to1(q));
        }
    }

    void setTooltip(const juce::String& text) { tooltip = text; }

private:
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced(2.0f).reduced(6.0f, 4.0f);
    }

    juce::Point<float> nodePosition(size_t index) const
    {
        const auto& band = eq.getBands()[index];
        const auto area = plotArea();
        const auto gain = band.gain != nullptr ? band.gain->get() : 0.0f;
        return { spectrum.xForFrequency(band.frequency->get(), area), juce::jlimit(area.getY(), area.getBottom(), spectrum.yForGain(gain, area)) };
    }

    int nodeAt(juce::Point<float> point) const
    {
        for (size_t i = 0; i < eq.getBands().size(); ++i)
            if (nodePosition(i).getDistanceFrom(point) <= 9.0f)
                return int(i);
        return -1;
    }

    void drawNodes(juce::Graphics& g) const
    {
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (size_t i = 0; i < eq.getBands().size(); ++i)
        {
            const auto position = nodePosition(i);
            const auto bounds = juce::Rectangle<float>(16.0f, 16.0f).withCentre(position);
            const auto on = eq.getBands()[i].on->get();
            g.setColour(bandColours[i].withAlpha(on ? 1.0f : 0.35f));
            if (on)
                g.fillEllipse(bounds);
            else
                g.drawEllipse(bounds.reduced(1.0f), 1.5f);
            g.setColour(on ? juce::Colours::black : bandColours[i]);
            g.drawText(juce::String(int(i + 1)), bounds, juce::Justification::centred);
        }
    }

    Equalizer& eq;
    display::Spectrum spectrum;
    juce::String tooltip;
    int dragging = -1;
};

std::unique_ptr<juce::Component> Equalizer::createDisplay()
{
    return std::make_unique<EqDisplay>(*this);
}
}

std::unique_ptr<juce::AudioProcessor> createEqualizer()
{
    return std::make_unique<Equalizer>();
}
}
