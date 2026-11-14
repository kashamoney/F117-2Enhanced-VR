#include "enhanced/vr_canopy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "enhanced/vr_canopy_points.h"

typedef struct {float x,y,z;} P;
enum {CAP=4096};
static VrCockpitVertex opaque[CAP],glass[CAP];
static VrCockpitMesh mesh={opaque,glass,0,0};
static VrCockpitMesh base;
static int built;
static P p(int i){const float *v=canopy_landmarks[i].p;return(P){v[0],v[1],v[2]};}
static P sub(P a,P b){return(P){a.x-b.x,a.y-b.y,a.z-b.z};}
static P mul(P a,float s){return(P){a.x*s,a.y*s,a.z*s};}
static P cross(P a,P b){return(P){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float dot(P a,P b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static void tri(P a,P b,P c,int transparent){
    P normal=cross(sub(b,a),sub(c,a));float length=sqrtf(dot(normal,normal));
    if(length<1e-8f)return;
    float shade=.65f+.35f*fabsf(dot(mul(normal,1/length),(P){-.35f,.82f,-.45f}));
    const float *colour=transparent?(float[]){.06f,.20f,.28f,.035f}:(float[]){.40f*shade,.42f*shade,.44f*shade,1};
    VrCockpitVertex *out=transparent?glass:opaque;unsigned *n=transparent?&mesh.glass_count:&mesh.opaque_count;
    if(*n+3>CAP)abort();
    P points[3]={a,b,c};
    for(int i=0;i<3;i++){out[*n]=(VrCockpitVertex){{points[i].x,points[i].y,points[i].z},{colour[0],colour[1],colour[2],colour[3]}};(*n)++;}
}
static void quad(P a,P b,P c,P d){tri(a,b,c,0);tri(a,c,d,0);}
static void aperture(const P *outer,const P *inner,int n){
    for(int i=0;i<n;i++){int j=(i+1)%n;quad(outer[i],outer[j],inner[j],inner[i]);}
    for(int i=1;i+1<n;i++)tri(inner[0],inner[i],inner[i+1],1);
}
static void build(void){
    if(built)return;
    built=1;
    P a=p(CAN_FACET_APEX),c=p(CAN_CROWN),k=p(CAN_REAR_CROWN);
    P fl=p(CAN_SILL_F_L),fr=p(CAN_SILL_F_R),sl=p(CAN_SILL_S_L),sr=p(CAN_SILL_S_R);
    P rl=p(CAN_SILL_R_L),rr=p(CAN_SILL_R_R),ql=p(CAN_REAR_SHOULDER_L),qr=p(CAN_REAR_SHOULDER_R);
    P hl=p(CAN_SIDE_HEADER_L),hr=p(CAN_SIDE_HEADER_R);
    P tl=p(CAN_FG_T_L),tr=p(CAN_FG_T_R),bl=p(CAN_FG_B_L),br=p(CAN_FG_B_R);
    /* Explicit lower windshield apron; the glass no longer fills the facet. */
    quad(fl,fr,br,bl);
    tri(fr,a,tr,0);tri(fr,tr,br,0);tri(a,tl,tr,0);tri(fl,bl,tl,0);tri(fl,tl,a,0);
    tri(bl,br,tr,1);tri(bl,tr,tl,1);
    P leftOuter[3]={fl,a,sl},leftInner[3]={p(CAN_QG_L_F),p(CAN_QG_L_T),p(CAN_QG_L_B)};
    P rightOuter[3]={fr,sr,a},rightInner[3]={p(CAN_QG_R_F),p(CAN_QG_R_B),p(CAN_QG_R_T)};
    aperture(leftOuter,leftInner,3);aperture(rightOuter,rightInner,3);
    P sideL[4]={sl,hl,ql,rl},glassL[4]={p(CAN_SG_L_FB),p(CAN_SG_L_FT),p(CAN_SG_L_AT),p(CAN_SG_L_AB)};
    P sideR[4]={sr,rr,qr,hr},glassR[4]={p(CAN_SG_R_FB),p(CAN_SG_R_AB),p(CAN_SG_R_AT),p(CAN_SG_R_FT)};
    aperture(sideL,glassL,4);aperture(sideR,glassR,4);
    tri(a,hl,sl,0);tri(a,sr,hr,0);
    tri(a,c,hl,0);tri(a,hr,c,0);
    quad(hl,c,k,ql);quad(c,hr,qr,k);
    tri(ql,k,qr,0);quad(ql,qr,rr,rl);
    unsigned start=mesh.opaque_count;
    quad(fl,sl,sr,fr);quad(sl,rl,rr,sr);
    base=(VrCockpitMesh){opaque+start,NULL,mesh.opaque_count-start,0};
    mesh.opaque_count=start;
}
const VrCockpitMesh *vr_canopy_mesh(void){build();return &mesh;}
const VrCockpitMesh *vr_canopy_base_mesh(void){build();return &base;}
const VrCockpitLandmark *vr_canopy_landmarks(unsigned *count){*count=CAN_POINT_COUNT;return canopy_landmarks;}
int vr_canopy_export(const char *path){
    build();FILE *file=fopen(path,"w");if(!file)return 0;
    fputs("{\"landmarks\":{",file);
    for(unsigned i=0;i<CAN_POINT_COUNT;i++)fprintf(file,"%s\"%s\":[%.8f,%.8f,%.8f]",i?",":"",canopy_landmarks[i].name,
        canopy_landmarks[i].p[0],canopy_landmarks[i].p[1],canopy_landmarks[i].p[2]);
    fputs("}",file);
    for(int layer=0;layer<3;layer++){
        const VrCockpitVertex *v=layer==2?base.opaque:layer?mesh.glass:mesh.opaque;
        unsigned n=layer==2?base.opaque_count:layer?mesh.glass_count:mesh.opaque_count;
        fprintf(file,",\"%s\":[",layer==2?"base":layer?"glass":"opaque");
        for(unsigned i=0;i<n;i+=3){
            fputs(i?",[":"[",file);
            for(int j=0;j<3;j++)fprintf(file,"%s[%.8f,%.8f,%.8f]",j?",":"",v[i+j].p[0],v[i+j].p[1],v[i+j].p[2]);
            fputs("]",file);
        }fputs("]",file);
    }
    fputs("}\n",file);int ok=!ferror(file);return fclose(file)==0&&ok;
}
int vr_canopy_selftest(void){
    build();int fail=0;
    const int panes[][4]={{CAN_FG_B_L,CAN_FG_B_R,CAN_FG_T_R,CAN_FG_T_L},
        {CAN_QG_L_F,CAN_QG_L_T,CAN_QG_L_B,-1},{CAN_QG_R_F,CAN_QG_R_B,CAN_QG_R_T,-1},
        {CAN_SG_L_FB,CAN_SG_L_FT,CAN_SG_L_AT,CAN_SG_L_AB},{CAN_SG_R_FB,CAN_SG_R_AB,CAN_SG_R_AT,CAN_SG_R_FT}};
    for(int i=0;i<5;i++){
        P a=p(panes[i][0]),b=p(panes[i][1]),c=p(panes[i][2]),n=cross(sub(b,a),sub(c,a));
        float area=sqrtf(dot(n,n));
        if(area<1e-5f){fprintf(stderr,"Canopy pane %d is degenerate\n",i);fail++;}
        if(panes[i][3]>=0&&fabsf(dot(n,sub(p(panes[i][3]),a)))/area>1e-5f){fprintf(stderr,"Canopy pane %d is not planar\n",i);fail++;}
    }
    for(unsigned i=0;i<mesh.opaque_count;i++)for(int k=0;k<3;k++)if(!isfinite(opaque[i].p[k]))fail++;
    if(!fail)printf("canopy-selftest OK: five planar openings, lower apron, %u named landmarks\n",CAN_POINT_COUNT);
    return fail?1:0;
}
