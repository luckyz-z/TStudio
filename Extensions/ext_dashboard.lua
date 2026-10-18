local view = { project = nil, age = 99 }

local function refresh()
  view.project = ts.project()
  view.counts = {}
  for _, channel in ipairs(view.project.channels) do
    view.counts[channel.id] = #ts.notes(channel.id)
  end
end

local dashboard = ts.panel({
  title = "Session dashboard",
  width = 460,
  height = 300,
  fps = 4,
  draw = function(w, h)
    view.age = view.age + 1
    if view.project == nil or view.age > 2 then
      refresh()
      view.age = 0
    end
    local p = view.project
    gfx.color("#121618")
    gfx.fill_rect(10, 10, w - 20, 64, 6)
    local tiles = {
      { "TEMPO", string.format("%.1f", p.tempo) },
      { "SWING", string.format("%d%%", math.floor(p.swing * 100 + 0.5)) },
      { "PATTERN", p.patterns[p.current_pattern + 1] and p.patterns[p.current_pattern + 1].name or "-" },
      { "MODE", p.song_mode and "SONG" or "PAT" },
    }
    local tile = (w - 20) / #tiles
    for i, t in ipairs(tiles) do
      local x = 10 + (i - 1) * tile
      gfx.color("#7f8b90")
      gfx.text(t[1], x, 16, tile, 14, 10, "center", true)
      gfx.color(i == 4 and p.playing and "#7be0a0" or "#e8483b")
      gfx.text(t[2], x, 32, tile, 34, 20, "center", true)
    end
    local most = 1
    for _, c in pairs(view.counts) do most = math.max(most, c) end
    local rows = math.min(#p.channels, 10)
    local rowHeight = (h - 100) / math.max(rows, 1)
    view.rowHeight = rowHeight
    for i = 1, rows do
      local channel = p.channels[i]
      local y = 86 + (i - 1) * rowHeight
      local count = view.counts[channel.id] or 0
      local selected = channel.id == p.selected_channel
      gfx.color(selected and "#d6dde0" or "#8f9a9f")
      gfx.text(channel.name, 14, y, 130, rowHeight - 2, 12, "left", selected)
      gfx.color("#121618")
      gfx.fill_rect(150, y + 3, w - 210, rowHeight - 8, 3)
      gfx.color(channel.muted and "#545d62" or "#e8483b")
      gfx.fill_rect(150, y + 3, (w - 210) * count / most, rowHeight - 8, 3)
      gfx.color("#8f9a9f")
      gfx.text(tostring(count), w - 54, y, 44, rowHeight - 2, 11, "right")
    end
  end,
  mouse_down = function(x, y)
    if y > 86 and view.project and view.rowHeight then
      local index = math.floor((y - 86) / view.rowHeight) + 1
      local channel = view.project.channels[index]
      if channel then
        ts.call("select_channel", { id = channel.id })
        view.age = 99
      end
    end
  end,
})

ts.menu("Session dashboard", function() ts.show_panel(dashboard) end)
