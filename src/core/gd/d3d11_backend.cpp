// Direct3D 11 backend (docs/renderer.md, "D3D11 driver").
//
// d3d11.dll and d3dcompiler_47.dll are loaded when the backend starts, so
// core.dll does not depend on them unless the driver is used.
#include "core/gd/backend.h"

#include "core/log.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace opensc4::gd {
namespace {

using Microsoft::WRL::ComPtr;

// Full-screen helpers: a triangle covering the viewport, then
//   PSClear: a constant colour (depth comes from the vertex shader),
//   PSCopyDepth: depth from another depth texture, offset by `shift`.
constexpr char kUtilityShaders[] = R"(
cbuffer Params : register(b0) {
    float4 color;
    float depth;
    float3 pad;
    int2 shift;
    int2 pad2;
};
Texture2D<float> source : register(t0);

float4 VSFull(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
}
float4 PSClear() : SV_Target { return color; }
float PSCopyDepth(float4 pos : SV_Position) : SV_Depth {
    return source.Load(int3(int2(pos.xy) + shift, 0));
}
)";

struct UtilityParams {
    float color[4];
    float depth;
    float pad[3];
    int32_t shift[2];
    int32_t pad2[2];
};

using CreateDeviceFn = decltype(&D3D11CreateDevice);
using CompileFn = decltype(&D3DCompile);

bool Clip(Rect& r, uint32_t width, uint32_t height)
{
    const int32_t x0 = std::max(r.x, 0), y0 = std::max(r.y, 0);
    const int32_t x1 = std::min(r.x + r.width, static_cast<int32_t>(width));
    const int32_t y1 = std::min(r.y + r.height, static_cast<int32_t>(height));
    r = {x0, y0, x1 - x0, y1 - y0};
    return r.width > 0 && r.height > 0;
}

struct DepthTarget {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> srv;
};

struct Region {
    Surface surface;
    ComPtr<ID3D11Texture2D> color; // front or back buffer regions
    DepthTarget depth;             // depth buffer regions
};

class D3D11Backend final : public Backend {
public:
    ~D3D11Backend() override
    {
        if (context_)
            context_->ClearState();
    }

    bool Init(HWND window, uint32_t width, uint32_t height) override
    {
        width_ = width;
        height_ = height;
        HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
        HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        auto createDevice = d3d11 ? reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d11, "D3D11CreateDevice")) : nullptr;
        compile_ = compiler ? reinterpret_cast<CompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
        if (!createDevice || !compile_) {
            log::Write("d3d11: d3d11.dll or d3dcompiler_47.dll is missing");
            return false;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        char debug[8];
        if (GetEnvironmentVariableA("OPENSC4_D3D11_DEBUG", debug, sizeof(debug)) && debug[0] == '1')
            flags |= D3D11_CREATE_DEVICE_DEBUG;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL level{};
        HRESULT hr = createDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                  D3D11_SDK_VERSION, &device_, &level, &context_);
        if (FAILED(hr)) {
            log::Write("d3d11: D3D11CreateDevice failed (0x%08lX)", hr);
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> factory;
        device_.As(&dxgiDevice);
        dxgiDevice->GetAdapter(&adapter);
        adapter->GetParent(IID_PPV_ARGS(&factory));
        DXGI_ADAPTER_DESC adapterDesc{};
        adapter->GetDesc(&adapterDesc);

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = width;
        desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = factory->CreateSwapChain(device_.Get(), &desc, &swapChain_);
        if (FAILED(hr)) {
            log::Write("d3d11: CreateSwapChain failed (0x%08lX)", hr);
            return false;
        }
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        swapChain_->GetBuffer(0, IID_PPV_ARGS(&swapBuffer_));

        if (!CreateColor(back_, true) || !CreateColor(front_, false) || !CreateDepth(depth_) || !CreateUtilities()) {
            log::Write("d3d11: could not create the frame buffers");
            return false;
        }
        device_->CreateRenderTargetView(back_.Get(), nullptr, &backRtv_);

        char name[128];
        WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, name, sizeof(name), nullptr, nullptr);
        log::Write("d3d11: device on %s (feature level %X), %ux%u", name, static_cast<unsigned>(level), width, height);
        return true;
    }

    uint32_t Width() const override { return width_; }
    uint32_t Height() const override { return height_; }

    void Clear(const Rect& rect, uint32_t mask, const float color[4], float depth, uint8_t stencil) override
    {
        Rect r = rect;
        if (!(mask & (kClearColor | kClearDepth | kClearStencil)) || !Clip(r, width_, height_))
            return;
        UtilityParams params{};
        std::memcpy(params.color, color, sizeof(params.color));
        params.depth = depth;
        context_->UpdateSubresource(utilityParams_.Get(), 0, nullptr, &params, 0, 0);

        const bool clearColor = mask & kClearColor;
        const int dsIndex = ((mask & kClearDepth) ? 1 : 0) | ((mask & kClearStencil) ? 2 : 0);
        BeginUtility(r);
        context_->OMSetRenderTargets(1, backRtv_.GetAddressOf(), depth_.dsv.Get());
        context_->OMSetBlendState(clearColor ? blendWrite_.Get() : blendNoWrite_.Get(), nullptr, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(clearDepthStencil_[dsIndex].Get(), stencil);
        context_->PSSetShader(psClear_.Get(), nullptr, 0);
        context_->Draw(3, 0);
    }

    void Present() override
    {
        context_->CopyResource(front_.Get(), back_.Get());
        context_->CopyResource(swapBuffer_.Get(), back_.Get());
        swapChain_->Present(0, 0);
    }

    int32_t CreateRegion(Surface surface) override
    {
        Region region{surface, {}, {}};
        const bool ok = surface == DepthBuffer ? CreateDepth(region.depth) : CreateColor(region.color, false);
        if (!ok)
            return 0;
        const int32_t handle = nextRegion_++;
        regions_.emplace(handle, std::move(region));
        return handle;
    }

    bool DeleteRegion(int32_t region) override { return regions_.erase(region) > 0; }

    bool IsRegion(int32_t region) const override { return regions_.count(region) > 0; }

    bool ReadRegion(int32_t handle, const Rect& source, int32_t regionX, int32_t regionY) override
    {
        auto it = regions_.find(handle);
        if (it == regions_.end())
            return false;
        Region& region = it->second;
        if (region.surface == DepthBuffer)
            return CopyDepth(depth_, region.depth, source, regionX, regionY);
        return CopyColor(SurfaceTexture(region.surface), region.color.Get(), source, regionX, regionY);
    }

    bool DrawRegion(int32_t handle, const Rect& target, int32_t regionX, int32_t regionY) override
    {
        auto it = regions_.find(handle);
        if (it == regions_.end())
            return false;
        Region& region = it->second;
        const Rect source{regionX, regionY, target.width, target.height};
        if (region.surface == DepthBuffer)
            return CopyDepth(region.depth, depth_, source, target.x, target.y);
        const bool ok = CopyColor(region.color.Get(), SurfaceTexture(region.surface), source, target.x, target.y);
        if (ok && region.surface == FrontBuffer) {
            // The front buffer is what the window shows.
            context_->CopyResource(swapBuffer_.Get(), front_.Get());
            swapChain_->Present(0, 0);
        }
        return ok;
    }

    bool ReadFront(const Rect& rect, uint8_t* pixels, uint32_t pitch) override
    {
        Rect r = rect;
        if (!Clip(r, width_, height_))
            return false;
        if (!staging_) {
            D3D11_TEXTURE2D_DESC desc = ColorDesc(false);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging_)))
                return false;
        }
        const D3D11_BOX box{static_cast<UINT>(r.x), static_cast<UINT>(r.y), 0, static_cast<UINT>(r.x + r.width),
                            static_cast<UINT>(r.y + r.height), 1};
        context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, front_.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        // Rows of the requested rectangle that fell outside the buffer stay as they were.
        uint8_t* out = pixels + (r.y - rect.y) * pitch + (r.x - rect.x) * 4;
        for (int32_t y = 0; y < r.height; ++y)
            std::memcpy(out + y * pitch, static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, r.width * 4);
        context_->Unmap(staging_.Get(), 0);
        return true;
    }

private:
    D3D11_TEXTURE2D_DESC ColorDesc(bool renderTarget) const
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (renderTarget ? D3D11_BIND_RENDER_TARGET : 0);
        return desc;
    }

    bool CreateColor(ComPtr<ID3D11Texture2D>& texture, bool renderTarget)
    {
        const D3D11_TEXTURE2D_DESC desc = ColorDesc(renderTarget);
        return SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &texture));
    }

    bool CreateDepth(DepthTarget& target)
    {
        D3D11_TEXTURE2D_DESC desc = ColorDesc(false);
        desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &target.texture)))
            return false;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        return SUCCEEDED(device_->CreateDepthStencilView(target.texture.Get(), &dsv, &target.dsv)) &&
               SUCCEEDED(device_->CreateShaderResourceView(target.texture.Get(), &srv, &target.srv));
    }

    ComPtr<ID3DBlob> Compile(const char* entry, const char* profile)
    {
        ComPtr<ID3DBlob> code, errors;
        const HRESULT hr = compile_(kUtilityShaders, sizeof(kUtilityShaders) - 1, "opensc4_utility", nullptr,
                                    nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr))
            log::Write("d3d11: compiling %s failed: %s", entry,
                       errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        return code;
    }

    bool CreateUtilities()
    {
        ComPtr<ID3DBlob> vs = Compile("VSFull", "vs_4_0"), clear = Compile("PSClear", "ps_4_0"),
                         copyDepth = Compile("PSCopyDepth", "ps_4_0");
        if (!vs || !clear || !copyDepth)
            return false;
        device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vsFull_);
        device_->CreatePixelShader(clear->GetBufferPointer(), clear->GetBufferSize(), nullptr, &psClear_);
        device_->CreatePixelShader(copyDepth->GetBufferPointer(), copyDepth->GetBufferSize(), nullptr, &psCopyDepth_);

        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(UtilityParams);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        device_->CreateBuffer(&cb, nullptr, &utilityParams_);

        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device_->CreateBlendState(&blend, &blendWrite_);
        blend.RenderTarget[0].RenderTargetWriteMask = 0;
        device_->CreateBlendState(&blend, &blendNoWrite_);

        // [bit 0: write depth, bit 1: write stencil]
        for (int i = 0; i < 4; ++i) {
            D3D11_DEPTH_STENCIL_DESC ds{};
            ds.DepthEnable = (i & 1) != 0;
            ds.DepthWriteMask = (i & 1) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
            ds.StencilEnable = (i & 2) != 0;
            ds.StencilReadMask = 0xFF;
            ds.StencilWriteMask = 0xFF;
            ds.FrontFace = {D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE,
                            D3D11_COMPARISON_ALWAYS};
            ds.BackFace = ds.FrontFace;
            device_->CreateDepthStencilState(&ds, &clearDepthStencil_[i]);
        }

        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.ScissorEnable = TRUE;
        raster.DepthClipEnable = TRUE;
        device_->CreateRasterizerState(&raster, &utilityRaster_);
        return utilityParams_ && blendWrite_ && blendNoWrite_ && utilityRaster_;
    }

    // Pipeline state for a full-viewport utility draw into `r`.
    void BeginUtility(const Rect& r)
    {
        const D3D11_VIEWPORT viewport{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.width),
                                      static_cast<float>(r.height), 0.0f, 1.0f};
        const D3D11_RECT scissor{r.x, r.y, r.x + r.width, r.y + r.height};
        context_->RSSetViewports(1, &viewport);
        context_->RSSetScissorRects(1, &scissor);
        context_->RSSetState(utilityRaster_.Get());
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vsFull_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, utilityParams_.GetAddressOf());
        context_->PSSetConstantBuffers(0, 1, utilityParams_.GetAddressOf());
    }

    ID3D11Texture2D* SurfaceTexture(Surface surface) const
    {
        return surface == FrontBuffer ? front_.Get() : back_.Get();
    }

    bool CopyColor(ID3D11Texture2D* from, ID3D11Texture2D* to, const Rect& source, int32_t x, int32_t y)
    {
        Rect s = source;
        if (!Clip(s, width_, height_))
            return false;
        x += s.x - source.x;
        y += s.y - source.y;
        Rect t{x, y, s.width, s.height};
        if (!Clip(t, width_, height_))
            return false;
        s.x += t.x - x;
        s.y += t.y - y;
        const D3D11_BOX box{static_cast<UINT>(s.x), static_cast<UINT>(s.y), 0, static_cast<UINT>(s.x + t.width),
                            static_cast<UINT>(s.y + t.height), 1};
        context_->CopySubresourceRegion(to, 0, t.x, t.y, 0, from, 0, &box);
        return true;
    }

    // Depth-stencil resources cannot be copied in part, so draw instead.
    bool CopyDepth(DepthTarget& from, DepthTarget& to, const Rect& source, int32_t x, int32_t y)
    {
        Rect t{x, y, source.width, source.height};
        if (!Clip(t, width_, height_))
            return false;
        UtilityParams params{};
        params.shift[0] = source.x - x;
        params.shift[1] = source.y - y;
        context_->UpdateSubresource(utilityParams_.Get(), 0, nullptr, &params, 0, 0);
        ID3D11RenderTargetView* noTarget = nullptr;
        context_->OMSetRenderTargets(1, &noTarget, to.dsv.Get());
        context_->OMSetBlendState(blendNoWrite_.Get(), nullptr, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(clearDepthStencil_[1].Get(), 0);
        BeginUtility(t);
        context_->PSSetShader(psCopyDepth_.Get(), nullptr, 0);
        context_->PSSetShaderResources(0, 1, from.srv.GetAddressOf());
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr;
        context_->PSSetShaderResources(0, 1, &none);
        return true;
    }

    uint32_t width_ = 0, height_ = 0;
    CompileFn compile_ = nullptr;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swapChain_;
    ComPtr<ID3D11Texture2D> swapBuffer_;
    ComPtr<ID3D11Texture2D> back_, front_, staging_;
    ComPtr<ID3D11RenderTargetView> backRtv_;
    DepthTarget depth_;

    ComPtr<ID3D11VertexShader> vsFull_;
    ComPtr<ID3D11PixelShader> psClear_, psCopyDepth_;
    ComPtr<ID3D11Buffer> utilityParams_;
    ComPtr<ID3D11BlendState> blendWrite_, blendNoWrite_;
    ComPtr<ID3D11DepthStencilState> clearDepthStencil_[4];
    ComPtr<ID3D11RasterizerState> utilityRaster_;

    std::map<int32_t, Region> regions_;
    int32_t nextRegion_ = 1;
};

} // namespace

std::unique_ptr<Backend> CreateD3D11Backend()
{
    return std::make_unique<D3D11Backend>();
}

} // namespace opensc4::gd
