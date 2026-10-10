// Our cSC4AirportDeveloper (build 641; class ID 0xAA2049A1): its own code
// at 0x0068AFD0-0x0068B95B plus six functions the linker folded with the
// seaport developer's (0x00700F40, 0x00700FA0, 0x00700FE0, 0x007010B0,
// 0x00701800, 0x00701830). The port base it derives from is in
// airport_sim.cpp.
//
// The object is 0x64 bytes: +0x0 the primary cISC4AirportDeveloper vtable
// (0x00AB0714, 15 slots), +0x4 the port base with its vtable (0x00AB06C8,
// 19 slots), +0x8 cIGZMessageTarget2 (0x00AB06B4), +0xC cIGZSerializable
// (0x00AB069C). The base's fields are 4 further than in airport_sim.h:
// reference count +0x14, message server +0x20, budget simulator +0x30,
// department budget +0x34, port list +0x48.
//
// Groups:
//   simulators.airport.sim     slots 3-14 of the primary vtable, slots 3-9
//                              and 11-18 of the +0x4 vtable, DoMessage,
//                              Write and Read (slot swaps; each checks the
//                              original is still there)
//   simulators.airport.object  our own object: the constructor (a detour;
//                              its only caller is the factory 0x0068B520),
//                              destructor, QueryInterface, AddRef, Release,
//                              GetGZCLSID, and copies of the four vtables
//                              taken at the first construction
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
namespace {

constexpr uint32_t kClassId = 0xAA2049A1;
constexpr uint32_t kIidAirportDeveloper = 0xEA20548D;
constexpr uint32_t kSimulatorType = 0xEA3955C4;
constexpr uint32_t kDepartment = 0x6A4119F0;
// The growth messages, in the order Init asks for them (0x0068B0B2).
constexpr uint32_t kGrowthMessages[6] = {0xEA394B74, 0xCB3BE815, 0xEB3BE415, 0x2B3BE44A, 0x6B3BE6D8, 0x0B3BE81A};

// Offsets from the airport developer's start.
constexpr std::size_t kBase = 0x4;
constexpr std::size_t kTarget = 0x8;
constexpr std::size_t kSerializable = 0xC;
constexpr std::size_t kInitialized = kBase + field::kInitialized; // +0x11
constexpr std::size_t kRefCount = kBase + field::kRefCount;       // +0x14
constexpr std::size_t kMessages = kBase + field::kMessages;       // +0x20
constexpr std::size_t kBudget = kBase + field::kBudget;           // +0x30
constexpr std::size_t kDepartmentBudget = kBase + field::kDepartment; // +0x34
constexpr std::size_t kPorts = kBase + field::kPorts;             // +0x48

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto RzNew = reinterpret_cast<NewFn>(addr::RZ_operator_new);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

char* Next(char* node) { return At<char*>(node, 0x0); }
char* PortOf(char* node) { return At<char*>(node, 0x8); }
char* StageOf(char* port)
{
    return At<char*>(At<char*>(port, port::kType), type::kStages) + At<uint32_t>(port, port::kStage) * 0x20;
}

// The department budget's line item 1 (slot 20).
void* LineItem(void* department) { return Virtual<void*>(department, 0x50, 1u); }

// --- The primary interface (self = the object) ---------------------------------------

// Slot 3 (0x0068B010): the base's Init; then the department budget
// 0x6A4119F0 from the budget simulator (slot 28), referenced; when there is
// none it is created (slot 25 with 0xAA369059), referenced, and set up: its
// slot 5 (0x6A3FF01C, 0xEA493564), slot 13 (the address 0x00B0D148), slot
// 17 (1, 0), and its line item 1's slot 18 (1, 0). Last the six growth
// notifications for the target at +0x8.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, kInitialized))
        return 1;
    const uint8_t done = base::Init(self + kBase, nullptr);
    if (!done)
        return done;
    void* department = Virtual<void*>(At<void*>(self, kBudget), 0x70, kDepartment);
    At<void*>(self, kDepartmentBudget) = department;
    if (!department) {
        department = Virtual<void*>(At<void*>(self, kBudget), 0x64, kDepartment, 0xAA369059u);
        At<void*>(self, kDepartmentBudget) = department;
        Virtual<uint32_t>(department, 0x4);
        Virtual<uint32_t>(At<void*>(self, kDepartmentBudget), 0x14, 0x6A3FF01Cu, 0xEA493564u);
        Virtual<uint32_t>(At<void*>(self, kDepartmentBudget), 0x34,
                          reinterpret_cast<void*>(addr::cSC4AirportDeveloper_DepartmentValue));
        Virtual<uint32_t>(At<void*>(self, kDepartmentBudget), 0x44, 1u, 0u);
        void* const item = LineItem(At<void*>(self, kDepartmentBudget));
        Virtual<uint32_t>(item, 0x48, 1u, 0u);
    } else {
        Virtual<uint32_t>(department, 0x4);
    }
    char* const target = self + kTarget;
    for (const uint32_t type : kGrowthMessages)
        Virtual<uint8_t>(At<void*>(self, kMessages), 0x14, target, type);
    return done;
}

// Slot 4 (0x0068B110): the six notifications removed, the base's Shutdown,
// the department released.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, kInitialized))
        return 1;
    char* const target = self + kTarget;
    for (const uint32_t type : kGrowthMessages)
        Virtual<uint8_t>(At<void*>(self, kMessages), 0x18, target, type);
    const uint8_t done = base::Shutdown(self + kBase, nullptr);
    if (void* const department = At<void*>(self, kDepartmentBudget)) {
        Virtual<uint32_t>(department, 0x8);
        At<void*>(self, kDepartmentBudget) = nullptr;
    }
    return done;
}

// Slot 5 (0x00700F40): the base's average; returns `out`.
float* __fastcall GetAvgEfficiency(char* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("GetAvgEfficiency", logged);
    base::GetAvgEfficiency(self + kBase, nullptr, out);
    return out;
}

// Slot 6 (0x0068AFE0).
uint32_t __fastcall GetSimulatorType(char*, void*)
{
    static bool logged = false;
    FirstCall("GetSimulatorType", logged);
    return kSimulatorType;
}

// Slots 7 to 9 (0x0068B260, 0x0068B000, 0x0068AFF0): the base's, at +4.
uint8_t __fastcall GetLeastEfficientPort(char* self, void*, float* efficiency, int32_t* cell)
{
    return base::GetLeastEfficientPort(self + kBase, nullptr, efficiency, cell);
}

void __fastcall GetAllPortLots(char* self, void*, void** vector)
{
    base::GetAllPortLots(self + kBase, nullptr, vector);
}

uint32_t __fastcall GetNumPorts(char* self, void*, uint32_t sizes, uint32_t stages)
{
    return base::GetNumPorts(self + kBase, nullptr, sizes, stages);
}

// Slot 10 (0x005FD7E0, shared).
char* __fastcall AsPortDeveloper(char* self, void*)
{
    static bool logged = false;
    FirstCall("AsISC4PortDeveloper", logged);
    return self ? self + kBase : nullptr;
}

// Slot 11 (0x0068B540): size 0 counts the port list; other sizes go to the
// base's count by size.
uint32_t __fastcall GetNumAirports(char* self, void*, uint32_t size)
{
    static bool logged = false;
    FirstCall("GetNumAirports", logged);
    if (size)
        return base::GetNumPortsOfSize(self + kBase, nullptr, size);
    char* const head = At<char*>(self, kPorts);
    uint32_t count = 0;
    for (char* node = Next(head); node != head; node = Next(node))
        ++count;
    return count;
}

// What both scaled queries read: the line item's slot 11 (int64) as a
// float, and its slot 10 (int64) as the x87 keeps it.
struct Funding {
    float spent;
    double budget;
};

Funding ReadFunding(void* item)
{
    Funding funding;
    funding.spent = static_cast<float>(Virtual<int64_t>(item, 0x2C));
    funding.budget = static_cast<double>(Virtual<int64_t>(item, 0x28));
    return funding;
}

// Slot 12 (0x00700FE0): the base's usage and capacity of the lot's port;
// with a line item whose slot 10 is above 0 the capacity is scaled by slot
// 11 over slot 10 (truncated), and the usage is at most the capacity.
uint8_t __fastcall GetAirportUsageValues(char* self, void*, void* lot, uint32_t* usage, uint32_t* capacity)
{
    static bool logged = false;
    FirstCall("GetAirportUsageValues", logged);
    const uint8_t found = base::GetUsageValues(self + kBase, nullptr, lot, usage, capacity);
    if (!found)
        return found;
    if (void* const item = LineItem(At<void*>(self, kDepartmentBudget))) {
        const Funding funding = ReadFunding(item);
        if (funding.budget > 0.0)
            *capacity = static_cast<uint32_t>(Ftol(static_cast<double>(*capacity) / funding.budget *
                                                   static_cast<double>(funding.spent)));
    }
    if (*usage > *capacity)
        *usage = *capacity;
    return found;
}

// Slot 13 (0x00701800): the sum of the ports' usage (+0x4 slot 12).
uint32_t __fastcall GetNumAirportTrips(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetNumAirportTrips", logged);
    char* const head = At<char*>(self, kPorts);
    uint32_t trips = 0;
    for (char* node = Next(head); node != head; node = Next(node))
        trips += Virtual<uint32_t>(self + kBase, 0x30, PortOf(node));
    return trips;
}

// Slot 14 (0x00701830): the monthly cost of the lot's port's stage (0
// without a port), scaled like the capacity above (the whole 64 bits of the
// truncation) when the line item's slot 10 is above 0.
int64_t __fastcall GetMaintenanceCost(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("GetMaintenanceCost", logged);
    int64_t cost = 0;
    char* port = nullptr;
    if (base::FindByLot(self + kBase, nullptr, lot, &port))
        cost = At<int64_t>(StageOf(port), stage::kCost);
    if (void* const item = LineItem(At<void*>(self, kDepartmentBudget))) {
        const Funding funding = ReadFunding(item);
        if (funding.budget > 0.0) {
            const double scaled = static_cast<double>(cost) / funding.budget * static_cast<double>(funding.spent);
            // __ftol2's whole result; what does not fit is the x87's 0x8000000000000000.
            if (!(scaled > -9.2233720368547758e18 && scaled < 9.2233720368547758e18))
                return INT64_MIN;
            return static_cast<int64_t>(scaled);
        }
    }
    return cost;
}

// --- The port base's interface (self = the object + 4) -------------------------------

uint8_t __fastcall InitAtBase(char* part, void*) { return Init(part - kBase, nullptr); }
uint8_t __fastcall ShutdownAtBase(char* part, void*) { return Shutdown(part - kBase, nullptr); }
float* __fastcall GetAvgEfficiencyAtBase(char* part, void*, float* out)
{
    return GetAvgEfficiency(part - kBase, nullptr, out);
}

// +0x4 slot 12 (0x0068B250).
uint32_t __fastcall GetPortUsage(char*, void*, char* port)
{
    static bool logged = false;
    FirstCall("GetPortUsage", logged);
    return At<uint32_t>(port, port::kUsage);
}

// +0x4 slots 13 and 14 (0x00648F10, shared): nothing.
void __fastcall EmptySlot(char*, void*, char*) {}

// +0x4 slot 15 (0x0068B400): a port wants to grow. The message of its
// type's size and its stage; posted through the message server's slot 4
// when 0x00B47B5C is set, else given to the base's PostAdvice.
void __fastcall NotifyGrowth(char* part, void*, char* port)
{
    static bool logged = false;
    FirstCall("NotifyGrowth", logged);
    const bool grown = At<uint32_t>(port, port::kStage) != 0;
    uint32_t type = 0;
    switch (At<uint8_t>(At<char*>(port, port::kType), type::kSize)) {
    case 1: type = grown ? kGrowthMessages[1] : kGrowthMessages[0]; break;
    case 2: type = grown ? kGrowthMessages[3] : kGrowthMessages[2]; break;
    case 3: type = grown ? kGrowthMessages[5] : kGrowthMessages[4]; break;
    default: break;
    }
    if (!Global<uint8_t>(addr::cSC4AirportDeveloper_PostGrowthMessages)) {
        base::PostAdvice(part, nullptr, type);
        return;
    }
    void* const memory = RzNew(0x2C);
    void* const message = memory ? reinterpret_cast<SelfFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (message)
        Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    Virtual<uint32_t>(At<void*>(part, field::kMessages), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// +0x4 slot 16 (0x0068B630): the ports' usage. The traffic simulator (the
// city's slot 69) fills two int vectors (slot 42, IDs 0x3320 and 0x3330)
// with one entry per index, and for each port's lot a vector of the indexes
// it serves (slot 39). Each index's ports share the sum of its two entries
// in proportion to their funded capacities:
//   scale     = line item slot 11 / slot 10 (as floats; 1 without an item
//               or when slot 10 is not above 0)
//   total     = trunc(sum of the ports' capacities (as int32) * scale)
//   per port  = trunc(trunc(capacity * scale) / total * entries' sum)
// added to the port's usage +0x24, which starts at 0.
void __fastcall UpdateUsage(char* part, void*)
{
    static bool logged = false;
    FirstCall("UpdateUsage", logged);
    char* const head = At<char*>(part, field::kPorts);
    if (Next(head) == head)
        return;
    void* const traffic = Virtual<void*>(At<void*>(part, field::kCity), 0x114);
    int32_t* first[3] = {};
    int32_t* second[3] = {};
    Virtual<uint32_t>(traffic, 0xA8, 0x3320u, static_cast<void*>(first));
    Virtual<uint32_t>(traffic, 0xA8, 0x3330u, static_cast<void*>(second));
    const int32_t count = static_cast<int32_t>(first[1] - first[0]);
    // new[] of `count` list heads: the count, then one empty list each.
    char** lists = nullptr;
    uint32_t* const block = static_cast<uint32_t*>(OperatorNew(static_cast<std::size_t>(count) * 4 + 4));
    if (block) {
        *block = static_cast<uint32_t>(count);
        lists = reinterpret_cast<char**>(block + 1);
        for (int32_t i = 0; i < count; ++i) {
            // 0x0068B610
            lists[i] = nullptr;
            char* const empty = static_cast<char*>(Allocate(0xC));
            At<char*>(empty, 0x0) = empty;
            At<char*>(empty, 0x4) = empty;
            lists[i] = empty;
        }
    }
    int32_t* served[3] = {};
    for (char* node = Next(head); node != head; node = Next(node)) {
        char* const port = PortOf(node);
        At<uint32_t>(port, port::kUsage) = 0;
        void* const lot = base::GetLot(port, nullptr);
        Virtual<uint32_t>(traffic, 0x9C, lot, static_cast<void*>(served));
        if (served[0] != served[1]) {
            for (int32_t* index = served[0]; index != served[1]; ++index) {
                char* const list = lists[*index];
                char* const entry = static_cast<char*>(Allocate(0xC));
                At<char*>(entry, 0x8) = port;
                char* const last = At<char*>(list, 0x4);
                At<char*>(entry, 0x0) = list;
                At<char*>(entry, 0x4) = last;
                At<char*>(last, 0x0) = entry;
                At<char*>(list, 0x4) = entry;
            }
            served[1] = served[0];
        }
    }
    float scale = 1.0f;
    if (void* const item = LineItem(At<void*>(part, field::kDepartment))) {
        const float spent = static_cast<float>(Virtual<int64_t>(item, 0x2C));
        const double budget = static_cast<double>(Virtual<int64_t>(item, 0x28));
        if (budget > 0.0)
            scale = static_cast<float>(static_cast<double>(spent) / budget);
    }
    for (int32_t i = 0; i < count; ++i) {
        char* const list = lists[i];
        if (Next(list) == list)
            continue;
        uint32_t sum = 0;
        for (char* entry = Next(list); entry != list; entry = Next(entry))
            sum += At<uint32_t>(StageOf(PortOf(entry)), stage::kCapacity);
        // The sum is loaded as a signed integer here.
        const int32_t total = Ftol(static_cast<double>(static_cast<int32_t>(sum)) * static_cast<double>(scale));
        const uint32_t trips = static_cast<uint32_t>(second[0][i]) + static_cast<uint32_t>(first[0][i]);
        const double totalValue = static_cast<double>(total);
        const double tripsValue = static_cast<double>(trips);
        for (char* entry = Next(list); entry != lists[i]; entry = Next(entry)) {
            char* const port = PortOf(entry);
            const uint32_t capacity = At<uint32_t>(StageOf(port), stage::kCapacity);
            const uint32_t funded =
                static_cast<uint32_t>(Ftol(static_cast<double>(capacity) * static_cast<double>(scale)));
            At<uint32_t>(port, port::kUsage) +=
                static_cast<uint32_t>(Ftol(static_cast<double>(funded) / totalValue * tripsValue));
        }
        for (char* entry = Next(lists[i]); entry != lists[i];) {
            char* const next = Next(entry);
            Free(entry);
            entry = next;
        }
        At<char*>(lists[i], 0x0) = lists[i];
        At<char*>(lists[i], 0x4) = lists[i];
    }
    if (lists) {
        // 0x0068B570 with flags 3: the lists destroyed last to first, then the block.
        for (int32_t i = static_cast<int32_t>(*block) - 1; i >= 0; --i) {
            char* const list = lists[i];
            for (char* entry = Next(list); entry != lists[i];) {
                char* const next = Next(entry);
                Free(entry);
                entry = next;
            }
            At<char*>(lists[i], 0x0) = lists[i];
            At<char*>(lists[i], 0x4) = lists[i];
            if (lists[i])
                Free(lists[i]);
        }
        OperatorDelete(block);
    }
    first[1] = first[0];
    second[1] = second[0];
    if (served[0])
        Free(served[0]);
    if (second[0])
        Free(second[0]);
    if (first[0])
        Free(first[0]);
}

// +0x4 slot 17 (0x00700FA0): the line item 1, referenced for the call, gets
// the sum of the ports' costs (its slot 12).
void __fastcall UpdateMaintenance(char* part, void*)
{
    static bool logged = false;
    FirstCall("UpdateMaintenance", logged);
    void* const item = LineItem(At<void*>(part, field::kDepartment));
    if (!item)
        return;
    Virtual<uint32_t>(item, 0x4);
    Virtual<uint32_t>(item, 0x30, base::SumCosts(part, nullptr));
    Virtual<uint32_t>(item, 0x8);
}

// +0x4 slot 18 (0x0068B270): the line item 1's slot 11.
int64_t __fastcall GetFunding(char* part, void*)
{
    static bool logged = false;
    FirstCall("GetFunding", logged);
    return Virtual<int64_t>(LineItem(At<void*>(part, field::kDepartment)), 0x2C);
}

// --- Messages and the save record ------------------------------------------------------

// +0x8 slot 3 (0x0068B1F0): a growth message runs the base's Grow; the
// rest is the base's.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    for (const uint32_t growth : kGrowthMessages) {
        if (type == growth) {
            base::Grow(target - 0x4, nullptr);
            return 1;
        }
    }
    return base::DoMessage(target, nullptr, message);
}

// +0xC slot 3 (0x0068B3E0): version 2, then the base's record.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, 2u);
    return base::Write(part, nullptr, stream);
}

// +0xC slot 4 (0x007010B0): version 2 only, then the base's Read.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    // The game reads the version into the stack slot of its stream
    // argument: when the read writes nothing, the pointer's low 16 bits
    // are compared.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 2)
        return 0;
    return base::Read(part, nullptr, stream);
}

// --- Installation of the slots -------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn)                                                                  \
    {addr::cSC4AirportDeveloper_##vtable, index, addr::cSC4AirportDeveloper_##original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(vftable, 3, Init, Init),
    OPENSC4_SLOT(vftable, 4, Shutdown, Shutdown),
    OPENSC4_SLOT(vftable, 5, GetAvgEfficiency, GetAvgEfficiency),
    OPENSC4_SLOT(vftable, 6, GetSimulatorType, GetSimulatorType),
    OPENSC4_SLOT(vftable, 7, GetLeastEfficientPort_Thunk, GetLeastEfficientPort),
    OPENSC4_SLOT(vftable, 8, GetAllPortLots_Thunk, GetAllPortLots),
    OPENSC4_SLOT(vftable, 9, GetNumPorts_Thunk, GetNumPorts),
    OPENSC4_SLOT(vftable, 10, AsISC4PortDeveloper, AsPortDeveloper),
    OPENSC4_SLOT(vftable, 11, GetNumAirports, GetNumAirports),
    OPENSC4_SLOT(vftable, 12, GetAirportUsageValues, GetAirportUsageValues),
    OPENSC4_SLOT(vftable, 13, GetNumAirportTrips, GetNumAirportTrips),
    OPENSC4_SLOT(vftable, 14, GetMaintenanceCost, GetMaintenanceCost),
    OPENSC4_SLOT(vftable_4, 3, Thunk4_Init, InitAtBase),
    OPENSC4_SLOT(vftable_4, 4, Thunk4_Shutdown, ShutdownAtBase),
    OPENSC4_SLOT(vftable_4, 5, Thunk4_GetAvgEfficiency, GetAvgEfficiencyAtBase),
    OPENSC4_SLOT(vftable_4, 6, Thunk4_GetSimulatorType, GetSimulatorType),
    // The game's thunks go to the primary's slot and back to the base: ours directly.
    OPENSC4_SLOT(vftable_4, 7, Thunk4_GetLeastEfficientPort, base::GetLeastEfficientPort),
    OPENSC4_SLOT(vftable_4, 8, Thunk4_GetAllPortLots, base::GetAllPortLots),
    OPENSC4_SLOT(vftable_4, 9, Thunk4_GetNumPorts, base::GetNumPorts),
    OPENSC4_SLOT(vftable_4, 11, DeleteThis_Jump, base::DeleteThis),
    OPENSC4_SLOT(vftable_4, 12, GetPortUsage, GetPortUsage),
    OPENSC4_SLOT(vftable_4, 13, EmptySlot, EmptySlot),
    OPENSC4_SLOT(vftable_4, 14, EmptySlot, EmptySlot),
    OPENSC4_SLOT(vftable_4, 15, NotifyGrowth, NotifyGrowth),
    OPENSC4_SLOT(vftable_4, 16, UpdateUsage, UpdateUsage),
    OPENSC4_SLOT(vftable_4, 17, UpdateMaintenance, UpdateMaintenance),
    OPENSC4_SLOT(vftable_4, 18, GetFunding, GetFunding),
    OPENSC4_SLOT(vftable_8, 3, DoMessage, DoMessage),
    OPENSC4_SLOT(vftable_C, 3, Write, Write),
    OPENSC4_SLOT(vftable_C, 4, Read, Read),
};

#undef OPENSC4_SLOT

bool InstallSim(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSlots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("airport sim: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("airport sim: %u slots are ours", static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])));
    return true;
}

const bool g_registeredSim = registry::Register({
    "simulators.airport.sim",
    "simulators/airport",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4AirportDeveloper_vftable,
    "Our airport developer: Init, Shutdown, the queries, the ports' usage, funding and growth notices, "
    "DoMessage, Write, Read (30 vtable slots; src/core/airport_object.cpp)",
    false,
    InstallSim,
});

// --- Our object ---------------------------------------------------------------------

// Copies of the game's 0x00AB0714 (15 slots), 0x00AB06C8 (at +4, 19 slots),
// 0x00AB06B4 (at +8) and 0x00AB069C (at +0xC), taken at the first
// construction, after every PostAppInit slot swap.
constexpr int kSlotCount = 15, kSlotCount4 = 19, kSlotCount8 = 4, kSlotCountC = 6;
void* g_vtable[kSlotCount];
void* g_vtable4[kSlotCount4];
void* g_vtable8[kSlotCount8];
void* g_vtableC[kSlotCountC];

// As the game's (0x0068B1D0, then the shared 0x004F7BA0 on the base).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, kRefCount); }

// As the game's (0x0068B1A0): the airport interface is the object, added
// to through its slot 1; everything else is the base's answer for +4.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid == kIidAirportDeveloper) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return base::QueryInterface(self + kBase, nullptr, iid, object);
}

// As the game's (0x0068B1E0, then the base's 0x006E8F90): at the last
// reference the base's slot 11 runs Shutdown and the deleting destructor.
uint32_t __fastcall ReleaseObject(char* self, void*) { return base::Release(self + kBase, nullptr); }

// The destructor (0x0068B3A0, reached through +0x4 slot 10).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("airport object: destroying %p", static_cast<void*>(self));
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kBase) = g_vtable4;
    At<void**>(self, kTarget) = g_vtable8;
    At<void**>(self, kSerializable) = g_vtableC;
    base::Destroy(self + kBase, nullptr);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x0068AFD0).
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

void* __fastcall DeletingDestructorAtBase(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - kBase, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4AirportDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4AirportDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4AirportDeveloper_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4AirportDeveloper_vftable_C), sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kBase>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<kBase>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<kBase>);
    g_vtable4[10] = reinterpret_cast<void*>(&DeletingDestructorAtBase);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kTarget>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<kTarget>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<kTarget>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kSerializable>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<kSerializable>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<kSerializable>);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("airport object: vtables copied (slot 3 %p, +4 slot 16 %p, +8 slot 3 %p, +0xC slots 3/4 %p %p)",
               g_vtable[3], g_vtable4[16], g_vtable8[3], g_vtableC[3], g_vtableC[4]);
}

// The constructor (detour of 0x0068B4C0, whose only caller is the factory
// 0x0068B520): the base, our vtables, then the airport's values in the
// base's fields.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    base::Construct(self + kBase, nullptr);
    At<void**>(self, kBase) = g_vtable4;
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kTarget) = g_vtable8;
    At<void**>(self, kSerializable) = g_vtableC;
    At<void*>(self, kDepartmentBudget) = nullptr;
    At<uint32_t>(self, kBase + field::kLotGroup) = 0xB;
    At<uint32_t>(self, kBase + field::kTypesExemplar) = 0xCA270E9D;
    At<uint32_t>(self, kBase + field::kMessageData) = 0x6A3E9032;
    At<uint32_t>(self, kBase + field::kDepartmentId) = kDepartment;
    At<uint32_t>(self, kBase + field::kOccupantGroup) = 0x1508;
    static int created = 0;
    if (created++ < 3)
        log::Write("airport object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4AirportDeveloper_cSC4AirportDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.airport.object",
    "simulators/airport",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AirportDeveloper_cSC4AirportDeveloper,
    "Our cSC4AirportDeveloper object: constructor (detour), destructor, identity; methods from copies of the "
    "(patched) game vtables (src/core/airport_object.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::airport_sim
