// Our cSC4ZoneDeveloper (build 641; the class map is in zone_sim.h), the
// parts around the zoning code: Init, Shutdown, the messages with the
// PlopAllBuildings cheat, the save record and our own object (constructor,
// destructor, identity). The lot manager's queries and the helpers the
// other parts call are in zone_slots.cpp.
//
// Fields this file verified (offsets from the object's start; DoMessage
// gets object + 8 and Write/Read object + 0xC, so their disassembly reads
// that much less):
//   +0x10 uint8  initialized (Init sets it last, Shutdown clears it first)
//   +0x11 uint8  loaded (Read sets it; message 0x26D31EC1 then calls each
//         lot's slot 8 at the lot's own cell)
//   +0x14 int32  reference count (signed compare in Release)
//   +0x18 message server; +0x1C 0x00B43DD0; +0x20 the building development
//         simulator; +0x24 the city, +0x28/+0x2C its slots 93/94 (cells in x
//         and z); +0x30 the occupant manager; +0x34 the terrain; +0x3C the
//         traffic simulator; +0x40 the zone manager: all referenced by Init
//   +0x38 the terrain picker (0x00602160), +0x48, +0x44, +0x4C the network
//         tools of types 3, 11 and 0 (the network manager's slot 5 with
//         (type, 1), then the tool's slot 3): taken at message 0x26D31EC1
//   +0x50 the lot developer (0x00B43D20)
//   +0x54 the lot grid, vector of columns (12 bytes each) of lot pointers
//   +0x60 the lot list (head; nodes {next, previous, lot}), +0x64 the list
//         of its iterators, +0x68 its size
//   +0x74 a cSC4SimGridUint16 (0x44 bytes, ID 0x69D5C456, tract size 4)
//   +0x78 map (head, size +0x7C; 0x20-byte head), erased by 0x00734600
//   +0x84, +0x88, +0x8C, +0x90, +0x94 int32: 2, 2, 1, 3, 6 at first
//   +0x98 to +0x9C bytes: 0, 1, 1, 0, 0 at first
//   +0xA0 the zone type of the operation under way (DoMessage sets it to
//         the deleted lot's while it parcels the lot's rectangle again)
//   +0xD4 the view's (0x00B43DD0) slot 10 object's +0x14 at Init; message
//         0xA6B79602 stores the dword its void 2 points at
//   +0xD8, +0xE4 maps with 0x24-byte heads, +0xF0 a map with a 0x1C-byte
//         head (head, size)
#include "core/zone_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::zone_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("zone: first %s", name);
}

namespace {

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidLot = 0xE818FCED;
constexpr uint32_t kMessageOccupant = 0x99EF1142;
constexpr uint32_t kMessageCityReady = 0x26D31EC1;
constexpr uint32_t kMessageA6B79602 = 0xA6B79602;
constexpr uint32_t kMessageCheat = 0x230E27AC;
constexpr uint32_t kMessages[3] = {kMessageOccupant, kMessageCityReady, kMessageA6B79602};
constexpr uint32_t kCheatPlopAllLots = 0x8929E68E;
constexpr uint32_t kCheatPlopAllBuildings = 0x4AB569C9;

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void(__thiscall*)(void* self);
using CtorIdFn = void*(__thiscall*)(void* self, uint32_t id);
using TextFn = void*(__thiscall*)(void* string, const char* text);
using RefFn = uint8_t(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using GetFn = void*(__cdecl*)();
using FillFn = void(__thiscall*)(void* vector, uint32_t count, const void* value);
using FillInsertFn = void(__thiscall*)(void* vector, void* where, uint32_t count, const void* value);
using CountFn = void(__thiscall*)(void* container, uint32_t count);
using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const void* value);
using EraseFn = void*(__thiscall*)(void* vector, void* first, void* last);
using NodeFn = void(__thiscall*)(void* map, void* node);
using InsertFn = void*(__thiscall*)(void* set, void* result, void* const* value);
using WalkStartFn = void(__thiscall*)(void* walk, int32_t x, int32_t z);
using WalkNextFn = uint8_t(__thiscall*)(void* walk, int32_t* x, int32_t* z);
using ProgressFn = void(__cdecl*)(uint32_t a, uint32_t b);
using RectFn = void(__thiscall*)(void* self, int32_t x, int32_t z, int32_t width, int32_t depth, uint32_t facing);
using ObjectFn = void(__thiscall*)(void* self, void* object);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);

char* LotManager(char* self) { return self + kLotManagerPart; }
char* Target(char* self) { return self + kMessagePart; }

uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// --- The game's red-black trees (head: color, parent = root, left most, right most) ---

// 0x0072ABE0 and 0x0072AC20 (the same code for the node types of the maps
// +0xD8/+0xE4 and +0xF0, whose values own nothing).
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        Free(node);
        node = left;
    }
}

void ResetHead(char* tree)
{
    char* const head = At<char*>(tree, 0x0);
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0xC) = head;
    At<uint32_t>(tree, 0x4) = 0;
}

// The inline clear: the nodes freed when the size is not 0, the head reset.
void ClearTree(char* tree)
{
    if (!At<uint32_t>(tree, 0x4))
        return;
    EraseNodes(At<char*>(At<char*>(tree, 0x0), 0x4));
    ResetHead(tree);
}

// A new empty tree head of `size` bytes from the STL allocator into `tree`.
void NewTree(char* tree, std::size_t size)
{
    At<char*>(tree, 0x0) = nullptr;
    char* const head = static_cast<char*>(Allocate(size));
    At<char*>(tree, 0x0) = head;
    At<uint32_t>(tree, 0x4) = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
}

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void DropString(GameString& string)
{
    string.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (string.begin)
        Free(string.begin);
}

// --- Init ------------------------------------------------------------------------

// A service into `offset` when there is none: stored even when null, then
// referenced; false when null.
bool Take(char* self, std::size_t offset, void* service)
{
    if (At<void*>(self, offset))
        return true;
    At<void*>(self, offset) = service;
    if (!service)
        return false;
    AddRefOf(service);
    return true;
}

void RegisterCheat(char* self, void* cheats, uint32_t code, std::uintptr_t nameAddress)
{
    GameString name{};
    void* const text = reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name, Global<const char*>(nameAddress));
    Virtual<uint8_t>(cheats, 0xC, code, text);
    DropString(name);
    Virtual<uint8_t>(cheats, 0x1C, Target(self), code);
}

// Slot 3 (0x00734030). False (initialized left 0) when a service is
// missing; true once done or when already done.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    uint8_t& once = Global<uint8_t>(addr::cSC4ZoneDeveloper_InitOnce);
    if (!once)
        once = 1;
    At<uint32_t>(self, field::kValueD4) = 0;
    void* const lotDeveloper = Global<void*>(addr::SC4_Service43D20);
    if (void* const old = At<void*>(self, field::kLotDeveloper); lotDeveloper != old) {
        if (lotDeveloper)
            AddRefOf(lotDeveloper);
        At<void*>(self, field::kLotDeveloper) = lotDeveloper;
        if (old)
            ReleaseOf(old);
    }
    if (!At<void*>(self, field::kView)) {
        if (!Take(self, field::kView, Global<void*>(addr::SC4_View43DD0)))
            return 0;
        void* const view = Global<void*>(addr::SC4_View43DD0);
        void* const object = view ? Virtual<void*>(view, 0x28) : nullptr;
        At<uint32_t>(self, field::kValueD4) = object ? At<uint32_t>(object, 0x14) : 0;
    }
    if (!Take(self, field::kDevelopment, Global<void*>(addr::SC4_Service43D68)))
        return 0;
    if (!At<void*>(self, field::kCity)) {
        if (!Take(self, field::kCity, Global<void*>(addr::SC4_City)))
            return 0;
        At<int32_t>(self, field::kCellsX) = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174);
        At<int32_t>(self, field::kCellsZ) = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178);
    }
    if (!Take(self, field::kOccupants, Global<void*>(addr::SC4_NetworkService)))
        return 0;
    if (!Take(self, field::kTerrain, Global<void*>(addr::SC4_Terrain)))
        return 0;
    if (!Take(self, field::kTraffic, Global<void*>(addr::SC4_Service43DA8)))
        return 0;
    if (!Take(self, field::kZones, Global<void*>(addr::SC4_Service43D14)))
        return 0;

    char* const grid = self + field::kLotGrid;
    if (At<char*>(grid, 0x0) == At<char*>(grid, 0x4)) {
        // A new city: a column of null lots per cell in x.
        reinterpret_cast<CountFn>(addr::cSC4ZoneDeveloper_ResizeLotGrid)(grid, At<uint32_t>(self, field::kCellsX));
        const int32_t columns = static_cast<int32_t>((At<char*>(grid, 0x4) - At<char*>(grid, 0x0)) / 12);
        const void* none = nullptr;
        for (int32_t i = 0; i < columns; ++i)
            reinterpret_cast<FillFn>(addr::SC4PtrVector_Fill)(At<char*>(grid, 0x0) + i * 12,
                                                              At<uint32_t>(self, field::kCellsZ), &none);
    } else {
        // A loaded city: every lot initialised once, at its own cell.
        for (int32_t x = 0; x < At<int32_t>(self, field::kCellsX); ++x)
            for (int32_t z = 0; z < At<int32_t>(self, field::kCellsZ); ++z)
                if (void* const lot = Virtual<void*>(LotManager(self), 0x18, x, z, 1u))
                    Virtual<uint8_t>(lot, 0x14);
    }
    if (!At<void*>(self, field::kGrid)) {
        void* grid16 = OperatorNew(0x44);
        if (grid16)
            grid16 = reinterpret_cast<CtorIdFn>(addr::cSC4SimGridUint16_Construct)(grid16, 0x49B9E600);
        At<void*>(self, field::kGrid) = grid16;
        AddRefOf(grid16);
        Virtual<uint8_t>(At<void*>(self, field::kGrid), 0xC);
        Virtual<uint8_t>(At<void*>(self, field::kGrid), 0x24, 4);
        Virtual<uint8_t>(At<void*>(self, field::kGrid), 0x18, 0x69D5C456u);
    }
    reinterpret_cast<SelfFn>(addr::cSC4ZoneDeveloper_Fn_0072FF80)(self);
    if (!At<void*>(self, field::kMessages)) {
        if (!Take(self, field::kMessages, Global<void*>(addr::SC4_MessageServer)))
            return 0;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, Target(self), message);
    }
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54)) {
            RegisterCheat(self, cheats, kCheatPlopAllLots, addr::cSC4ZoneDeveloper_PlopAllLotsName);
            RegisterCheat(self, cheats, kCheatPlopAllBuildings, addr::cSC4ZoneDeveloper_PlopAllBuildingsName);
        }
    }
    At<uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// --- Shutdown ---------------------------------------------------------------------

void Drop(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        ReleaseOf(object);
        At<void*>(self, offset) = nullptr;
    }
}

// 0x0072B8D0: the lot manager's slot 24 for every lot of the grid; the
// column count and each column's size are taken before their loops.
void DeleteAllLots(char* self)
{
    const int32_t columns =
        static_cast<int32_t>((At<char*>(self, field::kLotGrid + 4) - At<char*>(self, field::kLotGrid)) / 12);
    for (int32_t x = 0; x < columns; ++x) {
        char* const column = At<char*>(self, field::kLotGrid) + x * 12;
        const int32_t cells = static_cast<int32_t>((At<char*>(column, 0x4) - At<char*>(column, 0x0)) >> 2);
        for (int32_t z = 0; z < cells; ++z)
            if (void* const lot = GridLot(self, x, z))
                Virtual<void>(LotManager(self), 0x60, lot);
    }
}

// Slot 4 (0x007335A0). Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        for (const uint32_t code : {kCheatPlopAllLots, kCheatPlopAllBuildings}) {
            Virtual<uint8_t>(cheats, 0x20, Target(self), code);
            Virtual<uint8_t>(cheats, 0x10, code);
        }
    }
    if (At<void*>(self, field::kMessages)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, Target(self), message);
        ReleaseOf(At<void*>(self, field::kMessages));
        At<void*>(self, field::kMessages) = nullptr;
    }
    ClearTree(self + field::kMapF0);
    DeleteAllLots(self);
    reinterpret_cast<EraseFn>(addr::SC4EdgeRows_Erase)(self + field::kLotGrid, At<void*>(self, field::kLotGrid),
                                                       At<void*>(self, field::kLotGrid + 4));
    for (const std::size_t offset : {field::kTool44, field::kStreetTool, field::kTool4C, field::kGrid}) {
        if (At<void*>(self, offset)) {
            Virtual<uint8_t>(At<void*>(self, offset), 0x10);
            ReleaseOf(At<void*>(self, offset));
            At<void*>(self, offset) = nullptr;
        }
    }
    for (const std::size_t offset : {field::kZones, field::kTraffic, field::kTerrain, field::kTerrainPicker,
                                     field::kOccupants, field::kCity, field::kDevelopment, field::kView})
        Drop(self, offset);
    // Cleared before the release, unlike the others.
    if (void* const lotDeveloper = At<void*>(self, field::kLotDeveloper)) {
        At<void*>(self, field::kLotDeveloper) = nullptr;
        ReleaseOf(lotDeveloper);
    }
    return 1;
}

// --- Messages ---------------------------------------------------------------------

// 0x0072BE50, the cheat "PlopAllBuildings": for each building type of the
// development simulator a generic lot configuration, a lot at the first
// cell of a spiral walk from the city's centre where the lot manager makes
// one (slot 22), and the building through the lot developer. The walk goes
// on from lot to lot (the lot's width + 1 cells are skipped). The
// configurations are not released, as in the game.
void PlopAllBuildings(char* self)
{
    static bool logged = false;
    FirstCall("PlopAllBuildings", logged);
    const auto Start = reinterpret_cast<WalkStartFn>(addr::SC4SpiralWalk_Start);
    const auto Next = reinterpret_cast<WalkNextFn>(addr::SC4SpiralWalk_Next);
    void* const configurations = Global<void*>(addr::SC4_Service43D18);
    uint32_t walk[12] = {};
    walk[4] = 0x80000000;
    walk[5] = 0x80000000;
    walk[6] = 0x7FFFFFFF;
    walk[7] = 0x7FFFFFFF;
    reinterpret_cast<uint8_t*>(walk)[0x20] = 1;
    walk[11] = 0;
    Start(walk, 0, 0);
    const int32_t cellsX = At<int32_t>(self, field::kCellsX), cellsZ = At<int32_t>(self, field::kCellsZ);
    walk[4] = 0;
    walk[5] = 0;
    walk[6] = static_cast<uint32_t>(cellsX - 1 < 0 ? 0 : cellsX - 1);
    walk[7] = static_cast<uint32_t>(cellsZ - 1 < 0 ? 0 : cellsZ - 1);
    Start(walk, cellsX / 2, cellsZ / 2);
    struct {
        uint32_t* begin;
        uint32_t* end;
        uint32_t* capacity;
    } types{};
    int32_t placed = 0;
    Virtual<void>(Global<void*>(addr::SC4_Service43D68), 0x2C, &types);
    for (uint32_t* type = types.begin; type != types.end; ++type) {
        void* configuration = nullptr;
        Virtual<void>(configurations, 0x58, &configuration, *type);
        int32_t width = 0, depth = 0;
        Virtual<void>(configuration, 0x1C, &width, &depth);
        int32_t x = 0, z = 0;
        void* lot = nullptr;
        bool made = false;
        while (Next(walk, &x, &z)) {
            lot = nullptr;
            if (Virtual<uint8_t>(LotManager(self), 0x58, x, z, width, depth, 0u, &lot, 1u, 0u, configuration)) {
                made = true;
                break;
            }
        }
        if (!made)
            break;
        void* const lotDeveloper = Virtual<void*>(At<void*>(self, field::kDevelopment), 0x90);
        Virtual<void>(lotDeveloper, 0x1C, lot, configuration, *type, 0u);
        ReleaseOf(lot);
        for (int32_t skip = width + 1; skip != 0; --skip)
            Next(walk, &x, &z);
        if (++placed % 16 == 0)
            reinterpret_cast<ProgressFn>(addr::SC4_Fn_005F9140)(100, 1);
    }
    if (types.begin)
        Free(types.begin);
}

// The network manager's tool of a type into `offset` when there is none
// (0x00602680 is the city's slot 38; its slot 5 makes the tool), then the
// tool's slot 3. No null checks, as the game.
void TakeTool(char* self, std::size_t offset, uint32_t type)
{
    if (At<void*>(self, offset))
        return;
    void* const manager = reinterpret_cast<GetFn>(addr::SC4_GetNetworkManager)();
    void* const tool = Virtual<void*>(manager, 0x14, type, 1u);
    At<void*>(self, offset) = tool;
    AddRefOf(tool);
    Virtual<uint8_t>(At<void*>(self, offset), 0xC);
}

// Message 0x99EF1142 with data 3 = 0xC772BF98: an occupant was added. A
// lot under it (unless the occupant's property holder 0xA821EF94 answers
// its slot 9 for 0x80200090) is deleted and, when its zone type is an RCI
// one, its rectangle parcelled again; then the street joining by data 4.
void OccupantAdded(char* self, void* message)
{
    void* const occupant = Virtual<void*>(message, 0x28);
    void* holder = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &holder)) {
        const bool exempt = Virtual<uint8_t>(holder, 0x24, 0x80200090u) != 0;
        ReleaseOf(holder);
        if (exempt)
            return;
    }
    float position[3] = {};
    Virtual<void>(occupant, 0x20, position);
    int32_t x = 0, z = 0;
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, position[0], position[2], &x, &z);
    // No bounds check, as the game.
    if (void* const lot = GridLot(self, x, z)) {
        int32_t rect[4] = {};
        Virtual<void>(lot, 0x38, rect);
        const uint32_t facing = Virtual<uint32_t>(lot, 0x5C);
        At<uint32_t>(self, field::kZoneType) = Virtual<uint32_t>(lot, 0x7C);
        Virtual<void>(LotManager(self), 0x60, lot);
        if (Virtual<uint8_t>(At<void*>(self, field::kZones), 0x34, At<uint32_t>(self, field::kZoneType)))
            reinterpret_cast<RectFn>(addr::cSC4ZoneDeveloper_Fn_0072D440)(self, rect[0], rect[1],
                                                                           rect[2] - rect[0] + 1,
                                                                           rect[3] - rect[1] + 1, facing);
        At<uint32_t>(self, field::kZoneType) = 0;
    }
    const uint32_t kind = Virtual<uint32_t>(message, 0x60);
    if (kind == 1 || kind == 2)
        reinterpret_cast<ObjectFn>(addr::cSC4ZoneDeveloper_Fn_00731F60)(self, occupant);
    else if (kind == 3)
        reinterpret_cast<ObjectFn>(addr::cSC4ZoneDeveloper_Fn_00732450)(self, occupant);
}

// cIGZMessageTarget2 slot 3 (0x007328D0), on the part at +0x8. Always true.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - kMessagePart;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case kMessageCityReady: {
        if (!At<void*>(self, field::kTerrainPicker)) {
            void* const picker = reinterpret_cast<GetFn>(addr::SC4_TerrainPicker)();
            At<void*>(self, field::kTerrainPicker) = picker;
            AddRefOf(picker);
        }
        TakeTool(self, field::kStreetTool, 3);
        TakeTool(self, field::kTool44, 11);
        TakeTool(self, field::kTool4C, 0);
        if (!At<uint8_t>(self, field::kLoaded))
            break;
        for (int32_t x = 0; x < At<int32_t>(self, field::kCellsX); ++x)
            for (int32_t z = 0; z < At<int32_t>(self, field::kCellsZ); ++z) {
                void* const lot = GridLot(self, x, z);
                if (!lot)
                    continue;
                int32_t lotX = 0, lotZ = 0;
                Virtual<void>(lot, 0x54, &lotX, &lotZ);
                if (x == lotX && z == lotZ)
                    Virtual<uint8_t>(lot, 0x20);
            }
        break;
    }
    case kMessageCheat:
        if (Virtual<uint32_t>(message, 0x24) == kCheatPlopAllLots)
            reinterpret_cast<SelfFn>(addr::cSC4ZoneDeveloper_Fn_0072C8E0)(self);
        else if (Virtual<uint32_t>(message, 0x24) == kCheatPlopAllBuildings)
            PlopAllBuildings(self);
        break;
    case kMessageOccupant:
        if (Virtual<uint32_t>(message, 0x4C) == 0xC772BF98)
            OccupantAdded(self, message);
        break;
    case kMessageA6B79602:
        At<uint32_t>(self, field::kValueD4) = *Virtual<uint32_t*>(message, 0x3C);
        break;
    default:
        break;
    }
    return 1;
}

// --- Save --------------------------------------------------------------------------

// cIGZSerializable slot 3 (0x0072AB40), on the part at +0xC: version 1, the
// grid's columns, the first column's cells, then a reference per cell,
// column by column. False when the stream is in error after.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - kSerializablePart;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    char* const begin = At<char*>(self, field::kLotGrid);
    const uint32_t columns = static_cast<uint32_t>((At<char*>(self, field::kLotGrid + 4) - begin) / 12);
    const uint32_t cells =
        columns ? static_cast<uint32_t>((At<char*>(At<char*>(self, field::kLotGrid), 0x4) -
                                         At<char*>(At<char*>(self, field::kLotGrid), 0x0)) >>
                                        2)
                : 0;
    Virtual<uint8_t>(stream, 0x24, columns);
    Virtual<uint8_t>(stream, 0x24, cells);
    for (char* column = At<char*>(self, field::kLotGrid); column != At<char*>(self, field::kLotGrid + 4);
         column += 12)
        for (void** lot = At<void**>(column, 0x0); lot != At<void**>(column, 0x4); ++lot)
            WriteRef(stream, *lot);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The game's hash set of pointers (0x14 bytes).
struct PointerSet {
    void* unused;
    void* buckets;
    void* bucketsEnd;
    void* bucketsCapacity;
    uint32_t count;
};

// cIGZSerializable slot 4 (0x00734380): version 1 only (false otherwise,
// with +0x11 set all the same). Every cell's reference is read; a lot seen
// for the first time goes to the end of the lot list +0x60 with that
// reference, the references of its other cells are released.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - kSerializablePart;
    At<uint8_t>(self, field::kLoaded) = 1;
    // The game compares a stack word the read may leave unset; 0 here.
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    PointerSet seen{};
    reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Construct)(&seen, 100);
    uint32_t columns = 0, cells = 0;
    Virtual<uint8_t>(stream, 0x24, &columns);
    Virtual<uint8_t>(stream, 0x24, &cells);
    // The insert's 9-byte result lies where the empty column was (the game
    // reuses the stack slots).
    struct {
        void* a;
        void* b;
        uint32_t c;
    } scratch{};
    char* const grid = self + field::kLotGrid;
    reinterpret_cast<ResizeFn>(addr::SC4LotColumns_Resize)(grid, columns, &scratch);
    const void* none = nullptr;
    for (uint32_t x = 0; x < columns; ++x) {
        char* const column = At<char*>(grid, 0x0) + static_cast<std::ptrdiff_t>(x) * 12;
        const uint32_t size = static_cast<uint32_t>((At<char*>(column, 0x4) - At<char*>(column, 0x0)) >> 2);
        if (cells < size)
            At<char*>(column, 0x4) = At<char*>(column, 0x0) + static_cast<std::ptrdiff_t>(cells) * 4;
        else
            reinterpret_cast<FillInsertFn>(addr::SC4PtrVector_FillInsert)(column, At<void*>(column, 0x4),
                                                                          cells - size, &none);
        for (uint32_t z = 0; z < cells; ++z) {
            void* lot = nullptr;
            ReadRef(stream, kIidLot, &lot, 0u);
            At<void**>(column, 0x0)[z] = lot;
            if (!lot)
                continue;
            reinterpret_cast<CountFn>(addr::SC4PtrHashSet_Resize)(&seen, seen.count + 1);
            reinterpret_cast<InsertFn>(addr::SC4PtrHashSet_Insert)(&seen, &scratch, &lot);
            if (!(scratch.c & 0xFF)) {
                ReleaseOf(lot);
                continue;
            }
            void** const head = At<void**>(self, field::kLotList);
            void** const node = static_cast<void**>(Allocate(0xC));
            node[2] = lot;
            void** const last = static_cast<void**>(head[1]);
            node[0] = head;
            node[1] = last;
            last[0] = node;
            head[1] = node;
            ++At<uint32_t>(self, field::kLotCount);
        }
    }
    const uint8_t ok = Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
    reinterpret_cast<SelfFn>(addr::SC4Uint32HashSet_Clear)(&seen);
    if (seen.buckets)
        Free(seen.buckets);
    return ok;
}

// --- Installation of Init, Shutdown, DoMessage, Write and Read -----------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

// The lot manager's slots 3 and 4 are thunks that jump to the game's Init
// and Shutdown: they are swapped with them.
uint8_t __fastcall InitThunk4(char* part, void*) { return Init(part - kLotManagerPart, nullptr); }
uint8_t __fastcall ShutdownThunk4(char* part, void*) { return Shutdown(part - kLotManagerPart, nullptr); }

const Slot kInitSlots[] = {
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable, 3, addr::cSC4ZoneDeveloper_Init, Init),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 3, addr::cSC4ZoneDeveloper_LotManager_InitThunk, InitThunk4)};
const Slot kShutdownSlots[] = {
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable, 4, addr::cSC4ZoneDeveloper_Shutdown, Shutdown),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 4, addr::cSC4ZoneDeveloper_LotManager_ShutdownThunk,
                 ShutdownThunk4)};
const Slot kDoMessageSlots[] = {
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_8, 3, addr::cSC4ZoneDeveloper_DoMessage, DoMessage)};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_C, 3, addr::cSC4ZoneDeveloper_Write, Write),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_C, 4, addr::cSC4ZoneDeveloper_Read, Read)};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("zone %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("zone %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }
bool InstallShutdown(registry::Mode) { return SwapSlots(kShutdownSlots, "shutdown"); }
bool InstallMessages(registry::Mode) { return SwapSlots(kDoMessageSlots, "messages"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.zone.init", "simulators/zone", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneDeveloper_vftable,
                        "Our zone developer Init (src/core/zone_object.cpp)", false, InstallInit}),
    registry::Register({"simulators.zone.shutdown", "simulators/zone", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneDeveloper_vftable,
                        "Our zone developer Shutdown with the deletion of every lot (src/core/zone_object.cpp)",
                        false, InstallShutdown}),
    registry::Register({"simulators.zone.messages", "simulators/zone", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneDeveloper_vftable_8,
                        "Our zone developer DoMessage with the PlopAllBuildings cheat (src/core/zone_object.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.zone.save", "simulators/zone", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneDeveloper_vftable_C,
                        "Our zone developer Write and Read: the lot grid, version 1 (src/core/zone_object.cpp)",
                        false, InstallSave}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00AB3830 (11 slots, deleting destructor last),
// 0x00AB37C0 (at +4, 28 slots), 0x00AB37AC (at +8) and 0x00AB3794 (at
// +0xC), taken at the first construction, after every PostAppInit slot swap
// of the five parts' groups.
void* g_vtable[kSlots];
void* g_vtable4[kLotManagerSlots];
void* g_vtable8[kMessageSlots];
void* g_vtableC[kSerializableSlots];

// As the game's (0x00695330, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x00729EF0). The game's null tests of `this` for the
// parts cannot be met.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidZoneDeveloper: offset = 0; break;
    case kIidLotManager: offset = kLotManagerPart; break;
    case kIidMessageTarget2: offset = kMessagePart; break;
    case kIidSerializable: offset = kSerializablePart; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x0072A270): Shutdown and the deleting destructor (slot
// 10) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x28, 1u);
    return 0;
}

// A map's nodes and head freed, as the destructor's inline code.
void DestroyTree(char* tree)
{
    ClearTree(tree);
    if (void* const head = At<void*>(tree, 0x0))
        Free(head);
}

// The destructor (0x00734680): what Shutdown left.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kLotManagerPart) = g_vtable4;
    At<void**>(self, kMessagePart) = g_vtable8;
    At<void**>(self, kSerializablePart) = g_vtableC;
    DestroyTree(self + field::kMapF0);
    DestroyTree(self + field::kCellsE4);
    DestroyTree(self + field::kCellsD8);
    // The map +0x78 owns maps of maps: zone_parcellize.cpp's eraser (0x00734600).
    char* const tree78 = self + field::kTree78;
    if (At<uint32_t>(tree78, 0x4)) {
        reinterpret_cast<NodeFn>(addr::cSC4ZoneDeveloper_EraseTree78)(tree78, At<void*>(At<char*>(tree78, 0x0), 0x4));
        ResetHead(tree78);
    }
    if (void* const head = At<void*>(tree78, 0x0))
        Free(head);
    // The lot lists and the grid (generic, shared: left the game's).
    reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy)(self + field::kLotIterators);
    reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy)(self + field::kLotList);
    reinterpret_cast<SelfFn>(addr::SC4ShortVectorVector_Destroy)(self + field::kLotGrid);
    if (void* const lotDeveloper = At<void*>(self, field::kLotDeveloper))
        ReleaseOf(lotDeveloper);
}

// Slot 10, as the game's (0x00734AD0).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("zone object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x00729EB0).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ReleaseObject(part - Offset, nullptr);
}

// The lot manager's slots 3 and 4 (thunks 0x007348C0, 0x007348B0): the
// object's Init and Shutdown as its vtable copy has them.
uint8_t __fastcall InitAt4(char* part, void*)
{
    return Virtual<uint8_t>(part - kLotManagerPart, 0xC);
}

uint8_t __fastcall ShutdownAt4(char* part, void*)
{
    return Virtual<uint8_t>(part - kLotManagerPart, 0x10);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4ZoneDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4ZoneDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4ZoneDeveloper_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4ZoneDeveloper_vftable_C), sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[10] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kLotManagerPart>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<kLotManagerPart>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<kLotManagerPart>);
    g_vtable4[3] = reinterpret_cast<void*>(&InitAt4);
    g_vtable4[4] = reinterpret_cast<void*>(&ShutdownAt4);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kMessagePart>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<kMessagePart>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<kMessagePart>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kSerializablePart>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<kSerializablePart>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<kSerializablePart>);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("zone object: vtables copied (slots 3/4 %p %p, +4 slot 6 %p, +8 slot 3 %p, +0xC slots 3/4 %p %p)",
               g_vtable[3], g_vtable[4], g_vtable4[6], g_vtable8[3], g_vtableC[3], g_vtableC[4]);
}

// An empty list head (0xC bytes from the STL allocator, self-linked; the
// value is left unset) into `offset`.
void NewList(char* self, std::size_t offset)
{
    At<void*>(self, offset) = nullptr;
    void** const head = static_cast<void**>(Allocate(0xC));
    head[0] = head;
    head[1] = head;
    At<void**>(self, offset) = head;
}

// The constructor (detour of 0x007348D0, reached only from the factory
// 0x00734AF0): the fields the game's sets, in its order of allocations.
// Fields it leaves unset (+0x80, +0xBC to +0xC8, +0xD4, +0xE0, +0xEC, +0xF8)
// stay unset.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kLotManagerPart) = g_vtable4;
    At<void**>(self, kMessagePart) = g_vtable8;
    At<void**>(self, kSerializablePart) = g_vtableC;
    At<uint8_t>(self, 0x10) = 0;
    At<uint8_t>(self, 0x11) = 0;
    for (std::size_t offset = 0x14; offset <= 0x5C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    NewList(self, field::kLotList);
    NewList(self, field::kLotIterators);
    At<uint32_t>(self, 0x68) = 0;
    At<uint32_t>(self, 0x6C) = 0;
    At<uint32_t>(self, 0x70) = 0;
    At<uint32_t>(self, 0x74) = 0;
    NewTree(self + field::kTree78, 0x20);
    At<int32_t>(self, 0x84) = 2;
    At<int32_t>(self, 0x88) = 2;
    At<int32_t>(self, 0x8C) = 1;
    At<int32_t>(self, 0x90) = 3;
    At<int32_t>(self, 0x94) = 6;
    At<uint8_t>(self, 0x98) = 0;
    At<uint8_t>(self, 0x99) = 1;
    At<uint8_t>(self, 0x9A) = 1;
    At<uint8_t>(self, 0x9B) = 0;
    At<uint8_t>(self, 0x9C) = 0;
    At<uint32_t>(self, 0xA0) = 0;
    At<uint32_t>(self, 0xA4) = 0;
    At<uint32_t>(self, 0xA8) = 0;
    At<uint32_t>(self, 0xCC) = 0;
    At<uint32_t>(self, 0xD0) = 0;
    NewTree(self + field::kCellsD8, 0x24);
    NewTree(self + field::kCellsE4, 0x24);
    NewTree(self + field::kMapF0, 0x1C);
    for (std::size_t offset = 0xAC; offset <= 0xB8; offset += 4)
        At<uint32_t>(self, offset) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("zone object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4ZoneDeveloper_cSC4ZoneDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.zone.object",
    "simulators/zone",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ZoneDeveloper_cSC4ZoneDeveloper,
    "Our cSC4ZoneDeveloper object (with the lot manager part): constructor (detour), destructor, identity; "
    "methods from copies of the (patched) game vtables (src/core/zone_object.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::zone_sim
