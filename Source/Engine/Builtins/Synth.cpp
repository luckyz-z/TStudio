#include "Engine/Builtins/Synth.h"
#include "Engine/Builtins/Displays.h"

#include <cmath>

namespace ts
{
namespace
{
constexpr int voiceCount = 16;
constexpr int controlRate = 16;
const juce::StringArray shapes { "Sine", "Triangle", "Saw", "Square", "Noise" };

struct AnySound final : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

double polyBlep(double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}
}

class Synth::Voice final : public juce::SynthesiserVoice
{
public:
    explicit Voice(Synth& o) : owner(o), settings(o.settings) {}

    bool canPlaySound(juce::SynthesiserSound*) override { return true; }

    void slideTo(int note, double milliseconds)
    {
        sounding = note;
        frequency = juce::MidiMessage::getMidiNoteInHertz(note);
        rampFrom(current, milliseconds);
    }

    void glideFrom(int note, double milliseconds)
    {
        current = juce::MidiMessage::getMidiNoteInHertz(note);
        rampFrom(current, milliseconds);
    }

    int sounding = -1;

    void startNote(int note, float velocity, juce::SynthesiserSound*, int pitchWheel) override
    {
        sounding = note;
        ramp = 0;
        owner.lastStarted = this;
        frequency = juce::MidiMessage::getMidiNoteInHertz(note);
        current = settings.glide > 0.0f && owner.lastFrequency > 0.0 ? owner.lastFrequency : frequency;
        owner.lastFrequency = frequency;
        vibratoPhase = 0.0;
        gain = velocity;
        pitchWheelMoved(pitchWheel);
        for (auto& oscillatorPhases : phases)
            for (size_t u = 0; u < oscillatorPhases.size(); ++u)
                oscillatorPhases[u] = u == 0 ? 0.0 : random.nextDouble();
        lows.fill(0.0f);
        bands.fill(0.0f);
        envelope.setSampleRate(getSampleRate());
        envelope.setParameters(settings.envelope);
        envelope.reset();
        envelope.noteOn();
    }

    void stopNote(float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            envelope.noteOff();
            return;
        }
        envelope.reset();
        clearCurrentNote();
    }

    void pitchWheelMoved(int value) override
    {
        bend = std::pow(2.0, (value - 8192) / 8192.0 * 2.0 / 12.0);
    }

    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (!isVoiceActive())
            return;

        const auto rate = getSampleRate();
        auto* outLeft = output.getWritePointer(0, startSample);
        auto* outRight = output.getWritePointer(juce::jmin(1, output.getNumChannels() - 1), startSample);
        const auto glideCoefficient = settings.glide > 0.0f ? std::exp(-1.0 / (settings.glide * 0.001 * rate)) : 0.0;
        const auto vibratoStep = settings.vibratoRate / rate;
        const auto vibratoAmount = settings.vibratoDepth / 1200.0;
        const auto voices = juce::jlimit(1, maxUnison, settings.unison);
        const auto normalise = 1.0f / std::sqrt(float(voices));

        auto g = 0.0f, k = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            if (ramp > 0)
            {
                current *= rampStep;
                if (--ramp == 0)
                    current = frequency;
            }
            else
                current = frequency + (current - frequency) * glideCoefficient;
            vibratoPhase += vibratoStep;
            if (vibratoPhase >= 1.0)
                vibratoPhase -= 1.0;
            const auto pitch = current * bend * (vibratoAmount > 0.0 ? std::exp2(vibratoAmount * std::sin(juce::MathConstants<double>::twoPi * vibratoPhase)) : 1.0);

            const auto level = envelope.getNextSample();
            if (i % controlRate == 0)
            {
                const auto modulated = settings.cutoff * std::exp2(settings.filterEnvelope * level * 5.0f);
                g = std::tan(juce::MathConstants<float>::pi * juce::jmin(modulated, float(rate) * 0.45f) / float(rate));
                k = 2.0f - 1.96f * settings.resonance;
            }

            auto mixedLeft = 0.0f, mixedRight = 0.0f;
            for (size_t o = 0; o < size_t(oscillatorCount); ++o)
            {
                if (settings.level[o] <= 0.0f)
                    continue;
                for (size_t u = 0; u < size_t(voices); ++u)
                {
                    const auto increment = juce::jmin(0.45, pitch * settings.ratio[o] * settings.detune[u] / rate);
                    const auto value = float(oscillator(o, u, increment)) * settings.level[o];
                    mixedLeft += value * settings.left[u];
                    mixedRight += value * settings.right[u];
                    auto& phase = phases[o][u];
                    phase += increment;
                    if (phase >= 1.0)
                        phase -= 1.0;
                }
            }

            const auto amplitude = level * gain * settings.volume * normalise;
            outLeft[i] += filter(0, mixedLeft, g, k) * amplitude;
            const auto right = filter(1, mixedRight, g, k) * amplitude;
            if (outRight != outLeft)
                outRight[i] += right;

            if (!envelope.isActive())
            {
                clearCurrentNote();
                return;
            }
        }
    }

private:
    float filter(size_t channel, float input, float g, float k) noexcept
    {
        auto& low = lows[channel];
        auto& band = bands[channel];
        const auto high = input - k * band - low;
        band = juce::jlimit(-8.0f, 8.0f, band + g * high);
        low = juce::jlimit(-8.0f, 8.0f, low + g * band);
        return low;
    }

    double oscillator(size_t index, size_t unisonVoice, double dt) noexcept
    {
        const auto t = phases[index][unisonVoice];
        switch (settings.shape[index])
        {
            case 0: return std::sin(juce::MathConstants<double>::twoPi * t);
            case 1: return 4.0 * std::abs(t - 0.5) - 1.0;
            case 2: return 2.0 * t - 1.0 - polyBlep(t, dt);
            case 3: return (t < 0.5 ? 1.0 : -1.0) + polyBlep(t, dt) - polyBlep(std::fmod(t + 0.5, 1.0), dt);
            default: return random.nextDouble() * 2.0 - 1.0;
        }
    }

    void rampFrom(double start, double milliseconds)
    {
        const auto samples = juce::jmax(1, int(milliseconds * 0.001 * getSampleRate()));
        current = start;
        ramp = samples;
        rampStep = std::pow(frequency / juce::jmax(1.0, start), 1.0 / samples);
    }

    Synth& owner;
    const Settings& settings;
    juce::ADSR envelope;
    int ramp = 0;
    double rampStep = 1.0;
    juce::Random random;
    std::array<std::array<double, maxUnison>, oscillatorCount> phases {};
    double frequency = 440.0;
    double current = 440.0;
    double vibratoPhase = 0.0;
    double bend = 1.0;
    float gain = 1.0f;
    std::array<float, 2> lows {}, bands {};
};

Synth::Synth() : BuiltinProcessor(typeName, true)
{
    const std::array<int, oscillatorCount> defaultShapes { 2, 2, 0 };
    const std::array<float, oscillatorCount> defaultCoarse { 0.0f, 0.0f, -12.0f };
    const std::array<float, oscillatorCount> defaultFine { 0.0f, 8.0f, 0.0f };
    const std::array<float, oscillatorCount> defaultLevel { 0.7f, 0.5f, 0.4f };

    for (int i = 0; i < oscillatorCount; ++i)
    {
        const auto prefix = "osc" + juce::String(i + 1);
        const auto label = "Osc " + juce::String(i + 1) + " ";
        auto& osc = oscillators[size_t(i)];
        osc.shape = &addChoice(prefix + "Shape", label + "shape", shapes, defaultShapes[size_t(i)]);
        osc.coarse = &addFloat(prefix + "Coarse", label + "coarse", { -36.0f, 36.0f, 1.0f }, defaultCoarse[size_t(i)], "st");
        osc.fine = &addFloat(prefix + "Fine", label + "fine", { -100.0f, 100.0f }, defaultFine[size_t(i)], "ct");
        osc.level = &addFloat(prefix + "Level", label + "level", { 0.0f, 1.0f }, defaultLevel[size_t(i)]);
    }

    attack = &addFloat("attack", "Attack", skewed(0.0f, 5000.0f, 300.0f), 2.0f, "ms");
    decay = &addFloat("decay", "Decay", skewed(1.0f, 5000.0f, 400.0f), 300.0f, "ms");
    sustain = &addFloat("sustain", "Sustain", { 0.0f, 1.0f }, 0.7f);
    release = &addFloat("release", "Release", skewed(1.0f, 8000.0f, 400.0f), 180.0f, "ms");
    cutoff = &addFloat("cutoff", "Cutoff", skewed(20.0f, 20000.0f, 1500.0f), 6000.0f, "Hz");
    resonance = &addFloat("resonance", "Resonance", { 0.0f, 1.0f }, 0.15f);
    filterEnvelope = &addFloat("filterEnvelope", "Filter envelope", { 0.0f, 1.0f }, 0.0f);
    volume = &addFloat("volume", "Volume", { 0.0f, 1.0f }, 0.45f);
    glide = &addFloat("glide", "Glide", skewed(0.0f, 1000.0f, 150.0f), 0.0f, "ms");
    vibratoRate = &addFloat("vibratoRate", "Vibrato rate", skewed(0.1f, 12.0f, 5.0f), 5.0f, "Hz");
    vibratoDepth = &addFloat("vibratoDepth", "Vibrato depth", { 0.0f, 100.0f }, 0.0f, "ct");
    unison = &addInt("unison", "Unison", 1, maxUnison, 1);
    unisonDetune = &addFloat("detune", "Unison detune", { 0.0f, 100.0f }, 18.0f, "ct");
    unisonSpread = &addFloat("spread", "Stereo spread", { 0.0f, 1.0f }, 0.6f);

    synth.addSound(new AnySound());
    for (int i = 0; i < voiceCount; ++i)
        synth.addVoice(new Voice(*this));

    addPreset("Supersaw lead", { { "osc1Shape", 2 }, { "osc1Level", 0.7f }, { "osc2Shape", 2 }, { "osc2Coarse", 12 }, { "osc2Level", 0.35f }, { "osc3Shape", 3 },
                                 { "osc3Coarse", -12 }, { "osc3Level", 0.25f }, { "unison", 7 }, { "detune", 32 }, { "spread", 0.95f }, { "attack", 3 },
                                 { "decay", 400 }, { "sustain", 0.8f }, { "release", 320 }, { "cutoff", 9000 }, { "resonance", 0.1f }, { "volume", 0.38f } });
    addPreset("Pluck", { { "osc1Shape", 2 }, { "osc1Level", 0.6f }, { "osc2Shape", 3 }, { "osc2Coarse", 12 }, { "osc2Fine", 4 }, { "osc2Level", 0.25f },
                         { "osc3Shape", 0 }, { "osc3Coarse", 24 }, { "osc3Level", 0.15f }, { "attack", 1 }, { "decay", 260 }, { "sustain", 0.0f },
                         { "release", 260 }, { "cutoff", 2600 }, { "resonance", 0.25f }, { "filterEnvelope", 0.45f }, { "volume", 0.42f } });
    addPreset("Soft pad", { { "osc1Shape", 2 }, { "osc1Fine", -9 }, { "osc1Level", 0.45f }, { "osc2Shape", 2 }, { "osc2Fine", 9 }, { "osc2Level", 0.45f },
                            { "osc3Shape", 0 }, { "osc3Coarse", -12 }, { "osc3Level", 0.35f }, { "unison", 3 }, { "detune", 15 }, { "attack", 700 },
                            { "decay", 1200 }, { "sustain", 0.8f }, { "release", 1600 }, { "cutoff", 1500 }, { "resonance", 0.1f }, { "filterEnvelope", 0.12f },
                            { "volume", 0.3f } });
    addPreset("Bell", { { "osc1Shape", 0 }, { "osc1Level", 0.7f }, { "osc2Shape", 1 }, { "osc2Coarse", 12 }, { "osc2Level", 0.3f }, { "osc3Shape", 0 },
                        { "osc3Coarse", 24 }, { "osc3Fine", 3 }, { "osc3Level", 0.18f }, { "attack", 2 }, { "decay", 700 }, { "sustain", 0.15f },
                        { "release", 600 }, { "cutoff", 9000 }, { "volume", 0.42f } });
    addPreset("Sub bass", { { "osc1Shape", 0 }, { "osc1Level", 1.0f }, { "osc2Level", 0.0f }, { "osc3Level", 0.0f }, { "attack", 2 }, { "decay", 300 },
                            { "sustain", 1.0f }, { "release", 80 }, { "cutoff", 400 }, { "volume", 0.6f } });
    addPreset("Reese bass", { { "osc1Shape", 2 }, { "osc1Level", 0.7f }, { "osc2Shape", 2 }, { "osc2Fine", 22 }, { "osc2Level", 0.7f }, { "osc3Shape", 0 },
                              { "osc3Coarse", -12 }, { "osc3Level", 0.5f }, { "unison", 2 }, { "detune", 25 }, { "attack", 4 }, { "sustain", 1.0f },
                              { "release", 120 }, { "cutoff", 900 }, { "resonance", 0.2f }, { "volume", 0.45f } });
    addPreset("Chip lead", { { "osc1Shape", 3 }, { "osc1Level", 0.6f }, { "osc2Shape", 3 }, { "osc2Coarse", 12 }, { "osc2Fine", 7 }, { "osc2Level", 0.22f },
                             { "osc3Level", 0.0f }, { "attack", 1 }, { "decay", 160 }, { "sustain", 0.7f }, { "release", 70 }, { "cutoff", 12000 },
                             { "glide", 18 }, { "vibratoRate", 6.5f }, { "vibratoDepth", 16 }, { "volume", 0.2f } });
    addPreset("G-funk whistle", { { "osc1Shape", 0 }, { "osc1Level", 0.75f }, { "osc2Shape", 1 }, { "osc2Coarse", 12 }, { "osc2Level", 0.2f }, { "osc3Shape", 2 },
                                  { "osc3Level", 0.08f }, { "attack", 25 }, { "decay", 600 }, { "sustain", 0.85f }, { "release", 260 }, { "cutoff", 6000 },
                                  { "glide", 110 }, { "vibratoRate", 5.5f }, { "vibratoDepth", 28 }, { "volume", 0.34f } });
    addPreset("Brass stab", { { "osc1Shape", 2 }, { "osc1Fine", -8 }, { "osc1Level", 0.5f }, { "osc2Shape", 2 }, { "osc2Fine", 8 }, { "osc2Level", 0.5f },
                              { "osc3Shape", 3 }, { "osc3Coarse", -12 }, { "osc3Level", 0.2f }, { "attack", 6 }, { "decay", 220 }, { "sustain", 0.25f },
                              { "release", 140 }, { "cutoff", 2200 }, { "resonance", 0.15f }, { "filterEnvelope", 0.5f }, { "volume", 0.36f } });
    addPreset("Trance pluck", { { "osc1Shape", 2 }, { "osc1Level", 0.7f }, { "osc2Shape", 2 }, { "osc2Coarse", 12 }, { "osc2Level", 0.3f }, { "osc3Level", 0.0f },
                                { "unison", 5 }, { "detune", 22 }, { "spread", 0.9f }, { "attack", 1 }, { "decay", 220 }, { "sustain", 0.05f },
                                { "release", 200 }, { "cutoff", 1800 }, { "resonance", 0.3f }, { "filterEnvelope", 0.6f }, { "volume", 0.36f } });
    addPreset("Rage lead", { { "osc1Shape", 3 }, { "osc1Level", 0.6f }, { "osc2Shape", 2 }, { "osc2Fine", 12 }, { "osc2Level", 0.5f }, { "osc3Shape", 2 },
                             { "osc3Coarse", 12 }, { "osc3Level", 0.3f }, { "unison", 7 }, { "detune", 35 }, { "spread", 1.0f }, { "attack", 2 }, { "decay", 400 },
                             { "sustain", 0.8f }, { "release", 160 }, { "cutoff", 7000 }, { "resonance", 0.2f }, { "glide", 40 }, { "volume", 0.32f } });
    addPreset("Plugg bell", { { "osc1Shape", 0 }, { "osc1Level", 0.8f }, { "osc2Shape", 0 }, { "osc2Coarse", 24 }, { "osc2Fine", 3 }, { "osc2Level", 0.28f },
                              { "osc3Shape", 1 }, { "osc3Coarse", 12 }, { "osc3Level", 0.2f }, { "attack", 1 }, { "decay", 900 }, { "sustain", 0.0f },
                              { "release", 900 }, { "cutoff", 9000 }, { "resonance", 0.05f }, { "vibratoDepth", 6 }, { "volume", 0.42f } });
    addPreset("Dark pad", { { "osc1Shape", 2 }, { "osc1Fine", -12 }, { "osc1Level", 0.5f }, { "osc2Shape", 1 }, { "osc2Fine", 12 }, { "osc2Level", 0.5f },
                            { "osc3Shape", 4 }, { "osc3Level", 0.04f }, { "unison", 4 }, { "detune", 20 }, { "spread", 1.0f }, { "attack", 1400 },
                            { "decay", 1500 }, { "sustain", 0.8f }, { "release", 2600 }, { "cutoff", 900 }, { "resonance", 0.25f }, { "volume", 0.36f } });
    addPreset("Hoover", { { "osc1Shape", 2 }, { "osc1Level", 0.6f }, { "osc2Shape", 2 }, { "osc2Coarse", -12 }, { "osc2Fine", 30 }, { "osc2Level", 0.6f },
                          { "osc3Shape", 3 }, { "osc3Coarse", -12 }, { "osc3Level", 0.3f }, { "unison", 7 }, { "detune", 60 }, { "spread", 0.9f },
                          { "glide", 180 }, { "attack", 10 }, { "sustain", 1.0f }, { "release", 300 }, { "cutoff", 4500 }, { "volume", 0.3f } });
    addPreset("Wobble bass", { { "osc1Shape", 2 }, { "osc1Level", 0.7f }, { "osc2Shape", 3 }, { "osc2Coarse", -12 }, { "osc2Level", 0.6f }, { "osc3Shape", 0 },
                               { "osc3Coarse", -12 }, { "osc3Level", 0.5f }, { "attack", 2 }, { "decay", 600 }, { "sustain", 0.9f }, { "release", 120 },
                               { "cutoff", 450 }, { "resonance", 0.55f }, { "filterEnvelope", 0.35f }, { "volume", 0.38f } });
    addPreset("Acid 303", { { "osc1Shape", 2 }, { "osc1Level", 0.85f }, { "osc2Level", 0.0f }, { "osc3Level", 0.0f }, { "glide", 70 }, { "attack", 1 },
                            { "decay", 260 }, { "sustain", 0.2f }, { "release", 60 }, { "cutoff", 380 }, { "resonance", 0.82f }, { "filterEnvelope", 0.75f },
                            { "volume", 0.4f } });
    addPreset("Lofi keys", { { "osc1Shape", 1 }, { "osc1Level", 0.7f }, { "osc2Shape", 0 }, { "osc2Coarse", 12 }, { "osc2Fine", -6 }, { "osc2Level", 0.35f },
                             { "osc3Shape", 4 }, { "osc3Level", 0.02f }, { "attack", 4 }, { "decay", 1200 }, { "sustain", 0.25f }, { "release", 500 },
                             { "cutoff", 2400 }, { "vibratoRate", 0.6f }, { "vibratoDepth", 9 }, { "volume", 0.42f } });
    addPreset("Future bass chord", { { "osc1Shape", 2 }, { "osc1Level", 0.6f }, { "osc2Shape", 2 }, { "osc2Coarse", 12 }, { "osc2Level", 0.3f }, { "osc3Shape", 3 },
                                     { "osc3Level", 0.25f }, { "unison", 7 }, { "detune", 28 }, { "spread", 1.0f }, { "attack", 30 }, { "decay", 700 },
                                     { "sustain", 0.6f }, { "release", 450 }, { "cutoff", 5200 }, { "filterEnvelope", 0.2f }, { "volume", 0.3f } });
    addPreset("Detroit stab", { { "osc1Shape", 3 }, { "osc1Level", 0.6f }, { "osc2Shape", 2 }, { "osc2Coarse", 7 }, { "osc2Level", 0.45f }, { "osc3Shape", 2 },
                                { "osc3Coarse", 12 }, { "osc3Level", 0.3f }, { "attack", 1 }, { "decay", 180 }, { "sustain", 0.0f }, { "release", 160 },
                                { "cutoff", 1500 }, { "resonance", 0.45f }, { "filterEnvelope", 0.55f }, { "volume", 0.38f } });
    addPreset("Glide 808 lead", { { "osc1Shape", 0 }, { "osc1Level", 0.8f }, { "osc2Shape", 1 }, { "osc2Coarse", 12 }, { "osc2Level", 0.35f }, { "osc3Level", 0.0f },
                                  { "glide", 120 }, { "attack", 2 }, { "decay", 800 }, { "sustain", 0.7f }, { "release", 220 }, { "cutoff", 3200 },
                                  { "vibratoRate", 5.5f }, { "vibratoDepth", 14 }, { "volume", 0.42f } });
}

std::unique_ptr<juce::Component> Synth::createDisplay()
{
    auto adsr = std::make_unique<display::Curve>([this](float t)
    {
        const auto a = attack->get() * 0.001f;
        const auto d = decay->get() * 0.001f;
        const auto s = sustain->get();
        const auto r = release->get() * 0.001f;
        const auto hold = juce::jmax(0.4f, a + d);
        if (t < a)
            return t / juce::jmax(0.0001f, a);
        if (t < a + d)
            return 1.0f - (1.0f - s) * (t - a) / juce::jmax(0.0001f, d);
        if (t < hold + 0.6f)
            return s;
        return juce::jmax(0.0f, s * (1.0f - (t - hold - 0.6f) / juce::jmax(0.0001f, r)));
    }, juce::Range<float>(0.0f, 4.0f), juce::Range<float>(0.0f, 1.05f), nullptr, "4 s", "AMP ENVELOPE");
    return display::row(std::move(adsr), 1.0f, std::make_unique<display::Scope>(*this), 1.3f);
}

Synth::~Synth() = default;

void Synth::prepareToPlay(double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void Synth::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    for (size_t i = 0; i < oscillators.size(); ++i)
    {
        const auto& osc = oscillators[i];
        settings.shape[i] = osc.shape->getIndex();
        settings.ratio[i] = std::exp2((osc.coarse->get() + osc.fine->get() * 0.01) / 12.0);
        settings.level[i] = osc.level->get();
    }
    settings.envelope = { attack->get() * 0.001f, decay->get() * 0.001f, sustain->get(), release->get() * 0.001f };
    settings.cutoff = cutoff->get();
    settings.resonance = resonance->get();
    settings.filterEnvelope = filterEnvelope->get();
    settings.volume = volume->get();
    settings.glide = glide->get();
    settings.vibratoRate = vibratoRate->get();
    settings.vibratoDepth = vibratoDepth->get();
    settings.unison = unison->get();
    for (int u = 0; u < maxUnison; ++u)
    {
        const auto position = settings.unison > 1 ? double(u) / double(settings.unison - 1) * 2.0 - 1.0 : 0.0;
        settings.detune[size_t(u)] = std::exp2(position * unisonDetune->get() / 1200.0);
        const auto pan = float(position) * unisonSpread->get();
        settings.left[size_t(u)] = juce::jmin(1.0f, 1.0f - pan);
        settings.right[size_t(u)] = juce::jmin(1.0f, 1.0f + pan);
    }
    chunk.clear();
    auto position = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isController() && message.getControllerNumber() == 5)
        {
            slideMilliseconds = message.getControllerValue() * 10.0;
            continue;
        }
        if (message.isController() && message.getControllerNumber() == 84)
        {
            slideSource = message.getControllerValue();
            continue;
        }
        if (!message.isNoteOn() || slideSource < 0)
        {
            chunk.addEvent(message, metadata.samplePosition);
            continue;
        }
        if (metadata.samplePosition > position)
            synth.renderNextBlock(buffer, chunk, position, metadata.samplePosition - position);
        chunk.clear();
        position = metadata.samplePosition;
        Voice* sliding = nullptr;
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* voice = static_cast<Voice*>(synth.getVoice(i)); voice->isVoiceActive() && voice->isKeyDown() && voice->sounding == slideSource)
                sliding = voice;
        if (sliding != nullptr)
            sliding->slideTo(message.getNoteNumber(), slideMilliseconds);
        else
        {
            lastStarted = nullptr;
            synth.noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
            if (lastStarted != nullptr)
                lastStarted->glideFrom(slideSource, slideMilliseconds);
        }
        slideSource = -1;
    }
    if (buffer.getNumSamples() > position)
        synth.renderNextBlock(buffer, chunk, position, buffer.getNumSamples() - position);
}
}
