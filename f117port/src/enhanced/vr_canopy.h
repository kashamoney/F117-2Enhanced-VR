#pragma once
#include "enhanced/vr_cockpit.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Photo-landmark inspection candidate, separate from the flight cockpit. */
const VrCockpitMesh *vr_canopy_mesh(void);
/* Seated VR adaptation: cabin dimensions anchored to the eye and live dash. */
const VrCockpitMesh *vr_canopy_pilot_mesh(void);
const VrCockpitLandmark *vr_canopy_pilot_landmarks(unsigned *count);
int vr_canopy_pilot_export(const char *path);
/* Sill closure for exterior inspection only; the airframe normally hides this. */
const VrCockpitMesh *vr_canopy_base_mesh(void);
const VrCockpitLandmark *vr_canopy_landmarks(unsigned *count);
int vr_canopy_export(const char *path);
int vr_canopy_selftest(void);
#ifdef __cplusplus
}
#endif
