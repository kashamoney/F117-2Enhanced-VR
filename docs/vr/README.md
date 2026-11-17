# VR player guide

Start with the [fork README](../../README.md) for requirements and the download.
Commands for source-checkout helpers below run from the repository root.

## Downloaded Windows release

Extract the entire release ZIP, start SteamVR, then open `f117-enhanced-vr.exe`.
The EXE starts in VR without arguments and opens a native folder picker on first
use. It remembers your original game folder in
`%APPDATA%\F117-Enhanced-VR\settings.ini`; saves remain in that game folder.
On upgrade from preview 1, settings are copied from the former publisher-named
folder if the new file does not already exist. The old file is retained and an
existing new settings file is never replaced. Explicit settings overrides are
independent profiles and do not import the old default.
Keep all packaged DLLs beside the EXE. No runtime scripts or build tools are needed.

Hold Shift while opening the EXE to choose a different folder. From a terminal,
`f117-enhanced-vr.exe --vr-setup` does the same. Optional terminal commands:

```text
f117-enhanced-vr.exe --vr-preview
f117-enhanced-vr.exe --vr-diagnostic
f117-enhanced-vr.exe --check --game-dir "D:/Games/F117"
```

The native setup checks the supported original DOS builds before VR starts;
the GOG 473.04 VGAME build is unsupported. Logs are beside the EXE for native setup
launches. `--vr-config FILE` or `F117_VR_CONFIG` overrides the native settings
location for portable profiles or testing; these are optional developer controls.
Cancelling the first-use folder picker exits without starting VR.

## Source checkout: game folder and saves

`Play-VR.cmd` and `Preview-VR.cmd` use `tools/vr/Start-VR.ps1`. The launcher checks
`-GameDirectory`, then `F117_GAME_DIR`, `.vr-local.json`, and the repository's
`Game` folder. If none is configured, it opens a Windows folder picker. A chosen
folder must contain the original game files and be writable for saves.

The remembered path is local to your checkout and ignored by Git. To replace a
stale choice, pass a valid path explicitly:

```powershell
.\Play-VR.cmd -GameDirectory "D:\Games\F117"
```

For unattended checks, `-NoPrompt` reports a missing choice without opening a
dialog. `-Check` checks game files without opening a headset session. To inspect
the headset/runtime instead, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\vr\Start-VR.ps1 -Diagnostic
```

## Practice and recording

These optional shortcuts require Python 3 on PATH. They copy game data into a
new `work/vr-practice/` directory, use a test pilot, and leave your normal roster
untouched:

```powershell
.\tools\vr\Carrier-VR.cmd
.\tools\vr\Carrier-VR.cmd -Preview
.\tools\vr\Record-VR.cmd
```

Allow the scripted setup and briefing to finish. The practice flight starts on a
Libya carrier deck. In a recording session, **F9** captures a short stereo burst;
close the game to save the images and timing CSV in `work/vr-session/`.
`tools/vr/encode_vr_live.py` converts captures to video with FFmpeg.

## View and rendering

- **F12:** recenter while seated and looking straight ahead.
- **F11:** show/hide the cockpit.
- **Ctrl + arrows:** look around in desktop preview; release Ctrl to fly with arrows.
- **Alt + P / Enter:** pause/resume while head tracking stays active.
- Focus the mirror window for keyboard input. Head tracking owns the flight
  viewpoint; normal external/cockpit-view shortcuts and `C` view switching are
  suppressed in VR. Alt+Enter fullscreen is also suppressed in the mirror.

Useful examples:

```powershell
.\Play-VR.cmd -Resolution 2160 -WorldScale 1.0
.\Play-VR.cmd -DrawDistance 4
.\Preview-VR.cmd -OriginalPresentation
.\Play-VR.cmd -NoWaterDetail -NoWakes
```

`-Resolution 0` uses the runtime's recommended eye resolution; explicit values
640..4096 set eye height. GPU preview defaults to 1920x1920 per eye. The mirror
is 1024x1024. MSAA uses up to 8x where supported.

GPU VR defaults to 8x render distance. Choose `-DrawDistance 1`, `2`, `4` or `8`;
these change presentation, not detection, AI or weapon ranges. Available comparison
switches include `-OriginalPresentation`, `-OriginalDrawDistance`,
`-OriginalAircraftSize`, `-LegacyWaterPoints`, `-NoWaterDetail`, `-NoShorelines`,
`-NoWakes`, `-NoAtmosphere`, and `-NoLandingDetail`.

`-Software` retains the earlier software eye renderer for comparison. The raw
executable's `--true-night` option also selects that fallback; ordinary game night
palettes work on the GPU. `--classic` disables VR.

## Troubleshooting and current limits

- Missing game folder: supply `-GameDirectory` as shown above; the choice is saved.
- Missing executable: the launcher runs the build helper. Install the MSYS2
  prerequisites from the root README first.
- OpenXR startup failure: check that the intended runtime is active and the
  headset is tracking; use `-Diagnostic` and inspect `work/vr-session/f117-vr.log`.
- Keyboard input missing: focus the mirror window.
- Unexpected seat position: sit naturally, look forward, and press F12.

This is an experimental seated VR adaptation. Scale, comfort and sustained pacing
need review on each headset. Automated desktop checks do not measure headset
optics or runtime reprojection. The cockpit is adapted for usable visibility and
instruments; it is not a measured reconstruction.

[Developer options, validation commands and rendering details](DEVELOPMENT.md)
are kept separate from player setup. [Review reports](reviews/) preserve historical
implementation evidence; their local images/logs are not bundled repository assets.
