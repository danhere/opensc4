// fixes.puzzle_piece: the crash when a puzzle piece is held over a
// transit-enabled lot (docs/crashes.md). A solver in the network tool
// (0x0065ED80) takes the next entry to settle: a pending one from its stack,
// or else the entry of its hash table with the smallest range (hi - lo),
// failing on a negative range. When the table is empty, or no range compares
// below FLT_MAX (NaN or infinite), it chooses nothing and reads the chosen
// entry's flags through NULL (0x0065EE40). sc4fix patches the middle of the
// function to take its failure exit then; here an entry detour sees the same
// case before the original runs and returns that failure itself: nothing
// changed, failing id 0 at +0x10, false. The caller (0x00660B10) then stops
// and ignores the outputs, as it does on the original's own failure.
#include "core/address_check.h"
#include "core/crash.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <opensc4/addresses.h>

#include <cfloat>
#include <cstdint>

namespace opensc4::puzzle_piece_fix {
namespace {

using NextFn = bool(__fastcall*)(void* solver, void* edx, uint32_t* id, float* value, bool* changed);
NextFn g_original = nullptr;

// Whether the original would choose no entry: no pending pair, and no node
// whose range is negative (the original's own failure) or below FLT_MAX. The
// game computes the range on the x87 stack; a difference of two floats is
// exact in double, so double compares the same.
bool ChoosesNothing(void* solver)
{
    if (At<uint32_t>(solver, 0x50) != At<uint32_t>(solver, 0x54))
        return false;
    void** bucket = At<void**>(solver, 0x2C);
    void** end = At<void**>(solver, 0x30);
    for (; bucket < end; ++bucket) {
        for (void* node = *bucket; node; node = At<void*>(node, 0)) {
            const double range = static_cast<double>(At<float>(node, 0xC)) - At<float>(node, 0x8);
            if (range < 0.0 || range < FLT_MAX)
                return false;
        }
    }
    return true;
}

bool __fastcall Next(void* solver, void* edx, uint32_t* id, float* value, bool* changed)
{
    bool nothing = false;
    __try {
        nothing = ChoosesNothing(solver);
    } __except (crash::HookFilter("fixes.puzzle_piece", GetExceptionInformation())) {
        nothing = false;
    }
    if (!nothing)
        return g_original(solver, edx, id, value, changed);
    static unsigned s_logged = 0;
    if (s_logged++ < 10)
        log::Write("puzzle_piece_fix: solver %p had no entry to choose; failed it instead of reading NULL", solver);
    *changed = false;
    At<uint32_t>(solver, 0x10) = 0;
    return false;
}

bool Apply(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::PuzzlePieceSolver_NextEntry);
    return entry && hook::Install(*entry, &Next, &g_original);
}

const bool g_registered = registry::Register({
    "fixes.puzzle_piece",
    "fixes",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::PuzzlePieceSolver_NextEntry,
    "Fixes the crash when a puzzle piece is held over a transit-enabled lot (network solver with no entry)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::puzzle_piece_fix
