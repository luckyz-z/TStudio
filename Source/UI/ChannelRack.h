#pragma once

#include "Engine/Studio.h"
#include "UI/Actions.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class ChannelRack final : public juce::Component,
                          public juce::DragAndDropTarget,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    ChannelRack(Studio&, Actions&);
    ~ChannelRack() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

    int preferredWidth() const;
    int preferredHeight() const;

    std::function<void()> onPreferredSizeChanged;

private:
    class Row;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuildRows();
    void showAddMenu();
    int visibleSteps() const;

    Studio& studio;
    Actions& actions;
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<Row> rows;
    std::vector<int> rowIds;
    juce::Slider swing;
    juce::TextButton add { "+" };
    int steps = 16;
};
}
