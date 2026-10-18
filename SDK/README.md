# TStudio native plugin SDK (C API)

Native plugins are plain DLLs, written in C, C++, Rust, Zig or anything else that can export a C function. A DLL can contain any number of instruments and effects. TStudio builds the editor for you: knobs for numeric parameters and menus for choice parameters, plus an oscilloscope and a spectrum analyser.

## Where TStudio looks

TStudio loads every `.dll` in two folders at startup:

- `Documents/TStudio/NativePlugins/`
- the `Plugins/` folder next to `TStudio.exe`

## The contract

Include [`tstudio_plugin.h`](tstudio_plugin.h) and export one function:

```c
TSTUDIO_EXPORT const tstudio_plugin* const* tstudio_plugins(int* count);
```

Each `tstudio_plugin` describes one plugin:

| Field | Meaning |
| --- | --- |
| `api_version` | Always `TSTUDIO_PLUGIN_API_VERSION`. Plugins built for another version are skipped. |
| `name`, `author`, `description` | Shown in TStudio. `name` must be unique. |
| `is_instrument` | 1 for an instrument (receives notes, output starts silent), 0 for an effect. |
| `num_params`, `params` | Parameter list (see below). |
| `create(sample_rate, max_block)` | Allocate an instance. Called whenever playback is prepared. |
| `destroy(instance)` | Free it. |
| `set_param(instance, index, value)` | A parameter changed. `value` is the real value (not 0 to 1). It is called on the audio thread right before `process`, so no locking is needed. |
| `process(instance, left, right, frames, events, num_events, transport)` | Process in place. Instruments write into the zeroed buffers. `events` are note on/off events sorted by `offset`, the sample index within the block. `transport` has `bpm`, `beat` and `playing`. |

Each `tstudio_param` describes one parameter:

| Field | Meaning |
| --- | --- |
| `id` | Stable identifier, saved with projects. |
| `name` | Label in the editor. |
| `unit` | Shown after the value. |
| `min`, `max` | Range. |
| `default_value` | Starting value. |
| `num_choices`, `choices` | Optional list of choice names. With a list, the value is a 0-based index and the editor shows a menu. |

## Rules

- `process` runs on the real-time audio thread. Don't allocate memory, take locks or do file I/O there.
- Every string and table you return must stay valid while the DLL is loaded. Static data is the simplest way.
- TStudio replaces non-finite samples with silence and clamps output to ±4. Don't rely on that.
- Don't change existing `id`s after release, or saved projects lose their settings.

## Example

[`examples/tomato_native.c`](examples/tomato_native.c) holds two plugins, **Native Crusher** (bit and rate reduction) and **Native Sub** (a sine sub bass with drive). The main CMake build compiles it as `TomatoNative.dll` (`TomatoNative.so` on Linux) and copies it to the `Plugins/` folder next to TStudio. To build it on its own:

```bash
cl /LD /O2 /I SDK SDK/examples/tomato_native.c /Fe:TomatoNative.dll
```

```bash
gcc -shared -O2 -fPIC -ISDK SDK/examples/tomato_native.c -o TomatoNative.so -lm
```

## Lua or C?

- Use **Lua** for quick ideas, custom drawn UIs, and plugins that also run in other DAWs through the loader VST3. See [docs/LUA_PLUGINS.md](../docs/LUA_PLUGINS.md).
- Use **C** for heavy DSP that has to run many voices or very cheaply.
