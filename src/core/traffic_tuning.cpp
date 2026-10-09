// Our cSC4TrafficSimulator, tuning part: LoadTuning (0x00716AE0) reads the
// traffic exemplar into the simulator, the link cost tables and the travel
// time curve (docs/traffic.md, "Tuning"). The vectors it reads properties
// into are the game's own (filled by the game's property getters, grown by
// the game's vector code, freed in the game's order), so the heap sees the
// same allocations as with the game's function.
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
#include <cstring>
#include <initializer_list>
#include <vector>

namespace opensc4::traffic_sim {
namespace {

// std::vector as the game lays it out.
template <typename T>
struct GameVector {
    T* begin;
    T* end;
    T* capacity;
    uint32_t Size() const { return static_cast<uint32_t>(end - begin); }
};

// std::vector<bool>: words and bit offsets of both ends, then capacity.
struct BitVector {
    uint32_t* start;
    int32_t startBit;
    uint32_t* end;
    int32_t endBit;
    uint32_t* capacity;
    uint32_t Size() const { return static_cast<uint32_t>((end - start) * 32 - startBit + endBit); }
    bool Bit(int32_t index) const
    {
        const int32_t at = startBit + index;
        return (start[at / 32] & (1u << (at % 32))) != 0;
    }
};

using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetFloatsFn = uint8_t(__cdecl*)(void* holder, uint32_t property, GameVector<float>* values);
using GetBoolsFn = uint8_t(__cdecl*)(void* holder, uint32_t property, BitVector* values);
using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
using LoadCurvePropertyFn = uint8_t(__thiscall*)(void* curve, void* property);
using SetPointsFn = uint8_t(__thiscall*)(void* curve, const GameVector<float>* points);
using GetPointsFn = void(__thiscall*)(void* curve, GameVector<float>* points);
using InsertPointsFn = void(__thiscall*)(GameVector<float>* points, float* where, const float* value, void* allocator,
                                         uint32_t count, uint32_t atEnd);
using InsertFloatsFn = void(__thiscall*)(GameVector<float>* values, float* where, uint32_t count, const float* value);
using FreeFn = void(__cdecl*)(void* p);
using SelfFn = void(__thiscall*)(void* self);

const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
const auto GetPercentage = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyPercentage);
const auto GetUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
const auto GetFloats = reinterpret_cast<GetFloatsFn>(addr::SC4_GetPropertyFloatVector);
const auto GetBools = reinterpret_cast<GetBoolsFn>(addr::SC4_GetPropertyBoolVector);
const auto LoadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
const auto LoadCurveProperty = reinterpret_cast<LoadCurvePropertyFn>(addr::SC4Curve_LoadFromProperty);
const auto SetPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
const auto GetPoints = reinterpret_cast<GetPointsFn>(addr::SC4Curve_GetPoints);
const auto InsertPoints = reinterpret_cast<InsertPointsFn>(addr::SC4PointVector_InsertN);
const auto InsertFloats = reinterpret_cast<InsertFloatsFn>(addr::SC4FloatVector_InsertN);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr uint32_t kExemplarInterface = 0xA52160F5;
constexpr int kTravelTypes = 9;
constexpr int kBuildingTypes = 13;
constexpr float kFloatMax = 3.40282347e+38f;

// Fields LoadTuning writes (offsets from the simulator's start).
namespace field {
constexpr std::size_t kSpeedScale = 0x974;       // float
constexpr std::size_t kUnknown978 = 0x978;       // float
constexpr std::size_t kUnknown97C = 0x97C;       // float
constexpr std::size_t kLinkSpeeds = 0x980;       // float[9][13]
constexpr std::size_t kUnknownB54 = 0xB54;       // float[13]
constexpr std::size_t kUnknownB88 = 0xB88;       // float, a percentage
constexpr std::size_t kUnknownB8C = 0xB8C;       // float, a percentage
constexpr std::size_t kUnknownB90 = 0xB90;       // float
constexpr std::size_t kUnknownBA4 = 0xBA4;       // 9 words
constexpr std::size_t kUnknownBCC = 0xBCC;       // float[13], then their reciprocals at 0xC00
constexpr std::size_t kUnknownC34 = 0xC34;       // bool[9]
constexpr std::size_t kUnknownC3D = 0xC3D;       // bool[9]
constexpr std::size_t kUnknownC48 = 0xC48;       // uint32[3]
constexpr std::size_t kTripScale = 0xC54;        // float, then 255 / (last travel time point - 1)
constexpr std::size_t kScaleCount = 0xC68;       // uint8
constexpr std::size_t kScales = 0xC6C;           // vector<float>: reciprocals of 0xA92356BB
constexpr std::size_t kUnknownC78 = 0xC78;       // 9 words each at 0xC78, 0xC9C, 0xCC0
constexpr std::size_t kUnknownCE4 = 0xCE4;       // float
constexpr std::size_t kUnknownCE8 = 0xCE8;       // float[3]
constexpr std::size_t kRailAccidentChance = 0xD04;
constexpr std::size_t kCurveD10 = 0xD10;         // SC4Curve (16 bytes) each at 0xD10, 0xD20, 0xD30, 0xD40
constexpr std::size_t kUnknownD50 = 0xD50;       // float
constexpr std::size_t kUnknownD54 = 0xD54;       // float
} // namespace field

// push_back of an (x, y) point, as the game's inlined one: in place when
// there is room, else through the game's vector growth.
void PushPoint(GameVector<float>& points, float x, float y)
{
    const float value[2] = {x, y};
    if (points.end == points.capacity) {
        uint8_t allocator = 0;
        InsertPoints(&points, points.end, value, &allocator, 1, 1);
        return;
    }
    points.end[0] = x;
    points.end[1] = y;
    points.end += 2;
}

// A curve from its property, or from default points when the exemplar has
// none.
void LoadCurveOrDefault(void* holder, void* curve, uint32_t property, std::initializer_list<float> defaults)
{
    if (void* const found = Virtual<void*>(holder, 0x24, property)) {
        LoadCurveProperty(curve, found);
        return;
    }
    GameVector<float> points{};
    for (auto at = defaults.begin(); at != defaults.end(); at += 2)
        PushPoint(points, at[0], at[1]);
    SetPoints(curve, &points);
    if (points.begin)
        Free(points.begin);
}

// Nine words from a vector, 0 past its end.
void CopyWords(const GameVector<float>& values, void* to)
{
    for (uint32_t i = 0; i < kTravelTypes; ++i)
        static_cast<uint32_t*>(to)[i] = i < values.Size() ? reinterpret_cast<uint32_t*>(values.begin)[i] : 0;
}

// Nine flags from a bit vector, true past its end.
void CopyBits(const BitVector& values, uint8_t* to)
{
    for (int32_t i = 0; i < kTravelTypes; ++i)
        to[i] = static_cast<uint32_t>(i) < values.Size() ? values.Bit(i) : 1;
}

void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("traffic: first LoadTuning");
    }
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xC9133286};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &exemplar, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    GetFloat(holder, 0x491332E6, &At<float>(self, field::kSpeedScale));

    // Link speeds by travel type and building type, and the cost tables
    // (their reciprocals); FLT_MAX for no link.
    GameVector<float> speeds{};
    for (int type = 0; type < kTravelTypes; ++type) {
        float* const own = &At<float>(self, field::kLinkSpeeds + 0x34 * type);
        float* const table = &Global<float>(addr::SC4Traffic_LinkCostTables + 0x34 * type);
        int count = 0;
        if (GetFloats(holder, 0x491332E7 + type, &speeds)) {
            count = static_cast<int>(speeds.Size()) < kBuildingTypes ? static_cast<int>(speeds.Size()) : kBuildingTypes;
            for (int i = 0; i < count; ++i) {
                if (speeds.begin[i] > 0.0f) {
                    const float scaled = speeds.begin[i] * 16.666666f;
                    const double speed = scaled / static_cast<double>(Virtual<float>(Global<void*>(addr::SC4_City), 0x16C));
                    own[i] = static_cast<float>(speed);
                    table[i] = static_cast<float>(1.0 / speed);
                } else {
                    own[i] = 0.0f;
                    table[i] = kFloatMax;
                }
            }
        }
        for (int i = count; i < kBuildingTypes; ++i) {
            own[i] = 0.0f;
            table[i] = kFloatMax;
        }
    }
    GetFloat(holder, 0x29136788, &At<float>(self, field::kUnknown97C));
    GetFloat(holder, 0x29136789, &At<float>(self, field::kUnknown978));

    GameVector<float> v8c{};
    GetFloats(holder, 0x6A84493E, &v8c);
    for (uint32_t i = 0; i < kBuildingTypes; ++i)
        At<float>(self, field::kUnknownB54 + 4 * i) = i < v8c.Size() ? v8c.begin[i] : 0.0f;
    GetPercentage(holder, 0xA92356AE, &At<float>(self, field::kUnknownB88));
    GetPercentage(holder, 0xA92356AF, &At<float>(self, field::kUnknownB8C));
    GetFloat(holder, 0xA92356B0, &At<float>(self, field::kUnknownB90));
    GetUint32(holder, 0xA92356B6, &At<uint32_t>(self, field::kUnknownC48));
    GetUint32(holder, 0xA92356B7, &At<uint32_t>(self, field::kUnknownC48 + 4));
    GetUint32(holder, 0xA92356B8, &At<uint32_t>(self, field::kUnknownC48 + 8));

    // The game copies nine words whatever the vector's size.
    GameVector<float> vb0{};
    if (GetFloats(holder, 0xA92356B1, &vb0))
        std::memcpy(&At<uint32_t>(self, field::kUnknownBA4), vb0.begin, 4 * kTravelTypes);

    GameVector<float> v98{};
    if (GetFloats(holder, 0xA92356B3, &v98)) {
        for (uint32_t i = 0; i < kBuildingTypes; ++i) {
            const float value = i < v98.Size() ? v98.begin[i] : 0.0f;
            At<float>(self, field::kUnknownBCC + 4 * i) = value;
            At<float>(self, field::kUnknownBCC + 0x34 + 4 * i) =
                std::fabs(value) > 1e-10 ? static_cast<float>(1.0 / value) : 1.0f;
        }
    }

    BitVector bits{};
    if (GetBools(holder, 0xA92356B4, &bits))
        CopyBits(bits, &At<uint8_t>(self, field::kUnknownC34));
    if (GetBools(holder, 0xA92356BF, &bits))
        CopyBits(bits, &Global<uint8_t>(addr::SC4Traffic_TravelTypeEnabled));
    if (GetBools(holder, 0xA92356B5, &bits))
        CopyBits(bits, &At<uint8_t>(self, field::kUnknownC3D));

    // The travel time curve, its y values inverted.
    void* const travelTime = reinterpret_cast<void*>(addr::SC4Traffic_TravelTimeCurve);
    LoadCurve(travelTime, key, 0xA92356B9);
    GameVector<float> points{};
    GetPoints(travelTime, &points);
    for (uint32_t i = 0; i < points.Size() / 2; ++i)
        points.begin[2 * i + 1] = static_cast<float>(1.0 / points.begin[2 * i + 1]);
    SetPoints(travelTime, &points);
    At<float>(self, field::kTripScale + 4) = static_cast<float>(255.0 / (static_cast<double>(points.end[-1]) - 1.0));
    GetFloat(holder, 0xA92356BA, &At<float>(self, field::kTripScale));

    GameVector<float> v80{};
    if (GetFloats(holder, 0xA92356BB, &v80)) {
        const uint8_t count = static_cast<uint8_t>(v80.Size());
        At<uint8_t>(self, field::kScaleCount) = count;
        auto& scales = At<GameVector<float>>(self, field::kScales);
        if (count < scales.Size()) {
            scales.end = scales.begin + count;
        } else {
            const float zero = 0.0f;
            InsertFloats(&scales, scales.end, count - scales.Size(), &zero);
        }
        for (int i = 0; i < At<uint8_t>(self, field::kScaleCount); ++i)
            scales.begin[i] = static_cast<float>(1.0 / v80.begin[i]);
    }

    GameVector<float> v30{};
    if (GetFloats(holder, 0xA92356BC, &v30))
        CopyWords(v30, &At<uint32_t>(self, field::kUnknownC78));
    if (GetFloats(holder, 0xEA8C3CDB, &v30))
        CopyWords(v30, &At<uint32_t>(self, field::kUnknownC78 + 0x24));
    if (GetFloats(holder, 0xCAD64136, &v30))
        CopyWords(v30, &At<uint32_t>(self, field::kUnknownC78 + 0x48));
    GetFloat(holder, 0xA92356BD, &At<float>(self, field::kUnknownCE4));
    GetFloat(holder, 0x49A2E8BE, &At<float>(self, 0xCF4));
    GetFloat(holder, 0x4A678060, &At<float>(self, 0xCF8));
    GetFloat(holder, 0xCA76013B, &At<float>(self, 0xCFC));
    GetFloat(holder, 0xEA7B5F06, &At<float>(self, 0xD00));

    GameVector<float> va4{};
    if (GetFloats(holder, 0xA92356BE, &va4)) {
        const uint32_t count = va4.Size() < 3 ? va4.Size() : 3;
        for (uint32_t i = 0; i < 3; ++i)
            At<float>(self, field::kUnknownCE8 + 4 * i) = i < count ? va4.begin[i] : 1.0f;
    }

    char* const object = static_cast<char*>(self);
    LoadCurveOrDefault(holder, object + field::kCurveD10, 0xCA5F7821, {0.0f, 0.0f, 1.0f, 1.0f});
    LoadCurveOrDefault(holder, object + field::kCurveD10 + 0x20, 0x4BE09EC9, {0.25f, 0.0f, 1.0f, 0.05f, 2.0f, 0.25f});
    LoadCurveOrDefault(holder, object + field::kCurveD10 + 0x30, 0x4BE09ECA, {0.0f, 1.0f, 1000.0f, 1.0f});
    At<float>(self, field::kUnknownD50) = 10.0f;
    GetFloat(holder, 0x4BE09ECB, &At<float>(self, field::kUnknownD50));
    At<float>(self, field::kUnknownD54) = 60.0f;
    GetFloat(holder, 0x4BE09ECC, &At<float>(self, field::kUnknownD54));
    LoadCurveOrDefault(holder, object + field::kCurveD10 + 0x10, 0x0BD86FD4,
                       {0.0f, 0.02f, 100.0f, -0.01f, 120.0f, -0.04f});
    At<float>(self, field::kRailAccidentChance) = 1.0f;
    GetFloat(holder, 0x0BD86FD5, &At<float>(self, field::kRailAccidentChance));
    GetFloat(holder, 0x8C3CAEF5, &At<float>(self, 0xD08));
    GetFloat(holder, 0x0CAAAEB8, &At<float>(self, 0xD0C));
    Virtual<uint32_t>(exemplar, 0x8); // Release
    reinterpret_cast<SelfFn>(addr::cSC4TrafficSimulator_LoadTravelStrategies)(self);

    // Freed in the game's order.
    for (void* storage : {static_cast<void*>(va4.begin), static_cast<void*>(v30.begin), static_cast<void*>(v80.begin),
                          static_cast<void*>(points.begin), static_cast<void*>(bits.start),
                          static_cast<void*>(v98.begin), static_cast<void*>(vb0.begin),
                          static_cast<void*>(v8c.begin), static_cast<void*>(speeds.begin)})
        if (storage)
            Free(storage);
}

// --- Self-check ---------------------------------------------------------------

using TuningFn = void(__fastcall*)(void*, void*);
TuningFn g_original = nullptr;

// Everything LoadTuning writes: the simulator's 0x974-0xD58 with its
// vector and curves by content, the cost tables, the flags and the travel
// time curve.
std::vector<uint32_t> TuningState(void* self)
{
    std::vector<uint32_t> state;
    auto words = [&state](const void* from, std::size_t bytes) {
        for (std::size_t at = 0; at < bytes; at += 4)
            state.push_back(*reinterpret_cast<const uint32_t*>(static_cast<const char*>(from) + at));
    };
    auto range = [&state, &words](const void* object) {
        const auto* const fields = static_cast<const char* const*>(object);
        state.push_back(static_cast<uint32_t>(fields[1] - fields[0]));
        words(fields[0], static_cast<std::size_t>(fields[1] - fields[0]));
    };
    auto curve = [&state, &range](const void* object) {
        range(object);
        state.push_back(static_cast<const uint32_t*>(object)[3]);
    };
    char* const object = static_cast<char*>(self);
    words(object + 0x974, field::kScales - 0x974);
    range(object + field::kScales);
    words(object + 0xC78, field::kCurveD10 - 0xC78);
    for (std::size_t at = field::kCurveD10; at < field::kUnknownD50; at += 0x10)
        curve(object + at);
    words(object + field::kUnknownD50, 8);
    words(reinterpret_cast<void*>(addr::SC4Traffic_LinkCostTables), 4 * kTravelTypes * kBuildingTypes);
    words(reinterpret_cast<void*>(addr::SC4Traffic_TravelTypeEnabled), 8);
    state.push_back(Global<uint8_t>(addr::SC4Traffic_TravelTypeEnabled + 8));
    curve(reinterpret_cast<void*>(addr::SC4Traffic_TravelTimeCurve));
    return state;
}

// With OPENSC4_TRAFFIC_SELFCHECK set: the game's function, then ours, and
// everything both write compared.
void __fastcall CheckLoadTuning(void* self, void* edx)
{
    g_original(self, edx);
    const std::vector<uint32_t> theirs = TuningState(self);
    LoadTuning(self, edx);
    const std::vector<uint32_t> ours = TuningState(self);
    std::size_t first = 0;
    while (first < theirs.size() && first < ours.size() && theirs[first] == ours[first])
        ++first;
    log::Write("traffic tuning: self-check against the game's: %s (%zu words)", theirs == ours ? "same" : "DIFFERENT",
               theirs.size());
    if (theirs != ours)
        log::Write("traffic tuning: first difference at word %zu", first);
}

bool Install(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_LoadTuning);
    if (!game::IsBuild641() || !entry)
        return false;
    if (GetEnvironmentVariableA("OPENSC4_TRAFFIC_SELFCHECK", nullptr, 0) != 0)
        return hook::Install(*entry, &CheckLoadTuning, &g_original);
    return hook::Install(*entry, &LoadTuning, &g_original);
}

const bool g_registered = registry::Register({
    "simulators.traffic.tuning",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_LoadTuning,
    "Our traffic tuning load: link costs, curves and scales from the traffic exemplar (docs/traffic.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::traffic_sim
