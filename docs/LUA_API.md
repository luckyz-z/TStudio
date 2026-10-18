# Lua scripting API

Scripts run inside TStudio on the UI thread, with full access to the open project. Run them three ways:

- **Tools > Script console** (Ctrl+Shift+L), then Ctrl+Enter to run.
- **Tools > Run script** for the built-ins and anything in `Documents/TStudio/Scripts/`.
- **The command line:** `TStudio.exe --lua myscript.lua`.

The sandbox has `base`, `table`, `string`, `math`, `utf8` and `coroutine`. There is no `io`, `os` or `load`. A script that runs longer than 10 seconds is stopped.

## Units

| Constant | Value | Meaning |
| --- | --- | --- |
| `ts.PPQ`, `ts.BEAT` | 96 | ticks per beat |
| `ts.STEP` | 24 | one step (1/16 note) |
| `ts.BAR` | 384 | one 4/4 bar |

- Keys are MIDI note numbers, with **60 = C5** (FL Studio naming).
- Velocity runs from 0 to 1.
- Pattern indexes are 0-based. Channel ids come from `ts.channels()`.

## Project and transport

```lua
local p = ts.project()      -- { title, tempo, swing, song_mode, playing, position, selected_channel,
                            --   current_pattern, channels = {...}, patterns = {...}, clips = {...} }
ts.tempo(140)               -- set and return the tempo
ts.swing(0.3)
ts.play(); ts.stop()
ts.song_mode(true)
```

## Channels

```lua
for _, c in ipairs(ts.channels()) do print(c.id, c.name, c.type, c.volume, c.pan, c.muted, c.insert) end
local lead = ts.add_channel("Tomato Synth", "Lead")   -- also "pack:Kick", "plugin:Serum", "Lua Chip", a sample path...
ts.update_channel(lead, { volume = 0.8, pan = -0.2, insert = 3, name = "Lead L" })
ts.remove_channel(lead)
ts.selected_channel()
```

Channel settings (FL's Misc tab) go through `update_channel` too. `ts.channels()[i].settings` reads them back.

```lua
ts.update_channel(openHat, { cut_by = 1 })            -- choked by...
ts.update_channel(closedHat, { cut_group = 1 })       -- ...this channel
ts.update_channel(lead, { mono = true, porta = true, porta_time = 90 })
ts.update_channel(keys, { arp = 1, arp_time = 1, arp_range = 2, arp_gate = 0.5 })   -- arp: 1 up, 2 down, 3 up-down, 4 random; time: 0=1/32 1=1/16 2=1/8 3=1/4
ts.update_channel(hats, { time_shift = 4, swing_mix = 0.5 })                           -- shift in ticks (24 = one step)
```

## Notes

Notes look like `{ start = ticks, length = ticks, key = 60, velocity = 0.8 }`. Optional flags:
- `slide = true` glides the note that is already playing up or down to this key over this note's length, like FL's slide notes. Good for 808s.
- `porta = true` glides into this note from the previous one.
- `muted = true` keeps the note but doesn't play it.

```lua
local notes = ts.notes(channel)            -- current pattern; pass a pattern index as the 2nd argument for another
ts.set_notes(channel, notes)               -- replace
ts.add_notes(channel, { { start = 0, length = ts.STEP, key = 60 } })
ts.add_note(channel, start, length, key, velocity)
ts.clear(channel)
```

## Patterns, playlist and mixer

```lua
local index = ts.new_pattern("Hook")
ts.select_pattern(index)
ts.current_pattern()
ts.add_clip(index, 0, 8, 4)               -- pattern, playlist track, start bar, length in bars
ts.mixer(3, { volume = 0.7, pan = 0.1, muted = false, name = "Lead bus", width = 1.4, invert_phase = false, swap = false })   -- track 0 = master
ts.add_effect(3, 0, "Tomato Reverb")      -- track, slot 0-9, effect name ("" clears the slot)
ts.call("set_loop", { start_bar = 8, end_bar = 16 })   -- song-mode loop region; call with no arguments to clear it
```

### Routing and sidechain

```lua
ts.route(2, 10, { only = true })                -- insert 2 now goes only to insert 10 (a bus)
ts.route(5, 10, { level = 0.4 })                -- extra send at 40%
ts.route(1, 2, { sidechain = true })            -- kick (insert 1) feeds insert 2's sidechain input
ts.add_effect(2, 0, "Tomato Compressor")
ts.set_preset({ track = 2, slot = 0 }, "Sidechain duck")
ts.route(1, 0, { remove = true })               -- remove the kick's master send (silent sidechain source)
for _, t in ipairs(ts.call("get_mixer")) do print(t.track, t.name, #t.sends) end
```

Routing loops are refused with an error.

### Automation clips

Values are normalised 0-1. Ticks are relative to the clip start. Automation plays in song mode.

```lua
ts.automate({ kind = "parameter", channel = lead, parameter = "cutoff" }, { { tick = 0, value = 0.1 }, { tick = 8 * ts.BAR, value = 0.9 } })
ts.automate({ kind = "mixer_volume", track = 3 }, { { tick = 0, value = 0 }, { tick = 4 * ts.BAR, value = 0.8, tension = 0.5 } }, { start_bar = 16 })
ts.automate({ kind = "tempo" }, { { tick = 0, value = 0.5 }, { tick = 16 * ts.BAR, value = 0.6 } })   -- tempo maps 0-1 to 60-200 BPM
```

| `kind` | Target fields |
| --- | --- |
| `parameter` | `channel`, or `track` + `slot`, plus `parameter` (an id from `list_parameters`) |
| `channel_volume`, `channel_pan` | `channel` |
| `mixer_volume`, `mixer_pan` | `track` |
| `tempo` | none |
| `main_volume` | none |

`ts.call("list_automations")` and `ts.call("set_automation_points", { index = i, points = {...} })` edit existing clips.

## Parameters and presets

The target is `{ channel = id }` for an instrument, or `{ track = t, slot = s }` for a mixer effect.

```lua
for _, p in ipairs(ts.call("list_parameters", { channel = lead })) do print(p.id, p.name, p.value, p.text) end
ts.set_param({ channel = lead }, "cutoff", 1200)
print(table.concat(ts.presets({ channel = lead }).presets, ", "))
ts.set_preset({ channel = lead }, "Rage lead")   -- by name or index
```

## Files

```lua
ts.render("C:/Music/song.mp3")      -- WAV, FLAC, OGG or MP3 (320 kbps); relative paths resolve against the working folder
ts.save("C:/Music/song.tsp")
ts.call("open_project", { path = "beat.flp" })
ts.call("export_midi", { path = "hook.mid", channel = lead })
ts.call("import_midi", { path = "melody.mid", channel = lead })
```

## Music helpers

```lua
ts.note("C5")                    --> 60   (also "A#3", "Eb4")
ts.chord("A4", "min7")           --> { 57, 60, 64, 67 }
ts.scale_degree("A4", "minor", 3)
ts.scales, ts.chords             -- the tables behind them, editable
```

- **Scales:** major, minor, dorian, phrygian, harmonic_minor, pentatonic, minor_pentatonic, blues.
- **Chords:** major, minor, maj7, min7, dom7, sus2, sus4, dim, min9, add9.

## Everything else: `ts.call`

Every method of the automation API is reachable with `ts.call(name, arguments)`. It is the same API the MCP server exposes; [docs/MCP.md](MCP.md) has the full list. On failure, `ts.call` raises a Lua error, so wrap it in `pcall` to handle the failure yourself.

## Extensions

An extension is a Lua file that stays loaded. TStudio runs every file in `Documents/TStudio/Extensions/` at startup, plus the bundled ones. Reload them with **Tools > Extensions > Reload extensions**. Extensions use the whole API above, plus the calls below.

```lua
ts.menu("Notes: humanize hard", function()            -- Tools > Extensions > ...
  local ch = ts.selected_channel()
  local notes = ts.notes(ch)
  for _, n in ipairs(notes) do n.start = math.max(0, n.start + math.random(-6, 6)) end
  ts.set_notes(ch, notes)
end)

ts.shortcut("Ctrl+Alt+T", function() ts.tempo(ts.tempo() + 1) end)   -- runs before built-in shortcuts

ts.on("play", function() print("rolling") end)
```

**Events** passed to `ts.on`:

| Event | Argument |
| --- | --- |
| `play`, `stop` | none |
| `pattern` | pattern index |
| `channel` | channel id |
| `tempo` | new bpm |
| `load` | file path, or `demo:Name` |
| `save` | file path |

### Panels

`ts.panel` creates a floating window drawn in Lua, using the same `gfx` API as [Lua plugin UIs](LUA_PLUGINS.md#drawing-with-gfx).

```lua
local id = ts.panel({
  title = "Hello", width = 300, height = 120, fps = 10,
  draw = function(w, h)
    gfx.color("#e8483b")
    gfx.text(string.format("%.1f BPM", ts.tempo()), 0, 0, w, h, 32, "center", true)
  end,
  mouse_down = function(x, y) ts.tempo(ts.tempo() + 1) end,
})
ts.menu("Show hello panel", function() ts.show_panel(id) end)
```

- **Callbacks:** `draw(w, h)`, `mouse_down(x, y)`, `mouse_drag(x, y)`, `mouse_up(x, y)` and `mouse_wheel(x, y, delta)`.
- **Spec fields:** `fps` (default 30), `background` and `accent`.

For complete examples, see the bundled `Extensions/ext_note_tools.lua` and `Extensions/ext_dashboard.lua`.
