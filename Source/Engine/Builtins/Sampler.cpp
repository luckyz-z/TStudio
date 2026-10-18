#include "Engine/Builtins/Sampler.h"
#include "Engine/Builtins/Pack.h"
#include "Engine/Builtins/ParameterPanel.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

namespace ts
{
namespace
{
constexpr int voiceCount = 24;

struct AnySound final : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager manager;
    if (manager.getNumKnownFormats() == 0)
        manager.registerBasicFormats();
    return manager;
}
}

class Sampler::Voice final : public juce::SynthesiserVoice
{
public:
    explicit Voice(Sampler& s) : owner(s) {}

    bool canPlaySound(juce::SynthesiserSound*) override { return true; }

    void startNote(int note, float velocity, juce::SynthesiserSound*, int) override
    {
        data = owner.sample.get();
        if (data == nullptr || data->audio.getNumSamples() < 2)
        {
            clearCurrentNote();
            data = nullptr;
            return;
        }
        const auto semitones = double(note - owner.root.get()) + double(owner.pitch.get());
        step = std::pow(2.0, semitones / 12.0) * data->sampleRate / getSampleRate();
        reversed = owner.reverse.get();
        position = reversed ? double(data->audio.getNumSamples() - 2) : 0.0;
        gain = velocity;
        envelope.setSampleRate(getSampleRate());
        envelope.setParameters({ owner.attack.get() * 0.001f, 0.0f, 1.0f, juce::jmax(0.001f, owner.release.get() * 0.001f) });
        envelope.reset();
        envelope.noteOn();
    }

    void stopNote(float, bool allowTailOff) override
    {
        if (!allowTailOff)
        {
            finish();
            return;
        }
        if (owner.cut.get())
            envelope.noteOff();
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int controller, int) override
    {
        if (controller != 119 || data == nullptr)
            return;
        envelope.setParameters({ 0.001f, 0.0f, 1.0f, 0.005f });
        envelope.noteOff();
    }

    void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (data == nullptr)
            return;

        const auto& audio = data->audio;
        const auto length = audio.getNumSamples();
        const auto* left = audio.getReadPointer(0);
        const auto* right = audio.getReadPointer(juce::jmin(1, audio.getNumChannels() - 1));
        auto* outLeft = output.getWritePointer(0, startSample);
        auto* outRight = output.getWritePointer(juce::jmin(1, output.getNumChannels() - 1), startSample);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto index = int(position);
            if (index < 0 || index + 1 >= length || !envelope.isActive())
            {
                finish();
                return;
            }
            const auto fraction = float(position - index);
            const auto level = envelope.getNextSample() * gain;
            outLeft[i] += (left[index] + (left[index + 1] - left[index]) * fraction) * level;
            if (outRight != outLeft)
                outRight[i] += (right[index] + (right[index + 1] - right[index]) * fraction) * level;
            position += reversed ? -step : step;
        }
    }

private:
    void finish()
    {
        envelope.reset();
        clearCurrentNote();
        data = nullptr;
    }

    Sampler& owner;
    const SampleData* data = nullptr;
    juce::ADSR envelope;
    double position = 0.0;
    double step = 1.0;
    float gain = 1.0f;
    bool reversed = false;
};

class Sampler::Editor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    explicit Editor(Sampler& s) : AudioProcessorEditor(s), sampler(s), parameters(s)
    {
        load.onClick = [this] { choose(); };
        addAndMakeVisible(load);
        addAndMakeVisible(parameters);
        setSize(juce::jmax(480, parameters.getWidth()), waveHeight + 40 + parameters.getHeight());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        const auto data = sampler.getSample();
        g.setColour(findColour(juce::Label::textColourId));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(data != nullptr ? data->name : juce::String("Drop a sample here"), header.withTrimmedRight(110), juce::Justification::centredLeft);

        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(wave.toFloat(), 3.0f);
        if (data == nullptr || data->audio.getNumSamples() == 0)
            return;

        const auto& audio = data->audio;
        const auto samples = audio.getNumSamples();
        const auto* source = audio.getReadPointer(0);
        const auto middle = float(wave.getCentreY());
        const auto half = float(wave.getHeight()) * 0.45f;
        g.setColour(juce::Colour(0xffe8483b));
        for (int x = 0; x < wave.getWidth(); ++x)
        {
            const auto from = juce::jmin(samples - 1, int(juce::int64(x) * samples / wave.getWidth()));
            const auto to = juce::jmax(from + 1, int(juce::int64(x + 1) * samples / wave.getWidth()));
            const auto range = juce::FloatVectorOperations::findMinAndMax(source + from, juce::jmin(to, samples) - from);
            g.drawVerticalLine(wave.getX() + x, middle - range.getEnd() * half, middle - range.getStart() * half + 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 6);
        header = area.removeFromTop(24);
        load.setBounds(header.removeFromRight(100));
        area.removeFromTop(4);
        wave = area.removeFromTop(waveHeight - 10);
        parameters.setBounds(getLocalBounds().removeFromBottom(parameters.getHeight()));
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        return files.size() == 1 && Sampler::isAudioFile(juce::File(files[0]));
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (sampler.loadFile(juce::File(files[0])))
            repaint();
    }

private:
    static constexpr int waveHeight = 110;

    void choose()
    {
        chooser = std::make_unique<juce::FileChooser>("Load sample", juce::File(), formats().getWildcardForAllFormats());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<Editor>(this)](const juce::FileChooser& fc)
                             {
                                 if (safe != nullptr && fc.getResult().existsAsFile() && safe->sampler.loadFile(fc.getResult()))
                                     safe->repaint();
                             });
    }

    Sampler& sampler;
    ParameterPanel parameters;
    juce::TextButton load { "Load sample..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> header, wave;
};

Sampler::Sampler()
    : BuiltinProcessor(typeName, true),
      root(addInt("root", "Root note", 0, 127, 60)),
      pitch(addFloat("pitch", "Pitch", { -24.0f, 24.0f }, 0.0f, "st")),
      attack(addFloat("attack", "Attack", skewed(0.0f, 2000.0f, 100.0f), 0.0f, "ms")),
      release(addFloat("release", "Release", skewed(1.0f, 4000.0f, 200.0f), 20.0f, "ms")),
      cut(addBool("cut", "Cut at note off", false)),
      reverse(addBool("reverse", "Reverse", false))
{
    synth.addSound(new AnySound());
    for (int i = 0; i < voiceCount; ++i)
        synth.addVoice(new Voice(*this));
}

Sampler::~Sampler() = default;

void Sampler::setSample(std::shared_ptr<const SampleData> next)
{
    const juce::SpinLock::ScopedLockType guard(sampleLock);
    std::swap(sample, next);
    synth.allNotesOff(0, false);
}

std::shared_ptr<const SampleData> Sampler::getSample() const
{
    const juce::SpinLock::ScopedLockType guard(sampleLock);
    return sample;
}

bool Sampler::loadFile(const juce::File& file)
{
    auto data = readFile(file);
    if (data == nullptr)
        return false;
    setSample(std::move(data));
    return true;
}

std::shared_ptr<const SampleData> Sampler::readFile(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats().createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    const auto length = int(juce::jmin<juce::int64>(reader->lengthInSamples, juce::int64(reader->sampleRate * 600.0)));
    auto data = std::make_shared<SampleData>();
    data->audio.setSize(int(juce::jlimit(1u, 2u, reader->numChannels)), length);
    reader->read(&data->audio, 0, length, 0, true, true);
    data->sampleRate = reader->sampleRate;
    data->name = file.getFileNameWithoutExtension();
    data->file = file;
    return data;
}

bool Sampler::isAudioFile(const juce::File& file)
{
    return file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3;wma");
}

void Sampler::prepareToPlay(double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void Sampler::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    const juce::SpinLock::ScopedLockType guard(sampleLock);
    synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* Sampler::createEditor()
{
    return new Editor(*this);
}

void Sampler::saveExtra(juce::XmlElement& state) const
{
    const auto data = getSample();
    if (data == nullptr)
        return;
    if (data->pack.isNotEmpty())
        state.setAttribute("pack", data->pack);
    else
        state.setAttribute("file", data->file.getFullPathName());
    state.setAttribute("name", data->name);
}

void Sampler::loadExtra(const juce::XmlElement& state)
{
    if (const auto pack = state.getStringAttribute("pack"); pack.isNotEmpty())
    {
        setSample(Pack::get(pack));
        return;
    }

    const juce::File file(state.getStringAttribute("file"));
    if (state.getStringAttribute("file").isEmpty())
        return;
    if (auto data = readFile(file))
    {
        setSample(std::move(data));
        return;
    }
    auto missing = std::make_shared<SampleData>();
    missing->file = file;
    missing->name = state.getStringAttribute("name", file.getFileNameWithoutExtension()) + " (missing)";
    setSample(std::move(missing));
}
}
