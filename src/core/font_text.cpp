// Our font's text encodings and Thai, the fourth slice of f3 of
// docs/fonts.md ("f3d: the font's encodings and Thai are ours"): six more
// functions of class 0x1AA404AC (vtable 0x00AC55D8), detours in one group,
// off by default and installed at Attach:
//
//   framework.font.font.text   ToUnicode 0x00899BCA (every string the font
//                              lays out, measures or draws passes it),
//                              Convert 0x00899A15, HasThai 0x00896425, the
//                              Thai mark's kind 0x008963C9, the Thai pass
//                              0x00899665 and the Thai presentation form
//                              0x0089645A
//
// The text's conversion between encodings (0x00910E18), the Thai classes
// (0x0091169F), the vectors' code (the words' resize 0x0049D0C0 and
// push_back 0x00800540, the pointers' push_back 0x007EBD30 and insert
// 0x00645CA0, the erase 0x0040E490), the CRT's memcpy and f3b's containers
// (the text's find 0x00897599, reserve 0x008986E3 and insert 0x00897BDC)
// stay the game's, called at their addresses, as are these six when they
// call one another (so with the group off its functions are the game's
// wherever they are called from).
//
// The font's fields are listed in font_object.cpp; these use +0x7C (the
// encoding, 0x10 UTF-16), the six vectors of words at +0x90 (one per
// caller: ToUnicode's `which`), the last of them, +0xCC, the Thai pass's,
// and +0xD8, a vector of double words: for each word of the text the Thai
// pass made, the index of the word of its input it came from.
//
// Compare mode: the mark's kind, HasThai and the form compare their answer.
// ToUnicode, Convert and the Thai pass run ours first on scratch copies
// (the font's 0x15C bytes with empty vectors of its own, a copy of the
// vector given, a copy of the text), then the game's on what they were
// called with, and compare what each answered and wrote: the count, the
// words of the text, where it is (the input or which vector), what was put
// in the offsets. While the game's side runs, our detours inside it pass
// through to the game's.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

namespace opensc4::fonttext {
namespace {

// ====================================================================================
// Helpers

using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }
inline void* Ptr(Address at) { return reinterpret_cast<void*>(static_cast<uintptr_t>(at)); }

template <typename T>
inline T Load(Address at)
{
    T value;
    std::memcpy(&value, Ptr(at), sizeof value);
    return value;
}

template <typename T>
inline void Store(Address at, T value)
{
    std::memcpy(Ptr(at), &value, sizeof value);
}

// A thiscall game function: `self` in ECX, the rest on the stack.
template <typename R, typename... Args>
inline R This(std::uintptr_t fn, void* self, Args... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(self, nullptr, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t fn, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(fn)(args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fonttext: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions we call

constexpr std::uintptr_t kTextConvert = 0x00910E18;   // cdecl (from, count, encoding, to, room*, to encoding)
constexpr std::uintptr_t kThaiClass = 0x0091169F;     // cdecl (character): its Thai class, 0 for none
constexpr std::uintptr_t kWordsResize = 0x0049D0C0;   // thiscall (count), ret 4: a vector of words, new ones 0
constexpr std::uintptr_t kWordsPushBack = 0x00800540; // thiscall (const word*), ret 4
constexpr std::uintptr_t kVectorErase = 0x0040E490;   // thiscall (first, last), ret 8

// These functions, at their addresses (ours when the group is on).
constexpr std::uintptr_t kConvert = 0x00899A15;
constexpr std::uintptr_t kHasThai = 0x00896425;
constexpr std::uintptr_t kMarkKind = 0x008963C9;
constexpr std::uintptr_t kThai = 0x00899665;
constexpr std::uintptr_t kForm = 0x0089645A;

// ====================================================================================
// Layout

namespace font {
constexpr uint32_t kSize = 0x15C;
constexpr Address kEncoding = 0x7C;
// The six vectors of words, +0x90 + 0xC x which; the Thai pass's is the
// last.
constexpr Address kTexts = 0x90;
constexpr Address kThaiText = 0xCC;
constexpr Address kOffsets = 0xD8;
// The vectors' place, as compare mode empties them on its scratch font.
constexpr Address kVectorsEnd = 0xE4;
} // namespace font

// A vector: begin, end, the end of its room.
constexpr Address kEnd = 4, kRoom = 8;
constexpr uint32_t kVectorBytes = 0xC;

constexpr uint32_t kUtf16 = 0x10;
constexpr uint16_t kSaraAm = 0x0E33, kSaraAa = 0x0E32, kNikhahit = 0x0E4D, kYoYing = 0x0E0D, kThanthakhat = 0x0E4C;
constexpr uint32_t kToneClass = 9;

// ====================================================================================
// The functions

// The game's 0x008963C9 (ret 4, this unread): 1 for the characters the Thai
// pass may change (YO YING 0x0E0D, MAI HAN-AKAT 0x0E31, SARA AM 0x0E33,
// the upper vowels 0x0E34 to 0x0E37, MAITAIKHU 0x0E47, NIKHAHIT 0x0E4D), 3
// for the tone marks and THANTHAKHAT (0x0E48 to 0x0E4C), else 0.
uint32_t MarkKind(uint16_t c)
{
    if (c < 0x0E0D || c > 0x0E4D)
        return 0;
    if (c <= 0x0E37)
        return c >= 0x0E34 || c == 0x0E0D || c == 0x0E31 || c == 0x0E33 ? 1 : 0;
    if (c == 0x0E47)
        return 1;
    if (c < 0x0E47)
        return 0;
    if (c <= 0x0E4C)
        return 3;
    return c == 0x0E4D ? 1 : 0;
}

// The game's 0x00896425 (ret 8, `this` handed on): whether any word of the
// text has a mark's kind (0x008963C9), to the text's end as an address
// (32-bit, compared unsigned).
bool HasThai(void* self, Address text, uint32_t count)
{
    const Address end = text + count * 2;
    for (Address p = text; p < end; p += 2) {
        if (This<uint32_t>(kMarkKind, self, uint32_t{Load<uint16_t>(p)}) != 0)
            return true;
    }
    return false;
}

// The game's 0x0089645A (ret 8, this unread): the presentation form a Thai
// mark is drawn with (the fonts' private characters 0xF701 to 0xF717): kind
// 1 shifted left (YO YING 1 is its form without the tail), 2 lowered, 3
// both, only for the tone marks and THANTHAKHAT; the character as it came
// for anything else.
uint16_t Form(uint16_t c, uint32_t kind)
{
    switch (c) {
    case 0x0E0D:
        return kind == 1 ? 0xF70F : c;
    case 0x0E31:
        return kind == 1 ? 0xF710 : c;
    case 0x0E34:
    case 0x0E35:
    case 0x0E36:
    case 0x0E37:
        return kind == 1 ? static_cast<uint16_t>(0xF701 + (c - 0x0E34)) : c;
    case 0x0E47:
        return kind == 1 ? 0xF712 : c;
    case 0x0E48:
    case 0x0E49:
    case 0x0E4A:
    case 0x0E4B:
    case 0x0E4C: {
        const uint16_t step = static_cast<uint16_t>(c - 0x0E48);
        if (kind == 1)
            return static_cast<uint16_t>(0xF713 + step);
        if (kind == 2)
            return static_cast<uint16_t>(0xF70A + step);
        if (kind == 3)
            return static_cast<uint16_t>(0xF705 + step);
        return c;
    }
    case 0x0E4D:
        return kind == 1 ? 0xF711 : c;
    default:
        return c;
    }
}

// The game's 0x00899A15 (ret 0xC): the text in the font's encoding into the
// vector as UTF-16. For UTF-16 the vector resized to the count and the
// words copied (the CRT's memcpy); else 0x00910E18 converts into the
// vector's words, as many as its size (signed, halved) holds, and gives the
// count written. Answers the count.
uint32_t Convert(void* self, Address text, uint32_t count, Address vector)
{
    const uint32_t encoding = Load<uint32_t>(At(self) + font::kEncoding);
    if (encoding == kUtf16) {
        This<void>(kWordsResize, Ptr(vector), count);
        Cdecl<void*>(addr::CRT_memcpy, Ptr(Load<Address>(vector)), Ptr(text), count * 2);
        return count;
    }
    const Address begin = Load<Address>(vector);
    uint32_t room = static_cast<uint32_t>(static_cast<int32_t>(Load<Address>(vector + kEnd) - begin) >> 1);
    Cdecl<uint32_t>(kTextConvert, text, count, encoding, begin, &room, kUtf16);
    return room;
}

// The words of the vector (begin, end), as the game counts them (signed,
// halved).
uint32_t WordsOf(Address vector)
{
    return static_cast<uint32_t>(static_cast<int32_t>(Load<Address>(vector + kEnd) - Load<Address>(vector)) >> 1);
}

// Whether the tall consonants' tails reach the marks' place: PO PLA, FO FA,
// FO FAN, LO CHULA.
bool Tall(uint16_t c) { return c == 0x0E1B || c == 0x0E1D || c == 0x0E1F || c == 0x0E2C; }

// The index of a word, as the Thai pass keeps it (its byte offset, signed,
// halved), pushed on the offsets (+0xD8).
void PushOffset(Address at, uint32_t offset)
{
    uint32_t index = static_cast<uint32_t>(static_cast<int32_t>(offset) >> 1);
    This<void>(addr::SC4PtrVector_PushBack, Ptr(at + font::kOffsets), &index);
}

// The game's 0x00899665 (ret 0x14): the Thai pass over UTF-16 text. *text
// and *count are first the input. While the text has a SARA AM (the find
// 0x00897599), it is rebuilt into +0xCC (reserved for twice the count,
// emptied): each SARA AM becomes NIKHAHIT and SARA AA, the NIKHAHIT put
// before the word last rebuilt when the word before the SARA AM is a tone
// mark (class 9); with `keep` each word rebuilt pushes the index of its
// input word on the offsets (+0xD8; a NIKHAHIT put before is inserted
// before the last offset), and every rebuild pushes the count of its input
// there whatever `keep` is. The rebuilt text is then the input, `keep` off.
// Then, in place where *text points, each mark that needs it takes its
// form (0x0089645A): YO YING before a lower vowel (classes 6 to 8) its form
// 1; upper vowels and marks (classes 9 to 11 and 13 to 15) form 1 after a
// tall consonant or after a word that had form 1, and tone marks (and
// THANTHAKHAT) after a word of a class below 10 two more; a lower vowel
// (6, 7) after a tall consonant passes its form 1 on to the next word.
void Thai(void* self, Address text, uint32_t count, Address* outText, uint32_t* outCount, uint32_t keep)
{
    const Address at = At(self);
    *outText = text;
    *outCount = count;
    if (count == 0)
        return;
    const uint32_t sought = kSaraAm;
    const Address words = at + font::kThaiText;
    for (;;) {
        const Address end = text + count * 2;
        if (Cdecl<Address>(addr::GZFontText_FindTagged, text, end, &sought) == end)
            break;
        if (static_cast<uint32_t>(static_cast<int32_t>(Load<Address>(at + 0xD4) - Load<Address>(words)) >> 1) < count * 2)
            This<void>(addr::GZFontText_Reserve, Ptr(words), count * 2);
        This<void>(kWordsResize, Ptr(words), uint32_t{0});
        Address p = text;
        if (p < end) {
            uint32_t offset = 0;
            do {
                uint32_t word;
                const void* pushed = Ptr(p);
                if (Load<uint16_t>(p) == kSaraAm) {
                    const uint32_t before = p > text ? Cdecl<uint32_t>(kThaiClass, uint32_t{Load<uint16_t>(text + offset - 2)}) : 0;
                    word = kNikhahit;
                    if (before == kToneClass) {
                        const Address first = Load<Address>(words);
                        const Address last = first + WordsOf(words) * 2 - 2;
                        This<Address>(addr::GZFontText_Insert, Ptr(words), last, &word);
                        if (keep & 0xFF) {
                            word = static_cast<uint32_t>(static_cast<int32_t>(offset) >> 1);
                            const Address offsets = at + font::kOffsets;
                            const Address lastOffset =
                                Load<Address>(offsets) +
                                static_cast<uint32_t>(static_cast<int32_t>(Load<Address>(offsets + kEnd) - Load<Address>(offsets)) >> 2) * 4 - 4;
                            This<Address>(addr::SC4PtrVector_Insert, Ptr(offsets), lastOffset, &word);
                        }
                    } else {
                        This<void>(kWordsPushBack, Ptr(words), &word);
                        if (keep & 0xFF)
                            PushOffset(at, offset);
                    }
                    word = kSaraAa;
                    pushed = &word;
                }
                This<void>(kWordsPushBack, Ptr(words), pushed);
                if (keep & 0xFF)
                    PushOffset(at, offset);
                offset += 2;
                p += 2;
            } while (p < end);
        }
        uint32_t rebuilt = static_cast<uint32_t>(static_cast<int32_t>(p - text) >> 1);
        This<void>(addr::SC4PtrVector_PushBack, Ptr(at + font::kOffsets), &rebuilt);
        text = Load<Address>(words);
        count = WordsOf(words);
        *outText = text;
        keep = 0;
        *outCount = count;
        if (count == 0)
            return;
    }

    // The forms, in place: each word with the one before it and its class,
    // and the class of the one after it (0 past the end or for a 0 word).
    uint32_t next = Load<uint16_t>(text);
    uint32_t before = 0;
    uint32_t nextClass = Cdecl<uint32_t>(kThaiClass, next);
    int32_t beforeClass = 0;
    uint32_t form = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t previous = before;
        const uint32_t here = next;
        next = i < count - 1 ? (next & 0xFFFF0000u) | Load<uint16_t>(text + i * 2 + 2) : 0;
        const int32_t classBefore = beforeClass;
        const int32_t classHere = static_cast<int32_t>(nextClass);
        nextClass = static_cast<uint16_t>(next) == 0 ? 0 : Cdecl<uint32_t>(kThaiClass, uint32_t{static_cast<uint16_t>(next)});
        const uint32_t formBefore = form;
        form = 0;
        if (static_cast<uint16_t>(here) == kYoYing && static_cast<int32_t>(nextClass) >= 6 &&
            static_cast<int32_t>(nextClass) <= 8) {
            const uint16_t formed = This<uint16_t>(kForm, self, uint32_t{kYoYing}, uint32_t{1});
            Store<uint16_t>(*outText + i * 2, formed);
        } else {
            bool formed = false;
            if (classHere < 6) {
            } else if (classHere <= 7) {
                if (Tall(static_cast<uint16_t>(previous)))
                    form = 1;
            } else if (classHere <= 8) {
            } else if (classHere <= 0xB || (classHere > 0xC && classHere <= 0xF)) {
                if (formBefore == 1 || Tall(static_cast<uint16_t>(previous)))
                    form = 1;
                if ((classHere == static_cast<int32_t>(kToneClass) || static_cast<uint16_t>(here) == kThanthakhat) &&
                    classBefore < 0xD && classBefore <= 0xF && classBefore < 0xA && classBefore <= 0xC)
                    form += 2;
                formed = form != 0;
            }
            if (formed) {
                const uint16_t shaped = This<uint16_t>(kForm, self, here, form);
                Store<uint16_t>(*outText + i * 2, shaped);
            } else {
                const Address to = *outText;
                Store<uint16_t>(to + i * 2, Load<uint16_t>(text + i * 2));
            }
        }
        before = here;
        beforeClass = classHere;
    }
}

// The game's 0x00899BCA (ret 0x18): the text in the font's encoding as
// UTF-16 in *text, its count in *count, through the font's vector `which`
// (+0x90 + 0xC x which), the offsets (+0xD8) emptied first with `keep`.
// UTF-16 text without a mark (HasThai) is given back as it is; with one it
// is copied into the vector (sized for at least twice the count) and the
// Thai pass runs on the copy. Other encodings are converted into the vector
// (sized for at least 12 words a character) and the Thai pass runs if the
// result has a mark.
void ToUnicode(void* self, Address text, uint32_t count, Address* outText, uint32_t* outCount, uint32_t which,
               uint32_t keep)
{
    const Address at = At(self);
    const Address vector = at + (which * 3 + 0x24) * 4;
    if (keep & 0xFF)
        This<void>(kVectorErase, Ptr(at + font::kOffsets), Load<Address>(at + font::kOffsets),
                   Load<Address>(at + font::kOffsets + kEnd));
    if (Load<uint32_t>(at + font::kEncoding) == kUtf16) {
        if (!(This<uint32_t>(kHasThai, self, text, count) & 0xFF)) {
            *outText = text;
            *outCount = count;
            return;
        }
        if (WordsOf(vector) < count * 2)
            This<void>(kWordsResize, Ptr(vector), count * 2);
        const uint32_t converted = This<uint32_t>(kConvert, self, text, count, vector);
        *outCount = converted;
        This<void>(kThai, self, Load<Address>(vector), converted, outText, outCount, keep);
        return;
    }
    if (WordsOf(vector) < count * 12)
        This<void>(kWordsResize, Ptr(vector), count * 12);
    const uint32_t converted = This<uint32_t>(kConvert, self, text, count, vector);
    *outCount = converted;
    const Address begin = Load<Address>(vector);
    if (This<uint32_t>(kHasThai, self, begin, converted) & 0xFF)
        This<void>(kThai, self, begin, converted, outText, outCount, keep);
    else
        *outText = begin;
}

// ====================================================================================
// Detours

bool g_compare = false;

// Above 0 while the game's side of a comparison runs: our detours inside it
// pass through to the game's.
int g_inGame = 0;

struct GameSide {
    GameSide() { ++g_inGame; }
    ~GameSide() { --g_inGame; }
    GameSide(const GameSide&) = delete;
    GameSide& operator=(const GameSide&) = delete;
};

// Above 0 while ours runs on a scratch copy in a comparison: our detours
// inside it run ours, uncompared.
int g_oursSide = 0;

struct OursSide {
    OursSide() { ++g_oursSide; }
    ~OursSide() { --g_oursSide; }
    OursSide(const OursSide&) = delete;
    OursSide& operator=(const OursSide&) = delete;
};

bool Ours() { return g_oursSide > 0 || !g_compare; }

std::string Words(std::initializer_list<uint32_t> words)
{
    std::string text;
    char one[12];
    for (const uint32_t w : words) {
        std::snprintf(one, sizeof one, "%X ", w);
        text += one;
    }
    return text;
}

std::string Hex(Address bytes, uint32_t count)
{
    std::string text;
    char two[4];
    for (uint32_t i = 0; i < count; ++i) {
        std::snprintf(two, sizeof two, "%02X", Load<uint8_t>(bytes + i));
        text += two;
    }
    return text;
}

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

Address Allocate(uint32_t bytes) { return At(Cdecl<void*>(addr::SC4_stl_allocate, bytes)); }
void Free(Address p) { Cdecl<void>(addr::SC4_Free, Ptr(p)); }

// A font's copy for ours to run on: its bytes, its vectors (+0x90 to +0xE4)
// empty; freed afterwards.
struct ScratchFont {
    uint8_t bytes[font::kSize];
    explicit ScratchFont(const void* font)
    {
        std::memcpy(bytes, font, sizeof bytes);
        std::memset(bytes + font::kTexts, 0, font::kVectorsEnd - font::kTexts);
    }
    ~ScratchFont()
    {
        for (Address v = font::kTexts; v < font::kVectorsEnd; v += kVectorBytes) {
            const Address begin = Load<Address>(At(bytes) + v);
            if (begin)
                Free(begin);
        }
    }
    void* Self() { return bytes; }
    ScratchFont(const ScratchFont&) = delete;
    ScratchFont& operator=(const ScratchFont&) = delete;
};

// Where a text is: in the input (a caller's input may itself be in one of
// the font's vectors), in one of the font's vectors, or neither; the
// input's end counts as a vector's, whose block may start there.
std::string WhereOf(Address p, Address input, uint32_t inputCount, Address font)
{
    if (p == input || p - input < inputCount * 2)
        return "in+" + Words({p - input});
    for (Address v = font::kTexts; v < font::kVectorsEnd; v += kVectorBytes) {
        const Address begin = Load<Address>(font + v);
        if (begin && p >= begin && p <= Load<Address>(font + v + kRoom))
            return Words({v}) + "+" + Words({p - begin});
    }
    return "elsewhere ";
}

// What a text answer is: its count, where it is, its words, and the offsets
// pushed from `from` on.
std::string Answer(Address text, uint32_t count, Address input, uint32_t inputCount, Address font, uint32_t from)
{
    std::string s = Words({count}) + WhereOf(text, input, inputCount, font) + Hex(text, count * 2);
    const Address offsets = font + font::kOffsets;
    const uint32_t size = (Load<Address>(offsets + kEnd) - Load<Address>(offsets)) / 4;
    s += " offsets " + Words({size - from}) + Hex(Load<Address>(offsets) + from * 4, (size - from) * 4);
    return s;
}

uint32_t OffsetsOf(const void* font)
{
    const Address offsets = At(font) + font::kOffsets;
    return (Load<Address>(offsets + kEnd) - Load<Address>(offsets)) / 4;
}

using KindFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
using HasThaiFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t);
using FormFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t);
using ConvertFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, Address);
using ThaiFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, Address*, uint32_t*, uint32_t);
using ToUnicodeFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, Address*, uint32_t*, uint32_t, uint32_t);

KindFn g_markKind = nullptr;
HasThaiFn g_hasThai = nullptr;
FormFn g_form = nullptr;
ConvertFn g_convert = nullptr;
ThaiFn g_thai = nullptr;
ToUnicodeFn g_toUnicode = nullptr;

uint32_t __fastcall DMarkKind(void* self, void*, uint32_t c)
{
    OPENSC4_FIRST("MarkKind 0x008963C9");
    if (g_inGame)
        return g_markKind(self, nullptr, c);
    const uint32_t ours = MarkKind(static_cast<uint16_t>(c));
    if (Ours())
        return ours;
    uint32_t result;
    {
        GameSide side;
        result = g_markKind(self, nullptr, c);
    }
    Report("fonttext.markkind", Words({result}), Words({ours}));
    return result;
}

uint32_t __fastcall DHasThai(void* self, void*, Address text, uint32_t count)
{
    OPENSC4_FIRST("HasThai 0x00896425");
    if (g_inGame)
        return g_hasThai(self, nullptr, text, count);
    if (Ours())
        return HasThai(self, text, count) ? 1 : 0;
    uint32_t ours;
    {
        OursSide side;
        ours = HasThai(self, text, count) ? 1 : 0;
    }
    uint32_t result;
    {
        GameSide side;
        result = g_hasThai(self, nullptr, text, count);
    }
    Report("fonttext.hasthai", Words({result & 0xFF}), Words({ours}));
    return result;
}

uint32_t __fastcall DForm(void* self, void*, uint32_t c, uint32_t kind)
{
    OPENSC4_FIRST("Form 0x0089645A");
    if (g_inGame)
        return g_form(self, nullptr, c, kind);
    const uint32_t ours = Form(static_cast<uint16_t>(c), kind);
    if (Ours())
        return ours;
    uint32_t result;
    {
        GameSide side;
        result = g_form(self, nullptr, c, kind);
    }
    Report("fonttext.form", Words({result & 0xFFFF}), Words({ours}));
    return result;
}

uint32_t __fastcall DConvert(void* self, void*, Address text, uint32_t count, Address vector)
{
    OPENSC4_FIRST("Convert 0x00899A15");
    if (g_inGame)
        return g_convert(self, nullptr, text, count, vector);
    if (Ours())
        return Convert(self, text, count, vector);
    // Ours into a copy of the vector (its words, room for no more), then
    // the game's into the vector.
    const Address begin = Load<Address>(vector);
    const uint32_t bytes = Load<Address>(vector + kEnd) - begin;
    Address copy[3] = {0, 0, 0};
    if (bytes) {
        copy[0] = Allocate(bytes);
        std::memcpy(Ptr(copy[0]), Ptr(begin), bytes);
        copy[1] = copy[2] = copy[0] + bytes;
    }
    uint32_t ours;
    {
        OursSide side;
        ours = Convert(self, text, count, At(copy));
    }
    const std::string oursText = Words({ours, copy[1] - copy[0]}) + Hex(copy[0], copy[1] - copy[0]);
    if (copy[0])
        Free(copy[0]);
    uint32_t result;
    {
        GameSide side;
        result = g_convert(self, nullptr, text, count, vector);
    }
    const Address after = Load<Address>(vector);
    const uint32_t size = Load<Address>(vector + kEnd) - after;
    Report("fonttext.convert", Words({result, size}) + Hex(after, size), oursText);
    return result;
}

uint32_t __fastcall DThai(void* self, void*, Address text, uint32_t count, Address* outText, uint32_t* outCount,
                          uint32_t keep)
{
    OPENSC4_FIRST("Thai 0x00899665");
    if (g_inGame)
        return g_thai(self, nullptr, text, count, outText, outCount, keep);
    if (Ours()) {
        Thai(self, text, count, outText, outCount, keep);
        return 0;
    }
    // Ours on a scratch font over a copy of the text, then the game's.
    std::string oursText;
    {
        std::vector<uint16_t> input(count ? count : 1);
        if (count)
            std::memcpy(input.data(), Ptr(text), count * 2);
        ScratchFont scratch(self);
        Address t = 0;
        uint32_t n = 0;
        {
            OursSide side;
            Thai(scratch.Self(), At(input.data()), count, &t, &n, keep);
        }
        oursText = Answer(t, n, At(input.data()), count, At(scratch.Self()), 0);
    }
    const uint32_t from = OffsetsOf(self);
    uint32_t result;
    {
        GameSide side;
        result = g_thai(self, nullptr, text, count, outText, outCount, keep);
    }
    Report("fonttext.thai", Answer(*outText, *outCount, text, count, At(self), from), oursText);
    return result;
}

uint32_t __fastcall DToUnicode(void* self, void*, Address text, uint32_t count, Address* outText, uint32_t* outCount,
                               uint32_t which, uint32_t keep)
{
    OPENSC4_FIRST("ToUnicode 0x00899BCA");
    if (g_inGame)
        return g_toUnicode(self, nullptr, text, count, outText, outCount, which, keep);
    if (Ours()) {
        ToUnicode(self, text, count, outText, outCount, which, keep);
        return 0;
    }
    // Ours on a scratch font (the caller's text is only read), then the
    // game's.
    std::string oursText;
    {
        ScratchFont scratch(self);
        Address t = 0;
        uint32_t n = 0;
        {
            OursSide side;
            ToUnicode(scratch.Self(), text, count, &t, &n, which, keep);
        }
        oursText = Answer(t, n, text, count, At(scratch.Self()), 0);
    }
    const uint32_t from = keep & 0xFF ? 0 : OffsetsOf(self);
    uint32_t result;
    {
        GameSide side;
        result = g_toUnicode(self, nullptr, text, count, outText, outCount, which, keep);
    }
    Report("fonttext.tounicode", Answer(*outText, *outCount, text, count, At(self), from), oursText);
    return result;
}

OPENSC4_THUNK(TMarkKind, DMarkKind, 4)
OPENSC4_THUNK(THasThai, DHasThai, 8)
OPENSC4_THUNK(TForm, DForm, 8)
OPENSC4_THUNK(TConvert, DConvert, 12)
OPENSC4_THUNK(TThai, DThai, 20)
OPENSC4_THUNK(TToUnicode, DToUnicode, 24)

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void (*thunk)();
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

bool InstallText(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    const Detour detours[] = {
        {addr::GZFont_ToUnicode, &TToUnicode, Slot(&g_toUnicode)},
        {addr::GZFont_Convert, &TConvert, Slot(&g_convert)},
        {addr::GZFont_HasThai, &THasThai, Slot(&g_hasThai)},
        {addr::GZFont_ThaiMarkKind, &TMarkKind, Slot(&g_markKind)},
        {addr::GZFont_Thai, &TThai, Slot(&g_thai)},
        {addr::GZFont_ThaiForm, &TForm, Slot(&g_form)},
    };
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fonttext: text: %zu functions %s", std::size(detours),
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registered[] = {
    registry::Register({"framework.font.font.text", "framework/font", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZFont_ToUnicode,
                        "Our font's text encodings and Thai: ToUnicode, Convert, HasThai, the Thai mark's kind, "
                        "the Thai pass and its presentation forms (6 detours; docs/fonts.md)",
                        true, InstallText}),
};

} // namespace
} // namespace opensc4::fonttext
