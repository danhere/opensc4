#include "core/gd/vertex_format.h"

namespace opensc4::gd {
namespace {

constexpr uint32_t kFormatBit = 0x80000000;

// Per element type (tables at 0x00ACC0AC, 0x00ACC0D4, 0x00ACC0FC, 0x00ACC120):
// bytes per element, bit position and mask of its count, and the mask of the
// element types laid out before it.
constexpr uint32_t kSize[kElementCount] = {12, 4, 4, 12, 4, 4, 4, 8, 12, 16};
constexpr uint32_t kShift[kElementCount] = {0, 2, 5, 6, 7, 8, 10, 14, 18, 22};
constexpr uint32_t kMask[kElementCount] = {3, 7, 1, 1, 1, 3, 15, 15, 15, 15};
constexpr uint32_t kBefore[kElementCount] = {0xF, 0, 0x3, 0x1F, 0x3F, 0x7F, 0xFF, 0x3FF, 0x3FFF, 0x3FFFF};

uint32_t Count(uint32_t format, uint32_t type)
{
    return (format >> kShift[type]) & kMask[type];
}

} // namespace

uint32_t ExpandFormat(uint32_t format)
{
    if (format & kFormatBit)
        return format;
    switch (format) {
    case 0x01: return 0x80000101; // position, color
    case 0x02: return 0x80004001; // position, 1 texcoord
    case 0x03: return 0x80008001; // position, 2 texcoords
    case 0x0A: return 0x80004101; // position, color, 1 texcoord
    case 0x0B: return 0x80008101; // position, color, 2 texcoords
    case 0x20: return 0x80000001; // position
    case 0x21: return 0x80000041; // position, normal
    case 0x22: return 0x80000141; // position, normal, color
    case 0x23: return 0x80004041; // position, normal, 1 texcoord
    case 0x24: return 0x80008041; // position, normal, 2 texcoords
    case 0x25: return 0x80004141; // position, normal, color, 1 texcoord
    case 0x26: return 0x80008141; // position, normal, color, 2 texcoords
    default: return 0;
    }
}

uint32_t MakeFormat(uint32_t count, const uint32_t* elements)
{
    uint32_t counts[kElementCount] = {};
    for (uint32_t i = 0; i < count; ++i)
        if (elements[i] < kElementCount)
            ++counts[elements[i]];
    uint32_t format = kFormatBit;
    for (uint32_t type = 0; type < kElementCount; ++type)
        format |= (counts[type] & kMask[type]) << kShift[type];
    return format;
}

uint32_t Stride(uint32_t format)
{
    format = ExpandFormat(format);
    // 0x008D0370 adds the second group only if the low nibble is non-zero;
    // kept as is so odd formats come out the same.
    uint32_t dwords = Count(format, 6) + Count(format, 7) * 2 + Count(format, 5) + Count(format, 0) * 3;
    if (format & 0xF)
        dwords += (Count(format, 8) + Count(format, 3)) * 3 + Count(format, 9) * 4 + Count(format, 1) +
                  Count(format, 4) + Count(format, 2);
    return dwords * 4;
}

uint32_t ElementCount(uint32_t format, Element element)
{
    const auto type = static_cast<uint32_t>(element);
    return type < kElementCount ? Count(ExpandFormat(format), type) : 0;
}

uint32_t ElementOffset(uint32_t format, Element element, uint32_t index)
{
    const auto type = static_cast<uint32_t>(element);
    if (type >= kElementCount)
        return 0;
    uint32_t offset = kSize[type] * index;
    if (type > 0)
        offset += Stride((kBefore[type] | kFormatBit) & ExpandFormat(format));
    return offset;
}

} // namespace opensc4::gd
