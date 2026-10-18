#pragma once

#include "Engine/Automation.h"

struct lua_State;
struct lua_Debug;

namespace ts
{
class ScriptEngine final
{
public:
    explicit ScriptEngine(Automation&);
    ~ScriptEngine();

    struct Result
    {
        bool ok = true;
        juce::String output;
        juce::String error;
    };

    Result run(const juce::String& code, const juce::String& chunkName = "script");
    Result runFile(const juce::File&);
    Result invoke(const char* function, const juce::Array<juce::var>& arguments = {}, juce::var* returned = nullptr);
    Result loadExtensions();
    lua_State* luaState() const noexcept { return state; }

    std::function<void(int)> onShowPanel;

    static juce::File userFolder();
    static juce::File extensionsFolder();
    static juce::StringArray builtinScripts();
    static juce::String builtinScript(const juce::String& name);

private:
    static int call(lua_State*);
    static int print(lua_State*);
    static int showPanel(lua_State*);
    static void hook(lua_State*, ::lua_Debug*);

    Automation& automation;
    lua_State* state = nullptr;
    juce::String output;
    juce::uint32 deadline = 0;
};

}
