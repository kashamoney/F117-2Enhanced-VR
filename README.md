# F-117A Enhanced — SDL3

An enhanced native port of MicroProse's *F-117A Nighthawk Stealth Fighter 2.0* (1991), built on the faithful
SDL3 port ([F117-2](https://github.com/kylofon/F117-2), v0.1.0). Not an emulator. The original game files are not included.
Goal: a sharper, smoother game (higher resolution, higher frame rate) with the original art unchanged.

Status and plan: [PLAN.md](PLAN.md) (as a page: `python tools/plan_site.py`, then open
`site/index.html`). Agent rules: [CLAUDE.md](CLAUDE.md). Engine rules: [ENGINE.md](ENGINE.md).

## Features

* **Higher resolution**: the 3D view, HUD, MFDs, TV window and map are drawn at N times the original 320x200
  (render scale: Auto, or 1 to 8). The original art is unchanged.
* **Wider view**: on a wide window the first cockpit view and the outside views show more of the world beside the
  4:3 picture; the side, rear and second cockpit views are fitted to the window height.
* **Smooth distance shading** and **True night**: options, off by default.
* **Smooth frame rate**: drawn at the monitor's refresh rate (`--fps vsync|60|30|off`); the camera, objects,
  HUD and cockpit MFDs move smoothly between the game's steps.
* **Launcher** with an Enhanced box, and `--classic`: the original picture and behaviour, as the faithful port.

See [CHANGELOG.md](CHANGELOG.md).

## How to play

1. **You need the original game**: your own copy of the *F-117A Nighthawk Stealth Fighter 2.0* files (`F117.COM`,
   `SETUP.EXE`, `START.EXE`, `VGAME.EXE` and the rest). The launcher tells you which file is missing.
2. **Unzip** `F117-Enhanced-v0.2.0-win64.zip` into a folder of your choice.
3. **Start `f117-enhanced-launcher.exe`** and point **Folder** at your game files (or copy them into the `Game`
   folder beside it). The folder must be writable: the game saves your pilot roster there.
4. Set the Enhanced options if you like and press **Play**. In SETUP answer **N** for the joystick and **2** for AdLib.

`roster/ROSTER.FIL` is the game's pilot roster as shipped by this package; to start with it, copy it into your game
folder (back up your own `ROSTER.FIL` first).

The errors the game reports go to `f117.log` beside the program.

## Command line

```text
f117-enhanced [--game-dir DIR] [--scale N] [--aspect 4:3|1:1] [--fullscreen] [--frame-ticks N]
              [--keys NAME=CODE,...] [--check] [--version]
              [--classic] [--render-scale N] [--fps vsync|60|30|off] [--wide|--no-wide]
              [--smooth-shading|--no-smooth-shading] [--true-night|--no-true-night]
```

* `--classic`: every enhancement off, whatever else is given. `--render-scale N`: 1 to 8 times 320x200 (default Auto).
* `--fps vsync|60|30|off`: pictures a second (default vsync: the monitor's refresh).
* `--wide` (default) / `--no-wide`; `--smooth-shading`, `--true-night`: off by default.
* The other options are the faithful port's; the remaining test options are listed at the top of `f117port/src/main.c`.

## Building

```bash
cmake -S f117port -B f117port/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build f117port/build
ctest --test-dir f117port/build
python tools/make_release.py      # clean Release build with the launcher, ctest, the zip in release/
```

Needs CMake 3.24, a C11 compiler and SDL 3 (MSYS2 `mingw64`: `mingw-w64-x86_64-sdl3`, and
`mingw-w64-x86_64-wxwidgets3.2-msw` for the launcher; `-DF117_LAUNCHER=OFF` skips it). The program is
`f117port/build/f117-enhanced.exe`. Tests that need the game files skip themselves when `Game/` is missing.

## Experimental seated VR (Windows / OpenXR)

This development branch adds stereoscopic flight with headset rotation and position,
seated recentering, a simple geometric cockpit, and the original live instrument panel.
Menus are displayed on a virtual screen. Keyboard and gamepad flight controls stay the same.
The backend uses OpenXR through SteamVR and Direct3D 11, including headsets connected
through the Oasis driver such as the Samsung Odyssey+. It is an experimental prototype;
headset comfort, scale, and sustained frame timing need testing on the actual headset.

Run **`Play-VR.cmd`** from this repository with SteamVR running and the headset tracking.
The default game folder is `../F117-Enhanced-play/Game` in this workspace. Original files
stay separate from the source and the game saves your pilot roster in that working copy.
Run **`Preview-VR.cmd`** for a desktop stereo preview without a headset.
The desktop mirror shows the two square eye images side by side.
The default world source is now **2048x2048 per eye**, up from 1024x1024 (four times
the source pixels). The live panel/HUD texture retains the enhanced 2x rendering at
640x400 instead of being reduced to 320x200. Original bitmap artwork keeps its original
detail, while vector instruments and the 3D world can render more sharply. The desktop
mirror window stays at 1024x1024 independently of the eye source resolution.

For a first headset test, **`Carrier-VR.cmd`** prepares a separate practice copy with a test
pilot and automatically navigates setup/briefing to a Libya carrier mission. Allow the intro
and scripted menus to finish; then the aircraft is stationary on deck and you have control.
This shortcut requires Python 3 and leaves your regular pilot roster untouched. Practice
copies are under `work/vr-practice/`. `tools/Start-VR.ps1 -Preview -CarrierPractice` runs the
same practice scenario on the desktop.

| Control | Action |
|---|---|
| F12 | Recenter the seated position and heading; look straight ahead when pressing it |
| F11 | Show/hide the prototype cockpit |
| Ctrl + arrow keys | Look around in desktop preview (release Ctrl to fly with the arrow keys) |
| Alt + P / Enter | Pause / resume the simulation; head tracking continues while paused |
| Original game keys | Targeting, weapons, flaps, gear, throttle, and flight |

The mirror window must have keyboard focus. Head tracking owns the flight viewpoint:
the exterior/cockpit-view shortcuts and `C` view toggle are suppressed in VR. The fixed-size
mirror also suppresses Alt+Enter fullscreen. The desktop preview uses a 64 mm
eye separation; headset mode uses the actual eye poses reported by OpenXR.

Build prerequisites: MSYS2 `mingw64`, then in its shell:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-sdl3 mingw-w64-x86_64-openxr-sdk
```

Build and check with PowerShell:

```powershell
.\tools\Build-VR.ps1
.\tools\Start-VR.ps1 -Diagnostic
.\tools\Start-VR.ps1
.\tools\Start-VR.ps1 -Preview
```

`Build-VR.ps1 -DesktopOnly` builds without the OpenXR dependency. The original launcher
is omitted from these development builds; the normal release and its launcher are unchanged.
The equivalent CMake switch for enabling the backend is `-DF117_OPENXR=ON` (default OFF).
The build copies SDL/OpenXR/compiler runtime DLLs beside the executable.

Advanced launch options:

```powershell
.\tools\Start-VR.ps1 -Resolution 768 -WorldScale 1.0 -DumpEyes
.\tools\Start-VR.ps1 -GameDirectory 'D:\Games\F117'
```

`--vr-resolution 640..2048` sets each software-rendered eye image (default 2048).
The headset submission targets use OpenXR's recommended sizes. Lower the software
resolution if frame times are too high; this is a CPU software renderer, and a fast GPU
does not guarantee native headset frame rate. `--vr-world-scale 0.1..10` sets physical
metres per game metre; default 1.0 uses the original 512 km theatre / 0x100000 world-unit
mapping (about 0.488 m per renderer unit). The game's exaggerated vertical scale and
high-altitude compression are preserved, so HUD altitude is not a literal VR ruler.
`--vr-no-cockpit` starts with the cockpit hidden. `--classic` disables VR.
`--vr-dump` writes the first pair of software world images to the working directory.
Session logs are in `work/vr-session/f117-vr.log`; original crash logs remain beside the exe.

Implementation and current limits:

* Each eye renders from the same pre-render snapshot and interpolation alpha. All engine
  memory and canvas state are restored afterward; eye rendering never advances gameplay.
* Sub-foot eye offsets are applied to 16.16 camera translations, with the terrain/model
  scale tracked through the sorted object queue. The eye baseline survives integer rounding.
* The original cockpit pictures are reused on an aircraft-fixed instrument panel. The
  canopy frame, consoles, floor and seat are simple new geometry, not a detailed F-117 interior.
* The prototype HUD extracts symbols using the compositor's vector drawing records onto a
  transparent, finite-distance surface. It is not a collimated optical HUD; target alignment
  needs review in the headset. An optical HUD with a defined viewing aperture is follow-up work.
* There are no motion-controller interactions or clickable cockpit switches. Use the existing
  keyboard or gamepad. The original gamepad handling and game rules are unchanged.
* Tracking loss submits an empty frame; a runtime exit closes the game. Errors are logged.
  If startup fails, run `-Diagnostic`. SteamVR must be the active OpenXR runtime. An
  `Oasis_UnlockRequired` result comes from the driver: use the official
  [Oasis unlock instructions](https://github.com/mbucchia/Oasis-Driver-for-Windows-Mixed-Reality/wiki/Procedure-to-unlock-headset-and-controllers-for-Oasis).

Offline integration check (no headset required):

```powershell
python tools/test_vr.py --exe f117port/build-vr/f117-enhanced.exe --game-dir ../F117-Enhanced-play/Game
```

This runs a deterministic real mission on scratch copies of the game, checks identical
eye poses, physical stereo disparity and head rotation, and compares engine hashes and
the final pilot roster against a run with VR rendering off. Images/logs are saved under
`work/vr-check/`. `--vr-selftest` checks pose math without game files or a headset.
Add `--preview-smoke` to exercise the native D3D11 window and save `vr-mirror.png` with
the GPU-rendered cockpit. In this workspace the 43 regression checks, stereo integration
and native preview have passed; a live Odyssey+/Oasis carrier scene also submits at about
90 frames/s at the initial 1024-per-eye setting. Visual comfort/readability and performance in flight
remain user checks.
To restore that lower-cost setting, use `tools/Start-VR.ps1 -Resolution 1024` (or add
`-CarrierPractice` for the separate training pilot). The 2048 setting renders four times
as many source pixels. The live carrier test at 2048 measured roughly 40-45 submitted
frames/s on this PC, compared with about 90 at 1024; choose 1024 if the sharper setting
does not feel smooth enough. Full-world eye passes reuse scratch layers instead of
copying large pixel planes that the next render immediately overwrites; this optimization
was checked to produce exactly the same world images.
The integration tool accepts `--resolution 1024` or `--resolution 2048` to check either setting.

The full upstream regression suite additionally uses Python `unicorn`, `numpy`, and
`Pillow`, the faithful companion repository's tools/specs, original files through a
`Game` junction, and unpacked EXEs in `work/unpacked/`. These are prepared locally in
this workspace; the test Python is `../.vr-tools/python/Scripts/python.exe` and the
reference repository is `../.vr-tools/faithful` (also reachable through `../F117-2`).
For this non-default build directory, set `F117_COMM` to the absolute path of
`f117port/build-vr/shell_ref/ground/comm.bin` after the `shell` test, and `PYTHONPATH`
to the absolute reference `tools` directory before running the remaining ctests.

## Launcher

`f117-enhanced-launcher.exe` is the faithful port's launcher (game folder, window, speed, sound, key bindings)
plus an **Enhanced** box; its settings are kept in `%APPDATA%\F-117A Enhanced\settings.ini`.

* **Render scale**: Auto, or 1 to 8 times the original 320x200.
* **Frame rate** (`--fps`): Monitor (the default, not passed), 60, 30 or Off.
* **Wide view**, **Smooth distance shading**, **True night**.
* **Classic**: the original game; greys out the options above.

## Layout

* `f117port/src/` — the faithful engine (`ENGINE.md`; changes marked `ENH:`).
* `f117port/src/enhanced/` — the enhancements.
* `port/notes/`, `port/spec/*_symbols.csv`, `port/symbols.csv` — the verification harnesses and symbol tables the
  build and tests use (copied from the faithful repo).
* `tools/` — symbol generators, packaging and the plan page.

## License

MIT ([LICENSE](LICENSE)); third-party licences in `licenses/`. *F-117A Nighthawk Stealth Fighter 2.0* is ©
MicroProse; its files are not included.
