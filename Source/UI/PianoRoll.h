#pragma once

#include "Engine/Studio.h"
#include "UI/Actions.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>

namespace ts
{
class PianoRoll final : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public juce::DragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
public:
    PianoRoll(Studio&, Actions&);
    ~PianoRoll() override;

    void setChannel(int channelId);
    void setSnapIndex(int index) { snapBox.setSelectedItemIndex(index, juce::dontSendNotification); repaint(); }
    int getChannel() const noexcept { return channelId; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    std::function<void(const juce::String&)> onTitleChanged;

private:
    enum class Tool
    {
        draw,
        paint,
        erase,
        select,
        mute,
        slice
    };

    enum class Drag
    {
        none,
        move,
        resize,
        paint,
        erase,
        select,
        velocity,
        keyboard,
        ruler,
        mute
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    const std::vector<Note>* notes() const;
    template <typename Function>
    void edit(Function&&);
    Channel* channel() const;
    int snap() const;

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> velocityArea() const;
    float xForTick(double tick) const;
    double tickForX(float x) const;
    float yForKey(int key) const;
    int keyForY(float y) const;
    juce::Rectangle<float> noteBounds(const Note&) const;
    int noteAt(juce::Point<float>) const;
    bool nearRightEdge(const Note&, juce::Point<float>) const;
    int viewLength() const;

    void paintGrid(juce::Graphics&, juce::Rectangle<int>) const;
    void paintNotes(juce::Graphics&, juce::Rectangle<int>) const;
    void paintKeyboard(juce::Graphics&, juce::Rectangle<int>) const;
    void paintRuler(juce::Graphics&, juce::Rectangle<int>) const;
    void paintVelocity(juce::Graphics&, juce::Rectangle<int>) const;
    void updateScrollBars();
    void updateHeader();
    void showChannelMenu();

    void preview(int key, float velocity);
    void stopPreview();
    void eraseAt(juce::Point<float>);
    void paintAt(juce::Point<float>);
    void setVelocityAt(juce::Point<float>);
    void deleteSelection();
    void transpose(int semitones);
    void shift(int ticks);
    void duplicateSelection();
    void quantizeSelection();
    void copySelection();
    void paste();
    void selectAll();
    void setTool(Tool);
    void toggleFlag(bool Note::*flag, juce::TextButton& button);
    void sliceAt(juce::Point<float>);
    int scaleKey(int key) const;
    bool inScale(int key) const;
    Note newNote(int start, int key) const;
    void legato();
    void chop();
    void glue();
    void invertSelection();
    void stepChannel(int direction, bool withNotes);
    void zoom(double factor);
    void chooseMidiImport();
    void importMidi(const juce::File&);
    void showMidiMenu();

    Studio& studio;
    Actions& actions;
    juce::TextButton channelButton, midiButton { "MIDI" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ComboBox snapBox, chordBox;
    juce::TextButton drawTool { "Draw" }, paintTool { "Paint" }, eraseTool { "Delete" }, selectTool { "Select" }, muteTool { "Mute" }, sliceTool { "Slice" };
    juce::TextButton slideButton { "Slide" }, portaButton { "Porta" };
    juce::ComboBox scaleBox, rootBox;
    bool muteDragValue = false;
    juce::ScrollBar horizontal { false }, vertical { true };

    int channelId = -1;
    Tool tool = Tool::draw;
    Drag drag = Drag::none;
    double tickWidth = 0.75;
    double viewStart = 0.0;
    double topKey = 84.0;
    int keyHeight = 14;
    int lastLength = ticksPerStep;
    std::set<int> selection;
    std::vector<Note> original;
    std::vector<Note> clipboard;
    juce::Point<float> dragOrigin;
    juce::Rectangle<float> band;
    int anchor = -1;
    int previewKey = -1;
    double playhead = -1.0;
    size_t knownCount = 0;
};
}
