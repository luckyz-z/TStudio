#include "Engine/McpServer.h"

namespace ts
{
namespace
{
constexpr const char* latestProtocol = "2025-06-18";
constexpr int maxBody = 8 * 1024 * 1024;

juce::var rpcResult(const juce::var& id, const juce::var& result)
{
    return object({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } });
}

juce::var rpcError(const juce::var& id, int code, const juce::String& message)
{
    return object({ { "jsonrpc", "2.0" }, { "id", id }, { "error", object({ { "code", code }, { "message", message } }) } });
}

juce::var textContent(const juce::String& text, bool failed)
{
    return object({ { "content", juce::Array<juce::var> { object({ { "type", "text" }, { "text", text } }) } }, { "isError", failed } });
}

bool allowedOrigin(const juce::String& origin)
{
    if (origin.isEmpty())
        return true;
    const auto host = origin.fromFirstOccurrenceOf("://", false, false).upToFirstOccurrenceOf(":", false, false).upToFirstOccurrenceOf("/", false, false);
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

void reply(juce::StreamingSocket& socket, int status, const juce::String& reason, const juce::String& body, const juce::String& extraHeaders = {})
{
    const auto payload = body.toStdString();
    juce::String head;
    head << "HTTP/1.1 " << status << " " << reason << "\r\n"
         << "Content-Type: application/json\r\n"
         << "Content-Length: " << int(payload.size()) << "\r\n"
         << extraHeaders << "Connection: close\r\n\r\n";
    const auto headText = head.toStdString();
    socket.write(headText.data(), int(headText.size()));
    if (!payload.empty())
        socket.write(payload.data(), int(payload.size()));
}
}

McpServer::McpServer(Automation& a, ScriptEngine& s) : Thread("TStudio MCP"), automation(a), scripts(s) {}

McpServer::~McpServer()
{
    *alive = false;
    stop();
}

bool McpServer::start(int port, juce::String& error)
{
    stop();
    if (!listener.createListener(port, "127.0.0.1"))
    {
        error = "could not listen on 127.0.0.1:" + juce::String(port) + " (is the port in use?)";
        return false;
    }
    listeningPort = port;
    startThread();
    return true;
}

void McpServer::stop()
{
    signalThreadShouldExit();
    listener.close();
    stopThread(4000);
    listeningPort = 0;
}

void McpServer::run()
{
    while (!threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> client(listener.waitForNextConnection());
        if (client == nullptr)
        {
            if (!listener.isConnected())
                return;
            continue;
        }
        serve(*client);
    }
}

void McpServer::serve(juce::StreamingSocket& socket)
{
    std::string data;
    char buffer[8192];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos && data.size() < 65536)
    {
        if (socket.waitUntilReady(true, 5000) != 1)
            return;
        const auto count = socket.read(buffer, int(sizeof(buffer)), false);
        if (count <= 0)
            return;
        data.append(buffer, size_t(count));
        headerEnd = data.find("\r\n\r\n");
    }
    if (headerEnd == std::string::npos)
        return;

    const auto lines = juce::StringArray::fromLines(juce::String::fromUTF8(data.data(), int(headerEnd)));
    const auto requestLine = juce::StringArray::fromTokens(lines[0], " ", {});
    juce::StringPairArray headers;
    for (int i = 1; i < lines.size(); ++i)
        headers.set(lines[i].upToFirstOccurrenceOf(":", false, false).trim().toLowerCase(), lines[i].fromFirstOccurrenceOf(":", false, false).trim());

    const auto length = headers["content-length"].getIntValue();
    if (length < 0 || length > maxBody)
        return reply(socket, 413, "Payload Too Large", {});
    auto body = data.substr(headerEnd + 4);
    while (int(body.size()) < length)
    {
        if (socket.waitUntilReady(true, 5000) != 1)
            return;
        const auto count = socket.read(buffer, int(sizeof(buffer)), false);
        if (count <= 0)
            return;
        body.append(buffer, size_t(count));
    }

    if (!allowedOrigin(headers["origin"]))
        return reply(socket, 403, "Forbidden", R"({"error":"origin not allowed"})");
    if (!requestLine[1].startsWith("/mcp"))
        return reply(socket, 404, "Not Found", R"({"error":"use the /mcp endpoint"})");
    if (requestLine[0] != "POST")
        return reply(socket, 405, "Method Not Allowed", {}, "Allow: POST\r\n");

    juce::var request;
    if (const auto parsed = juce::JSON::parse(juce::String::fromUTF8(body.data(), length), request); parsed.failed() || !request.isObject())
        return reply(socket, 400, "Bad Request", juce::JSON::toString(rpcError({}, -32700, "parse error"), true));

    const auto response = handleOnMessageThread(request);
    if (response.isVoid())
        return reply(socket, 202, "Accepted", {});
    reply(socket, 200, "OK", juce::JSON::toString(response, true));
}

juce::var McpServer::handleOnMessageThread(const juce::var& request)
{
    struct Pending
    {
        juce::WaitableEvent done;
        juce::var response;
    };
    auto pending = std::make_shared<Pending>();
    juce::MessageManager::callAsync([this, pending, request, guard = alive]
    {
        if (!*guard)
            return;
        pending->response = handle(request);
        pending->done.signal();
    });
    while (!pending->done.wait(50))
        if (threadShouldExit())
            return rpcError(request.getProperty("id", {}), -32000, "TStudio is shutting down");
    return pending->response;
}

juce::var McpServer::handle(const juce::var& request)
{
    const auto method = request.getProperty("method", {}).toString();
    const auto params = request.getProperty("params", {});
    if (!request.hasProperty("id"))
        return {};
    const auto id = request.getProperty("id", {});

    if (method == "initialize")
    {
        const auto requested = params.getProperty("protocolVersion", latestProtocol).toString();
        return rpcResult(id, object({ { "protocolVersion", requested.isNotEmpty() ? requested : juce::String(latestProtocol) },
                                      { "capabilities", object({ { "tools", object({ { "listChanged", false } }) } }) },
                                      { "serverInfo", object({ { "name", "TStudio" }, { "version", JUCE_APPLICATION_VERSION_STRING } }) },
                                      { "instructions", "Controls the open TStudio project: tempo, channels, notes (96 ticks per beat, key 60 = C5), patterns, "
                                                        "playlist, mixer, rendering and files. Call get_project first to see what is there. run_lua runs a Lua "
                                                        "script with the full ts.* API for multi-step edits." } }));
    }
    if (method == "ping")
        return rpcResult(id, object({}));
    if (method == "tools/list")
        return rpcResult(id, object({ { "tools", tools() } }));
    if (method == "tools/call")
        return rpcResult(id, callTool(params.getProperty("name", {}).toString(), params.getProperty("arguments", {})));
    return rpcError(id, -32601, "method not found: " + method);
}

juce::var McpServer::tools() const
{
    juce::Array<juce::var> list;
    for (const auto& method : automation.methods())
        list.add(object({ { "name", method.name }, { "description", method.description }, { "inputSchema", method.schema } }));
    auto* code = new juce::DynamicObject();
    code->setProperty("type", "string");
    code->setProperty("description", "Lua source. Use print() to return text.");
    auto* properties = new juce::DynamicObject();
    properties->setProperty("code", juce::var(code));
    list.add(object({ { "name", "run_lua" },
                      { "description", "Runs a Lua script inside TStudio with the ts.* scripting API (ts.add_note, ts.chord, ts.scale_degree, ts.call...). "
                                       "Returns everything the script printed." },
                      { "inputSchema", object({ { "type", "object" }, { "properties", juce::var(properties) }, { "required", juce::Array<juce::var> { "code" } } }) } }));
    return list;
}

juce::var McpServer::callTool(const juce::String& name, const juce::var& arguments)
{
    if (name == "run_lua")
    {
        const auto result = scripts.run(arguments.getProperty("code", {}).toString(), "mcp");
        return textContent(result.ok ? (result.output.isNotEmpty() ? result.output : juce::String("ok")) : result.output + result.error, !result.ok);
    }
    const auto result = automation.call(name, arguments);
    return textContent(juce::JSON::toString(result, true), result.hasProperty("error"));
}
}
