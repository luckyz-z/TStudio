#include "UI/Mixer.h"
#include "UI/Browser.h"
#include "UI/Controls.h"
#include "UI/Theme.h"

#include <cmath>

namespace ts
{
namespace
{
constexpr int stripWidth = 58;
constexpr int masterWidth = 72;
constexpr int slotsWidth = 230;
constexpr int slotHeight = 24;
constexpr double maxGain = 1.9;
const double faderSkew = std::log(0.8) / std::log(1.0 / maxGain);

juce::String gainText(float gain)
{
    return gain <= 0.0001f ? juce::String("-inf") : juce::String(juce::Decibels::gainToDecibels(gain), 1) + " dB";
}
}

class Mixer::Strip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Strip(Mixer& m, int i) : mixer(m), studio(m.studio), index(i)
    {
        auto& track = *studio.project.mixer[size_t(index)];
        fader.setSliderStyle(juce::Slider::LinearVertical);
        fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange(0.0, maxGain);
        fader.setSkewFactor(faderSkew);
        fader.setDoubleClickReturnValue(true, 1.0);
        fader.setValue(track.volume.load(), juce::dontSendNotification);
        fader.setWantsKeyboardFocus(false);
        fader.setPopupDisplayEnabled(true, false, nullptr);
        fader.textFromValueFunction = [](double v) { return gainText(float(v)); };
        fader.setTooltip("Volume");
        fader.onValueChange = [this]
        {
            studio.project.mixer[size_t(index)]->volume.store(float(fader.getValue()));
            studio.project.dirty = true;
        };
        ui::setupKnob(pan, -1.0, 1.0, track.pan.load(), 0.0, [this](double v) { studio.project.mixer[size_t(index)]->pan.store(float(v)); studio.project.dirty = true; },
                      "Panning");
        addAndMakeVisible(fader);
        addAndMakeVisible(pan);
        setTooltip(index == 0 ? "Master: click to select, right-click to rename"
                              : "Insert " + juce::String(index) + ": click to select, double-click to rename, LED: mute (right-click: solo). "
                                "Bottom switch: route the selected track here (drag: send level, right-click: more)");
    }

    void refresh()
    {
        const auto& track = *studio.project.mixer[size_t(index)];
        fader.setValue(track.volume.load(), juce::dontSendNotification);
        pan.setValue(track.pan.load(), juce::dontSendNotification);
        repaint();
    }

    void updateMeters()
    {
        auto& track = *studio.project.mixer[size_t(index)];
        const auto left = juce::jmax(track.peakLeft.exchange(0.0f), levelLeft * 0.82f);
        const auto right = juce::jmax(track.peakRight.exchange(0.0f), levelRight * 0.82f);
        if (std::abs(left - levelLeft) < 0.001f && std::abs(right - levelRight) < 0.001f)
            return;
        levelLeft = left;
        levelRight = right;
        repaint(meterArea);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(3, 2);
        nameArea = area.removeFromTop(34);
        pan.setBounds(area.removeFromTop(30).withSizeKeepingCentre(26, 26));
        ledArea = area.removeFromTop(16);
        numberArea = area.removeFromBottom(18);
        routeArea = area.removeFromBottom(20);
        meterArea = area.removeFromRight(12).reduced(1, 4);
        fader.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& track = *studio.project.mixer[size_t(index)];
        const auto isSelected = mixer.selected == index;
        g.setColour(isSelected ? theme::titleActive : (index == 0 ? theme::title : theme::panel));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f);
        g.setColour(isSelected ? theme::accent : theme::outline);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f, 1.0f);

        g.setColour(theme::text);
        g.setFont(theme::font(11.5f, index == 0 ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(track.name, nameArea, juce::Justification::centred, 2);

        const auto light = ledArea.toFloat().withSizeKeepingCentre(9.0f, 9.0f);
        const auto solo = track.solo.load();
        g.setColour(solo ? theme::accent : (track.muted.load() ? theme::ledOff : theme::led));
        g.fillEllipse(light);
        g.setColour(theme::outline);
        g.drawEllipse(light, 1.0f);

        g.setColour(theme::textDim);
        g.setFont(theme::font(11.0f, juce::Font::bold));
        g.drawText(index == 0 ? juce::String("M") : juce::String(index), numberArea, juce::Justification::centred);
        paintRoute(g);

        g.setColour(theme::lcd);
        g.fillRect(meterArea);
        const auto meter = [&](juce::Rectangle<int> bar, float level)
        {
            const auto db = juce::Decibels::gainToDecibels(level, -60.0f);
            const auto height = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f) * float(bar.getHeight());
            const auto filled = bar.toFloat().removeFromBottom(height);
            juce::ColourGradient gradient(theme::meterHigh, 0.0f, float(bar.getY()), theme::meterLow, 0.0f, float(bar.getBottom()), false);
            gradient.addColour(0.25, theme::meterMid);
            g.setGradientFill(gradient);
            g.fillRect(filled);
        };
        auto bars = meterArea.reduced(1);
        meter(bars.removeFromLeft(bars.getWidth() / 2).withTrimmedRight(1), levelLeft);
        meter(bars, levelRight);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (routeArea.contains(e.getPosition()) && mixer.selected != index && mixer.selected != 0)
        {
            routeClicked(e);
            return;
        }
        mixer.selectTrack(index);
        auto& track = *studio.project.mixer[size_t(index)];
        if (ledArea.contains(e.getPosition()))
        {
            if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
                track.solo.store(!track.solo.load());
            else
                track.muted.store(!track.muted.load());
            studio.project.dirty = true;
            repaint();
            return;
        }
        if (e.mods.isPopupMenu())
            rename();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragLevel < 0.0f)
            return;
        const auto level = juce::jlimit(0.0f, 2.0f, dragLevel - float(e.getDistanceFromDragStartY()) / 120.0f);
        auto& sends = studio.project.mixer[size_t(mixer.selected)]->sends;
        {
            const juce::ScopedLock sl(studio.project.lock);
            if (const auto found = sends.find(index); found != sends.end())
                found->second.level = level;
        }
        studio.project.dirty = true;
        repaint(routeArea);
    }

    void mouseUp(const juce::MouseEvent&) override { dragLevel = -1.0f; }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (nameArea.contains(e.getPosition()))
            rename();
    }

private:
    const Send* incoming() const
    {
        if (mixer.selected == index || mixer.selected == 0)
            return nullptr;
        const auto& sends = studio.project.mixer[size_t(mixer.selected)]->sends;
        const auto found = sends.find(index);
        return found != sends.end() ? &found->second : nullptr;
    }

    void paintRoute(juce::Graphics& g) const
    {
        if (mixer.selected == index || mixer.selected == 0)
            return;
        const auto* send = incoming();
        const auto possible = send != nullptr || studio.project.canRoute(mixer.selected, index);
        auto area = routeArea.toFloat().withSizeKeepingCentre(22.0f, 14.0f);
        juce::Path arrow;
        arrow.addTriangle(area.getX(), area.getY(), area.getRight(), area.getY(), area.getCentreX(), area.getBottom());
        if (send != nullptr)
        {
            const auto colour = send->sidechain ? theme::selection : theme::accent;
            g.setColour(colour.withAlpha(0.25f + 0.75f * juce::jlimit(0.0f, 1.0f, send->level)));
            g.fillPath(arrow);
            g.setColour(colour);
            g.strokePath(arrow, juce::PathStrokeType(1.2f));
        }
        else
        {
            g.setColour(possible ? theme::textDim.withAlpha(0.6f) : theme::outline);
            g.strokePath(arrow, juce::PathStrokeType(1.0f));
        }
    }

    void routeClicked(const juce::MouseEvent& e)
    {
        auto& project = studio.project;
        const auto from = mixer.selected;
        const auto* send = incoming();
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addSectionHeader(project.mixer[size_t(from)]->name + "  ->  " + project.mixer[size_t(index)]->name);
            menu.addItem("Route to this track only", project.canRoute(from, index), false, [this, from] { studio.project.routeOnly(from, index); studio.project.touch(); });
            menu.addItem("Sidechain to this track", project.canRoute(from, index) || send != nullptr, send != nullptr && send->sidechain,
                         [this, from] { studio.project.route(from, index, 1.0f, true); studio.project.touch(); });
            menu.addItem("Route with send level", project.canRoute(from, index) || send != nullptr, send != nullptr && !send->sidechain,
                         [this, from] { studio.project.route(from, index, 1.0f, false); studio.project.touch(); });
            menu.addItem("Remove route", send != nullptr, false, [this, from] { studio.project.unroute(from, index); studio.project.touch(); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            return;
        }
        if (send != nullptr)
        {
            dragLevel = send->level;
            if (e.getNumberOfClicks() > 1)
                studio.project.unroute(from, index);
            studio.project.touch();
            return;
        }
        if (studio.project.route(from, index, 1.0f, false))
            studio.project.touch();
    }

    void automationMenu(AutomationTarget::Kind kind)
    {
        juce::PopupMenu menu;
        menu.addItem("Create automation clip", [this, kind]
        {
            AutomationTarget target;
            target.kind = kind;
            target.track = index;
            const auto& track = *studio.project.mixer[size_t(index)];
            const auto value = kind == AutomationTarget::Kind::mixerVolume ? AutomationTarget::mixerNormal(track.volume.load()) : (track.pan.load() + 1.0f) * 0.5f;
            if (mixer.actions.createAutomation)
                mixer.actions.createAutomation(target, value);
        });
        menu.showMenuAsync({});
    }

    void rename()
    {
        ui::askText("Rename mixer track", studio.project.mixer[size_t(index)]->name, [this](const juce::String& name)
        {
            studio.project.mixer[size_t(index)]->name = name.isNotEmpty() ? name : Project::defaultInsertName(index);
            studio.project.touch();
        });
    }

    Mixer& mixer;
    Studio& studio;
    const int index;
    juce::Slider fader, pan;
    ui::RightClick faderMenu { fader, [this] { automationMenu(AutomationTarget::Kind::mixerVolume); } };
    ui::RightClick panMenu { pan, [this] { automationMenu(AutomationTarget::Kind::mixerPan); } };
    juce::Rectangle<int> nameArea, ledArea, numberArea, meterArea, routeArea;
    float levelLeft = 0.0f, levelRight = 0.0f, dragLevel = -1.0f;
};

class Mixer::Slots final : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit Slots(Mixer& m) : mixer(m), studio(m.studio)
    {
        setTooltip("Effect slots: click an empty slot to add an effect, click a filled slot to open it, right-click for options, or drop from the browser");
        ui::setupKnob(width, -1.0, 1.0, 0.0, 0.0, [this](double v)
        {
            studio.project.mixer[size_t(mixer.selected)]->width.store(float(1.0 + v));
            studio.project.dirty = true;
        }, "Stereo separation: left = mono, centre = unchanged, right = wider");
        addAndMakeVisible(width);
        for (auto* button : { &phase, &swap })
        {
            button->setClickingTogglesState(true);
            button->setWantsKeyboardFocus(false);
            addAndMakeVisible(button);
        }
        phase.setTooltip("Invert polarity");
        swap.setTooltip("Swap left and right");
        phase.onClick = [this] { studio.project.mixer[size_t(mixer.selected)]->invertPhase.store(phase.getToggleState()); studio.project.dirty = true; };
        swap.onClick = [this] { studio.project.mixer[size_t(mixer.selected)]->swapSides.store(swap.getToggleState()); studio.project.dirty = true; };
    }

    void refresh()
    {
        const auto& track = *studio.project.mixer[size_t(mixer.selected)];
        width.setValue(track.width.load() - 1.0, juce::dontSendNotification);
        phase.setToggleState(track.invertPhase.load(), juce::dontSendNotification);
        swap.setToggleState(track.swapSides.load(), juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto row = juce::Rectangle<int>(8, rowBounds(effectSlotCount - 1).getBottom() + 12, getWidth() - 16, 40);
        width.setBounds(row.removeFromLeft(44).withSizeKeepingCentre(34, 34));
        row.removeFromLeft(56);
        phase.setBounds(row.removeFromLeft(56).withSizeKeepingCentre(52, 24));
        row.removeFromLeft(6);
        swap.setBounds(row.removeFromLeft(56).withSizeKeepingCentre(52, 24));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::panelDark);
        g.setColour(theme::outline);
        g.drawVerticalLine(0, 0.0f, float(getHeight()));
        const auto& track = *studio.project.mixer[size_t(mixer.selected)];
        g.setColour(theme::text);
        g.setFont(theme::font(13.0f, juce::Font::bold));
        g.drawText(track.name, getLocalBounds().removeFromTop(28).reduced(10, 0), juce::Justification::centredLeft, true);

        for (int slot = 0; slot < effectSlotCount; ++slot)
        {
            auto row = rowBounds(slot);
            const auto& device = track.slots[size_t(slot)];
            g.setColour(theme::button.darker(device != nullptr ? 0.0f : 0.3f));
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
            g.setColour(theme::outline);
            g.drawRoundedRectangle(row.toFloat().reduced(0.5f), 3.0f, 1.0f);
            const auto light = row.removeFromLeft(18).toFloat().withSizeKeepingCentre(8.0f, 8.0f);
            const auto active = device != nullptr && !device->bypassed.load();
            g.setColour(active ? theme::led : theme::ledOff);
            g.fillEllipse(light);
            g.setColour(device != nullptr ? theme::text : theme::textDim);
            g.setFont(theme::font(12.5f));
            g.drawText(device != nullptr ? device->getName() : "Slot " + juce::String(slot + 1), row.reduced(4, 0).withTrimmedRight(26), juce::Justification::centredLeft, true);
            if (device != nullptr)
            {
                const auto mix = row.removeFromRight(24).reduced(4, 6);
                g.setColour(theme::lcd);
                g.fillRect(mix);
                g.setColour(theme::accent);
                g.fillRect(mix.withWidth(juce::roundToInt(float(mix.getWidth()) * device->mix.load())));
            }
        }

        g.setColour(theme::textDim);
        g.setFont(theme::font(10.5f, juce::Font::bold));
        g.drawText("STEREO", juce::Rectangle<int>(54, rowBounds(effectSlotCount - 1).getBottom() + 12, 56, 40), juce::Justification::centredLeft);
        g.setFont(theme::font(12.0f));
        const auto& master = *studio.project.mixer[size_t(mixer.selected)];
        juce::String routes;
        for (const auto& [target, send] : master.sends)
            routes << (routes.isEmpty() ? "" : ", ") << studio.project.mixer[size_t(target)]->name << (send.sidechain ? " (sidechain)" : "")
                   << (std::abs(send.level - 1.0f) > 0.01f ? " " + juce::String(juce::roundToInt(send.level * 100.0f)) + "%" : juce::String());
        auto footer = getLocalBounds().removeFromBottom(46).reduced(10, 0);
        g.drawFittedText(mixer.selected == 0 ? juce::String("Output: audio device") : "Sends to: " + (routes.isEmpty() ? juce::String("nothing") : routes),
                         footer.removeFromTop(22), juce::Justification::centredLeft, 2);
        g.drawText("Volume " + gainText(master.volume.load()), footer, juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto slot = slotAt(e.getPosition());
        if (slot < 0)
            return;
        const auto track = mixer.selected;
        auto& device = studio.project.mixer[size_t(track)]->slots[size_t(slot)];
        auto row = rowBounds(slot);
        if (device != nullptr && row.removeFromLeft(18).contains(e.getPosition()))
        {
            device->bypassed.store(!device->bypassed.load());
            studio.project.dirty = true;
            repaint();
            return;
        }
        if (device != nullptr && row.removeFromRight(24).contains(e.getPosition()))
        {
            mixDrag = slot;
            setMix(e.getPosition());
            return;
        }
        if (device != nullptr && !e.mods.isPopupMenu())
        {
            mixer.actions.showDevice(*device, studio.project.mixer[size_t(track)]->name + " - " + device->getName());
            return;
        }

        juce::PopupMenu menu;
        juce::PopupMenu choose;
        ui::addDeviceItems(choose, studio, false, [this, track, slot](std::unique_ptr<Device> next)
        {
            if (next != nullptr)
                studio.setSlot(track, slot, std::move(next));
        });
        if (device == nullptr)
        {
            choose.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            return;
        }
        menu.addSubMenu("Replace", choose);
        menu.addItem("Bypass", true, device->bypassed.load(), [this, track, slot]
        {
            auto& target = studio.project.mixer[size_t(track)]->slots[size_t(slot)];
            if (target != nullptr)
                target->bypassed.store(!target->bypassed.load());
            repaint();
        });
        menu.addItem("Move up", slot > 0, false, [this, track, slot] { studio.moveSlot(track, slot, -1); });
        menu.addItem("Move down", slot < effectSlotCount - 1, false, [this, track, slot] { studio.moveSlot(track, slot, 1); });
        menu.addSeparator();
        menu.addItem("Delete", [this, track, slot] { studio.setSlot(track, slot, nullptr); });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mixDrag >= 0)
            setMix(e.getPosition());
    }

    void mouseUp(const juce::MouseEvent&) override { mixDrag = -1; }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        const auto text = details.description.toString();
        return text.startsWith("fx:") || text.startsWith("plugin:");
    }

    void itemDropped(const SourceDetails& details) override
    {
        const auto slot = slotAt(details.localPosition);
        if (slot < 0)
            return;
        const auto text = details.description.toString();
        const auto value = text.fromFirstOccurrenceOf(":", false, false);
        std::unique_ptr<Device> device;
        if (text.startsWith("fx:"))
        {
            device = studio.createBuiltin(value);
        }
        else if (const auto type = studio.plugins.known.getTypeForIdentifierString(value))
        {
            juce::String error;
            device = studio.createPlugin(*type, error);
            if (device == nullptr)
                ui::showMessage("Plugin error", "Could not load " + type->name + "\n" + error);
        }
        if (device != nullptr)
            studio.setSlot(mixer.selected, slot, std::move(device));
    }

private:
    juce::Rectangle<int> rowBounds(int slot) const
    {
        return { 8, 32 + slot * (slotHeight + 3), getWidth() - 16, slotHeight };
    }

    int slotAt(juce::Point<int> point) const
    {
        for (int slot = 0; slot < effectSlotCount; ++slot)
            if (rowBounds(slot).contains(point))
                return slot;
        return -1;
    }

    void setMix(juce::Point<int> point)
    {
        auto& device = studio.project.mixer[size_t(mixer.selected)]->slots[size_t(mixDrag)];
        if (device == nullptr)
            return;
        const auto area = rowBounds(mixDrag).removeFromRight(24).reduced(4, 6);
        device->mix.store(juce::jlimit(0.0f, 1.0f, float(point.x - area.getX()) / float(area.getWidth())));
        studio.project.dirty = true;
        repaint();
    }

    Mixer& mixer;
    Studio& studio;
    int mixDrag = -1;
    juce::Slider width;
    juce::TextButton phase { "Phase" }, swap { "Swap" };
};

Mixer::Mixer(Studio& s, Actions& a) : studio(s), actions(a), slots(std::make_unique<Slots>(*this))
{
    for (int i = 0; i < mixerTrackCount; ++i)
        holder.addAndMakeVisible(strips.add(new Strip(*this, i)));
    viewport.setViewedComponent(&holder, false);
    viewport.setScrollBarsShown(false, true);
    viewport.setScrollBarThickness(10);
    addAndMakeVisible(viewport);
    addAndMakeVisible(*slots);
    slots->refresh();
    studio.project.addChangeListener(this);
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

bool Mixer::keyPressed(const juce::KeyPress& key)
{
    auto& project = studio.project;
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    auto& track = *project.mixer[size_t(selected)];
    if (code == 'S' && !mods.isCommandDown())
    {
        const auto alone = track.solo.load() && std::none_of(project.mixer.begin(), project.mixer.end(), [&track](const auto& t) { return t.get() != &track && t->solo.load(); });
        for (auto& other : project.mixer)
            other->solo.store(!alone && other.get() == &track);
    }
    else if (code == juce::KeyPress::F2Key)
    {
        ui::askText("Rename mixer track", track.name, [this, index = selected](const juce::String& name)
        {
            studio.project.mixer[size_t(index)]->name = name.isNotEmpty() ? name : Project::defaultInsertName(index);
            studio.project.touch();
        });
        return true;
    }
    else if (mods.isCommandDown() && code == 'L')
    {
        if (auto* channel = project.findChannel(project.selectedChannel))
            channel->insert.store(selected);
    }
    else if (mods.isAltDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        selectTrack(juce::jlimit(0, mixerTrackCount - 1, selected + (code == juce::KeyPress::leftKey ? -1 : 1)));
    else
        return false;
    project.dirty = true;
    project.sendChangeMessage();
    return true;
}

Mixer::~Mixer()
{
    studio.project.removeChangeListener(this);
}

void Mixer::paint(juce::Graphics& g)
{
    g.fillAll(theme::workspace.brighter(0.05f));
}

void Mixer::resized()
{
    auto area = getLocalBounds();
    slots->setBounds(area.removeFromRight(slotsWidth));
    viewport.setBounds(area);
    const auto height = juce::jmax(160, area.getHeight() - viewport.getScrollBarThickness());
    holder.setSize(masterWidth + (mixerTrackCount - 1) * stripWidth + 8, height);
    auto x = 4;
    for (int i = 0; i < strips.size(); ++i)
    {
        const auto width = i == 0 ? masterWidth : stripWidth;
        strips[i]->setBounds(x, 2, width - 2, height - 4);
        x += width + (i == 0 ? 6 : 0);
    }
}

void Mixer::selectTrack(int track)
{
    selected = juce::jlimit(0, mixerTrackCount - 1, track);
    holder.repaint();
    slots->refresh();
    const auto bounds = strips[selected]->getBounds();
    const auto view = viewport.getViewArea();
    if (bounds.getX() < view.getX() || bounds.getRight() > view.getRight())
        viewport.setViewPosition(juce::jmax(0, bounds.getX() - view.getWidth() / 3), 0);
}

void Mixer::changeListenerCallback(juce::ChangeBroadcaster*)
{
    for (auto* strip : strips)
        strip->refresh();
    slots->refresh();
}

void Mixer::timerCallback()
{
    const auto view = viewport.getViewArea();
    for (auto* strip : strips)
        if (strip->getBounds().intersects(view))
            strip->updateMeters();
}
}
