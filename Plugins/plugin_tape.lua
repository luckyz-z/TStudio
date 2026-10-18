plugin = {
  name = "Lua Tape",
  type = "effect",
  author = "TStudio",
  description = "Warm tape saturation with head bump and soft top end",
  params = {
    { id = "drive", name = "Drive", min = 0, max = 24, default = 6, unit = "dB" },
    { id = "warmth", name = "Warmth", min = 0, max = 1, default = 0.5 },
    { id = "bias", name = "Bias", min = -0.3, max = 0.3, default = 0.05 },
    { id = "output", name = "Output", min = -18, max = 6, default = -3, unit = "dB" },
  },
  presets = { { name = "Glue", drive = 4, warmth = 0.4 }, { name = "Hot tape", drive = 14, warmth = 0.7, output = -8 } },
  meters = { "level" },
  ui = {
    width = 560, height = 220, background = "#2a2422", accent = "#f0a04b",
    widgets = {
      { type = "knob", param = "drive", x = 300, y = 16, w = 76, h = 100 },
      { type = "knob", param = "warmth", x = 384, y = 16, w = 76, h = 100 },
      { type = "knob", param = "bias", x = 468, y = 16, w = 76, h = 100, color = "#ffc857" },
      { type = "knob", param = "output", x = 300, y = 120, w = 76, h = 92, color = "#d6dde0" },
      { type = "label", text = "OUTPUT LEVEL", x = 388, y = 136, w = 156, h = 16, color = "#a89a90", bold = true, size = 11 },
      { type = "meter", meter = "level", x = 388, y = 158, w = 156, h = 10 },
    },
  },
}

local exp = math.exp
local lowL, lowR, bumpL, bumpR = 0, 0, 0, 0
local inGain, outGain, toneCoef, offset = 1, 1, 0.5, 0
local peak, spin = 0, 0

local function tanh(x)
  local e = exp(2 * x)
  return (e - 1) / (e + 1)
end

function block(frames)
  inGain = db_to_gain(params.drive)
  outGain = db_to_gain(params.output)
  toneCoef = 1 - exp(-TWO_PI * (18000 - params.warmth * 12000) / sample_rate)
  offset = tanh(params.bias)
  meters.level = peak
  peak = 0
end

local function saturate(x)
  return tanh(clamp(x * inGain + params.bias, -12, 12)) - offset
end

function process(l, r)
  lowL = lowL + (saturate(l) - lowL) * toneCoef
  lowR = lowR + (saturate(r) - lowR) * toneCoef
  bumpL = bumpL + (lowL - bumpL) * 0.002
  bumpR = bumpR + (lowR - bumpR) * 0.002
  local bump = params.warmth * 0.6
  local outL, outR = (lowL + bumpL * bump) * outGain, (lowR + bumpR * bump) * outGain
  local loud = outL > 0 and outL or -outL
  if loud > peak then peak = loud end
  return outL, outR
end

local function reel(cx, cy, r, angle)
  gfx.color("#1a1614")
  gfx.fill_circle(cx, cy, r)
  gfx.color("#4a403b")
  gfx.circle(cx, cy, r, 2)
  gfx.color("#3a312d")
  gfx.fill_circle(cx, cy, r * 0.62)
  for i = 0, 2 do
    local a = angle + i * TWO_PI / 3
    gfx.color("#1a1614")
    gfx.fill_circle(cx + math.cos(a) * r * 0.36, cy + math.sin(a) * r * 0.36, r * 0.14)
  end
  gfx.color("#d6dde0")
  gfx.fill_circle(cx, cy, r * 0.08)
end

function draw(w, h)
  spin = spin + 0.05 + (meters.level or 0) * 0.2
  gfx.color("#1f1a18")
  gfx.fill_rect(12, 12, 276, h - 24, 8)
  reel(84, 92, 58, spin)
  reel(216, 92, 58, spin * 0.8)
  gfx.color("#6b4a32")
  gfx.line(84, 150, 216, 150, 3)
  local gain, bias = db_to_gain(params.drive), params.bias
  local zero = tanh(bias)
  local points = {}
  for i = 0, 60 do
    local x = -1 + i / 30
    points[#points + 1] = 24 + i * 4
    points[#points + 1] = 184 - (tanh(clamp(x * gain + bias, -12, 12)) - zero) * 16
  end
  gfx.color("#f0a04b")
  gfx.polyline(points, 2)
  gfx.color("#a89a90")
  gfx.text("TRANSFER", 24, 160, 100, 14, 10, "left", true)
end
