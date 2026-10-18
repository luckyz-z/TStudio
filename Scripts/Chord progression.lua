local channel = ts.selected_channel()
local root = "A4"
local progression = { { 1, "minor" }, { 6, "major" }, { 3, "major" }, { 7, "major" } }
local notes = {}
for bar, chord in ipairs(progression) do
  local base = ts.scale_degree(root, "minor", chord[1])
  for _, interval in ipairs(ts.chords[chord[2]]) do
    notes[#notes + 1] = { start = (bar - 1) * ts.BAR, length = ts.BAR, key = base + interval, velocity = 0.7 }
  end
end
ts.set_notes(channel, notes)
print("Chords written: Am - F - C - G")
