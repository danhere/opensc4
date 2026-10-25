// The UI script writers (docs/gzui.md, part u17): 0x0095BAD7 to 0x0095FFFF,
// the code that writes a window back out as a LEGACY UI script, the reverse
// of the readers of parts u13 to u16b. The game never saves a UI script, so
// none of it ran in the coverage runs. 0x00965F73 (above the block, still
// the game's) writes `<LEGACY `, the base window's words, then asks the
// window for one interface and hands it to that class's writer, then `>`.
// A writer asks the interface for each of its settings and prints a line
// `name=value ` for it into the out string (an SC4String: its body at +4)
// through one of 14 small printers, one per form of value. Three helpers
// read a window's script properties (its slot 113) for the writers.
// Replacements, each off by default:
//
//   framework.uiscript.writelines  the 14 printers, the font printer, the
//                                  flag table and a key compare,
//                                  0x0095BC21 to 0x0095C185
//   framework.uiscript.writeprops  the property helpers 0x0095BAD7,
//                                  0x0095BB29 and 0x0095BB82
//   framework.uiscript.writers     the writers of GZWinFlatRect 0x0095C211,
//                                  Text 0x0095C55A, TextEdit 0x0095CC7B,
//                                  ListBox 0x0095D439, LineInput 0x0095DB1A
//                                  and Combo 0x0095E1E2
//   framework.uiscript.writers2    the writers of GZWinBtn 0x0095E6F0, BMP
//                                  0x0095F2CA, Slider 0x0095F4EC, Scrollbar
//                                  0x0095F663 and OptGrp 0x0095F9B0 with its
//                                  options' callback 0x0095F87B
//
// The writers call the printers and helpers by address (ours when their
// group is on). Many take arguments in registers; those enter through naked
// thunks that hand the body the registers. The strings each writer makes go
// on its stack, made and destroyed in the game's order. The writers' EAX:
// the dispatcher and the other writers read none after them; the printers'
// and helpers' EAX (or AL) is the game's. The groups go in at Attach, with
// the UI script loader's.
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
inline R Cdecl(std::uintptr_t address, Args... args)
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

// A string's slot 27, its printf (cdecl: the string, a format and the
// values), through the vtable read before.
template <typename... Args>
inline uint32_t PrintInto(void** vtable, void* string, std::uintptr_t format, Args... args)
{
    using Print = uint32_t(__cdecl*)(void*, const char*, ...);
    return reinterpret_cast<Print>(vtable[0x6C / 4])(string, reinterpret_cast<const char*>(format), args...);
}

inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t Low(uint32_t eax, uint32_t al) { return (eax & ~0xFFu) | (al & 0xFF); }

inline uint32_t Free(void* memory) { return reinterpret_cast<uint32_t(__cdecl*)(void*)>(addr::SC4_Free)(memory); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: writers: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// A string (SC4String, 0x14 bytes): the vtable 0x00A80810, the characters
// from +4. Made by the game's constructor or from a literal
// (SC4String::FromText); destroyed inline as the game does on its stack
// (the vtable back, the characters freed); the free's EAX answered when
// there were characters, else the one given.
constexpr std::size_t kTextBegin = 4;
constexpr std::size_t kTextSize = 0x14;

struct Text {
    alignas(4) char bytes[kTextSize];
    char* Begin() { return At<char*>(bytes, kTextBegin); }
    void Make() { CallThis<void*>(addr::SC4String_SC4String, bytes); }
    void* From(std::uintptr_t literal)
    {
        return CallThis<void*>(addr::SC4String_FromText, bytes, reinterpret_cast<const char*>(literal));
    }
    uint32_t End(uint32_t eax)
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (char* const begin = Begin())
            return Free(begin);
        return eax;
    }
};

// A string's characters as the printers push them (its +4).
inline const char* Chars(void* string) { return At<const char*>(string, kTextBegin); }

// The literals the writers print (by address).
constexpr std::uintptr_t kYes = 0x00A863D4;      // "yes"
constexpr std::uintptr_t kNo = 0x00A920B4;       // "no"
constexpr std::uintptr_t kSpace = 0x00A85B40;    // " "
constexpr std::uintptr_t kString = 0x00A9D8B4;   // "%s"
constexpr std::uintptr_t kInt = 0x00A8281C;      // "%d"
constexpr std::uintptr_t kHexWord = 0x00A8D784;  // "0x%08x"
constexpr uint32_t kDefaultFont = 0x68963C4C;

// The font service's slot 37 (a font's face by its word, or none) and the
// face's slot 5 (its name).
constexpr std::size_t kFaceOf = 0x94;
constexpr std::size_t kFaceName = 0x14;

// The window's slot 113 (a script property by key: bool, the value out)
// and a script value's slots.
constexpr std::size_t kProperty = 0x1C4;
constexpr std::size_t kValueRelease = 0x8;
constexpr std::size_t kValueKind = 0xC;
constexpr std::size_t kValueType = 0x10;
constexpr std::size_t kValueText = 0xDC;   // slot 55 (string*)
constexpr std::size_t kValueBytes = 0xE4;  // slot 57 (out, count*)
constexpr std::size_t kValueWord = 0x124;  // slot 73 (the word's place)
constexpr std::size_t kValuePair = 0x19C;  // slot 103 (two words' place)

// ====================================================================================
// The printers (framework.uiscript.writelines)

// The game's shape: a string made on the stack (SC4String::SC4String), the
// line printed into it (cRZBaseString::Sprintf, cdecl), its body appended
// to the out string's (SC4String::AppendStd on both +4s), the string
// destroyed inline; EAX the append's or the free's.
template <typename... Args>
uint32_t Print(void* out, std::uintptr_t format, Args... args)
{
    Text line;
    line.Make();
    Cdecl<uint32_t>(addr::cRZBaseString_Sprintf, line.bytes, reinterpret_cast<const char*>(format), args...);
    const uint32_t eax = CallThis<uint32_t>(addr::SC4String_AppendStd, static_cast<char*>(out) + kTextBegin,
                                            static_cast<void*>(line.bytes + kTextBegin));
    return line.End(eax);
}

// The game's 0x0095BC92 (cdecl: out, name, three bytes): "%s=(%u,%u,%u) ",
// each byte's low byte only.
uint32_t __cdecl LineColour(void* out, void* name, uint32_t r, uint32_t g, uint32_t b)
{
    OPENSC4_FIRST("LineColour");
    return Print(out, addr::cGZUIScriptWriters_ColourFormat, Chars(name), r & 0xFF, g & 0xFF, b & 0xFF);
}

// The game's 0x0095BCED: two colours, "%s=(%u,%u,%u)(%u,%u,%u) ".
uint32_t __cdecl LineColours2(void* out, void* name, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                              uint32_t f)
{
    OPENSC4_FIRST("LineColours2");
    return Print(out, addr::cGZUIScriptWriters_Colours2Format, Chars(name), a & 0xFF, b & 0xFF, c & 0xFF, d & 0xFF,
                 e & 0xFF, f & 0xFF);
}

// The game's 0x0095BD57: four colours (12 bytes).
uint32_t __cdecl LineColours4(void* out, void* name, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                              uint32_t f, uint32_t g, uint32_t h, uint32_t i, uint32_t j, uint32_t k, uint32_t l)
{
    OPENSC4_FIRST("LineColours4");
    return Print(out, addr::cGZUIScriptWriters_Colours4Format, Chars(name), a & 0xFF, b & 0xFF, c & 0xFF, d & 0xFF,
                 e & 0xFF, f & 0xFF, g & 0xFF, h & 0xFF, i & 0xFF, j & 0xFF, k & 0xFF, l & 0xFF);
}

// The game's 0x0095BDDF: "%s=%d ".
uint32_t __cdecl LineInt(void* out, void* name, uint32_t value)
{
    OPENSC4_FIRST("LineInt");
    return Print(out, addr::cGZUIScriptWriters_IntFormat, Chars(name), value);
}

// The game's 0x0095BE2E: "%s=0x%08x ".
uint32_t __cdecl LineHex(void* out, void* name, uint32_t value)
{
    OPENSC4_FIRST("LineHex");
    return Print(out, addr::cGZUIScriptWriters_HexFormat, Chars(name), value);
}

// The game's 0x0095BE7D: a resource key, "%s={%08x,%08x} ".
uint32_t __cdecl LineKey(void* out, void* name, uint32_t group, uint32_t instance)
{
    OPENSC4_FIRST("LineKey");
    return Print(out, addr::cGZUIScriptWriters_KeyFormat, Chars(name), group, instance);
}

// The game's 0x0095BECF: "%s=(%d,%d) ".
uint32_t __cdecl LinePair(void* out, void* name, uint32_t a, uint32_t b)
{
    OPENSC4_FIRST("LinePair");
    return Print(out, addr::cGZUIScriptWriters_PairFormat, Chars(name), a, b);
}

// The game's 0x0095BF21: "%s=(%d,%d,%d,%d) ".
uint32_t __cdecl LineRect(void* out, void* name, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    OPENSC4_FIRST("LineRect");
    return Print(out, addr::cGZUIScriptWriters_RectFormat, Chars(name), a, b, c, d);
}

// The game's 0x0095BF79: a word, "%s=%s " (the value a string).
uint32_t __cdecl LineWord(void* out, void* name, void* value)
{
    OPENSC4_FIRST("LineWord");
    return Print(out, addr::cGZUIScriptWriters_WordFormat, Chars(name), Chars(value));
}

// The game's 0x0095BFCB: a bool's low byte as `yes` or `no` ("%s=%s ").
uint32_t __cdecl LineBool(void* out, void* name, uint32_t value)
{
    OPENSC4_FIRST("LineBool");
    const std::uintptr_t word = (value & 0xFF) ? kYes : kNo;
    return Print(out, addr::cGZUIScriptWriters_WordFormat, Chars(name), reinterpret_cast<const char*>(word));
}

// The game's 0x0095C028: a quoted text, "%s=\"%s\" ".
uint32_t __cdecl LineQuoted(void* out, void* name, void* value)
{
    OPENSC4_FIRST("LineQuoted");
    return Print(out, addr::cGZUIScriptWriters_QuotedFormat, Chars(name), Chars(value));
}

// The game's 0x0095C07A (the string in EAX, the out string in ECX; the
// thunk hands them over): its body appended (a null string as a null
// body, which the game's would not survive either), then " "
// (0x00410F00); EAX that call's.
uint32_t __cdecl LineAppendBody(void* out, void* string)
{
    OPENSC4_FIRST("LineAppend");
    char* const body = static_cast<char*>(out) + kTextBegin;
    CallThis<uint32_t>(addr::SC4String_AppendStd, body,
                       string ? static_cast<void*>(static_cast<char*>(string) + kTextBegin) : nullptr);
    return CallThis<uint32_t>(addr::cSC4Advisor_F410F00, body, reinterpret_cast<const char*>(kSpace));
}

__declspec(naked) void LineAppend()
{
    __asm push eax
    __asm push ecx
    __asm call LineAppendBody
    __asm add esp, 8
    __asm ret
}

// The game's 0x0095C0A1 (cdecl: out, id, number, group, instance and a
// rectangle; the name string in EDI, the text in ESI): with a group and an
// instance both not 0 "%s=\"%u:%s:%u:{%08x,%08x}:(%d,%d,%d,%d)\" ", else
// "%s=\"%u:%s:%u::\" ".
uint32_t __cdecl LineOptionBody(void* name, void* text, void* out, uint32_t id, uint32_t number, uint32_t group,
                                uint32_t instance, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    OPENSC4_FIRST("LineOption");
    Text line;
    line.Make();
    if (group != 0 && instance != 0)
        Cdecl<uint32_t>(addr::cRZBaseString_Sprintf, line.bytes,
                        reinterpret_cast<const char*>(addr::cGZUIScriptWriters_OptionFormat), Chars(name), id,
                        Chars(text), number, group, instance, x, y, w, h);
    else
        Cdecl<uint32_t>(addr::cRZBaseString_Sprintf, line.bytes,
                        reinterpret_cast<const char*>(addr::cGZUIScriptWriters_OptionShortFormat), Chars(name), id,
                        Chars(text), number);
    const uint32_t eax = CallThis<uint32_t>(addr::SC4String_AppendStd, static_cast<char*>(out) + kTextBegin,
                                            static_cast<void*>(line.bytes + kTextBegin));
    return line.End(eax);
}

__declspec(naked) void LineOption()
{
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push esi
    __asm push edi
    __asm call LineOptionBody
    __asm add esp, 44
    __asm ret
}

// The game's 0x0095C130: an option's move or size, "%s=\"%u:(%d,%d)\" ".
uint32_t __cdecl LineMove(void* out, void* name, uint32_t id, uint32_t a, uint32_t b)
{
    OPENSC4_FIRST("LineMove");
    return Print(out, addr::cGZUIScriptWriters_MoveFormat, Chars(name), id, a, b);
}

// The game's 0x0095C185 (cdecl: out, the flags, the defaults; the table in
// EAX): a table of 12-byte rows (a mask, a name's literal, a byte), ended
// by a row with no name. For each row whose flag differs from the
// default, or whose byte is set: `yes` or `no` and the name made into
// strings (in that order), printed by 0x0095BF79 (by address), destroyed
// name first. EAX as the game's: the last row's mask read, the last
// call's, or the table's address for an empty table.
struct FlagRow {
    uint32_t mask;
    std::uintptr_t name;
    uint32_t always;
};

uint32_t __cdecl LineFlagsBody(const FlagRow* row, void* out, uint32_t flags, uint32_t defaults)
{
    OPENSC4_FIRST("LineFlags");
    uint32_t eax = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(row));
    for (; row->name; ++row) {
        eax = row->mask;
        if (!((flags ^ defaults) & eax) && !(row->always & 0xFF))
            continue;
        Text value;
        Text name;
        void* const valueMade = value.From((flags & eax) ? kYes : kNo);
        void* const nameMade = name.From(row->name);
        eax = Cdecl<uint32_t>(addr::cGZUIScriptWriters_LineWord, out, nameMade, valueMade);
        eax = name.End(eax);
        eax = value.End(eax);
    }
    return eax;
}

__declspec(naked) void LineFlags()
{
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push eax
    __asm call LineFlagsBody
    __asm add esp, 16
    __asm ret
}

// The game's 0x0095BC21 (cdecl: a font's word; the string in ESI): a font
// the font service knows by its face's name ("%s"), the default font
// 0x68963C4C nothing (AL 0, EAX slot 37's 0), any other as
// "0x%08x", each through the string's slot 27; AL 1 over its EAX.
uint32_t __cdecl LineFontBody(void* string, uint32_t font)
{
    OPENSC4_FIRST("LineFont");
    void* const face = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), kFaceOf, font);
    if (face) {
        void** const vtable = *static_cast<void***>(string);
        const uint32_t faceName = Virtual<uint32_t>(face, kFaceName);
        return Low(PrintInto(vtable, string, kString, faceName), 1);
    }
    if (font == kDefaultFont)
        return 0;
    return Low(PrintInto(*static_cast<void***>(string), string, kHexWord, font), 1);
}

__declspec(naked) void LineFont()
{
    __asm push dword ptr [esp + 4]
    __asm push esi
    __asm call LineFontBody
    __asm add esp, 8
    __asm ret
}

// The game's 0x0095BC74 (thiscall, `ret 4`): two keys the same by their
// +4 and +8 (1 or 0).
uint32_t __fastcall KeyEquals(const uint32_t* self, void*, const uint32_t* other)
{
    OPENSC4_FIRST("KeyEquals");
    return self[1] == other[1] && self[2] == other[2] ? 1u : 0u;
}

OPENSC4_THUNK(KeyEqualsThunk, KeyEquals, 4)

// ====================================================================================
// The property helpers (framework.uiscript.writeprops)

// The game's 0x0095BAD7 (cdecl: a key and an out place; the window in ECX,
// none allowed): the window's property under the key (slot 113); of type
// 0xC (its slot 4) its 12 bytes into the out place (slot 57 with a count
// of 0xC); the value released. AL 1 when written, else 0, over the last
// call's EAX (without a window the EAX it came with).
uint32_t __cdecl PropertyBytesBody(void* window, uint32_t eax, uint32_t key, void* out)
{
    OPENSC4_FIRST("PropertyBytes");
    if (!window)
        return Low(eax, 0);
    void* value;
    eax = Virtual<uint32_t>(window, kProperty, key, static_cast<void*>(&value));
    if (!Yes(eax))
        return Low(eax, 0);
    uint32_t written = 0;
    if (Virtual<uint32_t>(value, kValueType) == 0xC) {
        uint32_t count = 0xC;
        Virtual<uint32_t>(value, kValueBytes, out, static_cast<void*>(&count));
        written = 1;
    }
    return Low(Virtual<uint32_t>(value, kValueRelease), written);
}

__declspec(naked) void PropertyBytes()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push eax
    __asm push ecx
    __asm call PropertyBytesBody
    __asm add esp, 16
    __asm ret
}

// The game's 0x0095BB29 (cdecl: two out words and an index; the window in
// ECX): the window's property under 0xDAA3B860 + the index; a value whose
// kind (slot 3) has 0x8000 gives its two words (slot 103) into the outs;
// released. AL 1 when written, else 0, over the last call's EAX.
uint32_t __cdecl PropertyPairBody(void* window, uint32_t* first, uint32_t* second, uint32_t index)
{
    OPENSC4_FIRST("PropertyPair");
    void* value = nullptr;
    const uint32_t eax = Virtual<uint32_t>(window, kProperty, index + 0xDAA3B860u, static_cast<void*>(&value));
    if (!Yes(eax))
        return Low(eax, 0);
    uint32_t written = 0;
    if (Virtual<uint32_t>(value, kValueKind) & 0x8000) {
        const uint32_t* const pair = Virtual<const uint32_t*>(value, kValuePair);
        *first = pair[0];
        *second = pair[1];
        written = 1;
    }
    return Low(Virtual<uint32_t>(value, kValueRelease), written);
}

__declspec(naked) void PropertyPair()
{
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push ecx
    __asm call PropertyPairBody
    __asm add esp, 16
    __asm ret
}

// The game's 0x0095BB82 (cdecl: an index; the window in ECX, the out
// string in EDI): the window's property under 0xFAA3BE85 + the index. A
// number (kind 6): its word (slot 73) printed into the string by its slot
// 27 as the font service's face's name ("%s") when it is a font, else as
// "%d"; a text (kind 0x800E): given to the value's slot 55 with the
// string. Released. AL 1 for either kind, else 0, over the last call's
// EAX.
uint32_t __cdecl PropertyFontBody(void* window, void* string, uint32_t index)
{
    OPENSC4_FIRST("PropertyFont");
    void* value = nullptr;
    const uint32_t eax = Virtual<uint32_t>(window, kProperty, index + 0xFAA3BE85u, static_cast<void*>(&value));
    if (!Yes(eax))
        return Low(eax, 0);
    uint32_t written = 0;
    if (Virtual<uint32_t>(value, kValueKind) == 6) {
        const uint32_t word = *Virtual<const uint32_t*>(value, kValueWord);
        void* const face = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), kFaceOf, word);
        if (face) {
            void** const vtable = *static_cast<void***>(string);
            PrintInto(vtable, string, kString, Virtual<uint32_t>(face, kFaceName));
        } else {
            PrintInto(*static_cast<void***>(string), string, kInt, word);
        }
        written = 1;
    } else if (Virtual<uint32_t>(value, kValueKind) == 0x800E) {
        Virtual<uint32_t>(value, kValueText, string);
        written = 1;
    }
    return Low(Virtual<uint32_t>(value, kValueRelease), written);
}

__declspec(naked) void PropertyFont()
{
    __asm push dword ptr [esp + 4]
    __asm push edi
    __asm push ecx
    __asm call PropertyFontBody
    __asm add esp, 12
    __asm ret
}

// ====================================================================================
// The writers' common steps

// A line by one of the printers (by address, ours when their group is on):
// the name made from its literal, the printer called with the out string,
// the name and the values, the name destroyed.
template <typename... Args>
void Line(std::uintptr_t printer, void* out, std::uintptr_t literal, Args... args)
{
    Text name;
    void* const made = name.From(literal);
    name.End(Cdecl<uint32_t>(printer, out, made, args...));
}

// A word line (0x0095BF79) with a literal value: the value made first, then
// the name; the name destroyed first.
void WordLine(void* out, std::uintptr_t literal, std::uintptr_t word)
{
    Text value;
    Text name;
    void* const valueMade = value.From(word);
    void* const nameMade = name.From(literal);
    const uint32_t eax = Cdecl<uint32_t>(addr::cGZUIScriptWriters_LineWord, out, nameMade, valueMade);
    value.End(name.End(eax));
}

// A bool as a word line: `yes` or `no`.
void YesNoLine(void* out, std::uintptr_t literal, bool on) { WordLine(out, literal, on ? kYes : kNo); }

// ====================================================================================
// GZWinFlatRect's writer (framework.uiscript.writers)

// IGZWinFlatRect's slots.
constexpr std::size_t kFlatRectColour = 0x2C; // slot 11 (side, three byte places)
constexpr std::size_t kFlatRectStyle = 0x34;  // slot 13 (the style bits, AL)

// The game's 0x0095C211 (the interface in EAX, the out string in EBX): the
// four sides' colours (slot 11 with 0 to 3; the three bytes' places shared
// by the four calls and never set before, so a slot that writes nothing
// prints the last ones, or what the game's stack held), then for each of
// the style's seven bits its word under `style`.
uint32_t __cdecl FlatRectBody(void* face, void* out)
{
    OPENSC4_FIRST("FlatRect");
    static const std::uintptr_t kSides[4] = {
        0x00AD5814, // "colorleft"
        0x00AD5808, // "colortop"
        0x00AD57FC, // "colorright"
        0x00AD57F0, // "colorbottom"
    };
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    for (uint32_t side = 0; side < 4; ++side) {
        Virtual<uint32_t>(face, kFlatRectColour, side, static_cast<void*>(&r), static_cast<void*>(&g),
                          static_cast<void*>(&b));
        Line(addr::cGZUIScriptWriters_LineColour, out, kSides[side], uint32_t{r}, uint32_t{g}, uint32_t{b});
    }
    static const FlagRow kStyles[] = {
        {0x01, 0x00AA1BD8, 0}, // "left"
        {0x02, 0x00AD57EC, 0}, // "top"
        {0x04, 0x00AA1BCC, 0}, // "right"
        {0x08, 0x00AD57E4, 0}, // "bottom"
        {0x10, 0x00AD5624, 0}, // "raised"
        {0x20, 0x00AD562C, 0}, // "sunken"
        {0x40, 0x00AD5614, 0}, // "nofill"
    };
    const uint32_t style = Virtual<uint32_t>(face, kFlatRectStyle) & 0xFF;
    for (const FlagRow& row : kStyles)
        if (style & row.mask)
            WordLine(out, 0x00AD564C /* "style" */, row.name);
    return 0;
}

__declspec(naked) void FlatRect()
{
    __asm push ebx
    __asm push eax
    __asm call FlatRectBody
    __asm add esp, 8
    __asm ret
}

// ====================================================================================
// The writers' calls of the printers and helpers that take registers

// The address called with EAX, ECX, ESI and EDI from the block and nine
// stack arguments (cdecl, popped here; a callee taking fewer reads only
// its own); EAX answered.
struct Regs {
    uint32_t eax, ecx, esi, edi;
};

__declspec(naked) uint32_t __cdecl CallRegs(std::uintptr_t /*address*/, const Regs* /*regs*/, uint32_t /*a0*/,
                                            uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t /*a4*/,
                                            uint32_t /*a5*/, uint32_t /*a6*/, uint32_t /*a7*/, uint32_t /*a8*/)
{
    __asm push ebp
    __asm mov ebp, esp
    __asm push esi
    __asm push edi
    __asm push dword ptr [ebp + 48]
    __asm push dword ptr [ebp + 44]
    __asm push dword ptr [ebp + 40]
    __asm push dword ptr [ebp + 36]
    __asm push dword ptr [ebp + 32]
    __asm push dword ptr [ebp + 28]
    __asm push dword ptr [ebp + 24]
    __asm push dword ptr [ebp + 20]
    __asm push dword ptr [ebp + 16]
    __asm mov edx, [ebp + 12]
    __asm mov eax, [edx]
    __asm mov ecx, [edx + 4]
    __asm mov esi, [edx + 8]
    __asm mov edi, [edx + 12]
    __asm call dword ptr [ebp + 8]
    __asm add esp, 36
    __asm pop edi
    __asm pop esi
    __asm pop ebp
    __asm ret
}

inline uint32_t Word32(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// 0x0095BAD7: the window in ECX (and in EAX, as the writers' interface
// slot leaves it; the helper reads EAX only to hand it back without a
// window), a key, the 12 bytes' place.
uint32_t CallPropertyBytes(void* window, uint32_t key, uint32_t* bytes)
{
    const Regs regs = {Word32(window), Word32(window), 0, 0};
    return CallRegs(addr::cGZUIScriptWriters_PropertyBytes, &regs, key, Word32(bytes), 0, 0, 0, 0, 0, 0, 0);
}

// 0x0095BB29: the window in ECX, two out words and an index.
uint32_t CallPropertyPair(void* window, uint32_t* first, uint32_t* second, uint32_t index)
{
    const Regs regs = {0, Word32(window), 0, 0};
    return CallRegs(addr::cGZUIScriptWriters_PropertyPair, &regs, Word32(first), Word32(second), index, 0, 0, 0, 0, 0,
                    0);
}

// 0x0095BB82: the window in ECX, the string in EDI, an index.
uint32_t CallPropertyFont(void* window, void* string, uint32_t index)
{
    const Regs regs = {0, Word32(window), 0, Word32(string)};
    return CallRegs(addr::cGZUIScriptWriters_PropertyFont, &regs, index, 0, 0, 0, 0, 0, 0, 0, 0);
}

// 0x0095C07A: the string in EAX, the out string in ECX.
uint32_t CallAppend(void* out, void* string)
{
    const Regs regs = {Word32(string), Word32(out), 0, 0};
    return CallRegs(addr::cGZUIScriptWriters_LineAppend, &regs, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

// 0x0095C0A1: the name in EDI, the text in ESI, then the out string, the
// id, the number, the key and the rectangle.
uint32_t CallOption(void* name, void* text, void* out, uint32_t id, uint32_t number, uint32_t group,
                    uint32_t instance, const int32_t (&rect)[4])
{
    const Regs regs = {0, 0, Word32(text), Word32(name)};
    return CallRegs(addr::cGZUIScriptWriters_LineOption, &regs, Word32(out), id, number, group, instance,
                    static_cast<uint32_t>(rect[0]), static_cast<uint32_t>(rect[1]), static_cast<uint32_t>(rect[2]),
                    static_cast<uint32_t>(rect[3]));
}

// 0x0095BC21: the string in ESI, the font's word.
uint32_t CallFont(void* string, uint32_t word)
{
    const Regs regs = {0, 0, Word32(string), 0};
    return CallRegs(addr::cGZUIScriptWriters_LineFont, &regs, word, 0, 0, 0, 0, 0, 0, 0, 0);
}

// 0x0095C185: the table in EAX, the out string, the flags, the defaults.
uint32_t CallFlags(std::uintptr_t table, void* out, uint32_t flags, uint32_t defaults)
{
    const Regs regs = {static_cast<uint32_t>(table), 0, 0, 0};
    return CallRegs(addr::cGZUIScriptWriters_LineFlags, &regs, Word32(out), flags, defaults, 0, 0, 0, 0, 0, 0);
}

// A colour's three bytes by the colour service (0x00913C1A, slot 36: the
// colour and three places, the first the red), fetched afresh each time.
constexpr std::size_t kColourBytes = 0x90;

void ColourBytes(uint32_t colour, uint32_t* r, uint32_t* g, uint32_t* b)
{
    Virtual<uint32_t>(Cdecl<void*>(addr::SC4_Singleton913C1A), kColourBytes, colour, static_cast<void*>(r),
                      static_cast<void*>(g), static_cast<void*>(b));
}

// A colour line: the colour's bytes into the places the caller keeps
// (shared between its lines, as the game's are), then 0x0095BC92 prints
// them (the name made after).
void ColourLine(void* out, std::uintptr_t literal, uint32_t colour, uint32_t& r, uint32_t& g, uint32_t& b)
{
    ColourBytes(colour, &r, &g, &b);
    Line(addr::cGZUIScriptWriters_LineColour, out, literal, r, g, b);
}

void ColourLine(void* out, std::uintptr_t literal, uint32_t colour, uint32_t (&bytes)[3])
{
    ColourLine(out, literal, colour, bytes[0], bytes[1], bytes[2]);
}

// A font line: the font's word through 0x0095BC21 into the string; when it
// answers AL 1, the string printed under `font` by 0x0095BF79 (the name
// made after the font's text).
void FontLine(void* out, void* font, uint32_t word)
{
    if (Yes(CallFont(font, word)))
        Line(addr::cGZUIScriptWriters_LineWord, out, 0x00AD567C /* "font" */, font);
}

// `autofit`: the window's property 0xDACF8F9C (its slot 113); when there
// is one and its byte (slot 63) is not 0, `autofit` made into a string and
// appended with a space by 0x0095C07A, the string destroyed; the value
// released whenever found.
constexpr uint32_t kAutofitKey = 0xDACF8F9C;
constexpr std::size_t kValueBool = 0xFC; // slot 63 (the bool's place)

void AutofitLine(void* out, void* window)
{
    void* value = nullptr;
    if (!Yes(Virtual<uint32_t>(window, kProperty, kAutofitKey, static_cast<void*>(&value))))
        return;
    if (*Virtual<const char*>(value, kValueBool) != 0) {
        Text name;
        void* const made = name.From(0x00AD58AC); // "autofit"
        name.End(CallAppend(out, made));
    }
    Virtual<uint32_t>(value, kValueRelease);
}

// ====================================================================================
// GZWinText's writer (framework.uiscript.writers)

// IGZWinText's slots.
constexpr std::size_t kTextWindow = 0xC;       // slot 3 (its window)
constexpr std::size_t kTextFlag = 0x18;        // slot 6 (mask; AL)
constexpr std::size_t kTextColour = 0x30;      // slot 12 (index, colour*; AL)
constexpr std::size_t kTextFont = 0x50;        // slot 20 (font*; AL)
constexpr std::size_t kTextAlign = 0x58;       // slot 22 (alignment*; AL)
constexpr std::size_t kTextGutters = 0x60;     // slot 24 (two signed byte places; AL)
constexpr std::size_t kTextOffsets = 0x68;     // slot 26 (two signed byte places; AL)

// The game's 0x0095C55A (cdecl: the interface, the out string; named
// TextWriter here, the string type being Text). A string made first for
// the font. In order:
//
// - the font (slot 20, then LineFont into the string) under `font`;
// - the alignment (slot 22): 0 to 8 as a word under `align` (lefttop,
//   leftcenter, leftbottom, centertop, center, centerbottom, righttop,
//   rightcenter, rightbottom), any other by 0x0095BE2E as hex;
// - `notify`, `wrapped` and `opaque`: slot 6 with 1, 2 and 4, yes or no;
// - `forecolor` and `bkgcolor`: slot 12 with 0 and 1 (one colour place),
//   each through the colour service;
// - `gutters` and `textoffsets`: slots 24 and 26 (two shared signed bytes,
//   sign-extended) by 0x0095BECF;
// - `autofit`, from the interface's window (slot 3);
//
// then the font's string destroyed. The places the game reads from its
// stack when a getter answers yes without writing (the font word, the
// alignment, the colour, the bytes; the colour bytes when the service
// writes none) are 0 here.
uint32_t __cdecl TextWriter(void* face, void* out)
{
    OPENSC4_FIRST("TextWriter");
    Text font;
    font.Make();
    uint32_t word = 0;
    if (Yes(Virtual<uint32_t>(face, kTextFont, static_cast<void*>(&word))))
        FontLine(out, font.bytes, word);
    uint32_t align = 0;
    if (Yes(Virtual<uint32_t>(face, kTextAlign, static_cast<void*>(&align)))) {
        static const std::uintptr_t kAligns[9] = {
            0x00AD58A4, // "lefttop"
            0x00AD5898, // "leftcenter"
            0x00AD588C, // "leftbottom"
            0x00AD5880, // "centertop"
            0x00AD5644, // "center"
            0x00AD5870, // "centerbottom"
            0x00AD5864, // "righttop"
            0x00AD5858, // "rightcenter"
            0x00AD584C, // "rightbottom"
        };
        if (align <= 8)
            WordLine(out, 0x00A9F524 /* "align" */, kAligns[align]);
        else
            Line(addr::cGZUIScriptWriters_LineHex, out, 0x00A9F524 /* "align" */, align);
    }
    YesNoLine(out, 0x00AD565C /* "notify" */, Yes(Virtual<uint32_t>(face, kTextFlag, 1u)));
    YesNoLine(out, 0x00AD58D4 /* "wrapped" */, Yes(Virtual<uint32_t>(face, kTextFlag, 2u)));
    YesNoLine(out, 0x00AD58CC /* "opaque" */, Yes(Virtual<uint32_t>(face, kTextFlag, 4u)));
    uint32_t colour = 0;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    if (Yes(Virtual<uint32_t>(face, kTextColour, 0u, static_cast<void*>(&colour))))
        ColourLine(out, 0x00AD5664 /* "forecolor" */, colour, r, g, b);
    if (Yes(Virtual<uint32_t>(face, kTextColour, 1u, static_cast<void*>(&colour))))
        ColourLine(out, 0x00AD5670 /* "bkgcolor" */, colour, r, g, b);
    int8_t first = 0;
    int8_t second = 0;
    if (Yes(Virtual<uint32_t>(face, kTextGutters, static_cast<void*>(&first), static_cast<void*>(&second))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5654 /* "gutters" */, static_cast<int32_t>(first),
             static_cast<int32_t>(second));
    if (Yes(Virtual<uint32_t>(face, kTextOffsets, static_cast<void*>(&first), static_cast<void*>(&second))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5840 /* "textoffsets" */, static_cast<int32_t>(first),
             static_cast<int32_t>(second));
    AutofitLine(out, Virtual<void*>(face, kTextWindow));
    font.End(0);
    return 0;
}

// ====================================================================================
// GZWinTextEdit's writer (framework.uiscript.writers)

// IGZWinTextEdit's slots.
constexpr std::size_t kEditWindow = 0x14;        // slot 5 (its window)
constexpr std::size_t kEditFont = 0x2C;          // slot 11 (font*; AL)
constexpr std::size_t kEditColour = 0x38;        // slot 14 (state, colour*; AL)
constexpr std::size_t kEditCaretColour = 0x4C;   // slot 19 (colour*; AL)
constexpr std::size_t kEditHighlight = 0x58;     // slot 22 (the colour)
constexpr std::size_t kEditMaxText = 0x80;       // slot 32
constexpr std::size_t kEditOutline = 0x98;       // slot 38 (on byte*, colour*)
constexpr std::size_t kEditGutterX = 0x9C;       // slot 39
constexpr std::size_t kEditGutterY = 0xA4;       // slot 41
constexpr std::size_t kEditText = 0xB8;          // slot 46 (the characters)
constexpr std::size_t kEditInsertMode = 0x104;   // slot 65 (AL: insert, so `overwrite` no)
constexpr std::size_t kEditInsertIndex = 0x10C;  // slot 67 (index*; AL)
constexpr std::size_t kEditInsertPos = 0x114;    // slot 69 (x*, y*; AL)
constexpr std::size_t kEditCaretPeriod = 0x124;  // slot 73 (period*; AL)
constexpr std::size_t kEditFlags = 0x140;        // slot 80 (the flags)
constexpr std::size_t kEditMaxUndo = 0x148;      // slot 82
constexpr std::size_t kEditVScrollRect = 0x154;  // slot 85 (16 bytes*, rectangle*; AL)
constexpr std::size_t kEditHScrollRect = 0x158;  // slot 86 (16 bytes*, rectangle*; AL)

// The game's 0x0095CC7B (cdecl: the out string; the interface in EBX). In
// order:
//
// - the flags (slot 80) against the defaults 0x11FF by the flag table
//   0x00AD6D10 (0x0095C185: editable, wrapped, hscrollbar, vscrollbar,
//   outline, opaque, caretvisible, allowinsert and allowundo always
//   and singleline always written; notifyonreturn, notifyonchange,
//   notifyonlostfocus 0x8000, enableclipboard 0x1000 and passwordmode
//   0x2000 when they differ);
// - the font (slot 11, then LineFont into a string made for it) under
//   `font`;
// - `initvalue`, the text (slot 46) quoted (0x0095C028; the text's string
//   made before the name);
// - six colours (slot 14 with 0, 2, 4, 1, 3, 5: colorfontnormal,
//   colorfontdisabled, colorfonthilited, colorfontnormalbkg,
//   colorfontdisabledbkg, colorfonthilitedbkg), `caretcolor` (slot 19),
//   `highlightcolor` (slot 22, always) and `outlinecolor` (slot 38, when
//   its byte is set), one colour place and three byte places for all;
// - `maxtext` (slot 32), `gutters` (slots 39 and 41), `overwrite` (no when
//   slot 65 answers yes), `insertindex` (slot 67), `insertpos` (slot 69),
//   `caretperiod` (slot 73), `maxundo` (slot 82);
// - `hscrollimage` and `vscrollimage`: the window's (slot 5) properties
//   0x8B4BFEFC and 0x8B4BFEFD as 12 bytes (0x0095BAD7, the window in ECX
//   and EAX; one zeroed place for both), the last two words as a key;
// - `hscrollimageRect` and `vscrollimageRect` (slots 86 and 85, one
//   rectangle place) by 0x0095BF21;
// - `autofit`, from the window (slot 5);
//
// then the font's string destroyed. The places the game reads from its
// stack when a getter answers yes without writing (the font word, the
// colours, the outline byte, the index, position, period and rectangle;
// the colour bytes when the service writes none) are 0 here. Slots 85
// and 86 also get a 16-byte place the game shares with the font word,
// the period and the index; nothing reads it after.
uint32_t __cdecl TextEditBody(void* face, void* out)
{
    OPENSC4_FIRST("TextEdit");
    const uint32_t flags = Virtual<uint32_t>(face, kEditFlags);
    CallFlags(0x00AD6D10, out, flags, 0x11FFu);
    Text font;
    font.Make();
    uint32_t word = 0;
    if (Yes(Virtual<uint32_t>(face, kEditFont, static_cast<void*>(&word))))
        FontLine(out, font.bytes, word);
    {
        const char* const characters = Virtual<const char*>(face, kEditText);
        Text value;
        Text name;
        void* const valueMade = value.From(reinterpret_cast<std::uintptr_t>(characters));
        void* const nameMade = name.From(0x00AD59D8); // "initvalue"
        const uint32_t eax = Cdecl<uint32_t>(addr::cGZUIScriptWriters_LineQuoted, out, nameMade, valueMade);
        value.End(name.End(eax));
    }
    uint32_t colour = 0;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    static const struct {
        uint32_t state;
        std::uintptr_t name;
    } kStates[6] = {
        {0, 0x00AD5604}, // "colorfontnormal"
        {2, 0x00AD55F0}, // "colorfontdisabled"
        {4, 0x00AD55DC}, // "colorfonthilited"
        {1, 0x00AD55C8}, // "colorfontnormalbkg"
        {3, 0x00AD55B0}, // "colorfontdisabledbkg"
        {5, 0x00AD559C}, // "colorfonthilitedbkg"
    };
    for (const auto& state : kStates)
        if (Yes(Virtual<uint32_t>(face, kEditColour, state.state, static_cast<void*>(&colour))))
            ColourLine(out, state.name, colour, r, g, b);
    if (Yes(Virtual<uint32_t>(face, kEditCaretColour, static_cast<void*>(&colour))))
        ColourLine(out, 0x00AD59FC /* "caretcolor" */, colour, r, g, b);
    colour = Virtual<uint32_t>(face, kEditHighlight);
    ColourLine(out, 0x00AD59EC /* "highlightcolor" */, colour, r, g, b);
    uint8_t outline = 0;
    Virtual<uint32_t>(face, kEditOutline, static_cast<void*>(&outline), static_cast<void*>(&colour));
    if (outline)
        ColourLine(out, 0x00AD59C8 /* "outlinecolor" */, colour, r, g, b);
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD59E4 /* "maxtext" */, Virtual<uint32_t>(face, kEditMaxText));
    {
        const uint32_t x = Virtual<uint32_t>(face, kEditGutterX);
        const uint32_t y = Virtual<uint32_t>(face, kEditGutterY);
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5654 /* "gutters" */, x, y);
    }
    WordLine(out, 0x00AD59BC /* "overwrite" */, Yes(Virtual<uint32_t>(face, kEditInsertMode)) ? kNo : kYes);
    uint32_t index = 0;
    if (Yes(Virtual<uint32_t>(face, kEditInsertIndex, static_cast<void*>(&index))))
        Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD59B0 /* "insertindex" */, index);
    uint32_t x = 0;
    uint32_t y = 0;
    if (Yes(Virtual<uint32_t>(face, kEditInsertPos, static_cast<void*>(&x), static_cast<void*>(&y))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD59A4 /* "insertpos" */, x, y);
    uint32_t period = 0;
    if (Yes(Virtual<uint32_t>(face, kEditCaretPeriod, static_cast<void*>(&period))))
        Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5998 /* "caretperiod" */, period);
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5990 /* "maxundo" */, Virtual<uint32_t>(face, kEditMaxUndo));
    uint32_t bytes[3] = {0, 0, 0};
    static const std::uintptr_t kImages[2][2] = {
        {0x8B4BFEFC, 0x00AD5980}, // "hscrollimage"
        {0x8B4BFEFD, 0x00AD595C}, // "vscrollimage"
    };
    for (const auto& image : kImages) {
        if (Yes(CallPropertyBytes(Virtual<void*>(face, kEditWindow), static_cast<uint32_t>(image[0]), bytes)))
            Line(addr::cGZUIScriptWriters_LineKey, out, image[1], bytes[1], bytes[2]);
    }
    uint32_t place[4] = {0, 0, 0, 0};
    uint32_t rect[4] = {0, 0, 0, 0};
    if (Yes(Virtual<uint32_t>(face, kEditHScrollRect, static_cast<void*>(place), static_cast<void*>(rect))))
        Line(addr::cGZUIScriptWriters_LineRect, out, 0x00AD596C /* "hscrollimageRect" */, rect[0], rect[1], rect[2],
             rect[3]);
    if (Yes(Virtual<uint32_t>(face, kEditVScrollRect, static_cast<void*>(place), static_cast<void*>(rect))))
        Line(addr::cGZUIScriptWriters_LineRect, out, 0x00AD5948 /* "vscrollimageRect" */, rect[0], rect[1], rect[2],
             rect[3]);
    AutofitLine(out, Virtual<void*>(face, kEditWindow));
    font.End(0);
    return 0;
}

__declspec(naked) void TextEdit()
{
    __asm push dword ptr [esp + 4]
    __asm push ebx
    __asm call TextEditBody
    __asm add esp, 8
    __asm ret
}

// ====================================================================================
// GZWinListBox's writer (framework.uiscript.writers)

// IGZWinListBox's slots.
constexpr std::size_t kListBoxWindow = 0xC;          // slot 3 (its window)
constexpr std::size_t kListBoxSelection = 0x30;      // slot 12 (the initial selection)
constexpr std::size_t kListBoxSort = 0x3C;           // slot 15 (bool)
constexpr std::size_t kListBoxDrop = 0x48;           // slot 18 (bool)
constexpr std::size_t kListBoxCount = 0x78;          // slot 30 (the elements' count)
constexpr std::size_t kListBoxScrollbars = 0x90;     // slot 36 (two byte places; bool)
constexpr std::size_t kListBoxFontColour = 0xA0;     // slot 40 (which, colour*; bool)
constexpr std::size_t kListBoxHighlight = 0xAC;      // slot 43 (colour*; bool)
constexpr std::size_t kListBoxElement = 0xC4;        // slot 49 (index, string*; bool)
constexpr std::size_t kListBoxOutline = 0xD0;        // slot 52 (bool)
constexpr std::size_t kListBoxSunken = 0xD8;         // slot 54 (bool)
constexpr std::size_t kListBoxOutlineColours = 0xE0; // slot 56 (two colour*; bool)
constexpr std::size_t kListBoxBarGutters = 0xE8;     // slot 58 (two byte places; bool)
constexpr std::size_t kListBoxGutters = 0xF0;        // slot 60 (two byte places; bool)

// The game's 0x0095D439 (cdecl: the interface; the out string in EBX):
//
//   - slot 36's two bytes (when it answers) as `vscrollbar` and
//     `hscrollbar`, yes or no;
//   - a string made; the window's (slot 3) font into it by 0x0095BB82
//     with index 0, as `font` when there is one;
//   - slot 40 with 0, 1 and 2 (each when it answers) through the colour
//     service as `colorfontnormal`, `colorfontdisabled` and
//     `colorfonthilited`; slot 43 as `highlightcolor`; slot 56's two as
//     `outlinecolor` (two colours);
//   - slot 54 as `sunken` and slot 52 as `outline`, yes or no;
//   - slot 58's two bytes as `scrollbargutters` and slot 60's as
//     `gutters`, each signed, when the slot answers;
//   - slot 15 as `sort` and slot 18 as `drop`, yes or no; slot 12 as
//     `initselection` (%d);
//   - for each element (slot 30's count read once, signed), a string made,
//     slot 49 with the index and it, `listelement` quoted when it answers,
//     the string destroyed;
//   - the window's (slot 3 again) property 0xDACF8F9C: when its bool
//     (slot 63) is set, `autofit` alone (0x0095C07A); the value released;
//   - the font's string destroyed.
//
// The places the game never sets first, so a slot that answers without
// writing prints what its stack held (ours 0): the colour word and its
// three bytes (shared by every single colour and the outline's first, so
// later the last ones), the outline's second colour and bytes, the
// scrollbar bytes, and the gutters' bytes (shared by slots 58 and 60).
void __cdecl ListBoxBody(void* face, void* out)
{
    OPENSC4_FIRST("ListBox");
    uint8_t vertical = 0;
    uint8_t horizontal = 0;
    if (Yes(Virtual<uint32_t>(face, kListBoxScrollbars, static_cast<void*>(&vertical),
                              static_cast<void*>(&horizontal)))) {
        YesNoLine(out, 0x00AD5A3C /* "vscrollbar" */, vertical != 0);
        YesNoLine(out, 0x00AD5A48 /* "hscrollbar" */, horizontal != 0);
    }
    Text font;
    font.Make();
    if (Yes(CallPropertyFont(Virtual<void*>(face, kListBoxWindow), font.bytes, 0)))
        Line(addr::cGZUIScriptWriters_LineWord, out, 0x00AD567C /* "font" */, static_cast<void*>(font.bytes));
    uint32_t colour = 0;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    static const std::uintptr_t kFontColours[3] = {
        0x00AD5604, // "colorfontnormal"
        0x00AD55F0, // "colorfontdisabled"
        0x00AD55DC, // "colorfonthilited"
    };
    for (uint32_t which = 0; which < 3; ++which) {
        if (Yes(Virtual<uint32_t>(face, kListBoxFontColour, which, static_cast<void*>(&colour)))) {
            ColourBytes(colour, &r, &g, &b);
            Line(addr::cGZUIScriptWriters_LineColour, out, kFontColours[which], r, g, b);
        }
    }
    if (Yes(Virtual<uint32_t>(face, kListBoxHighlight, static_cast<void*>(&colour)))) {
        ColourBytes(colour, &r, &g, &b);
        Line(addr::cGZUIScriptWriters_LineColour, out, 0x00AD59EC /* "highlightcolor" */, r, g, b);
    }
    uint32_t second = 0;
    uint32_t r2 = 0;
    uint32_t g2 = 0;
    uint32_t b2 = 0;
    if (Yes(Virtual<uint32_t>(face, kListBoxOutlineColours, static_cast<void*>(&colour),
                              static_cast<void*>(&second)))) {
        ColourBytes(colour, &r, &g, &b);
        ColourBytes(second, &r2, &g2, &b2);
        Line(addr::cGZUIScriptWriters_LineColours2, out, 0x00AD59C8 /* "outlinecolor" */, r, g, b, r2, g2, b2);
    }
    YesNoLine(out, 0x00AD562C /* "sunken" */, Yes(Virtual<uint32_t>(face, kListBoxSunken)));
    YesNoLine(out, 0x00AD557C /* "outline" */, Yes(Virtual<uint32_t>(face, kListBoxOutline)));
    int8_t left = 0;
    int8_t right = 0;
    if (Yes(Virtual<uint32_t>(face, kListBoxBarGutters, static_cast<void*>(&left), static_cast<void*>(&right))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5AC4 /* "scrollbargutters" */,
             static_cast<uint32_t>(static_cast<int32_t>(left)), static_cast<uint32_t>(static_cast<int32_t>(right)));
    if (Yes(Virtual<uint32_t>(face, kListBoxGutters, static_cast<void*>(&left), static_cast<void*>(&right))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5654 /* "gutters" */,
             static_cast<uint32_t>(static_cast<int32_t>(left)), static_cast<uint32_t>(static_cast<int32_t>(right)));
    YesNoLine(out, 0x00ACA8D8 /* "sort" */, Yes(Virtual<uint32_t>(face, kListBoxSort)));
    YesNoLine(out, 0x00AD5AAC /* "drop" */, Yes(Virtual<uint32_t>(face, kListBoxDrop)));
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5AB4 /* "initselection" */,
         Virtual<uint32_t>(face, kListBoxSelection));
    const int32_t count = static_cast<int32_t>(Virtual<uint32_t>(face, kListBoxCount));
    for (int32_t index = 0; index < count; ++index) {
        Text element;
        element.Make();
        if (Yes(Virtual<uint32_t>(face, kListBoxElement, static_cast<uint32_t>(index),
                                  static_cast<void*>(element.bytes))))
            Line(addr::cGZUIScriptWriters_LineQuoted, out, 0x00AD5A80 /* "listelement" */,
                 static_cast<void*>(element.bytes));
        element.End(0);
    }
    AutofitLine(out, Virtual<void*>(face, kListBoxWindow));
    font.End(0);
}

__declspec(naked) void ListBox()
{
    __asm push ebx
    __asm push dword ptr [esp + 8]
    __asm call ListBoxBody
    __asm add esp, 8
    __asm ret
}

// ====================================================================================
// GZWinLineInput's writer (framework.uiscript.writers)

// IGZWinLineInput's slots.
constexpr std::size_t kLineInputWindow = 0xC;       // slot 3 (its window)
constexpr std::size_t kLineInputFontColour = 0x1C;  // slot 7 (which, colour*; bool)
constexpr std::size_t kLineInputFilled = 0x28;      // slot 10 (bool)
constexpr std::size_t kLineInputStyle = 0x30;       // slot 12 (style*)
constexpr std::size_t kLineInputColour = 0x3C;      // slot 15 (which, colour*; bool)
constexpr std::size_t kLineInputFillColour = 0x48;  // slot 18 (colour*; bool)
constexpr std::size_t kLineInputAlpha = 0x50;       // slot 20 (float*)
constexpr std::size_t kLineInputCaret = 0x68;       // slot 26 (int*; bool)
constexpr std::size_t kLineInputEditable = 0x6C;    // slot 27 (bool)
constexpr std::size_t kLineInputLimit = 0x78;       // slot 30 (int*; bool)
constexpr std::size_t kLineInputSelection = 0x80;   // slot 32 (start*, length*; bool)
constexpr std::size_t kLineInputGutters = 0x88;     // slot 34 (four byte places)

// The highlight's alpha as the game has it: the float times 255.0 (the
// game's 0x00A9D8B0) on the x87 and truncated by its _ftol (the precision
// is the FPU's).
int32_t LineInputAlpha(const float* alpha)
{
    const std::uintptr_t scale = addr::cGZUIScriptWriters_Float255;
    const std::uintptr_t ftol = addr::CRT_ftol;
    int32_t result;
    __asm {
        mov eax, alpha
        fld dword ptr [eax]
        mov ecx, scale
        fmul dword ptr [ecx]
        mov edx, ftol
        call edx
        mov result, eax
    }
    return result;
}

// The game's 0x0095DB1A (cdecl: the interface; the out string in EBX):
//
//   - a string made; the window's (slot 3) font into it by 0x0095BB82
//     with index 0, as `font` when there is one;
//   - slot 7 with 0, 1 and 2 (each when it answers) through the colour
//     service as `colorfontnormal`, `colorfontdisabled` and
//     `colorfonthilited`; slot 15 with 0 to 3 as `colorLT`, `colorRB`,
//     `colorLTinside` and `colorRBinside`; slot 18 as
//     `fillcolorhighlite`;
//   - slot 34's four bytes, always (no answer read), signed, as `gutters`
//     (a rectangle);
//   - slot 10 as `filled` and slot 27 as `editable`, yes or no;
//   - slot 12's style, always: 1 `normal`, 2 `outline3d`, 3 `outline3dEx`
//     under `style`, any other nothing;
//   - slot 20's float, always, times 255 truncated, as `fillalphahilite`;
//   - slot 26 as `caretpos`, slot 30 as `charlimit` (%d) when they answer;
//     slot 32's start and length, when it answers, as `selection`
//     (start, start + length);
//   - the font's string destroyed.
//
// The places the game never sets first, so a slot that writes nothing
// prints what its stack held (ours 0): the colour word and bytes (shared
// by the eight colours, so later the last ones), the four gutter bytes,
// the style, the float, the word caret position, character limit and
// selection start share, and the selection's length.
void __cdecl LineInputBody(void* face, void* out)
{
    OPENSC4_FIRST("LineInput");
    Text font;
    font.Make();
    if (Yes(CallPropertyFont(Virtual<void*>(face, kLineInputWindow), font.bytes, 0)))
        Line(addr::cGZUIScriptWriters_LineWord, out, 0x00AD567C /* "font" */, static_cast<void*>(font.bytes));
    uint32_t colour = 0;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    static const std::uintptr_t kFontColours[3] = {
        0x00AD5604, // "colorfontnormal"
        0x00AD55F0, // "colorfontdisabled"
        0x00AD55DC, // "colorfonthilited"
    };
    for (uint32_t which = 0; which < 3; ++which) {
        if (Yes(Virtual<uint32_t>(face, kLineInputFontColour, which, static_cast<void*>(&colour)))) {
            ColourBytes(colour, &r, &g, &b);
            Line(addr::cGZUIScriptWriters_LineColour, out, kFontColours[which], r, g, b);
        }
    }
    static const std::uintptr_t kColours[4] = {
        0x00AD6F20, // "colorLT"
        0x00AD6F18, // "colorRB"
        0x00AD6F08, // "colorLTinside"
        0x00AD6EF8, // "colorRBinside"
    };
    for (uint32_t which = 0; which < 4; ++which) {
        if (Yes(Virtual<uint32_t>(face, kLineInputColour, which, static_cast<void*>(&colour)))) {
            ColourBytes(colour, &r, &g, &b);
            Line(addr::cGZUIScriptWriters_LineColour, out, kColours[which], r, g, b);
        }
    }
    if (Yes(Virtual<uint32_t>(face, kLineInputFillColour, static_cast<void*>(&colour)))) {
        ColourBytes(colour, &r, &g, &b);
        Line(addr::cGZUIScriptWriters_LineColour, out, 0x00AD6EE4 /* "fillcolorhighlite" */, r, g, b);
    }
    int8_t gutters[4] = {0, 0, 0, 0};
    Virtual<uint32_t>(face, kLineInputGutters, static_cast<void*>(&gutters[0]), static_cast<void*>(&gutters[1]),
                      static_cast<void*>(&gutters[2]), static_cast<void*>(&gutters[3]));
    Line(addr::cGZUIScriptWriters_LineRect, out, 0x00AD5654 /* "gutters" */,
         static_cast<uint32_t>(static_cast<int32_t>(gutters[0])),
         static_cast<uint32_t>(static_cast<int32_t>(gutters[1])),
         static_cast<uint32_t>(static_cast<int32_t>(gutters[2])),
         static_cast<uint32_t>(static_cast<int32_t>(gutters[3])));
    YesNoLine(out, 0x00AD5E00 /* "filled" */, Yes(Virtual<uint32_t>(face, kLineInputFilled)));
    YesNoLine(out, 0x00AD5A54 /* "editable" */, Yes(Virtual<uint32_t>(face, kLineInputEditable)));
    uint32_t style = 0;
    Virtual<uint32_t>(face, kLineInputStyle, static_cast<void*>(&style));
    if (style == 1)
        WordLine(out, 0x00AD564C /* "style" */, 0x00AD5634 /* "normal" */);
    else if (style == 2)
        WordLine(out, 0x00AD564C /* "style" */, 0x00AD5DF4 /* "outline3d" */);
    else if (style == 3)
        WordLine(out, 0x00AD564C /* "style" */, 0x00AD6ED8 /* "outline3dEx" */);
    float alpha = 0.0f;
    Virtual<uint32_t>(face, kLineInputAlpha, static_cast<void*>(&alpha));
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5D98 /* "fillalphahilite" */,
         static_cast<uint32_t>(LineInputAlpha(&alpha)));
    uint32_t word = 0;
    if (Yes(Virtual<uint32_t>(face, kLineInputCaret, static_cast<void*>(&word))))
        Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5D8C /* "caretpos" */, word);
    if (Yes(Virtual<uint32_t>(face, kLineInputLimit, static_cast<void*>(&word))))
        Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5D80 /* "charlimit" */, word);
    uint32_t length = 0;
    if (Yes(Virtual<uint32_t>(face, kLineInputSelection, static_cast<void*>(&word), static_cast<void*>(&length))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5D74 /* "selection" */, word, word + length);
    font.End(0);
}

__declspec(naked) void LineInput()
{
    __asm push ebx
    __asm push dword ptr [esp + 8]
    __asm call LineInputBody
    __asm add esp, 8
    __asm ret
}

// ====================================================================================
// GZWinCombo's writer (framework.uiscript.writers)

// IGZWinCombo's slots.
constexpr std::size_t kComboWindow = 0xC;       // slot 3 (its window)
constexpr std::size_t kComboFontColour = 0x34;  // slot 13 (index, colour*: AL)
constexpr std::size_t kComboHighlight = 0x44;   // slot 17 (colour*: AL)
constexpr std::size_t kComboEditable = 0x50;    // slot 20 (AL)
constexpr std::size_t kComboOutline = 0x5C;     // slot 23 (colour*: AL)
constexpr std::size_t kComboSelection = 0x78;   // slot 30 (the initial selection)
constexpr std::size_t kComboItem = 0x7C;        // slot 31 (index, string*: AL)
constexpr std::size_t kComboArrowRect = 0x88;   // slot 34 (word*, four ints*: AL)
constexpr std::size_t kComboArrowColour = 0x90; // slot 36 (colour*: AL)
constexpr std::size_t kComboGutter = 0x98;      // slot 38 (signed byte*: AL)
constexpr std::size_t kComboGutters = 0xA0;     // slot 40 (two signed bytes*: AL)

// The game's 0x0095E1E2 (cdecl: the interface; the out string in EBX). In
// this order:
//   - the font: the interface's window (slot 3) gives property 0 to
//     0x0095BB82 with a string made first; when it answers, `font` as a
//     word line of that string;
//   - colorfontnormal, colorfontdisabled, colorfonthilited (slot 13 with 0
//     to 2), highlightcolor (17): each colour, when given, through the
//     colour service's slot 36 into three bytes, a colour line;
//   - editable (20): yes or no, always;
//   - outlinecolor (23), a colour line;
//   - initselection (30), an int line, always;
//   - combodownarrowimage: the window (slot 3 again) gives the pair
//     property 0 to 0x0095BB29; a key line of it when it answers;
//   - combodownarrowrect (34, with a word zeroed first): a rectangle line;
//   - combodowncolor (36), a colour line;
//   - buttongutter (38): a signed byte as an int line; gutters (40): two
//     signed bytes as a pair line;
//   - listelement: slot 31 with 0, 1, 2 ... into one string, a quoted line
//     for each until it answers no.
// The strings: the font's (made first) and the items' (made after the
// gutters) destroyed at the end, the items' first. Where the game reads
// its stack: the colour (one place for all six getters), its three bytes,
// the rectangle, the button gutter and the gutters' bytes when a getter
// answers yes and writes nothing; ours has 0 there (the colour and its
// bytes the last written, as the game's).
uint32_t __cdecl ComboBody(void* face, void* out)
{
    OPENSC4_FIRST("Combo");
    Text font;
    font.Make();
    if (Yes(CallPropertyFont(Virtual<void*>(face, kComboWindow), font.bytes, 0)))
        Line(addr::cGZUIScriptWriters_LineWord, out, 0x00AD567C /* "font" */, static_cast<void*>(font.bytes));
    uint32_t colour = 0;
    uint32_t bytes[3] = {};
    static const std::uintptr_t kFontColours[3] = {
        0x00AD5604, // "colorfontnormal"
        0x00AD55F0, // "colorfontdisabled"
        0x00AD55DC, // "colorfonthilited"
    };
    for (uint32_t i = 0; i < 3; ++i)
        if (Yes(Virtual<uint32_t>(face, kComboFontColour, i, static_cast<void*>(&colour))))
            ColourLine(out, kFontColours[i], colour, bytes);
    if (Yes(Virtual<uint32_t>(face, kComboHighlight, static_cast<void*>(&colour))))
        ColourLine(out, 0x00AD59EC /* "highlightcolor" */, colour, bytes);
    YesNoLine(out, 0x00AD5A54 /* "editable" */, Yes(Virtual<uint32_t>(face, kComboEditable)));
    if (Yes(Virtual<uint32_t>(face, kComboOutline, static_cast<void*>(&colour))))
        ColourLine(out, 0x00AD59C8 /* "outlinecolor" */, colour, bytes);
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5AB4 /* "initselection" */,
         Virtual<uint32_t>(face, kComboSelection));
    uint32_t first = 0;
    uint32_t second = 0;
    if (Yes(CallPropertyPair(Virtual<void*>(face, kComboWindow), &first, &second, 0)))
        Line(addr::cGZUIScriptWriters_LineKey, out, 0x00AD5B2C /* "combodownarrowimage" */, first, second);
    uint32_t word = 0;
    uint32_t rect[4] = {};
    if (Yes(Virtual<uint32_t>(face, kComboArrowRect, static_cast<void*>(&word), static_cast<void*>(rect))))
        Line(addr::cGZUIScriptWriters_LineRect, out, 0x00AD5B18 /* "combodownarrowrect" */, rect[0], rect[1],
             rect[2], rect[3]);
    if (Yes(Virtual<uint32_t>(face, kComboArrowColour, static_cast<void*>(&colour))))
        ColourLine(out, 0x00AD5B08 /* "combodowncolor" */, colour, bytes);
    int8_t gutter = 0;
    if (Yes(Virtual<uint32_t>(face, kComboGutter, static_cast<void*>(&gutter))))
        Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5AF8 /* "buttongutter" */,
             static_cast<uint32_t>(static_cast<int32_t>(gutter)));
    int8_t gutters[2] = {};
    if (Yes(Virtual<uint32_t>(face, kComboGutters, static_cast<void*>(&gutters[0]), static_cast<void*>(&gutters[1]))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5654 /* "gutters" */,
             static_cast<uint32_t>(static_cast<int32_t>(gutters[0])),
             static_cast<uint32_t>(static_cast<int32_t>(gutters[1])));
    Text item;
    item.Make();
    uint32_t index = 0;
    while (Yes(Virtual<uint32_t>(face, kComboItem, index, static_cast<void*>(item.bytes)))) {
        ++index;
        Line(addr::cGZUIScriptWriters_LineQuoted, out, 0x00AD5A80 /* "listelement" */, static_cast<void*>(item.bytes));
    }
    item.End(0);
    font.End(0);
    return 0;
}

__declspec(naked) void Combo()
{
    __asm push ebx
    __asm push dword ptr [esp + 8]
    __asm call ComboBody
    __asm add esp, 8
    __asm ret
}

// ====================================================================================
// GZWinBtn's writer (framework.uiscript.writers2)

// IGZWinBtn's slots.
constexpr std::size_t kBtnWindow = 0xC;      // slot 3 (its window)
constexpr std::size_t kBtnFlag = 0x10;       // slot 4 (mask: AL)
constexpr std::size_t kBtnStyle = 0x2C;      // slot 11 (the style)
constexpr std::size_t kBtnFont = 0x54;       // slot 21 (font*: AL)
constexpr std::size_t kBtnColour = 0x64;     // slot 25 (index, colour*: AL)
constexpr std::size_t kBtnAlign = 0x70;      // slot 28 (alignment*: AL)
constexpr std::size_t kBtnGutters = 0x7C;    // slot 31 (four ints*)
constexpr std::size_t kBtnTipText = 0x90;    // slot 36 (string*: AL)
constexpr std::size_t kBtnTipFlag = 0x9C;    // slot 39 (word*: AL)
constexpr std::size_t kBtnTipOffsets = 0xA4; // slot 41 (two ints*: AL)

// The button's flags (slot 4 with the mask): the word for a set flag, the
// word for a clear one, the name.
struct BtnFlag {
    uint32_t mask;
    std::uintptr_t on;
    std::uintptr_t off;
    std::uintptr_t name;
};

constexpr std::uintptr_t kBtnOn = 0x00A82B98;  // "on"
constexpr std::uintptr_t kBtnOff = 0x00A82B94; // "off"

// A sound's key from the window's property (slot 113, read directly): of a
// kind with 0x8000 its two words (slot 103), a key line when either is not
// 0; the value released (slot 2) whenever the window gave one.
void BtnSoundLine(void* face, void* out, uint32_t key, std::uintptr_t literal)
{
    void* value = nullptr;
    void* const window = Virtual<void*>(face, kBtnWindow);
    if (!Yes(Virtual<uint32_t>(window, kProperty, key, static_cast<void*>(&value))))
        return;
    if (Virtual<uint32_t>(value, kValueKind) & 0x8000) {
        const uint32_t* const pair = Virtual<const uint32_t*>(value, kValuePair);
        if (pair[0] != 0 || pair[1] != 0)
            Line(addr::cGZUIScriptWriters_LineKey, out, literal, pair[0], pair[1]);
    }
    Virtual<uint32_t>(value, kValueRelease);
}

// The game's 0x0095E6F0 (cdecl: the interface, the out string). In this
// order:
//   - image: a 12-byte key (zeroed first) from the window's (slot 3)
//     property 0x2B343BEC by 0x0095BAD7; a key line of its group and
//     instance when it answers;
//   - font (21): when given and 0x0095BC21 prints it into a string (made
//     after the image), a word line of that string;
//   - colorfontnormal, colorfontdisabled, colorfonthilited,
//     colorfontnormalbkg, colorfontdisabledbkg and colorfonthilitedbkg
//     (slot 25 with 0, 4, 2, 1, 5 and 3): each colour, when given, through
//     the colour service's slot 36, a colour line;
//   - ten flags (slot 4), each a word line always: toggle (1) and
//     triggerondown (2) on/off; showcaption (4), autosize (0x20),
//     wrapcaption (0x40), shiftcaption (0x80) and tips (0x100) yes/no; fill
//     (0x10), tipsdelay (0x200) and tipstimeout (0x400) no when set, yes
//     when clear;
//   - style (11): 1 label, 2 standard, 3 toggle, 4 radiocheck, else none;
//   - gutters (31): a rectangle line, always (the getter's answer not
//     read);
//   - tiptext (36, into a second string): a quoted line;
//   - tipres: the same 12-byte key (not zeroed again) from property
//     0x2B340FFE; a key line when it answers and its group or instance is
//     not 0;
//   - tipoffsets (41, into the gutters' first two places): a pair line;
//   - tipflag (39): a hex line;
//   - align (28): 0 center, 1 left, 2 right, else none;
//   - btnclicksnd (0x0A5BE39D) and btnupsnd (0x4A5BE2F8): key lines from
//     the window's properties.
// The tip text's string and then the font's destroyed at the end. Where
// the game reads its stack: the font word, the colour (one place) and its
// bytes, the gutters (when slot 31 writes nothing; the tip offsets share
// their first two places), the tip flag and the alignment when a getter
// answers yes and writes nothing; ours has 0 there.
uint32_t __cdecl Btn(void* face, void* out)
{
    OPENSC4_FIRST("Btn");
    uint32_t key[3] = {0, 0, 0};
    if (Yes(CallPropertyBytes(Virtual<void*>(face, kBtnWindow), 0x2B343BECu, key)))
        Line(addr::cGZUIScriptWriters_LineKey, out, 0x00A844A8 /* "image" */, key[1], key[2]);
    Text font;
    font.Make();
    uint32_t fontWord = 0;
    if (Yes(Virtual<uint32_t>(face, kBtnFont, static_cast<void*>(&fontWord))) &&
        Yes(CallFont(font.bytes, fontWord)))
        Line(addr::cGZUIScriptWriters_LineWord, out, 0x00AD567C /* "font" */, static_cast<void*>(font.bytes));
    uint32_t colour = 0;
    uint32_t bytes[3] = {};
    static const struct {
        uint32_t index;
        std::uintptr_t name;
    } kColours[6] = {
        {0, 0x00AD5604}, // "colorfontnormal"
        {4, 0x00AD55F0}, // "colorfontdisabled"
        {2, 0x00AD55DC}, // "colorfonthilited"
        {1, 0x00AD55C8}, // "colorfontnormalbkg"
        {5, 0x00AD55B0}, // "colorfontdisabledbkg"
        {3, 0x00AD559C}, // "colorfonthilitedbkg"
    };
    for (const auto& c : kColours)
        if (Yes(Virtual<uint32_t>(face, kBtnColour, c.index, static_cast<void*>(&colour))))
            ColourLine(out, c.name, colour, bytes);
    static const BtnFlag kFlags[] = {
        {0x001, kBtnOn, kBtnOff, 0x00AD5C98}, // "toggle"
        {0x002, kBtnOn, kBtnOff, 0x00AD5C88}, // "triggerondown"
        {0x004, kYes, kNo, 0x00AD5C7C},       // "showcaption"
        {0x010, kNo, kYes, 0x00AD561C},       // "fill"
        {0x020, kYes, kNo, 0x00AD5C70},       // "autosize"
        {0x040, kYes, kNo, 0x00AD5C64},       // "wrapcaption"
        {0x080, kYes, kNo, 0x00AD5C54},       // "shiftcaption"
        {0x100, kYes, kNo, 0x00AD5C4C},       // "tips"
        {0x200, kNo, kYes, 0x00AD5C40},       // "tipsdelay"
        {0x400, kNo, kYes, 0x00AD5C34},       // "tipstimeout"
    };
    for (const BtnFlag& flag : kFlags)
        WordLine(out, flag.name, Yes(Virtual<uint32_t>(face, kBtnFlag, flag.mask)) ? flag.on : flag.off);
    std::uintptr_t style = 0;
    switch (Virtual<uint32_t>(face, kBtnStyle)) {
    case 1:
        style = 0x00AD5C2C; // "label"
        break;
    case 2:
        style = 0x00AD5C20; // "standard"
        break;
    case 3:
        style = 0x00AD5C98; // "toggle"
        break;
    case 4:
        style = 0x00AD5B58; // "radiocheck"
        break;
    }
    if (style)
        WordLine(out, 0x00AD564C /* "style" */, style);
    uint32_t gutters[4] = {};
    Virtual<uint32_t>(face, kBtnGutters, static_cast<void*>(&gutters[0]), static_cast<void*>(&gutters[1]),
                      static_cast<void*>(&gutters[2]), static_cast<void*>(&gutters[3]));
    Line(addr::cGZUIScriptWriters_LineRect, out, 0x00AD5654 /* "gutters" */, gutters[0], gutters[1], gutters[2],
         gutters[3]);
    Text tip;
    tip.Make();
    if (Yes(Virtual<uint32_t>(face, kBtnTipText, static_cast<void*>(tip.bytes))))
        Line(addr::cGZUIScriptWriters_LineQuoted, out, 0x00AD5BD4 /* "tiptext" */, static_cast<void*>(tip.bytes));
    if (Yes(CallPropertyBytes(Virtual<void*>(face, kBtnWindow), 0x2B340FFEu, key)) &&
        (key[2] != 0 || key[1] != 0))
        Line(addr::cGZUIScriptWriters_LineKey, out, 0x00AD5BC0 /* "tipres" */, key[1], key[2]);
    if (Yes(Virtual<uint32_t>(face, kBtnTipOffsets, static_cast<void*>(&gutters[0]), static_cast<void*>(&gutters[1]))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5BC8 /* "tipoffsets" */, gutters[0], gutters[1]);
    uint32_t tipFlag = 0;
    if (Yes(Virtual<uint32_t>(face, kBtnTipFlag, static_cast<void*>(&tipFlag))))
        Line(addr::cGZUIScriptWriters_LineHex, out, 0x00AD5B7C /* "tipflag" */, tipFlag);
    uint32_t align = 0;
    if (Yes(Virtual<uint32_t>(face, kBtnAlign, static_cast<void*>(&align)))) {
        std::uintptr_t word = 0;
        switch (align) {
        case 0:
            word = 0x00AD5644; // "center"
            break;
        case 1:
            word = 0x00AA1BD8; // "left"
            break;
        case 2:
            word = 0x00AA1BCC; // "right"
            break;
        }
        if (word)
            WordLine(out, 0x00A9F524 /* "align" */, word);
    }
    BtnSoundLine(face, out, 0x0A5BE39D, 0x00AD5B70 /* "btnclicksnd" */);
    BtnSoundLine(face, out, 0x4A5BE2F8, 0x00AD5B64 /* "btnupsnd" */);
    tip.End(0);
    font.End(0);
    return 0;
}

// ====================================================================================
// GZWinBMP's writer (framework.uiscript.writers2)

// IGZWinBMP's slots.
constexpr std::size_t kBmpWindow = 0xC;   // slot 3 (its window)
constexpr std::size_t kBmpAlpha = 0x1C;   // slot 7 (on*, alpha* as a float)
constexpr std::size_t kBmpRect = 0x24;    // slot 9 (four ints*)
constexpr std::size_t kBmpFlag = 0x28;    // slot 10 (mask; AL)

// The alpha's float times 255.0 (0x00A9D8B0) on the x87, truncated by
// the game's _ftol, as the game has it.
uint32_t BmpAlphaOf(const float* alpha)
{
    const std::uintptr_t scale = addr::cGZUIScriptWriters_Float255;
    const std::uintptr_t ftol = addr::CRT_ftol;
    uint32_t result;
    __asm {
        mov eax, alpha
        fld dword ptr [eax]
        mov ecx, scale
        fmul dword ptr [ecx]
        mov edx, ftol
        call edx
        mov result, eax
    }
    return result;
}

// The game's 0x0095F2CA (cdecl: the interface, the out string): the image
// key, the window's property 0xAB428C5D as 12 bytes (0x0095BAD7; the
// three words zeroed first) printed as `image` (its second and third
// words); with flag 0x10 the rectangle (slot 9) as `imagerect`; flags 1,
// 2 and 8 as `notify`, `transparentbkg` and `edgeimage`; with the alpha
// on (slot 7), its float times 255 truncated, low byte, as `alpha`. The
// rectangle (in the game the words of the destroyed `image` name, or its
// stack), the alpha's byte and its float are the game's stack where the
// slots write nothing (ours 0).
void __cdecl BMP(void* face, void* out)
{
    OPENSC4_FIRST("BMP");
    uint32_t bytes[3] = {0, 0, 0};
    if (Yes(CallPropertyBytes(Virtual<void*>(face, kBmpWindow), 0xAB428C5D, bytes)))
        Line(addr::cGZUIScriptWriters_LineKey, out, 0x00A844A8 /* "image" */, bytes[1], bytes[2]);
    if (Yes(Virtual<uint32_t>(face, kBmpFlag, uint32_t{0x10}))) {
        int32_t rect[4] = {0, 0, 0, 0};
        Virtual<uint32_t>(face, kBmpRect, static_cast<void*>(rect));
        Line(addr::cGZUIScriptWriters_LineRect, out, 0x00AD5584 /* "imagerect" */, rect[0], rect[1], rect[2],
             rect[3]);
    }
    YesNoLine(out, 0x00AD565C /* "notify" */, Yes(Virtual<uint32_t>(face, kBmpFlag, uint32_t{1})));
    YesNoLine(out, 0x00AD5CC4 /* "transparentbkg" */, Yes(Virtual<uint32_t>(face, kBmpFlag, uint32_t{2})));
    YesNoLine(out, 0x00AD5CB8 /* "edgeimage" */, Yes(Virtual<uint32_t>(face, kBmpFlag, uint32_t{8})));
    uint8_t on = 0;
    float alpha = 0.0f;
    Virtual<uint32_t>(face, kBmpAlpha, static_cast<void*>(&on), static_cast<void*>(&alpha));
    if (on)
        Line(addr::cGZUIScriptWriters_LineInt, out, 0x00A9F5B0 /* "alpha" */, BmpAlphaOf(&alpha) & 0xFF);
}

// ====================================================================================
// GZWinSlider's writer (framework.uiscript.writers2)

// IGZWinSlider's slots.
constexpr std::size_t kSliderWindow = 0xC;     // slot 3 (its window)
constexpr std::size_t kSliderValue = 0x10;     // slot 4 (the value)
constexpr std::size_t kSliderMinimum = 0x1C;   // slot 7 (the minimum)
constexpr std::size_t kSliderMaximum = 0x24;   // slot 9 (the maximum)
constexpr std::size_t kSliderDirection = 0x2C; // slot 11 (direction*)

// The direction words (also the scroll bar's).
constexpr std::uintptr_t kDirection = 0x00AD5CF8;  // "direction"
constexpr std::uintptr_t kVertical = 0x00AA0108;   // "vertical"
constexpr std::uintptr_t kHorizontal = 0x00AD5CEC; // "horizontal"

// The game's 0x0095F4EC (the interface in EDI, the out string in EBX):
// slot 7 then slot 9 as `minmaxvalue` (a pair); the direction (slot 11)
// 2 as `direction=vertical`, 1 as `direction=horizontal`, else nothing;
// the image key, the window's property 0x6B4BCBD4 as 12 bytes (zeroed
// first; its second and third words) as `image`; slot 4 as `initvalue`.
// The direction is the game's stack where slot 11 writes nothing (ours 0).
uint32_t __cdecl SliderBody(void* face, void* out)
{
    OPENSC4_FIRST("Slider");
    const uint32_t minimum = Virtual<uint32_t>(face, kSliderMinimum);
    const uint32_t maximum = Virtual<uint32_t>(face, kSliderMaximum);
    Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5D04 /* "minmaxvalue" */, minimum, maximum);
    uint32_t direction = 0;
    Virtual<uint32_t>(face, kSliderDirection, static_cast<void*>(&direction));
    if (direction == 2)
        WordLine(out, kDirection, kVertical);
    else if (direction == 1)
        WordLine(out, kDirection, kHorizontal);
    uint32_t bytes[3] = {0, 0, 0};
    if (Yes(CallPropertyBytes(Virtual<void*>(face, kSliderWindow), 0x6B4BCBD4, bytes)))
        Line(addr::cGZUIScriptWriters_LineKey, out, 0x00A844A8 /* "image" */, bytes[1], bytes[2]);
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD59D8 /* "initvalue" */, Virtual<uint32_t>(face, kSliderValue));
    return 0;
}

__declspec(naked) void Slider()
{
    __asm push ebx
    __asm push edi
    __asm call SliderBody
    __asm add esp, 8
    __asm ret
}

// ====================================================================================
// GZWinScrollbar's writer (framework.uiscript.writers2)

// IGZWinScrollbar's slots.
constexpr std::size_t kScrollbarWindow = 0xC;     // slot 3 (its window)
constexpr std::size_t kScrollbarValue = 0x18;     // slot 6 (the value)
constexpr std::size_t kScrollbarRange = 0x20;     // slot 8 (minimum*, maximum*; AL)
constexpr std::size_t kScrollbarDirection = 0x28; // slot 10 (the direction)
constexpr std::size_t kScrollbarPage = 0x30;      // slot 12 (the page size)
constexpr std::size_t kScrollbarLine = 0x38;      // slot 14 (the line size)
constexpr std::size_t kScrollbarCounts = 0x40;    // slot 16 (lines*, pages*)

// The game's 0x0095F663 (the interface in EDI, the out string in EBX):
// the range (slot 8, when it answers yes) as `minmaxvalue`; the direction
// (slot 10) 2 as `direction=vertical`, 1 as `direction=horizontal`, else
// nothing; slot 12 as `pagesize`, slot 14 as `linesize`; slot 16's two
// words as `linepagecount` (always); the image key, the window's property
// 0x6B4BCBD4 as 12 bytes (zeroed first; its second and third words) as
// `image`; slot 6 as `initvalue`. The range and the counts are the game's
// stack where their slots write nothing (ours 0).
uint32_t __cdecl ScrollbarBody(void* face, void* out)
{
    OPENSC4_FIRST("Scrollbar");
    uint32_t range[2] = {0, 0};
    if (Yes(Virtual<uint32_t>(face, kScrollbarRange, static_cast<void*>(&range[0]), static_cast<void*>(&range[1]))))
        Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5D04 /* "minmaxvalue" */, range[0], range[1]);
    const uint32_t direction = Virtual<uint32_t>(face, kScrollbarDirection);
    if (direction == 2)
        WordLine(out, kDirection, kVertical);
    else if (direction == 1)
        WordLine(out, kDirection, kHorizontal);
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5D48 /* "pagesize" */, Virtual<uint32_t>(face, kScrollbarPage));
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5D3C /* "linesize" */, Virtual<uint32_t>(face, kScrollbarLine));
    uint32_t counts[2] = {0, 0};
    Virtual<uint32_t>(face, kScrollbarCounts, static_cast<void*>(&counts[0]), static_cast<void*>(&counts[1]));
    Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5D2C /* "linepagecount" */, counts[0], counts[1]);
    uint32_t bytes[3] = {0, 0, 0};
    if (Yes(CallPropertyBytes(Virtual<void*>(face, kScrollbarWindow), 0x6B4BCBD4, bytes)))
        Line(addr::cGZUIScriptWriters_LineKey, out, 0x00A844A8 /* "image" */, bytes[1], bytes[2]);
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD59D8 /* "initvalue" */,
         Virtual<uint32_t>(face, kScrollbarValue));
    return 0;
}

__declspec(naked) void Scrollbar()
{
    __asm push ebx
    __asm push edi
    __asm call ScrollbarBody
    __asm add esp, 8
    __asm ret
}

// ====================================================================================
// GZWinOptGrp's writer and its options' callback (framework.uiscript.writers2)

// IGZWinBtn's slots the callback calls (an option is a button).
constexpr std::size_t kOptGrpOptionWindow = 0xC;   // slot 3 (its window)
constexpr std::size_t kOptGrpOptionRect = 0x40;    // slot 16 (four ints*: the image's rectangle)
constexpr std::size_t kOptGrpOptionCaption = 0xC8; // slot 50 (its caption, a cIGZString)
// The window's slot 47 (four ints*: its area).
constexpr std::size_t kOptGrpWindowArea = 0xBC;

// The game's 0x0095F87B (cdecl: the group's window, the option's id, the
// option's IGZWinBtn, the out string), the callback the writer hands the
// window's slot 32 for each IGZWinBtn child. Without a button nothing.
// Else: the caption (slot 50) copied into a string (SC4String::
// FromIGZString); the image key, the window's pair 0 (0x0095BB29; the
// two words zeroed first, the second in the place of the button
// argument); the image's rectangle (slot 16); the window's area (its slot
// 47, through slot 3 again); `option` printed by 0x0095C0A1 with the id,
// the caption, number 0, the key and the image's rectangle; the area's
// left and top as `optionmoveto` and its width and height (right - left,
// bottom - top) as `optionsetsize` (0x0095C130); the caption's characters
// freed (the game leaves its vtable). AL 1 (the window's slot 32 counts
// the AL 0 answers and reads nothing more; the game's upper bytes are
// the last call's EAX, or the EAX it came with for a null button). The two rectangles are the
// game's stack where their slots write nothing (ours 0).
uint32_t __cdecl OptGrpOption(void*, uint32_t id, void* button, void* out)
{
    OPENSC4_FIRST("OptGrpOption");
    if (!button)
        return 1;
    Text caption;
    CallThis<void*>(addr::SC4String_FromIGZString, caption.bytes, Virtual<void*>(button, kOptGrpOptionCaption));
    uint32_t group = 0;
    uint32_t instance = 0;
    CallPropertyPair(Virtual<void*>(button, kOptGrpOptionWindow), &group, &instance, 0);
    int32_t rect[4] = {0, 0, 0, 0};
    Virtual<uint32_t>(button, kOptGrpOptionRect, static_cast<void*>(rect));
    int32_t area[4] = {0, 0, 0, 0};
    Virtual<uint32_t>(Virtual<void*>(button, kOptGrpOptionWindow), kOptGrpWindowArea, static_cast<void*>(area));
    {
        Text name;
        void* const made = name.From(0x00ABBE78 /* "option" */);
        name.End(CallOption(made, caption.bytes, out, id, 0, group, instance, rect));
    }
    Line(addr::cGZUIScriptWriters_LineMove, out, 0x00AD65CC /* "optionmoveto" */, id, area[0], area[1]);
    Line(addr::cGZUIScriptWriters_LineMove, out, 0x00AD65BC /* "optionsetsize" */, id, area[2] - area[0],
         area[3] - area[1]);
    return Low(caption.End(0), 1);
}

// IGZWinOptGrp's slots.
constexpr std::size_t kOptGrpWindow = 0xC;    // slot 3 (its window)
constexpr std::size_t kOptGrpFlag = 0x10;     // slot 4 (mask; AL)
constexpr std::size_t kOptGrpGutters = 0x1C;  // slot 7 (two ints*)
constexpr std::size_t kOptGrpColour = 0x2C;   // slot 11 (index, colour*; AL)
constexpr std::size_t kOptGrpSelected = 0x34; // slot 13 (the initial selection)
constexpr std::size_t kOptGrpOutline = 0x58;  // slot 22 (four colours*; AL)
constexpr std::size_t kOptGrpAutofill = 0x74; // slot 29 (manual*, size*; AL)
// The window's slot 32 (each child of an interface to a callback with
// the window, the child's id, the child's interface and a word; `ret
// 0xC`).
constexpr std::size_t kOptGrpChildren = 0x80;

// The game's 0x0095F9B0 (cdecl: the interface, the out string): flags 1,
// 2, 4 and 8 as `maincaption`, `itemcaption`, `drawfill` and `outline`;
// the gutters (slot 7, unchecked) as `gutters`; a string made, the
// window's fonts 0 and 1 printed into it (0x0095BB82) as `labelfont` and
// `buttonfont` (the second over the first); the window's property
// 0x4A4AF4AC, when its kind has 0x8000 and its pair (slot 103) is not
// (0, 0), as `captiontextresid`; the window's pair 0 (0x0095BB29) as
// `backimage`; colours 0 to 4 (slot 11, each through the colour service)
// as `labelfontcolornormal`, `labelfontcolordisabled`,
// `buttonfontcolornormal`, `buttonfontcolorhilited` and
// `buttonfontcolordisabled`; the four outline colours (slot 22) as
// `outlinecolor`; slot 13 as `initselection`; the autofill (slot 29):
// manual 0 as `autofilltype=automatic` and its size as `autofillsize`,
// else `autofilltype=manual`; the window's property 0xDACF8F9C whose bool
// (slot 63) is set as `autofit` (0x0095C07A); then the window's slot 32
// with IGZWinBtn 0x8810, the callback 0x0095F87B (the game's address,
// ours when the group is on) and the out string; the string destroyed.
// The colours' bytes share their places (a service that writes nothing
// leaves the last ones). The game's stack where a slot answers without
// writing (ours 0): the gutters, the shared colour and its bytes before
// their first write, the outline's other three colours and their nine
// bytes, the autofill words. The backimage's
// second word goes into the interface argument's slot in the game's; ours
// keeps it in a local. EAX: the dispatcher reads none.
void __cdecl OptGrp(void* face, void* out)
{
    OPENSC4_FIRST("OptGrp");
    YesNoLine(out, 0x00AD66D8 /* "maincaption" */, Yes(Virtual<uint32_t>(face, kOptGrpFlag, uint32_t{1})));
    YesNoLine(out, 0x00AD66CC /* "itemcaption" */, Yes(Virtual<uint32_t>(face, kOptGrpFlag, uint32_t{2})));
    YesNoLine(out, 0x00AD66C0 /* "drawfill" */, Yes(Virtual<uint32_t>(face, kOptGrpFlag, uint32_t{4})));
    YesNoLine(out, 0x00AD557C /* "outline" */, Yes(Virtual<uint32_t>(face, kOptGrpFlag, uint32_t{8})));
    uint32_t gutters[2] = {0, 0};
    Virtual<uint32_t>(face, kOptGrpGutters, static_cast<void*>(&gutters[0]), static_cast<void*>(&gutters[1]));
    Line(addr::cGZUIScriptWriters_LinePair, out, 0x00AD5654 /* "gutters" */, gutters[0], gutters[1]);
    Text font;
    font.Make();
    if (Yes(CallPropertyFont(Virtual<void*>(face, kOptGrpWindow), font.bytes, 0)))
        Line(addr::cGZUIScriptWriters_LineWord, out, 0x00AD66B4 /* "labelfont" */, static_cast<void*>(font.bytes));
    if (Yes(CallPropertyFont(Virtual<void*>(face, kOptGrpWindow), font.bytes, 1)))
        Line(addr::cGZUIScriptWriters_LineWord, out, 0x00AD66A8 /* "buttonfont" */, static_cast<void*>(font.bytes));
    void* value = nullptr;
    if (Yes(Virtual<uint32_t>(Virtual<void*>(face, kOptGrpWindow), kProperty, uint32_t{0x4A4AF4AC},
                              static_cast<void*>(&value)))) {
        if (Virtual<uint32_t>(value, kValueKind) & 0x8000) {
            const uint32_t* const pair = Virtual<const uint32_t*>(value, kValuePair);
            if (pair[0] != 0 || pair[1] != 0)
                Line(addr::cGZUIScriptWriters_LineKey, out, 0x00AD661C /* "captiontextresid" */, pair[0], pair[1]);
        }
        Virtual<uint32_t>(value, kValueRelease);
    }
    uint32_t group = 0;
    uint32_t instance = 0;
    if (Yes(CallPropertyPair(Virtual<void*>(face, kOptGrpWindow), &group, &instance, 0)))
        Line(addr::cGZUIScriptWriters_LineKey, out, 0x00AD6610 /* "backimage" */, group, instance);
    static const std::uintptr_t kColours[5] = {
        0x00AD6690, // "labelfontcolornormal"
        0x00AD6678, // "labelfontcolordisabled"
        0x00AD6660, // "buttonfontcolornormal"
        0x00AD6648, // "buttonfontcolorhilited"
        0x00AD6630, // "buttonfontcolordisabled"
    };
    // The first colour's places, shared by the five and the outline's
    // first.
    uint32_t colour = 0;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    for (uint32_t i = 0; i < 5; ++i) {
        if (!Yes(Virtual<uint32_t>(face, kOptGrpColour, i, static_cast<void*>(&colour))))
            continue;
        ColourBytes(colour, &r, &g, &b);
        Line(addr::cGZUIScriptWriters_LineColour, out, kColours[i], r, g, b);
    }
    uint32_t colours[4] = {colour, 0, 0, 0};
    if (Yes(Virtual<uint32_t>(face, kOptGrpOutline, static_cast<void*>(&colours[0]), static_cast<void*>(&colours[1]),
                              static_cast<void*>(&colours[2]), static_cast<void*>(&colours[3])))) {
        uint32_t bytes[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        ColourBytes(colours[0], &r, &g, &b);
        ColourBytes(colours[1], &bytes[0], &bytes[1], &bytes[2]);
        ColourBytes(colours[2], &bytes[3], &bytes[4], &bytes[5]);
        ColourBytes(colours[3], &bytes[6], &bytes[7], &bytes[8]);
        Line(addr::cGZUIScriptWriters_LineColours4, out, 0x00AD59C8 /* "outlinecolor" */, r, g, b, bytes[0], bytes[1],
             bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8]);
    }
    Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD5AB4 /* "initselection" */,
         Virtual<uint32_t>(face, kOptGrpSelected));
    uint32_t manual = 0;
    uint32_t size = 0;
    if (Yes(Virtual<uint32_t>(face, kOptGrpAutofill, static_cast<void*>(&manual), static_cast<void*>(&size)))) {
        if (manual == 0) {
            WordLine(out, 0x00AD6600 /* "autofilltype" */, 0x00AD65F4 /* "automatic" */);
            Line(addr::cGZUIScriptWriters_LineInt, out, 0x00AD65DC /* "autofillsize" */, size);
        } else {
            WordLine(out, 0x00AD6600 /* "autofilltype" */, 0x00AD65EC /* "manual" */);
        }
    }
    AutofitLine(out, Virtual<void*>(face, kOptGrpWindow));
    Virtual<uint32_t>(Virtual<void*>(face, kOptGrpWindow), kOptGrpChildren, uint32_t{0x8810},
                      static_cast<std::uintptr_t>(addr::cGZUIScriptWriters_OptGrpOption), out);
    font.End(0);
}


// ====================================================================================
// Install

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

bool InstallLines(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWriters_LineFont, reinterpret_cast<void*>(&LineFont)},
        {addr::cGZUIScriptWriters_KeyEquals, reinterpret_cast<void*>(&KeyEqualsThunk)},
        {addr::cGZUIScriptWriters_LineColour, reinterpret_cast<void*>(&LineColour)},
        {addr::cGZUIScriptWriters_LineColours2, reinterpret_cast<void*>(&LineColours2)},
        {addr::cGZUIScriptWriters_LineColours4, reinterpret_cast<void*>(&LineColours4)},
        {addr::cGZUIScriptWriters_LineInt, reinterpret_cast<void*>(&LineInt)},
        {addr::cGZUIScriptWriters_LineHex, reinterpret_cast<void*>(&LineHex)},
        {addr::cGZUIScriptWriters_LineKey, reinterpret_cast<void*>(&LineKey)},
        {addr::cGZUIScriptWriters_LinePair, reinterpret_cast<void*>(&LinePair)},
        {addr::cGZUIScriptWriters_LineRect, reinterpret_cast<void*>(&LineRect)},
        {addr::cGZUIScriptWriters_LineWord, reinterpret_cast<void*>(&LineWord)},
        {addr::cGZUIScriptWriters_LineBool, reinterpret_cast<void*>(&LineBool)},
        {addr::cGZUIScriptWriters_LineQuoted, reinterpret_cast<void*>(&LineQuoted)},
        {addr::cGZUIScriptWriters_LineAppend, reinterpret_cast<void*>(&LineAppend)},
        {addr::cGZUIScriptWriters_LineOption, reinterpret_cast<void*>(&LineOption)},
        {addr::cGZUIScriptWriters_LineMove, reinterpret_cast<void*>(&LineMove)},
        {addr::cGZUIScriptWriters_LineFlags, reinterpret_cast<void*>(&LineFlags)},
    };
    return InstallAll(detours);
}

bool InstallProps(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWriters_PropertyBytes, reinterpret_cast<void*>(&PropertyBytes)},
        {addr::cGZUIScriptWriters_PropertyPair, reinterpret_cast<void*>(&PropertyPair)},
        {addr::cGZUIScriptWriters_PropertyFont, reinterpret_cast<void*>(&PropertyFont)},
    };
    return InstallAll(detours);
}

bool InstallWriters(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWriters_FlatRect, reinterpret_cast<void*>(&FlatRect)},
        {addr::cGZUIScriptWriters_Text, reinterpret_cast<void*>(&TextWriter)},
        {addr::cGZUIScriptWriters_TextEdit, reinterpret_cast<void*>(&TextEdit)},
        {addr::cGZUIScriptWriters_ListBox, reinterpret_cast<void*>(&ListBox)},
        {addr::cGZUIScriptWriters_LineInput, reinterpret_cast<void*>(&LineInput)},
        {addr::cGZUIScriptWriters_Combo, reinterpret_cast<void*>(&Combo)},
    };
    return InstallAll(detours);
}

bool InstallWriters2(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWriters_Btn, reinterpret_cast<void*>(&Btn)},
        {addr::cGZUIScriptWriters_BMP, reinterpret_cast<void*>(&BMP)},
        {addr::cGZUIScriptWriters_Slider, reinterpret_cast<void*>(&Slider)},
        {addr::cGZUIScriptWriters_Scrollbar, reinterpret_cast<void*>(&Scrollbar)},
        {addr::cGZUIScriptWriters_OptGrp, reinterpret_cast<void*>(&OptGrp)},
        {addr::cGZUIScriptWriters_OptGrpOption, reinterpret_cast<void*>(&OptGrpOption)},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.uiscript.writelines", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWriters_LineColour,
                        "Our printers of the UI script writers: a setting's line `name=value ` in each of its 14 "
                        "forms, a font's name, the flag table and a key compare (17 detours; part u17, "
                        "docs/gzui.md)",
                        false, InstallLines}),
    registry::Register({"framework.uiscript.writeprops", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWriters_PropertyBytes,
                        "Our property helpers of the UI script writers: a window's script property read as 12 "
                        "bytes, a pair of words or a font or text (3 detours; part u17, docs/gzui.md)",
                        false, InstallProps}),
    registry::Register({"framework.uiscript.writers", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWriters_FlatRect,
                        "Our UI script writers of GZWinFlatRect, Text, TextEdit, ListBox, LineInput and Combo: a "
                        "window's settings written back as script lines (6 detours; part u17, docs/gzui.md)",
                        false, InstallWriters}),
    registry::Register({"framework.uiscript.writers2", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWriters_Btn,
                        "Our UI script writers of GZWinBtn, BMP, Slider, Scrollbar and OptGrp (with its options' "
                        "callback): a window's settings written back as script lines (6 detours; part u17, "
                        "docs/gzui.md)",
                        false, InstallWriters2}),
};

} // namespace
} // namespace opensc4::gzui
