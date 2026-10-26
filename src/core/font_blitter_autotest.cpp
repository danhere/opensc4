// The glyph blitter's part of the autotest (docs/fonts.md, "f4: the glyph
// blitter is ours"): OPENSC4_TEST_FONTS=<day> calls the 11 functions of
// the blitter by address (the game's with framework.font.blitter.* off,
// ours with them on), and draws fixed strings through real fonts' DrawString
// (slot 51), which ends in the blitter's Begin, DrawGlyphs and End.
//
// The pictures are mocks of our own (cIGZBuffer's slots that the blitter
// calls) whose pixels sit in canvases with a margin of known bytes all round,
// hashed whole after every draw, so a stray write counts too; every call a
// mock gets goes into the hash with its arguments. Pictures of every bit
// depth and colour type the blitter knows (and one it does not), tight,
// padded, odd and zero strides; glyph boxes off every edge, clips that cut
// them, coverage with negative pitch, characters below a space, every draw
// mode; real fonts at several sizes and styles in modes 1, 2 and 3. Every
// result goes into the fingerprint's `ft` part, one hash per stage logged as
// `autotest: fonts: <stage> <hash>`; addresses never go in.
//
// Then the T2K holder, its stream and the fonts' director (f5 and f1,
// docs/fonts.md, "f1 and f5 are ours"), by address: every face of the
// game's Fonts folder, four of Windows's and two OpenType ones, and files of
// our own (a TrueType face scrambled behind an MXFN header, headers too
// short and just long enough, junk, a file that is not there) opened,
// read through the stream at offsets around its ends and whole, named, their
// style bits and the glyphs of characters by name for faces without a cmap;
// the static table of glyph names; the three factories, the director made
// in a space of our own, its ID, its start on a mock director, the getter
// and the byte.
//
// Then the font system (f2a, docs/fonts.md, "f2a: the font system is
// ours"), by address, on systems of our own: the object (QueryInterface for
// eight IDs, the references, the version, the encoding, a mock blitter,
// Init, the families, Shutdown, the destructors in place); the faces (the
// type of every file and of files of our own, the files and folders added,
// ListFaces, HasFace, fonts added and released, faces removed, ListFonts);
// the aliases (added, listed into strings, resolved); GetFont over a matrix
// of face lists, kinds, sizes, styles and reuse, slot 9, slot 10 on every
// face, the sizes in mode 1.
//
// Then the font's object, properties and blitter (f3a, docs/fonts.md,
// "f3a: the font's object and properties are ours"), by address on fonts
// of our own and fonts the font system loaded: the constructor and the
// destructor between margins, QueryInterface, the count down to the last
// Release, Unload of fonts loaded (after measuring fills their glyph
// caches) and not, the glyph cache's three functions; every property's
// getter and setter (styles, colours, mode, the grey ramp's power and bias
// with the levels they build, path, family, encoding, scales, spacing,
// line metrics) over values at and past their limits, NaN and -0 among
// them; the blitter's mode, SetBlitter with mock blitters, Begin and End
// nested and past 0.
//
// Then the font's containers (f3b, docs/fonts.md, "f3b: the font's
// containers are ours"), by address on buffers between margins and
// containers of our own: the placed glyphs' copies, fills and vector
// (resize, fill insert, erase, overflow), the UTF-16 text's vector and
// find, both maps of characters, a glyph cache's table, a list of words, a
// vector of pointers out of room and a holder taken down.
//
// Then the font's loading, size and scale (f3c, docs/fonts.md, "f3c: the
// font's loading, size and scale are ours"), by address on fonts of our own
// given the paths of faces: Load, the sample characters of 21 languages,
// sizes and scales past their limits, the levels over powers and biases.
//
// Then the font's encodings and Thai (f3d, docs/fonts.md, "f3d: the font's
// encodings and Thai are ours"), by address on fonts of our own in six
// encodings: ToUnicode through each vector, Convert, the Thai pass, HasThai,
// the marks' kinds and forms, over Thai text that reaches every branch.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <windows.h>

namespace opensc4::autotest {
namespace {

bool g_ftOn = false;
uint32_t g_ftHash = 2166136261u;

void MixByte(uint8_t value) { g_ftHash = (g_ftHash ^ value) * 16777619u; }

void Mix(uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        MixByte(static_cast<uint8_t>(value >> (8 * i)));
}

void MixBytes(const uint8_t* bytes, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        MixByte(bytes[i]);
}

void FtStage(const char* name) { log::Write("autotest: fonts: %s %08X", name, g_ftHash); }

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state ^ (state >> 15);
    }
    uint8_t Byte() { return static_cast<uint8_t>(Next() >> 8); }
    int32_t Range(int32_t low, int32_t high)
    {
        return low + static_cast<int32_t>(Next() % static_cast<uint32_t>(high - low + 1));
    }
};

struct Rect {
    int32_t left, top, right, bottom;
};

struct PlacedGlyph {
    Rect box;
    const uint8_t* pixels;
    int32_t pitch;
    int32_t width;
    int32_t height;
    uint32_t character;
};
static_assert(sizeof(PlacedGlyph) == 0x24);

template <typename Fn>
Fn Game(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

// ====================================================================================
// The mock picture

constexpr int kSlots = 40;

struct Picture {
    void** vtable;
    uint32_t id;
    int32_t width = 37, height = 23;
    uint32_t bpp = 16;
    uint32_t type = 5;     // what GetColorType fills in
    uint32_t typeBits = 16;
    uint32_t stride = 74;
    uint8_t* bits = nullptr;
    uint32_t lockAnswer = 1;
};

void Rec(Picture* p, uint32_t slot)
{
    Mix(p->id);
    Mix(slot);
}

template <int Slot>
uint32_t __fastcall Trap(Picture* p, void*)
{
    Rec(p, 0xEE00 + Slot);
    log::Write("autotest: fonts: unexpected picture slot %d", Slot);
    return 0;
}

uint32_t __fastcall PicQuery(Picture* p, void*, uint32_t iid, void** out)
{
    Rec(p, 0);
    Mix(iid);
    log::Write("autotest: fonts: unexpected picture QueryInterface %08X", iid);
    *out = nullptr;
    return 0;
}

uint32_t __fastcall PicLock(Picture* p, void*, uint32_t flags)
{
    Rec(p, 6);
    Mix(flags);
    return p->lockAnswer;
}

uint32_t __fastcall PicUnlock(Picture* p, void*, uint32_t flags)
{
    Rec(p, 7);
    Mix(flags);
    return p->lockAnswer ^ 0x5A5A5A00;
}

int32_t __fastcall PicWidth(Picture* p, void*)
{
    Rec(p, 9);
    return p->width;
}

int32_t __fastcall PicHeight(Picture* p, void*)
{
    Rec(p, 10);
    return p->height;
}

void __fastcall PicColorType(Picture* p, void*, uint32_t* type)
{
    Rec(p, 14);
    Mix(type[0]);
    Mix(type[1]);
    type[0] = p->type;
    type[1] = p->typeBits;
}

uint32_t __fastcall PicBpp(Picture* p, void*)
{
    Rec(p, 16);
    return p->bpp;
}

uint8_t* __fastcall PicBits(Picture* p, void*)
{
    Rec(p, 34);
    return p->bits;
}

uint32_t __fastcall PicStride(Picture* p, void*)
{
    Rec(p, 35);
    return p->stride;
}

template <std::size_t... I>
void FillTraps(void** table, std::index_sequence<I...>)
{
    ((table[I] = reinterpret_cast<void*>(&Trap<static_cast<int>(I)>)), ...);
}

template <typename Fn>
void* V(Fn fn)
{
    return reinterpret_cast<void*>(fn);
}

void** PictureTable()
{
    static void* table[kSlots];
    static bool made = false;
    if (!made) {
        made = true;
        FillTraps(table, std::make_index_sequence<kSlots>{});
        table[0] = V(&PicQuery);
        table[6] = V(&PicLock);
        table[7] = V(&PicUnlock);
        table[9] = V(&PicWidth);
        table[10] = V(&PicHeight);
        table[14] = V(&PicColorType);
        table[16] = V(&PicBpp);
        table[34] = V(&PicBits);
        table[35] = V(&PicStride);
    }
    return table;
}

// A picture's pixels: `height` rows of `stride`, then as much as a row of
// `width` four-byte pixels takes (24-bit pictures are drawn as 32-bit), with
// a margin of 64 bytes of 0xA5 before and after.
struct Canvas {
    std::vector<uint8_t> bytes;
    Picture picture;
    Canvas(uint32_t id, int32_t width, int32_t height, uint32_t bpp, uint32_t type, uint32_t stride, Random& random)
    {
        picture.vtable = PictureTable();
        picture.id = id;
        picture.width = width;
        picture.height = height;
        picture.bpp = bpp;
        picture.type = type;
        picture.typeBits = bpp;
        picture.stride = stride;
        bytes.assign(64 + static_cast<std::size_t>(height) * stride + static_cast<std::size_t>(width) * 4 + 64, 0xA5);
        for (std::size_t i = 64; i + 64 < bytes.size(); ++i)
            bytes[i] = random.Byte();
        picture.bits = bytes.data() + 64;
    }
    void Hash() const { MixBytes(bytes.data(), bytes.size()); }
};

// ====================================================================================
// Blend tables and blends

using BuildFn = bool(__cdecl*)();

void DriveTables()
{
    auto* const table = reinterpret_cast<uint8_t*>(addr::GZFontBlitter_BlendTable);
    auto* const built = reinterpret_cast<uint8_t*>(addr::GZFontBlitter_TablesBuilt);
    const std::vector<uint8_t> saved(table, table + 0x8001);
    std::memset(table, 0xA5, 0x8000);
    MixByte(Game<BuildFn>(addr::GZFontBlitter_BuildBlendTables)() ? 1 : 0);
    MixBytes(table, 0x8000);
    MixByte(*built);
    std::memcpy(table, saved.data(), saved.size());
    FtStage("tables");
}

struct Regs {
    uint32_t eax, edx, ebx, esi, edi;
};

// A blend called with its registers (target EAX, colour ECX, alpha EDX)
// and marks in EBX, ESI and EDI; the registers after the call written to
// `out` (ECX is the game's to lose).
__declspec(naked) void __cdecl CallBlend(std::uintptr_t, uint32_t, uint32_t, uint32_t, Regs*)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push ebp
    __asm mov ebx, 0x13579BDF
    __asm mov esi, 0x2468ACE0
    __asm mov edi, 0x0F1E2D3C
    __asm mov eax, dword ptr [esp + 24]
    __asm mov ecx, dword ptr [esp + 28]
    __asm mov edx, dword ptr [esp + 32]
    __asm call dword ptr [esp + 20]
    __asm mov ebp, dword ptr [esp + 36]
    __asm mov dword ptr [ebp], eax
    __asm mov dword ptr [ebp + 4], edx
    __asm mov dword ptr [ebp + 8], ebx
    __asm mov dword ptr [ebp + 12], esi
    __asm mov dword ptr [ebp + 16], edi
    __asm pop ebp
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

void DriveBlends()
{
    Random random{0x5EED0F01};
    const std::uintptr_t blends[] = {addr::GZFontBlitter_Blend4444, addr::GZFontBlitter_Blend555,
                                     addr::GZFontBlitter_Blend565};
    for (const std::uintptr_t blend : blends) {
        std::vector<std::pair<uint32_t, uint32_t>> pairs = {
            {0, 0}, {0xFFFF, 0}, {0, 0xFFFF}, {0xFFFF, 0xFFFF}, {0x8000, 0x7FFF}, {0x07E0, 0xF81F}, {0xF000, 0x0FFF}};
        for (int i = 0; i < 48; ++i)
            pairs.emplace_back(random.Next() & 0xFFFF, random.Next() & 0xFFFF);
        for (const auto& [target, colour] : pairs)
            for (uint32_t alpha = 0; alpha <= 32; ++alpha) {
                Regs regs{};
                CallBlend(blend, target, colour, alpha, &regs);
                Mix(regs.eax);
                Mix(regs.edx);
                MixByte(regs.ebx == 0x13579BDF && regs.esi == 0x2468ACE0 && regs.edi == 0x0F1E2D3C ? 1 : 0);
            }
    }
    FtStage("blend");
}

// ====================================================================================
// The object

using ConstructFn = void*(__fastcall*)(void*, void*);
using QueryFn = uint8_t(__fastcall*)(void*, void*, uint32_t, void**);
using ReleaseFn = uint32_t(__fastcall*)(void*, void*);
using LockFn = uint32_t(__fastcall*)(void*, void*, void*, void*, uint32_t);

void DriveObject()
{
    const auto construct = Game<ConstructFn>(addr::GZFontBlitter_Construct);
    const auto query = Game<QueryFn>(addr::GZFontBlitter_QueryInterface);
    const auto release = Game<ReleaseFn>(addr::GZFontBlitter_Release);
    const auto vtable = static_cast<uint32_t>(addr::GZFontBlitter_Vtable);

    // Made in place, between margins; the vtable compared, not hashed.
    uint8_t space[24];
    std::memset(space, 0xA5, sizeof space);
    void* const self = space + 8;
    MixByte(construct(self, nullptr) == self ? 1 : 0);
    uint32_t word;
    std::memcpy(&word, space + 8, 4);
    MixByte(word == vtable ? 1 : 0);
    std::memset(space + 8, 0, 4);
    MixBytes(space, sizeof space);
    std::memcpy(space + 8, &vtable, 4);

    // The first one made builds the tables: the byte cleared and the tables
    // spoiled, then put back.
    {
        auto* const table = reinterpret_cast<uint8_t*>(addr::GZFontBlitter_BlendTable);
        const std::vector<uint8_t> saved(table, table + 0x8001);
        std::memset(table, 0x5A, 0x8001);
        table[0x8000] = 0;
        uint8_t other[8] = {};
        construct(other, nullptr);
        MixBytes(table, 0x8001);
        std::memset(table, 0x5A, 0x8001); // built: left alone
        construct(other, nullptr);
        MixBytes(table, 0x8001);
        std::memcpy(table, saved.data(), saved.size());
    }

    // QueryInterface: AddRef (the framework's) through its own slot 1.
    const uint32_t iids[] = {0, 1, 2, 0xA8BC2A75, 0xA8BC2A74, 0x28BC2A75, 0xFFFFFFFF, 0x10673960};
    for (const uint32_t iid : iids) {
        const int32_t count = 5;
        std::memcpy(space + 12, &count, 4);
        void* out = reinterpret_cast<void*>(static_cast<uintptr_t>(0x0BADF00D));
        MixByte(query(self, nullptr, iid, &out));
        Mix(out == self ? 1u : out == reinterpret_cast<void*>(static_cast<uintptr_t>(0x0BADF00D)) ? 2u : 3u);
        MixBytes(space + 12, 12);
    }

    // Release above 1 on the one in place.
    for (const int32_t count : {5, 3, 2, 0x7FFFFFFF}) {
        std::memcpy(space + 12, &count, 4);
        Mix(release(self, nullptr));
        MixBytes(space + 12, 12);
    }

    // Release to nothing on ones from the game's allocator: counts 2, 1, 0
    // and below 0 (deleted at once).
    const auto allocate = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new);
    for (const int32_t start : {2, 1, 0, -3}) {
        void* const made = allocate(8);
        construct(made, nullptr);
        std::memcpy(static_cast<uint8_t*>(made) + 4, &start, 4);
        for (int32_t left = start; left > 1; --left)
            Mix(release(made, nullptr));
        Mix(release(made, nullptr)); // deletes it
    }

    // AddRef and Release through the vtable of one made by the game's way.
    {
        void* const made = allocate(8);
        construct(made, nullptr);
        Mix(Virtual<uint32_t>(made, 0x4));
        Mix(Virtual<uint32_t>(made, 0x4));
        Mix(Virtual<uint32_t>(made, 0x8));
        Mix(Virtual<uint32_t>(made, 0x8));
    }
    FtStage("object");

    // Begin and End: the picture's Lock and Unlock with 0x8080, its EAX back.
    Random random{0x5EED0F02};
    Canvas canvas(1, 8, 4, 16, 5, 16, random);
    const auto begin = Game<LockFn>(addr::GZFontBlitter_Begin);
    const auto end = Game<LockFn>(addr::GZFontBlitter_End);
    for (const uint32_t answer : {0u, 1u, 0x12345601u, 0xFFFFFF00u, 0x80u}) {
        canvas.picture.lockAnswer = answer;
        Mix(begin(self, nullptr, nullptr, &canvas.picture, 3));
        Mix(end(self, nullptr, nullptr, &canvas.picture, 0x77));
    }
    FtStage("lock");
}

// ====================================================================================
// Glyphs on mock pictures

using DrawGlyphFn = uint8_t(__stdcall*)(void*, void*, const Rect*, uint32_t, const uint8_t*, const Rect*, int32_t,
                                        uint32_t, const uint32_t*, const Rect*);
using DrawGlyphsFn = uint8_t(__fastcall*)(void*, void*, void*, void*, const PlacedGlyph*, uint32_t, uint32_t,
                                          const uint32_t*, const Rect*);

struct PictureKind {
    uint32_t bpp, type;
    int32_t width, height;
    int32_t strideExtra; // bytes past width * bytes a pixel; -1 for none at all (stride 0)
};

// Coverage: 24 rows of 24 bytes, with every level the modes test.
struct Coverage {
    uint8_t bytes[24 * 24];
    explicit Coverage(Random& random)
    {
        const uint8_t special[] = {0, 1, 7, 8, 0x7F, 0x80, 0x81, 0xF8, 0xFF};
        for (std::size_t i = 0; i < sizeof bytes; ++i)
            bytes[i] = (i % 5 == 0) ? special[(i / 5) % sizeof special] : random.Byte();
    }
};

Rect RandomBox(Random& random, const Picture& p)
{
    const int32_t left = random.Range(-14, p.width + 6);
    const int32_t top = random.Range(-14, p.height + 6);
    return {left, top, left + random.Range(-2, 20), top + random.Range(-2, 20)};
}

void DriveGlyphs()
{
    Random random{0x5EED0F03};
    const PictureKind kinds[] = {
        {16, 3, 37, 23, 0}, {16, 4, 37, 23, 0}, {16, 5, 37, 23, 6}, {16, 6, 37, 23, 1}, {16, 7, 37, 23, 0},
        {16, 9, 37, 23, 2}, {16, 5, 37, 23, -1}, {32, 9, 29, 19, 0}, {32, 9, 29, 19, 12}, {24, 8, 29, 19, 5},
        {8, 2, 29, 19, 0},  {16, 5, 1, 1, 0},   {32, 9, 3, 40, 3},
    };
    const Coverage coverage(random);
    uint32_t colours[256];
    for (uint32_t& c : colours)
        c = random.Next();
    const Rect clips[] = {{0, 0, 0, 0}, {3, 2, 30, 17}, {-50, -50, 500, 500}, {10, 10, 5, 5}, {8, -3, 12, 40}};
    const auto drawGlyph = Game<DrawGlyphFn>(addr::GZFontBlitter_DrawGlyph);
    const auto drawGlyphs = Game<DrawGlyphsFn>(addr::GZFontBlitter_DrawGlyphs);
    uint8_t blitter[8] = {};

    uint32_t id = 100;
    for (const PictureKind& kind : kinds) {
        const uint32_t pixelBytes = kind.bpp == 16 ? 2 : kind.bpp == 8 ? 1 : kind.bpp == 24 ? 3 : 4;
        const uint32_t stride =
            kind.strideExtra < 0 ? 0 : static_cast<uint32_t>(kind.width) * pixelBytes + kind.strideExtra;
        for (uint32_t mode = 0; mode <= 4; ++mode) {
            for (std::size_t c = 0; c < sizeof clips / sizeof clips[0]; ++c) {
                const Rect* const clip = c == 0 ? nullptr : &clips[c];
                Canvas canvas(++id, kind.width, kind.height, kind.bpp, kind.type, stride, random);
                colours[0] = random.Next();

                // One glyph by DrawGlyph, its coverage from a random corner,
                // forwards or upwards.
                for (int i = 0; i < 3; ++i) {
                    const Rect box = RandomBox(random, canvas.picture);
                    const Rect source{random.Range(0, 3), random.Range(0, 3), random.Range(-5, 30), random.Range(-5, 30)};
                    const bool up = random.Next() & 1;
                    const uint8_t* pixels = up ? coverage.bytes + 23 * 24 : coverage.bytes;
                    MixByte(drawGlyph(nullptr, &canvas.picture, &box, 0x41 + i, pixels, &source, up ? -24 : 24, mode,
                                      colours, clip));
                    canvas.Hash();
                }

                // A list for DrawGlyphs: placed glyphs, some below a space.
                PlacedGlyph list[8];
                const uint32_t count = random.Next() % 9;
                for (uint32_t i = 0; i < count; ++i) {
                    PlacedGlyph& g = list[i];
                    g.box = RandomBox(random, canvas.picture);
                    g.width = random.Range(0, 20);
                    g.height = random.Range(0, 20);
                    g.pitch = 24;
                    g.pixels = coverage.bytes + random.Range(0, 3) + 24 * random.Range(0, 3);
                    const uint32_t characters[] = {0x41, 0x20, 0x1F, 0, 0xFFFF, 0x0E33, 0x7A};
                    g.character = characters[random.Next() % 7];
                }
                MixByte(drawGlyphs(blitter, nullptr, nullptr, &canvas.picture, list, count, mode, colours, clip));
                canvas.Hash();
            }
        }
    }
    FtStage("glyphs");
}

// ====================================================================================
// Strings through real fonts

// Fonts made for the test (GetFont without reuse: the font system keeps no
// hold on them) are released at the end.

// The game's string (cIGZString): made from text by 0x00408480, its buffer
// freed by the game's allocator.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

void DriveStrings()
{
    Random random{0x5EED0F04};
    void* const fonts = Game<void*(__cdecl*)()>(addr::SC4_Singleton913D22)();
    if (!fonts) {
        Mix(0xF0F0F0F0);
        FtStage("strings");
        return;
    }
    struct Face {
        const char* name;
        uint32_t size, style;
    };
    const Face faces[] = {{"Arial MT", 10, 0}, {"Arial MT", 14, 1}, {"Arial MT", 23, 0}, {"Times New Roman", 17, 2}};
    const char* const texts[] = {"OpenSC4 fonts 0123456789", "The quick brown fox, jumps! Wgjpqy|[]{}", "Sim City 4 \x7F~@#"};
    struct Place {
        int32_t x, y;
        bool clipped;
    };
    const Place places[] = {{2, 2, false}, {-7, 9, false}, {96, 27, false}, {4, 6, true}, {-3, -5, true}};
    const Rect clip{11, 7, 93, 21};
    struct Target {
        uint32_t bpp, type;
    };
    const Target targets[] = {{16, 5}, {16, 4}, {16, 7}, {32, 9}};
    uint32_t id = 500;
    uint32_t draws = 0, changed = 0;

    for (const Face& face : faces) {
        GameString name{};
        Game<void*(__fastcall*)(GameString*, void*, const char*)>(addr::SC4String_FromText)(&name, nullptr, face.name);
        void* const font = Virtual<void*>(fonts, 0x2C, &name, uint32_t{0}, face.size, face.style, uint32_t{0});
        name.vtable = addr::SC4String_Vtable;
        if (name.begin)
            Game<void(__cdecl*)(void*)>(addr::SC4_Free)(name.begin);
        if (!font) {
            Mix(0xF0F00000 | face.size);
            continue;
        }
        auto* const bytes = static_cast<uint8_t*>(font);
        float scaleX, scaleY;
        std::memcpy(&scaleX, bytes + 0x68, 4);
        std::memcpy(&scaleY, bytes + 0x6C, 4);
        uint32_t encoding;
        std::memcpy(&encoding, bytes + 0x7C, 4);
        Mix(encoding);

        // The font system hands its fonts a blitter of the game's own
        // (vtable 0x00AC14D0, through a drawing context); the font gets one
        // of ours (the f4 class, made as Load makes it) for the draws, and its
        // own back after.
        void* const own = *reinterpret_cast<void**>(bytes + 0x8C);
        if (own)
            Virtual<uint32_t>(own, 0x4);
        void* const blitter = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(8);
        Game<void*(__fastcall*)(void*, void*)>(addr::GZFontBlitter_Construct)(blitter, nullptr);
        Virtual<void>(font, 0xD8, blitter);

        // Mode 2 colours each level from DrawString's ramp of 128, but the
        // glyphs' grey levels run to 255 unless the font has a table of
        // levels (+0x5C to +0x64, a vector; nothing sets it that we found),
        // so the game reads past the ramp into the heap. For the draws in
        // mode 2 the font gets a table halving the levels, put back after.
        uint32_t levels[3];
        std::memcpy(levels, bytes + 0x5C, sizeof levels);
        static uint8_t half[256];
        for (int i = 0; i < 256; ++i)
            half[i] = static_cast<uint8_t>(i >> 1);
        const uint32_t table[3] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(half)),
                                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(half + 256)),
                                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(half + 256))};

        for (const uint32_t mode : {1u, 2u, 3u}) {
            std::memcpy(bytes + 0x5C, mode == 2 ? table : levels, sizeof levels);
            Virtual<void>(font, 0x40, mode);           // the draw mode
            Virtual<void>(font, 0x78, scaleX, scaleY); // empties the glyph cache
            for (const Target& target : targets) {
                Canvas canvas(++id, 120, 34, target.bpp, target.type, 120 * (target.bpp / 8) + 4, random);
                for (const char* text : texts) {
                    for (const Place& place : places) {
                        const uint32_t colour = target.bpp == 16 ? random.Next() & 0xFFFF : random.Next();
                        const uint32_t colour2 = target.bpp == 16 ? random.Next() & 0xFFFF : random.Next();
                        const uint32_t length = static_cast<uint32_t>(std::strlen(text));
                        std::u16string wide(text, text + length);
                        const void* chars = encoding == 0x10 ? static_cast<const void*>(wide.c_str()) : text;
                        int32_t pen[2] = {0x7777, 0x7777};
                        const bool withPen = (random.Next() & 3) == 0;
                        if (withPen) {
                            pen[0] = place.x + 3;
                            pen[1] = place.y + 1;
                        }
                        const std::vector<uint8_t> before = canvas.bytes;
                        const uint8_t drawn = Virtual<uint8_t>(font, 0xCC, &canvas.picture, place.x, place.y, chars,
                                                               length, colour, colour2,
                                                               place.clipped ? &clip : nullptr,
                                                               withPen ? pen : nullptr);
                        ++draws;
                        if (canvas.bytes != before)
                            ++changed;
                        MixByte(drawn);
                        Mix(static_cast<uint32_t>(pen[0]));
                        Mix(static_cast<uint32_t>(pen[1]));
                        canvas.Hash();
                    }
                }
                // Lock refused: nothing drawn.
                canvas.picture.lockAnswer = 0;
                const void* const four = encoding == 0x10 ? static_cast<const void*>(u"Open") : "Open";
                MixByte(Virtual<uint8_t>(font, 0xCC, &canvas.picture, 1, 1, four, uint32_t{4}, uint32_t{0xFFFF},
                                         uint32_t{0}, static_cast<const Rect*>(nullptr),
                                         static_cast<int32_t*>(nullptr)));
                canvas.Hash();
            }
        }
        std::memcpy(bytes + 0x5C, levels, sizeof levels);
        Virtual<void>(font, 0xD8, own); // ours released (and deleted) here
        if (own)
            Virtual<uint32_t>(own, 0x8);
        Virtual<uint32_t>(font, 0x8);
    }
    log::Write("autotest: fonts: %u of %u string draws changed pixels", changed, draws);
    FtStage("strings");
}

// ====================================================================================
// The T2K holder and its stream (f5)

using HThis = void*(__fastcall*)(void*, void*);
using HOpen = uint8_t(__fastcall*)(void*, void*, void*);
using HRemaining = uint32_t(__fastcall*)(void*, void*);
using HConstructStream = void*(__fastcall*)(void*, void*, void*);
using HRead = uint8_t(__fastcall*)(void*, void*, uint8_t*, uint32_t, uint32_t);
using HReadCallback = uint32_t(__cdecl*)(void*, uint8_t*, uint32_t, uint32_t);
using HReadAll = uint8_t(__fastcall*)(void*, void*, uint32_t*, uint32_t);
using HGetName = uint8_t(__fastcall*)(void*, void*, GameString*, uint32_t);
using HStyles = uint32_t(__fastcall*)(void*, void*);
using HGlyph = uint32_t(__fastcall*)(void*, void*, uint32_t);
using HMapFind = uint8_t*(__fastcall*)(void*, void*, const uint32_t*);

constexpr std::uintptr_t kHolderDestruct = 0x00897E36;
constexpr std::uintptr_t kMapFind = 0x0089762F;
constexpr std::uintptr_t kNameMap = 0x00B5C41C;
constexpr std::uintptr_t kNameTable = 0x00B150F0;
constexpr uint32_t kNameTableBytes = 0xEF8;

template <typename T>
T Field(const void* base, std::size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof value);
    return value;
}

uint8_t* At32(uint32_t address) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); }

// A game string made from text; freed by FreeString.
GameString MakeString(const char* text)
{
    GameString s{};
    Game<void*(__fastcall*)(GameString*, void*, const char*)>(addr::SC4String_FromText)(&s, nullptr, text);
    return s;
}

GameString EmptyString()
{
    GameString s{};
    Game<void*(__fastcall*)(GameString*, void*)>(addr::SC4String_SC4String)(&s, nullptr);
    return s;
}

void FreeString(GameString& s)
{
    s.vtable = addr::SC4String_Vtable;
    if (s.begin)
        Game<void(__cdecl*)(void*)>(addr::SC4_Free)(s.begin);
    s.begin = nullptr;
}

void MixString(const GameString& s)
{
    Mix(static_cast<uint32_t>(s.end - s.begin));
    MixBytes(reinterpret_cast<const uint8_t*>(s.begin), static_cast<std::size_t>(s.end - s.begin));
}

// The holder as Open leaves it: which objects there are (not where), the
// unwrapped bytes, flags and kind, the stream's fields and what Open set in
// the scaler; the map's count.
void MixHolder(const uint8_t* h)
{
    uint32_t present = 0;
    for (std::size_t field = 0; field <= 0x10; field += 4)
        present = present << 1 | (Field<uint32_t>(h, field) ? 1u : 0u);
    Mix(present);
    const auto* begin = Field<const uint8_t*>(h, 0x14);
    const auto* end = Field<const uint8_t*>(h, 0x18);
    Mix(static_cast<uint32_t>(end - begin));
    if (begin)
        MixBytes(begin, static_cast<std::size_t>(end - begin));
    Mix(Field<uint16_t>(h, 0x20));
    Mix(Field<uint32_t>(h, 0x24));
    if (const auto* file = Field<const uint8_t*>(h, 0xC)) {
        Mix(Field<uint32_t>(file, 0x80));
        Mix(Field<uint32_t>(file, 0x84));
        MixByte(Field<uint8_t>(file, 0x88));
    }
    if (const auto* made = Field<const uint8_t*>(h, 8)) {
        const auto* type = Field<const uint8_t*>(made, 0x180);
        Mix(Field<uint32_t>(made, 0x48));
        Mix(Field<uint16_t>(type, 0x74));
        Mix(Field<uint16_t>(type, 0x76));
    }
    Mix(Field<uint32_t>(h, 0x2C)); // the glyph map's count
}

// The stream's reads at offsets around its ends, into a buffer between
// margins; ReadAll whole, cut short and empty.
void DriveStreamOf(void* stream)
{
    const auto remaining = Game<HRemaining>(addr::GZFontStream_Remaining);
    const auto read = Game<HRead>(addr::GZFontStream_Read);
    const auto callback = Game<HReadCallback>(addr::GZFontStream_ReadCallback);
    const auto readAll = Game<HReadAll>(addr::GZFontStream_ReadAll);
    const uint32_t length = remaining(stream, nullptr);
    Mix(length);
    const uint32_t offsets[] = {0, 1, 7, 100, length - 3, length, length + 5};
    const uint32_t counts[] = {0, 1, 4, 64};
    uint8_t buffer[64 + 64 + 64];
    for (const uint32_t offset : offsets)
        for (const uint32_t count : counts) {
            std::memset(buffer, 0xA5, sizeof buffer);
            MixByte(read(stream, nullptr, buffer + 64, offset, count));
            MixBytes(buffer, sizeof buffer);
            std::memset(buffer, 0x5A, sizeof buffer);
            Mix(callback(stream, buffer + 64, offset, count));
            MixBytes(buffer, sizeof buffer);
        }
    for (const uint32_t most : {0xFFFFFFFFu, 100u, 0u}) {
        uint32_t vector[3] = {};
        MixByte(readAll(stream, nullptr, vector, most));
        const auto* begin = At32(vector[0]);
        Mix(vector[1] - vector[0]);
        if (begin)
            MixBytes(begin, vector[1] - vector[0]);
        // Again into the same vector, which empties it first.
        MixByte(readAll(stream, nullptr, vector, most == 0 ? 3u : most));
        Mix(vector[1] - vector[0]);
        if (vector[0])
            MixBytes(At32(vector[0]), vector[1] - vector[0]);
        if (vector[0])
            Game<void(__cdecl*)(void*)>(addr::SC4_Free)(At32(vector[0]));
    }
}

void WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        if (!bytes.empty())
            std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
}

std::vector<uint8_t> ReadWhole(const std::string& path)
{
    std::vector<uint8_t> bytes;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t chunk[4096];
        std::size_t n;
        while ((n = std::fread(chunk, 1, sizeof chunk, f)) != 0)
            bytes.insert(bytes.end(), chunk, chunk + n);
        std::fclose(f);
    }
    return bytes;
}

// The fonts: the game's Fonts folder (every face it has), four of
// Windows's, two OpenType faces if there, and files of our own in the temp
// folder: Courier New scrambled behind an MXFN header, MXFN headers too
// short and just long enough, junk, and a file that is not there.
std::vector<std::string> FontPaths()
{
    std::vector<std::string> paths;
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string root(exe);
    for (int up = 0; up < 2; ++up)
        root = root.substr(0, root.find_last_of("\\/"));
    const char* const game[] = {"Arial MT (Bold Italic).mxf", "Arial MT (Bold).mxf", "Arial MT (Italic).mxf",
                                "Arial MT.mxf", "Arta (Italic).mxf", "Arta.mxf", "CordiaUPC (Bold).mxf",
                                "CordiaUPC.mxf", "Courier LD (Bold Italic).mxf", "Courier LD (Bold).mxf",
                                "Courier LD (Italic).mxf", "Courier LD.mxf", "HG-Gothic (Bold).mxf", "HG-Gothic.mxf",
                                "Hy Round Gothic (bold).mxf", "Hy Round Gothic.mxf", "MHei B5 (Bold).mxf",
                                "MHei GB (Bold).mxf", "Stoclet ITC TT (Bold).mxf"};
    for (const char* name : game)
        paths.push_back(root + "\\Fonts\\" + name);
    char windows[MAX_PATH] = {};
    GetWindowsDirectoryA(windows, MAX_PATH);
    const std::string system = std::string(windows) + "\\Fonts\\";
    for (const char* name : {"arial.ttf", "times.ttf", "cour.ttf", "consola.ttf", "OpusMetronomeStd.otf",
                             "OpusBigTimeStd.otf"})
        paths.push_back(system + name);

    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    const std::string ours = std::string(temp) + "opensc4-fonttest\\";
    CreateDirectoryA(ours.c_str(), nullptr);
    std::vector<uint8_t> courier = ReadWhole(system + "cour.ttf");
    {
        const uint32_t scrambled = 4096;
        const uint8_t key = 0x5A;
        std::vector<uint8_t> file = {'M', 'X', 'F', 'N', 0, 0, 0x10, 0x00, key};
        for (std::size_t i = 0; i < courier.size(); ++i)
            file.push_back(i < scrambled ? static_cast<uint8_t>(courier[i] ^ key) : courier[i]);
        WriteFile(ours + "scrambled.mxf", file);
    }
    WriteFile(ours + "short.mxf", {'M', 'X', 'F', 'N', 0});
    WriteFile(ours + "ten.mxf", {'M', 'X', 'F', 'N', 0, 0, 0, 3, 0x11, 'A'});
    WriteFile(ours + "plain.ttf", courier);
    Random random{0x5EED0F05};
    std::vector<uint8_t> junk(3000);
    for (uint8_t& b : junk)
        b = random.Byte();
    WriteFile(ours + "junk.ttf", junk);
    for (const char* name : {"scrambled.mxf", "short.mxf", "ten.mxf", "plain.ttf", "junk.ttf", "missing.ttf"})
        paths.push_back(ours + name);
    return paths;
}

void DriveHolder()
{
    const auto construct = Game<HThis>(addr::GZFontFile_Construct);
    const auto open = Game<HOpen>(addr::GZFontFile_Open);
    const auto close = Game<HThis>(addr::GZFontFile_Close);
    const auto styles = Game<HStyles>(addr::GZFontFile_Styles);
    const auto getName = Game<HGetName>(addr::GZFontFile_GetName);
    const auto glyph = Game<HGlyph>(addr::GZFontFile_GlyphByUnicodeName);
    const auto allocate = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new);
    const auto release = Game<void(__cdecl*)(void*)>(addr::SC4_operator_delete);
    const std::vector<std::string> paths = FontPaths();

    // Made in place between margins; its map's header is a pointer.
    {
        uint8_t space[16 + 0x34 + 16];
        std::memset(space, 0xA5, sizeof space);
        MixByte(construct(space + 16, nullptr) == space + 16 ? 1 : 0);
        uint8_t copy[sizeof space];
        std::memcpy(copy, space, sizeof space);
        std::memset(copy + 16 + 0x28, 0, 4);
        MixBytes(copy, sizeof copy);
        Game<HThis>(kHolderDestruct)(space + 16, nullptr);
    }

    const uint32_t characters[] = {0x20, 0x41, 0x61, 0xE9, 0x152, 0x2022, 0x20AC, 0xFB01, 0x0E33, 0x4E00, 0xFFFF,
                                   0x10041, 0};
    uint32_t opened = 0, named = 0;
    std::string kinds;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        void* const h = allocate(0x34);
        construct(h, nullptr);
        GameString path = MakeString(paths[i].c_str());
        const uint8_t ok = open(h, nullptr, &path);
        MixByte(ok);
        MixHolder(static_cast<const uint8_t*>(h));
        opened += ok;
        char kind[4];
        std::snprintf(kind, sizeof kind, "%u", Field<uint32_t>(h, 0x24));
        kinds += kind;
        if (void* const stream = Field<void*>(h, 0xC))
            if (Virtual<uint8_t>(stream, 0x10))
                DriveStreamOf(stream);
        Mix(styles(h, nullptr));
        for (const uint32_t id : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 0x10u}) {
            GameString name = EmptyString();
            const uint8_t got = getName(h, nullptr, &name, id);
            MixByte(got);
            named += got;
            MixString(name);
            FreeString(name);
        }
        // The family name into a string that held something.
        {
            GameString name = MakeString("Something Bold Italic");
            MixByte(getName(h, nullptr, &name, 1));
            MixString(name);
            FreeString(name);
        }
        const uint32_t faceKind = Field<uint32_t>(h, 0x24);
        if (ok && (faceKind == 2 || faceKind == 3))
            for (int pass = 0; pass < 2; ++pass)
                for (const uint32_t c : characters)
                    Mix(glyph(h, nullptr, c));
        // Opened again over what is open (Open closes first), then closed
        // twice; then the name of a closed face.
        if (i + 1 < paths.size()) {
            GameString next = MakeString(paths[(i + 7) % paths.size()].c_str());
            MixByte(open(h, nullptr, &next));
            MixHolder(static_cast<const uint8_t*>(h));
            FreeString(next);
        }
        MixByte(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(close(h, nullptr))));
        MixHolder(static_cast<const uint8_t*>(h));
        MixByte(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(close(h, nullptr))));
        {
            GameString name = MakeString("Kept");
            MixByte(getName(h, nullptr, &name, 1));
            MixString(name);
            FreeString(name);
            Mix(styles(h, nullptr));
        }
        FreeString(path);
        Game<HThis>(kHolderDestruct)(h, nullptr);
        release(h);
    }
    log::Write("autotest: fonts: %u of %zu files opened (kinds %s), %u names", opened, paths.size(), kinds.c_str(),
               named);
    FtStage("holder");

    // The stream on its own, on our files: the MXFN header read or not.
    const auto constructStream = Game<HConstructStream>(addr::GZFontStream_Construct);
    for (std::size_t i = paths.size() - 6; i < paths.size(); ++i) {
        uint8_t space[16 + 0x8C + 16];
        std::memset(space, 0xA5, sizeof space);
        GameString path = MakeString(paths[i].c_str());
        void* const stream = space + 16;
        MixByte(constructStream(stream, nullptr, &path) == stream ? 1 : 0);
        MixBytes(space + 16 + 0x80, 0x8C - 0x80 + 16);
        const uint8_t isOpen = Virtual<uint8_t>(stream, 0x10);
        MixByte(isOpen);
        if (isOpen)
            DriveStreamOf(stream);
        Game<HThis>(addr::cRZFile_Destruct)(stream, nullptr);
        FreeString(path);
    }
    FtStage("stream");

    // The static table of glyph names: built (again; the map keeps what it
    // has), then every entry's name.
    Game<void(__cdecl*)()>(addr::GZFontFile_BuildNameTable)();
    const auto find = Game<HMapFind>(kMapFind);
    Mix(Field<uint32_t>(At32(static_cast<uint32_t>(kNameMap)), 4));
    for (uint32_t i = 0; i < kNameTableBytes; i += 8) {
        const uint32_t key = Field<uint16_t>(At32(static_cast<uint32_t>(kNameTable)), i);
        const uint8_t* const node = find(At32(static_cast<uint32_t>(kNameMap)), nullptr, &key);
        if (node == Field<const uint8_t*>(At32(static_cast<uint32_t>(kNameMap)), 0)) {
            Mix(0xFFFFFFFF);
            continue;
        }
        const char* const name = Field<const char*>(node, 0x14);
        MixBytes(reinterpret_cast<const uint8_t*>(name), std::strlen(name) + 1);
    }
    FtStage("names");
}

// ====================================================================================
// The director (f1)

// A director of our own whose COM object answers the font system's maker
// with nothing, so its start does not reach the framework.
struct MockCom {
    void** vtable;
    uint32_t calls;
};

uint32_t __fastcall MockGetClassObject(MockCom* com, void*, uint32_t classId, uint32_t iid, void** out)
{
    ++com->calls;
    Mix(classId);
    Mix(iid);
    MixByte(*out == nullptr ? 1 : 0);
    return 0;
}

struct MockDirector {
    void** vtable;
    MockCom* com;
};

MockCom* __fastcall MockDirectorCom(MockDirector* director, void*)
{
    Mix(0xD1C0);
    return director->com;
}

void DriveDirector()
{
    // The three factories: each object's vtable, then a reference given and
    // taken, which ends it.
    struct Factory {
        std::uintptr_t address, vtable;
    };
    const Factory factories[] = {{addr::GZFontDirector_MakeFontSystem, 0x00AC5488},
                                 {addr::GZFontDirector_MakeFont, 0x00AC55D8},
                                 {addr::GZFontDirector_MakeBlitter, addr::GZFontBlitter_Vtable}};
    for (const Factory& factory : factories) {
        void* const made = Game<void*(__cdecl*)()>(factory.address)();
        MixByte(made ? 1 : 0);
        if (!made)
            continue;
        MixByte(Field<uintptr_t>(made, 0) == factory.vtable ? 1 : 0);
        Mix(Virtual<uint32_t>(made, 0x4));
        Mix(Virtual<uint32_t>(made, 0x8));
    }

    // The constructor on a director of our own: its vtables, its three
    // classes and their factories (the class table: buckets at +0x3C and
    // +0x40, nodes {next, class, factory, type}), then the base's destructor.
    {
        alignas(8) uint8_t space[16 + 0x4C + 16];
        std::memset(space, 0xA5, sizeof space);
        void* const self = space + 16;
        MixByte(Game<HThis>(addr::GZFontDirector_Construct)(self, nullptr) == self ? 1 : 0);
        MixByte(Field<uint32_t>(self, 0) == 0x00AC5548 ? 1 : 0);
        MixByte(Field<uint32_t>(self, 4) == 0x00AC5518 ? 1 : 0);
        MixBytes(space, 16);
        MixBytes(space + 16 + 0x4C, 16);
        auto* const buckets = Field<uint8_t**>(self, 0x3C);
        const auto count = static_cast<uint32_t>(Field<uint8_t**>(self, 0x40) - buckets);
        Mix(count);
        for (const uint32_t id : {0x20673960u, 0x1AA404ACu, 0x08CDA30Bu, 0x12345678u}) {
            uint32_t factory = 0, type = 0;
            for (uint8_t* node = count ? buckets[id % count] : nullptr; node; node = Field<uint8_t*>(node, 0))
                if (Field<uint32_t>(node, 4) == id) {
                    factory = Field<uint32_t>(node, 8);
                    type = Field<uint32_t>(node, 0xC);
                    break;
                }
            Mix(factory);
            Mix(type);
        }
        Mix(Virtual<uint32_t>(self, 0x34));
        Game<HThis>(addr::cRZCOMDllDirector_Destruct)(self, nullptr);
    }

    // The ID by address and through the real director's vtable; the getter
    // (the director is made, so it is only handed back).
    void* const director = Game<void*(__cdecl*)()>(addr::GZFontDirector_Get)();
    MixByte(director == reinterpret_cast<void*>(static_cast<uintptr_t>(0x00B543C8)) ? 1 : 0);
    MixByte(*reinterpret_cast<const uint8_t*>(0x00B54414) & 1);
    Mix(Game<uint32_t(__fastcall*)(void*, void*)>(addr::GZFontDirector_DirectorId)(nullptr, nullptr));
    Mix(Virtual<uint32_t>(director, 0x34));

    // The start, on a director whose COM object makes nothing.
    {
        static void* comTable[4] = {};
        static void* directorTable[16] = {};
        comTable[3] = reinterpret_cast<void*>(&MockGetClassObject);
        directorTable[12] = reinterpret_cast<void*>(&MockDirectorCom);
        MockCom com{comTable, 0};
        MockDirector mock{directorTable, &com};
        for (uint32_t argument : {0u, 0x77u}) {
            MixByte(Game<uint8_t(__fastcall*)(void*, void*, uint32_t)>(addr::GZFontDirector_OnStart)(&mock, nullptr,
                                                                                                   argument));
            Mix(com.calls);
        }
    }

    // The byte: set to 1 whatever it held.
    for (const uint8_t held : {0, 1, 0x80, 0xFF}) {
        uint8_t bytes[3] = {0xA5, held, 0x5A};
        MixByte(Game<void*(__fastcall*)(void*, void*)>(addr::GZFontStatic_SetByte)(bytes + 1, nullptr) == bytes + 1
                    ? 1
                    : 0);
        MixBytes(bytes, sizeof bytes);
    }
    FtStage("director");
}

// ====================================================================================
// The font system (f2a)

// Systems of our own, made by the game's allocator and the constructor at
// its address, given a reference; taken down by the last Release, which
// shuts them down. Fonts are made from the game's and Windows's faces and
// files of our own; every result goes into the hash with the texts and
// numbers the system keeps, never an address (fonts by the order they were
// first seen in).

using SThis = uint32_t(__fastcall*)(void*, void*);
using SOne = uint32_t(__fastcall*)(void*, void*, uint32_t);
using STwo = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t);
using SThree = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t);
using SFour = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
using SFive = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

uint32_t U(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t TreeNext(uint32_t node) { return Game<uint32_t(__cdecl*)(uint32_t)>(0x0040D4C0)(node); }

// The text of a string kept in the system (begin and end at +4 and +8).
void MixTextAt(const uint8_t* string)
{
    const char* const begin = Field<const char*>(string, 4);
    const char* const end = Field<const char*>(string, 8);
    const uint32_t size = begin ? static_cast<uint32_t>(end - begin) : 0;
    Mix(size);
    if (size)
        MixBytes(reinterpret_cast<const uint8_t*>(begin), size);
}

std::string TextAt(const uint8_t* string)
{
    const char* const begin = Field<const char*>(string, 4);
    return begin ? std::string(begin, Field<const char*>(string, 8)) : std::string();
}

// Fonts by the order they were first seen in.
struct Seen {
    std::vector<void*> fonts;
    uint32_t Index(void* font)
    {
        if (!font)
            return 0xFFFFFFFF;
        for (std::size_t i = 0; i < fonts.size(); ++i)
            if (fonts[i] == font)
                return static_cast<uint32_t>(i);
        fonts.push_back(font);
        return static_cast<uint32_t>(fonts.size() - 1);
    }
};

// Every map of the system: the faces (path, name, styles, kind, type unless
// the path has `noType` in it, each font's size and style), the names, the
// families, the aliases, the sizes by face; the patterns and the fields.
void MixSystem(void* s, const char* noType = nullptr)
{
    const uint8_t* const b = static_cast<const uint8_t*>(s);
    MixByte(b[8]);
    Mix(Field<uint32_t>(b, 0xC));
    Mix(Field<uint32_t>(b, 0x10));
    Mix(Field<uint32_t>(b, 0x14));
    Mix(Field<uint32_t>(b, 0x18));
    Mix(Field<uint32_t>(b, 0x1C));
    Mix(Field<uint32_t>(b, 0x20));
    MixByte(Field<uint32_t>(b, 0x24) ? 1 : 0);
    const auto each = [&](uint32_t map, auto&& fn) {
        Mix(Field<uint32_t>(At32(map), 4));
        const uint32_t header = Field<uint32_t>(At32(map), 0);
        for (uint32_t node = Field<uint32_t>(At32(header), 8); node != header; node = TreeNext(node))
            fn(At32(node));
    };
    each(U(b) + 0x40, [&](const uint8_t* node) {
        MixTextAt(node + 0x10);
        MixTextAt(node + 0x28);
        Mix(Field<uint32_t>(node, 0x3C));
        Mix(Field<uint32_t>(node, 0x40));
        if (!noType || TextAt(node + 0x10).find(noType) == std::string::npos)
            Mix(Field<uint32_t>(node, 0x44));
        const uint8_t* const list = Field<const uint8_t*>(node, 0x24);
        for (const uint8_t* e = Field<const uint8_t*>(list, 0); e != list; e = Field<const uint8_t*>(e, 0)) {
            Mix(Virtual<uint32_t>(Field<void*>(e, 8), 0x28));
            Mix(Field<uint32_t>(e, 0xC));
        }
        Mix(0xFACE);
    });
    each(U(b) + 0x28, [&](const uint8_t* node) { MixTextAt(node + 0x10); });
    each(U(b) + 0x34, [&](const uint8_t* node) {
        Mix(Field<uint32_t>(node, 0x10));
        MixTextAt(node + 0x14);
    });
    each(U(b) + 0x4C, [&](const uint8_t* node) {
        MixTextAt(node + 0x10);
        const uint8_t* const begin = Field<const uint8_t*>(node, 0x24);
        const uint8_t* const end = Field<const uint8_t*>(node, 0x28);
        Mix(static_cast<uint32_t>(end - begin));
        for (const uint8_t* name = begin; name < end; name += 0x14)
            MixTextAt(name);
    });
    each(U(b) + 0x64, [&](const uint8_t* node) {
        MixTextAt(node + 0x10);
        Mix(Field<uint32_t>(node, 0x24));
        Mix(Field<uint32_t>(node, 0x28));
    });
    const uint8_t* const patterns = Field<const uint8_t*>(b, 0x58);
    for (const uint8_t* p = patterns; p < Field<const uint8_t*>(b, 0x5C); p += 0x14)
        MixTextAt(p);
}

// A font as the system made it: its path, size, style, kind, draw mode and
// encoding, and which font it is.
void MixFont(void* font, Seen& seen)
{
    Mix(seen.Index(font));
    if (!font)
        return;
    GameString path = EmptyString();
    Virtual<void>(font, 0x54, &path);
    MixString(path);
    FreeString(path);
    Mix(Virtual<uint32_t>(font, 0x28));
    Mix(Virtual<uint32_t>(font, 0x1C));
    Mix(Virtual<uint32_t>(font, 0x24));
    Mix(Field<uint32_t>(font, 0x4C));
    Mix(Field<uint32_t>(font, 0x7C));
}

void* NewSystem()
{
    void* const s = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x70);
    Game<SThis>(addr::GZFontSystem_Construct)(s, nullptr);
    Virtual<uint32_t>(s, 0x4);
    return s;
}

uint32_t ReleaseSystem(void* s) { return Game<SThis>(addr::GZFontSystem_Release)(s, nullptr); }

// A blitter of our own that counts what it is given and taken.
struct MockUnknown {
    void** vtable;
    int32_t refs;
};

uint32_t __fastcall MockAddRef(MockUnknown* u, void*) { return static_cast<uint32_t>(++u->refs); }
uint32_t __fastcall MockRelease(MockUnknown* u, void*) { return static_cast<uint32_t>(--u->refs); }

std::string GameFonts()
{
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string root(exe);
    for (int up = 0; up < 2; ++up)
        root = root.substr(0, root.find_last_of("\\/"));
    return root + "\\Fonts";
}

std::string TestFolder()
{
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    return std::string(temp) + "opensc4-fonttest\\";
}

// Every face of the game's and Windows's (FontPaths' first 25) and our
// files, added to a system.
void AddFaces(void* s, const std::vector<std::string>& paths, bool mix)
{
    const auto add = Game<SOne>(addr::GZFontSystem_AddFontFile);
    for (const std::string& p : paths) {
        GameString path = MakeString(p.c_str());
        const uint32_t added = add(s, nullptr, U(&path)) & 0xFF;
        if (mix)
            MixByte(static_cast<uint8_t>(added));
        FreeString(path);
    }
}

void DriveSystemObject()
{
    const auto queryInterface = Game<STwo>(addr::GZFontSystem_QueryInterface);
    const auto queryInterface2 = Game<STwo>(addr::GZFontSystem_QueryInterface2);
    const auto release2 = Game<SThis>(addr::GZFontSystem_Release2);
    void* const s = NewSystem();
    MixSystem(s);

    for (const uint32_t iid : {1u, 0x00673960u, 0x287FB697u, 0u, 2u, 0x20673960u, 0x10673960u, 0xFFFFFFFFu}) {
        for (int through = 0; through < 2; ++through) {
            uint32_t out = 0x12345678;
            const uint32_t ok = (through ? queryInterface2(static_cast<uint8_t*>(s) + 4, nullptr, iid, U(&out))
                                         : queryInterface(s, nullptr, iid, U(&out))) &
                                0xFF;
            MixByte(static_cast<uint8_t>(ok));
            Mix(out == 0x12345678 ? 0xFFFF : out - U(s));
            Mix(Field<uint32_t>(s, 0xC));
        }
    }
    // Down to one reference, by address, through the thunk and the vtables.
    Mix(Virtual<uint32_t>(s, 0x4));
    Mix(Virtual<uint32_t>(static_cast<uint8_t*>(s) + 4, 0x4));
    while (Field<uint32_t>(s, 0xC) > 2)
        Mix(Game<SThis>(addr::GZFontSystem_Release)(s, nullptr));
    Mix(release2(static_cast<uint8_t*>(s) + 4, nullptr));
    Mix(Field<uint32_t>(s, 0xC));

    // The version, by address and through the +4 vtable's two slots.
    Mix(Game<SThis>(addr::GZFontSystem_Version)(s, nullptr));
    Mix(Virtual<uint32_t>(static_cast<uint8_t*>(s) + 4, 0x14));
    Mix(Virtual<uint32_t>(static_cast<uint8_t*>(s) + 4, 0x30));

    // The encoding and the blitter.
    const uint32_t encoding = Field<uint32_t>(s, 0x20);
    for (const uint32_t e : {0x10u, 0x4E4u, 0u, encoding}) {
        MixByte(static_cast<uint8_t>(Game<SOne>(addr::GZFontSystem_SetEncoding)(s, nullptr, e)));
        Mix(Field<uint32_t>(s, 0x20));
    }
    {
        static void* table[4] = {};
        table[1] = reinterpret_cast<void*>(&MockAddRef);
        table[2] = reinterpret_cast<void*>(&MockRelease);
        MockUnknown a{table, 1}, b{table, 1};
        const auto setBlitter = Game<SOne>(addr::GZFontSystem_SetBlitter);
        for (MockUnknown* u : {&a, &a, &b, static_cast<MockUnknown*>(nullptr), &b}) {
            setBlitter(s, nullptr, U(u));
            MixByte(Field<uint32_t>(s, 0x24) == U(&a) ? 1 : Field<uint32_t>(s, 0x24) == U(&b) ? 2 : 0);
            Mix(static_cast<uint32_t>(a.refs));
            Mix(static_cast<uint32_t>(b.refs));
        }
        setBlitter(s, nullptr, 0);
        Mix(static_cast<uint32_t>(b.refs));
    }

    // Init, again, through the thunk; the families' faces, new and replaced.
    MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFontSystem_Init)(s, nullptr)));
    MixSystem(s);
    MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFontSystem_Init)(s, nullptr)));
    MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFontSystem_Init2)(static_cast<uint8_t*>(s) + 4, nullptr)));
    for (const auto& [code, name] : {std::pair<uint32_t, const char*>{5, "Georgia"}, {77, "New Face"},
                                     {0xFFFFFFFF, "Negative"}, {9, ""}, {77, "Again"}}) {
        GameString text = MakeString(name);
        MixByte(static_cast<uint8_t>(Game<STwo>(addr::GZFontSystem_SetFamily)(s, nullptr, code, U(&text))));
        FreeString(text);
    }
    MixSystem(s);

    // Shutdown with Init's byte set, then without; Init after.
    MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFontSystem_Shutdown)(s, nullptr)));
    MixSystem(s);
    MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFontSystem_Shutdown2)(static_cast<uint8_t*>(s) + 4, nullptr)));
    MixByte(static_cast<uint8_t>(Virtual<uint32_t>(static_cast<uint8_t*>(s) + 4, 0x20)));
    MixSystem(s);
    Mix(ReleaseSystem(s));

    // In place between margins: the constructor, then the destructor; the
    // deleting destructor without freeing, then freed by us.
    {
        alignas(8) uint8_t space[16 + 0x70 + 16];
        std::memset(space, 0xA5, sizeof space);
        void* const self = space + 16;
        MixByte(Game<SThis>(addr::GZFontSystem_Construct)(self, nullptr) == U(self) ? 1 : 0);
        MixBytes(space, 16);
        MixBytes(space + 16 + 0x70, 16);
        MixByte(Field<uint32_t>(self, 0) == 0x00AC5488 ? 1 : 0);
        MixByte(Field<uint32_t>(self, 4) == 0x00AC5454 ? 1 : 0);
        MixBytes(space + 16 + 8, 0x1C);
        MixSystem(self);
        Game<SThis>(addr::GZFontSystem_Destruct)(self, nullptr);
        MixBytes(space, 16);
        MixBytes(space + 16 + 0x70, 16);
        MixByte(Field<uint32_t>(self, 0) == 0x00AC5488 ? 1 : 0);
    }
    {
        void* const t = NewSystem();
        MixByte(Game<SOne>(addr::GZFontSystem_DeletingDestruct)(t, nullptr, 0) == U(t) ? 1 : 0);
        Game<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(t);
    }
    FtStage("system");
}

void DriveSystemFaces()
{
    const auto fileType = Game<SOne>(addr::GZFontSystem_FileType);
    const auto addFolder = Game<SOne>(addr::GZFontSystem_AddFontFolder);
    const auto makeFont = Game<SFour>(addr::GZFontSystem_MakeFont);
    const std::vector<std::string> paths = FontPaths();
    const std::string ours = TestFolder();
    char windows[MAX_PATH] = {};
    GetWindowsDirectoryA(windows, MAX_PATH);
    const std::string system = std::string(windows) + "\\Fonts\\";

    // Files of our own for the types: by extension in any case, by their
    // first bytes, too short, empty, missing.
    {
        Random random{0x5EED0F06};
        std::vector<uint8_t> junk(64);
        for (uint8_t& v : junk)
            v = random.Byte();
        std::vector<uint8_t> ps = junk, tt = junk, ttcf = junk, late = junk;
        std::memcpy(ps.data() + 11, "%!PS", 4);
        std::memcpy(tt.data(), "\0\1\0\0", 4);
        std::memcpy(ttcf.data(), "ttcf", 4);
        std::memcpy(late.data() + 40, "%!PS", 4);
        WriteFile(ours + "ps.dat", ps);
        WriteFile(ours + "tt.dat", tt);
        WriteFile(ours + "ttcf.dat", ttcf);
        WriteFile(ours + "late.dat", late);
        WriteFile(ours + "junk.dat", junk);
        WriteFile(ours + "upper.TTF", junk);
        WriteFile(ours + "mixed.PfB", junk);
        WriteFile(ours + "x.OtF", junk);
        WriteFile(ours + "x.ttc", junk);
        WriteFile(ours + "empty.dat", {});
        WriteFile(ours + "three.dat", {'%', '!', 'P'});
        WriteFile(ours + "made-arial.ttf", ReadWhole(system + "arial.ttf"));
    }
    std::vector<std::string> typed = paths;
    for (const char* name : {"ps.dat", "tt.dat", "ttcf.dat", "late.dat", "junk.dat", "upper.TTF", "mixed.PfB", "x.OtF",
                             "x.ttc", "empty.dat", "three.dat", "missing.dat", "noextension"})
        typed.push_back(ours + name);
    void* const s = NewSystem();
    Game<SThis>(addr::GZFontSystem_Init)(s, nullptr);
    std::string types;
    for (const std::string& p : typed) {
        GameString path = MakeString(p.c_str());
        const uint32_t type = fileType(s, nullptr, U(&path));
        Mix(type);
        types += static_cast<char>('0' + (type < 10 ? type : 9));
        FreeString(path);
    }

    // Every file of FontPaths, three of them again, then names that are
    // only in Windows's Fonts folder or nowhere.
    AddFaces(s, paths, true);
    AddFaces(s, {paths[0], paths[3], paths[19]}, true);
    AddFaces(s, {"arial.ttf", "times.ttf", "nosuch.ttf", "", ours + "ps.dat"}, true);
    MixSystem(s);

    // Folders: the game's (all there already), on a new system the game's
    // with a separator, ours, one that is not there; Windows's is left out
    // (its faces differ from machine to machine).
    uint32_t scanned = 0;
    const auto folder = [&](void* on, const std::string& name) {
        GameString text = MakeString(name.c_str());
        const uint32_t n = addFolder(on, nullptr, U(&text));
        Mix(n);
        scanned += n;
        FreeString(text);
    };
    folder(s, GameFonts());
    void* const t = NewSystem();
    Game<SThis>(addr::GZFontSystem_Init)(t, nullptr);
    folder(t, GameFonts() + "\\");
    folder(t, ours);
    folder(t, ours + "nosuch\\");
    MixSystem(t);

    // ListFaces into a buffer between margins.
    {
        const uint32_t faces = Field<uint32_t>(s, 0x44);
        for (const uint32_t count : {0u, 1u, 3u, faces, faces + 4}) {
            std::vector<uint8_t> buffer(32 + (std::size_t{count} + 1) * 0x88, 0xA5);
            Mix(Game<STwo>(addr::GZFontSystem_ListFaces)(s, nullptr, U(buffer.data() + 16), count));
            MixBytes(buffer.data(), buffer.size());
        }
    }

    // HasFace by name and style.
    for (const char* name : {"arial", "ARIAL", "Arial MT", "arial mt", "times new roman", "Courier LD", "nosuch", ""}) {
        GameString text = MakeString(name);
        for (const uint32_t style : {0u, 1u, 2u, 3u, 4u, 0xFFu})
            MixByte(static_cast<uint8_t>(
                Game<SThree>(addr::GZFontSystem_HasFace)(s, nullptr, U(&text), 0x77, style)));
        FreeString(text);
    }

    // Fonts made on t (not kept there) added to s: to a face of their path,
    // the same size again, another size, none; one of a path s does not
    // have, which makes a face.
    Seen seen;
    const auto made = [&](const std::string& p, uint32_t size) {
        GameString path = MakeString(p.c_str());
        void* const f = reinterpret_cast<void*>(
            static_cast<uintptr_t>(makeFont(t, nullptr, U(&path), size, 0, 0)));
        FreeString(path);
        MixFont(f, seen);
        return f;
    };
    void* const f1 = made(paths[3], 12);
    void* const f2 = made(paths[3], 12);
    void* const f3 = made(paths[3], 15);
    void* const f4 = made(ours + "made-arial.ttf", 11);
    const auto addFont = Game<SOne>(addr::GZFontSystem_AddFont);
    for (void* f : {f1, f2, f3, static_cast<void*>(nullptr), f4, f4})
        MixByte(static_cast<uint8_t>(addFont(s, nullptr, U(f))));
    MixSystem(s, "made-");

    // ListFonts between margins, the fonts by index.
    for (const uint32_t count : {0u, 1u, 2u, 10u}) {
        uint32_t out[16];
        for (uint32_t& v : out)
            v = 0xA5A5A5A5;
        Mix(Game<STwo>(addr::GZFontSystem_ListFonts)(s, nullptr, U(out + 1), count));
        for (const uint32_t v : out)
            Mix(v == 0xA5A5A5A5 ? 0xA5 : seen.Index(reinterpret_cast<void*>(static_cast<uintptr_t>(v))));
    }

    // ReleaseFont: f1 has the face's reference and ours, so it is released
    // twice and taken out (ours with it); f2 is in no list.
    const auto releaseFont = Game<SOne>(addr::GZFontSystem_ReleaseFont);
    Mix(releaseFont(s, nullptr, U(f1)));
    Mix(releaseFont(s, nullptr, U(f2)));
    MixSystem(s, "made-");

    // RemoveFace: every Arial MT face, f3's reference with it; one that is
    // not there.
    for (const char* name : {"Arial MT", "nosuch", "ARIAL MT"}) {
        GameString text = MakeString(name);
        MixByte(static_cast<uint8_t>(Game<SOne>(addr::GZFontSystem_RemoveFace)(s, nullptr, U(&text))));
        FreeString(text);
    }
    MixSystem(s, "made-");
    for (void* f : {f2, f3, f4})
        Mix(Virtual<uint32_t>(f, 0x8));
    Mix(ReleaseSystem(s));
    Mix(ReleaseSystem(t));
    log::Write("autotest: fonts: system types %s, %u from folders", types.c_str(), scanned);
    FtStage("faces");
}

void DriveSystemAliases()
{
    void* const s = NewSystem();
    Game<SThis>(addr::GZFontSystem_Init)(s, nullptr);
    std::vector<std::string> paths = FontPaths();
    paths.resize(23);
    AddFaces(s, paths, false);
    struct Alias {
        const char* alias;
        const char* face;
    };
    const Alias aliases[] = {{"Sans", "nosuch face"}, {"Sans", "Arial"},      {"Sans", "Arial MT"},
                             {"Serif", "Times New Roman"}, {"Mono", "Courier LD"}, {"Mono", "Courier New"},
                             {"sans", "x"},           {"Empty", ""},         {"Sans", "Arial"}};
    for (const Alias& a : aliases) {
        GameString alias = MakeString(a.alias), face = MakeString(a.face);
        MixByte(static_cast<uint8_t>(
            Game<STwo>(addr::GZFontSystem_AddAlias)(s, nullptr, U(&alias), U(&face))));
        FreeString(alias);
        FreeString(face);
    }
    MixSystem(s);

    struct Known {
        const char* alias;
        uint32_t names;
    };
    for (const Known& k : {Known{"Sans", 4}, Known{"Serif", 1}, Known{"Mono", 2}, Known{"sans", 1}, Known{"Empty", 1},
                           Known{"nosuch", 0}}) {
        GameString alias = MakeString(k.alias);
        for (uint32_t count = 0; count <= 5; ++count) {
            // A string for each of the alias's names, null past them (the
            // game would give those what lies past its vector).
            GameString outs[5];
            void* pointers[5] = {};
            for (uint32_t i = 0; i < 5; ++i) {
                outs[i] = MakeString("before");
                if (i < k.names && !(count == 3 && i == 1))
                    pointers[i] = &outs[i];
            }
            Mix(Game<SThree>(addr::GZFontSystem_GetAliasFaces)(s, nullptr, U(&alias), U(pointers), count));
            for (GameString& o : outs) {
                MixString(o);
                FreeString(o);
            }
        }
        GameString out = MakeString("unchanged");
        MixByte(static_cast<uint8_t>(
            Game<STwo>(addr::GZFontSystem_ResolveAlias)(s, nullptr, U(&alias), U(&out))));
        MixString(out);
        FreeString(out);
        FreeString(alias);
    }
    Mix(ReleaseSystem(s));
    FtStage("aliases");
}

void DriveSystemGetFont()
{
    const auto getFont = Game<SFive>(addr::GZFontSystem_GetFont);
    const auto makeFont = Game<SFour>(addr::GZFontSystem_MakeFont);
    const auto range = Game<SThree>(addr::GZFontSystem_SetMonoRange);
    void* const s = NewSystem();
    Game<SThis>(addr::GZFontSystem_Init)(s, nullptr);
    std::vector<std::string> paths = FontPaths();
    paths.resize(23);
    AddFaces(s, paths, false);
    for (const auto& [alias, face] : {std::pair<const char*, const char*>{"Sans", "Arial MT"}, {"Sans", "Arial"},
                                      {"Serif", "Times New Roman"}, {"Gothic", "HG-Gothic"},
                                      {"fixed", "courier new"}, {"Arial MT", "Times New Roman"}}) {
        GameString a = MakeString(alias), f = MakeString(face);
        Game<STwo>(addr::GZFontSystem_AddAlias)(s, nullptr, U(&a), U(&f));
        FreeString(a);
        FreeString(f);
    }
    // The sizes in mode 1: the system's, two faces' (the second of one kept
    // as the first), one removed, one that was never there removed.
    const auto setRange = [&](int32_t low, int32_t high, const char* face) {
        GameString text = MakeString(face ? face : "");
        range(s, nullptr, static_cast<uint32_t>(low), static_cast<uint32_t>(high), face ? U(&text) : 0);
        FreeString(text);
    };
    setRange(9, 11, nullptr);
    setRange(14, 18, "Times New Roman");
    setRange(20, 30, "Times New Roman");
    setRange(5, 5, "Arial");
    setRange(1, 2, "Courier New");
    setRange(3, 1, "Courier New");
    setRange(-4, 40, "HG-Gothic");
    MixSystem(s);

    Seen seen;
    std::vector<void*> held;
    std::string many;
    for (int i = 0; i < 40; ++i)
        many += "a,";
    many += "Arial";
    const std::string lists[] = {"Arial MT", "arial mt,times new roman", "Unknown,Courier New", "", ",", "Sans",
                                 "serif,sans", "Gothic", "nosuch", "Times New Roman,Arial MT,Courier LD,HG-Gothic",
                                 "FIXED", many};
    uint32_t made = 0, none = 0;
    for (const std::string& list : lists) {
        GameString text = MakeString(list.c_str());
        for (const uint32_t kind : {0u, 2u})
            for (const uint32_t size : {0u, 10u, 16u})
                for (const uint32_t style : {0u, 1u, 3u, 0x7Eu})
                    for (const uint32_t reuse : {0u, 1u}) {
                        void* const f = reinterpret_cast<void*>(static_cast<uintptr_t>(
                            getFont(s, nullptr, U(&text), kind, size, style, reuse)));
                        MixFont(f, seen);
                        if (f) {
                            held.push_back(f);
                            ++made;
                        } else {
                            ++none;
                        }
                    }
        FreeString(text);
    }
    MixSystem(s);

    // Slot 9: by kind, its colours set.
    for (const uint32_t kind : {0u, 1u, 2u})
        for (const uint32_t style : {0u, 2u})
            for (const uint32_t reuse : {0u, 1u}) {
                void* const f = reinterpret_cast<void*>(static_cast<uintptr_t>(
                    Game<SFive>(addr::GZFontSystem_GetFontFor)(s, nullptr, kind, 12, style, 0x00123456, reuse)));
                MixFont(f, seen);
                if (f)
                    held.push_back(f);
            }

    // Slot 10 on every face's path, a name only in Windows's Fonts folder,
    // a file that does not load and one that is not there.
    std::vector<std::string> keys;
    {
        const uint32_t header = Field<uint32_t>(static_cast<uint8_t*>(s) + 0x40, 0);
        for (uint32_t node = Field<uint32_t>(At32(header), 8); node != header; node = TreeNext(node))
            keys.push_back(TextAt(At32(node) + 0x10));
    }
    keys.push_back("arial.ttf");
    keys.push_back(TestFolder() + "junk.ttf");
    keys.push_back(TestFolder() + "missing.ttf");
    for (const std::string& key : keys) {
        GameString path = MakeString(key.c_str());
        for (const uint32_t size : {0u, 13u})
            for (const uint32_t style : {0u, 2u})
                for (const uint32_t reuse : {0u, 1u}) {
                    void* const f = reinterpret_cast<void*>(
                        static_cast<uintptr_t>(makeFont(s, nullptr, U(&path), size, style, reuse)));
                    MixFont(f, seen);
                    if (f)
                        held.push_back(f);
                }
        FreeString(path);
    }
    MixSystem(s);
    for (void* f : held)
        Virtual<uint32_t>(f, 0x8);
    Mix(ReleaseSystem(s));

    log::Write("autotest: fonts: system %u fonts got, %u none, %zu different", made, none, seen.fonts.size());

    // The game's own system, without reuse (nothing is kept there). Its
    // fonts may take the places of those just freed: counted afresh.
    if (void* const fonts = Game<void*(__cdecl*)()>(addr::SC4_Singleton913D22)()) {
        Seen fresh;
        std::vector<void*> got;
        for (const char* list : {"Arial MT", "nosuch,Arial MT", ""}) {
            GameString text = MakeString(list);
            void* const f =
                reinterpret_cast<void*>(static_cast<uintptr_t>(getFont(fonts, nullptr, U(&text), 0, 12, 0, 0)));
            MixFont(f, fresh);
            if (f)
                got.push_back(f);
            FreeString(text);
        }
        for (void* f : got)
            Virtual<uint32_t>(f, 0x8);
    }
    FtStage("getfont");
}

// ====================================================================================
// The font system's tables (f2b)

// Maps, lists and vectors of our own, made and changed by the 53 functions
// at their addresses (and the game's strings, vectors of strings and
// allocator). A tree goes into the hash whole: its count, its root and last
// node, then in order each node's colour, the places of its parent and
// children (by their order, never an address), its key and its value.

namespace tables {
constexpr std::uintptr_t kSetDrawMode = 0x00893D87, kSetServiceId = 0x00894F6D, kFill = 0x00893DB8,
                         kDistance = 0x00893DEC, kListNode = 0x00893E0F, kLowerBound = 0x00893FF0,
                         kUpperBound = 0x00894028, kBytesAllocate = 0x00894060, kListPushBack = 0x0089408D,
                         kBytesConstruct = 0x008940A7, kListInsertRange = 0x008940CD, kListCopy = 0x008940EB,
                         kEqualRange = 0x0089411A, kFaceConstruct = 0x00894144, kFaceDestruct = 0x0089415F,
                         kFacePairDestruct = 0x00894172, kAliasPairDestruct = 0x00894185,
                         kRangePairConstruct = 0x00894585, kFaceCopy = 0x008945AA, kFacesEraseNode = 0x008945E1,
                         kRangesEraseNode = 0x0089461C, kFacePairCopy = 0x008946F6, kAliasPairCopy = 0x0089471B,
                         kFacePairConstruct = 0x00894740, kAliasPairConstruct = 0x00894762,
                         kFacesClear = 0x00894784, kAliasesClear = 0x008947AD, kRangesClear = 0x008947D6,
                         kRangesEraseRange = 0x008947FF, kRangesNode = 0x00894843, kFacesDestruct = 0x00894DC7,
                         kAliasesDestruct = 0x00894DDE, kRangesDestruct = 0x00894DF5, kRangesEraseKey = 0x00894E0C,
                         kRangesInsertAt = 0x00894E55, kFacesNode = 0x00894EEA, kAliasesNode = 0x00894F0B,
                         kFacesConstruct = 0x0089505A, kAliasesConstruct = 0x00895073,
                         kRangesConstruct = 0x0089508C, kRangesInsert = 0x008950A5, kFacesInsertAt = 0x00895136,
                         kAliasesInsertAt = 0x008951CB, kFacesInsert = 0x008953A9, kAliasesInsert = 0x0089543A;
// The game's: an empty list, a list freed, a string assigned, a vector of
// strings' push_back and destructor, a string destroyed.
constexpr std::uintptr_t kListMake = 0x009A88C4, kListFree = 0x0096EDBC, kStringAssign = 0x00424380,
                         kVectorPushBack = 0x004522F0, kVectorFree = 0x00419140, kStringDestroy = 0x00414250;
} // namespace tables

using SCdecl3 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);

// A map: the header, the count, the comparator.
struct Tree {
    uint32_t header = 0, count = 0, compare = 0;
};

template <typename ValueFn>
void MixTree(const Tree& tree, ValueFn&& value)
{
    Mix(tree.count);
    const uint32_t header = tree.header;
    std::vector<uint32_t> order;
    for (uint32_t node = Field<uint32_t>(At32(header), 8); node != header; node = TreeNext(node))
        order.push_back(node);
    const auto place = [&](uint32_t node) -> uint32_t {
        if (!node)
            return 0xFFFFFFFF;
        if (node == header)
            return 0xFFFFFFFE;
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i] == node)
                return static_cast<uint32_t>(i);
        return 0xFFFFFFFD;
    };
    Mix(static_cast<uint32_t>(order.size()));
    MixByte(Field<uint8_t>(At32(header), 0));
    Mix(place(Field<uint32_t>(At32(header), 4)));
    Mix(place(Field<uint32_t>(At32(header), 0xC)));
    for (const uint32_t node : order) {
        const uint8_t* const n = At32(node);
        MixByte(Field<uint8_t>(n, 0));
        Mix(place(Field<uint32_t>(n, 4)));
        Mix(place(Field<uint32_t>(n, 8)));
        Mix(place(Field<uint32_t>(n, 0xC)));
        MixTextAt(n + 0x10);
        value(n);
    }
}

// A list of word pairs: each pair, and whether its node's previous is the
// one before it.
void MixList(uint32_t list)
{
    const uint32_t header = Field<uint32_t>(At32(list), 0);
    uint32_t before = header;
    uint32_t count = 0;
    for (uint32_t e = Field<uint32_t>(At32(header), 0); e != header; e = Field<uint32_t>(At32(e), 0)) {
        Mix(Field<uint32_t>(At32(e), 8));
        Mix(Field<uint32_t>(At32(e), 0xC));
        MixByte(Field<uint32_t>(At32(e), 4) == before ? 1 : 0);
        before = e;
        ++count;
    }
    Mix(count);
    MixByte(Field<uint32_t>(At32(header), 4) == before ? 1 : 0);
}

// A face (0x24 bytes): its fonts, its name, its three words.
void MixFace(const uint8_t* face)
{
    MixList(U(face));
    MixTextAt(face + 4);
    Mix(Field<uint32_t>(face, 0x18));
    Mix(Field<uint32_t>(face, 0x1C));
    Mix(Field<uint32_t>(face, 0x20));
}

// A vector of strings.
void MixNames(const uint8_t* vector)
{
    const uint8_t* const begin = Field<const uint8_t*>(vector, 0);
    const uint8_t* const end = Field<const uint8_t*>(vector, 4);
    Mix(static_cast<uint32_t>(end - begin));
    for (const uint8_t* name = begin; name < end; name += 0x14)
        MixTextAt(name);
}

// An insert's result: the byte, the three bytes after it (neither side
// writes them) and the node's key.
void MixInserted(const uint32_t (&result)[2])
{
    Mix(result[1]);
    MixTextAt(At32(result[0]) + 0x10);
}

void FreeNode(uint32_t node, std::uintptr_t destroyValue)
{
    Game<SThis>(destroyValue)(At32(node) + 0x10, nullptr);
    Game<void(__cdecl*)(void*)>(addr::SC4_Free)(At32(node));
}

void DriveTablesFaces(Random& random)
{
    using namespace tables;
    Tree faces;
    Game<SThis>(kFacesConstruct)(&faces, nullptr);
    const auto value = [](const uint8_t* node) { MixFace(node + 0x24); };
    const auto insert = [&](const char* text, uint32_t words) {
        GameString key = MakeString(text);
        uint8_t face[0x24];
        Game<SThis>(kFaceConstruct)(face, nullptr);
        GameString name = MakeString(words % 3 ? "Arial" : "");
        Game<SOne>(kStringAssign)(face + 4, nullptr, U(&name));
        FreeString(name);
        std::memcpy(face + 0x18, &words, 4);
        const uint32_t kind = words * 7, type = random.Next();
        std::memcpy(face + 0x1C, &kind, 4);
        std::memcpy(face + 0x20, &type, 4);
        for (uint32_t j = 0; j < words % 4; ++j) {
            const uint32_t pair[2] = {words, j};
            Game<SOne>(kListPushBack)(face, nullptr, U(pair));
        }
        uint8_t pair[0x38];
        Game<STwo>(kFacePairConstruct)(pair, nullptr, U(&key), U(face));
        uint32_t result[2] = {0, 0xA5A5A5A5};
        Game<STwo>(kFacesInsert)(&faces, nullptr, U(result), U(pair));
        MixInserted(result);
        Game<SThis>(kFacePairDestruct)(pair, nullptr);
        Game<SThis>(kFaceDestruct)(face, nullptr);
        FreeString(key);
    };
    char text[24];
    for (uint32_t i = 0; i < 64; ++i) {
        std::snprintf(text, sizeof text, "face %02d", random.Range(0, 40));
        insert(text, i);
    }
    MixTree(faces, value);

    // The root's face and pair copied, and a node made of the pair.
    const uint32_t root = Field<uint32_t>(At32(faces.header), 4);
    uint8_t face[0x24];
    Game<SOne>(kFaceCopy)(face, nullptr, root + 0x24);
    MixFace(face);
    Game<SThis>(kFaceDestruct)(face, nullptr);
    uint8_t pair[0x38];
    Game<SOne>(kFacePairCopy)(pair, nullptr, root + 0x10);
    MixTextAt(pair);
    MixFace(pair + 0x14);
    const uint32_t node = Game<SOne>(kFacesNode)(&faces, nullptr, U(pair));
    MixTextAt(At32(node) + 0x10);
    MixFace(At32(node) + 0x24);
    FreeNode(node, kFacePairDestruct);

    // Under the last node on its right, and under the first on its left with
    // x set.
    uint32_t result[2] = {0, 0};
    GameString last = MakeString("zz last");
    Game<SOne>(kStringAssign)(pair, nullptr, U(&last));
    Game<SFive>(kFacesInsertAt)(&faces, nullptr, U(result), 0, Field<uint32_t>(At32(faces.header), 0xC), U(pair), 1);
    GameString first = MakeString("  first");
    Game<SOne>(kStringAssign)(pair, nullptr, U(&first));
    Game<SFive>(kFacesInsertAt)(&faces, nullptr, U(result), 1, Field<uint32_t>(At32(faces.header), 8), U(pair), 0);
    FreeString(last);
    FreeString(first);
    Game<SThis>(kFacePairDestruct)(pair, nullptr);
    MixTree(faces, value);

    // Every third key's node erased.
    for (int k = 0; k <= 40; k += 3) {
        std::snprintf(text, sizeof text, "face %02d", k);
        GameString key = MakeString(text);
        const uint32_t found = Game<SOne>(kLowerBound)(&faces, nullptr, U(&key));
        if (found != faces.header && TextAt(At32(found) + 0x10) == text)
            Game<SOne>(kFacesEraseNode)(&faces, nullptr, found);
        FreeString(key);
    }
    MixTree(faces, value);
    Game<SThis>(kFacesClear)(&faces, nullptr);
    MixTree(faces, value);
    for (uint32_t i = 0; i < 5; ++i)
        insert(i % 2 ? "again" : "face 07", i);
    MixTree(faces, value);
    Game<SThis>(kFacesDestruct)(&faces, nullptr);
}

void DriveTablesAliases(Random& random)
{
    using namespace tables;
    Tree aliases;
    Game<SThis>(kAliasesConstruct)(&aliases, nullptr);
    const auto value = [](const uint8_t* node) { MixNames(node + 0x24); };
    const auto insert = [&](const char* text, uint32_t names) {
        GameString key = MakeString(text);
        uint32_t vector[3] = {};
        char name[24];
        for (uint32_t j = 0; j < names % 5; ++j) {
            std::snprintf(name, sizeof name, "name %u", random.Next() % 9);
            GameString s = MakeString(name);
            Game<SOne>(kVectorPushBack)(vector, nullptr, U(&s));
            FreeString(s);
        }
        uint8_t pair[0x20];
        Game<STwo>(kAliasPairConstruct)(pair, nullptr, U(&key), U(vector));
        uint32_t result[2] = {0, 0x5A5A5A5A};
        Game<STwo>(kAliasesInsert)(&aliases, nullptr, U(result), U(pair));
        MixInserted(result);
        Game<SThis>(kAliasPairDestruct)(pair, nullptr);
        Game<SThis>(kVectorFree)(vector, nullptr);
        FreeString(key);
    };
    char text[24];
    for (uint32_t i = 0; i < 48; ++i) {
        std::snprintf(text, sizeof text, "alias %u", random.Next() % 30);
        insert(text, i);
    }
    MixTree(aliases, value);

    const uint32_t root = Field<uint32_t>(At32(aliases.header), 4);
    uint8_t pair[0x20];
    Game<SOne>(kAliasPairCopy)(pair, nullptr, root + 0x10);
    MixTextAt(pair);
    MixNames(pair + 0x14);
    const uint32_t node = Game<SOne>(kAliasesNode)(&aliases, nullptr, U(pair));
    MixTextAt(At32(node) + 0x10);
    MixNames(At32(node) + 0x24);
    FreeNode(node, kAliasPairDestruct);
    uint32_t result[2] = {0, 0};
    GameString last = MakeString("zz last");
    Game<SOne>(kStringAssign)(pair, nullptr, U(&last));
    Game<SFive>(kAliasesInsertAt)(&aliases, nullptr, U(result), 0, Field<uint32_t>(At32(aliases.header), 0xC), U(pair),
                                   1);
    FreeString(last);
    Game<SThis>(kAliasPairDestruct)(pair, nullptr);
    MixTree(aliases, value);
    Game<SThis>(kAliasesClear)(&aliases, nullptr);
    MixTree(aliases, value);
    for (uint32_t i = 0; i < 6; ++i)
        insert(i % 2 ? "alias 1" : "alias 2", i);
    MixTree(aliases, value);
    Game<SThis>(kAliasesDestruct)(&aliases, nullptr);
}

void DriveTablesRanges(Random& random)
{
    using namespace tables;
    Tree ranges;
    Game<SThis>(kRangesConstruct)(&ranges, nullptr);
    const auto value = [](const uint8_t* node) {
        Mix(Field<uint32_t>(node, 0x24));
        Mix(Field<uint32_t>(node, 0x28));
    };
    const auto insert = [&](const char* text, uint32_t low) {
        GameString key = MakeString(text);
        const uint32_t words[2] = {low, random.Next()};
        uint8_t pair[0x1C];
        Game<STwo>(kRangePairConstruct)(pair, nullptr, U(&key), U(words));
        uint32_t result[2] = {0, 0xC3C3C3C3};
        Game<STwo>(kRangesInsert)(&ranges, nullptr, U(result), U(pair));
        MixInserted(result);
        Game<SThis>(kStringDestroy)(pair, nullptr);
        FreeString(key);
    };
    const auto place = [&](uint32_t node) {
        if (node == ranges.header)
            Mix(0xFFFFFFFE);
        else
            MixTextAt(At32(node) + 0x10);
    };
    char text[24];
    for (uint32_t i = 0; i < 80; ++i) {
        std::snprintf(text, sizeof text, "r%02u", random.Next() % 50);
        insert(text, i);
    }
    MixTree(ranges, value);

    // The bounds, the equal range and its distance for keys there and not,
    // before the first and after the last.
    for (const char* k : {"", "r", "r00", "r07", "r10", "r255", "r33", "r49", "r5", "s"}) {
        GameString key = MakeString(k);
        place(Game<SOne>(kLowerBound)(&ranges, nullptr, U(&key)));
        place(Game<SOne>(kUpperBound)(&ranges, nullptr, U(&key)));
        uint32_t range[2] = {};
        Mix(Game<STwo>(kEqualRange)(&ranges, nullptr, U(range), U(&key)) == U(range) ? 1 : 0);
        place(range[0]);
        place(range[1]);
        uint8_t tag = 0;
        Mix(Game<SCdecl3>(kDistance)(U(&range[0]), U(&range[1]), U(&tag)));
        FreeString(key);
    }

    // Keys erased (there and not), a range of them, a node, all of them.
    for (const char* k : {"r03", "r03", "r20", "zz", "r41"}) {
        GameString key = MakeString(k);
        Mix(Game<SOne>(kRangesEraseKey)(&ranges, nullptr, U(&key)));
        FreeString(key);
    }
    MixTree(ranges, value);
    {
        GameString from = MakeString("r10"), to = MakeString("r30");
        const uint32_t first = Game<SOne>(kLowerBound)(&ranges, nullptr, U(&from));
        const uint32_t last = Game<SOne>(kLowerBound)(&ranges, nullptr, U(&to));
        Game<STwo>(kRangesEraseRange)(&ranges, nullptr, first, last);
        FreeString(from);
        FreeString(to);
    }
    MixTree(ranges, value);
    Game<SOne>(kRangesEraseNode)(&ranges, nullptr, Field<uint32_t>(At32(ranges.header), 8));
    MixTree(ranges, value);

    // A node made, and one put under the last on its right.
    GameString key = MakeString("zz last");
    const uint32_t words[2] = {7, 9};
    uint8_t pair[0x1C];
    Game<STwo>(kRangePairConstruct)(pair, nullptr, U(&key), U(words));
    const uint32_t node = Game<SOne>(kRangesNode)(&ranges, nullptr, U(pair));
    MixTextAt(At32(node) + 0x10);
    value(At32(node));
    FreeNode(node, kStringDestroy);
    uint32_t result[2] = {0, 0};
    Game<SFive>(kRangesInsertAt)(&ranges, nullptr, U(result), 0, Field<uint32_t>(At32(ranges.header), 0xC), U(pair), 1);
    Game<SThis>(kStringDestroy)(pair, nullptr);
    FreeString(key);
    MixTree(ranges, value);

    Game<STwo>(kRangesEraseRange)(&ranges, nullptr, Field<uint32_t>(At32(ranges.header), 8), ranges.header);
    MixTree(ranges, value);
    for (uint32_t i = 0; i < 9; ++i) {
        std::snprintf(text, sizeof text, "r%u", i * 7 % 5);
        insert(text, i);
    }
    MixTree(ranges, value);
    Game<SThis>(kRangesClear)(&ranges, nullptr);
    MixTree(ranges, value);
    insert("one", 1);
    Game<SThis>(kRangesDestruct)(&ranges, nullptr);
}

void DriveTablesRest()
{
    using namespace tables;
    // The byte vector: made of counts and bytes, and filled into a buffer
    // between margins (counts below 0, 0 and above).
    const uint32_t made[][2] = {{0, 0}, {1, 'a'}, {5, 0xFF}, {33, 'x'}, {2, 0x180}};
    for (const auto& m : made) {
        uint32_t v[3] = {0xA5A5A5A5, 0xA5A5A5A5, 0xA5A5A5A5};
        Mix(Game<STwo>(kBytesConstruct)(v, nullptr, m[0], m[1]) == U(v) ? 1 : 0);
        Mix(v[1] - v[0]);
        Mix(v[2] - v[0]);
        MixBytes(At32(v[0]), v[1] - v[0] + 1);
        const uint32_t before = v[0];
        const uint32_t end = Game<SThree>(kBytesAllocate)(v, nullptr, m[0] + 2, m[1] + 1, 0);
        Mix(end - v[0]);
        MixBytes(At32(v[0]), v[1] - v[0] + 1);
        Game<void(__cdecl*)(void*)>(addr::SC4_Free)(At32(before));
        Game<void(__cdecl*)(void*)>(addr::SC4_Free)(At32(v[0]));
    }
    uint8_t buffer[64];
    for (const int32_t count : {-3, 0, 1, 20}) {
        std::memset(buffer, 0xA5, sizeof buffer);
        const uint8_t byte = static_cast<uint8_t>('q' + count);
        Mix(Game<SCdecl3>(kFill)(U(buffer + 8), static_cast<uint32_t>(count), U(&byte)) - U(buffer));
        MixBytes(buffer, sizeof buffer);
    }

    // Lists of word pairs: pushed, copied, a range inserted, a node.
    uint32_t list = 0, copy = 0;
    uint8_t allocator = 0;
    Game<SOne>(kListMake)(&list, nullptr, U(&allocator));
    for (uint32_t i = 0; i < 9; ++i) {
        const uint32_t pair[2] = {i, i * i};
        Game<SOne>(kListPushBack)(&list, nullptr, U(pair));
    }
    MixList(U(&list));
    Game<SOne>(kListCopy)(&copy, nullptr, U(&list));
    MixList(U(&copy));
    const uint32_t second = Field<uint32_t>(At32(Field<uint32_t>(At32(copy), 0)), 0);
    const uint32_t header = list;
    Game<SThree>(kListInsertRange)(&copy, nullptr, second, Field<uint32_t>(At32(header), 0), header);
    Game<SThree>(kListInsertRange)(&copy, nullptr, second, header, header);
    MixList(U(&copy));
    const uint32_t pair[2] = {0x12345678, 0x9ABCDEF0};
    const uint32_t node = Game<SOne>(kListNode)(&list, nullptr, U(pair));
    Mix(Field<uint32_t>(At32(node), 8));
    Mix(Field<uint32_t>(At32(node), 0xC));
    Game<void(__cdecl*)(void*)>(addr::SC4_Free)(At32(node));
    Game<SThis>(kListFree)(&copy, nullptr);
    Game<SThis>(kListFree)(&list, nullptr);

    // The two setters.
    uint32_t object[8];
    for (uint32_t i = 0; i < 8; ++i)
        object[i] = 0x11111111u * i;
    Mix(Game<SOne>(kSetDrawMode)(object, nullptr, 0xCAFE));
    Mix(Game<SOne>(kSetServiceId)(object, nullptr, 0xBEEF));
    MixBytes(reinterpret_cast<const uint8_t*>(object), sizeof object);
}

void DriveSystemTables()
{
    Random random{0x0F2B};
    DriveTablesFaces(random);
    FtStage("stl-faces");
    DriveTablesAliases(random);
    FtStage("stl-aliases");
    DriveTablesRanges(random);
    FtStage("stl-ranges");
    DriveTablesRest();
    FtStage("stl");
}

void DriveSystem()
{
    DriveSystemObject();
    DriveSystemFaces();
    DriveSystemAliases();
    DriveSystemGetFont();
    DriveSystemTables();
}

// ====================================================================================
// The font's object, properties and blitter (f3a)

// Fonts of our own, made by the constructor at its address in the game's
// allocator's memory or between margins, and fonts the font system made
// and loaded; the functions called by address (slots 17 and 19, which ours
// take in the vtable, through it). A font goes into the hash by its fields,
// texts and levels and whether its pointers are set, never where they are.

constexpr uint32_t kFontBytes = 0x15C;
constexpr uint32_t kCacheBytes = 0x2C;

using FFloat = uint32_t(__fastcall*)(void*, void*, float);
using FFloat2 = uint32_t(__fastcall*)(void*, void*, float, uint32_t);

void* NewFont()
{
    void* const f = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(kFontBytes);
    Game<SThis>(addr::GZFont_Construct)(f, nullptr);
    return f;
}

// A glyph cache's sheet: whether it has a block and pixels, then its width,
// height, pitch and next free column.
void MixCache(const uint8_t* cache)
{
    MixByte(Field<uint32_t>(cache, 0) ? 1 : 0);
    MixByte(Field<uint32_t>(cache, 4) ? 1 : 0);
    for (std::size_t word = 8; word < 0x18; word += 4)
        Mix(Field<uint32_t>(cache, word));
}

void MixFontFields(void* font)
{
    const uint8_t* const b = static_cast<const uint8_t*>(font);
    MixByte(Field<uint32_t>(b, 0) == 0x00AC55D8 ? 1 : 0);
    MixByte(b[4]);
    Mix(Field<uint32_t>(b, 8));
    MixTextAt(b + 0x0C);
    MixTextAt(b + 0x20);
    for (std::size_t field = 0x34; field < 0x90; field += 4) {
        if (field >= 0x5C && field < 0x68)
            continue;
        Mix(field == 0x8C ? (Field<uint32_t>(b, field) ? 1u : 0u) : Field<uint32_t>(b, field));
    }
    const uint8_t* const levels = Field<const uint8_t*>(b, 0x5C);
    const uint32_t count = levels ? static_cast<uint32_t>(Field<const uint8_t*>(b, 0x60) - levels) : 0;
    Mix(count);
    if (count)
        MixBytes(levels, count);
    Mix(Field<uint32_t>(b, 0xE4));
    Mix(Field<uint32_t>(b, 0xE8));
    MixByte(Field<uint32_t>(b, 0xEC) ? 1 : 0);
    Mix(Field<uint32_t>(b, 0xFC));
    MixCache(b + 0x104);
    MixCache(b + 0x130);
}

// A blitter of our own that notes every call: AddRef and Release count,
// slots 3 and 4 (given and taken a font), 5 and 6 (Begin and End: the
// font, the picture, the mode).
struct MockBlitter {
    void** vtable;
    int32_t refs;
    uint32_t id;
    void* font;
};

void NoteBlit(MockBlitter* b, uint32_t slot, void* font, uint32_t picture = 0, uint32_t mode = 0)
{
    Mix(b->id << 8 | slot);
    MixByte(font == b->font ? 1 : font ? 2 : 0);
    Mix(picture);
    Mix(mode);
}

uint32_t __fastcall BlitAddRef(MockBlitter* b, void*) { return static_cast<uint32_t>(++b->refs); }
uint32_t __fastcall BlitRelease(MockBlitter* b, void*) { return static_cast<uint32_t>(--b->refs); }
uint32_t __fastcall BlitGiven(MockBlitter* b, void*, void* font) { NoteBlit(b, 3, font); return 1; }
uint32_t __fastcall BlitTaken(MockBlitter* b, void*, void* font) { NoteBlit(b, 4, font); return 1; }
uint32_t __fastcall BlitBegin(MockBlitter* b, void*, void* font, uint32_t picture, uint32_t mode)
{
    NoteBlit(b, 5, font, picture, mode);
    return 1;
}
uint32_t __fastcall BlitEnd(MockBlitter* b, void*, void* font, uint32_t picture, uint32_t mode)
{
    NoteBlit(b, 6, font, picture, mode);
    return 1;
}

void** MockBlitterTable()
{
    static void* table[8] = {};
    table[1] = reinterpret_cast<void*>(&BlitAddRef);
    table[2] = reinterpret_cast<void*>(&BlitRelease);
    table[3] = reinterpret_cast<void*>(&BlitGiven);
    table[4] = reinterpret_cast<void*>(&BlitTaken);
    table[5] = reinterpret_cast<void*>(&BlitBegin);
    table[6] = reinterpret_cast<void*>(&BlitEnd);
    return table;
}

void DriveFontObject()
{
    // In place between margins: the constructor, QueryInterface, the count,
    // Unload of a font never loaded, then the destructor.
    {
        alignas(8) uint8_t space[16 + kFontBytes + 16];
        std::memset(space, 0xA5, sizeof space);
        void* const self = space + 16;
        MixByte(Game<SThis>(addr::GZFont_Construct)(self, nullptr) == U(self) ? 1 : 0);
        MixBytes(space, 16);
        MixBytes(space + 16 + kFontBytes, 16);
        MixFontFields(self);
        for (const uint32_t iid : {0x10673960u, 1u, 0u, 2u, 0x1AA404ACu, 0x00673960u, 0x287FB697u, 0xFFFFFFFFu}) {
            uint32_t out = 0x12345678;
            MixByte(static_cast<uint8_t>(Game<STwo>(addr::GZFont_QueryInterface)(self, nullptr, iid, U(&out))));
            Mix(out == 0x12345678 ? 0xFFFF : out - U(self));
            Mix(Field<uint32_t>(self, 8));
        }
        Mix(Virtual<uint32_t>(self, 0x4));
        while (Field<int32_t>(self, 8) > 1)
            Mix(Game<SThis>(addr::GZFont_Release)(self, nullptr));
        MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFont_Unload)(self, nullptr)));
        MixByte(static_cast<uint8_t>(Virtual<uint32_t>(self, 0x10)));
        MixFontFields(self);
        Game<SThis>(addr::GZFont_Destruct)(self, nullptr);
        MixBytes(space, 16);
        MixBytes(space + 16 + kFontBytes, 16);
        MixByte(Field<uint32_t>(self, 0) == 0x00AC55D8 ? 1 : 0);
        MixByte(space[16 + 4]);
        Mix(Field<uint32_t>(self, 0xEC));
    }
    // From the allocator: Release at 0 and at 1 (each destroys), down from
    // three, through the vtable; the last reference's function; the
    // deleting destructor without freeing (freed by us) and with.
    Mix(Game<SThis>(addr::GZFont_Release)(NewFont(), nullptr));
    {
        void* const f = NewFont();
        for (int i = 0; i < 3; ++i)
            Mix(Virtual<uint32_t>(f, 0x4));
        for (int i = 0; i < 2; ++i)
            Mix(Game<SThis>(addr::GZFont_Release)(f, nullptr));
        Mix(Virtual<uint32_t>(f, 0x8));
    }
    Game<SThis>(addr::GZFont_Destroy)(NewFont(), nullptr);
    {
        void* const f = NewFont();
        MixByte(Game<SOne>(addr::GZFont_DeletingDestruct)(f, nullptr, 0) == U(f) ? 1 : 0);
        Game<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(f);
        void* const g = NewFont();
        MixByte(Game<SOne>(addr::GZFont_DeletingDestruct)(g, nullptr, 3) == U(g) ? 1 : 0);
    }
    // A glyph cache in place between margins: made, cleared, cleared with a
    // block, taken down.
    {
        alignas(8) uint8_t space[16 + kCacheBytes + 16];
        std::memset(space, 0xA5, sizeof space);
        uint8_t* const cache = space + 16;
        MixByte(Game<SThis>(addr::GZFont_CacheConstruct)(cache, nullptr) == U(cache) ? 1 : 0);
        MixCache(cache);
        Game<SThis>(addr::GZFont_CacheClear)(cache, nullptr);
        MixCache(cache);
        void* const block = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x40);
        std::memcpy(cache, &block, 4);
        for (uint32_t word = 1; word < 6; ++word)
            std::memcpy(cache + 4 * word, &word, 4);
        Game<SThis>(addr::GZFont_CacheClear)(cache, nullptr);
        MixCache(cache);
        Game<SThis>(addr::GZFont_CacheDestroy)(cache, nullptr);
        MixCache(cache);
        MixBytes(space, 16);
        MixBytes(space + 16 + kCacheBytes, 16);
    }
    // Fonts the font system made and loaded: measured (filling the caches),
    // their properties, unloaded by address, loaded again, measured, freed
    // by their last Release.
    void* const fonts = Game<void*(__cdecl*)()>(addr::SC4_Singleton913D22)();
    if (!fonts) {
        Mix(0xF0F0F0F0);
        FtStage("fontobject");
        return;
    }
    struct Face {
        const char* name;
        uint32_t size, style;
    };
    const char text[] = "OpenSC4 fonts, Wgjpqy 0123456789";
    for (const Face& face : {Face{"Arial MT", 12, 0}, Face{"Times New Roman", 17, 2}, Face{"Arial MT", 30, 1}}) {
        GameString name = MakeString(face.name);
        void* const font = Virtual<void*>(fonts, 0x2C, &name, uint32_t{0}, face.size, face.style, uint32_t{0});
        FreeString(name);
        if (!font) {
            Mix(0xF0F00000 | face.size);
            continue;
        }
        MixFontFields(font);
        GameString family = EmptyString();
        Mix(Game<SOne>(addr::GZFont_GetFamily)(font, nullptr, U(&family)) & 0xFF);
        MixString(family);
        FreeString(family);
        for (const uint32_t mode : {1u, 2u}) {
            Game<SOne>(addr::GZFont_SetMode)(font, nullptr, mode);
            Mix(Virtual<uint32_t>(font, 0xC0, text, uint32_t{sizeof text - 1}));
            MixByte(Virtual<uint32_t>(font, 0xF0, uint32_t{'W'}) ? 1 : 0);
            Mix(Game<SThis>(addr::GZFont_GlyphCache)(font, nullptr) - U(font));
            MixFontFields(font);
        }
        Mix(Game<SThis>(addr::GZFont_LineHeight)(font, nullptr));
        Mix(Game<SThis>(addr::GZFont_Baseline)(font, nullptr));
        Mix(Game<SThis>(addr::GZFont_BelowBaseline)(font, nullptr));
        Game<FFloat2>(addr::GZFont_SetExtra)(font, nullptr, 2.0f, 0);
        MixFontFields(font);
        Mix(Virtual<uint32_t>(font, 0xC0, text, uint32_t{sizeof text - 1}));
        MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFont_Unload)(font, nullptr)));
        MixFontFields(font);
        MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFont_Unload)(font, nullptr)));
        MixByte(static_cast<uint8_t>(Virtual<uint32_t>(font, 0x0C)));
        MixFontFields(font);
        Mix(Virtual<uint32_t>(font, 0xC0, text, uint32_t{sizeof text - 1}));
        while (Field<int32_t>(font, 8) > 1)
            Mix(Game<SThis>(addr::GZFont_Release)(font, nullptr));
        Mix(Game<SThis>(addr::GZFont_Release)(font, nullptr));
    }
    FtStage("fontobject");
}

void DriveFontProperties()
{
    void* const f = NewFont();
    Virtual<uint32_t>(f, 0x4);
    uint8_t* const b = static_cast<uint8_t*>(f);

    // The style bits: set and cleared (the flag is a byte), asked.
    const uint32_t asks[] = {0, 1, 2, 4, 0x10, 0x3C, 0x40, 0x14, 0xFFFFFFFF};
    const std::pair<uint32_t, uint32_t> sets[] = {{0xFF, 1}, {0x4, 0}, {0x3, 1}, {0x10, 0x100}, {0x20, 0xFF},
                                                  {0xFFFFFFFF, 0}, {0x3C, 1}};
    for (const auto& [bits, on] : sets) {
        Mix(Game<STwo>(addr::GZFont_SetStyle)(f, nullptr, bits, on));
        Mix(Field<uint32_t>(b, 0x40));
        for (const uint32_t ask : asks)
            Mix(Game<SOne>(addr::GZFont_HasStyle)(f, nullptr, ask));
    }
    const uint32_t all = 0xFFFFFFFF;
    std::memcpy(b + 0x40, &all, 4);
    for (const uint32_t ask : asks)
        Mix(Game<SOne>(addr::GZFont_HasStyle)(f, nullptr, ask));

    // The colours and the mode.
    for (const auto& [c, d] : {std::pair<uint32_t, uint32_t>{0xFF00FF00, 0x12}, {0, 0}, {0x08000000, 0xFFFFFFFF}}) {
        Mix(Game<STwo>(addr::GZFont_SetColours)(f, nullptr, c, d));
        Mix(Field<uint32_t>(b, 0x44));
        Mix(Field<uint32_t>(b, 0x48));
    }
    for (const uint32_t mode : {2u, 0u, 1u, 3u, 7u, 0u, 0xFFFFFFFFu, 1u}) {
        Mix(Game<SOne>(addr::GZFont_SetMode)(f, nullptr, mode));
        Mix(Field<uint32_t>(b, 0x4C));
        Mix(Game<SThis>(addr::GZFont_GlyphCache)(f, nullptr) - U(f));
    }

    // The grey ramp: power and bias, read through the vtable, the levels.
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto mixRamp = [&] {
        Mix(std::bit_cast<uint32_t>(Virtual<float>(f, 0x44)));
        Mix(std::bit_cast<uint32_t>(Virtual<float>(f, 0x4C)));
        MixFontFields(f);
    };
    mixRamp();
    for (const float power : {0.5f, 0.5f, -1.0f, -0.0f, 0.0f, 0.25f, 9.99f, 10.0f, 11.0f, inf, -inf, nan, nan, 1.0f}) {
        Game<FFloat>(addr::GZFont_SetPower)(f, nullptr, power);
        mixRamp();
    }
    for (const float bias : {0.3f, 0.3f, -2.0f, -1.0f, -0.5f, -0.0f, 0.0f, 0.75f, 1.0f, 2.0f, nan, 0.0f}) {
        Game<FFloat>(addr::GZFont_SetBias)(f, nullptr, bias);
        mixRamp();
    }
    Game<FFloat>(addr::GZFont_SetPower)(f, nullptr, 2.0f);
    Game<FFloat>(addr::GZFont_SetBias)(f, nullptr, -0.25f);
    mixRamp();

    // The path and the family.
    for (const char* path : {"C:\\Fonts\\face.ttf", "", "arial.ttf"}) {
        GameString in = MakeString(path);
        Mix(Game<SOne>(addr::GZFont_SetPath)(f, nullptr, U(&in)) & 0xFF);
        FreeString(in);
        GameString out = MakeString("was here");
        Mix(Game<SOne>(addr::GZFont_GetPath)(f, nullptr, U(&out)) & 0xFF);
        MixString(out);
        FreeString(out);
        GameString family = MakeString("old");
        Mix(Game<SOne>(addr::GZFont_GetFamily)(f, nullptr, U(&family)) & 0xFF);
        MixString(family);
        FreeString(family);
    }

    // The encoding.
    for (const uint32_t e : {0x10u, 0x4E4u, 0xFFFFFF02u, 0u}) {
        Mix(Game<SOne>(addr::GZFont_SetEncoding)(f, nullptr, e));
        uint32_t out = 0x12345678;
        Mix(Game<SOne>(addr::GZFont_GetEncoding)(f, nullptr, U(&out)));
        Mix(out);
    }
    for (const uint32_t e : {0u, 1u, 2u, 3u, 0x102u, 0x10u})
        MixByte(static_cast<uint8_t>(Game<SOne>(addr::GZFont_SupportsEncoding)(f, nullptr, e)));

    // The scales.
    const auto mixScales = [&] {
        float x = 7.0f, y = 7.0f;
        Game<STwo>(addr::GZFont_GetScale)(f, nullptr, U(&x), U(&y));
        Mix(std::bit_cast<uint32_t>(x));
        Mix(std::bit_cast<uint32_t>(y));
        Game<STwo>(addr::GZFont_GetExtra)(f, nullptr, U(&x), U(&y));
        Mix(std::bit_cast<uint32_t>(x));
        Mix(std::bit_cast<uint32_t>(y));
        MixFontFields(f);
    };
    mixScales();
    for (const float extra : {0.05f, 0.1f, 0.1f, 1.0f, 5.5f, 10.0f, 20.0f, nan, -1.0f, -0.0f, 3.0f}) {
        Game<FFloat2>(addr::GZFont_SetExtra)(f, nullptr, extra, 0xDEADBEEF);
        mixScales();
    }

    // The spacing and the line's metrics (+0x84 and +0x3C set by hand, as
    // SetSize would).
    const uint32_t above = 11, height = 14;
    std::memcpy(b + 0x84, &above, 4);
    std::memcpy(b + 0x3C, &height, 4);
    for (const auto& [s, t] : {std::pair<uint32_t, uint32_t>{1, 2}, {1, 2}, {0xFFFFFFFF, 5}, {3, 0xFFFFFFF0}, {0, 0}}) {
        Mix(Game<STwo>(addr::GZFont_SetSpacing)(f, nullptr, s, t));
        uint32_t x = 0x12345678, y = 0x9ABCDEF0;
        Mix(Game<STwo>(addr::GZFont_GetSpacing)(f, nullptr, U(&x), U(&y)));
        Mix(x);
        Mix(y);
        Mix(Game<SThis>(addr::GZFont_LineHeight)(f, nullptr));
        Mix(Game<STwo>(addr::GZFont_TextHeight)(f, nullptr, 0x1234, 99));
        Mix(Game<SThis>(addr::GZFont_Baseline)(f, nullptr));
        Mix(Game<SThis>(addr::GZFont_BelowBaseline)(f, nullptr));
    }
    Mix(Game<SThis>(addr::GZFont_Release)(f, nullptr));
    FtStage("fontprops");
}

void DriveFontBlitter()
{
    for (const uint32_t mode : {0u, 1u, 2u, 3u, 4u, 0xFFFFFFFFu, 0x80000002u})
        Mix(Game<uint32_t(__cdecl*)(uint32_t)>(addr::GZFont_BlitMode)(mode));

    void* const f = NewFont();
    Virtual<uint32_t>(f, 0x4);
    MockBlitter a{MockBlitterTable(), 1, 1, f}, b{MockBlitterTable(), 1, 2, f};
    const auto setBlitter = Game<SOne>(addr::GZFont_SetBlitter);
    for (MockBlitter* m : {&a, &a, &b, static_cast<MockBlitter*>(nullptr), &b, &a}) {
        setBlitter(f, nullptr, U(m));
        MixByte(Field<uint32_t>(f, 0x8C) == U(&a) ? 1 : Field<uint32_t>(f, 0x8C) == U(&b) ? 2 : 0);
        Mix(static_cast<uint32_t>(a.refs));
        Mix(static_cast<uint32_t>(b.refs));
    }
    // Begin and End nested, End below 0 and Begin from there, in four modes.
    const auto begin = Game<SOne>(addr::GZFont_Begin);
    const auto end = Game<SOne>(addr::GZFont_End);
    for (const uint32_t mode : {1u, 2u, 3u, 9u}) {
        std::memcpy(static_cast<uint8_t*>(f) + 0x4C, &mode, 4);
        for (const auto& [isBegin, picture] : {std::pair<bool, uint32_t>{true, 0x1000}, {true, 0x2000}, {false, 0x3000},
                                               {false, 0x4000}, {false, 0x5000}, {true, 0x6000}, {true, 0x7000},
                                               {true, 0x8000}, {false, 0x9000}}) {
            MixByte(static_cast<uint8_t>((isBegin ? begin : end)(f, nullptr, picture)));
            Mix(Field<uint32_t>(f, 0x80));
        }
        const uint32_t zero = 0;
        std::memcpy(static_cast<uint8_t*>(f) + 0x80, &zero, 4);
    }
    setBlitter(f, nullptr, 0);
    Mix(static_cast<uint32_t>(a.refs));
    Mix(Game<SThis>(addr::GZFont_Release)(f, nullptr));
    FtStage("fontblitter");
}

// ====================================================================================
// The font's containers (f3b)

// Buffers between margins, vectors, maps, glyph tables and lists of our own,
// changed by the 36 functions at their addresses (and the game's allocator,
// balancing and growth). What goes into the hash is contents and places
// (offsets into a buffer, a node's order), never an address.

namespace containers {
constexpr std::uintptr_t kPlacedFill = 0x00896BF6, kTextFind = 0x00896C19, kPlacedCopy = 0x00896C90,
                         kPlacedConstruct = 0x00896CC2, kPlacedFillN = 0x00896CD8, kPlacedCopyBack = 0x00896CFD,
                         kGlyphsEraseTree = 0x0089753F, kNamesEraseTree = 0x0089756C, kTextFindTagged = 0x00897599,
                         kPlacedCopyTagged = 0x008975B4, kPlacedUninitCopy = 0x008975D1,
                         kPlacedCopyBackTagged = 0x008975F7, kPlacedFillNTagged = 0x00897614, kMapFind = 0x0089762F,
                         kGlyphValueConstruct = 0x0089766B, kPairConstruct = 0x0089768B, kGlyphsClear = 0x00897A1C,
                         kNamesClear = 0x00897A45, kGlyphNode = 0x00897A6E, kMapNode = 0x00897A91,
                         kTextInsert = 0x00897BDC, kGlyphsDestruct = 0x00897C5F, kNamesDestruct = 0x00897C76,
                         kListInsert = 0x00897C8D, kGlyphInsertNoResize = 0x00897CB2,
                         kPointersOverflow = 0x00897D22, kMapInsertAt = 0x00897DAC, kHolderDestruct = 0x00897E36,
                         kPlacedErase = 0x00897E56, kMapInsert = 0x00897E7C, kPlacedOverflow = 0x00897F04,
                         kTextAllocateCopy = 0x00897FC2, kGlyphInsert = 0x008986BF, kTextReserve = 0x008986E3,
                         kPlacedFillInsert = 0x0089874D, kPlacedResize = 0x00899513;
constexpr std::uintptr_t kHolderConstruct = 0x00897FED;
} // namespace containers

using CTwo = uint32_t(__cdecl*)(uint32_t, uint32_t);
using CThree = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);
using CFour = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);

uint32_t StlAllocate(uint32_t bytes) { return U(Game<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(bytes)); }
void StlFree(uint32_t p) { Game<void(__cdecl*)(void*)>(addr::SC4_Free)(At32(p)); }

// A place in a buffer: its offset, or a mark for null and for elsewhere.
uint32_t PlaceIn(uint32_t base, uint32_t size, uint32_t p)
{
    if (!p)
        return 0xFFFFFFFF;
    return p >= base && p <= base + size ? p - base : 0xFFFFFFFE;
}

// A vector (begin, end, room): its size and room in bytes and its contents.
void MixVector(const uint32_t (&v)[4])
{
    Mix(v[1] - v[0]);
    Mix(v[2] - v[0]);
    MixByte(v[0] ? 1 : 0);
    if (v[1] > v[0])
        MixBytes(At32(v[0]), v[1] - v[0]);
}

void DriveContainersHelpers()
{
    using namespace containers;
    // Placed glyphs in a buffer of 16 between margins.
    constexpr uint32_t kBytes = 0x24 * 16 + 64;
    uint8_t buffer[kBytes];
    const uint32_t base = U(buffer) + 32;
    uint32_t value[9], other[9];
    for (uint32_t i = 0; i < 9; ++i) {
        value[i] = 0x01010101u * (i + 1);
        other[i] = 0xF0E0D0C0u ^ (i * 0x1111);
    }
    const auto reset = [&] {
        for (uint32_t i = 0; i < kBytes; ++i)
            buffer[i] = static_cast<uint8_t>(i * 7 + 3);
    };
    const auto mixAll = [&](uint32_t result) {
        Mix(PlaceIn(U(buffer), kBytes, result));
        MixBytes(buffer, kBytes);
    };
    const auto at = [&](int32_t index) { return base + static_cast<uint32_t>(index * 0x24); };
    for (const auto& [first, last] : {std::pair<int32_t, int32_t>{0, 0}, {0, 1}, {2, 5}, {3, 3}}) {
        reset();
        mixAll(Game<CThree>(kPlacedFill)(at(first), at(last), U(value)));
    }
    // Copies forwards and back, by counts of 0, 2 and 5, one that is not a
    // whole number of glyphs, one backwards, and overlapping.
    for (const auto& [first, last, to] : {std::tuple<int32_t, int32_t, int32_t>{0, 0, 4}, {0, 2, 6}, {1, 6, 8},
                                          {5, 1, 0}, {0, 5, 2}, {3, 8, 1}}) {
        reset();
        mixAll(Game<CFour>(kPlacedCopy)(at(first), at(last), at(to), 0));
        reset();
        mixAll(Game<CThree>(kPlacedCopyTagged)(at(first), at(last), at(to)));
        reset();
        mixAll(Game<CFour>(kPlacedCopyBack)(at(first), at(last), at(to + last - first), 0));
        reset();
        mixAll(Game<CThree>(kPlacedCopyBackTagged)(at(first), at(last), at(to + last - first)));
        if (last >= first) {
            reset();
            mixAll(Game<CFour>(kPlacedUninitCopy)(at(first), at(last), at(to), 0));
        }
    }
    reset();
    mixAll(Game<CFour>(kPlacedCopy)(at(0), at(1) + 0x10, at(3), 0));
    reset();
    Game<CTwo>(kPlacedConstruct)(0, U(value));
    Game<CTwo>(kPlacedConstruct)(at(4), U(other));
    mixAll(0);
    for (const uint32_t count : {0u, 1u, 4u}) {
        reset();
        mixAll(Game<CFour>(kPlacedFillN)(at(2), count, U(other), 0));
        reset();
        mixAll(Game<CThree>(kPlacedFillNTagged)(at(1), count, U(value)));
    }

    // A glyph's value and a map's pair made, and not where null.
    uint32_t glyph[9], pair[3];
    std::memset(glyph, 0xA5, sizeof glyph);
    std::memset(pair, 0x5A, sizeof pair);
    Mix(Game<CTwo>(kGlyphValueConstruct)(U(glyph + 1), U(other)) == U(glyph + 1) ? 1 : 0);
    Mix(Game<CTwo>(kGlyphValueConstruct)(0, U(other)));
    MixBytes(reinterpret_cast<const uint8_t*>(glyph), sizeof glyph);
    Mix(Game<CTwo>(kPairConstruct)(U(pair), U(value)) == U(pair) ? 1 : 0);
    Mix(Game<CTwo>(kPairConstruct)(0, U(value)));
    MixBytes(reinterpret_cast<const uint8_t*>(pair), sizeof pair);
}

void DriveContainersPlaced()
{
    using namespace containers;
    uint32_t v[4] = {};
    uint32_t glyph[9];
    const auto make = [&](uint32_t seed) {
        for (uint32_t i = 0; i < 9; ++i)
            glyph[i] = seed * 0x9E3779B9u + i;
        return U(glyph);
    };
    // Resized up from nothing, down, the same, up past its room.
    for (const uint32_t count : {5u, 3u, 3u, 0u, 2u, 9u, 30u, 31u, 4u}) {
        Game<STwo>(kPlacedResize)(v, nullptr, count, make(count));
        MixVector(v);
    }
    // Copies put in at the front, the middle and the end, with more and
    // fewer glyphs after the place than are put in, room left and none.
    for (const auto& [place, count] : {std::pair<uint32_t, uint32_t>{0, 2}, {1, 1}, {3, 0}, {2, 5}, {6, 1}, {0, 9},
                                       {5, 3}, {0, 40}, {7, 2}}) {
        const uint32_t size = (v[1] - v[0]) / 0x24;
        const uint32_t to = v[0] + std::min(place, size) * 0x24;
        Game<SThree>(kPlacedFillInsert)(v, nullptr, to, count, make(place * 31 + count));
        MixVector(v);
    }
    // Erased: a range from the middle, from the front, nothing, to the end.
    for (const auto& [first, last] : {std::pair<uint32_t, uint32_t>{3, 7}, {0, 2}, {4, 4}, {10, 0xFFFFFFFF}}) {
        const uint32_t size = (v[1] - v[0]) / 0x24;
        const uint32_t a = v[0] + std::min(first, size) * 0x24;
        const uint32_t b = last == 0xFFFFFFFF ? v[1] : v[0] + std::min(last, size) * 0x24;
        Mix(Game<STwo>(kPlacedErase)(v, nullptr, a, b) == a ? 1 : 0);
        MixVector(v);
    }
    // The overflow by itself: one copy and three, at the front and at the end.
    uint8_t tag = 0;
    for (const auto& [place, count, atEnd] : {std::tuple<uint32_t, uint32_t, uint32_t>{0, 1, 0}, {2, 3, 0},
                                             {0xFFFFFFFF, 1, 1}, {0xFFFFFFFF, 4, 1}}) {
        const uint32_t to = place == 0xFFFFFFFF ? v[1] : v[0] + place * 0x24;
        Mix(Game<SFive>(kPlacedOverflow)(v, nullptr, to, make(count), U(&tag), count, atEnd) - v[0]);
        MixVector(v);
    }
    if (v[0])
        StlFree(v[0]);
}

void DriveContainersText()
{
    using namespace containers;
    uint32_t t[4] = {};
    // Reserve on nothing (no room asked, then some), words put in at the end,
    // the front and the middle until there is no room, then past it.
    Mix(Game<SOne>(kTextReserve)(t, nullptr, 0));
    MixVector(t);
    Game<SOne>(kTextReserve)(t, nullptr, 4);
    MixVector(t);
    for (uint32_t i = 0; i < 14; ++i) {
        const uint16_t word = static_cast<uint16_t>(0x0E30 + i);
        const uint32_t size = (t[1] - t[0]) / 2;
        const uint32_t place = i % 3 == 0 ? size : i % 3 == 1 ? 0 : size / 2;
        const uint32_t answer = Game<STwo>(kTextInsert)(t, nullptr, t[0] + place * 2, U(&word));
        Mix((answer - t[0]) / 2);
        MixVector(t);
    }
    // Reserve with words there: more (copied), less (nothing).
    Game<SOne>(kTextReserve)(t, nullptr, 40);
    MixVector(t);
    Mix(Game<SOne>(kTextReserve)(t, nullptr, 10) == t[0] ? 1 : 0);
    MixVector(t);
    // A copy into a new block.
    const uint32_t block = Game<SThree>(kTextAllocateCopy)(t, nullptr, 20, t[0] + 2, t[1] - 2);
    MixBytes(At32(block), t[1] - t[0] - 4);
    StlFree(block);

    // The find, by both, over every length from 0 to 14 for words there,
    // not there and with bits above the word, and over a range backwards.
    for (const uint32_t value : {0x0E30u, 0x0E35u, 0x0E3Du, 0x0E99u, 0x10E31u}) {
        for (uint32_t length = 0; length <= 14 && length <= (t[1] - t[0]) / 2; ++length) {
            const uint32_t first = t[0], last = t[0] + length * 2;
            Mix(PlaceIn(first, 64, Game<CFour>(kTextFind)(first, last, U(&value), 0)));
            Mix(PlaceIn(first, 64, Game<CThree>(kTextFindTagged)(first, last, U(&value))));
        }
        Mix(PlaceIn(t[0], 64, Game<CFour>(kTextFind)(t[0] + 8, t[0], U(&value), 0)));
    }
    StlFree(t[0]);
}

// A map of characters (header, count): its count, then in order each node's
// colour, the places of its parent and children (by their order) and its pair.
void MixCharMap(const uint32_t (&map)[2])
{
    Mix(map[1]);
    const uint32_t header = map[0];
    std::vector<uint32_t> order;
    for (uint32_t node = Field<uint32_t>(At32(header), 8); node != header; node = TreeNext(node))
        order.push_back(node);
    const auto place = [&](uint32_t node) -> uint32_t {
        if (!node)
            return 0xFFFFFFFF;
        if (node == header)
            return 0xFFFFFFFE;
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i] == node)
                return static_cast<uint32_t>(i);
        return 0xFFFFFFFD;
    };
    Mix(static_cast<uint32_t>(order.size()));
    Mix(place(Field<uint32_t>(At32(header), 4)));
    Mix(place(Field<uint32_t>(At32(header), 0xC)));
    for (const uint32_t node : order) {
        const uint8_t* const n = At32(node);
        MixByte(Field<uint8_t>(n, 0));
        Mix(place(Field<uint32_t>(n, 4)));
        Mix(place(Field<uint32_t>(n, 8)));
        Mix(place(Field<uint32_t>(n, 0xC)));
        Mix(Field<uint16_t>(n, 0x10));
        Mix(Field<uint32_t>(n, 0x14));
    }
}

void DriveContainersMap(Random& random, std::uintptr_t clear, std::uintptr_t destruct, std::uintptr_t eraseTree)
{
    using namespace containers;
    uint32_t map[2] = {StlAllocate(0x18), 0};
    const uint32_t header = map[0];
    std::memset(At32(header), 0, 0x18);
    std::memcpy(At32(header) + 8, &header, 4);
    std::memcpy(At32(header) + 0xC, &header, 4);
    const auto insert = [&](uint16_t key, uint32_t value) {
        const uint32_t pair[2] = {key, value};
        uint32_t result[2] = {0, 0xA5A5A5A5};
        Mix(Game<STwo>(kMapInsert)(map, nullptr, U(result), U(pair)) == U(result) ? 1 : 0);
        Mix(result[1]);
        Mix(Field<uint16_t>(At32(result[0]), 0x10));
        Mix(Field<uint32_t>(At32(result[0]), 0x14));
    };
    for (uint32_t i = 0; i < 70; ++i)
        insert(static_cast<uint16_t>(random.Range(0, 60) * 0x123), i);
    insert(0, 1);
    insert(0xFFFF, 2);
    insert(0, 3);
    MixCharMap(map);
    for (const uint16_t key : {0, 0x123, 0x124, 0x1FFF, 0x2A0F, 0x4374, 0xFFFF, 0xFFFE}) {
        const uint32_t found = Game<SOne>(containers::kMapFind)(map, nullptr, U(&key));
        Mix(found == header ? 0xFFFFFFFE : Field<uint16_t>(At32(found), 0x10));
    }
    // Under the last node on its right and under the first on its left with
    // x set; a node made.
    uint32_t result = 0;
    const uint32_t last[2] = {0xFFFF, 77}, first[2] = {0, 88};
    Game<SFive>(kMapInsertAt)(map, nullptr, U(&result), 0, Field<uint32_t>(At32(header), 0xC), U(last), 1);
    Game<SFive>(kMapInsertAt)(map, nullptr, U(&result), 1, Field<uint32_t>(At32(header), 8), U(first), 0);
    MixCharMap(map);
    const uint32_t node = Game<SOne>(kMapNode)(map, nullptr, U(last));
    Mix(Field<uint16_t>(At32(node), 0x10));
    Mix(Field<uint32_t>(At32(node), 0x14));
    StlFree(node);
    // The root's right subtree erased by itself, then the map cleared, filled
    // again and taken down.
    const uint32_t root = Field<uint32_t>(At32(header), 4);
    const uint32_t right = Field<uint32_t>(At32(root), 0xC);
    Game<SOne>(eraseTree)(map, nullptr, right);
    const uint32_t none = 0;
    std::memcpy(At32(root) + 0xC, &none, 4);
    Game<SThis>(clear)(map, nullptr);
    MixCharMap(map);
    Game<SThis>(clear)(map, nullptr);
    for (uint32_t i = 0; i < 6; ++i)
        insert(static_cast<uint16_t>(i * 3 % 4), i);
    MixCharMap(map);
    Game<SThis>(destruct)(map, nullptr);
}

void DriveContainersGlyphs(Random& random)
{
    using namespace containers;
    uint8_t cache[kCacheBytes];
    Game<SThis>(addr::GZFont_CacheConstruct)(cache, nullptr);
    uint8_t* const table = cache + 0x18;
    const auto mixTable = [&] {
        const uint32_t begin = Field<uint32_t>(table, 4), end = Field<uint32_t>(table, 8);
        Mix(Field<uint32_t>(table, 0x10));
        Mix((end - begin) / 4);
        for (uint32_t bucket = begin; bucket < end; bucket += 4) {
            uint32_t count = 0;
            for (uint32_t n = Field<uint32_t>(At32(bucket), 0); n; n = Field<uint32_t>(At32(n), 0), ++count)
                MixBytes(At32(n) + 4, 0x1C);
            Mix(count);
        }
    };
    uint32_t value[7];
    const auto insert = [&](std::uintptr_t fn, uint32_t key) {
        value[0] = key;
        for (uint32_t i = 1; i < 7; ++i)
            value[i] = random.Next();
        uint32_t result[3] = {0, 0, 0xC3C3C3C3};
        Mix(Game<STwo>(fn)(table, nullptr, U(result), U(value)) == U(result) ? 1 : 0);
        Mix(result[1] == U(table) ? 1 : 0);
        Mix(result[2]);
        Mix(Field<uint32_t>(At32(result[0]), 4));
    };
    for (uint32_t i = 0; i < 90; ++i)
        insert(kGlyphInsert, random.Range(0x20, 0x7E) + (i % 7 == 0 ? 0x0E00u : 0u));
    mixTable();
    for (const uint32_t key : {0x41u, 0x10041u, 0xFFFFFFFFu, 0u})
        insert(kGlyphInsertNoResize, key);
    mixTable();
    const uint32_t node = Game<SOne>(kGlyphNode)(table, nullptr, U(value));
    Mix(Field<uint32_t>(At32(node), 0));
    MixBytes(At32(node) + 4, 0x1C);
    StlFree(node);
    Game<SThis>(addr::GZFont_CacheDestroy)(cache, nullptr);
}

void DriveContainersRest()
{
    using namespace containers;
    // A list of words: put in before its header, its first node and others.
    uint32_t list = StlAllocate(0xC);
    std::memcpy(At32(list), &list, 4);
    std::memcpy(At32(list) + 4, &list, 4);
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t before = list;
        for (uint32_t step = 0; step < i % 3; ++step)
            before = Field<uint32_t>(At32(before), 0);
        const uint32_t word = 0x1000 + i;
        uint32_t result = 0;
        Mix(Game<SThree>(kListInsert)(&list, nullptr, U(&result), before, U(&word)) == U(&result) ? 1 : 0);
        Mix(Field<uint32_t>(At32(result), 8));
    }
    uint32_t previous = list, count = 0;
    for (uint32_t e = Field<uint32_t>(At32(list), 0); e != list; ++count) {
        Mix(Field<uint32_t>(At32(e), 8));
        MixByte(Field<uint32_t>(At32(e), 4) == previous ? 1 : 0);
        previous = e;
        const uint32_t next = Field<uint32_t>(At32(e), 0);
        StlFree(e);
        e = next;
    }
    Mix(count);
    StlFree(list);

    // A vector of pointers out of room: from nothing, in the middle, none,
    // one at the end.
    uint32_t v[4] = {};
    uint8_t tag = 0;
    for (const auto& [place, n, atEnd] : {std::tuple<uint32_t, uint32_t, uint32_t>{0, 3, 1}, {1, 2, 0},
                                         {0xFFFFFFFF, 0, 1}, {0xFFFFFFFF, 1, 1}, {0, 5, 0}}) {
        const uint32_t to = place == 0xFFFFFFFF ? v[1] : v[0] + place * 4;
        const uint32_t word = 0xABC00000 + n * 0x10 + place;
        Mix(Game<SFive>(kPointersOverflow)(v, nullptr, to, U(&word), U(&tag), n, atEnd) - v[0]);
        MixVector(v);
    }
    if (v[0])
        StlFree(v[0]);

    // A holder never opened, with characters in its map and a block, taken
    // down.
    const uint32_t holder = StlAllocate(0x34);
    Game<SThis>(kHolderConstruct)(At32(holder), nullptr);
    for (uint16_t key : {0x41, 0x20, 0x7A}) {
        const uint32_t pair[2] = {key, key * 2u};
        uint32_t result[2] = {};
        Game<STwo>(kMapInsert)(At32(holder) + 0x28, nullptr, U(result), U(pair));
    }
    const uint32_t block = StlAllocate(16);
    std::memcpy(At32(holder) + 0x14, &block, 4);
    Game<SThis>(containers::kHolderDestruct)(At32(holder), nullptr);
    Mix(Field<uint32_t>(At32(holder), 0x20));
    StlFree(holder);
}

void DriveFontContainers()
{
    Random random{0x0F3B};
    DriveContainersHelpers();
    DriveContainersPlaced();
    DriveContainersText();
    DriveContainersMap(random, containers::kGlyphsClear, containers::kGlyphsDestruct, containers::kGlyphsEraseTree);
    DriveContainersMap(random, containers::kNamesClear, containers::kNamesDestruct, containers::kNamesEraseTree);
    DriveContainersGlyphs(random);
    DriveContainersRest();
    FtStage("fontcontainers");
}

// ====================================================================================
// The font's loading, size and scale (f3c)

// Fonts of our own given the paths of faces (the game's, Windows's and our
// own files), loaded by address and asked their languages; sizes set past
// both ends and while a glyph cache holds glyphs; scales past both ends, -0,
// infinities and NaN, with strings measured at sensible ones; the levels
// rebuilt over powers and biases set in place (off, NaN among them); Load
// again while loaded, without a size, with a blitter of its own, refused by
// SetSize, of a file that is not there and of no path.
using FFloatTwo = uint32_t(__fastcall*)(void*, void*, float, float);

void SetFontPath(void* f, const std::string& path)
{
    GameString s = MakeString(path.c_str());
    Game<SOne>(addr::GZFont_SetPath)(f, nullptr, U(&s));
    FreeString(s);
}

void DriveFontLoad()
{
    const auto load = Game<SThis>(addr::GZFont_Load);
    const auto setSize = Game<SOne>(addr::GZFont_SetSize);
    const auto languageOf = Game<SOne>(addr::GZFont_SupportsLanguage);
    const auto setScale = Game<FFloatTwo>(addr::GZFont_SetScale);
    const auto rebuild = Game<SThis>(addr::GZFont_RebuildLevels);
    const char text[] = "OpenSC4 fonts, Wgjpqy 0123456789";
    const auto measure = [&](void* f) {
        for (const uint32_t mode : {1u, 2u}) {
            Game<SOne>(addr::GZFont_SetMode)(f, nullptr, mode);
            Mix(Virtual<uint32_t>(f, 0xC0, text, uint32_t{sizeof text - 1}));
        }
        MixFontFields(f);
    };
    const uint32_t languages[] = {0,     1,     8,     0x10,  0x36A, 0x3A4, 0x3A8, 0x3B5, 0x3B6,      0x4E2,     0x4E3,
                                  0x4E4, 0x4E5, 0x4E6, 0x4E7, 0x4E8, 0x4E9, 0x4EA, 0x369, 0xFFFFFFFF, 0x80000000};
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    std::vector<std::string> paths = FontPaths();
    for (const std::size_t i : {std::size_t{3}, std::size_t{0}, std::size_t{5}, std::size_t{7}, std::size_t{13},
                                std::size_t{18}, std::size_t{19},
                                std::size_t{20}, std::size_t{25}, std::size_t{28}, std::size_t{29}}) {
        if (i >= paths.size())
            continue;
        void* const f = NewFont();
        Virtual<uint32_t>(f, 0x4);
        // A size before the face is open: stored, true.
        Mix(setSize(f, nullptr, 13) & 0xFF);
        SetFontPath(f, paths[i]);
        MixByte(static_cast<uint8_t>(load(f, nullptr)));
        MixFontFields(f);
        MixByte(static_cast<uint8_t>(load(f, nullptr)));
        if (!Field<uint8_t>(f, 4)) {
            Game<SThis>(addr::GZFont_Release)(f, nullptr);
            continue;
        }
        for (const uint32_t language : languages)
            MixByte(static_cast<uint8_t>(languageOf(f, nullptr, language)));
        for (const uint32_t pixels : {0u, 1u, 5u, 12u, 29u, 64u, 0x800u, 0x801u, 0x7FFFFFFFu, 0xFFFFFFFFu, 14u}) {
            MixByte(static_cast<uint8_t>(setSize(f, nullptr, pixels)));
            MixFontFields(f);
        }
        // With glyphs in the caches SetSize refuses; SetScale clears them.
        measure(f);
        MixByte(static_cast<uint8_t>(setSize(f, nullptr, 20)));
        MixFontFields(f);
        for (const auto& [x, y] : {std::pair<float, float>{1.0f, 1.0f}, {0.5f, 2.0f}, {1.25f, 0.75f}, {0.001f, 5000.0f},
                                   {0.01f, 1000.0f}, {-0.0f, 0.0f}, {-1.0f, 0.2f}, {inf, -inf}, {nan, 1.0f},
                                   {nan, 1.0f}, {1.0f, nan}, {1.0f, 1.0f}, {1.5f, 1.5f}}) {
            setScale(f, nullptr, x, y);
            MixFontFields(f);
            if (x == x && y == y && x > 0.1f && x < 10.0f && y > 0.1f && y < 10.0f)
                measure(f);
        }
        MixByte(static_cast<uint8_t>(setSize(f, nullptr, 18)));
        setScale(f, nullptr, 1.0f, 1.0f);
        MixByte(static_cast<uint8_t>(setSize(f, nullptr, 18)));
        measure(f);
        // The levels over powers and biases put in place.
        for (const auto& [power, bias] :
             {std::pair<float, float>{1.0f, 0.0f}, {2.0f, 0.0f}, {1.0f, 0.5f}, {0.5f, -0.5f}, {1.0f, -1.0f},
              {1.0f, 1.0f}, {10.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, -0.0f}, {nan, 0.0f}, {1.0f, nan}, {0.3f, 0.7f},
              {3.0f, -0.9f}, {1.0f, 0.0f}}) {
            std::memcpy(static_cast<uint8_t*>(f) + 0x54, &power, 4);
            std::memcpy(static_cast<uint8_t*>(f) + 0x58, &bias, 4);
            rebuild(f, nullptr);
            MixFontFields(f);
        }
        // Unloaded, loaded again without a size (T2K then has no
        // transformation, so nothing is measured) and with a ramp on.
        Game<SThis>(addr::GZFont_Unload)(f, nullptr);
        const uint32_t noSize = 0;
        std::memcpy(static_cast<uint8_t*>(f) + 0x38, &noSize, 4);
        const float power = 0.6f;
        std::memcpy(static_cast<uint8_t*>(f) + 0x54, &power, 4);
        MixByte(static_cast<uint8_t>(load(f, nullptr)));
        MixFontFields(f);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }

    // A blitter of its own kept by Load.
    {
        void* const f = NewFont();
        Virtual<uint32_t>(f, 0x4);
        MockBlitter mock{MockBlitterTable(), 1, 1, f};
        Game<SOne>(addr::GZFont_SetBlitter)(f, nullptr, U(&mock));
        SetFontPath(f, paths[3]);
        MixByte(static_cast<uint8_t>(load(f, nullptr)));
        MixByte(Field<uint32_t>(f, 0x8C) == U(&mock) ? 1 : 0);
        Mix(static_cast<uint32_t>(mock.refs));
        MixFontFields(f);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
        Mix(static_cast<uint32_t>(mock.refs));
    }
    // Refused by SetSize (a glyph counted in the monochrome cache).
    {
        void* const f = NewFont();
        Virtual<uint32_t>(f, 0x4);
        const uint32_t one = 1, zero = 0;
        std::memcpy(static_cast<uint8_t*>(f) + 0x12C, &one, 4);
        SetFontPath(f, paths[3]);
        MixByte(static_cast<uint8_t>(load(f, nullptr)));
        MixFontFields(f);
        std::memcpy(static_cast<uint8_t*>(f) + 0x12C, &zero, 4);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }
    // A file that is not there, and no path.
    for (const char* path : {"C:\\opensc4-no-such-folder\\none.ttf", ""}) {
        void* const f = NewFont();
        Virtual<uint32_t>(f, 0x4);
        if (*path)
            SetFontPath(f, path);
        MixByte(static_cast<uint8_t>(load(f, nullptr)));
        MixFontFields(f);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }
    FtStage("fontload");
}

// Fonts of our own (the six need no face) in UTF-16, ASCII, UTF-8 (8 and
// 0xFFFFFFFE), Thai's code page 874 and 1252, given Thai text that reaches
// every branch (SARA AM after a consonant, a tone mark, at the start and the
// end, twice; tall consonants under upper vowels, tone marks, THANTHAKHAT,
// MAITAIKHU, NIKHAHIT; YO YING over lower vowels; tone marks over upper
// vowels; a 0 word; random runs of Thai) and Latin text, by ToUnicode
// through each of the five vectors the callers use, with and
// without the offsets; Convert into vectors of our own with room for none,
// a few and enough; the Thai pass alone over copies of the texts; HasThai
// over every length; the mark's kind of every 16-bit character (and with
// bits above it); every form of the Thai block by kinds 0 to 4; Thai text
// measured by fonts the font system loaded.
namespace fonttext {
using ToUnicodeFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

std::vector<std::u16string> ThaiTexts()
{
    std::vector<std::u16string> texts = {
        u"",
        u"ก",
        u"ำ",
        u"กำ",
        u"ก่ำ",
        u"ำก้ำำ",
        u"ปี",
        u"ป่",
        u"กิ่",
        u"ปิ่",
        u"ญุ",
        u"ญูญฺญ่",
        u"ปุ่",
        u"ฝู๊",
        u"ก์",
        u"ฟ์ฬ๋",
        u"ป็ปํปัปึปื",
        u"ฬั้กั้กี๋",
        u"ก้ำป้ำปำ",
        u"น้ำตาล สวัสดีครับ",
        std::u16string(u"ก่ำ\0ป่", 6),
        u"่ก",
        u"Wgjpqy ก é 0123",
        u"OpenSC4 fonts",
    };
    // Random runs of the Thai block, and of Thai with Latin.
    Random random{0x7A1B00F3u};
    for (uint32_t i = 0; i < 24; ++i) {
        std::u16string t;
        const int32_t length = random.Range(1, 40);
        for (int32_t k = 0; k < length; ++k)
            t += static_cast<char16_t>(i % 4 == 3 && random.Range(0, 3) == 0 ? random.Range(0x20, 0x7E)
                                                                                : random.Range(0x0E01, 0x0E5B));
        texts.push_back(t);
    }
    return texts;
}

std::string Utf8Of(const std::u16string& t)
{
    std::string s;
    for (const char16_t c : t) {
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else if (c < 0x800) {
            s += static_cast<char>(0xC0 | (c >> 6));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | (c >> 12));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// Code page 874: U+0E01 to U+0E3A at 0xA1, U+0E3F to U+0E5B at 0xDF; what
// is not there becomes '?'.
std::string Cp874Of(const std::u16string& t)
{
    std::string s;
    for (const char16_t c : t) {
        if (c < 0x80)
            s += static_cast<char>(c);
        else if (c >= 0x0E01 && c <= 0x0E5B && !(c > 0x0E3A && c < 0x0E3F))
            s += static_cast<char>(c - 0x0E01 + 0xA1);
        else
            s += '?';
    }
    return s;
}

// The font's seven vectors (+0x90 to +0xE4): size, room and contents.
void MixTextVectors(const void* font)
{
    for (std::size_t v = 0x90; v < 0xE4; v += 0xC) {
        const uint32_t begin = Field<uint32_t>(font, v);
        const uint32_t end = Field<uint32_t>(font, v + 4);
        Mix(end - begin);
        Mix(Field<uint32_t>(font, v + 8) - begin);
        if (end > begin)
            MixBytes(At32(begin), end - begin);
    }
}

// An answer: its count, where its text is (the input, a vector of the font
// or neither) and the text.
void MixAnswer(const void* font, uint32_t input, uint32_t inputBytes, uint32_t text, uint32_t count)
{
    Mix(count);
    uint32_t place = 0xFFFF;
    if (text >= input && text <= input + inputBytes) {
        place = 0x10000 + text - input;
    } else {
        for (std::size_t v = 0x90; v < 0xE4; v += 0xC) {
            const uint32_t begin = Field<uint32_t>(font, v);
            if (begin && text >= begin && text <= Field<uint32_t>(font, v + 8)) {
                place = static_cast<uint32_t>(v) << 16 | (text - begin);
                break;
            }
        }
    }
    Mix(place);
    if (place != 0xFFFF && count < 0x1000)
        MixBytes(At32(text), count * 2);
}
} // namespace fonttext

void DriveFontText()
{
    using namespace fonttext;
    const auto toUnicode = Game<ToUnicodeFn>(addr::GZFont_ToUnicode);
    const auto convert = Game<SThree>(addr::GZFont_Convert);
    const auto hasThai = Game<STwo>(addr::GZFont_HasThai);
    const auto markKind = Game<SOne>(addr::GZFont_ThaiMarkKind);
    const auto thai = Game<SFive>(addr::GZFont_Thai);
    const auto form = Game<STwo>(addr::GZFont_ThaiForm);
    const std::vector<std::u16string> texts = ThaiTexts();
    std::vector<std::string> bytes;
    for (const std::u16string& t : texts) {
        bytes.push_back(Utf8Of(t));
        bytes.push_back(Cp874Of(t));
    }
    bytes.push_back("caf\xE9 na\xEFve \x80\x9F\xFF");
    bytes.push_back("\xC3\x28 \xE0\xB8 \xF0\x9F\x98\x80");

    // The mark's kind and the forms.
    uint32_t kinds = 2166136261u;
    for (uint32_t c = 0; c < 0x10000; ++c)
        kinds = (kinds ^ markKind(nullptr, nullptr, c)) * 16777619u;
    Mix(kinds);
    for (const uint32_t c : {0x10E33u, 0xFFFF0E48u, 0x80000E0Du, 0x0E0C0000u})
        Mix(markKind(nullptr, nullptr, c));
    for (uint32_t c = 0x0E00; c < 0x0E60; ++c) {
        for (const uint32_t kind : {0u, 1u, 2u, 3u, 4u, 0x101u, 0xFFFFFFFFu})
            Mix(form(nullptr, nullptr, c, kind) & 0xFFFF);
    }
    for (const auto& [c, kind] : {std::pair<uint32_t, uint32_t>{0x10E48, 2}, {0xFFFF0E4D, 1}, {0x41, 1}, {0xF705, 3}})
        Mix(form(nullptr, nullptr, c, kind) & 0xFFFF);

    // HasThai over every length of each text.
    {
        void* const f = NewFont();
        for (const std::u16string& t : texts) {
            for (uint32_t n = 0; n <= t.size(); ++n)
                MixByte(static_cast<uint8_t>(hasThai(f, nullptr, U(t.data()), n)));
        }
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }

    // ToUnicode in each encoding, through each vector, with and without the
    // offsets.
    for (const uint32_t encoding : {0x10u, 0u, 8u, 0x36Au, 0x4E4u, 0xFFFFFFFEu}) {
        void* const f = NewFont();
        Game<SOne>(addr::GZFont_SetEncoding)(f, nullptr, encoding);
        const auto run = [&](uint32_t input, uint32_t count, uint32_t unit) {
            for (const auto& [which, keep] : {std::pair<uint32_t, uint32_t>{0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 0}, {0, 1},
                                              {4, 0x100}}) {
                uint32_t out = 0xDEADBEEF, n = 0xBEEF;
                toUnicode(f, nullptr, input, count, U(&out), U(&n), which, keep);
                MixAnswer(f, input, count * unit, out, n);
                MixTextVectors(f);
            }
        };
        if (encoding == 0x10) {
            for (const std::u16string& t : texts)
                run(U(t.data()), static_cast<uint32_t>(t.size()), 2);
        } else {
            for (const std::string& b : bytes)
                run(U(b.data()), static_cast<uint32_t>(b.size()), 1);
        }
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }

    // Convert into vectors of our own: no room, a few words, enough.
    for (const uint32_t encoding : {0x10u, 8u, 0x36Au, 0u}) {
        void* const f = NewFont();
        Game<SOne>(addr::GZFont_SetEncoding)(f, nullptr, encoding);
        for (std::size_t i = 0; i < 20; ++i) {
            for (const uint32_t room : {0u, 3u, 160u}) {
                uint32_t v[4] = {};
                if (room) {
                    v[0] = StlAllocate(room * 2);
                    std::memset(At32(v[0]), 0x5A, room * 2);
                    v[1] = v[2] = v[0] + room * 2;
                }
                const uint32_t n =
                    encoding == 0x10
                        ? convert(f, nullptr, U(texts[i].data()), static_cast<uint32_t>(texts[i].size()), U(v))
                        : convert(f, nullptr, U(bytes[i].data()), static_cast<uint32_t>(bytes[i].size()), U(v));
                Mix(n);
                MixVector(v);
                if (v[0])
                    StlFree(v[0]);
            }
        }
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }

    // The Thai pass alone, over copies of the texts, with and without the
    // offsets.
    {
        void* const f = NewFont();
        for (const uint32_t keep : {0u, 1u}) {
            for (const std::u16string& t : texts) {
                std::u16string copy = t + u'￿';
                uint32_t out = 0xDEADBEEF, n = 0xBEEF;
                thai(f, nullptr, U(copy.data()), static_cast<uint32_t>(t.size()), U(&out), U(&n), keep);
                MixAnswer(f, U(copy.data()), static_cast<uint32_t>(t.size()) * 2, out, n);
                MixBytes(reinterpret_cast<const uint8_t*>(copy.data()), copy.size() * 2);
                MixTextVectors(f);
            }
        }
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }

    // Thai text measured by fonts the font system loaded (MeasureString
    // ends in ToUnicode).
    std::vector<std::string> paths = FontPaths();
    for (const std::size_t i : {std::size_t{3}, std::size_t{19}, std::size_t{20}}) {
        if (i >= paths.size())
            continue;
        void* const f = NewFont();
        Virtual<uint32_t>(f, 0x4);
        Game<SOne>(addr::GZFont_SetSize)(f, nullptr, 16);
        SetFontPath(f, paths[i]);
        MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFont_Load)(f, nullptr)));
        if (Field<uint8_t>(f, 4)) {
            Game<SOne>(addr::GZFont_SetEncoding)(f, nullptr, 0x10);
            for (const std::u16string& t : texts)
                Mix(Virtual<uint32_t>(f, 0xC0, t.data(), static_cast<uint32_t>(t.size())));
            MixTextVectors(f);
        }
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }
    FtStage("fonttext");
}

// The lines' thickness over sizes; boxes of every small size; sheets of our
// own grown and stored into (1-bit and grey bitmaps, levels, every style,
// bearings and advances, sheets that grow wider and taller); glyphs got and
// rendered by loaded fonts in both modes, every style and extra scales
// around 1.0 (0.98 and NaN among them), with and without the fallback, and
// by a font with no face (boxes only).
namespace fontglyph {
using StoreFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, int32_t, uint32_t, uint32_t, uint32_t,
                                      uint32_t, uint32_t, uint32_t);

// The box function at its address: the width in ESI, the height in EBX.
__declspec(naked) uint32_t __cdecl CallBox(std::uintptr_t /*fn*/, int32_t /*width*/, int32_t /*height*/)
{
    __asm {
        push ebx
        push esi
        mov eax, dword ptr [esp + 12]
        mov esi, dword ptr [esp + 16]
        mov ebx, dword ptr [esp + 20]
        call eax
        pop esi
        pop ebx
        ret
    }
}

// A sheet: as MixCache, where its pixels are in its block, and its rows.
void MixSheet(const uint8_t* sheet)
{
    MixCache(sheet);
    const uint32_t block = Field<uint32_t>(sheet, 0), pixels = Field<uint32_t>(sheet, 4);
    Mix(pixels ? pixels - block : 0);
    if (!pixels)
        return;
    const uint32_t width = Field<uint32_t>(sheet, 8), height = Field<uint32_t>(sheet, 0xC);
    const uint32_t pitch = Field<uint32_t>(sheet, 0x10);
    for (uint32_t row = 0; row < height; ++row)
        MixBytes(At32(pixels + row * pitch), width);
}

// A glyph GetGlyph answered: its entry (the character, the box, three of
// the four words; the last is whatever the game's stack held).
void MixGlyph(uint32_t glyph)
{
    if (!glyph) {
        Mix(0xFFFFFFFF);
        return;
    }
    MixBytes(At32(glyph - 4), 0x1A);
}

void SetField(void* f, std::size_t offset, uint32_t value) { std::memcpy(static_cast<uint8_t*>(f) + offset, &value, 4); }

void ClearCaches(void* f)
{
    Game<SThis>(addr::GZFont_CacheClear)(static_cast<uint8_t*>(f) + 0x104, nullptr);
    Game<SThis>(addr::GZFont_CacheClear)(static_cast<uint8_t*>(f) + 0x130, nullptr);
}
} // namespace fontglyph

void DriveFontGlyphs()
{
    using namespace fontglyph;
    const auto underline = Game<SThis>(addr::GZFont_UnderlineRows);
    const auto strike = Game<SThis>(addr::GZFont_StrikeRows);
    const auto grow = Game<SThree>(addr::GZFont_GrowSheet);
    const auto store = Game<StoreFn>(addr::GZFont_StoreGlyph);
    const auto getGlyph = Game<SOne>(addr::GZFont_GetGlyph);
    const auto render = Game<STwo>(addr::GZFont_RenderGlyph);
    const auto operatorDelete = Game<void(__cdecl*)(void*)>(addr::SC4_operator_delete);

    // The lines' thickness.
    {
        void* const f = NewFont();
        for (uint32_t size = 0; size < 0x60; ++size) {
            SetField(f, 0x38, size);
            Mix(underline(f, nullptr));
            Mix(strike(f, nullptr));
        }
        for (const uint32_t size : {0xFFu, 0x800u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
            SetField(f, 0x38, size);
            Mix(underline(f, nullptr));
            Mix(strike(f, nullptr));
        }
        SetField(f, 0x38, 0);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }

    // Boxes (a product that is not positive is never written).
    for (int32_t w = -2; w <= 9; ++w) {
        for (int32_t h = -2; h <= 9; ++h) {
            if ((w < 0) != (h < 0) && w != 0 && h != 0)
                continue;
            const uint32_t block = CallBox(addr::GZFont_BoxGlyph, w, h);
            if (w > 0 && h > 0)
                MixBytes(At32(block), static_cast<std::size_t>(w * h));
            operatorDelete(At32(block));
        }
    }
    for (const auto& [w, h] : {std::pair<int32_t, int32_t>{32, 1}, {1, 40}, {64, 64}}) {
        const uint32_t block = CallBox(addr::GZFont_BoxGlyph, w, h);
        MixBytes(At32(block), static_cast<std::size_t>(w * h));
        operatorDelete(At32(block));
    }

    // Sheets of our own, grown with their rows written in between.
    {
        uint8_t sheet[0x18] = {};
        Random random{0x5EE7A11Cu};
        const auto fill = [&] {
            const uint32_t pixels = Field<uint32_t>(sheet, 4);
            for (uint32_t row = 0; row < Field<uint32_t>(sheet, 0xC); ++row) {
                for (uint32_t i = 0; i < Field<uint32_t>(sheet, 8); ++i)
                    At32(pixels + row * Field<uint32_t>(sheet, 0x10))[i] = static_cast<uint8_t>(random.Range(0, 255));
            }
        };
        for (const auto& [w, h] : {std::pair<uint32_t, uint32_t>{0, 0}, {5, 3}, {4, 2}, {5, 3}, {9, 3}, {9, 7},
                                   {3, 10}, {12, 10}, {12, 11}, {40, 16}}) {
            MixByte(static_cast<uint8_t>(grow(nullptr, nullptr, U(sheet), w, h)));
            MixSheet(sheet);
            if (Field<uint32_t>(sheet, 4))
                fill();
        }
        operatorDelete(At32(Field<uint32_t>(sheet, 0)));
    }

    // StoreGlyph into sheets of our own.
    {
        Random random{0x51E0F00Du};
        uint8_t bits[64 * 24];
        for (uint8_t& b : bits)
            b = static_cast<uint8_t>(random.Range(0, 255));
        uint8_t levels[256];
        for (uint32_t i = 0; i < 256; ++i)
            levels[i] = static_cast<uint8_t>(255 - i / 2);
        void* const f = NewFont();
        const uint32_t savedLevels = Field<uint32_t>(f, 0x5C), savedEnd = Field<uint32_t>(f, 0x60);
        for (const uint32_t style : {0u, 4u, 8u, 0xCu}) {
            for (const uint32_t mode : {1u, 2u}) {
                for (const bool table : {false, true}) {
                    if (table && mode == 1)
                        continue;
                    SetField(f, 0x40, style);
                    SetField(f, 0x4C, mode);
                    SetField(f, 0x5C, table ? U(levels) : savedLevels);
                    SetField(f, 0x60, table ? U(levels + 256) : savedLevels);
                    for (const uint32_t lineHeight : {14u, 30u}) {
                        SetField(f, 0x38, lineHeight - 2);
                        SetField(f, 0x3C, lineHeight);
                        SetField(f, 0x84, lineHeight * 3 / 4);
                        uint8_t sheet[0x18] = {};
                        int32_t x = 0;
                        for (uint32_t k = 0; k < 24; ++k) {
                            const uint32_t format = k % 3 == 0 ? 2u : 0x100u;
                            const uint32_t width = static_cast<uint32_t>(random.Range(0, 13));
                            const uint32_t height = static_cast<uint32_t>(random.Range(0, 12));
                            const uint32_t pitch =
                                (format == 2 ? (width + 7) / 8 : width) + static_cast<uint32_t>(random.Range(0, 3));
                            const uint32_t y = k % 5 == 4 ? static_cast<uint32_t>(random.Range(0, 20)) : 0u;
                            int32_t box[6] = {x, 0, x + static_cast<int32_t>(width), static_cast<int32_t>(height), 0,
                                              0};
                            const int16_t words[4] = {static_cast<int16_t>(random.Range(-3, 3)),
                                                      static_cast<int16_t>(random.Range(-4, 20)),
                                                      static_cast<int16_t>(random.Range(-2, 16)), 0};
                            std::memcpy(&box[4], words, sizeof words);
                            MixByte(static_cast<uint8_t>(
                                store(f, nullptr, U(sheet), U(box), x, y, U(bits), width, height, pitch, format)));
                            MixSheet(sheet);
                            MixBytes(reinterpret_cast<const uint8_t*>(box), sizeof box);
                            x = Field<int32_t>(sheet, 0x14);
                        }
                        // An empty glyph, and one at column 0.
                        int32_t box[6] = {x, 0, x, 0, 0x00050002, 0};
                        MixByte(static_cast<uint8_t>(store(f, nullptr, U(sheet), U(box), x, 0, 0, 0, 0, 0, 2)));
                        MixSheet(sheet);
                        MixBytes(reinterpret_cast<const uint8_t*>(box), sizeof box);
                        int32_t first[6] = {0, 0, 5, 6, 0x00040001, 0x7};
                        MixByte(static_cast<uint8_t>(
                            store(f, nullptr, U(sheet), U(first), 0, 1, U(bits), 5, 6, 5, 0x100)));
                        MixSheet(sheet);
                        MixBytes(reinterpret_cast<const uint8_t*>(first), sizeof first);
                        operatorDelete(At32(Field<uint32_t>(sheet, 0)));
                    }
                }
            }
        }
        SetField(f, 0x40, 0);
        SetField(f, 0x5C, savedLevels);
        SetField(f, 0x60, savedEnd);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }

    // Glyphs of loaded fonts.
    std::vector<uint32_t> characters;
    for (uint32_t c = 0x20; c < 0x7F; ++c)
        characters.push_back(c);
    for (const uint32_t c : {0u, 0x1Fu, 0xA0u, 0xE9u, 0x0E01u, 0x0E33u, 0x4E00u, 0x200Bu, 0xFEFFu, 0xF701u, 0xFFFFu,
                             0x10041u, 0x41u})
        characters.push_back(c);
    const auto glyphs = [&](void* f) {
        for (const uint32_t c : characters) {
            MixGlyph(getGlyph(f, nullptr, c));
            MixGlyph(getGlyph(f, nullptr, c));
        }
        MixSheet(static_cast<uint8_t*>(f) + 0x104);
        MixSheet(static_cast<uint8_t*>(f) + 0x130);
    };
    const std::vector<std::string> paths = FontPaths();
    for (const std::size_t i : {std::size_t{3}, std::size_t{5}, std::size_t{13}, std::size_t{19}, std::size_t{24}}) {
        if (i >= paths.size())
            continue;
        void* const f = NewFont();
        Virtual<uint32_t>(f, 0x4);
        Game<SOne>(addr::GZFont_SetSize)(f, nullptr, 16);
        SetFontPath(f, paths[i]);
        MixByte(static_cast<uint8_t>(Game<SThis>(addr::GZFont_Load)(f, nullptr)));
        if (!Field<uint8_t>(f, 4)) {
            Game<SThis>(addr::GZFont_Release)(f, nullptr);
            continue;
        }
        const uint32_t extra = Field<uint32_t>(f, 0x70);
        for (const uint32_t mode : {1u, 2u}) {
            Game<SOne>(addr::GZFont_SetMode)(f, nullptr, mode);
            for (const uint32_t style : {0u, 4u, 8u, 0xCu}) {
                SetField(f, 0x40, style);
                ClearCaches(f);
                glyphs(f);
            }
            SetField(f, 0x40, 0);
            for (const float scale : {0.99f, 0.98f, 0.9801f, 0.5f, 1.5f, 1.0f,
                                      std::numeric_limits<float>::quiet_NaN()}) {
                std::memcpy(static_cast<uint8_t*>(f) + 0x70, &scale, 4);
                ClearCaches(f);
                glyphs(f);
            }
            SetField(f, 0x70, extra);
        }
        // The space just under 1.0 at every size from 6 to 48 (advances
        // with a fraction reach its pixel less).
        for (uint32_t size = 6; size <= 48; ++size) {
            for (const float scale : {0.99f, 0.999f, 0.985f}) {
                ClearCaches(f);
                Game<SOne>(addr::GZFont_SetSize)(f, nullptr, size);
                std::memcpy(static_cast<uint8_t*>(f) + 0x70, &scale, 4);
                MixGlyph(getGlyph(f, nullptr, 0x20));
                MixGlyph(getGlyph(f, nullptr, 0x41));
            }
        }
        SetField(f, 0x70, extra);
        ClearCaches(f);
        Game<SOne>(addr::GZFont_SetSize)(f, nullptr, 16);
        // No fallback: characters without a glyph get the box.
        SetField(f, 0xE8, 0);
        ClearCaches(f);
        glyphs(f);
        // RenderGlyph again for characters the cache has (the table keeps
        // the first) and into a cache of our own.
        uint8_t* const cache = static_cast<uint8_t*>(f) + (Field<uint32_t>(f, 0x4C) == 1 ? 0x104 : 0x130);
        for (const uint32_t c : {0x41u, 0x20u, 0xFFFFu})
            MixGlyph(render(f, nullptr, c, U(cache)));
        MixSheet(cache);
        uint8_t own[kCacheBytes];
        Game<SThis>(addr::GZFont_CacheConstruct)(own, nullptr);
        for (const uint32_t c : {0x41u, 0x67u, 0x200Bu, 0xFFFFu})
            MixGlyph(render(f, nullptr, c, U(own)));
        MixSheet(own);
        Game<SThis>(addr::GZFont_CacheDestroy)(own, nullptr);
        ClearCaches(f);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }
    // A font with no face: every glyph is a box.
    {
        void* const f = NewFont();
        Virtual<uint32_t>(f, 0x4);
        for (const uint32_t lineHeight : {0u, 1u, 13u}) {
            SetField(f, 0x3C, lineHeight);
            ClearCaches(f);
            glyphs(f);
        }
        ClearCaches(f);
        Game<SThis>(addr::GZFont_Release)(f, nullptr);
    }
    FtStage("fontglyphs");
}

void DriveFont()
{
    DriveFontObject();
    DriveFontProperties();
    DriveFontBlitter();
    DriveFontContainers();
    DriveFontLoad();
    DriveFontText();
    DriveFontGlyphs();
}

// OPENSC4_TEST_FONTS=<day>: the 11 by address and real fonts' strings,
// then the holder, its stream and the director (f1 and f5), then the font
// system (f2a), then the font's object, properties and blitter (f3a)
// and its containers (f3b).
// Every result goes into the fingerprint's `ft` part.
void DriveFontsDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_FONTS") || !game::IsBuild641())
        return;
    g_ftOn = true;
    DriveTables();
    DriveBlends();
    DriveObject();
    DriveGlyphs();
    DriveStrings();
    DriveHolder();
    DriveDirector();
    DriveSystem();
    DriveFont();
    log::Write("autotest: fonts driven (hash %08X)", g_ftHash);
}

void FtFingerprint(cISC4City*, std::string& line)
{
    if (!g_ftOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | ft %08X", g_ftHash);
    line += text;
}

const FingerprintPart kFtFingerprint{"fonts", 6, &FtFingerprint};
const DayHook kFtDrive{"fonts", 6, &DriveFontsDay};

} // namespace
} // namespace opensc4::autotest
