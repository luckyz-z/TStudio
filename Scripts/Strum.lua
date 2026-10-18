local channel = ts.selected_channel()
local notes = ts.notes(channel)
table.sort(notes, function(a, b) return a.start == b.start and a.key < b.key or a.start < b.start end)
local last, offset = -1, 0
for _, note in ipairs(notes) do
  if note.start == last then offset = offset + 4 else offset = 0 end
  last = note.start
  note.start = note.start + offset
end
ts.set_notes(channel, notes)
print("Strummed " .. #notes .. " notes")
