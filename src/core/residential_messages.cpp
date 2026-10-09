// Our cSC4ResidentialSimulator (build 641; docs/residential.md), the
// messages and the simulation: DoMessage (cIGZMessageTarget2 slot 3) and
// everything it reaches in the simulator's own code: the step through the
// sixteen states (strikes, the service records, the proximity maps, the
// walk over the cells for EQ, HQ, population and age, the coverage of
// schools and hospitals and the monthly figures) and the bookkeeping of
// buildings added and removed. The queries and occupant slots it calls are
// called through the vtable, as the game does (residential_queries.cpp,
// residential_occupants.cpp).
#include "core/residential_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::residential_sim {
namespace {

// Fields used here besides residential_sim.h (offsets from the object's
// start; names ours):
//   0x28, 0x2C, 0x30  three objects by wealth (the development simulator's
//                     slot 32); their slot 15 is a uint16 grid of residents
//   0x48 + 4k         the population grids (uint16) by wealth k
//   0x54              the age grid (uint8)
//   0x58, 0x64        vectors of coverage grids (uint8), one per school or
//                     hospital list
//   0x70, 0x74        the school and health share grids (float)
//   0x7C, 0x88        the EQ and HQ grids (float) by wealth
//   0x94              the step's state (0-15)
//   0xA0              uint8: something changed (buildings, month)
//   0xA8              the population summed by the cell walk
//   0xAC, 0xB0        the people a school or a hospital serves
//   0xB8              the random number generator (cRZRandom)
//   0xD4, 0x134       the health and school blocks: +0xC strike length
//                     (days), +0x10 strike threshold, +0x24 strike curve,
//                     +0x44 on strike, +0x48 strike day, +0x5C message type
//   0x194, 0x198      the health and school service records (lists of
//                     0x34-byte records, see Record)
//   0x19C, 0x1A0      new schools and hospitals (SC4List of occupants)
//   0x1A4, 0x1C0      new and known buildings with properties 0x6A19F6B5
//                     and 0xAA19F6EA (SC4List of occupants)
//   0x1A8, 0x1B4      vectors of SC4List: schools and hospitals by type
//   0x1C4, 0x214      twenty age buckets: residents and their EQ summed
//   0x264             nine curves (16 bytes each), the monthly EQ figures
//   0x2FC, 0x308      vectors of uint32: capacity divisors per list
//   0x314, 0x318      the people the EQ and HQ spreads reached
//   0x324             float: the coverage radius scale
//   0x328             a cell iterator over the city
//   0x348             int64: life expectancy times residents, summed
//   0x350             the cell walk's parameters (0x70 bytes, see LoadParams)
//   0x3C0             uint8: the parameters were loaded
//   0x3C4             a temporary byte grid for the proximity maps
namespace local {
constexpr std::size_t kWealthSources = 0x28;
constexpr std::size_t kAgeGrid = 0x54;
constexpr std::size_t kSchoolCover = 0x58;
constexpr std::size_t kHealthCover = 0x64;
constexpr std::size_t kSchoolShare = 0x70;
constexpr std::size_t kHealthShare = 0x74;
constexpr std::size_t kState = 0x94;
constexpr std::size_t kDirty = 0xA0;
constexpr std::size_t kPopulationSum = 0xA8;
constexpr std::size_t kSchoolServed = 0xAC;
constexpr std::size_t kHealthServed = 0xB0;
constexpr std::size_t kRandom = 0xB8;
constexpr std::size_t kHealthRecords = 0x194;
constexpr std::size_t kSchoolRecords = 0x198;
constexpr std::size_t kNewSchools = 0x19C;
constexpr std::size_t kNewHospitals = 0x1A0;
constexpr std::size_t kNewOthers = 0x1A4;
constexpr std::size_t kOthers = 0x1C0;
constexpr std::size_t kAgeCounts = 0x1C4;
constexpr std::size_t kAgeQuality = 0x214;
constexpr std::size_t kMonthlyCurves = 0x264;
constexpr std::size_t kSchoolDivisors = 0x2FC;
constexpr std::size_t kHealthDivisors = 0x308;
constexpr std::size_t kSchoolReached = 0x314;
constexpr std::size_t kHealthReached = 0x318;
constexpr std::size_t kRadiusScale = 0x324;
constexpr std::size_t kIterator = 0x328;
constexpr std::size_t kLifeSum = 0x348;
constexpr std::size_t kParams = 0x350;
constexpr std::size_t kParamsLoaded = 0x3C0;
constexpr std::size_t kTempGrid = 0x3C4;
} // namespace local

constexpr uint32_t kStriking = 0xEAA69F83;  // building property: the day its strike began
constexpr uint32_t kStatistic = 0x89EFA536; // message type of the figures
constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kIidBuilding = 0x87DDA3A9;

struct Node {
    Node* next;
    Node* prev;
};

template <typename T>
T& Value(Node* node)
{
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(node) + 8);
}

Node*& Head(void* list) { return *static_cast<Node**>(list); }

void LinkBefore(Node* node, Node* where)
{
    Node* const last = where->prev;
    node->next = where;
    node->prev = last;
    last->next = node;
    where->prev = node;
}

struct Curve {
    void* begin;
    void* end;
    void* capacity;
    uint32_t count;
};

// A service record (0x34 bytes; in 0x3C-byte list nodes).
struct Record {
    float scale;
    Curve distance;
    float factor14;
    float factor18;
    Curve age;
    Node* groups; // list of uint32 occupant groups it applies to (empty: all)
    float factor30;
};
static_assert(sizeof(Record) == 0x34);

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

struct Point {
    float x;
    float y;
};

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// cISC4BudgetSimulator::BudgetItem (0x20 bytes).
struct alignas(8) BudgetItem {
    uint32_t department;
    uint32_t line;
    uint32_t purpose;
    uint32_t padding;
    int64_t cost;
    uint32_t word18;
    uint32_t funding;
};
static_assert(sizeof(BudgetItem) == 0x20);

// The time budget of a pass (0x0068CC20; rdtsc based), checked every 0x400.
struct Deadline {
    uint32_t start[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using CtorFn = void*(__thiscall*)(void* object);
using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using GetUint16Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint16_t* value);
using GetUint8Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using SetUint32Fn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using SetFloatFn = void(__cdecl*)(void* holder, uint32_t property, float value);
using EvaluateFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using UniformFn = double(__thiscall*)(void* random, double low, double high);
using SetPointsFn = uint8_t(__thiscall*)(void* curve, const Vector* points);
using LoadPropertyFn = uint8_t(__thiscall*)(void* curve, void* property);
using LoadExemplarFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
using CurveInitFn = void*(__thiscall*)(void* curve);
using CurveCopyFn = void*(__thiscall*)(void* curve, const void* other);
using PointInsertFn = void(__thiscall*)(Vector* vector, Point* where, const Point* value, void* allocator,
                                        uint32_t count, uint32_t atEnd);
using InsertRangeFn = void(__thiscall*)(void* list, Node* where, Node* first, Node* last, void* dummy);
using FromTextFn = void*(__thiscall*)(GameString* string, const char* text);
using FromRangeFn = void(__thiscall*)(char** string, const char* first, const char* last, void* allocator);
using DataFn = const char*(__thiscall*)(const GameString* string);
using AllocateNodesFn = Node*(__thiscall*)(void* list, uint32_t count, uint32_t zero);
using ListFn = void(__thiscall*)(void* list);
using EraseFn = Node**(__thiscall*)(void* list, Node** out, Node* node);
using GridCtorFn = void*(__thiscall*)(void* grid, uint32_t unused);
using MarkFn = void(__cdecl*)(void* grid, void* buildings, uint32_t key);
using SpreadPassFn = uint32_t(__cdecl*)(void* rows, uint32_t rowCount, uint32_t columnCount);
using CellsInRadiusFn = void(__cdecl*)(float x, float z, float radius, float cellSize, uint32_t rows,
                                       uint32_t columns, void* cells);
using SpreadQualityFn = void(__cdecl*)(void* ageGrid, void* qualityGrid, void* distanceCurves, float x, float z,
                                       float radius, void* ageCurves, float factor, float cap, uint32_t capacity,
                                       void* coverGrid, void* populationGrid, uint32_t* served);
using StartWithFn = void(__thiscall*)(Deadline* deadline, uint32_t budget, uint32_t* step);
using DeadlineFn = uint8_t(__thiscall*)(Deadline* deadline);
using CellNextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
using CellRestartFn = void(__thiscall*)(void* iterator, uint32_t a, uint32_t b);
using StoreFn = void(__cdecl*)(uint8_t* cell, uint32_t value, uint32_t argument);

const auto StlAllocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto GetUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
const auto GetBool = reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool);
const auto GetUint16 = reinterpret_cast<GetUint16Fn>(addr::SC4_GetPropertyUint16);
const auto GetUint8 = reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyUint8);
const auto GetPercentage = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyPercentage);
const auto SetUint32 = reinterpret_cast<SetUint32Fn>(addr::SC4_SetPropertyUint32);
const auto SetFloat = reinterpret_cast<SetFloatFn>(addr::SC4_SetPropertyFloat);
const auto Evaluate = reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate);
const auto Uniform = reinterpret_cast<UniformFn>(addr::cRZRandom_Uniform);
const auto SetPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
const auto LoadFromProperty = reinterpret_cast<LoadPropertyFn>(addr::SC4Curve_LoadFromProperty);
const auto LoadFromExemplar = reinterpret_cast<LoadExemplarFn>(addr::SC4Curve_LoadFromExemplar);
const auto CurveInit = reinterpret_cast<CurveInitFn>(addr::SC4Curve_Init);
const auto CurveCopy = reinterpret_cast<CurveCopyFn>(addr::SC4Curve_Copy);
const auto PointInsert = reinterpret_cast<PointInsertFn>(addr::SC4PointVector_InsertN);
const auto AllocateNodes = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes);
const auto ListClear2 = reinterpret_cast<ListFn>(addr::SC4List_Clear2);
const auto ListErase = reinterpret_cast<EraseFn>(addr::SC4List_Erase);

// The values the game rounds to float through 0x00A823A4 and the like.
constexpr double kHundredth = static_cast<double>(0.01f);

double AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// __ftol (0x009EEF04): truncation; the low word of its int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// FILD of a uint32 the game fixes up with + 2^32 when negative.
double Unsigned(uint32_t value) { return static_cast<double>(value); }

// The game's float rounding: (value + 1.5 * 2^23) as float, its bits less
// 0x4B400000 (round to nearest).
int32_t RoundFloat(double value)
{
    const float rounded = static_cast<float>(value + 12582912.0);
    return static_cast<int32_t>(Bits(rounded) + 0xB4C00000u);
}

float FullFunding() { return Global<float>(addr::cSC4ResidentialSimulator_FullFunding); }
uint32_t FullFundingBits() { return Global<uint32_t>(addr::cSC4ResidentialSimulator_FullFunding); }
uint32_t DefaultPercentageBits() { return Global<uint32_t>(addr::cSC4ResidentialSimulator_DefaultPercentage); }

void* Properties(void* occupant) { return Virtual<void*>(occupant, 0x18); }

void* Grid(char* self, std::size_t field, uint32_t index) { return At<void**>(self, field)[index]; }

uint32_t VectorSize(char* self, std::size_t field)
{
    return static_cast<uint32_t>((At<char*>(self, field + 4) - At<char*>(self, field)) >> 2);
}

Node* NewHead(std::size_t size)
{
    auto* const head = static_cast<Node*>(StlAllocate(size));
    head->next = head;
    head->prev = head;
    return head;
}

void* NewMessage()
{
    void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    if (message)
        message = reinterpret_cast<CtorFn>(addr::cGZMessage_cGZMessage)(message);
    return message;
}

// Today's day number (the city's slot 50, its slot 7, its slot 26).
uint32_t Today(char* self)
{
    void* const simulators = Virtual<void*>(At<void*>(self, field::kCity), 0xC8);
    return Virtual<uint32_t>(Virtual<void*>(simulators, 0x1C), 0x68);
}

void StartDeadline(Deadline& deadline, uint32_t budget, uint32_t bit, std::uintptr_t stepGlobal)
{
    uint32_t& once = Global<uint32_t>(addr::cSC4ResidentialSimulator_PassOnce);
    if (!(once & bit)) {
        once |= bit;
        Global<uint32_t>(stepGlobal) = 0x400;
        Global<uint32_t>(stepGlobal + 4) = 0;
    }
    reinterpret_cast<StartWithFn>(addr::SC4Deadline_StartWith)(&deadline, budget, &Global<uint32_t>(stepGlobal));
}

// The loop head of a pass: false when the budget ran out (now or before).
bool InBudget(Deadline& deadline)
{
    deadline.count += deadline.step;
    if (!(deadline.count & 0x3FF) && reinterpret_cast<DeadlineFn>(addr::SC4Deadline_Expired)(&deadline)) {
        deadline.timedOut = 1;
        return false;
    }
    return !deadline.timedOut;
}

void EndDeadline(Deadline& deadline) { reinterpret_cast<DeadlineFn>(addr::SC4Deadline_End)(&deadline); }

// Two points (0, 100) and (100, 100) as the game builds them for default curves.
void FlatPoints(Vector& points, void* allocator)
{
    Point point{0.0f, 100.0f};
    PointInsert(&points, nullptr, &point, allocator, 1, 1);
    point = {100.0f, 100.0f};
    if (points.end != points.capacity) {
        if (points.end)
            *reinterpret_cast<Point*>(points.end) = {100.0f, 100.0f};
        points.end += sizeof(Point);
    } else {
        PointInsert(&points, reinterpret_cast<Point*>(points.end), &point, allocator, 1, 1);
    }
}

// --- Small helpers -----------------------------------------------------------

// 0x006F6F80: the store of CopyGrid: the value's low byte into the cell.
void __cdecl StoreByte(uint8_t* cell, uint32_t value, uint32_t) { *cell = static_cast<uint8_t>(value); }

// 0x006F6EE0: the age buckets, the life sum and the population sum cleared.
void ClearAgeBuckets(char* self)
{
    for (std::size_t i = 0; i < 20; ++i) {
        At<uint32_t>(self, local::kAgeCounts + 4 * i) = 0;
        At<uint32_t>(self, local::kAgeQuality + 4 * i) = 0;
    }
    At<uint32_t>(self, local::kLifeSum) = 0;
    At<uint32_t>(self, local::kLifeSum + 4) = 0;
    At<uint32_t>(self, local::kPopulationSum) = 0;
}

// 0x006F6F30: whether an occupant is on strike (property 0xEAA69F83 set).
bool OnStrike(void* occupant)
{
    uint32_t day = 0;
    if (!GetUint32(Properties(occupant), kStriking, &day))
        return false;
    return day != 0;
}

// 0x006F7D00: a figure sent (message 0x89EFA536, data 1 the kind, data 2
// the value) when the message server is there.
void SendFigure(char* self, uint32_t kind, uint32_t value)
{
    if (!At<void*>(self, field::kMessages))
        return;
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, kStatistic);
    Virtual<void>(message, 0x2C, kind);
    Virtual<void>(message, 0x40, value);
    Virtual<uint8_t>(At<void*>(self, field::kMessages), 0xC, message);
    Virtual<uint32_t>(message, 0x8);
}

// 0x006F7FF0: the average of a pollution value (the pollution simulator's
// slot 6, kind 0-2) over n x n cells; unsigned throughout.
uint32_t AveragePollution(uint32_t kind, uint32_t x0, uint32_t z0, uint32_t n)
{
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    uint32_t sum = 0;
    const uint32_t xEnd = x0 + n;
    const uint32_t zEnd = z0 + n;
    for (uint32_t x = x0; x < xEnd; ++x)
        for (uint32_t z = z0; z < zEnd; ++z)
            sum += Virtual<uint16_t>(pollution, 0x18, kind, x, z);
    return sum / (n * n);
}

// 0x006F84C0: a strike begins for the block unless one is on: the day
// noted, message (the block's type) with the occupant posted.
bool BeginStrike(char* self, char* block, void* occupant)
{
    if (At<uint8_t>(block, 0x44))
        return false;
    At<uint8_t>(block, 0x44) = 1;
    At<uint32_t>(block, 0x48) = Today(self);
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, At<uint32_t>(block, 0x5C));
    Virtual<void>(message, 0x30, occupant);
    Virtual<void>(message, 0x84, occupant);
    Virtual<void>(message, 0x40, 1u);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
    return true;
}

// 0x006F8570: the block's strike ends after `days`: message posted.
bool EndStrike(char* block, uint32_t days)
{
    if (!At<uint8_t>(block, 0x44))
        return false;
    At<uint8_t>(block, 0x44) = 0;
    At<uint32_t>(block, 0x48) = 0;
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, At<uint32_t>(block, 0x5C));
    Virtual<void>(message, 0x40, 0u);
    Virtual<void>(message, 0x54, days);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
    return true;
}

// 0x006F9340: the products of the records' scale, factor 0x18 and factor
// 0x14 (each 1 without records).
void RecordFactors(void* records, float* scale, float* factor18, float* factor14)
{
    Node* const head = Head(records);
    *scale = 1.0f;
    *factor14 = 1.0f;
    *factor18 = 1.0f;
    for (Node* node = head->next; node != head; node = node->next) {
        const Record& record = Value<Record>(node);
        *scale = static_cast<float>(static_cast<double>(*scale) * record.scale);
        *factor18 = static_cast<float>(static_cast<double>(record.factor18) * *factor18);
        *factor14 = static_cast<float>(static_cast<double>(record.factor14) * *factor14);
    }
}

// 0x006F94F0: whether a record applies to an occupant: yes without groups,
// else when one of the occupant's values of `property` is among them.
bool RecordApplies(void* occupant, char* record, uint32_t property)
{
    void* const holder = Properties(occupant);
    Node* const groups = At<Node*>(record, 0x2C);
    if (groups->next == groups)
        return true;
    void* const found = Virtual<void*>(holder, 0x24, property);
    if (!found)
        return false;
    void* const variant = Virtual<void*>(found, 0x14);
    const auto* const values = Virtual<const uint32_t*>(variant, 0x1A4);
    for (uint32_t i = 0; i < Virtual<uint32_t>(variant, 0x10); ++i) {
        Node* const head = At<Node*>(record, 0x2C);
        Node* node = head->next;
        while (node != head && Value<uint32_t>(node) != values[i])
            node = node->next;
        if (node != At<Node*>(record, 0x2C))
            return true;
    }
    return false;
}

// 0x006FB9F0: a record copied (curves through the game's copy constructor,
// the group list through its range insert).
void CopyRecord(char* to, char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
    CurveCopy(to + 0x4, from + 0x4);
    At<uint32_t>(to, 0x14) = At<uint32_t>(from, 0x14);
    At<uint32_t>(to, 0x18) = At<uint32_t>(from, 0x18);
    CurveCopy(to + 0x1C, from + 0x1C);
    At<Node*>(to, 0x2C) = nullptr;
    Node* const groups = NewHead(0xC);
    At<Node*>(to, 0x2C) = groups;
    Node* const source = At<Node*>(from, 0x2C);
    void* dummy = from;
    reinterpret_cast<InsertRangeFn>(addr::SC4StdList_InsertRangeUint32)(to + 0x2C, groups->next, source->next, source,
                                                                         &dummy);
    At<uint32_t>(to, 0x30) = At<uint32_t>(from, 0x30);
}

// 0x006FCB40: a record list emptied (each record's groups, curves, node);
// the head stays.
void ClearRecords(void* list)
{
    Node* node = Head(list)->next;
    while (node != Head(list)) {
        Node* const current = node;
        char* const record = reinterpret_cast<char*>(current) + 8;
        node = node->next;
        Node* group = At<Node*>(record, 0x2C)->next;
        while (group != At<Node*>(record, 0x2C)) {
            Node* const dead = group;
            group = group->next;
            Free(dead);
        }
        At<Node*>(record, 0x2C)->next = At<Node*>(record, 0x2C);
        At<Node*>(record, 0x2C)->prev = At<Node*>(record, 0x2C);
        if (At<Node*>(record, 0x2C))
            Free(At<Node*>(record, 0x2C));
        if (At<void*>(record, 0x1C))
            Free(At<void*>(record, 0x1C));
        if (At<void*>(record, 0x4))
            Free(At<void*>(record, 0x4));
        Free(current);
    }
    Head(list)->next = Head(list);
    Head(list)->prev = Head(list);
}

// 0x006FD070: the records of `from` that apply to the occupant (by
// `property`) copied into `to`, emptied first.
void FilterRecords(void* occupant, uint32_t property, void* from, void* to)
{
    ClearRecords(to);
    Node* const head = Head(from);
    for (Node* node = head->next; node != head; node = node->next) {
        char* const record = reinterpret_cast<char*>(node) + 8;
        if (!RecordApplies(occupant, record, property))
            continue;
        Node* const where = Head(to);
        auto* const copy = static_cast<Node*>(StlAllocate(0x3C));
        CopyRecord(reinterpret_cast<char*>(copy) + 8, record);
        LinkBefore(copy, where);
    }
}

// 0x006FCBD0 (also the traffic simulator's): a list of 24-byte nodes with a
// buffer at +8 emptied, buffers and nodes freed; the head stays.
void __fastcall ClearCurveList(void* list, void*)
{
    static bool logged = false;
    FirstCall("ClearCurveList", logged);
    Node* node = Head(list)->next;
    while (node != Head(list)) {
        Node* const current = node;
        void* const buffer = Value<void*>(current);
        node = node->next;
        if (buffer)
            Free(buffer);
        Free(current);
    }
    Head(list)->next = Head(list);
    Head(list)->prev = Head(list);
}

// 0x006FCD70: the records' distance curves copied into `distance` and their
// age curves into `age`, both emptied first.
void SplitCurves(void* records, void* distance, void* age)
{
    Node* const head = Head(records);
    Node* node = head->next;
    ClearCurveList(distance, nullptr);
    ClearCurveList(age, nullptr);
    for (; node != head; node = node->next) {
        Node* where = Head(distance);
        auto* copy = static_cast<Node*>(StlAllocate(0x18));
        CurveCopy(reinterpret_cast<char*>(copy) + 8, reinterpret_cast<char*>(node) + 0xC);
        LinkBefore(copy, where);
        where = Head(age);
        copy = static_cast<Node*>(StlAllocate(0x18));
        CurveCopy(reinterpret_cast<char*>(copy) + 8, reinterpret_cast<char*>(node) + 0x24);
        LinkBefore(copy, where);
    }
}

// 0x006FC400: three curves from an exemplar's properties (school or
// health ids), each flat 100 when missing.
void LoadCurves(void* holder, void* first, void* second, void* third, uint8_t school)
{
    static constexpr uint32_t kSchool[3] = {0xC9299662, 0xC92D7EC0, 0xE921A936};
    static constexpr uint32_t kHealth[3] = {0xA92AE3BD, 0xA92D9FCD, 0x2921A9CC};
    Vector points{};
    uint32_t allocator = school;
    FlatPoints(points, &allocator);
    const uint32_t* const ids = school ? kSchool : kHealth;
    void* const curves[3] = {first, second, third};
    for (int i = 0; i < 3; ++i) {
        if (void* const property = Virtual<void*>(holder, 0x24, ids[i]))
            LoadFromProperty(curves[i], property);
        else
            SetPoints(curves[i], &points);
    }
    if (points.begin)
        Free(points.begin);
}

// 0x006F7E00: the cells within the radius (scaled by +0x324) of the
// occupant's position (its lot's when it has one) appended to `cells`.
void CellsAround(char* self, void* occupant, float radius, float cellSize, uint32_t rows, uint32_t columns,
                 void* cells)
{
    float position[3] = {};
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant))
        Virtual<void>(lot, 0x3C, position);
    else
        Virtual<void>(occupant, 0x20, position);
    const float scaled = static_cast<float>(static_cast<double>(radius) * At<float>(self, local::kRadiusScale));
    reinterpret_cast<CellsInRadiusFn>(addr::SC4_CellsInRadius)(position[0], position[2], scaled, cellSize, rows,
                                                                columns, cells);
}

// --- Funding ---------------------------------------------------------------

// 0x006F8710: the occupant's two funding factors (its funding, slot 20):
// with a school (or health) budget item, the first from the strike factor
// (+0x13C, +0xDC) when on strike, else the funding curve (+0x148, +0xE8),
// the second from the curve +0x168 (+0x108); both 1 without the item.
void FundingFactors(char* self, void* occupant, float* first, float* second, uint8_t school, uint8_t ignoreStrike)
{
    *first = FullFunding();
    *second = FullFunding();
    uint32_t a = 0, b = 0;
    Virtual<uint8_t>(self, 0x50, occupant, &a, &b, static_cast<uint32_t>(school));
    const uint32_t purpose = school ? 0xEA5654B6u : 0xCA565486u;
    const std::size_t strike = school ? 0x13C : 0xDC;
    const std::size_t curve = school ? 0x148 : 0xE8;
    const std::size_t secondCurve = school ? 0x168 : 0x108;
    BudgetItem item{};
    void* const budget = At<void*>(self, field::kBudget);
    if (!Virtual<uint8_t>(budget, 0xCC, Properties(occupant), purpose, &item))
        return;
    double value;
    if (!ignoreStrike && OnStrike(occupant)) {
        value = AsFloat(a) * static_cast<double>(At<float>(self, strike));
    } else {
        const float x = static_cast<float>(AsFloat(a) * 100.0);
        value = Evaluate(self + curve, x) * kHundredth;
    }
    *first = static_cast<float>(value);
    const float x = static_cast<float>(AsFloat(b) * 100.0);
    *second = static_cast<float>(Evaluate(self + secondCurve, x) * kHundredth);
}

// 0x006F85F0: the occupant's first funding share (slot 20) through the
// funding curve when it has the budget item, else as it is.
uint32_t* FundingFactor(char* self, uint32_t* out, void* occupant, uint8_t school)
{
    uint32_t a = 0, b = 0;
    Virtual<uint8_t>(self, 0x50, occupant, &a, &b, static_cast<uint32_t>(school));
    const uint32_t purpose = school ? 0xEA5654B6u : 0xCA565486u;
    const std::size_t curve = school ? 0x148 : 0xE8;
    BudgetItem item{};
    void* const budget = At<void*>(self, field::kBudget);
    if (!Virtual<uint8_t>(budget, 0xCC, Properties(occupant), purpose, &item)) {
        *out = a;
        return out;
    }
    const float x = static_cast<float>(AsFloat(a) * 100.0);
    *out = Bits(static_cast<float>(Evaluate(self + curve, x) * kHundredth));
    return out;
}

// --- Strikes ----------------------------------------------------------------

// 0x006FA280: one building's strike rolled: while on strike, it ends after
// the block's length or when its rating (`ratingProperty`, else 1) reaches
// the threshold; else it begins with the chance of the block's curve at
// the risk (`riskProperty`, else the default) x 100. On a change, the
// property set and the news told. True when it changed.
bool RollStrike(char* self, void* occupant, char* block, uint32_t ratingProperty, uint32_t riskProperty)
{
    const uint32_t today = Today(self);
    uint32_t striking = 0;
    GetUint32(Properties(occupant), kStriking, &striking);
    uint32_t next = striking;
    float value = 0.0f;
    if (striking) {
        const uint32_t fallback = FullFundingBits();
        const double rating = GetFloat(Properties(occupant), ratingProperty, &value) ? value : AsFloat(fallback);
        if (today - striking > At<uint32_t>(block, 0xC))
            next = 0;
        else if (rating >= At<float>(block, 0x10))
            next = 0;
    } else {
        const uint32_t fallback = DefaultPercentageBits();
        const double risk = GetFloat(Properties(occupant), riskProperty, &value) ? value : AsFloat(fallback);
        const float chance = static_cast<float>(Evaluate(block + 0x24, static_cast<float>(risk * 100.0)));
        const double roll = Uniform(self + local::kRandom, 0.0, 100.0);
        if (roll < chance)
            next = today;
    }
    if (next == striking)
        return false;
    SetUint32(Properties(occupant), kStriking, next);
    if (void* const news = Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC)) {
        const char* const text = At<uint32_t>(block, 0x5C) == 0x077ACCD2
                                     ? Global<const char*>(addr::cSC4ResidentialSimulator_HealthStrikeText)
                                     : Global<const char*>(addr::cSC4ResidentialSimulator_EducationStrikeText);
        GameString string{};
        reinterpret_cast<FromTextFn>(addr::SC4String_FromText)(&string, text);
        if (next)
            Virtual<uint32_t>(news, 0x28, &string, occupant, 0u);
        else
            Virtual<uint32_t>(news, 0x30, &string, occupant);
        string.vtable = addr::SC4String_Vtable;
        if (string.begin)
            Free(string.begin);
    }
    return true;
}

// 0x006FAFF0: strikes rolled for the buildings of `group` in the lists;
// once any changed, each striking one may begin the block's strike; the
// block's strike ends when something changed and none strikes any more.
void UpdateStrikes(char* self, char* block, char* lists, uint32_t ratingProperty, uint32_t riskProperty,
                   uint32_t group)
{
    bool changed = false;
    uint32_t striking = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>((At<char*>(lists, 4) - At<char*>(lists, 0)) >> 2); ++i) {
        Node* const head = At<Node**>(lists, 0)[i];
        for (Node* node = head->next; node != head; node = node->next) {
            void* const occupant = Value<void*>(node);
            if (!Virtual<uint8_t>(occupant, 0x58, group))
                continue;
            if (RollStrike(self, occupant, block, ratingProperty, riskProperty))
                changed = true;
            uint32_t day = 0;
            if (!GetUint32(Properties(occupant), kStriking, &day) || day == 0)
                continue;
            ++striking;
            if (changed)
                BeginStrike(self, block, occupant);
        }
    }
    if (changed && At<uint8_t>(block, 0x44) && striking == 0)
        EndStrike(block, Today(self) - At<uint32_t>(block, 0x48));
}

// --- Service records and parameters ------------------------------------------

// 0x006FE0F0: the service records of every exemplar the service at
// 0x00B43D90 lists: a school record (0xA91B3AF4 ...) into `schools`, a
// health record (0x491B3AD5 ...) into `health`, each when it has any of
// its properties.
void LoadRecords(void* health, void* schools)
{
    void* const service = Global<void*>(addr::SC4_Service43D90);
    void* heap = nullptr;
    uint32_t count = Virtual<uint32_t>(service, 0x38);
    uint32_t onStack[0x40];
    uint32_t* ids = onStack;
    if (count >= 0x40) {
        heap = OperatorNew(count * 4);
        ids = static_cast<uint32_t*>(heap);
    }
    Record second{};
    Record first{};
    CurveInit(&second.distance);
    CurveInit(&second.age);
    second.groups = nullptr;
    second.groups = NewHead(0xC);
    CurveInit(&first.distance);
    CurveInit(&first.age);
    first.groups = nullptr;
    first.groups = NewHead(0xC);
    Vector points{};
    uint8_t allocator = 0;
    FlatPoints(points, &allocator);
    Virtual<void>(service, 0x3C, ids, &count);
    const auto Empty = [](Node* head) {
        Node* node = head->next;
        while (node != head) {
            Node* const dead = node;
            node = node->next;
            Free(dead);
        }
        head->next = head;
        head->prev = head;
    };
    const auto Add = [](void* list, Record& record) {
        Node* const where = Head(list);
        auto* const node = static_cast<Node*>(StlAllocate(0x3C));
        CopyRecord(reinterpret_cast<char*>(node) + 8, reinterpret_cast<char*>(&record));
        LinkBefore(node, where);
    };
    for (uint32_t i = 0; i < count; ++i) {
        void* const exemplar = Virtual<void*>(service, 0x50, ids[i]);
        if (!exemplar || !Virtual<void*>(exemplar, 0x40))
            continue;
        float value = 0.0f;
        bool any = false;
        first.scale = 1.0f;
        SetPoints(&first.distance, &points);
        first.factor14 = 1.0f;
        first.factor30 = 1.0f;
        first.factor18 = 1.0f;
        SetPoints(&first.age, &points);
        Empty(first.groups);
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0xA91B3AF4, &value)) {
            any = true;
            first.scale = static_cast<float>(value * kHundredth);
        }
        if (void* const property = Virtual<void*>(Virtual<void*>(exemplar, 0x40), 0x24, 0xA91B3AFAu)) {
            LoadFromProperty(&first.distance, property);
            any = true;
        }
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0xA92D9D7A, &value)) {
            any = true;
            first.factor14 = static_cast<float>(value * kHundredth);
        }
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0x692EF65A, &value)) {
            any = true;
            first.factor30 = static_cast<float>(value * kHundredth);
        }
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0x892D9D02, &value)) {
            any = true;
            first.factor18 = static_cast<float>(value * kHundredth);
        }
        if (void* const property = Virtual<void*>(Virtual<void*>(exemplar, 0x40), 0x24, 0xC91B3B02u)) {
            LoadFromProperty(&first.age, property);
            Add(schools, first);
        } else if (any) {
            Add(schools, first);
        }
        any = false;
        second.scale = 1.0f;
        SetPoints(&second.distance, &points);
        second.factor14 = 1.0f;
        second.factor30 = 1.0f;
        second.factor18 = 1.0f;
        SetPoints(&second.age, &points);
        Empty(second.groups);
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0x491B3AD5, &value)) {
            any = true;
            second.scale = static_cast<float>(value * kHundredth);
        }
        if (void* const property = Virtual<void*>(Virtual<void*>(exemplar, 0x40), 0x24, 0x891B3AE6u)) {
            LoadFromProperty(&second.distance, property);
            any = true;
        }
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0xE91B3AEE, &value)) {
            any = true;
            second.factor14 = static_cast<float>(value * kHundredth);
        }
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0xC92D9C7A, &value)) {
            any = true;
            second.factor30 = static_cast<float>(value * kHundredth);
        }
        if (GetFloat(Virtual<void*>(exemplar, 0x40), 0x092D909B, &value)) {
            any = true;
            second.factor18 = static_cast<float>(value * kHundredth);
        }
        if (void* const property = Virtual<void*>(Virtual<void*>(exemplar, 0x40), 0x24, 0xE92D9DB4u)) {
            LoadFromProperty(&second.age, property);
            Add(health, second);
        } else if (any) {
            Add(health, second);
        }
    }
    if (points.begin)
        Free(points.begin);
    Empty(first.groups);
    if (first.groups)
        Free(first.groups);
    if (first.age.begin)
        Free(first.age.begin);
    if (first.distance.begin)
        Free(first.distance.begin);
    Empty(second.groups);
    if (second.groups)
        Free(second.groups);
    if (second.age.begin)
        Free(second.age.begin);
    if (second.distance.begin)
        Free(second.distance.begin);
    if (heap)
        OperatorDelete(heap);
}

// 0x006FC0C0: the cell walk's parameters (`params`, 0x70 bytes): defaults,
// then the tuning exemplar's values and curves; the first two scaled by
// the products of the school and health records' factor 0x30.
void LoadParams(char* self, char* params)
{
    float school = 1.0f;
    Node* head = At<Node*>(self, local::kSchoolRecords);
    if (head->next != head) {
        double product = 1.0;
        for (Node* node = head->next; node != head; node = node->next)
            product *= Value<Record>(node).factor30;
        school = static_cast<float>(product);
    }
    float health = 1.0f;
    head = At<Node*>(self, local::kHealthRecords);
    if (head->next != head) {
        double product = 1.0;
        for (Node* node = head->next; node != head; node = node->next)
            product *= Value<Record>(node).factor30;
        health = static_cast<float>(product);
    }
    At<float>(params, 0x0) = 1.0f;
    At<float>(params, 0x4) = 1.0f;
    At<uint16_t>(params, 0x8) = 25;
    At<uint16_t>(params, 0xA) = 30;
    At<uint16_t>(params, 0xC) = 35;
    At<uint8_t>(params, 0xE) = 40;
    At<uint32_t>(params, 0x10) = 0x3F333333;
    At<uint32_t>(params, 0x14) = 0x3F333333;
    At<float>(params, 0x18) = 4.0f;
    At<float>(params, 0x1C) = 6.0f;
    At<float>(params, 0x20) = 8.0f;
    At<uint32_t>(params, 0x30) = 0;
    At<uint32_t>(params, 0x34) = 0;
    At<uint32_t>(params, 0x38) = 0;
    At<uint32_t>(params, 0x3C) = 0;
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0x69189C84};
    void* exemplar = params;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        GetFloat(holder, 0xC929C433, &At<float>(params, 0x0));
        GetFloat(holder, 0x0929C439, &At<float>(params, 0x4));
        GetUint16(holder, 0xC929C652, &At<uint16_t>(params, 0x8));
        GetUint16(holder, 0x0929CC30, &At<uint16_t>(params, 0xA));
        GetUint16(holder, 0x0929CC36, &At<uint16_t>(params, 0xC));
        GetUint8(holder, 0xCA027307, &At<uint8_t>(params, 0xE));
        GetPercentage(holder, 0xCA027325, &At<float>(params, 0x10));
        GetPercentage(holder, 0xCA027328, &At<float>(params, 0x14));
        GetFloat(holder, 0xC929CC58, &At<float>(params, 0x18));
        GetFloat(holder, 0x0929CC65, &At<float>(params, 0x1C));
        GetFloat(holder, 0x2929CC6B, &At<float>(params, 0x20));
        GetFloat(holder, 0xC929CC8D, &At<float>(params, 0x24));
        GetFloat(holder, 0x6929CC92, &At<float>(params, 0x28));
        GetFloat(holder, 0x4929CC97, &At<float>(params, 0x2C));
        GetFloat(holder, 0x6AB70DA7, &At<float>(params, 0x30));
        GetFloat(holder, 0x6AB71654, &At<float>(params, 0x34));
        GetFloat(holder, 0x4AB71659, &At<float>(params, 0x38));
        GetFloat(holder, 0x4AB70DAC, &At<float>(params, 0x3C));
        Vector points{};
        Point point{0.0f, 0.0f};
        PointInsert(&points, nullptr, &point, &exemplar, 1, 1);
        point = {32000.0f, 100.0f};
        if (points.end != points.capacity) {
            if (points.end)
                *reinterpret_cast<Point*>(points.end) = {32000.0f, 100.0f};
            points.end += sizeof(Point);
        } else {
            PointInsert(&points, reinterpret_cast<Point*>(points.end), &point, &exemplar, 1, 1);
        }
        SetPoints(params + 0x40, &points);
        SetPoints(params + 0x50, &points);
        SetPoints(params + 0x60, &points);
        LoadFromExemplar(params + 0x40, key, 0x6941B022);
        LoadFromExemplar(params + 0x50, key, 0xA941B028);
        LoadFromExemplar(params + 0x60, key, 0xC941B02E);
        Virtual<uint32_t>(exemplar, 0x8);
        if (points.begin)
            Free(points.begin);
    }
    At<float>(params, 0x0) = static_cast<float>(static_cast<double>(school) * At<float>(params, 0x0));
    At<float>(params, 0x4) = static_cast<float>(static_cast<double>(health) * At<float>(params, 0x4));
    At<uint8_t>(self, local::kParamsLoaded) = 1;
}

// --- Grids -------------------------------------------------------------------

struct Row {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};

Row* Rows(void* data) { return *static_cast<Row**>(data); }

// 0x006F8970 (also called from 0x0068F63F): one byte grid into another
// through `store`: cell by cell at the same shift (its slot 11), averaged
// over the finer source cells, or repeated over the finer target cells.
void __cdecl CopyGrid(void* to, void* from, StoreFn store, uint32_t argument)
{
    static bool logged = false;
    FirstCall("CopyGrid", logged);
    const int32_t toShift = Virtual<int32_t>(to, 0x2C);
    const int32_t toRows = Virtual<int32_t>(to, 0x30);
    const int32_t toColumns = Virtual<int32_t>(to, 0x34);
    void* const toData = Virtual<void*>(to, 0x60);
    const int32_t fromShift = Virtual<int32_t>(from, 0x2C);
    const int32_t fromRows = Virtual<int32_t>(from, 0x30);
    const int32_t fromColumns = Virtual<int32_t>(from, 0x34);
    void* const fromData = Virtual<void*>(from, 0x64);
    if (toShift == fromShift) {
        for (int32_t r = 0; r < toRows; ++r)
            for (int32_t c = 0; c < toColumns; ++c)
                store(Rows(toData)[r].begin + c, Rows(fromData)[r].begin[c], argument);
    } else if (toShift > fromShift) {
        const int32_t shift = toShift - fromShift;
        for (int32_t r = 0; r < toRows; ++r) {
            for (int32_t c = 0; c < toColumns; ++c) {
                int32_t sum = 0;
                for (int32_t sr = r << shift; sr < (r + 1) << shift; ++sr)
                    for (int32_t sc = c << shift; sc < (c + 1) << shift; ++sc)
                        sum += Rows(fromData)[sr].begin[sc];
                store(Rows(toData)[r].begin + c, static_cast<uint32_t>((sum >> shift) >> shift), argument);
            }
        }
    } else {
        const int32_t shift = fromShift - toShift;
        for (int32_t r = 0; r < fromRows; ++r) {
            for (int32_t c = 0; c < fromColumns; ++c) {
                const uint8_t value = Rows(fromData)[r].begin[c];
                for (int32_t tr = r << shift; tr < (r + 1) << shift; ++tr)
                    for (int32_t tc = c << shift; tc < (c + 1) << shift; ++tc)
                        store(Rows(toData)[tr].begin + tc, value, argument);
            }
        }
    }
}

// --- The cell walk -----------------------------------------------------------

// The transform handed to a new effect (its slot 8): flags, a 3 x 3 matrix,
// the position and a scale.
struct Transform {
    uint8_t flags[4];
    float matrix[9];
    float position[3];
    float scale;
};
static_assert(sizeof(Transform) == 0x38);

// 0x006FA450: one cell (x, z) of the walk: HQ target from the pollution
// curves; EQ and HQ above their base reduced; maybe buzzards over a
// populated cell with low HQ; residents by wealth from the three sources,
// EQ and HQ of new residents blended in (at most 200); the age moved on
// (blended with the new residents' ages, reset with EQ and HQ x 0.7 past
// the life expectancy); the life sum and the age buckets.
void UpdateCell(char* self, char* p, int32_t x, int32_t z)
{
    static constexpr char kBuzzards[] = "buzzards";
    GameString name{};
    uint8_t allocator = 0;
    reinterpret_cast<FromRangeFn>(addr::SC4String_FromRange)(&name.begin, kBuzzards, kBuzzards + 8, &allocator);
    name.vtable = addr::SC4String_Vtable;
    name.zero = 0;
    void* const wealth[3] = {Virtual<void*>(At<void*>(self, local::kWealthSources), 0x3C),
                             Virtual<void*>(At<void*>(self, local::kWealthSources + 4), 0x3C),
                             Virtual<void*>(At<void*>(self, local::kWealthSources + 8), 0x3C)};
    const int32_t scale = Virtual<int32_t>(At<void*>(self, field::kPopulationGrids), 0x28);
    const uint32_t x0 = static_cast<uint32_t>(scale * x);
    const uint32_t z0 = static_cast<uint32_t>(scale * z);
    float target = At<float>(p, 0x4);
    for (uint32_t kind = 0; kind < 3; ++kind) {
        const uint8_t average = static_cast<uint8_t>(AveragePollution(kind, x0, z0, static_cast<uint32_t>(scale)));
        const double effect = Evaluate(p + 0x40 + 0x10 * kind, static_cast<float>(static_cast<int32_t>(average)));
        target = static_cast<float>(effect + target);
    }
    bool spawned = false;
    for (std::size_t k = 0; k < 3; ++k) {
        void* const eqGrid = At<void*>(self, field::kEQGrids + 4 * k);
        const double eqLeft = Virtual<double>(eqGrid, 0x58, x, z) - At<float>(p, 0x0);
        float eq = static_cast<float>(eqLeft);
        if (eqLeft < 0.0)
            eq = 0.0f;
        Virtual<void>(eqGrid, 0x68, x, z, eq);
        void* const hqGrid = At<void*>(self, field::kHQGrids + 4 * k);
        const double hqLeft = Virtual<double>(hqGrid, 0x58, x, z) - target;
        float hq = static_cast<float>(hqLeft);
        if (hqLeft < 0.0)
            hq = 0.0f;
        Virtual<void>(hqGrid, 0x68, x, z, hq);
        if (!(hq < At<float>(p, 0x30)))
            continue;
        if (spawned)
            continue;
        if (!Virtual<uint16_t>(At<void*>(self, field::kPopulationGrids + 4 * k), 0x58, x, z))
            continue;
        const double roll = Uniform(self + local::kRandom, 0.0, 100.0);
        if (!(At<float>(p, 0x3C) > roll))
            continue;
        void* effect = nullptr;
        void* const effects = Global<void*>(addr::SC4_Service43D1C);
        const char* const text = reinterpret_cast<DataFn>(addr::cRZBaseString_Data)(&name);
        Virtual<void>(effects, 0x1C, text, &effect);
        if (!effect)
            continue;
        void* const grid = At<void*>(self, field::kEQGrids + 4 * k);
        float start = static_cast<float>(Virtual<double>(grid, 0x38) * x);
        const float px = static_cast<float>(Virtual<double>(grid, 0x38) * 0.5 + start);
        start = static_cast<float>(Virtual<double>(grid, 0x3C) * z);
        const float pz = static_cast<float>(Virtual<double>(grid, 0x3C) * 0.5 + start);
        const double ground = Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, px, pz) + At<float>(p, 0x34);
        const double spread = At<float>(p, 0x38);
        const double jitter = Uniform(self + local::kRandom, -spread, spread);
        Transform transform{};
        transform.flags[0] = 1;
        transform.flags[1] = 1;
        transform.matrix[0] = 1.0f;
        transform.matrix[4] = 1.0f;
        transform.matrix[8] = 1.0f;
        transform.position[0] = px;
        transform.position[1] = static_cast<float>(jitter + ground);
        transform.position[2] = pz;
        transform.scale = 1.0f;
        Virtual<void>(effect, 0x20, &transform);
        Virtual<void>(effect, 0xC, 0u);
        spawned = true;
        Virtual<uint32_t>(effect, 0x8);
    }
    uint16_t now[3];
    for (int k = 0; k < 3; ++k)
        now[k] = Virtual<uint16_t>(wealth[k], 0x58, x, z);
    // The game's stack word +0x20: the last grown cell's old population, or
    // the third source; the month query below may leave it as it is.
    uint32_t month = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(wealth[2]));
    bool grew = false;
    int32_t total = 0;
    uint16_t before[3];
    uint16_t added[3];
    for (std::size_t k = 0; k < 3; ++k) {
        total += now[k];
        void* const popGrid = At<void*>(self, field::kPopulationGrids + 4 * k);
        const uint16_t old = Virtual<uint16_t>(popGrid, 0x58, x, z);
        before[k] = old;
        Virtual<void>(popGrid, 0x68, x, z, static_cast<uint32_t>(now[k]));
        At<uint32_t>(self, local::kPopulationSum) += now[k];
        void* const eqGrid = At<void*>(self, field::kEQGrids + 4 * k);
        void* const hqGrid = At<void*>(self, field::kHQGrids + 4 * k);
        if (old < now[k]) {
            const uint32_t difference = static_cast<uint32_t>(now[k]) - old;
            added[k] = static_cast<uint16_t>(difference);
            const double newcomers = static_cast<float>(static_cast<int16_t>(difference));
            const double oldF = static_cast<float>(static_cast<int32_t>(old));
            const double newF = static_cast<float>(static_cast<int32_t>(now[k]));
            month = old;
            const double eq = Virtual<double>(eqGrid, 0x58, x, z);
            const double eqDivisor = eq > 0.0 ? 2.0 : 1.0;
            const double eqBlend = ((eq + At<float>(p, 0x18 + 4 * k)) / eqDivisor * newcomers + oldF * eq) / newF;
            float eqNew = static_cast<float>(eqBlend);
            if (eqBlend > 200.0)
                eqNew = 200.0f;
            Virtual<void>(eqGrid, 0x68, x, z, eqNew);
            const double hq = Virtual<double>(hqGrid, 0x58, x, z);
            const double hqDivisor = hq > 0.0 ? 2.0 : 1.0;
            const double hqBlend = ((hq + At<float>(p, 0x24 + 4 * k)) / hqDivisor * newcomers + oldF * hq) / newF;
            float hqNew = static_cast<float>(hqBlend);
            if (hqBlend > 200.0)
                hqNew = 200.0f;
            Virtual<void>(hqGrid, 0x68, x, z, hqNew);
            grew = true;
        } else {
            added[k] = 0;
        }
        if (now[k] == 0) {
            Virtual<void>(eqGrid, 0x68, x, z, 0u);
            Virtual<void>(hqGrid, 0x68, x, z, 0u);
        }
    }
    void* const ageGrid = At<void*>(self, local::kAgeGrid);
    uint8_t age = Virtual<uint8_t>(ageGrid, 0x58, x, z);
    if (total == 0) {
        age = 0;
    } else {
        Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x18, 0u, &month, 0u, 0u, 0u);
        if (month == 12)
            ++age;
    }
    int32_t newAge = age;
    if (grew) {
        const uint32_t residents = static_cast<uint32_t>(now[1]) + now[2] + now[0];
        if (residents == 0) {
            newAge = 0;
        } else {
            uint32_t sum = (static_cast<uint32_t>(before[1]) + before[2] + before[0]) * static_cast<uint32_t>(age);
            sum += static_cast<uint32_t>(static_cast<int32_t>(At<int16_t>(p, 0xC)) * static_cast<int16_t>(added[2]));
            sum += static_cast<uint32_t>(static_cast<int32_t>(At<int16_t>(p, 0xA)) * static_cast<int16_t>(added[1]));
            sum += static_cast<uint32_t>(static_cast<int32_t>(At<int16_t>(p, 0x8)) * static_cast<int16_t>(added[0]));
            newAge = static_cast<int32_t>(sum) / static_cast<int32_t>(residents);
        }
    }
    void* const firstGrid = At<void*>(self, field::kPopulationGrids);
    const int32_t zScaled = Virtual<int32_t>(firstGrid, 0x28) * z;
    const int32_t xScaled = Virtual<int32_t>(firstGrid, 0x28) * x;
    const int32_t life = static_cast<int32_t>(Virtual<uint32_t>(self, 0x74, xScaled, zScaled) & 0xFF);
    if (newAge > life) {
        newAge = At<uint8_t>(p, 0xE);
        for (std::size_t k = 0; k < 3; ++k) {
            void* const eqGrid = At<void*>(self, field::kEQGrids + 4 * k);
            const double eqFactor = At<float>(p, 0x10);
            Virtual<void>(eqGrid, 0x68, x, z, static_cast<float>(Virtual<double>(eqGrid, 0x58, x, z) * eqFactor));
            void* const hqGrid = At<void*>(self, field::kHQGrids + 4 * k);
            const double hqFactor = At<float>(p, 0x14);
            Virtual<void>(hqGrid, 0x68, x, z, static_cast<float>(Virtual<double>(hqGrid, 0x58, x, z) * hqFactor));
        }
    }
    Virtual<void>(ageGrid, 0x68, x, z, static_cast<uint32_t>(newAge));
    At<int64_t>(self, local::kLifeSum) +=
        static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(life) * static_cast<uint32_t>(total)));
    int32_t bucket = static_cast<int32_t>(static_cast<uint32_t>(newAge) - 20u) / 3;
    if (static_cast<uint32_t>(bucket) >= 20)
        bucket = 19;
    for (std::size_t k = 0; k < 3; ++k) {
        const uint32_t residents = Virtual<uint16_t>(At<void*>(self, field::kPopulationGrids + 4 * k), 0x58, x, z);
        const int32_t eq = Ftol(Virtual<double>(At<void*>(self, field::kEQGrids + 4 * k), 0x58, x, z));
        At<uint32_t>(self, local::kAgeCounts + 4 * bucket) += residents;
        At<uint32_t>(self, local::kAgeQuality + 4 * bucket) += static_cast<uint32_t>(eq) * residents;
    }
    name.vtable = addr::SC4String_Vtable;
    if (name.begin)
        Free(name.begin);
}

// 0x006F8070: the month's figures: the population (+0xA4 from +0xA8), the
// average life expectancy, and for each of the nine curves the people
// counted by age bucket (curve x residents x 0.01) and their average EQ.
void SendMonthlyFigures(char* self)
{
    const int32_t population = At<int32_t>(self, local::kPopulationSum);
    At<int32_t>(self, field::kPopulation) = population;
    int32_t life = 0;
    if (population != 0)
        life = static_cast<int32_t>(At<int64_t>(self, local::kLifeSum) / static_cast<int64_t>(population));
    SendFigure(self, 0x2A5E7009, static_cast<uint32_t>(life));
    float quality[20];
    for (std::size_t i = 0; i < 20; ++i) {
        const uint32_t count = At<uint32_t>(self, local::kAgeCounts + 4 * i);
        if (count != 0)
            quality[i] = static_cast<float>(Unsigned(At<uint32_t>(self, local::kAgeQuality + 4 * i)) / Unsigned(count));
        else
            quality[i] = 0.0f;
    }
    for (uint32_t m = 0; m < 9; ++m) {
        const uint32_t kind = 0xCA545CCA + m;
        const char* const curve = self + local::kMonthlyCurves + 0x10 * m;
        int32_t people = 0;
        int32_t weighted = 0;
        for (uint32_t age = 21, i = 0; age < 81; age += 3, ++i) {
            const uint32_t count = At<uint32_t>(self, local::kAgeCounts + 4 * i);
            if (count == 0)
                continue;
            const double share = Evaluate(curve, static_cast<float>(static_cast<int32_t>(age)));
            const int32_t counted = Ftol(share * Unsigned(count) * kHundredth);
            people += counted;
            weighted = Ftol(static_cast<double>(counted) * quality[i] + static_cast<double>(weighted));
        }
        SendFigure(self, kind - 0x1F, static_cast<uint32_t>(people));
        const int32_t average = people > 0 ? weighted / people : 0;
        SendFigure(self, kind, static_cast<uint32_t>(average));
    }
}

// --- Coverage ----------------------------------------------------------------

// 0x006F9BF0: one building's coverage: its radius (`radiusProperty`) x
// `scale` over the cells around it; each cell's coverage (byte grid of its
// type) raised by capacity / divisor (at most 255); people in cells newly
// covered and by no lower type counted into `served`.
void CoverCells(char* self, void* grids, uint32_t type, void* occupant, uint32_t radiusProperty, float scale,
                uint32_t capacityProperty, uint32_t divisor, uint32_t* served)
{
    void* const grid = At<void**>(grids, 0)[type];
    void* const holder = Properties(occupant);
    float radius = 0.0f;
    if (!GetFloat(holder, radiusProperty, &radius))
        return;
    uint32_t capacity = 0;
    if (!GetUint32(holder, capacityProperty, &capacity))
        return;
    float position[3] = {};
    Virtual<void>(occupant, 0x20, position);
    Virtual<double>(grid, 0x40);
    const double reach = static_cast<double>(radius) * scale;
    const float reachF = static_cast<float>(reach);
    if (!(reach > 0.0))
        return;
    void* cells = nullptr;
    cells = NewHead(0x10);
    reinterpret_cast<ListFn>(addr::SC4StlList_Clear)(&cells);
    const uint32_t columns = Virtual<uint32_t>(grid, 0x34);
    const uint32_t rows = Virtual<uint32_t>(grid, 0x30);
    const float cellSize = static_cast<float>(Virtual<double>(grid, 0x38));
    CellsAround(self, occupant, reachF, cellSize, rows, columns, &cells);
    Node* const head = static_cast<Node*>(cells);
    for (Node* node = head->next; node != head; node = node->next) {
        const int32_t cx = At<int32_t>(node, 8);
        const int32_t cz = At<int32_t>(node, 0xC);
        const uint8_t had = Virtual<uint8_t>(grid, 0x58, cx, cz);
        uint32_t value = capacity / divisor + had;
        if (static_cast<uint16_t>(value) > 0xFF)
            value = 0xFF;
        Virtual<void>(grid, 0x68, cx, cz, value);
        if (had)
            continue;
        bool lower = false;
        for (int32_t j = 0; j < static_cast<int32_t>(type); ++j) {
            if (Virtual<uint8_t>(At<void**>(grids, 0)[j], 0x58, cx, cz)) {
                lower = true;
                break;
            }
        }
        if (lower)
            continue;
        uint32_t people = 0;
        for (std::size_t k = 0; k < 3; ++k)
            people += Virtual<uint16_t>(At<void*>(self, field::kPopulationGrids + 4 * k), 0x58, cx, cz);
        *served += people;
    }
    reinterpret_cast<ListFn>(addr::SC4PairList_Destroy)(&cells);
}

// 0x006FD0F0: the coverage of one list of schools (or hospitals): its grid
// cleared, then each building's cells covered with its funding factor x
// the scales of the records that apply to it.
void UpdateCoverage(char* self, void* grids, uint32_t type, void* buildings, uint8_t school)
{
    Virtual<void>(At<void**>(grids, 0)[type], 0x6C, 0u);
    Node* const head = Head(buildings);
    const uint32_t divisor =
        At<uint32_t*>(self, school ? local::kSchoolDivisors : local::kHealthDivisors)[type];
    const uint32_t groupProperty = school ? 0xA92D8E40u : 0x292D8F9Bu;
    const uint32_t radiusProperty = school ? 0x691B42B3u : 0x091B42A2u;
    const uint32_t capacityProperty = school ? 0x8922041Bu : 0x69220415u;
    char* const records = self + (school ? local::kSchoolRecords : local::kHealthRecords);
    void* applying = NewHead(0x3C);
    uint32_t* const served = &At<uint32_t>(self, school ? local::kSchoolServed : local::kHealthServed);
    float first = 0.0f, second = 0.0f;
    for (Node* node = head->next; node != head; node = node->next) {
        void* const building = Value<void*>(node);
        FundingFactors(self, building, &first, &second, school, 0);
        ClearRecords(&applying);
        FilterRecords(building, groupProperty, records, &applying);
        double product = 1.0;
        Node* const own = static_cast<Node*>(applying);
        for (Node* item = own->next; item != own; item = item->next)
            product *= Value<Record>(item).scale;
        const float scale = static_cast<float>(static_cast<double>(second) * product);
        CoverCells(self, grids, type, building, radiusProperty, scale, capacityProperty, divisor, served);
    }
    ClearRecords(&applying);
    Free(applying);
}

// 0x006F8F60: the people a building serves, rounded: over its cells
// covered, distance and age weights (the curves and the records' own),
// times capacity / divisor shared among the coverage, by wealth weight.
uint32_t CountServed(char* self, void* building, float radius, uint32_t capacity, void* ageCurve, void* wealthCurve,
                     void* distanceCurve, void* records, void* cover, uint32_t divisor, void* cells)
{
    const uint32_t share = capacity / divisor;
    float sum = 0.0f;
    if (radius <= 0.0f)
        return 0;
    Node* const head = Head(cells);
    Node* cursor = head->next;
    float position[3] = {};
    Virtual<void>(building, 0x20, position);
    int32_t cx = 0, cz = 0;
    Virtual<void>(At<void*>(self, field::kPopulationGrids), 0x4C, position[0], position[2], &cx, &cz);
    while (cursor != head) {
        Node* const node = cursor;
        const int32_t x = At<int32_t>(node, 8);
        const int32_t z = At<int32_t>(node, 0xC);
        if (!Virtual<uint8_t>(cover, 0x58, x, z)) {
            cursor = node->next;
            continue;
        }
        const double dx = Unsigned(static_cast<uint32_t>(cx - x));
        const int32_t dz = cz - z;
        const double dz2 = Unsigned(static_cast<uint32_t>(dz) * static_cast<uint32_t>(dz));
        const double length = std::sqrt(dx * dx + dz2);
        const float distance = static_cast<float>(length / radius * 100.0);
        float distanceWeight = static_cast<float>(Evaluate(distanceCurve, distance) * kHundredth);
        const uint8_t age = Virtual<uint8_t>(At<void*>(self, local::kAgeGrid), 0x58, x, z);
        const float ageF = static_cast<float>(static_cast<int32_t>(age));
        float ageWeight = static_cast<float>(Evaluate(ageCurve, ageF) * kHundredth);
        Node* const own = Head(records);
        for (Node* item = own->next; item != own; item = item->next) {
            char* const record = reinterpret_cast<char*>(item) + 8;
            distanceWeight = static_cast<float>(Evaluate(record + 0x4, distance) * distanceWeight * kHundredth);
            ageWeight = static_cast<float>(Evaluate(record + 0x1C, ageF) * ageWeight * kHundredth);
        }
        const float shareF = static_cast<float>(Unsigned(share));
        for (int32_t k = 0; k < 3; ++k) {
            const uint16_t people = Virtual<uint16_t>(At<void*>(self, field::kPopulationGrids + 4 * k), 0x58, x, z);
            const float peopleF = static_cast<float>(static_cast<int32_t>(people));
            const uint8_t covering = Virtual<uint8_t>(cover, 0x58, x, z);
            const float portion = static_cast<float>(static_cast<double>(shareF) /
                                                     static_cast<int32_t>(covering) * peopleF);
            const double weight = Evaluate(wealthCurve, static_cast<float>(k)) * kHundredth;
            sum = static_cast<float>(weight * portion * ageWeight * distanceWeight + sum);
        }
        cursor = cursor->next;
    }
    return static_cast<uint32_t>(RoundFloat(sum));
}

// 0x006FD970: one school (or hospital) applied: its radius x funding x the
// records' factors; the people it serves; the share of capacity used
// (two ratios, at most 1) spread over its cells into the share grid; EQ
// (or HQ) spread by wealth (SC4::SpreadQuality); the figures written back
// to the building's properties.
void ApplyBuilding(char* self, void* building, void* cover, uint32_t divisor, void* shareGrid, void** quality,
                   float cap, uint8_t school, uint32_t* reached)
{
    float firstFactor = 0.0f, secondFactor = 0.0f;
    void* records = NewHead(0x3C);
    void* const recordsHead = records;
    Curve ageCurve{}, wealthCurve{}, distanceCurve{};
    CurveInit(&ageCurve);
    CurveInit(&wealthCurve);
    CurveInit(&distanceCurve);
    const uint32_t groupProperty = school ? 0xA92D8E40u : 0x292D8F9Bu;
    const uint32_t radiusProperty = school ? 0x691B42B3u : 0x091B42A2u;
    const uint32_t capacityProperty = school ? 0x8922041Bu : 0x69220415u;
    const uint32_t weightProperty = school ? 0xA92AE446u : 0xC9299659u;
    const uint32_t servedProperty = school ? 0x09220161u : 0xC922015Cu;
    const uint32_t usedProperty = school ? 0xE9244B2Eu : 0x49244B21u;
    const uint32_t shareProperty = school ? 0x292453C5u : 0x092453CBu;
    const uint32_t ratioProperty = school ? 0x2A976C20u : 0xAA976BE7u;
    char* const allRecords = self + (school ? local::kSchoolRecords : local::kHealthRecords);
    void* const holder = Properties(building);
    float radius = 0.0f;
    uint32_t served = 0;
    uint32_t used = 0;
    uint32_t ratio = 0;
    if (GetFloat(Properties(building), radiusProperty, &radius)) {
        FundingFactors(self, building, &firstFactor, &secondFactor, school, 0);
        uint32_t funding = 0;
        const uint32_t fundingBits = *FundingFactor(self, &funding, building, school);
        LoadCurves(holder, &ageCurve, &wealthCurve, &distanceCurve, school);
        ClearRecords(&records);
        FilterRecords(building, groupProperty, allRecords, &records);
        float scale = 0.0f, factor18 = 0.0f, factor14 = 0.0f;
        RecordFactors(&records, &scale, &factor18, &factor14);
        radius = static_cast<float>(static_cast<double>(secondFactor) * scale * radius);
        void* cells = NewHead(0x10);
        void* const population = At<void*>(self, field::kPopulationGrids);
        const uint32_t columns = Virtual<uint32_t>(population, 0x34);
        const uint32_t rows = Virtual<uint32_t>(population, 0x30);
        const float cellSize = static_cast<float>(Virtual<double>(population, 0x38));
        CellsAround(self, building, radius, cellSize, rows, columns, &cells);
        uint32_t capacity = radiusProperty;
        GetUint32(holder, capacityProperty, &capacity);
        served = CountServed(self, building, radius, capacity, &ageCurve, &wealthCurve, &distanceCurve, &records,
                             cover, divisor, &cells);
        const int32_t wanted = Ftol(Unsigned(capacity) * AsFloat(fundingBits) * factor18);
        uint32_t first;
        if (wanted == 0)
            first = DefaultPercentageBits();
        else if (served == 0)
            first = FullFundingBits();
        else
            first = Bits(static_cast<float>(Unsigned(static_cast<uint32_t>(wanted)) / Unsigned(served)));
        if (AsFloat(first) > FullFunding())
            first = FullFundingBits();
        SetFloat(holder, ratioProperty, static_cast<float>(AsFloat(first)));
        const int32_t usable = Ftol(Unsigned(capacity) * firstFactor * factor18);
        used = static_cast<uint32_t>(usable);
        if (usable == 0)
            ratio = DefaultPercentageBits();
        else if (served == 0)
            ratio = FullFundingBits();
        else
            ratio = Bits(static_cast<float>(Unsigned(static_cast<uint32_t>(usable)) / Unsigned(served)));
        if (AsFloat(ratio) > FullFunding())
            ratio = FullFundingBits();
        const uint32_t share = capacity / divisor;
        Node* const cellsHead = static_cast<Node*>(cells);
        if (cellsHead->next != cellsHead) {
            const float shareF = static_cast<float>(Unsigned(share) * AsFloat(ratio));
            for (Node* node = cellsHead->next; node != cellsHead; node = node->next) {
                const int32_t x = At<int32_t>(node, 8);
                const int32_t z = At<int32_t>(node, 0xC);
                const uint32_t covering = Virtual<uint8_t>(cover, 0x58, x, z);
                double kept = 0.0;
                if (covering > 1)
                    kept = Virtual<double>(shareGrid, 0x58, x, z) * Unsigned(covering - 1);
                const double value = kept + shareF / Unsigned(covering);
                float stored = static_cast<float>(value);
                if (value > 1.0)
                    stored = 1.0f;
                Virtual<void>(shareGrid, 0x68, x, z, stored);
            }
        }
        float weight = 1.0f;
        GetFloat(holder, weightProperty, &weight);
        float position[3] = {};
        if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, building))
            Virtual<void>(lot, 0x3C, position);
        else
            Virtual<void>(building, 0x20, position);
        int32_t cellX = 0, cellZ = 0;
        Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
        if (radius > 0.0f) {
            void* distances = nullptr;
            distances = NewHead(0x18);
            void* ages = nullptr;
            ages = NewHead(0x18);
            SplitCurves(&records, &distances, &ages);
            Node* where = static_cast<Node*>(ages);
            auto* copy = static_cast<Node*>(StlAllocate(0x18));
            CurveCopy(reinterpret_cast<char*>(copy) + 8, &ageCurve);
            LinkBefore(copy, where);
            where = static_cast<Node*>(distances);
            copy = static_cast<Node*>(StlAllocate(0x18));
            CurveCopy(reinterpret_cast<char*>(copy) + 8, &distanceCurve);
            LinkBefore(copy, where);
            for (int32_t k = 0; k < 3; ++k) {
                const double wealth = Evaluate(&wealthCurve, static_cast<float>(k)) * kHundredth;
                const float factor = static_cast<float>(AsFloat(ratio) * wealth * weight * factor14);
                const float reach = static_cast<float>(static_cast<double>(radius) * At<float>(self, local::kRadiusScale));
                reinterpret_cast<SpreadQualityFn>(addr::SC4_SpreadQuality)(
                    At<void*>(self, local::kAgeGrid), quality[k], &ages, position[0], position[2], reach, &distances,
                    factor, cap, share, cover, At<void*>(self, field::kPopulationGrids + 4 * k), reached);
            }
            ClearCurveList(&ages, nullptr);
            if (ages)
                Free(ages);
            ClearCurveList(&distances, nullptr);
            if (distances)
                Free(distances);
        }
        Node* node = cellsHead->next;
        while (node != static_cast<Node*>(cells)) {
            Node* const dead = node;
            node = node->next;
            Free(dead);
        }
        static_cast<Node*>(cells)->next = static_cast<Node*>(cells);
        static_cast<Node*>(cells)->prev = static_cast<Node*>(cells);
        Free(cells);
    } else {
        ratio = DefaultPercentageBits();
        served = 0;
        used = 0;
    }
    SetUint32(holder, servedProperty, served);
    SetUint32(holder, usedProperty, used);
    SetFloat(holder, shareProperty, static_cast<float>(AsFloat(ratio)));
    if (distanceCurve.begin)
        Free(distanceCurve.begin);
    if (wealthCurve.begin)
        Free(wealthCurve.begin);
    if (ageCurve.begin)
        Free(ageCurve.begin);
    ClearRecords(&records);
    Free(recordsHead);
}

// --- Buildings added and removed ---------------------------------------------

Node* Find(Node* head, void* value)
{
    Node* node = head->next;
    while (node != head && Value<void*>(node) != value)
        node = node->next;
    return node;
}

// One budget item of a building: its line set to the building type when it
// has none (budget slot 52), the department's line item counted up or down
// (created when missing, removed at zero), the building added to or
// removed from it. False when the department is missing.
bool CountBudgetItem(char* self, void* building, void* holder, uint32_t purpose, uint8_t added, BudgetItem& item,
                     void*& department, void*& line)
{
    void* const budget = At<void*>(self, field::kBudget);
    if (item.line == 0) {
        const uint32_t type = Virtual<uint32_t>(building, 0x10);
        item.line = type;
        Virtual<uint8_t>(budget, 0xD0, holder, item.word18, type);
    }
    department = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, item.department);
    if (!department)
        return false;
    if (added) {
        if (!Virtual<void*>(department, 0x50, item.line)) {
            void* const created = Virtual<void*>(department, 0x48, item.line, 1u);
            Virtual<void>(created, 0x48, 2u, 1u);
        }
        Virtual<uint8_t>(department, 0x58, holder, purpose);
    } else {
        Virtual<uint8_t>(department, 0x5C, holder, purpose);
    }
    line = Virtual<void*>(department, 0x50, item.line);
    const int64_t count = Virtual<int64_t>(line, 0x20) + (added ? 1 : -1);
    Virtual<void>(line, 0x24, count);
    if (!added && Virtual<int64_t>(line, 0x20) == 0)
        Virtual<uint8_t>(department, 0x4C, item.line);
    return true;
}

// The second budget item (the building's other purpose): added to or
// removed from its department. False when the department is missing.
bool OtherBudgetItem(char* self, void* holder, uint32_t purpose, uint8_t added, const BudgetItem& item)
{
    void* const department = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, item.department);
    if (!department)
        return false;
    if (added)
        Virtual<uint8_t>(department, 0x58, holder, purpose);
    else
        Virtual<uint8_t>(department, 0x5C, holder, purpose);
    return true;
}

// A new school or hospital: its served/used properties reset (-1, 1, 1),
// the used figure copied from its capacity, referenced and listed.
void AddService(char* self, void* building, void* holder, const uint32_t (&ids)[5], uint32_t& scratch,
                std::size_t list)
{
    Virtual<uint8_t>(holder, 0x28, ids[0], 0u, 1u, 0u);
    SetUint32(holder, ids[0], 0xFFFFFFFF);
    Virtual<uint8_t>(holder, 0x28, ids[1], 0u, 1u, 0u);
    SetFloat(holder, ids[1], 1.0f);
    Virtual<uint8_t>(holder, 0x28, ids[2], 0u, 1u, 0u);
    SetFloat(holder, ids[2], 1.0f);
    GetUint32(holder, ids[3], &scratch);
    Virtual<uint8_t>(holder, 0x28, ids[4], 0u, 1u, 0u);
    SetUint32(holder, ids[4], scratch);
    Virtual<uint32_t>(building, 0x4);
    void* const occupant = Virtual<void*>(building, 0xC);
    Node* const where = Head(self + list);
    Node* const node = AllocateNodes(self + list, 1, 0);
    Value<void*>(node) = occupant;
    LinkBefore(node, where);
    At<uint8_t>(self, local::kDirty) = 1;
}

// A school or hospital removed: out of the first of its type lists that
// has it, else out of the new list; released.
void RemoveService(char* self, void* building, uint32_t& scratch, std::size_t lists, std::size_t newList)
{
    for (uint32_t i = 0; i < VectorSize(self, lists); ++i) {
        void* const occupant = Virtual<void*>(building, 0xC);
        void* const list = At<char*>(self, lists) + 4 * i;
        Node* const node = Find(Head(list), occupant);
        if (node != Head(list)) {
            ListErase(list, reinterpret_cast<Node**>(&scratch), node);
            Virtual<uint32_t>(Virtual<void*>(building, 0xC), 0x8);
            At<uint8_t>(self, local::kDirty) = 1;
            return;
        }
    }
    void* const occupant = Virtual<void*>(building, 0xC);
    Node* const node = Find(Head(self + newList), occupant);
    if (node == Head(self + newList))
        return;
    ListErase(self + newList, reinterpret_cast<Node**>(&scratch), node);
    Virtual<uint32_t>(Virtual<void*>(building, 0xC), 0x8);
    At<uint8_t>(self, local::kDirty) = 1;
}

// 0x006FB120: a building added or removed: its health budget items
// (0xCA565486, 0xEA56549E) and, for hospitals, the hospital lists; its
// school items (0xEA5654B6, 0x4A5654BA) and the school lists; buildings
// with both 0x6A19F6B5 and 0xAA19F6EA above 0 in their own lists; dirty
// for buildings of kind 1.
void BuildingChanged(char* self, void* building, uint8_t added)
{
    // The game's stack word +0x18: the health department, a property read
    // or an erase result, written back by the add paths below.
    uint32_t scratch = 0;
    void* const holder = Properties(Virtual<void*>(building, 0xC));
    BudgetItem healthItem{};
    bool hospital;
    if (Virtual<uint8_t>(At<void*>(self, field::kBudget), 0xCC, holder, 0xCA565486u, &healthItem)) {
        void* department = nullptr;
        void* line = nullptr;
        const bool ok = CountBudgetItem(self, building, holder, 0xCA565486u, added, healthItem, department, line);
        scratch = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(department));
        if (!ok)
            return;
        BudgetItem other{};
        if (Virtual<uint8_t>(At<void*>(self, field::kBudget), 0xCC, holder, 0xEA56549Eu, &other) &&
            !OtherBudgetItem(self, holder, 0xEA56549Eu, added, other))
            return;
        hospital = true;
    } else {
        hospital = Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, 0x1507u) ||
                   Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, 0x151Cu);
    }
    if (hospital) {
        static constexpr uint32_t kIds[5] = {0xC922015C, 0x092453CB, 0xAA976BE7, 0x69220415, 0x49244B21};
        if (added)
            AddService(self, building, holder, kIds, scratch, local::kNewHospitals);
        else
            RemoveService(self, building, scratch, field::kHospitalBuildings, local::kNewHospitals);
    }
    BudgetItem schoolItem{};
    bool schoolBuilding;
    if (Virtual<uint8_t>(At<void*>(self, field::kBudget), 0xCC, holder, 0xEA5654B6u, &schoolItem)) {
        void* department = nullptr;
        void* line = nullptr;
        const bool ok = CountBudgetItem(self, building, holder, 0xEA5654B6u, added, schoolItem, department, line);
        if (!ok)
            return;
        scratch = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(line));
        BudgetItem other{};
        if (Virtual<uint8_t>(At<void*>(self, field::kBudget), 0xCC, holder, 0x4A5654BAu, &other) &&
            !OtherBudgetItem(self, holder, 0x4A5654BAu, added, other))
            return;
        schoolBuilding = true;
    } else {
        schoolBuilding = Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, 0x1503u) ||
                         Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, 0x151Bu);
    }
    if (schoolBuilding) {
        static constexpr uint32_t kIds[5] = {0x09220161, 0x292453C5, 0x2A976C20, 0x8922041B, 0xE9244B2E};
        if (added)
            AddService(self, building, holder, kIds, scratch, local::kNewSchools);
        else
            RemoveService(self, building, scratch, field::kSchoolBuildings, local::kNewSchools);
    }
    float first = 0.0f, second = 0.0f;
    if (GetFloat(holder, 0x6A19F6B5, &first) && GetFloat(holder, 0xAA19F6EA, &second) && first > 0.0f &&
        second > 0.0f) {
        if (added) {
            Virtual<uint32_t>(Virtual<void*>(building, 0xC), 0x4);
            void* const occupant = Virtual<void*>(building, 0xC);
            Node* const where = Head(self + local::kNewOthers);
            Node* const node = AllocateNodes(self + local::kNewOthers, 1, 0);
            Value<void*>(node) = occupant;
            LinkBefore(node, where);
        } else {
            void* occupant = Virtual<void*>(building, 0xC);
            Node* node = Find(Head(self + local::kOthers), occupant);
            if (node != Head(self + local::kOthers)) {
                ListErase(self + local::kOthers, reinterpret_cast<Node**>(&scratch), node);
                Virtual<uint32_t>(Virtual<void*>(building, 0xC), 0x8);
            } else {
                occupant = Virtual<void*>(building, 0xC);
                node = Find(Head(self + local::kNewOthers), occupant);
                if (node != Head(self + local::kNewOthers)) {
                    ListErase(self + local::kNewOthers, reinterpret_cast<Node**>(&scratch), node);
                    Virtual<uint32_t>(Virtual<void*>(building, 0xC), 0x8);
                }
            }
        }
        At<uint8_t>(self, local::kDirty) = 1;
    }
    if (*Virtual<uint32_t*>(building, 0x2C) == 1)
        At<uint8_t>(self, local::kDirty) = 1;
}

// --- The step ----------------------------------------------------------------

// The new buildings of `from` moved into the first type list (`lists`,
// types from `table`) whose group they belong to; `from` emptied.
void SortNew(char* self, std::size_t from, std::size_t lists, std::uintptr_t table)
{
    Node* const head = At<Node*>(self, from);
    for (Node* node = head->next; node != head; node = node->next) {
        bool placed = false;
        for (uint32_t j = 0; j < VectorSize(self, lists); ++j) {
            if (placed)
                continue;
            void* const occupant = Value<void*>(node);
            if (!Virtual<uint8_t>(occupant, 0x58, Global<uint32_t>(table + 4 * j)))
                continue;
            void* const list = At<char*>(self, lists) + 4 * j;
            Node* const where = Head(list);
            Node* const added = AllocateNodes(list, 1, 0);
            Value<void*>(added) = occupant;
            LinkBefore(added, where);
            placed = true;
        }
    }
    ListClear2(self + from);
}

// States 13-15: the temporary grid (made and referenced when missing) sized
// as the proximity map and marked with the development simulator's
// buildings of wealth 1-3.
void MarkTempGrid(char* self, std::size_t map, uint32_t wealth, bool full)
{
    if (!At<void*>(self, local::kTempGrid)) {
        void* grid = OperatorNew(0x44);
        grid = grid ? reinterpret_cast<GridCtorFn>(addr::SC4SimGridBool8_SC4SimGridBool8)(grid, 0x49B9E600u) : nullptr;
        At<void*>(self, local::kTempGrid) = grid;
        Virtual<void>(grid, 0xC);
        Virtual<uint32_t>(At<void*>(self, local::kTempGrid), 0x4);
    }
    const uint32_t size = Virtual<uint32_t>(At<void*>(self, map), 0x28);
    Virtual<void>(At<void*>(self, local::kTempGrid), 0x24, size);
    void* const development = At<void*>(self, field::kDevelopment);
    const uint32_t key = Virtual<uint32_t>(development, 0xA8, 1u, wealth);
    void* const buildings = Virtual<void*>(development, 0xA4);
    const auto mark = full ? reinterpret_cast<MarkFn>(addr::SC4SimGridBool8_FromBuildings)
                           : reinterpret_cast<MarkFn>(addr::SC4SimGridBool8_MarkBuildings);
    mark(At<void*>(self, local::kTempGrid), buildings, key);
}

// States 1-3: spread passes over the temporary grid within the budget; when
// done, copied into the proximity map. False when the time ran out.
bool SpreadTempGrid(char* self, uint32_t budget, uint32_t bit, std::uintptr_t stepGlobal, std::size_t map,
                    uint32_t next, bool last)
{
    Deadline deadline{};
    StartDeadline(deadline, budget, bit, stepGlobal);
    for (;;) {
        void* const grid = At<void*>(self, local::kTempGrid);
        const uint32_t columns = Virtual<uint32_t>(grid, 0x34);
        const uint32_t rows = Virtual<uint32_t>(grid, 0x30);
        void* const data = Virtual<void*>(grid, 0x60);
        if (!reinterpret_cast<SpreadPassFn>(addr::SC4SimGrid8_SpreadPass)(data, rows, columns))
            break;
        if (!InBudget(deadline)) {
            EndDeadline(deadline);
            return false;
        }
    }
    CopyGrid(At<void*>(self, map), At<void*>(self, local::kTempGrid), &StoreByte, 0);
    if (last) {
        Virtual<void>(At<void*>(self, local::kTempGrid), 0x10);
        Virtual<uint32_t>(At<void*>(self, local::kTempGrid), 0x8);
        At<void*>(self, local::kTempGrid) = nullptr;
    }
    At<uint32_t>(self, local::kState) = next;
    EndDeadline(deadline);
    return true;
}

// States 6 and 8: the share grid cleared; each building of each list given
// its ideal funding when auto-budget (global, or its own 0x6C05FC4D) says
// so (slots 20, 58, 19), then applied.
void ApplyBuildings(char* self, std::size_t share, std::size_t lists, std::size_t covers, std::size_t divisors,
                    std::size_t quality, std::size_t autoBudget, uint8_t school, std::size_t reached)
{
    Virtual<void>(At<void*>(self, share), 0x6C, 0u);
    for (uint32_t i = 0; i < VectorSize(self, lists); ++i) {
        Node* const head = At<Node**>(self, lists)[i];
        for (Node* node = head->next; node != head; node = node->next) {
            void* const building = Value<void*>(node);
            uint8_t automatic = At<uint8_t>(self, autoBudget);
            if (!automatic)
                GetBool(Properties(building), 0x6C05FC4D, &automatic);
            if (automatic) {
                uint32_t first = 0, second = 0;
                Virtual<uint8_t>(self, 0x50, building, &first, &second, static_cast<uint32_t>(school));
                if (Virtual<uint8_t>(self, 0xE8, building, &first))
                    Virtual<uint8_t>(self, 0x4C, building, &first, &second, static_cast<uint32_t>(school));
            }
            ApplyBuilding(self, building, Grid(self, covers, i), At<uint32_t*>(self, divisors)[i], At<void*>(self, share),
                          reinterpret_cast<void**>(self + quality), 200.0f, school, &At<uint32_t>(self, reached));
        }
    }
}

// 0x006FF520: one step of the cycle (the state at +0x94); `budget` is the
// time budget of the timed states. False when a timed state ran out.
uint8_t Step(char* self, uint32_t budget)
{
    uint8_t result = 1;
    auto* const scratch = static_cast<uint8_t*>(StlAllocate(8));
    scratch[0] = 0;
    switch (At<uint32_t>(self, local::kState)) {
    case 0: {
        At<uint32_t>(self, local::kSchoolReached) = 0;
        At<uint32_t>(self, local::kHealthReached) = 0;
        UpdateStrikes(self, self + field::kSchool, self + field::kSchoolBuildings, 0x2A976C20, 0x292453C5, 0x1A03);
        UpdateStrikes(self, self + field::kHealth, self + field::kHospitalBuildings, 0xAA976BE7, 0x092453CB, 0x1A02);
        ClearRecords(self + local::kSchoolRecords);
        ClearRecords(self + local::kHealthRecords);
        LoadRecords(self + local::kHealthRecords, self + local::kSchoolRecords);
        SortNew(self, local::kNewSchools, field::kSchoolBuildings, addr::cSC4ResidentialSimulator_SchoolTypes);
        SortNew(self, local::kNewHospitals, field::kHospitalBuildings, addr::cSC4ResidentialSimulator_HospitalTypes);
        Node* const head = At<Node*>(self, local::kNewOthers);
        for (Node* node = head->next; node != head; node = node->next) {
            Node* const where = At<Node*>(self, local::kOthers);
            auto* const moved = static_cast<Node*>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
            Value<void*>(moved) = Value<void*>(node);
            LinkBefore(moved, where);
        }
        ListClear2(self + local::kNewOthers);
        At<uint32_t>(self, local::kState) = 0xD;
        break;
    }
    case 1:
        if (!SpreadTempGrid(self, budget, 1, addr::cSC4ResidentialSimulator_Pass1Step, field::kProximity, 0xE, false))
            result = 0;
        break;
    case 2:
        if (!SpreadTempGrid(self, budget, 2, addr::cSC4ResidentialSimulator_Pass2Step, field::kProximity + 4, 0xF,
                            false))
            result = 0;
        break;
    case 3:
        if (!SpreadTempGrid(self, budget, 4, addr::cSC4ResidentialSimulator_Pass3Step, field::kProximity + 8, 0xB,
                            true))
            result = 0;
        break;
    case 4: {
        Deadline deadline{};
        StartDeadline(deadline, budget, 8, addr::cSC4ResidentialSimulator_Pass4Step);
        if (!At<uint8_t>(self, local::kParamsLoaded))
            LoadParams(self, self + local::kParams);
        char* const iterator = self + local::kIterator;
        int32_t x = 0;
        int32_t z = static_cast<int32_t>(budget);
        bool done = false;
        for (;;) {
            reinterpret_cast<CellNextFn>(addr::SC4CellIterator_Next)(iterator, &x, &z);
            if (*reinterpret_cast<uint8_t*>(iterator) == 2) {
                done = true;
                break;
            }
            UpdateCell(self, self + local::kParams, x, z);
            if (*reinterpret_cast<uint8_t*>(iterator) == 2) {
                done = true;
                break;
            }
            if (!InBudget(deadline))
                break;
        }
        if (!done && *reinterpret_cast<uint8_t*>(iterator) != 2) {
            result = 0;
            EndDeadline(deadline);
            break;
        }
        result = 1;
        At<uint32_t>(self, local::kState) = 0xC;
        EndDeadline(deadline);
        break;
    }
    case 5:
        At<uint32_t>(self, local::kSchoolServed) = 0;
        for (uint32_t i = 0; i < VectorSize(self, field::kSchoolBuildings); ++i)
            UpdateCoverage(self, self + local::kSchoolCover, i, At<char*>(self, field::kSchoolBuildings) + 4 * i, 1);
        At<uint32_t>(self, local::kState) = 6;
        break;
    case 6:
        ApplyBuildings(self, local::kSchoolShare, field::kSchoolBuildings, local::kSchoolCover,
                       local::kSchoolDivisors, field::kEQGrids, field::kSchoolAutoBudget, 1, local::kSchoolReached);
        At<uint32_t>(self, local::kState) = 7;
        break;
    case 7:
        At<uint32_t>(self, local::kHealthServed) = 0;
        for (uint32_t i = 0; i < VectorSize(self, field::kHospitalBuildings); ++i)
            UpdateCoverage(self, self + local::kHealthCover, i, At<char*>(self, field::kHospitalBuildings) + 4 * i, 0);
        At<uint32_t>(self, local::kState) = 8;
        break;
    case 8:
        ApplyBuildings(self, local::kHealthShare, field::kHospitalBuildings, local::kHealthCover,
                       local::kHealthDivisors, field::kHQGrids, field::kHospitalAutoBudget, 0, local::kHealthReached);
        At<uint32_t>(self, local::kState) = 9;
        break;
    case 9: {
        SendFigure(self, 0xAA1A2864, static_cast<uint32_t>(Ftol(Virtual<double>(self, 0x80))));
        SendFigure(self, 0xAA1A2866, static_cast<uint32_t>(Ftol(Virtual<double>(self, 0x84))));
        SendFigure(self, 0xAA1A2CCA, At<uint32_t>(self, field::kPopulation));
        const int32_t population = At<int32_t>(self, field::kPopulation);
        if (population > 0) {
            const float health =
                static_cast<float>(static_cast<double>(At<int32_t>(self, local::kHealthServed)) / population);
            const double school = static_cast<double>(At<int32_t>(self, local::kSchoolServed)) / population;
            SendFigure(self, 0x4A8A3FE5, static_cast<uint32_t>(RoundFloat(school * 1000.0)));
            SendFigure(self, 0x4A8A3FE2, static_cast<uint32_t>(RoundFloat(static_cast<double>(health) * 1000.0)));
        } else {
            SendFigure(self, 0x4A8A3FE2, 0);
            SendFigure(self, 0x4A8A3FE5, 0);
        }
        At<uint32_t>(self, local::kState) = 0xA;
        break;
    }
    case 11:
        ClearAgeBuckets(self);
        At<uint32_t>(self, local::kState) = 4;
        At<uint8_t>(self, local::kParamsLoaded) = 0;
        break;
    case 12:
        reinterpret_cast<CellRestartFn>(addr::SC4CellIterator_Restart)(self + local::kIterator, 0, 0);
        SendMonthlyFigures(self);
        At<uint32_t>(self, local::kState) = 5;
        break;
    case 13:
        MarkTempGrid(self, field::kProximity, 1, false);
        At<uint32_t>(self, local::kState) = 1;
        break;
    case 14:
        MarkTempGrid(self, field::kProximity + 4, 2, false);
        At<uint32_t>(self, local::kState) = 2;
        break;
    case 15:
        MarkTempGrid(self, field::kProximity + 8, 3, true);
        At<uint32_t>(self, local::kState) = 3;
        break;
    default:
        break;
    }
    Free(scratch);
    return result;
}

// --- DoMessage --------------------------------------------------------------

// cIGZMessageTarget2 slot 3 (0x00700350), on the part at +4.
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = static_cast<char*>(part) - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x66956814)
        return Step(self, 5);
    if (type == 0x66956816) {
        if (At<uint32_t>(self, local::kState) == 0xA) {
            At<uint8_t>(self, local::kDirty) = 0;
            At<uint32_t>(self, local::kState) = 0;
        } else {
            At<uint8_t>(self, local::kDirty) = 1;
        }
        return 1;
    }
    if (type == 0x99EF1142 || type == 0x99EF1143) {
        if (Virtual<uint32_t>(message, 0x4C) != kBuildingType)
            return 1;
        void* const object = Virtual<void*>(message, 0x28);
        void* building = nullptr;
        if (!Virtual<uint8_t>(object, 0x0, kIidBuilding, &building))
            return 1;
        BuildingChanged(self, building, type == 0x99EF1142 ? 1 : 0);
        Virtual<uint32_t>(building, 0x8);
    }
    return 1;
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("residential messages: slot %d holds %08X, expected %08X; not installing", index,
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

using CopyGridFn = void(__cdecl*)(void*, void*, StoreFn, uint32_t);
using ClearListFn = void(__fastcall*)(void*, void*);
CopyGridFn g_originalCopyGrid = nullptr;
ClearListFn g_originalClearCurveList = nullptr;

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() &&
           Install(addr::cSC4ResidentialSimulator_CopyGrid, &CopyGrid, &g_originalCopyGrid) &&
           Install(addr::SC4VectorList_Clear, &ClearCurveList, &g_originalClearCurveList) &&
           SwapSlot(addr::cSC4ResidentialSimulator_vftable_4, 3, 0x00700350, reinterpret_cast<void*>(&DoMessage));
}

const bool g_registered = registry::Register({
    "simulators.residential.messages",
    "simulators/residential",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4ResidentialSimulator_vftable_4,
    "Our residential DoMessage, the step and its passes, and buildings added and removed (docs/residential.md)",
    false,
    InstallMessages,
});

} // namespace

// For Init (residential_object.cpp), which calls the same two helpers.
void LoadRecordsFor(void* first, void* second) { LoadRecords(first, second); }
void LoadParamsFor(char* self, char* params) { LoadParams(self, params); }

} // namespace opensc4::residential_sim
