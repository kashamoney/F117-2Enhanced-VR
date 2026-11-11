/* ENH: VR settings, physical camera transforms and fractional stereo translation. */
#include "enhanced/vr.h"
#include "enhanced/enhanced.h"
#include "enhanced/enh_canvas.h"
#include "mem.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#define VR_PI 3.14159265358979323846
#define VR_METRES_PER_UNIT (512000.0 / 1048576.0)
VrOptions vr_options = { .resolution = 4096, .world_scale = 1.0, .cockpit = true };
static bool eye_active;
static double eye_delta[3], object_units = 1.0, queued_units[50];

int vr_parse_arg(int argc, char **argv, int i)
{
    const char *a = argv[i];
    if (!strcmp(a, "--vr")) { vr_options.mode = 1; return 1; }
    if (!strcmp(a, "--vr-preview")) { vr_options.mode = 2; return 1; }
    if (!strcmp(a, "--vr-diagnostic")) { vr_options.diagnostic = true; return 1; }
    if (!strcmp(a, "--vr-selftest")) { vr_options.selftest = true; return 1; }
    if (!strcmp(a, "--vr-no-cockpit")) { vr_options.cockpit = false; return 1; }
    if (!strcmp(a, "--vr-dump")) { vr_options.dump = true; return 1; }
    if (!strcmp(a, "--vr-scene-test") && i + 1 < argc) {
        if (strlen(argv[i+1]) >= sizeof vr_options.scene_test) { fputs("VR test path too long\n", stderr); exit(2); }
        strcpy(vr_options.scene_test, argv[i+1]); vr_options.mode=3; return 2;
    }
    if ((!strcmp(a, "--vr-resolution") || !strcmp(a, "--vr-world-scale")) && i + 1 < argc) {
        char *end;
        double n = strtod(argv[i + 1], &end);
        bool res = !strcmp(a, "--vr-resolution");
        if (end == argv[i+1] || *end || !isfinite(n) || n < (res ? 640 : 0.1) || n > (res ? 4096 : 10) || (res && n != floor(n))) {
            fprintf(stderr, "%s: expected %s\n", a, res ? "integer 640..4096" : "number 0.1..10");
            exit(2);
        }
        if (res) vr_options.resolution = (int)n; else vr_options.world_scale = n;
        return 2;
    }
    return 0;
}
bool vr_enabled(void) { return vr_options.mode && enh_canvas_on(); }
bool vr_eye_active(void) { return eye_active; }
VrQuat vr_multiply(VrQuat a, VrQuat b)
{
    return (VrQuat){a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                    a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                    a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                    a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
void vr_matrix(VrQuat q, double m[9])
{
    double n = sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if (n < 1e-12) { q = (VrQuat){0,0,0,1}; n = 1; }
    double x=q.x/n, y=q.y/n, z=q.z/n, w=q.w/n;
    const double r[9] = {1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),
                         2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),
                         2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
    memcpy(m,r,sizeof r);
}
VrQuat vr_quat_from_matrix(const double m[9])
{
    VrQuat q;
    double t=m[0]+m[4]+m[8], s;
    if (t>0) { s=sqrt(t+1)*2; q=(VrQuat){(m[7]-m[5])/s,(m[2]-m[6])/s,(m[3]-m[1])/s,s/4}; }
    else if (m[0]>m[4] && m[0]>m[8]) { s=sqrt(1+m[0]-m[4]-m[8])*2; q=(VrQuat){s/4,(m[1]+m[3])/s,(m[2]+m[6])/s,(m[7]-m[5])/s}; }
    else if (m[4]>m[8]) { s=sqrt(1+m[4]-m[0]-m[8])*2; q=(VrQuat){(m[1]+m[3])/s,s/4,(m[5]+m[7])/s,(m[2]-m[6])/s}; }
    else { s=sqrt(1+m[8]-m[0]-m[4])*2; q=(VrQuat){(m[2]+m[6])/s,(m[5]+m[7])/s,s/4,(m[3]-m[1])/s}; }
    return q;
}
void vr_aircraft_matrix(double a, double p, double r, double m[9])
{
    /* Same rotation as r3d_math.c mat_from_euler(a,-p,r): Ry(-a) Rx(p) Rz(-r). */
    double ca=cos(a),sa=sin(a),cb=cos(-p),sb=sin(-p),cc=cos(r),sc=sin(r);
    const double v[9]={cc*ca-sc*sb*sa,cc*sb*sa+sc*ca,-cb*sa,
                       -sc*cb,cc*cb,sb,sc*sb*ca+cc*sa,sc*sa-cc*sb*ca,cb*ca};
    memcpy(m,v,sizeof v);
}
void vr_camera(const VrPose *eye, int heading, int pitch, int roll, int *oh, int *op, int *orr)
{
    double base[9], head[9], m[9]={0};
    const double rad=2*VR_PI/65536;
    vr_aircraft_matrix((int16_t)heading*rad,(int16_t)pitch*rad,(int16_t)roll*rad,base);
    vr_matrix(eye->orientation,head);
    for(int r=0;r<3;r++) for(int c=0;c<3;c++) for(int k=0;k<3;k++) m[3*r+c]+=base[3*r+k]*head[3*k+c];
    double b=asin(fmax(-1,fmin(1,m[5]))), a,c;
    if(fabs(cos(b))>1e-5) { a=atan2(-m[2],m[8]); c=atan2(-m[3],m[4]); }
    else { a=atan2(m[6],m[0]); c=0; }
    *oh=(int)lround(a/rad); *op=(int)lround(-b/rad); *orr=(int)lround(c/rad);
}
void vr_eye_begin(const VrPose *eye, int heading, int pitch, int roll)
{
    double base[9], rad=2*VR_PI/65536;
    vr_aircraft_matrix((int16_t)heading*rad,(int16_t)pitch*rad,(int16_t)roll*rad,base);
    for(int r=0;r<3;r++) {
        eye_delta[r]=0;
        for(int c=0;c<3;c++) eye_delta[r]+=base[3*r+c]*(eye->position[c]+(c==1?2.0:0.0))/(VR_METRES_PER_UNIT*vr_options.world_scale);
    }
    /* A simple seated eye position, two metres above the aircraft reference point. */
    object_units=1;
    for(int i=0;i<50;i++) queued_units[i]=1;
    eye_active=true;
}
void vr_eye_end(void) { eye_active=false; }
void vr_object_units(double scale) { if(eye_active) object_units=scale; }
void vr_terrain_units(int level) { if(eye_active) object_units=level==0?2:level==1?1:ldexp(1.0,-2*(level-1)); }
void vr_queue_units(unsigned rec) { if(eye_active && rec>=0x49F0 && (rec-0x49F0)/0x16<50) queued_units[(rec-0x49F0)/0x16]=object_units; }
void vr_dequeue_units(unsigned rec) { if(eye_active && rec>=0x49F0 && (rec-0x49F0)/0x16<50) object_units=queued_units[(rec-0x49F0)/0x16]; }
void vr_translate_object(void)
{
    if(!eye_active) return;
    for(int k=0;k<3;k++) {
        double d=0;
        for(int j=0;j<3;j++) d-=eye_delta[j]*object_units*DSS(0x7AF8+6*j+2*k);
        /* Q15 camera matrix times fractional model coordinates -> signed 16.16 vertex translation. */
        DSSL(0x7CA2+4*k)+=(s32)llround(d);
    }
}
void vr_capture_desktop(const uint32_t *src,int w,int h,uint32_t *out,int scale)
{
    const int ow=320*scale,oh=200*scale;
    const bool canvas=enh_canvas_active() && w==enh_cv.lw && h==enh_cv.lh;
    for(int y=0;y<oh;y++) for(int x=0;x<ow;x++) {
        int sx=canvas?enh_cv.fx+x*enh_cv.n/scale:x*w/ow;
        int sy=canvas?enh_cv.fy+y*enh_cv.n/scale:y*h/oh;
        out[y*ow+x]=(sx>=0 && sx<w && sy>=0 && sy<h)?src[sy*w+sx]:0;
    }
    if (canvas && enh_vr_ready()) enh_vr_hud_alpha(out,scale);
}
int vr_selftest(void)
{
    int fail=0;
#define CHECK(c,msg) do { if(!(c)) {fprintf(stderr,"VR FAIL: %s\n",msg);fail++;} } while(0)
    VrPose eye={{0,0,0,1},{0,0,0}};
    int h,p,r;
    for(int a=-20000;a<=20000;a+=5000) {
        vr_camera(&eye,a,1500,-2300,&h,&p,&r);
        CHECK(abs(h-a)<2 && abs(p-1500)<2 && abs(r+2300)<2,"aircraft attitude survives identity head pose");
    }
    eye.orientation=(VrQuat){0,sin(VR_PI/8),0,cos(VR_PI/8)};
    vr_camera(&eye,0,0,0,&h,&p,&r);
    CHECK(abs(h+8192)<2 && abs(p)<2 && abs(r)<2,"turning head left rotates view left");
    eye.orientation=(VrQuat){sin(VR_PI/8),0,0,cos(VR_PI/8)};
    vr_camera(&eye,0,0,0,&h,&p,&r);
    CHECK(abs(p-8192)<2,"looking up pitches camera up");
    double m[9],rt[9];
    vr_aircraft_matrix(2.1,0.7,-0.9,m); vr_matrix(vr_quat_from_matrix(m),rt);
    for(int i=0;i<9;i++) CHECK(fabs(m[i]-rt[i])<1e-10,"quaternion roundtrip");
    eye=(VrPose){{0,0,0,1},{-0.032,0,0}};
    vr_eye_begin(&eye,0,0,0);
    CHECK(fabs(eye_delta[0]+0.032/(VR_METRES_PER_UNIT*vr_options.world_scale))<1e-8,"left eye has physical IPD offset");
    CHECK(fabs(eye_delta[0])<1 && eye_delta[0]!=0,"sub-foot eye displacement is retained");
    vr_eye_end();
    /* High-resolution capture must retain every source pixel rather than reduce to 320x200. */
    for(int scale=1;scale<=4;scale*=2) {
        const size_t pixels=(size_t)(320*scale)*(200*scale);
        u32 *source=malloc(pixels*sizeof(u32)),*captured=malloc(pixels*sizeof(u32));
        CHECK(source && captured,"panel test allocation");
        if(source && captured) {
            for(size_t i=0;i<pixels;i++) source[i]=(u32)(i*0x010203u)&0xFFFFFF;
            vr_capture_desktop(source,320*scale,200*scale,captured,scale);
            CHECK(!memcmp(source,captured,pixels*sizeof(u32)),"panel capture retains every pixel at 1x, 2x and 4x");
        }
        free(source);free(captured);
    }
    if(!fail) puts("vr-selftest OK");
    return fail?1:0;
}
void vr_scene_check(void)
{
    static bool done;
    if (!vr_options.scene_test[0] || done || !enh_vr_ready()) return;
    done=true;
    const int size=vr_options.resolution;
    const size_t bytes=(size_t)size*size*sizeof(u32);
    u32 *a=malloc(bytes),*b=malloc(bytes),*c=malloc(bytes);
    if(!a || !b || !c) { fputs("VR scene test allocation failed\n",stderr); exit(3); }
    VrPose eye={{0,0,0,1},{-0.032,0,0}};
    enh_vr_frame();
    if(!enh_vr_render(&eye,a,size,size) || !enh_vr_render(&eye,b,size,size) || memcmp(a,b,bytes)) {
        fputs("VR scene test: identical poses did not produce identical images\n",stderr); exit(3);
    }
    eye.position[0]=0.032;
    if(!enh_vr_render(&eye,b,size,size)) exit(3);
    eye.orientation=(VrQuat){0,sin(0.25),0,cos(0.25)};
    if(!enh_vr_render(&eye,c,size,size)) exit(3);
    size_t stereo=0,turned=0;
    for(size_t i=0;i<bytes/4;i++) {stereo+=a[i]!=b[i];turned+=b[i]!=c[i];}
    /* An intervening eye and head rotation must not leave stale pixels on reused layers. */
    eye=(VrPose){{0,0,0,1},{-0.032,0,0}};
    u32 *repeat=malloc(bytes);
    if(!repeat || !enh_vr_render(&eye,repeat,size,size) || memcmp(a,repeat,bytes)) {
        fputs("VR scene test: scratch eye layers retained pixels from another pose\n",stderr);exit(3);
    }
    free(repeat);
    for(int i=0;i<3;i++) {
        char path[1200];snprintf(path,sizeof path,"%s/%s.png",vr_options.scene_test,i==0?"left":i==1?"right":"head-left");
        SDL_Surface *s=SDL_CreateSurfaceFrom(size,size,SDL_PIXELFORMAT_XRGB8888,i==0?a:i==1?b:c,size*4);
        if(!s || !SDL_SavePNG(s,path)) {fprintf(stderr,"VR scene dump failed: %s\n",SDL_GetError());exit(3);}
        SDL_DestroySurface(s);
    }
    free(a);free(b);free(c);
    if(!stereo || turned<100) {fprintf(stderr,"VR scene test lacks stereo or head motion: %zu / %zu\n",stereo,turned);exit(3);}
    printf("vr-scene-test OK: stereo=%zu head-turn=%zu; identical-pose repeat exact; state restored\n",stereo,turned);
}
#ifndef F117_OPENXR
bool vr_start(void *w) { (void)w; fprintf(stderr,"VR support was not built. Configure with -DF117_OPENXR=ON.\n");return false; }
void vr_stop(void) {}
void vr_tick(const uint32_t *p,int w,int h) {(void)p;(void)w;(void)h;}
bool vr_key(int sc,bool down,bool rep) {(void)sc;(void)down;(void)rep;return false;}
bool vr_wants_quit(void) {return false;}
int vr_diagnostic(void) {return vr_start(NULL)?0:1;}
#endif
