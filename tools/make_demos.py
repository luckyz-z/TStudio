import base64
import math
import os
import struct
import xml.etree.ElementTree as ET

PPQ = 96
STEP = PPQ // 4
BAR = PPQ * 4
OUT = os.path.join(os.path.dirname(__file__), "..", "Demos")

LINEAR = None


def norm(value, low, high, centre=LINEAR):
    proportion = min(1.0, max(0.0, (value - low) / (high - low)))
    if centre is None:
        return proportion
    skew = math.log(0.5) / math.log((centre - low) / (high - low))
    return proportion ** skew


SYNTH_RANGES = {
    "attack": (0, 5000, 300),
    "decay": (1, 5000, 400),
    "sustain": (0, 1, None),
    "release": (1, 8000, 400),
    "cutoff": (20, 20000, 1500),
    "resonance": (0, 1, None),
    "filterEnvelope": (0, 1, None),
    "volume": (0, 1, None),
    "glide": (0, 1000, 150),
    "vibratoRate": (0.1, 12, 5),
    "vibratoDepth": (0, 100, None),
}
PLUCK_RANGES = {
    "decay": (0.1, 12, 2), "brightness": (0, 1, None), "damping": (0, 1, None), "pick": (0.05, 0.5, None),
    "release": (5, 2000, 150), "width": (0, 1, None), "volume": (0, 1, None),
}
KEYS_RANGES = {
    "tone": (0, 1, None), "bell": (0, 1, None), "decay": (0.3, 10, 2), "release": (10, 3000, 300),
    "tremoloRate": (0.1, 10, 4), "tremoloDepth": (0, 1, None), "volume": (0, 1, None),
}
BASS808_RANGES = {
    "tune": (-12, 12, None), "glide": (0, 600, 120), "decay": (0.1, 10, 1.5), "release": (5, 1000, 120), "punch": (0, 36, None),
    "punchTime": (5, 200, 40), "drive": (0, 30, None), "tone": (150, 12000, 2000), "volume": (0, 1, None),
}
SHAPES = ["Sine", "Triangle", "Saw", "Square", "Noise"]

EFFECT_RANGES = {
    "Tomato Reverb": {"size": (0, 1, None), "damping": (0, 1, None), "width": (0, 1, None), "wet": (0, 1, None), "dry": (0, 1, None)},
    "Tomato Delay": {"time": (1, 32, None), "feedback": (0, 0.95, None), "wet": (0, 1, None), "damp": (500, 20000, 4000)},
    "Tomato Compressor": {"threshold": (-60, 0, None), "ratio": (1, 20, 4), "attack": (0.1, 200, 10), "release": (5, 2000, 150), "makeup": (-12, 24, None)},
    "Tomato Limiter": {"gain": (-12, 24, None), "ceiling": (-24, 0, None), "release": (1, 1000, 80)},
    "Tomato Distortion": {"drive": (0, 40, None), "tone": (500, 20000, 4000), "output": (-36, 6, None)},
    "Tomato Filter": {"cutoff": (20, 20000, 1000), "resonance": (0, 1, None), "drive": (1, 10, None)},
    "Tomato Chorus": {"rate": (0.01, 10, 1), "depth": (0, 1, None), "centre": (1, 50, None), "feedback": (-0.95, 0.95, None), "wet": (0, 1, None)},
    "Tomato Stereo": {"gain": (-36, 12, None), "pan": (-1, 1, None), "width": (0, 2, None)},
    "Tomato Maximizer": {"mode": ("choice", 4), "amount": (0, 1, None), "output": (-18, 6, None)},
    "Tomato Clipper": {"input": (0, 24, None), "ceiling": (-12, 0, None), "mode": ("choice", 3)},
    "Tomato Pump": {"rate": ("choice", 4), "depth": (0, 1, None), "curve": (0.3, 5, None), "hold": (0, 0.6, None)},
    "Tomato Gate": {"pattern": ("choice", 8), "depth": (0, 1, None), "smooth": (0.5, 40, None)},
    "Tomato Lofi": {"bits": (3, 16, None), "downsample": (1, 24, None), "wow": (0, 1, None), "noise": (0, 1, None), "tone": (800, 20000, 5000)},
    "Tomato Autopan": {"rate": ("choice", 6), "depth": (0, 1, None), "shape": ("choice", 3)},
    "Tomato EQ": {"lowFreq": (20, 1000, 120), "lowGain": (-18, 18, None), "midFreq": (100, 10000, 1000), "midGain": (-18, 18, None), "midQ": (0.2, 10, 1),
              "highFreq": (1000, 20000, 6000), "highGain": (-18, 18, None)},
}


def state_blob(parameters, attributes=None):
    root = ET.Element("State", attributes or {})
    for key, value in parameters.items():
        ET.SubElement(root, "Parameter", {"id": key, "value": f"{value:.6f}"})
    data = ET.tostring(root, encoding="unicode").encode("utf-8")
    blob = struct.pack("<ii", 0x21324356, len(data)) + data + b"\0"
    return base64.b64encode(blob).decode("ascii")


def sampler(sound, root=60, cut=False, release=20.0, pitch=0.0):
    parameters = {
        "root": root / 127.0,
        "pitch": norm(pitch, -24, 24),
        "attack": 0.0,
        "release": norm(release, 1, 4000, 200),
        "cut": 1.0 if cut else 0.0,
        "reverse": 0.0,
    }
    return ("Sampler", state_blob(parameters, {"pack": sound, "name": sound}))


def synth(oscillators, **settings):
    parameters = {}
    for index, (shape, coarse, fine, level) in enumerate(oscillators, start=1):
        parameters[f"osc{index}Shape"] = SHAPES.index(shape) / (len(SHAPES) - 1)
        parameters[f"osc{index}Coarse"] = norm(coarse, -36, 36)
        parameters[f"osc{index}Fine"] = norm(fine, -100, 100)
        parameters[f"osc{index}Level"] = level
    for key, value in settings.items():
        low, high, centre = SYNTH_RANGES[key]
        parameters[key] = norm(value, low, high, centre)
    return ("Tomato Synth", state_blob(parameters))


def instrument(name, ranges, **settings):
    parameters = {}
    for key, value in settings.items():
        low, high, centre = ranges[key]
        parameters[key] = norm(value, low, high, centre)
    return (name, state_blob(parameters))


def eight08(**settings):
    return instrument("Tomato 808", BASS808_RANGES, **settings)


def slides(notes, transpose=-12, overlap=STEP):
    ordered = sorted(notes)
    result = []
    for index, (start, length, key, velocity) in enumerate(ordered):
        following = ordered[index + 1] if index + 1 < len(ordered) else None
        if following and following[2] != key and following[0] - (start + length) <= STEP:
            length = following[0] - start + overlap
        result.append((start, length, key + transpose, velocity))
    return result


def master_chain(amount=0.35, mode=1):
    return [effect("Tomato Maximizer", mode=mode, amount=amount, output=-1.0), effect("Tomato Limiter", gain=0.0, ceiling=-0.5)]


def string(**settings):
    return instrument("Tomato Pluck", PLUCK_RANGES, **settings)


def rhodes(**settings):
    return instrument("Tomato Keys", KEYS_RANGES, **settings)


def effect(name, **settings):
    parameters = {}
    for key, value in settings.items():
        if isinstance(value, bool):
            parameters[key] = 1.0 if value else 0.0
            continue
        spec = EFFECT_RANGES[name][key]
        if spec[0] == "choice":
            parameters[key] = value / (spec[1] - 1)
            continue
        low, high, centre = spec
        parameters[key] = norm(value, low, high, centre)
    return (name, state_blob(parameters) if parameters else "")


PLUCK = lambda: synth([("Saw", 0, 0, 0.6), ("Square", 12, 4, 0.25), ("Sine", 24, 0, 0.15)],
                      attack=1, decay=260, sustain=0.0, release=260, cutoff=2600, resonance=0.25, filterEnvelope=0.45, volume=0.42)
PAD = lambda: synth([("Saw", 0, -9, 0.45), ("Saw", 0, 9, 0.45), ("Sine", -12, 0, 0.35)],
                    attack=700, decay=1200, sustain=0.8, release=1600, cutoff=1500, resonance=0.1, filterEnvelope=0.12, volume=0.26)
BELL = lambda: synth([("Sine", 0, 0, 0.7), ("Triangle", 12, 0, 0.3), ("Sine", 24, 3, 0.18)],
                     attack=2, decay=700, sustain=0.15, release=600, cutoff=9000, resonance=0.05, filterEnvelope=0.0, volume=0.4)
KEYS = lambda: synth([("Triangle", 0, 0, 0.7), ("Sine", 12, 0, 0.25), ("Sine", 0, 6, 0.3)],
                     attack=12, decay=900, sustain=0.35, release=420, cutoff=3200, resonance=0.1, filterEnvelope=0.2, volume=0.34)
BASS = lambda: synth([("Sine", 0, 0, 0.85), ("Saw", 0, 0, 0.3), ("Square", -12, 0, 0.0)],
                     attack=3, decay=350, sustain=0.65, release=120, cutoff=700, resonance=0.15, filterEnvelope=0.2, volume=0.5)
STAB = lambda: synth([("Saw", 0, -6, 0.5), ("Saw", 0, 6, 0.5), ("Square", 12, 0, 0.15)],
                     attack=1, decay=180, sustain=0.05, release=150, cutoff=1800, resonance=0.3, filterEnvelope=0.55, volume=0.36)

PALETTE = ["ff5f9e78", "ff6a8fc2", "ffc28b5a", "ffa36cb8", "ffc1606a", "ff5aa5a8", "ffb8a24f", "ff7d86c9"]


class Song:
    def __init__(self, title, tempo, swing=0.0):
        self.title = title
        self.tempo = tempo
        self.swing = swing
        self.channels = []
        self.patterns = []
        self.clips = []
        self.tracks = {}
        self.mixer = {0: ("Master", 1.0, [effect("Tomato Limiter", gain=0.0, ceiling=-0.5)])}

    def master(self, effects):
        self.mixer[0] = ("Master", 1.0, effects)

    def channel(self, name, device, volume=0.78, pan=0.0, insert=0):
        identifier = len(self.channels) + 1
        self.channels.append((identifier, name, PALETTE[(identifier - 1) % len(PALETTE)], device, volume, pan, insert))
        return identifier

    def pattern(self, name, notes):
        self.patterns.append((name, PALETTE[len(self.patterns) % len(PALETTE)], notes))
        return len(self.patterns) - 1

    def place(self, track, pattern, bar, bars):
        self.clips.append((track, bar * BAR, bars * BAR, pattern))

    def insert(self, index, name, effects, volume=1.0):
        self.mixer[index] = (name, volume, effects)

    def write(self, file_name):
        root = ET.Element("TStudio", {
            "version": "1", "title": self.title, "tempo": f"{self.tempo}", "swing": f"{self.swing}", "mainVolume": "0.8",
            "currentPattern": "0", "songMode": "1", "nextChannelId": str(len(self.channels) + 1),
        })
        channels = ET.SubElement(root, "Channels")
        for identifier, name, colour, (builtin, state), volume, pan, insert in self.channels:
            element = ET.SubElement(channels, "Channel", {
                "id": str(identifier), "name": name, "colour": colour, "volume": f"{volume}", "pan": f"{pan}", "muted": "0", "insert": str(insert),
            })
            ET.SubElement(element, "Device", {"builtin": builtin, "bypassed": "0", "mix": "1", "state": state})
        patterns = ET.SubElement(root, "Patterns")
        for name, colour, notes in self.patterns:
            element = ET.SubElement(patterns, "Pattern", {"name": name, "colour": colour})
            for channel, list_ in notes.items():
                text = " ".join(f"{s},{l},{k},{v:.3f}" for s, l, k, v in sorted(list_))
                ET.SubElement(element, "Notes", {"channel": str(channel)}).text = text
        playlist = ET.SubElement(root, "Playlist")
        for track, start, length, pattern in self.clips:
            ET.SubElement(playlist, "Clip", {"track": str(track), "start": str(start), "length": str(length), "pattern": str(pattern), "offset": "0", "muted": "0"})
        for index, name in self.tracks.items():
            ET.SubElement(playlist, "Track", {"index": str(index), "name": name, "muted": "0"})
        mixer = ET.SubElement(root, "Mixer")
        for index, (name, volume, effects) in sorted(self.mixer.items()):
            element = ET.SubElement(mixer, "Track", {"index": str(index), "name": name, "volume": f"{volume}", "pan": "0", "muted": "0", "solo": "0"})
            for slot, (builtin, state) in enumerate(effects):
                slot_element = ET.SubElement(element, "Slot", {"index": str(slot)})
                attributes = {"builtin": builtin, "bypassed": "0", "mix": "1"}
                if state:
                    attributes["state"] = state
                ET.SubElement(slot_element, "Device", attributes)
        ET.indent(root)
        os.makedirs(OUT, exist_ok=True)
        ET.ElementTree(root).write(os.path.join(OUT, file_name), encoding="UTF-8", xml_declaration=True)


def steps(positions, key=60, length=1, velocity=0.78, bar=0):
    return [((bar * 16 + p) * STEP, length * STEP, key, velocity) for p in positions]


def hits(spec, key=60, length=STEP):
    return [(int(tick), length, key, velocity) for tick, velocity in spec]


def chords(progression, length_steps, rhythm, velocity=0.7):
    notes = []
    for bar, chord in enumerate(progression):
        for start, length in rhythm:
            for key in chord:
                notes.append(((bar * 16 + start) * STEP, length * STEP, key, velocity))
    return notes


def melody(phrase, velocity=0.8):
    return [((bar * 16 + start) * STEP, length * STEP, key, velocity) for bar, start, length, key in phrase]


def night_shift():
    song = Song("Night Shift", 145.0)
    kick = song.channel("Kick", sampler("Kick Hard"), 0.8, insert=4)
    clap = song.channel("Clap", sampler("Clap Big"), 0.62, insert=4)
    snare = song.channel("Snare", sampler("Snare Trap"), 0.5, insert=4)
    hat = song.channel("Hat", sampler("Hat Tight"), 0.52, 0.12, insert=4)
    open_hat = song.channel("Open Hat", sampler("Open Hat"), 0.38, -0.15, insert=4)
    perc = song.channel("Rim", sampler("Rim"), 0.4, 0.25, insert=4)
    bass = song.channel("808", eight08(glide=80, decay=2.4, drive=10, tone=3200, punch=12), 0.86, insert=2)
    pluck = song.channel("Pluck", PLUCK(), 0.7, insert=1)
    pad = song.channel("Pad", PAD(), 0.62, insert=3)
    crash = song.channel("Crash", sampler("Crash"), 0.34, insert=4)

    hat_bar = [(s * STEP, 0.82 if s % 4 == 0 else 0.55) for s in range(0, 16, 2)]
    roll32 = [((12 * STEP) + i * 12, 0.45 + i * 0.05) for i in range(8)]
    triplet = [((8 * STEP) + i * 16, 0.5 + i * 0.06) for i in range(6)]
    drums = {
        kick: steps([0, 10], bar=0) + steps([0, 6, 11], bar=1) + steps([0, 10], bar=2) + steps([0, 3, 6, 11], bar=3),
        clap: [n for b in range(4) for n in steps([8], bar=b)],
        snare: [n for b in range(4) for n in steps([8], bar=b, velocity=0.6)] + steps([14, 15], bar=3, velocity=0.45),
        hat: hits(hat_bar) + hits([(t + BAR, v) for t, v in hat_bar if t < 12 * STEP] + [(t + BAR, v) for t, v in roll32])
             + hits([(t + 2 * BAR, v) for t, v in hat_bar if t < 8 * STEP] + [(t + 2 * BAR, v) for t, v in triplet] + [(t + 2 * BAR, v) for t, v in hat_bar if t >= 12 * STEP])
             + hits([(t + 3 * BAR, v) for t, v in hat_bar]),
        open_hat: steps([14], bar=0, length=2, velocity=0.6) + steps([14], bar=2, length=2, velocity=0.6),
        perc: steps([3, 13], bar=1, velocity=0.5) + steps([5], bar=3, velocity=0.45),
    }
    hats_only = {hat: hits([(s * STEP + b * BAR, 0.6 if s % 4 == 0 else 0.4) for b in range(4) for s in range(0, 16, 2)]), perc: steps([3, 13], bar=1, velocity=0.4) + steps([3, 13], bar=3, velocity=0.4)}

    roots = [36, 32, 39, 34]
    bass_notes = []
    for bar, root in enumerate(roots):
        bass_notes += [((bar * 16) * STEP, 5 * STEP, root, 0.9), ((bar * 16 + 6) * STEP, 3 * STEP, root, 0.8), ((bar * 16 + 10) * STEP, 4 * STEP, root, 0.85)]
    bass_notes += [((3 * 16 + 14) * STEP, 2 * STEP, roots[3] + 12, 0.8)]

    progression = [[60, 63, 67], [56, 60, 63], [55, 58, 63], [53, 58, 62]]
    pluck_phrase = [
        (0, 0, 2, 67), (0, 2, 2, 75), (0, 4, 2, 72), (0, 6, 1, 67), (0, 7, 1, 68), (0, 8, 2, 67), (0, 10, 2, 75), (0, 12, 2, 74), (0, 14, 2, 72),
        (1, 0, 2, 68), (1, 2, 2, 72), (1, 4, 2, 75), (1, 6, 1, 68), (1, 7, 1, 70), (1, 8, 2, 72), (1, 10, 2, 75), (1, 12, 2, 77), (1, 14, 2, 75),
        (2, 0, 2, 67), (2, 2, 2, 70), (2, 4, 2, 75), (2, 6, 2, 79), (2, 8, 2, 77), (2, 10, 2, 75), (2, 12, 2, 74), (2, 14, 2, 70),
        (3, 0, 2, 65), (3, 2, 2, 70), (3, 4, 2, 74), (3, 6, 2, 77), (3, 8, 2, 74), (3, 10, 2, 72), (3, 12, 2, 70), (3, 14, 2, 67),
    ]

    p_drums = song.pattern("Drums", drums)
    p_bass = song.pattern("808", {bass: slides(bass_notes)})
    p_pluck = song.pattern("Pluck", {pluck: melody(pluck_phrase)})
    p_pad = song.pattern("Pad", {pad: chords(progression, 16, [(0, 16)], 0.6)})
    p_hats = song.pattern("Hats intro", hats_only)
    p_crash = song.pattern("Crash", {crash: steps([0], length=8, velocity=0.7)})

    song.tracks = {0: "Pluck", 1: "Pad", 2: "Drums", 3: "808", 4: "FX"}
    for bar in range(0, 32, 4):
        section = bar // 4
        if section not in (3, 4):
            song.place(0, p_pluck, bar, 4)
        song.place(1, p_pad, bar, 4)
        if section == 0:
            song.place(2, p_hats, bar, 4)
        elif section < 7:
            song.place(2, p_drums, bar, 4)
            song.place(3, p_bass, bar, 4)
        if section in (1, 3, 5):
            song.place(4, p_crash, bar, 1)
    song.insert(1, "Pluck", [effect("Tomato Delay", time=3, feedback=0.38, wet=0.22, damp=4500), effect("Tomato Reverb", size=0.6, wet=0.22, dry=0.9)])
    song.insert(2, "808", [effect("Tomato Distortion", drive=5, tone=9000, output=-4), effect("Tomato Compressor", threshold=-14, ratio=3, attack=8, release=120, makeup=2)])
    song.insert(3, "Pad", [effect("Tomato Chorus", rate=0.4, depth=0.35, wet=0.4), effect("Tomato Reverb", size=0.85, damping=0.6, wet=0.38, dry=0.75),
                           effect("Tomato Pump", rate=1, depth=0.45, curve=2.0)], 0.85)
    song.insert(4, "Drums", [effect("Tomato Compressor", threshold=-12, ratio=2.5, attack=12, release=90, makeup=1.5), effect("Tomato Clipper", input=3, ceiling=-1)])
    song.master(master_chain(0.35, 1))
    song.write("Night Shift.tsp")


def carrot_drill():
    song = Song("Carrot Drill", 142.0)
    kick = song.channel("Kick", sampler("Kick Hard"), 0.8, insert=4)
    snare = song.channel("Snare", sampler("Snare Trap"), 0.6, insert=4)
    clap = song.channel("Clap", sampler("Clap"), 0.45, insert=4)
    hat = song.channel("Hat", sampler("Hat", pitch=2), 0.52, 0.1, insert=4)
    shaker = song.channel("Shaker", sampler("Shaker"), 0.32, -0.25, insert=4)
    bass = song.channel("808", eight08(glide=110, decay=2.0, drive=14, tone=2800, punch=10), 0.88, insert=2)
    bell = song.channel("Bell", BELL(), 0.66, insert=1)
    pad = song.channel("Choir Pad", PAD(), 0.55, insert=3)
    snap = song.channel("Snap", sampler("Snap"), 0.4, 0.2, insert=4)

    hat_rhythm = [0, 3, 6, 10, 12, 14]
    hat_notes = []
    for bar in range(4):
        for s in hat_rhythm:
            hat_notes.append(((bar * 16 + s) * STEP, STEP, 60, 0.75 if s in (0, 6, 12) else 0.5))
        if bar % 2 == 1:
            hat_notes += [((bar * 16 + 8) * STEP + i * 16, 16, 62, 0.45 + i * 0.07) for i in range(3)]
    drums = {
        kick: steps([0, 11], bar=0) + steps([6, 9], bar=1) + steps([0, 11], bar=2) + steps([2, 6, 13], bar=3),
        snare: [n for b in range(4) for n in steps([8], bar=b, velocity=0.8)] + steps([14], bar=1, velocity=0.35) + steps([3], bar=3, velocity=0.4),
        clap: [n for b in range(4) for n in steps([8], bar=b, velocity=0.6)],
        hat: hat_notes,
        shaker: [n for b in range(4) for n in steps(list(range(2, 16, 4)), bar=b, velocity=0.5)],
        snap: steps([12], bar=1, velocity=0.6) + steps([4], bar=3, velocity=0.5),
    }
    roots = [42, 38, 35, 37]
    bass_notes = []
    rhythm808 = [(0, 3), (3, 3), (6, 2), (10, 4), (14, 2)]
    for bar, root in enumerate(roots):
        for start, length in rhythm808:
            key = root + (12 if start == 14 else 0)
            bass_notes.append(((bar * 16 + start) * STEP, length * STEP, key, 0.85))
    bell_phrase = [
        (0, 0, 3, 78), (0, 3, 3, 81), (0, 6, 2, 85), (0, 10, 4, 83),
        (1, 0, 3, 81), (1, 3, 3, 78), (1, 6, 2, 74), (1, 10, 4, 78),
        (2, 0, 3, 74), (2, 3, 3, 78), (2, 6, 2, 83), (2, 10, 4, 81),
        (3, 0, 3, 80), (3, 3, 3, 76), (3, 6, 2, 73), (3, 10, 2, 76), (3, 12, 4, 80),
    ]
    progression = [[66, 69, 73], [62, 66, 69], [59, 62, 66], [61, 64, 68]]

    p_drums = song.pattern("Drill drums", drums)
    p_bass = song.pattern("808 slides", {bass: slides(bass_notes)})
    p_bell = song.pattern("Bell", {bell: melody(bell_phrase, 0.75)})
    p_pad = song.pattern("Choir", {pad: chords(progression, 16, [(0, 16)], 0.55)})
    p_intro = song.pattern("Intro hats", {hat: [n for n in hat_notes if n[3] > 0.6], shaker: drums[shaker]})

    song.tracks = {0: "Bell", 1: "Choir", 2: "Drums", 3: "808"}
    for bar in range(0, 32, 4):
        section = bar // 4
        song.place(1, p_pad, bar, 4)
        if section != 4:
            song.place(0, p_bell, bar, 4)
        if section == 0:
            song.place(2, p_intro, bar, 4)
        elif section < 7:
            song.place(2, p_drums, bar, 4)
            song.place(3, p_bass, bar, 4)
    song.insert(1, "Bell", [effect("Tomato Delay", time=6, feedback=0.42, wet=0.28, damp=3500), effect("Tomato Reverb", size=0.75, wet=0.3, dry=0.85)])
    song.insert(2, "808", [effect("Tomato Distortion", drive=9, tone=7000, output=-7)])
    song.insert(3, "Choir", [effect("Tomato Filter", cutoff=2400, resonance=0.1), effect("Tomato Reverb", size=0.9, damping=0.5, wet=0.42, dry=0.7)], 0.8)
    song.insert(4, "Drums", [effect("Tomato Compressor", threshold=-10, ratio=3, attack=6, release=80, makeup=2), effect("Tomato Clipper", input=4, ceiling=-1)])
    song.master(master_chain(0.4, 1))
    song.write("Carrot Drill.tsp")


def straw_hat_bounce():
    song = Song("Straw Hat Bounce", 90.0, swing=0.45)
    kick = song.channel("Kick", sampler("Kick", pitch=-2), 0.8, insert=4)
    snare = song.channel("Snare", sampler("Snare", pitch=-1), 0.62, insert=4)
    hat = song.channel("Hat", sampler("Hat"), 0.42, 0.2, insert=4)
    shaker = song.channel("Shaker", sampler("Shaker"), 0.3, -0.2, insert=4)
    keys = song.channel("Keys", KEYS(), 0.7, insert=1)
    bass = song.channel("Bass", BASS(), 0.74, insert=2)
    bell = song.channel("Bell", BELL(), 0.45, 0.15, insert=1)

    drums = {
        kick: steps([0, 7, 10], bar=0) + steps([0, 3, 10], bar=1) + steps([0, 7, 10], bar=2) + steps([0, 9, 11], bar=3),
        snare: [n for b in range(4) for n in steps([4, 12], bar=b, velocity=0.75)] + steps([15], bar=3, velocity=0.35),
        hat: [((b * 16 + s) * STEP, STEP, 60, 0.7 if s % 4 == 0 else 0.45) for b in range(4) for s in range(0, 16, 2)],
        shaker: [n for b in range(4) for n in steps([1, 5, 9, 13], bar=b, velocity=0.4)],
    }
    progression = [[51, 55, 58, 62], [48, 51, 55, 58], [53, 56, 60, 63], [46, 50, 53, 56]]
    keys_notes = chords(progression, 16, [(0, 6), (7, 7)], 0.62)
    bass_roots = [39, 36, 41, 34]
    bass_notes = []
    for bar, root in enumerate(bass_roots):
        bass_notes += [((bar * 16) * STEP, 6 * STEP, root, 0.85), ((bar * 16 + 7) * STEP, 2 * STEP, root, 0.7), ((bar * 16 + 10) * STEP, 4 * STEP, root + 7, 0.75)]
    bell_phrase = [(0, 8, 2, 79), (0, 10, 2, 82), (0, 12, 4, 86), (1, 10, 2, 82), (1, 12, 4, 79), (2, 8, 2, 77), (2, 10, 2, 80), (2, 12, 4, 84), (3, 8, 4, 82), (3, 12, 4, 80)]

    p_drums = song.pattern("Boom bap", drums)
    p_keys = song.pattern("Keys", {keys: keys_notes})
    p_bass = song.pattern("Bass", {bass: bass_notes})
    p_bell = song.pattern("Bell", {bell: melody(bell_phrase, 0.6)})

    song.tracks = {0: "Keys", 1: "Bell", 2: "Drums", 3: "Bass"}
    for bar in range(0, 24, 4):
        section = bar // 4
        song.place(0, p_keys, bar, 4)
        if section in (2, 3, 5):
            song.place(1, p_bell, bar, 4)
        if section >= 1:
            song.place(2, p_drums, bar, 4)
            song.place(3, p_bass, bar, 4)
    song.insert(1, "Keys", [effect("Tomato Chorus", rate=0.6, depth=0.25, wet=0.35), effect("Tomato Filter", cutoff=4200, resonance=0.05), effect("Tomato Reverb", size=0.55, wet=0.25, dry=0.9)])
    song.insert(2, "Bass", [effect("Tomato Compressor", threshold=-16, ratio=4, attack=10, release=150, makeup=3)])
    song.insert(4, "Drums", [effect("Tomato Distortion", drive=4, tone=8000, output=-3), effect("Tomato Compressor", threshold=-12, ratio=3, attack=15, release=100, makeup=2),
                             effect("Tomato Lofi", bits=12, downsample=2, wow=0.0, noise=0.1, tone=9000)])
    song.master([effect("Tomato Lofi", bits=16, wow=0.3, noise=0.2, tone=13000), effect("Tomato Maximizer", mode=0, amount=0.25, output=-1.0),
                 effect("Tomato Limiter", gain=0.0, ceiling=-0.5)])
    song.write("Straw Hat Bounce.tsp")


def hat_trick_house():
    song = Song("Hat Trick", 124.0)
    kick = song.channel("Kick", sampler("Kick", pitch=1), 0.82, insert=4)
    clap = song.channel("Clap", sampler("Clap"), 0.6, insert=4)
    hat = song.channel("Hat", sampler("Hat"), 0.45, 0.15, insert=4)
    open_hat = song.channel("Open Hat", sampler("Open Hat"), 0.5, -0.1, insert=4)
    shaker = song.channel("Shaker", sampler("Shaker"), 0.3, 0.3, insert=4)
    stab = song.channel("Stab", STAB(), 0.66, insert=1)
    bass = song.channel("Bass", BASS(), 0.72, insert=2)
    cowbell = song.channel("Cowbell", sampler("Cowbell"), 0.28, -0.3, insert=4)
    crash = song.channel("Crash", sampler("Crash"), 0.32, insert=4)

    drums = {
        kick: [n for b in range(4) for n in steps([0, 4, 8, 12], bar=b, velocity=0.85)],
        clap: [n for b in range(4) for n in steps([4, 12], bar=b, velocity=0.7)],
        hat: [n for b in range(4) for n in steps([0, 4, 8, 12, 14], bar=b, velocity=0.45)],
        open_hat: [n for b in range(4) for n in steps([2, 6, 10, 14], bar=b, length=2, velocity=0.6)],
        shaker: [n for b in range(4) for n in steps(list(range(1, 16, 2)), bar=b, velocity=0.35)],
        cowbell: steps([7], bar=1, velocity=0.5) + steps([7, 15], bar=3, velocity=0.5),
    }
    progression = [[57, 60, 64, 67], [62, 65, 69, 72], [53, 57, 60, 64], [55, 59, 62, 65]]
    stabs = chords(progression, 16, [(0, 1), (3, 1), (6, 2), (10, 1), (13, 2)], 0.7)
    bass_roots = [45, 38, 41, 43]
    bass_notes = [((bar * 16 + s) * STEP, STEP, root, 0.8) for bar, root in enumerate(bass_roots) for s in (2, 6, 10, 14)]

    p_drums = song.pattern("House drums", drums)
    p_kick = song.pattern("Kick only", {kick: drums[kick], hat: drums[hat]})
    p_stab = song.pattern("Stabs", {stab: stabs})
    p_bass = song.pattern("Offbeat bass", {bass: bass_notes})
    p_crash = song.pattern("Crash", {crash: steps([0], length=8, velocity=0.7)})

    song.tracks = {0: "Stabs", 1: "Drums", 2: "Bass", 3: "FX"}
    for bar in range(0, 32, 4):
        section = bar // 4
        song.place(1, p_kick if section in (0, 4) else p_drums, bar, 4)
        if section >= 1 and section != 4:
            song.place(2, p_bass, bar, 4)
        if section >= 2:
            song.place(0, p_stab, bar, 4)
        if section in (2, 5):
            song.place(3, p_crash, bar, 1)
    song.insert(1, "Stabs", [effect("Tomato Filter", cutoff=3500, resonance=0.25), effect("Tomato Delay", time=3, feedback=0.3, wet=0.2), effect("Tomato Reverb", size=0.5, wet=0.2, dry=0.9),
                             effect("Tomato Pump", rate=1, depth=0.6, curve=1.6)])
    song.insert(2, "Bass", [effect("Tomato Compressor", threshold=-14, ratio=4, attack=5, release=80, makeup=2)])
    song.insert(4, "Drums", [effect("Tomato Compressor", threshold=-10, ratio=2.5, attack=10, release=70, makeup=1.5)])
    song.insert(2, "Bass", [effect("Tomato Compressor", threshold=-14, ratio=4, attack=5, release=80, makeup=2), effect("Tomato Pump", rate=1, depth=0.7, curve=1.4)])
    song.master(master_chain(0.4, 0))
    song.write("Hat Trick.tsp")


if __name__ == "__main__":
    night_shift()
    carrot_drill()
    straw_hat_bounce()
    hat_trick_house()
    import type_beats
    for make in type_beats.ALL:
        make()
    print("\n".join(sorted(os.listdir(OUT))))
