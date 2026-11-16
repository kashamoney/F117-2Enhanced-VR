# VR input pacing and extended visibility — S8.18

The subsequent [S8.19 aircraft/water update](VR_AIRCRAFT_REVIEW.md) corrects a
camera-cell position jump, removes sea speckles and improves silhouette visibility.

Eight times the original render range is now the default in `Carrier-VR.cmd`
and `Play-VR.cmd`. Use `-DrawDistance 4` for the lighter setting, or select
1, 2, 4 or 8. Resolution and the accepted cockpit are unchanged.

## Why held thrust slowed the world

Keyboard peeks pump host events. Consuming repeated keys introduced extra host
pumps, each of which could block in the next OpenXR frame wait before the same
simulation step could finish. Stereo submissions stayed smooth while world
updates became slower. Instrument texture updates were not the cause: panel,
HUD and geometry work stayed steady, with zero interpolation restarts.

The regression uses real SDL repeated key events after flight begins, checks
that actual throttle values change, and compares world-step timing against a
control run. It uses disposable game copies and scratch pilot rosters.

| Evidence | Control world-step p95 | Held thrust p95 |
| --- | ---: | ---: |
| Before, 90 Hz wait reproduction (`work/vr-hitches/d6954e465c`) | 104.39 ms | 126.79 ms |
| Fixed, actual Odyssey+/SteamVR (`work/vr-hitches/1df669568f`) | 109.89 ms | 107.92 ms |
| Final 8x preview, repeat with reversals (`work/vr-hitches/a192fa539b`) | 113.69 ms | 114.32 ms |

The minimized held-only reproduction also failed before the fix at 126.46 ms.
Both actual headset scenarios submitted 900 stereo frames, with median spacing
11.112 ms. Some individual missed frames remain in the native trace; this
measurement establishes removal of the repeat-key penalty, not perfect delivery
under every workload. The original simulation remains approximately 10 Hz.

Frame servicing now uses the runtime's predicted display period to leave time
for input and simulation between submissions. Pending panel changes are retained
until the next serviced frame. OpenXR's blocking pacing behavior is documented in
[`xrWaitFrame`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html)
and [`XrFrameState`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFrameState.html).

## Farther models and a continuous point/model handoff

Increasing a cutoff alone was insufficient. Legacy cell offsets, camera
translations and the object-frustum depth test also imposed 16-bit limits.
The GPU eye pass now retains full render coordinates, uses a bounded proxy for
the original object renderer, and restores the full translation for GPU vertices.
The host queue retains every queued model and sorts by physical depth, keeping
the original equal-depth ordering. The terrain neighbourhood grows from the
original 5x5 cells to 17x17 at 4x and 33x33 at 8x.

The old 2x path lost an aircraft completely from 3,312.5 to 3,937.5 metres in the
controlled fixture (`work/vr-benchmark/ac664e84c7/vr-visibility.csv`). The point
threshold preceded a separate mesh-depth limitation. The extended frustum and
coordinates remove that gap. Aircraft detail handoffs now move with the selected
range too.

A second check renders an actual GPU aircraft mask. This caught original coarse
aircraft consisting almost entirely of edge-on faces: triangles were submitted
but covered zero samples. The original white point now remains until the model
has a minimum projected footprint. Aircraft retain atmospheric haze, while the
final terrain-to-horizon fade cannot erase their visibility marker.

| Setting | Distance samples | Missing representations | Farthest mesh |
| --- | ---: | ---: | ---: |
| 4x | 512 | 0 | 6.5 km |
| 8x | 512 | 0 | 13.0 km |

Final traces: [4x sweep](work/vr-benchmark/7ca603ce15/vr-visibility.csv) and
[8x sweep](work/vr-benchmark/a73684c2c3/vr-visibility.csv). Actual game renders
from the 8x trace: [1 km](work/vr-benchmark/a73684c2c3/vr-aircraft-4096.png),
[6 km](work/vr-benchmark/a73684c2c3/vr-aircraft-24576.png),
[12 km](work/vr-benchmark/a73684c2c3/vr-aircraft-49152.png).

The sweep runs from 62.5 metres to 32 km. Seven GPU coverage samples per setting
verify actual visible pixels, with a second isolated-aircraft mask to distinguish
occlusion from a subpixel mesh. At 8x the 1 km and 6 km samples have 41 and 17
visible aircraft pixels; the 12 km coarse model retains the point (29 pixels).
These are a deterministic aircraft/pose fixture, not universal detection ranges.
Targeting, sensors, weapons, AI and original model assets are unchanged.

## Performance and verification

The Odyssey+ native 8x benchmark used two 1716x1868 eye targets with 8x MSAA:
180 measured stereo frames after 120 warmup frames averaged 7.216 ms of work
including GPU completion and excluding OpenXR wait, with 89.5 submitted frames/s,
18,566 triangles and no invalid faces (`work/vr-benchmark/5fc30424f1`). This
headset measurement preceded the final detail/point refinements; final checks
continued in desktop preview after releasing the headset at the user's request.
SteamVR was closed when released and was not restarted for those checks.

The final 8x presentation comparison (`work/vr-presentation/3338555dcf`) preserves
all five accepted effects. Water detail, shorelines and ship wash change only
visible water. The extended-range fixture retains up to 517 overflow records.
The 1920x1920/8x MSAA preview averages 6.636 ms in the airborne range scene and
6.280 ms at the coast; timings vary with system load.

Both builds and focused checks pass. The scripted mission preserves all 213
engine hashes and the final roster (`work/vr-check/129c035142`), including exact
repeated-pose rendering and simulation restoration; desktop stereo smoke passes.
Original-presentation GPU/software and cockpit checks
(`work/vr-benchmark/713f22b5ab`) preserve 223,776 instrument pixels and all 2,806
visible HUD pixels. The frozen 0–90 degree roll check
(`work/vr-benchmark/17a9b79a58`) has zero unexpected sky changes and worst
normalized edge-coverage error 0.093. Images are generated for user review;
no new headset appearance/comfort judgment is claimed.

The final held-input trace also passes stereo-state and interpolation checks:
900 matching submissions across 105 simulation frames, with zero restarts.
An earlier preview comparison (`work/vr-hitches/6691b3033f`) narrowly missed the
one-frame p95 tolerance and recorded a 139 ms submission outlier while legacy
reference tests were running. The repeat above passed the same unchanged
threshold, with no submission gap above 17.4 ms across either scenario.
The legacy shell, render3d-leaf and sound checks pass. Configured sim_vgame,
render3d_golden, debrief and r3d_hr_smooth checks skip; they are not claimed as
passes. The desktop test interpreter was corrected to the existing environment
containing Unicorn after the system Python failed to import it.

Reproduce without starting SteamVR:

```powershell
python tools/check_vr_hitches.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../play/Game --reversals
python tools/check_vr_visibility.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../play/Game --draw-distance 8
python tools/check_vr_presentation.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../play/Game
```

When convenient, review held thrust, aircraft approaching from a distance and
scenery pop-in in the headset. Compare `Carrier-VR.cmd -DrawDistance 4` if 8x
costs too much frame time in a different theatre or with other GPU workloads.
