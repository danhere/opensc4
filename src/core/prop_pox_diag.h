// diagnostics.prop_pox: logs what the save and load do to a city's class
// records (docs/crashes.md, "Prop pox"). Changes nothing.
#pragma once

namespace opensc4::prop_pox_diag {

// Logs the counters (a no-op unless the diagnostic is on).
void LogSummary();

} // namespace opensc4::prop_pox_diag
