// Our cSC4NetworkOccupant, first piece (build 641; src/core/netocc_sim.h has
// the class map, the fields and the slots): 43 small functions of the
// interface, each a detour at the game's address, because the bridge, tunnel
// and pipe classes hold the same functions in their vtables and some are
// called directly.
//
// Every function was written from the disassembly. `self` is the object's
// start (the cISC4NetworkOccupant part); the detours are plain functions, so
// they clobber ECX and EDX where the game's code did not. Functions of the
// game that touch neither ECX nor EDX (callers of the LTCG build may keep
// values in them across the call; to be covered by the register scan before
// a batch): GetRotation 0x00604DC0, GetFlip 0x00604DD0, GetRotationAndFlip
// 0x00604DE0, GetVariation 0x00619880, GetPathIDOverride 0x006197E0,
// SetPathIDOverride 0x006197D0, GetVertexHeight 0x006194C0,
// HasAnyNetworkFlag 0x00604D30, IsLit 0x00619890, IsSupportPylonPresent
// 0x006199C0.
//
// Virtual calls, each count from the asm's pushes for that call (and
// where the callee is ours or known, its own ret N):
//   - the interface's own slots (this object): 0x20 HasNetworkFlag one,
//     0x28 SetNetworkFlag one, 0x2C ClearNetworkFlag one, 0x5C PieceId none,
//     0xC8 ReleasePathInfo none, 0xE8 SetTranslucency two (a bool and a
//     byte; pushed as 1, 0x80 and 0, 0xFF);
//   - the occupant part (object + 4): 0x84 GetFlags none (ours, occbase);
//   - the property holder (object + 0xC): 0x24 GetProperty one (ret 4 of the
//     base's callers in occbase), 0x38 SetProperty three (id, property, 0),
//     0x44 RemoveProperty two (id, 0);
//   - a property: 0x14 none (its variant), 0x1C one (the new variant);
//   - a variant: 0x198 two (pointer, count 2), 0x19C, 0x124, 0x144, 0x104
//     none; 0x04 and 0x08 (AddRef, Release) none;
//   - the framework's message (0x00ACEC00; the layout occbase_sim.cpp's
//     PostOccupantMessage uses): 0x14, 0x2C, 0x40, 0x54, 0x84 one, 0x04 and
//     0x08 none; the message server 0x10 two (the message, 0).
// Direct callees: 0x005E55E0 and 0x009133DA (cdecl, size), the script number
// constructors 0x009166D4 (none), 0x00915D1D, 0x00915D6D, 0x00915E0B (one),
// the destructor 0x00915EB5 (none), the edge store's 0x00619B70 (one, ret 4)
// and 0x00619A40 (two, ret 8), the map's find 0x00497520 (two).
#include "core/netocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::netocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("netocc: first %s", name);
}

namespace {

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// A new cGZMessage (0x2C bytes). Like the game, no test of the allocation
// before the first call through it.
void* NewMessage()
{
    void* const memory = CallCdecl<void*, std::size_t>(addr::RZ_operator_new, 0x2C);
    return memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// A new 0x14-byte script number made by `constructor` (the game's allocation
// 0x005E55E0, null when it fails, else the constructor's result).
template <typename... Args>
void* NewNumber(std::uintptr_t constructor, Args... args)
{
    void* const memory = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, 0x14);
    return memory ? CallThis<void*, Args...>(constructor, memory, args...) : nullptr;
}

// The number given to the holder as property `id` (the game's sequence:
// referenced if there is one, SetProperty(id, number, 0), released).
void StoreNumber(char* self, uint32_t id, void* number)
{
    if (number)
        Virtual<uint32_t>(number, 0x4);
    Virtual<uint32_t>(self + field::kHolder, holder::kSetProperty, id, number, 0u);
    if (number)
        Virtual<uint32_t>(number, 0x8);
}

// The property with ID `id`, or null.
void* PropertyOf(char* self, uint32_t id)
{
    return Virtual<void*>(self + field::kHolder, holder::kGetProperty, id);
}

// The pointer to a property's value: the variant (slot 0x14) and its getter
// at `valueSlot`.
template <typename T>
T* ValueOf(void* property, std::size_t valueSlot)
{
    void* const variant = Virtual<void*>(property, 0x14);
    return Virtual<T*>(variant, valueSlot);
}

// The count of bytes with a non-zero low three bits (0x07070707 mask), as
// the game computes it with a multiply, and of the grouped bit (0x04).
uint32_t CountActive(uint32_t edges)
{
    return (((edges & 0x07070707u) + 0x07070707u) & 0x08080808u) * 0x01020408u >> 0x1B;
}

uint32_t CountActiveNoGroup(uint32_t edges)
{
    return (((edges & 0x03030303u) + 0x03030303u) & 0x04040404u) * 0x01020408u >> 0x1A;
}

// The edge store's lookup (0x00619B70; the value for a key, 0 for none) and
// store (0x00619A40; a zero value removes the entry).
uint32_t StoreGet(char* self, uint32_t type)
{
    return CallThis<uint32_t, uint32_t>(addr::cSC4EdgeConnectionStore_Get, self + field::kEdgeStore, type);
}

uint32_t StoreSet(char* self, uint32_t type, uint32_t value)
{
    return CallThis<uint32_t, uint32_t, uint32_t>(addr::cSC4EdgeConnectionStore_Set, self + field::kEdgeStore, type,
                                                  value);
}

// All the edge values: the first entry's ORed with every further entry's.
uint32_t AllEdges(char* self)
{
    uint32_t edges = At<uint32_t>(self, field::kEdgeFirstValue);
    const uint32_t count = At<uint8_t>(self, field::kEdgeCount);
    if (count) {
        const char* entry = At<char*>(self, field::kEdgeEntries);
        for (uint32_t i = 0; i < count; ++i, entry += 8)
            edges |= *reinterpret_cast<const uint32_t*>(entry + 4);
    }
    return edges;
}

// ====================================================================================
// simulators.netocc.flags: the network flags and what they answer
// ====================================================================================

// Slot 8 (0x00604D10): every bit of the argument set; 0 or 1 in all of EAX.
uint32_t __fastcall HasNetworkFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("HasNetworkFlag", logged);
    return (At<uint32_t>(self, field::kFlags) & flags) == flags ? 1 : 0;
}

// Slot 9 (0x00604D30): any bit of the argument set; 0 or 1 in all of EAX.
// Touches neither ECX nor EDX in the game.
uint32_t __fastcall HasAnyNetworkFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("HasAnyNetworkFlag", logged);
    return (At<uint32_t>(self, field::kFlags) & flags) != 0 ? 1 : 0;
}

// Slot 10 (0x00619EC0): a flag that is new and the usable bit (0x10000000)
// on a placed piece (the occupant part's GetFlags, bit 0) posts message
// 0x89D3E9F3 (the occupant as object, data 1 = 1; queued). The translucent
// bit (0x4000) asks for SetTranslucency(true, 0x80). Then the bits are
// ORed in (read again from memory after the calls, as the game does).
void __fastcall SetNetworkFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SetNetworkFlag", logged);
    if ((flags & flag::kUsable) && !(At<uint32_t>(self, field::kFlags) & flag::kUsable)) {
        if (Virtual<uint32_t>(self + field::kOccupant, 0x84) & 1u) {
            void* const server = Global<void*>(addr::SC4_MessageServer);
            void* const message = NewMessage();
            Virtual<uint32_t>(message, 0x4);
            Virtual<uint32_t>(message, 0x14, kMessageUsableChanged);
            Virtual<uint32_t>(message, 0x84, self);
            Virtual<uint32_t>(message, 0x2C, 1u);
            Virtual<uint32_t>(server, 0x10, message, 0u);
            Virtual<uint32_t>(message, 0x8);
        }
    }
    if (flags & flag::kTranslucent)
        Virtual<uint32_t>(self, slot::kSetTranslucency, 1u, 0x80u);
    At<uint32_t>(self, field::kFlags) |= flags;
}

// Slot 11 (0x00619F70): the same for clearing: with the usable bit asked
// and set, message 0x89D3E9F3 with data 1 = 0 (no test for a placed piece);
// the translucent bit asks for SetTranslucency(false, 0xFF); then the bits
// are removed.
void __fastcall ClearNetworkFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("ClearNetworkFlag", logged);
    if ((flags & flag::kUsable) && (At<uint32_t>(self, field::kFlags) & flag::kUsable)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        void* const message = NewMessage();
        Virtual<uint32_t>(message, 0x4);
        Virtual<uint32_t>(message, 0x14, kMessageUsableChanged);
        Virtual<uint32_t>(message, 0x84, self);
        Virtual<uint32_t>(message, 0x2C, 0u);
        Virtual<uint32_t>(server, 0x10, message, 0u);
        Virtual<uint32_t>(message, 0x8);
    }
    if (flags & flag::kTranslucent)
        Virtual<uint32_t>(self, slot::kSetTranslucency, 0u, 0xFFu);
    At<uint32_t>(self, field::kFlags) &= ~flags;
}

// Slot 17 (0x00604D50): HasNetworkFlag(0x400000) through the slot, its
// result as it is (a subclass may answer differently).
uint32_t __fastcall IsImmovable(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsImmovable", logged);
    return Virtual<uint32_t>(self, slot::kHasNetworkFlag, flag::kImmovable);
}

// Slot 18 (0x00604D60): the argument's low byte set: SetNetworkFlag, else
// ClearNetworkFlag, of 0x400000, a tail jump in the game (its result).
uint32_t __fastcall SetImmovable(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetImmovable", logged);
    return Virtual<uint32_t>(self, static_cast<uint8_t>(value) ? slot::kSetNetworkFlag : slot::kClearNetworkFlag,
                             flag::kImmovable);
}

// Slot 19 (0x00604D80): HasNetworkFlag(0x10000000) through the slot.
uint32_t __fastcall IsUsable(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsUsable", logged);
    return Virtual<uint32_t>(self, slot::kHasNetworkFlag, flag::kUsable);
}

// Slot 20 (0x00604D90): as SetImmovable, with 0x10000000.
uint32_t __fastcall SetUsable(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetUsable", logged);
    return Virtual<uint32_t>(self, static_cast<uint8_t>(value) ? slot::kSetNetworkFlag : slot::kClearNetworkFlag,
                             flag::kUsable);
}

// Slot 21 (0x00604DB0): bit 0 of the occupant part's GetFlags (slot 0x84),
// in all of EAX.
uint32_t __fastcall IsPlaced(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsPlaced", logged);
    return Virtual<uint32_t>(self + field::kOccupant, 0x84) & 1u;
}

// Slot 22 (0x00619520): HasNetworkFlag with the flag from the table at
// 0x00AA7CE8 for the network type (no range test), a tail jump in the game
// (its result).
uint32_t __fastcall IsOfType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsOfType", logged);
    const uint32_t flags = reinterpret_cast<const uint32_t*>(addr::cSC4NetworkOccupant_TypeFlags)[type];
    return Virtual<uint32_t>(self, slot::kHasNetworkFlag, flags);
}

// Slot 55 (0x0061BD80): false when the piece lacks flag 0x40 (EAX as the
// HasNetworkFlag call left it). Else the piece ID (slot 23) is looked up
// in the map at 0x00B4528C (its find, 0x00497520, answers a pointer to the
// node it wrote) and the answer is whether that node is not the map's
// first word (the end node). The game leaves AL = the answer and the other
// bytes of EAX those of the node pointer, which is kept here.
uint32_t __fastcall IsIntersection(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsIntersection", logged);
    const uint32_t has = Virtual<uint32_t>(self, slot::kHasNetworkFlag, flag::kIntersectionCandidate);
    if (!static_cast<uint8_t>(has))
        return has;
    uint32_t key = Virtual<uint32_t>(self, slot::kPieceId);
    char* found = nullptr;
    void* const map = reinterpret_cast<void*>(addr::cSC4NetworkOccupant_PieceMap);
    char** const result = CallThis<char**, char**, const uint32_t*>(addr::SC4Uint32Map_Find, map, &found, &key);
    const std::uintptr_t node = reinterpret_cast<std::uintptr_t>(*result);
    const bool answer = *result != Global<char*>(addr::cSC4NetworkOccupant_PieceMap);
    return static_cast<uint32_t>((node & ~std::uintptr_t{0xFF}) | (answer ? 1u : 0u));
}

// Slot 60 (0x00619890): bit 29 of the flags. Touches neither ECX nor EDX.
uint32_t __fastcall IsLit(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsLit", logged);
    return (At<uint32_t>(self, field::kFlags) >> 0x1D) & 1u;
}

// Slot 73 (0x006199C0): bit 30 of the flags. Touches neither ECX nor EDX.
uint32_t __fastcall IsSupportPylonPresent(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsSupportPylonPresent", logged);
    return (At<uint32_t>(self, field::kFlags) >> 0x1E) & 1u;
}

// ====================================================================================
// simulators.netocc.edges: the edge store and the corner heights
// ====================================================================================

// Slot 39 (0x0061B430): the byte of edge `edge` in the type's value
// replaced (the shift is the edge x 8, taken mod 32 as the CPU does), the
// value stored. EAX is the store's.
uint32_t __fastcall SetEdgeType(char* self, void*, uint32_t type, uint32_t edge, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetEdgeType", logged);
    const uint32_t shift = (edge << 3) & 31u;
    uint32_t edges = StoreGet(self, type);
    edges &= ~(0xFFu << shift);
    edges |= static_cast<uint32_t>(static_cast<uint8_t>(value)) << shift;
    return StoreSet(self, type, edges);
}

// Slot 40 (0x0061AE90): the number of edges (bytes) with a connection (low
// three bits) in the ORed value of every entry.
uint32_t __fastcall GetActiveEdges(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetActiveEdges", logged);
    return CountActive(AllEdges(self));
}

// Slot 41 (0x0061AEE0): the same for one type's value.
uint32_t __fastcall GetActiveEdgesForType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetActiveEdgesForType", logged);
    return CountActive(StoreGet(self, type));
}

// Slot 42 (0x0061AF10): one type's value, the low two bits of each byte
// only.
uint32_t __fastcall GetActiveEdgesForTypeNoGroup(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetActiveEdgesForTypeNoGroup", logged);
    return CountActiveNoGroup(StoreGet(self, type));
}

// Slot 43 (0x0061AF40): the number of bytes with the grouped bit (0x04) in
// the ORed value of every entry.
uint32_t __fastcall GetGroupedEdges(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetGroupedEdges", logged);
    return ((AllEdges(self) & 0x04040404u) * 0x01020408u) >> 0x1A;
}

// Slot 34 (0x0065A6C0): the four corner heights (the floats' bits copied).
void __fastcall GetVertexHeights(char* self, void*, uint32_t* heights)
{
    static bool logged = false;
    FirstCall("GetVertexHeights", logged);
    for (std::size_t i = 0; i < 4; ++i)
        heights[i] = At<uint32_t>(self, field::kVertexHeights + i * field::kVertexStride);
}

// Slot 35 (0x006194C0): one corner's height in ST0, the index not checked.
// Touches neither ECX nor EDX.
double __fastcall GetVertexHeight(char* self, void*, uint32_t index)
{
    static bool logged = false;
    FirstCall("GetVertexHeight", logged);
    return static_cast<double>(At<float>(self, field::kVertexHeights + index * field::kVertexStride));
}

// Slot 36 (0x00619650): the four heights stored (bits copied).
void __fastcall SetVertexHeights(char* self, void*, const uint32_t* heights)
{
    static bool logged = false;
    FirstCall("SetVertexHeights", logged);
    for (std::size_t i = 0; i < 4; ++i)
        At<uint32_t>(self, field::kVertexHeights + i * field::kVertexStride) = heights[i];
}

// ====================================================================================
// simulators.netocc.pieces: rotation, variation, texture, path ID, cost
// ====================================================================================

// Slot 24 (0x00604DC0): the low seven bits of the byte. Touches neither ECX
// nor EDX.
uint32_t __fastcall GetRotation(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetRotation", logged);
    return At<uint8_t>(self, field::kRotationFlip) & 0x7Fu;
}

// Slot 25 (0x00604DD0): bit 7 of the byte. Touches neither ECX nor EDX.
uint32_t __fastcall GetFlip(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetFlip", logged);
    return static_cast<uint32_t>(At<uint8_t>(self, field::kRotationFlip)) >> 7;
}

// Slot 26 (0x00604DE0): the byte, in AL only (the rest of EAX is whatever
// it held). Touches neither ECX nor EDX.
uint8_t __fastcall GetRotationAndFlip(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetRotationAndFlip", logged);
    return At<uint8_t>(self, field::kRotationFlip);
}

// The message SetVariation and SetUnderTexture post: type 0x99EF1145 with
// the occupant part (object + 4) as the object and as data 1, data 2 = 1
// and data 3 = the network occupant type, queued (server slot 0x10).
void PostChanged(char* self)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, kMessageChanged);
    char* const part = self + field::kOccupant;
    Virtual<uint32_t>(message, 0x84, part);
    Virtual<uint32_t>(message, 0x2C, part);
    Virtual<uint32_t>(message, 0x40, 1u);
    Virtual<uint32_t>(message, 0x54, kOccupantType);
    Virtual<uint32_t>(server, 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// Slot 27 (0x0061B7F0): nothing when the byte is the same; else stored and
// the changed message posted.
void __fastcall SetVariation(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetVariation", logged);
    const uint8_t variation = static_cast<uint8_t>(value);
    if (variation == At<uint8_t>(self, field::kVariation))
        return;
    At<uint8_t>(self, field::kVariation) = variation;
    PostChanged(self);
}

// Slot 28 (0x00619880): the byte, in AL only. Touches neither ECX nor EDX.
uint8_t __fastcall GetVariation(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetVariation", logged);
    return At<uint8_t>(self, field::kVariation);
}

// Slot 29 (0x0061B880): as SetVariation, for the under texture's 32 bits.
void __fastcall SetUnderTexture(char* self, void*, uint32_t texture)
{
    static bool logged = false;
    FirstCall("SetUnderTexture", logged);
    if (texture == At<uint32_t>(self, field::kUnderTexture))
        return;
    At<uint32_t>(self, field::kUnderTexture) = texture;
    PostChanged(self);
}

// Slot 51 (0x006197D0): stored. Touches neither ECX nor EDX.
void __fastcall SetPathIDOverride(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetPathIDOverride", logged);
    At<uint32_t>(self, field::kPathIDOverride) = value;
}

// Slot 52 (0x006197E0). Touches neither ECX nor EDX.
uint32_t __fastcall GetPathIDOverride(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPathIDOverride", logged);
    return At<uint32_t>(self, field::kPathIDOverride);
}

// Slot 63 (0x00604E10): the 64 bits in EDX:EAX.
int64_t __fastcall GetCostOfDemolition(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetCostOfDemolition", logged);
    const uint64_t low = At<uint32_t>(self, field::kDemolitionCost);
    const uint64_t high = At<uint32_t>(self, field::kDemolitionCost + 4);
    return static_cast<int64_t>(high << 32 | low);
}

// Slot 64 (0x00604E20): the 64 bits as two arguments, low first.
void __fastcall SetCostOfDemolition(char* self, void*, uint32_t low, uint32_t high)
{
    static bool logged = false;
    FirstCall("SetCostOfDemolition", logged);
    At<uint32_t>(self, field::kDemolitionCost) = low;
    At<uint32_t>(self, field::kDemolitionCost + 4) = high;
}

// ====================================================================================
// simulators.netocc.properties: what the property holder keeps
// ====================================================================================

// Slot 6 (0x00619180): whether the retrofit property is there; 0 or 1 in
// all of EAX.
uint32_t __fastcall CheckForRetrofit(char* self, void*)
{
    static bool logged = false;
    FirstCall("CheckForRetrofit", logged);
    return PropertyOf(self, property::kRetrofit) != nullptr ? 1 : 0;
}

// Slot 7 (0x0061A120): with the argument's low byte clear the property is
// removed (RemoveProperty(id, 0)); else an empty script number (0x009166D4)
// is stored.
void __fastcall SetRetrofitFlag(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetRetrofitFlag", logged);
    if (!static_cast<uint8_t>(value)) {
        Virtual<uint32_t>(self + field::kHolder, holder::kRemoveProperty, property::kRetrofit, 0u);
        return;
    }
    StoreNumber(self, property::kRetrofit, NewNumber(addr::SC4ScriptNumber_Construct));
}

// Slot 13 (0x00619110): the property's two uint32 (variant slot 0x19C, a
// pointer to the array) into the two arguments; true, else false (AL only).
uint8_t __fastcall GetConnectionNodeEdgeTypes(char* self, void*, uint32_t* first, uint32_t* second)
{
    static bool logged = false;
    FirstCall("GetConnectionNodeEdgeTypes", logged);
    void* const found = PropertyOf(self, property::kNodeEdgeTypes);
    if (!found)
        return 0;
    const uint32_t* const values = ValueOf<uint32_t>(found, 0x19C);
    *first = values[0];
    *second = values[1];
    return 1;
}

// Slot 14 (0x0061A020): the two values in an array of two on the stack, a
// new script number (empty, 0x009166D4) given them through its slot 0x198
// (pointer, 2), then stored as the property. The number is released without
// a test, as in the game (a failed allocation crashes there as here).
void __fastcall SetConnectionNodeEdgeTypes(char* self, void*, uint32_t first, uint32_t second)
{
    static bool logged = false;
    FirstCall("SetConnectionNodeEdgeTypes", logged);
    uint32_t values[2] = {first, second};
    void* const number = NewNumber(addr::SC4ScriptNumber_Construct);
    if (number)
        Virtual<uint32_t>(number, 0x4);
    Virtual<uint32_t>(number, 0x198, static_cast<uint32_t*>(values), 2u);
    Virtual<uint32_t>(self + field::kHolder, holder::kSetProperty, property::kNodeEdgeTypes, number, 0u);
    Virtual<uint32_t>(number, 0x8);
}

// Slot 15 (0x00619150): the property's uint32 (variant slot 0x124), 0 for
// none.
uint32_t __fastcall GetConnectionPathIDOverride(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetConnectionPathIDOverride", logged);
    void* const found = PropertyOf(self, property::kPathIDOverride);
    if (!found)
        return 0;
    return *ValueOf<uint32_t>(found, 0x124);
}

// Slot 16 (0x0061A090): zero removes the property, else a script number
// from the value (0x00915D6D) is stored. Either way the path ID at +0x164
// is set, the interface's slot 50 (ReleasePathInfo, header name; no
// arguments) called and flag 0x2000 cleared through slot 11.
void __fastcall SetConnectionPathIDOverride(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetConnectionPathIDOverride", logged);
    if (value == 0)
        Virtual<uint32_t>(self + field::kHolder, holder::kRemoveProperty, property::kPathIDOverride, 0u);
    else
        StoreNumber(self, property::kPathIDOverride, NewNumber(addr::SC4ScriptNumber_FromUint, value));
    At<uint32_t>(self, field::kPathIDOverride) = value;
    Virtual<uint32_t>(self, slot::kReleasePathInfo);
    Virtual<uint32_t>(self, slot::kClearNetworkFlag, flag::kPathInfo);
}

// Slot 67 (0x00619900): the property's float (variant slot 0x144), its bits
// copied to the argument; true, else false (AL only).
uint8_t __fastcall GetPassageCeiling(char* self, void*, uint32_t* ceiling)
{
    static bool logged = false;
    FirstCall("GetPassageCeiling", logged);
    void* const found = PropertyOf(self, property::kPassageCeiling);
    if (!found)
        return 0;
    *ceiling = *ValueOf<uint32_t>(found, 0x144);
    return 1;
}

// Slot 68 (0x0061B9A0): a script number from the float (0x00915E0B; the
// argument's bits passed as they are) stored as the property.
void __fastcall SetPassageCeiling(char* self, void*, uint32_t ceiling)
{
    static bool logged = false;
    FirstCall("SetPassageCeiling", logged);
    StoreNumber(self, property::kPassageCeiling, NewNumber(addr::SC4ScriptNumber_FromFloat, ceiling));
}

// Slot 69 (0x00619940): the property's byte (variant slot 0x104), 0 for
// none (AL only).
uint8_t __fastcall GetOneWayDirections(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetOneWayDirections", logged);
    void* const found = PropertyOf(self, property::kOneWayDirections);
    if (!found)
        return 0;
    return *ValueOf<uint8_t>(found, 0x104);
}

// A script number of `constructor` on the stack (0x14 bytes) from `value`,
// given to an existing property through its slot 0x1C, then destroyed
// (0x00915EB5).
void AssignToProperty(void* found, std::uintptr_t constructor, uint32_t value)
{
    alignas(8) char number[0x14];
    void* const made = CallThis<void*, uint32_t>(constructor, number, value);
    Virtual<uint32_t>(found, 0x1C, made);
    CallThis<void>(addr::SC4ScriptNumber_Destroy, number);
}

// Slot 70 (0x0061B9F0): with the property there its value replaced (a
// temporary number from the byte given to its slot 0x1C). Else, for a
// non-zero low byte, a new script number from the byte (0x00915D1D) is
// stored; a zero byte does nothing. The argument is passed to the
// constructor as the full 32 bits pushed.
void __fastcall SetOneWayDirections(char* self, void*, uint32_t directions)
{
    static bool logged = false;
    FirstCall("SetOneWayDirections", logged);
    if (void* const found = PropertyOf(self, property::kOneWayDirections)) {
        AssignToProperty(found, addr::SC4ScriptNumber_FromUint8, directions);
        return;
    }
    if (!static_cast<uint8_t>(directions))
        return;
    StoreNumber(self, property::kOneWayDirections, NewNumber(addr::SC4ScriptNumber_FromUint8, directions));
}

// Slot 71 (0x00619970): the property's uint32 (variant slot 0x124) split:
// the low 16 bits to the first argument, the high 16 to the second; true,
// else false (AL only).
uint8_t __fastcall GetComplexPieceOrigin(char* self, void*, int32_t* x, int32_t* y)
{
    static bool logged = false;
    FirstCall("GetComplexPieceOrigin", logged);
    void* const found = PropertyOf(self, property::kComplexPieceOrigin);
    if (!found)
        return 0;
    const uint32_t packed = *ValueOf<uint32_t>(found, 0x124);
    *x = static_cast<int32_t>(packed & 0xFFFFu);
    *y = static_cast<int32_t>(packed >> 16);
    return 1;
}

// Slot 72 (0x0061BA90): (y << 16) + x as one number: given to the property
// when there, else stored as a new one (0x00915D6D).
void __fastcall SetComplexPieceOrigin(char* self, void*, uint32_t x, uint32_t y)
{
    static bool logged = false;
    FirstCall("SetComplexPieceOrigin", logged);
    const uint32_t packed = (y << 16) + x;
    if (void* const found = PropertyOf(self, property::kComplexPieceOrigin)) {
        AssignToProperty(found, addr::SC4ScriptNumber_FromUint, packed);
        return;
    }
    StoreNumber(self, property::kComplexPieceOrigin, NewNumber(addr::SC4ScriptNumber_FromUint, packed));
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_NETOCC_SKIP lists function
// addresses (hex, eight digits).
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_NETOCC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

// A detour at each address (entry bytes checked by hook::Install). The
// originals are not called again.
template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("netocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    unsigned installed = 0;
    for (const Detour& detour : detours) {
        if (Skipped(detour.address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
        ++installed;
    }
    log::Write("netocc %s: %u detours", what, installed);
    return true;
}

const Detour kFlags[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_HasNetworkFlag, HasNetworkFlag),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_HasAnyNetworkFlag, HasAnyNetworkFlag),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetNetworkFlag, SetNetworkFlag),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_ClearNetworkFlag, ClearNetworkFlag),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_IsImmovable, IsImmovable),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetImmovable, SetImmovable),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_IsUsable, IsUsable),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetUsable, SetUsable),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_IsPlaced, IsPlaced),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_IsOfType, IsOfType),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_IsIntersection, IsIntersection),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_IsLit, IsLit),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_IsSupportPylonPresent, IsSupportPylonPresent),
};

const Detour kEdges[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetEdgeType, SetEdgeType),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetActiveEdges, GetActiveEdges),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetActiveEdgesForType, GetActiveEdgesForType),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetActiveEdgesForTypeNoGroup, GetActiveEdgesForTypeNoGroup),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetGroupedEdges, GetGroupedEdges),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetVertexHeights, GetVertexHeights),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetVertexHeight, GetVertexHeight),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetVertexHeights, SetVertexHeights),
};

const Detour kPieces[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetRotation, GetRotation),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetFlip, GetFlip),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetRotationAndFlip, GetRotationAndFlip),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetVariation, SetVariation),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetVariation, GetVariation),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetUnderTexture, SetUnderTexture),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetPathIDOverride, SetPathIDOverride),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetPathIDOverride, GetPathIDOverride),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetCostOfDemolition, GetCostOfDemolition),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetCostOfDemolition, SetCostOfDemolition),
};

const Detour kProperties[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_CheckForRetrofit, CheckForRetrofit),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetRetrofitFlag, SetRetrofitFlag),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetConnectionNodeEdgeTypes, GetConnectionNodeEdgeTypes),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetConnectionNodeEdgeTypes, SetConnectionNodeEdgeTypes),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetConnectionPathIDOverride, GetConnectionPathIDOverride),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetConnectionPathIDOverride, SetConnectionPathIDOverride),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetPassageCeiling, GetPassageCeiling),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetPassageCeiling, SetPassageCeiling),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetOneWayDirections, GetOneWayDirections),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetOneWayDirections, SetOneWayDirections),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetComplexPieceOrigin, GetComplexPieceOrigin),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetComplexPieceOrigin, SetComplexPieceOrigin),
};

bool InstallFlags(registry::Mode) { return InstallDetours(kFlags, "flags"); }
bool InstallEdges(registry::Mode) { return InstallDetours(kEdges, "edges"); }
bool InstallPieces(registry::Mode) { return InstallDetours(kPieces, "pieces"); }
bool InstallProperties(registry::Mode) { return InstallDetours(kProperties, "properties"); }

const bool g_registered[] = {
    registry::Register({"simulators.netocc.flags", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_SetNetworkFlag,
                        "Our cSC4NetworkOccupant network flags and what they answer: has, set, clear, immovable, "
                        "usable, placed, type, intersection, lit, pylon (13 detours; src/core/netocc_sim.h)",
                        false, InstallFlags}),
    registry::Register({"simulators.netocc.edges", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_GetActiveEdges,
                        "Our cSC4NetworkOccupant edge store queries and SetEdgeType, and the corner heights "
                        "(8 detours; src/core/netocc_sim.h)",
                        false, InstallEdges}),
    registry::Register({"simulators.netocc.pieces", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_GetRotation,
                        "Our cSC4NetworkOccupant rotation, flip, variation, under texture, path ID and demolition "
                        "cost (10 detours; src/core/netocc_sim.h)",
                        false, InstallPieces}),
    registry::Register({"simulators.netocc.properties", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_CheckForRetrofit,
                        "Our cSC4NetworkOccupant properties in the holder: retrofit, connection node edge types, "
                        "path ID override, passage ceiling, one-way directions, complex piece origin "
                        "(12 detours; src/core/netocc_sim.h)",
                        false, InstallProperties}),
};

} // namespace
} // namespace opensc4::netocc_sim
