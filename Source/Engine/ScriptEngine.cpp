#include "Engine/ScriptEngine.h"
#include "Shared/LuaPluginUI.h"
#include "Shared/LuaVar.h"

#include <BinaryData.h>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace ts
{
namespace
{
constexpr juce::uint32 timeLimitMs = 10000;
constexpr const char* instanceKey = "tstudio.engine";

constexpr const char* prelude = R"lua(
ts.PPQ, ts.BEAT, ts.STEP, ts.BAR = 96, 96, 24, 384
local raw = ts._call
local function with(target, extra) local t = {} for k, v in pairs(target or {}) do t[k] = v end for k, v in pairs(extra) do t[k] = v end return t end
function ts.call(name, args)
  local result = raw(name, args or {})
  if type(result) == "table" and result.error then error(result.error, 2) end
  return result
end
function ts.project() return ts.call("get_project") end
function ts.tempo(bpm) if bpm then ts.call("set_tempo", { bpm = bpm }) end return ts.project().tempo end
function ts.swing(amount) ts.call("set_swing", { amount = amount }) end
function ts.play() ts.call("transport", { action = "play" }) end
function ts.stop() ts.call("transport", { action = "stop" }) end
function ts.song_mode(enabled) ts.call("set_song_mode", { enabled = enabled }) end
function ts.channels() return ts.project().channels end
function ts.selected_channel() return ts.project().selected_channel end
function ts.current_pattern() return ts.project().current_pattern end
function ts.add_channel(kind, name) return ts.call("add_channel", { type = kind, name = name }).id end
function ts.remove_channel(id) ts.call("remove_channel", { id = id }) end
function ts.update_channel(id, props) ts.call("update_channel", with(props, { id = id })) end
function ts.notes(channel, pattern) return ts.call("get_notes", { channel = channel, pattern = pattern }) end
function ts.set_notes(channel, notes, pattern) ts.call("set_notes", { channel = channel, notes = notes, pattern = pattern }) end
function ts.add_notes(channel, notes, pattern) ts.call("add_notes", { channel = channel, notes = notes, pattern = pattern }) end
function ts.add_note(channel, start, length, key, velocity, pattern)
  ts.add_notes(channel, { { start = start, length = length, key = key, velocity = velocity or 0.78 } }, pattern)
end
function ts.clear(channel, pattern) ts.call("clear_notes", { channel = channel, pattern = pattern }) end
function ts.new_pattern(name) return ts.call("new_pattern", { name = name }).index end
function ts.select_pattern(index) ts.call("select_pattern", { index = index }) end
function ts.add_clip(pattern, track, start_bar, length_bars)
  ts.call("add_clip", { pattern = pattern, track = track, start_bar = start_bar, length_bars = length_bars })
end
function ts.set_param(target, id, value) ts.call("set_parameter", with(target, { id = id, value = value })) end
function ts.presets(target) return ts.call("list_presets", target) end
function ts.set_preset(target, preset) ts.call("set_preset", with(target, type(preset) == "number" and { index = preset } or { name = preset })) end
function ts.add_effect(track, slot, name) ts.call("add_effect", { track = track, slot = slot, name = name }) end
function ts.mixer(track, props) ts.call("set_mixer_track", with(props, { track = track })) end
function ts.automate(target, points, options) return ts.call("create_automation", with(with(target, options or {}), { points = points })).index end
function ts.route(from, to, options) ts.call("route_mixer_track", with(options, { from = from, to = to })) end
function ts.render(path) ts.call("render", { path = path }) end
function ts.save(path) ts.call("save_project", { path = path }) end

local letters = { C = 0, D = 2, E = 4, F = 5, G = 7, A = 9, B = 11 }
function ts.note(name)
  if type(name) == "number" then return name end
  local letter, accidental, octave = name:match("^(%a)([#b]?)(%-?%d+)$")
  assert(letter, "bad note name " .. tostring(name))
  local value = letters[letter:upper()] + (accidental == "#" and 1 or accidental == "b" and -1 or 0)
  return value + tonumber(octave) * 12
end

ts.scales = {
  major = { 0, 2, 4, 5, 7, 9, 11 }, minor = { 0, 2, 3, 5, 7, 8, 10 }, dorian = { 0, 2, 3, 5, 7, 9, 10 },
  phrygian = { 0, 1, 3, 5, 7, 8, 10 }, harmonic_minor = { 0, 2, 3, 5, 7, 8, 11 }, pentatonic = { 0, 2, 4, 7, 9 },
  minor_pentatonic = { 0, 3, 5, 7, 10 }, blues = { 0, 3, 5, 6, 7, 10 },
}
ts.chords = {
  major = { 0, 4, 7 }, minor = { 0, 3, 7 }, maj7 = { 0, 4, 7, 11 }, min7 = { 0, 3, 7, 10 }, dom7 = { 0, 4, 7, 10 },
  sus2 = { 0, 2, 7 }, sus4 = { 0, 5, 7 }, dim = { 0, 3, 6 }, min9 = { 0, 3, 7, 10, 14 }, add9 = { 0, 4, 7, 14 },
}
function ts.chord(root, kind)
  local keys = {}
  for i, interval in ipairs(ts.chords[kind or "major"]) do keys[i] = ts.note(root) + interval end
  return keys
end
function ts.scale_degree(root, scale, degree)
  local steps = ts.scales[scale or "minor"]
  local index = (degree - 1) % #steps + 1
  local octave = (degree - 1) // #steps
  return ts.note(root) + steps[index] + octave * 12
end

local function keyname(key) return (string.lower(key):gsub("%s", "")) end
function ts._reset() ts._menus, ts._events, ts._keys, ts._panels = {}, {}, {}, {} end
ts._reset()
function ts.menu(name, fn) ts._menus[#ts._menus + 1] = { name = name, fn = fn } end
function ts.on(event, fn) ts._events[event] = ts._events[event] or {}; table.insert(ts._events[event], fn) end
function ts.shortcut(key, fn) ts._keys[keyname(key)] = fn end
function ts.panel(spec) ts._panels[#ts._panels + 1] = spec; return #ts._panels end
function ts.show_panel(index) ts._show_panel(index) end
function ts._emit(event, data) for _, fn in ipairs(ts._events[event] or {}) do fn(data) end end
function ts._menu(index) local item = ts._menus[index]; if item then item.fn() end end
function ts._key(name) local fn = ts._keys[keyname(name)]; if fn then fn(); return true end return false end
function ts._menu_names() local names = {} for i, item in ipairs(ts._menus) do names[i] = item.name end return names end
function ts._panel_info()
  local list = {}
  for i, p in ipairs(ts._panels) do list[i] = { title = p.title or ("Panel " .. i), width = p.width or 420, height = p.height or 280, fps = p.fps or 30, background = p.background or "#22282b", accent = p.accent or "#e8483b" } end
  return list
end
function ts._panel_call(index, name, ...) local p = ts._panels[index]; if p and p[name] then p[name](...) end end
)lua";

ScriptEngine* engineFrom(lua_State* state)
{
    lua_getfield(state, LUA_REGISTRYINDEX, instanceKey);
    auto* engine = static_cast<ScriptEngine*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return engine;
}

}

ScriptEngine::ScriptEngine(Automation& a) : automation(a)
{
    state = luaL_newstate();
    for (const auto& [name, opener] : std::initializer_list<std::pair<const char*, lua_CFunction>> {
             { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table }, { LUA_STRLIBNAME, luaopen_string },
             { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 }, { LUA_COLIBNAME, luaopen_coroutine } })
    {
        luaL_requiref(state, name, opener, 1);
        lua_pop(state, 1);
    }
    for (const auto* unsafe : { "dofile", "loadfile", "load" })
    {
        lua_pushnil(state);
        lua_setglobal(state, unsafe);
    }
    lua_pushlightuserdata(state, this);
    lua_setfield(state, LUA_REGISTRYINDEX, instanceKey);
    lua_pushcfunction(state, print);
    lua_setglobal(state, "print");
    lua_newtable(state);
    lua_pushcfunction(state, call);
    lua_setfield(state, -2, "_call");
    lua_pushcfunction(state, showPanel);
    lua_setfield(state, -2, "_show_panel");
    lua_setglobal(state, "ts");
    openGfx(state);
    luaL_dostring(state, prelude);
    lua_sethook(state, hook, LUA_MASKCOUNT, 100000);
}

ScriptEngine::~ScriptEngine()
{
    lua_close(state);
}

int ScriptEngine::call(lua_State* state)
{
    auto* engine = engineFrom(state);
    const auto method = juce::String::fromUTF8(luaL_checkstring(state, 1));
    const auto arguments = lua_gettop(state) >= 2 ? toVar(state, 2) : juce::var();
    pushVar(state, engine->automation.call(method, arguments));
    return 1;
}

int ScriptEngine::showPanel(lua_State* state)
{
    auto* engine = engineFrom(state);
    const auto index = int(luaL_checkinteger(state, 1));
    if (engine->onShowPanel)
        juce::MessageManager::callAsync([engine, index] { if (engine->onShowPanel) engine->onShowPanel(index); });
    return 0;
}

int ScriptEngine::print(lua_State* state)
{
    auto* engine = engineFrom(state);
    const auto count = lua_gettop(state);
    for (int i = 1; i <= count; ++i)
    {
        if (i > 1)
            engine->output << "\t";
        engine->output << juce::String::fromUTF8(luaL_tolstring(state, i, nullptr));
        lua_pop(state, 1);
    }
    engine->output << juce::newLine;
    return 0;
}

void ScriptEngine::hook(lua_State* state, ::lua_Debug*)
{
    if (juce::Time::getMillisecondCounter() > engineFrom(state)->deadline)
        luaL_error(state, "script stopped: it ran longer than %d seconds", int(timeLimitMs / 1000));
}

ScriptEngine::Result ScriptEngine::run(const juce::String& code, const juce::String& chunkName)
{
    output.clear();
    deadline = juce::Time::getMillisecondCounter() + timeLimitMs;
    Result result;
    const auto utf8 = code.toStdString();
    if (luaL_loadbuffer(state, utf8.data(), utf8.size(), ("=" + chunkName).toRawUTF8()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
    {
        result.ok = false;
        result.error = juce::String::fromUTF8(lua_tostring(state, -1));
        lua_pop(state, 1);
    }
    result.output = output;
    return result;
}

ScriptEngine::Result ScriptEngine::invoke(const char* function, const juce::Array<juce::var>& arguments, juce::var* returned)
{
    output.clear();
    deadline = juce::Time::getMillisecondCounter() + timeLimitMs;
    Result result;
    lua_getglobal(state, "ts");
    lua_getfield(state, -1, function);
    lua_remove(state, -2);
    for (const auto& argument : arguments)
        pushVar(state, argument);
    if (lua_pcall(state, arguments.size(), 1, 0) != LUA_OK)
    {
        result.ok = false;
        result.error = juce::String::fromUTF8(lua_tostring(state, -1));
    }
    else if (returned != nullptr)
        *returned = toVar(state, -1);
    lua_pop(state, 1);
    result.output = output;
    return result;
}

ScriptEngine::Result ScriptEngine::loadExtensions()
{
    Result report;
    invoke("_reset");
    const auto load = [this, &report](const juce::String& code, const juce::String& name)
    {
        const auto result = run(code, name);
        report.output << result.output;
        if (!result.ok)
        {
            report.ok = false;
            report.error << result.error << juce::newLine;
        }
    };
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String file(BinaryData::originalFilenames[i]);
        if (!file.startsWith("ext_") || !file.endsWith(".lua"))
            continue;
        int size = 0;
        if (const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
            load(juce::String::fromUTF8(data, size), file);
    }
    juce::Array<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(extensionsFolder(), false, "*.lua"))
        files.add(entry.getFile());
    files.sort();
    for (const auto& file : files)
        load(file.loadFileAsString(), file.getFileName());
    return report;
}

juce::File ScriptEngine::extensionsFolder()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TStudio").getChildFile("Extensions");
    folder.createDirectory();
    return folder;
}

ScriptEngine::Result ScriptEngine::runFile(const juce::File& file)
{
    return run(file.loadFileAsString(), file.getFileName());
}

juce::File ScriptEngine::userFolder()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TStudio").getChildFile("Scripts");
    folder.createDirectory();
    return folder;
}

juce::StringArray ScriptEngine::builtinScripts()
{
    juce::StringArray result;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (const juce::String file(BinaryData::originalFilenames[i]); file.endsWith(".lua") && !file.startsWith("plugin_") && !file.startsWith("ext_"))
            result.add(file.upToLastOccurrenceOf(".lua", false, false));
    result.sort(true);
    return result;
}

juce::String ScriptEngine::builtinScript(const juce::String& name)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (juce::String(BinaryData::originalFilenames[i]) != name + ".lua")
            continue;
        int size = 0;
        if (const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
            return juce::String::fromUTF8(data, size);
    }
    return {};
}
}
