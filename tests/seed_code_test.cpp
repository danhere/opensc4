// Seed codes: formatting and parsing what players type.
#include "core/seed_code.h"

#include <cstdint>
#include <cstdio>

using namespace opensc4;

namespace {

int Fail(const char* what)
{
    std::printf("seed code test FAILED: %s\n", what);
    return 1;
}

bool Parses(const char* text, std::uint32_t expected)
{
    std::uint32_t seed = 0;
    return seed_code::Parse(text, &seed) && seed == expected;
}

bool Rejects(const char* text)
{
    std::uint32_t seed = 0x12345678;
    return !seed_code::Parse(text, &seed) && seed == 0x12345678;
}

} // namespace

int main()
{
    if (seed_code::Format(0x1F2E3D4C) != "1F2E-3D4C" || seed_code::Format(0) != "0000-0000")
        return Fail("format");
    if (!Parses("1F2E-3D4C", 0x1F2E3D4C) || !Parses("1f2e3d4c", 0x1F2E3D4C) || !Parses("FFFFFFFF", 0xFFFFFFFF))
        return Fail("valid codes");
    if (!Rejects("") || !Rejects("1F2E-3D4") || !Rejects("1F2E-3D4C5") || !Rejects("1F2-E3D4C") ||
        !Rejects("1F2E--3D4C") || !Rejects("G0000000") || !Rejects("1F2E 3D4C") || !Rejects("random"))
        return Fail("invalid codes leave the seed alone");
    for (std::uint32_t seed : {0u, 1u, 0xDEADBEEFu, 0xFFFFFFFFu})
        if (!Parses(seed_code::Format(seed).c_str(), seed))
            return Fail("round trip");
    std::printf("seed code test OK\n");
    return 0;
}
