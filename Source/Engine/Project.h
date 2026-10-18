#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <bitset>
#include <map>
#include <set>
#include <memory>
#include <vector>

namespace ts
{
inline constexpr int ppq = 96;
inline constexpr int ticksPerStep = ppq / 4;
inline constexpr int ticksPerBar = ppq * 4;
inline constexpr int stepsPerBar = 16;
inline constexpr int mixerTrackCount = 126;
inline constexpr int effectSlotCount = 10;
inline constexpr int playlistTrackCount = 500;
inline constexpr int defaultKey = 60;
inline constexpr float defaultChannelVolume = 0.78f;

struct Note
{
    int start = 0;
    int length = ticksPerStep;
    int key = defaultKey;
    float velocity = 0.78f;
    bool slide = false;
    bool porta = false;
    bool muted = false;

    int end() const noexcept { return start + length; }
    bool operator==(const Note&) const = default;
};

struct Pattern
{
    juce::String name;
    juce::Colour colour;
    std::map<int, std::vector<Note>> notes;

    int length() const noexcept;
    bool isEmpty() const noexcept;
};

struct AutomationPoint
{
    int tick = 0;
    float value = 0.5f;
    float tension = 0.0f;

    bool operator==(const AutomationPoint&) const = default;
};

struct AutomationTarget
{
    enum class Kind
    {
        parameter,
        channelVolume,
        channelPan,
        mixerVolume,
        mixerPan,
        tempo,
        mainVolume
    };

    Kind kind = Kind::parameter;
    int channel = -1;
    int track = -1;
    int slot = -1;
    juce::String parameter;

    static constexpr double mixerMaxGain = 1.9;
    static constexpr double tempoLow = 60.0, tempoHigh = 200.0;
    static float mixerGain(float normal) noexcept;
    static float mixerNormal(float gain) noexcept;
    static juce::String idOf(const juce::AudioProcessorParameter&);
};

struct AutomationClip
{
    juce::String name;
    juce::Colour colour;
    AutomationTarget target;
    std::vector<AutomationPoint> points;
    int length = ticksPerBar * 4;

    float valueAt(double tick) const noexcept;
};

struct Clip
{
    int track = 0;
    int start = 0;
    int length = ticksPerBar;
    int pattern = 0;
    int offset = 0;
    int automation = -1;
    bool muted = false;

    bool isAutomation() const noexcept { return automation >= 0; }

    int end() const noexcept { return start + length; }
};

struct PlaylistTrack
{
    juce::String name;
    bool muted = false;
};

struct Device
{
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::String builtin;
    juce::PluginDescription description;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer emptyMidi;
    std::atomic<bool> bypassed { false };
    std::atomic<float> mix { 1.0f };

    bool isPlugin() const noexcept { return builtin.isEmpty(); }
    juce::String getName() const { return processor->getName(); }
    void prepare(double sampleRate, int blockSize);
    void process(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, int numSamples, const juce::AudioBuffer<float>* sidechain = nullptr) noexcept;
    void process(juce::AudioBuffer<float>& audio, int numSamples, const juce::AudioBuffer<float>* sidechain = nullptr) noexcept;
};

struct ChannelOptions
{
    static constexpr std::array<int, 4> arpTicks { 12, 24, 48, 96 };

    std::atomic<float> timeShift { 0.0f };
    std::atomic<float> swingMix { 1.0f };
    std::atomic<int> cutGroup { 0 };
    std::atomic<int> cutBy { 0 };
    std::atomic<bool> mono { false };
    std::atomic<bool> porta { false };
    std::atomic<float> portaTime { 120.0f };
    std::atomic<int> arpDirection { 0 };
    std::atomic<int> arpTime { 1 };
    std::atomic<int> arpRange { 1 };
    std::atomic<float> arpGate { 0.5f };

    void copyFrom(const ChannelOptions&) noexcept;
    void save(juce::XmlElement&) const;
    void load(const juce::XmlElement&);
    juce::var toVar() const;
    void apply(const juce::var&);
};

struct Channel
{
    int id = 0;
    juce::String name;
    juce::Colour colour;
    std::unique_ptr<Device> device;
    ChannelOptions options;
    std::atomic<float> volume { defaultChannelVolume };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> muted { false };
    std::atomic<int> insert { 0 };
    juce::MidiBuffer midi;
    std::bitset<128> held;
    juce::AudioBuffer<float> output;
};

struct Send
{
    float level = 1.0f;
    bool sidechain = false;
};

struct MixerTrack
{
    juce::String name;
    std::atomic<float> volume { 1.0f };
    std::atomic<float> pan { 0.0f };
    std::atomic<float> width { 1.0f };
    std::atomic<bool> muted { false };
    std::atomic<bool> solo { false };
    std::atomic<bool> invertPhase { false };
    std::atomic<bool> swapSides { false };
    std::map<int, Send> sends;
    std::array<std::unique_ptr<Device>, effectSlotCount> slots;
    juce::AudioBuffer<float> buffer;
    juce::AudioBuffer<float> sidechain;
    bool fed = false;
    bool sidechainFed = false;
    std::atomic<float> peakLeft { 0.0f };
    std::atomic<float> peakRight { 0.0f };

    bool hasEffects() const noexcept;
};

class Project final : public juce::ChangeBroadcaster
{
public:
    Project();
    ~Project() override;

    juce::CriticalSection lock;
    std::vector<std::unique_ptr<Channel>> channels;
    std::vector<Pattern> patterns;
    std::vector<Clip> clips;
    std::vector<AutomationClip> automations;
    std::vector<PlaylistTrack> tracks;
    std::array<std::unique_ptr<MixerTrack>, mixerTrackCount> mixer;
    std::vector<int> mixerOrder;

    std::atomic<double> tempo { 130.0 };
    std::atomic<float> swing { 0.0f };
    std::atomic<float> mainVolume { 0.8f };
    std::atomic<int> currentPattern { 0 };
    std::atomic<bool> songMode { false };
    std::atomic<int> mainPitch { 0 };
    std::atomic<int> loopStart { -1 };
    std::atomic<int> loopEnd { -1 };

    bool hasLoop() const noexcept { return loopStart.load() >= 0 && loopEnd.load() > loopStart.load(); }

    juce::String title;
    juce::File file;
    bool dirty = false;
    int selectedChannel = 0;
    int nextChannelId = 1;

    Channel* findChannel(int id) const noexcept;
    int indexOfChannel(int id) const noexcept;
    Pattern& pattern() noexcept { return patterns[size_t(currentPattern.load())]; }
    int songLength() const noexcept;
    int nextFreeInsert() const noexcept;
    juce::Colour nextChannelColour() const;
    juce::Colour nextPatternColour() const;

    bool canRoute(int from, int to) const;
    bool route(int from, int to, float level, bool sidechain);
    void unroute(int from, int to);
    void routeOnly(int from, int to);
    void updateMixerOrder();

    void swapContent(Project& other);
    void touch();
    void checkpoint();
    bool undo();
    bool redo();
    void clearHistory();

    static juce::String defaultTrackName(int index) { return "Track " + juce::String(index + 1); }
    static juce::String defaultInsertName(int index) { return index == 0 ? juce::String("Master") : "Insert " + juce::String(index); }

private:
    struct Snapshot
    {
        std::vector<Pattern> patterns;
        std::vector<Clip> clips;
        std::vector<AutomationClip> automations;
    };

    Snapshot snapshot() const { return { patterns, clips, automations }; }
    void restore(Snapshot);

    std::vector<Snapshot> undoStack, redoStack;
};
}
