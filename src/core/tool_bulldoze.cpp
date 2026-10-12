// The bulldoze tool (cSC4ViewInputControlDemolish, vtable 0x00A901A0, 0xD8
// bytes, ID 0x46DDB5F1, code 0x004B8830 to 0x004B9D56), on the view input
// control base class. Replacement, off by default:
//
//   ui.tools.bulldoze: the constructor and destructors, Init and Shutdown,
//     the key and mouse handlers, the cell and occupant picking, the
//     preview's overlay cell list.
//
// A press on the terrain starts a drag; the drag runs from the start cell
// (+0x58, +0x5C) to the end cell (+0x60, +0x64). The preview (Preview, run
// by every press and move that changes a cell) builds the cell region of
// the drag (+0x68), asks the demolition service for the price without
// demolishing (DemolishRegion with demolish 0) and paints the overlay (the
// colours, the cells, the cost indicator). The release asks the service
// again, for real, takes the price off the budget and puts the tool away.
// The overlay is the game's helper object (0x50 bytes, MapOverlay_*), made
// and fed by calls by address: nothing here draws.
//
// game.bulldoze (game_bulldoze.cpp) wraps four of the functions below at the
// game's entries: Preview, OnMouseMove, EndDrag and IsOccupantShown. They
// are translated here and exported by tool_bulldoze.h, but NOT in this
// group's hook table; the calls among them go through the game's entries, so
// the wrapper sees them. Every DemolishRegion call of the tool goes through
// DemolishRegionCall, which records the original call site.
#include "core/tool_bulldoze.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace opensc4::tool_bulldoze {

// The four functions game.bulldoze wraps, defined below the helpers; external
// so that their addresses can be handed out (tool_bulldoze.h).
uint8_t __fastcall IsOccupantShown(void* self, void* edx, void* occupant);
void __fastcall EndDrag(char* self, void* edx);
void __fastcall Preview(char* self, void* edx);
uint8_t __fastcall OnMouseMove(char* self, void* edx, int32_t x, int32_t z, uint32_t modifiers);

namespace {

namespace field { // from the tool's start; up to +0x24 the base class
constexpr std::size_t kInited = 0x04;     // byte, set by the base Init
constexpr std::size_t kId = 0x0C;         // two dwords: the tool's ID
constexpr std::size_t kView = 0x1C;       // the city view (cISC4View3DWin)
constexpr std::size_t kBudget = 0x28;     // the budget simulator (referenced)
constexpr std::size_t kCity = 0x2C;       // the city (referenced)
constexpr std::size_t kDemolition = 0x30; // the demolition service (global 0x00B43D00, referenced)
constexpr std::size_t kDeveloper = 0x34;  // the lot developer (global 0x00B43D68 slot 0x90, referenced)
constexpr std::size_t kLots = 0x38;       // the lot manager (referenced)
constexpr std::size_t kFilter = 0x3C;     // the tool's occupant filter (0xC bytes, made by Init, referenced)
constexpr std::size_t kNetwork = 0x40;    // the occupant manager (global 0x00B43D0C, referenced)
constexpr std::size_t kDragging = 0x44;   // byte: a press is held
constexpr std::size_t kCell = 0x48;       // four ints: the rectangle of the cell under the cursor
constexpr std::size_t kStart = 0x58;      // the drag's start cell (x, z)
constexpr std::size_t kEnd = 0x60;        // the drag's end cell (x, z)
constexpr std::size_t kRegion = 0x68;     // the cell region of the drag (0x24 bytes)
constexpr std::size_t kAllowed = 0x6C;    // byte: the service says the demolition can be done
constexpr std::size_t kHovered = 0x70;    // the occupant under the cursor (referenced)
constexpr std::size_t kLit = 0x74;        // the occupant given the highlight (referenced)
constexpr std::size_t kLitOld = 0x78;     // its highlight before ours
constexpr std::size_t kSet = 0x7C;        // header node of a std::set of referenced objects (never filled here)
constexpr std::size_t kSetCount = 0x80;
constexpr std::size_t kCost = 0x88;       // int64: the price the service gave
constexpr std::size_t kOverlay = 0x90;    // the MapOverlay helper (0x50 bytes, referenced)
constexpr std::size_t kSingle = 0x94;     // byte: the hovered occupant is one the tool takes alone
constexpr std::size_t kColors = 0x98;     // 16 dwords: four colours of four, see below
constexpr std::size_t kFillAllowed = 0xB8;
constexpr std::size_t kLineAllowed = 0x98;
constexpr std::size_t kFillDenied = 0xC8;
constexpr std::size_t kLineDenied = 0xA8;
} // namespace field

constexpr uint32_t kToolIdLow = 0x46DDB5F1, kToolIdHigh = 0xE1855ADC;
constexpr uint32_t kTypeFlora = 0x74758926, kTypeBuilding = 0x278128A0, kTypeConstruction = 0x2890D4DE;
constexpr uint32_t kTypeProp = 0xA823821E, kTypeNetwork = 0xC772BF98;

struct Rect {
    int32_t x0, z0, x1, z1;
};

bool g_wrappedOurs[4] = {};
std::uintptr_t g_callSite = 0;

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

// Every DemolishRegion call of the tool (slot 0x18 of the service at +0x30,
// eleven arguments): the original's call sites are 0x004B97F1 (Preview, with
// demolish 0) and 0x004B9D04 (OnMouseUpL, with demolish 1). game.bulldoze
// knows the tool's calls by the return address; InToolCall and ToolCallSite
// tell it ours.
uint8_t DemolishRegionCall(char* self, std::uintptr_t site, uint32_t demolish, void* region, int32_t privilege,
                           uint32_t flags, uint32_t clearZoned, void* filter, int64_t* cost, void* demolished,
                           void* occupant, int32_t a, int32_t b)
{
    const std::uintptr_t previous = g_callSite;
    g_callSite = site;
    const uint8_t result = Virtual<uint8_t>(At<void*>(self, field::kDemolition), 0x18, demolish, region, privilege,
                                            flags, clearZoned, filter, cost, demolished, occupant, a, b);
    g_callSite = previous;
    return result;
}

void DestroyRegion(char* region)
{
    char* const cells = region + 0x10;
    At<std::uintptr_t>(cells, 0) = addr::SC4CellRegion_CellsVtable;
    This<void>(addr::SC4CellRegion_DestroyCells, cells);
    Cdecl<void>(addr::SC4_operator_delete, region);
}

// The game's 0x004B8980 (ret 0xC): the cell under the screen point (x, z).
// Picks the ground (the terrain's slot 0x14C is the flag of the pick) and
// turns the point into a cell by the city's slot 0x17C. False when there is
// no view or no ground there.
uint8_t __fastcall PickCell(char* self, void*, int32_t x, int32_t z, int32_t* out)
{
    void* const view = At<void*>(self, field::kView);
    if (!view)
        return 0;
    const uint32_t flag = Virtual<uint32_t>(Global<void*>(addr::SC4_Terrain), 0x14C);
    float position[3];
    if (!Virtual<uint8_t>(view, 0x90, x, z, position, flag))
        return 0;
    int32_t cellX, cellZ;
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, position[0], position[2], &cellX, &cellZ);
    out[0] = cellX;
    out[1] = cellZ;
    return 1;
}

// The game's 0x004B8A00 (ret 0xC): asks the overlay service's pick (global
// 0x00B43DD0 slot 0x104, with the tool's own filter at +0x3C) what is at the
// screen point; its object's slot 3 gives an occupant, whose interface
// 0x26BB767E goes to `out` and which says in +0x94 whether it also has
// 0x4B44FBE2 (taken alone). True when it found one.
uint8_t __fastcall FindOccupant(char* self, void*, int32_t x, int32_t z, void** out)
{
    At<uint8_t>(self, field::kSingle) = 0;
    uint8_t found = 0;
    if (void* const service = Global<void*>(addr::SC4_View43DD0)) {
        void* picked = nullptr;
        if (Virtual<uint8_t>(service, 0x104, x, z, At<void*>(self, field::kFilter), &picked)) {
            if (void* const occupant = Virtual<void*>(picked, 0xC)) {
                if (Virtual<uint8_t>(occupant, 0x0, uint32_t{0x26BB767E}, out)) {
                    void* alone = nullptr;
                    At<uint8_t>(self, field::kSingle) = Virtual<uint8_t>(occupant, 0x0, uint32_t{0x4B44FBE2}, &alone);
                    found = 1;
                    if (alone)
                        Release(alone);
                }
            }
            Release(picked);
        }
    }
    return found;
}

// The game's 0x004B8D10 (ret 0xC): what the cursor is over. Lets go of the
// last hovered occupant, finds the new one (FindOccupant), takes its position
// (or the ground's under the cursor), the cell of that, and the lot there:
// when the lot can be demolished and its building passes the view's filter
// the rectangle is the lot's; else an occupant's own cell or cells, else the
// one cell. `rect` gets the rectangle. False without a view or without
// ground under the cursor.
uint8_t __fastcall UpdateHover(char* self, void*, int32_t x, int32_t z, int32_t* rect)
{
    void* const view = At<void*>(self, field::kView);
    if (!view)
        return 0;
    if (void* const old = At<void*>(self, field::kHovered)) {
        At<void*>(self, field::kHovered) = nullptr;
        Release(old);
    }
    FindOccupant(self, nullptr, x, z, reinterpret_cast<void**>(self + field::kHovered));
    float position[3];
    if (void* const hovered = At<void*>(self, field::kHovered)) {
        Virtual<void>(hovered, 0x20, position);
    } else {
        const uint32_t flag = Virtual<uint32_t>(Global<void*>(addr::SC4_Terrain), 0x14C);
        if (!Virtual<uint8_t>(At<void*>(self, field::kView), 0x90, x, z, position, flag))
            return 0;
    }
    int32_t cellX, cellZ;
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, position[0], position[2], &cellX, &cellZ);
    void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x18, cellX, cellZ, uint32_t{0});
    void* filter = nullptr;
    Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &filter);

    bool taken = false;
    if (lot && Virtual<uint8_t>(lot, 0x9C)) { // IsInDemolishableState
        bool passes = true;
        void* const building = Virtual<void*>(lot, 0xC0);
        if (building && filter) {
            void* const occupant = Virtual<void*>(building, 0xC);
            passes = Virtual<uint8_t>(filter, 0xC, occupant) != 0;
        }
        if (passes) {
            Virtual<void>(lot, 0x38, rect); // GetBoundingRect
            taken = true;
        }
    }
    if (!taken) {
        if (void* const hovered = At<void*>(self, field::kHovered)) {
            void* cells = nullptr;
            if (Virtual<uint8_t>(hovered, 0x0, uint32_t{0xA8C86142}, &cells)) {
                int32_t a, b;
                Virtual<void>(cells, 0x20, &a, &b);
                rect[0] = a;
                rect[2] = a;
                rect[1] = b;
                rect[3] = b;
                Release(cells);
            } else {
                Virtual<void>(hovered, 0x2C, rect); // GetBoundingCityCells
            }
        } else {
            rect[0] = cellX;
            rect[2] = cellX;
            rect[1] = cellZ;
            rect[3] = cellZ;
        }
    }
    if (filter)
        Release(filter);
    return 1;
}

// The game's 0x004B9250 (ret 4; this is the overlay): adds to the vector of
// 8-byte (x, z) items at +0x10, +0x14 (end), +0x18 (capacity) every cell of
// the region whose bit is set, growing the vector as the game's does (the
// size again, at least one).
void __fastcall AddCells(char* overlay, void*, char* region)
{
    const int32_t* const rect = reinterpret_cast<const int32_t*>(region);
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            const uint32_t i = static_cast<uint32_t>(x - rect[0]), j = static_cast<uint32_t>(z - rect[1]);
            if (i >= At<uint32_t>(region, 0x14) || j >= At<uint32_t>(region, 0x18))
                continue;
            if (!(At<uint32_t**>(region, 0x20)[i][j >> 5] & (1u << (j & 31))))
                continue;
            int32_t*& begin = At<int32_t*>(overlay, 0x10);
            int32_t*& end = At<int32_t*>(overlay, 0x14);
            int32_t*& capacity = At<int32_t*>(overlay, 0x18);
            if (end != capacity) {
                end[0] = x;
                end[1] = z;
                end += 2;
                continue;
            }
            const uint32_t size = static_cast<uint32_t>(end - begin) / 2;
            const uint32_t grow = size < 1 ? 1 : size;
            const uint32_t total = grow + size;
            int32_t* fresh = nullptr;
            if (total)
                fresh = Cdecl<int32_t*>(addr::SC4_stl_allocate, static_cast<std::size_t>(total) * 8);
            int32_t* to = fresh;
            for (int32_t* from = begin; from != end; from += 2, to += 2) {
                if (to) {
                    to[0] = from[0];
                    to[1] = from[1];
                }
            }
            if (to) {
                to[0] = x;
                to[1] = z;
            }
            int32_t* const newEnd = to + 2;
            if (begin)
                Cdecl<void>(addr::SC4_Free, begin);
            begin = fresh;
            end = newEnd;
            capacity = fresh + static_cast<std::size_t>(total) * 2;
        }
    }
}

// Whether a lot at (x, z) counts as touched by the selection and does not
// lie inside it; the lot's rectangle goes to `r`.
bool LotOutside(char* self, void* filter, int32_t x, int32_t z, Rect& r, int32_t minX, int32_t minZ, int32_t maxX,
                int32_t maxZ)
{
    void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x18, x, z, uint32_t{0});
    if (!lot)
        return false;
    if (Virtual<int32_t>(lot, 0x84) <= 1) // GetState: built lots only
        return false;
    void* const building = Virtual<void*>(lot, 0xC0);
    if (building && filter) {
        void* const occupant = Virtual<void*>(building, 0xC);
        if (!Virtual<uint8_t>(filter, 0xC, occupant))
            return false;
    }
    Virtual<void>(lot, 0x38, &r); // GetBoundingRect
    return r.x0 < minX || r.x1 > maxX || r.z0 < minZ || r.z1 > maxZ;
}

void SetBit(char* region, int32_t x, int32_t z)
{
    const int32_t* const rect = reinterpret_cast<const int32_t*>(region);
    uint32_t** const rows = At<uint32_t**>(region, 0x20);
    const uint32_t j = static_cast<uint32_t>(z - rect[1]);
    rows[x - rect[0]][j >> 5] |= 1u << (j & 31);
}

void* MakeRegion(int32_t x0, int32_t z0, int32_t x1, int32_t z1, bool fill)
{
    char* const region = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0x24});
    if (!region)
        return nullptr;
    int32_t* const rect = reinterpret_cast<int32_t*>(region);
    rect[0] = x0;
    rect[1] = z0;
    rect[2] = x1;
    rect[3] = z1;
    uint8_t flag = fill ? 1 : 0;
    This<void*>(addr::SC4CellRegion_ConstructCells, region + 0x10, static_cast<uint32_t>(x1 - x0 + 1),
                static_cast<uint32_t>(z1 - z0 + 1), &flag);
    return region;
}

// The game's 0x004B8830 (ret 8; slot 14): Escape ends a drag held by the
// current tool.
uint8_t __fastcall OnKeyDown(char* self, void*, uint32_t key, uint32_t)
{
    if (This<uint8_t>(addr::SC4ViewInputControl_IsCurrent, self) && key == 0x1B && At<uint8_t>(self, field::kDragging)) {
        Virtual<void>(self, 0x6C); // Cancel
        return 1;
    }
    return 0;
}

// The game's 0x004B9040 (slot 27): lets a held press go (capture, highlight,
// region) and clears the cost indicator.
void __fastcall Cancel(char* self, void*)
{
    if (At<uint8_t>(self, field::kDragging)) {
        At<uint8_t>(self, field::kDragging) = 0;
        This<void>(addr::SC4ViewInputControl_EndCapture, self);
        This<void>(addr::cSC4ViewInputControlDemolish_EndDrag, self);
    }
    Virtual<uint8_t>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
}

// The game's 0x004B9020 (slot 22; the cursor left): clears the cost
// indicator and lets the drag's highlight and region go. False.
uint8_t __fastcall OnSlot22(char* self, void*)
{
    Virtual<uint8_t>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    This<void>(addr::cSC4ViewInputControlDemolish_EndDrag, self);
    return 0;
}

// The game's 0x004B8AB0 (slot 3): the ID, the base Init, then once the
// overlay helper (made, scaled, handed to the overlay service 0x00B43DD0
// with 5 and 1000) and the services the tool holds; the four colours of the
// renderer's settings are copied into +0x98 to +0xD4.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, field::kInited))
        return 1;
    At<uint32_t>(self, field::kId) = kToolIdLow;
    At<uint32_t>(self, field::kId + 4) = kToolIdHigh;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    if (!At<void*>(self, field::kOverlay)) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x50});
        void* const overlay = memory ? This<void*>(addr::SC4UI_MapOverlay_Construct, memory) : nullptr;
        At<void*>(self, field::kOverlay) = overlay;
        AddRef(overlay);
        This<void>(addr::SC4UI_MapOverlay_SetFlagD, At<void*>(self, field::kOverlay), uint32_t{0});
        const uint32_t flag = Virtual<uint32_t>(Global<void*>(addr::SC4_Terrain), 0x14C);
        This<void>(addr::SC4UI_MapOverlay_SetFlagE, At<void*>(self, field::kOverlay), flag);
        This<void>(addr::SC4UI_MapOverlay_Start, At<void*>(self, field::kOverlay));
        This<void>(addr::SC4UI_MapOverlay_SetScale, At<void*>(self, field::kOverlay), 16.0f, 16.0f);
        if (void* const service = Global<void*>(addr::SC4_View43DD0))
            Virtual<void>(service, 0x80, At<void*>(self, field::kOverlay), uint32_t{5}, uint32_t{0x3E8});
        if (void* const picker = Cdecl<void*>(addr::SC4_TerrainPicker))
            Virtual<void>(picker, 0x28, uint32_t{0});
    }
    if (!At<void*>(self, field::kBudget)) {
        void* const service = Global<void*>(addr::SC4_BudgetSimulator);
        At<void*>(self, field::kBudget) = service;
        AddRef(service);
    }
    if (!At<void*>(self, field::kCity)) {
        void* const city = Global<void*>(addr::SC4_City);
        At<void*>(self, field::kCity) = city;
        AddRef(city);
    }
    if (!At<void*>(self, field::kDemolition)) {
        void* const service = Global<void*>(addr::SC4_Service43D00);
        At<void*>(self, field::kDemolition) = service;
        AddRef(service);
    }
    if (!At<void*>(self, field::kDeveloper)) {
        void* const developer = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
        At<void*>(self, field::kDeveloper) = developer;
        AddRef(developer);
    }
    if (!At<void*>(self, field::kLots)) {
        void* const lots = Global<void*>(addr::SC4_LotManager);
        At<void*>(self, field::kLots) = lots;
        AddRef(lots);
    }
    if (!At<void*>(self, field::kFilter)) {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
        char* filter = nullptr;
        if (memory) {
            This<void>(addr::SC4_OccupantFilterBase, memory);
            At<std::uintptr_t>(memory, 0) = addr::cSC4ViewInputControlDemolish_FilterVtable;
            At<std::uintptr_t>(memory, 4) = addr::cSC4ViewInputControlDemolish_FilterVtable4;
            filter = memory;
        }
        At<void*>(self, field::kFilter) = filter;
        AddRef(filter);
    }
    if (!At<void*>(self, field::kNetwork)) {
        void* const network = Global<void*>(addr::SC4_NetworkService);
        At<void*>(self, field::kNetwork) = network;
        AddRef(network);
    }
    void* const renderer = Virtual<void*>(At<void*>(self, field::kView), 0x10);
    char* const settings = At<char*>(Virtual<char*>(renderer, 0x28), 0x54);
    const std::size_t sources[4] = {0x50, 0x60, 0x90, 0xA0};
    const std::size_t targets[4] = {field::kLineAllowed, field::kLineDenied, field::kFillAllowed, field::kFillDenied};
    for (int group = 0; group < 4; ++group)
        for (std::size_t i = 0; i < 4; ++i)
            At<uint32_t>(self, targets[group] + i * 4) = At<uint32_t>(settings, sources[group] + i * 4);
    return 1;
}

// The game's 0x004B8F30 (slot 4): Cancel, EndDrag, the overlay taken off the
// overlay service and let go, every held service let go, the hovered and
// the highlighted occupants let go, the cost indicator cleared, the base
// Shutdown. Only when initialised. True.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, field::kInited))
        return 1;
    Virtual<void>(self, 0x6C);
    This<void>(addr::cSC4ViewInputControlDemolish_EndDrag, self);
    if (void* const overlay = At<void*>(self, field::kOverlay)) {
        if (void* const service = Global<void*>(addr::SC4_View43DD0))
            Virtual<void>(service, 0x84, overlay);
        Release(At<void*>(self, field::kOverlay));
        At<void*>(self, field::kOverlay) = nullptr;
    }
    for (const std::size_t offset : {field::kBudget, field::kCity, field::kDemolition, field::kDeveloper,
                                     field::kLots, field::kFilter, field::kNetwork}) {
        if (void* const held = At<void*>(self, offset)) {
            Release(held);
            At<void*>(self, offset) = nullptr;
        }
    }
    for (const std::size_t offset : {field::kHovered, field::kLit}) {
        if (void* const held = At<void*>(self, offset)) {
            At<void*>(self, offset) = nullptr;
            Release(held);
        }
    }
    Virtual<uint8_t>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004B9070: the constructor (the caller allocated 0xD8 bytes).
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::cSC4ViewInputControlDemolish_Vtable;
    for (const std::size_t offset : {field::kBudget, field::kCity, field::kDemolition, field::kDeveloper,
                                     field::kLots, field::kFilter, field::kNetwork})
        At<void*>(self, offset) = nullptr;
    At<uint8_t>(self, field::kDragging) = 0;
    At<uint32_t>(self, field::kStart) = 0x80000000;
    At<uint32_t>(self, field::kStart + 4) = 0x80000000;
    At<uint32_t>(self, field::kEnd) = 0x80000000;
    At<uint32_t>(self, field::kEnd + 4) = 0x80000000;
    At<void*>(self, field::kRegion) = nullptr;
    At<uint8_t>(self, field::kAllowed) = 1;
    At<void*>(self, field::kHovered) = nullptr;
    At<void*>(self, field::kLit) = nullptr;
    At<uint32_t>(self, field::kLitOld) = 0;
    At<void*>(self, field::kSet) = nullptr;
    char* const head = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{0x14});
    At<void*>(self, field::kSet) = head;
    At<uint32_t>(self, field::kSetCount) = 0;
    At<uint8_t>(head, 0) = 0;
    At<void*>(head, 4) = nullptr;
    At<void*>(head, 8) = head;
    At<void*>(head, 0xC) = head;
    At<uint32_t>(self, field::kCost) = 0;
    At<uint32_t>(self, field::kCost + 4) = 0;
    At<void*>(self, field::kOverlay) = nullptr;
    At<uint8_t>(self, field::kSingle) = 0;
    for (std::size_t offset = field::kColors; offset <= 0xD4; offset += 4)
        At<uint32_t>(self, offset) = Global<uint32_t>(addr::SC4_Global217FC);
    return self;
}

// The game's 0x004B91E0: the destructor body. Empties and frees the set's
// header node, lets go of the hovered and the highlighted occupants (not
// nulled), then the base destructor (the game jumps to it).
void __fastcall Destruct(char* self, void*)
{
    char* const set = self + field::kSet;
    if (At<uint32_t>(self, field::kSetCount)) {
        This<void>(addr::SC4RefSet_EraseSubtree, set, At<void*>(At<char*>(set, 0), 4));
        char* const head = At<char*>(set, 0);
        At<void*>(head, 8) = head;
        At<uint32_t>(At<char*>(set, 0), 4) = 0;
        At<void*>(At<char*>(set, 0), 0xC) = At<char*>(set, 0);
        At<uint32_t>(set, 4) = 0;
    }
    if (char* const head = At<char*>(set, 0))
        Cdecl<void>(addr::SC4_Free, head);
    if (void* const lit = At<void*>(self, field::kLit))
        Release(lit);
    if (void* const hovered = At<void*>(self, field::kHovered))
        Release(hovered);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004B91C0 (ret 4; slot 26): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004B9AE0 (ret 0xC; slot 16): a press. Picks the start cell;
// if there is one the drag starts: the mouse is captured, the hover is
// refreshed, the end cell is the start cell, the set is emptied and the
// preview runs. The click sound is played whatever happened. True.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    int32_t* const start = reinterpret_cast<int32_t*>(self + field::kStart);
    const uint8_t picked = PickCell(self, nullptr, x, z, start);
    At<uint8_t>(self, field::kDragging) = picked;
    if (picked) {
        This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
        UpdateHover(self, nullptr, x, z, reinterpret_cast<int32_t*>(self + field::kCell));
        At<int32_t>(self, field::kEnd) = start[0];
        At<int32_t>(self, field::kEnd + 4) = start[1];
        char* const set = self + field::kSet;
        if (At<uint32_t>(set, 4)) {
            This<void>(addr::SC4RefSet_EraseSubtree, set, At<void*>(At<char*>(set, 0), 4));
            char* const head = At<char*>(set, 0);
            At<void*>(head, 8) = head;
            At<uint32_t>(At<char*>(set, 0), 4) = 0;
            At<void*>(At<char*>(set, 0), 0xC) = At<char*>(set, 0);
            At<uint32_t>(set, 4) = 0;
        }
        This<void>(addr::cSC4ViewInputControlDemolish_Preview, self);
    }
    Virtual<void>(self, 0x70, uint32_t{0xCA5C328B});
    return 1;
}

// The game's 0x004B9C50 (ret 0xC; slot 18): the release. Without a held
// press, false. Else the end cell is picked, the preview runs, and when the
// service said yes the price comes off the budget and the demolishing is
// asked for: for the one hovered occupant if the press did not move
// (DemolishOccupant when the occupant is taken alone, else DemolishRegion
// with the occupant and the screen point), else for the region. The cost
// indicator is cleared, a sound plays (the free-demolition sound when the
// price is zero), the drag ends and the capture is let go. True.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (!At<uint8_t>(self, field::kDragging))
        return 0;
    int32_t* const end = reinterpret_cast<int32_t*>(self + field::kEnd);
    PickCell(self, nullptr, x, z, end);
    This<void>(addr::cSC4ViewInputControlDemolish_Preview, self);
    int64_t* const cost = reinterpret_cast<int64_t*>(self + field::kCost);
    if (At<uint8_t>(self, field::kAllowed)) {
        void* const budget = At<void*>(self, field::kBudget);
        const int64_t balance = Virtual<int64_t>(budget, 0x18);
        Virtual<void>(budget, 0x14, balance - *cost);
        void* const occupant = At<void*>(self, field::kHovered);
        void* const region = At<void*>(self, field::kRegion);
        if (At<int32_t>(self, field::kStart) == end[0] && At<int32_t>(self, field::kStart + 4) == end[1] && occupant) {
            if (At<uint8_t>(self, field::kSingle))
                Virtual<uint8_t>(At<void*>(self, field::kDemolition), 0x20, uint32_t{1}, occupant, int32_t{1},
                                 uint32_t{1}, cost, uint32_t{0}, uint32_t{0});
            else
                DemolishRegionCall(self, 0x004B9D04, 1, region, 1, 1, 0, nullptr, cost, nullptr, occupant, x, z);
        } else {
            DemolishRegionCall(self, 0x004B9D04, 1, region, 1, 1, 0, nullptr, cost, nullptr, nullptr, 0, 0);
        }
        Virtual<uint8_t>(At<void*>(self, field::kView), 0xB0, uint32_t{1});
        if (!*cost)
            Virtual<void>(self, 0x70, uint32_t{0x8A5EC5B8});
    } else {
        Virtual<uint8_t>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
        Virtual<void>(self, 0x70, uint32_t{0x495A6D86});
    }
    This<void>(addr::cSC4ViewInputControlDemolish_EndDrag, self);
    At<uint8_t>(self, field::kDragging) = 0;
    This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    return 1;
}

} // namespace

// --- The four functions game.bulldoze wraps ---------------------------------------------

// The game's 0x004B8880 (ret 4): whether the tool takes this occupant. The
// types: a building, construction, flora or network piece, then (at view
// level 3, a network piece for which the interface 0xA821EF94 says yes to
// 0x1F4F is out) and the occupant must pass CanRemove(2); a prop only when
// its interface 0xE9793A65 has type 0xAB72FBB3. `this` is not read.
uint8_t __fastcall IsOccupantShown(void*, void*, void* occupant)
{
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kTypeProp) {
        void* holder = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, uint32_t{0xE9793A65}, &holder)) {
            if (Virtual<uint32_t>(holder, 0x1C) == 0xAB72FBB3) {
                if (holder)
                    Release(holder);
                return 1;
            }
        }
        if (holder)
            Release(holder);
        return 0;
    }
    if (type != kTypeFlora && type != kTypeBuilding && type != kTypeConstruction && type != kTypeNetwork)
        return 0;
    uint8_t shown = 1;
    if (Virtual<int32_t>(Global<void*>(addr::SC4_Service43DC4), 0x58) == 3) { // GetCurrentViewLevel
        void* piece = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, uint32_t{0xA821EF94}, &piece)) {
            if (Virtual<uint8_t>(piece, 0x24, uint32_t{0x1F4F}))
                shown = 0;
        }
        if (piece)
            Release(piece);
    }
    if (!Virtual<uint8_t>(occupant, 0x38, uint32_t{2})) // CanRemove
        shown = 0;
    return shown;
}

// The game's 0x004B8ED0: the end of a drag. Gives the highlighted occupant
// its old highlight back and lets it go, empties the overlay's cell list,
// destroys the region.
void __fastcall EndDrag(char* self, void*)
{
    if (void* const lit = At<void*>(self, field::kLit)) {
        Virtual<void>(lit, 0x44, At<uint32_t>(self, field::kLitOld), uint32_t{0}); // SetHighlight
        At<uint32_t>(self, field::kLitOld) = 0;
        if (void* const again = At<void*>(self, field::kLit)) {
            At<void*>(self, field::kLit) = nullptr;
            Release(again);
        }
    }
    This<void>(addr::SC4UI_OverlayClear, At<void*>(self, field::kOverlay));
    if (char* const region = At<char*>(self, field::kRegion))
        DestroyRegion(region);
    At<void*>(self, field::kRegion) = nullptr;
}

// The game's 0x004B93B0: the preview of the drag, see the file's head. The
// order: the selection rectangle (the drag's, or the hovered cell's); the old
// region destroyed (its cell count kept); the view's filter fetched; the
// region built (the selection, widened by every built lot the filter takes
// that reaches out of it: a pass over the selection for the extent, then the
// bits set for the selection and for the lots' cells); the service asked for
// the price (DemolishOccupant for a single hovered occupant, else
// DemolishRegion, demolish 0); the highlight of the occupant under a one-cell
// preview moved (the hovered occupant, the building of the lot there, or a
// network piece found by cell); the overlay's colours, cells and the cost
// indicator set; a sound when the cell count changed.
void __fastcall Preview(char* self, void*)
{
    const int32_t sx = At<int32_t>(self, field::kStart), sz = At<int32_t>(self, field::kStart + 4);
    const int32_t ex = At<int32_t>(self, field::kEnd), ez = At<int32_t>(self, field::kEnd + 4);
    const bool drag = !(sx == ex && sz == ez);
    int32_t minX, minZ, maxX, maxZ;
    if (drag) {
        minX = sx < ex ? sx : ex;
        minZ = sz < ez ? sz : ez;
        maxX = sx > ex ? sx : ex;
        maxZ = sz > ez ? sz : ez;
    } else {
        minX = At<int32_t>(self, field::kCell);
        minZ = At<int32_t>(self, field::kCell + 4);
        maxX = At<int32_t>(self, field::kCell + 8);
        maxZ = At<int32_t>(self, field::kCell + 12);
    }

    int32_t oldCount = 0;
    if (char* const old = At<char*>(self, field::kRegion)) {
        const int32_t* const r = reinterpret_cast<const int32_t*>(old);
        oldCount = (r[3] - r[1] + 1) * (r[2] - r[0] + 1);
        DestroyRegion(old);
    }

    void* filter = nullptr;
    {
        void* got = nullptr;
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &got); // the view's filter
        filter = got;
        if (got) {
            AddRef(got);
            Release(got);
        }
    }

    int32_t bx0 = minX, bz0 = minZ, bx1 = maxX, bz1 = maxZ;
    bool reaches = false;
    if (minX <= maxX) {
        for (int32_t x = minX; x <= maxX; ++x) {
            for (int32_t z = minZ; z <= maxZ; ++z) {
                Rect r;
                if (!LotOutside(self, filter, x, z, r, minX, minZ, maxX, maxZ))
                    continue;
                reaches = true;
                if (bx0 >= r.x0)
                    bx0 = r.x0;
                if (bz0 >= r.z0)
                    bz0 = r.z0;
                if (bx1 <= r.x1)
                    bx1 = r.x1;
                if (bz1 <= r.z1)
                    bz1 = r.z1;
            }
        }
    }
    if (!reaches) {
        At<void*>(self, field::kRegion) = MakeRegion(minX, minZ, maxX, maxZ, true);
    } else {
        char* const region = static_cast<char*>(MakeRegion(bx0, bz0, bx1, bz1, false));
        At<void*>(self, field::kRegion) = region;
        for (int32_t x = minX; x <= maxX; ++x)
            for (int32_t z = minZ; z <= maxZ; ++z)
                SetBit(region, x, z);
        for (int32_t x = bx0; x <= bx1; ++x) {
            for (int32_t z = bz0; z <= bz1; ++z) {
                Rect r;
                if (LotOutside(self, filter, x, z, r, minX, minZ, maxX, maxZ))
                    SetBit(region, x, z);
            }
        }
    }

    // The price. Both services answer into +0x88.
    int64_t* const cost = reinterpret_cast<int64_t*>(self + field::kCost);
    *cost = 0;
    uint8_t allowed;
    if (At<uint8_t>(self, field::kSingle))
        allowed = Virtual<uint8_t>(At<void*>(self, field::kDemolition), 0x20, uint32_t{0},
                                   At<void*>(self, field::kHovered), int32_t{1}, uint32_t{1}, cost, uint32_t{0},
                                   uint32_t{0});
    else
        allowed = DemolishRegionCall(self, 0x004B97F1, 0, At<void*>(self, field::kRegion), 1, 1, 0, nullptr, cost,
                                     nullptr, nullptr, 0, 0);
    At<uint8_t>(self, field::kAllowed) = allowed;

    // The old highlight goes back.
    if (void* const lit = At<void*>(self, field::kLit)) {
        Virtual<void>(lit, 0x44, At<uint32_t>(self, field::kLitOld), uint32_t{0});
        At<uint32_t>(self, field::kLitOld) = 0;
        if (void* const again = At<void*>(self, field::kLit)) {
            At<void*>(self, field::kLit) = nullptr;
            Release(again);
        }
    }

    // The occupant that gets the highlight: only for a preview of one cell.
    if (!drag) {
        // Keeps the occupant the highlight goes to (referenced).
        const auto keep = [&](void* chosen) {
            void* const old = At<void*>(self, field::kLit);
            if (chosen != old) {
                if (chosen)
                    AddRef(chosen);
                At<void*>(self, field::kLit) = chosen;
                if (old)
                    Release(old);
            }
        };
        if (At<uint8_t>(self, field::kSingle)) {
            keep(At<void*>(self, field::kHovered));
        } else if (void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x18,
                                                    At<int32_t>(self, field::kCell),
                                                    At<int32_t>(self, field::kCell + 4), uint32_t{0})) {
            if (void* const building = Virtual<void*>(lot, 0xC0))
                keep(Virtual<void*>(building, 0xC));
        } else {
            void* const allocator = ListNodeAllocator();
            char* const head = Virtual<char*>(allocator, 0xC, uint32_t{0xC});
            At<char*>(head, 0) = head;
            At<char*>(head, 4) = head;
            struct {
                char* head;
            } list{head};
            const int32_t x = At<int32_t>(self, field::kCell), z = At<int32_t>(self, field::kCell + 4);
            int32_t low[2] = {x, z};
            int32_t high[2] = {x, z};
            void* const network = At<void*>(self, field::kNetwork);
            if (Virtual<uint8_t>(network, 0xA4, &list, low, high, uint32_t{kTypeNetwork}, int32_t{-1})) {
                for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
                    void* const candidate = At<void*>(node, 8);
                    if (Virtual<uint8_t>(At<void*>(self, field::kFilter), 0xC, candidate)) {
                        keep(candidate);
                        break;
                    }
                }
                Virtual<void>(network, 0xC0, &list);
            }
            This<void>(addr::SC4List_Clear2, &list);
            Virtual<void>(ListNodeAllocator(), 0x10, list.head);
        }
        if (void* const lit = At<void*>(self, field::kLit)) {
            At<uint32_t>(self, field::kLitOld) = Virtual<uint32_t>(lit, 0x40); // GetHighlight
            Virtual<void>(lit, 0x44, uint32_t{5}, uint32_t{0});
        }
    }

    // The overlay: colours, cells, the cost indicator.
    void* const overlay = At<void*>(self, field::kOverlay);
    if (At<uint8_t>(self, field::kAllowed)) {
        This<void>(addr::SC4UI_MapOverlay_SetFillColor, overlay, self + field::kFillAllowed);
        This<void>(addr::SC4UI_MapOverlay_SetLineColor, overlay, self + field::kLineAllowed);
    } else {
        This<void>(addr::SC4UI_MapOverlay_SetFillColor, overlay, self + field::kFillDenied);
        This<void>(addr::SC4UI_MapOverlay_SetLineColor, overlay, self + field::kLineDenied);
    }
    This<void>(addr::SC4UI_OverlayClear, overlay);
    char* const region = At<char*>(self, field::kRegion);
    AddCells(static_cast<char*>(overlay), nullptr, region);
    Virtual<void>(At<void*>(self, field::kView), 0xA8, At<uint32_t>(self, field::kCost), uint32_t{0});

    const int32_t* const r = reinterpret_cast<const int32_t*>(region);
    const int32_t newCount = (r[3] - r[1] + 1) * (r[2] - r[0] + 1);
    if (oldCount != newCount) {
        if (At<uint8_t>(self, field::kAllowed)) {
            if (At<uint8_t>(self, field::kDragging))
                Virtual<void>(self, 0x70, uint32_t{0x0A5C32AB});
        } else {
            Virtual<void>(self, 0x70, uint32_t{0xAA62C9D5});
        }
    }
    if (filter)
        Release(filter);
}

// The game's 0x004B9B70 (ret 0xC; slot 20): the mouse moved. Not in a drag
// (no press held): the hover is refreshed when the cell changed (or the
// hovered occupant did) and the start cell follows. In any case, when the
// cell under the cursor differs from the end cell, the end cell follows and
// the preview runs; if only the hover changed, it runs for that. True.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    uint8_t changed = 0;
    int32_t cell[2];
    if (PickCell(self, nullptr, x, z, cell)) {
        if (!At<uint8_t>(self, field::kDragging)) {
            if (cell[0] != At<int32_t>(self, field::kStart) || cell[1] != At<int32_t>(self, field::kStart + 4))
                changed = 1;
            void* const previous = At<void*>(self, field::kHovered);
            if (previous)
                AddRef(previous);
            if (UpdateHover(self, nullptr, x, z, reinterpret_cast<int32_t*>(self + field::kCell)) &&
                At<void*>(self, field::kHovered) != previous)
                changed = 1;
            if (changed) {
                At<int32_t>(self, field::kStart) = cell[0];
                At<int32_t>(self, field::kStart + 4) = cell[1];
            }
            if (previous)
                Release(previous);
        }
        if (cell[0] != At<int32_t>(self, field::kEnd) || cell[1] != At<int32_t>(self, field::kEnd + 4)) {
            At<int32_t>(self, field::kEnd) = cell[0];
            At<int32_t>(self, field::kEnd + 4) = cell[1];
            This<void>(addr::cSC4ViewInputControlDemolish_Preview, self);
            return 1;
        }
        if (changed)
            This<void>(addr::cSC4ViewInputControlDemolish_Preview, self);
    }
    return 1;
}

std::uintptr_t PreviewEntry() { return reinterpret_cast<std::uintptr_t>(&Preview); }
std::uintptr_t MouseMoveEntry() { return reinterpret_cast<std::uintptr_t>(&OnMouseMove); }
std::uintptr_t EndDragEntry() { return reinterpret_cast<std::uintptr_t>(&EndDrag); }
std::uintptr_t ShownEntry() { return reinterpret_cast<std::uintptr_t>(&IsOccupantShown); }
bool WrappedIsOurs(Wrapped which) { return g_wrappedOurs[static_cast<int>(which)]; }
bool WrappedAreOurs() { return g_wrappedOurs[0] && g_wrappedOurs[1] && g_wrappedOurs[2] && g_wrappedOurs[3]; }
void SetWrappedIsOurs(Wrapped which, bool ours) { g_wrappedOurs[static_cast<int>(which)] = ours; }
bool InToolCall() { return g_callSite != 0; }
std::uintptr_t ToolCallSite() { return g_callSite; }

namespace {

bool InstallBulldozeTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4ViewInputControlDemolish_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::cSC4ViewInputControlDemolish_PickCell, reinterpret_cast<void*>(&PickCell)},
        {addr::cSC4ViewInputControlDemolish_FindOccupant, reinterpret_cast<void*>(&FindOccupant)},
        {addr::cSC4ViewInputControlDemolish_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4ViewInputControlDemolish_UpdateHover, reinterpret_cast<void*>(&UpdateHover)},
        {addr::cSC4ViewInputControlDemolish_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4ViewInputControlDemolish_OnSlot22, reinterpret_cast<void*>(&OnSlot22)},
        {addr::cSC4ViewInputControlDemolish_Cancel, reinterpret_cast<void*>(&Cancel)},
        {addr::SC4ViewInputControls_F4B9070, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4ViewInputControlDemolish_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::cSC4ViewInputControlDemolish_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_MapOverlay_AddCells, reinterpret_cast<void*>(&AddCells)},
        {addr::cSC4ViewInputControlDemolish_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::cSC4ViewInputControlDemolish_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    // The four game.bulldoze wraps: ours at the game's entry too, so that the
    // wrapper (PostAppInit) can sit on our function (tool_bulldoze.h). A
    // foreign detour already at one of the entries: that one stays the
    // game's, the others are ours.
    static void* wrappedOriginals[4] = {};
    static const struct {
        Wrapped which;
        std::uintptr_t address;
        void* ours;
        const char* name;
    } wrapped[] = {
        {Wrapped::Preview, addr::cSC4ViewInputControlDemolish_Preview, reinterpret_cast<void*>(&Preview), "Preview"},
        {Wrapped::MouseMove, addr::cSC4ViewInputControlDemolish_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove),
         "OnMouseMove"},
        {Wrapped::EndDrag, addr::cSC4ViewInputControlDemolish_EndDrag, reinterpret_cast<void*>(&EndDrag), "EndDrag"},
        {Wrapped::Shown, addr::cSC4ViewInputControlDemolish_IsOccupantShown, reinterpret_cast<void*>(&IsOccupantShown),
         "IsOccupantShown"},
    };
    for (std::size_t i = 0; i < 4; ++i) {
        const addr::Entry* found = addr_check::Find(wrapped[i].address);
        if (!found)
            return false;
        if (hook::IsInstalledAt(found->address)) {
            log::Write("bulldoze tool: %s is already hooked; ours not installed there", wrapped[i].name);
            continue;
        }
        if (!hook::Install(*found, wrapped[i].ours, &wrappedOriginals[i]))
            return false;
        SetWrappedIsOurs(wrapped[i].which, true);
    }
    log::Write("bulldoze tool: ours at the game's entries of Preview %d, OnMouseMove %d, EndDrag %d, IsOccupantShown %d",
               WrappedIsOurs(Wrapped::Preview), WrappedIsOurs(Wrapped::MouseMove), WrappedIsOurs(Wrapped::EndDrag),
               WrappedIsOurs(Wrapped::Shown));
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.bulldoze",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PreAppInit, // before game.bulldoze wraps four of the functions (tool_bulldoze.h)
    addr::cSC4ViewInputControlDemolish_Init,
    "Our bulldoze tool: constructor, Init, Shutdown, key and mouse handlers, picking (docs/ui.md)",
    false,
    InstallBulldozeTool,
});

} // namespace
} // namespace opensc4::tool_bulldoze
