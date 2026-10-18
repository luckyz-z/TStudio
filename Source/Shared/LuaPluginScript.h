#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

struct lua_State;

namespace ts
{
void openPluginLibraries(lua_State*);
void armLuaWatchdog(lua_State*, int milliseconds);

struct LuaPluginInfo
{
    struct Parameter
    {
        juce::String id, name, unit;
        float min = 0.0f, max = 1.0f, value = 0.5f;
        juce::StringArray choices;
    };

    struct Preset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    juce::String name, author, description;
    bool instrument = false;
    juce::var ui;
    juce::StringArray meters;
    std::vector<Parameter> parameters;
    std::vector<Preset> presets;
};

class LuaPluginScript final
{
public:
    LuaPluginScript();
    ~LuaPluginScript();

    bool load(const juce::String& code, const juce::String& chunkName, LuaPluginInfo& info, juce::String& error);
    void prepare(double sampleRate, int blockSize);
    void setParameter(size_t index, float value);
    bool process(float* left, float* right, int numSamples, const juce::MidiBuffer& midi, double bpm, double beat);
    const juce::String& error() const noexcept { return failure; }
    bool isInstrument() const noexcept { return instrument; }
    float meter(int index) const noexcept { return index >= 0 && index < maxMeters ? meterValues[size_t(index)].load(std::memory_order_relaxed) : 0.0f; }

    static constexpr int maxMeters = 16;

    static bool inspect(const juce::String& code, const juce::String& chunkName, LuaPluginInfo& info, juce::String& error);

private:
    static int runBlock(lua_State*);
    int reference(const char* global);

    lua_State* state = nullptr;
    std::vector<juce::String> ids;
    int processRef = -1, renderRef = -1, noteOnRef = -1, noteOffRef = -1, prepareRef = -1, blockRef = -1;
    bool instrument = false;
    juce::String failure;
    float* blockLeft = nullptr;
    float* blockRight = nullptr;
    int blockSamples = 0;
    const juce::MidiBuffer* blockMidi = nullptr;
    std::vector<juce::String> meterNames;
    std::array<std::atomic<float>, maxMeters> meterValues {};
};
}
