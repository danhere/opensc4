// Counts calls through a game interface without knowing its methods'
// signatures: a wrapper whose vtable slots (after QueryInterface, AddRef and
// Release) are thunks that count the call and jump to the same slot of the
// wrapped object with `this` replaced. Only for interfaces whose methods are
// all __thiscall, which is every virtual method the game's compilers produced.
#pragma once

#include <atomic>
#include <cstdint>

namespace opensc4::interface_counter {

constexpr int kMaxSlots = 24;

// Per-interface call counts, by vtable slot.
struct Counters {
    const char* name;
    uint32_t iid;
    int slots; // including QueryInterface, AddRef and Release
    std::atomic<uint32_t> calls[kMaxSlots];
};

// Returns a wrapper around `inner` (an interface pointer the caller holds one
// reference to, which the wrapper takes over). The wrapper answers
// QueryInterface for `counters.iid` and cIGZUnknown with itself and forwards
// other IIDs to `inner`. Returns `inner` unchanged if counters.slots is out of
// range.
void* Wrap(void* inner, Counters& counters);

// Logs the slots that were called.
void Log(const Counters& counters);

} // namespace opensc4::interface_counter
