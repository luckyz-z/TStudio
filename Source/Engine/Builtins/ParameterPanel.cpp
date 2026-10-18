#include "Engine/Builtins/ParameterPanel.h"
#include "Engine/Builtins/BuiltinProcessor.h"

namespace ts
{
namespace
{
constexpr int cellWidth = 78;
constexpr int cellHeight = 92;
constexpr int sectionHeight = 20;
constexpr int compactHeight = 82;
constexpr int maxSectionColumns = 8;
constexpr int columns = 7;
constexpr int headerHeight = 44;
constexpr int displayHeight = 210;
const juce::Colour accent { 0xffe8483b };
const juce::Colour text { 0xffd6dde0 };
const juce::Colour dim { 0xff7f8b90 };

juce::String sectionOf(const juce::RangedAudioParameter& parameter)
{
    const auto name = parameter.getName(64);
    const auto words = juce::StringArray::fromTokens(name, " ", {});
    if (words.size() >= 3 && (words[0] == "Osc" || words[0] == "Band" || words[0] == "Op"))
        return words[0] + " " + words[1];
    return {};
}
}

struct ParameterPanel::Control final : private juce::MouseListener
{
    Control(juce::AudioProcessor& owner, juce::RangedAudioParameter& p) : processor(owner), parameter(p)
    {
        label.setText(p.getName(32), juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, dim);
        label.setInterceptsMouseClicks(false, false);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&p))
        {
            combo = std::make_unique<juce::ComboBox>();
            combo->addItemList(choice->choices, 1);
            comboAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(*choice, *combo);
        }
        else if (auto* toggle = dynamic_cast<juce::AudioParameterBool*>(&p))
        {
            button = std::make_unique<juce::ToggleButton>();
            buttonAttachment = std::make_unique<juce::ButtonParameterAttachment>(*toggle, *button);
        }
        else
        {
            knob = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle(juce::Slider::TextBoxBelow, true, cellWidth - 6, 16);
            knob->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            knob->setColour(juce::Slider::textBoxTextColourId, accent);
            knob->setMouseDragSensitivity(180);
            sliderAttachment = std::make_unique<juce::SliderParameterAttachment>(p, *knob);
            knob->setDoubleClickReturnValue(true, double(p.convertFrom0to1(p.getDefaultValue())));
            const auto unit = p.getLabel();
            const auto integer = dynamic_cast<juce::AudioParameterInt*>(&p) != nullptr;
            knob->textFromValueFunction = [unit, integer](double value)
            {
                const auto magnitude = std::abs(value);
                const auto decimals = integer || magnitude >= 100.0 ? 0 : (magnitude >= 10.0 ? 1 : 2);
                return juce::String(value, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
            };
            if (p.getParameterID() == "root")
                knob->textFromValueFunction = [](double value) { return juce::MidiMessage::getMidiNoteName(juce::roundToInt(value), true, true, 5); };
            knob->updateText();
        }
        editor().addMouseListener(this, true);
    }

    ~Control() override { editor().removeMouseListener(this); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isPopupMenu() || !createAutomation)
            return;
        juce::PopupMenu menu;
        menu.addSectionHeader(parameter.getName(40));
        menu.addItem("Create automation clip", [this] { createAutomation(processor, parameter.getParameterIndex()); });
        menu.addItem("Reset to default", [this] { parameter.setValueNotifyingHost(parameter.getDefaultValue()); });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&editor()));
    }

    juce::Component& editor()
    {
        if (combo != nullptr)
            return *combo;
        if (button != nullptr)
            return *button;
        return *knob;
    }

    juce::AudioProcessor& processor;
    juce::RangedAudioParameter& parameter;
    juce::Label label;
    std::unique_ptr<juce::Slider> knob;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> button;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
    std::unique_ptr<juce::ComboBoxParameterAttachment> comboAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> buttonAttachment;
};

ParameterPanel::ParameterPanel(juce::AudioProcessor& owner) : processor(owner)
{
    struct Group
    {
        juce::String section;
        std::vector<int> members;
    };
    std::vector<Group> groups;
    for (auto* raw : processor.getParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(raw);
        if (parameter == nullptr)
            continue;
        if (auto* builtin = dynamic_cast<BuiltinProcessor*>(&processor); builtin != nullptr && !builtin->showsInPanel(parameter->getParameterID()))
            continue;
        const auto section = sectionOf(*parameter);
        if (groups.empty() || groups.back().section != section)
            groups.push_back({ section, {} });
        auto control = std::make_unique<Control>(processor, *parameter);
        if (section.isNotEmpty())
            control->label.setText(parameter->getName(64).fromFirstOccurrenceOf(section, false, false).trim().toUpperCase().substring(0, 1)
                                       + parameter->getName(64).fromFirstOccurrenceOf(section, false, false).trim().substring(1),
                                   juce::dontSendNotification);
        addAndMakeVisible(control->label);
        addAndMakeVisible(control->editor());
        groups.back().members.push_back(int(controls.size()));
        controls.push_back(std::move(control));
    }

    cells.resize(controls.size());
    auto y = 6;
    auto width = cellWidth * 4;
    for (size_t g = 0; g < groups.size();)
    {
        if (groups[g].section.isEmpty())
        {
            const auto& members = groups[g].members;
            for (size_t i = 0; i < members.size(); ++i)
            {
                const auto column = int(i) % columns;
                if (column == 0 && i > 0)
                    y += cellHeight;
                cells[size_t(members[i])] = { 8 + column * cellWidth, y, cellWidth, cellHeight };
                width = juce::jmax(width, (column + 1) * cellWidth);
            }
            y += cellHeight;
            ++g;
            continue;
        }

        auto column = 0;
        auto tallest = 0;
        const auto firstHeader = headers.size();
        for (; g < groups.size() && groups[g].section.isNotEmpty() && column < maxSectionColumns; ++g, ++column)
        {
            const auto x = 8 + column * cellWidth;
            headers.push_back({ juce::Rectangle<int>(x, y, cellWidth, sectionHeight), groups[g].section });
            const auto& members = groups[g].members;
            for (size_t i = 0; i < members.size(); ++i)
                cells[size_t(members[i])] = { x, y + sectionHeight + int(i) * compactHeight, cellWidth, compactHeight };
            tallest = juce::jmax(tallest, int(members.size()));
            width = juce::jmax(width, (column + 1) * cellWidth);
        }
        for (auto h = firstHeader; h < headers.size(); ++h)
            headers[h].first.setHeight(sectionHeight + tallest * compactHeight);
        y += sectionHeight + tallest * compactHeight + 6;
    }
    setSize(width + 16, controls.empty() ? 0 : y + 6);
}

ParameterPanel::~ParameterPanel() = default;

void ParameterPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2b3135));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    for (const auto& [area, title] : headers)
    {
        g.setColour(juce::Colour(0xff23292c));
        g.fillRoundedRectangle(area.reduced(3, 0).toFloat(), 4.0f);
        g.setColour(accent.withAlpha(0.9f));
        g.drawText(title.toUpperCase(), area.withHeight(sectionHeight), juce::Justification::centred);
    }
}

void ParameterPanel::resized()
{
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& control = *controls[i];
        auto cell = cells[i].reduced(3, 2);
        control.label.setBounds(cell.removeFromTop(16));
        if (control.knob != nullptr)
            control.knob->setBounds(cell);
        else
            control.editor().setBounds(cell.withSizeKeepingCentre(cell.getWidth(), control.combo != nullptr ? 24 : 28));
    }
}

BuiltinEditor::BuiltinEditor(juce::AudioProcessor& processor) : AudioProcessorEditor(processor), panel(processor)
{
    if (auto* builtin = dynamic_cast<BuiltinProcessor*>(&processor))
        display = builtin->createDisplay();
    if (display != nullptr)
        addAndMakeVisible(*display);
    addAndMakeVisible(panel);

    if (processor.getNumPrograms() > 1)
    {
        for (int i = 0; i < processor.getNumPrograms(); ++i)
            presets.addItem(processor.getProgramName(i), i + 1);
        presets.setSelectedItemIndex(processor.getCurrentProgram(), juce::dontSendNotification);
        presets.setTooltip("Presets");
        presets.onChange = [this] { this->processor.setCurrentProgram(presets.getSelectedItemIndex()); };
        addAndMakeVisible(presets);
    }

    if (display != nullptr)
        displayArea = display->getHeight() > 0 ? display->getHeight() + 12 : displayHeight;
    const auto width = juce::jmax(panel.getWidth(), display != nullptr ? juce::jmax(600, display->getWidth() + 16) : 0);
    setSize(width, headerHeight + displayArea + panel.getHeight());
}

void BuiltinEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2b3135));
    auto header = getLocalBounds().removeFromTop(headerHeight);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff485257), 0.0f, 0.0f, juce::Colour(0xff31383c), 0.0f, float(headerHeight), false));
    g.fillRect(header);
    g.setColour(accent);
    g.fillRect(header.removeFromLeft(5));
    g.setColour(text);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText(processor.getName(), header.reduced(12, 0), juce::Justification::centredLeft);
    g.setColour(dim);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("TSTUDIO", header.reduced(12, 0).withTrimmedRight(presets.isVisible() ? 200 : 0), juce::Justification::centredRight);
}

void BuiltinEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(headerHeight);
    presets.setBounds(header.removeFromRight(190).reduced(8, 10));
    if (display != nullptr)
        display->setBounds(area.removeFromTop(displayArea).reduced(8, 6));
    panel.setBounds(area.withWidth(juce::jmax(area.getWidth(), panel.getWidth())));
}
}
