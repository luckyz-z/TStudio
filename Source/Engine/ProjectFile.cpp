#include "Engine/Studio.h"

namespace ts
{
namespace
{
constexpr int formatVersion = 1;

juce::String encodeNotes(const std::vector<Note>& notes)
{
    juce::StringArray parts;
    for (const auto& note : notes)
    {
        const auto flags = (note.slide ? 1 : 0) | (note.porta ? 2 : 0) | (note.muted ? 4 : 0);
        parts.add(juce::String(note.start) + "," + juce::String(note.length) + "," + juce::String(note.key) + "," + juce::String(note.velocity, 3)
                  + (flags != 0 ? "," + juce::String(flags) : juce::String()));
    }
    return parts.joinIntoString(" ");
}

std::vector<Note> decodeNotes(const juce::String& text)
{
    std::vector<Note> notes;
    for (const auto& part : juce::StringArray::fromTokens(text, " ", {}))
    {
        const auto fields = juce::StringArray::fromTokens(part, ",", {});
        if (fields.size() < 4)
            continue;
        const auto flags = fields.size() > 4 ? fields[4].getIntValue() : 0;
        notes.push_back({ fields[0].getIntValue(), juce::jmax(1, fields[1].getIntValue()), juce::jlimit(0, 131, fields[2].getIntValue()),
                          juce::jlimit(0.0f, 1.0f, fields[3].getFloatValue()), (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0 });
    }
    return notes;
}
}

std::unique_ptr<juce::XmlElement> Studio::serialise(const Device& device)
{
    auto xml = std::make_unique<juce::XmlElement>("Device");
    if (device.isPlugin())
        xml->addChildElement(device.description.createXml().release());
    else
        xml->setAttribute("builtin", device.builtin);
    xml->setAttribute("bypassed", device.bypassed.load());
    xml->setAttribute("mix", device.mix.load());
    juce::MemoryBlock state;
    device.processor->getStateInformation(state);
    xml->setAttribute("state", juce::Base64::toBase64(state.getData(), state.getSize()));
    return xml;
}

std::unique_ptr<Device> Studio::deserialiseDevice(const juce::XmlElement& xml, juce::String& error)
{
    std::unique_ptr<Device> device;
    if (xml.hasAttribute("builtin"))
    {
        device = createBuiltin(xml.getStringAttribute("builtin"));
        if (device == nullptr)
            error << "Unknown built-in plugin: " << xml.getStringAttribute("builtin") << "\n";
    }
    else if (const auto* plugin = xml.getChildByName("PLUGIN"))
    {
        juce::PluginDescription description;
        description.loadFromXml(*plugin);
        juce::String reason;
        device = createPlugin(description, reason);
        if (device == nullptr)
            error << "Missing plugin: " << description.name << (reason.isNotEmpty() ? " (" + reason + ")" : juce::String()) << "\n";
    }
    if (device == nullptr)
        return nullptr;

    juce::MemoryOutputStream state;
    if (juce::Base64::convertFromBase64(state, xml.getStringAttribute("state")) && state.getDataSize() > 0)
        device->processor->setStateInformation(state.getData(), int(state.getDataSize()));
    device->bypassed.store(xml.getBoolAttribute("bypassed"));
    device->mix.store(float(xml.getDoubleAttribute("mix", 1.0)));
    return device;
}

std::unique_ptr<juce::XmlElement> Studio::serialise()
{
    auto root = std::make_unique<juce::XmlElement>("TStudio");
    root->setAttribute("version", formatVersion);
    root->setAttribute("title", project.title);
    root->setAttribute("tempo", project.tempo.load());
    root->setAttribute("swing", project.swing.load());
    root->setAttribute("mainVolume", project.mainVolume.load());
    root->setAttribute("currentPattern", project.currentPattern.load());
    root->setAttribute("songMode", project.songMode.load());
    root->setAttribute("loopStart", project.loopStart.load());
    root->setAttribute("mainPitch", project.mainPitch.load());
    root->setAttribute("loopEnd", project.loopEnd.load());
    root->setAttribute("nextChannelId", project.nextChannelId);

    auto* channels = root->createNewChildElement("Channels");
    for (const auto& channel : project.channels)
    {
        auto* xml = channels->createNewChildElement("Channel");
        xml->setAttribute("id", channel->id);
        xml->setAttribute("name", channel->name);
        xml->setAttribute("colour", channel->colour.toString());
        xml->setAttribute("volume", channel->volume.load());
        xml->setAttribute("pan", channel->pan.load());
        xml->setAttribute("muted", channel->muted.load());
        xml->setAttribute("insert", channel->insert.load());
        channel->options.save(*xml);
        if (channel->device != nullptr)
            xml->addChildElement(serialise(*channel->device).release());
    }

    auto* patterns = root->createNewChildElement("Patterns");
    for (const auto& pattern : project.patterns)
    {
        auto* xml = patterns->createNewChildElement("Pattern");
        xml->setAttribute("name", pattern.name);
        xml->setAttribute("colour", pattern.colour.toString());
        for (const auto& [id, notes] : pattern.notes)
        {
            if (notes.empty())
                continue;
            auto* list = xml->createNewChildElement("Notes");
            list->setAttribute("channel", id);
            list->addTextElement(encodeNotes(notes));
        }
    }

    auto* automations = root->createNewChildElement("Automations");
    for (const auto& curve : project.automations)
    {
        auto* xml = automations->createNewChildElement("Automation");
        xml->setAttribute("name", curve.name);
        xml->setAttribute("colour", curve.colour.toString());
        xml->setAttribute("kind", int(curve.target.kind));
        xml->setAttribute("channel", curve.target.channel);
        xml->setAttribute("track", curve.target.track);
        xml->setAttribute("slot", curve.target.slot);
        xml->setAttribute("parameter", curve.target.parameter);
        xml->setAttribute("length", curve.length);
        for (const auto& point : curve.points)
        {
            auto* pointXml = xml->createNewChildElement("Point");
            pointXml->setAttribute("tick", point.tick);
            pointXml->setAttribute("value", point.value);
            if (point.tension != 0.0f)
                pointXml->setAttribute("tension", point.tension);
        }
    }

    auto* playlist = root->createNewChildElement("Playlist");
    for (const auto& clip : project.clips)
    {
        auto* xml = playlist->createNewChildElement("Clip");
        xml->setAttribute("track", clip.track);
        xml->setAttribute("start", clip.start);
        xml->setAttribute("length", clip.length);
        xml->setAttribute("pattern", clip.pattern);
        xml->setAttribute("offset", clip.offset);
        xml->setAttribute("muted", clip.muted);
        if (clip.isAutomation())
            xml->setAttribute("automation", clip.automation);
    }
    for (int i = 0; i < playlistTrackCount; ++i)
    {
        const auto& track = project.tracks[size_t(i)];
        if (track.name == Project::defaultTrackName(i) && !track.muted)
            continue;
        auto* xml = playlist->createNewChildElement("Track");
        xml->setAttribute("index", i);
        xml->setAttribute("name", track.name);
        xml->setAttribute("muted", track.muted);
    }

    auto* mixer = root->createNewChildElement("Mixer");
    for (int i = 0; i < mixerTrackCount; ++i)
    {
        const auto& track = *project.mixer[size_t(i)];
        auto* xml = mixer->createNewChildElement("Track");
        xml->setAttribute("index", i);
        xml->setAttribute("name", track.name);
        xml->setAttribute("volume", track.volume.load());
        xml->setAttribute("pan", track.pan.load());
        xml->setAttribute("muted", track.muted.load());
        xml->setAttribute("solo", track.solo.load());
        xml->setAttribute("width", track.width.load());
        xml->setAttribute("invertPhase", track.invertPhase.load());
        xml->setAttribute("swapSides", track.swapSides.load());
        if (i > 0)
            for (const auto& [target, send] : track.sends)
            {
                auto* sendXml = xml->createNewChildElement("Send");
                sendXml->setAttribute("to", target);
                sendXml->setAttribute("level", send.level);
                sendXml->setAttribute("sidechain", send.sidechain);
            }
        for (int s = 0; s < effectSlotCount; ++s)
        {
            if (const auto& slot = track.slots[size_t(s)])
            {
                auto* slotXml = xml->createNewChildElement("Slot");
                slotXml->setAttribute("index", s);
                slotXml->addChildElement(serialise(*slot).release());
            }
        }
    }
    return root;
}

bool Studio::deserialise(const juce::XmlElement& root, Project& target, juce::String& error)
{
    if (!root.hasTagName("TStudio") && !root.hasTagName("LFStudio"))
    {
        error = "This is not an TStudio project.";
        return false;
    }

    target.title = root.getStringAttribute("title");
    target.tempo.store(juce::jlimit(10.0, 522.0, root.getDoubleAttribute("tempo", AudioEngine::defaultTempo)));
    target.swing.store(float(root.getDoubleAttribute("swing")));
    target.mainVolume.store(float(root.getDoubleAttribute("mainVolume", 0.8)));
    target.songMode.store(root.getBoolAttribute("songMode"));
    target.loopStart.store(root.getIntAttribute("loopStart", -1));
    target.mainPitch.store(juce::jlimit(-12, 12, root.getIntAttribute("mainPitch")));
    target.loopEnd.store(root.getIntAttribute("loopEnd", -1));
    target.nextChannelId = root.getIntAttribute("nextChannelId", 1);

    if (const auto* channels = root.getChildByName("Channels"))
    {
        for (const auto* xml : channels->getChildIterator())
        {
            auto channel = std::make_unique<Channel>();
            channel->id = xml->getIntAttribute("id");
            channel->name = xml->getStringAttribute("name");
            channel->colour = juce::Colour::fromString(xml->getStringAttribute("colour", "ff5f9e78"));
            channel->volume.store(float(xml->getDoubleAttribute("volume", defaultChannelVolume)));
            channel->pan.store(float(xml->getDoubleAttribute("pan")));
            channel->muted.store(xml->getBoolAttribute("muted"));
            channel->insert.store(juce::jlimit(0, mixerTrackCount - 1, xml->getIntAttribute("insert")));
            channel->options.load(*xml);
            if (const auto* device = xml->getChildByName("Device"))
                channel->device = deserialiseDevice(*device, error);
            target.nextChannelId = juce::jmax(target.nextChannelId, channel->id + 1);
            target.channels.push_back(std::move(channel));
        }
    }

    if (const auto* patterns = root.getChildByName("Patterns"))
    {
        target.patterns.clear();
        for (const auto* xml : patterns->getChildIterator())
        {
            Pattern pattern { xml->getStringAttribute("name"), juce::Colour::fromString(xml->getStringAttribute("colour", "ff5f9e78")), {} };
            for (const auto* list : xml->getChildWithTagNameIterator("Notes"))
                pattern.notes[list->getIntAttribute("channel")] = decodeNotes(list->getAllSubText());
            target.patterns.push_back(std::move(pattern));
        }
        if (target.patterns.empty())
            target.patterns.push_back({ "Pattern 1", target.nextPatternColour(), {} });
    }
    target.currentPattern.store(juce::jlimit(0, int(target.patterns.size()) - 1, root.getIntAttribute("currentPattern")));

    if (const auto* automations = root.getChildByName("Automations"))
    {
        for (const auto* xml : automations->getChildWithTagNameIterator("Automation"))
        {
            AutomationClip curve;
            curve.name = xml->getStringAttribute("name", "Automation");
            curve.colour = juce::Colour::fromString(xml->getStringAttribute("colour", "ff8a9aa6"));
            curve.target.kind = AutomationTarget::Kind(juce::jlimit(0, int(AutomationTarget::Kind::mainVolume), xml->getIntAttribute("kind")));
            curve.target.channel = xml->getIntAttribute("channel", -1);
            curve.target.track = xml->getIntAttribute("track", -1);
            curve.target.slot = xml->getIntAttribute("slot", -1);
            curve.target.parameter = xml->getStringAttribute("parameter");
            curve.length = juce::jmax(ticksPerStep, xml->getIntAttribute("length", ticksPerBar * 4));
            for (const auto* pointXml : xml->getChildWithTagNameIterator("Point"))
                curve.points.push_back({ juce::jmax(0, pointXml->getIntAttribute("tick")), juce::jlimit(0.0f, 1.0f, float(pointXml->getDoubleAttribute("value"))),
                                         juce::jlimit(-1.0f, 1.0f, float(pointXml->getDoubleAttribute("tension"))) });
            std::sort(curve.points.begin(), curve.points.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
            target.automations.push_back(std::move(curve));
        }
    }

    if (const auto* playlist = root.getChildByName("Playlist"))
    {
        for (const auto* xml : playlist->getChildWithTagNameIterator("Clip"))
        {
            Clip clip;
            clip.track = juce::jlimit(0, playlistTrackCount - 1, xml->getIntAttribute("track"));
            clip.start = juce::jmax(0, xml->getIntAttribute("start"));
            clip.length = juce::jmax(1, xml->getIntAttribute("length", ticksPerBar));
            clip.pattern = xml->getIntAttribute("pattern");
            clip.offset = juce::jmax(0, xml->getIntAttribute("offset"));
            clip.muted = xml->getBoolAttribute("muted");
            clip.automation = xml->getIntAttribute("automation", -1);
            if (clip.isAutomation() ? clip.automation < int(target.automations.size()) : clip.pattern >= 0 && clip.pattern < int(target.patterns.size()))
                target.clips.push_back(clip);
        }
        for (const auto* xml : playlist->getChildWithTagNameIterator("Track"))
        {
            const auto index = xml->getIntAttribute("index", -1);
            if (index < 0 || index >= playlistTrackCount)
                continue;
            target.tracks[size_t(index)].name = xml->getStringAttribute("name", Project::defaultTrackName(index));
            target.tracks[size_t(index)].muted = xml->getBoolAttribute("muted");
        }
    }

    if (const auto* mixer = root.getChildByName("Mixer"))
    {
        for (const auto* xml : mixer->getChildWithTagNameIterator("Track"))
        {
            const auto index = xml->getIntAttribute("index", -1);
            if (index < 0 || index >= mixerTrackCount)
                continue;
            auto& track = *target.mixer[size_t(index)];
            track.name = xml->getStringAttribute("name", Project::defaultInsertName(index));
            track.volume.store(float(xml->getDoubleAttribute("volume", 1.0)));
            track.pan.store(float(xml->getDoubleAttribute("pan")));
            track.muted.store(xml->getBoolAttribute("muted"));
            track.solo.store(xml->getBoolAttribute("solo"));
            track.width.store(float(xml->getDoubleAttribute("width", 1.0)));
            track.invertPhase.store(xml->getBoolAttribute("invertPhase"));
            track.swapSides.store(xml->getBoolAttribute("swapSides"));
            if (index > 0 && xml->getChildByName("Send") != nullptr)
            {
                track.sends.clear();
                for (const auto* sendXml : xml->getChildWithTagNameIterator("Send"))
                {
                    const auto to = sendXml->getIntAttribute("to", -1);
                    if (to >= 0 && to < mixerTrackCount && to != index)
                        track.sends[to] = { float(sendXml->getDoubleAttribute("level", 1.0)), sendXml->getBoolAttribute("sidechain") };
                }
            }
            for (const auto* slotXml : xml->getChildWithTagNameIterator("Slot"))
            {
                const auto slot = slotXml->getIntAttribute("index", -1);
                if (slot >= 0 && slot < effectSlotCount)
                    if (const auto* device = slotXml->getChildByName("Device"))
                        track.slots[size_t(slot)] = deserialiseDevice(*device, error);
            }
        }
    }
    return true;
}

bool Studio::save(const juce::File& file, juce::String& error)
{
    const auto xml = serialise();
    if (!xml->writeTo(file))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    project.file = file;
    project.dirty = false;
    rememberRecent(file);
    project.sendChangeMessage();
    return true;
}

bool Studio::load(const juce::File& file, juce::String& error)
{
    if (!loadXml(file.loadFileAsString(), error))
    {
        if (error.isEmpty())
            error = "Could not read " + file.getFullPathName();
        return false;
    }
    project.file = file;
    rememberRecent(file);
    project.sendChangeMessage();
    return true;
}

bool Studio::loadXml(const juce::String& text, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse(text);
    if (xml == nullptr)
        return false;
    Project next;
    if (!deserialise(*xml, next, error))
        return false;
    replace(next);
    return true;
}
}
