// The vertex height satisfier's part of the autotest (part s1;
// src/core/nettool_satisfier_1.cpp has the class map).
//
// OPENSC4_TEST_NETTOOL_SOLVER=<day>: on that day, satisfiers the test makes
// itself (operator new 0x005E55E0 of 0x64 bytes and the constructor
// 0x00660810 at the game's address; never the tools' own +0x124/+0x188) are
// given four scenarios, every call at the game's address (addr::...), so an
// A run runs the original and a B run whatever is installed:
//   solvable  Begin (twice: the second answers 0), a fixed vertex, seven free
//             ones, slope constraints (one repeated reversed with a smaller
//             slope, one of a vertex with itself, one with a vertex that has
//             no entry), equality constraints (two groups, then one that
//             merges them, one inside a group, one that adds a vertex to a
//             group), curvature constraints (one
//             repeated with a smaller k, one with a repeated vertex),
//             preferred height and range, OrVertexFlags, FindGroup,
//             IntersectVertexRange, Solve, GetResult of every vertex (and
//             one never added), the results walked by BeginResults and
//             NextResult, Reset, the destructor and operator delete;
//   ranges    vertices restricted to ranges (RestrictVertex lo < hi), a
//             slope, an equality and a curvature constraint between them and
//             two merged by hand (MergeSum, MergeMidpoint): PropagateAll
//             alone (the entries then hashed), then Solve and the results;
//             the object deleted through its vtable's slot 0 (the deleting
//             destructor) with flag 1;
//   impossible  two fixed vertices 100 apart with a slope of 1 between them
//             and equalities to a third and a fourth: Solve fails (PropagateAll's
//             Propagate), +0x10 (the vertex it failed on), DumpGroups (on
//             the emptied groups: Solve clears before it dumps);
//   ranges math  RangeIntersect and RangeIntersectChanged on the test's own
//             pairs: a narrowing, a collapse within the gap, an empty one.
// Every vertex has a finite range (Begin's start range is finite): the
// original's NextEntry reads through NULL when no range is below FLT_MAX
// (the puzzle-piece crash). Every vertex of an equality, slope or curvature
// constraint is added before the constraint (the original dereferences
// entry + 8 of a missing vertex when it fixes a group). The vertex IDs are
// z * (cells x + 1) + x within the city's size, read from the city.
// Only values are hashed, never addresses: the answers (AL), the results'
// (vertex, height bits), the entries' (vertex, lo, hi, flags) after
// PropagateAll (order-free), +0x05, +0x10 after the failure, the counts of
// the tables. One log line `autotest: ntsolve: <scenario> ...` per scenario;
// the daily part ` | ntsolve set <hash>` once the drive ran.
#include "core/autotest_hooks.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/nettool_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

using nettool_sim::CallCdecl;
using nettool_sim::CallThis;

constexpr uint32_t kSize = 0x64;
constexpr std::size_t kAnswer = 0x05, kFailed = 0x10, kResultCount = 0x24, kEntries = 0x28, kEntryCount = 0x38;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

// --- The satisfier's functions at the game's addresses ------------------------

char* Make()
{
    void* const memory = CallCdecl<void*, uint32_t>(addr::SC4_operator_new, kSize);
    return memory ? CallThis<char*>(addr::SC4VertexHeightSatisfier_Construct, memory) : nullptr;
}

void Destroy(char* self)
{
    CallThis<void>(addr::SC4VertexHeightSatisfier_Destruct, self);
    CallCdecl<void, void*>(addr::SC4_operator_delete, self);
}

uint8_t Begin(char* self, float lo, float hi)
{
    return CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_Begin, self, Bits(lo), Bits(hi));
}
uint8_t Reset(char* self) { return CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_Reset, self); }
void AddVertex(char* self, uint32_t v, float wanted)
{
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddVertex, self, v, Bits(wanted));
}
void AddFixed(char* self, uint32_t v, float height)
{
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddFixedVertex, self, v, Bits(height));
}
void Restrict(char* self, uint32_t v, float lo, float hi)
{
    CallThis<void, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_RestrictVertex, self, v, Bits(lo),
                                                 Bits(hi));
}
void Slope(char* self, uint32_t a, uint32_t b, float s)
{
    CallThis<void, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_InsertSlopeConstraint, self, a, b,
                                                 Bits(s));
}
void Equal(char* self, uint32_t a, uint32_t b)
{
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_InsertEqualityConstraint, self, a, b);
}
void Curvature(char* self, uint32_t a, uint32_t b, uint32_t c, float k)
{
    CallThis<void, uint32_t, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_InsertCurvatureConstraint,
                                                           self, a, b, c, Bits(k));
}
uint8_t Solve(char* self) { return CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_Solve, self); }

// GetResult of each vertex, then the results walked (BeginResults,
// NextResult); returns how many the walk gave.
uint32_t MixResults(Hash& hash, char* self, const uint32_t* vertices, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        uint32_t height = 0;
        const uint8_t found = CallThis<uint8_t, uint32_t, uint32_t*>(addr::SC4VertexHeightSatisfier_GetResult, self,
                                                                     vertices[i], &height);
        hash.Mix(found);
        if (found)
            hash.Mix(height);
    }
    CallThis<void>(addr::SC4VertexHeightSatisfier_BeginResults, self);
    uint32_t walked = 0;
    for (;;) {
        uint32_t vertex = 0, height = 0;
        if (!CallThis<uint8_t, uint32_t*, uint32_t*>(addr::SC4VertexHeightSatisfier_NextResult, self, &vertex, &height))
            break;
        hash.Mix(vertex);
        hash.Mix(height);
        if (++walked > 4096)
            break;
    }
    hash.Mix(walked);
    hash.Mix(At<uint32_t>(self, kResultCount));
    return walked;
}

// The entries (+0x28) walked bucket by bucket: per entry the key, lo, hi,
// the wanted height and the flags, mixed order-free (sum and xor).
uint32_t MixEntries(Hash& hash, char* self)
{
    char** const begin = At<char**>(self, kEntries + 4);
    char** const end = At<char**>(self, kEntries + 8);
    uint32_t sum = 0, xored = 0, count = 0;
    for (char** bucket = begin; bucket < end; ++bucket)
        for (char* node = *bucket; node && count < 4096; node = At<char*>(node, 0)) {
            Hash one;
            one.Mix(At<uint32_t>(node, 4));
            for (const std::size_t word : {0x8u, 0xCu, 0x1Cu, 0x20u})
                one.Mix(At<uint32_t>(node, word));
            sum += one.h;
            xored ^= one.h;
            ++count;
        }
    hash.Mix(count);
    hash.Mix(sum);
    hash.Mix(xored);
    return count;
}

// --- The scenarios ---------------------------------------------------------------

struct Grid {
    uint32_t stride;
    uint32_t At(uint32_t x, uint32_t z) const { return z * stride + x; }
};

void Solvable(Hash& total, const Grid& grid)
{
    char* const self = Make();
    if (!self) {
        log::Write("autotest: ntsolve: solvable: no object");
        return;
    }
    Hash hash;
    uint32_t v[9];
    for (uint32_t i = 0; i < 9; ++i)
        v[i] = grid.At(2 + i, 3);
    hash.Mix(Begin(self, 0.0f, 100.0f));
    hash.Mix(Begin(self, -1.0f, 1.0f));
    AddFixed(self, v[0], 10.0f);
    const float wanted[] = {50.0f, 12.0f, 30.0f, 40.0f, 20.0f, 25.0f, 60.0f};
    for (uint32_t i = 1; i < 8; ++i)
        AddVertex(self, v[i], wanted[i - 1]);
    AddVertex(self, v[1], 99.0f); // already there: nothing
    Slope(self, v[0], v[1], 2.0f);
    Slope(self, v[1], v[2], 2.0f);
    Slope(self, v[2], v[1], 1.5f);
    Slope(self, v[3], v[3], 1.0f);
    Slope(self, v[0], v[8], 1.0f); // v[8] has no entry
    Equal(self, v[4], v[5]);
    Equal(self, v[6], v[7]);
    Equal(self, v[5], v[6]);
    Equal(self, v[4], v[7]);
    Equal(self, v[3], v[4]); // v[3] in no group yet: it joins v[4]'s
    Curvature(self, v[1], v[2], v[3], 1.0f);
    Curvature(self, v[3], v[2], v[1], 0.5f);
    Curvature(self, v[1], v[1], v[2], 1.0f);
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_SetPreferredHeight, self, v[3], Bits(15.0f));
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_SetPreferredHeight, self, v[8], Bits(15.0f));
    CallThis<void, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_SetPreferredRange, self, v[7],
                                                 Bits(22.0f), Bits(24.0f));
    hash.Mix(CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, v[2], 0u));
    hash.Mix(CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, v[8], 0u));
    hash.Mix(CallThis<char*, uint32_t>(addr::SC4VertexHeightSatisfier_FindGroup, self, v[4]) ? 1u : 0u);
    hash.Mix(CallThis<char*, uint32_t>(addr::SC4VertexHeightSatisfier_FindGroup, self, v[0]) ? 1u : 0u);
    {
        float pair[2] = {0.0f, 100.0f};
        uint8_t changed = 0;
        hash.Mix(CallThis<uint8_t, uint32_t, float*, uint8_t*>(addr::SC4VertexHeightSatisfier_IntersectVertexRange,
                                                               self, v[2], pair, &changed));
        hash.Mix(changed);
    }
    hash.Mix(At<uint32_t>(self, kEntryCount));
    const uint8_t solved = Solve(self);
    hash.Mix(solved);
    hash.Mix(At<uint8_t>(self, kAnswer));
    hash.Mix(At<uint32_t>(self, kEntryCount));
    const uint32_t results = MixResults(hash, self, v, 9);
    hash.Mix(Reset(self));
    hash.Mix(Reset(self));
    hash.Mix(At<uint32_t>(self, kResultCount));
    Destroy(self);
    log::Write("autotest: ntsolve: solvable: solved %u, %u results, hash %08X", solved, results, hash.h);
    total.Mix(hash.h);
}

void Ranges(Hash& total, const Grid& grid)
{
    char* const self = Make();
    if (!self) {
        log::Write("autotest: ntsolve: ranges: no object");
        return;
    }
    Hash hash;
    uint32_t v[7];
    for (uint32_t i = 0; i < 7; ++i)
        v[i] = grid.At(2 + i, 5);
    hash.Mix(Begin(self, -50.0f, 50.0f));
    for (uint32_t i = 0; i < 6; ++i)
        AddVertex(self, v[i], 0.0f);
    Restrict(self, v[0], 10.0f, 20.0f);
    Restrict(self, v[1], -5.0f, 5.0f);
    Restrict(self, v[2], 0.0f, 30.0f);
    Restrict(self, v[3], 4.0f, 40.0f);
    Restrict(self, v[5], 1.0f, 2.0f);
    Restrict(self, v[6], 1.0f, 2.0f); // v[6] has no entry: nothing
    Slope(self, v[0], v[1], 8.0f);
    Equal(self, v[2], v[3]);
    Curvature(self, v[3], v[4], v[5], 1.0f);
    {
        const uint32_t sum[4] = {v[4], v[5], Bits(-100.0f), Bits(100.0f)};
        hash.Mix(CallThis<uint8_t, const uint32_t*>(addr::SC4VertexHeightSatisfier_MergeSum, self, sum));
        hash.Mix(CallThis<uint8_t, const uint32_t*>(addr::SC4VertexHeightSatisfier_MergeSum, self, sum));
        const uint32_t midpoint[4] = {v[3], v[4], Bits(-200.0f), Bits(200.0f)};
        hash.Mix(CallThis<uint8_t, const uint32_t*>(addr::SC4VertexHeightSatisfier_MergeMidpoint, self, midpoint));
    }
    const uint8_t propagated = CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_PropagateAll, self);
    hash.Mix(propagated);
    const uint32_t entries = MixEntries(hash, self);
    const uint8_t solved = Solve(self);
    hash.Mix(solved);
    hash.Mix(At<uint8_t>(self, kAnswer));
    hash.Mix(At<uint32_t>(self, kEntryCount));
    const uint32_t results = MixResults(hash, self, v, 7);
    // The deleting destructor through the vtable's slot 0, flag 1.
    Virtual<char*>(self, 0, 1u);
    log::Write("autotest: ntsolve: ranges: propagated %u, %u entries, solved %u, %u results, hash %08X", propagated,
               entries, solved, results, hash.h);
    total.Mix(hash.h);
}

void Impossible(Hash& total, const Grid& grid)
{
    char* const self = Make();
    if (!self) {
        log::Write("autotest: ntsolve: impossible: no object");
        return;
    }
    Hash hash;
    const uint32_t a = grid.At(2, 7), b = grid.At(3, 7), c = grid.At(4, 7), d = grid.At(5, 7);
    hash.Mix(Begin(self, 0.0f, 100.0f));
    AddFixed(self, a, 0.0f);
    AddFixed(self, b, 100.0f);
    AddVertex(self, c, 50.0f);
    AddVertex(self, d, 50.0f);
    Slope(self, a, b, 1.0f);
    Equal(self, b, c);
    Equal(self, c, d); // d in no group yet: it joins c's
    const uint8_t solved = Solve(self);
    hash.Mix(solved);
    hash.Mix(At<uint8_t>(self, kAnswer));
    uint32_t failed = 0xFFFFFFFFu;
    if (!solved) {
        // Written by the failing Propagate (the constructor leaves +0x10 unset).
        failed = At<uint32_t>(self, kFailed);
        hash.Mix(failed == a ? 1u : failed == b ? 2u : failed == c ? 3u : 0u);
    }
    hash.Mix(At<uint32_t>(self, kEntryCount));
    const uint32_t vertices[3] = {a, b, c};
    const uint32_t results = MixResults(hash, self, vertices, 3);
    hash.Mix(Reset(self));
    Destroy(self);
    log::Write("autotest: ntsolve: impossible: solved %u, failed on %s, %u results, hash %08X", solved,
               failed == a ? "a" : failed == b ? "b" : failed == c ? "c" : "-", results, hash.h);
    total.Mix(hash.h);
}

void RangeMath(Hash& total)
{
    Hash hash;
    struct Case {
        float range[2];
        float pair[2];
    };
    const Case cases[] = {
        {{0.0f, 10.0f}, {2.0f, 8.0f}},   // narrowed
        {{0.0f, 1.0f}, {1.005f, 2.0f}},  // inverted within the gap: hi = lo
        {{0.0f, 1.0f}, {5.0f, 6.0f}},    // empty
        {{3.0f, 4.0f}, {-1.0f, 9.0f}},   // unchanged
    };
    uint32_t answers = 0;
    for (const Case& test : cases) {
        float range[2] = {test.range[0], test.range[1]};
        float pair[2] = {test.pair[0], test.pair[1]};
        const uint8_t plain = CallThis<uint8_t, float*>(addr::SC4VertexHeightSatisfier_RangeIntersect,
                                                        reinterpret_cast<char*>(range), pair);
        hash.Mix(plain);
        hash.Mix(Bits(range[0]));
        hash.Mix(Bits(range[1]));
        float again[2] = {test.range[0], test.range[1]};
        uint8_t changed = 0xAA;
        const uint8_t marked = CallThis<uint8_t, float*, uint8_t*>(addr::SC4VertexHeightSatisfier_RangeIntersectChanged,
                                                                   reinterpret_cast<char*>(again), pair, &changed);
        hash.Mix(marked);
        hash.Mix(changed);
        hash.Mix(Bits(again[0]));
        hash.Mix(Bits(again[1]));
        answers += plain + marked;
    }
    log::Write("autotest: ntsolve: ranges math: %u true answers, hash %08X", answers, hash.h);
    total.Mix(hash.h);
}

bool g_done = false;
uint32_t g_sum = 0;

void Drive(const Day& day)
{
    if (g_done || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL_SOLVER"))
        return;
    g_done = true;
    const uint32_t cellsX = day.city->CellCountX(), cellsZ = day.city->CellCountZ();
    if (cellsX < 12 || cellsZ < 8) {
        log::Write("autotest: ntsolve: the city is %u x %u cells; not driven", cellsX, cellsZ);
        return;
    }
    const Grid grid{cellsX + 1};
    Hash total;
    Solvable(total, grid);
    Ranges(total, grid);
    Impossible(total, grid);
    RangeMath(total);
    g_sum = total.h;
    log::Write("autotest: ntsolve: drive: hash %08X", g_sum);
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (!g_done)
        return;
    char text[48];
    std::snprintf(text, sizeof text, " | ntsolve set %08X", g_sum);
    line += text;
}

const FingerprintPart kFingerprint{"ntsolve", 176, &Fingerprint};
const DayHook kDrive{"ntsolve.drive", 198, &Drive};

} // namespace
} // namespace opensc4::autotest
