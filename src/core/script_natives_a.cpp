// The game's native script functions, first part (0x00409690 to 0x0040A78C):
// the trend, ordinance, advice, difficulty, building and MySim functions the
// advisors' scripts call. Each one is `int fn(lua_State*)`; the script
// context (cISCLua) comes from the object stored in front of the state.
// Replacement, off by default:
//
//   scripts.natives.a: the 22 natives below and the helper the three
//     ordinance fee functions share (0x00409CB0).
//
// The date objects' functions, the message constructor and the allocator
// stay the game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <utility>

namespace opensc4::script_natives {
namespace {

constexpr uint32_t kScriptContext = 0xEA10C4A2;

// cISCLua slots.
namespace lua {
constexpr std::size_t kGetTop = 0x18;
constexpr std::size_t kType = 0x30;
constexpr std::size_t kIsNumber = 0x38;
constexpr std::size_t kToNumber = 0x5C;
constexpr std::size_t kToObject = 0x70;
constexpr std::size_t kPushNil = 0x78;
constexpr std::size_t kPushNumber = 0x7C;
constexpr std::size_t kPushUserData = 0x114;
constexpr std::size_t kIsUserData = 0x124;
} // namespace lua

// A date object: two vtables, the day number at +4. Its second interface
// (+8) is what the game hands around.
struct Date {
    uint32_t words[4];
};

int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// x87 fpatan of (y, 1), as the game executes it (53-bit precision).
double X87Atan(double y)
{
    double result;
    __asm {
        fld y
        fld1
        fpatan
        fstp result
    }
    return result;
}

void* Context(char* state)
{
    void* context = nullptr;
    if (void* const owner = *reinterpret_cast<void**>(state - 4))
        Virtual<uint8_t>(owner, 0x0, kScriptContext, &context);
    return context;
}

void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

void* Advisors()
{
    return Global<void*>(addr::cSC4AdvisorSystem_Instance);
}

int32_t NumberArgument(void* context, int32_t index)
{
    return Ftol(Virtual<double>(context, lua::kToNumber, index));
}

void DateFromDay(Date* date, uint32_t day)
{
    reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::cRZDate_FromDay)(date, day);
}

void DateConstruct(Date* date, uint32_t day)
{
    reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::cRZDate_cRZDate)(date, day);
}

uint32_t DateDay(Date* date)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cRZDate_Day)(&date->words[2]);
}

void DateDestroy(Date* date)
{
    reinterpret_cast<void(__thiscall*)(void*)>(addr::cRZDate_Destroy)(date);
}

// The opening of the three trend functions: both services exist and the
// two arguments on top of the stack are numbers.
bool TrendArguments(void* context)
{
    if (!Global<void*>(addr::SC4_SimulatorService) || !Global<void*>(addr::SC4_MonthlyFigures))
        return false;
    if (Virtual<int32_t>(context, lua::kGetTop) <= 1)
        return false;
    if (Virtual<int32_t>(context, lua::kType, int32_t{-1}) != 3)
        return false;
    return Virtual<int32_t>(context, lua::kType, int32_t{-2}) == 3;
}

// The game's 0x00409690: game.trend_slope(figure, months).
int32_t __cdecl TrendSlope(char* state)
{
    void* const context = Context(state);
    float slope = 0.0f;
    uint32_t extra = 0;
    if (TrendArguments(context)) {
        const uint32_t months = static_cast<uint32_t>(NumberArgument(context, -1));
        const int32_t figure = NumberArgument(context, -2);
        Date now;
        Date then;
        DateFromDay(&now, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
        DateConstruct(&then, DateDay(&now) - months * 30u);
        Virtual<void>(Global<void*>(addr::SC4_MonthlyFigures), 0x18, figure, &then.words[2], &now.words[2], &slope,
                      &extra);
        slope = static_cast<float>(X87Atan(static_cast<double>(slope)) * static_cast<double>(0x1.fd4b6ep+5f));
        DateDestroy(&then);
        DateDestroy(&now);
    }
    Virtual<void>(context, lua::kPushNumber, static_cast<double>(slope));
    Release(context);
    return 1;
}

// The game's 0x004097E0: game.trend_value(figure, months ago).
int32_t __cdecl TrendValue(char* state)
{
    void* const context = Context(state);
    float value = 0.0f;
    if (TrendArguments(context)) {
        const uint32_t months = static_cast<uint32_t>(NumberArgument(context, -1));
        const int32_t figure = NumberArgument(context, -2);
        Date now;
        Date then;
        DateFromDay(&now, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
        DateConstruct(&then, DateDay(&now) - months * 30u);
        Virtual<void>(Global<void*>(addr::SC4_MonthlyFigures), 0x20, figure, &then.words[2], &value);
        DateDestroy(&then);
        DateDestroy(&now);
    }
    Virtual<void>(context, lua::kPushNumber, static_cast<double>(value));
    Release(context);
    return 1;
}

// The game's 0x00409900: game.trend_count(figure, months). The result is
// read back as a float whatever the service stored.
int32_t __cdecl TrendCount(char* state)
{
    void* const context = Context(state);
    float count = 0.0f;
    if (TrendArguments(context)) {
        const uint32_t months = static_cast<uint32_t>(NumberArgument(context, -1));
        const int32_t figure = NumberArgument(context, -2);
        Date now;
        Date then;
        DateFromDay(&now, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
        const double day = static_cast<double>(DateDay(&now));
        const int32_t span = static_cast<int32_t>(months * 30u - 1u);
        DateConstruct(&then, static_cast<uint32_t>(Ftol(day - static_cast<double>(span))));
        Virtual<void>(Global<void*>(addr::SC4_MonthlyFigures), 0x14, figure, &then.words[2], &now.words[2], &count);
        DateDestroy(&then);
        DateDestroy(&now);
    }
    Virtual<void>(context, lua::kPushNumber, static_cast<double>(count));
    Release(context);
    return 1;
}

// The opening of the ordinance functions: the ordinance simulator, when
// there is one and the top of the stack is a number.
void* OrdinanceArgument(void* context)
{
    Virtual<void*>(Advisors(), 0x50);
    void* const ordinances = Global<void*>(addr::SC4_Service43D90);
    if (!ordinances)
        return nullptr;
    if (Virtual<int32_t>(context, lua::kGetTop) <= 0)
        return nullptr;
    if (!Virtual<uint8_t>(context, lua::kIsNumber, int32_t{-1}))
        return nullptr;
    return ordinances;
}

// 1 when the simulator's slot answers true for the ordinance, nil otherwise.
int32_t OrdinanceQuery(char* state, std::size_t slot)
{
    void* const context = Context(state);
    void* const ordinances = OrdinanceArgument(context);
    if (ordinances && Virtual<uint8_t>(ordinances, slot, NumberArgument(context, -1)))
        Virtual<void>(context, lua::kPushNumber, 1.0);
    else
        Virtual<void>(context, lua::kPushNil);
    Release(context);
    return 1;
}

int32_t OrdinanceSwitch(char* state, int32_t on)
{
    void* const context = Context(state);
    if (void* const ordinances = OrdinanceArgument(context))
        Virtual<void>(ordinances, 0x34, NumberArgument(context, -1), on);
    Release(context);
    return 0;
}

// The game's 0x00409A50: game.ordinance_is_on(id).
int32_t __cdecl OrdinanceIsOn(char* state)
{
    return OrdinanceQuery(state, 0x20);
}

// The game's 0x00409B00: game.ordinance_set_off(id).
int32_t __cdecl OrdinanceSetOff(char* state)
{
    return OrdinanceSwitch(state, 0);
}

// The game's 0x00409B80: game.ordinance_set_on(id).
int32_t __cdecl OrdinanceSetOn(char* state)
{
    return OrdinanceSwitch(state, 1);
}

// The game's 0x00409C00: game.ordinance_is_available(id).
int32_t __cdecl OrdinanceIsAvailable(char* state)
{
    return OrdinanceQuery(state, 0x1C);
}

// The game's 0x00409CB0: one of an ordinance's three 64-bit amounts
// (0 enactment, 1 retraction, 2 monthly), 0 when there is no such ordinance.
int32_t __cdecl OrdinanceCost(char* state, int32_t which)
{
    void* const context = Context(state);
    void* const ordinances = OrdinanceArgument(context);
    void* const ordinance =
        ordinances ? Virtual<void*>(ordinances, 0x50, NumberArgument(context, -1)) : nullptr;
    if (!ordinance) {
        Virtual<void>(context, lua::kPushNumber, 0.0);
    } else if (which == 0) {
        Virtual<void>(context, lua::kPushNumber, static_cast<double>(Virtual<int64_t>(ordinance, 0x2C)));
    } else if (which == 1) {
        Virtual<void>(context, lua::kPushNumber, static_cast<double>(Virtual<int64_t>(ordinance, 0x30)));
    } else if (which == 2) {
        Virtual<void>(context, lua::kPushNumber, static_cast<double>(Virtual<int64_t>(ordinance, 0x50)));
    } else {
        Virtual<void>(context, lua::kPushNumber, 0.0);
    }
    Release(context);
    return 1;
}

// The game's 0x00409E10: game.ordinance_enact_fee(id).
int32_t __cdecl OrdinanceEnactFee(char* state)
{
    return OrdinanceCost(state, 0);
}

// The game's 0x00409E20: game.ordinance_repeal_fee(id).
int32_t __cdecl OrdinanceRepealFee(char* state)
{
    return OrdinanceCost(state, 1);
}

// The game's 0x00409E30: game.ordinance_monthly_cost(id).
int32_t __cdecl OrdinanceMonthlyCost(char* state)
{
    return OrdinanceCost(state, 2);
}

// The game's 0x00409E40: game.event_feedback([number]): a message of the
// current advice's event type, with the event's subject and the number,
// sent through the message server.
int32_t __cdecl EventFeedback(char* state)
{
    void* const context = Context(state);
    if (void* const advice = Virtual<void*>(Advisors(), 0x50)) {
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
        void* const message =
            memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        if (message)
            Virtual<uint32_t>(message, 0x4);
        Virtual<void>(message, 0x14, Virtual<uint32_t>(advice, 0x68));
        void* subject = nullptr;
        if (Virtual<void*>(advice, 0x64)) {
            void* const source = Virtual<void*>(advice, 0x64);
            if (void* const old = subject) {
                subject = nullptr;
                Virtual<uint32_t>(old, 0x8);
            }
            Virtual<uint8_t>(source, 0x0, uint32_t{1}, &subject);
            Virtual<void>(message, 0x84, subject);
        }
        Virtual<void>(message, 0x2C, int32_t{0});
        if (Virtual<int32_t>(context, lua::kGetTop) != 0 && Virtual<uint8_t>(context, lua::kIsNumber, int32_t{-1}))
            Virtual<void>(message, 0x2C, NumberArgument(context, -1));
        Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, int32_t{0});
        Release(subject);
        Virtual<uint32_t>(message, 0x8);
    }
    Release(context);
    return 0;
}

// The game's 0x00409F80: game.expire_advice().
int32_t __cdecl ExpireAdvice(char* state)
{
    void* const context = Context(state);
    if (void* const advice = Virtual<void*>(Advisors(), 0x50))
        Virtual<void>(advice, 0x38, int32_t{8}, int32_t{1});
    Release(context);
    return 0;
}

// The game's 0x00409FD0: game.retire_advice(): the current advice is
// handed to its advisor's slot 20.
int32_t __cdecl RetireAdvice(char* state)
{
    void* const context = Context(state);
    void* const advisors = Advisors();
    if (void* const advice = Virtual<void*>(advisors, 0x50)) {
        if (void* const advisor = Virtual<void*>(advisors, 0x1C, Virtual<uint32_t>(advice, 0x2C)))
            Virtual<void>(advisor, 0x50, advice);
    }
    Release(context);
    return 0;
}

// The game's 0x0040A040: game.advice_subject(): the object behind the
// current advice's event subject as user data, or a null one. The advice
// itself is not checked.
int32_t __cdecl AdviceSubject(char* state)
{
    void* const context = Context(state);
    void* const advisors = Advisors();
    void* const advice = Virtual<void*>(advisors, 0x50);
    if (void* const subject = Virtual<void*>(advice, 0x64)) {
        void* object = nullptr;
        if (Virtual<uint8_t>(subject, 0x34, &object)) {
            Virtual<uint32_t>(object, 0x4);
            void* const pushed = object;
            const int32_t tag = Virtual<int32_t>(advisors, 0x58, int32_t{0});
            Virtual<void>(context, lua::kPushUserData, pushed, tag);
            Release(object);
            Release(context);
            return 1;
        }
        Release(object);
    }
    Virtual<void>(context, lua::kPushUserData, static_cast<void*>(nullptr), int32_t{0});
    Release(context);
    return 1;
}

// The game's 0x0040A120: game.difficulty_level(): 0 to 2, or -1.
int32_t __cdecl DifficultyLevel(char* state)
{
    void* const context = Context(state);
    Virtual<void*>(Advisors(), 0x50);
    int32_t level = -1;
    if (void* const city = Global<void*>(addr::SC4_City)) {
        switch (Virtual<int32_t>(city, 0x60)) {
        case 0:
            level = 0;
            break;
        case 1:
            level = 1;
            break;
        case 2:
            level = 2;
            break;
        default:
            break;
        }
    }
    Virtual<void>(context, lua::kPushNumber, static_cast<double>(level));
    Release(context);
    return 1;
}

// The game's 0x0040A1B0: game.building_is_group(object, group): 1 or nil.
int32_t __cdecl BuildingIsGroup(char* state)
{
    void* const context = Context(state);
    Virtual<void*>(Advisors(), 0x50);
    if (Virtual<int32_t>(context, lua::kGetTop) > 1 && Virtual<uint8_t>(context, lua::kIsUserData, int32_t{1}) &&
        Virtual<uint8_t>(context, lua::kIsNumber, int32_t{2})) {
        void* object = Virtual<void*>(context, lua::kToObject, int32_t{1});
        const int32_t group = NumberArgument(context, 2);
        void* building = nullptr;
        if (object) {
            if (Virtual<void*>(object, 0x20)) {
                object = Virtual<void*>(object, 0x20);
                if (void* const old = building) {
                    building = nullptr;
                    Virtual<uint32_t>(old, 0x8);
                }
                if (Virtual<uint8_t>(object, 0x0, uint32_t{0x26BB767E}, &building) &&
                    Virtual<uint8_t>(building, 0x58, group)) {
                    Virtual<void>(context, lua::kPushNumber, 1.0);
                    Release(building);
                    Release(context);
                    return 1;
                }
            }
            Release(building);
        }
    }
    Virtual<void>(context, lua::kPushNil);
    Release(context);
    return 1;
}

// The game's 0x0040A2F0: game.event_building_distance(): the subject's
// figure when the current advice's event is 0xCA4E8986, nil otherwise. The
// advice itself is not checked.
int32_t __cdecl EventBuildingDistance(char* state)
{
    void* const context = Context(state);
    void* const advice = Virtual<void*>(Advisors(), 0x50);
    void* const subject = Virtual<void*>(advice, 0x64);
    if (subject && Virtual<uint32_t>(advice, 0x68) == 0xCA4E8986)
        Virtual<void>(context, lua::kPushNumber, static_cast<double>(Virtual<uint32_t>(subject, 0x24)));
    else
        Virtual<void>(context, lua::kPushNil);
    Release(context);
    return 1;
}

// The game's 0x0040A390: game.event_disaster_distance(): the subject's
// figure, nil without a subject. The advice itself is not checked.
int32_t __cdecl EventDisasterDistance(char* state)
{
    void* const context = Context(state);
    void* const advice = Virtual<void*>(Advisors(), 0x50);
    if (void* const subject = Virtual<void*>(advice, 0x64))
        Virtual<void>(context, lua::kPushNumber, static_cast<double>(Virtual<uint32_t>(subject, 0x24)));
    else
        Virtual<void>(context, lua::kPushNil);
    Release(context);
    return 1;
}

// The opening of the four MySim functions: the current advice's advisor
// (neither is checked) asked for its MySim interface when the top of the
// stack is a number. `mysim` starts as the state pointer, as the game's
// stack slot does.
bool MySimArgument(void* context, char* state, bool checkAdvisor, void** mysim)
{
    void* const advisors = Advisors();
    void* const advice = Virtual<void*>(advisors, 0x50);
    void* const advisor = Virtual<void*>(advisors, 0x1C, Virtual<uint32_t>(advice, 0x2C));
    *mysim = state;
    if (Virtual<int32_t>(context, lua::kGetTop) <= 0)
        return false;
    if (!Virtual<uint8_t>(context, lua::kIsNumber, int32_t{-1}))
        return false;
    if (checkAdvisor && !advisor)
        return false;
    return Virtual<uint8_t>(advisor, 0x0, uint32_t{0x0A1DA42F}, mysim) != 0;
}

int32_t MySimDistance(char* state, std::size_t slot)
{
    void* const context = Context(state);
    void* mysim;
    if (MySimArgument(context, state, false, &mysim)) {
        const uint32_t distance = Virtual<uint32_t>(mysim, slot, NumberArgument(context, -1));
        Virtual<void>(context, lua::kPushNumber, static_cast<double>(distance));
        Virtual<uint32_t>(mysim, 0x8);
    } else {
        Virtual<void>(context, lua::kPushNil);
    }
    Release(context);
    return 1;
}

int32_t MySimGrade(char* state, std::size_t slot, bool checkAdvisor)
{
    void* const context = Context(state);
    void* mysim;
    if (MySimArgument(context, state, checkAdvisor, &mysim)) {
        float storage;
        const float* const grade = Virtual<const float*>(mysim, slot, &storage, NumberArgument(context, -1));
        Virtual<void>(context, lua::kPushNumber, static_cast<double>(*grade) * static_cast<double>(100.0f));
        Virtual<uint32_t>(mysim, 0x8);
    } else {
        Virtual<void>(context, lua::kPushNil);
    }
    Release(context);
    return 1;
}

// The game's 0x0040A410: game.mysim_distance_to_closest_building(kind).
int32_t __cdecl MySimDistanceToClosestBuilding(char* state)
{
    return MySimDistance(state, 0xC8);
}

// The game's 0x0040A4F0: game.mysim_distance_to_closest_landmark_or_reward(kind).
int32_t __cdecl MySimDistanceToClosestLandmarkOrReward(char* state)
{
    return MySimDistance(state, 0xCC);
}

// The game's 0x0040A5D0: game.mysim_local_school_grade(kind).
int32_t __cdecl MySimLocalSchoolGrade(char* state)
{
    return MySimGrade(state, 0xD0, false);
}

// The game's 0x0040A6B0: game.mysim_local_hospital_grade(kind). The only
// one of the four that checks the advisor.
int32_t __cdecl MySimLocalHospitalGrade(char* state)
{
    return MySimGrade(state, 0xD4, true);
}

bool InstallNativesA(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4ScriptNatives_TrendSlope, reinterpret_cast<void*>(&TrendSlope)},
        {addr::SC4ScriptNatives_TrendValue, reinterpret_cast<void*>(&TrendValue)},
        {addr::SC4ScriptNatives_TrendCount, reinterpret_cast<void*>(&TrendCount)},
        {addr::SC4ScriptNatives_OrdinanceIsOn, reinterpret_cast<void*>(&OrdinanceIsOn)},
        {addr::SC4ScriptNatives_OrdinanceSetOff, reinterpret_cast<void*>(&OrdinanceSetOff)},
        {addr::SC4ScriptNatives_OrdinanceSetOn, reinterpret_cast<void*>(&OrdinanceSetOn)},
        {addr::SC4ScriptNatives_OrdinanceIsAvailable, reinterpret_cast<void*>(&OrdinanceIsAvailable)},
        {addr::SC4ScriptNatives_OrdinanceCost, reinterpret_cast<void*>(&OrdinanceCost)},
        {addr::SC4ScriptNatives_OrdinanceEnactFee, reinterpret_cast<void*>(&OrdinanceEnactFee)},
        {addr::SC4ScriptNatives_OrdinanceRepealFee, reinterpret_cast<void*>(&OrdinanceRepealFee)},
        {addr::SC4ScriptNatives_OrdinanceMonthlyCost, reinterpret_cast<void*>(&OrdinanceMonthlyCost)},
        {addr::SC4ScriptNatives_EventFeedback, reinterpret_cast<void*>(&EventFeedback)},
        {addr::SC4ScriptNatives_ExpireAdvice, reinterpret_cast<void*>(&ExpireAdvice)},
        {addr::SC4ScriptNatives_RetireAdvice, reinterpret_cast<void*>(&RetireAdvice)},
        {addr::SC4ScriptNatives_AdviceSubject, reinterpret_cast<void*>(&AdviceSubject)},
        {addr::SC4ScriptNatives_DifficultyLevel, reinterpret_cast<void*>(&DifficultyLevel)},
        {addr::SC4ScriptNatives_BuildingIsGroup, reinterpret_cast<void*>(&BuildingIsGroup)},
        {addr::SC4ScriptNatives_EventBuildingDistance, reinterpret_cast<void*>(&EventBuildingDistance)},
        {addr::SC4ScriptNatives_EventDisasterDistance, reinterpret_cast<void*>(&EventDisasterDistance)},
        {addr::SC4ScriptNatives_MySimDistanceToClosestBuilding,
         reinterpret_cast<void*>(&MySimDistanceToClosestBuilding)},
        {addr::SC4ScriptNatives_MySimDistanceToClosestLandmarkOrReward,
         reinterpret_cast<void*>(&MySimDistanceToClosestLandmarkOrReward)},
        {addr::SC4ScriptNatives_MySimLocalSchoolGrade, reinterpret_cast<void*>(&MySimLocalSchoolGrade)},
        {addr::SC4ScriptNatives_MySimLocalHospitalGrade, reinterpret_cast<void*>(&MySimLocalHospitalGrade)},
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

const bool g_nativesARegistered = registry::Register({
    "scripts.natives.a",
    "scripts/natives",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4ScriptNatives_TrendSlope,
    "Our native script functions, first part: trends, ordinances, advice, MySim",
    false,
    InstallNativesA,
});

} // namespace
} // namespace opensc4::script_natives
