// A test's way to pick the Data Views window's map (docs/renderer.md, "The
// data map").
#pragma once

#include <cstdint>

namespace opensc4::datamap_test {

// The map window shows this map (1 and up, in the order of the window's own
// list; 0 for none) from its next message on. Needs
// diagnostics.datamap_select and the window open.
void Select(uint32_t map);

} // namespace opensc4::datamap_test
