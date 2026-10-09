// The simulator's (cSC4Simulator, the clock and scheduler) part of the
// autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cISC4City.h>
#include <cISC4Simulator.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// An agent map's node after `node` (SGI red-black tree increment).
char* NextNode(char* node)
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

// The simulator (build 641 offsets; docs/simulator.md): the pause counts,
// speed, sim time and flags; the clock's minutes and hours with each agent
// type's data and passed flags; per agent type the count, the cursor's
// place in the map and the enabled agents; the time per tick, pacing, time
// per step, frame rate cap, the step times and the dilations.
void Fingerprint(cISC4City* city, std::string& line)
{
    char text[224] = "";
    char* const sim = game::IsBuild641() ? reinterpret_cast<char*>(city->GetSimulator()) : nullptr;
    if (sim) {
        uint32_t agents = 0;
        for (int type = 0; type < 11; ++type) {
            char* const header = At<char*>(sim, 0x114 + 12 * type);
            char* const cursor = At<char*>(sim, 0x1B0 + 4 * type);
            uint32_t place = 0, enabled = 0, index = 0;
            bool found = cursor == header;
            for (char* node = At<char*>(header, 0x8); node != header; node = NextNode(node), ++index) {
                if (node == cursor) {
                    place = index;
                    found = true;
                }
                enabled = enabled * 2 + At<uint8_t>(node, 0x14);
            }
            agents = agents * 31 + At<uint32_t>(sim, 0x118 + 12 * type);
            agents = agents * 31 + (found ? (cursor == header ? 0xFFFFu : place) : 0xEEEEu);
            agents = agents * 31 + enabled;
        }
        const float* const dilations = At<const float*>(sim, 0x108);
        std::snprintf(text, sizeof text,
                      " | sim p %d %d %d sp %d t %d f %u%u%u clock %u:%u %08X %08X agents %08X tick %u %u %u %u "
                      "steps %08X dil %08X",
                      At<int32_t>(sim, 0x30), At<int32_t>(sim, 0x34), At<int32_t>(sim, 0x38), At<int32_t>(sim, 0x3C),
                      At<int32_t>(sim, 0x40), At<uint8_t>(sim, 0x2D), At<uint8_t>(sim, 0x2E), At<uint8_t>(sim, 0x44),
                      At<uint32_t>(sim, 0xBC), At<uint32_t>(sim, 0xB8), BytesChecksum(sim + 0xB0, 44),
                      BytesChecksum(sim + 0xDC, 11), agents, At<uint32_t>(sim, 0x78), At<uint32_t>(sim, 0x7C),
                      At<uint32_t>(sim, 0x80), At<uint32_t>(sim, 0x84), BytesChecksum(sim + 0xFC, 12),
                      dilations ? BytesChecksum(dilations, 20) : 0u);
    }
    line += text;
}

// OPENSC4_TEST_SIMULATOR=<n>: on the n-th day the simulator's interface
// slots that the test cities never call are called with values that leave
// it as it was: each pause with its resume, the paused queries, the dates,
// the first agent's enabled flag read and set again, the city established
// flag set to the other value and back, the sim time and time per tick set
// to themselves, the animation dilation.
void DriveSimulator(cISC4City* city)
{
    char* const sim = reinterpret_cast<char*>(city->GetSimulator());
    if (!sim)
        return;
    uint32_t answers = 0;
    auto slot = [sim](std::size_t offset) { return Virtual<uint32_t>(sim, offset); };
    slot(0x24);
    answers = answers * 31 + slot(0x3C) + slot(0x40) + slot(0x44) + slot(0x48);
    slot(0x30);
    slot(0x28);
    answers = answers * 31 + slot(0x3C) + slot(0x40) + slot(0x44) + slot(0x48);
    slot(0x34);
    slot(0x2C);
    answers = answers * 31 + slot(0x3C) + slot(0x40) + slot(0x44) + slot(0x48);
    slot(0x38);
    uint32_t parts[5] = {};
    Virtual<void>(sim, 0x18, &parts[0], &parts[1], &parts[2], &parts[3], &parts[4]);
    answers = answers * 31 + BytesChecksum(parts, sizeof parts);
    answers = answers * 31 + static_cast<uint32_t>(Virtual<int32_t>(sim, 0x20));
    char* const header = At<char*>(sim, 0x114 + 12 * 4);
    char* const first = At<char*>(header, 0x8);
    if (first != header) {
        void* const name = first + 0x18;
        const uint8_t enabled = Virtual<uint8_t>(sim, 0x64, name);
        Virtual<void>(sim, 0x68, name, enabled);
        answers = answers * 31 + enabled;
    }
    const uint8_t established = At<uint8_t>(sim, 0x2E);
    Virtual<void>(sim, 0x84, static_cast<uint8_t>(established ? 0 : 1));
    Virtual<void>(sim, 0x84, established);
    Virtual<int32_t>(sim, 0x78, Virtual<int32_t>(sim, 0x74));
    Virtual<uint32_t>(sim, 0x7C, At<uint32_t>(sim, 0x78));
    const float dilation = Virtual<float>(sim, 0x80);
    uint32_t bits;
    std::memcpy(&bits, &dilation, sizeof bits);
    log::Write("autotest: simulator driven (answers %08X, dilation %08X)", answers, bits);
}

void Drive(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_SIMULATOR") && game::IsBuild641())
        DriveSimulator(day.city);
}

const FingerprintPart kFingerprint{"sim", 140, &Fingerprint};
const DayHook kDrive{"simulator.drive", 170, &Drive};

} // namespace
} // namespace opensc4::autotest
