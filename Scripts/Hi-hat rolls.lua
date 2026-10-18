local channel = ts.selected_channel()
local notes = {}
for bar = 0, 3 do
  for step = 0, 15, 2 do
    local start = bar * ts.BAR + step * ts.STEP
    local roll = (bar % 2 == 1) and step >= 12
    if roll then
      local division = (bar == 3) and 16 or 12
      for i = 0, (ts.STEP * 2) // division - 1 do
        notes[#notes + 1] = { start = start + i * division, length = division, key = 60 + (bar == 3 and i or 0), velocity = 0.45 + i * 0.05 }
      end
    else
      notes[#notes + 1] = { start = start, length = ts.STEP, key = 60, velocity = (step % 4 == 0) and 0.85 or 0.55 }
    end
  end
end
ts.set_notes(channel, notes)
print("Wrote " .. #notes .. " hats into channel " .. channel)
