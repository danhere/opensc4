// Our cSC4Demolition, the large functions (build 641, class ID 0x0990C38A,
// main vtable 0x00A88004, code 0x00467BB0 to 0x004695A0): ModifyTerrainHeight
// (slot 9), DemolishRegion (slot 6), DemolishOccupant (slot 8) and the
// helper 0x004681A0 they share. The small half of the class (object,
// Init, Shutdown, tuning, DemolishLots, the effects, the helpers below
// 0x00467BB0) is the other part's; its functions are called here by address
// as the original calls them.
//
// The object (fields used here; the other part writes the full map):
//   +0x10 uint8   initialised
//   +0x14 int32   the city's width in cells, +0x18 its depth
//   +0x1C         the default occupant filter (0x34 bytes, made by Init):
//                   +0x24 uint8  a test is active
//                   +0x28 int32  the privilege mode (1, or 2 with a privilege)
//                   +0x2C        the filter the caller passed (referenced)
//                   +0x30        the view's own filter (referenced)
//   +0x20         the city
//   +0x24         the lot developer (slot 12 StartDemolishLot, 13
//                 EndDemolishLot, 14 ExplodeLot)
//   +0x28         the occupant manager (global 0x00B43D0C, whose address
//                 entry is called SC4::NetworkService by an old mistake)
//   +0x2C         the terrain
//   +0x30         the region of the call in progress (DemolishRegion sets
//                 and clears it)
//
// DemolishRegion(demolish, region, privilege, flags, clearZoned, filter,
//                cost, demolished, occupant, a, b), ret 0x2C, 11 arguments:
//   demolish    byte: false only prices and collects (the bulldoze tool's
//               preview), true removes
//   region      SC4CellRegion: the rectangle (x1, z1, x2, z2), then width
//               +0x14, depth +0x18 and at +0x20 the rows of bits (one row
//               per x from x1, a bit per z from z1)
//   privilege   0 none; 1 the player's own tool (a building that cannot be
//               removed fails the call); anything else as 2 in the filter
//   flags       0x2000: the lots' other occupants only when they belong to
//               the lot; 0x300 (either bit): the lot developer's StartDemolishLot
//               is used only for lots whose configuration allows it;
//               0x1000: handed to ExplodeLot as its second argument
//               inverted; the low byte non-zero: show the effects (the
//               calls go through DoDemolishOccupantEffect and
//               DoDemolishGroupEffect); 0x100 is also tested by
//               DemolishOccupant
//   clearZoned  byte, handed to 0x004677F0
//   filter      cISC4OccupantFilter or null: kept in the default filter's
//               +0x2C for the call
//   cost        int64 out or null
//   demolished  a set of occupants (SC4LotPtrSet layout) that receives
//               every occupant the call took, or null
//   occupant    the occupant the bulldoze tool is pointing at (compared
//               with each one for DoDemolishOccupantEffect's fourth
//               argument)
//   a, b        the effect functions' last two arguments
// Returns false when a building that is not allowed to go stood in the
// area (privilege 1), true otherwise.
//
// What it does, in the order of the original:
//   1. the call's setup: the region kept at +0x30, the view's filter (the
//      service at global 0x00B43DC4, slot 18 gives it), the default filter's
//      fields, a local default filter (vtables 0x00A87FC0 and 0x00A87FA8),
//      the set of affected occupants, the list of lots to finish and the
//      set of lots seen;
//   2. every set cell of the region, x outer and z inner: the lot there
//      (the lot manager's slot 6), once per lot: when its building passes
//      the default filter (or the lot is empty of type 6 or 1 and the
//      view's filter takes buildings) the lot's occupants are all tested,
//      and when every building and prop passes the lot is queued and its
//      related occupants (0x00467AA0) join the set; otherwise the call
//      fails; then the occupants of the cell (0x00466B90) join the set
//      when the local filter takes them;
//   3. every occupant of the set goes through DemolishOccupant (twice when
//      the effects are shown: asked first, then done), its price added;
//   4. the group effect, the network manager told to refresh;
//   5. every queued lot: type 6 lots add width * depth * tuning
//      (0x00B07F50) to the price, and the lot developer finishes them (slots
//      12 and 13 and 0x004677F0, or ExplodeLot);
//   6. the occupants DemolishOccupant listed join the set (and the caller's
//      set), the set goes to 0x004681A0, the cost is stored, the default
//      filter is cleaned and everything is destroyed.
//
// DemolishOccupant(demolish, occupant, privilege, flags, cost, excludeNetworks,
//                  taken), ret 0x1C: one occupant by type (power pole, building,
//                  construction, foundation, network piece, prop, flora); the
//                  price goes to `cost` (zero for an unknown type), the occupants
//                  of a removed power pole to the list `taken`.
//
// ModifyTerrainHeight(apply, perCell, x1, z1, x2, z2, height, lot, why, cost),
//                  ret 0x28 (the tenth argument is not read): sets the terrain
//                  of a cell rectangle to `height` unless a lot with locked
//                  vertices other than `lot` is nearby, or (perCell) an
//                  occupant is on the cell. Returns the count of cells that
//                  differ.
//
// Every virtual call has its argument count from the callee's own `ret N`
// (the lot manager 0x00AB37C0, the lot 0x00AB19B0, the terrain 0x00AB3D18,
// the lot developer 0x00AB1B98, the city 0x00A87620, the building
// development simulator 0x00AB0AF8, the occupant manager 0x00A8F7F0, the
// occupant 0x00AA41D8, the filter 0x00AA42C8); the others (the view's
// service at 0x00B43DC4, the prop manager at 0x00B43D10, the power pole
// and network piece interfaces, the lot configuration 0x00A8CEB8, the
// message server, the message, the month-end service at 0x00B43D8C) are
// checked by the original's stack balance only.
#include "core/address_check.h"
#include "core/demolition.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::demolition {

// The region entry as the game's callers see it. Defined below; external so
// that RegionEntry (demolition.h) can hand its address to a wrapper.
uint8_t __fastcall DemolishRegionOurs(char* self, void* edx, uint32_t demolish, int32_t* region, int32_t privilege,
                                      uint32_t flags, uint32_t clearZoned, void* filter, int64_t* cost,
                                      void* demolished, void* occupant, int32_t a, int32_t b);

namespace {

namespace field {
constexpr std::size_t kWidth = 0x14;
constexpr std::size_t kDepth = 0x18;
constexpr std::size_t kFilter = 0x1C;
constexpr std::size_t kCity = 0x20;
constexpr std::size_t kLotDeveloper = 0x24;
constexpr std::size_t kOccupants = 0x28;
constexpr std::size_t kTerrain = 0x2C;
constexpr std::size_t kRegion = 0x30;
} // namespace field

namespace filter {
constexpr std::size_t kActive = 0x24;
constexpr std::size_t kMode = 0x28;
constexpr std::size_t kPassed = 0x2C;
constexpr std::size_t kView = 0x30;
} // namespace filter

constexpr uint32_t kTypeFoundation = 0x68FD0C69;
constexpr uint32_t kTypeConstruction = 0x096E763C;
constexpr uint32_t kTypeBuilding = 0x278128A0;
constexpr uint32_t kTypePowerPole = 0x2890D4DE;
constexpr uint32_t kTypeFlora = 0x74758926;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kTypeNetwork = 0xC772BF98;
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kIidPowerPole = 0xA8C86142;
constexpr uint32_t kIidNetworkPiece = 0xA821EF94;
constexpr uint32_t kMessagePoleRemoved = 0xAA7A014D;

// The other part's functions and data, by address.
constexpr std::uintptr_t kNotifyLists = 0x00466B90;      // (this, rect, list), ret 8: the occupants of a rectangle
constexpr std::uintptr_t kPostMessage = 0x00466C50;      // (this, id, occupant, 0), ret 0xC
constexpr std::uintptr_t kEraseOccupants = 0x004676A0;   // (set, root), ret 4: erases a set of referenced occupants
constexpr std::uintptr_t kEraseLots = 0x004676E0;        // (set, root), ret 4: erases a set of lots
constexpr std::uintptr_t kDestroyOccupantSet = 0x00467A50; // (set)
constexpr std::uintptr_t kRelatedOccupants = 0x00467AA0; // (this, lot, list), ret 8
constexpr std::uintptr_t kInsertOccupant = 0x00467B80;   // (set, result, value), ret 8
constexpr std::uintptr_t kFilterTest = 0x005E9630;       // (filter, occupant), ret 4
constexpr std::uintptr_t kLotCostScale = 0x00B07F50;     // uint32 tuning: the price of a cell of a type 6 lot

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using CdeclFn = void*(__cdecl*)();
using InsertFn = void*(__thiscall*)(void* set, void* result, void* const* value);
using SetEraseFn = void(__thiscall*)(void* set, void* root);
using ClearFn = void(__thiscall*)(void* list);
using SelfFn = void(__thiscall*)(void* self);
using RectListFn = void(__thiscall*)(void* self, int32_t* rect, void* list);
using LotListFn = void(__thiscall*)(void* self, void* lot, void* list);
using PostFn = void(__thiscall*)(void* self, uint32_t id, void* occupant, uint32_t zero);
using FilterTestFn = uint8_t(__thiscall*)(void* filter, void* occupant);
using HolderFn = void*(__thiscall*)(void* holder, void* object);
using PushFn = void(__thiscall*)(void* container, void* element);
using SetNewFn = void(__thiscall*)(void* set);
using NewObjectFn = void*(__cdecl*)(std::size_t size);
using MessageFn = void*(__thiscall*)(void* message);

const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto ClearList = reinterpret_cast<ClearFn>(addr::SC4List_Clear2);
const auto GetNetworkManager = reinterpret_cast<CdeclFn>(addr::SC4_GetNetworkManager);
const auto InsertUnique = reinterpret_cast<InsertFn>(addr::SC4LotPtrSet_InsertUnique);

inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("simulator: first %s", name);
}

// The game's std::set header: the head node and the count. The head node is
// 0x14 bytes (a colour byte, parent +4, leftmost +8, rightmost +0xC), a
// node's key at +0x10.
struct TreeSet {
    char* head;
    uint32_t size;
};

// InsertUnique's answer: the node and whether it was new. The game's callers
// give 8 bytes; 12 here so a longer write cannot reach the next local.
struct InsertResult {
    void* node;
    uint8_t inserted;
    uint8_t pad[7];
};

// The tree's next node, as the game walks it after a set's last visit.
char* Next(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

void NewSet(TreeSet& set)
{
    set.head = static_cast<char*>(Allocate(0x14));
    set.size = 0;
    At<uint8_t>(set.head, 0x0) = 0;
    At<void*>(set.head, 0x4) = nullptr;
    At<char*>(set.head, 0x8) = set.head;
    At<char*>(set.head, 0xC) = set.head;
}

// Erases a set the way the game does: the tree below the root, the head
// emptied, then the head freed.
void DestroySet(TreeSet& set, std::uintptr_t erase)
{
    if (set.size) {
        reinterpret_cast<SetEraseFn>(erase)(&set, At<void*>(set.head, 0x4));
        At<char*>(set.head, 0x8) = set.head;
        At<void*>(set.head, 0x4) = nullptr;
        At<char*>(set.head, 0xC) = set.head;
        set.size = 0;
    }
    if (set.head)
        Free(set.head);
}

// A list header (next, previous) from the game's list node allocator.
char* NewList()
{
    char* const head = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    return head;
}

void FreeListHead(char* head) { Virtual<void>(ListNodeAllocator(), 0x10, head); }

// The occupant manager's list release, the list's own clear, and the head
// freed: how the game ends a list of referenced occupants.
void DestroyOccupantList(char* self, char*& list)
{
    Virtual<void>(At<void*>(self, field::kOccupants), 0xC0, &list);
    ClearList(&list);
    FreeListHead(list);
}

// A smart-pointer assignment into `object`'s field: the new value referenced
// first, the old one released after.
void AssignRef(char* object, std::size_t offset, void* value)
{
    void* const old = At<void*>(object, offset);
    if (value == old)
        return;
    if (value)
        AddRefOf(value);
    At<void*>(object, offset) = value;
    if (old)
        ReleaseOf(old);
}

// One reference held on `occupant` for the insert, as the game does it.
bool InsertAffected(TreeSet& set, void* occupant)
{
    if (occupant)
        AddRefOf(occupant);
    void* held = occupant;
    InsertResult result{};
    InsertUnique(&set, &result, &held);
    if (occupant)
        ReleaseOf(occupant);
    return result.inserted != 0;
}

bool ListContains(char* list, void* occupant)
{
    for (char* at = At<char*>(list, 0x0); at != list; at = At<char*>(at, 0x0))
        if (At<void*>(at, 0x8) == occupant)
            return true;
    return false;
}

// The 0x004677F0 call: the lot in ECX, the byte in AL and two stack
// arguments the caller pops.
__declspec(naked) void LotRegionsThunk()
{
    __asm movzx eax, byte ptr [esp + 16]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm mov ecx, dword ptr [esp + 12]
    __asm mov edx, 0x004677F0
    __asm call edx
    __asm add esp, 8
    __asm ret
}

void CallLotRegions(void* lot, uint32_t flags, uint32_t zone, uint32_t clearZoned)
{
    // (lot, flags, zone, clearZoned): the thunk's stack arguments, cdecl.
    reinterpret_cast<void(__cdecl*)(void*, uint32_t, uint32_t, uint32_t)>(&LotRegionsThunk)(lot, flags, zone,
                                                                                            clearZoned);
}

// ====================================================================================
// 0x004681A0: the occupants a removal took, told to the network manager and
// the power poles announced
// ====================================================================================

// For each occupant of the set with flag 1: a power pole sends the message
// 0xAA7A014D and is taken off the occupant manager (slot 21 with true and
// 0); a network piece joins a list (referenced) that the network manager's
// slot 6 and the occupant manager's list release then take; the list is
// destroyed. `set` is the set object (head, count), not a pointer to its head.
void NotifyRemovedOccupants(char* self, TreeSet* set)
{
    static bool logged = false;
    FirstCall("NotifyRemovedOccupants", logged);
    char* pieces = NewList();
    char* const end = set->head;
    for (char* node = At<char*>(end, 0x8); node != end; node = Next(node)) {
        void* const occupant = At<void*>(node, 0x10);
        if (!Virtual<uint8_t>(occupant, 0x80, uint32_t{1}))
            continue;
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        if (type == kTypePowerPole) {
            void* const server = Global<void*>(addr::SC4_MessageServer);
            void* const raw = reinterpret_cast<NewObjectFn>(addr::RZ_operator_new)(0x2C);
            void* const message =
                raw ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(raw) : nullptr;
            Virtual<uint32_t>(message, 0x4);
            Virtual<void>(message, 0x14, kMessagePoleRemoved);
            Virtual<void>(message, 0x30, occupant);
            Virtual<void>(message, 0x84, occupant);
            Virtual<void>(server, 0xC, message);
            Virtual<uint32_t>(message, 0x8);
            Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, uint32_t{1}, uint32_t{0});
        } else if (type == kTypeNetwork) {
            // push_back of a node holding the occupant, then its reference.
            char* const back = At<char*>(pieces, 0x4);
            char* const link = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
            At<void*>(link, 0x8) = occupant;
            At<char*>(link, 0x4) = back;
            At<char*>(link, 0x0) = pieces;
            At<char*>(back, 0x0) = link;
            At<char*>(pieces, 0x4) = link;
            AddRefOf(occupant);
            Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x20, occupant);
        }
    }
    if (At<char*>(pieces, 0x0) != pieces) {
        Virtual<void>(GetNetworkManager(), 0x18, &pieces);
        Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &pieces);
    }
    ClearList(&pieces);
    FreeListHead(pieces);
}

// ====================================================================================
// DemolishOccupant, slot 8 (0x004691F0)
// ====================================================================================

// A vector of three pointers (SC4RefVector: begin, end, capacity).
struct RefVector {
    void** begin;
    void** end;
    void** capacity;
};

uint8_t __fastcall DemolishOccupantOurs(char* self, void*, uint32_t demolishArg, void* occupant, int32_t privilege,
                                        uint32_t flags, int64_t* costOut, uint32_t excludeArg, void* taken)
{
    static bool logged = false;
    FirstCall("DemolishOccupant", logged);
    (void)privilege;
    const bool demolish = (demolishArg & 0xFF) != 0;
    int64_t cost = 0;
    bool done = false;
    void* const occupants = At<void*>(self, field::kOccupants);

    // An occupant with flag 0x40 is refused when flag 0x100 is asked; no
    // cost is written then.
    if (Virtual<uint8_t>(occupant, 0x80, uint32_t{0x40}) && (flags & 0x100))
        return 0;

    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kTypePowerPole) {
        void* pole = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidPowerPole, &pole)) {
            cost = Virtual<int64_t>(pole, 0x28);
            done = true;
            if (demolish) {
                // The pole's connected poles, each held by a reference.
                RefVector connected{};
                const int32_t count = Virtual<int32_t>(pole, 0x10);
                for (int32_t i = 0; i < count; ++i) {
                    void* const other = Virtual<void*>(pole, 0x1C, i);
                    void* holder = nullptr;
                    reinterpret_cast<HolderFn>(addr::SC4RefHolder_SC4RefHolder)(&holder, other);
                    reinterpret_cast<PushFn>(addr::SC4RefVector_PushBack)(&connected, &holder);
                    if (holder)
                        ReleaseOf(holder);
                }
                reinterpret_cast<PostFn>(kPostMessage)(self, kMessagePoleRemoved, occupant, 0);
                done = Virtual<uint8_t>(occupants, 0x54, occupant, uint32_t{0}, uint32_t{0}) != 0;
                if (done) {
                    Virtual<void>(pole, 0x18, uint32_t{1});
                    Virtual<void>(occupant, 0x10);
                    for (int32_t i = 0; i < count; ++i) {
                        void* const other = connected.begin[i];
                        if (!Virtual<uint32_t>(other, 0x10)) {
                            void* found = nullptr;
                            if (Virtual<uint8_t>(other, 0x0, kIidOccupant, &found)) {
                                if (taken)
                                    reinterpret_cast<PushFn>(addr::SC4List_PushBack)(taken, &found);
                                else
                                    ReleaseOf(found);
                            }
                        }
                    }
                }
                reinterpret_cast<SelfFn>(addr::SC4RefVector_Destroy)(&connected);
            }
            ReleaseOf(pole);
        }
    } else if (type == kTypeBuilding) {
        void* const developer = Virtual<void*>(At<void*>(self, field::kCity), 0xD4);
        if (developer) {
            // The building's address is the occupant's less 4.
            cost = Virtual<int64_t>(developer, 0x28, static_cast<char*>(occupant) - 4, uint32_t{1});
            done = true;
        }
    } else if (type == kTypeConstruction) {
        done = true;
        if (demolish)
            done = Virtual<uint8_t>(occupants, 0x54, occupant, uint32_t{1}, uint32_t{0}) != 0;
    } else if (type == kTypeFoundation) {
        done = true;
        if (demolish && Virtual<uint8_t>(occupant, 0x80, uint32_t{1}))
            done = Virtual<uint8_t>(occupants, 0x54, occupant, uint32_t{1}, uint32_t{0}) != 0;
    } else if (type == kTypeNetwork) {
        void* piece = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkPiece, &piece)) {
            cost = Virtual<int64_t>(piece, 0xFC);
            done = true;
            if (demolish && !(excludeArg & 0xFF)) {
                // Bit 0 of the occupant's flags: it goes through the
                // network notification as a one-element set.
                if (Virtual<uint32_t>(occupant, 0x84) & 1u) {
                    TreeSet one{};
                    reinterpret_cast<SetNewFn>(addr::SC4Uint32Set_Construct)(&one);
                    void* holder = nullptr;
                    reinterpret_cast<HolderFn>(addr::SC4RefHolder_SC4RefHolder)(&holder, occupant);
                    InsertResult result{};
                    reinterpret_cast<InsertFn>(kInsertOccupant)(&one, &result, &holder);
                    if (holder)
                        ReleaseOf(holder);
                    NotifyRemovedOccupants(self, &one);
                    reinterpret_cast<SelfFn>(kDestroyOccupantSet)(&one);
                }
            }
            ReleaseOf(piece);
        }
    } else if (type == kTypeProp) {
        if (void* const props = Global<void*>(addr::SC4_Service43D10)) {
            done = true;
            if (demolish) {
                done = Virtual<uint8_t>(props, 0x74, occupant) != 0;
                if (done)
                    Virtual<uint32_t>(occupant, 0x10);
            }
        }
    } else if (type == kTypeFlora) {
        if (void* const flora = Virtual<void*>(At<void*>(self, field::kCity), 0xEC)) {
            // The pointer goes through "less 4, plus 4 unless zero", so an
            // occupant at the address 4 becomes null.
            char* const less = static_cast<char*>(occupant) - 4;
            void* const again = less ? less + 4 : nullptr;
            cost = Virtual<int64_t>(flora, 0x58, again, uint32_t{0});
            done = true;
            if (demolish)
                Virtual<uint8_t>(occupants, 0x54, again, uint32_t{1}, uint32_t{0});
        }
    }
    if (costOut)
        *costOut = cost;
    return done ? 1 : 0;
}

// ====================================================================================
// ModifyTerrainHeight, slot 9 (0x00467BB0)
// ====================================================================================

constexpr std::uintptr_t kFilterVtableMain = 0x00A88050;
constexpr std::uintptr_t kFilterVtableSecond = 0x00A88038;

int32_t __fastcall ModifyTerrainHeightOurs(char* self, void*, uint32_t applyArg, uint32_t perCellArg, int32_t x1,
                                           int32_t z1, int32_t x2, int32_t z2, uint32_t heightBits, void* ignoreLot,
                                           uint32_t why, int64_t*)
{
    static bool logged = false;
    FirstCall("ModifyTerrainHeight", logged);
    const bool apply = (applyArg & 0xFF) != 0;
    const bool perCell = (perCellArg & 0xFF) != 0;
    float height;
    __builtin_memcpy(&height, &heightBits, sizeof height);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const terrain = At<void*>(self, field::kTerrain);
    const double margin = Global<float>(addr::cSC4Demolition_TerrainBoxMargin);
    const double epsilon = Global<float>(addr::SC4_FloatTwentieth);

    // The rectangle one cell more at the low side, clamped to the city.
    int32_t rect[4] = {x1 - 1, z1 - 1, x2, z2};
    if (rect[0] < 0)
        rect[0] = 0;
    if (rect[1] < 0)
        rect[1] = 0;
    const int32_t width = At<int32_t>(self, field::kWidth);
    const int32_t depth = At<int32_t>(self, field::kDepth);
    if (x2 >= width)
        rect[2] = width - 1;
    if (z2 >= depth)
        rect[3] = depth - 1;

    int32_t changed = 0;
    char* found = NewList();
    // The occupants in the rectangle that a terrain change would meet: a
    // filter object of the class with vtables 0x00A88050 and 0x00A88038, one
    // reference held while the manager reads it.
    void* const raw = OperatorNew(0xC);
    char* made = nullptr;
    if (raw) {
        reinterpret_cast<SelfFn>(addr::SC4_OccupantFilterBase)(raw);
        At<uint32_t>(raw, 0x0) = static_cast<uint32_t>(kFilterVtableMain);
        At<uint32_t>(raw, 0x4) = static_cast<uint32_t>(kFilterVtableSecond);
        made = static_cast<char*>(raw);
    }
    AddRefOf(made);
    Virtual<void>(At<void*>(self, field::kOccupants), 0xA0, &found, &rect[0], &rect[2], made,
                  perCell ? int32_t{-1} : int32_t{1});
    ReleaseOf(made);

    if (perCell) {
        for (int32_t x = x1; x <= x2; ++x) {
            const int32_t boxX = (x + 1) * 16;
            for (int32_t z = z1; z <= z2; ++z) {
                const int32_t boxZ = (z + 1) * 16;
                bool clear = true;
                if (Virtual<uint8_t>(terrain, 0x94, x, z))
                    continue;
                const double step = std::fabs(static_cast<double>(Virtual<float>(terrain, 0x38, x, z)) - height);
                if (step < epsilon)
                    continue;
                // The lots around the cell: one (other than the caller's)
                // with its vertices locked stops it.
                char* nearby = static_cast<char*>(Allocate(0xC));
                At<char*>(nearby, 0x0) = nearby;
                At<char*>(nearby, 0x4) = nearby;
                if (Virtual<uint32_t>(lots, 0x1C, &nearby, x - 1, z - 1, x, z, uint32_t{0})) {
                    for (char* at = At<char*>(nearby, 0x0); at != nearby; at = At<char*>(at, 0x0)) {
                        void* const lot = At<void*>(at, 0x8);
                        if (clear && lot != ignoreLot && Virtual<uint8_t>(lot, 0x110))
                            clear = false;
                    }
                }
                for (char* at = At<char*>(nearby, 0x0); at != nearby;) {
                    char* const following = At<char*>(at, 0x0);
                    if (void* const lot = At<void*>(at, 0x8))
                        ReleaseOf(lot);
                    Free(at);
                    at = following;
                }
                At<char*>(nearby, 0x0) = nearby;
                At<char*>(nearby, 0x4) = nearby;
                Free(nearby);
                if (!clear)
                    continue;
                // The cell's box, 1.5 m inside the 32 m square around it.
                const float lowX = static_cast<float>(static_cast<double>(boxX - 32) + margin);
                const float lowZ = static_cast<float>(static_cast<double>(boxZ - 32) + margin);
                const float highX = static_cast<float>(static_cast<double>(boxX) - margin);
                const float highZ = static_cast<float>(static_cast<double>(boxZ) - margin);
                bool blocked = false;
                for (char* at = At<char*>(found, 0x0); at != found; at = At<char*>(at, 0x0)) {
                    float low[3];
                    float high[3];
                    Virtual<void>(At<void*>(at, 0x8), 0x28, low, high);
                    // The same tests in the same order, each a "less than"
                    // from the flags: a NaN passes the first three and
                    // blocks at the last.
                    if (static_cast<double>(highX) < static_cast<double>(low[0]))
                        continue;
                    if (static_cast<double>(lowX) > static_cast<double>(high[0]))
                        continue;
                    if (static_cast<double>(highZ) < static_cast<double>(low[2]))
                        continue;
                    if (static_cast<double>(lowZ) > static_cast<double>(high[2]))
                        continue;
                    blocked = true;
                    break;
                }
                if (blocked)
                    continue;
                ++changed;
                if (apply)
                    Virtual<void>(terrain, 0xCC, x, z, heightBits);
            }
        }
        if (changed) {
            rect[2] += 1;
            rect[3] += 1;
            Virtual<void>(terrain, 0x110, uint32_t{1}, uint32_t{1}, &rect[0], why);
        }
    } else if (At<char*>(found, 0x0) == found) {
        // The empty list: count the cells that differ.
        bool clear = true;
        for (int32_t x = x1; x <= x2; ++x)
            for (int32_t z = z1; z <= z2; ++z) {
                const double step = std::fabs(static_cast<double>(Virtual<float>(terrain, 0x38, x, z)) - height);
                // Counted when strictly more than the margin; NaN not.
                if (step > epsilon)
                    ++changed;
            }
        if (changed) {
            char* nearby = static_cast<char*>(Allocate(0xC));
            At<char*>(nearby, 0x0) = nearby;
            At<char*>(nearby, 0x4) = nearby;
            if (Virtual<uint32_t>(lots, 0x1C, &nearby, rect[0], rect[1], rect[2], rect[3], uint32_t{0})) {
                for (char* at = At<char*>(nearby, 0x0); at != nearby; at = At<char*>(at, 0x0)) {
                    void* const lot = At<void*>(at, 0x8);
                    if (clear && lot != ignoreLot && Virtual<uint8_t>(lot, 0x110))
                        clear = false;
                }
            }
            reinterpret_cast<ClearFn>(addr::SC4RefList_Clear)(&nearby);
            if (nearby)
                Free(nearby);
            if (clear && apply) {
                int32_t area[4] = {x1, z1, x2, z2};
                if (area[0] < 0)
                    area[0] = 0;
                if (area[1] < 0)
                    area[1] = 0;
                if (area[2] > width)
                    area[2] = width;
                if (area[3] > depth)
                    area[3] = depth;
                Virtual<void>(terrain, 0xC4, &area[0], heightBits, uint32_t{0}, why);
                Virtual<void>(terrain, 0x110, uint32_t{1}, uint32_t{1}, &area[0], why);
            }
        }
    }
    DestroyOccupantList(self, found);
    return changed;
}

// ====================================================================================
// DemolishRegion, slot 6 (0x00468410)
// ====================================================================================

// What DemolishRegion keeps between its phases (the original's stack frame).
struct Run {
    char* self;
    int32_t* region;
    int32_t privilege;
    uint32_t flags;
    void* view;          // the view's filter, referenced (or null)
    char* defaultFilter; // self + 0x1C
    bool keepView;       // privilege 0 or flags 0x2000: the view's filter is not put in the default filter
    bool result;
    uint32_t count;      // occupants newly in the set
    TreeSet affected;    // set of occupants
    char* queued;        // list of lots to finish (referenced), new ones in front
    TreeSet seen;        // set of lots met
    void* lotManager;
    uint8_t local[0xC];  // the local default filter object
};

bool RegionBit(const int32_t* region, int32_t x, int32_t z)
{
    const uint32_t i = static_cast<uint32_t>(x - region[0]);
    const uint32_t j = static_cast<uint32_t>(z - region[1]);
    if (i >= static_cast<uint32_t>(region[5]) || j >= static_cast<uint32_t>(region[6]))
        return false;
    uint32_t** const rows = reinterpret_cast<uint32_t**>(static_cast<std::uintptr_t>(region[8]));
    return (rows[i][j >> 5] & (1u << (j & 31))) != 0;
}

// 0x0046897A to 0x00468C52: a lot whose building the filter accepts. The
// lot's occupants are listed; the view's filter is taken off the default
// filter while they are tested; when every building and prop passes, the lot
// is queued and the occupants related to it join the set.
void TakeLot(Run& run, void* lot)
{
    char* const self = run.self;
    char* const filterObject = run.defaultFilter;
    char* occupants = NewList();
    Virtual<void>(lot, 0xC4, &occupants);
    bool everything = true;
    if (void* const view = At<void*>(filterObject, filter::kView)) {
        At<void*>(filterObject, filter::kView) = nullptr;
        ReleaseOf(view);
    }
    for (char* at = At<char*>(occupants, 0x0); at != occupants; at = At<char*>(at, 0x0)) {
        void* const occupant = At<void*>(at, 0x8);
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        if (type == kTypeBuilding || type == kTypeProp)
            if (!Virtual<uint8_t>(filterObject, 0xC, occupant)) {
                everything = false;
                break;
            }
    }
    if (!run.keepView)
        AssignRef(filterObject, filter::kView, run.view);
    if (everything) {
        // The lot pushed in front of the queue, one reference for the node.
        AddRefOf(lot);
        char* const first = At<char*>(run.queued, 0x0);
        char* const node = static_cast<char*>(Allocate(0xC));
        if (node) {
            At<void*>(node, 0x8) = lot;
            AddRefOf(lot);
        }
        char* const before = At<char*>(first, 0x4);
        At<char*>(node, 0x0) = first;
        At<char*>(node, 0x4) = before;
        At<char*>(before, 0x0) = node;
        At<char*>(first, 0x4) = node;
        ReleaseOf(lot);

        char* related = NewList();
        reinterpret_cast<LotListFn>(kRelatedOccupants)(self, lot, &related);
        for (char* at = At<char*>(related, 0x0); at != related; at = At<char*>(at, 0x0)) {
            void* const occupant = At<void*>(at, 0x8);
            if ((run.flags & 0x2000) && !ListContains(occupants, occupant))
                continue;
            if (InsertAffected(run.affected, occupant))
                ++run.count;
        }
        DestroyOccupantList(self, related);
    } else {
        run.result = false;
    }
    DestroyOccupantList(self, occupants);
}

// One cell of the region (0x004685D0 to 0x0046888A).
void VisitCell(Run& run, int32_t x, int32_t z)
{
    char* const self = run.self;
    void* const lot = Virtual<void*>(run.lotManager, 0x18, x, z, uint32_t{0});
    if (lot) {
        // The set of lots seen, searched like the game's lower bound.
        char* const head = run.seen.head;
        char* best = head;
        char* at = At<char*>(head, 0x4);
        while (at) {
            if (reinterpret_cast<std::uintptr_t>(At<void*>(at, 0x10)) < reinterpret_cast<std::uintptr_t>(lot)) {
                at = At<char*>(at, 0xC);
            } else {
                best = at;
                at = At<char*>(at, 0x8);
            }
        }
        const bool met = best != head && !(reinterpret_cast<std::uintptr_t>(lot) <
                                           reinterpret_cast<std::uintptr_t>(At<void*>(best, 0x10)));
        if (!met) {
            AddRefOf(lot);
            void* held = lot;
            InsertResult result{};
            InsertUnique(&run.seen, &result, &held);
            ReleaseOf(lot);
            void* const building = Virtual<void*>(lot, 0xC0);
            bool take = false;
            if (building) {
                if (Virtual<uint8_t>(lot, 0x9C)) {
                    void* const occupant = Virtual<void*>(building, 0xC);
                    take = Virtual<uint8_t>(run.defaultFilter, 0xC, occupant) != 0;
                }
            } else {
                const uint32_t state = Virtual<uint32_t>(lot, 0x84);
                if (state == 6 || state == 1)
                    take = !run.view || Virtual<uint8_t>(run.view, 0x10, kTypeBuilding);
            }
            if (take) {
                TakeLot(run, lot);
            } else if (run.privilege == 1 && building) {
                // The player's tool: a building that cannot be removed.
                void* const occupant = Virtual<void*>(building, 0xC);
                if (!Virtual<uint8_t>(occupant, 0x38, uint32_t{2}))
                    run.result = false;
            }
        }
    }
    // The occupants on the cell, whether or not there was a lot.
    int32_t rect[4] = {x, z, x, z};
    char* here = NewList();
    reinterpret_cast<RectListFn>(kNotifyLists)(self, rect, &here);
    for (char* at = At<char*>(here, 0x0); at != here; at = At<char*>(at, 0x0)) {
        void* const occupant = At<void*>(at, 0x8);
        const bool accepted = reinterpret_cast<FilterTestFn>(kFilterTest)(run.local, occupant) != 0;
        if (lot && accepted)
            continue;
        if ((run.flags & 0x2000) && !accepted)
            continue;
        if (InsertAffected(run.affected, occupant))
            ++run.count;
    }
    DestroyOccupantList(self, here);
}

} // namespace

uint8_t __fastcall DemolishRegionOurs(char* self, void*, uint32_t demolishArg, int32_t* region, int32_t privilege,
                                      uint32_t flags, uint32_t clearZonedArg, void* passedFilter, int64_t* costOut,
                                      void* demolishedSet, void* targetOccupant, int32_t effectA, int32_t effectB)
{
    static bool logged = false;
    FirstCall("DemolishRegion", logged);
    const bool demolish = (demolishArg & 0xFF) != 0;
    const uint32_t clearZoned = clearZonedArg & 0xFF;
    // The original keeps these as bytes; the dword it hands the lot
    // developer has stack garbage above bit 7, which the callee does not read.
    const bool swap = (flags & 0x300) != 0;
    const uint32_t raise = ((flags >> 12) & 1u) ^ 1u;
    const bool show = demolish && (flags & 0xFF) != 0;

    Run run{};
    run.self = self;
    run.region = region;
    run.privilege = privilege;
    run.flags = flags;
    run.result = true;
    At<void*>(self, field::kRegion) = region;

    void* const service = Global<void*>(addr::SC4_Service43DC4);
    Virtual<void>(service, 0x48, &run.view);
    Virtual<uint32_t>(service, 0x58);

    run.keepView = privilege == 0 || (flags & 0x2000) != 0;
    const uint32_t mode = privilege != 0 ? 2u : 1u;
    run.defaultFilter = At<char*>(self, field::kFilter);
    if (!run.keepView)
        AssignRef(run.defaultFilter, filter::kView, run.view);
    AssignRef(run.defaultFilter, filter::kPassed, passedFilter);
    At<uint8_t>(run.defaultFilter, filter::kActive) = 1;
    At<uint32_t>(run.defaultFilter, filter::kMode) = mode;

    // The local default filter: the base constructor, then the derived
    // vtables of class cSC4DefaultOccupantFilter.
    reinterpret_cast<SelfFn>(addr::SC4_OccupantFilterBase)(run.local);
    At<uint32_t>(run.local, 0x0) = 0x00A87FC0;
    At<uint32_t>(run.local, 0x4) = 0x00A87FA8;

    NewSet(run.affected);
    run.lotManager = Global<void*>(addr::SC4_LotManager);
    run.queued = static_cast<char*>(Allocate(0xC));
    At<char*>(run.queued, 0x0) = run.queued;
    At<char*>(run.queued, 0x4) = run.queued;
    NewSet(run.seen);

    for (int32_t x = region[0]; x <= region[2]; ++x)
        for (int32_t z = region[1]; z <= region[3]; ++z)
            if (RegionBit(region, x, z))
                VisitCell(run, x, z);

    // Every occupant of the set, in order.
    int64_t cost = 0;
    char* taken = NewList();
    void* const city = At<void*>(self, field::kCity);
    char* const end = run.affected.head;
    for (char* node = At<char*>(end, 0x8); node != end; node = Next(node)) {
        void* const occupant = At<void*>(node, 0x10);
        int64_t price = 0;
        const bool ok = Virtual<uint8_t>(self, 0x20, uint32_t{demolish && !show ? 1u : 0u}, occupant, privilege,
                                         flags, &price, uint32_t{1}, &taken) != 0;
        if (!ok)
            continue;
        if (show) {
            if (!run.view || Virtual<uint8_t>(run.view, 0xC, occupant)) {
                float position[3];
                Virtual<void>(occupant, 0x20, position);
                int32_t cellX = 0;
                int32_t cellZ = 0;
                Virtual<void>(city, 0x17C, position[0], position[2], &cellX, &cellZ);
                void* const lot = Virtual<void*>(run.lotManager, 0x18, cellX, cellZ, uint32_t{0});
                Virtual<void>(self, 0x28, occupant, flags, lot, uint32_t{occupant == targetOccupant ? 1u : 0u},
                              effectA, effectB);
            }
            Virtual<uint8_t>(self, 0x20, uint32_t{1}, occupant, privilege, flags, &price, uint32_t{1}, &taken);
        }
        cost += price;
        if (demolishedSet) {
            if (occupant)
                AddRefOf(occupant);
            void* held = occupant;
            InsertResult result{};
            InsertUnique(demolishedSet, &result, &held);
            if (occupant)
                ReleaseOf(occupant);
        }
    }
    if (show && run.count > 0)
        Virtual<void>(self, 0x2C, flags, uint32_t{0}, run.count, region);
    if (demolish)
        Virtual<void>(GetNetworkManager(), 0x1C, uint32_t{1});

    // The lots queued: the price of type 6 lots, then the developer.
    for (char* node = At<char*>(run.queued, 0x0); node != run.queued; node = At<char*>(node, 0x0)) {
        void* const lot = At<void*>(node, 0x8);
        if (Virtual<uint32_t>(lot, 0x84) == 6) {
            uint8_t depth = 0;
            uint8_t width = 0;
            Virtual<void>(lot, 0x58, &width, &depth);
            cost += static_cast<uint32_t>(static_cast<uint32_t>(depth) * width * Global<uint32_t>(kLotCostScale));
        }
        if (demolish) {
            void* const developer = At<void*>(self, field::kLotDeveloper);
            void* const configuration = Virtual<void*>(lot, 0x24);
            if ((configuration && Virtual<uint8_t>(configuration, 0x38)) || !swap) {
                const uint32_t zone = Virtual<uint32_t>(lot, 0x7C);
                Virtual<void>(developer, 0x30, lot);
                Virtual<void>(developer, 0x34, lot);
                CallLotRegions(lot, flags, zone, clearZoned);
            } else {
                Virtual<void>(developer, 0x38, lot, raise);
            }
        }
    }

    // The occupants DemolishOccupant listed join the set and the caller's.
    while (At<char*>(taken, 0x0) != taken) {
        char* const node = At<char*>(taken, 0x4);
        char* const before = At<char*>(node, 0x4);
        char* const after = At<char*>(node, 0x0);
        void* const occupant = At<void*>(node, 0x8);
        At<char*>(before, 0x0) = after;
        At<char*>(after, 0x4) = before;
        FreeListHead(node);
        if (occupant)
            AddRefOf(occupant);
        void* held = occupant;
        InsertResult result{};
        InsertUnique(&run.affected, &result, &held);
        if (occupant)
            ReleaseOf(occupant);
        if (demolishedSet) {
            if (occupant)
                AddRefOf(occupant);
            held = occupant;
            InsertUnique(demolishedSet, &result, &held);
            if (occupant)
                ReleaseOf(occupant);
        }
        ReleaseOf(occupant);
    }

    if (demolish) {
        NotifyRemovedOccupants(self, &run.affected);
        Virtual<void>(GetNetworkManager(), 0x1C, uint32_t{0});
    }
    if (costOut)
        *costOut = cost;

    // The default filter let go of what the call gave it.
    char* const defaultFilter = At<char*>(self, field::kFilter);
    if (void* const passed = At<void*>(defaultFilter, filter::kPassed)) {
        At<void*>(defaultFilter, filter::kPassed) = nullptr;
        ReleaseOf(passed);
    }
    if (void* const view = At<void*>(defaultFilter, filter::kView)) {
        At<void*>(defaultFilter, filter::kView) = nullptr;
        ReleaseOf(view);
    }
    At<uint8_t>(defaultFilter, filter::kActive) = 0;
    if (run.view)
        ReleaseOf(run.view);
    At<void*>(self, field::kRegion) = nullptr;

    ClearList(&taken);
    FreeListHead(taken);
    DestroySet(run.seen, kEraseLots);
    // The queued lots: each released, each node freed, the head freed.
    for (char* node = At<char*>(run.queued, 0x0); node != run.queued;) {
        char* const following = At<char*>(node, 0x0);
        if (void* const lot = At<void*>(node, 0x8))
            ReleaseOf(lot);
        Free(node);
        node = following;
    }
    At<char*>(run.queued, 0x0) = run.queued;
    At<char*>(run.queued, 0x4) = run.queued;
    Free(run.queued);
    DestroySet(run.affected, kEraseOccupants);
    reinterpret_cast<SelfFn>(addr::SC4OccupantFilterBase_Destroy)(run.local);
    return run.result ? 1 : 0;
}

namespace {

using RegionFn = uint8_t(__fastcall*)(char*, void*, uint32_t, int32_t*, int32_t, uint32_t, uint32_t, void*, int64_t*,
                                      void*, void*, int32_t, int32_t);
using OccupantFn = uint8_t(__fastcall*)(char*, void*, uint32_t, void*, int32_t, uint32_t, int64_t*, uint32_t, void*);
using TerrainFn = int32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, int32_t, int32_t, int32_t, int32_t, uint32_t,
                                       void*, uint32_t, int64_t*);

bool g_regionOurs = false;

bool Install(registry::Mode)
{
    const addr::Entry* region = addr_check::Find(addr::cSC4Demolition_DemolishRegion);
    const addr::Entry* occupant = addr_check::Find(addr::cSC4Demolition_DemolishOccupant);
    const addr::Entry* terrain = addr_check::Find(addr::cSC4Demolition_ModifyTerrainHeight);
    if (!region || !occupant || !terrain)
        return false;
    static RegionFn regionOriginal = nullptr;
    static OccupantFn occupantOriginal = nullptr;
    static TerrainFn terrainOriginal = nullptr;
    // Another detour at the entry cannot share it. Ours installs before the
    // wrappers (PreAppInit), so this is only a foreign patch: skip this one
    // hook and keep the others; the wrappers then hook the game's entry.
    if (hook::IsInstalledAt(region->address))
        log::Write("demolition: DemolishRegion is already hooked; ours not installed there");
    else
        g_regionOurs = hook::Install(*region, &DemolishRegionOurs, &regionOriginal);
    return (g_regionOurs || hook::IsInstalledAt(region->address)) &&
           hook::Install(*occupant, &DemolishOccupantOurs, &occupantOriginal) &&
           hook::Install(*terrain, &ModifyTerrainHeightOurs, &terrainOriginal);
}

} // namespace

std::uintptr_t RegionEntry() { return reinterpret_cast<std::uintptr_t>(&DemolishRegionOurs); }
bool RegionIsOurs() { return g_regionOurs; }

namespace {

const bool g_registered = registry::Register({
    "simulators.demolition.part2",
    "simulators/demolition",
    registry::Kind::Detour,
    registry::Phase::PreAppInit, // before game.bulldoze wraps DemolishRegion (demolition.h)
    addr::cSC4Demolition_DemolishRegion,
    "Our cSC4Demolition large functions: DemolishRegion, DemolishOccupant, ModifyTerrainHeight "
    "(src/core/demolition_2.cpp)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::demolition
