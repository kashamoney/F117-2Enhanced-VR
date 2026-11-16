/* ENH: OpenXR + D3D11 seated prototype. OpenXR owns pose prediction, eye FOVs and
 * swapchains; saved-state game passes supply original geometry for GPU rasterisation.
 * The cockpit is deliberately simple geometry with live original instrument art.
 * No original game assets are embedded in this backend. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <SDL3/SDL.h>
#include "enhanced/vr.h"
#include "enhanced/vr_gpu.h"
#include "enhanced/vr_presentation.h"
#include "enhanced/vr_cockpit.h"
#include "enhanced/vr_canopy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace {
constexpr double pi=3.14159265358979323846;
void dx(HRESULT r,const char *what) {if(FAILED(r)) {char b[256];std::snprintf(b,sizeof b,"%s (HRESULT %08lx)",what,(unsigned long)r);throw std::runtime_error(b);}}
void xr(XrResult r,const char *what) {if(XR_FAILED(r)) {char b[256];std::snprintf(b,sizeof b,"%s (OpenXR %d)",what,int(r));throw std::runtime_error(b);}}
struct V {float p[4],uv[2],color[4];};
struct CanopyPlacement {const char *name;float y,z,noseScale;};
struct Constants {float source[4],fov[4],mode[4];};
struct Image {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    int w=0,h=0;
};
struct EyeSwap {
    XrSwapchain handle=XR_NULL_HANDLE;
    int w=0,h=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    std::vector<XrSwapchainImageD3D11KHR> images;
};
struct WorldTarget {
    ComPtr<ID3D11Texture2D> indices;
    ComPtr<ID3D11RenderTargetView> rtv,colourRT;
    ComPtr<ID3D11ShaderResourceView> srv;
    int w=0,h=0;
};
#include "enhanced/vr_world_shader.h"
const char *shader=R"HLSL(
cbuffer Params : register(b0) {float4 source;float4 fov;float4 mode;};
Texture2D image : register(t0); SamplerState imageSampler : register(s0);
struct I {float4 p:POSITION;float2 uv:TEXCOORD0;float4 c:COLOR0;};
struct O {float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 c:COLOR0;};
O VS(I i) {O o;o.p=i.p;o.uv=i.uv;o.c=i.c;return o;}
float4 PS(O i):SV_TARGET {
    if(mode.x<0.5) return i.c;
    float2 uv=i.uv;
    if(mode.x>2.5) {
        float2 ray=float2(lerp(fov.x,fov.y,uv.x),lerp(fov.z,fov.w,uv.y));
        uv=source.xy+float2(ray.x,-ray.y)*source.zw;
        if(any(uv<0)||any(uv>1)) return float4(0,0,0,1);
    }
    float4 texel=image.Sample(imageSampler,uv);
    float3 c=texel.rgb;
    if(mode.x>1.5 && mode.x<2.5) {
        if(any(uv<0)||any(uv>1)||texel.a<=0) discard;
        // Transparent source pixels are black: filtering produces premultiplied
        // coverage. Preserve it instead of toggling edges at alpha == 0.5.
        if(mode.y<0.5) c=pow(max(c/texel.a,0),1.0/2.2)*texel.a;
        return float4(c,texel.a);
    }
    if(mode.y<0.5) c=pow(max(c,0),1.0/2.2); // UNORM fallback target; sRGB targets encode automatically
    return float4(c,1);
}
)HLSL";

class Backend {
public:
    XrInstance instance=XR_NULL_HANDLE;
    XrSystemId system=XR_NULL_SYSTEM_ID;
    XrSession session=XR_NULL_HANDLE;
    XrSpace local=XR_NULL_HANDLE,seat=XR_NULL_HANDLE,head=XR_NULL_HANDLE;
    bool running=false,exit=false,recenter=true,preview=false,in_tick=false,dumped=false;
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    std::array<EyeSwap,2> swaps;
    std::array<XrViewConfigurationView,2> config;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> mirror;
    ComPtr<ID3D11RenderTargetView> mirrorRT;
    ComPtr<ID3D11DepthStencilView> mirrorDepth;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb,cb;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11DepthStencilState> glassDepth;
    ComPtr<ID3D11BlendState> glassBlend;
    ComPtr<ID3D11BlendState> hudBlend;
    size_t vertexCapacity=4096;
    double eyeRotation[9]={1,0,0,0,1,0,0,0,1};
    Image world[2],panel;
    Image gpuPalette;
    WorldTarget worldTarget[2];
    VrGpuFrame geometry[2]{};
    ComPtr<ID3D11VertexShader> worldVS;
    ComPtr<ID3D11PixelShader> worldPS,lookupPS,inspectPS,actorPS;
    ComPtr<ID3D11InputLayout> worldLayout;
    ComPtr<ID3D11Buffer> worldVB,presentationCB;
    DXGI_FORMAT worldFormat=vr_options.presentation?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R8_UINT;
    std::array<ComPtr<ID3D11BlendState>,3> logic;
    size_t worldCapacity=0,uploadBytes=0;
    unsigned samples=1;
    bool gpuWorld=vr_use_gpu();
    std::vector<uint32_t> pixels[2];
    std::vector<uint32_t> panelPixels;
    Image hud;
    std::vector<uint32_t> hudPixels;
    std::vector<V> vertices;
    VrPose previewHead{{0,0,0,1},{0,0,0}};
    double previewYaw=0,previewPitch=0;
    Uint64 lastFrame=0,lastStats=0;
    unsigned frames=0;
    double frameMS=0;
    bool benchmark=SDL_getenv("F117_VR_BENCHMARK")!=nullptr;
    Uint64 testDeadline=0,inputStarted=0;
    Uint64 nextService=0,displayPeriod=SDL_NS_PER_SECOND/90;
    unsigned inputEvents=0;
    unsigned benchFrames=0;
    unsigned benchTarget=SDL_getenv("F117_VR_BENCH_FRAMES")?std::clamp(SDL_atoi(SDL_getenv("F117_VR_BENCH_FRAMES")),1,900):30;
    unsigned benchWarmup=SDL_getenv("F117_VR_BENCH_WARMUP")?std::clamp(SDL_atoi(SDL_getenv("F117_VR_BENCH_WARMUP")),0,900):5;
    Uint64 benchStarted=0;
    double benchMS=0;
    size_t benchBytes=0;
    double benchPanel=0,benchHUD=0,benchGeometry=0,benchDraw=0,benchPresent=0;
    bool panelDirty=true,panelFlight=false;
    bool targetSRGB=true;
    FILE *log=nullptr;

    Backend() {log=std::fopen("f117-vr.log","w");}
    void note(const char *s) {std::fprintf(stderr,"[VR] %s\n",s);if(log){std::fprintf(log,"%s\n",s);std::fflush(log);}}
    ~Backend() {
        if(capture){try{dumpLiveCapture();}catch(const std::exception &e){note(e.what());}}
        if(timing)std::fclose(timing);
        for(auto &f:geometry)vr_gpu_free(&f);
        if(context) context->ClearState();
        for(auto &s:swaps) if(s.handle) xrDestroySwapchain(s.handle);
        if(seat) xrDestroySpace(seat);
        if(head) xrDestroySpace(head);
        if(local) xrDestroySpace(local);
        if(session) xrDestroySession(session);
        if(instance) xrDestroyInstance(instance);
        if(log) std::fclose(log);
    }
    void createInstance() {
        uint32_t n=0;xr(xrEnumerateInstanceExtensionProperties(nullptr,0,&n,nullptr),"enumerate extensions");
        std::vector<XrExtensionProperties> exts(n,{XR_TYPE_EXTENSION_PROPERTIES});
        xr(xrEnumerateInstanceExtensionProperties(nullptr,n,&n,exts.data()),"read extensions");
        bool d3d=false;for(auto &e:exts) if(!std::strcmp(e.extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) d3d=true;
        if(!d3d) throw std::runtime_error("Active OpenXR runtime does not support Direct3D 11");
        const char *extensions[]={XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        std::strcpy(ci.applicationInfo.applicationName,"F-117A Enhanced VR");
        std::strcpy(ci.applicationInfo.engineName,"F117 SDL3 prototype");
        ci.applicationInfo.applicationVersion=1;
        ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        ci.enabledExtensionCount=1;ci.enabledExtensionNames=extensions;
        xr(xrCreateInstance(&ci,&instance),"create OpenXR instance (check SteamVR runtime)");
        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
        xr(xrGetInstanceProperties(instance,&props),"runtime properties");note(props.runtimeName);
        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        xr(xrGetSystem(instance,&si,&system),"find headset (start SteamVR and connect Odyssey+)");
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};xr(xrGetSystemProperties(instance,system,&sp),"headset properties");note(sp.systemName);
        uint32_t count=0;
        xr(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"stereo view count");
        if(count!=2) throw std::runtime_error("Prototype requires a two-eye stereo view configuration");
        for(auto &v:config) v={XR_TYPE_VIEW_CONFIGURATION_VIEW};
        xr(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,config.data()),"eye dimensions");
        for(int i=0;i<2;i++) {char b[120];std::snprintf(b,sizeof b,"Eye %d recommended target: %ux%u",i,config[i].recommendedImageRectWidth,config[i].recommendedImageRectHeight);note(b);}
    }
    void createDevice() {
        ComPtr<IDXGIAdapter1> adapter;
        D3D_FEATURE_LEVEL minimum=D3D_FEATURE_LEVEL_11_0;
        if(!preview) {
            PFN_xrGetD3D11GraphicsRequirementsKHR requirements=nullptr;
            xr(xrGetInstanceProcAddr(instance,"xrGetD3D11GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements)),"graphics requirements entry point");
            XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
            xr(requirements(instance,system,&req),"graphics requirements");minimum=req.minFeatureLevel;
            ComPtr<IDXGIFactory1> factory;dx(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
            for(UINT i=0;;i++) {
                ComPtr<IDXGIAdapter1> candidate;
                if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc;candidate->GetDesc1(&desc);
                if(!std::memcmp(&desc.AdapterLuid,&req.adapterLuid,sizeof(LUID))) {adapter=candidate;break;}
            }
            if(!adapter) throw std::runtime_error("Could not find headset GPU by OpenXR adapter LUID");
        }
        D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL obtained;
        dx(D3D11CreateDevice(adapter.Get(),adapter?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&device,&obtained,&context),"create D3D11 device");
        if(obtained<minimum) throw std::runtime_error("GPU feature level is below the headset requirement");
        ComPtr<ID3DBlob> v,p,errors;
        dx(D3DCompile(shader,std::strlen(shader),"vr",nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&errors),"compile VR vertex shader");
        errors.Reset();
        dx(D3DCompile(shader,std::strlen(shader),"vr",nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&errors),"compile VR pixel shader");
        dx(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs),"vertex shader");
        dx(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps),"pixel shader");
        D3D11_INPUT_ELEMENT_DESC inputs[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
        dx(device->CreateInputLayout(inputs,3,v->GetBufferPointer(),v->GetBufferSize(),&layout),"input layout");
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(V)*4096;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        dx(device->CreateBuffer(&bd,nullptr,&vb),"vertex buffer");
        bd.ByteWidth=sizeof(Constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;dx(device->CreateBuffer(&bd,nullptr,&cb),"constant buffer");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        dx(device->CreateSamplerState(&sd,&sampler),"sampler");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        dx(device->CreateRasterizerState(&rd,&raster),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        dx(device->CreateDepthStencilState(&dd,&depthState),"depth state");
        dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        dx(device->CreateDepthStencilState(&dd,&glassDepth),"canopy glass depth");
        D3D11_BLEND_DESC blend{};auto &b=blend.RenderTarget[0];b.BlendEnable=TRUE;
        b.SrcBlend=D3D11_BLEND_SRC_ALPHA;b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;b.BlendOp=D3D11_BLEND_OP_ADD;
        b.SrcBlendAlpha=D3D11_BLEND_ONE;b.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;b.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        dx(device->CreateBlendState(&blend,&glassBlend),"canopy glass blend");
        b.SrcBlend=D3D11_BLEND_ONE;
        dx(device->CreateBlendState(&blend,&hudBlend),"premultiplied HUD coverage");
        if(gpuWorld)createWorldPipeline();
    }
    void createWorldPipeline() {
        ComPtr<ID3D11Device1> d1;dx(device.As(&d1),"D3D11.1 world logic operations");
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        dx(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof options),"world capabilities");
        if(!options.OutputMergerLogicOp)throw std::runtime_error("GPU lacks palette logic operations; use --vr-software");
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{worldFormat,0};
        dx(device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2,&support,sizeof support),"world integer format support");
        if(!(support.OutFormatSupport2&D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP))
            throw std::runtime_error("GPU lacks logic operations for the presentation format; use --vr-no-presentation");
        // Diagnostic cap lets the same frozen scene reproduce lower coverage.
        unsigned cap=SDL_getenv("F117_VR_SAMPLES")?unsigned(std::clamp(SDL_atoi(SDL_getenv("F117_VR_SAMPLES")),1,8)):8;
        for(unsigned candidate : {8u,4u,2u}) {
            if(candidate>cap)continue;
            UINT quality=0;
            if(SUCCEEDED(device->CheckMultisampleQualityLevels(worldFormat,candidate,&quality))&&quality){samples=candidate;break;}
        }
        for(unsigned i=0;i<3;i++) {
            D3D11_BLEND_DESC1 b{};b.RenderTarget[0].LogicOpEnable=TRUE;
            b.RenderTarget[0].LogicOp=i==1?D3D11_LOGIC_OP_AND:i==2?D3D11_LOGIC_OP_OR:D3D11_LOGIC_OP_COPY;
            b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            ComPtr<ID3D11BlendState1> state;dx(d1->CreateBlendState1(&b,&state),"palette logic blend");dx(state.As(&logic[i]),"world blend state");
        }
        ComPtr<ID3DBlob> v,p,l,error;
        const std::string sampleCount=std::to_string(samples);
        const D3D_SHADER_MACRO macros[]={{"WORLD_SAMPLES",sampleCount.c_str()},{"WORLD_MULTISAMPLE",samples>1?"1":"0"},{nullptr,nullptr}};
        auto compile=[&](const char *entry,const char *profile,ComPtr<ID3DBlob> &blob){
            error.Reset();HRESULT result=D3DCompile(worldShader,std::strlen(worldShader),"world",macros,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
            if(FAILED(result)&&error)note(static_cast<const char*>(error->GetBufferPointer()));
            dx(result,entry);
        };
        compile("WV","vs_5_0",v);compile("WP","ps_5_0",p);compile(samples>1?"LookupMS":"Lookup","ps_5_0",l);
        dx(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&worldVS),"world VS");
        dx(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&worldPS),"world PS");
        dx(device->CreatePixelShader(l->GetBufferPointer(),l->GetBufferSize(),nullptr,&lookupPS),"palette PS");
        compile("Inspect","ps_5_0",l);
        dx(device->CreatePixelShader(l->GetBufferPointer(),l->GetBufferSize(),nullptr,&inspectPS),"surface inspection PS");
        compile("InspectActor","ps_5_0",l);
        dx(device->CreatePixelShader(l->GetBufferPointer(),l->GetBufferSize(),nullptr,&actorPS),"actor coverage PS");
        D3D11_INPUT_ELEMENT_DESC in[]={
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32_UINT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",1,DXGI_FORMAT_R32_UINT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0}};
        dx(device->CreateInputLayout(in,5,v->GetBufferPointer(),v->GetBufferSize(),&worldLayout),"world input");
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(VrGpuPresentation);bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        dx(device->CreateBuffer(&bd,nullptr,&presentationCB),"presentation constants");
    }
    void renderWorld(int index,const VrFov &f,int w,int h,bool backgroundOnly=false,unsigned inspection=0) {
        auto &t=worldTarget[index];auto &image=world[index];auto &g=geometry[index];
        if(t.w!=w||t.h!=h) {
            t=WorldTarget{};image=Image{};
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=worldFormat;
            d.SampleDesc.Count=samples;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            dx(device->CreateTexture2D(&d,nullptr,&t.indices),"world index image");
            dx(device->CreateRenderTargetView(t.indices.Get(),nullptr,&t.rtv),"world index target");
            dx(device->CreateShaderResourceView(t.indices.Get(),nullptr,&t.srv),"world index view");
            d.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;d.SampleDesc.Count=1;
            dx(device->CreateTexture2D(&d,nullptr,&image.texture),"world colour image");
            dx(device->CreateRenderTargetView(image.texture.Get(),nullptr,&t.colourRT),"world colour target");
            dx(device->CreateShaderResourceView(image.texture.Get(),nullptr,&image.srv),"world colour view");
            t.w=image.w=w;t.h=image.h=h;
        }
        const size_t bytes=g.count*sizeof(VrGpuVertex);
        if(bytes>worldCapacity) {
            worldVB.Reset();worldCapacity=bytes*2+65536;
            D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(worldCapacity);d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            dx(device->CreateBuffer(&d,nullptr,&worldVB),"world vertex buffer");
        }
        D3D11_MAPPED_SUBRESOURCE mapped;
        dx(context->Map(worldVB.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"world vertices");
        std::memcpy(mapped.pData,g.vertices,bytes);context->Unmap(worldVB.Get(),0);uploadBytes+=bytes;
        ID3D11RenderTargetView *rt=t.rtv.Get();context->OMSetRenderTargets(1,&rt,nullptr);
        context->OMSetDepthStencilState(nullptr,0);context->RSSetState(raster.Get());
        const float clear[4]={0,0,0,0};context->ClearRenderTargetView(rt,clear);
        D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&vp);
        Constants c{};c.fov[0]=float(std::tan(f.left));c.fov[1]=float(std::tan(f.right));c.fov[2]=float(std::tan(f.up));c.fov[3]=float(std::tan(f.down));
        c.source[0]=float(w);c.source[1]=float(h);c.mode[2]=g.fine?1.0f:0.0f;
        dx(context->Map(cb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"world constants");std::memcpy(mapped.pData,&c,sizeof c);context->Unmap(cb.Get(),0);
        ID3D11Buffer *constants=cb.Get();context->VSSetConstantBuffers(0,1,&constants);context->PSSetConstantBuffers(0,1,&constants);
        g.presentation.params[3]=vr_options.presentation?1.0f:0.0f;
        dx(context->Map(presentationCB.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"presentation constants");
        std::memcpy(mapped.pData,&g.presentation,sizeof g.presentation);context->Unmap(presentationCB.Get(),0);
        ID3D11Buffer *presentation=presentationCB.Get();context->PSSetConstantBuffers(1,1,&presentation);
        UINT stride=sizeof(VrGpuVertex),offset=0;ID3D11Buffer *buffer=worldVB.Get();context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->IASetInputLayout(worldLayout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(worldVS.Get(),nullptr,0);context->PSSetShader(worldPS.Get(),nullptr,0);
        if(backgroundOnly){context->OMSetBlendState(logic[0].Get(),nullptr,~0u);context->Draw(UINT(g.background_count),0);}
        else for(size_t i=0;i<g.batch_count;i++){auto &b=g.batches[i];context->OMSetBlendState(logic[b.op].Get(),nullptr,~0u);
            if(inspection==3){for(unsigned j=0;j<b.count;j+=3)if(g.vertices[b.first+j].flags&VR_SURFACE_AIRCRAFT)context->Draw(3,b.first+j);}
            else context->Draw(b.count,b.first);}
        context->OMSetBlendState(nullptr,nullptr,~0u);
        for(auto &colour:g.palette)colour|=0xFF000000;
        upload(gpuPalette,g.palette,256,1);
        rt=t.colourRT.Get();context->OMSetRenderTargets(1,&rt,nullptr);
        // The existing full-screen layout/VS only provides clip positions here.
        fullscreen();draw(0,f,nullptr);
        // draw() sets cockpit constants; restore the world pixel dimensions/FOV.
        dx(context->Map(cb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"lookup constants");
        std::memcpy(mapped.pData,&c,sizeof c);context->Unmap(cb.Get(),0);
        ID3D11ShaderResourceView *views[3]={samples>1?t.srv.Get():nullptr,gpuPalette.srv.Get(),samples==1?t.srv.Get():nullptr};
        context->PSSetShaderResources(0,3,views);context->PSSetShader(inspection>=2?actorPS.Get():inspection?inspectPS.Get():lookupPS.Get(),nullptr,0);context->Draw(6,0);
        ID3D11ShaderResourceView *none[3]={nullptr,nullptr,nullptr};context->PSSetShaderResources(0,3,none);
        rt=nullptr;context->OMSetRenderTargets(1,&rt,nullptr);
    }
    void createMirror(HWND hwnd) {
        if(!hwnd) return;
        ComPtr<IDXGIDevice> d;dx(device.As(&d),"DXGI device");
        ComPtr<IDXGIAdapter> adapter;dx(d->GetAdapter(&adapter),"mirror adapter");
        ComPtr<IDXGIFactory> factory;dx(adapter->GetParent(IID_PPV_ARGS(&factory)),"mirror factory");
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=1024;desc.BufferDesc.Height=1024;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2;desc.OutputWindow=hwnd;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        dx(factory->CreateSwapChain(device.Get(),&desc,&mirror),"stereo mirror swapchain");
        factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
        ComPtr<ID3D11Texture2D> back;dx(mirror->GetBuffer(0,IID_PPV_ARGS(&back)),"mirror backbuffer");
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        dx(device->CreateRenderTargetView(back.Get(),&rtv,&mirrorRT),"mirror target");
        mirrorDepth=createDepth(desc.BufferDesc.Width,desc.BufferDesc.Height);
    }
    ComPtr<ID3D11DepthStencilView> createDepth(int w,int h) {
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_D32_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> tex;ComPtr<ID3D11DepthStencilView> view;
        dx(device->CreateTexture2D(&d,nullptr,&tex),"depth image");dx(device->CreateDepthStencilView(tex.Get(),nullptr,&view),"depth view");return view;
    }
    void createSession() {
        XrGraphicsBindingD3D11KHR graphics{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};graphics.device=device.Get();
        XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};ci.next=&graphics;ci.systemId=system;
        xr(xrCreateSession(instance,&ci,&session),"create headset session");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;space.poseInReferenceSpace.orientation.w=1;
        xr(xrCreateReferenceSpace(session,&space,&local),"local tracking space");
        xr(xrCreateReferenceSpace(session,&space,&seat),"seated tracking space");
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;xr(xrCreateReferenceSpace(session,&space,&head),"head tracking space");
        uint32_t count=0;xr(xrEnumerateSwapchainFormats(session,0,&count,nullptr),"swapchain formats");
        std::vector<int64_t> formats(count);xr(xrEnumerateSwapchainFormats(session,count,&count,formats.data()),"swapchain formats");
        int64_t format=0;
        for(int64_t candidate:{int64_t(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),int64_t(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),int64_t(DXGI_FORMAT_R8G8B8A8_UNORM),int64_t(DXGI_FORMAT_B8G8R8A8_UNORM)})
            if(std::find(formats.begin(),formats.end(),candidate)!=formats.end()) {format=candidate;break;}
        if(!format) throw std::runtime_error("Runtime has no supported 8-bit swapchain format");
        for(int i=0;i<2;i++) {
            auto &s=swaps[i];s.w=config[i].recommendedImageRectWidth;s.h=config[i].recommendedImageRectHeight;s.format=static_cast<DXGI_FORMAT>(format);
            if(gpuWorld&&vr_options.resolution) {s.w=int(std::lround(double(s.w)*vr_options.resolution/s.h));s.h=vr_options.resolution;}
            char message[160];std::snprintf(message,sizeof message,"Eye %d world target: %dx%d, %ux MSAA",i,s.w,s.h,gpuWorld?samples:1);note(message);
            XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};sc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            sc.format=format;sc.sampleCount=1;sc.width=s.w;sc.height=s.h;sc.faceCount=1;sc.arraySize=1;sc.mipCount=1;
            xr(xrCreateSwapchain(session,&sc,&s.handle),"create eye swapchain");
            xr(xrEnumerateSwapchainImages(s.handle,0,&count,nullptr),"eye images");
            s.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            xr(xrEnumerateSwapchainImages(s.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(s.images.data())),"eye images");
        }
    }
    void init(HWND hwnd,bool p) {
        preview=p;if(!preview)createInstance();createDevice();createMirror(hwnd);if(!preview)createSession();
        if(SDL_getenv("F117_VR_TIMING")||SDL_getenv("F117_VR_CAPTURE_LIVE")) {
            timing=std::fopen("vr-timing.csv","w");
            if(!timing)throw std::runtime_error("Cannot open vr-timing.csv");
            std::setvbuf(timing,nullptr,_IOFBF,65536);
            std::fprintf(timing,"submission,eye,display_time,clock_ns,frame,alpha,x,y,heading,pitch,roll,capture,throttle,thrust,work_ms,panel_ms,hud_ms,geometry_ms\n");
        }
        char b[160];std::snprintf(b,sizeof b,"%s ready; %s world; F12 recenter, F11 cockpit",preview?"Desktop stereo preview":"OpenXR",gpuWorld?"GPU geometry at eye resolution":"software fallback");note(b);
        if(preview&&gpuWorld){std::snprintf(b,sizeof b,"Preview world target: %dx%d, %ux MSAA",vr_options.resolution?vr_options.resolution:1920,vr_options.resolution?vr_options.resolution:1920,samples);note(b);}
    }
    void upload(Image &i,const uint32_t *data,int w,int h) {
        uploadBytes+=size_t(w)*h*4;
        if(i.w!=w || i.h!=h) {
            i.srv.Reset();i.texture.Reset();
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            dx(device->CreateTexture2D(&d,nullptr,&i.texture),"upload image");
            dx(device->CreateShaderResourceView(i.texture.Get(),nullptr,&i.srv),"image view");i.w=w;i.h=h;
        }
        context->UpdateSubresource(i.texture.Get(),0,nullptr,data,w*4,0);
    }
    void requestLiveCapture() {
        if(captureCount){note("Live burst already recorded; close the game to save it");return;}
        captureArmed=true;note("Recording 180 live stereo frames; close the game to save them");
    }
    void draw(int mode,const VrFov &fov,Image *image,int n=1) {
        if(vertices.empty())return;
        if(vertices.size()>vertexCapacity) {
            if(vertices.size()>65536)throw std::runtime_error("VR cockpit geometry buffer overflow");
            vertexCapacity=vertices.size()*2;vb.Reset();
            D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(vertexCapacity*sizeof(V));d.Usage=D3D11_USAGE_DYNAMIC;
            d.BindFlags=D3D11_BIND_VERTEX_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            dx(device->CreateBuffer(&d,nullptr,&vb),"cockpit vertex buffer");
        }
        D3D11_MAPPED_SUBRESOURCE map{};
        dx(context->Map(vb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map),"map vertices");std::memcpy(map.pData,vertices.data(),vertices.size()*sizeof(V));context->Unmap(vb.Get(),0);
        uploadBytes+=vertices.size()*sizeof(V);
        Constants c{};c.mode[0]=float(mode);c.mode[1]=targetSRGB?1.0f:0.0f;
        if(image) {c.source[0]=(image->w-320*n)/2.0f/image->w+159.0f*n/image->w;c.source[1]=0.5f;c.source[2]=256.0f*n/image->w;c.source[3]=192.0f*n/image->h;}
        c.fov[0]=float(std::tan(fov.left));c.fov[1]=float(std::tan(fov.right));c.fov[2]=float(std::tan(fov.up));c.fov[3]=float(std::tan(fov.down));
        dx(context->Map(cb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map),"map constants");std::memcpy(map.pData,&c,sizeof c);context->Unmap(cb.Get(),0);
        UINT stride=sizeof(V),offset=0;ID3D11Buffer *buffer=vb.Get();context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
        ID3D11Buffer *constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);
        ID3D11ShaderResourceView *view=image?image->srv.Get():nullptr;context->PSSetShaderResources(0,1,&view);
        ID3D11SamplerState *s=sampler.Get();context->PSSetSamplers(0,1,&s);
        context->Draw(UINT(vertices.size()),0);
        view=nullptr;context->PSSetShaderResources(0,1,&view);vertices.clear();
    }
    V vertex(const VrPose &eye,const VrFov &f,double x,double y,double z,float u,float v,const std::array<float,4>&color) {
        double delta[3]={x-eye.position[0],y-eye.position[1],z-eye.position[2]},p[3]={};
        for(int r=0;r<3;r++)for(int k=0;k<3;k++)p[r]+=eyeRotation[3*k+r]*delta[k];
        double l=std::tan(f.left),r=std::tan(f.right),up=std::tan(f.up),down=std::tan(f.down),nearZ=0.03,farZ=1000;
        return V{{float(2*p[0]/(r-l)+(r+l)*p[2]/(r-l)),float(2*p[1]/(up-down)+(up+down)*p[2]/(up-down)),
            float(-farZ*p[2]/(farZ-nearZ)-farZ*nearZ/(farZ-nearZ)),float(-p[2])},{u,v},{color[0],color[1],color[2],color[3]}};
    }
    void quad(const VrPose &eye,const VrFov &f,const std::array<std::array<double,3>,4>&p,
        const std::array<float,4>&color,float u0=0,float v0=0,float u1=1,float v1=1) {
        V q[]={vertex(eye,f,p[0][0],p[0][1],p[0][2],u0,v0,color),vertex(eye,f,p[1][0],p[1][1],p[1][2],u1,v0,color),
               vertex(eye,f,p[2][0],p[2][1],p[2][2],u1,v1,color),vertex(eye,f,p[3][0],p[3][1],p[3][2],u0,v1,color)};
        for(int i:{0,1,2,0,2,3})vertices.push_back(q[i]);
    }
    void box(const VrPose &eye,const VrFov &f,double x0,double y0,double z0,double x1,double y1,double z1,const std::array<float,4>&c) {
        quad(eye,f,{{{x0,y1,z0},{x1,y1,z0},{x1,y0,z0},{x0,y0,z0}}},c);
        quad(eye,f,{{{x0,y1,z1},{x1,y1,z1},{x1,y0,z1},{x0,y0,z1}}},c);
        quad(eye,f,{{{x0,y1,z0},{x0,y1,z1},{x0,y0,z1},{x0,y0,z0}}},c);
        quad(eye,f,{{{x1,y1,z0},{x1,y1,z1},{x1,y0,z1},{x1,y0,z0}}},c);
        quad(eye,f,{{{x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}}},c);
        quad(eye,f,{{{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}}},c);
    }
    void fullscreen() {
        V q[]={{{-1,1,0.99999f,1},{0,0},{1,1,1,1}},{{1,1,0.99999f,1},{1,0},{1,1,1,1}},{{1,-1,0.99999f,1},{1,1},{1,1,1,1}},{{-1,-1,0.99999f,1},{0,1},{1,1,1,1}}};
        for(int i:{0,1,2,0,2,3})vertices.push_back(q[i]);
    }
    void cockpitMesh(const VrPose &eye,const VrFov &f,const VrCockpitVertex *data,unsigned count,
                     const VrCockpitVertex *omit=nullptr,unsigned omitCount=0) {
        vertices.reserve(count);
        for(unsigned i=0;i<count;i++) {
            if(omit&&data+i>=omit&&data+i<omit+omitCount)continue;
            const auto &v=data[i];
            vertices.push_back(vertex(eye,f,v.p[0],v.p[1],v.p[2],0,0,{v.colour[0],v.colour[1],v.colour[2],v.colour[3]}));
        }
        draw(0,f,nullptr);
    }
    void inspectionMesh(const VrPose &eye,const VrFov &f,const VrCockpitVertex *data,unsigned count,
                        const std::array<float,4>&colour,bool shade=true,const CanopyPlacement *placement=nullptr) {
        for(unsigned i=0;i<count;i+=3){
            float points[3][3];
            for(int j=0;j<3;j++){
                std::copy(data[i+j].p,data[i+j].p+3,points[j]);
                if(placement){
                    // Compress only the nose ahead of the fitted crown plane;
                    // rear-seat clearance is a separate constraint.
                    if(points[j][2]<-.4f)points[j][2]=-.4f+(points[j][2]+.4f)*placement->noseScale;
                    points[j][1]+=placement->y;points[j][2]+=placement->z;
                }
            }
            const auto *a=points[0],*b=points[1],*c=points[2];
            float u[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]},v[3]={c[0]-a[0],c[1]-a[1],c[2]-a[2]};
            float n[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
            float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            float light=shade&&length>0?.65f+.35f*std::fabs((-.35f*n[0]+.82f*n[1]-.45f*n[2])/length):1;
            for(int j=0;j<3;j++)vertices.push_back(vertex(eye,f,points[j][0],points[j][1],points[j][2],0,0,
                {colour[0]*light,colour[1]*light,colour[2]*light,colour[3]}));
        }
        draw(0,f,nullptr);
    }
    void inspectionCockpit(const VrPose &eye,const VrFov &f,int stage,bool ids=false,const CanopyPlacement *placement=nullptr,bool pilotFit=false) {
        vr_matrix(eye.orientation,eyeRotation);
        const auto *candidate=pilotFit?vr_canopy_pilot_mesh():vr_canopy_mesh(),*full=vr_cockpit_mesh();
        const auto *old=vr_cockpit_canopy_mesh(),*hardware=vr_cockpit_hud_mesh();
        inspectionMesh(eye,f,candidate->opaque,candidate->opaque_count,ids?std::array<float,4>{1,0,0,1}:std::array<float,4>{.4f,.42f,.44f,1},!ids,placement);
        if(stage>0){
            // The flight mesh retains its textures in normal play. Here all
            // instrument faces are deliberately plain for obstruction diagnosis.
            for(unsigned i=0;i<full->opaque_count;i+=3){
                const auto *v=full->opaque+i;
                if((v>=old->opaque&&v<old->opaque+old->opaque_count)||
                   (v>=hardware->opaque&&v<hardware->opaque+hardware->opaque_count))continue;
                inspectionMesh(eye,f,v,3,ids?std::array<float,4>{0,1,0,1}:std::array<float,4>{.36f,.38f,.40f,1},!ids);
            }
        }
        if(stage>1)inspectionMesh(eye,f,hardware->opaque,hardware->opaque_count,
            ids?std::array<float,4>{0,0,1,1}:std::array<float,4>{.22f,.24f,.26f,1},!ids);
        if(ids)return;
        context->OMSetBlendState(glassBlend.Get(),nullptr,~0u);context->OMSetDepthStencilState(glassDepth.Get(),0);
        inspectionMesh(eye,f,candidate->glass,candidate->glass_count,{.06f,.20f,.28f,.035f},false,placement);
        if(stage>1)cockpitMesh(eye,f,hardware->glass,hardware->glass_count);
        context->OMSetBlendState(nullptr,nullptr,~0u);
        if(stage>2)hudImage(eye,f);
        context->OMSetDepthStencilState(depthState.Get(),0);
    }
    void hudImage(const VrPose &eye,const VrFov &f) {
        const std::array<float,4> white={1,1,1,1};
        const auto *aperture=vr_cockpit_hud_aperture();
        // The combiner is the viewing aperture. Per-eye UVs project aircraft-
        // fixed directions through it, removing the old one-metre HUD parallax.
        // The projected image is visible only from the pilot side of the glass.
        if(eye.position[2]>aperture[0][2]+.03){
            V corners[VR_HUD_CORNERS];
            for(int i=0;i<VR_HUD_CORNERS;i++){
                float uv[2];vr_cockpit_hud_uv(aperture[i],eye.position,uv);
                corners[i]=vertex(eye,f,aperture[i][0],aperture[i][1],aperture[i][2],uv[0],uv[1],white);
            }
            for(int i=1;i+1<VR_HUD_CORNERS;i++)for(int j:{0,i,i+1})vertices.push_back(corners[j]);
            context->OMSetBlendState(hudBlend.Get(),nullptr,~0u);draw(2,f,&hud);
            context->OMSetBlendState(nullptr,nullptr,~0u);
        }
    }
    void cockpit(const VrPose &eye,const VrFov &f,bool showHud=true) {
        vr_matrix(eye.orientation,eyeRotation);
        const auto *mesh=vr_cockpit_mesh();
        const auto *hardware=vr_cockpit_hud_mesh();
        cockpitMesh(eye,f,mesh->opaque,mesh->opaque_count,showHud?nullptr:hardware->opaque,hardware->opaque_count);
        const std::array<float,4> white={1,1,1,1};
        unsigned count=0;const auto *instruments=vr_cockpit_instruments(&count);
        for(unsigned i=0;i<count;i++){
            const auto &in=instruments[i];const auto &p=in.face;
            quad(eye,f,{{{p.x0,p.y1,p.z},{p.x1,p.y1,p.z},{p.x1,p.y0,p.z},{p.x0,p.y0,p.z}}},white,
                in.source[0]/320.0f,in.source[1]/200.0f,in.source[2]/320.0f,in.source[3]/200.0f);
        }
        draw(1,f,&panel);
        context->OMSetBlendState(glassBlend.Get(),nullptr,~0u);context->OMSetDepthStencilState(glassDepth.Get(),0);
        cockpitMesh(eye,f,mesh->glass,mesh->glass_count,showHud?nullptr:hardware->glass,hardware->glass_count);
        context->OMSetBlendState(nullptr,nullptr,~0u);
        if(showHud)hudImage(eye,f);
        context->OMSetDepthStencilState(depthState.Get(),0);
    }
    void render(int index,const VrPose &eye,const VrFov &f,ID3D11RenderTargetView *target,ID3D11DepthStencilView *depth,
                float x,float y,float w,float h,bool flight) {
        vr_matrix(eye.orientation,eyeRotation);
        context->OMSetRenderTargets(1,&target,depth);context->OMSetDepthStencilState(depthState.Get(),0);context->RSSetState(raster.Get());
        D3D11_VIEWPORT vp{x,y,w,h,0,1};context->RSSetViewports(1,&vp);
        if(flight) {fullscreen();draw(gpuWorld?1:3,f,&world[index],std::max(1,vr_canvas_resolution()/1024));}
        const std::array<float,4> white={1,1,1,1};
        if(!flight) {
            quad(eye,f,{{{-1.05,0.65,-1.7},{1.05,0.65,-1.7},{1.05,-0.66,-1.7},{-1.05,-0.66,-1.7}}},white);
            draw(1,f,&panel);return;
        }
        if(!vr_options.cockpit)return;
        cockpit(eye,f);
    }
    void poll() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while(xrPollEvent(instance,&event)==XR_SUCCESS) {
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto *e=reinterpret_cast<XrEventDataSessionStateChanged*>(&event);state=e->state;
                if(state==XR_SESSION_STATE_READY) {XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;xr(xrBeginSession(session,&begin),"begin headset session");running=true;}
                if(state==XR_SESSION_STATE_STOPPING) {if(running)xr(xrEndSession(session),"end headset session");running=false;}
                if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING) {exit=true;running=false;}
            } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)exit=true;
            else if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)recenter=true;
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
    }
    void center(XrTime time) {
        if(!recenter)return;
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};xr(xrLocateSpace(head,local,time,&loc),"locate seated head");
        const auto flags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if((loc.locationFlags&flags)!=flags)return;
        auto q=loc.pose.orientation;
        double m[9];vr_matrix({q.x,q.y,q.z,q.w},m);double yaw=std::atan2(m[2],m[8]);
        XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;ci.poseInReferenceSpace=loc.pose;
        ci.poseInReferenceSpace.orientation={0,float(std::sin(yaw/2)),0,float(std::cos(yaw/2))};
        XrSpace next=XR_NULL_HANDLE;xr(xrCreateReferenceSpace(session,&ci,&next),"recenter seat");xrDestroySpace(seat);seat=next;recenter=false;note("Seated origin recentered");
    }
    void dumpEyes(int size) {
        if(!vr_options.dump || dumped)return;
        for(int i=0;i<2;i++) {
            auto *s=SDL_CreateSurfaceFrom(size,size,SDL_PIXELFORMAT_XRGB8888,pixels[i].data(),size*4);
            if(s){SDL_SavePNG(s,i?"vr-right.png":"vr-left.png");SDL_DestroySurface(s);}
        }
        dumped=true;note("Wrote vr-left.png and vr-right.png (software world views)");
    }
    std::vector<uint32_t> readWorld(Image &im,const char *path) {
        D3D11_TEXTURE2D_DESC desc;im.texture->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;dx(device->CreateTexture2D(&desc,nullptr,&staging),"world readback image");
        context->CopyResource(staging.Get(),im.texture.Get());D3D11_MAPPED_SUBRESOURCE mapped;
        dx(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"world readback");
        std::vector<uint32_t> data(size_t(im.w)*im.h);
        for(int y=0;y<im.h;y++)std::memcpy(data.data()+size_t(y)*im.w,static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(im.w)*4);
        context->Unmap(staging.Get(),0);
        if(path) {
            auto *surface=SDL_CreateSurfaceFrom(im.w,im.h,SDL_PIXELFORMAT_XRGB8888,data.data(),im.w*4);
            if(surface){SDL_SavePNG(surface,path);SDL_DestroySurface(surface);}
        }
        return data;
    }
    void locateCoast(int w,int h) {
        // Locate an actual shoreline in original rendered geometry. Tile type 0
        // is not a reliable water classifier: fine tile levels can be empty.
        const int32_t baseX=int32_t(geometry[0].source_camera[0]),baseY=int32_t(geometry[0].source_camera[1]);
        const double unit=.5*(512000.0/1048576)*vr_options.world_scale,tan=std::tan(pi/3);
        VrPose down{{-std::sin(pi/4),0,0,std::cos(pi/4)},{0,0,0}};
        VrFov f{-pi/3,pi/3,pi/3,-pi/3};const int size=512;
        for(int ring=0;ring<=3;ring++)for(int oy=-ring;oy<=ring;oy++)for(int ox=-ring;ox<=ring;ox++) {
            if(std::max(std::abs(ox),std::abs(oy))!=ring)continue;
            int32_t cx=baseX+ox*131072,cy=baseY+oy*131072;
            vr_presentation_test_camera(cx,cy,32000,0);
            vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&down,nullptr,w,h);vr_gpu_end();
            if(!ok||geometry[0].invalid)continue;
            renderWorld(0,f,size,size,false,true);auto mask=readWorld(world[0],nullptr);
            int best=INT_MAX,bx=-1,by=0,lx=0,ly=0;
            const int dx[]={1,-1,0,0},dy[]={0,0,1,-1};
            for(int y=4;y<size-4;y++)for(int x=4;x<size-4;x++) {
                if(((mask[size*y+x]>>8)&255)<240)continue;
                for(int k=0;k<4;k++)if(((mask[size*(y+dy[k])+x+dx[k]]>>16)&255)>240) {
                    int score=(x-size/2)*(x-size/2)+(y-size/2)*(y-size/2);
                    if(score<best){best=score;bx=x;by=y;lx=x+dx[k];ly=y+dy[k];}
                }
            }
            if(bx<0)continue;
            auto p=geometry[0].presentation;
            auto ground=[&](int x,int y) {
                double ray[3]={((x+.5)/size*2-1)*tan,(1-(y+.5)/size*2)*tan,-1};
                double d=0;for(int k=0;k<3;k++)d+=p.sea[k]*ray[k];
                double z=-p.sea[3]/d;std::array<double,2> q{};
                for(int k=0;k<3;k++){q[0]+=p.view[0][k]*ray[k]*z;q[1]+=p.view[2][k]*ray[k]*z;}
                return q;
            };
            auto seaPoint=ground(bx,by),landPoint=ground(lx,ly);
            double dxm=landPoint[0]-seaPoint[0],dzm=landPoint[1]-seaPoint[1],length=std::hypot(dxm,dzm);
            dxm/=length;dzm/=length;
            int32_t x=cx+int32_t(std::lround((seaPoint[0]-128*dxm)/unit));
            int32_t y=cy-int32_t(std::lround((seaPoint[1]-128*dzm)/unit));
            int heading=int(std::lround(std::atan2(dxm,-dzm)*65536/(2*pi)));
            readWorld(world[0],"vr-coast-locator.png");
            vr_presentation_test_camera(x,y,200,heading);
            note("Located actual original-geometry shoreline for controlled presentation capture");return;
        }
        throw std::runtime_error("No rendered coast found for presentation fixture");
    }
    void dumpCockpitViews(int w,int h) {
        if(!vr_canopy_export("vr-canopy-geometry.json"))throw std::runtime_error("Cannot export inspection canopy");
        if(!vr_canopy_pilot_export("vr-canopy-pilot-geometry.json"))throw std::runtime_error("Cannot export pilot canopy");
        readWorld(panel,"vr-panel-source.png");
        Image inspected;inspected.w=inspected.h=1024;
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=1024;d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        dx(device->CreateTexture2D(&d,nullptr,&inspected.texture),"cockpit inspection image");
        ComPtr<ID3D11RenderTargetView> target;dx(device->CreateRenderTargetView(inspected.texture.Get(),nullptr,&target),"cockpit inspection target");
        auto depth=createDepth(1024,1024);
        const char *names[]={"front","left","right","rear","front-left","front-right","up"};
        const double yaw[]={0,pi/2,-pi/2,pi,pi/4,-pi/4,0};VrFov f{-pi/4,pi/4,pi/4,-pi/4};
        for(int k=0;k<7;k++) {
            const double pitch=k==6?pi/3:0;
            VrPose p{vr_multiply({0,std::sin(yaw[k]/2),0,std::cos(yaw[k]/2)},
                {std::sin(pitch/2),0,0,std::cos(pitch/2)}),{0,0,0}};
            vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&p,nullptr,w,h);vr_gpu_end();
            if(!ok||geometry[0].invalid)throw std::runtime_error("Cockpit inspection world capture failed");
            renderWorld(0,f,1024,1024);
            const float clear[4]={0,0,0,1};context->ClearRenderTargetView(target.Get(),clear);
            context->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,1,0);targetSRGB=true;
            render(0,p,f,target.Get(),depth.Get(),0,0,1024,1024,true);
            char name[80];std::snprintf(name,sizeof name,"vr-cockpit-%s.png",names[k]);readWorld(inspected,name);
        }
        // Project the actual mesh orthographically as well: perspective views
        // from the seat conceal errors in pane topology and side proportions.
        const auto *mesh=vr_cockpit_mesh();
        for(int side=0;side<2;side++){
            const float clear[4]={.30f,.34f,.38f,1};context->ClearRenderTargetView(target.Get(),clear);
            context->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,1,0);targetSRGB=true;
            ID3D11RenderTargetView *rt=target.Get();context->OMSetRenderTargets(1,&rt,depth.Get());
            context->OMSetDepthStencilState(depthState.Get(),0);context->RSSetState(raster.Get());
            D3D11_VIEWPORT vp{0,0,1024,1024,0,1};context->RSSetViewports(1,&vp);
            auto profile=[&](const VrCockpitVertex *v,unsigned count){
                for(unsigned i=0;i<count;i++){
                    const auto &p=v[i].p;
                    const float scale=side?1.5f:.86f;
                    V q{{side?(p[2]+.52f)/scale:p[0]/scale,(p[1]+.08f)/scale,
                        side?(p[0]+.8f)/1.6f:(p[2]+2)/3,1},{0,0},
                        {v[i].colour[0],v[i].colour[1],v[i].colour[2],v[i].colour[3]}};
                    vertices.push_back(q);
                }draw(0,f,nullptr);
            };
            profile(mesh->opaque,mesh->opaque_count);
            context->OMSetBlendState(glassBlend.Get(),nullptr,~0u);context->OMSetDepthStencilState(glassDepth.Get(),0);
            profile(mesh->glass,mesh->glass_count);
            context->OMSetBlendState(nullptr,nullptr,~0u);context->OMSetDepthStencilState(depthState.Get(),0);
            readWorld(inspected,side?"vr-canopy-profile-side.png":"vr-canopy-profile-front.png");
        }
        auto prepare=[&](const float *clear){
            context->ClearRenderTargetView(target.Get(),clear);
            context->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,1,0);targetSRGB=true;
            ID3D11RenderTargetView *rt=target.Get();context->OMSetRenderTargets(1,&rt,depth.Get());
            context->OMSetDepthStencilState(depthState.Get(),0);context->RSSetState(raster.Get());
            D3D11_VIEWPORT vp{0,0,1024,1024,0,1};context->RSSetViewports(1,&vp);
        };
        // One frozen world, one eye pose and one FOV for all layer comparisons.
        // These use the photo-fitted candidate, not the production canopy.
        VrPose pilot{{0,0,0,1},{0,0,0}};
        vr_gpu_begin(&geometry[0]);bool inspectionOK=enh_vr_render(&pilot,nullptr,w,h);vr_gpu_end();
        if(!inspectionOK||geometry[0].invalid)throw std::runtime_error("Canopy layer world capture failed");
        renderWorld(0,f,1024,1024);
        const char *layers[]={"shell-only","hud-hidden","hud-hardware","hud-visible"};
        for(int stage=0;stage<4;stage++){
            const float clear[4]={0,0,0,1};prepare(clear);
            fullscreen();draw(gpuWorld?1:3,f,&world[0],std::max(1,vr_canvas_resolution()/1024));
            inspectionCockpit(pilot,f,stage);
            char name[96];std::snprintf(name,sizeof name,"vr-canopy-pilot-%s.png",layers[stage]);readWorld(inspected,name);
        }
        for(int stage=1;stage<3;stage++){
            const float clear[4]={0,0,0,1};prepare(clear);
            inspectionCockpit(pilot,f,stage,true);
            readWorld(inspected,stage==1?"vr-canopy-mask-hud-hidden.png":"vr-canopy-mask-hud-visible.png");
        }
        const CanopyPlacement placements[]={
            {"baseline",0,0,1},{"lower-eye",.1f,0,1},{"forward-eye",0,.6f,1},
            {"forward-lower",.1f,.6f,1},{"far-forward-lower",.1f,.8f,1},
            {"steeper-rake",0,0,.5f},{"steeper-mounted",.08f,.15f,.5f}};
        if(FILE *file=std::fopen("vr-canopy-seat-placements.json","w")){
            std::fputs("[",file);
            for(unsigned i=0;i<sizeof placements/sizeof *placements;i++){
                const auto &p=placements[i];
                std::fprintf(file,"%s{\"name\":\"%s\",\"offset\":[0,%.6f,%.6f],\"nose_scale\":%.6f}",i?",":"",p.name,p.y,p.z,p.noseScale);
            }std::fputs("]\n",file);std::fclose(file);
        }
        for(const auto &placement:placements){
            const float clear[4]={0,0,0,1};prepare(clear);
            fullscreen();draw(gpuWorld?1:3,f,&world[0],std::max(1,vr_canvas_resolution()/1024));
            inspectionCockpit(pilot,f,0,false,&placement);
            char name[96];std::snprintf(name,sizeof name,"vr-canopy-seat-%s.png",placement.name);readWorld(inspected,name);
        }
        for(int stage=0;stage<4;stage++){
            const float clear[4]={0,0,0,1};prepare(clear);
            fullscreen();draw(gpuWorld?1:3,f,&world[0],std::max(1,vr_canvas_resolution()/1024));
            inspectionCockpit(pilot,f,stage,false,nullptr,true);
            char name[96];std::snprintf(name,sizeof name,"vr-canopy-fitted-%s.png",layers[stage]);readWorld(inspected,name);
        }
        for(int showHud=0;showHud<2;showHud++){
            const float clear[4]={0,0,0,1};prepare(clear);
            fullscreen();draw(gpuWorld?1:3,f,&world[0],std::max(1,vr_canvas_resolution()/1024));
            cockpit(pilot,f,showHud!=0);
            readWorld(inspected,showHud?"vr-cockpit-pilot-hud-visible.png":"vr-cockpit-pilot-hud-hidden.png");
        }
        // Exterior comparisons render this exact cached cockpit mesh, not an
        // artist's reconstruction or the original game's whole-aircraft model.
        const double cameras[][3]={{-2.6,1.1,-3.8},{0,.25,-4.5},{-3.8,.25,-.5}};
        const char *outside[]={"front-left","front","side"};
        VrFov external{-pi/8,pi/8,pi/8,-pi/8};
        for(int k=0;k<3;k++){
            const float clear[4]={.24f,.28f,.32f,1};prepare(clear);
            double dx=-cameras[k][0],dy=-.30-cameras[k][1],dz=-.5-cameras[k][2];
            double yaw=std::atan2(-dx,-dz),pitch=std::atan2(dy,std::hypot(dx,dz));
            VrPose p{vr_multiply({0,std::sin(yaw/2),0,std::cos(yaw/2)},
                {std::sin(pitch/2),0,0,std::cos(pitch/2)}),{cameras[k][0],cameras[k][1],cameras[k][2]}};
            vr_matrix(p.orientation,eyeRotation);
            quad(p,external,{{{-4,-1.04,-5},{4,-1.04,-5},{4,-1.04,4},{-4,-1.04,4}}},{.11f,.13f,.15f,1});draw(0,external,nullptr);
            cockpit(p,external);
            char name[80];std::snprintf(name,sizeof name,"vr-cockpit-exterior-%s.png",outside[k]);readWorld(inspected,name);
        }
        const char *optics[]={"center","left","right","offset"};
        const double offsets[][3]={{0,0,0},{-.032,0,0},{.032,0,0},{.06,.025,.04}};
        for(int k=0;k<4;k++){
            const float clear[4]={0,0,0,1};prepare(clear);
            VrPose p{{0,0,0,1},{offsets[k][0],offsets[k][1],offsets[k][2]}};
            vr_matrix(p.orientation,eyeRotation);hudImage(p,f);
            char name[80];std::snprintf(name,sizeof name,"vr-hud-optics-%s.png",optics[k]);readWorld(inspected,name);
        }
        if(FILE *file=std::fopen("vr-cockpit-layout.json","w")){
            unsigned count=0;const auto *in=vr_cockpit_instruments(&count);
            std::fputs("{\"instruments\":[",file);
            for(unsigned i=0;i<count;i++){
                const auto &p=in[i].face;const int *s=in[i].source;
                std::fprintf(file,"%s{\"name\":\"%s\",\"source\":[%d,%d,%d,%d],\"face\":[%.6f,%.6f,%.6f,%.6f,%.6f]}",
                    i?",":"",in[i].name,s[0],s[1],s[2],s[3],p.x0,p.y0,p.x1,p.y1,p.z);
            }
            std::fputs("]}\n",file);std::fclose(file);
        }
    }
    void dumpTemporalViews(int w,int h) {
        VrFov f{-pi/4,pi/4,pi/4,-pi/4};
        int size=SDL_getenv("F117_VR_CAPTURE_SIZE")?std::clamp(SDL_atoi(SDL_getenv("F117_VR_CAPTURE_SIZE")),256,2048):512;
        int steps=SDL_getenv("F117_VR_CAPTURE_STEPS")?std::clamp(SDL_atoi(SDL_getenv("F117_VR_CAPTURE_STEPS")),16,360):40;
        if(FILE *file=std::fopen("vr-sweep.json","w")){
            std::fprintf(file,"{\"size\":%d,\"steps\":%d,\"radians\":%.15f,\"fps\":90,\"samples\":%u}\n",size,steps,pi/2,samples);std::fclose(file);
        }
        for(int k=0;k<=steps+1;k++) {
            double roll=k==steps+1?0:k*pi/(2*steps);
            VrPose p{{0,0,std::sin(roll/2),std::cos(roll/2)},{0,0,0}};
            auto &g=geometry[0];vr_gpu_begin(&g);bool ok=enh_vr_render(&p,nullptr,w,h);vr_gpu_end();
            if(!ok||g.invalid||!g.background_count)throw std::runtime_error("Temporal scene capture failed");
            if(k==1||k==steps/9||k==steps/3||k==steps/2||k==2*steps/3||k==steps-1||k==steps){
                renderWorld(0,f,std::min(size*4,4096),std::min(size*4,4096));
                char ref[80];std::snprintf(ref,sizeof ref,"roll-reference-%03d.png",k);readWorld(world[0],ref);
            }
            renderWorld(0,f,size,size);
            char name[80];std::snprintf(name,sizeof name,k==steps+1?"roll-repeat.png":"roll-%03d.png",k);readWorld(world[0],name);
            if(k==steps+1)break;
            std::array<uint32_t,256> palette;std::copy(g.palette,g.palette+256,palette.begin());
            for(int c=0;c<256;c++)g.palette[c]=(c>=0x80&&c<=0x8f)?0x00f00000:0x000000f0;
            renderWorld(0,f,size,size,true);std::snprintf(name,sizeof name,"sky-%03d.png",k);readWorld(world[0],name);
            std::copy(palette.begin(),palette.end(),g.palette);
        }
    }
    void dumpGpuEyes(const std::array<VrPose,2> &poses,const std::array<VrFov,2> &fovs,int w,int h) {
        if(!vr_options.dump||dumped||!vr_scene_initialized())return;
        if(SDL_getenv("F117_VR_VISIBILITY_TEST")) {
            FILE *file=std::fopen("vr-visibility.csv","w");if(!file)throw std::runtime_error("Visibility trace file");
            std::fprintf(file,"distance_units,metres,model_triangles,dots,invalid,rendered_pixels,isolated_pixels,tagged_vertices,in_view_vertices,model_width\n");
            VrPose pose{{0,0,0,1},{0,0,0}};
            for(int d=256;d<=131072;d+=256) {
                vr_presentation_test_aircraft(d);
                vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&pose,nullptr,w,h);vr_gpu_end();
                if(!ok){std::fclose(file);throw std::runtime_error("Visibility scene render");}
                const auto &g=geometry[0];
                int coverage=-1,isolated=-1;
                if(d==4096||d==12288||d==16384||d==24576||d==49152||d==65536||d==131072) {
                    int ew=preview?(vr_options.resolution?vr_options.resolution:1920):swaps[0].w;
                    int eh=preview?ew:swaps[0].h;
                    renderWorld(0,fovs[0],ew,eh,false,2);auto pixels=readWorld(world[0],nullptr);
                    coverage=int(std::count_if(pixels.begin(),pixels.end(),[](uint32_t p){return (p&0xFFFFFF)!=0;}));
                    renderWorld(0,fovs[0],ew,eh,false,3);pixels=readWorld(world[0],nullptr);
                    isolated=int(std::count_if(pixels.begin(),pixels.end(),[](uint32_t p){return (p&0xFFFFFF)!=0;}));
                    char name[96];std::snprintf(name,sizeof name,"vr-aircraft-%d.png",d);
                    renderWorld(0,fovs[0],ew,eh);readWorld(world[0],name);
                }
                unsigned tagged=0,inview=0;for(size_t k=0;k<g.count;k++)if(g.vertices[k].flags&VR_SURFACE_AIRCRAFT){tagged++;if(std::abs(g.vertices[k].x)<1&&std::abs(g.vertices[k].y)<1)inview++;}
                double lo=1e9,hi=-1e9;for(size_t k=0;k<g.count;k++)if((g.vertices[k].flags&(VR_SURFACE_AIRCRAFT|VR_SURFACE_LIGHT))==VR_SURFACE_AIRCRAFT){lo=std::min(lo,double(g.vertices[k].x));hi=std::max(hi,double(g.vertices[k].x));}
                std::fprintf(file,"%d,%.3f,%u,%u,%u,%d,%d,%u,%u,%.9f\n",d,d*.5*(512000.0/1048576)*vr_options.world_scale,g.aircraft_triangles,g.aircraft_dots,g.invalid,coverage,isolated,tagged,inview,hi>=lo?hi-lo:0);
            }
            std::fclose(file);
            file=std::fopen("vr-aircraft-motion.csv","w");if(!file)throw std::runtime_error("Aircraft motion trace");
            std::fprintf(file,"distance_units,axis,step,centre_x,centre_y,width,vertices\n");
            for(int distance:{4096,65536})for(int axis=0;axis<2;axis++)for(int step=0;step<64;step++) {
                vr_presentation_test_aircraft(distance);
                vr_presentation_test_motion(step,axis);
                vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&pose,nullptr,w,h);vr_gpu_end();
                if(!ok)throw std::runtime_error("Aircraft motion scene");
                double lo[2]={1e9,1e9},hi[2]={-1e9,-1e9};unsigned count=0;
                for(size_t k=0;k<geometry[0].count;k++)if(geometry[0].vertices[k].flags&VR_SURFACE_AIRCRAFT) {
                    auto &v=geometry[0].vertices[k];lo[0]=std::min(lo[0],double(v.x));lo[1]=std::min(lo[1],double(v.y));
                    hi[0]=std::max(hi[0],double(v.x));hi[1]=std::max(hi[1],double(v.y));count++;
                }
                std::fprintf(file,"%d,%d,%d,%.9f,%.9f,%.9f,%u\n",distance,axis,step,(lo[0]+hi[0])*.5,(lo[1]+hi[1])*.5,hi[0]-lo[0],count);
            }
            std::fclose(file);vr_presentation_test_motion(-1,0);
            file=std::fopen("vr-marker-poses.csv","w");if(!file)throw std::runtime_error("Marker pose trace");
            std::fprintf(file,"distance,pose,valid,marker_x,marker_y,model_x,model_y\n");
            const VrPose headPoses[]={{{0,0,0,1},{0,0,0}},{{0,std::sin(.2),0,std::cos(.2)},{0,0,0}},
                {{std::sin(.15),0,0,std::cos(.15)},{0,0,0}},{{-std::sin(.15),0,0,std::cos(.15)},{0,0,0}},
                {{0,0,std::sin(.2),std::cos(.2)},{0,0,0}},{{0,0,0,1},{.2,.15,.1}}};
            for(int distance:{4096,32768})for(int k=0;k<6;k++) {
                vr_presentation_test_aircraft(distance);
                vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&headPoses[k],nullptr,w,h);vr_gpu_end();
                const auto &g=geometry[0];
                std::fprintf(file,"%d,%d,%d,%.9f,%.9f,%.9f,%.9f\n",distance,k,ok&&g.actor_reference_valid&&g.marker_reference_valid,
                    g.marker_reference[0],g.marker_reference[1],g.actor_reference[0],g.actor_reference[1]);
            }
            std::fclose(file);vr_presentation_test_aircraft(0);
            vr_gpu_begin(&geometry[0]);bool restored=enh_vr_render(&poses[0],nullptr,w,h);vr_gpu_end();
            if(!restored)throw std::runtime_error("Restore after visibility sweep");
            int ew=preview?(vr_options.resolution?vr_options.resolution:1920):swaps[0].w;
            renderWorld(0,fovs[0],ew,preview?ew:swaps[0].h);dumped=true;
            note("Wrote continuous aircraft distance/representation sweep");return;
        }
        if(SDL_getenv("F117_VR_HUD_CHECK")&&!enh_vr_hud_check())throw std::runtime_error("VR HUD export discarded original symbols");
        VrSceneInfo info;vr_scene_info(&info);
        if(FILE *file=std::fopen("vr-scene.json","w")) {
            std::fprintf(file,"{\"site\":%d,\"carrier\":%d,\"deck\":%d,\"aircraft\":%d,\"camera\":%d}\n",info.site,info.carrier,info.deck,info.aircraft,info.camera);std::fclose(file);
        }
        readWorld(world[0],"vr-left.png");readWorld(world[1],"vr-right.png");
        if(SDL_getenv("F117_VR_COAST_FIXTURE"))locateCoast(w,h);
        if(SDL_getenv("F117_VR_TEMPORAL")) {
            dumpTemporalViews(w,h);
            vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&poses[0],nullptr,w,h);vr_gpu_end();
            if(!ok)throw std::runtime_error("Restore temporal scene");
            int ew=preview?(vr_options.resolution?vr_options.resolution:1920):swaps[0].w;
            renderWorld(0,fovs[0],ew,preview?ew:swaps[0].h);
            dumped=true;note("Wrote frozen-scene temporal roll sweep");return;
        }
        // Differential check at precisely the software reference's projection.
        const int n=4;double cx=(w-320*n)/2.0+159*n,cy=h/2.0;
        VrFov ref{std::atan(-cx/(256*n)),std::atan((w-cx)/(256*n)),std::atan(cy/(192*n)),std::atan((cy-h)/(192*n))};
        const bool validate=SDL_getenv("F117_VR_VALIDATE")!=nullptr;
        const VrQuat turns[]={{0,0,0,1},{0,float(std::sin(.25)),0,float(std::cos(.25))},
            {float(std::sin(.2)),0,0,float(std::cos(.2))},{0,0,float(std::sin(.4)),float(std::cos(.4))},
            {std::sin(-.5),0,0,std::cos(-.5)},
            vr_multiply({0,std::sin(.5),0,std::cos(.5)},{std::sin(-.35),0,0,std::cos(-.35)}),
            vr_multiply({0,std::sin(-.5),0,std::cos(-.5)},{std::sin(-.35),0,0,std::cos(-.35)}),
            vr_multiply({0,std::sin(pi/4),0,std::cos(pi/4)},{std::sin(-.35),0,0,std::cos(-.35)}),
            vr_multiply({0,std::sin(-pi/4),0,std::cos(-pi/4)},{std::sin(-.35),0,0,std::cos(-.35)}),
            vr_multiply({0,1,0,0},{std::sin(-.35),0,0,std::cos(-.35)}),
            vr_multiply({0,std::sin(pi/8),0,std::cos(pi/8)},{std::sin(-.35),0,0,std::cos(-.35)})};
        for(int k=0;k<(validate?(vr_options.presentation?11:7):1);k++) {
            VrPose check=poses[0];check.orientation=vr_multiply(check.orientation,turns[k]);
            // Detached exterior world comparison: wash at the waterline is
            // hidden by the deck when viewed from the seated carrier start.
            if(k==10){check.position[0]+=300;check.position[1]+=200;check.position[2]+=250;}
            vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&check,nullptr,w,h);vr_gpu_end();
            if(!ok||geometry[0].invalid)throw std::runtime_error("GPU validation geometry failed");
            char name[80];std::snprintf(name,sizeof name,"vr-gpu-reference-%d.png",k);
            renderWorld(0,ref,w,h);readWorld(world[0],name);
            if(vr_options.presentation) {
                std::snprintf(name,sizeof name,"vr-surfaces-%d.png",k);
                renderWorld(0,ref,w,h,false,true);readWorld(world[0],name);
                std::snprintf(name,sizeof name,"vr-presentation-%d.json",k);
                if(FILE *file=std::fopen(name,"w")) {
                    const auto &g=geometry[0];const auto &p=g.presentation;
                    std::fprintf(file,"{\"effects\":%.0f,\"distance\":%.0f,\"water_faces\":%u,\"surface_faces\":%u,\"overflow_records\":%u,\"removed_water_points\":%u,\"ships\":%.0f,\"sea\":[%.6f,%.6f,%.6f,%.6f],\"view\":[",
                        p.params[0],p.params[2],g.water_faces,g.surface_faces,g.spills,g.water_speckles,p.params[1],p.sea[0],p.sea[1],p.sea[2],p.sea[3]);
                    for(int r=0;r<3;r++)for(int c=0;c<3;c++)std::fprintf(file,"%s%.7f",r||c?",":"",p.view[r][c]);
                    std::fprintf(file,"],\"triangles\":%zu,\"origin\":[%.6f,%.6f,%.6f],\"source_camera\":[",g.count/3,p.origin[0],p.origin[1],p.origin[2]);
                    for(int c=0;c<8;c++)std::fprintf(file,"%s%u",c?",":"",g.source_camera[c]);
                    std::fprintf(file,"],\"ship_bounds\":[");
                    for(int i=0;i<(int)p.params[1];i++)std::fprintf(file,"%s[%.3f,%.3f,%.3f,%.3f,%.0f,%.0f]",i?",":"",p.ships[i][0],p.ships[i][1],p.shipshape[i][0],p.shipshape[i][1],p.shipshape[i][2],p.shipshape[i][3]);
                    std::fprintf(file,"]}\n");std::fclose(file);
                }
            }
            std::vector<uint32_t> software(size_t(w)*h);
            if(enh_vr_render(&check,software.data(),w,h)) {
                auto *surface=SDL_CreateSurfaceFrom(w,h,SDL_PIXELFORMAT_XRGB8888,software.data(),w*4);
                std::snprintf(name,sizeof name,"vr-software-reference-%d.png",k);
                if(surface){SDL_SavePNG(surface,name);SDL_DestroySurface(surface);}
            }
        }
        dumpCockpitViews(w,h);
        vr_gpu_begin(&geometry[0]);bool ok=enh_vr_render(&poses[0],nullptr,w,h);vr_gpu_end();
        if(!ok)throw std::runtime_error("Restore native GPU eye after validation");
        int ew=preview?(vr_options.resolution?vr_options.resolution:1920):swaps[0].w;
        int eh=preview?ew:swaps[0].h;renderWorld(0,fovs[0],ew,eh);
        dumped=true;note("Wrote native GPU eyes, matched-projection references, cockpit interior/exterior views, canopy profiles and HUD optics");
    }
    void dumpMirror(int size) {
        if(!vr_options.dump || !mirror || mirrorDumped)return;
        ComPtr<ID3D11Texture2D> back,staging;
        dx(mirror->GetBuffer(0,IID_PPV_ARGS(&back)),"capture mirror");
        D3D11_TEXTURE2D_DESC desc;back->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        dx(device->CreateTexture2D(&desc,nullptr,&staging),"mirror readback image");
        context->CopyResource(staging.Get(),back.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        dx(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"mirror readback");
        auto *surface=SDL_CreateSurfaceFrom(size,size,SDL_PIXELFORMAT_RGBA32,mapped.pData,mapped.RowPitch);
        if(surface){SDL_SavePNG(surface,"vr-mirror.png");SDL_DestroySurface(surface);}
        context->Unmap(staging.Get(),0);mirrorDumped=true;note("Wrote vr-mirror.png (GPU-rendered stereo cockpit)");
    }
    void tick(const uint32_t *desktop,int w,int h,bool changed) {
        panelDirty|=changed;
        if(in_tick || exit || !desktop)return;
        if(benchmark&&!enh_vr_ready())return;
        if(!preview){poll();if(!running)return;}
        const Uint64 now=SDL_GetTicksNS();
        // Keyboard peeks also pump the host. Do not turn every peek into a
        // blocking headset wait: leave time for simulation/input work between
        // actual display frames. panelDirty above retains pending updates.
        bool paced=!preview||(benchmark&&SDL_getenv("F117_VR_TEST_HZ"));
        if(paced&&nextService&&now<nextService)return;
        if(preview && !benchmark && lastFrame && now-lastFrame<SDL_NS_PER_SECOND/60)return;
        in_tick=true;
        struct Guard {bool &b;~Guard(){b=false;}} guard{in_tick};
        const Uint64 started=SDL_GetTicksNS();
        uploadBytes=0;
        XrFrameState frame{XR_TYPE_FRAME_STATE};bool begun=false;
        if(!preview) {
            XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};xr(xrWaitFrame(session,&wait,&frame),"wait VR frame");
            XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};xr(xrBeginFrame(session,&begin),"begin VR frame");begun=true;
            if(frame.predictedDisplayPeriod>0)displayPeriod=Uint64(frame.predictedDisplayPeriod);
        }
        if(preview&&benchmark&&SDL_getenv("F117_VR_TEST_HZ")) {
            Uint64 now=SDL_GetTicksNS();
            if(testDeadline>now)SDL_DelayNS(testDeadline-now);
            double hz=std::clamp(SDL_atof(SDL_getenv("F117_VR_TEST_HZ")),30.0,144.0);
            displayPeriod=Uint64(SDL_NS_PER_SECOND/hz);testDeadline=SDL_GetTicksNS()+displayPeriod;
        }
        if(paced)nextService=SDL_GetTicksNS()+displayPeriod-std::min(Uint64(2*SDL_NS_PER_MS),displayPeriod/4);
        // Ensure a begun frame is always ended, including errors and tracking loss.
        struct EndGuard {Backend *b;bool &active;XrTime t;~EndGuard(){if(active){XrFrameEndInfo e{XR_TYPE_FRAME_END_INFO};e.displayTime=t;e.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(b->session,&e);}}} end{this,begun,frame.predictedDisplayTime};
        std::array<VrPose,2> poses;
        std::array<VrFov,2> fovs;
        std::array<XrView,2> views={XrView{XR_TYPE_VIEW},XrView{XR_TYPE_VIEW}};
        if(preview) {
            const bool *keys=SDL_GetKeyboardState(nullptr);
            if(keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) {
                double step=0.015;
                previewYaw+=(keys[SDL_SCANCODE_LEFT]-keys[SDL_SCANCODE_RIGHT])*step;
                previewPitch+=(keys[SDL_SCANCODE_UP]-keys[SDL_SCANCODE_DOWN])*step;
                previewPitch=std::clamp(previewPitch,-1.3,1.3);
            }
            previewHead.orientation=vr_multiply({0,std::sin(previewYaw/2),0,std::cos(previewYaw/2)},{std::sin(previewPitch/2),0,0,std::cos(previewPitch/2)});
            double rotation[9];vr_matrix(previewHead.orientation,rotation);
            for(int i=0;i<2;i++) {
                poses[i]=previewHead;
                for(int k=0;k<3;k++)poses[i].position[k]+=rotation[k*3]*(i?0.032:-0.032);
                fovs[i]={-pi/4,pi/4,pi/4,-pi/4};
            }
        } else {
            if(!frame.shouldRender)return;
            center(frame.predictedDisplayTime);
            XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=frame.predictedDisplayTime;locate.space=seat;
            XrViewState viewState{XR_TYPE_VIEW_STATE};uint32_t count=0;
            xr(xrLocateViews(session,&locate,&viewState,2,&count,views.data()),"locate eye poses");
            const auto valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
            if(count!=2 || (viewState.viewStateFlags&valid)!=valid)return;
            for(int i=0;i<2;i++) {
                auto &p=views[i].pose;auto &f=views[i].fov;
                poses[i]={{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}};
                fovs[i]={f.angleLeft,f.angleRight,f.angleUp,f.angleDown};
            }
        }
        const Uint64 workStarted=SDL_GetTicksNS(); // exclude OpenXR frame wait / tracking lookup
        const int panelScale=gpuWorld?4:std::max(1,vr_canvas_resolution()/1024);
        bool flight=enh_vr_ready();
        if(panelDirty || panelFlight!=flight || !panel.texture) {
            panelPixels.resize(size_t(320*panelScale)*200*panelScale);
            vr_capture_desktop(desktop,w,h,panelPixels.data(),panelScale);
            upload(panel,panelPixels.data(),320*panelScale,200*panelScale);
            panelDirty=false;panelFlight=flight;
        }
        const Uint64 panelDone=SDL_GetTicksNS();
        double geometryMS=0,hudMS=0;
        const int size=vr_canvas_resolution();
        std::array<VrFrameState,2> states{};
        if(flight) {
            // Diagnostic key repeats begin only after the real flight is ready.
            // SDL events follow the same translation/BIOS path as a held key.
            if(benchmark&&vr_scene_initialized()&&SDL_getenv("F117_VR_TEST_INPUT")) {
                Uint64 now=SDL_GetTicksNS();if(!inputStarted)inputStarted=now;
                unsigned due=unsigned((now-inputStarted)/(80*SDL_NS_PER_MS));
                unsigned limit=!std::strcmp(SDL_getenv("F117_VR_TEST_INPUT"),"control")?12:54;
                while(inputEvents<=due&&inputEvents<limit) {
                    bool reverse=!std::strcmp(SDL_getenv("F117_VR_TEST_INPUT"),"held-reverse");
                    bool up=inputEvents>=12&&(!reverse||inputEvents<30||inputEvents>=42);
                    SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.down=true;e.key.repeat=(inputEvents!=0);
                    e.key.scancode=up?SDL_SCANCODE_EQUALS:SDL_SCANCODE_MINUS;e.key.key=up?SDLK_EQUALS:SDLK_MINUS;
                    SDL_PushEvent(&e);inputEvents++;
                }
            }
            enh_vr_frame(); // latch one alpha for both eyes
            Uint64 hudStart=SDL_GetTicksNS();
            hudPixels.resize(size_t(320*panelScale)*91*panelScale);
            if(!enh_vr_hud_render(hudPixels.data(),panelScale))throw std::runtime_error("HUD render failed");
            upload(hud,hudPixels.data(),320*panelScale,91*panelScale);
            hudMS=(SDL_GetTicksNS()-hudStart)/1e6;
            for(int i=0;i<2;i++) {
                if(gpuWorld) {
                    vr_gpu_begin(&geometry[i]);
                    Uint64 geometryStart=SDL_GetTicksNS();
                    bool ok=enh_vr_render(&poses[i],nullptr,w,h);vr_gpu_end();
                    geometryMS+=(SDL_GetTicksNS()-geometryStart)/1e6;
                    if(!ok){flight=false;break;}
                    int ew=preview?(vr_options.resolution?vr_options.resolution:1920):swaps[i].w;
                    int eh=preview?ew:swaps[i].h;
                    renderWorld(i,fovs[i],ew,eh);
                } else {
                    pixels[i].resize(size_t(size)*size);
                    if(!enh_vr_render(&poses[i],pixels[i].data(),size,size)){flight=false;break;}
                    upload(world[i],pixels[i].data(),size,size);
                }
                states[i]=enh_vr_frame_state();
            }
            if(flight&&!gpuWorld)dumpEyes(size);
            if(flight&&gpuWorld)dumpGpuEyes(poses,fovs,w,h);
        }
        const Uint64 worldDone=SDL_GetTicksNS();
        float clear[4]={0.015f,0.02f,0.025f,1};
        std::array<XrCompositionLayerProjectionView,2> projectionViews;
        if(!preview) {
            for(int i=0;i<2;i++) {
                auto &s=swaps[i];uint32_t imageIndex;
                XrSwapchainImageAcquireInfo acq{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};xr(xrAcquireSwapchainImage(s.handle,&acq,&imageIndex),"acquire eye image");
                bool acquired=true;
                struct ReleaseGuard {XrSwapchain s;bool &active;~ReleaseGuard(){if(active){XrSwapchainImageReleaseInfo r{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xrReleaseSwapchainImage(s,&r);}}} release{s.handle,acquired};
                XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;xr(xrWaitSwapchainImage(s.handle,&wait),"wait eye image");
                // OpenXR may return a typeless native texture. The RTV must explicitly use
                // the format negotiated for this swapchain, rather than infer a typeless format.
                D3D11_TEXTURE2D_DESC imageDesc{};s.images[imageIndex].texture->GetDesc(&imageDesc);
                D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=s.format;
                rtv.ViewDimension=imageDesc.ArraySize>1?D3D11_RTV_DIMENSION_TEXTURE2DARRAY:D3D11_RTV_DIMENSION_TEXTURE2D;
                if(imageDesc.ArraySize>1)rtv.Texture2DArray.ArraySize=1;
                ComPtr<ID3D11RenderTargetView> rt;dx(device->CreateRenderTargetView(s.images[imageIndex].texture,&rtv,&rt),"eye render target");
                D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};rt->GetDesc(&targetDesc);
                targetSRGB=targetDesc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || targetDesc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                // Cached dimensions; depth images can be reused across the two eye renders.
                if(depthW!=s.w || depthH!=s.h){eyeDepth=createDepth(s.w,s.h);depthW=s.w;depthH=s.h;}
                context->ClearRenderTargetView(rt.Get(),clear);context->ClearDepthStencilView(eyeDepth.Get(),D3D11_CLEAR_DEPTH,1,0);
                render(i,poses[i],fovs[i],rt.Get(),eyeDepth.Get(),0,0,float(s.w),float(s.h),flight);
                ID3D11RenderTargetView *none=nullptr;context->OMSetRenderTargets(1,&none,nullptr);
                XrSwapchainImageReleaseInfo rel{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xr(xrReleaseSwapchainImage(s.handle,&rel),"release eye image");acquired=false;
                projectionViews[i]={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};projectionViews[i].pose=views[i].pose;projectionViews[i].fov=views[i].fov;
                projectionViews[i].subImage.swapchain=s.handle;projectionViews[i].subImage.imageRect={{0,0},{s.w,s.h}};
            }
            XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=seat;layer.viewCount=2;layer.views=projectionViews.data();
            const XrCompositionLayerBaseHeader *layers[]={reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
            XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};info.displayTime=frame.predictedDisplayTime;info.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;info.layerCount=1;info.layers=layers;
            xr(xrEndFrame(session,&info),"submit headset frame");begun=false;
        }
        int captured=-1;
        if(mirror) {
            targetSRGB=true;
            context->ClearRenderTargetView(mirrorRT.Get(),clear);context->ClearDepthStencilView(mirrorDepth.Get(),D3D11_CLEAR_DEPTH,1,0);
            // Square eyes, side by side, letterboxed in the square desktop window.
            for(int i=0;i<2;i++)render(i,poses[i],fovs[i],mirrorRT.Get(),mirrorDepth.Get(),float(i*512),256,512,512,flight);
            if(flight && (dumped||!gpuWorld))dumpMirror(1024);
            if(flight&&vr_scene_initialized()&&SDL_getenv("F117_VR_CAPTURE_LIVE")&&(benchmark||captureArmed))captured=captureLiveFrame();
            dx(mirror->Present(0,0),"present stereo mirror");
        }
        if(timing&&flight)for(int i=0;i<2;i++) {
            const auto &s=states[i];
            std::fprintf(timing,"%llu,%d,%lld,%llu,%llu,%.9f,%d,%d,%d,%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f\n",
                (unsigned long long)submission,i,(long long)frame.predictedDisplayTime,
                (unsigned long long)workStarted,(unsigned long long)s.frame,s.alpha,
                s.x,s.y,s.heading,s.pitch,s.roll,captured,s.throttle,s.thrust,
                (SDL_GetTicksNS()-workStarted)/1e6,(panelDone-workStarted)/1e6,hudMS,geometryMS);
        }
        submission++;
        lastFrame=SDL_GetTicksNS();frameMS+=(lastFrame-started)/1e6;frames++;
        if(benchmark && flight && (!vr_options.dump || dumped)) {
            if(benchFrames==benchWarmup)benchStarted=workStarted;
            if(benchFrames++>=benchWarmup) {
                benchMS+=(lastFrame-workStarted)/1e6;benchBytes+=uploadBytes;
                benchPanel+=(panelDone-workStarted)/1e6;benchHUD+=hudMS;benchGeometry+=geometryMS;
                benchDraw+=(worldDone-panelDone)/1e6-geometryMS-hudMS;benchPresent+=(lastFrame-worldDone)/1e6;
            }
            if(benchFrames==benchTarget+benchWarmup) {
                // Include completion of GPU work, once, without forcing a per-frame stall.
                D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> q;
                dx(device->CreateQuery(&qd,&q),"benchmark fence");context->End(q.Get());context->Flush();
                while(context->GetData(q.Get(),nullptr,0,0)==S_FALSE) SDL_Delay(0);
                benchMS+=(SDL_GetTicksNS()-lastFrame)/1e6;
                char b[320];std::snprintf(b,sizeof b,"VR benchmark: %u stereo frames, %.3f ms/frame (render, upload, GPU completion; no XR wait); upload %.3f MiB/frame; %zu triangles, %u invalid faces; %.1f submitted frames/s",benchTarget,benchMS/benchTarget,benchBytes/double(benchTarget)/1048576.0,(geometry[0].count+geometry[1].count)/3,geometry[0].invalid+geometry[1].invalid,benchTarget*1e9/(SDL_GetTicksNS()-benchStarted));note(b);exit=true;
                std::snprintf(b,sizeof b,"VR stages (ms/frame): panel %.3f; HUD %.3f; geometry %.3f; world GPU commands %.3f; headset/mirror %.3f",benchPanel/benchTarget,benchHUD/benchTarget,benchGeometry/benchTarget,benchDraw/benchTarget,benchPresent/benchTarget);note(b);
            }
        }
        if(lastFrame-lastStats>5*SDL_NS_PER_SECOND) {
            char b[160];std::snprintf(b,sizeof b,"%s; %u frames, average frame work %.2f ms (includes OpenXR wait)",flight?"Stereo world":"Menu panel",frames,frameMS/std::max(1u,frames));note(b);lastStats=lastFrame;frames=0;frameMS=0;
        }
    }
private:
    // Bounded GPU-only burst of the live stereo mirror. Readback/PNG work occurs
    // at shutdown, after measurement, so capture does not stall every VR frame.
    ComPtr<ID3D11Texture2D> capture;
    unsigned captureCount=0;
    bool captureArmed=false;
    static constexpr unsigned captureLimit=180;
    int captureLiveFrame() {
        if(captureCount>=captureLimit)return -1;
        ComPtr<ID3D11Resource> src;mirrorRT->GetResource(&src);
        if(!capture){
            ComPtr<ID3D11Texture2D> tex;dx(src.As(&tex),"live mirror texture");
            D3D11_TEXTURE2D_DESC d;tex->GetDesc(&d);
            d.Width=1024;d.Height=512;d.ArraySize=captureLimit;d.BindFlags=0;d.MiscFlags=0;
            dx(device->CreateTexture2D(&d,nullptr,&capture),"live capture burst");
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);
        const D3D11_BOX region{0,256,0,1024,768,1};
        context->CopySubresourceRegion(capture.Get(),captureCount,0,0,0,src.Get(),0,&region);
        return int(captureCount++);
    }
    void dumpLiveCapture() {
        D3D11_TEXTURE2D_DESC d;capture->GetDesc(&d);d.ArraySize=1;
        d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;dx(device->CreateTexture2D(&d,nullptr,&staging),"live capture readback");
        for(unsigned i=0;i<captureCount;i++){
            context->CopySubresourceRegion(staging.Get(),0,0,0,0,capture.Get(),i,nullptr);
            D3D11_MAPPED_SUBRESOURCE m;dx(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"live capture map");
            auto *surface=SDL_CreateSurfaceFrom(1024,512,SDL_PIXELFORMAT_RGBA32,m.pData,m.RowPitch);
            char name[80];std::snprintf(name,sizeof name,"vr-live-%03u.png",i);
            bool ok=surface&&SDL_SavePNG(surface,name);if(surface)SDL_DestroySurface(surface);
            context->Unmap(staging.Get(),0);if(!ok)throw std::runtime_error("Cannot save live capture PNG");
        }
        note("Wrote live stereo frames and vr-timing.csv with original frame timestamps");
    }
    ComPtr<ID3D11DepthStencilView> eyeDepth;
    int depthW=0,depthH=0;
    bool mirrorDumped=false;
    FILE *timing=nullptr;
    uint64_t submission=0;
};
std::unique_ptr<Backend> backend;
bool failed=false;
}

extern "C" bool vr_start(void *hwnd) {
    try {backend=std::make_unique<Backend>();backend->init(static_cast<HWND>(hwnd),vr_options.mode==2);return true;}
    catch(const std::exception &e) {if(backend)backend->note(e.what());backend.reset();return false;}
}
extern "C" void vr_stop(void) {backend.reset();}
extern "C" void vr_tick(const uint32_t *pixels,int w,int h,bool changed) {
    if(!backend || failed)return;
    try {backend->tick(pixels,w,h,changed);}
    catch(const std::exception &e) {backend->note(e.what());backend->note("VR rendering stopped after an error; close the game and inspect f117-vr.log");failed=true;}
}
extern "C" bool vr_wants_quit(void) {return failed || (backend && backend->exit);}
extern "C" bool vr_key(int sc,bool down,bool repeat) {
    if(!backend)return false;
    if(sc==SDL_SCANCODE_F9&&SDL_getenv("F117_VR_CAPTURE_LIVE")) {
        if(down&&!repeat)backend->requestLiveCapture();
        return true;
    }
    if(enh_vr_ready()) {
        // ENH: headset tracking owns the view. Keep the canonical front cockpit for
        // live instruments and a fixed-size eye canvas; these keys normally change its art/layout.
        const bool shift=(SDL_GetModState()&SDL_KMOD_SHIFT)!=0;
        if(sc==SDL_SCANCODE_C || (shift && ((sc>=SDL_SCANCODE_F1 && sc<=SDL_SCANCODE_F6)
            || sc==SDL_SCANCODE_M || sc==SDL_SCANCODE_COMMA || sc==SDL_SCANCODE_PERIOD || sc==SDL_SCANCODE_SLASH)))return true;
    }
    if(sc==SDL_SCANCODE_F12) {
        if(down && !repeat){backend->recenter=true;backend->previewYaw=backend->previewPitch=0;backend->previewHead.position[0]=backend->previewHead.position[1]=backend->previewHead.position[2]=0;}
        return true;
    }
    if(sc==SDL_SCANCODE_F11) {if(down && !repeat)vr_options.cockpit=!vr_options.cockpit;return true;}
    if(backend->preview && sc>=SDL_SCANCODE_RIGHT && sc<=SDL_SCANCODE_UP && (SDL_GetModState()&SDL_KMOD_CTRL))return true;
    return false;
}
extern "C" int vr_diagnostic(void) {
    try {Backend test;test.createInstance();test.createDevice();test.note("OpenXR headset and required D3D11 adapter are available");return 0;}
    catch(const std::exception &e){std::fprintf(stderr,"VR diagnostic: %s\n",e.what());return 1;}
}
