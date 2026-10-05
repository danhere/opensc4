// Diagnostics registered as replacements under "diagnostics.*" that need work
// at shutdown (coverage results).
#pragma once

namespace opensc4::diagnostics {

// Writes results (opensc4-coverage.csv next to opensc4.log) and logs a summary.
void OnShutdown();

} // namespace opensc4::diagnostics
