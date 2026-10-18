#include "Engine/Mp3Format.h"

#include <lame.h>

namespace ts
{
class Mp3Format::Writer final : public juce::AudioFormatWriter
{
public:
    Writer(juce::OutputStream* stream, double rate, unsigned int channels, lame_global_flags* flags)
        : AudioFormatWriter(stream, "MP3 file", rate, channels, 32), encoder(flags)
    {
        usesFloatingPointData = true;
        startOfStream = stream->getPosition();
    }

    ~Writer() override
    {
        if (output != nullptr)
        {
            ensureSpace(7200);
            if (const auto bytes = lame_encode_flush(encoder, encoded.data(), int(encoded.size())); bytes > 0)
                output->write(encoded.data(), size_t(bytes));
            std::array<unsigned char, 2880> tag {};
            if (const auto size = lame_get_lametag_frame(encoder, tag.data(), tag.size()); size > 0 && size <= tag.size())
            {
                const auto end = output->getPosition();
                if (output->setPosition(startOfStream))
                {
                    output->write(tag.data(), size);
                    output->setPosition(end);
                }
            }
            output->flush();
        }
        lame_close(encoder);
    }

    bool write(const int** samples, int numSamples) override
    {
        if (numSamples <= 0)
            return true;
        const auto* left = reinterpret_cast<const float*>(samples[0]);
        const auto* right = numChannels > 1 && samples[1] != nullptr ? reinterpret_cast<const float*>(samples[1]) : left;
        ensureSpace(size_t(numSamples) * 5 / 4 + 7200);
        const auto bytes = lame_encode_buffer_ieee_float(encoder, left, right, numSamples, encoded.data(), int(encoded.size()));
        return bytes >= 0 && (bytes == 0 || output->write(encoded.data(), size_t(bytes)));
    }

private:
    void ensureSpace(size_t bytes)
    {
        if (encoded.size() < bytes)
            encoded.resize(bytes);
    }

    lame_global_flags* encoder;
    std::vector<unsigned char> encoded;
    juce::int64 startOfStream = 0;
};

Mp3Format::Mp3Format() : AudioFormat("MP3 file", ".mp3") {}

juce::AudioFormatWriter* Mp3Format::createWriterFor(juce::OutputStream* stream, double sampleRate, unsigned int numChannels, int, const juce::StringPairArray&,
                                                    int qualityOptionIndex)
{
    if (stream == nullptr || numChannels < 1 || numChannels > 2 || sampleRate < 8000.0)
        return nullptr;
    const std::array<int, 4> rates { 128, 192, 256, 320 };
    const auto kbps = rates[size_t(juce::jlimit(0, 3, qualityOptionIndex))];
    auto* flags = lame_init();
    if (flags == nullptr)
        return nullptr;
    lame_set_in_samplerate(flags, juce::roundToInt(sampleRate));
    lame_set_out_samplerate(flags, juce::roundToInt(sampleRate) == 44100 ? 44100 : (sampleRate >= 48000.0 ? 48000 : 44100));
    lame_set_num_channels(flags, int(numChannels));
    lame_set_mode(flags, numChannels == 1 ? MONO : JOINT_STEREO);
    lame_set_VBR(flags, vbr_off);
    lame_set_brate(flags, kbps);
    lame_set_quality(flags, 2);
    lame_set_bWriteVbrTag(flags, 1);
    if (lame_init_params(flags) < 0)
    {
        lame_close(flags);
        return nullptr;
    }
    return new Writer(stream, sampleRate, numChannels, flags);
}
}
