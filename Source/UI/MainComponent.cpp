#include "UI/MainComponent.h"
#include "UI/ChannelSettings.h"
#include "UI/Commands.h"
#include "UI/Community.h"
#include "UI/Controls.h"
#include "UI/Demos.h"
#include "UI/ScriptConsole.h"
#include "UI/Theme.h"
#include "Engine/Builtins/ParameterPanel.h"
#include "Engine/Builtins/Sampler.h"

namespace ts
{
namespace
{
constexpr int toolbarHeight = Toolbar::rowHeight * 2;
constexpr int browserWidth = 230;
constexpr const char* projectExtension = ".tsp";

const std::map<juce::juce_wchar, int> typingLayout {
    { 'z', 0 }, { 's', 1 }, { 'x', 2 }, { 'd', 3 }, { 'c', 4 }, { 'v', 5 }, { 'g', 6 }, { 'b', 7 }, { 'h', 8 }, { 'n', 9 }, { 'j', 10 }, { 'm', 11 },
    { ',', 12 }, { 'l', 13 }, { '.', 14 }, { ';', 15 }, { '/', 16 },
    { 'q', 12 }, { '2', 13 }, { 'w', 14 }, { '3', 15 }, { 'e', 16 }, { 'r', 17 }, { '5', 18 }, { 't', 19 }, { '6', 20 }, { 'y', 21 }, { '7', 22 }, { 'u', 23 },
    { 'i', 24 }, { '9', 25 }, { 'o', 26 }, { '0', 27 }, { 'p', 28 },
};
}

MainComponent::MainComponent()
{
    setLookAndFeel(&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    actions.openPianoRoll = [this](int id)
    {
        pianoRoll.setChannel(id);
        pianoRollPanel.present();
    };
    actions.showDevice = [this](Device& device, const juce::String& title) { deviceWindows.show(device, title); };
    actions.showMixerTrack = [this](int track)
    {
        mixer.selectTrack(track);
        mixerPanel.present();
    };
    actions.showChannelRack = [this] { rackPanel.present(); };
    actions.showChannelSettings = [this](int id)
    {
        if (const auto* channel = studio.project.findChannel(id))
            showDialog(std::make_unique<ChannelSettings>(studio, id), channel->name + " - Channel settings");
    };
    actions.typingKeyboardActive = [this] { return typingKeyboard; };
    actions.runScript = [this](const juce::String& name) { runScript(ScriptEngine::builtinScript(name), name); };
    actions.createAutomation = [this](const AutomationTarget& target, float value)
    {
        studio.addAutomation(target, studio.describe(target), value);
        playlistPanel.present();
    };
    ParameterPanel::createAutomation = [this](juce::AudioProcessor& processor, int index)
    {
        if (studio.automateParameter(processor, index) >= 0)
            playlistPanel.present();
    };
    pianoRoll.onTitleChanged = [this](const juce::String& title) { pianoRollPanel.setTitle(title); };
    rack.onPreferredSizeChanged = [this]
    {
        if (!panelsPlaced)
            return;
        const auto height = juce::jmin(rack.preferredHeight() + Panel::titleHeight, workspace.getHeight() - rackPanel.getY());
        rackPanel.setSize(juce::jmax(rackPanel.getWidth(), rack.preferredWidth()), height);
    };
    browser.onOpenDemo = [this](const juce::String& name) { whenSaved([this, name] { openDemo(name); }); };
    Browser::openProject = [this](const juce::File& file) { whenSaved([this, file] { openFile(file); }); };
    Browser::runScript = [this](const juce::File& file) { runScript(file.loadFileAsString(), file.getFileName()); };

    studio.onDeviceRemoving = [this](juce::AudioProcessor* processor) { deviceWindows.close(processor); };
    studio.onContentReplacing = [this] { deviceWindows.closeAll(); };

    commands.registerAllCommandsForTarget(this);
    setApplicationCommandManagerToWatch(&commands);

    addAndMakeVisible(menuBar);
    addAndMakeVisible(toolbar);
    addAndMakeVisible(browser);
    addAndMakeVisible(workspace);
    for (auto* panel : { &playlistPanel, &rackPanel, &pianoRollPanel, &mixerPanel })
        workspace.addChildComponent(panel);

    pianoRoll.setChannel(studio.project.selectedChannel);
    toolbar.onGlobalSnap = [this](int index)
    {
        pianoRoll.setSnapIndex(index);
        playlist.setSnapIndex(index);
    };
    studio.project.addChangeListener(this);
    extensions.onMessage = [](const juce::String& title, const juce::String& text) { ui::showMessage(title, text); };
    extensions.reload();
    if (studio.plugins.settings().getBoolValue("mcpServer", false))
        startMcp(false);
    setWantsKeyboardFocus(true);
    setSize(1440, 900);
}

MainComponent::~MainComponent()
{
    ParameterPanel::createAutomation = nullptr;
    Browser::openProject = nullptr;
    Browser::runScript = nullptr;
    studio.project.removeChangeListener(this);
    studio.onDeviceRemoving = nullptr;
    studio.onContentReplacing = nullptr;
    deviceWindows.closeAll();
    scriptWindow.reset();
    setApplicationCommandManagerToWatch(nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::workspace);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    toolbar.setBounds(area.removeFromTop(toolbarHeight));
    menuBar.setBounds(0, 4, Toolbar::menuWidth, Toolbar::rowHeight - 8);
    menuBar.toFront(false);
    browser.setVisible(browserVisible);
    if (browserVisible)
        browser.setBounds(area.removeFromLeft(browserWidth));
    workspace.setBounds(area);
    if (!panelsPlaced && workspace.getWidth() > 0)
        placePanels();
}

void MainComponent::placePanels()
{
    panelsPlaced = true;
    const auto area = workspace.getLocalBounds();
    playlistPanel.setBounds(area.reduced(6));
    rackPanel.setBounds(40, 40, rack.preferredWidth(), rack.preferredHeight() + Panel::titleHeight);
    pianoRollPanel.setBounds(area.reduced(60, 40).withTrimmedTop(20));
    mixerPanel.setBounds(area.withTrimmedTop(area.getHeight() / 3).reduced(30, 10));
    playlistPanel.setVisible(true);
    rackPanel.setVisible(true);
    rackPanel.toFront(false);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (extensions.keyPressed(key))
        return true;
    const auto code = key.getKeyCode();
    const auto plain = !key.getModifiers().isAnyModifierKeyDown();
    if (typingKeyboard && plain)
        if (const auto found = typingLayout.find(juce::CharacterFunctions::toLowerCase(key.getTextCharacter() != 0 ? key.getTextCharacter() : juce::juce_wchar(code))); found != typingLayout.end())
        {
            if (typedNotes.count(code) == 0)
            {
                const auto note = juce::jlimit(0, 127, defaultKey + found->second);
                typedNotes[code] = note;
                studio.engine.sendLive(juce::MidiMessage::noteOn(1, note, 0.8f));
            }
            return true;
        }
    if (plain && code >= juce::KeyPress::numberPad1 && code <= juce::KeyPress::numberPad9)
    {
        const auto index = code - juce::KeyPress::numberPad1;
        while (int(studio.project.patterns.size()) <= index)
            studio.addPattern();
        studio.selectPattern(index);
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        for (int i = workspace.getNumChildComponents(); --i >= 0;)
            if (auto* panel = dynamic_cast<Panel*>(workspace.getChildComponent(i)); panel != nullptr && panel->isVisible())
            {
                panel->setVisible(false);
                return true;
            }
    }
    const auto command = commands.getKeyMappings()->findCommandForKeyPress(key);
    return command != 0 && commands.invokeDirectly(command, false);
}

bool MainComponent::keyStateChanged(bool)
{
    for (auto it = typedNotes.begin(); it != typedNotes.end();)
    {
        if (juce::KeyPress::isKeyCurrentlyDown(it->first))
        {
            ++it;
            continue;
        }
        studio.engine.sendLive(juce::MidiMessage::noteOff(1, it->second));
        it = typedNotes.erase(it);
    }
    return false;
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("tsp;lfs;flp") || Sampler::isAudioFile(file))
            return true;
    }
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("tsp;lfs;flp"))
        {
            whenSaved([this, file] { openFile(file); });
            return;
        }
        if (Sampler::isAudioFile(file))
            studio.addSampleChannel(file);
    }
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& list)
{
    using namespace command;
    const juce::CommandID ids[] { newProject, open, save, saveAs, importFlp, exportWave, undo, redo, play, stop, songMode, newPattern, clonePattern, renamePattern,
                    deletePattern, nextPattern, previousPattern, showPlaylist, showChannelRack, showPianoRoll, showMixer, showBrowser, metronome,
                    command::typingKeyboard, audioSettings, managePlugins, about };
    list.addArray(ids, juce::numElementsInArray(ids));
    list.add(command::record);
    list.add(command::startupDemo);
    list.add(command::scriptConsole);
    list.add(command::mcpServer);
    list.add(command::openScriptsFolder);
    for (const auto extra : { command::saveNewVersion, command::exportMidi, command::exportMp3, command::panic, command::pause, command::rewind, command::nextBar,
                              command::previousBar, command::emptyPattern, command::closeAllWindows, command::arrangeWindows, command::togglePlaylistSize,
                              command::startupSound, command::starOnGitHub, command::reportBug, command::pluginDirectory })
        list.add(extra);
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using juce::KeyPress;
    using juce::ModifierKeys;
    const auto ctrl = ModifierKeys::commandModifier;
    switch (id)
    {
        case command::newProject: info.setInfo("New", "Start a new project", "File", 0); break;
        case command::saveNewVersion: info.setInfo("Save new version", "Save a numbered copy of the project and keep working on it", "File", 0); info.addDefaultKeypress('n', ctrl); break;
        case command::exportMp3: info.setInfo("MP3 file...", "Render the song to an MP3 file (320 kbps)", "File", 0); info.addDefaultKeypress('r', ctrl | ModifierKeys::shiftModifier); break;
        case command::exportMidi: info.setInfo("MIDI file...", "Export the current pattern as a MIDI file", "File", 0); info.addDefaultKeypress('m', ctrl | ModifierKeys::shiftModifier); break;
        case command::panic: info.setInfo("Stop sound (panic)", "Silence every hanging note", "Transport", 0); info.addDefaultKeypress('h', ctrl); break;
        case command::pause: info.setInfo("Start / pause", "Start playback or pause at the current position", "Transport", 0); info.addDefaultKeypress(KeyPress::spaceKey, ctrl); break;
        case command::rewind: info.setInfo("Go to start", "Move the playback position to the start", "Transport", 0); info.addDefaultKeypress(KeyPress::homeKey, 0); break;
        case command::nextBar: info.setInfo("Next bar", "Jump forward one bar", "Transport", 0); info.addDefaultKeypress(KeyPress::numberPadMultiply, 0); break;
        case command::previousBar: info.setInfo("Previous bar", "Jump back one bar", "Transport", 0); info.addDefaultKeypress(KeyPress::numberPadDivide, 0); break;
        case command::emptyPattern: info.setInfo("Next empty pattern", "Jump to a new empty pattern without naming it", "Patterns", 0); info.addDefaultKeypress(KeyPress::F4Key, ctrl); break;
        case command::closeAllWindows: info.setInfo("Close all windows", "Hide every window in the workspace", "View", 0); info.addDefaultKeypress(KeyPress::F12Key, 0); break;
        case command::arrangeWindows: info.setInfo("Arrange windows", "Put the windows back in their default places", "View", 0); info.addDefaultKeypress('h', ctrl | ModifierKeys::shiftModifier); break;
        case command::togglePlaylistSize: info.setInfo("Maximise playlist", "Toggle the playlist between maximised and normal", "View", 0); info.addDefaultKeypress(KeyPress::returnKey, 0); break;
        case command::open: info.setInfo("Open...", "Open a project", "File", 0); info.addDefaultKeypress('o', ctrl); break;
        case command::save: info.setInfo("Save", "Save the project", "File", 0); info.addDefaultKeypress('s', ctrl); break;
        case command::saveAs: info.setInfo("Save as...", "Save the project under a new name", "File", 0); info.addDefaultKeypress('s', ctrl | ModifierKeys::shiftModifier); break;
        case command::importFlp: info.setInfo("FL Studio project (.flp)...", "Import an FL Studio project", "File", 0); break;
        case command::exportWave: info.setInfo("Audio file (WAV, FLAC, OGG, MP3)...", "Render the song to an audio file", "File", 0); info.addDefaultKeypress('r', ctrl); break;
        case command::undo: info.setInfo("Undo", "Undo the last edit", "Edit", 0); info.addDefaultKeypress('z', ctrl); info.addDefaultKeypress('z', ctrl | ModifierKeys::altModifier); break;
        case command::redo: info.setInfo("Redo", "Redo the last undone edit", "Edit", 0); info.addDefaultKeypress('y', ctrl); info.addDefaultKeypress('z', ctrl | ModifierKeys::shiftModifier); break;
        case command::play: info.setInfo("Play / pause", "Start or stop playback", "Transport", 0); info.addDefaultKeypress(KeyPress::spaceKey, 0); break;
        case command::stop: info.setInfo("Stop", "Stop playback", "Transport", 0); break;
        case command::record: info.setInfo("Record", "Record notes into the current pattern", "Transport", 0); info.setTicked(studio.engine.recording.load()); info.addDefaultKeypress('r', 0); info.addDefaultKeypress('r', ModifierKeys::shiftModifier); break;
        case command::songMode: info.setInfo("Song mode", "Toggle pattern / song mode", "Transport", 0); info.setTicked(studio.project.songMode.load()); info.addDefaultKeypress('l', 0); break;
        case command::newPattern: info.setInfo("New pattern", "Create an empty pattern", "Patterns", 0); info.addDefaultKeypress(KeyPress::F4Key, 0); break;
        case command::clonePattern: info.setInfo("Clone pattern", "Duplicate the current pattern", "Patterns", 0); info.addDefaultKeypress('c', ModifierKeys::shiftModifier | ctrl); break;
        case command::renamePattern: info.setInfo("Rename pattern...", "Rename the current pattern", "Patterns", 0); info.addDefaultKeypress(KeyPress::F2Key, 0); break;
        case command::deletePattern: info.setInfo("Delete pattern", "Delete the current pattern", "Patterns", 0); break;
        case command::nextPattern: info.setInfo("Next pattern", "Select the next pattern", "Patterns", 0); info.addDefaultKeypress(KeyPress::numberPadAdd, 0); break;
        case command::previousPattern: info.setInfo("Previous pattern", "Select the previous pattern", "Patterns", 0); info.addDefaultKeypress(KeyPress::numberPadSubtract, 0); break;
        case command::showPlaylist: info.setInfo("Playlist", "Show the playlist", "View", 0); info.addDefaultKeypress(KeyPress::F5Key, 0); break;
        case command::showChannelRack: info.setInfo("Channel rack", "Show the channel rack", "View", 0); info.addDefaultKeypress(KeyPress::F6Key, 0); break;
        case command::showPianoRoll: info.setInfo("Piano roll", "Show the piano roll", "View", 0); info.addDefaultKeypress(KeyPress::F7Key, 0); break;
        case command::showMixer: info.setInfo("Mixer", "Show the mixer", "View", 0); info.addDefaultKeypress(KeyPress::F9Key, 0); break;
        case command::showBrowser: info.setInfo("Browser", "Show or hide the browser", "View", 0); info.setTicked(browserVisible); info.addDefaultKeypress(KeyPress::F8Key, ModifierKeys::altModifier); break;
        case command::metronome: info.setInfo("Metronome", "Toggle the metronome", "Options", 0); info.setTicked(studio.engine.metronome.load()); info.addDefaultKeypress('m', ctrl); break;
        case command::typingKeyboard: info.setInfo("Typing keyboard to piano", "Play the selected channel with the computer keyboard", "Options", 0); info.setTicked(typingKeyboard); info.addDefaultKeypress('t', ctrl); break;
        case command::audioSettings: info.setInfo("Audio settings...", "Choose audio and MIDI devices", "Options", 0); info.addDefaultKeypress(KeyPress::F10Key, 0); break;
        case command::managePlugins: info.setInfo("Manage plugins...", "Scan for VST3 plugins", "Options", 0); break;
        case command::startupDemo: info.setInfo("Play demo on startup", "Open and play a demo project when TStudio starts", "Options", 0); info.setTicked(studio.plugins.settings().getBoolValue("playStartupDemo", true)); break;
        case command::scriptConsole: info.setInfo("Script console...", "Write and run Lua scripts", "Tools", 0); info.addDefaultKeypress('l', ctrl | ModifierKeys::shiftModifier); break;
        case command::openScriptsFolder: info.setInfo("Open scripts folder", "Show your Lua scripts folder", "Tools", 0); break;
        case command::mcpServer:
            info.setInfo("MCP server (AI control) on 127.0.0.1:" + juce::String(McpServer::defaultPort), "Let AI assistants control TStudio over the Model Context Protocol", "Tools", 0);
            info.setTicked(mcp.isRunning());
            break;
        case command::about: info.setInfo("About TStudio", "About this program", "Help", 0); break;
        case command::startupSound: info.setInfo("Startup sound", "Play the TStudio chirp when the app opens", "Options", 0); info.setTicked(studio.plugins.settings().getBoolValue("startupSound", true)); break;
        case command::starOnGitHub: info.setInfo("Star TStudio on GitHub", "Open the TStudio repository", "Help", 0); break;
        case command::reportBug: info.setInfo("Report a bug or request a feature...", "Open the issue tracker", "Help", 0); break;
        case command::pluginDirectory: info.setInfo("Community plugins...", "Browse free and paid TStudio plugins", "Help", 0); break;
        default: break;
    }
}

bool MainComponent::perform(const InvocationInfo& info)
{
    auto& project = studio.project;
    switch (info.commandID)
    {
        case command::newProject: whenSaved([this] { studio.newProject(); }); break;
        case command::open: whenSaved([this] { chooseOpen(); }); break;
        case command::save:
            if (project.file.hasFileExtension(projectExtension))
            {
                juce::String error;
                if (!studio.save(project.file, error))
                    ui::showMessage("Save failed", error);
            }
            else
            {
                chooseSave({});
            }
            break;
        case command::saveAs: chooseSave({}); break;
        case command::importFlp: whenSaved([this] { chooseImport(); }); break;
        case command::exportWave: chooseExport(); break;
        case command::undo: project.undo(); break;
        case command::redo: project.redo(); break;
        case command::play: studio.engine.setPlaying(!studio.engine.isPlaying()); break;
        case command::pause:
            if (studio.engine.isPlaying())
            {
                const auto at = studio.engine.getSongPosition();
                studio.engine.setPlaying(false);
                studio.engine.setSongPosition(at);
            }
            else
                studio.engine.setPlaying(true);
            break;
        case command::panic: studio.engine.panic(); break;
        case command::rewind: studio.engine.setSongPosition(0.0); break;
        case command::nextBar:
        case command::previousBar:
        {
            const auto bar = double(ppq * 4);
            const auto current = std::floor(studio.engine.getSongPosition() / bar + 0.001);
            studio.engine.setSongPosition(juce::jmax(0.0, (current + (info.commandID == command::nextBar ? 1.0 : -1.0)) * bar));
            break;
        }
        case command::emptyPattern: studio.addPattern(); break;
        case command::closeAllWindows:
            for (auto* panel : { &playlistPanel, &rackPanel, &pianoRollPanel, &mixerPanel })
                panel->setVisible(false);
            break;
        case command::arrangeWindows:
            pianoRollPanel.setMaximised(false);
            playlistPanel.setMaximised(false);
            placePanels();
            break;
        case command::togglePlaylistSize: playlistPanel.present(); playlistPanel.setMaximised(!playlistPanel.isMaximised()); break;
        case command::saveNewVersion: saveNewVersion(); break;
        case command::exportMidi: chooseExportMidi(); break;
        case command::exportMp3: chooseExport(".mp3"); break;
        case command::stop:
            studio.engine.setPlaying(false);
            setRecording(false);
            break;
        case command::record: setRecording(!studio.engine.recording.load()); break;
        case command::songMode:
            project.songMode.store(!project.songMode.load());
            if (!studio.engine.isPlaying())
                studio.engine.setSongPosition(0.0);
            project.sendChangeMessage();
            commands.commandStatusChanged();
            break;
        case command::newPattern: studio.addPattern(); break;
        case command::clonePattern: studio.clonePattern(project.currentPattern.load()); break;
        case command::renamePattern:
            ui::askText("Rename pattern", project.pattern().name, [this](const juce::String& name)
            {
                if (name.isEmpty())
                    return;
                studio.project.pattern().name = name;
                studio.project.touch();
            });
            break;
        case command::deletePattern: studio.removePattern(project.currentPattern.load()); break;
        case command::nextPattern: studio.selectPattern(project.currentPattern.load() + 1); break;
        case command::previousPattern: studio.selectPattern(project.currentPattern.load() - 1); break;
        case command::showPlaylist: playlistPanel.toggle(); break;
        case command::showChannelRack: rackPanel.toggle(); break;
        case command::showPianoRoll:
            if (!pianoRollPanel.isVisible())
                pianoRoll.setChannel(project.selectedChannel);
            pianoRollPanel.toggle();
            break;
        case command::showMixer: mixerPanel.toggle(); break;
        case command::showBrowser:
            browserVisible = !browserVisible;
            resized();
            commands.commandStatusChanged();
            break;
        case command::metronome:
            studio.engine.metronome.store(!studio.engine.metronome.load());
            commands.commandStatusChanged();
            break;
        case command::typingKeyboard:
            typingKeyboard = !typingKeyboard;
            commands.commandStatusChanged();
            break;
        case command::audioSettings: showAudioSettings(); break;
        case command::managePlugins: showPluginManager(); break;
        case command::scriptConsole: showScriptConsole(); break;
        case command::openScriptsFolder: ScriptEngine::userFolder().startAsProcess(); break;
        case command::mcpServer:
            if (mcp.isRunning())
            {
                mcp.stop();
                studio.plugins.settings().setValue("mcpServer", false);
                studio.plugins.settings().saveIfNeeded();
            }
            else if (startMcp(true))
                ui::showMessage("MCP server running", "AI assistants can now control TStudio at http://127.0.0.1:" + juce::String(McpServer::defaultPort)
                                                          + "/mcp\n\nClaude Code:\nclaude mcp add --transport http tstudio http://127.0.0.1:"
                                                          + juce::String(McpServer::defaultPort) + "/mcp");
            commands.commandStatusChanged();
            break;
        case command::startupSound:
            studio.plugins.settings().setValue("startupSound", !studio.plugins.settings().getBoolValue("startupSound", true));
            studio.plugins.settings().saveIfNeeded();
            commands.commandStatusChanged();
            break;
        case command::starOnGitHub: juce::URL(community::repository).launchInDefaultBrowser(); break;
        case command::reportBug: juce::URL(community::issues).launchInDefaultBrowser(); break;
        case command::pluginDirectory: juce::URL(community::plugins).launchInDefaultBrowser(); break;
        case command::startupDemo:
            studio.plugins.settings().setValue("playStartupDemo", !studio.plugins.settings().getBoolValue("playStartupDemo", true));
            studio.plugins.settings().saveIfNeeded();
            commands.commandStatusChanged();
            break;
        case command::about:
            ui::showMessage("About TStudio", juce::String("TStudio ") + JUCE_APPLICATION_VERSION_STRING
                                                  + "\nAn open-source pattern-based DAW.\nLicensed under the AGPL-3.0. Built with JUCE.\n\n"
                                                    "Not affiliated with Image-Line. FL Studio is a trademark of Image-Line Software.");
            break;
        default: return false;
    }
    return true;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "FILE", "EDIT", "ADD", "PATTERNS", "VIEW", "OPTIONS", "TOOLS", "HELP" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    switch (index)
    {
        case 0:
        {
            menu.addCommandItem(&commands, command::newProject);
            menu.addCommandItem(&commands, command::open);
            juce::PopupMenu demoMenu;
            for (const auto& name : demos::names())
                demoMenu.addItem(name, [this, name] { whenSaved([this, name] { openDemo(name); }); });
            menu.addSubMenu("Open demo project", demoMenu);
            menu.addCommandItem(&commands, command::save);
            menu.addCommandItem(&commands, command::saveAs);
            menu.addCommandItem(&commands, command::saveNewVersion);
            menu.addSeparator();
            juce::PopupMenu import;
            import.addCommandItem(&commands, command::importFlp);
            menu.addSubMenu("Import", import);
            juce::PopupMenu exports;
            exports.addCommandItem(&commands, command::exportWave);
            exports.addCommandItem(&commands, command::exportMp3);
            exports.addCommandItem(&commands, command::exportMidi);
            menu.addSubMenu("Export", exports);
            menu.addSeparator();
            menu.addItem("Exit", [this] { requestQuit(); });
            break;
        }
        case 1:
            menu.addCommandItem(&commands, command::undo);
            menu.addCommandItem(&commands, command::redo);
            menu.addSeparator();
            menu.addCommandItem(&commands, command::pause);
            menu.addCommandItem(&commands, command::rewind);
            menu.addCommandItem(&commands, command::nextBar);
            menu.addCommandItem(&commands, command::previousBar);
            menu.addCommandItem(&commands, command::panic);
            break;
        case 2:
        {
            juce::PopupMenu channels;
            ui::addDeviceItems(channels, studio, true, [this](std::unique_ptr<Device> device)
            {
                if (device == nullptr)
                    return;
                const auto name = device->getName();
                studio.addChannel(std::move(device), name);
            });
            menu.addSubMenu("Channel", channels);
            break;
        }
        case 3:
            menu.addCommandItem(&commands, command::newPattern);
            menu.addCommandItem(&commands, command::emptyPattern);
            menu.addCommandItem(&commands, command::clonePattern);
            menu.addCommandItem(&commands, command::renamePattern);
            menu.addCommandItem(&commands, command::deletePattern);
            menu.addSeparator();
            menu.addCommandItem(&commands, command::nextPattern);
            menu.addCommandItem(&commands, command::previousPattern);
            break;
        case 4:
            menu.addCommandItem(&commands, command::showPlaylist);
            menu.addCommandItem(&commands, command::showChannelRack);
            menu.addCommandItem(&commands, command::showPianoRoll);
            menu.addCommandItem(&commands, command::showMixer);
            menu.addCommandItem(&commands, command::showBrowser);
            menu.addSeparator();
            menu.addCommandItem(&commands, command::togglePlaylistSize);
            menu.addCommandItem(&commands, command::arrangeWindows);
            menu.addCommandItem(&commands, command::closeAllWindows);
            break;
        case 5:
            menu.addCommandItem(&commands, command::audioSettings);
            menu.addCommandItem(&commands, command::managePlugins);
            menu.addSeparator();
            menu.addCommandItem(&commands, command::metronome);
            menu.addCommandItem(&commands, command::typingKeyboard);
            menu.addCommandItem(&commands, command::startupDemo);
            menu.addCommandItem(&commands, command::startupSound);
            break;
        case 6:
            {
                const auto [processor, parameter] = studio.engine.lastTweaked();
                juce::String name;
                for (const auto& channel : studio.project.channels)
                    if (channel->device != nullptr && channel->device->processor.get() == processor)
                        name = channel->name;
                for (const auto& track : studio.project.mixer)
                    for (const auto& slot : track->slots)
                        if (slot != nullptr && slot->processor.get() == processor)
                            name = track->name + " " + slot->getName();
                const auto valid = name.isNotEmpty() && juce::isPositiveAndBelow(parameter, processor->getParameters().size());
                juce::PopupMenu tweaked;
                tweaked.addItem("Create automation clip", valid, false, [this, processor, parameter]
                {
                    if (studio.automateParameter(*processor, parameter) >= 0)
                        playlistPanel.present();
                });
                menu.addSubMenu("Last tweaked" + (valid ? ": " + name + " - " + processor->getParameters()[parameter]->getName(30) : juce::String()), tweaked);
                menu.addSeparator();
            }
            menu.addCommandItem(&commands, command::scriptConsole);
            menu.addSubMenu("Run script", scriptsMenu());
            menu.addCommandItem(&commands, command::openScriptsFolder);
            menu.addCommandItem(&commands, command::mcpServer);
            menu.addSeparator();
            extensions.addToMenu(menu);
            break;
        default:
            menu.addCommandItem(&commands, command::starOnGitHub);
            menu.addCommandItem(&commands, command::pluginDirectory);
            menu.addCommandItem(&commands, command::reportBug);
            menu.addSeparator();
            menu.addCommandItem(&commands, command::about);
            break;
    }
    return menu;
}

void MainComponent::setRecording(bool shouldRecord)
{
    auto& engine = studio.engine;
    if (shouldRecord == engine.recording.load())
        return;
    if (shouldRecord)
    {
        studio.project.checkpoint();
        if (studio.project.songMode.load())
        {
            studio.project.songMode.store(false);
            studio.project.sendChangeMessage();
        }
        engine.recording.store(true);
        if (!engine.isPlaying())
            engine.setPlaying(true);
        startTimerHz(30);
    }
    else
    {
        engine.recording.store(false);
        timerCallback();
        pendingRecorded.clear();
        stopTimer();
    }
    commands.commandStatusChanged();
}

void MainComponent::timerCallback()
{
    std::array<RecordedNote, 256> events;
    const auto count = studio.engine.readRecorded(events.data(), int(events.size()));
    if (count == 0)
        return;
    std::vector<Note> finished;
    for (int i = 0; i < count; ++i)
    {
        const auto& event = events[size_t(i)];
        if (event.on)
        {
            pendingRecorded[event.key] = event;
            continue;
        }
        const auto found = pendingRecorded.find(event.key);
        if (found == pendingRecorded.end())
            continue;
        auto length = event.tick - found->second.tick;
        if (length <= 0.0)
            length += event.loop;
        finished.push_back({ juce::roundToInt(found->second.tick), juce::jmax(6, juce::roundToInt(length)), event.key, juce::jlimit(0.05f, 1.0f, found->second.velocity) });
        pendingRecorded.erase(found);
    }
    if (finished.empty() || studio.project.findChannel(studio.project.selectedChannel) == nullptr)
        return;
    {
        const juce::ScopedLock sl(studio.project.lock);
        auto& list = studio.project.pattern().notes[studio.project.selectedChannel];
        list.insert(list.end(), finished.begin(), finished.end());
    }
    studio.project.touch();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    updateTitle();
    commands.commandStatusChanged();
}

void MainComponent::updateTitle()
{
    auto* window = findParentComponentOfClass<juce::DocumentWindow>();
    if (window == nullptr)
        return;
    const auto& project = studio.project;
    auto name = project.title.isNotEmpty() ? project.title : (project.file != juce::File() ? project.file.getFileNameWithoutExtension() : juce::String("Untitled"));
    if (project.dirty)
        name << " *";
    window->setName("TStudio - " + name);
}

void MainComponent::whenSaved(std::function<void()> next)
{
    if (!studio.project.dirty)
    {
        next();
        return;
    }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle("Save changes?")
                                     .withMessage("The current project has unsaved changes.")
                                     .withButton("Save")
                                     .withButton("Discard")
                                     .withButton("Cancel"),
                                 [this, next](int result)
                                 {
                                     if (result == 2)
                                         next();
                                     else if (result == 1)
                                         chooseSave([next](bool saved) { if (saved) next(); });
                                 });
}

void MainComponent::requestQuit()
{
    whenSaved([] { juce::JUCEApplication::getInstance()->quit(); });
}

void MainComponent::openFile(const juce::File& file)
{
    juce::String message;
    const auto ok = file.hasFileExtension("flp") ? studio.importFlp(file, message) : studio.load(file, message);
    if (!ok)
        ui::showMessage("Could not open " + file.getFileName(), message);
    else if (message.isNotEmpty())
        ui::showMessage("Opened with warnings", message);
    pianoRoll.setChannel(studio.project.selectedChannel);
    updateTitle();
    if (ok)
        extensions.loaded(file.getFullPathName());
}

void MainComponent::openDemo(const juce::String& name)
{
    juce::String message;
    const auto ok = studio.loadXml(demos::load(name), message);
    if (!ok)
        ui::showMessage("Could not open demo", message);
    else if (message.isNotEmpty())
        ui::showMessage("Opened with warnings", message);
    pianoRoll.setChannel(studio.project.selectedChannel);
    updateTitle();
    if (ok)
        extensions.loaded("demo:" + name);
}

void MainComponent::playStartupDemo()
{
    if (!studio.plugins.settings().getBoolValue("playStartupDemo", true))
        return;
    openDemo("Straw Hat Bounce");
    studio.project.dirty = false;
    studio.engine.setSongPosition(0.0);
    studio.engine.setPlaying(true);
    updateTitle();
}

void MainComponent::showViews(const juce::StringArray& views)
{
    if (!panelsPlaced)
        placePanels();
    for (const auto& view : views)
    {
        if (view == "playlist")
            playlistPanel.present();
        else if (view == "rack")
            rackPanel.present();
        else if (view.startsWith("pianoroll"))
        {
            auto id = studio.project.selectedChannel;
            for (const auto& channel : studio.project.channels)
                if (channel->name == view.fromFirstOccurrenceOf("=", false, false))
                    id = channel->id;
            actions.openPianoRoll(id);
        }
        else if (view.startsWith("pattern="))
            studio.selectPattern(view.fromFirstOccurrenceOf("=", false, false).getIntValue());
        else if (view == "mixer")
            mixerPanel.present();
        else if (view == "nobrowser")
            perform(InvocationInfo(command::showBrowser));
        else if (view == "play")
            studio.engine.setPlaying(true);
        else if (view == "maxroll")
            pianoRollPanel.setMaximised(true);
        else if (view.startsWith("panel="))
            extensions.showPanel(view.fromFirstOccurrenceOf("=", false, false).getIntValue());
    }
}

void MainComponent::chooseOpen()
{
    chooser = std::make_unique<juce::FileChooser>("Open project", studio.project.file, "*.tsp;*.lfs;*.flp");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
            openFile(fc.getResult());
    });
}

void MainComponent::chooseImport()
{
    chooser = std::make_unique<juce::FileChooser>("Import FL Studio project", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.flp");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
            openFile(fc.getResult());
    });
}

void MainComponent::chooseSave(std::function<void(bool)> done)
{
    const auto& project = studio.project;
    const auto suggested = project.file != juce::File() ? project.file.withFileExtension(projectExtension)
                                                        : Studio::userFolder("Projects").getChildFile((project.title.isNotEmpty() ? project.title : juce::String("Untitled")) + projectExtension);
    chooser = std::make_unique<juce::FileChooser>("Save project", suggested, "*.tsp");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, done](const juce::FileChooser& fc)
                         {
                             auto file = fc.getResult();
                             auto saved = false;
                             if (file != juce::File())
                             {
                                 file = file.withFileExtension(projectExtension);
                                 juce::String error;
                                 saved = studio.save(file, error);
                                 if (!saved)
                                     ui::showMessage("Save failed", error);
                                 updateTitle();
                             }
                             if (done)
                                 done(saved);
                         });
}

void MainComponent::chooseExport(const juce::String& extension)
{
    const auto& project = studio.project;
    const auto base = project.file != juce::File() ? project.file.withFileExtension(extension)
                                                   : Studio::userFolder("Rendered").getChildFile((project.title.isNotEmpty() ? project.title : juce::String("TStudio export")) + extension);
    chooser = std::make_unique<juce::FileChooser>("Export audio file", base, extension == ".mp3" ? "*.mp3" : "*.wav;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, extension](const juce::FileChooser& fc)
                         {
                             auto file = fc.getResult();
                             if (file == juce::File())
                                 return;
                             if (!file.hasFileExtension("wav;flac;ogg;mp3"))
                                 file = file.withFileExtension(extension);
                             studio.exportWave(file, [file](bool ok, const juce::String& message)
                             {
                                 ui::showMessage(ok ? "Export finished" : "Export failed", ok ? "Saved " + file.getFullPathName() : message);
                             });
                         });
}

void MainComponent::chooseExportMidi()
{
    auto& project = studio.project;
    const auto base = project.file != juce::File() ? project.file.withFileExtension(".mid")
                                                   : juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(project.pattern().name + ".mid");
    chooser = std::make_unique<juce::FileChooser>("Export MIDI file", base, "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc)
                         {
                             if (fc.getResult() == juce::File())
                                 return;
                             juce::String error;
                             if (!studio.exportMidi(fc.getResult().withFileExtension(".mid"), -1, error))
                                 ui::showMessage("MIDI export failed", error);
                         });
}

void MainComponent::saveNewVersion()
{
    const auto& project = studio.project;
    if (!project.file.hasFileExtension(projectExtension))
    {
        chooseSave({});
        return;
    }
    auto stem = project.file.getFileNameWithoutExtension();
    auto number = 2;
    if (const auto tail = stem.fromLastOccurrenceOf("_", false, false); stem.containsChar('_') && tail.containsOnly("0123456789") && tail.isNotEmpty())
    {
        number = tail.getIntValue() + 1;
        stem = stem.upToLastOccurrenceOf("_", false, false);
    }
    auto target = project.file.getSiblingFile(stem + "_" + juce::String(number) + projectExtension);
    while (target.exists())
        target = project.file.getSiblingFile(stem + "_" + juce::String(++number) + projectExtension);
    juce::String error;
    if (!studio.save(target, error))
        ui::showMessage("Save failed", error);
    updateTitle();
}

void MainComponent::showDialog(std::unique_ptr<juce::Component> content, const juce::String& title)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = title;
    options.dialogBackgroundColour = theme::window;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

juce::PopupMenu MainComponent::scriptsMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("Built-in");
    for (const auto& name : ScriptEngine::builtinScripts())
        menu.addItem(name, [this, name] { runScript(ScriptEngine::builtinScript(name), name); });
    juce::Array<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(ScriptEngine::userFolder(), false, "*.lua"))
        files.add(entry.getFile());
    if (!files.isEmpty())
    {
        files.sort();
        menu.addSectionHeader("My scripts");
        for (const auto& file : files)
            menu.addItem(file.getFileNameWithoutExtension(), [this, file] { runScript(file.loadFileAsString(), file.getFileName()); });
    }
    return menu;
}

void MainComponent::runScript(const juce::String& code, const juce::String& name)
{
    const auto result = scripts.run(code, name);
    if (!result.ok)
        ui::showMessage("Script error: " + name, result.error);
    else if (result.output.isNotEmpty())
        ui::showMessage(name, result.output);
}

void MainComponent::showScriptConsole()
{
    if (scriptWindow == nullptr)
    {
        struct Window final : public juce::DocumentWindow
        {
            Window() : DocumentWindow("TStudio script console", theme::window, juce::DocumentWindow::closeButton) {}
            void closeButtonPressed() override { setVisible(false); }
        };
        scriptWindow = std::make_unique<Window>();
        scriptWindow->setUsingNativeTitleBar(true);
        scriptWindow->setContentOwned(new ScriptConsole(scripts), true);
        scriptWindow->setResizable(true, false);
        scriptWindow->centreWithSize(scriptWindow->getWidth(), scriptWindow->getHeight());
    }
    scriptWindow->setVisible(true);
    ui::attachToOwner(*scriptWindow, *this);
    scriptWindow->toFront(true);
}

void MainComponent::pressKeys(const juce::String& script)
{
    for (const auto& step : juce::StringArray::fromTokens(script, ";", {}))
    {
        const auto target = step.upToFirstOccurrenceOf(":", false, false).trim();
        const auto key = juce::KeyPress::createFromDescription(step.fromFirstOccurrenceOf(":", false, false).trim());
        juce::Component* component = this;
        if (target == "roll")
            component = &pianoRoll;
        else if (target == "rack")
            component = &rack;
        else if (target == "playlist")
            component = &playlist;
        else if (target == "mixer")
            component = &mixer;
        for (auto* c = component; c != nullptr; c = c->getParentComponent())
            if (c->keyPressed(key))
                break;
    }
}

void MainComponent::welcome()
{
    community::maybeAskForStar(*this, studio.plugins.settings());
}

bool MainComponent::startMcp(bool remember)
{
    juce::String error;
    if (!mcp.start(McpServer::defaultPort, error))
    {
        ui::showMessage("Could not start the MCP server", error);
        return false;
    }
    if (remember)
    {
        studio.plugins.settings().setValue("mcpServer", true);
        studio.plugins.settings().saveIfNeeded();
    }
    commands.commandStatusChanged();
    return true;
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(studio.devices, 0, 0, 2, 2, true, false, true, false);
    selector->setSize(560, 480);
    showDialog(std::move(selector), "Audio settings");
}

void MainComponent::showPluginManager()
{
    auto list = std::make_unique<juce::PluginListComponent>(studio.plugins.formats, studio.plugins.known, studio.plugins.deadMansPedal(), &studio.plugins.settings(), true);
    list->setSize(760, 520);
    showDialog(std::move(list), "Manage plugins");
}
}
