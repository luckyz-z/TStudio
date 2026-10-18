#include "UI/Community.h"
#include "UI/Theme.h"

#include <BinaryData.h>

namespace ts::community
{
namespace
{
class StarPrompt final : public juce::Component
{
public:
    explicit StarPrompt(juce::PropertiesFile& s) : settings(s), logo(juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize))
    {
        star.setColour(juce::TextButton::buttonColourId, theme::accent);
        star.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        for (auto* button : { &star, &later, &never })
        {
            button->setWantsKeyboardFocus(false);
            addAndMakeVisible(button);
        }
        star.onClick = [this]
        {
            juce::URL(repository).launchInDefaultBrowser();
            remember("starred");
        };
        later.onClick = [this]
        {
            settings.setValue("starPromptNext", settings.getIntValue("launches") + 4);
            remember("later");
        };
        never.onClick = [this] { remember("never"); };
        setSize(460, 300);
    }

    void paint(juce::Graphics& g) override
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b3135), 0.0f, 0.0f, juce::Colour(0xff181c1e), 0.0f, float(getHeight()), false));
        g.fillAll();
        g.drawImage(logo, juce::Rectangle<float>(96.0f, 96.0f).withCentre({ float(getWidth()) / 2.0f, 66.0f }), juce::RectanglePlacement::centred);
        g.setColour(theme::text);
        g.setFont(theme::font(22.0f, juce::Font::bold));
        g.drawText("Enjoying TStudio?", 0, 120, getWidth(), 30, juce::Justification::centred);
        g.setColour(theme::textDim);
        g.setFont(theme::font(14.0f));
        g.drawFittedText("TStudio is free and open source, built by producers for producers.\n"
                         "A star on GitHub helps other people find it. It takes one click.",
                         juce::Rectangle<int>(30, 154, getWidth() - 60, 48), juce::Justification::centred, 3);
    }

    void resized() override
    {
        star.setBounds(getWidth() / 2 - 120, 214, 240, 36);
        later.setBounds(getWidth() / 2 - 120, 258, 116, 26);
        never.setBounds(getWidth() / 2 + 4, 258, 116, 26);
    }

private:
    void remember(const juce::String& answer)
    {
        settings.setValue("starPrompt", answer);
        settings.saveIfNeeded();
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState(0);
    }

    juce::PropertiesFile& settings;
    juce::Image logo;
    juce::TextButton star { juce::String::fromUTF8("\xe2\x98\x85  Star TStudio on GitHub") }, later { "Maybe later" }, never { "Don't ask again" };
};
}

std::unique_ptr<juce::Component> createStarPrompt(juce::PropertiesFile& settings)
{
    return std::make_unique<StarPrompt>(settings);
}

void showStarPrompt(juce::Component& parent, juce::PropertiesFile& settings)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new StarPrompt(settings));
    options.dialogTitle = "TStudio";
    options.dialogBackgroundColour = theme::window;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = &parent;
    options.launchAsync();
}

void maybeAskForStar(juce::Component& parent, juce::PropertiesFile& settings)
{
    const auto launches = settings.getIntValue("launches") + 1;
    settings.setValue("launches", launches);
    settings.saveIfNeeded();
    const auto answer = settings.getValue("starPrompt");
    if (answer == "starred" || answer == "never")
        return;
    if (answer == "later" && launches < settings.getIntValue("starPromptNext"))
        return;
    juce::Timer::callAfterDelay(2500, [safe = juce::Component::SafePointer<juce::Component>(&parent), &settings]
    {
        if (safe != nullptr && safe->isShowing())
            showStarPrompt(*safe, settings);
    });
}
}
