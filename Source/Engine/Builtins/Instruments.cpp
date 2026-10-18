#include "Engine/Builtins/Registry.h"
#include "Engine/Builtins/BuiltinProcessor.h"
#include "Engine/Builtins/Displays.h"

#include <array>
#include <cmath>

namespace ts::Builtins
{
namespace
{
constexpr int voiceCount = 16;
constexpr double twoPi = juce::MathConstants<double>::twoPi;

struct AnySound final : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class Instrument : public BuiltinProcessor
{
public:
    explicit Instrument(const juce::String& name) : BuiltinProcessor(name, true) { synth.addSound(new AnySound()); }

    void prepareToPlay(double sampleRate, int) override
    {
        synth.setCurrentPlaybackSampleRate(sampleRate);
        prepareVoices(sampleRate);
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        update();
        synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
    }

protected:
    virtual void update() {}
    virtual void prepareVoices(double) {}

    juce::Synthesiser synth;
};

class Pluck final : public Instrument
{
public:
    Pluck()
        : Instrument("Tomato Pluck"),
          decay(addFloat("decay", "Decay", skewed(0.1f, 12.0f, 2.0f), 2.5f, "s")),
          brightness(addFloat("brightness", "Brightness", { 0.0f, 1.0f }, 0.6f)),
          damping(addFloat("damping", "Damping", { 0.0f, 1.0f }, 0.35f)),
          pick(addFloat("pick", "Pick position", { 0.05f, 0.5f }, 0.18f)),
          release(addFloat("release", "Release", skewed(5.0f, 2000.0f, 150.0f), 120.0f, "ms")),
          width(addFloat("width", "Stereo width", { 0.0f, 1.0f }, 0.4f)),
          volume(addFloat("volume", "Volume", { 0.0f, 1.0f }, 0.6f))
    {
        for (int i = 0; i < voiceCount; ++i)
            synth.addVoice(new Voice(*this));
        addPreset("Nylon guitar", { { "decay", 2.8f }, { "brightness", 0.45f }, { "damping", 0.4f }, { "pick", 0.2f } });
        addPreset("Steel guitar", { { "decay", 4.0f }, { "brightness", 0.75f }, { "damping", 0.2f }, { "pick", 0.15f } });
        addPreset("Harp", { { "decay", 4.0f }, { "brightness", 0.7f }, { "damping", 0.25f }, { "pick", 0.3f }, { "release", 600.0f }, { "width", 0.7f } });
        addPreset("Koto", { { "decay", 2.0f }, { "brightness", 0.85f }, { "damping", 0.1f }, { "pick", 0.08f } });
        addPreset("Muted bass", { { "decay", 0.6f }, { "brightness", 0.3f }, { "damping", 0.7f }, { "pick", 0.25f }, { "width", 0.0f } });
    }

    std::unique_ptr<juce::Component> createDisplay() override { return std::make_unique<display::Scope>(*this); }

private:
    struct Settings
    {
        float decay = 2.5f, brightness = 0.6f, damping = 0.35f, pick = 0.18f, release = 0.12f, width = 0.4f, volume = 0.6f;
    };

    class Voice final : public juce::SynthesiserVoice
    {
    public:
        explicit Voice(Pluck& o) : owner(o) {}

        bool canPlaySound(juce::SynthesiserSound*) override { return true; }

        void setCurrentPlaybackSampleRate(double rate) override
        {
            SynthesiserVoice::setCurrentPlaybackSampleRate(rate);
            if (rate > 0.0)
                line.assign(size_t(rate / 20.0) + 4, 0.0f);
        }

        void startNote(int note, float velocity, juce::SynthesiserSound*, int) override
        {
            const auto& s = owner.settings;
            const auto rate = getSampleRate();
            period = juce::jlimit(2.0, double(line.size() - 3), rate / juce::MidiMessage::getMidiNoteInHertz(note));
            feedback = float(std::pow(10.0, -3.0 * period / (rate * s.decay)));
            stretch = 0.5f - s.damping * 0.45f;
            std::fill(line.begin(), line.end(), 0.0f);
            const auto length = int(period);
            const auto pickOffset = juce::jmax(1, int(period * s.pick));
            auto smooth = 0.0f;
            const auto coefficient = 0.15f + s.brightness * 0.85f;
            for (int i = 0; i < length; ++i)
            {
                smooth += (random.nextFloat() * 2.0f - 1.0f - smooth) * coefficient;
                line[size_t(i)] = smooth * velocity;
            }
            for (int i = length; --i >= pickOffset;)
                line[size_t(i)] -= line[size_t(i - pickOffset)] * 0.6f;
            write = length;
            previous = 0.0f;
            lastInput = blocked = 0.0f;
            gain = 1.0f;
            releasing = false;
            pan = juce::jlimit(-1.0f, 1.0f, (float(note) - 60.0f) / 24.0f * s.width);
        }

        void stopNote(float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                releasing = true;
                return;
            }
            clearCurrentNote();
        }

        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override
        {
            if (!isVoiceActive())
                return;
            const auto& s = owner.settings;
            const auto size = int(line.size());
            const auto releaseFactor = float(std::exp(-1.0 / (juce::jmax(0.005f, s.release) * getSampleRate())));
            const auto left = juce::jmin(1.0f, 1.0f - pan) * s.volume;
            const auto right = juce::jmin(1.0f, 1.0f + pan) * s.volume;
            auto* outLeft = output.getWritePointer(0, startSample);
            auto* outRight = output.getWritePointer(juce::jmin(1, output.getNumChannels() - 1), startSample);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto readPosition = double(write) - period;
                auto index = int(std::floor(readPosition));
                const auto fraction = float(readPosition - index);
                index = (index % size + size) % size;
                const auto next = (index + 1) % size;
                const auto delayed = line[size_t(index)] + (line[size_t(next)] - line[size_t(index)]) * fraction;
                const auto filtered = (1.0f - stretch) * delayed + stretch * previous;
                previous = delayed;
                const auto value = filtered * feedback;
                line[size_t(write)] = value;
                write = (write + 1) % size;

                if (releasing)
                    gain *= releaseFactor;
                blocked = value - lastInput + 0.995f * blocked;
                lastInput = value;
                const auto sample = blocked * gain;
                outLeft[i] += sample * left;
                if (outRight != outLeft)
                    outRight[i] += sample * right;
            }
            if (gain < 0.0005f || (std::abs(previous) < 1.0e-5f && feedback < 0.9999f && !hasEnergy()))
                clearCurrentNote();
        }

    private:
        bool hasEnergy() const
        {
            return std::any_of(line.begin(), line.end(), [](float v) { return std::abs(v) > 1.0e-4f; });
        }

        Pluck& owner;
        std::vector<float> line;
        juce::Random random;
        double period = 100.0;
        int write = 0;
        float feedback = 0.99f, stretch = 0.5f, previous = 0.0f, gain = 1.0f, pan = 0.0f, lastInput = 0.0f, blocked = 0.0f;
        bool releasing = false;
    };

    void update() override
    {
        settings = { decay.get(), brightness.get(), damping.get(), pick.get(), release.get() * 0.001f, width.get(), volume.get() };
    }

    juce::AudioParameterFloat &decay, &brightness, &damping, &pick, &release, &width, &volume;
    Settings settings;
};

class Keys final : public Instrument
{
public:
    Keys()
        : Instrument("Tomato Keys"),
          tone(addFloat("tone", "Tone", { 0.0f, 1.0f }, 0.45f)),
          bell(addFloat("bell", "Bell", { 0.0f, 1.0f }, 0.3f)),
          decay(addFloat("decay", "Decay", skewed(0.3f, 10.0f, 2.0f), 3.0f, "s")),
          release(addFloat("release", "Release", skewed(10.0f, 3000.0f, 300.0f), 260.0f, "ms")),
          tremoloRate(addFloat("tremoloRate", "Tremolo rate", skewed(0.1f, 10.0f, 4.0f), 4.5f, "Hz")),
          tremoloDepth(addFloat("tremoloDepth", "Tremolo depth", { 0.0f, 1.0f }, 0.2f)),
          volume(addFloat("volume", "Volume", { 0.0f, 1.0f }, 0.5f))
    {
        for (int i = 0; i < voiceCount; ++i)
            synth.addVoice(new Voice(*this));
        addPreset("Rhodes", { { "tone", 0.35f }, { "bell", 0.25f }, { "decay", 4.0f }, { "tremoloRate", 4.0f }, { "tremoloDepth", 0.3f } });
        addPreset("Bright EP", { { "tone", 0.7f }, { "bell", 0.45f }, { "decay", 3.0f }, { "tremoloDepth", 0.1f } });
        addPreset("Music box", { { "tone", 0.15f }, { "bell", 1.0f }, { "decay", 1.4f }, { "release", 500.0f }, { "tremoloDepth", 0.0f } });
        addPreset("Glockenspiel", { { "tone", 0.1f }, { "bell", 1.0f }, { "decay", 1.0f }, { "tremoloDepth", 0.0f } });
        addPreset("Lofi keys", { { "tone", 0.3f }, { "bell", 0.2f }, { "decay", 3.5f }, { "tremoloRate", 3.5f }, { "tremoloDepth", 0.45f } });
    }

    std::unique_ptr<juce::Component> createDisplay() override { return std::make_unique<display::Scope>(*this); }

private:
    struct Settings
    {
        float tone = 0.45f, bell = 0.3f, decay = 3.0f, release = 0.26f, tremoloRate = 4.5f, tremoloDepth = 0.2f, volume = 0.5f;
    };

    class Voice final : public juce::SynthesiserVoice
    {
    public:
        explicit Voice(Keys& o) : owner(o) {}

        bool canPlaySound(juce::SynthesiserSound*) override { return true; }

        void startNote(int note, float velocity, juce::SynthesiserSound*, int) override
        {
            frequency = juce::MidiMessage::getMidiNoteInHertz(note);
            gain = velocity;
            carrier = modulator = tine = 0.0;
            time = 0.0;
            amplitude = 1.0f;
            releasing = false;
        }

        void stopNote(float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                releasing = true;
                return;
            }
            clearCurrentNote();
        }

        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override
        {
            if (!isVoiceActive())
                return;
            const auto& s = owner.settings;
            const auto rate = getSampleRate();
            const auto decayFactor = float(std::exp(-1.0 / (s.decay * rate * (1.0 - juce::jlimit(0.0, 0.6, (frequency - 200.0) / 2000.0)))));
            const auto releaseFactor = float(std::exp(-1.0 / (juce::jmax(0.01f, s.release) * rate)));
            const auto index = (0.6 + 2.4 * s.tone) * (0.35 + 0.65 * gain);
            auto* outLeft = output.getWritePointer(0, startSample);
            auto* outRight = output.getWritePointer(juce::jmin(1, output.getNumChannels() - 1), startSample);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto modulatorEnvelope = std::exp(-time * 3.5);
                const auto tineEnvelope = std::exp(-time * 14.0);
                const auto modulation = std::sin(twoPi * modulator) * index * modulatorEnvelope;
                const auto body = std::sin(twoPi * carrier + modulation);
                const auto bark = std::sin(twoPi * tine) * s.bell * tineEnvelope * 0.35;
                const auto tremolo = 1.0 - s.tremoloDepth * 0.5 * (1.0 + std::sin(twoPi * s.tremoloRate * time));
                const auto sample = float((body + bark) * tremolo) * amplitude * gain * s.volume;
                outLeft[i] += sample;
                if (outRight != outLeft)
                    outRight[i] += sample;

                amplitude *= releasing ? releaseFactor : decayFactor;
                carrier = std::fmod(carrier + frequency / rate, 1.0);
                modulator = std::fmod(modulator + frequency / rate, 1.0);
                tine = std::fmod(tine + frequency * 14.0 / rate, 1.0);
                time += 1.0 / rate;
            }
            if (amplitude < 0.0005f)
                clearCurrentNote();
        }

    private:
        Keys& owner;
        double frequency = 440.0, carrier = 0.0, modulator = 0.0, tine = 0.0, time = 0.0;
        float gain = 1.0f, amplitude = 1.0f;
        bool releasing = false;
    };

    void update() override
    {
        settings = { tone.get(), bell.get(), decay.get(), release.get() * 0.001f, tremoloRate.get(), tremoloDepth.get(), volume.get() };
    }

    juce::AudioParameterFloat &tone, &bell, &decay, &release, &tremoloRate, &tremoloDepth, &volume;
    Settings settings;
};

class Bass808 final : public BuiltinProcessor
{
public:
    Bass808()
        : BuiltinProcessor("Tomato 808", true),
          tune(addFloat("tune", "Tune", { -12.0f, 12.0f }, 0.0f, "st")),
          glide(addFloat("glide", "Glide", skewed(0.0f, 600.0f, 120.0f), 90.0f, "ms")),
          decay(addFloat("decay", "Decay", skewed(0.1f, 10.0f, 1.5f), 2.2f, "s")),
          release(addFloat("release", "Release", skewed(5.0f, 1000.0f, 120.0f), 70.0f, "ms")),
          punch(addFloat("punch", "Punch", { 0.0f, 36.0f }, 14.0f, "st")),
          punchTime(addFloat("punchTime", "Punch time", skewed(5.0f, 200.0f, 40.0f), 35.0f, "ms")),
          drive(addFloat("drive", "Drive", { 0.0f, 30.0f }, 9.0f, "dB")),
          tone(addFloat("tone", "Tone", skewed(150.0f, 12000.0f, 2000.0f), 3500.0f, "Hz")),
          volume(addFloat("volume", "Volume", { 0.0f, 1.0f }, 0.75f))
    {
        addPreset("Clean 808", { { "drive", 3.0f }, { "punch", 10.0f }, { "tone", 6000.0f } });
        addPreset("Distorted 808", { { "drive", 18.0f }, { "punch", 14.0f }, { "tone", 2500.0f }, { "decay", 2.5f } });
        addPreset("Long slide", { { "glide", 180.0f }, { "decay", 4.0f }, { "drive", 8.0f } });
        addPreset("Short punchy", { { "decay", 0.6f }, { "punch", 20.0f }, { "punchTime", 25.0f }, { "drive", 6.0f } });
        addPreset("Pluggnb sub", { { "drive", 0.0f }, { "punch", 4.0f }, { "tone", 8000.0f }, { "decay", 3.0f }, { "glide", 60.0f } });
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        auto pitch = std::make_unique<display::Curve>([this](float t) { return punch.get() * std::exp(-t / (punchTime.get() * 0.001f)); },
                                                      juce::Range<float>(0.0f, 0.25f), juce::Range<float>(0.0f, 37.0f), nullptr, "250 ms", "PITCH DROP st");
        return display::row(std::move(pitch), 1.0f, std::make_unique<display::Scope>(*this), 1.3f);
    }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        held.clear();
        held.reserve(128);
        amplitude = 0.0f;
        filtered = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        auto position = 0;
        for (const auto metadata : midi)
        {
            render(buffer, position, metadata.samplePosition);
            handle(metadata.getMessage());
            position = metadata.samplePosition;
        }
        render(buffer, position, buffer.getNumSamples());
    }

private:
    double frequencyFor(int note) const
    {
        return juce::MidiMessage::getMidiNoteInHertz(note) * std::exp2(tune.get() / 12.0);
    }

    void handle(const juce::MidiMessage& message)
    {
        if (message.isController() && message.getControllerNumber() == 5)
            slideSeconds = message.getControllerValue() * 0.01;
        else if (message.isController() && message.getControllerNumber() == 84)
            slideSource = message.getControllerValue();
        else if (message.isNoteOn())
        {
            const auto legato = !held.empty() && amplitude > 0.001f && !releasing;
            held.push_back(message.getNoteNumber());
            target = frequencyFor(message.getNoteNumber());
            slideCoefficient = 0.0;
            if (slideSource >= 0)
                slideCoefficient = std::exp(-4.0 / juce::jmax(1.0, slideSeconds * rate));
            if (slideSource >= 0 && !legato)
            {
                current = frequencyFor(slideSource);
                amplitude = 1.0f;
                age = 0.0;
                velocity = message.getFloatVelocity();
            }
            else if (!legato || (glide.get() <= 0.0f && slideSource < 0))
            {
                current = target;
                amplitude = 1.0f;
                age = 0.0;
                velocity = message.getFloatVelocity();
            }
            slideSource = -1;
            releasing = false;
        }
        else if (message.isNoteOff())
        {
            held.erase(std::remove(held.begin(), held.end(), message.getNoteNumber()), held.end());
            if (held.empty())
                releasing = true;
            else
                target = frequencyFor(held.back());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            held.clear();
            releasing = true;
        }
    }

    void render(juce::AudioBuffer<float>& buffer, int from, int to)
    {
        if (amplitude <= 0.0001f || to <= from)
            return;
        const auto glideCoefficient = slideCoefficient > 0.0 ? slideCoefficient : (glide.get() > 0.0f ? std::exp(-1.0 / (glide.get() * 0.001 * rate)) : 0.0);
        const auto decayFactor = float(std::exp(-1.0 / (decay.get() * rate)));
        const auto releaseFactor = float(std::exp(-1.0 / (release.get() * 0.001 * rate)));
        const auto driveGain = juce::Decibels::decibelsToGain(drive.get());
        const auto normaliser = 1.0f / std::tanh(driveGain);
        const auto toneCoefficient = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * tone.get() / float(rate));
        const auto punchDepth = punch.get() / 12.0;
        const auto punchSeconds = punchTime.get() * 0.001;
        const auto level = volume.get() * (0.4f + 0.6f * velocity);

        for (int i = from; i < to; ++i)
        {
            current = target + (current - target) * glideCoefficient;
            const auto pitch = current * std::exp2(punchDepth * std::exp(-age / punchSeconds));
            phase = std::fmod(phase + pitch / rate, 1.0);
            const auto raw = float(std::sin(twoPi * phase)) * amplitude;
            filtered += (std::tanh(raw * driveGain) * normaliser - filtered) * toneCoefficient;
            const auto sample = filtered * level;
            buffer.addSample(0, i, sample);
            buffer.addSample(1, i, sample);
            amplitude *= releasing ? releaseFactor : decayFactor;
            age += 1.0 / rate;
        }
    }

    juce::AudioParameterFloat &tune, &glide, &decay, &release, &punch, &punchTime, &drive, &tone, &volume;
    std::vector<int> held;
    double rate = 44100.0, phase = 0.0, current = 55.0, target = 55.0, age = 0.0;
    double slideSeconds = 0.1, slideCoefficient = 0.0;
    int slideSource = -1;
    float amplitude = 0.0f, filtered = 0.0f, velocity = 1.0f;
    bool releasing = false;
};
}

std::unique_ptr<juce::AudioProcessor> createInstrument(const juce::String& name)
{
    if (name == "Tomato 808")
        return std::make_unique<Bass808>();
    if (name == "Tomato Pluck")
        return std::make_unique<Pluck>();
    if (name == "Tomato Keys")
        return std::make_unique<Keys>();
    return nullptr;
}
}
