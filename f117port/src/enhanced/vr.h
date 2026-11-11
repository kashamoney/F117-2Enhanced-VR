#pragma once
/* ENH: optional seated VR. Eye rendering never advances or writes back simulation state. */
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { double x, y, z, w; } VrQuat;
typedef struct { VrQuat orientation; double position[3]; } VrPose;
typedef struct { double left, right, up, down; } VrFov; /* tangent angles, in radians */
typedef struct {
    int mode;                 /* 0 desktop, 1 OpenXR, 2 desktop stereo preview */
    int resolution;           /* software eye image, square */
    double world_scale;       /* physical metres per game metre */
    bool cockpit, diagnostic, selftest, dump;
    char scene_test[1024];
} VrOptions;
extern VrOptions vr_options;
int vr_parse_arg(int argc, char **argv, int i);
bool vr_enabled(void);
bool vr_start(void *window_handle);
void vr_stop(void);
void vr_tick(const uint32_t *desktop, int w, int h);
bool vr_key(int scancode, bool down, bool repeat);
bool vr_wants_quit(void);
int vr_selftest(void);
int vr_diagnostic(void);
void vr_matrix(VrQuat q, double m[9]);
VrQuat vr_quat_from_matrix(const double m[9]);
VrQuat vr_multiply(VrQuat a, VrQuat b);
void vr_aircraft_matrix(double yaw, double pitch, double roll, double m[9]);
void vr_camera(const VrPose *eye, int heading, int pitch, int roll,
               int *out_heading, int *out_pitch, int *out_roll);
/* Called only inside a saved-state eye pass. The theatre spans 512 km / 0x100000 units. */
void vr_eye_begin(const VrPose *eye, int heading, int pitch, int roll);
void vr_eye_end(void);
void vr_object_units(double scale);
void vr_terrain_units(int level);
void vr_queue_units(unsigned record);
void vr_dequeue_units(unsigned record);
void vr_translate_object(void); /* preserve fractional feet in the 16.16 camera translation */
bool vr_eye_active(void);
bool enh_vr_render(const VrPose *eye, uint32_t *out, int w, int h);
bool enh_vr_ready(void);
void enh_vr_frame(void);
void vr_scene_check(void);
/* Destination holds (320*scale)*(200*scale) pixels, preserving enhanced vectors/MFDs. */
void vr_capture_desktop(const uint32_t *src, int w, int h, uint32_t *out, int scale);
#ifdef __cplusplus
}
#endif
