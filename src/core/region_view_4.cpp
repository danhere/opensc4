// The region map window (docs/region.md), part 4: 0x007B51D0 to 0x007B62D0,
// the end of cSC4WinRegionView's code and of its picture helper.
// Replacement, off by default:
//
//   region.view.part4: the map window's Init (slot 4 of the main vtable
//     0x00AB9658), Shutdown (slot 5), SetArea (slot 55), destructor and
//     deleting destructor (slot 148); the functions the region screen calls
//     to hand over a tile record, to pick the tile under a point, to unpick,
//     and to give the name labels' layout; the three workers behind them
//     (a tile's place marked for repainting, its name label made or
//     removed, its airport and seaport markers and effects placed); and the
//     picture helper's set-up, shutdown and deleting destructor.
//
// cSC4WinRegionView (window ID 0x2BA6BB97, 0x128 bytes, constructor
// 0x007B4090; main vtable 0x00AB9658, an interface of four slots with the
// vtable 0x00AB9644 at +0xD8). What this part shows of it:
//
//   +0x0D8  the interface handed to the picture helper as its owner
//   +0x0DC  the nation's index of the region
//   +0x0E0  a picture (0x856DDBAC / 0x6A1EED2C / 0x4A2805FF)
//   +0x0E4  the picked tile record, or null
//   +0x0E8  the view's place (added to a tile's place on the window)
//   +0x0F0  the name labels' layout (instance of a UI resource of the type
//           0x96A006B0; 0 for no labels) and +0x0F4 the window ID in it
//   +0x100  vector of pointers to the screen's tile records
//   +0x10C  the picture helper
//   +0x111  labels on every tile (else only on the picked one)
//   +0x112  labels also on tiles without an established city
//   +0x116  mark the tile of the region's tutorial city
//   +0x11C  vector of two marker pictures (airport, seaport)
//
// The picture helper (0x44 bytes, made by Init; main vtable 0x00AB9630 with
// the interface 0xC989F960, the counted base's vtable 0x00AB9618 at +4)
// keeps the map as a grid of pictures of 256 by 256 pixels:
//
//   +0x0C  vector of 8-byte cells: a picture and a byte 'compose again'
//   +0x18  four dwords, zero after set-up
//   +0x28  a cell's width and +0x2C its height
//   +0x30  columns ((width - 1) / cell width + 2) and +0x34 rows
//   +0x38  the window's width and +0x3C its height
//   +0x40  the owner (the window's interface at +0xD8)
//
// A tile record of the region screen, as far as used here: +0x08 and +0x0C
// its cell in km, +0x1C its picture, +0x30 its name label (a window),
// +0x34 a byte cleared when the map is to be composed again, +0x70 a list
// of running effects, +0x74 a vector of 12-byte markers (x, y on the map
// picture and the marker's picture).
//
// Still the game's: 0x007B51D0 and 0x007B5E90 (the cell vector's fill
// insert and resize, STL code; the resize hands new cells a byte it never
// set), 0x007B62A0 (a cdecl helper of the class that follows, called from
// 0x007BCD1E only). 0x007B6130 is not a function: it is inside Init.
//
// Windows, the window manager, pictures, effects, the 3D view and the
// tutorial registry are the game's, called through their vtables or by
// address.
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

namespace opensc4::region {
namespace {

namespace view { // cSC4WinRegionView
constexpr std::size_t kOwnerPart = 0xD8;
constexpr std::size_t kRegionIndex = 0xDC;
constexpr std::size_t kPicture = 0xE0;
constexpr std::size_t kPicked = 0xE4;
constexpr std::size_t kPlaceX = 0xE8;
constexpr std::size_t kPlaceY = 0xEC;
constexpr std::size_t kLabelLayout = 0xF0;
constexpr std::size_t kLabelId = 0xF4;
constexpr std::size_t kRecords = 0x100;
constexpr std::size_t kRecordsEnd = 0x104;
constexpr std::size_t kRecordsCapacity = 0x108;
constexpr std::size_t kHelper = 0x10C;
constexpr std::size_t kLabelsOnAll = 0x111;
constexpr std::size_t kLabelsOnEmpty = 0x112;
constexpr std::size_t kMarkTutorial = 0x116;
constexpr std::size_t kMarkerPictures = 0x11C;
constexpr std::size_t kMarkerPicturesEnd = 0x120;
} // namespace view

namespace helper { // the picture helper
constexpr std::size_t kCounted = 0x4;
constexpr std::size_t kCells = 0xC;
constexpr std::size_t kCellsEnd = 0x10;
constexpr std::size_t kCellsCapacity = 0x14;
constexpr std::size_t kRect = 0x18;
constexpr std::size_t kCellWidth = 0x28;
constexpr std::size_t kCellHeight = 0x2C;
constexpr std::size_t kColumns = 0x30;
constexpr std::size_t kRows = 0x34;
constexpr std::size_t kWidth = 0x38;
constexpr std::size_t kHeight = 0x3C;
constexpr std::size_t kOwner = 0x40;
constexpr uint32_t kBytes = 0x44;
} // namespace helper

namespace tile { // a tile record of the region screen
constexpr std::size_t kX = 0x8;
constexpr std::size_t kY = 0xC;
constexpr std::size_t kPicture = 0x1C;
constexpr std::size_t kLabel = 0x30;
constexpr std::size_t kComposed = 0x34;
constexpr std::size_t kEffects = 0x70;
constexpr std::size_t kMarkers = 0x74;
constexpr std::size_t kMarkersEnd = 0x78;
constexpr std::size_t kMarkersCapacity = 0x7C;
} // namespace tile

constexpr uint32_t kViewWinId = 0x2BA6BB97;
constexpr uint32_t kWinIid = 0x22BA0121;
constexpr uint32_t kLabelTextIid = 0x5386D516;
constexpr uint32_t kWinManager = 0xA417445E;
constexpr uint32_t kWinManagerIid = 0x5A4;

constexpr uint32_t kPictureType = 0x856DDBAC;
constexpr uint32_t kPictureGroup = 0x6A1EED2C;
constexpr uint32_t kViewPicture = 0x4A2805FF;
constexpr uint32_t kMarkerGroup = 0x46A006B0;

constexpr uint32_t kLayoutType = 0x96A006B0;
constexpr uint32_t kTutorialLayout = 0xCA539343;
constexpr uint32_t kTutorialLabelId = 0x0A551C53;

constexpr int32_t kCellSide = 0x100;

// 1.5 * 2^23: a float sum with it holds the rounded value in its low bits.
constexpr float kRoundMagic = 12582912.0f;
constexpr uint32_t kRoundBias = 0xB4C00000u; // -0x4B400000, the magic's bits

// The game's table at 0x00AB9594: a marker picture's instance, the kind of
// point of interest asked of the city, and the effect's name.
struct MarkerKind {
    uint32_t picture;
    uint32_t kind;
    const char* effect;
};

constexpr MarkerKind kMarkerKinds[] = {
    {0xEBABB1B0, 0, "region_airport"},
    {0xEBABB1B1, 1, "region_seaport"},
};
constexpr std::size_t kMarkerKindCount = sizeof(kMarkerKinds) / sizeof(kMarkerKinds[0]);

// A cell of the helper's grid.
struct Cell {
    void* picture;
    uint8_t composeAgain;
};
static_assert(sizeof(Cell) == 8);

// A marker of a tile record.
struct Marker {
    uint32_t x;
    uint32_t y;
    void* picture;
};
static_assert(sizeof(Marker) == 12);

// A point of interest of a city, in metres within the city.
struct Point {
    float x;
    float y;
    float z;
};
static_assert(sizeof(Point) == 12);

struct PointVector {
    Point* begin;
    Point* end;
    Point* capacity;
};

// A node of the game's list.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};
static_assert(sizeof(ListNode) == 12);

// The game's string and picture loader (0x00602B70 fills it, 0x00602BE0
// empties it).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// What an effect is placed with: two flag bytes, a rotation, a position and
// a scale.
struct Placement {
    uint8_t flagA;
    uint8_t flagB;
    uint8_t pad[2];
    float rotation[9];
    float position[3];
    float scale;
};
static_assert(sizeof(Placement) == 0x38);

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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The game's inlined reference holder: take another object.
void Assign(void*& holder, void* fresh)
{
    void* const old = holder;
    if (fresh == old)
        return;
    if (fresh)
        AddRef(fresh);
    holder = fresh;
    if (old)
        Release(old);
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The bits per pixel of a picture's colour type, as the game's jump table at
// 0x007B6038 has them.
uint32_t BitsOfColourType(uint32_t type)
{
    switch (type) {
    case 2:
        return 8;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return 0x10;
    case 8:
    case 9:
        return 0x20;
    default:
        return 0;
    }
}

void* App()
{
    return Global<void*>(addr::SC4_Service43C94);
}

// The regional city holder at a tile's cell in the window's region, or null.
void* HolderAt(char* self, char* record)
{
    void* const nation = Virtual<void*>(App(), 0x88); // GetNation
    void* const region = Virtual<void*>(nation, 0x2C, At<uint32_t>(self, view::kRegionIndex));
    return Virtual<void*>(region, 0x2C, At<int32_t>(record, tile::kX), At<int32_t>(record, tile::kY));
}

// The game's 0x007B5350, the picture helper's shutdown: shuts every cell's
// picture down, empties the grid and lets the owner go. Returns 1.
uint8_t __fastcall HelperShutdown(char* self, void*)
{
    Cell* const end = At<Cell*>(self, helper::kCellsEnd);
    for (Cell* cell = At<Cell*>(self, helper::kCells); cell != end; ++cell) {
        if (void* const picture = cell->picture)
            Virtual<void>(picture, 0x10);
    }
    This<void*>(addr::SC4RegionViewGrid_EraseCells, self + helper::kCells, At<Cell*>(self, helper::kCells),
                At<Cell*>(self, helper::kCellsEnd));
    Clear(At<void*>(self, helper::kOwner));
    return 1;
}

// The game's 0x007B5300, the picture helper's deleting destructor; the
// counted base's slot 3 comes here through the thunk 0x007B3E70.
void* __fastcall HelperDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4RegionViewHelper_Vtable;
    At<std::uintptr_t>(self, helper::kCounted) = addr::SC4RegionViewHelper_UnknownVtable;
    if (void* const owner = At<void*>(self, helper::kOwner))
        Release(owner);
    This<void>(addr::SC4RegionViewGrid_DestroyCells, self + helper::kCells);
    This<void>(addr::cRZBaseUnknown_Destruct, self + helper::kCounted);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007B5EF0, the picture helper's set-up: (width, height, a
// cell's width and height, the owner). Sizes the grid for the window,
// keeps the owner and gives every cell a fresh picture of the cell's size,
// with 32 bits when the screen has more than 16 and with 16 otherwise. On a
// failure the helper is shut down and the answer is 0.
uint8_t __fastcall HelperSetUp(char* self, void*, int32_t width, int32_t height, int32_t cellWidth,
                               int32_t cellHeight, void* owner)
{
    const int32_t across = (width - 1) / cellWidth;
    At<int32_t>(self, helper::kCellWidth) = cellWidth;
    At<int32_t>(self, helper::kCellHeight) = cellHeight;
    At<int32_t>(self, helper::kWidth) = width;
    At<int32_t>(self, helper::kHeight) = height;
    At<int32_t>(self, helper::kColumns) = across + 2;
    At<int32_t>(self, helper::kRows) = (height - 1) / cellHeight + 2;
    Assign(At<void*>(self, helper::kOwner), owner);

    void* const graphics = Global<void*>(addr::SC4_Service43C9C);
    void* const driver = Virtual<void*>(graphics, 0x20);
    alignas(4) uint32_t mode[4] = {}; // the original leaves 8 bytes for the answer
    const uint32_t* const answer = Virtual<const uint32_t*>(driver, 0x3C, static_cast<void*>(mode));
    const uint32_t colourType = 0x10u < answer[1] ? 9u : 4u;

    const uint32_t count =
        static_cast<uint32_t>(At<int32_t>(self, helper::kColumns) * At<int32_t>(self, helper::kRows));
    This<void>(addr::SC4RegionViewGrid_ResizeCells, self + helper::kCells, count);

    Cell* const end = At<Cell*>(self, helper::kCellsEnd);
    for (Cell* cell = At<Cell*>(self, helper::kCells); cell != end; ++cell) {
        Clear(cell->picture);
        if (!Virtual<uint8_t>(graphics, 0xC, &cell->picture)) { // CreateBuffer
            HelperShutdown(self, nullptr);
            return 0;
        }
        void* const picture = cell->picture;
        const uint32_t bits = BitsOfColourType(colourType);
        if (!Virtual<uint8_t>(picture, 0xC, At<int32_t>(self, helper::kCellWidth),
                              At<int32_t>(self, helper::kCellHeight), colourType, bits)) { // Init
            HelperShutdown(self, nullptr);
            return 0;
        }
    }
    At<uint32_t>(self, helper::kRect + 0x0) = 0;
    At<uint32_t>(self, helper::kRect + 0x4) = 0;
    At<uint32_t>(self, helper::kRect + 0x8) = 0;
    At<uint32_t>(self, helper::kRect + 0xC) = 0;
    return 1;
}

// The game's 0x007B53A0, slot 5 of the map window's main vtable (Shutdown).
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        This<void>(addr::cSC4WinRegionView_ForgetRecords, self);
        This<void*>(addr::SC4RefVector_Erase, self + view::kMarkerPictures,
                    At<void**>(self, view::kMarkerPictures), At<void**>(self, view::kMarkerPicturesEnd));
        if (void* const made = At<void*>(self, view::kHelper)) {
            Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x84, made);
            HelperShutdown(At<char*>(self, view::kHelper), nullptr);
            Clear(At<void*>(self, view::kHelper));
        }
        Clear(At<void*>(self, view::kPicture));
        This<uint8_t>(addr::SC4Win_F99D2FE, self); // the window base class's shutdown
    }
    return 1;
}

// The game's 0x007B5430: (a tile record). Has the whole map composed again
// and sets the record's markers and effects up anew: for each of the city's
// airports and seaports a marker on the map picture and a running effect in
// the 3D view. A tile without a city keeps none.
void __fastcall PlaceMarkers(char* self, void*, char* record)
{
    At<uint8_t>(record, tile::kComposed) = 0;

    // Empty the markers.
    {
        uint8_t unused = 0;
        Marker* const end = At<Marker*>(record, tile::kMarkersEnd);
        At<Marker*>(record, tile::kMarkersEnd) = Cdecl<Marker*>(
            addr::SC4Vector12_Copy, end, end, At<Marker*>(record, tile::kMarkers), &unused, uint32_t{0});
    }

    // Stop the effects and empty their list.
    {
        ListNode* const head = At<ListNode*>(record, tile::kEffects);
        for (ListNode* node = head->next; node != head; node = node->next)
            Virtual<void>(node->value, 0x10, uint32_t{0});
        ListNode* node = At<ListNode*>(record, tile::kEffects)->next;
        if (node != At<ListNode*>(record, tile::kEffects)) {
            do {
                ListNode* const gone = node;
                node = node->next;
                if (void* const effect = gone->value)
                    Release(effect);
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(gone));
            } while (node != At<ListNode*>(record, tile::kEffects));
        }
        At<ListNode*>(record, tile::kEffects)->next = At<ListNode*>(record, tile::kEffects);
        At<ListNode*>(record, tile::kEffects)->previous = At<ListNode*>(record, tile::kEffects);
    }

    // Everything is composed again.
    {
        char* const made = At<char*>(self, view::kHelper);
        Cell* const end = At<Cell*>(made, helper::kCellsEnd);
        for (Cell* cell = At<Cell*>(made, helper::kCells); cell != end; ++cell)
            cell->composeAgain = 1;
    }
    {
        char** const end = At<char**>(self, view::kRecordsEnd);
        for (char** each = At<char**>(self, view::kRecords); each != end; ++each)
            At<uint8_t>(*each, tile::kComposed) = 0;
    }
    Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x50);

    void* const effects = Global<void*>(addr::SC4_Service43D1C);
    void** const holder = static_cast<void**>(HolderAt(self, record));
    if (!holder)
        return;
    void* const city = *holder;

    // The six floats at 0x00B0DBBC: map picture pixels per metre of x, y
    // and z, across and down.
    const float* const scale = reinterpret_cast<const float*>(addr::SC4RegionView_MarkerMatrix);

    // The tile's corner in metres and on the map picture.
    const int32_t cornerX = static_cast<int32_t>(At<uint32_t>(record, tile::kX) << 10);
    const int32_t cornerZ = static_cast<int32_t>(At<uint32_t>(record, tile::kY) << 10);
    const float metresX = static_cast<float>(static_cast<double>(cornerX));
    const float metresZ = static_cast<float>(static_cast<double>(cornerZ));
    const double zAcross = static_cast<double>(cornerZ) * static_cast<double>(scale[4]);
    const double zDown = static_cast<double>(metresZ) * static_cast<double>(scale[5]);
    const double xAcross = static_cast<double>(metresX) * static_cast<double>(scale[0]);
    const float xDown = static_cast<float>(static_cast<double>(metresX) * static_cast<double>(scale[1]));
    const float baseAcross = static_cast<float>(xAcross + zAcross);
    const float baseDown = static_cast<float>(static_cast<double>(xDown) + zDown);

    for (std::size_t kind = 0; kind < kMarkerKindCount; ++kind) {
        if (!At<void**>(self, view::kMarkerPictures)[kind])
            continue;
        PointVector points = {nullptr, nullptr, nullptr};
        Virtual<void>(city, 0x120, kMarkerKinds[kind].kind, &points); // GetPointsOfInterest
        Point* const pointsEnd = points.end;
        for (Point* point = points.begin; point != pointsEnd; ++point) {
            void* const picture = At<void**>(self, view::kMarkerPictures)[kind];
            Marker* const end = At<Marker*>(record, tile::kMarkersEnd);
            Marker* const capacity = At<Marker*>(record, tile::kMarkersCapacity);

            // The point on the map picture, rounded to whole pixels.
            const double zPart = static_cast<double>(scale[4]) * static_cast<double>(point->z);
            const float zPartDown = static_cast<float>(static_cast<double>(point->z) * static_cast<double>(scale[5]));
            const double yPart = static_cast<double>(scale[2]) * static_cast<double>(point->y);
            const float yPartDown = static_cast<float>(static_cast<double>(point->y) * static_cast<double>(scale[3]));
            const float xPart = static_cast<float>(static_cast<double>(scale[0]) * static_cast<double>(point->x));
            const float xPartDown = static_cast<float>(static_cast<double>(point->x) * static_cast<double>(scale[1]));
            const double acrossSum = yPart + static_cast<double>(xPart);
            const float downSum = static_cast<float>(static_cast<double>(xPartDown) + static_cast<double>(yPartDown));
            const double across = zPart + acrossSum;
            const double down = static_cast<double>(downSum) + static_cast<double>(zPartDown);
            const float acrossPlace = static_cast<float>(across + static_cast<double>(baseAcross));
            const float downWide = static_cast<float>((down + static_cast<double>(baseDown)) +
                                                      static_cast<double>(kRoundMagic));
            Marker marker;
            marker.y = Bits(downWide) + kRoundBias;
            const float acrossWide =
                static_cast<float>(static_cast<double>(acrossPlace) + static_cast<double>(kRoundMagic));
            marker.x = Bits(acrossWide) + kRoundBias;
            marker.picture = picture;

            // Append it (the vector's inlined push_back).
            if (end != capacity) {
                if (end)
                    *end = marker;
                At<std::uintptr_t>(record, tile::kMarkersEnd) += sizeof(Marker);
            } else {
                const int32_t size = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(end) -
                                                          At<std::uintptr_t>(record, tile::kMarkers)) /
                                     static_cast<int32_t>(sizeof(Marker));
                const uint32_t grown = (static_cast<uint32_t>(size) < 1u ? 1u : static_cast<uint32_t>(size)) +
                                       static_cast<uint32_t>(size);
                Marker* const fresh =
                    grown != 0 ? Cdecl<Marker*>(addr::SC4_stl_allocate, static_cast<uint32_t>(grown * sizeof(Marker)))
                               : nullptr;
                std::uintptr_t out = reinterpret_cast<std::uintptr_t>(fresh);
                for (Marker* in = At<Marker*>(record, tile::kMarkers); in != end; ++in) {
                    if (out)
                        *reinterpret_cast<Marker*>(out) = *in;
                    out += sizeof(Marker);
                }
                if (out)
                    *reinterpret_cast<Marker*>(out) = marker;
                const std::uintptr_t freshEnd = out + sizeof(Marker);
                if (void* const old = At<void*>(record, tile::kMarkers))
                    Cdecl<void>(addr::SC4_Free, old);
                At<Marker*>(record, tile::kMarkers) = fresh;
                At<std::uintptr_t>(record, tile::kMarkersEnd) = freshEnd;
                At<std::uintptr_t>(record, tile::kMarkersCapacity) =
                    reinterpret_cast<std::uintptr_t>(fresh) + grown * sizeof(Marker);
            }

            // The effect at the point in the 3D view.
            void* effect = nullptr;
            if (Virtual<uint8_t>(effects, 0x1C, kMarkerKinds[kind].effect, &effect)) {
                // The original first fills the rotation with the dword at
                // 0x00B4E1D4 and then overwrites all nine numbers. The two
                // bytes after the flags are never set there.
                Placement placement;
                placement.pad[0] = 0;
                placement.pad[1] = 0;
                void* const made = effect;
                placement.rotation[0] = 1.0f;
                placement.rotation[3] = 0.0f;
                placement.rotation[6] = 0.0f;
                placement.rotation[1] = 0.0f;
                placement.position[2] =
                    static_cast<float>(static_cast<double>(point->z) + static_cast<double>(metresZ));
                placement.position[0] =
                    static_cast<float>(static_cast<double>(point->x) + static_cast<double>(metresX));
                placement.rotation[4] = 1.0f;
                placement.rotation[7] = 0.0f;
                placement.rotation[2] = 0.0f;
                placement.rotation[5] = 0.0f;
                placement.rotation[8] = 1.0f;
                placement.scale = 1.0f;
                placement.position[1] = point->y;
                placement.flagB = 1;
                placement.flagA = 1;
                Virtual<void>(made, 0x1C, &placement);
                Virtual<void>(effect, 0xC, uint32_t{0});

                // Keep it in the record's list (the list's inlined push_back).
                ListNode* const head = At<ListNode*>(record, tile::kEffects);
                ListNode* const node = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{sizeof(ListNode)});
                node->value = effect;
                if (effect)
                    AddRef(effect);
                ListNode* const last = head->previous;
                node->next = head;
                node->previous = last;
                last->next = node;
                head->previous = node;
            }
            if (effect)
                Release(effect);
        }
        if (points.begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(points.begin));
    }
}

// The game's 0x007B59B0: (a tile record). Takes the record's name label
// away and makes a new one when the tile is to have one: every tile with
// an established city when labels are on for all (and the others too with
// +0x112), else the picked tile only; none while no layout is set. The tile
// of the region's tutorial city gets the tutorial's label whatever is set,
// unless it is the picked one. The window manager's value of slot 0x90 is
// read before the label is loaded and put back afterwards (slots 0x60 and
// 0x94).
void __fastcall MakeLabel(char* self, void*, char* record)
{
    if (void* const old = At<void*>(record, tile::kLabel)) {
        Virtual<uint8_t>(self, 0x40, old); // the window takes its child away
        Clear(At<void*>(record, tile::kLabel));
    }

    // Where the region's tutorial city is, if it has one.
    int32_t tutorialX = -1;
    int32_t tutorialY = -1;
    void* const registry = Virtual<void*>(App(), 0xC4); // GetTutorialRegistry
    void* const nation = Virtual<void*>(App(), 0x88);   // GetNation
    void* const open = Virtual<void*>(nation, 0x20);
    if (const uint32_t* const found = This<const uint32_t*>(addr::cSC4TutorialRegistry_FindForRegion, registry, open)) {
        // The city is not checked for null in the original.
        void* const city = This<void*>(addr::cSC4TutorialRegistry_CityOf, registry, *found);
        Virtual<void>(city, 0x14, &tutorialX, &tutorialY); // GetPosition
    }

    bool tutorial = false;
    if (At<uint8_t>(self, view::kMarkTutorial) != 0 && tutorialX == At<int32_t>(record, tile::kX) &&
        tutorialY == At<int32_t>(record, tile::kY) && tutorialX >= 0 && tutorialY >= 0 &&
        record != At<char*>(self, view::kPicked)) {
        tutorial = true;
    } else {
        if (At<uint32_t>(self, view::kLabelLayout) == 0)
            return;
        if (At<uint8_t>(self, view::kLabelsOnAll) == 0 && record != At<char*>(self, view::kPicked))
            return;
    }

    void** const holder = static_cast<void**>(HolderAt(self, record));
    const bool wanted = holder && (tutorial || Virtual<uint8_t>(*holder, 0xAC) != 0); // GetEstablished
    if (!wanted && At<uint8_t>(self, view::kLabelsOnEmpty) == 0)
        return;

    uint32_t labelId = At<uint32_t>(self, view::kLabelId);
    uint32_t key[3] = {0, kLayoutType, At<uint32_t>(self, view::kLabelLayout)};
    if (tutorial) {
        labelId = kTutorialLabelId;
        key[2] = kTutorialLayout;
    }

    // The window manager, used without a reference and without a check.
    void* service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kWinManager, kWinManagerIid, &service);
    void* const manager = service;
    This<void>(addr::SC4RefPtr_Release, &service);
    const uint32_t kept = Virtual<uint32_t>(manager, 0x90);

    void* const loaded = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], static_cast<void*>(self), labelId);
    Assign(At<void*>(record, tile::kLabel), loaded);
    if (void* const label = At<void*>(record, tile::kLabel)) {
        Release(label); // the reference LoadWindowPlain gave
        This<void>(addr::cSC4WinRegionView_FillPanel, self, record, static_cast<void*>(holder));

        // Every button of the label reports to the window's target.
        void* const fresh = At<void*>(record, tile::kLabel);
        void* const target = Virtual<void*>(self, 0x15C);
        Virtual<uint8_t>(fresh, 0x80, kWinIid, reinterpret_cast<void*>(addr::cSC4WinRegionScreen_AimButtonsAt),
                         target);

        // A label with the interface 0x5386D516 gets a handler object.
        void* text = nullptr;
        if (Virtual<uint8_t>(At<void*>(record, tile::kLabel), 0x0, kLabelTextIid, &text)) {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
            void* const handler = memory ? This<void*>(addr::SC4RegionViewLabelProc_Construct, memory) : nullptr;
            Virtual<void>(text, 0x1C, handler);
        }
        if (text)
            Release(text);
    }

    if (Virtual<uint8_t>(manager, 0x60, kept))
        Virtual<void>(manager, 0x94, kept);
}

// The game's 0x007B5CA0: (a tile record). Tells the picture helper that the
// record's place on the map is to be composed again, asks the 3D view for a
// redraw and renews the record's label.
void __fastcall TouchRecord(char* self, void*, char* record)
{
    // The record's place on the window; the original keeps the first
    // number in its argument's stack slot.
    int32_t x = 0;
    int32_t y = 0;
    This<void>(addr::cSC4WinRegionView_TilePlace, self, record, &x, &y);
    uint32_t down = static_cast<uint32_t>(y) + At<uint32_t>(self, view::kPlaceY);
    uint32_t across = static_cast<uint32_t>(x) + At<uint32_t>(self, view::kPlaceX);

    // The picture's area, moved to that place.
    const uint32_t* const area = Virtual<const uint32_t*>(At<void*>(record, tile::kPicture), 0x30); // GetArea
    const uint32_t left = area[0];
    const uint32_t top = area[1];
    const uint32_t right = area[2];
    const uint32_t bottom = area[3];
    across -= left;
    down -= top;
    uint32_t rect[4];
    rect[0] = left + across;
    rect[2] = right + across;
    rect[1] = top + down;
    rect[3] = bottom + down;
    This<void>(addr::SC4RegionViewGrid_MarkStale, At<void*>(self, view::kHelper), &rect[0]);

    Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x50);
    MakeLabel(self, nullptr, record);
}

// The game's 0x007B5D50: (a tile record). The region screen hands every
// record over with it: kept in the window's vector, then placed and marked.
void __fastcall AddRecord(char* self, void*, char* record)
{
    char** const end = At<char**>(self, view::kRecordsEnd);
    if (end != At<char**>(self, view::kRecordsCapacity)) {
        if (end)
            *end = record;
        At<std::uintptr_t>(self, view::kRecordsEnd) += sizeof(char*);
    } else {
        // The value and the unused allocator argument are both the address
        // of the argument's stack slot in the original.
        This<void>(addr::SC4PtrVector_InsertN, self + view::kRecords, end, &record, &record, uint32_t{1},
                   uint32_t{1});
    }
    TouchRecord(self, nullptr, record);
    PlaceMarkers(self, nullptr, record);
}

// The game's 0x007B5DB0: no tile is picked any more.
void __fastcall Unpick(char* self, void*)
{
    char* const old = At<char*>(self, view::kPicked);
    At<char*>(self, view::kPicked) = nullptr;
    if (old)
        TouchRecord(self, nullptr, old);
}

// The game's 0x007B5DD0: (x, y on the window). Picks the tile under the
// point, or none; the newly picked and the formerly picked are renewed.
void __fastcall PickAt(char* self, void*, int32_t x, int32_t y)
{
    char* const hit = This<char*>(addr::cSC4WinRegionView_TileAt, self, x, y);
    char* const old = At<char*>(self, view::kPicked);
    if (hit == old)
        return;
    At<char*>(self, view::kPicked) = hit;
    if (hit)
        TouchRecord(self, nullptr, hit);
    if (old)
        TouchRecord(self, nullptr, old);
}

// The game's 0x007B5E20: (the labels' layout, the window ID in it). Keeps
// the two and renews every record's label.
void __fastcall SetLabelLayout(char* self, void*, uint32_t layout, uint32_t id)
{
    char** each = At<char**>(self, view::kRecords);
    char** const end = At<char**>(self, view::kRecordsEnd);
    At<uint32_t>(self, view::kLabelLayout) = layout;
    At<uint32_t>(self, view::kLabelId) = id;
    for (; each != end; ++each)
        MakeLabel(self, nullptr, *each);
}

// The game's 0x007B5C40, the map window's destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4WinRegionView_Vtable;
    At<std::uintptr_t>(self, view::kOwnerPart) = addr::cSC4WinRegionView_PainterVtable;
    Shutdown(self, nullptr);
    This<void>(addr::SC4RefVector_Destroy, self + view::kMarkerPictures);
    if (void* const made = At<void*>(self, view::kHelper))
        Release(made);
    if (void* const records = At<void*>(self, view::kRecords))
        Cdecl<void>(addr::SC4_Free, records);
    if (void* const picture = At<void*>(self, view::kPicture))
        Release(picture);
    This<void>(addr::GZWin_Destruct, self);
}

// The game's 0x007B5E70, slot 148 of the main vtable: the deleting
// destructor.
void* __fastcall Delete(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The helper is set up for the window's size with cells of 256 by 256 and
// the window's interface as the owner. The height is asked first.
uint8_t SetUpHelper(char* self, void* owner)
{
    char* const made = At<char*>(self, view::kHelper);
    const int32_t height = Virtual<int32_t>(self, 0xA8);
    const int32_t width = Virtual<int32_t>(self, 0xA4);
    return HelperSetUp(made, nullptr, width, height, kCellSide, kCellSide, owner);
}

// The game's 0x007B6060, slot 4 of the main vtable (Init): starts the
// window base class, takes the window ID, makes the picture helper and sets
// it up (0 when that fails), registers the helper with the 3D view (slot
// 0x80: helper, 0, 1000), clears the window flags 0x10000 and 0x8000 and
// loads the window's picture and the two marker pictures. Returns 1, also
// when the window was started before.
uint8_t __fastcall Init(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<uint8_t>(addr::GZWin_Init, self);
    Virtual<uint8_t>(self, 0x100, kViewWinId); // SetID

    char* made = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, helper::kBytes));
    if (made) {
        At<std::uintptr_t>(made, 0x0) = addr::SC4RegionOverlay_BaseVtable;
        This<void>(addr::cRZBaseUnknown_Construct, made + helper::kCounted);
        At<std::uintptr_t>(made, 0x0) = addr::SC4RegionViewHelper_Vtable;
        At<std::uintptr_t>(made, helper::kCounted) = addr::SC4RegionViewHelper_UnknownVtable;
        At<void*>(made, helper::kCells) = nullptr;
        At<void*>(made, helper::kCellsEnd) = nullptr;
        At<void*>(made, helper::kCellsCapacity) = nullptr;
        At<void*>(made, helper::kOwner) = nullptr;
    }
    Assign(At<void*>(self, view::kHelper), made);

    if (!SetUpHelper(self, self + view::kOwnerPart))
        return 0;

    Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x80, At<void*>(self, view::kHelper), uint32_t{0},
                  uint32_t{0x3E8});
    Virtual<uint32_t>(self, 0x110, uint32_t{0x10000}, uint32_t{0}); // SetFlag
    Virtual<uint32_t>(self, 0x110, uint32_t{0x8000}, uint32_t{0});

    ImageLoader own;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &own, kPictureType, kPictureGroup, kViewPicture, uint32_t{1},
                uint32_t{0});
    if (own.image)
        This<void>(addr::SC4RefPtr_Assign, self + view::kPicture, own.image);

    char* const pictures = self + view::kMarkerPictures;
    This<void>(addr::SC4RefVector_Resize, pictures, uint32_t{2});
    for (std::size_t kind = 0; kind < kMarkerKindCount; ++kind) {
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, kPictureType, kMarkerGroup,
                    kMarkerKinds[kind].picture, uint32_t{1}, uint32_t{0});
        if (void* const image = loader.image)
            Assign(At<void**>(pictures, 0x0)[kind], image);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    }
    This<void>(addr::SC4ImageLoader_Destruct, &own);
    return 1;
}

// The game's 0x007B6240, slot 55 of the main vtable (SetArea): the window
// base class's, then the picture helper is set up again for the new size.
// The helper is not checked for null. Returns the helper's answer.
uint8_t __fastcall SetArea(char* self, void*, int32_t a, int32_t b, int32_t c, int32_t d)
{
    This<uint8_t>(addr::GZWin_SetArea, self, a, b, c, d);
    return SetUpHelper(self, self ? self + view::kOwnerPart : nullptr);
}

bool InstallRegionView4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4RegionViewGrid_DeletingDestructor, reinterpret_cast<void*>(&HelperDelete)},
        {addr::SC4RegionViewGrid_Shutdown, reinterpret_cast<void*>(&HelperShutdown)},
        {addr::cSC4WinRegionView_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4WinRegionView_PlaceMarkers, reinterpret_cast<void*>(&PlaceMarkers)},
        {addr::cSC4WinRegionView_MakeLabel, reinterpret_cast<void*>(&MakeLabel)},
        {addr::cSC4WinRegionView_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4WinRegionView_TouchRecord, reinterpret_cast<void*>(&TouchRecord)},
        {addr::cSC4WinRegionView_AddRecord, reinterpret_cast<void*>(&AddRecord)},
        {addr::cSC4WinRegionView_Unpick, reinterpret_cast<void*>(&Unpick)},
        {addr::cSC4WinRegionView_PickAt, reinterpret_cast<void*>(&PickAt)},
        {addr::cSC4WinRegionView_SetLabelLayout, reinterpret_cast<void*>(&SetLabelLayout)},
        {addr::cSC4WinRegionView_DeletingDestructor, reinterpret_cast<void*>(&Delete)},
        {addr::SC4RegionViewGrid_SetUp, reinterpret_cast<void*>(&HelperSetUp)},
        {addr::cSC4WinRegionView_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4WinRegionView_SetArea, reinterpret_cast<void*>(&SetArea)},
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

const bool g_regionView4Registered = registry::Register({
    "region.view.part4",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionView_Init,
    "Our region map window, part 4: Init, Shutdown, SetArea, the destructors, tile records, picking, labels, markers",
    false,
    InstallRegionView4,
});

} // namespace
} // namespace opensc4::region
