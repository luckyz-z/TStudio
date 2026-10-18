plugin = {
  name = "Lua FM Bell",
  type = "instrument",
  author = "TStudio",
  description = "Two-operator FM bells, plucks and glassy keys",
  params = {
    { id = "ratio", name = "Ratio", min = 0.5, max = 8, default = 3.5 },
    { id = "index", name = "Index", min = 0, max = 10, default = 3 },
    { id = "decay", name = "Decay", min = 0.1, max = 6, default = 1.8, unit = "s" },
    { id = "brightness", name = "Brightness decay", min = 0.05, max = 3, default = 0.5, unit = "s" },
    { id = "volume", name = "Volume", min = 0, max = 1, default = 0.45 },
  },
  presets = {
    { name = "Glass bell", ratio = 3.5, index = 3, decay = 2.5, brightness = 0.6 },
    { name = "Tubular", ratio = 1.4, index = 5, decay = 4, brightness = 1.5 },
    { name = "FM pluck", ratio = 1, index = 6, decay = 0.4, brightness = 0.1 },
  },
}

local exp, sin = math.exp, math.sin
local voices = {}

function note_on(key, velocity)
  voices[#voices + 1] = { key = key, frequency = note_freq(key), carrier = 0, modulator = 0, t = 0, velocity = velocity, released = false, gate = 1 }
  if #voices > 12 then table.remove(voices, 1) end
end

function note_off(key)
  for _, voice in ipairs(voices) do
    if voice.key == key then voice.released = true end
  end
end

function render()
  local p = params
  local step = 1 / sample_rate
  local out = 0
  for i = #voices, 1, -1 do
    local v = voices[i]
    v.modulator = (v.modulator + v.frequency * p.ratio * step) % 1
    v.carrier = (v.carrier + v.frequency * step) % 1
    if v.released then v.gate = v.gate * 0.9995 end
    local amp = exp(-v.t / p.decay) * v.gate * v.velocity
    out = out + sin(TWO_PI * v.carrier + p.index * exp(-v.t / p.brightness) * sin(TWO_PI * v.modulator)) * amp
    v.t = v.t + step
    if amp < 0.0005 then table.remove(voices, i) end
  end
  out = out * p.volume * 0.3
  return out, out
end
