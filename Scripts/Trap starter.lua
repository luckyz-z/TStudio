ts.tempo(145)
local pattern = ts.new_pattern("Trap starter")
local kick = ts.add_channel("pack:Kick Hard", "Kick")
local clap = ts.add_channel("pack:Clap Big", "Clap")
local hat = ts.add_channel("pack:Hat Tight", "Hat")
local bass = ts.add_channel("Tomato 808", "808")
local function hits(channel, steps, velocity)
  local notes = {}
  for _, s in ipairs(steps) do notes[#notes + 1] = { start = s * ts.STEP, length = ts.STEP, key = 60, velocity = velocity or 0.8 } end
  ts.set_notes(channel, notes, pattern)
end
hits(kick, { 0, 10, 16, 22, 27 })
hits(clap, { 8, 24 })
local hats = {}
for s = 0, 31, 2 do hats[#hats + 1] = s end
hits(hat, hats, 0.55)
ts.set_notes(bass, {
  { start = 0, length = 6 * ts.STEP, key = ts.note("C2"), velocity = 0.9 },
  { start = 10 * ts.STEP, length = 6 * ts.STEP, key = ts.note("C2"), velocity = 0.85 },
  { start = 16 * ts.STEP, length = 7 * ts.STEP, key = ts.note("G#1"), velocity = 0.9 },
  { start = 22 * ts.STEP, length = 6 * ts.STEP, key = ts.note("D#2"), velocity = 0.85 },
}, pattern)
print("Trap starter ready in pattern " .. pattern)
