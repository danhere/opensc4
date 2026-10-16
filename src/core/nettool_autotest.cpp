// The network tool's part of the autotest (src/core/autotest_hooks.h;
// docs/nettool.md, "The test").
//
// The daily part ` | nettool ...` (always; ` | nettool none` before the
// network manager is initialised) reads the network manager's own 13 tools
// as the game's callers get them (GetNetworkTool(type, 0): no reference
// added) and mixes, through the tools' vtables (so the game's functions or
// ours, whichever is installed), every query of the first part that changes
// nothing, and the tools' stable fields. Never a pointer: the vtable is
// mixed as its index among the tool family's five, the type row as its
// index in the type table. The fields:
//   tools  how many tools the manager gave; init  how many are initialised
//          now (+0x18); rows  how many have a type row (Init ran once);
//          used  how many have steps or solved cells (a drag ran); cost  on
//          how many GetCostOfSolution was asked;
//          bridges  the bridge records of those rows and how many have a
//          bitmap key; fail  the tools whose failure reason is set
//   st     the fields: type, vtable index, row index, the bytes +0x18,
//          +0x50, +0x52, +0x53, +0x234, +0x354, the words +0x1EC, +0x208,
//          +0x360, the step counts +0x70 and +0x78, the city size +0x240
//          and +0x244, and the lengths of the containers (steps, dragged
//          cells, solved cells, anchors, possible bridges, intersections,
//          major bridges, the vector at +0xE4, the neighbour connections,
//          the three hash sets, the two trees)
//   q      the queries on every tool: QueryInterface with IIDs 1, the
//          tool's, cIGZMessageTarget2's and one nobody answers (the answer's
//          low byte and where in the object the interface lies; the
//          reference given back), GetPreferredBridge, the altitude
//          override's bits, GetFailureState, the four major bridge getters,
//          GetPossibleNeighborConnection, GetPossibleBridges (the count and
//          the IDs), IsRegularStep of each step and GetLastValidPoint (only
//          when the step and cell vectors make the read an inside one),
//          GetCostOfSolution (only the cost of an empty solution: where
//          slot 20 is the general tool's 0x00625640, Init has run once (it
//          reads parameters Init copies) and the solved cells at +0x98 are
//          none; see below), DoNetworkSpecificPlacement (not on the power line
//          tool, whose slot 53 is its own) and GetNetworkHeightForTerrainHeight
//          of three heights (only where slot 74 is 0x0062C990: the general
//          and the power line tool)
//   ids    on the tools with a type row: ViewInputControlId, GetToolViewId,
//          CursorId, DirectionalCursorBase, PenWidth and GetPlacementParams
//          (its words and bytes and the contents of its five vectors; the
//          copies' memory is freed after)
//   br     on the tools with a type row, for each bridge record of the row:
//          GetBridgeBitmapKey and the text GetBridgeName gives
// The tools are met in four states: untouched (made by the manager, never
// initialised: no row, every vector empty, +0x44/+0x48 null), initialised,
// used (a drag ran: steps, cells, solved cells, previews filled) and shut
// down after use (Shutdown 0x00626D30 releases and nulls the terrain +0x44,
// the occupant manager +0x48, +0x4C, the filter +0x364 and the preview
// drawable +0x1F0, empties the possible bridges, the anchors and the dragged
// cells, clears +0x18; it leaves the row +0x358, the params, the steps
// +0x54, the solved cells +0x98, the major bridges +0xD8 and the other
// vectors, and the world cache). Everything read here is valid in all four:
// the fields and the vectors are read in place, IsRegularStep only inside
// the step vector, GetLastValidPoint only with an index inside both
// vectors, the row only when set (the static type table). GetCostOfSolution
// is the exception: for each solved cell it calls the cell entry 0x00633220
// (which MAKES a cell info when the cache has none, through 0x00624B70 and
// the occupant manager +0x48) and the terrain +0x44 (slot 0x30); on a tool
// the crew test used and shut down both are null and the game dies there
// (the silent exit of the crew leg). So it is asked only when +0x98 is
// empty: then it reads the vectors +0xD8, +0xE4, +0xCC (no calls), the RUL0
// store 0x00B452B4 and the words +0x220, +0x320 to +0x328, and nothing else.
// The part's `cost` field counts the tools it was asked of, `used` the tools
// with solved cells or steps.
// OccupantExists is not called: its cell lookup 0x00633220 makes a cell info
// in the world cache when there is none (and a tool that was never
// initialised has no occupant manager at +0x48 for that).
//   cells  the world caches of the 13 tools (part t2): the cells in the
//          cells' tables (walked bucket by bucket), the cells in the copies
//          (+0x14), and for how many of the walked cells GetCell 0x00647A20
//          (through its address: ours or the game's) gives the walked node's
//          cell info; then `ch`, per tool its type, the tables' counts (+0x10)
//          and an order-free sum and xor of each cell's hash: the key and
//          every field but the occupant pointer (+0xC), the padding and the
//          second pairs +0x88 to +0xA4 (the game's 0x00632EF0, kept the
//          game's, leaves those as its stack had them). The
//          walk and GetCell only read: nothing here makes a cell (the cell
//          entry 0x00633220 would, and on a shut-down tool its maker would
//          use the null terrain and occupant manager). The cache is valid in
//          every state the game leaves a tool in: the constructor makes it,
//          Shutdown leaves it, only Reset and the placement code clear it.
//
// OPENSC4_TEST_NETTOOL=<day>: on that day, on each of the 13 tools, the
// setters of the first part with another value, the value read back, then
// the old value put back (by the setter where there is one, else by writing
// the field the setter writes, as the disassembly shows it):
// SetPreferredBridge, SetBridgeAltitudeOverride (the old four bytes put back
// as they were), EnablePreviewUpdate, ForceReload (+0x53 written back),
// ClearFailureReason (on a reason the test writes to +0x20C, with
// GetFailureState and GetFailureReasonString before and after; the old
// reason written back), GetFailureReasonString's slot 52 with four reasons
// (three of the game's texts and one that has none: the missing text),
// SetValidStepCount (only on a tool with no steps, where the marking it
// calls has nothing to mark), SetAnchorPoints (two points of the test's,
// read back, then the old points) with GetDraggedCells, and
// UpdateOnZoneChange. The texts and slot 52 are not asked of the power line
// tool (its slot 52 does other work for a reason of 0), nor
// UpdateOnZoneChange (its slot 10 is its own). The part then ends with
// ` set <checksum>` and one log line `autotest: nettool: ...` says what ran.
//
// OPENSC4_TEST_NETTOOL_CELLS=1 (with OPENSC4_TEST_NETTOOL=<day>; part t2)
// adds, on that day, the world cache's functions on the road tool (type 0)
// as the game's callers use a tool (AddRef, Init, the calls, Shutdown,
// Release), only when the game has not left it initialised: the cache
// emptied (Reset 0x00647B20), the cell entry 0x00633220 for every fourth
// cell in x and z (it makes the cell info of each cell with a network
// occupant: 0x00633030, the cell info's constructor, edge sum, copy and
// assignment, the find-or-insert and the node insert), each found cell's
// hash; the cache's sums; SaveCells (the copy's sums), the cells' table
// cleared (0x006479B0) and RestoreCells (the sums again); ClearCellIndices
// (the sums); CollectCells into a vector of the test's (each cell's hash in
// order; their occupants released and the memory freed after); the
// neighbour walker's two slots (vtable 0x00AA8700, an object on the test's
// stack as 0x00627080 makes it: the cell, the tool, type 0 and the mask of
// the other networks) in four directions on the first 16 cells found; and
// the cache emptied again. It leaves the road tool's cache empty (whatever
// a drag left in it before is gone) and is therefore behind its own switch.
// One log line `autotest: nettool: cells: ...` gives the counts.
//
// Part t3 adds ` lf <hash>` to the daily part, after `ch`: per tool what the
// constructor, Init and DoMessage leave (MixLife below: the vertex counts
// and steps, the terrain's floats, the message's word, the bridge kind
// lists, the filter's mask and the preview drawable's state, never an
// address). OPENSC4_TEST_NETTOOL_LIFE=<bits> (with OPENSC4_TEST_NETTOOL=<day>)
// adds on that day a new tool of each type (GetNetworkTool with `unique`:
// the constructors, the game's three of their own calling ours) through its
// life: the fields; AddRef; Init twice (the second does nothing); Reset
// (its answer the emptied rule records' begin); slots 17 and 16; with bit 2
// GetUITool (the UI object released); DoMessage with a message of another
// type and then of 0xA6B79602 (a stand-in message of the test's; the word
// put back after); Shutdown twice; Init and Shutdown again; Release, which
// deletes the tool (slot 51, the destructor, the drawable's). One log line
// `autotest: nettool: life: ...` gives the counts.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netmgr_sim.h"
#include "core/nettool_sim.h"

#include <cISC4City.h>
#include <cISC4NetworkManager.h>
#include <cISC4NetworkTool.h>

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

using nettool_sim::CallCdecl;
using nettool_sim::CallThis;
using nettool_sim::Count;
using nettool_sim::GameString;
using nettool_sim::ReleaseOf;
using nettool_sim::Vector;
namespace field = nettool_sim::field;
namespace slot = nettool_sim::slot;
namespace params = nettool_sim::params;
namespace row = nettool_sim::row;
namespace bridge = nettool_sim::bridge;

// An IID nobody answers; the failure reason the drive writes, the reasons
// it asks slot 52 for (the last has no text), the preferred bridge offset,
// the altitude and the valid step count it sets, and the two anchors.
constexpr uint32_t kIidNobody = 0x0F10A0CC;
constexpr uint32_t kTestReason = 0xF0000003;
constexpr int32_t kReasons[] = {static_cast<int32_t>(0xF0000001), static_cast<int32_t>(0xF0000002),
                                static_cast<int32_t>(0xF0000007), 0x0F10A0CC};
constexpr int32_t kBridgeStep = 5;
constexpr float kTestAltitude = 42.75f;
constexpr uint32_t kTestSteps = 3;
constexpr uint32_t kTestAnchors[4] = {5, 6, 7, 8};
// The heights given to GetNetworkHeightForTerrainHeight, the most steps
// IsRegularStep is asked about, the general tool's GetCostOfSolution, and
// the most bridge records read per row.
constexpr float kHeights[] = {0.0f, 37.5f, -12.25f};
constexpr uint32_t kMostSteps = 64, kMostBridges = 64;
constexpr std::size_t kCostSlot = 20, kHeightSlot = 74;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixBytes(const char* begin, const char* end)
    {
        Mix(static_cast<uint32_t>(end - begin));
        for (const char* at = begin; at < end; ++at)
            h = (h ^ static_cast<uint8_t>(*at)) * 16777619u;
    }
};

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The city's network manager, once initialised; null otherwise.
cISC4NetworkManager* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    cISC4NetworkManager* const manager = city->GetNetworkManager();
    if (!manager || !At<uint8_t>(manager, netmgr_sim::field::kInitialized))
        return nullptr;
    return manager;
}

char* ToolOf(cISC4NetworkManager* manager, uint32_t type)
{
    return reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(type), false));
}

// The tool's vtable among the family's: 0 general, 1 pipe, 2 subway, 3
// power line, 4 the underground base, 0xFF none of them.
constexpr uint32_t kPowerLine = 3;
uint32_t VtableIndex(char* self)
{
    const std::uintptr_t vtable = At<std::uintptr_t>(self, 0);
    const std::uintptr_t known[] = {addr::cSC4NetworkTool_vftable, addr::cSC4NetworkTool_PipeVftable,
                                    addr::cSC4NetworkTool_SubwayVftable, addr::cSC4NetworkTool_PowerLineVftable,
                                    addr::cSC4NetworkTool_UndergroundVftable};
    for (uint32_t i = 0; i < 5; ++i)
        if (vtable == known[i])
            return i;
    return 0xFF;
}

// The game's address in a slot of the tool's vtable (a detour leaves it).
std::uintptr_t SlotAddress(char* self, std::size_t index)
{
    return At<const std::uintptr_t*>(self, 0)[index];
}

uint32_t RowIndex(char* self)
{
    const std::uintptr_t at = At<std::uintptr_t>(self, field::kRow);
    if (!at)
        return 0xFFFFFFFFu;
    return static_cast<uint32_t>((at - addr::cSC4NetworkConstructionCrew_NetworkToolTuning) / nettool_sim::kRowSize);
}

// The game's memory of a vector the game's code filled for the test.
void FreeVector(Vector& v)
{
    if (v.begin)
        CallCdecl<void, void*>(addr::SC4_Free, v.begin);
    v = Vector{nullptr, nullptr, nullptr};
}

void MixWords(Hash& hash, const Vector& v)
{
    hash.Mix(static_cast<uint32_t>((v.end - v.begin) / 4));
    for (const char* at = v.begin; at + 4 <= v.end; at += 4)
        hash.Mix(*reinterpret_cast<const uint32_t*>(at));
}

// A string of the game, empty, on the test's stack (0x00408480 with "");
// Text mixes its characters and frees them.
void MakeString(GameString& text)
{
    CallThis<void*, const char*>(addr::SC4String_FromText, &text, "");
}
void MixText(Hash& hash, GameString& text)
{
    hash.MixBytes(text.begin, text.end);
    if (text.begin)
        CallCdecl<void, void*>(addr::SC4_Free, text.begin);
    text.begin = text.end = text.capacity = nullptr;
}

// The stable fields (the `st` hash).
void MixFields(Hash& hash, char* self)
{
    hash.Mix(At<uint32_t>(self, field::kType));
    hash.Mix(VtableIndex(self));
    hash.Mix(RowIndex(self));
    for (const std::size_t offset : {field::kInitialized, field::kReplacing, field::kByte52, field::kReload,
                                     field::kPreviewUpdate, field::kHasBridge})
        hash.Mix(At<uint8_t>(self, offset));
    for (const std::size_t offset : {field::kOtherSurfaceMask, field::kWord208, field::kClassMask, field::kStepCount,
                                     field::kValidSteps, field::kCellsX, field::kCellsZ})
        hash.Mix(At<uint32_t>(self, offset));
    hash.Mix(Count(self, field::kSteps, nettool_sim::step::kSize));
    hash.Mix(Count(self, field::kCells, nettool_sim::kPointSize));
    hash.Mix(Count(self, field::kSolved, 0xC));
    hash.Mix(Count(self, field::kAnchors, nettool_sim::kPointSize));
    hash.Mix(Count(self, field::kPossibleBridges, 4));
    for (const std::size_t vector : {field::kIntersections, field::kMajorBridges, field::kVectorE4,
                                     field::kNeighborConnections})
        hash.Mix(Count(self, vector, 1));
    for (const std::size_t set : {field::kSet84, field::kSetA4, field::kSetB8})
        hash.Mix(At<uint32_t>(self, set + 0x10));
    hash.Mix(At<uint32_t>(self, field::kPreviewCount));
    hash.Mix(At<uint32_t>(self, field::kHighlightCount));
}

// One QueryInterface: the answer's low byte and where in the object the
// interface lies (never the address); the reference given back.
void AskInterface(Hash& hash, char* self, uint32_t iid)
{
    void* object = nullptr;
    const uint32_t answer = Virtual<uint32_t>(self, slot::kQueryInterface, iid, &object);
    hash.Mix(answer & 0xFF);
    if ((answer & 0xFF) && object) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(object) - self));
        ReleaseOf(object);
    } else {
        hash.Mix(0xFFFFFFFFu);
    }
}

// The queries (the `q` hash). True when GetCostOfSolution was asked.
bool MixQueries(Hash& hash, char* self)
{
    for (const uint32_t iid : {nettool_sim::kIidUnknown, nettool_sim::kIidTool, nettool_sim::kIidMessageTarget,
                               kIidNobody})
        AskInterface(hash, self, iid);
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetPreferredBridge)));
    hash.Mix(Bits(static_cast<float>(Virtual<double>(self, slot::kGetBridgeAltitudeOverride))));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetFailureState));
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetMajorBridge)));
    uint32_t altitude = 0xFFFFFFFFu, reference = 0xFFFFFFFFu;
    hash.Mix(Virtual<uint8_t>(self, slot::kGetMajorBridgeAltitude, &altitude));
    hash.Mix(altitude);
    hash.Mix(Virtual<uint8_t>(self, slot::kGetMajorBridgeReferenceAltitude, &reference));
    hash.Mix(reference);
    hash.Mix(Virtual<uint8_t>(self, slot::kIsMajorBridgeLandBridge));
    uint32_t neighbor[4] = {0, 0, 0, 0};
    hash.Mix(Virtual<uint8_t>(self, slot::kGetPossibleNeighborConnection, &neighbor[0]));
    for (const uint32_t word : neighbor)
        hash.Mix(word);
    Vector bridges{nullptr, nullptr, nullptr};
    hash.Mix(Virtual<uint8_t>(self, slot::kGetPossibleBridges, &bridges));
    MixWords(hash, bridges);
    FreeVector(bridges);
    const uint32_t steps = Count(self, field::kSteps, nettool_sim::step::kSize);
    for (uint32_t i = 0; i < steps && i < kMostSteps; ++i)
        hash.Mix(Virtual<uint8_t>(self, slot::kIsRegularStep, i));
    const uint32_t valid = At<uint32_t>(self, field::kValidSteps);
    if (valid >= 1 && valid <= steps) {
        const uint32_t index = At<uint32_t>(At<char*>(self, field::kSteps),
                                            (valid - 1) * nettool_sim::step::kSize + nettool_sim::step::kLastCell);
        if (index < Count(self, field::kCells, nettool_sim::kPointSize)) {
            uint32_t point[2] = {0, 0};
            Virtual<void>(self, slot::kGetLastValidPoint, &point[0]);
            hash.Mix(point[0]);
            hash.Mix(point[1]);
        }
    }
    const bool cost = At<char*>(self, field::kRow) && Count(self, field::kSolved, 1) == 0 &&
                      SlotAddress(self, kCostSlot) == addr::cSC4NetworkTool_GetCostOfSolution;
    if (cost)
        hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetCostOfSolution)));
    if (VtableIndex(self) != kPowerLine)
        hash.Mix(Virtual<uint8_t>(self, slot::kDoNetworkSpecificPlacement, 1u, 0u));
    if (SlotAddress(self, kHeightSlot) == addr::cSC4NetworkTool_GetNetworkHeightForTerrainHeight)
        for (const float height : kHeights)
            hash.Mix(Bits(static_cast<float>(Virtual<double>(self, slot::kGetNetworkHeightForTerrainHeight, height))));
    return cost;
}

// The type row's IDs and the placement parameters (the `ids` hash).
void MixIds(Hash& hash, char* self)
{
    hash.Mix(Virtual<uint32_t>(self, slot::kViewInputControlId));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetToolViewId));
    hash.Mix(Virtual<uint32_t>(self, slot::kCursorId));
    hash.Mix(Virtual<uint32_t>(self, slot::kDirectionalCursorBase));
    int32_t penX = 0, penZ = 0;
    Virtual<void>(self, slot::kPenWidth, &penX, &penZ);
    hash.Mix(static_cast<uint32_t>(penX));
    hash.Mix(static_cast<uint32_t>(penZ));
    alignas(4) char copy[params::kSize] = {};
    Virtual<void>(self, slot::kGetPlacementParams, &copy[0]);
    for (std::size_t offset = 0; offset < params::kSize; offset += 4) {
        const bool vector = (offset >= params::kVector28 && offset < params::kVector28 + 12) ||
                            (offset >= params::kVector54 && offset < params::kFloats6C + 12) ||
                            offset >= params::kVectorA0;
        if (offset == params::kByte24) {
            hash.Mix(At<uint8_t>(copy, params::kByte24));
            hash.Mix(At<uint8_t>(copy, params::kDiagonal));
        } else if (offset == params::kByte44) {
            hash.Mix(At<uint8_t>(copy, params::kByte44));
        } else if (!vector) {
            hash.Mix(At<uint32_t>(copy, offset));
        }
    }
    for (const std::size_t vector : {params::kVector28, params::kVector54, params::kVector60, params::kFloats6C,
                                     params::kVectorA0}) {
        Vector& v = *reinterpret_cast<Vector*>(copy + vector);
        MixWords(hash, v);
        FreeVector(v);
    }
}

// The bridge records of the tool's row (the `br` hash); counts the records
// and those with a bitmap key.
void MixBridges(Hash& hash, char* self, uint32_t& records, uint32_t& keyed)
{
    char* const typeRow = At<char*>(self, field::kRow);
    const uint32_t count = Count(typeRow, row::kBridges, bridge::kSize);
    hash.Mix(count);
    for (uint32_t id = 0; id < count && id < kMostBridges; ++id) {
        ++records;
        uint32_t group = 0, instance = 0;
        const uint8_t has = Virtual<uint8_t>(self, slot::kGetBridgeBitmapKey, static_cast<int32_t>(id), &group,
                                             &instance);
        keyed += has != 0;
        hash.Mix(has);
        hash.Mix(group);
        hash.Mix(instance);
        GameString text{};
        MakeString(text);
        Virtual<uint32_t>(self, slot::kGetBridgeName, static_cast<void*>(&text), static_cast<int32_t>(id));
        MixText(hash, text);
    }
}

// ---- The world caches (part t2) -------------------------------------------------

namespace cell = nettool_sim::cell;
namespace cache = nettool_sim::cache;
namespace node = nettool_sim::node;
namespace walker = nettool_sim::walker;

// A cell info's hash: the key and every field but the occupant pointer and
// the padding; the second pairs (+0x88 to +0xA4) only with `pairs`: the
// game's 0x00632EF0 (kept the game's) leaves them as its stack had them, and
// that stack differs when our code ran before it at that depth.
uint32_t CellHash(uint32_t key, char* info, bool pairs)
{
    Hash h;
    h.Mix(key);
    for (const std::size_t offset : {cell::kX, cell::kZ, cell::kFlags})
        h.Mix(At<uint32_t>(info, offset));
    h.Mix(At<uint8_t>(info, cell::kInPlacement));
    h.Mix(At<uint32_t>(info, cell::kStep));
    for (std::size_t offset = cell::kByType; offset <= cell::kCombined; offset += 4)
        h.Mix(At<uint32_t>(info, offset));
    for (std::size_t offset = cell::kOneWay; offset <= cell::kByte57; ++offset)
        h.Mix(At<uint8_t>(info, offset));
    for (std::size_t offset = cell::kHeights; offset <= cell::kSolvedIndex; offset += 4)
        if (pairs || offset < cell::kRanges2 || offset >= cell::kVertices)
            h.Mix(At<uint32_t>(info, offset));
    return h.h;
}

// The bucket count of a table as the game computes it.
uint32_t BucketCount(char* table)
{
    const Vector& buckets = *reinterpret_cast<const Vector*>(table + cache::kBuckets);
    return static_cast<uint32_t>(static_cast<int32_t>(buckets.end - buckets.begin) >> 2);
}

// An order-free sum of a table's cells. With `cells` (the cache) each
// walked node is also looked up through GetCell.
struct TableSum {
    uint32_t cells = 0, found = 0, sum = 0, xored = 0;
};
TableSum SumTable(char* table, char* cells, bool pairs)
{
    TableSum total;
    const uint32_t count = BucketCount(table);
    if (!count)
        return total;
    char** const buckets = reinterpret_cast<char**>(reinterpret_cast<const Vector*>(table + cache::kBuckets)->begin);
    for (uint32_t index = 0; index < count; ++index)
        for (char* at = buckets[index]; at; at = At<char*>(at, node::kNext)) {
            const uint32_t key = At<uint32_t>(at, node::kKey);
            const uint32_t h = CellHash(key, at + node::kValue, pairs);
            ++total.cells;
            total.sum += h;
            total.xored ^= h;
            if (cells && CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_GetCell, cells, key) ==
                             at + node::kValue)
                ++total.found;
        }
    return total;
}

void MixTableSum(Hash& hash, char* table, const TableSum& total)
{
    hash.Mix(At<uint32_t>(table, cache::kCount));
    hash.Mix(total.cells);
    hash.Mix(total.sum);
    hash.Mix(total.xored);
}

// The daily `cells` and `ch` fields of one tool.
void MixCache(Hash& hash, char* self, uint32_t& cells, uint32_t& saved, uint32_t& found)
{
    char* const worldCache = self + field::kWorldCache;
    char* const backup = worldCache + cache::kBackup;
    const TableSum main = SumTable(worldCache, worldCache, false);
    const TableSum copy = SumTable(backup, nullptr, false);
    cells += main.cells;
    saved += copy.cells;
    found += main.found;
    hash.Mix(At<uint32_t>(self, field::kType));
    MixTableSum(hash, worldCache, main);
    MixTableSum(hash, backup, copy);
}

// ---- Part t3: the life fields ----------------------------------------------------

namespace life = nettool_sim::life;
namespace preview = nettool_sim::preview;

// What Init, the constructor and DoMessage leave (the `lf` hash): the vertex
// counts and steps, the terrain's two floats and the angle, the message's
// word, the bridge kind lists, the filter and its mask, the preview list's
// presence and the drawable's non-pointer state (its vtables as known or
// not, the reference count, the two trees' counts, the state bytes +0x48 to
// +0x54). Never an address.
void MixLife(Hash& hash, char* self)
{
    hash.Mix(At<uint32_t>(self, field::kType));
    for (std::size_t at = life::kVerticesX; at < field::kParams; at += 4)
        hash.Mix(At<uint32_t>(self, at));
    for (const std::size_t at : {life::kTerrainScale, life::kTerrainTan, life::kAngle, field::kRefreshWord})
        hash.Mix(At<uint32_t>(self, at));
    MixWords(hash, At<Vector>(self, life::kBridgesBy99));
    MixWords(hash, At<Vector>(self, life::kBridgesBy98));
    // Only Init sets the filter; the constructor leaves the word as it found
    // it (a city whose tools were never initialised, such as a never-played
    // tile, has a stray value there).
    char* const filter = At<uint8_t>(self, field::kInitialized) ? At<char*>(self, field::kFilter) : nullptr;
    hash.Mix(filter ? At<uint32_t>(filter, 0xC) : 0xFFFFFFFFu);
    hash.Mix(At<void*>(self, life::kPreviewList) ? 1u : 0u);
    char* const drawable = At<char*>(self, field::kPreview);
    if (!drawable) {
        hash.Mix(0xFFFFFFFFu);
        return;
    }
    hash.Mix(At<std::uintptr_t>(drawable, 0) == addr::cSC4NetworkToolPreview_vftable ? 1u : 0u);
    hash.Mix(At<std::uintptr_t>(drawable, preview::kBase) == addr::cSC4NetworkToolPreview_BaseVftable ? 1u : 0u);
    hash.Mix(At<uint32_t>(drawable, preview::kBase + 4));
    hash.Mix(At<uint32_t>(drawable, preview::kCells + 4));
    hash.Mix(At<uint32_t>(drawable, preview::kTextured + 4));
    hash.Mix(At<void*>(drawable, preview::kMaterial) ? 1u : 0u);
    hash.MixBytes(drawable + preview::kStatesEnd, drawable + preview::kSize);
}

// ---- The drive -------------------------------------------------------------------

bool g_setDone = false;
uint32_t g_setSum = 0;

struct Ran {
    uint32_t tools = 0, texts = 0, steps = 0, anchors = 0, zone = 0;
};

// GetFailureReasonString (slot 15) into an empty string of the game.
void MixReasonText(Hash& hash, char* self)
{
    GameString text{};
    MakeString(text);
    Virtual<void>(self, slot::kGetFailureReasonString, static_cast<void*>(&text));
    MixText(hash, text);
}

void DriveTool(Hash& hash, char* self, Ran& ran)
{
    const bool power = VtableIndex(self) == kPowerLine;
    ++ran.tools;
    // SetPreferredBridge.
    const int32_t bridgeWas = Virtual<int32_t>(self, slot::kGetPreferredBridge);
    Virtual<int32_t>(self, slot::kSetPreferredBridge, bridgeWas + kBridgeStep);
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetPreferredBridge)));
    Virtual<int32_t>(self, slot::kSetPreferredBridge, bridgeWas);
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetPreferredBridge)));
    // SetBridgeAltitudeOverride: the old four bytes go back as they were.
    const uint32_t altitudeWas = At<uint32_t>(self, field::kAltitudeOverride);
    Virtual<void>(self, slot::kSetBridgeAltitudeOverride, kTestAltitude);
    hash.Mix(Bits(static_cast<float>(Virtual<double>(self, slot::kGetBridgeAltitudeOverride))));
    Virtual<void>(self, slot::kSetBridgeAltitudeOverride, altitudeWas);
    hash.Mix(At<uint32_t>(self, field::kAltitudeOverride));
    // EnablePreviewUpdate.
    const uint8_t previewWas = At<uint8_t>(self, field::kPreviewUpdate);
    Virtual<void>(self, slot::kEnablePreviewUpdate, uint32_t{previewWas ? 0u : 1u});
    hash.Mix(At<uint8_t>(self, field::kPreviewUpdate));
    Virtual<void>(self, slot::kEnablePreviewUpdate, uint32_t{previewWas});
    hash.Mix(At<uint8_t>(self, field::kPreviewUpdate));
    // ForceReload: no setter takes the byte back.
    const uint8_t reloadWas = At<uint8_t>(self, field::kReload);
    Virtual<void>(self, slot::kForceReload);
    hash.Mix(At<uint8_t>(self, field::kReload));
    At<uint8_t>(self, field::kReload) = reloadWas;
    // ClearFailureReason around a reason of the test's, and the texts.
    const uint32_t reasonWas = At<uint32_t>(self, field::kFailure);
    At<uint32_t>(self, field::kFailure) = kTestReason;
    hash.Mix(Virtual<uint32_t>(self, slot::kGetFailureState));
    if (!power)
        MixReasonText(hash, self);
    Virtual<void>(self, slot::kClearFailureReason);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetFailureState));
    hash.Mix(At<uint32_t>(self, field::kFailure));
    if (!power) {
        MixReasonText(hash, self);
        for (const int32_t reason : kReasons) {
            GameString text{};
            MakeString(text);
            Virtual<void>(self, slot::kGetFailureReasonText, reason, &text);
            MixText(hash, text);
        }
        ++ran.texts;
    }
    At<uint32_t>(self, field::kFailure) = reasonWas;
    // SetValidStepCount, only where MarkInvalidSteps has nothing to mark
    // (no steps, and counts that are not negative).
    const int32_t validWas = At<int32_t>(self, field::kValidSteps);
    if (At<int32_t>(self, field::kStepCount) == 0 && validWas >= 0) {
        Virtual<void>(self, slot::kSetValidStepCount, kTestSteps);
        hash.Mix(At<uint32_t>(self, field::kValidSteps));
        Virtual<void>(self, slot::kSetValidStepCount, static_cast<uint32_t>(validWas));
        hash.Mix(At<uint32_t>(self, field::kValidSteps));
        ++ran.steps;
    }
    // SetAnchorPoints (the game's thunk to 0x00611B00) with two points, then
    // the old ones; GetDraggedCells.
    const Vector& anchors = *reinterpret_cast<const Vector*>(self + field::kAnchors);
    const std::vector<uint32_t> anchorsWas(reinterpret_cast<const uint32_t*>(anchors.begin),
                                           reinterpret_cast<const uint32_t*>(anchors.end));
    const Vector test{reinterpret_cast<char*>(const_cast<uint32_t*>(&kTestAnchors[0])),
                      reinterpret_cast<char*>(const_cast<uint32_t*>(&kTestAnchors[4])),
                      reinterpret_cast<char*>(const_cast<uint32_t*>(&kTestAnchors[4]))};
    Virtual<void>(self, slot::kSetAnchorPoints, &test);
    MixWords(hash, anchors);
    char* const was =
        anchorsWas.empty() ? nullptr : reinterpret_cast<char*>(const_cast<uint32_t*>(anchorsWas.data()));
    char* const wasEnd = was ? was + anchorsWas.size() * 4 : nullptr;
    const Vector back{was, wasEnd, wasEnd};
    Virtual<void>(self, slot::kSetAnchorPoints, &back);
    MixWords(hash, anchors);
    ++ran.anchors;
    Vector cells{nullptr, nullptr, nullptr};
    Virtual<void>(self, slot::kGetDraggedCells, &cells);
    MixWords(hash, cells);
    FreeVector(cells);
    // UpdateOnZoneChange (five words, none read).
    if (!power) {
        Virtual<void>(self, slot::kUpdateOnZoneChange, 0u, 0u, 0u, 0u, 0u);
        ++ran.zone;
    }
}

// OPENSC4_TEST_NETTOOL_CELLS: the world cache's functions on the road tool
// (see the top of the file).
constexpr uint32_t kCellsType = 0, kCellStep = 4, kMostWalked = 16, kDirections = 4;

struct CellsRan {
    uint32_t asked = 0, found = 0, cached = 0, saved = 0, restored = 0, collected = 0, walked = 0;
};

void DriveCells(Hash& hash, cISC4NetworkManager* manager, CellsRan& ran)
{
    char* const self = ToolOf(manager, kCellsType);
    if (!self || At<uint8_t>(self, field::kInitialized)) {
        log::Write("autotest: nettool: cells: the road tool is %s; not driven", self ? "initialised" : "missing");
        hash.Mix(0xFFFFFFFFu);
        return;
    }
    char* const worldCache = self + field::kWorldCache;
    char* const backup = worldCache + cache::kBackup;
    Virtual<uint32_t>(self, slot::kAddRef);
    Virtual<uint8_t>(self, slot::kInit);
    CallThis<void>(addr::cSC4NetworkWorldCache_Reset, worldCache);
    const uint32_t sizeX = At<uint32_t>(self, field::kCellsX), sizeZ = At<uint32_t>(self, field::kCellsZ);
    std::vector<uint32_t> walk;
    for (uint32_t z = 0; z < sizeZ; z += kCellStep)
        for (uint32_t x = 0; x < sizeX; x += kCellStep) {
            const uint32_t key = (z << 16) + x;
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
            ++ran.asked;
            hash.Mix(info ? 1u : 0u);
            if (!info)
                continue;
            ++ran.found;
            hash.Mix(CellHash(key, info, true));
            if (walk.size() < kMostWalked)
                walk.push_back(key);
        }
    TableSum total = SumTable(worldCache, worldCache, true);
    ran.cached = total.cells;
    MixTableSum(hash, worldCache, total);
    hash.Mix(total.found);
    CallThis<void>(addr::cSC4NetworkWorldCache_SaveCells, worldCache);
    total = SumTable(backup, nullptr, true);
    ran.saved = total.cells;
    MixTableSum(hash, backup, total);
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearTable, worldCache);
    hash.Mix(At<uint32_t>(worldCache, cache::kCount));
    CallThis<void>(addr::cSC4NetworkWorldCache_RestoreCells, worldCache);
    total = SumTable(worldCache, worldCache, true);
    ran.restored = total.cells;
    MixTableSum(hash, worldCache, total);
    hash.Mix(total.found);
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearCellIndices, worldCache);
    MixTableSum(hash, worldCache, SumTable(worldCache, nullptr, true));
    Vector collected{nullptr, nullptr, nullptr};
    CallThis<void, Vector*>(addr::cSC4NetworkWorldCache_CollectCells, worldCache, &collected);
    for (char* at = collected.begin; at && at < collected.end; at += cell::kSize) {
        ++ran.collected;
        hash.Mix(CellHash(0, at, true));
        if (void* const occupant = At<void*>(at, cell::kOccupant))
            ReleaseOf(occupant);
    }
    FreeVector(collected);
    alignas(4) char walkerObject[walker::kSize] = {};
    At<std::uintptr_t>(walkerObject, 0) = addr::cSC4NetworkTool_NeighborVtable;
    At<char*>(walkerObject, walker::kTool) = self;
    At<uint32_t>(walkerObject, walker::kType) = kCellsType;
    At<uint32_t>(walkerObject, walker::kMask) =
        Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + kCellsType * 4) ^ 0x1FFFu;
    for (const uint32_t key : walk) {
        At<uint32_t>(walkerObject, walker::kX) = key & 0xFFFF;
        At<uint32_t>(walkerObject, walker::kZ) = key >> 16;
        for (uint32_t direction = 0; direction < kDirections; ++direction) {
            hash.Mix(Virtual<uint32_t>(walkerObject, walker::kIsClearSlot, direction));
            uint32_t edges = 0xFFFFFFFFu;
            char* const info = Virtual<char*>(walkerObject, walker::kEdgesSlot, direction, &edges);
            hash.Mix(info ? 1u : 0u);
            hash.Mix(edges);
        }
        ++ran.walked;
    }
    CallThis<void>(addr::cSC4NetworkWorldCache_Reset, worldCache);
    hash.Mix(At<uint32_t>(worldCache, cache::kCount));
    hash.Mix(At<uint32_t>(backup, cache::kCount));
    Virtual<uint8_t>(self, slot::kShutdown);
    Virtual<uint32_t>(self, slot::kRelease);
}

// OPENSC4_TEST_NETTOOL_LIFE (part t3; see the top of the file): a new tool
// of each type through its whole life.
constexpr uint32_t kOtherMessage = 0x0F10A0CC, kMessageWord = 0x00C0FFEE;

uint32_t __fastcall FakeRefreshType(void*, void*) { return life::kRefreshMessage; }
uint32_t __fastcall FakeOtherType(void*, void*) { return kOtherMessage; }
uint32_t* __fastcall FakeData(void*, void*)
{
    static uint32_t word = kMessageWord;
    return &word;
}
// A message as DoMessage reads it: slot 4 (+0x10) its type, slot 15 (+0x3C)
// the address of its first data word.
struct FakeMessage {
    const void* const* vtable;
};

struct LifeRan {
    uint32_t made = 0, inits = 0, resets = 0, uis = 0, messages = 0, deleted = 0;
};

void DriveLife(Hash& hash, cISC4NetworkManager* manager, int32_t bits, LifeRan& ran)
{
    const void* refreshSlots[16] = {};
    const void* otherSlots[16] = {};
    refreshSlots[4] = reinterpret_cast<const void*>(&FakeRefreshType);
    otherSlots[4] = reinterpret_cast<const void*>(&FakeOtherType);
    refreshSlots[15] = otherSlots[15] = reinterpret_cast<const void*>(&FakeData);
    FakeMessage refresh{refreshSlots}, other{otherSlots};
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(type), true));
        hash.Mix(self ? 1u : 0u);
        if (!self)
            continue;
        ++ran.made;
        hash.Mix(VtableIndex(self));
        MixFields(hash, self);
        MixLife(hash, self);
        Virtual<uint32_t>(self, slot::kAddRef);
        hash.Mix(Virtual<uint8_t>(self, slot::kInit));
        hash.Mix(Virtual<uint8_t>(self, slot::kInit));
        ++ran.inits;
        MixFields(hash, self);
        MixLife(hash, self);
        char* const end = Virtual<char*>(self, slot::kReset);
        hash.Mix(end == At<Vector>(self, life::kRuleCells).begin ? 1u : 0u);
        ++ran.resets;
        MixFields(hash, self);
        Virtual<void>(self, slot::kClearCellPreviews);
        Virtual<void>(self, slot::kClearOccupantHighlights);
        MixLife(hash, self);
        if (bits & 2) {
            void* ui = nullptr;
            hash.Mix(Virtual<uint8_t>(self, slot::kGetUITool, &ui));
            hash.Mix(ui ? 1u : 0u);
            if (ui) {
                ReleaseOf(ui);
                ++ran.uis;
            }
        }
        const uint32_t wordWas = At<uint32_t>(self, field::kRefreshWord);
        hash.Mix(Virtual<uint8_t>(self + nettool_sim::kMessagePart, slot::kDoMessage, &other));
        hash.Mix(At<uint32_t>(self, field::kRefreshWord));
        hash.Mix(Virtual<uint8_t>(self + nettool_sim::kMessagePart, slot::kDoMessage, &refresh));
        hash.Mix(At<uint32_t>(self, field::kRefreshWord));
        At<uint32_t>(self, field::kRefreshWord) = wordWas;
        ++ran.messages;
        hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
        hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
        MixFields(hash, self);
        MixLife(hash, self);
        hash.Mix(Virtual<uint8_t>(self, slot::kInit));
        MixLife(hash, self);
        hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
        hash.Mix(Virtual<uint32_t>(self, slot::kRelease));
        ++ran.deleted;
    }
}

void Drive(const Day& day)
{
    if (g_setDone || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL"))
        return;
    cISC4NetworkManager* const manager = Manager(day.city);
    if (!manager) {
        log::Write("autotest: nettool: no network manager; nothing driven");
        return;
    }
    Hash hash;
    Ran ran;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = ToolOf(manager, type);
        hash.Mix(self ? 1u : 0u);
        if (self)
            DriveTool(hash, self, ran);
    }
    if (EnvInt("OPENSC4_TEST_NETTOOL_CELLS", 0) == 1) {
        CellsRan cells;
        DriveCells(hash, manager, cells);
        log::Write("autotest: nettool: cells: %u asked, %u found, %u cached, %u saved, %u restored, %u collected, "
                   "%u walked",
                   cells.asked, cells.found, cells.cached, cells.saved, cells.restored, cells.collected,
                   cells.walked);
    }
    if (const int32_t bits = EnvInt("OPENSC4_TEST_NETTOOL_LIFE", 0); bits > 0) {
        LifeRan life;
        DriveLife(hash, manager, bits, life);
        log::Write("autotest: nettool: life: %u made, %u initialised, %u reset, %u UI objects, %u messages, %u "
                   "deleted",
                   life.made, life.inits, life.resets, life.uis, life.messages, life.deleted);
    }
    g_setDone = true;
    g_setSum = hash.h;
    log::Write("autotest: nettool: drive: %u tools, texts on %u, step counts on %u, anchors on %u, zone change on "
               "%u; set %08X",
               ran.tools, ran.texts, ran.steps, ran.anchors, ran.zone, hash.h);
}

void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4NetworkManager* const manager = Manager(city);
    if (!manager) {
        line += " | nettool none";
        return;
    }
    Hash fields, queries, ids, bridges, caches, lives;
    uint32_t tools = 0, initialised = 0, rows = 0, records = 0, keyed = 0, failing = 0, used = 0, costs = 0;
    uint32_t cells = 0, saved = 0, found = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = ToolOf(manager, type);
        if (!self) {
            fields.Mix(0xFFFFFFFFu);
            continue;
        }
        ++tools;
        initialised += At<uint8_t>(self, field::kInitialized) != 0;
        failing += At<uint32_t>(self, field::kFailure) != 0;
        MixFields(fields, self);
        used += Count(self, field::kSolved, 1) != 0 || Count(self, field::kSteps, 1) != 0;
        costs += MixQueries(queries, self) ? 1u : 0u;
        MixCache(caches, self, cells, saved, found);
        MixLife(lives, self);
        if (!At<char*>(self, field::kRow))
            continue;
        ++rows;
        MixIds(ids, self);
        MixBridges(bridges, self, records, keyed);
    }
    char text[256];
    std::snprintf(text, sizeof text,
                  " | nettool tools %u init %u rows %u used %u cost %u bridges %u,%u fail %u st %08X q %08X ids "
                  "%08X br %08X",
                  tools, initialised, rows, used, costs, records, keyed, failing, fields.h, queries.h, ids.h,
                  bridges.h);
    line += text;
    std::snprintf(text, sizeof text, " cells %u,%u,%u ch %08X lf %08X", cells, saved, found, caches.h, lives.h);
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X", g_setSum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"nettool", 175, &Fingerprint};
const DayHook kDrive{"nettool.drive", 197, &Drive};

} // namespace
} // namespace opensc4::autotest
