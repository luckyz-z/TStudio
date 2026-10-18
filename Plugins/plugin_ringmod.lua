plugin = {
  name = "Lua Ring Mod",
  type = "effect",
  author = "TStudio",
  description = "Ring modulator with LFO sweep for robotic and metallic tones",
  params = {
    { id = "frequency", name = "Frequency", min = 20, max = 2000, default = 220, unit = "Hz" },
    { id = "sweep", name = "LFO sweep", min = 0, max = 1, default = 0.2 },
    { id = "rate", name = "LFO rate", min = 0.05, max = 8, default = 0.5, unit = "Hz" },
    { id = "mix", name = "Mix", min = 0, max = 1, default = 0.5 },
  },
  presets = {
    { name = "Robot voice", frequency = 90, sweep = 0, mix = 0.8 },
    { name = "Metal sweep", frequency = 600, sweep = 0.6, rate = 0.3, mix = 0.6 },
  },
}

local sin = math.sin
local carrier, lfo = 0, 0

function process(l, r)
  lfo = (lfo + params.rate / sample_rate) % 1
  carrier = (carrier + params.frequency * (1 + params.sweep * sin(TWO_PI * lfo)) / sample_rate) % 1
  local c = sin(TWO_PI * carrier)
  local mix = params.mix
  return l * (1 - mix) + l * c * mix, r * (1 - mix) + r * c * mix
end
