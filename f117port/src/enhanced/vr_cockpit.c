#include "enhanced/vr_cockpit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { float x,y,z; } P;
typedef struct { float r,g,b,a; } C;
enum { CAP=24576 };
static VrCockpitVertex opaque[CAP],glass[CAP];
static VrCockpitMesh mesh={opaque,glass,0,0};
static VrCockpitMesh canopy_mesh,hud_mesh;
static int built;
const VrCockpitSurface vr_cockpit_panel={-.575f,-.602f,.575f,-.21f,-.88f};
static VrCockpitInstrument instruments[]={
    {"countermeasures/status",{0,104,64,200},{0,0,0,0,-.850f}},
    {"left MFD",{65,104,159,185},{0,0,0,0,-.850f}},
    {"right MFD",{160,104,255,179},{0,0,0,0,-.850f}},
    {"landing/status",{256,103,311,132},{0,0,0,0,-.850f}},
    {"attitude",{257,134,307,177},{0,0,0,0,-.850f}},
    {"hazard marking",{308,134,320,178},{0,0,0,0,-.850f}},
    {"engine",{65,186,159,200},{0,0,0,0,-.850f}},
    {"weapons",{169,181,255,200},{0,0,0,0,-.850f}},
    {"fuel/thrust",{257,178,320,200},{0,0,0,0,-.850f}},
    {"mode annunciators",{98,93,223,104},{0,0,0,0,-.850f}},
};
static float hud_aperture[VR_HUD_CORNERS][3];
static const float hud_depth=.48f;
static float panel_x(float px){return vr_cockpit_panel.x0+px*(vr_cockpit_panel.x1-vr_cockpit_panel.x0)/320;}
static float panel_y(float py){return vr_cockpit_panel.y1-(py-91)*(vr_cockpit_panel.y1-vr_cockpit_panel.y0)/109;}
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
/* A modest painted surround and narrow gasket. Pane size comes from the
 * canopy landmarks, rather than filling a large frame with an opaque border. */
static void window(const P *outer,int count,float inset){
    P center={0,0,0},inner[5];
    for(int i=0;i<count;i++)center=add(center,outer[i]);
    center=mul(center,1.0f/count);
    for(int i=0;i<count;i++)inner[i]=add(outer[i],mul(sub(center,outer[i]),inset));
    for(int i=0;i<count;i++){
        int j=(i+1)%count;
        quad(outer[i],outer[j],inner[j],inner[i],wall,0);
        beam(inner[i],inner[j],.008f,black);
    }
    for(int i=1;i+1<count;i++)pane(inner[0],inner[i],inner[i+1],inner[i+1]);
}
static void build(void){
    if(built)return;
    built=1;
    /* Compact the actual glazed shell: lower front header, narrower sill and
     * shorter fore/aft extent. Keep the pointed five-pane arrangement. */
    P fl={-.42f,-.15f,-1.68f},fr={.42f,-.15f,-1.68f},tl={-.04f,.36f,-.76f},tr={.04f,.36f,-.76f};
    P sl={-.62f,-.20f,-1.10f},sr={.62f,-.20f,-1.10f};
    P bl={-.60f,-.025f,.20f},br={.60f,-.025f,.20f};
    P aftLeft={-.43f,.30f,.72f},aftRight={.43f,.30f,.72f};
    /* Continuous lower hull: five faces around the sides/footwell, closed at
     * both ends. The open upper edge meets the canopy sill. */
    const P section[6]={{-.70f,-.26f,0},{-.72f,-.63f,0},{-.46f,-1.02f,0},{.46f,-1.02f,0},{.72f,-.63f,0},{.70f,-.26f,0}};
    const float stations[3]={fl.z,-.45f,.78f},scales[3]={.78f,1,1};
    P rings[3][6];
    for(int s=0;s<3;s++)for(int i=0;i<6;i++)rings[s][i]=(P){section[i].x*scales[s],section[i].y,stations[s]};
    for(int s=0;s<2;s++)for(int i=0;i<5;i++)quad(rings[s][i],rings[s+1][i],rings[s+1][i+1],rings[s][i+1],wall,0);
    for(int s=0;s<3;s+=2)for(int i=1;i<5;i++)tri(rings[s][0],rings[s][i],rings[s][i+1],wall,0);
    quad(rings[2][0],rings[2][5],aftRight,aftLeft,wall,0);

    /* One layout drives the live texture UVs and the solid instrument housings.
     * The original painted coaming is omitted; the real coaming joins the sill. */
    const float top=panel_y(103),bottom=panel_y(200);
    box(-.605f,bottom-.018f,-1.10f,.605f,top,-.884f,wall);
    quad((P){-.61f,top,-.91f},(P){.61f,top,-.91f},fr,fl,black,0);
    beam((P){-.61f,top,-.89f},(P){.61f,top,-.89f},.018f,edge);
    beam((P){-.605f,bottom-.012f,-.89f},(P){.605f,bottom-.012f,-.89f},.018f,edge);
    for(unsigned i=0;i<sizeof instruments/sizeof *instruments;i++){
        VrCockpitInstrument *in=&instruments[i];VrCockpitSurface *p=&in->face;
        p->x0=panel_x(in->source[0]);p->x1=panel_x(in->source[2]);
        p->y0=panel_y(in->source[3]);p->y1=panel_y(in->source[1]);
        const float rim=.003f,z=p->z-.003f;
        box(p->x0,p->y0,-.91f,p->x1,p->y1,z,black);
        beam((P){p->x0+rim,p->y0+rim,z},(P){p->x1-rim,p->y0+rim,z},.005f,edge);
        beam((P){p->x0+rim,p->y1-rim,z},(P){p->x1-rim,p->y1-rim,z},.005f,edge);
        beam((P){p->x0+rim,p->y0+rim,z},(P){p->x0+rim,p->y1-rim,z},.005f,edge);
        beam((P){p->x1-rim,p->y0+rim,z},(P){p->x1-rim,p->y1-rim,z},.005f,edge);
    }
    /* Physical HUD combiner, enlarged around the original six-sided aperture
     * to retain the readouts from both nominal seated eyes. */
    const unsigned hudOpaque=mesh.opaque_count,hudGlass=mesh.glass_count;
    const float outline[VR_HUD_CORNERS][2]={{46,91},{46,14},{90,2},{232,2},{275,14},{275,91}};
    P combiner[VR_HUD_CORNERS];
    for(int i=0;i<VR_HUD_CORNERS;i++){
        float x=(outline[i][0]-159)*hud_depth/256;
        float y=(52-outline[i][1])*hud_depth/192;
        x+=outline[i][0]<159?-.075f:.075f;
        y+=outline[i][1]>52?-.009f:.030f;
        combiner[i]=(P){x,y,-hud_depth};
        hud_aperture[i][0]=x;hud_aperture[i][1]=y;hud_aperture[i][2]=-hud_depth;
    }
    for(int i=1;i+1<VR_HUD_CORNERS;i++)tri(combiner[0],combiner[i],combiner[i+1],(C){.12f,.21f,.19f,.065f},1);
    for(int i=0;i+1<VR_HUD_CORNERS;i++)beam(combiner[i],combiner[i+1],.003f,edge);
    beam(combiner[0],combiner[5],.012f,black);
    beam(combiner[0],(P){-.40f,top-.005f,-.89f},.012f,edge);
    beam(combiner[5],(P){.40f,top-.005f,-.89f},.012f,edge);
    hud_mesh=(VrCockpitMesh){opaque+hudOpaque,glass+hudGlass,mesh.opaque_count-hudOpaque,mesh.glass_count-hudGlass};

    /* Five flat panes, as in the reference: a near-triangular raked windscreen
     * with a small clipped apex (not a broad horizontal brow),
     * two triangular front lights and two swept side panes. Each front light
     * shares its rear edge directly with its side pane: no invented opaque
     * cheek or center windshield divider. Side sills rise to a narrow aft tip. */
    const unsigned canopyOpaque=mesh.opaque_count,canopyGlass=mesh.glass_count;
    /* Solve the aft upper corner on the side plane, so each transparency is
     * flat rather than a warped polygon assembled from unrelated corners. */
    P normal=cross(sub(tl,sl),sub(bl,sl));
    P rl={0,.29f,-.12f};
    rl.x=sl.x-(normal.y*(rl.y-sl.y)+normal.z*(rl.z-sl.z))/normal.x;
    P rr={-rl.x,rl.y,rl.z};
    P peak0={0,.51f,-.64f},peak1={0,.40f,.26f},backPeak={0,.35f,.72f};
    P front[4]={fl,fr,tr,tl};
    P leftQuarter[3]={fl,tl,sl},rightQuarter[3]={fr,sr,tr};
    P leftSide[4]={sl,tl,rl,bl},rightSide[4]={sr,br,rr,tr};
    window(front,4,.055f);
    window(leftQuarter,3,.075f);window(rightQuarter,3,.075f);
    window(leftSide,4,.10f);window(rightSide,4,.10f);
    quad(tl,peak0,peak1,rl,wall,0);quad(peak0,tr,rr,peak1,wall,0);
    tri(tl,tr,peak0,wall,0);
    quad(bl,rl,aftLeft,rings[2][0],wall,0);
    quad(br,rings[2][5],aftRight,rr,wall,0);
    quad(rl,peak1,backPeak,aftLeft,black,0);
    quad(peak1,rr,aftRight,backPeak,black,0);
    tri(aftLeft,backPeak,aftRight,wall,0);
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
    canopy_mesh=(VrCockpitMesh){opaque+canopyOpaque,glass+canopyGlass,mesh.opaque_count-canopyOpaque,mesh.glass_count-canopyGlass};

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
            beam((P){side*(fabsf(sl.x)-.015f),-.23f,z},(P){side*.69f,-.58f,z},.022f,edge);
        }
        const float latchOuter=fabsf(sl.x)-.005f,latchInner=latchOuter-.045f;
        box(side<0?-latchOuter:latchInner,-.26f,-.40f,side<0?-latchInner:latchOuter,-.215f,-.18f,metal);
    }
    /* Ejection seat and harness, rear equipment, rudder pedals, stick and throttle. */
    box(-.27f,-.81f,-.06f,.27f,-.685f,.41f,olive);
    box(-.285f,-.73f,.32f,.285f,.205f,.48f,olive);
    box(-.18f,.105f,.39f,.18f,.285f,.55f,black);
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
const VrCockpitMesh *vr_cockpit_canopy_mesh(void){build();return &canopy_mesh;}
const VrCockpitMesh *vr_cockpit_hud_mesh(void){build();return &hud_mesh;}
const VrCockpitInstrument *vr_cockpit_instruments(unsigned *count){build();*count=sizeof instruments/sizeof *instruments;return instruments;}
const float (*vr_cockpit_hud_aperture(void))[3]{build();return hud_aperture;}
void vr_cockpit_hud_uv(const float point[3],const double eye[3],float uv[2]){
    const double depth=eye[2]-point[2];
    if(depth<=.03){uv[0]=uv[1]=-1;return;}
    uv[0]=(float)((159+256*(point[0]-eye[0])/depth)/320);
    uv[1]=(float)((52-192*(point[1]-eye[1])/depth)/91);
}

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
    const float marks[][2]={{159,52},{93,7},{230,7},{53,32},{265,32},{55,65},{265,65},{60,86},{260,86},{159,89}};
    for(int e=-1;e<=1;e++)for(unsigned i=0;i<sizeof marks/sizeof *marks;i++){
        P origin={e*.032f,0,0};
        P target={origin.x+(marks[i][0]-159)*hud_depth/256,(52-marks[i][1])*hud_depth/192,-hud_depth};
        P delta=sub(target,origin);
        if(ray_hit(opaque,mesh.opaque_count,origin,unit(delta),sqrtf(dot(delta,delta))-.002f)
            ||!ray_hit(glass,mesh.glass_count,origin,unit(delta),sqrtf(dot(delta,delta))+.002f)){
            fprintf(stderr,"Cockpit obscures HUD combiner: eye %d mark %u\n",e,i);fail++;
        }
        float uv[2];double eye[3]={origin.x,origin.y,origin.z};
        float position[3]={target.x,target.y,target.z};
        vr_cockpit_hud_uv(position,eye,uv);
        if(fabsf(uv[0]*320-marks[i][0])>.001f||fabsf(uv[1]*91-marks[i][1])>.001f){
            fprintf(stderr,"HUD bearing changes between eyes: eye %d mark %u\n",e,i);fail++;
        }
    }
    unsigned blocked=0,samples=0;
    for(unsigned i=0;i<sizeof instruments/sizeof *instruments;i++){
        unsigned before=blocked;
        for(unsigned e=0;e<3;e++)for(int x=0;x<=16;x++)for(int y=0;y<=8;y++){
            const VrCockpitSurface *p=&instruments[i].face;
            P target={p->x0+(p->x1-p->x0)*x/16,p->y0+(p->y1-p->y0)*y/8,p->z};
            P delta=sub(target,eyes[e]);samples++;
            blocked+=ray_hit(opaque,mesh.opaque_count,eyes[e],unit(delta),sqrtf(dot(delta,delta))-.001f);
        }
        if(blocked>before)fprintf(stderr,"Instrument %s obscured at %u sample points\n",instruments[i].name,blocked-before);
    }
    if(blocked){fprintf(stderr,"Cockpit obscures live instrument panel: %u / %u sightlines blocked\n",blocked,samples);fail++;}
    if(!fail)printf("cockpit-selftest OK: %u opaque / %u glass triangles; enclosure, windows, collimated HUD and %u instrument sightlines checked\n",mesh.opaque_count/3,mesh.glass_count/3,samples);
    return fail?1:0;
}
