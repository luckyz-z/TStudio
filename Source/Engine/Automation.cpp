#include "Engine/Automation.h"
#include "Engine/Builtins/Pack.h"
#include "Engine/Builtins/Registry.h"
#include "Engine/Builtins/Sampler.h"

namespace ts
{
namespace
{
struct Failure
{
    juce::String message;
};

[[noreturn]] void fail(const juce::String& message)
{
    throw Failure { message };
}

juce::var property(const juce::var& arguments, const char* key)
{
    return arguments.getProperty(key, {});
}

bool has(const juce::var& arguments, const char* key)
{
    return arguments.getDynamicObject() != nullptr && arguments.getDynamicObject()->hasProperty(key);
}

int integer(const juce::var& arguments, const char* key)
{
    if (!has(arguments, key))
        fail(juce::String("missing argument '") + key + "'");
    return int(property(arguments, key));
}

juce::var schemaOf(std::initializer_list<std::pair<const char*, const char*>> properties, std::initializer_list<const char*> required)
{
    auto* props = new juce::DynamicObject();
    for (const auto& [name, type] : properties)
    {
        auto* entry = new juce::DynamicObject();
        const juce::String text(type);
        entry->setProperty("type", text.upToFirstOccurrenceOf(":", false, false));
        if (text.contains(":"))
            entry->setProperty("description", text.fromFirstOccurrenceOf(":", false, false));
        props->setProperty(name, juce::var(entry));
    }
    juce::Array<juce::var> requiredList;
    for (const auto* name : required)
        requiredList.add(juce::String(name));
    auto* schema = new juce::DynamicObject();
    schema->setProperty("type", "object");
    schema->setProperty("properties", juce::var(props));
    schema->setProperty("required", requiredList);
    return juce::var(schema);
}

juce::var noteToVar(const Note& note)
{
    auto result = object({ { "start", note.start }, { "length", note.length }, { "key", note.key }, { "velocity", note.velocity } });
    if (note.slide)
        result.getDynamicObject()->setProperty("slide", true);
    if (note.porta)
        result.getDynamicObject()->setProperty("porta", true);
    if (note.muted)
        result.getDynamicObject()->setProperty("muted", true);
    return result;
}

std::vector<Note> notesFrom(const juce::var& list)
{
    std::vector<Note> notes;
    if (const auto* array = list.getArray())
    {
        for (const auto& item : *array)
        {
            Note note;
            note.start = juce::jmax(0, int(item.getProperty("start", 0)));
            note.length = juce::jmax(1, int(item.getProperty("length", ticksPerStep)));
            note.key = juce::jlimit(0, 127, int(item.getProperty("key", defaultKey)));
            note.velocity = juce::jlimit(0.0f, 1.0f, float(double(item.getProperty("velocity", 0.78))));
            note.slide = bool(item.getProperty("slide", false));
            note.porta = bool(item.getProperty("porta", false));
            note.muted = bool(item.getProperty("muted", false));
            notes.push_back(note);
        }
    }
    return notes;
}
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* result = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        result->setProperty(name, value);
    return juce::var(result);
}

Automation::Automation(Studio& s) : studio(s)
{
    registerProject();
    registerChannels();
    registerNotes();
    registerArrangement();
    registerMixer();
    registerFiles();
}

void Automation::add(const juce::String& name, const juce::String& description, juce::var schema, Handler handler)
{
    table.push_back({ name, description, std::move(schema) });
    handlers[name] = std::move(handler);
}

juce::var Automation::call(const juce::String& method, const juce::var& arguments)
{
    const auto found = handlers.find(method);
    if (found == handlers.end())
        return object({ { "error", "unknown method '" + method + "'" } });
    try
    {
        auto result = found->second(arguments.isObject() ? arguments : object({}));
        return result.isVoid() ? object({ { "ok", true } }) : result;
    }
    catch (const Failure& failure)
    {
        return object({ { "error", failure.message } });
    }
}

Pattern& Automation::patternFor(const juce::var& arguments)
{
    auto& project = studio.project;
    const auto index = has(arguments, "pattern") ? int(property(arguments, "pattern")) : project.currentPattern.load();
    if (index < 0 || index >= int(project.patterns.size()))
        fail("pattern " + juce::String(index) + " does not exist");
    return project.patterns[size_t(index)];
}

Device* Automation::deviceFor(const juce::var& arguments)
{
    auto& project = studio.project;
    if (has(arguments, "channel"))
    {
        auto* channel = project.findChannel(int(property(arguments, "channel")));
        if (channel == nullptr || channel->device == nullptr)
            fail("channel not found");
        return channel->device.get();
    }
    const auto track = integer(arguments, "track");
    const auto slot = integer(arguments, "slot");
    if (track < 0 || track >= mixerTrackCount || slot < 0 || slot >= effectSlotCount)
        fail("mixer slot out of range");
    auto* device = project.mixer[size_t(track)]->slots[size_t(slot)].get();
    if (device == nullptr)
        fail("mixer slot is empty");
    return device;
}

void Automation::registerProject()
{
    add("get_project", "Returns tempo, transport state, channels, patterns and the playlist summary of the open project.", schemaOf({}, {}), [this](const juce::var&)
    {
        auto& project = studio.project;
        juce::Array<juce::var> channels, patterns, clips;
        for (const auto& channel : project.channels)
            channels.add(object({ { "id", channel->id }, { "name", channel->name }, { "type", channel->device != nullptr ? channel->device->getName() : juce::String() },
                                  { "volume", channel->volume.load() }, { "pan", channel->pan.load() }, { "muted", channel->muted.load() },
                                  { "insert", channel->insert.load() }, { "settings", channel->options.toVar() } }));
        for (int i = 0; i < int(project.patterns.size()); ++i)
            patterns.add(object({ { "index", i }, { "name", project.patterns[size_t(i)].name }, { "length", project.patterns[size_t(i)].length() } }));
        for (const auto& clip : project.clips)
            clips.add(clip.isAutomation() ? object({ { "track", clip.track }, { "automation", clip.automation }, { "start", clip.start }, { "length", clip.length } })
                                           : object({ { "track", clip.track }, { "pattern", clip.pattern }, { "start", clip.start }, { "length", clip.length } }));
        return object({ { "title", project.title }, { "tempo", project.tempo.load() }, { "swing", project.swing.load() }, { "song_mode", project.songMode.load() },
                        { "playing", studio.engine.isPlaying() }, { "position", studio.engine.getSongPosition() }, { "ppq", ppq },
                        { "selected_channel", project.selectedChannel }, { "current_pattern", project.currentPattern.load() }, { "channels", channels },
                        { "patterns", patterns }, { "clips", clips } });
    });
    add("set_tempo", "Sets the project tempo in BPM (10-522).", schemaOf({ { "bpm", "number" } }, { "bpm" }), [this](const juce::var& a)
    {
        studio.project.tempo.store(juce::jlimit(10.0, 522.0, double(property(a, "bpm"))));
        studio.project.touch();
        return juce::var();
    });
    add("set_swing", "Sets the global swing amount (0-1).", schemaOf({ { "amount", "number" } }, { "amount" }), [this](const juce::var& a)
    {
        studio.project.swing.store(juce::jlimit(0.0f, 1.0f, float(double(property(a, "amount")))));
        studio.project.touch();
        return juce::var();
    });
    add("transport", "Controls playback: action is play, stop or toggle.", schemaOf({ { "action", "string:play, stop or toggle" } }, { "action" }), [this](const juce::var& a)
    {
        const auto action = property(a, "action").toString();
        studio.engine.setPlaying(action == "play" || (action == "toggle" && !studio.engine.isPlaying()));
        return juce::var();
    });
    add("set_song_mode", "Switches between pattern mode (false) and song mode (true).", schemaOf({ { "enabled", "boolean" } }, { "enabled" }), [this](const juce::var& a)
    {
        studio.project.songMode.store(bool(property(a, "enabled")));
        studio.project.sendChangeMessage();
        return juce::var();
    });
}

void Automation::registerChannels()
{
    add("list_instruments", "Lists instruments you can pass to add_channel: built-in generators, drum pack sounds (pack:Name) and scanned plugins.", schemaOf({}, {}),
        [this](const juce::var&)
        {
            juce::Array<juce::var> result;
            for (const auto& name : Builtins::generators())
                result.add(name);
            for (const auto& sound : Pack::names())
                result.add("pack:" + sound);
            for (const auto& type : studio.plugins.list(true))
                result.add("plugin:" + type.name);
            return juce::var(result);
        });
    add("add_channel", "Adds a channel. type is a built-in instrument (e.g. 'Tomato Synth'), 'pack:Kick' for a drum sound, 'plugin:Name' or a path to an audio file.",
        schemaOf({ { "type", "string" }, { "name", "string" } }, { "type" }), [this](const juce::var& a)
        {
            const auto type = property(a, "type").toString();
            Channel* channel = nullptr;
            if (type.startsWith("pack:"))
            {
                if (Pack::get(type.substring(5)) == nullptr)
                    fail("unknown pack sound " + type);
                channel = &studio.addPackChannel(type.substring(5));
            }
            else if (type.startsWith("plugin:"))
            {
                for (const auto& description : studio.plugins.list(true))
                {
                    if (!description.name.equalsIgnoreCase(type.substring(7)))
                        continue;
                    juce::String error;
                    auto device = studio.createPlugin(description, error);
                    if (device == nullptr)
                        fail(error);
                    channel = &studio.addChannel(std::move(device), description.name);
                    break;
                }
            }
            else if (juce::File::isAbsolutePath(type) && Sampler::isAudioFile(juce::File(type)))
            {
                channel = studio.addSampleChannel(juce::File(type));
            }
            else if (auto device = studio.createBuiltin(type))
            {
                channel = &studio.addChannel(std::move(device), type);
            }
            if (channel == nullptr)
                fail("could not create channel of type " + type);
            if (has(a, "name"))
                channel->name = property(a, "name").toString();
            studio.project.touch();
            return object({ { "id", channel->id } });
        });
    add("remove_channel", "Deletes a channel and its notes.", schemaOf({ { "id", "integer" } }, { "id" }), [this](const juce::var& a)
    {
        studio.removeChannel(integer(a, "id"));
        return juce::var();
    });
    add("update_channel", "Changes a channel's name, volume (0-1), pan (-1..1), muted, mixer insert, and channel settings: time_shift (ticks 0-24), swing_mix (0-1), "
                          "cut_group / cut_by (0-16; a note on a channel with cut_group N stops channels whose cut_by is N), mono, porta, porta_time (ms), "
                          "arp (0 off, 1 up, 2 down, 3 up-down, 4 random), arp_time (0 = 1/32, 1 = 1/16, 2 = 1/8, 3 = 1/4), arp_range (octaves 1-4), arp_gate (0.05-1).",
        schemaOf({ { "id", "integer" }, { "name", "string" }, { "volume", "number" }, { "pan", "number" }, { "muted", "boolean" }, { "insert", "integer" },
                   { "time_shift", "number" }, { "swing_mix", "number" }, { "cut_group", "integer" }, { "cut_by", "integer" }, { "mono", "boolean" }, { "porta", "boolean" },
                   { "porta_time", "number" }, { "arp", "integer" }, { "arp_time", "integer" }, { "arp_range", "integer" }, { "arp_gate", "number" } },
                 { "id" }),
        [this](const juce::var& a)
        {
            auto* channel = studio.project.findChannel(integer(a, "id"));
            if (channel == nullptr)
                fail("channel not found");
            if (has(a, "name"))
                channel->name = property(a, "name").toString();
            if (has(a, "volume"))
                channel->volume.store(juce::jlimit(0.0f, 1.0f, float(double(property(a, "volume")))));
            if (has(a, "pan"))
                channel->pan.store(juce::jlimit(-1.0f, 1.0f, float(double(property(a, "pan")))));
            if (has(a, "muted"))
                channel->muted.store(bool(property(a, "muted")));
            if (has(a, "insert"))
                channel->insert.store(juce::jlimit(0, mixerTrackCount - 1, int(property(a, "insert"))));
            channel->options.apply(a);
            studio.project.touch();
            return juce::var();
        });
    add("select_channel", "Selects a channel (it receives keyboard/MIDI input).", schemaOf({ { "id", "integer" } }, { "id" }), [this](const juce::var& a)
    {
        studio.selectChannel(integer(a, "id"));
        return juce::var();
    });
    add("list_parameters", "Lists the parameters of a channel's instrument (channel) or a mixer effect (track + slot).",
        schemaOf({ { "channel", "integer" }, { "track", "integer" }, { "slot", "integer" } }, {}), [this](const juce::var& a)
        {
            juce::Array<juce::var> result;
            for (auto* parameter : deviceFor(a)->processor->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                    result.add(object({ { "id", ranged->getParameterID() }, { "name", ranged->getName(64) }, { "value", ranged->convertFrom0to1(ranged->getValue()) },
                                        { "min", ranged->getNormalisableRange().start }, { "max", ranged->getNormalisableRange().end }, { "text", ranged->getCurrentValueAsText() } }));
            return juce::var(result);
        });
    add("set_parameter", "Sets an instrument or effect parameter to a real value (as shown by list_parameters).",
        schemaOf({ { "channel", "integer" }, { "track", "integer" }, { "slot", "integer" }, { "id", "string" }, { "value", "number" } }, { "id", "value" }),
        [this](const juce::var& a)
        {
            const auto id = property(a, "id").toString();
            for (auto* parameter : deviceFor(a)->processor->getParameters())
            {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
                if (ranged == nullptr || ranged->getParameterID() != id)
                    continue;
                ranged->setValueNotifyingHost(ranged->convertTo0to1(float(double(property(a, "value")))));
                studio.project.dirty = true;
                return juce::var();
            }
            fail("parameter " + id + " not found");
        });
    add("list_presets", "Lists the presets (programs) of a channel's instrument (channel) or a mixer effect (track + slot).",
        schemaOf({ { "channel", "integer" }, { "track", "integer" }, { "slot", "integer" } }, {}), [this](const juce::var& a)
        {
            auto& processor = *deviceFor(a)->processor;
            juce::Array<juce::var> names;
            for (int i = 0; i < processor.getNumPrograms(); ++i)
                names.add(processor.getProgramName(i));
            return object({ { "current", processor.getCurrentProgram() }, { "presets", names } });
        });
    add("set_preset", "Loads a preset by index or by name (see list_presets).",
        schemaOf({ { "channel", "integer" }, { "track", "integer" }, { "slot", "integer" }, { "index", "integer" }, { "name", "string" } }, {}),
        [this](const juce::var& a)
        {
            auto& processor = *deviceFor(a)->processor;
            auto index = has(a, "index") ? int(property(a, "index")) : -1;
            for (int i = 0; index < 0 && has(a, "name") && i < processor.getNumPrograms(); ++i)
                if (processor.getProgramName(i).equalsIgnoreCase(property(a, "name").toString()))
                    index = i;
            if (index < 0 || index >= processor.getNumPrograms())
                fail("preset not found");
            processor.setCurrentProgram(index);
            studio.project.touch();
            return juce::var();
        });
}

void Automation::registerNotes()
{
    add("get_notes", "Returns the notes of a channel in a pattern (default: current pattern). Times are in ticks (96 per beat, 24 per step).",
        schemaOf({ { "channel", "integer" }, { "pattern", "integer" } }, { "channel" }), [this](const juce::var& a)
        {
            const auto& pattern = patternFor(a);
            juce::Array<juce::var> result;
            if (const auto found = pattern.notes.find(integer(a, "channel")); found != pattern.notes.end())
                for (const auto& note : found->second)
                    result.add(noteToVar(note));
            return juce::var(result);
        });
    const auto noteSchema = schemaOf({ { "channel", "integer" }, { "pattern", "integer" }, { "notes", "array:list of {start, length, key, velocity, slide, porta, muted}; ticks at 96 PPQ, key 60 = C5; slide glides the playing note to this key over this note's length" } },
                                     { "channel", "notes" });
    add("set_notes", "Replaces all notes of a channel in a pattern.", noteSchema, [this](const juce::var& a)
    {
        auto& pattern = patternFor(a);
        const auto channel = integer(a, "channel");
        if (studio.project.findChannel(channel) == nullptr)
            fail("channel not found");
        auto notes = notesFrom(property(a, "notes"));
        studio.project.checkpoint();
        {
            const juce::ScopedLock sl(studio.project.lock);
            pattern.notes[channel] = std::move(notes);
        }
        studio.project.touch();
        return juce::var();
    });
    add("add_notes", "Adds notes to a channel in a pattern.", noteSchema, [this](const juce::var& a)
    {
        auto& pattern = patternFor(a);
        const auto channel = integer(a, "channel");
        if (studio.project.findChannel(channel) == nullptr)
            fail("channel not found");
        const auto notes = notesFrom(property(a, "notes"));
        studio.project.checkpoint();
        {
            const juce::ScopedLock sl(studio.project.lock);
            auto& list = pattern.notes[channel];
            list.insert(list.end(), notes.begin(), notes.end());
        }
        studio.project.touch();
        return juce::var();
    });
    add("clear_notes", "Removes all notes of a channel in a pattern.", schemaOf({ { "channel", "integer" }, { "pattern", "integer" } }, { "channel" }),
        [this](const juce::var& a)
        {
            auto& pattern = patternFor(a);
            studio.project.checkpoint();
            {
                const juce::ScopedLock sl(studio.project.lock);
                pattern.notes.erase(integer(a, "channel"));
            }
            studio.project.touch();
            return juce::var();
        });
}

void Automation::registerArrangement()
{
    add("new_pattern", "Creates a pattern and makes it current. Returns its index.", schemaOf({ { "name", "string" } }, {}), [this](const juce::var& a)
    {
        const auto index = studio.addPattern();
        if (has(a, "name"))
            studio.project.patterns[size_t(index)].name = property(a, "name").toString();
        studio.project.touch();
        return object({ { "index", index } });
    });
    add("select_pattern", "Makes a pattern current (0-based index).", schemaOf({ { "index", "integer" } }, { "index" }), [this](const juce::var& a)
    {
        studio.selectPattern(integer(a, "index"));
        return juce::var();
    });
    add("rename_pattern", "Renames a pattern.", schemaOf({ { "index", "integer" }, { "name", "string" } }, { "index", "name" }), [this](const juce::var& a)
    {
        const auto index = integer(a, "index");
        if (index < 0 || index >= int(studio.project.patterns.size()))
            fail("pattern not found");
        studio.project.patterns[size_t(index)].name = property(a, "name").toString();
        studio.project.touch();
        return juce::var();
    });
    add("add_clip", "Places a pattern on the playlist. start_bar is 0-based; length_bars defaults to the pattern length.",
        schemaOf({ { "pattern", "integer" }, { "track", "integer" }, { "start_bar", "number" }, { "length_bars", "number" } }, { "pattern", "track", "start_bar" }),
        [this](const juce::var& a)
        {
            const auto pattern = integer(a, "pattern");
            if (pattern < 0 || pattern >= int(studio.project.patterns.size()))
                fail("pattern not found");
            Clip clip;
            clip.pattern = pattern;
            clip.track = juce::jlimit(0, playlistTrackCount - 1, integer(a, "track"));
            clip.start = juce::jmax(0, juce::roundToInt(double(property(a, "start_bar")) * ticksPerBar));
            clip.length = has(a, "length_bars") ? juce::jmax(1, juce::roundToInt(double(property(a, "length_bars")) * ticksPerBar))
                                                : studio.project.patterns[size_t(pattern)].length();
            studio.project.checkpoint();
            {
                const juce::ScopedLock sl(studio.project.lock);
                studio.project.clips.push_back(clip);
            }
            studio.project.touch();
            return juce::var();
        });
    add("clear_playlist", "Removes every clip from the playlist.", schemaOf({}, {}), [this](const juce::var&)
    {
        studio.project.checkpoint();
        {
            const juce::ScopedLock sl(studio.project.lock);
            studio.project.clips.clear();
        }
        studio.project.touch();
        return juce::var();
    });
}

void Automation::registerMixer()
{
    add("get_mixer", "Returns every mixer track that is in use (or all with all=true): name, volume, pan, stereo width, phase, swap, mute, solo, sends and effects.",
        schemaOf({ { "all", "boolean" } }, {}), [this](const juce::var& a)
        {
            juce::Array<juce::var> result;
            const juce::ScopedLock sl(studio.project.lock);
            for (int i = 0; i < mixerTrackCount; ++i)
            {
                const auto& track = *studio.project.mixer[size_t(i)];
                juce::Array<juce::var> sends, effects;
                for (const auto& [target, send] : track.sends)
                    sends.add(object({ { "to", target }, { "level", send.level }, { "sidechain", send.sidechain } }));
                for (int s = 0; s < effectSlotCount; ++s)
                    if (const auto& slot = track.slots[size_t(s)])
                        effects.add(object({ { "slot", s }, { "name", slot->getName() }, { "bypassed", slot->bypassed.load() }, { "mix", slot->mix.load() } }));
                const auto routedElsewhere = track.sends.size() != 1 || track.sends.count(0) == 0 || track.sends.at(0).level != 1.0f;
                const auto used = i == 0 || !effects.isEmpty() || routedElsewhere || track.name != Project::defaultInsertName(i)
                                  || std::any_of(studio.project.channels.begin(), studio.project.channels.end(), [i](const auto& c) { return c->insert.load() == i; });
                if (!used && !bool(property(a, "all")))
                    continue;
                result.add(object({ { "track", i }, { "name", track.name }, { "volume", track.volume.load() }, { "pan", track.pan.load() },
                                    { "width", track.width.load() }, { "invert_phase", track.invertPhase.load() }, { "swap", track.swapSides.load() },
                                    { "muted", track.muted.load() }, { "solo", track.solo.load() }, { "sends", sends }, { "effects", effects } }));
            }
            return juce::var(result);
        });
    add("route_mixer_track", "Routes mixer track 'from' into track 'to' (0 = master) at a send level (0-2, default 1). sidechain=true feeds only the "
                             "destination's sidechain input (for Tomato Compressor/Pump with Sidechain mode). only=true removes the other audio routes. "
                             "remove=true deletes the route. Routing loops are refused.",
        schemaOf({ { "from", "integer" }, { "to", "integer" }, { "level", "number" }, { "sidechain", "boolean" }, { "only", "boolean" }, { "remove", "boolean" } },
                 { "from", "to" }),
        [this](const juce::var& a)
        {
            const auto from = integer(a, "from");
            const auto to = integer(a, "to");
            auto& project = studio.project;
            if (bool(property(a, "remove")))
                project.unroute(from, to);
            else if (bool(property(a, "only")))
            {
                if (!project.canRoute(from, to))
                    fail("that route would create a loop or is out of range");
                project.routeOnly(from, to);
            }
            else if (!project.route(from, to, has(a, "level") ? float(double(property(a, "level"))) : 1.0f, bool(property(a, "sidechain"))))
                fail("that route would create a loop or is out of range");
            project.touch();
            return juce::var();
        });
    add("set_mixer_track", "Changes a mixer track (0 = master): volume (linear gain, 1 = 0 dB), pan, width (stereo separation: 0 mono, 1 normal, 2 wide), "
                           "invert_phase, swap (left/right), muted, solo, name.",
        schemaOf({ { "track", "integer" }, { "volume", "number" }, { "pan", "number" }, { "width", "number" }, { "invert_phase", "boolean" }, { "swap", "boolean" },
                   { "muted", "boolean" }, { "solo", "boolean" }, { "name", "string" } }, { "track" }),
        [this](const juce::var& a)
        {
            const auto index = integer(a, "track");
            if (index < 0 || index >= mixerTrackCount)
                fail("mixer track out of range");
            auto& track = *studio.project.mixer[size_t(index)];
            if (has(a, "volume"))
                track.volume.store(juce::jlimit(0.0f, 1.9f, float(double(property(a, "volume")))));
            if (has(a, "pan"))
                track.pan.store(juce::jlimit(-1.0f, 1.0f, float(double(property(a, "pan")))));
            if (has(a, "muted"))
                track.muted.store(bool(property(a, "muted")));
            if (has(a, "solo"))
                track.solo.store(bool(property(a, "solo")));
            if (has(a, "width"))
                track.width.store(juce::jlimit(0.0f, 2.0f, float(double(property(a, "width")))));
            if (has(a, "invert_phase"))
                track.invertPhase.store(bool(property(a, "invert_phase")));
            if (has(a, "swap"))
                track.swapSides.store(bool(property(a, "swap")));
            if (has(a, "name"))
                track.name = property(a, "name").toString();
            studio.project.touch();
            return juce::var();
        });
    add("create_automation", "Creates an automation clip and places it on the playlist. Target: kind = parameter (with channel, or track + slot, and parameter id "
                             "from list_parameters), channel_volume / channel_pan (channel), mixer_volume / mixer_pan (track), tempo (60-200 BPM) or main_volume. "
                             "points = [{tick, value}] with value 0-1 (normalised) and ticks relative to the clip start; start_bar, length_bars and playlist_track are optional. "
                             "Automation plays in song mode.",
        schemaOf({ { "kind", "string" }, { "channel", "integer" }, { "track", "integer" }, { "slot", "integer" }, { "parameter", "string" },
                   { "points", "array:list of {tick, value 0-1, tension -1..1}" }, { "start_bar", "number" }, { "length_bars", "number" }, { "playlist_track", "integer" },
                   { "name", "string" } },
                 { "kind" }),
        [this](const juce::var& a)
        {
            static const std::map<juce::String, AutomationTarget::Kind> kinds {
                { "parameter", AutomationTarget::Kind::parameter }, { "channel_volume", AutomationTarget::Kind::channelVolume },
                { "channel_pan", AutomationTarget::Kind::channelPan }, { "mixer_volume", AutomationTarget::Kind::mixerVolume },
                { "mixer_pan", AutomationTarget::Kind::mixerPan }, { "tempo", AutomationTarget::Kind::tempo }, { "main_volume", AutomationTarget::Kind::mainVolume },
            };
            const auto found = kinds.find(property(a, "kind").toString());
            if (found == kinds.end())
                fail("unknown kind");
            AutomationTarget target;
            target.kind = found->second;
            target.channel = has(a, "channel") ? integer(a, "channel") : -1;
            target.track = has(a, "track") ? integer(a, "track") : -1;
            target.slot = has(a, "slot") ? integer(a, "slot") : -1;
            target.parameter = property(a, "parameter").toString();
            const auto needsChannel = target.kind == AutomationTarget::Kind::channelVolume || target.kind == AutomationTarget::Kind::channelPan;
            const auto needsTrack = target.kind == AutomationTarget::Kind::mixerVolume || target.kind == AutomationTarget::Kind::mixerPan;
            if (needsChannel && studio.project.findChannel(target.channel) == nullptr)
                fail("channel not found");
            if (needsTrack && (target.track < 0 || target.track >= mixerTrackCount))
                fail("mixer track out of range");
            if (target.kind == AutomationTarget::Kind::parameter)
            {
                auto* device = deviceFor(a);
                const auto& parameters = device->processor->getParameters();
                if (std::none_of(parameters.begin(), parameters.end(), [&target](auto* p) { return AutomationTarget::idOf(*p) == target.parameter; }))
                    fail("parameter " + target.parameter + " not found");
            }
            const auto start = has(a, "start_bar") ? juce::roundToInt(double(property(a, "start_bar")) * ticksPerBar) : 0;
            const auto length = has(a, "length_bars") ? juce::roundToInt(double(property(a, "length_bars")) * ticksPerBar) : 0;
            const auto name = has(a, "name") ? property(a, "name").toString() : studio.describe(target);
            const auto index = studio.addAutomation(target, name, 0.5f, start, length, has(a, "playlist_track") ? integer(a, "playlist_track") : -1);
            if (const auto* list = property(a, "points").getArray(); list != nullptr && !list->isEmpty())
            {
                std::vector<AutomationPoint> points;
                for (const auto& item : *list)
                    points.push_back({ juce::jmax(0, int(item.getProperty("tick", 0))), juce::jlimit(0.0f, 1.0f, float(double(item.getProperty("value", 0.5)))),
                                       juce::jlimit(-1.0f, 1.0f, float(double(item.getProperty("tension", 0.0)))) });
                std::sort(points.begin(), points.end(), [](const auto& x, const auto& y) { return x.tick < y.tick; });
                const juce::ScopedLock sl(studio.project.lock);
                studio.project.automations[size_t(index)].points = std::move(points);
            }
            studio.project.touch();
            return object({ { "index", index } });
        });
    add("list_automations", "Lists automation clips: index, name, target and points.", schemaOf({}, {}), [this](const juce::var&)
    {
        juce::Array<juce::var> result;
        for (int i = 0; i < int(studio.project.automations.size()); ++i)
        {
            const auto& curve = studio.project.automations[size_t(i)];
            juce::Array<juce::var> points;
            for (const auto& point : curve.points)
                points.add(object({ { "tick", point.tick }, { "value", point.value }, { "tension", point.tension } }));
            result.add(object({ { "index", i }, { "name", curve.name }, { "kind", int(curve.target.kind) }, { "channel", curve.target.channel },
                                { "track", curve.target.track }, { "slot", curve.target.slot }, { "parameter", curve.target.parameter }, { "length", curve.length },
                                { "points", points } }));
        }
        return juce::var(result);
    });
    add("set_automation_points", "Replaces the points of an automation clip: points = [{tick, value 0-1, tension}].",
        schemaOf({ { "index", "integer" }, { "points", "array" } }, { "index", "points" }), [this](const juce::var& a)
        {
            const auto index = integer(a, "index");
            if (index < 0 || index >= int(studio.project.automations.size()))
                fail("automation clip not found");
            std::vector<AutomationPoint> points;
            if (const auto* list = property(a, "points").getArray())
                for (const auto& item : *list)
                    points.push_back({ juce::jmax(0, int(item.getProperty("tick", 0))), juce::jlimit(0.0f, 1.0f, float(double(item.getProperty("value", 0.5)))),
                                       juce::jlimit(-1.0f, 1.0f, float(double(item.getProperty("tension", 0.0)))) });
            std::sort(points.begin(), points.end(), [](const auto& x, const auto& y) { return x.tick < y.tick; });
            studio.project.checkpoint();
            {
                const juce::ScopedLock sl(studio.project.lock);
                studio.project.automations[size_t(index)].points = std::move(points);
            }
            studio.project.touch();
            return juce::var();
        });
    add("set_loop", "Loops a section of the song in song mode (start_bar inclusive, end_bar exclusive, 0-based). Omit both to clear the loop. Rendering ignores the loop.",
        schemaOf({ { "start_bar", "number" }, { "end_bar", "number" } }, {}), [this](const juce::var& a)
        {
            if (!has(a, "start_bar") || !has(a, "end_bar"))
            {
                studio.project.loopStart.store(-1);
                studio.project.loopEnd.store(-1);
            }
            else
            {
                const auto start = juce::jmax(0, juce::roundToInt(double(property(a, "start_bar")) * ticksPerBar));
                const auto end = juce::roundToInt(double(property(a, "end_bar")) * ticksPerBar);
                if (end <= start)
                    fail("end_bar must be after start_bar");
                studio.project.loopStart.store(start);
                studio.project.loopEnd.store(end);
            }
            studio.project.touch();
            return juce::var();
        });
    add("list_effects", "Lists effect names you can pass to add_effect.", schemaOf({}, {}), [this](const juce::var&)
    {
        juce::Array<juce::var> result;
        for (const auto& name : Builtins::effects())
            result.add(name);
        for (const auto& type : studio.plugins.list(false))
            result.add("plugin:" + type.name);
        return juce::var(result);
    });
    add("add_effect", "Loads an effect into a mixer slot (track 0 = master, slot 0-9). Use an empty name to clear the slot.",
        schemaOf({ { "track", "integer" }, { "slot", "integer" }, { "name", "string" } }, { "track", "slot", "name" }), [this](const juce::var& a)
        {
            const auto track = integer(a, "track");
            const auto slot = integer(a, "slot");
            if (track < 0 || track >= mixerTrackCount || slot < 0 || slot >= effectSlotCount)
                fail("mixer slot out of range");
            const auto name = property(a, "name").toString();
            std::unique_ptr<Device> device;
            if (name.startsWith("plugin:"))
            {
                for (const auto& description : studio.plugins.list(false))
                {
                    if (!description.name.equalsIgnoreCase(name.substring(7)))
                        continue;
                    juce::String error;
                    device = studio.createPlugin(description, error);
                    if (device == nullptr)
                        fail(error);
                }
            }
            else if (name.isNotEmpty())
            {
                device = studio.createBuiltin(name);
            }
            if (name.isNotEmpty() && device == nullptr)
                fail("unknown effect " + name);
            studio.setSlot(track, slot, std::move(device));
            return juce::var();
        });
}

void Automation::registerFiles()
{
    add("render", "Renders the song (song mode) to a WAV, FLAC, OGG or MP3 (320 kbps) file. Blocks until finished.", schemaOf({ { "path", "string:output path (.wav, .flac, .ogg or .mp3)" } }, { "path" }),
        [this](const juce::var& a)
        {
            juce::String error;
            if (!studio.renderNow(juce::File::getCurrentWorkingDirectory().getChildFile(property(a, "path").toString()), error))
                fail(error);
            return juce::var();
        });
    add("save_project", "Saves the project as a .tsp file.", schemaOf({ { "path", "string:absolute path ending in .tsp" } }, { "path" }), [this](const juce::var& a)
    {
        juce::String error;
        if (!studio.save(juce::File::getCurrentWorkingDirectory().getChildFile(property(a, "path").toString()), error))
            fail(error);
        return juce::var();
    });
    add("open_project", "Opens a .tsp project or imports an FL Studio .flp project.", schemaOf({ { "path", "string" } }, { "path" }), [this](const juce::var& a)
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(property(a, "path").toString());
        juce::String error;
        const auto ok = file.hasFileExtension("flp") ? studio.importFlp(file, error) : studio.load(file, error);
        if (!ok)
            fail(error);
        return object({ { "warnings", error } });
    });
    add("export_midi", "Exports the current pattern (or one channel) as a MIDI file.", schemaOf({ { "path", "string" }, { "channel", "integer" } }, { "path" }),
        [this](const juce::var& a)
        {
            juce::String error;
            if (!studio.exportMidi(juce::File::getCurrentWorkingDirectory().getChildFile(property(a, "path").toString()), has(a, "channel") ? int(property(a, "channel")) : -1, error))
                fail(error);
            return juce::var();
        });
    add("import_midi", "Imports a MIDI file into a channel of the current pattern.", schemaOf({ { "path", "string" }, { "channel", "integer" } }, { "path", "channel" }),
        [this](const juce::var& a)
        {
            juce::String error;
            if (!studio.importMidi(juce::File::getCurrentWorkingDirectory().getChildFile(property(a, "path").toString()), integer(a, "channel"), error))
                fail(error);
            return juce::var();
        });
}
}
