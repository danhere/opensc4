// determinism.dispatch_info: the record of a dispatch that is gone reads the
// same in every run (docs/experiments/determinism.md, "A dispatch called off before its
// message"). For test runs only: play keeps the game's.
//
// The dispatch manager's GetDispatchInfo (0x00469D40) copies a dispatch's
// record (0x2C bytes) to the caller's and answers false, leaving the caller's
// record alone, when there is no dispatch of that ID. The tutorial system's
// handler of the message 0xE94671EB (0x00444480, the call at 0x004447A6)
// does not test the answer: it takes the position out of its record, a local
// it has not set, and moves the tutorial on a step when the cell lies in the
// step's rectangle. The message is posted, so a dispatch called off in the
// tick that made it is gone when the message comes, and the position is
// whatever the receiver before the tutorial system left on the stack. A run
// then follows which code that receiver is (the game's or ours), though
// both do the same. Here a record not found is cleared and its position put
// far off the map, so the dispatch that is gone is at no step's place in
// any run. Zeros alone would not do: the position would be the map's corner
// cell, which a step with no rectangle set takes for its place.
#include "core/address_check.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>

namespace opensc4::dispatch_info {
namespace {

constexpr std::size_t kRecordSize = 0x2C;
constexpr std::size_t kPositionX = 0x14; // float
constexpr std::size_t kPositionZ = 0x1C; // float
constexpr float kNowhere = -1.0e9f;      // off every map

using GetFn = uint8_t(__fastcall*)(void* self, void* edx, uint32_t id, void* record);
GetFn g_get = nullptr;

uint8_t __fastcall Get(void* self, void*, uint32_t id, void* record)
{
    const uint8_t found = g_get(self, nullptr, id, record);
    if (!found && record) {
        std::memset(record, 0, kRecordSize);
        std::memcpy(static_cast<char*>(record) + kPositionX, &kNowhere, sizeof kNowhere);
        std::memcpy(static_cast<char*>(record) + kPositionZ, &kNowhere, sizeof kNowhere);
    }
    return found;
}

bool Install(registry::Mode)
{
    const addr::Entry* get = addr_check::Find(addr::cSC4DispatchManager_GetDispatchInfo);
    return get && hook::Install(*get, &Get, &g_get);
}

const bool g_registered = registry::Register({
    "determinism.dispatch_info",
    "determinism",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DispatchManager_GetDispatchInfo,
    "Test runs: the record of a dispatch that no longer exists is cleared, with a place off the map, not what the stack held",
    false,
    Install,
});

} // namespace
} // namespace opensc4::dispatch_info
