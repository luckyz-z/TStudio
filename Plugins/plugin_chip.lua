plugin = {
  name = "Lua Chip",
  type = "instrument",
  author = "TStudio",
  description = "8-bit pulse synth with console-style vibrato",
  params = {
    { id = "duty", name = "Duty", choices = { "12.5%", "25%", "50%" }, default = 1 },
    { id = "decay", name = "Decay", min = 0.05, max = 3, default = 0.6, unit = "s" },
    { id = "sustain", name = "Sustain", min = 0, max = 1, default = 0.5 },
    { id = "release", name = "Release", min = 0.01, max = 2, default = 0.12, unit = "s" },
    { id = "vibrato", name = "Vibrato", min = 0, max = 50, default = 12, unit = "ct" },
    { id = "volume", name = "Volume", min = 0, max = 1, default = 0.5 },
  },
  presets = {
    { name = "Lead", duty = 1, sustain = 0.7, vibrato = 16 },
    { name = "Blip", duty = 0, decay = 0.12, sustain = 0, vibrato = 0 },
  },
}

local exp, sin, floor = math.exp, math.sin, math.floor
local duties = { 0.125, 0.25, 0.5 }
local voices = {}
local decayCoef, releaseCoef, vibratoPhase = 0.999, 0.99, 0

function block(frames)
  decayCoef = exp(-1 / (params.decay * sample_rate))
  releaseCoef = exp(-1 / (params.release * sample_rate))
end

function note_on(key, velocity)
  voices[#voices + 1] = { key = key, frequency = note_freq(key), phase = 0, level = 1, velocity = velocity, released = false }
  if #voices > 8 then table.remove(voices, 1) end
end

function note_off(key)
  for _, voice in ipairs(voices) do
    if voice.key == key then voice.released = true end
  end
end

function render()
  vibratoPhase = (vibratoPhase + 5.5 / sample_rate) % 1
  local wobble = 2 ^ (params.vibrato / 1200 * sin(TWO_PI * vibratoPhase))
  local duty = duties[floor(params.duty) + 1]
  local sustain = params.sustain
  local out = 0
  for i = #voices, 1, -1 do
    local voice = voices[i]
    voice.phase = (voice.phase + voice.frequency * wobble / sample_rate) % 1
    if voice.released then
      voice.level = voice.level * releaseCoef
    else
      voice.level = sustain + (voice.level - sustain) * decayCoef
    end
    out = out + (voice.phase < duty and 1 or -1) * voice.level * voice.velocity
    if voice.level < 0.0005 then table.remove(voices, i) end
  end
  out = out * params.volume * 0.25
  return out, out
end
