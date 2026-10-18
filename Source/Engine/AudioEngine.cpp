#include "Engine/AudioEngine.h"
#include "Engine/SampleData.h"

#include <algorithm>
#include <cmath>

namespace ts
{
namespace
{
juce::AudioBuffer<float> makeClick(double sampleRate, float frequency)
{
    juce::AudioBuffer<float> buffer(1, juce::roundToInt(sampleRate * 0.03));
    auto* out = buffer.getWritePointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto t = float(i / sampleRate);
        out[i] = std::sin(juce::MathConstants<float>::twoPi * frequency * t) * std::exp(-t * 160.0f) * 0.5f;
    }
    return buffer;
}

void meter(std::atomic<float>& peak, float value) noexcept
{
    if (value > peak.load(std::memory_order_relaxed))
        peak.store(value, std::memory_order_relaxed);
}

std::pair<float, float> balance(float pan) noexcept
{
    return { std::min(1.0f, 1.0f - pan), std::min(1.0f, 1.0f + pan) };
}
}

AudioEngine::AudioEngine(Project& p) : project(p)
{
    liveMidi.ensureSize(4096);
    live.reset(sampleRate.load());
}

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double newSampleRate)
{
    auto high = makeClick(newSampleRate, 1760.0f);
    auto low = makeClick(newSampleRate, 1180.0f);
    const juce::ScopedLock sl(project.lock);
    sampleRate.store(newSampleRate);
    blockSize.store(std::max(16, samplesPerBlockExpected));
    live.reset(newSampleRate);
    std::swap(clickHigh, high);
    std::swap(clickLow, low);
    click = nullptr;
    prepare(project);
}

void AudioEngine::prepare(Device& device)
{
    device.prepare(sampleRate.load(), blockSize.load());
    device.processor->setPlayHead(this);
    device.processor->addListener(this);
}

void AudioEngine::audioProcessorParameterChangeGestureBegin(juce::AudioProcessor* processor, int index)
{
    const juce::SpinLock::ScopedLockType guard(tweakLock);
    tweakedProcessor = processor;
    tweakedParameter = index;
}

std::pair<juce::AudioProcessor*, int> AudioEngine::lastTweaked() const
{
    const juce::SpinLock::ScopedLockType guard(tweakLock);
    return { tweakedProcessor, tweakedParameter };
}

void AudioEngine::prepare(Channel& channel)
{
    channel.output.setSize(2, blockSize.load(), false, false, true);
    channel.midi.ensureSize(4096);
    if (channel.device != nullptr)
        prepare(*channel.device);
}

void AudioEngine::prepare(MixerTrack& track)
{
    track.buffer.setSize(2, blockSize.load(), false, false, true);
    track.sidechain.setSize(2, blockSize.load(), false, false, true);
    for (auto& slot : track.slots)
        if (slot != nullptr)
            prepare(*slot);
}

void AudioEngine::prepare(Project& target)
{
    for (auto& channel : target.channels)
        prepare(*channel);
    for (auto& track : target.mixer)
        prepare(*track);
}

void AudioEngine::setPlaying(bool shouldPlay) noexcept
{
    if (!shouldPlay && playing.load())
        stopRequested.store(true);
    playing.store(shouldPlay);
}

void AudioEngine::setNonRealtime(bool offline) noexcept
{
    const juce::ScopedLock sl(project.lock);
    for (auto& channel : project.channels)
        if (channel->device != nullptr)
            channel->device->processor->setNonRealtime(offline);
    for (auto& track : project.mixer)
        for (auto& slot : track->slots)
            if (slot != nullptr)
                slot->processor->setNonRealtime(offline);
}

void AudioEngine::preview(std::shared_ptr<const SampleData> sample)
{
    const juce::ScopedLock sl(project.lock);
    std::swap(previewSample, sample);
    previewPosition = previewSample != nullptr ? 0.0 : -1.0;
}

void AudioEngine::sendLive(const juce::MidiMessage& message)
{
    auto copy = message;
    copy.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    live.addMessageToQueue(copy);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> AudioEngine::getPosition() const
{
    return playInfo;
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    for (int done = 0; done < info.numSamples;)
    {
        const auto count = std::min(info.numSamples - done, blockSize.load());
        render(*info.buffer, info.startSample + done, count);
        done += count;
    }
    if (silent.load(std::memory_order_relaxed))
        info.clearActiveBufferRegion();
}

void AudioEngine::render(juce::AudioBuffer<float>& destination, int startSample, int numSamples) noexcept
{
    const juce::ScopedLock sl(project.lock);
    for (auto& channel : project.channels)
        channel->midi.clear();
    for (auto& track : project.mixer)
    {
        track->fed = false;
        track->sidechainFed = false;
    }

    applyRequests();
    if (const auto pitch = project.mainPitch.load(std::memory_order_relaxed); pitch != appliedPitch)
    {
        releaseAll(0);
        appliedPitch = pitch;
    }
    applyAutomation();

    const auto rate = sampleRate.load();
    const auto beats = position.load() / ppq;
    playInfo.setBpm(project.tempo.load());
    playInfo.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
    playInfo.setPpqPosition(beats);
    playInfo.setPpqPositionOfLastBarStart(std::floor(beats / 4.0) * 4.0);
    playInfo.setIsPlaying(playing.load());
    playInfo.setTimeInSamples(samplesPlayed);
    playInfo.setTimeInSeconds(double(samplesPlayed) / rate);

    const auto blockStart = position.load();
    if (playing.load())
    {
        sequence(numSamples);
        samplesPlayed += numSamples;
    }

    liveMidi.clear();
    live.removeNextBlockOfMessages(liveMidi, numSamples);
    if (!liveMidi.isEmpty())
    {
        if (auto* channel = project.findChannel(liveChannel.load()))
            channel->midi.addEvents(liveMidi, 0, numSamples, 0);
        capture(blockStart, numSamples);
    }

    renderChannels(numSamples);
    renderMixer(numSamples);
    renderExtras(numSamples);

    auto& master = project.mixer[0]->buffer;
    output.push(juce::AudioBuffer<float>(master.getArrayOfWritePointers(), 2, numSamples));
    for (int c = 0; c < destination.getNumChannels(); ++c)
        destination.copyFrom(c, startSample, master, std::min(c, 1), 0, numSamples);
}

void AudioEngine::capture(double blockStart, int numSamples) noexcept
{
    if (!recording.load(std::memory_order_relaxed) || !playing.load(std::memory_order_relaxed) || project.songMode.load(std::memory_order_relaxed))
        return;
    const auto samplesPerTick = sampleRate.load() * 60.0 / (project.tempo.load() * ppq);
    const auto loop = double(project.pattern().length());
    for (const auto metadata : liveMidi)
    {
        const auto message = metadata.getMessage();
        if (!message.isNoteOnOrOff() || metadata.samplePosition >= numSamples)
            continue;
        const auto scope = recordFifo.write(1);
        if (scope.blockSize1 + scope.blockSize2 == 0)
            return;
        auto& note = scope.blockSize1 > 0 ? recordBuffer[size_t(scope.startIndex1)] : recordBuffer[size_t(scope.startIndex2)];
        note = { message.getNoteNumber(), message.getFloatVelocity(), std::fmod(blockStart + metadata.samplePosition / samplesPerTick, loop), loop, message.isNoteOn() };
    }
}

int AudioEngine::readRecorded(RecordedNote* destination, int maximum)
{
    const auto scope = recordFifo.read(juce::jmin(maximum, recordFifo.getNumReady()));
    auto count = 0;
    for (int i = 0; i < scope.blockSize1; ++i)
        destination[count++] = recordBuffer[size_t(scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        destination[count++] = recordBuffer[size_t(scope.startIndex2 + i)];
    return count;
}

void AudioEngine::applyRequests() noexcept
{
    if (panicRequested.exchange(false))
    {
        releaseAll(0);
        for (auto& channel : project.channels)
        {
            channel->midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            channel->midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
        }
    }
    if (stopRequested.exchange(false))
    {
        releaseAll(0);
        for (auto& channel : project.channels)
            channel->midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        position.store(0.0);
        samplesPlayed = 0;
    }
    if (const auto requested = requestedPosition.exchange(-1.0); requested >= 0.0)
    {
        releaseAll(0);
        position.store(requested);
        samplesPlayed = juce::roundToInt(requested * 60.0 / (project.tempo.load() * ppq) * sampleRate.load());
    }
}

void AudioEngine::sequence(int numSamples) noexcept
{
    const auto samplesPerTick = sampleRate.load() * 60.0 / (project.tempo.load() * ppq);
    const auto region = project.songMode.load() && project.hasLoop();
    const auto begin = region ? double(project.loopStart.load()) : 0.0;
    const auto loop = region ? double(project.loopEnd.load()) : double(project.songMode.load() ? project.songLength() : project.pattern().length());
    auto tick = position.load();
    if (tick >= loop)
        tick = begin + std::fmod(tick - begin, loop - begin);
    auto remaining = numSamples / samplesPerTick;
    auto sample = 0.0;

    while (remaining > 1.0e-9)
    {
        const auto segment = std::min(remaining, loop - tick);
        emit(tick, tick + segment, sample, samplesPerTick, numSamples, false);
        emit(tick, tick + segment, sample, samplesPerTick, numSamples, true);

        if (metronome.load(std::memory_order_relaxed))
        {
            for (auto beat = std::ceil(tick / ppq); beat * ppq < tick + segment; beat += 1.0)
            {
                click = std::fmod(beat, 4.0) == 0.0 ? &clickHigh : &clickLow;
                clickPosition = 0;
                clickDelay = juce::jlimit(0, numSamples - 1, int(sample + (beat * ppq - tick) * samplesPerTick));
            }
        }

        tick += segment;
        sample += segment * samplesPerTick;
        remaining -= segment;
        if (tick >= loop - 1.0e-9)
        {
            releaseAll(juce::jlimit(0, numSamples - 1, int(sample)));
            tick = begin;
        }
    }
    position.store(tick);
}

const Note* AudioEngine::glideSource(const std::vector<Note>& notes, const Note& target) noexcept
{
    const Note* best = nullptr;
    for (const auto& other : notes)
    {
        if (&other == &target || other.muted || other.start >= target.start)
            continue;
        if (target.slide && other.end() <= target.start)
            continue;
        if (best == nullptr || other.start > best->start || (other.start == best->start && !other.slide))
            best = &other;
    }
    return best;
}

void AudioEngine::emit(double from, double to, double firstSample, double samplesPerTick, int numSamples, bool ons) noexcept
{
    if (!project.songMode.load())
    {
        const auto& pattern = project.pattern();
        emitPattern(pattern, from, to, double(pattern.length()), firstSample, samplesPerTick, numSamples, ons);
        return;
    }

    for (const auto& clip : project.clips)
    {
        if (clip.muted || clip.isAutomation() || clip.start >= to || clip.end() < from || clip.pattern >= int(project.patterns.size()))
            continue;
        if (project.tracks[size_t(clip.track)].muted)
            continue;
        const auto shift = double(clip.start - clip.offset);
        const auto begin = std::max(from, double(clip.start)) - shift;
        emitPattern(project.patterns[size_t(clip.pattern)], begin, to - shift, double(clip.offset + clip.length),
                    firstSample + (begin + shift - from) * samplesPerTick, samplesPerTick, numSamples, ons);
    }
}

void AudioEngine::cutChannels(int group, int sample) noexcept
{
    for (auto& other : project.channels)
    {
        if (other->options.cutBy.load(std::memory_order_relaxed) != group)
            continue;
        other->midi.addEvent(juce::MidiMessage::controllerEvent(1, 119, 127), sample);
        for (int key = 0; key < 128; ++key)
            if (other->held.test(size_t(key)))
                other->midi.addEvent(juce::MidiMessage::noteOff(1, key), sample);
        other->held.reset();
    }
}

int AudioEngine::arpIndex(int direction, int step, int count, int salt) noexcept
{
    switch (direction)
    {
        case 1: return step % count;
        case 2: return count - 1 - step % count;
        case 3:
        {
            if (count == 1)
                return 0;
            const auto period = 2 * count - 2;
            const auto position = step % period;
            return position < count ? position : period - position;
        }
        default: return int((juce::uint32(step) * 2654435761u + juce::uint32(salt) * 40503u) >> 7) % count;
    }
}

void AudioEngine::emitPattern(const Pattern& pattern, double from, double to, double cut, double firstSample, double samplesPerTick,
                              int numSamples, bool ons) noexcept
{
    const auto swingTicks = double(project.swing.load(std::memory_order_relaxed)) * ticksPerStep * 0.5;
    const auto offset = [&](double tick) { return juce::jlimit(0, numSamples - 1, int(firstSample + (tick - from) * samplesPerTick)); };
    const auto ticksPerMillisecond = 1.0 / (samplesPerTick / sampleRate.load() * 1000.0);

    for (const auto& [id, notes] : pattern.notes)
    {
        auto* channel = project.findChannel(id);
        if (channel == nullptr || channel->device == nullptr)
            continue;
        const auto& options = channel->options;
        const auto shift = double(options.timeShift.load(std::memory_order_relaxed));
        const auto swing = swingTicks * double(options.swingMix.load(std::memory_order_relaxed));
        const auto group = options.cutGroup.load(std::memory_order_relaxed);
        const auto mono = options.mono.load(std::memory_order_relaxed);
        const auto portaAll = options.porta.load(std::memory_order_relaxed);
        const auto arp = options.arpDirection.load(std::memory_order_relaxed);
        const auto place = [&](int tick) { return tick + (tick % (ticksPerStep * 2) == ticksPerStep ? swing : 0.0) + shift; };

        const auto send = [&](double on, double off, int key, float velocity, int glideFrom, double glideMilliseconds)
        {
            key = juce::jlimit(0, 127, key + appliedPitch);
            glideFrom = glideFrom >= 0 ? juce::jlimit(0, 127, glideFrom + appliedPitch) : glideFrom;
            if (ons)
            {
                if (on < from || on >= to || on >= cut)
                    return;
                const auto at = offset(on);
                if (group > 0)
                    cutChannels(group, at);
                if (mono)
                {
                    for (int held = 0; held < 128; ++held)
                        if (held != key && channel->held.test(size_t(held)))
                            channel->midi.addEvent(juce::MidiMessage::noteOff(1, held), at);
                    channel->held.reset();
                }
                if (glideFrom >= 0)
                {
                    channel->midi.addEvent(juce::MidiMessage::controllerEvent(1, 5, juce::jlimit(1, 127, juce::roundToInt(glideMilliseconds / 10.0))), at);
                    channel->midi.addEvent(juce::MidiMessage::controllerEvent(1, 84, juce::jlimit(0, 127, glideFrom)), at);
                }
                channel->midi.addEvent(juce::MidiMessage::noteOn(1, key, juce::jlimit(0.01f, 1.0f, velocity)), at);
                channel->held.set(size_t(key));
                return;
            }
            off = std::min(off, cut);
            if (off >= from && off < to && on < cut)
            {
                channel->midi.addEvent(juce::MidiMessage::noteOff(1, key), offset(off));
                channel->held.reset(size_t(key));
            }
        };

        for (const auto& note : notes)
        {
            if (note.muted)
                continue;
            if (arp > 0)
            {
                std::array<int, 32> keys {};
                auto count = 0;
                auto end = note.end();
                auto duplicate = false;
                for (const auto& other : notes)
                {
                    if (other.muted || other.start != note.start)
                        continue;
                    end = std::max(end, other.end());
                    if (&other < &note && other.key == note.key)
                        duplicate = true;
                    if (count < int(keys.size()) && std::find(keys.begin(), keys.begin() + count, other.key) == keys.begin() + count)
                        keys[size_t(count++)] = other.key;
                }
                if (duplicate || count == 0)
                    continue;
                std::sort(keys.begin(), keys.begin() + count);
                const auto slot = int(std::find(keys.begin(), keys.begin() + count, note.key) - keys.begin());
                const auto range = options.arpRange.load(std::memory_order_relaxed);
                const auto stepTicks = ChannelOptions::arpTicks[size_t(juce::jlimit(0, 3, options.arpTime.load(std::memory_order_relaxed)))];
                const auto gate = double(options.arpGate.load(std::memory_order_relaxed));
                const auto total = count * range;
                for (int step = 0; note.start + step * stepTicks < end; ++step)
                {
                    const auto index = arpIndex(arp, step, total, note.start);
                    if (index % count != slot)
                        continue;
                    const auto start = note.start + step * stepTicks;
                    const auto on = place(start);
                    const auto length = std::min(double(stepTicks) * gate, double(end - start));
                    send(on, on + std::max(1.0, length), note.key + 12 * (index / count), note.velocity, -1, 0.0);
                }
                continue;
            }

            const auto* source = note.slide || note.porta || portaAll ? glideSource(notes, note) : nullptr;
            if (portaAll && !note.slide && !note.porta && source != nullptr && source->end() < note.start)
                source = nullptr;
            const auto glideMilliseconds = note.slide ? note.length / ticksPerMillisecond
                                                      : (note.porta ? ticksPerStep / ticksPerMillisecond : double(options.portaTime.load(std::memory_order_relaxed)));
            const auto on = place(note.start);
            const auto end = note.slide && source != nullptr ? std::max(note.end(), source->end()) : note.end();
            send(on, on + (end - note.start), note.key, note.velocity, source != nullptr ? source->key : -1, glideMilliseconds);
        }
    }
}

void AudioEngine::releaseAll(int sampleOffset) noexcept
{
    for (auto& channel : project.channels)
    {
        if (channel->held.none())
            continue;
        for (int key = 0; key < 128; ++key)
            if (channel->held.test(size_t(key)))
                channel->midi.addEvent(juce::MidiMessage::noteOff(1, key), sampleOffset);
        channel->held.reset();
    }
}

void AudioEngine::markSoloPaths(std::array<bool, mixerTrackCount>& audible) const noexcept
{
    for (int i = 1; i < mixerTrackCount; ++i)
    {
        if (!project.mixer[size_t(i)]->solo.load(std::memory_order_relaxed))
            continue;
        audible[size_t(i)] = true;
        for (auto current = i, guard = 0; guard < mixerTrackCount; ++guard)
        {
            auto next = 0;
            for (const auto& [target, send] : project.mixer[size_t(current)]->sends)
                if (target > 0 && !send.sidechain)
                    next = target;
            if (next == 0)
                break;
            audible[size_t(next)] = true;
            current = next;
        }
        for (int source = 1; source < mixerTrackCount; ++source)
            for (const auto& [target, send] : project.mixer[size_t(source)]->sends)
                if (target == i && !send.sidechain)
                    audible[size_t(source)] = true;
    }
}

MixerTrack& AudioEngine::feed(int index, int numSamples) noexcept
{
    auto& track = *project.mixer[size_t(juce::jlimit(0, mixerTrackCount - 1, index))];
    if (!track.fed)
    {
        track.buffer.clear(0, numSamples);
        track.fed = true;
    }
    return track;
}

void AudioEngine::applyAutomation() noexcept
{
    if (!playing.load() || !project.songMode.load() || project.automations.empty())
        return;
    const auto now = position.load();
    for (const auto& clip : project.clips)
    {
        if (!clip.isAutomation() || clip.muted || now < clip.start || now >= clip.end() || clip.automation >= int(project.automations.size()))
            continue;
        if (clip.track >= 0 && clip.track < int(project.tracks.size()) && project.tracks[size_t(clip.track)].muted)
            continue;
        const auto& curve = project.automations[size_t(clip.automation)];
        const auto value = juce::jlimit(0.0f, 1.0f, curve.valueAt(now - clip.start + clip.offset));
        const auto& target = curve.target;
        auto* channel = target.channel >= 0 ? project.findChannel(target.channel) : nullptr;
        auto* track = target.track >= 0 && target.track < mixerTrackCount ? project.mixer[size_t(target.track)].get() : nullptr;
        switch (target.kind)
        {
            case AutomationTarget::Kind::channelVolume:
                if (channel != nullptr)
                    channel->volume.store(value, std::memory_order_relaxed);
                break;
            case AutomationTarget::Kind::channelPan:
                if (channel != nullptr)
                    channel->pan.store(value * 2.0f - 1.0f, std::memory_order_relaxed);
                break;
            case AutomationTarget::Kind::mixerVolume:
                if (track != nullptr)
                    track->volume.store(AutomationTarget::mixerGain(value), std::memory_order_relaxed);
                break;
            case AutomationTarget::Kind::mixerPan:
                if (track != nullptr)
                    track->pan.store(value * 2.0f - 1.0f, std::memory_order_relaxed);
                break;
            case AutomationTarget::Kind::tempo:
                project.tempo.store(AutomationTarget::tempoLow + value * (AutomationTarget::tempoHigh - AutomationTarget::tempoLow), std::memory_order_relaxed);
                break;
            case AutomationTarget::Kind::mainVolume:
                project.mainVolume.store(value, std::memory_order_relaxed);
                break;
            case AutomationTarget::Kind::parameter:
            {
                Device* device = channel != nullptr ? channel->device.get()
                                                    : (track != nullptr && target.slot >= 0 && target.slot < effectSlotCount ? track->slots[size_t(target.slot)].get() : nullptr);
                if (device == nullptr)
                    break;
                for (auto* parameter : device->processor->getParameters())
                    if (AutomationTarget::idOf(*parameter) == target.parameter)
                    {
                        if (std::abs(parameter->getValue() - value) > 0.00001f)
                            parameter->setValueNotifyingHost(value);
                        break;
                    }
                break;
            }
        }
    }
}

void AudioEngine::renderChannels(int numSamples) noexcept
{
    for (auto& channel : project.channels)
    {
        if (channel->device == nullptr)
            continue;

        juce::AudioBuffer<float> block(channel->output.getArrayOfWritePointers(), 2, numSamples);
        block.clear();
        channel->device->process(block, channel->midi, numSamples);
        if (channel->muted.load(std::memory_order_relaxed))
            continue;

        const auto gain = channel->volume.load(std::memory_order_relaxed) / defaultChannelVolume;
        const auto [left, right] = balance(channel->pan.load(std::memory_order_relaxed));
        auto& track = feed(channel->insert.load(std::memory_order_relaxed), numSamples);
        track.buffer.addFrom(0, 0, block, 0, 0, numSamples, gain * left);
        track.buffer.addFrom(1, 0, block, 1, 0, numSamples, gain * right);
    }
}

void AudioEngine::renderMixer(int numSamples) noexcept
{
    auto& master = feed(0, numSamples);
    const auto anySolo = std::any_of(project.mixer.begin() + 1, project.mixer.end(), [](const auto& t) { return t->solo.load(std::memory_order_relaxed); });
    std::array<bool, mixerTrackCount> audible {};
    audible.fill(!anySolo);
    if (anySolo)
        markSoloPaths(audible);

    const auto finish = [numSamples](MixerTrack& track, float extraGain)
    {
        juce::AudioBuffer<float> block(track.buffer.getArrayOfWritePointers(), 2, numSamples);
        const juce::AudioBuffer<float>* sidechain = nullptr;
        if (track.sidechainFed)
            sidechain = &track.sidechain;
        for (auto& slot : track.slots)
            if (slot != nullptr)
                slot->process(block, numSamples, sidechain);
        auto* left = block.getWritePointer(0);
        auto* right = block.getWritePointer(1);
        if (track.swapSides.load(std::memory_order_relaxed))
            std::swap_ranges(left, left + numSamples, right);
        if (const auto width = track.width.load(std::memory_order_relaxed); std::abs(width - 1.0f) > 0.001f)
            for (int i = 0; i < numSamples; ++i)
            {
                const auto mid = (left[i] + right[i]) * 0.5f;
                const auto side = (left[i] - right[i]) * 0.5f * width;
                left[i] = mid + side;
                right[i] = mid - side;
            }
        const auto gain = track.volume.load(std::memory_order_relaxed) * extraGain * (track.invertPhase.load(std::memory_order_relaxed) ? -1.0f : 1.0f);
        const auto [panLeft, panRight] = balance(track.pan.load(std::memory_order_relaxed));
        block.applyGain(0, 0, numSamples, gain * panLeft);
        block.applyGain(1, 0, numSamples, gain * panRight);
        meter(track.peakLeft, block.getMagnitude(0, 0, numSamples));
        meter(track.peakRight, block.getMagnitude(1, 0, numSamples));
    };

    for (const auto index : project.mixerOrder)
    {
        auto& track = *project.mixer[size_t(index)];
        if (!track.fed && !track.hasEffects())
            continue;
        feed(index, numSamples);
        finish(track, 1.0f);
        if (track.muted.load(std::memory_order_relaxed) || !audible[size_t(index)])
            continue;
        for (const auto& [target, send] : track.sends)
        {
            if (target < 0 || target >= mixerTrackCount || send.level <= 0.0f)
                continue;
            auto& destination = *project.mixer[size_t(target)];
            if (send.sidechain)
            {
                if (!destination.sidechainFed)
                {
                    destination.sidechain.clear(0, numSamples);
                    destination.sidechainFed = true;
                }
                destination.sidechain.addFrom(0, 0, track.buffer, 0, 0, numSamples, send.level);
                destination.sidechain.addFrom(1, 0, track.buffer, 1, 0, numSamples, send.level);
                continue;
            }
            auto& fedTarget = feed(target, numSamples);
            fedTarget.buffer.addFrom(0, 0, track.buffer, 0, 0, numSamples, send.level);
            fedTarget.buffer.addFrom(1, 0, track.buffer, 1, 0, numSamples, send.level);
        }
    }

    if (master.muted.load(std::memory_order_relaxed))
        master.buffer.clear(0, numSamples);
    for (int c = 0; c < 2; ++c)
    {
        auto* data = master.buffer.getWritePointer(c);
        auto& input = dcInput[size_t(c)];
        auto& filtered = dcOutput[size_t(c)];
        for (int i = 0; i < numSamples; ++i)
        {
            filtered = data[i] - input + 0.9995f * filtered;
            input = data[i];
            data[i] = filtered;
        }
    }
    finish(master, project.mainVolume.load(std::memory_order_relaxed) / 0.8f);
}

void AudioEngine::renderExtras(int numSamples) noexcept
{
    auto& master = project.mixer[0]->buffer;

    if (click != nullptr)
    {
        const auto todo = std::min(numSamples - clickDelay, click->getNumSamples() - clickPosition);
        for (int c = 0; c < 2; ++c)
            master.addFrom(c, clickDelay, *click, 0, clickPosition, todo);
        clickPosition += todo;
        clickDelay = 0;
        if (clickPosition >= click->getNumSamples())
            click = nullptr;
    }

    if (previewSample == nullptr || previewPosition < 0.0)
        return;

    const auto& audio = previewSample->audio;
    const auto length = audio.getNumSamples();
    const auto step = previewSample->sampleRate / sampleRate.load();
    for (int i = 0; i < numSamples; ++i)
    {
        const auto index = int(previewPosition);
        if (index + 1 >= length)
        {
            previewPosition = -1.0;
            return;
        }
        const auto fraction = float(previewPosition - index);
        for (int c = 0; c < 2; ++c)
        {
            const auto* source = audio.getReadPointer(std::min(c, audio.getNumChannels() - 1));
            master.addSample(c, i, (source[index] + (source[index + 1] - source[index]) * fraction) * 0.7f);
        }
        previewPosition += step;
    }
}
}
