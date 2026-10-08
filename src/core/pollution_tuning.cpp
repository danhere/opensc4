// Our cSC4PollutionSimulator, sixth part: the tuning (docs/pollution.md,
// "Tuning"). LoadTuning sets the tuning globals and curves to defaults,
// then reads them from the simulator's exemplar; the curve defaults go
// through the game's own vector and curve code, so the heap sees the same
// allocations as with the game's function.
#include "core/pollution_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace opensc4::pollution_sim {
namespace {

// std::vector of (x, y) float pairs, as the game lays it out.
struct PointVector {
    float* begin;
    float* end;
    float* capacity;
};

using InsertFn = void(__thiscall*)(PointVector* points, float* where, const float* value, void* allocator,
                                   uint32_t count, uint32_t atEnd);
using SetPointsFn = uint8_t(__thiscall*)(void* curve, const PointVector* points);
using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
using GetPropertyFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using FreeFn = void(__cdecl*)(void* p);

const auto InsertPoints = reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN);
const auto SetPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
const auto LoadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
const auto GetPropertyFloat = reinterpret_cast<GetPropertyFloatFn>(addr::SC4_GetPropertyFloat);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr uint32_t kExemplarInterface = 0xA52160F5;
constexpr std::size_t kDisposalCost = 0x1D0; // float, per unit of garbage disposed of
constexpr std::size_t kFundingCurve = 0xDC;
constexpr std::size_t kUseCurve = 0xEC;

// The six global curves, in the order the game sets and loads them, with
// their properties.
constexpr struct {
    std::uintptr_t pointer;
    uint32_t property;
} kGlobalCurves[] = {
    {addr::PollutionTuning_GarbageRectCurve, 0x69A437F6},
    {addr::PollutionTuning_EffectCurves, 0x0A10C772},
    {addr::PollutionTuning_EffectCurves + 4, 0x0A10C773},
    {addr::PollutionTuning_EffectCurves + 8, 0x0A10C774},
    {addr::PollutionTuning_EffectCurves + 12, 0x0A10C775},
    {addr::PollutionTuning_LandfillCapacityCurve, 0x8C19C3E9},
};

// push_back, as the game's inlined one: in place when there is room.
void PushBack(PointVector& points, const float* value, void* allocator)
{
    if (points.end == points.capacity) {
        InsertPoints(&points, points.end, value, allocator, 1, 1);
        return;
    }
    if (points.end) {
        points.end[0] = value[0];
        points.end[1] = value[1];
    }
    points.end += 2;
}

void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("pollution tuning: first LoadTuning");
    }
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    Global<uint32_t>(addr::PollutionTuning_MaxGarbageLevelForUI) = 27000;
    Global<uint32_t>(addr::PollutionTuning_MaxAirLevelForUI) = 27000;
    Global<uint32_t>(addr::PollutionTuning_MaxWaterLevelForUI) = 32000;
    Global<uint32_t>(addr::PollutionTuning_MaxRadiationLevelForUI) = 32000;
    Global<uint32_t>(addr::PollutionTuning_MaxAirStep) = 50;
    Global<uint32_t>(addr::PollutionTuning_MaxWaterStep) = 50;
    Global<float>(addr::PollutionTuning_TrafficAirFactor) = 8.0f;
    Global<uint32_t>(addr::PollutionTuning_AirPollutedThreshold) = 16000;
    Global<uint32_t>(addr::PollutionTuning_WaterPollutedThreshold) = 16000;
    Global<uint32_t>(addr::PollutionTuning_GarbageAccumulatedThreshold) = 16000;
    Global<uint32_t>(addr::PollutionTuning_Unused291351B5) = 1;
    Global<uint32_t>(addr::PollutionTuning_MonthlyAbductionLimit) = 5000;
    Global<float>(addr::PollutionTuning_GarbageScalingFactor) = 0.125f;
    Global<uint32_t>(addr::PollutionTuning_LandfillTileCapacity) = 0x7FFF;
    Global<uint32_t>(addr::PollutionTuning_GarbageExcessLimit) = 0x9FFEC;
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xC911E35B};

    // Defaults: the global curves flat at 0, the aging curves flat at 1.
    PointVector points{};
    uint8_t allocator = 0;
    float value[2] = {0.0f, 0.0f};
    InsertPoints(&points, nullptr, value, &allocator, 1, 1);
    for (const auto& curve : kGlobalCurves)
        SetPoints(Global<void*>(curve.pointer), &points);
    points.end = points.begin;
    value[1] = 1.0f;
    PushBack(points, value, &allocator);
    value[0] = 1.0f;
    PushBack(points, value, &allocator);
    SetPoints(&At<char>(self, kFundingCurve), &points);
    SetPoints(&At<char>(self, kUseCurve), &points);

    void* exemplar = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, key, kExemplarInterface, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        auto u32 = [holder](uint32_t property, std::uintptr_t global) {
            GetPropertyUint32(holder, property, reinterpret_cast<void*>(global));
        };
        GetPropertyFloat(holder, 0x69501944, &Global<float>(addr::PollutionTuning_TrafficAirFactor));
        u32(0x49134BD4, addr::PollutionTuning_MaxGarbageLevelForUI);
        u32(0x49134BDD, addr::PollutionTuning_MaxAirLevelForUI);
        u32(0x09134BE2, addr::PollutionTuning_MaxWaterLevelForUI);
        u32(0x89135044, addr::PollutionTuning_MaxRadiationLevelForUI);
        u32(0x6A406201, addr::PollutionTuning_MaxAirStep);
        u32(0x8A40620B, addr::PollutionTuning_MaxWaterStep);
        u32(0x0913519B, addr::PollutionTuning_GarbageAccumulatedThreshold);
        u32(0xA91351A6, addr::PollutionTuning_AirPollutedThreshold);
        u32(0x091351AE, addr::PollutionTuning_WaterPollutedThreshold);
        u32(0x291351B5, addr::PollutionTuning_Unused291351B5);
        u32(0x09135479, addr::PollutionTuning_MonthlyAbductionLimit);
        GetPropertyFloat(holder, 0x09135482, &Global<float>(addr::PollutionTuning_GarbageScalingFactor));
        GetPropertyFloat(holder, 0xAA1F8AB7, &At<float>(self, kDisposalCost));
        u32(0x4A652679, addr::PollutionTuning_GarbageExcessLimit);
        u32(0x4A65267A, addr::PollutionTuning_LandfillTileCapacity);
        for (const auto& curve : kGlobalCurves)
            LoadCurve(Global<void*>(curve.pointer), key, curve.property);
        LoadCurve(&At<char>(self, kFundingCurve), key, 0x0911E2E8);
        LoadCurve(&At<char>(self, kUseCurve), key, 0x0911E2E9);
        Virtual<uint32_t>(exemplar, 0x8); // Release
    }
    if (points.begin)
        Free(points.begin);
}

// --- Self-check ---------------------------------------------------------------

using TuningFn = void(__fastcall*)(void*, void*);
TuningFn g_originalTuning = nullptr;

// Everything LoadTuning writes: the globals 0x00B49B68-0x00B49BA4, the
// disposal cost, and the eight curves' segments.
std::vector<uint32_t> TuningState(void* self)
{
    std::vector<uint32_t> state;
    for (std::uintptr_t at = addr::PollutionTuning_GarbageExcessLimit; at <= addr::PollutionTuning_LandfillTileCapacity;
         at += 4)
        state.push_back(Global<uint32_t>(at));
    state.push_back(At<uint32_t>(self, kDisposalCost));
    auto curve = [&state](const void* object) {
        const auto* const fields = static_cast<const uint32_t* const*>(object);
        state.push_back(static_cast<uint32_t>(reinterpret_cast<const char*>(fields[1]) -
                                              reinterpret_cast<const char*>(fields[0])));
        for (const uint32_t* word = fields[0]; word != fields[1]; ++word)
            state.push_back(*word);
        state.push_back(reinterpret_cast<const uint32_t*>(object)[3]);
    };
    for (const auto& global : kGlobalCurves)
        curve(Global<void*>(global.pointer));
    curve(&At<char>(self, kFundingCurve));
    curve(&At<char>(self, kUseCurve));
    return state;
}

// With OPENSC4_POLLUTION_SELFCHECK set: the game's function, then ours,
// and everything both write compared.
void __fastcall CheckLoadTuning(void* self, void* edx)
{
    g_originalTuning(self, edx);
    const std::vector<uint32_t> theirs = TuningState(self);
    LoadTuning(self, edx);
    const std::vector<uint32_t> ours = TuningState(self);
    std::size_t first = 0;
    while (first < theirs.size() && first < ours.size() && theirs[first] == ours[first])
        ++first;
    log::Write("pollution tuning: self-check against the game's: %s (%zu words%s)",
               theirs == ours ? "same" : "DIFFERENT", theirs.size(),
               theirs == ours ? "" : ", see the first differing index in the next line");
    if (theirs != ours)
        log::Write("pollution tuning: first difference at word %zu", first);
}

bool Install(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_LoadTuning);
    if (!game::IsBuild641() || !entry)
        return false;
    if (GetEnvironmentVariableA("OPENSC4_POLLUTION_SELFCHECK", nullptr, 0) != 0)
        return hook::Install(*entry, &CheckLoadTuning, &g_originalTuning);
    return hook::Install(*entry, &LoadTuning, &g_originalTuning);
}

const bool g_registered = registry::Register({
    "simulators.pollution.tuning",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_LoadTuning,
    "Our pollution tuning load: globals and curves from the simulator's exemplar (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim
