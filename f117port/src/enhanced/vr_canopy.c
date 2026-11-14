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
static VrCockpitVertex pilot_opaque[CAP],pilot_glass[CAP],pilot_base[12];
static VrCockpitMesh pilot_mesh,pilot_floor;
static VrCockpitLandmark pilot_points[CAN_POINT_COUNT];
static int pilot_built;
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
static void pilot_build(void){
    build();if(pilot_built)return;pilot_built=1;
    for(int i=0;i<CAN_POINT_COUNT;i++)pilot_points[i]=canopy_landmarks[i];
    /* The photo bundle does not determine cabin depth, absolute size or the
     * pilot datum. Anchor a practical five-pane adaptation to the live dash
     * and a seated eye at (0,0,0), rather than moving the simulation camera. */
    P a={0,.48f,-.80f},c={0,.52f,-.78f},k={0,.43f,.60f};
    P f={-.43f,-.20f,-1.65f},s={-.68f,-.26f,-1.00f},r={-.65f,-.16f,.60f},h={-.055f,.45f,-.76f};
    P n=cross(sub(s,h),sub(r,h)),q={0,.30f,.60f};
    q.x=h.x-(n.y*(q.y-h.y)+n.z*(q.z-h.z))/n.x;
    const int center[]={CAN_FACET_APEX,CAN_CROWN,CAN_REAR_CROWN};
    const P centers[]={a,c,k};
    for(int i=0;i<3;i++){pilot_points[center[i]].p[0]=centers[i].x;pilot_points[center[i]].p[1]=centers[i].y;pilot_points[center[i]].p[2]=centers[i].z;}
    const int left[]={CAN_SILL_F_L,CAN_SILL_S_L,CAN_SILL_R_L,CAN_SIDE_HEADER_L,CAN_REAR_SHOULDER_L,CAN_SILL_KNEE_L,
        CAN_FG_T_L,CAN_FG_B_L,CAN_QG_L_T,CAN_QG_L_F,CAN_QG_L_B,CAN_SG_L_FT,CAN_SG_L_FB,CAN_SG_L_AT,CAN_SG_L_AB};
    const int right[]={CAN_SILL_F_R,CAN_SILL_S_R,CAN_SILL_R_R,CAN_SIDE_HEADER_R,CAN_REAR_SHOULDER_R,CAN_SILL_KNEE_R,
        CAN_FG_T_R,CAN_FG_B_R,CAN_QG_R_T,CAN_QG_R_F,CAN_QG_R_B,CAN_SG_R_FT,CAN_SG_R_FB,CAN_SG_R_AT,CAN_SG_R_AB};
    P values[15]={f,s,r,h,q,{.25f*f.x+.75f*s.x,.25f*f.y+.75f*s.y,.25f*f.z+.75f*s.z},
        {-.025f,.385f,-.91875f},{-.34f,-.08f,-1.50f}};
    const float quarter[3][3]={{.90f,.06f,.04f},{.10f,.84f,.06f},{.08f,.10f,.82f}};
    for(int i=0;i<3;i++)values[8+i]=(P){quarter[i][0]*a.x+quarter[i][1]*f.x+quarter[i][2]*s.x,
        quarter[i][0]*a.y+quarter[i][1]*f.y+quarter[i][2]*s.y,quarter[i][0]*a.z+quarter[i][1]*f.z+quarter[i][2]*s.z};
    const float side[4][2]={{.08f,.88f},{.05f,.10f},{.68f,.87f},{.91f,.12f}};
    for(int i=0;i<4;i++){
        float u=side[i][0],v=side[i][1];
        values[11+i]=(P){(1-u)*((1-v)*s.x+v*h.x)+u*((1-v)*r.x+v*q.x),
            (1-u)*((1-v)*s.y+v*h.y)+u*((1-v)*r.y+v*q.y),
            (1-u)*((1-v)*s.z+v*h.z)+u*((1-v)*r.z+v*q.z)};
    }
    for(int i=0;i<15;i++)for(int mirror=0;mirror<2;mirror++){
        int index=mirror?right[i]:left[i];
        pilot_points[index].p[0]=mirror?-values[i].x:values[i].x;
        pilot_points[index].p[1]=values[i].y;pilot_points[index].p[2]=values[i].z;
    }
    pilot_mesh=(VrCockpitMesh){pilot_opaque,pilot_glass,mesh.opaque_count,mesh.glass_count};
    pilot_floor=(VrCockpitMesh){pilot_base,NULL,base.opaque_count,0};
    for(int layer=0;layer<3;layer++){
        const VrCockpitVertex *src=layer==2?base.opaque:layer?glass:opaque;
        VrCockpitVertex *dst=layer==2?pilot_base:layer?pilot_glass:pilot_opaque;
        unsigned count=layer==2?base.opaque_count:layer?mesh.glass_count:mesh.opaque_count;
        for(unsigned i=0;i<count;i++){
            dst[i]=src[i];int found=0;
            for(int n=0;n<CAN_POINT_COUNT;n++)if(src[i].p[0]==canopy_landmarks[n].p[0]&&src[i].p[1]==canopy_landmarks[n].p[1]&&src[i].p[2]==canopy_landmarks[n].p[2]){
                for(int k=0;k<3;k++)dst[i].p[k]=pilot_points[n].p[k];
                found=1;break;
            }
            if(!found)abort();
        }
        if(layer==0)for(unsigned i=0;i<count;i+=3){
            P a={dst[i].p[0],dst[i].p[1],dst[i].p[2]},b={dst[i+1].p[0],dst[i+1].p[1],dst[i+1].p[2]},c={dst[i+2].p[0],dst[i+2].p[1],dst[i+2].p[2]};
            P normal=cross(sub(b,a),sub(c,a));float length=sqrtf(dot(normal,normal));
            float shade=.65f+.35f*fabsf(dot(mul(normal,1/length),(P){-.35f,.82f,-.45f}));
            for(int j=0;j<3;j++){dst[i+j].colour[0]=.026f*shade;dst[i+j].colour[1]=.032f*shade;dst[i+j].colour[2]=.038f*shade;}
        }
    }
}
const VrCockpitMesh *vr_canopy_pilot_mesh(void){pilot_build();return &pilot_mesh;}
const VrCockpitLandmark *vr_canopy_pilot_landmarks(unsigned *count){pilot_build();*count=CAN_POINT_COUNT;return pilot_points;}
static int export_geometry(const char *path,const VrCockpitMesh *surface,const VrCockpitMesh *floor,const VrCockpitLandmark *points){
    FILE *file=fopen(path,"w");if(!file)return 0;
    fputs("{\"landmarks\":{",file);
    for(unsigned i=0;i<CAN_POINT_COUNT;i++)fprintf(file,"%s\"%s\":[%.8f,%.8f,%.8f]",i?",":"",points[i].name,
        points[i].p[0],points[i].p[1],points[i].p[2]);
    fputs("}",file);
    for(int layer=0;layer<3;layer++){
        const VrCockpitVertex *v=layer==2?floor->opaque:layer?surface->glass:surface->opaque;
        unsigned n=layer==2?floor->opaque_count:layer?surface->glass_count:surface->opaque_count;
        fprintf(file,",\"%s\":[",layer==2?"base":layer?"glass":"opaque");
        for(unsigned i=0;i<n;i+=3){
            fputs(i?",[":"[",file);
            for(int j=0;j<3;j++)fprintf(file,"%s[%.8f,%.8f,%.8f]",j?",":"",v[i+j].p[0],v[i+j].p[1],v[i+j].p[2]);
            fputs("]",file);
        }fputs("]",file);
    }
    fputs("}\n",file);int ok=!ferror(file);return fclose(file)==0&&ok;
}
int vr_canopy_export(const char *path){build();return export_geometry(path,&mesh,&base,canopy_landmarks);}
int vr_canopy_pilot_export(const char *path){pilot_build();return export_geometry(path,&pilot_mesh,&pilot_floor,pilot_points);}
int vr_canopy_selftest(void){
    pilot_build();int fail=0;
    const int panes[][4]={{CAN_FG_B_L,CAN_FG_B_R,CAN_FG_T_R,CAN_FG_T_L},
        {CAN_QG_L_F,CAN_QG_L_T,CAN_QG_L_B,-1},{CAN_QG_R_F,CAN_QG_R_B,CAN_QG_R_T,-1},
        {CAN_SG_L_FB,CAN_SG_L_FT,CAN_SG_L_AT,CAN_SG_L_AB},{CAN_SG_R_FB,CAN_SG_R_AB,CAN_SG_R_AT,CAN_SG_R_FT}};
    for(int variant=0;variant<2;variant++)for(int i=0;i<5;i++){
        P corners[4]={{0,0,0}};const VrCockpitLandmark *points=variant?pilot_points:canopy_landmarks;
        for(int j=0;j<4;j++)if(panes[i][j]>=0){const float *v=points[panes[i][j]].p;corners[j]=(P){v[0],v[1],v[2]};}
        P a=corners[0],b=corners[1],c=corners[2],n=cross(sub(b,a),sub(c,a));
        float area=sqrtf(dot(n,n));
        if(area<1e-5f){fprintf(stderr,"Canopy variant %d pane %d is degenerate\n",variant,i);fail++;continue;}
        if(panes[i][3]>=0&&fabsf(dot(n,sub(corners[3],a)))/area>1e-5f){fprintf(stderr,"Canopy variant %d pane %d is not planar\n",variant,i);fail++;}
    }
    for(unsigned i=0;i<mesh.opaque_count;i++)for(int k=0;k<3;k++)if(!isfinite(opaque[i].p[k]))fail++;
    if(!fail)printf("canopy-selftest OK: reference and seated meshes, five planar openings each, %u named landmarks\n",CAN_POINT_COUNT);
    return fail?1:0;
}
