#include "Engine/Builtins/LuaPlugins.h"
#include "Engine/Builtins/BuiltinProcessor.h"
#include "Engine/Builtins/Displays.h"
#include "Shared/LuaPluginScript.h"
#include "Shared/LuaPluginUI.h"

#include <BinaryData.h>

namespace ts::LuaPlugins
{
namespace
{
std::vector<Entry> entries;
juce::StringArray problems;
bool scanned = false;

class Processor final : public BuiltinProcessor, private LuaPluginHost
{
public:
    Processor(const Entry& entry, LuaPluginInfo i)
        : BuiltinProcessor(entry.name, i.instrument), info(std::move(i)), code(entry.code), source(entry.source), description(entry.description),
          script(std::make_unique<LuaPluginScript>())
    {
        if (const auto* widgets = info.ui.getProperty("widgets", {}).getArray())
            for (const auto& widget : *widgets)
                for (const auto* key : { "param", "x_param", "y_param" })
                    custom.addIfNotAlreadyThere(widget.getProperty(key, {}).toString());
        juce::String error;
        LuaPluginInfo loaded;
        if (!script->load(entry.code, entry.source, loaded, error))
            loadError = error;
        for (const auto& parameter : info.parameters)
        {
            if (!parameter.choices.isEmpty())
                parameters.push_back(&addChoice(parameter.id, parameter.name, parameter.choices, juce::roundToInt(parameter.value)));
            else
                parameters.push_back(&addFloat(parameter.id, parameter.name, { parameter.min, juce::jmax(parameter.min + 0.0001f, parameter.max) }, parameter.value, parameter.unit));
        }
        for (const auto& preset : info.presets)
        {
            Preset values { preset.name, {} };
            for (const auto& [id, value] : preset.values)
                values.second.emplace_back(id, value);
            namedPresets.push_back(values);
        }
        lastValues.assign(parameters.size(), std::numeric_limits<float>::quiet_NaN());
    }

    int getNumPrograms() override { return int(namedPresets.size()) + 1; }
    int getCurrentProgram() override { return chosen; }
    const juce::String getProgramName(int index) override
    {
        return index <= 0 || index > int(namedPresets.size()) ? juce::String("Init") : namedPresets[size_t(index - 1)].first;
    }

    void setCurrentProgram(int index) override
    {
        BuiltinProcessor::setCurrentProgram(0);
        chosen = juce::jlimit(0, int(namedPresets.size()), index);
        if (chosen == 0)
            return;
        for (const auto& [id, value] : namedPresets[size_t(chosen - 1)].second)
            if (auto* target = parameter(id))
                target->setValueNotifyingHost(target->convertTo0to1(value));
    }

    double getTailLengthSeconds() const override { return 4.0; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        script->prepare(sampleRate, samplesPerBlock);
        std::fill(lastValues.begin(), lastValues.end(), std::numeric_limits<float>::quiet_NaN());
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (isInstrument())
            buffer.clear();
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            const auto value = parameters[i]->convertFrom0to1(parameters[i]->getValue());
            if (value != lastValues[i])
            {
                lastValues[i] = value;
                script->setParameter(i, value);
            }
        }
        auto bpm = 130.0, beat = 0.0;
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
            {
                bpm = position->getBpm().orFallback(130.0);
                beat = position->getPpqPosition().orFallback(0.0);
            }
        auto* right = buffer.getWritePointer(juce::jmin(1, buffer.getNumChannels() - 1));
        if (!script->process(buffer.getWritePointer(0), right, buffer.getNumSamples(), midi, bpm, beat) && isInstrument())
            buffer.clear();
    }

    bool isInstrument() const { return acceptsMidi(); }

    std::unique_ptr<juce::Component> createDisplay() override;
    bool showsInPanel(const juce::String& id) const override { return !custom.contains(id); }

    juce::String status() const
    {
        if (loadError.isNotEmpty())
            return "Script error: " + loadError;
        if (script->error().isNotEmpty())
            return "Script stopped: " + script->error();
        return description.isNotEmpty() ? description : juce::String("Lua plugin");
    }

private:
    using Preset = std::pair<juce::String, std::vector<std::pair<juce::String, float>>>;

    juce::RangedAudioParameter* parameterFor(const juce::String& id) override { return parameter(id); }
    float meterValue(int index) override { return script->meter(index); }
    std::unique_ptr<juce::Component> createScope() override { return std::make_unique<display::Scope>(*this); }

    LuaPluginInfo info;
    juce::String code, source;
    juce::StringArray custom;
    juce::String description, loadError;
    std::unique_ptr<LuaPluginScript> script;
    std::vector<juce::RangedAudioParameter*> parameters;
    std::vector<float> lastValues;
    std::vector<Preset> namedPresets;
    int chosen = 0;
};

class StatusDisplay final : public juce::Component, private juce::Timer
{
public:
    StatusDisplay(Processor& p, std::unique_ptr<juce::Component> view) : processor(p), content(std::move(view))
    {
        addAndMakeVisible(*content);
        if (content->getWidth() > 0)
            setSize(content->getWidth(), content->getHeight() + 22);
        startTimerHz(4);
    }

    void timerCallback() override { repaint(getLocalBounds().removeFromBottom(22)); }

    void paint(juce::Graphics& g) override
    {
        const auto text = processor.status();
        const auto failed = text.startsWith("Script");
        g.setColour(failed ? display::trace : display::label);
        g.setFont(juce::FontOptions(12.0f, failed ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText("LUA  |  " + text, getLocalBounds().removeFromBottom(22).reduced(8, 0), juce::Justification::centredLeft, 1);
    }

    void resized() override { content->setBounds(getLocalBounds().withTrimmedBottom(22)); }

private:
    Processor& processor;
    std::unique_ptr<juce::Component> content;
};

std::unique_ptr<juce::Component> Processor::createDisplay()
{
    if (info.ui.getDynamicObject() != nullptr)
        return std::make_unique<StatusDisplay>(*this, std::make_unique<LuaPluginView>(code, source, info, static_cast<LuaPluginHost&>(*this)));
    auto view = isInstrument() ? std::unique_ptr<juce::Component>(std::make_unique<display::Scope>(*this))
                               : display::row(std::make_unique<display::Scope>(*this), 1.0f, std::make_unique<display::Spectrum>(*this), 1.0f);
    return std::make_unique<StatusDisplay>(*this, std::move(view));
}

void add(const juce::String& code, const juce::String& source)
{
    LuaPluginInfo info;
    juce::String error;
    if (!LuaPluginScript::inspect(code, source, info, error))
    {
        problems.add(source + ": " + error);
        return;
    }
    for (const auto& existing : entries)
        if (existing.name == info.name)
            return;
    entries.push_back({ info.name, info.description, code, source, info.instrument });
}
}

juce::File userFolder()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TStudio").getChildFile("Plugins");
    folder.createDirectory();
    return folder;
}

void rescan()
{
    entries.clear();
    problems.clear();
    for (const auto& entry : juce::RangedDirectoryIterator(userFolder(), true, "*.lua"))
        add(entry.getFile().loadFileAsString(), entry.getFile().getFileName());
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String file(BinaryData::originalFilenames[i]);
        if (!file.startsWith("plugin_") || !file.endsWith(".lua"))
            continue;
        int size = 0;
        if (const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
            add(juce::String::fromUTF8(data, size), file);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    scanned = true;
}

const std::vector<Entry>& all()
{
    if (!scanned)
        rescan();
    return entries;
}

juce::StringArray names(bool instruments)
{
    juce::StringArray result;
    for (const auto& entry : all())
        if (entry.instrument == instruments)
            result.add(entry.name);
    return result;
}

std::unique_ptr<juce::AudioProcessor> create(const juce::String& name)
{
    for (const auto& entry : all())
    {
        if (entry.name != name)
            continue;
        LuaPluginInfo info;
        juce::String error;
        LuaPluginScript::inspect(entry.code, entry.source, info, error);
        return std::make_unique<Processor>(entry, info);
    }
    return nullptr;
}

juce::String scanReport()
{
    all();
    juce::String report;
    report << int(entries.size()) << " Lua plugins loaded from " << userFolder().getFullPathName() << " and the built-in set.";
    if (!problems.isEmpty())
        report << juce::newLine << juce::newLine << "Problems:" << juce::newLine << problems.joinIntoString(juce::newLine);
    return report;
}
}
