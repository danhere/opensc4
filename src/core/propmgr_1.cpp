// Our cSC4PropManager, first half (build 641): the identity and lifetime
// functions, the prop lists of the city, the street, the lots and the
// buildings (the parts that need no exemplar), the prop filter, and the
// queries of the type, family and profile tables. The second half of the
// class (0x004A3040 to 0x004A44E0: CreateProp, removal by area, the message
// handler, the exemplar loaders, Init) is another file's.
//
// The class: class ID 0xA990BD10, 0x28 bytes (the factory 0x004A28E0
// allocates it with the game's pool operator new), made once per city by
// the game's class table (the factory's address is stored in a registration
// function, 0x00460090). Three parts, each with its own vtable:
//   +0x00 cISC4PropManager, vtable 0x00A8F928, 44 slots (the gzcom-dll
//         header lists the overloaded pairs in the other order: the first
//         of each pair takes the numbers, the second the rectangle or the
//         lot or the building)
//   +0x04 cIGZMessageTarget2, vtable 0x00A8F918, 4 slots (thunks of
//         QueryInterface 0x004A21F0, AddRef 0x0068D230, Release 0x004A2710;
//         3 DoMessage 0x004A34E0)
//   +0x08 cIGZSerializable, vtable 0x00A8F900, 6 slots (thunks 0x004A2200,
//         0x0068D250, 0x004A2720; 3 Write 0x004A2430, 4 Read 0x004A2470,
//         5 GetGZCLSID 0x004A1D20); Write and Read get the part (object +8)
// Fields (offsets from the object's start):
//   +0x0C uint8  initialized (Init sets it, Shutdown clears it)
//   +0x10 uint32 reference count (starts at 0; Release at 1 or 0 shuts down
//         and deletes)
//   +0x14 the occupant manager (global 0x00B43D0C), referenced
//   +0x18 the resource manager (global 0x00B43CA8), referenced
//   +0x1C the prop filter, referenced: a counted object of 0x14 bytes, made
//         by Init (see below)
//   +0x20 the lot manager (global 0x00B43D08), referenced
//   +0x24 an object from the service at 0x00B43DC4, referenced
// The object is saved with a version word (1) and two references: the
// occupant manager and the lot manager (Write and Read).
//
// The prop filter (made at 0x004A4441 by Init): 0x14 bytes, vtable 0x00A8F8E4
// (7 slots; slot 3 is IsIncluded, 0x004A2210) at +0, vtable 0x00A8F8CC (6
// slots, thunks and shared code) at +4, the reference count at +8, the mode
// at +0xC (0 any prop, else the prop usage that must match: 1 city, 2 lot,
// 3 building, 4 street) and the building at +0x10 (mode 3: only the props
// listed in that building's prop list, when it is set). The occupant
// manager calls slot 3 for every occupant of a query.
//
// The tables (globals the class owns; STLport hash tables: +4 the bucket
// array's begin, +8 its end, +0x10 the number of elements; a node is the
// next pointer, then the key at +4):
//   0x00B21760 the prop types: key, then the resource key (3 dwords) at +8
//   0x00B21774 the prop families: key, then a vector of types at +8
//   0x00B2179C the prop profiles: key, then the profile at +8
//   0x00B21788 the prop names (read through the map operator 0x004862F0,
//              the second half)
//   0x00B21758 a flag: the clean-up 0x004A2A30 is registered with the
//              service at 0x00B43C94
//
// Every virtual call below has its argument count from the callee's own
// `ret N`: the prop (0x00AA4900), the occupant part (0x00AA4868), the
// property holder (0x00AA47E8), the occupant manager (0x00A8F7F0), the
// resource manager (key, interface, out, 0, 0 as the other files call it).
// Not checked: the placeable object's slot 11 (0x004A2690: no push before
// the call) and the stream's slots 7 and 18, which are called as the other
// files call them.
//
// Replacement, off by default:
//
//   simulators.propmgr.part1: everything but the list in the next
//   paragraph.
//
// Still the game's: the four adjustor thunks 0x004A21F0, 0x004A2200 (they
// jump to QueryInterface) and 0x004A2710, 0x004A2720 (they jump to Release)
// and 0x004A29B0 (the insert of a 0x24-byte
// record into a hash table of the same kind as 0x004A2930, called from
// another class too, 0x0065FB41).
//
// Oddities kept: QueryInterface answers 1 (cIGZUnknown) with the message
// target part, not with the object; GetPropTypesByFamily returns 0 when the
// ID is a type (the vector then holds that one type); the prop filter's
// IsIncluded finds a building's prop list by pointer; AddLotProp ignores
// its lot argument; CreateGenericProp does not test the resource manager's
// answer (it tests the exemplar it left, which we start at null; the game
// leaves the local unset).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::propmgr {
namespace {

namespace field {
constexpr std::size_t kInitialized = 0x0C;
constexpr std::size_t kReferences = 0x10;
constexpr std::size_t kOccupants = 0x14;
constexpr std::size_t kResources = 0x18;
constexpr std::size_t kFilter = 0x1C;
constexpr std::size_t kLots = 0x20;
constexpr std::size_t kService = 0x24;
} // namespace field

namespace filter {
constexpr std::size_t kMode = 0xC;
constexpr std::size_t kBuilding = 0x10;
} // namespace filter

namespace usage { // cISC4PropOccupant::GetPropUsage
constexpr uint32_t kCity = 1;
constexpr uint32_t kLot = 2;
constexpr uint32_t kBuilding = 3;
constexpr uint32_t kStreet = 4;
} // namespace usage

namespace table { // hash tables
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kBucketsEnd = 0x8;
constexpr std::size_t kCount = 0x10;
constexpr std::size_t kKey = 0x4;
constexpr std::size_t kValue = 0x8;
} // namespace table

// The prop (cISC4PropOccupant, vtable 0x00AA4900) slots.
namespace prop {
constexpr std::size_t kAsOccupant = 0x0C;
constexpr std::size_t kSetBoundingBox = 0x10;
constexpr std::size_t kGetPropType = 0x1C;
constexpr std::size_t kSetOrientation = 0x28;
constexpr std::size_t kGetPropUsage = 0x58;
constexpr std::size_t kSetPropUsage = 0x5C;
constexpr std::size_t kExemplarPart = 0x24; // the object part: its slot 3 sets the exemplar
constexpr std::size_t kSetExemplar = 0x0C;
} // namespace prop

// The occupant (cISC4Occupant, vtable 0x00AA4868) slots.
namespace occupant {
constexpr std::size_t kQueryInterface = 0x00;
constexpr std::size_t kInit = 0x0C;
constexpr std::size_t kShutdown = 0x10;
constexpr std::size_t kAsPropertyHolder = 0x18;
constexpr std::size_t kGetType = 0x1C;
constexpr std::size_t kSetPosition = 0x24;
constexpr std::size_t kGetPlaceable = 0x4C;
constexpr std::size_t kClearFlag = 0x7C;
constexpr std::size_t kIsFlagSet = 0x80;
} // namespace occupant

// The occupant manager (vtable 0x00A8F7F0) slots used here.
namespace occmgr {
constexpr std::size_t kInsert = 0x50;
constexpr std::size_t kRemove = 0x54;
constexpr std::size_t kGetFirstByCityCellByType = 0x84;
constexpr std::size_t kGetByBBox = 0x90;
constexpr std::size_t kGetByCityCells = 0xA0;
} // namespace occmgr

// The property holder (vtable 0x00AA47E8) slots.
namespace holder {
constexpr std::size_t kQueryInterface = 0x00;
constexpr std::size_t kGetPropertyAsInterface = 0x18; // (property, interface, out)
constexpr std::size_t kRemoveProperty = 0x44;         // (property, 0)
constexpr std::size_t kGetProperty = 0x24;            // (property) the property or null
} // namespace holder

// The prop manager's own slots (vtable 0x00A8F928).
namespace slot {
constexpr std::size_t kShutdown = 0x10;
constexpr std::size_t kCreateProp = 0x3C;
constexpr std::size_t kGetLotProps = 0x60;
constexpr std::size_t kGetBuildingProps = 0x8C;
constexpr std::size_t kRemoveBuildingProps = 0x98;
constexpr std::size_t kDeletingDestructor = 0xA0;
} // namespace slot

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidManager = 0x28326F3E;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kIidExemplar = 0xA52160F5;
constexpr uint32_t kIidExemplarHolder = 0x0AC2B5F7;
constexpr uint32_t kIidOccupantManager = 0xC6BB8334;
constexpr uint32_t kIidLotManager = 0x681905BD;
constexpr uint32_t kClassId = 0xA990BD10;
constexpr uint32_t kPropClassDefault = 0x2977AA47;  // cSC4PropOccupant
constexpr uint32_t kPropClassDecal = 0x2977AA48;    // the terrain decal
constexpr uint32_t kPropClassEffect = 0x2977AA49;   // the visual effect
constexpr uint32_t kPropertyPropClass = 0xA977A86B; // the exemplar's occupant class
constexpr uint32_t kPropertyBoxSize = 0x27812810;
constexpr uint32_t kPropertyBuildingProps = 0xA8322B7F; // the building's prop list (a counted object)
constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kOccupantTypeProp = 0xA823821E;
constexpr uint32_t kMessageTypesLoaded = 0x896FDC70;
constexpr uint32_t kCommandId = 0x0BEAF1A2;
constexpr uint32_t kNotifications[] = {0xA6B79602, 0x06CDB65B, 0x26D31EC1, 0x0BEAF1A2};
constexpr uint32_t kBaseVtable4 = 0x00A81174; // the base constructor's vtables, overwritten at once
constexpr uint32_t kBaseVtable8 = 0x00A80784;

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// A list (the game's SC4List): the head node pointer; nodes are next,
// previous, value.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// --- Hash tables --------------------------------------------------------------------

char*& BucketBegin(std::uintptr_t t) { return Global<char*>(t + table::kBuckets); }

uint32_t BucketCount(std::uintptr_t t)
{
    return static_cast<uint32_t>(static_cast<int32_t>(Global<char*>(t + table::kBucketsEnd) - BucketBegin(t)) >> 2);
}

// The node of `key` or null, the game's inline find: the bucket is the key
// modulo the bucket count (a divide error when there are none, as in the
// game).
char* FindNode(std::uintptr_t t, uint32_t key)
{
    char* node = reinterpret_cast<char**>(BucketBegin(t))[key % BucketCount(t)];
    while (node && At<uint32_t>(node, table::kKey) != key)
        node = At<char*>(node, 0);
    return node;
}

// An iterator as the game's begin() (0x004B0390) fills it.
struct Iterator {
    char* node;
    char* table;
};

uint32_t BucketsOf(char* t)
{
    return static_cast<uint32_t>(static_cast<int32_t>(At<char*>(t, table::kBucketsEnd) - At<char*>(t, table::kBuckets)) >> 2);
}

// The iterator's ++ (inlined by the game): the next node of the bucket,
// else the first node of a later bucket, else null.
char* NextNode(char* node, char* t)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    const uint32_t count = BucketsOf(t);
    char* found = nullptr;
    for (uint32_t index = At<uint32_t>(node, table::kKey) % count + 1; index < count; ++index) {
        found = reinterpret_cast<char**>(At<char*>(t, table::kBuckets))[index];
        if (found)
            break;
    }
    return found;
}

// The game's inline push_back on a vector of uint32 (begin, end,
// capacity), growing through 0x0051CA60 with the value's own address in
// both pointer arguments.
void PushBack(void** vector, uint32_t* value)
{
    using InsertNFn = void(__thiscall*)(void* vector, void* where, const void* value, const void* allocator,
                                        uint32_t count, uint32_t atEnd);
    auto* const end = static_cast<uint32_t*>(vector[1]);
    if (end == vector[2]) {
        reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN)(vector, end, value, value, 1u, 1u);
        return;
    }
    if (end)
        *end = *value;
    vector[1] = end + 1;
}

// 0x004A2BF0 and 0x004A2CB0: empties the vector (end = begin) and appends
// every key of the table.
void ListKeys(void** vector, std::uintptr_t t)
{
    using BeginFn = Iterator*(__thiscall*)(void* table, Iterator* out);
    vector[1] = vector[0];
    Iterator it;
    reinterpret_cast<BeginFn>(addr::SC4HashTable_Begin)(reinterpret_cast<void*>(t), &it);
    for (char* node = it.node; node; node = NextNode(node, it.table)) {
        uint32_t key = At<uint32_t>(node, table::kKey);
        PushBack(vector, &key);
    }
}

// --- The pieces shared by the lists -------------------------------------------------

// 0x004A1F00 (thiscall, ret 8; the manager is not used): asks an occupant
// for its prop interface and gives the prop its usage.
void __fastcall SetUsage(char*, void*, void* occupant, uint32_t usageValue)
{
    void* prop = nullptr;
    if (Virtual<uint8_t>(occupant, occupant::kQueryInterface, kIidProp, &prop)) {
        Virtual<void>(prop, prop::kSetPropUsage, usageValue);
        Release(prop);
    }
}

// 0x004A2690 (thiscall, ret 4): takes an occupant out of the world. A
// flagged occupant (flag 2) has its placeable object cleared and let go;
// one in the occupant manager (flag 1) is removed from it; then its
// Shutdown.
void __fastcall RemoveProp(char* self, void*, void* occupant)
{
    if (Virtual<uint8_t>(occupant, occupant::kIsFlagSet, 2u)) {
        void* const placeable = Virtual<void*>(occupant, occupant::kGetPlaceable);
        if (placeable) {
            AddRef(placeable);
            Virtual<void>(placeable, 0x2C);
        }
        Virtual<void>(occupant, occupant::kClearFlag, 2u);
        if (placeable)
            Release(placeable);
    }
    if (void* const occupants = At<void*>(self, field::kOccupants)) {
        if (Virtual<uint8_t>(occupant, occupant::kIsFlagSet, 1u))
            Virtual<uint8_t>(occupants, occmgr::kRemove, occupant, 0u, 0u);
    }
    Virtual<void>(occupant, occupant::kShutdown);
}

// The body of 0x004A2010 and 0x004A1E70: makes a prop of the type
// (CreateProp, slot 15), puts it at the position and orientation,
// initialises its occupant, inserts it in the occupant manager and gives it
// the usage. True when all that succeeded. 0x004A2010 then asks the prop for
// its occupant after letting go of it (`occupantOut`); 0x004A1E70 does not.
bool MakeProp(char* self, uint32_t id, void* position, int32_t orientation, uint32_t usageValue, void** occupantOut)
{
    void* prop = nullptr;
    bool added = false;
    if (!Virtual<uint8_t>(self, slot::kCreateProp, id, &prop))
        return false;
    void* occupantPart = Virtual<void*>(prop, prop::kAsOccupant);
    Virtual<void>(occupantPart, occupant::kSetPosition, position);
    Virtual<void>(prop, prop::kSetOrientation, orientation);
    occupantPart = Virtual<void*>(prop, prop::kAsOccupant);
    if (Virtual<uint8_t>(occupantPart, occupant::kInit)) {
        occupantPart = Virtual<void*>(prop, prop::kAsOccupant);
        if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), occmgr::kInsert, occupantPart, 0u)) {
            Virtual<void>(prop, prop::kSetPropUsage, usageValue);
            added = true;
        }
    }
    Release(prop);
    if (added && occupantOut)
        *occupantOut = Virtual<void*>(prop, prop::kAsOccupant);
    return added;
}

// 0x004A2010 (thiscall, ret 0x10): MakeProp for a usage; returns the
// occupant or null.
void* __fastcall AddPropWithUsage(char* self, void*, uint32_t id, void* position, int32_t orientation,
                                  uint32_t usageValue)
{
    void* occupantPart = nullptr;
    return MakeProp(self, id, position, orientation, usageValue, &occupantPart) ? occupantPart : nullptr;
}

// The size of the exemplar-made prop (0x004A24D0 and 0x004A2A70): the box
// is the origin to the exemplar's size property (a vector of three
// floats), or (0,0,0) to (1,1,1) without one.
void SetBox(void* prop, void* occupant)
{
    void* const propHolder = Virtual<void*>(occupant, occupant::kAsPropertyHolder);
    void* const property = Virtual<void*>(propHolder, holder::kGetProperty, kPropertyBoxSize);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    if (property) {
        void* const variant = Virtual<void*>(property, 0x14);
        const float* const size = Virtual<const float*>(variant, 0x1BC);
        Virtual<void>(prop, prop::kSetBoundingBox, zero, size);
    } else {
        const float one[3] = {1.0f, 1.0f, 1.0f};
        Virtual<void>(prop, prop::kSetBoundingBox, zero, one);
    }
}

// --- Identity -------------------------------------------------------------------------

// 0x004A1D20 (the serializable part's slot 5).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    return kClassId;
}

// 0x004A1D30 (thiscall, ret 8; also reached by the jumps of the thunks, with
// the object's own address): the manager part answers kIidManager; the
// message target part answers 1 and kIidMessageTarget; the serializable part
// answers kIidSerializable; the answer is referenced.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    std::ptrdiff_t offset;
    if (iid == kIidManager)
        offset = 0;
    else if (iid == kIidMessageTarget || iid == kIidUnknown)
        offset = 4;
    else if (iid == kIidSerializable)
        offset = 8;
    else
        return 0;
    *out = self ? self + offset : nullptr;
    AddRef(self);
    return 1;
}

// 0x004A2310 (slot 40): the vtables back to the class's own, then the
// memory when bit 0 of the flags is set. Returns the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4PropManager_Vftable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4PropManager_MessageTargetVftable;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4PropManager_SerializableVftable;
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x004A2340 (slot 2): a count above 1 counts down; else Shutdown and the
// deleting destructor with 1. Returns the count (0 when deleted).
uint32_t __fastcall Release_(char* self, void*)
{
    uint32_t count = At<uint32_t>(self, field::kReferences);
    if (count > 1) {
        --count;
        At<uint32_t>(self, field::kReferences) = count;
        return count;
    }
    Virtual<uint8_t>(self, slot::kShutdown);
    Virtual<void*>(self, slot::kDeletingDestructor, 1u);
    return 0;
}

// 0x004A2370 (slot 4): once. The flag first; then the five references let
// go (the service object first, the occupant manager last); the four
// notifications taken off the message server (target: the message target
// part) and the command off the command service.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static constexpr std::size_t kHeld[] = {field::kService, field::kLots, field::kFilter, field::kResources,
                                            field::kOccupants};
    if (At<uint8_t>(self, field::kInitialized)) {
        At<uint8_t>(self, field::kInitialized) = 0;
        for (const std::size_t offset : kHeld) {
            if (void* const held = At<void*>(self, offset)) {
                Release(held);
                At<void*>(self, offset) = nullptr;
            }
        }
        if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
            for (const uint32_t id : kNotifications)
                Virtual<uint8_t>(server, 0x18, static_cast<void*>(self + 4), id);
        }
        Virtual<void>(Global<void*>(addr::SC4_Commands), 0x24, kCommandId);
    }
    return 1;
}

// 0x004A2430 (the serializable part's slot 3; `part` is the object + 8):
// version 1, then the occupant manager and the lot manager as references.
// True when the stream has no error.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    Virtual<void>(stream, 0x1C, 1u);
    Cdecl<uint8_t>(addr::SC4Stream_WriteReference, stream, At<void*>(part, 0xC));
    Cdecl<uint8_t>(addr::SC4Stream_WriteReference, stream, At<void*>(part, 0x18));
    return Virtual<uint8_t>(stream, 0x48) ? 0u : 1u;
}

// 0x004A2470 (slot 4): the version must be 1; then the two references into
// the fields (without letting go of what they held). True when the stream
// has no error.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    uint16_t version = 0;
    Virtual<void>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    Cdecl<uint8_t>(addr::SC4Stream_ReadReference, stream, kIidOccupantManager, static_cast<void**>(&At<void*>(part, 0xC)),
                   uint32_t{0});
    Cdecl<uint8_t>(addr::SC4Stream_ReadReference, stream, kIidLotManager, static_cast<void**>(&At<void*>(part, 0x18)),
                   uint32_t{0});
    return Virtual<uint8_t>(stream, 0x48) ? 0u : 1u;
}

// 0x004A28E0: the class factory. The base vtables first, then the class's.
void* __cdecl Create()
{
    char* const object = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x28});
    if (!object)
        return nullptr;
    At<std::uintptr_t>(object, 0x4) = kBaseVtable4;
    At<std::uintptr_t>(object, 0x8) = kBaseVtable8;
    At<std::uintptr_t>(object, 0x0) = addr::cSC4PropManager_Vftable;
    At<std::uintptr_t>(object, 0x4) = addr::cSC4PropManager_MessageTargetVftable;
    At<std::uintptr_t>(object, 0x8) = addr::cSC4PropManager_SerializableVftable;
    At<uint8_t>(object, field::kInitialized) = 0;
    At<uint32_t>(object, field::kReferences) = 0;
    At<uint32_t>(object, field::kOccupants) = 0;
    At<uint32_t>(object, field::kResources) = 0;
    At<uint32_t>(object, field::kFilter) = 0;
    At<uint32_t>(object, field::kLots) = 0;
    At<uint32_t>(object, field::kService) = 0;
    return object;
}

// 0x004A2A30 (cdecl, no arguments): the clean-up Init registers with the
// service at 0x00B43C94 (slot 0x114): once, it takes itself off (slot
// 0x118) and empties the type table. True.
uint8_t __cdecl CleanUp()
{
    uint8_t& registered = Global<uint8_t>(addr::cSC4PropManager_CleanUpRegistered);
    if (registered) {
        void* const service = Global<void*>(addr::SC4_Service43C94);
        registered = 0;
        Virtual<void>(service, 0x118, static_cast<std::uintptr_t>(addr::cSC4PropManager_CleanUp), uint32_t{0});
        This<void>(addr::SC4Uint32HashSet_Clear, reinterpret_cast<void*>(addr::cSC4PropManager_TypeTable));
    }
    return 1;
}

// 0x004A2730: posts the types-loaded message (0x896FDC70) through the
// message server, with the manager as its two objects and the number of
// types. A crash when the message cannot be allocated, as in the game.
void __fastcall PostTypesLoaded(char* self, void*)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRef(message);
    Virtual<void>(message, 0x14, kMessageTypesLoaded);
    Virtual<void>(message, 0x30, static_cast<void*>(self));
    Virtual<void>(message, 0x84, static_cast<void*>(self));
    Virtual<void>(message, 0x40, Global<uint32_t>(addr::cSC4PropManager_TypeTable + table::kCount));
    Virtual<uint32_t>(server, 0x10, message, uint32_t{0});
    Release(message);
}

// --- Props in the city, on lots and on streets ----------------------------------

// 18 (0x004A1DB0): the city's props in a rectangle of city cells (x0, z0,
// x1, z1): the occupant manager's query by standard city cells with the
// filter at mode 1 and no limit. Without a manager: 0.
uint32_t __fastcall GetCityPropsInRectangle(char* self, void*, void* list, const int32_t* rect)
{
    void* const occupants = At<void*>(self, field::kOccupants);
    if (!occupants)
        return 0;
    void* const propFilter = At<void*>(self, field::kFilter);
    At<uint32_t>(propFilter, filter::kMode) = usage::kCity;
    return Virtual<uint32_t>(occupants, occmgr::kGetByCityCells, list, rect, rect + 2, propFilter, uint32_t{0xFFFFFFFF});
}

// 17 (0x004A1DF0): the same for a rectangle in metres (x0, z0, x1, z1):
// the box has the whole height (the lowest to the highest float).
uint32_t __fastcall GetCityPropsInArea(char* self, void*, void* list, const float* rect)
{
    void* const occupants = At<void*>(self, field::kOccupants);
    if (!occupants)
        return 0;
    const float low[3] = {rect[0], -3.40282347e+38f, rect[1]}; // 0xFF7FFFFF
    const float high[3] = {rect[2], 3.40282347e+38f, rect[3]}; // 0x7F7FFFFF
    void* const propFilter = At<void*>(self, field::kFilter);
    At<uint32_t>(propFilter, filter::kMode) = usage::kCity;
    return Virtual<uint32_t>(occupants, occmgr::kGetByBBox, list, low, high, propFilter, uint32_t{0xFFFFFFFF});
}

// 30 (0x004A1F90): the street props in a rectangle of city cells: as 18
// with the filter at mode 4.
uint32_t __fastcall GetStreetPropsInRectangle(char* self, void*, void* list, const int32_t* rect)
{
    void* const occupants = At<void*>(self, field::kOccupants);
    if (!occupants)
        return 0;
    void* const propFilter = At<void*>(self, field::kFilter);
    At<uint32_t>(propFilter, filter::kMode) = usage::kStreet;
    return Virtual<uint32_t>(occupants, occmgr::kGetByCityCells, list, rect, rect + 2, propFilter, uint32_t{0xFFFFFFFF});
}

// 20 (0x004A1E70): a city prop of the type (usage 1) at the position and
// orientation. True when it is made and inserted.
uint8_t __fastcall AddCityPropNew(char* self, void*, uint32_t id, void* position, int32_t orientation)
{
    return MakeProp(self, id, position, orientation, usage::kCity, nullptr) ? 1 : 0;
}

// The three that take an existing occupant (0x004A1F30 with usage 1,
// 0x004A20C0 with usage 2): nothing without the occupant manager; an
// occupant that has flag 1 already counts as added (true); else its Init and
// its insertion in the occupant manager must succeed, and then it gets the
// usage (through its prop interface).
uint8_t AddExisting(char* self, void* occupant, uint32_t usageValue)
{
    if (!At<void*>(self, field::kOccupants))
        return 0;
    if (!Virtual<uint8_t>(occupant, occupant::kIsFlagSet, 1u)) {
        if (!Virtual<uint8_t>(occupant, occupant::kInit))
            return 0;
        if (!Virtual<uint8_t>(At<void*>(self, field::kOccupants), occmgr::kInsert, occupant, 0u))
            return 0;
        SetUsage(self, nullptr, occupant, usageValue);
    }
    return 1;
}

// 19 (0x004A1F30).
uint8_t __fastcall AddCityProp(char* self, void*, void* occupant)
{
    return AddExisting(self, occupant, usage::kCity);
}

// 25 (0x004A20C0): the lot (first argument) is not used.
uint8_t __fastcall AddLotProp(char* self, void*, void*, void* occupant)
{
    return AddExisting(self, occupant, usage::kLot);
}

// 26 (0x004A2670): a lot prop (usage 2); returns its occupant or null.
void* __fastcall AddLotPropNew(char* self, void*, uint32_t id, void* position, int32_t orientation)
{
    return AddPropWithUsage(self, nullptr, id, position, orientation, usage::kLot);
}

// 32 (0x004A2120): a street prop (usage 4); returns its occupant or null.
void* __fastcall AddStreetPropNew(char* self, void*, uint32_t id, void* position, int32_t orientation)
{
    return AddPropWithUsage(self, nullptr, id, position, orientation, usage::kStreet);
}

// 23 (0x004A1FD0): the lot at a city cell (the lot manager's slot 6 with
// x, z and 0), then its props (slot 24). Without a lot manager or a lot: 0.
uint32_t __fastcall GetLotPropsAt(char* self, void*, void* list, int32_t x, int32_t z)
{
    void* const lots = At<void*>(self, field::kLots);
    if (!lots)
        return 0;
    void* const lot = Virtual<void*>(lots, 0x18, x, z, uint32_t{0});
    if (!lot)
        return 0;
    return Virtual<uint32_t>(self, slot::kGetLotProps, list, lot);
}

// 29 (0x004A27B0): takes the prop out of the world. True.
uint8_t __fastcall RemovePropA(char* self, void*, void* occupant)
{
    RemoveProp(self, nullptr, occupant);
    return 1;
}

// 0x004A28A0 (thiscall, ret 4; called by the removals of the second half):
// takes every occupant of a list (the list holds a reference to each) out of
// the world and lets the reference go.
void __fastcall RemoveProps(char* self, void*, ListNode** list)
{
    ListNode* const head = *list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        RemoveProp(self, nullptr, occupant);
        Release(occupant);
    }
}

// 34 (0x004A2140): the building at a city cell (the occupant manager's
// first occupant of type 0x278128A0 there), then its props (slot 35). False
// without one.
uint8_t __fastcall GetBuildingPropsAt(char* self, void*, void* list, int32_t x, int32_t z)
{
    uint8_t result = 0;
    void* const occupants = At<void*>(self, field::kOccupants);
    if (!occupants)
        return 0;
    void* building = nullptr;
    if (Virtual<uint8_t>(occupants, occmgr::kGetFirstByCityCellByType, &building, x, z, kBuildingType)) {
        result = Virtual<uint8_t>(self, slot::kGetBuildingProps, list, building);
        Release(building);
    }
    return result;
}

// 37 (0x004A21A0): the same for the removal of the building's props (slot
// 38).
uint8_t __fastcall RemoveBuildingPropsAt(char* self, void*, int32_t x, int32_t z)
{
    uint8_t result = 0;
    void* const occupants = At<void*>(self, field::kOccupants);
    if (!occupants)
        return 0;
    void* building = nullptr;
    if (Virtual<uint8_t>(occupants, occmgr::kGetFirstByCityCellByType, &building, x, z, kBuildingType)) {
        result = Virtual<uint8_t>(self, slot::kRemoveBuildingProps, building);
        Release(building);
    }
    return result;
}

// 39 (0x004A27C0): takes one prop off a building's prop list (the building's
// property 0xA8322B7F, a counted object whose vector of occupants lies at
// +4 to +8): the prop leaves the occupant manager when flag 1 is set, is
// shut down and leaves the list. When the list is empty afterwards (also
// when it was before, or the prop was not in it), the property is removed
// from the building. True when the prop was found.
uint8_t __fastcall RemoveBuildingProp(char* self, void*, void* building, void* prop)
{
    uint8_t result = 0;
    void* const propHolder = Virtual<void*>(building, occupant::kAsPropertyHolder);
    if (!propHolder)
        return 0;
    void* list = nullptr;
    if (!Virtual<uint8_t>(propHolder, holder::kGetPropertyAsInterface, kPropertyBuildingProps, uint32_t{1}, &list))
        return 0;
    void** const vector = reinterpret_cast<void**>(static_cast<char*>(list) + 4);
    void** const begin = reinterpret_cast<void**>(vector[0]);
    void** const end = reinterpret_cast<void**>(vector[1]);
    for (void** at = begin; at != end; ++at) {
        if (*at != prop)
            continue;
        if (At<void*>(self, field::kOccupants)) {
            if (Virtual<uint8_t>(prop, occupant::kIsFlagSet, 1u))
                Virtual<uint8_t>(At<void*>(self, field::kOccupants), occmgr::kRemove, prop, 0u, 0u);
        }
        Virtual<void>(prop, occupant::kShutdown);
        void** const after = at + 1;
        if (reinterpret_cast<void**>(vector[1]) != after)
            Cdecl<void*>(addr::SC4_memmove, static_cast<void*>(at), static_cast<void*>(after),
                         static_cast<std::size_t>(reinterpret_cast<char*>(vector[1]) - reinterpret_cast<char*>(after)));
        vector[1] = reinterpret_cast<char*>(vector[1]) - 4;
        result = 1;
        break;
    }
    Release(list);
    if (vector[0] == vector[1])
        Virtual<uint8_t>(propHolder, holder::kRemoveProperty, kPropertyBuildingProps, uint32_t{0});
    return result;
}

// --- The filter -------------------------------------------------------------------

// 0x004A2210 (the filter's slot 3, `this` the filter object; ret 4): whether
// an occupant is a prop of the usage in the filter's mode. Mode 0: every
// prop. Mode 3 with a building set: only the props in the building's prop
// list (the property 0xA8322B7F of its holder).
uint8_t __fastcall IsIncluded(char* self, void*, void* candidate)
{
    if (Virtual<uint32_t>(candidate, occupant::kGetType) != kOccupantTypeProp)
        return 0;
    const uint32_t mode = At<uint32_t>(self, filter::kMode);
    if (mode == 0)
        return 1;
    void* prop = nullptr;
    if (!Virtual<uint8_t>(candidate, occupant::kQueryInterface, kIidProp, &prop))
        return 0;
    uint8_t result = 0;
    const uint32_t used = Virtual<uint8_t>(prop, prop::kGetPropUsage);
    if (used == mode) {
        if (mode == usage::kBuilding && At<void*>(self, filter::kBuilding)) {
            void* const propHolder = Virtual<void*>(At<void*>(self, filter::kBuilding), occupant::kAsPropertyHolder);
            void* list = nullptr;
            if (propHolder &&
                Virtual<uint8_t>(propHolder, holder::kGetPropertyAsInterface, kPropertyBuildingProps, uint32_t{1}, &list)) {
                void** const vector = reinterpret_cast<void**>(static_cast<char*>(list) + 4);
                for (void** at = reinterpret_cast<void**>(vector[0]); at != reinterpret_cast<void**>(vector[1]); ++at) {
                    if (*at == candidate) {
                        result = 1;
                        break;
                    }
                }
                Release(list);
            }
        } else {
            result = 1;
        }
    }
    Release(prop);
    return result;
}

// --- Making props from exemplars -----------------------------------------------------

// 16 (0x004A24D0): a prop from the exemplar at a resource key. The
// exemplar's class property (0xA977A86B, default 0x2977AA47) names the
// class: made through the class factory (COM slot 3) or, when that fails,
// built here: the terrain decal (0x2977AA48), the visual effect
// (0x2977AA49) or the plain prop. The prop is referenced into *out, gets
// the exemplar and its size. False without an exemplar.
uint8_t __fastcall CreateGenericProp(char*, void*, const uint32_t* key, void** out)
{
    void* exemplar = nullptr;
    Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kIidExemplar, &exemplar, uint32_t{0},
                     uint32_t{0});
    if (!exemplar)
        return 0;
    uint32_t classId = kPropClassDefault;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, Virtual<void*>(exemplar, 0x1C), kPropertyPropClass, &classId);
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (!Virtual<uint8_t>(com, 0xC, classId, kIidProp, out)) {
        void* made = nullptr;
        if (classId == kPropClassDecal) {
            if (void* const memory = Cdecl<void*>(addr::cSC4PropOccupantTerrainDecal_Allocate, std::size_t{0xA0})) {
                made = This<void*>(addr::cSC4PropOccupantTerrainDecal_Construct, memory);
                if (made)
                    made = static_cast<char*>(made) + 4;
            }
        } else if (classId == kPropClassEffect) {
            if (void* const memory = Cdecl<void*>(addr::cSC4PropOccupantVisualEffect_Allocate, std::size_t{0x94})) {
                made = This<void*>(addr::cSC4PropOccupantVisualEffect_Construct, memory);
                if (made)
                    made = static_cast<char*>(made) + 4;
            }
        } else {
            if (void* const memory = Cdecl<void*>(addr::cSC4PropOccupant_Allocate, std::size_t{0x68}))
                made = This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, memory);
        }
        *out = made;
        AddRef(made);
    }
    void* const prop = *out;
    Virtual<void>(static_cast<char*>(prop) + prop::kExemplarPart, prop::kSetExemplar, exemplar);
    SetBox(prop, Virtual<void*>(prop, prop::kAsOccupant));
    Release(exemplar);
    return 1;
}

// 0x004A2A70 (thiscall, ret 8; called by CreateProp): gives a prop the
// exemplar of a prop type through the exemplar part of its holder
// (interface 0x0AC2B5F7), if it has one, and its size. The type is the
// second argument, or the prop's own when that is 0. Does nothing for a
// type that is not in the table. True.
uint8_t __fastcall SetPropExemplar(char* self, void*, void* prop, uint32_t type)
{
    void* const occupantPart = Virtual<void*>(prop, prop::kAsOccupant);
    void* const propHolder = Virtual<void*>(occupantPart, occupant::kAsPropertyHolder);
    void* part = nullptr;
    Virtual<uint8_t>(propHolder, holder::kQueryInterface, kIidExemplarHolder, &part);
    const uint32_t key = type ? type : Virtual<uint32_t>(prop, prop::kGetPropType);
    if (char* const node = FindNode(addr::cSC4PropManager_TypeTable, key)) {
        if (part) {
            void* exemplar = nullptr;
            if (Virtual<uint8_t>(At<void*>(self, field::kResources), 0xC, static_cast<void*>(node + table::kValue),
                                 kIidExemplar, &exemplar, uint32_t{0}, uint32_t{0})) {
                Virtual<void>(part, prop::kSetExemplar, exemplar);
                SetBox(prop, occupantPart);
                Release(exemplar);
            }
        } else {
            return 1;
        }
    }
    if (part)
        Release(part);
    return 1;
}

// --- The tables ------------------------------------------------------------------------

// 0x004A2930 (thiscall, ret 4): inserts a 16-byte key (its first dword is
// the hash key) into a hash table of that kind: the table grows for one more
// element (0x004F0940), the key goes first in its bucket in a node of 0x14
// bytes (next, then the key at +4). Returns the node's copy of the key.
void* __fastcall InsertRecord(char* self, void*, const uint32_t* key)
{
    using ResizeFn = void(__thiscall*)(void* table, uint32_t count);
    reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(self, At<uint32_t>(self, table::kCount) + 1);
    const uint32_t index = key[0] % BucketsOf(self);
    void* const first = reinterpret_cast<void**>(At<char*>(self, table::kBuckets))[index];
    char* const node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x14});
    At<void*>(node, 0) = nullptr;
    uint32_t* const copy = reinterpret_cast<uint32_t*>(node + 4);
    if (copy) {
        copy[0] = key[0];
        copy[1] = key[1];
        copy[2] = key[2];
        copy[3] = key[3];
    }
    At<void*>(node, 0) = first;
    reinterpret_cast<void**>(At<char*>(self, table::kBuckets))[index] = node;
    ++At<uint32_t>(self, table::kCount);
    return copy;
}

// 5 (0x004A2BF0): every prop type into the vector (emptied first).
void __fastcall GetAllPropTypes(char*, void*, void** vector)
{
    ListKeys(vector, addr::cSC4PropManager_TypeTable);
}

// 6 (0x004A2CB0): every prop family into the vector.
void __fastcall GetAllPropFamilyTypes(char*, void*, void** vector)
{
    ListKeys(vector, addr::cSC4PropManager_FamilyTable);
}

// 7 (0x004A2D70): the resource key (3 dwords) of a prop type. False for an
// unknown type.
uint8_t __fastcall GetPropKeyFromType(char*, void*, uint32_t type, uint32_t* key)
{
    char* const node = FindNode(addr::cSC4PropManager_TypeTable, type);
    if (!node)
        return 0;
    key[0] = At<uint32_t>(node, table::kValue);
    key[1] = At<uint32_t>(node, table::kValue + 4);
    key[2] = At<uint32_t>(node, table::kValue + 8);
    return 1;
}

// 8 (0x004A2DD0).
uint32_t __fastcall IsPropTypeValid(char*, void*, uint32_t type)
{
    return FindNode(addr::cSC4PropManager_TypeTable, type) ? 1u : 0u;
}

// 11 (0x004A2E10): a family, or a type (every type is its own family).
uint32_t __fastcall IsPropFamilyTypeValid(char*, void*, uint32_t family)
{
    if (FindNode(addr::cSC4PropManager_FamilyTable, family))
        return 1;
    return FindNode(addr::cSC4PropManager_TypeTable, family) ? 1u : 0u;
}

// 9 (0x004A2E90): the types of a family copied into the vector; returns
// their number. For a type that is no family: the vector holds that type
// and the result is 0. For neither: 0 and the vector is untouched.
uint32_t __fastcall GetPropTypesByFamily(char*, void*, void** vector, uint32_t id)
{
    using CopyFn = void*(__thiscall*)(void* vector, const void* other);
    if (char* const family = FindNode(addr::cSC4PropManager_FamilyTable, id)) {
        const uint32_t count = static_cast<uint32_t>(
            static_cast<int32_t>(At<std::uintptr_t>(family, table::kValue + 4) - At<std::uintptr_t>(family, table::kValue)) >> 2);
        reinterpret_cast<CopyFn>(addr::SC4Uint32Vector_CopyAssign)(vector, family + table::kValue);
        return count;
    }
    if (FindNode(addr::cSC4PropManager_TypeTable, id)) {
        vector[1] = vector[0];
        PushBack(vector, &id);
    }
    return 0;
}

// 10 (0x004A2F80): a type is in a family when they are the same ID or the
// family's vector has it (a find, 0x004696C0).
uint8_t __fastcall IsPropInFamily(char*, void*, uint32_t type, uint32_t family)
{
    using FindFn = uint32_t*(__cdecl*)(uint32_t* first, uint32_t* last, const uint32_t* value, const void* tag);
    if (type == family)
        return 1;
    char* const node = FindNode(addr::cSC4PropManager_FamilyTable, family);
    if (!node)
        return 0;
    uint32_t* const last = At<uint32_t*>(node, table::kValue + 4);
    uint32_t* const found = reinterpret_cast<FindFn>(addr::SC4_FindUint32)(At<uint32_t*>(node, table::kValue), last, &type, &type);
    return found != last ? 1 : 0;
}

// 14 (0x004A2FF0): the profile of a type (in the node), or null.
void* __fastcall GetPropProfilePtr(char*, void*, uint32_t type)
{
    char* const node = FindNode(addr::cSC4PropManager_ProfileTable, type);
    return node ? node + table::kValue : nullptr;
}

// --- Registration -------------------------------------------------------------------

bool InstallPropMgr1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[40] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4PropManager_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
        {addr::cSC4PropManager_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4PropManager_GetCityPropsInRectangle, reinterpret_cast<void*>(&GetCityPropsInRectangle)},
        {addr::cSC4PropManager_GetCityPropsInArea, reinterpret_cast<void*>(&GetCityPropsInArea)},
        {addr::cSC4PropManager_AddCityPropNew, reinterpret_cast<void*>(&AddCityPropNew)},
        {addr::cSC4PropManager_SetUsage, reinterpret_cast<void*>(&SetUsage)},
        {addr::cSC4PropManager_AddCityProp, reinterpret_cast<void*>(&AddCityProp)},
        {addr::cSC4PropManager_GetStreetPropsInRectangle, reinterpret_cast<void*>(&GetStreetPropsInRectangle)},
        {addr::cSC4PropManager_GetLotPropsAt, reinterpret_cast<void*>(&GetLotPropsAt)},
        {addr::cSC4PropManager_AddPropWithUsage, reinterpret_cast<void*>(&AddPropWithUsage)},
        {addr::cSC4PropManager_AddLotProp, reinterpret_cast<void*>(&AddLotProp)},
        {addr::cSC4PropManager_AddStreetPropNew, reinterpret_cast<void*>(&AddStreetPropNew)},
        {addr::cSC4PropManager_GetBuildingPropsAt, reinterpret_cast<void*>(&GetBuildingPropsAt)},
        {addr::cSC4PropManager_RemoveBuildingPropsAt, reinterpret_cast<void*>(&RemoveBuildingPropsAt)},
        {addr::cSC4PropFilter_IsIncluded, reinterpret_cast<void*>(&IsIncluded)},
        {addr::cSC4PropManager_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::cSC4PropManager_Release, reinterpret_cast<void*>(&Release_)},
        {addr::cSC4PropManager_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4PropManager_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4PropManager_Read, reinterpret_cast<void*>(&Read)},
        {addr::cSC4PropManager_CreateGenericProp, reinterpret_cast<void*>(&CreateGenericProp)},
        {addr::cSC4PropManager_AddLotPropNew, reinterpret_cast<void*>(&AddLotPropNew)},
        {addr::cSC4PropManager_RemoveProp, reinterpret_cast<void*>(&RemoveProp)},
        {addr::cSC4PropManager_PostTypesLoaded, reinterpret_cast<void*>(&PostTypesLoaded)},
        {addr::cSC4PropManager_RemovePropA, reinterpret_cast<void*>(&RemovePropA)},
        {addr::cSC4PropManager_RemoveBuildingProp, reinterpret_cast<void*>(&RemoveBuildingProp)},
        {addr::cSC4PropManager_RemoveProps, reinterpret_cast<void*>(&RemoveProps)},
        {addr::cSC4PropManager_Create, reinterpret_cast<void*>(&Create)},
        {addr::cSC4PropManager_InsertRecord, reinterpret_cast<void*>(&InsertRecord)},
        {addr::cSC4PropManager_CleanUp, reinterpret_cast<void*>(&CleanUp)},
        {addr::cSC4PropManager_SetPropExemplar, reinterpret_cast<void*>(&SetPropExemplar)},
        {addr::cSC4PropManager_GetAllPropTypes, reinterpret_cast<void*>(&GetAllPropTypes)},
        {addr::cSC4PropManager_GetAllPropFamilyTypes, reinterpret_cast<void*>(&GetAllPropFamilyTypes)},
        {addr::cSC4PropManager_GetPropKeyFromType, reinterpret_cast<void*>(&GetPropKeyFromType)},
        {addr::cSC4PropManager_IsPropTypeValid, reinterpret_cast<void*>(&IsPropTypeValid)},
        {addr::cSC4PropManager_IsPropFamilyTypeValid, reinterpret_cast<void*>(&IsPropFamilyTypeValid)},
        {addr::cSC4PropManager_GetPropTypesByFamily, reinterpret_cast<void*>(&GetPropTypesByFamily)},
        {addr::cSC4PropManager_IsPropInFamily, reinterpret_cast<void*>(&IsPropInFamily)},
        {addr::cSC4PropManager_GetPropProfilePtr, reinterpret_cast<void*>(&GetPropProfilePtr)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_propMgr1Registered = registry::Register({
    "simulators.propmgr.part1",
    "simulators/propmgr",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropManager_QueryInterface,
    "Our prop manager, first half: identity and lifetime, the city, lot, street and building prop lists, the prop "
    "filter and the type, family and profile tables (src/core/propmgr_1.cpp)",
    false,
    InstallPropMgr1,
});

} // namespace
} // namespace opensc4::propmgr
