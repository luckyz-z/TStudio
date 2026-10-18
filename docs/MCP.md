# Controlling TStudio from AI assistants (MCP)

TStudio has a built-in [Model Context Protocol](https://modelcontextprotocol.io) server. It lets an assistant such as Claude read and edit the open project:

- write drum patterns and chord progressions
- set up mixer chains and load presets
- render the song

## Turn it on

Click **Tools > MCP server**. The setting is remembered between launches. The server:

- listens only on `http://127.0.0.1:7341/mcp`, using the Streamable HTTP transport with JSON responses
- rejects requests whose `Origin` header is not a local page, so websites in your browser cannot reach it
- is off by default

For one session from the command line, run `TStudio.exe --mcp`.

## Connect a client

**Claude Code:**

```bash
claude mcp add --transport http tstudio http://127.0.0.1:7341/mcp
```

**Clients that only launch stdio servers** (Claude Desktop, for example): use the bundled bridge. It needs only Python, with no extra packages.

```json
{
  "mcpServers": {
    "tstudio": { "command": "python", "args": ["C:/path/to/TStudio/tools/mcp_stdio_bridge.py"] }
  }
}
```

## Tools

| Tool | What it does |
| --- | --- |
| `get_project` | Tempo, transport, channels, patterns, playlist clips. |
| `set_tempo`, `set_swing`, `transport`, `set_song_mode` | Transport and timing. |
| `list_instruments`, `add_channel`, `remove_channel`, `update_channel`, `select_channel` | Channels. |
| `list_parameters`, `set_parameter`, `list_presets`, `set_preset` | Instrument and effect settings. |
| `get_notes`, `set_notes`, `add_notes`, `clear_notes` | Notes (96 ticks per beat, key 60 = C5). |
| `new_pattern`, `select_pattern`, `rename_pattern`, `add_clip`, `clear_playlist` | Patterns and arrangement. |
| `get_mixer`, `set_mixer_track`, `route_mixer_track`, `list_effects`, `add_effect` | Mixer, including sends, sidechain routing, stereo width, phase and swap. |
| `create_automation`, `list_automations`, `set_automation_points` | Automation clips for any parameter, volume, pan or tempo. |
| `set_loop` | Song-mode loop region. |
| `render`, `save_project`, `open_project`, `export_midi`, `import_midi` | Files (render writes WAV, FLAC, OGG or MP3). |
| `run_lua` | Runs a Lua script with the whole [scripting API](LUA_API.md) and returns what it printed. Good for multi-step edits in one call. |

Every tool publishes a JSON schema, so clients know the arguments without extra docs.

## Test it

```bash
python tools/mcp_smoke_test.py
```

The script initializes a session, lists the tools, changes the tempo, runs Lua, and checks that a foreign origin is rejected.

## Notes

- Tools run on TStudio's UI thread, the same as a click in the app. Undo (Ctrl+Z) works for note and pattern edits.
- `run_lua` uses the same sandbox as the script console: no file system or OS access.
