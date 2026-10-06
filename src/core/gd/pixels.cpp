#include "core/gd/pixels.h"

#include "core/gd/gd_types.h"

#include <cstring>

namespace opensc4::gd {
namespace {

// Expands an n-bit field to 8 bits.
uint8_t Expand(uint32_t value, uint32_t bits)
{
    const uint32_t max = (1u << bits) - 1;
    return static_cast<uint8_t>((value * 255 + max / 2) / max);
}

// Component layout of a source format: how many components and where R, G,
// B, A come from (-1: not present).
struct Layout {
    uint32_t components;
    int r, g, b, a;
};

bool LayoutOf(uint32_t format, Layout& layout)
{
    switch (format) {
    case RGB: layout = {3, 0, 1, 2, -1}; return true;
    case RGBA: layout = {4, 0, 1, 2, 3}; return true;
    case BGR: layout = {3, 2, 1, 0, -1}; return true;
    case BGRA: layout = {4, 2, 1, 0, 3}; return true;
    case Alpha: layout = {1, -1, -1, -1, 0}; return true;
    case Luminance: layout = {1, 0, 0, 0, -1}; return true;
    case LuminanceAlpha: layout = {2, 0, 0, 0, 1}; return true;
    default: return false;
    }
}

// Unpacks one pixel's components (in source order) as 8-bit values.
bool Unpack(uint32_t type, const uint8_t* p, uint32_t components, uint8_t out[4], uint32_t& bytes)
{
    switch (type) {
    case UnsignedByte:
        std::memcpy(out, p, components);
        bytes = components;
        return true;
    case UnsignedShort4444: { // first component in the top bits
        const uint32_t v = p[0] | (p[1] << 8);
        for (int i = 0; i < 4; ++i)
            out[i] = Expand((v >> (12 - 4 * i)) & 15, 4);
        bytes = 2;
        return components == 4;
    }
    case UnsignedShort4444Rev: { // first component in the bottom bits
        const uint32_t v = p[0] | (p[1] << 8);
        for (int i = 0; i < 4; ++i)
            out[i] = Expand((v >> (4 * i)) & 15, 4);
        bytes = 2;
        return components == 4;
    }
    case UnsignedShort5551: {
        const uint32_t v = p[0] | (p[1] << 8);
        out[0] = Expand((v >> 11) & 31, 5);
        out[1] = Expand((v >> 6) & 31, 5);
        out[2] = Expand((v >> 1) & 31, 5);
        out[3] = (v & 1) ? 255 : 0;
        bytes = 2;
        return components == 4;
    }
    case UnsignedInt8888: {
        const uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
        for (int i = 0; i < 4; ++i)
            out[i] = static_cast<uint8_t>(v >> (24 - 8 * i));
        bytes = 4;
        return components == 4;
    }
    default: return false;
    }
}

void Color565(uint32_t c, uint8_t out[4])
{
    out[0] = Expand(c & 31, 5);         // B
    out[1] = Expand((c >> 5) & 63, 6);  // G
    out[2] = Expand((c >> 11) & 31, 5); // R
    out[3] = 255;
}

// Decodes the colour half of a block into 16 BGRA pixels.
void DecodeColors(const uint8_t* block, bool fourColor, uint8_t pixels[16][4])
{
    const uint32_t c0 = block[0] | (block[1] << 8), c1 = block[2] | (block[3] << 8);
    uint8_t palette[4][4];
    Color565(c0, palette[0]);
    Color565(c1, palette[1]);
    if (c0 > c1 || fourColor) {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = static_cast<uint8_t>((2 * palette[0][i] + palette[1][i] + 1) / 3);
            palette[3][i] = static_cast<uint8_t>((palette[0][i] + 2 * palette[1][i] + 1) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i)
            palette[2][i] = static_cast<uint8_t>((palette[0][i] + palette[1][i]) / 2);
        palette[2][3] = 255;
        palette[3][0] = palette[3][1] = palette[3][2] = palette[3][3] = 0;
    }
    const uint32_t bits = block[4] | (block[5] << 8) | (block[6] << 16) | (static_cast<uint32_t>(block[7]) << 24);
    for (int i = 0; i < 16; ++i)
        std::memcpy(pixels[i], palette[(bits >> (2 * i)) & 3], 4);
}

} // namespace

bool IsCompressed(uint32_t pixelFormat)
{
    return pixelFormat >= DXT1 && pixelFormat <= DXT5;
}

uint32_t BlockBytes(uint32_t pixelFormat)
{
    return pixelFormat == DXT1 || pixelFormat == DXT1A ? 8 : 16;
}

bool ConvertToBGRA(uint32_t pixelFormat, uint32_t type, const void* source, uint32_t width, uint32_t height,
                   uint32_t rowLength, uint8_t* target, uint32_t pitch)
{
    Layout layout{};
    if (!LayoutOf(pixelFormat, layout))
        return false;
    if (rowLength == 0)
        rowLength = width;
    // Bytes per source pixel, from the first pixel.
    uint8_t c[4] = {};
    uint32_t bytes = 0;
    if (!Unpack(type, static_cast<const uint8_t*>(source), layout.components, c, bytes))
        return false;
    const auto* src = static_cast<const uint8_t*>(source);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* in = src + static_cast<size_t>(y) * rowLength * bytes;
        uint8_t* out = target + static_cast<size_t>(y) * pitch;
        for (uint32_t x = 0; x < width; ++x, in += bytes, out += 4) {
            Unpack(type, in, layout.components, c, bytes);
            out[0] = layout.b < 0 ? 255 : c[layout.b];
            out[1] = layout.g < 0 ? 255 : c[layout.g];
            out[2] = layout.r < 0 ? 255 : c[layout.r];
            out[3] = layout.a < 0 ? 255 : c[layout.a];
        }
    }
    return true;
}

void DecodeDXT(uint32_t pixelFormat, const void* blocks, uint32_t width, uint32_t height, uint8_t* target,
               uint32_t pitch)
{
    const auto* block = static_cast<const uint8_t*>(blocks);
    const uint32_t blockBytes = BlockBytes(pixelFormat);
    for (uint32_t by = 0; by < (height + 3) / 4; ++by) {
        for (uint32_t bx = 0; bx < (width + 3) / 4; ++bx, block += blockBytes) {
            uint8_t pixels[16][4];
            const uint8_t* colorBlock = blockBytes == 16 ? block + 8 : block;
            DecodeColors(colorBlock, pixelFormat == DXT3 || pixelFormat == DXT5, pixels);
            if (pixelFormat == DXT3) {
                for (int i = 0; i < 16; ++i)
                    pixels[i][3] = Expand((block[i / 2] >> (4 * (i & 1))) & 15, 4);
            } else if (pixelFormat == DXT5) {
                uint8_t alpha[8] = {block[0], block[1]};
                if (alpha[0] > alpha[1]) {
                    for (int i = 1; i < 7; ++i)
                        alpha[i + 1] = static_cast<uint8_t>(((7 - i) * alpha[0] + i * alpha[1] + 3) / 7);
                } else {
                    for (int i = 1; i < 5; ++i)
                        alpha[i + 1] = static_cast<uint8_t>(((5 - i) * alpha[0] + i * alpha[1] + 2) / 5);
                    alpha[6] = 0;
                    alpha[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i)
                    bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i)
                    pixels[i][3] = alpha[(bits >> (3 * i)) & 7];
            }
            for (uint32_t py = 0; py < 4; ++py) {
                const uint32_t y = by * 4 + py;
                if (y >= height)
                    break;
                for (uint32_t px = 0; px < 4; ++px) {
                    const uint32_t x = bx * 4 + px;
                    if (x < width)
                        std::memcpy(target + static_cast<size_t>(y) * pitch + x * 4, pixels[py * 4 + px], 4);
                }
            }
        }
    }
}

} // namespace opensc4::gd
