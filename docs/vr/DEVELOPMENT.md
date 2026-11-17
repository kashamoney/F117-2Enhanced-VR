# VR rendering and development

Run command examples from the repository root and replace `D:/Games/F117` with
your own game folder. [Player setup and controls](README.md) are documented separately.

Review reports under [reviews/](reviews/) are development records. Their images
and logs live in ignored local `work/` directories and are not shipped in Git.

## Build and verify

Use the MSYS2 prerequisites in the [root README](../../README.md). From PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\vr\Build-VR.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\vr\Build-VR.ps1 -DesktopOnly
```

The helper accepts `-MsysRoot` and an optional `-PythonExecutable` path for CMake's
reference tests. It does not look for a Python installation in a sibling folder.
For example, create and activate your own virtual environment, install `numpy`,
`Pillow` and `unicorn`, then pass its `Scripts/python.exe`. Canopy fitting additionally
uses SciPy. Runtime play does not require the faithful companion repository;
reference verification uses its tools/specs from `../reference` or `../F117-2`.

The helper omits the desktop GUI launcher and runs ten native checks. Full reference
verification also needs original game files via `Game/` and unpacked executables in
`work/unpacked/`. For non-default build directories, run `shell` first and set
`F117_COMM` to the resulting `shell_ref/ground/comm.bin`; set `PYTHONPATH` to the
companion's `tools` directory before the remaining tests.

All VR scripts live together in `tools/vr/`; Python imports between those scripts
use their shared directory, and repository paths are resolved from the script
location rather than the caller's working directory. Supply game/executable paths
explicitly to benchmark scripts. Example:

```powershell
python tools/vr/test_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117" --resolution 1024
```

Portable launcher regression checks copy the scripts, binaries and supplied game
files into an isolated checkout-shaped folder with spaces. They exercise first-use
errors, remembered/environment/argument choices, all shortcuts and invalid settings
without starting SteamVR or changing the supplied roster:

```powershell
python tools/vr/test_launcher.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117"
```

## Package a Windows VR download

The release version lives in `f117port/src/enhanced/vr_release.h`, independently
of the upstream engine version. Native Windows setup is in `vr_launch.cpp`; its
C ABI hook in `main.c` keeps ordinary developer/test command lines unchanged.
The default EXE launch enables VR, selects/validates original game data, and
remembers the folder through the Windows user profile. Desktop builds omit it.

The default profile is `%APPDATA%\F117-Enhanced-VR\settings.ini`. On upgrade,
the former publisher-prefixed profile is copied only if the new file is absent;
the original stays intact and a pre-existing new profile always wins. Explicit
`--vr-config` and `F117_VR_CONFIG` profiles do not import default settings.
`--vr --check` validates the remembered default without opening a headset session.
Release tests isolate APPDATA to check migration without changing user settings.

Commit source changes before packaging a public build:

```powershell
python tools/vr/package_release.py --game-dir "D:/Games/F117"
python tools/vr/test_release.py --package release/F117-Enhanced-VR-v0.1.0-preview.2-win64.zip --game-dir "D:/Games/F117" --check-dialog
```

Packaging performs a Release build and ten focused checks, includes the exact
runtime DLL set and license notices, records the source commit/file hashes in
`RELEASE.json`, and writes the ZIP plus `SHA256SUMS.txt`. Original game data is
read only as an exclusion reference: every staged file and the final ZIP are
checked by game-file name and content. No roster or game asset is distributed.
Use a fresh `--output-dir` for candidates; `--allow-dirty` is for internal
candidates only. Review runtime dependencies if their DLL set changes.

`test_release.py` extracts into paths with spaces and accented characters, strips
build tools from the child process PATH, validates native folder choices and
unsupported-game rejection, and renders a real D3D11 preview. `--check-dialog`
also starts the no-argument EXE and cancels only that test process's first-use
folder picker before OpenXR starts. Scripted mission checks can additionally
point `tools/vr/test_vr.py` at the extracted EXE. Headset comfort/reprojection are
outside desktop automation. Publish preview ZIPs as GitHub prereleases, with a
tag identifying the exact tested source commit.

## Update from upstream

Keep `origin` pointing to this fork and `upstream` pointing to
`https://github.com/kylofon/F117-2Enhanced.git`. With a clean working tree:

```sh
git fetch upstream
git switch main
git merge --ff-only upstream/main
git push origin main
git switch feature/openxr-vr
git switch -c integration/upstream-YYYY-MM-DD
git merge upstream/main
```

Resolve conflicts while preserving the VR hooks, fork README and `tools/vr/` paths.
Build both variants, run affected upstream/VR tests, then perform a headset flight
covering tracking/recenter, cockpit/HUD alignment, targeting, distant objects,
pause/resume and pacing. Record checks and limits in PLAN.md. After review:

```sh
git switch feature/openxr-vr
git merge --ff-only integration/upstream-YYYY-MM-DD
git push origin feature/openxr-vr
```

Keep upstream-owned documentation and engine paths stable to limit future conflicts.
VR additions belong in `tools/vr/` and `docs/vr/`; only the Play/Preview shortcuts
belong at the repository root. `.vr-local.json`, game files and generated `work/`
outputs stay outside Git. Review-report links to `work/` are local evidence only.

## Rendering, options, and limits

World polygons now rasterise on the GPU directly at the OpenXR eye target, with 8x
MSAA (falling back to 4x, 2x or 1x as supported). The default `-Resolution 0` uses the runtime recommendation
(on this Odyssey+/Oasis setup: **1716x1868 per eye**). A nonzero `-Resolution`, or
`--vr-resolution 640..4096`, sets the actual eye **height** and preserves the runtime's
width/height ratio. For example, `-Resolution 2160` increases GPU sampling without
increasing the CPU instrument canvas. Desktop preview defaults to 1920x1920 per eye.
The desktop mirror stays 1024x1024, the live panel stays 1280x800, and its HUD has
an independent transparent 1280x364 texture.

The previous 4096-square software source was a workaround: the headset sampled only
part of that image, and two full 32-bit images were uploaded every frame. The GPU
path sends original geometry and a palette instead. No original assets or flight
rules change. The original palette AND/OR operations and stippled faces are retained
in an integer GPU target before colour conversion and antialiasing.

The GPU VR presentation defaults to eight times the original model/draw cutoff
and a 33x33 terrain neighbourhood (originally 5x5). Aircraft detail transitions
move farther out too; their original white point remains when a distant or
edge-on model is too small to cover a pixel. A host render queue retains scenery
beyond the original 50-record limit. Terrain fades into the distant haze;
targeting, detection and weapon ranges remain the original's.

Distant aircraft grow gradually up to twice their normal visual size. A small
point shrinks away as the silhouette gains readable coverage, keeping the
handoff visible. Nearby aircraft retain their normal size. `-OriginalAircraftSize`
restores the previous size/point treatment for comparison. Camera-cell rounding
has also been corrected to prevent moving aircraft from jumping at cell boundaries.

The VR HUD includes the original target boxes, weapon-lock cues and bombing
diamond/release bar. Distant points use the same projection as the 3D world
through head movement. [`VR_TARGET_REVIEW.md`](reviews/VR_TARGET_REVIEW.md) records the
targeting and head-pose checks.

Use `tools/vr/Carrier-VR.cmd -DrawDistance 4` for a lighter 4x setting, or pass
`-DrawDistance 1`, `2`, `4` or `8` to either VR launcher. The extended coordinates,
model/point handoff and held-thrust timing checks are documented in
[`VR_VISIBILITY_REVIEW.md`](reviews/VR_VISIBILITY_REVIEW.md).

Sparse blue wavelets stay on the sea in world coordinates and fade with distance
and pixel footprint. The original seven-dot sea scenery is removed; the newer
water surface replaces it. `-LegacyWaterPoints` restores the dots for comparison.
Coast edges and restrained wash around original ships help
judge the surface. Ships are stationary scenery, so they get waterline wash rather
than long moving wakes. Sky/ground bands and atmospheric shading interpolate the
original palette colours. Existing landing paint gets a small contrast lift, and
actual model lights get slightly wider coverage and small halos. There is no new
altitude/descent warning, HUD cue or sound.

These effects are enabled by `Play-VR.cmd` and `tools/vr/Carrier-VR.cmd`. To compare:

```powershell
.\tools\vr\Start-VR.ps1 -Preview -CarrierPractice -OriginalPresentation
.\tools\vr\Start-VR.ps1 -NoWaterDetail -NoWakes
```

Each addition has an independent switch:

| Launcher option | Executable option |
| --- | --- |
| `-OriginalPresentation` | `--vr-no-presentation` |
| `-DrawDistance 8` (default; 1, 2, 4, 8) | `--vr-draw-distance 8` |
| `-OriginalDrawDistance` | `--vr-original-distance` |
| `-OriginalAircraftSize` | `--vr-original-aircraft-size` |
| `-LegacyWaterPoints` | `--vr-legacy-water-points` |
| `-NoWaterDetail` | `--vr-no-water-detail` |
| `-NoShorelines` | `--vr-no-shorelines` |
| `-NoWakes` | `--vr-no-wakes` |
| `-NoAtmosphere` | `--vr-no-atmosphere` |
| `-NoLandingDetail` | `--vr-no-landing-detail` |

`-Software` / `--vr-software` keeps the old software eye path for comparison; its
resolution remains a square source size (2048 if unspecified). `--smooth-shading`
now stays on the GPU path. `--true-night` still selects the software fallback;
normal game night palettes work on the GPU. The presentation additions apply to
the GPU VR/preview path; desktop/classic and the software fallback retain their
existing rendering. Evidence and validation are in `PRESENTATION_REVIEW.md`.

`--vr-world-scale 0.1..10` sets physical metres per game metre; default 1.0 uses the
original 512 km theatre / 0x100000 world-unit mapping (about 0.488 m per renderer unit).
The game's exaggerated vertical scale and high-altitude compression are preserved,
so HUD altitude is not a literal VR ruler.
`--vr-no-cockpit` starts with the cockpit hidden. `--classic` disables VR.
`--vr-dump` writes the first pair of eye images, the GPU cockpit mirror, and matched-projection
GPU/software reference images and front/left/right/rear, front-quarter and upward cockpit
previews to the working directory.
Capture waits for a settled mission; readbacks are excluded from benchmark warmup/samples.
Session logs are in `work/vr-session/f117-vr.log`; original crash logs remain beside the exe.

Implementation and current limits:

* Each eye renders from the same pre-render snapshot and interpolation alpha. All engine
  memory and canvas state are restored afterward; eye rendering never advances gameplay.
* Sub-foot eye offsets are applied to 16.16 camera translations, with the terrain/model
  scale tracked through the sorted object queue. The eye baseline survives integer rounding.
* Ten live regions of the original cockpit image are mapped onto physical instrument faces.
  One layout supplies both the source pixel rectangles and the matching 3D housings;
  the old painted coaming is replaced by a solid coaming joined to the windshield sill.
  The original displays, warning lamps, mode bar, countermeasures and engine/weapon readouts stay live.
  The enclosed lower hull, footwell, rear bulkhead, faceted glazed canopy, sills, ejection seat/harness,
  side consoles, stick, throttle, pedals and display bezels are cached procedural geometry.
  The canopy has five flat panes: a near-triangular raked windscreen with a narrow
  clipped apex, two triangular front quarter lights and two inward-leaning side windows.
  The side sills rise aft under an opaque faceted roof. The windshield and its
  lower panel are dimensioned around the seated eye and share their sill landmarks
  with the coaming/lower hull. The photo-fitted inspection shell remains a reference;
  its unconstrained cabin depth produced an unusably narrow view from inside.
  The flight model retains the five-pane shape with extra visibility for VR.
  This is an approximation of the interior; switches remain decorative and original instruments stay live.
  Instrument housings stay behind their texture faces and the consoles start beside the pilot.
  A mesh ray test checks 4,590 instrument sightlines from three seated eye positions.
* VR uses the original pilot-eye datum (`aircraft altitude + 0x18`) plus tracked physical
  head displacement. Using an unrelated two-metre offset put the camera below the carrier
  deck model and made the ship disappear. `tools/vr/Carrier-VR.cmd` already selects a carrier; its
  mission and pilot setup are unchanged.
* The prototype HUD runs the original bitmap/font/keyed-symbol and enhanced vector drawing
  routines on an empty sandboxed canvas at the same interpolated aircraft pose as the eyes.
  Its independent transparent layer includes readouts that vector-only provenance omitted;
  scenery cannot enter it, and display/canonical drawing tags cannot cut holes in it.
  The symbology is collimated through an aircraft-fixed six-sided combiner: each eye samples
  the image from its ray through the glass, preserving the original angular scale without
  the former one-metre parallax. The glass sits at 48 cm from the
  recentered eye; its physical size and distance preserve the angular symbol size.
  Glass, rim and support arms are physical geometry; the
  original painted rim is skipped only during the isolated VR HUD export. Premultiplied
  coverage avoids a hard transparency threshold on moving text/line edges.
  Moving outside the combiner aperture clips the image. Recenter with F12 while seated;
  optical alignment, readability and comfort still need review in the headset.
* There are no motion-controller interactions or clickable cockpit switches. Use the existing
  keyboard or gamepad. The original gamepad handling and game rules are unchanged.
* Tracking loss submits an empty frame; a runtime exit closes the game. Errors are logged.
  If startup fails, run `-Diagnostic`. SteamVR must be the active OpenXR runtime. An
  `Oasis_UnlockRequired` result comes from the driver: use the official
  [Oasis unlock instructions](https://github.com/mbucchia/Oasis-Driver-for-Windows-Mixed-Reality/wiki/Procedure-to-unlock-headset-and-controllers-for-Oasis).

Offline integration check (no headset required):

```powershell
python tools/vr/test_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117"
```

This runs a deterministic real mission on scratch copies of the game, checks identical
eye poses, physical stereo disparity and head rotation, and compares engine hashes and
the final pilot roster against a run with VR rendering off. Images/logs are saved under
`work/vr-check/`. `--vr-selftest` checks pose/clock math, cockpit clearance and HUD ray mapping
without game files or a headset.
Add `--preview-smoke` to exercise the native D3D11 presenter in a hidden test window and save `vr-mirror.png` with
the GPU-rendered cockpit. The scene check also exercises GPU geometry capture across stereo,
yaw, pitch and roll, checks exact repeated-pose geometry/palette, and audits engine restoration
throughout the mission. The integration tool accepts `--resolution 1024`, `2048`, or `4096`;
use 1024 for a faster software reference check.

The HUD coverage check compares the exported transparent layer with the original
HUD symbology drawn in isolation, including bitmap text and enhanced lines. The static
painted glass rim is replaced by physical geometry in VR. The check rejects
missing/recoloured HUD pixels and any opaque background pixels:

```powershell
python tools/vr/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117" --hud-check --validate
```

The run saves `vr-hud-reference.png`, `vr-hud-masked.png` (the exported RGBA layer),
the stereo/cockpit previews, and `vr-canopy-profile-front.png` / `vr-canopy-profile-side.png`.
The profiles project the actual cockpit mesh orthographically with equal horizontal and
vertical scale, for comparing window outlines and proportions with the references.
`vr-cockpit-exterior-front-left.png`, `vr-cockpit-exterior-front.png` and
`vr-cockpit-exterior-side.png` show the exact cockpit shell from outside on a neutral ground.
The aircraft fuselage is not included. `vr-panel-source.png` and `vr-cockpit-layout.json`
record the live image and shared instrument mapping. Validation checks that all coloured
instrument pixels are retained, rendered HUD bearings stay fixed across both eyes and
head translation, and bright HUD ink survives the final cockpit render. Isolated optics
images are saved as `vr-hud-optics-{center,left,right,offset}.png`.

The current seated cockpit and archived photo-fitting pass are documented in
[`CANOPY_REVIEW.md`](reviews/CANOPY_REVIEW.md), with actual photographs beside front,
side and front-quarter renders, named landmark errors, and pilot HUD hidden/visible
comparisons. Normal `tools/vr/Carrier-VR.cmd` / `Play-VR.cmd` use the seated adaptation.
The benchmark also exports `vr-canopy-geometry.json` (rejected reference fit),
`vr-canopy-pilot-geometry.json` (flight canopy), and
`vr-canopy-pilot-{shell-only,hud-hidden,hud-hardware,hud-visible}.png`, with instrument
textures hidden throughout. Coloured obstruction masks separate canopy, lower interior
and HUD hardware. `vr-canopy-fitted-*` captures use the seated model, while
`vr-cockpit-pilot-hud-{hidden,visible}.png` retain the live textures and compare the
actual game cockpit with the HUD assembly absent/present. Fixed-FOV eye/depth
experiments are saved as `vr-canopy-seat-*` and `vr-canopy-seat-placements.json`.
`tools/vr/check_canopy_seating.py` probes the compiled exports; the real cockpit
self-test requires clear forward/horizon and upward view from both eyes.
`tools/vr/render_canopy_comparison.py` renders the exported C mesh using the stored
photo cameras in `tools/vr/canopy_fit.json`; it rejects stale geometry. SciPy is needed
only for authoring a new fit with `tools/vr/fit_vr_canopy.py`.

`--headset --pacing --frames 900 --hud-check`
also checks actual runtime pacing. The integration check renders the HUD again
after intervening eye/head poses, verifies pixel equality, and audits state restoration.

A reproducible local performance check uses a hidden D3D11 preview and a disposable carrier
mission (no pilot changes):

```powershell
python tools/vr/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117"
```

It measures 30 stereo frames after warmup, including render/upload/GPU completion, against an
11.11 ms (90 Hz) budget. `--resolution 2160` raises actual eye height; `--software --resolution 4096`
exercises the old source-image pipeline. `--headset` runs against OpenXR; the benchmark excludes frame waiting and tracking lookup.
Use SteamVR frame timing to check delivery and missed display frames. Measurements exclude
mission boot; sustained comfort and performance in other missions remain headset checks.
Current local real-clock mission measurements on the Odyssey+/Oasis: **1716x1868 per eye, 8x MSAA,
5.00 ms average render/upload work, 89.9 submitted frames/s over 900 measured frames**
after runtime warmup. The old 4096-square software path measured 76.68 ms of work per
stereo frame. Mean upload falls from about 128 MiB to about 6.4 MiB per frame. These are local
measurements; startup, other workloads, and mission complexity can affect delivery.
The GPU/software image comparison (`--validate`, requires NumPy/Pillow) checks forward,
yaw, pitch and roll at matching projections after mission initialization has settled. Three
additional downward views require visible carrier-deck coverage; this catches a missing
ship even when both VR software/GPU paths share a wrong camera datum. `--vr-selftest`
also ray-tests the actual cockpit triangles for lower/rear/roof enclosure, clear glazed
window sightlines, and unobstructed HUD/panel sightlines. Headset comfort and sustained flight are
still user checks. To measure the actual runtime with a longer warmup:

```powershell
python tools/vr/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117" --headset --frames 900
```

To capture moving edges, use a deterministic airborne scene with a frozen simulation
and a 90-degree camera roll. The capture uses the real GPU renderer, with lossless
world PNGs and diagnostic sky masks under `work/vr-benchmark/<run>/`:

```powershell
python tools/vr/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117" --temporal --capture-size 1024 --capture-steps 90
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

VR world smoothing uses a clock owned by the current simulation snapshot. A
canonical screen flip must not restart interpolation while that older snapshot is
still being shown. The original simulation remains about 10 steps per second;
the VR camera interpolates between those steps at headset rate. Pause holds the
completed pose, and the resumed snapshot gets a normal step interval.

To audit actual moving-world pacing, including both eye poses and predicted
display times, use a real-clock mission with the normal frame-stream setting:

```powershell
python tools/vr/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir "D:/Games/F117" --headset --pacing --frames 900 --capture-live
```

This differs from the frozen roll sweep: the simulation advances in real time,
both eye passes record the aircraft pose they actually used, and up to 180 live
stereo mirror frames are copied into a bounded GPU buffer. Readback and PNG
encoding happen at shutdown, after timing measurement. `vr-timing.csv` checks
matching stereo state and detects interpolation restarting within one simulation
pair. The previous clock produced 110 restarts across 115 simulation frames;
the corrected path has zero and submits 90 frames/s in this local check.

For a recording while playing, launch `tools/vr/Record-VR.cmd`, fly until the issue is
visible, and press **F9** for one short burst. Close the game to write
`work/vr-session/vr-live-*.png` and `vr-timing.csv`. `tools/vr/Carrier-VR.cmd -TraceWorld`
records only the lightweight timing trace. Normal launches do neither.

```powershell
python tools/vr/encode_vr_live.py work/vr-session
python tools/vr/encode_vr_live.py work/vr-session --slow 6
python tools/vr/check_vr_pacing.py work/vr-session/vr-timing.csv
```

The encoder uses original submission timestamps, including gaps; it does not
turn a synthetic camera sweep into an assumed live frame rate. These recordings
show the application's stereo mirror and world motion. SteamVR reprojection and
the physical headset display/optics are outside that mirror capture.

VR-enabled builds now produce `f117port/build-vr/f117-enhanced-vr.exe`. The launch scripts
select this executable; an already running older build picks up the changes on relaunch.

The full upstream regression suite additionally uses Python `unicorn`, `numpy`, and
`Pillow`, the faithful companion repository's tools/specs, original files through a
`Game` junction, and unpacked EXEs in `work/unpacked/`. These are prepared locally in
a development environment you configure yourself. Install the Python dependencies
in your own virtual environment and activate it before building. The optional
faithful companion checkout may be placed at `../reference` or `../F117-2`.
For this non-default build directory, set `F117_COMM` to the absolute path of
`f117port/build-vr/shell_ref/ground/comm.bin` after the `shell` test, and `PYTHONPATH`
to the absolute reference `tools` directory before running the remaining ctests.
