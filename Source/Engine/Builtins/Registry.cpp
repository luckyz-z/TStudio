#include "Engine/Builtins/Registry.h"
#include "Engine/Builtins/Sampler.h"
#include "Engine/Builtins/Synth.h"
#include "Engine/Builtins/LuaPlugins.h"
#include "Engine/Builtins/NativePlugins.h"

namespace ts::Builtins
{
juce::StringArray generators()
{
    juce::StringArray names { Sampler::typeName, Synth::typeName, "Tomato 808", "Tomato Pluck", "Tomato Keys" };
    names.addArray(LuaPlugins::names(true));
    names.addArray(NativePlugins::names(true));
    return names;
}

std::unique_ptr<juce::AudioProcessor> create(const juce::String& name)
{
    if (name.startsWith("LF "))
        return create("Tomato " + name.substring(3));
    if (name == Sampler::typeName)
        return std::make_unique<Sampler>();
    if (name == Synth::typeName)
        return std::make_unique<Synth>();
    if (auto instrument = createInstrument(name))
        return instrument;
    if (auto effect = createEffect(name))
        return effect;
    if (auto lua = LuaPlugins::create(name))
        return lua;
    return NativePlugins::create(name);
}
}
