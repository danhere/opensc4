// The volcano (docs/disasters.md): cSC4DisasterManagerVolcano (class
// 0xA9B5AB9F, disaster 0xC4C90997) and cSC4DisasterInstanceVolcano (class
// 0x29B5AB89): a state machine around the terrain's volcano builder and
// the eruption effect. Replacement, off by default:
//
//   simulators.disasters.volcano: the manager (QueryInterface, the start
//     at a cell or anywhere, making the instance, the height query) and the
//     instance (Init, Shutdown, the states, the tick, the effect, the
//     tuning, the location).
//
// The objects' constructors and destructors, the terrain builder
// (0x0075Dxxx-0x0075Fxxx) and the curves stay the game's. The instance's
// own interface is its first vtable; +4 is the volcano interface
// (0x29B5AB88) and +8 the message target.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <utility>

namespace opensc4::disasters {
namespace {

constexpr uint32_t kVolcano = 0xC4C90997;

namespace field { // the instance
constexpr std::size_t kRefs = 0xC;
constexpr std::size_t kX = 0x10;
constexpr std::size_t kZ = 0x14;
constexpr std::size_t kInited = 0x20;
constexpr std::size_t kState = 0x24;
constexpr std::size_t kManager = 0x28;
constexpr std::size_t kGround = 0x2C;    // city slot 0x150
constexpr std::size_t kEffects = 0x30;   // city slot 0x154
constexpr std::size_t kEffect = 0x34;
constexpr std::size_t kPosition = 0x38;  // two floats
constexpr std::size_t kBuilding = 0x40;
constexpr std::size_t kEffectOn = 0x41;
constexpr std::size_t kEffectOver = 0x42;
constexpr std::size_t kBuilt = 0x43;
constexpr std::size_t kRegistered = 0x44;
constexpr std::size_t kBuilder = 0x4C;
constexpr std::size_t kElapsed = 0x50;
} // namespace field

using ThisFn = void(__thiscall*)(void* self);

void* City()
{
    return Global<void*>(addr::SC4_City);
}

// The tuning's statics.
void*& Curve(int which)
{
    return Global<void*>(addr::cSC4DisasterInstanceVolcano_Statics + (which ? 0x0 : 0x4));
}

uint8_t& BuildAllowed()
{
    return Global<uint8_t>(addr::cSC4DisasterInstanceVolcano_Statics + 0x9);
}

float& Scale()
{
    return Global<float>(addr::cSC4DisasterInstanceVolcano_Tuning);
}

float& Duration()
{
    return Global<float>(addr::cSC4DisasterInstanceVolcano_Tuning + 0x4);
}

void SetState(char* volcano, int32_t state)
{
    Virtual<uint8_t>(volcano + 0x4, 0x20, state);
}

// The manager (`interface` at +8, the volcano manager interface at +0x24).

uint8_t __fastcall VolcanoManagerQueryInterface(char* manager, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == 0xA9B5AB9E) {
        void* const part = manager ? manager + 0x24 : nullptr;
        *out = part;
        if (part) {
            Virtual<uint32_t>(manager, 0x4);
            return 1;
        }
    }
    return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cSC4DisasterManager_QueryInterface)(
        manager, iid, out);
}

uint8_t __fastcall VolcanoCreateInstance(char* extra, void*, char** out)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x54);
    char* const made = memory ? reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4DisasterInstanceVolcano_Construct)(memory)
                              : nullptr;
    if (!made)
        return 0;
    Virtual<uint32_t>(made, 0x4);
    Virtual<uint32_t>(extra - 0x1C, 0x20, made); // AddInstanceToList
    *out = made + 0x4;
    return 1;
}

char* __fastcall VolcanoAttemptDisasterStart(char* interface, void*, int32_t x, int32_t z)
{
    char* const list = At<char*>(interface, 0xC);
    if (At<char*>(list, 0x0) != list)
        return nullptr;
    char* part = nullptr;
    if (!Virtual<uint8_t>(interface + 0x1C, 0x10, &part))
        return nullptr;
    char* const made = part ? part - 0x4 : nullptr;
    Virtual<uint32_t>(interface, 0x20, made);
    int32_t point[2] = {x, z};
    Virtual<uint8_t>(made + 0x4, 0x14, point); // SetLocation
    Virtual<uint8_t>(made, 0x24);              // Init
    SetState(made, 3);
    return made;
}

char* __fastcall VolcanoAttemptDisasterStartAnywhere(char* interface, void*)
{
    alignas(8) char random[0x18] = {};
    {
        // The clock seed counts under the original's call site.
        const fixed_seed::SiteOverride site(0x0057F5B2);
        reinterpret_cast<void*(__thiscall*)(void*, int32_t)>(addr::SC4Random_Construct)(random, -1);
    }
    using BetweenFn = int32_t(__thiscall*)(void*, int32_t, int32_t);
    const int32_t x = reinterpret_cast<BetweenFn>(addr::cRZRandom_Between)(random, 0, Virtual<int32_t>(City(), 0x174));
    const int32_t z = reinterpret_cast<BetweenFn>(addr::cRZRandom_Between)(random, 0, Virtual<int32_t>(City(), 0x178));
    return Virtual<char*>(interface, 0x18, x, z);
}

float __fastcall VolcanoManagerGetHeightAt(char* extra, void*, uint32_t a, uint32_t b)
{
    char* const list = At<char*>(extra - 0x10, 0x0);
    if (At<char*>(list, 0x0) == list)
        return 0.0f;
    void* volcano = nullptr;
    Virtual<uint8_t>(At<void*>(At<char*>(list, 0x0), 0x8), 0x0, uint32_t{0x29B5AB88}, &volcano);
    if (!volcano)
        return 0.0f;
    const float height = Virtual<float>(volcano, 0x18, a, b);
    Virtual<uint32_t>(volcano, 0x8);
    return height;
}

// The instance.

uint8_t __fastcall VolcanoQueryInterface(char* volcano, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == 0x22223BA4)
        *out = volcano;
    else if (iid == 0x29B5AB88)
        *out = volcano ? volcano + 0x4 : nullptr;
    else
        return 0;
    if (!*out)
        return 0;
    Virtual<uint32_t>(volcano, 0x4);
    return 1;
}

uint32_t __fastcall VolcanoRelease(char* volcano, void*)
{
    uint32_t& refs = At<uint32_t>(volcano, field::kRefs);
    if (refs > 1)
        return --refs;
    Virtual<void*>(volcano, 0x20, 1u);
    return 0;
}

char* __fastcall VolcanoGetManager(char* volcano, void*)
{
    char* const manager = At<char*>(volcano, field::kManager);
    return manager ? manager + 0x8 : nullptr;
}

int32_t __fastcall VolcanoGetPhase(char* volcano, void*)
{
    switch (At<uint32_t>(volcano, field::kState)) {
    case 0:
        return 0;
    case 2:
    case 3:
        return 1;
    case 4:
        return 3;
    default:
        return 2;
    }
}

void __fastcall BeginBuilder(char* volcano, void*)
{
    if (!BuildAllowed() || At<uint8_t>(volcano, field::kBuilding))
        return;
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x178);
    void* const builder = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4VolcanoBuilder_Construct)(memory)
                                 : nullptr;
    At<void*>(volcano, field::kBuilder) = builder;
    if (!builder)
        return;
    Virtual<uint32_t>(builder, 0x4);
    reinterpret_cast<ThisFn>(addr::cSC4VolcanoBuilder_Prepare)(At<void*>(volcano, field::kBuilder));
    Virtual<uint32_t>(volcano, 0x4);
    reinterpret_cast<void(__thiscall*)(void*, float)>(addr::cSC4VolcanoBuilder_SetRateA)(
        At<void*>(volcano, field::kBuilder), 0.0f);
    reinterpret_cast<void(__thiscall*)(void*, float)>(addr::cSC4VolcanoBuilder_SetRateB)(
        At<void*>(volcano, field::kBuilder), 0.0f);
}

void __fastcall StartBuilding(char* volcano, void*)
{
    if (!BuildAllowed() || At<uint8_t>(volcano, field::kBuilding))
        return;
    At<uint32_t>(volcano, field::kElapsed) = 0;
    const auto height = static_cast<float>(Global<uint32_t>(addr::cSC4DisasterInstanceVolcano_Tuning + 0x8));
    reinterpret_cast<void(__thiscall*)(void*, float, float, float, int32_t)>(addr::cSC4VolcanoBuilder_Start)(
        At<void*>(volcano, field::kBuilder), static_cast<float>(At<int32_t>(volcano, field::kX)),
        static_cast<float>(At<int32_t>(volcano, field::kZ)), height, 1);
    At<uint8_t>(volcano, field::kBuilding) = 1;
}

void __fastcall Finish(char* volcano, void*)
{
    if (At<uint8_t>(volcano, field::kBuilding)) {
        reinterpret_cast<ThisFn>(addr::cSC4VolcanoBuilder_Finish)(At<void*>(volcano, field::kBuilder));
        Virtual<uint32_t>(At<void*>(volcano, field::kBuilder), 0x8);
        At<uint8_t>(volcano, field::kBuilding) = 0;
        At<uint8_t>(volcano, field::kBuilt) = 1;
        Virtual<uint32_t>(volcano, 0x8);
    }
    if (At<uint8_t>(volcano, field::kRegistered)) {
        if (void* const layer = Virtual<void*>(City(), 0x120)) {
            Virtual<void>(layer, 0x24);              // DisasterHasEnded
            Virtual<uint8_t>(layer, 0x38, volcano);  // UnregisterDisasterLocations
        }
        At<uint8_t>(volcano, field::kRegistered) = 0;
    }
}

void __fastcall StartEffect(char* volcano, void*)
{
    if (At<uint8_t>(volcano, field::kEffectOn))
        return;
    Virtual<void>(volcano, 0x34, "volcano_control", volcano + field::kPosition, int32_t{1}, volcano + field::kEffect);
    At<uint8_t>(volcano, field::kEffectOn) = 1;
}

void __fastcall Remove(char* volcano, void*)
{
    if (At<uint8_t>(volcano, field::kBuilding))
        return;
    Virtual<uint32_t>(volcano, 0x4);
    Virtual<uint8_t>(At<char*>(volcano, field::kManager) + 0x8, 0x24, volcano); // RemoveInstanceFromList
    Virtual<uint8_t>(volcano, 0xC);                                             // Shutdown
    Virtual<uint32_t>(volcano, 0x8);
}

float __fastcall GetHeightAt(char* part, void*, uint32_t a, uint32_t b)
{
    void* const builder = At<void*>(part - 0x4, field::kBuilder);
    if (!builder)
        return 0.0f;
    return reinterpret_cast<float(__thiscall*)(void*, uint32_t, uint32_t)>(addr::cSC4VolcanoBuilder_HeightAt)(builder, a,
                                                                                                             b);
}

// A position's cell registered with the layer as the volcano's location.
void Register(char* volcano, void* layer, float x, float z)
{
    int32_t cellX = 0;
    int32_t cellZ = 0;
    Virtual<void>(City(), 0x17C, x, z, &cellX, &cellZ);
    Virtual<uint8_t>(layer, 0x34, volcano, cellX, cellZ, int32_t{0}, int32_t{0}, int32_t{0});
}

uint8_t __fastcall SetLocation(char* part, void*, const int32_t* point)
{
    char* const volcano = part - 0x4;
    At<int32_t>(volcano, field::kX) = point[0];
    At<int32_t>(volcano, field::kZ) = point[1];
    void* const city = City();
    int32_t cellX = 0;
    int32_t cellZ = 0;
    Virtual<void>(city, 0x17C, static_cast<float>(point[0]), static_cast<float>(point[1]), &cellX, &cellZ);
    Virtual<uint8_t>(Virtual<void*>(city, 0x120), 0x34, volcano, cellX, cellZ, int32_t{0}, int32_t{0}, int32_t{0});
    return 1;
}

uint8_t __fastcall SetStateSlot(char* part, void*, int32_t state)
{
    char* const volcano = part - 0x4;
    if (state == At<int32_t>(volcano, field::kState))
        return 0;
    At<int32_t>(volcano, field::kState) = state;
    switch (state) {
    case 3:
        Virtual<void>(volcano, 0x28); // StartEffect
        if (BuildAllowed() && !At<uint8_t>(volcano, field::kBuilt) && !At<uint8_t>(volcano, field::kBuilding)) {
            reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceVolcano_BeginBuilder)(volcano);
            Virtual<uint8_t>(part, 0x20, int32_t{5});
        }
        break;
    case 5:
        if (BuildAllowed() && !At<uint8_t>(volcano, field::kBuilding))
            reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceVolcano_StartBuilding)(volcano);
        break;
    case 7:
        At<uint8_t>(volcano, field::kEffectOver) = 1;
        Virtual<uint8_t>(part, 0x20, BuildAllowed() && At<uint8_t>(volcano, field::kBuilding) ? int32_t{5} : int32_t{8});
        break;
    case 8:
        if (BuildAllowed())
            reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceVolcano_Finish)(volcano);
        Virtual<void>(volcano, 0x2C); // Remove
        break;
    default:
        break;
    }
    return 1;
}

uint8_t __fastcall VolcanoShutdown(char* volcano, void*)
{
    if (!At<uint8_t>(volcano, field::kInited))
        return 0;
    At<uint8_t>(volcano, field::kInited) = 0;
    char* const target = volcano + 0x8;
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x18, target, int32_t{0xC0});
    if (At<uint8_t>(volcano, field::kEffectOn)) {
        if (void* const effect = At<void*>(volcano, field::kEffect)) {
            Virtual<void>(effect, 0x10, int32_t{1});
            Virtual<uint32_t>(At<void*>(volcano, field::kEffect), 0x8);
        }
        At<uint8_t>(volcano, field::kEffectOn) = 0;
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<void>(server, 0x18, target, uint32_t{0xCA493C1D});
    if (At<uint8_t>(volcano, field::kBuilding) || At<uint8_t>(volcano, field::kRegistered))
        reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceVolcano_Finish)(volcano);
    if (void* const manager = Virtual<void*>(volcano, 0x10))
        Virtual<uint8_t>(manager, 0x24, volcano);
    for (std::size_t at : {field::kGround, field::kManager, field::kEffects}) {
        if (void* const object = At<void*>(volcano, at)) {
            Virtual<uint32_t>(object, 0x8);
            At<void*>(volcano, at) = nullptr;
        }
    }
    Virtual<uint32_t>(volcano, 0x8);
    return 1;
}

// The game's 0x0057F9F0.
void __cdecl VolcanoLoadTuning()
{
    uint32_t key[3] = {0x6534284A, 0x29B6C670, 1};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, uint32_t{0xA52160F5}, &exemplar, int32_t{0},
                          int32_t{0}))
        return;
    void* const properties = Virtual<void*>(exemplar, 0x1C);
    using LoadFn = void(__thiscall*)(void*, void*);
    reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromProperty)(Curve(0), Virtual<void*>(properties, 0x24, uint32_t{0xEA4BC6E4}));
    reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromProperty)(Curve(1), Virtual<void*>(properties, 0x24, uint32_t{0x4A4BC714}));
    using FloatFn = uint8_t(__cdecl*)(void*, uint32_t, float*);
    reinterpret_cast<FloatFn>(addr::SC4_GetPropertyFloat)(properties, 0x0A4BC742, &Duration());
    reinterpret_cast<FloatFn>(addr::SC4_GetPropertyFloat)(properties, 0xAA4BC72A, &Scale());
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint32_t*)>(addr::SC4_GetPropertyUint32)(
        properties, 0x0A4BC752, &Global<uint32_t>(addr::cSC4DisasterInstanceVolcano_Tuning + 0x8));
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyBool)(properties, 0x6A4BC8ED,
                                                                                             &BuildAllowed());
    BuildAllowed() = 0;
    if (void* const property = Virtual<void*>(properties, 0x24, uint32_t{0x6A4BC8ED}))
        BuildAllowed() = *Virtual<const uint8_t*>(Virtual<void*>(property, 0x14), 0xFC);
    Virtual<uint32_t>(exemplar, 0x8);
}

// The game's 0x0057FFE0 (its argument is not used).
uint8_t __cdecl StaticInit(void*)
{
    uint8_t& made = Global<uint8_t>(addr::cSC4DisasterInstanceVolcano_Statics + 0x8);
    if (made)
        return 1;
    made = 1;
    Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x114,
                  reinterpret_cast<void*>(addr::cSC4DisasterInstanceVolcano_StaticDestroy), int32_t{0});
    for (int which : {0, 1}) {
        if (Curve(which))
            continue;
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x10);
        Curve(which) = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4Curve_Init)(memory) : nullptr;
    }
    return 1;
}

// A coordinate kept inside the city: below 0 becomes 0, past the size
// less 1 becomes that.
void Clamp(char* volcano, std::size_t at, std::size_t sizeSlot)
{
    int32_t& value = At<int32_t>(volcano, at);
    if (value < 0) {
        value = 0;
        return;
    }
    const float asFloat = static_cast<float>(value);
    if (static_cast<double>(Virtual<float>(City(), sizeSlot)) - 1.0 < static_cast<double>(asFloat))
        value = static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(Virtual<float>(City(), sizeSlot)) - 1.0));
}

uint8_t __fastcall VolcanoInit(char* volcano, void*)
{
    At<uint8_t>(volcano, field::kInited) = 1;
    reinterpret_cast<uint8_t(__cdecl*)(void*)>(addr::cSC4DisasterInstanceVolcano_StaticInit)(volcano);
    void* const city = City();
    void* const ground = Virtual<void*>(city, 0x150);
    At<void*>(volcano, field::kGround) = ground;
    if (ground)
        Virtual<uint32_t>(ground, 0x4);
    void* const layer = Virtual<void*>(city, 0x120);
    if (char* const manager = Virtual<char*>(layer, 0x1C, kVolcano)) {
        At<char*>(volcano, field::kManager) = manager - 0x8;
        Virtual<uint32_t>(manager - 0x8, 0x4);
    }
    void* const effects = Virtual<void*>(city, 0x154);
    At<void*>(volcano, field::kEffects) = effects;
    if (effects)
        Virtual<uint32_t>(effects, 0x4);
    char* const target = volcano + 0x8;
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<void>(server, 0x14, target, uint32_t{0xCA493C1D});
    Clamp(volcano, field::kX, 0x164);
    Clamp(volcano, field::kZ, 0x168);
    reinterpret_cast<void(__cdecl*)()>(addr::cSC4DisasterInstanceVolcano_LoadTuning)();
    At<uint32_t>(volcano, field::kElapsed) = 0;
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x14, target, int32_t{0xC0});
    At<float>(volcano, field::kPosition) = static_cast<float>(At<int32_t>(volcano, field::kX));
    At<float>(volcano, field::kPosition + 4) = static_cast<float>(At<int32_t>(volcano, field::kZ));
    SetState(volcano, 1);
    Register(volcano, layer, At<float>(volcano, field::kPosition), At<float>(volcano, field::kPosition + 4));
    Virtual<uint32_t>(layer, 0x20, volcano); // DisasterHasBegun
    At<uint8_t>(volcano, field::kRegistered) = 1;
    return 1;
}

int32_t Points(void* curve)
{
    return static_cast<int32_t>((At<char*>(curve, 0x4) - At<char*>(curve, 0x0)) / 20);
}

// The game's 0x0057FB90.
void __fastcall VolcanoTick(char* volcano, void*, uint32_t passed)
{
    if (!BuildAllowed() || !At<uint8_t>(volcano, field::kBuilding))
        return;
    const uint32_t elapsed = At<uint32_t>(volcano, field::kElapsed) += passed;
    const double exact = static_cast<double>(elapsed) * static_cast<double>(Global<float>(addr::SC4_FloatMillionth));
    const float seconds = static_cast<float>(exact);
    if (exact > static_cast<double>(Duration())) {
        reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceVolcano_Finish)(volcano);
        SetState(volcano, At<uint8_t>(volcano, field::kEffectOver) ? 8 : 3);
        return;
    }
    if (At<int32_t>(volcano, field::kState) != 5)
        return;
    using EvaluateFn = float(__thiscall*)(void*, float);
    const double tiny = static_cast<double>(Global<float>(addr::SC4_FloatHundredThousandth));
    float rateA = 0.0f;
    float rateB = 0.0f;
    if (Points(Curve(0))) {
        const float at = static_cast<float>(static_cast<double>(seconds) / static_cast<double>(Duration()));
        rateA = static_cast<float>(static_cast<double>(reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(Curve(0), at)) *
                                   static_cast<double>(Scale()) * tiny);
    }
    if (Points(Curve(0))) {
        const float at = static_cast<float>(static_cast<double>(seconds) / static_cast<double>(Duration()));
        rateB = static_cast<float>(
            static_cast<double>(reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(Curve(1), at)) * tiny);
    }
    reinterpret_cast<void(__thiscall*)(void*, float)>(addr::cSC4VolcanoBuilder_SetRateA)(At<void*>(volcano, field::kBuilder),
                                                                                         rateA);
    reinterpret_cast<void(__thiscall*)(void*, float)>(addr::cSC4VolcanoBuilder_SetRateB)(At<void*>(volcano, field::kBuilder),
                                                                                         rateB);
}

// The game's 0x0057FCE0.
void __fastcall VolcanoMakeEffect(char* volcano, void*, const char* name, const float* position, uint8_t keep, void** out)
{
    *out = nullptr;
    if (!Virtual<uint8_t>(At<void*>(volcano, field::kEffects), 0x1C, name, out))
        return;
    struct {
        uint8_t a;
        uint8_t b;
        uint8_t pad[2];
        float matrix[9];
        float position[3];
        float scale;
    } placement{};
    placement.matrix[0] = 1.0f;
    placement.matrix[4] = 1.0f;
    placement.matrix[8] = 1.0f;
    placement.scale = 1.0f;
    Virtual<void>(*out, 0x28, uint32_t{0xCA493C1D}, volcano);
    const float x = position[0];
    const float z = position[1];
    placement.position[1] = Virtual<float>(At<void*>(volcano, field::kGround), 0x34, x, z);
    placement.position[0] = x;
    placement.position[2] = z;
    placement.b |= 1;
    ++placement.a;
    Virtual<void>(*out, 0x20, &placement);
    Virtual<void>(*out, 0xC, int32_t{0});
    if (!keep) {
        Virtual<uint32_t>(*out, 0x8);
        *out = nullptr;
    }
}

uint8_t __fastcall VolcanoDoMessage(char* target, void*, void* message)
{
    char* const volcano = target - 0x8;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, uint32_t{0x65297976}, &standard);
    const uint32_t type = Virtual<uint32_t>(standard, 0x10);
    if (type == 0x86AD10EE) {
        const uint32_t passed = Virtual<uint32_t>(standard, 0x38);
        Virtual<void>(volcano, 0x38, passed); // Tick
    } else if (type == 0xCA493C1D) {
        if (Virtual<char*>(standard, 0x24) == volcano)
            SetState(volcano, 7);
    }
    if (standard)
        Virtual<uint32_t>(standard, 0x8);
    return 1;
}

bool InstallVolcano(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterManagerVolcano_QueryInterface, reinterpret_cast<void*>(&VolcanoManagerQueryInterface)},
        {addr::cSC4DisasterInstanceVolcano_BeginBuilder, reinterpret_cast<void*>(&BeginBuilder)},
        {addr::cSC4DisasterInstanceVolcano_StartBuilding, reinterpret_cast<void*>(&StartBuilding)},
        {addr::cSC4DisasterInstanceVolcano_GetManager, reinterpret_cast<void*>(&VolcanoGetManager)},
        {addr::cSC4DisasterInstanceVolcano_GetPhase, reinterpret_cast<void*>(&VolcanoGetPhase)},
        {addr::cSC4DisasterInstanceVolcano_QueryInterface, reinterpret_cast<void*>(&VolcanoQueryInterface)},
        {addr::cSC4DisasterInstanceVolcano_Release, reinterpret_cast<void*>(&VolcanoRelease)},
        {addr::cSC4DisasterInstanceVolcano_StartEffect, reinterpret_cast<void*>(&StartEffect)},
        {addr::cSC4DisasterInstanceVolcano_Remove, reinterpret_cast<void*>(&Remove)},
        {addr::cSC4DisasterInstanceVolcano_GetHeightAt, reinterpret_cast<void*>(&GetHeightAt)},
        {addr::cSC4DisasterManagerVolcano_AttemptDisasterStartAnywhere,
         reinterpret_cast<void*>(&VolcanoAttemptDisasterStartAnywhere)},
        {addr::cSC4DisasterManagerVolcano_AttemptDisasterStart, reinterpret_cast<void*>(&VolcanoAttemptDisasterStart)},
        {addr::cSC4DisasterInstanceVolcano_DoMessage, reinterpret_cast<void*>(&VolcanoDoMessage)},
        {addr::cSC4DisasterInstanceVolcano_Finish, reinterpret_cast<void*>(&Finish)},
        {addr::cSC4DisasterInstanceVolcano_SetLocation, reinterpret_cast<void*>(&SetLocation)},
        {addr::cSC4DisasterInstanceVolcano_SetState, reinterpret_cast<void*>(&SetStateSlot)},
        {addr::cSC4DisasterInstanceVolcano_Shutdown, reinterpret_cast<void*>(&VolcanoShutdown)},
        {addr::cSC4DisasterInstanceVolcano_LoadTuning, reinterpret_cast<void*>(&VolcanoLoadTuning)},
        {addr::cSC4DisasterInstanceVolcano_Tick, reinterpret_cast<void*>(&VolcanoTick)},
        {addr::cSC4DisasterInstanceVolcano_MakeEffect, reinterpret_cast<void*>(&VolcanoMakeEffect)},
        {addr::cSC4DisasterManagerVolcano_CreateInstance, reinterpret_cast<void*>(&VolcanoCreateInstance)},
        {addr::cSC4DisasterManagerVolcano_GetHeightAt, reinterpret_cast<void*>(&VolcanoManagerGetHeightAt)},
        {addr::cSC4DisasterInstanceVolcano_StaticInit, reinterpret_cast<void*>(&StaticInit)},
        {addr::cSC4DisasterInstanceVolcano_Init, reinterpret_cast<void*>(&VolcanoInit)},
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

const bool g_volcanoRegistered = registry::Register({
    "simulators.disasters.volcano",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterInstanceVolcano_Tick,
    "Our volcano manager and instance (docs/disasters.md)",
    false,
    InstallVolcano,
});

} // namespace
} // namespace opensc4::disasters
