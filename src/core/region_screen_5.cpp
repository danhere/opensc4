// The region screen's window (docs/region.md), part 5: 0x007AEDD0 to
// 0x007AFAA0, three methods of cSC4WinRegionScreen. Replacement, off by
// default:
//
//   region.screen.part5: the screen's command handler (0x007AF720), the
//     command that opens a city by name or the first empty tile of a size
//     (0x007AF4B0), and the command that makes the region's terrain from a
//     picture file the player picks (0x007AEDD0).
//
// cSC4WinRegionScreen (window class 0xEA659793, constructor 0x007B0C00, main
// vtable 0x00AB9260, a second one at +0xD8 and the message target's
// 0x00AB923C at +0xDC). The message target's handler 0x007B0AF0 hands every
// command message (interface 0x4B99446A) to the command handler here with
// the message's type and its two parameter sets (cIGZCommandParameterSet:
// what came in, what goes back). What this part shows of the object:
//
//   +0x004  the window manager (window base class)
//   +0x0E0  the region map window (cSC4WinRegionView)
//   +0x118  vector of the region's tiles, 0x80 bytes each
//   +0x174  the scrolling child window (constructor 0x007AAE10)
//   +0x1A4  the index of the shown region in the nation's list
//
// A tile record: +0x00 and +0x04 two numbers whose largest plus one, times
// 64 plus one, must be the width and height of a terrain picture (the last
// km column and row the tile covers, by that use); +0x08, +0x0C its position
// in km; +0x18 log2 of its side in km (byte). The scrolling window: +0xFC
// and +0x100 its speed across and down, +0x104 the speed a key gives.
//
// Drawing, the window base class, the window manager, the picture dialog,
// the image classes and the application's city loading stay the game's.
// 0x007AF940 is not a function: it is a case label inside 0x007AF720.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace screen { // cSC4WinRegionScreen
constexpr std::size_t kWinManager = 0x4;
constexpr std::size_t kRegionView = 0xE0;
constexpr std::size_t kTiles = 0x118;
constexpr std::size_t kTilesEnd = 0x11C;
constexpr std::size_t kScroller = 0x174;
constexpr std::size_t kRegionIndex = 0x1A4;
} // namespace screen

namespace tile { // one record of the tile vector
constexpr std::size_t kLastX = 0x0;
constexpr std::size_t kLastY = 0x4;
constexpr std::size_t kX = 0x8;
constexpr std::size_t kY = 0xC;
constexpr std::size_t kSizeLog = 0x18;
constexpr std::ptrdiff_t kBytes = 0x80;
} // namespace tile

namespace scroller { // the scrolling child window
constexpr std::size_t kSpeedX = 0xFC;
constexpr std::size_t kSpeedY = 0x100;
constexpr std::size_t kKeySpeed = 0x104;
} // namespace scroller

namespace app { // cISC4App, by vtable offset
constexpr std::size_t kRequestNewCity = 0x18;        // (holder)
constexpr std::size_t kRequestQuitFromRegion = 0x2C; // (bool)
constexpr std::size_t kRequestGoToRegionView = 0x30; // (bool)
constexpr std::size_t kCloseCity = 0x38;
constexpr std::size_t kSaveCity = 0x3C; // (const cIGZString& path, bool fast)
constexpr std::size_t kGetNation = 0x88;
constexpr std::size_t kGetCity = 0x94;
constexpr std::size_t kGetNewCitySpecification = 0x9C;
} // namespace app

namespace image { // cIGZBuffer, by vtable offset
constexpr std::size_t kInit = 0xC; // (width, height, colour type, bits)
constexpr std::size_t kLock = 0x18;
constexpr std::size_t kUnlock = 0x1C;
constexpr std::size_t kGetArea = 0x30;
constexpr std::size_t kGetPixel = 0x54; // (x, y)
constexpr std::size_t kSetPixel = 0x58; // (x, y, colour)
} // namespace image

namespace params { // cIGZCommandParameterSet, by vtable offset
constexpr std::size_t kAt = 0x14;            // (index), never null
constexpr std::size_t kGet = 0x18;           // (index), null when missing
constexpr std::size_t kSet = 0x1C;           // (index, const cIGZVariant&)
constexpr std::size_t kSetStatus = 0x28;     // (status)
constexpr std::size_t kCompareString = 0x2C; // (index, text, case sensitive)
} // namespace params

namespace variant { // cIGZVariant, by vtable offset
constexpr std::size_t kGetBool = 0x24;
constexpr std::size_t kGetUint32 = 0x60;
constexpr std::size_t kGetString = 0xDC; // (cIGZString&)
} // namespace variant

// cISC4RegionalCity, by vtable offset.
constexpr std::size_t kCityGetSize = 0x1C; // (int32_t&, int32_t&)
constexpr std::size_t kCityGetName = 0x84; // (cIGZString&)
constexpr std::size_t kCityGetEstablished = 0xAC;

// cISC4Nation and cISC4Region, by vtable offset.
constexpr std::size_t kNationGetRegion = 0x2C;  // (index)
constexpr std::size_t kRegionGetCity = 0x2C;    // (x, y): the holder or null
constexpr std::size_t kRegionDeleteCity = 0x38; // (holder*&)

// The new city specification's terrain picture (a counted pointer).
constexpr std::size_t kSpecTerrain = 0x200;

// The commands of the screen.
constexpr uint32_t kCmdOpenCity = 0x0BB2747D;
constexpr uint32_t kCmdLoadRegion = 0x0BB3C277;
constexpr uint32_t kCmdStopDownA = 0x2A94826B;
constexpr uint32_t kCmdStopAcrossA = 0x2A94826E;
constexpr uint32_t kCmdStopDownB = 0x2A948272;
constexpr uint32_t kCmdStopAcrossB = 0x2A948275;
constexpr uint32_t kCmdScrollLeft = 0x6A935CD8;
constexpr uint32_t kCmdScrollUp = 0x6A935CDD;
constexpr uint32_t kCmdScrollRight = 0x6A935CE0;
constexpr uint32_t kCmdScrollDown = 0x6A935CE2;
constexpr uint32_t kCmdViewDrop = 0x6A935CF1;
constexpr uint32_t kCmdQuit = 0x6A935E3C;
constexpr uint32_t kCmdRegionBitmapLoad = 0x6A9757C2;
constexpr uint32_t kCmdOption = 0x6AA9FE51;

constexpr std::size_t kOptionByte = 0xF0A; // in the options at 0x00B43CD8

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's image loader (0x30 bytes); +4 is the loaded image.
struct ImageLoader {
    alignas(4) char bytes[0x30];
};

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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& string)
{
    string.body.begin = nullptr;
    string.body.end = nullptr;
    string.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    string.body.capacity = buffer + 8;
    string.body.begin = buffer;
    string.body.end = buffer;
    *buffer = 0;
    string.vtable = addr::SC4String_Vtable;
    string.zero = 0;
}

void DestroyString(GameString& string)
{
    char* const buffer = string.body.begin;
    string.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The number of tile records, as the game works it out (a signed shift).
int32_t TileCount(char* self)
{
    return static_cast<int32_t>(At<char*>(self, screen::kTilesEnd) - At<char*>(self, screen::kTiles)) >> 7;
}

// Half the difference of two lengths, rounded towards zero.
int32_t HalfDifference(int32_t outer, int32_t inner)
{
    return static_cast<int32_t>(static_cast<uint32_t>(outer) - static_cast<uint32_t>(inner)) / 2;
}

// The length from one edge of a rectangle to the other.
int32_t HalfOpen(int32_t from, int32_t to)
{
    return static_cast<int32_t>(static_cast<uint32_t>(to) - static_cast<uint32_t>(from));
}

// A float product as the x87 code makes it (53-bit, stored as a float).
float Product(float a, float b)
{
    return static_cast<float>(static_cast<double>(a) * static_cast<double>(b));
}

// The region the screen shows: the nation's region with the screen's index.
void* ShownRegion(char* self)
{
    void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), app::kGetNation);
    return Virtual<void*>(nation, kNationGetRegion, At<uint32_t>(self, screen::kRegionIndex));
}

// The game's 0x007AEDD0 (thiscall, no arguments): the command
// 'RegionBitmapLoad'. The player picks a picture file in a dialog; the
// picture must be 64 pixels for every km of the region plus one, each way.
// Then every city of the region is deleted with its file, and for every tile
// a new city is made whose terrain is the tile's part of the picture (handed
// over as the new city specification's terrain picture), saved and closed.
// A picture of another size gives a message box and changes nothing.
void __fastcall MakeTerrainFromPicture(char* self, void*)
{
    void* const region = ShownRegion(self);
    GameString path;
    InitString(path);
    uint8_t picked = 0;

    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xF0});
    void* dialog = nullptr;
    if (memory) {
        dialog = This<void*>(addr::SC4UI_F79D810, memory);
        if (dialog)
            AddRef(dialog);
    }
    Virtual<void>(dialog, 0x110, uint32_t{0x10000}, uint32_t{1}); // SetFlag
    Virtual<void>(dialog, 0x110, uint32_t{0x100}, uint32_t{1});

    // Centred on the main window.
    void* const mainWindow = Virtual<void*>(At<void*>(self, screen::kWinManager), 0xC);
    if (mainWindow) {
        const int32_t width = Virtual<int32_t>(dialog, 0xA4);
        const int32_t x = HalfDifference(Virtual<int32_t>(mainWindow, 0xA4), width);
        const int32_t height = Virtual<int32_t>(dialog, 0xA8);
        const int32_t y = HalfDifference(Virtual<int32_t>(mainWindow, 0xA8), height);
        Virtual<void>(dialog, 0xE0, x, y);
    }

    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, dialog, uint32_t{1});
    if (Virtual<int32_t>(At<void*>(self, screen::kWinManager), 0xA4, dialog) != 0) { // DoModalWin
        uint32_t selection[5]; // filled by the dialog's file list; +8 is the path
        if (This<uint8_t>(addr::SC4UI_F79D650, dialog, static_cast<void*>(selection))) {
            Virtual<void*>(&path, 0x34, reinterpret_cast<void*>(selection[2])); // the string's Copy
            picked = 1;
        }
    }
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, dialog);
    Virtual<uint8_t>(At<void*>(self, screen::kWinManager), 0x5C, dialog); // DestroyWindow

    if (!picked) {
        Release(dialog);
        DestroyString(path);
        return;
    }

    void* picture = nullptr;
    if (Cdecl<uint8_t>(addr::RZFile_Exists, static_cast<void*>(&path))) {
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructName, &loader, static_cast<void*>(&path), uint32_t{2}, uint32_t{0});
        void* const loaded = At<void*>(&loader, 0x4);
        if (loaded) {
            This<void*>(addr::SC4RefPtr_Assign, &picture, loaded);
            void* const imageService = Global<void*>(addr::SC4_Service43CD0);
            if (imageService)
                AddRef(imageService);
            void* const application = Global<void*>(addr::SC4_Service43C94);
            const int32_t* const area = Virtual<const int32_t*>(picture, image::kGetArea);

            // The region's extent in km from the tiles.
            int32_t extentX = 0;
            int32_t extentY = 0;
            if (TileCount(self) != 0) {
                char* record = At<char*>(self, screen::kTiles);
                uint32_t i = 0;
                do {
                    const int32_t pastX = static_cast<int32_t>(At<uint32_t>(record, tile::kLastX) + 1);
                    if (extentX < pastX)
                        extentX = pastX;
                    const int32_t pastY = static_cast<int32_t>(At<uint32_t>(record, tile::kLastY) + 1);
                    if (extentY < pastY)
                        extentY = pastY;
                    record += tile::kBytes;
                    ++i;
                } while (i < static_cast<uint32_t>(TileCount(self)));
            }
            const int32_t wantedWidth = static_cast<int32_t>((static_cast<uint32_t>(extentX) << 6) + 1);
            const int32_t wantedHeight = static_cast<int32_t>((static_cast<uint32_t>(extentY) << 6) + 1);

            if (HalfOpen(area[0], area[2]) != wantedWidth || HalfOpen(area[1], area[3]) != wantedHeight) {
                GameString message;
                InitString(message);
                const int32_t height = HalfOpen(area[1], area[3]);
                const int32_t width = HalfOpen(area[0], area[2]);
                void* const manager = At<void*>(self, screen::kWinManager);
                GameString title;
                This<void*>(addr::SC4String_FromText, &title, "Region generation error");
                using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
                void* const text = reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(
                    &message, "Region bitmap size is incorrect -- %dx%d, should be %dx%d", width, height, wantedWidth,
                    wantedHeight);
                Virtual<int32_t>(manager, 0xB0, text, static_cast<void*>(&title), uint32_t{0}, uint32_t{1},
                                 uint32_t{0}); // GZMsgBox
                DestroyString(message);
                DestroyString(title);
                if (imageService)
                    Release(imageService);
                This<void>(addr::SC4ImageLoader_Destruct, &loader);
                Release(picture);
                Release(dialog);
                DestroyString(path);
                return;
            }

            // Every city of the region goes, with its file.
            const int32_t count = TileCount(self);
            if (count > 0) {
                std::ptrdiff_t offset = 0;
                int32_t left = count;
                do {
                    char* const record = At<char*>(self, screen::kTiles) + offset;
                    const uint32_t y = At<uint32_t>(record, tile::kY);
                    const uint32_t x = At<uint32_t>(record, tile::kX);
                    void* holder = Virtual<void*>(region, kRegionGetCity, x, y);
                    if (holder)
                        Virtual<uint8_t>(region, kRegionDeleteCity, &holder);
                    offset += tile::kBytes;
                } while (--left != 0);
            }

            AddRef(self);
            if (count > 0) {
                std::ptrdiff_t offset = 0;
                int32_t left = count;
                do {
                    char* const record = At<char*>(self, screen::kTiles) + offset;
                    const uint32_t y = At<uint32_t>(record, tile::kY);
                    const uint32_t x = At<uint32_t>(record, tile::kX);
                    const uint32_t sizeLog = At<uint8_t>(record, tile::kSizeLog);
                    const int32_t side = int32_t{0x100} >> ((2u - sizeLog) & 0x1F); // cells: 64 for 1 km
                    void* terrain = nullptr;
                    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C9C), 0xC, &terrain) &&
                        Virtual<uint8_t>(terrain, image::kInit, side, side, uint32_t{2}, uint32_t{8})) {
                        if (Virtual<uint8_t>(picture, image::kLock, uint32_t{0x800})) {
                            if (Virtual<uint8_t>(terrain, image::kLock, uint32_t{0x8080})) {
                                const uint32_t left64 = x << 6;
                                const uint32_t top64 = y << 6;
                                for (int32_t column = 0; column < side; ++column) {
                                    for (int32_t row = 0; row < side; ++row) {
                                        const uint32_t pixel = Virtual<uint32_t>(
                                            picture, image::kGetPixel, left64 + static_cast<uint32_t>(column),
                                            top64 + static_cast<uint32_t>(row));
                                        Virtual<void>(terrain, image::kSetPixel, column, row, pixel);
                                    }
                                }
                                Virtual<uint8_t>(terrain, image::kUnlock, uint32_t{0x8080});
                            }
                            Virtual<uint8_t>(picture, image::kUnlock, uint32_t{0x800});
                        }

                        // The picture becomes the new city's terrain while the city is made.
                        char* const spec = Virtual<char*>(application, app::kGetNewCitySpecification);
                        void* const before = At<void*>(spec, kSpecTerrain);
                        if (terrain != before) {
                            if (terrain)
                                AddRef(terrain);
                            At<void*>(spec, kSpecTerrain) = terrain;
                            if (before)
                                Release(before);
                        }
                        void* const holder = This<void*>(addr::cSC4WinRegionScreen_CityAtTile, self,
                                                         static_cast<void*>(record), uint32_t{1});
                        Virtual<uint8_t>(application, app::kRequestNewCity, holder);
                        if (void* const held = At<void*>(spec, kSpecTerrain)) {
                            At<void*>(spec, kSpecTerrain) = nullptr;
                            Release(held);
                        }

                        GameString savePath;
                        InitString(savePath);
                        void* const city = Virtual<void*>(application, app::kGetCity);
                        Virtual<void>(city, 0x28, static_cast<void*>(&savePath)); // GetCitySaveFilePath
                        Virtual<uint8_t>(application, app::kSaveCity, static_cast<void*>(&savePath), uint32_t{0});
                        Virtual<uint8_t>(application, app::kCloseCity);
                        DestroyString(savePath);
                    }
                    offset += tile::kBytes;
                } while (--left != 0);
            }
            Release(self);
            Virtual<uint8_t>(application, app::kRequestGoToRegionView, uint32_t{0});
            if (imageService)
                Release(imageService);
        }
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        if (picture)
            Release(picture);
    }
    Release(dialog);
    DestroyString(path);
}

// The game's 0x007AF4B0 (thiscall, ret 0x10): opens a city of the shown
// region. With a name: the established city of that name. With an empty
// name: the first tile with no established city whose area in km fits the
// size class (0 small = 1, 1 medium = 4, 2 large = 16, anything else any
// size); a city file is made for it. The area is the city's own size, or
// the tile's when there is no city or `tileSize` is set. On success the
// city's name is stored in `out` and 1 returned.
uint8_t __fastcall OpenCityByNameOrSize(char* self, void*, GameString* name, uint32_t sizeClass, GameString* out,
                                        uint8_t tileSize)
{
    const uint8_t noName = name->body.begin == name->body.end;
    uint32_t wantedArea = 0;
    if (sizeClass == 0)
        wantedArea = 1;
    else if (sizeClass == 1)
        wantedArea = 4;
    else if (sizeClass == 2)
        wantedArea = 0x10;

    void* const region = ShownRegion(self);
    char* record = At<char*>(self, screen::kTiles);
    char* const end = At<char*>(self, screen::kTilesEnd);
    if (record == end)
        return 0;
    do {
        void** const holder =
            Virtual<void**>(region, kRegionGetCity, At<uint32_t>(record, tile::kX), At<uint32_t>(record, tile::kY));
        uint8_t established = 0;
        if (holder)
            established = Virtual<uint8_t>(*holder, kCityGetEstablished);

        if (noName) {
            if (!established) {
                uint32_t area;
                if (holder && !tileSize) {
                    uint32_t width = 0;
                    uint32_t height = 0;
                    Virtual<void>(*holder, kCityGetSize, &width, &height);
                    area = height * width;
                } else {
                    area = uint32_t{1} << (At<uint8_t>(record, tile::kSizeLog) & 0x1F);
                    area *= area;
                }
                if (wantedArea == 0 || wantedArea == area) {
                    void** const made =
                        This<void**>(addr::cSC4WinRegionScreen_CityAtTile, self, static_cast<void*>(record), uint32_t{1});
                    if (This<uint8_t>(addr::cSC4WinRegionScreen_OpenCity, self, made)) {
                        Virtual<void>(*made, kCityGetName, static_cast<void*>(out));
                        return 1;
                    }
                }
            }
        } else if (established) {
            GameString cityName;
            InitString(cityName);
            Virtual<void>(*holder, kCityGetName, static_cast<void*>(&cityName));
            const char* theirs = cityName.body.begin;
            const char* ours = name->body.begin;
            const std::ptrdiff_t length = cityName.body.end - theirs;
            if (length == name->body.end - ours) {
                std::ptrdiff_t i = 0;
                while (i != length && theirs[i] == ours[i])
                    ++i;
                if (i == length && This<uint8_t>(addr::cSC4WinRegionScreen_OpenCity, self, holder)) {
                    if (&cityName.body != &out->body)
                        This<void*>(addr::SC4String_AssignRange, &out->body, cityName.body.begin, cityName.body.end,
                                    &tileSize);
                    DestroyString(cityName);
                    return 1;
                }
            }
            DestroyString(cityName);
        }
        record += tile::kBytes;
    } while (record != end);
    return 0;
}

// The game's 0x007AF720 (thiscall, ret 0xC): the screen's commands, called
// by the message target's handler with the command and its parameter sets.
// Always returns 0.
uint8_t __fastcall DoCommand(char* self, void*, uint32_t command, void* input, void* output)
{
    switch (command) {
    case kCmdStopDownA:
    case kCmdStopDownB: {
        char* const child = At<char*>(self, screen::kScroller);
        At<float>(child, scroller::kSpeedY) = Product(At<float>(child, scroller::kKeySpeed), 0.0f);
        break;
    }
    case kCmdStopAcrossA:
    case kCmdStopAcrossB: {
        char* const child = At<char*>(self, screen::kScroller);
        At<float>(child, scroller::kSpeedX) = Product(At<float>(child, scroller::kKeySpeed), 0.0f);
        break;
    }
    case kCmdScrollLeft: {
        char* const child = At<char*>(self, screen::kScroller);
        At<float>(child, scroller::kSpeedX) = Product(At<float>(child, scroller::kKeySpeed), -1.0f);
        break;
    }
    case kCmdScrollUp: {
        char* const child = At<char*>(self, screen::kScroller);
        At<float>(child, scroller::kSpeedY) = Product(At<float>(child, scroller::kKeySpeed), -1.0f);
        break;
    }
    case kCmdScrollRight: {
        char* const child = At<char*>(self, screen::kScroller);
        At<uint32_t>(child, scroller::kSpeedX) = At<uint32_t>(child, scroller::kKeySpeed);
        break;
    }
    case kCmdScrollDown: {
        char* const child = At<char*>(self, screen::kScroller);
        At<uint32_t>(child, scroller::kSpeedY) = At<uint32_t>(child, scroller::kKeySpeed);
        break;
    }
    case kCmdViewDrop:
        This<void>(addr::cSC4WinRegionView_Unpick, At<void*>(self, screen::kRegionView));
        break;
    case kCmdQuit: {
        void* const value = Virtual<void*>(input, params::kAt, uint32_t{0});
        const uint8_t flag = Virtual<uint8_t>(value, variant::kGetBool);
        This<void>(addr::cSC4WinRegionScreen_SaveRegionChoice, self);
        This<void>(addr::cSC4WinRegionScreen_SaveViewPosition, self);
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), app::kRequestQuitFromRegion,
                         uint32_t{flag == 0 ? 1u : 0u});
        break;
    }
    case kCmdLoadRegion: {
        GameString name;
        This<void*>(addr::SC4String_SC4String, &name);
        void* const value = Virtual<void*>(input, params::kAt, uint32_t{0});
        Virtual<uint8_t>(value, variant::kGetString, static_cast<void*>(&name));
        if (!This<uint8_t>(addr::cSC4WinRegionScreen_GoToRegionNamed, self, static_cast<void*>(&name)))
            Virtual<void>(output, params::kSetStatus, uint32_t{2});
        This<void>(addr::SC4String_Destroy, &name);
        break;
    }
    case kCmdOpenCity: {
        GameString name;
        This<void*>(addr::SC4String_SC4String, &name);
        void* const value = Virtual<void*>(input, params::kAt, uint32_t{0});
        Virtual<uint8_t>(value, variant::kGetString, static_cast<void*>(&name));
        uint32_t sizeClass;
        if (Virtual<uint8_t>(input, params::kCompareString, uint32_t{1}, "small", uint32_t{0}))
            sizeClass = 0;
        else if (Virtual<uint8_t>(input, params::kCompareString, uint32_t{1}, "medium", uint32_t{0}))
            sizeClass = 1;
        else if (Virtual<uint8_t>(input, params::kCompareString, uint32_t{1}, "large", uint32_t{0}))
            sizeClass = 2;
        else if (Virtual<uint8_t>(input, params::kCompareString, uint32_t{1}, "any", uint32_t{0}))
            sizeClass = 3;
        else
            sizeClass = Virtual<uint32_t>(Virtual<void*>(input, params::kAt, uint32_t{1}), variant::kGetUint32);
        const uint8_t empty = Virtual<uint8_t>(input, params::kCompareString, uint32_t{2}, "empty", uint32_t{0});
        if (OpenCityByNameOrSize(self, nullptr, &name, sizeClass, &name, empty)) {
            alignas(4) char result[0x14]; // a script value holding the city's name
            void* const made = This<void*>(addr::SC4ScriptNumber_FromString, result, static_cast<void*>(&name));
            Virtual<void>(output, params::kSet, uint32_t{0}, made);
            This<void>(addr::SC4ScriptNumber_Destroy, result);
        } else {
            Virtual<void>(output, params::kSetStatus, uint32_t{2});
        }
        This<void>(addr::SC4String_Destroy, &name);
        break;
    }
    case kCmdRegionBitmapLoad:
        MakeTerrainFromPicture(self, nullptr);
        break;
    case kCmdOption: {
        void* const value = Virtual<void*>(input, params::kGet, uint32_t{0});
        char* const options = Global<char*>(addr::SC4_Options43CD8);
        if (value)
            At<uint8_t>(options, kOptionByte) = Virtual<uint8_t>(value, variant::kGetBool);
        else
            At<uint8_t>(options, kOptionByte) = At<uint8_t>(options, kOptionByte) == 0 ? 1 : 0;
        break;
    }
    default:
        break;
    }
    return 0;
}

bool InstallRegionScreen5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionScreen_MakeTerrainFromPicture, reinterpret_cast<void*>(&MakeTerrainFromPicture)},
        {addr::cSC4WinRegionScreen_OpenCityByNameOrSize, reinterpret_cast<void*>(&OpenCityByNameOrSize)},
        {addr::cSC4WinRegionScreen_DoCommand, reinterpret_cast<void*>(&DoCommand)},
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

const bool g_regionScreen5Registered = registry::Register({
    "region.screen.part5",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionScreen_DoCommand,
    "Our region screen, part 5: the command handler, opening a city by name or size, terrain from a picture",
    false,
    InstallRegionScreen5,
});

} // namespace
} // namespace opensc4::region
