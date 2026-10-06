// GD vertex formats (MakeVertexFormat, VertexFormatStride, ...): reimplements
// the helpers both stock drivers call (0x008D01F0, 0x008D02C0, 0x008D0370,
// 0x008D04A0, 0x008D04F0).
//
// A format is a bit field with bit 31 set: per element type, how many of it
// each vertex has. Smaller values are legacy format IDs that expand to one.
// Elements are laid out in type order, except that type 1 comes first.
#pragma once

#include <cstdint>

namespace opensc4::gd {

enum class Element : uint32_t {
    Position, // 3 floats
    Unknown1, // 1 dword each, up to 7 (blend weights?)
    Unknown2, // 1 dword
    Normal,   // 3 floats
    Unknown4, // 1 dword
    Color,    // D3DCOLOR (BGRA in memory), up to 3
    TexCoord1,
    TexCoord2, // 2 floats each, up to 15
    TexCoord3,
    TexCoord4,
};
constexpr uint32_t kElementCount = 10;

// Legacy IDs expanded; 0 for an unknown one. Bit fields pass through.
uint32_t ExpandFormat(uint32_t format);

// MakeVertexFormat(count, elements): a format with `count` elements of the given types.
uint32_t MakeFormat(uint32_t count, const uint32_t* elements);

uint32_t Stride(uint32_t format);
uint32_t ElementCount(uint32_t format, Element element);
// Byte offset of the `index`-th element of a type.
uint32_t ElementOffset(uint32_t format, Element element, uint32_t index);

} // namespace opensc4::gd
