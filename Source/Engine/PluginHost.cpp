#include "Engine/PluginHost.h"

namespace ts
{
PluginHost::PluginHost()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "TStudio";
    options.filenameSuffix = ".settings";
    options.folderName = "TStudio";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters(options);

    formats.addDefaultFormats();
    if (const auto saved = settings().getXmlValue("plugins"))
        known.recreateFromXml(*saved);
    known.addChangeListener(this);
}

PluginHost::~PluginHost()
{
    known.removeChangeListener(this);
}

juce::File PluginHost::deadMansPedal()
{
    return properties.getUserSettings()->getFile().getSiblingFile("PluginScanCrash.txt");
}

juce::Array<juce::PluginDescription> PluginHost::list(bool instruments) const
{
    juce::Array<juce::PluginDescription> result;
    for (const auto& type : known.getTypes())
        if (type.isInstrument == instruments)
            result.add(type);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return result;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create(const juce::PluginDescription& description, double sampleRate, int blockSize, juce::String& error)
{
    auto instance = formats.createPluginInstance(description, sampleRate, blockSize, error);
    if (instance != nullptr && !description.isInstrument && instance->getBusCount(true) > 1)
    {
        auto layout = instance->getBusesLayout();
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
        if (instance->checkBusesLayoutSupported(layout))
            instance->setBusesLayout(layout);
    }
    return instance;
}

void PluginHost::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (const auto xml = known.createXml())
        settings().setValue("plugins", xml.get());
    settings().saveIfNeeded();
}
}
