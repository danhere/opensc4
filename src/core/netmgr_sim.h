// Shared between the parts of our cSC4NetworkManager (build 641): the class
// map, the vtables slot by slot (in the TRUE order, see below), the fields
// and the containers it keeps. The manager is the city's desk for the
// networks: it holds the thirteen network tools (one per network type), the
// list of the pieces that are "managed globally", the three remap tables of
// the ini resource (preview textures, shadow textures with their edge keys,
// paths for left-hand traffic), the queue of removals, the retrofit list and
// the construction crews; it answers the lookups the tools and the lot code
// ask (a pipe at a cell, a subway station, the pieces of one intersection)
// and carries out removals and the lots' connection nodes. It has no grid
// and nothing sized from the city: the two places that need the city's size
// ask the city (slots 93 and 94).
//
// Written in three parts: netmgr_state.cpp (identity, the queries, the remap
// lookups, the crews, the filters, the retrofit list's append and the
// intersection walk), netmgr_life.cpp (life and save: the factory, the
// constructor, the destructors, Init, Shutdown, LoadRemapping with its four
// callbacks and the inserts they use, the release helpers, Write, Read, and
// our own object) and the messages, removals and nodes (DoMessage,
// ProcessRetrofitList, DestroyNetworkOccupants, QueueRemovals,
// RemoveOrReplaceOccupant, CreateConnectionNode, AutoconnectLotNodes), which
// are to come.
//
// The class: class ID 0xC990BD46 (registered at 0x004603C5 with the factory
// 0x00613750), 0xBC bytes, constructor 0x00613330 (only reached by the jump
// from the factory), destructor 0x00612000, deleting destructor 0x00612590.
// Code 0x00610530 to 0x006140FC, plus the two filters' deleting destructors
// at 0x006104D0 and 0x00610500. The city makes one per city unless the save
// brought one, and keeps it at +0x200; the city's slot 38 (+0x98) and
// SC4::GetNetworkManager 0x00602680 give it. No global pointer of its own.
//
// QueryInterface (0x00610750): IIDs 0x00000001 and 0xA9764C8C
// (cISC4NetworkManager) the object; 0x452294AA (cIGZMessageTarget2) +0x4;
// 0xE4FDA3D4 (cIGZSerializable) +0x8.
//
// --- +0x0: cISC4NetworkManager, vtable 0x00AA7AD0, 25 slots ----------------
// gzcom-dll's cISC4NetworkManager.h is right up to slot 12 and one slot off
// after it: its GetAllIntersectionPieces2 is no virtual in build 641 (it is
// the helper 0x00612300, with exactly the header's five arguments), so every
// name after it sits one slot too high in the header, and the last slot is
// the deleting destructor. Argument counts are each function's own `ret N`.
//    0 QueryInterface(iid, out) 0x00610750
//    1 AddRef 0x004664A0 (shared code: ++[+0xC])
//    2 Release 0x006107D0               3 Init 0x00610C10
//    4 Shutdown 0x00611EB0
//    5 GetNetworkTool(type, unique) 0x006108D0
//    6 DestroyNetworkOccupants(list) 0x00612110
//    7 QueueRemovals(queue) 0x00613450
//    8 RemoveOrReplaceOccupant(tool, occupant, out) 0x006110B0
//    9 IsOccupantSubwayStation(occupant) 0x00610540
//   10 FindPipeOccupant(x, z, out) 0x00610550
//   11 FindPipeConnectionOccupant(x, z, out) 0x00610570
//   12 GetAllIntersectionPieces(piece, list, occupant manager, other)
//      0x00612780
//   13 MapPreviewTexture(id, rotation&) 0x00610AD0      [header: slot 14]
//   14 MapShadowTexture(id, rotation&, network flag, edge key) 0x00611D50
//                                                       [header: slot 15]
//   15 MapPath(id) 0x00610B40                           [header: slot 16]
//   16 RegisterConstructionCrew(crew) 0x006122B0        [header: slot 17]
//   17 UnregisterConstructionCrew(crew) 0x006112A0      [header: slot 18]
//   18 GetWaterOccupantFilter 0x00610590                [header: slot 19]
//   19 GetSubwayOccupantFilter 0x006105C0               [header: slot 20]
//   20 CreateConnectionNode(out&, type, point&, a, b, c, flag) 0x00611300
//                                                       [header: slot 21]
//   21 AutoconnectLotNodes(lot, list) 0x00611530        [header: slot 22]
//   22 GetEnglishNetworkName(type) 0x006107F0           [header: slot 23]
//   23 AddToRetrofitList(piece) 0x00612480              [header: slot 24]
//   24 the deleting destructor 0x00612590               [not in the header]
// --- +0x4: cIGZMessageTarget2, vtable 0x00AA7ABC, 4 slots -----------------
//    thunks 0x006120D0, 0x006C2270 (shared), 0x006120E0; 3 DoMessage
//    0x00613950
// --- +0x8: cIGZSerializable, vtable 0x00AA7AA4, 6 slots -------------------
//    thunks 0x006120F0, 0x006C2290 (shared), 0x00612100; 3 Write 0x006111E0;
//    4 Read 0x006124C0; 5 GetGZCLSID 0x00610530
//
// Not virtual, in the first part: the shadow edge table's equal range
// 0x00611970 (this the table at +0x30; MapShadowTexture only), the retrofit
// list's node allocation 0x00611240 (AddToRetrofitList and the constructor)
// and the intersection walk 0x00612300 (GetAllIntersectionPieces and
// itself).
//
// Not virtual, in the second part: the factory 0x00613750, the constructor
// 0x00613330 and the destructor 0x00612000; LoadRemapping 0x00613140 (the
// constructor only) with the four callbacks the ini reader calls, cdecl
// (key string, value string, the manager): 0x00612F70 "Texture remapping",
// 0x00612EB0 "Shadow remapping", 0x00613030 "Shadow edge remapping" and
// 0x006130E0 "UK path remapping"; the two appends 0x006125B0 (an edge into
// the table at +0x30, through the table's insert 0x00611890) and 0x00612610
// (a path entry); ReleaseTools 0x00610880 (cdecl: first, last, the address
// of a tool pointer: every slot of the range is given that pointer),
// ShutdownCrews 0x00610B80, and the retrofit list's clear 0x00611A20 and
// destroy 0x00611AA0 (this the list's word at +0x78).
//
// The two filters the manager makes (slots 18 and 19) are classes of their
// own, 0xC bytes on the occupant filter base (constructor 0x005EA080,
// reference count at +8, AddRef 0x004BDF00, Release 0x007CB920, which calls
// slot 6 with 1): the water filter with the vtables 0x00AA7A54 and
// 0x00AA7A3C (its tests 0x00648C40 and 0x00648CC0 are in the pipe tool's
// code) and the subway filter with 0x00AA7A88 and 0x00AA7A70 (0x00653C00 and
// 0x00653CC0, in the subway tool's). A filter's slots: 0 QueryInterface, 1
// AddRef, 2 Release, 3 IsOccupantIncluded(occupant), 4
// IsOccupantTypeIncluded(type) (0x004FC900: always 1), 5
// IsPropertyHolderIncluded(holder), 6 the deleting destructor (0x006104D0
// and 0x00610500, the only code of the two classes in this class's range).
//
// Fields (offsets from the object's start; DoMessage gets object + 4 and
// Write/Read object + 8). Evidence: the constructor 0x00613330 and the
// functions named.
//   +0x0C uint32 reference count (unsigned compare in Release)
//   +0x10 uint8  initialized (Init sets it, Shutdown clears it)
//   +0x11 uint8  set by Init from a service's answer; MapPath maps only
//                when it is set (the ini section is "UK path remapping":
//                left-hand traffic)
//   +0x12 uint8  batching cell changes (DoMessage)
//   +0x14 int32[4] the dirty cell rectangle: min x, min z, max x, max z;
//                empty is 0x7FFFFFFF, 0x7FFFFFFF, -1, -1
//   +0x24 vector of pieces (begin, end, capacity): the globally managed
//                pieces, referenced; the save record
//   +0x30 the shadow edge table: an STLport hash table (buckets begin +0x34,
//                end +0x38, capacity +0x3C; count +0x40; made with 100
//                buckets), nodes of 0x18 bytes: next, key, network type,
//                base, mask, rotation byte
//   +0x44 vector of 0x18-byte preview texture entries
//   +0x50 vector of 0x18-byte shadow texture entries (the same shape)
//   +0x5C vector of 0x10-byte path entries
//   +0x68 vector of occupants queued for removal (referenced), +0x74 uint8
//                queueing
//   +0x78 the retrofit list: one pointer to the head node of a list of
//                0xC-byte nodes (next, previous, piece; referenced) from the
//                allocator cached at 0x00B45238
//   +0x7C vector of construction crews (referenced)
//   +0x88 the 13 network tools, by network type
// Never written by the constructor (as operator new left them): +0x13, the
// table's first word at +0x30 and +0x75 to +0x77.
// A remap entry (+0x44 and +0x50): +0 match, +4 mask (an ID matches when ID
// & mask == match), +8 base, +0xC and +0x10 two masks (the new ID is base +
// (ID & mask C) - (ID & mask 10)), +0x14 a rotation byte (bit 7: the
// rotation is mirrored: 8 + the byte less the old rotation; else added;
// always & 0x83), +0x15 a byte: non-zero goes on to the next entries with
// the new ID. A path entry (+0x5C): +0 match, +4 mask, +8 base, +0xC mask
// (the new ID is base + (ID & mask C)).
//
// The save record (type = the class ID; Write 0x006111E0, Read 0x006124C0):
// uint16 version 1, uint32 count, then count references: the vector at +0x24
// in its order (tools/re/save_schemas/netmgr.py).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::netmgr_sim {

inline constexpr uint32_t kClassId = 0xC990BD46;
inline constexpr uint32_t kIidUnknown = 0x00000001;
inline constexpr uint32_t kIidManager = 0xA9764C8C;
inline constexpr uint32_t kIidMessageTarget = 0x452294AA;
inline constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
inline constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
inline constexpr uint32_t kIidPipe = 0x49C05B9F;           // FindPipeOccupant asks the cell's occupants for it
inline constexpr uint32_t kIidPipeConnection = 0xEA0A5ACF; // FindPipeConnectionOccupant

inline constexpr std::size_t kObjectSize = 0xBC;
inline constexpr std::size_t kMessagePart = 0x4;
inline constexpr std::size_t kSerializablePart = 0x8;
inline constexpr std::size_t kSlots = 25;
inline constexpr std::size_t kMessageSlots = 4;
inline constexpr std::size_t kSerializableSlots = 6;

// The network types: 0 road, 1 rail, 2 highway, 3 street, 4 pipe, 5 power
// line, 6 avenue, 7 subway, 8 light rail, 9 monorail, 10 one-way road, 11
// dirt road, 12 ground highway. A type's flag is 1 << type (the table at
// 0x00AA79E4).
inline constexpr uint32_t kTypes = 13;
inline constexpr uint32_t kTypePipe = 4, kTypePowerLine = 5, kTypeSubway = 7;

// The mask of the filter GetAllIntersectionPieces makes (every type but
// pipe, power line and subway), the flag of a piece of an intersection of
// several cells, and the bits MapShadowTexture keeps of its network flag
// (highway, light rail, monorail).
inline constexpr uint32_t kSurfaceMask = 0x1F4F;
inline constexpr uint32_t kFlagIntersectionPart = 0x00100000;
inline constexpr uint32_t kShadowFlagMask = 0x304;

// Sizes of the tool objects GetNetworkTool makes.
inline constexpr std::size_t kToolSize = 0x368, kPipeToolSize = 0x398, kPowerLineToolSize = 0x3B8;
inline constexpr std::size_t kFilterSize = 0xC, kNetworkFilterSize = 0x10;

// The save record's version, the number of buckets the constructor asks the
// shadow edge table for (the table takes the next of its primes), the size
// of a table node, and the key of the remap resource (type 0, group
// 0x8A5971C5, instance 0x8A5993B9: an ini text).
inline constexpr uint16_t kRecordVersion = 1;
inline constexpr uint32_t kEdgeBuckets = 100;
inline constexpr std::size_t kEdgeNodeSize = 0x18;
inline constexpr uint32_t kRemapGroup = 0x8A5971C5, kRemapInstance = 0x8A5993B9;

// The language service Init asks (class and IID), and the eleven messages
// Init subscribes the part at +4 to, in Init's order. Shutdown takes them
// off in the same order but for the sixth and the seventh, which it swaps.
inline constexpr uint32_t kLanguageClass = 0x441E5070, kIidLanguage = 0x0054B7D5;
inline constexpr uint32_t kMessages[] = {0xA6B79602, 0x278E6003, 0xEA3B0AF1, 0x6A3B0AFD, 0x6AB97795, 0x26D31EC2,
                                         0x26D31EC1, 0x99EF1142, 0x99EF1143, 0xC9DA96E8, 0xC9DA96E9};

namespace field {
inline constexpr std::size_t kRefCount = 0x0C;
inline constexpr std::size_t kInitialized = 0x10;
inline constexpr std::size_t kLeftHand = 0x11;
inline constexpr std::size_t kBatching = 0x12;
inline constexpr std::size_t kDirtyRect = 0x14;
inline constexpr std::size_t kManaged = 0x24;
inline constexpr std::size_t kShadowEdges = 0x30;
inline constexpr std::size_t kPreviewRemap = 0x44;
inline constexpr std::size_t kShadowRemap = 0x50;
inline constexpr std::size_t kPathRemap = 0x5C;
inline constexpr std::size_t kQueue = 0x68;
inline constexpr std::size_t kQueueing = 0x74;
inline constexpr std::size_t kRetrofit = 0x78;
inline constexpr std::size_t kCrews = 0x7C;
inline constexpr std::size_t kTools = 0x88;
} // namespace field

// A remap entry of the vectors at +0x44 and +0x50, a path entry of the
// vector at +0x5C, a node of the shadow edge table and the table itself.
namespace remap {
inline constexpr std::size_t kMatch = 0x0, kMask = 0x4, kBase = 0x8, kKeep = 0xC, kDrop = 0x10;
inline constexpr std::size_t kRotation = 0x14, kGoOn = 0x15, kSize = 0x18;
inline constexpr std::size_t kPathSize = 0x10;
} // namespace remap
namespace edge {
inline constexpr std::size_t kNext = 0x0, kKey = 0x4, kType = 0x8, kBase = 0xC, kMask = 0x10, kRotation = 0x14;
inline constexpr std::size_t kBuckets = 0x4, kBucketsEnd = 0x8, kCount = 0x10;
} // namespace edge

// The manager's slots (byte offsets in the vtable 0x00AA7AD0), in the true
// order.
namespace slot {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kInit = 0x0C, kShutdown = 0x10;
inline constexpr std::size_t kGetNetworkTool = 0x14, kDestroyNetworkOccupants = 0x18, kQueueRemovals = 0x1C;
inline constexpr std::size_t kRemoveOrReplaceOccupant = 0x20, kIsOccupantSubwayStation = 0x24;
inline constexpr std::size_t kFindPipeOccupant = 0x28, kFindPipeConnectionOccupant = 0x2C;
inline constexpr std::size_t kGetAllIntersectionPieces = 0x30, kMapPreviewTexture = 0x34;
inline constexpr std::size_t kMapShadowTexture = 0x38, kMapPath = 0x3C, kRegisterConstructionCrew = 0x40;
inline constexpr std::size_t kUnregisterConstructionCrew = 0x44, kGetWaterOccupantFilter = 0x48;
inline constexpr std::size_t kGetSubwayOccupantFilter = 0x4C, kCreateConnectionNode = 0x50;
inline constexpr std::size_t kAutoconnectLotNodes = 0x54, kGetEnglishNetworkName = 0x58;
inline constexpr std::size_t kAddToRetrofitList = 0x5C, kDeletingDestructor = 0x60;
// The serializable part's (+0x8).
inline constexpr std::size_t kWrite = 0x0C, kRead = 0x10, kGetGZCLSID = 0x14;
} // namespace slot

// The slots of the services and streams the second part calls: the message
// server (0x00B43CCC; both take the target and the message ID), the
// language service and its utility, a stream (the same offsets for writing
// and reading), a crew and the node allocator.
namespace server_slot {
inline constexpr std::size_t kAddNotification = 0x14, kRemoveNotification = 0x18;
} // namespace server_slot
namespace language_slot {
inline constexpr std::size_t kGetLanguageUtility = 0x98; // slot 38: (language ID; 0 the current)
inline constexpr std::size_t kGetRoadDrivingSide = 0x64; // the utility's slot 25: no arguments
} // namespace language_slot
namespace stream_slot {
inline constexpr std::size_t kUint16 = 0x1C, kUint32 = 0x24, kGetError = 0x48;
} // namespace stream_slot
namespace crew_slot {
inline constexpr std::size_t kShutdown = 0x14; // slot 5: the crew unregisters itself
} // namespace crew_slot
namespace allocator_slot {
inline constexpr std::size_t kAllocate = 0xC, kFree = 0x10;
} // namespace allocator_slot

// An occupant filter's slots (the two the manager makes and the network
// filter 0x0061F530).
namespace filter {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kIsOccupantIncluded = 0x0C;
inline constexpr std::size_t kIsOccupantTypeIncluded = 0x10, kIsPropertyHolderIncluded = 0x14;
inline constexpr std::size_t kDeletingDestructor = 0x18;
} // namespace filter

// The slots of other objects the manager's code calls: a network piece
// (cISC4NetworkOccupant; src/core/netocc_sim.h has the whole table), the
// occupant manager (src/core/occmgr_sim.h) and the city.
namespace piece_slot {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kHasNetworkFlag = 0x20, kAsOccupant = 0xC0;
inline constexpr std::size_t kGetOccupiedCell = 0xEC, kIsPartOfTheSameStructure = 0x108;
inline constexpr std::size_t kGetComplexPieceOrigin = 0x11C;
} // namespace piece_slot
namespace occupants_slot {
inline constexpr std::size_t kGetFirstByCityCell = 0x80; // slot 32: (out, x, z, filter)
} // namespace occupants_slot
namespace city_slot {
inline constexpr std::size_t kCellCountX = 0x174, kCellCountZ = 0x178; // slots 93 and 94
} // namespace city_slot

// A vector as the game keeps it.
struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

// A node of the game's lists of references (the retrofit list, the lists
// GetAllIntersectionPieces fills): next, previous, the object.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// A string of the game as the ini reader hands it to a callback and as
// LoadRemapping makes one on its stack (0x14 bytes; constructor 0x00408480
// from a text; vtable 0x00A80810): the characters are at `begin`.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// A game function at its address: thiscall with `self` in ECX, or cdecl.
template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// Logs "netmgr: first <name>" once per flag (netmgr_state.cpp).
void FirstCall(const char* name, bool& logged);

// Shared by the parts (netmgr_state.cpp has them). A new network tool of the
// general class (0x368 bytes, constructor 0x0062C5F0 with the network type)
// and one of the three classes of their own (the size and the constructor,
// which takes no argument); each null when operator new gives none, else the
// constructor's EAX. The number of buckets of the shadow edge table.
void* MakeTool(uint32_t type);
void* MakeOwnTool(std::size_t size, std::uintptr_t constructor);
uint32_t BucketCount(char* table);

// A detour of a group: the game's address and our function. Skipped says
// whether OPENSC4_NETMGR_SKIP (function addresses, hex, eight digits) lists
// the address; InstallDetours installs a group's list but for those and logs
// "netmgr <what>: <n> detours".
struct Detour {
    std::uintptr_t address;
    void* ours;
};
#define OPENSC4_NETMGR_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}
bool Skipped(std::uintptr_t address);
bool InstallDetourList(const Detour* detours, std::size_t count, const char* what);
template <std::size_t N>
inline bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    return InstallDetourList(detours, N, what);
}

} // namespace opensc4::netmgr_sim
