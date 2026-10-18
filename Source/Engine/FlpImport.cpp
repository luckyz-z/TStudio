#include "Engine/Studio.h"
#include "Engine/Builtins/Sampler.h"
#include "Engine/Builtins/Synth.h"

#include <map>

namespace ts
{
namespace
{
constexpr int patternBase = 20480;

struct FlChannel
{
    int iid = 0;
    int type = 0;
    juce::String name, internal, sample, vst;
    bool enabled = true;
    int insert = 0;
    int root = 60;
    float volume = defaultChannelVolume;
    float pan = 0.0f;
    std::optional<juce::Colour> colour;
};

struct FlPattern
{
    juce::String name;
    std::optional<juce::Colour> colour;
    std::vector<std::pair<int, Note>> notes;
};

struct FlItem
{
    juce::int64 start = 0, length = 0;
    int pattern = 0, track = 0;
    double offset = 0.0;
};

struct FlSlot
{
    int insert = 0, slot = 0;
    juce::String internal, name, vst;
};

struct FlProject
{
    int ppq = 96;
    double tempo = 0.0, coarseTempo = 0.0, fineTempo = 0.0;
    juce::String title;
    int currentPattern = 1;
    std::vector<FlChannel> channels;
    std::map<int, FlPattern> patterns;
    std::vector<FlItem> items;
    std::map<int, juce::String> trackNames;
    std::map<int, juce::String> insertNames;
    std::vector<FlSlot> slots;
};

const std::vector<std::pair<juce::String, juce::String>> nativeEffects {
    { "Fruity Limiter", "Tomato Limiter" },
    { "Fruity Soft Clipper", "Tomato Clipper" },
    { "Maximus", "Tomato Multiband" },
    { "Fruity Reeverb 2", "Tomato Reverb" },
    { "Fruity Reeverb", "Tomato Reverb" },
    { "Fruity Convolver", "Tomato Reverb" },
    { "Fruity Delay 3", "Tomato Delay" },
    { "Fruity Delay 2", "Tomato Delay" },
    { "Fruity Delay", "Tomato Delay" },
    { "Fruity Delay Bank", "Tomato Delay" },
    { "Fruity Parametric EQ 2", "Tomato EQ" },
    { "Fruity Parametric EQ", "Tomato EQ" },
    { "Fruity 7 Band EQ", "Tomato EQ" },
    { "Fruity Compressor", "Tomato Compressor" },
    { "Fruity Multiband Compressor", "Tomato Multiband" },
    { "Fruity Filter", "Tomato Filter" },
    { "Fruity Love Philter", "Tomato Filter" },
    { "Fruity Chorus", "Tomato Chorus" },
    { "Fruity Flanger", "Tomato Flanger" },
    { "Fruity Flangus", "Tomato Flanger" },
    { "Fruity Phaser", "Tomato Phaser" },
    { "Fruity Blood Overdrive", "Tomato Distortion" },
    { "Fruity Fast Dist", "Tomato Distortion" },
    { "Fruity WaveShaper", "Tomato Shaper" },
    { "Transient Processor", "Tomato Transient" },
    { "Gross Beat", "Tomato Glitch" },
    { "Fruity Squeeze", "Tomato Lofi" },
    { "Soundgoodizer", "Tomato Maximizer" },
    { "Fruity PanOMatic", "Tomato Autopan" },
    { "Fruity Bass Boost", "Tomato Bass" },
    { "Wave Candy", "Tomato Scope" },
    { "Fruity Stereo Shaper", "Tomato Stereo" },
    { "Fruity Stereo Enhancer", "Tomato Stereo" },
    { "Fruity Balance", "Tomato Stereo" },
};

class Reader
{
public:
    Reader(const juce::uint8* d, size_t s) : data(d), size(s) {}

    bool has(size_t count) const noexcept { return position + count <= size; }
    size_t remaining() const noexcept { return size - position; }
    const juce::uint8* here() const noexcept { return data + position; }
    void skip(size_t count) noexcept { position = juce::jmin(size, position + count); }
    juce::uint8 u8() noexcept { return has(1) ? data[position++] : 0; }

    juce::uint16 u16() noexcept
    {
        if (!has(2))
            return skipAll<juce::uint16>();
        const auto value = juce::ByteOrder::littleEndianShort(here());
        position += 2;
        return value;
    }

    juce::uint32 u32() noexcept
    {
        if (!has(4))
            return skipAll<juce::uint32>();
        const auto value = juce::ByteOrder::littleEndianInt(here());
        position += 4;
        return value;
    }

    juce::uint64 u64() noexcept
    {
        if (!has(8))
            return skipAll<juce::uint64>();
        const auto value = juce::ByteOrder::littleEndianInt64(here());
        position += 8;
        return value;
    }

    float f32() noexcept
    {
        const auto bits = u32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    size_t varint() noexcept
    {
        size_t value = 0;
        for (int shift = 0; shift < 35 && has(1); shift += 7)
        {
            const auto byte = u8();
            value |= size_t(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0)
                break;
        }
        return value;
    }

private:
    template <typename Type>
    Type skipAll() noexcept
    {
        position = size;
        return 0;
    }

    const juce::uint8* data;
    size_t size;
    size_t position = 0;
};

juce::String decodeText(const juce::uint8* data, size_t length, bool unicode)
{
    juce::String text;
    if (unicode)
    {
        juce::MemoryOutputStream utf16;
        for (size_t i = 0; i + 1 < length; i += 2)
        {
            const auto unit = juce::ByteOrder::littleEndianShort(data + i);
            if (unit == 0)
                break;
            utf16.writeShort(short(unit));
        }
        utf16.writeShort(0);
        text = juce::String(juce::CharPointer_UTF16(static_cast<const juce::CharPointer_UTF16::CharType*>(utf16.getData())));
    }
    else
    {
        text = juce::String::fromUTF8(reinterpret_cast<const char*>(data), int(length));
    }
    return text.upToFirstOccurrenceOf(juce::String::charToString(0), false, false).trim();
}

juce::Colour decodeColour(juce::uint32 value)
{
    return juce::Colour(juce::uint8(value & 0xff), juce::uint8((value >> 8) & 0xff), juce::uint8((value >> 16) & 0xff));
}

juce::String wrappedPluginName(const juce::uint8* data, size_t length)
{
    Reader reader(data, length);
    reader.u32();
    while (reader.has(12))
    {
        const auto id = reader.u32();
        const auto size = size_t(reader.u64());
        if (!reader.has(size))
            break;
        if (id == 54)
            return juce::String::fromUTF8(reinterpret_cast<const char*>(reader.here()), int(size)).trim();
        reader.skip(size);
    }
    return {};
}

bool parse(const juce::MemoryBlock& file, FlProject& project, juce::String& error)
{
    const auto* bytes = static_cast<const juce::uint8*>(file.getData());
    if (file.getSize() < 22 || std::memcmp(bytes, "FLhd", 4) != 0 || std::memcmp(bytes + 14, "FLdt", 4) != 0)
    {
        error = "This is not an FL Studio project file.";
        return false;
    }

    project.ppq = juce::jmax(1, int(juce::ByteOrder::littleEndianShort(bytes + 12)));
    const auto dataSize = juce::jmin(size_t(juce::ByteOrder::littleEndianInt(bytes + 18)), file.getSize() - 22);
    Reader reader(bytes + 22, dataSize);

    auto unicode = false;
    auto inMixer = false;
    auto arrangements = 0;
    auto trackIndex = -1;
    auto insertGroup = 0;
    int channel = -1;
    int slot = -1;
    int pattern = 0;

    while (reader.has(1))
    {
        const auto id = reader.u8();
        juce::uint32 value = 0;
        const juce::uint8* data = nullptr;
        size_t length = 0;
        if (id < 64)
            value = reader.u8();
        else if (id < 128)
            value = reader.u16();
        else if (id < 192)
            value = reader.u32();
        else
        {
            length = juce::jmin(reader.varint(), reader.remaining());
            data = reader.here();
            reader.skip(length);
        }

        const auto text = [&] { return decodeText(data, length, unicode); };
        auto* current = channel >= 0 && !inMixer ? &project.channels[size_t(channel)] : nullptr;
        auto* currentSlot = inMixer && slot >= 0 ? &project.slots[size_t(slot)] : nullptr;

        switch (id)
        {
            case 199:
            {
                const auto version = juce::StringArray::fromTokens(decodeText(data, length, false), ".", {});
                const auto major = version[0].getIntValue();
                unicode = major > 11 || (major == 11 && version[1].getIntValue() >= 5);
                break;
            }
            case 156: project.tempo = value / 1000.0; break;
            case 66: project.coarseTempo = value; break;
            case 93: project.fineTempo = value / 1000.0; break;
            case 194: project.title = text(); break;
            case 67: project.currentPattern = int(value); break;
            case 64:
                project.channels.push_back({});
                project.channels.back().iid = int(value);
                channel = int(project.channels.size()) - 1;
                break;
            case 21: if (current) current->type = int(value); break;
            case 0: if (current) current->enabled = value != 0; break;
            case 22: if (current) current->insert = int(juce::int8(value)); break;
            case 128: if (current) current->colour = decodeColour(value); break;
            case 135: if (current) current->root = int(value); break;
            case 196: if (current) current->sample = text(); break;
            case 192: if (current && current->name.isEmpty()) current->name = text(); break;
            case 201:
                if (currentSlot) currentSlot->internal = text();
                else if (current) current->internal = text();
                break;
            case 203:
                if (currentSlot) currentSlot->name = text();
                else if (current) current->name = text();
                break;
            case 213:
                if (currentSlot && currentSlot->internal == "Fruity Wrapper") currentSlot->vst = wrappedPluginName(data, length);
                else if (current && current->internal == "Fruity Wrapper") current->vst = wrappedPluginName(data, length);
                break;
            case 219:
                if (current && length >= 8)
                {
                    current->pan = juce::jlimit(-1.0f, 1.0f, (float(juce::int32(juce::ByteOrder::littleEndianInt(data))) - 6400.0f) / 6400.0f);
                    current->volume = juce::jlimit(0.0f, 1.0f, float(juce::ByteOrder::littleEndianInt(data + 4)) / 12800.0f);
                }
                break;
            case 65:
                pattern = int(value);
                project.patterns[pattern];
                break;
            case 193: project.patterns[pattern].name = text(); break;
            case 150: project.patterns[pattern].colour = decodeColour(value); break;
            case 224:
                for (size_t offset = 0; offset + 24 <= length; offset += 24)
                {
                    const auto* note = data + offset;
                    Note result;
                    result.start = int(juce::ByteOrder::littleEndianInt(note));
                    result.length = int(juce::ByteOrder::littleEndianInt(note + 8));
                    result.key = juce::ByteOrder::littleEndianShort(note + 12);
                    result.velocity = juce::jlimit(0.0f, 1.0f, note[21] / 128.0f);
                    result.slide = (juce::ByteOrder::littleEndianShort(note + 4) & 8) != 0;
                    project.patterns[pattern].notes.emplace_back(int(juce::ByteOrder::littleEndianShort(note + 6)), result);
                }
                break;
            case 99:
                ++arrangements;
                trackIndex = -1;
                break;
            case 233:
            {
                if (arrangements > 1)
                    break;
                const auto record = length % 60 == 0 ? size_t(60) : size_t(32);
                for (size_t offset = 0; offset + record <= length; offset += record)
                {
                    Reader item(data + offset, record);
                    FlItem result;
                    result.start = item.u32();
                    item.u16();
                    const auto index = int(item.u16());
                    result.length = item.u32();
                    const auto reversed = int(item.u16());
                    item.skip(10);
                    result.offset = item.f32();
                    if (index <= patternBase)
                        continue;
                    result.pattern = index - patternBase;
                    result.track = (arrangements > 0 ? 499 : 198) - reversed;
                    project.items.push_back(result);
                }
                break;
            }
            case 238: ++trackIndex; break;
            case 239: if (arrangements <= 1 && trackIndex >= 0) project.trackNames[trackIndex] = text(); break;
            case 236: case 235: case 154: case 149: case 225: inMixer = true; break;
            case 204:
                inMixer = true;
                project.insertNames[insertGroup] = text();
                break;
            case 147:
                inMixer = true;
                ++insertGroup;
                slot = -1;
                break;
            case 98:
                inMixer = true;
                project.slots.push_back({ insertGroup, int(value), {}, {}, {} });
                slot = int(project.slots.size()) - 1;
                break;
            default: break;
        }
    }
    return true;
}

juce::File resolveSample(const juce::String& path, const juce::File& projectFile)
{
    if (path.isEmpty())
        return {};

    juce::Array<juce::File> candidates;
    if (path.contains("%FLStudioFactoryData%"))
    {
        juce::Array<juce::File> installs;
        for (const auto& root : { juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory).getChildFile("Image-Line"),
                                  juce::File("C:\\Program Files\\Image-Line") })
            for (const auto& entry : juce::RangedDirectoryIterator(root, false, "FL Studio*", juce::File::findDirectories))
                installs.add(entry.getFile());
        installs.sort();
        for (int i = installs.size(); --i >= 0;)
            candidates.add(juce::File(path.replace("%FLStudioFactoryData%", installs[i].getFullPathName())));
    }
    else if (path.contains("%FLStudioUserData%"))
    {
        const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        candidates.add(juce::File(path.replace("%FLStudioUserData%", documents.getChildFile("Image-Line/FL Studio").getFullPathName())));
    }
    else if (juce::File::isAbsolutePath(path))
    {
        candidates.add(juce::File(path));
    }

    const auto fileName = path.fromLastOccurrenceOf("\\", false, false).fromLastOccurrenceOf("/", false, false);
    candidates.add(projectFile.getSiblingFile(fileName));
    for (const auto& candidate : candidates)
        if (candidate.existsAsFile())
            return candidate;
    return candidates.isEmpty() ? juce::File() : candidates.getFirst();
}

std::optional<juce::PluginDescription> findPlugin(const juce::KnownPluginList& known, const juce::String& name)
{
    if (name.isEmpty())
        return {};
    for (const auto& type : known.getTypes())
        if (type.name.equalsIgnoreCase(name))
            return type;
    for (const auto& type : known.getTypes())
        if (type.name.containsIgnoreCase(name) || name.containsIgnoreCase(type.name))
            return type;
    return {};
}
}

bool Studio::importFlp(const juce::File& file, juce::String& error)
{
    juce::MemoryBlock content;
    if (!file.loadFileAsData(content))
    {
        error = "Could not read " + file.getFullPathName();
        return false;
    }

    FlProject source;
    if (!parse(content, source, error))
        return false;

    Project next;
    const auto scale = [&](juce::int64 ticks) { return int(ticks * ppq / source.ppq); };
    next.title = source.title.isNotEmpty() ? source.title : file.getFileNameWithoutExtension();
    const auto tempo = source.tempo > 0.0 ? source.tempo : source.coarseTempo + source.fineTempo;
    next.tempo.store(tempo > 0.0 ? juce::jlimit(10.0, 522.0, tempo) : AudioEngine::defaultTempo);

    juce::StringArray warnings;
    std::map<int, int> channelIds;
    for (const auto& flChannel : source.channels)
    {
        if (flChannel.type == 3 || flChannel.type == 5)
            continue;

        const auto displayName = flChannel.name.isNotEmpty() ? flChannel.name : (flChannel.internal.isNotEmpty() ? flChannel.internal : juce::String("Channel"));
        std::unique_ptr<Device> device;
        const auto isSampler = flChannel.type == 0 || (flChannel.sample.isNotEmpty() && flChannel.internal.isEmpty());

        if (isSampler)
        {
            device = createBuiltin(Sampler::typeName);
            juce::XmlElement state("State");
            state.setAttribute("file", resolveSample(flChannel.sample, file).getFullPathName());
            state.setAttribute("name", juce::File(flChannel.sample.replaceCharacter('\\', '/')).getFileNameWithoutExtension());
            auto* root = state.createNewChildElement("Parameter");
            root->setAttribute("id", "root");
            root->setAttribute("value", juce::jlimit(0, 127, flChannel.root) / 127.0);
            juce::MemoryBlock binary;
            juce::AudioProcessor::copyXmlToBinary(state, binary);
            if (flChannel.sample.isNotEmpty())
                device->processor->setStateInformation(binary.getData(), int(binary.getSize()));
        }
        else
        {
            const auto wanted = flChannel.internal == "Fruity Wrapper" ? flChannel.vst : flChannel.internal;
            if (const auto description = findPlugin(plugins.known, wanted))
            {
                juce::String reason;
                device = createPlugin(*description, reason);
            }
            if (device == nullptr)
            {
                device = createBuiltin(Synth::typeName);
                if (!wanted.equalsIgnoreCase("3x Osc"))
                    warnings.add("\"" + displayName + "\" uses " + (wanted.isNotEmpty() ? wanted : juce::String("an unknown plugin")) + ", replaced with Tomato Synth");
            }
        }

        auto channel = std::make_unique<Channel>();
        channel->id = next.nextChannelId++;
        channel->name = displayName;
        channel->colour = flChannel.colour.value_or(next.nextChannelColour());
        channel->device = std::move(device);
        channel->volume.store(flChannel.volume);
        channel->pan.store(flChannel.pan);
        channel->muted.store(!flChannel.enabled);
        channel->insert.store(juce::jlimit(0, mixerTrackCount - 1, flChannel.insert));
        channelIds[flChannel.iid] = channel->id;
        next.channels.push_back(std::move(channel));
    }

    std::map<int, int> patternIndices;
    next.patterns.clear();
    for (const auto& [iid, flPattern] : source.patterns)
    {
        Pattern pattern { flPattern.name.isNotEmpty() ? flPattern.name : "Pattern " + juce::String(iid), flPattern.colour.value_or(next.nextPatternColour()), {} };
        for (auto [channelIid, note] : flPattern.notes)
        {
            const auto found = channelIds.find(channelIid);
            if (found == channelIds.end())
                continue;
            note.start = scale(note.start);
            note.length = note.length > 0 ? juce::jmax(1, scale(note.length)) : ticksPerStep;
            note.key = juce::jlimit(0, 127, note.key);
            pattern.notes[found->second].push_back(note);
        }
        patternIndices[iid] = int(next.patterns.size());
        next.patterns.push_back(std::move(pattern));
    }
    if (next.patterns.empty())
        next.patterns.push_back({ "Pattern 1", next.nextPatternColour(), {} });
    if (const auto found = patternIndices.find(source.currentPattern); found != patternIndices.end())
        next.currentPattern.store(found->second);

    for (const auto& item : source.items)
    {
        const auto found = patternIndices.find(item.pattern);
        if (found == patternIndices.end() || item.length <= 0)
            continue;
        Clip clip;
        clip.track = juce::jlimit(0, playlistTrackCount - 1, item.track);
        clip.start = scale(item.start);
        clip.length = juce::jmax(1, scale(item.length));
        clip.pattern = found->second;
        clip.offset = item.offset > 0.0 && std::isfinite(item.offset) ? scale(juce::int64(item.offset)) : 0;
        next.clips.push_back(clip);
    }
    next.songMode.store(!next.clips.empty());

    for (const auto& [index, name] : source.trackNames)
        if (index >= 0 && index < playlistTrackCount && name.isNotEmpty())
            next.tracks[size_t(index)].name = name;
    for (const auto& [index, name] : source.insertNames)
        if (index >= 0 && index < mixerTrackCount && name.isNotEmpty())
            next.mixer[size_t(index)]->name = name;

    for (const auto& flSlot : source.slots)
    {
        if (flSlot.insert < 0 || flSlot.insert >= mixerTrackCount || flSlot.slot < 0 || flSlot.slot >= effectSlotCount || flSlot.internal.isEmpty())
            continue;
        std::unique_ptr<Device> device;
        for (const auto& [native, builtin] : nativeEffects)
            if (flSlot.internal.equalsIgnoreCase(native))
                device = createBuiltin(builtin);
        if (device == nullptr)
        {
            if (const auto description = findPlugin(plugins.known, flSlot.internal == "Fruity Wrapper" ? flSlot.vst : flSlot.internal))
            {
                juce::String reason;
                device = createPlugin(*description, reason);
            }
        }
        if (device == nullptr)
        {
            warnings.add("Insert " + juce::String(flSlot.insert) + ": " + (flSlot.vst.isNotEmpty() ? flSlot.vst : flSlot.internal) + " is not available");
            continue;
        }
        next.mixer[size_t(flSlot.insert)]->slots[size_t(flSlot.slot)] = std::move(device);
    }

    error = warnings.joinIntoString("\n");
    replace(next);
    project.dirty = true;
    return true;
}
}
