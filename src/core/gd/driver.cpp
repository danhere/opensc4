// renderer.d3d11: OpenSC4's own cIGZGDriver (P1 step 3). It keeps the game's
// fixed-function state the way the DirectX 7 driver does and draws through a
// Backend (backend.h); semantics are in docs/renderer.md, "What the driver
// calls mean".
//
// It takes over the stock OpenGL driver's class ID, like scvk and SCGL, and
// is selected as if the game had been started with -d:OpenGL unless -d:
// names another driver. Making it current later (SetGDriver at PreAppInit)
// is not enough: the game creates 3D textures through a driver pointer it
// caches before then, so they would land on the stock driver.
//
// Like the stock drivers it creates its own (ANSI) window in SetVideoMode and
// sends the window's messages to the procedure the game passes in.
#include "core/gd/backend.h"
#include "core/gd/frame_trace.h"
#include "core/gd/gd.h"
#include "core/gd/vertex_format.h"

#include "core/address_check.h"
#include "core/director.h"
#include "core/hook.h"
#include "core/iat.h"
#include "core/log.h"
#include "core/photo_mode.h"
#include "core/registry.h"
#include "core/settings.h"

#include <GZServPtrs.h>
#include <cIGZBuffer.h>
#include <cIGZCOM.h>
#include <cIGZFrameWork.h>
#include <cIGZGDriver.h>
#include <cIGZGraphicSystem2.h>
#include <SC4Preferences.h>

#include <windows.h>
#include <windowsx.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace opensc4::gd {
namespace {

// The stock OpenGL driver's class ID, which this driver replaces.
constexpr uint32_t kClassId = 0xC4554841;
constexpr uint32_t kCLSID_cGZBuffer = 0xC470D325;
constexpr uint32_t kStages = kMaxStages;
// GenTextures handles (the GL-style path, unused by the game so far) stay
// clear of the backend's CreateTexture handles.
constexpr uint32_t kGenTextureBase = 0x40000000;

constexpr char kWindowClass[] = "GDriverClass (OpenSC4 D3D11)";

// Error codes the DirectX driver reports through GetError.
enum Error : uint32_t { NoError, InvalidEnum, InvalidValue, InvalidOperation };

using StageState = StageDesc;

// A mode's size in physical pixels: with a render scale the game sees the
// mode's size divided by it ([renderer] scale), the window and back buffer
// have this size.
struct PhysicalSize {
    uint32_t width, height;
};

class Driver;
Driver* g_output = nullptr; // the driver whose window is up

class BufferRegions final : public BufferRegionExtension {
public:
    explicit BufferRegions(Driver& driver) : driver_(driver) {}
    bool QueryInterface(uint32_t iid, void** object) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    bool BufferRegionEnabled() override { return true; }
    int32_t NewBufferRegion(int32_t surface) override;
    bool DeleteBufferRegion(int32_t region) override;
    bool ReadBufferRegion(int32_t region, int32_t regionX, int32_t regionY, int32_t width, int32_t height, int32_t x,
                          int32_t y) override;
    bool DrawBufferRegion(int32_t region, int32_t regionX, int32_t regionY, int32_t width, int32_t height, int32_t x,
                          int32_t y) override;
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

class DynamicBuffers final : public VertexBuffers {
public:
    explicit DynamicBuffers(Driver& driver) : driver_(driver) {}
    bool QueryInterface(uint32_t iid, void** object) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    int32_t GetBuffer(uint32_t vertexFormat) override;
    uint32_t BufferFormat(int32_t buffer) override;
    int32_t Capacity(int32_t buffer) override;
    void* Lock(int32_t buffer, int32_t count) override;
    void* Append(int32_t buffer, int32_t count) override;
    void Unlock(int32_t buffer) override;
    void Draw(int32_t buffer, uint32_t primitive) override;
    void DrawIndexed(int32_t buffer, uint32_t primitive, int32_t count, const uint16_t* indices) override;
    void Reset() override;

private:
    struct Buffer {
        uint32_t format;
        uint32_t stride;
        int32_t capacity; // vertices: 64 KB worth, like the DirectX driver (0x008898D0)
        std::vector<uint8_t> data;
        int32_t position = 0;   // end of the current batch
        int32_t batchStart = 0; // first vertex of the current batch
        bool locked = false;
    };
    Buffer* Find(int32_t buffer);

    Driver& driver_;
    std::vector<Buffer> buffers_;
};

// Logs the first call of each method that does not do its job yet.
void Unimplemented(const char* method)
{
    static std::map<const char*, int> seen;
    if (seen[method]++ == 0)
        log::Write("d3d11 driver: %s is not implemented yet", method);
}

// The window the game sees at a scale ([renderer] scale), for the import
// wrappers below; null when the scale is 1.
HWND g_scaledWindow = nullptr;
float g_scale = 1.0f;

class Driver final : public cIGZGDriver {
public:
    Driver(std::vector<Mode> modes, std::vector<PhysicalSize> physical, float scale)
        : modes_(std::move(modes)), physical_(std::move(physical)), scale_(scale), regions_(*this), snapshots_(*this),
          vertexBuffers_(*this)
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
        } else if (iid == kIID_VertexBuffers) {
            *object = static_cast<VertexBuffers*>(&vertexBuffers_);
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
        ++draws_;
        if (!backend_ || count <= 0 || first < 0 || !vertexData_)
            return;
        Submit({prim, vertexFormat_, vertexStride_, static_cast<const uint8_t*>(vertexData_),
                                 static_cast<uint32_t>(first), static_cast<uint32_t>(count), nullptr, 0});
    }

    void DrawElements(uint32_t prim, int32_t count, uint32_t type, void const* indices) override
    {
        ++draws_;
        if (!backend_ || count <= 0 || !indices || !vertexData_)
            return;
        if (type != UnsignedByte && type != UnsignedShort && type != UnsignedInt) {
            error_ = InvalidEnum;
            return;
        }
        Submit({prim, vertexFormat_, vertexStride_, static_cast<const uint8_t*>(vertexData_), 0,
                                 static_cast<uint32_t>(count), indices, type});
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
            backend_->Clear(Physical(ViewportRect()), mask, clearColor_, clearDepth_, clearStencil_);
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
        if (param < 4 && value >= 0 && static_cast<uint32_t>(value) <= LinearMipmapLinear)
            stages_[stage_].params[param] = static_cast<uint32_t>(value);
        else
            error_ = InvalidEnum;
    }

    void Fog(uint32_t param, uint32_t value) override
    {
        if (param == FogMode)
            fogMode_ = value;
    }
    void Fog(uint32_t param, float const* values) override
    {
        switch (param) {
        case FogColor: std::memcpy(fogColor_, values, sizeof(fogColor_)); break;
        case FogDensity: fogDensity_ = values[0]; break;
        case FogStart: fogStart_ = values[0]; break;
        case FogEnd: fogEnd_ = values[0]; break;
        default: error_ = InvalidEnum; break;
        }
    }

    // Both stock drivers switch lighting on at the first call and leave it on.
    void ColorMultiplier(float r, float g, float b) override
    {
        colorMultiplier_[0] = r, colorMultiplier_[1] = g, colorMultiplier_[2] = b;
        lighting_ = true;
    }
    // Lit alpha comes from Direct3D's diffuse material source, which the
    // DirectX driver sets in EnableVertexColors from its second argument
    // (vertex colour if set, else the material), to the material when
    // AlphaMultiplier is below 1, and back to the vertex colour at 1 if that
    // argument was set (0x008833E0, 0x00883310). The decompiler shows a
    // constant for the first case; the disassembly reads the argument.
    void AlphaMultiplier(float a) override
    {
        colorMultiplier_[3] = a;
        lighting_ = true;
        if (a < 1.0f)
            diffuseFromVertex_ = false;
        else if (vertexSpecular_)
            diffuseFromVertex_ = true;
    }
    void EnableVertexColors(bool diffuse, bool specular) override
    {
        vertexColors_ = diffuse;
        vertexSpecular_ = specular;
        diffuseFromVertex_ = specular;
    }

    void GenTextures(int32_t n, uint32_t* textures) override
    {
        Unimplemented("GenTextures");
        for (int32_t i = 0; i < n; ++i)
            textures[i] = nextGenTexture_++;
    }
    void DeleteTextures(int32_t n, uint32_t const* textures) override
    {
        for (int32_t i = 0; i < n; ++i) {
            if (backend_)
                backend_->DeleteTexture(textures[i]);
            textures_.erase(textures[i]);
        }
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
        if (backend_) {
            backend_->Present();
            g_output = this; // the driver whose frames are on screen
            KeepShowingFront();
        }
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
    // The matrix applies to the stage's coordinates as given (GL layout). The
    // DirectX driver (0x00883600) negates its third row for (4 inputs, flag 1)
    // and (3 inputs, no flag 1), but doing the same here moves the terrain
    // shadows away from the stock driver's: Direct3D 7's camera-space
    // coordinates evidently have the opposite z already.
    void TexStageMatrix(float const* m, uint32_t inputs, uint32_t outputs, uint32_t flags) override
    {
        StageState& s = stages_[stage_];
        s.hasMatrix = m != nullptr;
        if (!m)
            return;
        LogTexStageMatrix(inputs, outputs, flags);
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
        if (texture && !textures_.count(texture)) {
            static std::map<uint32_t, int> seen;
            if (seen[texture]++ == 0 && seen.size() < 20)
                log::Write("d3d11 driver: SetTexture(0x%X, %u): not a texture we created", texture, stage);
        }
        if (stage < kStages)
            stages_[stage].texture = texture;
    }
    intptr_t GetTexture(uint32_t stage) override { return stage < kStages ? stages_[stage].texture : 0; }

    intptr_t CreateTexture(uint32_t format, uint32_t width, uint32_t height, uint32_t levels, uint32_t) override
    {
        const uint32_t id = backend_ ? backend_->CreateTexture(format, width, height, levels) : 0;
        if (id == 0) {
            error_ = InvalidOperation;
            return 0;
        }
        textures_.insert(id);
        frame_trace::TextureCreated(id, format, width, height, levels);
        return id;
    }

    void LoadTextureLevel(uint32_t texture, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height,
                          uint32_t format, uint32_t type, uint32_t rowLength, void const* pixels) override
    {
        ++textureUploads_;
        frame_trace::TextureUploaded(texture, level, x, y, width, height, format, type, rowLength, pixels);
        if (backend_ && level >= 0)
            backend_->UploadTexture(texture, static_cast<uint32_t>(level), x, y, width, height, format, type,
                                    rowLength, pixels);
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
        physicalWidth_ = physical_[index].width;
        physicalHeight_ = physical_[index].height;
        // The capabilities the DirectX driver reports in its current mode on a
        // current card (2026-10-06, RTX 3080 Ti): only the current mode has them.
        current_.redMask = 0xFF0000; // also in 16-bit modes, as the DirectX driver reports them
        current_.greenMask = 0xFF00;
        current_.blueMask = 0xFF;
        current_.stencil = 1;
        current_.textureStages = kStages;
        current_.multitexture = current_.envCombine = current_.fogCoordinate = 1;
        current_.dxt = 1;
        current_.unknown2F = 1;
        current_.initialized = 1;
        gameProc_ = windowProc ? reinterpret_cast<WNDPROC>(windowProc) : DefWindowProcA;
        if (!CreateOutput()) {
            DestroyOutput();
            error_ = static_cast<Error>(6); // the DirectX driver's code for a failed mode set
            return;
        }
        viewport_ = {0, 0, static_cast<int32_t>(current_.width), static_cast<int32_t>(current_.height)};
        log::Write("d3d11 driver: video mode %d, %ux%u, %s", index, current_.width, current_.height,
                   !current_.fullScreen ? "windowed" : displayChanged_ ? "full screen" : "full screen at desktop size");
        if (scale_ != 1.0f)
            log::Write("d3d11 driver: drawn at %ux%u (scale %.2f)", physicalWidth_, physicalHeight_, scale_);
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

    // The game parses this (0x007CF780) into Graphics Rules.sgr's sglDriverName,
    // sglDriverVersion, driverName, driverVersion, cardName and cardVersion,
    // one per line after the first, in the DirectX driver's format. The rules
    // match cardName ("Fast card": NVidia*Geforce*, ...), so it must be the real
    // adapter. sglDriverName stays "DirectX", the driver the rules were tuned on.
    char const* GetDriverInfo() const override
    {
        if (driverInfo_.empty()) {
            const AdapterInfo a = QueryAdapterInfo();
            char text[512];
            std::snprintf(text, sizeof(text),
                          "Maxis 3D GDriver\nDirectX\n2.0\nOpenSC4 Direct3D 11 driver\n%u.%u.%u.%u\n%s\n"
                          "Vendor: %04x, Device: %04x, Board: %08x, Chipset: %04x\n",
                          a.driverVersion[0], a.driverVersion[1], a.driverVersion[2], a.driverVersion[3], a.name.c_str(),
                          a.vendor, a.device, a.subsystem, a.revision);
            driverInfo_ = text;
        }
        return driverInfo_.c_str();
    }
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

    // Game pixels to physical pixels ([renderer] scale). Rectangles convert
    // their edges, so neighbouring rectangles stay neighbours.
    int32_t Physical(int32_t value) const { return static_cast<int32_t>(std::lround(value * scale_)); }
    Rect Physical(const Rect& r) const
    {
        const int32_t x0 = Physical(r.x), y0 = Physical(r.y);
        return {x0, y0, Physical(r.x + r.width) - x0, Physical(r.y + r.height) - y0};
    }
    float Scale() const { return scale_; }

    // The front buffer is shown when the game's main loop next handles
    // window messages, between its passes: a Flush can come in the middle of
    // a redraw (the 3D view's saved image restored, the changes not drawn
    // yet), and showing that one for a whole refresh flashes an older picture.
    // The stock drivers show every Flush at once, so such images last only
    // microseconds there. A frame the display cannot take yet is retried from
    // a timer.
    void KeepShowingFront()
    {
        if (window_ && backend_ && !showPosted_) {
            showPosted_ = true;
            PostMessageA(window_, kShowFrontMessage, 0, 0);
        }
    }

    // Draws vertices the dynamic buffers hold, with the current state.
    void DrawBuffered(uint32_t prim, uint32_t format, uint32_t stride, const uint8_t* vertices, uint32_t count,
                      const uint16_t* indices, uint32_t indexCount)
    {
        ++draws_;
        if (!backend_ || count == 0)
            return;
        if (indices)
            Submit({prim, format, stride, vertices, 0, indexCount, indices, UnsignedShort});
        else
            Submit({prim, format, stride, vertices, 0, count, nullptr, 0});
    }

    void LogStats(const char* when) const
    {
        log::Write("d3d11 driver: %s: %u frames, %u draws, %u textures, %u texture uploads", when, frames_, draws_,
                   static_cast<unsigned>(textures_.size()), textureUploads_);
    }

private:
    static void Identity(float* m)
    {
        std::memset(m, 0, 16 * sizeof(float));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // A draw to the backend, recorded for draw-call parity when traced.
    void Submit(const DrawCall& call)
    {
        const DrawState& state = State();
        frame_trace::Record(state, call);
        backend_->Draw(state, call);
    }

    // The state a draw uses, for the backend.
    const DrawState& State()
    {
        DrawState& s = drawState_;
        std::memcpy(s.modelView, modelView_, sizeof(s.modelView));
        std::memcpy(s.projection, projection_, sizeof(s.projection));
        s.viewport = Physical(ViewportRect());
        s.interfaceDraw = scale_ != 1.0f && PixelExactProjection();
        std::memcpy(s.enabled, enabled_, sizeof(s.enabled));
        s.depthFunc = depthFunc_;
        s.depthMask = depthMask_;
        s.stencilFunc = stencilFunc_;
        s.stencilRef = static_cast<uint32_t>(stencilRef_);
        s.stencilReadMask = stencilReadMask_;
        s.stencilWriteMask = stencilWriteMask_;
        std::memcpy(s.stencilOps, stencilOps_, sizeof(s.stencilOps));
        s.blendSrc = blendSrc_;
        s.blendDst = blendDst_;
        s.alphaFunc = alphaFunc_;
        s.alphaRef = alphaRef_;
        s.shadeModel = shadeModel_;
        s.polygonOffset = polygonOffset_;
        s.lighting = lighting_;
        std::memcpy(s.colorMultiplier, colorMultiplier_, sizeof(s.colorMultiplier));
        s.vertexColors = vertexColors_;
        s.diffuseFromVertex = diffuseFromVertex_;
        std::memcpy(s.textureFactor, textureFactor_, sizeof(s.textureFactor));
        s.fogMode = fogMode_;
        std::memcpy(s.fogColor, fogColor_, sizeof(s.fogColor));
        s.fogDensity = fogDensity_;
        s.fogStart = fogStart_;
        s.fogEnd = fogEnd_;
        s.stageCount = 0;
        for (uint32_t k = 0; k < kStages; ++k) {
            s.stages[k] = stages_[k];
            if (stages_[k].enabled)
                s.stageCount = k + 1;
        }
        return s;
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

    static void LogTexStageMatrix(uint32_t inputs, uint32_t outputs, uint32_t flags)
    {
        static std::map<uint32_t, int> seen;
        if (seen[(inputs << 16) | (outputs << 8) | flags]++ == 0)
            log::Write("d3d11 driver: first TexStageMatrix with %u inputs, %u outputs, flags 0x%X", inputs, outputs,
                       flags);
    }

    // Combiner args: types 0-3 RGB args, 4-7 alpha args (3 used of each).
    void SetArg(uint32_t type, uint8_t value, uint8_t mask)
    {
        Combiner& c = stages_[stage_].combiner;
        uint8_t* arg = type < 4 ? &c.rgbArg[type < 3 ? type : 2] : &c.alphaArg[type - 4 < 3 ? type - 4 : 2];
        *arg = static_cast<uint8_t>((*arg & ~mask) | (value & mask));
    }

    // A projection mapping one unit to one pixel of the viewport (the
    // interface's windows draw so, each in its own viewport; the city does not).
    bool PixelExactProjection() const
    {
        if (viewport_.width <= 0 || viewport_.height <= 0)
            return false;
        const float* p = projection_;
        const float x = p[0] * static_cast<float>(viewport_.width) * 0.5f;
        const float y = p[5] * static_cast<float>(viewport_.height) * 0.5f;
        return std::fabs(std::fabs(x) - 1.0f) < 1e-3f && std::fabs(std::fabs(y) - 1.0f) < 1e-3f &&
               std::fabs(p[1]) < 1e-6f && std::fabs(p[4]) < 1e-6f;
    }

    Rect ViewportRect() const
    {
        return {viewport_.x, static_cast<int32_t>(current_.height) - viewport_.y - viewport_.height, viewport_.width,
                viewport_.height};
    }

public:
    // Photo mode's presenting timer (see WindowProc).
    void StartPhotoTimer()
    {
        if (window_)
            SetTimer(window_, kPhotoTimer, 15, nullptr);
    }

    // The whole front buffer (photo mode).
    bool ReadWholeFront(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height)
    {
        if (!backend_)
            return false;
        width = backend_->Width();
        height = backend_->Height();
        pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
        return backend_->ReadFront({0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)}, pixels.data(),
                                   width * 4);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<Driver*>(GetWindowLongPtrA(window, GWLP_USERDATA));
        if (!self)
            self = creating_;
        if (self && message == WM_ACTIVATEAPP)
            self->OnActivateApp(wParam != 0);
        if (self && message == kShowFrontMessage) {
            self->showPosted_ = false;
            if (self->backend_ && self->backend_->ShowFront())
                SetTimer(window, kShowFrontTimer, USER_TIMER_MINIMUM, nullptr);
            return 0; // ours: the game never sees it
        }
        if (self && message >= WM_KEYFIRST && message <= WM_KEYLAST && photo_mode::OnKey(message, wParam))
            return 0; // photo mode's key: the game never sees it
        // Photo mode: with every interface window hidden the game stops
        // calling Flush (its window manager has nothing to update), so the
        // finished back buffer is shown on a timer instead.
        if (self && message == WM_TIMER && wParam == kPhotoTimer) {
            if (photo_mode::Active() && self->backend_) {
                self->backend_->Present();
                self->KeepShowingFront();
            } else {
                KillTimer(window, kPhotoTimer);
            }
            return 0;
        }
        if (self && message == WM_TIMER && wParam == kShowFrontTimer) {
            if (!self->backend_ || !self->backend_->ShowFront())
                KillTimer(window, kShowFrontTimer);
            return 0; // our timer: the game never sees it
        }
        // The game asks about the window while it is still being created
        // (WM_CREATE, the first WM_SIZE), before CreateWindowEx returns.
        if (self && self->scale_ != 1.0f && g_scaledWindow != window)
            g_scaledWindow = window;
        if (self && self->scale_ != 1.0f)
            lParam = self->ToGameCoordinates(message, lParam);
        if (self && self->gameProc_)
            return CallWindowProcA(self->gameProc_, window, message, wParam, lParam);
        return DefWindowProcA(window, message, wParam, lParam);
    }

    // Coordinates in messages are physical; the game expects its own: client
    // coordinates, and screen coordinates in the wheel and move messages (it
    // sees a scaled desktop, see GameScreenToClient).
    LPARAM ToGameCoordinates(UINT message, LPARAM lParam) const
    {
        const bool client = message == WM_SIZE || message == WM_MOVE || message == WM_MOUSEWHEEL ||
                            message == WM_MOUSEHWHEEL || (message >= WM_MOUSEMOVE && message <= WM_MBUTTONDBLCLK) ||
                            (message >= WM_XBUTTONDOWN && message <= WM_XBUTTONDBLCLK) || message == WM_MOUSEHOVER;
        if (!client)
            return lParam;
        const auto x = static_cast<int>(std::floor(GET_X_LPARAM(lParam) / scale_));
        const auto y = static_cast<int>(std::floor(GET_Y_LPARAM(lParam) / scale_));
        return MAKELPARAM(x, y);
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
        if (current_.fullScreen && !SetDisplayMode())
            return false;
        // The DirectX driver's windowed style (0x10CF0000), or the OpenGL
        // driver's popup (0x87000000) for full-screen modes.
        const DWORD style = current_.fullScreen ? (WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)
                                                : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        RECT rect{0, 0, static_cast<LONG>(physicalWidth_), static_cast<LONG>(physicalHeight_)};
        AdjustWindowRect(&rect, style, FALSE);
        creating_ = this;
        window_ = CreateWindowExA(0, kWindowClass, "SimCity 4", style, 0, 0, rect.right - rect.left,
                                  rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        creating_ = nullptr;
        if (!window_)
            return false;
        SetWindowLongPtrA(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        g_output = this;
        backend_ = CreateD3D11Backend();
        g_scaledWindow = scale_ != 1.0f ? window_ : nullptr;
        g_scale = scale_;
        if (!backend_->Init(window_, physicalWidth_, physicalHeight_)) {
            backend_.reset();
            return false;
        }
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        return true;
    }

    void DestroyOutput()
    {
        if (g_output == this)
            g_output = nullptr;
        if (backend_)
            LogStats("closing the window");
        backend_.reset();
        if (window_) {
            if (g_scaledWindow == window_)
                g_scaledWindow = nullptr;
            SetWindowLongPtrA(window_, GWLP_USERDATA, 0);
            DestroyWindow(window_);
            window_ = nullptr;
        }
        RestoreDisplayMode();
    }

    // Full-screen modes change the display mode as the stock OpenGL driver
    // does (0x0087FC90): width and height only, CDS_FULLSCREEN so that Windows
    // undoes it if the game dies, and back to the desktop's settings when the
    // mode goes. A mode the size of the desktop needs no change: its window
    // simply covers the screen. Scaling a smaller mode to the desktop instead
    // would need the game's own cursor calls (GetCursorPos, SetCursorPos)
    // translated too.
    bool SetDisplayMode()
    {
        DEVMODEA desktop{};
        desktop.dmSize = sizeof(desktop);
        if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &desktop))
            return false;
        if (displayChanged_ || (desktop.dmPelsWidth == physicalWidth_ && desktop.dmPelsHeight == physicalHeight_))
            return true;
        DEVMODEA mode = desktop;
        mode.dmPelsWidth = physicalWidth_;
        mode.dmPelsHeight = physicalHeight_;
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        const LONG result = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN);
        if (result != DISP_CHANGE_SUCCESSFUL) {
            log::Write("d3d11 driver: changing the display to %ux%u failed (%ld)", physicalWidth_, physicalHeight_,
                       result);
            return false;
        }
        displayChanged_ = true;
        return true;
    }

    void RestoreDisplayMode()
    {
        if (!displayChanged_)
            return;
        ChangeDisplaySettingsA(nullptr, 0);
        displayChanged_ = false;
    }

    // Switching away from a full-screen mode gives the desktop its own
    // resolution back until the game is active again (DirectDraw's exclusive
    // mode, which the DirectX driver uses, does the same).
    void OnActivateApp(bool active)
    {
        if (!current_.fullScreen || !window_)
            return;
        if (active) {
            if (SetDisplayMode()) {
                ShowWindow(window_, SW_RESTORE);
                SetWindowPos(window_, HWND_TOP, 0, 0, static_cast<int>(physicalWidth_),
                             static_cast<int>(physicalHeight_), SWP_NOACTIVATE);
            }
        } else if (displayChanged_) {
            ShowWindow(window_, SW_MINIMIZE);
            RestoreDisplayMode();
        }
    }

    static inline Driver* creating_ = nullptr;
    static constexpr UINT_PTR kShowFrontTimer = 0x05C40001;
    static constexpr UINT_PTR kPhotoTimer = 0x05C40002;
    static constexpr UINT kShowFrontMessage = WM_APP + 0x5C4;
    bool showPosted_ = false;

    std::atomic<uint32_t> refs_{0};
    std::vector<Mode> modes_;
    std::vector<PhysicalSize> physical_; // per mode
    float scale_ = 1.0f;
    uint32_t physicalWidth_ = 0, physicalHeight_ = 0;
    Mode current_{};
    HWND window_ = nullptr;
    bool displayChanged_ = false;
    mutable std::string driverInfo_;
    WNDPROC gameProc_ = nullptr;
    std::unique_ptr<Backend> backend_;
    BufferRegions regions_;
    Snapshots snapshots_;
    DynamicBuffers vertexBuffers_;
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
    bool lighting_ = false;
    bool vertexColors_ = true, vertexSpecular_ = false, diffuseFromVertex_ = true;
    float textureFactor_[4] = {1, 1, 1, 1};
    uint32_t fogMode_ = FogExp;
    float fogColor_[4]{};
    float fogDensity_ = 1.0f, fogStart_ = 0.0f, fogEnd_ = 1.0f;

    uint32_t matrixMode_ = ModelView;
    float modelView_[16];
    float projection_[16];

    uint32_t stage_ = 0;
    StageState stages_[kStages];
    std::set<uint32_t> textures_; // backend handles from CreateTexture
    uint32_t nextGenTexture_ = kGenTextureBase;
    int32_t unpackRowLength_ = 0;

    uint32_t vertexFormat_ = 0, vertexStride_ = 0;
    const void* vertexData_ = nullptr;

    DrawState drawState_{};
    uint32_t frames_ = 0, draws_ = 0, textureUploads_ = 0;
};

// Extensions share the driver's reference count.

bool BufferRegions::QueryInterface(uint32_t iid, void** object) { return driver_.QueryInterface(iid, object); }
uint32_t BufferRegions::AddRef() { return driver_.AddRef(); }
uint32_t BufferRegions::Release() { return driver_.Release(); }

// The game's surface numbers are the DirectX driver's (0x00886750): 0 the
// render target, the back buffer it draws into (+0x22C, from the device's
// GetRenderTarget); 1 and 2 depth surfaces (created with DDSCAPS_ZBUFFER).
int32_t BufferRegions::NewBufferRegion(int32_t surface)
{
    Backend* backend = driver_.GetBackend();
    if (!backend || surface < 0 || surface > 2)
        return 0;
    return backend->CreateRegion(surface == 0 ? BackBuffer : DepthBuffer);
}

bool BufferRegions::DeleteBufferRegion(int32_t region)
{
    Backend* backend = driver_.GetBackend();
    return backend && backend->DeleteRegion(region);
}

bool BufferRegions::ReadBufferRegion(int32_t region, int32_t regionX, int32_t regionY, int32_t width,
                                     int32_t height, int32_t x, int32_t y)
{
    Backend* backend = driver_.GetBackend();
    return backend && backend->ReadRegion(region, driver_.Physical(Rect{x, y, width, height}), driver_.Physical(regionX),
                                          driver_.Physical(regionY));
}

bool BufferRegions::DrawBufferRegion(int32_t region, int32_t regionX, int32_t regionY, int32_t width,
                                     int32_t height, int32_t x, int32_t y)
{
    Backend* backend = driver_.GetBackend();
    const bool ok = backend && backend->DrawRegion(region, driver_.Physical(Rect{x, y, width, height}),
                                                   driver_.Physical(regionX), driver_.Physical(regionY));
    driver_.KeepShowingFront();
    return ok;
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
    if (bits && driver_.Scale() == 1.0f) {
        backend->ReadFront({x, y, w, h}, bits, pitch);
    } else if (bits) {
        // Read the physical pixels and give the game its own size back (nearest).
        const Rect physical = driver_.Physical(Rect{x, y, w, h});
        std::vector<uint8_t> scaled(static_cast<size_t>(physical.width) * physical.height * 4);
        if (physical.width > 0 && physical.height > 0 &&
            backend->ReadFront(physical, scaled.data(), static_cast<uint32_t>(physical.width) * 4)) {
            for (int32_t row = 0; row < h; ++row) {
                const int32_t sy = std::min(physical.height - 1, static_cast<int32_t>(row * driver_.Scale()));
                for (int32_t col = 0; col < w; ++col) {
                    const int32_t sx = std::min(physical.width - 1, static_cast<int32_t>(col * driver_.Scale()));
                    std::memcpy(bits + static_cast<size_t>(row) * pitch + static_cast<size_t>(col) * 4,
                                scaled.data() + (static_cast<size_t>(sy) * physical.width + sx) * 4, 4);
                }
            }
        }
    }
    buffer->Unlock(kLockWrite);
    return buffer;
}

bool DynamicBuffers::QueryInterface(uint32_t iid, void** object)
{
    if (iid != kIID_VertexBuffers && iid != GZIID_cIGZUnknown)
        return false;
    *object = static_cast<VertexBuffers*>(this);
    AddRef();
    return true;
}
uint32_t DynamicBuffers::AddRef() { return driver_.AddRef(); }
uint32_t DynamicBuffers::Release() { return driver_.Release(); }

int32_t DynamicBuffers::GetBuffer(uint32_t vertexFormat)
{
    for (size_t i = 0; i < buffers_.size(); ++i)
        if (buffers_[i].format == vertexFormat)
            return static_cast<int32_t>(i);
    Buffer b{};
    b.format = vertexFormat;
    b.stride = Stride(vertexFormat);
    if (b.stride == 0)
        return -1;
    b.capacity = static_cast<int32_t>(0x10000 / b.stride);
    b.data.resize(static_cast<size_t>(b.capacity) * b.stride);
    buffers_.push_back(std::move(b));
    log::Write("d3d11 driver: dynamic vertex buffer %u for format 0x%08X (%u vertices of %u bytes)",
               static_cast<unsigned>(buffers_.size() - 1), vertexFormat, buffers_.back().capacity,
               buffers_.back().stride);
    return static_cast<int32_t>(buffers_.size() - 1);
}

DynamicBuffers::Buffer* DynamicBuffers::Find(int32_t buffer)
{
    return buffer >= 0 && static_cast<size_t>(buffer) < buffers_.size() ? &buffers_[buffer] : nullptr;
}

uint32_t DynamicBuffers::BufferFormat(int32_t buffer)
{
    Buffer* b = Find(buffer);
    return b ? b->format : 0;
}

int32_t DynamicBuffers::Capacity(int32_t buffer)
{
    Buffer* b = Find(buffer);
    return b ? b->capacity : 0;
}

void* DynamicBuffers::Lock(int32_t buffer, int32_t count)
{
    Buffer* b = Find(buffer);
    if (!b || b->locked || count < 0 || count > b->capacity)
        return nullptr;
    if (b->position + count > b->capacity)
        b->position = 0; // the DirectX driver locks with DISCARD here
    b->batchStart = b->position;
    b->position += count;
    b->locked = true;
    return b->data.data() + static_cast<size_t>(b->batchStart) * b->stride;
}

void* DynamicBuffers::Append(int32_t buffer, int32_t count)
{
    Buffer* b = Find(buffer);
    if (!b || !b->locked || count < 0 || b->position + count > b->capacity)
        return nullptr;
    const int32_t at = b->position;
    b->position += count;
    return b->data.data() + static_cast<size_t>(at) * b->stride;
}

void DynamicBuffers::Unlock(int32_t buffer)
{
    if (Buffer* b = Find(buffer))
        b->locked = false;
}

void DynamicBuffers::Draw(int32_t buffer, uint32_t primitive)
{
    Buffer* b = Find(buffer);
    if (!b)
        return;
    driver_.DrawBuffered(primitive, b->format, b->stride, b->data.data() + static_cast<size_t>(b->batchStart) * b->stride,
                         static_cast<uint32_t>(b->position - b->batchStart), nullptr, 0);
}

void DynamicBuffers::DrawIndexed(int32_t buffer, uint32_t primitive, int32_t count, const uint16_t* indices)
{
    Buffer* b = Find(buffer);
    if (!b || !indices || count <= 0)
        return;
    driver_.DrawBuffered(primitive, b->format, b->stride, b->data.data() + static_cast<size_t>(b->batchStart) * b->stride,
                         static_cast<uint32_t>(b->position - b->batchStart), indices, static_cast<uint32_t>(count));
}

void DynamicBuffers::Reset()
{
    for (Buffer& b : buffers_) {
        b.position = b.batchStart = 0;
        b.locked = false;
    }
}

// [renderer] scale: physical pixels per game pixel, 1 to 4. With 1.5 on a
// 3440x1440 screen the game runs at 2293x960 and everything it draws is shown
// 1.5 times as big.
float RenderScale()
{
    static const float scale = [] {
        const std::string text = settings::Get("renderer", "scale", "1");
        const float value = static_cast<float>(std::atof(text.c_str()));
        if (!(value >= 1.0f && value <= 4.0f)) {
            log::Write("d3d11 driver: [renderer] scale = \"%s\" is not between 1 and 4; using 1", text.c_str());
            return 1.0f;
        }
        return value;
    }();
    return scale;
}

uint32_t GameSize(uint32_t physical, float scale)
{
    return static_cast<uint32_t>(std::lround(physical / scale));
}

// With a render scale the game sees a desktop scaled down by the same factor,
// the way Windows virtualises coordinates for programs that are not DPI
// aware: client and screen coordinates, window rectangles, the work area, the
// cursor position and the screen's size are divided by the scale, and what
// the game hands back is multiplied. Its window layer mixes them (it keeps the
// client area as a screen rectangle, 0x0098D9B0; it takes the screen size as
// its own in full screen, 0x0098ADE0), so all of them have to agree. Patched
// in the exe's import table only: other modules, this one included, see
// Windows as it is. Our window's own messages are translated in WindowProc.
using PointFn = BOOL(WINAPI*)(HWND, LPPOINT);
using RectFn = BOOL(WINAPI*)(HWND, LPRECT);
using MapFn = int(WINAPI*)(HWND, HWND, LPPOINT, UINT);
using MetricsFn = int(WINAPI*)(int);
using CursorFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorFn = BOOL(WINAPI*)(int, int);
using MoveFn = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using PosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using ParamsFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT);
using PlacementFn = BOOL(WINAPI*)(HWND, WINDOWPLACEMENT*);
PointFn g_screenToClient = nullptr, g_clientToScreen = nullptr;
RectFn g_getClientRect = nullptr, g_getWindowRect = nullptr;
MapFn g_mapWindowPoints = nullptr;
MetricsFn g_getSystemMetrics = nullptr;
CursorFn g_getCursorPos = nullptr;
SetCursorFn g_setCursorPos = nullptr;
MoveFn g_moveWindow = nullptr;
PosFn g_setWindowPos = nullptr;
ParamsFn g_systemParametersInfo = nullptr;
PlacementFn g_getWindowPlacement = nullptr;

bool Scaled(HWND window)
{
    return window && window == g_scaledWindow;
}

LONG ToGame(LONG value)
{
    return static_cast<LONG>(std::floor(value / g_scale));
}

LONG ToPhysical(LONG value)
{
    return static_cast<LONG>(std::lround(value * g_scale));
}

void ToGame(POINT& p)
{
    p.x = ToGame(p.x);
    p.y = ToGame(p.y);
}

void ToPhysical(POINT& p)
{
    p.x = ToPhysical(p.x);
    p.y = ToPhysical(p.y);
}

void ToGame(RECT& r)
{
    r.left = ToGame(r.left);
    r.top = ToGame(r.top);
    r.right = ToGame(r.right);
    r.bottom = ToGame(r.bottom);
}

BOOL WINAPI GameScreenToClient(HWND window, LPPOINT point)
{
    if (!point || !Scaled(window))
        return g_screenToClient(window, point);
    ToPhysical(*point);
    const BOOL ok = g_screenToClient(window, point);
    ToGame(*point);
    return ok;
}

BOOL WINAPI GameClientToScreen(HWND window, LPPOINT point)
{
    if (!point || !Scaled(window))
        return g_clientToScreen(window, point);
    ToPhysical(*point);
    const BOOL ok = g_clientToScreen(window, point);
    ToGame(*point);
    return ok;
}

int WINAPI GameMapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count)
{
    // Our window's client area and the screen are both in game coordinates.
    const bool fromScaled = g_scaledWindow && (!from || Scaled(from));
    const bool toScaled = g_scaledWindow && (!to || Scaled(to));
    if (points && fromScaled)
        for (UINT i = 0; i < count; ++i)
            ToPhysical(points[i]);
    const int result = g_mapWindowPoints(from, to, points, count);
    if (points && toScaled)
        for (UINT i = 0; i < count; ++i)
            ToGame(points[i]);
    return result;
}

BOOL WINAPI GameGetClientRect(HWND window, LPRECT rect)
{
    const BOOL ok = g_getClientRect(window, rect);
    if (ok && rect && Scaled(window))
        ToGame(*rect);
    return ok;
}

BOOL WINAPI GameGetWindowRect(HWND window, LPRECT rect)
{
    const BOOL ok = g_getWindowRect(window, rect);
    if (ok && rect && Scaled(window))
        ToGame(*rect);
    return ok;
}

BOOL WINAPI GameGetWindowPlacement(HWND window, WINDOWPLACEMENT* placement)
{
    const BOOL ok = g_getWindowPlacement(window, placement);
    if (ok && placement && Scaled(window))
        ToGame(placement->rcNormalPosition);
    return ok;
}

BOOL WINAPI GameMoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint)
{
    if (Scaled(window))
        return g_moveWindow(window, ToPhysical(x), ToPhysical(y), ToPhysical(width), ToPhysical(height), repaint);
    return g_moveWindow(window, x, y, width, height, repaint);
}

BOOL WINAPI GameSetWindowPos(HWND window, HWND after, int x, int y, int width, int height, UINT flags)
{
    if (Scaled(window))
        return g_setWindowPos(window, after, ToPhysical(x), ToPhysical(y), ToPhysical(width), ToPhysical(height),
                              flags);
    return g_setWindowPos(window, after, x, y, width, height, flags);
}

BOOL WINAPI GameGetCursorPos(LPPOINT point)
{
    const BOOL ok = g_getCursorPos(point);
    if (ok && point && g_scaledWindow)
        ToGame(*point);
    return ok;
}

BOOL WINAPI GameSetCursorPos(int x, int y)
{
    if (g_scaledWindow)
        return g_setCursorPos(ToPhysical(x), ToPhysical(y));
    return g_setCursorPos(x, y);
}

BOOL WINAPI GameSystemParametersInfo(UINT action, UINT param, PVOID data, UINT flags)
{
    const BOOL ok = g_systemParametersInfo(action, param, data, flags);
    if (ok && data && action == SPI_GETWORKAREA && g_scale != 1.0f)
        ToGame(*static_cast<RECT*>(data));
    return ok;
}

using DisplayFn = BOOL(WINAPI*)(LPCSTR, DWORD, DEVMODEA*);
using CapsFn = int(WINAPI*)(HDC, int);
DisplayFn g_enumDisplaySettings = nullptr;
CapsFn g_getDeviceCaps = nullptr;

// The screen's size, as metrics, display modes and device capabilities:
// asked before the window exists, so these apply whenever a scale is set.
BOOL WINAPI GameEnumDisplaySettings(LPCSTR device, DWORD mode, DEVMODEA* settings)
{
    const BOOL ok = g_enumDisplaySettings(device, mode, settings);
    if (ok && settings && g_scale != 1.0f) {
        settings->dmPelsWidth = GameSize(settings->dmPelsWidth, g_scale);
        settings->dmPelsHeight = GameSize(settings->dmPelsHeight, g_scale);
    }
    return ok;
}

int WINAPI GameGetDeviceCaps(HDC dc, int index)
{
    const int value = g_getDeviceCaps(dc, index);
    const bool size = index == HORZRES || index == VERTRES || index == DESKTOPHORZRES || index == DESKTOPVERTRES;
    return size && g_scale != 1.0f ? static_cast<int>(GameSize(static_cast<uint32_t>(value), g_scale)) : value;
}

int WINAPI GameGetSystemMetrics(int index)
{
    const int value = g_getSystemMetrics(index);
    const bool size = index == SM_CXSCREEN || index == SM_CYSCREEN || index == SM_CXFULLSCREEN ||
                      index == SM_CYFULLSCREEN;
    return size && g_scale != 1.0f ? static_cast<int>(GameSize(static_cast<uint32_t>(value), g_scale)) : value;
}

void InstallImportWrappers(float scale)
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    g_scale = scale;
    struct Import {
        const char* name;
        void* replacement;
        void** original;
    };
    const Import imports[] = {
        {"ScreenToClient", reinterpret_cast<void*>(&GameScreenToClient), reinterpret_cast<void**>(&g_screenToClient)},
        {"ClientToScreen", reinterpret_cast<void*>(&GameClientToScreen), reinterpret_cast<void**>(&g_clientToScreen)},
        {"MapWindowPoints", reinterpret_cast<void*>(&GameMapWindowPoints), reinterpret_cast<void**>(&g_mapWindowPoints)},
        {"GetClientRect", reinterpret_cast<void*>(&GameGetClientRect), reinterpret_cast<void**>(&g_getClientRect)},
        {"GetWindowRect", reinterpret_cast<void*>(&GameGetWindowRect), reinterpret_cast<void**>(&g_getWindowRect)},
        {"GetWindowPlacement", reinterpret_cast<void*>(&GameGetWindowPlacement),
         reinterpret_cast<void**>(&g_getWindowPlacement)},
        {"MoveWindow", reinterpret_cast<void*>(&GameMoveWindow), reinterpret_cast<void**>(&g_moveWindow)},
        {"SetWindowPos", reinterpret_cast<void*>(&GameSetWindowPos), reinterpret_cast<void**>(&g_setWindowPos)},
        {"GetCursorPos", reinterpret_cast<void*>(&GameGetCursorPos), reinterpret_cast<void**>(&g_getCursorPos)},
        {"SetCursorPos", reinterpret_cast<void*>(&GameSetCursorPos), reinterpret_cast<void**>(&g_setCursorPos)},
        {"SystemParametersInfoA", reinterpret_cast<void*>(&GameSystemParametersInfo),
         reinterpret_cast<void**>(&g_systemParametersInfo)},
        {"GetSystemMetrics", reinterpret_cast<void*>(&GameGetSystemMetrics),
         reinterpret_cast<void**>(&g_getSystemMetrics)},
    };
    for (const Import& import : imports)
        if (!iat::Patch(nullptr, "USER32.dll", import.name, import.replacement, import.original))
            log::Write("d3d11 driver: could not wrap the game's %s; coordinates may be off", import.name);
    if (!iat::Patch(nullptr, "USER32.dll", "EnumDisplaySettingsA", &GameEnumDisplaySettings, &g_enumDisplaySettings))
        log::Write("d3d11 driver: could not wrap the game's EnumDisplaySettingsA; coordinates may be off");
    if (!iat::Patch(nullptr, "GDI32.dll", "GetDeviceCaps", &GameGetDeviceCaps, &g_getDeviceCaps))
        log::Write("d3d11 driver: could not wrap the game's GetDeviceCaps; coordinates may be off");
    log::Write("d3d11 driver: the game sees a desktop scaled by 1/%.2f", scale);
}

// The video modes, laid out as the DirectX driver lists them (seen on
// 2026-10-06): the desktop's resolution first, then every other resolution
// the display offers, each as a full-screen and a windowed mode, all at 32
// bits and then all again at 16 bits. The game picks the one matching its
// preferences (a saved 16-bit full-screen choice is common) and crashes when
// none matches. Colour masks are only filled in for the current mode. The
// driver renders in 32 bits whatever the mode says; changing the display
// mode changes only its size (current Windows has no 16-bit desktop modes).
std::vector<Mode> EnumerateModes(float scale, std::vector<PhysicalSize>& physical)
{
    std::vector<Mode> modes;
    DEVMODEA desktop{};
    desktop.dmSize = sizeof(desktop);
    if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &desktop))
        return modes;
    std::vector<std::pair<uint32_t, uint32_t>> sizes{{desktop.dmPelsWidth, desktop.dmPelsHeight}};
    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i) {
        const std::pair<uint32_t, uint32_t> size{dm.dmPelsWidth, dm.dmPelsHeight};
        if (std::find(sizes.begin(), sizes.end(), size) == sizes.end())
            sizes.push_back(size);
    }
    for (const uint32_t depth : {32u, 16u}) {
        for (const auto& [width, height] : sizes) {
            for (const bool fullScreen : {true, false}) {
                Mode m{};
                m.index = static_cast<int32_t>(modes.size());
                m.width = GameSize(width, scale);
                m.height = GameSize(height, scale);
                m.depth = depth;
                m.fullScreen = fullScreen;
                m.unknown22 = 1;
                modes.push_back(m);
                physical.push_back({width, height});
            }
        }
    }
    return modes;
}

cIGZUnknown* Create()
{
    const float scale = RenderScale();
    std::vector<PhysicalSize> physical;
    std::vector<Mode> modes = EnumerateModes(scale, physical);
    auto* driver = new Driver(std::move(modes), std::move(physical), scale);
    log::Write("d3d11 driver: created as class 0x%08X (%u video modes)", kClassId, driver->CountVideoModes());
    return static_cast<cIGZGDriver*>(driver);
}

// opensc4.ini [renderer] resolution (WxH or "desktop") and window (windowed,
// fullscreen, borderless), applied to the game's video preferences in memory
// just before it picks its driver and mode, as sc4-graphics-options does to
// the saved ones. Unset, the game's own options (or -r, -w, -f) apply.
// Borderless is full screen at the desktop's size, which needs no display
// mode change; a full-screen mode of another size changes the display mode.
void ApplyVideoSettings(SC4VideoPreferences& video)
{
    log::Write("d3d11 driver: game video preferences %ux%u, %u bits, %s", video.width, video.height, video.bitDepth,
               video.bFullScreen ? "full screen" : "windowed");
    std::string window = settings::Get("renderer", "window", "");
    std::string resolution = settings::Get("renderer", "resolution", "");
    // With a render scale the game's saved size (physical pixels) matches no
    // mode, and the game crashes on that: the ini decides, desktop size full
    // screen unless it says otherwise.
    const float scale = RenderScale();
    if (scale != 1.0f && resolution.empty())
        resolution = "desktop";
    if (scale != 1.0f && window.empty())
        window = "fullscreen";
    if (window.empty() && resolution.empty())
        return;
    DEVMODEA desktop{};
    desktop.dmSize = sizeof(desktop);
    EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &desktop);
    unsigned width = video.width, height = video.height;
    if (resolution == "desktop" || window == "borderless") {
        width = desktop.dmPelsWidth;
        height = desktop.dmPelsHeight;
    } else if (!resolution.empty() && std::sscanf(resolution.c_str(), "%ux%u", &width, &height) != 2) {
        log::Write("d3d11 driver: [renderer] resolution = \"%s\" is not WxH or desktop; ignored", resolution.c_str());
        width = video.width;
        height = video.height;
    }
    bool fullScreen = video.bFullScreen != 0;
    if (window == "windowed")
        fullScreen = false;
    else if (window == "fullscreen" || window == "borderless")
        fullScreen = true;
    else if (!window.empty())
        log::Write("d3d11 driver: [renderer] window = \"%s\" is not windowed, fullscreen or borderless; ignored",
                   window.c_str());
    width = GameSize(width, scale);
    height = GameSize(height, scale);
    log::Write("d3d11 driver: video preferences %ux%u %s -> %ux%u %s (opensc4.ini)", video.width, video.height,
               video.bFullScreen ? "full screen" : "windowed", width, height, fullScreen ? "full screen" : "windowed");
    video.width = width;
    video.height = height;
    video.bitDepth = 32;
    video.bFullScreen = fullScreen ? 1 : 0;
}

// cSC4App::SelectGraphicsDriver: request this driver when no -d: option
// asked for one, then put back the saved driver choice the game would
// otherwise overwrite (so turning the driver off leaves the user's settings
// as they were).
using SelectFn = void(__fastcall*)(void* app, void* edx, bool selectDriver);
SelectFn g_select = nullptr;
constexpr size_t kRequestedDriver = 0x65C;
constexpr size_t kSettings = 0x3B0;
constexpr size_t kSavedDriver = 0xF13;
static_assert(offsetof(SC4Preferences, videoPreferences) + offsetof(SC4VideoPreferences, driverType) == kSavedDriver);

void __fastcall SelectGraphicsDriver(void* app, void* edx, bool selectDriver)
{
    auto* base = static_cast<uint8_t*>(app);
    auto& requested = *reinterpret_cast<uint32_t*>(base + kRequestedDriver);
    uint8_t* settings = *reinterpret_cast<uint8_t**>(base + kSettings);
    if (!selectDriver || requested != 0 || !settings) {
        g_select(app, edx, selectDriver);
        return;
    }
    const uint8_t saved = settings[kSavedDriver];
    ApplyVideoSettings(reinterpret_cast<SC4Preferences*>(settings)->videoPreferences);
    requested = kClassId;
    g_select(app, edx, selectDriver);
    settings[kSavedDriver] = saved;
    log::Write("d3d11 driver: selected in place of the saved driver choice %u", saved);
}

// [renderer] scale_city = no (the default): only the interface is enlarged. The camera
// builds its orthographic box from pixels per tile (0x007CBE40), so dividing
// its fine zoom factor by the scale while it does makes each game pixel cover
// scale times as much of the city; the driver draws the city at the physical
// size, so it comes out at the size the stock game shows it at full
// resolution. Picking and scrolling use what this function stores, so they
// follow. The factor itself is put back for everything else.
using UpdateProjectionFn = void(__fastcall*)(void* camera, void* edx);
UpdateProjectionFn g_updateProjection = nullptr;
constexpr size_t kFineZoom = 0xF0;
float g_cityZoomDivisor = 1.0f;

void __fastcall UpdateProjection(void* camera, void* edx)
{
    float& fineZoom = *reinterpret_cast<float*>(static_cast<uint8_t*>(camera) + kFineZoom);
    const float saved = fineZoom;
    fineZoom = saved / RenderScale();
    g_updateProjection(camera, edx);
    fineZoom = saved;
}

bool Register(registry::Mode)
{
    // Before the game measures the screen for its window layer.
    if (RenderScale() != 1.0f)
        InstallImportWrappers(RenderScale());
    if (RenderScale() != 1.0f && !settings::GetBool("renderer", "scale_city", false)) {
        // Our camera control (renderer.camera.control) divides by it itself.
        g_cityZoomDivisor = RenderScale();
        const addr::Entry* projection = addr_check::Find(addr::cSC4CameraControl_UpdateProjection);
        if (projection && hook::Install(*projection, &UpdateProjection, &g_updateProjection))
            log::Write("d3d11 driver: the city keeps its full-resolution size; only the interface is enlarged");
        else
            log::Write("d3d11 driver: could not hook the camera projection; the city is enlarged too");
    }
    const addr::Entry* select = addr_check::Find(addr::cSC4App_SelectGraphicsDriver);
    if (!select || !hook::Install(*select, &SelectGraphicsDriver, &g_select))
        log::Write("d3d11 driver: could not hook the driver choice; start the game with -d:OpenGL");
    return director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "renderer.d3d11",
    "renderer",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "OpenSC4's Direct3D 11 graphics driver, in place of the OpenGL driver (P1 step 3)",
    false,
    Register,
});

} // namespace

float CityZoomDivisor() { return g_cityZoomDivisor; }

void PresentWhileActive()
{
    if (g_output)
        g_output->StartPhotoTimer();
}

bool ReadFrame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height)
{
    return g_output && g_output->ReadWholeFront(pixels, width, height);
}

} // namespace opensc4::gd
