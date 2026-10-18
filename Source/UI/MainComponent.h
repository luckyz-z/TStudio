#pragma once

#include "Engine/Studio.h"
#include "Engine/McpServer.h"
#include "Engine/ScriptEngine.h"
#include "UI/Actions.h"
#include "UI/Browser.h"
#include "UI/ChannelRack.h"
#include "UI/DeviceWindows.h"
#include "UI/Extensions.h"
#include "UI/LookAndFeel.h"
#include "UI/Mixer.h"
#include "UI/Panel.h"
#include "UI/PianoRoll.h"
#include "UI/Playlist.h"
#include "UI/Toolbar.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>

namespace ts
{
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::DragAndDropContainer,
                            public juce::FileDragAndDropTarget,
                            private juce::MenuBarModel,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>&) override;
    void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform(const InvocationInfo&) override;

    void requestQuit();
    void openFile(const juce::File&);
    void openFileWhenSaved(const juce::File& file) { whenSaved([this, file] { openFile(file); }); }
    void openDemo(const juce::String& name);
    void playStartupDemo();
    void showViews(const juce::StringArray& views);
    void silence() { studio.engine.silent.store(true); }
    bool startMcp(bool remember);
    void pressKeys(const juce::String& script);
    void welcome();
    Studio& getStudio() noexcept { return studio; }
    ScriptEngine& getScripts() noexcept { return scripts; }
    bool renderTo(const juce::File& file, juce::String& error) { return studio.renderNow(file, error); }

private:
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int, int) override {}
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void setRecording(bool);

    void whenSaved(std::function<void()> next);
    void chooseOpen();
    void chooseSave(std::function<void(bool)> done);
    void chooseImport();
    void chooseExport(const juce::String& extension = ".wav");
    void chooseExportMidi();
    void saveNewVersion();
    void showAudioSettings();
    void showPluginManager();
    void showDialog(std::unique_ptr<juce::Component>, const juce::String& title);
    void showScriptConsole();
    void runScript(const juce::String& code, const juce::String& name);
    juce::PopupMenu scriptsMenu();
    void updateTitle();
    void placePanels();

    LookAndFeel lookAndFeel;
    Studio studio;
    Automation automation { studio };
    ScriptEngine scripts { automation };
    McpServer mcp { automation, scripts };
    Actions actions;
    juce::ApplicationCommandManager commands;
    juce::MenuBarComponent menuBar { this };
    Toolbar toolbar { studio, commands };
    Browser browser { studio };
    juce::Component workspace;
    Extensions extensions { scripts, studio, workspace };
    Playlist playlist { studio, actions };
    ChannelRack rack { studio, actions };
    PianoRoll pianoRoll { studio, actions };
    Mixer mixer { studio, actions };
    Panel playlistPanel { "Playlist - Arrangement", playlist };
    Panel rackPanel { "Channel rack", rack };
    Panel pianoRollPanel { "Piano roll", pianoRoll };
    Panel mixerPanel { "Mixer", mixer };
    DeviceWindows deviceWindows { *this };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> scriptWindow;
    std::map<int, int> typedNotes;
    std::map<int, RecordedNote> pendingRecorded;
    bool typingKeyboard = false;
    bool browserVisible = true;
    bool panelsPlaced = false;
};
}
