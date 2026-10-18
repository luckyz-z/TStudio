#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class Panel final : public juce::Component
{
public:
    static constexpr int titleHeight = 22;

    Panel(const juce::String& title, juce::Component& content);
    ~Panel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void parentSizeChanged() override;

    void present();
    void toggle();
    void setMaximised(bool);
    bool isFrontmost() const;
    bool isMaximised() const noexcept { return maximised; }
    void setTitle(const juce::String&);

private:
    class Fronter;

    juce::String title;
    juce::Component& content;
    juce::ComponentDragger dragger;
    juce::ComponentBoundsConstrainer constrainer;
    juce::ResizableBorderComponent border { this, &constrainer };
    juce::ShapeButton closeButton, maximiseButton;
    juce::DropShadower shadower { juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 12, { 0, 3 }) };
    std::unique_ptr<Fronter> fronter;
    juce::Rectangle<int> restoreBounds;
    bool maximised = false;
};
}
