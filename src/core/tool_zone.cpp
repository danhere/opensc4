// The zoning tool, the view input control the player holds after choosing a
// zone type (or de-zoning) in the zoning menu (0x004DA5A0 to 0x004DC1A0), and
// the small cheat tool before it (0x004D9E50 to 0x004DA1B0). Replacement, off
// by default:
//
//   ui.tools.zone: the zoning tool (vtable 0x00A92CA8, message target
//     0x00A92C94 at +0x28, 0x98 bytes, made by 0x004DAE80 with the zone type
//     from the view's command 0x4A2B2466 handler 0x007E6DA0) and the first
//     functions of the cheat tool (vtable 0x00A92AC0, 0x38 bytes, made by the
//     view's cheat 0x0BFD8064): construction, Init, Shutdown, destruction.
//
// The zoning tool turns a drag into a rectangle of cells and asks the zone
// manager's PlaceZone (slot 11, docs/zonemgr.md) what it would do: with
// place = 0 while the mouse is down (the preview: the price on the cost
// indicator, the lots in the way highlighted, the cells shown through the
// terrain overlay, a text for the number of buildings lost or for the error)
// and with place = 1 when the button is let go. Type 0 is de-zoning. When the
// pointer rests for 750 (the frame message adds the frame time) the tool
// grows a region from the cell under it (cells within 8 of it that are
// unzoned and buildable, or zoned when de-zoning) and previews that; a click
// then zones the region.
//
// Left the game's, because it builds the outline model's transform inline:
// OnMouseMove 0x004DBE40 (slot 20). It reads the picked point, moves the
// outline model of the hover cell and, while dragging, updates the drag end
// and calls UpdateHover and UpdatePreview.
//
// The PlaceZone calls go through CallPlaceZone (see tool_zone.h): the game's
// call sites are 0x004DB312 (UpdatePreview) and 0x004DA846 (Place).
#include "core/tool_zone.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_zone {
namespace {

// The zoning tool's fields, from its start (up to +0x24 the base class).
namespace field {
constexpr std::size_t kInited = 0x04;     // byte, the base class
constexpr std::size_t kCursor = 0x10;     // cursor ID, A92C40[type]
constexpr std::size_t kView = 0x1C;       // the city view (base class)
constexpr std::size_t kSaved = 0x24;      // byte: the overlay's state before the drag
constexpr std::size_t kTarget = 0x28;     // the message target part (vtable 0x00A92C94)
constexpr std::size_t kOverlay = 0x2C;    // the terrain's cell overlay (terrain slot 0x108)
constexpr std::size_t kZones = 0x30;      // the zone manager
constexpr std::size_t kLots = 0x34;       // the lot manager
constexpr std::size_t kSound = 0x38;      // the sound service
constexpr std::size_t kType = 0x3C;       // zone type, 0 dezones
constexpr std::size_t kDragging = 0x40;   // byte
constexpr std::size_t kStartX = 0x44;     // the cell the drag began on
constexpr std::size_t kStartZ = 0x48;
constexpr std::size_t kCurX = 0x4C;       // the cell under the pointer
constexpr std::size_t kCurZ = 0x50;
constexpr std::size_t kHoverTime = 0x54;  // float, the frame time the pointer has rested
constexpr std::size_t kHover = 0x58;      // the grown region (0x24 bytes) or null
constexpr std::size_t kMinX = 0x5C;       // the drag's rectangle, ordered
constexpr std::size_t kMinZ = 0x60;
constexpr std::size_t kMaxX = 0x64;
constexpr std::size_t kMaxZ = 0x68;
constexpr std::size_t kCell = 0x6C;       // float, the terrain's slot 0x18
constexpr std::size_t kText = 0x70;       // the cursor text (a game string, 0x14 bytes)
constexpr std::size_t kTextBegin = 0x74;  // its buffer (begin, end, capacity)
constexpr std::size_t kTextEnd = 0x78;
constexpr std::size_t kLotSet = 0x84;     // a set of lots: the header node (0x14 bytes)
constexpr std::size_t kLotCount = 0x88;
constexpr std::size_t kPointer = 0x90;    // the base class's model holder
constexpr std::size_t kOutline = 0x94;    // the outline model, "local_tile_outline"
} // namespace field

constexpr uint32_t kMessageFrame = 0x86AD10EE;
constexpr uint32_t kMessageState = 0x277A9364;
constexpr uint32_t kCursorText = 0xA9C87CF4;
constexpr uint32_t kTextGroup = 0x0A419226;
constexpr uint32_t kCountText = 0xCA711A5E; // "%d" buildings text
constexpr uint32_t kHoverDelay = 750;        // float 750.0 (0x00A92D20)
constexpr uint32_t kTimerInterval = 0xC0;
constexpr int32_t kHoverReach = 8;

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

// The game's string: vtable, then begin, end and capacity of its buffer.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's localized string: a string first. 0x20 bytes, as the frame of
// 0x004DB240 keeps it (0x20 bytes between the neighbouring locals).
struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// A cell region: the rectangle (x0, z0, x1, z1), then the cell map at +0x10
// (vtable, block count at +0x14 (the width), the depth at +0x18, the table of
// rows at +0x20; a row is a bit per z).
struct CellRegion {
    int32_t rect[4];
    uint32_t cells[5];
};
static_assert(sizeof(CellRegion) == 0x24);

// The original call site the tool's PlaceZone call stands for.
std::uintptr_t g_placeSite = 0;

// The one place the tool calls the zone manager's PlaceZone (slot 11) from.
// `site` is the game's own call site (tool_zone.h). The byte arguments are
// the dwords the game pushes; PlaceZone tests their low bytes.
uint8_t CallPlaceZone(std::uintptr_t site, void* zones, void* region, uint32_t type, uint32_t place,
                      uint32_t skipFunds, uint32_t customZoning, uint32_t skipSize, uint32_t wholeLots,
                      int64_t* cost, int32_t* error, void* lotSet)
{
    g_placeSite = site;
    const uint8_t result = Virtual<uint8_t>(zones, 0x2C, region, type, place, skipFunds, customZoning, skipSize,
                                            wholeLots, cost, error, lotSet);
    g_placeSite = 0;
    return result;
}

// The game's _ftol2 (0x009EEF04): truncation toward zero to 64 bits, the low
// 32 kept; what does not fit in 64 bits (a NaN among them) is the x87
// "integer indefinite", whose low word is 0.
int32_t Truncate(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

// --- Small game services -------------------------------------------------------------

// The model holder at +0x90 and +0x94 is one pointer; the base class's
// functions on it.
void ModelShow(char* holder, bool show)
{
    This<void>(addr::SC4ViewModel_Show, holder, uint32_t{show ? 1u : 0u});
}

void FreeRegion(void* region)
{
    At<std::uintptr_t>(region, 0x10) = addr::SC4CellRegion_CellsVtable;
    This<void>(addr::SC4CellRegion_DestroyCells, static_cast<char*>(region) + 0x10);
    Cdecl<void>(addr::SC4_operator_delete, region);
}

void ClearText(char* self)
{
    char* const begin = At<char*>(self, field::kTextBegin);
    if (begin != At<char*>(self, field::kTextEnd)) {
        *begin = 0;
        At<char*>(self, field::kTextEnd) = At<char*>(self, field::kTextBegin);
    }
}

// The three flags the keys and the pointer hand the display service (slot 7,
// Service43DB0): bits of the modifier word as the game masks them.
void SetModifiers(uint32_t modifiers)
{
    const uint32_t third = (modifiers >> 1) & 0xFFFFFF01u;
    const uint32_t second = (~(modifiers & 0xFFu) & 0xFFu) & 0xFFFFFF01u;
    const uint32_t first = (modifiers >> 2) & 0xFFFFFF01u;
    Virtual<void>(Global<void*>(addr::SC4_Service43DB0), 0x1C, first, second, third);
}

// Whether the string at `at` (begin, end) and `other` hold the same bytes.
bool SameText(const char* at, const char* otherBegin, const char* otherEnd)
{
    const char* const begin = *reinterpret_cast<char* const*>(at);
    const char* const end = *reinterpret_cast<char* const*>(at + 4);
    if (end - begin != otherEnd - otherBegin)
        return false;
    return std::memcmp(begin, otherBegin, static_cast<std::size_t>(end - begin)) == 0;
}

// A new empty game string (buffer of 8 from the game's allocator).
void NewString(GameString& string)
{
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    string.vtable = addr::SC4String_Vtable;
    string.begin = buffer;
    string.end = buffer;
    string.capacity = buffer + 8;
    string.zero = 0;
    *buffer = 0;
}

// --- The set of lots and the helpers on cells ------------------------------------------

// The next node of the lot set (the game's tree, in order): the code at
// 0x004DAD81 and 0x004DB36E.
char* Successor(char* node)
{
    char* result = node;
    if (char* const right = At<char*>(node, 0xC)) {
        result = right;
        for (char* left = At<char*>(result, 8); left; left = At<char*>(result, 8))
            result = left;
        return result;
    }
    char* parent = At<char*>(node, 4);
    if (node == At<char*>(parent, 0xC)) {
        char* child;
        do {
            child = parent;
            parent = At<char*>(parent, 4);
        } while (child == At<char*>(parent, 0xC));
        result = child;
    }
    if (At<char*>(result, 0xC) != parent)
        result = parent;
    return result;
}

// Whether the region has the cell (x, z) set.
bool CellSet(const void* region, int32_t x, int32_t z)
{
    const uint32_t dx = static_cast<uint32_t>(x - At<int32_t>(const_cast<void*>(region), 0));
    const uint32_t dz = static_cast<uint32_t>(z - At<int32_t>(const_cast<void*>(region), 4));
    if (dx >= At<uint32_t>(const_cast<void*>(region), 0x14) || dz >= At<uint32_t>(const_cast<void*>(region), 0x18))
        return false;
    uint32_t* const row = At<uint32_t*>(At<char*>(const_cast<void*>(region), 0x20), dx * 4);
    return (row[dz >> 5] & (1u << (dz & 31))) != 0;
}

// --- Construction and destruction ------------------------------------------------------

// The game's 0x004DAE80 (ret 4): the constructor; the caller allocated 0x98
// bytes. Reuses the entry SC4ViewTool::Construct4DAE80.
char* __fastcall Construct(char* self, void*, uint32_t type)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::cSC4ViewInputControlZone_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4ViewInputControlZone_TargetVtable;
    At<void*>(self, field::kOverlay) = nullptr;
    At<void*>(self, field::kZones) = nullptr;
    At<void*>(self, field::kLots) = nullptr;
    At<void*>(self, field::kSound) = nullptr;
    At<uint32_t>(self, field::kType) = type;
    At<uint8_t>(self, field::kDragging) = 0;
    At<uint32_t>(self, field::kStartX) = 0;
    At<uint32_t>(self, field::kStartZ) = 0;
    At<uint32_t>(self, field::kCurX) = 0;
    At<uint32_t>(self, field::kCurZ) = 0;
    At<uint32_t>(self, field::kHoverTime) = 0;
    At<void*>(self, field::kHover) = nullptr;
    At<uint32_t>(self, field::kMinX) = 0;
    At<uint32_t>(self, field::kMinZ) = 0;
    At<uint32_t>(self, field::kMaxX) = 0;
    At<uint32_t>(self, field::kMaxZ) = 0;
    At<uint32_t>(self, field::kCell) = 0;
    At<char*>(self, field::kTextBegin) = nullptr;
    At<char*>(self, field::kTextEnd) = nullptr;
    At<char*>(self, 0x7C) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, field::kTextBegin) = buffer;
    At<char*>(self, field::kTextEnd) = buffer;
    At<char*>(self, 0x7C) = buffer + 8;
    *buffer = 0;
    At<std::uintptr_t>(self, field::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, 0x80) = 0;
    At<void*>(self, field::kLotSet) = nullptr;
    At<uint32_t>(self, 0x88) = 0;
    char* const head = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x14});
    At<char*>(self, field::kLotSet) = head;
    At<uint32_t>(self, field::kLotCount) = 0;
    *head = 0;
    At<uint32_t>(head, 4) = 0;
    At<char*>(head, 8) = head;
    At<char*>(head, 0xC) = head;
    This<void*>(addr::SC4ViewModel_Construct, self + field::kPointer);
    At<void*>(self, field::kOutline) = nullptr;
    At<uint32_t>(self, 0xC) = 0xA766C101;
    At<uint32_t>(self, field::kCursor) = Global<uint32_t>(addr::cSC4ViewInputControlZone_Cursors + type * 4);
    return self;
}

// The game's 0x004DAF90: the destructor's body (the deleting one calls it).
void __fastcall Destruct(char* self, void*)
{
    if (void* const outline = At<void*>(self, field::kOutline))
        Release(outline);
    This<void>(addr::SC4RefHolder_Destruct, self + field::kPointer);
    char* const head = At<char*>(self, field::kLotSet);
    if (At<uint32_t>(self, field::kLotCount)) {
        This<void>(addr::SC4LotSet_EraseNodes, self + field::kLotSet, At<char*>(head, 4));
        At<char*>(head, 8) = head;
        At<uint32_t>(head, 4) = 0;
        At<char*>(head, 0xC) = head;
        At<uint32_t>(self, field::kLotCount) = 0;
    }
    if (char* const header = At<char*>(self, field::kLotSet))
        Cdecl<void>(addr::SC4_Free, header);
    At<std::uintptr_t>(self, field::kText) = addr::SC4String_Vtable;
    if (char* const buffer = At<char*>(self, field::kTextBegin))
        Cdecl<void>(addr::SC4_Free, buffer);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004DAF70 (slot 26, ret 4): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- Init and Shutdown -----------------------------------------------------------------

// The game's 0x004DA6C0 (slot 3): the base Init, then the services: the
// terrain (the cell size in metres and its overlay), the zone and lot
// managers, the state message, the outline model and the sound service.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, field::kInited))
        return 1;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    if (void* const terrain = Global<void*>(addr::SC4_Terrain)) {
        const float cell = Virtual<float>(terrain, 0x18);
        At<float>(self, field::kCell) = cell;
        void* const overlay = Virtual<void*>(terrain, 0x108);
        At<void*>(self, field::kOverlay) = overlay;
        if (overlay)
            AddRef(overlay);
    }
    This<void>(addr::SC4ViewModel_Make, self + field::kPointer);
    if (!At<void*>(self, field::kZones)) {
        void* const zones = Global<void*>(addr::SC4_ZoneManager);
        At<void*>(self, field::kZones) = zones;
        AddRef(zones);
    }
    if (!At<void*>(self, field::kLots)) {
        void* const lots = Global<void*>(addr::SC4_LotManager);
        At<void*>(self, field::kLots) = lots;
        AddRef(lots);
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<void>(server, 0x14, static_cast<void*>(self + field::kTarget), kMessageState); // AddNotification
    if (!At<void*>(self, field::kOutline)) {
        void* const service = Global<void*>(addr::SC4_Service43D1C);
        Virtual<void>(service, 0x1C, reinterpret_cast<const void*>(addr::cSC4ViewInputControlZone_OutlineName),
                      static_cast<void*>(self + field::kOutline)); // makes the model by name
    }
    if (void* const sound = Global<void*>(addr::SC4_Sound43CB8)) {
        At<void*>(self, field::kSound) = sound;
        AddRef(sound);
    }
    return 1;
}

// The game's 0x004DA5A0 (slot 4): Shutdown. Cancels a drag, takes back the
// state message and the services, puts the models away.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, field::kInited))
        return 1;
    Virtual<void>(self, 0x6C); // CancelDrag
    void* const view = At<void*>(self, field::kView);
    Virtual<void>(view, 0xA4, kCursorText); // ClearCursorText
    Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<void>(server, 0x18, static_cast<void*>(self + field::kTarget), kMessageState); // RemoveNotification
    for (const std::size_t offset : {field::kSound, field::kLots, field::kZones, field::kOverlay}) {
        if (void* const held = At<void*>(self, offset)) {
            Release(held);
            At<void*>(self, offset) = nullptr;
        }
    }
    This<void>(addr::SC4ViewModel_Reset, self + field::kPointer);
    if (void* const outline = At<void*>(self, field::kOutline)) {
        if (Virtual<uint8_t>(outline, 0x14))
            Virtual<void>(At<void*>(self, field::kOutline), 0x10, uint32_t{0});
        if (void* const again = At<void*>(self, field::kOutline)) {
            At<void*>(self, field::kOutline) = nullptr;
            Release(again);
        }
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004DA9B0 (slot 22, OnMouseExit): lets go of the pointer, puts
// the outline away, empties the cursor text.
uint8_t __fastcall OnMouseExit(char* self, void*)
{
    This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    if (void* const outline = At<void*>(self, field::kOutline)) {
        if (Virtual<uint8_t>(outline, 0x14))
            Virtual<void>(At<void*>(self, field::kOutline), 0x10, uint32_t{0});
    }
    ModelShow(self + field::kPointer, false);
    char* const begin = At<char*>(self, field::kTextBegin);
    if (begin != At<char*>(self, field::kTextEnd)) {
        *begin = 0;
        At<char*>(self, field::kTextEnd) = At<char*>(self, field::kTextBegin);
    }
    void* const view = At<void*>(self, field::kView);
    Virtual<void>(view, 0xA4, kCursorText);
    Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    return 1;
}

// --- Cells -------------------------------------------------------------------------------

// The game's 0x004DA680 (cdecl; out, first, last, value, and an argument it
// ignores): the first node of the list [first, last) whose cell is the
// value's (x at +8, z at +0xC); `last` if none. Stored in *out.
void __cdecl FindCellRange(char** out, char* first, char* last, const int32_t* value, void*)
{
    char* node = first;
    if (node != last) {
        while (!(At<int32_t>(node, 8) == value[0] && At<int32_t>(node, 0xC) == value[1])) {
            node = At<char*>(node, 0);
            if (node == last)
                break;
        }
    }
    *out = node;
}

// The game's 0x004DA850 (cdecl; out, first, last, value): the same, giving
// back `out`.
char** __cdecl FindCell(char** out, char* first, char* last, const int32_t* value)
{
    FindCellRange(out, first, last, value, nullptr);
    return out;
}

// The game's 0x004DA880 (ret 0x10): whether the cell may join the list grown
// from `origin`: de-zoning takes zoned cells that are not already in the list
// and less than `reach` from the origin on both axes (zone types 0, 0xB, 0xC
// and 0xF are not taken); zoning takes unzoned cells of the same kind that
// the lot manager answers 8 to (slot 20).
uint8_t __fastcall CanAddCell(char* self, void*, const int32_t* cell, const int32_t* origin, int32_t reach,
                              char** list)
{
    uint8_t result = 0;
    const int32_t zone = Virtual<int32_t>(At<void*>(self, field::kZones), 0x30, cell[0], cell[1]); // GetZoneType
    const auto distance = [](int32_t a, int32_t b) {
        const int32_t difference = a - b;
        return difference < 0 ? -difference : difference;
    };
    if (At<uint32_t>(self, field::kType) == 0) {
        if (zone == 0 || zone == 0xF || zone == 0xB || zone == 0xC)
            return 0;
        char* const head = *list;
        char* found;
        FindCell(&found, At<char*>(head, 0), head, cell);
        if (found != head)
            return 0;
        if (distance(origin[0], cell[0]) >= reach || distance(origin[1], cell[1]) >= reach)
            return 0;
        return 1;
    }
    if (zone != 0)
        return 0;
    char* const head = *list;
    char* found;
    FindCell(&found, At<char*>(head, 0), head, cell);
    if (found != head)
        return 0;
    if (distance(origin[0], cell[0]) >= reach || distance(origin[1], cell[1]) >= reach)
        return 0;
    if (Virtual<int32_t>(At<void*>(self, field::kLots), 0x50, cell[0], cell[1], uint32_t{1}, uint32_t{1},
                         uint32_t{1}, uint32_t{0}, uint32_t{0}) == 8)
        result = 1;
    return result;
}

// A list node: {next, previous, x, z}, 0x10 bytes from the game's allocator.
void Append(char* head, int32_t x, int32_t z)
{
    char* const node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x10});
    At<int32_t>(node, 8) = x;
    At<int32_t>(node, 0xC) = z;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
}

// The game's 0x004DAA20 (ret 8; x, z): grows a list of cells from (x, z)
// with CanAddCell (both neighbours on each axis, in the order z - 1, z + 1,
// x - 1, x + 1) and makes a cell region of their bounding rectangle with the
// cells set. Null if no cell qualified. The region is the caller's to free.
char* __fastcall BuildHoverRegion(char* self, void*, int32_t x, int32_t z)
{
    void* const city = Global<void*>(addr::SC4_City);
    const int32_t maxX = Virtual<int32_t>(city, 0x174) - 1;
    const int32_t maxZ = Virtual<int32_t>(city, 0x178) - 1;
    char* const head = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x10});
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    int32_t seed[2] = {x, z};
    char* first = head;
    if (CanAddCell(self, nullptr, seed, seed, kHoverReach, &first))
        Append(head, seed[0], seed[1]);
    int32_t minX = seed[0], minZ = seed[1], highX = seed[0], highZ = seed[1];
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        const int32_t cx = At<int32_t>(node, 8);
        const int32_t cz = At<int32_t>(node, 0xC);
        if (cx < minX)
            minX = cx;
        if (cx > highX)
            highX = cx;
        if (cz < minZ)
            minZ = cz;
        if (cz > highZ)
            highZ = cz;
        const auto probe = [&](int32_t nx, int32_t nz) {
            int32_t cell[2] = {nx, nz};
            char* list = head;
            if (CanAddCell(self, nullptr, cell, seed, kHoverReach, &list))
                Append(head, nx, nz);
        };
        if (cz > 0)
            probe(cx, cz - 1);
        if (cz < maxZ)
            probe(cx, cz + 1);
        if (cx > 0)
            probe(cx - 1, cz);
        if (cx < maxX)
            probe(cx + 1, cz);
    }
    if (At<char*>(head, 0) == head) {
        At<char*>(head, 0) = head;
        At<char*>(head, 4) = head;
        Cdecl<void>(addr::SC4_Free, head);
        return nullptr;
    }
    char* const region = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x24});
    if (region) {
        At<int32_t>(region, 0) = minX;
        At<int32_t>(region, 4) = minZ;
        At<int32_t>(region, 8) = highX;
        At<int32_t>(region, 0xC) = highZ;
        uint8_t fill = 0;
        This<void*>(addr::SC4CellRegion_ConstructCells, region + 0x10, static_cast<uint32_t>(highX - minX + 1),
                    static_cast<uint32_t>(highZ - minZ + 1), &fill);
    }
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        const int32_t dx = At<int32_t>(node, 8) - At<int32_t>(region, 0);
        const int32_t dz = At<int32_t>(node, 0xC) - At<int32_t>(region, 4);
        uint32_t* const row = At<uint32_t*>(At<char*>(region, 0x20), static_cast<std::size_t>(dx) * 4);
        row[static_cast<uint32_t>(dz) >> 5] |= 1u << (static_cast<uint32_t>(dz) & 31);
    }
    char* node = At<char*>(head, 0);
    while (node != head) {
        char* const gone = node;
        node = At<char*>(node, 0);
        Cdecl<void>(addr::SC4_Free, gone);
    }
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    Cdecl<void>(addr::SC4_Free, head);
    return region;
}

// --- Preview -----------------------------------------------------------------------------

// The game's 0x004DAD40: takes the highlight off every lot of the set, empties
// the set, and shows an empty region of the tool's type to the display
// service (slot 5 of Service43DB0, 4 arguments).
void __fastcall ClearPreview(char* self, void*)
{
    char* const head = At<char*>(self, field::kLotSet);
    for (char* node = At<char*>(head, 8); node != head; node = Successor(node)) {
        void* const lot = At<void*>(node, 0x10);
        if (void* const highlight = Virtual<void*>(lot, 0xC0)) {
            void* const target = Virtual<void*>(highlight, 0xC);
            Virtual<void>(target, 0x44, uint32_t{0}, uint32_t{0});
        }
    }
    if (At<uint32_t>(self, field::kLotCount)) {
        This<void>(addr::SC4LotSet_EraseNodes, self + field::kLotSet, At<char*>(head, 4));
        At<char*>(head, 8) = head;
        At<uint32_t>(head, 4) = 0;
        At<char*>(head, 0xC) = head;
        At<uint32_t>(self, field::kLotCount) = 0;
    }
    uint8_t fill = 0;
    CellRegion empty = {};
    This<void*>(addr::SC4CellRegion_ConstructCells, empty.cells, uint32_t{1}, uint32_t{1}, &fill);
    Virtual<void>(Global<void*>(addr::SC4_Service43DB0), 0x14, static_cast<void*>(&empty),
                  At<uint32_t>(self, field::kType), static_cast<void*>(nullptr), static_cast<void*>(nullptr));
    empty.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    This<void>(addr::SC4CellRegion_DestroyCells, empty.cells);
}

// The game's 0x004DB0D0 (ret 8; the tool is not used): the cursor text for an
// error code from PlaceZone. 0 empties the string; 1 funds, 2 landfill on
// water, 3 too small, 4 too large, 5 landfill blocked take their text from
// the group 0x0A419226.
void __fastcall SetTipText(char*, void*, uint32_t error, GameString* out)
{
    uint32_t instance;
    switch (error) {
    case 0:
        if (out->begin != out->end) {
            *out->begin = 0;
            out->end = out->begin;
        }
        return;
    case 1:
        instance = 0x6A711976;
        break;
    case 2:
        instance = 0x6A711A47;
        break;
    case 3:
        instance = 0xAA711A4E;
        break;
    case 4:
        instance = 0x8A711A54;
        break;
    case 5:
        instance = 0x6A711A59;
        break;
    default:
        return;
    }
    LocalizedString text;
    void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, instance, uint32_t{1});
    This<void>(addr::cRZBaseString_Assign, out, made);
    text.text.vtable = addr::SC4String_Vtable;
    if (text.text.begin)
        Cdecl<void>(addr::SC4_Free, text.text.begin);
}

// The game's 0x004DB240: the preview. Orders the drag's rectangle, clears the
// last preview, asks PlaceZone what the rectangle (or the grown region) would
// do (place = 0), highlights the lots it would demolish, shows the cells and
// the price, and sets the cursor text. Returns whether PlaceZone accepted.
uint8_t __fastcall UpdatePreview(char* self, void*)
{
    {
        const int32_t startX = At<int32_t>(self, field::kStartX), curX = At<int32_t>(self, field::kCurX);
        if (startX < curX) {
            At<int32_t>(self, field::kMinX) = startX;
            At<int32_t>(self, field::kMaxX) = curX;
        } else {
            At<int32_t>(self, field::kMinX) = curX;
            At<int32_t>(self, field::kMaxX) = startX;
        }
        const int32_t startZ = At<int32_t>(self, field::kStartZ), curZ = At<int32_t>(self, field::kCurZ);
        if (startZ < curZ) {
            At<int32_t>(self, field::kMinZ) = startZ;
            At<int32_t>(self, field::kMaxZ) = curZ;
        } else {
            At<int32_t>(self, field::kMinZ) = curZ;
            At<int32_t>(self, field::kMaxZ) = startZ;
        }
    }
    ClearPreview(self, nullptr);
    const int32_t minX = At<int32_t>(self, field::kMinX), minZ = At<int32_t>(self, field::kMinZ);
    const int32_t maxX = At<int32_t>(self, field::kMaxX), maxZ = At<int32_t>(self, field::kMaxZ);
    CellRegion local = {};
    uint8_t fill = 1;
    local.rect[0] = minX;
    local.rect[1] = minZ;
    local.rect[2] = maxX;
    local.rect[3] = maxZ;
    This<void*>(addr::SC4CellRegion_ConstructCells, local.cells, static_cast<uint32_t>(maxX - minX + 1),
                static_cast<uint32_t>(maxZ - minZ + 1), &fill);
    void* const hover = At<void*>(self, field::kHover);
    void* const used = hover ? hover : static_cast<void*>(&local);
    void* const zones = At<void*>(self, field::kZones);
    const uint32_t type = At<uint32_t>(self, field::kType);
    int64_t cost = 0;
    int32_t error = 0;
    // The game pushes the dword holding the flag (upper bytes unset); PlaceZone
    // tests the low byte.
    const uint8_t ok = CallPlaceZone(kSiteUpdatePreview, zones, used, type, 0, 0, 1, hover ? 1u : 0u,
                                     type != 0 ? 1u : 0u, &cost, &error, self + field::kLotSet);
    uint8_t other = 0;
    uint32_t count = 0;
    {
        char* const head = At<char*>(self, field::kLotSet);
        for (char* node = At<char*>(head, 8); node != head; node = Successor(node)) {
            void* const lot = At<void*>(node, 0x10);
            if (void* const highlight = Virtual<void*>(lot, 0xC0)) {
                void* const target = Virtual<void*>(highlight, 0xC);
                Virtual<void>(target, 0x44, uint32_t{5}, uint32_t{0});
                ++count;
            } else if (Virtual<void*>(lot, 0xB8, uint32_t{0})) {
                other = 1;
            }
        }
        if (other && count == 0)
            count = 1;
    }
    void* const view = At<void*>(self, field::kView);
    void* const sound = At<void*>(self, field::kSound);
    GameString message;
    if (ok) {
        uint32_t key[3] = {0, 0, 0};
        // The byte of the colour (the game keeps only its low byte; the three
        // above it are unset stack).
        const uint32_t colour = static_cast<uint8_t>(Virtual<uint32_t>(zones, 0x3C, uint32_t{0}));
        if (sound) {
            if (const uint32_t cue = Global<uint32_t>(addr::cSC4ViewInputControlZone_PreviewCues + type * 4))
                Virtual<void>(sound, 0x2C, uint32_t{1}, cue, uint32_t{0}, uint32_t{0}, uint32_t{0});
        }
        Virtual<void>(zones, 0x38, type, key); // GetTextureForZone
        void* const overlay = At<void*>(self, field::kOverlay);
        Virtual<void>(overlay, 0x24);
        Virtual<void>(view, 0xAC, static_cast<uint32_t>(cost)); // SetCostIndicator
        NewString(message);
        if (count) {
            LocalizedString format;
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &format, kTextGroup, kCountText, uint32_t{1});
            const char* const text = This<const char*>(addr::cRZBaseString_Data, &format);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &message, text, count);
            if (!SameText(self + field::kTextBegin, message.begin, message.end)) {
                uint8_t tag = other;
                This<void>(addr::SC4String_AssignRange, self + field::kTextBegin, message.begin, message.end, &tag);
                Virtual<void>(At<void*>(self, field::kView), 0xA0, kCursorText, uint32_t{0}, static_cast<void*>(nullptr),
                              static_cast<void*>(&message), int32_t{1}); // SetCursorText
            }
            format.text.vtable = addr::SC4String_Vtable;
            if (format.text.begin)
                Cdecl<void>(addr::SC4_Free, format.text.begin);
        } else {
            ClearText(self);
            Virtual<void>(At<void*>(self, field::kView), 0xA4, kCursorText); // ClearCursorText
        }
        const auto showCell = [&](int32_t x, int32_t z) {
            int32_t cell[2] = {x, z};
            Virtual<void>(At<void*>(self, field::kOverlay), 0x14, cell, key, uint32_t{0}, uint32_t{0}, uint32_t{4},
                          colour, uint32_t{0});
        };
        if (type == 0) {
            if (At<void*>(self, field::kHover)) {
                for (int32_t z = At<int32_t>(At<void*>(self, field::kHover), 4);
                     z <= At<int32_t>(At<void*>(self, field::kHover), 0xC); ++z)
                    for (int32_t x = At<int32_t>(At<void*>(self, field::kHover), 0);
                         x <= At<int32_t>(At<void*>(self, field::kHover), 8); ++x)
                        if (CellSet(At<void*>(self, field::kHover), x, z))
                            showCell(x, z);
            } else {
                for (int32_t z = At<int32_t>(self, field::kMinZ); z <= At<int32_t>(self, field::kMaxZ); ++z)
                    for (int32_t x = At<int32_t>(self, field::kMinX); x <= At<int32_t>(self, field::kMaxX); ++x)
                        showCell(x, z);
            }
        } else if (type == 0xE) {
            for (int32_t z = At<int32_t>(self, field::kMinZ); z <= At<int32_t>(self, field::kMaxZ); ++z)
                for (int32_t x = At<int32_t>(self, field::kMinX); x <= At<int32_t>(self, field::kMaxX); ++x)
                    if (CellSet(used, x, z))
                        showCell(x, z);
        } else {
            Virtual<void>(Global<void*>(addr::SC4_Service43DB0), 0x14, used, type, static_cast<void*>(nullptr),
                          static_cast<void*>(nullptr));
        }
        message.vtable = addr::SC4String_Vtable;
    } else {
        if (sound) {
            if (const uint32_t cue = Global<uint32_t>(addr::cSC4ViewInputControlZone_FailCues + type * 4))
                Virtual<void>(sound, 0x2C, uint32_t{1}, cue, uint32_t{0}, uint32_t{0}, uint32_t{0});
        }
        Virtual<void>(At<void*>(self, field::kOverlay), 0x1C, static_cast<void*>(self + field::kMinX), uint32_t{0},
                      uint32_t{1}); // the rectangle, outlined
        NewString(message);
        SetTipText(nullptr, nullptr, static_cast<uint32_t>(error), &message);
        if (!SameText(self + field::kTextBegin, message.begin, message.end)) {
            uint8_t tag = other;
            This<void>(addr::SC4String_AssignRange, self + field::kTextBegin, message.begin, message.end, &tag);
            Virtual<void>(At<void*>(self, field::kView), 0xA0, kCursorText, uint32_t{0}, static_cast<void*>(nullptr),
                          static_cast<void*>(self + field::kText), int32_t{1});
        }
        if (type == 0 && error == 1)
            Virtual<void>(At<void*>(self, field::kView), 0xA8, static_cast<uint32_t>(cost), uint32_t{0});
        else
            Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    }
    if (message.begin)
        Cdecl<void>(addr::SC4_Free, message.begin);
    local.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    This<void>(addr::SC4CellRegion_DestroyCells, local.cells);
    return ok;
}

// The game's 0x004DBD40: the hover tick. While the pointer has not left the
// cell the drag began on and has rested for 750, grows the region
// (BuildHoverRegion), shows it to the display service, which may answer with
// another one, and previews it. When the pointer has moved the region is
// dropped and the timer reset.
void __fastcall UpdateHover(char* self, void*)
{
    if (At<int32_t>(self, field::kCurX) == At<int32_t>(self, field::kStartX) &&
        At<int32_t>(self, field::kCurZ) == At<int32_t>(self, field::kStartZ)) {
        if (!(static_cast<double>(At<float>(self, field::kHoverTime)) > static_cast<double>(kHoverDelay)) ||
            At<void*>(self, field::kHover))
            return;
        char* const region = BuildHoverRegion(self, nullptr, At<int32_t>(self, field::kStartX),
                                              At<int32_t>(self, field::kStartZ));
        At<void*>(self, field::kHover) = region;
        if (region && At<uint32_t>(self, field::kType)) {
            int32_t cell[2] = {At<int32_t>(self, field::kStartX), At<int32_t>(self, field::kStartZ)};
            void* answer = nullptr;
            Virtual<void>(Global<void*>(addr::SC4_Service43DB0), 0x14, static_cast<void*>(region),
                          At<uint32_t>(self, field::kType), static_cast<void*>(cell), static_cast<void*>(&answer));
            if (void* const old = At<void*>(self, field::kHover))
                FreeRegion(old);
            At<void*>(self, field::kHover) = answer;
        }
        UpdatePreview(self, nullptr);
        return;
    }
    void* const old = At<void*>(self, field::kHover);
    At<uint32_t>(self, field::kHoverTime) = 0;
    if (old) {
        FreeRegion(old);
        At<void*>(self, field::kHover) = nullptr;
    }
}

// --- Placing --------------------------------------------------------------------------

// The game's 0x004DA7A0 (ret 0x18; type, region, fromHover, skipSize, cost,
// error): the placing, place = 1. When the city's service 0x00B43D38 is
// there and answers slot 8 and slot 10 with a rectangle whose first number
// is not negative, a request is queued instead: with fromHover set, or when
// the rectangle differs from the region's, the service's slot 18 is told 1
// and nothing is placed (returns 0). Without the service, and with fromHover
// set, the region's rectangle becomes the drag's start and end. Returns what
// PlaceZone said.
uint8_t __fastcall Place(char* self, void*, uint32_t type, int32_t* region, uint32_t fromHover, uint32_t skipSize,
                         int64_t* cost, int32_t* error)
{
    void* const service = Global<void*>(addr::SC4_Service43D38);
    bool skip = false;
    if (service && Virtual<uint8_t>(service, 0x20)) {
        if (void* const queue = Virtual<void*>(service, 0x28)) {
            const int32_t* const rect = Virtual<const int32_t*>(queue, 0x14);
            if (*rect >= 0) {
                if (static_cast<uint8_t>(fromHover) ||
                    Cdecl<uint8_t>(addr::SC4Rect_Differs, rect, static_cast<const int32_t*>(region))) {
                    Virtual<void>(service, 0x48, uint32_t{1});
                    return 0;
                }
                skip = true;
            }
        }
    }
    if (!skip && static_cast<uint8_t>(fromHover)) {
        At<int32_t>(self, field::kStartX) = region[0];
        At<int32_t>(self, field::kStartZ) = region[1];
        At<int32_t>(self, field::kCurX) = region[2];
        At<int32_t>(self, field::kCurZ) = region[3];
    }
    return CallPlaceZone(kSitePlace, At<void*>(self, field::kZones), region, type, 1, 0, 1, skipSize,
                         At<uint32_t>(self, field::kType) != 0 ? 1u : 0u, cost, error, nullptr);
}

// --- Dragging ---------------------------------------------------------------------------

// The game's 0x004DB020 (slot 27): gives up a drag. Lets go of the pointer,
// drops the grown region, clears the cursor text and the preview and shows the
// pointer model again.
void __fastcall CancelDrag(char* self, void*)
{
    if (At<uint8_t>(self, field::kDragging)) {
        At<uint8_t>(self, field::kDragging) = 0;
        This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
        void* const timers = Cdecl<void*>(addr::SC4_GetService86AD10EC);
        Virtual<void>(timers, 0x18, static_cast<void*>(self + field::kTarget), int32_t{-1});
    }
    if (void* const hover = At<void*>(self, field::kHover)) {
        FreeRegion(hover);
        At<void*>(self, field::kHover) = nullptr;
    }
    Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    if (At<char*>(self, field::kTextBegin) != At<char*>(self, field::kTextEnd)) {
        Virtual<void>(At<void*>(self, field::kView), 0xA4, kCursorText);
        ClearText(self);
    }
    ClearPreview(self, nullptr);
    ModelShow(self + field::kPointer, false);
    if (void* const overlay = At<void*>(self, field::kOverlay))
        Virtual<void>(overlay, 0x24);
}

// The game's 0x004DB8B0 (slot 14, OnKeyDown; key, modifiers): Escape ends a
// drag; any other key during a drag passes the modifier flags on and
// previews again. Only for the view's current control. Never takes the key.
uint8_t __fastcall OnKeyDown(char* self, void*, uint32_t key, uint32_t modifiers)
{
    if (!This<uint8_t>(addr::SC4ViewInputControl_IsCurrent, self))
        return 0;
    if (key == 0x1B) {
        if (!At<uint8_t>(self, field::kDragging))
            return 0;
        Virtual<void>(self, 0x6C); // CancelDrag
        return 1;
    }
    if (!At<uint8_t>(self, field::kDragging))
        return 0;
    SetModifiers(modifiers);
    UpdatePreview(self, nullptr);
    return 0;
}

// The game's 0x004DB920 (slot 15, OnKeyUp).
uint8_t __fastcall OnKeyUp(char* self, void*, uint32_t, uint32_t modifiers)
{
    if (!This<uint8_t>(addr::SC4ViewInputControl_IsCurrent, self))
        return 0;
    if (!At<uint8_t>(self, field::kDragging))
        return 0;
    SetModifiers(modifiers);
    UpdatePreview(self, nullptr);
    return 0;
}

// The game's 0x004DB980 (slot 16, OnMouseDownL; x, z, modifiers): picks the
// terrain under the pointer; on a hit begins a drag there: the cell is the
// point over the cell size, truncated, the pointer is captured, the overlay's
// state is saved and the frame timer registered. Returns 1 on a miss as well,
// 0 without a view.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    SetModifiers(modifiers);
    float point[3];
    std::memcpy(&point[0], &Global<float>(addr::SC4ViewTool_UnsetPoint), sizeof(float));
    point[1] = point[0];
    point[2] = point[0];
    void* const view = At<void*>(self, field::kView);
    if (!view)
        return 0;
    if (void* const outline = At<void*>(self, field::kOutline)) {
        if (Virtual<uint8_t>(outline, 0x14))
            Virtual<void>(At<void*>(self, field::kOutline), 0x10, uint32_t{0});
    }
    if (!Virtual<uint8_t>(At<void*>(self, field::kView), 0x90, x, z, point, uint32_t{0}))
        return 1;
    if (void* const sound = At<void*>(self, field::kSound)) {
        if (const uint32_t cue = Global<uint32_t>(addr::cSC4ViewInputControlZone_DownCues + At<uint32_t>(self, field::kType) * 4))
            Virtual<void>(sound, 0x2C, uint32_t{1}, cue, uint32_t{0}, uint32_t{0}, uint32_t{0});
    }
    At<uint8_t>(self, field::kDragging) = 1;
    Virtual<void>(At<void*>(self, field::kView), 0x8C); // KillKeyboardScrolling
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
    const double cell = At<float>(self, field::kCell);
    At<int32_t>(self, field::kStartX) = Truncate(static_cast<double>(point[0]) / cell);
    At<int32_t>(self, field::kStartZ) = Truncate(static_cast<double>(point[2]) / cell);
    At<int32_t>(self, field::kCurX) = At<int32_t>(self, field::kStartX);
    At<int32_t>(self, field::kCurZ) = At<int32_t>(self, field::kStartZ);
    At<uint8_t>(self, field::kSaved) = Virtual<uint8_t>(At<void*>(self, field::kOverlay), 0x28, uint32_t{0});
    UpdatePreview(self, nullptr);
    void* const timers = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<void>(timers, 0x14, static_cast<void*>(self + field::kTarget), kTimerInterval);
    At<uint32_t>(self, field::kHoverTime) = 0;
    if (void* const hover = At<void*>(self, field::kHover)) {
        FreeRegion(hover);
        At<void*>(self, field::kHover) = nullptr;
    }
    ModelShow(self + field::kPointer, false);
    return 1;
}

// The game's 0x004DBAF0 (slot 18, OnMouseUpL; x, z, modifiers): ends a drag.
// The drag's end is picked again; the preview decides: accepted, the tool
// plays its cue and places (the grown region if there is one, else the
// rectangle), the cost indicator shows whether it was done; refused, the
// error cue. The cursor text and preview are cleared, the overlay and the
// models put back, the timer and the pointer let go.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    SetModifiers(modifiers);
    if (!At<uint8_t>(self, field::kDragging))
        return 0;
    float point[3];
    std::memcpy(&point[0], &Global<float>(addr::SC4ViewTool_UnsetPoint), sizeof(float));
    point[1] = point[0];
    point[2] = point[0];
    if (Virtual<uint8_t>(At<void*>(self, field::kView), 0x90, x, z, point, uint32_t{0})) {
        const double cell = At<float>(self, field::kCell);
        At<int32_t>(self, field::kCurX) = Truncate(static_cast<double>(point[0]) / cell);
        At<int32_t>(self, field::kCurZ) = Truncate(static_cast<double>(point[2]) / cell);
    }
    const uint8_t accepted = UpdatePreview(self, nullptr);
    void* const sound = At<void*>(self, field::kSound);
    const uint32_t type = At<uint32_t>(self, field::kType);
    if (accepted) {
        if (sound) {
            if (const uint32_t cue = Global<uint32_t>(addr::cSC4ViewInputControlZone_UpCues + type * 4))
                Virtual<void>(sound, 0x2C, uint32_t{1}, cue, uint32_t{0}, uint32_t{0}, uint32_t{0});
        }
        if (void* const hover = At<void*>(self, field::kHover)) {
            const uint8_t done = Place(self, nullptr, type, static_cast<int32_t*>(hover), 1, 1, nullptr, nullptr);
            Virtual<void>(At<void*>(self, field::kView), 0xB0, static_cast<uint32_t>(done));
        } else {
            CellRegion region = {};
            int32_t rect[4] = {At<int32_t>(self, field::kMinX), At<int32_t>(self, field::kMinZ),
                               At<int32_t>(self, field::kMaxX), At<int32_t>(self, field::kMaxZ)};
            This<void*>(addr::SC4CellRegion_Construct, &region, static_cast<const int32_t*>(rect), uint32_t{1});
            const uint8_t done = Place(self, nullptr, type, region.rect, 0, 0, nullptr, nullptr);
            region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
            This<void>(addr::SC4CellRegion_DestroyCells, region.cells);
            Virtual<void>(At<void*>(self, field::kView), 0xB0, static_cast<uint32_t>(done));
        }
    } else if (sound) {
        if (const uint32_t cue = Global<uint32_t>(addr::cSC4ViewInputControlZone_FailCues + type * 4))
            Virtual<void>(sound, 0x2C, uint32_t{1}, cue, uint32_t{0}, uint32_t{0}, uint32_t{0});
    }
    Virtual<void>(At<void*>(self, field::kView), 0xA4, kCursorText);
    ClearText(self);
    ClearPreview(self, nullptr);
    if (void* const outline = At<void*>(self, field::kOutline)) {
        if (!Virtual<uint8_t>(outline, 0x14))
            Virtual<void>(At<void*>(self, field::kOutline), 0xC, uint32_t{0});
    }
    void* const overlay = At<void*>(self, field::kOverlay);
    ModelShow(self + field::kPointer, Virtual<uint8_t>(overlay, 0x50) == 0);
    Virtual<void>(At<void*>(self, field::kOverlay), 0x24);
    Virtual<void>(At<void*>(self, field::kOverlay), 0x28, uint32_t{At<uint8_t>(self, field::kSaved)});
    void* const timers = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<void>(timers, 0x18, static_cast<void*>(self + field::kTarget), kTimerInterval);
    if (void* const hover = At<void*>(self, field::kHover)) {
        FreeRegion(hover);
        At<void*>(self, field::kHover) = nullptr;
    }
    This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    At<uint8_t>(self, field::kDragging) = 0;
    return 1;
}

// The game's 0x004DC110 (slot 3 of the message target at +0x28; ecx = this +
// 0x28, ret 4): the state message 0x277A9364 shows the pointer model when the
// overlay answers slot 20 with zero and no drag is on, hides it otherwise;
// the frame message 0x86AD10EE adds the frame time (a float the message's
// slot 19 gives) to the hover timer and runs UpdateHover. Nothing before the
// tool is inited; always true after.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - field::kTarget;
    if (!At<uint8_t>(self, field::kInited))
        return 0;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kMessageState) {
        const bool busy = Virtual<uint8_t>(At<void*>(self, field::kOverlay), 0x50) != 0;
        ModelShow(self + field::kPointer, !busy && !At<uint8_t>(self, field::kDragging));
        return 1;
    }
    if (type == kMessageFrame) {
        const uint32_t bits = Virtual<uint32_t>(message, 0x4C);
        float step;
        std::memcpy(&step, &bits, sizeof(step));
        At<float>(self, field::kHoverTime) =
            static_cast<float>(static_cast<double>(step) + static_cast<double>(At<float>(self, field::kHoverTime)));
        UpdateHover(self, nullptr);
    }
    return 1;
}

// --- The cheat tool (vtable 0x00A92AC0), the small functions ----------------------------

// The game's 0x004D9E50 (slot 3): the IDs, the base Init, and the byte at
// +0x30 set to 1 (the mode).
uint8_t __fastcall CheatInit(char* self, void*)
{
    if (!At<uint8_t>(self, field::kInited)) {
        At<uint32_t>(self, 0xC) = 0x8BFC65A7;
        At<uint32_t>(self, field::kCursor) = 0x88E70B76;
        This<uint8_t>(addr::SC4ViewInputControl_Init, self);
        At<uint32_t>(self, 0x30) = 1;
    }
    return 1;
}

// The game's 0x004D9E80: the constructor (0x38 bytes from the caller).
char* __fastcall CheatConstruct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4CheatTool_Vtable;
    At<void*>(self, 0x28) = nullptr;
    At<void*>(self, 0x2C) = nullptr;
    At<uint32_t>(self, 0x34) = 0;
    return self;
}

// The game's 0x004D9EA0 (slot 4): Shutdown. Hides and lets go of the model
// at +0x2C, then the base.
uint8_t __fastcall CheatShutdown(char* self, void*)
{
    if (At<uint8_t>(self, field::kInited)) {
        if (void* const model = At<void*>(self, 0x2C)) {
            Virtual<void>(model, 0x10, uint32_t{0});
            if (void* const again = At<void*>(self, 0x2C)) {
                At<void*>(self, 0x2C) = nullptr;
                Release(again);
            }
        }
        This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    }
    return 1;
}

// The game's 0x004DA180: the destructor's body (0x004D7AA0, the deleting
// destructor, calls it): lets go of the models at +0x2C and +0x28.
void __fastcall CheatDestruct(char* self, void*)
{
    if (void* const model = At<void*>(self, 0x2C))
        Release(model);
    if (void* const other = At<void*>(self, 0x28))
        Release(other);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// --- Registration -------------------------------------------------------------------------

bool InstallZoneTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4ViewTool_Construct4DAE80, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4ViewInputControlZone_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4ViewInputControlZone_FindCellRange, reinterpret_cast<void*>(&FindCellRange)},
        {addr::cSC4ViewInputControlZone_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4ViewInputControlZone_Place, reinterpret_cast<void*>(&Place)},
        {addr::cSC4ViewInputControlZone_FindCell, reinterpret_cast<void*>(&FindCell)},
        {addr::cSC4ViewInputControlZone_CanAddCell, reinterpret_cast<void*>(&CanAddCell)},
        {addr::cSC4ViewInputControlZone_OnMouseExit, reinterpret_cast<void*>(&OnMouseExit)},
        {addr::cSC4ViewInputControlZone_BuildHoverRegion, reinterpret_cast<void*>(&BuildHoverRegion)},
        {addr::cSC4ViewInputControlZone_ClearPreview, reinterpret_cast<void*>(&ClearPreview)},
        {addr::cSC4ViewInputControlZone_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::cSC4ViewInputControlZone_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4ViewInputControlZone_CancelDrag, reinterpret_cast<void*>(&CancelDrag)},
        {addr::cSC4ViewInputControlZone_SetTipText, reinterpret_cast<void*>(&SetTipText)},
        {addr::cSC4ViewInputControlZone_UpdatePreview, reinterpret_cast<void*>(&UpdatePreview)},
        {addr::cSC4ViewInputControlZone_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::cSC4ViewInputControlZone_OnKeyUp, reinterpret_cast<void*>(&OnKeyUp)},
        {addr::cSC4ViewInputControlZone_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::cSC4ViewInputControlZone_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::cSC4ViewInputControlZone_UpdateHover, reinterpret_cast<void*>(&UpdateHover)},
        {addr::cSC4ViewInputControlZone_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4CheatTool_Init, reinterpret_cast<void*>(&CheatInit)},
        {addr::SC4ViewInputControls_F4D9E80, reinterpret_cast<void*>(&CheatConstruct)},
        {addr::SC4CheatTool_Shutdown, reinterpret_cast<void*>(&CheatShutdown)},
        {addr::SC4CheatTool_Destruct, reinterpret_cast<void*>(&CheatDestruct)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.zone",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ViewInputControlZone_UpdatePreview,
    "Our zoning tool and the first functions of the cheat tool before it (docs/ui.md)",
    false,
    InstallZoneTool,
});

} // namespace

std::uintptr_t PlaceZoneSite()
{
    return g_placeSite;
}

} // namespace opensc4::tool_zone
