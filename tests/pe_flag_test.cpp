// The launcher's large-address-aware patch: flag set, checksum recomputed,
// and anything that is not a 32-bit PE refused.
#include "launcher/pe_patch.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace opensc4::launcher;

namespace {

int Fail(const char* what)
{
    std::printf("pe flag test FAILED: %s\n", what);
    return 1;
}

// A minimal 32-bit PE: MZ header, PE at 0x80, file header, optional header.
std::vector<std::uint8_t> MakeImage(std::uint16_t magic)
{
    std::vector<std::uint8_t> image(0x200, 0x11);
    image[0] = 'M';
    image[1] = 'Z';
    image[0x3C] = 0x80;
    image[0x3D] = image[0x3E] = image[0x3F] = 0;
    image[0x80] = 'P';
    image[0x81] = 'E';
    image[0x82] = image[0x83] = 0;
    image[0x84 + 18] = 0x0F; // characteristics 0x010F, as the game's
    image[0x84 + 19] = 0x01;
    image[0x98] = static_cast<std::uint8_t>(magic);
    image[0x99] = static_cast<std::uint8_t>(magic >> 8);
    return image;
}

std::uint32_t At32(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (std::uint32_t(b[at + 3]) << 24);
}

} // namespace

int main()
{
    std::vector<std::uint8_t> image = MakeImage(0x10B);
    bool was = true;
    std::string error;
    if (!SetLargeAddressAware(image, &was, &error) || was)
        return Fail("patching a 32-bit image");
    if (image[0x84 + 18] != 0x2F || image[0x84 + 19] != 0x01)
        return Fail("characteristics not 0x012F");
    const std::size_t checksum = 0x98 + 64;
    if (At32(image, checksum) != PeChecksum(image, checksum) || At32(image, checksum) == 0)
        return Fail("checksum not recomputed");
    // A second patch keeps the image as it is.
    std::vector<std::uint8_t> again = image;
    if (!SetLargeAddressAware(again, &was, &error) || !was || again != image)
        return Fail("patching twice");
    // The checksum skips its own field and adds the length.
    const std::vector<std::uint8_t> small = {1, 0, 2, 0, 0xFF, 0xFF, 0xFF, 0xFF, 3};
    if (PeChecksum(small, 4) != 1 + 2 + 3 + 9)
        return Fail("checksum arithmetic");
    std::vector<std::uint8_t> pe64 = MakeImage(0x20B);
    if (SetLargeAddressAware(pe64, nullptr, &error))
        return Fail("accepting a 64-bit image");
    std::vector<std::uint8_t> junk(0x200, 0);
    if (SetLargeAddressAware(junk, nullptr, &error))
        return Fail("accepting a non-PE file");
    std::printf("pe flag test OK\n");
    return 0;
}
