#pragma once
/* Aircraft-fixed geometry in metres relative to the seated, recentered eye.
 * Instruments remain the original live game texture; this contains no game art. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float p[3], colour[4]; } VrCockpitVertex;
typedef struct { float x0,y0,x1,y1,z; } VrCockpitSurface;
extern const VrCockpitSurface vr_cockpit_panel;
/* Original 320x200 source rectangles and their matching physical faces. */
typedef struct {
    const char *name;
    int source[4];
    VrCockpitSurface face;
} VrCockpitInstrument;
const VrCockpitInstrument *vr_cockpit_instruments(unsigned *count);
enum { VR_HUD_CORNERS=6 };
const float (*vr_cockpit_hud_aperture(void))[3];
/* Collimated image: UV depends on the ray through the combiner, not its position
 * on the glass. Both eyes therefore see the same aircraft-relative bearing. */
void vr_cockpit_hud_uv(const float point[3],const double eye[3],float uv[2]);
typedef struct {
    const VrCockpitVertex *opaque, *glass;
    unsigned opaque_count, glass_count;
} VrCockpitMesh;
const VrCockpitMesh *vr_cockpit_mesh(void);
int vr_cockpit_selftest(void);
#ifdef __cplusplus
}
#endif
