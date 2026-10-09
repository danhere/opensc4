// Shared between the parts of our cSC4CrimeSimulator (build 641;
// docs/crime.md): the object's fields, the incident and crime type records,
// and the game's helpers they call.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::crime_sim {

// Fields of cSC4CrimeSimulator (offsets from the object's start; 0x174 bytes).
namespace field {
constexpr std::size_t kInitialized = 0x10;    // uint8
constexpr std::size_t kLoaded = 0x11;         // uint8: set by Read
constexpr std::size_t kRefCount = 0x14;
constexpr std::size_t kTractIterator = 0x18;  // the tract walk's rectangle iterator (written as one record)
constexpr std::size_t kCity = 0x48;
constexpr std::size_t kMessageServer = 0x4C;
constexpr std::size_t kOccupantManager = 0x50;
constexpr std::size_t kOrdinances = 0x54;     // the service at 0x00B43D90
constexpr std::size_t kPolice = 0x58;         // cISC4PoliceSimulator*
constexpr std::size_t kCrimeGrid = 0x5C;      // cISC4SimGrid<uint8_t>*
constexpr std::size_t kEffectGrid = 0x60;     // cISC4SimGrid<uint8_t>*: buildings' crime effect (0xCA5B9306)
constexpr std::size_t kSaved64 = 0x64;        // uint32, saved, unused otherwise
constexpr std::size_t kSaved68 = 0x68;        // uint32, saved, unused otherwise
constexpr std::size_t kAverageCrime = 0x6C;   // float
constexpr std::size_t kIncidents = 0x70;      // vector of Incident*: begin, end, capacity
constexpr std::size_t kMonthlyCrimes = 0x7C;
constexpr std::size_t kHotspotX = 0x80;
constexpr std::size_t kHotspotZ = 0x84;
constexpr std::size_t kHotspotValue = 0x88;   // int32
constexpr std::size_t kOrdinanceFactor = 0x8C; // float
constexpr std::size_t kState = 0x90;          // uint8: the step's state, 6 = waiting for the month
constexpr std::size_t kExpected = 0x94;       // uint32: the month's expected crimes
constexpr std::size_t kTarget = 0x98;         // the month's crimes to place
constexpr std::size_t kCounts = 0x9C;         // uint32[3][4]: people by group and land value quarter
constexpr std::size_t kFactors = 0xCC;        // float[3]: demand factors by group
constexpr std::size_t kGridCellSize = 0xD8;   // tuning 0x47E2C200
constexpr std::size_t kCurveDC = 0xDC;        // curve 0x49A34301
constexpr std::size_t kQuarterCurves = 0xEC;  // 3 curves, 0x8A0CBAA4 to 0x8A0CBAA6
constexpr std::size_t kFactorCurves = 0x11C;  // 3 curves, 0x8A0CBAA7 to 0x8A0CBAA9
constexpr std::size_t kChanceCurve = 0x14C;   // curve 0x8A0CBAAA: police minus effect -> threshold
constexpr std::size_t kExcessCurve = 0x15C;   // curve 0x8A0CBAAB: crimes over the criminals
constexpr std::size_t kDecay = 0x16C;         // float, 0x8A0CBAAC
constexpr std::size_t kUnjailedFactor = 0x170; // float, 0x8A0CBAAD
} // namespace field

// An incident of the month (0x2C bytes, SC4::operator_new).
struct Incident {
    void* occupant;       // the lot, referenced
    uint32_t crimeType;
    const void* nameVtable; // the crime's name: an SC4String (vtable, buffer begin, end, capacity, 0)
    char* nameBegin;
    char* nameEnd;
    char* nameCapacity;
    uint32_t nameZero;
    uint32_t key[3];      // type, group, instance
    uint8_t visualsCreated;
};
static_assert(offsetof(Incident, key) == 0x1C);
static_assert(offsetof(Incident, visualsCreated) == 0x28);

// A crime type (0x3C bytes, SC4::operator_new), from the crime exemplars;
// the list is global (0x00B48620), loaded once.
struct CrimeType {
    uint32_t id;          // the exemplar's instance
    const void* nameVtable; // SC4String
    char* nameBegin;
    char* nameEnd;
    char* nameCapacity;
    uint32_t nameZero;
    uint32_t masks[4];    // which lots it can happen on (LotMasks): 0xAA12FCB2, B3, B4, B7
    uint32_t key[3];      // 0xAA12FCB1
    uint8_t strength;     // 0xAA12FCB5, default 0x40
    uint8_t notify;       // 0xAA12FCB6
    uint8_t pad[2];
    float weight;         // 0xAA12FCB8, default 10
};
static_assert(offsetof(CrimeType, masks) == 0x18);
static_assert(offsetof(CrimeType, key) == 0x28);
static_assert(offsetof(CrimeType, strength) == 0x34);
static_assert(offsetof(CrimeType, weight) == 0x38);
static_assert(sizeof(CrimeType) == 0x3C);

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

using OperatorNewFn = void*(__cdecl*)(std::size_t size);
using OperatorDeleteFn = void(__cdecl*)(void* p);
using FreeFn = void(__cdecl*)(void* p);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using RandomUnitFn = double(__thiscall*)(void* random);
using RandomRangeFn = double(__thiscall*)(void* random, double low, double high);
using MessageFn = void*(__thiscall*)(void* memory);
using PtrInsertFn = void(__thiscall*)(void* vector, void* where, const void* value, void* allocator, uint32_t count,
                                      uint32_t atEnd);

inline const auto OperatorNew = reinterpret_cast<OperatorNewFn>(addr::SC4_operator_new);
inline const auto OperatorDelete = reinterpret_cast<OperatorDeleteFn>(addr::SC4_operator_delete);
inline const auto StlAllocate = reinterpret_cast<OperatorNewFn>(addr::SC4_stl_allocate);
inline const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
inline const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
inline const auto RandomUnit = reinterpret_cast<RandomUnitFn>(addr::SC4Random_Unit);
inline const auto RandomRange = reinterpret_cast<RandomRangeFn>(addr::cRZRandom_Range);
inline const auto PtrInsert = reinterpret_cast<PtrInsertFn>(addr::SC4PtrVector_InsertN);

// __ftol2: truncation toward zero, the low 32 bits.
inline int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// A new cGZMessage (RZ operator new, its constructor), referenced.
inline void* NewMessage()
{
    void* const memory = reinterpret_cast<OperatorNewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    return message;
}

// push_back of a pointer, as the game's inline code: in place when there is
// room, else through SC4PtrVector::InsertN.
inline void PushPointer(Vector* vector, void* value)
{
    if (vector->end != vector->capacity) {
        if (vector->end)
            *reinterpret_cast<void**>(vector->end) = value;
        vector->end += sizeof(void*);
        return;
    }
    char allocator;
    PtrInsert(vector, vector->end, &value, &allocator, 1, 1);
}

inline CrimeType* const* CrimeTypes(uint32_t* count)
{
    const auto& types = Global<Vector>(addr::cSC4CrimeSimulator_CrimeTypes);
    *count = static_cast<uint32_t>((types.end - types.begin) >> 2);
    return reinterpret_cast<CrimeType* const*>(types.begin);
}

void FirstCall(const char* name, bool& logged);

} // namespace opensc4::crime_sim
