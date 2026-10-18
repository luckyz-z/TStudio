#pragma once

#include "Engine/Automation.h"
#include "Engine/ScriptEngine.h"

namespace ts
{
class McpServer final : private juce::Thread
{
public:
    static constexpr int defaultPort = 7341;

    McpServer(Automation&, ScriptEngine&);
    ~McpServer() override;

    bool start(int port, juce::String& error);
    void stop();
    bool isRunning() const { return isThreadRunning(); }
    int port() const noexcept { return listeningPort; }

    juce::var handle(const juce::var& request);

private:
    void run() override;
    void serve(juce::StreamingSocket&);
    juce::var handleOnMessageThread(const juce::var& request);
    juce::var tools() const;
    juce::var callTool(const juce::String& name, const juce::var& arguments);

    Automation& automation;
    ScriptEngine& scripts;
    juce::StreamingSocket listener;
    int listeningPort = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
}
