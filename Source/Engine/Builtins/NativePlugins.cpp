#include "Engine/Builtins/NativePlugins.h"
#include "Engine/Builtins/BuiltinProcessor.h"
#include "Engine/Builtins/Displays.h"

#include <tstudio_plugin.h>

namespace ts::NativePlugins
{
namespace
{
struct Library
{
    std::unique_ptr<juce::DynamicLibrary> handle;
    juce::File file;
};

struct Entry
{
    const tstudio_plugin* plugin;
    juce::String source;
};

std::vector<Library> libraries;
std::vector<Entry> entries;
juce::StringArray problems;
bool scanned = false;

bool valid(const tstudio_plugin* plugin)
{
    return plugin != nullptr && plugin->api_version == TSTUDIO_PLUGIN_API_VERSION && plugin->name != nullptr && plugin->create != nullptr
           && plugin->destroy != nullptr && plugin->process != nullptr && plugin->num_params >= 0 && (plugin->num_params == 0 || plugin->params != nullptr);
}

void load(const juce::File& file)
{
    auto handle = std::make_unique<juce::DynamicLibrary>();
    if (!handle->open(file.getFullPathName()))
    {
        problems.add(file.getFileName() + ": could not be loaded");
        return;
    }
    const auto entry = reinterpret_cast<tstudio_entry_fn>(handle->getFunction("tstudio_plugins"));
    if (entry == nullptr)
    {
        problems.add(file.getFileName() + ": does not export tstudio_plugins()");
        return;
    }
    auto count = 0;
    const auto* list = entry(&count);
    for (int i = 0; list != nullptr && i < count; ++i)
    {
        if (!valid(list[i]))
        {
            problems.add(file.getFileName() + ": plugin " + juce::String(i) + " is invalid or built for another API version");
            continue;
        }
        const auto duplicate = std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return juce::String(e.plugin->name) == list[i]->name; });
        if (!duplicate)
            entries.push_back({ list[i], file.getFileName() });
    }
    libraries.push_back({ std::move(handle), file });
}

void scan()
{
    if (scanned)
        return;
    scanned = true;
    const auto appPlugins = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getChildFile("Plugins");
    for (const auto& folder : { userFolder(), appPlugins })
        if (folder.isDirectory())
            for (const auto& item : juce::RangedDirectoryIterator(folder, false, "*.dll;*.so;*.dylib"))
                load(item.getFile());
}

class Processor final : public BuiltinProcessor
{
public:
    explicit Processor(const Entry& e) : BuiltinProcessor(e.plugin->name, e.plugin->is_instrument != 0), plugin(*e.plugin), description(e.plugin->description)
    {
        for (int i = 0; i < plugin.num_params; ++i)
        {
            const auto& info = plugin.params[i];
            const auto id = info.id != nullptr ? juce::String(info.id) : "p" + juce::String(i);
            const auto label = info.name != nullptr ? juce::String(info.name) : id;
            if (info.num_choices > 0 && info.choices != nullptr)
            {
                juce::StringArray choices;
                for (int c = 0; c < info.num_choices; ++c)
                    choices.add(info.choices[c] != nullptr ? info.choices[c] : "?");
                parameters.push_back(&addChoice(id, label, choices, juce::roundToInt(info.default_value)));
            }
            else
                parameters.push_back(&addFloat(id, label, { info.min, juce::jmax(info.min + 0.0001f, info.max) }, info.default_value, info.unit != nullptr ? info.unit : ""));
        }
        lastValues.assign(parameters.size(), std::numeric_limits<float>::quiet_NaN());
    }

    ~Processor() override { release(); }

    double getTailLengthSeconds() const override { return 2.0; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        release();
        instance = plugin.create(sampleRate, samplesPerBlock);
        std::fill(lastValues.begin(), lastValues.end(), std::numeric_limits<float>::quiet_NaN());
        events.reserve(1024);
    }

    void releaseResources() override { release(); }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (acceptsMidi())
            buffer.clear();
        if (instance == nullptr || buffer.getNumChannels() == 0)
            return;
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            const auto value = parameters[i]->convertFrom0to1(parameters[i]->getValue());
            if (value != lastValues[i] && plugin.set_param != nullptr)
            {
                lastValues[i] = value;
                plugin.set_param(instance, int(i), value);
            }
        }
        events.clear();
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
                events.push_back({ TSTUDIO_NOTE_ON, metadata.samplePosition, message.getNoteNumber(), message.getFloatVelocity() });
            else if (message.isNoteOff())
                events.push_back({ TSTUDIO_NOTE_OFF, metadata.samplePosition, message.getNoteNumber(), 0.0f });
        }
        tstudio_transport transport { 130.0, 0.0, 0 };
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
            {
                transport.bpm = position->getBpm().orFallback(130.0);
                transport.beat = position->getPpqPosition().orFallback(0.0);
                transport.playing = position->getIsPlaying() ? 1 : 0;
            }
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
        plugin.process(instance, left, right, buffer.getNumSamples(), events.data(), int(events.size()), &transport);
        for (auto channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
            for (auto* sample = buffer.getWritePointer(channel), *end = sample + buffer.getNumSamples(); sample != end; ++sample)
                *sample = std::isfinite(*sample) ? juce::jlimit(-4.0f, 4.0f, *sample) : 0.0f;
    }

    std::unique_ptr<juce::Component> createDisplay() override
    {
        if (acceptsMidi())
            return std::make_unique<display::Scope>(*this);
        return display::row(std::make_unique<display::Scope>(*this), 1.0f, std::make_unique<display::Spectrum>(*this), 1.0f);
    }

private:
    void release()
    {
        if (instance != nullptr)
            plugin.destroy(instance);
        instance = nullptr;
    }

    const tstudio_plugin& plugin;
    juce::String description;
    void* instance = nullptr;
    std::vector<juce::RangedAudioParameter*> parameters;
    std::vector<float> lastValues;
    std::vector<tstudio_event> events;
};
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TStudio").getChildFile("NativePlugins");
}

juce::StringArray names(bool instruments)
{
    scan();
    juce::StringArray result;
    for (const auto& entry : entries)
        if ((entry.plugin->is_instrument != 0) == instruments)
            result.add(entry.plugin->name);
    return result;
}

std::unique_ptr<juce::AudioProcessor> create(const juce::String& name)
{
    scan();
    for (const auto& entry : entries)
        if (name == entry.plugin->name)
            return std::make_unique<Processor>(entry);
    return nullptr;
}

juce::String scanReport()
{
    scan();
    juce::String report;
    report << int(entries.size()) << " native plugins from " << int(libraries.size()) << " libraries in " << userFolder().getFullPathName();
    if (!problems.isEmpty())
        report << juce::newLine << problems.joinIntoString(juce::newLine);
    return report;
}
}
