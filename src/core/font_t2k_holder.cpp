// Our T2K holder of the framework's fonts, f5 of docs/fonts.md ("f1 and f5
// are ours"): the 0x34-byte object a font keeps at +0xEC between itself and
// T2K, the font scaler the game links in (0x0089A578 to 0x008AE76D, which
// stays the game's), and the file stream T2K reads the font through. Each is
// a detour on the game's function, in three groups, off by default and
// installed at Attach (fonts are opened while the game starts):
//
//   framework.font.t2k.stream  the stream's constructor (0x00896D2F), what
//                              is left of it (0x0089603D), its read
//                              (0x00896DDD) and T2K's read callback over it
//                              (0x00896E4A), and reading it whole into a
//                              vector (0x00898C6B)
//   framework.font.t2k.holder  the holder's constructor (0x00897FED), Open
//                              (0x00898CD9) and Close (0x008976A4)
//   framework.font.t2k.names   the face's name (0x0089801E), its style bits
//                              (0x0089883F), the table of glyph names
//                              (0x008988F1) and the glyph of a character by
//                              its glyph name (0x00898EB2)
//
// The holder (0x34 bytes): +0x00 T2K's memory handler, +0x04 the sfnt, +0x08
// the scaler, +0x0C the stream, +0x10 T2K's input stream over it, +0x14 a
// byte vector (a Type 1 face unwrapped from its .pfb), +0x20 a word of flags
// (0xA, then 1 or 0x80 once open), +0x24 the kind of face (1 TrueType, 2
// Type 1, 3 and 4 two T2K types not identified, 0 none), +0x28 a map of
// characters to glyphs found by name (STLport, uint16 to uint32).
//
// The stream (0x8C bytes) is the game's file (cRZFile, 0x80 bytes, no
// vtable of its own) and three fields: +0x80 where the font starts in the
// file, +0x84 how many bytes from there are scrambled, +0x88 the byte they
// are XORed with. A file that starts with "MXFN" has the font from byte 9:
// four bytes of length (big-endian) and the key before it.
//
// Every T2K function is the game's and is called at the game's address;
// T2K's entry points set their own longjmp target, so its errors come back
// as the error word the Maxis side passes and never cross our frames. The
// read callback handed to T2K is the game's address 0x00896E4A, as the
// game's Open hands it.
//
// Compare mode runs ours and then the game's on the same inputs. Pure
// functions compare their result; the reads keep what ours wrote aside, put
// the memory back and compare the bytes; functions that make objects run
// the game's on a scratch copy (a scratch holder, stream, vector, string or
// map) and compare what they made, field by field, before the scratch is
// taken down with the game's own destructors. While the game's side runs,
// our detours inside it pass through to the game's. Close frees what it
// closes and runs the game's alone. Ours call each other directly (our Open
// our Close and stream, our Styles our GetName, our glyph lookup our name
// table), so those compare branches run when the game's code calls them.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace opensc4::fontfile {
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

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

template <typename Fn>
inline Fn Game(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontfile: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// An STLport vector's three pointers.
struct Vector {
    Address begin, end, capacity;
};

// ====================================================================================
// The game's functions we call

// T2K (cdecl; each takes the error word last and sets it).
constexpr std::uintptr_t kNewMemHandler = 0x0089A594;     // (int* err) -> handler
constexpr std::uintptr_t kDeleteMemHandler = 0x0089A8A7;  // (handler)
constexpr std::uintptr_t kNewInputStream = 0x0089AD13;    // (handler, id, read, length, err)
constexpr std::uintptr_t kNewMemInputStream = 0x0089AC7E; // (handler, data, length, err)
constexpr std::uintptr_t kDeleteInputStream = 0x0089AE0C; // (stream, err)
constexpr std::uintptr_t kFontType = 0x008A1C10;          // (stream, err) -> type in the low word
constexpr std::uintptr_t kUnwrapPfb = 0x0089F7C6;         // (data, uint32* length, err)
constexpr std::uintptr_t kNewSfnt = 0x0089D662;           // (handler, type, 0, stream, 0, 0, err)
constexpr std::uintptr_t kDeleteSfnt = 0x0089CABE;        // (sfnt, err)
constexpr std::uintptr_t kNewScaler = 0x0089D8DC;         // (handler, sfnt, err)
constexpr std::uintptr_t kDeleteScaler = 0x0089F3E6;      // (scaler, err)
constexpr std::uintptr_t kLoadName = 0x0089F353;          // (scaler, language, name ID, err)
constexpr std::uintptr_t kCodeOfGlyphName = 0x0089F4F3;   // (scaler, name, err) -> code, 0 if none
constexpr std::uintptr_t kGlyphOfCode = 0x0089F4B2;       // (scaler, code, err) -> glyph in the low word

using ErrorOnly = void*(__cdecl*)(int32_t*);
using DeleteOne = void(__cdecl*)(void*);
using DeleteWithError = void(__cdecl*)(void*, int32_t*);
using NewInputStreamFn = void*(__cdecl*)(void*, void*, Address, uint32_t, int32_t*);
using NewMemInputStreamFn = void*(__cdecl*)(void*, Address, uint32_t, int32_t*);
using FontTypeFn = uint32_t(__cdecl*)(void*, int32_t*);
using UnwrapPfbFn = void(__cdecl*)(Address, uint32_t*, int32_t*);
using NewSfntFn = void*(__cdecl*)(void*, uint32_t, uint32_t, void*, uint32_t, uint32_t, int32_t*);
using NewScalerFn = void*(__cdecl*)(void*, void*, int32_t*);
using LoadNameFn = void(__cdecl*)(void*, uint32_t, uint32_t, int32_t*);
using CodeOfGlyphNameFn = uint32_t(__cdecl*)(void*, Address, int32_t*);
using GlyphOfCodeFn = uint32_t(__cdecl*)(void*, uint32_t, int32_t*);

// The holder's map of characters to glyphs (and the static map of glyph
// names, the same STLport instance): find (the header if not there), insert
// and the destructor, all thiscall.
constexpr std::uintptr_t kMapFind = 0x0089762F;     // (const uint16* key) -> node; ret 4
constexpr std::uintptr_t kMapInsert = 0x00897E7C;   // (result*, const pair*); ret 8
constexpr std::uintptr_t kMapDestruct = 0x00897C5F; // the map's own (emptied, then its header freed)
constexpr std::uintptr_t kHolderDestruct = 0x00897E36;

using MapFindFn = Address(__fastcall*)(void*, void*, const void*);
using MapInsertFn = void*(__fastcall*)(void*, void*, void*, const void*);
using ThisOnly = void*(__fastcall*)(void*, void*);
using ThisOne = void*(__fastcall*)(void*, void*, void*);
using ThisTwo = void*(__fastcall*)(void*, void*, Address, Address);

// The map's layout: the header node at +0 and the count at +4; a node has
// its parent at +4, left and right at +8 and +0xC, the key (a word) at
// +0x10 and the value at +0x14.
constexpr Address kMapHeader = 0x0;
constexpr Address kMapCount = 0x4;
constexpr Address kNodeValue = 0x14;

struct MapPair {
    uint16_t key;
    uint16_t pad;
    uint32_t value;
};
static_assert(sizeof(MapPair) == 8);

// The game's file (cIGZFile's slots).
namespace file {
constexpr std::size_t kOpen = 0x0C;
constexpr std::size_t kIsOpen = 0x10;
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kSeekToBegin = 0x24;
constexpr std::size_t kSeekToPosition = 0x30;
constexpr std::size_t kRead = 0x38;
} // namespace file

// The game's string (cIGZString over an STLport body at +4): the slot that
// finds a text, and its MakeLower, Trim, FindBody and EraseBody.
constexpr std::size_t kStringFind = 0x68;
constexpr Address kStringBody = 0x4;
constexpr Address kStringBegin = 0x4;
constexpr Address kStringEnd = 0x8;

// ====================================================================================
// Layouts

namespace stream {
constexpr uint32_t kSize = 0x8C;
constexpr Address kBase = 0x80;
constexpr Address kScrambled = 0x84;
constexpr Address kKey = 0x88;
// "MXFN" read big-endian; the font then starts at byte 9.
constexpr uint32_t kMxfn = 0x4D58464E;
constexpr uint32_t kMxfnBase = 9;
constexpr uint32_t kHeaderAtLeast = 9; // a file must be longer than this to be looked at
// The game opens fonts for reading, existing files only, shared for reading.
constexpr uint32_t kRead = 1, kOpenExisting = 2, kShareRead = 1;
} // namespace stream

namespace holder {
constexpr uint32_t kSize = 0x34;
constexpr Address kMemory = 0x00;
constexpr Address kSfnt = 0x04;
constexpr Address kScaler = 0x08;
constexpr Address kStream = 0x0C;
constexpr Address kInput = 0x10;
constexpr Address kPfb = 0x14;
constexpr Address kFlags = 0x20;
constexpr Address kKind = 0x24;
constexpr Address kGlyphs = 0x28;
constexpr uint16_t kFirstFlags = 0xA;
} // namespace holder

// T2K's font types (the low word of 0x008A1C10's answer) and the holder's
// kinds for them.
constexpr uint16_t kTypeType1 = 1;
constexpr uint16_t kTypeTrueType = 2;
constexpr uint16_t kType7 = 7;
constexpr uint16_t kType16 = 0x16;
constexpr uint32_t kKindTrueType = 1;
constexpr uint32_t kKindType1 = 2;
constexpr uint32_t kKind16 = 3;
constexpr uint32_t kKind7 = 4;

// The scaler: the class at +0x180 (+0x74 and +0x76 two words of its, +0x3C a
// table whose +0x30 has the style bits), +0x48 a dword Open sets, +0x50 and
// +0x54 the last name loaded (bytes; UTF-16).
namespace scaler {
constexpr Address kClass = 0x180;
constexpr Address kClassMode = 0x74;
constexpr Address kClassNameAny = 0x76;
constexpr Address kClassStyle = 0x3C;
constexpr Address kStyleBits = 0x30;
constexpr Address kOpened = 0x48;
constexpr Address kName8 = 0x50;
constexpr Address kName16 = 0x54;
} // namespace scaler

constexpr uint32_t kEnglishUs = 0x409;
constexpr uint32_t kNameFamily = 1;
constexpr uint32_t kNameStyle = 2;
constexpr uint32_t kNameFullName = 4;
constexpr uint32_t kUtf16 = 0x10;
constexpr uint32_t kWindows1252 = 0x4E4;
constexpr uint32_t kNotFound = 0xFFFFFFFF;

constexpr Address kReadCallback = 0x00896E4A;
constexpr Address kBoldText = 0x00AA1260;   // "bold"
constexpr Address kItalicText = 0x00AA1258; // "italic"
constexpr Address kUniFormat = 0x00AC56D8;  // "uni%04X"

// The static map of glyph names (character to name), filled once from the
// table at 0x00B150F0: 479 entries of a character (a word) and its name.
constexpr Address kNameMap = 0x00B5C41C;
constexpr Address kNameTable = 0x00B150F0;
constexpr uint32_t kNameTableBytes = 0xEF8;

// ====================================================================================
// The stream

// The game's 0x0089603D (thiscall): the file's length less where the font
// starts.
uint32_t Remaining(void* self) { return Virtual<uint32_t>(self, file::kLength) - Load<uint32_t>(At(self) + stream::kBase); }

// The game's 0x00896D2F (thiscall, ret 4): the file at `path` opened for
// reading; an "MXFN" header gives where the font starts, how much of it is
// scrambled and the key. Returns this.
void* ConstructStream(void* self, void* path)
{
    Game<ThisOne>(addr::cRZFile_Construct)(self, nullptr, path);
    const Address at = At(self);
    Store<uint32_t>(at + stream::kBase, 0);
    Store<uint32_t>(at + stream::kScrambled, 0);
    Store<uint8_t>(at + stream::kKey, 0);
    if (Virtual<uint8_t>(self, file::kOpen, stream::kRead, stream::kOpenExisting, stream::kShareRead) &&
        Virtual<uint32_t>(self, file::kLength) > stream::kHeaderAtLeast) {
        Virtual<uint32_t>(self, file::kSeekToBegin);
        // The game reads the tag over its own argument: a failed read
        // compares the path's pointer.
        uint32_t tag = At(path);
        Virtual<uint8_t>(self, file::kRead, &tag, uint32_t{4});
        if (_byteswap_ulong(tag) == stream::kMxfn) {
            Store<uint32_t>(at + stream::kBase, stream::kMxfnBase);
            Virtual<uint8_t>(self, file::kRead, Ptr(at + stream::kScrambled), uint32_t{4});
            Store<uint32_t>(at + stream::kScrambled, _byteswap_ulong(Load<uint32_t>(at + stream::kScrambled)));
            Virtual<uint8_t>(self, file::kRead, Ptr(at + stream::kKey), uint32_t{1});
        }
    }
    return self;
}

// The game's 0x00896DDD (thiscall, ret 0xC): `count` bytes from `offset` of
// the font into `buffer`, the scrambled part XORed with the key. False if
// the file's read fails.
bool Read(void* self, Address buffer, uint32_t offset, uint32_t count)
{
    const Address at = At(self);
    Virtual<uint32_t>(self, file::kSeekToPosition, Load<uint32_t>(at + stream::kBase) + offset);
    if (!Virtual<uint8_t>(self, file::kRead, Ptr(buffer), count))
        return false;
    const uint32_t scrambled = Load<uint32_t>(at + stream::kScrambled);
    if (offset < scrambled) {
        const uint32_t left = scrambled - offset;
        const uint32_t n = count < left ? count : left;
        for (uint32_t i = 0; i < n; ++i)
            Store<uint8_t>(buffer + i, Load<uint8_t>(buffer + i) ^ Load<uint8_t>(at + stream::kKey));
    }
    return true;
}

// The game's 0x00896E4A (cdecl): T2K's read callback, the stream as its ID.
uint32_t ReadCallback(void* id, Address buffer, uint32_t offset, uint32_t count)
{
    return Read(id, buffer, offset, count) ? 1 : 0;
}

// The game's 0x00898C6B (thiscall, ret 8): the font, at most `most` bytes,
// into the vector (emptied first). False if the file is not open or the
// read fails.
bool ReadAll(void* self, Vector* out, uint32_t most)
{
    Game<ThisTwo>(addr::cSC4Advisor_F40E490)(out, nullptr, out->begin, out->end);
    if (!Virtual<uint8_t>(self, file::kIsOpen))
        return false;
    const uint32_t left = Virtual<uint32_t>(self, file::kLength) - Load<uint32_t>(At(self) + stream::kBase);
    const uint32_t n = left < most ? left : most;
    Game<void(__fastcall*)(void*, void*, uint32_t)>(addr::SC4ByteVector_ResizeZeroed)(out, nullptr, n);
    Virtual<uint32_t>(self, file::kSeekToPosition, Load<uint32_t>(At(self) + stream::kBase));
    return Read(self, out->begin, 0, n);
}

// ====================================================================================
// The holder

// The game's 0x00897FED (thiscall): everything empty, the flags 0xA and the
// map made. Returns this.
void* Construct(void* self)
{
    const Address at = At(self);
    for (Address field = 0; field < holder::kFlags; field += 4)
        Store<uint32_t>(at + field, 0);
    Store<uint16_t>(at + holder::kFlags, holder::kFirstFlags);
    Store<uint32_t>(at + holder::kKind, 0);
    Game<ThisOnly>(addr::cGZDBSegmentPackedFile_HolesMapConstruct)(Ptr(at + holder::kGlyphs), nullptr);
    return self;
}

// The game's 0x008976A4 (thiscall): T2K's objects deleted, then the stream
// and the memory handler, each field cleared after it. Returns true.
bool Close(void* self)
{
    const Address at = At(self);
    int32_t error = 0;
    if (void* const scaler = Load<void*>(at + holder::kScaler)) {
        Game<DeleteWithError>(kDeleteScaler)(scaler, &error);
        Store<uint32_t>(at + holder::kScaler, 0);
    }
    if (void* const sfnt = Load<void*>(at + holder::kSfnt)) {
        Game<DeleteWithError>(kDeleteSfnt)(sfnt, &error);
        Store<uint32_t>(at + holder::kSfnt, 0);
    }
    if (void* const input = Load<void*>(at + holder::kInput)) {
        Game<DeleteWithError>(kDeleteInputStream)(input, &error);
        Store<uint32_t>(at + holder::kInput, 0);
    }
    if (void* const file = Load<void*>(at + holder::kStream)) {
        Game<ThisOnly>(addr::cRZFile_Destruct)(file, nullptr);
        Game<DeleteOne>(addr::SC4_operator_delete)(file);
        Store<uint32_t>(at + holder::kStream, 0);
    }
    if (void* const memory = Load<void*>(at + holder::kMemory)) {
        Game<DeleteOne>(kDeleteMemHandler)(memory);
        Store<uint32_t>(at + holder::kMemory, 0);
    }
    return true;
}

// The game's 0x00898CD9 (thiscall, ret 4): the font file at `path` opened
// with T2K, whatever was open closed first. A Type 1 face is read whole,
// unwrapped from its .pfb and read again from memory. Every step stops at
// T2K's first error. True if a scaler was made.
bool Open(void* self, void* path)
{
    const Address at = At(self);
    int32_t error = 0;
    Close(self);
    void* const memory = Game<ErrorOnly>(kNewMemHandler)(&error);
    Store<void*>(at + holder::kMemory, memory);
    if (memory && error == 0) {
        void* const space = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(stream::kSize);
        void* const file = space ? ConstructStream(space, path) : nullptr;
        Store<void*>(at + holder::kStream, file);
        const uint32_t length = Remaining(file);
        void* const input = Game<NewInputStreamFn>(kNewInputStream)(
            Load<void*>(at + holder::kMemory), Load<void*>(at + holder::kStream), kReadCallback, length, &error);
        Store<void*>(at + holder::kInput, input);
        if (input && error == 0) {
            const uint32_t type = Game<FontTypeFn>(kFontType)(input, &error);
            const uint16_t low = static_cast<uint16_t>(type);
            uint32_t kind = 0;
            if (low == kTypeType1)
                kind = kKindType1;
            else if (low == kType16)
                kind = kKind16;
            else if (low == kType7)
                kind = kKind7;
            else if (low == kTypeTrueType)
                kind = kKindTrueType;
            Store<uint32_t>(at + holder::kKind, kind);
            kind = Load<uint32_t>(at + holder::kKind);
            if (kind != 0 && error == 0) {
                if (kind == kKindType1) {
                    auto* const pfb = static_cast<Vector*>(Ptr(at + holder::kPfb));
                    if (ReadAll(Load<void*>(at + holder::kStream), pfb, 0xFFFFFFFF)) {
                        uint32_t size = pfb->end - pfb->begin;
                        Game<UnwrapPfbFn>(kUnwrapPfb)(pfb->begin, &size, &error);
                        if (error == 0) {
                            Game<void(__fastcall*)(void*, void*, uint32_t)>(addr::SC4ByteVector_ResizeZeroed)(
                                pfb, nullptr, size);
                            void* const unwrapped = Game<NewMemInputStreamFn>(kNewMemInputStream)(
                                Load<void*>(at + holder::kMemory), pfb->begin, pfb->end - pfb->begin, &error);
                            Game<DeleteWithError>(kDeleteInputStream)(Load<void*>(at + holder::kInput), &error);
                            Store<void*>(at + holder::kInput, unwrapped);
                        }
                    }
                }
                if (Load<void*>(at + holder::kMemory) && error == 0) {
                    void* const sfnt = Game<NewSfntFn>(kNewSfnt)(Load<void*>(at + holder::kMemory), type, 0,
                                                                 Load<void*>(at + holder::kInput), 0, 0, &error);
                    Store<void*>(at + holder::kSfnt, sfnt);
                    if (sfnt && error == 0) {
                        void* const made =
                            Game<NewScalerFn>(kNewScaler)(Load<void*>(at + holder::kMemory), sfnt, &error);
                        Store<void*>(at + holder::kScaler, made);
                        if (made && error == 0) {
                            Store<uint16_t>(Load<Address>(At(made) + scaler::kClass) + scaler::kClassMode, 3);
                            const Address now = Load<Address>(at + holder::kScaler);
                            Store<uint16_t>(Load<Address>(now + scaler::kClass) + scaler::kClassNameAny, 1);
                            Store<uint32_t>(Load<Address>(at + holder::kScaler) + scaler::kOpened, 1);
                            if (low == kTypeTrueType || low == kType7)
                                Store<uint8_t>(at + holder::kFlags, Load<uint8_t>(at + holder::kFlags) | 0x80);
                            else
                                Store<uint16_t>(at + holder::kFlags,
                                                static_cast<uint16_t>(Load<uint16_t>(at + holder::kFlags) | 1));
                        }
                    }
                }
            }
        }
    }
    return Load<uint32_t>(at + holder::kScaler) != 0;
}

// ====================================================================================
// Names

// Both of the last name loaded empty.
inline bool NoName(Address scalerAt)
{
    return Load<uint32_t>(scalerAt + scaler::kName16) == 0 && Load<uint32_t>(scalerAt + scaler::kName8) == 0;
}

inline void SetNameAny(Address holderAt, uint16_t value)
{
    Store<uint16_t>(Load<Address>(Load<Address>(holderAt + holder::kScaler) + scaler::kClass) + scaler::kClassNameAny,
                    value);
}

// The game's 0x0089801E (thiscall, ret 8): the face's name `id` into `out`
// (a game string), American English first, then any language, then the
// full name (4), each first as asked and then with the class's word +0x76
// cleared for the one call; empty if there is none. For the family (1) the
// name is cut at "bold" and at "italic"; for the style (2) it keeps from the
// first of them on; both are then trimmed. True if `out` is not empty.
bool GetName(void* self, void* out, uint32_t id)
{
    const Address at = At(self);
    const Address outAt = At(out);
    if (Load<uint32_t>(at + holder::kScaler) == 0)
        return Load<Address>(outAt + kStringBegin) != Load<Address>(outAt + kStringEnd);
    const bool family = id == kNameFamily;
    const bool style = id == kNameStyle;
    int32_t error = 0;
    const auto loadName = Game<LoadNameFn>(kLoadName);
    const struct {
        uint32_t language, name;
        bool cleared;
    } tries[] = {
        {kEnglishUs, id, false},           {kEnglishUs, id, true},           {0, id, false},
        {0, id, true},                     {kEnglishUs, kNameFullName, false}, {kEnglishUs, kNameFullName, true},
        {0, kNameFullName, false},         {0, kNameFullName, true},
    };
    loadName(Load<void*>(at + holder::kScaler), tries[0].language, tries[0].name, &error);
    for (std::size_t i = 1; i < sizeof tries / sizeof tries[0]; ++i) {
        if (!NoName(Load<Address>(at + holder::kScaler)))
            continue;
        if (tries[i].cleared)
            SetNameAny(at, 0);
        loadName(Load<void*>(at + holder::kScaler), tries[i].language, tries[i].name, &error);
        if (tries[i].cleared)
            SetNameAny(at, 1);
    }

    const Address scalerAt = Load<Address>(at + holder::kScaler);
    uint8_t allocator = 0;
    if (const Address wide = Load<Address>(scalerAt + scaler::kName16)) {
        Vector text{};
        Game<void*(__fastcall*)(void*, void*, Address, void*)>(addr::SC4UI_F79CA50)(&text, nullptr, wide, &allocator);
        Game<void(__cdecl*)(Address, int32_t, uint32_t, void*)>(addr::SC4String_FromWide)(
            text.begin, static_cast<int32_t>(text.end - text.begin) >> 1, kUtf16, out);
        if (text.begin)
            Game<DeleteOne>(addr::SC4_Free)(Ptr(text.begin));
    } else if (const Address narrow = Load<Address>(scalerAt + scaler::kName8)) {
        Vector text{};
        Game<void*(__fastcall*)(void*, void*, Address, void*)>(addr::cSC4Advice_F408000)(&text, nullptr, narrow,
                                                                                         &allocator);
        Game<void(__cdecl*)(Address, int32_t, uint32_t, void*)>(addr::SC4String_FromWide)(
            text.begin, static_cast<int32_t>(text.end - text.begin), kWindows1252, out);
        if (text.begin)
            Game<DeleteOne>(addr::SC4_Free)(Ptr(text.begin));
    } else {
        const Address begin = Load<Address>(outAt + kStringBegin);
        if (begin != Load<Address>(outAt + kStringEnd)) {
            Store<uint8_t>(begin, 0);
            Store<Address>(outAt + kStringEnd, begin);
        }
    }

    if (family || style) {
        const uint32_t bold = Virtual<uint32_t>(out, kStringFind, kBoldText, kNotFound, uint32_t{0});
        const uint32_t italic = Virtual<uint32_t>(out, kStringFind, kItalicText, kNotFound, uint32_t{0});
        const auto erase = Game<void*(__fastcall*)(void*, void*, uint32_t, uint32_t)>(addr::SC4String_EraseBody);
        void* const body = Ptr(outAt + kStringBody);
        if (family) {
            if (bold != kNotFound)
                erase(body, nullptr, bold, kNotFound);
            if (italic != kNotFound)
                erase(body, nullptr, italic, kNotFound);
        } else {
            const uint32_t first = italic < bold ? italic : bold;
            if (first != kNotFound)
                erase(body, nullptr, 0, first);
        }
        Game<ThisOnly>(addr::cRZBaseString_Trim)(out, nullptr);
    }
    return Load<Address>(outAt + kStringBegin) != Load<Address>(outAt + kStringEnd);
}

// The game's string as Styles makes it on its stack.
struct GameString {
    Address vtable, begin, end, capacity, zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's 0x0089883F (thiscall): the face's style bits, 1 bold and 2
// italic, from the scaler's table and from "bold" and "italic" in the style
// name (lower-cased). 0 without a scaler.
uint32_t Styles(void* self)
{
    const Address at = At(self);
    uint32_t bits = 0;
    if (Load<uint32_t>(at + holder::kScaler) == 0)
        return bits;
    const Address type = Load<Address>(Load<Address>(at + holder::kScaler) + scaler::kClass);
    if (type) {
        const Address table = Load<Address>(type + scaler::kClassStyle);
        if (table && (Load<uint8_t>(table + scaler::kStyleBits) & 1))
            ++bits;
        if (type) {
            const Address again = Load<Address>(type + scaler::kClassStyle);
            if (again && (Load<uint8_t>(again + scaler::kStyleBits) & 2))
                bits |= 2;
        }
    }
    GameString name;
    Game<ThisOnly>(addr::SC4String_SC4String)(&name, nullptr);
    GetName(self, &name, kNameStyle);
    if (name.begin != name.end) {
        Game<ThisOnly>(addr::cRZBaseString_MakeLower)(&name, nullptr);
        const auto find = Game<uint32_t(__fastcall*)(void*, void*, Address, uint32_t)>(addr::SC4StringBody_Find);
        if (find(&name.begin, nullptr, kItalicText, 0) != kNotFound)
            bits |= 2;
        if (find(&name.begin, nullptr, kBoldText, 0) != kNotFound)
            bits |= 1;
    }
    name.vtable = static_cast<Address>(addr::SC4String_Vtable);
    if (name.begin)
        Game<DeleteOne>(addr::SC4_Free)(Ptr(name.begin));
    return bits;
}

// The game's 0x008988F1: the table of glyph names into the static map (here
// into any map, for compare mode).
void BuildNameTableInto(Address map)
{
    const auto insert = Game<MapInsertFn>(kMapInsert);
    for (uint32_t i = 0; i < kNameTableBytes; i += 8) {
        MapPair pair{Load<uint16_t>(kNameTable + i), 0, Load<uint32_t>(kNameTable + i + 4)};
        uint8_t result[8];
        insert(Ptr(map), nullptr, result, &pair);
    }
}

void BuildNameTable() { BuildNameTableInto(kNameMap); }

// The game's 0x00898EB2 (thiscall, ret 4): the glyph of a character by the
// glyph's PostScript name, for faces without a cmap: from the holder's map
// if there; else by the name in the static table, then by "uniXXXX". A
// glyph found (not 0) is kept in the map. 0 if none.
uint32_t GlyphByName(void* self, uint32_t character)
{
    const Address at = At(self);
    void* const glyphs = Ptr(at + holder::kGlyphs);
    const auto find = Game<MapFindFn>(kMapFind);
    const auto insert = Game<MapInsertFn>(kMapInsert);
    int32_t error = 0;
    uint32_t key = character;
    const Address known = find(glyphs, nullptr, &key);
    if (known != Load<Address>(at + holder::kGlyphs + kMapHeader))
        return Load<uint32_t>(known + kNodeValue);
    if (Load<uint32_t>(kNameMap + kMapCount) == 0)
        BuildNameTable();
    key = character;
    const Address named = find(Ptr(kNameMap), nullptr, &key);
    const auto codeOf = Game<CodeOfGlyphNameFn>(kCodeOfGlyphName);
    const auto glyphOf = Game<GlyphOfCodeFn>(kGlyphOfCode);
    uint8_t result[8];
    if (named != Load<Address>(kNameMap + kMapHeader)) {
        if (const uint32_t code = codeOf(Load<void*>(at + holder::kScaler), Load<Address>(named + kNodeValue), &error)) {
            const uint32_t glyph = glyphOf(Load<void*>(at + holder::kScaler), code, &error) & 0xFFFF;
            if (glyph) {
                const MapPair pair{static_cast<uint16_t>(character), 0, glyph};
                insert(glyphs, nullptr, result, &pair);
                return glyph;
            }
        }
    }
    char name[16];
    Game<int(__cdecl*)(char*, Address, uint32_t)>(addr::SC4_sprintf)(name, kUniFormat, character & 0xFFFF);
    if (const uint32_t code = codeOf(Load<void*>(at + holder::kScaler), At(name), &error)) {
        const uint32_t glyph = glyphOf(Load<void*>(at + holder::kScaler), code, &error) & 0xFFFF;
        if (glyph) {
            const MapPair pair{static_cast<uint16_t>(character), 0, glyph};
            insert(glyphs, nullptr, result, &pair);
            return glyph;
        }
    }
    return 0;
}

// ====================================================================================
// Detours

enum Group { kStream, kHolder, kNames, kGroups };
bool g_compare[kGroups] = {};

// Above 0 while the game's side of a comparison runs: our detours inside it
// pass through to the game's.
int g_inGame = 0;

struct GameSide {
    GameSide() { ++g_inGame; }
    ~GameSide() { --g_inGame; }
    GameSide(const GameSide&) = delete;
    GameSide& operator=(const GameSide&) = delete;
};

std::string Hex(const uint8_t* bytes, std::size_t count)
{
    std::string text;
    char two[4];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(two, sizeof two, "%02X", bytes[i]);
        text += two;
    }
    return text;
}

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

uint32_t Fnv(Address at, uint32_t size)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < size; ++i)
        hash = (hash ^ Load<uint8_t>(at + i)) * 16777619u;
    return hash;
}

// Ours first, what it wrote to `buffer` kept aside and the bytes put back,
// then the game's; the low bytes of the results and the bytes compared.
template <typename Original, typename Ours>
uint32_t ComparedBytes(const char* id, Address buffer, uint32_t size, Original&& original, Ours&& ours)
{
    uint8_t* const p = static_cast<uint8_t*>(Ptr(buffer));
    const std::vector<uint8_t> before(p, p + size);
    const uint32_t ourResult = ours();
    const std::vector<uint8_t> mine(p, p + size);
    std::memcpy(p, before.data(), size);
    uint32_t result;
    {
        GameSide side;
        result = original();
    }
    bool same = (result & 0xFF) == (ourResult & 0xFF);
    std::string game = same ? "" : "result " + Words({result & 0xFF}), text = same ? "" : "result " + Words({ourResult & 0xFF});
    if (same && size && std::memcmp(p, mine.data(), size) != 0) {
        same = false;
        std::size_t at = 0;
        while (p[at] == mine[at])
            ++at;
        const std::size_t n = std::min<std::size_t>(16, size - at);
        char where[32];
        std::snprintf(where, sizeof where, "@%zX ", at);
        game = where + Hex(p + at, n);
        text = where + Hex(mine.data() + at, n);
    }
    compare::Record(id, same, game, text);
    return result;
}

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

// --- the stream

using RemainingFn = uint32_t(__fastcall*)(void*, void*);
using ConstructStreamFn = void*(__fastcall*)(void*, void*, void*);
using ReadFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, uint32_t);
using ReadCallbackFn = uint32_t(__cdecl*)(void*, Address, uint32_t, uint32_t);
using ReadAllFn = uint32_t(__fastcall*)(void*, void*, Vector*, uint32_t);
RemainingFn g_remaining = nullptr;
ConstructStreamFn g_constructStream = nullptr;
ReadFn g_read = nullptr;
ReadCallbackFn g_readCallback = nullptr;
ReadAllFn g_readAll = nullptr;

uint32_t __fastcall DRemaining(void* self, void*)
{
    OPENSC4_FIRST("Remaining 0x0089603D");
    if (g_inGame)
        return g_remaining(self, nullptr);
    if (!g_compare[kStream])
        return Remaining(self);
    return compare::Call("fontfile.remaining", [&] {
        GameSide side;
        return g_remaining(self, nullptr);
    }, [&] { return Remaining(self); });
}

// What the stream's constructor made: its own three fields and whether the
// file is open, with its length.
std::string StreamState(void* self, void* made)
{
    const Address at = At(self);
    const bool open = Virtual<uint8_t>(self, file::kIsOpen) != 0;
    return Words({made == self ? 1u : 0u, Load<uint32_t>(at + stream::kBase), Load<uint32_t>(at + stream::kScrambled),
                  Load<uint8_t>(at + stream::kKey), open ? 1u : 0u, open ? Virtual<uint32_t>(self, file::kLength) : 0u});
}

void* __fastcall DConstructStream(void* self, void*, void* path)
{
    OPENSC4_FIRST("ConstructStream 0x00896D2F");
    if (g_inGame)
        return g_constructStream(self, nullptr, path);
    if (!g_compare[kStream])
        return ConstructStream(self, path);
    void* const made = ConstructStream(self, path);
    const std::string mine = StreamState(self, made);
    alignas(8) uint8_t scratch[stream::kSize];
    std::memset(scratch, 0, sizeof scratch);
    std::string game;
    {
        GameSide side;
        void* const theirs = g_constructStream(scratch, nullptr, path);
        game = StreamState(scratch, theirs);
        Game<ThisOnly>(addr::cRZFile_Destruct)(scratch, nullptr);
    }
    Report("fontfile.constructstream", game, mine);
    return made;
}

uint32_t __fastcall DRead(void* self, void*, Address buffer, uint32_t offset, uint32_t count)
{
    OPENSC4_FIRST("Read 0x00896DDD");
    if (g_inGame)
        return g_read(self, nullptr, buffer, offset, count);
    if (!g_compare[kStream])
        return Read(self, buffer, offset, count) ? 1 : 0;
    return ComparedBytes("fontfile.read", buffer, count, [&] { return g_read(self, nullptr, buffer, offset, count); },
                         [&] { return Read(self, buffer, offset, count) ? 1u : 0u; });
}

uint32_t __cdecl DReadCallback(void* id, Address buffer, uint32_t offset, uint32_t count)
{
    OPENSC4_FIRST("ReadCallback 0x00896E4A");
    if (g_inGame)
        return g_readCallback(id, buffer, offset, count);
    if (!g_compare[kStream])
        return ReadCallback(id, buffer, offset, count);
    return ComparedBytes("fontfile.readcallback", buffer, count,
                         [&] { return g_readCallback(id, buffer, offset, count); },
                         [&] { return ReadCallback(id, buffer, offset, count); });
}

std::string VectorState(uint32_t result, const Vector& v)
{
    return Words({result & 0xFF, v.end - v.begin, v.begin ? Fnv(v.begin, v.end - v.begin) : 0});
}

uint32_t __fastcall DReadAll(void* self, void*, Vector* out, uint32_t most)
{
    OPENSC4_FIRST("ReadAll 0x00898C6B");
    if (g_inGame)
        return g_readAll(self, nullptr, out, most);
    if (!g_compare[kStream])
        return ReadAll(self, out, most) ? 1 : 0;
    const uint32_t result = ReadAll(self, out, most) ? 1 : 0;
    const std::string mine = VectorState(result, *out);
    Vector scratch{};
    std::string game;
    {
        GameSide side;
        game = VectorState(g_readAll(self, nullptr, &scratch, most), scratch);
    }
    if (scratch.begin)
        Game<DeleteOne>(addr::SC4_Free)(Ptr(scratch.begin));
    Report("fontfile.readall", game, mine);
    return result;
}

OPENSC4_THUNK(TRemaining, DRemaining, 0)
OPENSC4_THUNK(TConstructStream, DConstructStream, 4)
OPENSC4_THUNK(TRead, DRead, 12)
OPENSC4_CDECL_THUNK(TReadCallback, DReadCallback, 16)
OPENSC4_THUNK(TReadAll, DReadAll, 8)

// --- the holder

using ConstructFn = void*(__fastcall*)(void*, void*);
using CloseFn = uint32_t(__fastcall*)(void*, void*);
using OpenFn = uint32_t(__fastcall*)(void*, void*, void*);
ConstructFn g_construct = nullptr;
CloseFn g_close = nullptr;
OpenFn g_open = nullptr;

// A map as the constructor leaves it: no nodes, the header red, its own
// left and right, no parent.
std::string MapState(Address map)
{
    // The header's colour is a byte; the three after it are padding.
    const Address header = Load<Address>(map + kMapHeader);
    return Words({Load<uint32_t>(map + kMapCount), Load<uint8_t>(header), Load<uint32_t>(header + 4),
                  Load<Address>(header + 8) == header ? 1u : 0u, Load<Address>(header + 0xC) == header ? 1u : 0u});
}

void* __fastcall DConstruct(void* self, void*)
{
    OPENSC4_FIRST("Construct 0x00897FED");
    if (g_inGame)
        return g_construct(self, nullptr);
    if (!g_compare[kHolder])
        return Construct(self);
    alignas(8) uint8_t scratch[holder::kSize];
    std::memcpy(scratch, self, sizeof scratch);
    void* const made = Construct(self);
    const std::string mine = Hex(static_cast<const uint8_t*>(self), holder::kGlyphs) + "| " +
                             MapState(At(self) + holder::kGlyphs) + (made == self ? "this" : "");
    std::string game;
    {
        GameSide side;
        void* const theirs = g_construct(scratch, nullptr);
        game = Hex(scratch, holder::kGlyphs) + "| " + MapState(At(scratch) + holder::kGlyphs) +
               (theirs == scratch ? "this" : "");
        Game<ThisOnly>(kMapDestruct)(scratch + holder::kGlyphs, nullptr);
    }
    Report("fontfile.construct", game, mine);
    return made;
}

uint32_t __fastcall DClose(void* self, void*)
{
    OPENSC4_FIRST("Close 0x008976A4");
    if (g_inGame || g_compare[kHolder])
        return g_close(self, nullptr);
    return Close(self) ? 1 : 0;
}

// What Open made: the result, which objects there are, the unwrapped bytes,
// the flags and kind, the stream's fields and what Open set in the scaler.
std::string OpenState(void* self, uint32_t result)
{
    const Address at = At(self);
    uint32_t present = 0;
    for (Address field = 0; field <= holder::kInput; field += 4)
        present = present << 1 | (Load<uint32_t>(at + field) ? 1u : 0u);
    const Vector& pfb = *static_cast<const Vector*>(Ptr(at + holder::kPfb));
    std::string text = Words({result & 0xFF, present, pfb.end - pfb.begin, pfb.begin ? Fnv(pfb.begin, pfb.end - pfb.begin) : 0,
                              Load<uint16_t>(at + holder::kFlags), Load<uint32_t>(at + holder::kKind)});
    if (const Address file = Load<Address>(at + holder::kStream))
        text += Words({Load<uint32_t>(file + stream::kBase), Load<uint32_t>(file + stream::kScrambled),
                       Load<uint8_t>(file + stream::kKey)});
    if (const Address made = Load<Address>(at + holder::kScaler)) {
        const Address type = Load<Address>(made + scaler::kClass);
        text += Words({Load<uint32_t>(made + scaler::kOpened), type ? Load<uint16_t>(type + scaler::kClassMode) : 0xFFFFu,
                       type ? Load<uint16_t>(type + scaler::kClassNameAny) : 0xFFFFu});
    }
    return text;
}

uint32_t __fastcall DOpen(void* self, void*, void* path)
{
    OPENSC4_FIRST("Open 0x00898CD9");
    if (g_inGame)
        return g_open(self, nullptr, path);
    if (!g_compare[kHolder])
        return Open(self, path) ? 1 : 0;
    // What Open keeps from before (Close leaves them): the unwrapped bytes of
    // a Type 1 face opened earlier, the flags and the kind.
    const Address at = At(self);
    const Vector before = Load<Vector>(at + holder::kPfb);
    const std::vector<uint8_t> bytes(static_cast<const uint8_t*>(Ptr(before.begin)),
                                     static_cast<const uint8_t*>(Ptr(before.begin)) + (before.end - before.begin));
    const uint16_t flags = Load<uint16_t>(at + holder::kFlags);
    const uint32_t kind = Load<uint32_t>(at + holder::kKind);
    const uint32_t result = Open(self, path) ? 1 : 0;
    const std::string mine = OpenState(self, result);
    // The game's opens the same file into a holder of its own, made by the
    // game's constructor, given what Open keeps (the bytes copied) and taken
    // down by the game's destructor.
    alignas(8) uint8_t scratch[holder::kSize];
    std::memset(scratch, 0, sizeof scratch);
    std::string game;
    {
        GameSide side;
        g_construct(scratch, nullptr);
        if (!bytes.empty()) {
            const Address copy =
                At(Game<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(bytes.size()));
            std::memcpy(Ptr(copy), bytes.data(), bytes.size());
            const Vector pfb{copy, copy + static_cast<uint32_t>(bytes.size()), copy + static_cast<uint32_t>(bytes.size())};
            Store<Vector>(At(scratch) + holder::kPfb, pfb);
        }
        Store<uint16_t>(At(scratch) + holder::kFlags, flags);
        Store<uint32_t>(At(scratch) + holder::kKind, kind);
        game = OpenState(scratch, g_open(scratch, nullptr, path));
        Game<ThisOnly>(kHolderDestruct)(scratch, nullptr);
    }
    Report("fontfile.open", game, mine);
    return result;
}

OPENSC4_THUNK(TConstruct, DConstruct, 0)
OPENSC4_THUNK(TClose, DClose, 0)
OPENSC4_THUNK(TOpen, DOpen, 4)

// --- names

using GetNameFn = uint32_t(__fastcall*)(void*, void*, void*, uint32_t);
using StylesFn = uint32_t(__fastcall*)(void*, void*);
using BuildFn = void(__cdecl*)();
using GlyphByNameFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
GetNameFn g_getName = nullptr;
StylesFn g_styles = nullptr;
BuildFn g_build = nullptr;
GlyphByNameFn g_glyphByName = nullptr;

std::string StringState(uint32_t result, Address string)
{
    const Address begin = Load<Address>(string + kStringBegin);
    const uint32_t size = Load<Address>(string + kStringEnd) - begin;
    return Words({result & 0xFF, size}) + (begin ? Hex(static_cast<const uint8_t*>(Ptr(begin)), size) : "");
}

uint32_t __fastcall DGetName(void* self, void*, void* out, uint32_t id)
{
    OPENSC4_FIRST("GetName 0x0089801E");
    if (g_inGame)
        return g_getName(self, nullptr, out, id);
    if (!g_compare[kNames])
        return GetName(self, out, id) ? 1 : 0;
    // Without a scaler both only read the string: the game's runs on it too.
    if (Load<uint32_t>(At(self) + holder::kScaler) == 0) {
        const uint32_t result = GetName(self, out, id) ? 1 : 0;
        const std::string mine = StringState(result, At(out));
        uint32_t game;
        {
            GameSide side;
            game = g_getName(self, nullptr, out, id);
        }
        Report("fontfile.getname", StringState(game, At(out)), mine);
        return game;
    }
    const uint32_t result = GetName(self, out, id) ? 1 : 0;
    const std::string mine = StringState(result, At(out));
    // The game's into an empty string of the same class: the name replaces
    // what a string held, and an empty one is left empty.
    GameString scratch;
    Game<ThisOnly>(addr::SC4String_SC4String)(&scratch, nullptr);
    scratch.vtable = Load<Address>(At(out));
    std::string game;
    {
        GameSide side;
        game = StringState(g_getName(self, nullptr, &scratch, id), At(&scratch));
    }
    if (scratch.begin)
        Game<DeleteOne>(addr::SC4_Free)(Ptr(scratch.begin));
    Report("fontfile.getname", game, mine);
    return result;
}

uint32_t __fastcall DStyles(void* self, void*)
{
    OPENSC4_FIRST("Styles 0x0089883F");
    if (g_inGame)
        return g_styles(self, nullptr);
    if (!g_compare[kNames])
        return Styles(self);
    return compare::Call("fontfile.styles", [&] {
        GameSide side;
        return g_styles(self, nullptr);
    }, [&] { return Styles(self); });
}

// Every entry of the table looked up in a map: the count and each name.
std::string NameMapState(Address map)
{
    const auto find = Game<MapFindFn>(kMapFind);
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < kNameTableBytes; i += 8) {
        const uint32_t key = Load<uint16_t>(kNameTable + i);
        const Address node = find(Ptr(map), nullptr, &key);
        const uint32_t value = node == Load<Address>(map + kMapHeader) ? 0xFFFFFFFF : Load<uint32_t>(node + kNodeValue);
        for (int b = 0; b < 4; ++b)
            hash = (hash ^ ((value >> (8 * b)) & 0xFF)) * 16777619u;
    }
    return Words({Load<uint32_t>(map + kMapCount), hash});
}

void __cdecl DBuildNameTable()
{
    OPENSC4_FIRST("BuildNameTable 0x008988F1");
    if (g_inGame) {
        g_build();
        return;
    }
    if (!g_compare[kNames]) {
        BuildNameTable();
        return;
    }
    // Ours into a map of our own, the game's into the static one.
    alignas(8) uint8_t scratch[12] = {};
    Game<ThisOnly>(addr::cGZDBSegmentPackedFile_HolesMapConstruct)(scratch, nullptr);
    BuildNameTableInto(At(scratch));
    const std::string mine = NameMapState(At(scratch));
    Game<ThisOnly>(kMapDestruct)(scratch, nullptr);
    {
        GameSide side;
        g_build();
    }
    Report("fontfile.buildnametable", NameMapState(kNameMap), mine);
}

uint32_t __fastcall DGlyphByName(void* self, void*, uint32_t character)
{
    OPENSC4_FIRST("GlyphByName 0x00898EB2");
    if (g_inGame)
        return g_glyphByName(self, nullptr, character);
    if (!g_compare[kNames])
        return GlyphByName(self, character);
    const uint32_t result = GlyphByName(self, character);
    // The game's on a copy of the holder with an empty map of its own, so it
    // looks the glyph up rather than finding what ours kept.
    alignas(8) uint8_t scratch[holder::kSize];
    std::memcpy(scratch, self, sizeof scratch);
    Game<ThisOnly>(addr::cGZDBSegmentPackedFile_HolesMapConstruct)(scratch + holder::kGlyphs, nullptr);
    uint32_t game;
    {
        GameSide side;
        game = g_glyphByName(scratch, nullptr, character);
    }
    Game<ThisOnly>(kMapDestruct)(scratch + holder::kGlyphs, nullptr);
    Report("fontfile.glyphbyname", Words({game}), Words({result}));
    return result;
}

OPENSC4_THUNK(TGetName, DGetName, 8)
OPENSC4_THUNK(TStyles, DStyles, 0)
OPENSC4_CDECL_THUNK(TBuildNameTable, DBuildNameTable, 0)
OPENSC4_THUNK(TGlyphByName, DGlyphByName, 4)

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void (*thunk)();
    void** original;
};

template <std::size_t N>
bool InstallAll(registry::Mode mode, Group group, const char* name, const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    g_compare[group] = mode == registry::Mode::Compare;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontfile: %s: %zu functions %s", name, N,
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

bool InstallStream(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontStream_Construct, &TConstructStream, Slot(&g_constructStream)},
        {addr::GZFontStream_Remaining, &TRemaining, Slot(&g_remaining)},
        {addr::GZFontStream_Read, &TRead, Slot(&g_read)},
        {addr::GZFontStream_ReadCallback, &TReadCallback, Slot(&g_readCallback)},
        {addr::GZFontStream_ReadAll, &TReadAll, Slot(&g_readAll)},
    };
    return InstallAll(mode, kStream, "stream", detours);
}

bool InstallHolder(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontFile_Construct, &TConstruct, Slot(&g_construct)},
        {addr::GZFontFile_Open, &TOpen, Slot(&g_open)},
        {addr::GZFontFile_Close, &TClose, Slot(&g_close)},
    };
    return InstallAll(mode, kHolder, "holder", detours);
}

bool InstallNames(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontFile_GetName, &TGetName, Slot(&g_getName)},
        {addr::GZFontFile_Styles, &TStyles, Slot(&g_styles)},
        {addr::GZFontFile_BuildNameTable, &TBuildNameTable, Slot(&g_build)},
        {addr::GZFontFile_GlyphByUnicodeName, &TGlyphByName, Slot(&g_glyphByName)},
    };
    return InstallAll(mode, kNames, "names", detours);
}

const bool g_registered[] = {
    registry::Register({"framework.font.t2k.stream", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontStream_Construct,
                        "Our font file stream: constructor (MXFN header), Remaining, Read, T2K's read callback and "
                        "ReadAll (5 detours; docs/fonts.md)",
                        true, InstallStream}),
    registry::Register({"framework.font.t2k.holder", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontFile_Construct,
                        "Our T2K holder: constructor, Open and Close (3 detours; docs/fonts.md)", true,
                        InstallHolder}),
    registry::Register({"framework.font.t2k.names", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontFile_GetName,
                        "Our face names: GetName, Styles, the glyph name table and GlyphByUnicodeName (4 detours; "
                        "docs/fonts.md)",
                        true, InstallNames}),
};

} // namespace
} // namespace opensc4::fontfile
