import json
import sys
import time
import urllib.error
import urllib.request

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7341/mcp"


def post(payload, origin=None):
    request = urllib.request.Request(URL, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
    if origin:
        request.add_header("Origin", origin)
    with urllib.request.urlopen(request, timeout=20) as response:
        body = response.read().decode()
        return response.status, json.loads(body) if body else None


def wait_for_server():
    for _ in range(60):
        try:
            return post({"jsonrpc": "2.0", "id": 0, "method": "ping"})
        except (urllib.error.URLError, ConnectionError):
            time.sleep(0.5)
    raise SystemExit("server did not come up")


def call(tool, arguments, ident):
    status, reply = post({"jsonrpc": "2.0", "id": ident, "method": "tools/call", "params": {"name": tool, "arguments": arguments}})
    result = reply["result"]
    assert status == 200 and not result["isError"], result
    return result["content"][0]["text"]


wait_for_server()
status, reply = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "smoke", "version": "1"}}})
assert reply["result"]["serverInfo"]["name"] == "TStudio", reply
status, _ = post({"jsonrpc": "2.0", "method": "notifications/initialized"})
assert status == 202, status
_, reply = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
names = [tool["name"] for tool in reply["result"]["tools"]]
assert "get_project" in names and "run_lua" in names, names
project = json.loads(call("get_project", {}, 3))
call("set_tempo", {"bpm": 142}, 4)
assert json.loads(call("get_project", {}, 5))["tempo"] == 142
lua = call("run_lua", {"code": "print(ts.tempo(), #ts.channels())"}, 6)
assert lua.startswith("142"), lua
_, reply = post({"jsonrpc": "2.0", "id": 7, "method": "nope"})
assert reply["error"]["code"] == -32601, reply
try:
    post({"jsonrpc": "2.0", "id": 8, "method": "ping"}, origin="http://evil.example")
    raise SystemExit("foreign origin was accepted")
except urllib.error.HTTPError as error:
    assert error.code == 403, error.code
print(f"MCP OK: {len(names)} tools, project '{project.get('title')}' with {len(project['channels'])} channels, lua -> {lua.strip()}")
