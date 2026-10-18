#include "Shared/LuaVar.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace ts
{
namespace
{
bool isSequence(lua_State* state, int index)
{
    const auto length = lua_rawlen(state, index);
    auto count = lua_Unsigned(0);
    lua_pushnil(state);
    while (lua_next(state, index) != 0)
    {
        lua_pop(state, 1);
        if (!lua_isinteger(state, -1))
        {
            lua_pop(state, 1);
            return false;
        }
        ++count;
    }
    return count == length;
}
}

juce::var toVar(lua_State* state, int index)
{
    index = lua_absindex(state, index);
    switch (lua_type(state, index))
    {
        case LUA_TBOOLEAN: return bool(lua_toboolean(state, index));
        case LUA_TNUMBER: return lua_isinteger(state, index) ? juce::var(juce::int64(lua_tointeger(state, index))) : juce::var(double(lua_tonumber(state, index)));
        case LUA_TSTRING: return juce::String::fromUTF8(lua_tostring(state, index));
        case LUA_TTABLE:
        {
            if (lua_rawlen(state, index) > 0 && isSequence(state, index))
            {
                juce::Array<juce::var> array;
                for (lua_Integer i = 1; i <= lua_Integer(lua_rawlen(state, index)); ++i)
                {
                    lua_rawgeti(state, index, i);
                    array.add(toVar(state, -1));
                    lua_pop(state, 1);
                }
                return array;
            }
            auto* object = new juce::DynamicObject();
            lua_pushnil(state);
            while (lua_next(state, index) != 0)
            {
                if (lua_type(state, -2) == LUA_TSTRING)
                    object->setProperty(juce::String::fromUTF8(lua_tostring(state, -2)), toVar(state, -1));
                lua_pop(state, 1);
            }
            return juce::var(object);
        }
        default: return {};
    }
}

void pushVar(lua_State* state, const juce::var& value)
{
    if (value.isVoid() || value.isUndefined())
        lua_pushnil(state);
    else if (value.isBool())
        lua_pushboolean(state, bool(value) ? 1 : 0);
    else if (value.isInt() || value.isInt64())
        lua_pushinteger(state, lua_Integer(juce::int64(value)));
    else if (value.isDouble())
        lua_pushnumber(state, double(value));
    else if (const auto* array = value.getArray())
    {
        lua_createtable(state, array->size(), 0);
        for (int i = 0; i < array->size(); ++i)
        {
            pushVar(state, array->getReference(i));
            lua_rawseti(state, -2, i + 1);
        }
    }
    else if (const auto* object = value.getDynamicObject())
    {
        lua_createtable(state, 0, object->getProperties().size());
        for (const auto& entry : object->getProperties())
        {
            pushVar(state, entry.value);
            lua_setfield(state, -2, entry.name.toString().toRawUTF8());
        }
    }
    else
        lua_pushstring(state, value.toString().toRawUTF8());
}
}
