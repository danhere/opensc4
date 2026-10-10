// The game's native script functions, second part (0x0040A790 to
// 0x0040B8C9): mysim_dispatch_tool, random_chance, reward_instance_count,
// get_reward_building_state, is_tutorial, nd_cancel, _combine_guids,
// advice_info, camera_jump, camera_zoom, ordinance_description,
// ordinance_name, trigger_event and unlock_reward_building, with the two
// reward record helpers only unlock_reward_building calls. Replacement,
// off by default:
//
//   scripts.natives.b: the fourteen natives and the two helpers.
//
// Each native is int fn(lua_State*): the script object that owns the state
// is at state - 4 and gives the script context (cISCLua, 0xEA10C4A2), whose
// methods are called by vtable offset. The string helpers 0x0040ADD0 and
// 0x0040AE00 between the natives are shared with the rest of the game and
// stay the game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::script_natives {
namespace {

constexpr uint32_t kIidScriptContext = 0xEA10C4A2;

// The game's string (vtable 0x00A80810).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return 0;
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void* AdvisorSystem()
{
    return Global<void*>(addr::cSC4AdvisorSystem_Instance);
}

// The script context of the state's owner (null without an owner).
void* QueryContext(char* state)
{
    void* context = nullptr;
    if (void* const owner = *reinterpret_cast<void**>(state - 4))
        Virtual<uint8_t>(owner, 0x0, kIidScriptContext, &context);
    return context;
}

void ReleaseContext(void* context)
{
    if (context)
        Virtual<uint32_t>(context, 0x8);
}

void MakeString(GameString& text, const char* begin, std::size_t length)
{
    uint8_t allocator;
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    reinterpret_cast<void(__thiscall*)(char**, const char*, const char*, uint8_t*)>(addr::SC4String_FromRange)(
        &text.begin, begin, begin + length, &allocator);
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
}

void FreeString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
}

// The natives' error report: the message, then the title, shown by
// 0x004179A0, then both freed (the title first).
template <std::size_t TitleSize, std::size_t MessageSize>
void ShowError(const char (&titleText)[TitleSize], const char (&messageText)[MessageSize])
{
    GameString message;
    MakeString(message, messageText, MessageSize - 1);
    GameString title;
    MakeString(title, titleText, TitleSize - 1);
    reinterpret_cast<void(__cdecl*)(GameString*, GameString*)>(addr::SC4ScriptNatives_ShowError)(&title, &message);
    FreeString(title);
    FreeString(message);
}

// The game's 0x0040A790, mysim_dispatch_tool: the context advice's advisor
// as a MySim (0x0A1DA42F), its slot 0x10C. The reference QueryInterface
// gives is not released, and the context advice is not checked for null.
int32_t __cdecl MySimDispatchTool(char* state)
{
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    void* const advice = Virtual<void*>(system, 0x50); // GetContextAdvice
    void* const advisor = Virtual<void*>(system, 0x1C, Virtual<uint32_t>(advice, 0x2C)); // GetAdvisor
    if (advisor) {
        void* mySim = state;
        if (Virtual<uint8_t>(advisor, 0x0, uint32_t{0x0A1DA42F}, &mySim))
            Virtual<void>(mySim, 0x10C);
    }
    ReleaseContext(context);
    return 0;
}

// The game's 0x0040A810, random_chance(percent): 1 when a draw from 0 to
// 100 is below the argument, else nil.
int32_t __cdecl RandomChance(char* state)
{
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    bool pushed = false;
    if (Virtual<void*>(system, 0x50) && Virtual<int32_t>(context, 0x18) > 0 &&
        Virtual<uint8_t>(context, 0x38, int32_t{-1})) {
        const double chance = Virtual<double>(context, 0x5C, int32_t{-1});
        void* const random = Virtual<void*>(system, 0x24);
        const double drawn =
            reinterpret_cast<double(__thiscall*)(void*, double, double)>(addr::cRZRandom_Uniform)(random, 0.0, 100.0);
        if (drawn < chance) {
            Virtual<void>(context, 0x7C, 1.0);
            pushed = true;
        }
    }
    if (!pushed)
        Virtual<void>(context, 0x78); // PushNil
    ReleaseContext(context);
    return 1;
}

// The argument at -1 as an ID: a string is read as text, a number
// truncated. False when it is neither.
bool IdArgument(void* context, int32_t& id)
{
    const int32_t type = Virtual<int32_t>(context, 0x30, int32_t{-1});
    if (type == 4) {
        const char* const text = Virtual<const char*>(context, 0x60, int32_t{-1});
        id = reinterpret_cast<int32_t(__cdecl*)(const char*)>(addr::SC4_TextToInt)(text);
        return true;
    }
    if (type == 3) {
        id = Ftol(Virtual<double>(context, 0x5C, int32_t{-1}));
        return true;
    }
    return false;
}

// The game's 0x0040A8E0, reward_instance_count(building): the count in the
// reward service's record, or without a record the count advisor 0x17
// keeps (0 when it has none).
int32_t __cdecl RewardInstanceCount(char* state)
{
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    void* const rewards = Global<void*>(addr::SC4_Service43DB4);
    int32_t id = 0;
    if (!rewards || Virtual<int32_t>(context, 0x18) <= 0 || !IdArgument(context, id) || id == 0) {
        Virtual<void>(context, 0x7C, 0.0);
    } else if (char* const record = Virtual<char*>(rewards, 0x18, id)) {
        const int32_t count = At<uint16_t>(record, 0x2);
        Virtual<void>(context, 0x7C, static_cast<double>(count));
    } else {
        void* const advisor = Virtual<void*>(system, 0x1C, int32_t{0x17});
        void* counts = nullptr;
        bool pushed = false;
        if (advisor) {
            void** const out = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset)(&counts);
            if (Virtual<uint8_t>(advisor, 0x0, uint32_t{0x2A5F877D}, out) &&
                reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t)>(addr::SC4ScriptNatives_HasRewardCount)(
                    counts, id)) {
                const int32_t count =
                    reinterpret_cast<int32_t(__thiscall*)(void*, int32_t)>(addr::SC4ScriptNatives_RewardCount)(
                        counts, id);
                Virtual<void>(context, 0x7C, static_cast<double>(count));
                pushed = true;
            }
        }
        if (!pushed)
            Virtual<void>(context, 0x7C, 0.0);
        if (counts)
            Virtual<uint32_t>(counts, 0x8);
    }
    ReleaseContext(context);
    return 1;
}

// The game's 0x0040AA60, get_reward_building_state(building): 0 without an
// ID or the service, 1 without a record, 2 when the record's bit 1 is set.
// With a record whose bit is clear it pushes nothing and still returns 1.
int32_t __cdecl GetRewardBuildingState(char* state)
{
    void* const context = QueryContext(state);
    void* const rewards = Global<void*>(addr::SC4_Service43DB4);
    int32_t id = 0;
    if (!rewards || Virtual<int32_t>(context, 0x18) <= 0 || !IdArgument(context, id) || id == 0) {
        Virtual<void>(context, 0x7C, 0.0);
    } else if (char* const record = Virtual<char*>(rewards, 0x18, id)) {
        if (At<uint8_t>(record, 0x0) & 0x2)
            Virtual<void>(context, 0x7C, 2.0);
    } else {
        Virtual<void>(context, 0x7C, 1.0);
    }
    ReleaseContext(context);
    return 1;
}

// The game's 0x0040AB30, is_tutorial([number]): 1 when the city's tutorial
// (slot 0x104 of the object the service's slot 0x90 gives) is the given
// one, or with no number when it is any (not -1).
int32_t __cdecl IsTutorial(char* state)
{
    void* const context = QueryContext(state);
    Virtual<void*>(AdvisorSystem(), 0x50);
    void* const service = Global<void*>(addr::SC4_Service43C94);
    void* const city = Global<void*>(addr::SC4_City);
    int32_t result = 0;
    void* const tutorial = Virtual<void*>(service, 0x90);
    if (city && tutorial) {
        bool numbered = false;
        int32_t number = 0;
        if (Virtual<int32_t>(context, 0x18) > 0 && Virtual<int32_t>(context, 0x30, int32_t{1}) == 3) {
            number = Ftol(Virtual<double>(context, 0x5C, int32_t{1}));
            numbered = number != -1;
        }
        if (numbered) {
            if (Virtual<int32_t>(tutorial, 0x104) == number)
                result = 1;
        } else if (Virtual<int32_t>(tutorial, 0x104) != -1) {
            result = 1;
        }
    }
    Virtual<void>(context, 0x7C, static_cast<double>(result));
    ReleaseContext(context);
    return 1;
}

// The user data at -1 when it carries the advisor system's data tag 0.
// The data tag is asked for only once the first checks pass.
bool SubjectArgument(void* context, void* system)
{
    if (Virtual<int32_t>(context, 0x18) <= 0)
        return false;
    if (!Virtual<uint8_t>(context, 0x124, int32_t{-1})) // IsUserData
        return false;
    if (!Virtual<void*>(context, 0x6C, int32_t{-1}))
        return false;
    const int32_t tag = Virtual<int32_t>(system, 0x58, int32_t{0}); // GetDataTag
    return Virtual<int32_t>(context, 0x44, int32_t{-1}) == tag;
}

// The game's 0x0040AC10, nd_cancel(subject): the subject's object (its slot
// 0x20) as 0x0773FEF1, slot 0x88.
int32_t __cdecl NdCancel(char* state)
{
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    Virtual<void*>(system, 0x50);
    if (SubjectArgument(context, system)) {
        void* const subject = Virtual<void*>(context, 0x70, int32_t{-1});
        if (subject && Virtual<void*>(subject, 0x20)) {
            void* target = nullptr;
            void* const object = Virtual<void*>(subject, 0x20);
            void** const out = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset)(&target);
            if (Virtual<uint8_t>(object, 0x0, uint32_t{0x0773FEF1}, out))
                Virtual<void>(target, 0x88);
            if (target)
                Virtual<uint32_t>(target, 0x8);
        }
    }
    ReleaseContext(context);
    return 0;
}

// The game's 0x0040AD10, _combine_guids(a, b): the two numbers truncated
// and combined with exclusive or (the game's 0x004177B0), or -1.
int32_t __cdecl CombineGuids(char* state)
{
    void* const context = QueryContext(state);
    if (Virtual<int32_t>(context, 0x18) > 1 && Virtual<uint8_t>(context, 0x38, int32_t{-1}) &&
        Virtual<uint8_t>(context, 0x38, int32_t{-2})) {
        const int32_t last = Ftol(Virtual<double>(context, 0x5C, int32_t{-1}));
        const int32_t first = Ftol(Virtual<double>(context, 0x5C, int32_t{-2}));
        const int32_t combined = last ^ first;
        Virtual<void>(context, 0x7C, static_cast<double>(combined));
    } else {
        Virtual<void>(context, 0x7C, -1.0);
    }
    ReleaseContext(context);
    return 1;
}

// The game's 0x0040AE50, advice_info: shows the context advice's advisor
// and title. The advisor is not checked for null.
int32_t __cdecl AdviceInfo(char* state)
{
    static const char kNone[] = "No context advice available...\n";
    static const char kTitle[] = "game.advice_info()";
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    void* const advice = Virtual<void*>(system, 0x50);
    GameString message;
    MakeString(message, kNone, sizeof kNone - 1);
    if (advice) {
        void* const adviceTitle = Virtual<void*>(advice, 0x48);
        void* const advisor = Virtual<void*>(system, 0x1C, Virtual<uint32_t>(advice, 0x2C));
        void* const advisorName = Virtual<void*>(advisor, 0x18);
        const char* const titleText = Virtual<const char*>(adviceTitle, 0x14);
        const char* const nameText = Virtual<const char*>(advisorName, 0x14);
        reinterpret_cast<void(__cdecl*)(GameString*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
            &message, "\tAdvisor : %s\n\tAdvice title : %s\n ", nameText, titleText);
    }
    GameString title;
    MakeString(title, kTitle, sizeof kTitle - 1);
    reinterpret_cast<void(__cdecl*)(GameString*, GameString*)>(addr::SC4ScriptNatives_ShowError)(&title, &message);
    FreeString(title);
    FreeString(message);
    ReleaseContext(context);
    return 0;
}

// The game's 0x0040AF90, camera_jump(subject): centres the camera on the
// subject: its own position (slots 0x10 and 0x14, scaled by the terrain's
// slot 0x18, at the terrain's height) when its slot 0x18 says so, else the
// position its object (slot 0x20, as 0x26BB767E) gives.
int32_t __cdecl CameraJump(char* state)
{
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    Virtual<void*>(system, 0x50);
    bool done = false;
    do {
        if (!SubjectArgument(context, system))
            break;
        void* const view = Global<void*>(addr::SC4_View43DD0);
        if (!view)
            break;
        void* const camera = Virtual<void*>(view, 0x40);
        if (!camera)
            break;
        float centre[3];
        const uint32_t initial = Global<uint32_t>(addr::SC4ScriptNatives_VectorInitial);
        std::memcpy(&centre[0], &initial, sizeof initial);
        std::memcpy(&centre[1], &initial, sizeof initial);
        std::memcpy(&centre[2], &initial, sizeof initial);
        const auto SetCenter = reinterpret_cast<uint8_t(__thiscall*)(void*, float*)>(addr::cSC4CameraControl_SetCenter);
        void* const subject = Virtual<void*>(context, 0x70, int32_t{-1});
        if (Virtual<uint8_t>(subject, 0x18)) {
            void* const terrain = Global<void*>(addr::SC4_Terrain);
            const float scaleX = static_cast<float>(Virtual<double>(terrain, 0x18));
            centre[0] = static_cast<float>(Virtual<double>(subject, 0x10) * static_cast<double>(scaleX));
            const float scaleZ = static_cast<float>(Virtual<double>(terrain, 0x18));
            centre[2] = static_cast<float>(Virtual<double>(subject, 0x14) * static_cast<double>(scaleZ));
            centre[1] = static_cast<float>(Virtual<double>(terrain, 0x34, centre[0], centre[2]));
            SetCenter(camera, centre);
            done = true;
            break;
        }
        if (!Virtual<void*>(subject, 0x20))
            break;
        void* place = nullptr;
        void* const object = Virtual<void*>(subject, 0x20);
        void** const out = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset)(&place);
        if (!Virtual<uint8_t>(object, 0x0, uint32_t{0x26BB767E}, out)) {
            if (place)
                Virtual<uint32_t>(place, 0x8);
            break;
        }
        Virtual<void>(place, 0x20, &centre[0]);
        SetCenter(camera, centre);
        if (place)
            Virtual<uint32_t>(place, 0x8);
        done = true;
    } while (false);
    if (!done)
        ShowError("game.LUAExtension_camera_jump()", "The subject for camera jump is unknown...");
    ReleaseContext(context);
    return 0;
}

// The game's 0x0040B1F0, camera_zoom(level).
int32_t __cdecl CameraZoom(char* state)
{
    void* const context = QueryContext(state);
    Virtual<void*>(AdvisorSystem(), 0x50);
    void* camera = nullptr;
    if (Virtual<int32_t>(context, 0x18) > 0 && Virtual<uint8_t>(context, 0x38, int32_t{-1})) {
        if (void* const view = Global<void*>(addr::SC4_View43DD0))
            camera = Virtual<void*>(view, 0x40);
    }
    if (camera) {
        const float zoom = static_cast<float>(Virtual<double>(context, 0x5C, int32_t{-1}));
        reinterpret_cast<uint8_t(__thiscall*)(void*, float)>(addr::cSC4CameraControl_SetZoom)(camera, zoom);
    } else {
        ShowError("game.LUAExtension_camera_zoom()", "'zoom_level' was not specified or valid range of zooms.");
    }
    ReleaseContext(context);
    return 0;
}

// ordinance_description and ordinance_name: the ordinance's text (the given
// slot of the object the ordinance service's slot 0x50 gives) in a new
// string, pushed as user data with the advisor system's data tag 1. A
// failed allocation is not checked (the original calls through null too).
int32_t OrdinanceText(char* state, const char* format, std::size_t slot)
{
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    Virtual<void*>(system, 0x50);
    void* const ordinances = Global<void*>(addr::SC4_Service43D90);
    void* ordinance = nullptr;
    int32_t id = 0;
    if (ordinances && Virtual<int32_t>(context, 0x18) > 0 && Virtual<uint8_t>(context, 0x38, int32_t{-1})) {
        id = Ftol(Virtual<double>(context, 0x5C, int32_t{-1}));
        ordinance = Virtual<void*>(ordinances, 0x50, id);
    }
    if (!ordinance) {
        Virtual<void>(context, 0x78); // PushNil
        ReleaseContext(context);
        return 1;
    }
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x14);
    GameString temporary;
    void* text = nullptr;
    if (memory) {
        GameString* const made =
            reinterpret_cast<GameString*(__thiscall*)(GameString*, const char*)>(addr::SC4String_FromText)(
                &temporary, "");
        // The string's slot 0x6C is its cdecl Sprintf.
        using SprintfFn = void*(__cdecl*)(GameString*, const char*, ...);
        const auto Sprintf = reinterpret_cast<SprintfFn>((*reinterpret_cast<void***>(made))[0x6C / sizeof(void*)]);
        void* const formatted = Sprintf(made, format, id);
        text = reinterpret_cast<void*(__thiscall*)(void*, void*)>(addr::SC4String_FromIGZString)(memory, formatted);
        reinterpret_cast<void(__thiscall*)(GameString*)>(addr::SC4String_Destroy)(&temporary);
    }
    Virtual<uint32_t>(text, 0x4);
    Virtual<uint32_t>(text, 0x38, Virtual<void*>(ordinance, slot));
    const int32_t tag = Virtual<int32_t>(system, 0x58, int32_t{1}); // GetDataTag
    Virtual<void>(context, 0x114, text, tag);
    ReleaseContext(context);
    return 1;
}

// The game's 0x0040B330, ordinance_description(ordinance).
int32_t __cdecl OrdinanceDescription(char* state)
{
    return OrdinanceText(state, "Ordinance Description #%08x", 0x20);
}

// The game's 0x0040B470, ordinance_name(ordinance).
int32_t __cdecl OrdinanceName(char* state)
{
    return OrdinanceText(state, "Ordinance Name #%08x", 0x1C);
}

// The game's 0x0040B5B0, trigger_event(event): the advisor system's
// PushEvent(event, 0).
int32_t __cdecl TriggerEvent(char* state)
{
    void* const context = QueryContext(state);
    void* const system = AdvisorSystem();
    if (Virtual<int32_t>(context, 0x18) > 0 && Virtual<uint8_t>(context, 0x38, int32_t{-1})) {
        const int32_t event = Ftol(Virtual<double>(context, 0x5C, int32_t{-1}));
        Virtual<void>(system, 0x3C, event, int32_t{0});
    } else {
        ShowError("LUAExtension_trigger_event()", "'eventGUID' was not specified or was not a valid number.");
    }
    ReleaseContext(context);
    return 0;
}

// A reward record as the reward service keeps it (0x18 bytes): a flag byte,
// a count word at +2, a string at +4.
namespace record {
constexpr std::size_t kFlags = 0x0;
constexpr std::size_t kCount = 0x2;
constexpr std::size_t kText = 0x4;
constexpr std::size_t kSize = 0x18;
} // namespace record

// The game's 0x0040B6E0: an empty string in the record. The flag byte and
// the count are left as they were.
char* __fastcall RewardRecordConstruct(char* self, void*)
{
    GameString& text = At<GameString>(self, record::kText);
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.references = 0;
    text.vtable = addr::SC4String_Vtable;
    return self;
}

// The game's 0x0040B720: copies another record's flag byte, count and text.
char* __fastcall RewardRecordAssign(char* self, void*, char* source)
{
    At<uint8_t>(self, record::kFlags) = At<uint8_t>(source, record::kFlags);
    At<uint16_t>(self, record::kCount) = At<uint16_t>(source, record::kCount);
    char* const from = reinterpret_cast<std::uintptr_t>(source) + record::kText == 0 ? nullptr : source + 0x8;
    char* const to = self + 0x8;
    if (from != to) {
        void* allocator = source;
        reinterpret_cast<void*(__thiscall*)(char*, const char*, const char*, void*)>(addr::SC4String_AssignRange)(
            to, At<char*>(from, 0x0), At<char*>(from, 0x4), &allocator);
    }
    return self;
}

// The game's 0x0040B760, unlock_reward_building(building): sets flag 2 in
// the reward service's record of the building (count 0 for a new one).
int32_t __cdecl UnlockRewardBuilding(char* state)
{
    void* const context = QueryContext(state);
    if (Virtual<int32_t>(context, 0x18) > 0 && Virtual<uint8_t>(context, 0x38, int32_t{-1})) {
        const double number = Virtual<double>(context, 0x5C, int32_t{-1});
        const int32_t id = Ftol(number);
        if (void* const rewards = Global<void*>(addr::SC4_Service43DB4)) {
            char entry[record::kSize];
            RewardRecordConstruct(entry, nullptr);
            if (char* const found = Virtual<char*>(rewards, 0x18, id))
                RewardRecordAssign(entry, nullptr, found);
            else
                At<uint16_t>(entry, record::kCount) = 0;
            At<uint8_t>(entry, record::kFlags) = 2;
            Virtual<void>(rewards, 0x1C, id, &entry[0]);
            FreeString(At<GameString>(entry, record::kText));
        }
    } else {
        ShowError("LUAExtension_unlock_reward_building()",
                  "'buildingGUID' was not specified or was not a valid number.");
    }
    ReleaseContext(context);
    return 0;
}

bool InstallNativesB(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4ScriptNatives_MySimDispatchTool, reinterpret_cast<void*>(&MySimDispatchTool)},
        {addr::SC4ScriptNatives_RandomChance, reinterpret_cast<void*>(&RandomChance)},
        {addr::SC4ScriptNatives_RewardInstanceCount, reinterpret_cast<void*>(&RewardInstanceCount)},
        {addr::SC4ScriptNatives_GetRewardBuildingState, reinterpret_cast<void*>(&GetRewardBuildingState)},
        {addr::SC4ScriptNatives_IsTutorial, reinterpret_cast<void*>(&IsTutorial)},
        {addr::SC4ScriptNatives_NdCancel, reinterpret_cast<void*>(&NdCancel)},
        {addr::SC4ScriptNatives_CombineGuids, reinterpret_cast<void*>(&CombineGuids)},
        {addr::SC4ScriptNatives_AdviceInfo, reinterpret_cast<void*>(&AdviceInfo)},
        {addr::SC4ScriptNatives_CameraJump, reinterpret_cast<void*>(&CameraJump)},
        {addr::SC4ScriptNatives_CameraZoom, reinterpret_cast<void*>(&CameraZoom)},
        {addr::SC4ScriptNatives_OrdinanceDescription, reinterpret_cast<void*>(&OrdinanceDescription)},
        {addr::SC4ScriptNatives_OrdinanceName, reinterpret_cast<void*>(&OrdinanceName)},
        {addr::SC4ScriptNatives_TriggerEvent, reinterpret_cast<void*>(&TriggerEvent)},
        {addr::SC4ScriptNatives_RewardRecordConstruct, reinterpret_cast<void*>(&RewardRecordConstruct)},
        {addr::SC4ScriptNatives_RewardRecordAssign, reinterpret_cast<void*>(&RewardRecordAssign)},
        {addr::SC4ScriptNatives_UnlockRewardBuilding, reinterpret_cast<void*>(&UnlockRewardBuilding)},
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

const bool g_nativesBRegistered = registry::Register({
    "scripts.natives.b",
    "scripts/natives",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4ScriptNatives_RandomChance,
    "Our native script functions, second part (mysim_dispatch_tool to unlock_reward_building)",
    false,
    InstallNativesB,
});

} // namespace
} // namespace opensc4::script_natives
