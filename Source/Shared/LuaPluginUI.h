#pragma once

#include "Shared/LuaPluginScript.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ts
{
void openGfx(lua_State*);

class GfxScope final
{
public:
    GfxScope(lua_State*, juce::Graphics&, juce::Colour accent);
    ~GfxScope();

    lua_State* const state;
    juce::Graphics& graphics;
    const juce::Colour accent;
};

class LuaPluginHost
{
public:
    virtual ~LuaPluginHost() = default;
    virtual juce::RangedAudioParameter* parameterFor(const juce::String& id) = 0;
    virtual float meterValue(int index) = 0;
    virtual std::unique_ptr<juce::Component> createScope() { return nullptr; }
};

class LuaPluginView final : public juce::Component, private juce::Timer
{
public:
    LuaPluginView(const juce::String& code, const juce::String& origin, const LuaPluginInfo&, LuaPluginHost&);
    ~LuaPluginView() override;

    static juce::Rectangle<int> preferredSize(const LuaPluginInfo&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Widget;

    void timerCallback() override;
    void buildWidgets();
    void refreshTables();
    bool callLua(const char* function, std::initializer_list<double> arguments);
    static int setParam(lua_State*);
    static int getParam(lua_State*);
    static LuaPluginView* from(lua_State*);

    const LuaPluginInfo info;
    LuaPluginHost& host;
    lua_State* state = nullptr;
    juce::Colour background, accent;
    std::vector<std::unique_ptr<Widget>> widgets;
    juce::String error;
    bool hasDraw = false;

};
}
