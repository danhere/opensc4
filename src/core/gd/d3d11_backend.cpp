// Direct3D 11 backend (docs/renderer.md, "D3D11 driver").
//
// d3d11.dll and d3dcompiler_47.dll are loaded when the backend starts, so
// core.dll does not depend on them unless the driver is used.
#include "core/gd/backend.h"

#include "core/gd/d3d11_shaders.h"
#include "core/gd/pixels.h"
#include "core/gd/vertex_format.h"
#include "core/log.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

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

// Per-draw constants; must match the cbuffer in d3d11_shaders.h.
struct StageConstants {
    uint32_t setup[4];
    uint32_t rgb[4];
    uint32_t alpha[4];
    float scale[4];
    float matrix[16];
};
struct DrawConstants {
    float modelView[16];
    float projection[16];
    float colorMul[4];
    float texFactor[4];
    float fogColor[4];
    float fogParams[4];
    uint32_t flags[4];
    uint32_t alphaTest[4];
    StageConstants stages[kMaxStages];
};

// Values for vertex elements a format lacks (vertex buffer slot 1, stride 0).
struct VertexDefaults {
    float normal[3] = {0, 0, 1};
    uint32_t color = 0xFFFFFFFF;
    float texCoords[kMaxStages][2] = {};
};

struct Texture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    uint32_t width, height, levels;
    bool compressed;      // BC1-3, uploaded as blocks
    bool hasAlpha;        // false for RGB5/RGB8: sampled alpha is 1, whatever the uploads hold
    uint32_t format;      // CreateTexture's
    uint32_t blockFormat; // the DXT PixelFormat when compressed
};

constexpr UINT kVertexRing = 8 << 20;
constexpr UINT kIndexRing = 2 << 20;

D3D11_COMPARISON_FUNC Comparison(uint32_t func)
{
    return static_cast<D3D11_COMPARISON_FUNC>(D3D11_COMPARISON_NEVER + (func & 7));
}

D3D11_BLEND BlendFactor(uint32_t factor, bool alpha)
{
    static const D3D11_BLEND color[] = {
        D3D11_BLEND_ZERO,       D3D11_BLEND_ONE,           D3D11_BLEND_SRC_COLOR,  D3D11_BLEND_INV_SRC_COLOR,
        D3D11_BLEND_SRC_ALPHA,  D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA,
        D3D11_BLEND_DEST_COLOR, D3D11_BLEND_INV_DEST_COLOR, D3D11_BLEND_SRC_ALPHA_SAT,
    };
    // The alpha channel cannot use colour factors: use their alpha equivalents.
    static const D3D11_BLEND forAlpha[] = {
        D3D11_BLEND_ZERO,       D3D11_BLEND_ONE,           D3D11_BLEND_SRC_ALPHA,  D3D11_BLEND_INV_SRC_ALPHA,
        D3D11_BLEND_SRC_ALPHA,  D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA,
        D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA, D3D11_BLEND_SRC_ALPHA_SAT,
    };
    if (factor > 10)
        factor = 1;
    return alpha ? forAlpha[factor] : color[factor];
}

D3D11_STENCIL_OP StencilOperation(uint32_t op)
{
    switch (op) {
    case StencilZero: return D3D11_STENCIL_OP_ZERO;
    case StencilReplace: return D3D11_STENCIL_OP_REPLACE;
    case StencilIncrement: return D3D11_STENCIL_OP_INCR_SAT;
    case StencilDecrement: return D3D11_STENCIL_OP_DECR_SAT;
    case StencilInvert: return D3D11_STENCIL_OP_INVERT;
    default: return D3D11_STENCIL_OP_KEEP;
    }
}

// Logs the first occurrence of each distinct value of something the game does.
void LogOnce(const char* what, uint64_t key, const char* format, ...)
{
    // At most 64 distinct values per kind, so a long session cannot flood the log.
    static std::unordered_map<std::string, std::unordered_map<uint64_t, int>> seen;
    auto& values = seen[what];
    if (values.size() >= 64 || values[key]++ != 0)
        return;
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    log::Write("d3d11: first %s: %s", what, text);
}

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
        if (frameReady_)
            CloseHandle(frameReady_);
        log::Write("d3d11: %u frames shown", shown_);
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
        // The window shows at most one frame per vertical blank unless
        // OPENSC4_D3D11_VSYNC=0 (then every Flush, as the stock drivers do).
        char vsync[8];
        syncInterval_ = GetEnvironmentVariableA("OPENSC4_D3D11_VSYNC", vsync, sizeof(vsync)) && vsync[0] == '0' ? 0 : 1;
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

        // A flip-model swap chain with a waitable object (Windows 8.1 and
        // later), so presenting can be paced to the display without blocking
        // the game; else the blt model, presenting on every Flush.
        ComPtr<IDXGIFactory2> factory2;
        factory.As(&factory2);
        ComPtr<IDXGIFactory5> factory5;
        BOOL tearing = FALSE;
        if (SUCCEEDED(factory.As(&factory5)))
            factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
        if (factory2) {
            DXGI_SWAP_CHAIN_DESC1 desc1{};
            desc1.Width = width;
            desc1.Height = height;
            desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc1.SampleDesc.Count = 1;
            desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc1.BufferCount = 2;
            desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc1.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                          (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
            ComPtr<IDXGISwapChain1> chain1;
            if (SUCCEEDED(factory2->CreateSwapChainForHwnd(device_.Get(), window, &desc1, nullptr, nullptr, &chain1))) {
                ComPtr<IDXGISwapChain2> chain2;
                if (SUCCEEDED(chain1.As(&chain2))) {
                    chain2->SetMaximumFrameLatency(1);
                    frameReady_ = chain2->GetFrameLatencyWaitableObject();
                }
                chain1.As(&swapChain_);
                tearing_ = tearing != FALSE;
            }
        }
        if (!swapChain_) {
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
        }
        log::Write("d3d11: %s swap chain, %s", frameReady_ ? "flip-model" : "blt-model",
                   !syncInterval_ ? (tearing_ ? "every Flush shown at once (tearing allowed)" : "every Flush shown")
                   : frameReady_ ? "paced to the display" : "every Flush shown, waiting for vertical sync");
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        swapChain_->GetBuffer(0, IID_PPV_ARGS(&swapBuffer_));

        if (!CreateColor(back_, true) || !CreateColor(front_, false) || !CreateDepth(depth_) || !CreateUtilities() ||
            !CreateDrawPipeline()) {
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
        frontPending_ = true;
        ShowFront();
        LimitQueue();
    }

    // Keeps the CPU at most kQueuedFlushes flushes ahead of the GPU. When
    // presenting is paced nothing else blocks, and the game would queue GPU
    // work (and delay the frames it shows) without bound.
    void LimitQueue()
    {
        ComPtr<ID3D11Query>& query = flushQueries_[flushQuery_];
        flushQuery_ = (flushQuery_ + 1) % kQueuedFlushes;
        if (query) {
            while (context_->GetData(query.Get(), nullptr, 0, 0) == S_FALSE)
                SwitchToThread();
        } else {
            const D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
            if (FAILED(device_->CreateQuery(&desc, &query)))
                return;
        }
        context_->End(query.Get());
    }

    // The front buffer goes to the window when the display can take a frame:
    // the game flushes many times per view update (about 800 times a second),
    // so waiting for vertical sync in Flush would starve its loop and the
    // simulation with it. A frame that is not shown yet waits for the next
    // Flush or ShowFront call.
    bool ShowFront() override
    {
        if (!frontPending_)
            return false;
        const bool paced = syncInterval_ && frameReady_;
        if (paced && WaitForSingleObject(frameReady_, 0) != WAIT_OBJECT_0)
            return true;
        context_->CopyResource(swapBuffer_.Get(), front_.Get());
        swapChain_->Present(paced ? 1 : 0, !syncInterval_ && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
        frontPending_ = false;
        ++shown_;
        return false;
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
            frontPending_ = true;
            ShowFront();
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

    uint32_t CreateTexture(uint32_t format, uint32_t width, uint32_t height, uint32_t levels) override
    {
        if (width == 0 || height == 0)
            return 0;
        uint32_t maxLevels = 1;
        for (uint32_t w = width, h = height; w > 1 || h > 1; w = std::max(w / 2, 1u), h = std::max(h / 2, 1u))
            ++maxLevels;
        levels = std::min(std::max(levels, 1u), maxLevels);

        Texture t{};
        t.width = width;
        t.height = height;
        t.levels = levels;
        t.hasAlpha = format != TexRGB5 && format != TexRGB8;
        t.format = format;
        DXGI_FORMAT dxgi = DXGI_FORMAT_B8G8R8A8_UNORM;
        // Block-compressed textures need a top level of whole blocks; others are decoded on upload.
        if ((format == TexDXT1A || format == TexDXT3 || format == TexDXT5) && width % 4 == 0 && height % 4 == 0) {
            t.compressed = true;
            t.blockFormat = format == TexDXT1A ? DXT1A : format == TexDXT3 ? DXT3 : DXT5;
            dxgi = format == TexDXT1A ? DXGI_FORMAT_BC1_UNORM
                   : format == TexDXT3 ? DXGI_FORMAT_BC2_UNORM
                                       : DXGI_FORMAT_BC3_UNORM;
        }
        LogOnce("texture format", format, "%u (%ux%u, %u levels)", format, width, height, levels);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = levels;
        desc.ArraySize = 1;
        desc.Format = dxgi;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &t.texture)) ||
            FAILED(device_->CreateShaderResourceView(t.texture.Get(), nullptr, &t.srv)))
            return 0;
        const uint32_t handle = nextTexture_++;
        textures_.emplace(handle, std::move(t));
        return handle;
    }

    void DeleteTexture(uint32_t texture) override { textures_.erase(texture); }

    void UploadTexture(uint32_t texture, uint32_t level, int32_t x, int32_t y, int32_t width, int32_t height,
                       uint32_t format, uint32_t type, uint32_t rowLength, const void* pixels) override
    {
        auto it = textures_.find(texture);
        if (it == textures_.end() || !pixels || width <= 0 || height <= 0 || x < 0 || y < 0)
            return;
        Texture& t = it->second;
        if (level >= t.levels)
            return;
        LogOnce("upload", (static_cast<uint64_t>(format) << 32) | type, "format %u, type %u", format, type);
        const uint32_t levelWidth = std::max(t.width >> level, 1u), levelHeight = std::max(t.height >> level, 1u);
        const uint32_t w = std::min(static_cast<uint32_t>(width), levelWidth - std::min<uint32_t>(x, levelWidth));
        const uint32_t h = std::min(static_cast<uint32_t>(height), levelHeight - std::min<uint32_t>(y, levelHeight));
        if (w == 0 || h == 0)
            return;
        D3D11_BOX box{static_cast<UINT>(x), static_cast<UINT>(y), 0, static_cast<UINT>(x) + w,
                      static_cast<UINT>(y) + h, 1};

        if (t.compressed) {
            if (!IsCompressed(format) || BlockBytes(format) != BlockBytes(t.blockFormat) || x % 4 || y % 4) {
                LogOnce("unsupported upload", (static_cast<uint64_t>(format) << 32) | type,
                        "format %u into a block-compressed texture at (%d, %d)", format, x, y);
                return;
            }
            // Boxes cover whole blocks, except where they end at the level's edge.
            box.right = std::min(box.left + ((w + 3) & ~3u), std::max(levelWidth, box.left + w));
            box.bottom = std::min(box.top + ((h + 3) & ~3u), std::max(levelHeight, box.top + h));
            const UINT pitch = ((static_cast<UINT>(width) + 3) / 4) * BlockBytes(format);
            context_->UpdateSubresource(t.texture.Get(), level, &box, pixels, pitch, 0);
            return;
        }

        scratch_.resize(static_cast<size_t>(width) * height * 4);
        if (IsCompressed(format)) {
            DecodeDXT(format, pixels, width, height, scratch_.data(), width * 4);
        } else if (!ConvertToBGRA(format, type, pixels, width, height, rowLength, scratch_.data(), width * 4)) {
            LogOnce("unsupported upload", (static_cast<uint64_t>(format) << 32) | type, "format %u, type %u", format,
                    type);
            return;
        }
        if (!t.hasAlpha)
            for (size_t i = 3; i < scratch_.size(); i += 4)
                scratch_[i] = 255;
        context_->UpdateSubresource(t.texture.Get(), level, &box, scratch_.data(), width * 4, 0);
    }

    void Draw(const DrawState& state, const DrawCall& call) override
    {
        if (!call.vertices || call.count == 0)
            return;
        LogOnce("primitive", call.primitive, "%u", call.primitive);
        LogOnce("vertex format", call.vertexFormat, "0x%08X (stride %u)", call.vertexFormat, call.vertexStride);

        // Which vertices the draw reads, and its indices relative to the first.
        indices_.clear();
        uint32_t first = call.first, last = call.first + call.count - 1;
        if (call.indices) {
            LogOnce("index type", call.indexType, "%u", call.indexType);
            indices_.resize(call.count);
            for (uint32_t i = 0; i < call.count; ++i) {
                switch (call.indexType) {
                case UnsignedByte: indices_[i] = static_cast<const uint8_t*>(call.indices)[i]; break;
                case UnsignedShort: indices_[i] = static_cast<const uint16_t*>(call.indices)[i]; break;
                default: indices_[i] = static_cast<const uint32_t*>(call.indices)[i]; break;
                }
            }
            first = *std::min_element(indices_.begin(), indices_.end());
            last = *std::max_element(indices_.begin(), indices_.end());
            for (uint32_t& index : indices_)
                index -= first;
        }
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        switch (call.primitive) {
        case Triangles: break;
        case TriangleStrip: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
        case Points: topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; break;
        case Lines: topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case LineStrip: topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
        case TriangleFan:
        case Quads:
        case QuadStrip: ToTriangleList(call.primitive, call.indices ? nullptr : &call.count); break;
        default: return;
        }

        const uint32_t vertexCount = last - first + 1;
        const UINT stride = call.vertexStride;
        UINT vertexOffset = 0;
        if (!Upload(vertexRing_, vertexRingPos_, kVertexRing, call.vertices + static_cast<size_t>(first) * stride,
                    vertexCount * stride, stride, vertexOffset))
            return;

        ID3D11InputLayout* layout = InputLayout(call.vertexFormat);
        if (!layout)
            return;
        SetPipeline(state, layout, topology, ElementCount(call.vertexFormat, Element::Color) > 0);
        ID3D11Buffer* buffers[2] = {vertexRing_.Get(), defaults_.Get()};
        const UINT strides[2] = {stride, 0}, offsets[2] = {vertexOffset, 0};
        context_->IASetVertexBuffers(0, 2, buffers, strides, offsets);

        if (indices_.empty()) {
            context_->Draw(vertexCount, 0);
            return;
        }
        // 16-bit indices unless they do not fit (or would hit a strip's cut value).
        const bool wide = vertexCount >= 0xFFFF;
        UINT indexOffset = 0;
        if (wide) {
            if (!Upload(indexRing_, indexRingPos_, kIndexRing, indices_.data(), indices_.size() * 4, 4, indexOffset))
                return;
        } else {
            shortIndices_.assign(indices_.begin(), indices_.end());
            if (!Upload(indexRing_, indexRingPos_, kIndexRing, shortIndices_.data(), shortIndices_.size() * 2, 2,
                        indexOffset))
                return;
        }
        context_->IASetIndexBuffer(indexRing_.Get(), wide ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, indexOffset);
        context_->DrawIndexed(static_cast<UINT>(indices_.size()), 0, 0);
    }

private:
    // Turns fans, quads and quad strips into triangle lists, from `indices_`
    // or, when `arrayCount` is given, from vertices 0..count-1.
    void ToTriangleList(uint32_t primitive, const uint32_t* arrayCount)
    {
        std::vector<uint32_t>& source = listSource_;
        if (arrayCount) {
            source.resize(*arrayCount);
            for (uint32_t i = 0; i < *arrayCount; ++i)
                source[i] = i;
        } else {
            source.swap(indices_);
        }
        indices_.clear();
        const size_t n = source.size();
        if (primitive == TriangleFan) {
            for (size_t i = 1; i + 1 < n; ++i)
                indices_.insert(indices_.end(), {source[0], source[i], source[i + 1]});
        } else if (primitive == Quads) {
            for (size_t i = 0; i + 3 < n; i += 4)
                indices_.insert(indices_.end(), {source[i], source[i + 1], source[i + 2], source[i], source[i + 2],
                                                 source[i + 3]});
        } else { // quad strip: quad i is 2i, 2i+1, 2i+3, 2i+2
            for (size_t i = 0; i + 3 < n; i += 2)
                indices_.insert(indices_.end(), {source[i], source[i + 1], source[i + 3], source[i], source[i + 3],
                                                 source[i + 2]});
        }
    }

    // Appends `bytes` to a ring buffer at an offset aligned to `align`.
    bool Upload(ComPtr<ID3D11Buffer>& ring, UINT& pos, UINT size, const void* data, size_t bytes, UINT align,
                UINT& offset)
    {
        if (bytes == 0 || bytes > size)
            return false;
        pos = (pos + align - 1) / align * align;
        D3D11_MAP map = D3D11_MAP_WRITE_NO_OVERWRITE;
        if (pos + bytes > size) {
            pos = 0;
            map = D3D11_MAP_WRITE_DISCARD;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(ring.Get(), 0, map, 0, &mapped)))
            return false;
        std::memcpy(static_cast<uint8_t*>(mapped.pData) + pos, data, bytes);
        context_->Unmap(ring.Get(), 0);
        offset = pos;
        pos += static_cast<UINT>(bytes);
        return true;
    }

    ID3D11InputLayout* InputLayout(uint32_t format)
    {
        auto it = layouts_.find(format);
        if (it != layouts_.end())
            return it->second.Get();
        D3D11_INPUT_ELEMENT_DESC elements[3 + kMaxStages];
        auto element = [&](int i, const char* name, UINT index, DXGI_FORMAT dxgi, bool present, UINT offset,
                           UINT defaultOffset) {
            elements[i] = {name, index, dxgi, present ? 0u : 1u, present ? offset : defaultOffset,
                           D3D11_INPUT_PER_VERTEX_DATA, 0};
        };
        element(0, "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, true, ElementOffset(format, Element::Position, 0), 0);
        element(1, "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, ElementCount(format, Element::Normal) > 0,
                ElementOffset(format, Element::Normal, 0), offsetof(VertexDefaults, normal));
        element(2, "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, ElementCount(format, Element::Color) > 0,
                ElementOffset(format, Element::Color, 0), offsetof(VertexDefaults, color));
        const uint32_t sets = ElementCount(format, Element::TexCoord2);
        for (uint32_t k = 0; k < kMaxStages; ++k)
            element(3 + k, "TEXCOORD", k, DXGI_FORMAT_R32G32_FLOAT, k < sets, ElementOffset(format, Element::TexCoord2, k),
                    static_cast<UINT>(offsetof(VertexDefaults, texCoords) + 8 * k));
        ComPtr<ID3D11InputLayout> layout;
        if (FAILED(device_->CreateInputLayout(elements, 3 + kMaxStages, vsCode_->GetBufferPointer(),
                                              vsCode_->GetBufferSize(), &layout))) {
            LogOnce("input layout failure", format, "format 0x%08X", format);
            return nullptr;
        }
        return (layouts_[format] = layout).Get();
    }

    void SetPipeline(const DrawState& state, ID3D11InputLayout* layout, D3D11_PRIMITIVE_TOPOLOGY topology,
                     bool hasColor)
    {
        DrawConstants c{};
        std::memcpy(c.modelView, state.modelView, sizeof(c.modelView));
        std::memcpy(c.projection, state.projection, sizeof(c.projection));
        // GL clip depth -w..w to Direct3D's 0..w: row 2 = (row 2 + row 3) / 2.
        for (int col = 0; col < 4; ++col)
            c.projection[col * 4 + 2] = 0.5f * (state.projection[col * 4 + 2] + state.projection[col * 4 + 3]);
        std::memcpy(c.colorMul, state.colorMultiplier, sizeof(c.colorMul));
        std::memcpy(c.texFactor, state.textureFactor, sizeof(c.texFactor));
        std::memcpy(c.fogColor, state.fogColor, sizeof(c.fogColor));
        c.fogParams[0] = state.fogMode == FogExp ? 0.0f : state.fogMode == FogExp2 ? 1.0f : 2.0f;
        c.fogParams[1] = state.fogDensity;
        c.fogParams[2] = state.fogStart;
        c.fogParams[3] = state.fogEnd;
        c.flags[0] = state.lighting | (state.diffuseFromVertex ? 2u : 0u) | (hasColor ? 4u : 0u);
        c.flags[1] = state.vertexColors;
        c.flags[2] = state.enabled[Fog];
        if (state.enabled[Fog])
            LogOnce("fog", state.fogMode, "mode %u, colour %.3f %.3f %.3f, density %g, start %g, end %g", state.fogMode,
                    state.fogColor[0], state.fogColor[1], state.fogColor[2], state.fogDensity, state.fogStart,
                    state.fogEnd);
        c.alphaTest[0] = state.enabled[AlphaTest];
        c.alphaTest[1] = state.alphaFunc;
        c.alphaTest[2] = static_cast<uint32_t>(std::clamp(state.alphaRef, 0.0f, 1.0f) * 255.0f + 0.5f);

        ID3D11ShaderResourceView* srvs[kMaxStages] = {};
        ID3D11SamplerState* samplers[kMaxStages] = {};
        // Direct3D semantics, as in the DirectX driver: the first stage that is
        // off, has no texture or uses the blend env mode (which that driver
        // does not implement) ends the cascade (0x00884320, 0x00884500).
        uint32_t stages = 0;
        while (stages < state.stageCount && stages < kMaxStages && state.stages[stages].enabled &&
               state.stages[stages].envMode != EnvBlend && textures_.count(state.stages[stages].texture))
            ++stages;
        c.flags[3] = stages;
        if (stages < state.stageCount && state.stages[stages].enabled && !textures_.count(state.stages[stages].texture))
            LogOnce("draw with an unknown texture", state.stages[stages].texture, "stage %u, handle 0x%X", stages,
                    state.stages[stages].texture);
        for (uint32_t k = 0; k < stages; ++k) {
            const StageDesc& s = state.stages[k];
            StageConstants& sc = c.stages[k];
            auto tex = textures_.find(s.texture);
            const bool enabled = true;
            sc.setup[0] = enabled;
            sc.setup[1] = s.envMode == EnvCombine4 ? EnvCombine : s.envMode;
            sc.setup[2] = s.texCoord;
            sc.setup[3] = s.hasMatrix;
            sc.rgb[0] = s.combiner.rgbOp;
            sc.alpha[0] = s.combiner.alphaOp;
            for (int a = 0; a < 3; ++a) {
                sc.rgb[a + 1] = s.combiner.rgbArg[a];
                sc.alpha[a + 1] = s.combiner.alphaArg[a];
            }
            sc.scale[0] = static_cast<float>(1u << std::min<uint32_t>(s.combiner.rgbScale, 2));
            sc.scale[1] = static_cast<float>(1u << std::min<uint32_t>(s.combiner.alphaScale, 2));
            std::memcpy(sc.matrix, s.matrix, sizeof(sc.matrix));
            if (enabled) {
                srvs[k] = tex->second.srv.Get();
                samplers[k] = Sampler(s.params);
                LogOnce("texture env", s.envMode == EnvCombine
                                           ? 0x100u | s.combiner.rgbOp | (s.combiner.alphaOp << 4)
                                           : s.envMode,
                        "mode %u, combiner ops %u/%u", s.envMode, s.combiner.rgbOp, s.combiner.alphaOp);
                if (s.texCoord & ~kTexCoordSetMask)
                    LogOnce("texture coordinate generation", s.texCoord, "0x%X", s.texCoord);
                // One full state dump per distinct stage setup, to compare with the stock driver.
                const uint64_t setup = (static_cast<uint64_t>(s.envMode) << 56) ^
                                       (static_cast<uint64_t>(s.combiner.rgbOp) << 48) ^
                                       (static_cast<uint64_t>(s.combiner.alphaOp) << 40) ^
                                       (static_cast<uint64_t>(s.combiner.rgbArg[0] | (s.combiner.rgbArg[1] << 8) |
                                                              (s.combiner.alphaArg[0] << 16) |
                                                              (s.combiner.alphaArg[1] << 24))
                                        << 8) ^
                                       (static_cast<uint64_t>(state.blendSrc) << 4) ^ state.blendDst ^
                                       (static_cast<uint64_t>(k) << 36) ^ (static_cast<uint64_t>(s.texCoord) << 32);
                LogOnce("stage setup", setup,
                        "stage %u/%u env %u ops %u/%u rgb args %02X %02X %02X alpha args %02X %02X %02X scale %u/%u "
                        "texcoord 0x%X matrix %d tex %u %ux%u/%u params %u %u %u %u | depth %d%d f%u offset %d cull %d | blend %d %u->%u alphatest %d f%u ref %.3f | lighting %d mul %.2f %.2f "
                        "%.2f %.2f vcolors %d | factor %.2f %.2f %.2f %.2f",
                        k, state.stageCount, s.envMode, s.combiner.rgbOp, s.combiner.alphaOp, s.combiner.rgbArg[0],
                        s.combiner.rgbArg[1], s.combiner.rgbArg[2], s.combiner.alphaArg[0], s.combiner.alphaArg[1],
                        s.combiner.alphaArg[2], s.combiner.rgbScale, s.combiner.alphaScale, s.texCoord, s.hasMatrix,
                        tex->second.format, tex->second.width, tex->second.height, tex->second.levels, s.params[0],
                        s.params[1], s.params[2], s.params[3], state.enabled[DepthTest], state.depthMask,
                        state.depthFunc, state.polygonOffset, state.enabled[CullFace],
                        state.enabled[Blending], state.blendSrc, state.blendDst, state.enabled[AlphaTest],
                        state.alphaFunc, state.alphaRef, state.lighting, state.colorMultiplier[0],
                        state.colorMultiplier[1], state.colorMultiplier[2], state.colorMultiplier[3],
                        state.vertexColors, state.textureFactor[0], state.textureFactor[1], state.textureFactor[2],
                        state.textureFactor[3]);
            }
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(drawConstants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &c, sizeof(c));
            context_->Unmap(drawConstants_.Get(), 0);
        }

        Rect r = state.viewport;
        const D3D11_VIEWPORT viewport{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.width),
                                      static_cast<float>(r.height), 0.0f, 1.0f};
        Clip(r, width_, height_);
        const D3D11_RECT scissor{r.x, r.y, r.x + r.width, r.y + r.height};
        context_->RSSetViewports(1, &viewport);
        context_->RSSetScissorRects(1, &scissor);
        context_->RSSetState(Rasterizer(state));
        context_->OMSetRenderTargets(1, backRtv_.GetAddressOf(), depth_.dsv.Get());
        context_->OMSetBlendState(Blend(state), nullptr, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(DepthStencil(state), state.stencilRef & 0xFF);
        context_->IASetInputLayout(layout);
        context_->IASetPrimitiveTopology(topology);
        context_->VSSetShader(vsDraw_.Get(), nullptr, 0);
        context_->PSSetShader(state.shadeModel == Flat ? psFlat_.Get() : psSmooth_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, drawConstants_.GetAddressOf());
        context_->PSSetConstantBuffers(0, 1, drawConstants_.GetAddressOf());
        context_->PSSetShaderResources(0, kMaxStages, srvs);
        context_->PSSetSamplers(0, kMaxStages, samplers);
    }

    ID3D11SamplerState* Sampler(const uint32_t params[4])
    {
        const uint32_t key = params[0] | (params[1] << 8) | (params[2] << 16) | (params[3] << 24);
        auto it = samplers_.find(key);
        if (it != samplers_.end())
            return it->second.Get();
        // As the DirectX driver maps them (0x00884320). Its LinearMipmapNearest
        // falls through to NearestMipmapLinear (a missing break); kept, since
        // the game's look was tuned on it.
        const uint32_t mag = params[MagFilter], min = params[MinFilter];
        const bool magLinear = mag == Linear;
        const bool minLinear = min == Linear || min == LinearMipmapLinear;
        const bool mipLinear = min == LinearMipmapNearest || min == NearestMipmapLinear || min == LinearMipmapLinear;
        const bool mipmaps = min >= NearestMipmapNearest;
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = static_cast<D3D11_FILTER>((minLinear ? 0x10 : 0) | (magLinear ? 0x4 : 0) | (mipLinear ? 0x1 : 0));
        desc.AddressU = params[WrapS] == Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressV = params[WrapT] == Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MinLOD = 0;
        desc.MaxLOD = mipmaps ? D3D11_FLOAT32_MAX : 0;
        LogOnce("sampler", key, "mag %u, min %u, wrap %u/%u", mag, min, params[WrapS], params[WrapT]);
        ComPtr<ID3D11SamplerState> sampler;
        device_->CreateSamplerState(&desc, &sampler);
        return (samplers_[key] = sampler).Get();
    }

    ID3D11BlendState* Blend(const DrawState& state)
    {
        const bool on = state.enabled[Blending];
        const uint32_t key = on ? (1 | (state.blendSrc << 8) | (state.blendDst << 16)) : 0;
        auto it = blends_.find(key);
        if (it != blends_.end())
            return it->second.Get();
        D3D11_BLEND_DESC desc{};
        auto& rt = desc.RenderTarget[0];
        rt.BlendEnable = on;
        rt.SrcBlend = BlendFactor(state.blendSrc, false);
        rt.DestBlend = BlendFactor(state.blendDst, false);
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = BlendFactor(state.blendSrc, true);
        rt.DestBlendAlpha = BlendFactor(state.blendDst, true);
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (!on) {
            rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_ZERO;
        }
        ComPtr<ID3D11BlendState> blend;
        device_->CreateBlendState(&desc, &blend);
        return (blends_[key] = blend).Get();
    }

    ID3D11DepthStencilState* DepthStencil(const DrawState& state)
    {
        const bool depth = state.enabled[DepthTest], stencil = state.enabled[StencilTest];
        uint64_t key = (depth ? 1 : 0) | (state.depthMask ? 2 : 0) | (static_cast<uint64_t>(state.depthFunc & 7) << 2);
        if (stencil)
            key |= 0x20 | (static_cast<uint64_t>(state.stencilFunc & 7) << 6) |
                   (static_cast<uint64_t>(state.stencilOps[0] & 7) << 9) |
                   (static_cast<uint64_t>(state.stencilOps[1] & 7) << 12) |
                   (static_cast<uint64_t>(state.stencilOps[2] & 7) << 15) |
                   (static_cast<uint64_t>(state.stencilReadMask & 0xFF) << 18) |
                   (static_cast<uint64_t>(state.stencilWriteMask & 0xFF) << 26);
        auto it = depthStencils_.find(key);
        if (it != depthStencils_.end())
            return it->second.Get();
        D3D11_DEPTH_STENCIL_DESC desc{};
        desc.DepthEnable = depth;
        desc.DepthWriteMask = state.depthMask ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = Comparison(state.depthFunc);
        desc.StencilEnable = stencil;
        desc.StencilReadMask = static_cast<UINT8>(state.stencilReadMask);
        desc.StencilWriteMask = static_cast<UINT8>(state.stencilWriteMask);
        desc.FrontFace = {StencilOperation(state.stencilOps[0]), StencilOperation(state.stencilOps[1]),
                          StencilOperation(state.stencilOps[2]), Comparison(state.stencilFunc)};
        desc.BackFace = desc.FrontFace;
        ComPtr<ID3D11DepthStencilState> ds;
        device_->CreateDepthStencilState(&desc, &ds);
        return (depthStencils_[key] = ds).Get();
    }

    ID3D11RasterizerState* Rasterizer(const DrawState& state)
    {
        const bool cull = state.enabled[CullFace];
        const uint64_t key = (cull ? 1 : 0) | (static_cast<uint64_t>(static_cast<uint32_t>(state.polygonOffset)) << 1);
        auto it = rasterizers_.find(key);
        if (it != rasterizers_.end())
            return it->second.Get();
        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        // Direct3D 7's D3DCULL_CW, which the DirectX driver sets for Enable(cull).
        desc.CullMode = cull ? D3D11_CULL_FRONT : D3D11_CULL_NONE;
        desc.FrontCounterClockwise = FALSE;
        // Direct3D 7 z-bias (0-16): higher values draw in front. Mapped as Wine's
        // OpenGL backend maps it, glPolygonOffset(-bias, -bias): one depth unit
        // and one unit of slope per step.
        desc.DepthBias = -state.polygonOffset;
        desc.SlopeScaledDepthBias = static_cast<float>(-state.polygonOffset);
        desc.ScissorEnable = TRUE;
        desc.DepthClipEnable = TRUE;
        if (state.polygonOffset)
            LogOnce("polygon offset", static_cast<uint32_t>(state.polygonOffset), "%d", state.polygonOffset);
        ComPtr<ID3D11RasterizerState> raster;
        device_->CreateRasterizerState(&desc, &raster);
        return (rasterizers_[key] = raster).Get();
    }

    bool CreateDrawPipeline()
    {
        auto compile = [&](const char* entry, const char* profile, const char* interpolation) {
            const D3D_SHADER_MACRO macros[] = {{"COLOR_INTERPOLATION", interpolation}, {nullptr, nullptr}};
            ComPtr<ID3DBlob> code, errors;
            const HRESULT hr = compile_(kFixedFunctionShaders, sizeof(kFixedFunctionShaders) - 1, "opensc4_ff", macros,
                                        nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (FAILED(hr))
                log::Write("d3d11: compiling %s failed: %s", entry,
                           errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
            return code;
        };
        vsCode_ = compile("VSMain", "vs_4_0", "");
        ComPtr<ID3DBlob> smooth = compile("PSMain", "ps_4_0", ""), flat = compile("PSMain", "ps_4_0", "nointerpolation");
        if (!vsCode_ || !smooth || !flat)
            return false;
        device_->CreateVertexShader(vsCode_->GetBufferPointer(), vsCode_->GetBufferSize(), nullptr, &vsDraw_);
        device_->CreatePixelShader(smooth->GetBufferPointer(), smooth->GetBufferSize(), nullptr, &psSmooth_);
        device_->CreatePixelShader(flat->GetBufferPointer(), flat->GetBufferSize(), nullptr, &psFlat_);

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(DrawConstants);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&desc, nullptr, &drawConstants_);
        desc.ByteWidth = kVertexRing;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        device_->CreateBuffer(&desc, nullptr, &vertexRing_);
        desc.ByteWidth = kIndexRing;
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        device_->CreateBuffer(&desc, nullptr, &indexRing_);

        const VertexDefaults defaults;
        D3D11_BUFFER_DESC fixed{};
        fixed.ByteWidth = sizeof(defaults);
        fixed.Usage = D3D11_USAGE_IMMUTABLE;
        fixed.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{&defaults, 0, 0};
        device_->CreateBuffer(&fixed, &data, &defaults_);
        return vsDraw_ && psSmooth_ && psFlat_ && drawConstants_ && vertexRing_ && indexRing_ && defaults_;
    }

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
    HANDLE frameReady_ = nullptr; // flip model: signalled when the display can take another frame
    bool tearing_ = false;
    bool frontPending_ = false;
    static constexpr uint32_t kQueuedFlushes = 3;
    ComPtr<ID3D11Query> flushQueries_[kQueuedFlushes];
    uint32_t flushQuery_ = 0;
    uint32_t shown_ = 0;
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

    // Drawing
    UINT syncInterval_ = 1;
    ComPtr<ID3DBlob> vsCode_;
    ComPtr<ID3D11VertexShader> vsDraw_;
    ComPtr<ID3D11PixelShader> psSmooth_, psFlat_;
    ComPtr<ID3D11Buffer> drawConstants_, vertexRing_, indexRing_, defaults_;
    UINT vertexRingPos_ = 0, indexRingPos_ = 0;
    std::unordered_map<uint32_t, ComPtr<ID3D11InputLayout>> layouts_;
    std::unordered_map<uint32_t, ComPtr<ID3D11SamplerState>> samplers_;
    std::unordered_map<uint32_t, ComPtr<ID3D11BlendState>> blends_;
    std::unordered_map<uint64_t, ComPtr<ID3D11DepthStencilState>> depthStencils_;
    std::unordered_map<uint64_t, ComPtr<ID3D11RasterizerState>> rasterizers_;
    std::vector<uint32_t> indices_, listSource_;
    std::vector<uint16_t> shortIndices_;

    std::unordered_map<uint32_t, Texture> textures_;
    uint32_t nextTexture_ = 1;
    std::vector<uint8_t> scratch_;
};

} // namespace

std::unique_ptr<Backend> CreateD3D11Backend()
{
    return std::make_unique<D3D11Backend>();
}

AdapterInfo QueryAdapterInfo()
{
    AdapterInfo info;
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    auto create = dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    if (!create || FAILED(create(__uuidof(IDXGIFactory1), &factory)) || FAILED(factory->EnumAdapters1(0, &adapter)) ||
        FAILED(adapter->GetDesc1(&desc)))
        return info;
    char name[128] = {};
    WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
    info.name = name;
    info.vendor = desc.VendorId;
    info.device = desc.DeviceId;
    info.subsystem = desc.SubSysId;
    info.revision = desc.Revision;
    LARGE_INTEGER version{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version)))
        for (int i = 0; i < 4; ++i)
            info.driverVersion[i] = static_cast<uint16_t>(version.QuadPart >> (48 - 16 * i));
    return info;
}

} // namespace opensc4::gd
