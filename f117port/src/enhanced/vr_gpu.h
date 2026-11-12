#pragma once
/* VR geometry capture. Camera-space vertices retain the original fractional
 * coordinates; the GPU rasterises them at the runtime's actual eye resolution. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float x, y; uint32_t colour; } VrGpuVertex; /* tangent rays */
typedef struct { unsigned first, count, op; } VrGpuBatch; /* 0 copy, 1 AND, 2 OR */
typedef struct {
    VrGpuVertex *vertices;
    VrGpuBatch *batches;
    size_t count, capacity, batch_count, batch_capacity;
    uint32_t palette[256];
    unsigned polygons, invalid;
    size_t background_count; /* end of sky/ground-band vertices, for capture diagnostics */
    bool fine;
} VrGpuFrame;
bool vr_gpu_active(void);
void vr_gpu_begin(VrGpuFrame *frame);
void vr_gpu_end(void);
void vr_gpu_palette(const uint32_t *palette);
void vr_gpu_free(VrGpuFrame *frame);
void vr_gpu_vertex(unsigned k, double x, double y, double z);
void vr_gpu_edge(unsigned rec, unsigned a, unsigned b);
void vr_gpu_add_edge(unsigned rec);
void vr_gpu_finish(uint16_t colour);
void vr_gpu_line(unsigned rec, uint16_t colour);
void vr_gpu_poly2d(int n, const int64_t *points, uint8_t colour);
void vr_gpu_rect(double x0, double y0, double x1, double y1, uint8_t colour);
void vr_gpu_line2d(double x0, double y0, double x1, double y1, uint8_t colour);
void vr_gpu_horizon(double x0, double y0, double x1, double y1, uint8_t sky, bool inverted);
#ifdef __cplusplus
}
#endif
