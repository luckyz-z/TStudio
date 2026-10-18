#include "UI/Controls.h"
#include "UI/Theme.h"
#include "Engine/Builtins/Registry.h"

#include <cmath>
#include <map>

namespace ts::ui
{
void setupKnob(juce::Slider& knob, double min, double max, double value, double reset, std::function<void(double)> onChange, const juce::String& tooltip)
{
    knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    knob.setRange(min, max);
    knob.setValue(value, juce::dontSendNotification);
    knob.setDoubleClickReturnValue(true, reset);
    knob.setMouseDragSensitivity(160);
    knob.setPopupDisplayEnabled(true, false, nullptr);
    knob.setTooltip(tooltip);
    knob.setWantsKeyboardFocus(false);
    knob.onValueChange = [&knob, change = std::move(onChange)] { change(knob.getValue()); };
}

void paintLcd(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& value, const juce::String& label)
{
    g.setColour(theme::lcd);
    g.fillRoundedRectangle(area.toFloat(), 3.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 3.0f, 1.0f);
    g.setColour(theme::textDim);
    g.setFont(theme::font(9.0f, juce::Font::bold));
    g.drawText(label, area.reduced(6, 3), juce::Justification::topLeft);
    g.setColour(theme::accent);
    g.setFont(theme::font(18.0f, juce::Font::bold));
    g.drawText(value, area.reduced(6, 0).withTrimmedTop(8), juce::Justification::centredRight);
}

void askText(const juce::String& title, const juce::String& current, std::function<void(const juce::String&)> done)
{
    auto* window = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("text", current);
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create([window, callback = std::move(done)](int result)
    {
        if (result == 1)
            callback(window->getTextEditorContents("text").trim());
    }), true);
}

void pickColour(juce::Component& anchor, juce::Colour current, std::function<void(juce::Colour)> done)
{
    struct Picker final : public juce::Component, private juce::ChangeListener
    {
        Picker(juce::Colour colour, std::function<void(juce::Colour)> callback) : changed(std::move(callback))
        {
            selector.setCurrentColour(colour);
            selector.addChangeListener(this);
            addAndMakeVisible(selector);
            setSize(260, 300);
        }

        void resized() override { selector.setBounds(getLocalBounds()); }
        void changeListenerCallback(juce::ChangeBroadcaster*) override { changed(selector.getCurrentColour()); }

        juce::ColourSelector selector { juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders };
        std::function<void(juce::Colour)> changed;
    };

    juce::CallOutBox::launchAsynchronously(std::make_unique<Picker>(current, std::move(done)), anchor.getScreenBounds(), nullptr);
}

void showMessage(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::InfoIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}

juce::String keyName(int key)
{
    return juce::MidiMessage::getMidiNoteName(key, true, true, 5);
}

bool isBlackKey(int key)
{
    const auto note = ((key % 12) + 12) % 12;
    return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
}

int snapDown(double tick, int snap)
{
    return int(std::floor(tick / snap)) * snap;
}

int snapNearest(double tick, int snap)
{
    return int(std::round(tick / snap)) * snap;
}

double patternPlayhead(const Studio& studio)
{
    if (!studio.engine.isPlaying())
        return -1.0;
    const auto position = studio.engine.getSongPosition();
    const auto& project = studio.project;
    if (!project.songMode.load())
        return position;
    const auto current = project.currentPattern.load();
    for (const auto& clip : project.clips)
        if (!clip.isAutomation() && clip.pattern == current && position >= clip.start && position < clip.end())
            return position - clip.start + clip.offset;
    return -1.0;
}

void addDeviceItems(juce::PopupMenu& menu, Studio& studio, bool instruments, std::function<void(std::unique_ptr<Device>)> picked)
{
    auto callback = std::make_shared<std::function<void(std::unique_ptr<Device>)>>(std::move(picked));
    menu.addSectionHeader("Built-in");
    for (const auto& name : instruments ? Builtins::generators() : Builtins::effects())
        menu.addItem(name, [&studio, name, callback] { (*callback)(studio.createBuiltin(name)); });

    const auto plugins = studio.plugins.list(instruments);
    if (plugins.isEmpty())
    {
        menu.addItem("No plugins scanned yet (Options > Manage plugins)", false, false, [] {});
        return;
    }

    menu.addSectionHeader("Plugins");
    std::map<juce::String, juce::PopupMenu> byVendor;
    for (const auto& description : plugins)
    {
        const auto vendor = description.manufacturerName.isNotEmpty() ? description.manufacturerName : juce::String("Unknown");
        byVendor[vendor].addItem(description.name, [&studio, description, callback]
        {
            juce::String error;
            auto device = studio.createPlugin(description, error);
            if (device == nullptr)
            {
                showMessage("Plugin error", "Could not load " + description.name + "\n" + error);
                return;
            }
            (*callback)(std::move(device));
        });
    }
    for (auto& [vendor, submenu] : byVendor)
        menu.addSubMenu(vendor, submenu);
}
}
