// Our cSTETerrain edge reconciling (build 641), piece 7 of the city's
// terrain object (vtable 0x00AB3D18, the service 0x00B43CF4): the four
// workers slot 89 (ReconcileEdgesWithNeighboringCities, 0x0074ED40, ours in
// terrain_messages.cpp) calls, and everything only they call. Group
// simulators.terrain.edges.
//
//   0x0074D010  FindEdgeDifferences  what the neighbours' edges ask of ours
//   0x007420F0  CityVertexRect       a regional city's place in vertices
//   0x0074BE80  ReadNeighbourEdge    one neighbour: its file, its strip
//   0x0074BDE0  ReadEdgeStrip        the strip record of a city file
//   0x007438C0  SharedEdge           where two cities' rectangles touch
//   0x0074AF00  AddStrip             the strip's altitudes into the set
//   0x0074B000  AddEmptyEdge         a place without a city: the base height
//   0x0074B0D0  CollectEdgeChanges   the cells and vertices around them
//   0x0074B2D0  PrepareEdgeChanges   the cells shown to the player
//   0x00748C90  ApplyEdgeChanges     the altitudes set, the land smoothed
//
// The procedure (all from the disassembly, statement by statement).
//
// 1. FindEdgeDifferences(set, withEmpty). The region (the application's
//    slot 35) gives its bounds (slot 24) and its base terrain height (slot
//    20); the regional city of this city (the application's slot 36) its
//    position and size in region units, 64 cells each. For each of the four
//    edges (0 west, 1 north, 2 east, 3 south) the units just outside it are
//    walked: where the region has a city there (slot 11) that was not seen
//    yet in this call, that city's file is opened for reading
//    (0x005DBFE0 with its save path, write 0, check version 1) and the edge
//    strip it saved for the side facing us is read: record 0xA9DD6FF4 /
//    0xE98F9525 / 0x64 + ((edge + 2) mod 4), version 1, a count and that
//    many floats (the four records every city writes, terrain_save.cpp).
//    Where the two cities' rectangles touch, the strip's altitudes go into
//    the set under our vertex's index (the greater one where two neighbours
//    name the same vertex). Where there is no city and `withEmpty` is set,
//    the 65 vertices along that unit get the region's base height x 1.5.
//    Then the set is cut down: a vertex whose altitude is already exactly
//    the wanted one is taken out; and when EVERY vertex of the set is within
//    1.2 (0x00B0D680) of its wanted altitude, the whole set is emptied
//    (nothing to reconcile). Nothing but the caller's set is written; the
//    neighbours' files are only read.
// 2. CollectEdgeChanges(set, cells, vertices): for each vertex of the set
//    the difference between the wanted and the present altitude gives a
//    reach in cells through the curve at 0x00B4BD64 (rounded to the nearest
//    whole number); the vertices of the square of that reach around it (cut
//    to the grid) go into the hash set `vertices`, and the cells of that
//    square widened by one towards the origin into the hash set `cells`.
//    Writes the two hash sets only.
// 3. PrepareEdgeChanges(cells): gives the view (its +0xC interface, slot 8)
//    the cells as a vector of (x, z) pairs, with 0 and 1: the cells the
//    second question shows. Nothing without a view. The view's and so
//    drawing: a call.
// 4. ApplyEdgeChanges(set, vertices): every vertex of the set is set to
//    its wanted altitude (slot 50); every vertex of `vertices` that is not
//    in the set and not on the map's border gets the mark 1; the rectangle
//    around `vertices` is smoothed (0x00745020, 0x00B0D67C = 5 passes, mark
//    1), the marks cleared (0x007409B0) and the rectangle redisplayed (slot
//    68 with 1, 1, the rectangle, 1). Writes this city's terrain only.
// None of the four opens a dialog (the three dialogs are slot 89's) and
// none writes a file.
//
// How the group reaches every caller: the four workers are called directly
// by the game's slot 89 (0x0074EDC6, 0x0074EE8C, 0x0074EE9E, 0x0074EF03) and
// at their addresses by ours (terrain_messages.cpp), so each gets a detour at
// its entry. Every one of those sites is a plain thiscall with the arguments
// on the stack and uses only EBX, EDI and the stack afterwards: no thunks.
// The six helpers are called only from inside these functions (scan of every
// direct call in the exe) and are plain functions here.
//
// The game's and called at their addresses: the region's, the regional
// city's and the view's slots; the city file and record streams (0x005DBFE0,
// 0x005DBC10, 0x005D9E70, 0x005D9670, 0x005D9C30); the containers (the
// pointer set 0x0049A050 / 0x004A4660, the map 0x006B5840, its range erase
// 0x00747830 and subtree free 0x007439D0, the vectors 0x0051CA60,
// 0x0048C290, 0x00464130, 0x007C61A0, the hash sets 0x004F0940, 0x00624DD0,
// 0x004B0390); the curve 0x005F4250; the terrain's own slots 50 and 68 and
// its smoothing 0x00745020 and flag walker 0x007409B0 (ours with
// simulators.terrain.brush).
//
// Floating point: the base height is the integer x 1.5f on the x87, rounded
// to float once; the differences are float - float on the stack; the reach
// is the curve's ST0 rounded to float and then to the nearest whole number
// (FISTP). No transcendental instruction, no random draws.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <xmmintrin.h>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace slot {
constexpr std::size_t kSetAltitudeAtIndex = 0xC8; // slot 50 (index, altitude)
constexpr std::size_t kRedisplayRect = 0x110;     // slot 68 (bool, bool, rect, uint32_t)
} // namespace slot

// The application (0x00B43C94), the region and a regional city as the edge
// code calls them (byte offsets of the slots; argument counts from the
// callers' stacks).
namespace app_slot {
constexpr std::size_t kGetRegion = 0x8C;       // slot 35
constexpr std::size_t kGetRegionalCity = 0x90; // slot 36
} // namespace app_slot

namespace region_slot {
constexpr std::size_t kGetCity = 0x2C;              // slot 11 (x, z) -> a reference holder, or null
constexpr std::size_t kGetBaseTerrainHeight = 0x50; // slot 20 -> int32
constexpr std::size_t kGetBounds = 0x60;            // slot 24 (int32 rect[4]: least x, least z, greatest x, greatest z)
} // namespace region_slot

namespace city_slot {
constexpr std::size_t kGetPosition = 0x14;         // slot 5 (int32* x, int32* z), region units
constexpr std::size_t kGetCitySize = 0x1C;         // slot 7 (int32* x, int32* z), region units
constexpr std::size_t kGetCitySaveFilePath = 0x7C; // slot 31 (string object)
} // namespace city_slot

// The view's +0xC interface.
namespace view_slot {
constexpr std::size_t kShowCells = 0x20; // slot 8 (vector of (x, z) cells, 0, 1)
} // namespace view_slot

// The edge strip records (terrain_save.cpp writes them).
namespace edge_record {
constexpr uint32_t kType = 0xA9DD6FF4;
constexpr uint32_t kGroup = 0xE98F9525;
constexpr uint32_t kFirstInstance = 0x64;
constexpr uint16_t kVersion = 1;
} // namespace edge_record

// A region unit in cells (the shifts by 6 at 0x0074210B to 0x00742137,
// 0x0074B014 and 0x0074B03E) and its vertices (0x0074B057).
constexpr uint32_t kUnitShift = 6;
constexpr uint32_t kUnitVertices = 0x41;
// The strip vector's first size (0x0074BEB1): 257 zeros, resized to the
// record's count as soon as that is read.
constexpr uint32_t kStripStart = 0x101;

namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("terrain: first %s", name);
}

template <typename R, typename... Args>
inline R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

// A red-black tree of the game's (STLport): the head's parent is the root,
// its left the first node, its right the last; a node's key at +0x10 and,
// in a map, its value at +0x14.
namespace node {
constexpr std::size_t kParent = 0x4, kLeft = 0x8, kRight = 0xC, kKey = 0x10, kValue = 0x14;
} // namespace node

struct Tree {
    char* head;
    uint32_t count;
};

inline char*& Link(char* at, std::size_t which) { return At<char*>(at, which); }

// The node after `at` in key order (inlined at 0x0074D352, 0x0074B277 and
// 0x00748CE2).
inline char* TreeNext(char* at)
{
    if (char* down = Link(at, node::kRight)) {
        while (char* left = Link(down, node::kLeft))
            down = left;
        return down;
    }
    char* up = Link(at, node::kParent);
    while (at == Link(up, node::kRight)) {
        at = up;
        up = Link(up, node::kParent);
    }
    if (Link(at, node::kRight) != up)
        at = up;
    return at;
}

// The first node of the edge set whose key is not below `key` (signed), or
// the head.
inline char* LowerBound(const Tree* tree, int32_t key)
{
    char* found = tree->head;
    for (char* at = Link(tree->head, node::kParent); at;) {
        if (At<int32_t>(at, node::kKey) < key) {
            at = Link(at, node::kRight);
        } else {
            found = at;
            at = Link(at, node::kLeft);
        }
    }
    return found;
}

// The node of the edge set with this key, or null (the test that follows
// every search: 0x0074AFA3, 0x0074B083, 0x00748DC6).
inline char* Find(const Tree* tree, int32_t key)
{
    char* const found = LowerBound(tree, key);
    if (found == tree->head || key < At<int32_t>(found, node::kKey))
        return nullptr;
    return found;
}

// A std::vector of the game (begin, end, capacity).
struct GameVector {
    uint32_t start, finish, capacity;
};

// The game's string object on the stack (vtable 0x00A80810).
struct GameString {
    std::uintptr_t vtable;
    char* start;
    char* finish;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A record stream as the game keeps it on its stack (0x10 bytes): the
// stream at +8 and, for a read record, its version at +0xC.
struct RecordStream {
    uint32_t words[2];
    void* stream;
    uint16_t version;
    uint16_t unused;
};
static_assert(sizeof(RecordStream) == 0x10);

constexpr std::size_t kStreamUint32 = 0x24, kStreamFloat32 = 0x30;

// A float that went through FLD and FSTP on its way (0x0074AF79): a
// signalling NaN comes out quiet; anything else unchanged.
inline uint32_t Reloaded(uint32_t bits)
{
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0)
        bits |= 0x00400000u;
    return bits;
}

inline float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// FISTP of a float: the nearest whole number (ties to even) under the
// game's control word, 0x80000000 for what does not fit and for a NaN; the
// same as CVTSS2SI under the default MXCSR.
inline int32_t RoundToInt(float value) { return _mm_cvtss_si32(_mm_set_ss(value)); }

// The value of the set's entry for `key` becomes `bits` when there is no
// entry yet (through the map's operator[], 0x006B5840, which inserts) or
// when the entry's value is below it (not for a NaN on either side).
// Inlined in 0x0074AF00 (0x0074AF7C) and 0x0074B000 (0x0074B060).
inline void KeepGreater(Tree* found, int32_t key, uint32_t bits)
{
    if (char* const at = Find(found, key)) {
        if (At<float>(at, node::kValue) < AsFloat(bits))
            At<uint32_t>(at, node::kValue) = bits;
        return;
    }
    *This<uint32_t*>(addr::SC4Sint32FloatMap_Index, found, &key) = bits;
}

// --- Reading the neighbours ----------------------------------------------------

// 0x007420F0 (cdecl: city, rect): a regional city's rectangle in vertex
// units of the region: its position and its position + size, each times 64.
void CityVertexRect(void* city, int32_t* rect)
{
    Virtual<void>(city, city_slot::kGetPosition, &rect[0], &rect[1]);
    rect[0] = static_cast<int32_t>(static_cast<uint32_t>(rect[0]) << kUnitShift);
    rect[1] = static_cast<int32_t>(static_cast<uint32_t>(rect[1]) << kUnitShift);
    // The game's two size words are its argument slots: whatever the getter
    // leaves alone is the city pointer and the rectangle's address.
    int32_t sizeX = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(rect));
    int32_t sizeZ = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(city));
    Virtual<void>(city, city_slot::kGetCitySize, &sizeX, &sizeZ);
    rect[2] = static_cast<int32_t>(static_cast<uint32_t>(rect[0]) + (static_cast<uint32_t>(sizeX) << kUnitShift));
    rect[3] = static_cast<int32_t>(static_cast<uint32_t>(rect[1]) + (static_cast<uint32_t>(sizeZ) << kUnitShift));
}

// 0x0074BDE0 (edge, segment, strip): reads the edge strip record `edge` of
// a city file into the vector: version 1 only; the vector resized to the
// record's count (new elements 0.0), then that many floats. 1 when read.
uint8_t ReadEdgeStrip(uint32_t edge, void* segment, GameVector* strip)
{
    RecordStream record;
    This<void*>(addr::SC4SegmentIStream_SC4SegmentIStream, &record, segment, edge_record::kType, edge_record::kGroup,
                edge + edge_record::kFirstInstance);
    uint8_t read = 0;
    if (This<uint8_t>(addr::SC4SegmentStream_IsOpen, &record) && record.version == edge_record::kVersion) {
        // The count's place is the game's first argument slot: the edge
        // number when the stream writes nothing.
        uint32_t count = edge;
        Virtual<uint8_t>(record.stream, kStreamUint32, &count);
        float fill = 0.0f;
        This<void>(addr::SC4FloatVector_Resize2, strip, count, &fill);
        for (uint32_t i = 0; i < count; ++i)
            Virtual<uint8_t>(record.stream, kStreamFloat32, reinterpret_cast<void*>(strip->start + 4u * i));
        read = 1;
    }
    This<void>(addr::SC4SegmentIStream_Close, &record);
    return read;
}

// 0x007438C0 (cdecl: mine, theirs, first, last): where the rectangle
// `theirs` lies against `mine`: on our west (their far x is our near x) or
// east (their near x is our far x) the shared part runs along z from the
// greater near z to the lesser far z; on our north (their far z is our near
// z) or south along x likewise. `first` and `last` are its two ends (x, z);
// 0 when the rectangles do not touch that way. The tests are made in this
// order, so a city touching at a corner only counts as the first that fits.
uint8_t SharedEdge(const int32_t* mine, const int32_t* theirs, int32_t* first, int32_t* last)
{
    int32_t x;
    if (theirs[2] == mine[0]) {
        x = mine[0];
    } else if (theirs[0] == mine[2]) {
        x = mine[2];
    } else {
        int32_t z;
        if (theirs[3] == mine[1])
            z = mine[1];
        else if (theirs[1] == mine[3])
            z = mine[3];
        else
            return 0;
        last[1] = z;
        first[1] = z;
        first[0] = theirs[0] < mine[0] ? mine[0] : theirs[0];
        last[0] = mine[2] < theirs[2] ? mine[2] : theirs[2];
        return 1;
    }
    last[0] = x;
    first[0] = x;
    first[1] = theirs[1] < mine[1] ? mine[1] : theirs[1];
    last[1] = mine[3] < theirs[3] ? mine[3] : theirs[3];
    return 1;
}

// 0x0074AF00 (mine, theirs, first, last, strip, set): the neighbour's strip
// along the shared part into the set. The key is our vertex index counted
// with the REGIONAL city's width (far x - near x + 1 vertices), from
// `first`; along x it goes on by 1, along z by that width; the strip is
// read from the place of `first` in the neighbour's edge. Nothing tests
// the strip's length.
void AddStrip(const int32_t* mine, const int32_t* theirs, const int32_t* first, const int32_t* last,
              const GameVector* strip, Tree* found)
{
    const uint32_t width = static_cast<uint32_t>(mine[2]) - static_cast<uint32_t>(mine[0]) + 1u;
    uint32_t key = (static_cast<uint32_t>(first[1]) - static_cast<uint32_t>(mine[1])) * width -
                   static_cast<uint32_t>(mine[0]) + static_cast<uint32_t>(first[0]);
    int32_t count;
    uint32_t step, from;
    if (first[1] == last[1]) {
        count = static_cast<int32_t>(static_cast<uint32_t>(last[0]) - static_cast<uint32_t>(first[0]) + 1u);
        from = static_cast<uint32_t>(first[0]) - static_cast<uint32_t>(theirs[0]);
        step = 1;
    } else {
        step = width;
        count = static_cast<int32_t>(static_cast<uint32_t>(last[1]) - static_cast<uint32_t>(first[1]) + 1u);
        from = static_cast<uint32_t>(first[1]) - static_cast<uint32_t>(theirs[1]);
    }
    if (count <= 0)
        return;
    uint32_t at = from * 4u;
    do {
        const uint32_t bits = Reloaded(AtAddress<uint32_t>(strip->start + at));
        KeepGreater(found, static_cast<int32_t>(key), bits);
        key += step;
        at += 4u;
    } while (--count != 0);
}

// 0x0074B000 (edge, city x, city z, unit x, unit z, altitude, set): the 65
// vertices of our edge along one region unit without a city get the given
// altitude in the set (the greater one where there is an entry). The keys
// are counted with the terrain's own row length: along the north (1) and
// south (3) edges from (unit x - city x) x 64, on the south edge + row
// length x cells z, one apart; along the west (0) and east (2) edges from
// (unit z - city z) x row length x 64, on the east edge + cells x, a row
// apart.
void AddEmptyEdge(char* self, uint32_t edge, int32_t cityX, int32_t cityZ, int32_t unitX, int32_t unitZ, uint32_t bits,
                  Tree* found)
{
    uint32_t key, step;
    if (edge & 1u) {
        key = (static_cast<uint32_t>(unitX) - static_cast<uint32_t>(cityX)) << kUnitShift;
        step = 1;
        if (edge == 3)
            key += U32(self, field::kVertsX) * U32(self, field::kCellsZ);
    } else {
        step = U32(self, field::kVertsX);
        key = ((static_cast<uint32_t>(unitZ) - static_cast<uint32_t>(cityZ)) * step) << kUnitShift;
        if (edge == 2)
            key += U32(self, field::kCellsX);
    }
    for (uint32_t left = kUnitVertices; left != 0; --left) {
        KeepGreater(found, static_cast<int32_t>(key), bits);
        key += step;
    }
}

// 0x0074BE80 (edge, mine, city, set): one neighbouring city: its save path
// (a string of the game's, 8 bytes at first), its file opened for reading
// with the version checked, the strip it saved for the side facing us
// ((edge + 2) mod 4), and where the two rectangles touch that strip into
// the set. Nothing when the file does not open, the strip is not there or
// the rectangles do not touch.
void ReadNeighbourEdge(uint32_t edge, const int32_t* mine, void* city, Tree* found)
{
    GameString path;
    path.start = path.finish = path.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, 8u);
    path.capacity = buffer + 8;
    path.start = buffer;
    path.finish = buffer;
    *buffer = 0;
    path.vtable = addr::SC4String_Vtable;
    path.zero = 0;
    GameVector strip;
    This<void*>(addr::SC4DwordVector_ConstructZeroed, &strip, kStripStart);
    int32_t theirs[4];
    CityVertexRect(city, theirs);
    Virtual<void>(city, city_slot::kGetCitySaveFilePath, static_cast<void*>(&path));
    void* const segment = Cdecl<void*>(addr::SC4CityFiles_OpenSegment, static_cast<void*>(&path), 0u, 1u);
    if (segment) {
        // (edge + 2) mod 4, signed as the game computes it.
        const uint32_t facing = static_cast<uint32_t>((static_cast<int32_t>(edge) + 2) % 4);
        if (ReadEdgeStrip(facing, segment, &strip)) {
            int32_t first[2], last[2];
            if (SharedEdge(mine, theirs, first, last))
                AddStrip(mine, theirs, first, last, &strip, found);
        }
        Cdecl<uint8_t>(addr::SC4CityFiles_CloseSegment, segment);
    }
    if (strip.start)
        Cdecl<void>(addr::SC4_Free, strip.start);
    path.vtable = addr::SC4String_Vtable;
    if (path.start)
        Cdecl<void>(addr::SC4_Free, path.start);
}

// --- FindEdgeDifferences -------------------------------------------------------

// push_back as inlined at 0x0074D29F and 0x0074D2DD (the value's own place
// serves as the allocator tag too).
inline void PushBack(GameVector& list, uint32_t value)
{
    if (list.finish != list.capacity) {
        if (list.finish)
            AtAddress<uint32_t>(list.finish) = value;
        list.finish += 4u;
        return;
    }
    This<void>(addr::SC4PtrVector_InsertN, &list, list.finish, &value, &value, 1u, 1u);
}

// The set of the cities seen in one call (pointers, nodes of 0x14 bytes),
// emptied and its head freed, as at 0x0074D483.
void DestroySeen(Tree& seen)
{
    if (seen.count) {
        char* at = Link(seen.head, node::kParent);
        while (at) {
            This<void>(addr::SC4PtrSet_EraseTree, &seen, Link(at, node::kRight));
            char* const left = Link(at, node::kLeft);
            Cdecl<void>(addr::SC4_Free, at);
            at = left;
        }
        Link(seen.head, node::kLeft) = seen.head;
        Link(seen.head, node::kParent) = nullptr;
        Link(seen.head, node::kRight) = seen.head;
        seen.count = 0;
    }
    if (seen.head)
        Cdecl<void>(addr::SC4_Free, seen.head);
}

// 0x0074D010 (set, withEmpty): see the top of the file. `found` is the
// caller's map of vertex index to wanted altitude (empty on entry at the
// only call site).
void __fastcall FindEdgeDifferences(char* self, void*, Tree* found, uint32_t withEmptyArgument)
{
    static bool logged = false;
    FirstCall("FindEdgeDifferences", logged);
    const uint8_t withEmpty = static_cast<uint8_t>(withEmptyArgument);
    Tree seen{nullptr, 0};
    seen.head = Cdecl<char*>(addr::SC4_stl_allocate, 0x14u);
    seen.count = 0;
    At<uint8_t>(seen.head, 0) = 0;
    Link(seen.head, node::kParent) = nullptr;
    Link(seen.head, node::kLeft) = seen.head;
    Link(seen.head, node::kRight) = seen.head;
    void* const region = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), app_slot::kGetRegion);
    if (!region) {
        DestroySeen(seen);
        return;
    }
    int32_t bounds[4];
    Virtual<void>(region, region_slot::kGetBounds, static_cast<void*>(bounds));
    void* const here = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), app_slot::kGetRegionalCity);
    int32_t mine[4];
    CityVertexRect(here, mine);
    // The game leaves these four as its stack had them when the getters
    // write nothing; zero here.
    int32_t cityX = 0, cityZ = 0, sizeX = 0, sizeZ = 0;
    Virtual<void>(here, city_slot::kGetPosition, &cityX, &cityZ);
    Virtual<void>(here, city_slot::kGetCitySize, &sizeX, &sizeZ);
    const int32_t base = Virtual<int32_t>(region, region_slot::kGetBaseTerrainHeight);
    const float baseHeight = static_cast<float>(static_cast<double>(Global<float>(addr::cSTETerrain_HeightUnit)) *
                                                static_cast<double>(base));

    for (uint32_t edge = 0; edge < 4; ++edge) {
        int32_t x, z, count;
        if (edge == 0 || edge == 2) {
            count = sizeZ;
            z = cityZ;
            x = edge == 0 ? static_cast<int32_t>(static_cast<uint32_t>(cityX) - 1u)
                          : static_cast<int32_t>(static_cast<uint32_t>(sizeX) + static_cast<uint32_t>(cityX));
        } else {
            count = sizeX;
            x = cityX;
            z = edge == 1 ? static_cast<int32_t>(static_cast<uint32_t>(cityZ) - 1u)
                          : static_cast<int32_t>(static_cast<uint32_t>(sizeZ) + static_cast<uint32_t>(cityZ));
        }
        if (x < bounds[0] || z < bounds[1] || x > bounds[2] || z > bounds[3] || count <= 0)
            continue;
        do {
            char** const holder = Virtual<char**>(region, region_slot::kGetCity, x, z);
            char* const other = holder ? *holder : nullptr;
            if (other) {
                // Seen already in this call? (pointers compared unsigned)
                const uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(other));
                char* at = seen.head;
                for (char* walk = Link(seen.head, node::kParent); walk;) {
                    if (At<uint32_t>(walk, node::kKey) < key) {
                        walk = Link(walk, node::kRight);
                    } else {
                        at = walk;
                        walk = Link(walk, node::kLeft);
                    }
                }
                if (at == seen.head || key < At<uint32_t>(at, node::kKey)) {
                    uint32_t inserted[2];
                    char* value = other;
                    This<void>(addr::SC4PtrSet_Insert, &seen, static_cast<void*>(inserted), &value);
                    ReadNeighbourEdge(edge, mine, other, found);
                }
            } else if (withEmpty) {
                AddEmptyEdge(self, edge, cityX, cityZ, x, z, Bits(baseHeight), found);
            }
            x = static_cast<int32_t>(static_cast<uint32_t>(x) + Global<uint32_t>(addr::cSTETerrain_EdgeStepX + 4u * edge));
            z = static_cast<int32_t>(static_cast<uint32_t>(z) + Global<uint32_t>(addr::cSTETerrain_EdgeStepZ + 4u * edge));
        } while (--count != 0);
    }

    // What is already there: `same` the vertices that have exactly the
    // wanted altitude, `close` those and the ones within the tolerance.
    char* const head = found->head;
    GameVector close{0, 0, 0};
    GameVector same{0, 0, 0};
    for (char* at = Link(head, node::kLeft); at != head; at = TreeNext(at)) {
        const uint32_t index = At<uint32_t>(at, node::kKey);
        const float wanted = At<float>(at, node::kValue);
        const float altitude = AtAddress<float>(RecordAddress(VertexBase(self), index));
        if (altitude == wanted) {
            PushBack(same, index);
            PushBack(close, index);
        } else {
            const double apart = std::fabs(static_cast<double>(altitude) - static_cast<double>(wanted));
            if (apart <= static_cast<double>(Global<float>(addr::cSTETerrain_EdgeTolerance)))
                PushBack(close, index);
        }
    }
    if ((static_cast<int32_t>(close.finish - close.start) >> 2) == static_cast<int32_t>(found->count)) {
        // Everything is close enough: nothing to reconcile.
        if (found->count != 0) {
            char* at = Link(found->head, node::kParent);
            while (at) {
                This<void>(addr::cSTETerrain_FreeEdgeNodes, found, Link(at, node::kRight));
                char* const left = Link(at, node::kLeft);
                Cdecl<void>(addr::SC4_Free, at);
                at = left;
            }
            Link(found->head, node::kLeft) = found->head;
            Link(found->head, node::kParent) = nullptr;
            Link(found->head, node::kRight) = found->head;
            found->count = 0;
        }
    } else {
        // Only the vertices that are exactly right are taken out.
        for (uint32_t at = same.start; at != same.finish; at += 4u) {
            const int32_t key = AtAddress<int32_t>(at);
            char* upper = found->head;
            for (char* walk = Link(found->head, node::kParent); walk;) {
                if (key >= At<int32_t>(walk, node::kKey)) {
                    walk = Link(walk, node::kRight);
                } else {
                    upper = walk;
                    walk = Link(walk, node::kLeft);
                }
            }
            char* const lower = LowerBound(found, key);
            This<void>(addr::cSTETerrain_EraseEdgeRange, found, lower, upper);
        }
    }
    if (same.start)
        Cdecl<void>(addr::SC4_Free, same.start);
    if (close.start)
        Cdecl<void>(addr::SC4_Free, close.start);
    DestroySeen(seen);
}

// --- CollectEdgeChanges ----------------------------------------------------------

// A hash set insert as the game makes it everywhere: room for one more,
// then the insert (its result is 9 bytes).
inline void HashInsert(void* hash, int32_t value)
{
    struct {
        uint32_t words[2];
        uint8_t inserted;
        uint8_t pad[3];
    } result;
    This<void>(addr::SC4Uint32HashSet_Resize, hash, At<uint32_t>(hash, 0x10) + 1u);
    This<void>(addr::SC4Uint32HashSet_Insert, hash, static_cast<void*>(&result), &value);
}

// 0x0074B0D0 (set, cells, vertices): see the top of the file. The reach
// is whatever the curve gives, also a negative one; the near ends are cut
// at 0 (signed), the far ends at the cell counts (unsigned); the cell
// square starts one before the vertex square where that is above 0 and
// ends at the last cell.
void __fastcall CollectEdgeChanges(char* self, void*, Tree* found, void* cells, void* vertices)
{
    static bool logged = false;
    FirstCall("CollectEdgeChanges", logged);
    char* const head = found->head;
    for (char* at = Link(head, node::kLeft); at != head; at = TreeNext(at)) {
        const uint32_t index = At<uint32_t>(at, node::kKey);
        const uint32_t x = index % U32(self, field::kVertsX);
        const uint32_t z = index / U32(self, field::kVertsX);
        const float apart =
            static_cast<float>(std::fabs(static_cast<double>(At<float>(at, node::kValue)) -
                                         static_cast<double>(AtAddress<float>(RecordAddress(VertexBase(self), index)))));
        const float reachFloat = static_cast<float>(
            This<double>(addr::SC4Curve_Evaluate, reinterpret_cast<void*>(addr::cSTETerrain_EdgeReachCurve), apart));
        const uint32_t reach = static_cast<uint32_t>(RoundToInt(reachFloat));
        int32_t x0 = static_cast<int32_t>(x - reach);
        if (x0 < 0)
            x0 = 0;
        uint32_t x1 = reach + x;
        if (x1 > U32(self, field::kCellsX))
            x1 = U32(self, field::kCellsX);
        int32_t z0 = static_cast<int32_t>(z - reach);
        if (z0 < 0)
            z0 = 0;
        uint32_t z1 = reach + z;
        if (z1 > U32(self, field::kCellsZ))
            z1 = U32(self, field::kCellsZ);
        int32_t cell[4] = {x0, z0, static_cast<int32_t>(x1), static_cast<int32_t>(z1)};
        if (x0 > 0)
            cell[0] = x0 - 1;
        if (x1 > U32(self, field::kLastCellX))
            cell[2] = static_cast<int32_t>(U32(self, field::kLastCellX));
        if (cell[1] > 0)
            cell[1] = cell[1] - 1;
        if (static_cast<uint32_t>(cell[3]) > U32(self, field::kLastCellZ))
            cell[3] = static_cast<int32_t>(U32(self, field::kLastCellZ));
        for (int32_t row = z0; row <= static_cast<int32_t>(z1); ++row) {
            const uint32_t start = U32(self, field::kVertsX) * static_cast<uint32_t>(row);
            const int32_t last = static_cast<int32_t>(start + x1);
            for (int32_t key = static_cast<int32_t>(start + static_cast<uint32_t>(x0)); key <= last; ++key)
                HashInsert(vertices, key);
        }
        for (int32_t row = cell[1]; row <= cell[3]; ++row) {
            const uint32_t start = U32(self, field::kCellsX) * static_cast<uint32_t>(row);
            const int32_t last = static_cast<int32_t>(start + static_cast<uint32_t>(cell[2]));
            for (int32_t key = static_cast<int32_t>(start + static_cast<uint32_t>(cell[0])); key <= last; ++key)
                HashInsert(cells, key);
        }
    }
}

// --- The hash sets' elements ---------------------------------------------------

// An STLport hash table: the buckets' begin and end at +4 and +8; a node's
// next at +0 and its value at +4.
inline char* HashAfter(char* at, void* table)
{
    if (char* const next = At<char*>(at, 0))
        return next;
    const uint32_t buckets = static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(table, 8) - At<uint32_t>(table, 4)) >> 2);
    uint32_t bucket = At<uint32_t>(at, 4) % buckets;
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(table, 8) - At<uint32_t>(table, 4)) >> 2);
    char* found = nullptr;
    for (;;) {
        ++bucket;
        if (bucket >= count)
            break;
        found = AtAddress<char*>(At<uint32_t>(table, 4) + 4u * bucket);
        if (found)
            break;
    }
    return found;
}

// 0x0074B2D0 (cells): see the top of the file. The vector of points is the
// game's (growing through 0x007C61A0) and freed afterwards.
void __fastcall PrepareEdgeChanges(char* self, void*, void* cells)
{
    static bool logged = false;
    FirstCall("PrepareEdgeChanges", logged);
    GameVector points{0, 0, 0};
    if (!At<char*>(self, field::kView))
        return;
    struct {
        char* node;
        char* table;
    } first{nullptr, nullptr};
    This<void>(addr::SC4HashTable_Begin, cells, static_cast<void*>(&first));
    if (char* at = first.node) {
        char* const table = first.table;
        do {
            uint32_t point[2];
            point[0] = At<uint32_t>(at, 4) % U32(self, field::kCellsX);
            point[1] = At<uint32_t>(at, 4) / U32(self, field::kCellsX);
            if (points.finish != points.capacity) {
                if (points.finish) {
                    AtAddress<uint32_t>(points.finish) = point[0];
                    AtAddress<uint32_t>(points.finish + 4u) = point[1];
                }
                points.finish += 8u;
            } else {
                uint32_t tag = 0;
                This<void>(addr::SC4PointVector_InsertN, &points, points.finish, static_cast<void*>(point), &tag, 1u, 1u);
            }
            at = HashAfter(at, table);
        } while (at);
    }
    Virtual<void>(At<char*>(self, field::kView) + 0xC, view_slot::kShowCells, static_cast<void*>(&points), 0u, 1u);
    if (points.start)
        Cdecl<void>(addr::SC4_Free, points.start);
}

// --- ApplyEdgeChanges ------------------------------------------------------------

// 0x00748C90 (set, vertices): see the top of the file. The rectangle
// starts as the empty one (0x00B0D698) and takes in every vertex of the
// hash set (signed compares); it is given to the smoothing as it is, also
// when it stayed empty.
void __fastcall ApplyEdgeChanges(char* self, void*, Tree* found, void* vertices)
{
    static bool logged = false;
    FirstCall("ApplyEdgeChanges", logged);
    int32_t rect[4];
    rect[0] = Global<int32_t>(addr::cSTETerrain_EmptyRect);
    rect[1] = Global<int32_t>(addr::cSTETerrain_EmptyRect + 4);
    rect[2] = Global<int32_t>(addr::cSTETerrain_EmptyRect + 8);
    rect[3] = Global<int32_t>(addr::cSTETerrain_EmptyRect + 12);
    char* const head = found->head;
    for (char* at = Link(head, node::kLeft); at != head; at = TreeNext(at))
        Virtual<void>(self, slot::kSetAltitudeAtIndex, At<uint32_t>(at, node::kKey), At<uint32_t>(at, node::kValue));

    // The first element: the first bucket that is not empty.
    char* at = nullptr;
    {
        const uint32_t begin = At<uint32_t>(vertices, 4);
        uint32_t bucket = 0;
        if (static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(vertices, 8) - begin) >> 2) != 0) {
            for (;;) {
                if (AtAddress<uint32_t>(begin + 4u * bucket) != 0) {
                    at = AtAddress<char*>(begin + 4u * bucket);
                    break;
                }
                ++bucket;
                if (bucket >=
                    static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(vertices, 8) - At<uint32_t>(vertices, 4)) >> 2))
                    break;
            }
        }
    }
    while (at) {
        const uint32_t index = At<uint32_t>(at, 4);
        const int32_t x = static_cast<int32_t>(index % U32(self, field::kVertsX));
        const int32_t z = static_cast<int32_t>(index / U32(self, field::kVertsX));
        if (x < rect[0])
            rect[0] = x;
        if (x > rect[2])
            rect[2] = x;
        if (z < rect[1])
            rect[1] = z;
        if (z > rect[3])
            rect[3] = z;
        if (!Find(found, static_cast<int32_t>(index)) && x != 0 && static_cast<uint32_t>(x) != U32(self, field::kCellsX) &&
            z != 0 && static_cast<uint32_t>(z) != U32(self, field::kCellsZ))
            AtAddress<uint8_t>(RecordAddress(VertexBase(self), index) + vertex::kFlags) |= 1;
        at = HashAfter(at, vertices);
    }
    This<void>(addr::cSTETerrain_SmoothRect, self, static_cast<void*>(rect), Global<int32_t>(addr::cSTETerrain_EdgeSmoothPasses),
               1u);
    This<void>(addr::cSTETerrain_ClearFlagsInRect, self, static_cast<void*>(rect), 1u);
    Virtual<void>(self, slot::kRedisplayRect, 1u, 1u, static_cast<void*>(rect), 1u);
}

// --- Installation ------------------------------------------------------------

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Every direct call site of each (scan of all direct calls in the exe), all
// in slot 89 (0x0074ED40): 0x0074D010 at 0x0074EDC6 (the stack and BL
// after it), 0x0074B0D0 at 0x0074EE8C (the stack and BL), 0x0074B2D0 at
// 0x0074EE9E (a cdecl call next), 0x00748C90 at 0x0074EF03 (the stack
// next). Arguments on the stack only; nothing kept in ECX, EDX or EAX.
const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::cSTETerrain_FindEdgeDifferences, FindEdgeDifferences),
    OPENSC4_DETOUR(addr::cSTETerrain_CollectEdgeChanges, CollectEdgeChanges),
    OPENSC4_DETOUR(addr::cSTETerrain_PrepareEdgeChanges, PrepareEdgeChanges),
    OPENSC4_DETOUR(addr::cSTETerrain_ApplyEdgeChanges, ApplyEdgeChanges),
};

#undef OPENSC4_DETOUR

void* g_originals[std::size(kDetours)];

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : kDetours) {
        if (!addr_check::Find(detour.address)) {
            log::Write("terrain edges: no address entry for %08X; not installing", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (std::size_t i = 0; i < std::size(kDetours); ++i) {
        if (!hook::Install(*addr_check::Find(kDetours[i].address), kDetours[i].ours, &g_originals[i])) {
            log::Write("terrain edges: detour %u of %u failed (%08X); the earlier ones stay", static_cast<unsigned>(i),
                       static_cast<unsigned>(std::size(kDetours)), static_cast<unsigned>(kDetours[i].address));
            return false;
        }
    }
    log::Write("terrain edges: %u functions (detours) are ours", static_cast<unsigned>(std::size(kDetours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.edges", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_FindEdgeDifferences,
     "Our cSTETerrain edge reconciling workers: the search of the neighbouring cities' saved edge strips, the cells "
     "and vertices around the differences, the cells shown, and the altitudes applied and smoothed, by detours",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
