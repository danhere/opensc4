// Our cSC4PollutionSimulator, seventh part: Init (docs/pollution.md,
// "Init and Shutdown"). Everything it allocates goes through the game's own
// code (operator new, the STL buffer allocator, the vector and curve
// helpers), in the game's order, so the heap sees the same allocations.
#include "core/pollution_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>

namespace opensc4::pollution_sim {
namespace {

using SelfFn = void(__cdecl*)(void* self);
using ThisFn = void(__thiscall*)(void* self);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using RestartFn = void(__thiscall*)(void* iterator, uint32_t flag);
using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
using FillInsertFn = void(__thiscall*)(void* vector, uint32_t* where, uint32_t count, const uint32_t* value);

const auto CreateCurves = reinterpret_cast<SelfFn>(addr::PollutionTuning_CreateCurves);
const auto LoadTuning = reinterpret_cast<ThisFn>(addr::cSC4PollutionSimulator_LoadTuning);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto RestartIterator = reinterpret_cast<RestartFn>(addr::TractIterator_Restart);
const auto NextTract = reinterpret_cast<NextFn>(addr::TractIterator_Next);
const auto FillInsert = reinterpret_cast<FillInsertFn>(addr::SC4UintVector_FillInsert);

constexpr std::size_t kLoaded = 0x11;      // bool: set by Read
constexpr std::size_t kInitialized = 0x10; // bool
constexpr std::size_t kTractIterator = 0x1D8;
constexpr std::size_t kBlockIterator = 0x78;
constexpr std::size_t kBlockVectors[] = {0xA8, 0xB4, 0xC0, 0xCC}; // vector<uint32_t> each
constexpr uint32_t kGridInterface = 0x49B9E605;
constexpr uint32_t kMessages[] = {0x99EF1142, 0x99EF1143, 0x26D31EC1, 0xCA1B151C, 0xC6CDBF1A};

// A cSC4SimGridSint16 as Init constructs it (the other fields are left as
// operator new returns them).
void* NewGrid()
{
    auto* const grid = static_cast<uint32_t*>(OperatorNew(0x44));
    if (!grid)
        return nullptr;
    grid[1] = 0x00A80784;
    grid[0] = static_cast<uint32_t>(addr::SC4SimGridSint16_Vtable);
    grid[1] = 0x00A879C4;
    reinterpret_cast<uint8_t*>(grid)[8] = 0;
    grid[3] = 0;
    grid[4] = kGridInterface;
    grid[6] = 0;
    grid[14] = 0;
    grid[15] = 0;
    grid[16] = 0;
    return grid;
}

// A rectangle iterator's bounds (the tract walk's at +0x1D8, the block
// one at +0x78): {flag byte at +1, dirty byte at +2, x0, z0, x1, z1 at +4}.
void SetIteratorBounds(char* iterator, int32_t x1, int32_t z1)
{
    auto* const bounds = reinterpret_cast<int32_t*>(iterator + 4);
    bounds[0] = 0;
    bounds[1] = 0;
    bounds[2] = x1;
    bounds[3] = z1;
    if (bounds[2] < bounds[0])
        bounds[2] = bounds[0];
    if (bounds[3] < bounds[1])
        bounds[3] = bounds[1];
}

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void TakeReference(void* self, std::size_t offset, void* object)
{
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4); // AddRef
}

uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("pollution init: first Init (%s)", At<uint8_t>(self, kLoaded) ? "loaded from a save" : "new");
    }
    char* const base = static_cast<char*>(self);
    CreateCurves(self);
    if (!At<uint8_t>(self, kLoaded)) {
        for (uint32_t type = 0; type < 4; ++type) {
            void*& value = At<void*>(self, field::kValueGrids + 4 * type);
            if (!value) {
                value = NewGrid();
                Virtual<uint32_t>(value, 0x4);
                Virtual<uint32_t>(value, 0xC);
                Virtual<uint32_t>(value, 0x24, 2u);
                Virtual<uint32_t>(value, 0x18, 0x89D5BBF0u + type);
            }
            void*& source = At<void*>(self, field::kAirGrid + 4 * type);
            if (!source) {
                source = NewGrid();
                Virtual<uint32_t>(source, 0x4);
                Virtual<uint32_t>(source, 0xC);
                Virtual<uint32_t>(type == 2 ? At<void*>(self, field::kGarbageGrid) : source, 0x24, type == 2 ? 1u : 2u);
                Virtual<uint32_t>(source, 0x18, 0x49D5BC40u + type);
            }
        }
        for (const std::size_t offset : {0x100, 0x104, 0x108, 0x10C, 0x110, 0x118, 0x114, 0x124, 0x11C, 0x13C, 0x1B4,
                                         0x1B0, 0x22C, 0x230, 0x1B8, 0x178, 0x17C, 0x180, 0x184, 0x188, 0x18C, 0x198,
                                         0x19C, 0x1A0, 0x1A4, 0x1A8, 0x1AC, 0x1BC, 0x12C, 0x130, 0x120})
            At<uint32_t>(self, offset) = 0;
        At<uint16_t>(self, field::kAverageGarbage) = 0;
        std::memcpy(base + 0x190, reinterpret_cast<const void*>(0x00A80990), 8);
        At<uint32_t>(self, field::kRecyclingCenterEffect) = Global<uint32_t>(addr::PollutionTuning_DefaultPercentage);
        At<uint32_t>(self, 0xFC) = ListCount(self, field::kRecyclingList);

        void* const grid = At<void*>(self, field::kValueGrids);
        const int32_t tractsZ = Virtual<int32_t>(grid, 0x34);
        const int32_t tractsX = Virtual<int32_t>(grid, 0x30);
        At<int32_t>(self, 0x1D4) =
            tractsZ * tractsX / static_cast<int32_t>(Global<uint16_t>(addr::PollutionSimulator_BlockCells) >> 1);
        char* const iterator = base + kTractIterator;
        if (iterator[1]) {
            iterator[1] = 0;
            iterator[2] = 1;
        }
        const int32_t lastZ = Virtual<int32_t>(At<void*>(self, field::kValueGrids), 0x34) - 1;
        const int32_t lastX = Virtual<int32_t>(At<void*>(self, field::kValueGrids), 0x30) - 1;
        SetIteratorBounds(iterator, lastX, lastZ);
        iterator[2] = 1;
        RestartIterator(iterator, 1);
        int32_t x = 0, z = 0;
        NextTract(iterator, &x, &z);
        At<int32_t>(self, 0x64) = z * 2;
        At<int32_t>(self, 0x60) = x * 2;
        for (std::size_t flag = field::kRadiationChanged; flag <= field::kGarbageChanged; ++flag)
            At<uint8_t>(self, flag) = 0;
        Virtual<uint32_t>(Global<void*>(addr::SC4_Service43DB8), 0x14, base + 0x8);
    }

    if (!At<uint8_t>(self, kInitialized)) {
        At<uint8_t>(self, kInitialized) = 1;
        LoadTuning(self);
        void* const simulators = Global<void*>(addr::SC4_SimulatorService);
        GameString name{};
        name.begin = static_cast<char*>(Allocate(0x17));
        name.end = name.begin;
        name.capacity = name.begin + 0x17;
        std::memmove(name.begin, reinterpret_cast<const void*>(addr::PollutionSimulator_Name), 0x16);
        name.end = name.begin + 0x16;
        *name.end = 0;
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        name.zero = 0;
        void* const target = base + 0x4;
        Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 2u);
        Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
        if (!At<void*>(self, 0x1C)) {
            TakeReference(self, 0x1C, Global<void*>(addr::SC4_MessageServer));
            for (const uint32_t message : kMessages)
                Virtual<uint8_t>(At<void*>(self, 0x1C), 0x14, target, message);
        }

        // The per-block vectors: one entry per 8x8-cell block, all 0.
        void* const city = Global<void*>(addr::SC4_City);
        const int32_t blocksX = Virtual<int32_t>(city, 0x174) / 8;
        const int32_t blocksZ = Virtual<int32_t>(city, 0x178) / 8;
        const uint32_t blocks = static_cast<uint32_t>(blocksZ * blocksX);
        for (const std::size_t offset : kBlockVectors) {
            auto** const vector = reinterpret_cast<uint32_t**>(base + offset);
            const uint32_t size = static_cast<uint32_t>((vector[1] - vector[0]));
            const uint32_t zero = 0;
            if (blocks < size)
                vector[1] = vector[0] + blocks;
            else
                FillInsert(vector, vector[1], blocks - size, &zero);
        }
        for (uint32_t block = 0; block < blocks; ++block)
            for (const std::size_t offset : kBlockVectors)
                (*reinterpret_cast<uint32_t**>(base + offset))[block] = 0;
        char* const blockIterator = base + kBlockIterator;
        if (blockIterator[1] != 1) {
            blockIterator[1] = 1;
            blockIterator[2] = 1;
        }
        SetIteratorBounds(blockIterator, blocksX - 1, blocksZ - 1);
        blockIterator[2] = 1;

        // The department budget and its three line items.
        if (!At<void*>(self, 0xD8)) {
            void* const department =
                Virtual<void*>(Global<void*>(addr::SC4_BudgetSimulator), 0x64, 0x6A42A8F5u, 0x4A357EAFu);
            void* const old = At<void*>(self, 0xD8);
            if (department != old) {
                if (department)
                    Virtual<uint32_t>(department, 0x4);
                At<void*>(self, 0xD8) = department;
                if (old)
                    Virtual<uint32_t>(old, 0x8);
            }
            void* const budget = At<void*>(self, 0xD8);
            Virtual<uint32_t>(budget, 0x14, 0x6A3FF01Cu, 0xCA4938F8u);
            Virtual<uint32_t>(budget, 0x34, reinterpret_cast<void*>(addr::PollutionSimulator_DepartmentScale));
            const struct {
                uint32_t line;
                uint32_t name;
            } lines[] = {{10, 0xCA57FE96}, {20, 0x4A57FFF0}, {30, 0x0A580005}};
            for (const auto& line : lines) {
                Virtual<uint32_t>(At<void*>(self, 0xD8), 0x44, line.line, 0u);
                void* const item = Virtual<void*>(At<void*>(self, 0xD8), 0x50, line.line);
                Virtual<uint32_t>(item, 0x1C, 0x6A3FF01Cu, line.name);
            }
        }
        Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x34, 0xEA110138u, 2u);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Free(name.begin);
    }

    // References to the city and its services.
    if (!At<void*>(self, 0x18))
        TakeReference(self, 0x18, Global<void*>(addr::SC4_City));
    if (!At<void*>(self, 0x38))
        TakeReference(self, 0x38, Global<void*>(addr::SC4_Service43D1C));
    const struct {
        std::size_t field;
        std::size_t slot;
    } services[] = {{0x20, 0xFC}, {0x24, 0x90}, {0x28, 0x114}, {0x2C, 0x110}, {0x30, 0xF8}, {0x34, 0x10C}};
    for (const auto& service : services)
        if (!At<void*>(self, service.field))
            TakeReference(self, service.field, Virtual<void*>(At<void*>(self, 0x18), service.slot));
    At<uint32_t>(self, 0x3C) = Global<uint32_t>(addr::SC4_Value43D28);
    return 1;
}

// Shutdown (0x006E5F50): the reverse, once Init has run (+0x10): the city's
// services, the department, the building lists handed back, the per-block
// objects and vectors, the grids, the message notifications, and two
// unregistrations.
void Release(void* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("pollution init: first Shutdown");
    }
    char* const base = static_cast<char*>(self);
    if (!At<uint8_t>(self, kInitialized))
        return 1;
    At<uint8_t>(self, kInitialized) = 0;
    At<uint32_t>(self, 0x3C) = 0;
    for (const std::size_t offset : {0x20, 0x24, 0x28, 0x2C, 0x30, 0x34})
        Release(self, offset);
    if (void* const department = At<void*>(self, 0xD8)) {
        At<void*>(self, 0xD8) = nullptr;
        Virtual<uint32_t>(department, 0x8);
    }
    void* const city = Global<void*>(addr::SC4_City);
    void* const occupants = Virtual<void*>(city, 0x88);
    for (const std::size_t list : {field::kWaterTreatmentList, field::kIncineratorList, field::kWasteToEnergyList,
                                   field::kRecyclingList})
        Virtual<uint32_t>(occupants, 0xC0, base + list);
    At<uint32_t>(self, 0xFC) = ListCount(self, field::kRecyclingList);

    const int32_t blocksX = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x174) / 8;
    const int32_t blocksZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x178) / 8;
    const uint32_t blocks = static_cast<uint32_t>(blocksZ * blocksX);
    for (uint32_t block = 0; block < blocks; ++block) {
        for (const std::size_t offset : kBlockVectors) {
            auto& vector = *reinterpret_cast<void***>(base + offset);
            if (!vector[block])
                continue;
            if (Virtual<uint8_t>(vector[block], 0x14))
                Virtual<uint32_t>(vector[block], 0x10, 1u);
            Virtual<uint32_t>(vector[block], 0x8);
            vector[block] = nullptr;
        }
    }
    for (const std::size_t offset : kBlockVectors) {
        auto** const vector = reinterpret_cast<uint32_t**>(base + offset);
        const uint32_t zero = 0;
        if (vector[1] == vector[0])
            FillInsert(vector, vector[1], 0, &zero);
        else
            vector[1] = vector[0];
    }
    Release(self, 0x38);
    Release(self, 0x18);
    for (uint32_t type = 0; type < 4; ++type) {
        for (const std::size_t offset : {field::kValueGrids + 4 * type, field::kAirGrid + 4 * type}) {
            if (void* const grid = At<void*>(self, offset)) {
                Virtual<uint32_t>(grid, 0x10); // Shutdown
                Virtual<uint32_t>(At<void*>(self, offset), 0x8);
                At<void*>(self, offset) = nullptr;
            }
        }
    }
    if (At<void*>(self, 0x1C)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, 0x1C), 0x18, base + 0x4, message); // RemoveNotification
        Release(self, 0x1C);
    }
    if (void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(service, 0x50, base + 0x4);
    if (void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), 0xA4))
        Virtual<uint32_t>(service, 0x18, base + 0x8);
    return 1;
}

using InitFn = uint8_t(__fastcall*)(void*, void*);
InitFn g_originalInit = nullptr;

bool Install(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_Init);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &Init, &g_originalInit);
}

// diagnostics.pollution_fresh: Init's new-city path on a loaded city, for
// checking it (every test city is loaded, and even the region's empty
// tiles carry a pollution record). The first call of CreateCurves, the first
// thing Init does, clears the loaded flag and forgets the grids (they are
// leaked, not released: other objects may hold them), so Init builds
// everything anew; the original's and ours can then be compared as usual.
using CreateCurvesFn = void(__cdecl*)(void* self);
CreateCurvesFn g_originalCreateCurves = nullptr;

void __cdecl FreshCreateCurves(void* self)
{
    static bool done = false;
    if (!done) {
        done = true;
        At<uint8_t>(self, kLoaded) = 0;
        for (uint32_t type = 0; type < 4; ++type) {
            At<void*>(self, field::kValueGrids + 4 * type) = nullptr;
            At<void*>(self, field::kAirGrid + 4 * type) = nullptr;
        }
        log::Write("pollution init: loaded flag cleared and grids forgotten for a fresh Init");
    }
    g_originalCreateCurves(self);
}

bool InstallFresh(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::PollutionTuning_CreateCurves);
    return game::IsBuild641() && entry && hook::Install(*entry, &FreshCreateCurves, &g_originalCreateCurves);
}

const bool g_registeredFresh = registry::Register({
    "diagnostics.pollution_fresh",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::PollutionTuning_CreateCurves,
    "Pollution Init's new-city path on a loaded city (fresh grids), for checking it (docs/pollution.md)",
    false,
    InstallFresh,
});

InitFn g_originalShutdown = nullptr;

bool InstallShutdown(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_Shutdown);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &Shutdown, &g_originalShutdown);
}

const bool g_registeredShutdown = registry::Register({
    "simulators.pollution.shutdown",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_Shutdown,
    "Our pollution simulator Shutdown (docs/pollution.md)",
    false,
    InstallShutdown,
});

const bool g_registered = registry::Register({
    "simulators.pollution.init",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_Init,
    "Our pollution simulator Init: grids, fields, registration, block vectors, department budget (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim
