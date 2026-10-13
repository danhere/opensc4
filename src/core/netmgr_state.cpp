// Our cSC4NetworkManager, first part (build 641; the class map, the vtables
// in their true order and the fields are in src/core/netmgr_sim.h): the
// identity, the queries, the remap lookups, the construction crews, the two
// occupant filters, the retrofit list's append and the walk over the pieces
// of an intersection, each a detour at the game's address (the thunks of the
// other vtable parts jump to QueryInterface and Release, and the constructor
// and the walk call the two helpers directly).
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither: GetGZCLSID 0x00610530 and GetEnglishNetworkName 0x006107F0 (EAX
// only); Release 0x006107D0 while the count stays above 1; GetNetworkTool
// 0x006108D0 for a type of 13 or more (in its not-creating form it keeps ECX
// and changes DL); MapPath 0x00610B40 when the byte at +0x11 is clear.
// QueryInterface 0x00610750 keeps ECX on every path (the AddRef it calls is
// the stub 0x004664A0, EAX only) and EDX when it refuses. The equal range
// 0x00611970 keeps ECX. All are reached through vtables or from code that
// loads its registers again after the call (0x00611DC3, 0x00612498,
// 0x006133B5, 0x00612449, 0x00612801). `self` is the object's start.
//
// Left as the game's: the adjustor thunks 0x006120D0, 0x006120E0, 0x006120F0
// and 0x00612100 (they jump to our QueryInterface and Release), the shared
// AddRef 0x004664A0 and the shared thunks 0x006C2270 and 0x006C2290; the
// tools' code the forwards call (0x00653B80, 0x00648F70, 0x00649080) and the
// tools' constructors; the filters' tests (0x00648C40, 0x00648CC0,
// 0x00653C00, 0x00653CC0: the pipe and subway tools' code).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the object's own slots: 0x4 AddRef none (0x004664A0), 0x60 the
//     deleting destructor one (0x00612590, ret 4);
//   - the tools' static code, all cdecl: 0x00653B80 one (`add esp, 4`),
//     0x00648F70 and 0x00649080 three (`add esp, 0xC`);
//   - operator new 0x005E55E0 and operator delete 0x005E5620 (cdecl, one);
//     the network tool's constructor 0x0062C5F0 one (the type, ret 4); the
//     pipe tool's 0x00648EC0, the subway tool's 0x00653D60 and the power
//     line tool's 0x006503C0 none (plain ret);
//   - the filter base's constructor 0x005EA080 and destructor 0x005E95C0
//     none (plain ret); the network filter's constructor 0x0061F530 one (the
//     mask, ret 4); a filter's 0x4 AddRef and 0x8 Release none;
//   - the pointer vector's InsertN 0x0051CA60 five (where, value*, value*
//     again in the allocator's place, 1, 1; ret 0x14); the pointer search
//     0x004696C0 (cdecl, four: first, last, value*, value*; `add esp,
//     0x10`); memmove 0x009EEB70 (cdecl, three);
//   - a crew's 0x4 AddRef and 0x8 Release none;
//   - the list node allocation 0x00413DD0 two (1, 0; ret 8; this the list);
//     the retrofit list's 0x00611240 two (1, 0; ret 8); its allocator's 0xC
//     one (the size; the game pushes it alone before the call);
//   - a piece's slots: 0x4 AddRef and 0x8 Release none, 0x20 HasNetworkFlag
//     one (0x00604D10, ret 4), 0xC0 AsOccupant none (0x005FD7E0), 0xEC
//     GetOccupiedCell two (0x0061B010, ret 8), 0x108
//     IsPartOfTheSameStructure one (0x006198A0, ret 4); an occupant's 0x0
//     QueryInterface two;
//   - the city (0x00B43CEC): 0x174 and 0x178, the cell counts, none (nothing
//     is pushed and the frame's ESP offsets after them are unchanged);
//   - the occupant manager (0x00B43D0C): 0x80 four (out, x, z, filter;
//     0x0049E690, ret 0x10);
//   - this class: the equal range 0x00611970 two (ret 8; this the table),
//     the walk 0x00612300 five (ret 0x14).
#include "core/netmgr_sim.h"

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

namespace opensc4::netmgr_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("netmgr: first %s", name);
}

// A new network tool of the general class (0x368 bytes, constructor
// 0x0062C5F0 with the network type); null when operator new gives none. The
// answer is the constructor's EAX. (Shared with netmgr_life.cpp's Init.)
void* MakeTool(uint32_t type)
{
    void* const raw = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, kToolSize);
    if (!raw)
        return nullptr;
    return CallThis<void*, uint32_t>(addr::cSC4NetworkManager_NetworkToolConstruct, raw, type);
}

// A new tool of one of the three classes of their own: the size and the
// constructor, which takes no argument.
void* MakeOwnTool(std::size_t size, std::uintptr_t constructor)
{
    void* const raw = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, size);
    if (!raw)
        return nullptr;
    return CallThis<void*>(constructor, raw);
}

// The number of buckets of the shadow edge table (the words between the
// pointers at +4 and +8; the byte difference shifted right as a signed
// number).
uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>(
        static_cast<int32_t>(At<uint32_t>(table, edge::kBucketsEnd) - At<uint32_t>(table, edge::kBuckets)) >> 2);
}

// Hooks to leave out, for bisecting: OPENSC4_NETMGR_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETOCC_SKIP does.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_NETMGR_SKIP", list, sizeof list))
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
            log::Write("netmgr %s: no address entry for 0x%08X", what, static_cast<unsigned>(detours[i].address));
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
    log::Write("netmgr %s: %u detours", what, installed);
    return true;
}

namespace {

uint32_t AsWord(const void* pointer)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// ====================================================================================
// simulators.netmgr.identity: QueryInterface, Release, GetGZCLSID
// ====================================================================================

// Slot 0 (0x00610750, ret 8; the thunks 0x006120D0 and 0x006120F0 of the
// parts at +4 and +8 take their offset off and jump here): IIDs 1 and
// 0xA9764C8C give the object, 0x452294AA the part at +4, 0xE4FDA3D4 the part
// at +8, each referenced through slot 1 of the object. The game's answer is
// AL = 1 and the rest of EAX from AddRef; for any other ID AL = 0 over what
// its compares left in EAX: the ID itself when it is above 0xA9764C8C, the
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

// Slot 2 (0x006107D0; the thunks 0x006120E0 and 0x00612100 jump here): above
// 1 (unsigned) the count goes down by one and is the answer; else the count
// is left as it is and slot 24, the deleting destructor, is called with 1;
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

// The serializable part's slot 5 (0x00610530; also called once by the class
// registration at 0x004603BD, before any detour is in place): the class ID.
// Only EAX is touched.
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// ====================================================================================
// simulators.netmgr.queries: the lookups, the tools, the names, the filters,
// the crews, the retrofit list and the intersection walk
// ====================================================================================

// Slot 9 (0x00610540, ret 4): the subway tool's test 0x00653B80 (cdecl) with
// the occupant; its EAX is the answer (AL: whether the byte array property
// 0xE90E25A1 of the occupant's property holder holds a 6). `this` is not
// read.
uint32_t __fastcall IsOccupantSubwayStation(void*, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("IsOccupantSubwayStation", logged);
    return CallCdecl<uint32_t, void*>(addr::cSC4NetworkManager_ToolIsSubwayStation, occupant);
}

// Slot 10 (0x00610550, ret 0xC): the pipe tool's lookup 0x00648F70 (cdecl)
// with the three arguments in order; its EAX is the answer. That function
// takes a cell (x, z) and an out pointer: of the occupants in the cell (the
// occupant manager's slot 40 with no filter) the last that answers IID
// 0x49C05B9F is stored, referenced, and AL says whether there was one.
// `this` is not read.
uint32_t __fastcall FindPipeOccupant(void*, void*, uint32_t x, uint32_t z, void** out)
{
    static bool logged = false;
    FirstCall("FindPipeOccupant", logged);
    return CallCdecl<uint32_t, uint32_t, uint32_t, void**>(addr::cSC4NetworkManager_ToolFindPipe, x, z, out);
}

// Slot 11 (0x00610570, ret 0xC): the same through 0x00649080, which asks
// for IID 0xEA0A5ACF.
uint32_t __fastcall FindPipeConnectionOccupant(void*, void*, uint32_t x, uint32_t z, void** out)
{
    static bool logged = false;
    FirstCall("FindPipeConnectionOccupant", logged);
    return CallCdecl<uint32_t, uint32_t, uint32_t, void**>(addr::cSC4NetworkManager_ToolFindPipeConnection, x, z,
                                                           out);
}

// Slot 5 (0x006108D0, ret 8): a type of 13 or more (unsigned) gives null.
// With the second argument's low byte clear: the manager's own tool of the
// type (+0x88 + 4 x type), not referenced. Else a NEW tool of the type, as
// the constructor leaves it (reference count 0, not initialised, not
// referenced here): by the jump table at 0x00610A9C the general class with
// the type for 0, 1, 2, 3, 6 and 8 to 12, the pipe tool (0x398 bytes,
// 0x00648EC0) for 4, the power line tool (0x3B8 bytes, 0x006503C0) for 5
// and the subway tool (0x398 bytes, 0x00653D60) for 7.
void* __fastcall GetNetworkTool(char* self, void*, uint32_t type, uint32_t unique)
{
    static bool logged = false;
    FirstCall("GetNetworkTool", logged);
    if (type >= kTypes)
        return nullptr;
    if (static_cast<uint8_t>(unique) == 0)
        return At<void*>(self, field::kTools + 4 * type);
    switch (type) {
    case kTypePipe:
        return MakeOwnTool(kPipeToolSize, addr::cSC4NetworkManager_PipeToolConstruct);
    case kTypePowerLine:
        return MakeOwnTool(kPowerLineToolSize, addr::cSC4NetworkManager_PowerLineToolConstruct);
    case kTypeSubway:
        return MakeOwnTool(kPipeToolSize, addr::cSC4NetworkManager_SubwayToolConstruct);
    default:
        return MakeTool(type);
    }
}

// Slot 22 (0x006107F0, ret 4): the type's entry of the table of thirteen
// name pointers at 0x00AA845C ("Road", "Rail", "Highway", "Street", "Pipe",
// "PowerLine", "Avenue", "Subway", "LightRail", "Monorail", "OneWayRoad",
// "DirtRoad", "GroundHighway"). The type is not looked at: a type outside
// 0 to 12 reads what lies beside the table, as in the game. Only EAX is
// touched.
const char* __fastcall GetEnglishNetworkName(void*, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetEnglishNetworkName", logged);
    return Global<const char*>(addr::cSC4NetworkManager_EnglishNames + type * 4u);
}

// A new 0xC-byte filter: operator new, the filter base's constructor
// (reference count 0), then the class's two vtables. Null when operator new
// gives none. Not referenced: the caller's AddRef is the first.
void* MakeFilter(std::uintptr_t vtable, std::uintptr_t vtable4)
{
    char* const made = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, kFilterSize);
    if (!made)
        return nullptr;
    CallThis<void*>(addr::SC4_OccupantFilterBase, made);
    At<std::uintptr_t>(made, 0x0) = vtable;
    At<std::uintptr_t>(made, 0x4) = vtable4;
    return made;
}

// A filter's scalar deleting destructor: the class's vtables stored again,
// the filter base's destructor (which stores the base's), and with flag 1
// the memory freed. The answer is the object.
void* DestroyFilter(char* self, uint32_t flags, std::uintptr_t vtable, std::uintptr_t vtable4)
{
    At<std::uintptr_t>(self, 0x0) = vtable;
    At<std::uintptr_t>(self, 0x4) = vtable4;
    CallThis<void>(addr::SC4OccupantFilterBase_Destroy, self);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// Slot 18 (0x00610590, no arguments; `this` is not read): a new water
// filter (vtables 0x00AA7A54 and 0x00AA7A3C).
void* __fastcall GetWaterOccupantFilter(void*, void*)
{
    static bool logged = false;
    FirstCall("GetWaterOccupantFilter", logged);
    return MakeFilter(addr::cSC4NetworkManager_WaterFilter_vftable, addr::cSC4NetworkManager_WaterFilter_vftable_4);
}

// Slot 19 (0x006105C0): a new subway filter (vtables 0x00AA7A88 and
// 0x00AA7A70).
void* __fastcall GetSubwayOccupantFilter(void*, void*)
{
    static bool logged = false;
    FirstCall("GetSubwayOccupantFilter", logged);
    return MakeFilter(addr::cSC4NetworkManager_SubwayFilter_vftable,
                      addr::cSC4NetworkManager_SubwayFilter_vftable_4);
}

// Slot 6 of the water filter's vtable (0x006104D0, ret 4: the flags; its
// Release 0x007CB920 calls it with 1).
void* __fastcall WaterFilterDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("WaterFilterDeletingDestructor", logged);
    return DestroyFilter(self, flags, addr::cSC4NetworkManager_WaterFilter_vftable,
                         addr::cSC4NetworkManager_WaterFilter_vftable_4);
}

// Slot 6 of the subway filter's vtable (0x00610500, ret 4).
void* __fastcall SubwayFilterDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SubwayFilterDeletingDestructor", logged);
    return DestroyFilter(self, flags, addr::cSC4NetworkManager_SubwayFilter_vftable,
                         addr::cSC4NetworkManager_SubwayFilter_vftable_4);
}

// Slot 16 (0x006122B0, ret 4): the crew appended to the vector at +0x7C,
// in place when there is room (the store is skipped for a null end, the
// game's placement test) and through the game's InsertN (0x0051CA60: at the
// end, one copy, the value's address twice) when the vector is full; then
// the crew is referenced (its slot 1). The game's EAX is AddRef's.
uint32_t __fastcall RegisterConstructionCrew(char* self, void*, void* crew)
{
    static bool logged = false;
    FirstCall("RegisterConstructionCrew", logged);
    Vector* const crews = reinterpret_cast<Vector*>(self + field::kCrews);
    char* const end = crews->end;
    if (end != crews->capacity) {
        if (end)
            At<void*>(end, 0) = crew;
        crews->end += 4;
        return AddRefOf(crew);
    }
    CallThis<void, char*, void**, void**, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, crews, end, &crew, &crew, 1u,
                                                              1u);
    return AddRefOf(crew);
}

// Slot 17 (0x006112A0, ret 4): the crew searched in the vector at +0x7C
// (the game's search 0x004696C0 over first, last and the value's address
// twice). Found: the entries after it moved down by one (memmove, not
// called for the last entry), the end (read again) down by 4 and the crew
// released (its slot 2). The game's EAX is Release's, or for a crew that is
// not there the vector's end.
uint32_t __fastcall UnregisterConstructionCrew(char* self, void*, void* crew)
{
    static bool logged = false;
    FirstCall("UnregisterConstructionCrew", logged);
    Vector* const crews = reinterpret_cast<Vector*>(self + field::kCrews);
    char* const end = crews->end;
    char* const found = CallCdecl<char*, char*, char*, void**, void**>(addr::SC4_FindUint32, crews->begin, end, &crew,
                                                                       &crew);
    if (found == end)
        return AsWord(end);
    if (end != found + 4)
        CallCdecl<void*, void*, void*, std::size_t>(addr::SC4_memmove, found, found + 4,
                                                    static_cast<std::size_t>(end - (found + 4)));
    crews->end = crews->end - 4;
    return ReleaseOf(crew);
}

// 0x00611240 (ret 8: a count and a word that is not read; `this`, the list,
// is not read either: ECX is pushed only to make room for a local): count x
// 12 bytes from the node allocator cached at 0x00B45238, which is fetched
// from the framework on the first use (class 0x3AE4BEA3, IID 0x3AE4BEAB) as
// the game's other list helpers do with their caches. Called by
// AddToRetrofitList and by the constructor (the list's head node).
void* __fastcall AllocateRetrofitNodes(void*, void*, uint32_t count, uint32_t)
{
    static bool logged = false;
    FirstCall("AllocateRetrofitNodes", logged);
    void* const allocator = NodeAllocatorAt(addr::cSC4NetworkManager_NodeAllocator);
    return Virtual<void*>(allocator, 0xC, count * 12u);
}

// Slot 23 (0x00612480, ret 4): the piece referenced (its slot 1) and
// appended to the retrofit list at +0x78 (the head node is read before the
// new node is allocated by 0x00611240, at its address). Nothing takes a
// piece off the list before ProcessRetrofitList empties it. The game's EAX
// is the new node.
void* __fastcall AddToRetrofitList(char* self, void*, void* piece)
{
    static bool logged = false;
    FirstCall("AddToRetrofitList", logged);
    AddRefOf(piece);
    ListNode* const head = At<ListNode*>(self, field::kRetrofit);
    ListNode* const node = CallThis<ListNode*, uint32_t, uint32_t>(addr::cSC4NetworkManager_AllocateRetrofitNodes,
                                                                    self + field::kRetrofit, 1u, 0u);
    LinkLast(head, node, piece);
    return node;
}

// The node of a list that holds `value`, or the head node.
ListNode* FindValue(ListNode* head, const void* value)
{
    ListNode* node = head->next;
    while (node != head && node->value != value)
        node = node->next;
    return node;
}

// 0x00612300 (ret 0x14; `this` is only handed on to the calls of itself):
// the walk behind GetAllIntersectionPieces, gzcom-dll's
// "GetAllIntersectionPieces2", which is no virtual in build 641. The piece
// is referenced (slot 1) and its occupant part (slot 48) appended to the
// list (a node of the list allocator, 0x00413DD0; the head node is read
// before the allocation): the list's entries each hold one reference. Then
// the piece's cell (slot 59) and its four neighbours by the tables at
// 0x00AA7A18 and 0x00AA7A28 (x - 1, z - 1, x + 1, z + 1): a neighbour
// outside the city (unsigned compares with the city's slots 93 and 94, the
// city read from 0x00B43CEC before each) is skipped; the occupant manager's
// slot 32 gives the first occupant of the cell that the filter takes; it is
// asked for the network occupant interface; a piece with the flag 0x100000
// whose occupant part is not in the list yet and that says it is part of
// the same structure as `other` (slot 66) is walked in turn, by a call at
// this function's address. The occupant and the piece it answered are
// released.
//
// The game keeps the occupant in its `list` argument's stack slot and the
// neighbouring piece in its `piece` argument's (both arguments live in
// registers by then), so the occupant manager is handed an out word that
// holds the list's address the first time and the last occupant, released,
// after that; `found` starts the same way here. The piece's word is cleared
// before each QueryInterface. The cell's two words are not set before slot
// 59 writes them (zero here). AL is 1; the rest of EAX is not reproduced
// (neither caller reads it).
uint8_t __fastcall CollectIntersectionPieces(char* self, void*, void* piece, ListNode** list, void* occupants,
                                             void* other, void* filter)
{
    static bool logged = false;
    FirstCall("CollectIntersectionPieces", logged);
    AddRefOf(piece);
    void* const part = Virtual<void*>(piece, piece_slot::kAsOccupant);
    ListNode* const head = *list;
    LinkLast(head, CallThis<ListNode*, uint32_t, uint32_t>(addr::SC4List_AllocateNodes, list, 1u, 0u), part);
    uint32_t x = 0, z = 0;
    Virtual<void>(piece, piece_slot::kGetOccupiedCell, &x, &z);
    void* found = list;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t nx = Global<uint32_t>(addr::cSC4NetworkManager_NeighborDx + 4 * i) + x;
        const uint32_t nz = Global<uint32_t>(addr::cSC4NetworkManager_NeighborDz + 4 * i) + z;
        if (nx >= Virtual<uint32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountX))
            continue;
        if (nz >= Virtual<uint32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountZ))
            continue;
        if (!Virtual<uint8_t>(occupants, occupants_slot::kGetFirstByCityCell, &found, nx, nz, filter))
            continue;
        void* neighbor = nullptr;
        if (Virtual<uint8_t>(found, 0x0, kIidNetworkOccupant, &neighbor) &&
            Virtual<uint8_t>(neighbor, piece_slot::kHasNetworkFlag, kFlagIntersectionPart)) {
            ListNode* const first = *list;
            if (FindValue(first, Virtual<void*>(neighbor, piece_slot::kAsOccupant)) == first &&
                Virtual<uint8_t>(neighbor, piece_slot::kIsPartOfTheSameStructure, other))
                CallThis<uint8_t, void*, ListNode**, void*, void*, void*>(
                    addr::cSC4NetworkManager_CollectIntersectionPieces, self, neighbor, list, occupants, other,
                    filter);
        }
        ReleaseOf(found);
        if (neighbor)
            ReleaseOf(neighbor);
    }
    return 1;
}

// Slot 12 (0x00612780, ret 0x10): the pieces of the intersection `piece`
// belongs to, appended to the list as occupant parts, each referenced. A
// null occupant manager is the one at 0x00B43D0C; a null `other` is the
// piece. A network filter of the mask 0x1F4F is made (0x10 bytes,
// constructor 0x0061F530) and referenced; unless the piece's occupant part
// (slot 48) is in the list already, the walk 0x00612300 is called at its
// address; the filter is released (which frees it). AL is 1 and the rest of
// EAX what the filter's Release answered.
uint32_t __fastcall GetAllIntersectionPieces(char* self, void*, void* piece, ListNode** list, void* occupants,
                                             void* other)
{
    static bool logged = false;
    FirstCall("GetAllIntersectionPieces", logged);
    if (!occupants)
        occupants = Global<void*>(addr::SC4_NetworkService);
    if (!other)
        other = piece;
    void* filter = nullptr;
    if (void* const raw = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, kNetworkFilterSize))
        filter = CallThis<void*, uint32_t>(addr::cSC4NetworkManager_NetworkFilterConstruct, raw, kSurfaceMask);
    if (filter)
        AddRefOf(filter);
    ListNode* const head = *list;
    if (FindValue(head, Virtual<void*>(piece, piece_slot::kAsOccupant)) == head)
        CallThis<uint8_t, void*, ListNode**, void*, void*, void*>(addr::cSC4NetworkManager_CollectIntersectionPieces,
                                                                 self, piece, list, occupants, other, filter);
    uint32_t left = 0;
    if (filter)
        left = ReleaseOf(filter);
    return (left & 0xFFFFFF00u) | 1u;
}

// ====================================================================================
// simulators.netmgr.maps: the remap lookups
// ====================================================================================

// The rotation byte of a remap entry applied to the caller's rotation: with
// bit 7 set the new rotation is the byte less the old plus 8, else the old
// plus the byte; both kept to the bits 0x83, in byte arithmetic.
void Turn(char* entry, uint8_t* rotation)
{
    const uint8_t turn = At<uint8_t>(entry, remap::kRotation);
    if (turn & 0x80)
        *rotation = static_cast<uint8_t>(static_cast<uint8_t>(turn - *rotation + 8) & 0x83);
    else
        *rotation = static_cast<uint8_t>(static_cast<uint8_t>(*rotation + turn) & 0x83);
}

// Slot 13 (0x00610AD0, ret 8: an ID and the address of a rotation byte;
// gzcom-dll's header has it at slot 14): the entries of the vector at +0x44
// in order. An entry matches when the ID and its mask give its match word;
// the ID then becomes the base plus (ID and the mask at +0xC) less (ID and
// the mask at +0x10), the rotation is turned, and unless the entry's byte
// at +0x15 says to go on the walk ends. Later entries see the new ID. The
// answer is the ID as it is at the end (unchanged when nothing matched; the
// rotation is only read or written for a match).
uint32_t __fastcall MapPreviewTexture(char* self, void*, uint32_t id, uint8_t* rotation)
{
    static bool logged = false;
    FirstCall("MapPreviewTexture", logged);
    const Vector* const entries = reinterpret_cast<const Vector*>(self + field::kPreviewRemap);
    char* const end = entries->end;
    for (char* entry = entries->begin; entry != end; entry += remap::kSize) {
        if ((At<uint32_t>(entry, remap::kMask) & id) != At<uint32_t>(entry, remap::kMatch))
            continue;
        const uint32_t drop = At<uint32_t>(entry, remap::kDrop) & id;
        const uint32_t keep = At<uint32_t>(entry, remap::kKeep) & id;
        id = At<uint32_t>(entry, remap::kBase) + (keep - drop);
        Turn(entry, rotation);
        if (!At<uint8_t>(entry, remap::kGoOn))
            break;
    }
    return id;
}

// The first node of the first bucket after `index` that has one, or null
// (the game's two loops read the bucket count once or at every step; nothing
// changes the table meanwhile).
char* NextBucketNode(char* table, uint32_t index)
{
    for (;;) {
        ++index;
        if (index >= BucketCount(table))
            return nullptr;
        if (char* const node = At<char*>(At<char*>(table, edge::kBuckets), index * 4))
            return node;
    }
}

// What 0x00611970 fills: the first node of a key and the node after its
// last, each with the table.
struct EdgeRange {
    char* first;
    char* table;
    char* last;
    char* tableAgain;
};

// 0x00611970 (ret 8: the out range and the address of the key; `this` is
// the table at the manager's +0x30; MapShadowTexture is its only caller):
// the equal range of a key in the hash table. The key's bucket is the key
// modulo the bucket count (unsigned); the first node of the chain with the
// key is the range's start (null, with a null end, when there is none). The
// end is the first node after it in the chain with another key, or, when
// the chain ends first, the first node of the next bucket that has one
// (null when no later bucket has). The table goes into the second and the
// fourth word. EAX is the out range. ECX is kept by the game's function.
EdgeRange* __fastcall FindShadowEdges(char* table, void*, EdgeRange* range, const uint32_t* keyAt)
{
    static bool logged = false;
    FirstCall("FindShadowEdges", logged);
    const uint32_t key = *keyAt;
    const uint32_t index = key % BucketCount(table);
    char* node = At<char*>(At<char*>(table, edge::kBuckets), index * 4);
    while (node && At<uint32_t>(node, edge::kKey) != key)
        node = At<char*>(node, edge::kNext);
    char* last = nullptr;
    if (node) {
        last = At<char*>(node, edge::kNext);
        while (last && At<uint32_t>(last, edge::kKey) == key)
            last = At<char*>(last, edge::kNext);
        if (!last)
            last = NextBucketNode(table, index);
    }
    range->first = node;
    range->table = table;
    range->last = last;
    range->tableAgain = table;
    return range;
}

// Slot 14 (0x00611D50, ret 0x10: an ID, the address of a rotation byte, a
// network flag and an edge key; gzcom-dll's header has it at slot 15): the
// entries of the vector at +0x50 in order, matched as the preview's. Only
// the bits 0x304 of the flag are kept (highway, light rail, monorail).
//   - An entry with a base: the ID becomes (ID and the mask at +0xC) less
//     (ID and the mask at +0x10) plus the base.
//   - An entry with base 0 looks the edge key up in the table at +0x30 (the
//     equal range 0x00611970, at its address) and walks the range (a node's
//     successor is its link or, at the end of a chain, the first node of
//     the next bucket that has one after the bucket of the node's key) for
//     the first node whose network type's flag (the table at 0x00AA79E4,
//     indexed by the node's type as it is) equals the kept flag bits. With
//     one, the ID becomes (ID and the node's mask) plus the node's base and
//     the rotation the node's byte. Without one the entry does nothing and
//     the walk goes on.
// Then the rotation is turned by the entry's byte, and unless the entry's
// byte at +0x15 says to go on the walk ends. The answer is the ID as it is
// at the end.
uint32_t __fastcall MapShadowTexture(char* self, void*, uint32_t id, uint8_t* rotation, uint32_t flag, uint32_t key)
{
    static bool logged = false;
    FirstCall("MapShadowTexture", logged);
    const Vector* const entries = reinterpret_cast<const Vector*>(self + field::kShadowRemap);
    char* const end = entries->end;
    flag &= kShadowFlagMask;
    for (char* entry = entries->begin; entry != end; entry += remap::kSize) {
        if ((At<uint32_t>(entry, remap::kMask) & id) != At<uint32_t>(entry, remap::kMatch))
            continue;
        const uint32_t base = At<uint32_t>(entry, remap::kBase);
        if (base != 0) {
            id = (At<uint32_t>(entry, remap::kKeep) & id) - (At<uint32_t>(entry, remap::kDrop) & id) + base;
        } else {
            EdgeRange range;
            CallThis<EdgeRange*, EdgeRange*, uint32_t*>(addr::cSC4NetworkManager_FindShadowEdges,
                                                        self + field::kShadowEdges, &range, &key);
            char* node = range.first;
            char* const last = range.last;
            if (node == last)
                continue;
            char* const table = range.table;
            bool matched = false;
            do {
                const uint32_t type = At<uint32_t>(node, edge::kType);
                if (Global<uint32_t>(addr::cSC4NetworkManager_TypeFlags + type * 4u) == flag) {
                    matched = true;
                    break;
                }
                char* next = At<char*>(node, edge::kNext);
                if (!next)
                    next = NextBucketNode(table, At<uint32_t>(node, edge::kKey) % BucketCount(table));
                node = next;
            } while (node != last);
            if (!matched)
                continue;
            id = (At<uint32_t>(node, edge::kMask) & id) + At<uint32_t>(node, edge::kBase);
            *rotation = At<uint8_t>(node, edge::kRotation);
        }
        Turn(entry, rotation);
        if (!At<uint8_t>(entry, remap::kGoOn))
            break;
    }
    return id;
}

// Slot 15 (0x00610B40, ret 4; gzcom-dll's header has it at slot 16): with
// the byte at +0x11 clear the ID as it is (ECX and EDX untouched). Else the
// first entry of the vector at +0x5C the ID matches gives its base plus (ID
// and the mask at +0xC); without a match the ID.
uint32_t __fastcall MapPath(char* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("MapPath", logged);
    if (!At<uint8_t>(self, field::kLeftHand))
        return id;
    const Vector* const entries = reinterpret_cast<const Vector*>(self + field::kPathRemap);
    char* const end = entries->end;
    for (char* entry = entries->begin; entry != end; entry += remap::kPathSize)
        if ((At<uint32_t>(entry, remap::kMask) & id) == At<uint32_t>(entry, remap::kMatch))
            return At<uint32_t>(entry, remap::kBase) + (At<uint32_t>(entry, remap::kKeep) & id);
    return id;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETMGR_DETOUR(address, fn)

const Detour kIdentity[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkManager_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_Release, Release),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_GetGZCLSID, GetGZCLSID),
};

const Detour kQueries[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkManager_IsOccupantSubwayStation, IsOccupantSubwayStation),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_FindPipeOccupant, FindPipeOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_FindPipeConnectionOccupant, FindPipeConnectionOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_GetNetworkTool, GetNetworkTool),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_GetEnglishNetworkName, GetEnglishNetworkName),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_GetWaterOccupantFilter, GetWaterOccupantFilter),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_GetSubwayOccupantFilter, GetSubwayOccupantFilter),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_WaterFilterDeletingDestructor, WaterFilterDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_SubwayFilterDeletingDestructor, SubwayFilterDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_RegisterConstructionCrew, RegisterConstructionCrew),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_UnregisterConstructionCrew, UnregisterConstructionCrew),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_AllocateRetrofitNodes, AllocateRetrofitNodes),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_AddToRetrofitList, AddToRetrofitList),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_CollectIntersectionPieces, CollectIntersectionPieces),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_GetAllIntersectionPieces, GetAllIntersectionPieces),
};

const Detour kMaps[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkManager_MapPreviewTexture, MapPreviewTexture),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_FindShadowEdges, FindShadowEdges),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_MapShadowTexture, MapShadowTexture),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_MapPath, MapPath),
};

bool InstallIdentity(registry::Mode) { return InstallDetours(kIdentity, "identity"); }
bool InstallQueries(registry::Mode) { return InstallDetours(kQueries, "queries"); }
bool InstallMaps(registry::Mode) { return InstallDetours(kMaps, "maps"); }

const bool g_registered[] = {
    registry::Register({"simulators.netmgr.identity", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_QueryInterface,
                        "Our network manager identity: QueryInterface, Release and GetGZCLSID (3 detours; "
                        "src/core/netmgr_state.cpp)",
                        false, InstallIdentity}),
    registry::Register({"simulators.netmgr.queries", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_GetNetworkTool,
                        "Our network manager queries: the subway station test and the pipe lookups (forwards), "
                        "GetNetworkTool, the English names, the water and subway filters with their destructors, the "
                        "construction crews, the retrofit list's append with its node allocation and "
                        "GetAllIntersectionPieces with its walk (15 detours; src/core/netmgr_state.cpp)",
                        false, InstallQueries}),
    registry::Register({"simulators.netmgr.maps", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_MapPreviewTexture,
                        "Our network manager remap lookups: MapPreviewTexture, MapShadowTexture with the shadow edge "
                        "table's equal range, MapPath (4 detours; src/core/netmgr_state.cpp)",
                        false, InstallMaps}),
};

} // namespace
} // namespace opensc4::netmgr_sim
