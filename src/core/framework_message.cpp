// The standard message (cGZMessage: vtable 0x00ACEC00, what the message
// servers and the message queue pass around) on the game's own objects
// (docs/framework.md). Replacements, each off by default:
//
//   framework.message.data       SetType (0x009132DB), the four data
//                                values' setters (0x00913322, 0x00913330,
//                                0x0091333E, 0x0091334C) and "has" answers
//                                (0x009132F3, 0x009132FE, 0x0091330A,
//                                0x00913316)
//   framework.message.objects    the string, the unknown and the extra
//                                object: their setters (0x00913452,
//                                0x0091346B, 0x009134A1), "has" answers
//                                (0x0091335A, 0x00913366, 0x0091336E) and
//                                GetString and GetExtra (0x00913438,
//                                0x00913484)
//   framework.message.interface  QueryInterface (0x00913292), Release
//                                (0x009132C1), Create (0x009134FF), the two
//                                comparisons by type (0x0091337A,
//                                0x00913394) and the deleting destructor
//                                (0x009134BA)
//
// The object (0x2C bytes, from the message pool): +0x00 vtable, +0x04
// reference count, +0x08 flags (bit 0 a type, bits 1 to 4 the data values,
// 5 the extra object, 6 the string, 7 the unknown), +0x0C type, +0x10 to
// +0x1C the four data values, +0x20 the string, +0x24 the unknown, +0x28 the
// extra object (counted references).
//
// The message's subclasses share these functions through their own vtables.
// The constructor, the destructor and the pool are in
// framework_messagepool.cpp. Still the game's: the getters
// of the type, the data values and the unknown (small functions shared with
// unrelated classes).
//
// A few of these have direct callers in game code, which may keep ECX and
// EDX across the call: every detour is entered through a thunk that keeps
// both.
#include "core/address_check.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kRefs = 0x04;
constexpr std::size_t kFlags = 0x08;
constexpr std::size_t kType = 0x0C;
constexpr std::size_t kData = 0x10;
constexpr std::size_t kString = 0x20;
constexpr std::size_t kUnknown = 0x24;
constexpr std::size_t kExtra = 0x28;

constexpr uint8_t kHasType = 0x01;
constexpr uint8_t kHasExtra = 0x20;
constexpr uint8_t kHasString = 0x40;
constexpr uint8_t kHasUnknown = 0x80;

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_cIGZMessage2Standard = 0x65297976;
constexpr uint32_t kIID_cIGZMessage2 = 0xA52294B4;

void SetFlag(char* self, uint8_t flag, bool on)
{
    if (on)
        At<uint8_t>(self, kFlags) |= flag;
    else
        At<uint8_t>(self, kFlags) &= static_cast<uint8_t>(~flag);
}

uint32_t Flag(char* self, unsigned bit)
{
    return (At<uint8_t>(self, kFlags) >> bit) & 1u;
}

// The game's 0x009132DB: a type of 0 is no type.
uint32_t __fastcall SetType(char* self, void*, uint32_t type)
{
    At<uint32_t>(self, kType) = type;
    SetFlag(self, kHasType, type != 0);
    return type;
}

// The game's 0x00913322, 0x00913330, 0x0091333E, 0x0091334C: the value and
// its flag, whatever the value.
uint32_t SetData(char* self, unsigned index, uint32_t value)
{
    At<uint8_t>(self, kFlags) |= static_cast<uint8_t>(2u << index);
    At<uint32_t>(self, kData + 4 * index) = value;
    return value;
}

uint32_t __fastcall SetData1(char* self, void*, uint32_t value)
{
    return SetData(self, 0, value);
}

uint32_t __fastcall SetData2(char* self, void*, uint32_t value)
{
    return SetData(self, 1, value);
}

uint32_t __fastcall SetData3(char* self, void*, uint32_t value)
{
    return SetData(self, 2, value);
}

uint32_t __fastcall SetData4(char* self, void*, uint32_t value)
{
    return SetData(self, 3, value);
}

// The game's 0x009132F3, 0x009132FE, 0x0091330A, 0x00913316.
uint32_t __fastcall GetHasData1(char* self, void*)
{
    return Flag(self, 1);
}

uint32_t __fastcall GetHasData2(char* self, void*)
{
    return Flag(self, 2);
}

uint32_t __fastcall GetHasData3(char* self, void*)
{
    return Flag(self, 3);
}

uint32_t __fastcall GetHasData4(char* self, void*)
{
    return Flag(self, 4);
}

// The game's 0x0091335A, 0x00913366, 0x0091336E.
uint32_t __fastcall GetHasString(char* self, void*)
{
    return Flag(self, 6);
}

uint32_t __fastcall GetHasIGZUnknown(char* self, void*)
{
    return Flag(self, 7);
}

uint32_t __fastcall GetHasExtra(char* self, void*)
{
    return Flag(self, 5);
}

// A counted reference of the message takes another object; the result is
// the reference's address, as the game's helper leaves it.
void* SetObject(char* self, std::size_t offset, uint8_t flag, void* object)
{
    SetFlag(self, flag, object != nullptr);
    void*& held = At<void*>(self, offset);
    if (held != object) {
        void* const before = held;
        AddRef(object);
        held = object;
        Release(before);
    }
    return &held;
}

// The game's 0x00913452, 0x0091346B, 0x009134A1.
void* __fastcall SetString(char* self, void*, void* string)
{
    return SetObject(self, kString, kHasString, string);
}

void* __fastcall SetIGZUnknown(char* self, void*, void* unknown)
{
    return SetObject(self, kUnknown, kHasUnknown, unknown);
}

void* __fastcall SetExtra(char* self, void*, void* extra)
{
    return SetObject(self, kExtra, kHasExtra, extra);
}

// The game's 0x00913438: when the message says it has a string (slot 28),
// the string asked for the interface.
bool __fastcall GetString(char* self, void*, uint32_t interfaceId, void** out)
{
    if (!Virtual<uint8_t>(self, 0x70))
        return false;
    return Virtual<bool>(At<void*>(self, kString), 0x0, interfaceId, out);
}

// The game's 0x00913484: the same for the extra object (slot 34).
bool __fastcall GetExtra(char* self, void*, uint32_t interfaceId, void** out)
{
    if (!Virtual<uint8_t>(self, 0x88))
        return false;
    return Virtual<bool>(At<void*>(self, kExtra), 0x0, interfaceId, out);
}

// The game's 0x00913292.
bool __fastcall QueryInterface(char* self, void*, uint32_t interfaceId, void** out)
{
    if (interfaceId != kIID_cIGZUnknown && interfaceId != kIID_cIGZMessage2Standard &&
        interfaceId != kIID_cIGZMessage2)
        return false;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return true;
}

// The game's 0x009132C1: the last reference deletes the message (slot 37
// with flag 1).
uint32_t __fastcall MessageRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, kRefs);
    if (count > 1) {
        At<uint32_t>(self, kRefs) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, 0x94, uint32_t{1});
    return 0;
}

// The game's 0x009134FF: a new standard message asked for the interface;
// it gets this message's type (slot 4) through its SetType (slot 5), and
// nothing else of this message. Without the interface it is deleted again.
bool __fastcall Create(char* self, void*, uint32_t interfaceId, void** out)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::RZ_operator_new)(0x2C);
    void* const created =
        memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (!Virtual<bool>(created, 0x0, interfaceId, out)) {
        Virtual<void*>(created, 0x94, uint32_t{1});
        return false;
    }
    Virtual<uint32_t>(created, 0x14, Virtual<uint32_t>(self, 0x10));
    return true;
}

// The game's 0x0091337A and 0x00913394: this type against the other
// message's (its slot 4).
uint32_t __fastcall IsSameType(char* self, void*, void* other)
{
    return At<uint32_t>(self, kType) == Virtual<uint32_t>(other, 0x10) ? 1 : 0;
}

uint32_t __fastcall IsLowerType(char* self, void*, void* other)
{
    return At<uint32_t>(self, kType) < Virtual<uint32_t>(other, 0x10) ? 1 : 0;
}

// The game's 0x009134BA.
char* __fastcall DeletingDestructor(char* self, void*, uint8_t flags)
{
    reinterpret_cast<void(__thiscall*)(char*)>(addr::cGZMessage_Destruct)(self);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::RZ_operator_delete)(self);
    return self;
}

OPENSC4_THUNK(SetTypeThunk, SetType, 4)
OPENSC4_THUNK(SetData1Thunk, SetData1, 4)
OPENSC4_THUNK(SetData2Thunk, SetData2, 4)
OPENSC4_THUNK(SetData3Thunk, SetData3, 4)
OPENSC4_THUNK(SetData4Thunk, SetData4, 4)
OPENSC4_THUNK(GetHasData1Thunk, GetHasData1, 0)
OPENSC4_THUNK(GetHasData2Thunk, GetHasData2, 0)
OPENSC4_THUNK(GetHasData3Thunk, GetHasData3, 0)
OPENSC4_THUNK(GetHasData4Thunk, GetHasData4, 0)
OPENSC4_THUNK(GetHasStringThunk, GetHasString, 0)
OPENSC4_THUNK(GetHasIGZUnknownThunk, GetHasIGZUnknown, 0)
OPENSC4_THUNK(GetHasExtraThunk, GetHasExtra, 0)
OPENSC4_THUNK(SetStringThunk, SetString, 4)
OPENSC4_THUNK(SetIGZUnknownThunk, SetIGZUnknown, 4)
OPENSC4_THUNK(SetExtraThunk, SetExtra, 4)
OPENSC4_THUNK(GetStringThunk, GetString, 8)
OPENSC4_THUNK(GetExtraThunk, GetExtra, 8)
OPENSC4_THUNK(QueryInterfaceThunk, QueryInterface, 8)
OPENSC4_THUNK(MessageReleaseThunk, MessageRelease, 0)
OPENSC4_THUNK(CreateThunk, Create, 8)
OPENSC4_THUNK(IsSameTypeThunk, IsSameType, 4)
OPENSC4_THUNK(IsLowerTypeThunk, IsLowerType, 4)
OPENSC4_THUNK(DeletingDestructorThunk, DeletingDestructor, 4)

struct Detour {
    std::uintptr_t address;
    void* ours;
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detour.ours, &original))
            return false;
    }
    return true;
}

bool InstallData(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessage_SetType, reinterpret_cast<void*>(&SetTypeThunk)},
        {addr::cGZMessage_SetData1, reinterpret_cast<void*>(&SetData1Thunk)},
        {addr::cGZMessage_SetData2, reinterpret_cast<void*>(&SetData2Thunk)},
        {addr::cGZMessage_SetData3, reinterpret_cast<void*>(&SetData3Thunk)},
        {addr::cGZMessage_SetData4, reinterpret_cast<void*>(&SetData4Thunk)},
        {addr::cGZMessage_GetHasData1, reinterpret_cast<void*>(&GetHasData1Thunk)},
        {addr::cGZMessage_GetHasData2, reinterpret_cast<void*>(&GetHasData2Thunk)},
        {addr::cGZMessage_GetHasData3, reinterpret_cast<void*>(&GetHasData3Thunk)},
        {addr::cGZMessage_GetHasData4, reinterpret_cast<void*>(&GetHasData4Thunk)},
    };
    return InstallAll(detours);
}

bool InstallObjects(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessage_GetHasString, reinterpret_cast<void*>(&GetHasStringThunk)},
        {addr::cGZMessage_GetString, reinterpret_cast<void*>(&GetStringThunk)},
        {addr::cGZMessage_SetString, reinterpret_cast<void*>(&SetStringThunk)},
        {addr::cGZMessage_GetHasIGZUnknown, reinterpret_cast<void*>(&GetHasIGZUnknownThunk)},
        {addr::cGZMessage_SetIGZUnknown, reinterpret_cast<void*>(&SetIGZUnknownThunk)},
        {addr::cGZMessage_GetHasExtra, reinterpret_cast<void*>(&GetHasExtraThunk)},
        {addr::cGZMessage_GetExtra, reinterpret_cast<void*>(&GetExtraThunk)},
        {addr::cGZMessage_SetExtra, reinterpret_cast<void*>(&SetExtraThunk)},
    };
    return InstallAll(detours);
}

bool InstallInterface(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessage_QueryInterface, reinterpret_cast<void*>(&QueryInterfaceThunk)},
        {addr::cGZMessage_Release, reinterpret_cast<void*>(&MessageReleaseThunk)},
        {addr::cGZMessage_Create, reinterpret_cast<void*>(&CreateThunk)},
        {addr::cGZMessage_IsSameType, reinterpret_cast<void*>(&IsSameTypeThunk)},
        {addr::cGZMessage_IsLowerType, reinterpret_cast<void*>(&IsLowerTypeThunk)},
        {addr::cGZMessage_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructorThunk)},
    };
    return InstallAll(detours);
}

const bool g_registeredData = registry::Register({
    "framework.message.data",
    "framework/message",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessage_SetType,
    "Our standard message: the type and the four data values (docs/framework.md)",
    false,
    InstallData,
});

const bool g_registeredObjects = registry::Register({
    "framework.message.objects",
    "framework/message",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessage_SetString,
    "Our standard message: its string, unknown and extra object (docs/framework.md)",
    false,
    InstallObjects,
});

const bool g_registeredInterface = registry::Register({
    "framework.message.interface",
    "framework/message",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessage_QueryInterface,
    "Our standard message: QueryInterface, Release, Create, the comparisons by type and the deleting "
    "destructor (docs/framework.md)",
    false,
    InstallInterface,
});

} // namespace
} // namespace opensc4::framework
