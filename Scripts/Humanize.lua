local channel = ts.selected_channel()
local notes = ts.notes(channel)
math.randomseed(os and os.time and os.time() or 1)
for _, note in ipairs(notes) do
  note.start = math.max(0, note.start + math.random(-3, 3))
  note.velocity = math.min(1, math.max(0.1, note.velocity + (math.random() - 0.5) * 0.18))
end
ts.set_notes(channel, notes)
print("Humanized " .. #notes .. " notes")
