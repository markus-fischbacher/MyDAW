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
| Scroll | Trackpad / wheel (Shift = horizontal) |
| Locate | Click/drag in ruler |
| Set loop range | Shift+drag in ruler; toggle with Loop button / Cmd+L |
| Metronome | Click button / Cmd+K |
| Tempo | Double-click the BPM readout and type |
| Mixer (volume, pan, M/S, meters, master) | Mixer button / Cmd+M |
| Add audio / instrument track | Cmd+T / Shift+Cmd+T, or right-click a track header |
| Rename / delete track | Double-click name / right-click header (or Track menu) |
| Mute / solo | M / S buttons in header or mixer |
| Automation (volume / pan) | A button in header (Cmd+A), Vol/Pan toggles parameter; click adds a point, drag moves, Alt-click or right-click deletes |
| Record | R button arms a track (audio input, or MIDI inputs on instrument tracks); Rec button / Cmd+R records armed tracks; stop to finish |
| Plug-ins (built-in FX, AU/VST3, instruments) | FX button in the mixer strip, or Cmd+P for the selected track; the menu also scans for plug-ins and opens editors |
| Tempo change / remove | Project menu: Insert Tempo Change at Playhead, Remove Tempo Changes (markers show in the ruler) |
| Time signature | Project menu: Set Time Signature (applies to the whole project) |
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
