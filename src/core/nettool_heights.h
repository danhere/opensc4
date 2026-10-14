// The network tool's height pass and its constraints (build 641; part t9):
// the layouts its code reads and the helpers its two files share
// (src/core/nettool_heights_1.cpp: the height pass, slots 77, 56, 57 and 73,
// whose top has the NAM, register, float and stack leftover analysis of the
// whole part; src/core/nettool_heights_2.cpp: the per-cell constraints next
// to the NAM-guarded 0x00636690, three of them called by NAM) and their test
// (src/core/nettool_heights_autotest.cpp). Each helper is a call of a game
// function at its address or the game's inlined code; all of it works on
// the game's memory in place.
#pragma once

#include "core/nettool_satisfier.h"
#include "core/nettool_sim.h"
#include "core/nettool_solve.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::heights {

// ====================================================================================
// Layouts
// ====================================================================================

// The tool's fields this part reads beyond nettool_sim.h's.
namespace hfield {
inline constexpr std::size_t kWord74 = 0x74;                       // inf: the regular step count
inline constexpr std::size_t kDirection7C = 0x7C, kDirection80 = 0x80; // the drag's directions (0..7)
inline constexpr std::size_t kSetA4 = 0xA4, kSetB8 = 0xB8;         // the cells to fit, the border vertices
inline constexpr std::size_t kRecordsEnd = 0xE8, kBridgesEnd = 0xDC, kIntersectionsEnd = 0xD0;
inline constexpr std::size_t kVerticesZ = 0x24C;
inline constexpr std::size_t kVertexStepsA = 0x250, kVertexStepsB = 0x270; // by quarter turn
inline constexpr std::size_t kMargin = 0x2B0;                      // the placement parameters' +0x20
} // namespace hfield
// A type row's floats and words this part reads.
namespace hrow {
inline constexpr std::size_t kAbove = 0x5C, kBelow = 0x60, kPieceDrop = 0x8C;
inline constexpr std::size_t kSlopeStraight = 0x100, kSlopeDiagonal = 0x104;
} // namespace hrow
// A +0xE4 record (0x10 bytes) and a portion (0x10 bytes) of the height
// pass's vector: the first and the last step and their heights.
namespace portion {
inline constexpr std::size_t kSize = 0x10, kFirst = 0x0, kLast = 0x4, kFirstHeight = 0x8, kLastHeight = 0xC;
} // namespace portion
namespace e4 {
inline constexpr std::size_t kSize = 0x10, kFirstHeight = 0x0, kLastHeight = 0x4, kFirst = 0x8, kLast = 0xC;
} // namespace e4
// A major bridge (0x28 bytes): +0x4 the first step, +0x8 the last, +0xC the
// index of its record in the type row's bridges, +0x10 the altitude, +0x1C
// its cells (0x50 bytes; the four corner heights at +0x14).
namespace mbridge {
inline constexpr std::size_t kSize = 0x28, kFirst = 0x4, kLast = 0x8, kRecord = 0xC, kAltitude = 0x10;
inline constexpr std::size_t kCells = 0x1C, kCellSize = 0x50, kCellHeights = 0x14;
inline constexpr std::size_t kRecordFloats = 0x60; // the bridge record's pointer to its floats
} // namespace mbridge
// An intersection (0x28 bytes): the rule ID, x, z.
namespace isect {
inline constexpr std::size_t kSize = 0x28, kRule = 0x0, kX = 0x4, kZ = 0x8;
} // namespace isect

inline constexpr uint32_t kLotOffset = 100000; // a network lot's own vertices
inline constexpr uint32_t kOneBits = 0x3F800000;

// ====================================================================================
// Helpers
// ====================================================================================

inline char* Entry(char* self, uint32_t key)
{
    return CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
}
inline uint32_t PointKey(const char* point)
{
    return (At<uint32_t>(const_cast<char*>(point), 4) << 16) + At<uint32_t>(const_cast<char*>(point), 0);
}
inline char* Cells(char* self) { return At<Vector>(self, field::kCells).begin; }
inline char* Steps(char* self) { return At<Vector>(self, field::kSteps).begin; }
inline char* Sat1(char* self) { return self + field::kSatisfier1; }
inline char* Sat2(char* self) { return self + field::kSatisfier2; }
inline void* Terrain(char* self) { return At<void*>(self, field::kTerrain); }

// A cell info's field at a computed address (the game writes through a
// null info's offsets when the cell entry fails; so do ours).
template <typename T>
inline T& Field(char* info, std::size_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(info) + offset);
}

// The satisfier's functions at their addresses (floats as their bits).
inline void AddVertex(char* sat, uint32_t v, uint32_t h)
{
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddVertex, sat, v, h);
}
inline void AddFixed(char* sat, uint32_t v, uint32_t h)
{
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddFixedVertex, sat, v, h);
}
inline void Restrict(char* sat, uint32_t v, uint32_t lo, uint32_t hi)
{
    CallThis<void, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_RestrictVertex, sat, v, lo, hi);
}
inline void PreferRange(char* sat, uint32_t v, uint32_t lo, uint32_t hi)
{
    CallThis<void, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_SetPreferredRange, sat, v, lo, hi);
}
inline void PreferHeight(char* sat, uint32_t v, uint32_t h)
{
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_SetPreferredHeight, sat, v, h);
}
inline void Equality(char* sat, uint32_t a, uint32_t b)
{
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_InsertEqualityConstraint, sat, a, b);
}
inline void Slope(char* sat, uint32_t a, uint32_t b, uint32_t s)
{
    CallThis<void, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_InsertSlopeConstraint, sat, a, b, s);
}
inline void Curvature(char* sat, uint32_t a, uint32_t b, uint32_t c, uint32_t k)
{
    CallThis<void, uint32_t, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_InsertCurvatureConstraint,
                                                           sat, a, b, c, k);
}
inline uint8_t GetResult(char* sat, uint32_t v, uint32_t* out)
{
    return CallThis<uint8_t, uint32_t, uint32_t*>(addr::SC4VertexHeightSatisfier_GetResult, sat, v, out);
}

inline double TerrainHeight(char* self, uint32_t vertex)
{
    return Virtual<double>(Terrain(self), terrain_slot::kVertexAltitude, vertex);
}
inline uint32_t VertexIndex(char* self, uint32_t x, uint32_t z)
{
    return Virtual<uint32_t>(Terrain(self), 0x24, x, z);
}

// The game's inline "first node" of a hash set: the first bucket in index
// order that has one.
inline char* FirstNode(char* set)
{
    const uint32_t count = solve::BucketCount(set);
    for (uint32_t index = 0; index < count; ++index)
        if (char* const head = reinterpret_cast<char**>(At<Vector>(set, rul::table::kBuckets).begin)[index])
            return head;
    return nullptr;
}

// A word of a static table indexed as the game does (32-bit wrapping).
inline int32_t TableWord(std::uintptr_t base, uint32_t byteOffset)
{
    return *reinterpret_cast<const int32_t*>(static_cast<uint32_t>(base + byteOffset));
}

// A 16-byte portion appended to a vector as the game's inlined push_back
// does: in place (nothing written at a null end), else new memory for size
// + max(size, 1) records, the old ones copied by UninitializedCopy (an
// unread tag), the record written (not at a null end), the old memory freed.
inline void PushPortion(Vector& v, const uint32_t* record)
{
    if (v.end != v.capacity) {
        if (v.end)
            for (std::size_t word = 0; word < 4; ++word)
                At<uint32_t>(v.end, word * 4) = record[word];
        v.end += portion::kSize;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
    const uint32_t grown = (size < 1 ? 1u : size) + size;
    char* const buffer = grown ? solve::Allocate(grown * static_cast<uint32_t>(portion::kSize)) : nullptr;
    uint32_t tag = 0;
    char* end = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4Vector16_UninitializedCopy, v.begin, v.end,
                                                                 buffer, &tag);
    if (end)
        for (std::size_t word = 0; word < 4; ++word)
            At<uint32_t>(end, word * 4) = record[word];
    end += portion::kSize;
    solve::FreeIfSet(v.begin);
    v.begin = buffer;
    v.end = end;
    v.capacity = buffer + grown * portion::kSize;
}

// Every cell of a step: the four heights and both words of the four first
// pairs set to `height` (per corner: the pair's high, its low, the height).
inline void SetStepHeights(char* self, uint32_t stepIndex, uint32_t height)
{
    for (uint32_t c = At<uint32_t>(Steps(self) + stepIndex * step::kSize, 0);
         c <= At<uint32_t>(Steps(self) + stepIndex * step::kSize, step::kLastCell); ++c) {
        char* const info = Entry(self, PointKey(Cells(self) + c * kPointSize));
        for (std::size_t k = 0; k < 4; ++k) {
            Field<uint32_t>(info, cell::kRanges + 8 * k + 4) = height;
            Field<uint32_t>(info, cell::kRanges + 8 * k) = height;
            Field<uint32_t>(info, cell::kHeights + 4 * k) = height;
        }
    }
}


} // namespace opensc4::nettool_sim::heights
