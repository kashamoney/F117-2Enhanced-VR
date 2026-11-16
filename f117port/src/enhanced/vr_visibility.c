#include "enhanced/vr_visibility.h"
#include "enhanced/vr_presentation.h"
#include "enhanced/vr_gpu.h"
#include "enhanced/vr.h"
#include "enhanced/hr.h"
#include "enhanced/wide.h"
#include "render3d/r3d.h"
#include "mem.h"
#include <math.h>
#include <string.h>

static VrVisibilityRecord current,slots[50];
static int64_t full_c;
static bool prepared,terrain,manual;
static int32_t origin[3],relative[3];
static int actor=-1;
static struct {bool seen,dot;double x,y,area;} actors[64];
static bool active(void) { return vr_gpu_active()&&vr_options.presentation; }
void vr_visibility_reset(void) {
    memset(&current,0,sizeof current);current.actor=-1;
    prepared=terrain=manual=false;actor=-1;memset(actors,0,sizeof actors);
}
void vr_visibility_terrain(int32_t x,int32_t y,int32_t z) { terrain=vr_presentation_range();origin[0]=x;origin[1]=y;origin[2]=z; }
void vr_visibility_terrain_end(void) { terrain=false;vr_visibility_place_end(); }
bool vr_visibility_model(uint16_t model,int32_t x,int32_t y,int16_t alt,int16_t h,int16_t p,int16_t r,int16_t lod) {
    if(!vr_presentation_range())return false;
    /* Site-cell wrapping resolves the original y32 datum before the old s16
     * truncation. The low fractional bits and original model LOD scale remain. */
    uint32_t cx=DSL(0xBA10),north=0x100000-DSL(0xC02A);
    int64_t dx=(int16_t)((uint32_t)x/32-DSW(0xC0A4))*32LL+((uint32_t)x&31)-(cx&31);
    int64_t dn=(int16_t)((uint32_t)y/32-DSW(0xC0B2))*32LL+((uint32_t)y&31)-(north&31);
    int16_t dz=(int16_t)(alt-DSS(0x2DF4));
    int sh=lod-(DSB(0x294B)==0?3:2);double units=ldexp(1.0,sh);
    vr_object_units(units);
    relative[0]=(int32_t)floor(dx*units);relative[1]=-(int32_t)floor(dn*units);
    relative[2]=(int32_t)floor(dz*units)+(alt!=0?1:0);manual=true;
    r3d_set_origin(0,0,0);DSW(0xE2FC)=1;
    r3d_place_object(r3d_scenery_id(model),(int16_t)-h,p,r,0,0,0);
    manual=false;return true;
}
bool vr_visibility_place(int x,int y,int z) {
    memset(&current,0,sizeof current);current.actor=actor;prepared=false;
    if(!vr_presentation_range())return true;
    int32_t a[3];
    if(manual){memcpy(a,relative,sizeof a);manual=false;}
    else if(terrain){a[0]=x-origin[0];a[1]=y-origin[1];a[2]=z-origin[2];}
    else {a[0]=DSS(0x4964);a[1]=DSS(0x4966);a[2]=DSS(0x4968);}
    int64_t v[3]={0,0,0};
    for(int k=0;k<3;k++)v[k]=(int64_t)DSS(0x498E +2*k)*a[0]+(int64_t)DSS(0x498E +6+2*k)*a[2]+(int64_t)DSS(0x498E +12+2*k)*a[1];
    double A=v[0]/32768.0,B=v[1]/32768.0,C=v[2]/32768.0;
    double far=vr_presentation_far(DSS(0x48B6));unsigned cls=DSW(0x497A)&7;
    if(C>far||C<DSS(0x48C6+2*cls)||fabs(C)+.25*(fabs(A)+fabs(B))>far)return false;
    /* A bounded proxy lets the original fixed-point face/part renderer run.
     * Restore the full translation immediately after its matrix multiply. */
    double length=sqrt((double)a[0]*a[0]+(double)a[1]*a[1]+(double)a[2]*a[2]),div=1;
    while(length/div>18000)div*=2;
    int16_t proxy[3];for(int k=0;k<3;k++)proxy[k]=(int16_t)lround(a[k]/div);
    DSW(0x4964)=(uint16_t)proxy[0];DSW(0x4966)=(uint16_t)proxy[1];DSW(0x4968)=(uint16_t)proxy[2];
    current.delta[0]=a[0]-proxy[0];current.delta[1]=a[2]-proxy[2];current.delta[2]=-(a[1]-proxy[1]);
    full_c=v[2]*2;prepared=true;return true;
}
void vr_visibility_place_end(void) { memset(&current,0,sizeof current);current.actor=-1;prepared=false; }
int vr_visibility_cull_limit(int original) { return prepared&&vr_presentation_range()?32760:original; }
int vr_visibility_lod_depth(int depth) { return vr_visibility_actor_active()?depth/vr_presentation_factor():depth; }
bool vr_visibility_frustum(int radius,int *visible) {
    if(!vr_presentation_range())return false;
    /* The legacy test caps z at about 8191 and doubles it in an s16. GPU
     * projection has no such limit; retain the same near/side tests in s32. */
    int32_t t=2*(int32_t)DSS(0x7CAC)+radius;
    if(t<0){*visible=0;return true;}
    uint32_t tx=(uint32_t)t,ty=tx;if(enh_wide_on)enh_wide_frustum(&tx,&ty);
    int64_t x=DSSL(0x7CA2),y=DSSL(0x7CA6);
    *visible=((uint64_t)(x<0?-x:x)>>16)<=tx&&((uint64_t)(y<0?-y:y)>>16)<=ty;
    return true;
}
double vr_visibility_translation(int axis) { return vr_presentation_range()?current.delta[axis]:0; }
void vr_visibility_note(unsigned rec) {
    unsigned k=(rec-0x49F0)/22;VrVisibilityRecord value=current;
    int shift=(4-DSB(0xE2FC))*2;if(shift<0)shift=0;if(shift>31)shift=31;
    /* Terrain levels and aircraft use different coordinate scales. Compare
     * physical depth, so a distant terrain cell cannot paint over a nearer
     * aircraft simply because its model coordinates are twice as large. */
    value.key=prepared?(int64_t)llround(full_c/(64.0*vr_queued_units(rec))):((int64_t)DSSL(0x4960)>>shift);
    if(DSW(0xE2FC)==2&&DSW(0x497A)==5)value.key+=0x200000;
    value.actor=actor;slots[k]=value;
}
VrVisibilityRecord vr_visibility_record(unsigned rec) { return slots[(rec-0x49F0)/22]; }
void vr_visibility_set_record(unsigned rec,VrVisibilityRecord v) { slots[(rec-0x49F0)/22]=v; }
void vr_visibility_load(unsigned rec) { current=slots[(rec-0x49F0)/22];actor=current.actor;prepared=false; }
void vr_visibility_actor_candidate(int id) {
    if(!active()||id<0||id>=64||!enh_hr_pt.valid)return;
    actors[id].seen=true;actors[id].x=enh_hr_pt.x;actors[id].y=enh_hr_pt.y;
}
void vr_visibility_actor_dot(int id) { if(active()&&id>=0&&id<64){actors[id].dot=true;actor=id;} }
void vr_visibility_actor_begin(int id) { actor=active()&&id>=0&&id<64?id:-1; }
void vr_visibility_actor_end(void) { actor=-1; }
bool vr_visibility_actor_active(void) { return active()&&actor>=0; }
void vr_visibility_triangle(double area) { if(vr_visibility_actor_active())actors[actor].area+=area; }
void vr_visibility_finish(void) {
    actor=-1;if(!active())return;
    /* Coarse aircraft can be almost edge-on: submitted triangles then cover
     * no eye samples. Retain the original point until the silhouette has a
     * minimum projected footprint (about one pixel at a 2K eye resolution). */
    for(int i=0;i<64;i++)if(actors[i].seen&&!actors[i].dot&&actors[i].area<1.0/(2048.0*2048.0)) {
        double r=enh_hr.n*.5;
        actor=i;
        vr_gpu_rect(actors[i].x-r,actors[i].y-r,actors[i].x+r,actors[i].y+r,15);vr_gpu_unit_dot();
    }
    actor=-1;
}
