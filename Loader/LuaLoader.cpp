#include "Shared/LuaPluginScript.h"
#include "Shared/LuaPluginUI.h"
#include "UI/LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <TStudioLoaderData.h>

namespace ts
{
namespace
{
constexpr int slotCount = 16;
const juce::Colour background { 0xff2b3135 }, header { 0xff485257 }, accent { 0xffe8483b }, text { 0xffd6dde0 }, dim { 0xff7f8b90 };

struct ScriptSource
{
    juce::String name, description, code, origin;
};

juce::File userFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TStudio").getChildFile("Plugins");
}

std::vector<ScriptSource> available(bool instruments)
{
    std::vector<ScriptSource> result;
    const auto consider = [&result, instruments](const juce::String& code, const juce::String& origin)
    {
        LuaPluginInfo info;
        juce::String error;
        if (!LuaPluginScript::inspect(code, origin, info, error) || info.instrument != instruments)
            return;
        for (const auto& existing : result)
            if (existing.name == info.name)
                return;
        result.push_back({ info.name, info.description, code, origin });
    };
    if (userFolder().isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator(userFolder(), true, "*.lua"))
            consider(entry.getFile().loadFileAsString(), entry.getFile().getFileName());
    for (int i = 0; i < TStudioLoaderData::namedResourceListSize; ++i)
    {
        const juce::String file(TStudioLoaderData::originalFilenames[i]);
        if (!file.endsWith(".lua"))
            continue;
        int size = 0;
        if (const auto* data = TStudioLoaderData::getNamedResource(TStudioLoaderData::namedResourceList[i], size))
            consider(juce::String::fromUTF8(data, size), file);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return result;
}

class SlotParameter final : public juce::AudioParameterFloat
{
public:
    explicit SlotParameter(int index)
        : AudioParameterFloat(juce::ParameterID { "slot" + juce::String(index + 1), 1 }, "Param " + juce::String(index + 1), { 0.0f, 1.0f }, 0.5f), fallback("Param " + juce::String(index + 1))
    {
    }

    juce::String getName(int maximumLength) const override { return (label.isNotEmpty() ? label : fallback).substring(0, maximumLength); }

    juce::String getText(float normalised, int) const override
    {
        if (info == nullptr)
            return juce::String(normalised, 2);
        const auto real = range.convertFrom0to1(normalised);
        if (!info->choices.isEmpty())
            return info->choices[juce::jlimit(0, info->choices.size() - 1, juce::roundToInt(real - info->min))];
        return juce::String(real, std::abs(real) >= 100.0f ? 0 : 2) + (info->unit.isNotEmpty() ? " " + info->unit : juce::String());
    }

    float realValue() const
    {
        return info == nullptr || info->choices.isEmpty() ? get() : float(juce::roundToInt(get()));
    }

    juce::String label;
    const LuaPluginInfo::Parameter* info = nullptr;

private:
    juce::String fallback;
};

class LoaderProcessor final : public juce::AudioProcessor, public LuaPluginHost
{
public:
    LoaderProcessor()
        : AudioProcessor(TS_LOADER_INSTRUMENT ? BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                              : BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < slotCount; ++i)
        {
            auto* slot = new SlotParameter(i);
            slots[size_t(i)] = slot;
            addParameter(slot);
        }
        catalogue = available(TS_LOADER_INSTRUMENT);
        if (!catalogue.empty())
            loadScript(catalogue.front(), true);
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return juce::jmax(1, int(catalogue.size())); }
    int getCurrentProgram() override
    {
        for (size_t i = 0; i < catalogue.size(); ++i)
            if (catalogue[i].name == current.name)
                return int(i);
        return 0;
    }
    void setCurrentProgram(int index) override
    {
        if (juce::isPositiveAndBelow(index, int(catalogue.size())) && catalogue[size_t(index)].name != current.name)
            loadScript(catalogue[size_t(index)], true);
    }
    const juce::String getProgramName(int index) override
    {
        return juce::isPositiveAndBelow(index, int(catalogue.size())) ? catalogue[size_t(index)].name : juce::String();
    }
    void changeProgramName(int, const juce::String&) override {}
    void releaseResources() override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        rate = sampleRate;
        blockSize = samplesPerBlock;
        const juce::SpinLock::ScopedLockType guard(lock);
        if (script != nullptr)
            script->prepare(sampleRate, samplesPerBlock);
        lastValues.fill(std::numeric_limits<float>::quiet_NaN());
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const juce::SpinLock::ScopedTryLockType guard(lock);
        if (TS_LOADER_INSTRUMENT)
            buffer.clear();
        if (!guard.isLocked() || script == nullptr || buffer.getNumChannels() == 0)
            return;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i]->info == nullptr)
                continue;
            const auto value = slots[i]->realValue();
            if (value != lastValues[i])
            {
                lastValues[i] = value;
                script->setParameter(i, value);
            }
        }
        auto bpm = 120.0, beat = 0.0;
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
            {
                bpm = position->getBpm().orFallback(120.0);
                beat = position->getPpqPosition().orFallback(0.0);
            }
        auto* right = buffer.getWritePointer(juce::jmin(1, buffer.getNumChannels() - 1));
        script->process(buffer.getWritePointer(0), right, buffer.getNumSamples(), midi, bpm, beat);
    }

    bool loadScript(const ScriptSource& source, bool resetValues)
    {
        auto next = std::make_unique<LuaPluginScript>();
        auto nextInfo = std::make_unique<LuaPluginInfo>();
        juce::String error;
        if (!next->load(source.code, source.origin, *nextInfo, error))
        {
            status = "Could not load " + source.origin + ": " + error;
            ++version;
            return false;
        }
        next->prepare(rate, blockSize);
        {
            const juce::SpinLock::ScopedLockType guard(lock);
            std::swap(script, next);
            std::swap(info, nextInfo);
            current = source;
            for (size_t i = 0; i < slots.size(); ++i)
            {
                const auto* parameterInfo = i < info->parameters.size() ? &info->parameters[i] : nullptr;
                slots[i]->info = parameterInfo;
                slots[i]->label = parameterInfo != nullptr ? parameterInfo->name : juce::String();
                slots[i]->range = parameterInfo != nullptr
                                      ? juce::NormalisableRange<float>(parameterInfo->min, juce::jmax(parameterInfo->min + 0.0001f, parameterInfo->max), parameterInfo->choices.isEmpty() ? 0.0f : 1.0f)
                                      : juce::NormalisableRange<float>(0.0f, 1.0f);
            }
            lastValues.fill(std::numeric_limits<float>::quiet_NaN());
        }
        if (resetValues)
            for (size_t i = 0; i < info->parameters.size() && i < slots.size(); ++i)
            {
                slots[i]->setValueNotifyingHost(slots[i]->convertTo0to1(info->parameters[i].value));
            }
        status = source.description.isNotEmpty() ? source.description : juce::String("Loaded");
        ++version;
        updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
        return true;
    }

    void getStateInformation(juce::MemoryBlock& destination) override
    {
        juce::XmlElement state("TStudioLuaLoader");
        state.setAttribute("name", current.name);
        state.setAttribute("origin", current.origin);
        state.createNewChildElement("Code")->addTextElement(current.code);
        for (size_t i = 0; i < slots.size(); ++i)
            state.setAttribute("slot" + juce::String(int(i) + 1), slots[i]->convertTo0to1(slots[i]->get()));
        copyXmlToBinary(state, destination);
    }

    void setStateInformation(const void* data, int size) override
    {
        const auto state = getXmlFromBinary(data, size);
        if (state == nullptr || !state->hasTagName("TStudioLuaLoader"))
            return;
        ScriptSource source { state->getStringAttribute("name"), {}, {}, state->getStringAttribute("origin") };
        if (const auto* code = state->getChildByName("Code"))
            source.code = code->getAllSubText();
        for (const auto& candidate : available(TS_LOADER_INSTRUMENT))
            if (candidate.name == source.name)
                source = candidate;
        if (source.code.isNotEmpty())
            loadScript(source, false);
        for (size_t i = 0; i < slots.size(); ++i)
            slots[i]->setValueNotifyingHost(float(state->getDoubleAttribute("slot" + juce::String(int(i) + 1), slots[i]->convertTo0to1(slots[i]->get()))));
    }

    std::array<SlotParameter*, slotCount> slots {};
    std::vector<ScriptSource> catalogue;
    ScriptSource current;
    std::unique_ptr<LuaPluginInfo> info;
    juce::String status { "No Lua plugin loaded" };
    std::atomic<int> version { 0 };

    juce::RangedAudioParameter* parameterFor(const juce::String& id) override
    {
        if (info != nullptr)
            for (size_t i = 0; i < info->parameters.size() && i < slots.size(); ++i)
                if (info->parameters[i].id == id)
                    return slots[i];
        return nullptr;
    }

    float meterValue(int index) override { return script != nullptr ? script->meter(index) : 0.0f; }

private:
    juce::SpinLock lock;
    std::unique_ptr<LuaPluginScript> script;
    std::array<float, slotCount> lastValues {};
    double rate = 44100.0;
    int blockSize = 512;
};

class LoaderEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LoaderEditor(LoaderProcessor& p) : AudioProcessorEditor(p), loader(p)
    {
        setLookAndFeel(&look.get());
        picker.onChange = [this]
        {
            const auto index = picker.getSelectedItemIndex();
            if (index >= 0 && index < int(sources.size()))
                loader.loadScript(sources[size_t(index)], true);
        };
        rescan.onClick = [this] { refreshList(); };
        folder.onClick = [] { userFolder().createDirectory(); userFolder().startAsProcess(); };
        addAndMakeVisible(picker);
        addAndMakeVisible(rescan);
        addAndMakeVisible(folder);
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& knob = knobs[i];
            knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 80, 16);
            knob.setColour(juce::Slider::rotarySliderFillColourId, accent);
            knob.setColour(juce::Slider::textBoxTextColourId, accent);
            knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            attachments[i] = std::make_unique<juce::SliderParameterAttachment>(*loader.slots[i], knob);
            labels[i].setJustificationType(juce::Justification::centred);
            labels[i].setColour(juce::Label::textColourId, dim);
            labels[i].setFont(juce::FontOptions(12.0f, juce::Font::bold));
            addChildComponent(knob);
            addChildComponent(labels[i]);
        }
        refreshList();
        setSize(620, 330);
        timerCallback();
        startTimerHz(8);
    }

    ~LoaderEditor() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(background);
        auto bar = getLocalBounds().removeFromTop(44);
        g.setGradientFill(juce::ColourGradient(header, 0.0f, 0.0f, juce::Colour(0xff31383c), 0.0f, 44.0f, false));
        g.fillRect(bar);
        g.setColour(accent);
        g.fillRect(bar.removeFromLeft(5));
        g.setColour(text);
        g.setFont(juce::FontOptions(19.0f, juce::Font::bold));
        g.drawText(JucePlugin_Name, bar.reduced(12, 0), juce::Justification::centredLeft);
        g.setColour(dim);
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(loader.status, getLocalBounds().removeFromBottom(24).reduced(12, 0), juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop(44).reduced(8, 9);
        folder.setBounds(bar.removeFromRight(110));
        bar.removeFromRight(6);
        rescan.setBounds(bar.removeFromRight(80));
        bar.removeFromRight(6);
        picker.setBounds(bar.removeFromRight(220));
        auto area = getLocalBounds().withTrimmedTop(52).withTrimmedBottom(26).reduced(8, 0);
        if (view != nullptr)
            view->setBounds(area.withSizeKeepingCentre(view->getWidth(), view->getHeight()));
        const auto cellWidth = area.getWidth() / 8;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const auto row = int(i) / 8;
            const auto column = int(i) % 8;
            auto cell = juce::Rectangle<int>(area.getX() + column * cellWidth, area.getY() + row * 124, cellWidth, 120).reduced(3);
            labels[i].setBounds(cell.removeFromTop(18));
            knobs[i].setBounds(cell);
        }
    }

private:
    void refreshList()
    {
        sources = available(TS_LOADER_INSTRUMENT);
        loader.catalogue = sources;
        loader.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
        picker.clear(juce::dontSendNotification);
        for (size_t i = 0; i < sources.size(); ++i)
            picker.addItem(sources[i].name, int(i) + 1);
        for (size_t i = 0; i < sources.size(); ++i)
            if (sources[i].name == loader.current.name)
                picker.setSelectedItemIndex(int(i), juce::dontSendNotification);
        seen = -1;
    }

    void timerCallback() override
    {
        if (loader.version.load() == seen)
            return;
        seen = loader.version.load();
        view.reset();
        if (loader.info != nullptr && loader.info->ui.getDynamicObject() != nullptr)
        {
            view = std::make_unique<LuaPluginView>(loader.current.code, loader.current.origin, *loader.info, loader);
            addAndMakeVisible(*view);
        }
        setSize(view != nullptr ? juce::jmax(620, view->getWidth() + 16) : 620, view != nullptr ? view->getHeight() + 86 : 330);
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const auto used = view == nullptr && loader.slots[i]->info != nullptr;
            knobs[i].setVisible(used);
            labels[i].setVisible(used);
            labels[i].setText(loader.slots[i]->label, juce::dontSendNotification);
            knobs[i].updateText();
        }
        repaint();
    }

    juce::SharedResourcePointer<LookAndFeel> look;
    LoaderProcessor& loader;
    std::vector<ScriptSource> sources;
    juce::ComboBox picker;
    juce::TextButton rescan { "Rescan" }, folder { "Open folder" };
    std::array<juce::Slider, slotCount> knobs;
    std::array<juce::Label, slotCount> labels;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, slotCount> attachments;
    std::unique_ptr<LuaPluginView> view;
    int seen = -1;
};

juce::AudioProcessorEditor* LoaderProcessor::createEditor()
{
    return new LoaderEditor(*this);
}
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ts::LoaderProcessor();
}
