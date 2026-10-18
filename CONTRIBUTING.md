# Contributing to TStudio

Thanks for helping. You can contribute in four ways:

- **Lua plugins, scripts and extensions.** These are the easiest way to add features. Most ideas don't need a C++ change at all. Share them in their own repositories, or open a pull request that adds them to `Plugins/`, `Scripts/` or `Extensions/`.
- **Native plugins.** See [SDK/README.md](SDK/README.md).
- **Core changes.** Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first.
- **Bug reports.** Include the project file if you can. Explain how to reproduce the bug, and what you expected to happen instead.

## Code style

- C++20 with JUCE idioms, formatted like the surrounding code: 4 spaces, braces on new lines, `const auto` where possible.
- No code comments. Name things so the code explains itself. Put explanations in the docs and in pull request descriptions.
- Keep the audio thread real-time safe: no allocation, locks other than `project.lock`, or I/O in `process` paths.
- UI colours come from `Source/UI/Theme.h`. The accent is tomato red, `#e8483b`.
- Match FL Studio behaviour and shortcuts where one exists. Users should never have to relearn muscle memory.

## Before you open a pull request

```bash
cmake --build build --config Release --parallel
```

```bash
python tests/run_tests.py --exe build/TStudio_artefacts/Release/TStudio.exe
```

- Every test should pass.
- If you change audio code, run the demo renders: they are part of the suite.
- If you add an automation method, add it to the table in [docs/MCP.md](docs/MCP.md).
- If you add a Lua API function, add it to [docs/LUA_API.md](docs/LUA_API.md).

## Licensing and assets

- TStudio is AGPL-3.0 with a [plugin exception](LICENSE-PLUGIN-EXCEPTION.md). Contributions are accepted under the same terms.
- Don't contribute anything copied from FL Studio or other commercial products, including samples, presets, artwork and code.
- Sounds must be synthesized or released under CC0.
