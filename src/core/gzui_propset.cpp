// The GZ framework's property set and its five value classes (docs/gzui.md,
// part u1) on the game's own objects. Replacements, each off by default:
//
//   framework.propset.set     the property set (vtable 0x00AD27D4): the
//                             constructor (0x00939237), QueryInterface,
//                             Release, SetProperty with its Insert,
//                             RemoveProperty with its EraseNode, RemoveAll,
//                             GetFirst, GetNext, GetProperty, HasProperty,
//                             the destructor and the deleting destructor
//   framework.propset.values  the uint32, sint32, float and bool values: the
//                             base's deleting destructor, DataSize,
//                             CompareBytes, the numbers' CopyFrom and Assign,
//                             the bool's, and each value's Clone and GetType
//   framework.propset.string  the string value: its constructors, its
//                             destructors, CopyFrom, Assign, Clone, GetType
//                             and Compare
//
// The set (0x18 bytes): +0x00 vtable, +0x04 reference count, +0x08 an STLport
// map of uint32 keys to byte blocks (+0x08 its header, +0x0C its size),
// +0x14 the cursor of GetFirst and GetNext. A node holds the key at +0x10
// and the block (begin, end, capacity) from +0x14; the value object lives
// in its block, made there by the value's Clone and destroyed there by its
// slot 0 with flag 0. A value is 8 bytes: its vtable and its data at +4 (a
// dword, a byte for the bool, a counted game string for the string).
//
// Still the game's: AddRef (0x00968B60) and Count (0x009CEDE1), the value's
// size (0x00940DA8, 8): small functions shared with unrelated classes; the
// map's STLport code (find, insert, erase, clear, the iterator) and the
// counted reference's assignment, which are shared or template code. Ours
// call them by address.
//
// The game's methods answer a byte (AL) and leave something in the rest of
// EAX; ours give the same EAX where it is known (the find's result, a type,
// a copied value), and 0 above AL where the game leaves its caller's EAX.
//
// Game code calls some of these directly (the constructor, Insert, the
// string value's constructors) and may keep ECX and EDX across a call:
// every detour is entered through a thunk that keeps both.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::gzui {
namespace {

// ====================================================================================
// Helpers

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

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

inline uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// A byte answer over what the game leaves in the rest of EAX.
inline uint32_t Answer(uint32_t eax, bool value) { return (eax & ~0xFFu) | (value ? 1u : 0u); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: propset: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

constexpr std::size_t kRefs = 0x04;
constexpr std::size_t kMap = 0x08;
constexpr std::size_t kCursor = 0x14;
constexpr std::size_t kNodeKey = 0x10;
constexpr std::size_t kNodeBlock = 0x14;
constexpr std::size_t kData = 0x04;

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_cIGZPropertySet = 0xEBB10CDD;

constexpr uint32_t kTypeUint32 = 0xEB61E4F7;
constexpr uint32_t kTypeSint32 = 0x0C264712;
constexpr uint32_t kTypeFloat32 = 0xABC78708;
constexpr uint32_t kTypeBool = 0xCBA908E1;
constexpr uint32_t kTypeString = 0x0B8BEA18;

// The map's header, a node's successor and the find (the game's STLport).
char* Header(char* self) { return At<char*>(self, kMap); }

char* Successor(char* node) { return CallCdecl<char*>(addr::cSC4Advisor_F40D4C0, node); }

char* Find(char* self, const uint32_t* key) { return CallThis<char*>(addr::cGZCOM_ClassMapFind, self + kMap, key); }

// A value destroyed where it lives (its slot 0 with flag 0).
void DestroyInPlace(void* value) { Virtual<void*>(value, 0x0, uint32_t{0}); }

// A byte vector as STLport lays it out.
struct ByteVector {
    char* begin;
    char* end;
    char* capacity;
};

// ====================================================================================
// The property set

// The game's 0x00939237.
char* __fastcall Construct(char* self, void*)
{
    OPENSC4_FIRST("Construct");
    At<uint32_t>(self, kRefs) = 0;
    At<std::uintptr_t>(self, 0) = addr::cGZPropertySet_Vtable;
    CallThis<void*>(addr::cGZDBSegmentPackedFile_OpenTreeConstruct, self + kMap);
    At<char*>(self, kCursor) = nullptr;
    return self;
}

// The game's 0x00939254.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("QueryInterface");
    if (iid != kIID_cIGZUnknown && iid != kIID_cIGZPropertySet)
        return 0;
    *out = self;
    return Answer(Virtual<uint32_t>(self, 0x4), true);
}

// The game's 0x00939279: at a count of 1 or less the deleting destructor
// (slot 11), without the count lowered.
uint32_t __fastcall Release(char* self, void*)
{
    OPENSC4_FIRST("Release");
    const uint32_t count = At<uint32_t>(self, kRefs);
    if (count <= 1) {
        Virtual<void*>(self, 0x2C, uint32_t{1});
        return 0;
    }
    At<uint32_t>(self, kRefs) = count - 1;
    return count - 1;
}

// The game's 0x00939431 (SetProperty's, for a new key): a zeroed block of
// the value's size, the pair copied into the map, the value cloned into
// the node's block. The node's pair, or 0 when the key was there.
uint32_t __fastcall Insert(char* self, void*, const uint32_t* key, void* value)
{
    OPENSC4_FIRST("Insert");
    const uint32_t k = *key;
    ByteVector zeroed;
    CallThis<void*>(addr::SC4ByteVector_ConstructZeroed, &zeroed, Virtual<uint32_t>(value, 0x14));
    struct {
        uint32_t key;
        ByteVector block;
    } pair;
    pair.key = k;
    CallThis<void*>(addr::SC4ByteVector_CopyConstruct, &pair.block, &zeroed);
    struct {
        char* node;
        uint8_t inserted;
    } result;
    CallThis<void*>(addr::GZPropertyMap_InsertUnique, self + kMap, &result, &pair);
    char* const entry = result.node + kNodeKey;
    uint32_t answer = 0;
    if (result.inserted) {
        Virtual<void*>(value, 0xC, At<char*>(entry, 4));
        answer = Bits(entry);
    }
    if (pair.block.begin)
        CallCdecl<void>(addr::SC4_Free, pair.block.begin);
    if (zeroed.begin)
        CallCdecl<void>(addr::SC4_Free, zeroed.begin);
    return answer;
}

// The game's 0x009393D4: a key already there has its old value destroyed
// in place, the block resized to the new value's size and the new value
// cloned into it.
uint32_t __fastcall SetProperty(char* self, void*, const uint32_t* key, void* value)
{
    OPENSC4_FIRST("SetProperty");
    char* const node = Find(self, key);
    if (node == Header(self))
        return Answer(CallThis<uint32_t>(addr::cGZPropertySet_Insert, self, key, value), true);
    char** const block = &At<char*>(node, kNodeBlock);
    DestroyInPlace(*block);
    const uint32_t size = Virtual<uint32_t>(value, 0x14);
    const uint8_t fill = 0;
    CallThis<void>(addr::SC4ByteVector_Resize2, block, size, &fill);
    return Answer(Bits(Virtual<void*>(value, 0xC, *block)), true);
}

// The game's 0x00939380 (this is the map): the node out of the tree, its
// pair's block and the node freed, the size lowered.
uint32_t __fastcall EraseNode(char* map, void*, char* node)
{
    OPENSC4_FIRST("EraseNode");
    char* const header = At<char*>(map, 0);
    char* const erased =
        CallCdecl<char*>(addr::SC4Tree_RebalanceForErase, node, header + 0x4, header + 0x8, header + 0xC);
    uint32_t eax = Bits(CallThis<void*>(addr::GZPropertyPair_Destroy, erased + kNodeKey, uint32_t{0}));
    if (erased)
        eax = CallCdecl<uint32_t>(addr::SC4_Free, erased);
    --At<uint32_t>(map, 0x4);
    return eax;
}

// The game's 0x0093934A.
uint32_t __fastcall RemoveProperty(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("RemoveProperty");
    char* const node = Find(self, key);
    if (node == Header(self))
        return Answer(Bits(node), false);
    DestroyInPlace(At<void*>(node, kNodeBlock));
    return Answer(CallThis<uint32_t>(addr::cGZPropertySet_EraseNode, self + kMap, node), true);
}

// The game's 0x00939040: every value destroyed in key order, then the
// map's clear.
uint32_t __fastcall RemoveAll(char* self, void*)
{
    OPENSC4_FIRST("RemoveAll");
    for (char* node = At<char*>(Header(self), 0x8); node != Header(self); node = Successor(node))
        DestroyInPlace(At<void*>(node, kNodeBlock));
    return CallThis<uint32_t>(addr::GZPropertyMap_Clear, self + kMap);
}

// The game's 0x00939290: with both pointers, the cursor at the first node.
uint32_t __fastcall GetFirst(char* self, void*, uint32_t* key, void** block)
{
    OPENSC4_FIRST("GetFirst");
    if (!key || !block)
        return 0;
    char* const header = Header(self);
    char* const first = At<char*>(header, 0x8);
    At<char*>(self, kCursor) = first;
    if (first == header)
        return Answer(Bits(header), false);
    *key = At<uint32_t>(first, kNodeKey);
    *block = At<void*>(first, kNodeBlock);
    return Answer(Bits(header), true);
}

// The game's 0x009392C7: the cursor moves on whatever the pointers.
uint32_t __fastcall GetNext(char* self, void*, uint32_t* key, void** block)
{
    OPENSC4_FIRST("GetNext");
    char* const next = Successor(At<char*>(self, kCursor));
    At<char*>(self, kCursor) = next;
    if (!key || !block)
        return Answer(Bits(next), false);
    if (next == Header(self))
        return Answer(Bits(next), false);
    *key = At<uint32_t>(next, kNodeKey);
    *block = At<void*>(next, kNodeBlock);
    return Answer(Bits(next), true);
}

// The game's 0x00939306: the out value's CopyFrom (slot 1) with the stored
// one.
uint32_t __fastcall GetProperty(char* self, void*, const uint32_t* key, void* out)
{
    OPENSC4_FIRST("GetProperty");
    char* const node = Find(self, key);
    if (node == Header(self))
        return Answer(Bits(node), false);
    return Virtual<uint32_t>(out, 0x4, At<void*>(node, kNodeBlock));
}

// The game's 0x0093932E.
uint32_t __fastcall HasProperty(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("HasProperty");
    char* const node = Find(self, key);
    return Answer(Bits(node), node != Header(self));
}

// The game's 0x009393BD: RemoveAll by its address, then the map's
// destructor.
uint32_t __fastcall Destruct(char* self, void*)
{
    OPENSC4_FIRST("Destruct");
    At<std::uintptr_t>(self, 0) = addr::cGZPropertySet_Vtable;
    CallThis<uint32_t>(addr::cGZPropertySet_RemoveAll, self);
    return CallThis<uint32_t>(addr::GZPropertyMap_Destroy, self + kMap);
}

// The game's 0x00939607.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("DeletingDestructor");
    CallThis<uint32_t>(addr::cGZPropertySet_Destruct, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// The uint32, sint32, float and bool values

// The game's 0x00940BC5 (the value base's slot 0).
char* __fastcall ValueDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("ValueDeletingDestructor");
    At<std::uintptr_t>(self, 0) = addr::cGZPropertyValue_Vtable;
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00940DAC: the object's size (slot 5) less 4.
uint32_t __fastcall DataSize(char* self, void*)
{
    OPENSC4_FIRST("DataSize");
    return Virtual<uint32_t>(self, 0x14) - 4;
}

// The game's 0x009378D3: the four data bytes as signed chars, in memory
// order.
int32_t __fastcall CompareBytes(char* self, void*, char* other)
{
    OPENSC4_FIRST("CompareBytes");
    const auto* const mine = reinterpret_cast<const int8_t*>(self + kData);
    const auto* const theirs = reinterpret_cast<const int8_t*>(other + kData);
    for (int i = 0; i < 4; ++i) {
        if (mine[i] < theirs[i])
            return -1;
        if (mine[i] > theirs[i])
            return 1;
    }
    return 0;
}

// The numbers' and the bool's copy: the other's type first (slot 4), then
// this one's; the data copied when they match.
uint32_t CopyDword(char* self, char* other)
{
    const uint32_t theirs = Virtual<uint32_t>(other, 0x10);
    const uint32_t type = Virtual<uint32_t>(self, 0x10);
    if (type != theirs)
        return Answer(type, false);
    const uint32_t data = At<uint32_t>(other, kData);
    At<uint32_t>(self, kData) = data;
    return Answer(data, true);
}

uint32_t CopyByte(char* self, char* other)
{
    const uint32_t theirs = Virtual<uint32_t>(other, 0x10);
    const uint32_t type = Virtual<uint32_t>(self, 0x10);
    if (type != theirs)
        return Answer(type, false);
    At<uint8_t>(self, kData) = At<uint8_t>(other, kData);
    return Answer(type, true);
}

// The game's 0x00940DB5 and 0x00937EC2 (without the null test).
uint32_t __fastcall NumberCopyFrom(char* self, void*, char* other)
{
    OPENSC4_FIRST("NumberCopyFrom");
    if (!other)
        return 0;
    return CopyDword(self, other);
}

uint32_t __fastcall NumberAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("NumberAssign");
    return CopyDword(self, other);
}

// The game's 0x00940BE2 and 0x00940C15.
uint32_t __fastcall BoolCopyFrom(char* self, void*, char* other)
{
    OPENSC4_FIRST("BoolCopyFrom");
    if (!other)
        return 0;
    return CopyByte(self, other);
}

uint32_t __fastcall BoolAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("BoolAssign");
    return CopyByte(self, other);
}

// The Clones: a copy at place (nothing for null), place answered.
char* CloneDword(char* self, char* place, std::uintptr_t vtable)
{
    if (place) {
        At<std::uintptr_t>(place, 0) = vtable;
        At<uint32_t>(place, kData) = At<uint32_t>(self, kData);
    }
    return place;
}

// The game's 0x00937EF1, 0x0093BB22, 0x00940DE8 and 0x00940C44.
char* __fastcall Uint32Clone(char* self, void*, char* place)
{
    OPENSC4_FIRST("Uint32Clone");
    return CloneDword(self, place, addr::cGZPropertyUint32_Vtable);
}

char* __fastcall Float32Clone(char* self, void*, char* place)
{
    OPENSC4_FIRST("Float32Clone");
    return CloneDword(self, place, addr::cGZPropertyFloat32_Vtable);
}

char* __fastcall Sint32Clone(char* self, void*, char* place)
{
    OPENSC4_FIRST("Sint32Clone");
    return CloneDword(self, place, addr::cGZPropertySint32_Vtable);
}

char* __fastcall BoolClone(char* self, void*, char* place)
{
    OPENSC4_FIRST("BoolClone");
    if (place) {
        At<std::uintptr_t>(place, 0) = addr::cGZPropertyBool_Vtable;
        At<uint8_t>(place, kData) = At<uint8_t>(self, kData);
    }
    return place;
}

// The game's 0x009378CD, 0x0093AAAF, 0x00940DA2, 0x00940BBF and 0x0093FFCE.
uint32_t __fastcall Uint32GetType(char*, void*)
{
    OPENSC4_FIRST("Uint32GetType");
    return kTypeUint32;
}

uint32_t __fastcall Float32GetType(char*, void*)
{
    OPENSC4_FIRST("Float32GetType");
    return kTypeFloat32;
}

uint32_t __fastcall Sint32GetType(char*, void*)
{
    OPENSC4_FIRST("Sint32GetType");
    return kTypeSint32;
}

uint32_t __fastcall BoolGetType(char*, void*)
{
    OPENSC4_FIRST("BoolGetType");
    return kTypeBool;
}

uint32_t __fastcall StringGetType(char*, void*)
{
    OPENSC4_FIRST("StringGetType");
    return kTypeString;
}

// ====================================================================================
// The string value

// The game's 0x0094002A: the string released (its pointer left), the
// base's vtable.
uint32_t __fastcall StringDestruct(char* self, void*)
{
    OPENSC4_FIRST("StringDestruct");
    uint32_t eax = 0;
    if (void* const string = At<void*>(self, kData))
        eax = Virtual<uint32_t>(string, 0x8);
    At<std::uintptr_t>(self, 0) = addr::cGZPropertyValue_Vtable;
    return eax;
}

// The game's 0x0094065E.
char* __fastcall StringDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("StringDeletingDestructor");
    CallThis<uint32_t>(addr::cGZPropertyString_Destruct, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00940627: the other's string shared.
char* __fastcall StringCopyConstruct(char* self, void*, char* other)
{
    OPENSC4_FIRST("StringCopyConstruct");
    At<std::uintptr_t>(self, 0) = addr::cGZPropertyString_Vtable;
    void* const string = At<void*>(other, kData);
    At<void*>(self, kData) = string;
    if (string)
        Virtual<uint32_t>(string, 0x4);
    return self;
}

// The game's 0x009407F4 and 0x0094082E: a new game string (0x14 bytes) made
// from the text and held.
char* ConstructFrom(char* self, std::uintptr_t constructor, const void* text)
{
    At<std::uintptr_t>(self, 0) = addr::cGZPropertyString_Vtable;
    At<void*>(self, kData) = nullptr;
    void* const memory = CallCdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
    void* const string = memory ? CallThis<void*>(constructor, memory, text) : nullptr;
    CallThis<void*>(addr::GZRefPtr_Assign, self + kData, string);
    return self;
}

char* __fastcall StringConstructFromIGZString(char* self, void*, const void* text)
{
    OPENSC4_FIRST("StringConstructFromIGZString");
    return ConstructFrom(self, addr::SC4String_FromIGZString, text);
}

char* __fastcall StringConstructFromText(char* self, void*, const char* text)
{
    OPENSC4_FIRST("StringConstructFromText");
    return ConstructFrom(self, addr::SC4String_FromText, text);
}

// The game's 0x0094059B and 0x009405D9 (without the null test): the
// other's string taken unless the other is this value.
uint32_t CopyString(char* self, char* other)
{
    const uint32_t theirs = Virtual<uint32_t>(other, 0x10);
    const uint32_t type = Virtual<uint32_t>(self, 0x10);
    if (type != theirs)
        return Answer(type, false);
    if (other == self)
        return Answer(Bits(other + kData), true);
    return Answer(Bits(CallThis<void*>(addr::GZRefPtr_Assign, self + kData, At<void*>(other, kData))), true);
}

uint32_t __fastcall StringCopyFrom(char* self, void*, char* other)
{
    OPENSC4_FIRST("StringCopyFrom");
    if (!other)
        return 0;
    return CopyString(self, other);
}

uint32_t __fastcall StringAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("StringAssign");
    return CopyString(self, other);
}

// The game's 0x00940613: the copy constructor by its address at place.
char* __fastcall StringClone(char* self, void*, char* place)
{
    OPENSC4_FIRST("StringClone");
    if (!place)
        return nullptr;
    return CallThis<char*>(addr::cGZPropertyString_CopyConstruct, place, self);
}

// The game's 0x00940649: the string's CompareTo (slot 12) with the other's
// string and 1.
int32_t __fastcall StringCompare(char* self, void*, char* other)
{
    OPENSC4_FIRST("StringCompare");
    return Virtual<int32_t>(At<void*>(self, kData), 0x30, At<void*>(other, kData), uint32_t{1});
}

// ====================================================================================
// Thunks and installation

OPENSC4_THUNK(ConstructThunk, Construct, 0)
OPENSC4_THUNK(QueryInterfaceThunk, QueryInterface, 8)
OPENSC4_THUNK(ReleaseThunk, Release, 0)
OPENSC4_THUNK(SetPropertyThunk, SetProperty, 8)
OPENSC4_THUNK(InsertThunk, Insert, 8)
OPENSC4_THUNK(RemovePropertyThunk, RemoveProperty, 4)
OPENSC4_THUNK(EraseNodeThunk, EraseNode, 4)
OPENSC4_THUNK(RemoveAllThunk, RemoveAll, 0)
OPENSC4_THUNK(GetFirstThunk, GetFirst, 8)
OPENSC4_THUNK(GetNextThunk, GetNext, 8)
OPENSC4_THUNK(GetPropertyThunk, GetProperty, 8)
OPENSC4_THUNK(HasPropertyThunk, HasProperty, 4)
OPENSC4_THUNK(DestructThunk, Destruct, 0)
OPENSC4_THUNK(DeletingDestructorThunk, DeletingDestructor, 4)

OPENSC4_THUNK(ValueDeletingDestructorThunk, ValueDeletingDestructor, 4)
OPENSC4_THUNK(DataSizeThunk, DataSize, 0)
OPENSC4_THUNK(CompareBytesThunk, CompareBytes, 4)
OPENSC4_THUNK(NumberCopyFromThunk, NumberCopyFrom, 4)
OPENSC4_THUNK(NumberAssignThunk, NumberAssign, 4)
OPENSC4_THUNK(Uint32CloneThunk, Uint32Clone, 4)
OPENSC4_THUNK(Uint32GetTypeThunk, Uint32GetType, 0)
OPENSC4_THUNK(Float32CloneThunk, Float32Clone, 4)
OPENSC4_THUNK(Float32GetTypeThunk, Float32GetType, 0)
OPENSC4_THUNK(Sint32CloneThunk, Sint32Clone, 4)
OPENSC4_THUNK(Sint32GetTypeThunk, Sint32GetType, 0)
OPENSC4_THUNK(BoolCopyFromThunk, BoolCopyFrom, 4)
OPENSC4_THUNK(BoolAssignThunk, BoolAssign, 4)
OPENSC4_THUNK(BoolCloneThunk, BoolClone, 4)
OPENSC4_THUNK(BoolGetTypeThunk, BoolGetType, 0)

OPENSC4_THUNK(StringDestructThunk, StringDestruct, 0)
OPENSC4_THUNK(StringDeletingDestructorThunk, StringDeletingDestructor, 4)
OPENSC4_THUNK(StringCopyConstructThunk, StringCopyConstruct, 4)
OPENSC4_THUNK(StringConstructFromIGZStringThunk, StringConstructFromIGZString, 4)
OPENSC4_THUNK(StringConstructFromTextThunk, StringConstructFromText, 4)
OPENSC4_THUNK(StringCopyFromThunk, StringCopyFrom, 4)
OPENSC4_THUNK(StringAssignThunk, StringAssign, 4)
OPENSC4_THUNK(StringCloneThunk, StringClone, 4)
OPENSC4_THUNK(StringGetTypeThunk, StringGetType, 0)
OPENSC4_THUNK(StringCompareThunk, StringCompare, 4)

struct Detour {
    std::uintptr_t address;
    void (*ours)();
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.ours), &original))
            return false;
    }
    return true;
}

bool InstallSet(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropertySet_Construct, &ConstructThunk},
        {addr::cGZPropertySet_QueryInterface, &QueryInterfaceThunk},
        {addr::cGZPropertySet_Release, &ReleaseThunk},
        {addr::cGZPropertySet_SetProperty, &SetPropertyThunk},
        {addr::cGZPropertySet_Insert, &InsertThunk},
        {addr::cGZPropertySet_RemoveProperty, &RemovePropertyThunk},
        {addr::cGZPropertySet_EraseNode, &EraseNodeThunk},
        {addr::cGZPropertySet_RemoveAll, &RemoveAllThunk},
        {addr::cGZPropertySet_GetFirst, &GetFirstThunk},
        {addr::cGZPropertySet_GetNext, &GetNextThunk},
        {addr::cGZPropertySet_GetProperty, &GetPropertyThunk},
        {addr::cGZPropertySet_HasProperty, &HasPropertyThunk},
        {addr::cGZPropertySet_Destruct, &DestructThunk},
        {addr::cGZPropertySet_DeletingDestructor, &DeletingDestructorThunk},
    };
    return InstallAll(detours);
}

bool InstallValues(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropertyValue_DeletingDestructor, &ValueDeletingDestructorThunk},
        {addr::cGZPropertyValue_DataSize, &DataSizeThunk},
        {addr::cGZPropertyValue_CompareBytes, &CompareBytesThunk},
        {addr::cGZPropertyNumber_CopyFrom, &NumberCopyFromThunk},
        {addr::cGZPropertyNumber_Assign, &NumberAssignThunk},
        {addr::cGZPropertyUint32_Clone, &Uint32CloneThunk},
        {addr::cGZPropertyUint32_GetType, &Uint32GetTypeThunk},
        {addr::cGZPropertyFloat32_Clone, &Float32CloneThunk},
        {addr::cGZPropertyFloat32_GetType, &Float32GetTypeThunk},
        {addr::cGZPropertySint32_Clone, &Sint32CloneThunk},
        {addr::cGZPropertySint32_GetType, &Sint32GetTypeThunk},
        {addr::cGZPropertyBool_CopyFrom, &BoolCopyFromThunk},
        {addr::cGZPropertyBool_Assign, &BoolAssignThunk},
        {addr::cGZPropertyBool_Clone, &BoolCloneThunk},
        {addr::cGZPropertyBool_GetType, &BoolGetTypeThunk},
    };
    return InstallAll(detours);
}

bool InstallString(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropertyString_Destruct, &StringDestructThunk},
        {addr::cGZPropertyString_DeletingDestructor, &StringDeletingDestructorThunk},
        {addr::cGZPropertyString_CopyConstruct, &StringCopyConstructThunk},
        {addr::cGZPropertyString_ConstructFromIGZString, &StringConstructFromIGZStringThunk},
        {addr::cGZPropertyString_ConstructFromText, &StringConstructFromTextThunk},
        {addr::cGZPropertyString_CopyFrom, &StringCopyFromThunk},
        {addr::cGZPropertyString_Assign, &StringAssignThunk},
        {addr::cGZPropertyString_Clone, &StringCloneThunk},
        {addr::cGZPropertyString_GetType, &StringGetTypeThunk},
        {addr::cGZPropertyString_Compare, &StringCompareThunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.propset.set", "framework/propset", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cGZPropertySet_SetProperty,
                        "Our property set: the constructor, QueryInterface, Release, SetProperty with its insert, "
                        "RemoveProperty with its erase, RemoveAll, GetFirst, GetNext, GetProperty, HasProperty and the "
                        "destructors (14 detours; part u1, docs/gzui.md)",
                        false, InstallSet}),
    registry::Register({"framework.propset.values", "framework/propset", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cGZPropertyNumber_CopyFrom,
                        "Our property values (uint32, sint32, float, bool): the base's deleting destructor, DataSize, "
                        "CompareBytes, CopyFrom, Assign, Clone and GetType (15 detours; part u1, docs/gzui.md)",
                        false, InstallValues}),
    registry::Register({"framework.propset.string", "framework/propset", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cGZPropertyString_CopyFrom,
                        "Our string property value: its constructors and destructors, CopyFrom, Assign, Clone, GetType "
                        "and Compare (10 detours; part u1, docs/gzui.md)",
                        false, InstallString}),
};

} // namespace
} // namespace opensc4::gzui
