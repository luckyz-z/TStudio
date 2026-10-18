#pragma once

#include "Engine/ScriptEngine.h"
#include "Engine/Studio.h"
#include "UI/Panel.h"

namespace ts
{
class Extensions final
{
public:
    Extensions(ScriptEngine&, Studio&, juce::Component& workspace);
    ~Extensions();

    ScriptEngine::Result reload();
    void addToMenu(juce::PopupMenu&);
    bool keyPressed(const juce::KeyPress&);
    void emit(const juce::String& event, const juce::var& data = {});
    void loaded(const juce::String& source);
    void showPanel(int index);
    const juce::String& log() const noexcept { return messages; }

    std::function<void(const juce::String& title, const juce::String& text)> onMessage;

private:
    class View;
    void poll();
    void report(const juce::String& source, const ScriptEngine::Result&);

    ScriptEngine& scripts;
    Studio& studio;
    juce::Component& workspace;
    std::vector<std::unique_ptr<View>> views;
    std::vector<std::unique_ptr<Panel>> panels;
    juce::StringArray menuNames, panelTitles;
    juce::String messages;
    juce::TimedCallback poller { [this] { poll(); } };
    bool playing = false, dirty = false;
    int pattern = -1, channel = -1;
    double tempo = 0.0;
};
}
