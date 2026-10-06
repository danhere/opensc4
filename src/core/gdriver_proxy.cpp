// Pass-through graphics driver (P1 step 1). Every cIGZGDriver call is counted
// and forwarded to the game's DirectX 7 driver; QueryInterface for any other
// interface returns the original's own pointer (and is logged).
//
// How it is installed: the graphics system creates exactly the three stock
// drivers (Software 0x7ACA35C6, DirectX 0xBADB6906, OpenGL 0xC4554841) between
// the director's OnStart and PreAppInit, and makes one of them current. It
// never instantiates other class IDs, even after SetDefaultDriverClassID or
// with cSC4App's driver ID field (0x65C, from -d:) set to one, so a driver
// under a new class ID is never created. Instead, at PreAppInit, the proxy
// wraps the current driver and is made current with
// cIGZGraphicSystem2::SetGDriver: public API, no class ID taken, no patching.
// The game sets its video mode after that, through the proxy.
//
// The game's drivers also implement four other interfaces (QueryInterface at
// 0x00882A00 for DirectX, 0x0087D670 for OpenGL): 0x669565FE (the object,
// 15 methods), 0xE69BFE2A (1 method), 0x87E2B87D (9 methods) and 0x09CD86F9
// (a separate object). Calls through those bypass the proxy.
#include "core/gdriver_proxy.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZServPtrs.h>
#include <cIGZGDriver.h>
#include <cIGZGraphicSystem2.h>

#include <atomic>
#include <cstdint>

namespace opensc4::gdriver_proxy {
namespace {

// cIGZGDriver methods in declaration order (gzcom-dll header; the game's
// DirectX 7 driver vtable at 0x00AC4660 has the same 83 slots). Overloads get
// distinct tags for counting. X(ret, name, tag, params, args, qualifiers)
#define OPENSC4_GDRIVER_METHODS(X)                                                                           \
    X(void, DrawArrays, DrawArrays, (uint32_t prim, int32_t first, int32_t count), (prim, first, count), )   \
    X(void, DrawElements, DrawElements, (uint32_t prim, int32_t count, uint32_t type, void const* indices),  \
      (prim, count, type, indices), )                                                                        \
    X(void, InterleavedArrays, InterleavedArrays, (uint32_t format, int32_t stride, void const* data),       \
      (format, stride, data), )                                                                              \
    X(uint32_t, MakeVertexFormat, MakeVertexFormatElements, (uint32_t count, intptr_t elements),             \
      (count, elements), )                                                                                   \
    X(uint32_t, MakeVertexFormat, MakeVertexFormat, (uint32_t format), (format), )                           \
    X(uint32_t, VertexFormatStride, VertexFormatStride, (uint32_t format), (format), )                       \
    X(uint32_t, VertexFormatElementOffset, VertexFormatElementOffset,                                        \
      (uint32_t format, uint32_t element, uint32_t index), (format, element, index), )                       \
    X(uint32_t, VertexFormatNumElements, VertexFormatNumElements, (uint32_t format, uint32_t element),       \
      (format, element), )                                                                                   \
    X(void, Clear, Clear, (uint32_t mask), (mask), )                                                         \
    X(void, ClearColor, ClearColor, (float r, float g, float b, float a), (r, g, b, a), )                    \
    X(void, ClearDepth, ClearDepth, (double depth), (depth), )                                               \
    X(void, ClearStencil, ClearStencil, (int32_t s), (s), )                                                  \
    X(void, ColorMask, ColorMask, (bool on), (on), )                                                         \
    X(void, DepthFunc, DepthFunc, (uint32_t func), (func), )                                                 \
    X(void, DepthMask, DepthMask, (bool on), (on), )                                                         \
    X(void, StencilFunc, StencilFunc, (uint32_t func, int32_t ref, uint32_t mask), (func, ref, mask), )      \
    X(void, StencilMask, StencilMask, (uint32_t mask), (mask), )                                             \
    X(void, StencilOp, StencilOp, (uint32_t fail, uint32_t zfail, uint32_t zpass), (fail, zfail, zpass), )   \
    X(void, BlendFunc, BlendFunc, (uint32_t src, uint32_t dst), (src, dst), )                                \
    X(void, AlphaFunc, AlphaFunc, (uint32_t func, float ref), (func, ref), )                                 \
    X(void, ShadeModel, ShadeModel, (uint32_t mode), (mode), )                                               \
    X(void, BindTexture, BindTexture, (uint32_t target, uint32_t texture), (target, texture), )              \
    X(void, TexImage2D, TexImage2D,                                                                          \
      (uint32_t target, int32_t level, int32_t internalFormat, int32_t width, int32_t height, int32_t border, \
       uint32_t format, uint32_t type, void const* pixels),                                                  \
      (target, level, internalFormat, width, height, border, format, type, pixels), )                        \
    X(void, PixelStore, PixelStore, (uint32_t param, int32_t value), (param, value), )                       \
    X(void, TexEnv, TexEnvInt, (uint32_t target, uint32_t param, int32_t value), (target, param, value), )   \
    X(void, TexEnv, TexEnvFloat, (uint32_t target, uint32_t param, float const* values),                     \
      (target, param, values), )                                                                             \
    X(void, TexParameter, TexParameter, (uint32_t target, uint32_t param, int32_t value),                    \
      (target, param, value), )                                                                              \
    X(void, Fog, FogInt, (uint32_t param, uint32_t value), (param, value), )                                 \
    X(void, Fog, FogFloat, (uint32_t param, float const* values), (param, values), )                         \
    X(void, ColorMultiplier, ColorMultiplier, (float r, float g, float b), (r, g, b), )                      \
    X(void, AlphaMultiplier, AlphaMultiplier, (float a), (a), )                                              \
    X(void, EnableVertexColors, EnableVertexColors, (bool diffuse, bool specular), (diffuse, specular), )    \
    X(void, GenTextures, GenTextures, (int32_t n, uint32_t* textures), (n, textures), )                      \
    X(void, DeleteTextures, DeleteTextures, (int32_t n, uint32_t const* textures), (n, textures), )          \
    X(bool, IsTexture, IsTexture, (uint32_t texture), (texture), )                                           \
    X(void, PrioritizeTextures, PrioritizeTextures,                                                          \
      (int32_t n, uint32_t const* textures, float const* priorities), (n, textures, priorities), )           \
    X(bool, AreTexturesResident, AreTexturesResident,                                                        \
      (int32_t n, uint32_t const* textures, bool* resident), (n, textures, resident), )                      \
    X(void, MatrixMode, MatrixMode, (uint32_t target), (target), )                                           \
    X(void, LoadMatrix, LoadMatrix, (float const* m), (m), )                                                 \
    X(void, LoadIdentity, LoadIdentity, (), (), )                                                            \
    X(void, Flush, Flush, (), (), )                                                                          \
    X(void, Enable, Enable, (uint32_t state), (state), )                                                     \
    X(void, Disable, Disable, (uint32_t state), (state), )                                                   \
    X(bool, IsEnabled, IsEnabled, (uint32_t state), (state), )                                               \
    X(void, GetBoolean, GetBoolean, (uint32_t param, bool* value), (param, value), )                         \
    X(void, GetInteger, GetInteger, (uint32_t param, int32_t* value), (param, value), )                      \
    X(void, GetFloat, GetFloat, (uint32_t param, float* value), (param, value), )                            \
    X(uint32_t, GetError, GetError, (), (), )                                                                \
    X(void, TexStage, TexStage, (uint32_t stage), (stage), )                                                 \
    X(void, TexStageCoord, TexStageCoord, (uint32_t source), (source), )                                     \
    X(void, TexStageMatrix, TexStageMatrix, (float const* m, uint32_t a, uint32_t b, uint32_t flags),        \
      (m, a, b, flags), )                                                                                    \
    X(void, TexStageCombine, TexStageCombineMode,                                                            \
      (eGDTextureStageCombineParamType type, eGDTextureStageCombineModeParam param), (type, param), )        \
    X(void, TexStageCombine, TexStageCombineSource,                                                          \
      (eGDTextureStageCombineSourceParamType type, eGDTextureStageCombineSourceParam param), (type, param), )\
    X(void, TexStageCombine, TexStageCombineOperand, (eGDTextureStageCombineOperandType type, eGDBlend blend),\
      (type, blend), )                                                                                       \
    X(void, TexStageCombine, TexStageCombineScale,                                                           \
      (eGDTextureStageCombineScaleParamType type, eGDTextureStageCombineScaleParam param), (type, param), )  \
    X(void, SetTexture, SetTexture, (uint32_t texture, uint32_t stage), (texture, stage), )                  \
    X(intptr_t, GetTexture, GetTexture, (uint32_t stage), (stage), )                                         \
    X(intptr_t, CreateTexture, CreateTexture,                                                                \
      (uint32_t format, uint32_t width, uint32_t height, uint32_t levels, uint32_t hints),                   \
      (format, width, height, levels, hints), )                                                              \
    X(void, LoadTextureLevel, LoadTextureLevel,                                                              \
      (uint32_t texture, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, \
       uint32_t type, uint32_t pitch, void const* pixels),                                                   \
      (texture, level, x, y, width, height, format, type, pitch, pixels), )                                  \
    X(void, SetCombiner, SetCombiner, (cGDCombiner const& combiner, uint32_t stage), (combiner, stage), )    \
    X(uint32_t, CountVideoModes, CountVideoModes, (), (), const)                                             \
    X(void, GetVideoModeInfo, GetVideoModeInfoIndex, (uint32_t index, sGDMode & mode), (index, mode), )      \
    X(void, GetVideoModeInfo, GetVideoModeInfoCurrent, (sGDMode & mode), (mode), )                           \
    X(void, SetVideoMode, SetVideoMode, (int32_t mode, void* window, bool fullScreen, bool b),               \
      (mode, window, fullScreen, b), )                                                                       \
    X(void, PolygonOffset, PolygonOffset, (int32_t offset), (offset), )                                      \
    X(void, BitBlt, BitBlt,                                                                                  \
      (int32_t x, int32_t y, int32_t w, int32_t h, uint32_t format, uint32_t type, void const* pixels,       \
       bool flag, void const* extra),                                                                        \
      (x, y, w, h, format, type, pixels, flag, extra), )                                                     \
    X(void, StretchBlt, StretchBlt,                                                                          \
      (int32_t x, int32_t y, int32_t w, int32_t h, int32_t sw, int32_t sh, uint32_t format, uint32_t type,   \
       void const* pixels, bool flag, void const* extra),                                                    \
      (x, y, w, h, sw, sh, format, type, pixels, flag, extra), )                                             \
    X(void, BitBltAlpha, BitBltAlpha,                                                                        \
      (int32_t x, int32_t y, int32_t w, int32_t h, uint32_t format, uint32_t type, void const* pixels,       \
       bool flag, void const* extra, uint32_t alpha),                                                        \
      (x, y, w, h, format, type, pixels, flag, extra, alpha), )                                              \
    X(void, StretchBltAlpha, StretchBltAlpha,                                                                \
      (int32_t x, int32_t y, int32_t w, int32_t h, int32_t sw, int32_t sh, uint32_t format, uint32_t type,   \
       void const* pixels, bool flag, void const* extra, uint32_t alpha),                                    \
      (x, y, w, h, sw, sh, format, type, pixels, flag, extra, alpha), )                                      \
    X(void, BitBltAlphaModulate, BitBltAlphaModulate,                                                        \
      (int32_t x, int32_t y, int32_t w, uint32_t format, uint32_t type, void const* pixels, bool flag,       \
       void const* extra, uint32_t alpha),                                                                   \
      (x, y, w, format, type, pixels, flag, extra, alpha), )                                                 \
    X(void, StretchBltAlphaModulate, StretchBltAlphaModulate,                                                \
      (int32_t x, int32_t y, int32_t w, int32_t h, int32_t sw, int32_t sh, uint32_t format, uint32_t type,   \
       void const* pixels, bool flag, void const* extra, uint32_t alpha),                                    \
      (x, y, w, h, sw, sh, format, type, pixels, flag, extra, alpha), )                                      \
    X(void, SetViewport, SetViewportFull, (), (), )                                                          \
    X(void, SetViewport, SetViewportRect, (int32_t x, int32_t y, int32_t width, int32_t height),             \
      (x, y, width, height), )                                                                               \
    X(void, GetViewport, GetViewport, (int32_t dimensions[4]), (dimensions), )                               \
    X(char const*, GetDriverInfo, GetDriverInfo, (), (), const)                                              \
    X(uint32_t, GetGZCLSID, GetGZCLSID, (), (), const)                                                       \
    X(bool, Init, Init, (), (), )                                                                            \
    X(bool, Shutdown, Shutdown, (), (), )                                                                    \
    X(bool, IsDeviceReady, IsDeviceReady, (), (), )                                                          \
    X(bool, Punt, Punt, (uint32_t code, void* data), (code, data), )

enum class Method : int {
#define OPENSC4_ENUM(ret, name, tag, params, args, qualifiers) tag,
    OPENSC4_GDRIVER_METHODS(OPENSC4_ENUM)
#undef OPENSC4_ENUM
    Count
};
static_assert(static_cast<int>(Method::Count) == 80, "cIGZGDriver has 80 methods after cIGZUnknown in build 641");

constexpr const char* kMethodNames[] = {
#define OPENSC4_NAME(ret, name, tag, params, args, qualifiers) #tag,
    OPENSC4_GDRIVER_METHODS(OPENSC4_NAME)
#undef OPENSC4_NAME
};

std::atomic<uint32_t> g_calls[static_cast<int>(Method::Count)];
bool g_installed = false;

void CountCall(Method m)
{
    if (g_calls[static_cast<int>(m)].fetch_add(1, std::memory_order_relaxed) == 0)
        log::Write("gdriver proxy: first call to %s", kMethodNames[static_cast<int>(m)]);
}

// QueryInterface calls for other interfaces, by IID: logged once, counted.
struct ForwardedQuery {
    std::atomic<uint32_t> iid{0};
    std::atomic<uint32_t> calls{0};
};
ForwardedQuery g_queries[16];

void CountForwardedQuery(uint32_t iid, bool found)
{
    for (ForwardedQuery& q : g_queries) {
        uint32_t expected = 0;
        if (q.iid.load() == iid || q.iid.compare_exchange_strong(expected, iid) || expected == iid) {
            if (q.calls.fetch_add(1) == 0)
                log::Write("gdriver proxy: QueryInterface(0x%08X) forwarded to the original: %s", iid,
                           found ? "found" : "not supported");
            return;
        }
    }
}

class GDriverProxy final : public cIGZGDriver {
public:
    // Takes over one reference to `inner`.
    explicit GDriverProxy(cIGZGDriver* inner) : inner_(inner) {}

    bool QueryInterface(uint32_t iid, void** object) override
    {
        if (iid == GZIID_cIGZGDriver || iid == GZIID_cIGZUnknown) {
            *object = static_cast<cIGZGDriver*>(this);
            AddRef();
            return true;
        }
        const bool found = inner_->QueryInterface(iid, object);
        CountForwardedQuery(iid, found);
        return found;
    }

    uint32_t AddRef() override { return ++refs_; }

    uint32_t Release() override
    {
        const uint32_t refs = --refs_;
        if (refs == 0) {
            LogCallCounts("driver released");
            inner_->Release();
            delete this;
        }
        return refs;
    }

#define OPENSC4_FORWARD(ret, name, tag, params, args, qualifiers) \
    ret name params qualifiers override                           \
    {                                                             \
        CountCall(Method::tag);                                   \
        return inner_->name args;                                 \
    }
    OPENSC4_GDRIVER_METHODS(OPENSC4_FORWARD)
#undef OPENSC4_FORWARD

private:
    cIGZGDriver* inner_;
    std::atomic<uint32_t> refs_{0};
};

bool Install(registry::Mode)
{
    cIGZGraphicSystem2Ptr graphics;
    cIGZGraphicSystem2* system = graphics;
    cIGZGDriver* current = system ? system->GetGDriver() : nullptr;
    if (!current) {
        log::Write("gdriver proxy: no current graphics driver at PreAppInit");
        return false;
    }
    current->AddRef(); // released by the proxy
    auto* proxy = new GDriverProxy(current);
    proxy->AddRef(); // ours: the graphics system's reference counting of SetGDriver is unknown
    system->SetGDriver(proxy);
    if (system->GetGDriver() != proxy) {
        log::Write("gdriver proxy: SetGDriver did not take the proxy");
        return false;
    }
    g_installed = true;
    log::Write("gdriver proxy: installed around driver %p (class 0x%08X, %u drivers)", static_cast<void*>(current),
               current->GetGZCLSID(), system->GetGDriverCount());
    return true;
}

const bool g_registered = registry::Register({
    "renderer.passthrough",
    "renderer",
    registry::Kind::Api,
    registry::Phase::PreAppInit,
    0,
    "Pass-through cIGZGDriver around the game's current driver (P1 step 1)",
    false,
    Install,
});

} // namespace

void LogCallCounts(const char* when)
{
    if (!g_installed)
        return;
    log::Write("gdriver proxy: call counts (%s):", when);
    for (int i = 0; i < static_cast<int>(Method::Count); ++i) {
        const uint32_t n = g_calls[i].load(std::memory_order_relaxed);
        if (n)
            log::Write("  %-28s %u", kMethodNames[i], n);
    }
    for (const ForwardedQuery& q : g_queries)
        if (q.iid.load())
            log::Write("  QueryInterface(0x%08X)    %u (forwarded)", q.iid.load(), q.calls.load());
}

} // namespace opensc4::gdriver_proxy
