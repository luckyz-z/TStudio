#include "Engine/Builtins/Displays.h"
#include "Engine/Builtins/Effect.h"

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

void raise(std::atomic<float>& value, float sample) noexcept
{
    if (sample > value.load(std::memory_order_relaxed))
        value.store(sample, std::memory_order_relaxed);
}

float coefficient(double milliseconds, double rate)
{
    return float(std::exp(-1.0 / juce::jmax(1.0, milliseconds * 0.001 * rate)));
}

class Multiband final : public Effect
{
public:
    static constexpr int bandCount = 3;

    Multiband()
        : Effect("Tomato Multiband"),
          lowCross(addFloat("lowCross", "Low crossover", skewed(40.0f, 800.0f, 200.0f), 160.0f, "Hz")),
          highCross(addFloat("highCross", "High crossover", skewed(1000.0f, 12000.0f, 3000.0f), 2500.0f, "Hz")),
          attack(addFloat("attack", "Attack", skewed(0.1f, 100.0f, 10.0f), 6.0f, "ms")),
          release(addFloat("release", "Release", skewed(10.0f, 1000.0f, 150.0f), 120.0f, "ms")),
          output(addFloat("output", "Output", { -18.0f, 12.0f }, 0.0f, "dB"))
    {
        static constexpr std::array<const char*, bandCount> names { "Low", "Mid", "High" };
        for (int b = 0; b < bandCount; ++b)
        {
            const juce::String label = names[size_t(b)];
            amounts[size_t(b)] = &addFloat("amount" + label, "Band " + juce::String(b + 1) + " " + label + " depth", { 0.0f, 1.0f }, 0.4f);
            gains[size_t(b)] = &addFloat("gain" + label, "Band " + juce::String(b + 1) + " " + label + " gain", { -12.0f, 12.0f }, 0.0f, "dB");
        }
        addPreset("Master glue", { { "amountLow", 0.3 }, { "amountMid", 0.25 }, { "amountHigh", 0.3 }, { "attack", 15.0 }, { "release", 180.0 } });
        addPreset("Squash it (OTT style)", { { "amountLow", 0.9 }, { "amountMid", 0.85 }, { "amountHigh", 0.9 }, { "gainLow", 4.0 }, { "gainMid", 2.0 },
                                             { "gainHigh", 5.0 }, { "attack", 2.0 }, { "release", 60.0 }, { "output", -3.0 } });
        addPreset("Tame the lows", { { "amountLow", 0.7 }, { "amountMid", 0.0 }, { "amountHigh", 0.0 }, { "lowCross", 180.0 } });
        addPreset("Smooth highs", { { "amountLow", 0.0 }, { "amountMid", 0.2 }, { "amountHigh", 0.7 }, { "highCross", 5000.0 } });
        addPreset("Vocal presence", { { "amountLow", 0.4 }, { "amountMid", 0.5 }, { "amountHigh", 0.3 }, { "gainMid", 2.5 }, { "gainHigh", 1.5 } });
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        rate = sampleRate;
        const auto spec = specFor(sampleRate, samplesPerBlock);
        for (auto* filter : { &lowSplit, &lowHigh, &midSplit, &midHigh })
        {
            filter->prepare(spec);
            filter->reset();
        }
        lowSplit.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
        lowHigh.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
        midSplit.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
        midHigh.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
        for (auto& band : bands)
            band.setSize(2, samplesPerBlock, false, false, true);
        envelopes.fill(-120.0f);
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto samples = buffer.getNumSamples();
        for (auto& band : bands)
            if (band.getNumSamples() < samples)
                band.setSize(2, samples, false, false, true);
        lowSplit.setCutoffFrequency(lowCross.get());
        lowHigh.setCutoffFrequency(lowCross.get());
        midSplit.setCutoffFrequency(juce::jmax(lowCross.get() * 1.5f, highCross.get()));
        midHigh.setCutoffFrequency(juce::jmax(lowCross.get() * 1.5f, highCross.get()));

        for (int c = 0; c < 2; ++c)
            for (auto& band : bands)
                band.copyFrom(c, 0, buffer, c, 0, samples);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < samples; ++i)
            {
                const auto input = buffer.getSample(c, i);
                const auto low = lowSplit.processSample(c, input);
                const auto rest = lowHigh.processSample(c, input);
                bands[0].setSample(c, i, low);
                bands[1].setSample(c, i, midSplit.processSample(c, rest));
                bands[2].setSample(c, i, midHigh.processSample(c, rest));
            }

        const auto attackCoefficient = coefficient(attack.get(), rate);
        const auto releaseCoefficient = coefficient(release.get(), rate);
        buffer.clear(0, 0, samples);
        buffer.clear(1, 0, samples);
        for (int b = 0; b < bandCount; ++b)
        {
            const auto amount = amounts[size_t(b)]->get();
            const auto threshold = -6.0f - amount * 30.0f;
            const auto ratio = 1.0f + amount * 9.0f;
            const auto makeup = juce::Decibels::decibelsToGain(gains[size_t(b)]->get() + amount * 6.0f);
            auto& envelope = envelopes[size_t(b)];
            auto deepest = 0.0f;
            auto* left = bands[size_t(b)].getWritePointer(0);
            auto* right = bands[size_t(b)].getWritePointer(1);
            for (int i = 0; i < samples; ++i)
            {
                const auto level = juce::Decibels::gainToDecibels(juce::jmax(std::abs(left[i]), std::abs(right[i])), -120.0f);
                envelope = level + (level > envelope ? attackCoefficient : releaseCoefficient) * (envelope - level);
                const auto over = envelope - threshold;
                const auto reduction = over > 0.0f ? over * (1.0f / ratio - 1.0f) : 0.0f;
                deepest = juce::jmin(deepest, reduction);
                const auto gain = juce::Decibels::decibelsToGain(reduction) * makeup;
                buffer.addSample(0, i, left[i] * gain);
                buffer.addSample(1, i, right[i] * gain);
            }
            raise(reductions[size_t(b)], -deepest);
        }
        buffer.applyGain(0, 0, samples, juce::Decibels::decibelsToGain(output.get()));
        buffer.applyGain(1, 0, samples, juce::Decibels::decibelsToGain(output.get()));
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto meters = std::make_unique<Meters>(std::vector<Meters::Meter> {
            { "LOW", [this] { return juce::jlimit(0.0f, 1.0f, reductions[0].exchange(0.0f) / 24.0f); }, display::trace },
            { "MID", [this] { return juce::jlimit(0.0f, 1.0f, reductions[1].exchange(0.0f) / 24.0f); }, display::trace },
            { "HIGH", [this] { return juce::jlimit(0.0f, 1.0f, reductions[2].exchange(0.0f) / 24.0f); }, display::trace },
        });
        return display::row(std::make_unique<Spectrum>(*this), 1.8f, std::move(meters), 1.0f);
    }

private:
    juce::AudioParameterFloat &lowCross, &highCross, &attack, &release, &output;
    std::array<juce::AudioParameterFloat*, bandCount> amounts {}, gains {};
    juce::dsp::LinkwitzRileyFilter<float> lowSplit, lowHigh, midSplit, midHigh;
    std::array<juce::AudioBuffer<float>, bandCount> bands;
    std::array<float, bandCount> envelopes {};
    std::array<std::atomic<float>, bandCount> reductions {};
    double rate = 44100.0;
};

class Transient final : public Effect
{
public:
    Transient()
        : Effect("Tomato Transient"),
          attackAmount(addFloat("attack", "Attack", { -1.0f, 1.0f }, 0.4f)),
          sustainAmount(addFloat("sustain", "Sustain", { -1.0f, 1.0f }, 0.0f)),
          speed(addFloat("speed", "Speed", skewed(5.0f, 200.0f, 40.0f), 40.0f, "ms")),
          output(addFloat("output", "Output", { -18.0f, 12.0f }, 0.0f, "dB"))
    {
        addPreset("Snappy drums", { { "attack", 0.6 }, { "sustain", -0.2 } });
        addPreset("Tighten 808", { { "attack", 0.2 }, { "sustain", -0.6 }, { "speed", 60.0 } });
        addPreset("Fat room", { { "attack", -0.2 }, { "sustain", 0.6 }, { "speed", 80.0 } });
        addPreset("Kill the click", { { "attack", -0.7 }, { "sustain", 0.0 }, { "speed", 20.0 } });
    }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        fast = slow = -120.0f;
    }

    float gainFor(float difference) const
    {
        const auto amount = difference > 0.0f ? attackAmount.get() : sustainAmount.get();
        return juce::jlimit(-18.0f, 18.0f, std::abs(difference) * amount * 1.5f);
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto fastAttack = coefficient(0.5, rate), fastRelease = coefficient(speed.get() * 0.5, rate);
        const auto slowAttack = coefficient(speed.get(), rate), slowRelease = coefficient(speed.get() * 4.0, rate);
        const auto out = juce::Decibels::decibelsToGain(output.get());
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        auto biggest = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto level = juce::Decibels::gainToDecibels(juce::jmax(std::abs(left[i]), std::abs(right[i])), -120.0f);
            fast = level + (level > fast ? fastAttack : fastRelease) * (fast - level);
            slow = level + (level > slow ? slowAttack : slowRelease) * (slow - level);
            const auto change = gainFor(fast - slow);
            biggest = std::abs(change) > std::abs(biggest) ? change : biggest;
            const auto gain = juce::Decibels::decibelsToGain(change) * out;
            left[i] *= gain;
            right[i] *= gain;
        }
        shaping.store(biggest, std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto curve = std::make_unique<Curve>([this](float x) { return gainFor(x); }, juce::Range<float>(-24.0f, 24.0f), juce::Range<float>(-18.0f, 18.0f),
                                             [this] { return shaping.load(); }, "SUSTAIN  <  envelope  >  ATTACK", "GAIN dB");
        return display::row(std::move(curve), 1.4f, std::make_unique<Scope>(*this), 1.0f);
    }

private:
    juce::AudioParameterFloat &attackAmount, &sustainAmount, &speed, &output;
    double rate = 44100.0;
    float fast = -120.0f, slow = -120.0f;
    std::atomic<float> shaping { 0.0f };
};

class Flanger final : public Effect
{
public:
    Flanger()
        : Effect("Tomato Flanger"),
          rate(addFloat("rate", "Rate", skewed(0.02f, 10.0f, 0.5f), 0.25f, "Hz")),
          depth(addFloat("depth", "Depth", { 0.0f, 1.0f }, 0.7f)),
          delayTime(addFloat("delay", "Delay", skewed(0.2f, 15.0f, 2.0f), 1.5f, "ms")),
          feedback(addFloat("feedback", "Feedback", { -0.95f, 0.95f }, 0.55f)),
          stereo(addFloat("stereo", "Stereo", { 0.0f, 1.0f }, 0.25f)),
          mix(addFloat("mix", "Mix", { 0.0f, 1.0f }, 0.5f))
    {
        addPreset("Jet sweep", { { "rate", 0.12 }, { "depth", 0.9 }, { "feedback", 0.8 }, { "mix", 0.5 } });
        addPreset("Subtle shimmer", { { "rate", 0.6 }, { "depth", 0.4 }, { "feedback", 0.3 }, { "mix", 0.3 }, { "stereo", 0.5 } });
        addPreset("Metallic", { { "rate", 0.05 }, { "depth", 0.3 }, { "delay", 0.4 }, { "feedback", -0.9 }, { "mix", 0.5 } });
        addPreset("Through zero-ish", { { "rate", 0.2 }, { "depth", 1.0 }, { "delay", 0.3 }, { "feedback", 0.6 }, { "mix", 0.5 } });
    }

    void prepareToPlay(double sampleRate, int) override
    {
        sample = sampleRate;
        line.setSize(2, int(sampleRate * 0.04) + 4);
        line.clear();
        write = 0;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto size = line.getNumSamples();
        const auto increment = rate.get() / sample;
        const auto baseSamples = delayTime.get() * 0.001 * sample;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            phase = std::fmod(phase + increment, 1.0);
            for (int c = 0; c < 2; ++c)
            {
                const auto lfo = 0.5 + 0.5 * std::sin(juce::MathConstants<double>::twoPi * (phase + (c == 1 ? stereo.get() * 0.5 : 0.0)));
                const auto delay = juce::jlimit(1.0, double(size - 2), baseSamples * (1.0 + depth.get() * 3.0 * lfo));
                auto readPosition = double(write) - delay;
                if (readPosition < 0.0)
                    readPosition += size;
                const auto index = int(readPosition);
                const auto fraction = float(readPosition - index);
                const auto* data = line.getReadPointer(c);
                const auto delayed = data[index] + (data[(index + 1) % size] - data[index]) * fraction;
                const auto input = buffer.getSample(c, i);
                line.setSample(c, write, std::tanh(input + delayed * feedback.get()));
                buffer.setSample(c, i, input + (delayed - input) * mix.get() * (1.0f - 0.5f * mix.get()));
            }
            write = (write + 1) % size;
        }
        sweep.store(float(phase), std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto curve = std::make_unique<Curve>([this](float x) { return delayTime.get() * (1.0f + depth.get() * 3.0f * (0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * x))); },
                                             juce::Range<float>(0.0f, 1.0f), juce::Range<float>(0.0f, 60.0f), [this] { return sweep.load(); }, "LFO CYCLE", "DELAY ms");
        return display::row(std::move(curve), 1.2f, std::make_unique<Spectrum>(*this), 1.0f);
    }

private:
    juce::AudioParameterFloat &rate, &depth, &delayTime, &feedback, &stereo, &mix;
    juce::AudioBuffer<float> line;
    double sample = 44100.0, phase = 0.0;
    int write = 0;
    std::atomic<float> sweep { 0.0f };
};

class Shaper final : public Effect
{
public:
    Shaper()
        : Effect("Tomato Shaper"),
          mode(addChoice("mode", "Curve", { "Soft saturate", "Hard clip", "Sine fold", "Tube (asymmetric)", "Rectify" }, 0)),
          drive(addFloat("drive", "Drive", { 0.0f, 36.0f }, 12.0f, "dB")),
          bias(addFloat("bias", "Bias", { -0.5f, 0.5f }, 0.0f)),
          tone(addFloat("tone", "Tone", skewed(500.0f, 20000.0f, 4000.0f), 12000.0f, "Hz")),
          mix(addFloat("mix", "Mix", { 0.0f, 1.0f }, 1.0f)),
          output(addFloat("output", "Output", { -24.0f, 6.0f }, -6.0f, "dB"))
    {
        addPreset("Warm saturation", { { "mode", 0.0 }, { "drive", 8.0 }, { "output", -3.0 } });
        addPreset("Brick clip", { { "mode", 1.0 }, { "drive", 14.0 }, { "output", -8.0 } });
        addPreset("Wavefold lead", { { "mode", 2.0 }, { "drive", 20.0 }, { "tone", 9000.0 }, { "output", -8.0 } });
        addPreset("Tube growl", { { "mode", 3.0 }, { "drive", 18.0 }, { "bias", 0.15 }, { "output", -9.0 } });
        addPreset("Octave fuzz", { { "mode", 4.0 }, { "drive", 10.0 }, { "tone", 6000.0 }, { "mix", 0.6 }, { "output", -6.0 } });
    }

    float shape(float x) const
    {
        const auto g = juce::Decibels::decibelsToGain(drive.get());
        const auto in = x * g + bias.get();
        switch (mode.getIndex())
        {
            case 1: return juce::jlimit(-1.0f, 1.0f, in);
            case 2: return std::sin(in * juce::MathConstants<float>::halfPi);
            case 3: return in >= 0.0f ? std::tanh(in) : std::tanh(in * 0.6f) * 1.2f;
            case 4: return std::abs(std::tanh(in)) * 1.4f - 0.5f;
            default: return std::tanh(in);
        }
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        rate = sampleRate;
        oversampling.initProcessing(size_t(juce::jmax(1, samplesPerBlock)));
        oversampling.reset();
        lows.fill(0.0f);
        dcIn.fill(0.0f);
        dcOut.fill(0.0f);
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto samples = buffer.getNumSamples();
        dry.makeCopyOf(buffer, true);
        juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(), 2, size_t(samples));
        auto up = oversampling.processSamplesUp(block);
        const auto offset = shape(0.0f);
        for (size_t c = 0; c < 2; ++c)
        {
            auto* data = up.getChannelPointer(c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                data[i] = shape(data[i]) - offset;
        }
        oversampling.processSamplesDown(block);
        const auto toneCoefficient = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * tone.get() / float(rate));
        const auto out = juce::Decibels::decibelsToGain(output.get());
        const auto wet = mix.get();
        for (int c = 0; c < 2; ++c)
        {
            auto* data = buffer.getWritePointer(c);
            const auto* original = dry.getReadPointer(c);
            for (int i = 0; i < samples; ++i)
            {
                lows[size_t(c)] += (data[i] - lows[size_t(c)]) * toneCoefficient;
                const auto filtered = lows[size_t(c)] - dcIn[size_t(c)] + 0.995f * dcOut[size_t(c)];
                dcIn[size_t(c)] = lows[size_t(c)];
                dcOut[size_t(c)] = filtered;
                data[i] = (original[i] + (filtered - original[i]) * wet) * out;
            }
        }
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto curve = std::make_unique<Curve>([this](float x) { return shape(x) - shape(0.0f); }, juce::Range<float>(-1.0f, 1.0f), juce::Range<float>(-1.5f, 1.5f), nullptr,
                                             "INPUT", "OUTPUT");
        return display::row(std::move(curve), 1.0f, std::make_unique<Spectrum>(*this), 1.4f);
    }

private:
    juce::AudioParameterChoice& mode;
    juce::AudioParameterFloat &drive, &bias, &tone, &mix, &output;
    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::AudioBuffer<float> dry;
    std::array<float, 2> lows {}, dcIn {}, dcOut {};
    double rate = 44100.0;
};

class Glitch final : public Effect
{
public:
    static constexpr int stepCount = 16;
    static inline const juce::StringArray modes { "-", "Mute", "Repeat", "Half", "Reverse", "Stop" };
    static inline const juce::StringArray shortNames { "", "M", "R", "1/2", "<", "S" };

    Glitch()
        : Effect("Tomato Glitch"),
          length(addChoice("length", "Pattern length", { "1 beat", "2 beats", "1 bar", "2 bars" }, 2)),
          mix(addFloat("mix", "Mix", { 0.0f, 1.0f }, 1.0f))
    {
        for (int i = 0; i < stepCount; ++i)
            steps[size_t(i)] = &addChoice("step" + juce::String(i + 1), "Step " + juce::String(i + 1), modes, 0);
        presetSteps("Half-time drop", { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 });
        presetSteps("Stutter end", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2 });
        presetSteps("Reverse fill", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4 });
        presetSteps("Tape stop", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5 });
        presetSteps("Trap gate", { 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 2, 2 });
        presetSteps("Chop up", { 0, 1, 0, 2, 0, 1, 4, 0, 0, 1, 0, 2, 3, 3, 5, 5 });
    }

    juce::AudioParameterChoice& step(int index) { return *steps[size_t(index)]; }
    bool showsInPanel(const juce::String& id) const override { return !id.startsWith("step"); }
    int activeStep() const { return current.load(std::memory_order_relaxed); }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        history.setSize(2, int(sampleRate * 16.0));
        history.clear();
        write = 0;
        lastStep = -1;
        stepStart = 0;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        static constexpr std::array<double, 4> lengths { 1.0, 2.0, 4.0, 8.0 };
        const auto patternBeats = lengths[size_t(length.getIndex())];
        const auto stepBeats = patternBeats / stepCount;
        const auto bpm = tempo();
        const auto stepSamples = stepBeats * 60.0 / bpm * rate;
        const auto beatStep = bpm / 60.0 / rate;
        auto position = beats();
        const auto playing = [this]
        {
            if (auto* head = getPlayHead())
                if (const auto info = head->getPosition())
                    return info->getIsPlaying();
            return false;
        }();
        const auto size = history.getNumSamples();
        const auto wet = mix.get();

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto left = buffer.getSample(0, i);
            const auto right = buffer.getSample(1, i);
            history.setSample(0, write, left);
            history.setSample(1, write, right);

            const auto index = int(std::floor(std::fmod(juce::jmax(0.0, position), patternBeats) / stepBeats)) % stepCount;
            if (index != lastStep)
            {
                lastStep = index;
                stepStart = write;
            }
            const auto phase = double((write - stepStart + size) % size);
            const auto mode = playing ? steps[size_t(index)]->getIndex() : 0;
            auto outLeft = left, outRight = right;
            if (mode != 0)
            {
                double read = double(stepStart);
                auto gain = 1.0f;
                switch (mode)
                {
                    case 1: gain = 0.0f; break;
                    case 2: read += std::fmod(phase, juce::jmax(64.0, stepSamples * 0.25)); break;
                    case 3: read += phase * 0.5; break;
                    case 4: read -= phase; break;
                    default:
                    {
                        const auto progress = juce::jlimit(0.0, 1.0, phase / juce::jmax(1.0, stepSamples));
                        read += phase * (1.0 - progress * 0.5);
                        gain = float(1.0 - progress * progress);
                        break;
                    }
                }
                const auto fade = float(juce::jlimit(0.0, 1.0, juce::jmin(phase, stepSamples - phase) / (rate * 0.002)));
                if (mode != 1)
                {
                    read = std::fmod(read + size * 4.0, double(size));
                    const auto at = int(read);
                    const auto fraction = float(read - at);
                    const auto next = (at + 1) % size;
                    const auto heldLeft = history.getSample(0, at) + (history.getSample(0, next) - history.getSample(0, at)) * fraction;
                    const auto heldRight = history.getSample(1, at) + (history.getSample(1, next) - history.getSample(1, at)) * fraction;
                    outLeft = left + (heldLeft * gain * fade + left * (1.0f - fade) - left) * wet;
                    outRight = right + (heldRight * gain * fade + right * (1.0f - fade) - right) * wet;
                }
                else
                {
                    outLeft = left * (1.0f - wet * fade);
                    outRight = right * (1.0f - wet * fade);
                }
            }
            buffer.setSample(0, i, outLeft);
            buffer.setSample(1, i, outRight);
            write = (write + 1) % size;
            position += beatStep;
        }
        current.store(playing ? lastStep : -1, std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createDisplay() override;

private:
    void presetSteps(const char* title, std::array<int, stepCount> values)
    {
        std::vector<std::pair<juce::String, double>> pairs;
        for (int i = 0; i < stepCount; ++i)
            pairs.emplace_back("step" + juce::String(i + 1), values[size_t(i)]);
        addPresetValues(title, pairs);
    }

    juce::AudioParameterChoice& length;
    juce::AudioParameterFloat& mix;
    std::array<juce::AudioParameterChoice*, stepCount> steps {};
    juce::AudioBuffer<float> history;
    double rate = 44100.0;
    int write = 0, lastStep = -1, stepStart = 0;
    std::atomic<int> current { -1 };
};

class GlitchGrid final : public display::Display
{
public:
    explicit GlitchGrid(Glitch& g) : glitch(g) {}

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto index = stepAt(e.position);
        if (index < 0)
            return;
        auto& step = glitch.step(index);
        const auto next = e.mods.isPopupMenu() ? 0 : (step.getIndex() + 1) % Glitch::modes.size();
        step.setValueNotifyingHost(step.convertTo0to1(float(next)));
        repaint();
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedTop(16.0f); }

    juce::Rectangle<float> cell(int index) const
    {
        const auto bounds = area();
        const auto width = bounds.getWidth() / float(Glitch::stepCount);
        return { bounds.getX() + width * float(index), bounds.getY(), width, bounds.getHeight() };
    }

    int stepAt(juce::Point<float> point) const
    {
        for (int i = 0; i < Glitch::stepCount; ++i)
            if (cell(i).contains(point))
                return i;
        return -1;
    }

    void draw(juce::Graphics& g, juce::Rectangle<float> bounds) override
    {
        g.setColour(display::label);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("CLICK A STEP TO CYCLE:  - / MUTE / REPEAT / HALF / REVERSE / STOP   (RIGHT-CLICK RESETS)", bounds.removeFromTop(14.0f),
                   juce::Justification::centredLeft);
        static const std::array<juce::Colour, 6> colours { juce::Colour(0xff2d3438), juce::Colour(0xff15191b), juce::Colour(0xffe8483b), juce::Colour(0xff6a8fc2),
                                                           juce::Colour(0xffa36cb8), juce::Colour(0xffc28b5a) };
        const auto active = glitch.activeStep();
        for (int i = 0; i < Glitch::stepCount; ++i)
        {
            const auto r = cell(i).reduced(2.0f);
            const auto mode = glitch.step(i).getIndex();
            g.setColour(colours[size_t(mode)].brighter(i == active ? 0.35f : 0.0f));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(i == active ? display::trace : juce::Colour(0xff0d1012));
            g.drawRoundedRectangle(r, 3.0f, i == active ? 2.0f : 1.0f);
            g.setColour(juce::Colour(0xffe4eaec));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText(Glitch::shortNames[mode], r, juce::Justification::centred);
            if (i % 4 == 0)
            {
                g.setColour(display::label);
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(juce::String(i / 4 + 1), r.withY(r.getBottom() - 12.0f).withHeight(12.0f), juce::Justification::centred);
            }
        }
    }

    void tick() override { repaint(); }

    Glitch& glitch;
};

std::unique_ptr<juce::Component> Glitch::createDisplay()
{
    return std::make_unique<GlitchGrid>(*this);
}

template <typename T>
std::unique_ptr<juce::AudioProcessor> make()
{
    return std::make_unique<T>();
}

const std::vector<std::pair<juce::String, std::unique_ptr<juce::AudioProcessor> (*)()>>& table()
{
    static const std::vector<std::pair<juce::String, std::unique_ptr<juce::AudioProcessor> (*)()>> entries {
        { "Tomato Multiband", make<Multiband> }, { "Tomato Transient", make<Transient> }, { "Tomato Flanger", make<Flanger> },
        { "Tomato Shaper", make<Shaper> },       { "Tomato Glitch", make<Glitch> },
    };
    return entries;
}
}

const juce::StringArray& motionEffects()
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

std::unique_ptr<juce::AudioProcessor> createMotionEffect(const juce::String& name)
{
    for (const auto& [effectName, factory] : table())
        if (effectName == name)
            return factory();
    return nullptr;
}
}
