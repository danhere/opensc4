// The system service base's SetServiceRunning (0x0091DD91) has one detour
// that two settings use: framework.service.interface (our translation) and
// diagnostics.trace.service_start (a record of the calls). Either puts the
// detour in; each call goes to the trace first when there is one, then to
// our translation or to the game's function (src/core/framework_servicebase.cpp).
#pragma once

#include <cstdint>

namespace opensc4::framework {

using ServiceRunningTrace = void (*)(char* service, uint8_t running);

// Puts the shared detour in (once) and has every call reported to `trace`
// before it is carried out. False when the detour could not be installed.
bool TraceSetServiceRunning(ServiceRunningTrace trace);

} // namespace opensc4::framework
