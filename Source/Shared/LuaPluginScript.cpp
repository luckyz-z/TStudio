#include "Shared/LuaPluginScript.h"
#include "Shared/LuaVar.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cmath>
#include <cstring>

namespace ts
{
namespace
{
constexpr const char* helpers = R"lua(
TWO_PI = math.pi * 2
function note_freq(key) return 440 * 2 ^ ((key - 69) / 12) end
function clamp(x, lo, hi) if x < lo then return lo elseif x > hi then return hi end return x end
function db_to_gain(db) return 10 ^ (db / 20) end
function gain_to_db(g) return 20 * math.log(math.max(g, 1e-9), 10) end
function lerp(a, b, t) return a + (b - a) * t end
function soft_clip(x) if x > 1 then return 2 / 3 elseif x < -1 then return -2 / 3 end return x - x * x * x / 3 end
)lua";

juce::String field(lua_State* state, int table, const char* key, const juce::String& fallback = {})
{
    lua_getfield(state, table, key);
    const auto value = lua_isstring(state, -1) ? juce::String::fromUTF8(lua_tostring(state, -1)) : fallback;
    lua_pop(state, 1);
    return value;
}

float number(lua_State* state, int table, const char* key, float fallback)
{
    lua_getfield(state, table, key);
    const auto value = lua_isnumber(state, -1) ? float(lua_tonumber(state, -1)) : fallback;
    lua_pop(state, 1);
    return value;
}
}

static void watchdog(lua_State* state, lua_Debug*)
{
    juce::uint32 deadline = 0;
    std::memcpy(&deadline, lua_getextraspace(state), sizeof(deadline));
    if (juce::Time::getMillisecondCounter() > deadline)
        luaL_error(state, "script took too long (endless loop?)");
}

void armLuaWatchdog(lua_State* state, int milliseconds)
{
    const auto deadline = juce::Time::getMillisecondCounter() + juce::uint32(milliseconds);
    std::memcpy(lua_getextraspace(state), &deadline, sizeof(deadline));
}

void openPluginLibraries(lua_State* state)
{
    armLuaWatchdog(state, 5000);
    lua_sethook(state, watchdog, LUA_MASKCOUNT, 200000);
    for (const auto& [name, opener] : std::initializer_list<std::pair<const char*, lua_CFunction>> {
             { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table }, { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math } })
    {
        luaL_requiref(state, name, opener, 1);
        lua_pop(state, 1);
    }
    for (const auto* unsafe : { "dofile", "loadfile", "load" })
    {
        lua_pushnil(state);
        lua_setglobal(state, unsafe);
    }
    luaL_dostring(state, helpers);
}

namespace
{
bool readInfo(lua_State* state, LuaPluginInfo& info, juce::String& error)
{
    lua_getglobal(state, "plugin");
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        error = "the script must define a global 'plugin' table";
        return false;
    }
    const auto table = lua_gettop(state);
    info.name = field(state, table, "name", "Lua plugin");
    info.author = field(state, table, "author");
    info.description = field(state, table, "description");
    info.instrument = field(state, table, "type", "effect") == "instrument";

    lua_getfield(state, table, "params");
    if (lua_istable(state, -1))
    {
        const auto list = lua_gettop(state);
        for (lua_Integer i = 1; i <= lua_Integer(lua_rawlen(state, list)); ++i)
        {
            lua_rawgeti(state, list, i);
            if (lua_istable(state, -1))
            {
                const auto entry = lua_gettop(state);
                LuaPluginInfo::Parameter parameter;
                parameter.id = field(state, entry, "id", "p" + juce::String(i));
                parameter.name = field(state, entry, "name", parameter.id);
                parameter.unit = field(state, entry, "unit");
                lua_getfield(state, entry, "choices");
                if (lua_istable(state, -1))
                {
                    for (lua_Integer c = 1; c <= lua_Integer(lua_rawlen(state, -1)); ++c)
                    {
                        lua_rawgeti(state, -1, c);
                        parameter.choices.add(juce::String::fromUTF8(luaL_optstring(state, -1, "?")));
                        lua_pop(state, 1);
                    }
                }
                lua_pop(state, 1);
                parameter.min = number(state, entry, "min", 0.0f);
                parameter.max = number(state, entry, "max", parameter.choices.isEmpty() ? 1.0f : float(parameter.choices.size() - 1));
                parameter.value = juce::jlimit(parameter.min, juce::jmax(parameter.min, parameter.max), number(state, entry, "default", parameter.min));
                info.parameters.push_back(parameter);
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, table, "ui");
    if (lua_istable(state, -1))
        info.ui = toVar(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, table, "meters");
    if (lua_istable(state, -1))
        for (lua_Integer i = 1; i <= lua_Integer(lua_rawlen(state, -1)); ++i)
        {
            lua_rawgeti(state, -1, i);
            if (lua_isstring(state, -1))
                info.meters.add(juce::String::fromUTF8(lua_tostring(state, -1)));
            lua_pop(state, 1);
        }
    lua_pop(state, 1);

    lua_getfield(state, table, "presets");
    if (lua_istable(state, -1))
    {
        const auto list = lua_gettop(state);
        for (lua_Integer i = 1; i <= lua_Integer(lua_rawlen(state, list)); ++i)
        {
            lua_rawgeti(state, list, i);
            if (lua_istable(state, -1))
            {
                const auto entry = lua_gettop(state);
                LuaPluginInfo::Preset preset { field(state, entry, "name", "Preset " + juce::String(i)), {} };
                for (const auto& parameter : info.parameters)
                {
                    lua_getfield(state, entry, parameter.id.toRawUTF8());
                    if (lua_isnumber(state, -1))
                        preset.values.emplace_back(parameter.id, float(lua_tonumber(state, -1)));
                    lua_pop(state, 1);
                }
                info.presets.push_back(preset);
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 2);
    return true;
}
}

LuaPluginScript::LuaPluginScript()
{
    state = luaL_newstate();
    lua_gc(state, LUA_GCGEN, 0, 0);
    openPluginLibraries(state);
}

LuaPluginScript::~LuaPluginScript()
{
    lua_close(state);
}

int LuaPluginScript::reference(const char* global)
{
    lua_getglobal(state, global);
    if (!lua_isfunction(state, -1))
    {
        lua_pop(state, 1);
        return -1;
    }
    return luaL_ref(state, LUA_REGISTRYINDEX);
}

bool LuaPluginScript::load(const juce::String& code, const juce::String& chunkName, LuaPluginInfo& info, juce::String& error)
{
    const auto utf8 = code.toStdString();
    armLuaWatchdog(state, 3000);
    if (luaL_loadbuffer(state, utf8.data(), utf8.size(), ("=" + chunkName).toRawUTF8()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
    {
        error = juce::String::fromUTF8(lua_tostring(state, -1));
        lua_pop(state, 1);
        return false;
    }
    if (!readInfo(state, info, error))
        return false;

    instrument = info.instrument;
    processRef = reference("process");
    renderRef = reference("render");
    noteOnRef = reference("note_on");
    noteOffRef = reference("note_off");
    prepareRef = reference("prepare");
    blockRef = reference("block");
    if ((instrument && renderRef < 0) || (!instrument && processRef < 0))
    {
        error = instrument ? "instrument plugins must define render()" : "effect plugins must define process(left, right)";
        return false;
    }

    lua_newtable(state);
    for (const auto& parameter : info.parameters)
    {
        ids.push_back(parameter.id);
        lua_pushnumber(state, parameter.value);
        lua_setfield(state, -2, parameter.id.toRawUTF8());
    }
    lua_setglobal(state, "params");
    lua_newtable(state);
    lua_setglobal(state, "meters");
    for (const auto& name : info.meters)
        if (int(meterNames.size()) < maxMeters)
            meterNames.push_back(name);
    return true;
}

bool LuaPluginScript::inspect(const juce::String& code, const juce::String& chunkName, LuaPluginInfo& info, juce::String& error)
{
    LuaPluginScript script;
    return script.load(code, chunkName, info, error);
}

void LuaPluginScript::prepare(double sampleRate, int blockSize)
{
    lua_pushnumber(state, sampleRate);
    lua_setglobal(state, "sample_rate");
    armLuaWatchdog(state, 3000);
    if (prepareRef < 0 || failure.isNotEmpty())
        return;
    lua_rawgeti(state, LUA_REGISTRYINDEX, prepareRef);
    lua_pushnumber(state, sampleRate);
    lua_pushinteger(state, blockSize);
    if (lua_pcall(state, 2, 0, 0) != LUA_OK)
    {
        failure = juce::String::fromUTF8(lua_tostring(state, -1));
        lua_pop(state, 1);
    }
}

void LuaPluginScript::setParameter(size_t index, float value)
{
    if (index >= ids.size())
        return;
    lua_getglobal(state, "params");
    lua_pushnumber(state, value);
    lua_setfield(state, -2, ids[index].toRawUTF8());
    lua_pop(state, 1);
}

int LuaPluginScript::runBlock(lua_State* state)
{
    auto* self = static_cast<LuaPluginScript*>(lua_touserdata(state, lua_upvalueindex(1)));
    if (self->blockRef >= 0)
    {
        lua_rawgeti(state, LUA_REGISTRYINDEX, self->blockRef);
        lua_pushinteger(state, self->blockSamples);
        lua_call(state, 1, 0);
    }

    auto event = self->blockMidi->cbegin();
    const auto end = self->blockMidi->cend();
    for (int i = 0; i < self->blockSamples; ++i)
    {
        for (; event != end && (*event).samplePosition <= i; ++event)
        {
            const auto message = (*event).getMessage();
            const auto on = message.isNoteOn();
            const auto ref = on ? self->noteOnRef : self->noteOffRef;
            if (!(on || message.isNoteOff()) || ref < 0)
                continue;
            lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
            lua_pushinteger(state, message.getNoteNumber());
            lua_pushnumber(state, message.getFloatVelocity());
            lua_call(state, 2, 0);
        }

        if (self->instrument)
        {
            lua_rawgeti(state, LUA_REGISTRYINDEX, self->renderRef);
            lua_call(state, 0, 2);
        }
        else
        {
            lua_rawgeti(state, LUA_REGISTRYINDEX, self->processRef);
            lua_pushnumber(state, self->blockLeft[i]);
            lua_pushnumber(state, self->blockRight[i]);
            lua_call(state, 2, 2);
        }
        const auto left = float(lua_tonumber(state, -2));
        const auto right = lua_isnumber(state, -1) ? float(lua_tonumber(state, -1)) : left;
        lua_pop(state, 2);
        self->blockLeft[i] = std::isfinite(left) ? juce::jlimit(-4.0f, 4.0f, left) : 0.0f;
        self->blockRight[i] = std::isfinite(right) ? juce::jlimit(-4.0f, 4.0f, right) : 0.0f;
    }
    return 0;
}

bool LuaPluginScript::process(float* left, float* right, int numSamples, const juce::MidiBuffer& midi, double bpm, double beat)
{
    if (failure.isNotEmpty())
        return false;
    lua_pushnumber(state, bpm);
    lua_setglobal(state, "tempo");
    lua_pushnumber(state, beat);
    lua_setglobal(state, "beat");
    blockLeft = left;
    blockRight = right;
    blockSamples = numSamples;
    blockMidi = &midi;
    lua_pushlightuserdata(state, this);
    armLuaWatchdog(state, 1000);
    lua_pushcclosure(state, runBlock, 1);
    if (lua_pcall(state, 0, 0, 0) != LUA_OK)
    {
        failure = juce::String::fromUTF8(lua_tostring(state, -1));
        lua_pop(state, 1);
        return false;
    }
    if (!meterNames.empty())
    {
        lua_getglobal(state, "meters");
        for (size_t i = 0; i < meterNames.size(); ++i)
        {
            lua_getfield(state, -1, meterNames[i].toRawUTF8());
            meterValues[i].store(lua_isnumber(state, -1) ? float(lua_tonumber(state, -1)) : 0.0f, std::memory_order_relaxed);
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }
    return true;
}
}
