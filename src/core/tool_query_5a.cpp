// The query tool of the city view, fifth piece, first half: the query's
// windows by cell, two fillers and the route counts, 0x004CC6E0 to
// 0x004CCF14, 0x004CDC80 to 0x004CDEFF and 0x004CF8A0 to 0x004D07B1.
// Replacement, off by default:
//
//   ui.tools.query5a: 8 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): the window for a neighbour connection at
//     the city's edge (0x004CC6E0) and the window for a cell (0x004CCCB0),
//     which load a layout, run it modal and register the walks 0x004CC660
//     and 0x004CC6A0 with it; fillers 20 (the mayor rating at an
//     occupant's cell) and 91 (the picture of the MySim living in a
//     building); the name of a network piece (0x004CF8A0); the trips of a
//     lot (0x004CFB30) or of every source touching a box (0x004CFD20)
//     counted by route kind as text, and the title, text and icon of the
//     route view for a building by its purposes (0x004D0190).
//
// Entries of the filler table at 0x00A92488 that are functions of this
// file (index, address): 20 0x004CDC80, 91 0x004CDCC0. Neither changes the
// city: both answer true without `fill` (20 asks its cell first; false
// without an occupant).
//
// Left the game's: 0x004CCF20 (the text pair's copy constructor, ret 8:
// folded, callers 0x00895DCF, 0x009586B0, 0x0097B612 outside the class) and
// SC4MapVector::CopyConstruct 0x004CDF00 (caller 0x006C2450 outside).
//
// No random number is drawn and no clock is read. The windows run modal
// through the window manager (slot 41, DoModalWin: real time, the player's
// clicks); 0x004CCCB0 for kind 0xE plays a sound (0x00B43CB8, slot 11). The
// route view sets the tool's route mode (+0x98). 0x004CFD20 never gives back
// the lot manager's slot 15 iterator (8 bytes), as in the game; 0x004D0190
// never lets go of the purposes property's value (QueryInterface 0x60FD4D2B),
// as in the game.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_5a {
namespace {

namespace field = tool_query::field;
namespace window_runner = tool_query::window_runner;
namespace trip_node = tool_query::trip_node;
namespace window_cell = tool_query::window_cell;

constexpr uint32_t kNetworkOccupant = 0xA821EF94; // the network occupant's interface
constexpr uint32_t kOtherOccupant = 0xA8C86142;   // the other interface the edge window takes (type 4)
constexpr uint32_t kBuildingIid = 0x87DDA3A9;     // the building occupant's interface
constexpr uint32_t kProcedureIid = 0x5386D516;    // a window's procedure part (the runner takes it)
constexpr uint32_t kWindowIid = 0x22BA0121;
constexpr uint32_t kPictureIid = 0xC12CEA13;      // what a picture control is asked for
constexpr uint32_t kLoadFlags = 0x10000005;       // LoadWindowChild's third argument
constexpr uint32_t kQueryTextGroup = 0xEA5524EB;  // the group of the query's game strings
constexpr uint32_t kPurposesProperty = 0xAA1DD396; // a building's purposes (uint32 array)
constexpr uint32_t kValueIid = 0x60FD4D2B;        // the property's value
constexpr uint32_t kUint32Array = 0x8006;         // the value's type
constexpr uint32_t kPortraitType = 0x46A006B0;    // the MySim pictures' type
constexpr uint32_t kAllRoutes = 0xEBB0F666;       // the window of kind 1's check box: all routes
constexpr uint32_t kQuerySecondStep = 0x200;       // what the cell window's query adds to the control IDs

// The edge window's types from the network piece's types (network slot 22),
// in the order the game asks; the last one the piece is of wins.
constexpr struct {
    uint32_t network;
    uint32_t type;
} kEdgeTypes[] = {{0xB, 10}, {0, 2}, {6, 8}, {0xC, 12}, {2, 3}, {7, 6}, {8, 7}, {9, 11}, {1, 1}, {4, 5}};

// The four cells next to a cell, as the edge window looks at them.
constexpr int32_t kStepX[4] = {-1, 0, 1, 0};
constexpr int32_t kStepZ[4] = {0, -1, 0, 1};

// The game strings the route counts are told by (0x00B09604: one for each
// of the nine route kinds) and the one for no routes.
constexpr uint32_t kRouteKindNames[9] = {0x2B8B39FE, 0x2B8B3A10, 0x8B8B3A1A, 0xAB8B3A26, 0x8B8B3AB4,
                                         0xCB8C5C08, 0xAB8B3A4A, 0x2BC7E0D2, 0xCC05A60F};
constexpr uint32_t kNoRoutes = 0x2BAAB5C0;
constexpr uint32_t kRouteKindCount = 9;
constexpr uint32_t kRouteKindsAll = 0x1CC; // kinds that drop bit 0

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

// --- The game's strings --------------------------------------------------------------------

// SC4String as the game keeps one on the stack: the vtable 0x00A80810, an
// STL body (begin, end, end of the buffer) and a zero dword.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// SC4LocalizedString: the string, then the key. 0x20 bytes.
struct LocalizedString {
    GameString text;
    uint32_t key[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// The game's inline destructor (SC4String::Destroy 0x00414250 does the
// same): the vtable set to the string's again, the buffer freed.
void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// An empty string as the game makes one in line: 8 bytes from the STL
// allocator, the first cleared.
void MakeEmpty(GameString& text)
{
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A localized string of the query's group, loaded (0x00603580 with 1).
void MakeLocalized(LocalizedString& text, uint32_t instance)
{
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kQueryTextGroup, instance, uint32_t{1});
}

// The body of an SC4String the caller passed (its +4).
char* BodyOf(void* text)
{
    return static_cast<char*>(text) + 0x4;
}

bool BodyEmpty(char* body)
{
    return At<char*>(body, 0x0) == At<char*>(body, 0x4);
}

// The game string as the title: Assign (0x00424380) from a localized
// string, which is then destroyed.
void SetTitle(void* title, uint32_t instance)
{
    LocalizedString text;
    MakeLocalized(text, instance);
    This<void>(addr::cRZBaseString_Assign, title, &text);
    Destroy(text.text);
}

// --- The query's windows by cell -------------------------------------------------------------

// What both windows do once their layout is loaded: the window's procedure
// part (QueryInterface 0x5386D516) gets a runner of its own (0x18 bytes,
// 0x004C5620, referenced) at its slot 7, and the procedure is let go. The
// runner (counted) or null.
void* AttachRunner(void* window)
{
    void* runner = nullptr;
    void* procedure = nullptr;
    if (Virtual<uint8_t>(window, 0x0, kProcedureIid, &procedure)) {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{window_runner::kSize});
        char* const made = memory ? This<char*>(addr::SC4QueryWindowRunner_Construct, memory, procedure) : nullptr;
        char* part = nullptr;
        if (made) {
            AddRef(made);
            runner = made;
            part = made + window_runner::kProcedure;
        }
        Virtual<void>(procedure, 0x1C, static_cast<void*>(part));
        Release(procedure);
    }
    return runner;
}

// The bar painter both windows give their children (4 bytes, vtable
// 0x00A90B38, referenced); null when there is no memory.
void* MakePainter()
{
    std::uintptr_t* const painter = Cdecl<std::uintptr_t*>(addr::SC4_operator_new, uint32_t{4});
    if (!painter)
        return nullptr;
    *painter = addr::SC4QueryBarPainter_Vtable;
    AddRef(painter);
    return painter;
}

// The modal run both windows share: the window given to the service
// 0x00B43CE0 (slot 10 with 1), run (window manager slot 41), taken back
// (slot 6). The manager's answer.
uint32_t RunModal(void* manager, void* window)
{
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    const uint32_t answer = Virtual<uint32_t>(manager, 0xA4, window);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
    return answer;
}

// What both windows do at the end: the children's painter taken off (slot
// 32 with 0x004C4370), the window taken off the manager (slot 23) and let
// go, the main window (0x00B43DC4's slot 3) given back the focus (manager
// slot 37), the painter and the runner let go.
void EndWindow(void* manager, void* window, void* painter, void* runner)
{
    Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_DetachChildren, painter);
    Virtual<void>(manager, 0x5C, window);
    Release(window);
    void* const main = Virtual<void*>(Global<void*>(addr::SC4_Service43DC4), 0xC);
    Virtual<void>(manager, 0x94, main);
    if (painter)
        Release(painter);
    if (runner)
        Release(runner);
}

// The pair list the edge window keeps its cell queries in: nodes of 0x18
// bytes from the STL allocator (next, previous, the 0x10-byte query).
void FreeQueries(void** sentinel)
{
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** const next = static_cast<void**>(node[0]);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
        node = next;
    }
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    Cdecl<void>(addr::SC4_Free, static_cast<void*>(sentinel));
}

// The game's 0x004CC6E0 (cdecl (occupant), callers 0x004D4427 and
// 0x004D444F): for an occupant whose cell (slot 8, city slot 95) is on the
// city's edge, the window of the neighbour connections next to it. The
// type comes from the network piece's types (the last of kEdgeTypes the
// piece is of; 4 for an occupant of the interface 0xA8C86142; when none
// matches, what was on the game's stack: 0 here). Each of the four cells
// next to it that lies on one edge (0x006CE7A0) and has a connection of
// the type (neighbours slot 11) gives a cell query {x, z, n * 0x200,
// type}. With one or two of them the layout {0, 0x96A006B0, instance by
// type and number} is loaded (0x005F9390); for a type the table does not
// know the instance is the network piece's pointer, as in the game. The
// window runs modal with the walk 0x004CC660 given each query (the first
// two kept at 0x00B21840 and 0x00B21844, the walk at 0x00B21848, the window
// at 0x00B2184C). True when the window ended with 0; false when the cell
// is inside or there were no or too many queries; without a layout the low
// byte of the network piece's pointer, as in the game.
uint8_t __cdecl RunEdgeWindow(void* occupant)
{
    float position[3];
    Virtual<void>(occupant, 0x20, static_cast<float*>(position));
    void* const city = Global<void*>(addr::SC4_City);
    int32_t cellX = 0, cellZ = 0;
    Virtual<uint8_t>(city, 0x17C, position[0], position[2], &cellX, &cellZ);
    const bool edge = cellX == 0 || cellX == Virtual<int32_t>(city, 0x174) - 1 || cellZ == 0 ||
                      cellZ == Virtual<int32_t>(city, 0x174) - 1;
    if (!edge)
        return 0;

    // The game reuses the argument's slot for the network piece.
    void* network = nullptr;
    uint32_t type = 0;
    if (Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &network)) {
        for (const auto& entry : kEdgeTypes) {
            if (Virtual<uint8_t>(network, 0x58, entry.network))
                type = entry.type;
        }
    } else {
        void* other = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kOtherOccupant, &other))
            type = 4;
        if (other)
            Release(other);
    }

    void** const sentinel = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{0x18});
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    uint32_t count = 0;
    for (int i = 0; i < 4; ++i) {
        const int32_t z = kStepZ[i] + cellZ;
        const int32_t x = kStepX[i] + cellX;
        void* const neighbours = Global<void*>(addr::SC4_MonthEndService);
        if (!Cdecl<uint8_t>(addr::SC4_IsOnOneCityEdge, x, z, static_cast<void*>(nullptr)))
            continue;
        if (!Virtual<void*>(neighbours, 0x2C, type, x, z))
            continue;
        void** const node = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{0x18});
        int32_t* const query = reinterpret_cast<int32_t*>(node + 2);
        query[0] = x;
        query[1] = z;
        query[2] = static_cast<int32_t>(count << 9);
        query[3] = static_cast<int32_t>(type);
        void** const last = static_cast<void**>(sentinel[1]);
        node[0] = sentinel;
        node[1] = last;
        last[0] = node;
        sentinel[1] = node;
        ++count;
    }
    if (count < 1 || count > 2) {
        FreeQueries(sentinel);
        if (network)
            Release(network);
        return 0;
    }

    const bool one = count == 1;
    uint32_t instance;
    switch (type) {
    case 1:
    case 6:
    case 7:
    case 11:
        instance = one ? 0xEA566A49 : 0x4A5E7ED3;
        break;
    case 2:
    case 3:
    case 8:
    case 10:
    case 12:
        instance = one ? 0xCA566F94 : 0xAA5E14CC;
        break;
    case 4:
        instance = one ? 0x4A566C14 : 0x8A5E7BD2;
        break;
    case 5:
        instance = one ? 0x4A566D6E : 0x2A5E7490;
        break;
    default:
        instance = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(network));
        break;
    }
    void* list = sentinel;
    uint32_t key[3] = {0, tool_query::kLayoutGroup, instance};
    void* const window = Cdecl<void*>(addr::SC4UI_LoadWindowChild, static_cast<uint32_t*>(key),
                                      static_cast<void*>(nullptr), kLoadFlags);
    uint8_t result;
    if (!window) {
        result = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(network));
    } else {
        Virtual<void>(window, 0x5C);
        void* const runner = AttachRunner(window);
        void* const painter = MakePainter();
        Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_AttachChildren, painter);
        void** const first = static_cast<void**>(sentinel[0]);
        Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = addr::SC4QueryTool_FillCellChildren;
        Global<void*>(addr::SC4QueryTool_FillCellWindow) = window;
        AddRef(window);
        uint32_t kept = 0;
        for (void** node = first; node != sentinel; node = static_cast<void**>(node[0])) {
            int32_t* const query = reinterpret_cast<int32_t*>(node + 2);
            Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_FillCellChildren, query);
            if (kept < window_cell::kQueries) {
                Global<int32_t*>(addr::SC4QueryTool_FillCellQueries + 4 * kept) = query;
                ++kept;
            }
        }
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        const uint32_t answer = RunModal(manager, window);
        Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = 0;
        Global<int32_t*>(addr::SC4QueryTool_FillCellQueries) = nullptr;
        Global<int32_t*>(addr::SC4QueryTool_FillCellQueries + 4) = nullptr;
        result = answer == 0;
        Release(window);
        Global<void*>(addr::SC4QueryTool_FillCellWindow) = nullptr;
        EndWindow(manager, window, painter, runner);
    }
    This<void>(addr::SC4PairList_Destroy, &list);
    if (network)
        Release(network);
    return result;
}

// The game's 0x004CCCB0 (cdecl (kind, cell x, cell z), callers 0x004D450C
// and 0x004D491C with the kinds 0xB, 0xC and 0xE): the window of a cell,
// the layout {0, 0x96A006B0, 0x6A561B3A / 0x4A562DA5 / 0xAA561F93} (any
// other kind is taken as the instance); for 0xE a sound first (0x00B43CB8,
// slot 11: 1, 0x2A62C927, 0, 0, 0). The window runs modal with the walk
// 0x004CC660 given the query {x, z, 0x200, 0} (kept at 0x00B21840, the
// walk at 0x00B21848, the window at 0x00B2184C); when it ends with 0 the
// walk without fill (0x004CC6A0) reads the controls back. True then;
// false otherwise or without a layout.
uint8_t __cdecl RunCellWindow(int32_t kind, int32_t x, int32_t z)
{
    uint32_t instance;
    switch (kind) {
    case 0xB:
        instance = 0x6A561B3A;
        break;
    case 0xC:
        instance = 0x4A562DA5;
        break;
    case 0xE:
        instance = 0xAA561F93;
        if (void* const sound = Global<void*>(addr::SC4_Sound43CB8)) {
            AddRef(sound);
            Virtual<void>(sound, 0x2C, uint32_t{1}, uint32_t{0x2A62C927}, uint32_t{0}, uint32_t{0}, uint32_t{0});
            Release(sound);
        }
        break;
    default:
        instance = static_cast<uint32_t>(kind);
        break;
    }
    uint32_t key[3] = {0, tool_query::kLayoutGroup, instance};
    void* const window = Cdecl<void*>(addr::SC4UI_LoadWindowChild, static_cast<uint32_t*>(key),
                                      static_cast<void*>(nullptr), kLoadFlags);
    if (!window)
        return 0;
    Virtual<void>(window, 0x5C);
    void* const runner = AttachRunner(window);
    void* const painter = MakePainter();
    Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_AttachChildren, painter);
    int32_t query[4] = {x, z, static_cast<int32_t>(kQuerySecondStep), 0};
    Global<int32_t*>(addr::SC4QueryTool_FillCellQueries) = query;
    Global<int32_t*>(addr::SC4QueryTool_FillCellQueries + 4) = nullptr;
    Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = addr::SC4QueryTool_FillCellChildren;
    Global<void*>(addr::SC4QueryTool_FillCellWindow) = window;
    AddRef(window);
    Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_FillCellChildren, static_cast<int32_t*>(query));
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    const uint32_t answer = RunModal(manager, window);
    Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = 0;
    Global<int32_t*>(addr::SC4QueryTool_FillCellQueries) = nullptr;
    Global<int32_t*>(addr::SC4QueryTool_FillCellQueries + 4) = nullptr;
    const uint8_t accepted = answer == 0;
    Release(window);
    Global<void*>(addr::SC4QueryTool_FillCellWindow) = nullptr;
    if (accepted)
        Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_ReadCellChildren, static_cast<int32_t*>(query));
    EndWindow(manager, window, painter, runner);
    return accepted;
}

// --- Fillers 20 and 91 ----------------------------------------------------------------------

// The game's 0x004CDC80 (table index 20): the occupant's top left cell
// (slot 11; its answer not looked at, 0 here where the game has what was
// on the stack) to 0x004CC160, the city's mayor rating. False without an
// occupant, with or without `fill`.
uint8_t __cdecl FillOccupantCityMayorRating(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!occupant)
        return 0;
    int32_t cells[4] = {};
    Virtual<uint8_t>(occupant, 0x2C, static_cast<int32_t*>(cells));
    return Cdecl<uint8_t>(addr::SC4QueryTool_FillCityMayorRating, window, id, fill, cells[0], cells[1]);
}

// The game's picture loader (0x00602B70 / 0x00602C30 fill it, 0x00602BE0
// empties it).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// GetChildAs (window slot 37) into a counted pointer the game empties first
// (0x004F20A0).
uint8_t PictureOf(void* window, uint32_t id, void** holder)
{
    void** const out = This<void**>(addr::SC4RefPtr_Reset, holder);
    return Virtual<uint8_t>(window, 0x94, id, kPictureIid, out);
}

// The game's 0x004CDCC0 (table index 91): with `fill`, the picture of the
// MySim living in the building (the city's slot 71, the MySim simulator,
// its slot 9 with the building occupant, QueryInterface 0x87DDA3A9) into
// the picture control `id`: by its bitmap ID (MySim slot 22: the picture
// {0x46A006B0, ID}) or else by its file (slot 23) under the application's
// folder (0x00B43C94 slot 61, loader mode 8). When it was set, the parent's
// control id + 0x100 (window slot 11) gets its slot 3's slot 69 and the
// control's slot 3 its slot 23. Without an occupant, a building or a MySim
// the controls id, id + 0x100 and id + 0x200 are hidden (0x009AFCFE). True.
uint8_t __cdecl FillMySimPortrait(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* picture = nullptr;
    bool shown = false;
    void* building = nullptr;
    if (occupant && Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building)) {
        void* const mysims = Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C);
        void* const mysim = Virtual<void*>(mysims, 0x24, building);
        if (mysim) {
            bool set = false;
            const uint32_t bitmap = Virtual<uint32_t>(mysim, 0x58);
            if (bitmap) {
                ImageLoader loader;
                This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kPortraitType, bitmap,
                            uint32_t{1}, uint32_t{0});
                if (PictureOf(window, id, &picture)) {
                    Virtual<void>(picture, 0x10, loader.image);
                    set = true;
                }
                This<void>(addr::SC4ImageLoader_Destruct, &loader);
            } else {
                GameString file;
                This<void*>(addr::SC4String_Substring, &file, Virtual<void*>(mysim, 0x5C), uint32_t{0},
                            uint32_t{0xFFFFFFFF});
                GameString path;
                This<void*>(addr::SC4String_SC4String, &path);
                Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xF4, &path);
                This<void>(addr::SC4String_AppendStd, &path.begin, &file.begin);
                ImageLoader loader;
                This<void*>(addr::SC4ImageLoader_ConstructName, &loader, &path, uint32_t{8}, uint32_t{0});
                // The game keeps this one in the slot of the `fill` argument.
                void* named = nullptr;
                if (PictureOf(window, id, &named)) {
                    Virtual<void>(named, 0x10, loader.image);
                    set = true;
                }
                if (named)
                    Release(named);
                This<void>(addr::SC4ImageLoader_Destruct, &loader);
                Destroy(path);
                Destroy(file);
            }
            if (set) {
                void* const parent = Virtual<void*>(window, 0x2C);
                if (PictureOf(parent, id + 0x100, &picture)) {
                    Virtual<void>(Virtual<void*>(picture, 0xC), 0x114);
                    if (PictureOf(window, id, &picture))
                        Virtual<void>(Virtual<void*>(picture, 0xC), 0x5C);
                }
            }
            shown = true;
        }
        Release(building);
    }
    if (!shown) {
        Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id, uint32_t{0}, uint32_t{0});
        Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id + 0x100, uint32_t{0}, uint32_t{0});
        Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id + 0x200, uint32_t{0}, uint32_t{0});
    }
    if (picture)
        Release(picture);
    return 1;
}

// --- The name of a network piece -------------------------------------------------------------

// The game strings of the network types, in the order 0x004CF8A0 asks
// (network slot 22).
constexpr struct {
    uint32_t type;
    uint32_t instance;
} kNetworkNames[] = {{0, 0x6A5589D4}, {1, 0x4A5589F3}, {0xA, 0x4BD9FDD8}, {0xB, 0xCBE09C9B},
                     {9, 0x0BE09CA2}, {3, 0x8A558A04}};

// The game's 0x004CF8A0 (thiscall (text, log), ret 8; caller DoMessage
// 0x004D7160): the name of the network piece at +0x2C (QueryInterface
// 0xA821EF94; nothing without one) as the text: the game string of the
// first of its types (network slot 22) in the order 2, 8, 0xC, 0, 1, 0xA,
// 0xB, 9, 3, 4, 7, 6. For type 2 the string's body is assigned (0x00408A70),
// the others through Assign (0x00424380). For a pipe (type 4) with the
// debug keys down (0x004C4700) the water simulator's slot 19 and slot 18
// go to the log object's slot 27 (cdecl, variadic: "Distressed Pipes: %d /
// %d\n"). The piece is let go.
void __fastcall NetworkName(char* self, void*, void* text, void* log)
{
    void* network = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, field::kOccupant), 0x0, kNetworkOccupant, &network))
        return;
    const auto has = [&](uint32_t type) { return Virtual<uint8_t>(network, 0x58, type) != 0; };
    const auto name = [&](uint32_t instance) {
        LocalizedString found;
        MakeLocalized(found, instance);
        This<void>(addr::cRZBaseString_Assign, text, &found);
        Destroy(found.text);
    };
    if (has(2)) {
        LocalizedString found;
        MakeLocalized(found, 0xAA5589FB);
        char* const body = BodyOf(text);
        if (reinterpret_cast<char*>(&found.text.begin) != body)
            This<void>(addr::SC4String_AssignRange, body, found.text.begin, found.text.end, static_cast<void*>(&text));
        Destroy(found.text);
    } else if (has(8)) {
        name(0x8B8C5CBD);
    } else if (has(0xC)) {
        name(0x2BE09C9E);
    } else {
        bool done = false;
        for (const auto& entry : kNetworkNames) {
            if (has(entry.type)) {
                name(entry.instance);
                done = true;
                break;
            }
        }
        if (!done) {
            if (has(4)) {
                name(0x0A558A0A);
                if (This<uint8_t>(addr::SC4QueryTool_DebugKeysDown, self)) {
                    void* const water = Global<void*>(addr::SC4_Service43D94);
                    const uint32_t first = Virtual<uint32_t>(water, 0x48);
                    const uint32_t second = Virtual<uint32_t>(water, 0x4C);
                    using LogFn = void(__cdecl*)(void*, const char*, uint32_t, uint32_t);
                    reinterpret_cast<LogFn>((*static_cast<void***>(log))[27])(log, "Distressed Pipes: %d / %d\n",
                                                                               second, first);
                }
            } else if (has(7)) {
                name(0x8A558A10);
            } else if (has(6)) {
                name(0x8B8C5CD0);
            }
        }
    }
    Release(network);
}

// --- The route counts ---------------------------------------------------------------------

// The trips of one traffic source (its slot 3 gives the list) that count
// for the box: those with 8 and not 2 in their flags, when the source's
// cells (slot 5, asked twice; the second answer is compared) overlap the
// box or the trip's route touches it (0x004C4AE0). Each counts its number
// of trips (the record's +0x18) for every route kind (0x004C3860) it is
// of; the kinds of 0x1CC drop bit 0.
void CountSource(char* self, void* source, const int32_t* box, uint32_t mode, uint32_t* sums)
{
    void** const sentinel = *Virtual<void***>(source, 0xC);
    void** node = static_cast<void**>(sentinel[0]);
    int32_t cells[4] = {};
    Virtual<int32_t*>(source, 0x14, static_cast<int32_t*>(cells));
    int32_t again[4] = {};
    const int32_t* const covers = Virtual<int32_t*>(source, 0x14, static_cast<int32_t*>(again));
    const bool inside =
        box[2] >= covers[0] && box[0] <= covers[2] && box[3] >= covers[1] && box[1] <= covers[3];
    for (; node != sentinel; node = static_cast<void**>(node[0])) {
        const uint8_t flags = At<uint8_t>(node, trip_node::kFlags);
        if (!(flags & 8) || (flags & 2))
            continue;
        char* const trip = reinterpret_cast<char*>(node) + trip_node::kRecord;
        if (!inside && !This<uint8_t>(addr::SC4QueryTool_RouteTouches, self, trip, box, mode))
            continue;
        uint32_t kinds = This<uint32_t>(addr::SC4QueryTool_RouteKinds, self, trip, mode);
        if (kinds & kRouteKindsAll)
            kinds &= ~uint32_t{1};
        for (uint32_t i = 0; i < kRouteKindCount; ++i) {
            if (kinds & (uint32_t{1} << i))
                sums[i] += At<uint32_t>(trip, 0x18);
        }
    }
}

// What 0x004CFB30 and 0x004CFD20 end with: each kind with trips as "%s: %d"
// (the kind's game string, its Data) on a line of its own appended to the
// text; when the text is still empty, the game string 0x2BAAB5C0.
void AppendCounts(void* text, const uint32_t* sums, void* allocator)
{
    char* const body = BodyOf(text);
    for (uint32_t i = 0; i < kRouteKindCount; ++i) {
        if (sums[i] == 0)
            continue;
        GameString line;
        MakeEmpty(line);
        if (!BodyEmpty(body)) {
            static const char kNewLine[] = "\n";
            This<void>(addr::cSC4Advisor_F40EBC0, body, &kNewLine[0], &kNewLine[1], allocator);
        }
        LocalizedString name;
        MakeLocalized(name, kRouteKindNames[i]);
        const char* const data = Virtual<const char*>(&name, 0x18);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "%s: %d", data, sums[i]);
        Destroy(name.text);
        This<void>(addr::SC4String_AppendRange, body, line.begin, line.end, allocator);
        Destroy(line);
    }
    if (BodyEmpty(body)) {
        LocalizedString none;
        MakeLocalized(none, kNoRoutes);
        if (reinterpret_cast<char*>(&none.text.begin) != body)
            This<void>(addr::SC4String_AssignRange, body, none.text.begin, none.text.end, allocator);
        Destroy(none.text);
    }
}

// The game's 0x004CFB30 (stdcall (lot, text), ret 8; called with the tool
// in ECX, not read; caller 0x004D0190): the lot's trips (lot slot 4, its
// traffic source) with 8 and not 2 in their flags, counted by their route
// kinds as the record's +0x30 gives them (bit 0 dropped when one of 0x1CC
// is set), as text. Nothing without a lot or a source.
void __stdcall LotRouteCounts(void* lot, void* text)
{
    if (!lot)
        return;
    void* const source = Virtual<void*>(lot, 0x10);
    if (!source)
        return;
    void** const sentinel = *Virtual<void***>(source, 0xC);
    uint32_t sums[kRouteKindCount] = {};
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        const uint8_t flags = At<uint8_t>(node, trip_node::kFlags);
        if (!(flags & 8) || (flags & 2))
            continue;
        uint32_t kinds = At<uint32_t>(node, 0x30);
        if (kinds & kRouteKindsAll)
            kinds &= ~uint32_t{1};
        for (uint32_t i = 0; i < kRouteKindCount; ++i) {
            if (kinds & (uint32_t{1} << i))
                sums[i] += At<uint32_t>(node, 0x20);
        }
    }
    AppendCounts(text, sums, &text);
}

// The game's 0x004CFD20 (thiscall (box, text, mode), ret 0xC; caller
// 0x004D0190): the trips of every lot (the lot manager's slot 15 iterator,
// never given back) and every neighbour connection (the neighbours
// simulator's slot 16 into a list of the allocator 0x00B207DC, emptied
// releasing and freed after) that count for the box (CountSource), as
// text.
void __fastcall BoxRouteCounts(char* self, void*, const int32_t* box, void* text, uint32_t mode)
{
    uint32_t sums[kRouteKindCount] = {};
    void** const iterator = Virtual<void**>(Global<void*>(addr::SC4_LotManager), 0x3C);
    for (;;) {
        void** const owner = static_cast<void**>(iterator[1]);
        if (!owner)
            break;
        void** const node = static_cast<void**>(iterator[0]);
        if (node == owner[0])
            break;
        iterator[0] = node[0];
        void* const source = Virtual<void*>(node[2], 0x10);
        if (source)
            CountSource(self, source, box, mode, sums);
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
            CountSource(self, source, box, mode, sums);
    }
    uint8_t allocator = 0;
    AppendCounts(text, sums, &allocator);
    This<void>(addr::SC4List_ClearReleasing, &list);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, list);
}

// --- The route view's title -----------------------------------------------------------------

// The game's 0x004D0190 (thiscall (title, text, icon), ret 0xC; caller
// DoMessage 0x004D7160): for the building at +0x2C with a lot (lot manager
// slot 14; the lot's slot 59 stands for it when set) and the purposes
// property 0xAA1DD396 (a uint32 array, value type 0x8006), the route view:
// route mode 1 (+0x98), the box of the lot (lot slot 14), then by the
// first purpose found in the order 0x1000, 0x1001, 0x1002, 0x1004, 0x1005,
// 0x1003, 0x150A / 0x150B a title (game string), an icon and the route
// counts (0x004CFB30 for the lot, 0x004CFD20 for the box). For 0x1003
// (route mode 3) the last argument of 0x004CFD20 is the check box
// 0xEBB0F666 of the window at +0x48 in the low byte of the dword that held
// the icon pointer, as in the game.
void __fastcall RouteSummary(char* self, void*, void* title, void* text, uint32_t* icon)
{
    void* const occupant = At<void*>(self, field::kOccupant);
    void* const holder = Virtual<void*>(occupant, 0x18);
    void* lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (!lot)
        return;
    void* const property = Virtual<void*>(holder, 0x24, kPurposesProperty);
    if (!property)
        return;
    void* value = nullptr;
    if (!Virtual<uint8_t>(property, 0x0, kValueIid, &value))
        return;
    if (Virtual<uint32_t>(value, 0xC) != kUint32Array)
        return;
    const uint32_t count = Virtual<uint32_t>(value, 0x10);
    const uint32_t* const purposes = Virtual<const uint32_t*>(value, 0x19C);
    if (Virtual<void*>(lot, 0xEC))
        lot = Virtual<void*>(lot, 0xEC);
    At<uint32_t>(self, field::kRouteMode) = 1;
    int32_t box[4] = {};
    Virtual<uint8_t>(lot, 0x38, static_cast<int32_t*>(box));
    const auto has = [&](uint32_t purpose) {
        for (uint32_t i = 0; i < count; ++i) {
            if (purposes[i] == purpose)
                return true;
        }
        return false;
    };
    const auto counts = [&](uint32_t mode) {
        This<void>(addr::SC4QueryTool_BoxRouteCounts, self, static_cast<int32_t*>(box), text, mode);
    };
    if (has(0x1000)) {
        SetTitle(title, 0x0BAAB7FF);
        *icon = 0x14416208;
        reinterpret_cast<void(__stdcall*)(void*, void*)>(addr::SC4QueryTool_LotRouteCounts)(lot, text);
        return;
    }
    if (has(0x1001)) {
        SetTitle(title, 0x4BAAB823);
        *icon = 0x14416202;
        counts(0);
        return;
    }
    if (has(0x1002)) {
        SetTitle(title, 0x0BAAB82D);
        *icon = 0x14416206;
        counts(0);
        return;
    }
    if (has(0x1004)) {
        SetTitle(title, 0x4BAAB823);
        *icon = 0x1441620A;
        counts(0);
        return;
    }
    if (has(0x1005)) {
        uint32_t picked = 0x14416200;
        if (has(0x1500))
            picked = 0x14416207;
        else if (has(0x1502))
            picked = 0x14416203;
        else if (has(0x1503) || has(0x1504))
            picked = 0x14416201;
        else if (has(0x1505) || has(0x1506))
            picked = 0x14416201;
        else if (has(0x1507))
            picked = 0x14416201;
        else if (has(0x150A))
            picked = 0x1441620C;
        else if (has(0x150B))
            picked = 0x1441620C;
        else if (has(0x1006))
            picked = 0x14416204;
        *icon = picked;
        SetTitle(title, 0x4BAAB823);
        counts(0);
        return;
    }
    if (has(0x1003)) {
        At<uint32_t>(self, field::kRouteMode) = 3;
        *icon = 0x14416209;
        const uint8_t all = Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, At<void*>(self, field::kPanel), kAllRoutes);
        const uint32_t mode =
            (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(icon)) & 0xFFFFFF00u) | uint32_t{all};
        if (has(0x1301) || has(0x1302) || has(0x1303) || has(0x1307) || has(0x1304)) {
            SetTitle(title, 0xABAAB83B);
            counts(mode);
            return;
        }
        if (has(0x1300)) {
            SetTitle(title, 0xCBAAB854);
            counts(mode);
            return;
        }
        counts(mode);
        if (!BodyEmpty(BodyOf(text))) {
            SetTitle(title, 0x4BAAB823);
            *icon = 0x14416209;
        }
        return;
    }
    if (has(0x150A) || has(0x150B)) {
        counts(0);
        if (!BodyEmpty(BodyOf(text))) {
            SetTitle(title, 0x4BAAB823);
            *icon = 0x1441620C;
        }
    }
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery5a(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[12] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_RunEdgeWindow, reinterpret_cast<void*>(&RunEdgeWindow)},
        {addr::SC4QueryTool_RunCellWindow, reinterpret_cast<void*>(&RunCellWindow)},
        {addr::SC4QueryTool_FillOccupantCityMayorRating, reinterpret_cast<void*>(&FillOccupantCityMayorRating)},
        {addr::SC4QueryTool_FillMySimPortrait, reinterpret_cast<void*>(&FillMySimPortrait)},
        {addr::SC4QueryTool_NetworkName, reinterpret_cast<void*>(&NetworkName)},
        {addr::SC4QueryTool_LotRouteCounts, reinterpret_cast<void*>(&LotRouteCounts)},
        {addr::SC4QueryTool_BoxRouteCounts, reinterpret_cast<void*>(&BoxRouteCounts)},
        {addr::SC4QueryTool_RouteSummary, reinterpret_cast<void*>(&RouteSummary)},
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
    "ui.tools.query5a",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_RunEdgeWindow,
    "Our query tool, fifth piece, first half: the windows by cell, fillers 20 and 91, network names and route "
    "counts (docs/tools.md)",
    false,
    InstallQuery5a,
});

} // namespace
} // namespace opensc4::tool_query_5a
