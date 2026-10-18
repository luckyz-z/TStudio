local channel = ts.selected_channel()
local notes = ts.notes(channel)
for _, note in ipairs(notes) do
  note.start = math.floor(note.start / ts.STEP + 0.5) * ts.STEP
end
ts.set_notes(channel, notes)
print("Quantized " .. #notes .. " notes")
