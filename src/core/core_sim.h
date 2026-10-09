// Our cSC4Simulator (build 641), the simulation's clock and scheduler:
// what the parts share (docs/simulator.md).
#pragma once

#include "core/game_access.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::core_sim {

// Fields of cSC4Simulator (offsets from the object's start; 0x1E0 bytes).
// Parts: cISC4Simulator at +0, cIGZSerializable at +0x4, the system
// service (cRZBaseSystemService) at +0x8, cIGZMessageTarget2 at +0x24.
namespace field {
constexpr std::size_t kRefs = 0x28;
constexpr std::size_t kInitialised = 0x2C;      // uint8
constexpr std::size_t kSaved2D = 0x2D;          // uint8, saved
constexpr std::size_t kEstablished = 0x2E;      // uint8: city established (slot 33)
constexpr std::size_t kPaused = 0x30;           // pause count (slots 9, 12)
constexpr std::size_t kHiddenPaused = 0x34;     // (slots 10, 13); Init sets 1, DoPostCityInit resumes
constexpr std::size_t kEmergencyPaused = 0x38;  // (slots 11, 14)
constexpr std::size_t kSpeed = 0x3C;            // 0-2, 3 = none yet
constexpr std::size_t kSaved40 = 0x40;
constexpr std::size_t kSpeedChangeable = 0x44;  // uint8: cleared while emergency paused
constexpr std::size_t kStepWatch = 0x48;        // GZStopwatch: time since the clock's last step
constexpr std::size_t kWatch60 = 0x60;          // GZStopwatch
constexpr std::size_t kMaxMsPerTick = 0x78;     // slot 31; the idle work's budget
constexpr std::size_t kPacing = 0x7C;           // the clock's paced unit: 2 minutes, 3 hours, 4 days
constexpr std::size_t kStepMs = 0x80;           // minimum time per step at this speed
constexpr std::size_t kFpsCap = 0x84;           // 30, 20, 15 by speed
constexpr std::size_t kCycles = 0x88;           // cycles-per-millisecond estimate (float)
constexpr std::size_t kAgentData = 0xB0;        // uint32[11]: data 3 of each agent type's message
constexpr std::size_t kMinutes = 0xB8;          // kAgentData[2]
constexpr std::size_t kHours = 0xBC;            // kAgentData[3]
constexpr std::size_t kDayOfYear = 0xC0;        // kAgentData[4]
constexpr std::size_t kWeek = 0xC4;
constexpr std::size_t kMonth = 0xC8;
constexpr std::size_t kYear = 0xCC;
constexpr std::size_t kAgentDue = 0xDC;         // uint8[11]: this tick's units passed, per agent type
constexpr std::size_t kDate = 0xE8;             // the simulation date (cIGZDate at +0xF0)
constexpr std::size_t kDateInterface = 0xF0;
constexpr std::size_t kMessageServer = 0xF8;
constexpr std::size_t kStepsPerSpeed = 0xFC;    // uint32[3]: ms per step at speeds 0-2 (5000, 1000, 100)
constexpr std::size_t kDilations = 0x108;       // vector of float: animation time dilations (slot 32)
constexpr std::size_t kAgents = 0x114;          // 11 maps (header, count, word) of agents by type
constexpr std::size_t kSliceWatch = 0x198;      // GZStopwatch: the running time slice
constexpr std::size_t kCursors = 0x1B0;         // node*[11]: each type's place in its map
} // namespace field

constexpr int kAgentTypes = 11;

// An agent map's fields: the header node, the count.
inline char*& AgentHeader(void* self, int type) { return At<char*>(self, field::kAgents + 12 * type); }
inline uint32_t& AgentCount(void* self, int type) { return At<uint32_t>(self, field::kAgents + 12 * type + 4); }
inline char*& Cursor(void* self, int type) { return At<char*>(self, field::kCursors + 4 * type); }

// An agent node (SGI red-black tree, 0x30 bytes): colour, parent (+4),
// left (+8), right (+0xC), then the agent (+0x10), enabled (+0x14), its
// name (a string at +0x18) and flags (+0x2C: bit 0 enabled before the city
// is established, bit 1 after).
inline char* Next(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>(address)(self, args...);
}

void FirstCall(const char* name, bool& logged);

// A cGZMessage (RZ::operator_new(0x2C)) with a type and data 1 to 3,
// referenced once.
void* NewMessage(uint32_t type, uint32_t data1, uint32_t data2, uint32_t data3);

} // namespace opensc4::core_sim
