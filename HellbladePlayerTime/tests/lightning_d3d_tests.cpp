#include "time_vfx_d3d.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
void Check(bool value) {if(!value) throw std::runtime_error("Offscreen D3D11 lightning regression");}
void HR(HRESULT value) {Check(SUCCEEDED(value));}
int main(int argc,char** argv) {
    using namespace HellbladeTime;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    HR(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,device.GetAddressOf(),nullptr,context.GetAddressOf()));
    VFX::Renderer renderer;std::string error;
    if(!renderer.Initialize(device.Get(),error)) throw std::runtime_error(error);
    const bool preview=argc>1;
    const unsigned width=preview ? 1280 : 640,height=preview ? 720 : 360;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture,sentinel,readback;
    HR(device->CreateTexture2D(&desc,nullptr,texture.GetAddressOf()));HR(device->CreateTexture2D(&desc,nullptr,sentinel.GetAddressOf()));
    ComPtr<ID3D11RenderTargetView> target,oldTarget;
    HR(device->CreateRenderTargetView(texture.Get(),nullptr,target.GetAddressOf()));HR(device->CreateRenderTargetView(sentinel.Get(),nullptr,oldTarget.GetAddressOf()));
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    HR(device->CreateTexture2D(&desc,nullptr,readback.GetAddressOf()));
    ID3D11RenderTargetView* priorTarget=oldTarget.Get();context->OMSetRenderTargets(1,&priorTarget,nullptr);
    D3D11_VIEWPORT priorViewport={7,9,311,177,.1f,.9f};context->RSSetViewports(1,&priorViewport);
    const float background[]={.009f,.014f,.028f,.42f};
    VisualFrame frame{{true,0,1.5f,437,Source::Attack},{}};
    SessionColors colors{437};
    std::filesystem::path folder=preview ? argv[1] : "";
    if(preview) std::filesystem::create_directories(folder);
    unsigned totalLit=0,totalWhite=0;const auto start=std::chrono::steady_clock::now();
    const unsigned frames=preview ? 360 : 12;
    for(unsigned i=0;i<frames;++i) {
        frame.session.elapsed=preview ? float(i%120)/60.f : float(i)*.12f;
        frame.session.serial=437+(preview ? i/120 : 0);
        frame.options={};colors.Apply(frame.session,frame.options);
        context->ClearRenderTargetView(target.Get(),background);
        const auto refsBefore=target->AddRef();target->Release();
        Check(renderer.Draw(target.Get(),width,height,frame));
        const auto refsAfter=target->AddRef();target->Release();Check(refsBefore==refsAfter);
        ComPtr<ID3D11RenderTargetView> restored;
        context->OMGetRenderTargets(1,restored.GetAddressOf(),nullptr);Check(restored.Get()==priorTarget);
        D3D11_VIEWPORT viewport;unsigned viewportCount=1;context->RSGetViewports(&viewportCount,&viewport);
        Check(viewport.TopLeftX==7 && viewport.TopLeftY==9 && viewport.Width==311 && viewport.MaxDepth==.9f);
        context->CopyResource(readback.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped;
        HR(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        std::ofstream ppm;
        if(preview) {
            char name[40];std::snprintf(name,sizeof(name),"frame-%04u.ppm",i);
            ppm.open(folder/name,std::ios::binary);ppm<<"P6\n"<<width<<" "<<height<<"\n255\n";
        }
        unsigned lit=0,white=0;
        for(unsigned y=0;y<height;++y) {
            const auto* row=static_cast<const unsigned char*>(mapped.pData)+std::size_t(y)*mapped.RowPitch;
            for(unsigned x=0;x<width;++x) {
                const auto* pixel=row+x*4;
                Check(pixel[3]>=106 && pixel[3]<=108);
                lit+=pixel[2]>40;white+=pixel[0]>250 && pixel[1]>250 && pixel[2]>250;
                if(preview) ppm.write(reinterpret_cast<const char*>(pixel),3);
            }
        }
        context->Unmap(readback.Get(),0);
        if(i==0 || frame.session.elapsed>=1.5f) Check(lit==0);
        totalLit+=lit;totalWhite+=white;
    }
    Check(totalLit>1000 && totalWhite<float(width)*height*frames*.05f);
    std::cout<<"Actual D3D11 WARP shaders/geometry rendered; render target and viewport restored, alpha preserved, start/end clean. Frames="<<frames<<", bright pixels="<<totalLit<<", saturated-white pixels="<<totalWhite<<", offscreen runtime="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"s. This is not a Skyrim/ENB capture.\n";
}
