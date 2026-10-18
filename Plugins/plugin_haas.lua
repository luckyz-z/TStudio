plugin = {
  name = "Lua Haas Widener",
  type = "effect",
  author = "TStudio",
  description = "Delays one side by a few milliseconds for instant width",
  params = {
    { id = "delay", name = "Delay", min = 0, max = 35, default = 12, unit = "ms" },
    { id = "side", name = "Delayed side", choices = { "Left", "Right" }, default = 1 },
    { id = "mix", name = "Mix", min = 0, max = 1, default = 1 },
  },
  presets = { { name = "Subtle", delay = 6, mix = 0.7 }, { name = "Wide", delay = 18, mix = 1 } },
}

local floor = math.floor
local size, write, buffer = 1, 0, {}

function prepare(rate)
  size = floor(rate * 0.05)
  for i = 0, size - 1 do buffer[i] = 0 end
  write = 0
end

function process(l, r)
  local leftSide = floor(params.side) == 0
  buffer[write] = leftSide and l or r
  local samples = floor(params.delay * 0.001 * sample_rate)
  local delayed = buffer[(write - samples) % size]
  write = (write + 1) % size
  local mix = params.mix
  if leftSide then return l * (1 - mix) + delayed * mix, r end
  return l, r * (1 - mix) + delayed * mix
end
