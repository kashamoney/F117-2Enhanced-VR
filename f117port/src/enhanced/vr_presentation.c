#include "enhanced/vr_presentation.h"
#include "enhanced/vr.h"
#include "enhanced/vr_gpu.h"
#include "mem.h"
#include "render3d/r3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned vr_presentation_effects(void) { return vr_options.presentation ? vr_options.effects : 0; }
bool vr_presentation_range(void) { return vr_gpu_active() && vr_options.presentation && vr_options.distance; }
int vr_presentation_far(int original) { return vr_presentation_range() ? (original > 15000 ? 30000 : original*2) : original; }
int vr_presentation_dot(int original) { return vr_presentation_range() ? original*2 : original; }

typedef struct { uint8_t data[22]; double units; int32_t key; unsigned order; } Record;
static Record *records;
static size_t count,capacity;
static unsigned spills;
static unsigned sequence,slot_order[50];
static bool merged;
static void append(unsigned rec) {
    if(count==capacity) {
        capacity=capacity*2+128;
        void *p=realloc(records,capacity*sizeof *records);
        if(!p)abort();
        records=p;
    }
    Record *r=&records[count];
    memcpy(r->data,DSP(rec),sizeof r->data);
    r->units=vr_queued_units(rec);r->key=(int32_t)DSL(rec);r->order=slot_order[(rec-0x49F0)/22];count++;
}
void vr_presentation_queue_reset(void) { count=0;spills=sequence=0;merged=false; }
void vr_presentation_queue_note(unsigned rec) { if(vr_presentation_range())slot_order[(rec-0x49F0)/22]=sequence++; }
void vr_presentation_spill(unsigned rec) { if(vr_presentation_range()){append(rec);spills++;} }
unsigned vr_presentation_spill_count(void) { return spills; }
static int compare(const void *a,const void *b) {
    const Record *x=a,*y=b;
    if(x->key!=y->key)return x->key>y->key?-1:1;
    /* The original insert puts a new record before existing equal keys. */
    return x->order>y->order?-1:x->order!=y->order;
}
int vr_presentation_queue_count(int original) {
    if(!vr_presentation_range() || !count)return original;
    for(int i=0;i<original;i++)append(DSW(0x4E3C+2*i));
    qsort(records,count,sizeof *records,compare);merged=true;
    return (int)count;
}
unsigned vr_presentation_queue_record(int i,unsigned original) {
    if(!merged)return original;
    /* The unchanged draw loop consumes one scratch record at a time, retaining
     * its player-part ordering locals. No pointer/table extends into DGROUP. */
    memcpy(DSP(0x49F0),records[i].data,22);
    vr_set_queued_units(0x49F0,records[i].units);
    return 0x49F0;
}
static struct { bool set;int32_t x,y;int altitude,heading; } test_camera;
void vr_presentation_test_camera(int32_t x,int32_t y,int altitude,int heading) {
    test_camera.set=true;test_camera.x=x;test_camera.y=y;test_camera.altitude=altitude;test_camera.heading=heading;
}
void vr_presentation_test_scene(void) {
    if(!test_camera.set||!getenv("F117_VR_BENCHMARK")||!getenv("F117_VR_COAST_FIXTURE"))return;
    DSL(0xBA10)=(uint32_t)test_camera.x;DSL(0xC02A)=(uint32_t)test_camera.y;
    DSW(0xC0A4)=(uint16_t)(DSL(0xBA10)>>5);DSW(0xC0B2)=(uint16_t)((0x100000-DSL(0xC02A))>>5);
    DSW(0x2DEE)=(uint16_t)test_camera.heading;DSW(0x2DF0)=DSW(0x2DF2)=0;DSW(0x2DF4)=(uint16_t)test_camera.altitude;
}
/* A real queue overflow must conserve every model and its scale, retaining
 * painter order and stable equal-depth order. No game files are required. */
int vr_presentation_selftest(void) {
    uint8_t *saved=malloc(65536);if(!saved)return 1;
    memcpy(saved,DSP(0),65536);
    VrOptions options=vr_options;vr_options.presentation=vr_options.distance=true;
    VrPose eye={{0,0,0,1},{0,0,0}};
    vr_eye_begin(&eye,0,0,0);
    VrGpuFrame frame={0};vr_gpu_begin(&frame);
    vr_presentation_queue_reset();DSW(0x49C4)=0;
    DSW(0xE2FC)=4;DSW(0x497A)=0;DSW(0x49C6)=0;
    int fail=0;
    for(int i=0;i<83;i++) {
        DSSL(0x4960)=(i%7)*65536;DSW(0x4956)=(uint16_t)i;
        vr_object_units((i%4)+1);r3d_queue_insert();
    }
    int n=vr_presentation_queue_count(DSW(0x49C4));
    if(n!=83||spills!=33)fail++;
    bool seen[83]={0};int32_t previous=INT32_MAX;int last=-1;
    for(int i=0;i<n;i++) {
        unsigned rec=vr_presentation_queue_record(i,0),id=DSW(rec+8);
        int32_t key=DSSL(rec);
        if(id>=83){fail++;continue;}
        if(seen[id]||key>previous||(key==previous&&(int)id>=last)||vr_queued_units(rec)!=(id%4)+1)fail++;
        seen[id]=true;previous=key;last=(int)id;
    }
    for(int i=0;i<83;i++)if(!seen[i])fail++;
    vr_options.distance=false;vr_presentation_queue_reset();
    if(vr_presentation_queue_count(50)!=50||vr_presentation_far(13332)!=13332)fail++;
    vr_gpu_end();vr_gpu_free(&frame);vr_eye_end();vr_options=options;
    memcpy(DSP(0),saved,65536);free(saved);
    if(fail)fprintf(stderr,"VR presentation queue failures: %d\n",fail);
    else puts("vr-presentation OK: 83 records conserved, sorted with original ties and scaled; original range bypass exact");
    return fail;
}
