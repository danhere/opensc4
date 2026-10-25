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
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
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

// OPENSC4_TEST_FONTS=<day>: the 11 by address and real fonts' strings,
// then the holder, its stream and the director (f1 and f5).
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
