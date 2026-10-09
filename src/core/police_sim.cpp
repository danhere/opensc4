// cSC4PoliceSimulator (class 0x6990C04F; docs/fire-police.md): the police
// department on top of the shared protection base (protection_sim.cpp, at
// +8), with jails, criminals, arrests and jailbreaks. Some of its code is
// also the fire simulator's (strike getters, station lists, funding).
//
// `this` differs by interface, as for fire (fire_sim.cpp): the
// cISC4PoliceSimulator slots get the object, the base's slots the object +
// 8 (`base`), the dispatch interface + 0xC, cIGZSerializable + 0x10.
// Offsets below are from the object.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::police_sim {
namespace {

namespace field {
constexpr std::size_t kBase = 0x8;              // cSC4ProtectionSimulator
constexpr std::size_t kRandom = 0x18;           // cRZRandom, base +0x10
constexpr std::size_t kCity = 0x6C;             // base +0x64
constexpr std::size_t kMessageServer = 0x70;    // base +0x68
constexpr std::size_t kBudget = 0x74;           // base +0x6C
constexpr std::size_t kDepartment = 0x7C;       // base +0x74
constexpr std::size_t kCoverage = 0x80;         // base +0x78
constexpr std::size_t kPower = 0x84;            // base +0x7C
constexpr std::size_t kStations = 0x88;         // base +0x80
constexpr std::size_t kAverage = 0x98;          // float, base +0x90
constexpr std::size_t kOnStrike = 0xA4;         // uint8, base +0x9C
constexpr std::size_t kStrikeDay = 0xA8;        // base +0xA0
constexpr std::size_t kLineItemKind = 0xB0;     // base +0xA8
constexpr std::size_t kInitialised = 0xC0;      // uint8
constexpr std::size_t kLoaded = 0xC1;           // uint8: set by Read
constexpr std::size_t kHelicopters = 0xC8;      // the helicopter dispatch source
constexpr std::size_t kService43D70 = 0xCC;
constexpr std::size_t kJailDepartment = 0xD0;
constexpr std::size_t kJails = 0xD4;            // list
constexpr std::size_t kJailCapacity = 0xD8;     // int32
constexpr std::size_t kJailed = 0xDC;           // int32
constexpr std::size_t kCriminals = 0xE0;        // int32
constexpr std::size_t kArrestsLastMonth = 0xE4; // int32
constexpr std::size_t kCrimesLastMonth = 0xE8;  // int32
constexpr std::size_t kArrests = 0xEC;          // int32: this month
constexpr std::size_t kCrimes = 0xF0;           // int32: this month
constexpr std::size_t kUnjailed = 0xF4;         // int32: crimes not cleared
constexpr std::size_t kFundedJails = 0xF8;      // list: jails with a line item
constexpr std::size_t kOvercrowding = 0xFC;     // float: capacity factor
constexpr std::size_t kEfficiencyCurve = 0x100; // by funding x 100
constexpr std::size_t kJailCurve = 0x110;       // by funding x 100
constexpr std::size_t kDepartmentShare = 0x120; // float
constexpr std::size_t kReleaseRate = 0x124;     // float: criminals let go a month
constexpr std::size_t kStrikeFactor = 0x128;    // float
constexpr std::size_t kStrikeDays = 0x12C;      // int32
constexpr std::size_t kStrikeEndFunding = 0x130;
constexpr std::size_t kStrikeCurve = 0x134;
constexpr std::size_t kArrestCoverage = 0x144;  // int32: coverage over which a crime is an arrest
constexpr std::size_t kJailbreakCurve = 0x148;  // by jailed share x 100
constexpr std::size_t kEscapeLow = 0x158;       // float
constexpr std::size_t kEscapeHigh = 0x15C;      // float
constexpr std::size_t kJailbreakEnd = 0x160;    // float: jailed share
constexpr std::size_t kJailbreaks = 0x164;      // list
constexpr std::size_t kSerializable = 0x10;
} // namespace field

constexpr uint32_t kTuningGroup = 0xE7E2C2DB;
constexpr uint32_t kTuningInstance = 0x28DFCF89;
constexpr uint32_t kJailLineItem = 0xEA56768A;
constexpr uint32_t kCrimeCounts = 0x8A70A0B9;   // uint32[4] per station
constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kStationChanged = 0xA949569A;

using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using SetUint32Fn = uint32_t(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using SelfFn = uint32_t(__thiscall*)(void* self);
using OneFn = uint32_t(__thiscall*)(void* self, void* argument);
using ScaleFn = void(__thiscall*)(void* self, void* info, float factor);
using NodesFn = char*(__thiscall*)(void* list, uint32_t count, uint32_t unused);
using EraseFn = void(__thiscall*)(void* list, char** next, char* node);
const auto GetPropertyUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
const auto GetPropertySint32 = reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32);
const auto GetPropertyFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
const auto GetPropertyPercentage = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyPercentage);
const auto SetPropertyUint32 = reinterpret_cast<SetUint32Fn>(addr::SC4_SetPropertyUint32);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto ScaleDispatchInfo = reinterpret_cast<ScaleFn>(addr::cSC4ProtectionSimulator_ScaleDispatchInfo);
const auto Free = reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free);

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void* Properties(void* occupant)
{
    return Virtual<void*>(occupant, 0x18);
}

float FloatOf(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

uint32_t BitsOf(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

int32_t MagicRound(double value)
{
    const auto rounded = static_cast<float>(value + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof(bits));
    return static_cast<int32_t>(bits + 0xB4C00000);
}

// An unsigned int as x87's fild with the 2^32 fix-up gives it.
double Unsigned(uint32_t value)
{
    return static_cast<double>(value);
}

uint32_t Today(char* police)
{
    void* const simulator = Virtual<void*>(At<void*>(police, field::kCity), 0xC8);
    return Virtual<uint32_t>(Virtual<void*>(simulator, 0x1C), 0x68);
}

// A new message of the given type, held; the caller fills, posts and
// releases it.
void* NewMessage(uint32_t type)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (message)
        Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    return message;
}

void Send(void* server, void* message)
{
    Virtual<uint32_t>(server, 0x10, message, 0u);
}

// The station's police line item: budget slot 51 fills `item` (0x20
// bytes; the game sets only +0x1C first).
bool FindLineItem(char* police, void* properties, uint32_t kind, uint8_t* item)
{
    At<uint32_t>(item, 0x1C) = 0;
    return Virtual<uint8_t>(At<void*>(police, field::kBudget), 0xCC, properties, kind, item) != 0;
}

// The city's slot 47 object told about `occupant` (slot 10 with a 0, or
// slot 12), with a string made from `text`.
void TellNews(const char* text, void* occupant, bool begun)
{
    void* const news = Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC);
    if (!news)
        return;
    struct GameString {
        std::uintptr_t vtable;
        char* begin;
        char* end;
        char* capacity;
        uint32_t zero;
    } string{};
    using FromTextFn = void(__thiscall*)(GameString* string, const char* value);
    reinterpret_cast<FromTextFn>(addr::SC4String_FromText)(&string, text);
    if (begun)
        Virtual<uint32_t>(news, 0x28, &string, occupant, 0u);
    else
        Virtual<uint32_t>(news, 0x30, &string, occupant);
    string.vtable = addr::SC4String_Vtable;
    if (string.begin)
        Free(string.begin);
}

char* FindNode(char* head, void* occupant)
{
    char* node = At<char*>(head, 0);
    while (node != head && At<void*>(node, 8) != occupant)
        node = At<char*>(node, 0);
    return node;
}

// Appended with a node from the list's allocator helper (0x00413DD0).
void Append(char* police, std::size_t list, void* occupant)
{
    char* const head = At<char*>(police, list);
    char* const node = reinterpret_cast<NodesFn>(addr::SC4List_AllocateNodes)(police + list, 1, 0);
    if (node + 8)
        At<void*>(node, 8) = occupant;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 4) = last;
    At<char*>(node, 0) = head;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
}

// Appended with a node from the list node allocator (0x00B207E4), as the
// getters copying lists do.
void AppendHeld(void** list, void* occupant)
{
    Virtual<uint32_t>(occupant, 0x4); // AddRef
    char* const end = static_cast<char*>(*list);
    auto* const added = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
    At<void*>(added, 8) = occupant;
    char* const last = At<char*>(end, 4);
    At<char*>(added, 0) = end;
    At<char*>(added, 4) = last;
    At<char*>(last, 0) = added;
    At<char*>(end, 4) = added;
}

int32_t CopyList(char* police, void** out, std::size_t list)
{
    using CopyFn = int32_t(__thiscall*)(void* self, void** out, void* in);
    return reinterpret_cast<CopyFn>(addr::SC4_CopyOccupantList)(police + field::kBase, out, police + list);
}

int32_t Count(char* police, std::size_t list)
{
    char* const head = At<char*>(police, list);
    int32_t count = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        ++count;
    return count;
}

// --- Identity -------------------------------------------------------------------------

// 0x006E0060 (slot 0; the other interfaces reach it through thunks).
uint8_t __fastcall QueryInterface(char* police, void*, uint32_t iid, void** out)
{
    char* found;
    switch (iid) {
    case 1:
    case 0x67659895:
        found = police;
        break;
    case 0x452294AA:
        found = police ? police + 0x8 : nullptr;
        break;
    case 0xC9C00435:
        found = police ? police + 0xC : nullptr;
        break;
    case 0xE4FDA3D4:
        found = police ? police + 0x10 : nullptr;
        break;
    default:
        return 0;
    }
    *out = found;
    Virtual<uint32_t>(police, 0x4); // AddRef
    return 1;
}

uint32_t __fastcall GetGZCLSID(void*, void*)
{
    return 0x6990C04F;
}

// --- Getters (several also the fire simulator's) ------------------------------------

uint8_t __fastcall IsOnStrike(char* self, void*)
{
    return self[field::kOnStrike];
}

uint32_t __fastcall GetStrikeStartDate(char* self, void*)
{
    return At<uint32_t>(self, field::kStrikeDay);
}

// Days since the strike began; -1 without one.
int32_t __fastcall GetStrikeLength(char* self, void*)
{
    if (!At<uint32_t>(self, field::kStrikeDay))
        return -1;
    return static_cast<int32_t>(Today(self) - At<uint32_t>(self, field::kStrikeDay));
}

int32_t __fastcall GetPoliceStations(char* self, void*, void** out)
{
    return CopyList(self, out, field::kStations);
}

int32_t __fastcall GetJails(char* police, void*, void** out)
{
    return CopyList(police, out, field::kJails);
}

uint8_t __fastcall IsPoliceStation(void*, void*, void* occupant)
{
    return Virtual<uint8_t>(Properties(occupant), 0xC, 0x48D71ED2u);
}

uint8_t __fastcall IsJail(void*, void*, void* occupant)
{
    return Virtual<uint8_t>(Properties(occupant), 0xC, 0x48D71EE0u);
}

float __fastcall GetAverageCoverage(char* self, void*)
{
    return At<float>(self, field::kAverage);
}

// 0x006E08D0: the station's line item's +0x10 and funding (+0x1C), each if
// asked for.
uint8_t __fastcall GetStationFunding(char* self, void*, void* station, uint32_t* value, uint32_t* funding)
{
    uint8_t item[0x20];
    if (!FindLineItem(self, Properties(station), At<uint32_t>(self, field::kLineItemKind), item))
        return 0;
    if (funding)
        *funding = At<uint32_t>(item, 0x1C);
    if (value)
        *value = At<uint32_t>(item, 0x10);
    return 1;
}

// 0x006E0F60: the station's reach strength (base slot 12), 0 without.
float __fastcall GetStationDispatchRadius(char* self, void*, void* station)
{
    uint32_t info[6];
    if (Virtual<uint8_t>(self + field::kBase, 0x30, station, info))
        return FloatOf(info[2]);
    return 0.0f;
}

int32_t __fastcall GetJailCount(char* police, void*)
{
    return Count(police, field::kJails);
}

// 0x006E1390: the stations' units (property 0x48D71ED5) summed.
uint32_t __fastcall GetTotalDispatchCount(char* police, void*)
{
    uint32_t total = 0;
    for (char* node = At<char*>(At<char*>(police, field::kStations), 0); node != At<char*>(police, field::kStations);
         node = At<char*>(node, 0)) {
        uint32_t units = 0;
        if (GetPropertyUint32(Properties(At<void*>(node, 8)), 0x48D71ED5, &units))
            total += units;
    }
    return total;
}

// The station's crime counts (property 0x8A70A0B9: crimes and arrests last
// month, then this month), read into a game vector.
struct Uint32Vector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

bool GetCounts(void* station, Uint32Vector* counts)
{
    using GetVectorFn = uint8_t(__cdecl*)(void* holder, uint32_t property, Uint32Vector* vector);
    return reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyUint32Vector)(Properties(station), kCrimeCounts,
                                                                           counts) != 0;
}

uint32_t __fastcall GetPoliceStationArrestCount(void*, void*, void* station)
{
    Uint32Vector counts{};
    uint32_t result = 0;
    if (GetCounts(station, &counts))
        result = counts.begin[1];
    if (counts.begin)
        Free(counts.begin);
    return result;
}

uint32_t __fastcall GetPoliceStationCrimeCount(void*, void*, void* station)
{
    Uint32Vector counts{};
    uint32_t result = 0;
    if (GetCounts(station, &counts))
        result = counts.begin[0];
    if (counts.begin)
        Free(counts.begin);
    return result;
}

// Four counts, zeros when the property is missing (the vector cut or
// grown to four, then cleared).
void CountsOrZeros(void* station, Uint32Vector* counts)
{
    using InsertFn = void(__thiscall*)(Uint32Vector* vector, uint32_t* where, uint32_t count, const uint32_t* value);
    if (GetCounts(station, counts))
        return;
    const auto size = static_cast<uint32_t>(counts->end - counts->begin);
    const uint32_t zero = 0;
    if (size > 4)
        counts->end = counts->begin + 4;
    else
        reinterpret_cast<InsertFn>(addr::SC4Uint32Vector_InsertN)(counts, counts->end, 4 - size, &zero);
    for (int i = 0; i < 4; ++i)
        counts->begin[i] = 0;
}

void SetCounts(void* station, Uint32Vector* counts)
{
    using SetVectorFn = void(__cdecl*)(void* holder, uint32_t property, Uint32Vector* vector);
    reinterpret_cast<SetVectorFn>(addr::SC4_SetPropertyUint32Vector)(Properties(station), kCrimeCounts, counts);
    if (counts->begin)
        Free(counts->begin);
}

// 0x006E2610: kind 0 a crime, 1 a crime with an arrest, 2 this month's
// counts become last month's.
void __fastcall RecordCrime(void*, void*, void* station, uint32_t kind)
{
    Properties(station);
    Uint32Vector counts{};
    CountsOrZeros(station, &counts);
    switch (kind) {
    case 1:
        ++counts.begin[3];
        [[fallthrough]];
    case 0:
        ++counts.begin[2];
        break;
    case 2:
        counts.begin[1] = counts.begin[3];
        counts.begin[0] = counts.begin[2];
        counts.begin[3] = 0;
        counts.begin[2] = 0;
        break;
    default:
        break;
    }
    SetCounts(station, &counts);
}

// --- Stations --------------------------------------------------------------------

// The funding of a line item found for `properties`: its +0x1C when the
// line has the item (slot 28), else the line's default (slot 10), `one`
// without a line; `fallback` without an item.
uint32_t LineFunding(char* police, void* properties, uint32_t kind, uint32_t fallback)
{
    uint8_t item[0x20];
    if (!FindLineItem(police, properties, kind, item))
        return fallback;
    void* const line = Virtual<void*>(At<void*>(police, field::kBudget), 0x70, At<uint32_t>(item, 0));
    if (!line)
        return Global<uint32_t>(addr::SC4_One2);
    if (Virtual<uint8_t>(line, 0x70, properties, kind))
        return At<uint32_t>(item, 0x1C);
    return *Virtual<uint32_t*>(line, 0x28);
}

double CurveAt(char* police, std::size_t curve, float funding)
{
    return EvaluateCurve(police + curve, static_cast<float>(static_cast<double>(funding) * 100.0)) *
           static_cast<double>(0.01f);
}

// 0x006E0E70 (slot 25): the efficiency curve at the station's funding,
// less on strike.
float* __fastcall GetStationEfficiency(char* police, void*, float* out, void* station)
{
    void* const properties = Properties(station);
    const uint32_t funding = LineFunding(police, properties, At<uint32_t>(police, field::kLineItemKind), 0);
    const auto efficiency = static_cast<float>(CurveAt(police, field::kEfficiencyCurve, FloatOf(funding)));
    if (Virtual<uint8_t>(police, 0x44, station))
        *out = static_cast<float>(static_cast<double>(efficiency) * At<float>(police, field::kStrikeFactor));
    else
        *out = efficiency;
    return out;
}

// 0x006E06E0: a jail's efficiency: the jail curve at its jail line item's
// funding (1 without an item or a line).
float* __fastcall GetJailEfficiency(char* police, void*, float* out, void* properties)
{
    uint32_t funding = Global<uint32_t>(addr::SC4_One2);
    uint8_t item[0x20];
    if (FindLineItem(police, properties, kJailLineItem, item)) {
        if (void* const line = Virtual<void*>(At<void*>(police, field::kBudget), 0x70, At<uint32_t>(item, 0))) {
            if (Virtual<uint8_t>(line, 0x70, properties, kJailLineItem))
                funding = At<uint32_t>(item, 0x1C);
            else
                funding = *Virtual<uint32_t*>(line, 0x28);
        }
    }
    *out = static_cast<float>(CurveAt(police, field::kJailCurve, FloatOf(funding)));
    return out;
}

// 0x006E0940 (slot 20): as fire's, the coverage at a distance.
uint8_t __fastcall GetOccupantCoverage(char* police, void*, void* station, const float* distance, float* coverage,
                                       float* radius)
{
    uint32_t info[6];
    if (!Virtual<uint8_t>(police + field::kBase, 0x38, station, info))
        return 0;
    *radius = FloatOf(info[2]);
    const auto reach = static_cast<uint32_t>(static_cast<int64_t>(Unsigned(info[5]) * *distance));
    if (reach > info[3]) {
        At<uint32_t>(coverage, 0) = 0;
        return 1;
    }
    if (reach < info[4]) {
        *coverage = FloatOf(info[2]);
        return 1;
    }
    const double part = Unsigned(info[3] - reach) * FloatOf(info[2]);
    *coverage = static_cast<float>(part / Unsigned(info[3] - info[4]));
    return 1;
}

// 0x006E0F90 (slot 11): 1 on strike, else the stations' chances combined.
float* __fastcall GetChanceOfStrike(char* police, void*, float* out)
{
    if (police[field::kOnStrike]) {
        At<uint32_t>(out, 0) = Global<uint32_t>(addr::SC4_One2);
        return out;
    }
    float total = Global<float>(addr::SC4_Zero2);
    char* const head = At<char*>(police, field::kStations);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        uint8_t item[0x20];
        uint32_t funding = Global<uint32_t>(addr::SC4_One2);
        if (FindLineItem(police, Properties(At<void*>(node, 8)), At<uint32_t>(police, field::kLineItemKind), item))
            funding = At<uint32_t>(item, 0x1C);
        const double chance = CurveAt(police, field::kStrikeCurve, FloatOf(funding));
        total = static_cast<float>((total + chance) - total * chance);
    }
    *out = total;
    return out;
}

// 0x006E1A00 (slot 16): none unless on strike; else the stations on strike
// appended, held; how many.
int32_t __fastcall GetStrikingStations(char* police, void*, void** list)
{
    if (!Virtual<uint8_t>(police, 0x28))
        return 0;
    int32_t count = 0;
    char* const head = At<char*>(police, field::kStations);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const station = At<void*>(node, 8);
        if (!Virtual<uint8_t>(police, 0x44, station))
            continue;
        AppendHeld(list, station);
        ++count;
    }
    return count;
}

// 0x006E13F0: the station nearest the cell (x, z) (city slot 97 gives its
// position).
void* __fastcall NearestStationTo(char* police, void*, int32_t x, int32_t z)
{
    float point[2];
    Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x184, x, z, &point[0], &point[1]);
    float best = 3.40282347e+38f;
    void* nearest = nullptr;
    for (char* node = At<char*>(At<char*>(police, field::kStations), 0); node != At<char*>(police, field::kStations);
         node = At<char*>(node, 0)) {
        void* const station = At<void*>(node, 8);
        float position[3];
        Virtual<uint32_t>(station, 0x20, position);
        const double dx = static_cast<double>(point[0]) - position[0];
        const double dz = static_cast<double>(point[1]) - position[2];
        const double distance = std::sqrt(dz * dz + dx * dx);
        if (distance < best) {
            best = static_cast<float>(distance);
            nearest = station;
        }
    }
    return nearest;
}

// --- Strikes -------------------------------------------------------------------------

uint8_t __fastcall BeginStrike(char* police, void*, void* station)
{
    if (police[field::kOnStrike])
        return 0;
    police[field::kOnStrike] = 1;
    At<uint32_t>(police, field::kStrikeDay) = Today(police);
    void* const message = NewMessage(0xA8DAF511);
    Virtual<uint32_t>(message, 0x30, station);
    Virtual<uint32_t>(message, 0x84, station);
    Send(At<void*>(police, field::kMessageServer), message);
    Release(message);
    return 1;
}

uint8_t __fastcall EndStrike(char* police, void*)
{
    if (!police[field::kOnStrike])
        return 0;
    police[field::kOnStrike] = 0;
    At<uint32_t>(police, field::kStrikeDay) = 0;
    void* const message = NewMessage(0xA8DAF512);
    Send(At<void*>(police, field::kMessageServer), message);
    Release(message);
    return 1;
}

// 0x006E1830: as fire's: a strike ends after +0x12C days or once funded at
// +0x130 or more, and begins when a 0-1 draw falls under the strike curve
// (as a fraction).
uint8_t __fastcall UpdateStationStrike(char* police, void*, void* station)
{
    using DrawFn = double(__thiscall*)(void* random);
    const uint32_t today = Today(police);
    uint32_t began = 0;
    GetPropertyUint32(Properties(station), 0xEAA69F83, &began);
    uint32_t now = began;
    const uint32_t kind = At<uint32_t>(police, field::kLineItemKind);
    if (began != 0) {
        if (today - began > At<uint32_t>(police, field::kStrikeDays)) {
            now = 0;
        } else {
            uint8_t item[0x20];
            uint32_t funding = Global<uint32_t>(addr::SC4_One2);
            if (FindLineItem(police, Properties(station), kind, item))
                funding = At<uint32_t>(item, 0x1C);
            if (!(FloatOf(funding) < At<float>(police, field::kStrikeEndFunding)))
                now = 0;
        }
    } else {
        uint8_t item[0x20];
        uint32_t funding = Global<uint32_t>(addr::SC4_One2);
        if (FindLineItem(police, Properties(station), kind, item))
            funding = At<uint32_t>(item, 0x1C);
        const auto chance = static_cast<float>(EvaluateCurve(
            police + field::kStrikeCurve, static_cast<float>(static_cast<double>(FloatOf(funding)) * 100.0)));
        const double draw = reinterpret_cast<DrawFn>(addr::cRZRandom_NextFloat)(police + field::kRandom);
        if (static_cast<double>(chance) * static_cast<double>(0.01f) > draw)
            now = today;
    }
    if (now == began)
        return 0;
    SetPropertyUint32(Properties(station), 0xEAA69F83, now);
    TellNews(Global<const char*>(addr::SC4_StrikingPoliceText), station, now != 0);
    return 1;
}

// 0x006E2570: every station's strike updated; once one changed, stations
// on strike are told (slot 12); with none on strike a strike in progress
// ends (slot 13); then the coverage is painted again.
void __fastcall OnMonthStrikes(char* police, void*)
{
    char* const head = At<char*>(police, field::kStations);
    uint8_t changed = 0;
    int32_t striking = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const station = At<void*>(node, 8);
        if (UpdateStationStrike(police, nullptr, station))
            changed = 1;
        if (Virtual<uint8_t>(police, 0x44, station)) {
            ++striking;
            if (changed)
                Virtual<uint8_t>(police, 0x30, station);
        }
    }
    if (!changed)
        return;
    if (striking == 0 && Virtual<uint8_t>(police, 0x28))
        Virtual<uint8_t>(police, 0x34);
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_PaintCoverage)(police + field::kBase);
}

// --- Jails ---------------------------------------------------------------------------

// 0x006E0AC0 (slot 28): a jail's capacity (property 0x48D71EE0) times its
// efficiency, rounded.
int32_t __fastcall GetJailCapacityOf(char* police, void*, void* jail)
{
    void* const properties = Properties(jail);
    float efficiency;
    GetJailEfficiency(police, nullptr, &efficiency, properties);
    uint32_t capacity = 0;
    GetPropertyUint32(properties, 0x48D71EE0, &capacity);
    return MagicRound(Unsigned(capacity) * efficiency);
}

// 0x006E0B30 (slot 36): criminals over capacity, at most 1.
float* __fastcall GetJailedPercentage(char* police, void*, float* out)
{
    const int32_t criminals = At<int32_t>(police, field::kCriminals);
    const int32_t capacity = At<int32_t>(police, field::kJailCapacity);
    if (criminals > capacity)
        At<uint32_t>(out, 0) = Global<uint32_t>(addr::SC4_One2);
    else
        *out = static_cast<float>(static_cast<double>(criminals) / capacity);
    return out;
}

// 0x006E0A00 (slot 27): criminals cut to capacity x +0xFC; the crimes not
// cleared (+0xF4) dropped.
int32_t __fastcall ResetUnjailedCriminals(char* police, void*)
{
    const auto room = static_cast<int32_t>(static_cast<int64_t>(
        static_cast<double>(At<int32_t>(police, field::kJailCapacity)) * At<float>(police, field::kOvercrowding)));
    const int32_t criminals = At<int32_t>(police, field::kCriminals);
    int32_t result = At<int32_t>(police, field::kUnjailed);
    At<int32_t>(police, field::kUnjailed) = 0;
    if (criminals >= room) {
        result = result - room + criminals;
        At<int32_t>(police, field::kCriminals) = room;
    }
    return result;
}

// 0x006E0A40 (slot 37): a jail's upkeep: its line item's int64 cost
// (+0x10) times its funding (0 without an item).
int64_t __fastcall GetJailCurrentUpkeepCost(char* police, void*, void* jail)
{
    uint32_t factor = Global<uint32_t>(addr::SC4_Zero2);
    int64_t cost = 0;
    uint8_t item[0x20];
    if (FindLineItem(police, Properties(jail), kJailLineItem, item)) {
        std::memcpy(&cost, item + 0x10, 8);
        factor = At<uint32_t>(item, 0x1C);
    }
    return static_cast<int64_t>(static_cast<double>(cost) * FloatOf(factor));
}

// 0x006E1240: capacity (+0xD8) summed over the jails (slot 28); each
// jail's population (property 0x48D71EE1) its rounded capacity times the
// jailed share; the total in +0xDC.
void __fastcall UpdateJails(char* police, void*)
{
    At<int32_t>(police, field::kJailCapacity) = 0;
    At<int32_t>(police, field::kJailed) = 0;
    char* const head = At<char*>(police, field::kJails);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        At<int32_t>(police, field::kJailCapacity) += Virtual<int32_t>(police, 0x70, At<void*>(node, 8));
    const int32_t capacity = At<int32_t>(police, field::kJailCapacity);
    float share = 0.0f;
    if (capacity > 0) {
        const int32_t criminals = At<int32_t>(police, field::kCriminals);
        if (criminals > capacity)
            share = 1.0f;
        else
            share = static_cast<float>(static_cast<double>(criminals) / capacity);
    }
    char* const jails = At<char*>(police, field::kJails);
    for (char* node = At<char*>(jails, 0); node != jails; node = At<char*>(node, 0)) {
        void* const properties = Properties(At<void*>(node, 8));
        uint32_t room = 0;
        GetPropertyUint32(properties, 0x48D71EE0, &room);
        float efficiency;
        GetJailEfficiency(police, nullptr, &efficiency, properties);
        const int32_t jailCapacity = MagicRound(Unsigned(room) * efficiency);
        const int32_t jailed = MagicRound(Unsigned(static_cast<uint32_t>(jailCapacity)) * share);
        SetPropertyUint32(properties, 0x48D71EE1, static_cast<uint32_t>(jailed));
        At<int32_t>(police, field::kJailed) += jailed;
    }
}

// 0x006E1070 (slot 39).
uint8_t __fastcall IsInJailbreak(char* police, void*, void* jail)
{
    if (!jail || !Virtual<uint8_t>(police, 0x84, jail))
        return 0;
    char* const head = At<char*>(police, field::kJailbreaks);
    return FindNode(head, jail) != head;
}

// 0x006E10C0: 0 for no jail, no prisoners or a breakout under way; else the
// jailbreak curve at the jailed share.
float* __fastcall GetJailbreakChance(char* police, void*, float* out, void* jail)
{
    uint32_t chance = Global<uint32_t>(addr::SC4_Zero2);
    if (jail && Virtual<uint8_t>(police, 0x84, jail) && At<int32_t>(police, field::kJailed) > 0) {
        using FindFn = char**(__cdecl*)(char** out, char* first, char* last, void* const* value);
        char* const head = At<char*>(police, field::kJailbreaks);
        char* found = nullptr;
        reinterpret_cast<FindFn>(addr::SC4List_Find)(&found, At<char*>(At<char*>(police, field::kJailbreaks), 0), head,
                                                    &jail);
        if (found != At<char*>(police, field::kJailbreaks)) {
            chance = Global<uint32_t>(addr::SC4_Zero2);
        } else {
            float jailed;
            Virtual<float*>(police, 0x90, &jailed);
            chance = BitsOf(static_cast<float>(CurveAt(police, field::kJailbreakCurve, jailed)));
        }
    }
    At<uint32_t>(out, 0) = chance;
    return out;
}

// 0x006E1190: each jail with a chance breaks out (slot 40) on a 0-1 draw
// under it.
void __fastcall CheckJailbreaks(char* police, void*)
{
    using DrawFn = double(__thiscall*)(void* random);
    for (char* node = At<char*>(At<char*>(police, field::kJails), 0); node != At<char*>(police, field::kJails);
         node = At<char*>(node, 0)) {
        void* const jail = At<void*>(node, 8);
        float chance;
        GetJailbreakChance(police, nullptr, &chance, jail);
        if (!(chance > Global<float>(addr::SC4_Zero2)))
            continue;
        const double draw = reinterpret_cast<DrawFn>(addr::cRZRandom_NextFloat)(police + field::kRandom);
        if (draw < chance)
            Virtual<uint8_t>(police, 0xA0, jail);
    }
}

// 0x006E11F0: breakouts end (slot 41) once the jailed share is under
// +0x160.
void __fastcall CheckJailbreakEnds(char* police, void*)
{
    char* node = At<char*>(At<char*>(police, field::kJailbreaks), 0);
    while (node != At<char*>(police, field::kJailbreaks)) {
        void* const building = At<void*>(node, 8);
        node = At<char*>(node, 0);
        float jailed;
        Virtual<float*>(police, 0x90, &jailed);
        if (jailed < At<float>(police, field::kJailbreakEnd))
            Virtual<uint8_t>(police, 0xA4, building);
    }
}

// 0x006E1AF0 (slot 40): a jail not already breaking out joins the list,
// held; some criminals escape (a draw between +0x158 and +0x15C times its
// capacity, rounded), message 0x2A614891, and the news told.
uint8_t __fastcall BeginJailbreak(char* police, void*, void* jail)
{
    using RangeFn = double(__thiscall*)(void* random, double low, double high);
    if (!jail || !Virtual<uint8_t>(police, 0x84, jail))
        return 0;
    char* const head = At<char*>(police, field::kJailbreaks);
    if (FindNode(head, jail) != head)
        return 0;
    Append(police, field::kJailbreaks, jail);
    Virtual<uint32_t>(jail, 0x4); // AddRef
    const auto share = static_cast<float>(reinterpret_cast<RangeFn>(addr::SC4Random_Uniform)(
        police + field::kRandom, At<float>(police, field::kEscapeLow), At<float>(police, field::kEscapeHigh)));
    const int32_t capacity = Virtual<int32_t>(police, 0x70, jail);
    const int32_t escaped = MagicRound(static_cast<double>(capacity) * share);
    if (escaped != 0) {
        int32_t& criminals = At<int32_t>(police, field::kCriminals);
        criminals -= escaped;
        if (criminals < 0)
            criminals = 0;
        UpdateJails(police, nullptr);
    }
    void* const message = NewMessage(0x2A614891);
    Virtual<uint32_t>(message, 0x30, police);
    Virtual<uint32_t>(message, 0x84, jail);
    Send(At<void*>(police, field::kMessageServer), message);
    TellNews(Global<const char*>(addr::SC4_JailbreakText), jail, true);
    Release(message);
    return 1;
}

// 0x006E1C90 (slot 41): the news told; a jail breaking out leaves the
// list, message 0x2A614892, released. Always 0.
uint8_t __fastcall EndJailbreak(char* police, void*, void* jail)
{
    if (!jail || !Virtual<uint8_t>(police, 0x84, jail))
        return 0;
    TellNews(Global<const char*>(addr::SC4_JailbreakText), jail, false);
    char* const head = At<char*>(police, field::kJailbreaks);
    char* const node = FindNode(head, jail);
    if (node == head)
        return 0;
    void* const message = NewMessage(0x2A614892);
    Virtual<uint32_t>(message, 0x30, police);
    Virtual<uint32_t>(message, 0x84, jail);
    Send(At<void*>(police, field::kMessageServer), message);
    char* next = nullptr;
    reinterpret_cast<EraseFn>(addr::SC4List_Erase)(police + field::kJailbreaks, &next, node);
    Release(jail);
    Release(message);
    return 0;
}

// 0x006E08A0 (slot 42).
int32_t __fastcall GetJailbreakBuildings(char* police, void*, void** out)
{
    char* const head = At<char*>(police, field::kJailbreaks);
    if (At<char*>(head, 0) == head)
        return 0;
    return CopyList(police, out, field::kJailbreaks);
}

// --- The month and crimes ------------------------------------------------------------

// 0x006E2B50 (slot 38): this month's arrests go to jail; this month's
// counts become last month's, per station too; figures 0x0A5CC155/56.
void __fastcall ProcessMonthlyCrimeStats(char* police, void*)
{
    const int32_t arrests = At<int32_t>(police, field::kArrests);
    if (arrests > 0) {
        int32_t& criminals = At<int32_t>(police, field::kCriminals);
        criminals += arrests;
        if (criminals < 0)
            criminals = 0;
        UpdateJails(police, nullptr);
    }
    At<int32_t>(police, field::kArrestsLastMonth) = At<int32_t>(police, field::kArrests);
    At<int32_t>(police, field::kArrests) = 0;
    At<int32_t>(police, field::kCrimesLastMonth) = At<int32_t>(police, field::kCrimes);
    At<int32_t>(police, field::kCrimes) = 0;
    for (char* node = At<char*>(At<char*>(police, field::kStations), 0); node != At<char*>(police, field::kStations);
         node = At<char*>(node, 0)) {
        void* const station = At<void*>(node, 8);
        Properties(station);
        Uint32Vector counts{};
        CountsOrZeros(station, &counts);
        counts.begin[1] = counts.begin[3];
        counts.begin[0] = counts.begin[2];
        counts.begin[3] = 0;
        counts.begin[2] = 0;
        SetCounts(station, &counts);
    }
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    Virtual<uint32_t>(figures, 0x30, 0x0A5CC155u, At<int32_t>(police, field::kCrimesLastMonth));
    Virtual<uint32_t>(figures, 0x30, 0x0A5CC156u, At<int32_t>(police, field::kArrestsLastMonth));
}

// 0x006E2A80 (base slot 6): criminals let go (+0x124 of them), strikes,
// jail properties read, breakouts ending and starting.
void __fastcall OnMonth(char* base, void*)
{
    char* const police = base - field::kBase;
    const int32_t released = MagicRound(static_cast<double>(At<int32_t>(police, field::kCriminals)) *
                                        At<float>(police, field::kReleaseRate));
    if (released != 0) {
        int32_t& criminals = At<int32_t>(police, field::kCriminals);
        criminals -= released;
        if (criminals < 0)
            criminals = 0;
        UpdateJails(police, nullptr);
    }
    OnMonthStrikes(police, nullptr);
    char* const head = At<char*>(police, field::kJails);
    for (char* node = At<char*>(head, 0); node != head;) {
        void* const jail = At<void*>(node, 8);
        node = At<char*>(node, 0);
        void* const properties = Properties(jail);
        uint32_t values[3] = {};
        GetPropertyUint32(properties, 0x48D71EE0, &values[0]);
        GetPropertyUint32(properties, 0x48D71EE1, &values[1]);
        GetPropertyUint32(properties, 0x48D71EE4, &values[2]);
    }
    CheckJailbreakEnds(police, nullptr);
    CheckJailbreaks(police, nullptr);
}

// 0x006E2920 (base slot 3): the base's handler; then jail funding changes
// (0x08DAC3E7, for the jail department), crimes (0x0A159D95: an
// arrest where the coverage is over +0x144, counted for the nearest
// station) and the tuning exemplar. Always handled.
uint8_t __fastcall DoMessage(char* base, void*, void* message)
{
    using DoMessageFn = uint8_t(__thiscall*)(void* self, void* message);
    char* const police = base - field::kBase;
    reinterpret_cast<DoMessageFn>(addr::cSC4ProtectionSimulator_DoMessage)(base, message);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x08DAC3E7) {
        const uint32_t id = Virtual<uint32_t>(At<void*>(police, field::kJailDepartment), 0xC);
        if (Virtual<uint32_t>(message, 0x38) == id)
            UpdateJails(police, nullptr);
    } else if (type == 0x0A159D95) {
        void* crime = nullptr;
        Virtual<uint8_t>(Virtual<void*>(message, 0x80), 0x0, 0xE818FCEDu, &crime);
        if (!crime)
            return 1;
        int32_t x;
        int32_t z;
        Virtual<uint32_t>(crime, 0x54, &x, &z);
        const auto covered = Virtual<uint8_t>(At<void*>(police, field::kCoverage), 0x1C, x, z);
        void* const station = NearestStationTo(police, nullptr, x, z);
        if (static_cast<int32_t>(covered) > At<int32_t>(police, field::kArrestCoverage)) {
            ++At<int32_t>(police, field::kArrests);
            if (station)
                RecordCrime(police, nullptr, station, 1);
        } else {
            ++At<int32_t>(police, field::kUnjailed);
            if (station)
                RecordCrime(police, nullptr, station, 0);
        }
        ++At<int32_t>(police, field::kCrimes);
        Release(crime);
    } else if (type == 0xC53D10AA) {
        const uint32_t group = Virtual<uint32_t>(message, 0x24);
        const uint32_t instance = Virtual<uint32_t>(message, 0x38);
        if (group == kTuningGroup && instance == kTuningInstance) {
            using TuningFn = void(__thiscall*)(void* self);
            reinterpret_cast<TuningFn>(addr::cSC4PoliceSimulator_LoadTuning)(police);
        }
    }
    return 1;
}

// --- The base's slots ---------------------------------------------------------------

// 0x006E02F0 (base slot 10) and 0x006E02D0 (slot 15).
uint32_t __fastcall SetUnitMask(void*, void*, void* station, uint32_t mask)
{
    return SetPropertyUint32(Properties(station), 0x48D71ED8, mask);
}

uint32_t __fastcall SetFirstUnit(void*, void*, void* station, uint32_t first)
{
    return SetPropertyUint32(Properties(station), 0x48D71ED7, first);
}

// 0x006E0C50 (base slot 11): the line item's +0x10, if asked for.
uint8_t __fastcall GetStationFundingValue(char* base, void*, void* station, uint32_t* out)
{
    void* const properties = Properties(station);
    if (!out)
        return 1;
    uint8_t item[0x20];
    At<uint32_t>(item, 0x1C) = 0;
    if (!Virtual<uint8_t>(At<void*>(base, 0x6C), 0xCC, properties, At<uint32_t>(base, 0xA8), item))
        return 0;
    *out = At<uint32_t>(item, 0x10);
    return 1;
}

// 0x006E0CB0 (base slot 12): a dispatch source's reach (properties
// 0x48D71ED9 outer, 0x48D71EDA inner, 0x48D71EDB strength), scaled by its
// efficiency (slot 25).
uint8_t __fastcall GetSourceReach(char* base, void*, void* source, char* info)
{
    void* const properties = Properties(source);
    At<uint32_t>(info, 0) = 0;
    At<uint32_t>(info, 4) = 0;
    At<int32_t>(info, 0xC) = 0;
    GetPropertySint32(properties, 0x48D71ED9, info + 0xC);
    if (At<int32_t>(info, 0xC) == 0)
        return 0;
    GetPropertySint32(properties, 0x48D71EDA, info + 0x10);
    GetPropertyFloat(properties, 0x48D71EDB, info + 8);
    At<int32_t>(info, 0x14) = At<int32_t>(info, 0xC);
    auto slot = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(source)); // the game's out slot
    const float* const efficiency = Virtual<float*>(base - field::kBase, 0x64, &slot, source);
    ScaleDispatchInfo(base, info, *efficiency);
    return 1;
}

// 0x006E0D50 (base slot 14): a station's position and reach (properties
// 0x48D71ED0 outer, 0x48D71ED1 inner, 0x48D71ED2 strength), scaled by the
// department's funding curve for an occupant with interface 0xE9793A65,
// else by the station's efficiency (slot 25).
uint8_t __fastcall GetStationInfo(char* base, void*, void* station, char* info)
{
    void* const properties = Properties(station);
    At<int32_t>(info, 0xC) = 0;
    GetPropertySint32(properties, 0x48D71ED0, info + 0xC);
    if (At<int32_t>(info, 0xC) == 0)
        return 0;
    GetPropertySint32(properties, 0x48D71ED1, info + 0x10);
    GetPropertyFloat(properties, 0x48D71ED2, info + 8);
    float position[3];
    Virtual<uint32_t>(station, 0x20, position);
    At<float>(info, 4) = position[2];
    At<int32_t>(info, 0x14) = At<int32_t>(info, 0xC);
    At<float>(info, 0) = position[0];
    void* other = nullptr;
    float factor;
    if (Virtual<uint8_t>(station, 0x0, 0xE9793A65u, &other)) {
        const float* const funding = Virtual<float*>(At<void*>(base, 0x74), 0x28);
        factor = static_cast<float>(
            EvaluateCurve(base + 0xF8, static_cast<float>(static_cast<double>(*funding) * 100.0)) *
            static_cast<double>(0.01f));
    } else {
        auto slot = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(station));
        factor = *Virtual<float*>(base - field::kBase, 0x64, &slot, station);
    }
    ScaleDispatchInfo(base, info, factor);
    if (other)
        Release(other);
    return 1;
}

// A message 0xA949569A about the simulator, for a police station.
void TellStationChanged(char* police, void* occupant)
{
    if (!Virtual<uint8_t>(police, 0x58, occupant))
        return;
    void* const message = NewMessage(kStationChanged);
    Virtual<uint32_t>(message, 0x84, police);
    Send(At<void*>(police, field::kMessageServer), message);
    Release(message);
}

// 0x006E0B80 (base slot 9).
void __fastcall OnStationChanged(char* base, void*, void* occupant)
{
    reinterpret_cast<OneFn>(addr::cSC4ProtectionSimulator_OnStationChanged)(base, occupant);
    TellStationChanged(base - field::kBase, occupant);
}

// 0x006E1DB0 (base slot 7): the base's; police stations told; a jail joins
// the jails (held), and once its line item exists the funded jails, then
// the jails are recomputed.
void __fastcall OnStationAdded(char* base, void*, void* occupant)
{
    char* const police = base - field::kBase;
    reinterpret_cast<OneFn>(addr::cSC4ProtectionSimulator_OnStationAdded)(base, occupant);
    if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
        return;
    TellStationChanged(police, occupant);
    if (!Virtual<uint8_t>(police, 0x84, occupant))
        return;
    void* const properties = Properties(occupant);
    Virtual<uint32_t>(occupant, 0x4); // AddRef
    Append(police, field::kJails, occupant);
    uint8_t item[0x20];
    if (FindLineItem(police, properties, kJailLineItem, item) &&
        Virtual<void*>(At<void*>(police, field::kBudget), 0x70, At<uint32_t>(item, 0))) {
        using FindFn = char**(__cdecl*)(char** out, char* first, char* last, void* const* value);
        using PushBackFn = void(__thiscall*)(void* list, void* const* value);
        char* const head = At<char*>(police, field::kFundedJails);
        char* found = nullptr;
        reinterpret_cast<FindFn>(addr::SC4List_Find)(&found, At<char*>(head, 0), head, &occupant);
        if (found == head) {
            Virtual<uint32_t>(occupant, 0x4);
            reinterpret_cast<PushBackFn>(addr::SC4List_PushBack)(police + field::kFundedJails, &occupant);
        }
        uint8_t second[0x20];
        if (FindLineItem(police, Properties(occupant), kJailLineItem, second)) {
            void* const line = Virtual<void*>(At<void*>(police, field::kBudget), 0x70, At<uint32_t>(second, 0));
            Virtual<uint32_t>(line, 0x58, Properties(occupant), kJailLineItem);
        }
    }
    UpdateJails(police, nullptr);
}

// 0x006E1570 (base slot 8): a building leaves the funded jails and the
// jails (its jail line item told, slot 23; jails recomputed), then the
// base's; police stations told.
void __fastcall OnStationRemoved(char* base, void*, void* occupant)
{
    char* const police = base - field::kBase;
    if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
        return;
    char* const funded = At<char*>(police, field::kFundedJails);
    if (char* const node = FindNode(funded, occupant); node != At<char*>(police, field::kFundedJails)) {
        Release(At<void*>(node, 8));
        char* next = nullptr;
        reinterpret_cast<EraseFn>(addr::SC4List_Erase)(police + field::kFundedJails, &next, node);
    }
    char* const jails = At<char*>(police, field::kJails);
    if (char* const node = FindNode(jails, occupant); node != At<char*>(police, field::kJails)) {
        Virtual<uint32_t>(occupant, 0x4); // AddRef
        Release(At<void*>(node, 8));
        char* next = nullptr;
        reinterpret_cast<EraseFn>(addr::SC4List_Erase)(police + field::kJails, &next, node);
        void* const properties = Properties(occupant);
        uint8_t item[0x20];
        if (FindLineItem(police, properties, kJailLineItem, item)) {
            void* const line = Virtual<void*>(At<void*>(police, field::kBudget), 0x70, At<uint32_t>(item, 0));
            Virtual<uint32_t>(line, 0x5C, properties, kJailLineItem);
        }
        UpdateJails(police, nullptr);
        Release(occupant);
    }
    reinterpret_cast<OneFn>(addr::cSC4ProtectionSimulator_OnStationRemoved)(base, occupant);
    TellStationChanged(police, occupant);
}

// 0x006E0610 (base slot 17, the walk's end): figures 0x4A11D354/55 and
// message 0xA8DAF510 about the simulator.
void __fastcall OnWalkEnd(char* base, void*)
{
    char* const police = base - field::kBase;
    void* self = nullptr;
    Virtual<uint8_t>(police, 0x0, 1u, &self);
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    Virtual<uint32_t>(figures, 0x30, 0x4A11D354u,
                      static_cast<int32_t>(static_cast<double>(At<float>(base, 0x94)) * 100.0));
    Virtual<uint32_t>(figures, 0x30, 0x4A11D355u, static_cast<int32_t>(At<float>(base, 0x90)));
    void* const message = NewMessage(0xA8DAF510);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Send(At<void*>(base, 0x68), message);
    Release(message);
    if (self)
        Release(self);
}

// --- The dispatch interface (+0xC) --------------------------------------------------

uint32_t __fastcall IsUnitKind(void*, void*, uint32_t kind)
{
    return kind == 0;
}

// 0x006E0220 (+0xC slot 7): as fire's with properties 0x48D71ED5 (count),
// 0x48D71ED8 (mask) and 0x48D71ED7 (first slot).
uint8_t __fastcall GetUnits(void*, void*, void* station, uint32_t* mask, uint32_t* first, uint32_t* count)
{
    void* const properties = Properties(station);
    uint32_t units = 0;
    if (!GetPropertyUint32(properties, 0x48D71ED5, &units))
        return 0;
    if (count)
        *count = units;
    if (mask) {
        uint32_t bits = 0;
        if (!GetPropertyUint32(properties, 0x48D71ED8, &bits))
            return 0;
        *mask = bits & ((1u << (units & 31)) - 1);
    }
    if (first) {
        uint32_t slot = 0;
        if (!GetPropertyUint32(properties, 0x48D71ED7, &slot))
            return 0;
        *first = slot;
    }
    return 1;
}

// 0x006E0C00 (+0xC slot 11): city slot 72's slot 10 picks 0x4207 or
// 0x4228.
uint8_t __fastcall GetUnitAppearance(void*, void*, uint32_t, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t)
{
    void* const object = Virtual<void*>(Global<void*>(addr::SC4_City), 0x120);
    *a = Virtual<uint8_t>(object, 0x28) ? 0x4207 : 0x4228;
    b[0] = 0;
    b[1] = 0;
    b[2] = 0;
    *c = 0x11882;
    return 1;
}

// --- Tuning (0x006E2110) -----------------------------------------------------------

struct PointVector {
    float* begin;
    float* end;
    float* capacity;
};

void PushPoint(PointVector& points, float x, float y)
{
    using InsertFn = void(__thiscall*)(PointVector* points, float* where, const float* value, void* allocator,
                                       uint32_t count, uint32_t atEnd);
    const float value[2] = {x, y};
    uint8_t allocator = 0;
    if (points.end == points.capacity) {
        reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN)(&points, points.end, value, &allocator, 1, 1);
        return;
    }
    if (points.end) {
        points.end[0] = value[0];
        points.end[1] = value[1];
    }
    points.end += 2;
}

void __fastcall LoadTuning(char* police, void*)
{
    using SetPointsFn = uint8_t(__thiscall*)(void* curve, const PointVector* points);
    using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
    const auto setPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
    const auto loadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
    At<int32_t>(police, field::kBase + 0x2C) = 2;  // block size
    At<int32_t>(police, field::kBase + 0x30) = 40; // steps a walk
    At<float>(police, field::kOvercrowding) = 1.1f;
    PointVector efficiency{};
    PushPoint(efficiency, 0.0f, 0.0f);
    PushPoint(efficiency, 100.0f, 100.0f);
    PushPoint(efficiency, 120.0f, 110.0f);
    setPoints(police + field::kEfficiencyCurve, &efficiency);
    setPoints(police + field::kJailCurve, &efficiency);
    At<float>(police, field::kDepartmentShare) = 1.2f;
    At<float>(police, field::kReleaseRate) = 0.01f;
    At<float>(police, field::kStrikeFactor) = 0.5f;
    At<int32_t>(police, field::kStrikeDays) = 10;
    At<float>(police, field::kStrikeEndFunding) = 0.9f;
    At<int32_t>(police, field::kArrestCoverage) = 16;
    for (const std::size_t curve : {field::kStrikeCurve, field::kJailbreakCurve}) {
        PointVector points{};
        PushPoint(points, 0.0f, 20.0f);
        PushPoint(points, 100.0f, 100.0f);
        setPoints(police + curve, &points);
        if (points.begin)
            Free(points.begin);
    }
    At<float>(police, field::kJailbreakEnd) = 0.5f;
    At<float>(police, field::kEscapeLow) = 1.0f;
    At<float>(police, field::kEscapeHigh) = 5.0f;
    const uint32_t key[3] = {0x6534284A, kTuningGroup, kTuningInstance};
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        GetPropertyPercentage(holder, 0x68DDAE92, police + field::kOvercrowding);
        loadCurve(police + field::kEfficiencyCurve, key, 0x68DDAE93);
        loadCurve(police + field::kJailCurve, key, 0x68DDAEA0);
        GetPropertyPercentage(holder, 0x68DDAE95, police + field::kDepartmentShare);
        GetPropertyPercentage(holder, 0x68DDAE97, police + field::kReleaseRate);
        GetPropertyPercentage(holder, 0x68DDAE9A, police + field::kStrikeFactor);
        GetPropertySint32(holder, 0x68DDAE9B, police + field::kStrikeDays);
        GetPropertyPercentage(holder, 0x68DDAE9C, police + field::kStrikeEndFunding);
        GetPropertySint32(holder, 0x68DDAE9F, police + field::kArrestCoverage);
        loadCurve(police + field::kStrikeCurve, key, 0x68DDAE9E);
        loadCurve(police + field::kJailbreakCurve, key, 0xEA614F63);
        if (void* const property = Virtual<void*>(holder, 0x24, 0xEA614F64u)) {
            void* const value = Virtual<void*>(property, 0x14);
            const float* const pair = Virtual<const float*>(value, 0x1BC);
            if (Virtual<uint32_t>(value, 0x10) >= 2) {
                At<float>(police, field::kEscapeLow) = static_cast<float>(static_cast<double>(pair[0]) * 0.01f);
                At<float>(police, field::kEscapeHigh) = static_cast<float>(static_cast<double>(pair[1]) * 0.01f);
            }
        }
        GetPropertyPercentage(holder, 0xEA614F65, police + field::kJailbreakEnd);
        Release(exemplar);
    }
    if (efficiency.begin)
        Free(efficiency.begin);
}

// --- Init (0x006E2740) and Shutdown (0x006E0310) --------------------------------

// A department line (budget slot 25) set up as Init does.
void SetUpDepartment(char* police, std::size_t at, uint32_t id, uint32_t part)
{
    void* const department = Virtual<void*>(At<void*>(police, field::kBudget), 0x64, id, 0x4A357B40u);
    At<void*>(police, at) = department;
    Virtual<uint32_t>(department, 0x4);
    Virtual<uint32_t>(At<void*>(police, at), 0x14, 0x6A3FF01Cu, part);
    Virtual<uint32_t>(At<void*>(police, at), 0x34, police + field::kDepartmentShare);
    void* const line = Virtual<void*>(At<void*>(police, at), 0x44, 1u, 1u);
    Virtual<uint32_t>(line, 0x48, 1u, 0u);
}

uint8_t __fastcall Init(char* police, void*)
{
    if (police[field::kInitialised])
        return 1;
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("police sim: first Init (%p)", static_cast<void*>(police));
    }
    police[field::kInitialised] = 1;
    uint8_t& registered = Global<uint8_t>(addr::cSC4PoliceSimulator_JailbreakFlag);
    if (!registered) {
        registered = 1;
        Virtual<uint32_t>(Global<void*>(addr::SC4_Service43C94), 0x114,
                          static_cast<uint32_t>(addr::cSC4PoliceSimulator_ClearJailbreakFlag), 0u);
    }
    LoadTuning(police, nullptr);
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_Init)(police + field::kBase);
    At<uint32_t>(police, field::kLineItemKind) = 0x0A567BAA;
    if (!At<void*>(police, field::kDepartment))
        SetUpDepartment(police, field::kDepartment, 0xA2963983, 0xAA493855);
    if (!At<void*>(police, field::kJailDepartment))
        SetUpDepartment(police, field::kJailDepartment, 0xA2963984, 0x4A4FC955);
    if (!At<void*>(police, field::kService43D70)) {
        void* const service = Global<void*>(addr::SC4_Service43D70);
        At<void*>(police, field::kService43D70) = service;
        Virtual<uint32_t>(service, 0x4);
    }
    if (void* const server = At<void*>(police, field::kMessageServer))
        Virtual<uint8_t>(server, 0x14, police + field::kBase, 0x0A159D95u);
    if (void* const grid = At<void*>(police, field::kCoverage))
        Virtual<uint32_t>(grid, 0x18, 0x49D5BBB9u);
    if (void* const grid = At<void*>(police, field::kPower))
        Virtual<uint32_t>(grid, 0x18, 0x49D5BBCEu);
    if (!At<void*>(police, field::kHelicopters)) {
        using NewFn = void*(__cdecl*)(std::size_t size);
        using ConstructFn = void*(__thiscall*)(void* memory);
        void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x28);
        void* const helicopters =
            memory ? reinterpret_cast<ConstructFn>(addr::SC4PoliceHelicopterSource_Construct)(memory) : nullptr;
        void* const old = At<void*>(police, field::kHelicopters);
        if (helicopters != old) {
            if (helicopters)
                Virtual<uint32_t>(helicopters, 0x4);
            At<void*>(police, field::kHelicopters) = helicopters;
            if (old)
                Release(old);
        }
    }
    if (void* const helicopters = At<void*>(police, field::kHelicopters))
        reinterpret_cast<SelfFn>(addr::SC4DispatchSource_Init)(helicopters);
    return 1;
}

uint8_t __fastcall Shutdown(char* police, void*)
{
    if (!police[field::kInitialised])
        return 1;
    police[field::kInitialised] = 0;
    if (At<void*>(police, field::kHelicopters)) {
        reinterpret_cast<SelfFn>(addr::SC4DispatchSource_Shutdown)(At<void*>(police, field::kHelicopters));
        if (void* const helicopters = At<void*>(police, field::kHelicopters)) {
            At<void*>(police, field::kHelicopters) = nullptr;
            Release(helicopters);
        }
    }
    if (void* const server = At<void*>(police, field::kMessageServer))
        Virtual<uint8_t>(server, 0x18, police + field::kBase, 0x0A159D95u);
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_Shutdown)(police + field::kBase);
    void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), 0x88);
    Virtual<uint32_t>(occupants, 0xC0, police + field::kJails);
    Virtual<uint32_t>(occupants, 0xC0, police + field::kFundedJails);
    Virtual<uint32_t>(occupants, 0xC0, police + field::kJailbreaks);
    for (const std::size_t at : {field::kService43D70, field::kDepartment, field::kJailDepartment}) {
        if (void* const held = At<void*>(police, at)) {
            Release(held);
            At<void*>(police, at) = nullptr;
        }
    }
    return 1;
}

// --- Write (0x006E03F0) and Read (0x006E04E0), this = +0x10 -------------------------

uint32_t StreamOk(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) == 0;
}

constexpr std::size_t kCounters[] = {field::kJailCapacity, field::kJailed,  field::kCriminals, field::kArrestsLastMonth,
                                     field::kArrests,      field::kUnjailed, field::kCrimesLastMonth, field::kCrimes};

uint32_t __fastcall Write(char* serializable, void*, void* stream)
{
    using BaseFn = uint8_t(__thiscall*)(void* self, void* stream);
    using WriteObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
    char* const police = serializable - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, uint16_t{8}); // version
    if (reinterpret_cast<BaseFn>(addr::cSC4ProtectionSimulator_Write)(serializable, stream)) {
        const auto writeReference = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);
        const auto writeList = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReferenceList);
        writeReference(stream, At<void*>(police, field::kJailDepartment));
        writeList(stream, police + field::kFundedJails);
        writeList(stream, police + field::kJails);
        for (const std::size_t at : kCounters)
            Virtual<uint8_t>(stream, 0x20, At<int32_t>(police, at));
        char* const helicopters = At<char*>(police, field::kHelicopters);
        writeReference(stream, helicopters ? helicopters + 8 : nullptr);
    }
    return StreamOk(stream);
}

uint32_t __fastcall Read(char* serializable, void*, void* stream)
{
    using BaseFn = uint8_t(__thiscall*)(void* self, void* stream);
    using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);
    using ReadObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
    char* const police = serializable - field::kSerializable;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version > 8)
        return 0;
    if (reinterpret_cast<BaseFn>(addr::cSC4ProtectionSimulator_Read)(serializable, stream)) {
        const auto readReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
        const auto readList = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReferenceList);
        readReference(stream, 0x223E2B67, &At<void*>(police, field::kJailDepartment), 0);
        readList(stream, police + field::kFundedJails);
        readList(stream, police + field::kJails);
        for (const std::size_t at : kCounters)
            Virtual<uint8_t>(stream, 0x20, police + at);
        if (version > 7) {
            void*& helicopters = At<void*>(police, field::kHelicopters);
            if (void* const old = helicopters) {
                helicopters = nullptr;
                Release(old);
            }
            readReference(stream, 0xC9C00435, &helicopters, 0);
        }
    }
    police[field::kLoaded] = 1;
    return StreamOk(stream);
}

// Hooks to leave out, for bisecting: OPENSC4_POLICE_SKIP lists function
// addresses (hex).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_POLICE_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[80] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4PoliceSimulator_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4PoliceSimulator_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
        {addr::cSC4PoliceSimulator_IsOnStrike, reinterpret_cast<void*>(&IsOnStrike)},
        {addr::cSC4PoliceSimulator_GetStrikeStartDate, reinterpret_cast<void*>(&GetStrikeStartDate)},
        {addr::cSC4PoliceSimulator_GetStrikeLength, reinterpret_cast<void*>(&GetStrikeLength)},
        {addr::cSC4PoliceSimulator_GetPoliceStations, reinterpret_cast<void*>(&GetPoliceStations)},
        {addr::cSC4PoliceSimulator_GetJails, reinterpret_cast<void*>(&GetJails)},
        {addr::cSC4PoliceSimulator_IsPoliceStation, reinterpret_cast<void*>(&IsPoliceStation)},
        {addr::cSC4PoliceSimulator_IsJail, reinterpret_cast<void*>(&IsJail)},
        {addr::cSC4PoliceSimulator_GetAverageCoverage, reinterpret_cast<void*>(&GetAverageCoverage)},
        {addr::cSC4PoliceSimulator_GetStationFunding, reinterpret_cast<void*>(&GetStationFunding)},
        {addr::cSC4PoliceSimulator_GetStationDispatchRadius, reinterpret_cast<void*>(&GetStationDispatchRadius)},
        {addr::cSC4PoliceSimulator_GetJailCount, reinterpret_cast<void*>(&GetJailCount)},
        {addr::cSC4PoliceSimulator_GetTotalDispatchCount, reinterpret_cast<void*>(&GetTotalDispatchCount)},
        {addr::cSC4PoliceSimulator_GetPoliceStationArrestCount, reinterpret_cast<void*>(&GetPoliceStationArrestCount)},
        {addr::cSC4PoliceSimulator_GetPoliceStationCrimeCount, reinterpret_cast<void*>(&GetPoliceStationCrimeCount)},
        {addr::cSC4PoliceSimulator_RecordCrime, reinterpret_cast<void*>(&RecordCrime)},
        {addr::cSC4PoliceSimulator_GetStationEfficiency, reinterpret_cast<void*>(&GetStationEfficiency)},
        {addr::cSC4PoliceSimulator_GetJailEfficiency, reinterpret_cast<void*>(&GetJailEfficiency)},
        {addr::cSC4PoliceSimulator_GetOccupantCoverage, reinterpret_cast<void*>(&GetOccupantCoverage)},
        {addr::cSC4PoliceSimulator_GetChanceOfStrike, reinterpret_cast<void*>(&GetChanceOfStrike)},
        {addr::cSC4PoliceSimulator_GetStrikingStations, reinterpret_cast<void*>(&GetStrikingStations)},
        {addr::cSC4PoliceSimulator_NearestStationTo, reinterpret_cast<void*>(&NearestStationTo)},
        {addr::cSC4PoliceSimulator_BeginStrike, reinterpret_cast<void*>(&BeginStrike)},
        {addr::cSC4PoliceSimulator_EndStrike, reinterpret_cast<void*>(&EndStrike)},
        {addr::cSC4PoliceSimulator_UpdateStationStrike, reinterpret_cast<void*>(&UpdateStationStrike)},
        {addr::cSC4PoliceSimulator_OnMonthStrikes, reinterpret_cast<void*>(&OnMonthStrikes)},
        {addr::cSC4PoliceSimulator_GetJailCapacityOf, reinterpret_cast<void*>(&GetJailCapacityOf)},
        {addr::cSC4PoliceSimulator_GetJailedPercentage, reinterpret_cast<void*>(&GetJailedPercentage)},
        {addr::cSC4PoliceSimulator_ResetUnjailedCriminals, reinterpret_cast<void*>(&ResetUnjailedCriminals)},
        {addr::cSC4PoliceSimulator_GetJailCurrentUpkeepCost, reinterpret_cast<void*>(&GetJailCurrentUpkeepCost)},
        {addr::cSC4PoliceSimulator_UpdateJails, reinterpret_cast<void*>(&UpdateJails)},
        {addr::cSC4PoliceSimulator_IsInJailbreak, reinterpret_cast<void*>(&IsInJailbreak)},
        {addr::cSC4PoliceSimulator_GetJailbreakChance, reinterpret_cast<void*>(&GetJailbreakChance)},
        {addr::cSC4PoliceSimulator_CheckJailbreaks, reinterpret_cast<void*>(&CheckJailbreaks)},
        {addr::cSC4PoliceSimulator_CheckJailbreakEnds, reinterpret_cast<void*>(&CheckJailbreakEnds)},
        {addr::cSC4PoliceSimulator_BeginJailbreak, reinterpret_cast<void*>(&BeginJailbreak)},
        {addr::cSC4PoliceSimulator_EndJailbreak, reinterpret_cast<void*>(&EndJailbreak)},
        {addr::cSC4PoliceSimulator_GetJailbreakBuildings, reinterpret_cast<void*>(&GetJailbreakBuildings)},
        {addr::cSC4PoliceSimulator_ProcessMonthlyCrimeStats, reinterpret_cast<void*>(&ProcessMonthlyCrimeStats)},
        {addr::cSC4PoliceSimulator_OnMonth, reinterpret_cast<void*>(&OnMonth)},
        {addr::cSC4PoliceSimulator_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4PoliceSimulator_SetUnitMask, reinterpret_cast<void*>(&SetUnitMask)},
        {addr::cSC4PoliceSimulator_SetFirstUnit, reinterpret_cast<void*>(&SetFirstUnit)},
        {addr::cSC4PoliceSimulator_GetStationFundingValue, reinterpret_cast<void*>(&GetStationFundingValue)},
        {addr::cSC4PoliceSimulator_GetSourceReach, reinterpret_cast<void*>(&GetSourceReach)},
        {addr::cSC4PoliceSimulator_GetStationInfo, reinterpret_cast<void*>(&GetStationInfo)},
        {addr::cSC4PoliceSimulator_OnStationChanged, reinterpret_cast<void*>(&OnStationChanged)},
        {addr::cSC4PoliceSimulator_OnStationAdded, reinterpret_cast<void*>(&OnStationAdded)},
        {addr::cSC4PoliceSimulator_OnStationRemoved, reinterpret_cast<void*>(&OnStationRemoved)},
        {addr::cSC4PoliceSimulator_OnWalkEnd, reinterpret_cast<void*>(&OnWalkEnd)},
        {addr::cSC4PoliceSimulator_IsUnitKind, reinterpret_cast<void*>(&IsUnitKind)},
        {addr::cSC4PoliceSimulator_GetUnits, reinterpret_cast<void*>(&GetUnits)},
        {addr::cSC4PoliceSimulator_GetUnitAppearance, reinterpret_cast<void*>(&GetUnitAppearance)},
        {addr::cSC4PoliceSimulator_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::cSC4PoliceSimulator_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4PoliceSimulator_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4PoliceSimulator_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4PoliceSimulator_Read, reinterpret_cast<void*>(&Read)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        if (Skipped(hooks[i].address))
            continue;
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "simulators.police.sim",
    "simulators/police",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PoliceSimulator_Init,
    "Our police simulator: stations, jails, crimes, jailbreaks, strikes, tuning, Init, Shutdown, Write, Read "
    "(docs/fire-police.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::police_sim
