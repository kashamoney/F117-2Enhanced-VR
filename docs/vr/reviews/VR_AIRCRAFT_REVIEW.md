# Water cleanup and aircraft readability — S8.19

Development record: linked images and logs are local, ignored `work/` outputs,
not bundled GitHub assets. Run commands from the repository root. See the
[developer guide](../DEVELOPMENT.md) to regenerate evidence.

The normal VR launchers now remove the old sea-dot scenery, correct a moving
camera/aircraft position jump, and improve the point-to-model transition.
All testing in this session used desktop preview; SteamVR was not started.

## Moving aircraft

The S8.18 extended-coordinate path mixed rounded camera map cells with the
fractional part of a precise camera position. Crossing a cell midpoint displaced
an aircraft by an extra 32 world units. This explains why a stationary carrier
view could look good while moving through a dogfight could expose jumps.

A one-aircraft fixture moves the camera by one world unit per sample through two
cell boundaries. The original test failed with a maximum movement **33.00x** its
median. Matching the cell origin and fractional remainder corrects it. The final
near-model sweep measures **1.00x / 1.02x** on the two camera axes, and the far
point sweep **1.00x / 1.00x**. All 256 samples retain the aircraft.

The distance point also uses precise world coordinates and the same eye/camera
matrix as the model, avoiding whole-map-cell stepping during the handoff.

- Before: `work/vr-benchmark/1a93f7465d/vr-aircraft-motion.csv`.
- Final: [motion trace](../../../work/vr-benchmark/dc7783bb86/vr-aircraft-motion.csv).
- Recheck: `python tools/vr/check_vr_aircraft_motion.py TRACE.csv`.

This reproduces and fixes a definite renderer position jump. An actual headset
MiG dogfight still needs the user's visual review; no change to simulation or
turning behavior is claimed.

## Readable silhouettes

Aircraft grow smoothly from normal size toward **2x visual size** over
800–3500 physical metres. The scale uses range so looking away from the aircraft
does not make its size pump. Nearby aircraft remain at normal size. Original
model parts enlarge around the original model origin; gameplay positions and
hit geometry do not change.

The point now shrinks smoothly as the mesh gains several pixels of projected
coverage. An almost edge-on model retains a point, keeping it visible through
the transition. This combines a capped model-size boost with a minimum visible
mark, rather than giving all distant objects an unlimited apparent size.

The full distance sweep has 512 samples, no representation gaps or invalid faces,
and seven checks of actual GPU coverage. Comparing against
`--vr-original-aircraft-size` verifies unchanged close size, approximately 1.92x
silhouette width at the fixture's 3 km sample and 2x farther out. Perspective can
make the measured projected width slightly exceed 2x, while vertex scaling is
strictly capped at 2x.

Actual game images: [previous size at 6 km](../../../work/vr-benchmark/285a975626/vr-aircraft-24576.png),
[new size/point treatment](../../../work/vr-benchmark/dc7783bb86/vr-aircraft-24576.png).
These are fixture-distance labels, with the original game's exaggerated model
proportions retained; they are not a claim about sensor/detection range.

## Sea dots

The original sea texture is a separate seven-vertex model drawn as seven
zero-length lines. The same structural signature was checked in all nine
theatre banks. GPU presentation suppresses that pattern; individual lights and
other scenery remain. The newer wavelets, coast edges, ship wash and atmospheric
shading continue to work.

Eleven controlled views verify that restoring the legacy points adds exactly two
triangles per removed visible point, with no other geometry change. The views
remove 3,139–3,614 visible points each. Ripple, shoreline and wash comparisons
still affect only visible water. The range benchmark drops from 18,836 to 5,140
triangles. Final preview work is 5.595 ms per stereo frame at 1920x1920/8x MSAA;
this is not a headset delivery measurement.

## Checks and comparison switches

Both builds and ten focused checks pass. The scripted mission retains all 213
engine hashes and the final roster, repeated-pose rendering is exact, and engine
state is restored (`work/vr-check/fbf15ac9ce`). The full presentation comparison
passes in `work/vr-presentation/271afc2d50`; final motion, coverage and size checks
use `work/vr-benchmark/dc7783bb86`.

Launch normally with `tools/vr/Carrier-VR.cmd` or `Play-VR.cmd`. For comparisons:

```powershell
.\Carrier-VR.cmd -OriginalAircraftSize
.\Carrier-VR.cmd -LegacyWaterPoints
```

`-NoWaterDetail` disables the newer wavelets while leaving the old dots removed.
`-OriginalPresentation` restores the original presentation. Headset comfort and
appearance were not evaluated in this session.
