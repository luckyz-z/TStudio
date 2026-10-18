#include "UI/ChannelRack.h"
#include "UI/Browser.h"
#include "UI/Controls.h"
#include "UI/Theme.h"
#include "Engine/Builtins/Sampler.h"

namespace ts
{
namespace
{
constexpr int rowHeight = 28;
constexpr int headerHeight = 28;
constexpr int footerHeight = 28;
constexpr int stepWidth = 16;
constexpr int stepGap = 2;
constexpr int groupGap = 5;
constexpr int stepsLeft = 236;
constexpr int selectWidth = 22;

int stepsWidth(int steps)
{
    return steps * stepWidth + (steps - 1) * stepGap + (steps / 4 - 1) * groupGap;
}

bool isStepNote(const Note& note)
{
    return note.start % ticksPerStep == 0 && note.length == ticksPerStep && note.key == defaultKey;
}
}

class ChannelRack::Row final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Row(ChannelRack& r, Channel& c) : rack(r), studio(r.studio), id(c.id)
    {
        enabled.setWantsKeyboardFocus(false);
        enabled.setTooltip("Channel on/off");
        enabled.setToggleState(!channel().muted.load(), juce::dontSendNotification);
        enabled.onClick = [this]
        {
            channel().muted.store(!enabled.getToggleState());
            studio.project.dirty = true;
            repaint();
        };
        ui::setupKnob(pan, -1.0, 1.0, channel().pan.load(), 0.0, [this](double v) { channel().pan.store(float(v)); studio.project.dirty = true; }, "Channel panning");
        ui::setupKnob(volume, 0.0, 1.0, channel().volume.load(), defaultChannelVolume, [this](double v) { channel().volume.store(float(v)); studio.project.dirty = true; },
                      "Channel volume");
        addAndMakeVisible(enabled);
        addAndMakeVisible(pan);
        addAndMakeVisible(volume);
    }

    int getId() const noexcept { return id; }

    void refresh()
    {
        enabled.setToggleState(!channel().muted.load(), juce::dontSendNotification);
        pan.setValue(channel().pan.load(), juce::dontSendNotification);
        volume.setValue(channel().volume.load(), juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        enabled.setBounds(area.removeFromLeft(20));
        pan.setBounds(area.removeFromLeft(26).reduced(1, 2));
        volume.setBounds(area.removeFromLeft(26).reduced(1, 2));
        area.removeFromLeft(4);
        insertArea = area.removeFromLeft(28).reduced(0, 4);
        area.removeFromLeft(6);
        nameArea = area.removeFromLeft(stepsLeft - area.getX() - 8).reduced(0, 3);
        stepArea = { stepsLeft, 0, stepsWidth(rack.steps), getHeight() };
        selectArea = { stepArea.getRight() + 6, 0, selectWidth - 6, getHeight() };
    }

    void paint(juce::Graphics& g) override
    {
        const auto selected = studio.project.selectedChannel == id;
        paintInsert(g);
        paintName(g, selected);
        if (isStepMode())
            for (int step = 0; step < rack.steps; ++step)
                paintStep(g, step);
        else
            paintPreview(g);

        const auto light = selectArea.toFloat().withSizeKeepingCentre(5.0f, float(getHeight()) - 8.0f);
        g.setColour(selected ? channel().colour.brighter(0.6f).withSaturation(0.45f) : theme::rackBox.darker(0.35f));
        g.fillRoundedRectangle(light, 1.5f);
        g.setColour(theme::outline.withAlpha(0.6f));
        g.drawRoundedRectangle(light, 1.5f, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto position = e.getPosition();
        painting = false;
        if (selectArea.contains(position))
        {
            studio.selectChannel(id);
            return;
        }
        if (insertArea.contains(position))
        {
            insertDragStart = channel().insert.load();
            return;
        }
        if (nameArea.contains(position))
        {
            studio.selectChannel(id);
            if (e.mods.isPopupMenu())
                showMenu();
            else if (channel().device != nullptr)
                rack.actions.showDevice(*channel().device, channel().name);
            return;
        }
        if (!stepArea.contains(position))
            return;
        if (!isStepMode())
        {
            rack.actions.openPianoRoll(id);
            return;
        }
        const auto step = stepAt(position);
        if (step < 0)
            return;
        studio.project.checkpoint();
        painting = true;
        paintValue = !e.mods.isPopupMenu() && !isStepOn(step);
        setStep(step, paintValue);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (insertArea.contains(e.getMouseDownPosition()))
        {
            setInsert(insertDragStart - e.getDistanceFromDragStartY() / 6);
            return;
        }
        if (const auto step = stepAt(e.getPosition()); painting && step >= 0)
            setStep(step, paintValue);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (insertArea.contains(e.getPosition()))
            setInsert(studio.project.nextFreeInsert());
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (insertArea.contains(e.getPosition()))
            setInsert(channel().insert.load() + (wheel.deltaY > 0.0f ? 1 : -1));
        else
            Component::mouseWheelMove(e, wheel);
    }

    juce::String getTooltip() override
    {
        const auto position = getMouseXYRelative();
        if (insertArea.contains(position))
            return "Target mixer track: drag or wheel to change, double-click for a free insert";
        if (nameArea.contains(position))
            return channel().name + ": click to show the plugin, right-click for options";
        if (stepArea.contains(position))
            return isStepMode() ? "Step sequencer: click to toggle, drag to paint, right-drag to erase" : "Piano roll preview: click to edit";
        if (selectArea.contains(position))
            return "Select channel (receives keyboard and MIDI input)";
        return {};
    }

    void setPlayhead(int step)
    {
        if (step == playhead)
            return;
        if (playhead >= 0)
            repaint(stepBounds(playhead).expanded(2));
        playhead = step;
        if (playhead >= 0)
            repaint(stepBounds(playhead).expanded(2));
    }

private:
    const std::vector<Note>* notes() const
    {
        const auto& pattern = studio.project.pattern();
        const auto found = pattern.notes.find(id);
        return found != pattern.notes.end() ? &found->second : nullptr;
    }

    bool isStepMode() const
    {
        const auto* list = notes();
        return list == nullptr || std::all_of(list->begin(), list->end(), isStepNote);
    }

    bool isStepOn(int step) const
    {
        const auto* list = notes();
        return list != nullptr && std::any_of(list->begin(), list->end(), [step](const Note& n) { return n.start == step * ticksPerStep; });
    }

    void setStep(int step, bool on)
    {
        if (isStepOn(step) == on)
            return;
        {
            const juce::ScopedLock sl(studio.project.lock);
            auto& list = studio.project.pattern().notes[id];
            if (on)
                list.push_back({ step * ticksPerStep, ticksPerStep, defaultKey, 0.78f });
            else
                list.erase(std::remove_if(list.begin(), list.end(), [step](const Note& n) { return n.start == step * ticksPerStep; }), list.end());
        }
        studio.project.touch();
    }

    void fillEvery(int interval)
    {
        studio.project.checkpoint();
        {
            const juce::ScopedLock sl(studio.project.lock);
            auto& list = studio.project.pattern().notes[id];
            list.clear();
            for (int step = 0; step < rack.steps; step += interval)
                list.push_back({ step * ticksPerStep, ticksPerStep, defaultKey, 0.78f });
        }
        studio.project.touch();
    }

    void setInsert(int insert)
    {
        channel().insert.store(juce::jlimit(0, mixerTrackCount - 1, insert));
        studio.project.dirty = true;
        repaint(insertArea);
    }

    juce::Rectangle<int> stepBounds(int step) const
    {
        return { stepArea.getX() + step * (stepWidth + stepGap) + (step / 4) * groupGap, 3, stepWidth, getHeight() - 6 };
    }

    int stepAt(juce::Point<int> point) const
    {
        for (int step = 0; step < rack.steps; ++step)
            if (stepBounds(step).expanded(stepGap / 2 + 1, 4).contains(point))
                return step;
        return -1;
    }

    void paintInsert(juce::Graphics& g) const
    {
        const auto insert = channel().insert.load();
        g.setColour(theme::rackBox);
        g.fillRoundedRectangle(insertArea.toFloat(), 3.0f);
        g.setColour(theme::outline.withAlpha(0.7f));
        g.drawRoundedRectangle(insertArea.toFloat().reduced(0.5f), 3.0f, 1.0f);
        g.setColour(insert == 0 ? theme::textDim : theme::text.withAlpha(0.85f));
        g.setFont(theme::font(14.0f));
        g.drawText(insert == 0 ? juce::String("--") : juce::String(insert), insertArea, juce::Justification::centred);
    }

    void paintName(juce::Graphics& g, bool selected) const
    {
        const auto box = nameArea.toFloat();
        const auto fill = channel().colour.withSaturation(juce::jmin(channel().colour.getSaturation(), 0.42f)).withBrightness(0.5f).interpolatedWith(theme::button, 0.3f);
        g.setGradientFill(juce::ColourGradient(fill.brighter(0.12f), box.getTopLeft(), fill.darker(0.18f), box.getBottomLeft(), false));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawHorizontalLine(int(box.getY()) + 1, box.getX() + 3.0f, box.getRight() - 3.0f);
        g.setColour(selected ? theme::text : theme::outline);
        g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, selected ? 1.4f : 1.0f);
        g.setColour(channel().muted.load() ? theme::textDim : theme::text);
        g.setFont(theme::font(13.0f));
        g.drawText(channel().name, nameArea.reduced(8, 0), juce::Justification::centred, true);
    }

    void paintStep(juce::Graphics& g, int step) const
    {
        const auto box = stepBounds(step).toFloat();
        const auto on = isStepOn(step);
        const auto base = on ? theme::stepOn : ((step / 4) % 2 == 0 ? theme::stepA : theme::stepB);
        g.setColour(theme::outline.withAlpha(0.55f));
        g.fillRoundedRectangle(box.translated(0.0f, 1.0f), 3.0f);
        g.setGradientFill(juce::ColourGradient(base.brighter(0.14f), box.getTopLeft(), base.darker(0.12f), box.getBottomLeft(), false));
        g.fillRoundedRectangle(box, 3.0f);
        const auto notch = box.withSizeKeepingCentre(box.getWidth() * 0.42f, 2.0f).withY(box.getY() + 4.0f);
        g.setColour(base.darker(on ? 0.35f : 0.6f));
        g.fillRect(notch);
        g.setColour(base.brighter(0.35f).withAlpha(0.5f));
        g.fillRect(notch.translated(0.0f, 2.0f).withHeight(1.0f));
        g.setColour(step == playhead ? theme::accent : theme::outline);
        g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, step == playhead ? 1.6f : 1.0f);
    }

    void paintPreview(juce::Graphics& g) const
    {
        const auto box = stepArea.toFloat().reduced(0.0f, 3.0f);
        g.setColour(theme::lcd);
        g.fillRoundedRectangle(box, 3.0f);
        const auto* list = notes();
        if (list == nullptr || list->empty())
            return;
        const auto [low, high] = std::minmax_element(list->begin(), list->end(), [](const Note& a, const Note& b) { return a.key < b.key; });
        const auto span = float(juce::jmax(1, high->key - low->key));
        const auto length = float(rack.steps * ticksPerStep);
        g.setColour(channel().colour.brighter(0.4f));
        for (const auto& note : *list)
        {
            const auto x = box.getX() + box.getWidth() * float(note.start) / length;
            const auto w = juce::jmax(2.0f, box.getWidth() * float(note.length) / length - 1.0f);
            const auto y = box.getBottom() - 4.0f - (box.getHeight() - 9.0f) * float(note.key - low->key) / span;
            g.fillRect(x, y - 2.0f, w, 3.0f);
        }
        if (playhead >= 0)
        {
            g.setColour(theme::playhead);
            g.drawVerticalLine(int(box.getX() + box.getWidth() * float(playhead) / float(rack.steps)), box.getY(), box.getBottom());
        }
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem("Piano roll", [this] { rack.actions.openPianoRoll(id); });
        menu.addItem("Show plugin", channel().device != nullptr, false, [this] { rack.actions.showDevice(*channel().device, channel().name); });
        menu.addItem("Channel settings...", [this] { if (rack.actions.showChannelSettings) rack.actions.showChannelSettings(id); });
        menu.addSeparator();
        menu.addItem("Rename...", [this]
        {
            ui::askText("Rename channel", channel().name, [this](const juce::String& name)
            {
                if (name.isEmpty())
                    return;
                channel().name = name;
                studio.project.touch();
            });
        });
        menu.addItem("Colour...", [this]
        {
            ui::pickColour(*this, channel().colour, [safe = juce::Component::SafePointer<Row>(this)](juce::Colour colour)
            {
                if (safe == nullptr)
                    return;
                safe->channel().colour = colour;
                safe->studio.project.touch();
            });
        });
        juce::PopupMenu replace;
        ui::addDeviceItems(replace, studio, true, [this, id = id](std::unique_ptr<Device> device)
        {
            if (device != nullptr)
                studio.setChannelDevice(id, std::move(device));
        });
        menu.addSubMenu("Replace", replace);
        if (channel().device != nullptr && dynamic_cast<Sampler*>(channel().device->processor.get()) != nullptr)
            menu.addItem("Load sample...", [this] { rack.actions.showDevice(*channel().device, channel().name); });
        menu.addSeparator();
        juce::PopupMenu fill;
        for (const auto interval : { 1, 2, 4, 8 })
            fill.addItem("Every " + juce::String(interval) + (interval == 1 ? " step" : " steps"), [this, interval] { fillEvery(interval); });
        menu.addSubMenu("Fill", fill);
        menu.addItem("Clear notes", [this]
        {
            studio.project.checkpoint();
            {
                const juce::ScopedLock sl(studio.project.lock);
                studio.project.pattern().notes.erase(id);
            }
            studio.project.touch();
        });
        menu.addSeparator();
        menu.addItem("Route to free insert", [this] { setInsert(studio.project.nextFreeInsert()); });
        menu.addItem("Show in mixer", [this] { rack.actions.showMixerTrack(channel().insert.load()); });
        menu.addSeparator();
        menu.addItem("Move up", [this, id = id] { studio.moveChannel(id, -1); });
        menu.addItem("Move down", [this, id = id] { studio.moveChannel(id, 1); });
        menu.addItem("Clone", [this, id = id] { studio.cloneChannel(id); });
        menu.addItem("Delete", [this, id = id]
        {
            juce::MessageManager::callAsync([&s = studio, id] { s.removeChannel(id); });
        });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    ChannelRack& rack;
    Studio& studio;
    int id;

    Channel& channel() const
    {
        if (auto* found = studio.project.findChannel(id))
            return *found;
        static Channel placeholder;
        return placeholder;
    }
    juce::ToggleButton enabled;
    juce::Slider pan, volume;
    ui::RightClick panMenu { pan, [this] { automationMenu(AutomationTarget::Kind::channelPan); } };
    ui::RightClick volumeMenu { volume, [this] { automationMenu(AutomationTarget::Kind::channelVolume); } };

    void automationMenu(AutomationTarget::Kind kind)
    {
        juce::PopupMenu menu;
        menu.addItem("Create automation clip", [this, kind]
        {
            AutomationTarget target;
            target.kind = kind;
            target.channel = id;
            const auto value = kind == AutomationTarget::Kind::channelVolume ? channel().volume.load() : (channel().pan.load() + 1.0f) * 0.5f;
            if (rack.actions.createAutomation)
                rack.actions.createAutomation(target, value);
        });
        menu.showMenuAsync({});
    }
    juce::Rectangle<int> insertArea, nameArea, stepArea, selectArea;
    bool paintValue = true;
    bool painting = false;
    int playhead = -1;
    int insertDragStart = 0;
};

ChannelRack::ChannelRack(Studio& s, Actions& a) : studio(s), actions(a)
{
    viewport.setViewedComponent(&rowHolder, false);
    viewport.setScrollBarsShown(true, true);
    viewport.setScrollBarThickness(10);
    addAndMakeVisible(viewport);

    ui::setupKnob(swing, 0.0, 1.0, studio.project.swing.load(), 0.0, [this](double v) { studio.project.swing.store(float(v)); studio.project.dirty = true; },
                  "Main swing");
    addAndMakeVisible(swing);
    add.setTooltip("Add a channel");
    add.setWantsKeyboardFocus(false);
    add.onClick = [this] { showAddMenu(); };
    addAndMakeVisible(add);

    setWantsKeyboardFocus(true);
    rebuildRows();
    studio.project.addChangeListener(this);
    startTimerHz(30);
}

ChannelRack::~ChannelRack()
{
    studio.project.removeChangeListener(this);
}

int ChannelRack::visibleSteps() const
{
    return juce::jmax(stepsPerBar, studio.project.pattern().length() / ticksPerStep);
}

int ChannelRack::preferredWidth() const
{
    return stepsLeft + stepsWidth(16) + selectWidth + 26;
}

int ChannelRack::preferredHeight() const
{
    return headerHeight + footerHeight + rowHeight * juce::jlimit(4, 14, int(rows.size())) + 8;
}

void ChannelRack::paint(juce::Graphics& g)
{
    g.fillAll(theme::rack);
    auto header = getLocalBounds().removeFromTop(headerHeight).reduced(8, 0);
    header.removeFromLeft(30);
    g.setColour(theme::textDim);
    g.setFont(theme::font(11.0f, juce::Font::bold));
    g.drawText("SWING", header.removeFromLeft(44), juce::Justification::centredLeft);
    g.setColour(theme::text);
    g.setFont(theme::font(13.0f));
    g.drawText(studio.project.pattern().name, header, juce::Justification::centredRight, true);
}

void ChannelRack::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(headerHeight).reduced(8, 2);
    swing.setBounds(header.removeFromLeft(26));
    auto footer = area.removeFromBottom(footerHeight).reduced(8, 3);
    add.setBounds(footer.removeFromLeft(stepsLeft - 8).withTrimmedLeft(104));
    viewport.setBounds(area.reduced(4, 0));
    rowHolder.setSize(juce::jmax(viewport.getMaximumVisibleWidth(), stepsLeft + stepsWidth(steps) + selectWidth + 8), rowHeight * rows.size());
    auto rowArea = rowHolder.getLocalBounds();
    for (auto* row : rows)
        row->setBounds(rowArea.removeFromTop(rowHeight));
}

bool ChannelRack::keyPressed(const juce::KeyPress& key)
{
    auto& project = studio.project;
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto selected = [&project]
    {
        for (int i = 0; i < int(project.channels.size()); ++i)
            if (project.channels[size_t(i)]->id == project.selectedChannel)
                return i;
        return -1;
    }();
    const auto count = int(project.channels.size());
    if (count == 0)
        return false;

    if ((code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) && !mods.isCommandDown())
    {
        const auto direction = code == juce::KeyPress::upKey ? -1 : 1;
        if (mods.isAltDown() || mods.isShiftDown())
            studio.moveChannel(project.selectedChannel, direction);
        else
            studio.selectChannel(project.channels[size_t(juce::jlimit(0, count - 1, selected + direction))]->id);
        return true;
    }
    if (mods.isAltDown() && code == 'C')
    {
        studio.cloneChannel(project.selectedChannel);
        return true;
    }
    if (mods.isAltDown() && code == juce::KeyPress::deleteKey)
    {
        juce::MessageManager::callAsync([&s = studio, id = project.selectedChannel] { s.removeChannel(id); });
        return true;
    }

    const auto digit = code >= '0' && code <= '9' ? (code == '0' ? 9 : code - '1') : -1;
    if (digit < 0 || digit >= count || mods.isAltDown() || mods.isShiftDown())
        return false;
    if (!mods.isCommandDown() && actions.typingKeyboardActive && actions.typingKeyboardActive())
        return false;
    auto& target = *project.channels[size_t(digit)];
    if (mods.isCommandDown())
    {
        const auto alone = !target.muted.load()
                           && std::all_of(project.channels.begin(), project.channels.end(), [&target](const auto& c) { return c.get() == &target || c->muted.load(); });
        for (auto& channel : project.channels)
            channel->muted.store(alone ? false : channel.get() != &target);
    }
    else
        target.muted.store(!target.muted.load());
    project.dirty = true;
    project.sendChangeMessage();
    return true;
}

void ChannelRack::rebuildRows()
{
    rows.clear();
    rowIds.clear();
    steps = visibleSteps();
    for (auto& channel : studio.project.channels)
    {
        rowHolder.addAndMakeVisible(rows.add(new Row(*this, *channel)));
        rowIds.push_back(channel->id);
    }
    resized();
    if (onPreferredSizeChanged)
        onPreferredSizeChanged();
}

void ChannelRack::changeListenerCallback(juce::ChangeBroadcaster*)
{
    std::vector<int> ids;
    for (const auto& channel : studio.project.channels)
        ids.push_back(channel->id);
    if (ids != rowIds || visibleSteps() != steps)
    {
        rebuildRows();
    }
    else
    {
        for (auto* row : rows)
            row->refresh();
    }
    swing.setValue(studio.project.swing.load(), juce::dontSendNotification);
    repaint(getLocalBounds().removeFromTop(headerHeight));
}

void ChannelRack::timerCallback()
{
    const auto tick = ui::patternPlayhead(studio);
    const auto step = tick >= 0.0 ? int(tick) / ticksPerStep : -1;
    for (auto* row : rows)
        row->setPlayhead(step);
}

void ChannelRack::showAddMenu()
{
    juce::PopupMenu menu;
    ui::addDeviceItems(menu, studio, true, [this](std::unique_ptr<Device> device)
    {
        if (device == nullptr)
            return;
        const auto name = device->getName();
        studio.addChannel(std::move(device), name);
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&add));
}

bool ChannelRack::isInterestedInDragSource(const SourceDetails& details)
{
    return Browser::isBrowserDrag(details.description);
}

void ChannelRack::itemDropped(const SourceDetails& details)
{
    Browser::activate(studio, details.description.toString());
}

bool ChannelRack::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const juce::String& f) { return Sampler::isAudioFile(juce::File(f)); });
}

void ChannelRack::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& file : files)
        if (Sampler::isAudioFile(juce::File(file)))
            studio.addSampleChannel(juce::File(file));
}
}
