# F-117A Enhanced VR

Experimental seated **OpenXR VR support for Windows**, built on
[kylofon/F117-2Enhanced](https://github.com/kylofon/F117-2Enhanced), the native SDL3
port of MicroProse's *F-117A Nighthawk Stealth Fighter 2.0*.

This fork adds stereoscopic flight, head tracking and recentering, a faceted 3D
cockpit with the original live instruments and HUD, and GPU-rendered scenery.
It retains the original simulation and keyboard/gamepad flight controls.

## Requirements

- Windows 64-bit and a Direct3D 11-capable GPU.
- An OpenXR headset/runtime for headset play. The tested setup is SteamVR with a
  Samsung Odyssey+ through Oasis; other headsets and runtimes are unverified.
- Your own complete, writable folder of the **original DOS game files**, including
  `F117.COM`, `VGAME.EXE`, `SETUP.EXE`, `START.EXE` and the other original files.
  Game files are not included. The GOG `VGAME.EXE` 473.04 build is not supported.
- MSYS2's mingw64 build tools and libraries, listed below.

The fork currently provides source code; no VR binary release is published.
Upstream desktop release packages do not contain this fork's VR implementation.
A headset is optional for the desktop stereo preview.

## Build

Clone this fork and open a terminal in the repository folder:

```sh
git clone --branch feature/openxr-vr https://github.com/kashamoney/F117-2Enhanced.git
cd F117-2Enhanced
```

Install these packages in the **MSYS2 MINGW64** shell:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-sdl3 mingw-w64-x86_64-openxr-sdk
```

Then build from PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\vr\Build-VR.ps1
```

The script builds `f117port/build-vr/f117-enhanced-vr.exe`, copies its runtime DLLs,
and runs ten focused checks. MSYS2 defaults to `C:\msys64`; pass `-MsysRoot` if you
installed it elsewhere. Python 3 is needed for practice missions and development
checks, but not ordinary headset play. More build/test details are in the
[developer guide](docs/vr/DEVELOPMENT.md).

## Play

1. Start SteamVR and make sure your headset is tracking. For the tested setup,
   SteamVR must be the active OpenXR runtime.
2. Double-click **`Play-VR.cmd`**.
3. On first launch, choose the folder containing your original game files.
   Your choice is remembered in `.vr-local.json`, which is ignored by Git.
4. Keep the game's mirror window focused for keyboard input. Menus appear on a
   virtual screen; flight uses the tracked cockpit view.

You can choose or change the game folder explicitly:

```powershell
.\Play-VR.cmd -GameDirectory "D:\Games\F117"
```

You may also set `F117_GAME_DIR`, or put your own game files in a `Game` folder at
this repository's root. Selection order is: explicit argument, environment
variable, remembered choice, repository `Game` folder, then the folder picker.
There is no dependency on a particular parent-folder layout. Saves go to the
selected game folder; preserve your existing `ROSTER.FIL` when moving game data.

**`Preview-VR.cmd`** runs the desktop stereo preview without a headset. It uses
the same selected game folder and saves. For an automated game-file check without
opening VR, run `Play-VR.cmd -Check -NoPrompt` after choosing a game folder.

| Control | Action |
|---|---|
| F12 | Recenter while looking straight ahead |
| F11 | Show/hide the cockpit |
| Ctrl + arrow keys | Look around in the desktop preview |
| Alt + P / Enter | Pause / resume; head tracking continues while paused |
| Original game keys | Flight, targeting, weapons, gear, flaps and throttle |

For practice missions, recording, rendering options and troubleshooting, see the
[VR player guide](docs/vr/README.md). Logs are written under `work/vr-session/`.

## Project layout and maintenance

- `f117port/`: engine, VR modules and CMake build configuration.
- `tools/vr/`: VR build/launch helpers, benchmarks and development utilities.
- `docs/vr/`: player/developer guides and historical review reports.
- `port/`, `third_party/`, `licenses/`: upstream verification material and dependencies.
- [PLAN.md](PLAN.md), [DESIGN.md](DESIGN.md), [ENGINE.md](ENGINE.md): development records and engine rules.

`feature/openxr-vr` is the published VR branch. `main` mirrors upstream; temporary
`integration/upstream-*` branches combine upstream updates with VR for testing.
Keep upstream updates on an integration branch until automated checks and a
headset flight have been reviewed. The [developer guide](docs/vr/DEVELOPMENT.md)
contains the update workflow and advanced rendering notes.

Upstream's [README](https://github.com/kylofon/F117-2Enhanced#readme) describes its
desktop features and releases; those may be newer than the published VR branch.
The VR build helper omits the desktop GUI launcher. `-DesktopOnly` builds the
non-VR executable, and `--classic` disables enhancements including VR.

## Credits and license

Based on kylofon's Enhanced port and the
[faithful F117-2 port](https://github.com/kylofon/F117-2). Source is MIT licensed;
see [LICENSE](LICENSE) and [licenses/](licenses/). The original game is copyright
MicroProse. Its files and optional Mac artwork are not distributed by this fork.
