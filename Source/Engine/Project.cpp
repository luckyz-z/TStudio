#include "Engine/Project.h"

#include <algorithm>

namespace ts
{
namespace
{
constexpr size_t historyLimit = 100;

const std::array<juce::Colour, 8> palette {
    juce::Colour(0xff5f9e78), juce::Colour(0xff6a8fc2), juce::Colour(0xffc28b5a), juce::Colour(0xffa36cb8),
    juce::Colour(0xffc1606a), juce::Colour(0xff5aa5a8), juce::Colour(0xffb8a24f), juce::Colour(0xff7d86c9),
};
}

int Pattern::length() const noexcept
{
    auto last = 0;
    for (const auto& [id, list] : notes)
        for (const auto& note : list)
            last = std::max(last, note.end());
    return std::max(1, (last + ticksPerBar - 1) / ticksPerBar) * ticksPerBar;
}

bool Pattern::isEmpty() const noexcept
{
    return std::all_of(notes.begin(), notes.end(), [](const auto& entry) { return entry.second.empty(); });
}

float AutomationTarget::mixerGain(float normal) noexcept
{
    static const auto skew = std::log(0.8) / std::log(1.0 / mixerMaxGain);
    return float(mixerMaxGain * std::pow(double(juce::jlimit(0.0f, 1.0f, normal)), 1.0 / skew));
}

juce::String AutomationTarget::idOf(const juce::AudioProcessorParameter& parameter)
{
    if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(&parameter))
        return withId->getParameterID();
    if (const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*>(&parameter))
        return hosted->getParameterID();
    return juce::String(parameter.getParameterIndex());
}

float AutomationTarget::mixerNormal(float gain) noexcept
{
    static const auto skew = std::log(0.8) / std::log(1.0 / mixerMaxGain);
    return float(std::pow(juce::jlimit(0.0, 1.0, double(gain) / mixerMaxGain), skew));
}

float AutomationClip::valueAt(double tick) const noexcept
{
    if (points.empty())
        return 0.5f;
    if (tick <= points.front().tick)
        return points.front().value;
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& next = points[i];
        if (tick > next.tick)
            continue;
        const auto& previous = points[i - 1];
        const auto span = double(juce::jmax(1, next.tick - previous.tick));
        auto t = float((tick - previous.tick) / span);
        if (std::abs(next.tension) > 0.001f)
            t = next.tension > 0.0f ? std::pow(t, 1.0f + next.tension * 4.0f) : 1.0f - std::pow(1.0f - t, 1.0f - next.tension * 4.0f);
        return previous.value + (next.value - previous.value) * t;
    }
    return points.back().value;
}

void ChannelOptions::copyFrom(const ChannelOptions& other) noexcept
{
    timeShift.store(other.timeShift.load());
    swingMix.store(other.swingMix.load());
    cutGroup.store(other.cutGroup.load());
    cutBy.store(other.cutBy.load());
    mono.store(other.mono.load());
    porta.store(other.porta.load());
    portaTime.store(other.portaTime.load());
    arpDirection.store(other.arpDirection.load());
    arpTime.store(other.arpTime.load());
    arpRange.store(other.arpRange.load());
    arpGate.store(other.arpGate.load());
}

void ChannelOptions::save(juce::XmlElement& xml) const
{
    const auto value = toVar();
    if (auto* object = value.getDynamicObject())
        for (const auto& entry : object->getProperties())
            xml.setAttribute("option_" + entry.name.toString(), entry.value.toString());
}

void ChannelOptions::load(const juce::XmlElement& xml)
{
    auto* object = new juce::DynamicObject();
    for (int i = 0; i < xml.getNumAttributes(); ++i)
        if (xml.getAttributeName(i).startsWith("option_"))
        {
            const auto text = xml.getAttributeValue(i);
            object->setProperty(xml.getAttributeName(i).fromFirstOccurrenceOf("option_", false, false),
                                text == "true" || text == "false" ? juce::var(text == "true") : juce::var(text.getDoubleValue()));
        }
    apply(juce::var(object));
}

juce::var ChannelOptions::toVar() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("time_shift", timeShift.load());
    object->setProperty("swing_mix", swingMix.load());
    object->setProperty("cut_group", cutGroup.load());
    object->setProperty("cut_by", cutBy.load());
    object->setProperty("mono", mono.load());
    object->setProperty("porta", porta.load());
    object->setProperty("porta_time", portaTime.load());
    object->setProperty("arp", arpDirection.load());
    object->setProperty("arp_time", arpTime.load());
    object->setProperty("arp_range", arpRange.load());
    object->setProperty("arp_gate", arpGate.load());
    return juce::var(object);
}

void ChannelOptions::apply(const juce::var& value)
{
    const auto number = [&value](const char* key, double fallback) { return value.hasProperty(key) ? double(value[key]) : fallback; };
    timeShift.store(float(juce::jlimit(0.0, double(ticksPerStep), number("time_shift", timeShift.load()))));
    swingMix.store(float(juce::jlimit(0.0, 1.0, number("swing_mix", swingMix.load()))));
    cutGroup.store(juce::jlimit(0, 16, int(number("cut_group", cutGroup.load()))));
    cutBy.store(juce::jlimit(0, 16, int(number("cut_by", cutBy.load()))));
    if (value.hasProperty("mono"))
        mono.store(bool(value["mono"]));
    if (value.hasProperty("porta"))
        porta.store(bool(value["porta"]));
    portaTime.store(float(juce::jlimit(1.0, 2000.0, number("porta_time", portaTime.load()))));
    arpDirection.store(juce::jlimit(0, 4, int(number("arp", arpDirection.load()))));
    arpTime.store(juce::jlimit(0, int(arpTicks.size()) - 1, int(number("arp_time", arpTime.load()))));
    arpRange.store(juce::jlimit(1, 4, int(number("arp_range", arpRange.load()))));
    arpGate.store(float(juce::jlimit(0.05, 1.0, number("arp_gate", arpGate.load()))));
}

bool MixerTrack::hasEffects() const noexcept
{
    return std::any_of(slots.begin(), slots.end(), [](const auto& slot) { return slot != nullptr; });
}

void Device::prepare(double sampleRate, int blockSize)
{
    const auto channels = std::max({ 2, processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels() });
    processor->setRateAndBufferSizeDetails(sampleRate, blockSize);
    processor->prepareToPlay(sampleRate, blockSize);
    scratch.setSize(channels, blockSize, false, false, true);
    emptyMidi.ensureSize(2048);
}

void Device::process(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, int numSamples, const juce::AudioBuffer<float>* sidechain) noexcept
{
    if (bypassed.load(std::memory_order_relaxed))
        return;

    const auto channels = scratch.getNumChannels();
    const auto sidechainStart = processor->getBusCount(true) > 1 && processor->getBus(true, 1)->isEnabled() ? processor->getChannelCountOfBus(true, 0) : channels;
    for (int c = 0; c < channels; ++c)
    {
        if (c < 2)
            scratch.copyFrom(c, 0, audio, c, 0, numSamples);
        else if (sidechain != nullptr && c >= sidechainStart && c - sidechainStart < 2)
            scratch.copyFrom(c, 0, *sidechain, c - sidechainStart, 0, numSamples);
        else
            scratch.clear(c, 0, numSamples);
    }

    juce::AudioBuffer<float> block(scratch.getArrayOfWritePointers(), channels, numSamples);
    processor->processBlock(block, midi);

    const auto outputs = processor->getTotalNumOutputChannels();
    if (outputs <= 0)
        return;

    const auto wet = mix.load(std::memory_order_relaxed);
    for (int c = 0; c < 2; ++c)
    {
        audio.applyGain(c, 0, numSamples, 1.0f - wet);
        audio.addFrom(c, 0, block, std::min(c, outputs - 1), 0, numSamples, wet);
    }
}

void Device::process(juce::AudioBuffer<float>& audio, int numSamples, const juce::AudioBuffer<float>* sidechain) noexcept
{
    emptyMidi.clear();
    process(audio, emptyMidi, numSamples, sidechain);
}

Project::Project()
{
    for (auto& track : mixer)
        track = std::make_unique<MixerTrack>();
    for (int i = 0; i < mixerTrackCount; ++i)
    {
        mixer[size_t(i)]->name = defaultInsertName(i);
        if (i > 0)
            mixer[size_t(i)]->sends[0] = {};
    }
    updateMixerOrder();
    tracks.resize(playlistTrackCount);
    for (int i = 0; i < playlistTrackCount; ++i)
        tracks[size_t(i)].name = defaultTrackName(i);
    patterns.push_back({ "Pattern 1", palette[0], {} });
}

Project::~Project() = default;

Channel* Project::findChannel(int id) const noexcept
{
    for (const auto& channel : channels)
        if (channel->id == id)
            return channel.get();
    return nullptr;
}

int Project::indexOfChannel(int id) const noexcept
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i]->id == id)
            return int(i);
    return -1;
}

int Project::songLength() const noexcept
{
    auto last = 0;
    for (const auto& clip : clips)
        last = std::max(last, clip.end());
    return std::max(1, (last + ticksPerBar - 1) / ticksPerBar) * ticksPerBar;
}

int Project::nextFreeInsert() const noexcept
{
    for (int i = 1; i < mixerTrackCount; ++i)
    {
        const auto used = std::any_of(channels.begin(), channels.end(), [i](const auto& c) { return c->insert.load() == i; });
        if (!used && !mixer[size_t(i)]->hasEffects())
            return i;
    }
    return 0;
}

juce::Colour Project::nextChannelColour() const
{
    return palette[channels.size() % palette.size()];
}

juce::Colour Project::nextPatternColour() const
{
    return palette[patterns.size() % palette.size()];
}

void Project::swapContent(Project& other)
{
    {
        const juce::ScopedLock sl(lock);
        std::swap(channels, other.channels);
        std::swap(patterns, other.patterns);
        std::swap(clips, other.clips);
        std::swap(automations, other.automations);
        std::swap(tracks, other.tracks);
        std::swap(mixer, other.mixer);
        updateMixerOrder();
        other.updateMixerOrder();
        tempo.store(other.tempo.load());
        swing.store(other.swing.load());
        mainVolume.store(other.mainVolume.load());
        currentPattern.store(juce::jlimit(0, int(patterns.size()) - 1, other.currentPattern.load()));
        songMode.store(other.songMode.load());
        loopStart.store(other.loopStart.load());
        mainPitch.store(other.mainPitch.load());
        loopEnd.store(other.loopEnd.load());
    }
    std::swap(title, other.title);
    std::swap(file, other.file);
    nextChannelId = other.nextChannelId;
    selectedChannel = channels.empty() ? 0 : channels.front()->id;
    dirty = false;
    clearHistory();
    sendChangeMessage();
}

bool Project::canRoute(int from, int to) const
{
    if (from <= 0 || from >= mixerTrackCount || to < 0 || to >= mixerTrackCount || from == to)
        return false;
    if (to == 0)
        return true;
    std::vector<int> stack { to };
    std::set<int> seen;
    while (!stack.empty())
    {
        const auto current = stack.back();
        stack.pop_back();
        if (current == from)
            return false;
        if (current == 0 || !seen.insert(current).second)
            continue;
        for (const auto& [target, send] : mixer[size_t(current)]->sends)
            stack.push_back(target);
    }
    return true;
}

bool Project::route(int from, int to, float level, bool sidechain)
{
    const juce::ScopedLock sl(lock);
    if (!canRoute(from, to))
        return false;
    mixer[size_t(from)]->sends[to] = { juce::jlimit(0.0f, 2.0f, level), sidechain };
    updateMixerOrder();
    return true;
}

void Project::unroute(int from, int to)
{
    const juce::ScopedLock sl(lock);
    if (from > 0 && from < mixerTrackCount)
        mixer[size_t(from)]->sends.erase(to);
    updateMixerOrder();
}

void Project::routeOnly(int from, int to)
{
    const juce::ScopedLock sl(lock);
    if (!canRoute(from, to))
        return;
    auto& sends = mixer[size_t(from)]->sends;
    for (auto it = sends.begin(); it != sends.end();)
        it = it->second.sidechain ? std::next(it) : sends.erase(it);
    sends[to] = {};
    updateMixerOrder();
}

void Project::updateMixerOrder()
{
    std::array<int, mixerTrackCount> pending {};
    for (int i = 1; i < mixerTrackCount; ++i)
        for (const auto& [target, send] : mixer[size_t(i)]->sends)
            if (target > 0 && target < mixerTrackCount)
                ++pending[size_t(target)];
    std::vector<int> order, ready;
    for (int i = mixerTrackCount - 1; i >= 1; --i)
        if (pending[size_t(i)] == 0)
            ready.push_back(i);
    while (!ready.empty())
    {
        const auto current = ready.back();
        ready.pop_back();
        order.push_back(current);
        for (const auto& [target, send] : mixer[size_t(current)]->sends)
            if (target > 0 && target < mixerTrackCount && --pending[size_t(target)] == 0)
                ready.push_back(target);
    }
    for (int i = 1; i < mixerTrackCount; ++i)
        if (std::find(order.begin(), order.end(), i) == order.end())
            order.push_back(i);
    mixerOrder = std::move(order);
}

void Project::touch()
{
    dirty = true;
    sendChangeMessage();
}

void Project::checkpoint()
{
    undoStack.push_back(snapshot());
    if (undoStack.size() > historyLimit)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

bool Project::undo()
{
    if (undoStack.empty())
        return false;
    redoStack.push_back(snapshot());
    restore(std::move(undoStack.back()));
    undoStack.pop_back();
    return true;
}

bool Project::redo()
{
    if (redoStack.empty())
        return false;
    undoStack.push_back(snapshot());
    restore(std::move(redoStack.back()));
    redoStack.pop_back();
    return true;
}

void Project::clearHistory()
{
    undoStack.clear();
    redoStack.clear();
}

void Project::restore(Snapshot state)
{
    {
        const juce::ScopedLock sl(lock);
        patterns = std::move(state.patterns);
        clips = std::move(state.clips);
        automations = std::move(state.automations);
        currentPattern.store(juce::jlimit(0, int(patterns.size()) - 1, currentPattern.load()));
    }
    touch();
}
}
