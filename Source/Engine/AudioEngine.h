#pragma once

#include "Engine/Builtins/BuiltinProcessor.h"
#include "Engine/Project.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace ts
{
struct SampleData;

struct RecordedNote
{
    int key = 0;
    float velocity = 0.0f;
    double tick = 0.0;
    double loop = 0.0;
    bool on = false;
};

class AudioEngine final : public juce::AudioSource, private juce::AudioPlayHead, private juce::AudioProcessorListener
{
public:
    std::pair<juce::AudioProcessor*, int> lastTweaked() const;

    static constexpr double defaultTempo = 130.0;

    explicit AudioEngine(Project&);

    void prepareToPlay(int samplesPerBlockExpected, double newSampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

    void prepare(Device&);
    void prepare(Channel&);
    void prepare(MixerTrack&);
    void prepare(Project&);

    void setPlaying(bool shouldPlay) noexcept;
    void panic() noexcept { panicRequested.store(true); }
    bool isPlaying() const noexcept { return playing.load(); }
    void setSongPosition(double ticks) noexcept { requestedPosition.store(juce::jmax(0.0, ticks)); }
    double getSongPosition() const noexcept { return position.load(); }
    double getSampleRate() const noexcept { return sampleRate.load(); }
    int getBlockSize() const noexcept { return blockSize.load(); }
    void setNonRealtime(bool) noexcept;
    void render(juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept;
    void preview(std::shared_ptr<const SampleData>);
    void sendLive(const juce::MidiMessage&);
    int readRecorded(RecordedNote* destination, int maximum);

    juce::MidiMessageCollector live;
    ScopeBuffer output;
    std::atomic<int> liveChannel { -1 };
    std::atomic<bool> metronome { false };
    std::atomic<bool> recording { false };
    std::atomic<bool> silent { false };

private:
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override;

    mutable juce::SpinLock tweakLock;
    juce::AudioProcessor* tweakedProcessor = nullptr;
    int tweakedParameter = -1;

    void applyRequests() noexcept;
    void sequence(int numSamples) noexcept;
    void emit(double from, double to, double firstSample, double samplesPerTick, int numSamples, bool ons) noexcept;
    void emitPattern(const Pattern&, double from, double to, double cut, double firstSample, double samplesPerTick, int numSamples, bool ons) noexcept;
    void releaseAll(int sampleOffset) noexcept;
    static const Note* glideSource(const std::vector<Note>& notes, const Note& target) noexcept;
    static int arpIndex(int direction, int step, int count, int salt) noexcept;
    void cutChannels(int group, int sample) noexcept;
    void applyAutomation() noexcept;
    void renderChannels(int numSamples) noexcept;
    void renderMixer(int numSamples) noexcept;
    void renderExtras(int numSamples) noexcept;
    void capture(double blockStart, int numSamples) noexcept;
    MixerTrack& feed(int index, int numSamples) noexcept;
    void markSoloPaths(std::array<bool, mixerTrackCount>& audible) const noexcept;

    Project& project;
    std::atomic<bool> playing { false };
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> panicRequested { false };
    int appliedPitch = 0;
    std::atomic<double> requestedPosition { -1.0 };
    std::atomic<double> position { 0.0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<int> blockSize { 512 };
    juce::MidiBuffer liveMidi;
    juce::AudioPlayHead::PositionInfo playInfo;
    juce::int64 samplesPlayed = 0;
    juce::AudioBuffer<float> clickHigh, clickLow;
    const juce::AudioBuffer<float>* click = nullptr;
    int clickPosition = 0;
    int clickDelay = 0;
    std::array<float, 2> dcInput {}, dcOutput {};
    std::shared_ptr<const SampleData> previewSample;
    double previewPosition = -1.0;
    juce::AbstractFifo recordFifo { 1024 };
    std::array<RecordedNote, 1024> recordBuffer;
};
}
