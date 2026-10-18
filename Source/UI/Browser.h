#pragma once

#include "Engine/Studio.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts
{
class Browser final : public juce::Component, private juce::ChangeListener
{
public:
    explicit Browser(Studio&);
    ~Browser() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static bool isBrowserDrag(const juce::var& description);
    static bool activate(Studio&, const juce::String& description);

    class Item;

    std::function<void(const juce::String&)> onOpenDemo;
    static inline std::function<void(const juce::File&)> openProject, runScript;

private:

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuild();
    void chooseFolder();
    juce::StringArray userFolders() const;
    void setUserFolders(const juce::StringArray&);
    juce::String projectSignature() const;

    Studio& studio;
    juce::TreeView tree;
    std::unique_ptr<Item> root;
    juce::TextButton addFolder { "+" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String signature;
};
}
