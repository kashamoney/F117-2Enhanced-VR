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
seated recentering, an enclosed faceted cockpit, and the original live instrument panel.
Menus are displayed on a virtual screen. Keyboard and gamepad flight controls stay the same.
The backend uses OpenXR through SteamVR and Direct3D 11, including headsets connected
through the Oasis driver such as the Samsung Odyssey+. It is an experimental prototype;
headset comfort, scale, and sustained frame timing need testing on the actual headset.

Run **`Play-VR.cmd`** from this repository with SteamVR running and the headset tracking.
The default game folder is `../F117-Enhanced-play/Game` in this workspace. Original files
stay separate from the source and the game saves your pilot roster in that working copy.
Run **`Preview-VR.cmd`** for a desktop stereo preview without a headset.
The desktop mirror shows the two square eye images side by side.
The world now renders on the GPU at the headset's recommended eye resolution
(**1716x1868 per eye** on this Odyssey+/Oasis setup), with 4x antialiasing where supported.
The live panel/HUD texture retains enhanced 4x rendering at 1280x800. VR defaults to
fine dithering; pass `--dither grid` directly to the executable to restore the coarse
original checkerboard. Original bitmap artwork keeps its original detail, while vector
instruments and the 3D world can render more sharply. The desktop mirror stays at
1024x1024 independently of the eye resolution.

For a first headset test, **`Carrier-VR.cmd`** prepares a separate practice copy with a test
pilot and automatically navigates setup/briefing to a Libya carrier mission. Allow the intro
and scripted menus to finish; then the aircraft is stationary on deck and you have control.
This shortcut requires Python 3 and leaves your regular pilot roster untouched. Practice
copies are under `work/vr-practice/`. `tools/Start-VR.ps1 -Preview -CarrierPractice` runs the
same practice scenario on the desktop.

| Control | Action |
|---|---|
| F12 | Recenter the seated position and heading; look straight ahead when pressing it |
| F11 | Show/hide the cockpit |
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
.\tools\Start-VR.ps1 -Resolution 2160 -WorldScale 1.0
.\tools\Start-VR.ps1 -GameDirectory 'D:\Games\F117'
```

World polygons now rasterise on the GPU directly at the OpenXR eye target, with 8x
MSAA (falling back to 4x, 2x or 1x as supported). The default `-Resolution 0` uses the runtime recommendation
(on this Odyssey+/Oasis setup: **1716x1868 per eye**). A nonzero `-Resolution`, or
`--vr-resolution 640..4096`, sets the actual eye **height** and preserves the runtime's
width/height ratio. For example, `-Resolution 2160` increases GPU sampling without
increasing the CPU instrument canvas. Desktop preview defaults to 1920x1920 per eye.
The desktop mirror stays 1024x1024 and the original live panel/HUD stays 1280x800.

The previous 4096-square software source was a workaround: the headset sampled only
part of that image, and two full 32-bit images were uploaded every frame. The GPU
path sends original geometry and a palette instead. No original assets or flight
rules change. The original palette AND/OR operations and stippled faces are retained
in an integer GPU target before colour conversion and antialiasing.

`-Software` / `--vr-software` keeps the old software eye path for comparison; its
resolution remains a square source size (2048 if unspecified). The optional desktop
`--smooth-shading` and `--true-night` effects also select that fallback, because their
per-pixel enhancement planes are not implemented by the GPU backend. Normal game
night palettes work in the GPU path.

`--vr-world-scale 0.1..10` sets physical metres per game metre; default 1.0 uses the
original 512 km theatre / 0x100000 world-unit mapping (about 0.488 m per renderer unit).
The game's exaggerated vertical scale and high-altitude compression are preserved,
so HUD altitude is not a literal VR ruler.
`--vr-no-cockpit` starts with the cockpit hidden. `--classic` disables VR.
`--vr-dump` writes the first pair of eye images, the GPU cockpit mirror, and matched-projection
GPU/software reference images and front/left/right/rear cockpit previews to the working directory.
Capture waits for a settled mission; readbacks are excluded from benchmark warmup/samples.
Session logs are in `work/vr-session/f117-vr.log`; original crash logs remain beside the exe.

Implementation and current limits:

* Each eye renders from the same pre-render snapshot and interpolation alpha. All engine
  memory and canvas state are restored afterward; eye rendering never advances gameplay.
* Sub-foot eye offsets are applied to 16.16 camera translations, with the terrain/model
  scale tracked through the sorted object queue. The eye baseline survives integer rounding.
* The original cockpit pictures are reused on an aircraft-fixed instrument panel. The
  enclosed lower hull, footwell, rear bulkhead, faceted glazed canopy, sills, ejection seat/harness,
  side consoles, stick, throttle, pedals and display bezels are cached procedural geometry.
  This is an approximation of the interior; switches remain decorative and original instruments stay live.
  The decorative bezels are recessed and the consoles start beside the pilot to keep the
  full-size live panel visible. A mesh ray test checks 1,683 panel sightlines from three seated eye positions.
* VR uses the original pilot-eye datum (`aircraft altitude + 0x18`) plus tracked physical
  head displacement. Using an unrelated two-metre offset put the camera below the carrier
  deck model and made the ship disappear. `Carrier-VR.cmd` already selects a carrier; its
  mission and pilot setup are unchanged.
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
python tools/test_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../F117-Enhanced-play/Game
```

This runs a deterministic real mission on scratch copies of the game, checks identical
eye poses, physical stereo disparity and head rotation, and compares engine hashes and
the final pilot roster against a run with VR rendering off. Images/logs are saved under
`work/vr-check/`. `--vr-selftest` checks pose math without game files or a headset.
Add `--preview-smoke` to exercise the native D3D11 presenter in a hidden test window and save `vr-mirror.png` with
the GPU-rendered cockpit. The scene check also exercises GPU geometry capture across stereo,
yaw, pitch and roll, checks exact repeated-pose geometry/palette, and audits engine restoration
throughout the mission. The integration tool accepts `--resolution 1024`, `2048`, or `4096`;
use 1024 for a faster software reference check.

A reproducible local performance check uses a hidden D3D11 preview and a disposable carrier
mission (no pilot changes):

```powershell
python tools/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../F117-Enhanced-play/Game
```

It measures 30 stereo frames after warmup, including render/upload/GPU completion, against an
11.11 ms (90 Hz) budget. `--resolution 2160` raises actual eye height; `--software --resolution 4096`
exercises the old source-image pipeline. `--headset` runs against OpenXR; the benchmark excludes frame waiting and tracking lookup.
Use SteamVR frame timing to check delivery and missed display frames. Measurements exclude
mission boot; sustained comfort and performance in other missions remain headset checks.
Current local carrier measurements on the Odyssey+/Oasis: **1716x1868 per eye, 8x MSAA,
4.88 ms average render/upload work, 90.0 submitted frames/s over 900 measured frames**
after runtime warmup. The old 4096-square software path measured 76.68 ms of work per
stereo frame. Mean upload falls from about 128 MiB to about 5.1 MiB per frame. These are local
measurements; startup, other workloads, and mission complexity can affect delivery.
The GPU/software image comparison (`--validate`, requires NumPy/Pillow) checks forward,
yaw, pitch and roll at matching projections after mission initialization has settled. Three
additional downward views require visible carrier-deck coverage; this catches a missing
ship even when both VR software/GPU paths share a wrong camera datum. `--vr-selftest`
also ray-tests the actual cockpit triangles for lower/rear enclosure and clear glazed
window sightlines. Headset comfort and sustained flight are
still user checks. To measure the actual runtime with a longer warmup:

```powershell
python tools/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../F117-Enhanced-play/Game --headset --frames 900
```

To capture moving edges, use a deterministic airborne scene with a frozen simulation
and a 90-degree camera roll. The capture uses the real GPU renderer, with lossless
world PNGs and diagnostic sky masks under `work/vr-benchmark/<run>/`:

```powershell
python tools/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../F117-Enhanced-play/Game --temporal --capture-size 1024 --capture-steps 90
```

The check detects discontinuous horizon coverage, stale frames at a repeated pose,
and excessive edge-coverage error against a denser render at each sampled pose.
`--samples 4` reproduces the previous antialiasing level for comparison. This is a
renderer regression scene, not a measurement of headset delivery, reprojection or optics.
NumPy/Pillow are required. To encode the 91 world frames as a 90 fps clip (requires FFmpeg):

```powershell
ffmpeg -framerate 90 -i work/vr-benchmark/<run>/roll-%03d.png -frames:v 91 -c:v libx264 -crf 12 -pix_fmt yuv420p roll-90fps.mp4
```

For six-times slow motion, use `-framerate 15`, add `-vf fps=30`, and set `-frames:v 182`. The PNGs retain
edge detail that video compression may obscure. Headset inspection is still required
to judge the remaining shimmer during flight.

VR-enabled builds now produce `f117port/build-vr/f117-enhanced-vr.exe`. The launch scripts
select this executable; an already running older build picks up the changes on relaunch.

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
