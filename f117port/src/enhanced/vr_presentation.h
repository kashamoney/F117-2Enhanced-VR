#pragma once
/* Render-only scenery options and overflow for the original 50-record queue. */
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { VR_WATER=1, VR_SHORE=2, VR_WAKES=4, VR_ATMOSPHERE=8, VR_LANDING=16 };
unsigned vr_presentation_effects(void);
bool vr_presentation_range(void);
int vr_presentation_factor(void);
int vr_presentation_cell_radius(void);
int vr_presentation_far(int original);
int vr_presentation_dot(int original);
void vr_presentation_queue_reset(void);
void vr_presentation_spill(unsigned record);
void vr_presentation_queue_note(unsigned record);
int vr_presentation_queue_count(int original);
unsigned vr_presentation_queue_record(int index, unsigned original);
unsigned vr_presentation_spill_count(void);
int vr_presentation_selftest(void);
void vr_presentation_test_scene(void); /* benchmark-only coast camera, inside the saved-state eye pass */
void vr_presentation_test_camera(int32_t x,int32_t y,int altitude,int heading);
void vr_presentation_test_aircraft(int distance);
void vr_presentation_test_motion(int step,int axis);
#ifdef __cplusplus
}
#endif
