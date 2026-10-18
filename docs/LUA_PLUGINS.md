# Writing Lua plugins

A Lua plugin is one `.lua` file that defines a global `plugin` table plus a few functions. Put it in `Documents/TStudio/Plugins/` and it appears in TStudio's instrument or effect lists. It also loads in any VST3 host through the **TStudio Lua FX** and **TStudio Lua Synth** loaders, where the host's preset menu switches between scripts.

The nine bundled plugins in `Plugins/` are worked examples. Read them alongside this page:

| Plugin | Type | What it shows |
| --- | --- | --- |
| `plugin_chip.lua` | instrument | Square wave voice |
| `plugin_kick.lua` | instrument | Pitch-swept kick |
| `plugin_fmbell.lua` | instrument | FM synthesis |
| `plugin_tremolo.lua` | effect | Custom UI that draws the LFO |
| `plugin_tape.lua` | effect | Spinning reels and a transfer curve |
| `plugin_vinyl.lua` | effect | Turntable UI |
| `plugin_haas.lua` | effect | Haas stereo widener |
| `plugin_ringmod.lua` | effect | Ring modulator |
| `plugin_stutter.lua` | effect | Tempo-synced stutter |

## Minimal effect

```lua
plugin = {
  name = "My Gain",
  type = "effect",                 -- or "instrument"
  author = "you",
  description = "Shown under the editor",
  params = {
    { id = "gain", name = "Gain", min = -24, max = 24, default = 0, unit = "dB" },
    { id = "mode", name = "Mode", choices = { "Clean", "Warm" }, default = 0 },
  },
  presets = {
    { name = "Loud", gain = 6, mode = 1 },
  },
}

local g = 1
function block(frames)              -- once per audio block: do per-block maths here
  g = db_to_gain(params.gain)
end

function process(l, r)              -- once per sample; return the new left and right
  if params.mode == 1 then return soft_clip(l * g), soft_clip(r * g) end
  return l * g, r * g
end
```

## Minimal instrument

```lua
plugin = { name = "My Sine", type = "instrument", params = { { id = "volume", name = "Volume", default = 0.5 } } }

local phase, step, level = 0, 0, 0
function note_on(key, velocity) step = note_freq(key) / sample_rate; level = velocity end
function note_off(key) level = 0 end
function render()                   -- once per sample; return left and right
  phase = (phase + step) % 1
  local s = math.sin(TWO_PI * phase) * level * params.volume
  return s, s
end
```

## Reference

**Callbacks.** All are optional except `process` (effects) or `render` (instruments).

| Function | Called |
| --- | --- |
| `process(l, r)` → `l, r` | per sample, effects |
| `render()` → `l, r` | per sample, instruments |
| `note_on(key, velocity)`, `note_off(key, velocity)` | sample-accurately, before that sample's `render` |
| `block(frames)` | at the start of every audio block |
| `prepare(sample_rate, block_size)` | when playback starts or the rate changes |

**Globals the host keeps up to date:**

| Global | Value |
| --- | --- |
| `params` | real parameter values, keyed by id; choices are 0-based indexes |
| `sample_rate` | the current sample rate |
| `tempo` | project bpm |
| `beat` | song position in beats at the start of the block |
| `meters` | a table you write values into (see Meters) |

**Helpers:**

| Helper | Does |
| --- | --- |
| `TWO_PI` | 2π |
| `note_freq(key)` | Hz for a MIDI key (69 = 440 Hz) |
| `clamp(x, lo, hi)` | limits `x` to the range |
| `lerp(a, b, t)` | linear interpolation |
| `db_to_gain(db)`, `gain_to_db(g)` | convert between dB and linear gain |
| `soft_clip(x)` | gentle saturation |

**Libraries:** base, `table`, `string` and `math`. There is no `io`, `os` or `load`.

**Parameter fields:**

| Field | Meaning |
| --- | --- |
| `id` | key used in `params` |
| `name` | label in the UI |
| `min`, `max` | range (default 0 and 1) |
| `default` | starting value |
| `unit` | shown after the value |
| `choices` | a list of strings; turns the parameter into a menu |

In the VST3 loader the first 16 parameters are exposed to the host.

**Safety.**

- Output is clamped to ±4, and NaN or infinite samples become silence.
- If a callback raises an error, the plugin stops and the editor shows the message in its status line.

**Performance.** Lua runs per sample, so keep `process` and `render` small:

- Do per-block maths in `block`.
- Cache library functions in locals (`local sin = math.sin`).
- Avoid creating tables per sample.

The bundled plugins run many times faster than realtime.

## Meters

To show signal levels in your UI, declare meter names and write numbers into `meters` from `block` (or anywhere on the audio side). The UI reads the latest values about 30 times a second:

```lua
plugin = { ..., meters = { "level" } }
local peak = 0
function block() meters.level = peak; peak = 0 end
function process(l, r) local a = math.abs(l); if a > peak then peak = a end return l, r end
```

## Custom UIs

Add a `ui` table to replace the default knob panel. You can lay out ready-made **widgets**, draw anything yourself with **`draw`**, or both.

```lua
plugin = {
  ...,
  ui = {
    width = 560, height = 220,              -- editor size in pixels
    background = "#1d1b1a", accent = "#e8483b",
    widgets = {
      { type = "knob",   param = "gain", x = 20, y = 20, w = 76, h = 96 },
      { type = "choice", param = "mode", x = 110, y = 40, w = 120, h = 24 },
      { type = "meter",  meter = "level", x = 20, y = 130, w = 220, h = 10 },
      { type = "label",  text = "MY GAIN", x = 250, y = 20, w = 200, h = 24, size = 18, bold = true },
    },
  },
}
```

**Widgets.** Every widget takes `x`, `y`, `w`, `h`, plus `color = "#rrggbb"` to override the accent.

| Type | Fields | Behaviour |
| --- | --- | --- |
| `knob` | `param`, `label`, `value` (show the readout, default true) | Rotary knob; double-click resets |
| `slider` / `hslider` | `param`, `label` | Vertical or horizontal fader |
| `toggle` | `param`, `label` | On/off button: on = parameter max |
| `choice` | `param` | Drop-down for a `choices` parameter |
| `xy` | `x_param`, `y_param` | Pad that sets two parameters |
| `meter` | `meter` | Bar showing a meter value (0 to 1) |
| `label` | `text`, `size`, `bold`, `align` (`left`/`center`/`right`) | Static text |
| `scope` | none | Live oscilloscope (TStudio only, not the VST3 loader) |

In TStudio, parameters that a widget controls are hidden from the default knob panel, and any you leave out still appear below your UI. The VST3 loader shows only your UI.

### Drawing with `gfx`

If the script defines `draw(w, h)`, TStudio calls it about 30 times a second. Inside it, `params` and `meters` hold the current values. You can also handle the mouse:

```lua
function draw(w, h)
  gfx.color("#121618"); gfx.fill_rect(10, 10, w - 20, h - 20, 6)
  gfx.color("#e8483b"); gfx.knob(30, 30, 64, (params.gain + 24) / 48)
  gfx.color(255, 255, 255, 200); gfx.text("GAIN", 30, 100, 64, 16, 12, "center", true)
end
function mouse_drag(x, y) set_param("gain", clamp((y - 100) / -4, -24, 24)) end
```

| Call | Draws |
| --- | --- |
| `gfx.color("#rrggbb")` or `gfx.color(r, g, b, a)` | sets the colour |
| `gfx.fill_rect(x, y, w, h, radius)` | filled (rounded) rectangle |
| `gfx.rect(x, y, w, h, thickness, radius)` | rectangle outline |
| `gfx.fill_circle(cx, cy, r)` | filled circle |
| `gfx.circle(cx, cy, r, thickness)` | circle outline |
| `gfx.line(x1, y1, x2, y2, thickness)` | line |
| `gfx.arc(cx, cy, r, from, to, thickness)` | arc; angles in radians, 0 at 12 o'clock, clockwise |
| `gfx.polyline({ x1, y1, x2, y2, ... }, thickness)` | connected lines (waveforms, curves) |
| `gfx.fill_poly({ x1, y1, ... })` | filled polygon |
| `gfx.gradient(x1, y1, "#c1", x2, y2, "#c2")` | sets a linear gradient as the fill |
| `gfx.text(str, x, y, w, h, size, align, bold)` | text in a box |
| `gfx.knob(x, y, size, value01)` | TStudio-style knob |

**Mouse callbacks:** `mouse_down(x, y)`, `mouse_drag(x, y)`, `mouse_up(x, y)` and `mouse_wheel(x, y, delta)`. Use `set_param(id, value)` to change a parameter (the host records it as automation) and `get_param(id)` to read one.

**The UI runs in its own Lua state.** TStudio loads your file a second time for the editor, so audio-side locals are not visible to `draw`. Share state through `params` (UI → audio) and `meters` (audio → UI). Top-level code runs in both states, so keep it free of side effects. If `draw` errors, the editor shows the message at the bottom and the audio keeps running.
