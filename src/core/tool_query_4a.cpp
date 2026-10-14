// The query tool of the city view, fourth piece, first half: the routes,
// the highlight, the window of kind 1 and three of the tool's slots,
// 0x004CA460 to 0x004CC112.
// Replacement, off by default:
//
//   ui.tools.query4a: 11 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): the builder of the route lines
//     (0x004CA460) and the four functions that call it for a building's lot
//     or a network piece, the highlight of a lot's occupants, the erase of
//     a subtree of the text map, the message handler of the window of kind
//     1 (slot 3 of the panel target, vtable 0x00A90980), and the tool's
//     Shutdown (slot 4), OnMouseMove (slot 20) and OnMouseExit (slot 22).
//
// The route builder is logic that hands each route to the drawing: which
// lots and neighbour connections, which of their trips, which kinds of
// route (the window's check boxes) and where the line is pinned are
// decided here; the line object, its set-up and the points it gets stay
// the game's (0x007DDD50, 0x007DCD90, 0x007DDC30, 0x0053C940 and the route
// line 0x004C5E20, called by address; the render service takes the line by
// its slots 32 and 33).
//
// Nothing is left the game's in this half. No random number is drawn.
// Time: OnMouseMove stops and starts the tool's timer at +0x50 (a
// cRZTimer, real time: +0x90 milliseconds, 50 from the view) unless the
// left button is down (+0x89); Shutdown and OnMouseExit stop it. Nothing
// here reads a clock. The lot manager's slot 15 (an iterator over the lots,
// 8 bytes from the game's heap, registered with the manager) is never
// given back by the route builder: kept, as in the game.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_4a {
namespace {

namespace base = tool_query::base;
namespace field = tool_query::field;
namespace panel_target = tool_query::panel_target;
namespace trip_node = tool_query::trip_node;
namespace text_node = tool_query::text_node;

// Occupant types.
constexpr uint32_t kTypeBuilding = 0x278128A0;
constexpr uint32_t kTypeNetwork = 0xC772BF98;

constexpr uint32_t kNetworkOccupant = 0xA821EF94; // the network occupant's interface
constexpr uint32_t kProcedureIid = 0x5386D516;    // what a window is asked for to get the tool's target
constexpr uint32_t kCursorText = 0x2A456CB1;      // the tool's text at the pointer
constexpr uint32_t kRedirectProperty = 0x6A95E503; // a bool: the lot's building stands for the occupant
constexpr uint32_t kLotGroup = 0x1003;            // an occupant group: all of the lot's routes
constexpr uint32_t kBuildingGroup = 0x1006;       // an occupant group kind 0 takes the lot's building for
constexpr uint32_t kHighlight = 7;                // what the occupant under the pointer is highlighted with
constexpr uint32_t kSubway = 7;                   // the network type the route mode 2 is for

// The messages Init registered the tool's target part for.
constexpr uint32_t kMessages[] = {0x0A456D95, 0x0A456D96, 0x26D31EC2, 0xA6B79602, 0x66956814, 0x69247DC7};

// The window of kind 1 (0x0BB0F5E7) and its controls.
constexpr uint32_t kWindowMessage = 0xA2BF8AD5; // a window's message (first dword) that sets the radio pair
constexpr uint32_t kCommand = 3;                // a control's message: this first dword,
constexpr uint32_t kCommandKind = 0x287259F6;   // this second one and the control's ID third
constexpr uint32_t kAllRoutes = 0xEBB0F666;     // check box: all routes (the builder's flag), radio with ...
constexpr uint32_t kSomeRoutes = 0xABB0F65F;    // ... this one
constexpr uint32_t kShowOther = 0x4BB92C1F;     // button: the first window shown, the second hidden
constexpr uint32_t kShowSecond = 0xEBB91356;    // button: the second window shown (0x004C4630 with 1)

// The route kinds' check boxes and the bits of the mask the route line
// gets: a box that is not checked clears its bits. The first box sets the
// mask to 0xFFFFFFFE, the others clear bits of it.
constexpr struct {
    uint32_t id;
    uint32_t keep;
} kRouteBoxes[] = {
    {0x4BB0F628, ~uint32_t{0x2}},  {0xEBB0F630, ~uint32_t{0x4}},  {0x4BB91326, ~uint32_t{0x8}},
    {0x6BB91318, ~uint32_t{0x10}}, {0x2BB9131F, ~uint32_t{0x20}}, {0xABB0F639, ~uint32_t{0xC0}},
    {0x4C081783, ~uint32_t{0x100}}, {0x00000001, 0x7FFFFFFFu},
};
constexpr uint32_t kFirstRouteBox = 0x2BB0F616;

// The line's widths by zoom (the six floats the builder keeps on its
// stack); the game reads the zoom's entry unchecked.
constexpr float kWidths[6] = {2.0f, 2.0f, 2.0f, 2.0f, 1.4f, 1.2f};

// The floats at 0x00A84D2C (0.5) and 0x00A9097C (the bits 0x38D1B717,
// 1e-4: how far the ground point moves before it counts).
constexpr float kHalf = 0.5f;
constexpr float kMoved = 9.99999975e-05f;

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

// --- Helpers by address -----------------------------------------------------------------

// The game's 0x004C38D0 takes the terrain in ESI and a point (x, z) on the
// stack, which the caller removes; the height comes back on the x87 stack:
// (terrain, x, z, function).
__declspec(naked) void HeightThunk()
{
    __asm push esi
    __asm mov esi, dword ptr [esp + 8]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm call dword ptr [esp + 28]
    __asm add esp, 8
    __asm pop esi
    __asm ret
}

// The terrain's height at a point held inside the city (0x004C38D0).
float HeightAt(void* terrain, float x, float z)
{
    using ThunkFn = float(__cdecl*)(void*, float, float, std::uintptr_t);
    return reinterpret_cast<ThunkFn>(&HeightThunk)(terrain, x, z, addr::SC4QueryTool_AddClampedPoint);
}

// The route builder, by address (0x004CA460, ret 4).
void BuildRoutesAt(char* self, int32_t* box)
{
    This<void>(addr::SC4QueryTool_BuildRoutes, self, box);
}

// The highlight of the occupant's lot, by address (0x004CACC0, ret 4).
void HighlightLotAt(char* self, uint32_t highlight)
{
    This<void>(addr::SC4QueryTool_HighlightLot, self, highlight);
}

// --- The routes ----------------------------------------------------------------------------

// What the builder does for one traffic source (a lot's, slot 4 of the lot;
// or a neighbour connection's): the source's trips (slot 3: a list whose
// first dword is the sentinel) and its cells (slot 5, asked twice: the
// first answer's corner cells give the place, the second's the box the
// source covers). The place is the middle of the two corners' centres
// (city slot 97, CellCenterToPosition) at the terrain's height there
// (0x004C38D0, asked before the trips are looked at). A trip whose flags
// (the node's +0x26) have 8 and not 2 gets its route line (0x004C5E20, the
// game's: drawing) when the source's box overlaps the tool's box or the
// trip's route touches it (0x004C4AE0).
void SourceRoutes(char* self, void* city, void* terrain, void* source, int32_t* box, uint32_t back, uint32_t mask)
{
    void** const sentinel = *Virtual<void***>(source, 0xC);
    void** node = static_cast<void**>(sentinel[0]);
    int32_t cells[4] = {};
    Virtual<int32_t*>(source, 0x14, static_cast<int32_t*>(cells));
    int32_t again[4] = {};
    const int32_t* const covers = Virtual<int32_t*>(source, 0x14, static_cast<int32_t*>(again));
    const bool inside =
        box[2] >= covers[0] && box[0] <= covers[2] && box[3] >= covers[1] && box[1] <= covers[3];
    float firstX = 0.0f, firstZ = 0.0f, lastX = 0.0f, lastZ = 0.0f;
    Virtual<void>(city, 0x184, cells[0], cells[1], &firstX, &firstZ);
    Virtual<void>(city, 0x184, cells[2], cells[3], &lastX, &lastZ);
    const float x =
        static_cast<float>((static_cast<double>(lastX) + static_cast<double>(firstX)) * static_cast<double>(kHalf));
    const float z =
        static_cast<float>((static_cast<double>(lastZ) + static_cast<double>(firstZ)) * static_cast<double>(kHalf));
    const float y = HeightAt(terrain, x, z);
    for (; node != sentinel; node = static_cast<void**>(node[0])) {
        const uint8_t flags = At<uint8_t>(node, trip_node::kFlags);
        if (!(flags & 8) || (flags & 2))
            continue;
        char* const trip = reinterpret_cast<char*>(node) + trip_node::kRecord;
        if (!inside && !This<uint8_t>(addr::SC4QueryTool_RouteTouches, self, trip, box, back))
            continue;
        This<void>(addr::SC4QueryTool_BuildRouteLine, self, source, trip, x, y, z, back, mask);
    }
}

// The game's 0x004CA460 (ret 4; the box is the tool's +0x30 from every
// caller): the route lines of the trips that touch the box. The terrain
// (city slot 84) and the camera's zoom (0x007F66A0) first. Without a line
// at +0x9C one is made (0x2C bytes, 0x007DDD50), referenced, set up
// (0x007DCD90) and given to the render service (slot 32, layer 5, order
// 1000); its width (+0x10, 0x008B6DE0) is the zoom's. The window at +0x48
// gives the flag (0xEBB0F666 checked: the way back too) and the mask (a
// route kind box not checked clears its bits; 0x009AFEBC does not check the
// window, so kind 0 would call through null as in the game). Then the
// trips of every lot (the lot manager's slot 15 hands out an iterator over
// its lots, never given back; each lot's slot 4 is its traffic source) and
// of every neighbour connection (the neighbours simulator's slot 16 fills
// a list of them, referenced; their slot 4).
void __fastcall BuildRoutes(char* self, void*, int32_t* box)
{
    void* const city = Global<void*>(addr::SC4_City);
    void* const terrain = Virtual<void*>(city, 0x150);
    int32_t zoom = 0, rotation = 0;
    This<void>(addr::SC4CameraView_ZoomAndRotation, Global<void*>(addr::SC4_CameraView), &zoom, &rotation);
    uint32_t back = 0;
    uint32_t mask = 0xFFFFFFFF;
    if (!At<void*>(self, field::kObject9C)) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x2C});
        void* const line = memory ? This<void*>(addr::SC4RouteLine_Construct, memory) : nullptr;
        void* const held = At<void*>(self, field::kObject9C);
        if (line != held) {
            if (line)
                AddRef(line);
            At<void*>(self, field::kObject9C) = line;
            if (held)
                Release(held);
        }
        This<void>(addr::SC4RouteLine_Prepare, At<void*>(self, field::kObject9C));
        Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x80, At<void*>(self, field::kObject9C), uint32_t{5},
                      uint32_t{1000});
    }
    This<void>(addr::cSC4DepartmentBudget_SetId, At<void*>(self, field::kObject9C), kWidths[zoom]);

    if (Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, At<void*>(self, field::kPanel), kAllRoutes))
        back = 1;
    if (!Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, At<void*>(self, field::kPanel), kFirstRouteBox))
        mask = 0xFFFFFFFE;
    for (const auto& routeBox : kRouteBoxes) {
        if (!Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, At<void*>(self, field::kPanel), routeBox.id))
            mask &= routeBox.keep;
    }

    void** const lots = Virtual<void**>(Global<void*>(addr::SC4_LotManager), 0x3C);
    for (;;) {
        void** const container = static_cast<void**>(lots[1]);
        if (!container || lots[0] == container[0])
            break;
        void** const node = static_cast<void**>(lots[0]);
        lots[0] = node[0];
        void* const source = Virtual<void*>(node[2], 0x10);
        if (source)
            SourceRoutes(self, city, terrain, source, box, back, mask);
    }

    void** const sentinel =
        static_cast<void**>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x40, &list);
    void** const end = static_cast<void**>(list);
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const source = Virtual<void*>(node[2], 0x10);
        if (source)
            SourceRoutes(self, city, terrain, source, box, back, mask);
    }
    This<void>(addr::SC4List_ClearReleasing, &list);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, list);
}

// The game's 0x004CAA60 (no arguments; through 0x004CAC50 for a building):
// the box is the bounding rectangle (lot slot 14) of the occupant's lot
// (the lot manager's slot 14), or of the lot it travels for (lot slot 59,
// asked twice) when there is one; the lot's slot 4 is asked and not used.
// Route mode 1, or 3 when the occupant is of the group 0x1003; then the
// routes. False without a lot. The occupant is not checked for null.
uint8_t __fastcall LotRoutes(char* self, void*)
{
    void* lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, At<void*>(self, field::kOccupant));
    if (!lot)
        return 0;
    if (Virtual<void*>(lot, 0xEC))
        lot = Virtual<void*>(lot, 0xEC);
    Virtual<void*>(lot, 0x10);
    int32_t* const box = &At<int32_t>(self, field::kBox);
    Virtual<uint8_t>(lot, 0x38, box);
    At<uint32_t>(self, field::kRouteMode) = 1;
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupant), 0x58, kLotGroup))
        At<uint32_t>(self, field::kRouteMode) = 3;
    BuildRoutesAt(self, box);
    return 1;
}

// The game's 0x004CAAE0 (no arguments; through 0x004CAC50 for a network
// piece): the terrain is asked (city slot 84) and not used. When the
// occupant answers the network occupant's interface: the box is the cell
// it stands on (its slot 8 position, city slot 95), widened by one cell
// toward the first side (of four, a byte each of the edge flags, slot 38,
// with bit 4) of the first network type (0 to 12) the piece is of (slot
// 22); route mode 2 for a subway piece (type 7), else 1; then the routes.
// The interface is let go. True either way. The position's place is not
// set before the call (0 here).
uint8_t __fastcall NetworkRoutes(char* self, void*)
{
    Virtual<void*>(Global<void*>(addr::SC4_City), 0x150);
    void* network = nullptr;
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupant), 0x0, kNetworkOccupant, &network)) {
        float position[3] = {};
        Virtual<uint8_t>(At<void*>(self, field::kOccupant), 0x20, static_cast<float*>(position));
        int32_t cellX = 0, cellZ = 0;
        Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
        int32_t* const box = &At<int32_t>(self, field::kBox);
        box[0] = cellX;
        box[1] = cellZ;
        box[2] = cellX;
        box[3] = cellZ;
        bool done = false;
        for (uint32_t type = 0; type < 13 && !done; ++type) {
            if (!Virtual<uint8_t>(network, 0x58, type))
                continue;
            uint32_t edges = Virtual<uint32_t>(network, 0x98, type);
            for (uint32_t side = 0; side < 4; ++side, edges >>= 8) {
                if (!(edges & 4))
                    continue;
                switch (side) {
                case 0:
                    box[0] = cellX - 1;
                    box[1] = cellZ;
                    box[2] = cellX;
                    box[3] = cellZ;
                    break;
                case 1:
                    box[1] = cellZ - 1;
                    box[0] = cellX;
                    box[2] = cellX;
                    box[3] = cellZ;
                    break;
                case 2:
                    box[0] = cellX;
                    box[1] = cellZ;
                    box[2] = cellX + 1;
                    box[3] = cellZ;
                    break;
                default:
                    box[1] = cellZ;
                    box[0] = cellX;
                    box[2] = cellX;
                    box[3] = cellZ + 1;
                    break;
                }
                done = true;
                break;
            }
        }
        At<uint32_t>(self, field::kRouteMode) = Virtual<uint8_t>(network, 0x58, kSubway) ? 2 : 1;
        BuildRoutesAt(self, box);
    }
    if (network)
        Release(network);
    return 1;
}

// The game's 0x004CAC30 (no arguments; from the window's handler): with a
// line, its points emptied (0x007DDC30) and the routes built again for the
// box.
void __fastcall RebuildRoutes(char* self, void*)
{
    if (!At<void*>(self, field::kObject9C))
        return;
    This<void>(addr::SC4RouteLine_Clear, At<void*>(self, field::kObject9C));
    BuildRoutesAt(self, &At<int32_t>(self, field::kBox));
}

// The game's 0x004CAC50 (no arguments; called by OnMouseDownL 0x004D4D70 for
// kind 1): a line is taken off the render service (slot 33), unset
// (0x0053C940) and let go; then a building's routes (0x004CAA60) or a
// network piece's (0x004CAAE0), by the occupant's type (slot 7), as tail
// calls. False without an occupant or of another type.
uint8_t __fastcall ShowRoutes(char* self, void*)
{
    if (void* const line = At<void*>(self, field::kObject9C)) {
        Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x84, line);
        This<void>(addr::SC4RouteLine_Unprepare, At<void*>(self, field::kObject9C));
        if (void* const held = At<void*>(self, field::kObject9C)) {
            At<void*>(self, field::kObject9C) = nullptr;
            Release(held);
        }
    }
    void* const occupant = At<void*>(self, field::kOccupant);
    if (!occupant)
        return 0;
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kTypeBuilding)
        return This<uint8_t>(addr::SC4QueryTool_LotRoutes, self);
    if (type == kTypeNetwork)
        return This<uint8_t>(addr::SC4QueryTool_NetworkRoutes, self);
    return 0;
}

// --- The highlight ---------------------------------------------------------------------------

// The game's 0x004CACC0 (ret 4): every occupant of the occupant's lot (the
// lot manager's slot 14; held meanwhile; lot slot 49 fills a list of the
// allocator at 0x00B207E4) whose property 0x6A95E503 is true (0x005FD390)
// gets the highlight (slot 17, not sent now); the list's references are let
// go by the occupant manager (slot 48) and the list freed (0x00413FE0).
// Nothing without an occupant or a lot.
void __fastcall HighlightLot(char* self, void*, uint32_t highlight)
{
    void* const occupant = At<void*>(self, field::kOccupant);
    if (!occupant)
        return;
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (!lot)
        return;
    AddRef(lot);
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    Virtual<void>(lot, 0xC4, &list);
    void** const end = static_cast<void**>(list);
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const each = node[2];
        uint8_t redirect = 0;
        Cdecl<uint8_t>(addr::SC4_GetPropertyBool, Virtual<void*>(each, 0x18), kRedirectProperty, &redirect);
        if (redirect)
            Virtual<uint8_t>(each, 0x44, highlight, uint32_t{0});
    }
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &list);
    This<void>(addr::SC4List_Destroy2, &list);
    Release(lot);
}

// --- The text map --------------------------------------------------------------------------

// The game's 0x004CB8B0 (ret 4; this, the map, is not read; called by
// 0x004D5C90 and 0x004D6C70 and by itself): a subtree of the text map is
// freed, the right side first, then down the left: each node's string
// buffer and the node (0x0090CF63).
void __fastcall EraseTexts(char* self, void*, char* node)
{
    while (node) {
        EraseTexts(self, nullptr, At<char*>(node, text_node::kRight));
        char* const left = At<char*>(node, text_node::kLeft);
        At<std::uintptr_t>(node, text_node::kText) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(node, text_node::kTextBuffer))
            Cdecl<void>(addr::SC4_Free, buffer);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
        node = left;
    }
}

// --- The window of kind 1 --------------------------------------------------------------------

// The game's 0x004CB900 (slot 3 of the panel target, vtable 0x00A90980, ret
// 8: the window, the message): for the window 0x0BB0F5E7 (its slot 63) the
// message 0xA2BF8AD5 sets the two radio boxes (0xABB0F65F on, 0xEBB0F666
// off; 0x009AFE69). A control's message (3, 0x287259F6, its ID): a route
// kind box makes the routes again when there is a line (0x007DDC30, then
// the builder for the box); 0x4BB92C1F shows the first window and hides
// the second (no check of the tool's windows past both being there);
// 0xEBB91356 shows the second (0x004C4630); 0xABB0F65F and 0xEBB0F666 set
// the radio pair and make the routes again (0x004CAC30). False always.
uint8_t __fastcall PanelOnMessage(char* target, void*, void* window, const uint32_t* message)
{
    char* const tool = At<char*>(target, panel_target::kTool);
    if (message[0] == kWindowMessage && Virtual<uint32_t>(window, 0xFC) == tool_query::kPanelId) {
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, kSomeRoutes, uint32_t{1});
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, kAllRoutes, uint32_t{0});
        return 0;
    }
    if (message[0] != kCommand || message[1] != kCommandKind)
        return 0;
    const uint32_t id = message[2];
    bool routeBox = id == kFirstRouteBox;
    for (const auto& routeKind : kRouteBoxes)
        routeBox = routeBox || id == routeKind.id;
    if (routeBox) {
        if (At<void*>(tool, field::kObject9C)) {
            This<void>(addr::SC4RouteLine_Clear, At<void*>(tool, field::kObject9C));
            BuildRoutesAt(tool, &At<int32_t>(tool, field::kBox));
        }
        return 0;
    }
    switch (id) {
    case kShowOther:
        if (At<void*>(tool, field::kPanel) && At<void*>(tool, field::kPanelOther)) {
            Virtual<void>(At<void*>(tool, field::kPanel), 0x114);
            Virtual<void>(At<void*>(tool, field::kPanelOther), 0x118);
        }
        break;
    case kShowSecond:
        This<void>(addr::SC4QueryTool_ShowOtherPanel, tool, uint32_t{1});
        break;
    case kSomeRoutes:
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, kSomeRoutes, uint32_t{1});
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, kAllRoutes, uint32_t{0});
        This<void>(addr::SC4QueryTool_RebuildRoutes, tool);
        break;
    case kAllRoutes:
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, kSomeRoutes, uint32_t{0});
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, kAllRoutes, uint32_t{1});
        This<void>(addr::SC4QueryTool_RebuildRoutes, tool);
        break;
    default:
        break;
    }
    return 0;
}

// --- The tool's slots ----------------------------------------------------------------------

// What Shutdown does with each of the two windows of kind 1: its target
// cleared (what it answers for 0x5386D516, slot 7 with null), taken out of
// the view's window (slot 16), the field cleared and the window let go.
void PutAwayWindow(char* self, std::size_t at)
{
    if (void* const window = At<void*>(self, at)) {
        void* procedure = nullptr;
        if (Virtual<uint8_t>(window, 0x0, kProcedureIid, &procedure))
            Virtual<void>(procedure, 0x1C, static_cast<void*>(nullptr));
        void* const viewWindow = Virtual<void*>(At<void*>(self, base::kView), 0xC);
        Virtual<void>(viewWindow, 0x40, At<void*>(self, at));
        if (procedure)
            Release(procedure);
    }
    if (void* const held = At<void*>(self, at)) {
        At<void*>(self, at) = nullptr;
        Release(held);
    }
}

// What the slots share when the occupant under the pointer is let go: its
// own highlight back (slot 17 with +0x40, not sent now), the lot's
// highlight cleared (0x004CACC0 with 0), let go.
void PutBackOccupant(char* self)
{
    Virtual<uint8_t>(At<void*>(self, field::kOccupant), 0x44, At<uint32_t>(self, field::kField40), uint32_t{0});
    HighlightLotAt(self, 0);
}

// The game's 0x004CBA80 (slot 4): Shutdown. True, at once when not inited.
// The view's text at the pointer cleared (slot 41); the tip's window taken
// out of the view's window (slot 16) and the tip let go; the two windows put
// away; the line taken off the render service (slot 33), unset and let go;
// the occupant put back (+0x40 cleared) and let go; the target part's six
// messages given up (the message server's slot 6, when there is one); the
// timer stopped when it runs (slots 8 and 7; +0x78 cleared); the base's
// Shutdown (0x005FAFD0).
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    Virtual<void>(At<void*>(self, base::kView), 0xA4, kCursorText);
    if (void* const tip = At<void*>(self, field::kTip)) {
        void* const viewWindow = Virtual<void*>(At<void*>(self, base::kView), 0xC);
        Virtual<void>(viewWindow, 0x40, Virtual<void*>(tip, 0xC));
    }
    if (void* const tip = At<void*>(self, field::kTip)) {
        At<void*>(self, field::kTip) = nullptr;
        Release(tip);
    }
    PutAwayWindow(self, field::kPanel);
    PutAwayWindow(self, field::kPanelOther);
    if (void* const line = At<void*>(self, field::kObject9C)) {
        Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x84, line);
        This<void>(addr::SC4RouteLine_Unprepare, At<void*>(self, field::kObject9C));
        if (void* const held = At<void*>(self, field::kObject9C)) {
            At<void*>(self, field::kObject9C) = nullptr;
            Release(held);
        }
    }
    if (At<void*>(self, field::kOccupant)) {
        PutBackOccupant(self);
        At<uint32_t>(self, field::kField40) = 0;
        Release(At<void*>(self, field::kOccupant));
        At<void*>(self, field::kOccupant) = nullptr;
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x18, static_cast<void*>(self + field::kTarget), message);
    }
    if (Virtual<uint8_t>(self + field::kTimer, 0x20)) {
        At<uint8_t>(self, field::kFlag78) = 0;
        Virtual<void>(self + field::kTimer, 0x1C);
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004CBC70 (slot 20, OnMouseMove, ret 0xC: screen x, z, the
// modifiers, not read). +0x88 cleared; nothing more without a view. A
// filter for the pick (0xC bytes: buildings or networks for kind 1, else
// what can be asked about; referenced when it was made) and the occupant
// under the point (0x004C4C90). When one is picked (+0x88 set): if its
// property 0x6A95E503 is true, the building of its lot (lot manager slot
// 14, lot slot 48, its slot 3) takes its place; the ground point goes into
// +0x7C when the view picks with it (+0x94; the view's slot 36). Else the
// ground point under the pointer (slot 36; the dword at 0x00B21858 three
// times when it fails) gives a cell (city slot 95) and the lot there (lot
// manager slot 6): its building stands for the occupant when the filter
// takes it (kind 1; +0x88 set) or it is of the group 0x1006 (kind 0; +0x88
// set for kind 0 in any case); the point replaces +0x7C when it moved by at
// least 1e-4 on an axis (or is not a number), and with no occupant that
// counts as a change. A new occupant swaps with the one at +0x2C: the old
// one is put back and let go, the new one keeps its highlight at +0x40,
// gets 7 (slot 17) with its lot's (0x004CACC0) and is referenced. On a
// change the view's text at the pointer is cleared and the tip hidden.
// Unless the button is down (+0x89) the timer is started again (stopped,
// +0x78 set, slot 6 with +0x90 and the tool). A shown tip goes to the
// point (its slot 15). The filter is let go. True.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    At<uint8_t>(self, field::kFlag88) = 0;
    if (!At<void*>(self, base::kView))
        return 1;
    bool changed = false;
    void* picked = nullptr;
    void* filter = nullptr;
    const bool second = At<uint32_t>(self, field::kKind) == 1;
    if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xC})) {
        This<void*>(addr::SC4_OccupantFilterBase, memory);
        At<std::uintptr_t>(memory, 0) =
            second ? addr::SC4QueryTool_BuildingFilterVtable : addr::SC4QueryTool_AskableFilterVtable;
        At<std::uintptr_t>(memory, 4) =
            second ? addr::SC4QueryTool_BuildingFilterVtable2 : addr::SC4QueryTool_AskableFilterVtable2;
        AddRef(memory);
        filter = memory;
    }
    void* occupant = nullptr;
    if (Cdecl<uint8_t>(addr::SC4QueryTool_PickOccupant, x, z, &picked, filter)) {
        occupant = picked;
        At<uint8_t>(self, field::kFlag88) = 1;
        uint8_t redirect = 0;
        Cdecl<uint8_t>(addr::SC4_GetPropertyBool, Virtual<void*>(occupant, 0x18), kRedirectProperty, &redirect);
        if (redirect) {
            if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant)) {
                AddRef(lot);
                if (void* const building = Virtual<void*>(lot, 0xC0)) {
                    AddRef(building);
                    Release(occupant);
                    occupant = Virtual<void*>(building, 0xC);
                    AddRef(occupant);
                    Release(building);
                }
                Release(lot);
            }
        }
    } else {
        occupant = picked;
    }
    if (At<uint8_t>(self, field::kFlag88)) {
        if (At<uint8_t>(self, field::kFlag94))
            Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, &At<float>(self, field::kPlace), uint32_t{0});
    } else {
        const uint32_t start = Global<uint32_t>(addr::SC4QueryTool_DwordB21858);
        uint32_t point[3] = {start, start, start};
        if (Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, reinterpret_cast<float*>(point), uint32_t{0})) {
            const float pointX = At<float>(point, 0);
            const float pointZ = At<float>(point, 8);
            int32_t cellX = 0, cellZ = 0;
            Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, pointX, pointZ, &cellX, &cellZ);
            void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, uint32_t{0});
            if (lot) {
                AddRef(lot);
                void* const building = Virtual<void*>(lot, 0xC0);
                if (building)
                    AddRef(building);
                if (second) {
                    if (building) {
                        if (Virtual<uint8_t>(filter, 0xC, Virtual<void*>(building, 0xC))) {
                            occupant = Virtual<void*>(building, 0xC);
                            AddRef(occupant);
                            At<uint8_t>(self, field::kFlag88) = 1;
                        }
                        Release(building);
                    }
                } else if (building) {
                    if (Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, kBuildingGroup)) {
                        occupant = Virtual<void*>(building, 0xC);
                        AddRef(occupant);
                    }
                    Release(building);
                }
            }
            if (!second)
                At<uint8_t>(self, field::kFlag88) = 1;
            if (lot)
                Release(lot);
        }
        bool moved = false;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double now = static_cast<double>(At<float>(point, axis * 4));
            const double before = static_cast<double>(At<float>(self, field::kPlace + axis * 4));
            const double distance = before - now < 0.0 ? now - before : before - now;
            if (!(distance < static_cast<double>(kMoved))) {
                moved = true;
                break;
            }
        }
        if (moved) {
            At<uint32_t>(self, field::kPlace) = point[0];
            At<uint32_t>(self, field::kPlace + 4) = point[1];
            At<uint32_t>(self, field::kPlace + 8) = point[2];
            if (!occupant)
                changed = true;
        }
    }

    if (occupant != At<void*>(self, field::kOccupant)) {
        changed = true;
        if (At<void*>(self, field::kOccupant)) {
            PutBackOccupant(self);
            Release(At<void*>(self, field::kOccupant));
        }
        At<void*>(self, field::kOccupant) = occupant;
        if (occupant) {
            At<uint32_t>(self, field::kField40) = Virtual<uint32_t>(occupant, 0x40);
            Virtual<uint8_t>(At<void*>(self, field::kOccupant), 0x44, kHighlight, uint32_t{0});
            HighlightLotAt(self, kHighlight);
            AddRef(At<void*>(self, field::kOccupant));
            Release(occupant);
        } else {
            At<uint32_t>(self, field::kField40) = 0;
        }
    } else if (occupant) {
        Release(occupant);
    }
    if (changed) {
        Virtual<void>(At<void*>(self, base::kView), 0xA4, kCursorText);
        Virtual<void>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x118);
    }
    if (!At<uint8_t>(self, field::kPressed)) {
        Virtual<void>(self + field::kTimer, 0x1C);
        At<uint8_t>(self, field::kFlag78) = 1;
        Virtual<void>(self + field::kTimer, 0x18, At<uint32_t>(self, field::kSecond), static_cast<void*>(self));
    }
    if (void* const tip = At<void*>(self, field::kTip)) {
        if (Virtual<uint8_t>(Virtual<void*>(tip, 0xC), 0x11C)) {
            int32_t at[2] = {x, z};
            Virtual<void>(At<void*>(self, field::kTip), 0x3C, static_cast<int32_t*>(at));
        }
    }
    if (filter)
        Release(filter);
    return 1;
}

// The game's 0x004CC0B0 (slot 22, OnMouseExit): the timer stopped, the
// view's text at the pointer cleared, the tip's window hidden (the tip is
// not checked); the occupant put back, let go and cleared (+0x40 kept);
// +0x88 cleared. True.
uint8_t __fastcall OnMouseExit(char* self, void*)
{
    Virtual<void>(self + field::kTimer, 0x1C);
    Virtual<void>(At<void*>(self, base::kView), 0xA4, kCursorText);
    Virtual<void>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x118);
    if (At<void*>(self, field::kOccupant)) {
        PutBackOccupant(self);
        Release(At<void*>(self, field::kOccupant));
        At<void*>(self, field::kOccupant) = nullptr;
    }
    At<uint8_t>(self, field::kFlag88) = 0;
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery4a(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_BuildRoutes, reinterpret_cast<void*>(&BuildRoutes)},
        {addr::SC4QueryTool_LotRoutes, reinterpret_cast<void*>(&LotRoutes)},
        {addr::SC4QueryTool_NetworkRoutes, reinterpret_cast<void*>(&NetworkRoutes)},
        {addr::SC4QueryTool_RebuildRoutes, reinterpret_cast<void*>(&RebuildRoutes)},
        {addr::SC4QueryTool_ShowRoutes, reinterpret_cast<void*>(&ShowRoutes)},
        {addr::SC4QueryTool_HighlightLot, reinterpret_cast<void*>(&HighlightLot)},
        {addr::SC4QueryTool_EraseTexts, reinterpret_cast<void*>(&EraseTexts)},
        {addr::SC4QueryPanelTarget_OnMessage, reinterpret_cast<void*>(&PanelOnMessage)},
        {addr::SC4QueryTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4QueryTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::SC4QueryTool_OnMouseExit, reinterpret_cast<void*>(&OnMouseExit)},
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
    "ui.tools.query4a",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_BuildRoutes,
    "Our query tool, fourth piece, first half: the routes, the highlight, the window of kind 1, Shutdown and the "
    "mouse moves (docs/tools.md)",
    false,
    InstallQuery4a,
});

} // namespace
} // namespace opensc4::tool_query_4a
