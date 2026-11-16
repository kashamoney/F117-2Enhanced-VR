# VR targeting HUD and point projection — S8.20

The missing bombing cues were a VR rendering defect. Target selection on the
right MFD was working, but the isolated HUD texture omitted the original drawing
stage that produces target boxes, weapon-lock symbols, bombing diamonds and
release bars. The earlier HUD coverage test exercised ordinary text/marks, so it
did not catch that omission.

The exporter now prepares the normal forward-view camera/viewport and runs the
original target-symbol stage before the ordinary HUD text. World effects and MFD
drawing are excluded from this isolated pass: those already have stereo-scene
and instrument renders. Original targeting and release rules are unchanged.

The new regression selects an existing ground target inside a disposable render
snapshot, compares target-selected and target-cleared HUD images, and requires
both release-bar and target-area pixels. It covers retarded bombs, selecting
Durandal when present, and free-fall bombs. It failed before the fix with zero
pixels in both regions. Retarded bombs use 800 ft; free-fall bombs use 4000 ft to
avoid deliberately testing the original low-altitude warning's hidden blink.

| HUD fixture | Before: bar / target pixels | Fixed: bar / target pixels |
| --- | ---: | ---: |
| Retarded bomb | 0 / 0 | 584 / 702 |
| Free-fall bomb | omitted drawing stage | 1096 / 510 |

Actual exported HUD images:
[retarded bomb](work/vr-benchmark/1739daf9a3/vr-hud-bomb-29.png),
[free-fall bomb](work/vr-benchmark/1739daf9a3/vr-hud-bomb-30.png).
The generic HUD also preserves all 10,383 original ink pixels, with zero missing
pixels or background leakage. These are controlled fixture renders, not captures
of the user's current mission.

The distant-aircraft point had a separate error introduced by its precise-world
projection: it used the engine camera's vertical sign and scale directly, while
the GPU world reverses that axis and removes its 0.75 vertical compression.
That made points move vertically in the opposite direction and at another scale
as the pilot pitched their head. The point now uses the same ray convention as
the model geometry.

The new head-pose regression compares the point's actual draw projection with
the model origin at two distances through yaw, pitch, roll and tracked
translation. All twelve cases were present. Maximum angular-ray disagreement
fell from **0.541517** to below the trace's nine-decimal precision.

- Before point trace: `work/vr-benchmark/804f53373f/vr-marker-poses.csv`.
- Fixed [point/head-pose trace](work/vr-benchmark/31cc79ce71/vr-marker-poses.csv).
- Recheck: `python tools/check_vr_marker_alignment.py TRACE.csv`.
- Bomb regression: run `tools/benchmark_vr.py` with `--hud-check` and the usual
  executable/game-directory arguments. It uses disposable game files.

Both builds and ten focused checks pass. The scripted mission retains 213 exact
engine hashes and its final roster, with exact repeated-pose rendering and state
restoration (`work/vr-check/4387c9f033`). The distance and camera-motion checks
remain passing with no missing representations. Original-presentation
GPU/software validation, 223,776 instrument pixels and all 2,806 HUD pixels in the
final cockpit pass (`work/vr-benchmark/1aa5be9efa`). The enhanced preview averages
5.255 ms of work per stereo frame at 1920x1920/8x MSAA; headset delivery was not
measured. SteamVR was not started.

Relaunch the normal VR launcher to load the new build. In GND mode with Durandal,
acquire a target ahead, line up the steering diamond, open the bay with **8**, and
press **Enter** as the release bar collapses. Confirm the target box/cues and
point motion in the headset when convenient.
