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
#include "core/gd/gd.h"
#include "core/gd/vertex_format.h"

#include "core/address_check.h"
#include "core/director.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZServPtrs.h>
#include <cIGZBuffer.h>
#include <cIGZCOM.h>
#include <cIGZFrameWork.h>
#include <cIGZGDriver.h>
#include <cIGZGraphicSystem2.h>

#include <windows.h>

#include <opensc4/addresses.h>

#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <set>
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

class Driver final : public cIGZGDriver {
public:
    explicit Driver(std::vector<Mode> modes)
        : modes_(std::move(modes)), regions_(*this), snapshots_(*this), vertexBuffers_(*this)
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
        backend_->Draw(State(), {prim, vertexFormat_, vertexStride_, static_cast<const uint8_t*>(vertexData_),
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
        backend_->Draw(State(), {prim, vertexFormat_, vertexStride_, static_cast<const uint8_t*>(vertexData_), 0,
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
    // DirectX driver sets to the vertex colour in EnableVertexColors (whatever
    // its arguments) and to the material when AlphaMultiplier is below 1
    // (0x008833E0, 0x00883310).
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
        diffuseFromVertex_ = true;
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
        return id;
    }

    void LoadTextureLevel(uint32_t texture, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height,
                          uint32_t format, uint32_t type, uint32_t rowLength, void const* pixels) override
    {
        ++textureUploads_;
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

    // Draws vertices the dynamic buffers hold, with the current state.
    void DrawBuffered(uint32_t prim, uint32_t format, uint32_t stride, const uint8_t* vertices, uint32_t count,
                      const uint16_t* indices, uint32_t indexCount)
    {
        ++draws_;
        if (!backend_ || count == 0)
            return;
        if (indices)
            backend_->Draw(State(), {prim, format, stride, vertices, 0, indexCount, indices, UnsignedShort});
        else
            backend_->Draw(State(), {prim, format, stride, vertices, 0, count, nullptr, 0});
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

    // The state a draw uses, for the backend.
    const DrawState& State()
    {
        DrawState& s = drawState_;
        std::memcpy(s.modelView, modelView_, sizeof(s.modelView));
        std::memcpy(s.projection, projection_, sizeof(s.projection));
        s.viewport = ViewportRect();
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

// The video modes the stock OpenGL driver lists (0x0087F410): every
// resolution at the desktop's colour depth as a full-screen mode, plus a
// windowed one for each that is smaller than the desktop.
std::vector<Mode> EnumerateModes()
{
    std::vector<Mode> modes;
    DEVMODEA desktop{};
    desktop.dmSize = sizeof(desktop);
    if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &desktop) || desktop.dmBitsPerPel < 15)
        return modes;
    const bool deep = desktop.dmBitsPerPel > 16;
    auto add = [&](uint32_t width, uint32_t height, bool fullScreen) {
        Mode m{};
        m.index = static_cast<int32_t>(modes.size());
        m.width = width;
        m.height = height;
        m.depth = deep ? 32 : 16;
        m.alphaMask = deep ? 0xFF000000 : 0x1;
        m.redMask = deep ? 0xFF0000 : 0xF800;
        m.greenMask = deep ? 0xFF00 : 0x7C0;
        m.blueMask = deep ? 0xFF : 0x3E;
        m.fullScreen = fullScreen;
        m.unknown21 = m.unknown22 = 1;
        m.unknown24 = 1;
        m.unknown2C[3] = m.unknown2C[4] = 1;
        modes.push_back(m);
    };
    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel != desktop.dmBitsPerPel)
            continue;
        bool known = false;
        for (const Mode& m : modes)
            known = known || (m.width == dm.dmPelsWidth && m.height == dm.dmPelsHeight);
        if (known)
            continue;
        add(dm.dmPelsWidth, dm.dmPelsHeight, true);
        if (dm.dmPelsWidth < desktop.dmPelsWidth && dm.dmPelsHeight < desktop.dmPelsHeight)
            add(dm.dmPelsWidth, dm.dmPelsHeight, false);
    }
    return modes;
}

cIGZUnknown* Create()
{
    auto* driver = new Driver(EnumerateModes());
    log::Write("d3d11 driver: created as class 0x%08X (%u video modes)", kClassId, driver->CountVideoModes());
    return static_cast<cIGZGDriver*>(driver);
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
    requested = kClassId;
    g_select(app, edx, selectDriver);
    settings[kSavedDriver] = saved;
    log::Write("d3d11 driver: selected in place of the saved driver choice %u", saved);
}

bool Register(registry::Mode)
{
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
} // namespace opensc4::gd
