#include "Engine/Mp3Format.h"
#include "Engine/Studio.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace ts
{
namespace
{
constexpr double tailSeconds = 3.0;

class OfflineSession final
{
public:
    explicit OfflineSession(Studio& s) : studio(s), wasSongMode(s.project.songMode.load()), loopStart(s.project.loopStart.load()), loopEnd(s.project.loopEnd.load())
    {
        studio.project.loopStart.store(-1);
        studio.project.loopEnd.store(-1);
        studio.engine.setPlaying(false);
        studio.player.setSource(nullptr);
        studio.engine.setNonRealtime(true);
        studio.project.songMode.store(true);
    }

    ~OfflineSession()
    {
        studio.engine.setNonRealtime(false);
        studio.project.songMode.store(wasSongMode);
        studio.project.loopStart.store(loopStart);
        studio.project.loopEnd.store(loopEnd);
        studio.player.setSource(&studio.engine);
    }

private:
    Studio& studio;
    bool wasSongMode;
    int loopStart, loopEnd;
};

bool render(Studio& studio, const juce::File& file, const std::function<bool(double)>& progress, juce::String& error)
{
    auto& engine = studio.engine;
    const auto rate = engine.getSampleRate();
    const auto block = engine.getBlockSize();

    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (!stream->openedOk())
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    std::unique_ptr<juce::AudioFormat> format;
    auto bits = 24;
    auto quality = 0;
    if (file.hasFileExtension("flac"))
    {
        format = std::make_unique<juce::FlacAudioFormat>();
    }
    else if (file.hasFileExtension("mp3"))
    {
        format = std::make_unique<Mp3Format>();
        quality = 3;
    }
    else if (file.hasFileExtension("ogg"))
    {
        format = std::make_unique<juce::OggVorbisAudioFormat>();
        bits = 16;
        quality = format->getQualityOptions().size() - 2;
    }
    else
    {
        format = std::make_unique<juce::WavAudioFormat>();
    }
    std::unique_ptr<juce::AudioFormatWriter> writer(format->createWriterFor(stream.get(), rate, 2, bits, {}, juce::jmax(0, quality)));
    if (writer == nullptr)
    {
        error = "Could not create the " + file.getFileExtension() + " file.";
        return false;
    }
    stream.release();

    const auto songSamples = juce::int64(studio.project.songLength() * 60.0 / (studio.project.tempo.load() * ppq) * rate);
    const auto total = songSamples + juce::int64(tailSeconds * rate);
    juce::AudioBuffer<float> buffer(2, block);

    engine.setSongPosition(0.0);
    engine.setPlaying(true);
    for (juce::int64 position = 0; position < total;)
    {
        if (progress && !progress(double(position) / double(total)))
        {
            engine.setPlaying(false);
            error = "Export cancelled.";
            return false;
        }
        auto count = juce::jmin<juce::int64>(block, total - position);
        if (position < songSamples)
            count = juce::jmin(count, songSamples - position);
        engine.render(buffer, 0, int(count));
        writer->writeFromAudioSampleBuffer(buffer, 0, int(count));
        position += count;
        if (position >= songSamples && engine.isPlaying())
            engine.setPlaying(false);
    }
    engine.setPlaying(false);
    return true;
}

class Renderer final : public juce::ThreadWithProgressWindow
{
public:
    Renderer(Studio& s, juce::File f, std::function<void(bool, juce::String)> callback)
        : ThreadWithProgressWindow("Exporting " + f.getFileName(), true, true),
          studio(s),
          file(std::move(f)),
          done(std::move(callback)),
          session(std::make_unique<OfflineSession>(s))
    {
    }

    void run() override
    {
        success = render(studio, file, [this](double value)
        {
            setProgress(value);
            return !threadShouldExit();
        }, message);
    }

    void threadComplete(bool cancelled) override
    {
        session.reset();
        if (cancelled && message.isEmpty())
            message = "Export cancelled.";
        if (done)
            done(success && !cancelled, message);
        delete this;
    }

private:
    Studio& studio;
    juce::File file;
    std::function<void(bool, juce::String)> done;
    std::unique_ptr<OfflineSession> session;
    juce::String message;
    bool success = false;
};
}

void Studio::exportWave(const juce::File& file, std::function<void(bool, juce::String)> done)
{
    (new Renderer(*this, file, std::move(done)))->launchThread();
}

bool Studio::renderNow(const juce::File& file, juce::String& error)
{
    const OfflineSession session(*this);
    return render(*this, file, {}, error);
}
}
