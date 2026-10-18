# Keyboard shortcuts

TStudio uses FL Studio's default shortcuts. Window-specific keys apply while that window has focus. Click inside it first. Extensions can add their own shortcuts (`ts.shortcut`), and those take priority over the built-in ones.

## Global

| Keys | Action |
| --- | --- |
| Space | Play / stop (stop returns to the start) |
| Ctrl+Space | Play / pause (keeps the position) |
| R, Shift+R | Record |
| L | Pattern / song mode |
| Home | Go to start |
| Numpad * / Numpad / | Next / previous bar |
| Ctrl+H | Stop sound (panic) |
| Ctrl+M | Metronome |
| Ctrl+T | Typing keyboard to piano |
| Ctrl+Z, Ctrl+Alt+Z | Undo (step by step) |
| Ctrl+Y, Ctrl+Shift+Z | Redo |
| Ctrl+O, Ctrl+S, Ctrl+Shift+S | Open, save, save as |
| Ctrl+N | Save new version (`song_2.tsp`, `song_3.tsp`, ...) |
| Ctrl+R | Export audio (WAV/FLAC/OGG) |
| Ctrl+Shift+M | Export MIDI |
| Ctrl+Shift+R | Export MP3 |
| F4 / Ctrl+F4 | New pattern / next empty pattern |
| F2 | Rename pattern (rename the mixer track when the mixer has focus) |
| Numpad + / Numpad - | Next / previous pattern |
| Numpad 1-9 | Select pattern 1-9 (created if missing) |
| F5 / F6 / F7 / F9 | Playlist / channel rack / piano roll / mixer |
| Alt+F8 | Browser |
| F10 | Audio settings |
| F12 | Close all windows |
| Esc | Close the front window |
| Enter | Maximise / restore the playlist |
| Ctrl+Shift+H | Arrange windows |
| Ctrl+Shift+L | Script console |

## Channel rack

| Keys | Action |
| --- | --- |
| Up / Down | Select channel above / below |
| Alt+Up / Down, Shift+Up / Down | Move the selected channel |
| 1-9, 0 | Mute / unmute channels 1-10 (when the typing keyboard is off) |
| Ctrl+1-9, Ctrl+0 | Solo / unsolo channels 1-10 |
| Alt+C | Clone channel |
| Alt+Del | Delete channel |

## Piano roll

| Keys | Action |
| --- | --- |
| P / B / D / E | Draw / paint / delete / select tool |
| T / C | Mute / slice tool |
| S / O | Slide / portamento for new notes (and toggles them on selected notes) |
| Ctrl+A, Ctrl+D | Select all, deselect |
| Shift+I | Invert selection |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste |
| Ctrl+B | Duplicate selection to the right |
| Del, Backspace | Delete selection |
| Up / Down | Transpose a semitone |
| Ctrl+Up / Down | Transpose an octave |
| Left / Right | Move by the snap size |
| Alt+Left / Right | Nudge (a quarter of the snap size) |
| Ctrl+Q, Alt+Q | Quantize |
| Ctrl+L | Quick legato |
| Ctrl+U | Quick chop (split into snap-size notes) |
| Ctrl+G | Glue touching notes |
| Alt+R / Alt+S / Alt+A | Humanize / strum / arpeggiate (runs the built-in script) |
| G / K | Previous / next channel |
| H / J | Previous / next channel that has notes |
| PgUp / PgDn | Zoom in / out |
| Ctrl+M | Import MIDI file |

When the typing keyboard is on, letter keys play notes instead of switching tools.

## Playlist

| Keys | Action |
| --- | --- |
| P, B / D / E | Draw / delete / select tool |
| Ctrl+A, Ctrl+D | Select all, deselect |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste at the playhead |
| Ctrl+B | Duplicate selection to the right |
| Shift+Left / Right | Move selection by the snap size |
| Del | Delete selection |
| Ctrl+drag / right-drag on the timeline | Set the song loop region (double-click the timeline to clear it) |
| Click / drag inside an automation clip | Add or move points (right-click deletes a point, Alt+drag bends the curve) |

## Mixer

| Keys | Action |
| --- | --- |
| S | Solo / unsolo the selected track |
| F2 | Rename the selected track |
| Ctrl+L | Route the selected channel to the selected track |
| Alt+Left / Right | Select the previous / next track |

## Mouse

| Action | Result |
| --- | --- |
| Right-click a knob, fader or pan | Create automation clip (also Tools > Last tweaked for any plugin knob) |
| Right-click the tempo | Type a tempo, or automate it |
| Click the arrow under a mixer track | Route the selected track there (drag for send level, right-click: route only / sidechain / remove) |
| Right-click a channel | Channel settings (cut groups, time shift, mono/porta, arpeggiator) |
