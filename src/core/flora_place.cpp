// Our cSC4FloraSimulator (build 641), fifth part: placing trees (docs/flora.md,
// "Placing"): whether a place takes a tree (slot 15) and a new tree by type
// and position (slot 17), where trees close by of the same growth chain
// merge into one older tree.
#include "core/flora_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::flora_sim {
namespace {

constexpr uint32_t kFloraOccupant = 0x74758926;
constexpr uint32_t kFloraInterface = 0xC83DD057;

using ListFn = void(__thiscall*)(void* list);
using IntersectFn = uint8_t(__cdecl*)(const float* min1, const float* max1, const float* min2, const float* max2,
                                      uint32_t, uint32_t);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using CtorFn = void*(__thiscall*)(void* object);
using RangeFn = uint32_t(__thiscall*)(void* random, uint32_t low, uint32_t high);
using MergedFn = uint32_t(__thiscall*)(void* self, uint32_t a, uint32_t b);

// The type after `type` in the growth chain (+4), 0 for none or unknown.
uint32_t NextStage(uint32_t type)
{
    const char* const data = FloraData(type);
    return data ? At<uint32_t>(const_cast<char*>(data), 4) : 0;
}

// 0x006B0DA0: two trees as one: when b follows a in the growth chain, the
// type after b; when a follows b, the type after a; else 0.
uint32_t __fastcall MergedType(void*, void*, uint32_t a, uint32_t b)
{
    static bool logged = false;
    FirstCall("MergedType", logged);
    for (uint32_t type = a; type;) {
        if (type == b)
            return NextStage(b);
        const char* const data = FloraData(type);
        if (!data)
            break;
        type = At<uint32_t>(const_cast<char*>(data), 4);
    }
    for (uint32_t type = b; type;) {
        if (type == a)
            return NextStage(a);
        const char* const data = FloraData(type);
        if (!data)
            return 0;
        type = At<uint32_t>(const_cast<char*>(data), 4);
    }
    return 0;
}

// Slot 15 (0x006B1C70): a tree of `type` at x, z: on land (unless a water
// tree), on ground no steeper than the type allows, inside the city, and
// clear of other flora's boxes (but `ignore`).
uint8_t __fastcall LocationIsOKForNewFlora(void* self, void*, uint32_t type, float x, float z, void* ignore)
{
    static bool logged = false;
    FirstCall("LocationIsOKForNewFlora", logged);
    char* const data = FloraData(type);
    if (!data)
        return 0;
    uint8_t unchecked = 0;
    const float height = static_cast<float>(Virtual<double>(At<void*>(self, field::kLots), 0x48, x, z, &unchecked));
    const double sea = Virtual<double>(At<void*>(self, field::kTerrain), 0x74);
    if (!unchecked) {
        if (!(height >= sea) && !data[0xC])
            return 0;
        const float fill = Global<float>(addr::cSC4FloraSimulator_DefaultNormal);
        float normal[3] = {fill, fill, fill};
        Virtual<void>(At<void*>(self, field::kTerrain), 0x68, x, z, normal);
        if (!(1.0 - static_cast<double>(At<float>(data, 0x2C)) < static_cast<double>(normal[1])))
            return 0;
    }
    uint8_t ok = 1;
    const double radius = At<float>(data, 0x40);
    const float min[3] = {static_cast<float>(x - radius), static_cast<float>(height - 16.0),
                          static_cast<float>(z - radius)};
    const float max[3] = {static_cast<float>(x + radius),
                          static_cast<float>(height + static_cast<double>(At<float>(data, 0x44))),
                          static_cast<float>(radius + z)};
    if (min[0] < 0.0f || min[2] < 0.0f)
        return 0;
    void* const city = Global<void*>(addr::SC4_City);
    if (Virtual<double>(city, 0x164) < max[0] || Virtual<double>(city, 0x168) < max[2])
        return 0;
    void* list = nullptr;
    reinterpret_cast<ListFn>(addr::SC4List_Construct3)(&list);
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x90, &list, min, max, At<void*>(self, field::kFilter), -1);
    for (void** node = static_cast<void**>(static_cast<void**>(list)[0]); node != list;
         node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        if (ok && occupant != ignore) {
            float otherMin[3], otherMax[3];
            Virtual<void*>(occupant, 0x28, otherMin, otherMax);
            if (reinterpret_cast<IntersectFn>(addr::SC4_BoxesIntersect)(min, max, otherMin, otherMax, 0, 0))
                ok = 0;
        }
        Virtual<uint32_t>(occupant, 0x8);
    }
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&list);
    return ok;
}

// Slot 17 (0x006B1EA0): a new tree of `type` (or the best type for the
// place when 0) at `position`. Unless `force`, flora within the type's
// radius of the same growth chain is taken out and merged in: the type
// moves on and the tree goes to the average place, until nothing merges;
// then the place must take the tree. The height comes from the ground when
// position[1] <= 0. Returns the new occupant, or null.
void* __fastcall AddNewFloraOccupantAt(void* self, void*, uint32_t type, float* position, uint8_t force)
{
    static bool logged = false;
    FirstCall("AddNewFloraOccupant(type)", logged);
    void* result = nullptr;
    const bool valid = Virtual<uint8_t>(self, 0x2C, type);
    if (!valid && type)
        return nullptr;
    if (!valid || !type) {
        float score = 0;
        type = Virtual<uint32_t>(self, 0x40, position[0], position[2], &score);
        if (!type)
            return nullptr;
    }
    if (!force) {
        for (;;) {
            char* const data = FloraData(type);
            if (!data)
                break;
            void** const list = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
            list[0] = list;
            list[1] = list;
            void* head = list;
            int32_t merged = 1;
            const double ground =
                Virtual<double>(At<void*>(self, field::kTerrain), 0x34, position[0], position[2]);
            const double radius = At<float>(data, 0x48);
            const double half = At<float>(data, 0x4C);
            float min[3] = {static_cast<float>(position[0] - radius), static_cast<float>(ground - half),
                            static_cast<float>(position[2] - radius)};
            float max[3] = {static_cast<float>(position[0] + radius), static_cast<float>(ground + half),
                            static_cast<float>(position[2] + radius)};
            Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x94, &head, min, max, kFloraOccupant, -1);
            position[0] = position[0] + position[0];
            position[1] = position[1] + position[1];
            position[2] = position[2] + position[2];
            int32_t count = 2;
            for (void** node = static_cast<void**>(list[0]); node != list; node = static_cast<void**>(node[0])) {
                void* const occupant = node[2];
                void* flora = nullptr;
                if (Virtual<uint8_t>(occupant, 0x0, kFloraInterface, &flora)) {
                    const uint32_t mergedType =
                        reinterpret_cast<MergedFn>(addr::cSC4FloraSimulator_MergedType)(
                            self, Virtual<uint32_t>(flora, 0x10), type);
                    if (mergedType) {
                        Virtual<void*>(occupant, 0x28, min, max);
                        const double share = static_cast<double>(Global<float>(addr::SC4_FloatHalf)) / count;
                        const float sumX = static_cast<float>(
                            static_cast<double>(max[0]) + min[0] + position[0]);
                        const float sumZ = static_cast<float>(
                            static_cast<double>(max[2]) + min[2] + position[2]);
                        if (Virtual<uint8_t>(self, 0x3C, mergedType, static_cast<float>(sumX * share),
                                             static_cast<float>(sumZ * share), occupant)) {
                            Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, 0u);
                            position[2] = sumZ;
                            position[0] = sumX;
                            type = mergedType;
                            ++merged;
                            ++count;
                        }
                    }
                }
                Virtual<uint32_t>(occupant, 0x8);
                if (flora)
                    Virtual<uint32_t>(flora, 0x8);
            }
            position[1] = -1.0f;
            const double share = static_cast<double>(Global<float>(addr::SC4_FloatHalf)) / merged;
            position[0] = static_cast<float>(share * position[0]);
            position[2] = static_cast<float>(share * position[2]);
            reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
            Virtual<void>(ListNodeAllocator(), 0x10, head);
            if (merged <= 1)
                break;
        }
        if (!Virtual<uint8_t>(self, 0x3C, type, position[0], position[2], nullptr))
            return nullptr;
    }

    void* flora = reinterpret_cast<AllocateFn>(addr::cSC4FloraOccupant_Allocate)(0x5C);
    if (flora)
        flora = reinterpret_cast<CtorFn>(addr::cSC4FloraOccupant_cSC4FloraOccupant)(flora);
    Virtual<uint32_t>(flora, 0x4);
    Virtual<uint8_t>(flora, 0x14, type);
    Virtual<uint8_t>(flora, 0x34,
                     reinterpret_cast<RangeFn>(addr::cRZRandom_Between)(static_cast<char*>(self) + field::kRandom, 0u, 4u));
    if (position[1] <= 0.0f) {
        const float sea = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x74));
        const char* const data = FloraData(type);
        const double height = Virtual<double>(At<void*>(self, field::kLots), 0x48, position[0], position[2], 0u);
        position[1] = static_cast<float>(height);
        if (!data[0xC] && height < sea)
            position[1] = sea;
    }
    if (position[1] >= 0.0f) {
        if (char* data = FloraData(type))
            Virtual<uint8_t>(flora, 0x38, data);
        void* const occupant = static_cast<char*>(flora) + 4;
        Virtual<uint8_t>(occupant, 0x24, position);
        Virtual<uint8_t>(flora, 0x1C, Virtual<uint32_t>(At<void*>(self, field::kSimulator), 0x20));
        Virtual<uint8_t>(flora, 0x24, Virtual<uint32_t>(flora, 0x18));
        Virtual<uint8_t>(occupant, 0xC);
        if (Virtual<uint8_t>(self, 0x48, occupant)) {
            result = Virtual<void*>(flora, 0xC);
            if (result) {
                int32_t cells[4];
                Virtual<uint8_t>(result, 0x2C, cells);
            }
        }
    }
    Virtual<uint32_t>(flora, 0x8);
    return result;
}

// --- Installation -----------------------------------------------------------

using MergedOursFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t);
MergedOursFn g_originalMerged = nullptr;

bool InstallPlacement(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4FloraSimulator_vftable);
    if (vtable[15] != 0x006B1C70 || vtable[17] != 0x006B1EA0) {
        log::Write("flora placement: slots 15/17 hold %08X/%08X; not installing", static_cast<unsigned>(vtable[15]),
                   static_cast<unsigned>(vtable[17]));
        return false;
    }
    const addr::Entry* merged = addr_check::Find(addr::cSC4FloraSimulator_MergedType);
    if (!merged || !hook::Install(*merged, &MergedType, &g_originalMerged))
        return false;
    DWORD old = 0;
    if (!VirtualProtect(vtable + 15, 3 * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    vtable[15] = reinterpret_cast<std::uintptr_t>(&LocationIsOKForNewFlora);
    vtable[17] = reinterpret_cast<std::uintptr_t>(&AddNewFloraOccupantAt);
    VirtualProtect(vtable + 15, 3 * sizeof(*vtable), old, &old);
    return true;
}

const bool g_registeredPlacement = registry::Register({
    "simulators.flora.placement",
    "simulators/flora",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_vftable,
    "Our flora placement: whether a place takes a tree, new trees and their merging (docs/flora.md)",
    false,
    InstallPlacement,
});

} // namespace
} // namespace opensc4::flora_sim
