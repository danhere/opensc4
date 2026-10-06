// renderer.d3d11: OpenSC4's own cIGZGDriver (P1 step 3), made current with
// cIGZGraphicSystem2::SetGDriver at PreAppInit like renderer.passthrough.
// It keeps the game's fixed-function state the way the DirectX 7 driver
// does and draws through a Backend (backend.h); semantics are in
// docs/renderer.md, "What the driver calls mean".
//
// Like the stock drivers it creates its own (ANSI) window in SetVideoMode and
// sends the window's messages to the procedure the game passes in.
#include "core/gd/backend.h"
#include "core/gd/gd.h"
#include "core/gd/vertex_format.h"

#include "core/log.h"
#include "core/registry.h"

#include <GZServPtrs.h>
#include <cIGZBuffer.h>
#include <cIGZCOM.h>
#include <cIGZFrameWork.h>
#include <cIGZGDriver.h>
#include <cIGZGraphicSystem2.h>

#include <windows.h>

#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace opensc4::gd {
namespace {

// Not a stock driver's ID, so code that checks for the DirectX driver before
// reading its internals (render-services does) leaves this one alone.
constexpr uint32_t kClassId = 0x4F534344; // "OSCD"
constexpr uint32_t kCLSID_cGZBuffer = 0xC470D325;
constexpr uint32_t kStages = 8;

constexpr char kWindowClass[] = "GDriverClass (OpenSC4 D3D11)";

// Error codes the DirectX driver reports through GetError.
enum Error : uint32_t { NoError, InvalidEnum, InvalidValue, InvalidOperation };

struct TextureInfo {
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t levels;
};

struct StageState {
    uint32_t texture = 0;
    bool enabled = false;
    uint32_t texCoord = 0; // TexStageCoord
    uint32_t envMode = kGDTextureEnvParam_Modulate;
    Combiner combiner{};
    float params[4]{}; // TexParameter
    float matrix[16]{};
    bool hasMatrix = false;
};

class Driver;

class BufferRegions final : public BufferRegionExtension {
public:
    explicit BufferRegions(Driver& driver) : driver_(driver) {}
    bool QueryInterface(uint32_t iid, void** object) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    bool BufferRegionEnabled() override { return true; }
    int32_t NewBufferRegion(int32_t surface) override;
    bool DeleteBufferRegion(int32_t region) override;
    bool ReadBufferRegion(int32_t region, int32_t x, int32_t y, int32_t width, int32_t height, int32_t regionX,
                          int32_t regionY) override;
    bool DrawBufferRegion(int32_t region, int32_t x, int32_t y, int32_t width, int32_t height, int32_t regionX,
                          int32_t regionY) override;
    bool IsBufferRegion(int32_t region) override;
    bool CanDoPartialRegionWrites() override { return true; }
    bool CanDoOffsetReads() override { return true; }

private:
    Driver& driver_;
};

class Snapshots final : public SnapshotExtension {
public:
    explicit Snapshots(Driver& driver) : driver_(driver) {}
    bool QueryInterface(uint32_t iid, void** object) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    cIGZBuffer* CopyColorBuffer(int32_t x, int32_t y, int32_t width, int32_t height, cIGZBuffer* buffer) override;

private:
    Driver& driver_;
};

// Logs the first call of each method that does not do its job yet.
void Unimplemented(const char* method)
{
    static std::map<const char*, int> seen;
    if (seen[method]++ == 0)
        log::Write("d3d11 driver: %s is not implemented yet", method);
}

class Driver final : public cIGZGDriver {
public:
    explicit Driver(std::vector<Mode> modes) : modes_(std::move(modes)), regions_(*this), snapshots_(*this)
    {
        for (StageState& s : stages_)
            Identity(s.matrix);
        Identity(modelView_);
        Identity(projection_);
    }

    // cIGZUnknown

    bool QueryInterface(uint32_t iid, void** object) override
    {
        if (iid == GZIID_cIGZGDriver || iid == GZIID_cIGZUnknown) {
            *object = static_cast<cIGZGDriver*>(this);
        } else if (iid == kIID_BufferRegionExtension) {
            *object = static_cast<BufferRegionExtension*>(&regions_);
        } else if (iid == kIID_SnapshotExtension) {
            *object = static_cast<SnapshotExtension*>(&snapshots_);
        } else {
            static std::map<uint32_t, int> refused;
            if (refused[iid]++ == 0)
                log::Write("d3d11 driver: QueryInterface(0x%08X) not supported", iid);
            return false;
        }
        AddRef();
        return true;
    }

    uint32_t AddRef() override { return ++refs_; }

    uint32_t Release() override
    {
        const uint32_t refs = --refs_;
        if (refs == 0)
            delete this;
        return refs;
    }

    // Vertex arrays and drawing

    void DrawArrays(uint32_t prim, int32_t first, int32_t count) override
    {
        (void)prim, (void)first, (void)count;
        ++draws_;
    }

    void DrawElements(uint32_t prim, int32_t count, uint32_t type, void const* indices) override
    {
        (void)prim, (void)count, (void)type, (void)indices;
        ++draws_;
    }

    void InterleavedArrays(uint32_t format, int32_t stride, void const* data) override
    {
        vertexFormat_ = format;
        vertexStride_ = stride ? static_cast<uint32_t>(stride) : Stride(format);
        vertexData_ = data;
    }

    uint32_t MakeVertexFormat(uint32_t count, intptr_t elements) override
    {
        return MakeFormat(count, reinterpret_cast<const uint32_t*>(elements));
    }
    uint32_t MakeVertexFormat(uint32_t format) override { return ExpandFormat(format); }
    uint32_t VertexFormatStride(uint32_t format) override { return Stride(format); }
    uint32_t VertexFormatElementOffset(uint32_t format, uint32_t element, uint32_t index) override
    {
        return ElementOffset(format, static_cast<Element>(element), index);
    }
    uint32_t VertexFormatNumElements(uint32_t format, uint32_t element) override
    {
        return ElementCount(format, static_cast<Element>(element));
    }

    // Frame buffer

    void Clear(uint32_t mask) override
    {
        if (backend_)
            backend_->Clear(ViewportRect(), mask, clearColor_, clearDepth_, clearStencil_);
    }
    void ClearColor(float r, float g, float b, float a) override
    {
        clearColor_[0] = r, clearColor_[1] = g, clearColor_[2] = b, clearColor_[3] = a;
    }
    void ClearDepth(double depth) override { clearDepth_ = static_cast<float>(depth); }
    void ClearStencil(int32_t s) override { clearStencil_ = static_cast<uint8_t>(s); }
    // The DirectX driver ignores ColorMask (0x00882D10), so the game's output never depended on it.
    void ColorMask(bool) override {}
    void DepthFunc(uint32_t func) override { depthFunc_ = func; }
    void DepthMask(bool on) override { depthMask_ = on; }
    void StencilFunc(uint32_t func, int32_t ref, uint32_t mask) override
    {
        stencilFunc_ = func, stencilRef_ = ref, stencilReadMask_ = mask;
    }
    void StencilMask(uint32_t mask) override { stencilWriteMask_ = mask; }
    void StencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) override
    {
        stencilOps_[0] = fail, stencilOps_[1] = zfail, stencilOps_[2] = zpass;
    }
    void BlendFunc(uint32_t src, uint32_t dst) override { blendSrc_ = src, blendDst_ = dst; }
    void AlphaFunc(uint32_t func, float ref) override { alphaFunc_ = func, alphaRef_ = ref; }
    void ShadeModel(uint32_t mode) override { shadeModel_ = mode; }

    // Textures (GL style)

    void BindTexture(uint32_t target, uint32_t texture) override
    {
        if (target != 0) {
            error_ = InvalidEnum;
            return;
        }
        stages_[stage_].texture = texture;
    }

    void TexImage2D(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void const*) override
    {
        Unimplemented("TexImage2D");
    }

    void PixelStore(uint32_t param, int32_t value) override
    {
        if (param == 0)
            unpackRowLength_ = value;
    }

    void TexEnv(uint32_t, uint32_t param, int32_t value) override
    {
        if (param == kGDTextureEnvParamType_Mode)
            stages_[stage_].envMode = static_cast<uint32_t>(value);
    }
    void TexEnv(uint32_t, uint32_t param, float const* values) override
    {
        if (param == kGDTextureEnvParamType_Color && values)
            std::memcpy(textureFactor_, values, sizeof(textureFactor_));
    }
    void TexParameter(uint32_t, uint32_t param, int32_t value) override
    {
        if (param < 4)
            stages_[stage_].params[param] = static_cast<float>(value);
    }

    void Fog(uint32_t param, uint32_t value) override { (void)param, (void)value, Unimplemented("Fog(int)"); }
    void Fog(uint32_t param, float const* values) override { (void)param, (void)values, Unimplemented("Fog(float)"); }

    void ColorMultiplier(float r, float g, float b) override
    {
        colorMultiplier_[0] = r, colorMultiplier_[1] = g, colorMultiplier_[2] = b;
    }
    void AlphaMultiplier(float a) override { colorMultiplier_[3] = a; }
    void EnableVertexColors(bool diffuse, bool specular) override
    {
        vertexColors_ = diffuse;
        vertexSpecular_ = specular;
    }

    void GenTextures(int32_t n, uint32_t* textures) override
    {
        for (int32_t i = 0; i < n; ++i) {
            textures[i] = nextTexture_++;
            textures_[textures[i]] = {};
        }
    }
    void DeleteTextures(int32_t n, uint32_t const* textures) override
    {
        for (int32_t i = 0; i < n; ++i)
            textures_.erase(textures[i]);
    }
    bool IsTexture(uint32_t texture) override { return textures_.count(texture) > 0; }
    void PrioritizeTextures(int32_t, uint32_t const*, float const*) override {}
    bool AreTexturesResident(int32_t n, uint32_t const* textures, bool* resident) override
    {
        for (int32_t i = 0; i < n; ++i)
            resident[i] = textures_.count(textures[i]) > 0;
        return true;
    }

    // Matrices

    void MatrixMode(uint32_t mode) override { matrixMode_ = mode; }
    void LoadMatrix(float const* m) override
    {
        if (matrixMode_ == ModelView)
            std::memcpy(modelView_, m, sizeof(modelView_));
        else if (matrixMode_ == Projection)
            std::memcpy(projection_, m, sizeof(projection_));
    }
    void LoadIdentity() override
    {
        if (matrixMode_ == ModelView)
            Identity(modelView_);
        else if (matrixMode_ == Projection)
            Identity(projection_);
    }

    void Flush() override
    {
        if (backend_)
            backend_->Present();
        ++frames_;
    }

    // State

    void Enable(uint32_t state) override { SetEnabled(state, true); }
    void Disable(uint32_t state) override { SetEnabled(state, false); }
    bool IsEnabled(uint32_t state) override
    {
        if (state == kGDCapability_Texture2D)
            return stages_[stage_].enabled;
        return state < kGDNumCapabilities && enabled_[state];
    }

    void GetBoolean(uint32_t, bool*) override {} // the DirectX driver's is empty
    void GetInteger(uint32_t param, int32_t* value) override
    {
        if (param == 0)
            *value = unpackRowLength_;
    }
    void GetFloat(uint32_t, float*) override {} // empty in the DirectX driver too
    uint32_t GetError() override
    {
        const uint32_t error = error_;
        error_ = NoError;
        return error;
    }

    // Texture stages

    void TexStage(uint32_t stage) override
    {
        if (stage >= kStages) {
            error_ = InvalidValue;
            return;
        }
        stage_ = stage;
    }
    void TexStageCoord(uint32_t source) override { stages_[stage_].texCoord = source; }
    void TexStageMatrix(float const* m, uint32_t, uint32_t, uint32_t) override
    {
        StageState& s = stages_[stage_];
        s.hasMatrix = m != nullptr;
        if (m)
            std::memcpy(s.matrix, m, sizeof(s.matrix));
    }
    void TexStageCombine(eGDTextureStageCombineParamType type, eGDTextureStageCombineModeParam mode) override
    {
        Combiner& c = stages_[stage_].combiner;
        (type == eGDTextureStageCombineParamType::RGB ? c.rgbOp : c.alphaOp) = static_cast<uint8_t>(mode);
    }
    void TexStageCombine(eGDTextureStageCombineSourceParamType type, eGDTextureStageCombineSourceParam source) override
    {
        SetArg(static_cast<uint32_t>(type), static_cast<uint8_t>(source), 0x0F);
    }
    void TexStageCombine(eGDTextureStageCombineOperandType type, eGDBlend operand) override
    {
        // Operands are blend factors: SrcColor, OneMinusSrcColor, SrcAlpha, OneMinusSrcAlpha -> 0..3.
        SetArg(static_cast<uint32_t>(type), static_cast<uint8_t>((static_cast<uint32_t>(operand) - 2) << 4), 0xF0);
    }
    void TexStageCombine(eGDTextureStageCombineScaleParamType type, eGDTextureStageCombineScaleParam scale) override
    {
        Combiner& c = stages_[stage_].combiner;
        (type == eGDTextureStageCombineScaleParamType::RGB ? c.rgbScale : c.alphaScale) = static_cast<uint8_t>(scale);
    }

    void SetTexture(uint32_t texture, uint32_t stage) override
    {
        if (stage < kStages)
            stages_[stage].texture = texture;
    }
    intptr_t GetTexture(uint32_t stage) override { return stage < kStages ? stages_[stage].texture : 0; }

    intptr_t CreateTexture(uint32_t format, uint32_t width, uint32_t height, uint32_t levels, uint32_t) override
    {
        const uint32_t id = nextTexture_++;
        textures_[id] = {format, width, height, levels ? levels : 1};
        return id;
    }

    void LoadTextureLevel(uint32_t texture, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height,
                          uint32_t format, uint32_t type, uint32_t rowLength, void const* pixels) override
    {
        (void)texture, (void)level, (void)x, (void)y, (void)width, (void)height, (void)format, (void)type,
            (void)rowLength, (void)pixels;
        ++textureUploads_;
    }

    void SetCombiner(cGDCombiner const& combiner, uint32_t stage) override
    {
        const auto& c = reinterpret_cast<const Combiner&>(combiner);
        if (stage >= kStages || c.rgbOp == 0xFF)
            return;
        StageState& s = stages_[stage];
        s.combiner = c;
        s.envMode = kGDTextureEnvParam_Combine;
    }

    // Video modes and window

    uint32_t CountVideoModes() const override { return static_cast<uint32_t>(modes_.size()); }

    void GetVideoModeInfo(uint32_t index, sGDMode& mode) override
    {
        if (index >= modes_.size()) {
            error_ = InvalidValue;
            return;
        }
        reinterpret_cast<Mode&>(mode) = modes_[index];
    }

    void GetVideoModeInfo(sGDMode& mode) override { reinterpret_cast<Mode&>(mode) = current_; }

    void SetVideoMode(int32_t index, void* windowProc, bool, bool) override
    {
        if (index == -1) {
            DestroyOutput();
            return;
        }
        if (index < 0 || static_cast<size_t>(index) >= modes_.size()) {
            error_ = InvalidValue;
            return;
        }
        DestroyOutput();
        current_ = modes_[index];
        gameProc_ = windowProc ? reinterpret_cast<WNDPROC>(windowProc) : DefWindowProcA;
        if (!CreateOutput()) {
            DestroyOutput();
            error_ = static_cast<Error>(6); // the DirectX driver's code for a failed mode set
            return;
        }
        viewport_ = {0, 0, static_cast<int32_t>(current_.width), static_cast<int32_t>(current_.height)};
        log::Write("d3d11 driver: video mode %d, %ux%u, %s", index, current_.width, current_.height,
                   current_.fullScreen ? "full screen (shown in a window for now)" : "windowed");
    }

    void PolygonOffset(int32_t offset) override { polygonOffset_ = offset; }

    // The stock drivers do not implement the blits either (they set an error).
    void BitBlt(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void const*, bool, void const*) override
    {
        error_ = InvalidOperation;
    }
    void StretchBlt(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void const*, bool,
                    void const*) override
    {
        error_ = InvalidOperation;
    }
    void BitBltAlpha(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void const*, bool, void const*,
                     uint32_t) override
    {
        error_ = InvalidOperation;
    }
    void StretchBltAlpha(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void const*, bool,
                         void const*, uint32_t) override
    {
        error_ = InvalidOperation;
    }
    void BitBltAlphaModulate(int32_t, int32_t, int32_t, uint32_t, uint32_t, void const*, bool, void const*,
                             uint32_t) override
    {
        error_ = InvalidOperation;
    }
    void StretchBltAlphaModulate(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t,
                                 void const*, bool, void const*, uint32_t) override
    {
        error_ = InvalidOperation;
    }

    // Viewport: the game uses GL coordinates (origin bottom left).
    void SetViewport() override
    {
        viewport_ = {0, 0, static_cast<int32_t>(current_.width), static_cast<int32_t>(current_.height)};
    }
    void SetViewport(int32_t x, int32_t y, int32_t width, int32_t height) override
    {
        // Clamped to the frame buffer like the DirectX driver (0x008837E0).
        const auto w = static_cast<int32_t>(current_.width), h = static_cast<int32_t>(current_.height);
        x = x > w ? w : x;
        y = y > h ? h : y;
        width = x + width > w ? w - x : width;
        height = y + height > h ? h - y : height;
        viewport_ = {x, y, width, height};
    }
    void GetViewport(int32_t dimensions[4]) override
    {
        dimensions[0] = viewport_.x;
        dimensions[1] = viewport_.y;
        dimensions[2] = viewport_.x + viewport_.width;
        dimensions[3] = viewport_.y + viewport_.height;
    }

    char const* GetDriverInfo() const override { return "OpenSC4 Direct3D 11 driver"; }
    uint32_t GetGZCLSID() const override { return kClassId; }
    bool Init() override { return true; }
    bool Shutdown() override
    {
        DestroyOutput();
        return true;
    }
    bool IsDeviceReady() override { return true; }
    bool Punt(uint32_t, void*) override { return false; }

    // For the extensions

    Backend* GetBackend() { return backend_.get(); }

    void LogStats(const char* when) const
    {
        log::Write("d3d11 driver: %s: %u frames, %u draws (not drawn yet), %u texture uploads (not uploaded yet)",
                   when, frames_, draws_, textureUploads_);
    }

private:
    static void Identity(float* m)
    {
        std::memset(m, 0, 16 * sizeof(float));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    void SetEnabled(uint32_t state, bool on)
    {
        if (state == kGDCapability_Texture2D)
            stages_[stage_].enabled = on;
        else if (state < kGDNumCapabilities)
            enabled_[state] = on;
        else
            error_ = InvalidEnum;
    }

    // Combiner args: types 0-3 RGB args, 4-7 alpha args (3 used of each).
    void SetArg(uint32_t type, uint8_t value, uint8_t mask)
    {
        Combiner& c = stages_[stage_].combiner;
        uint8_t* arg = type < 4 ? &c.rgbArg[type < 3 ? type : 2] : &c.alphaArg[type - 4 < 3 ? type - 4 : 2];
        *arg = static_cast<uint8_t>((*arg & ~mask) | (value & mask));
    }

    Rect ViewportRect() const
    {
        return {viewport_.x, static_cast<int32_t>(current_.height) - viewport_.y - viewport_.height, viewport_.width,
                viewport_.height};
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<Driver*>(GetWindowLongPtrA(window, GWLP_USERDATA));
        if (!self)
            self = creating_;
        if (self && self->gameProc_)
            return CallWindowProcA(self->gameProc_, window, message, wParam, lParam);
        return DefWindowProcA(window, message, wParam, lParam);
    }

    bool CreateOutput()
    {
        HINSTANCE instance = GetModuleHandleA(nullptr);
        WNDCLASSA wc{};
        if (!GetClassInfoA(instance, kWindowClass, &wc)) {
            wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)); // IDC_ARROW
            wc.lpszClassName = kWindowClass;
            if (!RegisterClassA(&wc))
                return false;
        }
        // The DirectX driver's windowed style (0x10CF0000), or a popup for full-screen modes.
        const DWORD style = current_.fullScreen ? (WS_POPUP | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        RECT rect{0, 0, static_cast<LONG>(current_.width), static_cast<LONG>(current_.height)};
        AdjustWindowRect(&rect, style, FALSE);
        creating_ = this;
        window_ = CreateWindowExA(0, kWindowClass, "SimCity 4", style, 0, 0, rect.right - rect.left,
                                  rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        creating_ = nullptr;
        if (!window_)
            return false;
        SetWindowLongPtrA(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        backend_ = CreateD3D11Backend();
        if (!backend_->Init(window_, current_.width, current_.height)) {
            backend_.reset();
            return false;
        }
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        return true;
    }

    void DestroyOutput()
    {
        if (backend_)
            LogStats("closing the window");
        backend_.reset();
        if (window_) {
            SetWindowLongPtrA(window_, GWLP_USERDATA, 0);
            DestroyWindow(window_);
            window_ = nullptr;
        }
    }

    static inline Driver* creating_ = nullptr;

    std::atomic<uint32_t> refs_{0};
    std::vector<Mode> modes_;
    Mode current_{};
    HWND window_ = nullptr;
    WNDPROC gameProc_ = nullptr;
    std::unique_ptr<Backend> backend_;
    BufferRegions regions_;
    Snapshots snapshots_;
    uint32_t error_ = NoError;

    Rect viewport_{}; // GL coordinates
    float clearColor_[4]{};
    float clearDepth_ = 1.0f;
    uint8_t clearStencil_ = 0;

    bool enabled_[kGDNumCapabilities]{};
    uint32_t depthFunc_ = Less;
    bool depthMask_ = true;
    uint32_t stencilFunc_ = Always, stencilReadMask_ = 0xFF, stencilWriteMask_ = 0xFF;
    int32_t stencilRef_ = 0;
    uint32_t stencilOps_[3] = {StencilKeep, StencilKeep, StencilKeep};
    uint32_t blendSrc_ = 1, blendDst_ = 0; // One, Zero
    uint32_t alphaFunc_ = Always;
    float alphaRef_ = 0.0f;
    uint32_t shadeModel_ = Smooth;
    int32_t polygonOffset_ = 0;
    float colorMultiplier_[4] = {1, 1, 1, 1};
    bool vertexColors_ = true, vertexSpecular_ = false;
    float textureFactor_[4] = {1, 1, 1, 1};

    uint32_t matrixMode_ = ModelView;
    float modelView_[16];
    float projection_[16];

    uint32_t stage_ = 0;
    StageState stages_[kStages];
    std::map<uint32_t, TextureInfo> textures_;
    uint32_t nextTexture_ = 1;
    int32_t unpackRowLength_ = 0;

    uint32_t vertexFormat_ = 0, vertexStride_ = 0;
    const void* vertexData_ = nullptr;

    uint32_t frames_ = 0, draws_ = 0, textureUploads_ = 0;
};

// Extensions share the driver's reference count.

bool BufferRegions::QueryInterface(uint32_t iid, void** object) { return driver_.QueryInterface(iid, object); }
uint32_t BufferRegions::AddRef() { return driver_.AddRef(); }
uint32_t BufferRegions::Release() { return driver_.Release(); }

int32_t BufferRegions::NewBufferRegion(int32_t surface)
{
    Backend* backend = driver_.GetBackend();
    if (!backend || surface < FrontBuffer || surface > DepthBuffer)
        return 0;
    return backend->CreateRegion(static_cast<Surface>(surface));
}

bool BufferRegions::DeleteBufferRegion(int32_t region)
{
    Backend* backend = driver_.GetBackend();
    return backend && backend->DeleteRegion(region);
}

bool BufferRegions::ReadBufferRegion(int32_t region, int32_t x, int32_t y, int32_t width, int32_t height,
                                     int32_t regionX, int32_t regionY)
{
    Backend* backend = driver_.GetBackend();
    return backend && backend->ReadRegion(region, {x, y, width, height}, regionX, regionY);
}

bool BufferRegions::DrawBufferRegion(int32_t region, int32_t x, int32_t y, int32_t width, int32_t height,
                                     int32_t regionX, int32_t regionY)
{
    Backend* backend = driver_.GetBackend();
    return backend && backend->DrawRegion(region, {x, y, width, height}, regionX, regionY);
}

bool BufferRegions::IsBufferRegion(int32_t region)
{
    Backend* backend = driver_.GetBackend();
    return backend && backend->IsRegion(region);
}

bool Snapshots::QueryInterface(uint32_t iid, void** object) { return driver_.QueryInterface(iid, object); }
uint32_t Snapshots::AddRef() { return driver_.AddRef(); }
uint32_t Snapshots::Release() { return driver_.Release(); }

cIGZBuffer* Snapshots::CopyColorBuffer(int32_t x, int32_t y, int32_t width, int32_t height, cIGZBuffer* buffer)
{
    Backend* backend = driver_.GetBackend();
    if (!backend || width <= 0 || height <= 0)
        return buffer;
    if (!buffer) {
        cIGZFrameWork* framework = RZGetFrameWork();
        cIGZCOM* com = framework ? framework->GetCOMObject() : nullptr;
        if (!com || !com->GetClassObject(kCLSID_cGZBuffer, GZIID_cIGZBuffer, reinterpret_cast<void**>(&buffer)) ||
            !buffer)
            return nullptr;
    }
    if (!buffer->IsReady() &&
        !buffer->Init(static_cast<uint32_t>(width), static_cast<uint32_t>(height), cGZBufferColorType::A8R8G8B8, 32))
        return buffer;
    constexpr uint32_t kLockWrite = 0x8080; // the DirectX driver's lock flags
    if (!buffer->Lock(kLockWrite))
        return buffer;
    auto* bits = static_cast<uint8_t*>(buffer->GetColorSurfaceBits());
    const uint32_t pitch = buffer->GetColorSurfaceStride();
    const int32_t w = width < buffer->Width() ? width : buffer->Width();
    const int32_t h = height < buffer->Height() ? height : buffer->Height();
    if (bits)
        backend->ReadFront({x, y, w, h}, bits, pitch);
    buffer->Unlock(kLockWrite);
    return buffer;
}

bool Install(registry::Mode)
{
    cIGZGraphicSystem2Ptr graphics;
    cIGZGraphicSystem2* system = graphics;
    cIGZGDriver* stock = system ? system->GetGDriver() : nullptr;
    if (!stock) {
        log::Write("d3d11 driver: no current graphics driver at PreAppInit");
        return false;
    }
    // The same mode list as the stock driver, so the game picks the same mode.
    std::vector<Mode> modes(stock->CountVideoModes());
    for (uint32_t i = 0; i < modes.size(); ++i)
        stock->GetVideoModeInfo(i, reinterpret_cast<sGDMode&>(modes[i]));
    auto* driver = new Driver(std::move(modes));
    driver->AddRef(); // ours: the graphics system's reference counting of SetGDriver is unknown
    system->SetGDriver(driver);
    if (system->GetGDriver() != driver) {
        log::Write("d3d11 driver: SetGDriver did not take the driver");
        return false;
    }
    log::Write("d3d11 driver: installed in place of driver 0x%08X (%u video modes)", stock->GetGZCLSID(),
               driver->CountVideoModes());
    return true;
}

const bool g_registered = registry::Register({
    "renderer.d3d11",
    "renderer",
    registry::Kind::Api,
    registry::Phase::PreAppInit,
    0,
    "OpenSC4's Direct3D 11 graphics driver (P1 step 3, in progress)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::gd
