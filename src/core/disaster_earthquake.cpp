// The earthquake (docs/disasters.md): cSC4DisasterManagerEarthquake
// (disaster 0x42963812) and cSC4DisasterInstanceEarthquake: a fault of two
// Bezier halves drawn from the epicentre, walked in steps that each send
// the terrain a request. Replacement, off by default:
//
//   simulators.disasters.earthquake: the manager's start at a cell and the
//     instance (the start, the fault, the tuning, the rumble, the steps,
//     the news items, the effects, the tick, the end, Shutdown).
//
// The objects' constructors and destructors, the curves and the terrain's
// own work stay the game's. The instance's interface is at +8 and its
// message target at +0xC.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::disasters {
namespace {

constexpr uint32_t kEarthquake = 0x42963812;

namespace field { // the instance
constexpr std::size_t kInited = 0x10;
constexpr std::size_t kPhase = 0x14;       // 1 rumble, 2 quake, 3 after, 4 over
constexpr std::size_t kManager = 0x18;
constexpr std::size_t kGround = 0x1C;      // city slot 0x150
constexpr std::size_t kEffects = 0x20;     // city slot 0x154
constexpr std::size_t kStrength = 0x24;
constexpr std::size_t kEpicentre = 0x28;   // two floats
constexpr std::size_t kMaxX = 0x30;
constexpr std::size_t kMaxZ = 0x34;
constexpr std::size_t kElapsed = 0x38;
constexpr std::size_t kRumbleTimer = 0x3C;
constexpr std::size_t kCos = 0x40;
constexpr std::size_t kSin = 0x44;
constexpr std::size_t kPointsA = 0x48;     // vectors of float pairs
constexpr std::size_t kPointsB = 0x54;
constexpr std::size_t kDoneA = 0x60;
constexpr std::size_t kDoneB = 0x61;
constexpr std::size_t kStep = 0x64;
constexpr std::size_t kAt = 0x68;
constexpr std::size_t kTimerA = 0x6C;
constexpr std::size_t kTimerB = 0x70;
constexpr std::size_t kPrevA = 0x74;       // valid byte, then two floats at +4
constexpr std::size_t kPrevB = 0x80;
constexpr std::size_t kWalked = 0x8C;
constexpr std::size_t kTarget = 0x90;
constexpr std::size_t kScaleA = 0x94;
constexpr std::size_t kScaleB = 0x98;
constexpr std::size_t kRequestA = 0x9C;    // 0x38 bytes
constexpr std::size_t kRequestB = 0xD4;
constexpr std::size_t kEffectA = 0x10C;
constexpr std::size_t kEffectB = 0x110;
constexpr std::size_t kNews = 0x114;       // vector of news items
} // namespace field

namespace request { // a terrain request
constexpr std::size_t kFrom = 0x0;
constexpr std::size_t kTo = 0x8;
constexpr std::size_t kA = 0x10;
constexpr std::size_t kB = 0x14;
constexpr std::size_t kAmplitudeA = 0x18;
constexpr std::size_t kAmplitudeB = 0x1C;
constexpr std::size_t kKind = 0x20;
constexpr std::size_t kRect = 0x24;
constexpr std::size_t kSize = 0x38;
} // namespace request

// A point of one half of the fault, with whether it is still in the city.
struct FaultPoint {
    uint8_t valid;
    float x;
    float z;
};

struct Rect {
    int32_t left = 0x7FFFFFFF;
    int32_t top = 0x7FFFFFFF;
    int32_t right = static_cast<int32_t>(0x80000000);
    int32_t bottom = static_cast<int32_t>(0x80000000);
};

void* City()
{
    return Global<void*>(addr::SC4_City);
}

void* Curve(int which)
{
    return reinterpret_cast<void*>(addr::cSC4DisasterInstanceEarthquake_Curves + 0x10 * which);
}

double Evaluate(int which, float at)
{
    return reinterpret_cast<double(__thiscall*)(void*, float)>(addr::SC4Curve_Evaluate)(Curve(which), at);
}

template <typename T>
T& Tuning(std::size_t at)
{
    return Global<T>(addr::cSC4DisasterInstanceEarthquake_Tuning + at);
}

float& Spread()
{
    return Global<float>(addr::cSC4DisasterInstanceEarthquake_Spread);
}

double Unsigned(uint32_t value)
{
    return static_cast<double>(value);
}

// x87 instructions, as the game executes them (53-bit precision).
double X87Cos(double x)
{
    double result;
    __asm {
        fld x
        fcos
        fstp result
    }
    return result;
}

double X87Sin(double x)
{
    double result;
    __asm {
        fld x
        fsin
        fstp result
    }
    return result;
}

int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

double Height(char* quake, float x, float z)
{
    return Virtual<double>(At<void*>(quake, field::kGround), 0x34, x, z);
}

// An effect's placement: no rotation, on the ground at (x, z).
struct Placement {
    uint8_t a;
    uint8_t b;
    uint8_t pad[2];
    float matrix[9];
    float position[3];
    float scale;
};

void Place(char* quake, void* effect, const float* position)
{
    Placement placement{};
    placement.matrix[0] = 1.0f;
    placement.matrix[4] = 1.0f;
    placement.matrix[8] = 1.0f;
    placement.scale = 1.0f;
    const float x = position[0];
    const float z = position[1];
    placement.position[1] = static_cast<float>(Height(quake, x, z));
    placement.position[0] = x;
    placement.position[2] = z;
    placement.b |= 1;
    ++placement.a;
    Virtual<void>(effect, 0x20, &placement);
}

uint32_t __fastcall InCity(char* quake, void*, float x, float z)
{
    return x >= 0.0f && x <= At<float>(quake, field::kMaxX) && z >= 0.0f && z <= At<float>(quake, field::kMaxZ);
}

char* __fastcall QuakeGetManager(char* part, void*)
{
    char* const manager = At<char*>(part, 0x10);
    return manager ? manager + 0x8 : nullptr;
}

// The game's 0x005756F0.
void __fastcall End(char* quake, void*)
{
    Virtual<void>(At<void*>(quake, field::kGround), 0x13C);
    char* const part = quake + 0x8;
    if (void* const layer = Virtual<void*>(City(), 0x120)) {
        Virtual<uint8_t>(layer, 0x38, part); // UnregisterDisasterLocations
        Virtual<void>(layer, 0x24);          // DisasterHasEnded
    }
    Virtual<uint32_t>(quake, 0x4);
    Virtual<uint8_t>(At<char*>(quake, field::kManager) + 0x8, 0x24, part); // RemoveInstanceFromList
    Virtual<uint8_t>(part, 0xC);                                           // Shutdown
    Virtual<uint32_t>(quake, 0x8);
}

// The game's 0x00575750.
void __cdecl QuakeLoadTuning()
{
    uint32_t key[3] = {0x6534284A, 0x29B6C670, 2};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, uint32_t{0xA52160F5}, &exemplar, int32_t{0},
                          int32_t{0}))
        return;
    void* const properties = Virtual<void*>(exemplar, 0x1C);
    using LoadFn = void(__thiscall*)(void*, void*);
    for (int which = 0; which < 4; ++which)
        reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromProperty)(
            Curve(which), Virtual<void*>(properties, 0x24, uint32_t{0x4A08C500} + which));
    using FloatFn = uint8_t(__cdecl*)(void*, uint32_t, float*);
    const auto GetFloat = reinterpret_cast<FloatFn>(addr::SC4_GetPropertyFloat);
    GetFloat(properties, 0x4A08C504, &Tuning<float>(0x4));
    GetFloat(properties, 0x4A08C505, &Tuning<float>(0x8));
    GetFloat(properties, 0x4A08C506, &Spread());
    Spread() = static_cast<float>(static_cast<double>(Spread()) * Global<double>(addr::SC4_DoubleDegree));
    GetFloat(properties, 0x4A08C507, &Tuning<float>(0xC));
    Tuning<float>(0xC) = static_cast<float>(static_cast<double>(Tuning<float>(0xC)) * static_cast<double>(1e-6f));
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint32_t*)>(addr::SC4_GetPropertyUint32)(properties, 0x4A08C508,
                                                                                               &Tuning<uint32_t>(0x10));
    GetFloat(properties, 0x4A08C509, &Tuning<float>(0x14));
    Virtual<uint32_t>(exemplar, 0x8);
}

// The game's 0x00575890.
void __fastcall QuakeMakeEffect(char* quake, void*, const char* name, const float* position, uint8_t keep, void** out)
{
    *out = nullptr;
    if (!Virtual<uint8_t>(At<void*>(quake, field::kEffects), 0x1C, name, out))
        return;
    Place(quake, *out, position);
    Virtual<void>(*out, 0xC, int32_t{0});
    if (!keep) {
        Virtual<uint32_t>(*out, 0x8);
        *out = nullptr;
    }
}

// The game's 0x005759B0.
void __fastcall Rumble(char* quake, void*, uint32_t passed)
{
    float& strength = At<float>(quake, field::kStrength);
    if (strength < 1.0f) {
        const double grown = Unsigned(passed) * static_cast<double>(1e-7f) + static_cast<double>(strength);
        strength = static_cast<float>(grown);
        if (grown > 1.0)
            strength = 1.0f;
    }
    uint32_t& timer = At<uint32_t>(quake, field::kRumbleTimer);
    timer += passed;
    if (timer >= Tuning<uint32_t>(0x10)) {
        void* effect = nullptr;
        QuakeMakeEffect(quake, nullptr, "QuakerShaker", &At<float>(quake, field::kEpicentre), 0, &effect);
        timer = 0;
    }
}

// The game's 0x00575A30: a cubic Bezier through the vector's four points.
void __cdecl Bezier(char* points, float t, float* out)
{
    const float* const p = At<const float*>(points, 0x0);
    const double u = 1.0 - static_cast<double>(t);
    const double uu = u * u;
    const float tt = static_cast<float>(static_cast<double>(t) * static_cast<double>(t));
    const float uuu = static_cast<float>(uu * u);
    const double b1 = uu * static_cast<double>(t) * 3.0;
    const double b2 = static_cast<double>(tt) * u * 3.0;
    const double ttt = static_cast<double>(tt) * static_cast<double>(t);
    out[0] = static_cast<float>(ttt * p[6] + b2 * p[4] + b1 * p[2] + static_cast<double>(uuu) * p[0]);
    out[1] = static_cast<float>(ttt * p[7] + b2 * p[5] + b1 * p[3] + static_cast<double>(uuu) * p[1]);
}

// The game's 0x00575AC0.
uint8_t __fastcall NextPoints(char* quake, void*, FaultPoint* a, FaultPoint* b)
{
    uint8_t& doneA = At<uint8_t>(quake, field::kDoneA);
    uint8_t& doneB = At<uint8_t>(quake, field::kDoneB);
    if (doneA && doneB)
        return 0;
    a->valid = !doneA;
    b->valid = !doneB;
    if (a->valid) {
        Bezier(quake + field::kPointsA, At<float>(quake, field::kAt), &a->x);
        if (!InCity(quake, nullptr, a->x, a->z)) {
            a->valid = 0;
            doneA = 1;
        }
    }
    if (b->valid) {
        Bezier(quake + field::kPointsB, At<float>(quake, field::kAt), &b->x);
        if (!InCity(quake, nullptr, b->x, b->z)) {
            b->valid = 0;
            doneB = 1;
        }
    }
    At<float>(quake, field::kAt) =
        static_cast<float>(static_cast<double>(At<float>(quake, field::kStep)) + At<float>(quake, field::kAt));
    return 1;
}

// The game's 0x00575B70.
void __fastcall PlaceEffectAt(char* quake, void*, void* effect, const float* position)
{
    Place(quake, effect, position);
}

// The game's 0x00575C40.
void __fastcall StartQuake(char* quake, void*)
{
    At<uint32_t>(quake, field::kWalked) = 0;
    At<uint32_t>(quake, field::kTarget) = 0;
    At<float>(quake, field::kScaleA) = static_cast<float>(Evaluate(0, At<float>(quake, field::kStrength)));
    At<float>(quake, field::kScaleB) = static_cast<float>(Evaluate(1, At<float>(quake, field::kStrength)));
    At<uint32_t>(quake, field::kRequestA + request::kA) = 0;
    At<uint8_t>(quake, field::kRequestA + request::kB) = 0;
    At<uint32_t>(quake, field::kRequestA + request::kKind) = 3;
    std::memcpy(quake + field::kRequestB, quake + field::kRequestA, request::kSize);
    At<uint8_t>(quake, field::kPrevA) = 0;
    At<uint8_t>(quake, field::kPrevB) = 0;
    float* const epicentre = &At<float>(quake, field::kEpicentre);
    QuakeMakeEffect(quake, nullptr, "Quaker", epicentre, 1, &At<void*>(quake, field::kEffectA));
    QuakeMakeEffect(quake, nullptr, "Quaker", epicentre, 1, &At<void*>(quake, field::kEffectB));
    At<uint32_t>(quake, field::kTimerA) = 0;
    At<uint32_t>(quake, field::kTimerB) = 0;
    At<uint8_t>(quake, field::kDoneA) = 0;
    At<uint8_t>(quake, field::kDoneB) = 0;
    At<uint32_t>(quake, field::kAt) = 0;
    At<float>(quake, field::kStep) = static_cast<float>(8.0 / (Virtual<double>(City(), 0x164) * 3.0));
    Virtual<void>(At<void*>(quake, field::kGround), 0x15C);
    void* const sound = Global<void*>(addr::SC4_Sound43CB8);
    if (!sound)
        return;
    Virtual<uint32_t>(sound, 0x4);
    const int32_t height = Ftol(static_cast<float>(Height(quake, epicentre[0], epicentre[1])));
    for (char* who : {quake, quake + 1}) {
        Virtual<void>(sound, 0x2C, uint32_t{0xEA451903}, who == quake ? uint32_t{0x2A3836F0} : uint32_t{0xCA383686}, who,
                      int32_t{0}, int32_t{0});
        const int32_t z = Ftol(epicentre[1]);
        const int32_t x = Ftol(epicentre[0]);
        Virtual<void>(sound, 0x2C, uint32_t{0xCA45190F}, who, x, height, z);
    }
    Virtual<uint32_t>(sound, 0x8);
}

// One half's request for a step: from the previous point (or the point
// itself the first time) to the point.
void Shake(char* quake, std::size_t requestAt, std::size_t prevAt, const FaultPoint& point, int32_t kind, Rect& rect,
           std::size_t timerAt, std::size_t effectAt)
{
    char* const req = quake + requestAt;
    At<float>(req, request::kTo) = point.x;
    At<float>(req, request::kTo + 4) = point.z;
    if (At<uint8_t>(quake, prevAt)) {
        At<float>(req, request::kFrom) = At<float>(quake, prevAt + 4);
        At<float>(req, request::kFrom + 4) = At<float>(quake, prevAt + 8);
    } else {
        At<float>(req, request::kFrom) = point.x;
        At<float>(req, request::kFrom + 4) = point.z;
    }
    Virtual<void>(At<void*>(quake, field::kGround), 0xE4, kind, req);
    reinterpret_cast<void(__thiscall*)(Rect*, Rect*, void*)>(addr::SC4Rect_Union)(&rect, &rect, req + request::kRect);
    if (At<float>(quake, timerAt) < Tuning<float>(0x4))
        return;
    if (void* const effect = At<void*>(quake, effectAt))
        Place(quake, effect, &point.x);
    At<uint32_t>(quake, timerAt) = 0;
}

// The game's 0x00575DE0: true when the fault is finished.
uint8_t __fastcall Step(char* quake, void*, uint32_t passed)
{
    Rect rectA;
    Rect rectB;
    FaultPoint a{};
    FaultPoint b{};
    const double target =
        Unsigned(passed) * static_cast<double>(Tuning<float>(0xC)) + static_cast<double>(At<float>(quake, field::kTarget));
    At<float>(quake, field::kTarget) = static_cast<float>(target);
    const double due = (target - static_cast<double>(At<float>(quake, field::kWalked))) * 0.125;
    const float rounded = static_cast<float>(due - 0.5 + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    int32_t steps = static_cast<int32_t>(bits + 0xB4C00000u);
    if (due == static_cast<double>(steps + 1))
        ++steps;
    for (int32_t i = 0; i < steps; ++i) {
        if (!NextPoints(quake, nullptr, &a, &b))
            return 1;
        const float walked = At<float>(quake, field::kWalked);
        const float amplitudeA =
            static_cast<float>(Evaluate(2, walked) * static_cast<double>(At<float>(quake, field::kScaleA)));
        At<float>(quake, field::kRequestB + request::kAmplitudeA) = amplitudeA;
        At<float>(quake, field::kRequestA + request::kAmplitudeA) = amplitudeA;
        const double amplitudeB = Evaluate(3, walked) * static_cast<double>(At<float>(quake, field::kScaleB));
        At<float>(quake, field::kRequestB + request::kAmplitudeB) = static_cast<float>(amplitudeB);
        At<float>(quake, field::kRequestA + request::kAmplitudeB) = static_cast<float>(amplitudeB);
        if (amplitudeA < 0.001f)
            return 1;
        if (amplitudeB < static_cast<double>(0.002f))
            return 1;
        if (a.valid)
            Shake(quake, field::kRequestA, field::kPrevA, a, 0x11, rectA, field::kTimerA, field::kEffectA);
        if (b.valid)
            Shake(quake, field::kRequestB, field::kPrevB, b, 0x12, rectB, field::kTimerB, field::kEffectB);
        for (std::size_t at : {field::kWalked, field::kTimerA, field::kTimerB})
            At<float>(quake, at) = static_cast<float>(static_cast<double>(At<float>(quake, at)) + 8.0);
        At<uint8_t>(quake, field::kPrevA) = a.valid;
        At<float>(quake, field::kPrevA + 4) = a.x;
        At<float>(quake, field::kPrevA + 8) = a.z;
        At<uint8_t>(quake, field::kPrevB) = b.valid;
        At<float>(quake, field::kPrevB + 4) = b.x;
        At<float>(quake, field::kPrevB + 8) = b.z;
    }
    void* const ground = At<void*>(quake, field::kGround);
    Virtual<void>(ground, 0x138);
    for (Rect* rect : {&rectA, &rectB}) {
        if (rect->left <= rect->right && rect->top <= rect->bottom)
            Virtual<void>(At<void*>(quake, field::kGround), 0x110, int32_t{1}, int32_t{1}, rect, int32_t{3});
    }
    return 0;
}

// The game's 0x00576280.
void __fastcall ClearNews(char* quake, void*)
{
    void* const news = Virtual<void*>(City(), 0xBC);
    void** const end = At<void**>(quake, field::kNews + 4);
    for (void** item = At<void**>(quake, field::kNews); item != end; ++item) {
        if (news)
            Virtual<void>(news, 0x1C, *item);
        Virtual<uint32_t>(*item, 0x8);
    }
    At<void**>(quake, field::kNews + 4) = At<void**>(quake, field::kNews);
}

// The game's 0x00576300, on the interface at +8.
uint8_t __fastcall QuakeShutdown(char* part, void*)
{
    char* const quake = part - 0x8;
    if (!At<uint8_t>(quake, field::kInited))
        return 1;
    At<uint8_t>(quake, field::kInited) = 0;
    void* const sound = Global<void*>(addr::SC4_Sound43CB8);
    if (sound) {
        Virtual<uint32_t>(sound, 0x4);
        Virtual<void>(sound, 0x2C, uint32_t{0x6A451908}, quake, int32_t{0}, int32_t{0}, int32_t{0});
        Virtual<void>(sound, 0x2C, uint32_t{0x6A451908}, quake + 1, int32_t{0}, int32_t{0}, int32_t{0});
    }
    ClearNews(quake, nullptr);
    for (std::size_t at : {field::kEffectA, field::kEffectB}) {
        if (void* const effect = At<void*>(quake, at)) {
            Virtual<uint32_t>(effect, 0x8);
            At<void*>(quake, at) = nullptr;
        }
    }
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x18, quake + 0xC, int32_t{0xC0});
    for (std::size_t at : {field::kEffects, field::kGround, field::kManager}) {
        if (void* const object = At<void*>(quake, at)) {
            Virtual<uint32_t>(object, 0x8);
            At<void*>(quake, at) = nullptr;
        }
    }
    if (sound)
        Virtual<uint32_t>(sound, 0x8);
    return 1;
}

// The game's 0x00576490.
void __fastcall AddNews(char* quake, void*, const float* position)
{
    float where[3] = {position[0], Global<float>(addr::cSC4DisasterInstanceEarthquake_Statics + 0x8), position[1]};
    where[1] = static_cast<float>(Height(quake, position[0], position[1]));
    void* const news = Virtual<void*>(City(), 0xBC);
    if (!news)
        return;
    struct GameString {
        std::uintptr_t vtable;
        char* begin;
        char* end;
        char* capacity;
        uint32_t zero;
    } text{};
    reinterpret_cast<void*(__thiscall*)(GameString*, const char*)>(addr::SC4String_FromText)(&text,
                                                                                            Tuning<const char*>(0x0));
    void* item = Virtual<void*>(news, 0x14, int32_t{0}, &text, where);
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
    if (!item)
        return;
    Virtual<uint32_t>(item, 0x4);
    void**& end = At<void**>(quake, field::kNews + 4);
    if (end != At<void**>(quake, field::kNews + 8)) {
        if (end)
            *end = item;
        ++end;
        return;
    }
    using InsertFn = void(__thiscall*)(void*, void**, void**, void*, uint32_t, uint32_t);
    reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(quake + field::kNews, end, &item, &item, 1, 1);
}

// The game's 0x00576580.
void __fastcall Preview(char* quake, void*)
{
    At<uint8_t>(quake, field::kDoneA) = 0;
    At<uint8_t>(quake, field::kDoneB) = 0;
    At<uint32_t>(quake, field::kAt) = 0;
    FaultPoint a{};
    FaultPoint b{};
    At<float>(quake, field::kStep) =
        static_cast<float>(static_cast<double>(Tuning<float>(0x14)) / (Virtual<double>(City(), 0x164) * 3.0));
    while (NextPoints(quake, nullptr, &a, &b)) {
        if (a.valid)
            AddNews(quake, nullptr, &a.x);
        if (b.valid)
            AddNews(quake, nullptr, &b.x);
    }
}

void PushPoint(char* vector, const float* point)
{
    float*& end = At<float*>(vector, 0x4);
    if (end != At<float*>(vector, 0x8)) {
        if (end) {
            end[0] = point[0];
            end[1] = point[1];
        }
        end += 2;
        return;
    }
    uint8_t allocator = 0;
    using InsertFn = void(__thiscall*)(void*, float*, const float*, void*, uint32_t, uint32_t);
    reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN)(vector, end, point, &allocator, 1, 1);
}

// The game's 0x00576640: the direction and the control points of both
// halves, which leave the epicentre in opposite directions.
void __fastcall MakeFault(char* quake, void*)
{
    using UniformFn = double(__thiscall*)(void*, double, double);
    const auto Uniform = reinterpret_cast<UniformFn>(addr::cRZRandom_Uniform);
    void* const random = reinterpret_cast<void*>(addr::cSC4DisasterInstanceEarthquake_Random);
    uint8_t& drawn = Global<uint8_t>(addr::cSC4DisasterInstanceEarthquake_Statics + 0x1);
    float& base = Global<float>(addr::cSC4DisasterInstanceEarthquake_Statics + 0x4);
    if (!drawn) {
        base = static_cast<float>(Uniform(random, 0.0, static_cast<double>(3.14159274f)));
        drawn = 1;
    }
    const double spread = Spread();
    const float baseNow = base;
    const double angle = Uniform(random, -spread, spread) + static_cast<double>(baseNow);
    At<float>(quake, field::kCos) = static_cast<float>(X87Cos(angle));
    At<float>(quake, field::kSin) = static_cast<float>(X87Sin(angle));
    const double acrossX = -At<float>(quake, field::kSin);
    const double acrossZ = At<float>(quake, field::kCos);
    const float* const epicentre = &At<float>(quake, field::kEpicentre);
    float minus[2] = {epicentre[0], epicentre[1]};
    float plus[2] = {epicentre[0], epicentre[1]};
    const double third = (static_cast<double>(At<float>(quake, field::kMaxX)) + 1.0) * static_cast<double>(1.0f / 3.0f);
    const float alongX = static_cast<float>(third * At<float>(quake, field::kCos));
    const float alongZ = static_cast<float>(third * At<float>(quake, field::kSin));
    char* const pointsA = quake + field::kPointsA;
    char* const pointsB = quake + field::kPointsB;
    PushPoint(pointsA, epicentre);
    PushPoint(pointsB, epicentre);
    const double jitter = Tuning<float>(0x8);
    float point[2];

    const double first = Uniform(random, -jitter, jitter);
    const float firstBack = static_cast<float>(-first);
    minus[0] = static_cast<float>(static_cast<double>(minus[0]) - alongX);
    minus[1] = static_cast<float>(static_cast<double>(minus[1]) - alongZ);
    plus[0] = static_cast<float>(static_cast<double>(alongX) + plus[0]);
    plus[1] = static_cast<float>(static_cast<double>(alongZ) + plus[1]);
    const float firstZ = static_cast<float>(acrossZ * first);
    point[0] = static_cast<float>(acrossX * first + minus[0]);
    point[1] = static_cast<float>(static_cast<double>(firstZ) + minus[1]);
    PushPoint(pointsA, point);
    point[0] = static_cast<float>(acrossX * firstBack + plus[0]);
    point[1] = static_cast<float>(acrossZ * firstBack + plus[1]);
    PushPoint(pointsB, point);

    const float second = static_cast<float>(Uniform(random, -jitter, jitter));
    const float secondB = static_cast<float>(Uniform(random, -jitter, jitter));
    minus[0] = static_cast<float>(static_cast<double>(minus[0]) - alongX);
    minus[1] = static_cast<float>(static_cast<double>(minus[1]) - alongZ);
    plus[0] = static_cast<float>(static_cast<double>(alongX) + plus[0]);
    plus[1] = static_cast<float>(static_cast<double>(alongZ) + plus[1]);
    point[0] = static_cast<float>(acrossX * second + minus[0]);
    point[1] = static_cast<float>(acrossZ * second + minus[1]);
    PushPoint(pointsA, point);
    point[0] = static_cast<float>(acrossX * secondB + plus[0]);
    point[1] = static_cast<float>(acrossZ * secondB + plus[1]);
    PushPoint(pointsB, point);

    const float third4 = static_cast<float>(Uniform(random, -jitter, jitter));
    const float thirdB = static_cast<float>(Uniform(random, -jitter, jitter));
    const double lastX = static_cast<double>(minus[0]) - alongX;
    const double lastZ = static_cast<double>(minus[1]) - alongZ;
    plus[0] = static_cast<float>(static_cast<double>(alongX) + plus[0]);
    plus[1] = static_cast<float>(static_cast<double>(alongZ) + plus[1]);
    const float thirdZ = static_cast<float>(acrossZ * third4);
    point[0] = static_cast<float>(acrossX * third4 + lastX);
    point[1] = static_cast<float>(static_cast<double>(thirdZ) + lastZ);
    PushPoint(pointsA, point);
    point[0] = static_cast<float>(acrossX * thirdB + plus[0]);
    point[1] = static_cast<float>(acrossZ * thirdB + plus[1]);
    PushPoint(pointsB, point);
}

// The game's 0x00576B10.
void __fastcall QuakeTick(char* quake, void*, uint32_t passed)
{
    const uint32_t elapsed = At<uint32_t>(quake, field::kElapsed) += passed;
    switch (At<int32_t>(quake, field::kPhase)) {
    case 1:
        Rumble(quake, nullptr, passed);
        break;
    case 2:
        if (Step(quake, nullptr, passed)) {
            At<int32_t>(quake, field::kPhase) = 3;
            Virtual<void>(At<void*>(quake, field::kGround), 0x160);
        }
        break;
    case 3:
        if (elapsed > 3000000) {
            At<uint32_t>(quake, field::kElapsed) = 0;
            At<int32_t>(quake, field::kPhase) = 4;
            End(quake, nullptr);
        }
        break;
    default:
        break;
    }
}

uint8_t __fastcall QuakeDoMessage(char* target, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) == 0x86AD10EE) {
        const uint32_t passed = Virtual<uint32_t>(message, 0x38);
        QuakeTick(target - 0xC, nullptr, passed);
    }
    return 1;
}

// The game's 0x00576BB0.
uint8_t __fastcall Start(char* quake, void*, int32_t x, int32_t z)
{
    uint8_t& loaded = Global<uint8_t>(addr::cSC4DisasterInstanceEarthquake_Statics);
    if (!loaded) {
        loaded = 1;
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x114,
                      reinterpret_cast<void*>(addr::cSC4DisasterInstanceEarthquake_ResetStatics), int32_t{0});
        QuakeLoadTuning();
    }
    At<uint8_t>(quake, field::kInited) = 1;
    void* const city = City();
    At<float>(quake, field::kMaxX) = static_cast<float>(Virtual<double>(city, 0x164) - 1.0);
    At<float>(quake, field::kMaxZ) = static_cast<float>(Virtual<double>(city, 0x168) - 1.0);
    void* const ground = Virtual<void*>(city, 0x150);
    At<void*>(quake, field::kGround) = ground;
    if (ground)
        Virtual<uint32_t>(ground, 0x4);
    void* const layer = Virtual<void*>(city, 0x120);
    if (char* const manager = Virtual<char*>(layer, 0x1C, kEarthquake)) {
        At<char*>(quake, field::kManager) = manager - 0x8;
        Virtual<uint32_t>(manager - 0x8, 0x4);
    }
    void* const effects = Virtual<void*>(city, 0x154);
    At<void*>(quake, field::kEffects) = effects;
    if (effects)
        Virtual<uint32_t>(effects, 0x4);
    float* const epicentre = &At<float>(quake, field::kEpicentre);
    epicentre[0] = static_cast<float>(x);
    epicentre[1] = static_cast<float>(z);
    if (x < 0)
        epicentre[0] = 0.0f;
    else if (static_cast<double>(x) > static_cast<double>(At<float>(quake, field::kMaxX)))
        epicentre[0] = At<float>(quake, field::kMaxX);
    const float depth = static_cast<float>(z);
    if (depth < 0.0f)
        epicentre[1] = 0.0f;
    else if (depth > At<float>(quake, field::kMaxZ))
        epicentre[1] = At<float>(quake, field::kMaxZ);
    MakeFault(quake, nullptr);
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x14, quake + 0xC, int32_t{0xC0});
    void* effect = nullptr;
    QuakeMakeEffect(quake, nullptr, "QuakeStart", epicentre, 0, &effect);
    At<uint32_t>(quake, field::kRumbleTimer) = 0;
    if (At<int32_t>(quake, field::kPhase) != 1) {
        At<int32_t>(quake, field::kPhase) = 1;
        At<uint32_t>(quake, field::kElapsed) = 0;
        Preview(quake, nullptr);
    }
    int32_t cellX = 0;
    int32_t cellZ = 0;
    Virtual<void>(city, 0x17C, epicentre[0], epicentre[1], &cellX, &cellZ);
    char* const part = quake + 0x8;
    Virtual<uint8_t>(layer, 0x34, part, cellX, cellZ, int32_t{0}, int32_t{0}, int32_t{0});
    Virtual<uint32_t>(layer, 0x20, part); // DisasterHasBegun
    return 1;
}

// The manager's interface slot 6: one earthquake at a time, started in
// the quake phase.
char* __fastcall QuakeAttemptDisasterStart(char* interface, void*, int32_t x, int32_t z)
{
    char* const list = At<char*>(interface, 0xC);
    if (At<char*>(list, 0x0) != list)
        return nullptr;
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x120);
    char* const quake =
        memory ? reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4DisasterInstanceEarthquake_Construct)(memory)
               : nullptr;
    Virtual<uint32_t>(interface, 0x20, quake ? quake + 0x8 : nullptr); // AddInstanceToList
    Start(quake, nullptr, x, z);
    if (At<int32_t>(quake, field::kPhase) != 2) {
        At<uint32_t>(quake, field::kElapsed) = 0;
        At<int32_t>(quake, field::kPhase) = 2;
        StartQuake(quake, nullptr);
    }
    return quake + 0x8;
}

bool InstallEarthquake(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterInstanceEarthquake_InCity, reinterpret_cast<void*>(&InCity)},
        {addr::cSC4DisasterInstanceEarthquake_GetManager, reinterpret_cast<void*>(&QuakeGetManager)},
        {addr::cSC4DisasterInstanceEarthquake_End, reinterpret_cast<void*>(&End)},
        {addr::cSC4DisasterInstanceEarthquake_LoadTuning, reinterpret_cast<void*>(&QuakeLoadTuning)},
        {addr::cSC4DisasterInstanceEarthquake_MakeEffect, reinterpret_cast<void*>(&QuakeMakeEffect)},
        {addr::cSC4DisasterInstanceEarthquake_Rumble, reinterpret_cast<void*>(&Rumble)},
        {addr::cSC4DisasterInstanceEarthquake_Bezier, reinterpret_cast<void*>(&Bezier)},
        {addr::cSC4DisasterInstanceEarthquake_NextPoints, reinterpret_cast<void*>(&NextPoints)},
        {addr::cSC4DisasterInstanceEarthquake_PlaceEffectAt, reinterpret_cast<void*>(&PlaceEffectAt)},
        {addr::cSC4DisasterInstanceEarthquake_StartQuake, reinterpret_cast<void*>(&StartQuake)},
        {addr::cSC4DisasterInstanceEarthquake_Step, reinterpret_cast<void*>(&Step)},
        {addr::cSC4DisasterInstanceEarthquake_ClearNews, reinterpret_cast<void*>(&ClearNews)},
        {addr::cSC4DisasterInstanceEarthquake_Shutdown, reinterpret_cast<void*>(&QuakeShutdown)},
        {addr::cSC4DisasterInstanceEarthquake_AddNews, reinterpret_cast<void*>(&AddNews)},
        {addr::cSC4DisasterInstanceEarthquake_Preview, reinterpret_cast<void*>(&Preview)},
        {addr::cSC4DisasterInstanceEarthquake_MakeFault, reinterpret_cast<void*>(&MakeFault)},
        {addr::cSC4DisasterInstanceEarthquake_Tick, reinterpret_cast<void*>(&QuakeTick)},
        {addr::cSC4DisasterInstanceEarthquake_DoMessage, reinterpret_cast<void*>(&QuakeDoMessage)},
        {addr::cSC4DisasterInstanceEarthquake_Start, reinterpret_cast<void*>(&Start)},
        {addr::cSC4DisasterManagerEarthquake_AttemptDisasterStart, reinterpret_cast<void*>(&QuakeAttemptDisasterStart)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_earthquakeRegistered = registry::Register({
    "simulators.disasters.earthquake",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterInstanceEarthquake_Tick,
    "Our earthquake manager and instance (docs/disasters.md)",
    false,
    InstallEarthquake,
});

} // namespace
} // namespace opensc4::disasters
