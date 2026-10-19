#include "time_vfx_d3d.hpp"
#include <d3dcompiler.h>
#include <cstring>

namespace HellbladeTime::VFX {
namespace {


constexpr char shader[]=R"(
struct Vin {float2 pos:POSITION;float2 uv:TEXCOORD0;float4 light:COLOR0;};
struct Pin {float4 pos:SV_POSITION;float2 uv:TEXCOORD0;float4 light:COLOR0;};
Pin VSMain(Vin v) {Pin p;p.pos=float4(v.pos,0,1);p.uv=v.uv;p.light=v.light;return p;}
float4 PSMain(Pin p):SV_TARGET {
    float d=abs(p.uv.x);
    float aa=max(fwidth(p.uv.x),0.0001);
    float core=1-smoothstep(0.012,0.012+aa*0.85,d);
    float sheath=exp2(-d*d*80.0)*0.65;
    float halo=exp2(-d*d*7.0)*0.32;
    float edge=1-smoothstep(0.8,1.0,d);
    float3 radiance=(float3(0.88,0.95,1.0)*core*1.4+p.light.rgb*(sheath+halo))*p.light.a*edge;
    return float4(radiance,0);
}
struct FullPin {float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};
FullPin FullVS(uint id:SV_VertexID) {
    FullPin p;p.uv=float2((id<<1)&2,id&2);
    p.pos=float4(p.uv*float2(2,-2)+float2(-1,1),0,1);return p;
}
Texture2D Glow:register(t0);SamplerState LinearClamp:register(s0);
cbuffer BlurParameters:register(b0) {float2 direction;float strength;float unused;};
float4 BlurMain(FullPin p):SV_TARGET {
    static const float weights[8]={0.134598,0.127324,0.107778,0.081650,0.055335,0.033560,0.018216,0.008846};
    float3 c=Glow.Sample(LinearClamp,p.uv).rgb*weights[0];
    [unroll] for(int k=1;k<8;++k)
        c+=(Glow.Sample(LinearClamp,p.uv+direction*k).rgb+
            Glow.Sample(LinearClamp,p.uv-direction*k).rgb)*weights[k];
    return float4(c*strength,0);
})";
struct RestoreContext {
    ID3D11DeviceContext1* context;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> previous;
    RestoreContext(ID3D11DeviceContext1* c,ID3DDeviceContextState* state):context(c) {
        context->SwapDeviceContextState(state,previous.GetAddressOf());
    }
    ~RestoreContext() {


        context->OMSetRenderTargets(0,nullptr,nullptr);
        ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);
        context->SwapDeviceContextState(previous.Get(),nullptr);
    }
};
}
bool Renderer::Initialize(ID3D11Device* input,std::string& error) {
    if(!input) {error="No D3D11 device";return false;}

    device.Reset();context.Reset();privateState.Reset();
    vs.Reset();fullScreenVS.Reset();ps.Reset();blurPS.Reset();layout.Reset();vertices.Reset();blurParameters.Reset();
    blend.Reset();raster.Reset();depth.Reset();sampler.Reset();
    for(auto& target:glow) target={};glowWidth=glowHeight=0;
    Ptr<ID3D11Device1> d1;
    Ptr<ID3D11DeviceContext> base;
    input->GetImmediateContext(base.GetAddressOf());
    if(FAILED(input->QueryInterface(IID_PPV_ARGS(d1.GetAddressOf()))) ||
        FAILED(base.As(&context))) {error="D3D11.1 state isolation unavailable";return false;}
    const auto level=input->GetFeatureLevel();
    const unsigned flags=(input->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED) ?
        D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    auto hr=d1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,privateState.GetAddressOf());
    if(FAILED(hr)) {error="Cannot isolate D3D pipeline state";return false;}
    Ptr<ID3DBlob> vsCode,psCode,diagnostics;
    hr=D3DCompile(shader,sizeof(shader)-1,"Hellblade procedural lightning",nullptr,nullptr,"VSMain","vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,vsCode.GetAddressOf(),diagnostics.GetAddressOf());
    if(FAILED(hr)) {error=diagnostics ? std::string(static_cast<char*>(diagnostics->GetBufferPointer()),diagnostics->GetBufferSize()) : "Vertex shader compile failed";return false;}
    diagnostics.Reset();
    hr=D3DCompile(shader,sizeof(shader)-1,"Hellblade procedural lightning",nullptr,nullptr,"PSMain","ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,psCode.GetAddressOf(),diagnostics.GetAddressOf());
    if(FAILED(hr)) {error=diagnostics ? std::string(static_cast<char*>(diagnostics->GetBufferPointer()),diagnostics->GetBufferSize()) : "Pixel shader compile failed";return false;}
    if(FAILED(input->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.GetAddressOf())) ||
        FAILED(input->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.GetAddressOf()))) {error="Cannot create lightning shaders";return false;}
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(input->CreateInputLayout(elements,3,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.GetAddressOf()))) {error="Cannot create lightning vertex layout";return false;}
    Ptr<ID3DBlob> fullCode,blurCode;
    diagnostics.Reset();
    hr=D3DCompile(shader,sizeof(shader)-1,"Hellblade procedural glow",nullptr,nullptr,"FullVS","vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,fullCode.GetAddressOf(),diagnostics.GetAddressOf());
    if(FAILED(hr)) {error="Fullscreen vertex shader compile failed";return false;}
    diagnostics.Reset();
    hr=D3DCompile(shader,sizeof(shader)-1,"Hellblade procedural glow",nullptr,nullptr,"BlurMain","ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blurCode.GetAddressOf(),diagnostics.GetAddressOf());
    if(FAILED(hr)) {error="Glow blur shader compile failed";return false;}
    if(FAILED(input->CreateVertexShader(fullCode->GetBufferPointer(),fullCode->GetBufferSize(),nullptr,fullScreenVS.GetAddressOf())) ||
        FAILED(input->CreatePixelShader(blurCode->GetBufferPointer(),blurCode->GetBufferSize(),nullptr,blurPS.GetAddressOf()))) {error="Cannot create glow shaders";return false;}
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=unsigned(Lightning::MaxVertices*sizeof(Lightning::Vertex));
    buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    D3D11_BLEND_DESC blending{};auto& rt=blending.RenderTarget[0];
    rt.BlendEnable=true;rt.SrcBlend=D3D11_BLEND_ONE;rt.DestBlend=D3D11_BLEND_ONE;rt.BlendOp=D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha=D3D11_BLEND_ZERO;rt.DestBlendAlpha=D3D11_BLEND_ONE;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_RASTERIZER_DESC rasterizer{};rasterizer.FillMode=D3D11_FILL_SOLID;rasterizer.CullMode=D3D11_CULL_NONE;rasterizer.DepthClipEnable=true;
    D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=false;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampling.MaxLOD=D3D11_FLOAT32_MAX;sampling.ComparisonFunc=D3D11_COMPARISON_NEVER;
    if(FAILED(input->CreateBuffer(&buffer,nullptr,vertices.GetAddressOf())) ||
        FAILED(input->CreateBlendState(&blending,blend.GetAddressOf())) ||
        FAILED(input->CreateRasterizerState(&rasterizer,raster.GetAddressOf())) ||
        FAILED(input->CreateDepthStencilState(&z,depth.GetAddressOf())) ||
        FAILED(input->CreateBuffer(&cb,nullptr,blurParameters.GetAddressOf())) ||
        FAILED(input->CreateSamplerState(&sampling,sampler.GetAddressOf()))) {error="Cannot allocate lightning render resources";return false;}
    device=input;return true;
}
bool Renderer::ResizeGlow(unsigned width,unsigned height) {
    width=std::max(1u,width/2);height=std::max(1u,height/2);
    if(width==glowWidth && height==glowHeight) return true;
    for(auto& target:glow) target={};glowWidth=glowHeight=0;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.MipLevels=desc.ArraySize=1;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    for(auto& target:glow) {
        if(FAILED(device->CreateTexture2D(&desc,nullptr,target.texture.GetAddressOf())) ||
            FAILED(device->CreateRenderTargetView(target.texture.Get(),nullptr,target.view.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(target.texture.Get(),nullptr,target.source.GetAddressOf()))) return false;
    }
    glowWidth=width;glowHeight=height;return true;
}
bool Renderer::Draw(ID3D11RenderTargetView* target,unsigned width,unsigned height,const VisualFrame& frame) {
    if(!device || !target || !width || !height) return false;
    const float aspect=float(width)/float(height);
    geometry.Build(frame,aspect);
    if(geometry.vertices.empty()) return true;
    if(!ResizeGlow(width,height)) return false;


    RestoreContext restore(context.Get(),privateState.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
    auto* output=static_cast<Lightning::Vertex*>(mapped.pData);
    for(const auto& vertex:geometry.vertices) { *output=vertex;output->x/=aspect;++output; }
    context->Unmap(vertices.Get(),0);
    const unsigned stride=sizeof(Lightning::Vertex),offset=0;
    ID3D11Buffer* vb=vertices.Get();
    context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    const D3D11_VIEWPORT viewport={0,0,float(width),float(height),0,1};
    const D3D11_VIEWPORT halfViewport={0,0,float(glowWidth),float(glowHeight),0,1};
    context->RSSetViewports(1,&halfViewport);context->RSSetState(raster.Get());
    context->OMSetDepthStencilState(depth.Get(),0);
    context->OMSetBlendState(blend.Get(),nullptr,0xffffffff);
    const float clear[4]={};
    context->ClearRenderTargetView(glow[0].view.Get(),clear);
    context->ClearRenderTargetView(glow[1].view.Get(),clear);
    auto* first=glow[0].view.Get();context->OMSetRenderTargets(1,&first,nullptr);
    context->Draw(unsigned(geometry.vertices.size()),0);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(fullScreenVS.Get(),nullptr,0);context->PSSetShader(blurPS.Get(),nullptr,0);
    ID3D11SamplerState* sampling=sampler.Get();context->PSSetSamplers(0,1,&sampling);
    auto* cb=blurParameters.Get();context->PSSetConstantBuffers(0,1,&cb);
    for(unsigned pass=0;pass<2;++pass) {
        auto* outputTarget=pass ? target : glow[1].view.Get();
        context->OMSetRenderTargets(1,&outputTarget,nullptr);
        if(pass) context->RSSetViewports(1,&viewport);
        if(FAILED(context->Map(blurParameters.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
        const float params[4]={pass ? 0.f : 1.f/float(glowWidth),pass ? 1.f/float(glowHeight) : 0.f,pass ? 1.1f : 1.f,0};
        std::memcpy(mapped.pData,params,sizeof(params));context->Unmap(blurParameters.Get(),0);
        auto* resource=glow[pass].source.Get();context->PSSetShaderResources(0,1,&resource);
        context->Draw(3,0);
        ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);
    }
    context->IASetInputLayout(layout.Get());context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    context->Draw(unsigned(geometry.vertices.size()),0);

    context->OMSetRenderTargets(0,nullptr,nullptr);
    return true;
}
}
