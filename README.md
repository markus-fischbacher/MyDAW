# Arranger

Native arrangement-view DAW prototype — C++20, JUCE 9 + tracktion_engine 3.5 (vendored in `libs/`).

## Build & run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
open build/Arranger_artefacts/Debug/Arranger.app
```

Optional args: a `.tracktionedit` project to open, audio files (placed on consecutive tracks at bar 1), `--snapshot out.png` (renders the window to PNG and quits) and `--save-and-quit out.tracktionedit` (saves the project and quits, for scripted checks).

## Controls

| Action | How |
|---|---|
| Play / pause | Space |
| Return to start | Return / Home, or Stop twice |
| New / Open / Save / Save As | Cmd+N / Cmd+O / Cmd+S / Shift+Cmd+S (File menu) |
| Import audio | Drag files onto a lane, or Cmd+I |
| Move clip | Drag body (across tracks too), snaps to grid; hold Alt to bypass snap |
| Trim clip | Drag left/right edge |
| Split at playhead | Cmd+E |
| Delete clip | Backspace / Delete |
| Undo / redo | Cmd+Z / Shift+Cmd+Z |
| Zoom | Pinch, Cmd+scroll, Cmd+= / Cmd+- |
| Scroll | Trackpad / wheel (Shift = horizontal) |
| Locate | Click/drag in ruler |

## Layout

- `Source/MainComponent` — owns `te::Engine` + `te::Edit`, global key commands
- `Source/TransportBar` — transport buttons, bars.beats / time / tempo / CPU readouts
- `Source/ArrangementView` — the timeline; paints everything in one component (no per-clip components) in musical time via the tempo sequence

## Licensing

Arranger is licensed under the GNU Affero General Public License v3.0, see [LICENSE](LICENSE). This follows from the libraries it builds on (JUCE: AGPL-3.0, tracktion_engine: GPL-3.0-or-later); see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). A closed-source release would need commercial licences for both.

## Projects

Projects are saved as `.tracktionedit` files (XML). Audio is referenced by absolute path, not copied into the project, so moving or deleting a source file breaks the clip. The window title shows a bullet when there are unsaved changes, and New / Open / Quit ask before discarding them.
