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
//
// The third slice (part u12, slice 3): the handlers themselves, each called
// (cdecl) with a window, its script's vector of pairs and the pass, and the
// filler of the variable table's words:
//
//   framework.uiscript.propwords   0x0094D641, the 64 words ("clsid", "yes",
//                                  "font", "winflag_visible"...) given to
//                                  the variable table (its pointer at
//                                  0x00B63588)
//   framework.uiscript.prophandle  the handlers of eight controls'
//                                  interfaces: 0x0094E33B (0xC2AFA76F),
//                                  0x0094E516 (0x212CDC1F, the text control),
//                                  0x0094E7C8 (0x231A1C57, the text edit),
//                                  0x0094EE39 (0x4132242B), 0x0094F757
//                                  (0x8810, the button), 0x0095002C
//                                  (0xC12CEA13), 0x00950218 (0x21325207)
//                                  and 0x00950AF6 (0x612CE0C3)
//
// The handlers call our readers, makers, loaders and lookups directly. They
// go in at Attach too.
//
// The fourth slice (part u12, slice 4): the handlers of six stock widgets no
// shipped UI script makes, so they never run in the game:
//
//   framework.uiscript.prophandle2  0x0094CE28 (0x032F0B3C, the text
//                                   ticker), 0x0094D17E (0xA13351B8, the
//                                   folders), 0x0094F36D (0x412CE496, the
//                                   combo), 0x009503AB (0x61325A2D, the
//                                   scrollbar), 0x009505B6 (0x21335C59, the
//                                   line input) and 0x00951084 (0xE303E5AE,
//                                   the outline)
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

template <typename T>
inline T Global(std::uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
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
    // A game string made from a C string (SC4String::FromText).
    explicit Text(const char* text) { CallThis<void*>(addr::SC4String_FromText, bytes, text); }
    ~Text()
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (char* const begin = At<char*>(bytes, kTextBegin))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    }
    const char* Begin() const { return *reinterpret_cast<const char* const*>(bytes + kTextBegin); }
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
// The words

// The words 0x0094D641 gives the variable table (its slot 3, a game string
// and the word's value), in the game's order.
struct Word {
    const char* text;
    uint32_t value;
};

constexpr Word kWords[] = {
    {"clsid", 0xFFFFFFFF},
    {"iid", 0xFFFFFFFE},
    {"yes", 1},
    {"true", 1},
    {"no", 0},
    {"false", 0},
    {"on", 1},
    {"off", 0},
    {"GZWin", 0xE2BA00EE},
    {"id", 0x100},
    {"area", 0x101},
    {"pos", 0x102},
    {"size", 0x103},
    {"fillcolor", 0x104},
    {"caption", 0x105},
    {"captionres", 0x106},
    {"transparent", 0x107},
    {"font", 0xF000},
    {"bkgcolor", 0xF001},
    {"forecolor", 0xF002},
    {"notify", 0xF003},
    {"gutters", 0xF004},
    {"style", 0xF005},
    {"left", 0xF006},
    {"right", 0xF007},
    {"center", 0xF008},
    {"flat", 0xF009},
    {"normal", 0xF00A},
    {"sunken", 0xF00B},
    {"raised", 0xF00C},
    {"fill", 0xF00D},
    {"nofill", 0xF00E},
    {"colorfontnormal", 0xF00F},
    {"colorfontdisabled", 0xF010},
    {"colorfonthilited", 0xF011},
    {"colorfontnormalbkg", 0xF012},
    {"colorfontdisabledbkg", 0xF013},
    {"colorfonthilitedbkg", 0xF014},
    {"align", 0xF015},
    {"image", 0xF016},
    {"imagetype", 0xF017},
    {"imagerect", 0xF018},
    {"outline", 0xF019},
    {"comments", 0x108},
    {"commentslmd", 0x109},
    {"commentsrmd", 0x10A},
    {"commentslmu", 0x10B},
    {"commentsrmu", 0x10C},
    {"commentsmm", 0x10D},
    {"commentsku", 0x10E},
    {"commentskd", 0x10F},
    {"winflag_visible", 0xF01A},
    {"winflag_enabled", 0xF01B},
    {"winflag_moveable", 0xF01C},
    {"winflag_sizeable", 0xF01D},
    {"winflag_sortable", 0xF01E},
    {"winflag_pbuff", 0xF01F},
    {"winflag_pbufftrans", 0xF020},
    {"winflag_pbufferase", 0xF021},
    {"winflag_pbuffvid", 0xF022},
    {"winflag_alphablend", 0xF026},
    {"winflag_mousetrans", 0xF023},
    {"winflag_ignoremouse", 0xF024},
    {"winflag_acceptfocus", 0xF025},
};

// The game's 0x0094D641 (cdecl, no arguments; the first LEGACY reader's
// constructor calls it once the variable table is made): each word as a
// game string (SC4String::FromText) given to the table's slot 3 with its
// value, the string destroyed inline. The table's vtable is read before
// the string is made, the table again after.
void __cdecl AddWords()
{
    OPENSC4_FIRST("AddWords");
    for (const Word& word : kWords) {
        void** const vtable = *Global<void***>(addr::cGZUIScriptLegacy_Table);
        Text text(word.text);
        reinterpret_cast<uint32_t(__thiscall*)(void*, void*, uint32_t)>(vtable[3])(
            Global<void*>(addr::cGZUIScriptLegacy_Table), text.bytes, word.value);
    }
}

// ====================================================================================
// The property handlers

// Each handler (cdecl: the window, the vector of its script's pairs and a
// byte, 0 for the first pass and 1 for the second) asks the window for a
// control's interface (its slot 0 with an out), and with one reads the
// properties into the control's slots, the window's slot 114 or the makers.
// The bodies get the handler's argument slots: the game uses the window's
// and others as outs.
//
// The locals are named after the game's frame offsets (f10 is [ebp-0x10]).
// Where the game pushes a dword of which it wrote only the low byte, or one
// it never wrote, the bytes above are its stack's leftovers; ours are 0 (or
// what ours last wrote there). The controls take those as bytes.

// The properties as the handlers read them: the vector, and the EDX the
// number finders leave (their callers push SETcc DL over its upper bytes).
struct Props {
    char* const* vector;
    uint32_t edx = 0;

    explicit Props(uint32_t vector32) : vector(reinterpret_cast<char* const*>(vector32)) {}
    bool Word(uint32_t key, uint32_t* out) { return Yes(FindWord73Body(0, vector, key, out, &edx)); }
    bool Word75(uint32_t key, uint32_t* out) { return Yes(FindWord75Body(0, vector, key, out, &edx)); }
    bool Has(uint32_t key) const { return Yes(HasKeyBody(vector, key)); }
    bool Find(Text& text, uint32_t key) const { return Yes(FindTextBody(text.bytes, vector, key)); }
    bool Bytes(uint32_t key, uint32_t* first, uint32_t* second, uint32_t* third) const
    {
        return Yes(ReadBytesBody(vector, key, reinterpret_cast<uint8_t*>(first), reinterpret_cast<uint8_t*>(second),
                                 reinterpret_cast<uint8_t*>(third)));
    }
    bool Pair(uint32_t key, uint32_t* first, uint32_t* second) const
    {
        return Yes(ReadPairBody(vector, key, reinterpret_cast<int32_t*>(first), reinterpret_cast<int32_t*>(second)));
    }
    bool Rect(uint32_t key, int32_t* rect) const { return Yes(ReadRectBody(vector, key, rect)); }
    // SETcc DL; PUSH EDX after a number finder.
    uint32_t Dl(bool value) const { return (edx & ~0xFFu) | (value ? 1u : 0u); }
};

inline void* AsPointer(uint32_t value) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value)); }
inline uint32_t AsWord(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// The window's control for an interface (its slot 0), and its release.
struct Control {
    void* object = nullptr;
    Control(void* window, uint32_t iid) : found(Yes(Virtual<uint32_t>(window, 0, iid, &object))) {}
    ~Control()
    {
        if (object)
            Virtual<uint32_t>(object, 8);
    }
    bool found;
};

void* Singleton913C1A() { return reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C1A)(); }
void* Singleton913C72() { return reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C72)(); }

// A colour from three numbers: the framework singleton 0x00913C1A's slot 34.
uint32_t Color(uint32_t first, uint32_t second, uint32_t third)
{
    return Virtual<uint32_t>(Singleton913C1A(), 0x88, first, second, third);
}

// A font from a number: the singleton 0x00913C72's slot 5.
uint32_t Font(uint32_t number) { return Virtual<uint32_t>(Singleton913C72(), 0x14, number); }

// The window's slot 114 with a new value made by a constructor of
// SC4ScriptNumber (none without memory).
constexpr uint32_t kShownKey = 0xDACF8F9C;

void PutBool(void* window, uint32_t key, uint32_t value)
{
    void* const memory = NewValue();
    void* const made = memory ? CallThis<void*>(addr::SC4ScriptNumber_FromBool, memory, value) : nullptr;
    Virtual<uint32_t>(window, kWindowPut, key, made);
}

void PutBytes(void* window, uint32_t key, const void* data)
{
    void* const memory = NewValue();
    void* const made = memory ? CallThis<void*>(addr::SC4ScriptNumber_FromBytes, memory, data, 8u, kBytesKind) : nullptr;
    Virtual<uint32_t>(window, kWindowPut, key, made);
}

// The 0x304 the handlers end their first pass with (0x0094E516 both): whether the key is
// there, as a boolean value under 0xDACF8F9C; true when it is.
bool PutShown(void* window, const Props& props)
{
    const bool shown = props.Has(0x304);
    PutBool(window, kShownKey, shown ? 1u : 0u);
    return shown;
}

// The resource key the image and text properties are written back as: a
// type, then the two words of "{%x,%x}".
constexpr uint32_t kImageKeyType = 0x856DDBAC;
constexpr uint32_t kTextKeyType = 0x2026960B;

uint32_t ScanKey(const Text& text, uint32_t* first, uint32_t* second)
{
    return static_cast<uint32_t>(Scan(text.Begin(), addr::cGZUIScriptProps_KeyFormat, first, second));
}

void* LoadImageOf(uint32_t group, uint32_t instance)
{
    uint32_t argument = instance;
    return AsPointer(LoadImageBody(group, &argument));
}

void MakeKeyOf(void* window, uint32_t key, const uint32_t* data) { MakeKeyBody(0, window, key, data); }

// The game's 0x0094E33B (interface 0xC2AFA76F): on the second pass only.
// The flags byte (the window argument's top byte) from every 0xF005 number
// pair (slot 73's word: 0xF006 1, 0x204 2, 0x205 8, 0x206 0xF, 0x207 0xD,
// 0x208 0xE, 0xF007 4, 0xF00B 0x20, 0xF00C 0x10, 0xF00E 0x40) to the
// control's slot 12; then the colours 0x200 to 0x203 by index (slot 10:
// the index, the three bytes' dwords).
void __cdecl HandleC2AFA76FBody(uint32_t* args)
{
    OPENSC4_FIRST("HandleC2AFA76F");
    Control control(AsPointer(args[0]), 0xC2AFA76F);
    if (!control.found || !static_cast<uint8_t>(args[2]))
        return;
    char* const* const vector = reinterpret_cast<char* const*>(args[1]);
    uint32_t f8 = 0, f4 = 0;
    uint8_t& flags = reinterpret_cast<uint8_t*>(&args[0])[3];
    flags = 0;
    const uint32_t count = PairCount(vector);
    for (; f8 < count; ++f8) {
        char* const pair = vector[0] + f8 * 8;
        if (At<uint32_t>(pair, 0) != 0xF005)
            continue;
        void* const value = At<void*>(pair, 4);
        if (!value || Virtual<uint32_t>(value, 0xC) != kNumberKind)
            continue;
        switch (*Virtual<const uint32_t*>(At<void*>(vector[0] + f8 * 8, 4), 0x124)) {
        case 0xF006: flags |= 0x01; break;
        case 0x204: flags |= 0x02; break;
        case 0x205: flags |= 0x08; break;
        case 0x206: flags |= 0x0F; break;
        case 0x207: flags |= 0x0D; break;
        case 0x208: flags |= 0x0E; break;
        case 0xF007: flags |= 0x04; break;
        case 0xF00B: flags |= 0x20; break;
        case 0xF00C: flags |= 0x10; break;
        case 0xF00E: flags |= 0x40; break;
        default: break;
        }
    }
    Virtual<uint32_t>(control.object, 0x30, static_cast<uint32_t>(flags));
    const Props props(args[1]);
    for (uint32_t index = 0; index < 4; ++index) {
        if (props.Bytes(0x200 + index, &f8, &f4, &args[0]))
            Virtual<uint32_t>(control.object, 0x28, index, f8, f4, args[0]);
    }
}

// The game's 0x0094E516 (interface 0x212CDC1F, the text control). First
// pass: the 0xF015 alignment (0xF008 as 4) to slot 21 (the window
// argument the out). Second pass: the 0xF003, 0x300 and 0x301 flags (1, 2
// and 4) to slot 7, with 0x301 off the window's slot 68 (0x20000, 1); the
// 0xF000 font number to slot 19 and back as a number value (else its text
// back as a text value); the 0xF002 and 0xF001 colours to slot 9 (0 and
// 1); the 0xF004 gutters to slot 23 (0, 0 without); the 0x305 pair to slot
// 25. Both passes end with 0x304 (slot 5 (0, 0) when there).
void __cdecl Handle212CDC1FBody(uint32_t* args)
{
    OPENSC4_FIRST("Handle212CDC1F");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x212CDC1F);
    if (!control.found)
        return;
    Props props(args[1]);
    if (static_cast<uint8_t>(args[2])) {
        uint32_t f8 = 0, fC = 0, f10 = 0, f14 = 0, f18 = 0;
        if (props.Word(0xF003, &f8))
            Virtual<uint32_t>(control.object, 0x1C, 1u, props.Dl(f8 != 0));
        if (props.Word(0x300, &f8))
            Virtual<uint32_t>(control.object, 0x1C, 2u, props.Dl(f8 != 0));
        if (props.Word(0x301, &f8)) {
            Virtual<uint32_t>(control.object, 0x1C, 4u, props.Dl(f8 != 0));
            if (f8 == 0)
                Virtual<uint32_t>(window, 0x110, 0x20000u, 1u);
        }
        {
            Text f2C;
            if (props.Word(0xF000, &f10)) {
                Virtual<uint32_t>(control.object, 0x4C, f10);
                MakeNumberBody(window, f10, 0);
            } else if (props.Find(f2C, 0xF000)) {
                MakeTextBody(window, f2C.bytes, 0);
            }
            if (props.Bytes(0xF002, &f10, &fC, &f8))
                Virtual<uint32_t>(control.object, 0x24, 0u, Color(f10, fC, f8));
            if (props.Bytes(0xF001, &f10, &fC, &f8))
                Virtual<uint32_t>(control.object, 0x24, 1u, Color(f10, fC, f8));
            if (props.Pair(0xF004, &f18, &f14))
                Virtual<uint32_t>(control.object, 0x5C, f18, f14);
            else
                Virtual<uint32_t>(control.object, 0x5C, 0u, 0u);
            if (props.Pair(0x305, &f18, &f14))
                Virtual<uint32_t>(control.object, 0x64, f18, f14);
        }
    } else if (props.Word(0xF015, &args[0])) {
        Virtual<uint32_t>(control.object, 0x54, args[0] == 0xF008 ? 4u : args[0]);
    }
    if (PutShown(window, props))
        Virtual<uint32_t>(control.object, 0x14, 0u, 0u);
}

// The image properties of the text edit and the others: "{%x,%x}" under
// the key; with the image (0x0094C4E0) the rectangle under the rectangle
// key (all four read, else the zeros of another) to the control's slot;
// then the key written back by 0x0094C1A4 (the type 0x856DDBAC and the two
// words) and 0x0094C1DA (the two words); the image released. Each handler
// lays it out a little differently; see each.

// The game's 0x0094E7C8 (interface 0x231A1C57, the text edit). First
// pass: 0x304 (slot 43 when there). Second pass: the flags from slot 80
// read through 0x0094C719 (the table 0x00AD56C8) to slot 77; the 0xF000
// font number to slot 10 and back (else its text back); the colours
// 0xF00F to 0xF014 to slot 12 (by index 0, 2, 4, 1, 3, 5), 0x407 to slot
// 18 and 0x408 to slot 23; 0x40B's colour to slot 37 with the 0xF019 flag
// (SETNE AL over the colour); 0x409 to slot 33; the 0xF004 pair to slots
// 40 and 42; 0x40C (off) to slot 64, 0x40D to slot 66, the 0x40E pair to
// slot 68, 0x40F to slot 72, 0x410 to slot 81; the images 0x413 (rect
// 0x414, slot 88, key 0x8B4BFEFC, pair key 0) and 0x411 (rect 0x412, slot
// 87, 0x8B4BFEFD, 1).
void __cdecl Handle231A1C57Body(uint32_t* args)
{
    OPENSC4_FIRST("Handle231A1C57");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x231A1C57);
    if (!control.found)
        return;
    Props props(args[1]);
    if (!static_cast<uint8_t>(args[2])) {
        if (PutShown(window, props))
            Virtual<uint32_t>(control.object, 0xAC);
        return;
    }
    uint32_t flagsArgs[2] = {args[1], Virtual<uint32_t>(control.object, 0x140)};
    Virtual<uint32_t>(control.object, 0x134,
                      ReadFlagsBody(reinterpret_cast<const uint32_t*>(addr::cGZUIScriptProps_EditFlags), flagsArgs));
    uint32_t f8 = 0, fC = 0, f10 = 0, f14 = 0, f18 = 0, f1C = 0;
    Text f64;
    if (props.Word(0xF000, &f10)) {
        Virtual<uint32_t>(control.object, 0x28, f10);
        MakeNumberBody(window, f10, 0);
    } else if (props.Find(f64, 0xF000)) {
        MakeTextBody(window, f64.bytes, 0);
    }
    const uint32_t indexes[] = {0, 2, 4, 1, 3, 5};
    for (uint32_t i = 0; i < 6; ++i) {
        if (props.Bytes(0xF00F + i, &f10, &fC, &f8))
            Virtual<uint32_t>(control.object, 0x30, indexes[i], Color(f10, fC, f8));
    }
    if (props.Bytes(0x407, &f10, &fC, &f8))
        Virtual<uint32_t>(control.object, 0x48, Color(f10, fC, f8));
    if (props.Bytes(0x408, &f10, &fC, &f8))
        Virtual<uint32_t>(control.object, 0x5C, Color(f10, fC, f8));
    if (props.Bytes(0x40B, &f10, &fC, &f8)) {
        props.Word(0xF019, &f1C);
        const uint32_t color = Color(f10, fC, f8);
        Virtual<uint32_t>(control.object, 0x94, (color & ~0xFFu) | (f1C != 0 ? 1u : 0u), color);
    }
    if (props.Word(0x409, &f1C))
        Virtual<uint32_t>(control.object, 0x84, f1C);
    if (props.Pair(0xF004, &f18, &f14)) {
        Virtual<uint32_t>(control.object, 0xA0, f18);
        Virtual<uint32_t>(control.object, 0xA8, f14);
    }
    f10 = 0;
    if (props.Word(0x40C, &f10))
        Virtual<uint32_t>(control.object, 0x100, props.Dl(f10 == 0));
    if (props.Word(0x40D, &f1C))
        Virtual<uint32_t>(control.object, 0x108, f1C);
    if (props.Pair(0x40E, &f18, &f14))
        Virtual<uint32_t>(control.object, 0x110, f18, f14);
    if (props.Word(0x40F, &f10))
        Virtual<uint32_t>(control.object, 0x120, f10);
    if (props.Word(0x410, &f10))
        Virtual<uint32_t>(control.object, 0x144, f10);
    Text f50;
    struct Image {
        uint32_t key, rectKey, slot, writeKey, pairKey;
    };
    const Image images[] = {{0x413, 0x414, 0x160, 0x8B4BFEFC, 0}, {0x411, 0x412, 0x15C, 0x8B4BFEFD, 1}};
    for (const Image& image : images) {
        if (!props.Find(f50, image.key) || ScanKey(f50, &fC, &f10) != 2)
            continue;
        void* const loaded = LoadImageOf(fC, f10);
        int32_t f3C[4] = {};
        uint32_t f2C[4] = {};
        if (loaded) {
            void* rect = f3C;
            if (!props.Rect(image.rectKey, f3C)) {
                f2C[0] = f2C[1] = f2C[2] = f2C[3] = 0;
                rect = f2C;
            }
            Virtual<uint32_t>(control.object, image.slot, loaded, rect);
        }
        const uint32_t f28[3] = {kImageKeyType, fC, f10};
        MakeKeyOf(window, image.writeKey, f28);
        MakePairBody(fC, window, f10, image.pairKey);
        if (loaded)
            Virtual<uint32_t>(loaded, 8);
    }
}

// The game's 0x0094EE39 (interface 0x4132242B). First pass: every 0x502
// pair whose value is a text (slot 3 0x800E; the value is not checked set)
// to slot 7 (the text, -2, 0); 0x503 at 1 to slots 14 and 16; 0x501 to
// slot 10 (the word, 1); 0x505 to slot 27, 0x506 to slot 29; 0x304 (slots
// 9, 47 and 48 when there). The outs are the window argument. Second pass:
// 0x402 (default on) and 0x401 (default off) at 1, as the low bytes of two
// dwords, to slot 35; the 0xF000 font number through 0x00913C72's slot 5
// to slot 38 and back (else its text back); the colours 0xF00F, 0xF010 and
// 0xF011 to slot 39 (0, 1, 2) and 0x408 to slot 42; 0xF019 at 1 to slot
// 51; 0x40B's "(%d,%d,%d)(%d,%d,%d)" as two colours to slot 55 (the first
// triple's colour, then the second's: the second is made first); 0xF00B
// at 1 to slot 53; the 0x500 pair to slot 57 and 0xF004 to slot 59; 0x504
// at 1 to slot 17.
void __cdecl Handle4132242BBody(uint32_t* args)
{
    OPENSC4_FIRST("Handle4132242B");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x4132242B);
    if (!control.found)
        return;
    Props props(args[1]);
    if (!static_cast<uint8_t>(args[2])) {
        const uint32_t count = PairCount(props.vector);
        for (uint32_t f10 = 0; f10 < count; ++f10) {
            char* const pair = props.vector[0] + f10 * 8;
            if (At<uint32_t>(pair, 0) != 0x502 || Virtual<uint32_t>(At<void*>(pair, 4), 0xC) != kTextKind)
                continue;
            Text f50;
            Virtual<uint32_t>(At<void*>(props.vector[0] + f10 * 8, 4), 0xDC, static_cast<void*>(f50.bytes));
            Virtual<uint32_t>(control.object, 0x1C, static_cast<void*>(f50.bytes), 0xFFFFFFFEu, 0u);
        }
        if (props.Word(0x503, &args[0]) && args[0] == 1) {
            Virtual<uint32_t>(control.object, 0x38);
            Virtual<uint32_t>(control.object, 0x40);
        }
        if (props.Word(0x501, &args[0]))
            Virtual<uint32_t>(control.object, 0x28, args[0], 1u);
        if (props.Word(0x505, &args[0]))
            Virtual<uint32_t>(control.object, 0x6C, args[0]);
        if (props.Word(0x506, &args[0]))
            Virtual<uint32_t>(control.object, 0x74, args[0]);
        if (PutShown(window, props)) {
            Virtual<uint32_t>(control.object, 0x24);
            Virtual<uint32_t>(control.object, 0xBC);
            Virtual<uint32_t>(control.object, 0xC0);
        }
        return;
    }
    args[0] = 0;
    uint32_t f8 = 0, fC = 1, f10 = 0, f14 = 0, f18 = 0, f1C = 0, f20 = 0, f24 = 0;
    if (props.Word(0x402, &args[0]))
        fC = (fC & ~0xFFu) | (args[0] == 1 ? 1u : 0u);
    if (props.Word(0x401, &args[0]))
        f10 = (f10 & ~0xFFu) | (args[0] == 1 ? 1u : 0u);
    Virtual<uint32_t>(control.object, 0x8C, fC, f10);
    Text f50;
    if (props.Word(0xF000, &f10)) {
        Virtual<uint32_t>(control.object, 0x98, Font(f10));
        MakeNumberBody(window, f10, 0);
    } else if (props.Find(f50, 0xF000)) {
        MakeTextBody(window, f50.bytes, 0);
    }
    for (uint32_t index = 0; index < 3; ++index) {
        if (props.Bytes(0xF00F + index, &f10, &fC, &f8))
            Virtual<uint32_t>(control.object, 0x9C, index, Color(f10, fC, f8));
    }
    if (props.Bytes(0x408, &f10, &fC, &f8))
        Virtual<uint32_t>(control.object, 0xA8, Color(f10, fC, f8));
    if (props.Word(0xF019, &args[0]))
        Virtual<uint32_t>(control.object, 0xCC, props.Dl(args[0] == 1));
    Text f3C;
    if (props.Find(f3C, 0x40B) &&
        Scan(f3C.Begin(), addr::cGZUIScriptProps_ColorsFormat, &f24, &f20, &f1C, &f8, &fC, &f10) == 6) {
        void* const colors = Singleton913C1A();
        void* const colors2 = Singleton913C1A();
        const uint32_t second = Virtual<uint32_t>(colors, 0x88, f8, fC, f10);
        const uint32_t first = Virtual<uint32_t>(colors2, 0x88, f24, f20, f1C);
        Virtual<uint32_t>(control.object, 0xDC, first, second);
    }
    if (props.Word(0xF00B, &args[0]))
        Virtual<uint32_t>(control.object, 0xD4, props.Dl(args[0] == 1));
    if (props.Pair(0x500, &f18, &f14))
        Virtual<uint32_t>(control.object, 0xE4, f18, f14);
    if (props.Pair(0xF004, &f18, &f14))
        Virtual<uint32_t>(control.object, 0xEC, f18, f14);
    if (props.Word(0x504, &args[0]))
        Virtual<uint32_t>(control.object, 0x44, props.Dl(args[0] == 1));
}

// The game's 0x0094F757 (interface 0x8810, the button). First pass: 0xA02
// to slot 5 (4; the window argument the out). Second pass: the flags 0xA00
// 1, 0xA01 2, 0xA02 4, 0xF00D 0x10 (off), 0xA03 0x20, 0xA04 0x40, 0xA05
// 0x80, 0xA06 0x100, 0xA07 0x200 (off) and 0xA08 0x400 (off) to slot 5;
// the 0xF005 style as a kind (0xA00, 0xA0C, 0xA0D 3; 0xA09 1; 0xA0A, 0xA0B
// 2; 0xA0E, 0xA0F, 0xA1B 4; else 0); the 0xF000 font number to slot 20 and
// back (else its text back, else the default font to slot 19); the
// colours 0xF00F to 0xF014 to slot 23 (by index 0, 4, 2, 1, 5, 3); the
// 0xF004 gutters ("(%d,%d)" to slot 30 or "(%d,%d,%d,%d)" to slot 29, by
// the text's commas); the 0xF016 image (unchecked "{%x,%x}"; its area,
// slot 12's 16 bytes, copied) with the kind to slot 12, written back under
// 0x2B343BEC (none: slot 12 with the kind, 0, 0); the 0xF015 alignment
// (0xF006 1, 0xF007 2, 0xF008 0) to slot 27; the 0xA10 text to slot 35;
// 0xA12's text by key (0x0094C58D) to slot 35, the key written back under
// 0x2B340FFE; the 0xA11 pair to slot 40; 0xA18 (else 0x1000000) to slot
// 38; 0xA19 and 0xA1A's keys as 8 bytes under 0x0A5BE39D and 0x4A5BE2F8
// and their second words to slot 45 (0 and 1; 0 without).
void __cdecl Handle8810Body(uint32_t* args)
{
    OPENSC4_FIRST("Handle8810");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x8810);
    if (!control.found)
        return;
    Props props(args[1]);
    if (!static_cast<uint8_t>(args[2])) {
        if (props.Word(0xA02, &args[0]))
            Virtual<uint32_t>(control.object, 0x14, 4u, props.Dl(args[0] != 0));
        return;
    }
    uint32_t f64 = 0, f60 = 0, f58 = 0, f54 = 0, f4C = 0, f24 = 0, f20 = 0;
    struct Flag {
        uint32_t key, mask;
        bool off;
    };
    const Flag flags[] = {{0xA00, 0x1, false},   {0xA01, 0x2, false},   {0xA02, 0x4, false},  {0xF00D, 0x10, true},
                          {0xA03, 0x20, false},  {0xA04, 0x40, false},  {0xA05, 0x80, false}, {0xA06, 0x100, false},
                          {0xA07, 0x200, true},  {0xA08, 0x400, true}};
    for (const Flag& flag : flags) {
        if (props.Word(flag.key, &f64))
            Virtual<uint32_t>(control.object, 0x14, flag.mask, props.Dl(flag.off ? f64 == 0 : f64 != 0));
    }
    f64 = 0;
    if (props.Word(0xF005, &f60)) {
        switch (f60) {
        case 0xA00:
        case 0xA0C:
        case 0xA0D: f64 = 3; break;
        case 0xA09: f64 = 1; break;
        case 0xA0A:
        case 0xA0B: f64 = 2; break;
        case 0xA0E:
        case 0xA0F:
        case 0xA1B: f64 = 4; break;
        default: break;
        }
    }
    Text m44;
    if (props.Word(0xF000, &f60)) {
        Virtual<uint32_t>(control.object, 0x50, f60);
        MakeNumberBody(window, f60, 0);
    } else if (props.Find(m44, 0xF000)) {
        MakeTextBody(window, m44.bytes, 0);
    } else {
        Text name("main9");
        const uint32_t font = Virtual<uint32_t>(Singleton913C72(), 0x98, static_cast<void*>(name.bytes));
        Virtual<uint32_t>(control.object, 0x4C, Font(font));
    }
    const uint32_t indexes[] = {0, 4, 2, 1, 5, 3};
    for (uint32_t i = 0; i < 6; ++i) {
        if (props.Bytes(0xF00F + i, &f60, &f54, &f58))
            Virtual<uint32_t>(control.object, 0x5C, indexes[i], Color(f60, f54, f58));
    }
    Text p0C;
    if (props.Find(p0C, 0xF004)) {
        const uint32_t length = CallThis<uint32_t>(addr::cRZBaseString_Strlen, p0C.bytes);
        if (length) {
            const char* const text = p0C.Begin();
            uint32_t commas = 0;
            for (uint32_t i = 0; i < length; ++i) {
                if (text[i] == ',')
                    ++commas;
            }
            if (commas == 1) {
                Scan(text, addr::cGZUIScriptProps_PairFormat, &f54, &f60);
                Virtual<uint32_t>(control.object, 0x78, f54, f60);
            } else if (commas == 3) {
                Scan(text, addr::cGZUIScriptProps_RectFormat, &f4C, &f58, &f54, &f60);
                Virtual<uint32_t>(control.object, 0x74, f4C, f58, f54, f60);
            }
        }
    }
    if (props.Find(p0C, 0xF016)) {
        ScanKey(p0C, &f54, &f60);
        void* const loaded = LoadImageOf(f54, f60);
        uint32_t p3C[4] = {};
        if (loaded) {
            const uint32_t* const area = Virtual<const uint32_t*>(loaded, 0x30);
            for (uint32_t i = 0; i < 4; ++i)
                p3C[i] = area[i];
            Virtual<uint32_t>(control.object, 0x30, f64, loaded, static_cast<void*>(p3C));
        } else {
            Virtual<uint32_t>(control.object, 0x30, f64, 0u, 0u);
        }
        const uint32_t p40[3] = {kImageKeyType, f54, f60};
        MakeKeyOf(window, 0x2B343BEC, p40);
        MakePairBody(f54, window, f60, 0);
        if (loaded)
            Virtual<uint32_t>(loaded, 8);
    } else {
        Virtual<uint32_t>(control.object, 0x30, f64, 0u, 0u);
    }
    if (props.Word(0xF015, &f4C)) {
        switch (f4C) {
        case 0xF006: Virtual<uint32_t>(control.object, 0x6C, 1u); break;
        case 0xF007: Virtual<uint32_t>(control.object, 0x6C, 2u); break;
        case 0xF008: Virtual<uint32_t>(control.object, 0x6C, 0u); break;
        default: break;
        }
    }
    Text m30;
    if (props.Find(m30, 0xA10))
        Virtual<uint32_t>(control.object, 0x8C, static_cast<void*>(m30.bytes));
    Text m1C;
    if (props.Find(m1C, 0xA12)) {
        ScanKey(m1C, &f60, &f54);
        {
            Text p28;
            uint32_t instance = f54;
            if (Yes(LoadTextBody(f60, p28.bytes, &instance)))
                Virtual<uint32_t>(control.object, 0x8C, static_cast<void*>(p28.bytes));
            const uint32_t p40[3] = {kTextKeyType, f60, f54};
            MakeKeyOf(window, 0x2B340FFE, p40);
        }
    }
    if (props.Pair(0xA11, &f24, &f20))
        Virtual<uint32_t>(control.object, 0xA0, f24, f20);
    Virtual<uint32_t>(control.object, 0x98, props.Word(0xA18, &f4C) ? f4C : 0x1000000u);
    Text m08;
    if (props.Find(m08, 0xA19)) {
        ScanKey(m08, &f4C, &f60);
        const uint32_t p50[2] = {f4C, f60};
        f54 = f60;
        PutBytes(window, 0x0A5BE39D, p50);
        Virtual<uint32_t>(control.object, 0xB4, 0u, f60);
    } else {
        Virtual<uint32_t>(control.object, 0xB4, 0u, 0u);
    }
    if (props.Find(m08, 0xA1A)) {
        ScanKey(m08, &f4C, &args[0]);
        const uint32_t p5C[2] = {f4C, args[0]};
        PutBytes(window, 0x4A5BE2F8, p5C);
        Virtual<uint32_t>(control.object, 0xB4, 1u, args[0]);
    } else {
        Virtual<uint32_t>(control.object, 0xB4, 1u, 0u);
    }
}

// The game's 0x0095002C (interface 0xC12CEA13). First pass: the 0xF018
// rectangle to slot 8 (read into ESI's place), then slot 11 (0x10, 1; 0
// without). Second pass: 0xB00 (above 0xFF as 0xFF) times 1/255 as a
// float to slot 6 (1, the float); 0xF003, 0xB01 and 0xB02 (the out zeroed
// before the last two) to slot 11 (1, 2, 8); the 0xF016 image (unchecked
// "{%x,%x}") to slot 4 and its width and height (slots 9 and 10) to the
// window's slot 53, written back under 0xAB428C5D. The window argument is
// the outs'.
void __cdecl HandleC12CEA13Body(uint32_t* args)
{
    OPENSC4_FIRST("HandleC12CEA13");
    void* const window = AsPointer(args[0]);
    Control control(window, 0xC12CEA13);
    if (!control.found)
        return;
    Props props(args[1]);
    if (!static_cast<uint8_t>(args[2])) {
        int32_t f18[4] = {};
        if (props.Rect(0xF018, f18)) {
            Virtual<uint32_t>(control.object, 0x20, static_cast<void*>(f18));
            Virtual<uint32_t>(control.object, 0x2C, 0x10u, 1u);
        } else {
            Virtual<uint32_t>(control.object, 0x2C, 0x10u, 0u);
        }
        return;
    }
    if (props.Word(0xB00, &args[0])) {
        // The game's FILD of a word of at most 0xFF never takes its
        // negative branch (+2^32).
        args[0] = args[0] > 0xFF ? 0xFFu : args[0];
        const float alpha = static_cast<float>(static_cast<double>(static_cast<int32_t>(args[0])) *
                                               static_cast<double>(Global<float>(addr::SC4_FloatInv255)));
        Virtual<uint32_t>(control.object, 0x18, 1u, alpha);
    }
    if (props.Word(0xF003, &args[0]))
        Virtual<uint32_t>(control.object, 0x2C, 1u, props.Dl(args[0] != 0));
    args[0] = 0;
    if (props.Word(0xB01, &args[0]))
        Virtual<uint32_t>(control.object, 0x2C, 2u, props.Dl(args[0] != 0));
    args[0] = 0;
    if (props.Word(0xB02, &args[0]))
        Virtual<uint32_t>(control.object, 0x2C, 8u, props.Dl(args[0] != 0));
    Text f2C;
    if (props.Find(f2C, 0xF016)) {
        uint32_t f8 = 0;
        ScanKey(f2C, &f8, &args[0]);
        void* const loaded = LoadImageOf(f8, args[0]);
        if (loaded) {
            Virtual<uint32_t>(control.object, 0x10, loaded);
            const uint32_t height = Virtual<uint32_t>(loaded, 0x28);
            const uint32_t width = Virtual<uint32_t>(loaded, 0x24);
            Virtual<uint32_t>(window, 0xD4, width, height);
        }
        const uint32_t f14[3] = {kImageKeyType, f8, args[0]};
        MakeKeyOf(window, 0xAB428C5D, f14);
        MakePairBody(f8, window, args[0], 0);
        if (loaded)
            Virtual<uint32_t>(loaded, 8);
    }
}

// The game's 0x00950218 (interface 0x21325207). First pass: 0x40A through
// slot 75 to slot 5 (the window argument the out). Second pass: the 0xC00
// pair, the larger to slot 8 and the smaller to slot 6; 0xC01 (0xC03 as 2,
// else 1) to slot 10; the 0xF016 image (both words read) with the 0xF018
// rectangle (else none) to slot 12, written back under 0x6B4BCBD4.
void __cdecl Handle21325207Body(uint32_t* args)
{
    OPENSC4_FIRST("Handle21325207");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x21325207);
    if (!control.found)
        return;
    Props props(args[1]);
    if (!static_cast<uint8_t>(args[2])) {
        if (props.Word75(0x40A, &args[0]))
            Virtual<uint32_t>(control.object, 0x14, args[0]);
        return;
    }
    uint32_t f8 = 0, fC = 0, f10 = 0, f14 = 0;
    if (props.Pair(0xC00, &fC, &f8)) {
        if (static_cast<int32_t>(fC) > static_cast<int32_t>(f8)) {
            const uint32_t larger = fC;
            fC = f8;
            f8 = larger;
        }
        Virtual<uint32_t>(control.object, 0x20, f8);
        Virtual<uint32_t>(control.object, 0x18, fC);
    }
    if (props.Word(0xC01, &f14))
        Virtual<uint32_t>(control.object, 0x28, f14 == 0xC03 ? 2u : 1u);
    Text f44;
    if (props.Find(f44, 0xF016) && ScanKey(f44, &f14, &f10) == 2) {
        void* const loaded = LoadImageOf(f14, f10);
        if (loaded) {
            int32_t f30[4] = {};
            const bool read = props.Rect(0xF018, f30);
            Virtual<uint32_t>(control.object, 0x30, loaded, read ? static_cast<void*>(f30) : nullptr);
        }
        const uint32_t f20[3] = {kImageKeyType, f14, f10};
        MakeKeyOf(window, 0x6B4BCBD4, f20);
        MakePairBody(f14, window, f10, 0);
        if (loaded)
            Virtual<uint32_t>(loaded, 8);
    }
}

// The game's 0x00950AF6 (interface 0x612CE0C3). First pass: the 0x1004
// pair to slot 10 (the second into the window argument); 0x1005 and 0x40A
// through slot 75 to slots 12 and 9. Second pass: 0x1000 (0 0, 0xF006 1,
// 0xF007 2) to slot 4; the flags 0xF019 1, 0xF00D 2, 0x1001 0x1000, 0x1002
// 0x2000 and 0x1003 0x4000 to slot 6; the 0xF000 font number through
// 0x00913C72's slot 5 to slot 19 and back (else its text back, else the
// default font to slot 19; that path leaves the control's vtable in its
// f18); the colours 0xF00F, 0xF010 and 0xF011 to slot 23 (0, 2, 1); the
// 0xF016 image (unchecked "{%x,%x}") to slot 14 (the image, 0), written
// back under 0x8BD1333A; the 0xF015 alignment (0xF006 0, 0xF007 2, 0xF008
// 1) to slot 27; the 0xF004 pair to slot 29; the colours 0x1006 (all four
// places) and 0x1007 to 0x100A (one each), with any of them the four to
// slot 31.
void __cdecl Handle612CE0C3Body(uint32_t* args)
{
    OPENSC4_FIRST("Handle612CE0C3");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x612CE0C3);
    if (!control.found)
        return;
    Props props(args[1]);
    uint32_t f8 = 0, fC = 0, f10 = 0, f14 = 0, f18 = 0, f1C = 0, f20 = 0;
    if (!static_cast<uint8_t>(args[2])) {
        if (props.Pair(0x1004, &f20, &args[0]))
            Virtual<uint32_t>(control.object, 0x28, f20, args[0]);
        if (props.Word75(0x1005, &f1C))
            Virtual<uint32_t>(control.object, 0x30, f1C);
        if (props.Word75(0x40A, &f1C))
            Virtual<uint32_t>(control.object, 0x24, f1C);
        return;
    }
    if (props.Word(0x1000, &f18)) {
        switch (f18) {
        case 0: Virtual<uint32_t>(control.object, 0x10, 0u); break;
        case 0xF006: Virtual<uint32_t>(control.object, 0x10, 1u); break;
        case 0xF007: Virtual<uint32_t>(control.object, 0x10, 2u); break;
        default: break;
        }
    }
    const uint32_t flags[][2] = {{0xF019, 1}, {0xF00D, 2}, {0x1001, 0x1000}, {0x1002, 0x2000}, {0x1003, 0x4000}};
    for (const auto& flag : flags) {
        if (props.Word(flag[0], &f10))
            Virtual<uint32_t>(control.object, 0x18, flag[1], props.Dl(f10 != 0));
    }
    Text f5C;
    if (props.Word(0xF000, &f18)) {
        Virtual<uint32_t>(control.object, 0x4C, Font(f18));
        MakeNumberBody(window, f18, 0);
    } else if (props.Find(f5C, 0xF000)) {
        MakeTextBody(window, f5C.bytes, 0);
    } else {
        Text name("main9");
        const uint32_t font = Virtual<uint32_t>(Singleton913C72(), 0x98, static_cast<void*>(name.bytes));
        void* const singleton = Singleton913C72();
        f18 = AsWord(*static_cast<void**>(control.object));
        Virtual<uint32_t>(control.object, 0x4C, Virtual<uint32_t>(singleton, 0x14, font));
    }
    const uint32_t indexes[] = {0, 2, 1};
    for (uint32_t i = 0; i < 3; ++i) {
        if (props.Bytes(0xF00F + i, &f10, &fC, &f8))
            Virtual<uint32_t>(control.object, 0x5C, indexes[i], Color(f10, fC, f8));
    }
    {
        Text f48;
        if (props.Find(f48, 0xF016)) {
            ScanKey(f48, &f18, &f14);
            void* const loaded = LoadImageOf(f18, f14);
            if (loaded)
                Virtual<uint32_t>(control.object, 0x38, loaded, 0u);
            const uint32_t f2C[3] = {kImageKeyType, f18, f14};
            MakeKeyOf(window, 0x8BD1333A, f2C);
            MakePairBody(f18, window, f14, 0);
            if (loaded)
                Virtual<uint32_t>(loaded, 8);
        }
        if (props.Word(0xF015, &args[0])) {
            switch (args[0]) {
            case 0xF006: Virtual<uint32_t>(control.object, 0x6C, 0u); break;
            case 0xF007: Virtual<uint32_t>(control.object, 0x6C, 2u); break;
            case 0xF008: Virtual<uint32_t>(control.object, 0x6C, 1u); break;
            default: break;
            }
        }
        if (props.Pair(0xF004, &f20, &f1C))
            Virtual<uint32_t>(control.object, 0x74, f20, f1C);
        uint32_t esi = 0;
        bool any = false;
        f14 = f18 = args[0] = 0;
        if (props.Bytes(0x1006, &f10, &fC, &f8)) {
            esi = args[0] = f18 = f14 = Color(f10, fC, f8);
            any = true;
        }
        if (props.Bytes(0x1007, &f10, &fC, &f8)) {
            f14 = Color(f10, fC, f8);
            any = true;
        }
        if (props.Bytes(0x1008, &f10, &fC, &f8)) {
            f18 = Color(f10, fC, f8);
            any = true;
        }
        if (props.Bytes(0x1009, &f10, &fC, &f8)) {
            args[0] = Color(f10, fC, f8);
            any = true;
        }
        if (props.Bytes(0x100A, &f10, &fC, &f8)) {
            esi = Color(f10, fC, f8);
            any = true;
        }
        if (any)
            Virtual<uint32_t>(control.object, 0x7C, f14, f18, args[0], esi);
    }
}

// ====================================================================================
// The property handlers that never run (part u12, slice 4): those of six
// stock widgets no shipped UI script makes. Laid out as the eight above.

// A colour from a text read as "(%d,%d,%d)", the count not checked (the
// three ints are the caller's, left as they were where the text has fewer).
uint32_t ScanColor(const Text& text, uint32_t* first, uint32_t* second, uint32_t* third)
{
    Scan(text.Begin(), addr::cGZUIScriptProps_BytesFormat, first, second, third);
    return Color(*first, *second, *third);
}

// The game's 0x0094CE28 (interface 0x032F0B3C, GZWinTextTicker): on the
// second pass only. 0x1200 and 0x1201 to slots 8 and 9; the 0xF000 font
// number through 0x00913C72's slot 5 to slot 7 and back (else its text
// back, else the default font to slot 7); then the pairs in order: each
// 0xF002 or 0xF001 text (the value checked set) a colour, unchecked
// "(%d,%d,%d)" (the defaults 0xFFFFFFFF and 0), and each 0x1202 text put
// into a new game string (from SC4::operator_new, unchecked; its slot 1
// before, slot 2 after) given to slot 3 with the two colours so far, 0 and
// 0. The window argument ends as the loop's index (the pair count).
void __cdecl Handle032F0B3CBody(uint32_t* args)
{
    OPENSC4_FIRST("Handle032F0B3C");
    Control control(AsPointer(args[0]), 0x032F0B3C);
    if (!control.found || !static_cast<uint8_t>(args[2]))
        return;
    void* const window = AsPointer(args[0]);
    Props props(args[1]);
    uint32_t f8 = 0, fC = 0;
    if (props.Word(0x1200, &fC))
        Virtual<uint32_t>(control.object, 0x20, fC);
    if (props.Word(0x1201, &fC))
        Virtual<uint32_t>(control.object, 0x24, fC);
    Text f64;
    if (props.Word(0xF000, &f8)) {
        Virtual<uint32_t>(control.object, 0x1C, Font(f8));
        MakeNumberBody(window, f8, 0);
    } else if (props.Find(f64, 0xF000)) {
        MakeTextBody(window, f64.bytes, 0);
    } else {
        Text name("main9");
        const uint32_t font = Virtual<uint32_t>(Singleton913C72(), 0x98, static_cast<void*>(name.bytes));
        void* const singleton = Singleton913C72();
        Virtual<uint32_t>(control.object, 0x1C, Virtual<uint32_t>(singleton, 0x14, font));
    }
    f8 = 0;
    fC = 0xFFFFFFFF;
    uint32_t f10 = 0, f14 = 0, f18 = 0, f1C = 0, f20 = 0, f24 = 0;
    const uint32_t count = PairCount(props.vector);
    for (args[0] = 0; args[0] < count; ++args[0]) {
        char* const pair = props.vector[0] + args[0] * 8;
        const uint32_t key = At<uint32_t>(pair, 0);
        if (key != 0x1202 && key != 0xF001 && key != 0xF002)
            continue;
        void* const value = At<void*>(pair, 4);
        if (!value || Virtual<uint32_t>(value, 0xC) != kTextKind)
            continue;
        void* const text = At<void*>(props.vector[0] + args[0] * 8, 4);
        if (key == 0x1202) {
            void* const memory = NewValue();
            void* const string = memory ? CallThis<void*>(addr::SC4String_SC4String, memory) : nullptr;
            Virtual<uint32_t>(string, 4);
            Virtual<uint32_t>(text, 0xDC, string);
            Virtual<uint32_t>(control.object, 0xC, string, fC, f8, 0u, 0u);
            Virtual<uint32_t>(string, 8);
        } else if (key == 0xF001) {
            Text f50;
            Virtual<uint32_t>(text, 0xDC, static_cast<void*>(f50.bytes));
            f8 = ScanColor(f50, &f24, &f20, &f1C);
        } else {
            Text f3C;
            Virtual<uint32_t>(text, 0xDC, static_cast<void*>(f3C.bytes));
            fC = ScanColor(f3C, &f18, &f14, &f10);
        }
    }
}

// The game's 0x0094D17E (interface 0xA13351B8, GZWinFolders). First pass:
// slot 7, then every 0x502 pair whose value is a text (not checked set) to
// slot 4 (the text, 0). Second pass: the 0xF000 font number through
// 0x00913C72's slot 5 to slot 10 and back (else its text back); the 0xF016
// image (unchecked "{%x,%x}", into the window argument and a local) to
// slot 11, its two words as 8 bytes under 0xDAA3B860.
void __cdecl HandleA13351B8Body(uint32_t* args)
{
    OPENSC4_FIRST("HandleA13351B8");
    void* const window = AsPointer(args[0]);
    Control control(window, 0xA13351B8);
    if (!control.found)
        return;
    Props props(args[1]);
    if (!static_cast<uint8_t>(args[2])) {
        Virtual<uint32_t>(control.object, 0x1C);
        Text f38;
        const uint32_t count = PairCount(props.vector);
        for (uint32_t i = 0; i < count; ++i) {
            char* const pair = props.vector[0] + i * 8;
            if (At<uint32_t>(pair, 0) != 0x502 || Virtual<uint32_t>(At<void*>(pair, 4), 0xC) != kTextKind)
                continue;
            Virtual<uint32_t>(At<void*>(props.vector[0] + i * 8, 4), 0xDC, static_cast<void*>(f38.bytes));
            Virtual<uint32_t>(control.object, 0x10, static_cast<void*>(f38.bytes), 0u);
        }
        return;
    }
    Text f38;
    if (props.Word(0xF000, &args[0])) {
        Virtual<uint32_t>(control.object, 0x28, Font(args[0]));
        MakeNumberBody(window, args[0], 0);
    } else if (props.Find(f38, 0xF000)) {
        MakeTextBody(window, f38.bytes, 0);
    }
    Text f24;
    if (props.Find(f24, 0xF016)) {
        uint32_t f8 = 0;
        ScanKey(f24, &args[0], &f8);
        void* const loaded = LoadImageOf(args[0], f8);
        if (loaded)
            Virtual<uint32_t>(control.object, 0x2C, loaded);
        const uint32_t f10[2] = {args[0], f8};
        PutBytes(window, kPairKeyBase, f10);
        if (loaded)
            Virtual<uint32_t>(loaded, 8);
    }
}

// The game's 0x0094F36D (interface 0x412CE496, GZWinCombo). First pass:
// the colours 0xF00F, 0xF010 and 0xF011 to slot 12 (0, 1, 2), 0x408 to
// slot 16, 0x40B to slot 22 and 0x802 to slot 35; 0x400 at 1 to slot 21;
// every 0x502 text (not checked set) to slot 27 (the text, -2); 0x501 to
// slot 28 (the word, 1). The window argument the outs'. Second pass: the
// 0xF000 font number through 0x00913C72's slot 5 to slot 11 and back (else
// its text back); the 0x800 image (both words read) with the 0x801
// rectangle (else none) to slot 33, its words by 0x0094C1DA under 0; 0x803
// to slot 37; the 0xF004 pair to slot 39.
void __cdecl Handle412CE496Body(uint32_t* args)
{
    OPENSC4_FIRST("Handle412CE496");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x412CE496);
    if (!control.found)
        return;
    Props props(args[1]);
    uint32_t f8 = 0, fC = 0, f10 = 0, f14 = 0;
    if (!static_cast<uint8_t>(args[2])) {
        for (uint32_t index = 0; index < 3; ++index) {
            if (props.Bytes(0xF00F + index, &fC, &f8, &args[0]))
                Virtual<uint32_t>(control.object, 0x30, index, Color(fC, f8, args[0]));
        }
        const uint32_t colors[][2] = {{0x408, 0x40}, {0x40B, 0x58}, {0x802, 0x8C}};
        for (const auto& color : colors) {
            if (props.Bytes(color[0], &fC, &f8, &args[0]))
                Virtual<uint32_t>(control.object, color[1], Color(fC, f8, args[0]));
        }
        if (props.Word(0x400, &args[0]))
            Virtual<uint32_t>(control.object, 0x54, props.Dl(args[0] == 1));
        Text f4C;
        const uint32_t count = PairCount(props.vector);
        for (uint32_t i = 0; i < count; ++i) {
            char* const pair = props.vector[0] + i * 8;
            if (At<uint32_t>(pair, 0) != 0x502 || Virtual<uint32_t>(At<void*>(pair, 4), 0xC) != kTextKind)
                continue;
            Virtual<uint32_t>(At<void*>(props.vector[0] + i * 8, 4), 0xDC, static_cast<void*>(f4C.bytes));
            Virtual<uint32_t>(control.object, 0x6C, static_cast<void*>(f4C.bytes), 0xFFFFFFFEu);
        }
        if (props.Word(0x501, &args[0]))
            Virtual<uint32_t>(control.object, 0x70, args[0], 1u);
        return;
    }
    Text f4C;
    if (props.Word(0xF000, &fC)) {
        Virtual<uint32_t>(control.object, 0x2C, Font(fC));
        MakeNumberBody(window, fC, 0);
    } else if (props.Find(f4C, 0xF000)) {
        MakeTextBody(window, f4C.bytes, 0);
    }
    Text f38;
    if (props.Find(f38, 0x800) && ScanKey(f38, &f8, &fC) == 2) {
        void* const loaded = LoadImageOf(f8, fC);
        if (loaded) {
            int32_t f24[4] = {};
            const bool read = props.Rect(0x801, f24);
            Virtual<uint32_t>(control.object, 0x84, loaded, read ? static_cast<void*>(f24) : nullptr);
        }
        MakePairBody(f8, window, fC, 0);
        if (loaded)
            Virtual<uint32_t>(loaded, 8);
    }
    if (props.Word(0x803, &args[0]))
        Virtual<uint32_t>(control.object, 0x94, args[0]);
    if (props.Pair(0xF004, &f14, &f10))
        Virtual<uint32_t>(control.object, 0x9C, f14, f10);
}

// The game's 0x009503AB (interface 0x61325A2D, GZWinScrollbar). First
// pass: 0x40A through slot 75 to slot 5 (the window argument the out).
// Second pass: the 0xC00 pair, the smaller then the larger, to slot 7;
// 0xC01 (0xC03 as 2, else 1) to slot 9; 0xD00 to slot 11, 0xD01 to slot
// 13; the 0xD02 pair to slot 15; the 0xF016 image (both words read) to
// slot 17 with the 0xF018 rectangle (else the image's slot 12 area) and
// 0, written back under 0x6B4BCBD4.
void __cdecl Handle61325A2DBody(uint32_t* args)
{
    OPENSC4_FIRST("Handle61325A2D");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x61325A2D);
    if (!control.found)
        return;
    Props props(args[1]);
    if (!static_cast<uint8_t>(args[2])) {
        if (props.Word75(0x40A, &args[0]))
            Virtual<uint32_t>(control.object, 0x14, args[0]);
        return;
    }
    uint32_t f8 = 0, fC = 0, f10 = 0, f14 = 0, f18 = 0, f1C = 0;
    if (props.Pair(0xC00, &f10, &fC)) {
        if (static_cast<int32_t>(f10) > static_cast<int32_t>(fC)) {
            const uint32_t larger = f10;
            f10 = fC;
            fC = larger;
        }
        Virtual<uint32_t>(control.object, 0x1C, f10, fC);
    }
    if (props.Word(0xC01, &f8))
        Virtual<uint32_t>(control.object, 0x24, f8 == 0xC03 ? 2u : 1u);
    if (props.Word(0xD00, &f8))
        Virtual<uint32_t>(control.object, 0x2C, f8);
    if (props.Word(0xD01, &f8))
        Virtual<uint32_t>(control.object, 0x34, f8);
    if (props.Pair(0xD02, &f1C, &f18))
        Virtual<uint32_t>(control.object, 0x3C, f1C, f18);
    Text f4C;
    if (props.Find(f4C, 0xF016) && ScanKey(f4C, &f14, &f8) == 2) {
        void* const loaded = LoadImageOf(f14, f8);
        if (loaded) {
            int32_t f38[4] = {};
            if (props.Rect(0xF018, f38))
                Virtual<uint32_t>(control.object, 0x44, static_cast<void*>(f38), loaded, 0u);
            else
                Virtual<uint32_t>(control.object, 0x44, Virtual<void*>(loaded, 0x30), loaded, 0u);
        }
        const uint32_t f28[3] = {kImageKeyType, f14, f8};
        MakeKeyOf(window, 0x6B4BCBD4, f28);
        MakePairBody(f14, window, f8, 0);
        if (loaded)
            Virtual<uint32_t>(loaded, 8);
    }
}

// The game's 0x009505B6 (interface 0x21335C59, GZWinLineInput). First
// pass: 0xE09 to slot 25; 0x400 (on when not 0) to slot 28; 0xE0A to slot
// 29; the 0xE0B pair to slot 31 (the first, the second less the first; the
// first into the window argument). Second pass: the 0xF000 font number
// through 0x00913C72's slot 5 to slot 4 and back (else its text back, else
// the default font to slot 4); the colours 0xF00F, 0xF010 and 0xF011 to
// slot 5 (0, 1, 2), 0xE03 to 0xE06 to slot 14 (0 to 3) and 0xE07 to slot
// 17; 0xE08 (above 0xFF as 0xFF) times 1/255 as a float to slot 19; 0xE00
// (on when not 0; the out zeroed first) to slot 9; the 0xF005 style
// (0xE01 2, 0xE02 3, 0xF00A 1; the out zeroed first) to slot 11; the 0xF004
// gutters ("(%d,%d)" as both pairs, or "(%d,%d,%d,%d)", by the text's
// commas; unchecked) to slot 33. The window argument the outs'.
void __cdecl Handle21335C59Body(uint32_t* args)
{
    OPENSC4_FIRST("Handle21335C59");
    void* const window = AsPointer(args[0]);
    Control control(window, 0x21335C59);
    if (!control.found)
        return;
    Props props(args[1]);
    uint32_t f8 = 0, fC = 0, f10 = 0;
    if (!static_cast<uint8_t>(args[2])) {
        if (props.Word(0xE09, &f10))
            Virtual<uint32_t>(control.object, 0x64, f10);
        if (props.Word(0x400, &f10))
            Virtual<uint32_t>(control.object, 0x70, props.Dl(f10 != 0));
        if (props.Word(0xE0A, &f10))
            Virtual<uint32_t>(control.object, 0x74, f10);
        if (props.Pair(0xE0B, &args[0], &fC))
            Virtual<uint32_t>(control.object, 0x7C, args[0], fC - args[0]);
        return;
    }
    Text f38;
    if (props.Word(0xF000, &args[0])) {
        Virtual<uint32_t>(control.object, 0x10, Font(args[0]));
        MakeNumberBody(window, args[0], 0);
    } else if (props.Find(f38, 0xF000)) {
        MakeTextBody(window, f38.bytes, 0);
    } else {
        Text name("main9");
        const uint32_t font = Virtual<uint32_t>(Singleton913C72(), 0x98, static_cast<void*>(name.bytes));
        void* const singleton = Singleton913C72();
        Virtual<uint32_t>(control.object, 0x10, Virtual<uint32_t>(singleton, 0x14, font));
    }
    if (props.Bytes(0xF00F, &fC, &f8, &args[0])) {
        const uint32_t color = Color(fC, f8, args[0]);
        f10 = AsWord(*static_cast<void**>(control.object));
        Virtual<uint32_t>(control.object, 0x14, 0u, color);
    }
    for (uint32_t index = 1; index < 3; ++index) {
        if (props.Bytes(0xF00F + index, &fC, &f8, &args[0]))
            Virtual<uint32_t>(control.object, 0x14, index, Color(fC, f8, args[0]));
    }
    for (uint32_t index = 0; index < 4; ++index) {
        if (props.Bytes(0xE03 + index, &fC, &f8, &args[0]))
            Virtual<uint32_t>(control.object, 0x38, index, Color(fC, f8, args[0]));
    }
    if (props.Bytes(0xE07, &fC, &f8, &args[0]))
        Virtual<uint32_t>(control.object, 0x44, Color(fC, f8, args[0]));
    if (props.Word(0xE08, &args[0])) {
        // As 0x0095002C's: a word of at most 0xFF, never negative.
        args[0] = args[0] > 0xFF ? 0xFFu : args[0];
        const float alpha = static_cast<float>(static_cast<double>(static_cast<int32_t>(args[0])) *
                                               static_cast<double>(Global<float>(addr::SC4_FloatInv255)));
        Virtual<uint32_t>(control.object, 0x4C, alpha);
    }
    args[0] = 0;
    if (props.Word(0xE00, &args[0]))
        Virtual<uint32_t>(control.object, 0x24, props.Dl(args[0] != 0));
    args[0] = 0;
    if (props.Word(0xF005, &args[0])) {
        switch (args[0]) {
        case 0xE01: Virtual<uint32_t>(control.object, 0x2C, 2u); break;
        case 0xE02: Virtual<uint32_t>(control.object, 0x2C, 3u); break;
        case 0xF00A: Virtual<uint32_t>(control.object, 0x2C, 1u); break;
        default: break;
        }
    }
    Text f24;
    if (!props.Find(f24, 0xF004))
        return;
    const uint32_t length = CallThis<uint32_t>(addr::cRZBaseString_Strlen, f24.bytes);
    if (!length)
        return;
    const char* const text = f24.Begin();
    uint32_t commas = 0;
    for (uint32_t i = 0; i < length; ++i) {
        if (text[i] == ',')
            ++commas;
    }
    if (commas == 1) {
        Scan(text, addr::cGZUIScriptProps_PairFormat, &fC, &args[0]);
        Virtual<uint32_t>(control.object, 0x84, fC, args[0], fC, args[0]);
    } else if (commas == 3) {
        Scan(text, addr::cGZUIScriptProps_RectFormat, &f8, &fC, &f10, &args[0]);
        Virtual<uint32_t>(control.object, 0x84, f8, fC, f10, args[0]);
    }
}

// The game's 0x00951084 (interface 0xE303E5AE, GZWinOutline): on the
// second pass only. The flags (the window argument) from every 0xF005
// number pair (slot 73's word: 0xF009 1, 0xF00B 2, 0xF00C 4, 0xF00D 0x20)
// to slot 3 when any; the 0x1100 and 0x1101 bytes to slot 5 (1 and 2, the
// three bytes' dwords); every 0x1102 text (the value checked set) read as
// "(%d,%d)" (unchecked) into a table of at most 32 pairs on the stack, with
// any to slot 13 (the count, the table, 1, 1). The window argument ends as
// the loop's index.
void __cdecl HandleE303E5AEBody(uint32_t* args)
{
    OPENSC4_FIRST("HandleE303E5AE");
    Control control(AsPointer(args[0]), 0xE303E5AE);
    if (!control.found || !static_cast<uint8_t>(args[2]))
        return;
    Props props(args[1]);
    args[0] = 0;
    uint32_t count = PairCount(props.vector);
    for (uint32_t i = 0; i < count; ++i) {
        char* const pair = props.vector[0] + i * 8;
        if (At<uint32_t>(pair, 0) != 0xF005)
            continue;
        void* const value = At<void*>(pair, 4);
        if (!value || Virtual<uint32_t>(value, 0xC) != kNumberKind)
            continue;
        switch (*Virtual<const uint32_t*>(At<void*>(props.vector[0] + i * 8, 4), 0x124)) {
        case 0xF009: args[0] |= 0x01; break;
        case 0xF00B: args[0] |= 0x02; break;
        case 0xF00C: args[0] |= 0x04; break;
        case 0xF00D: args[0] |= 0x20; break;
        default: break;
        }
    }
    if (count && args[0])
        Virtual<uint32_t>(control.object, 0xC, args[0]);
    uint32_t f8 = 0, fC = 0;
    if (props.Bytes(0x1100, &f8, &fC, &args[0]))
        Virtual<uint32_t>(control.object, 0x14, 1u, f8, fC, args[0]);
    if (props.Bytes(0x1101, &f8, &fC, &args[0]))
        Virtual<uint32_t>(control.object, 0x14, 2u, f8, fC, args[0]);
    uint32_t table[32][2] = {};
    uint32_t used = 0;
    count = PairCount(props.vector);
    for (args[0] = 0; args[0] < count && used < 32; ++args[0]) {
        char* const pair = props.vector[0] + args[0] * 8;
        if (At<uint32_t>(pair, 0) != 0x1102)
            continue;
        void* const value = At<void*>(pair, 4);
        if (!value || Virtual<uint32_t>(value, 0xC) != kTextKind)
            continue;
        Text f20;
        Virtual<uint32_t>(At<void*>(props.vector[0] + args[0] * 8, 4), 0xDC, static_cast<void*>(f20.bytes));
        Scan(f20.Begin(), addr::cGZUIScriptProps_PairFormat, &f8, &fC);
        table[used][0] = f8;
        table[used][1] = fC;
        ++used;
    }
    if (used)
        Virtual<uint32_t>(control.object, 0x34, used, static_cast<void*>(table), 1u, 1u);
}

// The thunks: the bodies get the handler's three argument slots.
#define OPENSC4_HANDLER_THUNK(name)                                                                            \
    __declspec(naked) void name()                                                                              \
    {                                                                                                          \
        __asm lea eax, [esp + 4]                                                                               \
        __asm push eax                                                                                         \
        __asm call name##Body                                                                                  \
        __asm add esp, 4                                                                                       \
        __asm ret                                                                                              \
    }

OPENSC4_HANDLER_THUNK(HandleC2AFA76F)
OPENSC4_HANDLER_THUNK(Handle212CDC1F)
OPENSC4_HANDLER_THUNK(Handle231A1C57)
OPENSC4_HANDLER_THUNK(Handle4132242B)
OPENSC4_HANDLER_THUNK(Handle8810)
OPENSC4_HANDLER_THUNK(HandleC12CEA13)
OPENSC4_HANDLER_THUNK(Handle21325207)
OPENSC4_HANDLER_THUNK(Handle612CE0C3)
OPENSC4_HANDLER_THUNK(Handle032F0B3C)
OPENSC4_HANDLER_THUNK(HandleA13351B8)
OPENSC4_HANDLER_THUNK(Handle412CE496)
OPENSC4_HANDLER_THUNK(Handle61325A2D)
OPENSC4_HANDLER_THUNK(Handle21335C59)
OPENSC4_HANDLER_THUNK(HandleE303E5AE)

#undef OPENSC4_HANDLER_THUNK

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

bool InstallPropWords(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptProps_AddWords, &AddWords},
    };
    return InstallAll(detours);
}

bool InstallPropHandle(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptProps_HandleC2AFA76F, &HandleC2AFA76F},
        {addr::cGZUIScriptProps_Handle212CDC1F, &Handle212CDC1F},
        {addr::cGZUIScriptProps_Handle231A1C57, &Handle231A1C57},
        {addr::cGZUIScriptProps_Handle4132242B, &Handle4132242B},
        {addr::cGZUIScriptProps_Handle8810, &Handle8810},
        {addr::cGZUIScriptProps_HandleC12CEA13, &HandleC12CEA13},
        {addr::cGZUIScriptProps_Handle21325207, &Handle21325207},
        {addr::cGZUIScriptProps_Handle612CE0C3, &Handle612CE0C3},
    };
    return InstallAll(detours);
}

bool InstallPropHandle2(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptProps_Handle032F0B3C, &Handle032F0B3C},
        {addr::cGZUIScriptProps_HandleA13351B8, &HandleA13351B8},
        {addr::cGZUIScriptProps_Handle412CE496, &Handle412CE496},
        {addr::cGZUIScriptProps_Handle61325A2D, &Handle61325A2D},
        {addr::cGZUIScriptProps_Handle21335C59, &Handle21335C59},
        {addr::cGZUIScriptProps_HandleE303E5AE, &HandleE303E5AE},
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
    registry::Register({"framework.uiscript.propwords", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_AddWords,
                        "Our filler of the UI script variable table: the 64 words of a window's script "
                        "properties and their values (1 detour; part u12, slice 3, docs/gzui.md)",
                        false, InstallPropWords}),
    registry::Register({"framework.uiscript.prophandle", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_HandleC2AFA76F,
                        "Our handlers of a window's script properties for eight controls: the text control, "
                        "the text edit, the button and five others (8 detours; part u12, slice 3, docs/gzui.md)",
                        false, InstallPropHandle}),
    registry::Register({"framework.uiscript.prophandle2", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptProps_Handle032F0B3C,
                        "Our handlers of a window's script properties for six widgets no shipped script "
                        "makes: the text ticker, folders, combo, scrollbar, line input and outline (6 "
                        "detours; part u12, slice 4, docs/gzui.md)",
                        false, InstallPropHandle2}),
};

} // namespace
} // namespace opensc4::gzui
