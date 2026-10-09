// cSC4FireProtectionSimulator (class 0x2990C05A; docs/fire-police.md): the
// fire department on top of the shared protection base (protection_sim.cpp,
// at +8). The base keeps the stations, coverage and dispatch slots; this
// adds the stations' units and reach from their properties, efficiency from
// the budget, strikes, tuning and the fire plane dispatch source (+0xD0).
//
// `this` differs by interface: the cISC4FireProtectionSimulator slots get
// the object, the base's slots the object + 8 (named `base` here), its
// dispatch interface + 0xC, and its cIGZSerializable + 0x10. Offsets below
// are from the object.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::fire_sim {
namespace {

namespace field {
constexpr std::size_t kBase = 0x8;              // cSC4ProtectionSimulator
constexpr std::size_t kCity = 0x6C;             // base +0x64
constexpr std::size_t kMessageServer = 0x70;    // base +0x68
constexpr std::size_t kBudget = 0x74;           // base +0x6C
constexpr std::size_t kDepartment = 0x7C;       // base +0x74
constexpr std::size_t kCoverage = 0x80;         // base +0x78
constexpr std::size_t kPower = 0x84;            // base +0x7C
constexpr std::size_t kStations = 0x88;         // base +0x80
constexpr std::size_t kOnStrike = 0xA4;         // uint8, base +0x9C
constexpr std::size_t kStrikeDay = 0xA8;        // base +0xA0
constexpr std::size_t kLineItemKind = 0xB0;     // base +0xA8
constexpr std::size_t kInitialised = 0xC0;      // uint8
constexpr std::size_t kReferences = 0xC4;
constexpr std::size_t kLoaded = 0xC8;           // uint8: set by Read
constexpr std::size_t kService43D7C = 0xCC;
constexpr std::size_t kPlanes = 0xD0;           // the fire plane dispatch source
constexpr std::size_t kEfficiencyCurve = 0xD4;  // by funding x 100
constexpr std::size_t kDepartmentShare = 0xE4;  // float (percentage property)
constexpr std::size_t kStrikeFactor = 0xE8;     // float: efficiency on strike
constexpr std::size_t kStrikeEndFunding = 0xEC; // float
constexpr std::size_t kStrikeCurve = 0xF0;      // chance by funding x 100
constexpr std::size_t kUnfundedFactor = 0x100;  // float: lots not funded
constexpr std::size_t kStrikeDays = 0x104;      // int32
constexpr std::size_t kSerializable = 0x10;     // cIGZSerializable
} // namespace field

constexpr uint32_t kTuningGroup = 0xE7E2C2DB;
constexpr uint32_t kTuningInstance = 0x68F5524A;

using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using SetUint32Fn = uint32_t(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using SelfFn = uint32_t(__thiscall*)(void* self);
using ScaleFn = void(__thiscall*)(void* self, void* info, float factor);
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

uint32_t Today(char* fire)
{
    void* const simulator = Virtual<void*>(At<void*>(fire, field::kCity), 0xC8);
    return Virtual<uint32_t>(Virtual<void*>(simulator, 0x1C), 0x68);
}

// A message of the given type about `subject` (slots 12 and 33) posted.
void Post(char* fire, uint32_t type, void* subject, bool withSubject)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    if (withSubject) {
        Virtual<uint32_t>(message, 0x30, subject);
        Virtual<uint32_t>(message, 0x84, subject);
    }
    Virtual<uint32_t>(At<void*>(fire, field::kMessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// The station's funding from its budget line item (+0x1C of what budget
// slot 51 fills), or `fallback` when it has none.
uint32_t Funding(char* fire, void* station, uint32_t fallback)
{
    uint8_t item[0x20]; // the game sets only +0x1C
    At<uint32_t>(item, 0x1C) = 0;
    const uint32_t kind = At<uint32_t>(fire, field::kLineItemKind);
    void* const budget = At<void*>(fire, field::kBudget);
    if (Virtual<uint8_t>(budget, 0xCC, Properties(station), kind, item))
        return At<uint32_t>(item, 0x1C);
    return fallback;
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

// --- Identity and lifetime ---------------------------------------------------------

// 0x006ACED0 (slot 0; the +8, +0xC and +0x10 interfaces reach it through
// thunks).
uint8_t __fastcall QueryInterface(char* fire, void*, uint32_t iid, void** out)
{
    char* found;
    switch (iid) {
    case 1:
    case 0x88F54C7B:
        found = fire;
        break;
    case 0x452294AA:
        found = fire ? fire + 0x8 : nullptr;
        break;
    case 0xC9C00435:
        found = fire ? fire + 0xC : nullptr;
        break;
    case 0xE4FDA3D4:
        found = fire ? fire + 0x10 : nullptr;
        break;
    default:
        return 0;
    }
    *out = found;
    Virtual<uint32_t>(fire, 0x4); // AddRef
    return 1;
}

// 0x006AD1B0 (slot 2): at the last reference, Shutdown and the deleting
// destructor (through the base's slot 4).
uint32_t __fastcall ReleaseSelf(char* fire, void*)
{
    int32_t& references = At<int32_t>(fire, field::kReferences);
    if (references > 1)
        return static_cast<uint32_t>(--references);
    Virtual<uint8_t>(fire, 0x10);
    Virtual<void*>(fire + field::kBase, 0x10, 1u);
    return 0;
}

// 0x006ACEC0 (+0x10 slot 5).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    return 0x2990C05A;
}

// --- Tuning (0x006ADDA0) -----------------------------------------------------------

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

// Defaults, then the simulator tuning exemplar's curves and values.
void __fastcall LoadTuning(char* fire, void*)
{
    using SetPointsFn = uint8_t(__thiscall*)(void* curve, const PointVector* points);
    using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
    const auto setPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
    const auto loadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
    At<int32_t>(fire, field::kBase + 0x2C) = 2;  // block size
    At<int32_t>(fire, field::kBase + 0x30) = 50; // steps a walk
    PointVector efficiency{};
    PushPoint(efficiency, 0.0f, 0.0f);
    PushPoint(efficiency, 100.0f, 100.0f);
    PushPoint(efficiency, 120.0f, 110.0f);
    setPoints(fire + field::kEfficiencyCurve, &efficiency);
    At<float>(fire, field::kStrikeEndFunding) = 0.9f;
    At<float>(fire, field::kUnfundedFactor) = 0.8f;
    At<float>(fire, field::kDepartmentShare) = 1.2f;
    At<float>(fire, field::kStrikeFactor) = 0.5f;
    PointVector strikes{};
    PushPoint(strikes, 0.0f, 20.0f);
    PushPoint(strikes, 100.0f, 100.0f);
    setPoints(fire + field::kStrikeCurve, &strikes);
    At<int32_t>(fire, field::kStrikeDays) = 60;
    const uint32_t key[3] = {0x6534284A, kTuningGroup, kTuningInstance};
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        loadCurve(fire + field::kStrikeCurve, key, 0x29233E98);
        loadCurve(fire + field::kEfficiencyCurve, key, 0x29233E8D);
        GetPropertyPercentage(holder, 0x29233E8F, fire + field::kDepartmentShare);
        GetPropertyFloat(holder, 0x29233E94, fire + field::kStrikeFactor);
        GetPropertyPercentage(holder, 0x29233E96, fire + field::kStrikeEndFunding);
        GetPropertyFloat(holder, 0x69B425DB, fire + field::kUnfundedFactor);
        GetPropertySint32(holder, 0x29233E95, fire + field::kStrikeDays);
        Release(exemplar);
    }
    if (strikes.begin)
        Free(strikes.begin);
    if (efficiency.begin)
        Free(efficiency.begin);
}

// --- Init (0x006AE240) and Shutdown (0x006AD1E0) --------------------------------

uint8_t __fastcall Init(char* fire, void*)
{
    if (fire[field::kInitialised])
        return 1;
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("fire sim: first Init (%p)", static_cast<void*>(fire));
    }
    fire[field::kInitialised] = 1;
    At<uint32_t>(fire, field::kLineItemKind) = 0xEA567BC3;
    LoadTuning(fire, nullptr);
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_Init)(fire + field::kBase);
    if (!At<void*>(fire, field::kDepartment)) {
        void* const department = Virtual<void*>(At<void*>(fire, field::kBudget), 0x64, 0x28F55A9Fu, 0x4A357B40u);
        At<void*>(fire, field::kDepartment) = department;
        Virtual<uint32_t>(department, 0x4); // AddRef
        Virtual<uint32_t>(At<void*>(fire, field::kDepartment), 0x14, 0x6A3FF01Cu, 0xAA4935FEu);
        Virtual<uint32_t>(At<void*>(fire, field::kDepartment), 0x34, fire + field::kDepartmentShare);
        Virtual<uint32_t>(At<void*>(fire, field::kDepartment), 0x44, 1u, 1u);
        void* const part = Virtual<void*>(At<void*>(fire, field::kDepartment), 0x50, 1u);
        Virtual<uint32_t>(part, 0x48, 1u, 0u);
    }
    if (!At<void*>(fire, field::kService43D7C)) {
        void* const service = Global<void*>(addr::SC4_Service43D7C);
        At<void*>(fire, field::kService43D7C) = service;
        Virtual<uint32_t>(service, 0x4);
    }
    if (void* const grid = At<void*>(fire, field::kCoverage))
        Virtual<uint32_t>(grid, 0x18, 0x49D5B8F9u);
    if (void* const grid = At<void*>(fire, field::kPower))
        Virtual<uint32_t>(grid, 0x18, 0x89D5B91Au);
    if (!At<void*>(fire, field::kPlanes)) {
        using NewFn = void*(__cdecl*)(std::size_t size);
        using ConstructFn = void*(__thiscall*)(void* memory);
        void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x28);
        void* const planes = memory ? reinterpret_cast<ConstructFn>(addr::SC4FirePlaneSource_Construct)(memory) : nullptr;
        void* const old = At<void*>(fire, field::kPlanes);
        if (planes != old) {
            if (planes)
                Virtual<uint32_t>(planes, 0x4);
            At<void*>(fire, field::kPlanes) = planes;
            if (old)
                Release(old);
        }
    }
    if (void* const planes = At<void*>(fire, field::kPlanes))
        reinterpret_cast<SelfFn>(addr::SC4DispatchSource_Init)(planes);
    return 1;
}

uint8_t __fastcall Shutdown(char* fire, void*)
{
    if (!fire[field::kInitialised])
        return 1;
    fire[field::kInitialised] = 0;
    if (At<void*>(fire, field::kPlanes)) {
        reinterpret_cast<SelfFn>(addr::SC4DispatchSource_Shutdown)(At<void*>(fire, field::kPlanes));
        if (void* const planes = At<void*>(fire, field::kPlanes)) {
            At<void*>(fire, field::kPlanes) = nullptr;
            Release(planes);
        }
    }
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_Shutdown)(fire + field::kBase);
    for (const std::size_t at : {field::kService43D7C, field::kDepartment}) {
        if (void* const held = At<void*>(fire, at)) {
            Release(held);
            At<void*>(fire, at) = nullptr;
        }
    }
    return 1;
}

// --- Messages and the month ---------------------------------------------------------

// 0x006AE070 (base slot 3): the base's handler; the tuning exemplar's
// reload message reloads the tuning. Always handled.
uint8_t __fastcall DoMessage(char* base, void*, void* message)
{
    using DoMessageFn = uint8_t(__thiscall*)(void* self, void* message);
    if (!reinterpret_cast<DoMessageFn>(addr::cSC4ProtectionSimulator_DoMessage)(base, message) &&
        Virtual<uint32_t>(message, 0x10) == 0xC53D10AA) {
        const uint32_t group = Virtual<uint32_t>(message, 0x24);
        const uint32_t instance = Virtual<uint32_t>(message, 0x38);
        if (group == kTuningGroup && instance == kTuningInstance)
            LoadTuning(base - field::kBase, nullptr);
    }
    return 1;
}

// 0x006ACF70 (base slot 5): the grid scale 1, then the base's walk.
void __fastcall Step(char* base, void*, uint32_t argument)
{
    using StepFn = void(__thiscall*)(void* self, uint32_t argument);
    At<float>(base, 0x98) = 1.0f;
    reinterpret_cast<StepFn>(addr::cSC4ProtectionSimulator_Step)(base, argument);
}

// 0x006ADBC0: a station's strike (property 0xEAA69F83 = the day it began,
// 0 for none). A strike ends after +0x104 days, or once the station is
// funded at +0xEC or more; one begins when a draw of 0-100 falls under the
// strike curve at the funding. A change is stored and told to the city's
// slot 47 object; whether there was one.
uint8_t __fastcall UpdateStationStrike(char* fire, void*, void* station)
{
    using RandomFn = double(__thiscall*)(void* random, double low, double high);
    const uint32_t today = Today(fire);
    uint32_t began = 0;
    GetPropertyUint32(Properties(station), 0xEAA69F83, &began);
    uint32_t now = began;
    if (began != 0) {
        if (today - began > At<uint32_t>(fire, field::kStrikeDays)) {
            now = 0;
        } else {
            const float funding = FloatOf(Funding(fire, station, Global<uint32_t>(addr::SC4_One)));
            if (!(funding < At<float>(fire, field::kStrikeEndFunding)))
                now = 0;
        }
    } else {
        const float funding = FloatOf(Funding(fire, station, Global<uint32_t>(addr::SC4_One)));
        const auto chance = static_cast<float>(
            EvaluateCurve(fire + field::kStrikeCurve, static_cast<float>(static_cast<double>(funding) * 100.0)));
        const double draw =
            reinterpret_cast<RandomFn>(addr::SC4Random_Uniform)(fire + field::kBase + 0x10, 0.0, 100.0);
        if (draw < chance)
            now = today;
    }
    if (now == began)
        return 0;
    SetPropertyUint32(Properties(station), 0xEAA69F83, now);
    if (void* const news = Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC)) {
        struct GameString {
            std::uintptr_t vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t zero;
        } text{};
        using FromTextFn = void(__thiscall*)(GameString* text, const char* value);
        reinterpret_cast<FromTextFn>(addr::SC4String_FromText)(&text, Global<const char*>(addr::SC4_DefaultText));
        if (now)
            Virtual<uint32_t>(news, 0x28, &text, station, 0u);
        else
            Virtual<uint32_t>(news, 0x30, &text, station);
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            Free(text.begin);
    }
    return 1;
}

// 0x006AE0C0 (base slot 6): every station's strike updated; once one
// changed, stations on strike are told (slot 13); with none on strike a
// strike in progress ends (slot 14); then the coverage is painted again.
void __fastcall OnMonth(char* base, void*)
{
    char* const fire = base - field::kBase;
    char* const head = At<char*>(base, 0x80);
    uint8_t changed = 0;
    int32_t striking = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const station = At<void*>(node, 8);
        if (UpdateStationStrike(fire, nullptr, station))
            changed = 1;
        if (Virtual<uint8_t>(fire, 0x48, station)) {
            ++striking;
            if (changed)
                Virtual<uint8_t>(fire, 0x34, station);
        }
    }
    if (!changed)
        return;
    if (striking == 0 && Virtual<uint8_t>(fire, 0x2C))
        Virtual<uint8_t>(fire, 0x38);
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_PaintCoverage)(base);
}

// 0x006AD320 (base slot 17, the walk's end): the share and average to the
// monthly figures, the share averaged with the police simulator's (its slot 19), and message
// 0x68F54CE0 about the simulator.
void __fastcall OnWalkEnd(char* base, void*)
{
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    const float share = At<float>(base, 0x94);
    Virtual<uint32_t>(figures, 0x30, 0x2A11D203u, static_cast<int32_t>(static_cast<double>(share) * 100.0));
    Virtual<uint32_t>(figures, 0x30, 0x2A11D204u, static_cast<int32_t>(At<float>(base, 0x90)));
    const auto percent = static_cast<float>(static_cast<double>(share) * 100.0);
    const float* const other = Virtual<float*>(Global<void*>(addr::SC4_PoliceSimulator), 0x4C);
    Virtual<uint32_t>(figures, 0x30, 0xAAAD4D87u,
                      static_cast<int32_t>((static_cast<double>(*other) * 100.0 + percent) * 0.5));
    char* const fire = base - field::kBase;
    void* self = nullptr;
    Virtual<uint8_t>(fire, 0x0, 1u, &self);
    Post(fire, 0x68F54CE0, self, true);
    if (self)
        Release(self);
}

// --- Strikes -------------------------------------------------------------------------

// 0x006AD060 (slot 13).
uint8_t __fastcall BeginStrike(char* fire, void*, void* station)
{
    if (fire[field::kOnStrike])
        return 0;
    fire[field::kOnStrike] = 1;
    At<uint32_t>(fire, field::kStrikeDay) = Today(fire);
    Post(fire, 0x68F54CE1, station, true);
    return 1;
}

// 0x006AD100 (slot 14).
uint8_t __fastcall EndStrike(char* fire, void*)
{
    if (!fire[field::kOnStrike])
        return 0;
    fire[field::kOnStrike] = 0;
    At<uint32_t>(fire, field::kStrikeDay) = 0;
    Post(fire, 0x68F54CE2, nullptr, false);
    return 1;
}

// 0x006AD950 (slot 12): 1 on strike; else each station's chance (the strike
// curve at its funding, as a fraction) combined.
float* __fastcall GetChanceOfStrike(char* fire, void*, float* out)
{
    if (fire[field::kOnStrike]) {
        At<uint32_t>(out, 0) = Global<uint32_t>(addr::SC4_One);
        return out;
    }
    float total = Global<float>(addr::SC4_Zero);
    char* const head = At<char*>(fire, field::kStations);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        const float funding = FloatOf(Funding(fire, At<void*>(node, 8), Global<uint32_t>(addr::SC4_One)));
        const double chance =
            EvaluateCurve(fire + field::kStrikeCurve, static_cast<float>(static_cast<double>(funding) * 100.0)) *
            static_cast<double>(0.01f);
        total = static_cast<float>((total + chance) - total * chance);
    }
    *out = total;
    return out;
}

// 0x006AE170 (slot 17): the stations on strike appended to the caller's
// list, held; how many.
int32_t __fastcall GetStrikingStations(char* fire, void*, void** list)
{
    int32_t count = 0;
    char* const head = At<char*>(fire, field::kStations);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const station = At<void*>(node, 8);
        if (!Virtual<uint8_t>(fire, 0x48, station))
            continue;
        Virtual<uint32_t>(station, 0x4); // AddRef
        char* const end = static_cast<char*>(*list);
        auto* const added = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
        At<void*>(added, 8) = station;
        char* const last = At<char*>(end, 4);
        At<char*>(added, 0) = end;
        At<char*>(added, 4) = last;
        At<char*>(last, 0) = added;
        At<char*>(end, 4) = added;
        ++count;
    }
    return count;
}

// --- Stations --------------------------------------------------------------------

// 0x006ACF80 (slot 7; `this` unused).
uint8_t __fastcall IsFireStation(void*, void*, void* occupant)
{
    return Virtual<uint8_t>(Properties(occupant), 0xC, 0x08F55DC2u);
}

// 0x006AD530 (slot 9; police slot 8): the funding (by reference) given to
// the station's line item (slot 24, SetLocalFundingPercent).
uint8_t __fastcall SetStationFunding(char* fire, void*, void* station, const float* funding)
{
    uint8_t item[0x20];
    At<uint32_t>(item, 0x1C) = 0;
    const uint32_t kind = At<uint32_t>(fire, field::kLineItemKind);
    if (!Virtual<uint8_t>(At<void*>(fire, field::kBudget), 0xCC, Properties(station), kind, item))
        return 0;
    void* const line = Virtual<void*>(At<void*>(fire, field::kBudget), 0x70, At<uint32_t>(item, 0));
    if (!line)
        return 0;
    Virtual<uint32_t>(line, 0x60, Properties(station), funding, At<uint32_t>(fire, field::kLineItemKind));
    return 1;
}

// 0x006AD430 (slot 10): the efficiency curve at the station's funding (its
// line item's when the line has it (slot 28), else the line's default
// (slot 10), 1 without a line, 0 without an item), as a fraction; less for
// a lot not funded and on strike.
float* __fastcall GetStationEfficiency(char* fire, void*, float* out, void* station)
{
    void* const properties = Properties(station);
    uint8_t item[0x20];
    At<uint32_t>(item, 0x1C) = 0;
    uint32_t funding = 0;
    const uint32_t kind = At<uint32_t>(fire, field::kLineItemKind);
    if (Virtual<uint8_t>(At<void*>(fire, field::kBudget), 0xCC, properties, kind, item)) {
        void* const line = Virtual<void*>(At<void*>(fire, field::kBudget), 0x70, At<uint32_t>(item, 0));
        if (!line)
            funding = Global<uint32_t>(addr::SC4_One);
        else if (Virtual<uint8_t>(line, 0x70, properties, At<uint32_t>(fire, field::kLineItemKind)))
            funding = At<uint32_t>(item, 0x1C);
        else
            funding = *Virtual<uint32_t*>(line, 0x28);
    }
    auto efficiency = static_cast<float>(
        EvaluateCurve(fire + field::kEfficiencyCurve, static_cast<float>(static_cast<double>(FloatOf(funding)) * 100.0)) *
        static_cast<double>(0.01f));
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, station))
        if (!Virtual<uint8_t>(lot, 0x70))
            efficiency = static_cast<float>(static_cast<double>(efficiency) * At<float>(fire, field::kUnfundedFactor));
    double result = efficiency;
    if (Virtual<uint8_t>(fire, 0x48, station))
        result *= At<float>(fire, field::kStrikeFactor);
    *out = static_cast<float>(result);
    return out;
}

// 0x006AD5E0 (slot 21): a station's coverage at a distance (x its info's
// +0x14): its strength inside +0x10, nothing past +0xC, linear between.
uint8_t __fastcall GetOccupantCoverage(char* fire, void*, void* station, const float* distance, uint32_t* coverage,
                                       uint32_t* radius)
{
    *coverage = 0;
    *radius = 0;
    uint32_t info[6];
    if (!Virtual<uint8_t>(fire + field::kBase, 0x38, station, info))
        return 0;
    *radius = info[2];
    const auto reach = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(info[5]) * *distance));
    if (reach > info[3]) {
        *coverage = 0;
        return 1;
    }
    if (reach < info[4]) {
        *coverage = info[2];
        return 1;
    }
    const double part = static_cast<double>(info[3] - reach) * FloatOf(info[2]);
    *coverage = BitsOf(static_cast<float>(part / static_cast<double>(info[3] - info[4])));
    return 1;
}

// 0x006ADA30 (slot 23).
int32_t __fastcall GetFireStationCount(char* fire, void*)
{
    char* const head = At<char*>(fire, field::kStations);
    int32_t count = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        ++count;
    return count;
}

// 0x006AD8F0 (slot 25): the stations' units (property 0x08F55DC5) summed.
uint32_t __fastcall GetTotalDispatchCount(char* fire, void*)
{
    uint32_t total = 0;
    for (char* node = At<char*>(At<char*>(fire, field::kStations), 0); node != At<char*>(fire, field::kStations);
         node = At<char*>(node, 0)) {
        uint32_t units = 0;
        if (GetPropertyUint32(Properties(At<void*>(node, 8)), 0x08F55DC5, &units))
            total += units;
    }
    return total;
}

// --- The base's slots ---------------------------------------------------------------

// 0x006AD190 (base slot 10) and 0x006AD170 (slot 15): a station's unit mask
// and first dispatch slot.
uint32_t __fastcall SetUnitMask(void*, void*, void* station, uint32_t mask)
{
    return SetPropertyUint32(Properties(station), 0x69B425DE, mask);
}

uint32_t __fastcall SetFirstUnit(void*, void*, void* station, uint32_t first)
{
    return SetPropertyUint32(Properties(station), 0x69B425DD, first);
}

// 0x006AD6B0 (base slot 11): the line item's +0x10, if asked for.
uint8_t __fastcall GetStationFundingValue(char* base, void*, void* station, uint32_t* out)
{
    Properties(station);
    if (!out)
        return 1;
    uint8_t item[0x20];
    At<uint32_t>(item, 0x1C) = 0;
    const uint32_t kind = At<uint32_t>(base, 0xA8);
    if (!Virtual<uint8_t>(At<void*>(base, 0x6C), 0xCC, Properties(station), kind, item))
        return 0;
    *out = At<uint32_t>(item, 0x10);
    return 1;
}

// 0x006AD720 (base slot 12): a dispatch source's reach (properties
// 0x29233E9A outer, 0x29233E9B inner, 0x29233E9C strength), scaled by its
// efficiency (slot 10).
uint8_t __fastcall GetSourceReach(char* base, void*, void* source, char* info)
{
    void* const properties = Properties(source);
    At<uint32_t>(info, 0) = 0;
    At<uint32_t>(info, 4) = 0;
    At<int32_t>(info, 0xC) = 0;
    GetPropertySint32(properties, 0x29233E9A, info + 0xC);
    if (At<int32_t>(info, 0xC) == 0)
        return 0;
    GetPropertySint32(properties, 0x29233E9B, info + 0x10);
    GetPropertyFloat(properties, 0x29233E9C, info + 8);
    At<int32_t>(info, 0x14) = At<int32_t>(info, 0xC);
    auto slot = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(source)); // the game's out slot
    const float* const efficiency = Virtual<float*>(base - field::kBase, 0x28, &slot, source);
    ScaleDispatchInfo(base, info, *efficiency);
    return 1;
}

// 0x006AD7C0 (base slot 14): a station's position and reach (properties
// 0x08F55DC0 outer, 0x08F55DC1 inner, 0x08F55DC2 strength), scaled by the
// department's funding curve for an occupant with interface 0xE9793A65,
// else by the station's efficiency (slot 10).
uint8_t __fastcall GetStationInfo(char* base, void*, void* station, char* info)
{
    void* const properties = Properties(station);
    At<int32_t>(info, 0xC) = 0;
    At<int32_t>(info, 0x10) = 0;
    At<uint32_t>(info, 8) = 0;
    uint8_t ok = GetPropertySint32(properties, 0x08F55DC0, info + 0xC);
    GetPropertySint32(properties, 0x08F55DC1, info + 0x10);
    if (!GetPropertyFloat(properties, 0x08F55DC2, info + 8))
        ok = 0;
    float position[3];
    Virtual<uint32_t>(station, 0x20, position);
    At<int32_t>(info, 0x14) = At<int32_t>(info, 0xC);
    At<float>(info, 0) = position[0];
    At<float>(info, 4) = position[2];
    if (!ok)
        return 0;
    void* other = nullptr;
    float factor;
    if (Virtual<uint8_t>(station, 0x0, 0xE9793A65u, &other)) {
        const float* const funding = Virtual<float*>(At<void*>(base, 0x74), 0x28);
        factor = static_cast<float>(
            EvaluateCurve(base + 0xCC, static_cast<float>(static_cast<double>(*funding) * 100.0)) *
            static_cast<double>(0.01f));
    } else {
        uint32_t slot = 0;
        factor = *Virtual<float*>(base - field::kBase, 0x28, &slot, station);
    }
    ScaleDispatchInfo(base, info, factor);
    if (other)
        Release(other);
    return 1;
}

// --- The dispatch interface (+0xC) --------------------------------------------------

// 0x006ACFA0 (+0xC slot 3).
uint32_t __fastcall IsUnitKind(void*, void*, uint32_t kind)
{
    return kind == 1;
}

// 0x006ACFB0 (+0xC slot 7): a station's units (property 0x08F55DC5), the
// mask of those at home (0x69B425DE, cut to that many bits) and its first
// dispatch slot (0x69B425DD); each only if asked for.
uint8_t __fastcall GetUnits(void*, void*, void* station, uint32_t* mask, uint32_t* first, uint32_t* count)
{
    void* const properties = Properties(station);
    uint32_t units = 0;
    if (!GetPropertyUint32(properties, 0x08F55DC5, &units))
        return 0;
    if (count)
        *count = units;
    if (mask) {
        uint32_t bits = 0;
        if (!GetPropertyUint32(properties, 0x69B425DE, &bits))
            return 0;
        *mask = bits & ((1u << (units & 31)) - 1);
    }
    if (first) {
        uint32_t slot = 0;
        if (!GetPropertyUint32(properties, 0x69B425DD, &slot))
            return 0;
        *first = slot;
    }
    return 1;
}

// 0x006AD5B0 (+0xC slot 11).
uint8_t __fastcall GetUnitAppearance(void*, void*, uint32_t, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t)
{
    *a = 0x4202;
    b[0] = 0;
    b[1] = 0;
    b[2] = 0;
    *c = 0x11882;
    return 1;
}

// --- Write (0x006AD250) and Read (0x006AD2A0), this = +0x10 -------------------------

uint32_t StreamOk(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) == 0;
}

uint32_t __fastcall Write(char* serializable, void*, void* stream)
{
    using BaseFn = uint32_t(__thiscall*)(void* self, void* stream);
    using WriteObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
    Virtual<uint8_t>(stream, 0x1C, uint16_t{3}); // version
    reinterpret_cast<BaseFn>(addr::cSC4ProtectionSimulator_Write)(serializable, stream);
    char* const planes = At<char*>(serializable, field::kPlanes - field::kSerializable);
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference)(stream, planes ? planes + 8 : nullptr);
    return StreamOk(stream);
}

uint32_t __fastcall Read(char* serializable, void*, void* stream)
{
    using BaseFn = uint32_t(__thiscall*)(void* self, void* stream);
    using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version > 3)
        return 0;
    serializable[field::kLoaded - field::kSerializable] = 1;
    reinterpret_cast<BaseFn>(addr::cSC4ProtectionSimulator_Read)(serializable, stream);
    if (version > 2) {
        void*& planes = At<void*>(serializable, field::kPlanes - field::kSerializable);
        if (void* const old = planes) {
            planes = nullptr;
            Release(old);
        }
        reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference)(stream, 0xC9C00435, &planes, 0);
    }
    return StreamOk(stream);
}

// Hooks to leave out, for bisecting: OPENSC4_FIRE_SKIP lists function
// addresses (hex).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_FIRE_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[35] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4FireProtectionSimulator_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4FireProtectionSimulator_Release, reinterpret_cast<void*>(&ReleaseSelf)},
        {addr::cSC4FireProtectionSimulator_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
        {addr::cSC4FireProtectionSimulator_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::cSC4FireProtectionSimulator_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4FireProtectionSimulator_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4FireProtectionSimulator_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4FireProtectionSimulator_Step, reinterpret_cast<void*>(&Step)},
        {addr::cSC4FireProtectionSimulator_UpdateStationStrike, reinterpret_cast<void*>(&UpdateStationStrike)},
        {addr::cSC4FireProtectionSimulator_OnMonth, reinterpret_cast<void*>(&OnMonth)},
        {addr::cSC4FireProtectionSimulator_OnWalkEnd, reinterpret_cast<void*>(&OnWalkEnd)},
        {addr::cSC4FireProtectionSimulator_BeginStrike, reinterpret_cast<void*>(&BeginStrike)},
        {addr::cSC4FireProtectionSimulator_EndStrike, reinterpret_cast<void*>(&EndStrike)},
        {addr::cSC4FireProtectionSimulator_GetChanceOfStrike, reinterpret_cast<void*>(&GetChanceOfStrike)},
        {addr::cSC4FireProtectionSimulator_GetStrikingStations, reinterpret_cast<void*>(&GetStrikingStations)},
        {addr::cSC4FireProtectionSimulator_IsFireStation, reinterpret_cast<void*>(&IsFireStation)},
        {addr::cSC4FireProtectionSimulator_SetStationFunding, reinterpret_cast<void*>(&SetStationFunding)},
        {addr::cSC4FireProtectionSimulator_GetStationEfficiency, reinterpret_cast<void*>(&GetStationEfficiency)},
        {addr::cSC4FireProtectionSimulator_GetOccupantCoverage, reinterpret_cast<void*>(&GetOccupantCoverage)},
        {addr::cSC4FireProtectionSimulator_GetFireStationCount, reinterpret_cast<void*>(&GetFireStationCount)},
        {addr::cSC4FireProtectionSimulator_GetTotalDispatchCount, reinterpret_cast<void*>(&GetTotalDispatchCount)},
        {addr::cSC4FireProtectionSimulator_SetUnitMask, reinterpret_cast<void*>(&SetUnitMask)},
        {addr::cSC4FireProtectionSimulator_SetFirstUnit, reinterpret_cast<void*>(&SetFirstUnit)},
        {addr::cSC4FireProtectionSimulator_GetStationFundingValue, reinterpret_cast<void*>(&GetStationFundingValue)},
        {addr::cSC4FireProtectionSimulator_GetSourceReach, reinterpret_cast<void*>(&GetSourceReach)},
        {addr::cSC4FireProtectionSimulator_GetStationInfo, reinterpret_cast<void*>(&GetStationInfo)},
        {addr::cSC4FireProtectionSimulator_IsUnitKind, reinterpret_cast<void*>(&IsUnitKind)},
        {addr::cSC4FireProtectionSimulator_GetUnits, reinterpret_cast<void*>(&GetUnits)},
        {addr::cSC4FireProtectionSimulator_GetUnitAppearance, reinterpret_cast<void*>(&GetUnitAppearance)},
        {addr::cSC4FireProtectionSimulator_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4FireProtectionSimulator_Read, reinterpret_cast<void*>(&Read)},
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
    "simulators.fire.sim",
    "simulators/fire",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FireProtectionSimulator_Init,
    "Our fire simulator: stations, strikes, efficiency, tuning, Init, Shutdown, Write, Read (docs/fire-police.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::fire_sim
