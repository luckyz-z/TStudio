#include "UI/ScriptConsole.h"
#include "UI/Theme.h"

namespace ts
{
namespace
{
constexpr const char* starter = R"lua(local channel = ts.selected_channel()
local notes = {}
for step = 0, 15 do
  notes[#notes + 1] = { start = step * ts.STEP, length = ts.STEP, key = ts.note("C5"), velocity = step % 4 == 0 and 0.9 or 0.6 }
end
ts.set_notes(channel, notes)
print("Wrote " .. #notes .. " notes to channel " .. channel)
)lua";
}

ScriptConsole::ScriptConsole(ScriptEngine& e) : engine(e)
{
    editor.setColour(juce::CodeEditorComponent::backgroundColourId, theme::lcd);
    editor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, theme::panelDark);
    editor.setColour(juce::CodeEditorComponent::lineNumberTextId, theme::textDim);
    editor.setColour(juce::CodeEditorComponent::defaultTextColourId, theme::text);
    editor.setColour(juce::CodeEditorComponent::highlightColourId, theme::accent.withAlpha(0.3f));
    editor.setColour(juce::CaretComponent::caretColourId, theme::accent);
    editor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));
    editor.setTabSize(2, true);
    addAndMakeVisible(editor);

    output.setMultiLine(true);
    output.setReadOnly(true);
    output.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    output.setColour(juce::TextEditor::backgroundColourId, theme::panelDark);
    output.setColour(juce::TextEditor::textColourId, theme::text);
    addAndMakeVisible(output);

    run.onClick = [this] { runCode(); };
    examples.onClick = [this] { showExamples(); };
    clear.onClick = [this] { output.clear(); };
    for (auto* button : { &run, &examples, &clear })
        addAndMakeVisible(button);

    document.replaceAllContent(starter);
    setSize(820, 620);
}

void ScriptConsole::paint(juce::Graphics& g)
{
    g.fillAll(theme::window);
    g.setColour(theme::textDim);
    g.setFont(theme::font(12.0f));
    g.drawText("Lua API: ts.notes / ts.set_notes / ts.add_channel / ts.tempo / ts.chord / ts.note(\"C5\") ...  See docs/LUA_API.md",
               getLocalBounds().removeFromBottom(24).reduced(10, 0), juce::Justification::centredLeft);
}

void ScriptConsole::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromBottom(18);
    auto bar = area.removeFromTop(30);
    run.setBounds(bar.removeFromLeft(150).reduced(0, 2));
    bar.removeFromLeft(8);
    examples.setBounds(bar.removeFromLeft(110).reduced(0, 2));
    clear.setBounds(bar.removeFromRight(110).reduced(0, 2));
    area.removeFromTop(6);
    output.setBounds(area.removeFromBottom(150));
    area.removeFromBottom(6);
    editor.setBounds(area);
}

bool ScriptConsole::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::commandModifier, 0))
    {
        runCode();
        return true;
    }
    return false;
}

void ScriptConsole::load(const juce::String& code)
{
    document.replaceAllContent(code);
}

void ScriptConsole::runCode()
{
    const auto result = engine.run(document.getAllContent(), "console");
    output.moveCaretToEnd();
    const juce::String line("\n");
    output.insertTextAtCaret("> run" + line);
    if (result.output.isNotEmpty())
        output.insertTextAtCaret(result.output);
    output.insertTextAtCaret(result.ok ? "ok" + line : "error: " + result.error + line);
}

void ScriptConsole::showExamples()
{
    juce::PopupMenu menu;
    for (const auto& name : ScriptEngine::builtinScripts())
        menu.addItem(name, [this, name] { load(ScriptEngine::builtinScript(name)); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&examples));
}
}
