#include "enhanced/vr_gpu.h"
#include "enhanced/hr.h"
#include "enhanced/enhanced.h"
#include "enhanced/vr.h"
#include "render3d/r3d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double x,y,z; } P3;
typedef struct { double x,y; } P2;
static P3 points[256];
static unsigned links[256][2], face[64], nf;
static VrGpuFrame *current;
bool vr_gpu_active(void) { return current != NULL; }
void vr_gpu_begin(VrGpuFrame *f) { current=f;f->count=f->batch_count=0;f->polygons=f->invalid=0;f->fine=enh.dither_fine;nf=0; }
void vr_gpu_end(void) { current=NULL;nf=0; }
void vr_gpu_palette(const uint32_t *p) { memcpy(current->palette,p,sizeof current->palette); }
void vr_gpu_free(VrGpuFrame *f) { free(f->vertices);free(f->batches);memset(f,0,sizeof *f); }
static void *grow(void *p,size_t bytes) {
    void *q=realloc(p,bytes);if(!q){fputs("VR geometry allocation failed\n",stderr);exit(3);}return q;
}
static void triangle(P2 a,P2 b,P2 c,uint16_t colour) {
    unsigned ah=colour>>8,op=ah==0xFE?1:ah==0xFD?2:0;
    if(ah==0xFB)return;
    VrGpuFrame *f=current;
    if(f->count+3>f->capacity) {f->capacity=(f->count+3)*2+4096;f->vertices=grow(f->vertices,f->capacity*sizeof *f->vertices);}
    if(!f->batch_count || f->batches[f->batch_count-1].op!=op) {
        if(f->batch_count==f->batch_capacity){f->batch_capacity=f->batch_count*2+64;f->batches=grow(f->batches,f->batch_capacity*sizeof *f->batches);}
        f->batches[f->batch_count++]=(VrGpuBatch){(unsigned)f->count,0,op};
    }
    P2 p[3]={a,b,c};
    for(int i=0;i<3;i++) f->vertices[f->count++]=(VrGpuVertex){(float)p[i].x,(float)p[i].y,colour};
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
void vr_gpu_vertex(unsigned k,double x,double y,double z) { if(k<256)points[k]=(P3){x,y,z}; }
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
    polygon(projected,n,colour);
}
static P2 screen(double x,double y) {
    return (P2){(x-enh_hr.cx)/(256.0*enh_hr.n),-(y-enh_hr.cy)/(192.0*enh_hr.n)};
}
void vr_gpu_poly2d(int n,const int64_t *p,uint8_t colour) {
    if(n>64)return;
    P2 q[64];for(int i=0;i<n;i++)q[i]=screen(p[2*i]+enh_hr.ox,p[2*i+1]+enh_hr.oy);
    polygon(q,n,(uint16_t)(0xFF00|colour));
}
void vr_gpu_rect(double x0,double y0,double x1,double y1,uint8_t colour) {
    P2 q[4]={screen(x0,y0),screen(x1,y0),screen(x1,y1),screen(x0,y1)};
    triangle(q[0],q[1],q[2],0xFF00|colour);triangle(q[0],q[2],q[3],0xFF00|colour);
}
void vr_gpu_line2d(double x0,double y0,double x1,double y1,uint8_t colour) {
    double dx=x1-x0,dy=y1-y0,len=hypot(dx,dy),r=enh_hr.n*0.17;
    if(len<0.001){vr_gpu_rect(x0-r,y0-r,x0+r,y0+r,colour);return;}
    dx=dx/len*r;dy=dy/len*r;
    P2 p[4]={screen(x0-dy,y0+dx),screen(x1-dy,y1+dx),screen(x1+dy,y1-dx),screen(x0+dy,y0-dx)};
    polygon(p,4,0xFF00|colour);
}
void vr_gpu_line(unsigned rec,uint16_t colour) {
    unsigned k=(uint16_t)(rec-R3D_EDGES)/0x18;if(k>=256)return;
    P3 a=points[links[k][0]],b=points[links[k][1]];
    const double near=vr_near_depth();
    if(a.z<near&&b.z<near)return;
    if(a.z<near){double t=(near-a.z)/(b.z-a.z);a=(P3){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),near};}
    if(b.z<near){double t=(near-b.z)/(a.z-b.z);b=(P3){b.x+t*(a.x-b.x),b.y+t*(a.y-b.y),near};}
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
}
