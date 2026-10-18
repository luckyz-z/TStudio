#include "UI/Controls.h"

#if JUCE_WINDOWS
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <shlobj.h>
 #include <mmsystem.h>
 #include <psapi.h>
 #pragma comment(lib, "psapi.lib")
 #pragma comment(lib, "shell32.lib")
 #pragma comment(lib, "winmm.lib")
#endif

#include <juce_audio_formats/juce_audio_formats.h>

namespace ts::ui
{
namespace
{
const juce::MemoryBlock& startupSound()
{
    static const auto wave = []
    {
        constexpr double rate = 44100.0;
        juce::AudioBuffer<float> audio(1, int(rate * 0.75));
        audio.clear();
        auto* data = audio.getWritePointer(0);
        auto phase = 0.0;
        for (int i = 0; i < int(rate * 0.09); ++i)
        {
            const auto t = i / rate;
            const auto frequency = 520.0 * std::pow(2.0, t / 0.09 * 1.6);
            phase += frequency / rate;
            data[i] += float(std::sin(juce::MathConstants<double>::twoPi * phase) * std::sin(juce::MathConstants<double>::pi * t / 0.09) * 0.32);
        }
        const auto bell = [&](double start, double frequency, double level)
        {
            for (int i = int(start * rate); i < audio.getNumSamples(); ++i)
            {
                const auto t = i / rate - start;
                const auto envelope = (1.0 - std::exp(-t * 400.0)) * std::exp(-t * 6.5) * level;
                const auto tone = std::sin(juce::MathConstants<double>::twoPi * frequency * t) + 0.35 * std::sin(juce::MathConstants<double>::twoPi * frequency * 2.0 * t) * std::exp(-t * 12.0);
                data[i] += float(tone * envelope);
            }
        };
        bell(0.085, 1046.5, 0.22);
        bell(0.17, 1567.98, 0.18);
        juce::MemoryBlock block;
        {
            auto stream = std::make_unique<juce::MemoryOutputStream>(block, false);
            std::unique_ptr<juce::AudioFormatWriter> writer(juce::WavAudioFormat().createWriterFor(stream.get(), rate, 1, 16, {}, 0));
            if (writer != nullptr)
            {
                stream.release();
                writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
            }
        }
        return block;
    }();
    return wave;
}
}

bool startupSoundEnabled()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "TStudio";
    options.filenameSuffix = ".settings";
    options.folderName = "TStudio";
    options.osxLibrarySubFolder = "Application Support";
    return juce::PropertiesFile(options).getBoolValue("startupSound", true);
}

void playStartupSound()
{
#if JUCE_WINDOWS
    const auto& wave = startupSound();
    PlaySoundW(static_cast<LPCWSTR>(wave.getData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
}

int processMemoryMegabytes()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return int(counters.WorkingSetSize / (1024 * 1024));
#endif
    return 0;
}

void writeStartupSound(const juce::File& file)
{
    file.replaceWithData(startupSound().getData(), startupSound().getSize());
}

void attachToOwner(juce::Component& window, juce::Component& owner)
{
#if JUCE_WINDOWS
    auto* top = owner.getTopLevelComponent();
    auto* child = static_cast<HWND>(window.getWindowHandle());
    auto* parent = top != nullptr ? static_cast<HWND>(top->getWindowHandle()) : nullptr;
    if (child != nullptr && parent != nullptr)
        SetWindowLongPtrW(child, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(parent));
#else
    juce::ignoreUnused(window, owner);
#endif
}

void registerFileType()
{
#if JUCE_WINDOWS
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
    const juce::String root = "HKEY_CURRENT_USER\\Software\\Classes\\";
    const auto command = "\"" + exe + "\" \"%1\"";
    if (juce::WindowsRegistry::getValue(root + "TStudio.Project\\shell\\open\\command\\") == command)
        return;
    juce::WindowsRegistry::setValue(root + ".tsp\\", "TStudio.Project");
    juce::WindowsRegistry::setValue(root + ".lfs\\", "TStudio.Project");
    juce::WindowsRegistry::setValue(root + "TStudio.Project\\", "TStudio Project");
    juce::WindowsRegistry::setValue(root + "TStudio.Project\\DefaultIcon\\", "\"" + exe + "\",0");
    juce::WindowsRegistry::setValue(root + "TStudio.Project\\shell\\open\\command\\", command);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}
}
