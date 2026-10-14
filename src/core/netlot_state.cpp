// Our cSC4NetworkLotManager, first part (build 641; the class maps of the
// manager, the network lot and the network lot configuration, their vtables
// and fields are in src/core/netlot_sim.h): the identity, the queries, the
// maps' helpers and the small methods of the two small classes. Each is a
// detour at the game's address, but for the six functions whose code the
// linker folded with other classes' (the same address stands in other
// vtables): those are replaced in this class's vtables only
// (simulators.netlot.slots).
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither: GetGZCLSID 0x0060C720, the manager's AddRef 0x0060C730 and the
// configuration's 0x0060C830 (EAX only); the three Releases (0x0060C740,
// 0x0060C7E0, 0x0060C840) while the count stays above 1; the lot's
// QueryInterface 0x0060C800 when it refuses. The manager's QueryInterface
// 0x0060C760 keeps ECX on every path and EDX when it refuses; the cell
// iterator's rectangle 0x0060C960 keeps ECX (EDX is its first argument on
// return) and the reset 0x0060C930 keeps EDX (ECX is 0x7FFFFFFF on return).
// The lot map's operator[] 0x0060DA50 keeps ECX only when the key is there.
// All of these are reached through vtables, through the adjustor thunks, or
// from code that loads its registers again after the call: the iterator's
// three call sites (0x0060FD96, 0x0060FF36, 0x0060FF53) reload EAX, ECX and
// EDX from the stack or from a global before the next use, the operator[]
// sites (0x0060DB67, 0x0060DBBB, 0x0060DF13, 0x0060E159) store through EAX
// and load ECX anew, and ConfigurationFits' two (0x0060DDEE, 0x0060E377)
// test AL and reload. `self` is the object's start.
//
// Left as the game's: the six adjustor thunks 0x0060E510 to 0x0060E560 (they
// jump to our QueryInterface, AddRef and Release); the lot's AddRef
// 0x004F7BA0 and the configuration's QueryInterface 0x0048C940, which are
// other classes' code too and are not in a vtable of ours that needs them
// changed; the shared containers in the class's range (netlot_sim.h has the
// list); and the second and third parts' functions, which this part calls at
// their addresses or through the vtable.
//
// Not touched by this part at all: the texture and style tables at
// 0x00AA7758 and 0x00AA7728 (another DLL writes them in place; only
// UpdatePieceStyle and UpdateConnectedPieces read them). The zone rank
// table 0x00AA76E8 and the neighbour offset tables 0x00AA7858 and
// 0x00AA7878 are read from the game's memory at every call, never copied.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the manager's own slots: 0x4 AddRef none (0x0060C730, plain ret), 0x3C
//     the deleting destructor one (0x0060EF20, ret 4); a lot's 0x4 none
//     (0x004F7BA0) and 0x18 one (0x0060D130, ret 4); a configuration's 0x4
//     and 0x8 none, 0xC none (0x0060FB50), 0x10 none (0x0040CE60), 0x1C one
//     (0x00610070, ret 4); a network piece's 0x8 Release none;
//   - the hash table's begin 0x004B0390 one (the out pair; ret 4);
//   - the tree set's erase of a subtree 0x00499F50 one (the root; ret 4) and
//     its insert 0x0049A050 two (the out pair, the value's address; ret 8);
//   - the bit vector's 0x00477A40 two (the out pair, the index; ret 8);
//   - the uint16 map's node insert 0x00712120 one (the pair; ret 4); the
//     hash table's resize 0x004F0940 one (ret 4); the vector copy
//     constructor 0x004B03E0 one (ret 4); the node allocation 0x0090CF54 and
//     the free 0x0090CF63 (cdecl, one: `add esp, 4`); operator delete
//     0x005E5620 (cdecl, one); atexit 0x009EF059 (cdecl, one);
//   - the city (0x00B43CEC): 0x174 and 0x178 none (nothing is pushed and the
//     frame's ESP offsets after them are unchanged);
//   - the zone manager (0x00B43D14): 0x14 none (0x00698CF0, plain ret); the
//     grid it gives: 0x64 none (nothing pushed; the argument offsets after
//     it are those of a frame with two saved registers);
//   - the lot manager (0x00B43D08): 0x18 three (x, z, 0; 0x0072B410, ret
//     0xC); a lot's 0xA4 none (0x006C21F0), 0x94 none (0x006C2150), 0xB8 one
//     (0x006BFA00, ret 4);
//   - the building development simulator (0x00B43D68): 0x90 none
//     (0x00698CE0), 0xA0 one (0x006985C0, ret 4); the lot developer's 0x74
//     two (the rectangle's address, 1; 0x006C5DC0, ret 8; a float in ST0);
//   - a string's 0x34 one (GetName jumps there with its one argument
//     replaced, so the callee's `ret` is GetName's);
//   - this class: the configuration map's node insert 0x0060E250 one (ret
//     4); the callback of IterateAllNetworkLots cdecl with four (`add esp,
//     0x10`).
#include "core/netlot_sim.h"

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

namespace opensc4::netlot_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("netlot: first %s", name);
}

// Hooks to leave out, for bisecting: OPENSC4_NETLOT_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETMGR_SKIP does. A slot is
// named by the address the game has in it.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_NETLOT_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool InstallDetourList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("netlot %s: no address entry for 0x%08X", what, static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("netlot %s: %u detours", what, installed);
    return true;
}

bool SwapSlotList(const Slot* slots, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slots[i].vtable);
        if (vtable[slots[i].index] != slots[i].original) {
            log::Write("netlot %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slots[i].index,
                       static_cast<unsigned>(slots[i].vtable), static_cast<unsigned>(vtable[slots[i].index]),
                       static_cast<unsigned>(slots[i].original));
            return false;
        }
    }
    unsigned swapped = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(slots[i].original))
            continue;
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slots[i].vtable) + slots[i].index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slots[i].ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
        ++swapped;
    }
    log::Write("netlot %s: %u slots are ours", what, swapped);
    return true;
}

namespace {

// ====================================================================================
// simulators.netlot.identity: QueryInterface, AddRef, Release, GetGZCLSID
// ====================================================================================

// Slot 0 (0x0060C760, ret 8; the thunks 0x0060E510 and 0x0060E540 of the
// parts at +4 and +8 take their offset off and jump here): IIDs 1 and
// 0x89A5BA3F give the object, 0x452294AA the part at +4, 0xE4FDA3D4 the part
// at +8, each referenced through slot 1 of the object. The game's answer is
// AL = 1 and the rest of EAX from AddRef; for any other ID AL = 0 over what
// its compares left in EAX: the ID itself when it is above 0x89A5BA3F, the
// ID less 0x452294AA when below. (The game also has a branch for a null
// object at the two parts: it stores null and then reads the null object's
// vtable. Not written.)
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid > kIidManager) {
        if (iid != kIidSerializable)
            return iid & 0xFFFFFF00u;
        *object = self + kSerializablePart;
    } else if (iid == kIidManager || iid == kIidUnknown) {
        *object = self;
    } else if (iid == kIidMessageTarget) {
        *object = self + kMessagePart;
    } else {
        return (iid - kIidMessageTarget) & 0xFFFFFF00u;
    }
    return (Virtual<uint32_t>(self, slot::kAddRef) & 0xFFFFFF00u) | 1u;
}

// Slot 1 (0x0060C730; the thunks 0x0060E520 and 0x0060E550 jump here): the
// count at +0x6C up by one; the new count is the answer. The game touches
// EAX only.
uint32_t __fastcall AddRef(char* self, void*)
{
    static bool logged = false;
    FirstCall("AddRef", logged);
    return ++At<uint32_t>(self, field::kRefCount);
}

// Slot 2 (0x0060C740; the thunks 0x0060E530 and 0x0060E560 jump here): above
// 1 (unsigned) the count goes down by one and is the answer; else the count
// is left as it is and slot 15, the deleting destructor, is called with 1;
// the answer is 0.
uint32_t __fastcall Release(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release", logged);
    const uint32_t count = At<uint32_t>(self, field::kRefCount);
    if (count > 1) {
        At<uint32_t>(self, field::kRefCount) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, slot::kDeletingDestructor, 1u);
    return 0;
}

// The serializable part's slot 5 (0x0060C720; also called once by the class
// registration at 0x0046043B, before any detour is in place): the class ID.
// Only EAX is touched.
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// ====================================================================================
// simulators.netlot.queries: the lots, the configurations, zone and wealth,
// the fit test, the maps and the cell iterator
// ====================================================================================

// Slot 5 (0x0060D650, ret 8): the lot of a cell, or null; not referenced.
// The key is ((x & 0xFF) << 8) | z cut to 16 bits (x's low byte goes to AH
// of a cleared EAX and all of z is ORed in); the lot map's bucket is the key
// modulo the bucket count (unsigned) and the chain is walked for the key.
void* __fastcall GetNetworkLot(char* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("GetNetworkLot", logged);
    const uint16_t key = CellKey(x, z);
    for (char* found = BucketHead(self + field::kLots, key); found; found = At<char*>(found, hashnode::kNext))
        if (At<uint16_t>(found, hashnode::kKey) == key)
            return At<void*>(found, hashnode::kValue);
    return nullptr;
}

// Slot 6 (0x0060DBF0, ret 4): every configuration of the configuration map
// into the caller's tree set of pointers. A set that is not empty is
// emptied first: its nodes freed by the game's 0x00499F50 from the root, the
// head made its own left and right again with no root, the count 0 (what
// the nodes held is not released). Then the map's nodes in the table's
// order (0x004B0390 gives the first) and each node's vector in its order:
// the configuration inserted (0x0049A050, at its address) and referenced
// (its slot 1), also when the set had it already. The caller owns one
// reference per entry. The game's count of a vector is read again after
// every insert. EAX is not set.
void __fastcall GetNetworkLotConfigurationSet(char* self, void*, TreeSet* set)
{
    static bool logged = false;
    FirstCall("GetNetworkLotConfigurationSet", logged);
    if (set->count != 0) {
        CallThis<void, char*>(addr::SC4PtrSet_EraseSubtree, set, At<char*>(set->head, tree::kParent));
        At<char*>(set->head, tree::kLeft) = set->head;
        At<char*>(set->head, tree::kParent) = nullptr;
        At<char*>(set->head, tree::kRight) = set->head;
        set->count = 0;
    }
    HashIterator first;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, self + field::kConfigurations, &first);
    char* const table = first.table;
    for (char* found = first.node; found; found = NextNode32(table, found)) {
        for (uint32_t i = 0; i < ConfigurationCount(found); ++i) {
            void* value = At<void*>(At<char*>(found, hashnode::kValue), i * 4);
            struct {
                char* where;
                uint32_t inserted;
            } result;
            CallThis<void*, void*, void**>(addr::SC4PtrSet_Insert, set, &result, &value);
            AddRefOf(At<void*>(At<char*>(found, hashnode::kValue), i * 4));
        }
    }
}

// Slot 7 (0x0060D9A0, ret 8): whether some configuration of the map has the
// ID (each one's slot 4 is asked, in the table's order and each vector's
// order, until one says so). The second argument is never read. AL only.
uint8_t __fastcall GetNetworkLotConfigurationById(char* self, void*, uint32_t id, void*)
{
    static bool logged = false;
    FirstCall("GetNetworkLotConfigurationById", logged);
    HashIterator first;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, self + field::kConfigurations, &first);
    char* const table = first.table;
    for (char* found = first.node; found; found = NextNode32(table, found))
        for (uint32_t i = 0; i < ConfigurationCount(found); ++i) {
            void* const config = At<void*>(At<char*>(found, hashnode::kValue), i * 4);
            if (Virtual<uint32_t>(config, configuration_slot::kGetId) == id)
                return 1;
        }
    return 0;
}

// The zone byte of a cell in what the zone grid's slot 25 gives: its first
// word points at the columns (12 bytes each), a column's first word at its
// bytes by z. The game's 32-bit sums, no bounds.
int8_t ZoneAt(char* columns, int32_t x, int32_t z)
{
    const std::uintptr_t column = At<std::uintptr_t>(columns, 0) + static_cast<uint32_t>(x) * zones_slot::kColumnSize;
    return *reinterpret_cast<const int8_t*>(*reinterpret_cast<const std::uintptr_t*>(column) +
                                            static_cast<uint32_t>(z));
}

int32_t ZoneRank(int32_t zone)
{
    return Global<int32_t>(addr::cSC4NetworkLotManager_ZoneRanks + static_cast<uint32_t>(zone) * 4u);
}

// Slot 9 (0x0060D030, ret 8; `this` is not read): the zone type a network
// cell counts as. The zone manager's (0x00B43D14) grid (its slot 5) gives
// its columns (the grid's slot 25); the cell's own byte, sign-extended, is
// the answer when it is not 0. Else the best of the neighbours: the eight
// offsets of the tables at 0x00AA7858 (x) and 0x00AA7878 (z), read from the
// game's memory, are the four neighbours and then the four diagonals; a
// neighbour inside the city (signed compares with the city's slots 93 and
// 94, asked once each) replaces the best so far when its rank in the table
// at 0x00AA76E8 is lower (signed; the best starts as 0, whose rank is the
// highest). After the fourth neighbour the diagonals are only looked at
// when the best is 0, 7 or 14. The game keeps that switch in the low byte
// of its first argument's stack slot; x is in a register by then.
int32_t __fastcall GetZoneTypeForNetwork(void*, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("GetZoneTypeForNetwork", logged);
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_ZoneManager), zones_slot::kGetGrid);
    char* const columns = Virtual<char*>(grid, zones_slot::kGetColumns);
    const int8_t own = ZoneAt(columns, x, z);
    if (own != 0)
        return own;
    const int32_t sizeX = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountX);
    const int32_t sizeZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountZ);
    bool diagonals = false;
    int32_t best = kZoneNone;
    for (uint32_t i = 0;; ++i) {
        if (i >= kNeighbours && (i >= kNeighboursAndDiagonals || !diagonals))
            break;
        const int32_t nx = Global<int32_t>(addr::cSC4NetworkLotManager_NeighborDx + i * 4u) + x;
        const int32_t nz = Global<int32_t>(addr::cSC4NetworkLotManager_NeighborDz + i * 4u) + z;
        if (nx > -1 && nx < sizeX && nz > -1 && nz < sizeZ) {
            const int32_t zone = ZoneAt(columns, nx, nz);
            if (ZoneRank(zone) < ZoneRank(best))
                best = zone;
        }
        if (i == kNeighbours - 1 && (best == kZoneNone || best == kZoneWeakA || best == kZoneWeakB))
            diagonals = true;
    }
    return best;
}

// Slot 10 (0x0060CBB0, ret 8; `this` is not read): the wealth (0 to 3) a
// network cell counts as: the commonest wealth of the lots around it. The
// same eight offsets; the diagonals are only looked at when none of the
// four neighbours had a lot. A cell inside the city (signed compares, the
// city's slots 93 and 94 asked once each) is asked of the lot manager
// (0x00B43D08, slot 6 with the cell and 0). A lot counts with its occupant
// wealth (slot 41); when the lot is in its livable state (slot 37) and that
// wealth is 0, with the wealth of its building's profile instead: the lot's
// building type (slot 46 with 0, asked twice), when not 0, to the building
// development simulator's (0x00B43D68) slot 40; the word at +4 of what that
// gives, or 0 when it gives nothing. A wealth outside 0 to 3 counts as 0.
// The answer is the last of 1, 2, 3 whose count is not 0 and not below the
// count of the answer so far (which starts as 0): ties go to the higher.
uint32_t __fastcall GetWealthForNetwork(void*, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("GetWealthForNetwork", logged);
    const int32_t sizeX = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountX);
    const int32_t sizeZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountZ);
    uint32_t counts[kWealths] = {0, 0, 0, 0};
    bool none = true;
    for (uint32_t i = 0;; ++i) {
        if (i >= kNeighbours && (i >= kNeighboursAndDiagonals || !none))
            break;
        const int32_t nx = Global<int32_t>(addr::cSC4NetworkLotManager_NeighborDx + i * 4u) + x;
        const int32_t nz = Global<int32_t>(addr::cSC4NetworkLotManager_NeighborDz + i * 4u) + z;
        if (!(nx > -1 && nx < sizeX && nz > -1 && nz < sizeZ))
            continue;
        void* const cityLot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), lots_slot::kGetLot, nx, nz, 0u);
        if (!cityLot)
            continue;
        int32_t wealth = Virtual<int32_t>(cityLot, citylot_slot::kGetOccupantWealth);
        if (Virtual<uint8_t>(cityLot, citylot_slot::kIsInLivableState) && wealth == 0) {
            if (Virtual<uint32_t>(cityLot, citylot_slot::kGetBuildingType, 0u) != 0) {
                void* const development = Global<void*>(addr::SC4_Service43D68);
                const uint32_t type = Virtual<uint32_t>(cityLot, citylot_slot::kGetBuildingType, 0u);
                if (char* const profile = Virtual<char*>(development, development_slot::kGetBuildingProfilePtr, type)) {
                    wealth = At<int32_t>(profile, development_slot::kProfileWealth);
                    if (wealth < 0 || wealth >= static_cast<int32_t>(kWealths))
                        wealth = 0;
                }
            }
        } else if (wealth < 0 || wealth >= static_cast<int32_t>(kWealths)) {
            wealth = 0;
        }
        ++counts[wealth];
        if (i < kNeighbours)
            none = false;
    }
    uint32_t best = 0;
    for (uint32_t wealth = 1; wealth < kWealths; ++wealth)
        if (counts[wealth] > 0 && counts[wealth] >= counts[best])
            best = wealth;
    return best;
}

// Slot 11 (0x0060D820, ret 8): the callback (cdecl) for every lot of the lot
// map in the table's order, with the key's high byte (x), its low byte (z),
// the lot and the context, until it answers false. The next node is read
// after the callback came back. AL is the last answer, 1 for an empty map.
uint8_t __fastcall IterateAllNetworkLots(char* self, void*, LotCallback callback, void* context)
{
    static bool logged = false;
    FirstCall("IterateAllNetworkLots", logged);
    uint8_t goOn = 1;
    HashIterator first;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, self + field::kLots, &first);
    char* const table = first.table;
    char* found = first.node;
    while (found) {
        const uint32_t key = At<uint16_t>(found, hashnode::kKey);
        goOn = callback(key >> 8, key & 0xFF, At<void*>(found, hashnode::kValue), context);
        found = NextNode16(table, found);
        if (!goOn)
            break;
    }
    return goOn;
}

// A bit of one of the game's bit vectors (the words' address, then the
// first bit's offset): the word and the mask, as the game has it inline for
// the zones and calls 0x00477A40 for: the index plus the offset, divided by
// 32 toward zero for the word, the remainder for the bit, and for a
// negative remainder the word before and the bit plus 32.
bool BitSet(char* vector, int32_t index)
{
    const int32_t bit = index + At<int32_t>(vector, 4);
    const uint32_t* word = At<const uint32_t*>(vector, 0) + bit / 32;
    int32_t shift = bit % 32;
    if (shift < 0) {
        shift += 32;
        --word;
    }
    return (*word & (1u << shift)) != 0;
}

// 0x0060CD30 (ret 0x1C; `this` is not read; CreateLot's 0x0060DDEE and
// UpdateLot's 0x0060E377 are the callers): whether a configuration fits a
// cell. In the game's order:
//   - the cell: the mask byte at +0x2C + (z modulo the period) must have
//     the bit of (x modulo the period); signed remainders, the period the
//     byte at +0x30 (a period of 0 divides by zero, as in the game); the
//     row is the remainder's low byte and the bit 1 shifted in a byte by
//     the remainder's low five bits;
//   - the zone's bit in the vector at +0x34 and the wealth's in the one at
//     +0x48 (through the game's 0x00477A40, at its address);
//   - the piece's value (the caller's: the piece's slot 0x60): bit `value`
//     (its low five bits) of the byte at +0x64;
//   - the rule byte at +0x65: 1 needs the piece's flag (the caller's: slot
//     0x64), 2 forbids it;
//   - the slope of the cell: the building development simulator's
//     (0x00B43D68) lot developer (slot 36), its slot 29 with the rectangle
//     (x, z, x, z) and 1; the float it leaves in ST0 must not be below the
//     float at +0x5C nor above the one at +0x60 (an unordered compare
//     passes both, as the game's flag tests do).
// AL only.
uint8_t __fastcall ConfigurationFits(void*, void*, char* config, int32_t x, int32_t z, int32_t zone, int32_t wealth,
                                     uint32_t pieceValue, uint32_t pieceFlag)
{
    static bool logged = false;
    FirstCall("ConfigurationFits", logged);
    const int32_t period = At<uint8_t>(config, configuration::kPeriod);
    const uint8_t column = static_cast<uint8_t>(1u << (static_cast<uint32_t>(x % period) & 31u));
    const uint8_t row = static_cast<uint8_t>(z % period);
    if (!(At<uint8_t>(config, configuration::kCellMasks + row) & column))
        return 0;
    if (!BitSet(config + configuration::kZones, zone))
        return 0;
    struct {
        const uint32_t* word;
        uint32_t mask;
    } bit;
    CallThis<void*, void*, int32_t>(addr::SC4BitVector_At, config + configuration::kWealths, &bit, wealth);
    if (!(*bit.word & bit.mask))
        return 0;
    if (!(At<uint8_t>(config, configuration::kPieceMask) & static_cast<uint8_t>(1u << (pieceValue & 31u))))
        return 0;
    const uint8_t rule = At<uint8_t>(config, configuration::kPieceFlagRule);
    if (rule != 0) {
        const uint8_t flag = static_cast<uint8_t>(pieceFlag);
        if (rule == configuration::kNeedsPieceFlag && !flag)
            return 0;
        if (rule == configuration::kForbidsPieceFlag && flag)
            return 0;
    }
    void* const development = Global<void*>(addr::SC4_Service43D68);
    int32_t rectangle[4] = {x, z, x, z};
    void* const developer = Virtual<void*>(development, development_slot::kGetLotDeveloper);
    const double slope = Virtual<double>(developer, development_slot::kGetAreaSlope, &rectangle[0], 1u);
    if (slope < static_cast<double>(At<float>(config, configuration::kLeastSlope)))
        return 0;
    if (slope > static_cast<double>(At<float>(config, configuration::kGreatestSlope)))
        return 0;
    return 1;
}

// 0x0060DA50 (ret 4: the address of a uint16 key; `this` is the table: the
// lot map for Read 0x0060DB67 and CreateLot 0x0060DF13, the pothole map for
// Read 0x0060DBBB and Pothole 0x0060E159): the table's operator[]. The
// address of the value word of the key's node; without one a node with a
// null value is put in by the game's 0x00712120 (which grows the table
// first and answers the address of the node's key), at its address.
void** __fastcall LotMapIndex(char* table, void*, const uint16_t* keyAt)
{
    static bool logged = false;
    FirstCall("LotMapIndex", logged);
    const uint16_t key = *keyAt;
    for (char* found = BucketHead(table, key); found; found = At<char*>(found, hashnode::kNext))
        if (At<uint16_t>(found, hashnode::kKey) == key)
            return reinterpret_cast<void**>(found + hashnode::kValue);
    struct {
        uint16_t key;
        uint16_t unset; // the game leaves these two bytes as its stack had them; the insert copies the key only
        void* value;
    } pair = {key, 0, nullptr};
    return reinterpret_cast<void**>(CallThis<char*, void*>(addr::SC4Uint16PtrMap_InsertNode, table, &pair) + 4);
}

// What the configuration map's operator[] hands to its node insert: a piece
// ID and a vector of configurations.
struct ConfigurationEntry {
    uint32_t pieceId;
    Vector configurations;
};

// 0x0060E250 (ret 4: an entry; `this` is the configuration map; the
// operator[] below is its only caller): a new node for the entry. The table
// grows for one more element first (0x004F0940); then the head of the key's
// bucket is read, a node of 0x14 bytes allocated (0x0090CF54), its link
// cleared, the key stored and the entry's vector copied into the node
// (0x004B03E0); the node goes to the front of the bucket and the count up
// by one. The answer is the address of the node's key. The game skips the
// fill when that address is null (its placement test).
char* __fastcall ConfigMapInsertNode(char* table, void*, ConfigurationEntry* entry)
{
    static bool logged = false;
    FirstCall("ConfigMapInsertNode", logged);
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, table, At<uint32_t>(table, hashtable::kCount) + 1);
    const uint32_t index = entry->pieceId % BucketCount(table);
    char* const head = At<char*>(At<char*>(table, hashtable::kBuckets), index * 4);
    char* const made = CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, hashnode::kConfigurationNodeSize);
    At<char*>(made, hashnode::kNext) = nullptr;
    if (reinterpret_cast<std::uintptr_t>(made) + hashnode::kKey != 0) {
        At<uint32_t>(made, hashnode::kKey) = entry->pieceId;
        CallThis<void*, Vector*>(addr::SC4PtrVector_Copy, made + hashnode::kValue, &entry->configurations);
    }
    At<char*>(made, hashnode::kNext) = head;
    At<char*>(At<char*>(table, hashtable::kBuckets), index * 4) = made;
    At<uint32_t>(table, hashtable::kCount) = At<uint32_t>(table, hashtable::kCount) + 1;
    return made + hashnode::kKey;
}

// 0x0060E3C0 (ret 4: the address of a piece ID; `this` is the configuration
// map; AddConfiguration 0x0060E7C3 and WriteLotConfiguration 0x0060ECA1 call
// it): the map's operator[]. The address of the vector of the ID's node;
// without one an entry with a copy of an empty vector is made on the stack
// (the copy constructor 0x004B03E0 allocates nothing for it) and handed to
// the node insert 0x0060E250, at its address, and the entry's vector is
// freed when it has storage (0x0090CF63; it never has).
Vector* __fastcall ConfigMapIndex(char* table, void*, const uint32_t* keyAt)
{
    static bool logged = false;
    FirstCall("ConfigMapIndex", logged);
    const uint32_t key = *keyAt;
    for (char* found = BucketHead(table, key); found; found = At<char*>(found, hashnode::kNext))
        if (At<uint32_t>(found, hashnode::kKey) == key)
            return reinterpret_cast<Vector*>(found + hashnode::kValue);
    Vector empty = {nullptr, nullptr, nullptr};
    ConfigurationEntry entry;
    entry.pieceId = key;
    CallThis<void*, Vector*>(addr::SC4PtrVector_Copy, &entry.configurations, &empty);
    char* const made =
        CallThis<char*, ConfigurationEntry*>(addr::cSC4NetworkLotManager_ConfigMapInsertNode, table, &entry);
    if (entry.configurations.begin)
        CallCdecl<void, void*>(addr::SC4_Free, entry.configurations.begin);
    return reinterpret_cast<Vector*>(made + 4);
}

// 0x0060C930 (no stack arguments; `this` is a cell iterator; DoMessage's
// 0x0060FF36 is the only caller): the iterator as new: the two bytes and
// the current cell 0, the step 1, the rectangle 0x80000000 twice and
// 0x7FFFFFFF twice. The answer is the iterator.
char* __fastcall CellIteratorReset(char* self, void*)
{
    static bool logged = false;
    FirstCall("CellIteratorReset", logged);
    At<uint8_t>(self, iterator::kState) = 0;
    At<uint8_t>(self, iterator::kByte1) = 0;
    At<int32_t>(self, iterator::kX) = 0;
    At<int32_t>(self, iterator::kZ) = 0;
    At<int32_t>(self, iterator::kStep) = 1;
    At<int32_t>(self, iterator::kLowX) = iterator::kResetLow;
    At<int32_t>(self, iterator::kLowZ) = iterator::kResetLow;
    At<int32_t>(self, iterator::kHighX) = iterator::kResetHigh;
    At<int32_t>(self, iterator::kHighZ) = iterator::kResetHigh;
    return self;
}

// 0x0060C960 (ret 0x10; `this` is a cell iterator; DoMessage's 0x0060FD96
// and 0x0060FF53 call it): the rectangle set to low x, low z, high x, high
// z, a high bound below its low bound (signed) raised to it. EAX is the low
// z.
int32_t __fastcall CellIteratorSetRectangle(char* self, void*, int32_t lowX, int32_t lowZ, int32_t highX,
                                            int32_t highZ)
{
    static bool logged = false;
    FirstCall("CellIteratorSetRectangle", logged);
    At<int32_t>(self, iterator::kLowX) = lowX;
    At<int32_t>(self, iterator::kLowZ) = lowZ;
    At<int32_t>(self, iterator::kHighX) = highX;
    At<int32_t>(self, iterator::kHighZ) = highZ;
    if (At<int32_t>(self, iterator::kHighX) < lowX)
        At<int32_t>(self, iterator::kHighX) = lowX;
    const int32_t z = At<int32_t>(self, iterator::kLowZ);
    if (At<int32_t>(self, iterator::kHighZ) < z)
        At<int32_t>(self, iterator::kHighZ) = z;
    return z;
}

// ====================================================================================
// simulators.netlot.lot: the network lot and the configuration's reference count
// ====================================================================================

// The lot's slot 0 (0x0060C800, ret 8): IIDs 1 and 0x89A5BA66 give the lot,
// referenced through its slot 1; AL = 1 over AddRef's EAX. Any other ID: AL
// = 0 over the ID.
uint32_t __fastcall LotQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("Lot QueryInterface", logged);
    if (iid != kIidUnknown && iid != kIidLot)
        return iid & 0xFFFFFF00u;
    *object = self;
    return (Virtual<uint32_t>(self, lot_slot::kAddRef) & 0xFFFFFF00u) | 1u;
}

// The lot's slot 2 (0x0060C7E0): as the manager's, with the count at +0x10
// and the deleting destructor at slot 6.
uint32_t __fastcall LotRelease(char* self, void*)
{
    static bool logged = false;
    FirstCall("Lot Release", logged);
    const uint32_t count = At<uint32_t>(self, lot::kRefCount);
    if (count > 1) {
        At<uint32_t>(self, lot::kRefCount) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, lot_slot::kDeletingDestructor, 1u);
    return 0;
}

// The lot's slot 4 (0x0060CE50): the number of objects of its
// configuration: the byte length of the vector the configuration's slot 3
// gives, divided by 0x34 (signed, toward zero: the game multiplies by
// 0x4EC4EC4F); 0 without a configuration.
int32_t __fastcall LotGetObjectCount(char* self, void*)
{
    static bool logged = false;
    FirstCall("Lot GetLotConfigurationObjectCount", logged);
    void* const config = At<void*>(self, lot::kConfiguration);
    if (!config)
        return 0;
    const Vector* const objects = Virtual<const Vector*>(config, configuration_slot::kGetObjectArray);
    return static_cast<int32_t>(objects->end - objects->begin) / static_cast<int32_t>(configuration::kObjectSize);
}

// The lot's slot 5 (0x00610020): the vector of objects of its configuration
// (the configuration's slot 3, by a jump in the game). Without a
// configuration the game's static empty vector at 0x00B44858, which is made
// on the first use: the bit 0 of the guard at 0x00B44864 set, the three
// words cleared and the game's exit function 0x00A7C050 handed to its
// atexit (0x009EF059). The game's own static is used, so that both this and
// the game's function (when this one is not installed) agree on the guard.
void* __fastcall LotGetObjectArray(char* self, void*)
{
    static bool logged = false;
    FirstCall("Lot GetLotConfigurationObjectArray", logged);
    if (void* const config = At<void*>(self, lot::kConfiguration))
        return Virtual<void*>(config, configuration_slot::kGetObjectArray);
    if (!(Global<uint8_t>(addr::cSC4NetworkLot_EmptyObjectsGuard) & 1)) {
        Global<uint32_t>(addr::cSC4NetworkLot_EmptyObjectsGuard) |= 1u;
        Global<uint32_t>(addr::cSC4NetworkLot_EmptyObjects) = 0;
        Global<uint32_t>(addr::cSC4NetworkLot_EmptyObjects + 4) = 0;
        Global<uint32_t>(addr::cSC4NetworkLot_EmptyObjects + 8) = 0;
        CallCdecl<int, std::uintptr_t>(addr::SC4_atexit, addr::cSC4NetworkLot_EmptyObjectsAtExit);
    }
    return reinterpret_cast<void*>(addr::cSC4NetworkLot_EmptyObjects);
}

// The lot's slot 6 (0x0060D130, ret 4: the flags; Release calls it with 1):
// the vtable stored again, the configuration released when there is one
// (its slot 2), then the network piece (its slot 2), and with flag 1 the
// memory freed (0x005E5620). The answer is the lot.
void* __fastcall LotDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("Lot deleting destructor", logged);
    At<std::uintptr_t>(self, 0) = addr::cSC4NetworkLot_vftable;
    if (void* const config = At<void*>(self, lot::kConfiguration))
        ReleaseOf(config);
    if (void* const piece = At<void*>(self, lot::kOccupant))
        ReleaseOf(piece);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// The configuration's slot 1 (0x0060C830): the count at +0x68 up by one;
// EAX only in the game.
uint32_t __fastcall ConfigurationAddRef(char* self, void*)
{
    static bool logged = false;
    FirstCall("Configuration AddRef", logged);
    return ++At<uint32_t>(self, configuration::kRefCount);
}

// The configuration's slot 2 (0x0060C840): as the manager's, with the count
// at +0x68 and the deleting destructor at slot 7.
uint32_t __fastcall ConfigurationRelease(char* self, void*)
{
    static bool logged = false;
    FirstCall("Configuration Release", logged);
    const uint32_t count = At<uint32_t>(self, configuration::kRefCount);
    if (count > 1) {
        At<uint32_t>(self, configuration::kRefCount) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, configuration_slot::kDeletingDestructor, 1u);
    return 0;
}

// ====================================================================================
// simulators.netlot.slots: the functions whose code is folded with other
// classes', in this class's three vtables only
// ====================================================================================

// The manager's slot 13 (0x0060CB80, ret 4; the same code is slot 10 of
// cSC4Lot's vtable): the configuration forced for the next piece. Unless it
// is the one at +0x34 already: the new one referenced when not null (its
// slot 1), stored, and the old one released when not null (its slot 2).
// EAX is not set.
void __fastcall SetLotConfigurationForNextPiece(char* self, void*, void* config)
{
    static bool logged = false;
    FirstCall("SetLotConfigurationForNextPiece", logged);
    void* const old = At<void*>(self, field::kForced);
    if (config == old)
        return;
    if (config)
        AddRefOf(config);
    At<void*>(self, field::kForced) = config;
    if (old)
        ReleaseOf(old);
}

// The lot's slot 3 (0x0060CE80; in about 150 vtables): the network piece at
// +4, not referenced.
void* __fastcall LotGetNetworkOccupant(char* self, void*)
{
    static bool logged = false;
    FirstCall("Lot GetNetworkOccupant", logged);
    return At<void*>(self, lot::kOccupant);
}

// The configuration's slot 3 (0x0060FB50; in 44 vtables): the address of
// the vector of objects at +4.
void* __fastcall ConfigurationGetObjectArray(char* self, void*)
{
    static bool logged = false;
    FirstCall("Configuration GetObjectArray", logged);
    return self + configuration::kObjects;
}

// The configuration's slot 4 (0x0040CE60; shared): the ID at +0x10.
uint32_t __fastcall ConfigurationGetId(char* self, void*)
{
    static bool logged = false;
    FirstCall("Configuration GetID", logged);
    return At<uint32_t>(self, configuration::kId);
}

// The configuration's slot 5 (0x0060FB60, one argument; also in the vtable
// at 0x00AB0EC0): the caller's string is told to copy the name at +0x14
// (its slot 13). The game replaces its argument and jumps, so the string's
// function returns to the caller with its own EAX.
uint32_t __fastcall ConfigurationGetName(char* self, void*, void* text)
{
    static bool logged = false;
    FirstCall("Configuration GetName", logged);
    return Virtual<uint32_t>(text, string_slot::kCopy, self + configuration::kName);
}

// The configuration's slot 6 (0x0060FB80; in 13 vtables): the network piece
// ID at +0x28.
uint32_t __fastcall ConfigurationGetNetworkPieceId(char* self, void*)
{
    static bool logged = false;
    FirstCall("Configuration GetNetworkPieceId", logged);
    return At<uint32_t>(self, configuration::kPieceId);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETLOT_DETOUR(address, fn)
#define OPENSC4_SLOT(vtable, index, original, fn) OPENSC4_NETLOT_SLOT(vtable, index, original, fn)

const Detour kIdentity[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_AddRef, AddRef),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_Release, Release),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_GetGZCLSID, GetGZCLSID),
};

const Detour kQueries[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_GetNetworkLot, GetNetworkLot),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_GetNetworkLotConfigurationSet, GetNetworkLotConfigurationSet),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_GetNetworkLotConfigurationById, GetNetworkLotConfigurationById),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_GetZoneTypeForNetwork, GetZoneTypeForNetwork),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_GetWealthForNetwork, GetWealthForNetwork),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_IterateAllNetworkLots, IterateAllNetworkLots),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_ConfigurationFits, ConfigurationFits),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_LotMapIndex, LotMapIndex),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_ConfigMapInsertNode, ConfigMapInsertNode),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_ConfigMapIndex, ConfigMapIndex),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_CellIteratorReset, CellIteratorReset),
    OPENSC4_DETOUR(addr::cSC4NetworkLotManager_CellIteratorSetRectangle, CellIteratorSetRectangle),
};

const Detour kLot[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkLot_QueryInterface, LotQueryInterface),
    OPENSC4_DETOUR(addr::cSC4NetworkLot_Release, LotRelease),
    OPENSC4_DETOUR(addr::cSC4NetworkLot_GetLotConfigurationObjectCount, LotGetObjectCount),
    OPENSC4_DETOUR(addr::cSC4NetworkLot_GetLotConfigurationObjectArray, LotGetObjectArray),
    OPENSC4_DETOUR(addr::cSC4NetworkLot_DeletingDestructor, LotDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4NetworkLotConfiguration_AddRef, ConfigurationAddRef),
    OPENSC4_DETOUR(addr::cSC4NetworkLotConfiguration_Release, ConfigurationRelease),
};

// The folded code. None of these slots is called directly at its address by
// this class's code (the game reaches each through the object's vtable), and
// the other vtables that hold the same addresses keep the game's code.
const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4NetworkLotManager_vftable, 13, addr::cSC4NetworkLotManager_SetLotConfigurationForNextPiece,
                 SetLotConfigurationForNextPiece),
    OPENSC4_SLOT(addr::cSC4NetworkLot_vftable, 3, addr::cSC4NetworkLot_GetNetworkOccupant, LotGetNetworkOccupant),
    OPENSC4_SLOT(addr::cSC4NetworkLotConfiguration_vftable, 3, addr::cSC4NetworkLotConfiguration_GetObjectArray,
                 ConfigurationGetObjectArray),
    OPENSC4_SLOT(addr::cSC4NetworkLotConfiguration_vftable, 4, addr::cSC4Advisor_F40CE60, ConfigurationGetId),
    OPENSC4_SLOT(addr::cSC4NetworkLotConfiguration_vftable, 5, addr::cSC4NetworkLotConfiguration_GetName,
                 ConfigurationGetName),
    OPENSC4_SLOT(addr::cSC4NetworkLotConfiguration_vftable, 6, addr::cSTETerrain_CellCountX,
                 ConfigurationGetNetworkPieceId),
};

#undef OPENSC4_DETOUR
#undef OPENSC4_SLOT

bool InstallIdentity(registry::Mode) { return InstallDetours(kIdentity, "identity"); }
bool InstallQueries(registry::Mode) { return InstallDetours(kQueries, "queries"); }
bool InstallLot(registry::Mode) { return InstallDetours(kLot, "lot"); }
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }

const bool g_registered[] = {
    registry::Register({"simulators.netlot.identity", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_QueryInterface,
                        "Our network lot manager identity: QueryInterface, AddRef, Release and GetGZCLSID (4 "
                        "detours; src/core/netlot_state.cpp)",
                        false, InstallIdentity}),
    registry::Register({"simulators.netlot.queries", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_GetNetworkLot,
                        "Our network lot manager queries: GetNetworkLot, GetNetworkLotConfigurationSet, "
                        "GetNetworkLotConfigurationById, GetZoneTypeForNetwork, GetWealthForNetwork, "
                        "IterateAllNetworkLots, ConfigurationFits, the lot map and configuration map operator[] with "
                        "the node insert, the cell iterator reset and rectangle (12 detours; "
                        "src/core/netlot_state.cpp)",
                        false, InstallQueries}),
    registry::Register({"simulators.netlot.lot", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLot_QueryInterface,
                        "Our network lot: QueryInterface, Release, the object count and array, the deleting "
                        "destructor; and the network lot configuration's AddRef and Release (7 detours; "
                        "src/core/netlot_state.cpp)",
                        false, InstallLot}),
    registry::Register({"simulators.netlot.slots", "simulators/netlot", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_vftable,
                        "Our network lot code that is folded with other classes, by vtable slot: the manager's "
                        "SetLotConfigurationForNextPiece, the lot's GetNetworkOccupant, the configuration's "
                        "GetObjectArray, GetID, GetName and GetNetworkPieceId (6 slots; src/core/netlot_state.cpp)",
                        false, InstallSlots}),
};

} // namespace
} // namespace opensc4::netlot_sim
