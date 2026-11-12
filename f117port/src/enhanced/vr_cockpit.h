#pragma once
/* Aircraft-fixed geometry in metres relative to the seated, recentered eye.
 * Instruments remain the original live game texture; this contains no game art. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float p[3], colour[4]; } VrCockpitVertex;
typedef struct { float x0,y0,x1,y1,z; } VrCockpitSurface;
extern const VrCockpitSurface vr_cockpit_panel;
typedef struct {
    const VrCockpitVertex *opaque, *glass;
    unsigned opaque_count, glass_count;
} VrCockpitMesh;
const VrCockpitMesh *vr_cockpit_mesh(void);
int vr_cockpit_selftest(void);
#ifdef __cplusplus
}
#endif
