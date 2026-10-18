local channel = ts.selected_channel()
local chords = {}
for _, note in ipairs(ts.notes(channel)) do
  local group = chords[note.start] or { length = note.length, keys = {} }
  group.keys[#group.keys + 1] = note.key
  group.length = math.max(group.length, note.length)
  chords[note.start] = group
end
local notes = {}
for start, group in pairs(chords) do
  table.sort(group.keys)
  local order = {}
  for i = 1, #group.keys do order[#order + 1] = group.keys[i] end
  for i = #group.keys - 1, 2, -1 do order[#order + 1] = group.keys[i] end
  local index = 1
  for t = start, start + group.length - 1, ts.STEP do
    notes[#notes + 1] = { start = t, length = ts.STEP, key = order[index] + 12, velocity = 0.7 }
    index = index % #order + 1
  end
end
ts.set_notes(channel, notes)
print("Arpeggiated into " .. #notes .. " notes")
