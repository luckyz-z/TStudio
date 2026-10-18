#pragma once

#include <juce_core/juce_core.h>

struct lua_State;

namespace ts
{
juce::var toVar(lua_State*, int index);
void pushVar(lua_State*, const juce::var&);
}
