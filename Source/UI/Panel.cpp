#include "UI/Panel.h"
#include "UI/Theme.h"

namespace ts
{
namespace
{
juce::Path crossShape()
{
    juce::Path path;
    path.addLineSegment({ 0.0f, 0.0f, 8.0f, 8.0f }, 1.6f);
    path.addLineSegment({ 8.0f, 0.0f, 0.0f, 8.0f }, 1.6f);
    return path;
}

juce::Path boxShape()
{
    juce::Path path;
    path.addRectangle(0.0f, 0.0f, 9.0f, 8.0f);
    path.addRectangle(1.5f, 2.5f, 6.0f, 4.0f);
    path.setUsingNonZeroWinding(false);
    return path;
}
}

class Panel::Fronter final : public juce::MouseListener
{
public:
    explicit Fronter(Panel& p) : panel(p) {}
    void mouseDown(const juce::MouseEvent&) override { panel.toFront(false); }

private:
    Panel& panel;
};

Panel::Panel(const juce::String& t, juce::Component& c)
    : title(t),
      content(c),
      closeButton("Close", theme::textDim, theme::text, theme::accent),
      maximiseButton("Maximise", theme::textDim, theme::text, theme::accent),
      fronter(std::make_unique<Fronter>(*this))
{
    closeButton.setShape(crossShape(), false, true, false);
    maximiseButton.setShape(boxShape(), false, true, false);
    closeButton.onClick = [this] { setVisible(false); };
    maximiseButton.onClick = [this] { setMaximised(!maximised); };
    closeButton.setTooltip("Close");
    maximiseButton.setTooltip("Maximise / restore");
    for (auto* child : std::initializer_list<juce::Component*> { &closeButton, &maximiseButton, &border })
    {
        child->setWantsKeyboardFocus(false);
        addAndMakeVisible(child);
    }
    addAndMakeVisible(content);
    addMouseListener(fronter.get(), true);
    constrainer.setMinimumSize(220, 120);
    constrainer.setMinimumOnscreenAmounts(titleHeight, 80, 40, 80);
    shadower.setOwner(this);
    setVisible(false);
}

Panel::~Panel()
{
    removeMouseListener(fronter.get());
}

void Panel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto header = getLocalBounds().removeFromTop(titleHeight);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(isFrontmost() ? theme::titleActive : theme::title);
    g.fillRoundedRectangle(header.toFloat(), 4.0f);
    g.fillRect(header.withTrimmedTop(titleHeight / 2));
    g.setColour(theme::text);
    g.setFont(theme::font(13.0f, juce::Font::bold));
    g.drawText(title, header.reduced(10, 0).withTrimmedRight(50), juce::Justification::centredLeft);
    g.setColour(theme::outline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
}

void Panel::resized()
{
    auto header = getLocalBounds().removeFromTop(titleHeight).reduced(6, 0);
    closeButton.setBounds(header.removeFromRight(18).withSizeKeepingCentre(12, 12));
    header.removeFromRight(4);
    maximiseButton.setBounds(header.removeFromRight(18).withSizeKeepingCentre(12, 12));
    border.setBounds(getLocalBounds());
    border.setBorderThickness(juce::BorderSize<int>(maximised ? 0 : 4));
    border.toBack();
    content.setBounds(getLocalBounds().withTrimmedTop(titleHeight).reduced(2, 0).withTrimmedBottom(2));
}

void Panel::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == this && e.y < titleHeight && !maximised)
        dragger.startDraggingComponent(this, e);
}

void Panel::mouseDrag(const juce::MouseEvent& e)
{
    if (e.eventComponent == this && e.mouseDownPosition.y < float(titleHeight) && !maximised)
        dragger.dragComponent(this, e, &constrainer);
}

void Panel::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.eventComponent == this && e.y < titleHeight)
        setMaximised(!maximised);
}

void Panel::parentSizeChanged()
{
    if (maximised && getParentComponent() != nullptr)
        setBounds(getParentComponent()->getLocalBounds());
}

void Panel::present()
{
    setVisible(true);
    toFront(true);
    content.grabKeyboardFocus();
}

void Panel::toggle()
{
    if (isVisible() && isFrontmost())
        setVisible(false);
    else
        present();
}

void Panel::setMaximised(bool shouldBeMaximised)
{
    if (shouldBeMaximised == maximised || getParentComponent() == nullptr)
        return;
    maximised = shouldBeMaximised;
    if (maximised)
    {
        restoreBounds = getBounds();
        setBounds(getParentComponent()->getLocalBounds());
    }
    else
    {
        setBounds(restoreBounds);
    }
}

bool Panel::isFrontmost() const
{
    const auto* parent = getParentComponent();
    if (parent == nullptr)
        return false;
    for (int i = parent->getNumChildComponents(); --i >= 0;)
    {
        const auto* child = parent->getChildComponent(i);
        if (child == this)
            return true;
        if (child->isVisible() && dynamic_cast<const Panel*>(child) != nullptr)
            return false;
    }
    return false;
}

void Panel::setTitle(const juce::String& newTitle)
{
    title = newTitle;
    repaint(getLocalBounds().removeFromTop(titleHeight));
}
}
