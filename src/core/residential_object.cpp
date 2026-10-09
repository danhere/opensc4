// Our cSC4ResidentialSimulator (build 641; docs/residential.md), the parts
// around the simulation: the tuning, Init, Shutdown, the save record, and
// our own object under the class ID. The queries, the occupant functions and
// the messages with the simulation steps are in residential_queries.cpp,
// residential_occupants.cpp and residential_messages.cpp.
#include "core/residential_sim.h"

#include "core/director.h"
#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <utility>

namespace opensc4::residential_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("residential: first %s", name);
}

namespace {

// Fields used only here (offsets from the object's start).
namespace local {
constexpr std::size_t kCellSize = 0x44;        // the developer grid's cell size
constexpr std::size_t kBoolGrid54 = 0x54;
constexpr std::size_t kBoolGrids58 = 0x58;     // vector of 7 bit grids
constexpr std::size_t kBoolGrids64 = 0x64;     // vector of 3 bit grids
constexpr std::size_t kFloatGrid70 = 0x70;
constexpr std::size_t kFloatGrid74 = 0x74;
constexpr std::size_t kQualityCellSize = 0x78; // the EQ and HQ grids' cell size
constexpr std::size_t kState94 = 0x94;
constexpr std::size_t kFloat98 = 0x98;
constexpr std::size_t kFloat9C = 0x9C;
constexpr std::size_t kFlagA0 = 0xA0;          // uint8
constexpr std::size_t kA8 = 0xA8;
constexpr std::size_t kRandom = 0xB8;
constexpr std::size_t kSchoolItem = 0x120;     // budget items (the budget's slot 25)
constexpr std::size_t kHealthItem = 0x124;
constexpr std::size_t kSchoolItem2 = 0x180;
constexpr std::size_t kHealthItem2 = 0x184;
constexpr std::size_t kMap194 = 0x194;
constexpr std::size_t kMap198 = 0x198;
constexpr std::size_t kList19C = 0x19C;
constexpr std::size_t kList1A0 = 0x1A0;
constexpr std::size_t kList1A4 = 0x1A4;
constexpr std::size_t kList1C0 = 0x1C0;
constexpr std::size_t kCurves264 = 0x264;      // nine curves, 16 bytes each
constexpr std::size_t kIterator = 0x328;       // a cell iterator over the rect +0x338
constexpr std::size_t kRect = 0x338;           // int32 x0, z0, x1, z1
constexpr std::size_t kStep348 = 0x348;        // int64
constexpr std::size_t kState350 = 0x350;
constexpr std::size_t kHolder3C4 = 0x3C4;
} // namespace local

constexpr uint32_t kTuningKey[3] = {0x6534284A, 0xE7E2C2DB, 0x69189C84};
constexpr uint32_t kIidGrid = 0xC9BD939A;
constexpr uint32_t kIidBuilding = 0x223E2B67;

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

struct Point {
    float x;
    float y;
};

using PointInsertFn = void(__thiscall*)(Vector* vector, Point* where, const Point* value, void* allocator,
                                        uint32_t count, uint32_t atEnd);
using SetPointsFn = uint8_t(__thiscall*)(void* curve, const Vector* points);
using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetFloatsFn = uint8_t(__cdecl*)(void* holder, uint32_t property, Vector* value);
using GetIntFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void(__thiscall*)(void* self);
using RefFn = void(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using ListStreamFn = uint8_t(__cdecl*)(void* stream, void* list);

const auto PointInsert = reinterpret_cast<PointInsertFn>(addr::SC4PointVector_InsertN);
const auto SetPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
const auto LoadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
const auto GetFloats = reinterpret_cast<GetFloatsFn>(addr::SC4_GetPropertyFloatVector);
const auto GetSint32 = reinterpret_cast<GetIntFn>(addr::SC4_GetPropertySint32);
const auto GetUint32 = reinterpret_cast<GetIntFn>(addr::SC4_GetPropertyUint32);
const auto GetPercentage = reinterpret_cast<GetIntFn>(addr::SC4_GetPropertyPercentage);
const auto GetUint32s = reinterpret_cast<GetIntFn>(addr::SC4_GetPropertyUint32Vector);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);

// push_back of a point, as the game's inline code.
void PushPoint(Vector* vector, float x, float y)
{
    const Point point{x, y};
    if (vector->end != vector->capacity) {
        if (vector->end)
            *reinterpret_cast<Point*>(vector->end) = point;
        vector->end += sizeof(Point);
        return;
    }
    char allocator;
    PointInsert(vector, reinterpret_cast<Point*>(vector->end), &point, &allocator, 1, 1);
}

// The first point, inserted at null into an empty vector, as the game does.
void FirstPoint(Vector* vector, float x, float y)
{
    *vector = {};
    const Point point{x, y};
    char allocator;
    PointInsert(vector, nullptr, &point, &allocator, 1, 1);
}

float Scaled(float value) { return static_cast<float>(static_cast<double>(value) * Global<float>(addr::SC4Traffic_FerryCostScale)); }

// --- The tuning ---------------------------------------------------------------

// 0x006FBAB0 (Init, before the first run): the curves set to default points
// (100,100)-(120,110) and (0,20)-(100,100), the school and health blocks'
// defaults, then from the tuning exemplar 6534284A-E7E2C2DB-69189C84 the
// blocks' values and curves, the nine curves +0x264, and the remaining
// figures.
void LoadTuning(char* self)
{
    Vector rising;
    FirstPoint(&rising, 100.0f, 100.0f);
    PushPoint(&rising, 120.0f, 110.0f);
    Vector flat;
    FirstPoint(&flat, 0.0f, 20.0f);
    PushPoint(&flat, 100.0f, 100.0f);
    SetPoints(self + 0xE8, &rising);
    SetPoints(self + 0xF8, &flat);
    SetPoints(self + 0x108, &flat);
    At<uint32_t>(self, 0xDC) = 0x3E4CCCCD; // 0.2
    At<uint32_t>(self, 0xE0) = 10;
    At<uint32_t>(self, 0xE4) = 0x3F666666; // 0.9
    At<uint32_t>(self, 0xD4) = 0x3F19999A; // 0.6
    At<uint32_t>(self, 0x130) = 0x077ACCD2;
    At<uint32_t>(self, 0x128) = 0x3E800000; // 0.25
    At<uint32_t>(self, 0xD8) = 0x3F99999A; // 1.2
    At<uint8_t>(self, field::kHealthStrike) = 0;
    At<uint32_t>(self, field::kHealthStrikeDay) = 0;
    At<uint32_t>(self, 0x12C) = 0x42C80000; // 100
    SetPoints(self + 0x148, &rising);
    SetPoints(self + 0x158, &flat);
    SetPoints(self + 0x168, &flat);
    At<uint32_t>(self, 0x138) = 0x3F99999A;
    At<uint32_t>(self, 0x13C) = 0x3E4CCCCD;
    At<uint32_t>(self, 0x140) = 10;
    At<uint32_t>(self, 0x144) = 0x3F666666;
    At<uint32_t>(self, 0x134) = 0x3F19999A;
    At<uint32_t>(self, 0x190) = 0x077ACCD0;
    At<uint32_t>(self, 0x188) = 0x3E800000;
    At<uint8_t>(self, field::kSchoolStrike) = 0;
    At<uint32_t>(self, field::kSchoolStrikeDay) = 0;
    At<uint32_t>(self, 0x18C) = 0x42C80000;
    for (int i = 0; i < 9; ++i)
        SetPoints(self + local::kCurves264 + 0x10 * i, &rising);
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, kTuningKey, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        float value = 0.0f;
        if (GetFloat(holder, 0xE918946F, &value))
            At<float>(self, 0xD4) = Scaled(value);
        LoadCurve(self + 0xE8, kTuningKey, 0x2918949E);
        LoadCurve(self + 0x108, kTuningKey, 0x0A53B2F9);
        Vector health{};
        if (GetFloats(holder, 0xA9189479, &health)) {
            At<float>(self, 0x128) = Scaled(reinterpret_cast<float*>(health.begin)[0]);
            At<float>(self, 0xD8) = Scaled(reinterpret_cast<float*>(health.begin)[1]);
        }
        if (GetFloat(holder, 0x89189482, &value))
            At<float>(self, 0xDC) = Scaled(value);
        GetSint32(holder, 0x8918948B, self + 0xE0);
        GetPercentage(holder, 0x89189495, self + 0xE4);
        LoadCurve(self + 0xF8, kTuningKey, 0x691894A4);
        if (GetFloat(holder, 0xC91894A8, &value))
            At<float>(self, 0x134) = Scaled(value);
        LoadCurve(self + 0x148, kTuningKey, 0x691894C6);
        LoadCurve(self + 0x168, kTuningKey, 0xEA53B30F);
        Vector school{};
        if (GetFloats(holder, 0x891894AD, &school)) {
            At<float>(self, 0x188) = Scaled(reinterpret_cast<float*>(school.begin)[0]);
            At<float>(self, 0x138) = Scaled(reinterpret_cast<float*>(school.begin)[1]);
        }
        if (GetFloat(holder, 0xE91894B3, &value))
            At<float>(self, 0x13C) = Scaled(value);
        GetSint32(holder, 0xA91894BC, self + 0x140);
        GetPercentage(holder, 0x691894C0, self + 0x144);
        LoadCurve(self + 0x158, kTuningKey, 0xC91894CB);
        for (uint32_t i = 0; i < 9; ++i)
            LoadCurve(self + local::kCurves264 + 0x10 * i, kTuningKey, 0xEA540621 + i);
        At<uint32_t>(self, 0x2F4) = 30;
        At<uint32_t>(self, 0x2F8) = 30;
        GetUint32(holder, 0x8A891421, self + 0x2F4);
        GetUint32(holder, 0x8A89142A, self + 0x2F8);
        At<uint32_t>(self, 0x324) = 0x3F8CCCCD; // 1.1
        GetFloat(holder, 0x6A9F6EDD, &At<float>(self, 0x324));
        Virtual<uint32_t>(exemplar, 0x8);
        At<uint32_t>(self, 0x31C) = 0x41200000; // 10
        At<uint32_t>(self, 0x320) = 0x41200000;
        GetFloat(holder, 0x4AA84161, &At<float>(self, 0x31C));
        GetFloat(holder, 0x2AA84164, &At<float>(self, 0x320));
        GetUint32s(holder, 0xEC43826B, self + 0x2FC);
        GetUint32s(holder, 0xAC43826D, self + 0x308);
        if (school.begin)
            Free(school.begin);
        if (health.begin)
            Free(health.begin);
    }
    if (flat.begin)
        Free(flat.begin);
    if (rising.begin)
        Free(rising.begin);
}

// --- Init ----------------------------------------------------------------------

// A new grid as the game's inline constructors build it (0x44 bytes): the
// two vtables, its type at +0x10, the rest empty.
void* NewGrid(std::uintptr_t vtable, std::uintptr_t vtable4, uint32_t type)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    char* const grid = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x44));
    if (!grid)
        return nullptr;
    At<std::uintptr_t>(grid, 0x4) = 0x00A80784;
    At<std::uintptr_t>(grid, 0x0) = vtable;
    At<std::uintptr_t>(grid, 0x4) = vtable4;
    At<uint8_t>(grid, 0x8) = 0;
    At<uint32_t>(grid, 0xC) = 0;
    At<uint32_t>(grid, 0x10) = type;
    At<uint32_t>(grid, 0x18) = 0;
    At<uint32_t>(grid, 0x38) = 0;
    At<uint32_t>(grid, 0x3C) = 0;
    At<uint32_t>(grid, 0x40) = 0;
    return grid;
}

void* NewBoolGrid() { return NewGrid(0x00A878C0, 0x00A878A4, 0x49B9E602); }
void* NewUint16Grid() { return NewGrid(0x00A87D40, 0x00A87D24, 0x49B9E604); }
void* NewFloatGrid() { return NewGrid(0x00A87C20, 0x00A87C04, 0x49B9E60A); }

// A grid stored at `slot`, referenced, set up (its slot 3), sized (slot 9)
// and given its ID (slot 6), as Init does for each.
void SetUpGrid(void** slot, void* grid, uint32_t size, uint32_t id)
{
    *slot = grid;
    Virtual<uint32_t>(*slot, 0x4);
    Virtual<void>(*slot, 0xC);
    Virtual<void>(*slot, 0x24, size);
    Virtual<void>(*slot, 0x18, id);
}

// A budget item (the budget's slot 25) into `slot` when there is none: its
// slot 5, slot 13 with the block's funding curve, slot 11 with `amount`.
void* BudgetItem(char* self, std::size_t slot, uint32_t id, uint32_t kind, void* curve, void* amount)
{
    void* const item = Virtual<void*>(At<void*>(self, field::kBudget), 0x64, id, 0x6A357B7Fu);
    At<void*>(self, slot) = item;
    Virtual<uint32_t>(item, 0x4);
    Virtual<void>(At<void*>(self, slot), 0x14, 0x6A3FF01Cu, kind);
    Virtual<void>(At<void*>(self, slot), 0x34, curve);
    Virtual<void>(At<void*>(self, slot), 0x2C, amount, 0u);
    return item;
}

// The cell iterator +0x328 restarted over the rect +0x338 (2 when the rect
// is empty or out of range), as the constructor, Init and Read do inline.
void ResetIterator(char* self)
{
    At<uint32_t>(self, local::kIterator + 4) = 0;
    At<uint32_t>(self, local::kIterator + 8) = 0;
    At<uint8_t>(self, local::kIterator) = 0;
    const int32_t* const rect = &At<int32_t>(self, local::kRect);
    if (rect[0] > 0 || rect[1] > 0 || rect[2] < 0 || rect[3] < 0)
        At<uint8_t>(self, local::kIterator) = 2;
}

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// Slot 3 (0x006FEAD0): the services (city, building development, network,
// the developer grids, the zone grid), the occupant messages, the budget;
// registration with the simulator service (0, 10, 6, 4); the monthly
// figures; the grid sizes (once); the tuning and two maps (before the first
// run); every grid that no save gave; the four budget items; the rect over
// the population grid and the iterator; false when a service is missing.
uint8_t __fastcall Init(void* object, void*)
{
    using TextFn = void*(__thiscall*)(void* string, const char* text);
    static bool logged = false;
    FirstCall("Init", logged);
    char* const self = static_cast<char*>(object);
    uint8_t& once = Global<uint8_t>(addr::cSC4ResidentialSimulator_Started);
    if (!once)
        once = 1;
    const auto Take = [&](std::size_t offset, void* value) {
        if (!At<void*>(self, offset)) {
            At<void*>(self, offset) = value;
            Virtual<uint32_t>(value, 0x4);
        }
    };
    Take(field::kCity, Global<void*>(addr::SC4_City));
    Take(field::kDevelopment, Global<void*>(addr::SC4_Service43D68));
    Take(field::kService1C, Global<void*>(addr::SC4_NetworkService));
    if (!At<void*>(self, field::kDeveloper28)) {
        for (uint32_t i = 0; i < 3; ++i) {
            void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x80, 0x1010u + 0x10 * i);
            At<void*>(self, field::kDeveloper28 + 4 * i) = grid;
            Virtual<uint32_t>(grid, 0x4);
        }
    }
    if (!At<void*>(self, field::kDeveloper24)) {
        void* const zones = Virtual<void*>(Global<void*>(addr::SC4_Service43D74), 0x18, 0x1000u, 0u);
        At<void*>(self, field::kDeveloper24) = zones;
        if (!zones)
            return 0;
        Virtual<uint32_t>(zones, 0x4);
    }
    if (!At<void*>(self, field::kMessages)) {
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, field::kMessages) = messages;
        if (!messages)
            return 0;
        Virtual<uint32_t>(messages, 0x4);
        void* const target = self + 0x4;
        Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, 0x99EF1142u);
        Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, 0x99EF1143u);
    }
    if (!At<void*>(self, field::kBudget)) {
        void* const budget = Virtual<void*>(At<void*>(self, field::kCity), 0xD0);
        At<void*>(self, field::kBudget) = budget;
        Virtual<uint32_t>(budget, 0x4);
    }
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    if (!simulators)
        return 0;
    GameString name{};
    reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                       reinterpret_cast<const char*>(addr::cSC4ResidentialSimulator_Name));
    void* const target = self + 0x4;
    for (const uint32_t phase : {0u, 10u, 6u, 4u})
        Virtual<uint32_t>(simulators, 0x4C, target, phase, &name, 2u);
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    for (uint32_t i = 0; i < 9; ++i) {
        Virtual<void>(figures, 0x34, 0xCA545CABu + i, 2u);
        Virtual<void>(figures, 0x34, 0xCA545CCAu + i, 2u);
    }
    Virtual<void>(figures, 0x34, 0x4A8A3FE2u, 2u);
    Virtual<void>(figures, 0x34, 0x4A8A3FE5u, 2u);
    if (!At<uint8_t>(self, field::kSetUp)) {
        const uint32_t size = Virtual<uint32_t>(Virtual<void*>(At<void*>(self, field::kDeveloper28), 0x3C), 0x28);
        At<uint32_t>(self, local::kCellSize) = size;
        At<uint32_t>(self, field::kPopulation) = 0;
        At<uint32_t>(self, local::kA8) = 0;
        At<uint32_t>(self, local::kQualityCellSize) = size;
        At<uint32_t>(self, local::kState94) = 0;
        At<uint8_t>(self, field::kSetUp) = 1;
    }
    if (!At<uint8_t>(self, field::kInitialized)) {
        LoadTuning(self);
        LoadRecordsFor(self + local::kMap194, self + local::kMap198);
    }
    const uint32_t cell = At<uint32_t>(self, local::kCellSize);
    const uint32_t quality = At<uint32_t>(self, local::kQualityCellSize);
    void** const proximity = &At<void*>(self, field::kProximity);
    if (!proximity[2])
        SetUpGrid(&proximity[2], NewBoolGrid(), At<uint32_t>(self, field::kMapFlag), 0x49D5BC96);
    if (!proximity[1])
        SetUpGrid(&proximity[1], NewBoolGrid(), At<uint32_t>(self, field::kMapFlag), 0x49D5BCA6);
    if (!proximity[0])
        SetUpGrid(&proximity[0], NewBoolGrid(), At<uint32_t>(self, field::kMapFlag), 0x49D5BCB7);
    void** const population = &At<void*>(self, field::kPopulationGrids);
    if (!population[0])
        for (uint32_t i = 0; i < 3; ++i)
            SetUpGrid(&population[i], NewUint16Grid(), At<uint32_t>(self, local::kCellSize), 0x69D5C2A0 + i);
    if (!At<void*>(self, local::kBoolGrid54))
        SetUpGrid(&At<void*>(self, local::kBoolGrid54), NewBoolGrid(), At<uint32_t>(self, local::kCellSize), 0x69D5C303);
    for (const auto& [vector, table] : {std::pair<std::size_t, std::uintptr_t>{local::kBoolGrids58, 0x00AB2B7C},
                                        std::pair<std::size_t, std::uintptr_t>{local::kBoolGrids64, 0x00AB2BA4}}) {
        void** const begin = At<void**>(self, vector);
        if (begin[0])
            continue;
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<void**>(self, vector + 4) - At<void**>(self, vector)); ++i)
            SetUpGrid(&At<void**>(self, vector)[i], NewBoolGrid(), At<uint32_t>(self, local::kCellSize),
                      reinterpret_cast<const uint32_t*>(table)[i]);
    }
    if (!At<void*>(self, local::kFloatGrid70))
        SetUpGrid(&At<void*>(self, local::kFloatGrid70), NewFloatGrid(), At<uint32_t>(self, local::kCellSize),
                  0x69D5C324);
    if (!At<void*>(self, local::kFloatGrid74))
        SetUpGrid(&At<void*>(self, local::kFloatGrid74), NewFloatGrid(), At<uint32_t>(self, local::kCellSize),
                  0x69D5C330);
    void** const eq = &At<void*>(self, field::kEQGrids);
    if (!eq[0])
        for (uint32_t i = 0; i < 3; ++i)
            SetUpGrid(&eq[i], NewFloatGrid(), At<uint32_t>(self, local::kQualityCellSize), 0x69D5C2D0 + i);
    void** const hq = &At<void*>(self, field::kHQGrids);
    if (!hq[0])
        for (uint32_t i = 0; i < 3; ++i)
            SetUpGrid(&hq[i], NewFloatGrid(), At<uint32_t>(self, local::kQualityCellSize), 0x69D5C2E0 + i);
    (void)cell;
    (void)quality;
    void* const hundred = reinterpret_cast<void*>(addr::cSC4ResidentialSimulator_FullFunding);
    if (!At<void*>(self, local::kSchoolItem))
        BudgetItem(self, local::kSchoolItem, 0x09188F42, 0xCA493A56, self + 0xD8, self + 0x128);
    if (!At<void*>(self, local::kHealthItem)) {
        BudgetItem(self, local::kHealthItem, 0xAA538CB3, 0xCA538D98, self + 0xD8, hundred);
        Virtual<void>(At<void*>(self, local::kHealthItem), 0x44, 1u, 1u);
        Virtual<void>(Virtual<void*>(At<void*>(self, local::kHealthItem), 0x50, 1u), 0x48, 1u, 0u);
    }
    if (!At<void*>(self, local::kSchoolItem2))
        BudgetItem(self, local::kSchoolItem2, 0x09188F4C, 0x6A493A66, self + 0x138, self + 0x188);
    if (!At<void*>(self, local::kHealthItem2)) {
        BudgetItem(self, local::kHealthItem2, 0x4A538CC6, 0x8A538DAC, self + 0x138, hundred);
        Virtual<void>(At<void*>(self, local::kHealthItem2), 0x44, 1u, 1u);
        Virtual<void>(Virtual<void*>(At<void*>(self, local::kHealthItem2), 0x50, 1u), 0x48, 1u, 0u);
    }
    const int32_t lastZ = Virtual<int32_t>(At<void*>(self, field::kPopulationGrids), 0x34) - 1;
    const int32_t countX = Virtual<int32_t>(At<void*>(self, field::kPopulationGrids), 0x30);
    int32_t* const rect = &At<int32_t>(self, local::kRect);
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = countX - 1;
    rect[3] = lastZ;
    if (rect[2] < rect[0])
        rect[2] = rect[0];
    if (rect[3] < rect[1])
        rect[3] = rect[1];
    ResetIterator(self);
    LoadParamsFor(self, self + local::kState350);
    At<int64_t>(self, local::kStep348) = 0;
    At<uint8_t>(self, field::kInitialized) = 1;
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        Free(name.begin);
    return 1;
}

// --- Shutdown -----------------------------------------------------------------

void Release(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

void ShutDownGrid(void** slot)
{
    if (*slot) {
        Virtual<uint8_t>(*slot, 0x10);
        Virtual<uint32_t>(*slot, 0x8);
        *slot = nullptr;
    }
}

// Slot 4 (0x006F97F0): unregistered from the simulator service; the
// messages off; the budget and its four items, the maps and grids (shut
// down), the services released; each building list handed to the occupant
// manager (the city's slot 34, its slot 48) and emptied.
uint8_t __fastcall Shutdown(void* object, void*)
{
    using ListFn = void(__thiscall*)(void* list);
    static bool logged = false;
    FirstCall("Shutdown", logged);
    char* const self = static_cast<char*>(object);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, self + 0x4);
    if (void* const messages = At<void*>(self, field::kMessages)) {
        void* const target = self + 0x4;
        Virtual<uint8_t>(messages, 0x18, target, 0x99EF1142u);
        Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, 0x99EF1143u);
        Virtual<uint32_t>(At<void*>(self, field::kMessages), 0x8);
        At<void*>(self, field::kMessages) = nullptr;
    }
    Release(self, field::kBudget);
    Release(self, local::kSchoolItem);
    Release(self, local::kHealthItem);
    Release(self, local::kSchoolItem2);
    Release(self, local::kHealthItem2);
    ShutDownGrid(&At<void*>(self, field::kProximity + 8));
    ShutDownGrid(&At<void*>(self, field::kProximity + 4));
    ShutDownGrid(&At<void*>(self, field::kProximity));
    if (At<void*>(self, field::kPopulationGrids))
        for (int i = 0; i < 3; ++i)
            ShutDownGrid(&At<void*>(self, field::kPopulationGrids + 4 * i));
    ShutDownGrid(&At<void*>(self, local::kBoolGrid54));
    for (const std::size_t vector : {local::kBoolGrids58, local::kBoolGrids64})
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<void**>(self, vector + 4) - At<void**>(self, vector)); ++i)
            ShutDownGrid(&At<void**>(self, vector)[i]);
    ShutDownGrid(&At<void*>(self, local::kFloatGrid70));
    ShutDownGrid(&At<void*>(self, local::kFloatGrid74));
    if (At<void*>(self, field::kEQGrids))
        for (int i = 0; i < 3; ++i)
            ShutDownGrid(&At<void*>(self, field::kEQGrids + 4 * i));
    if (At<void*>(self, field::kHQGrids))
        for (int i = 0; i < 3; ++i)
            ShutDownGrid(&At<void*>(self, field::kHQGrids + 4 * i));
    Release(self, field::kDeveloper24);
    Release(self, local::kHolder3C4);
    Release(self, field::kCity);
    Release(self, field::kDevelopment);
    Release(self, field::kService1C);
    Release(self, field::kDeveloper28);
    Release(self, field::kDeveloper28 + 4);
    Release(self, field::kDeveloper28 + 8);
    void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), 0x88);
    const auto Hand = [&](void* list) {
        Virtual<void>(occupants, 0xC0, list);
        reinterpret_cast<ListFn>(addr::SC4List_Clear2)(list);
    };
    for (uint32_t i = 0;
         i < static_cast<uint32_t>(At<char*>(self, field::kSchoolBuildings + 4) - At<char*>(self, field::kSchoolBuildings)) / 4;
         ++i)
        Hand(At<char*>(self, field::kSchoolBuildings) + 4 * i);
    Hand(self + local::kList19C);
    for (uint32_t i = 0;
         i < static_cast<uint32_t>(At<char*>(self, field::kHospitalBuildings + 4) - At<char*>(self, field::kHospitalBuildings)) / 4;
         ++i)
        Hand(At<char*>(self, field::kHospitalBuildings) + 4 * i);
    Hand(self + local::kList1A0);
    Hand(self + local::kList1A4);
    Hand(self + local::kList1C0);
    return 1;
}

// --- Save -----------------------------------------------------------------------

// 0x006F7B00: a school or health block: its three floats, a number, a float,
// a flag, a number, two references, two floats, a number.
uint8_t WriteBlock(void* stream, char* block)
{
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(block, 0x0));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(block, 0x4));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(block, 0x8));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(block, 0xC));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(block, 0x10));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(block, 0x44)));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(block, 0x48));
    WriteRef(stream, At<void*>(block, 0x4C));
    WriteRef(stream, At<void*>(block, 0x50));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(block, 0x54));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(block, 0x58));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(block, 0x5C));
    return 1;
}

// 0x006F7BA0: a block back; before version 5 the floats were percentages
// (times 0.01), before 6 the one at +0x10 too.
uint8_t ReadBlock(void* stream, char* block, uint16_t version)
{
    float value = 0.0f;
    const auto ReadFloat = [&](std::size_t offset, bool scale) {
        Virtual<uint8_t>(stream, 0x30, &value);
        At<float>(block, offset) = scale ? Scaled(value) : static_cast<float>(static_cast<double>(value));
    };
    if (version >= 5) {
        ReadFloat(0x0, false);
        Virtual<uint8_t>(stream, 0x30, &value);
        At<float>(block, 0x4) = value;
        Virtual<uint8_t>(stream, 0x30, &value);
        At<float>(block, 0x8) = value;
    } else {
        ReadFloat(0x0, true);
        ReadFloat(0x4, true);
        ReadFloat(0x8, true);
    }
    Virtual<uint8_t>(stream, 0x20, block + 0xC);
    Virtual<uint8_t>(stream, 0x30, &value);
    if (version >= 6)
        At<float>(block, 0x10) = value;
    else
        At<float>(block, 0x10) = Scaled(value);
    Virtual<uint8_t>(stream, 0x14, block + 0x44);
    Virtual<uint8_t>(stream, 0x20, block + 0x48);
    ReadRef(stream, kIidBuilding, block + 0x4C, 0u);
    ReadRef(stream, kIidBuilding, block + 0x50, 0u);
    Virtual<uint8_t>(stream, 0x30, &value);
    if (version >= 5)
        At<float>(block, 0x54) = value;
    else
        At<float>(block, 0x54) = Scaled(value);
    Virtual<uint8_t>(stream, 0x30, block + 0x58);
    Virtual<uint8_t>(stream, 0x24, block + 0x5C);
    return 1;
}

// cIGZSerializable slot 3 (0x006F8C40), on the part at +8: version 7 (see
// docs/residential.md for the layout). Returns true whatever the stream says.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    using CellFn = void(__cdecl*)(void* stream, void* iterator);
    using BoolFn = void(__cdecl*)(void* stream, uint32_t value);
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 8;
    const auto WriteList = reinterpret_cast<ListStreamFn>(addr::SC4Stream_WriteReferenceList);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(7));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kMapFlag));
    for (int i = 0; i < 3; ++i)
        WriteRef(stream, At<void*>(self, field::kProximity + 4 * i));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, local::kCellSize));
    for (int i = 0; i < 3; ++i)
        WriteRef(stream, At<void*>(self, field::kPopulationGrids + 4 * i));
    WriteRef(stream, At<void*>(self, local::kBoolGrid54));
    for (const std::size_t vector : {local::kBoolGrids58, local::kBoolGrids64}) {
        Virtual<uint8_t>(stream, 0x24,
                         static_cast<uint32_t>(At<void**>(self, vector + 4) - At<void**>(self, vector)));
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<void**>(self, vector + 4) - At<void**>(self, vector)); ++i)
            WriteRef(stream, At<void**>(self, vector)[i]);
    }
    WriteRef(stream, At<void*>(self, local::kFloatGrid70));
    WriteRef(stream, At<void*>(self, local::kFloatGrid74));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, local::kQualityCellSize));
    for (int i = 0; i < 3; ++i)
        WriteRef(stream, At<void*>(self, field::kEQGrids + 4 * i));
    for (int i = 0; i < 3; ++i)
        WriteRef(stream, At<void*>(self, field::kHQGrids + 4 * i));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, local::kFloat98));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, local::kFloat9C));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, local::kState94));
    const bool pending = At<uint8_t>(self, local::kFlagA0) || At<uint32_t>(self, local::kState94) != 0xA;
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(pending));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kPopulation));
    WriteBlock(stream, self + field::kHealth);
    WriteBlock(stream, self + field::kSchool);
    WriteList(stream, self + local::kList19C);
    WriteList(stream, self + local::kList1A0);
    WriteList(stream, self + local::kList1A4);
    for (const std::size_t vector : {field::kSchoolBuildings, field::kHospitalBuildings}) {
        Virtual<uint8_t>(stream, 0x24,
                         static_cast<uint32_t>(At<char*>(self, vector + 4) - At<char*>(self, vector)) / 4);
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<char*>(self, vector + 4) - At<char*>(self, vector)) / 4; ++i)
            WriteList(stream, At<char*>(self, vector) + 4 * i);
    }
    WriteList(stream, self + local::kList1C0);
    reinterpret_cast<CellFn>(addr::SC4Stream_WriteCellIterator)(stream, self + local::kIterator);
    Virtual<uint8_t>(stream, 0x2C, At<int64_t>(self, local::kStep348));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, local::kA8));
    reinterpret_cast<BoolFn>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(self, field::kSchoolAutoBudget));
    reinterpret_cast<BoolFn>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(self, field::kHospitalAutoBudget));
    return 1;
}

// A vector of grid references read back: its count, the vector resized as
// the game does (shortened, or filled with nulls), then each reference.
void ReadGridVector(void* stream, char* self, std::size_t offset)
{
    using FillFn = void(__thiscall*)(void* vector, void** where, uint32_t count, void* const* value);
    uint32_t count = 1;
    Virtual<uint8_t>(stream, 0x24, &count);
    void** const begin = At<void**>(self, offset);
    void** const end = At<void**>(self, offset + 4);
    const uint32_t size = static_cast<uint32_t>(end - begin);
    void* const none = nullptr;
    if (count < size)
        At<void**>(self, offset + 4) = begin + count;
    else
        reinterpret_cast<FillFn>(addr::SC4PtrVector_FillInsert)(self + offset, end, count - size, &none);
    for (uint32_t i = 0; i < static_cast<uint32_t>(At<void**>(self, offset + 4) - At<void**>(self, offset)); ++i)
        ReadRef(stream, kIidGrid, &At<void**>(self, offset)[i], 0u);
}

// Slot 4 (0x007009A0): the record back (versions 1 to 7; older ones get
// defaults for the iterator, the step and the auto budgets), the state
// +0x94 renumbered from 1-3 to 13-15, and +0x3C0 cleared and +0xD set.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    using ResizeFn = void(__thiscall*)(void* vector, uint32_t count);
    using CellFn = void(__cdecl*)(void* stream, void* iterator);
    using BoolFn = void(__cdecl*)(void* stream, void* value);
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - 8;
    const auto ReadList = reinterpret_cast<ListStreamFn>(addr::SC4Stream_ReadReferenceList);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    Virtual<uint8_t>(stream, 0x20, self + field::kMapFlag);
    for (int i = 0; i < 3; ++i)
        ReadRef(stream, kIidGrid, self + field::kProximity + 4 * i, 0u);
    Virtual<uint8_t>(stream, 0x20, self + local::kCellSize);
    for (int i = 0; i < 3; ++i)
        ReadRef(stream, kIidGrid, self + field::kPopulationGrids + 4 * i, 0u);
    ReadRef(stream, kIidGrid, self + local::kBoolGrid54, 0u);
    ReadGridVector(stream, self, local::kBoolGrids58);
    ReadGridVector(stream, self, local::kBoolGrids64);
    ReadRef(stream, kIidGrid, self + local::kFloatGrid70, 0u);
    ReadRef(stream, kIidGrid, self + local::kFloatGrid74, 0u);
    Virtual<uint8_t>(stream, 0x20, self + local::kQualityCellSize);
    for (int i = 0; i < 3; ++i)
        ReadRef(stream, kIidGrid, self + field::kEQGrids + 4 * i, 0u);
    for (int i = 0; i < 3; ++i)
        ReadRef(stream, kIidGrid, self + field::kHQGrids + 4 * i, 0u);
    Virtual<uint8_t>(stream, 0x30, self + local::kFloat98);
    Virtual<uint8_t>(stream, 0x30, self + local::kFloat9C);
    uint32_t state = 0;
    Virtual<uint8_t>(stream, 0x24, &state);
    At<uint32_t>(self, local::kState94) = state;
    Virtual<uint8_t>(stream, 0x14, self + local::kFlagA0);
    Virtual<uint8_t>(stream, 0x20, self + field::kPopulation);
    ReadBlock(stream, self + field::kHealth, version);
    ReadBlock(stream, self + field::kSchool, version);
    ReadList(stream, self + local::kList19C);
    ReadList(stream, self + local::kList1A0);
    ReadList(stream, self + local::kList1A4);
    for (const std::size_t vector : {field::kSchoolBuildings, field::kHospitalBuildings}) {
        uint32_t count = 0;
        Virtual<uint8_t>(stream, 0x24, &count);
        reinterpret_cast<ResizeFn>(addr::SC4ListVector_Resize)(self + vector, count);
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<char*>(self, vector + 4) - At<char*>(self, vector)) / 4; ++i)
            ReadList(stream, At<char*>(self, vector) + 4 * i);
    }
    ReadList(stream, self + local::kList1C0);
    if (version >= 3) {
        reinterpret_cast<CellFn>(addr::SC4Stream_ReadCellIterator)(stream, self + local::kIterator);
        Virtual<uint8_t>(stream, 0x2C, self + local::kStep348);
        if (version >= 4)
            Virtual<uint8_t>(stream, 0x20, self + local::kA8);
    } else {
        ResetIterator(self);
        if (At<uint32_t>(self, local::kState94) == 4)
            At<uint32_t>(self, local::kState94) = 0xB;
        At<int64_t>(self, local::kStep348) = 0;
        At<uint32_t>(self, local::kA8) = 0;
    }
    if (version >= 7) {
        reinterpret_cast<BoolFn>(addr::SC4Stream_ReadBool)(stream, self + field::kSchoolAutoBudget);
        reinterpret_cast<BoolFn>(addr::SC4Stream_ReadBool)(stream, self + field::kHospitalAutoBudget);
    } else {
        At<uint8_t>(self, field::kHospitalAutoBudget) = 0;
        At<uint8_t>(self, field::kSchoolAutoBudget) = 0;
    }
    uint32_t& renumbered = At<uint32_t>(self, local::kState94);
    if (renumbered == 1)
        renumbered = 0xD;
    if (renumbered == 2)
        renumbered = 0xE;
    if (renumbered == 3)
        renumbered = 0xF;
    At<uint8_t>(self, 0x3C0) = 0;
    At<uint8_t>(self, field::kSetUp) = 1;
    return 1;
}

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kInitSlots[] = {OPENSC4_SLOT(addr::cSC4ResidentialSimulator_vftable, 3, 0x006FEAD0, Init)};
const Slot kShutdownSlots[] = {OPENSC4_SLOT(addr::cSC4ResidentialSimulator_vftable, 4, 0x006F97F0, Shutdown)};
const Slot kSaveSlots[] = {OPENSC4_SLOT(addr::cSC4ResidentialSimulator_vftable_8, 3, 0x006F8C40, Write)};
const Slot kLoadSlots[] = {OPENSC4_SLOT(addr::cSC4ResidentialSimulator_vftable_8, 4, 0x007009A0, Read)};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("residential %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    log::Write("residential %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }
bool InstallShutdown(registry::Mode) { return SwapSlots(kShutdownSlots, "shutdown"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }
bool InstallLoad(registry::Mode) { return SwapSlots(kLoadSlots, "load"); }

const bool g_registered[] = {
    registry::Register({"simulators.residential.init", "simulators/residential", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ResidentialSimulator_vftable,
                        "Our residential Init and its tuning (docs/residential.md)", false, InstallInit}),
    registry::Register({"simulators.residential.shutdown", "simulators/residential", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ResidentialSimulator_vftable,
                        "Our residential Shutdown (docs/residential.md)", false, InstallShutdown}),
    registry::Register({"simulators.residential.save", "simulators/residential", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ResidentialSimulator_vftable_8,
                        "Our residential Write: the save record, version 7 (docs/residential.md)", false,
                        InstallSave}),
    registry::Register({"simulators.residential.load", "simulators/residential", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ResidentialSimulator_vftable_8,
                        "Our residential Read, versions 1 to 7 (docs/residential.md)", false, InstallLoad}),
};

// --- Our object -------------------------------------------------------------------

constexpr uint32_t kClassId = 0x4990C013;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidResidential = 0x077AC1EE;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x3D0;

constexpr int kSlots = 61, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006F6E60).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidResidential:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006F7DD0): Shutdown and the deleting destructor (slot
// 60) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0xF0, 1u);
    return 0;
}

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

// As the game's (0x00700330): the members destroyed by the game's own
// member destructor (0x006FE840), which also restores the game's vtables.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    using FreeObjectFn = void(__cdecl*)(void* block);
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("residential object: destroying %p", static_cast<void*>(self));
    reinterpret_cast<SelfFn>(addr::cSC4ResidentialSimulator_DestroyMembers)(self);
    if (flags & 1)
        reinterpret_cast<FreeObjectFn>(addr::SC4_operator_delete)(self);
    return self;
}

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

// A list sentinel from the list node allocator (0x00413DD0), into `slot`.
void NewList(char* self, std::size_t slot)
{
    using AllocateFn = void**(__thiscall*)(void* list, uint32_t count, uint32_t zero);
    At<void*>(self, slot) = nullptr;
    void** const node = reinterpret_cast<AllocateFn>(addr::SC4List_AllocateNodes)(self + slot, 1, 0);
    node[0] = node;
    node[1] = node;
    At<void**>(self, slot) = node;
}

// As the game's constructor (0x00700510).
void Construct(char* self)
{
    using RandomFn = void*(__thiscall*)(void* random, int32_t seed);
    using CurveFn = void(__thiscall*)(void* curve);
    using AllocFn = void*(__cdecl*)(std::size_t size);
    using FillFn = void(__thiscall*)(void* vector, void** where, uint32_t count, void* const* value);
    using ResizeFn = void(__thiscall*)(void* vector, uint32_t count);
    const auto Curve = reinterpret_cast<CurveFn>(addr::SC4Curve_Init);
    SetVtables(self);
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint8_t>(self, field::kSetUp) = 0;
    for (std::size_t offset = 0x10; offset <= 0x30; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kMapFlag) = 1;
    At<uint32_t>(self, 0x38) = 0;
    At<uint32_t>(self, 0x3C) = 0;
    At<uint32_t>(self, 0x40) = 0;
    At<uint32_t>(self, 0x54) = 0;
    At<uint32_t>(self, 0x44) = 4;
    for (std::size_t offset = 0x58; offset <= 0x6C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x78) = 4;
    At<uint32_t>(self, 0x70) = 0;
    At<uint32_t>(self, 0x74) = 0;
    At<uint32_t>(self, 0x94) = 0;
    At<uint32_t>(self, 0x98) = 0;
    At<uint32_t>(self, 0x9C) = 0;
    At<uint8_t>(self, 0xA0) = 1;
    At<uint32_t>(self, 0xA4) = 0;
    At<uint32_t>(self, 0xA8) = 0;
    At<uint32_t>(self, 0xAC) = 0;
    At<uint32_t>(self, 0xB0) = 0;
    reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(self + local::kRandom, -1);
    for (std::size_t offset = 0xD0; offset <= 0xDC; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0xE4) = 0;
    Curve(self + 0xE8);
    Curve(self + 0xF8);
    Curve(self + 0x108);
    At<uint32_t>(self, 0x120) = 0;
    At<uint32_t>(self, 0x124) = 0;
    At<uint32_t>(self, 0x128) = 0;
    At<uint32_t>(self, 0x134) = 0;
    At<uint32_t>(self, 0x138) = 0;
    At<uint32_t>(self, 0x13C) = 0;
    At<uint32_t>(self, 0x144) = 0;
    Curve(self + 0x148);
    Curve(self + 0x158);
    Curve(self + 0x168);
    At<uint32_t>(self, 0x180) = 0;
    At<uint32_t>(self, 0x184) = 0;
    At<uint32_t>(self, 0x188) = 0;
    At<uint32_t>(self, local::kMap194) = 0;
    void** map = static_cast<void**>(reinterpret_cast<AllocFn>(addr::SC4_stl_allocate)(0x3C));
    map[0] = map;
    map[1] = map;
    At<void**>(self, local::kMap194) = map;
    At<uint32_t>(self, local::kMap198) = 0;
    map = static_cast<void**>(reinterpret_cast<AllocFn>(addr::SC4_stl_allocate)(0x3C));
    map[0] = map;
    map[1] = map;
    At<void**>(self, local::kMap198) = map;
    NewList(self, local::kList19C);
    NewList(self, local::kList1A0);
    NewList(self, local::kList1A4);
    for (std::size_t offset = 0x1A8; offset <= 0x1BC; offset += 4)
        At<uint32_t>(self, offset) = 0;
    NewList(self, local::kList1C0);
    for (int i = 0; i < 9; ++i)
        Curve(self + local::kCurves264 + 0x10 * i);
    for (std::size_t offset = 0x2FC; offset <= 0x310; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x324) = 0;
    At<uint32_t>(self, 0x334) = 1;
    At<uint32_t>(self, 0x338) = 0x80000000u;
    At<uint32_t>(self, 0x33C) = 0x80000000u;
    At<uint32_t>(self, 0x340) = 0x7FFFFFFFu;
    At<uint32_t>(self, 0x344) = 0x7FFFFFFFu;
    ResetIterator(self);
    At<uint32_t>(self, 0x348) = 0;
    At<uint32_t>(self, 0x34C) = 0;
    At<uint32_t>(self, 0x360) = 0;
    At<uint32_t>(self, 0x364) = 0;
    Curve(self + 0x390);
    Curve(self + 0x3A0);
    Curve(self + 0x3B0);
    At<uint8_t>(self, 0x3C0) = 0;
    At<uint32_t>(self, local::kHolder3C4) = 0;
    At<uint8_t>(self, field::kSchoolAutoBudget) = 0;
    At<uint8_t>(self, field::kHospitalAutoBudget) = 0;
    for (std::size_t offset = 0x48; offset <= 0x50; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = 0x7C; offset <= 0x90; offset += 4)
        At<uint32_t>(self, offset) = 0;
    void* const none = nullptr;
    for (const auto& [vector, lists, count] : {std::tuple<std::size_t, std::size_t, uint32_t>{local::kBoolGrids58, field::kSchoolBuildings, 7},
                                               std::tuple<std::size_t, std::size_t, uint32_t>{local::kBoolGrids64, field::kHospitalBuildings, 3}}) {
        const uint32_t size = static_cast<uint32_t>(At<void**>(self, vector + 4) - At<void**>(self, vector));
        if (size > count)
            At<void**>(self, vector + 4) = At<void**>(self, vector) + count;
        else
            reinterpret_cast<FillFn>(addr::SC4PtrVector_FillInsert)(self + vector, At<void**>(self, vector + 4),
                                                                    count - size, &none);
        reinterpret_cast<ResizeFn>(addr::SC4ListVector_Resize)(self + lists, count);
        for (uint32_t i = 0; i < count; ++i)
            At<void**>(self, vector)[i] = nullptr;
    }
    ResetIterator(self);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4ResidentialSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4ResidentialSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4ResidentialSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[60] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x00700E60).
cIGZUnknown* Create()
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("residential object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode) { return game::IsBuild641() && director::AddClass(kClassId, Create); }

const bool g_registeredObject = registry::Register({
    "simulators.residential.object",
    "simulators/residential",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4ResidentialSimulator object under the game's class ID: factory, constructor, identity; methods "
    "from the (patched) vtables; the members still destroyed by the game's code (docs/residential.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::residential_sim
