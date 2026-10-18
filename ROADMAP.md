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

- [x] **Build on Linux** 🟡. Use LAME's autotools `config.h` instead of `configMS.h` in `CMakeLists.txt`, keep the MSVC flags behind `if(MSVC)`, and list the apt packages JUCE needs (ALSA, JACK, X11, FreeType, libcurl) in the README. ([#5](https://github.com/luckyz-z/TStudio/issues/5))
- [x] **Port `Source/UI/NativeWindow.cpp`** 🟡. Play the startup chirp through JUCE audio instead of `PlaySoundW`. Read memory use from `/proc/self/status`. Register `.tsp` files with a `.desktop` file and a shared-mime-info XML. ([#6](https://github.com/luckyz-z/TStudio/issues/6))
- [x] **Linux CI job** 🟢. Add an `ubuntu-latest` job to `.github/workflows/build.yml` that builds and runs `tests/run_tests.py` under `xvfb-run`. ([#7](https://github.com/luckyz-z/TStudio/issues/7))
- [ ] **Packages** 🟡. CI already ships a `.tar.gz`. Next: an AppImage, then a Flatpak on Flathub. ([#8](https://github.com/luckyz-z/TStudio/issues/8))
- [ ] **JACK and PipeWire** 🟡. Test low-latency setups and document them. ([#9](https://github.com/luckyz-z/TStudio/issues/9))
- [ ] **LV2 hosting** 🟡. JUCE can host LV2 on Linux. Turn it on and add it to the plugin manager. ([#10](https://github.com/luckyz-z/TStudio/issues/10))
- [x] **Native plugins as `.so`** 🟢. The loader already looks for `.so`. Add a Linux build line to `SDK/README.md` and test it. ([#11](https://github.com/luckyz-z/TStudio/issues/11))

### 🎧 Audio clips and recording 🔴

Today TStudio records notes, not audio. These are the biggest gaps compared to FL:

- [ ] **Audio clips in the playlist.** Drag a sample onto the playlist, trim, fade, reverse, and change gain per clip. ([#12](https://github.com/luckyz-z/TStudio/issues/12))
- [ ] **Record audio** into the playlist from an input, with punch in and out on the loop region. ([#13](https://github.com/luckyz-z/TStudio/issues/13))
- [ ] **Time-stretching** 🟡. Stretch clips to the project tempo without changing pitch, and resample mode for the chipmunk sound. ([#14](https://github.com/luckyz-z/TStudio/issues/14))
- [ ] **Waveform editor** 🟡. An Edison-style window for trimming, normalizing and chopping samples. ([#15](https://github.com/luckyz-z/TStudio/issues/15))

### 🎛️ Mixer and workflow

- [ ] **Plugin delay compensation** 🟡. Read each plugin's reported latency and align the mixer paths. ([#16](https://github.com/luckyz-z/TStudio/issues/16))
- [ ] **Stem export** 🟢. Render each mixer track to its own WAV ("split mixer tracks" in FL's export dialog). ([#17](https://github.com/luckyz-z/TStudio/issues/17))
- [ ] **MIDI learn** 🟡. Right-click a knob, choose "Link to controller", wiggle a hardware knob. ([#18](https://github.com/luckyz-z/TStudio/issues/18))
- [ ] **Make unique** 🟢. Turn a pattern clip into its own copy of the pattern. ([#19](https://github.com/luckyz-z/TStudio/issues/19))
- [ ] **Mixer track groups and colours** 🟢. ([#20](https://github.com/luckyz-z/TStudio/issues/20))
- [ ] **Project templates** 🟢 🧩. File > New from template, with a few starter templates (trap, lofi, house). ([#21](https://github.com/luckyz-z/TStudio/issues/21))

### 🎨 Themes and colours

FL Studio lets you tint the whole UI and colour your channels and patterns. TStudio should go further, with shareable theme files, live sliders, and themes that Lua extensions and plugins can follow. Tracked in [#40](https://github.com/luckyz-z/TStudio/issues/40). Start with the theme engine.

- [ ] **Theme engine** 🟡. Turn the 30 colours in `Source/UI/Theme.h` into a runtime palette that can be loaded from JSON, and move the hardcoded colours in other files into it. Everything else in this section builds on it. ([#50](https://github.com/luckyz-z/TStudio/issues/50))
- [ ] **Hue, saturation and brightness sliders** 🟢. FL-style sliders in Options > Theme that shift the whole palette live, plus a reset button. Needs theme engine. ([#51](https://github.com/luckyz-z/TStudio/issues/51))
- [ ] **Accent colour picker** 🟢. Swap tomato red for any colour. Reuse `ui::pickColour`. Needs theme engine. ([#52](https://github.com/luckyz-z/TStudio/issues/52))
- [ ] **Theme files and a theme menu** 🟢. Read themes from `Documents/TStudio/Themes/*.json`, list them in Options > Theme, and remember the choice. Needs theme engine. ([#53](https://github.com/luckyz-z/TStudio/issues/53))
- [ ] **Bundled themes** 🟢 🧩. Ship a few themes: Tomato Dark (the default), Light, High contrast, Midnight and Classic Grey. Needs theme files and a theme menu. ([#54](https://github.com/luckyz-z/TStudio/issues/54))
- [ ] **Workspace background image** 🟢. A wallpaper behind the windows, with adjustable dim and blur, like FL's background setting. Needs theme engine. ([#55](https://github.com/luckyz-z/TStudio/issues/55))
- [ ] **Colour groups and gradients** 🟢. Colour several channels, patterns or mixer tracks at once, with an optional gradient, like FL's colour selected / gradient tools. ([#56](https://github.com/luckyz-z/TStudio/issues/56))
- [ ] **Share themes in TStudio-Plugins** 🟢 🧩. Add a `theme` kind to `plugins.json` and its validator so people can share and sell themes. Needs theme files and a theme menu. ([#57](https://github.com/luckyz-z/TStudio/issues/57))
- [ ] **Lua theme API** 🟢. `ts.theme()` for extensions and `theme` colours for Lua plugin UIs, so community panels match the active theme. Needs theme engine. ([#58](https://github.com/luckyz-z/TStudio/issues/58))

---

## Later

### 🍎 macOS 🔴

- [ ] Build universal binaries (Apple Silicon and Intel). ([#22](https://github.com/luckyz-z/TStudio/issues/22))
- [ ] Host Audio Units next to VST3. ([#23](https://github.com/luckyz-z/TStudio/issues/23))
- [ ] Port `NativeWindow.cpp` (startup sound, memory use, file associations through `Info.plist`). ([#24](https://github.com/luckyz-z/TStudio/issues/24))
- [ ] Sign and notarize a `.dmg` in CI. ([#25](https://github.com/luckyz-z/TStudio/issues/25))

### 🔌 Plugins and the ecosystem

- [ ] **In-app plugin store** 🟡. Read `plugins.json` from [TStudio-Plugins](https://github.com/Luckyz-z/TStudio-Plugins), show free and paid plugins in the browser, install free ones with one click, and open the buy page for paid ones. ([#26](https://github.com/luckyz-z/TStudio/issues/26))
- [ ] **Hot reload for Lua plugins** 🟢. Watch `Documents/TStudio/Plugins/` and reload a plugin when its file changes. ([#27](https://github.com/luckyz-z/TStudio/issues/27))
- [ ] **CLAP hosting** 🟡, and a CLAP build of the Lua loaders. ([#28](https://github.com/luckyz-z/TStudio/issues/28))
- [ ] **Rust and Zig bindings** for `tstudio_plugin.h` 🟢. Publish them as small crates and packages with an example plugin. ([#29](https://github.com/luckyz-z/TStudio/issues/29))
- [ ] **Lua plugin debugger** 🟡. Error line highlighting, print to the script console, and a CPU meter per plugin. ([#30](https://github.com/luckyz-z/TStudio/issues/30))
- [ ] **More example plugins** 🟢 🧩. A wavetable synth, granular delay, vocoder, sidechain pump with a drawable curve, and a step-sequenced filter. ([#31](https://github.com/luckyz-z/TStudio/issues/31))

### 🎹 Sound

- [ ] **Drum machine instrument** 🟡. An FPC-style 16-pad sampler with per-pad choke and layers. ([#32](https://github.com/luckyz-z/TStudio/issues/32))
- [ ] **Wavetable synth** 🟡 with a drawable wavetable editor. ([#33](https://github.com/luckyz-z/TStudio/issues/33))
- [ ] **Slicer instrument** 🟡. Auto-chop a loop to transients and map the slices to keys. ([#34](https://github.com/luckyz-z/TStudio/issues/34))
- [ ] **CC0 sample packs** 🟢 🧩. Synthesized or recorded and released under CC0, browsable from Packs. ([#35](https://github.com/luckyz-z/TStudio/issues/35))
- [ ] **Preset packs** 🟢 🧩 for Tomato Synth, 808 and the effects. ([#36](https://github.com/luckyz-z/TStudio/issues/36))

### 🤖 AI and automation

- [ ] **More MCP tools** 🟢. Audio clips, sample browsing, preset loading and "render this pattern" previews. ([#37](https://github.com/luckyz-z/TStudio/issues/37))
- [ ] **Chat panel inside TStudio** 🟡 that talks to any MCP-capable assistant the user has configured. ([#38](https://github.com/luckyz-z/TStudio/issues/38))
- [ ] **Lua script gallery** 🟢 🧩. Chord progression, humanize and fill generators shared in TStudio-Plugins. ([#39](https://github.com/luckyz-z/TStudio/issues/39))

### 🎨 Look and feel

- [ ] **Translations** 🟡 🧩. Route UI strings through JUCE's `LocalisedStrings`, then crowdsource languages. ([#41](https://github.com/luckyz-z/TStudio/issues/41))
- [ ] **Accessibility** 🟡. Screen-reader names on controls and full keyboard navigation in every window. ([#42](https://github.com/luckyz-z/TStudio/issues/42))
- [ ] **Touch and pen input** 🟡 for tablets and touchscreen laptops. ([#43](https://github.com/luckyz-z/TStudio/issues/43))

### 🛠️ Project health

- [ ] **Update check** 🟢. On startup, compare against GitHub's latest release and show a "new version" link in the toolbar's info panel. Make it opt-out. ([#44](https://github.com/luckyz-z/TStudio/issues/44))
- [ ] **Code signing** for the Windows installer, so SmartScreen stops warning. ([#45](https://github.com/luckyz-z/TStudio/issues/45))
- [ ] **DSP unit tests** 🟢. Small tests for each built-in effect next to the integration suite. ([#46](https://github.com/luckyz-z/TStudio/issues/46))
- [ ] **Performance benchmarks** 🟡. Render the demos in CI and fail when CPU use goes up a lot. ([#47](https://github.com/luckyz-z/TStudio/issues/47))
- [ ] **Docs site** 🟢 🧩. Publish `docs/` with GitHub Pages and add tutorials ("your first beat", "your first Lua plugin"). ([#48](https://github.com/luckyz-z/TStudio/issues/48))

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
- [x] Linux builds, tests and release tarballs

Have an idea that isn't here? Open a [feature request](https://github.com/Luckyz-z/TStudio/issues/new/choose) or start a thread in [Discussions](https://github.com/Luckyz-z/TStudio/discussions).
