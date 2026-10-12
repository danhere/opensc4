// Our occupant filters, the small ones and their base class (build 641): the
// range 0x005E95B0 to 0x005EA610, plus the lot's own filter (0x006BFB80 and
// 0x006BFC10 in cSC4Lot's range). The string specification filter from
// 0x005EA610 on is left the game's.
//
// The classes (class ID, factory, vtable pair: the filter vtable at +0, the
// cIGZSerializable vtable at +4; every filter object is 0xC bytes of base
// plus its own fields, +8 the reference count):
//   base               0xA9C74DC9  0x005EA130  0x00AA42C8 / 0x00AA42B0   0xC
//   default            0xC9C75EAE  0x005EA160  0x00A87FC0 / 0x00A87FA8   0xC
//   type               0xE9C75ED1  0x005EA190  0x00A8F79C / 0x00A8F784   0x10  +0xC the type
//   types              0xE9C75EE8  0x005EA330  0x00A8EDBC / 0x00A8EDA4   0x18  +0xC a vector of uint32
//   types, hashed      0xAA25CB90  0x005EB970  0x00A8EE38 / 0x00A8EE20   a hash set at +0xC
//   realistic          0x8A7A1C2C  0x005EA240  0x00A88084 / 0x00A8806C   0x10  +0xC, +0xD, +0xE bytes
//   network            0xEAC3A3D2  0x005EA1C0  0x00AA42FC / 0x00AA42E4   0x10  +0xC a mask (0x1FFF)
//   view level (kind)  0xAAC3C01D  0x005EA200  0x00A8FAD4 / 0x00A8FABC   0x10  +0xC the kind (1)
//   string spec.       0x0BCEB6C2  0x005EBBD0  0x00AA442C / (not ours)
//   group (no ID, no factory: made by 0x005EA280 and 0x005EA440 inside
//   other code)                                0x00AA4330 / 0x00AA4318   0x10  +0xC a vector of uint32
// Slots of the filter vtable: 0 QueryInterface 0x005E95D0 (all 44), 1 AddRef
// 0x004BDF00, 2 Release 0x007CB920, 3 IsOccupantIncluded, 4
// IsOccupantTypeIncluded, 5 IsPropertyHolderIncluded, 6 the deleting
// destructor. The serializable vtable: 0-2 thunks, 3 Write, 4 Read, 5
// GetGZCLSID. Write and Read get the serializable part (object + 4).
//
// Virtual calls with arguments, each count from the callee's own ret N
// (vt.py on the vtables):
//   - the occupant (0x00AA41D8): 0x00 QueryInterface two, 0x08 Release none,
//     0x18 AsPropertyHolder none, 0x1C GetType none, 0x20 GetPosition one,
//     0x58 IsOccupantGroup one (the holder vtable 0x00AA4158 at +8, whose
//     slot 0x58 is the deleting destructor, is another receiver);
//   - the property holder (0x00AA4158): 0x0C HasProperty one;
//   - the streams (0x00AA2310 out, 0x00AA2280 in): 0x14, 0x1C, 0x24 one,
//     0x48 none;
//   - the city's 0x17C four, the lot manager's 0x18 three (0x00AB1B98 is
//     the developer's; the manager's use in audio_1.cpp has three), the
//     lot's 0x84 none, the occupant manager's 0x90 five (vtable 0x00A8F7F0);
//   - the network occupant interface (IID 0xA821EF94) 0x30: none (read from
//     the game's call, the callee not located);
//   - a filter's own slots 0x04 none, 0x0C (embedded filters) and 0x14 one.
// Not checked against a callee: the interfaces 0x097F6D98, 0x4B44FBE2,
// 0x2B3B7D86 and 0xA821EF94 are only queried and released.
//
// Register use of the callers (regscan and eaxscan over every direct call
// and jump): none keeps ECX or EDX across a hooked function; the four tail
// jumps to 0x005E95C0 return its EAX, so that hook is a naked thunk that
// keeps EAX, ECX and EDX; 0x005E9CA0 takes ESI, also a naked thunk. The
// rest are plain detours.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::occfilters_1 {
namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("occfilters: first %s", name);
}

#define OCCFILTERS_FIRST(name)  \
    static bool logged = false; \
    FirstCall(name, logged)

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

constexpr uint32_t kClassBase = 0xA9C74DC9;
constexpr uint32_t kClassType = 0xE9C75ED1;
constexpr uint32_t kClassTypes = 0xE9C75EE8;
constexpr uint32_t kClassTypesHashed = 0xAA25CB90;
constexpr uint32_t kClassKind = 0xAAC3C01D;
constexpr uint32_t kClassNetwork = 0xEAC3A3D2;

constexpr uint32_t kIidFilter = 0x46A80026;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidUnknown = 1;

// The occupant interfaces the tests ask for. The first two keep an occupant
// out of the default filter; the pipe and the 0xEA0A5ACF class (the
// survey's subway) are kind 3; the next two are kind 2.
constexpr uint32_t kIidExcluded1 = 0x4A10AFC2;
constexpr uint32_t kIidUndergroundConnection = 0x8A139184; // cSC4UndergroundConnectionOccupant
constexpr uint32_t kIidPipe = 0x49C05B9F;                  // cSC4PipeOccupant
constexpr uint32_t kIidSubway = 0xEA0A5ACF;
constexpr uint32_t kIidKind2a = 0xCA16374F;
constexpr uint32_t kIidKind2b = 0xCA15ED6B;
constexpr uint32_t kIidLotTexture = 0x097F6D98; // the lot base texture occupant
constexpr uint32_t kIidProp1 = 0x4B44FBE2;
constexpr uint32_t kIidProp2 = 0x2B3B7D86;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;

constexpr uint32_t kTypeLotTexture = 0x497F6D9D;
constexpr uint32_t kTypeProp = 0xA823821E;

// Occupant groups.
constexpr uint32_t kGroupA = 0x4217;
constexpr uint32_t kGroupB = 0x4208;
constexpr uint32_t kGroupC = 0x421D;
constexpr uint32_t kGroupRealistic = 0x5001;

// Holder properties.
constexpr uint32_t kPropertyExcluded1 = 0x49512A09;
constexpr uint32_t kPropertyExcluded2 = 0x8A15F1BD;
constexpr uint32_t kPropertyBool0 = 0xCAA45670;
constexpr uint32_t kPropertyBool1 = 0xCAA45671;
constexpr uint32_t kPropertyBool2 = 0xCAA45672;

namespace occupant {
constexpr std::size_t kQueryInterface = 0x00;
constexpr std::size_t kRelease = 0x08;
constexpr std::size_t kPropertyHolder = 0x18;
constexpr std::size_t kGetType = 0x1C;
constexpr std::size_t kGetPosition = 0x20;
constexpr std::size_t kIsOccupantGroup = 0x58;
} // namespace occupant

namespace stream {
constexpr std::size_t kU8 = 0x14;
constexpr std::size_t kU16 = 0x1C;
constexpr std::size_t kU32 = 0x24;
constexpr std::size_t kError = 0x48;
} // namespace stream

constexpr std::size_t kHolderHasProperty = 0x0C;
constexpr std::size_t kFilterAddRef = 0x04;
constexpr std::size_t kFilterIsOccupantIncluded = 0x0C;
constexpr std::size_t kFilterIsPropertyHolderIncluded = 0x14;

// The object's fields (from the filter part).
namespace field {
constexpr std::size_t kSerializable = 0x04;
constexpr std::size_t kRefCount = 0x08;
constexpr std::size_t kValue = 0x0C; // the type, the mask, the kind, the first flag, the vector, the hash set
constexpr std::size_t kVectorEnd = 0x10;
constexpr std::size_t kVectorCapacity = 0x14;
constexpr std::size_t kRealisticLot = 0x0C;      // flag: lots in state 1 are not removed
constexpr std::size_t kRealisticGroup = 0x0D;    // flag: group 0x5001 is not removed
constexpr std::size_t kRealisticProp = 0x0E;     // flag: props with the two interfaces are not removed
} // namespace field

// The string specification filter's vectors and box (0x48 bytes; the class
// is not ours).
namespace spec {
constexpr std::size_t kLow = 0x0C;
constexpr std::size_t kHigh = 0x18;
constexpr std::size_t kTypes = 0x24;
constexpr std::size_t kGroups = 0x30;
constexpr std::size_t kKinds = 0x3C;
} // namespace spec

// The lot's filter (0x38 bytes): the box, then two embedded filters.
namespace lotfilter {
constexpr std::size_t kMinX = 0x0C;
constexpr std::size_t kMinZ = 0x10;
constexpr std::size_t kMaxX = 0x14;
constexpr std::size_t kMaxZ = 0x18;
constexpr std::size_t kDefault = 0x1C;
constexpr std::size_t kRealistic = 0x28;
} // namespace lotfilter

// The game's vector of uint32 (begin, end, capacity).
struct Uint32Vector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// The hash set's iterator (SC4HashTable::Begin writes two pointers): the
// first node and the set.
struct HashIterator {
    char* node;
    char* table;
};

// The result of the hash set's insert: a pair of an iterator (two pointers)
// and a bool, 9 bytes written (the game's frame has the room).
struct InsertResult {
    char* node;
    char* table;
    uint8_t inserted;
    uint8_t padding[3];
};

// --- small helpers ---------------------------------------------------------------------

inline uint32_t AddRefOf(void* object)
{
    return Virtual<uint32_t>(object, kFilterAddRef);
}

inline void ReleaseOf(void* object)
{
    Virtual<uint32_t>(object, occupant::kRelease);
}

inline bool NoError(void* stream)
{
    return Virtual<uint8_t>(stream, stream::kError) == 0;
}

inline bool IsGroup(void* occupant, uint32_t group)
{
    return Virtual<uint8_t>(occupant, occupant::kIsOccupantGroup, group) != 0;
}

inline bool HasProperty(void* holder, uint32_t property)
{
    return Virtual<uint8_t>(holder, kHolderHasProperty, property) != 0;
}

inline bool Query(void* occupant, uint32_t iid, void** object)
{
    return Virtual<uint8_t>(occupant, occupant::kQueryInterface, iid, object) != 0;
}

// The game's SC4::GetPropertyBool (cdecl: holder, property, bool*).
inline void GetPropertyBool(void* holder, uint32_t property, bool* value)
{
    CallCdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, property, value);
}

// The low word of a pointer: what the game's 16-bit version slots hold when
// the stream gives nothing (they are stack slots that held a pointer).
inline uint16_t LowWord(const void* pointer)
{
    return static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// --- the base --------------------------------------------------------------------------

// 0x005E95B0: cIGZSerializable slot 5 of the base.
uint32_t __fastcall BaseGetGZCLSID(char*, void*)
{
    OCCFILTERS_FIRST("GetGZCLSID");
    return kClassBase;
}

// 0x005E95C0: the base vtables (what a derived destructor ends with; 16
// direct callers).
void __fastcall BaseDestroy(char* self, void*)
{
    OCCFILTERS_FIRST("Destroy");
    At<std::uintptr_t>(self, 0x0) = addr::SC4OccupantFilterBase_vftable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4OccupantFilterBase_vftable_4;
}

// The hook of 0x005E95C0 keeps EAX, ECX and EDX as the game's function does:
// four of its callers jump to it as their last act (0x00466EDE, 0x00491226,
// 0x005204C0, 0x007A04DE) and so return whatever EAX held.
__declspec(naked) void BaseDestroyThunk()
{
    __asm push eax
    __asm push ecx
    __asm push edx
    __asm call BaseDestroy
    __asm pop edx
    __asm pop ecx
    __asm pop eax
    __asm ret
}

// 0x005E95D0: slot 0 of the filter vtable. `self` may be null in the game's
// test of the 0xE4FDA3D4 case; the AddRef through the null object then
// crashes as in the game.
uint32_t __fastcall BaseQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    OCCFILTERS_FIRST("QueryInterface");
    if (iid == kIidUnknown || iid == kIidFilter) {
        *object = self;
        AddRefOf(self);
        return 1;
    }
    if (iid != kIidSerializable)
        return 0;
    if (self)
        *object = self + field::kSerializable;
    else
        *object = nullptr;
    AddRefOf(self);
    return 1;
}

// 0x005E9790: slot 6 of the base vtable. `self` is the filter part.
char* __fastcall BaseDeletingDestructor(char* self, void*, uint32_t flags)
{
    OCCFILTERS_FIRST("DeletingDestructor");
    At<std::uintptr_t>(self, 0x0) = addr::SC4OccupantFilterBase_vftable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4OccupantFilterBase_vftable_4;
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x005E97C0: Write, the version 3 as 16 bits. `part` is the serializable
// part.
uint32_t __fastcall BaseWrite(char*, void*, void* stream)
{
    OCCFILTERS_FIRST("Write");
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    return NoError(stream) ? 1 : 0;
}

// 0x005E97E0: Read. The version slot is the stream argument's own stack
// slot: a stream that stores nothing leaves the low word of its pointer.
uint32_t __fastcall BaseRead(char*, void*, void* stream)
{
    OCCFILTERS_FIRST("Read");
    uint16_t version = LowWord(stream);
    Virtual<void>(stream, stream::kU16, &version);
    if (version != 3)
        return 0;
    return NoError(stream) ? 1 : 0;
}

// 0x005EA080 is core.toml's SC4::OccupantFilterBase (the constructor, 78
// direct callers); the game stores 0x00A80784 in +4 first and overwrites it
// at once, which we leave out.
char* __fastcall BaseConstruct(char* self, void*)
{
    OCCFILTERS_FIRST("OccupantFilterBase");
    At<std::uintptr_t>(self, 0x0) = addr::SC4OccupantFilterBase_vftable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4OccupantFilterBase_vftable_4;
    At<uint32_t>(self, field::kRefCount) = 0;
    return self;
}

// --- the default filter ----------------------------------------------------------------

// 0x005E96E0: slot 5 of the default and the realistic filter.
uint32_t __fastcall DefaultIsPropertyHolderIncluded(char*, void*, void* holder)
{
    OCCFILTERS_FIRST("IsPropertyHolderIncluded");
    if (HasProperty(holder, kPropertyExcluded1))
        return 0;
    if (HasProperty(holder, kPropertyExcluded2))
        return 0;
    bool value = false;
    GetPropertyBool(holder, kPropertyBool0, &value);
    return value ? 0 : 1;
}

// 0x005E9630: slot 3 of the default filter.
uint32_t __fastcall DefaultIsOccupantIncluded(char* self, void*, void* occupantObject)
{
    OCCFILTERS_FIRST("DefaultIsOccupantIncluded");
    bool included = true;
    void* first = nullptr;
    if (Query(occupantObject, kIidExcluded1, &first)) {
        ReleaseOf(first);
        included = false;
    }
    void* second = nullptr;
    if (Query(occupantObject, kIidUndergroundConnection, &second)) {
        ReleaseOf(second);
        included = false;
    }
    if (IsGroup(occupantObject, kGroupA) || IsGroup(occupantObject, kGroupB) || IsGroup(occupantObject, kGroupC))
        return 0;
    if (!included)
        return 0;
    void* const holder = Virtual<void*>(occupantObject, occupant::kPropertyHolder);
    return Virtual<uint8_t>(self, kFilterIsPropertyHolderIncluded, holder);
}

// 0x005E9810: Write of the default filter.
uint32_t __fastcall DefaultWrite(char*, void*, void* stream)
{
    OCCFILTERS_FIRST("DefaultWrite");
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    return NoError(stream) ? 1 : 0;
}

// 0x005E9840: Read of the default filter. The first version slot is the
// stream argument's slot, the second the slot the game's `push ecx` left
// holding the part pointer.
uint32_t __fastcall DefaultRead(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("DefaultRead");
    uint16_t first = LowWord(stream);
    Virtual<void>(stream, stream::kU16, &first);
    if (first != 3)
        return 0;
    uint16_t second = LowWord(part);
    Virtual<void>(stream, stream::kU16, &second);
    if (second != 3)
        return 0;
    return NoError(stream) ? 1 : 0;
}

// --- the type filter -------------------------------------------------------------------

// 0x005E9730, 0x005E9760, 0x005E9770, 0x005E9780, 0x005EA120: class IDs.
uint32_t __fastcall TypeGetGZCLSID(char*, void*)
{
    OCCFILTERS_FIRST("TypeGetGZCLSID");
    return kClassType;
}

uint32_t __fastcall TypesGetGZCLSID(char*, void*)
{
    OCCFILTERS_FIRST("TypesGetGZCLSID");
    return kClassTypes;
}

uint32_t __fastcall TypesHashedGetGZCLSID(char*, void*)
{
    OCCFILTERS_FIRST("TypesHashedGetGZCLSID");
    return kClassTypesHashed;
}

uint32_t __fastcall KindGetGZCLSID(char*, void*)
{
    OCCFILTERS_FIRST("KindGetGZCLSID");
    return kClassKind;
}

uint32_t __fastcall NetworkGetGZCLSID(char*, void*)
{
    OCCFILTERS_FIRST("NetworkGetGZCLSID");
    return kClassNetwork;
}

// 0x005E9740: slot 4 of the type filter.
uint32_t __fastcall TypeIsOccupantTypeIncluded(char* self, void*, uint32_t type)
{
    OCCFILTERS_FIRST("TypeIsOccupantTypeIncluded");
    return type == At<uint32_t>(self, field::kValue) ? 1 : 0;
}

// 0x005E9890: Write of the type filter; `part` is the serializable part,
// whose +8 is the object's +0xC.
uint32_t __fastcall TypeWrite(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("TypeWrite");
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    if (Virtual<uint8_t>(stream, stream::kError) == 0)
        Virtual<void>(stream, stream::kU32, At<uint32_t>(part, 0x8));
    return NoError(stream) ? 1 : 0;
}

// 0x005E98E0: Read of the type filter. The first version slot is the slot
// the game's `push ecx` left holding the part pointer, the second the stream
// argument's slot.
uint32_t __fastcall TypeRead(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("TypeRead");
    uint16_t first = LowWord(part);
    Virtual<void>(stream, stream::kU16, &first);
    if (first != 3)
        return 0;
    uint16_t second = LowWord(stream);
    Virtual<void>(stream, stream::kU16, &second);
    if (second == 3 && Virtual<uint8_t>(stream, stream::kError) == 0)
        Virtual<void>(stream, stream::kU32, &At<uint32_t>(part, 0x8));
    return NoError(stream) ? 1 : 0;
}

// --- the types filter ------------------------------------------------------------------

// 0x005E9950: slot 4 of the types filter.
uint32_t __fastcall TypesIsOccupantTypeIncluded(char* self, void*, uint32_t type)
{
    OCCFILTERS_FIRST("TypesIsOccupantTypeIncluded");
    const Uint32Vector& types = At<Uint32Vector>(self, field::kValue);
    if (types.begin == types.end)
        return 1;
    for (const uint32_t* at = types.begin; at != types.end; ++at)
        if (type == *at)
            return 1;
    return 0;
}

// 0x005EA0A0: Write of the types filter; the vector is at the part's +8.
uint32_t __fastcall TypesWrite(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("TypesWrite");
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    if (Virtual<uint8_t>(stream, stream::kError) == 0) {
        const Uint32Vector& types = At<Uint32Vector>(part, 0x8);
        Virtual<void>(stream, stream::kU32, static_cast<uint32_t>((types.end - types.begin)));
        const int32_t count = static_cast<int32_t>(types.end - types.begin);
        for (int32_t i = 0; i < count; ++i)
            Virtual<void>(stream, stream::kU32, At<Uint32Vector>(part, 0x8).begin[i]);
    }
    return NoError(stream) ? 1 : 0;
}

// 0x005EA490: Read of the types filter. The first version slot is the
// frame's first local (the game never sets it: 0 here), the second the
// stream argument's slot, which then also holds the count.
uint32_t __fastcall TypesRead(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("TypesRead");
    uint16_t first = 0;
    Virtual<void>(stream, stream::kU16, &first);
    if (first != 3)
        return 0;
    uint32_t slot = static_cast<uint32_t>(LowWord(stream));
    Virtual<void>(stream, stream::kU16, reinterpret_cast<uint16_t*>(&slot));
    if (static_cast<uint16_t>(slot) == 3 && Virtual<uint8_t>(stream, stream::kError) == 0) {
        Virtual<void>(stream, stream::kU32, &slot);
        void* const vector = part + 0x8;
        uint32_t zero = 0;
        CallThis<void>(addr::SC4Uint32Vector_Resize, vector, slot, &zero);
        for (uint32_t i = 0; i < slot; ++i)
            Virtual<void>(stream, stream::kU32, &At<Uint32Vector>(part, 0x8).begin[i]);
    }
    return NoError(stream) ? 1 : 0;
}

// --- the hashed types filter -----------------------------------------------------------

// 0x005EA370: Write of the hashed types filter; the set is at the part's +8
// (its count at +0x18). Keys in the order the set iterates: the nodes of a
// bucket, then the next bucket with a node.
uint32_t __fastcall TypesHashedWrite(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("TypesHashedWrite");
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    if (Virtual<uint8_t>(stream, stream::kError) == 0) {
        Virtual<void>(stream, stream::kU32, At<uint32_t>(part, 0x18));
        HashIterator iterator = {};
        CallThis<void>(addr::SC4HashTable_Begin, part + 0x8, &iterator);
        char* node = iterator.node;
        char* const table = iterator.table;
        while (node) {
            const uint32_t key = At<uint32_t>(node, 4);
            Virtual<void>(stream, stream::kU32, key);
            char* const next = At<char*>(node, 0);
            if (next) {
                node = next;
                continue;
            }
            uint32_t** const buckets = reinterpret_cast<uint32_t**>(table + 4);
            const uint32_t count = static_cast<uint32_t>((buckets[1] - buckets[0]));
            uint32_t index = key % count;
            const uint32_t total = static_cast<uint32_t>(buckets[1] - buckets[0]);
            uint32_t found = 0;
            ++index;
            while (index < total) {
                found = buckets[0][index];
                if (found)
                    break;
                ++index;
            }
            node = reinterpret_cast<char*>(found);
        }
    }
    return NoError(stream) ? 1 : 0;
}

// 0x005EA550: Read of the hashed types filter. The first version slot is a
// frame local the game never sets (0 here); the second is the stream
// argument's slot, which then holds the count.
uint32_t __fastcall TypesHashedRead(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("TypesHashedRead");
    uint16_t first = 0;
    Virtual<void>(stream, stream::kU16, &first);
    if (first != 3)
        return 0;
    uint32_t slot = static_cast<uint32_t>(LowWord(stream));
    Virtual<void>(stream, stream::kU16, reinterpret_cast<uint16_t*>(&slot));
    if (static_cast<uint16_t>(slot) == 3 && Virtual<uint8_t>(stream, stream::kError) == 0) {
        Virtual<void>(stream, stream::kU32, &slot);
        void* const set = part + 0x8;
        for (uint32_t i = 0; i < slot; ++i) {
            uint32_t key = 0;
            Virtual<void>(stream, stream::kU32, &key);
            CallThis<void>(addr::SC4Uint32HashSet_Resize, set, At<uint32_t>(set, 0x10) + 1);
            InsertResult result;
            CallThis<void>(addr::SC4Uint32HashSet_Insert, set, &result, &key);
        }
    }
    return NoError(stream) ? 1 : 0;
}

// --- the realistic filter --------------------------------------------------------------

// 0x005E9990: slot 3 of the realistic filter.
uint32_t __fastcall RealisticIsOccupantIncluded(char* self, void*, void* occupantObject)
{
    OCCFILTERS_FIRST("RealisticIsOccupantIncluded");
    bool included = DefaultIsOccupantIncluded(self, nullptr, occupantObject) != 0;
    if (!included)
        return 0;
    if (!At<uint8_t>(self, field::kRealisticGroup) && IsGroup(occupantObject, kGroupRealistic))
        return 0;
    const uint32_t type = Virtual<uint32_t>(occupantObject, occupant::kGetType);
    if (type == kTypeLotTexture) {
        if (At<uint8_t>(self, field::kRealisticLot))
            return included ? 1 : 0;
        void* texture = nullptr;
        if (Query(occupantObject, kIidLotTexture, &texture)) {
            float position[3];
            Virtual<void>(occupantObject, occupant::kGetPosition, static_cast<float*>(position));
            int32_t cellZ = 0;
            int32_t cellX = 0;
            Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
            void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, uint32_t{0});
            if (lot && Virtual<uint32_t>(lot, 0x84) == 1)
                included = false;
        }
        if (texture)
            ReleaseOf(texture);
    } else if (type == kTypeProp) {
        if (At<uint8_t>(self, field::kRealisticProp))
            return included ? 1 : 0;
        void* first = nullptr;
        if (Query(occupantObject, kIidProp1, &first)) {
            included = false;
        } else {
            void* second = nullptr;
            if (Query(occupantObject, kIidProp2, static_cast<void**>(CallThis<void*>(addr::SC4RefPtr_Reset, &second))))
                included = false;
            if (second)
                ReleaseOf(second);
        }
        if (first)
            ReleaseOf(first);
    }
    return included ? 1 : 0;
}

// 0x005E9AF0: Write of the realistic filter; the flags are at the part's
// +8, +9, +0xA.
uint32_t __fastcall RealisticWrite(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("RealisticWrite");
    Virtual<void>(stream, stream::kU16, uint32_t{4});
    CallCdecl<void*>(addr::SC4Stream_WriteBool, stream, static_cast<uint32_t>(At<uint8_t>(part, 0x8)));
    CallCdecl<void*>(addr::SC4Stream_WriteBool, stream, static_cast<uint32_t>(At<uint8_t>(part, 0x9)));
    CallCdecl<void*>(addr::SC4Stream_WriteBool, stream, static_cast<uint32_t>(At<uint8_t>(part, 0xA)));
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    Virtual<void>(stream, stream::kU16, uint32_t{3});
    return NoError(stream) ? 1 : 0;
}

// 0x005E9B50: Read of the realistic filter. The version slot is the stream
// argument's slot. Below version 3 nothing is read; below 4 the third flag
// is a copy of the second; then the default filter's Read.
uint32_t __fastcall RealisticRead(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("RealisticRead");
    uint16_t version = LowWord(stream);
    Virtual<void>(stream, stream::kU16, &version);
    if (version < 3)
        return 0;
    CallCdecl<void*>(addr::SC4Stream_ReadBool, stream, part + 0x8);
    CallCdecl<void*>(addr::SC4Stream_ReadBool, stream, part + 0x9);
    if (version < 4) {
        At<uint8_t>(part, 0xA) = At<uint8_t>(part, 0x9);
        return DefaultRead(part, nullptr, stream);
    }
    CallCdecl<void*>(addr::SC4Stream_ReadBool, stream, part + 0xA);
    return DefaultRead(part, nullptr, stream);
}

// --- the network filter ----------------------------------------------------------------

// 0x005E9BD0: slot 3 of the network filter.
uint32_t __fastcall NetworkIsOccupantIncluded(char* self, void*, void* occupantObject)
{
    OCCFILTERS_FIRST("NetworkIsOccupantIncluded");
    void* network = nullptr;
    if (Query(occupantObject, kIidNetworkOccupant, &network)) {
        const uint32_t bits = Virtual<uint32_t>(network, 0x30);
        const bool included = (At<uint32_t>(self, field::kValue) & bits) != 0;
        if (network)
            ReleaseOf(network);
        return included ? 1 : 0;
    }
    if (network)
        ReleaseOf(network);
    return 0;
}

// 0x005E9C30: Write of the network filter; the mask is at the part's +8.
uint32_t __fastcall NetworkWrite(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("NetworkWrite");
    Virtual<void>(stream, stream::kU16, uint32_t{1});
    Virtual<void>(stream, stream::kU32, At<uint32_t>(part, 0x8));
    return NoError(stream) ? 1 : 0;
}

// 0x005E9C60: Read of the network filter. The version slot is the stream
// argument's slot.
uint32_t __fastcall NetworkRead(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("NetworkRead");
    uint16_t version = LowWord(stream);
    Virtual<void>(stream, stream::kU16, &version);
    if (version != 1)
        return 0;
    Virtual<void>(stream, stream::kU32, &At<uint32_t>(part, 0x8));
    return NoError(stream) ? 1 : 0;
}

// --- the view level (kind) filter ------------------------------------------------------

// 0x005E9CA0: the occupant's kind, 1, 2 or 3. The game's version takes the
// occupant in ESI.
uint32_t OccupantKind(void* occupantObject)
{
    void* const holder = Virtual<void*>(occupantObject, occupant::kPropertyHolder);
    uint32_t kind = 1;
    void* pipe = nullptr;
    if (Query(occupantObject, kIidPipe, &pipe))
        kind = 3;
    void* subway = nullptr;
    if (Query(occupantObject, kIidSubway, &subway))
        kind = 3;
    if (HasProperty(holder, kPropertyExcluded1))
        kind = 3;
    void* second = nullptr;
    if (Query(occupantObject, kIidKind2a, &second))
        kind = 2;
    void* third = nullptr;
    if (Query(occupantObject, kIidKind2b, &third))
        kind = 2;
    if (IsGroup(occupantObject, kGroupA) || IsGroup(occupantObject, kGroupB) || IsGroup(occupantObject, kGroupC))
        kind = 2;
    if (HasProperty(holder, kPropertyExcluded2)) {
        kind = 2;
    } else if (kind == 1) {
        bool flag2 = false;
        bool flag1 = false;
        bool flag0 = false;
        GetPropertyBool(holder, kPropertyBool2, &flag2);
        GetPropertyBool(holder, kPropertyBool1, &flag1);
        GetPropertyBool(holder, kPropertyBool0, &flag0);
        if (flag2)
            kind = 3;
        if (flag1)
            kind = 2;
        if (!flag0)
            kind = 1;
    }
    if (third)
        ReleaseOf(third);
    if (second)
        ReleaseOf(second);
    if (subway)
        ReleaseOf(subway);
    if (pipe)
        ReleaseOf(pipe);
    return kind;
}

// The game's convention for 0x005E9CA0: the occupant in ESI, the answer in
// EAX, EBX, EBP and EDI kept.
__declspec(naked) void KindThunk()
{
    __asm push esi
    __asm call OccupantKind
    __asm add esp, 4
    __asm ret
}

// 0x005E9E40: slot 3 of the view level filter.
uint32_t __fastcall KindIsOccupantIncluded(char* self, void*, void* occupantObject)
{
    OCCFILTERS_FIRST("KindIsOccupantIncluded");
    return OccupantKind(occupantObject) == At<uint32_t>(self, field::kValue) ? 1 : 0;
}

// 0x005E9E60: Write of the view level filter; the kind is the part's +8,
// written as a byte.
uint32_t __fastcall KindWrite(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("KindWrite");
    Virtual<void>(stream, stream::kU16, uint32_t{1});
    Virtual<void>(stream, stream::kU8, static_cast<uint32_t>(At<uint8_t>(part, 0x8)));
    return NoError(stream) ? 1 : 0;
}

// 0x005E9E90: Read of the view level filter. The version slot is the slot
// the game's `push ecx` left holding the part pointer; the byte is the low
// byte of the stream argument's slot.
uint32_t __fastcall KindRead(char* part, void*, void* stream)
{
    OCCFILTERS_FIRST("KindRead");
    uint16_t version = LowWord(part);
    Virtual<void>(stream, stream::kU16, &version);
    if (version != 1)
        return 0;
    uint8_t level = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<void>(stream, stream::kU8, &level);
    At<uint32_t>(part, 0x8) = level;
    return NoError(stream) ? 1 : 0;
}

// --- the string specification filter's two small methods -------------------------------

// 0x005E9EE0: the occupant manager's GetOccupantsByBBox over the filter's
// own box, with the filter. EAX when there is no manager: the object with
// its low byte cleared.
uint32_t __fastcall SpecGetOccupants(char* self, void*, void* list, int32_t limit)
{
    OCCFILTERS_FIRST("SpecGetOccupants");
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    if (!manager)
        return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self)) & 0xFFFFFF00u;
    return Virtual<uint32_t>(manager, 0x90, list, reinterpret_cast<float*>(self + spec::kLow),
                             reinterpret_cast<float*>(self + spec::kHigh), static_cast<void*>(self), limit);
}

// 0x005E9F20: slot 3 of the string specification filter. An empty vector
// passes; the type, a group and the kind are looked for in theirs.
uint32_t __fastcall SpecIsOccupantIncluded(char* self, void*, void* occupantObject)
{
    OCCFILTERS_FIRST("SpecIsOccupantIncluded");
    bool typeOk = At<uint32_t*>(self, spec::kTypes) == At<uint32_t*>(self, spec::kTypes + 4);
    bool groupOk = At<uint32_t*>(self, spec::kGroups) == At<uint32_t*>(self, spec::kGroups + 4);
    bool kindOk = At<uint32_t*>(self, spec::kKinds) == At<uint32_t*>(self, spec::kKinds + 4);
    if (!typeOk) {
        const uint32_t type = Virtual<uint32_t>(occupantObject, occupant::kGetType);
        const Uint32Vector& types = At<Uint32Vector>(self, spec::kTypes);
        const uint32_t count = static_cast<uint32_t>(types.end - types.begin);
        for (uint32_t i = 0; i < count; ++i) {
            if (type == types.begin[i]) {
                typeOk = true;
                break;
            }
        }
    }
    if (!groupOk) {
        const uint32_t count = static_cast<uint32_t>(At<Uint32Vector>(self, spec::kGroups).end -
                                                     At<Uint32Vector>(self, spec::kGroups).begin);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t group = At<Uint32Vector>(self, spec::kGroups).begin[i];
            if (IsGroup(occupantObject, group)) {
                groupOk = true;
                break;
            }
        }
    }
    if (!kindOk) {
        const uint32_t kind = OccupantKind(occupantObject);
        const Uint32Vector& kinds = At<Uint32Vector>(self, spec::kKinds);
        const uint32_t count = static_cast<uint32_t>(kinds.end - kinds.begin);
        for (uint32_t i = 0; i < count; ++i) {
            if (kind == kinds.begin[i]) {
                kindOk = true;
                break;
            }
        }
    }
    return typeOk && groupOk && kindOk ? 1 : 0;
}

// --- the group filter ------------------------------------------------------------------

// 0x005EA020: slot 3 of the group filter.
uint32_t __fastcall GroupIsOccupantIncluded(char* self, void*, void* occupantObject)
{
    OCCFILTERS_FIRST("GroupIsOccupantIncluded");
    if (!occupantObject)
        return 0;
    const Uint32Vector& groups = At<Uint32Vector>(self, field::kValue);
    for (const uint32_t* at = groups.begin; at != groups.end; ++at)
        if (IsGroup(occupantObject, *at))
            return 1;
    return 0;
}

// The game's push_back of a uint32 (inline in 0x005EA2F0; the constructor's
// copy tests the end against 0 instead of the capacity).
void PushGroup(char* vector, uint32_t* where, uint32_t* group)
{
    CallThis<void>(addr::SC4PtrVector_InsertN, vector, where, group, group, uint32_t{1}, uint32_t{1});
}

// 0x005EA280: the group filter with one group. The end is 0 in a new
// vector, so the game's test (end == 0, not end == capacity) always grows
// it; its other branch is not reachable.
char* __fastcall GroupConstruct(char* self, void*, uint32_t group)
{
    OCCFILTERS_FIRST("GroupConstruct");
    At<uint32_t>(self, field::kRefCount) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::SC4OccupantGroupFilter_vftable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4OccupantGroupFilter_vftable_4;
    At<Uint32Vector>(self, field::kValue) = {nullptr, nullptr, nullptr};
    if (group != 0)
        PushGroup(self + field::kValue, nullptr, &group);
    return self;
}

// 0x005EA2F0: slot 7 of the group filter vtable, AddGroup.
void __fastcall GroupAdd(char* self, void*, uint32_t group)
{
    OCCFILTERS_FIRST("GroupAdd");
    if (group == 0)
        return;
    char* const vector = self + field::kValue;
    uint32_t* const end = At<uint32_t*>(self, field::kVectorEnd);
    if (end == At<uint32_t*>(self, field::kVectorCapacity)) {
        PushGroup(vector, end, &group);
        return;
    }
    if (end)
        *end = group;
    At<uint32_t*>(self, field::kVectorEnd) = end + 1;
}

// 0x005EA440: the group filter copying a vector of groups.
char* __fastcall GroupCopyConstruct(char* self, void*, const void* groups)
{
    OCCFILTERS_FIRST("GroupCopyConstruct");
    At<uint32_t>(self, field::kRefCount) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::SC4OccupantGroupFilter_vftable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4OccupantGroupFilter_vftable_4;
    At<Uint32Vector>(self, field::kValue) = {nullptr, nullptr, nullptr};
    CallThis<void*>(addr::SC4Uint32Vector_CopyAssign, self + field::kValue, groups);
    return self;
}

// 0x005EA420: slot 6 of the types and group filters.
char* __fastcall WithListDeletingDestructor(char* self, void*, uint32_t flags)
{
    OCCFILTERS_FIRST("WithListDeletingDestructor");
    CallThis<void>(addr::SC4OccupantFilterWithList_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// --- the factories ---------------------------------------------------------------------

// Each: the object from the allocator (null stays null), the vtables, the
// reference count 0 (+8) and the class's fields. The game stores 0x00A80784
// in +4 first and overwrites it at once, which is left out.
char* NewObject(std::size_t size)
{
    return static_cast<char*>(CallCdecl<void*, std::size_t>(addr::SC4_operator_new, size));
}

// 0x005EA130
char* __cdecl BaseFactory()
{
    OCCFILTERS_FIRST("BaseFactory");
    char* const object = NewObject(0xC);
    if (!object)
        return nullptr;
    At<std::uintptr_t>(object, 0x0) = addr::SC4OccupantFilterBase_vftable;
    At<std::uintptr_t>(object, field::kSerializable) = addr::SC4OccupantFilterBase_vftable_4;
    At<uint32_t>(object, field::kRefCount) = 0;
    return object;
}

// 0x005EA160
char* __cdecl DefaultFactory()
{
    OCCFILTERS_FIRST("DefaultFactory");
    char* const object = NewObject(0xC);
    if (!object)
        return nullptr;
    At<uint32_t>(object, field::kRefCount) = 0;
    At<std::uintptr_t>(object, 0x0) = addr::SC4OccupantFilterAll_Vtable;
    At<std::uintptr_t>(object, field::kSerializable) = addr::SC4OccupantFilterAll_Vtable2;
    return object;
}

// 0x005EA190
char* __cdecl TypeFactory()
{
    OCCFILTERS_FIRST("TypeFactory");
    char* const object = NewObject(0x10);
    if (!object)
        return nullptr;
    At<uint32_t>(object, field::kRefCount) = 0;
    At<std::uintptr_t>(object, 0x0) = addr::SC4OccupantTypeFilter_Vtable;
    At<std::uintptr_t>(object, field::kSerializable) = addr::SC4OccupantTypeFilter_Vtable2;
    At<uint32_t>(object, field::kValue) = 0;
    return object;
}

// 0x005EA1C0
char* __cdecl NetworkFactory()
{
    OCCFILTERS_FIRST("NetworkFactory");
    char* const object = NewObject(0x10);
    if (!object)
        return nullptr;
    At<uint32_t>(object, field::kRefCount) = 0;
    At<std::uintptr_t>(object, 0x0) = addr::SC4NetworkOccupantFilter_vftable;
    At<std::uintptr_t>(object, field::kSerializable) = addr::SC4NetworkOccupantFilter_vftable_4;
    At<uint32_t>(object, field::kValue) = 0x1FFF;
    return object;
}

// 0x005EA200
char* __cdecl KindFactory()
{
    OCCFILTERS_FIRST("KindFactory");
    char* const object = NewObject(0x10);
    if (!object)
        return nullptr;
    At<uint32_t>(object, field::kRefCount) = 0;
    At<std::uintptr_t>(object, 0x0) = addr::SC4OccupantFilterByKind_vftable;
    At<std::uintptr_t>(object, field::kSerializable) = addr::SC4OccupantFilterByKind_vftable_4;
    At<uint32_t>(object, field::kValue) = 1;
    return object;
}

// 0x005EA240
char* __cdecl RealisticFactory()
{
    OCCFILTERS_FIRST("RealisticFactory");
    char* const object = NewObject(0x10);
    if (!object)
        return nullptr;
    At<uint32_t>(object, field::kRefCount) = 0;
    At<std::uintptr_t>(object, 0x0) = addr::SC4CityFiles_PictureFilterVtable;
    At<std::uintptr_t>(object, field::kSerializable) = addr::SC4CityFiles_PictureFilterVtable4;
    At<uint8_t>(object, field::kRealisticLot) = 0;
    At<uint8_t>(object, field::kRealisticGroup) = 0;
    At<uint8_t>(object, field::kRealisticProp) = 0;
    return object;
}

// 0x005EA330
char* __cdecl TypesFactory()
{
    OCCFILTERS_FIRST("TypesFactory");
    char* const object = NewObject(0x18);
    if (!object)
        return nullptr;
    At<uint32_t>(object, field::kRefCount) = 0;
    At<std::uintptr_t>(object, 0x0) = addr::SC4OccupantTypeFilter_vftable;
    At<std::uintptr_t>(object, field::kSerializable) = addr::SC4OccupantTypeFilter_vftable_4;
    At<Uint32Vector>(object, field::kValue) = {nullptr, nullptr, nullptr};
    return object;
}

// --- the lot's own filter --------------------------------------------------------------

// 0x006BFB80: slot 3 of the lot filter (made by GetLotOccupants): the
// occupant's x and z within the box, inclusive (a NaN is out), and both
// embedded filters. The position is three floats the game leaves on its
// stack.
uint32_t __fastcall LotFilterIsOccupantIncluded(char* self, void*, void* occupantObject)
{
    OCCFILTERS_FIRST("LotFilterIsOccupantIncluded");
    float position[3];
    Virtual<void>(occupantObject, occupant::kGetPosition, static_cast<float*>(position));
    if (!(position[0] >= At<float>(self, lotfilter::kMinX)))
        return 0;
    if (!(position[2] >= At<float>(self, lotfilter::kMinZ)))
        return 0;
    if (!(position[0] <= At<float>(self, lotfilter::kMaxX)))
        return 0;
    if (!(position[2] <= At<float>(self, lotfilter::kMaxZ)))
        return 0;
    if (!Virtual<uint8_t>(self + lotfilter::kDefault, kFilterIsOccupantIncluded, occupantObject))
        return 0;
    if (!Virtual<uint8_t>(self + lotfilter::kRealistic, kFilterIsOccupantIncluded, occupantObject))
        return 0;
    return 1;
}

// 0x006BFC10: slot 6 of the lot filter.
char* __fastcall LotFilterDeletingDestructor(char* self, void*, uint32_t flags)
{
    OCCFILTERS_FIRST("LotFilterDeletingDestructor");
    BaseDestroy(self + lotfilter::kRealistic, nullptr);
    BaseDestroy(self + lotfilter::kDefault, nullptr);
    BaseDestroy(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::SC4OccupantFilterBase_GetGZCLSID, BaseGetGZCLSID),
    OPENSC4_DETOUR(addr::SC4OccupantFilterBase_Destroy, BaseDestroyThunk),
    OPENSC4_DETOUR(addr::SC4OccupantFilterBase_QueryInterface, BaseQueryInterface),
    OPENSC4_DETOUR(addr::cSC4DefaultOccupantFilter_Test, DefaultIsOccupantIncluded),
    OPENSC4_DETOUR(addr::SC4OccupantFilterAll_IsPropertyHolderIncluded, DefaultIsPropertyHolderIncluded),
    OPENSC4_DETOUR(addr::SC4OccupantTypeFilter_GetGZCLSID, TypeGetGZCLSID),
    OPENSC4_DETOUR(addr::SC4OccupantTypeFilter_IsOccupantTypeIncluded, TypeIsOccupantTypeIncluded),
    OPENSC4_DETOUR(addr::SC4OccupantTypesFilter_GetGZCLSID, TypesGetGZCLSID),
    OPENSC4_DETOUR(addr::SC4OccupantTypesHashedFilter_GetGZCLSID, TypesHashedGetGZCLSID),
    OPENSC4_DETOUR(addr::SC4OccupantFilterByKind_GetGZCLSID, KindGetGZCLSID),
    OPENSC4_DETOUR(addr::SC4OccupantFilterBase_DeletingDestructor, BaseDeletingDestructor),
    OPENSC4_DETOUR(addr::SC4OccupantFilterBase_Write, BaseWrite),
    OPENSC4_DETOUR(addr::SC4OccupantFilter_Read, BaseRead),
    OPENSC4_DETOUR(addr::SC4OccupantFilterAll_Write, DefaultWrite),
    OPENSC4_DETOUR(addr::SC4OccupantFilterAll_Read, DefaultRead),
    OPENSC4_DETOUR(addr::SC4OccupantTypeFilter_Write, TypeWrite),
    OPENSC4_DETOUR(addr::SC4OccupantTypeFilter_Read, TypeRead),
    OPENSC4_DETOUR(addr::SC4OccupantTypesFilter_IsOccupantTypeIncluded, TypesIsOccupantTypeIncluded),
    OPENSC4_DETOUR(addr::cSC4RealisticOccupantFilter_Test, RealisticIsOccupantIncluded),
    OPENSC4_DETOUR(addr::SC4RealisticOccupantFilter_Write, RealisticWrite),
    OPENSC4_DETOUR(addr::SC4RealisticOccupantFilter_Read, RealisticRead),
    OPENSC4_DETOUR(addr::SC4NetworkOccupantFilter_IsOccupantIncluded, NetworkIsOccupantIncluded),
    OPENSC4_DETOUR(addr::SC4NetworkOccupantFilter_Write, NetworkWrite),
    OPENSC4_DETOUR(addr::SC4NetworkOccupantFilter_Read, NetworkRead),
    OPENSC4_DETOUR(addr::SC4OccupantFilterByKind_GetKind, KindThunk),
    OPENSC4_DETOUR(addr::SC4OccupantFilterByKind_IsOccupantIncluded, KindIsOccupantIncluded),
    OPENSC4_DETOUR(addr::SC4OccupantFilterByKind_Write, KindWrite),
    OPENSC4_DETOUR(addr::SC4OccupantFilterByKind_Read, KindRead),
    OPENSC4_DETOUR(addr::SC4StringSpecificationOccupantFilter_GetOccupants, SpecGetOccupants),
    OPENSC4_DETOUR(addr::SC4StringSpecificationOccupantFilter_IsOccupantIncluded, SpecIsOccupantIncluded),
    OPENSC4_DETOUR(addr::SC4OccupantGroupFilter_IsOccupantIncluded, GroupIsOccupantIncluded),
    OPENSC4_DETOUR(addr::SC4_OccupantFilterBase, BaseConstruct),
    OPENSC4_DETOUR(addr::SC4OccupantTypesFilter_Write, TypesWrite),
    OPENSC4_DETOUR(addr::SC4NetworkOccupantFilter_GetGZCLSID, NetworkGetGZCLSID),
    OPENSC4_DETOUR(addr::SC4OccupantFilterBase_Factory, BaseFactory),
    OPENSC4_DETOUR(addr::SC4OccupantFilterAll_Factory, DefaultFactory),
    OPENSC4_DETOUR(addr::SC4OccupantTypeFilter_Factory, TypeFactory),
    OPENSC4_DETOUR(addr::SC4NetworkOccupantFilter_Factory, NetworkFactory),
    OPENSC4_DETOUR(addr::SC4OccupantFilterByKind_Factory, KindFactory),
    OPENSC4_DETOUR(addr::SC4RealisticOccupantFilter_Factory, RealisticFactory),
    OPENSC4_DETOUR(addr::SC4OccupantGroupFilter_SC4OccupantGroupFilter, GroupConstruct),
    OPENSC4_DETOUR(addr::SC4OccupantGroupFilter_AddGroup, GroupAdd),
    OPENSC4_DETOUR(addr::SC4OccupantTypesFilter_Factory, TypesFactory),
    OPENSC4_DETOUR(addr::SC4OccupantTypesHashedFilter_Write, TypesHashedWrite),
    OPENSC4_DETOUR(addr::SC4OccupantFilterWithList_DeletingDestructor, WithListDeletingDestructor),
    OPENSC4_DETOUR(addr::SC4OccupantGroupFilter_CopyConstruct, GroupCopyConstruct),
    OPENSC4_DETOUR(addr::SC4OccupantTypesFilter_Read, TypesRead),
    OPENSC4_DETOUR(addr::SC4OccupantTypesHashedFilter_Read, TypesHashedRead),
    OPENSC4_DETOUR(addr::cSC4LotOccupantFilter_IsOccupantIncluded, LotFilterIsOccupantIncluded),
    OPENSC4_DETOUR(addr::cSC4LotOccupantFilter_DeletingDestructor, LotFilterDeletingDestructor),
};

// A detour at each address (entry bytes checked by hook::Install). The
// originals are not called again.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : kDetours)
        if (!addr_check::Find(detour.address)) {
            log::Write("occfilters: no address entry for 0x%08X", static_cast<unsigned>(detour.address));
            return false;
        }
    for (const Detour& detour : kDetours) {
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
    }
    log::Write("occfilters: %u detours", static_cast<unsigned>(sizeof(kDetours) / sizeof(kDetours[0])));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.occfilters.part1", "simulators/occfilters", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::SC4OccupantFilterBase_Destroy,
     "Our small occupant filters and their base class (default, type, types, hashed types, realistic, network, "
     "view level, group), part of the string specification filter's small methods and the lot's own filter "
     "(0x005E95B0 to 0x005EA610; 50 detours; src/core/occfilters_1.cpp)",
     false, Install});

} // namespace
} // namespace opensc4::occfilters_1
