#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ts::community
{
inline const juce::String repository { "https://github.com/Luckyz-z/TStudio" };
inline const juce::String plugins { "https://github.com/Luckyz-z/TStudio-Plugins" };
inline const juce::String issues { repository + "/issues/new/choose" };

void maybeAskForStar(juce::Component& parent, juce::PropertiesFile& settings);
void showStarPrompt(juce::Component& parent, juce::PropertiesFile& settings);
std::unique_ptr<juce::Component> createStarPrompt(juce::PropertiesFile& settings);
}
