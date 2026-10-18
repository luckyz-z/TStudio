#include "UI/Demos.h"

#include <BinaryData.h>

namespace ts::demos
{
juce::StringArray names()
{
    juce::StringArray result;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (const juce::String file(BinaryData::originalFilenames[i]); file.endsWith(".tsp"))
            result.add(file.upToLastOccurrenceOf(".tsp", false, false));
    result.sort(true);
    return result;
}

juce::String load(const juce::String& name)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (juce::String(BinaryData::originalFilenames[i]) != name + ".tsp")
            continue;
        int size = 0;
        if (const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
            return juce::String::fromUTF8(data, size);
    }
    return {};
}
}
