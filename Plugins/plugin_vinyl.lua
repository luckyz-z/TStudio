plugin = {
  name = "Lua Vinyl",
  type = "effect",
  author = "TStudio",
  description = "Record player: crackle, wow and flutter, worn top end",
  params = {
    { id = "crackle", name = "Crackle", min = 0, max = 1, default = 0.35 },
    { id = "wow", name = "Wow", min = 0, max = 1, default = 0.3 },
    { id = "tone", name = "Tone", min = 0, max = 1, default = 0.4 },
    { id = "mix", name = "Mix", min = 0, max = 1, default = 1 },
  },
  presets = {
    { name = "Dusty", crackle = 0.6, wow = 0.2, tone = 0.6 },
    { name = "Warped", crackle = 0.25, wow = 0.85, tone = 0.45 },
    { name = "Clean press", crackle = 0.08, wow = 0.08, tone = 0.15 },
  },
  meters = { "angle", "pop" },
  ui = {
    width = 560, height = 220, background = "#1d1b1a", accent = "#e8483b",
    widgets = {
      { type = "knob", param = "crackle", x = 300, y = 18, w = 76, h = 96 },
      { type = "knob", param = "wow", x = 384, y = 18, w = 76, h = 96, color = "#ffc857" },
      { type = "knob", param = "tone", x = 468, y = 18, w = 76, h = 96, color = "#7be0a0" },
      { type = "hslider", param = "mix", x = 300, y = 150, w = 244, h = 24 },
      { type = "label", text = "MIX", x = 300, y = 130, w = 100, h = 16, color = "#8f9a9f", bold = true, size = 11 },
      { type = "meter", meter = "pop", x = 300, y = 186, w = 244, h = 8, color = "#ffc857" },
    },
  },
}

local sin, random, exp = math.sin, math.random, math.exp
local size = 4096
local bufferL, bufferR = {}, {}
for i = 0, size - 1 do bufferL[i], bufferR[i] = 0, 0 end
local write, wowPhase, flutterPhase = 0, 0, 0
local lowL, lowR, coef, pop, angle = 0, 0, 0.5, 0, 0
local popLevel = 0
local quiet, hold, fade = math.huge, 44100, 22050

function block(frames)
  coef = 1 - exp(-TWO_PI * (16000 - params.tone * 13000) / sample_rate)
  hold, fade = sample_rate, sample_rate * 0.5
  angle = (angle + frames / sample_rate * 0.55 * TWO_PI) % TWO_PI
  meters.angle = angle
  meters.pop = popLevel
  popLevel = popLevel * 0.6
end

local function read(buffer, delay)
  local position = write - delay
  local index = math.floor(position)
  local fraction = position - index
  local a = buffer[index % size]
  local b = buffer[(index + 1) % size]
  return a + (b - a) * fraction
end

function process(l, r)
  bufferL[write % size], bufferR[write % size] = l, r
  wowPhase = wowPhase + 0.55 / sample_rate
  flutterPhase = flutterPhase + 7.3 / sample_rate
  local depth = params.wow * sample_rate * 0.0025
  local delay = 8 + depth * (1 + sin(TWO_PI * wowPhase)) + depth * 0.08 * (1 + sin(TWO_PI * flutterPhase))
  local wl, wr = read(bufferL, delay), read(bufferR, delay)
  write = write + 1
  lowL = lowL + (wl - lowL) * coef
  lowR = lowR + (wr - lowR) * coef
  local noise = 0
  if random() < params.crackle * 0.0009 then
    pop = (random() - 0.5) * params.crackle * 0.6
    if pop * pop > popLevel then popLevel = pop * pop * 4 end
  end
  if l * l + r * r > 1e-7 then quiet = 0 else quiet = quiet + 1 end
  local presence = quiet < hold and 1 or math.max(0, 1 - (quiet - hold) / fade)
  noise = (pop + (random() - 0.5) * params.crackle * 0.004) * presence
  pop = pop * 0.82
  local mix = params.mix
  return l + (lowL + noise - l) * mix, r + (lowR + noise - r) * mix
end

function draw(w, h)
  local cx, cy, radius = 150, h / 2, 92
  gfx.color("#0f0e0d")
  gfx.fill_rect(12, 12, 276, h - 24, 8)
  gfx.color("#070707")
  gfx.fill_circle(cx, cy, radius)
  gfx.color("#1b1a19")
  for ring = 40, 88, 6 do gfx.circle(cx, cy, ring, 1) end
  gfx.color("#e8483b")
  gfx.fill_circle(cx, cy, 30)
  local a = meters.angle or 0
  gfx.color("#ffffff")
  gfx.line(cx + math.cos(a) * 12, cy + math.sin(a) * 12, cx + math.cos(a) * 26, cy + math.sin(a) * 26, 2)
  gfx.color("#1d1b1a")
  gfx.fill_circle(cx, cy, 4)
  local wobble = (params.wow or 0) * 6 * math.sin(a * 2)
  gfx.color("#b9bfc2")
  gfx.line(268, 30, 214 + wobble, 140, 4)
  gfx.fill_circle(268, 30, 8)
  gfx.color("#8f9a9f")
  gfx.text("33 1/3", 22, h - 34, 80, 14, 10, "left", true)
end
