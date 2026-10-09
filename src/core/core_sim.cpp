// Our cSC4Simulator (build 641), first part: the interface, the message
// posts, the speed, the clock and the agent dispatch, each a replacement on
// the original object (docs/simulator.md). The frame's time slice
// (RunTimeSlice) and adding and removing agents stay the game's: the
// determinism replacements hook them (determinism.fixed_ticks,
// determinism.agent_order).
#include "core/core_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::core_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("simulator: first %s", name);
}

void* NewMessage(uint32_t type, uint32_t data1, uint32_t data2, uint32_t data3)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    Virtual<uint32_t>(message, 0x2C, data1);
    Virtual<uint32_t>(message, 0x40, data2);
    Virtual<uint32_t>(message, 0x54, data3);
    return message;
}

namespace {

void* Date(char* self) { return self + field::kDateInterface; }

int32_t DayNumber(char* self) { return Virtual<int32_t>(Date(self), 0x68); }

uint32_t ElapsedMs(char* self, std::size_t watch)
{
    return CallThis<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + watch);
}

void StartWatch(char* self, std::size_t watch) { CallThis<void>(addr::GZStopwatch_Start, self + watch); }

// --- Posting (0x007020E0) and agent messages (0x007024C0) -------------------------

// A message posted to the message server (slot 4); the result is the
// message's Release, which the pause slots return.
uint32_t __fastcall Post(char* self, void*, uint32_t type, uint32_t data1, uint32_t data2, uint32_t data3)
{
    void* const message = NewMessage(type, data1, data2, data3);
    Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x10, message, 0u);
    return Virtual<uint32_t>(message, 0x8);
}

// A message handed to an agent (its DoMessage).
uint8_t __fastcall SendToAgent(char*, void*, void** value, uint32_t type, uint32_t data1, uint32_t data2, uint32_t data3)
{
    void* const agent = *value;
    void* const message = NewMessage(type, data1, data2, data3);
    const uint8_t done = Virtual<uint8_t>(agent, 0xC, message);
    Virtual<uint32_t>(message, 0x8);
    return done;
}

uint32_t PostMessage(char* self, uint32_t type, uint32_t data1, uint32_t data2, uint32_t data3)
{
    return CallThis<uint32_t>(addr::cSC4Simulator_Post, self, type, data1, data2, data3);
}

// --- The speed (0x00702430) -------------------------------------------------------

// Speeds 0 to 2: the frame rate cap, the time per step, the step watch
// restarted, 0x4A7FB7E9 posted.
void __fastcall ApplySpeed(char* self, void*, int32_t speed)
{
    static bool logged = false;
    FirstCall("ApplySpeed", logged);
    switch (speed) {
    case 0: At<uint32_t>(self, field::kFpsCap) = 30; break;
    case 1: At<uint32_t>(self, field::kFpsCap) = 20; break;
    case 2: At<uint32_t>(self, field::kFpsCap) = 15; break;
    default: return;
    }
    At<int32_t>(self, field::kSpeed) = speed;
    At<uint32_t>(self, field::kStepMs) = At<uint32_t>(self, field::kStepsPerSpeed + 4 * speed);
    StartWatch(self, field::kStepWatch);
    PostMessage(self, 0x4A7FB7E9, static_cast<uint32_t>(At<int32_t>(self, field::kSpeed)), 0, 0);
}

// --- The clock (0x00702A80) -------------------------------------------------------

// One step of the clock: a minute once every minute agent (type 2) had its
// turn, an hour after 60 minutes once the hour agents (3) had theirs, a day
// after 24 hours once the day agents (4) had theirs and, when tomorrow
// starts a week, month or year, the agents of that unit (5-7) too. The paced
// unit (+0x7C) also waits for the step watch. A unit with no agents passes
// at once. The day posts NewDay, then NewWeek, MonthPassed and NewYear when
// they begin.
void __fastcall ClockStep(char* self, void*)
{
    static bool logged = false;
    FirstCall("ClockStep", logged);
    At<uint32_t>(self, field::kAgentDue) = 0;
    At<uint32_t>(self, field::kAgentDue + 4) = 0;
    At<uint16_t>(self, field::kAgentDue + 8) = 0;
    At<uint8_t>(self, field::kAgentDue + 10) = 0;
    if (AgentCount(self, 1) != 0 || AgentCount(self, 0) != 0)
        return;
    const bool stepDue = !(ElapsedMs(self, field::kStepWatch) < At<uint32_t>(self, field::kStepMs));
    bool roundDone[kAgentTypes];
    for (int type = 2; type < kAgentTypes; ++type)
        roundDone[type] = Cursor(self, type) == AgentHeader(self, type);
    alignas(4) char tomorrow[0x10];
    CallThis<void>(addr::cRZDate_cRZDate, tomorrow, static_cast<uint32_t>(DayNumber(self) + 1));
    bool starts[3];
    starts[0] = CallThis<int32_t>(addr::cRZDate_DayOfWeekAt8, tomorrow + 8) == 1;
    starts[1] = CallThis<int32_t>(addr::cRZDate_DayOfMonthAt8, tomorrow + 8) == 1;
    starts[2] = CallThis<int32_t>(addr::cRZDate_DayOfYearAt8, tomorrow + 8) == 1;
    const uint32_t pacing = At<uint32_t>(self, field::kPacing);
    uint32_t& minutes = At<uint32_t>(self, field::kMinutes);
    uint32_t& hours = At<uint32_t>(self, field::kHours);
    if (roundDone[2] && (pacing != 2 || stepDue)) {
        if (pacing == 2)
            StartWatch(self, field::kStepWatch);
        if (AgentCount(self, 2) == 0) {
            minutes = 60;
        } else {
            At<uint8_t>(self, field::kAgentDue + 2) = 1;
            ++minutes;
        }
    }
    if (minutes >= 60 && roundDone[3] && (pacing != 3 || stepDue)) {
        minutes = 0;
        if (pacing == 3)
            StartWatch(self, field::kStepWatch);
        if (AgentCount(self, 3) == 0) {
            hours = 24;
        } else {
            At<uint8_t>(self, field::kAgentDue + 3) = 1;
            ++hours;
        }
    }
    if (hours >= 24 && (pacing != 4 || stepDue) && roundDone[4]) {
        bool ready = true;
        for (int unit = 0; unit < 3 && ready; ++unit)
            if (starts[unit] && !roundDone[5 + unit])
                ready = false;
        if (ready) {
            hours = 0;
            if (pacing == 4)
                StartWatch(self, field::kStepWatch);
            void* const date = Date(self);
            At<uint8_t>(self, field::kAgentDue + 4) = 1;
            Virtual<uint32_t>(date, 0x94); // the next day
            At<uint32_t>(self, field::kDayOfYear) = Virtual<uint32_t>(date, 0x38);
            const uint32_t day = Virtual<uint32_t>(date, 0x68);
            PostMessage(self, 0x66956814, day, 0, At<uint32_t>(self, field::kDayOfYear));
            if (Virtual<int32_t>(date, 0x30) == 1) {
                At<uint8_t>(self, field::kAgentDue + 5) = 1;
                const uint32_t week = (Virtual<uint32_t>(date, 0x38) - 1) / 7 + 1;
                At<uint32_t>(self, field::kWeek) = week;
                PostMessage(self, 0x66956815, day, 0, week);
            }
            if (Virtual<int32_t>(date, 0x3C) == 1) {
                At<uint8_t>(self, field::kAgentDue + 6) = 1;
                At<uint32_t>(self, field::kMonth) = Virtual<uint32_t>(date, 0x5C);
                PostMessage(self, 0x66956816, day, 0, At<uint32_t>(self, field::kMonth));
            }
            if (Virtual<int32_t>(date, 0x38) == 1) {
                At<uint8_t>(self, field::kAgentDue + 7) = 1;
                At<uint32_t>(self, field::kYear) = Virtual<uint32_t>(date, 0x34);
                PostMessage(self, 0x66956817, day, 0, At<uint32_t>(self, field::kYear));
            }
        }
    }
    CallThis<void>(addr::cRZDate_Destroy, tomorrow);
}

// --- The agents (0x00702F10, 0x007038B0, 0x00702DF0) ------------------------------

// Every agent of a type, from its cursor to the end of its map, gets the
// type's message (0x66956810 + type, the day number, 0, the type's data),
// again until it answers true; the cursor then goes back to the start.
void __fastcall DispatchType(char* self, void*, int32_t type)
{
    static bool logged = false;
    FirstCall("DispatchType", logged);
    if (AgentCount(self, type) == 0)
        return;
    const uint32_t day = static_cast<uint32_t>(DayNumber(self));
    char* const header = AgentHeader(self, type);
    while (Cursor(self, type) != header) {
        char* const node = Cursor(self, type);
        Cursor(self, type) = Next(node);
        if (!At<uint8_t>(node, 0x14))
            continue;
        bool done;
        do {
            const uint32_t data = At<uint32_t>(self, field::kAgentData + 4 * type);
            void* const agent = At<void*>(node, 0x10);
            void* const message = NewMessage(0x66956810u + static_cast<uint32_t>(type), day, 0, data);
            done = Virtual<uint8_t>(agent, 0xC, message) != 0;
            Virtual<uint32_t>(message, 0x8);
        } while (!done);
    }
    if (AgentCount(self, type) != 0)
        Cursor(self, type) = At<char*>(AgentHeader(self, type), 0x8);
}

// The tick's agents: the units the clock passed (types 2-7) in full, then
// one agent at a time, from the cursors, while the slice's time lasts: the
// one-shot agents (types 0, 1; type 0 leaves its map after its turn) first,
// then types 2 to 9 that still have a round going. An agent that answers
// false gets the next turn again.
void __fastcall TickAgents(char* self, void*)
{
    static bool logged = false;
    FirstCall("TickAgents", logged);
    for (int type = 2; type <= 7; ++type)
        if (At<uint8_t>(self, field::kAgentDue + type))
            CallThis<void>(addr::cSC4Simulator_DispatchType, self, type);
    const uint32_t day = static_cast<uint32_t>(DayNumber(self));
    int type = 0;
    for (;;) {
        bool turn = true;
        if (Cursor(self, type) == AgentHeader(self, type)) {
            if (type <= 1 && AgentCount(self, type) != 0) {
                type = 0;
                CallThis<void>(addr::cSC4Simulator_DispatchType, self, 0);
            } else {
                ++type;
                if (type == 10)
                    return;
                if (AgentCount(self, type) == 0 || Cursor(self, type) == AgentHeader(self, type))
                    turn = false;
            }
        }
        if (turn) {
            char* const node = Cursor(self, type);
            if (!At<uint8_t>(node, 0x14)) {
                Cursor(self, type) = Next(node);
            } else {
                const uint32_t data = At<uint32_t>(self, field::kAgentData + 4 * type);
                void* const agent = At<void*>(node, 0x10);
                void* const message = NewMessage(0x66956810u + static_cast<uint32_t>(type), day, 0, data);
                const bool done = Virtual<uint8_t>(agent, 0xC, message) != 0;
                Virtual<uint32_t>(message, 0x8);
                if (done)
                    Cursor(self, type) = Next(Cursor(self, type));
            }
            if (type == 0) {
                using EraseFn = char*(__cdecl*)(char* node, char** root, char** leftmost, char** rightmost);
                using FreeFn = void(__cdecl*)(void* p);
                char* const header = AgentHeader(self, 0);
                char* const erased = reinterpret_cast<EraseFn>(addr::SC4Tree_RebalanceForErase)(
                    node, &At<char*>(header, 0x4), &At<char*>(header, 0x8), &At<char*>(header, 0xC));
                At<const void*>(erased, 0x18) = reinterpret_cast<const void*>(addr::SC4String_Vtable);
                if (void* const name = At<void*>(erased, 0x1C))
                    reinterpret_cast<FreeFn>(addr::SC4_Free)(name);
                if (void* const agent = At<void*>(erased, 0x10))
                    Virtual<uint32_t>(agent, 0x8);
                if (erased)
                    reinterpret_cast<FreeFn>(addr::SC4_Free)(erased);
                --AgentCount(self, 0);
            }
        }
        if (!(ElapsedMs(self, field::kSliceWatch) < At<uint32_t>(self, field::kMaxMsPerTick)))
            return;
    }
}

// The idle agents (type 10), from the cursor, while the slice's time
// lasts, each with a budget of the time left, at most a third of the
// tick's (message 0x6695681A: the day number, paused, the budget).
void __fastcall IdleAgents(char* self, void*)
{
    static bool logged = false;
    FirstCall("IdleAgents", logged);
    if (At<int32_t>(self, field::kHiddenPaused) > 0 || AgentCount(self, 10) == 0)
        return;
    char* const header = AgentHeader(self, 10);
    if (Cursor(self, 10) == header)
        Cursor(self, 10) = At<char*>(header, 0x8);
    const uint32_t day = static_cast<uint32_t>(DayNumber(self));
    const uint32_t limit = At<uint32_t>(self, field::kMaxMsPerTick);
    const uint32_t third = limit / 3;
    uint32_t elapsed = ElapsedMs(self, field::kSliceWatch);
    if (!(elapsed < limit))
        return;
    do {
        char* const node = Cursor(self, 10);
        Cursor(self, 10) = Next(node);
        const uint32_t remaining = At<uint32_t>(self, field::kMaxMsPerTick) - elapsed;
        const uint32_t budget = remaining < third ? remaining : third;
        if (At<uint8_t>(node, 0x14))
            CallThis<uint8_t>(addr::cSC4Simulator_SendToAgent, self, reinterpret_cast<void**>(node + 0x10), 0x6695681Au,
                              day, At<int32_t>(self, field::kPaused) > 0 ? 1u : 0u, budget);
        if (Cursor(self, 10) == header)
            break;
        elapsed = ElapsedMs(self, field::kSliceWatch);
    } while (elapsed < At<uint32_t>(self, field::kMaxMsPerTick));
}

// --- The interface (vtable 0x00AB2FB8) ----------------------------------------------

// 5 (0x007024A0): 1 January 2000.
uint32_t __fastcall GetSimStartDate(char*, void*, void* date)
{
    return Virtual<uint32_t>(date, 0x18, 1u, 1u, 2000u);
}

// 6 (0x00702150).
void __fastcall GetSimDateParts(char* self, void*, uint32_t* year, uint32_t* month, uint32_t* day, uint32_t* dayOfYear,
                                uint32_t* weekDay)
{
    void* const date = Date(self);
    if (year)
        *year = Virtual<uint32_t>(date, 0x34);
    if (month)
        *month = Virtual<uint32_t>(date, 0x5C);
    if (day)
        *day = Virtual<uint32_t>(date, 0x3C);
    if (dayOfYear)
        *dayOfYear = Virtual<uint32_t>(date, 0x38);
    if (weekDay)
        *weekDay = Virtual<uint32_t>(date, 0x30);
}

// 7, 8 (0x00703D20, 0x00703D40).
void* __fastcall GetSimDate(char* self, void*) { return Date(self); }
int32_t __fastcall GetSimDateNumber(char* self, void*) { return DayNumber(self); }

// 9-14 (0x007021E0 ...): the counts; the first pause and the last resume
// of each kind post 0xAA7FB7E0 (pause), 0x4A7FB7E2 (hidden) or 0x4A7FB807
// (emergency) with 1 or 0. The result is what the game leaves in EAX.
uint32_t __fastcall Pause(char* self, void*)
{
    const int32_t count = ++At<int32_t>(self, field::kPaused);
    return count == 1 ? PostMessage(self, 0xAA7FB7E0, 1, 0, 0) : static_cast<uint32_t>(count);
}

uint32_t __fastcall HiddenPause(char* self, void*)
{
    const int32_t count = ++At<int32_t>(self, field::kHiddenPaused);
    return count == 1 ? PostMessage(self, 0x4A7FB7E2, 1, 0, 0) : static_cast<uint32_t>(count);
}

uint32_t __fastcall EmergencyPause(char* self, void*)
{
    Virtual<uint32_t>(self, 0x24); // Pause
    if (At<uint8_t>(self, field::kSpeedChangeable))
        At<uint8_t>(self, field::kSpeedChangeable) = 0;
    const int32_t count = ++At<int32_t>(self, field::kEmergencyPaused);
    return count == 1 ? PostMessage(self, 0x4A7FB807, 1, 0, 0) : static_cast<uint32_t>(count);
}

uint32_t __fastcall Resume(char* self, void*)
{
    const uint8_t changeable = At<uint8_t>(self, field::kSpeedChangeable);
    if (!changeable)
        return 0;
    if (--At<int32_t>(self, field::kPaused) != 0)
        return changeable;
    return PostMessage(self, 0xAA7FB7E0, 0, 0, 0);
}

uint32_t __fastcall HiddenResume(char* self, void*)
{
    if (--At<int32_t>(self, field::kHiddenPaused) != 0)
        return 1;
    return PostMessage(self, 0x4A7FB7E2, 0, 0, 0);
}

uint32_t __fastcall EmergencyResume(char* self, void*)
{
    if (At<uint8_t>(self, field::kSpeedChangeable) != 1)
        At<uint8_t>(self, field::kSpeedChangeable) = 1;
    const uint32_t resumed = Virtual<uint32_t>(self, 0x30); // Resume
    if (--At<int32_t>(self, field::kEmergencyPaused) != 0)
        return resumed;
    return PostMessage(self, 0x4A7FB807, 0, 0, 0);
}

// 15-18.
uint32_t __fastcall IsPaused(char* self, void*) { return At<int32_t>(self, field::kPaused) > 0 ? 1 : 0; }
uint32_t __fastcall IsHiddenPaused(char* self, void*) { return At<int32_t>(self, field::kHiddenPaused) > 0 ? 1 : 0; }
uint32_t __fastcall IsEmergencyPaused(char* self, void*) { return At<int32_t>(self, field::kEmergencyPaused) > 0 ? 1 : 0; }

uint32_t __fastcall IsAnyPaused(char* self, void*)
{
    return At<int32_t>(self, field::kPaused) >= 1 || At<int32_t>(self, field::kHiddenPaused) >= 1 ||
                   At<int32_t>(self, field::kEmergencyPaused) >= 1
               ? 1
               : 0;
}

// 22, 23 (0x00703FB0, 0x00704010): a map emptied (0x00703850 frees the
// nodes), its cursor at the end.
void ClearAgents(char* self, int type)
{
    char*& header = AgentHeader(self, type);
    if (AgentCount(self, type) != 0) {
        CallThis<void>(addr::cSC4Simulator_FreeAgentNodes, self + field::kAgents + 12 * type, At<char*>(header, 0x4));
        At<char*>(header, 0x8) = header;
        At<char*>(header, 0x4) = nullptr;
        At<char*>(header, 0xC) = header;
        AgentCount(self, type) = 0;
    }
    Cursor(self, type) = header;
}

uint8_t __fastcall RemoveAllAgentsOfType(char* self, void*, int32_t type)
{
    if (type < kAgentTypes)
        ClearAgents(self, type);
    return 1;
}

uint8_t __fastcall RemoveAllAgents(char* self, void*)
{
    for (int type = 0; type < kAgentTypes; ++type)
        ClearAgents(self, type);
    return 1;
}

// 25, 26 (0x00703080, 0x00703120): agents by name (their string's slot 10,
// case-insensitive).
uint8_t __fastcall GetAgentEnabled(char* self, void*, void* name)
{
    for (int type = 0; type < kAgentTypes; ++type) {
        char* const header = AgentHeader(self, type);
        for (char* node = At<char*>(header, 0x8); node != header; node = Next(node))
            if (Virtual<uint8_t>(node + 0x18, 0x28, name, 0u) && At<uint8_t>(node, 0x14))
                return 1;
    }
    return 0;
}

void __fastcall SetAgentEnabled(char* self, void*, void* name, uint8_t enabled)
{
    for (int type = 0; type < kAgentTypes; ++type) {
        char* const header = AgentHeader(self, type);
        for (char* node = At<char*>(header, 0x8); node != header; node = Next(node))
            if (Virtual<uint8_t>(node + 0x18, 0x28, name, 0u))
                At<uint8_t>(node, 0x14) = enabled;
    }
}

// 27-31: speed and time (0x008265C0, 0x00702430 through 0x007026D0,
// 0x0068D1B0, 0x007B62D0, 0x00703DA0).
int32_t __fastcall GetSimSpeed(char* self, void*) { return At<int32_t>(self, field::kSpeed); }

uint8_t __fastcall SetSimSpeed(char* self, void*, int32_t speed)
{
    if (!At<uint8_t>(self, field::kSpeedChangeable))
        return 0;
    if (At<int32_t>(self, field::kSpeed) != speed)
        CallThis<void>(addr::cSC4Simulator_ApplySpeed, self, speed);
    return 1;
}

int32_t __fastcall GetSimTime(char* self, void*) { return At<int32_t>(self, field::kSaved40); }

int32_t __fastcall SetSimTime(char* self, void*, int32_t time)
{
    At<int32_t>(self, field::kSaved40) = time;
    return time;
}

uint32_t __fastcall SetMaxMillisecondsPerTick(char* self, void*, uint32_t ms)
{
    At<uint32_t>(self, field::kMaxMsPerTick) = ms;
    return ms;
}

// 32 (0x007026F0): the dilations: [4] emergency paused or while the
// service at 0x00B43D44 says so (its slot 13), [0] paused, else by speed
// [1], [2], [3].
float __fastcall GetAnimationTimeDilation(char* self, void*)
{
    const float* const dilations = At<const float*>(self, field::kDilations);
    if (Virtual<uint8_t>(self, 0x44) || Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D44), 0x34))
        return dilations[4];
    if (Virtual<uint8_t>(self, 0x3C))
        return dilations[0];
    switch (Virtual<int32_t>(self, 0x6C)) {
    case 1: return dilations[2];
    case 2: return dilations[3];
    default: return dilations[1];
    }
}

// 33 (0x007031C0): each agent enabled by its flags for before (bit 0) or
// after (bit 1) the city is established.
void __fastcall SetCityEstablished(char* self, void*, uint8_t established)
{
    if (static_cast<uint32_t>(established) == (At<uint8_t>(self, field::kEstablished) != 0 ? 1u : 0u))
        return;
    At<uint8_t>(self, field::kEstablished) = established;
    for (int type = 0; type < kAgentTypes; ++type) {
        char* const header = AgentHeader(self, type);
        for (char* node = At<char*>(header, 0x8); node != header; node = Next(node)) {
            uint8_t flags = At<uint8_t>(node, 0x2C);
            if (At<uint8_t>(self, field::kEstablished))
                flags >>= 1;
            At<uint8_t>(node, 0x14) = flags & 1;
        }
    }
}

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x007024A0, GetSimStartDate),
    OPENSC4_SLOT(6, 0x00702150, GetSimDateParts),
    OPENSC4_SLOT(7, 0x00703D20, GetSimDate),
    OPENSC4_SLOT(8, 0x00703D40, GetSimDateNumber),
    OPENSC4_SLOT(9, 0x007021E0, Pause),
    OPENSC4_SLOT(10, 0x00702200, HiddenPause),
    OPENSC4_SLOT(11, 0x00702540, EmergencyPause),
    OPENSC4_SLOT(12, 0x00702220, Resume),
    OPENSC4_SLOT(13, 0x00702240, HiddenResume),
    OPENSC4_SLOT(14, 0x00702580, EmergencyResume),
    OPENSC4_SLOT(15, 0x00703D50, IsPaused),
    OPENSC4_SLOT(16, 0x00703D60, IsHiddenPaused),
    OPENSC4_SLOT(17, 0x00703D70, IsEmergencyPaused),
    OPENSC4_SLOT(18, 0x00703D80, IsAnyPaused),
    OPENSC4_SLOT(22, 0x00703FB0, RemoveAllAgentsOfType),
    OPENSC4_SLOT(23, 0x00704010, RemoveAllAgents),
    OPENSC4_SLOT(25, 0x00703080, GetAgentEnabled),
    OPENSC4_SLOT(26, 0x00703120, SetAgentEnabled),
    OPENSC4_SLOT(27, 0x008265C0, GetSimSpeed),
    OPENSC4_SLOT(28, 0x007026D0, SetSimSpeed),
    OPENSC4_SLOT(29, 0x0068D1B0, GetSimTime),
    OPENSC4_SLOT(30, 0x007B62D0, SetSimTime),
    OPENSC4_SLOT(31, 0x00703DA0, SetMaxMillisecondsPerTick),
    OPENSC4_SLOT(32, 0x007026F0, GetAnimationTimeDilation),
    OPENSC4_SLOT(33, 0x007031C0, SetCityEstablished),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 34;

bool InstallInterface(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4Simulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("simulator interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("simulator interface: %u of %d cISC4Simulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registeredInterface = registry::Register({
    "simulators.core.interface",
    "simulators/core",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_vftable,
    "Our cISC4Simulator slots (dates, pauses, speed, agent queries) on the original object; AddAgent and RemoveAgent "
    "stay the game's (docs/simulator.md)",
    false,
    InstallInterface,
});

// --- Installation of the detours ----------------------------------------------------

template <typename Fn>
bool Detour(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

using PostFn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
using SpeedFn = void(__fastcall*)(char*, void*, int32_t);
using StepFn = void(__fastcall*)(char*, void*);
using SendFn = uint8_t(__fastcall*)(char*, void*, void**, uint32_t, uint32_t, uint32_t, uint32_t);
PostFn g_originalPost = nullptr;
SpeedFn g_originalSpeed = nullptr;
StepFn g_originalClock = nullptr;
SpeedFn g_originalDispatch = nullptr;
StepFn g_originalTick = nullptr;
StepFn g_originalIdle = nullptr;
SendFn g_originalSend = nullptr;

bool InstallClock(registry::Mode)
{
    return game::IsBuild641() && Detour(addr::cSC4Simulator_Post, &Post, &g_originalPost) &&
           Detour(addr::cSC4Simulator_ApplySpeed, &ApplySpeed, &g_originalSpeed) &&
           Detour(addr::cSC4Simulator_ClockStep, &ClockStep, &g_originalClock);
}

bool InstallAgents(registry::Mode)
{
    return game::IsBuild641() && Detour(addr::cSC4Simulator_DispatchType, &DispatchType, &g_originalDispatch) &&
           Detour(addr::cSC4Simulator_TickAgents, &TickAgents, &g_originalTick) &&
           Detour(addr::cSC4Simulator_IdleAgents, &IdleAgents, &g_originalIdle) &&
           Detour(addr::cSC4Simulator_SendToAgent, &SendToAgent, &g_originalSend);
}

const bool g_registeredClock = registry::Register({
    "simulators.core.clock",
    "simulators/core",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_ClockStep,
    "Our simulation clock step, message posts and speed changes (docs/simulator.md)",
    false,
    InstallClock,
});

const bool g_registeredAgents = registry::Register({
    "simulators.core.agents",
    "simulators/core",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_TickAgents,
    "Our agent dispatch: the tick's agents, a type's round, the idle agents (docs/simulator.md)",
    false,
    InstallAgents,
});

} // namespace
} // namespace opensc4::core_sim
