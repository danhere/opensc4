// P1 step 1: a pass-through cIGZGDriver. At PreAppInit it wraps the game's
// current graphics driver and becomes the current driver, forwarding every call
// and counting them, to show which driver methods the game uses and how often.
// Replacement id: renderer.passthrough (gdriver_proxy.cpp explains how).
#pragma once

namespace opensc4::gdriver_proxy {

// Logs the call counts so far (also logged when the proxy is released).
void LogCallCounts(const char* when);

} // namespace opensc4::gdriver_proxy
