// Diagnostic: what the stock DirectX driver actually sets on its Direct3D 7
// device. At LateInit it logs the driver's GetDriverInfo and mode records, then
// detours ddraw.dll's draw methods, found through the vtable of the
// IDirect3DDevice7 the driver keeps at cIGZGDriver + 0x218 (patching that
// vtable instead saw only 2 of millions of draws). For each distinct state on a
// draw that generates texture coordinates (projected shadows) or alpha-tests,
// it logs the render states, texture stage states, material and texture surface
// format once, and a draw count per state at shutdown.
// Replacement id: diagnostics.d3d7_probe. Use with the stock driver only.
#include "core/d3d7_probe.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZServPtrs.h>
#include <cIGZGDriver.h>
#include <cIGZGraphicSystem2.h>
#include <sGDMode.h>

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace opensc4::d3d7_probe {
namespace {

using Device = void*;
using GetState = HRESULT(__stdcall*)(Device, uint32_t, uint32_t*);
using GetStage = HRESULT(__stdcall*)(Device, uint32_t, uint32_t, uint32_t*);
using GetMat = HRESULT(__stdcall*)(Device, float*);
using GetTex = HRESULT(__stdcall*)(Device, uint32_t, void**);

// IDirect3DDevice7 vtable offsets.
constexpr int kGetMaterial = 0x44 / 4, kGetRenderState = 0x54 / 4, kDrawPrimitive = 0x64 / 4,
              kDrawIndexedPrimitive = 0x68 / 4, kDrawPrimitiveStrided = 0x74 / 4,
              kDrawIndexedPrimitiveStrided = 0x78 / 4, kDrawPrimitiveVB = 0x7C / 4, kDrawIndexedPrimitiveVB = 0x80 / 4,
              kGetTexture = 0x88 / 4, kGetTextureStageState = 0x90 / 4;

void** g_vtable = nullptr;
void* g_original[64] = {};
std::map<std::string, uint32_t> g_seen;
uint32_t g_draws = 0;

void Append(std::string& s, const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    s += buffer;
}

uint32_t RenderState(Device d, uint32_t state)
{
    uint32_t value = 0xDEADBEEF;
    reinterpret_cast<GetState>((*reinterpret_cast<void***>(d))[kGetRenderState])(d, state, &value);
    return value;
}

uint32_t StageState(Device d, uint32_t stage, uint32_t state)
{
    uint32_t value = 0xDEADBEEF;
    reinterpret_cast<GetStage>((*reinterpret_cast<void***>(d))[kGetTextureStageState])(d, stage, state, &value);
    return value;
}

void Inspect(Device d, const char* call, uint32_t fvf)
{
    ++g_draws;
    void** vtable = *reinterpret_cast<void***>(d);
    // Interesting: any stage generating coordinates, or alpha test on.
    bool generated = false;
    for (uint32_t s = 0; s < 3; ++s)
        generated = generated || (StageState(d, s, 11) & 0xFFFF0000) != 0; // D3DTSS_TEXCOORDINDEX
    const uint32_t alphaTest = RenderState(d, 15);
    if (!generated && !alphaTest)
        return;

    std::string key;
    Append(key, "%s fvf %X |", call, fvf);
    // ZENABLE, ZWRITE, ZFUNC, ZBIAS, ALPHATEST, ALPHAREF, ALPHAFUNC, BLEND, SRC, DEST, FOG, LIGHTING,
    // AMBIENT, COLORVERTEX, DIFFUSE/AMBIENT MATERIALSOURCE, TEXTUREFACTOR, CULL, SHADE, DITHER
    static const struct { uint32_t id; const char* name; } kStates[] = {
        {7, "z"}, {14, "zw"}, {23, "zf"}, {47, "zbias"}, {15, "at"}, {24, "aref"}, {25, "af"}, {27, "blend"},
        {19, "src"}, {20, "dst"}, {28, "fog"}, {137, "light"}, {139, "amb"}, {141, "cv"}, {145, "dms"},
        {147, "ams"}, {60, "tf"}, {22, "cull"}, {9, "shade"}, {26, "dither"}, {16, "lastpix"}};
    for (const auto& s : kStates)
        Append(key, " %s=%X", s.name, RenderState(d, s.id));
    for (uint32_t stage = 0; stage < 3; ++stage) {
        const uint32_t colorOp = StageState(d, stage, 1);
        Append(key, " | s%u cop=%u", stage, colorOp);
        if (colorOp == 1) // D3DTOP_DISABLE
            break;
        // COLORARG1/2, ALPHAOP, ALPHAARG1/2, TEXCOORDINDEX, ADDRESSU/V, MAG/MIN/MIPFILTER, TRANSFORMFLAGS
        static const struct { uint32_t id; const char* name; } kStage[] = {
            {2, "ca1"}, {3, "ca2"}, {4, "aop"}, {5, "aa1"}, {6, "aa2"}, {11, "tci"}, {13, "u"}, {14, "v"},
            {16, "mag"}, {17, "min"}, {18, "mip"}, {24, "ttf"}, {19, "lodbias"}, {20, "maxmip"}};
        for (const auto& s : kStage)
            Append(key, " %s=%X", s.name, StageState(d, stage, s.id));
        void* surface = nullptr;
        reinterpret_cast<GetTex>(vtable[kGetTexture])(d, stage, &surface);
        if (surface) {
            uint8_t desc[124] = {};
            *reinterpret_cast<uint32_t*>(desc) = sizeof(desc);
            void** sv = *reinterpret_cast<void***>(surface);
            using GetDesc = HRESULT(__stdcall*)(void*, void*);
            reinterpret_cast<GetDesc>(sv[0x58 / 4])(surface, desc);
            auto u = [&](int off) { return *reinterpret_cast<uint32_t*>(desc + off); };
            Append(key, " tex %ux%u mips %u pf flags %X fourcc %.4s bits %u masks %X %X %X %X", u(0xC), u(8),
                   u(0x18), u(0x4C), reinterpret_cast<const char*>(desc + 0x50), u(0x54), u(0x58), u(0x5C),
                   u(0x60), u(0x64));
            using Release = ULONG(__stdcall*)(void*);
            reinterpret_cast<Release>(sv[2])(surface);
        }
    }
    float material[17] = {};
    reinterpret_cast<GetMat>(vtable[kGetMaterial])(d, material);
    Append(key, " | mat diff %.2f %.2f %.2f %.2f amb %.2f %.2f %.2f %.2f", material[0], material[1], material[2],
           material[3], material[4], material[5], material[6], material[7]);

    auto it = g_seen.find(key);
    if (it != g_seen.end()) {
        ++it->second;
        return;
    }
    if (g_seen.size() >= 64)
        return;
    g_seen.emplace(key, 1);
    log::Write("d3d7 probe: #%u %s", static_cast<unsigned>(g_seen.size()), key.c_str());
}

HRESULT __stdcall DrawPrimitive(Device d, uint32_t type, uint32_t fvf, void* v, uint32_t n, uint32_t flags)
{
    Inspect(d, "DP", fvf);
    using F = HRESULT(__stdcall*)(Device, uint32_t, uint32_t, void*, uint32_t, uint32_t);
    return reinterpret_cast<F>(g_original[kDrawPrimitive])(d, type, fvf, v, n, flags);
}

HRESULT __stdcall DrawIndexedPrimitive(Device d, uint32_t type, uint32_t fvf, void* v, uint32_t n, uint16_t* i,
                                       uint32_t ni, uint32_t flags)
{
    Inspect(d, "DIP", fvf);
    using F = HRESULT(__stdcall*)(Device, uint32_t, uint32_t, void*, uint32_t, uint16_t*, uint32_t, uint32_t);
    return reinterpret_cast<F>(g_original[kDrawIndexedPrimitive])(d, type, fvf, v, n, i, ni, flags);
}

HRESULT __stdcall DrawPrimitiveStrided(Device d, uint32_t type, uint32_t fvf, void* v, uint32_t n, uint32_t flags)
{
    Inspect(d, "DPS", fvf);
    using F = HRESULT(__stdcall*)(Device, uint32_t, uint32_t, void*, uint32_t, uint32_t);
    return reinterpret_cast<F>(g_original[kDrawPrimitiveStrided])(d, type, fvf, v, n, flags);
}

HRESULT __stdcall DrawIndexedPrimitiveStrided(Device d, uint32_t type, uint32_t fvf, void* v, uint32_t n, uint16_t* i,
                                              uint32_t ni, uint32_t flags)
{
    Inspect(d, "DIPS", fvf);
    using F = HRESULT(__stdcall*)(Device, uint32_t, uint32_t, void*, uint32_t, uint16_t*, uint32_t, uint32_t);
    return reinterpret_cast<F>(g_original[kDrawIndexedPrimitiveStrided])(d, type, fvf, v, n, i, ni, flags);
}

uint32_t BufferFvf(void* vb)
{
    uint32_t desc[4] = {sizeof(desc), 0, 0, 0}; // D3DVERTEXBUFFERDESC: size, caps, fvf, count
    void** vv = *reinterpret_cast<void***>(vb);
    using GetDesc = HRESULT(__stdcall*)(void*, void*);
    reinterpret_cast<GetDesc>(vv[0x18 / 4])(vb, desc);
    return desc[2];
}

HRESULT __stdcall DrawPrimitiveVB(Device d, uint32_t type, void* vb, uint32_t start, uint32_t n, uint32_t flags)
{
    Inspect(d, "DPVB", BufferFvf(vb));
    using F = HRESULT(__stdcall*)(Device, uint32_t, void*, uint32_t, uint32_t, uint32_t);
    return reinterpret_cast<F>(g_original[kDrawPrimitiveVB])(d, type, vb, start, n, flags);
}

HRESULT __stdcall DrawIndexedPrimitiveVB(Device d, uint32_t type, void* vb, uint32_t start, uint32_t n, uint16_t* i,
                                         uint32_t ni, uint32_t flags)
{
    Inspect(d, "DIPVB", BufferFvf(vb));
    using F = HRESULT(__stdcall*)(Device, uint32_t, void*, uint32_t, uint32_t, uint16_t*, uint32_t, uint32_t);
    return reinterpret_cast<F>(g_original[kDrawIndexedPrimitiveVB])(d, type, vb, start, n, i, ni, flags);
}

void Patch(int slot, void* detour)
{
    // Expect whatever is there now: this is a diagnostic on a system DLL.
    const auto target = reinterpret_cast<std::uintptr_t>(g_vtable[slot]);
    unsigned char expect[5];
    std::memcpy(expect, g_vtable[slot], sizeof(expect));
    char name[32];
    std::snprintf(name, sizeof(name), "IDirect3DDevice7 slot 0x%X", slot * 4);
    hook::InstallAt(name, target, expect, sizeof(expect), detour, &g_original[slot]);
}

bool Install(registry::Mode)
{
    cIGZGraphicSystem2Ptr graphics;
    cIGZGraphicSystem2* system = graphics;
    cIGZGDriver* driver = system ? system->GetGDriver() : nullptr;
    if (!driver || driver->GetGZCLSID() != 0xBADB6906) {
        log::Write("d3d7 probe: the current driver is not the stock DirectX driver");
        return false;
    }
    if (const char* info = driver->GetDriverInfo())
        log::Write("d3d7 probe: GetDriverInfo:\n%s", info);
    {
        sGDMode mode{};
        driver->GetVideoModeInfo(mode);
        const auto* w = reinterpret_cast<const uint32_t*>(&mode);
        log::Write("d3d7 probe: current mode %08X %08X %08X %08X %08X %08X %08X %08X | %08X %08X %08X %08X %08X", w[0],
                   w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12]);
        std::string list;
        char item[48];
        for (uint32_t i = 0; i < driver->CountVideoModes(); ++i) {
            driver->GetVideoModeInfo(i, mode);
            std::snprintf(item, sizeof(item), " %u:%ux%ux%u%s", i, w[1], w[2], w[3], (w[8] & 0xFF) ? "f" : "w");
            list += item;
        }
        log::Write("d3d7 probe: %u modes:%s", driver->CountVideoModes(), list.c_str());
    }
    auto* object = reinterpret_cast<uint8_t*>(driver); // the driver's methods use offsets from this pointer
    Device device = *reinterpret_cast<Device*>(object + 0x218);
    if (!device) {
        log::Write("d3d7 probe: no Direct3D device yet");
        return false;
    }
    g_vtable = *reinterpret_cast<void***>(device);
    Patch(kDrawPrimitive, reinterpret_cast<void*>(DrawPrimitive));
    Patch(kDrawIndexedPrimitive, reinterpret_cast<void*>(DrawIndexedPrimitive));
    Patch(kDrawPrimitiveStrided, reinterpret_cast<void*>(DrawPrimitiveStrided));
    Patch(kDrawIndexedPrimitiveStrided, reinterpret_cast<void*>(DrawIndexedPrimitiveStrided));
    Patch(kDrawPrimitiveVB, reinterpret_cast<void*>(DrawPrimitiveVB));
    Patch(kDrawIndexedPrimitiveVB, reinterpret_cast<void*>(DrawIndexedPrimitiveVB));
    log::Write("d3d7 probe: hooked the draw methods of device %p (vtable %p)", device, static_cast<void*>(g_vtable));
    return true;
}

const bool g_registered = registry::Register({
    "diagnostics.d3d7_probe",
    "diagnostics",
    registry::Kind::Api,
    registry::Phase::LateInit,
    0,
    "Logs the stock DirectX driver's Direct3D 7 state on shadow and alpha-tested draws",
    false,
    Install,
});

} // namespace

void LogCounts()
{
    if (g_draws)
        log::Write("d3d7 probe: %u draws in all", g_draws);
    for (const auto& [key, count] : g_seen)
        log::Write("d3d7 probe: %u draws: %.120s", count, key.c_str());
}

} // namespace opensc4::d3d7_probe
