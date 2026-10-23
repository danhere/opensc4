// The window's script properties, first slice (docs/gzui.md, part u12): the
// readers the property handlers 0x0094CE28 to 0x00951084 use to take a
// value's text from a window's script properties. A script's properties
// are an STLport vector of 8-byte pairs (a key, a property object); a
// property whose slot 3 answers 0x800E is text, and its slot 55 puts that
// text into a string. The readers find a key's text and scan it as
// "(%d,%d,%d)" (three bytes), "(%d,%d)" or "(%d,%d,%d,%d)" (a rectangle).
// Replacements, each off by default:
//
//   framework.uiscript.propread  the text finder 0x0094CCD9 and the three
//                                scanners 0x0094D4CD, 0x0094D55A and
//                                0x0094D5CA
//
// They take arguments in registers (the vector in EDI, the text in EBX, the
// rectangle in ESI) as well as on the stack (cdecl): each is entered
// through a naked thunk that hands them on and keeps every register the
// game's does. The scanners call our finder directly, as the game calls its
// own. Still the game's: the rest of the range (see the doc). The group
// goes in at Attach, with the UI script loader's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

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

inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

template <typename... Args>
inline int32_t Scan(const char* text, std::uintptr_t format, Args... args)
{
    using Fn = int32_t(__cdecl*)(const char*, const char*, ...);
    return reinterpret_cast<Fn>(addr::SC4_sscanf)(text, reinterpret_cast<const char*>(format), args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: props: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The readers

// A string (SC4String, 0x14 bytes): the vtable 0x00A80810, the characters
// from +4 to +8.
constexpr std::size_t kTextBegin = 4;
constexpr std::size_t kTextEnd = 8;
constexpr std::size_t kTextSize = 0x14;
// What a text property's slot 3 answers.
constexpr uint32_t kTextKind = 0x800E;

// The scanners' strings, made and destroyed as the game does on its stack:
// the destructor inline (the vtable back, the characters freed).
struct Text {
    alignas(4) char bytes[kTextSize];
    Text() { CallThis<void*>(addr::SC4String_SC4String, bytes); }
    ~Text()
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (char* const begin = At<char*>(bytes, kTextBegin))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    }
    const char* Begin() { return At<const char*>(bytes, kTextBegin); }
};

// The game's 0x0094CCD9 (cdecl; the key on the stack, the string in EBX,
// the vector of pairs in EDI): the string emptied (when not already), then
// the first pair with the key whose property is set and answers 0x800E
// (slot 3) puts its text in the string (slot 55; its EAX answered). The
// count is taken once, the vector's start each time. AL 0 when none; the
// game leaves leftovers of its own above it (a pointer, slot 3's answer;
// ours 0). Every caller tests AL or ignores it.
uint32_t __cdecl FindTextBody(char* text, char* const* vector, uint32_t key)
{
    OPENSC4_FIRST("FindText");
    char* const begin = At<char*>(text, kTextBegin);
    if (begin != At<char*>(text, kTextEnd)) {
        *begin = 0;
        At<char*>(text, kTextEnd) = begin;
    }
    const uint32_t count = static_cast<uint32_t>((vector[1] - vector[0]) >> 3);
    for (uint32_t i = 0; i < count; ++i) {
        char* const pair = vector[0] + i * 8;
        if (At<uint32_t>(pair, 0) != key)
            continue;
        void* const property = At<void*>(pair, 4);
        if (property && Virtual<uint32_t>(property, 0xC) == kTextKind)
            return Virtual<uint32_t>(At<void*>(vector[0] + i * 8, 4), 0xDC, text);
    }
    return 0;
}

// The game's 0x0094D4CD (cdecl: the key and three byte pointers; the
// vector in EDI): the key's text (0x0094CCD9) scanned as "(%d,%d,%d)"; with
// all three the low bytes stored and AL 1. AL 0 otherwise (no byte stored).
uint32_t __cdecl ReadBytesBody(char* const* vector, uint32_t key, uint8_t* first, uint8_t* second,
                               uint8_t* third)
{
    OPENSC4_FIRST("ReadBytes");
    Text text;
    if (!Yes(FindTextBody(text.bytes, vector, key)))
        return 0;
    int32_t values[3];
    if (Scan(text.Begin(), addr::cGZUIScriptProps_BytesFormat, &values[0], &values[1], &values[2]) != 3)
        return 0;
    *first = static_cast<uint8_t>(values[0]);
    *second = static_cast<uint8_t>(values[1]);
    *third = static_cast<uint8_t>(values[2]);
    return 1;
}

// The game's 0x0094D55A (cdecl: the key and two int pointers; the vector
// in EDI): the key's text scanned as "(%d,%d)" straight into them; AL 1
// when both were read (sscanf writes what it reads either way).
uint32_t __cdecl ReadPairBody(char* const* vector, uint32_t key, int32_t* first, int32_t* second)
{
    OPENSC4_FIRST("ReadPair");
    Text text;
    if (!Yes(FindTextBody(text.bytes, vector, key)))
        return 0;
    return Scan(text.Begin(), addr::cGZUIScriptProps_PairFormat, first, second) == 2 ? 1u : 0u;
}

// The game's 0x0094D5CA (cdecl: the key; the rectangle's four ints in ESI,
// the vector in EDI): the key's text scanned as "(%d,%d,%d,%d)" into them;
// AL 1 when all four were read.
uint32_t __cdecl ReadRectBody(char* const* vector, uint32_t key, int32_t* rect)
{
    OPENSC4_FIRST("ReadRect");
    Text text;
    if (!Yes(FindTextBody(text.bytes, vector, key)))
        return 0;
    return Scan(text.Begin(), addr::cGZUIScriptProps_RectFormat, &rect[0], &rect[1], &rect[2], &rect[3]) == 4
               ? 1u
               : 0u;
}

// The thunks: the stack arguments handed on after the registers' values;
// the bodies keep EBX, ESI, EDI and EBP. At entry the key is at [esp+4].
__declspec(naked) void FindText()
{
    __asm push dword ptr [esp + 4]
    __asm push edi
    __asm push ebx
    __asm call FindTextBody
    __asm add esp, 12
    __asm ret
}

__declspec(naked) void ReadBytes()
{
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push edi
    __asm call ReadBytesBody
    __asm add esp, 20
    __asm ret
}

__declspec(naked) void ReadPair()
{
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push edi
    __asm call ReadPairBody
    __asm add esp, 16
    __asm ret
}

__declspec(naked) void ReadRect()
{
    __asm push esi
    __asm push dword ptr [esp + 8]
    __asm push edi
    __asm call ReadRectBody
    __asm add esp, 12
    __asm ret
}

// ====================================================================================
// Install

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

bool InstallPropRead(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptProps_FindText, &FindText},
        {addr::cGZUIScriptProps_ReadBytes, &ReadBytes},
        {addr::cGZUIScriptProps_ReadPair, &ReadPair},
        {addr::cGZUIScriptProps_ReadRect, &ReadRect},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.uiscript.propread", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_FindText,
                        "Our readers of a window's script properties: the text finder and the scanners of "
                        "three bytes, a pair and a rectangle (4 detours; part u12, docs/gzui.md)",
                        false, InstallPropRead}),
};

} // namespace
} // namespace opensc4::gzui
