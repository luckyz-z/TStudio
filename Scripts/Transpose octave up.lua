local channel = ts.selected_channel()
local notes = ts.notes(channel)
for _, note in ipairs(notes) do note.key = math.min(127, note.key + 12) end
ts.set_notes(channel, notes)
