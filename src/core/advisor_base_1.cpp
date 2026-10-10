// The advisors' base class, part one of three (docs/lua.md): the game's
// 0x0040CE00 to 0x0040E7C1. Three things live in that range:
//
//   - the place an advice points at (class 0x6A26D049): an optional position
//     kept inside the city, an object and two numbers. Main vtable at +0,
//     serialisable part at +8, interface at +0xC;
//   - the small methods of the advisor base class (interface at +8,
//     serialisable part at +0xC, message target at +0x10): the advice
//     records' lookups, the selection, the save, the messages;
//   - the helpers of its containers: the red-black tree of advice pointers
//     (ordered by the advice's slot 7 value, then by address), the vector of
//     advice records and a byte vector's erase. Some of them are shared with
//     the rest of the game (the tree's rebalancing, the node steps).
//
// Replacement, off by default:
//
//   scripts.advisors.base1: every function of the range that can be hooked.
//
// Left the game's: the one-instruction getters of 4 bytes (0x0040CE50,
// 0x0040CE60, 0x0040CE70, 0x0040CF10, 0x0040D390, 0x0040D3A0, 0x0040D3D0).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <utility>

namespace opensc4::advisor_base {
namespace {

constexpr uint32_t kSerializableIid = 0xE4FDA3D4;
constexpr uint32_t kMessageTargetIid = 0x452294AA;

constexpr uint32_t kPlaceClass = 0x6A26D049;
constexpr uint32_t kPlaceIid = 0x6A26D050;

constexpr uint32_t kAdvisorClass = 0xEAC937B4;
constexpr uint32_t kAdvisorIid = 0xCA09F544;

namespace place { // from the main vtable
constexpr std::size_t kSerializable = 0x8;
constexpr std::size_t kInterface = 0xC;
constexpr std::size_t kHasPosition = 0x10;
constexpr std::size_t kX = 0x14;
constexpr std::size_t kY = 0x18;
constexpr std::size_t kFirst = 0x1C;
constexpr std::size_t kSecond = 0x20;
constexpr std::size_t kObject = 0x24;
constexpr std::size_t kSize = 0x28;
} // namespace place

namespace field { // the advisor, from the main vtable
constexpr std::size_t kInterface = 0x8;
constexpr std::size_t kSerializable = 0xC;
constexpr std::size_t kTarget = 0x10;
constexpr std::size_t kSaved14 = 0x14;
constexpr std::size_t kStringA = 0x18;
constexpr std::size_t kStringB = 0x38;
constexpr std::size_t kTicks = 0x58;
constexpr std::size_t kNow = 0x5C;
constexpr std::size_t kSaved60 = 0x60;
constexpr std::size_t kSaved64 = 0x64;
constexpr std::size_t kRecords = 0x84;      // vector of Record
constexpr std::size_t kSelected = 0x90;
constexpr std::size_t kSelectedKind = 0x94;
constexpr std::size_t kListA = 0x98;        // vectors of 4-byte items
constexpr std::size_t kListB = 0xA4;
constexpr std::size_t kPairs = 0xB0;        // list of (number, object)
} // namespace field

// An advice with its three numbers, as the advisor's vector keeps it.
struct Record {
    void* advice;
    int32_t a;
    int32_t b;
    int32_t c;
};

struct TreeNode {
    uint8_t color; // 0 red, 1 black
    uint8_t pad[3];
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    void* value;
    uint32_t extra;
};

struct Tree {
    TreeNode* header;
    uint32_t count;
    uint8_t less;
};

struct InsertResult {
    TreeNode* node;
    uint8_t inserted;
    uint8_t pad[3];
};

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

uint8_t WriteReference(void* stream, void* object)
{
    return reinterpret_cast<uint8_t(__cdecl*)(void*, void*)>(addr::SC4Stream_WriteReference)(stream, object);
}

// A new message holding one reference, as the game's inlined code makes it.
void* NewMessage()
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message =
        memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (message)
        AddRef(message);
    return message;
}

// ---------------------------------------------------------------------------
// The place.

void SetPlaceVtables(char* self)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4ScriptPlace_Vtable;
    At<std::uintptr_t>(self, place::kSerializable) = addr::SC4ScriptPlace_SerializableVtable;
    At<std::uintptr_t>(self, place::kInterface) = addr::SC4ScriptPlace_InterfaceVtable;
}

// The inlined constructor.
void ConstructPlace(char* self)
{
    reinterpret_cast<void*(__thiscall*)(void*)>(addr::cRZBaseUnknown_Construct)(self);
    SetPlaceVtables(self);
    At<uint8_t>(self, place::kHasPosition) = 0;
    At<uint32_t>(self, place::kFirst) = 0;
    At<void*>(self, place::kObject) = nullptr;
}

// The game's 0x0040CE00, on the serialisable part.
uint32_t __fastcall PlaceClassId(char*, void*)
{
    return kPlaceClass;
}

// The game's 0x0040CE10, on the interface.
float __fastcall PlaceGetX(char* part, void*)
{
    if (At<uint8_t>(part, 0x4))
        return At<float>(part, 0x8);
    return Global<float>(addr::SC4_FloatZero);
}

// The game's 0x0040CE30, on the interface.
float __fastcall PlaceGetY(char* part, void*)
{
    if (At<uint8_t>(part, 0x4))
        return At<float>(part, 0xC);
    return Global<float>(addr::SC4_FloatZero);
}

// The game's 0x0040CE80, on the interface.
uint32_t __fastcall PlaceSetSecond(char* part, void*, uint32_t value)
{
    At<uint32_t>(part, 0x14) = value;
    return value;
}

// The game's 0x0040CE90.
uint8_t __fastcall PlaceQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    std::size_t part;
    if (iid == kPlaceIid)
        part = place::kInterface;
    else if (iid == kSerializableIid)
        part = place::kSerializable;
    else
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            self, iid, out);
    *out = self ? self + part : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x0040CF20, on the serialisable part. The flag is pushed in
// the low byte of the slot that holds the stream argument.
uint8_t __fastcall PlaceWrite(char* part, void*, void* stream)
{
    char* const self = part - place::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, int32_t{4});
    const uint8_t has = At<uint8_t>(self, place::kHasPosition) != 0;
    Virtual<uint8_t>(stream, 0x10, (reinterpret_cast<uint32_t>(stream) & 0xFFFFFF00u) | has);
    if (has) {
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, place::kX));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, place::kY));
    }
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, place::kFirst));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, place::kSecond));
    WriteReference(stream, At<void*>(self, place::kObject));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The game's 0x0040CFA0, on the serialisable part. The version is read into
// a slot that holds `this` before, the flag into the stream argument's.
uint8_t __fastcall PlaceRead(char* part, void*, void* stream)
{
    char* const self = part - place::kSerializable;
    uint32_t version = reinterpret_cast<uint32_t>(part);
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 4)
        return 0;
    uint32_t flag = reinterpret_cast<uint32_t>(stream);
    Virtual<uint8_t>(stream, 0x10, &flag);
    const uint8_t has = static_cast<uint8_t>(flag);
    At<uint8_t>(self, place::kHasPosition) = has != 0;
    if (has) {
        Virtual<uint8_t>(stream, 0x30, self + place::kX);
        Virtual<uint8_t>(stream, 0x30, self + place::kY);
    }
    Virtual<uint8_t>(stream, 0x24, self + place::kFirst);
    Virtual<uint8_t>(stream, 0x24, self + place::kSecond);
    void** const object = &At<void*>(self, place::kObject);
    if (void* const old = *object) {
        *object = nullptr;
        Release(old);
    }
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, void**, uint32_t)>(addr::SC4Stream_ReadReference)(
        stream, 1, object, 0);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The game's 0x0040D050: keeps the position inside the city (0 to the
// city's slots 93 and 94).
void __fastcall PlaceClamp(char* self, void*)
{
    if (!At<uint8_t>(self, place::kHasPosition))
        return;
    void* const city = Global<void*>(addr::SC4_City);
    const float width = static_cast<float>(Virtual<int32_t>(city, 0x174));
    const double height = static_cast<double>(Virtual<int32_t>(city, 0x178));
    if (At<float>(self, place::kX) < Global<float>(addr::SC4_FloatZero))
        At<uint32_t>(self, place::kX) = 0;
    if (At<float>(self, place::kY) < Global<float>(addr::SC4_FloatZero))
        At<uint32_t>(self, place::kY) = 0;
    if (At<float>(self, place::kX) > width)
        At<float>(self, place::kX) = width;
    if (height < static_cast<double>(At<float>(self, place::kY)))
        At<float>(self, place::kY) = static_cast<float>(height);
}

// The game's 0x0040D0F0: QueryInterface from the serialisable part.
uint8_t __fastcall PlaceQueryInterfaceFromSerializable(char* part, void*, uint32_t iid, void** out)
{
    return PlaceQueryInterface(part - place::kSerializable, nullptr, iid, out);
}

// The game's 0x0040D100: QueryInterface from the interface.
uint8_t __fastcall PlaceQueryInterfaceFromInterface(char* part, void*, uint32_t iid, void** out)
{
    return PlaceQueryInterface(part - place::kInterface, nullptr, iid, out);
}

// The game's 0x0040D110, on the interface: the position (two floats, copied
// as bits).
void __fastcall PlaceSetPosition(char* part, void*, uint32_t x, uint32_t y)
{
    At<uint32_t>(part, 0x8) = x;
    At<uint32_t>(part, 0xC) = y;
    At<uint8_t>(part, 0x4) = 1;
    PlaceClamp(part - place::kInterface, nullptr);
}

// The game's 0x0040D130, on the interface.
uint8_t __fastcall PlaceSetObject(char* part, void*, void* object)
{
    if (object)
        AddRef(object);
    void** const kept = &At<void*>(part, 0x18);
    void* const old = *kept;
    if (object != old) {
        if (object)
            AddRef(object);
        *kept = object;
        if (old)
            Release(old);
    }
    if (object)
        Release(object);
    return 1;
}

// The game's 0x0040D180, on the interface: a copy of the place.
uint8_t __fastcall PlaceClone(char* part, void*, void** out)
{
    char* const memory =
        static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(place::kSize));
    char* copy = nullptr;
    if (memory) {
        ConstructPlace(memory);
        copy = memory + place::kInterface;
        AddRef(copy);
    }
    if (Virtual<uint8_t>(part, 0x18)) {
        const float y = static_cast<float>(Virtual<double>(part, 0x14));
        const float x = static_cast<float>(Virtual<double>(part, 0x10));
        Virtual<void>(copy, 0xC, x, y);
    }
    Virtual<void>(copy, 0x1C, Virtual<void*>(part, 0x20));
    Virtual<void>(copy, 0x28, Virtual<uint32_t>(part, 0x24));
    Virtual<void>(copy, 0x30, Virtual<uint32_t>(part, 0x2C));
    if (out) {
        *out = copy;
        AddRef(copy);
        Release(copy);
        return 1;
    }
    Release(copy);
    return 0;
}

// The game's 0x0040D270: the class factory's maker (no reference taken).
char* __cdecl PlaceCreate()
{
    char* const memory =
        static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(place::kSize));
    if (!memory)
        return nullptr;
    ConstructPlace(memory);
    return memory;
}

// The game's 0x0040D2D0: the deleting destructor.
char* __fastcall PlaceDelete(char* self, void*, uint32_t flags)
{
    SetPlaceVtables(self);
    void** const object = &At<void*>(self, place::kObject);
    if (void* const old = *object) {
        *object = nullptr;
        Release(old);
    }
    if (void* const again = *object)
        Release(again);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::cRZBaseUnknown_Destruct)(self);
    if (static_cast<uint8_t>(flags) & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

// ---------------------------------------------------------------------------
// The advisor's small methods.

// The game's 0x0040D330: entry `index` of the table on top of a script
// thread's stack (the thread at +8 of a holder, valid when +0xC is set).
uint32_t __fastcall TableEntry(char* holder, void*, int32_t index)
{
    if (!At<uint8_t>(holder, 0xC))
        return 0;
    Virtual<void>(At<void*>(holder, 0x8), 0x7C, static_cast<double>(index));
    Virtual<void>(At<void*>(holder, 0x8), 0x98, int32_t{-2});
    const uint32_t entry = Virtual<uint32_t>(At<void*>(holder, 0x8), 0x58, int32_t{-1});
    Virtual<void>(At<void*>(holder, 0x8), 0x2C, int32_t{1});
    return entry;
}

// The game's 0x0040D380, on the serialisable part.
uint32_t __fastcall AdvisorClassId(char*, void*)
{
    return kAdvisorClass;
}

// The game's 0x0040D3B0 and 0x0040D3C0, on the interface: slot 14 of the
// string at +0x10 and of the one at +0x30, with the caller's arguments.
__declspec(naked) void StringASlot14()
{
    __asm mov eax, [ecx + 0x10]
    __asm add ecx, 0x10
    __asm jmp dword ptr [eax + 0x38]
}

__declspec(naked) void StringBSlot14()
{
    __asm mov eax, [ecx + 0x30]
    __asm add ecx, 0x30
    __asm jmp dword ptr [eax + 0x38]
}

// The game's 0x0040D3E0, on the interface: selects item `index` of one of
// the two lists, or nothing when there is no such item.
uint8_t __fastcall Select(char* part, void*, uint32_t which, int32_t index)
{
    char* const self = part - field::kInterface;
    if (index >= 0 && index < Virtual<int32_t>(part, 0x3C, which)) {
        At<int32_t>(self, field::kSelected) = index;
        At<uint8_t>(self, field::kSelectedKind) = static_cast<uint8_t>(which);
        return 1;
    }
    At<int32_t>(self, field::kSelected) = -1;
    return 0;
}

// The game's 0x0040D430.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    std::size_t part;
    if (iid == kMessageTargetIid)
        part = field::kTarget;
    else if (iid == kAdvisorIid)
        part = field::kInterface;
    else if (iid == kSerializableIid)
        part = field::kSerializable;
    else
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            self, iid, out);
    *out = self ? self + part : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x0040D4B0.
uint32_t __cdecl AdvisorTypeId()
{
    return 0xCA09F543;
}

// The game's 0x0040D7A0, on the interface: the advice whose slot 10 gives
// `id`.
void* __fastcall FindAdviceBySlot10(char* part, void*, uint32_t id)
{
    char* const self = part - field::kInterface;
    Record* record = At<Record*>(self, field::kRecords);
    if (record == At<Record*>(self, field::kRecords + 4))
        return nullptr;
    do {
        if (Virtual<uint32_t>(record->advice, 0x28) == id)
            return record->advice;
        ++record;
    } while (record != At<Record*>(self, field::kRecords + 4));
    return nullptr;
}

// The game's 0x0040D7E0, on the interface: the same by slot 13.
void* __fastcall FindAdviceBySlot13(char* part, void*, uint32_t id)
{
    char* const self = part - field::kInterface;
    Record* record = At<Record*>(self, field::kRecords);
    if (record == At<Record*>(self, field::kRecords + 4))
        return nullptr;
    do {
        if (Virtual<uint32_t>(record->advice, 0x34) == id)
            return record->advice;
        ++record;
    } while (record != At<Record*>(self, field::kRecords + 4));
    return nullptr;
}

// The game's 0x0040D820, on the interface: 0 takes the time from the
// simulator service and calls the main slot 6, 1 counts and calls slot 8.
uint8_t __fastcall OnTime(char* part, void*, int32_t what)
{
    char* const self = part - field::kInterface;
    if (what == 0) {
        void* const clock = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
        At<uint32_t>(self, field::kNow) = Virtual<uint32_t>(clock, 0x68);
        Virtual<void>(self, 0x18);
    } else if (what == 1) {
        ++At<int32_t>(self, field::kTicks);
        Virtual<void>(self, 0x20);
    }
    return 1;
}

// The game's 0x0040D870, on the interface: the size of one of the lists.
int32_t __fastcall ListCount(char* part, void*, uint8_t which)
{
    char* const self = part - field::kInterface;
    const std::size_t list = which ? field::kListA : field::kListB;
    return static_cast<int32_t>(At<char*>(self, list + 4) - At<char*>(self, list)) >> 2;
}

// The game's 0x0040D8A0, on the interface (slot 19): from the last record
// back, the first advice without flag 0x40 whose slot 27 gives `argument`
// and whose slot 8 succeeds.
uint8_t __fastcall Run(char* part, void*, uint32_t argument)
{
    char* const self = part - field::kInterface;
    int32_t left =
        static_cast<int32_t>(At<char*>(self, field::kRecords + 4) - At<char*>(self, field::kRecords)) >> 4;
    if (!left)
        return 0;
    int32_t at = left << 4;
    do {
        at -= 0x10;
        Record* const record = reinterpret_cast<Record*>(At<char*>(self, field::kRecords) + at);
        --left;
        if (!Virtual<uint8_t>(record->advice, 0x3C, int32_t{0x40}) &&
            Virtual<uint32_t>(record->advice, 0x6C) == argument && Virtual<uint8_t>(record->advice, 0x20))
            return 1;
    } while (left);
    return 0;
}

// The game's 0x0040D910: posts message 0xCA8F3DB6 with two values to the
// advisor's own target.
void __fastcall RequestUpdate(char* self, void*, uint32_t a, uint32_t b)
{
    void* const message = NewMessage();
    Virtual<void>(message, 0x14, uint32_t{0xCA8F3DB6});
    Virtual<void>(message, 0x2C, a);
    Virtual<void>(message, 0x40, b);
    void* const target = self ? self + field::kTarget : nullptr;
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x1C, message, target);
    Release(message);
}

// The game's 0x0040DA10: sends a message of `type` with the source's
// unknown and `data`, queued or at once. The game passes the source in ECX
// and the rest on the stack, which the caller removes.
void __cdecl PostFrom(void* source, uint32_t queued, uint32_t type, uint32_t data)
{
    void* unknown = nullptr;
    if (!source || Virtual<uint8_t>(source, 0x0, uint32_t{1}, &unknown)) {
        void* const message = NewMessage();
        Virtual<void>(message, 0x14, type);
        Virtual<void>(message, 0x84, unknown);
        Virtual<void>(message, 0x2C, data);
        void* const server = Global<void*>(addr::SC4_MessageServer);
        if (static_cast<uint8_t>(queued))
            Virtual<void>(server, 0x10, message, int32_t{0});
        else
            Virtual<void>(server, 0xC, message);
        Release(message);
    }
    if (unknown)
        Release(unknown);
}

__declspec(naked) void PostFromThunk()
{
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push ecx
    __asm call PostFrom
    __asm add esp, 16
    __asm ret
}

// The game's 0x0040DF20: starts a record's advice: its numbers, flags 2 on
// and 8 off, and the announcements (the second with the advice's slot 27
// value, when it has one).
void __fastcall StartRecord(char* self, void*, Record* record)
{
    record->a = 0;
    record->b = Virtual<int32_t>(record->advice, 0x44) + At<int32_t>(self, field::kNow);
    Virtual<void>(record->advice, 0x38, int32_t{2}, int32_t{1});
    Virtual<void>(record->advice, 0x38, int32_t{8}, int32_t{0});
    PostFrom(record->advice, 1, 0x8A09FA64, 0);
    if (const uint32_t value = Virtual<uint32_t>(record->advice, 0x6C))
        PostFrom(self, 1, 0x8A09FA6C, value);
}

// The game's 0x0040DF90: ends a record's advice: flags 0x40A off, the time
// it may come back (never with flag 4), and the announcement.
void __fastcall EndRecord(char* self, void*, Record* record)
{
    Virtual<void>(record->advice, 0x38, int32_t{0x40A}, int32_t{0});
    if (Virtual<uint8_t>(record->advice, 0x3C, int32_t{4}))
        record->a = 0x7FFFFFFF;
    else
        record->a = Virtual<int32_t>(record->advice, 0x40) + At<int32_t>(self, field::kNow);
    record->b = 0;
    Virtual<void>(record->advice, 0x60, int32_t{0});
    PostFrom(record->advice, 0, 0x8A09FA65, 0);
}

// The game's 0x0040E2E0, on the serialisable part. Each record's flag is
// pushed in the low byte of a slot that holds `this`.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, int32_t{3});
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kSaved14));
    Virtual<uint8_t>(stream, 0x3C, self + field::kStringA);
    Virtual<uint8_t>(stream, 0x3C, self + field::kStringB);
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kTicks));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kNow));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kSaved60));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kSaved64));
    const int32_t count =
        static_cast<int32_t>(At<char*>(self, field::kRecords + 4) - At<char*>(self, field::kRecords)) >> 4;
    Virtual<uint8_t>(stream, 0x20, count);
    for (int32_t i = 0; i < count; ++i) {
        Record* const record = At<Record*>(self, field::kRecords) + i;
        WriteReference(stream, record->advice);
        Virtual<uint8_t>(stream, 0x20, record->b);
        Virtual<uint8_t>(stream, 0x20, record->c);
        Virtual<uint8_t>(stream, 0x20, record->a);
        const uint8_t flag = Virtual<uint8_t>(record->advice, 0x3C, int32_t{2}) != 0;
        Virtual<uint8_t>(stream, 0x10, (reinterpret_cast<uint32_t>(part) & 0xFFFFFF00u) | flag);
        Virtual<uint8_t>(stream, 0x20, Virtual<uint32_t>(record->advice, 0x68));
    }
    char* const list = At<char*>(self, field::kPairs);
    uint32_t pairs = 0;
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0))
        ++pairs;
    Virtual<uint8_t>(stream, 0x20, pairs);
    for (char* node = At<char*>(At<char*>(self, field::kPairs), 0x0); node != At<char*>(self, field::kPairs);
         node = At<char*>(node, 0x0)) {
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(node, 0x8));
        WriteReference(stream, At<void*>(node, 0xC));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// The records' vector helpers.

void ConstructRecord(Record* to, const Record* from)
{
    to->advice = from->advice;
    if (to->advice)
        AddRef(to->advice);
    to->a = from->a;
    to->b = from->b;
    to->c = from->c;
}

// The game's 0x0040DDF0: copies records into raw memory.
Record* __cdecl CopyRecordsNew(Record* first, Record* last, Record* to)
{
    for (; first != last; ++first, ++to) {
        if (to)
            ConstructRecord(to, first);
    }
    return to;
}

// The game's 0x0040DE40: fills raw memory with `count` copies of a record.
Record* __cdecl FillRecordsNew(Record* to, uint32_t count, const Record* value)
{
    if (count == 0)
        return to;
    do {
        if (to)
            ConstructRecord(to, value);
        ++to;
    } while (--count);
    return to;
}

// The game's 0x0040E260: assigns records over records.
Record* __cdecl CopyRecords(Record* first, Record* last, Record* to)
{
    int32_t left = static_cast<int32_t>(reinterpret_cast<char*>(last) - reinterpret_cast<char*>(first)) >> 4;
    if (left <= 0)
        return to;
    do {
        if (first != to) {
            void* const advice = first->advice;
            void* const old = to->advice;
            if (advice != old) {
                if (advice)
                    AddRef(advice);
                to->advice = advice;
                if (old)
                    Release(old);
            }
        }
        to->a = first->a;
        to->b = first->b;
        to->c = first->c;
        ++first;
        ++to;
    } while (--left);
    return to;
}

// ---------------------------------------------------------------------------
// The tree.

// The game's 0x0040D540.
TreeNode* __cdecl RotateLeft(TreeNode* x, TreeNode** root)
{
    TreeNode* const y = x->right;
    x->right = y->left;
    if (y->left)
        y->left->parent = x;
    y->parent = x->parent;
    if (x == *root)
        *root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;
    return y;
}

// The game's 0x0040D590.
TreeNode* __cdecl RotateRight(TreeNode* x, TreeNode** root)
{
    TreeNode* const y = x->left;
    x->left = y->right;
    if (y->right)
        y->right->parent = x;
    y->parent = x->parent;
    if (x == *root)
        *root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;
    y->right = x;
    x->parent = y;
    return y;
}

// The game's 0x0040D5E0.
void __cdecl RebalanceForInsert(TreeNode* x, TreeNode** root)
{
    x->color = 0;
    while (x != *root && x->parent->color == 0) {
        TreeNode* const parent = x->parent;
        TreeNode* const grand = parent->parent;
        if (parent == grand->left) {
            TreeNode* const uncle = grand->right;
            if (uncle && uncle->color == 0) {
                x->parent->color = 1;
                uncle->color = 1;
                x->parent->parent->color = 0;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    RotateLeft(x, root);
                }
                x->parent->color = 1;
                x->parent->parent->color = 0;
                RotateRight(x->parent->parent, root);
            }
        } else {
            TreeNode* const uncle = grand->left;
            if (uncle && uncle->color == 0) {
                x->parent->color = 1;
                uncle->color = 1;
                x->parent->parent->color = 0;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    RotateRight(x, root);
                }
                x->parent->color = 1;
                x->parent->parent->color = 0;
                RotateLeft(x->parent->parent, root);
            }
        }
    }
    (*root)->color = 1;
}

// The game's 0x0040DAF0: unlinks `z` and returns the node to free.
TreeNode* __cdecl RebalanceForErase(TreeNode* z, TreeNode** root, TreeNode** leftmost, TreeNode** rightmost)
{
    TreeNode* y = z;
    TreeNode* x;
    TreeNode* xParent;
    if (!z->left) {
        x = z->right;
    } else if (!z->right) {
        x = z->left;
    } else {
        y = z->right;
        while (y->left)
            y = y->left;
        x = y->right;
    }
    if (y != z) {
        z->left->parent = y;
        y->left = z->left;
        if (y != z->right) {
            xParent = y->parent;
            if (x)
                x->parent = xParent;
            y->parent->left = x;
            y->right = z->right;
            z->right->parent = y;
        } else {
            xParent = y;
        }
        if (*root == z)
            *root = y;
        else if (z->parent->left == z)
            z->parent->left = y;
        else
            z->parent->right = y;
        y->parent = z->parent;
        const uint8_t color = y->color;
        y->color = z->color;
        z->color = color;
        y = z;
    } else {
        xParent = y->parent;
        if (x)
            x->parent = xParent;
        if (*root == z)
            *root = x;
        else if (z->parent->left == z)
            z->parent->left = x;
        else
            z->parent->right = x;
        if (*leftmost == z) {
            if (!z->right) {
                *leftmost = z->parent;
            } else {
                TreeNode* least = x;
                while (least->left)
                    least = least->left;
                *leftmost = least;
            }
        }
        if (*rightmost == z) {
            if (!z->left) {
                *rightmost = z->parent;
            } else {
                TreeNode* most = x;
                while (most->right)
                    most = most->right;
                *rightmost = most;
            }
        }
    }
    if (y->color == 0)
        return y;
    while (x != *root && (!x || x->color == 1)) {
        if (x == xParent->left) {
            TreeNode* w = xParent->right;
            if (w->color == 0) {
                w->color = 1;
                xParent->color = 0;
                RotateLeft(xParent, root);
                w = xParent->right;
            }
            if ((!w->left || w->left->color == 1) && (!w->right || w->right->color == 1)) {
                w->color = 0;
                x = xParent;
                xParent = xParent->parent;
                continue;
            }
            if (!w->right || w->right->color == 1) {
                if (w->left)
                    w->left->color = 1;
                w->color = 0;
                RotateRight(w, root);
                w = xParent->right;
            }
            w->color = xParent->color;
            xParent->color = 1;
            if (w->right)
                w->right->color = 1;
            RotateLeft(xParent, root);
            break;
        }
        TreeNode* w = xParent->left;
        if (w->color == 0) {
            w->color = 1;
            xParent->color = 0;
            RotateRight(xParent, root);
            w = xParent->left;
        }
        if ((!w->right || w->right->color == 1) && (!w->left || w->left->color == 1)) {
            w->color = 0;
            x = xParent;
            xParent = xParent->parent;
            continue;
        }
        if (!w->left || w->left->color == 1) {
            if (w->right)
                w->right->color = 1;
            w->color = 0;
            RotateLeft(w, root);
            w = xParent->left;
        }
        w->color = xParent->color;
        xParent->color = 1;
        if (w->left)
            w->left->color = 1;
        RotateRight(xParent, root);
        break;
    }
    if (x)
        x->color = 1;
    return y;
}

// The order of the tree: the advice's slot 7 value, then the address.
uint8_t Less(void** a, void** b)
{
    const int32_t x = Virtual<int32_t>(*a, 0x1C);
    const int32_t y = Virtual<int32_t>(*b, 0x1C);
    if (x != y)
        return x < y;
    return reinterpret_cast<std::uintptr_t>(*a) < reinterpret_cast<std::uintptr_t>(*b);
}

// The game's 0x0040D990, on the tree's comparer (which it does not use).
uint8_t __fastcall LessAdvice(void*, void*, void** a, void** b)
{
    return Less(a, b);
}

// The game's 0x0040DE90: the node of `key`, or the header.
TreeNode* __fastcall TreeFind(Tree* tree, void*, void** key)
{
    TreeNode* y = tree->header;
    TreeNode* x = y->parent;
    while (x) {
        if (!Less(&x->value, key)) {
            y = x;
            x = x->left;
        } else {
            x = x->right;
        }
    }
    if (y == tree->header)
        return tree->header;
    if (Less(key, &y->value))
        return tree->header;
    return y;
}

// The game's 0x0040E000: the first node not before `key`, or the header.
TreeNode* __fastcall TreeLowerBound(Tree* tree, void*, void** key)
{
    TreeNode* y = tree->header;
    TreeNode* x = y->parent;
    while (x) {
        if (!Less(&x->value, key)) {
            y = x;
            x = x->left;
        } else {
            x = x->right;
        }
    }
    return y;
}

TreeNode* NewNode(void** value)
{
    TreeNode* const node =
        static_cast<TreeNode*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0x18));
    node->value = value[0];
    if (node->value)
        AddRef(node->value);
    node->extra = reinterpret_cast<uint32_t>(value[1]);
    return node;
}

// The game's 0x0040E090: a new node of `value` (an advice and a number)
// under `y`, on the left when `x` is set or the value comes first, on the
// right when `w` is set.
TreeNode** __fastcall TreeInsertAt(Tree* tree, void*, TreeNode** out, TreeNode* x, TreeNode* y, void** value,
                                   TreeNode* w)
{
    TreeNode* node;
    if (y == tree->header || (!w && (x || LessAdvice(&tree->less, nullptr, value, &y->value)))) {
        node = NewNode(value);
        y->left = node;
        TreeNode* const header = tree->header;
        if (y == header) {
            header->parent = node;
            tree->header->right = node;
        } else if (y == header->left) {
            header->left = node;
        }
    } else {
        node = NewNode(value);
        y->right = node;
        TreeNode* const header = tree->header;
        if (y == header->right)
            header->right = node;
    }
    node->left = nullptr;
    node->right = nullptr;
    node->parent = y;
    RebalanceForInsert(node, &tree->header->parent);
    ++tree->count;
    *out = node;
    return out;
}

void Decrement(TreeNode** iterator)
{
    reinterpret_cast<void(__thiscall*)(TreeNode**)>(addr::SC4TreeIterator_Decrement)(iterator);
}

void Increment(TreeNode** iterator)
{
    reinterpret_cast<void(__thiscall*)(TreeNode**)>(addr::SC4MapIterator_Next)(iterator);
}

// The game's 0x0040E180: inserts `value` unless its key is there already.
InsertResult* __fastcall TreeInsertUnique(Tree* tree, void*, InsertResult* out, void** value)
{
    TreeNode* y = tree->header;
    TreeNode* x = y->parent;
    uint8_t less = 1;
    while (x) {
        y = x;
        less = Less(value, &x->value);
        x = less ? x->left : x->right;
    }
    TreeNode* j = y;
    bool insert = false;
    if (less) {
        if (y == tree->header->left)
            insert = true;
        else
            Decrement(&j);
    }
    if (insert || Less(&j->value, value)) {
        TreeInsertAt(tree, nullptr, &j, x, y, value, nullptr);
        out->node = j;
        out->inserted = 1;
        return out;
    }
    out->node = j;
    out->inserted = 0;
    return out;
}

// The game's 0x0040E440: unlinks a node of the advice tree, releases its
// advice and frees it.
void __fastcall TreeErase(Tree* tree, void*, TreeNode* node)
{
    TreeNode* const header = tree->header;
    TreeNode* const freed = RebalanceForErase(node, &header->parent, &header->left, &header->right);
    if (void* const advice = freed->value)
        Release(advice);
    if (freed)
        Free(freed);
    --tree->count;
}

// The game's 0x0040E4D0: inserts `value` unless its key is there already,
// trying next to `hint` first.
TreeNode** __fastcall TreeInsertHint(Tree* tree, void*, TreeNode** out, TreeNode* hint, void** value)
{
    void* const comparer = &tree->less;
    InsertResult result;
    TreeNode* const header = tree->header;
    if (hint == header->left) {
        if (tree->count == 0) {
            *out = TreeInsertUnique(tree, nullptr, &result, value)->node;
            return out;
        }
        if (Less(value, &hint->value))
            return TreeInsertAt(tree, nullptr, out, hint, hint, value, nullptr);
        if (!LessAdvice(comparer, nullptr, &hint->value, value)) {
            *out = hint;
            return out;
        }
        TreeNode* after = hint;
        Increment(&after);
        if (after == tree->header)
            return TreeInsertAt(tree, nullptr, out, nullptr, hint, value, hint);
        if (LessAdvice(comparer, nullptr, value, &after->value)) {
            if (!hint->right)
                return TreeInsertAt(tree, nullptr, out, nullptr, hint, value, hint);
            return TreeInsertAt(tree, nullptr, out, after, after, value, nullptr);
        }
        *out = TreeInsertUnique(tree, nullptr, &result, value)->node;
        return out;
    }
    if (hint == header) {
        if (!Less(&header->right->value, value)) {
            *out = TreeInsertUnique(tree, nullptr, &result, value)->node;
            return out;
        }
        return TreeInsertAt(tree, nullptr, out, nullptr, tree->header->right, value, hint);
    }
    TreeNode* before = hint;
    Decrement(&before);
    const uint8_t valueFirst = Less(value, &hint->value);
    if (valueFirst && LessAdvice(comparer, nullptr, &before->value, value)) {
        if (!before->right)
            return TreeInsertAt(tree, nullptr, out, nullptr, before, value, before);
        return TreeInsertAt(tree, nullptr, out, hint, hint, value, nullptr);
    }
    TreeNode* after = hint;
    Increment(&after);
    uint8_t first = valueFirst;
    uint8_t second = first == 0;
    if (!first) {
        const uint8_t hintFirst = LessAdvice(comparer, nullptr, &hint->value, value);
        if (!hintFirst) {
            *out = hint;
            return out;
        }
        if (after == tree->header || LessAdvice(comparer, nullptr, value, &after->value)) {
            if (!hint->right)
                return TreeInsertAt(tree, nullptr, out, nullptr, hint, value, hint);
            return TreeInsertAt(tree, nullptr, out, after, after, value, nullptr);
        }
        first = valueFirst;
        second = hintFirst;
    }
    if (first == second) {
        *out = hint;
        return out;
    }
    *out = TreeInsertUnique(tree, nullptr, &result, value)->node;
    return out;
}

bool InstallBase1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[56] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Advisor_F40CE00, reinterpret_cast<void*>(&PlaceClassId)},
        {addr::cSC4Advisor_F40CE10, reinterpret_cast<void*>(&PlaceGetX)},
        {addr::cSC4Advisor_F40CE30, reinterpret_cast<void*>(&PlaceGetY)},
        {addr::cSC4Advisor_F40CE80, reinterpret_cast<void*>(&PlaceSetSecond)},
        {addr::cSC4Advisor_F40CE90, reinterpret_cast<void*>(&PlaceQueryInterface)},
        {addr::cSC4Advisor_F40CF20, reinterpret_cast<void*>(&PlaceWrite)},
        {addr::cSC4Advisor_F40CFA0, reinterpret_cast<void*>(&PlaceRead)},
        {addr::cSC4Advisor_F40D050, reinterpret_cast<void*>(&PlaceClamp)},
        {addr::cSC4Advisor_F40D0F0, reinterpret_cast<void*>(&PlaceQueryInterfaceFromSerializable)},
        {addr::cSC4Advisor_F40D100, reinterpret_cast<void*>(&PlaceQueryInterfaceFromInterface)},
        {addr::cSC4Advisor_F40D110, reinterpret_cast<void*>(&PlaceSetPosition)},
        {addr::cSC4Advisor_F40D130, reinterpret_cast<void*>(&PlaceSetObject)},
        {addr::cSC4Advisor_F40D180, reinterpret_cast<void*>(&PlaceClone)},
        {addr::cSC4Advisor_F40D270, reinterpret_cast<void*>(&PlaceCreate)},
        {addr::cSC4Advisor_F40D2D0, reinterpret_cast<void*>(&PlaceDelete)},
        {addr::cSC4Advisor_F40D330, reinterpret_cast<void*>(&TableEntry)},
        {addr::cSC4Advisor_F40D380, reinterpret_cast<void*>(&AdvisorClassId)},
        {addr::cSC4Advisor_F40D3B0, reinterpret_cast<void*>(&StringASlot14)},
        {addr::cSC4Advisor_F40D3C0, reinterpret_cast<void*>(&StringBSlot14)},
        {addr::cSC4Advisor_F40D3E0, reinterpret_cast<void*>(&Select)},
        {addr::SC4AgentBase_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4Advisor_F40D4B0, reinterpret_cast<void*>(&AdvisorTypeId)},
        {addr::cSC4Advisor_F40D7A0, reinterpret_cast<void*>(&FindAdviceBySlot10)},
        {addr::cSC4Advisor_F40D7E0, reinterpret_cast<void*>(&FindAdviceBySlot13)},
        {addr::cSC4Advisor_F40D820, reinterpret_cast<void*>(&OnTime)},
        {addr::cSC4Advisor_F40D870, reinterpret_cast<void*>(&ListCount)},
        {addr::SC4AdvisorBase_Run, reinterpret_cast<void*>(&Run)},
        {addr::cSC4Advisor_RequestUpdate, reinterpret_cast<void*>(&RequestUpdate)},
        {addr::cSC4Advisor_F40D990, reinterpret_cast<void*>(&LessAdvice)},
        {addr::cSC4Advisor_F40DA10, reinterpret_cast<void*>(&PostFromThunk)},
        {addr::cSC4Advisor_F40DDF0, reinterpret_cast<void*>(&CopyRecordsNew)},
        {addr::cSC4Advisor_F40DE40, reinterpret_cast<void*>(&FillRecordsNew)},
        {addr::cSC4Advisor_F40DE90, reinterpret_cast<void*>(&TreeFind)},
        {addr::cSC4Advisor_F40DF20, reinterpret_cast<void*>(&StartRecord)},
        {addr::cSC4Advisor_F40DF90, reinterpret_cast<void*>(&EndRecord)},
        {addr::cSC4Advisor_F40E000, reinterpret_cast<void*>(&TreeLowerBound)},
        {addr::cSC4Advisor_F40E090, reinterpret_cast<void*>(&TreeInsertAt)},
        {addr::cSC4Advisor_F40E180, reinterpret_cast<void*>(&TreeInsertUnique)},
        {addr::cSC4Advisor_F40E260, reinterpret_cast<void*>(&CopyRecords)},
        {addr::SC4AgentBase_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4Advisor_F40E440, reinterpret_cast<void*>(&TreeErase)},
        {addr::cSC4Advisor_F40E4D0, reinterpret_cast<void*>(&TreeInsertHint)},
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

const bool g_base1Registered = registry::Register({
    "scripts.advisors.base1",
    "scripts/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4AdvisorBase_Run,
    "Our advisor base class, part one: the advice place, the small methods, the tree (docs/lua.md)",
    false,
    InstallBase1,
});

} // namespace
} // namespace opensc4::advisor_base
