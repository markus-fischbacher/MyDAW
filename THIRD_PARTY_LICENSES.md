# Third-party licences

Arranger links against the libraries below. They are vendored as git submodules under `libs/`; their own licence files remain in their directories.

| Component | Location | Licence | Text |
|---|---|---|---|
| JUCE | `libs/tracktion_engine/modules/juce` | AGPL-3.0 (or commercial) | [LICENSE](LICENSE), plus `libs/tracktion_engine/modules/juce/LICENSE.md` |
| tracktion_engine | `libs/tracktion_engine` | GPL-3.0-or-later (or commercial) | [licenses/GPL-3.0.txt](licenses/GPL-3.0.txt), plus `libs/tracktion_engine/LICENSE.md` |
| SoundTouch (time-stretching, enabled in the build) | `libs/tracktion_engine/modules/tracktion_engine/3rd_party/soundtouch` | LGPL-2.1 | [licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt) |

tracktion_engine also bundles further third-party code (for example Signalsmith Stretch and Airwindows, both MIT-licensed); see the files under `libs/tracktion_engine/modules/tracktion_engine/3rd_party` for their notices.

Because the application combines AGPL-3.0 (JUCE) and GPL-3.0-or-later (tracktion_engine) code, the combined work is distributed under the **GNU Affero General Public License v3.0** ([LICENSE](LICENSE)). Anyone who wants to ship a closed-source version needs commercial licences from both JUCE and Tracktion.
