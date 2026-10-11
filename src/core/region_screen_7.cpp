// The region screen window (docs/region.md), part 7: 0x007B0AF0 to
// 0x007B1900. The window class 0xEA659793 (cSC4WinRegionScreen, 0x200 bytes,
// made by 0x004E0D40, which keeps it at its own +0xEC) is a window with two
// more parts:
//
//   +0x00  the window (main vtable 0x00AB9260, 151 slots: 3 0x007AB9F0,
//          4 Init 0x007B1900, 5 Shutdown 0x007B0F60, 148 the deleting
//          destructor 0x007B2320)
//   +0xD8  the screen's own interface (IID 0xC6AE7085, vtable 0x00AB924C)
//   +0xDC  a message target (vtable 0x00AB923C: slot 3 DoMessage 0x007B0AF0)
//   +0xE0  the region map window; +0xE4 the overlay window (shown while the
//          preferences' byte +0xF11 is set); +0xE8 the plain window behind
//   +0xEC, +0xF8  three counted pointers each; +0x104, +0x108 two more
//   +0x10C the set of message IDs the target listens for
//   +0x118 a vector of city records, 0x80 bytes each (below)
//   +0x124 twelve pictures (six pairs); +0x154 to +0x174 nine more pointers
//          (+0x164 is the camera control)
//   +0x178 to +0x194 dwords, 0 when made; +0x198, +0x19C 150
//   +0x1A0, +0x1A1, +0x1A2 bytes 0, 0x80, 0x40
//   +0x1A4 the index of the region shown (the nation's GetRegion takes it)
//   +0x1A8 1; +0x1B0 0x808080; +0x1B4 0xFFFF00
//   +0x1B8, +0x1D0 two stopwatches
//   +0x1E8 0; +0x1EC 5.0f; +0x1F0 1200.0f; +0x1F4, +0x1F8 0; +0x1FC, +0x1FD
//          bytes
//
// A city record is one layout tile of the region as the map shows it:
//
//   +0x00, +0x04  the tile's last cell (x + side - 1, y + side - 1)
//   +0x08, +0x0C  its first cell (x, y), km
//   +0x10, +0x14  where it goes in the map's picture (floats)
//   +0x18  log2 of the side in km (byte)
//   +0x1C, +0x20  the city's first picture and its mask; +0x24, +0x28 the
//          second pair; +0x2C, +0x30 two more counted pointers
//   +0x34  a byte, 0 when made
//   +0x38, +0x44, +0x50, +0x5C  four vectors; +0x68, +0x6C -1 when made
//   +0x70  a list (its sentinel node); +0x74 a vector
//
// Replacement, off by default:
//
//   region.screen.part7: the message target's DoMessage and its
//     QueryInterface thunk, the interface's QueryInterface thunk, the
//     constructor, Shutdown, the destructor, the city records made for the
//     region shown (0x007B13C0), and the record vector's erase and growing
//     insert.
//
// Still the game's: 0x007B0E40 and 0x007B0E50, the target's AddRef and
// Release thunks, which the small map window's target (vtable 0x00AB837C)
// shares through code folding.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::region {
namespace {

namespace screen {
constexpr std::size_t kClassId = 0x10; // the window base's field
constexpr std::size_t kInterface = 0xD8;
constexpr std::size_t kTarget = 0xDC;
constexpr std::size_t kMapWindow = 0xE0;
constexpr std::size_t kOverlay = 0xE4;
constexpr std::size_t kBackWindow = 0xE8;
constexpr std::size_t kTripleEC = 0xEC;
constexpr std::size_t kTripleF8 = 0xF8;
constexpr std::size_t kPointer104 = 0x104;
constexpr std::size_t kPointer108 = 0x108;
constexpr std::size_t kMessages = 0x10C;
constexpr std::size_t kRecords = 0x118; // begin, end, capacity
constexpr std::size_t kPairs = 0x124;   // twelve pointers
constexpr std::size_t kPointer154 = 0x154;
constexpr std::size_t kCameraControl = 0x164;
constexpr std::size_t kPointer174 = 0x174;
constexpr std::size_t kRegionIndex = 0x1A4;
constexpr std::size_t kStopwatch1 = 0x1B8;
constexpr std::size_t kStopwatch2 = 0x1D0;
} // namespace screen

namespace record {
constexpr std::size_t kLastX = 0x00;
constexpr std::size_t kLastY = 0x04;
constexpr std::size_t kX = 0x08;
constexpr std::size_t kY = 0x0C;
constexpr std::size_t kPictureX = 0x10;
constexpr std::size_t kPictureY = 0x14;
constexpr std::size_t kLog2 = 0x18;
constexpr std::size_t kPicture = 0x1C;
constexpr std::size_t kMask = 0x20;
constexpr std::size_t kPicture2 = 0x24;
constexpr std::size_t kMask2 = 0x28;
constexpr std::size_t kByte34 = 0x34;
constexpr std::size_t kList = 0x70;
constexpr std::size_t kSize = 0x80;
} // namespace record

constexpr uint32_t kWindowClass = 0xEA659793;
constexpr uint32_t kCommandMessageIid = 0x4B99446A;
constexpr uint32_t kMessage231BBF91 = 0x231BBF91;
constexpr uint32_t kMessage2B96B3EA = 0x2B96B3EA;
constexpr uint32_t kPreRegionShutdown = 0x8BB5BB46;
constexpr uint32_t kPostRegionShutdown = 0x8BB5BB4B;

// The commands Shutdown takes back, in its order.
constexpr uint32_t kCommands[] = {
    0x6A935E3C, 0x6A9757C2, 0x6A935CD8, 0x2A948275, 0x2A94826E, 0x6A935CE0, 0x6A935CDD,
    0x2A948272, 0x2A94826B, 0x6A935CE2, 0x6AA9FE51, 0x0BB3C277, 0x0BB2747D, 0x6A935CF1,
};

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A layout tile as the region gives it.
struct Tile {
    int32_t x;
    int32_t y;
    int32_t log2;
};
static_assert(sizeof(Tile) == 0xC);

struct TileVector {
    Tile* begin;
    Tile* end;
    Tile* capacity;
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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The holder is emptied before its object is released.
void ReleaseAndClear(void** holder)
{
    if (void* const object = *holder) {
        *holder = nullptr;
        Release(object);
    }
}

void ReleaseIfSet(void* object)
{
    if (object)
        Release(object);
}

void DestroyRecord(char* item)
{
    This<void>(addr::SC4RegionTileRecord_Destruct, item);
}

// The game's 0x007B0AF0: DoMessage of the message target (+0xDC). A message
// that has the command interface goes to the command handler (0x007AF720)
// with its type and two of its values; two plain messages switch flag 1
// (shown) of the overlay window and of the window behind. Always answers 1.
uint8_t __fastcall TargetDoMessage(char* target, void*, void* message)
{
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    void* command = nullptr;
    if (Virtual<uint8_t>(message, 0x0, kCommandMessageIid, &command)) {
        // Both values are asked of the message itself, not of the interface.
        void* const second = Virtual<void*>(message, 0x28);
        void* const third = Virtual<void*>(message, 0x3C);
        This<void>(addr::cSC4WinRegionScreen_DoCommand, target - screen::kTarget, type, second, third);
    } else if (type == kMessage231BBF91) {
        char* const preferences = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98);
        if (void* const child = At<void*>(target, screen::kOverlay - screen::kTarget)) {
            // The original sets only the low byte of the register it pushes;
            // the rest is the preferences' address.
            const bool on = At<uint8_t>(preferences, 0xF11) != 0;
            const std::uintptr_t pushed =
                (reinterpret_cast<std::uintptr_t>(preferences) & ~std::uintptr_t{0xFF}) | (on ? 1u : 0u);
            Virtual<void>(child, 0x110, uint32_t{1}, pushed);
        }
    } else if (type == kMessage2B96B3EA) {
        This<void>(addr::cSC4CameraControl_PostSetUp, At<void*>(target, screen::kCameraControl - screen::kTarget));
        void* const child = At<void*>(target, screen::kBackWindow - screen::kTarget);
        Virtual<void>(child, 0x110, uint32_t{1}, uint32_t{1});
    }
    if (command)
        Release(command);
    return 1;
}

// The game's 0x007B0BB0: erases [first, last) from a vector of city records
// (`this` is the vector); returns first.
char* __fastcall RecordsErase(char* vector, void*, char* first, char* last)
{
    char* const newEnd = Cdecl<char*>(addr::SC4RegionTileRecord_AssignRange, last, At<char*>(vector, 0x4), first,
                                      static_cast<void*>(&first), uint32_t{0});
    char* const oldEnd = At<char*>(vector, 0x4);
    for (char* item = newEnd; item != oldEnd; item += record::kSize)
        DestroyRecord(item);
    At<char*>(vector, 0x4) = newEnd;
    return first;
}

// The game's 0x007B0C00: the constructor.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, screen::kInterface) = addr::SC4KeyFilter_BaseVtable;
    At<std::uintptr_t>(self, screen::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0x0) = addr::SC4RegionScreen_Vtable;
    At<std::uintptr_t>(self, screen::kInterface) = addr::SC4RegionScreen_VtableD8;
    At<std::uintptr_t>(self, screen::kTarget) = addr::SC4RegionScreen_VtableDC;
    for (std::size_t offset = screen::kMapWindow; offset <= screen::kPointer108; offset += 4)
        At<uint32_t>(self, offset) = 0;
    char* const messages = self + screen::kMessages;
    This<void*>(addr::SC4MessageIdSet_Construct, messages);
    for (std::size_t offset = screen::kRecords; offset <= 0x194; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x198) = 0x96;
    At<uint32_t>(self, 0x19C) = 0x96;
    At<uint8_t>(self, 0x1A0) = 0;
    At<uint8_t>(self, 0x1A1) = 0x80;
    At<uint8_t>(self, 0x1A2) = 0x40;
    At<uint32_t>(self, screen::kRegionIndex) = 0;
    At<uint32_t>(self, 0x1A8) = 1;
    At<uint32_t>(self, 0x1B0) = 0x808080;
    At<uint32_t>(self, 0x1B4) = 0xFFFF00;
    This<void*>(addr::SC4Stopwatch_Construct, self + screen::kStopwatch1, int32_t{4});
    This<void*>(addr::SC4Stopwatch_Construct, self + screen::kStopwatch2, int32_t{4});
    At<uint32_t>(self, 0x1E8) = 0;
    At<uint32_t>(self, 0x1EC) = 0x40A00000; // 5.0f
    At<uint32_t>(self, 0x1F0) = 0x44960000; // 1200.0f
    At<uint32_t>(self, 0x1F4) = 0;
    At<uint32_t>(self, 0x1F8) = 0;
    At<uint8_t>(self, 0x1FC) = 0;
    At<uint8_t>(self, 0x1FD) = 0;
    This<uint32_t>(addr::GZWin_SetFlagDirect, self, uint32_t{0x200000}, uint32_t{1});
    This<uint32_t>(addr::GZWin_SetFlagDirect, self, uint32_t{0x10000}, uint32_t{0});
    At<uint32_t>(self, screen::kClassId) = kWindowClass;
    // The seventeen message IDs the target will listen for.
    This<uint8_t>(addr::SC4MessageIdSet_InsertRange, messages,
                  reinterpret_cast<const uint32_t*>(addr::SC4RegionScreen_Messages), uint32_t{0x11});
    return self;
}

// The game's 0x007B0E20: QueryInterface of the screen's interface (+0xD8).
uint8_t __fastcall InterfaceQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::cSC4WinRegionScreen_QueryInterface, self - screen::kInterface, iid, out);
}

// The game's 0x007B0E30: QueryInterface of the message target (+0xDC).
uint8_t __fastcall TargetQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::cSC4WinRegionScreen_QueryInterface, self - screen::kTarget, iid, out);
}

// The game's 0x007B0E60: inserts `count` copies of a record at `position`
// into a vector of city records that has no room left (`this` is the
// vector): new storage of the old size plus the larger of it and the count,
// the records copied over, the old ones destroyed. `tag` is not used.
void __fastcall RecordsInsertOverflow(char* vector, void*, char* position, char* value, void* tag, uint32_t count,
                                      uint32_t atEnd)
{
    (void)tag;
    const uint32_t size = static_cast<uint32_t>((At<char*>(vector, 0x4) - At<char*>(vector, 0x0)) >> 7);
    const uint32_t capacity = (size < count ? count : size) + size;
    char* const storage = capacity ? Cdecl<char*>(addr::SC4_stl_allocate, capacity << 7) : nullptr;

    // The helpers take a fourth argument they do not read (the original
    // passes the address of its last argument).
    void* const unused = &atEnd;
    char* finish = Cdecl<char*>(addr::SC4RegionTileRecord_UninitializedCopy, At<char*>(vector, 0x0), position, storage, unused);
    if (count == 1) {
        if (finish)
            This<void>(addr::SC4RegionTileRecord_CopyConstruct, finish, value);
        finish += record::kSize;
    } else {
        finish = Cdecl<char*>(addr::SC4RegionTileRecord_UninitializedFillN, finish, count, value, unused);
    }
    if (!static_cast<uint8_t>(atEnd))
        finish = Cdecl<char*>(addr::SC4RegionTileRecord_UninitializedCopy, position, At<char*>(vector, 0x4), finish, unused);

    char* const oldEnd = At<char*>(vector, 0x4);
    for (char* item = At<char*>(vector, 0x0); item != oldEnd; item += record::kSize)
        DestroyRecord(item);
    if (char* const old = At<char*>(vector, 0x0))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(old));
    At<char*>(vector, 0x4) = finish;
    At<char*>(vector, 0x8) = storage + (capacity << 7);
    At<char*>(vector, 0x0) = storage;
}

// The game's 0x007B0F60: the window's Shutdown (slot 5). Posts the message
// before the region is shut down, deletes two child windows, stops
// listening, takes its fourteen commands back, empties the city records,
// lets go of everything it holds, shuts the window down and posts the
// message after. Always answers 1.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<void>(addr::cSC4WinRegionScreen_PostPlainMessage, self, kPreRegionShutdown);

    for (const std::size_t offset : {screen::kBackWindow, screen::kMapWindow}) {
        if (void* const child = At<void*>(self, offset)) {
            Virtual<uint8_t>(self, 0x40, child); // ChildDelete
            ReleaseAndClear(&At<void*>(self, offset));
        }
    }

    This<void>(addr::SC4MessageIdSet_RemoveNotifications, self + screen::kMessages,
               static_cast<void*>(self + screen::kTarget), static_cast<void*>(nullptr));

    if (void* const commands = Global<void*>(addr::SC4_Commands)) {
        for (const uint32_t command : kCommands)
            Virtual<uint8_t>(commands, 0x24, command); // UnregisterCommand
    }

    RecordsErase(self + screen::kRecords, nullptr, At<char*>(self, screen::kRecords),
                 At<char*>(self, screen::kRecords + 0x4));
    This<void>(addr::cSC4WinRegionScreen_ShutDownView, self);

    ReleaseAndClear(&At<void*>(self, screen::kPointer154));
    ReleaseAndClear(&At<void*>(self, screen::kOverlay));
    ReleaseAndClear(&At<void*>(self, screen::kPointer104));
    ReleaseAndClear(&At<void*>(self, screen::kPointer174));
    ReleaseAndClear(&At<void*>(self, screen::kPointer108));
    for (std::size_t i = 0; i < 3; ++i)
        ReleaseAndClear(&At<void*>(self, screen::kTripleEC + i * 4));
    for (std::size_t i = 0; i < 3; ++i)
        ReleaseAndClear(&At<void*>(self, screen::kTripleF8 + i * 4));
    for (std::size_t i = 0; i < 12; ++i)
        ReleaseAndClear(&At<void*>(self, screen::kPairs + i * 4));

    if (void* const resources = Global<void*>(addr::SC4_ResourceManager))
        Virtual<void>(resources, 0xAC);
    This<uint8_t>(addr::SC4Win_F99D2FE, self);

    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
        void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
        if (message)
            AddRef(message);
        Virtual<void>(message, 0x14, kPostRegionShutdown);
        Virtual<void>(server, 0x10, message, int32_t{0});
        Release(message);
    }
    return 1;
}

// The game's 0x007B1200: the destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4RegionScreen_Vtable;
    At<std::uintptr_t>(self, screen::kInterface) = addr::SC4RegionScreen_VtableD8;
    At<std::uintptr_t>(self, screen::kTarget) = addr::SC4RegionScreen_VtableDC;
    Shutdown(self, nullptr);
    This<void>(addr::SC4Stopwatch_Destruct, self + screen::kStopwatch2);
    This<void>(addr::SC4Stopwatch_Destruct, self + screen::kStopwatch1);

    // Nine pointers from +0x174 down to +0x154, then the twelve below them.
    for (std::size_t offset = screen::kPointer174; offset >= screen::kPairs; offset -= 4)
        ReleaseIfSet(At<void*>(self, offset));

    char* const end = At<char*>(self, screen::kRecords + 0x4);
    for (char* item = At<char*>(self, screen::kRecords); item != end; item += record::kSize)
        DestroyRecord(item);
    if (char* const storage = At<char*>(self, screen::kRecords))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(storage));
    This<void>(addr::SC4MessageIdSet_Destruct, self + screen::kMessages);

    // From +0x108 down to +0xE0.
    for (std::size_t offset = screen::kPointer108; offset >= screen::kMapWindow; offset -= 4)
        ReleaseIfSet(At<void*>(self, offset));
    This<void>(addr::GZWin_Destruct, self);
}

int32_t Width(const int32_t* area)
{
    return area[2] - area[0];
}

int32_t Height(const int32_t* area)
{
    return area[3] - area[1];
}

// The pictures of one city, read from its file into the record. False when
// they could not be read or the first picture and its mask differ in size
// (the caller then marks the record again). A second pair that does not
// match is dropped.
uint8_t LoadRecordPictures(char* item, GameString* path)
{
    void** const picture = &At<void*>(item, record::kPicture);
    void** const mask = &At<void*>(item, record::kMask);
    void** const picture2 = &At<void*>(item, record::kPicture2);
    void** const mask2 = &At<void*>(item, record::kMask2);
    ReleaseAndClear(mask2);
    ReleaseAndClear(picture2);
    ReleaseAndClear(mask);
    ReleaseAndClear(picture);

    uint8_t loaded = Cdecl<uint8_t>(addr::SC4_LoadSavePictures, path, picture, mask, picture2, mask2);
    if (!loaded)
        return 0;

    if (void* const first = *picture) {
        if (void* const firstMask = *mask) {
            const int32_t* area = Virtual<int32_t*>(firstMask, 0x30);
            const int32_t height = Height(area);
            const int32_t width = Width(area);
            area = Virtual<int32_t*>(first, 0x30);
            if (Width(area) != width || Height(area) != height)
                loaded = 0;
        }
    }

    if (*picture) {
        void* const second = *picture2;
        if (second && *mask2) {
            const int32_t* area = Virtual<int32_t*>(*mask2, 0x30);
            const int32_t height = Height(area);
            const int32_t width = Width(area);
            area = Virtual<int32_t*>(second, 0x30);
            bool drop = true;
            if (Width(area) == width && Height(area) == height) {
                // The second picture must also be as large as the first.
                void* const first = *picture;
                void* const other = *picture2;
                area = Virtual<int32_t*>(first, 0x30);
                int32_t firstSize[2] = {Width(area), Height(area)};
                area = Virtual<int32_t*>(other, 0x30);
                int32_t otherSize[2] = {Width(area), Height(area)};
                drop = Cdecl<uint8_t>(addr::SC4_CellPairsDiffer, static_cast<int32_t*>(otherSize),
                                      static_cast<int32_t*>(firstSize)) != 0;
            }
            if (drop) {
                ReleaseAndClear(mask2);
                ReleaseAndClear(picture2);
            }
        }
    }
    return loaded;
}

// The game's 0x007B13C0: makes the city records of the region shown. For
// every layout tile of the region, in sorted order, a record is added with
// the tile's cells and its place in the map's picture; where the region has
// a city, the pictures are read from the city's file. Every record is then
// given to the region map window.
void __fastcall BuildRecords(char* self, void*)
{
    void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
    void* const region = Virtual<void*>(nation, 0x2C, At<uint32_t>(self, screen::kRegionIndex));
    TileVector tiles = {nullptr, nullptr, nullptr};
    Virtual<void>(region, 0x5C, &tiles); // GetCityLocations
    // The ordering object is an empty one passed by value.
    Cdecl<void>(addr::SC4RegionTileSort_Sort, tiles.begin, tiles.end, uint32_t{0});

    uint8_t loaded = 0; // not set before its first use as the unused tag in the original
    Tile* const tilesEnd = tiles.end;
    for (Tile* tile = tiles.begin; tile != tilesEnd; ++tile) {
        // An empty record, added at the end of the vector.
        alignas(4) char fresh[record::kSize] = {};
        for (std::size_t offset = record::kPicture; offset <= 0x30; offset += 4)
            At<uint32_t>(fresh, offset) = 0;
        for (std::size_t offset = 0x38; offset <= 0x64; offset += 4)
            At<uint32_t>(fresh, offset) = 0;
        At<int32_t>(fresh, 0x68) = -1;
        At<int32_t>(fresh, 0x6C) = -1;
        void** const node = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{0xC});
        node[0] = node;
        node[1] = node;
        At<void**>(fresh, record::kList) = node;
        char* const records = self + screen::kRecords;
        char* const end = At<char*>(records, 0x4);
        char* const capacity = At<char*>(records, 0x8);
        At<uint32_t>(fresh, 0x74) = 0;
        At<uint32_t>(fresh, 0x78) = 0;
        At<uint32_t>(fresh, 0x7C) = 0;
        if (end != capacity) {
            if (end)
                This<void>(addr::SC4RegionTileRecord_CopyConstruct, end, static_cast<char*>(fresh));
            At<char*>(records, 0x4) += record::kSize;
        } else {
            RecordsInsertOverflow(records, nullptr, end, fresh, &loaded, 1, 1);
        }
        DestroyRecord(fresh);

        char* const item = At<char*>(self, screen::kRecords + 0x4) - record::kSize;
        const uint32_t tileSide = 1u << (tile->log2 & 0x1F);
        At<int32_t>(item, record::kLastX) = static_cast<int32_t>(tileSide + static_cast<uint32_t>(tile->x) - 1u);
        At<int32_t>(item, record::kLastY) = static_cast<int32_t>(tileSide + static_cast<uint32_t>(tile->y) - 1u);
        At<int32_t>(item, record::kX) = tile->x;
        At<int32_t>(item, record::kY) = tile->y;
        const uint8_t log2 = static_cast<uint8_t>(tile->log2);
        At<uint8_t>(item, record::kLog2) = log2;
        const int32_t side = static_cast<int32_t>(1u << (log2 & 0x1F));
        const int32_t y = At<int32_t>(item, record::kY);
        At<uint8_t>(item, record::kByte34) = 0;
        const int32_t farY = y + side;
        const int32_t x = At<int32_t>(item, record::kX);
        const int32_t farX = side + x;
        // Four floats: the picture's x and y for a step of one cell in x,
        // then the same for a step in y.
        const float* const factors = reinterpret_cast<const float*>(addr::SC4RegionScreen_DriftDirection);
        At<float>(item, record::kPictureX) = static_cast<float>(
            static_cast<double>(x) * static_cast<double>(factors[0]) +
            static_cast<double>(farY) * static_cast<double>(factors[2]));
        At<float>(item, record::kPictureY) = static_cast<float>(
            static_cast<double>(farX) * static_cast<double>(factors[1]) +
            static_cast<double>(farY) * static_cast<double>(factors[3]));

        void** const holder = Virtual<void**>(region, 0x2C, x, y); // GetCity
        This<void>(addr::cSC4WinRegionScreen_GiveStockPictures, self, item);
        if (holder) {
            void* const city = *holder;
            if (city)
                AddRef(city);
            GameString path;
            path.begin = nullptr;
            path.end = nullptr;
            path.capacity = nullptr;
            char* const text = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
            path.begin = text;
            path.end = text;
            path.capacity = text + 8;
            *text = 0;
            path.vtable = addr::SC4String_Vtable;
            path.zero = 0;
            Virtual<void>(city, 0x7C, &path); // GetCitySaveFilePath
            if (path.begin != path.end) {
                loaded = LoadRecordPictures(item, &path);
                if (!loaded)
                    This<void>(addr::cSC4WinRegionScreen_GiveStockPictures, self, item);

                // A second picture without its mask, or a mask without its
                // picture, is shut down and let go.
                void** const picture2 = &At<void*>(item, record::kPicture2);
                void** const mask2 = &At<void*>(item, record::kMask2);
                if (!(*picture2 && *mask2)) {
                    if (void* const second = *picture2) {
                        Virtual<void>(second, 0x10);
                        ReleaseAndClear(picture2);
                    }
                    if (void* const secondMask = *mask2) {
                        Virtual<void>(secondMask, 0x10);
                        ReleaseAndClear(mask2);
                    }
                }
            }
            path.vtable = addr::SC4String_Vtable;
            if (path.begin)
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(path.begin));
            Release(city);
        }

        This<void>(addr::cSC4WinRegionScreen_PrepareTilePictures, self, item);
        void** const mask = &At<void*>(item, record::kMask);
        if (void* const firstMask = *mask) {
            Virtual<void>(firstMask, 0x10);
            ReleaseAndClear(mask);
        }
    }

    char* const recordsEnd = At<char*>(self, screen::kRecords + 0x4);
    for (char* item = At<char*>(self, screen::kRecords); item != recordsEnd; item += record::kSize)
        This<void>(addr::cSC4WinRegionView_AddRecord, At<void*>(self, screen::kMapWindow), item);
    This<void>(addr::cSC4WinRegionView_SetLabelLayout, At<void*>(self, screen::kMapWindow), uint32_t{0}, uint32_t{0});
    This<void>(addr::cSC4WinRegionScreen_LayOutMap, self);

    if (tiles.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(tiles.begin));
}

bool InstallRegionScreen7(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[10] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionScreen_TargetDoMessage, reinterpret_cast<void*>(&TargetDoMessage)},
        {addr::SC4RegionTileRecord_VectorErase, reinterpret_cast<void*>(&RecordsErase)},
        {addr::cSC4WinRegionScreen_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4WinRegionScreen_InterfaceQueryInterface, reinterpret_cast<void*>(&InterfaceQueryInterface)},
        {addr::cSC4WinRegionScreen_TargetQueryInterface, reinterpret_cast<void*>(&TargetQueryInterface)},
        {addr::SC4RegionTileRecord_VectorInsertOverflow, reinterpret_cast<void*>(&RecordsInsertOverflow)},
        {addr::cSC4WinRegionScreen_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4WinRegionScreen_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4WinRegionScreen_BuildCityRecords, reinterpret_cast<void*>(&BuildRecords)},
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

const bool g_regionScreen7Registered = registry::Register({
    "region.screen.part7",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionScreen_Construct,
    "Our region screen, part 7: the message target, the constructor, Shutdown, the destructor, the city records",
    false,
    InstallRegionScreen7,
});

} // namespace
} // namespace opensc4::region
