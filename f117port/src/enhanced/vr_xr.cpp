/* ENH: OpenXR + D3D11 seated prototype. OpenXR owns pose prediction, eye FOVs and
 * swapchains; the original game supplies two saved-state software-rendered worlds.
 * The cockpit is deliberately simple geometry with live original instrument art.
 * No original game assets are embedded in this backend. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <SDL3/SDL.h>
#include "enhanced/vr.h"
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
        if(texel.a<0.5) discard;
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
    Image world[2],panel;
    std::vector<uint32_t> pixels[2];
    std::array<uint32_t,320*200> panelPixels{};
    std::vector<V> vertices;
    VrPose previewHead{{0,0,0,1},{0,0,0}};
    double previewYaw=0,previewPitch=0;
    Uint64 lastFrame=0,lastStats=0;
    unsigned frames=0;
    double frameMS=0;
    bool targetSRGB=true;
    FILE *log=nullptr;

    Backend() {log=std::fopen("f117-vr.log","w");}
    void note(const char *s) {std::fprintf(stderr,"[VR] %s\n",s);if(log){std::fprintf(log,"%s\n",s);std::fflush(log);}}
    ~Backend() {
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
    }
    void createMirror(HWND hwnd) {
        if(!hwnd) return;
        ComPtr<IDXGIDevice> d;dx(device.As(&d),"DXGI device");
        ComPtr<IDXGIAdapter> adapter;dx(d->GetAdapter(&adapter),"mirror adapter");
        ComPtr<IDXGIFactory> factory;dx(adapter->GetParent(IID_PPV_ARGS(&factory)),"mirror factory");
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=vr_options.resolution;desc.BufferDesc.Height=vr_options.resolution;
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
        char b[160];std::snprintf(b,sizeof b,"%s ready; software eyes %dx%d; F12 recenter, F11 cockpit",preview?"Desktop stereo preview":"OpenXR",vr_options.resolution,vr_options.resolution);note(b);
    }
    void upload(Image &i,const uint32_t *data,int w,int h) {
        if(i.w!=w || i.h!=h) {
            i.srv.Reset();i.texture.Reset();
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            dx(device->CreateTexture2D(&d,nullptr,&i.texture),"upload image");
            dx(device->CreateShaderResourceView(i.texture.Get(),nullptr,&i.srv),"image view");i.w=w;i.h=h;
        }
        context->UpdateSubresource(i.texture.Get(),0,nullptr,data,w*4,0);
    }
    void draw(int mode,const VrFov &fov,Image *image,int n=1) {
        if(vertices.empty())return;
        if(vertices.size()>4096)throw std::runtime_error("VR geometry buffer overflow");
        D3D11_MAPPED_SUBRESOURCE map{};
        dx(context->Map(vb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map),"map vertices");std::memcpy(map.pData,vertices.data(),vertices.size()*sizeof(V));context->Unmap(vb.Get(),0);
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
        double m[9];vr_matrix(eye.orientation,m);
        double delta[3]={x-eye.position[0],y-eye.position[1],z-eye.position[2]},p[3]={};
        for(int r=0;r<3;r++)for(int k=0;k<3;k++)p[r]+=m[3*k+r]*delta[k];
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
    void render(int index,const VrPose &eye,const VrFov &f,ID3D11RenderTargetView *target,ID3D11DepthStencilView *depth,
                float x,float y,float w,float h,bool flight) {
        context->OMSetRenderTargets(1,&target,depth);context->OMSetDepthStencilState(depthState.Get(),0);context->RSSetState(raster.Get());
        D3D11_VIEWPORT vp{x,y,w,h,0,1};context->RSSetViewports(1,&vp);
        if(flight) {fullscreen();draw(3,f,&world[index],std::max(1,vr_options.resolution/1024));}
        const std::array<float,4> white={1,1,1,1},dark={0.025f,0.03f,0.035f,1},rim={0.07f,0.08f,0.09f,1};
        if(!flight) {
            quad(eye,f,{{{-1.05,0.65,-1.7},{1.05,0.65,-1.7},{1.05,-0.66,-1.7},{-1.05,-0.66,-1.7}}},white);
            draw(1,f,&panel);return;
        }
        if(!vr_options.cockpit)return;
        box(eye,f,-0.63,-0.75,-1.05,0.63,-0.20,-0.87,dark); // dashboard
        box(eye,f,-0.70,-0.83,-0.88,-0.43,-0.46,0.65,dark); // left console
        box(eye,f,0.43,-0.83,-0.88,0.70,-0.46,0.65,dark);
        box(eye,f,-0.70,-1.05,-1.0,0.70,-0.95,0.75,dark); // floor
        box(eye,f,-0.72,-0.50,-0.9,-0.67,0.58,-0.65,rim); // canopy supports
        box(eye,f,0.67,-0.50,-0.9,0.72,0.58,-0.65,rim);
        box(eye,f,-0.72,0.55,-0.9,0.72,0.60,-0.65,rim);
        box(eye,f,-0.42,-0.75,0.35,0.42,0.10,0.53,dark); // seat back
        draw(0,f,nullptr);
        quad(eye,f,{{{-0.575,-0.21,-0.86},{0.575,-0.21,-0.86},{0.575,-0.602,-0.86},{-0.575,-0.602,-0.86}}},white,0,91.0f/200,1,1);
        draw(1,f,&panel);
        /* Prototype HUD: original vector symbology on an aircraft-fixed transparent plane.
         * It is finite-depth, not an optical collimated HUD. This is documented for headset QA. */
        quad(eye,f,{{{-0.6211,0.2708,-1.0},{0.6289,0.2708,-1.0},{0.6289,-0.2031,-1.0},{-0.6211,-0.2031,-1.0}}},white,0,0,1,91.0f/200);
        draw(2,f,&panel);
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
    void tick(const uint32_t *desktop,int w,int h) {
        if(in_tick || exit || !desktop)return;
        const Uint64 now=SDL_GetTicksNS();
        if(preview && lastFrame && now-lastFrame<SDL_NS_PER_SECOND/60)return;
        in_tick=true;
        struct Guard {bool &b;~Guard(){b=false;}} guard{in_tick};
        if(!preview){poll();if(!running)return;}
        const Uint64 started=SDL_GetTicksNS();
        XrFrameState frame{XR_TYPE_FRAME_STATE};bool begun=false;
        if(!preview) {
            XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};xr(xrWaitFrame(session,&wait,&frame),"wait VR frame");
            XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};xr(xrBeginFrame(session,&begin),"begin VR frame");begun=true;
        }
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
        vr_capture_desktop(desktop,w,h,panelPixels.data());upload(panel,panelPixels.data(),320,200);
        bool flight=enh_vr_ready();
        const int size=vr_options.resolution;
        if(flight) {
            enh_vr_frame(); // latch one alpha for both eyes
            for(int i=0;i<2;i++) {
                pixels[i].resize(size_t(size)*size);
                if(!enh_vr_render(&poses[i],pixels[i].data(),size,size)){flight=false;break;}
                upload(world[i],pixels[i].data(),size,size);
            }
            if(flight)dumpEyes(size);
        }
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
        if(mirror) {
            targetSRGB=true;
            context->ClearRenderTargetView(mirrorRT.Get(),clear);context->ClearDepthStencilView(mirrorDepth.Get(),D3D11_CLEAR_DEPTH,1,0);
            // Square eyes, side by side, letterboxed in the square desktop window.
            for(int i=0;i<2;i++)render(i,poses[i],fovs[i],mirrorRT.Get(),mirrorDepth.Get(),float(i*size/2),float(size/4),float(size/2),float(size/2),flight);
            if(flight)dumpMirror(size);
            dx(mirror->Present(0,0),"present stereo mirror");
        }
        lastFrame=SDL_GetTicksNS();frameMS+=(lastFrame-started)/1e6;frames++;
        if(lastFrame-lastStats>5*SDL_NS_PER_SECOND) {
            char b[160];std::snprintf(b,sizeof b,"%s; %u frames, average frame work %.2f ms (includes OpenXR wait)",flight?"Stereo world":"Menu panel",frames,frameMS/std::max(1u,frames));note(b);lastStats=lastFrame;frames=0;frameMS=0;
        }
    }
private:
    ComPtr<ID3D11DepthStencilView> eyeDepth;
    int depthW=0,depthH=0;
    bool mirrorDumped=false;
};
std::unique_ptr<Backend> backend;
bool failed=false;
}

extern "C" bool vr_start(void *hwnd) {
    try {backend=std::make_unique<Backend>();backend->init(static_cast<HWND>(hwnd),vr_options.mode==2);return true;}
    catch(const std::exception &e) {if(backend)backend->note(e.what());backend.reset();return false;}
}
extern "C" void vr_stop(void) {backend.reset();}
extern "C" void vr_tick(const uint32_t *pixels,int w,int h) {
    if(!backend || failed)return;
    try {backend->tick(pixels,w,h);}
    catch(const std::exception &e) {backend->note(e.what());backend->note("VR rendering stopped after an error; close the game and inspect f117-vr.log");failed=true;}
}
extern "C" bool vr_wants_quit(void) {return failed || (backend && backend->exit);}
extern "C" bool vr_key(int sc,bool down,bool repeat) {
    if(!backend)return false;
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
