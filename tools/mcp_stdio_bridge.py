import json
import sys
import urllib.error
import urllib.request

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7341/mcp"


def forward(line):
    message = json.loads(line)
    request = urllib.request.Request(URL, data=line.encode(), headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            body = response.read().decode()
    except (urllib.error.URLError, ConnectionError) as error:
        if "id" not in message:
            return None
        body = json.dumps({"jsonrpc": "2.0", "id": message["id"], "error": {"code": -32000, "message": f"TStudio is not reachable at {URL}: {error}. Open TStudio and enable Tools > MCP server."}})
    return body or None


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    reply = forward(line)
    if reply:
        sys.stdout.write(json.dumps(json.loads(reply)) + "\n")
        sys.stdout.flush()
