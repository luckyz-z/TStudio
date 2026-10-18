#pragma once

#include "Engine/Studio.h"
#include "UI/Actions.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>

namespace ts
{
class Playlist final : public juce::Component, private juce::ChangeListener, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    Playlist(Studio&, Actions&);
    ~Playlist() override;

    void setSnapIndex(int index) { snapBox.setSelectedItemIndex(index, juce::dontSendNotification); repaint(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    enum class Drag
    {
        none,
        move,
        resizeEnd,
        resizeStart,
        erase,
        select,
        ruler,
        point,
        loop
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    template <typename Function>
    void edit(Function&&);

    juce::Rectangle<int> pickerArea() const;
    juce::Rectangle<int> headerArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> gridArea() const;
    float xForTick(double tick) const;
    double tickForX(float x) const;
    float yForTrack(int track) const;
    int trackForY(float y) const;
    juce::Rectangle<float> clipBounds(const Clip&) const;
    int clipAt(juce::Point<float>) const;
    int snap() const;
    int viewLength() const;

    void paintPicker(juce::Graphics&, juce::Rectangle<int>) const;
    void paintHeaders(juce::Graphics&, juce::Rectangle<int>) const;
    void paintRuler(juce::Graphics&, juce::Rectangle<int>) const;
    void paintGrid(juce::Graphics&, juce::Rectangle<int>) const;
    void paintClip(juce::Graphics&, const Clip&, bool selected) const;
    void updateScrollBars();
    void pickerClicked(const juce::MouseEvent&);
    void headerClicked(const juce::MouseEvent&);
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void pasteClips();
    void moveSelection(int ticks);

    Studio& studio;
    Actions& actions;
    juce::ComboBox snapBox;
    juce::TextButton drawTool { "Draw" }, eraseTool { "Delete" }, selectTool { "Select" };
    juce::ScrollBar horizontal { false }, vertical { true };

    Drag drag = Drag::none;
    double tickWidth = 64.0 / ticksPerBar;
    double viewStart = 0.0;
    double topTrack = 0.0;
    int trackHeight = 38;
    juce::Rectangle<float> clipBody(const Clip&) const;
    int pointAt(int clipIndex, juce::Point<float>) const;
    void paintAutomation(juce::Graphics&, const Clip&, juce::Rectangle<float> body, juce::Colour) const;
    void editPoint(juce::Point<float>, bool snapToGrid);

    int pickedAutomation = -1;
    int pointClip = -1;
    int pointIndex = -1;
    bool tensionDrag = false;
    std::set<int> selection;
    std::vector<Clip> clipboard;
    std::vector<Clip> original;
    juce::Point<float> dragOrigin;
    juce::Rectangle<float> band;
    int anchor = -1;
    double playhead = -1.0;
};
}
