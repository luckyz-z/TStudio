# Architecture

TStudio is one C++20 / JUCE 8 application plus two small VST3 "loader" plugins that share its Lua plugin runtime.

```
                 ┌──────────── UI (message thread) ─────────────┐
 keyboard/mouse →│ MainComponent: menus, commands, shortcuts     │
                 │ Playlist · ChannelRack · PianoRoll · Mixer    │
                 │ DeviceWindows · Extensions · ScriptConsole    │
                 └───────┬───────────────────────────┬──────────┘
                         │ edits under project.lock  │ scripts / MCP
                 ┌───────▼──────┐   ┌───────────────┐ ┌▼───────────────┐
                 │ Studio       │◄──┤ Automation    │◄┤ ScriptEngine   │◄─ Lua scripts, extensions
                 │ (facade)     │   │ (JSON methods)│ │ McpServer      │◄─ AI clients over HTTP
                 └───────┬──────┘   └───────────────┘ └────────────────┘
                         │ owns
     ┌───────────────────┼───────────────────────┬────────────────────┐
┌────▼─────┐   ┌─────────▼─────────┐   ┌─────────▼───────┐   ┌────────▼────────┐
│ Project  │   │ AudioEngine       │   │ PluginHost      │   │ File formats    │
│ patterns │◄──┤ (audio thread)    │   │ VST3 scanning   │   │ .tsp XML, .flp, │
│ channels │   │ sequencing, mixer │   │ and loading     │   │ MIDI, render    │
│ mixer    │   └───────────────────┘   └─────────────────┘   └─────────────────┘
└──────────┘
```

## Threading

- The **message thread** owns all edits. Code that changes notes, clips, channels or mixer slots takes `project.lock` (a `CriticalSection`), makes the change, then calls `project.touch()` (marks the project dirty and broadcasts the change).
- The **audio thread** (`AudioEngine::getNextAudioBlock`) takes the same lock for the block, then:
  1. sequences notes sample-accurately (note-offs first, then note-ons)
  2. renders channels into their mixer inserts
  3. runs the insert effects
  4. sums into the master
- Transport changes, panic and position jumps are atomics that the audio thread applies at the start of the next block (`applyRequests`).
- Mixer inserts render in dependency order (`Project::mixerOrder`, rebuilt whenever routing changes; routes that would form a loop are refused). Each insert has a send map: Master by default, plus buses at send levels, plus sidechain-only routes. Sidechain-only routes fill the destination's sidechain buffer, which reaches plugins as their second input bus.
- Automation clips are evaluated once per block in song mode (`AudioEngine::applyAutomation`), before sequencing. They write parameters through the normal parameter API, so editors and hosts stay in sync.
- Channel settings (cut groups, time shift, swing mix, mono, porta, arpeggiator) and note slides are applied while notes are emitted, so they work with any instrument. Glides use the MIDI portamento messages CC84 (source note) and CC5 (time).
- Recording goes the other way. The audio thread pushes notes into a lock-free `AbstractFifo`, and the UI reads them on a timer.

## Devices

- A `Device` wraps any `juce::AudioProcessor`: a built-in, a Lua plugin, a native C plugin or a VST3. It adds bypass, wet/dry mix and a scratch buffer.
- Built-ins derive from `BuiltinProcessor`, which provides:
  - parameter helpers and presets
  - state save and load
  - a scope tap for the editor displays
  - `createDisplay()`, which supplies the dashboard shown above the knobs
- `Builtins::create(name)` tries, in order: built-in instruments, built-in effects, Lua plugins (`LuaPlugins`), native DLL plugins (`NativePlugins`).

## Lua

| Use | Where it runs | Notes |
| --- | --- | --- |
| **Scripts and extensions** | `ScriptEngine`, one persistent `lua_State` on the message thread | The `ts.*` API is a thin Lua prelude over `Automation::call`. Extensions register menus, shortcuts, events and panels in tables the C++ side reads. |
| **Plugins** | `Shared/LuaPluginScript`, one `lua_State` per instance on the audio thread | Calls `process` or `render` per sample inside one protected call per block. Uses the generational GC. |
| **Plugin UIs** | `Shared/LuaPluginUI`, a second `lua_State` on the message thread | The UI and audio states share only `params` (UI to audio) and `meters` (audio to UI, through atomics). |

`Shared/` is compiled into both the app and the loader VST3s (`Loader/LuaLoader.cpp`), so a Lua plugin behaves the same everywhere.

## Automation, scripting and MCP

`Automation` is a table of named methods with JSON schemas, such as `get_project` and `add_notes`. Three front ends use it:

- the Lua `ts.call` function, plus the friendlier `ts.*` helpers built on it
- the MCP server, which turns each method into a tool and adds `run_lua`
- the integration tests, through `--lua`

To add a feature for all three, add one method in `Automation.cpp`.

## Files

| Format | Notes |
| --- | --- |
| `.tsp` | XML: patterns, notes, channels with device state (base64), mixer, playlist. The loader also accepts the legacy `.lfs` root tag. |
| `.flp` | Read-only import. Channels, notes, patterns, playlist, mixer routing, and native FL effects mapped to Tomato equivalents. |
| MIDI | Import into a channel; export a pattern or one channel. |
| Rendering | `OfflineSession` renders the song faster than realtime to WAV, FLAC or OGG. |
