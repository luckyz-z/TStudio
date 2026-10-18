#include "UI/Browser.h"
#include "UI/Controls.h"
#include "UI/Demos.h"
#include "UI/Theme.h"
#include "Engine/Builtins/Pack.h"
#include "Engine/Builtins/Registry.h"
#include "Engine/Builtins/Sampler.h"

namespace ts
{
class Browser::Item final : public juce::TreeViewItem
{
public:
    enum class Kind
    {
        folder,
        sound,
        plugin,
        info
    };

    Item(juce::String text, Kind k, juce::String drag = {}) : label(std::move(text)), kind(k), dragDescription(std::move(drag)) {}

    Item& add(juce::String text, Kind k, juce::String drag = {})
    {
        auto* item = new Item(std::move(text), k, std::move(drag));
        addSubItem(item);
        return *item;
    }

    bool mightContainSubItems() override { return kind == Kind::folder; }
    juce::String getUniqueName() const override { return label; }
    int getItemHeight() const override { return 21; }
    juce::var getDragSourceDescription() override { return dragDescription; }

    void itemOpennessChanged(bool isNowOpen) override
    {
        if (!isNowOpen || !populate || (getNumSubItems() > 0 && !dynamic))
            return;
        clearSubItems();
        populate(*this);
    }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
            g.fillAll(theme::title);
        auto area = juce::Rectangle<int>(0, 0, width, height);
        const auto icon = area.removeFromLeft(18).toFloat().withSizeKeepingCentre(10.0f, 9.0f);
        g.setColour(kind == Kind::folder ? juce::Colour(0xff958f88) : (kind == Kind::plugin ? theme::led.withAlpha(0.7f) : theme::textDim));
        if (kind == Kind::folder)
        {
            g.fillRoundedRectangle(icon.withWidth(icon.getWidth() * 0.45f).withHeight(3.0f), 1.0f);
            g.fillRoundedRectangle(icon.withTrimmedTop(2.0f), 1.0f);
        }
        else if (kind != Kind::info)
            g.fillEllipse(icon.withSizeKeepingCentre(6.0f, 6.0f));
        g.setColour(kind == Kind::info ? theme::textDim : theme::text.withAlpha(0.78f));
        g.setFont(theme::font(13.0f));
        g.drawText(label, area, juce::Justification::centredLeft, true);
    }

    void itemClicked(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && contextMenu)
            contextMenu();
        else if (click)
            click();
    }

    void itemDoubleClicked(const juce::MouseEvent&) override
    {
        if (doubleClick)
            doubleClick();
        else if (kind == Kind::folder)
            setOpen(!isOpen());
    }

    juce::String label;
    Kind kind;
    juce::String dragDescription;
    bool dynamic = false;
    std::function<void(Item&)> populate;
    std::function<void()> click, doubleClick, contextMenu;
};

namespace
{
void populateFolder(Studio& studio, Browser::Item& item, const juce::File& folder);
}

Browser::Browser(Studio& s) : studio(s)
{
    tree.setRootItemVisible(false);
    tree.setDefaultOpenness(false);
    tree.setIndentSize(14);
    tree.setColour(juce::TreeView::linesColourId, theme::outline);
    addAndMakeVisible(tree);
    addFolder.setTooltip("Add a folder to the browser");
    addFolder.setWantsKeyboardFocus(false);
    addFolder.onClick = [this] { chooseFolder(); };
    addAndMakeVisible(addFolder);
    rebuild();
    studio.project.addChangeListener(this);
}

Browser::~Browser()
{
    studio.project.removeChangeListener(this);
    tree.setRootItem(nullptr);
}

void Browser::paint(juce::Graphics& g)
{
    g.fillAll(theme::panelDark);
    g.setColour(theme::outline);
    g.drawVerticalLine(getWidth() - 1, 0.0f, float(getHeight()));
    g.setColour(theme::textDim);
    g.setFont(theme::font(11.0f, juce::Font::bold));
    g.drawText("BROWSER", getLocalBounds().removeFromTop(26).reduced(10, 0), juce::Justification::centredLeft);
}

void Browser::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(26);
    addFolder.setBounds(header.removeFromRight(30).reduced(4));
    tree.setBounds(area.reduced(2, 0).withTrimmedRight(1));
}

bool Browser::isBrowserDrag(const juce::var& description)
{
    const auto text = description.toString();
    return text.startsWith("pack:") || text.startsWith("file:") || text.startsWith("gen:") || text.startsWith("plugin:") || text.startsWith("fx:");
}

bool Browser::activate(Studio& studio, const juce::String& description)
{
    const auto value = description.fromFirstOccurrenceOf(":", false, false);
    if (description.startsWith("pack:"))
    {
        studio.addPackChannel(value);
        return true;
    }
    if (description.startsWith("file:"))
        return studio.addSampleChannel(juce::File(value)) != nullptr;
    if (description.startsWith("gen:"))
    {
        studio.addChannel(studio.createBuiltin(value), value);
        return true;
    }
    if (description.startsWith("plugin:"))
    {
        const auto type = studio.plugins.known.getTypeForIdentifierString(value);
        if (type == nullptr || !type->isInstrument)
            return false;
        juce::String error;
        auto device = studio.createPlugin(*type, error);
        if (device == nullptr)
        {
            ui::showMessage("Plugin error", "Could not load " + type->name + "\n" + error);
            return false;
        }
        studio.addChannel(std::move(device), type->name);
        return true;
    }
    return false;
}

void Browser::rebuild()
{
    const auto openness = tree.getOpennessState(true);
    tree.setRootItem(nullptr);
    root = std::make_unique<Item>("root", Item::Kind::folder);

    auto& current = root->add("Current project", Item::Kind::folder);
    current.dynamic = true;
    current.populate = [this](Item& item)
    {
        auto& channels = item.add("Channels", Item::Kind::folder);
        channels.dynamic = true;
        channels.populate = [this](Item& list)
        {
            for (const auto& channel : studio.project.channels)
                list.add(channel->name, Item::Kind::plugin).doubleClick = [this, id = channel->id] { studio.selectChannel(id); };
        };
        auto& patterns = item.add("Patterns", Item::Kind::folder);
        patterns.dynamic = true;
        patterns.populate = [this](Item& list)
        {
            for (int i = 0; i < int(studio.project.patterns.size()); ++i)
            {
                list.add(studio.project.patterns[size_t(i)].name, Item::Kind::info).click = [this, i]
                {
                    studio.project.currentPattern.store(i);
                    studio.project.sendChangeMessage();
                };
            }
        };
    };

    auto& recent = root->add("Recent files", Item::Kind::folder);
    recent.dynamic = true;
    recent.populate = [this](Item& list)
    {
        const auto files = studio.recentFiles();
        for (const auto& file : files)
            list.add(file.getFileNameWithoutExtension(), Item::Kind::plugin).doubleClick = [file] { if (Browser::openProject) Browser::openProject(file); };
        if (files.isEmpty())
            list.add("(nothing yet)", Item::Kind::info);
    };

    auto& demoFolder = root->add("Demo projects", Item::Kind::folder);
    for (const auto& name : demos::names())
    {
        auto& item = demoFolder.add(name, Item::Kind::info);
        item.doubleClick = [this, name]
        {
            if (onOpenDemo)
                onOpenDemo(name);
        };
    }

    auto& packs = root->add("Packs", Item::Kind::folder);
    auto& drums = packs.add(Pack::name, Item::Kind::folder);
    for (const auto& sound : Pack::names())
    {
        auto& item = drums.add(sound, Item::Kind::sound, "pack:" + sound);
        item.click = [this, sound] { studio.engine.preview(Pack::get(sound)); };
        item.doubleClick = [this, sound] { studio.addPackChannel(sound); };
    }

    auto& database = root->add("Plugin database", Item::Kind::folder);
    auto& generators = database.add("Generators", Item::Kind::folder);
    generators.dynamic = true;
    generators.populate = [this](Item& list)
    {
        for (const auto& name : Builtins::generators())
            list.add(name, Item::Kind::plugin, "gen:" + name).doubleClick = [this, name] { activate(studio, "gen:" + name); };
        for (const auto& type : studio.plugins.list(true))
        {
            const auto id = "plugin:" + type.createIdentifierString();
            list.add(type.name, Item::Kind::plugin, id).doubleClick = [this, id] { activate(studio, id); };
        }
    };
    auto& effects = database.add("Effects", Item::Kind::folder);
    effects.dynamic = true;
    effects.populate = [this](Item& list)
    {
        for (const auto& name : Builtins::effects())
            list.add(name, Item::Kind::plugin, "fx:" + name);
        for (const auto& type : studio.plugins.list(false))
            list.add(type.name, Item::Kind::plugin, "plugin:" + type.createIdentifierString());
    };

    for (const auto& [title, name] : std::initializer_list<std::pair<const char*, const char*>> {
             { "My projects", "Projects" }, { "Rendered", "Rendered" }, { "Scripts", "Scripts" }, { "Lua plugins", "Plugins" }, { "Extensions", "Extensions" },
             { "Native plugins", "NativePlugins" } })
    {
        auto& item = root->add(title, Item::Kind::folder);
        item.dynamic = true;
        item.populate = [this, folder = Studio::userFolder(name)](Item& target) { populateFolder(studio, target, folder); };
    }

    for (const auto& path : userFolders())
    {
        const juce::File folder(path);
        auto& item = root->add(folder.getFileName(), Item::Kind::folder);
        item.populate = [this, folder](Item& target) { populateFolder(studio, target, folder); };
        item.contextMenu = [this, path]
        {
            juce::PopupMenu menu;
            menu.addItem("Remove from browser", [this, path]
            {
                auto folders = userFolders();
                folders.removeString(path);
                setUserFolders(folders);
                juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Browser>(this)] { if (safe != nullptr) safe->rebuild(); });
            });
            menu.showMenuAsync({});
        };
    }

    tree.setRootItem(root.get());
    if (openness != nullptr)
        tree.restoreOpennessState(*openness, true);
    signature = projectSignature();
}

void Browser::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (const auto next = projectSignature(); next != signature)
        rebuild();
}

juce::String Browser::projectSignature() const
{
    juce::String result;
    for (const auto& channel : studio.project.channels)
        result << channel->name << "|";
    for (const auto& pattern : studio.project.patterns)
        result << pattern.name << "|";
    return result;
}

void Browser::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Add folder to browser", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (!folder.isDirectory())
            return;
        auto folders = userFolders();
        folders.addIfNotAlreadyThere(folder.getFullPathName());
        setUserFolders(folders);
        rebuild();
    });
}

juce::StringArray Browser::userFolders() const
{
    return juce::StringArray::fromLines(studio.plugins.settings().getValue("browserFolders"));
}

void Browser::setUserFolders(const juce::StringArray& folders)
{
    auto cleaned = folders;
    cleaned.removeEmptyStrings();
    studio.plugins.settings().setValue("browserFolders", cleaned.joinIntoString("\n"));
    studio.plugins.settings().saveIfNeeded();
}

namespace
{
void populateFolder(Studio& studio, Browser::Item& item, const juce::File& folder)
{
    juce::Array<juce::File> folders, files;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
    {
        const auto file = entry.getFile();
        if (entry.isDirectory())
            folders.add(file);
        else if (Sampler::isAudioFile(file) || file.hasFileExtension("mid;midi;tsp;lfs;flp;lua"))
            files.add(file);
    }
    folders.sort();
    files.sort();

    for (const auto& sub : folders)
        item.add(sub.getFileName(), Browser::Item::Kind::folder).populate = [&studio, sub](Browser::Item& target) { populateFolder(studio, target, sub); };

    for (const auto& file : files)
    {
        if (file.hasFileExtension("tsp;lfs;flp"))
        {
            item.add(file.getFileName(), Browser::Item::Kind::plugin).doubleClick = [file] { if (Browser::openProject) Browser::openProject(file); };
            continue;
        }
        if (file.hasFileExtension("lua"))
        {
            item.add(file.getFileName(), Browser::Item::Kind::info).doubleClick = [file] { if (Browser::runScript) Browser::runScript(file); };
            continue;
        }
        if (file.hasFileExtension("mid;midi"))
        {
            auto& midi = item.add(file.getFileName(), Browser::Item::Kind::info, "midi:" + file.getFullPathName());
            midi.doubleClick = [&studio, file]
            {
                juce::String error;
                if (!studio.importMidi(file, studio.project.selectedChannel, error))
                    ui::showMessage("MIDI import", error);
            };
            continue;
        }
        auto& sound = item.add(file.getFileName(), Browser::Item::Kind::sound, "file:" + file.getFullPathName());
        sound.click = [&studio, file] { studio.engine.preview(Sampler::readFile(file)); };
        sound.doubleClick = [&studio, file] { studio.addSampleChannel(file); };
    }
    if (folders.isEmpty() && files.isEmpty())
        item.add("(empty)", Browser::Item::Kind::info);
}
}
}
