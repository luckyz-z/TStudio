#include "UI/Toolbar.h"
#include "UI/Commands.h"
#include "UI/Community.h"
#include "UI/Controls.h"
#include "UI/Theme.h"

#include <BinaryData.h>

namespace ts
{
namespace
{
juce::Path playShape()
{
    juce::Path path;
    path.addTriangle(0.0f, 0.0f, 0.0f, 12.0f, 10.0f, 6.0f);
    return path;
}

juce::Path stopShape()
{
    juce::Path path;
    path.addRectangle(0.0f, 0.0f, 10.0f, 10.0f);
    return path;
}

juce::Path recordShape()
{
    juce::Path path;
    path.addEllipse(0.0f, 0.0f, 10.0f, 10.0f);
    return path;
}

juce::Path metronomeShape()
{
    juce::Path path;
    path.addTriangle(5.0f, 0.0f, 0.0f, 14.0f, 10.0f, 14.0f);
    path.addLineSegment({ 5.0f, 10.0f, 10.0f, 2.0f }, 1.4f);
    return path;
}

juce::Path keyboardShape()
{
    juce::Path path;
    path.addRectangle(0.0f, 0.0f, 16.0f, 10.0f);
    for (auto x : { 3.0f, 6.5f, 10.0f, 13.5f })
        path.addRectangle(x - 0.6f, 1.0f, 1.2f, 5.0f);
    path.setUsingNonZeroWinding(false);
    return path;
}

juce::Path playlistShape()
{
    juce::Path path;
    path.addRectangle(0.0f, 0.0f, 9.0f, 3.0f);
    path.addRectangle(4.0f, 5.0f, 10.0f, 3.0f);
    path.addRectangle(2.0f, 10.0f, 8.0f, 3.0f);
    return path;
}

juce::Path pianoRollShape()
{
    juce::Path path;
    path.addRectangle(0.0f, 0.0f, 3.0f, 14.0f);
    path.addRectangle(5.0f, 1.0f, 6.0f, 2.5f);
    path.addRectangle(8.0f, 6.0f, 7.0f, 2.5f);
    path.addRectangle(5.0f, 11.0f, 4.0f, 2.5f);
    return path;
}

juce::Path rackShape()
{
    juce::Path path;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            path.addRectangle(float(column) * 4.0f, float(row) * 5.0f, 3.0f, 3.5f);
    return path;
}

juce::Path mixerShape()
{
    juce::Path path;
    for (int i = 0; i < 3; ++i)
    {
        const auto x = float(i) * 5.0f;
        path.addRectangle(x + 1.0f, 0.0f, 1.0f, 14.0f);
        path.addRectangle(x, float(3 + i * 3), 3.0f, 3.0f);
    }
    return path;
}

juce::Path browserShape()
{
    juce::Path path;
    path.addRectangle(0.0f, 0.0f, 6.0f, 3.0f);
    path.addRectangle(0.0f, 2.0f, 14.0f, 10.0f);
    path.addRectangle(1.5f, 4.5f, 11.0f, 6.0f);
    path.setUsingNonZeroWinding(false);
    return path;
}

juce::Path plugShape()
{
    juce::Path path;
    path.addRectangle(3.0f, 0.0f, 1.6f, 4.0f);
    path.addRectangle(8.4f, 0.0f, 1.6f, 4.0f);
    path.addRoundedRectangle(1.0f, 4.0f, 11.0f, 6.0f, 1.5f);
    path.addRectangle(5.5f, 10.0f, 2.0f, 4.0f);
    return path;
}

juce::Path codeShape()
{
    juce::Path path;
    path.addLineSegment({ 4.0f, 2.0f, 0.0f, 7.0f }, 1.6f);
    path.addLineSegment({ 0.0f, 7.0f, 4.0f, 12.0f }, 1.6f);
    path.addLineSegment({ 12.0f, 2.0f, 16.0f, 7.0f }, 1.6f);
    path.addLineSegment({ 16.0f, 7.0f, 12.0f, 12.0f }, 1.6f);
    path.addLineSegment({ 9.5f, 0.5f, 6.5f, 13.5f }, 1.4f);
    return path;
}

juce::Path undoShape()
{
    juce::Path path;
    path.addCentredArc(8.0f, 8.0f, 6.0f, 6.0f, 0.0f, -1.6f, 1.9f, true);
    juce::Path stroke;
    juce::PathStrokeType(1.8f).createStrokedPath(stroke, path);
    stroke.addTriangle(-1.0f, 0.5f, 5.5f, 0.5f, 2.2f, 5.5f);
    return stroke;
}

juce::Path saveShape()
{
    juce::Path path;
    path.addRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 1.5f);
    path.addRectangle(3.0f, 1.5f, 8.0f, 4.0f);
    path.addRectangle(3.0f, 8.0f, 8.0f, 4.5f);
    path.setUsingNonZeroWinding(false);
    return path;
}

juce::Path exportShape()
{
    juce::Path path;
    path.addRectangle(6.0f, 0.0f, 2.0f, 7.0f);
    path.addTriangle(2.5f, 6.0f, 11.5f, 6.0f, 7.0f, 10.5f);
    path.addRectangle(0.0f, 11.5f, 14.0f, 2.0f);
    path.addRectangle(0.0f, 8.0f, 2.0f, 5.5f);
    path.addRectangle(12.0f, 8.0f, 2.0f, 5.5f);
    return path;
}

juce::Path plusShape()
{
    juce::Path path;
    path.addRectangle(4.0f, 0.0f, 2.0f, 10.0f);
    path.addRectangle(0.0f, 4.0f, 10.0f, 2.0f);
    return path;
}

juce::Path starShape()
{
    juce::Path path;
    path.addStar({ 7.0f, 7.0f }, 5, 3.0f, 7.0f);
    return path;
}

bool isTicked(juce::ApplicationCommandManager& commands, juce::CommandID id)
{
    juce::ApplicationCommandInfo info(id);
    if (commands.getTargetForCommand(id, info) == nullptr)
        return false;
    return (info.flags & juce::ApplicationCommandInfo::isTicked) != 0;
}

juce::String findHint()
{
    for (auto* component = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse(); component != nullptr;
         component = component->getParentComponent())
        if (auto* client = dynamic_cast<juce::TooltipClient*>(component))
            if (const auto tip = client->getTooltip(); tip.isNotEmpty())
                return tip;
    return {};
}

void paintPanel(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(theme::lcd);
    g.fillRoundedRectangle(area.toFloat(), 3.0f);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 3.0f, 1.0f);
}
}

class Toolbar::Icon final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Icon(juce::Path p, const juce::String& tip, std::function<void()> click, std::function<bool()> on = {}, juce::Colour onColour = theme::accent)
        : shape(std::move(p)), action(std::move(click)), active(std::move(on)), highlight(onColour)
    {
        setTooltip(tip);
        setWantsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        const auto on = active && active();
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setGradientFill(juce::ColourGradient(on ? highlight.withMultipliedBrightness(0.95f) : theme::button.brighter(isMouseOver() ? 0.12f : 0.0f), 0.0f, bounds.getY(),
                                               on ? highlight.darker(0.35f) : theme::button.darker(0.35f), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(theme::outline);
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        const auto inner = bounds.reduced(bounds.getHeight() * 0.27f);
        g.setColour(on ? juce::Colours::white : (isMouseOver() ? theme::text : theme::text.withAlpha(0.78f)));
        g.fillPath(shape, shape.getTransformToScaleToFit(inner, true));
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (contains(e.getPosition()) && action)
            action();
        repaint();
    }

private:
    juce::Path shape;
    std::function<void()> action;
    std::function<bool()> active;
    juce::Colour highlight;
};

class Toolbar::ModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModeSwitch(Studio& s, juce::ApplicationCommandManager& c) : studio(s), commands(c)
    {
        setTooltip("Pattern / song mode (L)");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        const auto song = studio.project.songMode.load();
        paintPanel(g, getLocalBounds().reduced(0, 2));
        auto area = getLocalBounds().reduced(4, 4);
        const auto row = [&](juce::Rectangle<int> bounds, const juce::String& label, bool on)
        {
            const auto light = bounds.removeFromLeft(9).toFloat().withSizeKeepingCentre(6.0f, 6.0f);
            g.setColour(on ? theme::accent : theme::ledOff.darker());
            g.fillEllipse(light);
            g.setColour(on ? theme::text : theme::textDim);
            g.setFont(theme::font(9.5f, juce::Font::bold));
            g.drawText(label, bounds.withTrimmedLeft(2), juce::Justification::centredLeft);
        };
        row(area.removeFromTop(area.getHeight() / 2), "PAT", !song);
        row(area, "SONG", song);
    }

    void mouseDown(const juce::MouseEvent&) override { commands.invokeDirectly(command::songMode, true); }

private:
    Studio& studio;
    juce::ApplicationCommandManager& commands;
};

class Toolbar::TempoDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TempoDisplay(Studio& s) : studio(s)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setTooltip("Tempo: drag or wheel to change, double-click to reset, right-click to type a value or automate it");
    }

    void paint(juce::Graphics& g) override
    {
        paintPanel(g, getLocalBounds().reduced(0, 2));
        g.setColour(theme::accent);
        g.setFont(theme::font(15.0f, juce::Font::bold));
        g.drawText(juce::String(studio.project.tempo.load(), 3), getLocalBounds().reduced(6, 0), juce::Justification::centredRight);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStart = studio.project.tempo.load();
        if (!e.mods.isPopupMenu())
            return;
        juce::PopupMenu menu;
        menu.addItem("Type in value...", [this]
        {
            ui::askText("Tempo", juce::String(studio.project.tempo.load(), 3), [this](const juce::String& text)
            {
                if (text.getDoubleValue() > 0.0)
                    set(text.getDoubleValue());
            });
        });
        menu.addItem("Create automation clip", [this]
        {
            AutomationTarget target;
            target.kind = AutomationTarget::Kind::tempo;
            const auto normal = float((studio.project.tempo.load() - AutomationTarget::tempoLow) / (AutomationTarget::tempoHigh - AutomationTarget::tempoLow));
            studio.addAutomation(target, "Tempo", juce::jlimit(0.0f, 1.0f, normal));
        });
        menu.showMenuAsync({});
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!e.mods.isPopupMenu())
            set(std::round(dragStart - e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.1 : 0.5)));
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        set(std::round(studio.project.tempo.load()) + (wheel.deltaY > 0.0f ? 1.0 : -1.0));
    }

    void mouseDoubleClick(const juce::MouseEvent&) override { set(AudioEngine::defaultTempo); }

private:
    void set(double bpm)
    {
        studio.project.tempo.store(juce::jlimit(10.0, 522.0, bpm));
        studio.project.dirty = true;
        repaint();
    }

    Studio& studio;
    double dragStart = AudioEngine::defaultTempo;
};

class Toolbar::PatternBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PatternBox(Studio& s, juce::ApplicationCommandManager& c) : studio(s), commands(c)
    {
        setTooltip("Pattern selector: click for the pattern list, wheel to switch");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& project = studio.project;
        const auto& pattern = project.patterns[size_t(project.currentPattern.load())];
        auto area = getLocalBounds().reduced(0, 3);
        paintPanel(g, area);
        g.setColour(pattern.colour);
        g.fillRoundedRectangle(area.removeFromLeft(9).reduced(3, 4).toFloat(), 1.5f);
        g.setColour(theme::text);
        g.setFont(theme::font(13.0f, juce::Font::bold));
        g.drawText(pattern.name, area.reduced(4, 0).withTrimmedRight(14), juce::Justification::centredLeft, true);
        juce::Path arrow;
        arrow.addTriangle(0.0f, 0.0f, 8.0f, 0.0f, 4.0f, 5.0f);
        g.setColour(theme::textDim);
        g.fillPath(arrow, juce::AffineTransform::translation(float(area.getRight() - 13), float(area.getCentreY() - 2)));
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        auto& project = studio.project;
        juce::PopupMenu menu;
        for (int i = 0; i < int(project.patterns.size()); ++i)
        {
            menu.addItem(project.patterns[size_t(i)].name, true, i == project.currentPattern.load(), [this, i]
            {
                studio.project.currentPattern.store(i);
                studio.project.sendChangeMessage();
            });
        }
        menu.addSeparator();
        menu.addCommandItem(&commands, command::newPattern);
        menu.addCommandItem(&commands, command::clonePattern);
        menu.addCommandItem(&commands, command::renamePattern);
        menu.addCommandItem(&commands, command::deletePattern);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        commands.invokeDirectly(wheel.deltaY > 0.0f ? command::previousPattern : command::nextPattern, true);
    }

private:
    Studio& studio;
    juce::ApplicationCommandManager& commands;
};

class Toolbar::Oscilloscope final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Oscilloscope(Studio& s) : studio(s) { setTooltip("Output oscilloscope"); }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().reduced(0, 2);
        paintPanel(g, area);
        std::array<float, 512> samples {};
        studio.engine.output.copyLatest(samples.data(), int(samples.size()));
        juce::Path wave;
        const auto inner = area.toFloat().reduced(3.0f, 2.0f);
        for (size_t i = 0; i < samples.size(); i += 2)
        {
            const auto x = inner.getX() + inner.getWidth() * float(i) / float(samples.size() - 1);
            const auto y = inner.getCentreY() - juce::jlimit(-1.0f, 1.0f, samples[i]) * inner.getHeight() * 0.5f;
            if (i == 0)
                wave.startNewSubPath(x, y);
            else
                wave.lineTo(x, y);
        }
        g.setColour(theme::playhead.withAlpha(0.9f));
        g.strokePath(wave, juce::PathStrokeType(1.1f));
    }

private:
    Studio& studio;
};

Toolbar::Toolbar(Studio& s, juce::ApplicationCommandManager& c)
    : studio(s),
      commands(c),
      mode(std::make_unique<ModeSwitch>(s, c)),
      tempo(std::make_unique<TempoDisplay>(s)),
      patterns(std::make_unique<PatternBox>(s, c)),
      scope(std::make_unique<Oscilloscope>(s)),
      logo(juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize))
{
    const auto invoke = [this](juce::CommandID id) { return [this, id] { commands.invokeDirectly(id, true); }; };
    const auto ticked = [this](juce::CommandID id) { return [this, id] { return isTicked(commands, id); }; };

    play = &addIcon(std::make_unique<Icon>(playShape(), "Play / pause (Space)", invoke(command::play), [this] { return studio.engine.isPlaying(); }));
    stop = &addIcon(std::make_unique<Icon>(stopShape(), "Stop (Space)", invoke(command::stop)));
    record = &addIcon(std::make_unique<Icon>(recordShape(), "Record (R)", invoke(command::record), [this] { return studio.engine.recording.load(); }, theme::selection));
    transport = { play, stop, record };
    metronome = &addIcon(std::make_unique<Icon>(metronomeShape(), "Metronome (Ctrl+M)", invoke(command::metronome), [this] { return studio.engine.metronome.load(); }));
    typing = &addIcon(std::make_unique<Icon>(keyboardShape(), "Typing keyboard to piano keyboard (Ctrl+T)", invoke(command::typingKeyboard), ticked(command::typingKeyboard)));
    toggles = { metronome, typing };
    newPattern = &addIcon(std::make_unique<Icon>(plusShape(), "New pattern (F4)", invoke(command::newPattern)));
    views = {
        &addIcon(std::make_unique<Icon>(playlistShape(), "Playlist (F5)", invoke(command::showPlaylist))),
        &addIcon(std::make_unique<Icon>(pianoRollShape(), "Piano roll (F7)", invoke(command::showPianoRoll))),
        &addIcon(std::make_unique<Icon>(rackShape(), "Channel rack (F6)", invoke(command::showChannelRack))),
        &addIcon(std::make_unique<Icon>(mixerShape(), "Mixer (F9)", invoke(command::showMixer))),
        &addIcon(std::make_unique<Icon>(browserShape(), "Browser (Alt+F8)", invoke(command::showBrowser), ticked(command::showBrowser))),
        &addIcon(std::make_unique<Icon>(plugShape(), "Manage plugins", invoke(command::managePlugins))),
        &addIcon(std::make_unique<Icon>(codeShape(), "Script console (Ctrl+Shift+L)", invoke(command::scriptConsole))),
    };
    quick = {
        &addIcon(std::make_unique<Icon>(undoShape(), "Undo (Ctrl+Z)", invoke(command::undo))),
        &addIcon(std::make_unique<Icon>(saveShape(), "Save (Ctrl+S)", invoke(command::save))),
        &addIcon(std::make_unique<Icon>(exportShape(), "Export audio (Ctrl+R)", invoke(command::exportWave))),
        &addIcon(std::make_unique<Icon>(starShape(), "Star TStudio on GitHub", invoke(command::starOnGitHub), std::function<bool()> {}, theme::selection)),
    };

    ui::setupKnob(master, 0.0, 1.0, studio.project.mainVolume.load(), 0.8, [this](double v) { studio.project.mainVolume.store(float(v)); }, "Main volume");
    pitch.setSliderStyle(juce::Slider::LinearHorizontal);
    pitch.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitch.setRange(-12.0, 12.0, 1.0);
    pitch.setValue(studio.project.mainPitch.load(), juce::dontSendNotification);
    pitch.setDoubleClickReturnValue(true, 0.0);
    pitch.setTooltip("Main pitch: transposes every channel in semitones (double-click resets)");
    pitch.onValueChange = [this]
    {
        studio.project.mainPitch.store(int(pitch.getValue()));
        studio.project.dirty = true;
        repaint(hintArea);
    };
    for (const auto& option : ui::snapOptions)
        snap.addItem(option.name, snap.getNumItems() + 1);
    snap.setSelectedItemIndex(5, juce::dontSendNotification);
    snap.setTooltip("Global snap: sets the snap of the piano roll and the playlist");
    snap.onChange = [this]
    {
        if (onGlobalSnap)
            onGlobalSnap(snap.getSelectedItemIndex());
    };
    for (auto* child : std::initializer_list<juce::Component*> { mode.get(), tempo.get(), patterns.get(), scope.get(), &master, &pitch, &snap })
    {
        child->setWantsKeyboardFocus(false);
        addAndMakeVisible(child);
    }

    studio.project.addChangeListener(this);
    startTimerHz(30);
}

Toolbar::~Toolbar()
{
    studio.project.removeChangeListener(this);
}

Toolbar::Icon& Toolbar::addIcon(std::unique_ptr<Icon> icon)
{
    addAndMakeVisible(*icon);
    icons.push_back(std::move(icon));
    return *icons.back();
}

void Toolbar::paint(juce::Graphics& g)
{
    g.setGradientFill(juce::ColourGradient(theme::title.brighter(0.04f), 0.0f, 0.0f, theme::window, 0.0f, float(getHeight()), false));
    g.fillAll();
    g.setColour(theme::outline.withAlpha(0.6f));
    g.drawHorizontalLine(rowHeight, 0.0f, float(getWidth()));
    g.setColour(theme::outline);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, float(getWidth()));

    g.drawImage(logo, logoArea.toFloat(), juce::RectanglePlacement::centred);
    paintPanel(g, hintArea);
    auto text = hintArea.reduced(7, 2);
    g.setColour(theme::text);
    g.setFont(theme::font(11.5f, juce::Font::bold));
    const auto title = studio.project.title.isNotEmpty() ? studio.project.title
                                                          : (studio.project.file != juce::File() ? studio.project.file.getFileNameWithoutExtension() : juce::String("Untitled"));
    g.drawText(title, text.removeFromTop(text.getHeight() / 2), juce::Justification::centredLeft, true);
    g.setColour(hint.isEmpty() ? theme::textDim : theme::selection.withAlpha(0.9f));
    g.setFont(theme::font(11.0f));
    const auto bars = studio.project.songLength() / ticksPerBar;
    g.drawText(hint.isNotEmpty() ? hint : juce::String(bars) + (bars == 1 ? " bar" : " bars") + " at " + juce::String(studio.project.tempo.load(), 1) + " BPM", text,
               juce::Justification::centredLeft, true);

    paintPanel(g, positionArea.reduced(0, 2));
    g.setColour(theme::textDim);
    g.setFont(theme::font(8.5f, juce::Font::bold));
    g.drawText(studio.project.songMode.load() ? "SONG  B:S:T" : "PAT  B:S:T", positionArea.reduced(5, 3).removeFromTop(9), juce::Justification::topRight);
    g.setColour(theme::accent);
    g.setFont(theme::font(21.0f, juce::Font::bold));
    g.drawText(position, positionArea.reduced(6, 2).withTrimmedTop(6), juce::Justification::centredLeft);

    paintPanel(g, meterArea.reduced(0, 2));
    const auto bar = [&](int index, float level)
    {
        auto lane = meterArea.reduced(5, 7).toFloat();
        lane = lane.withHeight(lane.getHeight() / 2.0f - 1.0f).translated(0.0f, index * (lane.getHeight() / 2.0f + 1.0f));
        const auto db = juce::Decibels::gainToDecibels(level, -60.0f);
        const auto width = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 63.0f) * lane.getWidth();
        juce::ColourGradient gradient(theme::meterLow, lane.getX(), 0.0f, theme::meterHigh, lane.getRight(), 0.0f, false);
        gradient.addColour(0.75, theme::meterMid);
        g.setGradientFill(gradient);
        g.fillRect(lane.withWidth(width));
    };
    bar(0, meterLeft);
    bar(1, meterRight);

    paintPanel(g, statsArea.reduced(0, 2));
    g.setColour(theme::textDim);
    g.setFont(theme::font(10.0f, juce::Font::bold));
    g.drawFittedText(cpu, statsArea.reduced(7, 3), juce::Justification::centredLeft, 2);

    paintPanel(g, infoArea.reduced(0, 3));
    g.setColour(theme::textDim);
    g.setFont(theme::font(10.5f));
    g.drawFittedText(juce::String("TStudio ") + JUCE_APPLICATION_VERSION_STRING + "  -  open source  -  click to star on GitHub", infoArea.reduced(8, 3),
                     juce::Justification::centredLeft, 1);
}

void Toolbar::resized()
{
    auto top = getLocalBounds().removeFromTop(rowHeight).reduced(0, 3);
    top.removeFromLeft(menuWidth);
    mode->setBounds(top.removeFromLeft(44));
    top.removeFromLeft(5);
    for (auto* icon : transport)
        icon->setBounds(top.removeFromLeft(28).reduced(1, 0));
    top.removeFromLeft(5);
    tempo->setBounds(top.removeFromLeft(76));
    top.removeFromLeft(5);
    for (auto* icon : toggles)
        icon->setBounds(top.removeFromLeft(28).reduced(1, 0));
    top.removeFromLeft(6);
    positionArea = top.removeFromLeft(120);
    top.removeFromLeft(5);
    scope->setBounds(top.removeFromLeft(110));
    top.removeFromLeft(4);
    meterArea = top.removeFromLeft(60);
    top.removeFromLeft(4);
    statsArea = top.removeFromLeft(112);
    for (auto it = quick.rbegin(); it != quick.rend(); ++it)
        (*it)->setBounds(top.removeFromRight(28).reduced(1, 0));

    auto bottom = getLocalBounds().withTrimmedTop(rowHeight).reduced(0, 2);
    bottom.removeFromLeft(5);
    logoArea = bottom.removeFromLeft(26).reduced(0, 1);
    bottom.removeFromLeft(5);
    hintArea = bottom.removeFromLeft(300).reduced(0, 1);
    bottom.removeFromLeft(6);
    master.setBounds(bottom.removeFromLeft(28));
    pitch.setBounds(bottom.removeFromLeft(96).reduced(0, 5));
    bottom.removeFromLeft(8);
    snap.setBounds(bottom.removeFromLeft(92).reduced(0, 3));
    bottom.removeFromLeft(6);
    patterns->setBounds(bottom.removeFromLeft(156));
    newPattern->setBounds(bottom.removeFromLeft(24).reduced(1, 3));
    bottom.removeFromLeft(8);
    for (auto* icon : views)
        icon->setBounds(bottom.removeFromLeft(28).reduced(1, 1));
    bottom.removeFromLeft(8);
    infoArea = bottom.removeFromLeft(juce::jmin(bottom.getWidth() - 4, 300));
}

void Toolbar::mouseUp(const juce::MouseEvent& e)
{
    if (infoArea.contains(e.getPosition()))
        juce::URL(community::repository).launchInDefaultBrowser();
}

void Toolbar::timerCallback()
{
    const auto ticks = int(studio.engine.getSongPosition());
    const auto nextPosition = juce::String(ticks / ticksPerBar + 1) + ":" + juce::String((ticks % ticksPerBar) / ticksPerStep + 1).paddedLeft('0', 2) + ":"
                              + juce::String(ticks % ticksPerStep).paddedLeft('0', 2);
    auto voices = 0;
    for (const auto& channel : studio.project.channels)
        voices += int(channel->held.count());
    const auto nextCpu = "CPU " + juce::String(juce::roundToInt(studio.devices.getCpuUsage() * 100.0)) + "%   " + juce::String(ui::processMemoryMegabytes()) + " MB\n"
                         + juce::String(voices) + " voices   " + juce::String(juce::roundToInt(studio.engine.getSampleRate() / 1000.0)) + " kHz";
    const auto nextHint = findHint();
    const auto left = juce::jmax(studio.engine.output.takePeak(0), meterLeft * 0.86f);
    const auto right = juce::jmax(studio.engine.output.takePeak(1), meterRight * 0.86f);

    if (nextPosition != position || nextCpu != cpu || nextHint != hint || std::abs(left - meterLeft) > 0.001f || std::abs(right - meterRight) > 0.001f)
    {
        position = nextPosition;
        cpu = nextCpu;
        hint = nextHint;
        meterLeft = left;
        meterRight = right;
        repaint();
    }
    scope->repaint();
    tempo->repaint();
    for (auto* icon : transport)
        icon->repaint();
    for (auto* icon : toggles)
        icon->repaint();
}

void Toolbar::changeListenerCallback(juce::ChangeBroadcaster*)
{
    mode->repaint();
    patterns->repaint();
    master.setValue(studio.project.mainVolume.load(), juce::dontSendNotification);
    pitch.setValue(studio.project.mainPitch.load(), juce::dontSendNotification);
    repaint(hintArea);
}
}
