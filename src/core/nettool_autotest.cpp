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
    Hash fields, queries, ids, bridges;
    uint32_t tools = 0, initialised = 0, rows = 0, records = 0, keyed = 0, failing = 0, used = 0, costs = 0;
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
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X", g_setSum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"nettool", 175, &Fingerprint};
const DayHook kDrive{"nettool.drive", 197, &Drive};

} // namespace
} // namespace opensc4::autotest
