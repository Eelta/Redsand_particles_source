#pragma once
#include "lightning_geometry.hpp"
#include <d3d11_1.h>
#include <wrl/client.h>
#include <string>

namespace HellbladeTime::VFX {
class Renderer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext1> context;
    Ptr<ID3DDeviceContextState> privateState;
    Ptr<ID3D11VertexShader> vs;
    Ptr<ID3D11VertexShader> fullScreenVS;
    Ptr<ID3D11PixelShader> ps;
    Ptr<ID3D11PixelShader> blurPS;
    Ptr<ID3D11InputLayout> layout;
    Ptr<ID3D11Buffer> vertices;
    Ptr<ID3D11Buffer> blurParameters;
    Ptr<ID3D11BlendState> blend;
    Ptr<ID3D11RasterizerState> raster;
    Ptr<ID3D11DepthStencilState> depth;
    Lightning::Geometry geometry;
    struct GlowTarget {
        Ptr<ID3D11Texture2D> texture;
        Ptr<ID3D11RenderTargetView> view;
        Ptr<ID3D11ShaderResourceView> source;
    } glow[2];
    Ptr<ID3D11SamplerState> sampler;
    unsigned glowWidth=0,glowHeight=0;
    bool ResizeGlow(unsigned width,unsigned height);
public:
    bool Initialize(ID3D11Device* input,std::string& error);
    bool Draw(ID3D11RenderTargetView* target,unsigned width,unsigned height,
        const VisualFrame& frame);
    bool Matches(ID3D11Device* input) const {return device.Get()==input;}
    std::size_t VertexCount() const {return geometry.vertices.size();}
};
}
