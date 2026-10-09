// Our cSC4NeighborsSimulator (build 641), second part: making and removing
// connections (docs/neighbors.md, "Connections"): the request dialog, a
// new connection with its marker at the city's edge, a connection taken off
// with its markers, and the connections of a network piece or of a type.
#include "core/neighbors_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::neighbors_sim {
namespace {

constexpr uint32_t kConnectionAdded = 0x8A0D15DA;
constexpr uint32_t kConnectionRemoved = 0xEA0D163E;
constexpr uint32_t kMarker = 0xCAA46595; // the property marking a connection's marker occupant

using AllocateNodesFn = ListNode*(__thiscall*)(void* list, uint32_t count, uint32_t zero);
using CtorFn = void*(__thiscall*)(void* object);
using NewFn = void*(__cdecl*)(std::size_t size);
using TileFn = void(__cdecl*)(int32_t, int32_t, int32_t*, int32_t*, void*);
using RemoveFn = uint32_t(__thiscall*)(void* self, void* connection, uint8_t fromList);
using GetBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using ListFn = void(__thiscall*)(void* list);
using RemoveValueFn = void(__thiscall*)(void* list, void* const* value);
using FreeFn = void(__cdecl*)(void* block);

// A message of `type` with a connection type and a cell (slots 11, 16, 21),
// posted to the message server; what its last Release returns.
uint32_t Post(uint32_t type, uint32_t connectionType, int32_t x, int32_t z)
{
    void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    if (message)
        message = reinterpret_cast<CtorFn>(addr::cGZMessage_cGZMessage)(message);
    Virtual<uint32_t>(message, 0x4);
    Virtual<void*>(message, 0x14, type);
    Virtual<void*>(message, 0x2C, connectionType);
    Virtual<void*>(message, 0x40, x);
    Virtual<void*>(message, 0x54, z);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
    return Virtual<uint32_t>(message, 0x8);
}

// The game's 0x14-byte string.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A localized string (0x00603580): 0x20 bytes, a GameString first.
struct LocalizedString {
    GameString string;
    uint32_t rest[3];
};

void Drop(GameString& string)
{
    string.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (string.begin)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(string.begin);
}

// 5 (0x006D15E0): unless there is one, asks the player whether to connect
// to the city across (its name when established), the game's dialog
// 0x0078DFF0 with the texts 6A231EAA-AA8E151B, -0A8E152D and -0A8E15F9.
// True when the player agrees; the connection itself comes later.
uint8_t __fastcall RequestCreateConnection(void* self, void*, uint32_t type, int32_t x, int32_t z, uint8_t)
{
    static bool logged = false;
    FirstCall("RequestCreateConnection", logged);
    using LocalizedFn = void*(__thiscall*)(LocalizedString* string, uint32_t group, uint32_t instance, uint32_t flag);
    using FormatFn = void(__cdecl*)(GameString* out, const char* format, const char* argument);
    using DialogFn = uint8_t(__cdecl*)(LocalizedString* title, GameString* text, uint32_t, uint32_t);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    if (Virtual<uint8_t>(self, 0x28, type, x, z))
        return 0;
    LocalizedString title, format, name;
    const auto Localized = reinterpret_cast<LocalizedFn>(addr::SC4LocalizedString_SC4LocalizedString);
    Localized(&title, 0x6A231EAA, 0xAA8E151B, 1);
    Localized(&format, 0x6A231EAA, 0x0A8E152D, 1);
    Localized(&name, 0x6A231EAA, 0x0A8E15F9, 1);
    GameString text{};
    char* const buffer = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    buffer[0] = 0;
    text.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    text.zero = 0;
    void* const city = Virtual<void*>(self, 0x30, x, z);
    if (city && Virtual<uint8_t>(city, 0xAC))
        Virtual<void>(city, 0x84, &name);
    reinterpret_cast<FormatFn>(addr::cRZBaseString_Sprintf)(&text, format.string.begin, name.string.begin);
    const uint8_t agreed =
        reinterpret_cast<DialogFn>(addr::SC4UI_QuestionDialog)(&title, &text, 0u, 0u);
    Drop(text);
    Drop(name.string);
    Drop(format.string);
    Drop(title.string);
    return agreed ? 1 : 0;
}

// 6 (0x006D4860): a new connection of a type at a cell past the edge
// (unless there is one), added to the list, its region tile noted, and
// registered with the city across (slot 22). Unless it is a ferry, a marker
// occupant (0x29F10000, property 0xCAA46595; pipes and subways also
// 0xCAA45670 and 0xCAA45672 or 0xCAA45671) on the ground just inside the
// edge, 15.5 m up for elevated kinds, turned by the edge; then message
// 0x8A0D15DA. Returns what the game leaves in EAX.
uint32_t __fastcall CreateConnection(void* self, void*, uint32_t type, int32_t x, int32_t z, uint8_t flag)
{
    static bool logged = false;
    FirstCall("CreateConnection", logged);
    if (Virtual<uint8_t>(self, 0x28, type, x, z))
        return 1;
    void* connection = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x44);
    if (connection)
        connection = reinterpret_cast<CtorFn>(addr::cSC4NeighborConnection_cSC4NeighborConnection)(connection);
    if (connection)
        Virtual<uint32_t>(connection, 0x4);
    {
        ListNode* const head = Head(self, field::kConnections);
        ListNode* const node = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes3)(
            static_cast<char*>(self) + field::kConnections, 1, 0);
        node->value = connection;
        if (connection)
            Virtual<uint32_t>(connection, 0x4);
        ListNode* const last = head->prev;
        node->next = head;
        node->prev = last;
        last->next = node;
        head->prev = node;
        if (connection)
            Virtual<uint32_t>(connection, 0x8);
    }
    int32_t tileX = 0, tileZ = 0;
    reinterpret_cast<TileFn>(addr::cSC4NeighborsSimulator_OtherSideTile)(x, z, &tileX, &tileZ, nullptr);
    At<int32_t>(connection, 0x2C) = z;
    At<int32_t>(connection, 0x30) = tileX;
    At<uint32_t>(connection, 0x24) = type;
    At<int32_t>(connection, 0x28) = x;
    At<int32_t>(connection, 0x34) = tileZ;
    if (void* const city = Virtual<void*>(connection, 0x24))
        Virtual<void>(city, 0x58, connection, 1u);
    if (type != 9) {
        void* const props = Global<void*>(addr::SC4_Service43D10);
        float position[3] = {0, 0, 0};
        void* const ours = At<void*>(self, field::kCity);
        uint32_t orientation;
        int32_t cellX = x, cellZ = z;
        if (x < 0) {
            orientation = flag ? 1 : 3;
            cellX = x + 1;
        } else if (z < 0) {
            orientation = flag ? 2 : 0;
            cellZ = z + 1;
        } else if (x >= Virtual<int32_t>(ours, 0x174)) {
            orientation = flag ? 3 : 1;
            cellX = x - 1;
        } else {
            orientation = flag ? 0 : 2;
            cellZ = z - 1;
        }
        Virtual<void>(ours, 0x184, cellX, cellZ, &position[0], &position[2]);
        position[1] = static_cast<float>(
            Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, position[0], position[2]));
        if (type == 3 || type == 7 || type == 11)
            position[1] = static_cast<float>(static_cast<double>(position[1]) + Global<float>(addr::NeighborTuning_ElevatedHeight));
        void* marker = nullptr;
        if (Virtual<uint8_t>(props, 0x3C, 0x29F10000u, &marker)) {
            Virtual<uint8_t>(Virtual<void*>(marker, 0xC), 0x24, position);
            Virtual<uint8_t>(marker, 0x28, orientation);
            void* const holder = Virtual<void*>(Virtual<void*>(marker, 0xC), 0x18);
            Virtual<uint8_t>(holder, 0x34, kMarker, 1u, 0u);
            if (type == 5) {
                Virtual<uint8_t>(holder, 0x34, 0xCAA45670u, 1u, 0u);
                Virtual<uint8_t>(holder, 0x34, 0xCAA45672u, 1u, 0u);
            } else if (type == 6) {
                Virtual<uint8_t>(holder, 0x34, 0xCAA45670u, 1u, 0u);
                Virtual<uint8_t>(holder, 0x34, 0xCAA45671u, 1u, 0u);
            }
            Virtual<uint8_t>(props, 0x4C, Virtual<void*>(marker, 0xC));
            Virtual<uint32_t>(marker, 0x8);
        }
    }
    return Post(kConnectionAdded, type, x, z);
}

// 0x006D17B0: a connection taken off: unregistered from the city across,
// the marker occupants on the cell just inside the edge (property
// 0xCAA46595) removed, message 0xEA0D163E; and out of the list if asked.
uint32_t __fastcall RemoveConnection(void* self, void*, void* connection, uint8_t fromList)
{
    static bool logged = false;
    FirstCall("RemoveConnection", logged);
    if (void* const city = Virtual<void*>(connection, 0x24))
        Virtual<void>(city, 0x58, connection, 0u);
    void* const props = Global<void*>(addr::SC4_Service43D10);
    int32_t x = 0, z = 0;
    Virtual<void>(connection, 0x18, &x, &z);
    int32_t cellX = x, cellZ = z;
    if (x < 0)
        cellX = x + 1;
    else if (z < 0)
        cellZ = z + 1;
    else if (x >= Virtual<int32_t>(At<void*>(self, field::kCity), 0x174))
        cellX = x - 1;
    else
        cellZ = z - 1;
    auto* const list = static_cast<ListNode*>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    list->next = list;
    list->prev = list;
    void* head = list;
    const int32_t rect[4] = {cellX, cellZ, cellX, cellZ};
    Virtual<uint8_t>(props, 0x48, &head, rect);
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const occupant = node->value;
        uint8_t marker = 0;
        reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool)(Virtual<void*>(occupant, 0x18), kMarker, &marker);
        if (marker)
            Virtual<uint8_t>(props, 0x74, occupant);
    }
    Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0xC0, &head);
    Post(kConnectionRemoved, Virtual<uint32_t>(connection, 0x14), x, z);
    if (fromList) {
        void* value = connection;
        Virtual<uint32_t>(connection, 0x4);
        reinterpret_cast<RemoveValueFn>(addr::SC4List_RemoveValue3)(static_cast<char*>(self) + field::kConnections,
                                                                    &value);
        Virtual<uint32_t>(connection, 0x8);
    }
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
    return Virtual<uint32_t>(ListNodeAllocator(), 0x10, head);
}

// 7 (0x006D4640): the connection of a type at a cell taken off.
uint32_t __fastcall DeleteConnection(void* self, void*, uint32_t type, int32_t x, int32_t z)
{
    void* const connection = Virtual<void*>(self, 0x2C, type, x, z);
    if (!connection)
        return 0;
    return reinterpret_cast<RemoveFn>(addr::cSC4NeighborsSimulator_RemoveConnection)(self, connection, 1);
}

// 8 (0x006CEE40): the connections a network piece or power pole at the
// city's edge makes, taken off (slot 7 at the cell past each edge it is on).
void __fastcall DeleteOccupantConnection(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("DeleteOccupantConnection", logged);
    const uint32_t occupantType = Virtual<uint32_t>(occupant, 0x1C);
    uint32_t type = 0;
    if (occupantType == 0xC772BF98) {
        void* network = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &network)) {
            // Network kinds to connection types; the last match wins.
            constexpr struct {
                uint32_t network;
                uint32_t connection;
            } kKinds[] = {{4, 5}, {11, 10}, {0, 2}, {6, 8}, {12, 12}, {2, 3}, {7, 6}, {8, 7}, {9, 11}, {1, 1}};
            for (const auto& kind : kKinds)
                if (Virtual<uint8_t>(network, 0x58, kind.network))
                    type = kind.connection;
        }
        if (network)
            Virtual<uint32_t>(network, 0x8);
    } else if (occupantType == 0x2890D4DE) {
        type = 4;
    } else {
        return;
    }
    if (!type)
        return;
    float position[3];
    Virtual<uint8_t>(occupant, 0x20, position);
    int32_t cellX = 0, cellZ = 0;
    void* const city = At<void*>(self, field::kCity);
    Virtual<void>(city, 0x17C, position[0], position[2], &cellX, &cellZ);
    if (cellX == 0) {
        cellX = -1;
        Virtual<void>(self, 0x1C, type, -1, cellZ);
        cellX = 0;
    } else if (cellX == Virtual<int32_t>(city, 0x174) - 1) {
        cellX = cellX + 1;
        Virtual<void>(self, 0x1C, type, cellX, cellZ);
        cellX = cellX - 1;
    }
    if (cellZ == 0) {
        Virtual<void>(self, 0x1C, type, cellX, -1);
    } else if (cellZ == Virtual<int32_t>(city, 0x178) - 1) {
        Virtual<void>(self, 0x1C, type, cellX, cellZ + 1);
    }
}

// The type test of 0x006CF660 and 0x006CFAC0: the connection referenced
// while it is asked.
bool OfType(void* connection, uint32_t type)
{
    if (connection)
        Virtual<uint32_t>(connection, 0x4);
    const bool match = Virtual<uint32_t>(connection, 0x14) == type;
    Virtual<uint32_t>(connection, 0x8);
    return match;
}

// 9 (0x006D19F0): every connection of a type taken off (0x006D17B0 without
// the list), then out of the list as the game's remove_if does
// (0x006CF660, 0x006CFAC0, 0x006CFCA0): the others moved forward, the rest
// erased.
void __fastcall DeleteAllConnectionsOfType(void* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("DeleteAllConnectionsOfType", logged);
    for (ListNode* node = Head(self, field::kConnections)->next; node != Head(self, field::kConnections);
         node = node->next) {
        void* const connection = node->value;
        if (Virtual<uint32_t>(connection, 0x14) == type)
            reinterpret_cast<RemoveFn>(addr::cSC4NeighborsSimulator_RemoveConnection)(self, connection, 0);
    }
    ListNode* const head = Head(self, field::kConnections);
    ListNode* found = head->next;
    while (found != head && !OfType(found->value, type))
        found = found->next;
    ListNode* dest = found;
    if (found != head) {
        for (ListNode* node = found->next; node != head; node = node->next) {
            void* const value = node->value;
            if (OfType(value, type))
                continue;
            if (&node->value != &dest->value) {
                void* const old = dest->value;
                if (value != old) {
                    if (value)
                        Virtual<uint32_t>(value, 0x4);
                    dest->value = value;
                    if (old)
                        Virtual<uint32_t>(old, 0x8);
                }
            }
            dest = dest->next;
        }
    }
    for (ListNode* node = dest; node != head;) {
        ListNode* const next = node->next;
        node->prev->next = node->next;
        node->next->prev = node->prev;
        if (node->value)
            Virtual<uint32_t>(node->value, 0x8);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, node);
        node = next;
    }
}

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x006D15E0, RequestCreateConnection),
    OPENSC4_SLOT(6, 0x006D4860, CreateConnection),
    OPENSC4_SLOT(7, 0x006D4640, DeleteConnection),
    OPENSC4_SLOT(8, 0x006CEE40, DeleteOccupantConnection),
    OPENSC4_SLOT(9, 0x006D19F0, DeleteAllConnectionsOfType),
};

#undef OPENSC4_SLOT

using RemoveOursFn = uint32_t(__fastcall*)(void*, void*, void*, uint8_t);
RemoveOursFn g_originalRemove = nullptr;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4NeighborsSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("neighbors connections: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    const addr::Entry* remove = addr_check::Find(addr::cSC4NeighborsSimulator_RemoveConnection);
    if (!remove || !hook::Install(*remove, &RemoveConnection, &g_originalRemove))
        return false;
    DWORD old = 0;
    if (!VirtualProtect(vtable + 5, 5 * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable + 5, 5 * sizeof(*vtable), old, &old);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.neighbors.connections",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_vftable,
    "Our neighbor connections made and taken off, with their markers and messages (docs/neighbors.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::neighbors_sim
