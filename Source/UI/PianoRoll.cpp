#include "UI/PianoRoll.h"
#include "UI/Controls.h"
#include "UI/Theme.h"

#include <cmath>

namespace ts
{
namespace
{
constexpr int headerHeight = 30;
constexpr int keyboardWidth = 64;
constexpr int rulerHeight = 22;
constexpr int velocityHeight = 72;
constexpr int scrollSize = 10;
constexpr int maxKey = 127;
constexpr int defaultSnap = 5;

struct Chord
{
    const char* name;
    std::vector<int> intervals;
};

const std::vector<Chord> chordStamps {
    { "Single note", { 0 } },
    { "Major", { 0, 4, 7 } },
    { "Minor", { 0, 3, 7 } },
    { "Major 7", { 0, 4, 7, 11 } },
    { "Minor 7", { 0, 3, 7, 10 } },
    { "Dominant 7", { 0, 4, 7, 10 } },
    { "Minor 9", { 0, 3, 7, 10, 14 } },
    { "Add 9", { 0, 4, 7, 14 } },
    { "Sus 2", { 0, 2, 7 } },
    { "Sus 4", { 0, 5, 7 } },
    { "Diminished", { 0, 3, 6 } },
    { "Augmented", { 0, 4, 8 } },
    { "Power", { 0, 7, 12 } },
    { "Octave", { 0, 12 } },
};

const juce::Colour whiteKey { 0xffdfe4e6 };
const juce::Colour blackKey { 0xff22272a };
}

template <typename Function>
void PianoRoll::edit(Function&& function)
{
    {
        const juce::ScopedLock sl(studio.project.lock);
        function(studio.project.pattern().notes[channelId]);
    }
    knownCount = notes() != nullptr ? notes()->size() : 0;
    studio.project.touch();
}

PianoRoll::PianoRoll(Studio& s, Actions& a) : studio(s), actions(a)
{
    channelButton.setTooltip("Target channel");
    channelButton.onClick = [this] { showChannelMenu(); };
    addAndMakeVisible(channelButton);
    midiButton.setTooltip("Import or export MIDI files (you can also drop .mid files here)");
    midiButton.onClick = [this] { showMidiMenu(); };
    addAndMakeVisible(midiButton);

    for (const auto& option : ui::snapOptions)
        snapBox.addItem(option.name, snapBox.getNumItems() + 1);
    snapBox.setSelectedItemIndex(defaultSnap, juce::dontSendNotification);
    snapBox.setTooltip("Snap to grid");
    addAndMakeVisible(snapBox);
    for (const auto& chord : chordStamps)
        chordBox.addItem(chord.name, chordBox.getNumItems() + 1);
    chordBox.setSelectedItemIndex(0, juce::dontSendNotification);
    chordBox.setTooltip("Stamp: draw whole chords with one click");
    addAndMakeVisible(chordBox);

    const std::array<std::pair<juce::TextButton*, Tool>, 6> tools {
        { { &drawTool, Tool::draw }, { &paintTool, Tool::paint }, { &eraseTool, Tool::erase }, { &selectTool, Tool::select }, { &muteTool, Tool::mute }, { &sliceTool, Tool::slice } }
    };
    for (const auto& [button, kind] : tools)
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(1);
        button->onClick = [this, kind = kind] { setTool(kind); };
        addAndMakeVisible(button);
    }
    drawTool.setToggleState(true, juce::dontSendNotification);
    drawTool.setTooltip("Draw: click to add notes, drag to move, drag the end to resize, right-click to delete");
    paintTool.setTooltip("Paint: drag to paint notes");
    eraseTool.setTooltip("Delete: click or drag over notes to delete them");
    selectTool.setTooltip("Select: drag a box to select notes (or hold Ctrl)");
    muteTool.setTooltip("Mute (T): click or drag over notes to mute or unmute them");
    sliceTool.setTooltip("Slice (C): click a note to cut it at that point");
    for (auto* button : { &slideButton, &portaButton })
    {
        button->setClickingTogglesState(true);
        button->setColour(juce::TextButton::buttonOnColourId, theme::selection);
        addAndMakeVisible(button);
    }
    slideButton.setTooltip("Slide (S): new notes slide the playing note to their pitch over their length. With notes selected, toggles slide on them");
    portaButton.setTooltip("Portamento (O): new notes glide in from the previous note. With notes selected, toggles portamento on them");
    slideButton.onClick = [this] { toggleFlag(&Note::slide, slideButton); };
    portaButton.onClick = [this] { toggleFlag(&Note::porta, portaButton); };
    scaleBox.addItemList({ "No scale", "Major", "Minor", "Dorian", "Phrygian", "Harmonic minor", "Pentatonic", "Minor pentatonic", "Blues" }, 1);
    scaleBox.setSelectedItemIndex(0, juce::dontSendNotification);
    scaleBox.setTooltip("Scale: highlights the scale and snaps new notes to it");
    scaleBox.onChange = [this] { repaint(); };
    rootBox.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    rootBox.setSelectedItemIndex(0, juce::dontSendNotification);
    rootBox.setTooltip("Scale root note");
    rootBox.onChange = [this] { repaint(); };
    addAndMakeVisible(scaleBox);
    addAndMakeVisible(rootBox);

    for (auto* child : std::initializer_list<juce::Component*> { &channelButton, &midiButton, &snapBox, &chordBox, &drawTool, &paintTool, &eraseTool, &selectTool, &muteTool,
                                                                 &sliceTool, &slideButton, &portaButton, &scaleBox, &rootBox, &horizontal, &vertical })
        child->setWantsKeyboardFocus(false);
    horizontal.addListener(this);
    vertical.addListener(this);
    addAndMakeVisible(horizontal);
    addAndMakeVisible(vertical);

    setWantsKeyboardFocus(true);
    studio.project.addChangeListener(this);
    startTimerHz(30);
}

PianoRoll::~PianoRoll()
{
    studio.project.removeChangeListener(this);
}

void PianoRoll::setChannel(int id)
{
    channelId = id;
    selection.clear();
    knownCount = notes() != nullptr ? notes()->size() : 0;
    if (const auto* list = notes(); list != nullptr && !list->empty())
    {
        const auto highest = std::max_element(list->begin(), list->end(), [](const Note& a, const Note& b) { return a.key < b.key; })->key;
        topKey = juce::jlimit(12.0, double(maxKey), double(highest + 6));
    }
    if (id >= 0)
        studio.selectChannel(id);
    updateHeader();
    updateScrollBars();
    repaint();
}

Channel* PianoRoll::channel() const
{
    return studio.project.findChannel(channelId);
}

const std::vector<Note>* PianoRoll::notes() const
{
    const auto& pattern = studio.project.patterns[size_t(studio.project.currentPattern.load())];
    const auto found = pattern.notes.find(channelId);
    return found != pattern.notes.end() ? &found->second : nullptr;
}

int PianoRoll::snap() const
{
    return ui::snapOptions[size_t(juce::jlimit(0, int(ui::snapOptions.size()) - 1, snapBox.getSelectedItemIndex()))].ticks;
}

juce::Rectangle<int> PianoRoll::rulerArea() const
{
    return { keyboardWidth, headerHeight, getWidth() - keyboardWidth - scrollSize, rulerHeight };
}

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return { keyboardWidth, headerHeight + rulerHeight, getWidth() - keyboardWidth - scrollSize,
             juce::jmax(10, getHeight() - headerHeight - rulerHeight - velocityHeight - scrollSize) };
}

juce::Rectangle<int> PianoRoll::keyboardArea() const
{
    const auto grid = gridArea();
    return { 0, grid.getY(), keyboardWidth, grid.getHeight() };
}

juce::Rectangle<int> PianoRoll::velocityArea() const
{
    const auto grid = gridArea();
    return { grid.getX(), grid.getBottom(), grid.getWidth(), velocityHeight };
}

float PianoRoll::xForTick(double tick) const
{
    return float(gridArea().getX() + (tick - viewStart) * tickWidth);
}

double PianoRoll::tickForX(float x) const
{
    return viewStart + (x - float(gridArea().getX())) / tickWidth;
}

float PianoRoll::yForKey(int key) const
{
    return float(gridArea().getY() + (topKey - key) * keyHeight);
}

int PianoRoll::keyForY(float y) const
{
    return juce::jlimit(0, maxKey, int(std::floor(topKey - std::floor((y - float(gridArea().getY())) / float(keyHeight)))));
}

juce::Rectangle<float> PianoRoll::noteBounds(const Note& note) const
{
    const auto x = xForTick(note.start);
    return { x, yForKey(note.key), juce::jmax(3.0f, xForTick(note.end()) - x), float(keyHeight) };
}

int PianoRoll::noteAt(juce::Point<float> point) const
{
    const auto* list = notes();
    if (list == nullptr)
        return -1;
    for (int i = int(list->size()); --i >= 0;)
        if (noteBounds((*list)[size_t(i)]).contains(point))
            return i;
    return -1;
}

bool PianoRoll::nearRightEdge(const Note& note, juce::Point<float> point) const
{
    const auto bounds = noteBounds(note);
    return point.x >= bounds.getRight() - juce::jmin(7.0f, bounds.getWidth() * 0.35f);
}

int PianoRoll::viewLength() const
{
    return studio.project.patterns[size_t(studio.project.currentPattern.load())].length() + ticksPerBar * 4;
}

void PianoRoll::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::panelDark);
    g.fillRect(getLocalBounds().removeFromTop(headerHeight));

    const auto grid = gridArea();
    paintRuler(g, rulerArea());
    paintKeyboard(g, keyboardArea());
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(grid);
        paintGrid(g, grid);
        paintNotes(g, grid);
        if (!band.isEmpty())
        {
            g.setColour(theme::selection.withAlpha(0.15f));
            g.fillRect(band);
            g.setColour(theme::selection);
            g.drawRect(band, 1.0f);
        }
        if (playhead >= 0.0)
        {
            g.setColour(theme::playhead);
            g.drawVerticalLine(juce::roundToInt(xForTick(playhead)), float(grid.getY()), float(grid.getBottom()));
        }
    }
    paintVelocity(g, velocityArea());
    g.setColour(theme::outline);
    g.drawRect(grid.withTrimmedTop(-rulerHeight).withTrimmedBottom(-velocityHeight).expanded(1, 0), 1);
}

void PianoRoll::paintGrid(juce::Graphics& g, juce::Rectangle<int> grid) const
{
    for (auto key = int(std::ceil(topKey)); key >= 0; --key)
    {
        const auto y = yForKey(key);
        if (y > float(grid.getBottom()))
            break;
        g.setColour(ui::isBlackKey(key) ? theme::rowBlack : theme::rowWhite);
        if (scaleBox.getSelectedItemIndex() > 0)
            g.setColour(inScale(key) ? (key % 12 == rootBox.getSelectedItemIndex() ? theme::rowWhite.brighter(0.12f) : theme::rowWhite) : theme::rowBlack.darker(0.25f));
        g.fillRect(float(grid.getX()), y, float(grid.getWidth()), float(keyHeight));
        g.setColour(key % 12 == 0 ? theme::barLine : theme::gridLine);
        g.drawHorizontalLine(juce::roundToInt(y + float(keyHeight)) - 1, float(grid.getX()), float(grid.getRight()));
    }

    const auto stepPixels = ticksPerStep * tickWidth;
    const auto first = int(std::floor(viewStart / ticksPerStep)) * ticksPerStep;
    for (auto tick = first; xForTick(tick) < float(grid.getRight()); tick += ticksPerStep)
    {
        const auto bar = tick % ticksPerBar == 0;
        const auto beat = tick % ppq == 0;
        if (!bar && !beat && stepPixels < 6.0)
            continue;
        g.setColour(bar ? theme::barLine : (beat ? theme::beatLine : theme::gridLine));
        g.drawVerticalLine(juce::roundToInt(xForTick(tick)), float(grid.getY()), float(grid.getBottom()));
    }

    const auto end = xForTick(studio.project.patterns[size_t(studio.project.currentPattern.load())].length());
    if (end < float(grid.getRight()))
    {
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRect(juce::Rectangle<float>(end, float(grid.getY()), float(grid.getRight()) - end, float(grid.getHeight())));
    }
}

void PianoRoll::paintNotes(juce::Graphics& g, juce::Rectangle<int> grid) const
{
    const auto& pattern = studio.project.patterns[size_t(studio.project.currentPattern.load())];
    const auto visible = [&grid](juce::Rectangle<float> r) { return r.getRight() >= float(grid.getX()) && r.getX() <= float(grid.getRight()) && r.getBottom() >= float(grid.getY()) && r.getY() <= float(grid.getBottom()); };

    for (const auto& [id, list] : pattern.notes)
    {
        if (id == channelId)
            continue;
        const auto* other = studio.project.findChannel(id);
        g.setColour((other != nullptr ? other->colour : theme::textDim).withAlpha(0.22f));
        for (const auto& note : list)
            if (const auto r = noteBounds(note).reduced(0.0f, 1.0f); visible(r))
                g.fillRoundedRectangle(r, 2.0f);
    }

    const auto* list = notes();
    if (list == nullptr)
        return;
    const auto* owner = channel();
    const auto base = owner != nullptr ? owner->colour.brighter(0.35f) : theme::accent;
    g.setFont(theme::font(10.5f, juce::Font::bold));
    for (int i = 0; i < int(list->size()); ++i)
    {
        const auto& note = (*list)[size_t(i)];
        const auto r = noteBounds(note).reduced(0.0f, 0.5f);
        if (!visible(r))
            continue;
        auto colour = (selection.count(i) > 0 ? theme::selection : base).withMultipliedBrightness(0.65f + 0.45f * note.velocity);
        if (note.muted)
            colour = colour.withSaturation(0.05f).withMultipliedBrightness(0.55f).withAlpha(0.6f);
        g.setGradientFill(juce::ColourGradient(colour.brighter(0.15f), r.getTopLeft(), colour.darker(0.15f), r.getBottomLeft(), false));
        g.fillRoundedRectangle(r, 2.5f);
        g.setColour(theme::outline);
        g.drawRoundedRectangle(r, 2.5f, 1.0f);
        if (note.slide || note.porta)
        {
            const auto marker = r.withWidth(juce::jmin(r.getHeight(), r.getWidth() * 0.5f)).reduced(1.5f);
            juce::Path shape;
            if (note.slide)
                shape.addTriangle(marker.getX(), marker.getBottom(), marker.getRight(), marker.getCentreY(), marker.getX(), marker.getY());
            else
                shape.addEllipse(marker.reduced(marker.getHeight() * 0.15f));
            g.setColour(theme::noteText.withAlpha(0.8f));
            g.fillPath(shape);
        }
        if (r.getWidth() > 26.0f && keyHeight >= 10)
        {
            g.setColour(theme::noteText);
            g.drawText(ui::keyName(note.key), r.reduced(4.0f, 0.0f).withTrimmedLeft(note.slide || note.porta ? r.getHeight() : 0.0f), juce::Justification::centredLeft, false);
        }
    }
}

void PianoRoll::paintKeyboard(juce::Graphics& g, juce::Rectangle<int> area) const
{
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area);
    g.setColour(whiteKey);
    g.fillRect(area);
    g.setFont(theme::font(10.0f, juce::Font::bold));
    for (auto key = int(std::ceil(topKey)); key >= 0; --key)
    {
        const auto y = yForKey(key);
        if (y > float(area.getBottom()))
            break;
        const auto row = juce::Rectangle<float>(float(area.getX()), y, float(area.getWidth()), float(keyHeight));
        if (key == previewKey)
        {
            g.setColour(theme::accent);
            g.fillRect(row);
        }
        if (ui::isBlackKey(key))
        {
            g.setColour(key == previewKey ? theme::accent.darker(0.4f) : blackKey);
            g.fillRect(row.withWidth(row.getWidth() * 0.62f).reduced(0.0f, 1.0f));
        }
        else
        {
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            const auto note = key % 12;
            if (note == 4 || note == 11)
                g.drawHorizontalLine(juce::roundToInt(y), row.getX(), row.getRight());
            if (note == 0)
            {
                g.drawHorizontalLine(juce::roundToInt(row.getBottom()) - 1, row.getX(), row.getRight());
                g.setColour(theme::noteText);
                g.drawText(ui::keyName(key), row.reduced(4.0f, 0.0f), juce::Justification::centredRight, false);
            }
        }
    }
    g.setColour(theme::outline);
    g.drawVerticalLine(area.getRight() - 1, float(area.getY()), float(area.getBottom()));
}

void PianoRoll::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const
{
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area);
    g.setColour(theme::title);
    g.fillRect(area);
    g.setFont(theme::font(11.0f, juce::Font::bold));
    const auto first = int(std::floor(viewStart / ppq)) * ppq;
    for (auto tick = first; xForTick(tick) < float(area.getRight()); tick += ppq)
    {
        const auto x = juce::roundToInt(xForTick(tick));
        const auto bar = tick % ticksPerBar == 0;
        g.setColour(bar ? theme::text : theme::textDim);
        g.drawVerticalLine(x, float(area.getBottom() - (bar ? 10 : 5)), float(area.getBottom()));
        if (bar)
            g.drawText(juce::String(tick / ticksPerBar + 1), x + 3, area.getY(), 40, area.getHeight() - 4, juce::Justification::centredLeft);
    }
    if (playhead >= 0.0)
    {
        juce::Path marker;
        const auto x = xForTick(playhead);
        marker.addTriangle(x - 5.0f, float(area.getY() + 4), x + 5.0f, float(area.getY() + 4), x, float(area.getBottom() - 2));
        g.setColour(theme::playhead);
        g.fillPath(marker);
    }
}

void PianoRoll::paintVelocity(juce::Graphics& g, juce::Rectangle<int> area) const
{
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area);
    g.setColour(theme::lcd);
    g.fillRect(area);
    g.setColour(theme::outline);
    g.drawHorizontalLine(area.getY(), float(area.getX()), float(area.getRight()));
    g.setColour(theme::textDim);
    g.setFont(theme::font(10.0f, juce::Font::bold));
    g.drawText("VELOCITY", area.reduced(6, 4), juce::Justification::topLeft);

    const auto* list = notes();
    if (list == nullptr)
        return;
    const auto* owner = channel();
    const auto base = owner != nullptr ? owner->colour.brighter(0.4f) : theme::accent;
    for (int i = 0; i < int(list->size()); ++i)
    {
        const auto& note = (*list)[size_t(i)];
        const auto x = xForTick(note.start) + 2.0f;
        if (x < float(area.getX()) - 4.0f || x > float(area.getRight()) + 4.0f)
            continue;
        const auto top = float(area.getBottom() - 4) - float(area.getHeight() - 10) * note.velocity;
        g.setColour(selection.count(i) > 0 ? theme::selection : base);
        g.fillRect(x - 1.0f, top, 2.0f, float(area.getBottom() - 4) - top);
        g.fillEllipse(x - 3.5f, top - 3.5f, 7.0f, 7.0f);
    }
}

void PianoRoll::resized()
{
    auto header = getLocalBounds().removeFromTop(headerHeight).reduced(6, 4);
    channelButton.setBounds(header.removeFromLeft(150));
    header.removeFromLeft(8);
    for (auto* button : { &drawTool, &paintTool, &eraseTool, &selectTool, &muteTool, &sliceTool })
        button->setBounds(header.removeFromLeft(52).reduced(1, 0));
    header.removeFromLeft(8);
    for (auto* button : { &slideButton, &portaButton })
        button->setBounds(header.removeFromLeft(50).reduced(1, 0));
    header.removeFromLeft(8);
    snapBox.setBounds(header.removeFromLeft(100));
    header.removeFromLeft(6);
    chordBox.setBounds(header.removeFromLeft(118));
    header.removeFromLeft(6);
    scaleBox.setBounds(header.removeFromLeft(118));
    rootBox.setBounds(header.removeFromLeft(54));
    header.removeFromLeft(6);
    midiButton.setBounds(header.removeFromLeft(52));

    const auto grid = gridArea();
    vertical.setBounds(grid.getRight(), grid.getY(), scrollSize, grid.getHeight());
    horizontal.setBounds(grid.getX(), getHeight() - scrollSize, grid.getWidth(), scrollSize);
    updateScrollBars();
}

void PianoRoll::updateScrollBars()
{
    const auto grid = gridArea();
    const auto visibleKeys = double(grid.getHeight()) / keyHeight;
    topKey = juce::jlimit(juce::jmin(double(maxKey), visibleKeys - 1.0), double(maxKey), topKey);
    vertical.setRangeLimits(0.0, maxKey + 1.0, juce::dontSendNotification);
    vertical.setCurrentRange(maxKey - topKey, visibleKeys, juce::dontSendNotification);

    const auto visibleTicks = grid.getWidth() / tickWidth;
    viewStart = juce::jmax(0.0, viewStart);
    horizontal.setRangeLimits(0.0, juce::jmax(double(viewLength()), viewStart + visibleTicks), juce::dontSendNotification);
    horizontal.setCurrentRange(viewStart, visibleTicks, juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &vertical)
        topKey = maxKey - newRangeStart;
    else
        viewStart = newRangeStart;
    repaint();
}

void PianoRoll::updateHeader()
{
    const auto* owner = channel();
    const auto name = owner != nullptr ? owner->name : juce::String("(no channel)");
    channelButton.setButtonText(name);
    if (onTitleChanged)
        onTitleChanged("Piano roll - " + name);
}

void PianoRoll::showChannelMenu()
{
    juce::PopupMenu menu;
    for (const auto& item : studio.project.channels)
        menu.addItem(item->name, true, item->id == channelId, [this, id = item->id] { setChannel(id); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&channelButton));
}

void PianoRoll::preview(int key, float velocity)
{
    stopPreview();
    previewKey = key;
    studio.engine.sendLive(juce::MidiMessage::noteOn(1, key, juce::jlimit(0.05f, 1.0f, velocity)));
    repaint(keyboardArea());
}

void PianoRoll::stopPreview()
{
    if (previewKey < 0)
        return;
    studio.engine.sendLive(juce::MidiMessage::noteOff(1, previewKey));
    previewKey = -1;
    repaint(keyboardArea());
}

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    dragOrigin = p;
    drag = Drag::none;

    if (keyboardArea().contains(p.toInt()))
    {
        drag = Drag::keyboard;
        preview(keyForY(p.y), 0.8f);
        return;
    }
    if (rulerArea().contains(p.toInt()))
    {
        drag = Drag::ruler;
        if (!studio.project.songMode.load())
            studio.engine.setSongPosition(juce::jmax(0.0, tickForX(p.x)));
        return;
    }
    if (channel() == nullptr)
        return;
    if (velocityArea().contains(p.toInt()))
    {
        studio.project.checkpoint();
        drag = Drag::velocity;
        setVelocityAt(p);
        return;
    }
    if (!gridArea().contains(p.toInt()))
        return;

    const auto index = noteAt(p);
    if (tool == Tool::mute && !e.mods.isPopupMenu())
    {
        studio.project.checkpoint();
        drag = Drag::mute;
        muteDragValue = index >= 0 ? !(*notes())[size_t(index)].muted : true;
        if (index >= 0)
            edit([this, index](std::vector<Note>& list) { list[size_t(index)].muted = muteDragValue; });
        return;
    }
    if (tool == Tool::slice && !e.mods.isPopupMenu())
    {
        studio.project.checkpoint();
        sliceAt(p);
        return;
    }
    if (e.mods.isPopupMenu() || tool == Tool::erase)
    {
        studio.project.checkpoint();
        drag = Drag::erase;
        eraseAt(p);
        return;
    }
    if (tool == Tool::select || (e.mods.isCtrlDown() && index < 0))
    {
        drag = Drag::select;
        if (!e.mods.isShiftDown())
            selection.clear();
        band = { p, p };
        repaint();
        return;
    }
    if (index >= 0)
    {
        studio.project.checkpoint();
        if (selection.count(index) == 0)
        {
            if (!e.mods.isShiftDown())
                selection.clear();
            selection.insert(index);
        }
        original = *notes();
        anchor = index;
        const auto& note = original[size_t(index)];
        drag = nearRightEdge(note, p) ? Drag::resize : Drag::move;
        lastLength = note.length;
        preview(note.key, note.velocity);
        repaint();
        return;
    }
    if (tool == Tool::paint)
    {
        studio.project.checkpoint();
        selection.clear();
        drag = Drag::paint;
        paintAt(p);
        return;
    }

    studio.project.checkpoint();
    const auto note = newNote(juce::jmax(0, ui::snapDown(tickForX(p.x), snap())), keyForY(p.y));
    const auto& intervals = chordStamps[size_t(juce::jmax(0, chordBox.getSelectedItemIndex()))].intervals;
    const auto first = notes() != nullptr ? int(notes()->size()) : 0;
    edit([&](std::vector<Note>& list)
    {
        for (const auto interval : intervals)
            if (note.key + interval <= maxKey)
            {
                auto stamped = note;
                stamped.key = scaleKey(note.key + interval);
                list.push_back(stamped);
            }
    });
    selection.clear();
    for (int i = first; i < int(notes()->size()); ++i)
        selection.insert(i);
    original = *notes();
    anchor = first;
    drag = Drag::move;
    preview(note.key, note.velocity);
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    const auto p = e.position;
    switch (drag)
    {
        case Drag::keyboard:
            if (const auto key = keyForY(p.y); key != previewKey)
                preview(key, 0.8f);
            break;
        case Drag::ruler:
            if (!studio.project.songMode.load())
                studio.engine.setSongPosition(juce::jmax(0.0, tickForX(p.x)));
            break;
        case Drag::velocity: setVelocityAt(p); break;
        case Drag::erase: eraseAt(p); break;
        case Drag::paint: paintAt(p); break;
        case Drag::mute:
            if (const auto index = noteAt(p); index >= 0 && (*notes())[size_t(index)].muted != muteDragValue)
                edit([this, index](std::vector<Note>& list) { list[size_t(index)].muted = muteDragValue; });
            break;
        case Drag::select:
        {
            band = juce::Rectangle<float>(dragOrigin, p);
            if (const auto* list = notes())
                for (int i = 0; i < int(list->size()); ++i)
                    if (noteBounds((*list)[size_t(i)]).intersects(band))
                        selection.insert(i);
            repaint();
            break;
        }
        case Drag::move:
        {
            if (anchor < 0 || anchor >= int(original.size()))
                break;
            const auto& pivot = original[size_t(anchor)];
            const auto raw = tickForX(p.x) - tickForX(dragOrigin.x);
            const auto start = e.mods.isAltDown() ? juce::roundToInt(pivot.start + raw) : ui::snapNearest(pivot.start + raw, snap());
            auto deltaTicks = start - pivot.start;
            auto deltaKeys = keyForY(p.y) - keyForY(dragOrigin.y);
            for (const auto i : selection)
            {
                if (i >= int(original.size()))
                    continue;
                deltaTicks = juce::jmax(deltaTicks, -original[size_t(i)].start);
                deltaKeys = juce::jlimit(-original[size_t(i)].key, maxKey - original[size_t(i)].key, deltaKeys);
            }
            const auto previousKey = notes() != nullptr && anchor < int(notes()->size()) ? (*notes())[size_t(anchor)].key : -1;
            edit([&](std::vector<Note>& list)
            {
                for (const auto i : selection)
                {
                    if (i >= int(list.size()) || i >= int(original.size()))
                        continue;
                    list[size_t(i)].start = original[size_t(i)].start + deltaTicks;
                    list[size_t(i)].key = original[size_t(i)].key + deltaKeys;
                }
            });
            if (pivot.key + deltaKeys != previousKey)
                preview(pivot.key + deltaKeys, pivot.velocity);
            break;
        }
        case Drag::resize:
        {
            if (anchor < 0 || anchor >= int(original.size()))
                break;
            const auto& pivot = original[size_t(anchor)];
            const auto raw = tickForX(p.x) - tickForX(dragOrigin.x);
            const auto grid = snap();
            const auto end = e.mods.isAltDown() ? juce::roundToInt(pivot.end() + raw) : ui::snapNearest(pivot.end() + raw, grid);
            const auto deltaLength = end - pivot.end();
            edit([&](std::vector<Note>& list)
            {
                for (const auto i : selection)
                    if (i < int(list.size()) && i < int(original.size()))
                        list[size_t(i)].length = juce::jmax(e.mods.isAltDown() ? 1 : grid, original[size_t(i)].length + deltaLength);
            });
            if (anchor < int(notes()->size()))
                lastLength = (*notes())[size_t(anchor)].length;
            break;
        }
        case Drag::none: break;
    }
}

void PianoRoll::mouseUp(const juce::MouseEvent&)
{
    stopPreview();
    drag = Drag::none;
    anchor = -1;
    original.clear();
    if (!band.isEmpty())
    {
        band = {};
        repaint();
    }
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    const auto index = gridArea().contains(e.getPosition()) ? noteAt(e.position) : -1;
    if (index < 0 || tool == Tool::erase)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (nearRightEdge((*notes())[size_t(index)], e.position))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (e.mods.isCtrlDown())
    {
        const auto x = juce::jmax(float(gridArea().getX()), e.position.x);
        const auto anchorTick = tickForX(x);
        tickWidth = juce::jlimit(0.04, 8.0, tickWidth * (delta > 0.0f ? 1.18 : 1.0 / 1.18));
        viewStart = juce::jmax(0.0, anchorTick - (x - float(gridArea().getX())) / tickWidth);
    }
    else if (e.mods.isShiftDown() || wheel.deltaX != 0.0f)
    {
        viewStart = juce::jmax(0.0, viewStart - delta * 300.0 / tickWidth);
    }
    else if (e.mods.isAltDown())
    {
        keyHeight = juce::jlimit(6, 30, keyHeight + (delta > 0.0f ? 2 : -2));
    }
    else
    {
        topKey += delta * 14.0;
    }
    updateScrollBars();
    repaint();
}

bool PianoRoll::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto ctrl = mods.isCommandDown();
    const auto alt = mods.isAltDown();
    const auto shift = mods.isShiftDown();
    const auto plain = !mods.isAnyModifierKeyDown();
    const auto code = key.getKeyCode();
    const auto typing = actions.typingKeyboardActive && actions.typingKeyboardActive();
    const auto script = [this](const char* name) { if (actions.runScript) actions.runScript(name); };
    if (plain && !typing && (code == 'P' || code == 'B' || code == 'D' || code == 'E' || code == 'T' || code == 'C'))
        setTool(code == 'P' ? Tool::draw : code == 'B' ? Tool::paint : code == 'D' ? Tool::erase : code == 'E' ? Tool::select : code == 'T' ? Tool::mute : Tool::slice);
    else if (plain && !typing && (code == 'S' || code == 'O'))
    {
        auto& button = code == 'S' ? slideButton : portaButton;
        button.setToggleState(!button.getToggleState(), juce::dontSendNotification);
        toggleFlag(code == 'S' ? &Note::slide : &Note::porta, button);
    }
    else if (plain && !typing && (code == 'G' || code == 'K'))
        stepChannel(code == 'G' ? -1 : 1, false);
    else if (plain && !typing && (code == 'H' || code == 'J'))
        stepChannel(code == 'H' ? -1 : 1, true);
    else if (ctrl && code == 'L')
        legato();
    else if (ctrl && code == 'U')
        chop();
    else if (ctrl && code == 'G')
        glue();
    else if (ctrl && code == 'M')
        chooseMidiImport();
    else if (shift && code == 'I')
        invertSelection();
    else if (alt && code == 'Q')
        quantizeSelection();
    else if (alt && code == 'R')
        script("Humanize");
    else if (alt && code == 'S')
        script("Strum");
    else if (alt && code == 'A')
        script("Arpeggiate chords");
    else if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
        zoom(code == juce::KeyPress::pageUpKey ? 1.25 : 0.8);
    else if (alt && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        PianoRoll::shift(code == juce::KeyPress::leftKey ? -juce::jmax(1, snap() / 4) : juce::jmax(1, snap() / 4));
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        deleteSelection();
    else if (ctrl && code == 'A')
        selectAll();
    else if (ctrl && code == 'D')
    {
        selection.clear();
        repaint();
    }
    else if (ctrl && code == 'C')
        copySelection();
    else if (ctrl && code == 'X')
    {
        copySelection();
        deleteSelection();
    }
    else if (ctrl && code == 'V')
        paste();
    else if (ctrl && code == 'B')
        duplicateSelection();
    else if (ctrl && code == 'Q')
        quantizeSelection();
    else if (code == juce::KeyPress::upKey)
        transpose(ctrl ? 12 : 1);
    else if (code == juce::KeyPress::downKey)
        transpose(ctrl ? -12 : -1);
    else if (code == juce::KeyPress::leftKey)
        PianoRoll::shift(-snap());
    else if (code == juce::KeyPress::rightKey)
        PianoRoll::shift(snap());
    else
        return false;
    return true;
}

void PianoRoll::eraseAt(juce::Point<float> point)
{
    const auto index = noteAt(point);
    if (index < 0)
        return;
    selection.clear();
    edit([index](std::vector<Note>& list) { list.erase(list.begin() + index); });
}

void PianoRoll::paintAt(juce::Point<float> point)
{
    const auto start = juce::jmax(0, ui::snapDown(tickForX(point.x), snap()));
    const auto key = keyForY(point.y);
    if (const auto* list = notes())
        for (const auto& note : *list)
            if (note.key == key && start < note.end() && start + lastLength > note.start)
                return;
    edit([&](std::vector<Note>& list) { list.push_back(newNote(start, key)); });
}

void PianoRoll::setVelocityAt(juce::Point<float> point)
{
    const auto* list = notes();
    if (list == nullptr)
        return;
    const auto area = velocityArea();
    const auto velocity = juce::jlimit(0.0f, 1.0f, (float(area.getBottom() - 4) - point.y) / float(area.getHeight() - 10));
    std::vector<int> targets;
    for (int i = 0; i < int(list->size()); ++i)
        if (std::abs(xForTick((*list)[size_t(i)].start) + 2.0f - point.x) <= 4.0f && (selection.empty() || selection.count(i) > 0))
            targets.push_back(i);
    if (targets.empty())
        return;
    edit([&](std::vector<Note>& notesToEdit)
    {
        for (const auto i : targets)
            notesToEdit[size_t(i)].velocity = velocity;
    });
}

void PianoRoll::deleteSelection()
{
    if (selection.empty())
        return;
    studio.project.checkpoint();
    edit([this](std::vector<Note>& list)
    {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it)
            if (*it < int(list.size()))
                list.erase(list.begin() + *it);
    });
    selection.clear();
    repaint();
}

void PianoRoll::transpose(int semitones)
{
    if (selection.empty())
        return;
    studio.project.checkpoint();
    edit([&](std::vector<Note>& list)
    {
        for (const auto i : selection)
            if (i < int(list.size()))
                list[size_t(i)].key = juce::jlimit(0, maxKey, list[size_t(i)].key + semitones);
    });
}

void PianoRoll::shift(int ticks)
{
    if (selection.empty())
        return;
    studio.project.checkpoint();
    edit([&](std::vector<Note>& list)
    {
        for (const auto i : selection)
            if (i < int(list.size()))
                list[size_t(i)].start = juce::jmax(0, list[size_t(i)].start + ticks);
    });
}

void PianoRoll::duplicateSelection()
{
    const auto* list = notes();
    if (list == nullptr || selection.empty())
        return;
    auto first = std::numeric_limits<int>::max(), last = 0;
    for (const auto i : selection)
    {
        first = juce::jmin(first, (*list)[size_t(i)].start);
        last = juce::jmax(last, (*list)[size_t(i)].end());
    }
    const auto span = juce::jmax(snap(), ((last - first + ticksPerStep - 1) / ticksPerStep) * ticksPerStep);
    std::vector<Note> copies;
    for (const auto i : selection)
    {
        auto copy = (*list)[size_t(i)];
        copy.start += span;
        copies.push_back(copy);
    }
    studio.project.checkpoint();
    const auto base = int(list->size());
    edit([&copies](std::vector<Note>& target) { target.insert(target.end(), copies.begin(), copies.end()); });
    selection.clear();
    for (int i = 0; i < int(copies.size()); ++i)
        selection.insert(base + i);
}

void PianoRoll::quantizeSelection()
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    studio.project.checkpoint();
    const auto grid = snap();
    edit([&](std::vector<Note>& target)
    {
        for (int i = 0; i < int(target.size()); ++i)
            if (selection.empty() || selection.count(i) > 0)
                target[size_t(i)].start = juce::jmax(0, ui::snapNearest(target[size_t(i)].start, grid));
    });
}

void PianoRoll::copySelection()
{
    clipboard.clear();
    if (const auto* list = notes())
        for (const auto i : selection)
            clipboard.push_back((*list)[size_t(i)]);
}

void PianoRoll::paste()
{
    if (clipboard.empty() || channel() == nullptr)
        return;
    studio.project.checkpoint();
    const auto base = notes() != nullptr ? int(notes()->size()) : 0;
    edit([this](std::vector<Note>& list) { list.insert(list.end(), clipboard.begin(), clipboard.end()); });
    selection.clear();
    for (int i = 0; i < int(clipboard.size()); ++i)
        selection.insert(base + i);
    repaint();
}

Note PianoRoll::newNote(int start, int key) const
{
    Note note { start, lastLength, scaleKey(key), 0.78f };
    note.slide = slideButton.getToggleState();
    note.porta = portaButton.getToggleState();
    return note;
}

bool PianoRoll::inScale(int key) const
{
    static const std::array<std::vector<int>, 9> scales { {
        {}, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
        { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 },
    } };
    const auto& scale = scales[size_t(juce::jlimit(0, 8, scaleBox.getSelectedItemIndex()))];
    if (scale.empty())
        return true;
    const auto degree = ((key - rootBox.getSelectedItemIndex()) % 12 + 12) % 12;
    return std::find(scale.begin(), scale.end(), degree) != scale.end();
}

int PianoRoll::scaleKey(int key) const
{
    for (auto distance = 0; distance < 12; ++distance)
    {
        if (key - distance >= 0 && inScale(key - distance))
            return key - distance;
        if (key + distance <= maxKey && inScale(key + distance))
            return key + distance;
    }
    return key;
}

void PianoRoll::toggleFlag(bool Note::*flag, juce::TextButton& button)
{
    if (selection.empty())
        return;
    const auto value = button.getToggleState();
    studio.project.checkpoint();
    edit([&](std::vector<Note>& list)
    {
        for (const auto i : selection)
            if (i < int(list.size()))
                list[size_t(i)].*flag = value;
    });
}

void PianoRoll::sliceAt(juce::Point<float> p)
{
    const auto index = noteAt(p);
    if (index < 0)
        return;
    const auto at = ui::snapNearest(juce::roundToInt(tickForX(p.x)), snap());
    edit([&](std::vector<Note>& list)
    {
        auto& note = list[size_t(index)];
        if (at <= note.start || at >= note.end())
            return;
        auto tail = note;
        tail.start = at;
        tail.length = note.end() - at;
        tail.slide = tail.porta = false;
        note.length = at - note.start;
        list.push_back(tail);
    });
    selection.clear();
}

void PianoRoll::setTool(Tool next)
{
    tool = next;
    const std::array<std::pair<juce::TextButton*, Tool>, 6> buttons {
        { { &drawTool, Tool::draw }, { &paintTool, Tool::paint }, { &eraseTool, Tool::erase }, { &selectTool, Tool::select }, { &muteTool, Tool::mute }, { &sliceTool, Tool::slice } }
    };
    for (const auto& [button, kind] : buttons)
        button->setToggleState(kind == next, juce::dontSendNotification);
}

void PianoRoll::legato()
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    studio.project.checkpoint();
    edit([&](std::vector<Note>& target)
    {
        for (int i = 0; i < int(target.size()); ++i)
        {
            if (!selection.empty() && selection.count(i) == 0)
                continue;
            auto next = std::numeric_limits<int>::max();
            for (const auto& other : target)
                if (other.start > target[size_t(i)].start)
                    next = juce::jmin(next, other.start);
            if (next != std::numeric_limits<int>::max())
                target[size_t(i)].length = next - target[size_t(i)].start;
        }
    });
}

void PianoRoll::chop()
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    const auto grid = juce::jmax(1, snap());
    studio.project.checkpoint();
    edit([&](std::vector<Note>& target)
    {
        std::vector<Note> result;
        for (int i = 0; i < int(target.size()); ++i)
        {
            const auto& note = target[size_t(i)];
            if ((!selection.empty() && selection.count(i) == 0) || note.length <= grid)
            {
                result.push_back(note);
                continue;
            }
            for (auto start = note.start; start < note.end(); start += grid)
                result.push_back({ start, juce::jmin(grid, note.end() - start), note.key, note.velocity, false, false, note.muted });
        }
        target = std::move(result);
    });
    selection.clear();
}

void PianoRoll::glue()
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    studio.project.checkpoint();
    edit([&](std::vector<Note>& target)
    {
        std::vector<Note> picked, kept;
        for (int i = 0; i < int(target.size()); ++i)
            (selection.empty() || selection.count(i) > 0 ? picked : kept).push_back(target[size_t(i)]);
        std::sort(picked.begin(), picked.end(), [](const Note& a, const Note& b) { return a.key != b.key ? a.key < b.key : a.start < b.start; });
        std::vector<Note> merged;
        for (const auto& note : picked)
        {
            if (!merged.empty() && merged.back().key == note.key && note.start <= merged.back().end() + 1)
                merged.back().length = juce::jmax(merged.back().end(), note.end()) - merged.back().start;
            else
                merged.push_back(note);
        }
        kept.insert(kept.end(), merged.begin(), merged.end());
        target = std::move(kept);
    });
    selection.clear();
}

void PianoRoll::invertSelection()
{
    std::set<int> inverted;
    if (const auto* list = notes())
        for (int i = 0; i < int(list->size()); ++i)
            if (selection.count(i) == 0)
                inverted.insert(i);
    selection = std::move(inverted);
    repaint();
}

void PianoRoll::stepChannel(int direction, bool withNotes)
{
    const auto& channels = studio.project.channels;
    auto index = -1;
    for (int i = 0; i < int(channels.size()); ++i)
        if (channels[size_t(i)]->id == channelId)
            index = i;
    for (auto i = index + direction; i >= 0 && i < int(channels.size()); i += direction)
    {
        const auto& pattern = studio.project.pattern().notes;
        const auto found = pattern.find(channels[size_t(i)]->id);
        if (!withNotes || (found != pattern.end() && !found->second.empty()))
        {
            setChannel(channels[size_t(i)]->id);
            return;
        }
    }
}

void PianoRoll::zoom(double factor)
{
    tickWidth = juce::jlimit(0.04, 8.0, tickWidth * factor);
    updateScrollBars();
    repaint();
}

void PianoRoll::chooseMidiImport()
{
    chooser = std::make_unique<juce::FileChooser>("Import MIDI", juce::File(), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
            importMidi(fc.getResult());
    });
}

void PianoRoll::selectAll()
{
    selection.clear();
    if (const auto* list = notes())
        for (int i = 0; i < int(list->size()); ++i)
            selection.insert(i);
    repaint();
}

void PianoRoll::importMidi(const juce::File& file)
{
    juce::String error;
    if (!studio.importMidi(file, channelId, error))
        ui::showMessage("MIDI import", error);
    selection.clear();
}

void PianoRoll::showMidiMenu()
{
    juce::PopupMenu menu;
    menu.addItem("Import MIDI file...    Ctrl+M", [this] { chooseMidiImport(); });
    const auto exportTo = [this](int channel)
    {
        chooser = std::make_unique<juce::FileChooser>("Export MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                                         .getChildFile(studio.project.pattern().name + ".mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this, channel](const juce::FileChooser& fc)
                             {
                                 if (fc.getResult() == juce::File())
                                     return;
                                 juce::String error;
                                 if (!studio.exportMidi(fc.getResult().withFileExtension(".mid"), channel, error))
                                     ui::showMessage("MIDI export", error);
                             });
    };
    menu.addItem("Export this channel as MIDI...", [exportTo, this] { exportTo(channelId); });
    menu.addItem("Export whole pattern as MIDI...", [exportTo] { exportTo(-1); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&midiButton));
}

bool PianoRoll::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("mid;midi");
}

void PianoRoll::filesDropped(const juce::StringArray& files, int, int)
{
    importMidi(juce::File(files[0]));
}

bool PianoRoll::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith("midi:");
}

void PianoRoll::itemDropped(const SourceDetails& details)
{
    importMidi(juce::File(details.description.toString().fromFirstOccurrenceOf(":", false, false)));
}

void PianoRoll::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (channel() == nullptr && !studio.project.channels.empty())
    {
        setChannel(studio.project.selectedChannel);
        return;
    }
    const auto count = notes() != nullptr ? notes()->size() : 0;
    if (count != knownCount)
    {
        selection.clear();
        knownCount = count;
    }
    updateHeader();
    updateScrollBars();
    repaint();
}

void PianoRoll::timerCallback()
{
    const auto next = ui::patternPlayhead(studio);
    if (next == playhead)
        return;
    const auto grid = gridArea();
    const auto strip = [&](double tick) { return juce::Rectangle<int>(juce::roundToInt(xForTick(tick)) - 6, rulerArea().getY(), 12, grid.getBottom() - rulerArea().getY()); };
    if (playhead >= 0.0)
        repaint(strip(playhead));
    playhead = next;
    if (playhead >= 0.0)
        repaint(strip(playhead));
}
}
