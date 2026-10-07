// Our cSC4PollutionSimulator, fourth part: UpdateWorstTractsAndEffects
// (0x006E63C0), the CityInited handler, also run at each month's end: the
// worst tracts and the pollution visual effects (docs/pollution.md, "Worst
// tracts and effects").
//
// The effects draw random numbers (cRZRandom::Range on the generator at
// 0x00B49BD0) for the placement of new kind-1 effects, so ours has to make
// the same draws in the same order: the rest of the city shares that state
// only through this generator, but a different draw would move every later
// one.
#include "core/pollution_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::pollution_sim {
namespace {

constexpr std::size_t kCity = 0x18;            // cISC4City*
constexpr std::size_t kEffects = 0x38;         // effect manager (created with 0x0A78747A)
constexpr std::size_t kBlockIterator = 0x78;   // TractIterator over 8x8-tract blocks
constexpr std::size_t kEffectsEnabled = 0x20C; // bool: kinds 2 to 4 only when set
constexpr std::size_t kWorstAirValue = 0x214;
constexpr std::size_t kWorstWaterValue = 0x21C;
constexpr std::size_t kWorstGarbageValue = 0x224;

constexpr uint32_t kEffectGroup = 0x0A78747A;
constexpr double kThreshold = static_cast<double>(0.05f); // 0x00A880D8
constexpr float kValueScale = 3.0518509e-05f;             // 0x00AB25E4, 1/32767

using RestartFn = void(__thiscall*)(void* iterator, int32_t mode);
using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using RangeFn = double(__thiscall*)(void* random, double low, double high);

const auto RestartBlocks = reinterpret_cast<RestartFn>(addr::TractIterator_Restart);
const auto NextBlock = reinterpret_cast<NextFn>(addr::TractIterator_Next);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto RandomRange = reinterpret_cast<RangeFn>(addr::cRZRandom_Range);

int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// What the game fills in to place an effect: a count, flags and a transform
// with the position at +0x28. The game leaves the two bytes after the flags
// unset; they are 0 here.
struct EffectPlacement {
    uint8_t count = 0;
    uint8_t flags = 0;
    uint8_t unset[2] = {};
    float values[13] = {1.0f, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f}; // +4 to +0x34
};
static_assert(sizeof(EffectPlacement) == 0x38);

int32_t g_created = 0; // effects created by the current call, and random draws
int32_t g_draws = 0;

// Removes a block's effect.
void RemoveEffect(void** slot)
{
    if (!*slot)
        return;
    if (Virtual<uint8_t>(*slot, 0x14))
        Virtual<uint32_t>(*slot, 0x10, 0);
    Virtual<uint32_t>(*slot, 0x8); // Release
    *slot = nullptr;
}

// Creates a block's effect of `kind` at `x, y, z` with `strength`.
void CreateEffect(void* self, void** slot, uint32_t kind, uint32_t code, float x, float y, float z, float* strength)
{
    EffectPlacement placement;
    placement.values[9] = x;  // +0x28
    placement.values[10] = y; // +0x2C
    placement.values[11] = z; // +0x30
    placement.count += 1;
    placement.flags |= 1;
    ++g_created;
    Virtual<uint32_t>(At<void*>(self, kEffects), 0x18, kEffectGroup, kind, slot);
    if (*slot) {
        Virtual<uint32_t>(*slot, 0x20, &placement);
        Virtual<uint32_t>(*slot, 0x30, code, strength, 1u);
        Virtual<uint32_t>(*slot, 0xC, 0u);
    }
}

// The effect strength for a block's value: the kind's curve at value / 32767.
double Strength(std::size_t curve, uint32_t value)
{
    const auto x = static_cast<float>(static_cast<double>(static_cast<int32_t>(value)) * static_cast<double>(kValueScale));
    return EvaluateCurve(Global<void*>(addr::PollutionTuning_EffectCurves + curve * 4), x);
}

void __fastcall UpdateWorstTractsAndEffects(void* self, void*)
{
    g_created = 0;
    g_draws = 0;
    void* const city = At<void*>(self, kCity);
    const int32_t blockWidthX = Ftol(Virtual<double>(city, 0x16C) * 8.0);
    const int32_t blockWidthZ = Ftol(Virtual<double>(city, 0x170) * 8.0);
    const int32_t blocksX = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x174) / 8;
    void* const iterator = &At<char>(self, kBlockIterator);
    RestartBlocks(iterator, 1);
    At<uint32_t>(self, kWorstAirValue) = 0;
    At<uint32_t>(self, kWorstWaterValue) = 0;
    At<uint32_t>(self, kWorstGarbageValue) = 0;

    int32_t blockX, blockZ;
    while (NextBlock(iterator, &blockX, &blockZ)) {
        // The block's 8x8 tracts: worst tracts by the block averages.
        const int32_t x0 = blockX * 8;
        const int32_t z0 = blockZ * 8;
        uint32_t water = 0, air = 0, radiation = 0, garbage = 0;
        for (int32_t x = x0; x <= x0 + 7; ++x) {
            for (int32_t z = z0; z <= z0 + 7; ++z) {
                const auto cx = static_cast<uint32_t>(x);
                const auto cz = static_cast<uint32_t>(z);
                water += static_cast<uint32_t>(static_cast<int32_t>(GetCellValue(Grid(self, field::kWaterGrid), cx, cz)));
                air += static_cast<uint32_t>(static_cast<int32_t>(GetCellValue(Grid(self, field::kAirGrid), cx, cz)));
                radiation += static_cast<uint32_t>(static_cast<int32_t>(GetCellValue(Grid(self, field::kRadiationGrid), cx, cz)));
                garbage += static_cast<uint16_t>(GetCellValue(Grid(self, field::kGarbageGrid), cx, cz)) & 0x7FFFu;
            }
        }
        air >>= 6;
        water >>= 6;
        radiation >>= 6;
        garbage >>= 6;
        const auto centerX = static_cast<uint16_t>(x0 + 4);
        const auto centerZ = static_cast<uint16_t>(z0 + 4);
        if (At<uint32_t>(self, kWorstAirValue) < air) {
            At<uint32_t>(self, kWorstAirValue) = air;
            At<uint16_t>(self, field::kWorstAirTract) = centerX;
            At<uint16_t>(self, field::kWorstAirTract + 2) = centerZ;
        }
        if (At<uint32_t>(self, kWorstWaterValue) < water) {
            At<uint16_t>(self, field::kWorstWaterTract) = centerX;
            At<uint32_t>(self, kWorstWaterValue) = water;
            At<uint16_t>(self, field::kWorstWaterTract + 2) = centerZ;
        }
        if (At<uint32_t>(self, kWorstGarbageValue) < garbage) {
            At<uint32_t>(self, kWorstGarbageValue) = garbage;
            At<uint16_t>(self, field::kWorstGarbageTract) = centerX;
            At<uint16_t>(self, field::kWorstGarbageTract + 2) = centerZ;
        }

        const std::size_t index = static_cast<std::size_t>(blockZ * blocksX + blockX);

        // Kind 1, from water, around a random point near the block's corner.
        {
            void** const slot = At<void**>(self, 0xA8) + index;
            const double strength = Strength(0, water);
            float strengthF = static_cast<float>(strength);
            if (!(strength > kThreshold)) {
                RemoveEffect(slot);
            } else if (*slot) {
                Virtual<uint32_t>(*slot, 0x30, 3u, &strengthF, 1u);
            } else {
                const double rangeX = blockWidthX / 4;
                const double rangeZ = blockWidthZ / 4;
                void* const random = reinterpret_cast<void*>(addr::PollutionTuning_EffectRandom);
                g_draws += 2;
                const double dx = RandomRange(random, -rangeX, rangeX);
                const auto x = static_cast<float>(Virtual<double>(city, 0x16C) * static_cast<double>(x0) + dx);
                const double dz = RandomRange(random, -rangeZ, rangeZ);
                const auto z = static_cast<float>(Virtual<double>(city, 0x170) * static_cast<double>(z0) + dz);
                CreateEffect(self, slot, 1, 3, x, 0.0f, z, &strengthF);
            }
        }

        // Kinds 2 to 4, from air, radiation and garbage, at the block's
        // center on the ground (radiation 3 above it).
        struct Kind {
            std::size_t slots;
            std::size_t curve;
            uint32_t value;
            uint32_t kind;
            float lift;
        };
        const Kind kinds[] = {
            {0xB4, 1, air, 2, 0.0f},
            {0xC0, 2, radiation, 3, 3.0f},
            {0xCC, 3, garbage, 4, 0.0f},
        };
        for (const Kind& k : kinds) {
            void** const slot = At<void**>(self, k.slots) + index;
            const double strength = Strength(k.curve, k.value);
            float strengthF = static_cast<float>(strength);
            if (!(strength > kThreshold) || !At<uint8_t>(self, kEffectsEnabled)) {
                RemoveEffect(slot);
            } else if (*slot) {
                Virtual<uint32_t>(*slot, 0x30, 2u, &strengthF, 1u);
            } else {
                const auto x = static_cast<float>(Virtual<double>(city, 0x16C) * static_cast<double>(x0) +
                                                  static_cast<double>(blockWidthX / 2));
                const auto z = static_cast<float>(Virtual<double>(city, 0x170) * static_cast<double>(z0) +
                                                  static_cast<double>(blockWidthZ / 2));
                double height = Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, x, z);
                if (k.lift != 0.0f)
                    height = height + static_cast<double>(k.lift);
                CreateEffect(self, slot, k.kind, 2, x, static_cast<float>(height), z, &strengthF);
            }
        }
    }
    static int calls = 0;
    if (calls++ < 2)
        log::Write("pollution effects: call %d created %d effects, %d random draws", calls, g_created, g_draws);
}

using HandlerFn = void(__fastcall*)(void* self, void* edx);
HandlerFn g_original = nullptr;

bool Install(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_UpdateWorstTractsAndEffects);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &UpdateWorstTractsAndEffects, &g_original);
}

const bool g_registered = registry::Register({
    "simulators.pollution.effects",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_UpdateWorstTractsAndEffects,
    "Our CityInited handler: worst tracts and pollution effects (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim
