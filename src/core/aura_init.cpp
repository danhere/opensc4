// Our cSC4AuraSimulator (build 641), second part: the tuning, Init,
// Shutdown and the save (Write, Read), each a replacement on the original
// object (docs/aura.md).
#include "core/address_check.h"
#include "core/aura_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::aura_sim {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void*(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

void* Service(std::uintptr_t global) { return Global<void*>(global); }

// A curve field (SC4Curve*, 16 bytes: points begin, end, capacity, count)
// freed and cleared.
void DropCurve(void* self, std::size_t offset)
{
    void*& curve = At<void*>(self, offset);
    if (!curve)
        return;
    if (void* const points = At<void*>(curve, 0))
        Free(points);
    OperatorDelete(curve);
    curve = nullptr;
}

// 0x0068D700 (EDI the field, EDX the property): the curve from the
// property, made on first use; without the property, dropped.
void LoadCurve(void* holder, uint32_t property, void*& curve)
{
    using LoadFn = uint8_t(__thiscall*)(void* curve, void* property);
    void* const value = Virtual<void*>(holder, 0x24, property);
    if (!value) {
        if (curve) {
            if (void* const points = At<void*>(curve, 0))
                Free(points);
            OperatorDelete(curve);
            curve = nullptr;
        }
        return;
    }
    if (!curve) {
        void* const memory = OperatorNew(0x10);
        curve = memory ? reinterpret_cast<ThisFn>(addr::SC4Curve_Construct)(memory) : nullptr;
    }
    reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromProperty)(curve, value);
}

// --- The tuning (0x0068D770) ----------------------------------------------------

struct ResourceKey {
    uint32_t type, group, instance;
};

void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
    using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
    using GetVectorFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* vector);
    using LoadExemplarFn = uint8_t(__thiscall*)(void* curve, const ResourceKey* key, uint32_t property);
    ResourceKey key{0x6534284A, 0xE7E2C2DB, 0xAA023079};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Service(addr::SC4_ResourceManager), 0xC, &key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        if (exemplar)
            Virtual<uint32_t>(exemplar, 0x8);
        return;
    }
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    if (!holder) {
        if (exemplar)
            Virtual<uint32_t>(exemplar, 0x8);
        return;
    }
    LoadCurve(holder, 0xEA023270, At<void*>(self, field::kParkCurve));
    LoadCurve(holder, 0xEA023275, At<void*>(self, field::kCoverageCurve1));
    LoadCurve(holder, 0xEA023276, At<void*>(self, field::kCoverageCurve2));
    LoadCurve(holder, 0xEA023277, At<void*>(self, field::kService43D70Curve));
    LoadCurve(holder, 0xEA023278, At<void*>(self, field::kService43DA8Curve1));
    LoadCurve(holder, 0xEA023279, At<void*>(self, field::kService43DA8Curve2));
    LoadCurve(holder, 0xEA02327F, At<void*>(self, field::kFundingCurve));
    LoadCurve(holder, 0xEA023280, At<void*>(self, field::kHeightCurve));
    for (uint32_t i = 0; i < 4; ++i)
        LoadCurve(holder, 0xEA023271 + i, At<void**>(self, field::kPollutionCurves)[i]);
    const auto getFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
    const auto getSint32 = reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32);
    At<uint32_t>(self, field::kSmoothing) = 0x3F19999A; // 0.6f
    At<uint32_t>(self, field::kTransientDecay) = 0x3ECCCCCD; // 0.4f
    getFloat(holder, 0xEA02327A, &At<float>(self, field::kSmoothing));
    getFloat(holder, 0xEA02327B, &At<float>(self, field::kTransientDecay));
    getSint32(holder, 0xEA02327C, &At<int32_t>(self, field::kTuningBase));
    getSint32(holder, 0xEA02327D, &At<int32_t>(self, field::kTuningBase + 4));
    getSint32(holder, 0xEA02327E, &At<int32_t>(self, field::kTuningBase + 8));
    for (EffectEntry* entry = EffectTable(); entry->id; ++entry)
        if (entry->tuningIndex >= 0 && entry->tuningIndex <= 2)
            entry->base = At<int32_t>(self, field::kTuningBase + 4 * entry->tuningIndex);
    for (uint32_t i = 0; i < 4; ++i) {
        At<uint32_t>(self, field::kTransientValue + 4 * i) = 0;
        At<uint32_t>(self, field::kTransientRadius + 4 * i) = 0;
        float* vector[3] = {};
        if (reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyFloatVector)(holder, 0xEA023290 + i, vector)) {
            At<float>(self, field::kTransientValue + 4 * i) = vector[0][0];
            At<float>(self, field::kTransientRadius + 4 * i) = vector[0][1];
        }
        if (vector[0])
            Free(vector[0]);
    }
    At<int32_t>(self, field::kClusterDistance) = 8;
    At<int32_t>(self, field::kLowThreshold) = 0xC4;
    At<int32_t>(self, field::kClusterMinimum) = 0x10;
    uint32_t* limits[3] = {};
    reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyUint32Vector)(holder, 0xEA0232A0, limits);
    if (((reinterpret_cast<char*>(limits[1]) - reinterpret_cast<char*>(limits[0])) & ~3) == 0xC) {
        At<uint32_t>(self, field::kLowThreshold) = limits[0][0];
        At<uint32_t>(self, field::kClusterDistance) = limits[0][1];
        At<uint32_t>(self, field::kClusterMinimum) = limits[0][2];
    }
    DropCurve(self, field::kRiotCurve);
    if (Virtual<uint8_t>(holder, 0xC, 0xEA0232A1u)) {
        void* const memory = OperatorNew(0x10);
        void* const curve = memory ? reinterpret_cast<ThisFn>(addr::SC4Curve_Construct)(memory) : nullptr;
        At<void*>(self, field::kRiotCurve) = curve;
        reinterpret_cast<LoadExemplarFn>(addr::SC4Curve_LoadFromExemplar)(curve, &key, 0xEA0232A1u);
    }
    if (limits[0])
        Free(limits[0]);
    if (exemplar)
        Virtual<uint32_t>(exemplar, 0x8);
}

using LifecycleFn = uint8_t(__fastcall*)(void*, void*);
using VoidFn = void(__fastcall*)(void*, void*);
VoidFn g_originalLoadTuning = nullptr;

bool InstallTuning(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4AuraSimulator_LoadTuning);
    return game::IsBuild641() && entry && hook::Install(*entry, &LoadTuning, &g_originalLoadTuning);
}

const bool g_registeredTuning = registry::Register({
    "simulators.aura.tuning",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_LoadTuning,
    "Our aura tuning load (docs/aura.md)",
    false,
    InstallTuning,
});

// --- Init and Shutdown ----------------------------------------------------------

// A grid as Init constructs it (inline in the game: SC4::operator_new(0x44)
// and the fields of cSC4SimGridSint8 or Sint16); SetUpGrid then calls its
// slots 1, 3, 9 (size), 6 (id) and 27 (fill) when given.
void* NewGrid(bool wide)
{
    auto* const grid = static_cast<uint32_t*>(OperatorNew(0x44));
    if (grid) {
        grid[1] = 0x00A80784;
        grid[0] = static_cast<uint32_t>(wide ? addr::SC4SimGridSint16_Vtable : addr::SC4SimGridSint8_Vtable);
        grid[1] = wide ? 0x00A879C4 : 0x00A87934;
        reinterpret_cast<uint8_t*>(grid)[8] = 0;
        grid[3] = 0;
        grid[4] = wide ? 0x49B9E605 : 0x49B9E603;
        grid[6] = 0;
        grid[14] = 0;
        grid[15] = 0;
        grid[16] = 0;
    }
    return grid;
}

void SetUpGrid(void* self, std::size_t offset, bool wide, uint32_t size, uint32_t id, bool fill, int32_t value)
{
    if (At<void*>(self, offset))
        return;
    void* const grid = NewGrid(wide);
    At<void*>(self, offset) = grid;
    Virtual<uint32_t>(grid, 0x4);
    Virtual<uint32_t>(At<void*>(self, offset), 0xC);
    Virtual<uint32_t>(At<void*>(self, offset), 0x24, size);
    Virtual<uint32_t>(At<void*>(self, offset), 0x18, id);
    if (fill)
        Virtual<uint32_t>(At<void*>(self, offset), 0x6C, value);
}

void Reference(void* self, std::size_t offset, void* object)
{
    if (At<void*>(self, offset))
        return;
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4);
}

constexpr uint32_t kAuraMessages[] = {0x99EF1142, 0x99EF1143, 0xC2C09554, 0xC53D10AA, 0x26D31EC1};

// The script service (0xEBE0E860, interface 0xE898ED03) into `holder`.
void GetScriptService(void** holder)
{
    using GetFrameworkFn = void*(__cdecl*)();
    *holder = nullptr;
    if (void* const framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
        Virtual<uint8_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, holder);
}

void ReleaseHolder(void** holder)
{
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4_ReleaseRefHolder)(holder);
}

uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    using FillFn = void(__thiscall*)(void* vector, uint32_t count, void** value);
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    using RegisterTableFn = uint32_t(__cdecl*)(void* self, const char* name, uint32_t, uint32_t, uint32_t, void* service);
    using RegisterFunctionFn = uint32_t(__cdecl*)(std::uintptr_t callback, const char* name, std::uintptr_t callback2,
                                                  void* self, uint32_t, void* self2, void* service);
    if (At<uint8_t>(self, field::kInitialised))
        return 1;
    if (!Global<uint8_t>(addr::AuraSimulator_Started))
        Global<uint8_t>(addr::AuraSimulator_Started) = 1;
    void* zero = nullptr;
    reinterpret_cast<FillFn>(addr::SC4PtrVector_Fill)(static_cast<char*>(self) + field::kPollutionCurves, 4, &zero);
    At<uint32_t>(self, field::kState) = 0;
    At<void*>(self, field::kLotIterator) = nullptr;
    Reference(self, field::kCity, Service(addr::SC4_City));
    Reference(self, field::kService43D68, Service(addr::SC4_Service43D68));
    const uint32_t size = At<uint32_t>(self, field::kGridSize);
    SetUpGrid(self, field::kAuraGrid, false, size, 0x69D5B600, true, 0);
    SetUpGrid(self, field::kSmoothedGrid, false, size, 0x69D5B604, true, -128);
    SetUpGrid(self, field::kTransientGrid, false, size, 0x69D5B605, true, 0);
    SetUpGrid(self, field::kEffectGrid, false, 1, 0x69D5B606, false, 0);
    SetUpGrid(self, field::kParkMap, true, size, 0x49D5B602, false, 0);
    SetUpGrid(self, field::kParkSources, true, size, 0x2A674CA5, true, 0);
    SetUpGrid(self, field::kLandmarkMap, true, size, 0x8A674CAF, true, 0);
    SetUpGrid(self, field::kDepartmentSum, true, size, 0x8A674CAF, true, 0);
    SetUpGrid(self, field::kLandmarkSources, false, size, 0x49D5B607, true, 0);
    At<uint32_t>(self, field::kState) = 1;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4AuraSimulator_LoadTuning)(self);
    void* const target = static_cast<char*>(self) + 0x4;
    if (!At<void*>(self, field::kMessageServer)) {
        Reference(self, field::kMessageServer, Service(addr::SC4_MessageServer));
        for (const uint32_t message : kAuraMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, target, message);
    }
    if (void* const simulators = Service(addr::SC4_SimulatorService)) {
        struct {
            const void* vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t zero;
        } name;
        reinterpret_cast<StringFn>(addr::SC4String_FromText)(&name, reinterpret_cast<const char*>(addr::AuraSimulator_Name));
        for (const uint32_t phase : {0u, 10u, 4u, 6u})
            Virtual<uint32_t>(simulators, 0x4C, target, phase, &name, 2u);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Free(name.begin);
    }
    void* script;
    GetScriptService(&script);
    reinterpret_cast<RegisterTableFn>(addr::SC4Script_RegisterTable)(self, reinterpret_cast<const char*>(0x00AB0830), 0,
                                                                      0, 0, script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterFunctionFn>(addr::SC4Script_RegisterFunction)(
        0x0068BCA0, reinterpret_cast<const char*>(0x00AB0818), 0x0068BCA0, self, 0, self, script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterFunctionFn>(addr::SC4Script_RegisterFunction)(
        0x0068BD50, reinterpret_cast<const char*>(0x00AB080C), 0x0068BD50, self, 0, self, script);
    ReleaseHolder(&script);
    At<uint8_t>(self, field::kInitialised) = 1;
    return 1;
}

void Drop(void* self, std::size_t offset)
{
    if (void*& object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8); // Release
        object = nullptr;
    }
}

void DropGrid(void* self, std::size_t offset)
{
    if (At<void*>(self, offset)) {
        Virtual<uint32_t>(At<void*>(self, offset), 0x10); // Shutdown
        Virtual<uint32_t>(At<void*>(self, offset), 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    using UnregisterFn = uint8_t(__cdecl*)(void* self, void* service);
    if (!At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 0;
    void* script;
    GetScriptService(&script);
    reinterpret_cast<UnregisterFn>(addr::SC4Script_UnregisterTable)(self, script);
    ReleaseHolder(&script);
    DropCurve(self, field::kRiotCurve);
    void* const target = static_cast<char*>(self) + 0x4;
    if (void* const service = Virtual<void*>(Service(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(service, 0x50, target);
    if (At<void*>(self, field::kMessageServer)) {
        for (const uint32_t message : kAuraMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, target, message);
        Drop(self, field::kMessageServer);
    }
    for (const std::size_t grid : {field::kAuraGrid, field::kSmoothedGrid, field::kEffectGrid, field::kTransientGrid,
                                   field::kParkMap, field::kParkSources, field::kLandmarkMap, field::kDepartmentSum})
        DropGrid(self, grid);
    if (At<uint32_t>(self, field::kDepartmentGridCount)) {
        void* const header = At<void*>(self, field::kDepartmentGrids);
        for (void* node = At<void*>(header, 0x8); node != header;) {
            if (void* const grid = At<void*>(node, 0x14)) {
                Virtual<uint32_t>(grid, 0x10);
                Virtual<uint32_t>(grid, 0x8);
            }
            if (void* right = At<void*>(node, 0xC)) {
                while (void* left = At<void*>(right, 0x8))
                    right = left;
                node = right;
            } else {
                void* parent = At<void*>(node, 0x4);
                while (node == At<void*>(parent, 0xC)) {
                    node = parent;
                    parent = At<void*>(parent, 0x4);
                }
                if (At<void*>(node, 0xC) != parent)
                    node = parent;
            }
        }
        if (At<uint32_t>(self, field::kDepartmentGridCount)) {
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4Uint32Map_EraseTree)(At<void*>(header, 0x4));
            At<void*>(header, 0x8) = header;
            At<void*>(header, 0x4) = nullptr;
            At<void*>(header, 0xC) = header;
            At<uint32_t>(self, field::kDepartmentGridCount) = 0;
        }
    }
    DropGrid(self, field::kLandmarkSources);
    if (void** iterator = At<void**>(self, field::kLotIterator)) {
        if (iterator[1]) {
            void* holder = iterator;
            reinterpret_cast<void(__thiscall*)(void*, void**)>(addr::SC4ListIterator_FreeList)(
                static_cast<char*>(iterator[1]) + 4, &holder);
        }
        OperatorDelete(iterator);
        At<void*>(self, field::kLotIterator) = nullptr;
    }
    Drop(self, field::kService43D68);
    Drop(self, field::kCity);
    for (uint32_t i = 0;; ++i) {
        void** const curves = At<void**>(self, field::kPollutionCurves);
        const auto count = static_cast<uint32_t>((At<char*>(self, field::kPollutionCurves + 4) -
                                                  reinterpret_cast<char*>(curves)) >> 2);
        if (i >= count)
            break;
        if (void* const curve = curves[i]) {
            if (void* const points = At<void*>(curve, 0))
                Free(points);
            OperatorDelete(curve);
        }
        At<void**>(self, field::kPollutionCurves)[i] = nullptr;
    }
    for (const std::size_t curve : {field::kParkCurve, field::kCoverageCurve1, field::kCoverageCurve2,
                                    field::kService43D70Curve, field::kService43DA8Curve1, field::kService43DA8Curve2,
                                    field::kFundingCurve, field::kHeightCurve, field::kRiotCurve})
        DropCurve(self, curve);
    return 1;
}

LifecycleFn g_originalInit = nullptr;
LifecycleFn g_originalShutdown = nullptr;

bool InstallInit(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4AuraSimulator_Init);
    return game::IsBuild641() && entry && hook::Install(*entry, &Init, &g_originalInit);
}

bool InstallShutdown(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4AuraSimulator_Shutdown);
    return game::IsBuild641() && entry && hook::Install(*entry, &Shutdown, &g_originalShutdown);
}

const bool g_registeredInit = registry::Register({
    "simulators.aura.init",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_Init,
    "Our aura Init: references, grids, tuning, messages, registrations (docs/aura.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.aura.shutdown",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_Shutdown,
    "Our aura Shutdown (docs/aura.md)",
    false,
    InstallShutdown,
});

// --- Saving: cIGZSerializable (vtable 0x00AB07A0) at +0x8 ---------------------------
//
// Version 9: seven grids (references: the aura, park map, park sources,
// smoothed, transient and landmark sources grids and the landmark map), the
// department grids (count, then key and reference each), +0x80, the rating,
// the best and worst tracts (x, z, value as uint16 each), the clusters
// (count, then x, z, count each). Version 8 had one grid instead of the
// department grids; reading it asks for the department refresh (+0xD4).

constexpr uint32_t kGridInterface = 0xC9BD939A;
constexpr std::size_t kSavedGrids[] = {field::kAuraGrid,     field::kParkMap,        field::kParkSources,
                                       field::kSmoothedGrid, field::kTransientGrid, field::kLandmarkSources,
                                       field::kLandmarkMap};

uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    using WriteReferenceFn = uint8_t(__cdecl*)(void* stream, void* object);
    const auto writeReference = reinterpret_cast<WriteReferenceFn>(addr::SC4Stream_WriteReference);
    char* const self = part - 0x8;
    Virtual<uint32_t>(stream, 0x1C, uint32_t{9});
    for (const std::size_t grid : kSavedGrids)
        writeReference(stream, At<void*>(self, grid));
    Virtual<uint32_t>(stream, 0x24, At<uint32_t>(self, field::kDepartmentGridCount));
    void* const header = At<void*>(self, field::kDepartmentGrids);
    for (void* node = At<void*>(header, 0x8); node != header;) {
        void* const grid = At<void*>(node, 0x14);
        Virtual<uint32_t>(stream, 0x24, At<uint32_t>(node, 0x10));
        writeReference(stream, grid);
        if (void* right = At<void*>(node, 0xC)) {
            while (void* left = At<void*>(right, 0x8))
                right = left;
            node = right;
        } else {
            void* parent = At<void*>(node, 0x4);
            while (node == At<void*>(parent, 0xC)) {
                node = parent;
                parent = At<void*>(parent, 0x4);
            }
            if (At<void*>(node, 0xC) != parent)
                node = parent;
        }
    }
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kPassFlag));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kMayorRating));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kBestX));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kBestZ));
    Virtual<uint32_t>(stream, 0x18, static_cast<uint32_t>(At<uint16_t>(self, field::kBestValue)));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kWorstX));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kWorstZ));
    Virtual<uint32_t>(stream, 0x18, static_cast<uint32_t>(At<uint16_t>(self, field::kWorstValue)));
    void** const head = At<void**>(self, field::kClusters);
    uint32_t count = 0;
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
        ++count;
    Virtual<uint32_t>(stream, 0x24, count);
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0])) {
        Virtual<uint32_t>(stream, 0x20, At<uint32_t>(node, 0x8));
        Virtual<uint32_t>(stream, 0x20, At<uint32_t>(node, 0xC));
        Virtual<uint32_t>(stream, 0x20, At<uint32_t>(node, 0x10));
    }
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t flags);
    using AtFn = void**(__thiscall*)(void* map, const uint32_t* key);
    using AllocateFn2 = void*(__cdecl*)(std::size_t size);
    const auto readReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
    char* const self = part - 0x8;
    uint16_t version = 0;
    Virtual<uint32_t>(stream, 0x1C, &version);
    if (version < 8)
        return 0;
    for (const std::size_t grid : kSavedGrids)
        readReference(stream, kGridInterface, &At<void*>(self, grid), 0);
    if (version < 9) {
        void* old = nullptr;
        readReference(stream, kGridInterface, &old, 0);
        Virtual<uint32_t>(old, 0x10);
        Virtual<uint32_t>(old, 0x8);
        At<uint8_t>(self, field::kStrikeFlag) = 1;
    } else {
        uint32_t count = 0;
        Virtual<uint32_t>(stream, 0x24, &count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t key = 0;
            void* grid = nullptr;
            Virtual<uint32_t>(stream, 0x24, &key);
            readReference(stream, kGridInterface, &grid, 0);
            *reinterpret_cast<AtFn>(addr::SC4Uint32Map_At)(self + field::kDepartmentGrids, &key) = grid;
        }
    }
    Virtual<uint32_t>(stream, 0x20, &At<uint32_t>(self, field::kPassFlag));
    Virtual<uint32_t>(stream, 0x20, &At<uint32_t>(self, field::kMayorRating));
    Virtual<uint32_t>(stream, 0x20, &At<uint32_t>(self, field::kBestX));
    Virtual<uint32_t>(stream, 0x20, &At<uint32_t>(self, field::kBestZ));
    Virtual<uint32_t>(stream, 0x18, &At<uint16_t>(self, field::kBestValue));
    Virtual<uint32_t>(stream, 0x20, &At<uint32_t>(self, field::kWorstX));
    Virtual<uint32_t>(stream, 0x20, &At<uint32_t>(self, field::kWorstZ));
    Virtual<uint32_t>(stream, 0x18, &At<uint16_t>(self, field::kWorstValue));
    uint32_t clusters = 0;
    Virtual<uint32_t>(stream, 0x24, &clusters);
    while (clusters) {
        --clusters;
        int32_t value[3] = {};
        Virtual<uint32_t>(stream, 0x20, &value[0]);
        Virtual<uint32_t>(stream, 0x20, &value[1]);
        Virtual<uint32_t>(stream, 0x20, &value[2]);
        void** const head = At<void**>(self, field::kClusters);
        auto* const node = static_cast<void**>(reinterpret_cast<AllocateFn2>(addr::SC4_stl_allocate)(0x14));
        At<int32_t>(node, 0x8) = value[0];
        At<int32_t>(node, 0xC) = value[1];
        At<int32_t>(node, 0x10) = value[2];
        void** const last = static_cast<void**>(head[1]);
        node[0] = head;
        node[1] = last;
        last[0] = node;
        head[1] = node;
    }
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

using SerializeFn = uint8_t(__fastcall*)(char*, void*, void*);
SerializeFn g_originalWrite = nullptr;
SerializeFn g_originalRead = nullptr;

bool InstallSave(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4AuraSimulator_Write);
    return game::IsBuild641() && entry && hook::Install(*entry, &Write, &g_originalWrite);
}

bool InstallLoad(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4AuraSimulator_Read);
    return game::IsBuild641() && entry && hook::Install(*entry, &Read, &g_originalRead);
}

const bool g_registeredSave = registry::Register({
    "simulators.aura.save",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_Write,
    "Our aura Write, version 9 (docs/aura.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.aura.load",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_Read,
    "Our aura Read, versions 8 and 9 (docs/aura.md)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::aura_sim
