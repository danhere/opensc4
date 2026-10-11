// Shared between the parts of our cSTETerrain (build 641): the city's
// terrain object, the service at 0x00B43CF4 (addr::SC4_Terrain), primary
// vtable 0x00AB3D18 (98 slots; cISTETerrain : cISTETerrainMap of gzcom-dll,
// plus slots 90 to 97 the header does not have), code 0x00740060 to
// 0x00751000. This header holds the object's fields as far as they are
// established (all from the disassembly; evidence named per field) and the
// helpers the parts share. Later pieces extend it.
//
// The terrain is a grid of (cells + 1) x (cells + 1) vertices, 16 metres
// apart, kept as one array of 36-byte records (+0x6C). The water depth and
// the sediment of each vertex live in a second array of 36-byte records with
// the same indexing, owned by another object (the service 0x00B43CF0, kept at
// +0x24; its array pointer at its +0x18).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::terrain_sim {

// Fields of cSTETerrain (offsets from the object's start). Names are ours.
// "Init" is slot 3 (0x0074FEA0), which sets most of them at 0x0074FEC2 to
// 0x007500E0.
namespace field {
constexpr std::size_t kInitialized = 0xC;   // uint8: Init sets 1; slot 5 (0x005B9D30) returns it
constexpr std::size_t kRefCount = 0x10;     // slot 1 (0x004F7BA0) increments it
constexpr std::size_t kTerrainType = 0x18;  // int32: slot 42 (0x0040CF10) returns it; saved (0x007453E0)
constexpr std::size_t kCity = 0x1C;         // the city (0x00B43CEC), taken by Init
constexpr std::size_t kWater = 0x24;        // the service 0x00B43CF0, taken by Init: the water records are at its +0x18
constexpr std::size_t kCellsX = 0x28;       // uint32: the city's slot 93 (+0x174); slot 7 returns it
constexpr std::size_t kCellsZ = 0x2C;       // uint32: the city's slot 94 (+0x178); slot 8 returns it
constexpr std::size_t kLastCellX = 0x30;    // cells x - 1 (Init); GetAltitude and IsPointHidden clamp to it
constexpr std::size_t kLastCellZ = 0x34;    // cells z - 1
constexpr std::size_t kVertsX = 0x38;       // cells x + 1: vertices per row (the index is kVertsX * z + x)
constexpr std::size_t kVertsZ = 0x3C;       // cells z + 1
constexpr std::size_t kCellRect = 0x40;     // int32[4]: 0, 0, cells x, cells z (Init); SetSeaLevel (0x00743550) gives it to the water object and to slot 68 as the whole terrain
constexpr std::size_t kSizeX = 0x50;        // float: cells x * 16 (Init); LocationIsInBounds
constexpr std::size_t kSizeZ = 0x54;        // float: cells z * 16
constexpr std::size_t kVertexCount = 0x5C;  // kVertsX * kVertsZ (Init)
constexpr std::size_t kLoaded = 0x68;       // uint8: slot 96 (0x00747800) stores whether its three reads succeeded
constexpr std::size_t kVertices = 0x6C;     // the vertex records (36 bytes each, kVertexCount of them)
constexpr std::size_t kMinHeight = 0x70;    // float: slot 21; saved
constexpr std::size_t kMaxHeight = 0x74;    // float: slot 22; saved
constexpr std::size_t kSeaLevel = 0x7C;     // float: slot 29; Init copies the float at 0x00B4B404; SetSeaLevel (0x00743550) writes it
constexpr std::size_t kBrushStep = 0xBC;    // uint32: index into a brush record's arrays (slots 61, 62)
constexpr std::size_t kAccumulating = 0xC0; // uint8: slot 80; set by slot 78 (0x007420B0), cleared by slot 79 (0x00749A70)
constexpr std::size_t kAccumulated = 0xC4;  // int32[4]: the rectangle gathered while accumulating (empty: 0x7FFFFFFF x2, 0x80000000 x2)
constexpr std::size_t kShowWater = 0xDD;    // uint8: slot 83; slot 84 (0x007434E0) sets it and posts message 0x6AB97795
constexpr std::size_t kWaterVertices = 0xE4; // uint32: slot 54 (0x0074C6E0) returns it
constexpr std::size_t kView = 0xE8;         // the terrain view object (slot 66 returns it + 0xC); the Renderer's
constexpr std::size_t kViewActive = 0xEC;   // uint8: the flag setters tell the view (0x00752AF0) when it is set
} // namespace field

// A vertex record (36 bytes; the array at field::kVertices).
namespace vertex {
constexpr uint32_t kSize = 0x24;
constexpr std::size_t kAltitude = 0x0; // float; saved (0x00745560)
constexpr std::size_t kNormal = 0x4;   // float[3]; slot 28 copies them
constexpr std::size_t kFlags = 0x22;   // uint16; saved (0x007456D0)
// +0x10 to +0x21 are not read by the queries (the view's and the lighting's).
} // namespace vertex

// Bits of a vertex record's flags word (the vertex stands for the cell whose
// first corner it is).
namespace flag {
constexpr uint16_t kEvenHidden = 0x0020;  // slots 72, 74; cell flag 1
constexpr uint16_t kOddHidden = 0x0040;   // slots 73, 75; cell flag 2
constexpr uint16_t kCell4 = 0x0200;       // cell flag 4 (slots 81, 82)
constexpr uint16_t kFlipped1 = 0x1000;    // slot 93 (0x00741180) sets it; slot 70 tests both
constexpr uint16_t kFlipped2 = 0x2000;    // slot 94 (0x007411F0); cell flag 8
constexpr uint16_t kLocked = 0x4000;      // LockAltitude (slot 52, 0x00741F90)
} // namespace flag

// The water object (service 0x00B43CF0, at field::kWater) and its records
// (36 bytes each, indexed like the vertices).
namespace water {
constexpr std::size_t kRecords = 0x18; // the record array, in the water object
constexpr uint32_t kSize = 0x24;
constexpr std::size_t kDepth = 0x0;     // float: above 0 is water (slots 16, 30 to 37, 53, 92)
constexpr std::size_t kSediment = 0x1C; // float: slot 55
} // namespace water

// Byte offsets of the slots our own code calls in the terrain's vtable.
namespace slot {
constexpr std::size_t kGetVertIndex = 0x24;       // slot 9
constexpr std::size_t kLocationIsInBounds = 0x2C; // slot 11
} // namespace slot

constexpr int kSlotCount = 98;

inline uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

template <typename T>
inline T& AtAddress(uint32_t address)
{
    return *reinterpret_cast<T*>(static_cast<std::uintptr_t>(address));
}

// The address of a vertex record (or of a water record with `base` the water
// array), computed as the game does: 32-bit, wrapping.
inline uint32_t RecordAddress(uint32_t base, uint32_t index) { return base + index * 0x24u; }

inline uint32_t VertexBase(void* self) { return At<uint32_t>(self, field::kVertices); }

inline uint32_t WaterBase(void* self) { return At<uint32_t>(At<void*>(self, field::kWater), water::kRecords); }

// The game's floor of a cell coordinate (inlined in slots 13, 26, 27, 30, 32,
// 55 and 77): t - 0.5 + 12582912.0 stored as a float (round to nearest even)
// with 0x4B400000 taken from its bits, then one more when t is exactly that
// number + 1. Unsigned in what follows; nothing tests the range here.
inline uint32_t FloorCell(double t)
{
    const float stored = static_cast<float>((t - 0.5) + 12582912.0);
    uint32_t n = Bits(stored) + 0xB4C00000u;
    const int32_t next = static_cast<int32_t>(n + 1u);
    if (t == static_cast<double>(next))
        n = static_cast<uint32_t>(next);
    return n;
}

// 1 / 16 as the game keeps it (.data, read at each use).
inline float InverseCellWidth() { return Global<float>(addr::cSTETerrain_InverseCellWidth); }

} // namespace opensc4::terrain_sim
