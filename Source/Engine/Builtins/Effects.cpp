#include "Engine/Builtins/Registry.h"
#include "Engine/Builtins/Effect.h"
#include "Engine/Builtins/Displays.h"
#include "Engine/Builtins/LuaPlugins.h"
#include "Engine/Builtins/NativePlugins.h"

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

float level(std::atomic<float>& value)
{
    return display::decibelsToUnit(juce::Decibels::gainToDecibels(value.exchange(0.0f, std::memory_order_relaxed), -60.0f));
}

void track(std::atomic<float>& value, float sample) noexcept
{
    if (sample > value.load(std::memory_order_relaxed))
        value.store(sample, std::memory_order_relaxed);
}

class Reverb final : public Effect
{
public:
    Reverb()
        : Effect("Tomato Reverb"),
          size(addFloat("size", "Room size", { 0.0f, 1.0f }, 0.55f)),
          damping(addFloat("damping", "Damping", { 0.0f, 1.0f }, 0.45f)),
          width(addFloat("width", "Width", { 0.0f, 1.0f }, 1.0f)),
          wet(addFloat("wet", "Wet", { 0.0f, 1.0f }, 0.28f)),
          dry(addFloat("dry", "Dry", { 0.0f, 1.0f }, 0.85f)),
          predelay(addFloat("predelay", "Pre-delay", { 0.0f, 150.0f }, 0.0f, "ms"))
    {
        addPreset("Small room", { { "size", 0.25f }, { "damping", 0.6f }, { "wet", 0.22f }, { "dry", 0.9f }, { "predelay", 5.0f } });
        addPreset("Plate", { { "size", 0.55f }, { "damping", 0.25f }, { "wet", 0.3f }, { "dry", 0.85f }, { "predelay", 12.0f } });
        addPreset("Hall", { { "size", 0.8f }, { "damping", 0.45f }, { "wet", 0.35f }, { "dry", 0.8f }, { "predelay", 25.0f } });
        addPreset("Huge space", { { "size", 0.97f }, { "damping", 0.3f }, { "wet", 0.5f }, { "dry", 0.7f }, { "predelay", 40.0f } });
        addPreset("Vocal verb", { { "size", 0.65f }, { "damping", 0.55f }, { "wet", 0.25f }, { "dry", 0.9f }, { "predelay", 60.0f } });
        addPreset("Wash (100% wet)", { { "size", 0.92f }, { "damping", 0.4f }, { "wet", 0.8f }, { "dry", 0.0f } });
    }

    double getTailLengthSeconds() const override { return 10.0; }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        reverb.setSampleRate(sampleRate);
        reverb.reset();
        line.setSize(2, int(sampleRate * 0.2) + 2);
        line.clear();
        write = 0;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::Reverb::Parameters parameters;
        parameters.roomSize = size.get();
        parameters.damping = damping.get();
        parameters.width = width.get();
        parameters.wetLevel = wet.get();
        parameters.dryLevel = 0.0f;
        reverb.setParameters(parameters);

        const auto n = buffer.getNumSamples();
        wetBuffer.setSize(2, n, false, false, true);
        const auto delay = juce::jlimit(0, line.getNumSamples() - 1, juce::roundToInt(predelay.get() * 0.001 * rate));
        const auto lineSize = line.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                line.setSample(c, write, buffer.getSample(c, i));
                wetBuffer.setSample(c, i, line.getSample(c, (write - delay + lineSize) % lineSize));
            }
            write = (write + 1) % lineSize;
        }
        reverb.processStereo(wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), n);
        const auto dryLevel = dry.get();
        for (int c = 0; c < 2; ++c)
        {
            buffer.applyGain(c, 0, n, dryLevel);
            buffer.addFrom(c, 0, wetBuffer, c, 0, n);
        }
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto decay = [this](float t)
        {
            const auto seconds = 0.25f + std::pow(size.get(), 2.0f) * 7.0f * (1.0f - damping.get() * 0.4f);
            const auto start = predelay.get() * 0.001f;
            return t < start ? -60.0f : juce::jmax(-60.0f, -60.0f * (t - start) / seconds) + juce::Decibels::gainToDecibels(wet.get() + 0.001f);
        };
        return display::row(std::make_unique<Curve>(decay, juce::Range<float>(0.0f, 6.0f), juce::Range<float>(-60.0f, 0.0f), nullptr, "6 s", "DECAY"), 1.0f,
                            std::make_unique<Scope>(*this), 1.0f);
    }

private:
    juce::AudioParameterFloat &size, &damping, &width, &wet, &dry, &predelay;
    juce::Reverb reverb;
    juce::AudioBuffer<float> line, wetBuffer;
    double rate = 44100.0;
    int write = 0;
};

class Delay final : public Effect
{
public:
    Delay()
        : Effect("Tomato Delay"),
          time(addFloat("time", "Time", { 1.0f, 32.0f, 1.0f }, 3.0f, "steps")),
          feedback(addFloat("feedback", "Feedback", { 0.0f, 0.95f }, 0.45f)),
          wet(addFloat("wet", "Wet", { 0.0f, 1.0f }, 0.35f)),
          damp(addFloat("damp", "Damping", skewed(500.0f, 20000.0f, 4000.0f), 6000.0f, "Hz")),
          pingPong(addBool("pingPong", "Ping pong", true))
    {
        addPreset("1/8 echo", { { "time", 2.0f }, { "feedback", 0.35f }, { "wet", 0.3f } });
        addPreset("Dotted 1/8", { { "time", 3.0f }, { "feedback", 0.45f }, { "wet", 0.3f } });
        addPreset("1/4 ping pong", { { "time", 4.0f }, { "feedback", 0.5f }, { "wet", 0.32f }, { "pingPong", 1.0f } });
        addPreset("Slapback", { { "time", 1.0f }, { "feedback", 0.1f }, { "wet", 0.25f }, { "pingPong", 0.0f }, { "damp", 3500.0f } });
        addPreset("Dub tail", { { "time", 6.0f }, { "feedback", 0.75f }, { "wet", 0.4f }, { "damp", 1800.0f } });
    }

    double getTailLengthSeconds() const override { return 8.0; }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        line.setSize(2, juce::roundToInt(sampleRate * 8.0) + 1);
        line.clear();
        write = 0;
        filtered.fill(0.0f);
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto size = line.getNumSamples();
        const auto delay = juce::jlimit(1, size - 1, juce::roundToInt(time.get() * 60.0 / tempo() / 4.0 * rate));
        const auto amount = feedback.get();
        const auto levelOut = wet.get();
        const auto coefficient = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * damp.get() / float(rate));
        const auto ping = pingPong.get();
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        auto* lineLeft = line.getWritePointer(0);
        auto* lineRight = line.getWritePointer(1);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto read = (write - delay + size) % size;
            const auto echoLeft = lineLeft[read];
            const auto echoRight = lineRight[read];
            filtered[0] += (echoLeft - filtered[0]) * coefficient;
            filtered[1] += (echoRight - filtered[1]) * coefficient;
            if (ping)
            {
                lineLeft[write] = (left[i] + right[i]) * 0.5f + filtered[1] * amount;
                lineRight[write] = filtered[0] * amount;
            }
            else
            {
                lineLeft[write] = left[i] + filtered[0] * amount;
                lineRight[write] = right[i] + filtered[1] * amount;
            }
            left[i] += echoLeft * levelOut;
            right[i] += echoRight * levelOut;
            write = (write + 1) % size;
        }
    }

    std::unique_ptr<juce::Component> createDisplay() override;

    juce::AudioParameterFloat &time, &feedback, &wet, &damp;
    juce::AudioParameterBool& pingPong;

private:
    juce::AudioBuffer<float> line;
    std::array<float, 2> filtered {};
    double rate = 44100.0;
    int write = 0;
};

class Taps final : public display::Display
{
public:
    explicit Taps(Delay& d) : delay(d) {}

private:
    void draw(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto stepWidth = area.getWidth() / 32.0f;
        g.setColour(display::grid);
        for (int i = 0; i <= 32; i += 4)
            g.drawVerticalLine(juce::roundToInt(area.getX() + stepWidth * float(i)), area.getY(), area.getBottom());
        g.setColour(display::label);
        g.fillRect(juce::Rectangle<float>(area.getX(), area.getY() + 6.0f, 4.0f, area.getHeight() - 6.0f));
        auto gain = delay.wet.get();
        auto tap = 1;
        for (auto step = delay.time.get(); step <= 32.0f && gain > 0.01f; step += delay.time.get(), ++tap)
        {
            const auto x = area.getX() + stepWidth * step;
            const auto height = (area.getHeight() - 6.0f) * juce::jlimit(0.0f, 1.0f, gain);
            const auto right = delay.pingPong.get() && tap % 2 == 0;
            g.setColour(right ? display::secondary : display::trace);
            g.fillRect(juce::Rectangle<float>(x - 2.0f, area.getBottom() - height, 4.0f, height));
            gain *= delay.feedback.get();
        }
        g.setColour(display::label);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("ECHOES OVER 2 BARS  (RED = LEFT, GREEN = RIGHT)", area.removeFromTop(12.0f), juce::Justification::left);
    }

    Delay& delay;
};

std::unique_ptr<juce::Component> Delay::createDisplay()
{
    return display::row(std::make_unique<Taps>(*this), 1.4f, std::make_unique<Scope>(*this), 1.0f);
}

class Compressor final : public Effect
{
public:
    Compressor()
        : Effect("Tomato Compressor", true),
          threshold(addFloat("threshold", "Threshold", { -60.0f, 0.0f }, -18.0f, "dB")),
          ratio(addFloat("ratio", "Ratio", skewed(1.0f, 20.0f, 4.0f), 4.0f, ":1")),
          attack(addFloat("attack", "Attack", skewed(0.1f, 200.0f, 10.0f), 5.0f, "ms")),
          release(addFloat("release", "Release", skewed(5.0f, 2000.0f, 150.0f), 120.0f, "ms")),
          makeup(addFloat("makeup", "Gain", { -12.0f, 24.0f }, 0.0f, "dB")),
          knee(addFloat("knee", "Knee", { 0.0f, 24.0f }, 6.0f, "dB")),
          source(addChoice("source", "Detector", { "Input", "Sidechain" }, 0))
    {
        addPreset("Vocal leveler", { { "threshold", -20.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 120.0f }, { "makeup", 4.0f } });
        addPreset("Drum bus glue", { { "threshold", -14.0f }, { "ratio", 2.5f }, { "attack", 20.0f }, { "release", 90.0f }, { "makeup", 2.0f } });
        addPreset("808 control", { { "threshold", -12.0f }, { "ratio", 4.0f }, { "attack", 6.0f }, { "release", 160.0f }, { "makeup", 2.5f } });
        addPreset("Smash", { { "threshold", -32.0f }, { "ratio", 12.0f }, { "attack", 1.0f }, { "release", 60.0f }, { "makeup", 12.0f }, { "knee", 2.0f } });
        addPreset("Master glue", { { "threshold", -10.0f }, { "ratio", 1.8f }, { "attack", 30.0f }, { "release", 200.0f }, { "makeup", 1.5f }, { "knee", 12.0f } });
        addPreset("Sidechain duck", { { "threshold", -20.0f }, { "ratio", 8.0f }, { "attack", 1.0f }, { "release", 90.0f }, { "knee", 6.0f }, { "source", 1.0f } });
    }

    float transfer(float input) const
    {
        const auto t = threshold.get();
        const auto r = ratio.get();
        const auto w = knee.get();
        if (2.0f * (input - t) < -w)
            return input;
        if (w > 0.0f && 2.0f * std::abs(input - t) <= w)
            return input + (1.0f / r - 1.0f) * std::pow(input - t + w * 0.5f, 2.0f) / (2.0f * w);
        return t + (input - t) / r;
    }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        envelope = -120.0f;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto attackCoefficient = std::exp(-1.0f / (attack.get() * 0.001f * float(rate)));
        const auto releaseCoefficient = std::exp(-1.0f / (release.get() * 0.001f * float(rate)));
        const auto gainOut = juce::Decibels::decibelsToGain(makeup.get());
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        const auto* keyLeft = source.getIndex() == 1 ? sidechainChannel(buffer, 0) : nullptr;
        const auto* keyRight = source.getIndex() == 1 ? sidechainChannel(buffer, 1) : nullptr;
        auto maxReduction = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto peak = keyLeft != nullptr ? juce::jmax(std::abs(keyLeft[i]), std::abs(keyRight[i])) : juce::jmax(std::abs(left[i]), std::abs(right[i]));
            track(inputPeak, peak);
            const auto inputDb = juce::Decibels::gainToDecibels(peak, -120.0f);
            const auto coefficient = inputDb > envelope ? attackCoefficient : releaseCoefficient;
            envelope = inputDb + coefficient * (envelope - inputDb);
            const auto reduction = transfer(envelope) - envelope;
            maxReduction = juce::jmin(maxReduction, reduction);
            const auto gain = juce::Decibels::decibelsToGain(reduction) * gainOut;
            left[i] *= gain;
            right[i] *= gain;
            track(outputPeak, juce::jmax(std::abs(left[i]), std::abs(right[i])));
        }
        track(gainReduction, -maxReduction);
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto curve = std::make_unique<Curve>([this](float x) { return transfer(x) + makeup.get(); }, juce::Range<float>(-60.0f, 0.0f),
                                             juce::Range<float>(-60.0f, 6.0f), [this] { return envelope; }, "INPUT dB", "OUTPUT dB");
        curve->identity = true;
        auto meters = std::make_unique<Meters>(std::vector<Meters::Meter> {
            { "IN", [this] { return level(inputPeak); }, display::secondary },
            { "GR", [this] { return juce::jlimit(0.0f, 1.0f, gainReduction.exchange(0.0f) / 24.0f); }, display::trace },
            { "OUT", [this] { return level(outputPeak); }, display::secondary },
        });
        return display::row(std::move(curve), 1.6f, std::move(meters), 1.0f);
    }

private:
    juce::AudioParameterFloat &threshold, &ratio, &attack, &release, &makeup, &knee;
    juce::AudioParameterChoice& source;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, gainReduction { 0.0f };
    double rate = 44100.0;
    float envelope = -120.0f;
};

class Limiter final : public Effect
{
public:
    Limiter()
        : Effect("Tomato Limiter"),
          gain(addFloat("gain", "Gain", { -12.0f, 24.0f }, 0.0f, "dB")),
          ceiling(addFloat("ceiling", "Ceiling", { -24.0f, 0.0f }, -0.3f, "dB")),
          release(addFloat("release", "Release", skewed(1.0f, 1000.0f, 80.0f), 60.0f, "ms"))
    {
        addPreset("Safety", { { "gain", 0.0f }, { "ceiling", -0.3f }, { "release", 80.0f } });
        addPreset("Loud master", { { "gain", 6.0f }, { "ceiling", -0.5f }, { "release", 50.0f } });
        addPreset("Brickwall", { { "gain", 10.0f }, { "ceiling", -1.0f }, { "release", 20.0f } });
    }

    void prepareToPlay(double sampleRate, int samples) override
    {
        limiter.prepare(specFor(sampleRate, samples));
        limiter.reset();
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain(juce::Decibels::decibelsToGain(gain.get()));
        const auto before = buffer.getMagnitude(0, buffer.getNumSamples());
        track(inputPeak, before);
        limiter.setThreshold(ceiling.get());
        limiter.setRelease(release.get());
        juce::dsp::AudioBlock<float> block(buffer);
        limiter.process(juce::dsp::ProcessContextReplacing<float>(block));
        buffer.applyGain(juce::Decibels::decibelsToGain(ceiling.get()));
        const auto after = buffer.getMagnitude(0, buffer.getNumSamples());
        track(outputPeak, after);
        if (before > 0.0001f)
            track(gainReduction, juce::jmax(0.0f, juce::Decibels::gainToDecibels(before) - juce::Decibels::gainToDecibels(after + 1.0e-9f)));
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto meters = std::make_unique<Meters>(std::vector<Meters::Meter> {
            { "IN", [this] { return level(inputPeak); }, display::secondary },
            { "GR", [this] { return juce::jlimit(0.0f, 1.0f, gainReduction.exchange(0.0f) / 18.0f); }, display::trace },
            { "OUT", [this] { return level(outputPeak); }, display::secondary },
        });
        return display::row(std::make_unique<Scope>(*this), 1.6f, std::move(meters), 1.0f);
    }

private:
    juce::AudioParameterFloat &gain, &ceiling, &release;
    juce::dsp::Limiter<float> limiter;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, gainReduction { 0.0f };
};

class Filter final : public Effect
{
public:
    Filter()
        : Effect("Tomato Filter"),
          mode(addChoice("mode", "Mode", { "Low pass 12", "High pass 12", "Band pass 12", "Low pass 24", "High pass 24", "Band pass 24" }, 3)),
          cutoff(addFloat("cutoff", "Cutoff", skewed(20.0f, 20000.0f, 1000.0f), 2500.0f, "Hz")),
          resonance(addFloat("resonance", "Resonance", { 0.0f, 1.0f }, 0.2f)),
          drive(addFloat("drive", "Drive", { 1.0f, 10.0f }, 1.0f))
    {
        addPreset("Dark low pass", { { "mode", 3.0f }, { "cutoff", 700.0f }, { "resonance", 0.15f } });
        addPreset("Acid squelch", { { "mode", 3.0f }, { "cutoff", 1200.0f }, { "resonance", 0.8f }, { "drive", 3.0f } });
        addPreset("Thin high pass", { { "mode", 4.0f }, { "cutoff", 400.0f }, { "resonance", 0.1f } });
        addPreset("Radio band", { { "mode", 2.0f }, { "cutoff", 1500.0f }, { "resonance", 0.4f } });
    }

    float responseDb(float frequency) const
    {
        const auto rate = juce::jmax(8000.0, getSampleRate());
        const auto f = juce::jmin(cutoff.get(), float(rate) * 0.45f);
        const auto q = 0.5f + resonance.get() * 6.0f;
        const auto index = mode.getIndex();
        const auto kind = index % 3;
        const auto coefficients = kind == 0 ? juce::dsp::IIR::Coefficients<float>::makeLowPass(rate, f, q)
                                            : (kind == 1 ? juce::dsp::IIR::Coefficients<float>::makeHighPass(rate, f, q)
                                                         : juce::dsp::IIR::Coefficients<float>::makeBandPass(rate, f, q));
        const auto magnitude = coefficients->getMagnitudeForFrequency(frequency, rate);
        return float(juce::Decibels::gainToDecibels(index >= 3 ? magnitude * magnitude : magnitude, -60.0));
    }

    void prepareToPlay(double sampleRate, int samples) override
    {
        filter.prepare(specFor(sampleRate, samples));
        filter.reset();
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        static constexpr std::array modes {
            juce::dsp::LadderFilterMode::LPF12, juce::dsp::LadderFilterMode::HPF12, juce::dsp::LadderFilterMode::BPF12,
            juce::dsp::LadderFilterMode::LPF24, juce::dsp::LadderFilterMode::HPF24, juce::dsp::LadderFilterMode::BPF24,
        };
        filter.setMode(modes[size_t(mode.getIndex())]);
        filter.setCutoffFrequencyHz(juce::jmin(cutoff.get(), float(getSampleRate()) * 0.45f));
        filter.setResonance(resonance.get());
        filter.setDrive(drive.get());
        juce::dsp::AudioBlock<float> block(buffer);
        filter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        return std::make_unique<Spectrum>(*this, [this](float f) { return responseDb(f); }, 30.0f);
    }

private:
    juce::AudioParameterChoice& mode;
    juce::AudioParameterFloat &cutoff, &resonance, &drive;
    juce::dsp::LadderFilter<float> filter;
};

class Chorus final : public Effect
{
public:
    Chorus()
        : Effect("Tomato Chorus"),
          rate(addFloat("rate", "Rate", skewed(0.01f, 10.0f, 1.0f), 0.8f, "Hz")),
          depth(addFloat("depth", "Depth", { 0.0f, 1.0f }, 0.3f)),
          centre(addFloat("centre", "Delay", { 1.0f, 50.0f }, 7.0f, "ms")),
          feedback(addFloat("feedback", "Feedback", { -0.95f, 0.95f }, 0.0f)),
          wet(addFloat("wet", "Mix", { 0.0f, 1.0f }, 0.5f))
    {
        addPreset("Subtle width", { { "rate", 0.4f }, { "depth", 0.2f }, { "centre", 8.0f }, { "wet", 0.35f } });
        addPreset("Lush chorus", { { "rate", 0.8f }, { "depth", 0.5f }, { "centre", 12.0f }, { "wet", 0.5f } });
        addPreset("Jet flanger", { { "rate", 0.2f }, { "depth", 0.8f }, { "centre", 2.0f }, { "feedback", 0.7f }, { "wet", 0.5f } });
    }

    void prepareToPlay(double sampleRate, int samples) override
    {
        chorus.prepare(specFor(sampleRate, samples));
        chorus.reset();
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        chorus.setRate(rate.get());
        chorus.setDepth(depth.get());
        chorus.setCentreDelay(centre.get());
        chorus.setFeedback(feedback.get());
        chorus.setMix(wet.get());
        juce::dsp::AudioBlock<float> block(buffer);
        chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto lfo = std::make_unique<Curve>([this](float t) { return centre.get() * (1.0f + depth.get() * std::sin(juce::MathConstants<float>::twoPi * rate.get() * t)); },
                                           juce::Range<float>(0.0f, 4.0f), juce::Range<float>(0.0f, 100.0f), nullptr, "4 s", "DELAY ms");
        return display::row(std::move(lfo), 1.0f, std::make_unique<Scope>(*this), 1.0f);
    }

private:
    juce::AudioParameterFloat &rate, &depth, &centre, &feedback, &wet;
    juce::dsp::Chorus<float> chorus;
};

class Phaser final : public Effect
{
public:
    Phaser()
        : Effect("Tomato Phaser"),
          rate(addFloat("rate", "Rate", skewed(0.01f, 10.0f, 1.0f), 0.4f, "Hz")),
          depth(addFloat("depth", "Depth", { 0.0f, 1.0f }, 0.6f)),
          centre(addFloat("centre", "Centre", skewed(50.0f, 10000.0f, 1000.0f), 1300.0f, "Hz")),
          feedback(addFloat("feedback", "Feedback", { -0.95f, 0.95f }, 0.5f)),
          wet(addFloat("wet", "Mix", { 0.0f, 1.0f }, 0.5f))
    {
        addPreset("Slow sweep", { { "rate", 0.15f }, { "depth", 0.8f }, { "feedback", 0.6f } });
        addPreset("Fast swirl", { { "rate", 3.0f }, { "depth", 0.5f }, { "feedback", 0.3f } });
    }

    void prepareToPlay(double sampleRate, int samples) override
    {
        phaser.prepare(specFor(sampleRate, samples));
        phaser.reset();
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        phaser.setRate(rate.get());
        phaser.setDepth(depth.get());
        phaser.setCentreFrequency(centre.get());
        phaser.setFeedback(feedback.get());
        phaser.setMix(wet.get());
        juce::dsp::AudioBlock<float> block(buffer);
        phaser.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        return std::make_unique<Spectrum>(*this);
    }

private:
    juce::AudioParameterFloat &rate, &depth, &centre, &feedback, &wet;
    juce::dsp::Phaser<float> phaser;
};

class Distortion final : public Effect
{
public:
    Distortion()
        : Effect("Tomato Distortion"),
          drive(addFloat("drive", "Drive", { 0.0f, 40.0f }, 12.0f, "dB")),
          tone(addFloat("tone", "Tone", skewed(500.0f, 20000.0f, 4000.0f), 12000.0f, "Hz")),
          output(addFloat("output", "Output", { -36.0f, 6.0f }, -8.0f, "dB")),
          mode(addChoice("mode", "Mode", { "Tube", "Overdrive", "Fuzz", "Fold", "Rectify", "Crush" }, 0)),
          bias(addFloat("bias", "Bias", { -0.5f, 0.5f }, 0.1f))
    {
        addPreset("Warm tube", { { "mode", 0.0f }, { "drive", 8.0f }, { "tone", 9000.0f }, { "output", -5.0f }, { "bias", 0.15f } });
        addPreset("808 grit", { { "mode", 1.0f }, { "drive", 14.0f }, { "tone", 6000.0f }, { "output", -10.0f } });
        addPreset("Fuzz box", { { "mode", 2.0f }, { "drive", 26.0f }, { "tone", 5000.0f }, { "output", -16.0f } });
        addPreset("Wavefolder", { { "mode", 3.0f }, { "drive", 18.0f }, { "tone", 14000.0f }, { "output", -8.0f } });
        addPreset("Octave rectifier", { { "mode", 4.0f }, { "drive", 10.0f }, { "tone", 10000.0f }, { "output", -6.0f } });
        addPreset("Bitcrushed", { { "mode", 5.0f }, { "drive", 6.0f }, { "tone", 16000.0f }, { "output", -4.0f } });
    }

    float shape(float x) const
    {
        switch (mode.getIndex())
        {
            case 0: return std::tanh(x + bias.get()) - std::tanh(bias.get());
            case 1: return 2.0f / juce::MathConstants<float>::pi * std::atan(x * 1.4f);
            case 2: return juce::jlimit(-0.92f, 0.92f, std::copysign(1.0f - std::exp(-std::abs(x) * 2.5f), x));
            case 3: return std::sin(x * juce::MathConstants<float>::halfPi);
            case 4: return std::abs(std::tanh(x)) * 1.6f - 0.8f + std::tanh(x) * 0.2f;
            default: return std::round(std::tanh(x) * 6.0f) / 6.0f;
        }
    }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        state.fill(0.0f);
        dcInput.fill(0.0f);
        dcOutput.fill(0.0f);
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto pre = juce::Decibels::decibelsToGain(drive.get());
        const auto post = juce::Decibels::decibelsToGain(output.get());
        const auto coefficient = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * tone.get() / float(rate));
        track(inputPeak, buffer.getMagnitude(0, buffer.getNumSamples()));
        for (int c = 0; c < 2; ++c)
        {
            auto* data = buffer.getWritePointer(c);
            auto& s = state[size_t(c)];
            auto& previousInput = dcInput[size_t(c)];
            auto& blocked = dcOutput[size_t(c)];
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                s += (shape(data[i] * pre) - s) * coefficient;
                blocked = s - previousInput + 0.999f * blocked;
                previousInput = s;
                data[i] = blocked * post;
            }
        }
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto curve = std::make_unique<Curve>([this](float x) { return shape(x * juce::Decibels::decibelsToGain(drive.get())); }, juce::Range<float>(-1.0f, 1.0f),
                                             juce::Range<float>(-1.05f, 1.05f), [this] { return juce::jmin(1.0f, inputPeak.exchange(0.0f)); }, "IN", "OUT");
        curve->identity = true;
        return display::row(std::move(curve), 1.0f, std::make_unique<Scope>(*this), 1.3f);
    }

private:
    juce::AudioParameterFloat &drive, &tone, &output;
    juce::AudioParameterChoice& mode;
    juce::AudioParameterFloat& bias;
    std::array<float, 2> state {}, dcInput {}, dcOutput {};
    std::atomic<float> inputPeak { 0.0f };
    double rate = 44100.0;
};

class Stereo final : public Effect
{
public:
    static constexpr int pointCount = 1024;

    Stereo()
        : Effect("Tomato Stereo"),
          gain(addFloat("gain", "Gain", { -36.0f, 12.0f }, 0.0f, "dB")),
          pan(addFloat("pan", "Pan", { -1.0f, 1.0f }, 0.0f)),
          width(addFloat("width", "Width", { 0.0f, 2.0f }, 1.0f)),
          swap(addBool("swap", "Swap left/right", false)),
          invert(addBool("invert", "Invert polarity", false))
    {
        addPreset("Extra wide", { { "width", 1.6f } });
        addPreset("Mono", { { "width", 0.0f } });
        addPreset("Swap sides", { { "swap", 1.0f } });
    }

    void prepareToPlay(double, int) override {}

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto levelGain = juce::Decibels::decibelsToGain(gain.get()) * (invert.get() ? -1.0f : 1.0f);
        const auto left = levelGain * juce::jmin(1.0f, 1.0f - pan.get());
        const auto right = levelGain * juce::jmin(1.0f, 1.0f + pan.get());
        const auto spread = width.get();
        auto* l = buffer.getWritePointer(swap.get() ? 1 : 0);
        auto* r = buffer.getWritePointer(swap.get() ? 0 : 1);
        auto position = write.load(std::memory_order_relaxed);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto mid = (l[i] + r[i]) * 0.5f;
            const auto side = (l[i] - r[i]) * 0.5f * spread;
            l[i] = (mid + side) * left;
            r[i] = (mid - side) * right;
            points[size_t(position)] = { l[i], r[i] };
            position = (position + 1) % pointCount;
        }
        write.store(position, std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createDisplay() override;

    std::array<std::pair<float, float>, pointCount> points {};
    std::atomic<int> write { 0 };

private:
    juce::AudioParameterFloat &gain, &pan, &width;
    juce::AudioParameterBool &swap, &invert;
};

class Vectorscope final : public display::Display
{
public:
    explicit Vectorscope(Stereo& s) : stereo(s) {}

private:
    void draw(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto square = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()), juce::jmin(area.getWidth(), area.getHeight()));
        const auto centre = square.getCentre();
        const auto radius = square.getWidth() * 0.48f;
        g.setColour(display::grid);
        g.drawEllipse(square.reduced(2.0f), 1.0f);
        g.drawLine(centre.x, square.getY(), centre.x, square.getBottom());
        g.drawLine(square.getX(), centre.y, square.getRight(), centre.y);
        g.setColour(display::label);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("L", square.withWidth(14.0f), juce::Justification::centred);
        g.setColour(display::trace.withAlpha(0.6f));
        for (const auto& [left, right] : stereo.points)
        {
            const auto x = (right - left) * 0.7071f;
            const auto y = (left + right) * 0.7071f;
            g.fillRect(centre.x + x * radius, centre.y - y * radius, 1.6f, 1.6f);
        }
    }

    Stereo& stereo;
};

std::unique_ptr<juce::Component> Stereo::createDisplay()
{
    return display::row(std::make_unique<Vectorscope>(*this), 1.0f, std::make_unique<Scope>(*this), 1.2f);
}

class Analyzer final : public Effect
{
public:
    Analyzer() : Effect("Tomato Scope") {}

    void prepareToPlay(double, int) override {}
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto meters = std::make_unique<Meters>(std::vector<Meters::Meter> {
            { "L", [this] { return display::decibelsToUnit(juce::Decibels::gainToDecibels(analysis().takePeak(0), -60.0f)); }, display::secondary },
            { "R", [this] { return display::decibelsToUnit(juce::Decibels::gainToDecibels(analysis().takePeak(1), -60.0f)); }, display::secondary },
        });
        auto right = display::row(std::make_unique<Scope>(*this), 1.0f, std::move(meters), 0.35f);
        return display::row(std::make_unique<Spectrum>(*this), 1.3f, std::move(right), 1.0f);
    }
};

template <typename Type>
std::unique_ptr<juce::AudioProcessor> make()
{
    return std::make_unique<Type>();
}

using Factory = std::unique_ptr<juce::AudioProcessor> (*)();

const std::vector<std::pair<juce::String, Factory>>& effectTable()
{
    static const std::vector<std::pair<juce::String, Factory>> table {
        { "Tomato Reverb", make<Reverb> },
        { "Tomato Delay", make<Delay> },
        { "Tomato Compressor", make<Compressor> },
        { "Tomato Limiter", make<Limiter> },
        { "Tomato Filter", make<Filter> },
        { "Tomato Distortion", make<Distortion> },
        { "Tomato Chorus", make<Chorus> },
        { "Tomato Phaser", make<Phaser> },
        { "Tomato Stereo", make<Stereo> },
        { "Tomato Scope", make<Analyzer> },
    };
    return table;
}
}

juce::StringArray effects()
{
    juce::StringArray result { "Tomato EQ" };
    for (const auto& [name, factory] : effectTable())
        result.add(name);
    result.addArray(characterEffects());
    result.addArray(motionEffects());
    result.addArray(LuaPlugins::names(false));
    result.addArray(NativePlugins::names(false));
    return result;
}

std::unique_ptr<juce::AudioProcessor> createEffect(const juce::String& name)
{
    if (name == "Tomato EQ")
        return createEqualizer();
    for (const auto& [effectName, factory] : effectTable())
        if (effectName == name)
            return factory();
    if (auto effect = createCharacterEffect(name))
        return effect;
    return createMotionEffect(name);
}
}
