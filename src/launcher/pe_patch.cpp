#include "launcher/pe_patch.h"

namespace opensc4::launcher {
namespace {

std::uint32_t Read32(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (std::uint32_t(b[at + 3]) << 24);
}

std::uint16_t Read16(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

void Write16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v)
{
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

void Write32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}

constexpr std::uint16_t kLargeAddressAware = 0x0020;
constexpr std::uint16_t kPe32Magic = 0x10B;

} // namespace

std::uint32_t PeChecksum(const std::vector<std::uint8_t>& image, std::size_t checksumOffset)
{
    std::uint64_t sum = 0;
    const std::size_t size = image.size();
    for (std::size_t i = 0; i < size; i += 2) {
        if (i == checksumOffset || i == checksumOffset + 2)
            continue;
        std::uint32_t word = image[i];
        if (i + 1 < size)
            word |= image[i + 1] << 8;
        sum += word;
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<std::uint32_t>(sum + size);
}

bool SetLargeAddressAware(std::vector<std::uint8_t>& image, bool* alreadySet, std::string* error)
{
    auto fail = [&](const char* why) {
        if (error)
            *error = why;
        return false;
    };
    if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z')
        return fail("not an MZ executable");
    const std::size_t pe = Read32(image, 0x3C);
    // Signature (4), file header (20), then the optional header's magic and
    // up to its checksum field at +64.
    if (pe + 24 + 68 > image.size() || Read32(image, pe) != 0x00004550)
        return fail("no PE header");
    const std::size_t characteristics = pe + 4 + 18;
    const std::size_t optional = pe + 24;
    if (Read16(image, optional) != kPe32Magic)
        return fail("not a 32-bit PE image");
    const std::uint16_t flags = Read16(image, characteristics);
    if (alreadySet)
        *alreadySet = (flags & kLargeAddressAware) != 0;
    Write16(image, characteristics, static_cast<std::uint16_t>(flags | kLargeAddressAware));
    const std::size_t checksum = optional + 64;
    Write32(image, checksum, PeChecksum(image, checksum));
    return true;
}

} // namespace opensc4::launcher
