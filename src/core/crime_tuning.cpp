// Our cSC4CrimeSimulator (build 641), third part: the tuning and the crime
// types (docs/crime.md, "Tuning"). Allocations go through the game's own
// helpers in the game's order (default curve points, crime type records),
// so the heap sees the same allocations.
#include "core/crime_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::crime_sim {
namespace {

struct Point {
    float x;
    float y;
};

using PointInsertFn = void(__thiscall*)(Vector* vector, Point* where, const Point* value, void* allocator,
                                        uint32_t count, uint32_t atEnd);
using SetPointsFn = uint8_t(__thiscall*)(void* curve, const Vector* points);
using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetUint8Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using GetBytesFn = uint8_t(__cdecl*)(void* holder, uint32_t property, Vector* value);
using GetStringFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* string);
using GetKeyFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* key);
using LocalizedFn = uint8_t(__cdecl*)(void* string, uint32_t group, uint32_t instance, uint32_t type, uint32_t flags);
using FilterFn = void(__thiscall*)(void* filter, uint32_t group, uint32_t type);
using FilterDestroyFn = void(__thiscall*)(void* filter);
using LoadTypeFn = void(__cdecl*)(const uint32_t* key);

const auto PointInsert = reinterpret_cast<PointInsertFn>(addr::SC4PointVector_InsertN);
const auto SetPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
const auto LoadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
const auto GetSint32 = reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32);
const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
const auto GetUint8 = reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyUint8);
const auto GetBytes = reinterpret_cast<GetBytesFn>(addr::SC4_GetPropertyUint8Vector);
const auto GetString = reinterpret_cast<GetStringFn>(addr::SC4_GetPropertyString);
const auto GetKey = reinterpret_cast<GetKeyFn>(addr::SC4_GetPropertyResourceKey);
const auto Localized = reinterpret_cast<LocalizedFn>(addr::SC4_GetLocalizedString);

constexpr uint32_t kTuningKey[3] = {0x6534284A, 0xE7E2C2DB, 0xC8E8F6C5};
constexpr uint32_t kExemplarInterface = 0xA52160F5;

// push_back of a point, as the game's inline code.
void PushPoint(Vector* vector, float x, float y)
{
    const Point point{x, y};
    if (vector->end != vector->capacity) {
        if (vector->end)
            *reinterpret_cast<Point*>(vector->end) = point;
        vector->end += sizeof(Point);
        return;
    }
    char allocator;
    PointInsert(vector, reinterpret_cast<Point*>(vector->end), &point, &allocator, 1, 1);
}

// The first point, inserted at null into an empty vector, as the game does.
void FirstPoint(Vector* vector, float x, float y)
{
    *vector = {};
    const Point point{x, y};
    char allocator;
    PointInsert(vector, nullptr, &point, &allocator, 1, 1);
}

// 0x006A07A0 (Init, message 0xC53D10AA): the defaults (+0xD8 = 2, +0x16C =
// 0.5, +0x170 = 1), then from the tuning exemplar 6534284A-E7E2C2DB-C8E8F6C5:
// the grid's cell size, curve +0xDC, the two floats, and the eight curves,
// each set to its default points first.
void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    char* const base = static_cast<char*>(self);
    At<int32_t>(self, field::kGridCellSize) = 2;
    At<float>(self, field::kDecay) = 0.5f;
    At<float>(self, field::kUnjailedFactor) = 1.0f;
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, kTuningKey, kExemplarInterface, &exemplar, 0u,
                          0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    GetSint32(holder, 0x47E2C200, &At<int32_t>(self, field::kGridCellSize));
    LoadCurve(base + field::kCurveDC, kTuningKey, 0x49A34301);
    GetFloat(holder, 0x8A0CBAAC, &At<float>(self, field::kDecay));
    GetFloat(holder, 0x8A0CBAAD, &At<float>(self, field::kUnjailedFactor));

    Vector quarterPoints;
    FirstPoint(&quarterPoints, 0.0f, 0.1f);
    PushPoint(&quarterPoints, 255.0f, 0.0f);
    Vector factorPoints;
    FirstPoint(&factorPoints, 1.0f, 0.0f);
    PushPoint(&factorPoints, 2.0f, 0.1f);
    PushPoint(&factorPoints, 5.0f, 0.5f);
    for (uint32_t group = 0; group < 3; ++group) {
        char* const quarter = base + field::kQuarterCurves + 16 * group;
        SetPoints(quarter, &quarterPoints);
        LoadCurve(quarter, kTuningKey, 0x8A0CBAA4 + group);
        char* const factor = base + field::kFactorCurves + 16 * group;
        SetPoints(factor, &factorPoints);
        LoadCurve(factor, kTuningKey, 0x8A0CBAA7 + group);
    }
    Vector excessPoints;
    FirstPoint(&excessPoints, 0.0f, 0.0f);
    PushPoint(&excessPoints, 1000000.0f, 50000.0f);
    SetPoints(base + field::kExcessCurve, &excessPoints);
    LoadCurve(base + field::kExcessCurve, kTuningKey, 0x8A0CBAAA);
    Vector chancePoints;
    FirstPoint(&chancePoints, 0.0f, 0.0f);
    PushPoint(&chancePoints, 255.0f, 1.0f);
    SetPoints(base + field::kChanceCurve, &chancePoints);
    LoadCurve(base + field::kChanceCurve, kTuningKey, 0x8A0CBAAB);
    Virtual<uint32_t>(exemplar, 0x8);
    for (Vector* points : {&chancePoints, &excessPoints, &factorPoints, &quarterPoints})
        if (points->begin)
            Free(points->begin);
}

// 0x006A0BA0 inlines this (0x0069FCF0, registers ESI and EDI): the bits
// 1 << (n - 1) of a byte list; all bits when a byte is 0.
void MaskOf(const Vector& bytes, uint32_t* mask)
{
    *mask = 0;
    for (const char* byte = bytes.begin; byte != bytes.end; ++byte) {
        const uint8_t n = static_cast<uint8_t>(*byte);
        if (!n) {
            *mask = 0xFFFFFFFF;
            return;
        }
        *mask |= 1u << ((n - 1) & 31);
    }
}

// Frees each crime type and empties the list (the loop of 0x006A1450 and
// 0x006A13E0).
void FreeTypes()
{
    auto& types = Global<Vector>(addr::cSC4CrimeSimulator_CrimeTypes);
    for (uint32_t i = 0; i < static_cast<uint32_t>((types.end - types.begin) >> 2); ++i) {
        auto* const type = reinterpret_cast<CrimeType**>(types.begin)[i];
        if (!type)
            continue;
        type->nameVtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (type->nameBegin)
            Free(type->nameBegin);
        OperatorDelete(type);
    }
    types.end = types.begin;
}

// 0x006A0BA0: one crime type from its exemplar (the record with its
// instance is reused): the name (localized, else property 0xAA12FCB0), the
// key (0xAA12FCB1), the four masks (0xAA12FCB2, B3, B4, B7, all bits by
// default), the strength (0xAA12FCB5, 0x40), notify (0xAA12FCB6) and the
// weight (0xAA12FCB8, 10).
void __cdecl LoadCrimeType(const uint32_t* key)
{
    static bool logged = false;
    FirstCall("LoadCrimeType", logged);
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &exemplar, 0u, 0u))
        return;
    const uint32_t instance = key[2];
    uint32_t count;
    CrimeType* const* types = CrimeTypes(&count);
    CrimeType* type = nullptr;
    for (uint32_t i = 0; i < count; ++i)
        if (types[i]->id == instance) {
            type = types[i];
            break;
        }
    if (!type) {
        type = static_cast<CrimeType*>(OperatorNew(0x3C));
        if (type) {
            type->nameBegin = nullptr;
            type->nameEnd = nullptr;
            type->nameCapacity = nullptr;
            char* const buffer = static_cast<char*>(StlAllocate(8));
            type->nameBegin = buffer;
            type->nameEnd = buffer;
            type->nameCapacity = buffer + 8;
            buffer[0] = 0;
            type->nameVtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
            type->nameZero = 0;
            type->key[0] = type->key[1] = type->key[2] = 0;
        }
        auto& list = Global<Vector>(addr::cSC4CrimeSimulator_CrimeTypes);
        if (list.end != list.capacity) {
            if (list.end)
                *reinterpret_cast<CrimeType**>(list.end) = type;
            list.end += sizeof(CrimeType*);
        } else {
            char allocator;
            PtrInsert(&list, list.end, &type, &allocator, 1, 1);
        }
    }
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    type->id = instance;
    if (type->nameBegin != type->nameEnd) {
        type->nameBegin[0] = 0;
        type->nameEnd = type->nameBegin;
    }
    type->masks[2] = 0xFFFFFFFF;
    type->masks[0] = 0xFFFFFFFF;
    type->masks[1] = 0xFFFFFFFF;
    type->masks[3] = 0xFFFFFFFF;
    type->strength = 0x40;
    type->notify = 0;
    type->weight = 10.0f;
    if (!Localized(&type->nameVtable, key[1], key[2], 0x8A416B2E, 0))
        GetString(holder, 0xAA12FCB0, &type->nameVtable);
    GetKey(holder, 0xAA12FCB1, type->key);
    Vector bytes{};
    constexpr uint32_t kMaskProperties[4] = {0xAA12FCB2, 0xAA12FCB3, 0xAA12FCB4, 0xAA12FCB7};
    for (int mask = 0; mask < 4; ++mask)
        if (GetBytes(holder, kMaskProperties[mask], &bytes))
            MaskOf(bytes, &type->masks[mask]);
    GetUint8(holder, 0xAA12FCB5, &type->strength);
    GetUint8(holder, 0xAA12FCB6, &type->notify);
    GetFloat(holder, 0xAA12FCB8, &type->weight);
    Virtual<uint32_t>(exemplar, 0x8);
    if (bytes.begin)
        Free(bytes.begin);
}

// 0x006A1450 (the first Init, message 0xC53D10AA): the crime types freed,
// then one per exemplar of type 0xEA12F32C in group 0x6534284A (resource
// manager slot 17 with a key filter).
void __cdecl LoadCrimeTypes()
{
    static bool logged = false;
    FirstCall("LoadCrimeTypes", logged);
    alignas(4) char filter[0x14];
    reinterpret_cast<FilterFn>(addr::SC4ResourceKeyFilter_ctor)(filter, 0x6534284A, 0xEA12F32C);
    FreeTypes();
    void* keys = nullptr;
    if (Virtual<uint32_t>(Global<void*>(addr::SC4_ResourceManager), 0x44, &keys, filter + 8)) {
        const int32_t count = Virtual<int32_t>(keys, 0x24);
        for (int32_t i = 0; i < count; ++i)
            reinterpret_cast<LoadTypeFn>(addr::cSC4CrimeSimulator_LoadCrimeType)(
                Virtual<const uint32_t*>(keys, 0x28, i));
        Virtual<uint32_t>(keys, 0x8);
    }
    reinterpret_cast<FilterDestroyFn>(addr::SC4ResourceKeyFilter_dtor)(filter);
}

// 0x006A13E0: the cleanup Init registers (service 0x00B43C94, slot 69):
// frees the crime types once.
uint8_t __cdecl FreeCrimeTypes()
{
    static bool logged = false;
    FirstCall("FreeCrimeTypes", logged);
    uint8_t& loaded = Global<uint8_t>(addr::cSC4CrimeSimulator_CrimeTypesLoaded);
    if (loaded) {
        loaded = 0;
        FreeTypes();
    }
    return 1;
}

using TuningFn = void(__fastcall*)(void*, void*);
using TypeFn = void(__cdecl*)(const uint32_t*);
using TypesFn = void(__cdecl*)();
using CleanupFn = uint8_t(__cdecl*)();
TuningFn g_originalTuning = nullptr;
TypeFn g_originalType = nullptr;
TypesFn g_originalTypes = nullptr;
CleanupFn g_originalCleanup = nullptr;

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

bool InstallTuning(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4CrimeSimulator_LoadTuning, &LoadTuning, &g_originalTuning) &&
           Install(addr::cSC4CrimeSimulator_LoadCrimeType, &LoadCrimeType, &g_originalType) &&
           Install(addr::cSC4CrimeSimulator_LoadCrimeTypes, &LoadCrimeTypes, &g_originalTypes) &&
           Install(addr::cSC4CrimeSimulator_FreeCrimeTypes, &FreeCrimeTypes, &g_originalCleanup);
}

const bool g_registeredTuning = registry::Register({
    "simulators.crime.tuning",
    "simulators/crime",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_LoadTuning,
    "Our crime tuning and crime types (docs/crime.md)",
    false,
    InstallTuning,
});

} // namespace
} // namespace opensc4::crime_sim
