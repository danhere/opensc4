// The region screen's windows (docs/region.md), part 2: 0x007AB630 to
// 0x007AC830. Four classes have code here:
//
//   - the region screen itself, cSC4WinRegionScreen (window class 0xEA659793,
//     main vtable 0x00AB9260 with 151 slots, the window base class's layout):
//     its message handler (slot 3), three mouse slots (0x21C, 0x228, 0x238)
//     and the helpers its Init, Shutdown and command handler call: laying the
//     map out, loading the tuning exemplar, the stock pictures of an empty
//     tile, the three side panels, the view position kept in the
//     preferences, the 3D view's centre and its teardown;
//   - the scroll window (name ours; main vtable 0x00AB8CD0, kept by the
//     screen at +0x174): its deleting destructor and the function that puts
//     its marker picture where the player clicked;
//   - the terrain overlay (name ours; 0x14 bytes, vtables 0x00AB8CB8 and
//     0x00AB8CA0 at +4): its constructor;
//   - two pieces of the sort the screen orders layout tiles with (12 bytes
//     each: x, y, log2 of the side), with the ordering inlined.
//
// Replacement, off by default:
//
//   region.screen.part2: everything above.
//
// Still the game's: 0x007AB630, the overlay's draw (slot 3 of 0x00AB8CB8: it
// sets a render state, fills four vertices and draws them; drawing belongs
// to the renderer). 0x007ABB50 is not a function: it is the out-of-line tail
// of 0x007ABB00.
//
// Windows, controls, pictures, the camera and the region map window
// (cSC4WinRegionView, 0x007B2440 on) are the game's, called by vtable offset
// or address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace screen { // cSC4WinRegionScreen
constexpr std::size_t kView = 0xE0;       // the region map window (cSC4WinRegionView)
constexpr std::size_t kOverlayWin = 0xE4; // the overlay window (vtable 0x00AB88C0)
constexpr std::size_t kPanels = 0xEC;     // three side panels
constexpr std::size_t kPanelParts = 0xF8; // and a second window of each
constexpr std::size_t kKeys = 0x108;      // the key table: slot 0x28 turns a key into a command
constexpr std::size_t kPictures = 0x124;  // stock pictures: [terrain type 0, 1][size 0 to 2][picture, mask]
constexpr std::size_t kPart158 = 0x158;   // parts of the 3D view, shut down by slot 0x10
constexpr std::size_t kPart15C = 0x15C;
constexpr std::size_t kPart160 = 0x160;
constexpr std::size_t kCamera = 0x164;    // the camera control
constexpr std::size_t kRenderer = 0x168;  // slot 0x20 gives the camera
constexpr std::size_t kTerrain = 0x16C;   // also the global 0x00B43CF8
constexpr std::size_t kScroller = 0x174;  // the scroll window
constexpr std::size_t kScrollX = 0x178;   // floats
constexpr std::size_t kScrollY = 0x17C;
constexpr std::size_t kMapLeft = 0x180;   // the map's rectangle in the window, pixels
constexpr std::size_t kMapTop = 0x184;
constexpr std::size_t kMapRight = 0x188;
constexpr std::size_t kMapBottom = 0x18C;
constexpr std::size_t kMarginX = 0x198;
constexpr std::size_t kMarginY = 0x19C;
constexpr std::size_t kBusy = 0x1A0;      // byte: the map takes no mouse
constexpr std::size_t kHeight0 = 0x1A1;   // bytes, copied to the region's terrain height table
constexpr std::size_t kHeight1 = 0x1A2;
constexpr std::size_t kRegionIndex = 0x1A4;
constexpr std::size_t kMode = 0x1A8;
constexpr std::size_t kViewValueA = 0x1B0; // copied to the map window's +0xF8
constexpr std::size_t kViewValueB = 0x1B4; // and +0xFC
constexpr std::size_t kTuning1EC = 0x1EC;  // floats
constexpr std::size_t kTuning1F0 = 0x1F0;
} // namespace screen

namespace scroller { // the scroll window
constexpr std::size_t kX = 0xDC;
constexpr std::size_t kY = 0xE0;
constexpr std::size_t kPlaced = 0x109;
constexpr std::size_t kImage = 0x10C;
constexpr std::size_t kTuning110 = 0x110;
constexpr std::size_t kTuning114 = 0x114;
constexpr std::size_t kTuning11C = 0x11C; // float
constexpr std::size_t kStopwatch = 0x120;
} // namespace scroller

namespace win { // the window base class, by vtable offset
constexpr std::size_t kAsWin = 0xC;         // of a window's cIGZWin* part holder
constexpr std::size_t kOwner = 0x18;        // the object at +4
constexpr std::size_t kIsChild = 0x38;      // (window)
constexpr std::size_t kToFront = 0x68;      // (window)
constexpr std::size_t kPlaceBy = 0x78;      // (window, 1)
constexpr std::size_t kChild = 0x88;        // (id)
constexpr std::size_t kChildAs = 0x94;      // (id, iid, void** out)
constexpr std::size_t kWidth = 0xA4;
constexpr std::size_t kHeight = 0xA8;
constexpr std::size_t kSetArea = 0xDC;      // (left, top, right, bottom)
constexpr std::size_t kSetFlag = 0x110;     // (flag, on)
constexpr std::size_t kShow = 0x114;
constexpr std::size_t kHide = 0x118;
constexpr std::size_t kIsVisible = 0x11C;
} // namespace win

namespace image { // cIGZBuffer, by vtable offset
constexpr std::size_t kLock = 0x18;      // (flags)
constexpr std::size_t kUnlock = 0x1C;    // (flags)
constexpr std::size_t kArea = 0x30;      // the rectangle: left, top, right, bottom
constexpr std::size_t kGetPixel = 0x54;  // (x, y)
constexpr std::size_t kSetPixel = 0x58;  // (x, y, colour)
constexpr std::size_t kSetKey = 0x5C;    // (colour)
constexpr std::size_t kColour = 0x78;    // (red, green, blue)
} // namespace image

constexpr uint32_t kMarkerClass = 0x48E945B4; // the marker's window class, and its ID as a child
constexpr uint32_t kPanelAnchor = 0x09EBE9EE;
constexpr uint32_t kPanelButtons[3] = {0x09EBF2BD, 0x09EBF2C8, 0x09EBF2C3}; // the table at 0x00AB91AC

constexpr uint32_t kMessageKey = 0x1B;
constexpr uint32_t kMessageMapBusy = 0x8A4BAC53;
constexpr uint32_t kMessageMapFree = 0x8A4BAC5B;
constexpr uint32_t kMessageScroll = 0xAAA1CDF2;

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};
static_assert(sizeof(ResourceKey) == 0xC);

// A layout tile as the screen sorts them.
struct Tile {
    int32_t x;
    int32_t y;
    int32_t size; // log2 of the side in km
};
static_assert(sizeof(Tile) == 0xC);

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The application (cISC4App): slot 0x88 the nation, 0x98 the preferences,
// 0x44 saves the preferences.
void* App()
{
    return Global<void*>(addr::SC4_Service43C94);
}

// The four floats at 0x00B0DBA4: the map's two axes in pixels per km, the x
// axis (90.51, 18.75) and the y axis (-37.49, 45.25).
float MapAxis(std::size_t index)
{
    return Global<float>(addr::SC4RegionScreen_DriftDirection + index * 4);
}

// A counted pointer takes another's object, as the original does inline.
void AssignRef(void** target, void** source)
{
    if (source == target)
        return;
    void* const fresh = *source;
    void* const old = *target;
    if (fresh == old)
        return;
    if (fresh)
        AddRef(fresh);
    *target = fresh;
    if (old)
        Release(old);
}

void ReleaseAndClear(void** slot)
{
    if (void* const object = *slot) {
        *slot = nullptr;
        Release(object);
    }
}

// One part of the 3D view: shut down by its slot 0x10, then released.
void ShutDownPart(char* self, std::size_t offset)
{
    void* part = At<void*>(self, offset);
    if (!part)
        return;
    Virtual<void>(part, 0x10);
    part = At<void*>(self, offset);
    if (part) {
        At<void*>(self, offset) = nullptr;
        Release(part);
    }
}

// The game's 0x007AB760: slot 0x228 of the screen (the mouse moved). The map
// window marks the tile under the point unless the map is busy.
uint8_t __fastcall ScreenMouseMove(char* self, void*, uint32_t x, uint32_t y, uint32_t)
{
    if (!At<uint8_t>(self, screen::kBusy))
        This<void>(addr::cSC4WinRegionView_PickAt, At<void*>(self, screen::kView), x, y);
    return 1;
}

// The game's 0x007AB790: slot 0x21C of the screen (a mouse button went
// down): handed to the scroll window's own slot. No check for a missing
// scroll window.
uint8_t __fastcall ScreenMouseDown(char* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Virtual<void>(At<void*>(self, screen::kScroller), 0x21C, a, b, c);
    return 1;
}

// The game's 0x007AB7C0. Lays the map out in the window: the size of the
// region's bounding box of tiles along the two map axes plus the margins,
// centred when it is smaller than the window, and the overlay window told
// the rectangle that is on screen.
void __fastcall LayOutMap(char* self, void*)
{
    void* const nation = Virtual<void*>(App(), 0x88);
    void* const region = Virtual<void*>(nation, 0x2C, At<uint32_t>(self, screen::kRegionIndex));
    int32_t box[4];
    Virtual<void>(region, 0x60, box);

    const int32_t columns = box[2] - box[0] + 1;
    const int32_t rows = box[3] - box[1] + 1;
    const double h = static_cast<double>(rows);
    const float w = static_cast<float>(columns);

    const int32_t marginX2 = static_cast<int32_t>(At<uint32_t>(self, screen::kMarginX) << 1);
    double sum = std::fabs(static_cast<double>(MapAxis(2))) * h;
    sum += std::fabs(static_cast<double>(MapAxis(0))) * static_cast<double>(w);
    sum += static_cast<double>(marginX2);
    int32_t extraWidth = Truncate(sum);

    const int32_t marginY2 = static_cast<int32_t>(At<uint32_t>(self, screen::kMarginY) << 1);
    sum = std::fabs(static_cast<double>(MapAxis(3))) * h;
    sum += std::fabs(static_cast<double>(MapAxis(1))) * static_cast<double>(w);
    sum += static_cast<double>(marginY2);
    int32_t extraHeight = Truncate(sum);

    const float originX = static_cast<float>(h * static_cast<double>(MapAxis(2)));

    int32_t half = Virtual<int32_t>(self, win::kWidth) / 2;
    half = static_cast<int32_t>(static_cast<uint32_t>(half) + At<uint32_t>(self, screen::kMarginX));
    int32_t left = Truncate(static_cast<double>(originX) - static_cast<double>(half));

    half = Virtual<int32_t>(self, win::kHeight) / 2;
    const int32_t above = static_cast<int32_t>(0u - At<uint32_t>(self, screen::kMarginY) - static_cast<uint32_t>(half));
    int32_t top = Truncate(static_cast<double>(above) - static_cast<double>(MapAxis(3)));

    if (extraWidth > Virtual<int32_t>(self, win::kWidth)) {
        extraWidth -= Virtual<int32_t>(self, win::kWidth);
        left += Virtual<int32_t>(self, win::kWidth) / 2;
    } else {
        left += extraWidth / 2;
        extraWidth = 0;
    }
    if (extraHeight > Virtual<int32_t>(self, win::kHeight)) {
        extraHeight -= Virtual<int32_t>(self, win::kHeight);
        top += Virtual<int32_t>(self, win::kHeight) / 2;
    } else {
        top += extraHeight / 2;
        extraHeight = 0;
    }

    At<int32_t>(self, screen::kMapLeft) = left;
    At<int32_t>(self, screen::kMapTop) = top;
    At<int32_t>(self, screen::kMapRight) = left + extraWidth;
    At<int32_t>(self, screen::kMapBottom) = top + extraHeight;

    void* const overlay = At<void*>(self, screen::kOverlayWin);
    if (!overlay)
        return;
    const float bottom =
        static_cast<float>(Virtual<int32_t>(self, win::kHeight) + At<int32_t>(self, screen::kMapBottom));
    const float right = static_cast<float>(Virtual<int32_t>(self, win::kWidth) + At<int32_t>(self, screen::kMapRight));
    const float topEdge = static_cast<float>(At<int32_t>(self, screen::kMapTop));
    const float leftEdge = static_cast<float>(At<int32_t>(self, screen::kMapLeft));
    This<void>(addr::SC4RegionOverlayWin_SetBounds, overlay, leftEdge, topEdge, right, bottom);
}

// The game's 0x007AB9F0: slot 3 of the screen, the window message handler.
// A scroll message moves the view position (in 256ths of a pixel), two
// messages mark the map busy and free, a key press or release goes through
// the key table to the command service; the rest is the base class's.
uint8_t __fastcall ScreenDoWinMsg(char* self, void*, uint32_t* message)
{
    const uint32_t type = message[0];
    if (type == kMessageScroll) {
        const int32_t dx = static_cast<int32_t>(message[1]);
        const int32_t dy = static_cast<int32_t>(message[2]);
        At<float>(self, screen::kScrollX) = static_cast<float>(
            static_cast<double>(dx) * 0.00390625f + static_cast<double>(At<float>(self, screen::kScrollX)));
        At<float>(self, screen::kScrollY) = static_cast<float>(
            static_cast<double>(dy) * 0.00390625f + static_cast<double>(At<float>(self, screen::kScrollY)));
        return 1;
    }
    if (type == kMessageMapFree) {
        At<uint8_t>(At<char*>(self, screen::kView), 0x110) = 0;
        return 1;
    }
    if (type == kMessageMapBusy) {
        char* const view = At<char*>(self, screen::kView);
        At<uint8_t>(view, 0x110) = 1;
        Virtual<void>(view, 0x170);
        return 1;
    }
    if (type == kMessageKey) {
        const uint32_t kind = message[2];
        if (kind == 5 || kind == 6) {
            const uint32_t* const key = reinterpret_cast<const uint32_t*>(message[3]);
            const uint32_t code = key[1];
            const uint32_t modifiers = key[2];
            uint32_t command = 0;
            // The original pushes a register whose low byte is the flag and
            // whose upper bytes are part of a stack address.
            const uint32_t pressed = (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&command)) & 0xFFFFFF00u)
                | (kind == 5 ? 1u : 0u);
            if (!Virtual<uint8_t>(At<void*>(self, screen::kKeys), 0x28, code, modifiers, pressed, &command))
                return 0;
            Virtual<void>(Global<void*>(addr::SC4_Commands), 0x50, command, int32_t{0}, int32_t{0});
            return 1;
        }
    }
    return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
}

// The game's 0x007ABB00 (with its tail at 0x007ABB50). Posts a message of
// the given type with nothing in it; the screen's Init and Shutdown announce
// themselves with it. `this` is not used. When the allocation fails the
// original goes on with a null message, and so does this.
void __fastcall PostPlainMessage(char*, void*, uint32_t type)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
    void* message = nullptr;
    if (memory) {
        message = This<void*>(addr::cGZMessage_cGZMessage, memory);
        if (message)
            AddRef(message);
    }
    Virtual<void>(message, 0x14, type);
    Virtual<void>(server, 0x10, message, int32_t{0});
    Release(message);
}

// The game's 0x007ABB60: slot 0x238 of the screen (the mouse left): the map
// window drops its mark unless the map is busy.
uint8_t __fastcall ScreenMouseExit(char* self, void*, uint32_t)
{
    if (!At<uint8_t>(self, screen::kBusy))
        This<void>(addr::cSC4WinRegionView_Unpick, At<void*>(self, screen::kView));
    return 1;
}

// The game's 0x007ABB80. Gives a tile record of the map (size byte at +0x18,
// picture at +0x1C, mask at +0x20, two more counted pointers at +0x24 and
// +0x28) the stock pictures of an empty tile of its size, from the set of
// the open region's terrain type, and drops the two other pointers.
void __fastcall GiveStockPictures(char* self, void*, char* tile)
{
    void* const nation = Virtual<void*>(App(), 0x88);
    void* const region = Virtual<void*>(nation, 0x20);
    const int32_t terrain = Virtual<int32_t>(region, 0x48);
    const std::size_t set = terrain == 1 ? screen::kPictures + 0x18 : screen::kPictures;
    AssignRef(&At<void*>(tile, 0x1C), &At<void*>(self, set + At<uint8_t>(tile, 0x18) * 8u));
    AssignRef(&At<void*>(tile, 0x20), &At<void*>(self, set + 4 + At<uint8_t>(tile, 0x18) * 8u));
    ReleaseAndClear(&At<void*>(tile, 0x24));
    ReleaseAndClear(&At<void*>(tile, 0x28));
}

// The game's 0x007ABCD0 (cdecl). Puts the mask's pixels into the picture's
// top byte: picture = picture's low 24 bits + mask << 24, over the mask's
// area. The original takes the area twice from the mask and never from the
// picture.
void __cdecl MergeMask(void* picture, void* mask)
{
    if (!Virtual<uint8_t>(picture, image::kLock, uint32_t{0x8080}))
        return;
    if (Virtual<uint8_t>(mask, image::kLock, uint32_t{0x800})) {
        const int32_t* const first = Virtual<const int32_t*>(mask, image::kArea);
        const int32_t* const second = Virtual<const int32_t*>(mask, image::kArea);
        const int32_t secondWidth = second[2] - second[0];
        const int32_t firstWidth = first[2] - first[0];
        const int32_t width = secondWidth < firstWidth ? secondWidth : firstWidth;
        const int32_t secondHeight = second[3] - second[1];
        const int32_t firstHeight = first[3] - first[1];
        const int32_t height = secondHeight < firstHeight ? secondHeight : firstHeight;
        for (int32_t y = 0; y < height; ++y) {
            for (int32_t x = 0; x < width; ++x) {
                const uint32_t colour = Virtual<uint32_t>(picture, image::kGetPixel, x, y) & 0xFFFFFFu;
                const uint32_t alpha = Virtual<uint32_t>(mask, image::kGetPixel, x, y);
                Virtual<void>(picture, image::kSetPixel, x, y, colour + (alpha << 24));
            }
        }
        Virtual<void>(mask, image::kUnlock, uint32_t{0x800});
    }
    Virtual<void>(picture, image::kUnlock, uint32_t{0x8080});
}

// The game's 0x007ABDF0. Opens one of the three side panels, or closes it
// when it is the one open; the three buttons (0x00AB91AC) show which is open.
void __fastcall SelectPanel(char* self, void*, int32_t index)
{
    void* const anchor = Virtual<void*>(self, win::kChild, kPanelAnchor);
    int32_t open = -1;
    void* panel = index >= 0 ? At<void*>(self, screen::kPanels + static_cast<std::size_t>(index) * 4) : nullptr;
    if (panel && Virtual<uint8_t>(panel, win::kIsVisible)) {
        panel = At<void*>(self, screen::kPanels + static_cast<std::size_t>(index) * 4);
        if (panel) {
            Virtual<void>(panel, win::kHide);
            if (void* const part = At<void*>(self, screen::kPanelParts + static_cast<std::size_t>(index) * 4))
                Virtual<void>(part, win::kHide);
        }
    } else {
        for (int32_t i = 0; i < 3; ++i) {
            if (void* const each = At<void*>(self, screen::kPanels + static_cast<std::size_t>(i) * 4))
                Virtual<void>(each, win::kSetFlag, uint32_t{1}, uint32_t{i == index});
            if (void* const part = At<void*>(self, screen::kPanelParts + static_cast<std::size_t>(i) * 4)) {
                if (i == index) {
                    Virtual<void>(part, win::kPlaceBy, anchor, uint32_t{1});
                    Virtual<void>(At<void*>(self, screen::kPanelParts + static_cast<std::size_t>(i) * 4), win::kShow);
                } else {
                    Virtual<void>(part, win::kHide);
                }
            }
        }
        open = index;
    }
    for (int32_t i = 0; i < 3; ++i) {
        void* button = nullptr;
        if (Virtual<uint8_t>(self, win::kChildAs, kPanelButtons[i], uint32_t{0x8810}, &button))
            Virtual<void>(button, 0x24, uint32_t{i == open});
        if (button)
            Release(button);
    }
}

// The game's 0x007ABF10. Reads the screen's tuning exemplar (0x6534284A,
// 0x690F693F, 0xAA383BFE): the scroll window's values, the margins, three
// globals, two values for the map window, the two terrain heights (also
// copied to the region class's table), two floats and the overlay window's
// two flags.
void __fastcall LoadTuning(char* self, void*)
{
    void* resource = nullptr; // not set by the original before the call
    ResourceKey key{0x6534284A, 0x690F693F, 0xAA383BFE};
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key, uint32_t{0xA52160F5}, &resource,
            int32_t{0}, int32_t{0}))
        return;
    void* const holder = Virtual<void*>(resource, 0x1C);

    using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* out);
    const auto getUint8 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint8);
    const auto getSint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32);
    const auto getUint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32);
    const auto getFloat = reinterpret_cast<GetFn>(addr::SC4_GetPropertyFloat);

    if (At<char*>(self, screen::kScroller)) {
        getUint32(holder, 0xCA383CA4, At<char*>(self, screen::kScroller) + scroller::kTuning110);
        getUint32(holder, 0xCA383CA3, At<char*>(self, screen::kScroller) + scroller::kTuning114);
        getFloat(holder, 0xCA383CA7, At<char*>(self, screen::kScroller) + scroller::kTuning11C);
    }
    getSint32(holder, 0xCA383CA5, self + screen::kMarginX);
    getSint32(holder, 0xCA383CA6, self + screen::kMarginY);
    char* const globals = &Global<char>(addr::SC4RegionScreen_Tuning4E1C0);
    At<uint8_t>(globals, 0xC) = 1;
    getUint32(holder, 0xCA383CA8, globals + 8);
    getUint32(holder, 0xCA383CA9, globals + 4);
    getUint32(holder, 0xCA383CAA, globals);
    getUint32(holder, 0xCA383CAB, self + screen::kViewValueA);
    getUint32(holder, 0xCA383CAC, self + screen::kViewValueB);
    getUint8(holder, 0xCA383CAD, self + screen::kHeight0);
    getUint8(holder, 0xCA383CAE, self + screen::kHeight1);
    Global<uint32_t>(addr::cSC4Region_TerrainHeights) = At<uint8_t>(self, screen::kHeight0);
    Global<uint32_t>(addr::cSC4Region_TerrainHeights + 4) = At<uint8_t>(self, screen::kHeight1);
    getFloat(holder, 0xCA383CAF, self + screen::kTuning1F0);
    getFloat(holder, 0xCA383CB0, self + screen::kTuning1EC);

    if (void* const property = Virtual<void*>(holder, 0x24, uint32_t{0xCA383CB1})) {
        void* const value = Virtual<void*>(property, 0x14);
        const uint8_t* const bytes = Virtual<const uint8_t*>(value, 0x17C);
        void* const overlay = At<void*>(self, screen::kOverlayWin);
        const uint8_t first = bytes[0];
        const uint8_t second = bytes[1];
        if (overlay)
            This<void>(addr::SC4RegionOverlayWin_SetFlags, overlay, uint32_t{first}, uint32_t{second});
    }
    if (char* const view = At<char*>(self, screen::kView)) {
        const uint32_t b = At<uint32_t>(self, screen::kViewValueB);
        const uint32_t a = At<uint32_t>(self, screen::kViewValueA);
        At<uint32_t>(view, 0xF8) = a;
        At<uint32_t>(view, 0xFC) = b;
    }
    Release(resource);
}

// The game's 0x007AC110. Sets the map window up for the screen's mode (0 or
// 1; other values change only the first flag), lets the map take the mouse
// again and drops its mark.
void __fastcall ApplyMode(char* self, void*)
{
    This<void>(addr::cSC4WinRegionView_SetFlag116, At<void*>(self, screen::kView), uint32_t{1});
    const uint32_t mode = At<uint32_t>(self, screen::kMode);
    if (mode == 0) {
        This<void>(addr::cSC4WinRegionView_SetFlags111, At<void*>(self, screen::kView), uint32_t{0}, uint32_t{1});
        This<void>(addr::cSC4WinRegionView_SetLabelLayout, At<void*>(self, screen::kView), uint32_t{0}, uint32_t{0});
        This<void>(addr::cSC4WinRegionView_SetFlags114, At<void*>(self, screen::kView), uint32_t{0}, uint32_t{1});
    } else if (mode == 1) {
        This<void>(addr::cSC4WinRegionView_SetFlags111, At<void*>(self, screen::kView), uint32_t{1}, uint32_t{0});
        This<void>(addr::cSC4WinRegionView_SetLabelLayout, At<void*>(self, screen::kView), uint32_t{0}, uint32_t{0});
        This<void>(addr::cSC4WinRegionView_SetFlags114, At<void*>(self, screen::kView), uint32_t{1}, uint32_t{1});
    }
    void* const view = At<void*>(self, screen::kView);
    At<uint8_t>(self, screen::kBusy) = 0;
    This<void>(addr::cSC4WinRegionView_Unpick, view);
}

// The game's 0x007AC1A0. Points the 3D view's camera at the view position:
// the window's centre plus the scroll offset, scaled, along the camera's two
// screen axes (+0x60 and +0x6C of the camera, scale at +0x150).
void __fastcall CenterCamera(char* self, void*)
{
    char* const camera = Virtual<char*>(At<void*>(self, screen::kRenderer), 0x20);

    double along = static_cast<double>(Virtual<int32_t>(self, win::kHeight)) * 0.5f;
    along += static_cast<double>(At<float>(self, screen::kScrollY));
    along *= static_cast<double>(At<float>(camera, 0x150));
    const float down0 = static_cast<float>(along * static_cast<double>(At<float>(camera, 0x6C)));
    const float down1 = static_cast<float>(along * static_cast<double>(At<float>(camera, 0x70)));
    const float down2 = static_cast<float>(along * static_cast<double>(At<float>(camera, 0x74)));

    along = static_cast<double>(Virtual<int32_t>(self, win::kWidth)) * 0.5f;
    void* const control = At<void*>(self, screen::kCamera);
    along += static_cast<double>(At<float>(self, screen::kScrollX));
    along *= static_cast<double>(At<float>(camera, 0x150));
    const double across0 = along * static_cast<double>(At<float>(camera, 0x60)); // not rounded to a float
    const float across1 = static_cast<float>(along * static_cast<double>(At<float>(camera, 0x64)));
    const float across2 = static_cast<float>(along * static_cast<double>(At<float>(camera, 0x68)));

    float center[3];
    center[0] = static_cast<float>(across0 - static_cast<double>(down0));
    center[1] = static_cast<float>(static_cast<double>(across1) - static_cast<double>(down1));
    center[2] = static_cast<float>(static_cast<double>(across2) - static_cast<double>(down2));
    This<void>(addr::cSC4CameraControl_SetCenter, control, center);
}

// The game's 0x007AC270. Writes the open region's name into the preferences
// (64 bytes at +0xEBC), marks the saved view position unset (0x80000000
// twice, at +0xEFC) and saves the preferences. `this` is not used.
uint32_t __fastcall SaveRegionChoice(char*, void*)
{
    void* const app = App();
    void* const nation = Virtual<void*>(app, 0x88);
    void* const region = Virtual<void*>(nation, 0x20);
    void* const name = Virtual<void*>(region, 0xC);
    char* const saved = Virtual<char*>(app, 0x98) + 0xEBC;
    Cdecl<char*>(addr::SC4_strncpy, saved, Virtual<const char*>(name, 0x14), std::size_t{0x40});
    At<uint32_t>(saved, 0x40) = 0x80000000;
    At<uint32_t>(saved, 0x44) = 0x80000000;
    return Virtual<uint32_t>(app, 0x44);
}

// The game's 0x007AC2D0. Writes the view position (the window's centre plus
// the scroll offset, in 256ths of a pixel) into the preferences and saves
// them. The region's name is asked for and not used.
uint32_t __fastcall SaveViewPosition(char* self, void*)
{
    void* const app = App();
    void* const nation = Virtual<void*>(app, 0x88);
    void* const region = Virtual<void*>(nation, 0x20);
    Virtual<void*>(region, 0xC);
    char* const saved = Virtual<char*>(app, 0x98) + 0xEBC;

    const float y = static_cast<float>(static_cast<double>(Virtual<int32_t>(self, win::kHeight)) * 0.5f
        + static_cast<double>(At<float>(self, screen::kScrollY)));
    double x = static_cast<double>(Virtual<int32_t>(self, win::kWidth)) * 0.5f;
    x += static_cast<double>(At<float>(self, screen::kScrollX));
    x *= 256.0f;
    At<int32_t>(saved, 0x40) = Truncate(x);
    At<int32_t>(saved, 0x44) = Truncate(static_cast<double>(y) * 256.0f);
    return Virtual<uint32_t>(app, 0x44);
}

// The game's 0x007AC380. Takes the 3D view down: clears the globals that
// point into it and shuts down and releases its parts.
void __fastcall ShutDownView(char* self, void*)
{
    Global<void*>(addr::SC4_Service43D1C) = nullptr;
    ShutDownPart(self, screen::kPart15C);
    Global<void*>(addr::SC4_Transform43DDC) = nullptr;
    Global<void*>(addr::SC4_CameraView) = nullptr;
    Global<void*>(addr::SC4_View43DD0) = nullptr;
    ShutDownPart(self, screen::kRenderer);
    if (void* control = At<void*>(self, screen::kCamera)) {
        This<void>(addr::cSC4CameraControl_ReleaseCamera, control);
        control = At<void*>(self, screen::kCamera);
        if (control) {
            At<void*>(self, screen::kCamera) = nullptr;
            Release(control);
        }
    }
    if (void* part = At<void*>(self, screen::kPart160)) {
        This<uint8_t>(addr::SC4_ReturnTrue648F00, part);
        part = At<void*>(self, screen::kPart160);
        if (part) {
            At<void*>(self, screen::kPart160) = nullptr;
            Release(part);
        }
    }
    Global<void*>(addr::SC4_RegionTerrain) = nullptr;
    ShutDownPart(self, screen::kTerrain);
    ShutDownPart(self, screen::kPart158);
}

// Whether tile a sorts before tile b: by (1 << size) + x + y, then by x.
bool TileBefore(const Tile& a, const Tile& b)
{
    uint32_t difference = 1u << (static_cast<uint32_t>(a.size) & 31);
    difference -= 1u << (static_cast<uint32_t>(b.size) & 31);
    difference -= static_cast<uint32_t>(b.y);
    difference += static_cast<uint32_t>(a.y);
    difference -= static_cast<uint32_t>(b.x);
    difference += static_cast<uint32_t>(a.x);
    const int32_t sign = static_cast<int32_t>(difference);
    return sign < 0 || (sign == 0 && a.x < b.x);
}

// The game's 0x007AC490 (cdecl): the sort's insertion pass, each tile put in
// its place by 0x007AA9C0, which looks backwards without a lower bound. The
// third argument is not used; the fourth is the (empty) ordering object.
void __cdecl InsertionSortTiles(Tile* first, Tile* last, uint32_t, uint32_t order)
{
    for (Tile* tile = first; tile != last; ++tile)
        Cdecl<void>(addr::SC4RegionTileSort_InsertBackward, tile, *tile, order);
}

// The game's 0x007AC4D0 (cdecl): the sort's heap adjustment. The hole moves
// down to the larger child until the bottom, then 0x007AAA40 pushes the
// value up from there.
void __cdecl AdjustTileHeap(Tile* base, int32_t hole, int32_t length, Tile value, uint32_t order)
{
    const int32_t top = hole;
    int32_t child = hole + hole + 2;
    while (child < length) {
        if (TileBefore(base[child], base[child - 1]))
            --child;
        base[hole] = base[child];
        hole = child;
        child = child + child + 2;
    }
    if (child == length) {
        base[hole] = base[child - 1];
        hole = child - 1;
    }
    Cdecl<void>(addr::SC4RegionTileSort_PushHeap, base, hole, top, value, order);
}

// The game's 0x007AC5C0: the scroll window's deleting destructor (slot 0x250
// of vtable 0x00AB8CD0).
char* __fastcall ScrollerDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4RegionScroller_Vtable;
    if (This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        if (void* const picture = At<void*>(self, scroller::kImage)) {
            Release(picture);
            At<void*>(self, scroller::kImage) = nullptr;
        }
        This<uint8_t>(addr::SC4Win_F99D2FE, self);
    }
    This<void>(addr::SC4Stopwatch_Destruct, self + scroller::kStopwatch);
    This<void>(addr::GZWin_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007AC620. The scroll window's click (its slot 0x21C,
// 0x007AD3B0) calls it with the point: keeps the point, loads the marker
// picture (0x856DDBAC, 0x46A006B0, 0x094AC89A; magenta is see-through) on
// first use and makes a picture window of it centred on the point, as a
// child of this window.
void __fastcall ScrollerPlaceMarker(char* self, void*, int32_t x, int32_t y)
{
    At<int32_t>(self, scroller::kX) = x;
    At<int32_t>(self, scroller::kY) = y;
    At<uint8_t>(self, scroller::kPlaced) = 1;
    if (!At<void*>(self, scroller::kImage)) {
        alignas(4) char loader[0x30];
        This<void*>(addr::SC4ImageLoader_ConstructParts, static_cast<void*>(loader), uint32_t{0x856DDBAC},
            uint32_t{0x46A006B0}, uint32_t{0x094AC89A}, uint32_t{1}, uint32_t{0});
        void* const loaded = At<void*>(loader, 0x4);
        At<void*>(self, scroller::kImage) = loaded;
        if (loaded) {
            AddRef(loaded);
            const uint32_t colour = Virtual<uint32_t>(At<void*>(self, scroller::kImage), image::kColour, uint32_t{0xFF},
                uint32_t{0}, uint32_t{0xFF});
            Virtual<void>(At<void*>(self, scroller::kImage), image::kSetKey, colour);
        }
        This<void>(addr::SC4ImageLoader_Destruct, static_cast<void*>(loader));
    }
    void* const picture = At<void*>(self, scroller::kImage);
    if (!picture)
        return;

    const int32_t* const area = Virtual<const int32_t*>(picture, image::kArea);
    int32_t left = area[0];
    int32_t top = area[1];
    int32_t right = area[2];
    int32_t bottom = area[3];
    const int32_t halfHeight = (bottom - top) / 2;
    const int32_t halfWidth = (right - left) / 2;
    left -= halfWidth;
    right -= halfWidth;
    const int32_t atY = At<int32_t>(self, scroller::kY);
    top -= halfHeight;
    bottom -= halfHeight;
    const int32_t atX = At<int32_t>(self, scroller::kX);
    right += atX;
    bottom += atY;
    left += atX;
    top += atY;

    void* const maker = Cdecl<void*>(addr::SC4_Singleton913C72);
    void* const made = Virtual<void*>(maker, 0x3C, kMarkerClass, At<void*>(self, scroller::kImage), uint32_t{1});
    if (!made)
        return;
    void* const marker = Virtual<void*>(made, win::kAsWin);
    Virtual<void>(marker, win::kSetArea, left, top, right, bottom);
    Virtual<void>(marker, win::kSetFlag, uint32_t{2}, uint32_t{0});
    Virtual<void>(marker, win::kSetFlag, uint32_t{0x200000}, uint32_t{1});
    Virtual<void>(marker, win::kSetFlag, uint32_t{4}, uint32_t{1});
    Virtual<void>(marker, win::kShow);
    if (!Virtual<uint8_t>(self, win::kIsChild, marker)) {
        void* const owner = Virtual<void*>(self, win::kOwner);
        Virtual<void>(owner, 0x5C, marker);
    } else {
        Virtual<void>(self, win::kToFront, marker);
    }
    Release(made);
}

// The game's 0x007AC7D0: the terrain overlay's constructor (0x14 bytes: the
// drawable's vtable, a counted base at +4, a packed render state at +0xC).
// The screen makes one in 0x007ACC90 and hands it to the 3D view.
char* __fastcall OverlayConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4RegionOverlay_BaseVtable;
    This<void*>(addr::cRZBaseUnknown_Construct, self + 4);
    At<std::uintptr_t>(self, 0) = addr::SC4RegionOverlay_Vtable;
    At<std::uintptr_t>(self, 4) = addr::SC4RegionOverlay_UnknownVtable;
    At<uint32_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x10) = 0;
    This<void>(addr::SC4RenderState_Init, self + 0xC, 1u, 1u, 1u, 0u, 1u, 0u, 0u, 7u, 0u, 1u, 0xFFFFFFFFu, -1.0f,
        0xFFFFFFFFu, 0xFFFFFFFFu);
    return self;
}

bool InstallRegionScreen2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionScreen_OnMouseMove, reinterpret_cast<void*>(&ScreenMouseMove)},
        {addr::cSC4WinRegionScreen_OnMouseDownR, reinterpret_cast<void*>(&ScreenMouseDown)},
        {addr::cSC4WinRegionScreen_LayOutMap, reinterpret_cast<void*>(&LayOutMap)},
        {addr::cSC4WinRegionScreen_DoWinMsg, reinterpret_cast<void*>(&ScreenDoWinMsg)},
        {addr::cSC4WinRegionScreen_PostPlainMessage, reinterpret_cast<void*>(&PostPlainMessage)},
        {addr::cSC4WinRegionScreen_OnMouseExit, reinterpret_cast<void*>(&ScreenMouseExit)},
        {addr::cSC4WinRegionScreen_GiveStockPictures, reinterpret_cast<void*>(&GiveStockPictures)},
        {addr::SC4RegionPictures_MergeMask, reinterpret_cast<void*>(&MergeMask)},
        {addr::cSC4WinRegionScreen_SelectPanel, reinterpret_cast<void*>(&SelectPanel)},
        {addr::cSC4WinRegionScreen_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::cSC4WinRegionScreen_ApplyMode, reinterpret_cast<void*>(&ApplyMode)},
        {addr::cSC4WinRegionScreen_CenterCamera, reinterpret_cast<void*>(&CenterCamera)},
        {addr::cSC4WinRegionScreen_SaveRegionChoice, reinterpret_cast<void*>(&SaveRegionChoice)},
        {addr::cSC4WinRegionScreen_SaveViewPosition, reinterpret_cast<void*>(&SaveViewPosition)},
        {addr::cSC4WinRegionScreen_ShutDownView, reinterpret_cast<void*>(&ShutDownView)},
        {addr::SC4RegionTileSort_InsertionPass, reinterpret_cast<void*>(&InsertionSortTiles)},
        {addr::SC4RegionTileSort_AdjustHeap, reinterpret_cast<void*>(&AdjustTileHeap)},
        {addr::SC4RegionScroller_DeletingDestructor, reinterpret_cast<void*>(&ScrollerDelete)},
        {addr::SC4RegionScroller_PlaceMarker, reinterpret_cast<void*>(&ScrollerPlaceMarker)},
        {addr::SC4RegionOverlay_Construct, reinterpret_cast<void*>(&OverlayConstruct)},
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

const bool g_regionScreen2Registered = registry::Register({
    "region.screen.part2",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionScreen_DoWinMsg,
    "Our region screen, part 2: the message handler, mouse slots, map layout, tuning, panels, view position",
    false,
    InstallRegionScreen2,
});

} // namespace
} // namespace opensc4::region
