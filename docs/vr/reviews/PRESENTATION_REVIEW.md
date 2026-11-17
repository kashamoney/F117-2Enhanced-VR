# VR world presentation — S8.17

Development record: linked images and logs are local, ignored `work/` outputs,
not bundled GitHub assets. Run commands from the repository root. See the
[developer guide](../DEVELOPMENT.md) to regenerate evidence.

The additions are enabled in the rebuilt VR executable used by `tools/vr/Carrier-VR.cmd`
and `Play-VR.cmd`. Relaunch the game to use them. The accepted cockpit layout is
unchanged. No altitude/descent warning, HUD cue or warning sound was added.

The default render cutoff is doubled (with the original signed-coordinate limit),
and the terrain neighbourhood grows from 5x5 to 9x9. A host render queue retains
objects that the original 50-record queue would discard, including each object's
stereo scale and original equal-depth order. Existing lower-detail models remain
in use. Models blend towards the original horizon colours near their outer
cutoff. This affects rendering inside saved-state eye passes; detection, target
selection, AI and weapon ranges remain the original's.

Sparse world-anchored wavelets use the existing water colours. Their spacing,
contrast and coverage fade with distance and screen footprint. Shore edges blend
a narrow brighter band on the water side. Original ships are stationary scenery:
they receive restrained waterline wash, rather than long trails suggesting motion.
Carrier wash follows the rendered game hull/deck bounds, including the game's
exaggerated proportions. The waterline is hidden behind the deck from the normal
seated carrier start; the detached exterior comparison below makes it visible.

Sky/ground bands interpolate adjacent original palette entries. Models use smooth
distance shading and haze. Existing landing paint receives a small contrast lift;
source model light markers get slightly wider coverage and small halos. Effects
run before the cockpit, instrument and HUD layers. Original assets are not edited.
The added surface/depth data stays on the GPU, rather than uploading larger CPU
eye images.

## Actual on/off renders

The pairs contain native game-world renders, with no generated replacement art.
Each pair uses the same camera and completed simulation snapshot. The coast camera
was located from a top-down land/water mask of the original rendered terrain;
only the saved-state display camera moves. View 10 is a detached exterior camera,
not the normal seated viewpoint. Images were generated for user visual review;
the automated results below measure coverage and state, not comfort or appearance.

| Addition | Evidence | Camera |
| --- | --- | --- |
| Draw Range | [Full-size paired render](../../../work/vr-presentation/95bc741dd2/draw-range.png) | Original GPU world captures, view 0; same pose/time. |
| Water | [Full-size paired render](../../../work/vr-presentation/95bc741dd2/water.png) | Original GPU world captures, view 10; same pose/time. |
| Shoreline | [Full-size paired render](../../../work/vr-presentation/95bc741dd2/shoreline.png) | Original GPU world captures, view 0; same pose/time. |
| Ship Wash | [Full-size paired render](../../../work/vr-presentation/95bc741dd2/ship-wash.png) | Original GPU world captures, view 10; same pose/time. |
| Atmosphere | [Full-size paired render](../../../work/vr-presentation/95bc741dd2/atmosphere.png) | Original GPU world captures, view 0; same pose/time. |
| Landing | [Full-size paired render](../../../work/vr-presentation/95bc741dd2/landing.png) | Original GPU world captures, view 4; same pose/time. |

The full textured stereo cockpit is also captured in
[the enabled mirror](../../../work/vr-benchmark/a843de7e20/vr-mirror.png).
The [original-presentation mirror](../../../work/vr-benchmark/fd830093e2/vr-mirror.png)
uses the master opt-out. Fine detail is easier to judge in the full-size paired
world renders than in the smaller desktop mirror.

## Checks and practical limits

- Both Windows builds compile; 10 focused ctests pass per build. The VR math check
  includes conservation of 83 queued objects through the 50-slot overflow, their
  individual scales, original tie ordering and the original-distance bypass.
- All five enabled/disabled image comparisons pass. Wavelets change 57,307 pixels,
  shore edges 16,838, and ship wash 51,383 across 11 views; all changes from these
  three water effects remain within actual visible water coverage. Atmosphere and
  landing-detail switches also produce distinct images. Same-pose/time metadata
  is checked for every pair.
- The extended-range scene retains up to 89 overflow records that the original
  render queue would drop. The original-distance switch restores the old queue.

- Enabled carrier preview: **3.222 ms/stereo frame**, 1,970 triangles, 6.447 MiB
  upload per stereo frame, 1920x1920 per eye / 8x MSAA. Enabled coast fixture:
  **5.095 ms/stereo frame**. These local desktop measurements include GPU
  completion but exclude headset/compositor wait; they are not headset timing
  or comfort measurements. Runs with individual effects disabled vary with
  other machine load and are not evidence that enabling an effect speeds it up.
- Scripted mission: **213 engine hashes and final pilot roster match exactly**;
  repeated-pose geometry/palette and render-state restoration pass. Final run:
  `work/vr-check/485d5f0360/`.
- Frozen camera roll sweep passes with zero unexpected sky changes, exact repeated
  pose and worst normalized edge-coverage error 0.090 against a supersampled
  reference: `work/vr-benchmark/a88c25295e/`.
- Original-presentation differential check passes; deck coverage remains
  90.9 / 97.2 / 100 percent for down/port/starboard, all 223,776 instrument pixels
  remain present, and all 2,806 HUD ink pixels remain visible. This check uses
  the master opt-out because the old software rasteriser does not implement
  the new styling. Evidence: `work/vr-benchmark/fd830093e2/`.
- Real-clock preview pacing also passes: 200 stereo submissions share matching
  poses/alpha/display time, with zero interpolation restarts across ten simulation
  frames. This test does not start a headset: `work/vr-benchmark/10ce2fab0e/`.
- The 1x MSAA fallback and explicit `--smooth-shading` run successfully on the
  GPU. CPU shading planes are bypassed during GPU geometry capture. Evidence:
  `work/vr-benchmark/d7539e702f/`.
- Additional legacy shell, render3d-leaf and sound checks pass. The configured
  `sim_vgame`, `render3d_golden`, `debrief` and `r3d_hr_smooth` tests report skipped; they are not
  counted as passes. The full scripted mission and image checks above ran.
- SteamVR was not started for these tests. Headset appearance/comfort is U-VR17.

## Controls and reproduction

The additions apply to GPU VR and desktop stereo preview. The normal desktop,
`--classic` and software VR rendering keep their existing paths. `--true-night`
still selects the software fallback; ordinary game night palettes work on the GPU.

Use `tools/vr/Start-VR.ps1 -OriginalPresentation` for the previous presentation, or
independent switches `-OriginalDrawDistance`, `-NoWaterDetail`, `-NoShorelines`,
`-NoWakes`, `-NoAtmosphere` and `-NoLandingDetail`. Executable equivalents are
listed in `README.md`. No headset resolution increase is needed.

```powershell
python tools/vr/check_vr_presentation.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../play/Game
python tools/vr/test_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../play/Game --resolution 1024
python tools/vr/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../play/Game --validate --hud-check
```

Controlled comparisons freeze only benchmark interpolation alpha. Normal play and
real-clock pacing tests retain the snapshot-owned interpolation clock. Benchmark
and integration tools use disposable copies of the game and pilot roster.
Detailed feature results: `work/vr-presentation/95bc741dd2/results.json`.
