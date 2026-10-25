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
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
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

void DriveFont()
{
    DriveFontObject();
    DriveFontProperties();
    DriveFontBlitter();
}

// OPENSC4_TEST_FONTS=<day>: the 11 by address and real fonts' strings,
// then the holder, its stream and the director (f1 and f5), then the font
// system (f2a), then the font's object, properties and blitter (f3a).
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
