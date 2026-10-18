#include "Engine/Builtins/Effect.h"
#include "Engine/Builtins/Displays.h"

#include <array>
#include <cmath>

namespace ts::Builtins
{
namespace
{
using display::Curve;
using display::Meters;
using display::Scope;
using display::Spectrum;

constexpr double twoPi = juce::MathConstants<double>::twoPi;

double beatsPerSample(double bpm, double rate)
{
    return bpm / 60.0 / rate;
}

void track(std::atomic<float>& value, float sample) noexcept
{
    if (sample > value.load(std::memory_order_relaxed))
        value.store(sample, std::memory_order_relaxed);
}

float unit(std::atomic<float>& value)
{
    return display::decibelsToUnit(juce::Decibels::gainToDecibels(value.exchange(0.0f, std::memory_order_relaxed), -60.0f));
}

class Maximizer final : public Effect
{
public:
    Maximizer()
        : Effect("Tomato Maximizer"),
          mode(addChoice("mode", "Mode", { "A - Balanced", "B - Punch", "C - Bright", "D - Loud" }, 0)),
          amount(addFloat("amount", "Amount", { 0.0f, 1.0f }, 0.5f)),
          output(addFloat("output", "Output", { -18.0f, 6.0f }, 0.0f, "dB"))
    {
        addPreset("A gentle", { { "mode", 0.0f }, { "amount", 0.3f } });
        addPreset("B punchy drums", { { "mode", 1.0f }, { "amount", 0.5f } });
        addPreset("C bright vocal", { { "mode", 2.0f }, { "amount", 0.45f } });
        addPreset("D loud master", { { "mode", 3.0f }, { "amount", 0.7f }, { "output", -1.0f } });
    }

    void prepareToPlay(double sampleRate, int samples) override
    {
        const auto spec = specFor(sampleRate, samples);
        for (auto* band : { &low, &mid, &high })
            band->setSize(2, samples, false, false, true);
        const auto setup = [&spec](juce::dsp::LinkwitzRileyFilter<float>& filter, juce::dsp::LinkwitzRileyFilterType type, float frequency)
        {
            filter.setType(type);
            filter.setCutoffFrequency(frequency);
            filter.prepare(spec);
            filter.reset();
        };
        setup(lowSplit, juce::dsp::LinkwitzRileyFilterType::lowpass, 200.0f);
        setup(restSplit, juce::dsp::LinkwitzRileyFilterType::highpass, 200.0f);
        setup(midSplit, juce::dsp::LinkwitzRileyFilterType::lowpass, 3200.0f);
        setup(highSplit, juce::dsp::LinkwitzRileyFilterType::highpass, 3200.0f);
        for (auto& compressor : compressors)
        {
            compressor.prepare(spec);
            compressor.reset();
        }
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        static constexpr std::array<std::array<float, 3>, 4> weights { { { 1.0f, 1.0f, 1.0f }, { 1.3f, 0.9f, 0.9f }, { 0.9f, 1.0f, 1.35f }, { 1.3f, 1.2f, 1.3f } } };
        const auto n = buffer.getNumSamples();
        const auto a = amount.get();
        const auto& weight = weights[size_t(mode.getIndex())];

        for (int c = 0; c < 2; ++c)
        {
            low.copyFrom(c, 0, buffer, c, 0, n);
            mid.copyFrom(c, 0, buffer, c, 0, n);
        }
        auto lowBlock = juce::dsp::AudioBlock<float>(low).getSubBlock(0, size_t(n));
        auto midBlock = juce::dsp::AudioBlock<float>(mid).getSubBlock(0, size_t(n));
        lowSplit.process(juce::dsp::ProcessContextReplacing<float>(lowBlock));
        restSplit.process(juce::dsp::ProcessContextReplacing<float>(midBlock));
        for (int c = 0; c < 2; ++c)
            high.copyFrom(c, 0, mid, c, 0, n);
        auto highBlock = juce::dsp::AudioBlock<float>(high).getSubBlock(0, size_t(n));
        midSplit.process(juce::dsp::ProcessContextReplacing<float>(midBlock));
        highSplit.process(juce::dsp::ProcessContextReplacing<float>(highBlock));

        const std::array<juce::dsp::AudioBlock<float>*, 3> blocks { &lowBlock, &midBlock, &highBlock };
        for (size_t b = 0; b < blocks.size(); ++b)
        {
            auto& compressor = compressors[b];
            compressor.setThreshold(-4.0f - 26.0f * a * weight[b]);
            compressor.setRatio(1.5f + 6.0f * a * weight[b]);
            compressor.setAttack(b == 0 ? 18.0f : 6.0f);
            compressor.setRelease(b == 0 ? 160.0f : 90.0f);
            const auto before = std::abs(blocks[b]->findMinAndMax().getEnd());
            compressor.process(juce::dsp::ProcessContextReplacing<float>(*blocks[b]));
            const auto after = std::abs(blocks[b]->findMinAndMax().getEnd());
            if (before > 0.0001f)
                track(reductions[b], juce::jmax(0.0f, juce::Decibels::gainToDecibels(before) - juce::Decibels::gainToDecibels(after + 1.0e-9f)));
            blocks[b]->multiplyBy(juce::Decibels::decibelsToGain(10.0f * a * weight[b]));
        }

        const auto drive = 1.0f + (mode.getIndex() == 3 ? 1.5f : 0.6f) * a;
        const auto gain = juce::Decibels::decibelsToGain(output.get());
        for (int c = 0; c < 2; ++c)
        {
            auto* out = buffer.getWritePointer(c);
            const auto* l = low.getReadPointer(c);
            const auto* m = mid.getReadPointer(c);
            const auto* h = high.getReadPointer(c);
            for (int i = 0; i < n; ++i)
                out[i] = std::tanh((l[i] + m[i] + h[i]) * drive) / std::tanh(drive) * gain;
        }
        track(outputPeak, buffer.getMagnitude(0, n));
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        const auto band = [this](size_t index) { return [this, index] { return juce::jlimit(0.0f, 1.0f, reductions[index].exchange(0.0f) / 18.0f); }; };
        auto meters = std::make_unique<Meters>(std::vector<Meters::Meter> {
            { "LOW GR", band(0), display::trace },
            { "MID GR", band(1), display::trace },
            { "HIGH GR", band(2), display::trace },
            { "OUT", [this] { return unit(outputPeak); }, display::secondary },
        });
        return display::row(std::make_unique<Spectrum>(*this), 1.4f, std::move(meters), 1.0f);
    }

private:
    juce::AudioParameterChoice& mode;
    juce::AudioParameterFloat &amount, &output;
    juce::AudioBuffer<float> low, mid, high;
    juce::dsp::LinkwitzRileyFilter<float> lowSplit, restSplit, midSplit, highSplit;
    std::array<juce::dsp::Compressor<float>, 3> compressors;
    std::array<std::atomic<float>, 3> reductions {};
    std::atomic<float> outputPeak { 0.0f };
};

class Clipper final : public Effect
{
public:
    Clipper()
        : Effect("Tomato Clipper"),
          input(addFloat("input", "Input", { 0.0f, 24.0f }, 0.0f, "dB")),
          ceiling(addFloat("ceiling", "Ceiling", { -12.0f, 0.0f }, -0.3f, "dB")),
          mode(addChoice("mode", "Mode", { "Soft", "Hard", "Analog" }, 0))
    {
        addPreset("Soft glue", { { "input", 3.0f }, { "ceiling", -0.5f }, { "mode", 0.0f } });
        addPreset("Hard drums", { { "input", 6.0f }, { "ceiling", -1.0f }, { "mode", 1.0f } });
        addPreset("Analog warmth", { { "input", 4.0f }, { "ceiling", -0.8f }, { "mode", 2.0f } });
    }

    float shape(float x) const
    {
        const auto kind = mode.getIndex();
        return kind == 0 ? std::tanh(x) : (kind == 1 ? juce::jlimit(-1.0f, 1.0f, x) : x / std::pow(1.0f + std::pow(std::abs(x), 2.5f), 0.4f));
    }

    void prepareToPlay(double, int) override {}

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto drive = juce::Decibels::decibelsToGain(input.get());
        const auto limit = juce::Decibels::decibelsToGain(ceiling.get());
        track(inputPeak, buffer.getMagnitude(0, buffer.getNumSamples()) * drive / limit);
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            auto* data = buffer.getWritePointer(c);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                data[i] = shape(data[i] * drive / limit) * limit;
        }
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto curve = std::make_unique<Curve>([this](float x) { return shape(x); }, juce::Range<float>(0.0f, 2.0f), juce::Range<float>(0.0f, 1.2f),
                                             [this] { return juce::jmin(2.0f, inputPeak.exchange(0.0f)); }, "INPUT", "OUTPUT");
        curve->identity = true;
        return display::row(std::move(curve), 1.0f, std::make_unique<Scope>(*this), 1.3f);
    }

private:
    juce::AudioParameterFloat &input, &ceiling;
    juce::AudioParameterChoice& mode;
    std::atomic<float> inputPeak { 0.0f };
};

class Pump final : public Effect
{
public:
    Pump()
        : Effect("Tomato Pump", true),
          source(addChoice("source", "Trigger", { "Tempo", "Sidechain" }, 0)),
          rate(addChoice("rate", "Rate", { "1/8", "1/4", "1/2", "1 bar" }, 1)),
          depth(addFloat("depth", "Depth", { 0.0f, 1.0f }, 0.7f)),
          curve(addFloat("curve", "Curve", { 0.3f, 5.0f }, 1.8f)),
          hold(addFloat("hold", "Hold", { 0.0f, 0.6f }, 0.05f))
    {
        addPreset("Classic 1/4 pump", { { "rate", 1.0f }, { "depth", 0.75f }, { "curve", 1.8f } });
        addPreset("Gentle breathing", { { "rate", 1.0f }, { "depth", 0.35f }, { "curve", 2.5f } });
        addPreset("1/8 stutter duck", { { "rate", 0.0f }, { "depth", 0.8f }, { "curve", 1.2f } });
        addPreset("Half-time swell", { { "rate", 2.0f }, { "depth", 0.6f }, { "curve", 3.0f } });
        addPreset("Kick sidechain", { { "source", 1.0f }, { "depth", 0.8f }, { "curve", 1.6f }, { "hold", 0.1f } });
    }

    float gainAt(float position) const
    {
        const auto opened = juce::jlimit(0.0f, 1.0f, (position - hold.get()) / juce::jmax(0.01f, 1.0f - hold.get()));
        return 1.0f - depth.get() * std::pow(1.0f - opened, curve.get());
    }

    void prepareToPlay(double sampleRate, int) override { currentRate = sampleRate; }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto channels = juce::jmin(2, buffer.getNumChannels());
        if (source.getIndex() == 1)
        {
            const auto* keyLeft = sidechainChannel(buffer, 0);
            const auto* keyRight = sidechainChannel(buffer, 1);
            const auto release = std::exp(-1.0f / ((0.02f + 0.04f * curve.get()) * float(currentRate)));
            const auto holdSamples = hold.get() * 0.25f * float(currentRate);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto key = keyLeft != nullptr ? juce::jmax(std::abs(keyLeft[i]), std::abs(keyRight[i])) : 0.0f;
                if (key > follower)
                {
                    follower = key;
                    held = holdSamples;
                }
                else if (held > 0.0f)
                    held -= 1.0f;
                else
                    follower *= release;
                const auto duck = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(follower, -100.0f) + 24.0f) / 18.0f);
                const auto gain = 1.0f - depth.get() * duck;
                for (int c = 0; c < channels; ++c)
                    buffer.getWritePointer(c)[i] *= gain;
            }
            phase.store(juce::jlimit(0.0f, 1.0f, 1.0f - (juce::Decibels::gainToDecibels(follower, -100.0f) + 24.0f) / 18.0f), std::memory_order_relaxed);
            return;
        }
        static constexpr std::array<double, 4> lengths { 0.5, 1.0, 2.0, 4.0 };
        const auto length = lengths[size_t(rate.getIndex())];
        const auto step = beatsPerSample(tempo(), currentRate);
        auto position = beats();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto gain = gainAt(float(std::fmod(position, length) / length));
            for (int c = 0; c < channels; ++c)
                buffer.getWritePointer(c)[i] *= gain;
            position += step;
        }
        phase.store(float(std::fmod(position, length) / length), std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        return display::row(std::make_unique<Curve>([this](float x) { return gainAt(x); }, juce::Range<float>(0.0f, 1.0f), juce::Range<float>(0.0f, 1.05f),
                                                    [this] { return phase.load(); }, "ONE PERIOD", "VOLUME"),
                            1.4f, std::make_unique<Scope>(*this), 1.0f);
    }

private:
    juce::AudioParameterChoice &source, &rate;
    juce::AudioParameterFloat &depth, &curve, &hold;
    std::atomic<float> phase { 0.0f };
    double currentRate = 44100.0;
    float follower = 0.0f, held = 0.0f;
};

class Gate final : public Effect
{
public:
    static constexpr int stepCount = 16;
    using Pattern = std::array<int, stepCount>;

    Gate()
        : Effect("Tomato Gate"),
          depth(addFloat("depth", "Depth", { 0.0f, 1.0f }, 1.0f)),
          smooth(addFloat("smooth", "Smooth", { 0.5f, 40.0f }, 4.0f, "ms")),
          length(addChoice("length", "Pattern length", { "1 beat", "2 beats", "1 bar", "2 bars" }, 2))
    {
        static constexpr Pattern trance { 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1 };
        for (int i = 0; i < stepCount; ++i)
            steps[size_t(i)] = &addBool("step" + juce::String(i + 1), "Step " + juce::String(i + 1), trance[size_t(i)] != 0);

        patterns = {
            { "Half gate", { 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 } },
            { "Quarter chop", { 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0 } },
            { "Trance 1", trance },
            { "Trance 2", { 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1 } },
            { "Stutter", { 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1 } },
            { "Off-beat", { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1 } },
            { "Build-up", { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1 } },
            { "Tape stop feel", { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0 } },
        };
    }

    int getNumPrograms() override { return int(patterns.size()) + 1; }
    int getCurrentProgram() override { return chosen; }
    bool showsInPanel(const juce::String& id) const override { return !id.startsWith("step"); }

    const juce::String getProgramName(int index) override
    {
        return index <= 0 || index > int(patterns.size()) ? juce::String("Init") : patterns[size_t(index - 1)].first;
    }

    void setCurrentProgram(int index) override
    {
        BuiltinProcessor::setCurrentProgram(0);
        chosen = index;
        if (index <= 0 || index > int(patterns.size()))
            return;
        const auto& values = patterns[size_t(index - 1)].second;
        for (size_t i = 0; i < steps.size(); ++i)
            steps[i]->setValueNotifyingHost(values[i] != 0 ? 1.0f : 0.0f);
    }

    void prepareToPlay(double sampleRate, int) override
    {
        currentRate = sampleRate;
        levelNow = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        static constexpr std::array<double, 4> lengths { 1.0, 2.0, 4.0, 8.0 };
        const auto span = lengths[size_t(length.getIndex())];
        const auto step = beatsPerSample(tempo(), currentRate);
        const auto coefficient = 1.0f - std::exp(-1.0f / (smooth.get() * 0.001f * float(currentRate)));
        auto position = beats();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto index = int(std::fmod(position, span) / span * stepCount) % stepCount;
            const auto target = steps[size_t(index)]->get() ? 1.0f : 1.0f - depth.get();
            levelNow += (target - levelNow) * coefficient;
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.getWritePointer(c)[i] *= levelNow;
            position += step;
        }
        current.store(int(std::fmod(position, span) / span * stepCount) % stepCount, std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createDisplay() override;

    std::array<juce::AudioParameterBool*, stepCount> steps {};
    std::atomic<int> current { 0 };

private:
    juce::AudioParameterFloat &depth, &smooth;
    juce::AudioParameterChoice& length;
    std::vector<std::pair<juce::String, Pattern>> patterns;
    int chosen = 0;
    double currentRate = 44100.0;
    float levelNow = 1.0f;
};

class StepGrid final : public display::Display
{
public:
    explicit StepGrid(Gate& g) : gate(g) {}

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto index = stepAt(e.position);
        if (index < 0)
            return;
        paintValue = !gate.steps[size_t(index)]->get();
        set(index);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (const auto index = stepAt(e.position); index >= 0)
            set(index);
    }

private:
    juce::Rectangle<float> bounds() const { return getLocalBounds().toFloat().reduced(2.0f).reduced(6.0f, 4.0f).withTrimmedTop(14.0f); }

    juce::Rectangle<float> cell(int index, juce::Rectangle<float> area) const
    {
        const auto width = area.getWidth() / float(Gate::stepCount);
        return { area.getX() + width * float(index), area.getY(), width, area.getHeight() };
    }

    int stepAt(juce::Point<float> point) const
    {
        const auto area = bounds();
        if (!area.contains(point))
            return -1;
        return juce::jlimit(0, Gate::stepCount - 1, int((point.x - area.getX()) / area.getWidth() * Gate::stepCount));
    }

    void set(int index)
    {
        gate.steps[size_t(index)]->setValueNotifyingHost(paintValue ? 1.0f : 0.0f);
        repaint();
    }

    void draw(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        g.setColour(display::label);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("CLICK OR DRAG TO DRAW THE GATE PATTERN", area.removeFromTop(14.0f), juce::Justification::left);
        const auto playing = gate.current.load();
        for (int i = 0; i < Gate::stepCount; ++i)
        {
            const auto box = cell(i, area).reduced(2.0f, 0.0f);
            const auto on = gate.steps[size_t(i)]->get();
            g.setColour(on ? display::trace : display::grid);
            g.fillRoundedRectangle(on ? box : box.withTrimmedTop(box.getHeight() * 0.8f), 3.0f);
            if (i == playing)
            {
                g.setColour(display::secondary);
                g.drawRoundedRectangle(box, 3.0f, 2.0f);
            }
        }
    }

    Gate& gate;
    bool paintValue = true;
};

std::unique_ptr<juce::Component> Gate::createDisplay()
{
    return std::make_unique<StepGrid>(*this);
}

class Lofi final : public Effect
{
public:
    Lofi()
        : Effect("Tomato Lofi"),
          bits(addFloat("bits", "Bits", { 3.0f, 16.0f }, 12.0f)),
          downsample(addFloat("downsample", "Downsample", { 1.0f, 24.0f, 1.0f }, 1.0f, "x")),
          wow(addFloat("wow", "Wow & flutter", { 0.0f, 1.0f }, 0.3f)),
          noise(addFloat("noise", "Vinyl", { 0.0f, 1.0f }, 0.2f)),
          tone(addFloat("tone", "Tone", skewed(800.0f, 20000.0f, 5000.0f), 9000.0f, "Hz"))
    {
        addPreset("Cassette", { { "bits", 14.0f }, { "wow", 0.5f }, { "noise", 0.15f }, { "tone", 9000.0f } });
        addPreset("Dusty vinyl", { { "bits", 16.0f }, { "wow", 0.25f }, { "noise", 0.55f }, { "tone", 11000.0f } });
        addPreset("8-bit console", { { "bits", 6.0f }, { "downsample", 4.0f }, { "wow", 0.0f }, { "noise", 0.0f }, { "tone", 16000.0f } });
        addPreset("Old radio", { { "bits", 10.0f }, { "downsample", 2.0f }, { "wow", 0.2f }, { "noise", 0.35f }, { "tone", 3000.0f } });
    }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        line.setSize(2, int(sampleRate * 0.05) + 4);
        line.clear();
        write = 0;
        held.fill(0.0f);
        filtered.fill(0.0f);
        crackle = 0.0f;
        quiet = std::numeric_limits<int>::max() / 2;
        counter = 0;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto size = line.getNumSamples();
        const auto levels = std::pow(2.0f, bits.get() - 1.0f);
        const auto hold = juce::jmax(1, juce::roundToInt(downsample.get()));
        const auto depth = wow.get() * 0.0025 * rate;
        const auto toneCoefficient = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * tone.get() / float(rate));
        const auto vinyl = noise.get();
        const auto tail = int(rate), fade = int(rate * 0.5);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            wowPhase = std::fmod(wowPhase + 0.55 / rate, 1.0);
            flutterPhase = std::fmod(flutterPhase + 6.3 / rate, 1.0);
            const auto input = juce::jmax(std::abs(buffer.getSample(0, i)), std::abs(buffer.getSample(1, i)));
            quiet = input > 0.0003f ? 0 : juce::jmin(quiet + 1, tail + fade);
            const auto presence = 1.0f - float(juce::jmax(0, quiet - tail)) / float(fade);
            const auto delay = 2.0 + depth * (1.0 + std::sin(twoPi * wowPhase)) + depth * 0.15 * std::sin(twoPi * flutterPhase);
            if (random.nextFloat() < vinyl * 0.0006f)
                crackle = (random.nextFloat() * 2.0f - 1.0f) * vinyl * 0.35f;
            crackle *= 0.86f;
            const auto hiss = (random.nextFloat() * 2.0f - 1.0f) * vinyl * 0.006f;
            const auto sampleNow = counter++ % hold == 0;

            for (int c = 0; c < 2; ++c)
            {
                auto* data = buffer.getWritePointer(c);
                line.setSample(c, write, data[i]);
                const auto read = double(write) - delay;
                auto index = int(std::floor(read));
                const auto fraction = float(read - index);
                index = (index % size + size) % size;
                const auto a = line.getSample(c, index);
                const auto b = line.getSample(c, (index + 1) % size);
                auto x = a + (b - a) * fraction;
                if (sampleNow)
                    held[size_t(c)] = std::round(x * levels) / levels;
                x = held[size_t(c)];
                filtered[size_t(c)] += (x - filtered[size_t(c)]) * toneCoefficient;
                data[i] = filtered[size_t(c)] + (crackle + hiss) * presence;
            }
            write = (write + 1) % size;
        }
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto staircase = std::make_unique<Curve>([this](float x)
        {
            const auto levels = std::pow(2.0f, juce::jmin(bits.get(), 8.0f) - 1.0f);
            return std::round(std::sin(juce::MathConstants<float>::twoPi * x) * levels) / levels;
        }, juce::Range<float>(0.0f, 1.0f), juce::Range<float>(-1.1f, 1.1f), nullptr, "ONE CYCLE", "BIT DEPTH");
        return display::row(std::move(staircase), 1.0f, std::make_unique<Scope>(*this), 1.3f);
    }

private:
    juce::AudioParameterFloat &bits, &downsample, &wow, &noise, &tone;
    juce::AudioBuffer<float> line;
    juce::Random random;
    std::array<float, 2> held {}, filtered {};
    double rate = 44100.0, wowPhase = 0.0, flutterPhase = 0.0;
    float crackle = 0.0f;
    int write = 0, quiet = std::numeric_limits<int>::max() / 2;
    juce::int64 counter = 0;
};

class Autopan final : public Effect
{
public:
    Autopan()
        : Effect("Tomato Autopan"),
          rate(addChoice("rate", "Rate", { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars" }, 2)),
          depth(addFloat("depth", "Depth", { 0.0f, 1.0f }, 0.6f)),
          shape(addChoice("shape", "Shape", { "Sine", "Triangle", "Square" }, 0))
    {
        addPreset("1/8 bounce", { { "rate", 1.0f }, { "depth", 0.7f }, { "shape", 2.0f } });
        addPreset("Slow sweep", { { "rate", 4.0f }, { "depth", 0.8f }, { "shape", 0.0f } });
    }

    float panAt(float position) const
    {
        const auto kind = shape.getIndex();
        const auto wave = kind == 0 ? std::sin(juce::MathConstants<float>::twoPi * position)
                                    : (kind == 1 ? 1.0f - 4.0f * std::abs(position - 0.5f) : (position < 0.5f ? 1.0f : -1.0f));
        return wave * depth.get();
    }

    void prepareToPlay(double sampleRate, int) override { currentRate = sampleRate; }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        static constexpr std::array<double, 6> lengths { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
        const auto length = lengths[size_t(rate.getIndex())];
        const auto step = beatsPerSample(tempo(), currentRate);
        auto position = beats();
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto pan = panAt(float(std::fmod(position, length) / length));
            const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            left[i] *= std::cos(angle) * juce::MathConstants<float>::sqrt2;
            right[i] *= std::sin(angle) * juce::MathConstants<float>::sqrt2;
            position += step;
        }
        phase.store(float(std::fmod(position, length) / length), std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        return std::make_unique<Curve>([this](float x) { return panAt(x); }, juce::Range<float>(0.0f, 1.0f), juce::Range<float>(-1.05f, 1.05f),
                                       [this] { return phase.load(); }, "ONE PERIOD", "PAN  R / L");
    }

private:
    juce::AudioParameterChoice& rate;
    juce::AudioParameterFloat& depth;
    juce::AudioParameterChoice& shape;
    std::atomic<float> phase { 0.0f };
    double currentRate = 44100.0;
};

class BassEnhancer final : public Effect
{
public:
    BassEnhancer()
        : Effect("Tomato Bass"),
          frequency(addFloat("frequency", "Frequency", skewed(40.0f, 250.0f, 100.0f), 100.0f, "Hz")),
          sub(addFloat("sub", "Sub octave", { 0.0f, 1.0f }, 0.3f)),
          punch(addFloat("punch", "Punch", { 0.0f, 12.0f }, 3.0f, "dB")),
          harmonics(addFloat("harmonics", "Harmonics", { 0.0f, 1.0f }, 0.35f)),
          output(addFloat("output", "Output", { -12.0f, 6.0f }, 0.0f, "dB"))
    {
        addPreset("Sub boost", { { "frequency", 80.0f }, { "sub", 0.6f }, { "punch", 4.0f }, { "harmonics", 0.1f } });
        addPreset("808 on phone speakers", { { "frequency", 110.0f }, { "sub", 0.0f }, { "punch", 2.0f }, { "harmonics", 0.8f } });
        addPreset("Warm bottom", { { "frequency", 140.0f }, { "sub", 0.2f }, { "punch", 5.0f }, { "harmonics", 0.3f }, { "output", -2.0f } });
    }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        for (auto* filters : { &lowFilters, &subFilters, &harmonicFilters, &shelves })
            for (auto& filter : *filters)
                filter.reset();
        flip.fill(1.0f);
        previous.fill(0.0f);
        envelope.fill(0.0f);
        cached = -1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        update();
        const auto subAmount = sub.get();
        const auto harmonicAmount = harmonics.get();
        const auto gain = juce::Decibels::decibelsToGain(output.get());
        const auto release = std::exp(-1.0f / (0.03f * float(rate)));
        for (int c = 0; c < 2; ++c)
        {
            auto* data = buffer.getWritePointer(c);
            const auto index = size_t(c);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto x = data[i];
                const auto low = lowFilters[index].processSingleSampleRaw(x);
                if (previous[index] < 0.0f && low >= 0.0f)
                    flip[index] = -flip[index];
                previous[index] = low;
                envelope[index] = juce::jmax(std::abs(low), envelope[index] * release);
                const auto octave = subFilters[index].processSingleSampleRaw(flip[index] * envelope[index]);
                const auto saturated = harmonicFilters[index].processSingleSampleRaw(std::tanh(low * 6.0f) * 0.5f);
                data[i] = (shelves[index].processSingleSampleRaw(x) + octave * subAmount + saturated * harmonicAmount) * gain;
            }
        }
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        return std::make_unique<Spectrum>(*this);
    }

private:
    void update()
    {
        const auto signature = frequency.get() * 100.0f + punch.get();
        if (signature == cached)
            return;
        cached = signature;
        const auto f = double(frequency.get());
        for (auto& filter : lowFilters)
            filter.setCoefficients(juce::IIRCoefficients::makeLowPass(rate, f, 0.7071));
        for (auto& filter : subFilters)
            filter.setCoefficients(juce::IIRCoefficients::makeLowPass(rate, f * 0.5, 0.7071));
        for (auto& filter : harmonicFilters)
            filter.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, f * 3.0, 0.8));
        for (auto& filter : shelves)
            filter.setCoefficients(juce::IIRCoefficients::makeLowShelf(rate, f, 0.7071, juce::Decibels::decibelsToGain(punch.get())));
    }

    juce::AudioParameterFloat &frequency, &sub, &punch, &harmonics, &output;
    std::array<juce::IIRFilter, 2> lowFilters, subFilters, harmonicFilters, shelves;
    std::array<float, 2> flip {}, previous {}, envelope {};
    double rate = 44100.0;
    float cached = -1.0f;
};

template <typename Type>
std::unique_ptr<juce::AudioProcessor> make()
{
    return std::make_unique<Type>();
}

using Factory = std::unique_ptr<juce::AudioProcessor> (*)();

const std::vector<std::pair<juce::String, Factory>>& table()
{
    static const std::vector<std::pair<juce::String, Factory>> entries {
        { "Tomato Maximizer", make<Maximizer> },
        { "Tomato Clipper", make<Clipper> },
        { "Tomato Bass", make<BassEnhancer> },
        { "Tomato Pump", make<Pump> },
        { "Tomato Gate", make<Gate> },
        { "Tomato Lofi", make<Lofi> },
        { "Tomato Autopan", make<Autopan> },
    };
    return entries;
}
}

const juce::StringArray& characterEffects()
{
    static const auto names = []
    {
        juce::StringArray result;
        for (const auto& [name, factory] : table())
            result.add(name);
        return result;
    }();
    return names;
}

std::unique_ptr<juce::AudioProcessor> createCharacterEffect(const juce::String& name)
{
    for (const auto& [effectName, factory] : table())
        if (effectName == name)
            return factory();
    return nullptr;
}
}
