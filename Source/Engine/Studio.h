#pragma once

#include "Engine/AudioEngine.h"
#include "Engine/PluginHost.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace ts
{
class Studio final
{
public:
    Studio();
    ~Studio();

    std::unique_ptr<Device> createBuiltin(const juce::String& name);
    std::unique_ptr<Device> createPlugin(const juce::PluginDescription&, juce::String& error);

    Channel& addChannel(std::unique_ptr<Device>, const juce::String& name);
    Channel* addSampleChannel(const juce::File&);
    Channel& addPackChannel(const juce::String& sound);
    Channel& cloneChannel(int id);
    void removeChannel(int id);
    void moveChannel(int id, int delta);
    void setChannelDevice(int id, std::unique_ptr<Device>);
    void setSlot(int track, int slot, std::unique_ptr<Device>);
    void moveSlot(int track, int slot, int delta);
    void selectChannel(int id);
    int addPattern();
    void rememberRecent(const juce::File&);
    juce::Array<juce::File> recentFiles();
    static juce::File userFolder(const juce::String& name);
    int addAutomation(const AutomationTarget&, const juce::String& name, float value, int start = 0, int length = 0, int track = -1);
    int automateParameter(juce::AudioProcessor&, int parameterIndex);
    juce::String describe(const AutomationTarget&) const;
    int clonePattern(int index);
    void removePattern(int index);
    void selectPattern(int index);

    void newProject();
    bool save(const juce::File&, juce::String& error);
    bool load(const juce::File&, juce::String& error);
    bool loadXml(const juce::String& text, juce::String& error);
    bool importFlp(const juce::File&, juce::String& error);
    bool importMidi(const juce::File&, int channelId, juce::String& error);
    bool exportMidi(const juce::File&, int channelId, juce::String& error);
    void exportWave(const juce::File&, std::function<void(bool, juce::String)> done);
    bool renderNow(const juce::File&, juce::String& error);
    void saveSettings();

    Project project;
    AudioEngine engine { project };
    PluginHost plugins;
    juce::AudioDeviceManager devices;
    juce::AudioSourcePlayer player;
    std::function<void(juce::AudioProcessor*)> onDeviceRemoving;
    std::function<void()> onContentReplacing;

private:
    void retire(std::unique_ptr<Device>);
    void replace(Project& next);
    void fillDefaults(Project& target);
    std::unique_ptr<Device> makeDevice(std::unique_ptr<juce::AudioProcessor>, const juce::String& builtin, const juce::PluginDescription&);
    std::unique_ptr<juce::XmlElement> serialise();
    bool deserialise(const juce::XmlElement&, Project& target, juce::String& error);
    std::unique_ptr<juce::XmlElement> serialise(const Device&);
    std::unique_ptr<Device> deserialiseDevice(const juce::XmlElement&, juce::String& error);
};
}
