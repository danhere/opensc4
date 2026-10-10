// Our cSC4PortDeveloper (build 641): the base class of the airport and the
// seaport developers, code 0x006E8F10-0x006EABA6. The airport developer
// itself and our own object are in airport_object.cpp.
//
// The base is never an object of its own: it sits at +0x4 of an airport
// developer (0x64 bytes) or a seaport developer, and its own vtables
// (0x00AB2790, 0x00AB2780, 0x00AB2768) are only set while its constructor
// and destructor run. The subclasses reach its functions by direct calls and
// jumps, so two groups replace them with detours:
//   simulators.airport.port        Init, Shutdown, the messages (ports
//                                  added and removed, the monthly update,
//                                  growth), Write, Read, the queries; and
//                                  the destructors of a port and a port type
//                                  (slot 0 of 0x00AB2760 and 0x00AB27DC)
//   simulators.airport.portobject  constructor, destructor, QueryInterface,
//                                  Release, DeleteThis
// Functions only the base itself calls are plain functions here. Left the
// game's: the base's own vtable entries nobody can reach (its deleting
// destructor 0x006E9F00, GetGZCLSID 0x006E9CE0, the identity thunks
// 0x006E9CF0-0x006E9D20) and the container helper 0x006E9150 (the copy of
// 0x20-byte elements used by the vector insert 0x006915E0).
//
// Fields (airport_sim.h) and what showed them:
//   base +0x0C uint8  port types loaded (LoadTypes sets it, Read tests it)
//        +0x0D uint8  initialized (Init, Shutdown)
//        +0x10 int32  reference count (Release compares signed)
//        +0x14 the instance ID of the exemplar listing the types; saved
//        +0x18 int32  type count (the list property's length)
//        +0x1C/+0x20/+0x24/+0x28/+0x2C  message server, zone manager, lot
//              manager, city, budget simulator (Init, from the globals)
//        +0x30 the department budget (the subclass's Init)
//        +0x34 uint32 asked of a removed lot (0x006E9EC4); low half saved
//        +0x38 data 1 of message 0x89EFA536 (Update)
//        +0x40 occupant group of port buildings (IsPortOccupant)
//        +0x44 std::list<Port*>; +0x48 std::vector<PortType*>;
//        +0x54 std::vector<uint32_t> of type exemplar IDs
//   port, port type and stage: see airport_sim.h.
#include "core/airport_sim.h"

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

namespace opensc4::airport_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("airport: first %s", name);
}

int32_t Ftol(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return 0;
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

namespace {

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidPortDeveloper = 0x4A2C4E69;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidLot = 0xE818FCED;
constexpr uint32_t kIidExemplar = 0xA52160F5;
constexpr uint32_t kMessageUpdate = 0x66956816;
constexpr uint32_t kMessageCityLoaded = 0x26D31EC1;
constexpr uint32_t kMessageOccupantInserted = 0x99EF1142;
constexpr uint32_t kMessageLotChanged = 0xE8BDA2E7;
constexpr uint32_t kMessageEfficiency = 0x89EFA536;
// The three the base asks the message server for (0x006EAA1D), in order.
constexpr uint32_t kNotifications[3] = {kMessageLotChanged, kMessageCityLoaded, kMessageOccupantInserted};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using InsertFn = void(__thiscall*)(void* vector, void* where, const void* value, const void* allocator,
                                   uint32_t count, uint32_t atEnd);
using ListValueFn = void(__thiscall*)(void* list, void* const* value);
using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
using ResizeFn = void(__thiscall*)(void* vector, uint32_t count);
using Resize2Fn = void(__thiscall*)(void* vector, uint32_t count, const uint32_t* fill);
using PushStageFn = void(__thiscall*)(void* vector, const void* stage);
using TextFn = void*(__thiscall*)(void* string, const char* text);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto RzNew = reinterpret_cast<NewFn>(addr::RZ_operator_new);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto InsertN = reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN);

// The game's cIGZString over an STL-style buffer (0x14 bytes on its stack).
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A 0x24-byte cell region: the rectangle, then the cell part 0x004422A0 builds.
struct CellRegion {
    int32_t x0, z0, x1, z1;
    uint8_t cells[0x14];
};

// The key of a resource: type, group, instance.
struct ResourceKey {
    uint32_t type, group, instance;
};

// --- The port list (std::list: head node {next, previous}, nodes {next, previous, port}) ---

char* Head(char* self) { return At<char*>(self, field::kPorts); }
char* Next(char* node) { return At<char*>(node, 0x0); }
char* PortOf(char* node) { return At<char*>(node, 0x8); }

// push_back as the game inlines it (Read, the airport's UpdateUsage): the
// node from the STL allocator, the value stored first.
void ListPushBack(char* head, void* value)
{
    char* const node = static_cast<char*>(Allocate(0xC));
    At<void*>(node, 0x8) = value;
    char* const last = At<char*>(head, 0x4);
    At<char*>(node, 0x0) = head;
    At<char*>(node, 0x4) = last;
    At<char*>(last, 0x0) = node;
    At<char*>(head, 0x4) = node;
}

// push_back of a pointer vector as the game inlines it: in place while
// there is room, else through the shared insert (0x0051CA60).
void VectorPushBack(void** vector, void* value, const void* valueAddress)
{
    if (vector[1] != vector[2]) {
        if (vector[1])
            *static_cast<void**>(vector[1]) = value;
        vector[1] = static_cast<char*>(vector[1]) + 4;
    } else {
        InsertN(vector, vector[1], valueAddress, valueAddress, 1, 1);
    }
}

// The port's current stage (the type's stages + stage * 0x20).
char* StageOf(char* port)
{
    return At<char*>(At<char*>(port, port::kType), type::kStages) + At<uint32_t>(port, port::kStage) * 0x20;
}

// (double) of an unsigned 32-bit value as the game loads it (fild, plus
// 2^32 when the sign bit is set).
double Unsigned(uint32_t value) { return static_cast<double>(value); }

// --- A port ------------------------------------------------------------------------

// 0x006E90E0, also inline in Read (0x006EAAFB).
void ConstructPort(char* port)
{
    At<std::uintptr_t>(port, 0x0) = addr::cSC4PortDeveloper_Port_vftable;
    At<uint32_t>(port, port::kStage) = 0;
    At<void*>(port, port::kType) = nullptr;
    At<uint8_t>(port, port::kUnresolved) = 0;
    At<int16_t>(port, port::kCellX) = 0;
    At<int16_t>(port, port::kCellZ) = 0;
    At<uint32_t>(port, port::kEfficiency) = Global<uint32_t>(addr::cSC4PortDeveloper_OnePercentage);
    At<uint8_t>(port, port::kReplacing) = 0;
    At<uint8_t>(port, 0x19) = 0;
    At<uint32_t>(port, port::kDemand) = 0;
    At<void*>(port, port::kLot) = nullptr;
    At<uint32_t>(port, port::kUsage) = 0;
}

// 0x006E9180: slot 0 of a port's vtable.
void* __fastcall PortDeletingDestructor(char* port, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("Port destructor", logged);
    At<std::uintptr_t>(port, 0x0) = addr::cSC4PortDeveloper_Port_vftable;
    if (void* const lot = At<void*>(port, port::kLot)) {
        Virtual<uint32_t>(lot, 0x8);
        At<void*>(port, port::kLot) = nullptr;
    }
    if (flags & 1)
        OperatorDelete(port);
    return port;
}

// The body of 0x006E91C0, which the game also inlines in eight functions:
// a port read from a save gets its lot from the lot manager's slot 6
// (cell x, cell z, 1) on first use.
void* ResolveLot(char* port)
{
    if (At<uint8_t>(port, port::kUnresolved)) {
        void* const lots = Global<void*>(addr::SC4_LotManager);
        Virtual<uint32_t>(lots, 0x4);
        void* const lot = Virtual<void*>(lots, 0x18, static_cast<int32_t>(At<int16_t>(port, port::kCellX)),
                                         static_cast<int32_t>(At<int16_t>(port, port::kCellZ)), 1u);
        At<void*>(port, port::kLot) = lot;
        if (lot)
            Virtual<uint32_t>(lot, 0x4);
        Virtual<uint32_t>(lots, 0x8);
        At<uint8_t>(port, port::kUnresolved) = 0;
    }
    return At<void*>(port, port::kLot);
}

// 0x006E9620: the port's efficiency from the funding ratio and its usage,
// stored at +0x14 and in *out. The capacity is funded first (a float); no
// usage gives 0x00B0D418 (1), no funded capacity or more usage than that
// 0x00B4A5A8 (0), else 1 - usage / funded capacity.
uint32_t* UpdateEfficiency(char* port, uint32_t* out, float funding, uint32_t usage)
{
    static bool logged = false;
    FirstCall("Port_UpdateEfficiency", logged);
    const float funded = static_cast<float>(Unsigned(At<uint32_t>(StageOf(port), stage::kCapacity)) *
                                            static_cast<double>(funding));
    const float used = static_cast<float>(Unsigned(usage));
    uint32_t bits;
    if (usage == 0) {
        bits = Global<uint32_t>(addr::cSC4PortDeveloper_OnePercentage);
    } else if (funded <= 0.0f || used > funded) {
        // funded <= 0 is the game's ordered test; NaN goes on to the division.
        bits = Global<uint32_t>(addr::cSC4PortDeveloper_ZeroPercentage);
    } else {
        const float efficiency = static_cast<float>(1.0 - static_cast<double>(used) / static_cast<double>(funded));
        std::memcpy(&bits, &efficiency, 4);
    }
    At<uint32_t>(port, port::kEfficiency) = bits;
    *out = bits;
    return out;
}

// 0x006E96E0: whether the port should grow to its next stage: it has one,
// its lot's slot 33 is not 2, and its usage (developer slot 12) is at least
// 0.96 (0x00AB2764) of its capacity.
uint8_t WantsGrowth(char* port, char* developer)
{
    static bool logged = false;
    FirstCall("Port_WantsGrowth", logged);
    char* const portType = At<char*>(port, port::kType);
    if (At<uint32_t>(port, port::kStage) + 1 >= At<uint32_t>(portType, type::kStageCount))
        return 0;
    const uint32_t usage = Virtual<uint32_t>(developer, 0x30, port);
    char* const current = StageOf(port);
    void* const lot = ResolveLot(port);
    if (Virtual<uint32_t>(lot, 0x84) == 2)
        return 0;
    const double threshold = Unsigned(At<uint32_t>(current, stage::kCapacity)) * static_cast<double>(0.96f);
    return threshold <= Unsigned(usage) ? 1 : 0;
}

// --- A port type -------------------------------------------------------------------

// 0x006EA3B0: slot 0 of a port type's vtable.
void* __fastcall TypeDeletingDestructor(char* portType, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("PortType destructor", logged);
    At<std::uintptr_t>(portType, 0x0) = addr::cSC4PortDeveloper_PortType_vftable;
    reinterpret_cast<SelfFn>(addr::SC4ShortVectorVector_Destroy)(portType + type::kTable);
    if (void* const stages = At<void*>(portType, type::kStages))
        Free(stages);
    if (flags & 1)
        OperatorDelete(portType);
    return portType;
}

// A property of the holder (slot 9), or null.
void* Property(void* holder, uint32_t id) { return Virtual<void*>(holder, 0x24, id); }
// A property's value object (slot 5).
void* Value(void* property) { return Virtual<void*>(property, 0x14); }

// 0x006EA3F0: a port type from its exemplar. False without the stage count
// property or without all four per-stage properties.
uint8_t LoadPortType(char* portType, uint32_t index, void* holder)
{
    static bool logged = false;
    FirstCall("PortType_Load", logged);
    uint8_t loaded = 0;
    uint8_t rows = 0, columns = 0;
    At<uint32_t>(portType, type::kIndex) = index;
    void* const count = Property(holder, 0xCA271780);
    if (!count)
        return 0;
    Virtual<uint32_t>(Value(count), 0x64, portType + type::kStageCount);
    void* const buildings = Property(holder, 0xCA271781);
    void* const costs = Property(holder, 0xCA271783);
    void* const capacities = Property(holder, 0xCA271784);
    void* const growCosts = Property(holder, 0xCA271786);
    if (buildings && costs && capacities && growCosts) {
        const uint32_t* building = Virtual<const uint32_t*>(Value(buildings), 0x19C);
        const uint32_t* cost = Virtual<const uint32_t*>(Value(costs), 0x1B4);
        const uint32_t* const capacity0 = Virtual<const uint32_t*>(Value(capacities), 0x19C);
        const uint32_t* const growCost0 = Virtual<const uint32_t*>(Value(growCosts), 0x1B4);
        const uint32_t* capacity = capacity0;
        const uint32_t* growCost = growCost0;
        for (uint32_t i = 0; i < At<uint32_t>(portType, type::kStageCount);
             ++i, ++building, cost += 2, ++capacity, growCost += 2) {
            // A stage built in a block of its own (0x006E8F10; +0x14 is
            // never written), copied into the vector, then freed.
            char* const block = static_cast<char*>(OperatorNew(0x20));
            if (block) {
                At<uint32_t>(block, stage::kIndex) = i;
                At<uint32_t>(block, stage::kBuilding) = *building;
                At<uint32_t>(block, stage::kCost) = cost[0];
                At<uint32_t>(block, stage::kCost + 4) = cost[1];
                At<uint32_t>(block, stage::kCapacity) = *capacity;
                At<uint32_t>(block, stage::kGrowCost) = growCost[0];
                At<uint32_t>(block, stage::kGrowCost + 4) = growCost[1];
            }
            reinterpret_cast<PushStageFn>(addr::SC4Block32Vector_PushBack)(portType + type::kStages, block);
            if (block)
                OperatorDelete(block);
        }
        loaded = 1;
    }
    if (void* const percent = Property(holder, 0xCA271785)) {
        // The game's local is whatever its stack slot held (the index, or
        // a pointer from the stage loop) when the value writes nothing.
        float value = 0.0f;
        Virtual<uint32_t>(Value(percent), 0x94, &value);
        At<float>(portType, type::kMinEfficiency) =
            static_cast<float>(static_cast<double>(value) * static_cast<double>(0.01f)); // 0x00A823A4
    }
    if (void* const size = Property(holder, 0xCA271787))
        Virtual<uint32_t>(Value(size), 0x34, portType + type::kSize);
    if (void* const property = Property(holder, 0xCA271788))
        Virtual<uint32_t>(Value(property), 0x34, &rows);
    if (void* const property = Property(holder, 0xCA271789))
        Virtual<uint32_t>(Value(property), 0x34, &columns);
    if (rows > 0 && columns > 0) {
        if (void* const property = Property(holder, 0xCA271800)) {
            void* const value = Value(property);
            const uint32_t* const data = Virtual<const uint32_t*>(value, 0x1BC);
            const uint32_t length = Virtual<uint32_t>(value, 0x10);
            if (static_cast<uint32_t>(columns) * rows == length) {
                char* const table = portType + type::kTable;
                reinterpret_cast<ResizeFn>(addr::SC4Uint32VectorVector_Resize)(table, rows);
                const uint32_t zero = 0;
                for (uint32_t i = 0; i < rows; ++i)
                    reinterpret_cast<Resize2Fn>(addr::SC4FloatVector_Resize2)(At<char*>(table, 0x0) + i * 0xC,
                                                                              columns, &zero);
                uint32_t k = 0;
                for (uint32_t i = 0; i < rows; ++i) {
                    for (uint32_t j = 0; j < columns; ++j, ++k)
                        At<uint32_t*>(At<char*>(table, 0x0), i * 0xC)[j] = data[k];
                }
            }
        }
    }
    return loaded;
}

// The exemplar (0x6534284A, 0x67CD5FA1, instance) through the resource
// manager's slot 3, or null.
void* GetExemplar(uint32_t instance)
{
    void* exemplar = nullptr;
    ResourceKey key{0x6534284A, 0x67CD5FA1, instance};
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key, kIidExemplar, &exemplar, 0u, 0u))
        return nullptr;
    return exemplar;
}

// 0x006EA740: the port type `index` from its exemplar, appended to +0x48.
uint8_t LoadType(char* self, int32_t index)
{
    static bool logged = false;
    FirstCall("LoadType", logged);
    void* const exemplar = GetExemplar(At<uint32_t*>(self, field::kTypeIds)[index]);
    if (!exemplar)
        return 0;
    uint8_t made = 0;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    char* portType = static_cast<char*>(OperatorNew(0x30));
    if (portType) {
        At<std::uintptr_t>(portType, 0x0) = addr::cSC4PortDeveloper_PortType_vftable;
        At<uint32_t>(portType, 0x4) = 0;
        At<int32_t>(portType, type::kIndex) = -1;
        At<uint32_t>(portType, type::kStageCount) = 0;
        At<uint32_t>(portType, type::kMinEfficiency) = Global<uint32_t>(addr::cSC4PortDeveloper_ZeroPercentage);
        At<uint32_t>(portType, type::kStages) = 0;
        At<uint32_t>(portType, type::kStages + 4) = 0;
        At<uint32_t>(portType, type::kStages + 8) = 0;
        At<uint8_t>(portType, type::kSize) = 2;
        At<uint32_t>(portType, type::kTable) = 0;
        At<uint32_t>(portType, type::kTable + 4) = 0;
        At<uint32_t>(portType, type::kTable + 8) = 0;
        LoadPortType(portType, static_cast<uint32_t>(index), holder);
        VectorPushBack(reinterpret_cast<void**>(self + field::kTypes), portType, &portType);
        made = 1;
    }
    Virtual<uint32_t>(exemplar, 0x8);
    return made;
}

// 0x006EA840: once, the type IDs from the list exemplar (+0x14, property
// 0x8A270FC3) and every type. Without an exemplar ID nothing happens and it
// is not marked done; a missing exemplar marks it done.
void LoadTypes(char* self)
{
    static bool logged = false;
    FirstCall("LoadTypes", logged);
    if (At<uint8_t>(self, field::kTypesLoaded))
        return;
    if (!At<uint32_t>(self, field::kTypesExemplar))
        return;
    if (void* const exemplar = GetExemplar(At<uint32_t>(self, field::kTypesExemplar))) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        if (void* const property = Property(holder, 0x8A270FC3)) {
            void* const value = Value(property);
            uint32_t* id = Virtual<uint32_t*>(value, 0x19C);
            At<int32_t>(self, field::kTypeCount) = Virtual<int32_t>(value, 0x10);
            for (int32_t i = 0; i < At<int32_t>(self, field::kTypeCount); ++i, ++id)
                VectorPushBack(reinterpret_cast<void**>(self + field::kTypeIds),
                               reinterpret_cast<void*>(static_cast<std::uintptr_t>(*id)), id);
        }
        Virtual<uint32_t>(exemplar, 0x8);
        for (int32_t i = 0; i < At<int32_t>(self, field::kTypeCount); ++i)
            LoadType(self, i);
    }
    At<uint8_t>(self, field::kTypesLoaded) = 1;
}

// --- Ports added, replaced and removed ---------------------------------------------

// 0x006E8F50: a port building: occupant type 0x278128A0 (slot 7) in the
// occupant group +0x40 (slot 22).
uint32_t IsPortOccupant(char* self, void* occupant)
{
    static bool logged = false;
    FirstCall("IsPortOccupant", logged);
    if (Virtual<uint32_t>(occupant, 0x1C) == 0x278128A0 &&
        Virtual<uint8_t>(occupant, 0x58, At<uint32_t>(self, field::kOccupantGroup)))
        return 1;
    return 0;
}

// 0x006E9250: the index of the type whose first stage is `building`
// (-1 and false when none).
uint8_t FindType(char* self, uint32_t building, int32_t* index)
{
    static bool logged = false;
    FirstCall("FindType", logged);
    *index = -1;
    const int32_t count = At<int32_t>(self, field::kTypeCount);
    char** const types = At<char**>(self, field::kTypes);
    for (int32_t i = 0; i < count; ++i) {
        char* const portType = types[i];
        if (At<uint32_t>(portType, type::kStageCount) > 0 &&
            building == At<uint32_t>(At<char*>(portType, type::kStages), stage::kBuilding)) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

// 0x006EA0E0: a port for a newly built port lot: the lot's slot 9 object
// must answer 1 from its slot 11, and its slot 3 (the lot's building
// configuration) must be the first stage of a type; else the new port is
// deleted again. Then slots 17 and 14 of the developer.
void AddPort(char* self, void* lot)
{
    static bool logged = false;
    FirstCall("AddPort", logged);
    void* const configuration = Virtual<void*>(lot, 0x24);
    if (!configuration)
        return;
    if (Virtual<uint8_t>(configuration, 0x2C) != 1)
        return;
    char* port = static_cast<char*>(OperatorNew(0x28));
    if (port)
        ConstructPort(port);
    int32_t x = 0, z = 0; // not set by the game before the call
    Virtual<uint32_t>(lot, 0x54, &x, &z);
    At<void*>(port, port::kLot) = lot;
    At<int16_t>(port, port::kCellX) = static_cast<int16_t>(x);
    At<int16_t>(port, port::kCellZ) = static_cast<int16_t>(z);
    Virtual<uint32_t>(lot, 0x4);
    int32_t index = 0;
    const uint32_t building = Virtual<uint32_t>(configuration, 0xC);
    if (FindType(self, building, &index)) {
        At<char*>(port, port::kType) = At<char**>(self, field::kTypes)[index];
        reinterpret_cast<ListValueFn>(addr::SC4StlList_PushBack)(self + field::kPorts,
                                                                 reinterpret_cast<void* const*>(&port));
        Virtual<uint32_t>(self, 0x44);
        Virtual<uint32_t>(self, 0x38, port);
    } else {
        Virtual<void*>(port, 0x0, 1u);
    }
}

// 0x006E92A0: the port's lot replaced by one of the configuration
// `building` on the same rectangle. 0x00B43D18's slot 8 gives the
// configuration, 0x00B43D68's slot 36 the object that removes the old lot
// (slots 12, 13) and finishes the new one (slot 7); the lot manager's slot
// 23 makes the lot. The width and depth are computed in bytes as the game
// does. While it runs the port is marked +0x18, so the removal message of
// the old lot does not remove the port.
uint8_t ReplaceLot(char* self, char* port, uint32_t building)
{
    static bool logged = false;
    FirstCall("ReplaceLot", logged);
    void* const lot = ResolveLot(port);
    void* const configuration = Virtual<void*>(Global<void*>(addr::SC4_Service43D18), 0x20, building);
    void* const developer = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
    Virtual<uint32_t>(developer, 0x4);
    At<uint8_t>(port, port::kReplacing) = 1;
    const uint32_t facing = Virtual<uint32_t>(lot, 0x5C);
    int32_t rect[4] = {}; // not set by the game before the call
    Virtual<uint32_t>(lot, 0x38, rect);
    const uint8_t width = static_cast<uint8_t>(static_cast<uint8_t>(rect[2]) - static_cast<uint8_t>(rect[0]) + 1);
    const uint8_t depth = static_cast<uint8_t>(static_cast<uint8_t>(rect[3]) - static_cast<uint8_t>(rect[1]) + 1);
    Virtual<uint32_t>(developer, 0x30, lot);
    Virtual<uint32_t>(developer, 0x34, lot);
    Virtual<uint32_t>(lot, 0x8);
    At<void*>(port, port::kLot) = nullptr;
    void* created = nullptr;
    uint8_t done = Virtual<uint8_t>(At<void*>(self, field::kLots), 0x5C, rect[0], rect[1],
                                    static_cast<uint32_t>(width), static_cast<uint32_t>(depth), facing, &created);
    if (done) {
        At<void*>(port, port::kLot) = created;
        Virtual<uint32_t>(created, 0x28, configuration);
        Virtual<uint32_t>(created, 0x80);
        done = Virtual<uint8_t>(developer, 0x1C, created, configuration, 0u, 1u);
    }
    Virtual<uint32_t>(developer, 0x8);
    return done;
}

// 0x006E9D70: the port's building leaves the demand (0x00B43D74 slot 7:
// the building, its last value, 0), its lot's rectangle is dezoned through
// the zone manager's slot 11, the port leaves the list and is deleted;
// then slot 17.
void RemovePort(char* self, char* port)
{
    static bool logged = false;
    FirstCall("RemovePort", logged);
    ResolveLot(port);
    void* const demand = Global<void*>(addr::SC4_Service43D74);
    void* const lot = At<void*>(port, port::kLot);
    uint32_t last = At<uint32_t>(port, port::kDemand);
    void* const building = Virtual<void*>(lot, 0xB8, 0u);
    Virtual<uint32_t>(demand, 0x1C, building, &last,
                      reinterpret_cast<void*>(addr::cSC4PortDeveloper_ZeroPercentage));
    At<uint32_t>(port, port::kDemand) = 0;
    int32_t rect[4] = {}; // not set by the game before the call
    Virtual<uint32_t>(lot, 0x38, rect);
    CellRegion region;
    const uint8_t fill = 1;
    region.x1 = rect[2];
    region.z1 = rect[3];
    region.x0 = rect[0];
    region.z0 = rect[1];
    reinterpret_cast<CellsFn>(addr::SC4CellRegion_ConstructCells)(
        region.cells, static_cast<uint32_t>(rect[2] - rect[0] + 1), static_cast<uint32_t>(rect[3] - rect[1] + 1),
        &fill);
    Virtual<uint32_t>(At<void*>(self, field::kZones), 0x2C, &region, 0u, 1u, 1u, 0u, 1u, 0u, 0u, 0u, 0u);
    *reinterpret_cast<std::uintptr_t*>(region.cells) = addr::SC4CellRegion_CellsVtable;
    reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells)(region.cells);
    // The list's remove (0x00809580, shared).
    reinterpret_cast<ListValueFn>(addr::SC4ListIterator_FreeList)(self + field::kPorts,
                                                                  reinterpret_cast<void* const*>(&port));
    Virtual<void*>(port, 0x0, 1u);
    Virtual<uint32_t>(self, 0x44);
}

// 0x006E9400: the port whose lot is at the cell (slot 21) of `lot`.
char* FindByCell(char* self, void* lot)
{
    static bool logged = false;
    FirstCall("FindByCell", logged);
    int32_t x = 0, z = 0; // not set by the game before the calls
    Virtual<uint32_t>(lot, 0x54, &x, &z);
    for (char* node = Next(Head(self)); node != Head(self); node = Next(node)) {
        char* const port = PortOf(node);
        int32_t portX = 0, portZ = 0;
        Virtual<uint32_t>(ResolveLot(port), 0x54, &portX, &portZ);
        if (portX == x && portZ == z)
            return port;
    }
    return nullptr;
}

// 0x006E9EA0: a lot changed (message 0xE8BDA2E7; only data 2 = 5 counts):
// if its slot 9 object says yes to +0x34 (slot 15) and a port is at its
// cell, the port is removed, unless it is being replaced.
void OnLotRemoved(char* self, void* lot, uint32_t /*data1*/, uint32_t data2)
{
    static bool logged = false;
    FirstCall("OnLotRemoved", logged);
    if (data2 != 5 || !lot)
        return;
    void* const configuration = Virtual<void*>(lot, 0x24);
    if (!configuration)
        return;
    if (!Virtual<uint8_t>(configuration, 0x3C, At<uint32_t>(self, field::kLotGroup)))
        return;
    char* const port = FindByCell(self, lot);
    if (!port)
        return;
    if (!At<uint8_t>(port, port::kReplacing))
        RemovePort(self, port);
    else
        At<uint8_t>(port, port::kReplacing) = 0;
}

// --- The monthly update --------------------------------------------------------------

// 0x006E9070: message 0x89EFA536 with two data words, sent through the
// message server's slot 3.
void SendEfficiencyMessage(char* self, uint32_t data1, uint32_t data2)
{
    static bool logged = false;
    FirstCall("SendEfficiencyMessage", logged);
    if (!At<void*>(self, field::kMessages))
        return;
    void* const memory = RzNew(0x2C);
    void* const message = memory ? reinterpret_cast<SelfFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, kMessageEfficiency);
    Virtual<uint32_t>(message, 0x2C, data1);
    Virtual<uint32_t>(message, 0x40, data2);
    Virtual<uint32_t>(At<void*>(self, field::kMessages), 0xC, message);
    Virtual<uint32_t>(message, 0x8);
}

// 0x006E9980 (message 0x66956816, after the subclass's slot 16 set the
// usage). The funding ratio is slot 18 (int64) as a float over SumCosts.
// Per port, unless its lot's slot 35 is 1: its efficiency; its usage over
// capacity given to the demand (0x00B43D74 slot 7: the lot's slot 46
// building, the last value, the new one); its lot's slot 36 set to 2 when
// its slot 40 is above 90 and either the efficiency is not above the type's
// minimum or there is no funding, else to 0; and, for the lot states (slot
// 33) 1, 3 and 4, a growth request (slot 15) for the first port that wants
// one. At the end message 0x89EFA536 with the average efficiency in
// percent, rounded by the game's float trick (x * 100 + 1.5 * 2^23 as a
// float, its bits minus that constant's).
void Update(char* self)
{
    static bool logged = false;
    FirstCall("Update", logged);
    if (Next(Head(self)) == Head(self))
        return;
    bool requested = false;
    const float funding = static_cast<float>(Virtual<int64_t>(self, 0x48));
    const float ratio = static_cast<float>(static_cast<double>(funding) /
                                           static_cast<double>(base::SumCosts(self, nullptr)));
    void* const demand = Global<void*>(addr::SC4_Service43D74);
    for (char* node = Next(Head(self)); node != Head(self); node = Next(node)) {
        char* const port = PortOf(node);
        void* const lot = ResolveLot(port);
        const int32_t state = Virtual<int32_t>(lot, 0x84);
        if (Virtual<uint32_t>(lot, 0x8C) == 1)
            continue;
        const uint32_t portUsage = Virtual<uint32_t>(self, 0x30, port);
        uint32_t efficiencyBits = 0;
        UpdateEfficiency(port, &efficiencyBits, ratio, portUsage);
        float efficiency;
        std::memcpy(&efficiency, &efficiencyBits, 4);
        uint32_t usage = 0, capacity = 0;
        base::GetUsageValues(self, nullptr, lot, &usage, &capacity);
        const float share = static_cast<float>(Unsigned(usage) / Unsigned(capacity));
        uint32_t last = At<uint32_t>(port, port::kDemand);
        float next = share;
        void* const building = Virtual<void*>(lot, 0xB8, 0u);
        Virtual<uint32_t>(demand, 0x1C, building, &last, &next);
        At<float>(port, port::kDemand) = share;
        const float minimum = At<float>(At<char*>(port, port::kType), type::kMinEfficiency);
        bool distressed = false;
        if (minimum >= efficiency && Virtual<uint32_t>(lot, 0xA0) > 0x5A)
            distressed = true;
        else if (!(funding > 0.0f) && Virtual<uint32_t>(lot, 0xA0) > 0x5A)
            distressed = true;
        Virtual<uint32_t>(lot, 0x90, distressed ? 2u : 0u);
        if (state == 1 || state == 3 || state == 4) {
            if (WantsGrowth(port, self) && !requested) {
                Virtual<uint32_t>(self, 0x3C, port);
                requested = true;
            }
        }
    }
    float average = 0.0f; // the game's scratch slot
    const float* const result = Virtual<const float*>(self, 0x14, &average);
    // The constants 0x00A80AA0 (100) and 0x00AB2758 (1.5 * 2^23).
    const float rounded = static_cast<float>(static_cast<double>(*result) * 100.0 + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &rounded, 4);
    SendEfficiencyMessage(self, At<uint32_t>(self, field::kMessageData), bits + 0xB4C00000u);
}

// --- Installation -------------------------------------------------------------------

bool SwapSlot(std::uintptr_t vtableAddress, std::uintptr_t original, void* ours, const char* what)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress);
    if (*slot != original) {
        log::Write("airport port: the %s slot holds %08X, expected %08X; not installing", what,
                   static_cast<unsigned>(*slot), static_cast<unsigned>(original));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], void* (&originals)[N])
{
    for (std::size_t i = 0; i < N; ++i) {
        const addr::Entry* const entry = addr_check::Find(detours[i].address);
        if (!entry || !hook::Install(*entry, detours[i].ours, &originals[i])) {
            log::Write("airport port: no detour at %08X", static_cast<unsigned>(detours[i].address));
            return false;
        }
    }
    return true;
}

#define OPENSC4_DETOUR(name, fn) {addr::cSC4PortDeveloper_##name, reinterpret_cast<void*>(&fn)}

bool InstallPort(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    // Checked at the call sites of every detoured function (the airport's
    // and the seaport's code): none keeps a value in ECX, EDX or EAX across
    // the call or passes anything outside __thiscall.
    static const Detour detours[] = {
        OPENSC4_DETOUR(Init, base::Init),
        OPENSC4_DETOUR(Shutdown, base::Shutdown),
        OPENSC4_DETOUR(DoMessage, base::DoMessage),
        OPENSC4_DETOUR(Write, base::Write),
        OPENSC4_DETOUR(Read, base::Read),
        OPENSC4_DETOUR(GetAvgEfficiency, base::GetAvgEfficiency),
        OPENSC4_DETOUR(GetLeastEfficientPort, base::GetLeastEfficientPort),
        OPENSC4_DETOUR(GetAllPortLots, base::GetAllPortLots),
        OPENSC4_DETOUR(GetNumPorts, base::GetNumPorts),
        OPENSC4_DETOUR(GetNumPortsOfSize, base::GetNumPortsOfSize),
        OPENSC4_DETOUR(PostAdvice, base::PostAdvice),
        OPENSC4_DETOUR(Port_GetLot, base::GetLot),
        OPENSC4_DETOUR(SumCosts, base::SumCosts),
        OPENSC4_DETOUR(FindByLot, base::FindByLot),
        OPENSC4_DETOUR(GetUsageValues, base::GetUsageValues),
        OPENSC4_DETOUR(Grow, base::Grow),
    };
    static void* originals[sizeof(detours) / sizeof(detours[0])] = {};
    if (!SwapSlot(addr::cSC4PortDeveloper_Port_vftable, addr::cSC4PortDeveloper_Port_DeletingDestructor,
                  reinterpret_cast<void*>(&PortDeletingDestructor), "port destructor") ||
        !SwapSlot(addr::cSC4PortDeveloper_PortType_vftable, addr::cSC4PortDeveloper_PortType_DeletingDestructor,
                  reinterpret_cast<void*>(&TypeDeletingDestructor), "port type destructor"))
        return false;
    if (!InstallDetours(detours, originals))
        return false;
    log::Write("airport port: %u base functions and 2 destructor slots are ours",
               static_cast<unsigned>(sizeof(detours) / sizeof(detours[0])));
    return true;
}

bool InstallPortObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const Detour detours[] = {
        OPENSC4_DETOUR(cSC4PortDeveloper, base::Construct),
        OPENSC4_DETOUR(Destroy, base::Destroy),
        OPENSC4_DETOUR(QueryInterface, base::QueryInterface),
        OPENSC4_DETOUR(Release, base::Release),
        OPENSC4_DETOUR(DeleteThis, base::DeleteThis),
    };
    static void* originals[sizeof(detours) / sizeof(detours[0])] = {};
    if (!InstallDetours(detours, originals))
        return false;
    log::Write("airport port: the base's constructor, destructor and identity are ours");
    return true;
}

#undef OPENSC4_DETOUR

const bool g_registered[] = {
    registry::Register({"simulators.airport.port", "simulators/airport", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PortDeveloper_Init,
                        "Our cSC4PortDeveloper, the base of the airport and seaport developers: Init, Shutdown, "
                        "messages (ports added, removed, updated, grown), Write, Read, queries (16 detours), the "
                        "port and port type destructors (src/core/airport_sim.cpp)",
                        false, InstallPort}),
    registry::Register({"simulators.airport.portobject", "simulators/airport", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PortDeveloper_cSC4PortDeveloper,
                        "Our cSC4PortDeveloper constructor, destructor, QueryInterface, Release and DeleteThis "
                        "(5 detours; src/core/airport_sim.cpp)",
                        false, InstallPortObject}),
};

} // namespace

// --- The functions the subclasses call ------------------------------------------------

namespace base {

// 0x006E9C60. The cIGZUnknown-level vtables the game writes first are
// skipped; +0x34 to +0x3C are left to the subclass, as the game leaves them.
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("port Construct", logged);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4PortDeveloper_vftable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4PortDeveloper_vftable_4;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4PortDeveloper_vftable_8;
    At<uint8_t>(self, field::kTypesLoaded) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    for (std::size_t offset = 0x10; offset <= 0x30; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kOccupantGroup) = 0xFFFFFFFF;
    At<void*>(self, field::kPorts) = nullptr;
    char* const head = static_cast<char*>(Allocate(0xC));
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    At<char*>(self, field::kPorts) = head;
    for (std::size_t offset = 0x48; offset <= 0x5C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    return self;
}

// 0x006E9D30: the two vectors' storage and the port list (nodes and head;
// 0x005650A0, shared). The ports and types themselves are Shutdown's.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("port Destroy", logged);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4PortDeveloper_vftable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4PortDeveloper_vftable_4;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4PortDeveloper_vftable_8;
    if (void* const ids = At<void*>(self, field::kTypeIds))
        Free(ids);
    if (void* const types = At<void*>(self, field::kTypes))
        Free(types);
    reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy)(self + field::kPorts);
}

// 0x006E8FD0. AddRef through the part's own slot 1.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("port QueryInterface", logged);
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidPortDeveloper: offset = 0; break;
    case kIidMessageTarget2: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// 0x006E8F90: slot 11 (DeleteThis) at the last reference.
uint32_t __fastcall Release(char* self, void*)
{
    static bool logged = false;
    FirstCall("port Release", logged);
    int32_t& references = At<int32_t>(self, field::kRefCount);
    if (references > 1)
        return static_cast<uint32_t>(--references);
    Virtual<void*>(self, 0x2C);
    return 0;
}

// 0x006E8FB0: Shutdown (slot 4), then the deleting destructor (slot 10).
void* __fastcall DeleteThis(char* self, void*)
{
    static bool logged = false;
    FirstCall("port DeleteThis", logged);
    Virtual<uint8_t>(self, 0x10);
    return Virtual<void*>(self, 0x28, 1u);
}

// 0x006EA950: once. The types, the registration with the simulator service
// (slot 19: target, 6, name, 2), the services each referenced when not set
// yet, and with the message server the three notifications.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("port Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    LoadTypes(self);
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    char* const target = self + 0x4;
    GameString name{};
    void* const text = reinterpret_cast<TextFn>(addr::SC4String_FromText)(
        &name, reinterpret_cast<const char*>(addr::cSC4PortDeveloper_Name));
    Virtual<uint32_t>(simulators, 0x4C, target, 6u, text, 2u);
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        Free(name.begin);
    const struct {
        std::size_t field;
        std::uintptr_t global;
    } services[] = {{field::kCity, addr::SC4_City},
                    {field::kZones, addr::SC4_ZoneManager},
                    {field::kLots, addr::SC4_LotManager},
                    {field::kBudget, addr::SC4_BudgetSimulator}};
    for (const auto& service : services) {
        if (!At<void*>(self, service.field)) {
            void* const object = Global<void*>(service.global);
            At<void*>(self, service.field) = object;
            Virtual<uint32_t>(object, 0x4);
        }
    }
    if (!At<void*>(self, field::kMessages)) {
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, field::kMessages) = messages;
        Virtual<uint32_t>(messages, 0x4);
        for (const uint32_t type : kNotifications)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, type);
    }
    return 1;
}

// 0x006E9F60: the notifications removed and the message server released;
// every port deleted (an unresolved one fetches its lot first, as the game
// does) and the list emptied; every type deleted and the vector emptied;
// the budget simulator, lot manager, zone manager and city released; the
// target removed from the simulator (the global city's slot 47, its slot 20).
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("port Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    char* const target = self + 0x4;
    if (void* const messages = At<void*>(self, field::kMessages)) {
        for (const uint32_t type : kNotifications)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, type);
        Virtual<uint32_t>(messages, 0x8);
        At<void*>(self, field::kMessages) = nullptr;
    }
    for (char* node = Next(Head(self)); node != Head(self); node = Next(node)) {
        char* const port = PortOf(node);
        ResolveLot(port);
        Virtual<void*>(port, 0x0, 1u);
    }
    for (char* node = Next(Head(self)); node != Head(self);) {
        char* const next = Next(node);
        Free(node);
        node = next;
    }
    At<char*>(Head(self), 0x0) = Head(self);
    At<char*>(Head(self), 0x4) = Head(self);
    for (char** entry = At<char**>(self, field::kTypes); entry != At<char**>(self, field::kTypes + 4); ++entry)
        if (*entry)
            Virtual<void*>(*entry, 0x0, 1u);
    At<char**>(self, field::kTypes + 4) = At<char**>(self, field::kTypes);
    for (const std::size_t offset : {field::kBudget, field::kLots, field::kZones, field::kCity}) {
        if (void* const object = At<void*>(self, offset)) {
            Virtual<uint32_t>(object, 0x8);
            At<void*>(self, offset) = nullptr;
        }
    }
    if (void* const simulator = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulator, 0x50, target);
    return 1;
}

// 0x006EA1C0 (`target` = self + 4); always true.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    static bool logged = false;
    FirstCall("port DoMessage", logged);
    char* const self = target - 0x4;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case kMessageUpdate:
        Virtual<uint32_t>(self, 0x40);
        Update(self);
        break;
    case kMessageCityLoaded:
        for (char* node = Next(Head(self)); node != Head(self); node = Next(node))
            ResolveLot(PortOf(node));
        break;
    case kMessageOccupantInserted: {
        // The occupant (void 1) is used without a null test, as in the game.
        void* const occupant = Virtual<void*>(message, 0x28);
        if (occupant)
            Virtual<uint32_t>(occupant, 0x4);
        void* const info = Virtual<void*>(occupant, 0x18);
        if (!Virtual<uint8_t>(info, 0xC, 0x029244DBu)) {
            if (static_cast<uint8_t>(IsPortOccupant(self, occupant))) {
                if (void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x38, occupant))
                    AddPort(self, lot);
            }
        }
        Virtual<uint32_t>(occupant, 0x8);
        break;
    }
    case kMessageLotChanged: {
        const uint32_t data1 = Virtual<uint32_t>(message, 0x24);
        const uint32_t data2 = Virtual<uint32_t>(message, 0x38);
        void* lot = nullptr;
        void* const unknown = Virtual<void*>(message, 0x80);
        Virtual<uint8_t>(unknown, 0x0, kIidLot, &lot);
        if (lot) {
            OnLotRemoved(self, lot, data1, data2);
            Virtual<uint32_t>(lot, 0x8);
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

// 0x006E9850 (`part` = self + 8): version 1, the list exemplar's ID, the
// low half of +0x34, the port count, then per port its stage, its type's
// index and its cell. The 16-bit values go out zero-extended.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("port Write", logged);
    char* const self = part - 0x8;
    Virtual<uint8_t>(stream, 0x1C, 1u);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kTypesExemplar));
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint32_t>(At<uint16_t>(self, field::kLotGroup)));
    uint32_t count = 0;
    for (char* node = Next(Head(self)); node != Head(self); node = Next(node))
        ++count;
    Virtual<uint8_t>(stream, 0x1C, count);
    if (static_cast<uint16_t>(count) == 0)
        return 1;
    for (char* node = Next(Head(self)); node != Head(self); node = Next(node)) {
        char* const port = PortOf(node);
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(port, port::kStage));
        Virtual<uint8_t>(stream, 0x1C,
                         static_cast<uint32_t>(At<uint16_t>(At<char*>(port, port::kType), type::kIndex)));
        Virtual<uint8_t>(stream, 0x18, static_cast<uint32_t>(At<uint16_t>(port, port::kCellX)));
        Virtual<uint8_t>(stream, 0x18, static_cast<uint32_t>(At<uint16_t>(port, port::kCellZ)));
    }
    return 1;
}

// 0x006EAA60 (`part` = self + 8): version 1 only. The types are loaded
// first when Init has not run yet; each port is created unresolved (its lot
// is fetched at its cell on first use) and appended to the list.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("port Read", logged);
    char* const self = part - 0x8;
    // The game reads the version into the stack slot of its stream
    // argument: when the read writes nothing, the pointer's low 16 bits
    // are compared.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    Virtual<uint8_t>(stream, 0x24, self + field::kTypesExemplar);
    // These three locals are not set by the game before the reads.
    uint16_t lotGroup = 0, count = 0, typeIndex = 0;
    Virtual<uint8_t>(stream, 0x1C, &lotGroup);
    At<uint32_t>(self, field::kLotGroup) = lotGroup;
    Virtual<uint8_t>(stream, 0x1C, &count);
    if (!At<uint8_t>(self, field::kTypesLoaded))
        LoadTypes(self);
    while (count > 0) {
        --count;
        char* const port = static_cast<char*>(OperatorNew(0x28));
        if (port)
            ConstructPort(port);
        At<uint8_t>(port, port::kUnresolved) = 1;
        Virtual<uint8_t>(stream, 0x24, port + port::kStage);
        Virtual<uint8_t>(stream, 0x1C, &typeIndex);
        At<char*>(port, port::kType) = At<char**>(self, field::kTypes)[typeIndex];
        Virtual<uint8_t>(stream, 0x18, port + port::kCellX);
        Virtual<uint8_t>(stream, 0x18, port + port::kCellZ);
        ListPushBack(Head(self), port);
    }
    return 1;
}

// 0x006E9900 (slot 5): the mean of the ports' efficiencies, summed as the
// x87 does (in double), or 0x00B0D418 (1) without ports. Returns `out`.
float* __fastcall GetAvgEfficiency(char* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("port GetAvgEfficiency", logged);
    char* const head = Head(self);
    if (Next(head) == head) {
        std::memcpy(out, reinterpret_cast<const void*>(addr::cSC4PortDeveloper_OnePercentage), 4);
        return out;
    }
    uint32_t count = 0;
    for (char* node = Next(head); node != head; node = Next(node))
        ++count;
    double sum = 0.0;
    for (char* node = Next(head); node != head; node = Next(node))
        sum += static_cast<double>(At<float>(PortOf(node), port::kEfficiency));
    *out = static_cast<float>(sum / Unsigned(count));
    return out;
}

// 0x006E9520 (slot 7). No port: false. One port: its efficiency and its
// lot's cell (slot 21), true. Two or more: the game's search loop compares
// the first two efficiencies without ever advancing (0x006E9550: both of
// its branches jump back to the compare) and never returns; kept.
uint8_t __fastcall GetLeastEfficientPort(char* self, void*, float* efficiency, int32_t* cell)
{
    static bool logged = false;
    FirstCall("port GetLeastEfficientPort", logged);
    char* const head = Head(self);
    char* const first = Next(head);
    if (first == head)
        return 0;
    char* const port = PortOf(first);
    const uint32_t bits = At<uint32_t>(port, port::kEfficiency);
    if (Next(first) != head) {
        log::Write("airport: GetLeastEfficientPort with several ports never returns in the game either");
        for (volatile bool forever = true; forever;) {
        }
    }
    void* const lot = ResolveLot(port);
    Virtual<uint32_t>(lot, 0x54, cell, cell + 1);
    std::memcpy(efficiency, &bits, 4);
    return 1;
}

// 0x006EA2F0 (slot 8): every port's lot, referenced (no null test), appended.
void __fastcall GetAllPortLots(char* self, void*, void** vector)
{
    static bool logged = false;
    FirstCall("port GetAllPortLots", logged);
    char* const head = Head(self);
    for (char* node = Next(head); node != head; node = Next(node)) {
        void* lot = ResolveLot(PortOf(node));
        Virtual<uint32_t>(lot, 0x4);
        VectorPushBack(vector, lot, &lot);
    }
}

// 0x006E94C0 (slot 9): the ports whose type's size bit is in `sizes` and
// whose stage bit is in `stages` (shift counts taken modulo 32, as x86 does).
uint32_t __fastcall GetNumPorts(char* self, void*, uint32_t sizes, uint32_t stages)
{
    static bool logged = false;
    FirstCall("port GetNumPorts", logged);
    char* const head = Head(self);
    uint32_t count = 0;
    for (char* node = Next(head); node != head; node = Next(node)) {
        char* const port = PortOf(node);
        const uint32_t size = At<uint8_t>(At<char*>(port, port::kType), type::kSize);
        if ((sizes & (1u << (size & 31))) && (stages & (1u << (At<uint32_t>(port, port::kStage) & 31))))
            ++count;
    }
    return count;
}

// 0x006E9050: slot 9 for one size and every stage.
uint32_t __fastcall GetNumPortsOfSize(char* self, void*, uint32_t size)
{
    static bool logged = false;
    FirstCall("port GetNumPortsOfSize", logged);
    return Virtual<uint32_t>(self, 0x24, 1u << (size & 31), 0x1Fu);
}

// 0x006E9120: the city's slot 81 object, referenced for the call, its slot
// 15 with (id, 0).
void __fastcall PostAdvice(char* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("port PostAdvice", logged);
    void* const advisor = Virtual<void*>(At<void*>(self, field::kCity), 0x144);
    Virtual<uint32_t>(advisor, 0x4);
    Virtual<uint32_t>(advisor, 0x3C, id, 0u);
    Virtual<uint32_t>(advisor, 0x8);
}

// 0x006E91C0.
void* __fastcall GetLot(char* port, void*)
{
    static bool logged = false;
    FirstCall("Port_GetLot", logged);
    return ResolveLot(port);
}

// 0x006E9210: the sum of the ports' current stages' costs.
int64_t __fastcall SumCosts(char* self, void*)
{
    static bool logged = false;
    FirstCall("port SumCosts", logged);
    char* const head = Head(self);
    int64_t sum = 0;
    for (char* node = Next(head); node != head; node = Next(node))
        sum += At<int64_t>(StageOf(PortOf(node)), stage::kCost);
    return sum;
}

// 0x006E9590: the port of a lot (every port before it gets its lot fetched).
uint8_t __fastcall FindByLot(char* self, void*, void* lot, char** port)
{
    static bool logged = false;
    FirstCall("port FindByLot", logged);
    *port = nullptr;
    char* const head = Head(self);
    for (char* node = Next(head); node != head; node = Next(node)) {
        char* const candidate = PortOf(node);
        ResolveLot(candidate);
        if (lot == At<void*>(candidate, port::kLot)) {
            *port = candidate;
            return 1;
        }
    }
    return 0;
}

// 0x006E9770: the usage (slot 12) and capacity of the lot's port; the
// usage is at most the capacity (also when no port was found: both 0).
uint8_t __fastcall GetUsageValues(char* self, void*, void* lot, uint32_t* usage, uint32_t* capacity)
{
    static bool logged = false;
    FirstCall("port GetUsageValues", logged);
    *capacity = 0;
    *usage = 0;
    uint8_t found = 0;
    if (lot) {
        char* const head = Head(self);
        for (char* node = Next(head); node != head; node = Next(node)) {
            char* const port = PortOf(node);
            ResolveLot(port);
            if (At<void*>(port, port::kLot) == lot) {
                *usage = Virtual<uint32_t>(self, 0x30, port);
                *capacity = At<uint32_t>(StageOf(port), stage::kCapacity);
                found = 1;
                break;
            }
        }
    }
    if (*usage > *capacity)
        *usage = *capacity;
    return found;
}

// 0x006E9BD0: every port that wants growth and has a next stage gets that
// stage's lot (ReplaceLot); when that works its stage goes up, the budget
// simulator's slot 8 gets the stage's int64 at +0x18, then slots 13 (the
// port) and 17. Always true.
uint8_t __fastcall Grow(char* self, void*)
{
    static bool logged = false;
    FirstCall("port Grow", logged);
    for (char* node = Next(Head(self)); node != Head(self); node = Next(node)) {
        char* const port = PortOf(node);
        if (!WantsGrowth(port, self))
            continue;
        char* const portType = At<char*>(port, port::kType);
        const uint32_t nextStage = At<uint32_t>(port, port::kStage) + 1;
        if (nextStage >= At<uint32_t>(portType, type::kStageCount))
            continue;
        char* const next = At<char*>(portType, type::kStages) + nextStage * 0x20;
        if (!ReplaceLot(self, port, At<uint32_t>(next, stage::kBuilding)))
            continue;
        const uint32_t grown = At<uint32_t>(port, port::kStage) + 1;
        if (grown < At<uint32_t>(At<char*>(port, port::kType), type::kStageCount))
            At<uint32_t>(port, port::kStage) = grown;
        Virtual<uint32_t>(At<void*>(self, field::kBudget), 0x20, At<uint32_t>(next, stage::kGrowCost),
                          At<uint32_t>(next, stage::kGrowCost + 4));
        Virtual<uint32_t>(self, 0x34, port);
        Virtual<uint32_t>(self, 0x44);
    }
    return 1;
}

} // namespace base

} // namespace opensc4::airport_sim
