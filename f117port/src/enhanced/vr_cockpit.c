#include "enhanced/vr_cockpit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { float x,y,z; } P;
typedef struct { float r,g,b,a; } C;
enum { CAP=24576 };
static VrCockpitVertex opaque[CAP],glass[CAP];
static VrCockpitMesh mesh={opaque,glass,0,0};
static int built;
const VrCockpitSurface vr_cockpit_panel={-.575f,-.602f,.575f,-.21f,-.86f};
static const C wall={.024f,.030f,.036f,1},edge={.070f,.081f,.090f,1},black={.009f,.012f,.015f,1};
static const C console={.040f,.048f,.054f,1},metal={.17f,.19f,.20f,1},olive={.10f,.12f,.08f,1};
static const C yellow={.65f,.43f,.045f,1},green={.05f,.46f,.19f,1},red={.48f,.035f,.018f,1};
static P sub(P a,P b){return(P){a.x-b.x,a.y-b.y,a.z-b.z};}
static P add(P a,P b){return(P){a.x+b.x,a.y+b.y,a.z+b.z};}
static P mul(P a,float s){return(P){a.x*s,a.y*s,a.z*s};}
static P cross(P a,P b){return(P){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float dot(P a,P b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static P unit(P a){float s=sqrtf(dot(a,a));return mul(a,s>0?1/s:0);}
static void tri(P a,P b,P c,C colour,int transparent) {
    P normal=cross(sub(b,a),sub(c,a));
    if(dot(normal,normal)<1e-14f)return;
    VrCockpitVertex *out=transparent?glass:opaque;
    unsigned *n=transparent?&mesh.glass_count:&mesh.opaque_count;
    if(*n+3>CAP){fputs("Cockpit mesh capacity exceeded\n",stderr);abort();}
    P points[3]={a,b,c};
    float shade=1;
    if(!transparent){normal=unit(normal);shade=.72f+.28f*fabsf(dot(normal,(P){-.3f,.8f,.5f}));}
    for(int i=0;i<3;i++)out[(*n)++]=(VrCockpitVertex){{points[i].x,points[i].y,points[i].z},{colour.r*shade,colour.g*shade,colour.b*shade,colour.a}};
}
static void quad(P a,P b,P c,P d,C colour,int transparent){tri(a,b,c,colour,transparent);tri(a,c,d,colour,transparent);}
static void box(float x0,float y0,float z0,float x1,float y1,float z1,C c){
    P p[8]={{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
    const int f[6][4]={{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
    for(int i=0;i<6;i++)quad(p[f[i][0]],p[f[i][1]],p[f[i][2]],p[f[i][3]],c,0);
}
/* Square beams suit the faceted canopy. Their cross section follows the beam,
 * so diagonal windshield posts have physical thickness from every viewpoint. */
static void beam(P a,P b,float width,C c){
    P axis=unit(sub(b,a)),u=unit(cross(axis,fabsf(axis.y)<.9f?(P){0,1,0}:(P){1,0,0})),v=cross(axis,u);
    u=mul(u,width*.5f);v=mul(v,width*.5f);
    P p[4]={add(u,v),sub(v,u),mul(add(u,v),-1),sub(u,v)};
    for(int i=0;i<4;i++)quad(add(a,p[i]),add(b,p[i]),add(b,p[(i+1)%4]),add(a,p[(i+1)%4]),c,0);
    quad(add(a,p[3]),add(a,p[2]),add(a,p[1]),add(a,p[0]),c,0);
    quad(add(b,p[0]),add(b,p[1]),add(b,p[2]),add(b,p[3]),c,0);
}
static void pane(P a,P b,P c,P d){quad(a,b,c,d,(C){.10f,.16f,.17f,.035f},1);}
/* Inset glazing into a solid faceted surround. Keep the openings larger than
 * the references for VR visibility, while retaining their angular outlines. */
static void window(const P *outer,int count,float inset){
    P center={0,0,0},inner[5];
    for(int i=0;i<count;i++)center=add(center,outer[i]);
    center=mul(center,1.0f/count);
    for(int i=0;i<count;i++)inner[i]=add(outer[i],mul(sub(center,outer[i]),inset));
    for(int i=0;i<count;i++){
        int j=(i+1)%count;
        quad(outer[i],outer[j],inner[j],inner[i],black,0);
        beam(inner[i],inner[j],.008f,edge);
    }
    for(int i=1;i+1<count;i++)pane(inner[0],inner[i],inner[i+1],inner[i+1]);
}
static void bezel(float x0,float y0,float x1,float y1){
    /* Recess decorative frames behind the live art, including their buttons. */
    const float w=.009f,z=-.880f;
    box(x0-w,y0-w,z-.025f,x1+w,y0,z,black);box(x0-w,y1,z-.025f,x1+w,y1+w,z,black);
    box(x0-w,y0,z-.025f,x0,y1,z,edge);box(x1,y0,z-.025f,x1+w,y1,z,edge);
    for(int i=0;i<5;i++){
        float x=x0+(i+.5f)*(x1-x0)/5;
        box(x-.007f,y0-.019f,z-.004f,x+.007f,y0-.010f,z+.006f,console);
    }
}
static void build(void){
    if(built)return;
    built=1;
    /* Continuous lower hull: five faces around the sides/footwell, closed at
     * both ends. The open upper edge meets the canopy sill. */
    const P section[6]={{-.70f,-.26f,0},{-.72f,-.63f,0},{-.46f,-1.02f,0},{.46f,-1.02f,0},{.72f,-.63f,0},{.70f,-.26f,0}};
    const float stations[3]={-1.82f,-.45f,.78f},scales[3]={.78f,1,1};
    P rings[3][6];
    for(int s=0;s<3;s++)for(int i=0;i<6;i++)rings[s][i]=(P){section[i].x*scales[s],section[i].y,stations[s]};
    for(int s=0;s<2;s++)for(int i=0;i<5;i++)quad(rings[s][i],rings[s+1][i],rings[s+1][i+1],rings[s][i+1],wall,0);
    for(int s=0;s<3;s+=2)for(int i=1;i<5;i++)tri(rings[s][0],rings[s][i],rings[s][i+1],wall,0);
    quad(rings[2][0],rings[2][5],(P){.46f,.43f,.72f},(P){-.46f,.43f,.72f},wall,0);

    /* Dashboard, coaming and a recessed frame around the original live panel. */
    box(-.64f,-.70f,-1.09f,.64f,-.195f,-.89f,wall);
    quad((P){-.65f,-.18f,-.88f},(P){.65f,-.18f,-.88f},(P){.57f,-.26f,-1.24f},(P){-.57f,-.26f,-1.24f},black,0);
    beam((P){-.61f,-.195f,-.866f},(P){.61f,-.195f,-.866f},.025f,edge);
    beam((P){-.61f,-.65f,-.866f},(P){.61f,-.65f,-.866f},.025f,edge);
    beam((P){-.61f,-.195f,-.866f},(P){-.61f,-.65f,-.866f},.025f,edge);
    beam((P){.61f,-.195f,-.866f},(P){.61f,-.65f,-.866f},.025f,edge);
    bezel(-.309f,-.492f,-.036f,-.278f);bezel(.032f,-.492f,.306f,-.278f);

    /* Five flat panes, as in the reference: a near-triangular raked windscreen
     * with a small clipped apex (not a broad horizontal brow),
     * two triangular front lights and two swept side panes. Each front light
     * shares its rear edge directly with its side pane: no invented opaque
     * cheek or center windshield divider. Side sills rise to a narrow aft tip. */
    P fl={-.46f,-.17f,-1.82f},fr={.46f,-.17f,-1.82f},tl={-.045f,.48f,-.72f},tr={.045f,.48f,-.72f};
    P sl={-.70f,-.25f,-1.20f},sr={.70f,-.25f,-1.20f};
    P bl={-.68f,-.01f,.52f},br={.68f,-.01f,.52f};
    /* Solve the aft upper corner on the side plane, so each transparency is
     * flat rather than a warped polygon assembled from unrelated corners. */
    P normal=cross(sub(tl,sl),sub(bl,sl));
    P rl={0,.31f,.08f};
    rl.x=sl.x-(normal.y*(rl.y-sl.y)+normal.z*(rl.z-sl.z))/normal.x;
    P rr={-rl.x,rl.y,rl.z};
    P peak0={0,.58f,-.60f},peak1={0,.53f,.30f},backPeak={0,.47f,.72f};
    P front[4]={fl,fr,tr,tl};
    P leftQuarter[3]={fl,tl,sl},rightQuarter[3]={fr,sr,tr};
    P leftSide[4]={sl,tl,rl,bl},rightSide[4]={sr,br,rr,tr};
    window(front,4,.055f);
    window(leftQuarter,3,.075f);window(rightQuarter,3,.075f);
    window(leftSide,4,.10f);window(rightSide,4,.10f);
    quad(tl,peak0,peak1,rl,wall,0);quad(peak0,tr,rr,peak1,wall,0);
    tri(tl,tr,peak0,wall,0);
    quad(bl,rl,(P){-.46f,.43f,.72f},rings[2][0],wall,0);
    quad(br,rings[2][5],(P){.46f,.43f,.72f},rr,wall,0);
    quad(rl,peak1,backPeak,(P){-.46f,.43f,.72f},black,0);
    quad(peak1,rr,(P){.46f,.43f,.72f},backPeak,black,0);
    tri((P){-.46f,.43f,.72f},backPeak,(P){.46f,.43f,.72f},wall,0);
    quad(fl,fr,rings[0][5],rings[0][0],wall,0);
    quad(fl,sl,rings[1][0],rings[0][0],wall,0);
    quad(fr,rings[0][5],rings[1][5],sr,wall,0);
    quad(sl,bl,rings[2][0],rings[1][0],wall,0);
    quad(sr,rings[1][5],rings[2][5],br,wall,0);
    const P ends[][2]={{fl,fr},{fl,tl},{fr,tr},{tl,tr},{fl,sl},{fr,sr},{sl,tl},{sr,tr},
        {sl,bl},{sr,br},{tl,rl},{tr,rr},{bl,rl},{br,rr},{rl,rr},{tl,peak0},{tr,peak0},{rl,peak1},{rr,peak1}};
    for(unsigned i=0;i<sizeof ends/sizeof *ends;i++)beam(ends[i][0],ends[i][1],.028f,edge);
    beam(peak0,peak1,.022f,edge);
    /* Broad sill seals hide the join between glass and lower airframe. */
    beam(fl,sl,.050f,black);beam(fr,sr,.050f,black);beam(sl,bl,.055f,black);beam(sr,br,.055f,black);
    beam(bl,rings[2][0],.055f,black);beam(br,rings[2][5],.055f,black);

    for(int side=-1;side<=1;side+=2){
        float x=side<0?-.65f:.42f;
        /* Consoles start beside the pilot, clear of the panel's viewing cone. */
        box(x,-.66f,-.55f,x+.23f,-.44f,.51f,console);
        for(int bank=0;bank<4;bank++){
            float z=-.50f+bank*.26f;
            box(x+.018f,-.435f,z,x+.212f,-.427f,z+.20f,black);
            for(int row=0;row<3;row++)for(int col=0;col<2;col++){
                float px=x+.05f+col*.10f,pz=z+.035f+row*.06f;
                box(px-.018f,-.425f,pz-.016f,px+.018f,-.413f,pz+.016f,edge);
                beam((P){px,-.41f,pz},(P){px,-.385f,pz-.012f},.009f,metal);
            }
            box(x+.03f,-.42f,z+.208f,x+.07f,-.41f,z+.219f,bank==0?yellow:green);
        }
        /* Inner wall ribs, latch hardware and ventilation slats. */
        for(int i=0;i<4;i++){
            float z=-.55f+i*.32f;
            beam((P){side*.69f,-.23f,z},(P){side*.69f,-.58f,z},.022f,edge);
        }
        box(side<0?-.695f:.65f,-.26f,-.40f,side<0?-.65f:.695f,-.215f,-.18f,metal);
    }
    /* Ejection seat and harness, rear equipment, rudder pedals, stick and throttle. */
    box(-.27f,-.81f,-.06f,.27f,-.685f,.41f,olive);
    box(-.285f,-.73f,.32f,.285f,.205f,.48f,olive);
    box(-.18f,.205f,.39f,.18f,.385f,.55f,black);
    for(int side=-1;side<=1;side+=2){
        beam((P){side*.17f,.16f,.307f},(P){side*.10f,-.59f,.307f},.048f,(C){.20f,.22f,.15f,1});
        box(side<0?-.275f:.11f,-.945f,-.99f,side<0?-.11f:.275f,-.90f,-.80f,metal);
        for(int i=0;i<4;i++)box(side<0?-.267f:.118f,-.895f,-.97f+i*.045f,side<0?-.118f:.267f,-.888f,-.961f+i*.045f,black);
        box(side<0?-.43f:.31f,-.53f,.53f,side<0?-.31f:.43f,.15f,.69f,black);
    }
    box(.17f,-.74f,-.52f,.31f,-.68f,-.36f,black);
    beam((P){.24f,-.68f,-.44f},(P){.24f,-.43f,-.42f},.035f,metal);
    box(.207f,-.47f,-.45f,.275f,-.345f,-.385f,black);box(.23f,-.35f,-.455f,.26f,-.333f,-.42f,red);
    box(-.61f,-.425f,-.39f,-.45f,-.41f,-.18f,black);
    beam((P){-.54f,-.41f,-.34f},(P){-.54f,-.32f,-.31f},.038f,metal);
    box(-.60f,-.335f,-.345f,-.48f,-.29f,-.275f,black);
    beam((P){-.06f,-.65f,.10f},(P){-.06f,-.57f,.10f},.018f,yellow);
    beam((P){.06f,-.65f,.10f},(P){.06f,-.57f,.10f},.018f,yellow);
    beam((P){-.06f,-.57f,.10f},(P){.06f,-.57f,.10f},.018f,yellow);
    for(int i=0;i<5;i++)box(-.055f+i*.024f,-.581f,.089f,-.047f+i*.024f,-.559f,.111f,black);
}
const VrCockpitMesh *vr_cockpit_mesh(void){build();return &mesh;}

/* Test the rendered triangle soup: lower/rear sightlines must hit the enclosure,
 * while forward/side window sightlines remain clear through a glass pane. */
static int ray_hit(const VrCockpitVertex *v,unsigned count,P origin,P dir,float limit){
    for(unsigned i=0;i+2<count;i+=3){
        P a={v[i].p[0],v[i].p[1],v[i].p[2]},b={v[i+1].p[0],v[i+1].p[1],v[i+1].p[2]},c={v[i+2].p[0],v[i+2].p[1],v[i+2].p[2]};
        P e1=sub(b,a),e2=sub(c,a),h=cross(dir,e2);float det=dot(e1,h);
        if(fabsf(det)<1e-7f)continue;
        P s=sub(origin,a);float u=dot(s,h)/det;if(u<0||u>1)continue;
        P q=cross(s,e1);float vv=dot(dir,q)/det;if(vv<0||u+vv>1)continue;
        float t=dot(e2,q)/det;if(t>.015f&&t<limit)return 1;
    }return 0;
}
int vr_cockpit_selftest(void){
    build();int fail=0;
    const P enclosed[]={{0,-1,0},{-1,-.65f,0},{1,-.65f,0},{0,0,1},{0,.4f,1},{0,-.8f,-1},{-.5f,-1,-.5f},{.5f,-1,-.5f},
        {0,1,0},{-.35f,.8f,-.25f},{.35f,.8f,-.25f}};
    const P eyes[]={{0,0,0},{-.10f,0,0},{.10f,0,0}};
    for(unsigned e=0;e<3;e++)for(unsigned i=0;i<sizeof enclosed/sizeof *enclosed;i++)
        if(!ray_hit(opaque,mesh.opaque_count,eyes[e],unit(enclosed[i]),2.5f)){fprintf(stderr,"Cockpit enclosure leak: eye %u ray %u\n",e,i);fail++;}
    /* The triangular front lights are narrow: aim through their centers,
     * rather than the swept posts shared with the side panes. */
    const P windows[]={{0,.03f,-1},{-.18f,.03f,-1},{.18f,.03f,-1},{-.38f,.016f,-1},{.38f,.016f,-1},{1,.1f,0},{-1,.1f,0}};
    for(unsigned i=0;i<sizeof windows/sizeof *windows;i++){
        P d=unit(windows[i]);
        if(ray_hit(opaque,mesh.opaque_count,(P){0,0,0},d,2.5f)||!ray_hit(glass,mesh.glass_count,(P){0,0,0},d,2.5f)){
            fprintf(stderr,"Cockpit window blocked or unglazed: ray %u\n",i);fail++;
        }
    }
    for(unsigned e=0;e<3;e++)for(int x=-1;x<=1;x++)for(int y=0;y<3;y++){
        P target={x*.45f,-.19f+y*.19f,-1},delta=sub(target,eyes[e]);
        if(ray_hit(opaque,mesh.opaque_count,eyes[e],unit(delta),sqrtf(dot(delta,delta))-.005f)){
            fprintf(stderr,"Cockpit obscures HUD: eye %u point %d,%d\n",e,x,y);fail++;
        }
    }
    unsigned blocked=0,samples=0;
    for(unsigned e=0;e<3;e++)for(int x=0;x<=32;x++)for(int y=0;y<=16;y++){
        const VrCockpitSurface *p=&vr_cockpit_panel;
        P target={p->x0+(p->x1-p->x0)*x/32,p->y0+(p->y1-p->y0)*y/16,p->z};
        P delta=sub(target,eyes[e]);samples++;
        blocked+=ray_hit(opaque,mesh.opaque_count,eyes[e],unit(delta),sqrtf(dot(delta,delta))-.001f);
    }
    if(blocked){fprintf(stderr,"Cockpit obscures live instrument panel: %u / %u sightlines blocked\n",blocked,samples);fail++;}
    if(!fail)printf("cockpit-selftest OK: %u opaque / %u glass triangles; enclosure, windows, HUD and %u panel sightlines checked\n",mesh.opaque_count/3,mesh.glass_count/3,samples);
    return fail?1:0;
}
