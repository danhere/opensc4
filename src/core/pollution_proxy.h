// Experiment: a pass-through cISC4PollutionSimulator registered under the
// game's own class ID. See docs/experiments/pollution-proxy.md.
#pragma once

#include <cstdint>

class cIGZUnknown;

namespace opensc4::pollution_proxy {

// cSC4PollutionSimulator's class ID and the interface the city requests.
constexpr uint32_t kClassId = 0x8990C065;
constexpr uint32_t kIid = 0x877181EC;

// GZCOM type-1 factory: creates the original simulator through the game's
// own factory and wraps its cISC4PollutionSimulator interface. Build 641 only.
cIGZUnknown* Create();

// The most recently created proxy (as its cISC4PollutionSimulator pointer), or
// null. Lets a test check whether the city ended up holding the proxy.
const void* LastCreated();

} // namespace opensc4::pollution_proxy
