#pragma once

#include "Engine/ScriptEngine.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace ts
{
class ScriptConsole final : public juce::Component
{
public:
    explicit ScriptConsole(ScriptEngine&);

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void load(const juce::String& code);

private:
    void runCode();
    void showExamples();

    ScriptEngine& engine;
    juce::CodeDocument document;
    juce::LuaTokeniser tokeniser;
    juce::CodeEditorComponent editor { document, &tokeniser };
    juce::TextEditor output;
    juce::TextButton run { "Run  (Ctrl+Enter)" }, examples { "Examples" }, clear { "Clear output" };
};
}
