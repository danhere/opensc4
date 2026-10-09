// The budget simulator's Init, tuning and Shutdown (docs/budget.md).
#include "core/address_check.h"
#include "core/budget_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::budget_sim {
namespace {

using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void* FromText(GameString* string, const char* text)
{
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    return reinterpret_cast<StringFn>(addr::SC4String_FromText)(string, text);
}

void Destroy(GameString& string)
{
    string.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (string.begin)
        Free(string.begin);
}

void Reference(void* self, std::size_t offset, std::uintptr_t global)
{
    if (At<void*>(self, offset))
        return;
    void* const object = Global<void*>(global);
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4);
}

void Drop(void* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// The script service (0xEBE0E860, interface 0xE898ED03) into `holder`.
void GetScriptService(void** holder)
{
    using GetFrameworkFn = void*(__cdecl*)();
    *holder = nullptr;
    if (void* const framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
        Virtual<uint8_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, holder);
}

void ReleaseHolder(void** holder)
{
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4_ReleaseRefHolder)(holder);
}

constexpr uint32_t kMessages[] = {0x26D31EC4, 0x29222F02, 0x08DAC3E8, 0x08DAC3E7};
constexpr uint32_t kCheats[] = {0xCBF31943, 0x4AB87766, 0x493F4FE6};

// --- 0x00692210: the tuning ------------------------------------------------------

void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    char* const base = static_cast<char*>(self);
    At<float>(self, field::kDefaultGroupValue) = 25.0f;
    At<int64_t>(self, field::kMinFunds) = -100000;
    At<int64_t>(self, field::kBondIncrement) = 1000;
    At<int32_t>(self, field::kBondsAllowed) = 5;
    At<int64_t>(self, field::kBondLimit) = 10000;
    At<uint32_t>(self, field::kInterestRate) = 0x3DAE147B; // 0.085
    At<uint32_t>(self, field::kLoanMonths) = 120;
    using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const float* value);
    const auto resize = reinterpret_cast<ResizeFn>(addr::SC4FloatVector_Resize2);
    float value = 1.0f;
    resize(base + field::kWealthScale, 3, &value);
    value = 100000.0f;
    resize(base + field::kMonthShares, 3, &value);
    uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0x0BFF6824};
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &resource, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(resource, 0x1C);
    using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t id, void* value);
    const auto get = [&](std::uintptr_t fn, uint32_t id, void* to) { return reinterpret_cast<GetFn>(fn)(holder, id, to); };
    get(addr::SC4_GetPropertySint64Vector, 0x6A9C7718, base + field::kStartingFunds);
    get(addr::SC4_GetPropertyFloat, 0x890E0A99, base + field::kDefaultGroupValue);
    get(addr::SC4_GetPropertySint64, 0x090E0A9F, base + field::kMinFunds);
    get(addr::SC4_GetPropertySint64, 0x490E0AA6, base + field::kBondIncrement);
    get(addr::SC4_GetPropertySint32, 0x090E0AAC, base + field::kBondsAllowed);
    get(addr::SC4_GetPropertySint64, 0x890E0AB4, base + field::kBondLimit);
    if (get(addr::SC4_GetPropertyFloat, 0x8A3FB98D, base + field::kInterestRate))
        At<float>(self, field::kInterestRate) =
            static_cast<float>(static_cast<double>(At<float>(self, field::kInterestRate)) * 0.01f);
    get(addr::SC4_GetPropertyUint32, 0x0A3FB9A8, base + field::kLoanMonths);
    get(addr::SC4_GetPropertyFloat, 0x89170C6C, reinterpret_cast<void*>(addr::Budget_Tuning1));
    get(addr::SC4_GetPropertyFloat, 0x09170C74, reinterpret_cast<void*>(addr::Budget_Tuning2));
    get(addr::SC4_GetPropertyFloat, 0xC9170C78, reinterpret_cast<void*>(addr::Budget_Tuning3));
    get(addr::SC4_GetPropertyUint8, 0x49170C7C, reinterpret_cast<void*>(addr::Budget_Tuning4));
    struct {
        uint32_t id;
        std::size_t offset;
    } const factors[] = {{0xA9188EF5, 0x7C8}, {0xE918C920, 0x7CC}, {0x4918C925, 0x7D0}, {0xA918C929, 0x7D4},
                         {0x4918C92C, 0x7D8}, {0x4918C930, 0x7E4}, {0x8918C937, 0x7E0}, {0x4918C93A, 0x7E4},
                         {0x69A36E40, 0x7E8}, {0xA918C942, 0x7EC}, {0x8918C93E, 0x7F0}, {0xA9A36E47, 0x7F4}};
    for (const auto& factor : factors)
        get(addr::SC4_GetPropertyFloat, factor.id, base + factor.offset);
    get(addr::SC4_GetPropertyFloatVector, 0x8A480DF7, base + field::kWealthScale);
    get(addr::SC4_GetPropertyFloatVector, 0xEBFF69E7, base + field::kMonthShares);
    get(addr::SC4_GetPropertyFloatVector, 0x0C032488, base + field::kStartingRates);
    using CurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
    reinterpret_cast<CurveFn>(addr::SC4Curve_LoadFromExemplar)(base + field::kLandValueCurve, key, 0x4AC519B8);
    Virtual<uint32_t>(resource, 0x8);
}

// --- Init (slot 3, 0x00692FF0) ------------------------------------------------------

uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("budget init: first Init (%s)", At<uint8_t>(self, field::kLoaded) ? "loaded from a save" : "new");
    }
    if (At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 1;
    if (!Global<uint8_t>(addr::BudgetSimulator_Started))
        Global<uint8_t>(addr::BudgetSimulator_Started) = 1;
    reinterpret_cast<ThisFn>(addr::cSC4BudgetSimulator_LoadTuning)(self);
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    GameString name{};
    FromText(&name, reinterpret_cast<const char*>(addr::BudgetSimulator_Name));
    void* const target = static_cast<char*>(self) + 0x4;
    for (const uint32_t phase : {0u, 6u, 4u})
        Virtual<uint32_t>(simulators, 0x4C, target, phase, &name, 2u);
    if (!At<void*>(self, field::kMessageServer)) {
        At<void*>(self, field::kMessageServer) = Global<void*>(addr::SC4_MessageServer);
        Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x4);
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, target, message);
    }
    Reference(self, field::kClock, addr::SC4_City);
    Reference(self, field::kLandValues, addr::SC4_Service43D88);
    Reference(self, field::kTrade, addr::SC4_Service43D90);
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        const auto add = [&](uint32_t code, std::uintptr_t text) {
            GameString word{};
            void* const made = FromText(&word, Global<const char*>(text));
            Virtual<uint32_t>(cheats, 0xC, code, made);
            Destroy(word);
            Virtual<uint32_t>(cheats, 0x1C, target, code);
        };
        add(kCheats[0], addr::Budget_CheatName1);
        add(kCheats[1], addr::Budget_CheatName2);
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54))
            add(kCheats[2], addr::Budget_CheatName3);
    }
    if (!At<uint8_t>(self, field::kLoaded)) {
        // A new city: the starting funds and rates by the level.
        const int32_t level = Virtual<int32_t>(At<void*>(self, field::kClock), 0x60);
        const int64_t funds = At<const int64_t*>(self, field::kStartingFunds)[level];
        At<int64_t>(self, field::kFunds) = funds;
        At<int64_t>(self, field::kFundsShown) = funds;
        reinterpret_cast<ThisFn>(addr::cSC4BudgetSimulator_NotifyFunds)(self);
        for (uint32_t group = 0; group < kTaxGroups; ++group) {
            char* const g = static_cast<char*>(self) + group * kGroupSize;
            const int32_t at = Virtual<int32_t>(At<void*>(self, field::kClock), 0x60);
            At<float>(g, field::kGroupRate) = At<const float*>(self, field::kStartingRates)[at];
            At<float>(g, 0x74) = At<float>(self, field::kDefaultGroupValue);
            At<uint32_t>(g, 0x78) = 0;
            At<uint32_t>(g, 0x7C) = 0;
            // The game's forward dword copy of +0x78.. onto +0x80.. (22 dwords),
            // overlapping, then six more dwords cleared.
            auto* const words = reinterpret_cast<uint32_t*>(g + 0x78);
            for (int i = 0; i < 22; ++i)
                words[i + 2] = words[i];
            for (std::size_t offset = 0xD8; offset < 0xF0; offset += 4)
                At<uint32_t>(g, offset) = 0;
        }
        void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
        for (uint32_t group = 0; group < kTaxGroups; ++group)
            Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x34, group + 0xAABAAA9Fu, 0x16Du);
        Virtual<uint32_t>(figures, 0x30, 0x4A5BA3D0u, static_cast<uint32_t>(At<int64_t>(self, field::kFunds) / 1000));
    }
    if (At<uint8_t>(self, field::kGroup8Fixed)) {
        const int32_t level = Virtual<int32_t>(At<void*>(self, field::kClock), 0x60);
        At<float>(self, field::kGroupRate + 8 * kGroupSize) = At<const float*>(self, field::kStartingRates)[level];
    }
    using RegisterTableFn = uint32_t(__cdecl*)(void* self, const char* name, uint32_t, uint32_t, uint32_t, void* service);
    using RegisterVariableFn = uint32_t(__cdecl*)(void* value, const char* name, std::uintptr_t callback, void* self,
                                                  uint32_t, void* self2, void* service);
    using RegisterFunctionFn = uint32_t(__cdecl*)(std::uintptr_t callback, const char* name, std::uintptr_t callback2,
                                                  void* self, uint32_t, void* self2, void* service);
    void* script;
    GetScriptService(&script);
    reinterpret_cast<RegisterTableFn>(addr::SC4Script_RegisterTable)(
        self, reinterpret_cast<const char*>(addr::Budget_ScriptTable), 0, 0, 0, script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterVariableFn>(addr::SC4Script_RegisterVariable)(
        static_cast<char*>(self) + field::kFunds, reinterpret_cast<const char*>(addr::Budget_ScriptFunds),
        addr::cSC4BudgetSimulator_ScriptFunds, self, 0, self, script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterFunctionFn>(addr::SC4Script_RegisterFunction)(
        addr::cSC4BudgetSimulator_ScriptDeposit, reinterpret_cast<const char*>(addr::Budget_ScriptDeposit),
        addr::cSC4BudgetSimulator_ScriptDeposit, self, 0, self, script);
    ReleaseHolder(&script);
    Destroy(name);
    return 1;
}

// --- Shutdown (slot 4, 0x00690CB0) -------------------------------------------------

uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 0;
    void* script;
    GetScriptService(&script);
    using UnregisterFn = uint32_t(__cdecl*)(void* self, void* service);
    reinterpret_cast<UnregisterFn>(addr::SC4Script_UnregisterTable)(self, script);
    ReleaseHolder(&script);
    void* const target = static_cast<char*>(self) + 0x4;
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        for (const uint32_t code : kCheats) {
            Virtual<uint32_t>(cheats, 0x20, target, code);
            Virtual<uint32_t>(cheats, 0x10, code);
        }
    }
    Drop(self, field::kClock);
    if (void* const server = At<void*>(self, field::kMessageServer)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, target, message);
        Drop(self, field::kMessageServer);
        (void)server;
    }
    Drop(self, field::kLandValues);
    Drop(self, field::kTrade);
    char* const header = At<char*>(self, field::kDepartments);
    for (char* node = At<char*>(header, 0x8); node != At<char*>(self, field::kDepartments); node = MapNext(node)) {
        void* const department = At<void*>(node, 0x14);
        reinterpret_cast<ThisFn>(addr::cSC4DepartmentBudget_Shutdown)(department);
        Virtual<uint32_t>(department, 0x8);
    }
    if (At<uint32_t>(self, field::kDepartments + 4)) {
        using EraseFn = void(__thiscall*)(void* map, char* node);
        char* const map = static_cast<char*>(self) + field::kDepartments;
        reinterpret_cast<EraseFn>(addr::DepartmentMap_EraseNodes)(map, At<char*>(At<char*>(map, 0), 0x4));
        char* const h = At<char*>(map, 0);
        At<char*>(h, 0x8) = h;
        At<char*>(h, 0x4) = nullptr;
        At<char*>(h, 0xC) = h;
        At<uint32_t>(map, 0x4) = 0;
    }
    if (void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(service, 0x50, target);
    return 1;
}

using SelfFn = void(__fastcall*)(void*, void*);
SelfFn g_originalTuning = nullptr;

const Slot kInitSlots[] = {{3, 0x00692FF0, reinterpret_cast<void*>(&Init)}};
const Slot kShutdownSlots[] = {{4, 0x00690CB0, reinterpret_cast<void*>(&Shutdown)}};

bool InstallInit(registry::Mode)
{
    const addr::Entry* tuning = addr_check::Find(addr::cSC4BudgetSimulator_LoadTuning);
    return game::IsBuild641() && tuning && hook::Install(*tuning, &LoadTuning, &g_originalTuning) &&
           SwapSlots("budget init", addr::cSC4BudgetSimulator_vftable, kInitSlots, 1);
}

bool InstallShutdown(registry::Mode)
{
    return SwapSlots("budget shutdown", addr::cSC4BudgetSimulator_vftable, kShutdownSlots, 1);
}

const bool g_registeredInit = registry::Register({
    "simulators.budget.init",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable,
    "Our budget Init and tuning load (docs/budget.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.budget.shutdown",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable,
    "Our budget Shutdown (docs/budget.md)",
    false,
    InstallShutdown,
});

} // namespace
} // namespace opensc4::budget_sim
