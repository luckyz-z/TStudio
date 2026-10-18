#include "UI/Playlist.h"
#include "UI/Controls.h"
#include "UI/Theme.h"

#include <cmath>

namespace ts
{
namespace
{
constexpr int toolbarHeight = 30;
constexpr int pickerWidth = 132;
constexpr int headerWidth = 112;
constexpr int rulerHeight = 22;
constexpr int scrollSize = 10;
constexpr int pickerRow = 22;
constexpr int edge = 6;
constexpr int defaultSnap = 7;
}

template <typename Function>
void Playlist::edit(Function&& function)
{
    {
        const juce::ScopedLock sl(studio.project.lock);
        function(studio.project.clips);
    }
    studio.project.touch();
}

Playlist::Playlist(Studio& s, Actions& a) : studio(s), actions(a)
{
    for (const auto& option : ui::snapOptions)
        snapBox.addItem(option.name, snapBox.getNumItems() + 1);
    snapBox.setSelectedItemIndex(defaultSnap, juce::dontSendNotification);
    snapBox.setTooltip("Snap to grid");
    addAndMakeVisible(snapBox);

    for (auto* button : { &drawTool, &eraseTool, &selectTool })
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(2);
        addAndMakeVisible(button);
    }
    drawTool.setToggleState(true, juce::dontSendNotification);
    drawTool.setTooltip("Draw: click to place the selected pattern, drag to move, drag the edges to resize, right-click to delete");
    eraseTool.setTooltip("Delete: click or drag over clips to delete them");
    selectTool.setTooltip("Select: drag a box to select clips (or hold Ctrl)");

    for (auto* child : std::initializer_list<juce::Component*> { &snapBox, &drawTool, &eraseTool, &selectTool, &horizontal, &vertical })
        child->setWantsKeyboardFocus(false);
    horizontal.addListener(this);
    vertical.addListener(this);
    addAndMakeVisible(horizontal);
    addAndMakeVisible(vertical);

    setWantsKeyboardFocus(true);
    studio.project.addChangeListener(this);
    startTimerHz(30);
}

Playlist::~Playlist()
{
    studio.project.removeChangeListener(this);
}

juce::Rectangle<int> Playlist::pickerArea() const
{
    return { 0, toolbarHeight, pickerWidth, getHeight() - toolbarHeight };
}

juce::Rectangle<int> Playlist::rulerArea() const
{
    return { pickerWidth + headerWidth, toolbarHeight, getWidth() - pickerWidth - headerWidth - scrollSize, rulerHeight };
}

juce::Rectangle<int> Playlist::gridArea() const
{
    const auto ruler = rulerArea();
    return { ruler.getX(), ruler.getBottom(), ruler.getWidth(), juce::jmax(10, getHeight() - ruler.getBottom() - scrollSize) };
}

juce::Rectangle<int> Playlist::headerArea() const
{
    const auto grid = gridArea();
    return { pickerWidth, grid.getY(), headerWidth, grid.getHeight() };
}

float Playlist::xForTick(double tick) const
{
    return float(gridArea().getX() + (tick - viewStart) * tickWidth);
}

double Playlist::tickForX(float x) const
{
    return viewStart + (x - float(gridArea().getX())) / tickWidth;
}

float Playlist::yForTrack(int track) const
{
    return float(gridArea().getY() + (track - topTrack) * trackHeight);
}

int Playlist::trackForY(float y) const
{
    return juce::jlimit(0, playlistTrackCount - 1, int(std::floor(topTrack + (y - float(gridArea().getY())) / float(trackHeight))));
}

juce::Rectangle<float> Playlist::clipBounds(const Clip& clip) const
{
    const auto x = xForTick(clip.start);
    return { x, yForTrack(clip.track) + 1.0f, juce::jmax(4.0f, xForTick(clip.end()) - x), float(trackHeight - 2) };
}

int Playlist::clipAt(juce::Point<float> point) const
{
    const auto& clips = studio.project.clips;
    for (int i = int(clips.size()); --i >= 0;)
        if (clipBounds(clips[size_t(i)]).contains(point))
            return i;
    return -1;
}

int Playlist::snap() const
{
    return ui::snapOptions[size_t(juce::jlimit(0, int(ui::snapOptions.size()) - 1, snapBox.getSelectedItemIndex()))].ticks;
}

int Playlist::viewLength() const
{
    return studio.project.songLength() + ticksPerBar * 16;
}

void Playlist::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::panelDark);
    g.fillRect(getLocalBounds().removeFromTop(toolbarHeight));
    paintPicker(g, pickerArea());
    paintHeaders(g, headerArea());
    paintRuler(g, rulerArea());
    paintGrid(g, gridArea());
}

void Playlist::paintPicker(juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour(theme::panelDark);
    g.fillRect(area);
    g.setColour(theme::outline);
    g.drawVerticalLine(area.getRight() - 1, float(area.getY()), float(area.getBottom()));

    const auto& project = studio.project;
    auto rows = area.reduced(4, 4);
    g.setFont(theme::font(12.5f));
    for (int i = 0; i < int(project.patterns.size()) && rows.getHeight() >= pickerRow; ++i)
    {
        const auto row = rows.removeFromTop(pickerRow).reduced(0, 1);
        const auto& pattern = project.patterns[size_t(i)];
        const auto current = i == project.currentPattern.load();
        g.setColour(current ? pattern.colour : pattern.colour.withMultipliedBrightness(0.55f));
        g.fillRoundedRectangle(row.toFloat(), 3.0f);
        g.setColour(current ? theme::noteText : theme::text);
        g.drawText(pattern.name, row.reduced(6, 0), juce::Justification::centredLeft, true);
    }
    if (rows.getHeight() >= pickerRow)
    {
        g.setColour(theme::textDim);
        g.drawText("+ New pattern", rows.removeFromTop(pickerRow).reduced(6, 0), juce::Justification::centredLeft);
    }
    for (int i = 0; i < int(project.automations.size()) && rows.getHeight() >= pickerRow; ++i)
    {
        const auto row = rows.removeFromTop(pickerRow).reduced(0, 1);
        const auto& curve = project.automations[size_t(i)];
        const auto current = i == pickedAutomation;
        g.setColour(current ? curve.colour : curve.colour.withMultipliedBrightness(0.45f));
        g.fillRoundedRectangle(row.toFloat(), 3.0f);
        g.setColour(current ? theme::noteText : theme::text);
        g.drawText(juce::String::fromUTF8("\xe2\x88\xbf ") + curve.name, row.reduced(6, 0), juce::Justification::centredLeft, true);
    }
}

void Playlist::paintHeaders(juce::Graphics& g, juce::Rectangle<int> area) const
{
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area);
    g.setColour(theme::title.darker(0.2f));
    g.fillRect(area);
    g.setFont(theme::font(12.5f));
    for (auto track = int(topTrack); track < playlistTrackCount; ++track)
    {
        const auto y = yForTrack(track);
        if (y > float(area.getBottom()))
            break;
        const auto row = juce::Rectangle<float>(float(area.getX()), y, float(area.getWidth()), float(trackHeight));
        const auto& info = studio.project.tracks[size_t(track)];
        const auto light = row.withWidth(18.0f).withSizeKeepingCentre(8.0f, 8.0f).translated(4.0f, 0.0f);
        g.setColour(info.muted ? theme::ledOff : theme::led);
        g.fillEllipse(light);
        g.setColour(info.muted ? theme::textDim : theme::text);
        g.drawText(info.name, row.withTrimmedLeft(24.0f).reduced(2.0f, 0.0f), juce::Justification::centredLeft, true);
        g.setColour(theme::outline);
        g.drawHorizontalLine(juce::roundToInt(row.getBottom()) - 1, row.getX(), row.getRight());
    }
    g.drawVerticalLine(area.getRight() - 1, float(area.getY()), float(area.getBottom()));
}

void Playlist::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const
{
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area);
    g.setColour(theme::title);
    g.fillRect(area);
    g.setFont(theme::font(11.0f, juce::Font::bold));
    const auto barPixels = ticksPerBar * tickWidth;
    const auto every = barPixels < 18.0 ? 8 : (barPixels < 36.0 ? 4 : 1);
    const auto first = int(std::floor(viewStart / ticksPerBar));
    for (auto bar = first; xForTick(double(bar) * ticksPerBar) < float(area.getRight()); ++bar)
    {
        const auto x = juce::roundToInt(xForTick(double(bar) * ticksPerBar));
        const auto labelled = bar % every == 0;
        g.setColour(labelled ? theme::text : theme::textDim);
        g.drawVerticalLine(x, float(area.getBottom() - (labelled ? 10 : 5)), float(area.getBottom()));
        if (labelled)
            g.drawText(juce::String(bar + 1), x + 3, area.getY(), 40, area.getHeight() - 4, juce::Justification::centredLeft);
    }
    if (studio.project.hasLoop())
    {
        const auto left = xForTick(studio.project.loopStart.load());
        const auto right = xForTick(studio.project.loopEnd.load());
        g.setColour(theme::accent.withAlpha(0.35f));
        g.fillRect(juce::Rectangle<float>(left, float(area.getY()), right - left, float(area.getHeight())));
        g.setColour(theme::accent);
        g.fillRect(juce::Rectangle<float>(left, float(area.getY()), right - left, 3.0f));
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

void Playlist::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) const
{
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area);
    for (auto track = int(topTrack); track < playlistTrackCount; ++track)
    {
        const auto y = yForTrack(track);
        if (y > float(area.getBottom()))
            break;
        g.setColour(track % 2 == 0 ? theme::rowWhite : theme::rowBlack);
        g.fillRect(float(area.getX()), y, float(area.getWidth()), float(trackHeight));
        g.setColour(theme::gridLine);
        g.drawHorizontalLine(juce::roundToInt(y + float(trackHeight)) - 1, float(area.getX()), float(area.getRight()));
    }

    const auto beatPixels = ppq * tickWidth;
    const auto first = int(std::floor(viewStart / ppq)) * ppq;
    for (auto tick = first; xForTick(tick) < float(area.getRight()); tick += ppq)
    {
        const auto bar = tick % ticksPerBar == 0;
        if (!bar && beatPixels < 8.0)
            continue;
        g.setColour(bar ? theme::barLine : theme::beatLine);
        g.drawVerticalLine(juce::roundToInt(xForTick(tick)), float(area.getY()), float(area.getBottom()));
    }

    const auto& clips = studio.project.clips;
    for (int i = 0; i < int(clips.size()); ++i)
    {
        const auto r = clipBounds(clips[size_t(i)]);
        if (r.getRight() >= float(area.getX()) && r.getX() <= float(area.getRight()) && r.getBottom() >= float(area.getY()) && r.getY() <= float(area.getBottom()))
            paintClip(g, clips[size_t(i)], selection.count(i) > 0);
    }

    if (studio.project.hasLoop())
    {
        const auto left = xForTick(studio.project.loopStart.load());
        const auto right = xForTick(studio.project.loopEnd.load());
        g.setColour(theme::accent.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(left, float(area.getY()), right - left, float(area.getHeight())));
        g.setColour(theme::accent.withAlpha(0.5f));
        g.drawVerticalLine(juce::roundToInt(left), float(area.getY()), float(area.getBottom()));
        g.drawVerticalLine(juce::roundToInt(right), float(area.getY()), float(area.getBottom()));
    }
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
        g.drawVerticalLine(juce::roundToInt(xForTick(playhead)), float(area.getY()), float(area.getBottom()));
    }
}

juce::Rectangle<float> Playlist::clipBody(const Clip& clip) const
{
    const auto r = clipBounds(clip);
    return r.withTrimmedTop(juce::jmin(14.0f, r.getHeight() * 0.4f) + 2.0f).reduced(2.0f, 2.0f);
}

void Playlist::paintAutomation(juce::Graphics& g, const Clip& clip, juce::Rectangle<float> body, juce::Colour colour) const
{
    const auto& curve = studio.project.automations[size_t(clip.automation)];
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(body.toNearestInt());
    const auto yFor = [&body](float value) { return body.getBottom() - value * body.getHeight(); };
    juce::Path line, fill;
    const auto step = juce::jmax(1.0, 3.0 / tickWidth);
    auto first = true;
    for (auto tick = double(clip.offset); tick <= double(clip.offset + clip.length) + step; tick += step)
    {
        const auto x = xForTick(clip.start + tick - clip.offset);
        const auto y = yFor(curve.valueAt(tick));
        if (first)
        {
            line.startNewSubPath(x, y);
            fill.startNewSubPath(x, body.getBottom());
            fill.lineTo(x, y);
            first = false;
        }
        else
        {
            line.lineTo(x, y);
            fill.lineTo(x, y);
        }
    }
    fill.lineTo(xForTick(clip.end() + step), body.getBottom());
    fill.closeSubPath();
    g.setColour(colour.withAlpha(0.25f));
    g.fillPath(fill);
    g.setColour(colour.brighter(0.5f));
    g.strokePath(line, juce::PathStrokeType(1.6f));
    for (const auto& point : curve.points)
    {
        if (point.tick < clip.offset || point.tick > clip.offset + clip.length)
            continue;
        const auto centre = juce::Point<float>(xForTick(clip.start + point.tick - clip.offset), yFor(point.value));
        g.setColour(theme::outline);
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(centre));
        g.setColour(colour.brighter(0.8f));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(centre));
    }
}

void Playlist::paintClip(juce::Graphics& g, const Clip& clip, bool selected) const
{
    const auto r = clipBounds(clip);
    if (clip.isAutomation())
    {
        if (clip.automation >= int(studio.project.automations.size()))
            return;
        const auto& curve = studio.project.automations[size_t(clip.automation)];
        const auto muted = clip.muted || studio.project.tracks[size_t(clip.track)].muted;
        const auto colour = muted ? curve.colour.withSaturation(0.1f).withMultipliedBrightness(0.6f) : curve.colour;
        g.setColour(colour.withMultipliedBrightness(0.35f).withAlpha(0.92f));
        g.fillRoundedRectangle(r, 2.5f);
        auto header = r.withHeight(juce::jmin(14.0f, r.getHeight() * 0.4f));
        g.setColour(colour.withMultipliedBrightness(0.8f));
        g.fillRoundedRectangle(header, 2.5f);
        g.setColour(theme::noteText);
        g.setFont(theme::font(11.0f, juce::Font::bold));
        g.drawText(curve.name, header.reduced(4.0f, 0.0f), juce::Justification::centredLeft, true);
        if (clipBody(clip).getHeight() > 3.0f)
            paintAutomation(g, clip, clipBody(clip), colour);
        g.setColour(selected ? theme::selection : theme::outline);
        g.drawRoundedRectangle(r, 2.5f, selected ? 1.6f : 1.0f);
        return;
    }
    const auto& pattern = studio.project.patterns[size_t(clip.pattern)];
    const auto muted = clip.muted || studio.project.tracks[size_t(clip.track)].muted;
    const auto colour = muted ? pattern.colour.withSaturation(0.1f).withMultipliedBrightness(0.6f) : pattern.colour;

    g.setColour(colour.withMultipliedBrightness(0.55f).withAlpha(0.9f));
    g.fillRoundedRectangle(r, 2.5f);
    auto header = r.withHeight(juce::jmin(14.0f, r.getHeight() * 0.4f));
    g.setColour(colour);
    g.fillRoundedRectangle(header, 2.5f);
    g.setColour(theme::noteText);
    g.setFont(theme::font(11.0f, juce::Font::bold));
    g.drawText(pattern.name, header.reduced(4.0f, 0.0f), juce::Justification::centredLeft, true);

    const auto body = r.withTrimmedTop(header.getHeight() + 2.0f).reduced(2.0f, 2.0f);
    auto low = 127, high = 0;
    for (const auto& [id, list] : pattern.notes)
        for (const auto& note : list)
        {
            low = juce::jmin(low, note.key);
            high = juce::jmax(high, note.key);
        }
    if (low <= high && body.getHeight() > 3.0f)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(body.toNearestInt());
        const auto span = float(juce::jmax(1, high - low));
        g.setColour(colour.brighter(0.6f));
        for (const auto& [id, list] : pattern.notes)
            for (const auto& note : list)
            {
                const auto local = note.start - clip.offset;
                if (local + note.length < 0 || local > clip.length)
                    continue;
                const auto x = xForTick(clip.start + local);
                const auto w = juce::jmax(1.5f, float(note.length * tickWidth) - 1.0f);
                const auto y = body.getBottom() - 2.0f - (body.getHeight() - 3.0f) * float(note.key - low) / span;
                g.fillRect(x, y - 1.0f, w, 2.0f);
            }
    }

    g.setColour(selected ? theme::selection : theme::outline);
    g.drawRoundedRectangle(r, 2.5f, selected ? 1.6f : 1.0f);
}

void Playlist::resized()
{
    auto toolbar = getLocalBounds().removeFromTop(toolbarHeight).reduced(6, 4);
    toolbar.removeFromLeft(pickerWidth - 6);
    for (auto* button : { &drawTool, &eraseTool, &selectTool })
        button->setBounds(toolbar.removeFromLeft(58).reduced(1, 0));
    toolbar.removeFromLeft(10);
    snapBox.setBounds(toolbar.removeFromLeft(110));

    const auto grid = gridArea();
    vertical.setBounds(grid.getRight(), grid.getY(), scrollSize, grid.getHeight());
    horizontal.setBounds(grid.getX(), getHeight() - scrollSize, grid.getWidth(), scrollSize);
    updateScrollBars();
}

void Playlist::updateScrollBars()
{
    const auto grid = gridArea();
    const auto visibleTracks = double(grid.getHeight()) / trackHeight;
    topTrack = juce::jlimit(0.0, juce::jmax(0.0, playlistTrackCount - visibleTracks), topTrack);
    vertical.setRangeLimits(0.0, playlistTrackCount, juce::dontSendNotification);
    vertical.setCurrentRange(topTrack, visibleTracks, juce::dontSendNotification);

    const auto visibleTicks = grid.getWidth() / tickWidth;
    viewStart = juce::jmax(0.0, viewStart);
    horizontal.setRangeLimits(0.0, juce::jmax(double(viewLength()), viewStart + visibleTicks), juce::dontSendNotification);
    horizontal.setCurrentRange(viewStart, visibleTicks, juce::dontSendNotification);
}

void Playlist::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &vertical)
        topTrack = newRangeStart;
    else
        viewStart = newRangeStart;
    repaint();
}

void Playlist::pickerClicked(const juce::MouseEvent& e)
{
    const auto index = (e.y - pickerArea().getY() - 4) / pickerRow;
    auto& project = studio.project;
    if (index == int(project.patterns.size()))
    {
        studio.addPattern();
        return;
    }
    if (const auto automation = index - int(project.patterns.size()) - 1; automation >= 0 && automation < int(project.automations.size()))
    {
        pickedAutomation = automation;
        repaint();
        if (!e.mods.isPopupMenu())
            return;
        juce::PopupMenu menu;
        menu.addItem("Rename...", [this, automation]
        {
            ui::askText("Rename automation clip", studio.project.automations[size_t(automation)].name, [this, automation](const juce::String& name)
            {
                if (name.isNotEmpty() && automation < int(studio.project.automations.size()))
                {
                    studio.project.automations[size_t(automation)].name = name;
                    studio.project.touch();
                }
            });
        });
        menu.addItem("Reset to a flat line", [this, automation]
        {
            studio.project.checkpoint();
            {
                const juce::ScopedLock sl(studio.project.lock);
                auto& curve = studio.project.automations[size_t(automation)];
                const auto value = curve.points.empty() ? 0.5f : curve.points.front().value;
                curve.points = { { 0, value, 0.0f }, { curve.length, value, 0.0f } };
            }
            studio.project.touch();
        });
        menu.showMenuAsync({});
        return;
    }
    pickedAutomation = -1;
    if (index < 0 || index > int(project.patterns.size()))
        return;
    studio.selectPattern(index);
    if (!e.mods.isPopupMenu())
        return;

    juce::PopupMenu menu;
    menu.addItem("Rename...", [this, index]
    {
        ui::askText("Rename pattern", studio.project.patterns[size_t(index)].name, [this, index](const juce::String& name)
        {
            if (name.isNotEmpty() && index < int(studio.project.patterns.size()))
            {
                studio.project.patterns[size_t(index)].name = name;
                studio.project.touch();
            }
        });
    });
    menu.addItem("Colour...", [this, index]
    {
        ui::pickColour(*this, studio.project.patterns[size_t(index)].colour, [safe = juce::Component::SafePointer<Playlist>(this), index](juce::Colour colour)
        {
            if (safe != nullptr && index < int(safe->studio.project.patterns.size()))
            {
                safe->studio.project.patterns[size_t(index)].colour = colour;
                safe->studio.project.touch();
            }
        });
    });
    menu.addItem("Clone", [this, index] { studio.clonePattern(index); });
    menu.addItem("Delete", [this, index] { studio.removePattern(index); });
    menu.showMenuAsync({});
}

void Playlist::headerClicked(const juce::MouseEvent& e)
{
    const auto track = trackForY(e.position.y);
    auto& info = studio.project.tracks[size_t(track)];
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem("Rename...", [this, track]
        {
            ui::askText("Rename track", studio.project.tracks[size_t(track)].name, [this, track](const juce::String& name)
            {
                studio.project.tracks[size_t(track)].name = name.isNotEmpty() ? name : Project::defaultTrackName(track);
                studio.project.touch();
            });
        });
        menu.addItem("Mute", true, info.muted, [this, track]
        {
            {
                const juce::ScopedLock sl(studio.project.lock);
                studio.project.tracks[size_t(track)].muted = !studio.project.tracks[size_t(track)].muted;
            }
            studio.project.touch();
        });
        menu.showMenuAsync({});
        return;
    }
    if (e.x - headerArea().getX() < 24)
    {
        {
            const juce::ScopedLock sl(studio.project.lock);
            info.muted = !info.muted;
        }
        studio.project.touch();
    }
}

void Playlist::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    dragOrigin = p;
    drag = Drag::none;

    if (pickerArea().contains(p.toInt()))
    {
        pickerClicked(e);
        return;
    }
    if (headerArea().contains(p.toInt()))
    {
        headerClicked(e);
        return;
    }
    if (rulerArea().contains(p.toInt()) && (e.mods.isCtrlDown() || e.mods.isPopupMenu()))
    {
        drag = Drag::loop;
        const auto tick = juce::jmax(0, ui::snapDown(tickForX(p.x), juce::jmax(snap(), ticksPerBar / 4)));
        studio.project.loopStart.store(tick);
        studio.project.loopEnd.store(tick);
        repaint();
        return;
    }
    if (rulerArea().contains(p.toInt()))
    {
        drag = Drag::ruler;
        if (!studio.project.songMode.load())
        {
            studio.project.songMode.store(true);
            studio.project.sendChangeMessage();
        }
        studio.engine.setSongPosition(juce::jmax(0, ui::snapDown(tickForX(p.x), snap())));
        return;
    }
    if (!gridArea().contains(p.toInt()))
        return;

    const auto index = clipAt(p);
    if (index >= 0 && studio.project.clips[size_t(index)].isAutomation() && !eraseTool.getToggleState() && !selectTool.getToggleState()
        && clipBody(studio.project.clips[size_t(index)]).contains(p) && !e.mods.isShiftDown())
    {
        studio.project.checkpoint();
        pointClip = index;
        pointIndex = pointAt(index, p);
        const auto& clip = studio.project.clips[size_t(index)];
        if (e.mods.isPopupMenu())
        {
            if (pointIndex >= 0)
            {
                {
                    const juce::ScopedLock sl(studio.project.lock);
                    auto& points = studio.project.automations[size_t(clip.automation)].points;
                    if (points.size() > 1)
                        points.erase(points.begin() + pointIndex);
                }
                studio.project.touch();
            }
            pointClip = pointIndex = -1;
            return;
        }
        if (pointIndex < 0)
        {
            const auto body = clipBody(clip);
            const auto tick = juce::jmax(0, (e.mods.isAltDown() ? juce::roundToInt(tickForX(p.x)) : ui::snapNearest(juce::roundToInt(tickForX(p.x)), snap())) - clip.start + clip.offset);
            const auto value = juce::jlimit(0.0f, 1.0f, (body.getBottom() - p.y) / body.getHeight());
            {
                const juce::ScopedLock sl(studio.project.lock);
                auto& points = studio.project.automations[size_t(clip.automation)].points;
                const auto at = std::upper_bound(points.begin(), points.end(), tick, [](int t, const AutomationPoint& point) { return t < point.tick; });
                pointIndex = int(at - points.begin());
                points.insert(at, { tick, value, 0.0f });
            }
            studio.project.touch();
        }
        drag = Drag::point;
        tensionDrag = e.mods.isAltDown();
        return;
    }
    if (e.mods.isPopupMenu() || eraseTool.getToggleState())
    {
        studio.project.checkpoint();
        drag = Drag::erase;
        if (index >= 0)
        {
            selection.clear();
            edit([index](std::vector<Clip>& clips) { clips.erase(clips.begin() + index); });
        }
        return;
    }
    if (selectTool.getToggleState() || (e.mods.isCtrlDown() && index < 0))
    {
        drag = Drag::select;
        if (!e.mods.isShiftDown())
            selection.clear();
        band = { p, p };
        repaint();
        return;
    }

    studio.project.checkpoint();
    if (index >= 0)
    {
        if (selection.count(index) == 0)
        {
            if (!e.mods.isShiftDown())
                selection.clear();
            selection.insert(index);
        }
        const auto bounds = clipBounds(studio.project.clips[size_t(index)]);
        drag = p.x >= bounds.getRight() - edge ? Drag::resizeEnd : (p.x <= bounds.getX() + edge ? Drag::resizeStart : Drag::move);
        anchor = index;
        original = studio.project.clips;
        if (!e.mods.isShiftDown() && !studio.project.clips[size_t(index)].isAutomation())
            studio.selectPattern(studio.project.clips[size_t(index)].pattern);
        if (studio.project.clips[size_t(index)].isAutomation())
            pickedAutomation = studio.project.clips[size_t(index)].automation;
        repaint();
        return;
    }

    Clip clip;
    clip.track = trackForY(p.y);
    clip.start = juce::jmax(0, ui::snapDown(tickForX(p.x), snap()));
    clip.pattern = studio.project.currentPattern.load();
    clip.length = studio.project.pattern().length();
    if (pickedAutomation >= 0 && pickedAutomation < int(studio.project.automations.size()))
    {
        clip.automation = pickedAutomation;
        clip.length = studio.project.automations[size_t(pickedAutomation)].length;
    }
    edit([&clip](std::vector<Clip>& clips) { clips.push_back(clip); });
    selection = { int(studio.project.clips.size()) - 1 };
    anchor = *selection.begin();
    original = studio.project.clips;
    drag = Drag::move;
}

void Playlist::mouseDrag(const juce::MouseEvent& e)
{
    const auto p = e.position;
    const auto grid = snap();
    const auto raw = tickForX(p.x) - tickForX(dragOrigin.x);
    switch (drag)
    {
        case Drag::ruler:
            studio.engine.setSongPosition(juce::jmax(0, ui::snapDown(tickForX(p.x), grid)));
            break;
        case Drag::erase:
            if (const auto index = clipAt(p); index >= 0)
            {
                selection.clear();
                edit([index](std::vector<Clip>& clips) { clips.erase(clips.begin() + index); });
            }
            break;
        case Drag::select:
        {
            band = juce::Rectangle<float>(dragOrigin, p);
            const auto& clips = studio.project.clips;
            for (int i = 0; i < int(clips.size()); ++i)
                if (clipBounds(clips[size_t(i)]).intersects(band))
                    selection.insert(i);
            repaint();
            break;
        }
        case Drag::move:
        {
            if (anchor < 0 || anchor >= int(original.size()))
                break;
            const auto& pivot = original[size_t(anchor)];
            const auto start = e.mods.isAltDown() ? juce::roundToInt(pivot.start + raw) : ui::snapNearest(pivot.start + raw, grid);
            auto deltaTicks = start - pivot.start;
            auto deltaTracks = trackForY(p.y) - trackForY(dragOrigin.y);
            for (const auto i : selection)
            {
                if (i >= int(original.size()))
                    continue;
                deltaTicks = juce::jmax(deltaTicks, -original[size_t(i)].start);
                deltaTracks = juce::jlimit(-original[size_t(i)].track, playlistTrackCount - 1 - original[size_t(i)].track, deltaTracks);
            }
            edit([&](std::vector<Clip>& clips)
            {
                for (const auto i : selection)
                {
                    if (i >= int(clips.size()) || i >= int(original.size()))
                        continue;
                    clips[size_t(i)].start = original[size_t(i)].start + deltaTicks;
                    clips[size_t(i)].track = original[size_t(i)].track + deltaTracks;
                }
            });
            break;
        }
        case Drag::resizeEnd:
        {
            if (anchor < 0 || anchor >= int(original.size()))
                break;
            const auto& pivot = original[size_t(anchor)];
            const auto end = e.mods.isAltDown() ? juce::roundToInt(pivot.end() + raw) : ui::snapNearest(pivot.end() + raw, grid);
            const auto delta = end - pivot.end();
            edit([&](std::vector<Clip>& clips)
            {
                for (const auto i : selection)
                    if (i < int(clips.size()) && i < int(original.size()))
                        clips[size_t(i)].length = juce::jmax(grid, original[size_t(i)].length + delta);
            });
            break;
        }
        case Drag::resizeStart:
        {
            if (anchor < 0 || anchor >= int(original.size()))
                break;
            const auto& pivot = original[size_t(anchor)];
            const auto start = e.mods.isAltDown() ? juce::roundToInt(pivot.start + raw) : ui::snapNearest(pivot.start + raw, grid);
            auto delta = start - pivot.start;
            for (const auto i : selection)
            {
                if (i >= int(original.size()))
                    continue;
                const auto& clip = original[size_t(i)];
                delta = juce::jlimit(juce::jmax(-clip.start, -clip.offset), clip.length - grid, delta);
            }
            edit([&](std::vector<Clip>& clips)
            {
                for (const auto i : selection)
                {
                    if (i >= int(clips.size()) || i >= int(original.size()))
                        continue;
                    const auto& clip = original[size_t(i)];
                    clips[size_t(i)].start = clip.start + delta;
                    clips[size_t(i)].offset = clip.offset + delta;
                    clips[size_t(i)].length = clip.length - delta;
                }
            });
            break;
        }
        case Drag::point:
            editPoint(p, !e.mods.isAltDown());
            break;
        case Drag::loop:
        {
            const auto loopGrid = juce::jmax(grid, ticksPerBar / 4);
            const auto anchorTick = juce::jmax(0, ui::snapDown(tickForX(dragOrigin.x), loopGrid));
            const auto current = juce::jmax(0, ui::snapNearest(juce::roundToInt(tickForX(p.x)), loopGrid));
            studio.project.loopStart.store(juce::jmin(anchorTick, current));
            studio.project.loopEnd.store(juce::jmax(anchorTick, current));
            repaint();
            break;
        }
        case Drag::none: break;
    }
}

int Playlist::pointAt(int clipIndex, juce::Point<float> p) const
{
    const auto& clip = studio.project.clips[size_t(clipIndex)];
    const auto& curve = studio.project.automations[size_t(clip.automation)];
    const auto body = clipBody(clip);
    for (int i = 0; i < int(curve.points.size()); ++i)
    {
        const auto& point = curve.points[size_t(i)];
        const auto centre = juce::Point<float>(xForTick(clip.start + point.tick - clip.offset), body.getBottom() - point.value * body.getHeight());
        if (centre.getDistanceFrom(p) <= 7.0f)
            return i;
    }
    return -1;
}

void Playlist::editPoint(juce::Point<float> p, bool snapToGrid)
{
    if (pointClip < 0 || pointClip >= int(studio.project.clips.size()))
        return;
    const auto& clip = studio.project.clips[size_t(pointClip)];
    if (!clip.isAutomation() || clip.automation >= int(studio.project.automations.size()))
        return;
    const auto body = clipBody(clip);
    {
        const juce::ScopedLock sl(studio.project.lock);
        auto& points = studio.project.automations[size_t(clip.automation)].points;
        if (pointIndex < 0 || pointIndex >= int(points.size()))
            return;
        auto& point = points[size_t(pointIndex)];
        if (tensionDrag)
        {
            point.tension = juce::jlimit(-1.0f, 1.0f, (p.y - dragOrigin.y) / 60.0f);
        }
        else
        {
            const auto raw = juce::roundToInt(tickForX(p.x)) - clip.start + clip.offset;
            const auto low = pointIndex > 0 ? points[size_t(pointIndex - 1)].tick : 0;
            const auto high = pointIndex + 1 < int(points.size()) ? points[size_t(pointIndex + 1)].tick : std::numeric_limits<int>::max();
            point.tick = juce::jlimit(low, high, snapToGrid ? ui::snapNearest(raw + clip.start, snap()) - clip.start : raw);
            point.value = juce::jlimit(0.0f, 1.0f, (body.getBottom() - p.y) / body.getHeight());
        }
    }
    studio.project.touch();
}

void Playlist::mouseUp(const juce::MouseEvent&)
{
    if (drag == Drag::loop)
    {
        if (!studio.project.hasLoop())
        {
            studio.project.loopStart.store(-1);
            studio.project.loopEnd.store(-1);
        }
        else if (!studio.project.songMode.load())
        {
            studio.project.songMode.store(true);
            studio.engine.setSongPosition(double(studio.project.loopStart.load()));
        }
        studio.project.touch();
    }
    drag = Drag::none;
    pointClip = pointIndex = -1;
    tensionDrag = false;
    anchor = -1;
    original.clear();
    if (!band.isEmpty())
    {
        band = {};
        repaint();
    }
}

void Playlist::mouseMove(const juce::MouseEvent& e)
{
    const auto index = gridArea().contains(e.getPosition()) ? clipAt(e.position) : -1;
    if (index < 0 || eraseTool.getToggleState())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    const auto bounds = clipBounds(studio.project.clips[size_t(index)]);
    const auto onEdge = e.position.x >= bounds.getRight() - edge || e.position.x <= bounds.getX() + edge;
    setMouseCursor(onEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void Playlist::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (rulerArea().contains(p.toInt()) && studio.project.hasLoop())
    {
        studio.project.loopStart.store(-1);
        studio.project.loopEnd.store(-1);
        studio.project.touch();
        return;
    }
    if (pickerArea().contains(p.toInt()))
    {
        const auto index = (e.y - pickerArea().getY() - 4) / pickerRow;
        if (index >= 0 && index < int(studio.project.patterns.size()))
            ui::askText("Rename pattern", studio.project.patterns[size_t(index)].name, [this, index](const juce::String& name)
            {
                if (name.isNotEmpty() && index < int(studio.project.patterns.size()))
                {
                    studio.project.patterns[size_t(index)].name = name;
                    studio.project.touch();
                }
            });
        return;
    }
    if (headerArea().contains(p.toInt()) && e.x - headerArea().getX() >= 24)
    {
        const auto track = trackForY(p.y);
        ui::askText("Rename track", studio.project.tracks[size_t(track)].name, [this, track](const juce::String& name)
        {
            studio.project.tracks[size_t(track)].name = name.isNotEmpty() ? name : Project::defaultTrackName(track);
            studio.project.touch();
        });
        return;
    }
    if (const auto index = clipAt(p); index >= 0 && gridArea().contains(p.toInt()) && !studio.project.clips[size_t(index)].isAutomation())
    {
        studio.selectPattern(studio.project.clips[size_t(index)].pattern);
        actions.showChannelRack();
    }
}

void Playlist::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (e.mods.isCtrlDown())
    {
        const auto x = juce::jmax(float(gridArea().getX()), e.position.x);
        const auto anchorTick = tickForX(x);
        tickWidth = juce::jlimit(4.0 / ticksPerBar, 600.0 / ticksPerBar, tickWidth * (delta > 0.0f ? 1.18 : 1.0 / 1.18));
        viewStart = juce::jmax(0.0, anchorTick - (x - float(gridArea().getX())) / tickWidth);
    }
    else if (e.mods.isShiftDown() || wheel.deltaX != 0.0f)
    {
        viewStart = juce::jmax(0.0, viewStart - delta * 300.0 / tickWidth);
    }
    else if (e.mods.isAltDown())
    {
        trackHeight = juce::jlimit(18, 120, trackHeight + (delta > 0.0f ? 4 : -4));
    }
    else
    {
        topTrack -= delta * 6.0;
    }
    updateScrollBars();
    repaint();
}

bool Playlist::keyPressed(const juce::KeyPress& key)
{
    const auto ctrl = key.getModifiers().isCommandDown();
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        deleteSelection();
    else if (ctrl && code == 'A')
    {
        selection.clear();
        for (int i = 0; i < int(studio.project.clips.size()); ++i)
            selection.insert(i);
        repaint();
    }
    else if (ctrl && code == 'D')
    {
        selection.clear();
        repaint();
    }
    else if (ctrl && code == 'B')
        duplicateSelection();
    else if (ctrl && code == 'C')
        copySelection();
    else if (ctrl && code == 'X')
    {
        copySelection();
        deleteSelection();
    }
    else if (ctrl && code == 'V')
        pasteClips();
    else if (key.getModifiers().isShiftDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        moveSelection(code == juce::KeyPress::leftKey ? -snap() : snap());
    else if (!key.getModifiers().isAnyModifierKeyDown() && (code == 'P' || code == 'B' || code == 'D' || code == 'E'))
    {
        auto& button = code == 'D' ? eraseTool : code == 'E' ? selectTool : drawTool;
        button.setToggleState(true, juce::dontSendNotification);
    }
    else
        return false;
    return true;
}

void Playlist::copySelection()
{
    clipboard.clear();
    for (const auto i : selection)
        if (i < int(studio.project.clips.size()))
            clipboard.push_back(studio.project.clips[size_t(i)]);
}

void Playlist::pasteClips()
{
    if (clipboard.empty())
        return;
    auto first = std::numeric_limits<int>::max();
    for (const auto& clip : clipboard)
        first = juce::jmin(first, clip.start);
    const auto offset = ui::snapDown(juce::jmax(0, int(studio.engine.getSongPosition())), ticksPerBar) - first;
    studio.project.checkpoint();
    const auto base = int(studio.project.clips.size());
    edit([this, offset](std::vector<Clip>& target)
    {
        for (auto clip : clipboard)
        {
            clip.start = juce::jmax(0, clip.start + offset);
            target.push_back(clip);
        }
    });
    selection.clear();
    for (int i = 0; i < int(clipboard.size()); ++i)
        selection.insert(base + i);
}

void Playlist::moveSelection(int ticks)
{
    if (selection.empty())
        return;
    studio.project.checkpoint();
    edit([this, ticks](std::vector<Clip>& target)
    {
        for (const auto i : selection)
            if (i < int(target.size()))
                target[size_t(i)].start = juce::jmax(0, target[size_t(i)].start + ticks);
    });
}

void Playlist::deleteSelection()
{
    if (selection.empty())
        return;
    studio.project.checkpoint();
    edit([this](std::vector<Clip>& clips)
    {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it)
            if (*it < int(clips.size()))
                clips.erase(clips.begin() + *it);
    });
    selection.clear();
}

void Playlist::duplicateSelection()
{
    if (selection.empty())
        return;
    const auto& clips = studio.project.clips;
    auto first = std::numeric_limits<int>::max(), last = 0;
    for (const auto i : selection)
    {
        first = juce::jmin(first, clips[size_t(i)].start);
        last = juce::jmax(last, clips[size_t(i)].end());
    }
    const auto span = ((last - first + ticksPerBar - 1) / ticksPerBar) * ticksPerBar;
    std::vector<Clip> copies;
    for (const auto i : selection)
    {
        auto copy = clips[size_t(i)];
        copy.start += span;
        copies.push_back(copy);
    }
    studio.project.checkpoint();
    const auto base = int(clips.size());
    edit([&copies](std::vector<Clip>& target) { target.insert(target.end(), copies.begin(), copies.end()); });
    selection.clear();
    for (int i = 0; i < int(copies.size()); ++i)
        selection.insert(base + i);
}

void Playlist::changeListenerCallback(juce::ChangeBroadcaster*)
{
    const auto count = int(studio.project.clips.size());
    for (auto it = selection.begin(); it != selection.end();)
        it = *it >= count ? selection.erase(it) : std::next(it);
    updateScrollBars();
    repaint();
}

void Playlist::timerCallback()
{
    const auto next = studio.project.songMode.load() && studio.engine.isPlaying() ? studio.engine.getSongPosition() : -1.0;
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
