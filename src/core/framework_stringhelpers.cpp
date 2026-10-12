// The string class's helpers: the methods outside its interface that game
// code calls directly (docs/framework.md), on the game's own strings.
// Replacements, each off by default:
//
//   framework.string.helpers  BeginsWith (0x0090E443), EndsWith
//                             (0x0090E478), AppendText (0x0090EA42),
//                             AssignLimited (0x0090F430), Mid (0x0090EA57),
//                             Left (0x0090F45F), Right (0x0090F4B3), two
//                             assigning helpers (0x0090F31B, 0x0090FBE5)
//                             and the two conversions between a string and
//                             a wide string (0x0090E901, 0x0090E953)
//   framework.string.split    LTrim (0x0090E869), RTrim (0x0090E887), Trim
//                             (0x0090E8A6), CutThroughChar (0x0090E5DC),
//                             SplitOff (0x0090E98C), SplitAtChar
//                             (0x0090F335), SplitToken (0x0090F39D)
//   framework.string.case     MakeLower (0x0090EAC5), MakeUpper
//                             (0x0090EB8E)
//
// As with the interface's methods (framework_string.cpp), the class is a
// thin layer over a string body (STLport's: first character, one past the
// last, end of the block, at the object's +4), and the layer is what is
// ours. Still the game's: the body's operations (find, erase, assign,
// append, swap, the constructors), the C runtime's character tests and
// case tables, and the two conversions to and from wide characters that
// the case changes use for text that is not ASCII.
//
// Game code calls all of these directly: the methods are entered through
// thunks that keep ECX and EDX.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::framework {
namespace {

constexpr std::size_t kBody = 0x04;
constexpr std::size_t kBegin = 0x04;
constexpr std::size_t kEnd = 0x08;
constexpr uint32_t kNone = 0xFFFFFFFF;
constexpr uint32_t kStringSize = 0x14;

uint32_t Size(char* self)
{
    return static_cast<uint32_t>(At<char*>(self, kEnd) - At<char*>(self, kBegin));
}

// The body's operations, the game's.
uint32_t FindChar(char* body, char c, uint32_t position)
{
    using Fn = uint32_t(__thiscall*)(char*, uint32_t, uint32_t);
    return reinterpret_cast<Fn>(addr::SC4String_FindCharBody)(body, static_cast<uint32_t>(static_cast<int>(c)),
                                                               position);
}

void Erase(char* body, uint32_t position, uint32_t count)
{
    reinterpret_cast<void*(__thiscall*)(char*, uint32_t, uint32_t)>(addr::SC4String_EraseBody)(body, position, count);
}

void AssignPart(char* body, char* source, uint32_t position, uint32_t count)
{
    using Fn = void*(__thiscall*)(char*, char*, uint32_t, uint32_t);
    reinterpret_cast<Fn>(addr::SC4StringBody_AssignBody)(body, source, position, count);
}

void Swap(char* body, char* other)
{
    reinterpret_cast<void(__thiscall*)(char*, char*)>(addr::cGZDBSegmentPackedFile_F96EDF7)(body, other);
}

// A string built in `out` from part of another (the game's 0x00408BE0).
char* Substring(char* out, char* source, uint32_t position, uint32_t count)
{
    using Fn = char*(__thiscall*)(char*, char*, uint32_t, uint32_t);
    return reinterpret_cast<Fn>(addr::SC4String_Substring)(out, source, position, count);
}

// A string on the stack that ends as the game's temporaries do: its text
// freed, nothing else.
struct Temporary {
    char object[kStringSize];

    ~Temporary()
    {
        if (At<void*>(object, kBegin))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<void*>(object, kBegin));
    }
    char* MakeEmpty() { return reinterpret_cast<char*(__thiscall*)(char*)>(addr::SC4String_SC4String)(object); }
};

// The game's 0x0090E443.
bool __fastcall BeginsWith(char* self, void*, const char* text, uint32_t length)
{
    if (length > Size(self))
        return false;
    const char* const begin = At<char*>(self, kBegin);
    for (uint32_t i = 0; i < length; ++i)
        if (begin[i] != text[i])
            return false;
    return true;
}

// The game's 0x0090E478: compared from the last character back.
bool __fastcall EndsWith(char* self, void*, const char* text, uint32_t length)
{
    const uint32_t size = Size(self);
    if (length > size)
        return false;
    const char* const begin = At<char*>(self, kBegin);
    for (uint32_t i = 1; i <= length; ++i)
        if (begin[size - i] != text[length - i])
            return false;
    return true;
}

// The game's 0x0090EA42.
char* __fastcall AppendText(char* self, void*, const char* text)
{
    reinterpret_cast<void*(__thiscall*)(char*, const char*)>(addr::cSC4Advisor_F410F00)(self + kBody, text);
    return self;
}

// The game's 0x0090F430.
char* __fastcall AssignLimited(char* self, void*, const char* text, uint32_t most)
{
    const uint32_t length = static_cast<uint32_t>(std::strlen(text));
    const char* const end = text + (most > length ? length : most);
    using Fn = void*(__thiscall*)(char*, const char*, const char*);
    reinterpret_cast<Fn>(addr::SC4StringBody_AssignChars)(self + kBody, text, end);
    return self;
}

// The game's 0x0090EA57: the part is made in a temporary and copied out.
char* Mid(char* self, char* out, uint32_t position, uint32_t count)
{
    Temporary part;
    char* made;
    const uint32_t size = Size(self);
    if (position >= size) {
        made = part.MakeEmpty();
    } else {
        const uint32_t left = size - position;
        made = Substring(part.object, self, position, count < left ? count : left);
    }
    Substring(out, made, 0, kNone);
    return out;
}

char* __fastcall MidEntry(char* self, void*, char* out, uint32_t position, uint32_t count)
{
    return Mid(self, out, position, count);
}

// The game's 0x0090F45F: more than the string has gives all of it.
char* __fastcall Left(char* self, void*, char* out, uint32_t count)
{
    if (count > Size(self)) {
        Substring(out, self, 0, kNone);
        return out;
    }
    Temporary part;
    Substring(out, Mid(self, part.object, 0, count), 0, kNone);
    return out;
}

// The game's 0x0090F4B3.
char* __fastcall Right(char* self, void*, char* out, uint32_t count)
{
    const uint32_t size = Size(self);
    if (count > size) {
        Substring(out, self, 0, kNone);
        return out;
    }
    Temporary part;
    Substring(out, Mid(self, part.object, size - count, count), 0, kNone);
    return out;
}

// The game's 0x0090F31B: a body takes a string's text.
void* __cdecl AssignToBody(char* source, char* targetBody)
{
    using Fn = void*(__thiscall*)(char*, char*);
    return reinterpret_cast<Fn>(addr::cSC4Advice_F408C40)(targetBody, source ? source + kBody : nullptr);
}

// The game's 0x0090FBE5: a string takes a range of characters, through a
// temporary and its own operator=.
void __cdecl AssignFromRange(void* range, char* target)
{
    Temporary copy;
    reinterpret_cast<char*(__thiscall*)(char*, void*)>(addr::cSC4Advice_F408B90)(copy.object, range);
    reinterpret_cast<void*(__thiscall*)(char*, char*)>(addr::cRZBaseString_Assign)(target, copy.object);
}

// The game's 0x0090E901: wide characters above 0x7F become question marks.
void __cdecl FromWideAscii(char* wideBody, char* target)
{
    char* const body = target + kBody;
    reinterpret_cast<void*(__thiscall*)(char*, uint32_t, uint32_t)>(addr::SC4String_ResizeBody)(body, 0, 0);
    const int count = static_cast<int>(At<char*>(wideBody, 0x4) - At<char*>(wideBody, 0x0)) >> 1;
    const auto push = reinterpret_cast<void*(__thiscall*)(char*, int)>(addr::SC4String_PushBack);
    for (int i = 0; i < count; ++i) {
        const uint16_t* const wide = At<const uint16_t*>(wideBody, 0x0) + i;
        push(body, *wide > 0x7F ? '?' : static_cast<int>(static_cast<signed char>(*wide)));
    }
}

// The game's 0x0090E953: each byte is sign-extended, so a byte above 0x7F
// becomes a wide character above 0xFF00.
void __cdecl ToWideSigned(char* source, char* wideBody)
{
    reinterpret_cast<void*(__thiscall*)(char*, uint32_t, uint32_t)>(addr::SC4WideStringBody_Resize)(wideBody, 0, 0);
    const int count = static_cast<int>(Size(source));
    const auto push = reinterpret_cast<void*(__thiscall*)(char*, uint32_t)>(addr::SC4WideStringBody_PushBack);
    for (int i = 0; i < count; ++i)
        push(wideBody, static_cast<uint32_t>(static_cast<int>(static_cast<signed char>(At<char*>(source, kBegin)[i]))));
}

// The game's 0x0090E869: spaces and tabs.
void __fastcall LTrim(char* self, void*)
{
    char* const body = self + kBody;
    using Fn = uint32_t(__thiscall*)(char*, const char*, uint32_t);
    Erase(body, 0, reinterpret_cast<Fn>(addr::SC4StringBody_FindFirstNotOf)(body, " \t", 0));
}

// The game's 0x0090E887.
void __fastcall RTrim(char* self, void*)
{
    char* const body = self + kBody;
    using Fn = uint32_t(__thiscall*)(char*, const char*, uint32_t);
    Erase(body, reinterpret_cast<Fn>(addr::SC4StringBody_FindLastNotOf)(body, " \t", kNone) + 1, kNone);
}

// The game's 0x0090E8A6.
void __fastcall Trim(char* self, void*)
{
    LTrim(self, nullptr);
    RTrim(self, nullptr);
}

// The game's 0x0090E5DC: without the character the whole text goes.
bool __fastcall CutThroughChar(char* self, void*, char c)
{
    char* const body = self + kBody;
    const uint32_t position = FindChar(body, c, 0);
    if (position == kNone) {
        Erase(body, 0, kNone);
        return false;
    }
    Erase(body, 0, position + 1);
    return true;
}

// The game's 0x0090E98C: the text before the separator is built in `out`
// and leaves the string with the separator. Without a separator in the
// text, `out` is empty and the string keeps everything; a null or empty
// separator gives a copy of the string.
char* __fastcall SplitOff(char* self, void*, char* out, const char* separator)
{
    const uint32_t length = separator ? static_cast<uint32_t>(std::strlen(separator)) : 0;
    if (length == 0) {
        Substring(out, self, 0, kNone);
        return out;
    }
    char* const body = self + kBody;
    using FindFn = uint32_t(__thiscall*)(char*, const char*, uint32_t, uint32_t);
    const uint32_t position = reinterpret_cast<FindFn>(addr::SC4ByteString_Find)(body, separator, 0, length);
    Temporary part;
    if (position == kNone) {
        Substring(out, part.MakeEmpty(), 0, kNone);
    } else {
        Substring(part.object, self, 0, position);
        Erase(body, 0, position + length);
        Substring(out, part.object, 0, kNone);
    }
    return out;
}

// The game's 0x0090F335: the text before the character goes to `out` and
// leaves the string with the character. Without the character `out` takes
// everything (the two bodies change places after `out` is emptied) and the
// result is false.
bool __fastcall SplitAtChar(char* self, void*, char c, char* out)
{
    char* const body = self + kBody;
    const uint32_t position = FindChar(body, c, 0);
    if (position == kNone) {
        char* const outBody = out + kBody;
        Erase(outBody, 0, kNone);
        Swap(body, out ? outBody : nullptr);
        return false;
    }
    AssignPart(out + kBody, self ? body : nullptr, 0, position);
    Erase(body, 0, position + 1);
    return true;
}

// The game's 0x0090F39D: the next token between runs of the character.
// Leading separators are dropped; the token goes to `out` and leaves the
// string with the separators after it. The last token takes the whole
// rest; an empty string has none.
bool __fastcall SplitToken(char* self, void*, char c, char* out)
{
    char* const body = self + kBody;
    using NotOfFn = uint32_t(__thiscall*)(char*, uint32_t, uint32_t);
    const auto firstNotOf = reinterpret_cast<NotOfFn>(addr::SC4StringBody_FindFirstNotOfChar);
    for (;;) {
        const uint32_t position = FindChar(body, c, 0);
        if (position == kNone)
            break;
        const uint32_t next = firstNotOf(body, static_cast<uint32_t>(static_cast<int>(c)), position);
        if (position > 0) {
            AssignPart(out + kBody, self ? body : nullptr, 0, position);
            Erase(body, 0, next);
            return true;
        }
        Erase(body, 0, next);
    }
    if (At<char*>(self, kBegin) == At<char*>(self, kEnd))
        return false;
    char* const outBody = out + kBody;
    Erase(outBody, 0, kNone);
    Swap(outBody, self ? body : nullptr);
    return true;
}

// The game's 0x0090EAC5 and 0x0090EB8E. Characters below 0x7F change in
// place through the C runtime's test and table. At the first byte from
// 0x7F up, the whole text is turned into wide characters (in 256 of them on
// the stack, or a block for longer texts), each goes through the C
// runtime's wide table, and the result is turned back.
void ChangeCase(char* self, std::uintptr_t test, std::uintptr_t change, std::uintptr_t changeWide)
{
    char* p = At<char*>(self, kBegin);
    char* const end = At<char*>(self, kEnd);
    if (p == end)
        return;
    const auto is = reinterpret_cast<int(__cdecl*)(int)>(test);
    const auto to = reinterpret_cast<int(__cdecl*)(int)>(change);
    for (;;) {
        char b = *p;
        if (static_cast<uint8_t>(b) >= 0x7F)
            break;
        const int value = b;
        if (is(value))
            b = static_cast<char>(to(value));
        *p = b;
        if (++p == end)
            return;
    }

    uint32_t length = Size(self);
    uint16_t onStack[0x100];
    uint16_t* wide = onStack;
    struct {
        uint16_t* begin = nullptr;
        uint16_t* end = nullptr;
        uint16_t* capacity = nullptr;
    } block;
    if (length > 0x100) {
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4WideVector_Allocate)(&block, length);
        wide = block.begin;
    }
    using ToWideFn = uint32_t(__cdecl*)(char*, void*, uint32_t*, uint32_t);
    reinterpret_cast<ToWideFn>(addr::RZString_ConvertToBuffer)(self, wide, &length, 0x10);
    const auto toWide = reinterpret_cast<uint16_t(__cdecl*)(uint32_t)>(changeWide);
    for (uint32_t i = 0; i < length; ++i)
        wide[i] = toWide(wide[i]);
    using FromWideFn = uint32_t(__cdecl*)(const void*, uint32_t, uint32_t, char*);
    reinterpret_cast<FromWideFn>(addr::SC4String_FromWide)(wide, length, 0x10, self);
    if (block.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(block.begin);
}

void __fastcall MakeLower(char* self, void*)
{
    ChangeCase(self, addr::CRT_isupper, addr::CRT_tolower, addr::CRT_towlower);
}

void __fastcall MakeUpper(char* self, void*)
{
    ChangeCase(self, addr::CRT_islower, addr::CRT_toupper, addr::CRT_towupper);
}

OPENSC4_THUNK(BeginsWithThunk, BeginsWith, 8)
OPENSC4_THUNK(EndsWithThunk, EndsWith, 8)
OPENSC4_THUNK(AppendTextThunk, AppendText, 4)
OPENSC4_THUNK(AssignLimitedThunk, AssignLimited, 8)
OPENSC4_THUNK(MidThunk, MidEntry, 12)
OPENSC4_THUNK(LeftThunk, Left, 8)
OPENSC4_THUNK(RightThunk, Right, 8)
OPENSC4_THUNK(LTrimThunk, LTrim, 0)
OPENSC4_THUNK(RTrimThunk, RTrim, 0)
OPENSC4_THUNK(TrimThunk, Trim, 0)
OPENSC4_THUNK(CutThroughCharThunk, CutThroughChar, 4)
OPENSC4_THUNK(SplitOffThunk, SplitOff, 8)
OPENSC4_THUNK(SplitAtCharThunk, SplitAtChar, 8)
OPENSC4_THUNK(SplitTokenThunk, SplitToken, 8)
OPENSC4_THUNK(MakeLowerThunk, MakeLower, 0)
OPENSC4_THUNK(MakeUpperThunk, MakeUpper, 0)

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

bool InstallHelpers(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseString_BeginsWith, reinterpret_cast<void*>(&BeginsWithThunk)},
        {addr::cRZBaseString_EndsWith, reinterpret_cast<void*>(&EndsWithThunk)},
        {addr::cRZBaseString_AppendText, reinterpret_cast<void*>(&AppendTextThunk)},
        {addr::cRZBaseString_AssignLimited, reinterpret_cast<void*>(&AssignLimitedThunk)},
        {addr::cRZBaseString_Mid, reinterpret_cast<void*>(&MidThunk)},
        {addr::cRZBaseString_Left, reinterpret_cast<void*>(&LeftThunk)},
        {addr::cRZBaseString_Right, reinterpret_cast<void*>(&RightThunk)},
        {addr::cRZBaseString_AssignToBody, reinterpret_cast<void*>(&AssignToBody)},
        {addr::cRZBaseString_AssignFromRange, reinterpret_cast<void*>(&AssignFromRange)},
        {addr::cRZBaseString_FromWideAscii, reinterpret_cast<void*>(&FromWideAscii)},
        {addr::cRZBaseString_ToWideSigned, reinterpret_cast<void*>(&ToWideSigned)},
    };
    return InstallAll(detours);
}

bool InstallSplit(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseString_LTrim, reinterpret_cast<void*>(&LTrimThunk)},
        {addr::cRZBaseString_RTrim, reinterpret_cast<void*>(&RTrimThunk)},
        {addr::cRZBaseString_Trim, reinterpret_cast<void*>(&TrimThunk)},
        {addr::cRZBaseString_CutThroughChar, reinterpret_cast<void*>(&CutThroughCharThunk)},
        {addr::SC4String_SplitOff, reinterpret_cast<void*>(&SplitOffThunk)},
        {addr::SC4String_SplitAtChar, reinterpret_cast<void*>(&SplitAtCharThunk)},
        {addr::SC4String_SplitToken, reinterpret_cast<void*>(&SplitTokenThunk)},
    };
    return InstallAll(detours);
}

bool InstallCase(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseString_MakeLower, reinterpret_cast<void*>(&MakeLowerThunk)},
        {addr::cRZBaseString_MakeUpper, reinterpret_cast<void*>(&MakeUpperThunk)},
    };
    return InstallAll(detours);
}

const bool g_registeredHelpers = registry::Register({
    "framework.string.helpers",
    "framework/string",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZBaseString_BeginsWith,
    "Our string helpers: BeginsWith, EndsWith, AppendText, AssignLimited, Mid, Left, Right and four small "
    "assigning and converting helpers (docs/framework.md)",
    false,
    InstallHelpers,
});

const bool g_registeredSplit = registry::Register({
    "framework.string.split",
    "framework/string",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4String_SplitOff,
    "Our string helpers: the trims, CutThroughChar, SplitOff, SplitAtChar and SplitToken (docs/framework.md)",
    false,
    InstallSplit,
});

const bool g_registeredCase = registry::Register({
    "framework.string.case",
    "framework/string",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZBaseString_MakeLower,
    "Our string helpers: MakeLower and MakeUpper (docs/framework.md)",
    false,
    InstallCase,
});

} // namespace
} // namespace opensc4::framework
