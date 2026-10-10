// The tract developer's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

// The tract developer (the city's slot 80; build 641; docs/tract.md): the
// current style (slot 8), the style index +0x124, the years since the
// last style change +0x128, one style at a time +0x12C, the years between
// changes +0x7C and the active styles (+0x118, a vector of uint32).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const tract = game::IsBuild641() ? Virtual<void*>(city, 0x140) : nullptr;
    if (!tract) {
        line += " | tract none";
        return;
    }
    const uint32_t* const begin = At<uint32_t*>(tract, 0x118);
    const uint32_t* const end = At<uint32_t*>(tract, 0x11C);
    char text[96];
    std::snprintf(text, sizeof text, " | tract %X %d %d %u %d n %u %08X", Virtual<uint32_t>(tract, 0x20),
                  At<int32_t>(tract, 0x124), At<int32_t>(tract, 0x128), At<uint8_t>(tract, 0x12C),
                  At<int32_t>(tract, 0x7C), static_cast<uint32_t>(end - begin),
                  begin ? BytesChecksum(begin, static_cast<std::size_t>(end - begin) * 4) : 0u);
    line += text;
}

const FingerprintPart kFingerprint{"tract", 92, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
