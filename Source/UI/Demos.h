#pragma once

#include <juce_core/juce_core.h>

namespace ts::demos
{
juce::StringArray names();
juce::String load(const juce::String& name);
}
