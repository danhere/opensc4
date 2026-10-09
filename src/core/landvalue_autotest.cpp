// The land value simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"

#include <cISC4City.h>
#include <cISC4LandValueSimulator.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Land value (the total grid is in the main line): the intrinsic and
// slope grids, the average land value, the mode and average altitude
// (float bits) and the step's state (+0x78, +0x38; docs/landvalue.md).
void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4LandValueSimulator* land = city->GetLandValueSimulator();
    char landText[96] = "";
    if (land && game::IsBuild641()) {
        const float mode = land->GetModeAltitude();
        uint32_t modeBits, altitudeBits;
        std::memcpy(&modeBits, &mode, sizeof modeBits);
        std::memcpy(&altitudeBits, reinterpret_cast<const char*>(land) + 0x78, sizeof altitudeBits);
        std::snprintf(landText, sizeof landText, " | lv %08X %08X avg %u mode %08X alt %08X st %d",
                      GridChecksum(land->GetIntrinsicLandValueMap()), GridChecksum(land->GetSlopeMap()),
                      land->GetAverageLandValue(), modeBits, altitudeBits,
                      *reinterpret_cast<const int32_t*>(reinterpret_cast<const char*>(land) + 0x38));
    }
    line += landText;
}

const FingerprintPart kFingerprint{"landvalue", 100, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
