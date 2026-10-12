// The framework's string class (cRZBaseString: vtable 0x00A80810, what the
// game's cIGZString objects are) on the game's own objects
// (docs/framework.md). Replacements, each off by default:
//
//   framework.string.modify     FromChar (0x0090F50F, and 0x0090F546 with a
//                               length), operator= (0x0090E230), Copy
//                               (0x0090E20A), Resize (0x0090F042), Append
//                               (0x0090F06C, 0x0090F053), Insert (0x0090F0B0,
//                               0x0090F093), Replace (0x0090FC30,
//                               0x0090FC13), Erase (0x0090E4D8)
//   framework.string.compare    IsEqual (0x0090EC57, 0x0090ED5D), CompareTo
//                               (0x0090EE61, 0x0090EF5B), Find (0x0090F16C,
//                               0x0090F0DB), RFind (0x0090F28C, 0x0090F1FB)
//   framework.string.interface  QueryInterface (0x0090E1D1), Release
//                               (0x0090E1F3)
//
// The object (0x14 bytes): +0x00 vtable, +0x04 the string body (STLport's
// basic_string: first character, one past the last, end of the block),
// +0x10 reference count.
//
// The class's methods are the layer above the body: which body operation
// with which arguments, and the comparisons without regard to case, which
// lower copies of both sides first. The body's operations (assign, resize,
// append, insert, replace, erase, find), the lowering (it goes through the
// language's wide characters for text that is not ASCII), the three
// constructors used for the copies, the C runtime's strlen, strncmp and
// memcmp and the destructor are still the game's; Sprintf (0x0090F574) is in
// framework_stringformat.cpp.
//
// Game code calls many of these directly, and may keep ECX and EDX across
// the call: every detour is entered through a thunk that keeps both.
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

constexpr std::size_t kBody = 0x04;
constexpr std::size_t kBegin = 0x04;
constexpr std::size_t kEnd = 0x08;
constexpr std::size_t kRefs = 0x10;

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_cIGZString = 0x089B7DC8;

const char* Begin(char* self)
{
    return At<const char*>(self, kBegin);
}

uint32_t Size(char* self)
{
    return static_cast<uint32_t>(At<const char*>(self, kEnd) - At<const char*>(self, kBegin));
}

uint32_t StrLen(const char* text)
{
    return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(addr::CRT_strlen)(text);
}

int32_t StrNCmp(const char* a, const char* b, uint32_t count)
{
    return reinterpret_cast<int32_t(__cdecl*)(const char*, const char*, uint32_t)>(addr::CRT_strncmp)(a, b, count);
}

int32_t MemCmp(const char* a, const char* b, uint32_t count)
{
    return reinterpret_cast<int32_t(__cdecl*)(const char*, const char*, uint32_t)>(addr::CRT_memcmp)(a, b, count);
}

// Another string's text and length through its interface (slots 5/6 and 7).
const char* ToChar(void* string)
{
    return Virtual<const char*>(string, 0x14);
}

const char* Data(void* string)
{
    return Virtual<const char*>(string, 0x18);
}

uint32_t Length(void* string)
{
    return Virtual<uint32_t>(string, 0x1C);
}

// A lowered copy of a string, made with the game's constructors and its
// lowering, on our stack.
struct Lowered {
    char object[0x14];

    char* Self() { return object; }
    const char* Begin() { return framework::Begin(object); }
    uint32_t Size() { return framework::Size(object); }

    // All of a game string.
    explicit Lowered(char* source)
    {
        using Fn = void*(__thiscall*)(char*, char*, uint32_t, uint32_t);
        reinterpret_cast<Fn>(addr::SC4String_Substring)(object, source, 0, 0xFFFFFFFFu);
        Lower();
    }
    Lowered(const char* text, uint32_t length)
    {
        using Fn = void*(__thiscall*)(char*, const char*, uint32_t);
        reinterpret_cast<Fn>(addr::SC4String_FromBytes)(object, text, length);
        Lower();
    }
    // Any cIGZString.
    struct FromInterface {};
    Lowered(FromInterface, void* string)
    {
        using Fn = void*(__thiscall*)(char*, void*);
        reinterpret_cast<Fn>(addr::SC4String_FromIGZString)(object, string);
        Lower();
    }
    ~Lowered()
    {
        if (void* const block = At<void*>(object, kBegin))
            Free(block);
    }
    Lowered(const Lowered&) = delete;
    Lowered& operator=(const Lowered&) = delete;

private:
    void Lower() { reinterpret_cast<void(__thiscall*)(char*)>(addr::cRZBaseString_MakeLower)(object); }
};

// The body's operations.
void BodyAssign(char* self, const char* first, const char* last)
{
    using Fn = void*(__thiscall*)(char*, const char*, const char*);
    reinterpret_cast<Fn>(addr::SC4StringBody_AssignChars)(self + kBody, first, last);
}

int32_t BodyFind(char* self, std::uintptr_t function, const char* text, uint32_t position)
{
    using Fn = int32_t(__thiscall*)(char*, const char*, uint32_t);
    return reinterpret_cast<Fn>(function)(self + kBody, text, position);
}

// The game's 0x0090F546: the characters, or the empty string for a null
// pointer.
void __fastcall FromCharLength(char* self, void*, const char* text, uint32_t length)
{
    if (text) {
        BodyAssign(self, text, text + length);
        return;
    }
    char* const begin = At<char*>(self, kBegin);
    if (begin != At<char*>(self, kEnd)) {
        *begin = 0;
        At<char*>(self, kEnd) = At<char*>(self, kBegin);
    }
}

// The game's 0x0090F50F.
void __fastcall FromChar(char* self, void*, const char* text)
{
    FromCharLength(self, nullptr, text, text ? StrLen(text) : 0);
}

// The game's 0x0090E20A: the other string's text (its Data and length)
// through this string's slot 3.
void __fastcall Copy(char* self, void*, void* other)
{
    const uint32_t length = Length(other);
    const char* const text = Data(other);
    Virtual<void>(self, 0xC, text, length);
}

// The game's 0x0090E230: Copy through slot 14.
char* __fastcall AssignString(char* self, void*, void* other)
{
    Virtual<void>(self, 0x38, other);
    return self;
}

// The game's 0x0090F042: the new length, filled with zeros.
void* __fastcall Resize(char* self, void*, uint32_t length)
{
    using Fn = void*(__thiscall*)(char*, uint32_t, uint32_t);
    return reinterpret_cast<Fn>(addr::SC4String_ResizeBody)(self + kBody, length, 0);
}

// The game's 0x0090F053 and 0x0090F06C.
char* __fastcall AppendChars(char* self, void*, const char* text, uint32_t length)
{
    using Fn = void*(__thiscall*)(char*, const char*, uint32_t);
    reinterpret_cast<Fn>(addr::SC4StringBody_AppendChars)(self + kBody, text, length);
    return self;
}

char* __fastcall AppendString(char* self, void*, void* other)
{
    const uint32_t length = Length(other);
    return AppendChars(self, nullptr, ToChar(other), length);
}

// The game's 0x0090F093 and 0x0090F0B0.
char* __fastcall InsertChars(char* self, void*, uint32_t position, const char* text, uint32_t length)
{
    using Fn = void*(__thiscall*)(char*, uint32_t, const char*, uint32_t);
    reinterpret_cast<Fn>(addr::SC4StringBody_InsertChars)(self + kBody, position, text, length);
    return self;
}

char* __fastcall InsertString(char* self, void*, uint32_t position, void* other)
{
    const uint32_t length = Length(other);
    return InsertChars(self, nullptr, position, ToChar(other), length);
}

// The game's 0x0090FC13 and 0x0090FC30: `length` characters at `position`
// give way to the text (the body takes position, count, text).
char* __fastcall ReplaceChars(char* self, void*, uint32_t position, const char* text, uint32_t length)
{
    using Fn = void*(__thiscall*)(char*, uint32_t, uint32_t, const char*);
    reinterpret_cast<Fn>(addr::SC4StringBody_ReplaceChars)(self + kBody, position, length, text);
    return self;
}

char* __fastcall ReplaceString(char* self, void*, uint32_t position, void* other)
{
    const char* const text = ToChar(other);
    return ReplaceChars(self, nullptr, position, text, Length(other));
}

// The game's 0x0090E4D8.
char* __fastcall Erase(char* self, void*, uint32_t position, uint32_t count)
{
    using Fn = void*(__thiscall*)(char*, uint32_t, uint32_t);
    reinterpret_cast<Fn>(addr::SC4String_EraseBody)(self + kBody, position, count);
    return self;
}

// The game's 0x0090ED5D: equal only at the same length; with case by
// strncmp, without by memcmp of lowered copies (their lengths compared
// once more: lowering text that is not ASCII can change it).
bool __fastcall IsEqualChars(char* self, void*, const char* text, uint32_t length, uint8_t caseSensitive)
{
    if (length == 0xFFFFFFFFu)
        length = StrLen(text);
    if (Size(self) != length)
        return false;
    if (caseSensitive)
        return StrNCmp(Begin(self), text, Size(self)) == 0;
    Lowered mine(self);
    Lowered theirs(text, length);
    if (mine.Size() != theirs.Size())
        return false;
    return MemCmp(mine.Begin(), theirs.Begin(), mine.Size()) == 0;
}

// The game's 0x0090EC57: the other string's Data, at this string's length.
bool __fastcall IsEqual(char* self, void*, void* other, uint8_t caseSensitive)
{
    if (Size(self) != Length(other))
        return false;
    if (caseSensitive) {
        const char* const mine = Begin(self);
        const uint32_t size = Size(self);
        return StrNCmp(mine, Data(other), size) == 0;
    }
    Lowered mine(self);
    const uint32_t size = Size(self);
    Lowered theirs(Data(other), size);
    if (mine.Size() != theirs.Size())
        return false;
    return MemCmp(mine.Begin(), theirs.Begin(), mine.Size()) == 0;
}

// The game's 0x0090EF5B: the common part compared (with case by strncmp,
// without by memcmp of lowered copies); when that is equal, the difference
// of the lengths.
int32_t __fastcall CompareToChars(char* self, void*, const char* text, uint32_t length, uint8_t caseSensitive)
{
    if (length == 0xFFFFFFFFu)
        length = StrLen(text);
    const uint32_t size = Size(self);
    int32_t order;
    if (caseSensitive) {
        order = StrNCmp(Begin(self), text, length < size ? length : size);
    } else {
        Lowered mine(self);
        Lowered theirs(text, length);
        const uint32_t common = theirs.Size() < mine.Size() ? theirs.Size() : mine.Size();
        order = MemCmp(mine.Begin(), theirs.Begin(), common);
    }
    if (order == 0)
        order = static_cast<int32_t>(Size(self) - length);
    return order;
}

// The game's 0x0090EE61.
int32_t __fastcall CompareTo(char* self, void*, void* other, uint8_t caseSensitive)
{
    const uint32_t length = Length(other);
    const uint32_t size = Size(self);
    int32_t order;
    if (caseSensitive) {
        const char* const mine = Begin(self);
        order = StrNCmp(mine, Data(other), length < size ? length : size);
    } else {
        Lowered mine(self);
        const uint32_t theirLength = Length(other);
        Lowered theirs(Data(other), theirLength);
        const uint32_t common = theirs.Size() < mine.Size() ? theirs.Size() : mine.Size();
        order = MemCmp(mine.Begin(), theirs.Begin(), common);
    }
    if (order == 0)
        order = static_cast<int32_t>(Size(self) - Length(other));
    return order;
}

// The game's 0x0090F0DB and 0x0090F1FB: the body's find (reverse find) of
// the text from the position; without case, of the lowered text in the
// lowered copy.
int32_t FindChars(char* self, std::uintptr_t function, const char* text, uint32_t position, uint8_t caseSensitive)
{
    if (caseSensitive)
        return BodyFind(self, function, text, position);
    Lowered mine(self);
    Lowered theirs(text, StrLen(text));
    return BodyFind(mine.Self(), function, theirs.Begin(), position);
}

// The game's 0x0090F16C and 0x0090F28C: the same for a string.
int32_t FindString(char* self, std::uintptr_t function, void* other, uint32_t position, uint8_t caseSensitive)
{
    if (caseSensitive)
        return BodyFind(self, function, ToChar(other), position);
    Lowered mine(self);
    Lowered theirs(Lowered::FromInterface{}, other);
    return BodyFind(mine.Self(), function, theirs.Begin(), position);
}

int32_t __fastcall FindCharsForward(char* self, void*, const char* text, uint32_t position, uint8_t caseSensitive)
{
    return FindChars(self, addr::SC4StringBody_Find, text, position, caseSensitive);
}

int32_t __fastcall FindCharsBackward(char* self, void*, const char* text, uint32_t position, uint8_t caseSensitive)
{
    return FindChars(self, addr::SC4StringBody_RFind, text, position, caseSensitive);
}

int32_t __fastcall FindStringForward(char* self, void*, void* other, uint32_t position, uint8_t caseSensitive)
{
    return FindString(self, addr::SC4StringBody_Find, other, position, caseSensitive);
}

int32_t __fastcall FindStringBackward(char* self, void*, void* other, uint32_t position, uint8_t caseSensitive)
{
    return FindString(self, addr::SC4StringBody_RFind, other, position, caseSensitive);
}

// The game's 0x0090E1D1.
bool __fastcall QueryInterface(char* self, void*, uint32_t interfaceId, void** out)
{
    if (interfaceId != kIID_cIGZUnknown && interfaceId != kIID_cIGZString)
        return false;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return true;
}

// The game's 0x0090E1F3: the last reference deletes the string (slot 28
// with flag 1).
uint32_t __fastcall StringRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, kRefs);
    if (count > 1) {
        At<uint32_t>(self, kRefs) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, 0x70, uint32_t{1});
    return 0;
}

OPENSC4_THUNK(FromCharLengthThunk, FromCharLength, 8)
OPENSC4_THUNK(FromCharThunk, FromChar, 4)
OPENSC4_THUNK(CopyThunk, Copy, 4)
OPENSC4_THUNK(AssignStringThunk, AssignString, 4)
OPENSC4_THUNK(ResizeThunk, Resize, 4)
OPENSC4_THUNK(AppendCharsThunk, AppendChars, 8)
OPENSC4_THUNK(AppendStringThunk, AppendString, 4)
OPENSC4_THUNK(InsertCharsThunk, InsertChars, 12)
OPENSC4_THUNK(InsertStringThunk, InsertString, 8)
OPENSC4_THUNK(ReplaceCharsThunk, ReplaceChars, 12)
OPENSC4_THUNK(ReplaceStringThunk, ReplaceString, 8)
OPENSC4_THUNK(EraseThunk, Erase, 8)
OPENSC4_THUNK(IsEqualCharsThunk, IsEqualChars, 12)
OPENSC4_THUNK(IsEqualThunk, IsEqual, 8)
OPENSC4_THUNK(CompareToCharsThunk, CompareToChars, 12)
OPENSC4_THUNK(CompareToThunk, CompareTo, 8)
OPENSC4_THUNK(FindCharsForwardThunk, FindCharsForward, 12)
OPENSC4_THUNK(FindCharsBackwardThunk, FindCharsBackward, 12)
OPENSC4_THUNK(FindStringForwardThunk, FindStringForward, 12)
OPENSC4_THUNK(FindStringBackwardThunk, FindStringBackward, 12)
OPENSC4_THUNK(QueryInterfaceThunk, QueryInterface, 8)
OPENSC4_THUNK(StringReleaseThunk, StringRelease, 0)

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

bool InstallModify(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseString_FromCharLength, reinterpret_cast<void*>(&FromCharLengthThunk)},
        {addr::cRZBaseString_FromChar, reinterpret_cast<void*>(&FromCharThunk)},
        {addr::SC4String_AssignFromIGZString, reinterpret_cast<void*>(&CopyThunk)},
        {addr::cRZBaseString_AssignString, reinterpret_cast<void*>(&AssignStringThunk)},
        {addr::cRZBaseString_Truncate, reinterpret_cast<void*>(&ResizeThunk)},
        {addr::cRZBaseString_AppendChars, reinterpret_cast<void*>(&AppendCharsThunk)},
        {addr::cRZBaseString_AppendString, reinterpret_cast<void*>(&AppendStringThunk)},
        {addr::cRZBaseString_InsertChars, reinterpret_cast<void*>(&InsertCharsThunk)},
        {addr::cRZBaseString_InsertString, reinterpret_cast<void*>(&InsertStringThunk)},
        {addr::cRZBaseString_ReplaceChars, reinterpret_cast<void*>(&ReplaceCharsThunk)},
        {addr::cRZBaseString_ReplaceString, reinterpret_cast<void*>(&ReplaceStringThunk)},
        {addr::cRZBaseString_Erase, reinterpret_cast<void*>(&EraseThunk)},
    };
    return InstallAll(detours);
}

bool InstallCompare(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseString_IsEqualChars, reinterpret_cast<void*>(&IsEqualCharsThunk)},
        {addr::cRZBaseString_IsEqual, reinterpret_cast<void*>(&IsEqualThunk)},
        {addr::cRZBaseString_CompareToChars, reinterpret_cast<void*>(&CompareToCharsThunk)},
        {addr::cRZBaseString_CompareTo, reinterpret_cast<void*>(&CompareToThunk)},
        {addr::SC4String_Find, reinterpret_cast<void*>(&FindCharsForwardThunk)},
        {addr::cRZBaseString_RFindChars, reinterpret_cast<void*>(&FindCharsBackwardThunk)},
        {addr::cRZBaseString_FindString, reinterpret_cast<void*>(&FindStringForwardThunk)},
        {addr::cRZBaseString_RFindString, reinterpret_cast<void*>(&FindStringBackwardThunk)},
    };
    return InstallAll(detours);
}

bool InstallInterface(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseString_QueryInterface, reinterpret_cast<void*>(&QueryInterfaceThunk)},
        {addr::cRZBaseString_Release, reinterpret_cast<void*>(&StringReleaseThunk)},
    };
    return InstallAll(detours);
}

const bool g_registeredModify = registry::Register({
    "framework.string.modify",
    "framework/string",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZBaseString_FromChar,
    "Our string methods that change the text: FromChar, Copy, Resize, Append, Insert, Replace, Erase "
    "(docs/framework.md)",
    false,
    InstallModify,
});

const bool g_registeredCompare = registry::Register({
    "framework.string.compare",
    "framework/string",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZBaseString_IsEqual,
    "Our string comparisons and searches, with and without regard to case: IsEqual, CompareTo, Find, RFind "
    "(docs/framework.md)",
    false,
    InstallCompare,
});

const bool g_registeredInterface = registry::Register({
    "framework.string.interface",
    "framework/string",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZBaseString_QueryInterface,
    "Our QueryInterface and Release of the string class (docs/framework.md)",
    false,
    InstallInterface,
});

} // namespace
} // namespace opensc4::framework
