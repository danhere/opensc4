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
//
// The second slice (part u12, slice 2): the helpers part u11 left in its
// range, which the property handlers call with arguments in registers too:
//
//   framework.uiscript.propmake  the makers that give a window (ESI) a new
//                                0x14-byte script value through its slot
//                                114: 0x0094C1A4 (12 bytes), 0x0094C1DA (8
//                                bytes), 0x0094C222 (a number) and
//                                0x0094C255 (a text)
//   framework.uiscript.propload  0x0094C4E0 (an image's key, 0x856DDBAC)
//                                and 0x0094C58D (a text's, 0x2026960B, into
//                                the string in EDI): asked of the UI script
//                                loader's held object, else of the resource
//                                manager
//   framework.uiscript.proplook  the lookups in vectors of pairs: whether a
//                                key is there 0x0094C64E (the vector in
//                                EAX), a number value's word through slot 73
//                                0x0094C675 or slot 75 0x0094C6C7 (the
//                                vector in EDI), and the flags read from a
//                                table of (key, mask) 0x0094C719
//
// The same kind of thunks: the registers' values and the stack arguments
// handed to a cdecl body; where the game writes into its own argument slots
// (the loaders' out, the flags), the body gets those slots. The flags reader
// calls our 0x0094C675. These groups go in at Attach too.
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
// The makers

// A script value (SC4ScriptNumber, 0x14 bytes) from SC4::operator_new and
// one of its constructors; none when there is no memory.
constexpr std::size_t kValueSize = 0x14;
constexpr uint32_t kBytesKind = 0x8000;
// The window's slot 114: a value put under a key.
constexpr std::size_t kWindowPut = 0x1C8;
// What the makers add to their key.
constexpr uint32_t kPairKeyBase = 0xDAA3B860;
constexpr uint32_t kValueKeyBase = 0xFAA3BE85;

void* NewValue()
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(kValueSize);
}

void* ValueFromBytes(const void* data, uint32_t size)
{
    void* const memory = NewValue();
    return memory ? CallThis<void*>(addr::SC4ScriptNumber_FromBytes, memory, data, size, kBytesKind) : nullptr;
}

inline uint32_t WithTrue(uint32_t eax) { return (eax & ~0xFFu) | 1u; }

// The game's 0x0094C1A4 (cdecl: a key and 12 bytes, a resource key's three
// words; the window in ESI): without a window nothing (the caller's EAX
// back); else the 12 bytes as a value (kind 0x8000) put under the key,
// slot 114's EAX answered.
uint32_t __cdecl MakeKeyBody(uint32_t eax, void* window, uint32_t key, const void* data)
{
    OPENSC4_FIRST("MakeKey");
    if (!window)
        return eax;
    return Virtual<uint32_t>(window, kWindowPut, key, ValueFromBytes(data, 0xC));
}

// The game's 0x0094C1DA (cdecl: the second word and a key; the first word
// in EAX, the window in ESI, unchecked): the two words as an 8-byte value
// put under the key + 0xDAA3B860; AL 1 over slot 114's EAX.
uint32_t __cdecl MakePairBody(uint32_t first, void* window, uint32_t second, uint32_t key)
{
    OPENSC4_FIRST("MakePair");
    const uint32_t words[2] = {first, second};
    return WithTrue(Virtual<uint32_t>(window, kWindowPut, key + kPairKeyBase, ValueFromBytes(words, 8)));
}

// The game's 0x0094C222 (cdecl: a number and a key; the window in ESI): a
// number value (0x00915D6D) put under the key + 0xFAA3BE85; AL 1 over slot
// 114's EAX.
uint32_t __cdecl MakeNumberBody(void* window, uint32_t number, uint32_t key)
{
    OPENSC4_FIRST("MakeNumber");
    void* const memory = NewValue();
    void* const value = memory ? CallThis<void*>(addr::SC4ScriptNumber_FromUint, memory, number) : nullptr;
    return WithTrue(Virtual<uint32_t>(window, kWindowPut, key + kValueKeyBase, value));
}

// The game's 0x0094C255: the same with a text value (a game string's
// characters, 0x00915CDE).
uint32_t __cdecl MakeTextBody(void* window, void* text, uint32_t key)
{
    OPENSC4_FIRST("MakeText");
    void* const memory = NewValue();
    void* const value = memory ? CallThis<void*>(addr::SC4ScriptNumber_FromString, memory, text) : nullptr;
    return WithTrue(Virtual<uint32_t>(window, kWindowPut, key + kValueKeyBase, value));
}

// ====================================================================================
// The loaders

// The resource types the loaders ask for, and the interfaces they ask the
// resource manager for.
constexpr uint32_t kImageType = 0x856DDBAC;
constexpr uint32_t kTextType = 0x2026960B;
constexpr uint32_t kImageInterface = 0x06F1568E;
constexpr uint32_t kStringInterface = 0x089B7DC8;

// The two reference holders the loaders make (one word each): the UI
// script loader (0x00449750) and the resource manager (0x004496C0), both
// destroyed by 0x004495C0, whose EAX the text loader answers above its byte.
struct Holder {
    void* object;
    explicit Holder(std::uintptr_t constructor) { CallThis<void*>(constructor, this); }
    uint32_t Destroy() { return CallThis<uint32_t>(addr::SC4RefPtr_Destruct, this); }
};

// The UI script loader's held object (its slot 9), none without a loader.
void* HeldObject(const Holder& loader) { return loader.object ? Virtual<void*>(loader.object, 0x24) : nullptr; }

void ReleaseIfAny(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 8);
}

// The game's 0x0094C4E0 (cdecl: the instance; the group in EAX): the key
// (0x856DDBAC, group, instance) asked of the loader's held object (slot 3,
// its EAX answered), else of the resource manager (slot 3 with the
// interface 0x06F1568E into the argument's own slot, zeroed first; when it
// answers, the resource's slot 3 is answered, else 0), the resource
// released. The holders destroyed in reverse.
uint32_t __cdecl LoadImageBody(uint32_t group, uint32_t* arguments)
{
    OPENSC4_FIRST("LoadImage");
    const uint32_t key[3] = {kImageType, group, arguments[0]};
    Holder loader(addr::SC4RefPtr_UIScriptLoader);
    uint32_t result;
    if (void* const held = HeldObject(loader)) {
        result = Virtual<uint32_t>(held, 0xC, key);
    } else {
        arguments[0] = 0;
        Holder manager(addr::SC4RefPtr_ResourceManager);
        result = 0;
        void** const out = reinterpret_cast<void**>(&arguments[0]);
        if (Yes(Virtual<uint32_t>(manager.object, 0xC, key, kImageInterface, out, 0u, 0u)))
            result = Virtual<uint32_t>(*out, 0xC);
        manager.Destroy();
        ReleaseIfAny(*out);
    }
    loader.Destroy();
    return result;
}

// The game's 0x0094C58D (cdecl: the instance; the group in EAX, the string
// in EDI): the key (0x2026960B, group, instance) asked of the loader's
// held object (slot 4 with the string; its AL answered), else of the
// resource manager (slot 4 with the interface 0x089B7DC8 into the
// argument's own slot, zeroed first; when it answers, the string's slot 13
// copies the text, AL 1), the text released. AL over the loader holder's
// destruction's EAX.
uint32_t __cdecl LoadTextBody(uint32_t group, void* string, uint32_t* arguments)
{
    OPENSC4_FIRST("LoadText");
    const uint32_t key[3] = {kTextType, group, arguments[0]};
    Holder loader(addr::SC4RefPtr_UIScriptLoader);
    uint32_t found = 0;
    if (void* const held = HeldObject(loader)) {
        found = Virtual<uint32_t>(held, 0x10, key, string) & 0xFF;
    } else {
        arguments[0] = 0;
        Holder manager(addr::SC4RefPtr_ResourceManager);
        void** const out = reinterpret_cast<void**>(&arguments[0]);
        if (Yes(Virtual<uint32_t>(manager.object, 0x10, key, kStringInterface, out, 0u, 0u))) {
            Virtual<uint32_t>(string, 0x34, *out);
            found = 1;
        }
        manager.Destroy();
        ReleaseIfAny(*out);
    }
    return (loader.Destroy() & ~0xFFu) | found;
}

// ====================================================================================
// The lookups

// A number value (slot 3 answers 6).
constexpr uint32_t kNumberKind = 6;

inline uint32_t PairCount(char* const* vector)
{
    return static_cast<uint32_t>(static_cast<int32_t>(vector[1] - vector[0]) >> 3);
}

// The game's 0x0094C64E (cdecl: the key; the vector of pairs in EAX):
// whether a pair has the key. AL over the pair count.
uint32_t __cdecl HasKeyBody(char* const* vector, uint32_t key)
{
    OPENSC4_FIRST("HasKey");
    const uint32_t count = PairCount(vector);
    for (uint32_t i = 0; i < count; ++i) {
        if (At<uint32_t>(vector[0] + i * 8, 0) == key)
            return (count & ~0xFFu) | 1u;
    }
    return count & ~0xFFu;
}

// The game's 0x0094C675 and 0x0094C6C7 (cdecl: the key and an out; the
// vector in EDI): the first pair with the key whose value is set and a
// number (slot 3 answers 6); its slot 73 (or 75) gives a pointer whose word
// goes to the out; AL 1 over that word. AL 0 when none, over what EAX last
// held there: the caller's for an empty vector, else the last pair's
// place, its null value or its slot 3's answer. The count is taken once,
// the vector's start each time.
// The value's slot 3 and slot 73 (or 75) as the game calls them, EDX kept
// for the thunk: slot 3 with EDX the value's vtable, the word's slot with
// EDX as the last call left it; EDX after each call back in *edx. Callers
// of the finders push SETcc DL as a bool argument, over EDX's upper bytes.
__declspec(naked) uint32_t __cdecl CallKind(void* /*value*/, uint32_t* /*edx*/)
{
    __asm mov ecx, [esp + 4]
    __asm mov edx, [ecx]
    __asm call dword ptr [edx + 0xC]
    __asm mov ecx, [esp + 8]
    __asm mov [ecx], edx
    __asm ret
}

__declspec(naked) const uint32_t* __cdecl CallWord(void* /*value*/, std::size_t /*slot*/, uint32_t* /*edx*/)
{
    __asm mov ecx, [esp + 12]
    __asm mov edx, [ecx]
    __asm mov ecx, [esp + 4]
    __asm mov eax, [ecx]
    __asm add eax, [esp + 8]
    __asm call dword ptr [eax]
    __asm mov ecx, [esp + 12]
    __asm mov [ecx], edx
    __asm ret
}

uint32_t FindWord(uint32_t eax, char* const* vector, uint32_t key, uint32_t* out, std::size_t slot, uint32_t* edx)
{
    const uint32_t count = PairCount(vector);
    for (uint32_t i = 0; i < count; ++i) {
        char* const pair = vector[0] + i * 8;
        eax = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pair));
        if (At<uint32_t>(pair, 0) != key)
            continue;
        void* const value = At<void*>(pair, 4);
        eax = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(value));
        if (!value)
            continue;
        eax = CallKind(value, edx);
        if (eax == kNumberKind) {
            const uint32_t word = *CallWord(At<void*>(vector[0] + i * 8, 4), slot, edx);
            *out = word;
            return (word & ~0xFFu) | 1u;
        }
    }
    return eax & ~0xFFu;
}

// The bodies get EDX's place on the thunk's stack (the caller's EDX at
// entry; the thunk pops it back into EDX).
uint32_t __cdecl FindWord73Body(uint32_t eax, char* const* vector, uint32_t key, uint32_t* out, uint32_t* edx)
{
    OPENSC4_FIRST("FindWord73");
    return FindWord(eax, vector, key, out, 0x124, edx);
}

uint32_t __cdecl FindWord75Body(uint32_t eax, char* const* vector, uint32_t key, uint32_t* out, uint32_t* edx)
{
    OPENSC4_FIRST("FindWord75");
    return FindWord(eax, vector, key, out, 0x12C, edx);
}

// The game's 0x0094C719 (cdecl: the vector and the flags; a table of
// (key, mask) pairs ending with key 0 in EAX): for each key found as a
// number (0x0094C675; the out zeroed first) the mask cleared from the
// flags, and set again when the word is 1. The flags are kept in their own
// argument slot; EAX the flags.
uint32_t __cdecl ReadFlagsBody(const uint32_t* table, uint32_t* arguments)
{
    OPENSC4_FIRST("ReadFlags");
    char* const* const vector = reinterpret_cast<char* const*>(arguments[0]);
    for (; table[0]; table += 2) {
        uint32_t word = 0;
        uint32_t edx = 0;
        if (Yes(FindWord73Body(table[0], vector, table[0], &word, &edx))) {
            arguments[1] &= ~table[1];
            if (word == 1)
                arguments[1] |= table[1];
        }
    }
    return arguments[1];
}

// The thunks. At entry the first stack argument is at [esp+4].
__declspec(naked) void MakeKey()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push esi
    __asm push eax
    __asm call MakeKeyBody
    __asm add esp, 16
    __asm ret
}

__declspec(naked) void MakePair()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push esi
    __asm push eax
    __asm call MakePairBody
    __asm add esp, 16
    __asm ret
}

__declspec(naked) void MakeNumber()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push esi
    __asm call MakeNumberBody
    __asm add esp, 12
    __asm ret
}

__declspec(naked) void MakeText()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push esi
    __asm call MakeTextBody
    __asm add esp, 12
    __asm ret
}

__declspec(naked) void LoadImage()
{
    __asm lea ecx, [esp + 4]
    __asm push ecx
    __asm push eax
    __asm call LoadImageBody
    __asm add esp, 8
    __asm ret
}

__declspec(naked) void LoadText()
{
    __asm lea ecx, [esp + 4]
    __asm push ecx
    __asm push edi
    __asm push eax
    __asm call LoadTextBody
    __asm add esp, 12
    __asm ret
}

__declspec(naked) void HasKey()
{
    __asm push dword ptr [esp + 4]
    __asm push eax
    __asm call HasKeyBody
    __asm add esp, 8
    __asm ret
}

__declspec(naked) void FindWord73()
{
    __asm push edx
    __asm mov ecx, esp
    __asm push ecx
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push edi
    __asm push eax
    __asm call FindWord73Body
    __asm add esp, 20
    __asm pop edx
    __asm ret
}

__declspec(naked) void FindWord75()
{
    __asm push edx
    __asm mov ecx, esp
    __asm push ecx
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push edi
    __asm push eax
    __asm call FindWord75Body
    __asm add esp, 20
    __asm pop edx
    __asm ret
}

__declspec(naked) void ReadFlags()
{
    __asm lea ecx, [esp + 4]
    __asm push ecx
    __asm push eax
    __asm call ReadFlagsBody
    __asm add esp, 8
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

bool InstallPropMake(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptProps_MakeKey, &MakeKey},
        {addr::cGZUIScriptProps_MakePair, &MakePair},
        {addr::cGZUIScriptProps_MakeNumber, &MakeNumber},
        {addr::cGZUIScriptProps_MakeText, &MakeText},
    };
    return InstallAll(detours);
}

bool InstallPropLoad(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptProps_LoadImage, &LoadImage},
        {addr::cGZUIScriptProps_LoadText, &LoadText},
    };
    return InstallAll(detours);
}

bool InstallPropLook(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptProps_HasKey, &HasKey},
        {addr::cGZUIScriptProps_FindWord73, &FindWord73},
        {addr::cGZUIScriptProps_FindWord75, &FindWord75},
        {addr::cGZUIScriptProps_ReadFlags, &ReadFlags},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.uiscript.propread", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_FindText,
                        "Our readers of a window's script properties: the text finder and the scanners of "
                        "three bytes, a pair and a rectangle (4 detours; part u12, docs/gzui.md)",
                        false, InstallPropRead}),
    registry::Register({"framework.uiscript.propmake", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_MakeKey,
                        "Our makers of a window's script values: 12 bytes, 8 bytes, a number and a text put "
                        "through the window's slot 114 (4 detours; part u12, docs/gzui.md)",
                        false, InstallPropMake}),
    registry::Register({"framework.uiscript.propload", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_LoadImage,
                        "Our loaders of an image and a text by key for a window's script: the UI script "
                        "loader's held object, else the resource manager (2 detours; part u12, docs/gzui.md)",
                        false, InstallPropLoad}),
    registry::Register({"framework.uiscript.proplook", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_HasKey,
                        "Our lookups in a window's script properties: a key, a number's word through slot 73 "
                        "or 75, and flags from a table of masks (4 detours; part u12, docs/gzui.md)",
                        false, InstallPropLook}),
};

} // namespace
} // namespace opensc4::gzui
