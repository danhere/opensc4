// Our font of the framework's fonts, the first slice of f3 of docs/fonts.md
// ("f3a: the font's object and properties are ours"): class 0x1AA404AC
// (0x15C bytes, vtable 0x00AC55D8, 64 slots), one face at one size. Each is
// a detour on the game's function, in three groups, off by default and
// installed at Attach (the first fonts are made while the game starts):
//
//   framework.font.font.object      the constructor (0x00899AAB), the
//                                   destructor (0x0089893F) and its deleting
//                                   slot 57 (0x00898FC0), QueryInterface (slot
//                                   0, 0x0089626E), Release (slot 2,
//                                   0x008972A8) and what it calls at the last
//                                   reference (0x00896293), Unload (slot 4,
//                                   0x008982E1), and the glyph cache's
//                                   constructor (0x00899A89), clear
//                                   (0x008982BA) and destructor (0x0089892E)
//   framework.font.font.properties  GetFamily (slot 5, 0x008989E5),
//                                   HasStyle (6, 0x00898A00), SetStyle (8,
//                                   0x008962A9), SetColours (14, 0x00898A13),
//                                   SetMode (16, 0x00896654), the grey
//                                   ramp's power and bias (17 to 20: 0x00896662,
//                                   0x00899987, 0x00896666, 0x008999CE),
//                                   GetPath and SetPath (21, 22: 0x00898A24,
//                                   0x00898A3F), the encoding (23, 24, 26:
//                                   0x0089666A, 0x00896678, 0x00896684),
//                                   GetScale (29, 0x00896692), the extra
//                                   scale (31, 32: 0x008966A7, 0x0089865F),
//                                   the spacing (33, 34: 0x008966BB,
//                                   0x008966D0), the line's metrics (35, 37,
//                                   38, 39: 0x008966EB, 0x008966F2,
//                                   0x008966F8, 0x00896702) and the glyph
//                                   cache of the mode (59, 0x008962C4)
//   framework.font.font.blitter     the blitter's mode of a draw mode
//                                   (0x00896B2C), SetBlitter (slot 54,
//                                   0x00896B45), Begin and End (55, 56:
//                                   0x00896B8E, 0x00896BC2)
//
// Slots 17 and 19 (four bytes each, FLD and RET, back to back) are too
// short for a detour: their vtable slots point at ours instead. The other
// slots of 0x00AC55D8 that are a few bytes of code folded with other
// classes' (1, 7, 9, 10, 12, 13, 15, 53) stay the game's, as does the rest
// of the font (f3's later slices: Load, sizes, layout, measuring, drawing,
// the glyph cache's lookups and the sheet), the T2K holder (f5) and the
// containers' code, all called at the game's addresses.
//
// The font (0x15C bytes): +0x04 a byte set while loaded, +0x08 the count,
// +0x0C the path and +0x20 the family (the game's strings), +0x34 the kind
// (1, or 2 when T2K's scaler has +0x5C), +0x38 the size in pixels, +0x3C the
// height of a line's glyphs, +0x40 the style bits, +0x44 and +0x48 two
// colours, +0x4C the draw mode, +0x54 and +0x58 the grey ramp's power and
// bias with +0x5C a vector of 256 levels made from them, +0x68 and +0x6C the
// scale, +0x70 an extra scale, +0x74 and +0x78 two spacings (the second
// added to every line), +0x7C the text encoding, +0x80 how deep Begin is,
// +0x84 and +0x88 two metrics SetSize sets (the scaler's +0x7C and +0x6C
// in pixels, and +0x7C alone), +0x8C the blitter, +0x90 six
// objects of 0xC bytes, +0xD8 and +0xF0 two vectors, +0xE4 the fallback
// character (0x20), +0xEC the T2K holder, +0xFC a word and +0x100 a list of
// blocks the glyph sheet frees, +0x104 the monochrome glyph cache and +0x130
// the grey one (0x2C bytes each: a glyph sheet, its block, pixels, width,
// height, pitch and next free column, then the table of its glyphs at
// +0x18).
//
// Compare mode runs ours and then the game's where both can run. What only
// reads compares its result; what writes only the font's own fields or its
// caller's out words (QueryInterface, SetStyle, SetColours, SetMode, the
// encoding, the spacing, the getters with outs) keeps what ours wrote aside,
// puts the bytes back, runs the game's and compares; GetFamily, GetPath and
// SetPath assign the same text twice and compare it. The setters that
// rebuild the levels or clear the caches (the ramp's power and bias, the
// extra scale) run the game's and compare the field it set with what ours
// would have set. The constructor runs the game's on the font and ours on a
// scratch font, compares them field by field and takes the scratch down
// with the game's destructor. What frees, releases or calls the blitter
// otherwise runs the game's alone. While the game's side runs, our detours
// inside it pass through to the game's.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace opensc4::fontobject {
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

template <typename R, typename... Args>
inline R Stdcall(std::uintptr_t fn, Args... args)
{
    return reinterpret_cast<R(__stdcall*)(Args...)>(fn)(args...);
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontobject: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions we call

constexpr std::uintptr_t kStringConstruct = 0x00578390;   // SC4String::SC4String, thiscall ()
constexpr std::uintptr_t kStringDestroy = 0x00414250;     // thiscall ()
constexpr std::uintptr_t kFree = 0x0090CF63;              // cdecl (block): a vector's block
constexpr std::uintptr_t kConstructArray = 0x0041D490;    // stdcall (array, size, count, constructor)
constexpr std::uintptr_t kDestroyArray = 0x0041D3C0;      // stdcall (array, size, count, destructor)
constexpr Address kSixConstruct = 0x0076BB00;             // +0x90's elements' constructor
constexpr Address kSixDestroy = 0x00676DA0;               // and destructor
constexpr std::uintptr_t kListMake = 0x0093F7EB;          // thiscall on +0x100 (const allocator*)
constexpr std::uintptr_t kListClear = 0x009A841F;         // thiscall on +0x100 ()
constexpr std::uintptr_t kListFree = 0x0096EDBC;          // thiscall on +0x100 ()
constexpr std::uintptr_t kTableMake = 0x00969AD5;         // thiscall on a cache's +0x18 ()
constexpr std::uintptr_t kTableClear = 0x0097C1A2;        // thiscall on a cache's +0x18 ()
constexpr std::uintptr_t kTableFree = 0x0099784E;         // thiscall on a cache's +0x18 ()
constexpr std::uintptr_t kHolderFree = 0x00897E36;        // thiscall on the T2K holder (): its destructor
constexpr std::uintptr_t kRebuildLevels = 0x008993BB;     // thiscall (): the levels +0x5C from +0x54, +0x58

// The constants the setters clamp by, read where the game reads them.
constexpr Address kPowerLow = 0x00A81054;   // 0.0
constexpr Address kPowerHigh = 0x00A94D50;  // 10.0, also the extra scale's high
constexpr Address kBiasLow = 0x00A8FE78;    // -1.0
constexpr Address kOne = 0x00A81228;        // 1.0, the bias's high
constexpr Address kExtraLow = 0x00A8C950;   // 0.1

// ====================================================================================
// Layout

namespace font {
constexpr uint32_t kSize = 0x15C;
constexpr Address kVtable = 0x00AC55D8;
constexpr Address kLoaded = 0x04;
constexpr Address kRefs = 0x08;
constexpr Address kPath = 0x0C;
constexpr Address kFamily = 0x20;
constexpr Address kKind = 0x34;
constexpr Address kPixels = 0x38;
constexpr Address kHeight = 0x3C;
constexpr Address kStyles = 0x40;
constexpr Address kColour = 0x44;
constexpr Address kColour2 = 0x48;
constexpr Address kMode = 0x4C;
constexpr Address kMode2 = 0x50;
constexpr Address kPower = 0x54;
constexpr Address kBias = 0x58;
constexpr Address kLevels = 0x5C;
constexpr Address kScaleX = 0x68;
constexpr Address kScaleY = 0x6C;
constexpr Address kExtra = 0x70;
constexpr Address kSpacing = 0x74;
constexpr Address kLineSpacing = 0x78;
constexpr Address kEncoding = 0x7C;
constexpr Address kDepth = 0x80;
constexpr Address kBaselineOffset = 0x84;
constexpr Address kBlitter = 0x8C;
constexpr Address kSix = 0x90;
constexpr Address kVectorD8 = 0xD8;
constexpr Address kFallback = 0xE4;
constexpr Address kE8 = 0xE8;
constexpr Address kHolder = 0xEC;
constexpr Address kVectorF0 = 0xF0;
constexpr Address kFC = 0xFC;
constexpr Address kBlocks = 0x100;
constexpr Address kMonoCache = 0x104;
constexpr Address kGreyCache = 0x130;
// Its own slots it calls.
constexpr std::size_t kAddRef = 0x04, kUnload = 0x10, kLineHeight = 0x8C, kBaseline = 0x98,
                      kSetBlitter = 0xD8, kDelete = 0xE4;
} // namespace font

constexpr uint32_t kHolderSize = 0x34;
constexpr uint32_t kCacheWords = 6; // a cache's sheet: block, pixels, width, height, pitch, next column
constexpr Address kCacheTable = 0x18;
constexpr uint32_t kIidGzUnknown = 1;
constexpr uint32_t kIidFont = 0x10673960;
constexpr uint32_t kStyleBits = 0x3C;

// cIGZString's slots: assign from text, and its text.
constexpr std::size_t kIgzAssignText = 0x10, kIgzText = 0x14, kIgzData = 0x18, kIgzLength = 0x1C;
// The blitter's slots the font calls: it is given and taken the font, and
// Begin and End a draw.
constexpr std::size_t kBlitAddRef = 0x04, kBlitRelease = 0x08, kBlitGiven = 0x0C, kBlitTaken = 0x10,
                      kBlitBegin = 0x14, kBlitEnd = 0x18;

// ====================================================================================
// The object

// The game's 0x00899A89 (thiscall): a glyph cache, its sheet's six words 0
// and its table made. Returns this.
void* CacheConstruct(void* self)
{
    const Address at = At(self);
    for (uint32_t i = 0; i < kCacheWords; ++i)
        Store<uint32_t>(at + 4 * i, 0);
    This<void*>(kTableMake, Ptr(at + kCacheTable));
    return self;
}

// The game's 0x008982BA (thiscall): the sheet's block freed, its six words
// 0, the table cleared (what its clear returns).
uint32_t CacheClear(void* self)
{
    const Address at = At(self);
    Cdecl<void>(addr::SC4_operator_delete, Load<void*>(at));
    for (uint32_t i = 0; i < kCacheWords; ++i)
        Store<uint32_t>(at + 4 * i, 0);
    return This<uint32_t>(kTableClear, Ptr(at + kCacheTable));
}

// The game's 0x0089892E (thiscall): cleared, then its table taken down.
uint32_t CacheDestroy(void* self)
{
    CacheClear(self);
    return This<uint32_t>(kTableFree, Ptr(At(self) + kCacheTable));
}

// The game's 0x0089626E (slot 0, ret 8): 0x10673960 and 1 give the font,
// AddRef'd through slot 1.
bool QueryInterface(void* self, uint32_t iid, Address* out)
{
    if (iid != kIidFont && iid != kIidGzUnknown)
        return false;
    *out = At(self);
    Virtual<uint32_t>(self, font::kAddRef);
    return true;
}

// The game's 0x00896293 (thiscall): Unload, then the deleting destructor.
void Destroy(void* self)
{
    Virtual<uint32_t>(self, font::kUnload);
    Virtual<void*>(self, font::kDelete, uint32_t{1});
}

// The game's 0x008972A8 (slot 2): the count less one while above 1, signed;
// at 1 or below the font is destroyed (the count left) and 0 returned.
uint32_t Release(void* self)
{
    const int32_t refs = Load<int32_t>(At(self) + font::kRefs);
    if (refs > 1) {
        Store<int32_t>(At(self) + font::kRefs, refs - 1);
        return static_cast<uint32_t>(refs - 1);
    }
    Destroy(self);
    return 0;
}

// The game's 0x008982E1 (slot 4): if loaded, the blitter given up (slot
// 54), both caches cleared, the sheet's blocks freed and their list
// emptied, the face closed. Returns true.
bool Unload(void* self)
{
    const Address at = At(self);
    if (!Load<uint8_t>(at + font::kLoaded))
        return true;
    Virtual<uint32_t>(self, font::kSetBlitter, uint32_t{0});
    CacheClear(Ptr(at + font::kMonoCache));
    CacheClear(Ptr(at + font::kGreyCache));
    Store<uint32_t>(at + font::kFC, 0);
    const Address list = at + font::kBlocks;
    for (Address node = Load<Address>(Load<Address>(list)); node != Load<Address>(list); node = Load<Address>(node))
        Cdecl<void>(addr::SC4_operator_delete, Load<void*>(node + 8));
    This<void>(kListClear, Ptr(list));
    This<uint32_t>(addr::GZFontFile_Close, Ptr(Load<Address>(at + font::kHolder)));
    Store<uint8_t>(at + font::kLoaded, 0);
    return true;
}

// The game's 0x00899AAB (thiscall): the fields, the strings, the six
// objects, the list and the caches, then a T2K holder. Returns this.
void* Construct(void* self)
{
    const Address at = At(self);
    Store<Address>(at, font::kVtable);
    Store<uint8_t>(at + font::kLoaded, 0);
    Store<uint32_t>(at + font::kRefs, 0);
    This<void*>(kStringConstruct, Ptr(at + font::kPath));
    This<void*>(kStringConstruct, Ptr(at + font::kFamily));
    Store<float>(at + font::kPower, 1.0f);
    Store<float>(at + font::kBias, 0.0f);
    Store<uint32_t>(at + font::kPixels, 0x10);
    Store<uint32_t>(at + font::kHeight, 0x10);
    Store<uint32_t>(at + font::kKind, 0);
    Store<uint32_t>(at + font::kStyles, 0);
    Store<uint32_t>(at + font::kColour, 0);
    Store<uint32_t>(at + font::kColour2, 0);
    Store<uint32_t>(at + font::kMode, 1);
    Store<uint32_t>(at + font::kMode2, 2);
    for (Address field = font::kLevels; field < font::kLevels + 12; field += 4)
        Store<uint32_t>(at + field, 0);
    Store<float>(at + font::kScaleX, 1.0f);
    Store<float>(at + font::kScaleY, 1.0f);
    Store<float>(at + font::kExtra, 1.0f);
    for (Address field = font::kSpacing; field <= font::kBlitter; field += 4)
        Store<uint32_t>(at + field, 0);
    Stdcall<void>(kConstructArray, Ptr(at + font::kSix), uint32_t{0xC}, uint32_t{6}, kSixConstruct);
    for (Address field = font::kVectorD8; field < font::kVectorD8 + 12; field += 4)
        Store<uint32_t>(at + field, 0);
    Store<uint32_t>(at + font::kFallback, 0x20);
    Store<uint32_t>(at + font::kE8, 0);
    Store<uint32_t>(at + font::kHolder, 0);
    for (Address field = font::kVectorF0; field < font::kVectorF0 + 12; field += 4)
        Store<uint32_t>(at + field, 0);
    Store<uint32_t>(at + font::kFC, 0);
    const uint8_t allocator = 0; // the list's allocator, an empty object passed by reference
    This<void*>(kListMake, Ptr(at + font::kBlocks), &allocator);
    CacheConstruct(Ptr(at + font::kMonoCache));
    CacheConstruct(Ptr(at + font::kGreyCache));
    void* const space = Cdecl<void*>(addr::SC4_operator_new, kHolderSize);
    Store<Address>(at + font::kHolder, space ? At(This<void*>(addr::GZFontFile_Construct, space)) : 0);
    return self;
}

// The game's 0x0089893F (thiscall): the vtable, Unload, the holder taken
// down and freed, then the caches, the list, the vectors, the six objects
// and the strings, last first.
void Destruct(void* self)
{
    const Address at = At(self);
    Store<Address>(at, font::kVtable);
    Unload(self);
    if (const Address holder = Load<Address>(at + font::kHolder)) {
        This<void>(kHolderFree, Ptr(holder));
        Cdecl<void>(addr::SC4_operator_delete, Ptr(holder));
    }
    Store<uint32_t>(at + font::kHolder, 0);
    CacheDestroy(Ptr(at + font::kGreyCache));
    CacheDestroy(Ptr(at + font::kMonoCache));
    This<void>(kListFree, Ptr(at + font::kBlocks));
    for (const Address vector : {font::kVectorF0, font::kVectorD8}) {
        if (const Address block = Load<Address>(at + vector))
            Cdecl<void>(kFree, Ptr(block));
    }
    Stdcall<void>(kDestroyArray, Ptr(at + font::kSix), uint32_t{0xC}, uint32_t{6}, kSixDestroy);
    if (const Address block = Load<Address>(at + font::kLevels))
        Cdecl<void>(kFree, Ptr(block));
    This<void>(kStringDestroy, Ptr(at + font::kFamily));
    This<void>(kStringDestroy, Ptr(at + font::kPath));
}

// The game's 0x00898FC0 (slot 57, ret 4): destroyed, and freed if bit 0.
void* DeletingDestruct(void* self, uint32_t flags)
{
    Destruct(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// Properties

// The game's 0x008989E5 (slot 5, ret 4) and 0x00898A24 (slot 21, ret 4):
// the family or the path into `out`, through its slot 4 with the text of
// ours (what that returns). 0x00898A3F (slot 22, ret 4): the path from
// `in`'s text.
uint32_t CopyOut(Address string, void* out) { return Virtual<uint32_t>(out, kIgzAssignText, Virtual<Address>(Ptr(string), kIgzText)); }
uint32_t GetFamily(void* self, void* out) { return CopyOut(At(self) + font::kFamily, out); }
uint32_t GetPath(void* self, void* out) { return CopyOut(At(self) + font::kPath, out); }
uint32_t SetPath(void* self, void* in)
{
    return Virtual<uint32_t>(Ptr(At(self) + font::kPath), kIgzAssignText, Virtual<Address>(in, kIgzText));
}

// The game's 0x00898A00 (slot 6, ret 4): whether every one of `bits` is set.
uint32_t HasStyle(void* self, uint32_t bits)
{
    return (Load<uint32_t>(At(self) + font::kStyles) & bits) == bits ? 1 : 0;
}

// The game's 0x008962A9 (slot 8, ret 8): bits 2 to 5 of `bits` set or
// cleared (the byte `on`). EAX is the mask it applied.
uint32_t SetStyle(void* self, uint32_t bits, uint8_t on)
{
    const Address field = At(self) + font::kStyles;
    const uint32_t kept = bits & kStyleBits;
    if (on) {
        Store<uint32_t>(field, Load<uint32_t>(field) | kept);
        return kept;
    }
    Store<uint32_t>(field, Load<uint32_t>(field) & ~kept);
    return ~kept;
}

// The game's 0x00898A13 (slot 14, ret 8): the two colours; EAX the second.
uint32_t SetColours(void* self, uint32_t colour, uint32_t colour2)
{
    Store<uint32_t>(At(self) + font::kColour, colour);
    Store<uint32_t>(At(self) + font::kColour2, colour2);
    return colour2;
}

// The game's 0x00896654 (slot 16, ret 4): the draw mode, unless 0.
uint32_t SetMode(void* self, uint32_t mode)
{
    if (mode)
        Store<uint32_t>(At(self) + font::kMode, mode);
    return mode;
}

// The game's 0x00896662 and 0x00896666 (slots 17 and 19): the ramp's power
// and bias.
float GetPower(void* self) { return Load<float>(At(self) + font::kPower); }
float GetBias(void* self) { return Load<float>(At(self) + font::kBias); }

// What the setters store: the power below 0 is 0 and above 10 is 10, the
// bias kept in -1 to 1, the extra scale in 0.1 to 10 (a NaN kept as it is).
float ClampPower(float power)
{
    if (power < Load<float>(kPowerLow))
        power = 0.0f;
    if (power > Load<float>(kPowerHigh))
        power = Load<float>(kPowerHigh);
    return power;
}

float ClampBias(float bias)
{
    if (bias < Load<float>(kBiasLow))
        bias = Load<float>(kBiasLow);
    if (bias > Load<float>(kOne))
        bias = 1.0f;
    return bias;
}

float ClampExtra(float extra)
{
    if (extra < Load<float>(kExtraLow))
        extra = Load<float>(kExtraLow);
    if (extra > Load<float>(kPowerHigh))
        extra = Load<float>(kPowerHigh);
    return extra;
}

// The game's 0x00899987 (slot 18, ret 4) and 0x008999CE (slot 20, ret 4):
// the power or the bias clamped and, if it differs from the field
// (unordered counts as differing), stored and the levels rebuilt.
void SetPower(void* self, float power)
{
    power = ClampPower(power);
    const Address field = At(self) + font::kPower;
    if (!(Load<float>(field) == power)) {
        Store<float>(field, power);
        This<void>(kRebuildLevels, self);
    }
}

void SetBias(void* self, float bias)
{
    bias = ClampBias(bias);
    const Address field = At(self) + font::kBias;
    if (!(Load<float>(field) == bias)) {
        Store<float>(field, bias);
        This<void>(kRebuildLevels, self);
    }
}

// The game's 0x0089666A (slot 23, ret 4): the encoding into `out`; AL 1,
// the rest of EAX the encoding's upper bytes, as the game loads it there.
uint32_t GetEncoding(void* self, uint32_t* out)
{
    const uint32_t encoding = Load<uint32_t>(At(self) + font::kEncoding);
    *out = encoding;
    return (encoding & ~0xFFu) | 1;
}

// The game's 0x00896678 (slot 24, ret 4): the encoding; AL 1 over it.
uint32_t SetEncoding(void* self, uint32_t encoding)
{
    Store<uint32_t>(At(self) + font::kEncoding, encoding);
    return (encoding & ~0xFFu) | 1;
}

// The game's 0x00896684 (slot 26, ret 4): true for encoding 2 alone.
uint32_t SupportsEncoding(uint32_t encoding) { return encoding == 2 ? 1 : 0; }

// The game's 0x00896692 (slot 29, ret 8): the scale.
void GetScale(void* self, float* x, float* y)
{
    *x = Load<float>(At(self) + font::kScaleX);
    *y = Load<float>(At(self) + font::kScaleY);
}

// The game's 0x008966A7 (slot 31, ret 8): the extra scale, and 1.
void GetExtra(void* self, float* extra, float* one)
{
    *extra = Load<float>(At(self) + font::kExtra);
    *one = 1.0f;
}

// The game's 0x0089865F (slot 32, ret 8; the second argument unused): the
// extra scale clamped and, if it differs, stored and both caches cleared.
void SetExtra(void* self, float extra)
{
    extra = ClampExtra(extra);
    const Address at = At(self);
    if (!(Load<float>(at + font::kExtra) == extra)) {
        Store<float>(at + font::kExtra, extra);
        CacheClear(Ptr(at + font::kMonoCache));
        CacheClear(Ptr(at + font::kGreyCache));
    }
}

// The game's 0x008966BB (slot 33, ret 8): the two spacings; EAX the second.
uint32_t GetSpacing(void* self, uint32_t* spacing, uint32_t* lineSpacing)
{
    *spacing = Load<uint32_t>(At(self) + font::kSpacing);
    const uint32_t second = Load<uint32_t>(At(self) + font::kLineSpacing);
    *lineSpacing = second;
    return second;
}

// The game's 0x008966D0 (slot 34, ret 8): each written only if it differs;
// EAX the second.
uint32_t SetSpacing(void* self, uint32_t spacing, uint32_t lineSpacing)
{
    const Address at = At(self);
    if (Load<uint32_t>(at + font::kSpacing) != spacing)
        Store<uint32_t>(at + font::kSpacing, spacing);
    if (Load<uint32_t>(at + font::kLineSpacing) != lineSpacing)
        Store<uint32_t>(at + font::kLineSpacing, lineSpacing);
    return lineSpacing;
}

// The game's 0x008966EB (slot 35): a line's height, the glyphs' and the
// line spacing. 0x008966F2 (slot 37, ret 8; both arguments unused): the
// glyphs' height. 0x008966F8 (slot 38): +0x84 and the line spacing.
// 0x00896702 (slot 39): slot 35 less slot 38, slot 38 asked first.
uint32_t LineHeight(void* self)
{
    return Load<uint32_t>(At(self) + font::kLineSpacing) + Load<uint32_t>(At(self) + font::kHeight);
}
uint32_t TextHeight(void* self) { return Load<uint32_t>(At(self) + font::kHeight); }
uint32_t Baseline(void* self)
{
    return Load<uint32_t>(At(self) + font::kBaselineOffset) + Load<uint32_t>(At(self) + font::kLineSpacing);
}
uint32_t BelowBaseline(void* self)
{
    const uint32_t baseline = Virtual<uint32_t>(self, font::kBaseline);
    return Virtual<uint32_t>(self, font::kLineHeight) - baseline;
}

// The game's 0x008962C4 (slot 59): the monochrome cache in mode 1, else
// the grey one.
Address GlyphCache(void* self)
{
    return At(self) + (Load<uint32_t>(At(self) + font::kMode) == 1 ? font::kMonoCache : font::kGreyCache);
}

// ====================================================================================
// The blitter

// The game's 0x00896B2C (cdecl): the blitter's mode of a draw mode: 2 and
// 3 as they are, anything else 1.
uint32_t BlitMode(uint32_t mode) { return mode == 2 || mode == 3 ? mode : 1; }

// The game's 0x00896B45 (slot 54, ret 4): a new blitter AddRef'd and given
// the font (its slot 3), the old one taken it (slot 4) and released.
void SetBlitter(void* self, void* blitter)
{
    const Address field = At(self) + font::kBlitter;
    if (Load<Address>(field) == At(blitter))
        return;
    if (blitter) {
        Virtual<uint32_t>(blitter, kBlitAddRef);
        Virtual<uint32_t>(blitter, kBlitGiven, self);
    }
    if (void* const old = Load<void*>(field)) {
        Virtual<uint32_t>(old, kBlitTaken, self);
        Virtual<uint32_t>(Load<void*>(field), kBlitRelease);
    }
    Store<Address>(field, At(blitter));
}

// The game's 0x00896B8E (slot 55, ret 4): at depth 0 the blitter's Begin
// (slot 5: the font, the picture, the blitter's mode); the depth one more.
// 0x00896BC2 (slot 56, ret 4): at depth 1 its End (slot 6); one less.
// Both return true.
bool Begin(void* self, uint32_t picture)
{
    const Address at = At(self);
    if (Load<uint32_t>(at + font::kDepth) == 0)
        Virtual<uint32_t>(Load<void*>(at + font::kBlitter), kBlitBegin, self, picture,
                          BlitMode(Load<uint32_t>(at + font::kMode)));
    Store<uint32_t>(at + font::kDepth, Load<uint32_t>(at + font::kDepth) + 1);
    return true;
}

bool End(void* self, uint32_t picture)
{
    const Address at = At(self);
    if (Load<uint32_t>(at + font::kDepth) == 1)
        Virtual<uint32_t>(Load<void*>(at + font::kBlitter), kBlitEnd, self, picture,
                          BlitMode(Load<uint32_t>(at + font::kMode)));
    Store<uint32_t>(at + font::kDepth, Load<uint32_t>(at + font::kDepth) - 1);
    return true;
}

// ====================================================================================
// Detours

enum Group { kObject, kProperties, kBlitter, kGroups };
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

bool Ours(Group group) { return !g_inGame && !g_compare[group]; }

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

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

// The bytes a comparison keeps aside: the font's and the words its caller
// passed for answers.
struct Kept {
    std::vector<std::pair<uint8_t*, std::vector<uint8_t>>> regions;
    Kept(std::initializer_list<std::pair<void*, std::size_t>> list)
    {
        for (const auto& [at, size] : list) {
            auto* const bytes = static_cast<uint8_t*>(at);
            regions.emplace_back(bytes, std::vector<uint8_t>(bytes, bytes + size));
        }
    }
    std::string Now() const
    {
        std::string text;
        for (const auto& [at, before] : regions)
            text += Hex(at, before.size()) + " ";
        return text;
    }
    void PutBack() const
    {
        for (const auto& [at, before] : regions)
            std::memcpy(at, before.data(), before.size());
    }
};

// Ours, what it wrote and returned kept, the bytes put back; then the
// game's, whose result is returned.
template <typename Mine, typename Theirs>
uint32_t Twice(const char* id, const Kept& kept, uint32_t mask, Mine&& mine, Theirs&& theirs)
{
    const uint32_t ours = mine() & mask;
    const std::string oursText = Words({ours}) + kept.Now();
    kept.PutBack();
    uint32_t result;
    {
        GameSide side;
        result = theirs();
    }
    Report(id, Words({result & mask}) + kept.Now(), oursText);
    return result;
}

std::pair<void*, std::size_t> Whole(void* self) { return {self, font::kSize}; }

// A cIGZString's text.
std::string IgzText(void* s)
{
    const auto data = Virtual<const char*>(s, kIgzData);
    const uint32_t length = Virtual<uint32_t>(s, kIgzLength);
    return data ? std::string(data, length) : std::string();
}

// A font as text for the constructor's comparison: every field but where
// the strings, vectors, list, caches and holder are (whether there is one).
std::string Layout(void* self)
{
    const Address at = At(self);
    std::string text = Words({Load<Address>(at), Load<uint8_t>(at + font::kLoaded), Load<uint32_t>(at + font::kRefs)});
    text += IgzText(Ptr(at + font::kPath)) + "|" + IgzText(Ptr(at + font::kFamily)) + "| ";
    for (Address field = font::kKind; field < font::kSix; field += 4) {
        const bool pointer = (field >= font::kLevels && field < font::kLevels + 12) || field == font::kBlitter;
        const uint32_t value = Load<uint32_t>(at + field);
        text += Words({pointer ? uint32_t{value != 0} : value});
    }
    text += Hex(static_cast<const uint8_t*>(Ptr(at + font::kSix)), 0x48) == std::string(0x90, '0') ? "six0 " : "six ";
    for (const Address field : {font::kVectorD8, font::kVectorD8 + 4, font::kVectorD8 + 8, font::kFallback, font::kE8,
                                font::kVectorF0, font::kVectorF0 + 4, font::kVectorF0 + 8, font::kFC})
        text += Words({Load<uint32_t>(at + field)});
    text += Words({Load<Address>(at + font::kHolder) != 0, Load<Address>(at + font::kBlocks) != 0});
    for (const Address cache : {font::kMonoCache, font::kGreyCache})
        text += Hex(static_cast<const uint8_t*>(Ptr(at + cache)), 4 * kCacheWords) + " ";
    return text;
}

using ThisOnlyFn = uint32_t(__fastcall*)(void*, void*);
using ThisOneFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
using ThisTwoFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t);
using FloatGetFn = float(__fastcall*)(void*, void*);
using FloatSetFn = uint32_t(__fastcall*)(void*, void*, float);
using FloatSet2Fn = uint32_t(__fastcall*)(void*, void*, float, uint32_t);
using CdeclOneFn = uint32_t(__cdecl*)(uint32_t);

// --- the object

ThisOnlyFn g_construct = nullptr;
ThisOnlyFn g_destruct = nullptr;
ThisOneFn g_deletingDestruct = nullptr;
ThisTwoFn g_queryInterface = nullptr;
ThisOnlyFn g_release = nullptr;
ThisOnlyFn g_destroy = nullptr;
ThisOnlyFn g_unload = nullptr;
ThisOnlyFn g_cacheConstruct = nullptr;
ThisOnlyFn g_cacheClear = nullptr;
ThisOnlyFn g_cacheDestroy = nullptr;

uint32_t __fastcall DConstruct(void* self, void*)
{
    OPENSC4_FIRST("Construct 0x00899AAB");
    if (Ours(kObject))
        return At(Construct(self));
    if (g_inGame)
        return g_construct(self, nullptr);
    uint32_t result;
    {
        GameSide side;
        result = g_construct(self, nullptr);
    }
    void* const scratch = Cdecl<void*>(addr::SC4_operator_new, font::kSize);
    if (!scratch)
        return result;
    Construct(scratch);
    Report("fontobject.construct", Layout(self), Layout(scratch));
    {
        GameSide side;
        g_destruct(scratch, nullptr);
    }
    Cdecl<void>(addr::SC4_operator_delete, scratch);
    return result;
}

uint32_t __fastcall DDestruct(void* self, void*)
{
    OPENSC4_FIRST("Destruct 0x0089893F");
    if (!Ours(kObject))
        return g_destruct(self, nullptr);
    Destruct(self);
    return 0;
}

uint32_t __fastcall DDeletingDestruct(void* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("DeletingDestruct 0x00898FC0");
    if (!Ours(kObject))
        return g_deletingDestruct(self, nullptr, flags);
    return At(DeletingDestruct(self, flags));
}

uint32_t __fastcall DQueryInterface(void* self, void*, uint32_t iid, Address* out)
{
    OPENSC4_FIRST("QueryInterface 0x0089626E");
    if (g_inGame)
        return g_queryInterface(self, nullptr, iid, At(out));
    if (!g_compare[kObject])
        return QueryInterface(self, iid, out) ? 1 : 0;
    // Neither reads `out` for an ID they do not answer.
    if (iid != kIidFont && iid != kIidGzUnknown)
        return Twice("fontobject.queryinterface", Kept{Whole(self)}, 0xFF,
                     [&] { return QueryInterface(self, iid, out) ? 1u : 0u; },
                     [&] { return g_queryInterface(self, nullptr, iid, At(out)); });
    return Twice("fontobject.queryinterface", Kept{Whole(self), {out, 4}}, 0xFF,
                 [&] { return QueryInterface(self, iid, out) ? 1u : 0u; },
                 [&] { return g_queryInterface(self, nullptr, iid, At(out)); });
}

uint32_t __fastcall DRelease(void* self, void*)
{
    OPENSC4_FIRST("Release 0x008972A8");
    if (!Ours(kObject))
        return g_release(self, nullptr);
    return Release(self);
}

uint32_t __fastcall DDestroy(void* self, void*)
{
    OPENSC4_FIRST("Destroy 0x00896293");
    if (!Ours(kObject))
        return g_destroy(self, nullptr);
    Destroy(self);
    return 0;
}

uint32_t __fastcall DUnload(void* self, void*)
{
    OPENSC4_FIRST("Unload 0x008982E1");
    if (!Ours(kObject))
        return g_unload(self, nullptr);
    return Unload(self) ? 1 : 0;
}

uint32_t __fastcall DCacheConstruct(void* self, void*)
{
    OPENSC4_FIRST("CacheConstruct 0x00899A89");
    if (!Ours(kObject))
        return g_cacheConstruct(self, nullptr);
    return At(CacheConstruct(self));
}

uint32_t __fastcall DCacheClear(void* self, void*)
{
    OPENSC4_FIRST("CacheClear 0x008982BA");
    if (!Ours(kObject))
        return g_cacheClear(self, nullptr);
    return CacheClear(self);
}

uint32_t __fastcall DCacheDestroy(void* self, void*)
{
    OPENSC4_FIRST("CacheDestroy 0x0089892E");
    if (!Ours(kObject))
        return g_cacheDestroy(self, nullptr);
    return CacheDestroy(self);
}

OPENSC4_THUNK(TConstruct, DConstruct, 0)
OPENSC4_THUNK(TDestruct, DDestruct, 0)
OPENSC4_THUNK(TDeletingDestruct, DDeletingDestruct, 4)
OPENSC4_THUNK(TQueryInterface, DQueryInterface, 8)
OPENSC4_THUNK(TRelease, DRelease, 0)
OPENSC4_THUNK(TDestroy, DDestroy, 0)
OPENSC4_THUNK(TUnload, DUnload, 0)
OPENSC4_THUNK(TCacheConstruct, DCacheConstruct, 0)
OPENSC4_THUNK(TCacheClear, DCacheClear, 0)
OPENSC4_THUNK(TCacheDestroy, DCacheDestroy, 0)

// --- properties

ThisOneFn g_getFamily = nullptr;
ThisOneFn g_hasStyle = nullptr;
ThisTwoFn g_setStyle = nullptr;
ThisTwoFn g_setColours = nullptr;
ThisOneFn g_setMode = nullptr;
FloatGetFn g_getPower = nullptr;
FloatSetFn g_setPower = nullptr;
FloatGetFn g_getBias = nullptr;
FloatSetFn g_setBias = nullptr;
ThisOneFn g_getPath = nullptr;
ThisOneFn g_setPath = nullptr;
ThisOneFn g_getEncoding = nullptr;
ThisOneFn g_setEncoding = nullptr;
ThisOneFn g_supportsEncoding = nullptr;
ThisTwoFn g_getScale = nullptr;
ThisTwoFn g_getExtra = nullptr;
FloatSet2Fn g_setExtra = nullptr;
ThisTwoFn g_getSpacing = nullptr;
ThisTwoFn g_setSpacing = nullptr;
ThisOnlyFn g_lineHeight = nullptr;
ThisTwoFn g_textHeight = nullptr;
ThisOnlyFn g_baseline = nullptr;
ThisOnlyFn g_belowBaseline = nullptr;
ThisOnlyFn g_glyphCache = nullptr;

// What only reads: its result compared.
template <typename Mine, typename Theirs>
uint32_t Pure(const char* id, uint32_t mask, Mine&& mine, Theirs&& theirs)
{
    const uint32_t ours = mine() & mask;
    uint32_t result;
    {
        GameSide side;
        result = theirs();
    }
    Report(id, Words({result & mask}), Words({ours}));
    return result;
}

// GetFamily and GetPath: ours, the text it gave; then the game's.
uint32_t CopyOutDetour(const char* id, void* self, void* out, uint32_t (*mine)(void*, void*), ThisOneFn theirs)
{
    if (g_inGame)
        return theirs(self, nullptr, At(out));
    if (!g_compare[kProperties])
        return mine(self, out);
    const uint32_t ours = mine(self, out);
    const std::string oursText = Words({ours}) + IgzText(out);
    uint32_t result;
    {
        GameSide side;
        result = theirs(self, nullptr, At(out));
    }
    Report(id, Words({result}) + IgzText(out), oursText);
    return result;
}

uint32_t __fastcall DGetFamily(void* self, void*, void* out)
{
    OPENSC4_FIRST("GetFamily 0x008989E5");
    return CopyOutDetour("fontobject.getfamily", self, out, &GetFamily, g_getFamily);
}

uint32_t __fastcall DGetPath(void* self, void*, void* out)
{
    OPENSC4_FIRST("GetPath 0x00898A24");
    return CopyOutDetour("fontobject.getpath", self, out, &GetPath, g_getPath);
}

uint32_t __fastcall DSetPath(void* self, void*, void* in)
{
    OPENSC4_FIRST("SetPath 0x00898A3F");
    if (g_inGame)
        return g_setPath(self, nullptr, At(in));
    if (!g_compare[kProperties])
        return SetPath(self, in);
    const uint32_t ours = SetPath(self, in);
    const std::string oursText = Words({ours}) + IgzText(Ptr(At(self) + font::kPath));
    uint32_t result;
    {
        GameSide side;
        result = g_setPath(self, nullptr, At(in));
    }
    Report("fontobject.setpath", Words({result}) + IgzText(Ptr(At(self) + font::kPath)), oursText);
    return result;
}

uint32_t __fastcall DHasStyle(void* self, void*, uint32_t bits)
{
    OPENSC4_FIRST("HasStyle 0x00898A00");
    if (g_inGame)
        return g_hasStyle(self, nullptr, bits);
    if (!g_compare[kProperties])
        return HasStyle(self, bits);
    return Pure("fontobject.hasstyle", 0xFFFFFFFF, [&] { return HasStyle(self, bits); },
                [&] { return g_hasStyle(self, nullptr, bits); });
}

uint32_t __fastcall DSetStyle(void* self, void*, uint32_t bits, uint32_t on)
{
    OPENSC4_FIRST("SetStyle 0x008962A9");
    if (g_inGame)
        return g_setStyle(self, nullptr, bits, on);
    if (!g_compare[kProperties])
        return SetStyle(self, bits, static_cast<uint8_t>(on));
    return Twice("fontobject.setstyle", Kept{Whole(self)}, 0xFFFFFFFF,
                 [&] { return SetStyle(self, bits, static_cast<uint8_t>(on)); },
                 [&] { return g_setStyle(self, nullptr, bits, on); });
}

uint32_t __fastcall DSetColours(void* self, void*, uint32_t colour, uint32_t colour2)
{
    OPENSC4_FIRST("SetColours 0x00898A13");
    if (g_inGame)
        return g_setColours(self, nullptr, colour, colour2);
    if (!g_compare[kProperties])
        return SetColours(self, colour, colour2);
    return Twice("fontobject.setcolours", Kept{Whole(self)}, 0xFFFFFFFF,
                 [&] { return SetColours(self, colour, colour2); },
                 [&] { return g_setColours(self, nullptr, colour, colour2); });
}

uint32_t __fastcall DSetMode(void* self, void*, uint32_t mode)
{
    OPENSC4_FIRST("SetMode 0x00896654");
    if (g_inGame)
        return g_setMode(self, nullptr, mode);
    if (!g_compare[kProperties])
        return SetMode(self, mode);
    return Twice("fontobject.setmode", Kept{Whole(self)}, 0xFFFFFFFF, [&] { return SetMode(self, mode); },
                 [&] { return g_setMode(self, nullptr, mode); });
}

float FloatGet(const char* id, void* self, float (*mine)(void*), FloatGetFn theirs)
{
    if (g_inGame)
        return theirs(self, nullptr);
    if (!g_compare[kProperties])
        return mine(self);
    const float ours = mine(self);
    float result;
    {
        GameSide side;
        result = theirs(self, nullptr);
    }
    Report(id, Words({Bits(result)}), Words({Bits(ours)}));
    return result;
}

float __fastcall DGetPower(void* self, void*)
{
    OPENSC4_FIRST("GetPower 0x00896662");
    return FloatGet("fontobject.getpower", self, &GetPower, g_getPower);
}

float __fastcall DGetBias(void* self, void*)
{
    OPENSC4_FIRST("GetBias 0x00896666");
    return FloatGet("fontobject.getbias", self, &GetBias, g_getBias);
}

// The setters that rebuild: the game's, then the field it left compared
// with what ours would store.
// What a setter that stores only a value that differs leaves: the old one
// if equal (0 and -0 are), else the new.
float Stays(float old, float now) { return old == now ? old : now; }

uint32_t __fastcall DSetPower(void* self, void*, float power)
{
    OPENSC4_FIRST("SetPower 0x00899987");
    if (g_inGame)
        return g_setPower(self, nullptr, power);
    if (!g_compare[kProperties]) {
        SetPower(self, power);
        return 0;
    }
    const float ours = Stays(Load<float>(At(self) + font::kPower), ClampPower(power));
    uint32_t result;
    {
        GameSide side;
        result = g_setPower(self, nullptr, power);
    }
    Report("fontobject.setpower", Words({Bits(Load<float>(At(self) + font::kPower))}), Words({Bits(ours)}));
    return result;
}

uint32_t __fastcall DSetBias(void* self, void*, float bias)
{
    OPENSC4_FIRST("SetBias 0x008999CE");
    if (g_inGame)
        return g_setBias(self, nullptr, bias);
    if (!g_compare[kProperties]) {
        SetBias(self, bias);
        return 0;
    }
    const float ours = Stays(Load<float>(At(self) + font::kBias), ClampBias(bias));
    uint32_t result;
    {
        GameSide side;
        result = g_setBias(self, nullptr, bias);
    }
    Report("fontobject.setbias", Words({Bits(Load<float>(At(self) + font::kBias))}), Words({Bits(ours)}));
    return result;
}

uint32_t __fastcall DSetExtra(void* self, void*, float extra, uint32_t unused)
{
    OPENSC4_FIRST("SetExtra 0x0089865F");
    if (g_inGame)
        return g_setExtra(self, nullptr, extra, unused);
    if (!g_compare[kProperties]) {
        SetExtra(self, extra);
        return 0;
    }
    const float ours = Stays(Load<float>(At(self) + font::kExtra), ClampExtra(extra));
    uint32_t result;
    {
        GameSide side;
        result = g_setExtra(self, nullptr, extra, unused);
    }
    Report("fontobject.setextra", Words({Bits(Load<float>(At(self) + font::kExtra))}), Words({Bits(ours)}));
    return result;
}

uint32_t __fastcall DGetEncoding(void* self, void*, uint32_t* out)
{
    OPENSC4_FIRST("GetEncoding 0x0089666A");
    if (g_inGame)
        return g_getEncoding(self, nullptr, At(out));
    if (!g_compare[kProperties])
        return GetEncoding(self, out);
    return Twice("fontobject.getencoding", Kept{{out, 4}}, 0xFFFFFFFF, [&] { return GetEncoding(self, out); },
                 [&] { return g_getEncoding(self, nullptr, At(out)); });
}

uint32_t __fastcall DSetEncoding(void* self, void*, uint32_t encoding)
{
    OPENSC4_FIRST("SetEncoding 0x00896678");
    if (g_inGame)
        return g_setEncoding(self, nullptr, encoding);
    if (!g_compare[kProperties])
        return SetEncoding(self, encoding);
    return Twice("fontobject.setencoding", Kept{Whole(self)}, 0xFFFFFFFF,
                 [&] { return SetEncoding(self, encoding); },
                 [&] { return g_setEncoding(self, nullptr, encoding); });
}

uint32_t __fastcall DSupportsEncoding(void* self, void*, uint32_t encoding)
{
    OPENSC4_FIRST("SupportsEncoding 0x00896684");
    if (g_inGame)
        return g_supportsEncoding(self, nullptr, encoding);
    if (!g_compare[kProperties])
        return SupportsEncoding(encoding);
    return Pure("fontobject.supportsencoding", 0xFF, [&] { return SupportsEncoding(encoding); },
                [&] { return g_supportsEncoding(self, nullptr, encoding); });
}

uint32_t __fastcall DGetScale(void* self, void*, float* x, float* y)
{
    OPENSC4_FIRST("GetScale 0x00896692");
    if (g_inGame)
        return g_getScale(self, nullptr, At(x), At(y));
    if (!g_compare[kProperties]) {
        GetScale(self, x, y);
        return 0;
    }
    return Twice("fontobject.getscale", Kept{{x, 4}, {y, 4}}, 0, [&] { GetScale(self, x, y); return 0u; },
                 [&] { return g_getScale(self, nullptr, At(x), At(y)); });
}

uint32_t __fastcall DGetExtra(void* self, void*, float* extra, float* one)
{
    OPENSC4_FIRST("GetExtra 0x008966A7");
    if (g_inGame)
        return g_getExtra(self, nullptr, At(extra), At(one));
    if (!g_compare[kProperties]) {
        GetExtra(self, extra, one);
        return 0;
    }
    return Twice("fontobject.getextra", Kept{{extra, 4}, {one, 4}}, 0,
                 [&] { GetExtra(self, extra, one); return 0u; },
                 [&] { return g_getExtra(self, nullptr, At(extra), At(one)); });
}

uint32_t __fastcall DGetSpacing(void* self, void*, uint32_t* spacing, uint32_t* lineSpacing)
{
    OPENSC4_FIRST("GetSpacing 0x008966BB");
    if (g_inGame)
        return g_getSpacing(self, nullptr, At(spacing), At(lineSpacing));
    if (!g_compare[kProperties])
        return GetSpacing(self, spacing, lineSpacing);
    return Twice("fontobject.getspacing", Kept{{spacing, 4}, {lineSpacing, 4}}, 0xFFFFFFFF,
                 [&] { return GetSpacing(self, spacing, lineSpacing); },
                 [&] { return g_getSpacing(self, nullptr, At(spacing), At(lineSpacing)); });
}

uint32_t __fastcall DSetSpacing(void* self, void*, uint32_t spacing, uint32_t lineSpacing)
{
    OPENSC4_FIRST("SetSpacing 0x008966D0");
    if (g_inGame)
        return g_setSpacing(self, nullptr, spacing, lineSpacing);
    if (!g_compare[kProperties])
        return SetSpacing(self, spacing, lineSpacing);
    return Twice("fontobject.setspacing", Kept{Whole(self)}, 0xFFFFFFFF,
                 [&] { return SetSpacing(self, spacing, lineSpacing); },
                 [&] { return g_setSpacing(self, nullptr, spacing, lineSpacing); });
}

uint32_t __fastcall DLineHeight(void* self, void*)
{
    OPENSC4_FIRST("LineHeight 0x008966EB");
    if (g_inGame)
        return g_lineHeight(self, nullptr);
    if (!g_compare[kProperties])
        return LineHeight(self);
    return Pure("fontobject.lineheight", 0xFFFFFFFF, [&] { return LineHeight(self); },
                [&] { return g_lineHeight(self, nullptr); });
}

uint32_t __fastcall DTextHeight(void* self, void*, uint32_t text, uint32_t count)
{
    OPENSC4_FIRST("TextHeight 0x008966F2");
    if (g_inGame)
        return g_textHeight(self, nullptr, text, count);
    if (!g_compare[kProperties])
        return TextHeight(self);
    return Pure("fontobject.textheight", 0xFFFFFFFF, [&] { return TextHeight(self); },
                [&] { return g_textHeight(self, nullptr, text, count); });
}

uint32_t __fastcall DBaseline(void* self, void*)
{
    OPENSC4_FIRST("Baseline 0x008966F8");
    if (g_inGame)
        return g_baseline(self, nullptr);
    if (!g_compare[kProperties])
        return Baseline(self);
    return Pure("fontobject.baseline", 0xFFFFFFFF, [&] { return Baseline(self); },
                [&] { return g_baseline(self, nullptr); });
}

uint32_t __fastcall DBelowBaseline(void* self, void*)
{
    OPENSC4_FIRST("BelowBaseline 0x00896702");
    if (g_inGame)
        return g_belowBaseline(self, nullptr);
    if (!g_compare[kProperties])
        return BelowBaseline(self);
    return Pure("fontobject.belowbaseline", 0xFFFFFFFF, [&] { return BelowBaseline(self); },
                [&] { return g_belowBaseline(self, nullptr); });
}

uint32_t __fastcall DGlyphCache(void* self, void*)
{
    OPENSC4_FIRST("GlyphCache 0x008962C4");
    if (g_inGame)
        return g_glyphCache(self, nullptr);
    if (!g_compare[kProperties])
        return GlyphCache(self);
    return Pure("fontobject.glyphcache", 0xFFFFFFFF, [&] { return GlyphCache(self); },
                [&] { return g_glyphCache(self, nullptr); });
}

OPENSC4_THUNK(TGetFamily, DGetFamily, 4)
OPENSC4_THUNK(THasStyle, DHasStyle, 4)
OPENSC4_THUNK(TSetStyle, DSetStyle, 8)
OPENSC4_THUNK(TSetColours, DSetColours, 8)
OPENSC4_THUNK(TSetMode, DSetMode, 4)
OPENSC4_THUNK(TGetPower, DGetPower, 0)
OPENSC4_THUNK(TSetPower, DSetPower, 4)
OPENSC4_THUNK(TGetBias, DGetBias, 0)
OPENSC4_THUNK(TSetBias, DSetBias, 4)
OPENSC4_THUNK(TGetPath, DGetPath, 4)
OPENSC4_THUNK(TSetPath, DSetPath, 4)
OPENSC4_THUNK(TGetEncoding, DGetEncoding, 4)
OPENSC4_THUNK(TSetEncoding, DSetEncoding, 4)
OPENSC4_THUNK(TSupportsEncoding, DSupportsEncoding, 4)
OPENSC4_THUNK(TGetScale, DGetScale, 8)
OPENSC4_THUNK(TGetExtra, DGetExtra, 8)
OPENSC4_THUNK(TSetExtra, DSetExtra, 8)
OPENSC4_THUNK(TGetSpacing, DGetSpacing, 8)
OPENSC4_THUNK(TSetSpacing, DSetSpacing, 8)
OPENSC4_THUNK(TLineHeight, DLineHeight, 0)
OPENSC4_THUNK(TTextHeight, DTextHeight, 8)
OPENSC4_THUNK(TBaseline, DBaseline, 0)
OPENSC4_THUNK(TBelowBaseline, DBelowBaseline, 0)
OPENSC4_THUNK(TGlyphCache, DGlyphCache, 0)

// --- the blitter

CdeclOneFn g_blitMode = nullptr;
ThisOneFn g_setBlitter = nullptr;
ThisOneFn g_begin = nullptr;
ThisOneFn g_end = nullptr;

uint32_t __cdecl DBlitMode(uint32_t mode)
{
    OPENSC4_FIRST("BlitMode 0x00896B2C");
    if (g_inGame)
        return g_blitMode(mode);
    if (!g_compare[kBlitter])
        return BlitMode(mode);
    return Pure("fontobject.blitmode", 0xFFFFFFFF, [&] { return BlitMode(mode); }, [&] { return g_blitMode(mode); });
}

uint32_t __fastcall DSetBlitter(void* self, void*, uint32_t blitter)
{
    OPENSC4_FIRST("SetBlitter 0x00896B45");
    if (!Ours(kBlitter))
        return g_setBlitter(self, nullptr, blitter);
    SetBlitter(self, Ptr(blitter));
    return 0;
}

uint32_t __fastcall DBegin(void* self, void*, uint32_t picture)
{
    OPENSC4_FIRST("Begin 0x00896B8E");
    if (!Ours(kBlitter))
        return g_begin(self, nullptr, picture);
    return Begin(self, picture) ? 1 : 0;
}

uint32_t __fastcall DEnd(void* self, void*, uint32_t picture)
{
    OPENSC4_FIRST("End 0x00896BC2");
    if (!Ours(kBlitter))
        return g_end(self, nullptr, picture);
    return End(self, picture) ? 1 : 0;
}

OPENSC4_CDECL_THUNK(TBlitMode, DBlitMode, 4)
OPENSC4_THUNK(TSetBlitter, DSetBlitter, 4)
OPENSC4_THUNK(TBegin, DBegin, 4)
OPENSC4_THUNK(TEnd, DEnd, 4)

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

// A vtable slot pointed at ours, for a function too short for a detour:
// `original` is the game's function itself.
struct VtableSlot {
    Address vtable;
    uint32_t index;
    std::uintptr_t function;
    void (*thunk)();
    void** original;
};

bool PatchSlot(const VtableSlot& slot)
{
    auto* const entry = static_cast<std::uintptr_t*>(Ptr(slot.vtable)) + slot.index;
    if (*entry != slot.function) {
        log::Write("fontobject: vtable 0x%08X slot %u is 0x%08X, not 0x%08X: refused", static_cast<unsigned>(slot.vtable),
                   slot.index, static_cast<unsigned>(*entry), static_cast<unsigned>(slot.function));
        return false;
    }
    *slot.original = reinterpret_cast<void*>(slot.function);
    DWORD old = 0;
    if (!VirtualProtect(entry, sizeof *entry, PAGE_READWRITE, &old))
        return false;
    *entry = reinterpret_cast<std::uintptr_t>(slot.thunk);
    VirtualProtect(entry, sizeof *entry, old, &old);
    return true;
}

template <std::size_t N>
bool InstallAll(registry::Mode mode, Group group, const char* name, const Detour (&detours)[N],
                std::initializer_list<VtableSlot> slots = {})
{
    if (!game::IsBuild641())
        return false;
    g_compare[group] = mode == registry::Mode::Compare;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    for (const VtableSlot& slot : slots) {
        if (!PatchSlot(slot))
            return false;
    }
    log::Write("fontobject: %s: %zu functions %s", name, N + slots.size(),
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallObject(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFont_Construct, &TConstruct, Slot(&g_construct)},
        {addr::GZFont_Destruct, &TDestruct, Slot(&g_destruct)},
        {addr::GZFont_DeletingDestruct, &TDeletingDestruct, Slot(&g_deletingDestruct)},
        {addr::GZFont_QueryInterface, &TQueryInterface, Slot(&g_queryInterface)},
        {addr::GZFont_Release, &TRelease, Slot(&g_release)},
        {addr::GZFont_Destroy, &TDestroy, Slot(&g_destroy)},
        {addr::GZFont_Unload, &TUnload, Slot(&g_unload)},
        {addr::GZFont_CacheConstruct, &TCacheConstruct, Slot(&g_cacheConstruct)},
        {addr::GZFont_CacheClear, &TCacheClear, Slot(&g_cacheClear)},
        {addr::GZFont_CacheDestroy, &TCacheDestroy, Slot(&g_cacheDestroy)},
    };
    return InstallAll(mode, kObject, "object", detours);
}

bool InstallProperties(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFont_GetFamily, &TGetFamily, Slot(&g_getFamily)},
        {addr::GZFont_HasStyle, &THasStyle, Slot(&g_hasStyle)},
        {addr::GZFont_SetStyle, &TSetStyle, Slot(&g_setStyle)},
        {addr::GZFont_SetColours, &TSetColours, Slot(&g_setColours)},
        {addr::GZFont_SetMode, &TSetMode, Slot(&g_setMode)},
        {addr::GZFont_SetPower, &TSetPower, Slot(&g_setPower)},
        {addr::GZFont_SetBias, &TSetBias, Slot(&g_setBias)},
        {addr::GZFont_GetPath, &TGetPath, Slot(&g_getPath)},
        {addr::GZFont_SetPath, &TSetPath, Slot(&g_setPath)},
        {addr::GZFont_GetEncoding, &TGetEncoding, Slot(&g_getEncoding)},
        {addr::GZFont_SetEncoding, &TSetEncoding, Slot(&g_setEncoding)},
        {addr::GZFont_SupportsEncoding, &TSupportsEncoding, Slot(&g_supportsEncoding)},
        {addr::GZFont_GetScale, &TGetScale, Slot(&g_getScale)},
        {addr::GZFont_GetExtra, &TGetExtra, Slot(&g_getExtra)},
        {addr::GZFont_SetExtra, &TSetExtra, Slot(&g_setExtra)},
        {addr::GZFont_GetSpacing, &TGetSpacing, Slot(&g_getSpacing)},
        {addr::GZFont_SetSpacing, &TSetSpacing, Slot(&g_setSpacing)},
        {addr::GZFont_LineHeight, &TLineHeight, Slot(&g_lineHeight)},
        {addr::GZFont_TextHeight, &TTextHeight, Slot(&g_textHeight)},
        {addr::GZFont_Baseline, &TBaseline, Slot(&g_baseline)},
        {addr::GZFont_BelowBaseline, &TBelowBaseline, Slot(&g_belowBaseline)},
        {addr::GZFont_GlyphCache, &TGlyphCache, Slot(&g_glyphCache)},
    };
    return InstallAll(mode, kProperties, "properties", detours,
                      {{font::kVtable, 17, addr::GZFont_GetPower, &TGetPower, Slot(&g_getPower)},
                       {font::kVtable, 19, addr::GZFont_GetBias, &TGetBias, Slot(&g_getBias)}});
}

bool InstallBlitter(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFont_BlitMode, &TBlitMode, Slot(&g_blitMode)},
        {addr::GZFont_SetBlitter, &TSetBlitter, Slot(&g_setBlitter)},
        {addr::GZFont_Begin, &TBegin, Slot(&g_begin)},
        {addr::GZFont_End, &TEnd, Slot(&g_end)},
    };
    return InstallAll(mode, kBlitter, "blitter", detours);
}

const bool g_registered[] = {
    registry::Register({"framework.font.font.object", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFont_Construct,
                        "Our font's object: constructor, destructors, QueryInterface, Release, Unload and the glyph "
                        "caches' constructor, clear and destructor (10 detours; docs/fonts.md)",
                        true, InstallObject}),
    registry::Register({"framework.font.font.properties", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFont_SetStyle,
                        "Our font's properties: family, path, styles, colours, mode, the grey ramp, encoding, scales, "
                        "spacing, line metrics, glyph cache (22 detours and 2 vtable slots; docs/fonts.md)",
                        true, InstallProperties}),
    registry::Register({"framework.font.font.blitter", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFont_SetBlitter,
                        "Our font's blitter: the blitter's mode, SetBlitter, Begin, End (4 detours; docs/fonts.md)",
                        true, InstallBlitter}),
};

} // namespace
} // namespace opensc4::fontobject
