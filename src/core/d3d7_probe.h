// Diagnostic replacement diagnostics.d3d7_probe: logs the stock DirectX
// driver's Direct3D 7 device state on shadow and alpha-tested draws
// (d3d7_probe.cpp explains how).
#pragma once

namespace opensc4::d3d7_probe {

// Logs how many draws each logged state had (no-op unless installed).
void LogCounts();

} // namespace opensc4::d3d7_probe
