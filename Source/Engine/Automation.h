#pragma once

#include "Engine/Studio.h"

namespace ts
{
class Automation final
{
public:
    struct Method
    {
        juce::String name;
        juce::String description;
        juce::var schema;
    };

    explicit Automation(Studio&);

    juce::var call(const juce::String& method, const juce::var& arguments);
    const std::vector<Method>& methods() const noexcept { return table; }

private:
    using Handler = std::function<juce::var(const juce::var&)>;

    void add(const juce::String& name, const juce::String& description, juce::var schema, Handler);
    void registerProject();
    void registerChannels();
    void registerNotes();
    void registerArrangement();
    void registerMixer();
    void registerFiles();

    Pattern& patternFor(const juce::var& arguments);
    Device* deviceFor(const juce::var& arguments);

    Studio& studio;
    std::vector<Method> table;
    std::map<juce::String, Handler> handlers;
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>>);
}
