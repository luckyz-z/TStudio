plugin = {
  name = "Lua Tremolo",
  type = "effect",
  author = "TStudio",
  description = "Tempo-synced tremolo with shapes and stereo offset",
  params = {
    { id = "rate", name = "Rate", choices = { "1/4", "1/8", "1/16", "1/8 triplet", "1/32" }, default = 1 },
    { id = "depth", name = "Depth", min = 0, max = 1, default = 0.6 },
    { id = "shape", name = "Shape", choices = { "Sine", "Square", "Saw down" }, default = 0 },
    { id = "stereo", name = "Stereo offset", min = 0, max = 0.5, default = 0 },
  },
  presets = {
    { name = "Choppy 1/16", rate = 2, depth = 1, shape = 1 },
    { name = "Gentle wobble", rate = 0, depth = 0.35, shape = 0 },
    { name = "Ping-pong", rate = 1, depth = 0.8, shape = 0, stereo = 0.5 },
  },
  meters = { "phase" },
  ui = {
    width = 560, height = 230,
    widgets = {
      { type = "label", text = "RATE", x = 16, y = 12, w = 120, h = 16, color = "#8f9a9f", bold = true, size = 12 },
      { type = "choice", param = "rate", x = 16, y = 30, w = 120, h = 24 },
      { type = "label", text = "SHAPE", x = 16, y = 64, w = 120, h = 16, color = "#8f9a9f", bold = true, size = 12 },
      { type = "choice", param = "shape", x = 16, y = 82, w = 120, h = 24 },
      { type = "knob", param = "depth", x = 10, y = 120, w = 64, h = 96 },
      { type = "knob", param = "stereo", label = "Stereo", x = 76, y = 120, w = 64, h = 96, color = "#ffc857" },
    },
  },
}

local sin, floor = math.sin, math.floor
local lengths = { 1, 0.5, 0.25, 1 / 3, 0.125 }
local position, step = 0, 0

function block(frames)
  position = beat
  step = tempo / 60 / sample_rate
  meters.phase = (beat / lengths[floor(params.rate) + 1]) % 1
end

local function wave(phase, shape)
  if shape == 0 then return 0.5 + 0.5 * sin(TWO_PI * phase) end
  if shape == 1 then return phase < 0.5 and 1 or 0 end
  return 1 - phase
end

function process(l, r)
  local length = lengths[floor(params.rate) + 1]
  local phase = (position / length) % 1
  local depth, shape = params.depth, floor(params.shape)
  position = position + step
  return l * (1 - depth * (1 - wave(phase, shape))), r * (1 - depth * (1 - wave((phase + params.stereo) % 1, shape)))
end

function draw(w, h)
  local x, y, pw, ph = 156, 12, w - 168, h - 24
  gfx.color("#121618")
  gfx.fill_rect(x, y, pw, ph, 6)
  gfx.color("#252b2f")
  for i = 1, 7 do gfx.line(x + pw * i / 8, y, x + pw * i / 8, y + ph) end
  gfx.line(x, y + ph / 2, x + pw, y + ph / 2)
  local depth, shape = params.depth, floor(params.shape)
  local function curve(offset, colour)
    local points = {}
    for i = 0, 160 do
      local phase = (i / 80 + offset) % 1
      local gain = 1 - depth * (1 - wave(phase, shape))
      points[#points + 1] = x + 6 + (pw - 12) * i / 160
      points[#points + 1] = y + ph - 10 - (ph - 20) * gain
    end
    gfx.color(colour)
    gfx.polyline(points, 2)
  end
  if params.stereo > 0 then curve(params.stereo, "#ffc857") end
  curve(0, "#e8483b")
  local head = x + 6 + (pw - 12) * ((meters.phase or 0) / 2)
  gfx.color(123, 224, 160, 200)
  gfx.line(head, y + 4, head, y + ph - 4, 1.5)
  gfx.color("#8f9a9f")
  gfx.text(plugin.params[1].choices[floor(params.rate) + 1] or "", x + 10, y + 6, 120, 16, 12, "left", true)
end
