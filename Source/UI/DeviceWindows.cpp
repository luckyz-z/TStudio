#include "UI/DeviceWindows.h"
#include "UI/Controls.h"
#include "UI/Theme.h"

namespace ts
{
class DeviceWindows::Window final : public juce::DocumentWindow
{
public:
    Window(DeviceWindows& o, juce::AudioProcessor& p, const juce::String& title)
        : DocumentWindow(title, theme::window, juce::DocumentWindow::closeButton), owner(o), processor(p)
    {
        setUsingNativeTitleBar(true);
        auto* editor = processor.hasEditor() ? processor.createEditorIfNeeded() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor(processor);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        ui::attachToOwner(*this, owner.owner);
    }

    ~Window() override
    {
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Window>(this)]
        {
            if (safe != nullptr)
                safe->owner.close(&safe->processor);
        });
    }

    juce::AudioProcessor* getProcessor() const noexcept { return &processor; }

private:
    DeviceWindows& owner;
    juce::AudioProcessor& processor;
};

DeviceWindows::DeviceWindows(juce::Component& o) : owner(o) {}

DeviceWindows::~DeviceWindows()
{
    closeAll();
}

void DeviceWindows::show(Device& device, const juce::String& title)
{
    for (auto& window : windows)
    {
        if (window->getProcessor() == device.processor.get())
        {
            window->setVisible(true);
            window->toFront(true);
            return;
        }
    }
    windows.push_back(std::make_unique<Window>(*this, *device.processor, title));
}

void DeviceWindows::close(juce::AudioProcessor* processor)
{
    windows.erase(std::remove_if(windows.begin(), windows.end(), [processor](const auto& w) { return w->getProcessor() == processor; }), windows.end());
}

void DeviceWindows::closeAll()
{
    windows.clear();
}
}
