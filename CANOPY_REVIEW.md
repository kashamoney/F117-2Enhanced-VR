# Canopy geometry pass — 2026-11-13

The photo-fitted canopy is an **inspection candidate**. It has an opaque grey shell, a distinct lower windshield apron, one clipped triangular windscreen, two triangular quarter windows and two swept side windows. Exterior comparisons contain only the shell and contrasting blue aperture fills. The instrument textures and HUD are absent.

The exterior proportions are closer to the annotated photographs, but the pilot-layer captures expose a substantial visibility problem at the current eye position. The existing HUD also extends beyond the new windshield opening. This candidate is therefore staged separately from the cockpit used by `Carrier-VR.cmd`; it is not ready to replace that cockpit.

## Actual photographs beside compiled geometry

Each pair preserves the photograph's aspect ratio and uses a fitted pinhole camera for the render. The front reference is cropped from the complete USAF photograph while preserving its original principal point. Camera rotation, translation, focal length and principal point are saved with the evidence. This is an approximate perspective fit with unknown lens calibration and unmeasured aircraft dimensions.

| View | Source pixels per panel | Landmark RMS | Largest error |
|---|---:|---:|---:|
| Front | 1120 × 840 | 5.00 px | 11.14 px |
| Side oblique | 902 × 601 | 7.00 px | 14.45 px |
| Front three-quarter close-up | 800 × 600 | 7.75 px | 14.94 px |
| Supplied side photograph | 335 × 187, displayed uniformly at 3× | 0.98 px | 1.80 px |
| Additional low-front view, geometry frozen and camera fitted | 728 × 488 | 4.70 px | 8.05 px |

The first four views participate in the geometry fit. The last view fits only its camera after the geometry is frozen. These errors measure manually annotated points, not an independently calibrated aircraft reconstruction. Annotation uncertainty is recorded per point. The model uses left/right symmetry; all five apertures have visible corners scored across the views.

Front — [USAF / SSGT Jason Colbert](https://commons.wikimedia.org/wiki/File:F-117_Front_View.jpg):

![Actual USAF frontal photograph beside the grey canopy render](work/canopy-geometry-pass/front-comparison.png)

Side — [previously consulted close-up](https://i.pinimg.com/originals/65/96/7f/65967f0ced77b1053dbfe35d3f66a9fb.jpg), photographer unidentified:

![Actual side photograph beside the grey canopy render](work/canopy-geometry-pass/side-comparison.png)

Front three-quarter — [Burkhard Domke's close-up](https://www.b-domke.de/AviationImages/Nighthawk/3657cu.html), copyright credit retained in the photograph. The crown is outside this crop and is not scored:

![Actual front three-quarter close-up beside the grey canopy render](work/canopy-geometry-pass/quarter-comparison.png)

Original supplied side photograph — original photographer unknown. Both panels use the same uniform magnification:

![User-supplied side photograph beside the corresponding grey canopy render](work/canopy-geometry-pass/user-side-comparison.png)

[Additional low-front pair](work/canopy-geometry-pass/low-front-check-comparison.png), photograph credited to [anthony@maw.bc.ca, 2005](https://imgur.com/gallery/f-117-stealth-plane-front-end-closeup-Rxp5d).

## Named landmarks and edges

| Landmark | Evidence and remaining discrepancy |
|---|---|
| Crown | Front reference `(483,40)` → render `(480.17,41.58)`, 3.24 px error. Side crown error is 5.24 px; supplied side error is 0.77 px. Roof silhouette constraints prevent the rear roof from projecting above the photographed crown/profile. |
| Window corners | The windscreen's front upper corners miss by 1.29–2.40 px. The starboard quarter apex misses by 1.76 px in front. The port quarter apex misses by 1.93 px in the close-up. Side-window corners in the main side view miss by 1.84–10.04 px. The shadowed port quarter apex in the front photograph is unscored. |
| Lower glass edges | The front windshield lower edge measures 201.01 px in the photograph and 209.49 px in the render, about 4.2% wider. Its tilt differs by about 0.87°. The main side-window lower edge tilt differs by about 1.05°. These are specific remaining shape discrepancies. |
| Lower windshield panel / canopy sill | The front gap from the glass's lower-edge midpoint to the fore-sill midpoint is 120.0 px in the photo and 115.0 px in the render. The side sill shoulder is 14.45 px too far forward/above the marked corner. The close-up fore-sill corner misses by 14.94 px. Dogtooth joints are currently represented by mean straight edges, so this area still needs refinement. |
| Rear edge | The main side view's rear sill corner misses by 2.50 px. Side-window aft upper/lower corners miss by 2.54/1.84 px. The shell ends at the canopy rear plane; the aircraft fuselage continuing behind it is outside this model. |

Numbered markers, window-edge overlays and full landmark names are included in the [front](work/canopy-geometry-pass/front-landmarks.png), [side](work/canopy-geometry-pass/side-landmarks.png), [three-quarter](work/canopy-geometry-pass/quarter-landmarks.png) and [supplied-side](work/canopy-geometry-pass/user-side-landmarks.png) sheets. [All coordinates, uncertainties and residuals](work/canopy-geometry-pass/landmark-report.md) are also available as [CSV](work/canopy-geometry-pass/landmarks.csv).

The aerial photograph supplied in the conversation has very small, dark canopy details. The open-canopy model photograph has a different hinge pose, and the painting guide supplies projected artwork. They inform the pane topology but are not treated as calibrated closed-canopy camera measurements. Photographs remain in ignored local evidence directories, with no game artwork added to the repository.

## Pilot view: canopy, dashboard and HUD isolated

All four captures use the same frozen carrier world, centred eye, 90° square field of view and 1024 × 1024 target. Instrument textures stay hidden. The new canopy is combined with the existing dashboard/interior and HUD dimensions. The four stages are:

1. Canopy alone.
2. Canopy plus plain dashboard/interior; entire HUD assembly absent.
3. HUD hardware/glass added; symbols hidden.
4. HUD hardware/glass and symbols visible.

![Four pilot captures separating the canopy, dashboard and HUD](work/canopy-geometry-pass/pilot-layers.png)

![Direct pilot comparison with the HUD absent and visible](work/canopy-geometry-pass/pilot-hud-toggle.png)

The roof and converging front framing obstruct the view with the HUD absent. Adding the plain dashboard raises the lower obstruction. The HUD introduces its rim/support arms, while much of its projected image lies against opaque roof/frame rather than scenery through the windscreen. Fitting the existing seat/eye position and HUD aperture to this canopy remains necessary.

Opaque pixel ID masks measure the frontmost surface in a **±35° horizontal, ±25° vertical** central region. Transparent glass and HUD light are excluded from opaque obstruction counts:

| Capture | Canopy | Dashboard / lower interior | HUD hardware | Open view |
|---|---:|---:|---:|---:|
| HUD absent | 49.84% | 32.21% | 0.00% | 17.94% |
| HUD visible | 49.53% | 29.09% | 4.19% | 17.19% |

The HUD removes **0.75 percentage points** of previously open view; much of its hardware overlaps surfaces that already block scenery. These values describe this candidate at the chosen eye position, not the real aircraft's visibility. Absolute scale and the eye-to-canopy relationship have not been established from measurements.

[Coloured obstruction ID images](work/canopy-geometry-pass/pilot-obstructions.png) show the canopy in red, dashboard/lower interior in green, HUD hardware in blue and open view in black.

## Build and rendering checks

- VR and desktop builds pass without new compiler warnings.
- Ten focused checks pass in each build, including composition, interpolation rules, viewport geometry and VR maths: **20/20**.
- Mesh checks pass for five nondegenerate planar apertures and 33 named landmarks. The candidate has 46 opaque and eight glass triangles, plus a separate four-triangle underside depth mask for exterior inspection. Existing flight-cockpit enclosure, HUD optics and 4,590 instrument sightline checks also pass; those flight checks apply to the existing cockpit, not the candidate's visibility.
- D3D11 captures export this candidate's actual C mesh. The comparison tool rejects stale exports that differ from the stored fitted points.
- A fresh authoring fit reproduced every stored point exactly after the roof-envelope observations were moved into the annotation data.
- Existing flight-render validation preserves 223,776 coloured instrument pixels and 2,806/2,806 checked bright HUD pixels, with the carrier deck coverage checks passing. The production stereo preview measured 3.635 ms/frame across 30 measured frames; this is not headset delivery timing or a performance claim for the staged candidate.
- Native pilot captures and raw geometry are in `work/vr-benchmark/d74fd03be1/`. Review sheets and point tables are in `work/canopy-geometry-pass/`.
- SteamVR was not started. `vrmonitor`, `vrserver` and `vrcompositor` were verified absent after testing. Original game assets and pilot files were not written.

## Reproduce

The stored fit in `tools/canopy_fit.json` and generated C header allow normal game builds without SciPy. Local comparison rendering needs NumPy, Pillow and the reference files named in `tools/canopy_landmarks.json` under `work/canopy-reference/`.

```powershell
python tools/benchmark_vr.py --exe f117port/build-vr/f117-enhanced-vr.exe --game-dir ../F117-Enhanced-play/Game --validate --hud-check
python tools/render_canopy_comparison.py --fit tools/canopy_fit.json --capture work/vr-benchmark/<capture-id> --references work/canopy-reference --out work/canopy-geometry-pass
```

For geometry authoring, install SciPy into a development venv and run:

```powershell
python tools/fit_vr_canopy.py --out work/canopy-fit/new --header f117port/src/enhanced/vr_canopy_points.h --manifest tools/canopy_fit.json
```

Rebuild and recapture after changing geometry. A game-free compiled export for exterior iteration can also be built from `tools/export_vr_canopy.c` and `f117port/src/enhanced/vr_canopy.c` with the include path `f117port/src` and maths library.
