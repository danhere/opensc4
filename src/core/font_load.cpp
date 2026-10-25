// Our font's loading, size and scale, the third slice of f3 of docs/fonts.md
// ("f3c: the font's loading, size and scale are ours"): five more functions
// of class 0x1AA404AC (vtable 0x00AC55D8), detours in one group, off by
// default and installed at Attach (the first fonts are loaded while the game
// starts):
//
//   framework.font.font.load   Load (slot 3, 0x0089955C), SetSize (slot 11,
//                              0x00897AB1), the sample characters of a
//                              language (slot 25, 0x00898A5A), SetScale (slot
//                              30, 0x0089854E) and the grey ramp's levels
//                              (0x008993BB)
//
// The T2K holder (f5), T2K itself (its transformation 0x0089DC9A and glyph
// lookups 0x0089F4B2 and 0x0089F538), the glyph blitter's constructor (f4),
// the C runtime's pow and its float wrapper, the CRT's ftol and the
// vectors' code stay the game's, called at their addresses.
//
// The arithmetic the game does on the x87 (the transformation's two words
// from the scale and the size, the ramp's curve and its 256 levels) is the
// game's own instruction sequence in naked functions below, so the result
// is the FPU's at its precision, as the game's is.
//
// The font's fields are listed in font_object.cpp; these also use +0xE8,
// the glyph of the fallback character (+0xE4), and a glyph cache's count of
// glyphs (its +0x28: +0x12C and +0x158 of the font).
//
// Compare mode: the sample characters compare their answer; SetSize runs
// ours, keeps what it wrote aside with the font's 0x15C bytes, puts them
// back and runs the game's (both hand T2K the transformation; the game's
// goes last); SetScale runs the game's and compares the two scales it left
// with what ours would leave; the levels run the game's and compare the
// vector it left with the 256 bytes ours works out (or none). Load runs the
// game's, and what it calls compares itself. While the game's side of a
// comparison runs, our detours inside it pass through to the game's.
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

namespace opensc4::fontload {
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

float Float(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontload: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions and constants we use

constexpr std::uintptr_t kCacheClear = 0x008982BA;      // thiscall on a glyph cache ()
constexpr std::uintptr_t kVectorErase = 0x0040E490;     // thiscall (first, last): a byte vector's erase
constexpr std::uintptr_t kT2kTransform = 0x0089DC9A;    // cdecl (scaler, 1, 72, 72, matrix, 1, error*)
constexpr std::uintptr_t kT2kGlyphIndex = 0x0089F4B2;   // cdecl (scaler, character, error*): the glyph
constexpr std::uintptr_t kT2kGlyphExists = 0x0089F538;  // cdecl (scaler, character, flags, error*)

constexpr Address kOne = 0x00A81228;        // 1.0: the ramp's power when off
constexpr Address kZero = 0x00A81054;       // 0.0: its bias when off
constexpr Address kScaleLow = 0x00A823A4;   // 0.01
constexpr Address kScaleHigh = 0x00A8FAF0;  // 1000.0

// ====================================================================================
// Layout

namespace font {
constexpr uint32_t kSize = 0x15C;
constexpr Address kLoaded = 0x04;
constexpr Address kPath = 0x0C;
constexpr Address kFamily = 0x20;
constexpr Address kKind = 0x34;
constexpr Address kPixels = 0x38;
constexpr Address kHeight = 0x3C;
constexpr Address kStyles = 0x40;
constexpr Address kPower = 0x54;
constexpr Address kBias = 0x58;
constexpr Address kLevels = 0x5C;
constexpr Address kScaleX = 0x68;
constexpr Address kScaleY = 0x6C;
constexpr Address kBelow = 0x84;
constexpr Address kAbove = 0x88;
constexpr Address kBlitter = 0x8C;
constexpr Address kFallback = 0xE4;
constexpr Address kFallbackGlyph = 0xE8;
constexpr Address kHolder = 0xEC;
constexpr Address kMonoCache = 0x104;
constexpr Address kGreyCache = 0x130;
constexpr Address kMonoCount = 0x12C;
constexpr Address kGreyCount = 0x158;
// Its own slot it calls: SetSize.
constexpr std::size_t kSetSize = 0x2C;
} // namespace font

// A game string (+4 the text's start, +8 its end) and the T2K holder (+8
// T2K's scaler, +0x24 the kind of face).
constexpr Address kStringBegin = 4, kStringEnd = 8;
constexpr Address kHolderScaler = 8, kHolderKind = 0x24;
// T2K's scaler: +0x5C set for faces with outlines of their own kind (the
// font's kind 2), +0x6C, +0x74 and +0x7C the metrics in 16.16 pixels.
constexpr Address kScalerKind = 0x5C, kScalerLineGap = 0x6C, kScalerDescent = 0x74, kScalerAscent = 0x7C;

constexpr uint32_t kStyleBits = 0x3C;
constexpr uint32_t kMaxPixels = 0x800;
constexpr uint32_t kBlitterSize = 8;
constexpr uint32_t kLevelCount = 0x100;
constexpr std::size_t kBlitAddRef = 0x04;

// ====================================================================================
// The game's x87 arithmetic

// The transformation's two words (16.16) as SetSize and SetScale work them
// out: the size as the x87 loads it (2^32 more when the dword is negative),
// times each scale times 65536.0 (0x00AB3CAC), truncated by the CRT's ftol.
__declspec(naked) void __cdecl ScaleWords(const void* /*font*/, uint32_t /*pixels*/, int32_t* /*out*/)
{
    __asm {
        push esi
        mov esi, dword ptr [esp + 8]
        fild dword ptr [esp + 12]
        mov eax, dword ptr [esp + 12]
        test eax, eax
        jge size_ok
        mov eax, 0x00A80AA8
        fadd dword ptr [eax]
    size_ok:
        fld dword ptr [esi + 0x68]
        mov eax, 0x00AB3CAC
        fmul dword ptr [eax]
        fmul st, st(1)
        mov eax, 0x009EEF04
        call eax
        mov ecx, dword ptr [esp + 16]
        mov dword ptr [ecx], eax
        fld dword ptr [esi + 0x6C]
        mov eax, 0x00AB3CAC
        fmul dword ptr [eax]
        fmul st, st(1)
        mov eax, 0x009EEF04
        call eax
        fstp st(0)
        mov ecx, dword ptr [esp + 16]
        mov dword ptr [ecx + 4], eax
        pop esi
        ret
    }
}

// The ramp's curve as 0x008993BB works it out: the C runtime's 0x009EFC20
// (pow) of the bias plus 1.0 and 1.0 over its 0x009F2160 of 2.0, stored as
// a float.
__declspec(naked) void __cdecl RampCurve(const void* /*font*/, float* /*out*/)
{
    __asm {
        push esi
        mov esi, dword ptr [esp + 8]
        mov eax, 0x00A80AC8
        fld qword ptr [eax]
        push ecx
        push ecx
        fstp qword ptr [esp]
        mov eax, 0x009F2160
        call eax
        mov eax, 0x00A80AB0
        fdivr qword ptr [eax]
        fstp qword ptr [esp]
        fld dword ptr [esi + 0x58]
        mov eax, 0x00A80AB0
        fadd qword ptr [eax]
        push ecx
        push ecx
        fstp qword ptr [esp]
        mov eax, 0x009EFC20
        call eax
        mov ecx, dword ptr [esp + 0x1C]
        fstp dword ptr [ecx]
        add esp, 0x10
        pop esi
        ret
    }
}

// One level as 0x008993BB works it out (the game's instructions in its
// order): i / 255 (0x00A90BBC); unless the power is 1.0, 0x004C37A0 (pow of
// two floats) of it and the power; unless the bias is 0.0, kept at most 1.0
// and bent by the curve (below 128 pow(2t, curve) / 2, from 128 1 less
// pow(2 - 2t, curve) / 2); times 255.0, truncated by the CRT's ftol.
// Answers what ftol leaves in EAX (the level is AL).
__declspec(naked) uint32_t __cdecl LevelOf(const void* /*font*/, int32_t /*i*/, const float* /*curve*/)
{
    __asm {
        push ebx
        push esi
        mov esi, dword ptr [esp + 12]
        mov ebx, dword ptr [esp + 16]
        fild dword ptr [esp + 16]
        mov eax, 0x00A90BBC
        fmul dword ptr [eax]
        mov eax, 0x00A81228
        fld dword ptr [eax]
        fld dword ptr [esi + 0x54]
        fucompp
        fnstsw ax
        test ah, 0x44
        jnp power_done
        fld dword ptr [esi + 0x54]
        push ecx
        push ecx
        fstp dword ptr [esp + 4]
        fstp dword ptr [esp]
        mov eax, 0x004C37A0
        call eax
        pop ecx
        pop ecx
    power_done:
        mov eax, 0x00A81054
        fld dword ptr [eax]
        fld dword ptr [esi + 0x58]
        fucompp
        fnstsw ax
        test ah, 0x44
        jnp bias_done
        mov eax, 0x00A81228
        fcom dword ptr [eax]
        fnstsw ax
        test ah, 0x41
        jnz below_one
        fstp st(0)
        fld1
    below_one:
        mov edx, dword ptr [esp + 20]
        cmp ebx, 0x7F
        fld dword ptr [edx]
        push ecx
        push ecx
        fstp dword ptr [esp + 4]
        jg upper_half
        fadd st, st(0)
        fstp dword ptr [esp]
        mov eax, 0x004C37A0
        call eax
        mov eax, 0x00A84D2C
        fmul dword ptr [eax]
        jmp bent
    upper_half:
        fld1
        fsub st, st(1)
        fadd st, st(0)
        fstp dword ptr [esp]
        fstp st(0)
        mov eax, 0x004C37A0
        call eax
        mov eax, 0x00A84D2C
        fmul dword ptr [eax]
        mov eax, 0x00A81228
        fsubr dword ptr [eax]
    bent:
        pop ecx
        pop ecx
    bias_done:
        mov eax, 0x00A9D8B0
        fmul dword ptr [eax]
        mov eax, 0x009EEF04
        call eax
        pop esi
        pop ebx
        ret
    }
}

// ====================================================================================
// The functions

// Whether the ramp is off (power 1.0 and bias 0.0, unordered counting as
// not), when the levels are emptied.
bool RampOff(Address at) { return Load<float>(at + font::kPower) == Load<float>(kOne) && Load<float>(at + font::kBias) == Load<float>(kZero); }

// The 256 levels ours works out for the font's power and bias.
void LevelsOf(void* self, uint8_t (&levels)[kLevelCount])
{
    float curve;
    RampCurve(self, &curve);
    for (int32_t i = 0; i < static_cast<int32_t>(kLevelCount); ++i)
        levels[i] = static_cast<uint8_t>(LevelOf(self, i, &curve));
}

// The game's 0x008993BB (thiscall): with the ramp off the levels emptied
// (the vector's erase from its start to its end) and the grey glyph cache
// cleared; else the vector made 256 zeros, the grey cache cleared and every
// level worked out.
void RebuildLevels(void* self)
{
    const Address at = At(self);
    if (RampOff(at)) {
        This<uint32_t>(kVectorErase, Ptr(at + font::kLevels), Load<Address>(at + font::kLevels),
                       Load<Address>(at + font::kLevels + 4));
        This<uint32_t>(kCacheClear, Ptr(at + font::kGreyCache));
        return;
    }
    const uint8_t zero = 0;
    This<void>(addr::SC4ByteVector_Resize2, Ptr(at + font::kLevels), kLevelCount, &zero);
    This<uint32_t>(kCacheClear, Ptr(at + font::kGreyCache));
    float curve;
    RampCurve(self, &curve);
    for (int32_t i = 0; i < static_cast<int32_t>(kLevelCount); ++i) {
        const uint8_t level = static_cast<uint8_t>(LevelOf(self, i, &curve));
        Store<uint8_t>(Load<Address>(at + font::kLevels) + static_cast<Address>(i), level);
    }
}

// T2K's transformation of the font's scale at `pixels`, at 72 dpi; what
// T2K reports goes into `error`.
void Transform(void* self, Address scaler, uint32_t pixels, uint32_t* error)
{
    int32_t words[2];
    ScaleWords(self, pixels, words);
    int32_t matrix[4] = {words[0], 0, 0, words[1]};
    Cdecl<void>(kT2kTransform, scaler, uint32_t{1}, uint32_t{0x48}, uint32_t{0x48}, matrix, uint32_t{1}, error);
}

// The game's 0x00897AB1 (slot 11, ret 4): refused (false) for 0 pixels or
// while either glyph cache holds glyphs. The size kept at most 0x800 is
// stored; with a face open, T2K's transformation set for it and the
// metrics read back (signed, truncated): above the baseline +0x88, that and
// the line gap +0x84, the glyphs' height +0x3C (1 when 0; one more, and
// another above 28, for holders of kind 2 and 3). True when T2K reported
// nothing, or without a face.
bool SetSize(void* self, uint32_t pixels)
{
    const Address at = At(self);
    if (pixels == 0 || Load<uint32_t>(at + font::kMonoCount) != 0 || Load<uint32_t>(at + font::kGreyCount) != 0)
        return false;
    if (pixels > kMaxPixels)
        pixels = kMaxPixels;
    const Address holder = Load<Address>(at + font::kHolder);
    Store<uint32_t>(at + font::kPixels, pixels);
    if (!holder || !Load<Address>(holder + kHolderScaler))
        return true;
    uint32_t error = 0;
    Transform(self, Load<Address>(holder + kHolderScaler), pixels, &error);
    const auto scaler = [&] { return Load<Address>(holder + kHolderScaler); };
    Store<int32_t>(at + font::kAbove, Load<int32_t>(scaler() + kScalerAscent) / 0x10000);
    Store<int32_t>(at + font::kBelow,
                   static_cast<int32_t>(Load<uint32_t>(scaler() + kScalerAscent) + Load<uint32_t>(scaler() + kScalerLineGap)) /
                       0x10000);
    const Address s = scaler();
    const int32_t height = static_cast<int32_t>(Load<uint32_t>(s + kScalerAscent) - Load<uint32_t>(s + kScalerDescent) +
                                                Load<uint32_t>(s + kScalerLineGap)) /
                           0x10000;
    Store<int32_t>(at + font::kHeight, height);
    if (height == 0)
        Store<uint32_t>(at + font::kHeight, 1);
    const uint32_t kind = Load<uint32_t>(holder + kHolderKind);
    if (kind == 2 || kind == 3) {
        const uint32_t more = Load<uint32_t>(at + font::kHeight) + 1;
        Store<uint32_t>(at + font::kHeight, more);
        if (more > 0x1C)
            Store<uint32_t>(at + font::kHeight, more + 1);
    }
    return error == 0;
}

// What SetScale stores of one scale: below 0.01 that, above 1000 that, the
// bits as they came otherwise (a NaN too).
uint32_t ClampScale(uint32_t bits)
{
    if (Float(bits) < Load<float>(kScaleLow))
        bits = Load<uint32_t>(kScaleLow);
    if (Float(bits) > Load<float>(kScaleHigh))
        bits = Load<uint32_t>(kScaleHigh);
    return bits;
}

// Whether SetScale stores (either scale differs from its field, unordered
// counting as differing).
bool ScaleDiffers(Address at, uint32_t x, uint32_t y)
{
    return !(Float(x) == Load<float>(at + font::kScaleX)) || !(Float(y) == Load<float>(at + font::kScaleY));
}

// The game's 0x0089854E (slot 30, ret 8): both scales kept in 0.01 to 1000
// and, if either differs, both stored, both glyph caches cleared and T2K's
// transformation set for them at the size (no check for a face; what T2K
// reports is not read).
void SetScale(void* self, uint32_t xBits, uint32_t yBits)
{
    const Address at = At(self);
    const uint32_t x = ClampScale(xBits);
    const uint32_t y = ClampScale(yBits);
    if (!ScaleDiffers(at, x, y))
        return;
    Store<uint32_t>(at + font::kScaleX, x);
    Store<uint32_t>(at + font::kScaleY, y);
    This<uint32_t>(kCacheClear, Ptr(at + font::kMonoCache));
    This<uint32_t>(kCacheClear, Ptr(at + font::kGreyCache));
    uint32_t error = 0;
    Transform(self, Load<Address>(Load<Address>(at + font::kHolder) + kHolderScaler), Load<uint32_t>(at + font::kPixels),
              &error);
}

// The game's 0x0089955C (slot 3): unless loaded already or without a path,
// and when the path's file is there and the holder opens it: the size set
// (slot 11, when there is one; refused, nothing more), the family named,
// the face's style bits over the font's own (bits 2 to 5 kept), the kind
// (2 when T2K's scaler has +0x5C, else 1), the fallback character's glyph
// (0 when T2K reports an error), loaded when the family has text. Once
// loaded, a glyph blitter made and AddRef'd if there is none, and the
// levels rebuilt. Answers the loaded byte.
uint8_t LoadFont(void* self)
{
    const Address at = At(self);
    if (Load<uint8_t>(at + font::kLoaded) != 0 ||
        Load<Address>(at + font::kPath + kStringBegin) == Load<Address>(at + font::kPath + kStringEnd))
        return Load<uint8_t>(at + font::kLoaded);
    if (!(Cdecl<uint32_t>(addr::RZFile_Exists, Ptr(at + font::kPath)) & 0xFF))
        return Load<uint8_t>(at + font::kLoaded);
    const Address holder = Load<Address>(at + font::kHolder);
    if (This<uint32_t>(addr::GZFontFile_Open, Ptr(holder), Ptr(at + font::kPath)) & 0xFF) {
        uint32_t error = 0;
        const uint32_t pixels = Load<uint32_t>(at + font::kPixels);
        if (pixels == 0 || (Virtual<uint32_t>(self, font::kSetSize, pixels) & 0xFF)) {
            This<uint32_t>(addr::GZFontFile_GetName, Ptr(holder), Ptr(at + font::kFamily), uint32_t{1});
            const uint32_t kept = Load<uint32_t>(at + font::kStyles) & kStyleBits;
            Store<uint32_t>(at + font::kStyles, This<uint32_t>(addr::GZFontFile_Styles, Ptr(holder)) | kept);
            Store<uint32_t>(at + font::kKind, Load<uint32_t>(Load<Address>(holder + kHolderScaler) + kScalerKind) != 0 ? 2 : 1);
            const uint32_t glyph = Cdecl<uint32_t>(kT2kGlyphIndex, Load<Address>(holder + kHolderScaler),
                                                   uint32_t{Load<uint16_t>(at + font::kFallback)}, &error);
            Store<uint32_t>(at + font::kFallbackGlyph, glyph & 0xFFFF);
            if (error != 0)
                Store<uint32_t>(at + font::kFallbackGlyph, 0);
            Store<uint8_t>(at + font::kLoaded,
                           Load<Address>(at + font::kFamily + kStringBegin) != Load<Address>(at + font::kFamily + kStringEnd)
                               ? 1
                               : 0);
        }
    }
    if (Load<uint8_t>(at + font::kLoaded) != 0) {
        if (Load<Address>(at + font::kBlitter) == 0) {
            void* const space = Cdecl<void*>(addr::SC4_operator_new, kBlitterSize);
            void* const blitter = space ? This<void*>(addr::GZFontBlitter_Construct, space) : nullptr;
            Store<Address>(at + font::kBlitter, At(blitter));
            Virtual<uint32_t>(blitter, kBlitAddRef);
        }
        This<void>(addr::GZFont_RebuildLevels, self);
    }
    return Load<uint8_t>(at + font::kLoaded);
}

// The characters 0x00898A5A asks T2K for, by language; false for a
// language whose characters are all asked (none for one it does not know).
struct Samples {
    uint32_t count;
    uint32_t characters[2];
};

Samples SamplesOf(int32_t language)
{
    switch (language) {
    case 0:
    case 0x4E4:
        return {1, {0x61}};
    case 0x36A:
        return {2, {0xE01, 0xE2C}};
    case 0x3A4:
        return {2, {0x3061, 0x30A4}};
    case 0x3A8:
        return {2, {0x70A7, 0x7535}};
    case 0x3B5:
        return {2, {0xC2B9, 0xC9D1}};
    case 0x3B6:
        return {2, {0x6F54, 0x70B5}};
    case 0x4E2:
        return {1, {0x142}};
    case 0x4E3:
        return {1, {0x401}};
    case 0x4E5:
        return {2, {0x370, 0x393}};
    case 0x4E6:
        return {1, {0x11E}};
    case 0x4E7:
        return {2, {0x5D0, 0x5E2}};
    case 0x4E8:
        return {2, {0x621, 0x641}};
    case 0x4E9:
        return {1, {0x160}};
    default:
        return {0, {}};
    }
}

// The game's 0x00898A5A (slot 25, ret 4): true for languages 8 and 0x10
// without asking; else whether T2K has a glyph for each sample character
// of the language (0x0089F538 with flags 0), stopping at the first it has
// not. The game gathers the characters in a vector of its allocator; ours
// keeps them in an array.
bool SupportsLanguage(void* self, uint32_t language)
{
    if (language == 8 || language == 0x10)
        return true;
    const Samples samples = SamplesOf(static_cast<int32_t>(language));
    const Address at = At(self);
    for (uint32_t i = 0; i < samples.count; ++i) {
        uint32_t error = 0;
        const Address scaler = Load<Address>(Load<Address>(at + font::kHolder) + kHolderScaler);
        if (Cdecl<uint32_t>(kT2kGlyphExists, scaler, samples.characters[i], uint32_t{0}, &error) == 0)
            return false;
    }
    return true;
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

bool Ours() { return !g_inGame && !g_compare; }

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

using ThisOnlyFn = uint32_t(__fastcall*)(void*, void*);
using ThisOneFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
using FloatTwoFn = uint32_t(__fastcall*)(void*, void*, float, float);

ThisOnlyFn g_load = nullptr;
ThisOneFn g_setSize = nullptr;
ThisOneFn g_supportsLanguage = nullptr;
FloatTwoFn g_setScale = nullptr;
ThisOnlyFn g_rebuildLevels = nullptr;

uint32_t __fastcall DLoad(void* self, void*)
{
    OPENSC4_FIRST("Load 0x0089955C");
    if (!Ours())
        return g_load(self, nullptr);
    return LoadFont(self);
}

uint32_t __fastcall DSetSize(void* self, void*, uint32_t pixels)
{
    OPENSC4_FIRST("SetSize 0x00897AB1");
    if (g_inGame)
        return g_setSize(self, nullptr, pixels);
    if (!g_compare)
        return SetSize(self, pixels) ? 1 : 0;
    // Ours, what it wrote kept aside, the font put back; then the game's.
    std::vector<uint8_t> before(static_cast<uint8_t*>(self), static_cast<uint8_t*>(self) + font::kSize);
    const uint32_t ours = SetSize(self, pixels) ? 1 : 0;
    const std::string oursText = Words({ours}) + Hex(static_cast<uint8_t*>(self), font::kSize);
    std::memcpy(self, before.data(), before.size());
    uint32_t result;
    {
        GameSide side;
        result = g_setSize(self, nullptr, pixels);
    }
    Report("fontload.setsize", Words({result & 0xFF}) + Hex(static_cast<uint8_t*>(self), font::kSize), oursText);
    return result;
}

uint32_t __fastcall DSupportsLanguage(void* self, void*, uint32_t language)
{
    OPENSC4_FIRST("SupportsLanguage 0x00898A5A");
    if (g_inGame)
        return g_supportsLanguage(self, nullptr, language);
    if (!g_compare)
        return SupportsLanguage(self, language) ? 1 : 0;
    const uint32_t ours = SupportsLanguage(self, language) ? 1 : 0;
    uint32_t result;
    {
        GameSide side;
        result = g_supportsLanguage(self, nullptr, language);
    }
    Report("fontload.supportslanguage", Words({result & 0xFF}), Words({ours}));
    return result;
}

uint32_t __fastcall DSetScale(void* self, void*, float x, float y)
{
    OPENSC4_FIRST("SetScale 0x0089854E");
    if (g_inGame)
        return g_setScale(self, nullptr, x, y);
    uint32_t xBits, yBits;
    std::memcpy(&xBits, &x, 4);
    std::memcpy(&yBits, &y, 4);
    if (!g_compare) {
        SetScale(self, xBits, yBits);
        return 0;
    }
    // What ours would leave: the clamped scales if either differs, else
    // the fields as they are.
    const Address at = At(self);
    const uint32_t cx = ClampScale(xBits), cy = ClampScale(yBits);
    const bool stores = ScaleDiffers(at, cx, cy);
    const std::string oursText = stores ? Words({cx, cy})
                                        : Words({Load<uint32_t>(at + font::kScaleX), Load<uint32_t>(at + font::kScaleY)});
    uint32_t result;
    {
        GameSide side;
        result = g_setScale(self, nullptr, x, y);
    }
    Report("fontload.setscale", Words({Load<uint32_t>(at + font::kScaleX), Load<uint32_t>(at + font::kScaleY)}), oursText);
    return result;
}

uint32_t __fastcall DRebuildLevels(void* self, void*)
{
    OPENSC4_FIRST("RebuildLevels 0x008993BB");
    if (g_inGame)
        return g_rebuildLevels(self, nullptr);
    if (!g_compare) {
        RebuildLevels(self);
        return 0;
    }
    uint32_t result;
    {
        GameSide side;
        result = g_rebuildLevels(self, nullptr);
    }
    // The game's vector against what ours works out from the same power
    // and bias.
    const Address at = At(self);
    std::string ours;
    if (!RampOff(at)) {
        uint8_t levels[kLevelCount];
        LevelsOf(self, levels);
        ours = Hex(levels, kLevelCount);
    }
    const Address begin = Load<Address>(at + font::kLevels);
    const Address end = Load<Address>(at + font::kLevels + 4);
    Report("fontload.levels", begin ? Hex(static_cast<const uint8_t*>(Ptr(begin)), end - begin) : std::string(), ours);
    return result;
}

OPENSC4_THUNK(TLoad, DLoad, 0)
OPENSC4_THUNK(TSetSize, DSetSize, 4)
OPENSC4_THUNK(TSupportsLanguage, DSupportsLanguage, 4)
OPENSC4_THUNK(TSetScale, DSetScale, 8)
OPENSC4_THUNK(TRebuildLevels, DRebuildLevels, 0)

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

bool InstallLoad(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    const Detour detours[] = {
        {addr::GZFont_Load, &TLoad, Slot(&g_load)},
        {addr::GZFont_SetSize, &TSetSize, Slot(&g_setSize)},
        {addr::GZFont_SupportsLanguage, &TSupportsLanguage, Slot(&g_supportsLanguage)},
        {addr::GZFont_SetScale, &TSetScale, Slot(&g_setScale)},
        {addr::GZFont_RebuildLevels, &TRebuildLevels, Slot(&g_rebuildLevels)},
    };
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontload: load: %zu functions %s", std::size(detours),
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registered[] = {
    registry::Register({"framework.font.font.load", "framework/font", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZFont_Load,
                        "Our font's loading, size and scale: Load, SetSize, the sample characters of a language, "
                        "SetScale and the grey ramp's levels (5 detours; docs/fonts.md)",
                        true, InstallLoad}),
};

} // namespace
} // namespace opensc4::fontload
