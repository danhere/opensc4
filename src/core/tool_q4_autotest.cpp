// The query tool's part of the autotest, fourth piece (src/core/
// autotest_hooks.h; tool_query_4a.cpp, tool_query_4b.cpp): calls the
// functions of 0x004CA460 to 0x004CC6E0 and hashes what comes out, so that a
// run with ui.tools.query4a/4b on and one with them off can be compared.
// Only the game's addresses and vtable slots are called, never our own
// functions.
//
//   OPENSC4_TEST_Q4=<day>    token "query4", sub-hashes on the log lines
//     "autotest: query4: ...". Everything here leaves the city as it was
//     (highlights are put back; nothing here changes the city).
//     OPENSC4_TEST_Q4_PARTS picks the parts (default all, 63):
//
//   1   the fillers by cell: 0x004CC240 for its 22 control IDs and one it
//       does not know, with fill 1 and 0, at a few cells inside the city,
//       at the sample's lots and at every fourth cell just outside the
//       edges with the connection types 0 to 12, on the stand-in window;
//       the two walks 0x004CC660 and 0x004CC6A0 on a stand-in whose slot 32
//       records what it is given.
//   2   fillers 18 and 107 through 0x004D3A90 for the sample's buildings,
//       18 without an occupant; 0x004CADE0 at the lots' cells and a cell
//       without a lot; 0x004CC160; 0x004CB740 with a holder without the
//       curve property.
//   4   the tool of kind 1 as the view makes it (command 0x6A935CF4):
//       OnMouseMove (slot 20) over a grid of screen points and once off
//       the screen, then OnMouseExit (slot 22).
//   8   the routes on that tool: the occupant set to a few buildings and
//       network pieces, 0x004CAC50 (box, mode, the line, its width and its
//       number of points); the panel target's handler 0x004CB900 with the
//       messages of its window; 0x004CAC30.
//   16  0x004CACC0 with 7, then 0, on a building's lot (its occupants'
//       highlights before, between and after).
//   32  0x004CB8B0 on a three-node tree made for it (not hashed).
//   The tool is put away by the view (slot 0x68: Shutdown) at the end.
//
// Kept out of the hashes: every pointer (only whether it is set).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
    void Text(const std::string& text)
    {
        Add(static_cast<uint32_t>(text.size()));
        Bytes(text.data(), text.size());
    }
};

char g_token[48];

namespace base = tool_query::base;
namespace field = tool_query::field;

constexpr uint32_t kTextControl = 0x212CDC1F, kNumberControl = 0x21325207, kValueControl = 0xC12CEA13;
constexpr uint32_t kTypeNetwork = 0xC772BF98;
constexpr uint32_t kCellIds[] = {0x1011, 0x1012, 0x1013, 0x1014, 0x1015, 0x1026, 0x1027, 0x1028,
                                 0x104B, 0x104C, 0x104D, 0x104E, 0x1066, 0x1067, 0x1068, 0x1069,
                                 0x106A, 0x1078, 0x107B, 0x107C, 0x107D, 0x107E, 0x1010};
constexpr uint32_t kEdgeIds[] = {0x104B, 0x104C, 0x104D, 0x104E, 0x1066, 0x1067, 0x1068, 0x1069, 0x106A};

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

std::string TextOf(const void* text)
{
    if (!text)
        return "(null)";
    const char* const begin = At<const char*>(const_cast<void*>(text), 0x4);
    const char* const end = At<const char*>(const_cast<void*>(text), 0x8);
    return begin && end >= begin ? std::string(begin, end) : std::string("?");
}

// The city view, as tool_bulldoze_autotest.cpp finds it.
char* CityView()
{
    constexpr uint32_t kWinSC4App = 0x6104489A;
    constexpr uint32_t kWinView3D = 0x9A47B417;
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    cIGZWin* const parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
    cRZAutoRefCount<cISC4View3DWin> view;
    if (parent)
        parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid());
    return reinterpret_cast<char*>(static_cast<cISC4View3DWin*>(view));
}

// --- The stand-ins ---------------------------------------------------------------------------

struct Fake {
    void** vtable;
    int32_t references;
};

void* g_windowVtable[128];
void* g_textVtable[8];
void* g_numberVtable[12];
void* g_walkVtable[40];
void* g_holderVtable[12];
Fake g_window, g_text, g_number, g_walk, g_holder;
Fnv g_events;
std::string g_caption;
uint32_t g_walkIid = 0;
std::uintptr_t g_walkFunction = 0;
const void* g_walkData = nullptr;
int32_t g_walkCalls = 0;

uint32_t __fastcall FakeAddRef(Fake* self, void*)
{
    return static_cast<uint32_t>(++self->references);
}

uint32_t __fastcall FakeRelease(Fake* self, void*)
{
    return static_cast<uint32_t>(--self->references);
}

void* __fastcall FakeSelf(Fake* self, void*)
{
    return self;
}

void* __fastcall FakeWindowOf(Fake*, void*)
{
    return &g_window;
}

uint8_t __fastcall FakeGetChildAs(Fake*, void*, uint32_t id, uint32_t iid, void** out)
{
    g_events.Add(uint8_t{0x25});
    g_events.Add(id);
    g_events.Add(iid);
    Fake* child = nullptr;
    if (iid == kTextControl)
        child = &g_text;
    else if (iid == kNumberControl || iid == kValueControl)
        child = &g_number;
    if (!child)
        return 0;
    ++child->references;
    *out = child;
    return 1;
}

uint8_t __fastcall FakeGetProperty(Fake*, void*, uint32_t id, void**)
{
    g_events.Add(uint8_t{0x71});
    g_events.Add(id);
    return 0;
}

uint8_t __fastcall FakeSetProperty(Fake*, void*, uint32_t id, void*)
{
    g_events.Add(uint8_t{0x72});
    g_events.Add(id);
    return 1;
}

void __fastcall FakeSetCaption(Fake*, void*, const void* text)
{
    g_caption = TextOf(text);
    g_events.Add(uint8_t{0x04});
    g_events.Text(g_caption);
}

int32_t __fastcall FakeGetNumber(Fake*, void*)
{
    g_events.Add(uint8_t{0x05});
    return 0;
}

void __fastcall FakeNumberSet(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x14});
    g_events.Add(value);
}

uint8_t __fastcall FakeWalk(Fake*, void*, uint32_t iid, std::uintptr_t function, const void* data)
{
    g_walkIid = iid;
    g_walkFunction = function;
    g_walkData = data;
    ++g_walkCalls;
    return 1;
}

void* __fastcall FakeNoProperty(Fake*, void*, uint32_t id)
{
    g_events.Add(uint8_t{0x09});
    g_events.Add(id);
    return nullptr;
}

void SetUpFakes()
{
    void* const addRef = reinterpret_cast<void*>(&FakeAddRef);
    void* const release = reinterpret_cast<void*>(&FakeRelease);
    for (void** table : {g_windowVtable, g_textVtable, g_numberVtable, g_walkVtable, g_holderVtable}) {
        table[1] = addRef;
        table[2] = release;
    }
    g_windowVtable[3] = reinterpret_cast<void*>(&FakeSelf);
    g_windowVtable[37] = reinterpret_cast<void*>(&FakeGetChildAs);
    g_windowVtable[113] = reinterpret_cast<void*>(&FakeGetProperty);
    g_windowVtable[114] = reinterpret_cast<void*>(&FakeSetProperty);
    g_textVtable[4] = reinterpret_cast<void*>(&FakeSetCaption);
    g_numberVtable[3] = reinterpret_cast<void*>(&FakeWindowOf);
    g_numberVtable[4] = reinterpret_cast<void*>(&FakeGetNumber);
    g_numberVtable[5] = reinterpret_cast<void*>(&FakeNumberSet);
    g_walkVtable[32] = reinterpret_cast<void*>(&FakeWalk);
    g_holderVtable[9] = reinterpret_cast<void*>(&FakeNoProperty);
    g_window = {g_windowVtable, 0};
    g_text = {g_textVtable, 0};
    g_number = {g_numberVtable, 0};
    g_walk = {g_walkVtable, 0};
    g_holder = {g_holderVtable, 0};
}

// --- The city's occupants -------------------------------------------------------------------

struct Building {
    void* lot;
    void* occupant;
    int32_t x, z;
};

struct Sample {
    std::vector<Building> buildings; // up to 24, every third cell
    std::vector<void*> networks;      // up to 3 network pieces, referenced by the test
};

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    std::vector<void*> lots;
    for (int32_t z = 0; z < sizeZ; z += 3) {
        for (int32_t x = 0; x < sizeX; x += 3) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            if (!occupant || std::find(lots.begin(), lots.end(), lot) != lots.end())
                continue;
            lots.push_back(lot);
            if (out.buildings.size() < 24)
                out.buildings.push_back({lot, occupant, x, z});
        }
    }
    // Network pieces: the occupant manager's slot 41 over the whole city.
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    int32_t first[2] = {0, 0};
    int32_t last[2] = {sizeX - 1, sizeZ - 1};
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    Virtual<void>(occupants, 0xA4, &list, static_cast<int32_t*>(first), static_cast<int32_t*>(last), uint32_t{0},
                  uint32_t{0xFFFFFFFF});
    void** const end = static_cast<void**>(list);
    int32_t seen = 0;
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        if (Virtual<uint32_t>(occupant, 0x1C) != kTypeNetwork)
            continue;
        if (++seen % 37 != 1 || out.networks.size() >= 3)
            continue;
        Virtual<uint32_t>(occupant, 0x4);
        out.networks.push_back(occupant);
    }
    Virtual<void>(occupants, 0xC0, &list);
    This<void>(addr::SC4List_Destroy2, &list);
}

std::string Counts(const Sample& s)
{
    char text[120];
    std::snprintf(text, sizeof text, "%d buildings, %d network pieces", static_cast<int32_t>(s.buildings.size()),
                  static_cast<int32_t>(s.networks.size()));
    return text;
}

uint32_t Part(const char* name, Fnv& f, const char* more)
{
    f.Add(g_events.h);
    log::Write("autotest: query4: %s %08X (%s)", name, f.h, more);
    return f.h;
}

// --- 1: the fillers by cell -------------------------------------------------------------------

void Cell(Fnv& f, uint32_t id, uint32_t fill, int32_t x, int32_t z, int32_t type, int32_t& said)
{
    int32_t query[4] = {x, z, 0, type};
    g_caption.clear();
    const uint8_t answer =
        Cdecl<uint8_t>(addr::SC4QueryTool_FillCell, &g_window, id, fill, static_cast<int32_t*>(query));
    f.Add(answer);
    f.Text(g_caption);
    if (!g_caption.empty())
        ++said;
}

uint32_t DriveCells(const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    g_events = Fnv();
    int32_t said = 0;
    std::vector<std::pair<int32_t, int32_t>> cells = {{0, 0}, {sizeX / 2, sizeZ / 3}, {-5, -5}, {sizeX + 3, 2}};
    for (std::size_t i = 0; i < s.buildings.size() && i < 8; ++i)
        cells.push_back({s.buildings[i].x, s.buildings[i].z});
    for (const auto& c : cells)
        for (const uint32_t id : kCellIds)
            for (const uint32_t fill : {1u, 0u})
                Cell(f, id, fill, c.first, c.second, 0, said);
    int32_t edges = 0;
    const auto edge = [&](int32_t x, int32_t z) {
        ++edges;
        for (int32_t type = 0; type <= 12; ++type)
            for (const uint32_t id : kEdgeIds)
                Cell(f, id, 1, x, z, type, said);
    };
    for (int32_t z = 0; z < sizeZ; z += 4) {
        edge(-1, z);
        edge(sizeX, z);
    }
    for (int32_t x = 0; x < sizeX; x += 4) {
        edge(x, -1);
        edge(x, sizeZ);
    }
    // The walks: the child filled (or read) and the walk handed on.
    int32_t query[4] = {0, 0, 0, 0};
    for (const std::uintptr_t walk : {addr::SC4QueryTool_FillCellChildren, addr::SC4QueryTool_ReadCellChildren}) {
        g_walkIid = 0;
        g_walkFunction = 0;
        g_walkData = nullptr;
        g_caption.clear();
        f.Add(Cdecl<uint8_t>(walk, &g_window, uint32_t{0x1012}, &g_walk, static_cast<int32_t*>(query)));
        f.Add(g_walkIid);
        f.Add(uint8_t{g_walkFunction == walk});
        f.Add(uint8_t{g_walkData == query});
        f.Text(g_caption);
    }
    f.Add(g_walkCalls);
    char more[120];
    std::snprintf(more, sizeof more, "%d captions, %d cells inside, %d edge cells, %d walks", said,
                  static_cast<int32_t>(cells.size()), edges, g_walkCalls);
    return Part("cells", f, more);
}

// --- 2: the occupant fillers ------------------------------------------------------------------

uint32_t DriveOccupants(const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    g_events = Fnv();
    int32_t said = 0;
    for (const Building& b : s.buildings) {
        for (const uint32_t index : {18u, 107u}) {
            for (const uint32_t fill : {1u, 0u}) {
                g_caption.clear();
                f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, 0x1000 | index, fill, b.occupant));
                f.Text(g_caption);
                said += !g_caption.empty();
            }
        }
        g_caption.clear();
        f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillLotNetworkNumber, &g_window, uint32_t{0x1012}, uint32_t{1}, b.x,
                             b.z));
        f.Text(g_caption);
    }
    g_caption.clear();
    f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillOccupantLotNetworkNumber, &g_window, uint32_t{0x1012}, uint32_t{1},
                         static_cast<void*>(nullptr)));
    f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillLotNetworkNumber, &g_window, uint32_t{0x1012}, uint32_t{1},
                         sizeX + 5, sizeZ + 5));
    f.Text(g_caption);
    std::string rating, identity;
    for (const uint32_t fill : {1u, 0u}) {
        g_caption.clear();
        f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillCityMayorRating, &g_window, uint32_t{0x1014}, fill, int32_t{0},
                             int32_t{0}));
        f.Text(g_caption);
        if (fill)
            rating = g_caption;
        g_caption.clear();
        f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillMayorRatingCurve, &g_window, uint32_t{0x106B}, fill,
                             static_cast<void*>(&g_holder), uint32_t{0xAA60E745}));
        f.Text(g_caption);
        if (fill)
            identity = g_caption;
    }
    char more[160];
    std::snprintf(more, sizeof more, "%d captions; mayor rating '%s', through the plain curve '%s'", said,
                  rating.c_str(), identity.c_str());
    return Part("occupants", f, more);
}

// --- 4, 8, 16: the tool -------------------------------------------------------------------------

// The fields OnMouseMove and the routes leave.
void HashTool(Fnv& f, char* tool)
{
    void* const occupant = At<void*>(tool, field::kOccupant);
    f.Add(uint8_t{occupant != nullptr});
    if (occupant)
        f.Add(Virtual<uint32_t>(occupant, 0x1C));
    f.Add(At<uint32_t>(tool, field::kField40));
    f.Bytes(tool + field::kPlace, 0xC);
    f.Add(At<uint8_t>(tool, field::kFlag88));
    f.Add(At<uint8_t>(tool, field::kFlag78));
    f.Add(At<uint32_t>(tool, field::kRouteMode));
    f.Add(uint8_t{At<void*>(tool, field::kObject9C) != nullptr});
}

// The route line: its width and how many points it holds (a list with its
// sentinel at +0x14).
int32_t HashLine(Fnv& f, char* tool)
{
    char* const line = At<char*>(tool, field::kObject9C);
    f.Add(uint8_t{line != nullptr});
    if (!line)
        return -1;
    f.Add(At<uint32_t>(line, tool_query::route_line::kWidth));
    void** const sentinel = At<void**>(line, tool_query::route_line::kPoints);
    int32_t points = 0;
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel && points < 1000000;
         node = static_cast<void**>(node[0]))
        ++points;
    f.Add(points);
    return points;
}

// The occupant at +0x2C replaced by `occupant` (referenced), its own
// highlight at +0x40, as OnMouseMove leaves it.
void SetOccupant(char* tool, void* occupant)
{
    if (void* const old = At<void*>(tool, field::kOccupant))
        Virtual<uint32_t>(old, 0x8);
    At<void*>(tool, field::kOccupant) = occupant;
    if (occupant) {
        Virtual<uint32_t>(occupant, 0x4);
        At<uint32_t>(tool, field::kField40) = Virtual<uint32_t>(occupant, 0x40);
    } else {
        At<uint32_t>(tool, field::kField40) = 0;
    }
}

uint32_t DriveMoves(char* tool)
{
    Fnv f;
    g_events = Fnv();
    int32_t points = 0, found = 0, picked = 0;
    for (int32_t z = 60; z <= 700; z += 40) {
        for (int32_t x = 40; x <= 980; x += 60) {
            ++points;
            f.Add(Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0}));
            HashTool(f, tool);
            found += At<void*>(tool, field::kOccupant) != nullptr;
            picked += At<uint8_t>(tool, field::kFlag88) != 0;
        }
    }
    f.Add(Virtual<uint8_t>(tool, 0x50, int32_t{-1000}, int32_t{-1000}, uint32_t{0}));
    HashTool(f, tool);
    f.Add(Virtual<uint8_t>(tool, 0x58));
    HashTool(f, tool);
    char more[120];
    std::snprintf(more, sizeof more, "%d points, an occupant at %d, picked at %d", points, found, picked);
    return Part("moves", f, more);
}

uint32_t DriveRoutes(char* tool, const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    std::string counts;
    const auto route = [&](void* occupant) {
        SetOccupant(tool, occupant);
        f.Add(This<uint8_t>(addr::SC4QueryTool_ShowRoutes, tool));
        f.Bytes(tool + field::kBox, 0x10);
        HashTool(f, tool);
        char text[24];
        std::snprintf(text, sizeof text, "%d ", HashLine(f, tool));
        counts += text;
    };
    for (std::size_t i = 0; i < s.buildings.size() && i < 6; ++i)
        route(s.buildings[i].occupant);
    for (void* const network : s.networks)
        route(network);
    // The window of kind 1 and its messages.
    void* const panel = At<void*>(tool, field::kPanel);
    f.Add(uint8_t{panel != nullptr});
    if (panel && !s.buildings.empty()) {
        route(s.buildings[0].occupant);
        char target[tool_query::panel_target::kSize] = {};
        At<char*>(target, tool_query::panel_target::kTool) = tool;
        const auto send = [&](uint32_t a, uint32_t b, uint32_t c) {
            uint32_t message[4] = {a, b, c, 0};
            f.Add(This<uint8_t>(addr::SC4QueryPanelTarget_OnMessage, target, panel, static_cast<uint32_t*>(message)));
            f.Add(Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, panel, uint32_t{0xABB0F65F}));
            f.Add(Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, panel, uint32_t{0xEBB0F666}));
            f.Add(Virtual<uint8_t>(panel, 0x11C));
            if (void* const other = At<void*>(tool, field::kPanelOther))
                f.Add(Virtual<uint8_t>(other, 0x11C));
            char text[24];
            std::snprintf(text, sizeof text, "m%d ", HashLine(f, tool));
            counts += text;
        };
        send(0xA2BF8AD5, 0, 0);
        send(3, 0x287259F6, 0xEBB0F666);
        send(3, 0x287259F6, 0xABB0F65F);
        send(3, 0x287259F6, 0x4BB0F628);
        send(3, 0x287259F6, 0xEBB91356);
        send(3, 0x287259F6, 0x4BB92C1F);
        send(3, 0x287259F6, 0x12345678);
        This<void>(addr::SC4QueryTool_RebuildRoutes, tool);
        HashLine(f, tool);
    }
    return Part("routes", f, counts.c_str());
}

uint32_t DriveHighlight(char* tool, const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    if (s.buildings.empty())
        return Part("highlight", f, "no building");
    SetOccupant(tool, s.buildings[0].occupant);
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    Virtual<void>(s.buildings[0].lot, 0xC4, &list);
    const auto read = [&]() {
        void** const end = static_cast<void**>(list);
        for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0]))
            f.Add(Virtual<uint32_t>(node[2], 0x40));
    };
    int32_t occupants = 0;
    for (void** node = static_cast<void**>(static_cast<void**>(list)[0]); node != list;
         node = static_cast<void**>(node[0]))
        ++occupants;
    read();
    This<void>(addr::SC4QueryTool_HighlightLot, tool, uint32_t{7});
    read();
    This<void>(addr::SC4QueryTool_HighlightLot, tool, uint32_t{0});
    read();
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &list);
    This<void>(addr::SC4List_Destroy2, &list);
    char more[60];
    std::snprintf(more, sizeof more, "%d occupants on the lot", occupants);
    return Part("highlight", f, more);
}

// --- 32: the text map's erase ------------------------------------------------------------------

void DriveErase()
{
    using namespace tool_query;
    char* nodes[3] = {};
    for (char*& node : nodes) {
        node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x28});
        std::memset(node, 0, 0x28);
        At<std::uintptr_t>(node, text_node::kText) = addr::SC4String_Vtable;
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        *buffer = 0;
        At<char*>(node, text_node::kTextBuffer) = buffer;
    }
    At<char*>(nodes[0], text_node::kLeft) = nodes[1];
    At<char*>(nodes[0], text_node::kRight) = nodes[2];
    This<void>(addr::SC4QueryTool_EraseTexts, nodes[0], nodes[0]);
    log::Write("autotest: query4: erase done");
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q4") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query4: no city view");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_Q4_PARTS", 63);
    SetUpFakes();
    const int32_t sizeX = static_cast<int32_t>(day.city->CellCountX());
    const int32_t sizeZ = static_cast<int32_t>(day.city->CellCountZ());
    Sample sample;
    Collect(sample, sizeX, sizeZ);
    const std::string counts = Counts(sample);
    log::Write("autotest: query4: %s", counts.c_str());
    Fnv total;
    total.Text(counts);
    if (parts & 1)
        total.Add(DriveCells(sample, sizeX, sizeZ));
    if (parts & 2)
        total.Add(DriveOccupants(sample, sizeX, sizeZ));
    if (parts & (4 | 8 | 16)) {
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
        Virtual<uint8_t>(view, 0x1C, uint32_t{0x6A935CF4}, static_cast<void*>(nullptr), uint32_t{0});
        char* const tool = Virtual<char*>(view, 0x60);
        const bool ours = tool && At<std::uintptr_t>(tool, 0) == addr::SC4QueryTool_Vtable;
        total.Add(uint8_t{ours});
        if (ours) {
            Virtual<uint32_t>(tool, 0x4);
            if (parts & 4)
                total.Add(DriveMoves(tool));
            if (parts & 8)
                total.Add(DriveRoutes(tool, sample));
            if (parts & 16)
                total.Add(DriveHighlight(tool, sample));
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
            Fnv f;
            HashTool(f, tool);
            f.Add(uint8_t{At<void*>(tool, field::kTip) != nullptr});
            f.Add(uint8_t{At<void*>(tool, field::kPanel) != nullptr});
            total.Add(f.h);
            total.Add(Virtual<uint32_t>(tool, 0x8));
            log::Write("autotest: query4: put away %08X", f.h);
        } else {
            log::Write("autotest: query4: no query tool from the view");
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
        }
    }
    if (parts & 32)
        DriveErase();
    for (void* const network : sample.networks)
        Virtual<uint32_t>(network, 0x8);
    total.Add(g_window.references);
    total.Add(g_text.references);
    total.Add(g_number.references);
    std::snprintf(g_token, sizeof g_token, " query4 %08X", total.h);
    log::Write("autotest: query4: total %08X (references %d %d %d)", total.h, g_window.references,
               g_text.references, g_number.references);
}

const DayHook kQuery4{"query4", 64, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query4", 24, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
