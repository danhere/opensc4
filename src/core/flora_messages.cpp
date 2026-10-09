// Our cSC4FloraSimulator (build 641), third part: the messages (docs/flora.md,
// "Messages"): the daily and monthly passes, buildings and networks placed
// over trees, trees removed, terrain changes, the "PlaceFlora" command and
// resource reloads. The cheat code is in flora_object.cpp.
#include "core/flora_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace opensc4::flora_sim {
namespace {

constexpr uint32_t kFloraOccupant = 0x74758926;
constexpr uint32_t kFloraInterface = 0xC83DD057;

using ListFn = void(__thiscall*)(void* list);
using EraseFn = void*(__thiscall*)(void* list, void** out, void* node);
using SelfFn = void(__thiscall*)(void* self);
using OccupantFn = void(__thiscall*)(void* self, void* occupant, uint32_t type);
using RectFn = void(__thiscall*)(void* self, const int32_t* rect);
using ReheightFn = void(__thiscall*)(void* self, void* occupant, void* flora);
using StaticFn = void(__thiscall*)(void* object);

double Epsilon() { return Global<float>(addr::FloraTuning_HeightEpsilon); }

// 0x006B1200: a tree back on the ground (the lot service's height, slot
// 18; at least sea level for land trees) when it is off by more than
// 0.001, and its occupant updated (slot 24).
void __fastcall Reheight(void* self, void*, void* occupant, void* flora)
{
    static bool logged = false;
    FirstCall("Reheight", logged);
    const float sea = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x74));
    const char* const data = FloraData(Virtual<uint32_t>(flora, 0x10));
    float position[3];
    Virtual<uint32_t>(occupant, 0x20, position);
    double height = Virtual<double>(At<void*>(self, field::kLots), 0x48, position[0], position[2], 0u);
    if (!data[0xC] && height < sea)
        height = sea;
    if (std::fabs(height - position[1]) < Epsilon())
        return;
    position[1] = static_cast<float>(height);
    Virtual<uint8_t>(occupant, 0x24, position);
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x60, occupant, 1u);
}

// 0x006B1AB0 (message 0x278E6003, terrain or networks changed over a cell
// rectangle): each tree there sinks or floats back to the ground; one off by
// 0.001 or more is removed unless +0x2F is set.
void __fastcall NetworkChanged(void* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("NetworkChanged", logged);
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* head = sentinel;
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &head, rect, rect + 2, kFloraOccupant, -1)) {
        for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
            void* const occupant = node[2];
            void* flora = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kFloraInterface, &flora)) {
                float position[3];
                Virtual<uint32_t>(Virtual<void*>(flora, 0xC), 0x20, position);
                const double height =
                    Virtual<double>(At<void*>(self, field::kLots), 0x48, position[0], position[2], 0u);
                if (At<uint8_t>(self, 0x2F) || std::fabs(height - position[1]) < Epsilon())
                    reinterpret_cast<ReheightFn>(addr::cSC4FloraSimulator_Reheight)(self, occupant, flora);
                else
                    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, Virtual<void*>(flora, 0xC), 1u, 0u);
            }
            Virtual<uint32_t>(occupant, 0x8);
            if (flora)
                Virtual<uint32_t>(flora, 0x8);
        }
        reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
    }
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&head);
}

// 0x006B04F0 (message 0x99EF1142): trees under a new building (2 m around
// it) or a network piece the filter (+0x28) accepts are demolished (slot 21).
void __fastcall OnOccupantInserted(void* self, void*, void* occupant, uint32_t type)
{
    static bool logged = false;
    FirstCall("OnOccupantInserted", logged);
    float margin;
    if (type == 0x278128A0)
        margin = 2.0f;
    else if (type == 0xC772BF98 && Virtual<uint8_t>(At<void*>(self, field::kFilter), 0xC, occupant))
        margin = 0.0f;
    else
        return;
    float low[3] = {1.0f, 1.0f, 1.0f};
    float high[3] = {0.0f, 0.0f, 0.0f};
    Virtual<uint32_t>(occupant, 0x28, low, high);
    low[0] = static_cast<float>(static_cast<double>(low[0]) - margin);
    low[1] = -margin;
    low[2] = static_cast<float>(static_cast<double>(low[2]) - margin);
    high[0] = static_cast<float>(static_cast<double>(high[0]) + margin);
    high[1] = static_cast<float>(static_cast<double>(margin) + Global<float>(addr::FloraTuning_Huge));
    high[2] = static_cast<float>(static_cast<double>(high[2]) + margin);
    Virtual<uint8_t>(self, 0x54, low, high, 0);
}

// 0x006B1190 (message 0x99EF1143): a flora occupant removed leaves the
// dying list (+0xA4).
void __fastcall OnOccupantRemoved(void* self, void*, void* occupant, uint32_t type)
{
    static bool logged = false;
    FirstCall("OnOccupantRemoved", logged);
    void** const list = At<void**>(self, field::kDying);
    if (type != kFloraOccupant || static_cast<void**>(list[0]) == list)
        return;
    if (occupant)
        Virtual<uint32_t>(occupant, 0x4);
    void** node = static_cast<void**>(list[0]);
    while (node != list && node[2] != occupant)
        node = static_cast<void**>(node[0]);
    if (occupant)
        Virtual<uint32_t>(occupant, 0x8);
    if (node == At<void**>(self, field::kDying))
        return;
    Virtual<uint32_t>(occupant, 0x8);
    void* out = nullptr;
    reinterpret_cast<EraseFn>(addr::SC4List_Erase)(static_cast<char*>(self) + field::kDying, &out, node);
}

// cIGZMessageTarget2 slot 3 (0x006B2F70), on the part at +4.
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(part) - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x26D31EC4:
        At<uint8_t>(self, field::kSeeding) = 0;
        if (!At<uint8_t>(self, 0x2E))
            return 1;
        At<uint8_t>(self, field::kSeeding) = 1;
        return 1;
    case 0x26D31EC5:
        At<uint8_t>(self, field::kSeeding) = 1;
        return 1;
    case 0x66956814:
        reinterpret_cast<SelfFn>(addr::cSC4FloraSimulator_Step)(self);
        return 1;
    case 0x66956816:
        At<uint32_t>(self, field::kDate) = Virtual<uint32_t>(message, 0x24);
        reinterpret_cast<SelfFn>(addr::cSC4FloraSimulator_OnMonth)(self);
        return 1;
    case 0x278E6003: {
        void* const terrain = At<void*>(self, field::kTerrain);
        if (!terrain || !Virtual<uint8_t>(terrain, 0x14))
            return 1;
        const uint32_t first = Virtual<uint32_t>(message, 0x24);
        const uint32_t second = Virtual<uint32_t>(message, 0x38);
        int32_t rect[4] = {static_cast<int32_t>(first >> 16) - 1, static_cast<int32_t>(second >> 16) - 1,
                           static_cast<int32_t>(first & 0xFFFF), static_cast<int32_t>(second & 0xFFFF)};
        if (rect[0] < 0)
            rect[0] = 0;
        if (rect[2] >= At<int32_t>(self, field::kCellsX))
            rect[2] = At<int32_t>(self, field::kCellsX) - 1;
        if (rect[1] < 0)
            rect[1] = 0;
        if (rect[3] >= At<int32_t>(self, field::kCellsZ))
            rect[3] = At<int32_t>(self, field::kCellsZ) - 1;
        reinterpret_cast<RectFn>(addr::cSC4FloraSimulator_NetworkChanged)(self, rect);
        return 1;
    }
    case 0x99EF1142:
    case 0x99EF1143: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, 0x029244DBu))
            return 1;
        const uint32_t occupantType = Virtual<uint32_t>(message, 0x4C);
        reinterpret_cast<OccupantFn>(type == 0x99EF1142 ? addr::cSC4FloraSimulator_OnOccupantInserted
                                                        : addr::cSC4FloraSimulator_OnOccupantRemoved)(
            self, occupant, occupantType);
        return 1;
    }
    case 0xAB99A64B: {
        // The "PlaceFlora" command: type, x, z; the position goes in as
        // x, z, -1 (the game's order).
        void* const command = Virtual<void*>(message, 0x28);
        const uint32_t floraType = Virtual<uint32_t>(Virtual<void*>(command, 0x14, 0u), 0x60);
        float position[3];
        position[0] = static_cast<float>(Virtual<double>(Virtual<void*>(command, 0x14, 1u), 0x90));
        position[1] = static_cast<float>(Virtual<double>(Virtual<void*>(command, 0x14, 2u), 0x90));
        position[2] = -1.0f;
        Virtual<uint32_t>(self, 0x44, floraType, position, 0u);
        return 1;
    }
    case 0x230E27AC:
        // Another cheat code, or "Flora" alone, runs on into the reload
        // code below, as in the game.
        if (CheatMessage(self, message))
            return 1;
        [[fallthrough]];
    case 0xC53D10AA: {
        Virtual<uint32_t>(message, 0x24);
        const uint32_t instance = Virtual<uint32_t>(message, 0x38);
        if (Virtual<void*>(self, 0x1C, instance)) {
            reinterpret_cast<StaticFn>(addr::SC4Uint32Vector_Clear)(reinterpret_cast<void*>(addr::cSC4FloraSimulator_TypeList));
            reinterpret_cast<StaticFn>(addr::SC4FloraMap_Clear)(reinterpret_cast<void*>(addr::cSC4FloraSimulator_TypeMap));
            reinterpret_cast<StaticFn>(addr::SC4FamilyMap_Clear)(reinterpret_cast<void*>(addr::cSC4FloraSimulator_FamilyMap));
            reinterpret_cast<SelfFn>(addr::cSC4FloraSimulator_LoadTypes)(self);
        }
        if (instance == 0x1A2FDB6A)
            reinterpret_cast<SelfFn>(addr::cSC4FloraSimulator_LoadTuning)(self);
        return 1;
    }
    default:
        return 1;
    }
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

using ReheightOursFn = void(__fastcall*)(void*, void*, void*, void*);
using RectOursFn = void(__fastcall*)(void*, void*, const int32_t*);
using OccupantOursFn = void(__fastcall*)(void*, void*, void*, uint32_t);
ReheightOursFn g_originalReheight = nullptr;
RectOursFn g_originalNetwork = nullptr;
OccupantOursFn g_originalInserted = nullptr, g_originalRemoved = nullptr;

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641() || !Install(addr::cSC4FloraSimulator_Reheight, &Reheight, &g_originalReheight) ||
        !Install(addr::cSC4FloraSimulator_NetworkChanged, &NetworkChanged, &g_originalNetwork) ||
        !Install(addr::cSC4FloraSimulator_OnOccupantInserted, &OnOccupantInserted, &g_originalInserted) ||
        !Install(addr::cSC4FloraSimulator_OnOccupantRemoved, &OnOccupantRemoved, &g_originalRemoved))
        return false;
    auto* const slot = reinterpret_cast<std::uintptr_t*>(addr::cSC4FloraSimulator_vftable_4) + 3;
    if (*slot != 0x006B2F70) {
        log::Write("flora messages: slot 3 holds %08X; not installing", static_cast<unsigned>(*slot));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(&DoMessage);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

const bool g_registeredMessages = registry::Register({
    "simulators.flora.messages",
    "simulators/flora",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_vftable_4,
    "Our flora DoMessage and its handlers (buildings, networks, terrain); cheat codes still the game's (docs/flora.md)",
    false,
    InstallMessages,
});

} // namespace
} // namespace opensc4::flora_sim
