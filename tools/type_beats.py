from make_demos import BASS, BELL, PAD, STEP, Song, chords, effect, eight08, master_chain, melody, rhodes, sampler, slides, steps, string, synth


def arp(progression, pattern, rate=1, length=1, shift=0, velocity=0.75, accent=None):
    notes = []
    for bar, chord in enumerate(progression):
        tones = [k + shift for k in chord] + [k + shift + 12 for k in chord] + [k + shift + 24 for k in chord]
        for i, step in enumerate(range(0, 16, rate)):
            index = pattern[i % len(pattern)]
            v = velocity * (accent[i % len(accent)] if accent else 1.0)
            notes.append(((bar * 16 + step) * STEP, length * STEP, tones[index], v))
    return notes


def per_bar(bars, builder):
    notes = []
    for bar in range(bars):
        notes += builder(bar)
    return notes


def lyfe_rage():
    song = Song("Lyfe Rage (Yeat type beat)", 150.0)
    kick = song.channel("Kick", sampler("Kick Hard", pitch=-1), 0.82, insert=4)
    clap = song.channel("Clap", sampler("Clap Big"), 0.62, insert=4)
    hat = song.channel("Hat", sampler("Hat Tight", pitch=1), 0.52, 0.1, insert=4)
    open_hat = song.channel("Open Hat", sampler("Open Hat"), 0.36, -0.12, insert=4)
    bass = song.channel("808", eight08(glide=70, decay=3.0, drive=18, tone=2500, punch=14), 0.88, insert=2)
    bells = song.channel("Bells", BELL(), 0.6, 0.1, insert=1)
    lead = song.channel("Rage Lead", synth([("Square", 0, -14, 0.5), ("Square", 0, 14, 0.5), ("Saw", 12, 0, 0.2)],
                                         attack=2, decay=220, sustain=0.55, release=120, cutoff=3800, resonance=0.25, filterEnvelope=0.35, volume=0.32),
                        0.62, insert=3)
    crash = song.channel("Crash", sampler("Crash"), 0.3, insert=4)

    progression = [[54, 57, 61], [50, 54, 57], [54, 57, 61], [52, 56, 59]]
    bell_notes = arp(progression, [0, 2, 4, 2, 1, 3, 5, 3, 0, 2, 4, 5, 4, 2, 3, 1], rate=1, length=1, shift=12, velocity=0.62, accent=[1, 0.6, 0.8, 0.6])
    motifs = {54: [66, 78, 73, 69, 66, 78, 76, 73], 50: [62, 74, 69, 66, 62, 74, 73, 69], 52: [64, 76, 71, 68, 64, 76, 73, 71]}
    lead_notes = [((bar * 16 + i * 2) * STEP, 2 * STEP, key, 0.8 if i % 2 == 0 else 0.65)
                  for bar, chord in enumerate(progression) for i, key in enumerate(motifs[chord[0]])]
    roots = [42, 38, 42, 40]
    bass_notes = per_bar(4, lambda b: [((b * 16) * STEP, 4 * STEP, roots[b], 0.95), ((b * 16 + 4) * STEP, 2 * STEP, roots[b] + 12, 0.8),
                                       ((b * 16 + 7) * STEP, 3 * STEP, roots[b], 0.85), ((b * 16 + 10) * STEP, 6 * STEP, roots[b], 0.9)])
    hat_accent = [0.85, 0.35, 0.6, 0.35]

    def hat_bar(b):
        notes = [((b * 16 + st) * STEP, STEP, 60, hat_accent[st % 4]) for st in range(16) if not (b % 2 == 1 and st >= 12)]
        if b % 2 == 1:
            notes += [((b * 16 + 12) * STEP + i * 12, 12, 60 + i // 2, 0.4 + i * 0.07) for i in range(8)]
        return notes

    drums = {
        kick: steps([0, 7, 10], bar=0) + steps([0, 3, 7, 10, 14], bar=1) + steps([0, 7, 10], bar=2) + steps([0, 4, 7, 11, 13], bar=3),
        clap: [n for b in range(4) for n in steps([8], bar=b, velocity=0.85)] + steps([15], bar=3, velocity=0.4),
        hat: per_bar(4, hat_bar),
        open_hat: [n for b in range(4) for n in steps([2, 10], bar=b, length=2, velocity=0.45)],
    }
    switch = {
        kick: [n for b in range(4) for n in steps([0, 10], bar=b)],
        clap: [n for b in range(4) for n in steps([8], bar=b, velocity=0.85)],
        hat: [((b * 16 + st) * STEP + t * 16, 16, 62, 0.55 if t == 0 else 0.4) for b in range(4) for st in (0, 4, 8, 12) for t in range(3)],
    }

    p_drums = song.pattern("Rage drums", drums)
    p_switch = song.pattern("Beat switch drums", switch)
    p_bass = song.pattern("808", {bass: slides(bass_notes)})
    p_bells = song.pattern("Bells", {bells: bell_notes})
    p_lead = song.pattern("Rage lead", {lead: lead_notes})
    p_crash = song.pattern("Crash", {crash: steps([0], length=8, velocity=0.7)})

    song.tracks = {0: "Bells", 1: "Lead", 2: "Drums", 3: "808", 4: "FX"}
    for bar in range(0, 36, 4):
        section = bar // 4
        song.place(0, p_bells, bar, 4)
        if section in (1, 2, 5, 6, 7):
            song.place(1, p_lead, bar, 4)
        if 1 <= section <= 7:
            song.place(2, p_switch if section in (3, 4) else p_drums, bar, 4)
            song.place(3, p_bass, bar, 4)
        if section in (1, 3, 5):
            song.place(4, p_crash, bar, 1)
    song.insert(1, "Bells", [effect("Tomato Delay", time=3, feedback=0.3, wet=0.18), effect("Tomato Reverb", size=0.45, wet=0.18, dry=0.95)])
    song.insert(2, "808", [effect("Tomato Distortion", drive=16, tone=6000, output=-12), effect("Tomato Compressor", threshold=-10, ratio=4, attack=5, release=100, makeup=2)])
    song.insert(3, "Rage Lead", [effect("Tomato Distortion", drive=10, tone=9000, output=-10), effect("Tomato Chorus", rate=0.9, depth=0.45, wet=0.35),
                                 effect("Tomato Reverb", size=0.35, wet=0.15, dry=0.95)])
    song.insert(4, "Drums", [effect("Tomato Compressor", threshold=-9, ratio=3, attack=4, release=70, makeup=2), effect("Tomato Clipper", input=5, ceiling=-1)])
    song.master(master_chain(0.45, 3))
    song.write("Lyfe Rage (Yeat type beat).tsp")


def bedroom_tapes():
    song = Song("Bedroom Tapes (Alex G type beat)", 100.0, swing=0.55)
    kick = song.channel("Kick", sampler("Kick", pitch=-3), 0.7, insert=4)
    rim = song.channel("Rim", sampler("Rim"), 0.42, 0.1, insert=4)
    snare = song.channel("Snare", sampler("Snare", pitch=-3), 0.4, insert=4)
    hat = song.channel("Hat", sampler("Hat", pitch=-2), 0.32, 0.2, insert=4)
    shaker = song.channel("Shaker", sampler("Shaker"), 0.26, -0.25, insert=4)
    guitar = song.channel("Guitar", string(decay=2.8, brightness=0.45, damping=0.4, pick=0.2, release=220, width=0.5, volume=0.7), 0.74, insert=1)
    keys = song.channel("Rhodes", rhodes(tone=0.3, bell=0.2, decay=3.5, release=400, tremoloRate=4.0, tremoloDepth=0.35, volume=0.45), 0.6, insert=3)
    box = song.channel("Music Box", rhodes(tone=0.15, bell=1.0, decay=1.4, release=500, tremoloDepth=0.0, volume=0.35), 0.42, 0.2, insert=3)
    bass = song.channel("Bass", BASS(), 0.62, insert=2)

    progression = [[48, 52, 55, 59], [52, 55, 59, 62], [53, 57, 60, 67], [53, 56, 60, 62]]
    guitar_notes = arp(progression, [0, 2, 1, 3, 4, 2, 5, 3], rate=2, length=3, shift=12, velocity=0.7, accent=[1, 0.75, 0.85, 0.7])
    keys_notes = chords(progression, 16, [(0, 14)], 0.5)
    roots = [36, 40, 41, 41]
    bass_notes = per_bar(4, lambda b: [((b * 16) * STEP, 6 * STEP, roots[b], 0.75), ((b * 16 + 10) * STEP, 4 * STEP, roots[b] + (7 if b < 2 else 3), 0.6)])
    box_phrase = [(0, 4, 2, 76), (0, 8, 4, 79), (1, 2, 2, 74), (1, 6, 6, 71), (2, 4, 2, 72), (2, 8, 4, 77), (3, 0, 4, 75), (3, 6, 6, 72)]
    drums = {
        kick: [n for b in range(4) for n in steps([0, 6] + ([9] if b % 2 else []), bar=b, velocity=0.7)],
        rim: [n for b in range(4) for n in steps([4, 12], bar=b, velocity=0.6)],
        snare: [n for b in range(4) for n in steps([12], bar=b, velocity=0.4)],
        hat: [((b * 16 + st) * STEP, STEP, 60, 0.45 if st % 4 == 0 else 0.3) for b in range(4) for st in range(0, 16, 2)],
        shaker: [n for b in range(4) for n in steps([3, 7, 11, 15], bar=b, velocity=0.35)],
    }

    p_guitar = song.pattern("Guitar loop", {guitar: guitar_notes})
    p_drums = song.pattern("Soft drums", drums)
    p_bass = song.pattern("Bass", {bass: bass_notes})
    p_keys = song.pattern("Rhodes", {keys: keys_notes})
    p_box = song.pattern("Music box", {box: melody(box_phrase, 0.6)})

    song.tracks = {0: "Guitar", 1: "Rhodes", 2: "Music box", 3: "Drums", 4: "Bass"}
    for bar in range(0, 32, 4):
        section = bar // 4
        song.place(0, p_guitar, bar, 4)
        if section >= 2 and section != 5:
            song.place(1, p_keys, bar, 4)
        if section in (3, 4, 7):
            song.place(2, p_box, bar, 4)
        if 1 <= section <= 6 and section != 5:
            song.place(3, p_drums, bar, 4)
            song.place(4, p_bass, bar, 4)
    song.insert(1, "Guitar", [effect("Tomato Chorus", rate=0.35, depth=0.3, wet=0.3), effect("Tomato Filter", cutoff=6000, resonance=0.05),
                              effect("Tomato Reverb", size=0.4, wet=0.2, dry=0.95)])
    song.insert(2, "Bass", [effect("Tomato Compressor", threshold=-16, ratio=3, attack=12, release=150, makeup=2)])
    song.insert(3, "Keys", [effect("Tomato Chorus", rate=0.5, depth=0.4, wet=0.4), effect("Tomato Reverb", size=0.7, damping=0.6, wet=0.32, dry=0.85)])
    song.insert(4, "Drums", [effect("Tomato Filter", cutoff=7000, resonance=0.0), effect("Tomato Distortion", drive=6, tone=6000, output=-6),
                             effect("Tomato Compressor", threshold=-14, ratio=3, attack=15, release=120, makeup=3)])
    song.master([effect("Tomato Lofi", bits=14, wow=0.45, noise=0.25, tone=11000), effect("Tomato Maximizer", mode=0, amount=0.2, output=-1.0),
                 effect("Tomato Limiter", gain=0.0, ceiling=-0.5)])
    song.write("Bedroom Tapes (Alex G type beat).tsp")


def bad_boy_energy():
    song = Song("Bad Boy Energy (Juice WRLD type beat)", 150.0, swing=0.12)
    kick = song.channel("Kick", sampler("Kick Hard"), 0.8, insert=4)
    snare = song.channel("Snare", sampler("Snare Trap"), 0.55, insert=4)
    snap = song.channel("Snap", sampler("Snap"), 0.45, 0.15, insert=4)
    hat = song.channel("Hat", sampler("Hat"), 0.46, 0.12, insert=4)
    open_hat = song.channel("Open Hat", sampler("Open Hat"), 0.34, -0.15, insert=4)
    bass = song.channel("808", eight08(glide=90, decay=2.6, drive=9, tone=3500, punch=12), 0.88, insert=2)
    game = song.channel("Game Pluck", synth([("Square", 0, 0, 0.55), ("Square", 12, 5, 0.25), ("Triangle", 0, 0, 0.3)],
                                          attack=1, decay=180, sustain=0.1, release=160, cutoff=5200, resonance=0.1, filterEnvelope=0.3, volume=0.34),
                        0.62, insert=1)
    guitar = song.channel("Emo Guitar", string(decay=3.5, brightness=0.55, damping=0.3, pick=0.15, release=300, width=0.6, volume=0.7), 0.66, insert=3)
    glock = song.channel("Glock", rhodes(tone=0.1, bell=1.0, decay=1.2, release=400, tremoloDepth=0.0, volume=0.4), 0.42, 0.25, insert=1)
    pad = song.channel("Air Pad", PAD(), 0.48, insert=3)

    progression = [[57, 60, 64], [53, 57, 60], [55, 60, 64], [55, 59, 62]]
    game_notes = arp(progression, [3, 4, 5, 4, 3, 5, 4, 5], rate=2, length=1, shift=12, velocity=0.7, accent=[1, 0.7, 0.85, 0.7])
    guitar_notes = arp(progression, [0, 1, 2, 3, 2, 1, 3, 4], rate=2, length=3, shift=0, velocity=0.65)
    glock_notes = per_bar(4, lambda b: [((b * 16 + st) * STEP, 2 * STEP, progression[b][i % 3] + 24, 0.6) for i, st in enumerate((0, 6, 12))])
    roots = [33, 29, 36, 31]
    bass_notes = per_bar(4, lambda b: [((b * 16) * STEP, 6 * STEP, roots[b], 0.92), ((b * 16 + 6) * STEP, 2 * STEP, roots[b], 0.75),
                                       ((b * 16 + 10) * STEP, 4 * STEP, roots[b], 0.85), ((b * 16 + 14) * STEP, 2 * STEP, roots[b] + 12, 0.75)])

    def hats(b):
        notes = [((b * 16 + st) * STEP, STEP, 60, 0.7 if st % 4 == 0 else 0.45) for st in range(0, 16, 2) if not (b % 2 == 1 and st == 10)]
        if b % 2 == 1:
            notes += [((b * 16 + 10) * STEP + i * 16, 16, 61, 0.45 + i * 0.08) for i in range(3)]
        return notes

    drums = {
        kick: steps([0, 10], bar=0) + steps([0, 3, 11], bar=1) + steps([0, 10], bar=2) + steps([0, 6, 9, 13], bar=3),
        snare: [n for b in range(4) for n in steps([8], bar=b, velocity=0.85)],
        snap: [n for b in range(4) for n in steps([8], bar=b, velocity=0.5)] + steps([15], bar=1, velocity=0.35),
        hat: per_bar(4, hats),
        open_hat: [n for b in range(4) for n in steps([6, 14], bar=b, length=2, velocity=0.42)],
    }

    p_drums = song.pattern("Bounce drums", drums)
    p_bass = song.pattern("808", {bass: slides(bass_notes, transpose=0)})
    p_game = song.pattern("Game pluck", {game: game_notes})
    p_guitar = song.pattern("Emo guitar", {guitar: guitar_notes})
    p_glock = song.pattern("Glock", {glock: glock_notes})
    p_pad = song.pattern("Air pad", {pad: chords(progression, 16, [(0, 16)], 0.5)})

    song.tracks = {0: "Guitar", 1: "Pluck", 2: "Glock", 3: "Pad", 4: "Drums", 5: "808"}
    for bar in range(0, 36, 4):
        section = bar // 4
        song.place(0, p_guitar, bar, 4)
        song.place(3, p_pad, bar, 4)
        if section in (1, 2, 5, 6):
            song.place(1, p_game, bar, 4)
            song.place(2, p_glock, bar, 4)
        if 1 <= section <= 7:
            song.place(4, p_drums, bar, 4)
            song.place(5, p_bass, bar, 4)
    song.insert(1, "Pluck + Glock", [effect("Tomato Delay", time=3, feedback=0.35, wet=0.25), effect("Tomato Reverb", size=0.6, wet=0.25, dry=0.9)])
    song.insert(2, "808", [effect("Tomato Distortion", drive=8, tone=8000, output=-6), effect("Tomato Compressor", threshold=-12, ratio=3, attack=6, release=110, makeup=2)])
    song.insert(3, "Guitar + Pad", [effect("Tomato Chorus", rate=0.5, depth=0.35, wet=0.35), effect("Tomato Reverb", size=0.8, damping=0.5, wet=0.35, dry=0.8),
                                    effect("Tomato Pump", rate=1, depth=0.35, curve=2.2)], 0.9)
    song.insert(4, "Drums", [effect("Tomato Compressor", threshold=-11, ratio=2.5, attack=8, release=80, makeup=1.5), effect("Tomato Clipper", input=3, ceiling=-1)])
    song.master(master_chain(0.35, 1))
    song.write("Bad Boy Energy (Juice WRLD type beat).tsp")


def hit_em_hard():
    song = Song("Hit Em Hard (2Pac type beat)", 95.0, swing=0.3)
    kick = song.channel("Kick", sampler("Kick", pitch=-2), 0.86, insert=4)
    snare = song.channel("Snare", sampler("Snare", pitch=-2), 0.75, insert=4)
    clap = song.channel("Clap", sampler("Clap"), 0.45, insert=4)
    hat = song.channel("Hat", sampler("Hat"), 0.42, 0.15, insert=4)
    open_hat = song.channel("Open Hat", sampler("Open Hat"), 0.32, -0.15, insert=4)
    shaker = song.channel("Shaker", sampler("Shaker"), 0.24, 0.25, insert=4)
    bass = song.channel("Funk Bass", synth([("Saw", 0, 0, 0.6), ("Square", -12, 0, 0.35), ("Sine", 0, 0, 0.5)],
                                         attack=2, decay=240, sustain=0.45, release=70, cutoff=900, resonance=0.35, filterEnvelope=0.45, volume=0.55, glide=25),
                        0.8, insert=2)
    whine = song.channel("Whine Lead", synth([("Sine", 0, 0, 0.75), ("Triangle", 12, 0, 0.2), ("Saw", 0, 0, 0.08)],
                                           attack=25, decay=600, sustain=0.85, release=260, cutoff=6000, resonance=0.0, volume=0.32,
                                           glide=110, vibratoRate=5.5, vibratoDepth=28), 0.6, insert=1)
    stab = song.channel("Brass Stab", synth([("Saw", 0, -8, 0.5), ("Saw", 0, 8, 0.5), ("Square", -12, 0, 0.2)],
                                          attack=6, decay=220, sustain=0.25, release=140, cutoff=2200, resonance=0.15, filterEnvelope=0.5, volume=0.36),
                        0.55, insert=3)
    keys = song.channel("Piano", rhodes(tone=0.6, bell=0.35, decay=2.0, release=200, tremoloDepth=0.0, volume=0.42), 0.5, insert=3)

    line = [(0, 2, 47, 0.95), (3, 1, 47, 0.5), (4, 1, 50, 0.75), (6, 2, 47, 0.9), (8, 1, 42, 0.8), (10, 2, 45, 0.85), (12, 1, 47, 0.7),
            (14, 1, 50, 0.75), (15, 1, 49, 0.6)]
    bass_notes = per_bar(4, lambda b: [((b * 16 + st) * STEP, ln * STEP, key - (2 if b == 3 else 0), v) for st, ln, key, v in line])
    drums = {
        kick: steps([0, 7, 10], bar=0) + steps([0, 2, 10], bar=1) + steps([0, 7, 10], bar=2) + steps([0, 2, 7, 11], bar=3),
        snare: [n for b in range(4) for n in steps([4, 12], bar=b, velocity=0.95)],
        clap: [n for b in range(4) for n in steps([12], bar=b, velocity=0.5)],
        hat: [((b * 16 + st) * STEP, STEP, 60, 0.6 if st % 4 == 0 else 0.4) for b in range(4) for st in range(0, 16, 2) if st != 14],
        open_hat: [n for b in range(4) for n in steps([14], bar=b, length=2, velocity=0.5)],
        shaker: [n for b in range(4) for n in steps(list(range(1, 16, 2)), bar=b, velocity=0.3)],
    }
    whine_phrase = [(0, 0, 6, 78), (0, 6, 2, 81), (0, 8, 8, 83), (1, 0, 4, 81), (1, 4, 4, 78), (1, 8, 8, 76),
                    (2, 0, 6, 74), (2, 6, 2, 76), (2, 8, 8, 78), (3, 0, 4, 76), (3, 4, 4, 73), (3, 8, 8, 69)]
    stab_notes = per_bar(4, lambda b: [((b * 16 + st) * STEP, 2 * STEP, k - (2 if b == 3 else 0), 0.75) for st in (0, 3) for k in (59, 62, 66)] if b % 2 == 1 else [])
    piano_notes = per_bar(4, lambda b: [((b * 16 + st) * STEP, STEP, k - (2 if b == 3 else 0), 0.6) for st in (6, 14) for k in (71, 74, 78)])

    p_drums = song.pattern("Boom bap", drums)
    p_bass = song.pattern("Funk bass", {bass: bass_notes})
    p_whine = song.pattern("Whine lead", {whine: melody(whine_phrase, 0.75)})
    p_stab = song.pattern("Stabs", {stab: stab_notes})
    p_piano = song.pattern("Piano hits", {keys: piano_notes})
    p_intro = song.pattern("Intro", {hat: drums[hat], shaker: drums[shaker]})

    song.tracks = {0: "Whine", 1: "Stabs", 2: "Piano", 3: "Drums", 4: "Bass"}
    plan = ["intro", "verse", "verse", "verse", "verse", "hook", "hook", "verse", "verse", "verse", "verse", "hook", "hook", "outro"]
    for section, kind in enumerate(plan):
        bar = section * 4
        song.place(4, p_bass, bar, 4)
        if kind == "intro":
            song.place(3, p_intro, bar, 4)
            continue
        if kind != "outro":
            song.place(3, p_drums, bar, 4)
            song.place(2, p_piano, bar, 4)
        if kind in ("hook", "outro"):
            song.place(0, p_whine, bar, 4)
        if kind == "hook":
            song.place(1, p_stab, bar, 4)
    song.insert(1, "Whine", [effect("Tomato Delay", time=4, feedback=0.3, wet=0.2), effect("Tomato Reverb", size=0.5, wet=0.2, dry=0.9)])
    song.insert(2, "Bass", [effect("Tomato Compressor", threshold=-14, ratio=4, attack=4, release=90, makeup=3)])
    song.insert(3, "Keys", [effect("Tomato Reverb", size=0.35, wet=0.15, dry=0.95)])
    song.insert(4, "Drums", [effect("Tomato Distortion", drive=5, tone=9000, output=-4), effect("Tomato Compressor", threshold=-12, ratio=3.5, attack=10, release=80, makeup=2.5)])
    song.master([effect("Tomato Lofi", bits=16, wow=0.1, noise=0.12, tone=15000), effect("Tomato Maximizer", mode=1, amount=0.3, output=-1.0),
                 effect("Tomato Limiter", gain=0.0, ceiling=-0.5)])
    song.write("Hit Em Hard (2Pac type beat).tsp")


def sun_dreaming():
    song = Song("Sun Dreaming", 82.0, swing=0.55)
    kick = song.channel("Kick", sampler("Kick", pitch=-3), 0.72, insert=4)
    snare = song.channel("Snare", sampler("Snare", pitch=-2), 0.5, insert=4)
    hat = song.channel("Hat", sampler("Hat", pitch=-1), 0.34, 0.2, insert=4)
    shaker = song.channel("Shaker", sampler("Shaker"), 0.26, -0.25, insert=4)
    keys = song.channel("Rhodes", rhodes(tone=0.35, bell=0.25, decay=4.0, release=500, tremoloRate=3.5, tremoloDepth=0.3, volume=0.45), 0.66, insert=1)
    harp = song.channel("Harp", string(decay=4.0, brightness=0.7, damping=0.25, pick=0.3, release=600, width=0.7, volume=0.5), 0.5, insert=3)
    flute = song.channel("Dream Lead", synth([("Sine", 12, 0, 0.7), ("Triangle", 12, 4, 0.25), ("Noise", 0, 0, 0.03)],
                                           attack=70, decay=500, sustain=0.75, release=420, cutoff=5200, resonance=0.0, volume=0.3,
                                           glide=60, vibratoRate=4.8, vibratoDepth=14), 0.5, insert=3)
    bass = song.channel("Sub", synth([("Sine", 0, 0, 0.9), ("Triangle", 0, 0, 0.2), ("Square", 0, 0, 0.0)],
                                    attack=8, decay=500, sustain=0.8, release=200, cutoff=500, resonance=0.0, volume=0.55), 0.66, insert=2)

    progression = [[50, 54, 57, 61], [47, 50, 54, 57], [55, 59, 62, 66], [57, 61, 64, 66]]
    keys_notes = chords(progression, 16, [(0, 7), (8, 7)], 0.55)
    harp_notes = arp(progression, [0, 1, 2, 3, 4, 5, 6, 7], rate=2, length=4, shift=12, velocity=0.5, accent=[1, 0.7, 0.8, 0.7])
    roots = [38, 35, 43, 45]
    bass_notes = per_bar(4, lambda b: [((b * 16) * STEP, 7 * STEP, roots[b], 0.8), ((b * 16 + 8) * STEP, 6 * STEP, roots[b] + (12 if b % 2 else 7), 0.65)])
    flute_phrase = [(0, 4, 4, 78), (0, 8, 6, 81), (1, 0, 4, 78), (1, 4, 8, 74), (2, 2, 4, 79), (2, 6, 6, 83), (3, 0, 8, 81), (3, 8, 6, 76)]
    drums = {
        kick: steps([0, 7], bar=0) + steps([0, 6, 10], bar=1) + steps([0, 7], bar=2) + steps([0, 6, 9, 11], bar=3),
        snare: [n for b in range(4) for n in steps([4, 12], bar=b, velocity=0.6)],
        hat: [((b * 16 + st) * STEP, STEP, 60, 0.42 if st % 4 == 0 else 0.26) for b in range(4) for st in range(0, 16, 2)],
        shaker: [n for b in range(4) for n in steps(list(range(1, 16, 2)), bar=b, velocity=0.25)],
    }

    p_keys = song.pattern("Rhodes", {keys: keys_notes})
    p_harp = song.pattern("Harp", {harp: harp_notes})
    p_flute = song.pattern("Dream lead", {flute: melody(flute_phrase, 0.65)})
    p_drums = song.pattern("Lazy drums", drums)
    p_bass = song.pattern("Sub", {bass: bass_notes})

    song.tracks = {0: "Rhodes", 1: "Harp", 2: "Lead", 3: "Drums", 4: "Sub"}
    for bar in range(0, 32, 4):
        section = bar // 4
        song.place(0, p_keys, bar, 4)
        if section != 0:
            song.place(1, p_harp, bar, 4)
        if section in (3, 4, 6):
            song.place(2, p_flute, bar, 4)
        if 1 <= section <= 6:
            song.place(3, p_drums, bar, 4)
            song.place(4, p_bass, bar, 4)
    song.insert(1, "Rhodes", [effect("Tomato Chorus", rate=0.4, depth=0.35, wet=0.35), effect("Tomato Filter", cutoff=5000, resonance=0.05),
                              effect("Tomato Reverb", size=0.7, wet=0.28, dry=0.85)])
    song.insert(2, "Sub", [effect("Tomato Compressor", threshold=-16, ratio=3, attack=20, release=200, makeup=2)])
    song.insert(3, "Dreams", [effect("Tomato Delay", time=6, feedback=0.4, wet=0.25, damp=3000), effect("Tomato Reverb", size=0.92, damping=0.55, wet=0.42, dry=0.75)], 0.9)
    song.insert(4, "Drums", [effect("Tomato Filter", cutoff=6500, resonance=0.0), effect("Tomato Distortion", drive=5, tone=5500, output=-5),
                             effect("Tomato Compressor", threshold=-13, ratio=3, attack=15, release=110, makeup=2.5)])
    song.master([effect("Tomato Lofi", bits=15, wow=0.4, noise=0.22, tone=12000), effect("Tomato Maximizer", mode=0, amount=0.2, output=-1.0),
                 effect("Tomato Limiter", gain=0.0, ceiling=-0.5)])
    song.write("Sun Dreaming.tsp")


def pixel_heartache():
    song = Song("Pixel Heartache (misery type beat)", 155.0)
    kick = song.channel("Kick", sampler("Kick Hard"), 0.82, insert=4)
    snare = song.channel("Snare", sampler("Snare Trap"), 0.62, insert=4)
    clap = song.channel("Clap", sampler("Clap Big"), 0.5, insert=4)
    hat = song.channel("Hat", sampler("Hat Tight"), 0.46, 0.12, insert=4)
    open_hat = song.channel("Open Hat", sampler("Open Hat"), 0.3, -0.15, insert=4)
    lead = song.channel("Chip Lead", synth([("Square", 0, 0, 0.6), ("Square", 12, 7, 0.22), ("Triangle", 0, 0, 0.0)],
                                         attack=1, decay=160, sustain=0.7, release=70, cutoff=12000, resonance=0.0, volume=0.32,
                                         glide=18, vibratoRate=6.5, vibratoDepth=16), 0.6, insert=1)
    arp_channel = song.channel("Chip Arp", synth([("Square", 0, 0, 0.5), ("Square", -12, 0, 0.0), ("Triangle", 0, 0, 0.0)],
                                               attack=1, decay=90, sustain=0.15, release=40, cutoff=9000, resonance=0.0, volume=0.3), 0.46, 0.18, insert=1)
    guitar = song.channel("Fuzz Guitar", string(decay=4.0, brightness=0.8, damping=0.15, pick=0.12, release=90, width=0.8, volume=0.75), 0.62, insert=3)
    bass = song.channel("Throb Bass", synth([("Saw", 0, 0, 0.55), ("Square", -12, 0, 0.4), ("Sine", -12, 0, 0.5)],
                                          attack=2, decay=180, sustain=0.6, release=60, cutoff=1100, resonance=0.3, filterEnvelope=0.4, volume=0.5), 0.72, insert=2)
    sfx = song.channel("Game SFX", synth([("Square", 0, 0, 0.5), ("Triangle", 12, 0, 0.3), ("Sine", 0, 0, 0.0)],
                                       attack=1, decay=120, sustain=0.4, release=60, cutoff=12000, volume=0.28, glide=55), 0.45, -0.2, insert=1)
    crash = song.channel("Crash", sampler("Crash"), 0.32, insert=4)

    progression = [[62, 66, 69], [61, 64, 69], [59, 62, 66], [59, 62, 67]]
    power = [[50, 57, 62], [45, 52, 57], [47, 54, 59], [43, 50, 55]]
    roots = [38, 33, 35, 31]
    lead_phrase = [
        (0, 0, 2, 78), (0, 2, 2, 81), (0, 4, 2, 78), (0, 6, 2, 76), (0, 8, 3, 74), (0, 11, 1, 76), (0, 12, 4, 78),
        (1, 0, 2, 76), (1, 2, 2, 73), (1, 4, 2, 76), (1, 6, 2, 81), (1, 8, 4, 80), (1, 12, 2, 76), (1, 14, 2, 73),
        (2, 0, 2, 74), (2, 2, 2, 78), (2, 4, 2, 83), (2, 6, 2, 81), (2, 8, 3, 78), (2, 11, 1, 76), (2, 12, 4, 74),
        (3, 0, 2, 71), (3, 2, 2, 74), (3, 4, 2, 79), (3, 6, 2, 78), (3, 8, 4, 76), (3, 12, 2, 78), (3, 14, 2, 74),
    ]
    arp_notes = arp(progression, [0, 1, 2, 3, 4, 5, 4, 3, 2, 3, 4, 5, 6, 5, 4, 3], rate=1, length=1, shift=12, velocity=0.55, accent=[1, 0.6, 0.75, 0.6])
    guitar_notes = [((bar * 16 + st) * STEP, 2 * STEP, key, 0.75 if st % 4 == 0 else 0.6) for bar, chord in enumerate(power) for st in range(0, 16, 2) for key in chord]
    bass_notes = [((bar * 16 + st) * STEP, STEP, root + (12 if st in (6, 14) else 0), 0.85 if st % 4 == 0 else 0.7) for bar, root in enumerate(roots) for st in range(0, 16, 2)]
    sfx_notes = [(0, STEP, 84, 0.7), (STEP, STEP, 96, 0.7), (2 * STEP, 2 * STEP, 91, 0.6), (3 * 16 * STEP + 12 * STEP, STEP, 72, 0.6), (3 * 16 * STEP + 13 * STEP, 3 * STEP, 96, 0.7)]

    def hats(b):
        notes = [((b * 16 + st) * STEP, STEP, 60, 0.7 if st % 4 == 0 else 0.45) for st in range(0, 16, 2) if not (b == 3 and st >= 12)]
        if b == 3:
            notes += [((b * 16 + 12) * STEP + i * 12, 12, 60 + i, 0.4 + i * 0.07) for i in range(8)]
        return notes

    drums = {
        kick: [n for b in range(4) for n in steps([0, 6, 10] if b % 2 == 0 else [0, 3, 6, 10, 13], bar=b, velocity=0.9)],
        snare: [n for b in range(4) for n in steps([4, 12], bar=b, velocity=0.9)] + steps([14, 15], bar=3, velocity=0.6),
        clap: [n for b in range(4) for n in steps([4, 12], bar=b, velocity=0.6)],
        hat: per_bar(4, hats),
        open_hat: [n for b in range(4) for n in steps([10], bar=b, length=2, velocity=0.45)],
    }

    p_drums = song.pattern("Blown drums", drums)
    p_lead = song.pattern("Chip lead", {lead: melody(lead_phrase, 0.8)})
    p_arp = song.pattern("Chip arp", {arp_channel: arp_notes})
    p_guitar = song.pattern("Fuzz guitar", {guitar: guitar_notes})
    p_bass = song.pattern("Throb bass", {bass: bass_notes})
    p_sfx = song.pattern("Game SFX", {sfx: sfx_notes})
    p_crash = song.pattern("Crash", {crash: steps([0], length=8, velocity=0.7)})

    song.tracks = {0: "Lead", 1: "Arp", 2: "Guitar", 3: "Drums", 4: "Bass", 5: "SFX"}
    plan = ["intro", "hook", "hook", "verse", "verse", "hook", "hook", "bridge", "hook", "hook", "outro"]
    for section, kind in enumerate(plan):
        bar = section * 4
        song.place(1, p_arp, bar, 4)
        if kind in ("hook", "bridge", "outro"):
            song.place(2, p_guitar, bar, 4)
        if kind == "hook":
            song.place(0, p_lead, bar, 4)
        if kind in ("hook", "verse"):
            song.place(3, p_drums, bar, 4)
            song.place(4, p_bass, bar, 4)
        if kind in ("intro", "bridge") or (kind == "hook" and plan[section - 1] != "hook"):
            song.place(5, p_sfx, bar, 4)
            song.place(5, p_crash, bar, 1)
    song.insert(1, "Chip", [effect("Tomato Delay", time=3, feedback=0.3, wet=0.2), effect("Tomato Reverb", size=0.4, wet=0.15, dry=0.95)])
    song.insert(2, "Bass", [effect("Tomato Distortion", drive=8, tone=5000, output=-6), effect("Tomato Pump", rate=1, depth=0.55, curve=1.6)])
    song.insert(3, "Fuzz", [effect("Tomato Distortion", drive=26, tone=5500, output=-16), effect("Tomato Chorus", rate=0.6, depth=0.3, wet=0.3),
                            effect("Tomato Filter", cutoff=6500, resonance=0.05)], 0.8)
    song.insert(4, "Drums", [effect("Tomato Distortion", drive=12, tone=11000, output=-9), effect("Tomato Clipper", input=8, ceiling=-1, mode=1)])
    song.master(master_chain(0.5, 3))
    song.write("Pixel Heartache (misery type beat).tsp")


ALL = [lyfe_rage, bedroom_tapes, bad_boy_energy, hit_em_hard, sun_dreaming, pixel_heartache]
