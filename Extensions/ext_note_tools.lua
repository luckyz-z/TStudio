local function edit(name, transform)
  ts.menu(name, function()
    local channel = ts.selected_channel()
    local notes = ts.notes(channel)
    if #notes == 0 then
      print("The selected channel has no notes in this pattern.")
      return
    end
    table.sort(notes, function(a, b) return a.start < b.start end)
    ts.set_notes(channel, transform(notes))
  end)
end

edit("Notes: randomize velocity", function(notes)
  for _, n in ipairs(notes) do n.velocity = math.min(1, math.max(0.1, n.velocity + (math.random() - 0.5) * 0.3)) end
  return notes
end)

edit("Notes: reverse", function(notes)
  local last = 0
  for _, n in ipairs(notes) do last = math.max(last, n.start + n.length) end
  for _, n in ipairs(notes) do n.start = last - n.start - n.length end
  return notes
end)

edit("Notes: legato", function(notes)
  for i, n in ipairs(notes) do
    for j = i + 1, #notes do
      if notes[j].start > n.start then
        n.length = notes[j].start - n.start
        break
      end
    end
  end
  return notes
end)

edit("Notes: double speed", function(notes)
  local count = #notes
  for i = 1, count do
    local n = notes[i]
    n.start, n.length = n.start // 2, math.max(6, n.length // 2)
  end
  local span = 0
  for _, n in ipairs(notes) do span = math.max(span, n.start + n.length) end
  span = math.ceil(span / ts.BAR) * ts.BAR
  for i = 1, count do
    local n = notes[i]
    notes[#notes + 1] = { start = n.start + span, length = n.length, key = n.key, velocity = n.velocity }
  end
  return notes
end)
