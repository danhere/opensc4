// The small map of the city (class 0xCA318388, cSC4WinMiniMap in gzcom-dll's
// class list; 0x96C bytes, a window with two more parts), the end of its
// code (0x007A8920 to 0x007A9235): the object made and destroyed, the map
// started for a city, the window's Init and SetArea, and the message handler.
//
// The object: the window (main vtable 0x00AB83B8) with the map's interface
// at +0xD8 (IID 0xCA318385, vtable 0x00AB838C) and a message target at +0xDC
// (0x00AB837C). The map window keeps one as child 0x4203 of its own window.
//
//   +0xE0  what is shown (bits; 7 when made)
//   +0xE4  the picture's side in pixels, a power of two
//   +0xE8, +0xEC  the city's size in cells, set when the map is started
//   +0xF0  the picture's buffer, +0xF4 a second buffer given by the owner
//   +0xF8  milliseconds since the picture was last brought up to date
//   +0xFC  started; +0xFD terrain to be redone; +0xFE picture to be redone;
//          +0xFF window to be redrawn; +0x101 listening for occupants
//   +0x108 sixteen colours (a vector of 32-bit values)
//   +0x114 the picture's own pixels
//   +0x120 sixteen rows of bits: the 16 by 16 cell tiles to be redone
//   +0x160, +0x560  two tables of 256 colours
//   +0x960 two bits a cell (a vector of 32-bit values)
//
// Replacement, off by default:
//
//   ui.map.part6: the ten functions above.
//
// The picture itself stays the game's: the window's paint (0x007A79B0), the
// terrain's pixels (0x007A7FF0) and the picture brought up to date
// (0x007A8640, slot 9 of the interface). The message handler calls the last
// through the vtable when a city is loaded and once a second.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::ui {
namespace {

namespace map {
constexpr std::size_t kInterface = 0xD8;
constexpr std::size_t kTarget = 0xDC;
constexpr std::size_t kShown = 0xE0;
constexpr std::size_t kSide = 0xE4;
constexpr std::size_t kCityWidth = 0xE8;
constexpr std::size_t kCityDepth = 0xEC;
constexpr std::size_t kBuffer = 0xF0;
constexpr std::size_t kSecondBuffer = 0xF4;
constexpr std::size_t kElapsed = 0xF8;
constexpr std::size_t kStarted = 0xFC;
constexpr std::size_t kTerrainStale = 0xFD;
constexpr std::size_t kPictureStale = 0xFE;
constexpr std::size_t kRedraw = 0xFF;
constexpr std::size_t kByte100 = 0x100;
constexpr std::size_t kListening = 0x101;
constexpr std::size_t kBlink = 0x102;
constexpr std::size_t kColors = 0x108; // begin, end, capacity
constexpr std::size_t kPixels = 0x114;
constexpr std::size_t kFirstTable = 0x160;
constexpr std::size_t kSecondTable = 0x560;
constexpr std::size_t kCells = 0x960; // begin, end, capacity
constexpr std::size_t kObjectSize = 0x96C;
} // namespace map

constexpr uint32_t kTick = 0x86AD10EE;
constexpr uint32_t kCityLoaded = 0x26D31EC1;
constexpr uint32_t kCityClosing = 0x26D31EC2;
constexpr uint32_t kCellsChanged = 0x07699455;
constexpr uint32_t kAreaChanged1 = 0x278E6003;
constexpr uint32_t kAreaChanged2 = 0x678E6E3A;
constexpr uint32_t kAreaChanged3 = 0x89171496;
constexpr uint32_t kOccupantInserted = 0x99EF1142;
constexpr uint32_t kOccupantRemoved = 0x99EF1143;
constexpr uint32_t kCameraChanged1 = 0xA6B79602;
constexpr uint32_t kCameraChanged2 = 0xA6B79612;
constexpr uint32_t kResourceChanged = 0xC53D10AA;

constexpr uint32_t kNetworkOccupantType = 0xC772BF98;
constexpr uint32_t kNetworkOccupantIid = 0xA821EF94;

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

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The game's 0x007A8920: the constructor.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, map::kInterface) = addr::SC4UI_MiniMapBaseVtableD8;
    At<std::uintptr_t>(self, map::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MiniMapVtable;
    At<std::uintptr_t>(self, map::kInterface) = addr::SC4UI_MiniMapVtableD8;
    At<std::uintptr_t>(self, map::kTarget) = addr::SC4UI_MiniMapVtableDC;
    At<uint32_t>(self, map::kShown) = 7;
    At<uint32_t>(self, map::kSide) = 0;
    At<uint32_t>(self, map::kCityWidth) = 0;
    At<uint32_t>(self, map::kCityDepth) = 0;
    At<void*>(self, map::kBuffer) = nullptr;
    At<void*>(self, map::kSecondBuffer) = nullptr;
    At<uint32_t>(self, map::kElapsed) = 0;
    At<uint8_t>(self, map::kStarted) = 0;
    At<uint8_t>(self, map::kTerrainStale) = 0;
    At<uint8_t>(self, map::kPictureStale) = 0;
    At<uint8_t>(self, map::kRedraw) = 0;
    At<uint8_t>(self, map::kByte100) = 0;
    At<uint8_t>(self, map::kListening) = 0;
    At<uint8_t>(self, map::kBlink) = 0;
    At<uint32_t*>(self, map::kColors) = nullptr;
    At<uint32_t*>(self, map::kColors + 0x4) = nullptr;
    At<uint32_t*>(self, map::kColors + 0x8) = nullptr;

    // Sixteen colours, all zero.
    uint32_t* const colors = Cdecl<uint32_t*>(addr::SC4_stl_allocate, uint32_t{0x40});
    At<uint32_t*>(self, map::kColors) = colors;
    At<uint32_t*>(self, map::kColors + 0x8) = colors + 16;
    for (int i = 0; i < 16; ++i)
        colors[i] = 0;
    At<uint32_t*>(self, map::kColors + 0x4) = colors + 16;

    At<void*>(self, map::kPixels) = nullptr;
    At<uint32_t*>(self, map::kCells) = nullptr;
    At<uint32_t*>(self, map::kCells + 0x4) = nullptr;
    At<uint32_t*>(self, map::kCells + 0x8) = nullptr;
    return self;
}

// The game's 0x007A8A40: QueryInterface of the map's interface (+0xD8).
uint8_t __fastcall InterfaceQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F7A6650, self - map::kInterface, iid, out);
}

// The game's 0x007A8A50: QueryInterface of the message target (+0xDC).
uint8_t __fastcall TargetQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F7A6650, self - map::kTarget, iid, out);
}

// The game's 0x007A8A60: the destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MiniMapVtable;
    At<std::uintptr_t>(self, map::kInterface) = addr::SC4UI_MiniMapVtableD8;
    At<std::uintptr_t>(self, map::kTarget) = addr::SC4UI_MiniMapVtableDC;
    if (void* const cells = At<void*>(self, map::kCells))
        Cdecl<void>(addr::SC4_Free, cells);
    Cdecl<void>(addr::SC4_operator_delete, At<void*>(self, map::kPixels));
    At<void*>(self, map::kPixels) = nullptr;
    if (void* const colors = At<void*>(self, map::kColors))
        Cdecl<void>(addr::SC4_Free, colors);
    if (void* const second = At<void*>(self, map::kSecondBuffer))
        Release(second);
    if (void* const buffer = At<void*>(self, map::kBuffer))
        Release(buffer);
    This<void>(addr::GZWin_Destruct, self);
}

// The game's 0x007A8AF0: the class factory; gives the map's interface.
void* __cdecl Create()
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{map::kObjectSize});
    if (memory) {
        if (char* const object = Construct(memory, nullptr))
            return object + map::kInterface;
    }
    return nullptr;
}

// The game's 0x007A8B20: the deleting destructor (the main vtable's last
// slot).
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x007A8B40: starts the map for the city, once: the picture's
// pixels sized, two bits a cell made, the frame ticks asked for.
void __fastcall Start(char* self, void*)
{
    if (At<uint8_t>(self, map::kStarted))
        return;
    At<uint8_t>(self, map::kStarted) = 1;
    This<void>(addr::SC4UI_F7A7840, self);

    void* const city = Global<void*>(addr::SC4_City);
    const int32_t width = Virtual<int32_t>(city, 0x174);
    const int32_t depth = Virtual<int32_t>(city, 0x178);
    At<int32_t>(self, map::kCityDepth) = depth;
    const int32_t cells = depth * width;
    const int32_t words = (cells + cells + 0x1F) >> 5;
    At<int32_t>(self, map::kCityWidth) = width;
    uint32_t zero = 0;
    This<void>(addr::SC4Uint32Vector_Resize, self + map::kCells, static_cast<uint32_t>(words), &zero);

    void* const ticks = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<uint8_t>(ticks, 0x14, static_cast<void*>(self + map::kTarget), uint32_t{0});
    This<void>(addr::SC4UI_F7A7140, self);
}

// The game's 0x007A8BD0 (it runs to 0x007A8E23): the window's Init, slot 4.
// Makes the picture's buffer, grey blue at first, listens for nine messages,
// sets the colours and starts the map.
uint8_t __fastcall Init(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;

    void* service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xC416025C}, uint32_t{0x0073283C}, &service);
    // The original keeps the pointer past its own release.
    void* const buffers = service;
    This<void>(addr::SC4RefPtr_Destruct, &service);

    void** const holder = &At<void*>(self, map::kBuffer);
    if (void* const old = *holder) {
        *holder = nullptr;
        Release(old);
    }
    if (!Virtual<uint8_t>(buffers, 0xC, holder))
        return 0;

    // Init(width, height, colour type 9, 32 bits).
    const uint32_t side = At<uint32_t>(self, map::kSide);
    if (!Virtual<uint8_t>(*holder, 0xC, side, side, uint32_t{9}, uint32_t{0x20})) {
        if (void* const made = *holder) {
            *holder = nullptr;
            Release(made);
        }
        return 0;
    }
    if (Virtual<uint8_t>(*holder, 0x18, uint32_t{0x8001})) {
        void* const buffer = *holder;
        const uint32_t color = Virtual<uint32_t>(buffer, 0x78, uint32_t{0x64}, uint32_t{0x64}, uint32_t{0x7F});
        Virtual<uint8_t>(buffer, 0x48, color, static_cast<void*>(nullptr));
        Virtual<uint8_t>(*holder, 0x1C, uint32_t{0x8001});
    }

    if (!This<uint8_t>(addr::GZWin_Init, self)) {
        Virtual<uint8_t>(self, 0x14);
        return 0;
    }

    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const target = self + map::kTarget;
    static const uint32_t messages[] = {
        kAreaChanged3, kAreaChanged2, kAreaChanged1, kCellsChanged, kCameraChanged1,
        kCameraChanged2, kCityLoaded, kCityClosing, kResourceChanged,
    };
    for (const uint32_t message : messages)
        Virtual<uint8_t>(server, 0x14, target, message);

    Cdecl<void*>(addr::SC4_MemSet, static_cast<void*>(self + map::kFirstTable), int32_t{0}, uint32_t{0x400});
    Cdecl<void*>(addr::SC4_MemSet, static_cast<void*>(self + map::kSecondTable), int32_t{0}, uint32_t{0x400});

    void* const iface = self + map::kInterface;
    Virtual<void>(iface, 0x14, uint32_t{0xE57300});
    Virtual<void>(iface, 0x1C, uint32_t{1}, uint32_t{0xEC00});
    Virtual<void>(iface, 0x1C, uint32_t{2}, uint32_t{0xEC00});
    Virtual<void>(iface, 0x1C, uint32_t{3}, uint32_t{0xEC00});
    Virtual<void>(iface, 0x1C, uint32_t{4}, uint32_t{0x7979FF});
    Virtual<void>(iface, 0x1C, uint32_t{5}, uint32_t{0x7979FF});
    Virtual<void>(iface, 0x1C, uint32_t{6}, uint32_t{0x7979FF});
    Virtual<void>(iface, 0x1C, uint32_t{7}, uint32_t{0xC7BC00});
    Virtual<void>(iface, 0x1C, uint32_t{8}, uint32_t{0xC7BC00});
    Virtual<void>(iface, 0x1C, uint32_t{9}, uint32_t{0xC7BC00});
    This<void>(addr::SC4UI_F7A7450, self);
    Start(self, nullptr);
    return 1;
}

// The game's 0x007A8E30: the window's SetArea, slot 55. The picture's side
// becomes the largest power of two that fits the smaller side of the window.
void __fastcall SetArea(char* self, void*, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    This<void>(addr::GZWin_SetArea, self, left, top, right, bottom);
    const int32_t width = right - left;
    const int32_t height = bottom - top;
    uint32_t side = static_cast<uint32_t>(height < width ? height : width);
    for (uint32_t rest = (side - 1) & side; rest != 0; rest &= rest - 1)
        side = rest;
    const void* const pixels = At<void*>(self, map::kPixels);
    At<uint32_t>(self, map::kSide) = side;
    if (pixels)
        Start(self, nullptr);
}

// The game's 0x007A8EA0 (it runs to 0x007A9235): the message handler, slot 3
// of the target at +0xDC.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - map::kTarget;
    void* const iface = self + map::kInterface;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);

    switch (type) {
    case kCityLoaded:
        Start(self, nullptr);
        Virtual<void>(iface, 0x24); // the picture brought up to date
        Virtual<void>(self, 0x170);
        return 1;

    case kCellsChanged: {
        const int32_t* const from = Virtual<int32_t*>(message, 0x3C);
        int32_t area[4] = {from[0], from[1], from[2], from[3]};
        This<uint8_t>(addr::SC4UI_F7A71F0, self, static_cast<int32_t*>(area));
        return 1;
    }

    case kCityClosing:
        This<void>(addr::SC4UI_F7A77E0, self);
        return 1;

    case kAreaChanged1:
    case kAreaChanged2:
    case kAreaChanged3: {
        const uint32_t lowSecond = Virtual<uint32_t>(message, 0x38) & 0xFFFF;
        const uint32_t lowFirst = Virtual<uint32_t>(message, 0x24) & 0xFFFF;
        const uint32_t highSecond = Virtual<uint32_t>(message, 0x38) >> 16;
        const uint32_t highFirst = Virtual<uint32_t>(message, 0x24) >> 16;
        int32_t area[4] = {
            static_cast<int32_t>(highFirst),
            static_cast<int32_t>(highSecond),
            static_cast<int32_t>(lowFirst),
            static_cast<int32_t>(lowSecond),
        };
        This<uint8_t>(addr::SC4UI_F7A71F0, self, static_cast<int32_t*>(area));
        return 1;
    }

    case kTick: {
        if (!Cdecl<uint8_t>(addr::SC4UI_F7A6590, static_cast<void*>(self))) {
            // Not on show: the picture's pixels are dropped.
            Cdecl<void>(addr::SC4_operator_delete, At<void*>(self, map::kPixels));
            At<void*>(self, map::kPixels) = nullptr;
            return 1;
        }
        // The message's third value is read as a float.
        const uint32_t bits = Virtual<uint32_t>(message, 0x4C);
        float elapsed;
        std::memcpy(&elapsed, &bits, sizeof(elapsed));
        const double sum = static_cast<double>(elapsed) + static_cast<double>(At<float>(self, map::kElapsed));
        At<float>(self, map::kElapsed) = static_cast<float>(sum);
        if (sum > static_cast<double>(Global<float>(addr::SC4UI_MiniMapInterval))) {
            At<uint32_t>(self, map::kElapsed) = 0;
            Virtual<void>(iface, 0x24); // the picture brought up to date
            if (At<uint8_t>(self, map::kShown) & 0x10) {
                void* const city = Global<void*>(addr::SC4_City);
                void* const object = Virtual<void*>(city, 0xC4);
                if (Virtual<uint32_t>(object, 0x18))
                    At<uint8_t>(self, map::kRedraw) = 1;
            }
        }
        if (At<uint8_t>(self, map::kRedraw)) {
            Virtual<void>(self, 0x170);
            At<uint8_t>(self, map::kRedraw) = 0;
        }
        return 1;
    }

    case kOccupantInserted:
    case kOccupantRemoved: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (Virtual<uint32_t>(message, 0x4C) != kNetworkOccupantType)
            return 1;
        void* network = nullptr;
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &network);
        if (Virtual<uint8_t>(occupant, 0x0, kNetworkOccupantIid, out)) {
            // The original leaves x unset and z holding the message's
            // address until the call fills them.
            uint32_t x = 0;
            uint32_t z = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(message));
            Virtual<void>(network, 0xEC, &x, &z);
            const uint32_t width = At<uint32_t>(self, map::kCityWidth);
            if (x < width && z < At<uint32_t>(self, map::kCityDepth)) {
                const uint32_t bit = (width * z + x) << 1;
                At<uint32_t*>(self, map::kCells)[bit >> 5] &= ~(uint32_t{1} << (bit & 0x1F));
            }
            if (type == kOccupantRemoved) {
                int32_t area[4] = {
                    static_cast<int32_t>(x),
                    static_cast<int32_t>(z),
                    static_cast<int32_t>(x),
                    static_cast<int32_t>(z),
                };
                This<uint8_t>(addr::SC4UI_F7A71F0, self, static_cast<int32_t*>(area));
            } else {
                At<uint8_t>(self, map::kPictureStale) = 1;
            }
        }
        if (network)
            Release(network);
        return 1;
    }

    case kCameraChanged1: {
        const uint32_t* const first = Virtual<uint32_t*>(message, 0x28);
        const uint32_t* const second = Virtual<uint32_t*>(message, 0x3C);
        if (first[1] != second[1])
            This<void>(addr::SC4UI_F7A66C0, self);
        if (first[0] != second[0])
            At<uint8_t>(self, map::kRedraw) = 1;
        return 1;
    }

    case kCameraChanged2:
        At<uint8_t>(self, map::kRedraw) = 1;
        return 1;

    case kResourceChanged: {
        const uint32_t group = Virtual<uint32_t>(message, 0x24);
        const uint32_t instance = Virtual<uint32_t>(message, 0x38);
        if (group == 0x690F693F && instance == 0x8A356F2C)
            This<void>(addr::SC4UI_F7A7450, self);
        return 1;
    }

    default:
        return 0;
    }
}

bool InstallMap6(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[10] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7A8920, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F7A8A40, reinterpret_cast<void*>(&InterfaceQueryInterface)},
        {addr::SC4UI_F7A8A50, reinterpret_cast<void*>(&TargetQueryInterface)},
        {addr::SC4UI_F7A8A60, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F7A8AF0, reinterpret_cast<void*>(&Create)},
        {addr::SC4UI_F7A8B20, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4UI_F7A8B40, reinterpret_cast<void*>(&Start)},
        {addr::SC4UI_F7A8BD0, reinterpret_cast<void*>(&Init)},
        {addr::SC4UI_F7A8E30, reinterpret_cast<void*>(&SetArea)},
        {addr::SC4UI_F7A8EA0, reinterpret_cast<void*>(&DoMessage)},
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

const bool g_map6Registered = registry::Register({
    "ui.map.part6",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7A8920,
    "Our small map of the city: made, destroyed, started, Init, SetArea, the message handler (docs/ui.md)",
    false,
    InstallMap6,
});

} // namespace
} // namespace opensc4::ui
