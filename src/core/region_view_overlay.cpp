// The region screen's overlay window (docs/region.md), 0x007A9880 to
// 0x007AA0E0, and the two picture filter helpers after it. Name ours,
// SC4RegionOverlayWin: a window of 0x140 bytes (main vtable 0x00AB88C0, 151
// slots, the window base class's layout; own slots 0x10 Init, 0x14 Shutdown,
// 0x160 the paint slot, 0x250 the deleting destructor) that the region
// screen keeps at +0xE4. It holds a list of marks, each one of four textures
// (0x4A624656 to 0x4A624659) with a place, a speed along the window's
// direction and an alpha: the marks start at the left edge of the bounds or,
// when the bounds are set, anywhere in them, drift along the direction and
// are dropped past the right edge.
//
// Replacement, off by default:
//
//   region.view.overlay: the constructor and destructors, Init, Shutdown,
//     the setters (offset, alpha range, direction, bounds), the function that
//     makes a mark, and the two filter helpers the region screen's picture
//     shift uses (the tent weight 0x007AA0E0 and the row mixer 0x007AA110).
//
// Still the game's: 0x007A9D60, the paint slot. It is drawing code (render
// states, four vertices on the stack, a texture and a draw call per mark),
// and it also does the marks' step: a new mark with chance 0.003 a frame,
// each mark moved by its speed times the seconds since the last frame (the
// stopwatch at +0xD8, at most 0.2), marks at or past the right edge freed.
// It calls our mark maker through the hook.
//
// 0x007A9800 is not a function start (it is the tail of 0x007A97D0), and
// 0x007A9810 is the message handler of the window class before this one
// (main vtable 0x00AB8628; 0x007A9810 is slot 2 of its part at +0xDC, vtable
// 0x00AB8614, and calls that class's 0x007A93E0 on this - 0xDC): neither is
// touched here.
//
// The window base class, the stopwatch, the random generator, the texture
// factory and its bindings are the game's, called by vtable offset or address.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::region {
namespace {

namespace overlay { // SC4RegionOverlayWin
constexpr std::size_t kStopwatch = 0xD8; // 0x18 bytes, mode 4: the time between two paints
constexpr std::size_t kOffsetX = 0xF0;   // int32: taken from a mark's place when it is drawn
constexpr std::size_t kOffsetY = 0xF4;
constexpr std::size_t kLeft = 0xF8;      // floats: the bounds
constexpr std::size_t kTop = 0xFC;
constexpr std::size_t kRight = 0x100;
constexpr std::size_t kBottom = 0x104;
constexpr std::size_t kDirectionX = 0x108; // floats, length 1
constexpr std::size_t kDirectionY = 0x10C;
constexpr std::size_t kAlphaLow = 0x110; // bytes: a mark's alpha is drawn between them
constexpr std::size_t kAlphaHigh = 0x111;
constexpr std::size_t kMarks = 0x114;    // list: the sentinel node
constexpr std::size_t kTextures = 0x118; // four texture bindings
constexpr std::size_t kRandom = 0x128;   // the generator, 0xC bytes; the object is 0x140 bytes
} // namespace overlay

namespace mark { // a mark, 0x18 bytes; in a list node at +8
constexpr std::size_t kX = 0x0; // floats
constexpr std::size_t kY = 0x4;
constexpr std::size_t kSpeedX = 0x8; // pixels a second
constexpr std::size_t kSpeedY = 0xC;
constexpr std::size_t kTexture = 0x10; // 0 to 3
constexpr std::size_t kAlpha = 0x14;   // byte
constexpr std::size_t kSize = 0x18;
} // namespace mark

namespace win { // the window base class, by vtable offset
constexpr std::size_t kMainWindow = 0x28;
constexpr std::size_t kWidth = 0xA4;
constexpr std::size_t kHeight = 0xA8;
constexpr std::size_t kSetArea = 0xDC; // (left, top, right, bottom)
constexpr std::size_t kSetId = 0x100;  // (id)
} // namespace win

constexpr uint32_t kOverlayWinId = 0x6A0AF41D;
constexpr uint32_t kTextureFactoryClass = 0x1AC0E11A; // cIS3DTextureBindingFactory
constexpr uint32_t kTextureFactoryInterface = 0xFAC0E219;
constexpr uint32_t kFirstTexture = 0x4A624656; // and the three after it
constexpr std::size_t kTextureCount = 4;
constexpr std::size_t kNodeSize = 0x20; // next, previous, the mark

// The float at 0x00AB7E10: a mark's side in pixels, and how far left of and
// above the bounds a mark may start.
constexpr double kMarkSide = 128.0;
// The double at 0x00AB3AD0: a mark's speed is 5 to 10 pixels a second.
constexpr double kSpeedScale = 5.0;
// The double at 0x00AB8B20: one mark per 16384 square pixels of the bounds.
constexpr double kMarksPerPixel = 1.0 / 16384.0;

static_assert(overlay::kTextures + kTextureCount * 4 == overlay::kRandom);

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The generator's draw in 0 to 1 (0x0091372E), left on the x87 stack.
double NextFloat(char* self)
{
    return This<double>(addr::cRZRandom_NextFloat, self + overlay::kRandom);
}

// What Shutdown does, also inlined in the destructor.
void ShutDownWindow(char* self)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        This<void>(addr::GZWin_SetInitialized, self, uint32_t{0});
        This<uint8_t>(addr::SC4Win_F99D2FE, self); // the base class's Shutdown
    }
}

// The game's 0x007A9880: Shutdown, slot 0x14.
uint8_t __fastcall Shutdown(char* self, void*)
{
    ShutDownWindow(self);
    return 1;
}

// The game's 0x007A98A0: the pair taken from every mark's place when the
// marks are drawn (the region screen gives the view's place on screen).
void __fastcall SetOffset(char* self, void*, const uint32_t* point)
{
    At<uint32_t>(self, overlay::kOffsetX) = point[0];
    At<uint32_t>(self, overlay::kOffsetY) = point[1];
}

// The game's 0x007A98C0: the range new marks' alpha is drawn from. Only the
// low byte of each argument is read.
void __fastcall SetFlags(char* self, void*, uint32_t low, uint32_t high)
{
    const uint8_t first = static_cast<uint8_t>(low);
    const uint8_t second = static_cast<uint8_t>(high);
    At<uint8_t>(self, overlay::kAlphaLow) = first;
    At<uint8_t>(self, overlay::kAlphaHigh) = second;
}

// The game's 0x007A98E0: fills a new mark (0x18 bytes at `out`): at the left
// edge less the mark's side, at a random height, with a random speed along
// the direction, one of the four textures and an alpha in the range. Four
// draws, in this order: two floats, then two integers.
void __fastcall MakeMark(char* self, void*, char* out)
{
    At<float>(out, mark::kX) = static_cast<float>(static_cast<double>(At<float>(self, overlay::kLeft)) - kMarkSide);

    const double down = NextFloat(self);
    const double height =
        static_cast<double>(At<float>(self, overlay::kBottom)) - static_cast<double>(At<float>(self, overlay::kTop));
    At<float>(out, mark::kY) =
        static_cast<float>(down * height + (static_cast<double>(At<float>(self, overlay::kTop)) - kMarkSide));

    const double speed = (NextFloat(self) + 1.0) * kSpeedScale;
    At<float>(out, mark::kSpeedX) =
        static_cast<float>(speed * static_cast<double>(At<float>(self, overlay::kDirectionX)));
    At<float>(out, mark::kSpeedY) =
        static_cast<float>(speed * static_cast<double>(At<float>(self, overlay::kDirectionY)));

    At<uint32_t>(out, mark::kTexture) =
        This<uint32_t>(addr::cRZRandom_Between, self + overlay::kRandom, uint32_t{0}, uint32_t{kTextureCount});
    const uint32_t high = At<uint8_t>(self, overlay::kAlphaHigh);
    const uint32_t low = At<uint8_t>(self, overlay::kAlphaLow);
    At<uint8_t>(out, mark::kAlpha) =
        static_cast<uint8_t>(This<uint32_t>(addr::cRZRandom_Between, self + overlay::kRandom, low, high));
}

// The game's 0x007A9980: the direction the marks drift in, scaled to length
// 1. A zero vector gives what the division gives (not checked).
void __fastcall SetDirection(char* self, void*, float x, float y)
{
    const double wideX = static_cast<double>(x);
    const double wideY = static_cast<double>(y);
    const double scale = 1.0 / std::sqrt(wideX * wideX + wideY * wideY);
    At<float>(self, overlay::kDirectionX) = static_cast<float>(wideX * scale);
    At<float>(self, overlay::kDirectionY) = static_cast<float>(scale * wideY);
}

// The game's 0x007A99C0: Init, slot 0x10. Sets the window's ID, gets the four
// textures from the texture factory, covers the main window and takes its own
// size as the bounds.
uint8_t __fastcall Init(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<uint8_t>(addr::GZWin_Init, self);
    Virtual<void>(self, win::kSetId, kOverlayWinId);

    void* factory = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kTextureFactoryClass, kTextureFactoryInterface, &factory);
    if (factory) {
        for (uint32_t index = 0; index < kTextureCount; ++index) {
            void** const slot = &At<void*>(self, overlay::kTextures + index * 4);
            void* const old = *slot;
            void* const from = factory;
            if (old) {
                *slot = nullptr;
                This<uint32_t>(addr::SC4TextureBinding_Release, old);
            }
            Virtual<uint8_t>(from, 0x14, uint32_t{kFirstTexture + index}, slot); // GetBinding(id, out)
        }
    }

    void* const forHeight = Virtual<void*>(self, win::kMainWindow);
    void* const forWidth = Virtual<void*>(self, win::kMainWindow);
    // The original reads its own vtable before it asks for the two sizes.
    const std::uintptr_t table = At<std::uintptr_t>(self, 0);
    const int32_t mainHeight = Virtual<int32_t>(forHeight, win::kHeight);
    const int32_t mainWidth = Virtual<int32_t>(forWidth, win::kWidth);
    using SetAreaFn = void(__thiscall*)(void*, int32_t, int32_t, int32_t, int32_t);
    reinterpret_cast<SetAreaFn>(*reinterpret_cast<std::uintptr_t*>(table + win::kSetArea))(
        self, int32_t{0}, int32_t{0}, mainWidth, mainHeight);

    At<uint32_t>(self, overlay::kLeft) = 0;
    const int32_t width = Virtual<int32_t>(self, win::kWidth);
    At<uint32_t>(self, overlay::kTop) = 0;
    At<float>(self, overlay::kRight) = static_cast<float>(width);
    const int32_t height = Virtual<int32_t>(self, win::kHeight);
    At<float>(self, overlay::kBottom) = static_cast<float>(height);

    This<void>(addr::SC4RefPtr_Destruct, &factory);
    return 1;
}

// The game's 0x007A9AE0: the constructor.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4RegionOverlayWin_Vtable;
    This<void*>(addr::SC4Stopwatch_Construct, self + overlay::kStopwatch, int32_t{4});
    At<uint32_t>(self, overlay::kOffsetX) = 0;
    At<uint32_t>(self, overlay::kOffsetY) = 0;
    At<uint8_t>(self, overlay::kAlphaLow) = 0x20;
    At<uint8_t>(self, overlay::kAlphaHigh) = 0x40;
    At<void*>(self, overlay::kMarks) = nullptr;
    char* const sentinel = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{kNodeSize});
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    At<char*>(self, overlay::kMarks) = sentinel;
    for (std::size_t index = 0; index < kTextureCount; ++index)
        At<void*>(self, overlay::kTextures + index * 4) = nullptr;
    {
        // The clock seed counts under the original's call site.
        const fixed_seed::SiteOverride site(0x007A9B66);
        This<void*>(addr::SC4Random_Construct, self + overlay::kRandom, int32_t{-1});
    }
    This<void>(addr::SC4Stopwatch_ConstructMode, self + overlay::kStopwatch, int32_t{4});
    This<uint32_t>(addr::GZWin_SetFlagDirect, self, uint32_t{0x10000}, uint32_t{0});
    This<uint32_t>(addr::GZWin_SetFlagDirect, self, uint32_t{0x200000}, uint32_t{1});
    return self;
}

// The game's 0x007A9B90: the destructor. It ends in a jump to the base
// class's, whose result it so returns. The texture slots are released from
// the last to the first and not cleared.
void* __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4RegionOverlayWin_Vtable;
    ShutDownWindow(self);
    for (std::size_t index = kTextureCount; index-- > 0;) {
        if (void* const texture = At<void*>(self, overlay::kTextures + index * 4))
            This<uint32_t>(addr::SC4TextureBinding_Release, texture);
    }
    This<void>(addr::SC4PairList_Destroy, self + overlay::kMarks);
    This<void>(addr::SC4Stopwatch_Destruct, self + overlay::kStopwatch);
    return This<void*>(addr::GZWin_Destruct, self);
}

// The game's 0x007A9C00: the deleting destructor, slot 0x250.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (static_cast<uint8_t>(flags) & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x007A9C20: the bounds, as four floats (copied as dwords). The
// marks are freed and the bounds filled afresh, one mark per 16384 square
// pixels, each made as a new one and then put at a random x in the bounds.
// A count below zero is not guarded against: the loop only ends at zero.
void __fastcall SetBounds(char* self, void*, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
{
    At<uint32_t>(self, overlay::kLeft) = left;
    At<uint32_t>(self, overlay::kTop) = top;
    At<uint32_t>(self, overlay::kRight) = right;
    At<uint32_t>(self, overlay::kBottom) = bottom;

    for (char* node = At<char*>(At<char*>(self, overlay::kMarks), 0); node != At<char*>(self, overlay::kMarks);) {
        char* const gone = node;
        node = At<char*>(node, 0);
        Cdecl<void>(addr::SC4_Free, gone);
    }
    At<char*>(At<char*>(self, overlay::kMarks), 0) = At<char*>(self, overlay::kMarks);
    At<char*>(At<char*>(self, overlay::kMarks), 4) = At<char*>(self, overlay::kMarks);

    const double height =
        static_cast<double>(At<float>(self, overlay::kBottom)) - static_cast<double>(At<float>(self, overlay::kTop));
    const double width =
        static_cast<double>(At<float>(self, overlay::kRight)) - static_cast<double>(At<float>(self, overlay::kLeft));
    int32_t count = Truncate(height * width * kMarksPerPixel);
    if (count == 0)
        return;
    do {
        char fresh[mark::kSize]; // the alpha's three padding bytes stay as they are, as in the original
        MakeMark(self, nullptr, fresh);
        const double across = NextFloat(self);
        const double span = static_cast<double>(At<float>(self, overlay::kRight))
            - static_cast<double>(At<float>(self, overlay::kLeft)) - kMarkSide;
        char* const sentinel = At<char*>(self, overlay::kMarks);
        At<float>(fresh, mark::kX) =
            static_cast<float>(across * span + (static_cast<double>(At<float>(self, overlay::kLeft)) - kMarkSide));

        char* const node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{kNodeSize});
        std::memcpy(node + 8, fresh, mark::kSize);
        char* const last = At<char*>(sentinel, 4);
        At<char*>(node, 0) = sentinel;
        At<char*>(node, 4) = last;
        At<char*>(last, 0) = node;
        At<char*>(sentinel, 4) = node;
    } while (--count != 0);
}

// The game's 0x007AA0E0: the tent weight, 1 - |x| below 1 and else 0 (a NaN
// gives 0). The result stays on the x87 stack. The region screen's picture
// shift hands it to the weight table maker 0x007AA860 as a function pointer.
double __cdecl TentFilter(double x)
{
    const double size = std::fabs(x);
    if (size < 1.0)
        return 1.0 - size;
    return 0.0;
}

// One channel of the row mixer: rounded, 14 bits dropped, clamped to 0..255.
uint32_t MixedChannel(uint32_t sum)
{
    const int32_t value = static_cast<int32_t>(sum + 0x2000) >> 14;
    if (static_cast<uint32_t>(value) >= 0x100)
        return static_cast<uint8_t>(~(value >> 31));
    return static_cast<uint32_t>(value);
}

// The game's 0x007AA110: fills `count` pixels of `to`; each is, per byte, the
// pixel of `from` at the position times weights[0] plus the next one times
// weights[1] (14-bit fixed point). The position starts at `start` (16.16) and
// moves a whole pixel a step; pixels outside 0 to fromCount - 1 count as 0.
void __cdecl MixRowAcross(uint32_t* to, const uint32_t* from, int32_t count, int32_t fromCount,
    const int32_t* weights, int32_t start)
{
    if (count <= 0)
        return;
    uint32_t position = static_cast<uint32_t>(start);
    uint32_t edge[2];
    do {
        int32_t at = static_cast<int32_t>(position) >> 16;
        const uint32_t* pair = reinterpret_cast<const uint32_t*>(
            reinterpret_cast<std::uintptr_t>(from) + static_cast<std::uintptr_t>(static_cast<uint32_t>(at) * 4u));
        if (at < 0 || at > static_cast<int32_t>(static_cast<uint32_t>(fromCount) - 2u)) {
            const uint32_t* source = pair;
            for (std::size_t index = 0; index < 2; ++index) {
                if (at < 0)
                    edge[index] = 0;
                else if (at >= fromCount)
                    edge[index] = 0;
                else
                    edge[index] = *source;
                ++at;
                ++source;
            }
            pair = edge;
        }

        const uint32_t first = pair[0];
        const uint32_t firstWeight = static_cast<uint32_t>(weights[0]);
        const uint32_t second = pair[1];
        const uint32_t secondWeight = static_cast<uint32_t>(weights[1]);
        const uint32_t byte3 = MixedChannel((first >> 24) * firstWeight + (second >> 24) * secondWeight);
        const uint32_t byte2 =
            MixedChannel(((first >> 16) & 0xFF) * firstWeight + ((second >> 16) & 0xFF) * secondWeight);
        const uint32_t byte1 =
            MixedChannel(((first >> 8) & 0xFF) * firstWeight + ((second >> 8) & 0xFF) * secondWeight);
        const uint32_t byte0 = MixedChannel((first & 0xFF) * firstWeight + (second & 0xFF) * secondWeight);
        *to = (((((byte3 << 8) + byte2) << 8) + byte1) << 8) + byte0;
        ++to;
        position += 0x10000;
    } while (--count != 0);
}

bool InstallRegionViewOverlay(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[12] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4RegionOverlayWin_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4RegionOverlayWin_SetOffset, reinterpret_cast<void*>(&SetOffset)},
        {addr::SC4RegionOverlayWin_SetFlags, reinterpret_cast<void*>(&SetFlags)},
        {addr::SC4RegionOverlayWin_MakeMark, reinterpret_cast<void*>(&MakeMark)},
        {addr::SC4RegionOverlayWin_SetDirection, reinterpret_cast<void*>(&SetDirection)},
        {addr::SC4RegionOverlayWin_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4RegionOverlayWin_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4RegionOverlayWin_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4RegionOverlayWin_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::SC4RegionOverlayWin_SetBounds, reinterpret_cast<void*>(&SetBounds)},
        {addr::SC4Image_TentFilter, reinterpret_cast<void*>(&TentFilter)},
        {addr::SC4Image_MixRowAcross, reinterpret_cast<void*>(&MixRowAcross)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_regionViewOverlayRegistered = registry::Register({
    "region.view.overlay",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RegionOverlayWin_Construct,
    "Our region overlay window: constructor, Init, Shutdown, setters, the mark maker; the two picture filter helpers",
    false,
    InstallRegionViewOverlay,
});

} // namespace
} // namespace opensc4::region
