// Shared between the parts of our cSC4TractDeveloper (build 641;
// docs/tract.md): the object's fields as far as they are known, and the
// helpers the parts share. The tract developer picks the lots and buildings
// that grow on zoned land (Grow), declines them (Decline), keeps the
// population of tracts (UpdatePopulation) and the building styles.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tract_sim {

// Fields of cSC4TractDeveloper (offsets from the object's start; 0x188
// bytes; constructor 0x0070C900; class ID 0x2990C142). Names are ours. The
// interfaces: +0x0 the primary cISC4TractDeveloper (0x00AB3118, 16 slots),
// +0x4 cIGZMessageTarget2 (0x00AB3104, DoMessage 0x007048E0), +0x8
// cIGZSerializable (0x00AB30EC: Write 0x00704B60, Read 0x00707950,
// GetGZCLSID 0x007045A0). Each part lists the fields it uses, with their
// evidence, in its file's header comment (docs/tract.md has the summary).

// Logs a part's first call once (defined in tract_object.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::tract_sim
