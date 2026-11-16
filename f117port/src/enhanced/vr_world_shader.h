/* Palette rasterisation and optional presentation. No scene depth test: retain
 * the original painter order and COPY / AND / OR terrain construction. */
static const char *worldShader=R"HLSL(
cbuffer Params : register(b0) {float4 source;float4 fov;float4 mode;};
cbuffer Presentation : register(b1) {
    float4 viewX,viewY,viewZ,origin,sea,ships[8],shipshape[8],settings;
};
struct I {float2 ray:POSITION;uint colour:COLOR0;float invDepth:TEXCOORD0;float band:TEXCOORD1;uint flags:COLOR1;};
struct O {float4 p:SV_POSITION;nointerpolation uint colour:COLOR0;
    noperspective float invDepth:TEXCOORD0;noperspective float band:TEXCOORD1;nointerpolation uint flags:COLOR1;};
O WV(I i) {O o;o.p=float4((2*i.ray.x-fov.y-fov.x)/(fov.y-fov.x),
    (2*i.ray.y-fov.z-fov.w)/(fov.z-fov.w),0,1);o.colour=i.colour;
    o.invDepth=i.invDepth;o.band=i.band;o.flags=i.flags;return o;}
uint WP(O i):SV_TARGET {
    uint lo=i.colour&255,hi=i.colour>>8;
    uint parity=(uint(i.p.x)^uint(i.p.y))&1;
    if(mode.z<0.5) {
        float2 ray=float2(lerp(fov.x,fov.y,i.p.x/source.x),lerp(fov.z,fov.w,i.p.y/source.y));
        parity=(int(floor(159+ray.x*256))^int(floor(100-ray.y*192)))&1;
    }
    if(hi==252 && parity==0) discard;
    uint c=hi<251 && parity!=0 ? hi:lo;
    if(settings.w<0.5)return c;
    // AND preserves all attributes. OR adds water without erasing ground bands.
    if(hi==254)return 0xFFFFFF00|c;
    if(hi==253)return c|(i.flags&0x300);
    uint depth=i.invDepth>0 ? f32tof16(min(65000,rcp(max(i.invDepth,1e-8))/8)) : 0;
    return c|i.flags|(uint(round(saturate(i.band)*15))<<12)|(depth<<16);
}
Texture2DMS<uint,WORLD_SAMPLES> idxMS:register(t0);
Texture2D<float4> palette:register(t1);
Texture2D<uint> idx:register(t2);
float4 pal(uint i) {return palette.Load(int3(i&255,0,0));}
uint loadIndex(int2 q,int k) {
    q=clamp(q,int2(0,0),int2(source.xy)-1);
#if WORLD_MULTISAMPLE
    return idxMS.Load(q,k);
#else
    return idx.Load(int3(q,0));
#endif
}
bool water(uint v) {return (v&0x100)!=0 && (v&0xF0)==0xF0;}
bool land(uint v) {return (v&0x200)!=0 && (v&0xF0)==0x70;}
float hash(float2 p) {return frac(sin(dot(p-floor(p/32)*32,float2(127.1,311.7)))*43758.5453);}
float wavelet(float2 world,float size,float time) {
    float2 uv=float2(world.x+world.y*.5,world.y)/size;
    float seed=hash(floor(uv));
    float2 fw=max(fwidth(uv),1e-5);
    float crest=abs(frac(uv.y-seed*.4-time*.125/size)-.5);
    float width=.6/size;
    float coverage=1-smoothstep(width,width+fw.y,crest);
    float end=smoothstep(.12,.2,frac(uv.x))*(1-smoothstep(.6,.8,frac(uv.x)));
    return coverage*end*step(.36,seed)*(1-smoothstep(.12,.45,max(fw.x,fw.y)));
}
float4 colourAt(uint v,float seaDepth,float2 ray) {
    uint i=v&255;float4 c=pal(i);uint effects=uint(settings.x);
    if((effects&8)==0)return c;
    float depth=abs(f16tof32(v>>16))*8;
    // Interpolate adjacent original DAC entries, centred on each former band.
    if((v&0x600)!=0 && depth==0) {
        float f=float((v>>12)&15)/15-.5;
        uint lo=i&15;
        int step=f<0?-1:1;
        if((lo>0||step>0)&&(lo<15||step<0))c=lerp(c,pal(i+step),abs(f));
    } else if(i>=0xA0 && !water(v) && depth>0) {
        float index=max(float(i&0xF0),float(i)-min(15,depth/1400));
        c=lerp(pal(uint(index)),pal(min(uint(index)+1,(i&0xF0)+15)),frac(index));
    }
    if(depth>0 && !water(v)) {
        float haze=(1-exp(-depth/24000))*.62;
        // The same horizon colours as the source game's sky and ground.
        float4 horizon=lerp(pal(0x80),pal(0x70),step(0,seaDepth));
        c=lerp(c,horizon,haze);
        if(settings.z>1 && (v&0x80000000)==0) {
            float units=exp2(float((v>>12)&15)-8);
            float farDepth=max(1,viewX.w/units);
            float d=depth*(1+.25*abs(ray.x)+.1875*abs(ray.y));
            c=lerp(c,horizon,smoothstep(.68,1,d/farDepth));
        }
    }
    return c;
}
float4 present(float4 screen) {
    float2 ray=float2(lerp(fov.x,fov.y,screen.x/source.x),lerp(fov.z,fov.w,screen.y/source.y));
    float3 cameraRay=float3(ray,-1);
    float denominator=dot(sea.xyz,cameraRay);
    float z=-sea.w/(abs(denominator)<1e-5?(denominator<0?-1e-5:1e-5):denominator);
    float3 position=cameraRay*clamp(z,0,100000);
    float2 relative=float2(dot(viewX.xyz,position),dot(viewZ.xyz,position));
    float2 world=relative+origin.xy;
    // Evaluate derivatives uniformly, including horizon/coverage pixels.
    float fine=wavelet(world,16,origin.z),coarse=wavelet(world,64,origin.z);
    float footprint=max(length(ddx(relative)),length(ddy(relative)));
    float waterFade=(1-smoothstep(1800,6500,z))*(1-smoothstep(8,28,footprint))*step(0,z);
    float4 c=0;float waterCoverage=0,groundCoverage=0,lightCoverage=0;
    [unroll]for(int k=0;k<WORLD_SAMPLES;k++) {
        uint v=loadIndex(int2(screen.xy),k);
        c+=colourAt(v,z,ray);
        waterCoverage+=water(v)?1:0;groundCoverage+=(v&0x200)?1:0;lightCoverage+=(v&0x800)?1:0;
    }
    c/=WORLD_SAMPLES;waterCoverage/=WORLD_SAMPLES;groundCoverage/=WORLD_SAMPLES;
    uint effects=uint(settings.x),centre=loadIndex(int2(screen.xy),0);
    if((effects&1)!=0)c.rgb+=c.rgb*(fine*.22+coarse*.12)*waterCoverage*waterFade;
    if((effects&2)!=0 && waterCoverage>0) {
        // Coast only: neighbouring source terrain bands, not cockpit, ships or sky.
        float edge=0;
        [unroll]for(int j=0;j<4;j++) {
            int2 offset=j==0?int2(2,0):j==1?int2(-2,0):j==2?int2(0,2):int2(0,-2);
            edge+=land(loadIndex(int2(screen.xy)+offset,0))?1:0;
        }
        c.rgb+=c.rgb*.24*saturate(edge)*waterCoverage*waterFade;
    }
    if((effects&4)!=0 && waterCoverage>0) {
        float wake=0;
        [loop]for(int j=0;j<int(settings.y);j++) {
            if(shipshape[j].z<.5)continue;
            float2 delta=relative-ships[j].xy,forward=ships[j].zw;
            float behind=-dot(delta,forward),side=abs(delta.x*forward.y-delta.y*forward.x);
            float width=2+footprint;
            float length=shipshape[j].x,beam=shipshape[j].y;
            float hull=beam*saturate((length-abs(behind))/max(beam,1));
            float waterline=(1-smoothstep(2,width+4,abs(side-hull-5)))*(1-smoothstep(length,length+8,abs(behind)))*.55;
            wake=max(wake,waterline);
        }
        c.rgb+=c.rgb*.5*wake*waterCoverage*waterFade;
    }
    if((effects&16)!=0) {
        uint ci=centre&255;
        if((ci==7||ci==15||ci==14||ci==31||ci==0x94)&&groundCoverage>0)c.rgb=saturate(c.rgb*1.16);
        // A small halo from actual source light/point primitives; never the HUD.
        float3 glow=0;
        [unroll]for(int j=0;j<4;j++) {
            int2 offset=j==0?int2(2,0):j==1?int2(-2,0):j==2?int2(0,2):int2(0,-2);
            uint neighbour=loadIndex(int2(screen.xy)+offset,0);
            if(neighbour&0x800)glow+=pal(neighbour&255).rgb*.035;
        }
        c.rgb=saturate(c.rgb+glow);
    }
    return float4(c.rgb,1);
}
float4 LookupMS(float4 p:SV_POSITION):SV_TARGET {return present(p);}
float4 Lookup(float4 p:SV_POSITION):SV_TARGET {return present(p);}
float4 Inspect(float4 p:SV_POSITION):SV_TARGET {
    float3 c=0;
    [unroll]for(int k=0;k<WORLD_SAMPLES;k++) {
        uint v=loadIndex(int2(p.xy),k);
        c+=float3(land(v)?1:0,water(v)?1:0,(v&0x800)?1:0);
    }
    return float4(c/WORLD_SAMPLES,1);
}
float4 InspectActor(float4 p:SV_POSITION):SV_TARGET {
    float c=0;[unroll]for(int k=0;k<WORLD_SAMPLES;k++)c+=(loadIndex(int2(p.xy),k)&0x80000000)?1:0;
    return float4(c/WORLD_SAMPLES,c/WORLD_SAMPLES,c/WORLD_SAMPLES,1);
}
)HLSL";
