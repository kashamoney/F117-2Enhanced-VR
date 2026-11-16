#include "enhanced/vr_gpu.h"
#include "enhanced/hr.h"
#include "enhanced/enhanced.h"
#include "enhanced/vr.h"
#include "enhanced/vr_presentation.h"
#include "enhanced/vr_visibility.h"
#include "render3d/r3d.h"
#include "mem.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double x,y,z; } P3;
typedef struct { double x,y; } P2;
static P3 points[256];
static unsigned links[256][2], face[64], nf;
static VrGpuFrame *current;
/* Reciprocal physical depth and band fraction are affine in tangent rays. */
static double depth_plane[3],band_plane[3];
static double band_gradient[2][2];
static unsigned surface;
static double object_centre[3],object_scale=1;
static void reset_face(void) { memset(depth_plane,0,sizeof depth_plane);memset(band_plane,0,sizeof band_plane);surface=0; }
bool vr_gpu_active(void) { return current != NULL; }
void vr_gpu_begin(VrGpuFrame *f) { current=f;f->count=f->batch_count=f->background_count=0;f->polygons=f->invalid=f->spills=f->water_faces=f->surface_faces=0;f->fine=enh.dither_fine;nf=0;memset(&f->presentation,0,sizeof f->presentation);reset_face();
    f->aircraft_triangles=f->aircraft_dots=f->water_speckles=0;object_scale=1;
    for(int i=0;i<8;i++)for(int j=0;j<2;j++){f->ship_min[i][j]=1e30;f->ship_max[i][j]=-1e30;}
}
void vr_gpu_end(void) { current=NULL;nf=0; }
void vr_gpu_unit_dot(void) { if(current)current->aircraft_dots++; }
void vr_gpu_palette(const uint32_t *p) {
    vr_visibility_finish();
    memcpy(current->palette,p,sizeof current->palette);current->spills=vr_presentation_spill_count();
    /* Use the original rendered vessel's bounds, including exaggerated game
     * proportions. Real-world guessed dimensions can put wash inside the hull. */
    VrGpuPresentation *v=&current->presentation;
    for(int i=0;i<(int)v->params[1];i++)if(current->ship_min[i][0]<1e29) {
        double dx=(current->ship_max[i][0]-current->ship_min[i][0])*.5,dz=(current->ship_max[i][1]-current->ship_min[i][1])*.5;
        v->ships[i][0]=(float)((current->ship_max[i][0]+current->ship_min[i][0])*.5);
        v->ships[i][1]=(float)((current->ship_max[i][1]+current->ship_min[i][1])*.5);
        v->ships[i][2]=dx>dz?1:0;v->ships[i][3]=dx>dz?0:1;
        v->shipshape[i][0]=(float)fmax(3,fmax(dx,dz));v->shipshape[i][1]=(float)fmax(2,fmin(dx,dz));v->shipshape[i][2]=1;
    }
}
void vr_gpu_scene(int heading,int pitch,int roll,const double position[3],double time) {
    VrGpuPresentation *p=&current->presentation;
    /* Original Q14 camera products are stored unshifted in 16.16 vertices.
     * Match that geometry's physical scale, including its vertical compression. */
    double m[9],rad=6.2831853071795864769/65536,unit=.5*(512000.0/1048576)*vr_options.world_scale;
    vr_aircraft_matrix((int16_t)heading*rad,(int16_t)pitch*rad,(int16_t)roll*rad,m);
    for(int r=0;r<3;r++)for(int c=0;c<3;c++)p->view[r][c]=(float)m[3*r+c];
    p->view[0][3]=(float)(vr_presentation_far(DSS(0x48B6))*unit);
    /* Model +Z is north: terrain submits -Y to r3d_draw_object. */
    double cx=(int32_t)DSL(0xBA10)*unit,cz=-(int32_t)DSL(0xC02A)*unit;
    /* Head displacement is supplied in world metres by vr_eye_begin. */
    cx+=position[0]*unit;cz+=position[2]*unit;
    p->origin[0]=(float)(cx-floor(cx/4096)*4096);p->origin[1]=(float)(cz-floor(cz/4096)*4096);
    p->origin[2]=(float)fmod(time,4096);p->params[0]=(float)vr_presentation_effects();
    p->params[2]=(float)vr_presentation_factor();
    /* One world sea plane for every LOD and head direction. Deriving it from
     * whichever rounded distant face was visible makes ripples swim on turns. */
    p->sea[0]=(float)m[3];p->sea[1]=(float)m[4];p->sea[2]=(float)m[5];
    p->sea[3]=(float)((DSS(0x2DF4)+VR_PILOT_EYE_UNITS)*unit+position[1]*unit);
    /* Ships are original scenery sites, not aircraft-unit type IDs. They are
     * stationary in this game: use waterline wash, never a fictitious long wake. */
    double distances[8];for(int k=0;k<8;k++)distances[k]=1e30;
    /* Site cells use the map's 0x100000 north datum; aircraft-unit y32 has
     * a different wrapping convention (r3d_draw_model), which must not be used here. */
    uint32_t north=0x100000-DSL(0xC02A),east=DSL(0xBA10);
    unsigned home=0xB2A4+16*DSW(0xE2DC);
    uint32_t debug[8]={east,DSL(0xC02A),DSW(0xC0A4),DSW(0xC0B2),DSW(home),DSW(home+2),DSW(home+12),DSW(0xE540)};
    memcpy(current->source_camera,debug,sizeof debug);
    unsigned sites=DSW(0xC0B4);
    if(DSW(0xE540)>sites)sites=DSW(0xE540);
    if(DSW(0xE2DC)>=sites)sites=DSW(0xE2DC)+1;
    for(unsigned i=1;i<sites&&i<256;i++) {
        unsigned s=0xB2A4+16*i,t=DSB(s+12)&127,cls=DSB(0xC604+t)&15;
        bool carrier=(DSB(s+7)&2)!=0;
        if((cls!=12&&cls!=13&&!carrier)||(DSB(s+6)&0x80))continue;
        double x=((int16_t)(DSW(s)-(east>>5))*32.0-(east&31)-position[0])*unit;
        double z=(-(int16_t)((north>>5)-DSW(s+2))*32.0-(north&31)-position[2])*unit,d=x*x+z*z;
        if(d>7500.0*7500.0)continue;
        int k=7;if(d>=distances[k])continue;
        while(k>0&&d<distances[k-1]){distances[k]=distances[k-1];memcpy(p->ships[k],p->ships[k-1],sizeof p->ships[k]);memcpy(p->shipshape[k],p->shipshape[k-1],sizeof p->shipshape[k]);k--;}
        distances[k]=d;
        p->ships[k][0]=(float)x;p->ships[k][1]=(float)z;
        p->ships[k][2]=0;p->ships[k][3]=DSS(0xB16E)<0?1:-1;
        p->shipshape[k][0]=carrier?105:48;p->shipshape[k][1]=carrier?22:9;
        p->shipshape[k][2]=0;p->shipshape[k][3]=(float)((r3d_scenery_id(DSW(s+12))&0x7FFF)|(carrier?0x10000:0));
    }
    for(int k=0;k<8;k++)if(distances[k]<1e30)p->params[1]++;
}
void vr_gpu_free(VrGpuFrame *f) { free(f->vertices);free(f->batches);memset(f,0,sizeof *f); }
static void *grow(void *p,size_t bytes) {
    void *q=realloc(p,bytes);if(!q){fputs("VR geometry allocation failed\n",stderr);exit(3);}return q;
}
static void triangle(P2 a,P2 b,P2 c,uint16_t colour) {
    unsigned ah=colour>>8,op=ah==0xFE?1:ah==0xFD?2:0;
    if(ah==0xFB)return;
    VrGpuFrame *f=current;
    if(depth_plane[0]!=0||depth_plane[1]!=0||depth_plane[2]!=0) {
        if(vr_visibility_actor_active())f->aircraft_triangles++;
        vr_visibility_triangle(fabs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*.5);
    }
    if(f->count+3>f->capacity) {f->capacity=(f->count+3)*2+4096;f->vertices=grow(f->vertices,f->capacity*sizeof *f->vertices);}
    if(!f->batch_count || f->batches[f->batch_count-1].op!=op) {
        if(f->batch_count==f->batch_capacity){f->batch_capacity=f->batch_count*2+64;f->batches=grow(f->batches,f->batch_capacity*sizeof *f->batches);}
        f->batches[f->batch_count++]=(VrGpuBatch){(unsigned)f->count,0,op};
    }
    P2 p[3]={a,b,c};
    for(int i=0;i<3;i++) f->vertices[f->count++]=(VrGpuVertex){(float)p[i].x,(float)p[i].y,colour,
        (float)fmax(0,depth_plane[0]*p[i].x+depth_plane[1]*p[i].y+depth_plane[2]),
        (float)(band_plane[0]*p[i].x+band_plane[1]*p[i].y+band_plane[2]),surface};
    f->batches[f->batch_count-1].count+=3;
}
static double cross(P2 a,P2 b,P2 c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
/* Ear clipping retains concave original faces; no fan through their exterior. */
static void polygon(P2 *p,int n,uint16_t colour) {
    if(n<3)return;
    double area=0;for(int i=0;i<n;i++)area+=p[i].x*p[(i+1)%n].y-p[i].y*p[(i+1)%n].x;
    if(fabs(area)<1e-15)return;
    double sign=area>0?1:-1;
    int ids[80];for(int i=0;i<n;i++)ids[i]=i;
    current->polygons++;
    while(n>3) {
        bool cut=false;
        for(int i=0;i<n;i++) {
            int i0=ids[(i+n-1)%n],i1=ids[i],i2=ids[(i+1)%n];
            P2 a=p[i0],b=p[i1],c=p[i2];
            double turn=cross(a,b,c)*sign;
            if(turn<-1e-14)continue;
            bool inside=false;
            if(turn>1e-14)for(int j=0;j<n;j++) {
                int k=ids[j];if(k==i0||k==i1||k==i2)continue;
                if(cross(a,b,p[k])*sign>1e-14 && cross(b,c,p[k])*sign>1e-14 && cross(c,a,p[k])*sign>1e-14){inside=true;break;}
            }
            if(inside)continue;
            if(turn>1e-14)triangle(a,b,c,colour);
            memmove(ids+i,ids+i+1,(size_t)(n-i-1)*sizeof *ids);n--;cut=true;break;
        }
        if(!cut){current->invalid++;return;}
    }
    triangle(p[ids[0]],p[ids[1]],p[ids[2]],colour);
}
void vr_gpu_object(void) {
    object_scale=1;
    if(!vr_gpu_active()||!vr_visibility_actor_active()||!vr_options.aircraft_visibility)return;
    for(int k=0;k<3;k++)object_centre[k]=DSSL(0x7CA2+4*k)/65536.0+vr_camera_residual(k);
    /* A gradual 1x..2x boost over 800..3500 physical metres. Model-file radii
     * vary too much between LODs to use them as a visible-silhouette minimum. */
    /* Match the physical camera space used for atmospheric depth. Range, not
     * forward depth, keeps the size steady as the pilot turns their head. */
    double x=object_centre[0],y=object_centre[1]/.75,z=object_centre[2];
    double distance=sqrt(x*x+y*y+z*z)*(.03/vr_near_depth());
    double t=fmin(1,fmax(0,(distance-800)/2700));
    object_scale=1+t*t*(3-2*t);
}
void vr_gpu_vertex(unsigned k,double x,double y,double z) {
    x+=vr_camera_residual(0);y+=vr_camera_residual(1);z+=vr_camera_residual(2);
    if(vr_visibility_actor_active()&&object_scale>1) {
        x=object_centre[0]+(x-object_centre[0])*object_scale;
        y=object_centre[1]+(y-object_centre[1])*object_scale;
        z=object_centre[2]+(z-object_centre[2])*object_scale;
    }
    if(k<256)points[k]=(P3){x,y,z};
    VrGpuPresentation *p=&current->presentation;
    double scale=.03/vr_near_depth(),cam[3]={x*scale,-y*scale/.75,-z*scale},world[2]={0,0};
    for(int c=0;c<3;c++){world[0]+=p->view[0][c]*cam[c];world[1]+=p->view[2][c]*cam[c];}
    int nearest=-1;double best=1500.0*1500;
    double height=p->sea[3];for(int c=0;c<3;c++)height+=p->view[1][c]*cam[c];
    for(int i=0;i<(int)p->params[1];i++)if((DSW(0x4956)&0x7FFF)==((unsigned)p->shipshape[i][3]&0xFFFF) ||
        (((unsigned)p->shipshape[i][3]&0x10000)&&DSB(0x4957)>0&&height>8&&height<50)) {
        double dx=world[0]-p->ships[i][0],dz=world[1]-p->ships[i][1],d=dx*dx+dz*dz;
        if(d<best){best=d;nearest=i;}
    }
    if(nearest>=0)for(int c=0;c<2;c++) {
        current->ship_min[nearest][c]=fmin(current->ship_min[nearest][c],world[c]);
        current->ship_max[nearest][c]=fmax(current->ship_max[nearest][c],world[c]);
    }
}
void vr_gpu_edge(unsigned rec,unsigned a,unsigned b) {
    unsigned k=(uint16_t)(rec-R3D_EDGES)/0x18;
    if(k<256){links[k][0]=a/8;links[k][1]=b/8;}
}
void vr_gpu_add_edge(unsigned rec) {
    if(rec==R3D_CLOSE)return;
    unsigned k=(uint16_t)(rec-R3D_EDGES)/0x18;
    if(k<256 && nf<64)face[nf++]=k;
}
void vr_gpu_finish(uint16_t colour) {
    reset_face();
    if(vr_visibility_actor_active())surface|=VR_SURFACE_AIRCRAFT;
    unsigned count=nf;nf=0;if(count<3)return;
    unsigned ring[65],used[64]={0};
    ring[0]=links[face[0]][0];ring[1]=links[face[0]][1];used[0]=1;
    for(unsigned i=1;i<count;i++) {
        bool found=false;
        for(unsigned j=1;j<count;j++)if(!used[j]) {
            unsigned a=links[face[j]][0],b=links[face[j]][1];
            if(a==ring[i]||b==ring[i]){ring[i+1]=a==ring[i]?b:a;used[j]=1;found=true;break;}
        }
        if(!found){current->invalid++;return;}
    }
    if(ring[count]!=ring[0]){current->invalid++;return;}
    /* Near clipping before perspective divide; unlike the integer 1x clipper,
     * these intersections retain their fractional camera coordinates. */
    P3 out[80];int n=0;const double near=vr_near_depth();
    for(unsigned i=0;i<count;i++) {
        P3 a=points[ring[i]],b=points[ring[(i+1)%count]];
        bool ia=a.z>=near,ib=b.z>=near;
        if(ia)out[n++]=a;
        if(ia!=ib){double t=(near-a.z)/(b.z-a.z);out[n++]=(P3){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),near};}
    }
    P2 projected[80];
    for(int i=0;i<n;i++)projected[i]=(P2){out[i].x/out[i].z,-out[i].y/(0.75*out[i].z)};
    if(hr_light_element())surface|=VR_SURFACE_LIGHT;
    if(n>=3) {
        /* Plane in OpenXR camera axes: +x right, +y up, forward -z. */
        double scale=.03/near;
        band_plane[2]=(8+log2((512000.0/1048576)*2*vr_options.world_scale/scale))/15;
        P3 a={out[0].x*scale,-out[0].y*scale/.75,-out[0].z*scale},normal={0,0,0};
        for(int i=1;i<n-1;i++) {
            P3 b={(out[i].x-out[0].x)*scale,-(out[i].y-out[0].y)*scale/.75,-(out[i].z-out[0].z)*scale};
            P3 c={(out[i+1].x-out[0].x)*scale,-(out[i+1].y-out[0].y)*scale/.75,-(out[i+1].z-out[0].z)*scale};
            normal=(P3){b.y*c.z-b.z*c.y,b.z*c.x-b.x*c.z,b.x*c.y-b.y*c.x};
            if(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z>1e-12)break;
        }
        double d=normal.x*a.x+normal.y*a.y+normal.z*a.z;
        if(fabs(d)>1e-12){depth_plane[0]=normal.x/d;depth_plane[1]=normal.y/d;depth_plane[2]=-normal.z/d;}
        double len=sqrt(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
        const VrGpuPresentation *p=&current->presentation;
        double up=normal.x*p->view[1][0]+normal.y*p->view[1][1]+normal.z*p->view[1][2];
        if(len>0&&fabs(up)>len*.985) {surface|=VR_SURFACE_GROUND;current->surface_faces++;}
        if((colour>>8)==0xFD && (colour&255)==0x80) {
            surface|=VR_SURFACE_WATER|VR_SURFACE_GROUND;current->water_faces++;
        }
    }
    polygon(projected,n,colour);
    reset_face();
}
static P2 screen(double x,double y) {
    return (P2){(x-enh_hr.cx)/(256.0*enh_hr.n),-(y-enh_hr.cy)/(192.0*enh_hr.n)};
}
void vr_gpu_poly2d(int n,const int64_t *p,uint8_t colour) {
    reset_face();
    if(n>64)return;
    P2 q[64];for(int i=0;i<n;i++)q[i]=screen(p[2*i]+enh_hr.ox,p[2*i+1]+enh_hr.oy);
    surface=colour>=0x80&&colour<=0x8F?VR_SURFACE_SKY:VR_SURFACE_GROUND;
    if(n==4) {
        double ax=(q[0].x+q[1].x)*.5,ay=(q[0].y+q[1].y)*.5;
        double dx=(q[2].x+q[3].x)*.5-ax,dy=(q[2].y+q[3].y)*.5-ay,d=dx*dx+dy*dy;
        if(d>1e-15) {
            int g=(surface&VR_SURFACE_SKY)!=0;
            band_plane[0]=dx/d;band_plane[1]=dy/d;
            if((colour&15)==15) {band_plane[0]=band_gradient[g][0];band_plane[1]=band_gradient[g][1];}
            else {band_gradient[g][0]=band_plane[0];band_gradient[g][1]=band_plane[1];}
            band_plane[2]=-band_plane[0]*ax-band_plane[1]*ay;
        }
    }
    polygon(q,n,(uint16_t)(0xFF00|colour));
    reset_face();
    current->background_count=current->count;
}
void vr_gpu_rect(double x0,double y0,double x1,double y1,uint8_t colour) {
    if(vr_visibility_actor_active())surface|=VR_SURFACE_AIRCRAFT;
    if(colour==0xF||colour==0xE||colour==0x1F)surface|=VR_SURFACE_LIGHT;
    P2 q[4]={screen(x0,y0),screen(x1,y0),screen(x1,y1),screen(x0,y1)};
    triangle(q[0],q[1],q[2],0xFF00|colour);triangle(q[0],q[2],q[3],0xFF00|colour);
    reset_face();
}
void vr_gpu_line2d(double x0,double y0,double x1,double y1,uint8_t colour) {
    double dx=x1-x0,dy=y1-y0,len=hypot(dx,dy),r=enh_hr.n*0.17;
    if((surface&VR_SURFACE_LIGHT)&&(vr_presentation_effects()&VR_LANDING))r*=1.25;
    if(len<0.001){vr_gpu_rect(x0-r,y0-r,x0+r,y0+r,colour);return;}
    dx=dx/len*r;dy=dy/len*r;
    P2 p[4]={screen(x0-dy,y0+dx),screen(x1-dy,y1+dx),screen(x1+dy,y1-dx),screen(x0+dy,y0-dx)};
    polygon(p,4,0xFF00|colour);
    reset_face();
}
void vr_gpu_line(unsigned rec,uint16_t colour) {
    reset_face();
    unsigned k=(uint16_t)(rec-R3D_EDGES)/0x18;if(k>=256)return;
    P3 a=points[links[k][0]],b=points[links[k][1]];
    const double near=vr_near_depth();
    if(a.z<near&&b.z<near)return;
    /* Original sea speckles have seven vertices, no planes, and seven self
     * edges. Recognize the shared pattern rather than theatre-specific IDs. */
    if(vr_options.presentation&&!vr_options.legacy_water_points&&DSB(0x4957)==1&&DSS(0x7D2C)==7&&DSS(0x7D2A)==0) {
        bool dots=true;for(int i=0;i<7;i++)if(links[i][0]!=links[i][1])dots=false;
        if(dots){current->water_speckles++;return;}
    }
    if(a.z<near){double t=(near-a.z)/(b.z-a.z);a=(P3){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),near};}
    if(b.z<near){double t=(near-b.z)/(a.z-b.z);b=(P3){b.x+t*(a.x-b.x),b.y+t*(a.y-b.y),near};}
    if(vr_visibility_actor_active())surface|=VR_SURFACE_AIRCRAFT;
    if(hr_light_element()||((colour&255)>=0x90&&(colour&255)<=0x97))surface|=VR_SURFACE_LIGHT;
    depth_plane[2]=2/(a.z+b.z)/(.03/near);
    band_plane[2]=(8+log2((512000.0/1048576)*2*vr_options.world_scale/(.03/near)))/15;
    vr_gpu_line2d(enh_hr.cx+256*enh_hr.n*a.x/a.z,enh_hr.cy+256*enh_hr.n*a.y/a.z,
                  enh_hr.cx+256*enh_hr.n*b.x/b.z,enh_hr.cy+256*enh_hr.n*b.y/b.z,(uint8_t)colour);
}
void vr_gpu_horizon(double x0,double y0,double x1,double y1,uint8_t sky,bool inverted) {
    /* Clip a large screen rectangle by the horizon. Roll flips which half is
     * sky; this also handles a horizon entirely outside the eye view. */
    P2 rect[4]={{0,0},{enh_hr.lw,0},{enh_hr.lw,enh_hr.lh},{0,enh_hr.lh}},p[8];int n=0;
    double dx=x1-x0,dy=y1-y0,sign=inverted?-1:1;
    if(dx<0){dx=-dx;dy=-dy;}
    vr_gpu_rect(0,0,enh_hr.lw,enh_hr.lh,0x78);
    for(int i=0;i<4;i++) {
        P2 a=rect[i],b=rect[(i+1)%4];
        double da=(dx*(a.y-y0)-dy*(a.x-x0))*sign,db=(dx*(b.y-y0)-dy*(b.x-x0))*sign;
        if(da<=0)p[n++]=a;
        if((da<=0)!=(db<=0)){double t=da/(da-db);p[n++]=(P2){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y)};}
    }
    P2 q[8];for(int i=0;i<n;i++)q[i]=screen(p[i].x,p[i].y);
    polygon(q,n,0xFF00|sky);
    current->background_count=current->count;
}
