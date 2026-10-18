plugin = {
  name = "Lua Stutter",
  type = "effect",
  author = "TStudio",
  description = "Beat-repeat glitch: grabs a slice and loops it in time",
  params = {
    { id = "division", name = "Slice", choices = { "1/4", "1/8", "1/16", "1/32" }, default = 2 },
    { id = "when", name = "When", choices = { "Always", "Last beat of bar", "Every 2nd bar", "Off" }, default = 1 },
    { id = "mix", name = "Mix", min = 0, max = 1, default = 1 },
    { id = "fade", name = "Fade", min = 0, max = 1, default = 0.2 },
  },
  presets = {
    { name = "Fill on beat 4", division = 2, when = 1, mix = 1 },
    { name = "Machine gun", division = 3, when = 0, mix = 1, fade = 0.4 },
    { name = "Half-bar repeat", division = 0, when = 2, mix = 1 },
  },
}

local floor, max = math.floor, math.max
local divisions = { 1, 0.5, 0.25, 0.125 }
local size, write = 1, 0
local left, right = {}, {}
local position, step = 0, 0
local active, sliceStart, sliceLength, repeatPos = false, 0, 1, 0

function prepare(rate)
  size = floor(rate * 2)
  for i = 0, size - 1 do left[i] = 0; right[i] = 0 end
  write = 0
end

function block(frames)
  position = beat
  step = tempo / 60 / sample_rate
end

function process(l, r)
  left[write] = l
  right[write] = r
  local mode = floor(params.when)
  local bar = position % 4
  local wanted = mode == 0 or (mode == 1 and bar >= 3) or (mode == 2 and floor(position / 4) % 2 == 1)
  if wanted and not active then
    active = true
    sliceStart = write
    sliceLength = max(64, floor(divisions[floor(params.division) + 1] * 60 / tempo * sample_rate))
    repeatPos = 0
  elseif not wanted then
    active = false
  end
  local outL, outR = l, r
  if active then
    local offset = repeatPos % sliceLength
    local index = (sliceStart + offset) % size
    local fadeLength = max(1, floor(sliceLength * params.fade * 0.5))
    local gain = 1
    if params.fade > 0 then
      if offset < fadeLength then gain = offset / fadeLength elseif sliceLength - offset < fadeLength then gain = (sliceLength - offset) / fadeLength end
    end
    local mix = params.mix
    outL = l * (1 - mix) + left[index] * gain * mix
    outR = r * (1 - mix) + right[index] * gain * mix
    repeatPos = repeatPos + 1
  end
  write = (write + 1) % size
  position = position + step
  return outL, outR
end
