#include "UI/ChannelSettings.h"
#include "UI/Controls.h"
#include "UI/Theme.h"

namespace ts
{
namespace
{
constexpr int sectionHeight = 96;
const std::array<const char*, 4> sections { "TIME", "POLYPHONY", "CUT GROUPS", "ARPEGGIATOR" };
}

ChannelSettings::ChannelSettings(Studio& s, int channelId) : studio(s), id(channelId)
{
    const auto knob = [this](juce::Slider& slider, double low, double high, double initial, const juce::String& tip, std::function<juce::String(double)> text)
    {
        ui::setupKnob(slider, low, high, initial, initial, [this](double) { changed(); }, tip);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 70, 16);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxTextColourId, theme::accent);
        slider.textFromValueFunction = std::move(text);
        slider.updateText();
        addAndMakeVisible(slider);
    };
    knob(shift, 0.0, double(ticksPerStep), 0.0, "Time shift: delays this channel's notes by up to one step", [](double v) { return juce::String(v / ticksPerStep * 100.0, 0) + "% step"; });
    knob(swing, 0.0, 1.0, 1.0, "Swing mix: how much of the main swing this channel follows", [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });
    knob(portaTime, 1.0, 1000.0, 120.0, "Portamento / mono glide time", [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; });
    knob(gate, 0.05, 1.0, 0.5, "Arpeggiator gate: length of each arpeggiated note", [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });
    portaTime.setSkewFactorFromMidPoint(150.0);

    for (auto* toggle : { &mono, &porta })
    {
        toggle->onClick = [this] { changed(); };
        addAndMakeVisible(toggle);
    }
    mono.setTooltip("Mono: a new note stops the previous one");
    porta.setTooltip("Porta: every note glides from the previous one");

    for (auto* box : { &cutGroup, &cutBy })
    {
        box->addItem("None", 1);
        for (int i = 1; i <= 16; ++i)
            box->addItem(juce::String(i), i + 1);
        box->onChange = [this] { changed(); };
        addAndMakeVisible(box);
    }
    cutGroup.setTooltip("Cut: notes on this channel stop channels whose 'Cut by' matches (closed hat cuts open hat)");
    cutBy.setTooltip("Cut by: this channel is stopped by channels in this cut group");

    arp.addItemList({ "Off", "Up", "Down", "Up and down", "Random" }, 1);
    arpTime.addItemList({ "1/32", "1/16", "1/8", "1/4" }, 1);
    arpRange.addItemList({ "1 octave", "2 octaves", "3 octaves", "4 octaves" }, 1);
    for (auto* box : { &arp, &arpTime, &arpRange })
    {
        box->onChange = [this] { changed(); };
        addAndMakeVisible(box);
    }
    arp.setTooltip("Arpeggiator: plays held chords one note at a time");
    refresh();
    setSize(420, sectionHeight * int(sections.size()) + 16);
}

ChannelOptions* ChannelSettings::options() const
{
    auto* channel = studio.project.findChannel(id);
    return channel != nullptr ? &channel->options : nullptr;
}

void ChannelSettings::refresh()
{
    const auto* o = options();
    if (o == nullptr)
        return;
    shift.setValue(o->timeShift.load(), juce::dontSendNotification);
    swing.setValue(o->swingMix.load(), juce::dontSendNotification);
    portaTime.setValue(o->portaTime.load(), juce::dontSendNotification);
    gate.setValue(o->arpGate.load(), juce::dontSendNotification);
    mono.setToggleState(o->mono.load(), juce::dontSendNotification);
    porta.setToggleState(o->porta.load(), juce::dontSendNotification);
    cutGroup.setSelectedItemIndex(o->cutGroup.load(), juce::dontSendNotification);
    cutBy.setSelectedItemIndex(o->cutBy.load(), juce::dontSendNotification);
    arp.setSelectedItemIndex(o->arpDirection.load(), juce::dontSendNotification);
    arpTime.setSelectedItemIndex(o->arpTime.load(), juce::dontSendNotification);
    arpRange.setSelectedItemIndex(o->arpRange.load() - 1, juce::dontSendNotification);
}

void ChannelSettings::changed()
{
    auto* o = options();
    if (o == nullptr)
        return;
    o->timeShift.store(float(shift.getValue()));
    o->swingMix.store(float(swing.getValue()));
    o->portaTime.store(float(portaTime.getValue()));
    o->arpGate.store(float(gate.getValue()));
    o->mono.store(mono.getToggleState());
    o->porta.store(porta.getToggleState());
    o->cutGroup.store(juce::jmax(0, cutGroup.getSelectedItemIndex()));
    o->cutBy.store(juce::jmax(0, cutBy.getSelectedItemIndex()));
    o->arpDirection.store(juce::jmax(0, arp.getSelectedItemIndex()));
    o->arpTime.store(juce::jmax(0, arpTime.getSelectedItemIndex()));
    o->arpRange.store(juce::jmax(0, arpRange.getSelectedItemIndex()) + 1);
    studio.project.dirty = true;
}

void ChannelSettings::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto area = juce::Rectangle<int>(8, 8 + int(i) * sectionHeight, getWidth() - 16, sectionHeight - 8);
        g.setColour(theme::panelDark);
        g.fillRoundedRectangle(area.toFloat(), 4.0f);
        g.setColour(theme::accent);
        g.setFont(theme::font(11.0f, juce::Font::bold));
        g.drawText(sections[i], area.reduced(10, 4).removeFromTop(14), juce::Justification::centredLeft);
    }
    g.setColour(theme::textDim);
    g.setFont(theme::font(11.0f, juce::Font::bold));
    const auto label = [&g](juce::Rectangle<int> r, const juce::String& text) { g.drawText(text, r.withHeight(14).translated(0, -15), juce::Justification::centredLeft); };
    label(cutGroup.getBounds(), "CUT");
    label(cutBy.getBounds(), "CUT BY");
    label(arp.getBounds(), "DIRECTION");
    label(arpTime.getBounds(), "TIME");
    label(arpRange.getBounds(), "RANGE");
    for (auto* knob : { &shift, &swing, &portaTime, &gate })
        g.drawText(knob == &shift ? "SHIFT" : knob == &swing ? "SWING" : knob == &portaTime ? "GLIDE" : "GATE", knob->getBounds().withHeight(14).translated(0, -14),
                   juce::Justification::centred);
}

void ChannelSettings::resized()
{
    const auto row = [](int index) { return juce::Rectangle<int>(18, 8 + index * sectionHeight + 34, 384, sectionHeight - 46); };
    auto time = row(0);
    shift.setBounds(time.removeFromLeft(80));
    swing.setBounds(time.removeFromLeft(80));
    auto poly = row(1);
    mono.setBounds(poly.removeFromLeft(80).withSizeKeepingCentre(76, 24));
    porta.setBounds(poly.removeFromLeft(80).withSizeKeepingCentre(76, 24));
    portaTime.setBounds(poly.removeFromLeft(80));
    auto cut = row(2).withTrimmedTop(8);
    cutGroup.setBounds(cut.removeFromLeft(110).withHeight(24));
    cut.removeFromLeft(16);
    cutBy.setBounds(cut.removeFromLeft(110).withHeight(24));
    auto arpeggio = row(3).withTrimmedTop(8);
    arp.setBounds(arpeggio.removeFromLeft(116).withHeight(24));
    arpeggio.removeFromLeft(8);
    arpTime.setBounds(arpeggio.removeFromLeft(70).withHeight(24));
    arpeggio.removeFromLeft(8);
    arpRange.setBounds(arpeggio.removeFromLeft(96).withHeight(24));
    gate.setBounds(arpeggio.removeFromLeft(80).translated(0, -10));
}
}
