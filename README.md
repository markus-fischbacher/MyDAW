# Arranger

Native arrangement-view DAW prototype — C++20, JUCE 9 + tracktion_engine 3.5 (vendored in `libs/`).

## Build & run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
open build/Arranger_artefacts/Debug/Arranger.app
```

Optional args: a `.tracktionedit` project to open, audio files (placed on consecutive tracks at bar 1), `--demo-midi` (adds an example MIDI clip), `--snapshot out.png` (renders the window to PNG and quits) and `--save-and-quit out.tracktionedit` (saves the project and quits, for scripted checks).

## Controls

| Action | How |
|---|---|
| Play / pause | Space |
| Return to start | Return / Home, or Stop twice |
| New / Open / Save / Save As | Cmd+N / Cmd+O / Cmd+S / Shift+Cmd+S (File menu) |
| Import audio | Drag files onto a lane, or Cmd+I |
| Move clip | Drag body (across tracks too), snaps to grid; hold Alt to bypass snap |
| Create MIDI clip | Double-click an empty lane (adds a synth to the track if it has none) |
| Edit MIDI clip | Double-click it; piano roll opens below (click: add note, drag: move/resize, right-click or Delete: remove, Esc: close) |
| Trim clip | Drag left/right edge |
| Split at playhead | Cmd+E |
| Delete clip | Backspace / Delete |
| Undo / redo | Cmd+Z / Shift+Cmd+Z |
| Zoom | Pinch, Cmd+scroll, Cmd+= / Cmd+- |
| Clip menu (rename, colour, gain, fades, reverse, transpose, warp, takes) | Right-click a clip |
| Fade in / out | Drag the white handles at the clip's top corners |
| Duplicate clip | Cmd+D |
| Return track / sends | Track menu > Add Return Track; green send slider and output button per mixer strip (route to a group track) |
| Piano roll velocity / quantise / transpose | Drag in the velocity lane; Q quantises to grid; Up/Down arrows transpose selected note |
| Browser (preview, insert, drag onto a lane) | Cmd+B; double-click inserts at the playhead |
| Export (WAV/AIFF/FLAC/OGG, normalise, stems) | Shift+Cmd+E |
| Zoom to fit | Shift+Cmd+F |
| Recent projects / autosave recovery | File > Open Recent; autosaves every minute, File > Recover Autosave |
| Sidechain | Track plug-ins menu > compressor > Sidechain Source |
| MIDI CC / pitch-bend lanes | In the piano roll press C to cycle the lane (velocity, mod, volume, pan, expression, sustain, pitch bend); click adds/edits, Alt- or right-click deletes |
| Import / export MIDI | File > Import MIDI File (or drop .mid on a lane); right-click a MIDI clip > Export MIDI File |
| Crossfade | Right-click an audio clip > Crossfade into Next Clip |
| Bounce / freeze | Right-click clip > Bounce to New Track; right-click track header > Freeze Track |
| Automate any plug-in parameter | Show automation (A), then click the Vol/Pan/FX button to pick a parameter |
| MP3 export | Shown in the export dialog when the `lame` encoder is installed (e.g. `brew install lame`) |
| Expand all automation lanes | Shift+Cmd+A (View menu), or per track via the header right-click menu; shows volume, pan and every plug-in parameter that has automation |
| Expand / collapse one track | Small arrow button at the top right of the track header; the arrow on a lane header collapses it to a thin strip and expands it again |
| Resize / hide automation lanes | Drag the bottom edge of an expanded lane; right-click a lane for Hide / Show hidden lanes |
| Automation curve shapes | Right-click a point: Linear, Ease in, Ease out, Hold, Delete |
| Automation range copy/paste | Shift+drag in a lane to select, Cmd+C / Cmd+X / Delete, Cmd+V pastes at the playhead |
| Swipe comping | Track header menu: Show / Hide Take Lanes, select a clip with takes, drag across a take lane to use it for that range |
| Edit tempo / time-signature markers | Double-click a marker in the ruler, or right-click the ruler (edit, move to playhead, delete, add) |
| Rebind shortcuts | View menu > Keyboard Shortcuts... (conflicts are rejected) |
| Consolidate clip | Clip right-click menu: Consolidate in Place renders the clip with its effects (turn Warp off first) |
| Move tempo / time-sig markers | Drag a marker in the ruler (time signatures snap to bars) |
| Loop recording | Enable Loop (loop longer than 2 s), arm a track and record: each pass becomes a take; use the take lanes to comp |
| Scroll | Trackpad / wheel (Shift = horizontal) |
| Locate | Click/drag in ruler |
| Set loop range | Drag in the lower half of the ruler ("drag here to set loop"); drag its edges to resize, its body to move, click it to toggle looping; Cmd+L also toggles |
| Metronome | Click button / Cmd+K |
| Tempo | Double-click the BPM readout and type |
| Mixer (volume, pan, M/S, meters, master) | Mixer button / Cmd+M |
| Add audio / instrument track | Cmd+T / Shift+Cmd+T, or right-click a track header |
| Rename / delete track | Double-click name / right-click header (or Track menu) |
| Mute / solo | M / S buttons in header or mixer |
| Automation (volume / pan) | A button in header (Cmd+A), Vol/Pan toggles parameter; click adds a point, drag moves, Alt-click or right-click deletes |
| Record | R button arms a track (audio input, or MIDI inputs on instrument tracks); Rec button / Cmd+R records armed tracks; stop to finish |
| Plug-ins (built-in FX, AU/VST3, instruments) | FX button in a mixer strip (master strip too, or Project > Master Plug-ins), or Cmd+P for the selected track; the menu also scans for plug-ins and opens editors |
| Tempo change / remove | Project menu: Insert Tempo Change at Playhead, Remove Tempo Changes (markers show in the ruler) |
| Time signature | Project menu: Set Time Signature (start of project) or Insert Time Signature Change at Playhead (from that bar on) |
| Delete tempo / time-signature change | Right-click its marker in the ruler |
| Punch in/out | Punch button or Shift+Cmd+P; records only inside the loop range (Shift+drag in ruler) |
| Export WAV | Shift+Cmd+E |

## Layout

- `Source/MainComponent` — owns `te::Engine` + `te::Edit`, global key commands
- `Source/TransportBar` — transport buttons, bars.beats / time / tempo / CPU readouts
- `Source/MixerView` — channel strips, master, level meters
- `Source/ArrangementView` — the timeline; paints everything in one component (no per-clip components) in musical time via the tempo sequence

## Licensing

Arranger is licensed under the GNU Affero General Public License v3.0, see [LICENSE](LICENSE). This follows from the libraries it builds on (JUCE: AGPL-3.0, tracktion_engine: GPL-3.0-or-later); see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). A closed-source release would need commercial licences for both.

## Projects

Projects are saved as `.tracktionedit` files (XML). Audio is referenced by absolute path, not copied into the project, so moving or deleting a source file breaks the clip. The window title shows a bullet when there are unsaved changes, and New / Open / Quit ask before discarding them.
