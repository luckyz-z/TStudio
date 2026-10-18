#pragma once

#include "Engine/SampleData.h"

#include <memory>

namespace ts::Pack
{
inline constexpr const char* name = "Tomato Drums";

const juce::StringArray& names();
std::shared_ptr<const SampleData> get(const juce::String& sound);
}
