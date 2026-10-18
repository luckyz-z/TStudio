#include "Engine/Builtins/Pack.h"

#include <array>
#include <cmath>
#include <map>
#include <mutex>

namespace ts::Pack
{
namespace
{
constexpr double rate = 48000.0;
constexpr float twoPi = juce::MathConstants<float>::twoPi;
constexpr std::array<float, 6> hatPartials { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
constexpr std::array<float, 6> cymbalPartials { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f };

juce::AudioBuffer<float> makeBuffer(double seconds)
{
    juce::AudioBuffer<float> buffer(1, juce::roundToInt(rate * seconds));
    buffer.clear();
    return buffer;
}

juce::AudioBuffer<float> finish(juce::AudioBuffer<float> buffer)
{
    const auto length = buffer.getNumSamples();
    if (const auto peak = buffer.getMagnitude(0, length); peak > 0.0f)
        buffer.applyGain(0.9f / peak);
    const auto fade = juce::jmin(64, length);
    buffer.applyGainRamp(0, length - fade, fade, 1.0f, 0.0f);
    return buffer;
}

float noise(juce::Random& random)
{
    return random.nextFloat() * 2.0f - 1.0f;
}

float time(int index)
{
    return float(index / rate);
}

template <typename Fill>
juce::AudioBuffer<float> generate(double seconds, Fill&& fill)
{
    auto buffer = makeBuffer(seconds);
    auto* out = buffer.getWritePointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        out[i] = fill(time(i));
    return finish(std::move(buffer));
}

float advance(float& phase, float frequency)
{
    phase = std::fmod(phase + twoPi * frequency / float(rate), twoPi);
    return std::sin(phase);
}

template <size_t Count>
float metal(std::array<float, Count>& phases, const std::array<float, Count>& partials, float scale)
{
    auto sum = 0.0f;
    for (size_t k = 0; k < Count; ++k)
    {
        phases[k] = std::fmod(phases[k] + partials[k] * scale / float(rate), 1.0f);
        sum += phases[k] < 0.5f ? 1.0f : -1.0f;
    }
    return sum / float(Count);
}

juce::AudioBuffer<float> kick(float startHz, float endHz, float sweep, float decay, float drive, double seconds, float clickAmount, int seed)
{
    juce::Random random(seed);
    juce::IIRFilter clickFilter;
    clickFilter.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 4200.0, 0.7));
    auto phase = 0.0f;
    return generate(seconds, [&](float t)
    {
        const auto frequency = endHz + (startHz - endHz) * std::exp(-t * sweep);
        const auto body = advance(phase, frequency) * std::exp(-t * decay);
        const auto click = clickFilter.processSingleSampleRaw(noise(random)) * std::exp(-t * 700.0f) * clickAmount;
        return std::tanh((body + click) * drive);
    });
}

juce::AudioBuffer<float> clap(float tail, int seed)
{
    juce::Random random(seed);
    juce::IIRFilter band, high;
    band.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 1250.0, 1.1));
    high.setCoefficients(juce::IIRCoefficients::makeHighPass(rate, 650.0));
    constexpr std::array<float, 4> bursts { 0.0f, 0.0095f, 0.0185f, 0.029f };
    return generate(0.2 + 2.5 / tail, [&](float t)
    {
        auto envelope = std::exp(-juce::jmax(0.0f, t - bursts.back()) * tail) * (t >= bursts.back() ? 1.0f : 0.0f);
        for (const auto start : bursts)
            if (t >= start && t < start + 0.012f)
                envelope = juce::jmax(envelope, std::exp(-(t - start) * 320.0f));
        const auto x = high.processSingleSampleRaw(band.processSingleSampleRaw(noise(random)));
        return std::tanh(x * envelope * 2.2f);
    });
}

juce::AudioBuffer<float> hat(double seconds, float decay, int seed, float brightness = 1.0f)
{
    juce::Random random(seed);
    juce::IIRFilter band, first, second;
    band.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 9500.0 * brightness, 0.9));
    first.setCoefficients(juce::IIRCoefficients::makeHighPass(rate, 6800.0));
    second.setCoefficients(juce::IIRCoefficients::makeHighPass(rate, 6800.0));
    std::array<float, hatPartials.size()> phases {};
    return generate(seconds, [&](float t)
    {
        const auto x = metal(phases, hatPartials, 1.7f * brightness) * 0.8f + noise(random) * 0.55f;
        const auto shaped = second.processSingleSampleRaw(first.processSingleSampleRaw(band.processSingleSampleRaw(x)));
        return shaped * std::exp(-t * decay) * (1.0f - std::exp(-t * 6000.0f));
    });
}

juce::AudioBuffer<float> snare(float tone, float bodyDecay, float noiseDecay, double brightness, int seed)
{
    juce::Random random(seed);
    juce::IIRFilter high, band;
    high.setCoefficients(juce::IIRCoefficients::makeHighPass(rate, brightness));
    band.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 5200.0, 0.6));
    auto first = 0.0f, second = 0.0f;
    return generate(0.42, [&](float t)
    {
        const auto sweep = 1.0f + 0.55f * std::exp(-t * 55.0f);
        const auto body = (advance(first, tone * sweep) + advance(second, tone * 1.74f * sweep) * 0.45f) * std::exp(-t * bodyDecay) * 0.75f;
        const auto n = noise(random);
        const auto hiss = (high.processSingleSampleRaw(n) * 0.75f + band.processSingleSampleRaw(n) * 0.5f) * std::exp(-t * noiseDecay);
        const auto crack = n * std::exp(-t * 900.0f) * 0.5f;
        return std::tanh((body + hiss + crack) * 1.6f);
    });
}

juce::AudioBuffer<float> perc()
{
    juce::Random random(21);
    auto phase = 0.0f;
    return generate(0.22, [&](float t)
    {
        const auto tone = advance(phase, 620.0f * (1.0f + 0.8f * std::exp(-t * 90.0f))) * std::exp(-t * 26.0f);
        return std::tanh((tone + noise(random) * std::exp(-t * 300.0f) * 0.3f) * 1.4f);
    });
}

juce::AudioBuffer<float> rim()
{
    juce::Random random(4);
    juce::IIRFilter band;
    band.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 3200.0, 2.0));
    auto a = 0.0f, b = 0.0f;
    return generate(0.08, [&](float t)
    {
        const auto tone = (advance(a, 1700.0f) + advance(b, 820.0f) * 0.6f) * std::exp(-t * 90.0f);
        return tone + band.processSingleSampleRaw(noise(random)) * std::exp(-t * 200.0f);
    });
}

juce::AudioBuffer<float> tom()
{
    juce::Random random(5);
    auto phase = 0.0f;
    return generate(0.45, [&](float t)
    {
        const auto body = advance(phase, 135.0f + 90.0f * std::exp(-t * 14.0f)) * std::exp(-t * 7.5f);
        return std::tanh(body * 1.4f + noise(random) * std::exp(-t * 60.0f) * 0.15f);
    });
}

juce::AudioBuffer<float> crash()
{
    juce::Random random(6);
    juce::IIRFilter first, second;
    first.setCoefficients(juce::IIRCoefficients::makeHighPass(rate, 4800.0));
    second.setCoefficients(juce::IIRCoefficients::makeHighPass(rate, 3000.0));
    std::array<float, cymbalPartials.size()> phases {};
    return generate(1.8, [&](float t)
    {
        const auto x = metal(phases, cymbalPartials, 2.1f) * 0.6f + noise(random) * 0.8f;
        const auto envelope = std::exp(-t * 2.4f) * (1.0f - std::exp(-t * 900.0f));
        return second.processSingleSampleRaw(first.processSingleSampleRaw(x)) * envelope;
    });
}

juce::AudioBuffer<float> bass808()
{
    auto phase = 0.0f;
    return generate(1.6, [&](float t)
    {
        const auto body = advance(phase, 49.0f + 30.0f * std::exp(-t * 24.0f)) * std::exp(-t * 1.9f);
        return std::tanh(body * 1.8f);
    });
}

juce::AudioBuffer<float> shaker()
{
    juce::Random random(7);
    juce::IIRFilter band;
    band.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 6500.0, 1.4));
    return generate(0.16, [&](float t)
    {
        const auto envelope = (1.0f - std::exp(-t * 120.0f)) * std::exp(-t * 28.0f);
        return band.processSingleSampleRaw(noise(random)) * envelope;
    });
}

juce::AudioBuffer<float> cowbell()
{
    juce::IIRFilter band;
    band.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 2600.0, 0.9));
    auto a = 0.0f, b = 0.0f;
    return generate(0.35, [&](float t)
    {
        a = std::fmod(a + 540.0f / float(rate), 1.0f);
        b = std::fmod(b + 800.0f / float(rate), 1.0f);
        const auto x = (a < 0.5f ? 1.0f : -1.0f) + (b < 0.5f ? 1.0f : -1.0f);
        return band.processSingleSampleRaw(x) * (std::exp(-t * 30.0f) * 0.6f + std::exp(-t * 7.0f) * 0.4f);
    });
}

juce::AudioBuffer<float> clave()
{
    auto phase = 0.0f;
    return generate(0.1, [&](float t) { return advance(phase, 2480.0f) * std::exp(-t * 55.0f); });
}

juce::AudioBuffer<float> snap()
{
    juce::Random random(8);
    juce::IIRFilter band;
    band.setCoefficients(juce::IIRCoefficients::makeBandPass(rate, 2100.0, 3.0));
    return generate(0.18, [&](float t) { return band.processSingleSampleRaw(noise(random)) * std::exp(-t * 34.0f) * (1.0f - std::exp(-t * 3000.0f)); });
}

using Generator = juce::AudioBuffer<float> (*)();

const std::vector<std::pair<juce::String, Generator>>& table()
{
    static const std::vector<std::pair<juce::String, Generator>> sounds {
        { "Kick", [] { return kick(185.0f, 48.0f, 34.0f, 5.2f, 2.2f, 0.55, 0.55f, 11); } },
        { "Kick Hard", [] { return kick(260.0f, 52.0f, 24.0f, 3.6f, 4.2f, 0.6, 0.95f, 12); } },
        { "Kick Boom", [] { return kick(150.0f, 42.0f, 20.0f, 2.2f, 2.8f, 1.1, 0.35f, 13); } },
        { "Clap", [] { return clap(16.0f, 1); } },
        { "Clap Big", [] { return clap(7.5f, 4); } },
        { "Snare", [] { return snare(190.0f, 26.0f, 15.0f, 1800.0, 3); } },
        { "Snare Trap", [] { return snare(235.0f, 34.0f, 22.0f, 2600.0, 5); } },
        { "Hat", [] { return hat(0.11, 42.0f, 2); } },
        { "Hat Tight", [] { return hat(0.06, 85.0f, 6, 1.15f); } },
        { "Open Hat", [] { return hat(0.55, 6.0f, 9); } },
        { "Perc", perc },
        { "Rim", rim },
        { "Tom", tom },
        { "Crash", crash },
        { "808", bass808 },
        { "Shaker", shaker },
        { "Cowbell", cowbell },
        { "Clave", clave },
        { "Snap", snap },
    };
    return sounds;
}
}

const juce::StringArray& names()
{
    static const auto list = []
    {
        juce::StringArray result;
        for (const auto& [soundName, make] : table())
            result.add(soundName);
        return result;
    }();
    return list;
}

std::shared_ptr<const SampleData> get(const juce::String& sound)
{
    static std::mutex mutex;
    static std::map<juce::String, std::shared_ptr<const SampleData>> cache;

    const std::scoped_lock guard(mutex);
    if (const auto found = cache.find(sound); found != cache.end())
        return found->second;

    for (const auto& [soundName, make] : table())
    {
        if (soundName != sound)
            continue;
        auto data = std::make_shared<SampleData>();
        data->audio = make();
        data->sampleRate = rate;
        data->name = soundName;
        data->pack = soundName;
        cache.emplace(sound, data);
        return data;
    }
    return nullptr;
}
}
