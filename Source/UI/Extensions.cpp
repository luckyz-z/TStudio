#include "UI/Extensions.h"
#include "Shared/LuaPluginUI.h"
#include "UI/Theme.h"

namespace ts
{
class Extensions::View final : public juce::Component, private juce::Timer
{
public:
    View(Extensions& e, int i, const juce::var& spec)
        : owner(e), index(i),
          background(juce::Colour::fromString("ff" + spec.getProperty("background", "#22282b").toString().fromFirstOccurrenceOf("#", false, false))),
          accent(juce::Colour::fromString("ff" + spec.getProperty("accent", "#e8483b").toString().fromFirstOccurrenceOf("#", false, false)))
    {
        setSize(int(spec.getProperty("width", 420)), int(spec.getProperty("height", 280)));
        startTimerHz(juce::jlimit(1, 60, int(spec.getProperty("fps", 30))));
        setWantsKeyboardFocus(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(background);
        if (failure.isNotEmpty())
        {
            g.setColour(theme::accent);
            g.setFont(theme::font(13.0f, juce::Font::bold));
            g.drawFittedText("Extension error: " + failure, getLocalBounds().reduced(12), juce::Justification::topLeft, 8);
            return;
        }
        const GfxScope scope(owner.scripts.luaState(), g, accent);
        call("draw", { getWidth(), getHeight() });
    }

    void mouseDown(const juce::MouseEvent& e) override { call("mouse_down", { e.position.x, e.position.y }); }
    void mouseDrag(const juce::MouseEvent& e) override { call("mouse_drag", { e.position.x, e.position.y }); }
    void mouseUp(const juce::MouseEvent& e) override { call("mouse_up", { e.position.x, e.position.y }); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override { call("mouse_wheel", { e.position.x, e.position.y, wheel.deltaY }); }

private:
    void timerCallback() override
    {
        if (isShowing())
            repaint();
    }

    void call(const char* name, std::initializer_list<juce::var> arguments)
    {
        if (failure.isNotEmpty())
            return;
        juce::Array<juce::var> list { index + 1, juce::String(name) };
        list.addArray(arguments);
        const auto result = owner.scripts.invoke("_panel_call", list);
        if (!result.ok)
        {
            failure = result.error;
            repaint();
        }
        if (juce::String(name) != "draw")
            repaint();
    }

    Extensions& owner;
    int index;
    juce::Colour background, accent;
    juce::String failure;
};

Extensions::Extensions(ScriptEngine& s, Studio& st, juce::Component& w) : scripts(s), studio(st), workspace(w)
{
    scripts.onShowPanel = [this](int index) { showPanel(index); };
}

Extensions::~Extensions()
{
    scripts.onShowPanel = nullptr;
    poller.stopTimer();
    panels.clear();
    views.clear();
}

ScriptEngine::Result Extensions::reload()
{
    panels.clear();
    views.clear();
    panelTitles.clear();
    const auto result = scripts.loadExtensions();
    report("extensions", result);

    juce::var names, info;
    scripts.invoke("_menu_names", {}, &names);
    menuNames.clear();
    if (const auto* list = names.getArray())
        for (const auto& name : *list)
            menuNames.add(name.toString());

    scripts.invoke("_panel_info", {}, &info);
    if (const auto* list = info.getArray())
        for (int i = 0; i < list->size(); ++i)
        {
            const auto& spec = list->getReference(i);
            views.push_back(std::make_unique<View>(*this, i, spec));
            panelTitles.add(spec.getProperty("title", "Panel").toString());
            auto panel = std::make_unique<Panel>(panelTitles[i], *views.back());
            panel->setBounds(80 + 24 * i, 70 + 24 * i, views.back()->getWidth(), views.back()->getHeight() + Panel::titleHeight);
            workspace.addChildComponent(*panel);
            panels.push_back(std::move(panel));
        }

    playing = studio.engine.isPlaying();
    pattern = studio.project.currentPattern.load();
    channel = studio.project.selectedChannel;
    tempo = studio.project.tempo.load();
    dirty = studio.project.dirty;
    poller.startTimerHz(10);
    return result;
}

void Extensions::addToMenu(juce::PopupMenu& menu)
{
    juce::PopupMenu sub;
    for (int i = 0; i < menuNames.size(); ++i)
        sub.addItem(menuNames[i], [this, i]
        {
            report(menuNames[i], scripts.invoke("_menu", { i + 1 }));
            studio.project.sendChangeMessage();
        });
    if (!panels.empty())
    {
        sub.addSeparator();
        for (size_t i = 0; i < panels.size(); ++i)
            sub.addItem("Show " + panelTitles[int(i)], [this, i] { showPanel(int(i) + 1); });
    }
    if (sub.getNumItems() > 0)
        sub.addSeparator();
    sub.addItem("Reload extensions", [this] { reload(); });
    sub.addItem("Open extensions folder", [] { ScriptEngine::extensionsFolder().startAsProcess(); });
    sub.addItem("Show extension log", [this] { if (onMessage) onMessage("Extension log", messages.isNotEmpty() ? messages : juce::String("Nothing logged yet.")); });
    menu.addSubMenu("Extensions", sub);
}

bool Extensions::keyPressed(const juce::KeyPress& key)
{
    juce::var handled;
    const auto result = scripts.invoke("_key", { key.getTextDescription() }, &handled);
    report("shortcut " + key.getTextDescription(), result);
    if (bool(handled))
        studio.project.sendChangeMessage();
    return bool(handled);
}

void Extensions::emit(const juce::String& event, const juce::var& data)
{
    report("event " + event, scripts.invoke("_emit", { event, data }));
}

void Extensions::loaded(const juce::String& source)
{
    dirty = studio.project.dirty;
    emit("load", source);
}

void Extensions::showPanel(int index)
{
    if (!juce::isPositiveAndNotGreaterThan(index, int(panels.size())) || index == 0)
        return;
    panels[size_t(index - 1)]->present();
}

void Extensions::poll()
{
    if (const auto now = studio.engine.isPlaying(); now != playing)
    {
        playing = now;
        emit(now ? "play" : "stop");
    }
    if (const auto now = studio.project.currentPattern.load(); now != pattern)
    {
        pattern = now;
        emit("pattern", now);
    }
    if (const auto now = studio.project.selectedChannel; now != channel)
    {
        channel = now;
        emit("channel", now);
    }
    if (const auto now = studio.project.tempo.load(); now != tempo)
    {
        tempo = now;
        emit("tempo", now);
    }
    if (const auto now = studio.project.dirty; now != dirty)
    {
        dirty = now;
        if (!now && studio.project.file.existsAsFile())
            emit("save", studio.project.file.getFullPathName());
    }
}

void Extensions::report(const juce::String& source, const ScriptEngine::Result& result)
{
    if (result.output.isNotEmpty())
        messages << result.output;
    if (result.ok)
        return;
    messages << "[" << source << "] " << result.error << juce::newLine;
    if (onMessage)
        onMessage("Extension error", source + ": " + result.error);
}
}
