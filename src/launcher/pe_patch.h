// Setting the large-address-aware flag on a copy of the game's exe, as the
// community's 4GB patch does to the exe itself (docs/crashes.md).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opensc4::launcher {

// The PE checksum of `image` (the algorithm of imagehlp's CheckSumMappedFile:
// 16-bit one's-complement sum of the file with the checksum field taken as 0,
// plus the file length). `checksumOffset` is the file offset of the field.
std::uint32_t PeChecksum(const std::vector<std::uint8_t>& image, std::size_t checksumOffset);

// Sets IMAGE_FILE_LARGE_ADDRESS_AWARE in a 32-bit PE image and recomputes
// its checksum. Returns false (with `error`) if `image` is not a 32-bit PE.
// `alreadySet` tells whether the flag was set before.
bool SetLargeAddressAware(std::vector<std::uint8_t>& image, bool* alreadySet, std::string* error);

} // namespace opensc4::launcher
