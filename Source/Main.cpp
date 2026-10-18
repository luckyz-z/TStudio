#include "UI/MainComponent.h"
#include "UI/ChannelSettings.h"
#include "UI/Community.h"
#include "UI/Controls.h"
#include "UI/Theme.h"
#include "Engine/Builtins/Registry.h"

#include <BinaryData.h>

namespace ts
{
void writeCrashLog(void*)
{
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TStudio");
    folder.createDirectory();
    folder.getChildFile("crash.log").replaceWithText("TStudio " JUCE_APPLICATION_VERSION_STRING " crashed at " + juce::Time::getCurrentTime().toString(true, true)
                                                        + juce::newLine + juce::SystemStats::getStackBacktrace());
}

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains("--"); }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        const juce::File file(commandLine.unquoted().trim());
        if (window == nullptr || !file.existsAsFile())
            return;
        window->toFront(true);
        window->main().openFileWhenSaved(file);
    }

    void initialise(const juce::String& commandLine) override
    {
        juce::SystemStats::setApplicationCrashHandler(writeCrashLog);
        if (!commandLine.contains("--"))
        {
            splash = std::make_unique<Splash>(getApplicationVersion());
            const auto launched = juce::Time::getMillisecondCounter();
            if (ui::startupSoundEnabled())
                ui::playStartupSound();
            juce::Timer::callAfterDelay(120, [this, commandLine, launched]
            {
                window = std::make_unique<Window>(getApplicationName());
                splash->setStatus("opening project...");
                ui::registerFileType();
                const auto remaining = int(juce::jmax<juce::int64>(150, 1500 - juce::int64(juce::Time::getMillisecondCounter() - launched)));
                juce::Timer::callAfterDelay(remaining, [this, commandLine]
                {
                    window->setVisible(true);
                    window->toFront(true);
                    window->getContentComponent()->grabKeyboardFocus();
                    if (const juce::File file(commandLine.unquoted().trim()); commandLine.isNotEmpty() && file.existsAsFile())
                        window->main().openFile(file);
                    else
                        window->main().playStartupDemo();
                    splash.reset();
                    window->main().welcome();
                });
            });
            return;
        }

        window = std::make_unique<Window>(getApplicationName());
        window->setVisible(true);
        window->main().silence();
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        const auto option = [&args](const juce::String& name) { return args.contains(name) ? args[args.indexOf(name) + 1].unquoted() : juce::String(); };

        if (const auto demo = option("--demo"); demo.isNotEmpty())
            window->main().openDemo(demo);
        else if (const juce::File file(option("--open").isNotEmpty() ? option("--open") : commandLine.unquoted().trim()); file.existsAsFile())
            window->main().openFile(file);

        if (const auto views = option("--view"); views.isNotEmpty())
            window->main().showViews(juce::StringArray::fromTokens(views, ",", {}));

        auto& studio = window->main().getStudio();
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        juce::String report;
        if (const auto path = option("--add-plugin"); path.isNotEmpty())
        {
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* format : studio.plugins.formats.getFormats())
                if (format->fileMightContainThisPluginType(path))
                    format->findAllTypesForFile(found, cwd.getChildFile(path).getFullPathName());
            juce::String error = found.isEmpty() ? juce::String("no plugin found in ") + path : juce::String();
            if (!found.isEmpty())
            {
                const auto description = *found.getFirst();
                if (auto device = studio.createPlugin(description, error))
                {
                    if (description.isInstrument)
                        studio.addChannel(std::move(device), description.name);
                    else
                        studio.setSlot(1, 0, std::move(device));
                }
            }
            if (error.isNotEmpty())
                cwd.getChildFile("tstudio-plugin-error.txt").replaceWithText(error);
        }
        if (const auto keys = option("--press"); keys.isNotEmpty())
            window->main().pressKeys(keys);
        if (args.contains("--mcp"))
            window->main().startMcp(false);
        if (const auto path = option("--lua"); path.isNotEmpty())
        {
            const auto result = window->main().getScripts().runFile(cwd.getChildFile(path));
            cwd.getChildFile("tstudio-lua-output.txt").replaceWithText(result.output + (result.ok ? juce::String() : "ERROR: " + result.error));
        }
        if (const auto path = option("--export-midi"); path.isNotEmpty())
            if (juce::String error; !studio.exportMidi(cwd.getChildFile(path), -1, error))
                report << error << juce::newLine;
        if (const auto path = option("--import-midi"); path.isNotEmpty())
            if (juce::String error; !studio.importMidi(cwd.getChildFile(path), studio.project.selectedChannel, error))
                report << error << juce::newLine;
        if (const auto path = option("--save"); path.isNotEmpty())
            if (juce::String error; !studio.save(cwd.getChildFile(path), error))
                report << error << juce::newLine;
        if (report.isNotEmpty())
            cwd.getChildFile("tstudio-cli-errors.txt").replaceWithText(report);
        if (args.contains("--quit"))
        {
            quit();
            return;
        }

        if (const auto render = option("--render"); render.isNotEmpty())
        {
            juce::String error;
            if (!window->main().renderTo(juce::File::getCurrentWorkingDirectory().getChildFile(render), error))
                juce::File::getCurrentWorkingDirectory().getChildFile(render + ".error.txt").replaceWithText(error);
            quit();
            return;
        }

        if (const auto editorName = option("--editor"); editorName.isNotEmpty())
        {
            if (editorName == "star")
            {
                juce::PropertiesFile::Options scratch;
                scratch.applicationName = "TStudio snapshot";
                scratch.filenameSuffix = ".settings";
                scratch.folderName = "TStudio";
                juce::PropertiesFile settings(scratch);
                const auto view = community::createStarPrompt(settings);
                const auto image = view->createComponentSnapshot(view->getLocalBounds(), true, 1.0f);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(option("--snapshot"));
                file.deleteFile();
                if (juce::FileOutputStream stream(file); stream.openedOk())
                    juce::PNGImageFormat().writeImageToStream(image, stream);
                quit();
                return;
            }
            if (editorName == "splash")
            {
                Splash view(getApplicationVersion());
                juce::Thread::sleep(900);
                const auto image = view.createComponentSnapshot(view.getLocalBounds(), true, 1.0f);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(option("--snapshot"));
                file.deleteFile();
                if (juce::FileOutputStream stream(file); stream.openedOk())
                    juce::PNGImageFormat().writeImageToStream(image, stream);
                if (args.contains("--chirp"))
                    ui::writeStartupSound(juce::File::getCurrentWorkingDirectory().getChildFile("chirp.wav"));
                quit();
                return;
            }
            if (editorName == "channel-settings")
            {
                ChannelSettings view(studio, studio.project.selectedChannel);
                const auto image = view.createComponentSnapshot(view.getLocalBounds(), true, 1.0f);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(option("--snapshot"));
                file.deleteFile();
                if (juce::FileOutputStream stream(file); stream.openedOk())
                    juce::PNGImageFormat().writeImageToStream(image, stream);
                quit();
                return;
            }
            std::unique_ptr<juce::AudioProcessor> processor = Builtins::create(editorName);
            if (processor == nullptr && cwd.getChildFile(editorName).exists())
            {
                juce::OwnedArray<juce::PluginDescription> found;
                for (auto* format : studio.plugins.formats.getFormats())
                    format->findAllTypesForFile(found, cwd.getChildFile(editorName).getFullPathName());
                juce::String error;
                if (!found.isEmpty())
                    processor = studio.plugins.formats.createPluginInstance(*found.getFirst(), 48000.0, 512, error);
            }
            if (processor != nullptr)
            {
                processor->setRateAndBufferSizeDetails(48000.0, 512);
                processor->prepareToPlay(48000.0, 512);
                if (const auto preset = option("--preset"); preset.isNotEmpty())
                    processor->setCurrentProgram(preset.getIntValue());
                const std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(option("--snapshot"));
                file.deleteFile();
                if (juce::FileOutputStream stream(file); stream.openedOk())
                    juce::PNGImageFormat().writeImageToStream(image, stream);
            }
            quit();
            return;
        }


        snapshotFile = option("--snapshot");
        if (snapshotFile.isNotEmpty())
            startTimer(juce::jmax(300, option("--delay").getIntValue()));
    }

    void shutdown() override
    {
        splash.reset();
        window.reset();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr && snapshotFile.isEmpty())
            window->main().requestQuit();
        else
            quit();
    }

private:
    void timerCallback() override
    {
        stopTimer();
        auto& content = window->main();
        const auto image = content.createComponentSnapshot(content.getLocalBounds(), true, 1.0f);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(snapshotFile);
        file.deleteFile();
        if (juce::FileOutputStream stream(file); stream.openedOk())
            juce::PNGImageFormat().writeImageToStream(image, stream);
        quit();
    }

    class Splash final : public juce::Component, private juce::Timer
    {
    public:
        explicit Splash(const juce::String& version) : logo(juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize)), versionText(version)
        {
            setOpaque(false);
            setSize(440, 400);
            addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIsSemiTransparent);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
            toFront(false);
            started = juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
        }

        void setStatus(const juce::String& text)
        {
            status = text;
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            const auto age = float((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
            const auto bounds = getLocalBounds().toFloat().reduced(6.0f);
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRoundedRectangle(bounds.translated(0.0f, 4.0f), 18.0f);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b3135), bounds.getCentreX(), bounds.getY(), juce::Colour(0xff15191b), bounds.getCentreX(),
                                                   bounds.getBottom(), false));
            g.fillRoundedRectangle(bounds, 18.0f);
            g.setColour(juce::Colour(0xff3a4246));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 18.0f, 1.0f);

            const auto pop = juce::jlimit(0.0f, 1.0f, age / 0.45f);
            const auto overshoot = 1.0f + 0.12f * std::sin(pop * juce::MathConstants<float>::pi) * (1.0f - pop * 0.4f);
            const auto scale = (0.55f + 0.45f * (1.0f - std::pow(1.0f - pop, 3.0f))) * overshoot;
            const auto centre = juce::Point<float>(bounds.getCentreX(), bounds.getY() + 150.0f);
            const auto glow = 0.5f + 0.5f * std::sin(age * 3.0f);
            g.setGradientFill(juce::ColourGradient(theme::accent.withAlpha(0.28f + 0.12f * glow), centre.x, centre.y, juce::Colours::transparentBlack,
                                                   centre.x + 150.0f, centre.y, true));
            g.fillEllipse(juce::Rectangle<float>(300.0f, 300.0f).withCentre(centre));
            const auto size = 210.0f * scale;
            g.setOpacity(juce::jlimit(0.0f, 1.0f, pop * 2.0f));
            g.drawImage(logo, juce::Rectangle<float>(size, size).withCentre(centre), juce::RectanglePlacement::centred);
            g.setOpacity(1.0f);

            const auto text = juce::jlimit(0.0f, 1.0f, (age - 0.3f) / 0.4f);
            g.setColour(theme::text.withAlpha(text));
            g.setFont(theme::font(34.0f, juce::Font::bold));
            g.drawText("TStudio", bounds.withTrimmedTop(262.0f).withHeight(44.0f), juce::Justification::centred);
            g.setColour(theme::textDim.withAlpha(text));
            g.setFont(theme::font(12.5f));
            g.drawText("v" + versionText + "  -  " + status, bounds.withTrimmedTop(306.0f).withHeight(20.0f), juce::Justification::centred);

            const auto track = juce::Rectangle<float>(bounds.getX() + 70.0f, bounds.getBottom() - 42.0f, bounds.getWidth() - 140.0f, 4.0f);
            g.setColour(juce::Colour(0xff101315));
            g.fillRoundedRectangle(track, 2.0f);
            const auto fill = 1.0f - std::exp(-age * 1.6f);
            g.setGradientFill(juce::ColourGradient(theme::accent.darker(0.3f), track.getX(), 0.0f, theme::accent.brighter(0.2f), track.getRight(), 0.0f, false));
            g.fillRoundedRectangle(track.withWidth(track.getWidth() * fill), 2.0f);
        }

    private:
        void timerCallback() override { repaint(); }

        juce::Image logo;
        juce::String versionText, status { "loading audio engine..." };
        double started = 0.0;
    };

    class Window final : public juce::DocumentWindow
    {
    public:
        explicit Window(const juce::String& name) : DocumentWindow(name, theme::window, allButtons)
        {
            setUsingNativeTitleBar(true);
            setIcon(juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize));
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(960, 600, 10000, 10000);
            centreWithSize(getWidth(), getHeight());
        }

        MainComponent& main() { return *static_cast<MainComponent*>(getContentComponent()); }
        void closeButtonPressed() override { main().requestQuit(); }
    };

    std::unique_ptr<Splash> splash;
    std::unique_ptr<Window> window;
    juce::String snapshotFile;
};
}

START_JUCE_APPLICATION(ts::Application)
