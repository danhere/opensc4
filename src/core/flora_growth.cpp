// Our cSC4FloraSimulator (build 641), second part: growth (docs/flora.md,
// "Growth"): the daily removal of dying trees, the monthly pass over the
// tracts (aging, spreading) and the random seeding.
#include "core/flora_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace opensc4::flora_sim {
namespace {

constexpr uint32_t kFloraOccupant = 0x74758926;
constexpr uint32_t kFloraInterface = 0xC83DD057;
constexpr uint32_t kChanged = 0x99EF1145;

using ListFn = void(__thiscall*)(void* list);
using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
using RestartFn = void(__thiscall*)(void* iterator, int32_t mode);
using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t count);
using UnitFn = double(__thiscall*)(void* random);
using UniformFn = double(__thiscall*)(void* random, double low, double high);
using SuitabilityFn = double(__thiscall*)(void* self, uint32_t type, float x, float z);
using SeedFn = uint8_t(__thiscall*)(void* self, uint32_t type, float x, float z);
using AgeFn = void(__thiscall*)(void* self, void* flora);
using RemoveFn = void(__thiscall*)(void* self, uint32_t count);
using SlopeFn = int32_t(__thiscall*)(void* self, float x, float z, uint8_t uphill);

void* Random(void* self) { return static_cast<char*>(self) + field::kRandom; }

// value - 0.5 rounded through a float, then one more when value is that
// plus one: the game's floor.
int32_t Floor(double value)
{
    const float rounded = static_cast<float>(value - Global<float>(addr::SC4_FloatHalf) + Global<float>(addr::SC4_FloatRoundMagic));
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    int32_t result = static_cast<int32_t>(bits + 0xB4C00000u);
    if (static_cast<double>(result + 1) == value)
        result += 1;
    return result;
}

// An int read as unsigned, as a float (FILD, then 2^32 when negative).
double Unsigned(int32_t value)
{
    double result = value;
    if (value < 0)
        result += Global<float>(addr::SC4_FloatTwoTo32);
    return result;
}

// 0x006B15F0: up to `count` dying trees off the list (+0xA4), each removed
// from the occupant manager (slot 21) and released.
void __fastcall RemoveDying(void* self, void*, uint32_t count)
{
    static bool logged = false;
    FirstCall("RemoveDying", logged);
    while (count != 0) {
        void** const list = At<void**>(self, field::kDying);
        --count;
        if (static_cast<void**>(list[0]) == list)
            return;
        void** const node = static_cast<void**>(list[0]);
        void** const previous = static_cast<void**>(node[1]);
        void** const next = static_cast<void**>(node[0]);
        void* const occupant = node[2];
        previous[0] = next;
        next[1] = previous;
        Virtual<uint32_t>(ListNodeAllocator(), 0x10, node);
        Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, 0u);
        Virtual<uint32_t>(occupant, 0x8);
    }
}

// 0x006B16D0 (message 0x66956814): the death timer (+0xA8) by its rate
// (+0xB0); each whole step, +0xB4 more deaths; whole deaths are removed.
void __fastcall Step(void* self, void*)
{
    static bool logged = false;
    FirstCall("Step", logged);
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    const double timer = static_cast<double>(At<float>(self, field::kDeathRate)) + At<float>(self, field::kDeathTimer);
    At<float>(self, field::kDeathTimer) = static_cast<float>(timer);
    if (timer >= one) {
        At<float>(self, field::kDeathTimer) = static_cast<float>(timer - one);
        At<float>(self, field::kDeaths) =
            static_cast<float>(static_cast<double>(At<float>(self, field::kDeathsPerStep)) + At<float>(self, field::kDeaths));
    }
    if (!(At<float>(self, field::kDeaths) < one)) {
        const int32_t whole = Floor(At<float>(self, field::kDeaths));
        At<float>(self, field::kDeaths) = static_cast<float>(static_cast<double>(At<float>(self, field::kDeaths)) - whole);
        reinterpret_cast<RemoveFn>(addr::cSC4FloraSimulator_RemoveDying)(self, static_cast<uint32_t>(whole));
    }
}

// 0x006B0900: a type's suitability at a position (its table at +0x50, row
// by slope class, column by the cell service's slot 19), over 255; 0 for
// an unknown type.
double __fastcall Suitability(void* self, void*, uint32_t type, float x, float z)
{
    static bool logged = false;
    FirstCall("Suitability", logged);
    const char* const data = FloraData(type);
    if (!data)
        return Global<float>(addr::SC4_FloatZero);
    const int32_t column = Virtual<int32_t>(At<void*>(self, field::kCellService), 0x4C, x, z);
    const int32_t row = reinterpret_cast<SlopeFn>(addr::cSC4FloraSimulator_SlopeClass)(self, x, z, data[0xC]);
    return static_cast<double>(static_cast<uint8_t>(data[(row + 5) * 16 + column])) * Global<float>(addr::SC4_FloatInv255);
}

// 0x006B0980: a new tree of the type at a position, if the place is fine
// (slot 15) and suitable enough (+0x30), with a chance from +0x34 (slot 17).
uint8_t __fastcall Seed(void* self, void*, uint32_t type, float x, float z)
{
    static bool logged = false;
    FirstCall("Seed", logged);
    if (!Virtual<uint8_t>(self, 0x3C, type, x, z, static_cast<void*>(nullptr)))
        return 0;
    const char* const data = FloraData(type);
    const double suitability =
        reinterpret_cast<SuitabilityFn>(addr::cSC4FloraSimulator_Suitability)(self, type, x, z);
    const float stored = static_cast<float>(suitability);
    const float threshold = *reinterpret_cast<const float*>(data + 0x30);
    if (suitability < threshold)
        return 0;
    const float chance = *reinterpret_cast<const float*>(data + 0x34);
    const double draw = reinterpret_cast<UnitFn>(addr::cRZRandom_NextFloat)(Random(self));
    const double left = (static_cast<double>(stored) - threshold) * draw;
    const double right = (static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)) - threshold) * chance;
    if (!(right > left))
        return 0;
    const float position[3] = {x, 0.0f, z};
    Virtual<uint32_t>(self, 0x44, type, position, 0u);
    return 1;
}

// 0x006AFBE0, inlined (its caller pops its argument): a tree's life stage
// by age against its type's +0x10, +0x24 and +0x28.
int32_t Stage(const char* data, float age)
{
    const float mature = *reinterpret_cast<const float*>(data + 0x10);
    if (!(age < *reinterpret_cast<const float*>(data + 0x28)))
        return 4;
    if (!(age < *reinterpret_cast<const float*>(data + 0x24)))
        return 3;
    if (!(age < mature))
        return 2;
    const double half = static_cast<double>(mature) * Global<float>(addr::SC4_FloatHalf);
    return half <= age ? 1 : 0;
}

// 0x006B0A80: one tree's month: dying or dead ones start over as seedlings;
// the others age, die where unsuitable, and mature ones spread seeds
// around them now and then.
void __fastcall Age(void* self, void*, void* flora)
{
    static bool logged = false;
    FirstCall("Age", logged);
    const int32_t age = At<int32_t>(self, field::kDate) - Virtual<int32_t>(flora, 0x18);
    const uint32_t type = Virtual<uint32_t>(flora, 0x10);
    const char* const data = FloraData(type);
    const int32_t state = Virtual<int32_t>(flora, 0x28);
    if (!data)
        return;
    int32_t stage = 0;
    float ageFloat = 0.0f;
    if (state < 3) {
        ageFloat = static_cast<float>(Unsigned(age));
        stage = Stage(data, ageFloat);
    }
    if (state >= 3 || stage >= 3) {
        Virtual<uint8_t>(flora, 0x1C, At<int32_t>(self, field::kDate));
        Virtual<uint8_t>(flora, 0x2C, 0);
        Virtual<uint8_t>(Virtual<void*>(flora, 0xC), 0x3C, kChanged, 0u);
        return;
    }
    Virtual<uint32_t>(flora, 0x4);
    float position[3];
    Virtual<uint32_t>(Virtual<void*>(flora, 0xC), 0x20, position);
    uint8_t spread = 0;
    const double suitability =
        reinterpret_cast<SuitabilityFn>(addr::cSC4FloraSimulator_Suitability)(self, type, position[0], position[2]);
    if (At<uint8_t>(self, field::kSeeding) || !(data[0x16C] & 2)) {
        if (suitability < *reinterpret_cast<const float*>(data + 0x30))
            stage = 4;
        else if (!(ageFloat < *reinterpret_cast<const float*>(data + 0x14)))
            spread = 1;
    }
    if (state != stage) {
        float low[3], high[3];
        Virtual<uint32_t>(Virtual<void*>(flora, 0xC), 0x28, high, low);
        Virtual<uint8_t>(flora, 0x2C, stage);
        if (stage > 0 && stage <= 2)
            Virtual<uint8_t>(Virtual<void*>(flora, 0xC), 0x3C, kChanged, 0u);
        else if (stage != 3)
            Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, Virtual<void*>(flora, 0xC), 1u, 0u);
    }
    if (At<uint8_t>(self, field::kSeeding) && spread) {
        const int32_t now = Virtual<int32_t>(At<void*>(self, field::kSimulator), 0x20);
        const int32_t last = Virtual<int32_t>(flora, 0x20);
        const uint32_t seedType = *reinterpret_cast<const uint32_t*>(data + 0x8);
        const double due = Unsigned(last) + *reinterpret_cast<const float*>(data + 0x18);
        if (due < Unsigned(now)) {
            const float seeds = *reinterpret_cast<const float*>(data + 0x1C);
            const float rounded = static_cast<float>(static_cast<double>(seeds) + Global<float>(addr::SC4_FloatRoundMagic));
            uint32_t bits;
            std::memcpy(&bits, &rounded, sizeof bits);
            int32_t count = static_cast<int32_t>(bits + 0xB4C00000u);
            const float reach = *reinterpret_cast<const float*>(data + 0x20);
            const auto Uniform = reinterpret_cast<UniformFn>(addr::cRZRandom_Uniform);
            for (; count > 0; --count) {
                const float x = static_cast<float>(Uniform(Random(self), static_cast<double>(position[0]) - reach,
                                                           static_cast<double>(position[0]) + reach));
                const float z = static_cast<float>(Uniform(Random(self), static_cast<double>(position[2]) - reach,
                                                           static_cast<double>(position[2]) + reach));
                if (x >= 0.0f && x < At<float>(self, field::kSizeX) && z >= 0.0f && z < At<float>(self, field::kSizeZ))
                    reinterpret_cast<SeedFn>(addr::cSC4FloraSimulator_Seed)(self, seedType, x, z);
                Virtual<uint8_t>(flora, 0x24, now);
            }
        }
    }
    Virtual<uint32_t>(flora, 0x8);
}

// 0x006B2CB0 (MonthPassed, after the date into +0x40): the next +0x48
// tracts of the walk (+0x54), every flora occupant in each (2 by 2 cells)
// aged; then +0x4C seeding tries of random types at random places.
void __fastcall OnMonth(void* self, void*)
{
    static bool logged = false;
    FirstCall("OnMonth", logged);
    if (!At<uint8_t>(self, field::kEnabled))
        return;
    int32_t tracts = At<int32_t>(self, field::kTractsPerMonth);
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* head = sentinel;
    void* const walk = static_cast<char*>(self) + field::kWalk;
    while (tracts != 0) {
        int32_t x, z;
        if (!reinterpret_cast<NextFn>(addr::TractIterator_Next)(walk, &x, &z)) {
            reinterpret_cast<RestartFn>(addr::TractIterator_Restart)(walk, 1);
            continue;
        }
        const int32_t low[2] = {2 * x, 2 * z};
        const int32_t high[2] = {2 * x + 1, 2 * z + 1};
        Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &head, low, high, kFloraOccupant, -1);
        if (static_cast<void**>(sentinel[0]) != sentinel) {
            for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
                void* const occupant = node[2];
                void* flora = nullptr;
                if (Virtual<uint8_t>(occupant, 0x80, 1u) && Virtual<uint8_t>(occupant, 0x0, kFloraInterface, &flora)) {
                    reinterpret_cast<AgeFn>(addr::cSC4FloraSimulator_Age)(self, flora);
                    Virtual<uint32_t>(flora, 0x8);
                }
                Virtual<uint32_t>(occupant, 0x8);
            }
            reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
        }
        --tracts;
    }
    void* const terrain = At<void*>(self, field::kTerrain);
    if (At<uint8_t>(self, field::kSeeding) && terrain && Virtual<uint8_t>(terrain, 0x14)) {
        const auto Below = reinterpret_cast<BelowFn>(addr::cRZRandom_Below);
        for (int32_t tries = At<int32_t>(self, field::kSeedsPerMonth); tries != 0;) {
            struct Words {
                char* begin;
                char* end;
            };
            const auto& types = Global<Words>(addr::cSC4FloraSimulator_TypeList);
            --tries;
            const uint32_t index = Below(Random(self), static_cast<uint32_t>((types.end - types.begin) >> 2));
            const uint32_t type = reinterpret_cast<const uint32_t*>(Global<Words>(addr::cSC4FloraSimulator_TypeList).begin)[index];
            const char* const data = FloraData(type);
            if (data[0x16C] & 1) {
                ++tries;
                continue;
            }
            const int32_t xCell = static_cast<int32_t>(Below(Random(self), static_cast<uint32_t>(
                                                                         static_cast<int64_t>(At<float>(self, field::kSizeX)))));
            const float x = static_cast<float>(Unsigned(xCell));
            const int32_t zCell = static_cast<int32_t>(Below(Random(self), static_cast<uint32_t>(
                                                                         static_cast<int64_t>(At<float>(self, field::kSizeZ)))));
            const float z = static_cast<float>(Unsigned(zCell));
            reinterpret_cast<SeedFn>(addr::cSC4FloraSimulator_Seed)(self, type, x, z);
        }
    }
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&head);
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

using RemoveOursFn = void(__fastcall*)(void*, void*, uint32_t);
using StepOursFn = void(__fastcall*)(void*, void*);
using SuitabilityOursFn = double(__fastcall*)(void*, void*, uint32_t, float, float);
using SeedOursFn = uint8_t(__fastcall*)(void*, void*, uint32_t, float, float);
using AgeOursFn = void(__fastcall*)(void*, void*, void*);
RemoveOursFn g_originalRemove = nullptr;
StepOursFn g_originalStep = nullptr, g_originalMonth = nullptr;
SuitabilityOursFn g_originalSuitability = nullptr;
SeedOursFn g_originalSeed = nullptr;
AgeOursFn g_originalAge = nullptr;

bool InstallGrowth(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4FloraSimulator_RemoveDying, &RemoveDying, &g_originalRemove) &&
           Install(addr::cSC4FloraSimulator_Step, &Step, &g_originalStep) &&
           Install(addr::cSC4FloraSimulator_Suitability, &Suitability, &g_originalSuitability) &&
           Install(addr::cSC4FloraSimulator_Seed, &Seed, &g_originalSeed) &&
           Install(addr::cSC4FloraSimulator_Age, &Age, &g_originalAge) &&
           Install(addr::cSC4FloraSimulator_OnMonth, &OnMonth, &g_originalMonth);
}

const bool g_registeredGrowth = registry::Register({
    "simulators.flora.growth",
    "simulators/flora",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_OnMonth,
    "Our flora growth: dying trees, the monthly aging and spreading, random seeding (docs/flora.md)",
    false,
    InstallGrowth,
});

} // namespace
} // namespace opensc4::flora_sim
