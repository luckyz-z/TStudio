## What this changes

<!-- One or two sentences. Link the issue: Fixes #123 -->

## How you tested it

<!-- Which tests you ran, which demo projects you played, screenshots for UI changes. -->

## Checklist

- [ ] `python tests/run_tests.py --exe build/TStudio_artefacts/Release/TStudio.exe` passes
- [ ] No code comments; names explain the code (see CONTRIBUTING.md)
- [ ] The audio thread stays real-time safe (no allocations, I/O or new locks in `process` paths)
- [ ] FL Studio shortcuts and behaviour are matched where they exist
- [ ] Docs updated if I added a Lua API function, MCP tool or keyboard shortcut
- [ ] Nothing copied from FL Studio or other commercial products (code, art, samples, presets)
