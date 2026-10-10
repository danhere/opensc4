// Our cSC4SeaportDeveloper (build 641; class ID 0xEA230A28, code
// 0x00700E80-0x00702023): the seaports' budget department, the sea access
// map, the capacity of each port and our own object. The port base
// (cSC4PortDeveloper, 0x006E8F90-0x006EAC30: the port list, growth, the
// save record) is shared with the airport developer and called here at the
// game's addresses.
//
// The object (0x7C bytes; constructor 0x007018F0, factory 0x007019F0):
//   +0x00 the primary cISC4SeaportDeveloper vtable (0x00AB2E10, 17 slots)
//   +0x04 the port base B (its vtable here 0x00AB2DC0, 19 slots); the base's
//         functions get B, so their disassembly reads 4 less than below
//   +0x08 cIGZMessageTarget2 (0x00AB2DAC: DoMessage 0x00701F20)
//   +0x0C cIGZSerializable (0x00AB2D94: Write 0x0068B3E0, Read 0x007010B0,
//         GetGZCLSID 0x00700E80)
//   +0x10 uint8   the base's port types are loaded
//   +0x11 uint8   initialized (the base's Init sets it, its Shutdown clears it)
//   +0x14 int32   reference count
//   +0x18 uint32  0xEA29AD92 (saved)
//   +0x20 the message server; +0x24 0x00B43D14; +0x28 the lot manager;
//   +0x2C the city; +0x30 the budget simulator (the base's Init, referenced)
//   +0x34 the seaports' budget department (ID 0x4A4155AB, referenced by
//         Init); its line item 1 holds the ports' cost
//   +0x38 uint32  0xC (saved as uint16); +0x3C 0xEA3E904A (a message the
//         base posts); +0x40 0x4A4155AB; +0x44 0x1509
//   +0x48 the port list (head node; nodes: next, previous, port)
//   +0x4C vector of port types (the base's)
//   +0x64 uint8   ports changed: Update checks their sea access
//   +0x65 uint8   the sea access map is stale (nothing in the class sets it)
//   +0x66 uint8   the city has sea (water on a map edge)
//   +0x68 vector<uint8_t> the sea access map (begin, end, capacity), one
//         byte a cell: 0x80 water; 1, 8, 2, 4 reached from the z = 0,
//         z = max, x = 0, x = max edge
//   +0x74 uint32  the map's width; +0x78 its height (terrain slots 7, 8)
//
// A port (0x28 bytes, vtable 0x00AB2760): +0x4 stage index, +0x8 its type
// (+0xC stage count, +0x14 stages of 0x20 bytes: +0x8 int64 cost, +0x10
// uint32 capacity; +0x24 vector of rows of uint32, the terrain table),
// +0xC uint8 lot not looked up yet, +0xE/+0x10 int16 cell, +0x14 float
// efficiency, +0x20 the lot.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::seaport_sim {
namespace {

constexpr uint32_t kClassId = 0xEA230A28;
constexpr uint32_t kIidSeaportDeveloper = 0xCA22FE3E;
constexpr uint32_t kSimulatorType = 0xEA3957AD;
constexpr uint32_t kDepartmentId = 0x4A4155AB;
constexpr uint32_t kMessageGrow = 0x8A394DC4;
constexpr uint32_t kMessageTerrain = 0x6A3B0AFD;
constexpr uint32_t kMessageNoAccess = 0x4A490D33;
constexpr uint32_t kOccupantType = 0xA823821E;
constexpr uint32_t kAccessProperty = 0xC98204B8;
constexpr uint32_t kIidAccessOccupant = 0xE9793A65;

namespace field {
constexpr std::size_t kBase = 0x4;
constexpr std::size_t kTarget = 0x8;
constexpr std::size_t kInitialized = 0x11;
constexpr std::size_t kRefCount = 0x14;
constexpr std::size_t kMessages = 0x20;
constexpr std::size_t kCity = 0x2C;
constexpr std::size_t kBudget = 0x30;
constexpr std::size_t kDepartment = 0x34;
constexpr std::size_t kPorts = 0x48;
constexpr std::size_t kPortsChanged = 0x64;
constexpr std::size_t kMapStale = 0x65;
constexpr std::size_t kHasSea = 0x66;
constexpr std::size_t kMap = 0x68;
constexpr std::size_t kWidth = 0x74;
constexpr std::size_t kHeight = 0x78;
} // namespace field

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("seaport: first %s", name);
}

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using BoolFn = uint8_t(__thiscall*)(void* self);
using Int64Fn = int64_t(__thiscall*)(void* self);
using MessageFn = void(__thiscall*)(void* base, uint32_t message);
using QueryFn = uint8_t(__thiscall*)(void* base, uint32_t iid, void** object);
using FindFn = uint8_t(__thiscall*)(void* base, void* lot, char** port);
using UsageFn = uint8_t(__thiscall*)(void* base, void* lot, uint32_t* usage, uint32_t* capacity);
using AverageFn = void(__thiscall*)(void* base, float* result);
using StreamFn = uint8_t(__thiscall*)(void* part, void* stream);
using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const uint8_t* value);
using FloodFn = void(__cdecl*)(int32_t x, int32_t z, uint32_t value, int32_t minX, int32_t minZ, int32_t maxX,
                               int32_t maxZ, void* get, void* getContext, void* set, void* setContext, void* test,
                               void* testContext);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
// The port base (shared with the airport developer, whose address file
// names it; the game's code, or simulators.airport.port's).
const auto PortDeleteThis = reinterpret_cast<SelfFn>(addr::cSC4PortDeveloper_DeleteThis);
const auto PortQueryInterface = reinterpret_cast<QueryFn>(addr::cSC4PortDeveloper_QueryInterface);
const auto PortPostMessage = reinterpret_cast<MessageFn>(addr::cSC4PortDeveloper_PostAdvice);
const auto PortGetLot = reinterpret_cast<SelfFn>(addr::cSC4PortDeveloper_Port_GetLot);
const auto PortSumCosts = reinterpret_cast<Int64Fn>(addr::cSC4PortDeveloper_SumCosts);
const auto PortFindByLot = reinterpret_cast<FindFn>(addr::cSC4PortDeveloper_FindByLot);
const auto PortGetUsageValues = reinterpret_cast<UsageFn>(addr::cSC4PortDeveloper_GetUsageValues);
const auto PortGetAvgEfficiency = reinterpret_cast<AverageFn>(addr::cSC4PortDeveloper_GetAvgEfficiency);
const auto PortGrow = reinterpret_cast<BoolFn>(addr::cSC4PortDeveloper_Grow);
const auto PortConstruct = reinterpret_cast<SelfFn>(addr::cSC4PortDeveloper_cSC4PortDeveloper);
const auto PortDestroy = reinterpret_cast<SelfFn>(addr::cSC4PortDeveloper_Destroy);
const auto PortShutdown = reinterpret_cast<BoolFn>(addr::cSC4PortDeveloper_Shutdown);
const auto PortDoMessage = reinterpret_cast<StreamFn>(addr::cSC4PortDeveloper_DoMessage);
const auto PortInit = reinterpret_cast<BoolFn>(addr::cSC4PortDeveloper_Init);
const auto PortWrite = reinterpret_cast<StreamFn>(addr::cSC4PortDeveloper_Write);
const auto PortRead = reinterpret_cast<StreamFn>(addr::cSC4PortDeveloper_Read);

char* Base(char* self) { return self + field::kBase; }

// The truncating float-to-int conversion (_ftol, 0x009EEF04): EDX:EAX.
int64_t Ftol64(double value) { return static_cast<int64_t>(value); }
int32_t Ftol(double value) { return static_cast<int32_t>(Ftol64(value)); }

// FILD of an int64, stored as a float (rounded to double first, as the FPU
// at 53 bits does).
float Int64ToFloat(int64_t value) { return static_cast<float>(static_cast<double>(value)); }

// A port's stage (0x20 bytes) in its type's table.
char* Stage(char* port)
{
    return At<char*>(At<char*>(port, 0x8), 0x14) + (static_cast<std::size_t>(At<uint32_t>(port, 0x4)) << 5);
}

// The line item 1 of the department (its slot 20), or null.
void* LineItem(char* self) { return Virtual<void*>(At<void*>(self, field::kDepartment), 0x50, 1u); }

uint8_t& Cell(char* self, uint32_t x, uint32_t z)
{
    return At<uint8_t*>(self, field::kMap)[At<uint32_t>(self, field::kWidth) * z + x];
}

// --- The sea access map -------------------------------------------------------------

struct FloodContext {
    char* self;
    uint8_t mask;
};

// 0x007010A0: the flood fill's value of a cell; always water.
uint32_t __cdecl WaterValue(int32_t, int32_t, void*)
{
    static bool logged = false;
    FirstCall("WaterValue", logged);
    return 0x80;
}

// 0x007017A0: the mask into a cell.
void __cdecl MarkCell(int32_t x, int32_t z, uint32_t, FloodContext* context)
{
    static bool logged = false;
    FirstCall("MarkCell", logged);
    Cell(context->self, static_cast<uint32_t>(x), static_cast<uint32_t>(z)) |= context->mask;
}

// 0x007017D0: water the mask has not reached.
uint8_t __cdecl IsUnmarkedWater(int32_t x, int32_t z, uint32_t, FloodContext* context)
{
    static bool logged = false;
    FirstCall("IsUnmarkedWater", logged);
    const uint8_t cell = Cell(context->self, static_cast<uint32_t>(x), static_cast<uint32_t>(z));
    return (cell & (context->mask | 0x80)) == 0x80 ? 1 : 0;
}

// One edge cell of a scan: a flood fill (the game's 0x005E3AC0, used by this
// function only) from each cell that starts a run of water.
void ScanCell(char* self, FloodContext& context, uint8_t& previous, uint8_t cell, uint32_t x, uint32_t z)
{
    const uint8_t water = cell & 0x80;
    if (water == previous)
        return;
    previous = water;
    if (!water)
        return;
    At<uint8_t>(self, field::kHasSea) = 1;
    reinterpret_cast<FloodFn>(addr::SC4_FloodFill)(
        static_cast<int32_t>(x), static_cast<int32_t>(z), 0u, 0, 0,
        static_cast<int32_t>(At<uint32_t>(self, field::kWidth)),
        static_cast<int32_t>(At<uint32_t>(self, field::kHeight)), reinterpret_cast<void*>(&WaterValue), nullptr,
        reinterpret_cast<void*>(&MarkCell), &context, reinterpret_cast<void*>(&IsUnmarkedWater), &context);
}

// 0x00701BB0: the map sized to the terrain, 0x80 where the terrain says
// water (its slot 34), then the edges z = 0 (mask 1), z = max (8), x = 0 (2)
// and x = max (4) scanned. Nothing without a terrain.
void BuildSeaAccess(char* self)
{
    static bool logged = false;
    FirstCall("BuildSeaAccess", logged);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!terrain)
        return;
    uint32_t& width = At<uint32_t>(self, field::kWidth);
    uint32_t& height = At<uint32_t>(self, field::kHeight);
    width = Virtual<uint32_t>(terrain, 0x1C);
    const uint32_t cellsZ = Virtual<uint32_t>(terrain, 0x20);
    const uint8_t none = 0;
    height = cellsZ;
    reinterpret_cast<ResizeFn>(addr::SC4ByteVector_Resize2)(self + field::kMap, width * cellsZ, &none);
    for (uint32_t z = 0; z < height; ++z)
        for (uint32_t x = 0; x < width; ++x)
            Cell(self, x, z) = Virtual<uint8_t>(terrain, 0x88, x, z) ? 0x80 : 0;

    At<uint8_t>(self, field::kHasSea) = 0;
    FloodContext context{self, 1};
    uint8_t previous = 0xFF;
    for (uint32_t x = 0; x < width; ++x)
        ScanCell(self, context, previous, Cell(self, x, 0), x, 0);
    context.mask = 8;
    previous = 0xFF;
    for (uint32_t x = 0; x < width; ++x)
        ScanCell(self, context, previous, Cell(self, x, height - 1), x, height - 1);
    previous = 0xFF;
    context.mask = 2;
    for (uint32_t z = 0; z < height; ++z)
        ScanCell(self, context, previous, Cell(self, 0, z), 0, z);
    context.mask = 4;
    previous = 0xFF;
    for (uint32_t z = 0; z < height; ++z)
        ScanCell(self, context, previous, Cell(self, width - 1, z), width - 1, z);
    At<uint8_t>(self, field::kMapStale) = 0;
}

// 0x00701710: a cell of the port lot's rectangle (inclusive) has one of the
// four edge bits.
bool HasSeaAccess(char* self, char* port)
{
    static bool logged = false;
    FirstCall("HasSeaAccess", logged);
    int32_t rect[4] = {};
    Virtual<void>(PortGetLot(port), 0x38, rect);
    for (int32_t z = rect[1]; z <= rect[3]; ++z)
        for (int32_t x = rect[0]; x <= rect[2]; ++x)
            if (At<uint8_t*>(self, field::kMap)[At<int32_t>(self, field::kWidth) * z + x] & 0xF)
                return true;
    return false;
}

// --- The occupants of a port's area ------------------------------------------------

// The area filter's slot 3 (0x007010E0): type 0xA823821E, position inside
// +0xC..+0x18 (x, z minimum, x, z maximum) and, when +0x1C is set, a holder
// of that property. The position starts as three copies of the float
// 0x00B4AFC0.
uint8_t __fastcall AreaFilterTest(char* filter, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("AreaFilter::Test", logged);
    if (Virtual<uint32_t>(occupant, 0x1C) != kOccupantType)
        return 0;
    const float start = Global<float>(addr::cSC4SeaportDeveloper_FilterStart);
    float position[3] = {start, start, start};
    Virtual<void>(occupant, 0x20, position);
    if (!(position[0] >= At<float>(filter, 0xC)) || !(position[2] >= At<float>(filter, 0x10)))
        return 0;
    if (!(position[0] <= At<float>(filter, 0x14)) || !(position[2] <= At<float>(filter, 0x18)))
        return 0;
    const uint32_t property = At<uint32_t>(filter, 0x1C);
    if (!property)
        return 1;
    void* const holder = Virtual<void*>(occupant, 0x18);
    return Virtual<uint8_t>(holder, 0xC, property) ? 1 : 0;
}

// 0x00701190: the occupants the filter accepts over the lot's cells, into
// `list` (0x00B43D0C slot 40). The rectangle's corners in world units come
// from the city's slot 96.
void GetAreaOccupants(char* self, void* lot, void* list, uint32_t property)
{
    static bool logged = false;
    FirstCall("GetAreaOccupants", logged);
    int32_t rect[4] = {};
    Virtual<void>(lot, 0x38, rect);
    void* const city = At<void*>(self, field::kCity);
    float low[2] = {}, high[2] = {};
    Virtual<void>(city, 0x180, rect[0], rect[1], &low[0], &low[1]);
    Virtual<void>(city, 0x180, rect[2] + 1, rect[3] + 1, &high[0], &high[1]);
    // The game goes on with a null filter when the allocation fails.
    char* const filter = static_cast<char*>(OperatorNew(0x20));
    if (filter) {
        reinterpret_cast<SelfFn>(addr::SC4_OccupantFilterBase)(filter);
        At<std::uintptr_t>(filter, 0x0) = addr::cSC4SeaportDeveloper_AreaFilter_vftable;
        At<std::uintptr_t>(filter, 0x4) = addr::cSC4SeaportDeveloper_AreaFilter_vftable_4;
        At<uint32_t>(filter, 0x1C) = 0;
        Virtual<uint32_t>(filter, 0x4);
    }
    At<float>(filter, 0xC) = low[0];
    At<float>(filter, 0x10) = low[1];
    At<float>(filter, 0x14) = high[0];
    At<float>(filter, 0x18) = high[1];
    At<uint32_t>(filter, 0x1C) = property;
    int32_t first[2] = {rect[0], rect[1]};
    int32_t last[2] = {rect[2], rect[3]};
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA0, list, first, last, filter, -1);
    Virtual<uint32_t>(filter, 0x8);
}

// 0x00701A60: slot 11 of interface 0xE9793A65 called with `value` on every
// occupant of the lot's area that has the property. The list is one of the
// node allocator 0x00B207E4.
void NotifyAreaOccupants(char* self, void* lot, uint32_t property, uint32_t value)
{
    static bool logged = false;
    FirstCall("NotifyAreaOccupants", logged);
    void** list = Virtual<void**>(ListNodeAllocator(), 0xC, 0xCu);
    list[0] = list;
    list[1] = list;
    GetAreaOccupants(self, lot, &list, property);
    for (void** node = static_cast<void**>(list[0]); node != list; node = static_cast<void**>(node[0])) {
        void* found = nullptr;
        Virtual<uint8_t>(node[2], 0x0, kIidAccessOccupant, &found);
        void* const occupant = found;
        if (!occupant)
            continue;
        Virtual<uint32_t>(occupant, 0x4);
        if (found)
            Virtual<uint32_t>(found, 0x8);
        Virtual<void>(occupant, 0x2C, value);
        Virtual<uint32_t>(occupant, 0x8);
    }
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &list);
    reinterpret_cast<SelfFn>(addr::SC4List_Clear2)(&list);
    Virtual<void>(ListNodeAllocator(), 0x10, list);
}

// --- Capacity and cost ---------------------------------------------------------------

// 0x007012A0: the port's stage capacity times the line item's current /
// full funding (1 without a line item or with nothing to fund), truncated,
// to 0x00B43DA8 slot 19 for the lot's property holder (slot 3) and 3.
void SetCapacity(char* self, char* port)
{
    static bool logged = false;
    FirstCall("SetCapacity", logged);
    float ratio = 1.0f;
    if (void* const item = LineItem(self)) {
        const float current = Int64ToFloat(Virtual<int64_t>(item, 0x2C));
        const double full = static_cast<double>(Virtual<int64_t>(item, 0x28));
        if (full > 0.0)
            ratio = static_cast<float>(static_cast<double>(current) / full);
    }
    void* const lot = PortGetLot(port);
    void* const service = Global<void*>(addr::SC4_Service43DA8);
    const int32_t capacity =
        Ftol(static_cast<double>(At<uint32_t>(Stage(port), 0x10)) * static_cast<double>(ratio));
    // The lot's slot 3 (its property holder) takes no argument: the game's
    // PUSH 3 before it is the service call's second argument, pushed early.
    void* const holder = Virtual<void*>(lot, 0xC);
    Virtual<void>(service, 0x4C, holder, 3u, capacity);
}

// +4 slot 13 (0x00701360).
void __fastcall SetCapacityAt4(char* base, void*, char* port)
{
    static bool logged = false;
    FirstCall("SetCapacity (+4 slot 13)", logged);
    SetCapacity(base - field::kBase, port);
}

// +4 slot 14 (0x00701370): the capacity sent and the ports marked changed;
// then, when the port type's table (+0x24: rows of uint32) has as many
// values as (x1 - x0) * (z1 - z0) of the lot's rectangle, each cell of
// [x0, x1) by [z0, z1) gets its value through the terrain's slot 51, at the
// cell's position (city slot 97, truncated). The lot's orientation (slot 23)
// picks the value: 0 row x - x0, column z - z0; 1 row z1 - 1 - z, column
// x1 - 1 - x; 2 row x1 - 1 - x, column z1 - 1 - z; 3 row z - z0, column
// x - x0.
void __fastcall OnPortPlaced(char* base, void*, char* port)
{
    static bool logged = false;
    FirstCall("OnPortPlaced", logged);
    char* const self = base - field::kBase;
    SetCapacity(self, port);
    At<uint8_t>(self, field::kPortsChanged) = 1;
    const auto Rows = [port]() { return At<char*>(At<char*>(port, 0x8), 0x24); };
    const auto RowsEnd = [port]() { return At<char*>(At<char*>(port, 0x8), 0x28); };
    if (Rows() == RowsEnd())
        return;
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    Virtual<uint32_t>(terrain, 0x4);
    void* const lot = PortGetLot(port);
    int32_t rect[4] = {};
    Virtual<void>(lot, 0x38, rect);
    const int32_t x0 = rect[0], z0 = rect[1], x1 = rect[2], z1 = rect[3];
    do {
        if (Rows() == RowsEnd())
            break;
        const int32_t rows = static_cast<int32_t>((RowsEnd() - Rows()) / 12);
        const int32_t columns = static_cast<int32_t>((At<char*>(Rows(), 0x4) - At<char*>(Rows(), 0x0)) >> 2);
        if ((z1 - z0) * (x1 - x0) != rows * columns)
            break;
        const uint32_t orientation = Virtual<uint32_t>(lot, 0x5C);
        if (orientation > 3)
            break;
        void* const city = At<void*>(self, field::kCity);
        for (int32_t z = z0; z < z1; ++z) {
            for (int32_t x = x0; x < x1; ++x) {
                // The game's outputs are a stack local and its own argument
                // slot; the call writes both.
                float fx = 0.0f, fz = 0.0f;
                Virtual<void>(city, 0x184, x, z, &fx, &fz);
                const auto Value = [&](int32_t row, int32_t column) {
                    return At<uint32_t*>(Rows(), static_cast<std::size_t>(row) * 12)[column];
                };
                uint32_t value;
                switch (orientation) {
                case 0: value = Value(x - x0, z - z0); break;
                case 1: value = Value(z1 - z - 1, x1 - x - 1); break;
                case 2: value = Value(x1 - x - 1, z1 - z - 1); break;
                default: value = Value(z - z0, x - x0); break;
                }
                const int32_t positionZ = Ftol(fz);
                const int32_t positionX = Ftol(fx);
                Virtual<void>(terrain, 0xCC, positionX, positionZ, value);
            }
        }
    } while (false);
    Virtual<uint32_t>(terrain, 0x8);
}

// +4 slot 17 (0x00700FA0): the ports' summed stage costs as the line item's
// full amount (its slot 12). Returns what the line item's Release does (0
// without one).
uint32_t __fastcall UpdateMaintenance(char* base, void*)
{
    static bool logged = false;
    FirstCall("UpdateMaintenance", logged);
    void* const item = Virtual<void*>(At<void*>(base, field::kDepartment - field::kBase), 0x50, 1u);
    if (!item)
        return 0;
    Virtual<uint32_t>(item, 0x4);
    Virtual<void>(item, 0x30, PortSumCosts(base));
    return Virtual<uint32_t>(item, 0x8);
}

// --- The interface -------------------------------------------------------------------

// Slot 5 (0x00700F40): the base's mean efficiency into `result`.
float* __fastcall GetAvgEfficiency(char* self, void*, float* result)
{
    static bool logged = false;
    FirstCall("GetAvgEfficiency", logged);
    PortGetAvgEfficiency(Base(self), result);
    return result;
}

// Slot 6 (0x00701970).
uint32_t __fastcall GetSimulatorType(char*, void*)
{
    static bool logged = false;
    FirstCall("GetSimulatorType", logged);
    return kSimulatorType;
}

// Slot 11 (0x007019E0, a jump to the list count 0x00411D50; no stack
// argument).
uint32_t __fastcall GetNumSeaports(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetNumSeaports", logged);
    void** const head = At<void**>(self, field::kPorts);
    uint32_t count = 0;
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
        ++count;
    return count;
}

// Slot 12 (0x00700FE0): the base's values of the lot's port; the capacity
// scaled by current / full funding when there is something to fund, the
// usage capped at the capacity.
uint8_t __fastcall GetSeaportUsageValues(char* self, void*, void* lot, uint32_t* usage, uint32_t* capacity)
{
    static bool logged = false;
    FirstCall("GetSeaportUsageValues", logged);
    const uint8_t found = PortGetUsageValues(Base(self), lot, usage, capacity);
    if (!found)
        return found;
    if (void* const item = LineItem(self)) {
        const float current = Int64ToFloat(Virtual<int64_t>(item, 0x2C));
        const double full = static_cast<double>(Virtual<int64_t>(item, 0x28));
        if (full > 0.0)
            *capacity = static_cast<uint32_t>(
                Ftol(static_cast<double>(*capacity) / full * static_cast<double>(current)));
    }
    if (*usage > *capacity)
        *usage = *capacity;
    return found;
}

// +4 slot 12 (0x00700F60): the port's trips, from the city's slot 69 object
// (its slot 21) for the lot's property holder (slot 3) and 3.
uint32_t __fastcall GetPortTrips(char* base, void*, char* port)
{
    static bool logged = false;
    FirstCall("GetPortTrips", logged);
    void* const counter = Virtual<void*>(At<void*>(base, field::kCity - field::kBase), 0x114);
    Virtual<uint32_t>(counter, 0x4);
    void* const lot = PortGetLot(port);
    // As in SetCapacity: the 3 is the counter call's second argument.
    void* const holder = Virtual<void*>(lot, 0xC);
    const uint32_t trips = Virtual<uint32_t>(counter, 0x54, holder, 3u);
    Virtual<uint32_t>(counter, 0x8);
    return trips;
}

// Slot 13 (0x00701800): the ports' trips (+4 slot 12) summed.
uint32_t __fastcall GetNumSeaportTrips(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetNumSeaportTrips", logged);
    void** const head = At<void**>(self, field::kPorts);
    uint32_t trips = 0;
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
        trips += Virtual<uint32_t>(Base(self), 0x30, node[2]);
    return trips;
}

// Slot 14 (0x00701830): the stage cost of the lot's port (0 without one),
// times current / full funding when there is something to fund (truncated).
int64_t __fastcall GetMaintenanceCost(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("GetMaintenanceCost", logged);
    int64_t cost = 0;
    char* port = nullptr;
    if (PortFindByLot(Base(self), lot, &port))
        cost = At<int64_t>(Stage(port), 0x8);
    if (void* const item = LineItem(self)) {
        const float current = Int64ToFloat(Virtual<int64_t>(item, 0x2C));
        const double full = static_cast<double>(Virtual<int64_t>(item, 0x28));
        if (full > 0.0)
            return Ftol64(static_cast<double>(cost) / full * static_cast<double>(current));
    }
    return cost;
}

// Slot 15 (0x00701090).
uint8_t __fastcall CityHasSea(char* self, void*)
{
    static bool logged = false;
    FirstCall("CityHasSea", logged);
    return At<uint8_t>(self, field::kHasSea);
}

// Slot 16 (0x00701780): the map's byte; no bounds check.
uint8_t __fastcall GetCellSeaAccess(char* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("GetCellSeaAccess", logged);
    return Cell(self, x, z);
}

// +4 slot 11 (0x00700F10): the base's Shutdown and deleting destructor.
void* __fastcall DeleteThis(char* base, void*)
{
    static bool logged = false;
    FirstCall("DeleteThis", logged);
    return PortDeleteThis(base);
}

// +4 slot 15 (0x00700F20): growth now when the byte 0x00B4AFBC is set
// (nothing sets it), else message 0x8A394DC4 posted. The argument is unused.
void __fastcall RequestGrowth(char* base, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("RequestGrowth", logged);
    if (Global<uint8_t>(addr::cSC4SeaportDeveloper_GrowAtOnce))
        PortGrow(base);
    else
        PortPostMessage(base, kMessageGrow);
}

// +4 slot 16 (0x00701F70): the map rebuilt when stale, every port's
// capacity sent; when the ports changed, each port without sea access
// notifies its area's occupants, and message 0x4A490D33 is posted if any
// did.
void __fastcall Update(char* base, void*)
{
    static bool logged = false;
    FirstCall("Update", logged);
    char* const self = base - field::kBase;
    if (At<uint8_t>(self, field::kMapStale))
        BuildSeaAccess(self);
    for (void** node = static_cast<void**>(At<void**>(self, field::kPorts)[0]);
         node != At<void**>(self, field::kPorts); node = static_cast<void**>(node[0]))
        SetCapacity(self, static_cast<char*>(node[2]));
    if (!At<uint8_t>(self, field::kPortsChanged))
        return;
    void** const head = At<void**>(self, field::kPorts);
    At<uint8_t>(self, field::kPortsChanged) = 0;
    bool cut = false;
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0])) {
        char* const port = static_cast<char*>(node[2]);
        if (HasSeaAccess(self, port))
            continue;
        cut = true;
        NotifyAreaOccupants(self, PortGetLot(port), kAccessProperty, 1u);
    }
    if (cut)
        PortPostMessage(base, kMessageNoAccess);
}

// --- Init, Shutdown, messages ------------------------------------------------------

// Slot 3 (0x00701E60): the base's Init; the department 0x4A4155AB of the
// budget simulator, made with its line item 1 when missing, referenced; the
// two messages; the sea access map. True when already done.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    const uint8_t done = PortInit(Base(self));
    if (!done)
        return done;
    void*& department = At<void*>(self, field::kDepartment);
    department = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, kDepartmentId);
    if (!department) {
        department = Virtual<void*>(At<void*>(self, field::kBudget), 0x64, kDepartmentId, 0xAA369059u);
        Virtual<uint32_t>(department, 0x14, 0x6A3FF01Cu, 0xCA493AF3u);
        Virtual<uint32_t>(department, 0x34, reinterpret_cast<const float*>(addr::cSC4SeaportDeveloper_FundingScale));
        void* const item = Virtual<void*>(department, 0x44, 1u, 0u);
        Virtual<uint32_t>(item, 0x48, 1u, 0u);
    }
    Virtual<uint32_t>(department, 0x4);
    void* const target = self + field::kTarget;
    Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, kMessageGrow);
    Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, kMessageTerrain);
    BuildSeaAccess(self);
    return done;
}

// Slot 4 (0x00700E90): the messages unregistered, the base shut down, the
// department released. True when not initialized.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    void* const target = self + field::kTarget;
    Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, kMessageGrow);
    Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, kMessageTerrain);
    const uint8_t done = PortShutdown(Base(self));
    if (void* const department = At<void*>(self, field::kDepartment)) {
        Virtual<uint32_t>(department, 0x8);
        At<void*>(self, field::kDepartment) = nullptr;
    }
    return done;
}

// +8 slot 3 (0x00701F20), on the part at +0x8: the terrain message rebuilds
// the map and marks the ports changed, the growth message grows the ports,
// the rest is the base's.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = target - field::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kMessageTerrain) {
        BuildSeaAccess(self);
        At<uint8_t>(self, field::kPortsChanged) = 1;
        return 1;
    }
    if (type == kMessageGrow) {
        PortGrow(Base(self));
        return 1;
    }
    return PortDoMessage(target, message);
}

// The thunks of the base's vtable at +4 (0x00702020, 0x00701990, 0x0068B2E0,
// 0x007019A0), which jump to the game's functions.
uint8_t __fastcall InitAt4(char* base, void*) { return Init(base - field::kBase, nullptr); }
uint8_t __fastcall ShutdownAt4(char* base, void*) { return Shutdown(base - field::kBase, nullptr); }
float* __fastcall GetAvgEfficiencyAt4(char* base, void*, float* result)
{
    return GetAvgEfficiency(base - field::kBase, nullptr, result);
}
uint32_t __fastcall GetSimulatorTypeAt4(char* base, void*) { return GetSimulatorType(base - field::kBase, nullptr); }

// --- Save --------------------------------------------------------------------------

// +0xC slot 3 (0x0068B3E0, the same function for the airport developer), on
// the part at +0xC: version 2, then the base's record.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(2));
    return PortWrite(part, stream);
}

// +0xC slot 4 (0x007010B0): version 2 only, then the base's record.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    // The game reads the version into the stack slot of its stream
    // argument: when the read writes nothing, the pointer's low 16 bits are
    // compared.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 2)
        return 0;
    return PortRead(part, stream);
}

// --- Installation --------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSimSlots[] = {
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 3, addr::cSC4SeaportDeveloper_Init, Init),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 4, addr::cSC4SeaportDeveloper_Shutdown, Shutdown),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 5, addr::cSC4SeaportDeveloper_GetAvgEfficiency, GetAvgEfficiency),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 6, addr::cSC4SeaportDeveloper_GetSimulatorType, GetSimulatorType),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 11, addr::cSC4SeaportDeveloper_GetNumSeaports, GetNumSeaports),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 12, addr::cSC4SeaportDeveloper_GetSeaportUsageValues,
                 GetSeaportUsageValues),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 13, addr::cSC4SeaportDeveloper_GetNumSeaportTrips,
                 GetNumSeaportTrips),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 14, addr::cSC4SeaportDeveloper_GetMaintenanceCost,
                 GetMaintenanceCost),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 15, addr::cSC4SeaportDeveloper_CityHasSea, CityHasSea),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable, 16, addr::cSC4SeaportDeveloper_GetCellSeaAccess, GetCellSeaAccess),
    // The thunks at 0x00702020, 0x00701990, 0x0068B2E0 and 0x007019A0.
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 3, 0x00702020, InitAt4),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 4, 0x00701990, ShutdownAt4),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 5, 0x0068B2E0, GetAvgEfficiencyAt4),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 6, 0x007019A0, GetSimulatorTypeAt4),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 11, addr::cSC4SeaportDeveloper_DeleteThis, DeleteThis),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 12, addr::cSC4SeaportDeveloper_GetPortTrips, GetPortTrips),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 13, addr::cSC4SeaportDeveloper_SetCapacity_4, SetCapacityAt4),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 14, addr::cSC4SeaportDeveloper_OnPortPlaced, OnPortPlaced),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 15, addr::cSC4SeaportDeveloper_RequestGrowth, RequestGrowth),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 16, addr::cSC4SeaportDeveloper_Update, Update),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_4, 17, addr::cSC4SeaportDeveloper_UpdateMaintenance,
                 UpdateMaintenance),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_8, 3, addr::cSC4SeaportDeveloper_DoMessage, DoMessage),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_AreaFilter_vftable, 3, addr::cSC4SeaportDeveloper_AreaFilter_Test,
                 AreaFilterTest),
};
const Slot kSaveSlots[] = {
    // Write is the function at 0x0068B3E0 the airport developer shares.
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_C, 3, 0x0068B3E0, Write),
    OPENSC4_SLOT(addr::cSC4SeaportDeveloper_vftable_C, 4, addr::cSC4SeaportDeveloper_Read, Read),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("seaport %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    log::Write("seaport %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallSim(registry::Mode) { return SwapSlots(kSimSlots, "sim"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.seaport.sim", "simulators/seaport", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4SeaportDeveloper_vftable,
                        "Our seaport developer: Init, Shutdown, the messages, the sea access map, the ports' "
                        "capacity and cost, the queries and the area filter (the port base stays the game's)",
                        false, InstallSim}),
    registry::Register({"simulators.seaport.save", "simulators/seaport", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4SeaportDeveloper_vftable_C,
                        "Our seaport developer Write and Read: version 2 around the port base's record", false,
                        InstallSave}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00AB2E10 (17 slots), 0x00AB2DC0 (at +4, 19 slots),
// 0x00AB2DAC (at +8) and 0x00AB2D94 (at +0xC), taken at the first
// construction, after every PostAppInit slot swap of the other groups.
constexpr int kSlots = 17, kSlots4 = 19, kSlots8 = 4, kSlotsC = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];

// As the game's (0x0068B1D0 to 0x004F7BA0 on the base).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x00700EE0): the seaport interface is the object; the
// base's QueryInterface (the game's) gives its parts, IUnknown being the
// base at +0x4.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid != kIidSeaportDeveloper)
        return PortQueryInterface(Base(self), iid, object);
    *object = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x0068B1E0 to 0x006E8F90 on the base): at the last
// reference the base's slot 11 (Shutdown, then the deleting destructor).
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<void*>(Base(self), 0x2C);
    return 0;
}

// As the game's (0x00701A10, behind +4 slot 10): the map freed, the base
// destroyed (the game's 0x006E9D30, which frees the port list's head and
// the type vectors).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("seaport object: destroying %p", static_cast<void*>(self));
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<void**>(self, 0xC) = g_vtableC;
    if (void* const map = At<void*>(self, field::kMap))
        Free(map);
    PortDestroy(Base(self));
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x00700E80).
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

void* __fastcall DeletingDestructorAt4(char* base, void*, uint32_t flags)
{
    return DeletingDestructor(base - field::kBase, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4SeaportDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4SeaportDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4SeaportDeveloper_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4SeaportDeveloper_vftable_C), sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable4[10] = reinterpret_cast<void*>(&DeletingDestructorAt4);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<0xC>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<0xC>);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("seaport object: vtables copied (slot 3 %p, +4 slot 16 %p, +8 slot 3 %p, +C slots 3/4 %p %p)",
               g_vtable[3], g_vtable4[16], g_vtable8[3], g_vtableC[3], g_vtableC[4]);
}

// The constructor (detour of 0x007018F0, reached only from the factory
// 0x007019F0): the base constructed at +0x4 (the game's 0x006E9C60, which
// allocates the port list's head), then the fields the game's sets.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    PortConstruct(Base(self));
    At<uint8_t>(self, field::kPortsChanged) = 0;
    At<uint8_t>(self, field::kMapStale) = 0;
    At<uint8_t>(self, field::kHasSea) = 0;
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<void**>(self, 0xC) = g_vtableC;
    for (std::size_t offset = 0x68; offset <= 0x78; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<void*>(self, field::kDepartment) = nullptr;
    At<uint32_t>(self, 0x38) = 0xC;
    At<uint32_t>(self, 0x18) = 0xEA29AD92;
    At<uint32_t>(self, 0x3C) = 0xEA3E904A;
    At<uint32_t>(self, 0x40) = kDepartmentId;
    At<uint32_t>(self, 0x44) = 0x1509;
    static int created = 0;
    if (created++ < 3)
        log::Write("seaport object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4SeaportDeveloper_cSC4SeaportDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.seaport.object",
    "simulators/seaport",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4SeaportDeveloper_cSC4SeaportDeveloper,
    "Our cSC4SeaportDeveloper object: constructor (detour), destructor, identity; methods from copies of the "
    "(patched) game vtables; the port base inside it stays the game's",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::seaport_sim
