import argparse
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import numpy as np  # noqa: E402
from analyze_wav import load, stats  # noqa: E402

DEFAULT_EXE = ROOT / "build-dev" / "TStudio_artefacts" / "Release" / "TStudio.exe"


class Skip(Exception):
    pass


class Studio:
    def __init__(self, exe, work, validator=None):
        self.exe, self.work, self.validator = exe, work, validator

    def run(self, *args, timeout=240):
        result = subprocess.run([str(self.exe), *args], cwd=self.work, timeout=timeout, capture_output=True)
        assert result.returncode == 0, f"TStudio exited with 0x{result.returncode & 0xFFFFFFFF:08X} for {args}"
        return result.returncode

    def lua(self, code, *args):
        script = self.work / "test.lua"
        script.write_text(code, encoding="utf-8")
        output = self.work / "tstudio-lua-output.txt"
        output.unlink(missing_ok=True)
        self.run(*args, "--lua", str(script), "--quit")
        text = output.read_text(encoding="utf-8") if output.exists() else ""
        assert "ERROR:" not in text, text
        return text

    def render_ok(self, path, min_rms=-45.0):
        info = stats(str(path))
        assert info["finite"], info
        assert info["clipped"] == 0, info
        assert info["peak_db"] <= 0.0, info
        assert info["rms_db"] > min_rms, info
        assert info["dc"] < 0.01, info
        return info


CLEAR = """
for _, c in ipairs(ts.channels()) do ts.remove_channel(c.id) end
ts.call("clear_playlist", {})
"""


def test_demos_render(studio):
    for demo in sorted((ROOT / "Demos").glob("*.tsp")):
        out = studio.work / f"demo-{demo.stem}.wav"
        studio.lua(f'ts.render([[{out}]])', "--demo", demo.stem)
        info = studio.render_ok(out, min_rms=-30.0)
        assert info["seconds"] > 10, (demo.stem, info)


def test_lua_api(studio):
    text = studio.lua("""
local p = ts.project()
assert(ts.note("C5") == 60 and ts.note("A#3") == 46)
assert(#ts.chord("A4", "min7") == 4)
local ok = pcall(function() ts.call("set_notes", { channel = 999, notes = {} }) end)
assert(not ok, "bad channel must raise")
local id = ts.add_channel("Tomato Synth", "Lead")
ts.add_note(id, 0, ts.BEAT, 60, 0.8)
assert(#ts.notes(id) == 1)
local t = { channel = id }
ts.set_preset(t, "Rage lead")
ts.set_preset(t, 0)
assert(t.index == nil, "helpers must not mutate arguments")
ts.tempo(150)
print("ok", ts.tempo(), #ts.channels())
""", "--demo", "Straw Hat Bounce")
    assert text.startswith("ok\t150"), text


def test_midi_roundtrip(studio):
    mid = studio.work / "roundtrip.mid"
    first = studio.lua(f"""
local target
for _, c in ipairs(ts.channels()) do if #ts.notes(c.id) > 4 then target = c.id break end end
ts.call("export_midi", {{ path = [[{mid}]], channel = target }})
print(#ts.notes(target))
""", "--demo", "Night Shift")
    second = studio.lua(CLEAR + f'local id = ts.add_channel("Tomato Synth") ts.call("import_midi", {{ path = [[{mid}]], channel = id }}) print(#ts.notes(id))')
    assert int(first) > 0 and first == second, (first, second)


def test_lua_plugins(studio):
    out = studio.work / "lua-plugins.wav"
    studio.lua(CLEAR + f"""
ts.tempo(140)
local chip, kick, bell = ts.add_channel("Lua Chip"), ts.add_channel("Lua Kick"), ts.add_channel("Lua FM Bell")
ts.update_channel(chip, {{ insert = 1 }})
ts.update_channel(bell, {{ insert = 2 }})
for step = 0, 31 do ts.add_note(chip, step * ts.STEP, ts.STEP, ts.scale_degree("A5", "minor", (step * 3) % 8 + 1), 0.8) end
for beat = 0, 7 do ts.add_note(kick, beat * ts.BEAT, ts.STEP, 60, 1) end
ts.add_note(bell, 0, ts.BAR, 72, 0.9)
for i, fx in ipairs({{ "Lua Tremolo", "Lua Stutter", "Lua Tape", "Lua Vinyl", "Lua Haas Widener", "Lua Ring Mod" }}) do ts.add_effect(1 + (i - 1) // 3, (i - 1) % 3, fx) end
ts.add_clip(ts.current_pattern(), 0, 0, 2)
ts.render([[{out}]])
""")
    studio.render_ok(out)


def test_native_plugins(studio):
    out = studio.work / "native.wav"
    studio.lua(CLEAR + f"""
local id = ts.add_channel("Native Sub", "Sub")
ts.update_channel(id, {{ insert = 1 }})
for i = 0, 7 do ts.add_note(id, i * ts.BEAT, ts.BEAT // 2, 36 + (i % 4) * 3, 0.9) end
ts.add_effect(1, 0, "Native Crusher")
ts.add_clip(ts.current_pattern(), 0, 0, 2)
ts.render([[{out}]])
""")
    studio.render_ok(out)


def test_mp3_export(studio):
    out = studio.work / "song.mp3"
    studio.lua(f"ts.render([[{out}]])", "--demo", "Hat Trick")
    data = out.read_bytes()
    assert len(data) > 200_000, len(data)
    assert data[0] == 0xFF and (data[1] & 0xE0) == 0xE0, data[:4]
    assert b"Info" in data[:200] or b"Xing" in data[:200], "missing LAME/Xing header"


def test_mixer_routing(studio):
    plain, ducked, project = studio.work / "plain.wav", studio.work / "ducked.wav", studio.work / "routes.tsp"
    studio.lua(CLEAR + f"""
local kick, pad = ts.add_channel("pack:Kick", "Kick"), ts.add_channel("Tomato Synth", "Pad")
ts.update_channel(kick, {{ insert = 1 }})
ts.update_channel(pad, {{ insert = 2 }})
ts.set_preset({{ channel = pad }}, "Soft pad")
for beat = 0, 7 do ts.add_note(kick, beat * ts.BEAT, ts.STEP, 60, 1) end
for _, k in ipairs(ts.chord("C5", "min7")) do ts.add_note(pad, 0, 2 * ts.BAR, k, 0.9) end
ts.add_clip(ts.current_pattern(), 0, 0, 2)
ts.route(1, 0, {{ remove = true }})
ts.render([[{plain}]])
ts.route(2, 3, {{ only = true }})
ts.add_effect(3, 0, "Tomato Compressor")
ts.set_preset({{ track = 3, slot = 0 }}, "Sidechain duck")
ts.route(1, 3, {{ sidechain = true }})
ts.mixer(3, {{ width = 0.5, invert_phase = true }})
assert(not pcall(ts.route, 3, 2), "loops must be refused")
ts.render([[{ducked}]])
ts.save([[{project}]])
""")
    reloaded = studio.lua('for _, t in ipairs(ts.call("get_mixer")) do local s = {} for _, x in ipairs(t.sends) do s[#s + 1] = x.to .. (x.sidechain and "sc" or "") end '
                          'print(t.track, table.concat(s, ","), t.width, tostring(t.invert_phase)) end', "--open", str(project))
    rows = {line.split("	")[0]: line.split("	")[1:] for line in reloaded.splitlines()}
    assert rows["1"][0] == "3sc" and rows["2"][0] == "3" and rows["3"][0] == "0", rows
    assert float(rows["3"][1]) == 0.5 and rows["3"][2] == "true", rows
    from analyze_wav import load
    import numpy as np
    def beat_rms(path):
        data, rate = load(str(path))
        beat = int(rate * 60 / 130)
        return np.mean([np.sqrt((data[b * beat + int(0.01 * rate):b * beat + int(0.06 * rate)] ** 2).mean()) for b in range(2, 7)])
    assert 20 * np.log10(beat_rms(plain) / beat_rms(ducked)) > 6, "sidechain compressor did not duck"


def test_automation_clips(studio):
    out, project = studio.work / "automation.wav", studio.work / "automation.tsp"
    studio.lua(CLEAR + f"""
ts.tempo(120)
local pad = ts.add_channel("Tomato Synth", "Pad")
ts.update_channel(pad, {{ insert = 1 }})
ts.set_preset({{ channel = pad }}, "Soft pad")
for bar = 0, 3 do for _, k in ipairs(ts.chord("C5", "min7")) do ts.add_note(pad, bar * ts.BAR, ts.BAR, k, 0.9) end end
ts.add_clip(ts.current_pattern(), 0, 0, 4)
ts.automate({{ kind = "mixer_volume", track = 1 }}, {{ {{ tick = 0, value = 0 }}, {{ tick = 4 * ts.BAR, value = 0.8 }} }})
ts.automate({{ kind = "parameter", channel = pad, parameter = "cutoff" }}, {{ {{ tick = 0, value = 0.1 }}, {{ tick = 4 * ts.BAR, value = 0.9 }} }})
assert(not pcall(ts.automate, {{ kind = "parameter", channel = pad, parameter = "nope" }}, {{}}), "unknown parameters must fail")
ts.render([[{out}]])
ts.save([[{project}]])
""")
    listing = studio.lua('local l = ts.call("list_automations") print(#l, #l[1].points, l[2].parameter)', "--open", str(project))
    assert listing.split() == ["2", "2", "cutoff"], listing
    from analyze_wav import load
    import numpy as np
    data, rate = load(str(out))
    bar = int(rate * 2)
    levels = [20 * np.log10(np.sqrt((data[b * bar + bar // 4:b * bar + bar * 3 // 4] ** 2).mean()) + 1e-12) for b in range(4)]
    assert levels[0] < levels[1] < levels[2] < levels[3] and levels[3] - levels[0] > 20, levels


def test_note_slides_and_mutes(studio):
    out, muted = studio.work / "slide.wav", studio.work / "muted.wav"
    studio.lua(CLEAR + f"""
ts.tempo(120)
local id = ts.add_channel("Tomato 808", "808")
ts.set_notes(id, {{ {{ start = 0, length = 2 * ts.BAR, key = 36, velocity = 0.9 }}, {{ start = ts.BAR, length = ts.BEAT, key = 43, velocity = 0.9, slide = true }} }})
ts.add_clip(ts.current_pattern(), 0, 0, 2)
ts.render([[{out}]])
ts.set_notes(id, {{ {{ start = 0, length = ts.BAR, key = 36, velocity = 1, muted = true }} }})
ts.render([[{muted}]])
""")
    from analyze_wav import load
    import numpy as np
    data, rate = load(str(out))
    def pitch(seconds):
        segment = data[int(seconds * rate):int(seconds * rate) + 8192, 0]
        spectrum = np.abs(np.fft.rfft(segment * np.hanning(len(segment)), n=65536))
        freqs = np.fft.rfftfreq(65536, 1 / rate)
        band = (freqs > 40) & (freqs < 200)
        return freqs[band][np.argmax(spectrum[band])]
    assert abs(pitch(0.5) - 65.4) < 3 and abs(pitch(3.0) - 98.0) < 3, (pitch(0.5), pitch(3.0))
    assert stats(str(muted))["peak_db"] < -100, "muted notes must not play"
    toggled = studio.lua("local s = 0 for _, n in ipairs(ts.notes(ts.selected_channel())) do if n.slide then s = s + 1 end end print(s)",
                         "--demo", "Straw Hat Bounce", "--view", "pattern=1,pianoroll=Keys", "--press", "roll:ctrl + A;roll:S")
    assert int(toggled) == 32, toggled


def test_channel_settings(studio):
    free, cut, arp, project = studio.work / "free.wav", studio.work / "cut.wav", studio.work / "arp.wav", studio.work / "settings.tsp"
    studio.lua(CLEAR + f"""
ts.tempo(120)
local open, closed = ts.add_channel("pack:Open Hat", "Open"), ts.add_channel("pack:Hat", "Closed")
ts.set_notes(open, {{ {{ start = 0, length = 2 * ts.BEAT, key = 60, velocity = 1 }} }})
ts.set_notes(closed, {{ {{ start = ts.BEAT // 2, length = ts.STEP, key = 60, velocity = 0.01 }} }})
ts.add_clip(ts.current_pattern(), 0, 0, 1)
ts.render([[{free}]])
ts.update_channel(closed, {{ cut_group = 1 }})
ts.update_channel(open, {{ cut_by = 1 }})
ts.render([[{cut}]])
ts.remove_channel(open)
ts.remove_channel(closed)
local pluck = ts.add_channel("Tomato Pluck", "Pluck")
for _, k in ipairs(ts.chord("C5", "min7")) do ts.add_note(pluck, 0, ts.BAR, k, 0.9) end
ts.update_channel(pluck, {{ arp = 1, arp_time = 1, arp_gate = 0.5, time_shift = 6, mono = true }})
ts.render([[{arp}]])
ts.save([[{project}]])
""")
    settings = studio.lua("local s = ts.channels()[1].settings print(s.arp, s.arp_time, s.time_shift, tostring(s.mono))", "--open", str(project))
    assert settings.split() == ["1", "1", "6.0", "true"], settings
    from analyze_wav import load
    import numpy as np
    def tail_db(path):
        data, rate = load(str(path))
        segment = data[int(0.3 * rate):int(0.6 * rate)]
        return 20 * np.log10(np.sqrt((segment ** 2).mean()) + 1e-12)
    assert tail_db(free) - tail_db(cut) > 30, (tail_db(free), tail_db(cut))
    data, rate = load(str(arp))
    hop = int(rate * 0.005)
    envelope = np.array([np.abs(data[i:i + hop, 0]).max() for i in range(0, int(rate * 2) - hop, hop)])
    rises = np.where((envelope[1:] > envelope[:-1] * 2.5) & (envelope[1:] > 0.02))[0]
    onsets = [rises[0]] if len(rises) else []
    for value in rises[1:]:
        if value - onsets[-1] > 10:
            onsets.append(value)
    assert 12 <= len(onsets) <= 17, len(onsets)


def test_new_effects(studio):
    names = ["Tomato Multiband", "Tomato Transient", "Tomato Flanger", "Tomato Shaper", "Tomato Glitch"]
    renders = [studio.work / f"effect{i}.wav" for i in range(len(names))]
    script = "\n".join(f'ts.add_effect(0, 0, "{name}") local list = ts.presets({{ track = 0, slot = 0 }}).presets '
                         f'ts.set_preset({{ track = 0, slot = 0 }}, #list - 1) ts.render([[{path}]])' for name, path in zip(names, renders))
    studio.lua(script, "--demo", "Hat Trick")
    for path in renders:
        studio.render_ok(path, min_rms=-30.0)


def test_vinyl_noise_stops_on_silence(studio):
    for name in ("Tomato Lofi", "Lua Vinyl"):
        out = studio.work / f"vinyl-{name.replace(' ', '-')}.wav"
        studio.lua(CLEAR + f"""
local id = ts.add_channel("Tomato Synth", "Synth")
ts.add_note(id, 0, ts.STEP, ts.note("C4"), 0.8)
ts.add_clip(ts.current_pattern(), 0, 0, 4)
ts.add_effect(0, 0, "{name}")
ts.set_param({{ track = 0, slot = 0 }}, "{'noise' if name == 'Tomato Lofi' else 'crackle'}", 1.0)
ts.render([[{out}]])
""")
        data, rate = load(str(out))
        start, end = int(0.1 * rate), int(0.6 * rate)
        assert np.abs(data[start:end]).max() > 0.001, (name, "no sound while the note plays")
        tail = data[-rate:]
        assert np.abs(tail).max() < 1e-4, (name, "noise after the audio stopped", float(np.abs(tail).max()))


def test_synth_presets(studio):
    folder = studio.work / "presets"
    folder.mkdir(exist_ok=True)
    studio.lua(CLEAR + f"""
local id = ts.add_channel("Tomato Synth", "Synth")
for _, k in ipairs(ts.chord(ts.note("C4"), "min7")) do ts.add_note(id, 0, ts.BAR - ts.STEP, k, 0.85) end
ts.add_clip(ts.current_pattern(), 0, 0, 1)
local list = ts.presets({{ channel = id }}).presets
for i = 1, #list - 1 do
  ts.set_preset({{ channel = id }}, i)
  ts.render([[{folder}]] .. "/" .. i .. ".wav")
end
""")
    files = list(folder.glob("*.wav"))
    assert len(files) >= 20, len(files)
    for path in files:
        studio.render_ok(path)


def test_extensions(studio):
    text = studio.lua("""
local names = ts._menu_names()
assert(#names >= 5, "bundled extension menus missing")
local ch = ts.selected_channel()
local before = #ts.notes(ch)
for i, name in ipairs(names) do if name == "Notes: double speed" then ts._menu(i) end end
assert(#ts.notes(ch) == before * 2)
local seen
ts.on("tempo", function(v) seen = v end)
ts._emit("tempo", 141)
assert(seen == 141)
ts.shortcut("Ctrl + Shift + K", function() end)
assert(ts._key("ctrl+shift+k") and not ts._key("ctrl+shift+j"))
assert(#ts._panel_info() >= 1)
print("ok")
""", "--demo", "Straw Hat Bounce")
    assert text.strip() == "ok", text


def test_keybindings(studio):
    probe = """
local p = ts.project()
local muted = {}
for _, c in ipairs(p.channels) do muted[#muted + 1] = c.muted and 1 or 0 end
print(p.current_pattern, #p.patterns, tostring(p.song_mode), table.concat(muted), p.selected_channel, #ts.notes(p.selected_channel))
"""
    base = studio.lua(probe, "--demo", "Straw Hat Bounce").split()
    keyed = studio.lua(probe, "--demo", "Straw Hat Bounce", "--press", "rack:2;main:L;main:numpad 6;rack:cursor down;rack:cursor down").split()
    assert keyed[0] == "5" and keyed[1] == "6", keyed
    assert keyed[2] != base[2], (base, keyed)
    assert keyed[3][1] == "1" and keyed[3].count("1") == 1, keyed
    typing = studio.lua(probe, "--demo", "Straw Hat Bounce", "--press", "main:ctrl + T;main:L").split()
    assert typing[2] == base[2], (base, typing)
    solo = studio.lua(probe, "--demo", "Straw Hat Bounce", "--press", "rack:ctrl + 3").split()
    assert solo[3][2] == "0" and solo[3].count("0") == 1, solo
    chopped = studio.lua(probe, "--demo", "Straw Hat Bounce", "--view", "pattern=1,pianoroll=Keys", "--press", "roll:ctrl + A;roll:ctrl + U").split()
    glued = studio.lua(probe, "--demo", "Straw Hat Bounce", "--view", "pattern=1,pianoroll=Keys", "--press", "roll:ctrl + A;roll:ctrl + U;roll:ctrl + A;roll:ctrl + G").split()
    plain = studio.lua(probe, "--demo", "Straw Hat Bounce", "--view", "pattern=1,pianoroll=Keys").split()
    assert int(chopped[5]) > int(plain[5]) and glued[5] == plain[5], (plain, chopped, glued)


def test_editor_snapshots(studio):
    listing = studio.lua("""
for _, method in ipairs({ "list_instruments", "list_effects" }) do
  for _, name in ipairs(ts.call(method)) do
    if not name:find(":") then print(name) end
  end
end
""")
    names = [line for line in listing.splitlines() if line.strip()]
    assert len(names) >= 25, names
    for name in names:
        shot = studio.work / "editor.png"
        shot.unlink(missing_ok=True)
        studio.run("--editor", name, "--snapshot", str(shot), timeout=60)
        assert shot.exists() and shot.stat().st_size > 3000, name


def test_mcp(studio):
    process = subprocess.Popen([str(studio.exe), "--demo", "Night Shift", "--mcp"], cwd=studio.work)
    try:
        result = subprocess.run([sys.executable, str(ROOT / "tools" / "mcp_smoke_test.py")], capture_output=True, text=True, timeout=120)
        assert result.returncode == 0 and "MCP OK" in result.stdout, result.stdout + result.stderr
    finally:
        process.kill()
        process.wait(timeout=30)


def test_vst3_validator(studio):
    if not studio.validator:
        raise Skip("pass --vst3-validator to run Steinberg's VST3 validator")
    build = studio.exe.parent.parent.parent
    for kind in ("FX", "Synth"):
        bundle = build / f"TStudioLua{kind}_artefacts" / "Release" / "VST3" / f"TStudio Lua {kind}.vst3"
        result = subprocess.run([str(studio.validator), str(bundle)], capture_output=True, text=True, timeout=600)
        assert result.returncode == 0 and " 0 tests failed" in result.stdout, (kind, result.stdout[-2000:])


TESTS = [test_lua_api, test_extensions, test_keybindings, test_midi_roundtrip, test_lua_plugins, test_native_plugins, test_mp3_export, test_mixer_routing, test_automation_clips, test_note_slides_and_mutes, test_channel_settings, test_new_effects, test_vinyl_noise_stops_on_silence, test_synth_presets,
         test_editor_snapshots, test_mcp, test_vst3_validator, test_demos_render]


def main():
    parser = argparse.ArgumentParser(description="TStudio integration tests")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("-k", default="", help="only run tests whose name contains this")
    parser.add_argument("--vst3-validator", type=Path, help="path to Steinberg's validator.exe")
    args = parser.parse_args()
    if not args.exe.exists():
        raise SystemExit(f"TStudio executable not found: {args.exe}")
    failures = skipped = 0
    with tempfile.TemporaryDirectory(prefix="tstudio-tests-") as folder:
        studio = Studio(args.exe.resolve(), Path(folder), args.vst3_validator)
        for test in TESTS:
            if args.k not in test.__name__:
                continue
            started = time.time()
            try:
                test(studio)
                print(f"PASS {test.__name__} ({time.time() - started:.1f}s)", flush=True)
            except Skip as reason:
                print(f"SKIP {test.__name__}: {reason}", flush=True)
                skipped += 1
            except Exception as error:
                failures += 1
                print(f"FAIL {test.__name__}: {error!r}"[:2000], flush=True)
    print(f"{len([t for t in TESTS if args.k in t.__name__]) - failures - skipped} passed, {failures} failed, {skipped} skipped")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
