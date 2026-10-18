# TStudio roadmap

This is where TStudio is heading and where you can help. Almost everything here can be built by the community. Pick something, say so on its issue (or open one), and go.

## How to pick something

| Tag | Meaning |
| --- | --- |
| 🟢 **good first** | Small and self-contained. A good first pull request. |
| 🟡 **medium** | A few days of work. Touches one area of the code. |
| 🔴 **big** | A project. Talk it through in an issue or [Discussions](https://github.com/Luckyz-z/TStudio/discussions) first. |
| 🧩 **no C++** | Can be done in Lua, Python, docs or art. |

1. Check the [issues](https://github.com/Luckyz-z/TStudio/issues) for one that already covers it. If none does, open a feature request that names the roadmap item.
2. Comment that you're working on it, so two people don't build the same thing.
3. Read [CONTRIBUTING.md](CONTRIBUTING.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), then open a draft pull request early. Feedback on a half-finished PR is cheap.

---

## Next up: 0.2

The goals: run on Linux, and cover the FL features people miss most.

### 🐧 Linux support 🔴

TStudio is built on JUCE, which already runs on Linux, and the Windows-only code is gathered in one file. This is the most wanted port, and it can be done in steps:

- [ ] **Build on Linux** 🟡. Use LAME's autotools `config.h` instead of `configMS.h` in `CMakeLists.txt`, keep the MSVC flags behind `if(MSVC)`, and list the apt packages JUCE needs (ALSA, JACK, X11, FreeType, libcurl) in the README.
- [ ] **Port `Source/UI/NativeWindow.cpp`** 🟡. Play the startup chirp through JUCE audio instead of `PlaySoundW`. Read memory use from `/proc/self/status`. Register `.tsp` files with a `.desktop` file and a shared-mime-info XML.
- [ ] **Linux CI job** 🟢. Add an `ubuntu-latest` job to `.github/workflows/build.yml` that builds and runs `tests/run_tests.py` under `xvfb-run`.
- [ ] **Packages** 🟡. Ship an AppImage from CI first, then a Flatpak on Flathub.
- [ ] **JACK and PipeWire** 🟡. Test low-latency setups and document them.
- [ ] **LV2 hosting** 🟡. JUCE can host LV2 on Linux. Turn it on and add it to the plugin manager.
- [ ] **Native plugins as `.so`** 🟢. The loader already looks for `.so`. Add a Linux build line to `SDK/README.md` and test it.

### 🎧 Audio clips and recording 🔴

Today TStudio records notes, not audio. These are the biggest gaps compared to FL:

- [ ] **Audio clips in the playlist.** Drag a sample onto the playlist, trim, fade, reverse, and change gain per clip.
- [ ] **Record audio** into the playlist from an input, with punch in and out on the loop region.
- [ ] **Time-stretching** 🟡. Stretch clips to the project tempo without changing pitch, and resample mode for the chipmunk sound.
- [ ] **Waveform editor** 🟡. An Edison-style window for trimming, normalizing and chopping samples.

### 🎛️ Mixer and workflow

- [ ] **Plugin delay compensation** 🟡. Read each plugin's reported latency and align the mixer paths.
- [ ] **Stem export** 🟢. Render each mixer track to its own WAV ("split mixer tracks" in FL's export dialog).
- [ ] **MIDI learn** 🟡. Right-click a knob, choose "Link to controller", wiggle a hardware knob.
- [ ] **Make unique** 🟢. Turn a pattern clip into its own copy of the pattern.
- [ ] **Mixer track groups and colours** 🟢.
- [ ] **Project templates** 🟢 🧩. File > New from template, with a few starter templates (trap, lofi, house).

---

## Later

### 🍎 macOS 🔴

- [ ] Build universal binaries (Apple Silicon and Intel).
- [ ] Host Audio Units next to VST3.
- [ ] Port `NativeWindow.cpp` (startup sound, memory use, file associations through `Info.plist`).
- [ ] Sign and notarize a `.dmg` in CI.

### 🔌 Plugins and the ecosystem

- [ ] **In-app plugin store** 🟡. Read `plugins.json` from [TStudio-Plugins](https://github.com/Luckyz-z/TStudio-Plugins), show free and paid plugins in the browser, install free ones with one click, and open the buy page for paid ones.
- [ ] **Hot reload for Lua plugins** 🟢. Watch `Documents/TStudio/Plugins/` and reload a plugin when its file changes.
- [ ] **CLAP hosting** 🟡, and a CLAP build of the Lua loaders.
- [ ] **Rust and Zig bindings** for `tstudio_plugin.h` 🟢. Publish them as small crates and packages with an example plugin.
- [ ] **Lua plugin debugger** 🟡. Error line highlighting, print to the script console, and a CPU meter per plugin.
- [ ] **More example plugins** 🟢 🧩. A wavetable synth, granular delay, vocoder, sidechain pump with a drawable curve, and a step-sequenced filter.

### 🎹 Sound

- [ ] **Drum machine instrument** 🟡. An FPC-style 16-pad sampler with per-pad choke and layers.
- [ ] **Wavetable synth** 🟡 with a drawable wavetable editor.
- [ ] **Slicer instrument** 🟡. Auto-chop a loop to transients and map the slices to keys.
- [ ] **CC0 sample packs** 🟢 🧩. Synthesized or recorded and released under CC0, browsable from Packs.
- [ ] **Preset packs** 🟢 🧩 for Tomato Synth, 808 and the effects.

### 🤖 AI and automation

- [ ] **More MCP tools** 🟢. Audio clips, sample browsing, preset loading and "render this pattern" previews.
- [ ] **Chat panel inside TStudio** 🟡 that talks to any MCP-capable assistant the user has configured.
- [ ] **Lua script gallery** 🟢 🧩. Chord progression, humanize and fill generators shared in TStudio-Plugins.

### 🎨 Look and feel

- [ ] **Themes** 🟡. Load colours from a JSON file in `Documents/TStudio/Themes/`, with a light theme and a high-contrast theme.
- [ ] **Translations** 🟡 🧩. Route UI strings through JUCE's `LocalisedStrings`, then crowdsource languages.
- [ ] **Accessibility** 🟡. Screen-reader names on controls and full keyboard navigation in every window.
- [ ] **Touch and pen input** 🟡 for tablets and touchscreen laptops.

### 🛠️ Project health

- [ ] **Update check** 🟢. On startup, compare against GitHub's latest release and show a "new version" link in the toolbar's info panel. Make it opt-out.
- [ ] **Code signing** for the Windows installer, so SmartScreen stops warning.
- [ ] **DSP unit tests** 🟢. Small tests for each built-in effect next to the integration suite.
- [ ] **Performance benchmarks** 🟡. Render the demos in CI and fail when CPU use goes up a lot.
- [ ] **Docs site** 🟢 🧩. Publish `docs/` with GitHub Pages and add tutorials ("your first beat", "your first Lua plugin").

---

## Done

- [x] FL-style channel rack, piano roll, playlist and mixer, with FL shortcuts
- [x] `.flp` import, MIDI import and export, WAV, FLAC, OGG and MP3 export
- [x] Mixer routing, sends and sidechain
- [x] Automation clips, slides, porta, cut groups, arpeggiator
- [x] Lua scripting, extensions and Lua plugins with custom UIs
- [x] Lua loader VST3s for other DAWs, native C plugin SDK
- [x] MCP server for AI assistants
- [x] Windows installer and automatic GitHub releases

Have an idea that isn't here? Open a [feature request](https://github.com/Luckyz-z/TStudio/issues/new/choose) or start a thread in [Discussions](https://github.com/Luckyz-z/TStudio/discussions).
