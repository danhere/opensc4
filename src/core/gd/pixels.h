// Texture upload conversions: the game's source formats (LoadTextureLevel's
// format and type, GL semantics) to 32-bit BGRA, and DXT block decoding.
#pragma once

#include <cstdint>

namespace opensc4::gd {

bool IsCompressed(uint32_t pixelFormat);
// Bytes per 4x4 block of a DXT format (8 or 16).
uint32_t BlockBytes(uint32_t pixelFormat);

// Converts `width` x `height` pixels, source rows `rowLength` pixels apart, to
// BGRA rows `pitch` bytes apart. Alpha-only sources become white with alpha,
// luminance sources grey. False for an unsupported format/type pair.
bool ConvertToBGRA(uint32_t pixelFormat, uint32_t type, const void* source, uint32_t width, uint32_t height,
                   uint32_t rowLength, uint8_t* target, uint32_t pitch);

// Decodes DXT1/1A/3/5 blocks (rows of ceil(width / 4) blocks) to BGRA rows.
void DecodeDXT(uint32_t pixelFormat, const void* blocks, uint32_t width, uint32_t height, uint8_t* target,
               uint32_t pitch);

} // namespace opensc4::gd
