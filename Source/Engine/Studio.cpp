#include "Engine/Studio.h"
#include "Engine/Builtins/Pack.h"
#include "Engine/Builtins/Registry.h"
#include "Engine/Builtins/Sampler.h"

namespace ts
{
namespace
{
std::unique_ptr<Channel> makeChannel(int id, const juce::String& name, juce::Colour colour, std::unique_ptr<Device> device)
{
    auto channel = std::make_unique<Channel>();
    channel->id = id;
    channel->name = name;
    channel->colour = colour;
    channel->device = std::move(device);
    return channel;
}
}

Studio::Studio()
{
    newProject();

    const auto saved = plugins.settings().getXmlValue("audio");
    devices.initialise(0, 2, saved.get(), true);
    if (saved == nullptr)
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            devices.setMidiInputDeviceEnabled(input.identifier, true);

    player.setSource(&engine);
    devices.addAudioCallback(&player);
    devices.addMidiInputDeviceCallback({}, &engine.live);
}

Studio::~Studio()
{
    saveSettings();
    devices.removeMidiInputDeviceCallback({}, &engine.live);
    devices.removeAudioCallback(&player);
    player.setSource(nullptr);
}

void Studio::saveSettings()
{
    if (const auto state = devices.createStateXml())
        plugins.settings().setValue("audio", state.get());
    plugins.settings().saveIfNeeded();
}

std::unique_ptr<Device> Studio::makeDevice(std::unique_ptr<juce::AudioProcessor> processor, const juce::String& builtin, const juce::PluginDescription& description)
{
    if (processor == nullptr)
        return nullptr;
    auto device = std::make_unique<Device>();
    device->processor = std::move(processor);
    device->builtin = builtin;
    device->description = description;
    return device;
}

std::unique_ptr<Device> Studio::createBuiltin(const juce::String& name)
{
    return makeDevice(Builtins::create(name), name, {});
}

std::unique_ptr<Device> Studio::createPlugin(const juce::PluginDescription& description, juce::String& error)
{
    return makeDevice(plugins.create(description, engine.getSampleRate(), engine.getBlockSize(), error), {}, description);
}

Channel& Studio::addChannel(std::unique_ptr<Device> device, const juce::String& name)
{
    auto channel = makeChannel(project.nextChannelId++, name, project.nextChannelColour(), std::move(device));
    engine.prepare(*channel);
    auto& added = *channel;
    {
        const juce::ScopedLock sl(project.lock);
        project.channels.push_back(std::move(channel));
    }
    selectChannel(added.id);
    project.touch();
    return added;
}

Channel* Studio::addSampleChannel(const juce::File& file)
{
    auto data = Sampler::readFile(file);
    if (data == nullptr)
        return nullptr;
    auto device = createBuiltin(Sampler::typeName);
    static_cast<Sampler&>(*device->processor).setSample(std::move(data));
    return &addChannel(std::move(device), file.getFileNameWithoutExtension());
}

Channel& Studio::addPackChannel(const juce::String& sound)
{
    auto device = createBuiltin(Sampler::typeName);
    static_cast<Sampler&>(*device->processor).setSample(Pack::get(sound));
    return addChannel(std::move(device), sound);
}

Channel& Studio::cloneChannel(int id)
{
    auto* source = project.findChannel(id);
    jassert(source != nullptr);

    std::unique_ptr<Device> device;
    if (source->device != nullptr)
    {
        juce::String error;
        device = source->device->isPlugin() ? createPlugin(source->device->description, error) : createBuiltin(source->device->builtin);
        if (device != nullptr)
        {
            juce::MemoryBlock state;
            source->device->processor->getStateInformation(state);
            device->processor->setStateInformation(state.getData(), int(state.getSize()));
        }
    }

    auto& clone = addChannel(std::move(device), source->name + " #2");
    clone.colour = source->colour;
    clone.volume.store(source->volume.load());
    clone.pan.store(source->pan.load());
    clone.insert.store(source->insert.load());
    clone.options.copyFrom(source->options);

    const juce::ScopedLock sl(project.lock);
    const auto from = project.indexOfChannel(id);
    auto moved = std::move(project.channels.back());
    project.channels.pop_back();
    project.channels.insert(project.channels.begin() + from + 1, std::move(moved));
    return clone;
}

void Studio::removeChannel(int id)
{
    std::unique_ptr<Channel> removed;
    {
        const juce::ScopedLock sl(project.lock);
        const auto index = project.indexOfChannel(id);
        if (index < 0)
            return;
        removed = std::move(project.channels[size_t(index)]);
        project.channels.erase(project.channels.begin() + index);
        for (auto& pattern : project.patterns)
            pattern.notes.erase(id);
    }
    retire(std::move(removed->device));
    if (project.selectedChannel == id)
        selectChannel(project.channels.empty() ? 0 : project.channels.front()->id);
    project.clearHistory();
    project.touch();
}

void Studio::moveChannel(int id, int delta)
{
    {
        const juce::ScopedLock sl(project.lock);
        const auto index = project.indexOfChannel(id);
        const auto target = index + delta;
        if (index < 0 || target < 0 || target >= int(project.channels.size()))
            return;
        std::swap(project.channels[size_t(index)], project.channels[size_t(target)]);
    }
    project.touch();
}

void Studio::setChannelDevice(int id, std::unique_ptr<Device> device)
{
    auto* channel = project.findChannel(id);
    if (channel == nullptr)
        return;
    if (device != nullptr)
        engine.prepare(*device);
    {
        const juce::ScopedLock sl(project.lock);
        std::swap(channel->device, device);
    }
    retire(std::move(device));
    project.touch();
}

void Studio::setSlot(int track, int slot, std::unique_ptr<Device> device)
{
    if (device != nullptr)
        engine.prepare(*device);
    {
        const juce::ScopedLock sl(project.lock);
        std::swap(project.mixer[size_t(track)]->slots[size_t(slot)], device);
    }
    retire(std::move(device));
    project.touch();
}

void Studio::moveSlot(int track, int slot, int delta)
{
    const auto target = slot + delta;
    if (target < 0 || target >= effectSlotCount)
        return;
    {
        const juce::ScopedLock sl(project.lock);
        auto& slots = project.mixer[size_t(track)]->slots;
        std::swap(slots[size_t(slot)], slots[size_t(target)]);
    }
    project.touch();
}

void Studio::selectChannel(int id)
{
    project.selectedChannel = id;
    engine.liveChannel.store(id);
    project.sendChangeMessage();
}

int Studio::addAutomation(const AutomationTarget& target, const juce::String& name, float value, int start, int length, int track)
{
    project.checkpoint();
    const auto span = length > 0 ? length : juce::jmax(project.songLength(), ticksPerBar * 4);
    AutomationClip curve;
    curve.name = name;
    curve.colour = juce::Colour(0xff8fb3c9);
    curve.target = target;
    curve.length = span;
    curve.points = { { 0, juce::jlimit(0.0f, 1.0f, value), 0.0f }, { span, juce::jlimit(0.0f, 1.0f, value), 0.0f } };
    auto row = track;
    if (row < 0)
    {
        row = 0;
        for (const auto& clip : project.clips)
            row = juce::jmax(row, clip.track + 1);
    }
    int index = 0;
    {
        const juce::ScopedLock sl(project.lock);
        project.automations.push_back(std::move(curve));
        index = int(project.automations.size()) - 1;
        Clip clip;
        clip.track = juce::jlimit(0, playlistTrackCount - 1, row);
        clip.start = juce::jmax(0, start);
        clip.length = span;
        clip.automation = index;
        project.clips.push_back(clip);
    }
    project.touch();
    return index;
}

juce::String Studio::describe(const AutomationTarget& target) const
{
    const auto* channel = project.findChannel(target.channel);
    const auto track = target.track >= 0 && target.track < mixerTrackCount ? project.mixer[size_t(target.track)]->name : juce::String();
    switch (target.kind)
    {
        case AutomationTarget::Kind::channelVolume: return (channel != nullptr ? channel->name : juce::String("Channel")) + " - Volume";
        case AutomationTarget::Kind::channelPan: return (channel != nullptr ? channel->name : juce::String("Channel")) + " - Pan";
        case AutomationTarget::Kind::mixerVolume: return track + " - Volume";
        case AutomationTarget::Kind::mixerPan: return track + " - Pan";
        case AutomationTarget::Kind::tempo: return "Tempo";
        case AutomationTarget::Kind::mainVolume: return "Main volume";
        case AutomationTarget::Kind::parameter: break;
    }
    const Device* device = channel != nullptr ? channel->device.get()
                                              : (target.track >= 0 && target.track < mixerTrackCount && target.slot >= 0 && target.slot < effectSlotCount
                                                     ? project.mixer[size_t(target.track)]->slots[size_t(target.slot)].get()
                                                     : nullptr);
    if (device == nullptr)
        return "Automation";
    for (auto* parameter : device->processor->getParameters())
        if (AutomationTarget::idOf(*parameter) == target.parameter)
            return (channel != nullptr ? channel->name : track + " " + device->getName()) + " - " + parameter->getName(40);
    return device->getName();
}

int Studio::automateParameter(juce::AudioProcessor& processor, int parameterIndex)
{
    const auto& parameters = processor.getParameters();
    if (!juce::isPositiveAndBelow(parameterIndex, parameters.size()))
        return -1;
    auto* parameter = parameters[parameterIndex];
    AutomationTarget target;
    target.parameter = AutomationTarget::idOf(*parameter);
    for (const auto& channel : project.channels)
        if (channel->device != nullptr && channel->device->processor.get() == &processor)
            target.channel = channel->id;
    for (int t = 0; t < mixerTrackCount && target.channel < 0 && target.track < 0; ++t)
        for (int s = 0; s < effectSlotCount; ++s)
            if (const auto& slot = project.mixer[size_t(t)]->slots[size_t(s)]; slot != nullptr && slot->processor.get() == &processor)
            {
                target.track = t;
                target.slot = s;
            }
    if (target.channel < 0 && target.track < 0)
        return -1;
    return addAutomation(target, describe(target), parameter->getValue());
}

juce::File Studio::userFolder(const juce::String& name)
{
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TStudio").getChildFile(name);
    folder.createDirectory();
    return folder;
}

void Studio::rememberRecent(const juce::File& file)
{
    auto list = juce::StringArray::fromLines(plugins.settings().getValue("recentFiles"));
    list.removeString(file.getFullPathName());
    list.insert(0, file.getFullPathName());
    list.removeEmptyStrings();
    while (list.size() > 12)
        list.remove(list.size() - 1);
    plugins.settings().setValue("recentFiles", list.joinIntoString("\n"));
    plugins.settings().saveIfNeeded();
}

juce::Array<juce::File> Studio::recentFiles()
{
    juce::Array<juce::File> result;
    for (const auto& path : juce::StringArray::fromLines(plugins.settings().getValue("recentFiles")))
        if (const juce::File file(path); path.isNotEmpty() && file.existsAsFile())
            result.add(file);
    return result;
}

int Studio::addPattern()
{
    project.checkpoint();
    {
        const juce::ScopedLock sl(project.lock);
        project.patterns.push_back({ "Pattern " + juce::String(project.patterns.size() + 1), project.nextPatternColour(), {} });
    }
    selectPattern(int(project.patterns.size()) - 1);
    return project.currentPattern.load();
}

int Studio::clonePattern(int index)
{
    if (index < 0 || index >= int(project.patterns.size()))
        return -1;
    project.checkpoint();
    {
        const juce::ScopedLock sl(project.lock);
        auto copy = project.patterns[size_t(index)];
        copy.name << " #2";
        project.patterns.insert(project.patterns.begin() + index + 1, std::move(copy));
        for (auto& clip : project.clips)
            if (!clip.isAutomation() && clip.pattern > index)
                ++clip.pattern;
    }
    selectPattern(index + 1);
    return index + 1;
}

void Studio::removePattern(int index)
{
    if (index < 0 || index >= int(project.patterns.size()))
        return;
    project.checkpoint();
    {
        const juce::ScopedLock sl(project.lock);
        if (project.patterns.size() == 1)
        {
            project.patterns.front().notes.clear();
            project.clips.clear();
        }
        else
        {
            project.patterns.erase(project.patterns.begin() + index);
            project.clips.erase(std::remove_if(project.clips.begin(), project.clips.end(), [index](const Clip& c) { return !c.isAutomation() && c.pattern == index; }), project.clips.end());
            for (auto& clip : project.clips)
                if (!clip.isAutomation() && clip.pattern > index)
                    --clip.pattern;
        }
        project.currentPattern.store(juce::jlimit(0, int(project.patterns.size()) - 1, project.currentPattern.load()));
    }
    project.touch();
}

void Studio::selectPattern(int index)
{
    project.currentPattern.store(juce::jlimit(0, int(project.patterns.size()) - 1, index));
    project.touch();
}

void Studio::retire(std::unique_ptr<Device> device)
{
    if (device != nullptr && onDeviceRemoving)
        onDeviceRemoving(device->processor.get());
}

void Studio::fillDefaults(Project& target)
{
    for (const auto* sound : { "Kick", "Clap", "Hat", "Snare" })
    {
        auto device = createBuiltin(Sampler::typeName);
        static_cast<Sampler&>(*device->processor).setSample(Pack::get(sound));
        target.channels.push_back(makeChannel(target.nextChannelId++, sound, target.nextChannelColour(), std::move(device)));
    }
    target.mixer[0]->slots[0] = createBuiltin("Tomato Limiter");
}

void Studio::replace(Project& next)
{
    if (onContentReplacing)
        onContentReplacing();
    engine.setPlaying(false);
    engine.prepare(next);
    project.swapContent(next);
    engine.liveChannel.store(project.selectedChannel);
}

void Studio::newProject()
{
    Project next;
    fillDefaults(next);
    replace(next);
}
}
