#pragma once
/* Extended render coordinates and actor continuity. Simulation data is untouched. */
#include <stdbool.h>
#include <stdint.h>
typedef struct { double delta[3]; int64_t key; int actor; } VrVisibilityRecord;
void vr_visibility_reset(void);
void vr_visibility_terrain(int32_t x,int32_t y,int32_t z);
void vr_visibility_terrain_end(void);
bool vr_visibility_model(uint16_t model,int32_t x,int32_t y,int16_t alt,int16_t h,int16_t p,int16_t r,int16_t lod);
bool vr_visibility_place(int x,int y,int z);
void vr_visibility_place_end(void);
int vr_visibility_cull_limit(int original);
bool vr_visibility_frustum(int radius,int *visible);
int vr_visibility_lod_depth(int depth);
double vr_visibility_translation(int axis);
void vr_visibility_note(unsigned rec);
VrVisibilityRecord vr_visibility_record(unsigned rec);
void vr_visibility_set_record(unsigned rec,VrVisibilityRecord value);
void vr_visibility_load(unsigned rec);
void vr_visibility_actor_candidate(int id);
bool vr_visibility_actor_projection(int id,double ray[2]);
bool vr_visibility_actor_dot(int id);
void vr_visibility_actor_begin(int id);
void vr_visibility_actor_end(void);
bool vr_visibility_actor_active(void);
void vr_visibility_triangle(double projected_area);
void vr_visibility_finish(void);
