plugin = {
  name = "Lua Kick",
  type = "instrument",
  author = "TStudio",
  description = "Kick drum generator: pitch sweep, click and drive",
  params = {
    { id = "start", name = "Start pitch", min = 80, max = 600, default = 240, unit = "Hz" },
    { id = "finish", name = "End pitch", min = 30, max = 120, default = 50, unit = "Hz" },
    { id = "sweep", name = "Sweep", min = 5, max = 200, default = 35, unit = "ms" },
    { id = "decay", name = "Decay", min = 0.05, max = 2, default = 0.45, unit = "s" },
    { id = "click", name = "Click", min = 0, max = 1, default = 0.4 },
    { id = "drive", name = "Drive", min = 0, max = 24, default = 6, unit = "dB" },
  },
  presets = {
    { name = "Trap knock", start = 300, finish = 52, sweep = 25, decay = 0.4, click = 0.6, drive = 9 },
    { name = "Boom", start = 180, finish = 42, sweep = 60, decay = 1.2, click = 0.2, drive = 4 },
    { name = "Techno thud", start = 220, finish = 48, sweep = 40, decay = 0.3, click = 0.5, drive = 14 },
  },
}

local exp, sin, random = math.exp, math.sin, math.random
local active, t, phase, velocity = false, 0, 0, 1

local function tanh(x)
  local e = exp(2 * x)
  return (e - 1) / (e + 1)
end

function note_on(key, v)
  active, t, phase, velocity = true, 0, 0, v
end

function note_off(key)
end

function render()
  if not active then return 0, 0 end
  local p = params
  local frequency = p.finish + (p.start - p.finish) * exp(-t / (p.sweep * 0.001))
  phase = (phase + frequency / sample_rate) % 1
  local body = sin(TWO_PI * phase) * exp(-t / p.decay * 3)
  local click = (random() * 2 - 1) * exp(-t * 900) * p.click
  local drive = db_to_gain(p.drive)
  local out = tanh((body + click) * drive) / tanh(drive) * velocity * 0.9
  t = t + 1 / sample_rate
  if t > p.decay * 3 then active = false end
  return out, out
end
