// Our cSC4TractDeveloper's lot candidates (build 641; docs/tract.md): the
// part of Grow that finds where a building can go on a tract's zoned lots.
// Grow (tract_grow.cpp) runs the passes below in this order and keeps the
// highest reason code each returns (0x1B = something fits; lower codes say
// why nothing did and are counted in the statistics at +0x170):
//
//   Occupy (0x0070A3D0)       existing buildings with room: every lot of the
//                             tract (+0xCC) whose building matches the target
//                             zone kind and is empty (state 4) or, for a
//                             wealthier developer, upgradeable (state 3)
//                             becomes a kind 1 candidate; the candidates are
//                             scored, sorted and taken until the developer's
//                             share of new population (+0xB8..+0xC0 scaled by
//                             +0x44/+0x48) is reached
//   MatchSameSize (0x00708110)  a new building on one empty lot: every lot
//                             configuration of the developer (+0xD8) with the
//                             lot's exact size, zone density and slope range
//                             gives a kind 2 candidate; the first that fits
//                             (FitConfig) is kept
//   MergeLots (0x007097E0)    a bigger building over several lots: for every
//                             larger configuration, a kind 3 candidate at the
//                             lot and one shifted back by the size difference
//                             along the lot's facing, each collecting every
//                             lot it covers (CollectLots)
//   SplitLot (0x00708C00)     smaller buildings side by side on one lot: a
//                             configuration as deep as the lot whose width
//                             divides the lot's gives a kind 4 candidate with
//                             the repeat count at +0x6C
//   ScoreCandidates (0x00705750)  ranks the candidates (+0x34): capacity
//                             weights +0x50/+0x54, merged lot count +0x58,
//                             split bonus +0x5C, the networks next to each
//                             side by zone kind, and noise for industry
//   Record (0x0070AD20)       when the building development simulator asks
//                             for it (slot 57), appends a snapshot of the
//                             candidates and counters to the list at +0x16C
//
// Helpers: EvaluateLot (0x00707A60) checks one lot (zone, recent change,
// land value, state, lot manager verdict, historical, growth stage, road
// access) and caches the verdict per lot in the map at +0x10C; FitConfig
// (0x00706500) checks a configuration against a candidate (size, zone,
// growth stage, slope, power/water/fire/park caps, required roads);
// RoadSides (0x00705DA0) the configuration's required road sides against
// the networks next to the candidate (CountNeighbors 0x00705040);
// CandidateStats (0x007055B0) totals the capacity, growth stage and wealth
// of a candidate's lots. Candidates come from a pool (0x00707D90) and go back
// to it (0x00707320).
//
// Reason codes (as the game assigns them here): 0 size or lot manager 0,
// 3 lot manager 3, 4 lot state or no building type, 5 no configuration of
// that size, 6 zone, 0xA slope, 0xC growth stage vs wealth, 0xF capacity or
// building kind, 0x10 wealth, 0x11 growth stage, 0x12 historical, 0x13
// changed recently, 0x15 road access, 0x16 land value, 0x17 fire, 0x18 park,
// 0x19 water, 0x1A power, 0x1B fits.
//
// Fields of cSC4TractDeveloper used here (offsets from the object's start;
// evidence: the disassembly of the functions named):
//   +0x18 city (0x00B43CEC): slots 93/94 (+0x174/+0x178) the size in cells,
//         slot 40 (+0xA0) the network manager (CountNeighbors, CollectLots)
//   +0x1C 0x00B43D68, the building development simulator: +0x9C building
//         type -> {kind, wealth}, +0x84 (kind, wealth) -> that developer,
//         +0xD8/+0xDC/+0xE0 skip the water/power/all-utility caps, +0xE4
//         keep the history (Record)
//   +0x20 0x00B43D88: +0x18 (x, z) the land value (byte)
//   +0x28 0x00B43D20: +0x74 (rect, 0) the slope of a rectangle; +0x20 (lot,
//         5) and +0x24 (lot, developer type) when Occupy takes a lot
//   +0x2C lot manager: +0x18 (x, z, 0) lot at a cell, +0x50 (x, z, w, d, 0,
//         0, 0) the verdict on a rectangle
//   +0x30 0x00B43D94 (water): +0x2C (x, z) has water
//   +0x34 0x00B43DA0: +0x8C the power grid, whose +0x1C (x, z) < 2 = no power
//   +0x38 0x00B43DA8: +0x34 a grid, whose +0x1C (x, z) is road access for
//         purpose 1
//   +0x3C zone manager: +0x30 (x, z) the zone type at a cell
//   +0x44, +0x48 float  Occupy's minimum and maximum share of the gap
//         between +0xB8/+0xBC and +0xC0
//   +0x50, +0x54, +0x58, +0x5C float  score weights (capacity of other lots,
//         capacity of empty lots, merged lot count, split lot)
//   +0x6C uint8   wealth upgrades allowed (Occupy, EvaluateLot)
//   +0x80 the growth developer (cISC4GrowthDeveloper*): +0x14 target zone
//         kind, +0x18 developer type, +0x1C purpose, +0x20 wealth, +0x24
//         construction info, +0x4C/+0x50/+0x54/+0x58 the power, water, fire
//         and park caps
//   +0x8C int32   growth stage limit (EvaluateLot)
//   +0x98, +0x9C int32  the developer's wealth and kind (EvaluateLot)
//   +0xB8, +0xBC, +0xC0 int32  population targets and the current
//         population; Occupy adds the capacity it takes to +0xC0
//   +0xC8 uint8   copied into Record's snapshot
//   +0xCC set<cISC4Lot*> the tract's lots (header pointer; 0x14-byte nodes)
//   +0xD8 set<cISC4LotConfiguration*> the developer's configurations
//   +0xE4 vector<candidate*> the candidates (end +0xE8, capacity +0xEC)
//   +0xF0..+0x100 int32  copied into Record's snapshot; +0xF8 also gets the
//         land value of the last lot rejected for it (EvaluateLot)
//   +0x104 pointer; Record copies [[[+0x104]]+4]+8
//   +0x108 uint8  set by MatchSameSize when a lot is not in state 3 and not
//         rejected for its zone
//   +0x10C map<cISC4Lot*, int32> the verdict cache (0x18-byte nodes)
//   +0x16C list head of Record's snapshots (0x3C-byte nodes)
//   +0x170 vector<vector<int32>> statistics: each pass counts its reasons in
//         one inner vector (byte offset of the inner vector: Grow 0x0,
//         Occupy 0xC, MatchSameSize 0x24, MergeLots 0x30, SplitLot 0x3C,
//         EvaluateLot 0x60 (indexed by reason code), CollectLots 0x6C,
//         FitConfig 0x84)
//
// A candidate (0xA4 bytes; constructor 0x00707680, assignment 0x00707240):
//   +0x00 int32  kind (1 lot as built, 2 lot + same-size configuration, 3
//                merged lots, 4 split lot)
//   +0x04 cISC4Lot*  the lot
//   +0x08, +0x0C int32  cell x, z
//   +0x10 int32  facing (-1 = take the lot's)
//   +0x14, +0x15 uint8  size x, z
//   +0x18 set<cISC4Lot*> the lots a kind 3 candidate covers (size +0x1C)
//   +0x24 int32  capacity of its lots not in state 4 (-1 = not computed)
//   +0x28 int32  capacity of its lots in state 4 (-1 = not computed; Occupy
//                replaces it with the lot's available capacity)
//   +0x2C int32  highest growth stage of its lots' configurations
//   +0x30 int32  highest wealth of its lots' buildings
//   +0x34 float  score (-1 initially)
//   +0x38 int32[4]  rectangle x0, z0, x1, z1 (-1 = not computed)
//   +0x48..+0x68 nine dwords copied by the assignment, otherwise unused here
//   +0x6C uint8  repeat count of a split lot
//   +0x70, +0x7C, +0x88, +0x94 vector<int32>(4)  per side (0 north of z0, 1
//                east of x1, 2 south of z1, 3 west of x0) the cells next to
//                it with streets or dirt roads (3, 0xB), roads or one-way
//                roads (0, 0xA), avenues (6), rail (1)
//   +0xA0 uint8  the configuration requires a road on side 0, 1 or 2
#include "core/tract_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tract_sim {
namespace {

namespace lots {
constexpr std::size_t kCity = 0x18;
constexpr std::size_t kBuildingDevelopment = 0x1C;
constexpr std::size_t kLandValue = 0x20;
constexpr std::size_t kService28 = 0x28;
constexpr std::size_t kLotManager = 0x2C;
constexpr std::size_t kWater = 0x30;
constexpr std::size_t kPower = 0x34;
constexpr std::size_t kService38 = 0x38;
constexpr std::size_t kZoneManager = 0x3C;
constexpr std::size_t kMinShare = 0x44;  // float
constexpr std::size_t kMaxShare = 0x48;  // float
constexpr std::size_t kWeightOther = 0x50;  // float
constexpr std::size_t kWeightEmpty = 0x54;  // float
constexpr std::size_t kWeightMerged = 0x58; // float
constexpr std::size_t kWeightSplit = 0x5C;  // float
constexpr std::size_t kUpgrades = 0x6C;  // uint8
constexpr std::size_t kGrowthDeveloper = 0x80;
constexpr std::size_t kStageLimit = 0x8C;
constexpr std::size_t kWealth = 0x98;
constexpr std::size_t kKind = 0x9C;
constexpr std::size_t kTarget1 = 0xB8;
constexpr std::size_t kTarget2 = 0xBC;
constexpr std::size_t kPopulation = 0xC0;
constexpr std::size_t kRecordFlag = 0xC8; // uint8
constexpr std::size_t kLots = 0xCC;
constexpr std::size_t kConfigs = 0xD8;
constexpr std::size_t kCandidates = 0xE4; // vector: begin, end +0xE8, capacity +0xEC
constexpr std::size_t kRejectedLandValue = 0xF8;
constexpr std::size_t kRecord104 = 0x104;
constexpr std::size_t kNotAllSettled = 0x108; // uint8
constexpr std::size_t kVerdicts = 0x10C;
constexpr std::size_t kHistory = 0x16C;
constexpr std::size_t kStats = 0x170;
} // namespace lots

// Statistics lists (byte offsets of the inner vectors in +0x170).
namespace stats {
constexpr std::size_t kOccupy = 0x0C;
constexpr std::size_t kSameSize = 0x24;
constexpr std::size_t kMerge = 0x30;
constexpr std::size_t kSplit = 0x3C;
constexpr std::size_t kEvaluate = 0x60;
constexpr std::size_t kCollect = 0x6C;
constexpr std::size_t kFit = 0x84;
} // namespace stats

namespace cand {
constexpr std::size_t kKind = 0x00;
constexpr std::size_t kLot = 0x04;
constexpr std::size_t kX = 0x08;
constexpr std::size_t kZ = 0x0C;
constexpr std::size_t kFacing = 0x10;
constexpr std::size_t kSizeX = 0x14; // uint8
constexpr std::size_t kSizeZ = 0x15; // uint8
constexpr std::size_t kLots = 0x18;  // set header pointer; size +0x1C
constexpr std::size_t kLotCount = 0x1C;
constexpr std::size_t kCapacityOther = 0x24;
constexpr std::size_t kCapacityEmpty = 0x28;
constexpr std::size_t kStage = 0x2C;
constexpr std::size_t kTopWealth = 0x30;
constexpr std::size_t kScore = 0x34; // float
constexpr std::size_t kRect = 0x38;  // int32[4]
constexpr std::size_t kRepeat = 0x6C; // uint8
constexpr std::size_t kStreets = 0x70; // vector<int32>(4) each
constexpr std::size_t kRoads = 0x7C;
constexpr std::size_t kAvenues = 0x88;
constexpr std::size_t kRail = 0x94;
constexpr std::size_t kRoadSides = 0xA0; // uint8
constexpr std::size_t kSize = 0xA4;
} // namespace cand

constexpr int32_t kFits = 0x1B;

// --- Game helpers -----------------------------------------------------------------

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using MapAtFn = int32_t*(__thiscall*)(void* map, void* const* key);
using SetInsertFn = void*(__thiscall*)(void* set, void* result, void* const* key);
using InsertNFn = void(__thiscall*)(void* vector, void** where, void* const* value, void* allocator, uint32_t count,
                                    uint32_t atEnd);
using PushBackFn = void(__thiscall*)(void* vector, void* const* value);
using VectorCopyFn = void*(__thiscall*)(void* self, const void* other);
using IntVectorResizeFn = void(__thiscall*)(void* vector, uint32_t count, const int32_t* value);
using IntVectorAssignFn = void*(__thiscall*)(void* self, const void* other);
using FitsZoneFn = uint32_t(__cdecl*)(uint32_t constructionType, int32_t zoneType);
using StageFitsZoneFn = uint32_t(__cdecl*)(uint32_t stage, uint32_t zoneType);
using ZoneKindOfFn = int32_t(__cdecl*)(uint32_t zoneType);
using NextFloatFn = double(__thiscall*)(void* random); // ST0, unrounded
using AtExitFn = int32_t(__cdecl*)(void (*)());
using EraseFn = void(__thiscall*)(void* tree, void* node);
using Comparator = void*;
using SortLoopFn = void(__cdecl*)(void** first, void** last, void* unused, int32_t depth, Comparator less);
using InsertionSortFn = void(__cdecl*)(void** first, void** last, Comparator less);
using UnguardedSortFn = void(__cdecl*)(void** first, void** last, void* unused, Comparator less);
using ReleaseAllFn = void(__cdecl*)(void* candidates);

void* Allocate(std::size_t size) { return reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(size); }
void Free(void* p) { reinterpret_cast<FreeFn>(addr::SC4_Free)(p); }

// Virtual calls whose result the game reads as AL or AX only.
template <typename... Args>
uint8_t VByte(void* object, std::size_t offset, Args... args)
{
    return static_cast<uint8_t>(Virtual<uint32_t>(object, offset, args...));
}

template <typename... Args>
uint16_t VWord(void* object, std::size_t offset, Args... args)
{
    return static_cast<uint16_t>(Virtual<uint32_t>(object, offset, args...));
}

// A statistics counter: +0x170 is a vector of vectors of int32.
void Count(char* self, std::size_t list, std::size_t byteOffset)
{
    char* const outer = At<char*>(self, lots::kStats);
    ++At<int32_t>(At<char*>(outer, list), byteOffset);
}

// --- STLport trees (0x14-byte nodes: color, parent, left, right, value) ----------

struct Node {
    uint8_t color;
    uint8_t pad[3];
    Node* parent;
    Node* left;
    Node* right;
    void* value; // the key; a map's mapped value follows at +0x14
};
static_assert(sizeof(Node) == 0x14);

// ++ on an iterator (STLport's _M_increment, as the game inlines it).
Node* Next(Node* node)
{
    if (node->right) {
        node = node->right;
        while (node->left)
            node = node->left;
        return node;
    }
    Node* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    if (node->right != parent)
        node = parent;
    return node;
}

// find() on a map keyed by pointer (unsigned compare); the header when absent.
Node* Find(Node* header, void* key)
{
    Node* best = header;
    for (Node* node = header->parent; node;) {
        if (reinterpret_cast<std::uintptr_t>(node->value) < reinterpret_cast<std::uintptr_t>(key)) {
            node = node->right;
        } else {
            best = node;
            node = node->left;
        }
    }
    if (best == header || reinterpret_cast<std::uintptr_t>(key) < reinterpret_cast<std::uintptr_t>(best->value))
        return header;
    return best;
}

// A tree object: header pointer, node count.
struct Tree {
    Node* header;
    uint32_t count;
};

// 0x00705380 / 0x00705430 (_M_copy, two instantiations): copies the subtree
// at `x` under `parent`, right children by recursion, left spines in a loop.
using CopyFn = Node* (*)(Tree* tree, Node* x, Node* parent);

Node* CloneNode(const Node* x)
{
    auto* const node = static_cast<Node*>(Allocate(sizeof(Node)));
    node->value = x->value;
    node->color = x->color;
    node->left = nullptr;
    node->right = nullptr;
    return node;
}

template <CopyFn Self>
Node* CopyNodes(Tree* tree, Node* x, Node* parent)
{
    Node* const top = CloneNode(x);
    top->parent = parent;
    if (x->right)
        top->right = Self(tree, x->right, top);
    parent = top;
    for (x = x->left; x; x = x->left) {
        Node* const y = CloneNode(x);
        parent->left = y;
        y->parent = parent;
        if (x->right)
            y->right = Self(tree, x->right, y);
        parent = y;
    }
    return top;
}

Node* CopyNodesA(Tree* tree, Node* x, Node* parent);
Node* CopyNodesB(Tree* tree, Node* x, Node* parent);

// 0x00705380: _M_copy of the candidates' lot sets.
Node* CopyNodesA(Tree* tree, Node* x, Node* parent) { return CopyNodes<CopyNodesA>(tree, x, parent); }

// 0x00705430: _M_copy of the configuration sets (Record's snapshots).
Node* CopyNodesB(Tree* tree, Node* x, Node* parent) { return CopyNodes<CopyNodesB>(tree, x, parent); }

Node* __fastcall CopyNodesBEntry(Tree* tree, void*, Node* x, Node* parent)
{
    static bool logged = false;
    FirstCall("lots CopyNodes", logged);
    return CopyNodesB(tree, x, parent);
}

// Sets the header's leftmost and rightmost from the root.
void SetExtremes(Tree* tree)
{
    Node* const header = tree->header;
    Node* node = header->parent;
    while (node->left)
        node = node->left;
    header->left = node;
    node = header->parent;
    while (node->right)
        node = node->right;
    header->right = node;
}

// 0x00705950 / 0x00705A10 (operator=, two instantiations): erase (through the
// grow part's 0x00704990 / 0x007049D0), then copy.
Tree* AssignTree(Tree* self, const Tree* other, std::uintptr_t erase, CopyFn copy)
{
    if (self == other)
        return self;
    if (self->count != 0) {
        reinterpret_cast<EraseFn>(erase)(self, self->header->parent);
        self->header->left = self->header;
        self->header->parent = nullptr;
        self->header->right = self->header;
        self->count = 0;
    }
    self->count = 0;
    if (!other->header->parent) {
        self->header->parent = nullptr;
        self->header->left = self->header;
        self->header->right = self->header;
        return self;
    }
    self->header->parent = copy(self, other->header->parent, self->header);
    SetExtremes(self);
    self->count = other->count;
    return self;
}

// 0x00705A10: operator= of the configuration sets (Record; the grow part's
// 0x0070B130).
Tree* __fastcall AssignTreeB(Tree* self, void*, const Tree* other)
{
    static bool logged = false;
    FirstCall("lots AssignTree", logged);
    return AssignTree(self, other, addr::cSC4TractDeveloper_Fn_007049D0, CopyNodesB);
}

// 0x00705AD0: copy constructor of the configuration sets (Record's
// snapshots; the grow part's 0x00709570). The count is copied even when the
// other tree is empty.
Tree* __fastcall ConstructTreeB(Tree* self, void*, const Tree* other)
{
    static bool logged = false;
    FirstCall("lots ConstructTree", logged);
    self->header = nullptr;
    auto* const header = static_cast<Node*>(Allocate(sizeof(Node)));
    self->header = header;
    self->count = 0;
    header->color = 0;
    if (!other->header->parent) {
        header->parent = nullptr;
        header->left = header;
        header->right = header;
        self->count = other->count;
        return self;
    }
    header->parent = CopyNodesB(self, other->header->parent, header);
    SetExtremes(self);
    self->count = other->count;
    return self;
}

// vector<bool> as filled by a configuration's required roads (slot 21,
// +0x54): {word*, bit offset} start, {word*, bit offset} finish, capacity.
struct BitVector {
    uint32_t* words;
    int32_t offset;
    uint32_t* finishWords;
    int32_t finishOffset;
    uint32_t* capacity;
};

// Bit i (the game's floor division by 32, as 0x00477A40).
bool Bit(const BitVector& bits, int32_t i)
{
    const int32_t index = bits.offset + i;
    int32_t word = index / 32;
    int32_t bit = index % 32;
    if (bit < 0) {
        bit += 32;
        --word;
    }
    return (bits.words[word] & (1u << bit)) != 0;
}

// The pointer vectors' push_back as the game inlines it (InsertN when full).
void PushBack(void** vector, void* value)
{
    void** const end = static_cast<void**>(vector[1]);
    if (end != vector[2]) {
        if (end)
            *end = value;
        vector[1] = end + 1;
        return;
    }
    char allocator = 0;
    reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN)(vector, end, &value, &allocator, 1, 1);
}

// --- Candidates ---------------------------------------------------------------------

// 0x00707680: the candidate constructor.
char* __fastcall ConstructCandidate(char* c, void*)
{
    static bool logged = false;
    FirstCall("lots ConstructCandidate", logged);
    At<int32_t>(c, cand::kKind) = 0;
    At<void*>(c, cand::kLot) = nullptr;
    At<int32_t>(c, cand::kX) = -1;
    At<int32_t>(c, cand::kZ) = -1;
    At<int32_t>(c, cand::kFacing) = -1;
    At<uint8_t>(c, cand::kSizeX) = 0;
    At<uint8_t>(c, cand::kSizeZ) = 0;
    At<void*>(c, cand::kLots) = nullptr;
    auto* const header = static_cast<Node*>(Allocate(sizeof(Node)));
    At<Node*>(c, cand::kLots) = header;
    At<uint32_t>(c, cand::kLotCount) = 0;
    header->color = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
    At<int32_t>(c, cand::kCapacityOther) = -1;
    At<int32_t>(c, cand::kCapacityEmpty) = -1;
    At<float>(c, cand::kScore) = -1.0f;
    for (std::size_t i = 0; i < 4; ++i)
        At<int32_t>(c, cand::kRect + 4 * i) = -1;
    At<uint8_t>(c, cand::kRepeat) = 0;
    for (std::size_t offset = cand::kStreets; offset < cand::kRoadSides; offset += 4)
        At<uint32_t>(c, offset) = 0;
    At<uint8_t>(c, cand::kRoadSides) = 0;
    const auto resize = reinterpret_cast<IntVectorResizeFn>(addr::SC4Uint32Vector_Resize);
    for (const std::size_t offset : {cand::kStreets, cand::kRoads, cand::kAvenues, cand::kRail}) {
        const int32_t zero = 0;
        resize(c + offset, 4, &zero);
    }
    return c;
}

// 0x00705950: operator= of the candidates' lot sets (0x00705380, erase 0x00704990).
Tree* AssignTreeA(Tree* self, const Tree* other)
{
    return AssignTree(self, other, addr::cSC4TractDeveloper_Fn_00704990, CopyNodesA);
}

// 0x00707240: candidate assignment. +0x20 and +0x9C (padding) are not copied.
char* __fastcall AssignCandidate(char* c, void*, const char* other)
{
    static bool logged = false;
    FirstCall("lots AssignCandidate", logged);
    for (std::size_t offset = 0; offset <= cand::kFacing; offset += 4)
        At<uint32_t>(c, offset) = At<uint32_t>(const_cast<char*>(other), offset);
    c[cand::kSizeX] = other[cand::kSizeX];
    c[cand::kSizeZ] = other[cand::kSizeZ];
    AssignTreeA(reinterpret_cast<Tree*>(c + cand::kLots), reinterpret_cast<const Tree*>(other + cand::kLots));
    for (std::size_t offset = cand::kCapacityOther; offset < cand::kRepeat; offset += 4)
        At<uint32_t>(c, offset) = At<uint32_t>(const_cast<char*>(other), offset);
    c[cand::kRepeat] = other[cand::kRepeat];
    const auto assign = reinterpret_cast<IntVectorAssignFn>(addr::SC4IntVector_Assign);
    for (const std::size_t offset : {cand::kStreets, cand::kRoads, cand::kAvenues, cand::kRail})
        assign(c + offset, other + offset);
    c[cand::kRoadSides] = other[cand::kRoadSides];
    return c;
}

// 0x00707D90: a candidate from the pool, reset to the default candidate (a
// static built on first use). An empty pool gets ten new candidates first.
char* GetCandidate()
{
    static bool logged = false;
    FirstCall("lots GetCandidate", logged);
    uint32_t& guard = Global<uint32_t>(addr::cSC4TractDeveloper_DefaultCandidateGuard);
    char* const fallback = reinterpret_cast<char*>(addr::cSC4TractDeveloper_DefaultCandidate);
    if (!(guard & 1)) {
        guard |= 1;
        ConstructCandidate(fallback, nullptr);
        reinterpret_cast<AtExitFn>(addr::SC4_atexit)(
            reinterpret_cast<void (*)()>(addr::cSC4TractDeveloper_DefaultCandidateAtExit));
    }
    void** const pool = reinterpret_cast<void**>(addr::cSC4TractDeveloper_CandidatePool);
    if (pool[0] == pool[1]) {
        for (int i = 0; i < 10; ++i) {
            void* fresh = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(cand::kSize);
            fresh = fresh ? ConstructCandidate(static_cast<char*>(fresh), nullptr) : nullptr;
            PushBack(pool, fresh);
        }
    }
    void** const end = static_cast<void**>(pool[1]);
    char* const c = static_cast<char*>(end[-1]);
    pool[1] = end - 1;
    AssignCandidate(c, nullptr, fallback);
    return c;
}

// 0x00707320: back to the pool.
void ReleaseCandidate(char* c) { PushBack(reinterpret_cast<void**>(addr::cSC4TractDeveloper_CandidatePool), c); }

// 0x00704630: the candidate's rectangle (+0x38) unless all four are set.
// With facing -1 it takes facing, size, position and rectangle from the
// lot; otherwise it spans size x, z from the position by the facing.
void CandidateRect(char* c)
{
    static bool logged = false;
    FirstCall("lots CandidateRect", logged);
    int32_t* const rect = &At<int32_t>(c, cand::kRect);
    if (rect[0] >= 0 && rect[1] >= 0 && rect[2] >= 0 && rect[3] >= 0)
        return;
    const int32_t facing = At<int32_t>(c, cand::kFacing);
    if (facing == -1) {
        void* const lot = At<void*>(c, cand::kLot);
        At<int32_t>(c, cand::kFacing) = Virtual<int32_t>(lot, 0x5C);
        Virtual<uint32_t>(lot, 0x58, c + cand::kSizeX, c + cand::kSizeZ);
        Virtual<uint32_t>(lot, 0x54, &At<int32_t>(c, cand::kX), &At<int32_t>(c, cand::kZ));
        Virtual<uint32_t>(lot, 0x38, rect);
        return;
    }
    const int32_t w = At<uint8_t>(c, cand::kSizeX);
    const int32_t d = At<uint8_t>(c, cand::kSizeZ);
    const int32_t x = At<int32_t>(c, cand::kX);
    const int32_t z = At<int32_t>(c, cand::kZ);
    switch (static_cast<uint32_t>(facing)) {
    case 0:
        rect[0] = x - w + 1;
        rect[2] = x;
        rect[1] = z;
        rect[3] = d + z - 1;
        break;
    case 1:
        rect[0] = x - d + 1;
        rect[2] = x;
        rect[1] = z - w + 1;
        rect[3] = z;
        break;
    case 2:
        rect[0] = x;
        rect[2] = w + x - 1;
        rect[1] = z - d + 1;
        rect[3] = z;
        break;
    case 3:
        rect[0] = x;
        rect[2] = d + x - 1;
        rect[1] = z;
        rect[3] = w + z - 1;
        break;
    default:
        break;
    }
}

// One neighbour cell of CountNeighbors: the network occupant there (types
// mask 0xC4B) counted per side.
void CountCell(char* c, void* networks, int32_t x, int32_t z, std::size_t side)
{
    void* const occupant = Virtual<void*>(networks, 0x20, x, z, 0xC4Bu, 1u);
    if (!occupant)
        return;
    const auto bump = [&](std::size_t list) { ++At<int32_t*>(c, list)[side]; };
    if (VByte(occupant, 0x58, 3u) || VByte(occupant, 0x58, 0xBu))
        bump(cand::kStreets);
    if (VByte(occupant, 0x58, 0u) || VByte(occupant, 0x58, 0xAu))
        bump(cand::kRoads);
    if (VByte(occupant, 0x58, 6u))
        bump(cand::kAvenues);
    if (VByte(occupant, 0x58, 1u))
        bump(cand::kRail);
}

// 0x00705040: counts the networks on the cells around the candidate's
// rectangle (a split lot's whole lot), per side.
void CountNeighbors(char* self, char* c)
{
    static bool logged = false;
    FirstCall("lots CountNeighbors", logged);
    void* const networks = Virtual<void*>(Global<void*>(addr::SC4_City), 0xA0);
    CandidateRect(c);
    int32_t rect[4];
    if (At<int32_t>(c, cand::kKind) == 4) {
        Virtual<uint32_t>(At<void*>(c, cand::kLot), 0x38, rect);
    } else {
        for (int i = 0; i < 4; ++i)
            rect[i] = At<int32_t>(c, cand::kRect + 4 * i);
    }
    void* const city = At<void*>(self, lots::kCity);
    if (rect[1] > 0)
        for (int32_t x = rect[0]; x <= rect[2]; ++x)
            CountCell(c, networks, x, rect[1] - 1, 0);
    if (rect[2] < Virtual<int32_t>(city, 0x174) - 1)
        for (int32_t z = rect[1]; z <= rect[3]; ++z)
            CountCell(c, networks, rect[2] + 1, z, 1);
    if (rect[3] < Virtual<int32_t>(city, 0x178) - 1)
        for (int32_t x = rect[0]; x <= rect[2]; ++x)
            CountCell(c, networks, x, rect[3] + 1, 2);
    if (rect[0] > 0)
        for (int32_t z = rect[1]; z <= rect[3]; ++z)
            CountCell(c, networks, rect[0] - 1, z, 3);
}

// One lot of CandidateStats.
void AddLotStats(char* self, char* c, void* lot)
{
    void* const developer = At<void*>(self, lots::kGrowthDeveloper);
    const uint32_t type = Virtual<uint32_t>(developer, 0x18);
    const int32_t capacity = VWord(lot, 0xC8, type, 1u); // GetCapacity(type, force)
    if (Virtual<int32_t>(lot, 0x84) == 4)
        At<int32_t>(c, cand::kCapacityEmpty) += capacity;
    else
        At<int32_t>(c, cand::kCapacityOther) += capacity;
    void* const config = Virtual<void*>(lot, 0x24);
    const int32_t stage = config ? VByte(config, 0x2C) : 0;
    if (At<int32_t>(c, cand::kStage) < stage)
        At<int32_t>(c, cand::kStage) = stage;
    const uint32_t buildingType = Virtual<uint32_t>(lot, 0xB8, 0u);
    if (!buildingType)
        return;
    const int32_t wealth = Virtual<int32_t*>(At<void*>(self, lots::kBuildingDevelopment), 0x9C, buildingType)[1];
    if (At<int32_t>(c, cand::kTopWealth) < wealth)
        At<int32_t>(c, cand::kTopWealth) = wealth;
}

// 0x007055B0: capacity, growth stage and wealth of the candidate's lots
// (its lot for kinds 1, 2, 4; its lot set for kind 3), unless done.
void CandidateStats(char* self, char* c)
{
    if (At<int32_t>(c, cand::kCapacityEmpty) >= 0 && At<int32_t>(c, cand::kCapacityOther) >= 0)
        return;
    At<int32_t>(c, cand::kStage) = 0;
    At<int32_t>(c, cand::kCapacityEmpty) = 0;
    At<int32_t>(c, cand::kCapacityOther) = 0;
    At<int32_t>(c, cand::kTopWealth) = 0;
    switch (At<int32_t>(c, cand::kKind)) {
    case 1:
    case 2:
    case 4:
        AddLotStats(self, c, At<void*>(c, cand::kLot));
        break;
    case 3: {
        Node* const header = At<Node*>(c, cand::kLots);
        for (Node* it = header->left; it != header; it = Next(it))
            AddLotStats(self, c, it->value);
        break;
    }
    default:
        break;
    }
}

void __fastcall CandidateStatsEntry(char* self, void*, char* c)
{
    static bool logged = false;
    FirstCall("lots CandidateStats", logged);
    CandidateStats(self, c);
}

// --- Checks -------------------------------------------------------------------------

// The verdict on one lot (the body of 0x00707A60; MatchSameSize, SplitLot
// and CollectLots inline the same with `full` set).
int32_t AssessLot(char* self, void* lot, bool full)
{
    int32_t x = 0;
    int32_t z = 0;
    Virtual<uint32_t>(lot, 0x54, &x, &z);
    void* const developer = At<void*>(self, lots::kGrowthDeveloper);
    uint32_t info[8] = {}; // GetConstructionInfo writes +0..+0x11
    Virtual<void*>(developer, 0x24, info);
    const auto zone = static_cast<int8_t>(VByte(At<void*>(self, lots::kZoneManager), 0x30, x, z));
    if (!static_cast<uint8_t>(reinterpret_cast<FitsZoneFn>(addr::SC4ConstructionType_FitsZone)(info[0], zone)))
        return 6;
    if (full && VByte(lot, 0x94) && Virtual<uint32_t>(lot, 0xA0) < 0xB4)
        return 0x13;
    const auto* const bytes = reinterpret_cast<const uint8_t*>(info);
    const uint32_t landMin = bytes[0x10];
    const uint32_t landMax = bytes[0x11];
    if (landMin || landMax) {
        const uint32_t value = VByte(At<void*>(self, lots::kLandValue), 0x18, x, z);
        if (value < landMin || value > landMax) {
            At<uint32_t>(self, lots::kRejectedLandValue) = value;
            return 0x16;
        }
    }
    const int32_t state = Virtual<int32_t>(lot, 0x84);
    if (state == 2 || state >= 5)
        return 4;
    int32_t rect[4];
    Virtual<uint32_t>(lot, 0x38, rect);
    const int32_t verdict = Virtual<int32_t>(At<void*>(self, lots::kLotManager), 0x50, rect[0], rect[1],
                                             rect[2] - rect[0] + 1, rect[3] - rect[1] + 1, 0u, 0u, 0u);
    if (verdict != 8 && verdict != 7) {
        switch (static_cast<uint32_t>(verdict)) {
        case 1:
            return 4;
        case 2:
            return 6;
        case 3:
            return 3;
        default:
            return 0;
        }
    }
    if (VByte(lot, 0x98)) {
        const uint32_t buildingType = Virtual<uint32_t>(lot, 0xB8, 0u);
        if (!buildingType)
            return 4;
        const int32_t* const building =
            Virtual<int32_t*>(At<void*>(self, lots::kBuildingDevelopment), 0x9C, buildingType);
        const int32_t kind = building[0];
        if (kind == 5 || (kind == 7 && At<int32_t>(self, lots::kKind) == 6))
            return 0xF;
        if (full) {
            if (!At<uint8_t>(self, lots::kUpgrades) && At<int32_t>(self, lots::kWealth) != building[1])
                return 0x10;
            const int32_t wealth = At<int32_t>(self, lots::kWealth);
            if (wealth < building[1] || (kind != At<int32_t>(self, lots::kKind) && wealth <= building[1]))
                return 0x10;
        }
        if (VByte(lot, 0xAC))
            return 0x12;
        if (void* const config = Virtual<void*>(lot, 0x24)) {
            const int32_t stage = VByte(config, 0x2C);
            const int32_t limit = At<int32_t>(self, lots::kStageLimit);
            const bool ok = building[1] == At<int32_t>(self, lots::kWealth) ? limit > stage : limit >= stage;
            if (!ok)
                return 0x11;
        }
    }
    const int32_t purpose = Virtual<int32_t>(developer, 0x1C);
    uint8_t access;
    if (purpose == 1) {
        void* const grid = Virtual<void*>(At<void*>(self, lots::kService38), 0x34);
        access = VByte(grid, 0x1C, x, z);
    } else if (purpose > 5 && purpose <= 8) {
        access = VByte(lot, 0x74, purpose);
    } else {
        access = VByte(lot, 0x70);
    }
    return access ? kFits : 0x15;
}

// 0x00707A60: the verdict on a lot. With `full` it answers from the cache
// (+0x10C) and stores new verdicts there; each new verdict is counted.
int32_t EvaluateLot(char* self, void* lot, bool full)
{
    static bool logged = false;
    FirstCall("lots EvaluateLot", logged);
    Node* const verdicts = At<Node*>(self, lots::kVerdicts);
    if (full) {
        Node* const found = Find(verdicts, lot);
        if (found != verdicts)
            return At<int32_t>(found, 0x14);
    }
    const int32_t verdict = AssessLot(self, lot, full);
    if (full)
        *reinterpret_cast<MapAtFn>(addr::SC4Uint32Map_At)(self + lots::kVerdicts, &lot) = verdict;
    Count(self, stats::kEvaluate, 4 * static_cast<std::size_t>(verdict));
    return verdict;
}

// 0x00705DA0: every side the configuration requires a road on (bits 0-3 of
// slot 21, turned by the candidate's facing + 1) has a street, road or
// avenue next to the candidate.
uint8_t RoadSides(void* config, char* c)
{
    BitVector bits = {};
    Virtual<uint32_t>(config, 0x54, &bits);
    uint8_t ok = 1;
    for (int32_t i = 0; i <= 3; ++i) {
        if (!Bit(bits, i))
            continue;
        int32_t side = At<int32_t>(c, cand::kFacing) + i + 1;
        if (side < 0)
            side += static_cast<int32_t>((static_cast<uint32_t>(-1 - side) >> 2) * 4 + 4);
        if (side > 3)
            side = side - 4 - static_cast<int32_t>((static_cast<uint32_t>(side - 4) >> 2) << 2);
        if (At<int32_t*>(c, cand::kAvenues)[side] == 0 && At<int32_t*>(c, cand::kRoads)[side] == 0 &&
            At<int32_t*>(c, cand::kStreets)[side] == 0) {
            ok = 0;
            break;
        }
    }
    if (bits.words)
        Free(bits.words);
    return ok;
}

uint8_t __fastcall RoadSidesEntry(char*, void*, void* config, char* c)
{
    static bool logged = false;
    FirstCall("lots RoadSides", logged);
    return RoadSides(config, c);
}

// 0x00706500: does the configuration fit the candidate? Size, zone, growth
// stage against the candidate's buildings (strictly higher for a wealth the
// configuration takes), slope, the utility caps, then (but for industry)
// the required roads; `needRoad` rejects a configuration that requires no
// road on sides 0-2.
int32_t FitConfig(char* self, void* config, char* c, uint8_t needRoad)
{
    const int32_t x = At<int32_t>(c, cand::kX);
    const int32_t z = At<int32_t>(c, cand::kZ);
    CandidateStats(self, c);
    uint32_t sizeX = 0;
    uint32_t sizeZ = 0;
    Virtual<uint32_t>(config, 0x1C, &sizeX, &sizeZ);
    if (sizeX != At<uint8_t>(c, cand::kSizeX) || sizeZ != At<uint8_t>(c, cand::kSizeZ)) {
        Count(self, stats::kFit, 0x14);
        return 0;
    }
    const uint32_t zone = Virtual<uint32_t>(At<void*>(self, lots::kZoneManager), 0x30, x, z);
    if (!VByte(config, 0x3C, zone)) {
        Count(self, stats::kFit, 0x18);
        return 6;
    }
    const uint8_t stage = VByte(config, 0x2C);
    const bool higher = VByte(config, 0x40, At<uint32_t>(c, cand::kTopWealth)) != 0;
    const bool stageOk =
        higher ? static_cast<int32_t>(stage) > At<int32_t>(c, cand::kStage) : stage >= At<int32_t>(c, cand::kStage);
    if (!stageOk) {
        Count(self, stats::kFit, 0x30);
        return 0xC;
    }
    float slope;
    if (void* const lot = At<void*>(c, cand::kLot))
        slope = static_cast<float>(Virtual<double>(lot, 0x48));
    else
        slope = static_cast<float>(Virtual<double>(At<void*>(self, lots::kService28), 0x74, c + cand::kRect, 0u));
    const auto minSlope = static_cast<float>(Virtual<double>(config, 0x24));
    const double maxSlope = Virtual<double>(config, 0x20);
    if (slope < minSlope || slope > maxSlope) {
        Count(self, stats::kFit, 0x28);
        return 0xA;
    }
    void* const development = At<void*>(self, lots::kBuildingDevelopment);
    void* const developer = At<void*>(self, lots::kGrowthDeveloper);
    if (!VByte(development, 0xE0)) {
        if (stage > VByte(developer, 0x4C) && !VByte(development, 0xDC) &&
            VByte(Virtual<void*>(At<void*>(self, lots::kPower), 0x8C), 0x1C, x, z) < 2) {
            Count(self, stats::kFit, 0x68);
            return 0x1A;
        }
        if (stage > VByte(developer, 0x50) && !VByte(development, 0xD8) &&
            !VByte(At<void*>(self, lots::kWater), 0x2C, x, z)) {
            Count(self, stats::kFit, 0x64);
            return 0x19;
        }
        if (stage > VByte(developer, 0x54) &&
            !VByte(Virtual<void*>(Global<void*>(addr::SC4_Service43D78), 0x14), 0x1C, x, z)) {
            Count(self, stats::kFit, 0x5C);
            return 0x17;
        }
        if (stage > VByte(developer, 0x58) &&
            VWord(Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x38), 0x1C, x, z) == 0) {
            Count(self, stats::kFit, 0x60);
            return 0x18;
        }
    }
    if (Virtual<int32_t>(developer, 0x14) == 3) {
        Count(self, stats::kFit, 0x6C);
        return kFits;
    }
    BitVector bits = {};
    Virtual<uint32_t>(config, 0x54, &bits);
    bool front = false;
    for (int32_t i = 0; i < 3; ++i) {
        if (Bit(bits, i)) {
            front = true;
            break;
        }
    }
    if (!needRoad || front) {
        if (RoadSides(config, c)) {
            if (front)
                At<uint8_t>(c, cand::kRoadSides) = 1;
            if (bits.words)
                Free(bits.words);
            Count(self, stats::kFit, 0x6C);
            return kFits;
        }
    }
    Count(self, stats::kFit, 0x54);
    if (bits.words)
        Free(bits.words);
    return 0x15;
}

int32_t __fastcall FitConfigEntry(char* self, void*, void* config, char* c, uint8_t needRoad)
{
    static bool logged = false;
    FirstCall("lots FitConfig", logged);
    return FitConfig(self, config, c, needRoad);
}

// 0x00708700: collects every lot under a merged candidate's rectangle into
// its lot set. All must exist, fit (or lack only road access), share the
// first one's zone type, at least one facing the candidate's way; at least
// two lots, all inside the rectangle.
bool CollectLots(char* self, char* c)
{
    static bool logged = false;
    FirstCall("lots CollectLots", logged);
    int32_t zoneType = 0;
    bool facing = false;
    CandidateRect(c);
    const int32_t* const rect = &At<int32_t>(c, cand::kRect);
    if (rect[0] < 0 || rect[1] < 0)
        return false;
    void* const city = At<void*>(self, lots::kCity);
    if (rect[2] >= Virtual<int32_t>(city, 0x174))
        return false;
    if (rect[3] >= Virtual<int32_t>(city, 0x178))
        return false;
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            void* lot = Virtual<void*>(At<void*>(self, lots::kLotManager), 0x18, x, z, 0u);
            if (!lot)
                return false;
            const int32_t verdict = EvaluateLot(self, lot, true);
            if (verdict == kFits) {
                if (Virtual<int32_t>(lot, 0x5C) == At<int32_t>(c, cand::kFacing))
                    facing = true;
            } else if (verdict != 0x15) {
                return false;
            }
            if (zoneType == 0)
                zoneType = Virtual<int32_t>(lot, 0x7C);
            if (Virtual<int32_t>(lot, 0x7C) != zoneType) {
                Count(self, stats::kCollect, 0x18);
                return false;
            }
            uint32_t result[2];
            reinterpret_cast<SetInsertFn>(addr::SC4PtrSet_Insert)(c + cand::kLots, result, &lot);
        }
    }
    if (!facing) {
        Count(self, stats::kCollect, 0x54);
        return false;
    }
    if (At<uint32_t>(c, cand::kLotCount) < 2) {
        Count(self, stats::kCollect, 0xC);
        return false;
    }
    Node* const header = At<Node*>(c, cand::kLots);
    for (Node* it = header->left; it != header; it = Next(it)) {
        int32_t lotRect[4];
        Virtual<uint32_t>(it->value, 0x38, lotRect);
        if (lotRect[1] < rect[1] || lotRect[3] > rect[3] || lotRect[0] < rect[0] || lotRect[2] > rect[2]) {
            Count(self, stats::kCollect, 0xC);
            return false;
        }
    }
    Count(self, stats::kCollect, 0x6C);
    return true;
}

// --- The passes -----------------------------------------------------------------------

void Raise(int32_t& best, int32_t value)
{
    if (best < value)
        best = value;
}

uint8_t GrowthStageFitsZone(uint32_t stage, uint32_t zone)
{
    return static_cast<uint8_t>(reinterpret_cast<StageFitsZoneFn>(addr::cSC4BuildingDevelopmentSimulator_GrowthStageFitsZone)(stage, zone));
}

// 0x00708110: MatchSameSize (see the file header).
int32_t __fastcall MatchSameSize(char* self, void*)
{
    static bool logged = false;
    FirstCall("lots MatchSameSize", logged);
    int32_t best = 0;
    Node* const lotSet = At<Node*>(self, lots::kLots);
    for (Node* it = lotSet->left; it != lotSet; it = Next(it)) {
        void* const lot = it->value;
        const int32_t verdict = EvaluateLot(self, lot, true);
        if (Virtual<int32_t>(lot, 0x84) != 3 && verdict != 6)
            At<uint8_t>(self, lots::kNotAllSettled) = 1;
        int32_t lotX = 0;
        int32_t lotZ = 0;
        Virtual<uint32_t>(lot, 0x54, &lotX, &lotZ);
        if (verdict != kFits) {
            Raise(best, verdict);
            continue;
        }
        uint8_t sizeX = 0;
        uint8_t sizeZ = 0;
        Virtual<uint32_t>(lot, 0x58, &sizeX, &sizeZ);
        const auto slope = static_cast<float>(Virtual<double>(lot, 0x48));
        Node* const configs = At<Node*>(self, lots::kConfigs);
        for (Node* node = configs->left; node != configs; node = Next(node)) {
            void* const config = node->value;
            uint32_t configX = 0;
            uint32_t configZ = 0;
            Virtual<uint32_t>(config, 0x1C, &configX, &configZ);
            if (configX != sizeX || configZ != sizeZ) {
                Count(self, stats::kSameSize, 0x14);
                Raise(best, 5);
                continue;
            }
            const uint32_t stage = VByte(config, 0x2C);
            if (!GrowthStageFitsZone(stage, Virtual<uint32_t>(At<void*>(self, lots::kZoneManager), 0x30, lotX, lotZ))) {
                Count(self, stats::kSameSize, 0x18);
                Raise(best, 6);
                continue;
            }
            const auto minSlope = static_cast<float>(Virtual<double>(config, 0x24));
            const double maxSlope = Virtual<double>(config, 0x20);
            // fcomp + jp: unordered fails here (it passes in the other passes)
            if (!(minSlope <= slope && slope <= maxSlope)) {
                Count(self, stats::kSameSize, 0x28);
                Raise(best, 10);
                continue;
            }
            char* c = GetCandidate();
            At<int32_t>(c, cand::kKind) = 2;
            At<void*>(c, cand::kLot) = lot;
            Virtual<uint32_t>(lot, 0x54, &At<int32_t>(c, cand::kX), &At<int32_t>(c, cand::kZ));
            At<uint8_t>(c, cand::kSizeX) = sizeX;
            At<uint8_t>(c, cand::kSizeZ) = sizeZ;
            At<int32_t>(c, cand::kFacing) = Virtual<int32_t>(lot, 0x5C);
            CountNeighbors(self, c);
            const int32_t fit = FitConfig(self, config, c, 0);
            Raise(best, fit);
            if (fit == kFits) {
                PushBack(reinterpret_cast<void**>(self + lots::kCandidates), c);
                Count(self, stats::kSameSize, 0x6C);
                break;
            }
            ReleaseCandidate(c);
        }
    }
    return best;
}

// 0x00708C00: SplitLot (see the file header).
int32_t __fastcall SplitLot(char* self, void*)
{
    static bool logged = false;
    FirstCall("lots SplitLot", logged);
    int32_t best = 0;
    Node* const lotSet = At<Node*>(self, lots::kLots);
    for (Node* it = lotSet->left; it != lotSet; it = Next(it)) {
        void* const lot = it->value;
        uint8_t sizeX = 0;
        uint8_t sizeZ = 0;
        Virtual<uint32_t>(lot, 0x58, &sizeX, &sizeZ);
        int32_t lotX = 0;
        int32_t lotZ = 0;
        Virtual<uint32_t>(lot, 0x54, &lotX, &lotZ);
        const auto slope = static_cast<float>(Virtual<double>(lot, 0x48));
        const int32_t verdict = EvaluateLot(self, lot, true);
        if (verdict != kFits) {
            Raise(best, verdict);
            continue;
        }
        Node* const configs = At<Node*>(self, lots::kConfigs);
        for (Node* node = configs->left; node != configs; node = Next(node)) {
            void* const config = node->value;
            uint32_t configX = 0;
            uint32_t configZ = 0;
            Virtual<uint32_t>(config, 0x1C, &configX, &configZ);
            const uint32_t width = sizeX;
            const uint32_t depth = sizeZ;
            // A zero-width configuration divides by zero here, as in the game.
            if (width < configX || depth < configZ || (width == configX && depth == configZ) ||
                width % configX != 0 || depth != configZ) {
                Count(self, stats::kSplit, 0xC);
                Raise(best, 3);
                continue;
            }
            const uint32_t stage = VByte(config, 0x2C);
            if (!GrowthStageFitsZone(stage, Virtual<uint32_t>(At<void*>(self, lots::kZoneManager), 0x30, lotX, lotZ))) {
                Count(self, stats::kSplit, 0x18);
                Raise(best, 6);
                continue;
            }
            const auto minSlope = static_cast<float>(Virtual<double>(config, 0x24));
            const double maxSlope = Virtual<double>(config, 0x20);
            if (slope < minSlope || slope > maxSlope) {
                Count(self, stats::kSplit, 0x28);
                Raise(best, 10);
                continue;
            }
            const uint32_t repeat = width / configX;
            char* c = GetCandidate();
            At<int32_t>(c, cand::kKind) = 4;
            At<void*>(c, cand::kLot) = lot;
            At<uint8_t>(c, cand::kSizeX) = static_cast<uint8_t>(configX);
            At<uint8_t>(c, cand::kSizeZ) = static_cast<uint8_t>(configZ);
            At<int32_t>(c, cand::kFacing) = Virtual<int32_t>(lot, 0x5C);
            Virtual<uint32_t>(At<void*>(c, cand::kLot), 0x54, &At<int32_t>(c, cand::kX), &At<int32_t>(c, cand::kZ));
            At<uint8_t>(c, cand::kRepeat) = static_cast<uint8_t>(repeat);
            CountNeighbors(self, c);
            if (FitConfig(self, config, c, 0) == kFits) {
                reinterpret_cast<PushBackFn>(addr::SC4PtrVector_PushBack)(self + lots::kCandidates,
                                                                          reinterpret_cast<void* const*>(&c));
                Count(self, stats::kSplit, 0x6C);
                best = kFits;
            } else {
                ReleaseCandidate(c);
            }
        }
    }
    return best;
}

// One merged candidate of MergeLots: kept when it collects its lots and the
// configuration fits.
void TryMerged(char* self, void* config, char* c, int32_t& best)
{
    CountNeighbors(self, c);
    if (CollectLots(self, c) && FitConfig(self, config, c, 0) == kFits) {
        reinterpret_cast<PushBackFn>(addr::SC4PtrVector_PushBack)(self + lots::kCandidates,
                                                                  reinterpret_cast<void* const*>(&c));
        Count(self, stats::kMerge, 0x6C);
        best = kFits;
        return;
    }
    ReleaseCandidate(c);
    Raise(best, 5);
}

// 0x007097E0: MergeLots (see the file header).
int32_t __fastcall MergeLots(char* self, void*)
{
    static bool logged = false;
    FirstCall("lots MergeLots", logged);
    int32_t best = 0;
    Node* const lotSet = At<Node*>(self, lots::kLots);
    for (Node* it = lotSet->left; it != lotSet; it = Next(it)) {
        void* const lot = it->value;
        uint8_t sizeX = 0;
        uint8_t sizeZ = 0;
        Virtual<uint32_t>(lot, 0x58, &sizeX, &sizeZ);
        int32_t lotX = 0;
        int32_t lotZ = 0;
        Virtual<uint32_t>(lot, 0x54, &lotX, &lotZ);
        const auto slope = static_cast<float>(Virtual<double>(lot, 0x48));
        Node* const configs = At<Node*>(self, lots::kConfigs);
        for (Node* node = configs->left; node != configs; node = Next(node)) {
            void* const config = node->value;
            uint32_t configX = 0;
            uint32_t configZ = 0;
            Virtual<uint32_t>(config, 0x1C, &configX, &configZ);
            if (sizeX > configX || sizeZ > configZ || (sizeX == configX && sizeZ == configZ)) {
                Count(self, stats::kMerge, 0x14);
                Raise(best, 5);
                continue;
            }
            const uint32_t stage = VByte(config, 0x2C);
            if (!GrowthStageFitsZone(stage, Virtual<uint32_t>(At<void*>(self, lots::kZoneManager), 0x30, lotX, lotZ))) {
                Count(self, stats::kMerge, 0x18);
                Raise(best, 5);
                continue;
            }
            const auto minSlope = static_cast<float>(Virtual<double>(config, 0x24));
            const double maxSlope = Virtual<double>(config, 0x20);
            if (slope < minSlope || slope > maxSlope) {
                Count(self, stats::kMerge, 0x28);
                Raise(best, 5);
                continue;
            }
            // At the lot's position.
            char* c = GetCandidate();
            At<int32_t>(c, cand::kKind) = 3;
            At<uint8_t>(c, cand::kSizeX) = static_cast<uint8_t>(configX);
            At<uint8_t>(c, cand::kSizeZ) = static_cast<uint8_t>(configZ);
            At<int32_t>(c, cand::kFacing) = Virtual<int32_t>(lot, 0x5C);
            Virtual<uint32_t>(lot, 0x54, &At<int32_t>(c, cand::kX), &At<int32_t>(c, cand::kZ));
            TryMerged(self, config, c, best);
            // Shifted back by the width difference along the facing.
            c = GetCandidate();
            At<int32_t>(c, cand::kKind) = 3;
            At<uint8_t>(c, cand::kSizeX) = static_cast<uint8_t>(configX);
            At<uint8_t>(c, cand::kSizeZ) = static_cast<uint8_t>(configZ);
            At<int32_t>(c, cand::kFacing) = Virtual<int32_t>(lot, 0x5C);
            Virtual<uint32_t>(lot, 0x54, &At<int32_t>(c, cand::kX), &At<int32_t>(c, cand::kZ));
            const int32_t shift = static_cast<int32_t>(configX) - sizeX;
            if (shift > 0) {
                switch (static_cast<uint32_t>(At<int32_t>(c, cand::kFacing))) {
                case 0:
                    At<int32_t>(c, cand::kX) += shift;
                    break;
                case 1:
                    At<int32_t>(c, cand::kZ) += shift;
                    break;
                case 2:
                    At<int32_t>(c, cand::kX) -= shift;
                    break;
                case 3:
                    At<int32_t>(c, cand::kZ) -= shift;
                    break;
                default:
                    break;
                }
            }
            At<int32_t>(c, cand::kRect + 4) = -1;
            TryMerged(self, config, c, best);
        }
    }
    return best;
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04).
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// 0x00705750: ScoreCandidates (see the file header). Weights by the target
// zone kind: residential rail 10, road 1, avenue 2; commercial none (rail
// 0 turns the network term off); industrial rail -5, road -1, avenue -1,
// noise 20 and no merged/split terms.
void ScoreCandidates(char* self)
{
    float rail = 0.0f;
    float road = 0.0f;
    float avenue = 0.0f;
    float noise = 0.0f;
    bool shapes = true;
    switch (Virtual<int32_t>(At<void*>(self, lots::kGrowthDeveloper), 0x14)) {
    case 1:
        road = 1.0f;
        avenue = 2.0f;
        rail = 10.0f;
        break;
    case 2:
        road = -2.0f;
        avenue = -5.0f;
        rail = 0.0f;
        break;
    case 3:
        road = -1.0f;
        avenue = -1.0f;
        rail = -5.0f;
        noise = 20.0f;
        shapes = false;
        break;
    default:
        break;
    }
    for (auto** p = At<char**>(self, lots::kCandidates); p != At<char**>(self, lots::kCandidates + 4); ++p) {
        char* const c = *p;
        float& score = At<float>(c, cand::kScore);
        score = 0.0f;
        if (At<int32_t>(c, cand::kKind) != 1) {
            CandidateStats(self, c);
            score = static_cast<float>(
                static_cast<double>(At<int32_t>(c, cand::kCapacityEmpty)) * At<float>(self, lots::kWeightEmpty) +
                static_cast<double>(At<int32_t>(c, cand::kCapacityOther)) * At<float>(self, lots::kWeightOther) +
                score);
        }
        if (shapes) {
            const int32_t kind = At<int32_t>(c, cand::kKind);
            if (kind == 3) {
                const auto merged = static_cast<uint32_t>(At<int32_t>(c, cand::kLotCount) - 1);
                score = static_cast<float>(static_cast<double>(merged) * At<float>(self, lots::kWeightMerged) + score);
            }
            if (kind == 4)
                score = static_cast<float>(static_cast<double>(At<float>(self, lots::kWeightSplit)) + score);
        }
        if (rail != 0.0f) {
            const int32_t* const roads = At<int32_t*>(c, cand::kRoads);
            const int32_t* const avenues = At<int32_t*>(c, cand::kAvenues);
            const int32_t* const tracks = At<int32_t*>(c, cand::kRail);
            // The sum stays on the FPU stack between sides (only the last
            // store counts).
            double sum = static_cast<double>(avenues[0]) * avenue + static_cast<double>(tracks[0]) * rail +
                         static_cast<double>(roads[0]) * road + score;
            for (int side = 1; side < 4; ++side)
                sum += static_cast<double>(roads[side]) * road + static_cast<double>(avenues[side]) * avenue +
                       static_cast<double>(tracks[side]) * rail;
            score = static_cast<float>(sum);
        }
        if (noise != 0.0f) {
            const double draw =
                reinterpret_cast<NextFloatFn>(addr::cRZRandom_NextFloat)(reinterpret_cast<void*>(addr::cSC4TractDeveloper_Random));
            score = static_cast<float>(draw * noise + score);
        }
    }
}

void __fastcall ScoreCandidatesEntry(char* self, void*)
{
    static bool logged = false;
    FirstCall("lots ScoreCandidates", logged);
    ScoreCandidates(self);
}

// 0x00706EA0: the final insertion sort of std::sort (the loops are the grow
// part's 0x007062D0 and 0x00704A90).
void FinalInsertionSort(void** first, void** last, Comparator less)
{
    if (((reinterpret_cast<char*>(last) - reinterpret_cast<char*>(first)) & ~3) > 0x40) {
        reinterpret_cast<InsertionSortFn>(addr::cSC4TractDeveloper_Fn_007062D0)(first, first + 16, less);
        reinterpret_cast<UnguardedSortFn>(addr::cSC4TractDeveloper_Fn_00704A90)(first + 16, last, nullptr, less);
        return;
    }
    reinterpret_cast<InsertionSortFn>(addr::cSC4TractDeveloper_Fn_007062D0)(first, last, less);
}

// 0x00709520: std::sort of candidate pointers (introsort 0x00707F20 with
// depth 2 log2 n, then the insertion sort).
void Sort(void** first, void** last, Comparator less)
{
    static bool logged = false;
    FirstCall("lots Sort", logged);
    if (first == last)
        return;
    int32_t n = static_cast<int32_t>(last - first);
    int32_t depth = 0;
    while (n != 1) {
        n >>= 1;
        ++depth;
    }
    reinterpret_cast<SortLoopFn>(addr::cSC4TractDeveloper_Fn_00707F20)(first, last, nullptr, depth * 2, less);
    FinalInsertionSort(first, last, less);
}

// 0x0070A3D0: Occupy (see the file header).
int32_t __fastcall Occupy(char* self, void*)
{
    static bool logged = false;
    FirstCall("lots Occupy", logged);
    void* const developer = At<void*>(self, lots::kGrowthDeveloper);
    void* const development = At<void*>(self, lots::kBuildingDevelopment);
    const int32_t zoneKind = Virtual<int32_t>(developer, 0x14);
    const uint32_t type = Virtual<uint32_t>(developer, 0x18);
    const int32_t population = At<int32_t>(self, lots::kPopulation);
    const int32_t minimum =
        Ftol(static_cast<double>(At<int32_t>(self, lots::kTarget1) - population) * At<float>(self, lots::kMinShare));
    int32_t offered = 0;
    const int32_t maximum =
        Ftol(static_cast<double>(At<int32_t>(self, lots::kTarget2) - population) * At<float>(self, lots::kMaxShare));
    int32_t best = 1;
    Node* const lotSet = At<Node*>(self, lots::kLots);
    for (Node* it = lotSet->left; it != lotSet; it = Next(it)) {
        void* const lot = it->value;
        const int32_t state = Virtual<int32_t>(lot, 0x84);
        const int32_t lotWealth = Virtual<int32_t>(lot, 0xA4);
        const uint32_t buildingType = Virtual<uint32_t>(lot, 0xB8, 0u);
        const int32_t buildingWealth =
            buildingType ? Virtual<int32_t*>(development, 0x9C, buildingType)[1] : 0;
        const int32_t wealth = Virtual<int32_t>(developer, 0x20);
        if (state != 4 && (!At<uint8_t>(self, lots::kUpgrades) || state != 3 || lotWealth >= wealth ||
                           wealth > buildingWealth))
            continue;
        if (reinterpret_cast<ZoneKindOfFn>(addr::cSC4GrowthDeveloper_ZoneKindOf)(Virtual<uint32_t>(lot, 0x7C)) != zoneKind)
            continue;
        int32_t verdict;
        // The game asks the developer for its type again here.
        if (VWord(lot, 0xC8, Virtual<uint32_t>(developer, 0x18), 1u) == 0) {
            Count(self, stats::kOccupy, 0x3C);
            verdict = 0xF;
        } else {
            verdict = EvaluateLot(self, lot, false);
        }
        if (verdict != kFits && verdict != 0x12) {
            Raise(best, verdict);
            continue;
        }
        char* const c = GetCandidate();
        At<int32_t>(c, cand::kKind) = 1;
        At<void*>(c, cand::kLot) = lot;
        Virtual<uint32_t>(lot, 0x54, &At<int32_t>(c, cand::kX), &At<int32_t>(c, cand::kZ));
        Virtual<uint32_t>(lot, 0x58, c + cand::kSizeX, c + cand::kSizeZ);
        At<int32_t>(c, cand::kFacing) = Virtual<int32_t>(lot, 0x5C);
        CandidateStats(self, c);
        if (At<int32_t>(c, cand::kCapacityEmpty) == 0) {
            const int32_t available = VWord(lot, 0xD0, type, 1u); // GetAvailableCapacity(type, force)
            At<int32_t>(c, cand::kCapacityEmpty) = available;
            if (available == 0 && state == 3)
                At<int32_t>(c, cand::kCapacityEmpty) = VWord(lot, 0xC8, type, 1u);
        }
        bool ok = At<int32_t>(c, cand::kCapacityEmpty) > 0;
        if (ok && !VByte(development, 0xE0)) {
            if (void* const config = Virtual<void*>(lot, 0x24)) {
                const uint8_t stage = VByte(config, 0x2C);
                const int32_t* const building =
                    Virtual<int32_t*>(development, 0x9C, Virtual<uint32_t>(lot, 0xB8, 0u));
                void* const occupantDeveloper = Virtual<void*>(development, 0x84, building[0], building[1]);
                const int32_t x = At<int32_t>(c, cand::kX);
                const int32_t z = At<int32_t>(c, cand::kZ);
                if (stage > VByte(occupantDeveloper, 0x4C) && !VByte(development, 0xDC) &&
                    VByte(Virtual<void*>(At<void*>(self, lots::kPower), 0x8C), 0x1C, x, z) < 2) {
                    Count(self, stats::kOccupy, 0x68);
                    Raise(best, 0x1A);
                    ok = false;
                }
                if (stage > VByte(occupantDeveloper, 0x50) && !VByte(development, 0xD8) &&
                    !VByte(At<void*>(self, lots::kWater), 0x2C, x, z)) {
                    Count(self, stats::kOccupy, 0x64);
                    Raise(best, 0x19);
                    ok = false;
                }
                if (stage > VByte(developer, 0x54) &&
                    !VByte(Virtual<void*>(Global<void*>(addr::SC4_Service43D78), 0x14), 0x1C, x, z)) {
                    Count(self, stats::kOccupy, 0x5C);
                    Raise(best, 0x17);
                    ok = false;
                }
                if (stage > VByte(developer, 0x58) &&
                    VWord(Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x38), 0x1C, x, z) == 0) {
                    Count(self, stats::kOccupy, 0x60);
                    Raise(best, 0x18);
                    ok = false;
                }
            }
        }
        if (!ok) {
            ReleaseCandidate(c);
            continue;
        }
        PushBack(reinterpret_cast<void**>(self + lots::kCandidates), c);
        offered += At<int32_t>(c, cand::kCapacityEmpty);
    }
    const auto releaseAll = reinterpret_cast<ReleaseAllFn>(addr::cSC4TractDeveloper_Fn_00707370);
    char** const candidates = &At<char*>(self, lots::kCandidates);
    if (candidates[0] != candidates[1]) {
        if (offered > minimum) {
            ScoreCandidates(self);
            if (candidates[0] != candidates[1])
                Sort(reinterpret_cast<void**>(candidates[0]), reinterpret_cast<void**>(candidates[1]),
                     reinterpret_cast<Comparator>(addr::cSC4TractDeveloper_Fn_00704770));
            int32_t taken = 0;
            for (auto** p = reinterpret_cast<char**>(candidates[0]); p != reinterpret_cast<char**>(candidates[1]);
                 ++p) {
                char* const c = *p;
                void* const lot = At<void*>(c, cand::kLot);
                void* const service = At<void*>(self, lots::kService28);
                if (Virtual<int32_t>(lot, 0x84) == 3)
                    Virtual<uint32_t>(service, 0x20, lot, 5u);
                Virtual<uint32_t>(service, 0x24, lot, type);
                At<int32_t>(self, lots::kPopulation) += At<int32_t>(c, cand::kCapacityEmpty);
                taken += At<int32_t>(c, cand::kCapacityEmpty);
                if (taken > maximum)
                    break;
            }
            releaseAll(candidates);
            Count(self, stats::kOccupy, 0x6C);
            return kFits;
        }
        releaseAll(candidates);
    }
    if (best == 1)
        Count(self, stats::kOccupy, 0x4);
    return best;
}

// 0x0070AD20: Record (see the file header). The snapshot (0x34 bytes, in a
// 0x3C-byte list node after next/prev): +0 the byte +0xC8, +4 a copy of the
// configuration set, +0x10 vector of copies of the candidates, +0x1C..+0x30
// +0xF0, +0xF4, +0xF8, +0xFC, +0x100 and [[[+0x104]]+4]+8.
struct Snapshot {
    uint8_t flag;
    uint8_t pad[3];
    Tree configs;
    uint32_t unused; // never written
    void** begin;
    void** end;
    void** capacity;
    uint32_t values[6];
};
static_assert(sizeof(Snapshot) == 0x34);

void __fastcall Record(char* self, void*)
{
    static bool logged = false;
    FirstCall("lots Record", logged);
    if (!VByte(At<void*>(self, lots::kBuildingDevelopment), 0xE4))
        return;
    Snapshot snapshot;
    snapshot.unused = 0;
    snapshot.configs.header = nullptr;
    auto* const header = static_cast<Node*>(Allocate(sizeof(Node)));
    snapshot.configs.header = header;
    snapshot.configs.count = 0;
    header->color = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
    snapshot.flag = At<uint8_t>(self, lots::kRecordFlag);
    snapshot.begin = nullptr;
    snapshot.end = nullptr;
    snapshot.capacity = nullptr;
    AssignTree(&snapshot.configs, reinterpret_cast<const Tree*>(self + lots::kConfigs),
               addr::cSC4TractDeveloper_Fn_007049D0, CopyNodesB);
    snapshot.values[0] = At<uint32_t>(self, 0xF0);
    snapshot.values[1] = At<uint32_t>(self, 0xF4);
    char* const first = *At<char**>(self, lots::kRecord104);
    snapshot.values[5] = At<uint32_t>(At<char*>(first, 4), 8);
    snapshot.values[2] = At<uint32_t>(self, 0xF8);
    snapshot.values[3] = At<uint32_t>(self, 0xFC);
    snapshot.values[4] = At<uint32_t>(self, 0x100);
    for (auto** p = At<char**>(self, lots::kCandidates); p != At<char**>(self, lots::kCandidates + 4); ++p) {
        void* copy = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(cand::kSize);
        copy = copy ? ConstructCandidate(static_cast<char*>(copy), nullptr) : nullptr;
        AssignCandidate(static_cast<char*>(copy), nullptr, *p);
        PushBack(reinterpret_cast<void**>(&snapshot.begin), copy);
    }
    auto** const list = At<void**>(self, lots::kHistory);
    auto** const node = static_cast<void**>(Allocate(0x3C));
    auto* const value = reinterpret_cast<Snapshot*>(node + 2);
    if (value) {
        value->flag = snapshot.flag;
        ConstructTreeB(&value->configs, nullptr, &snapshot.configs);
        reinterpret_cast<VectorCopyFn>(addr::SC4PtrVector_Copy)(&value->begin, &snapshot.begin);
        for (int i = 0; i < 6; ++i)
            value->values[i] = snapshot.values[i];
    }
    void** const previous = static_cast<void**>(list[1]);
    node[0] = list;
    node[1] = previous;
    previous[0] = node;
    list[1] = node;
    if (snapshot.begin)
        Free(snapshot.begin);
    if (snapshot.configs.count != 0) {
        reinterpret_cast<EraseFn>(addr::cSC4TractDeveloper_Fn_007049D0)(&snapshot.configs,
                                                                        snapshot.configs.header->parent);
        snapshot.configs.header->left = snapshot.configs.header;
        snapshot.configs.header->parent = nullptr;
        snapshot.configs.header->right = snapshot.configs.header;
        snapshot.configs.count = 0;
    }
    if (snapshot.configs.header)
        Free(snapshot.configs.header);
}

// --- Installation -----------------------------------------------------------------------

struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn), nullptr}

Detour g_detours[] = {
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_0070A3D0, Occupy),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00708110, MatchSameSize),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_007097E0, MergeLots),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00708C00, SplitLot),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00705750, ScoreCandidatesEntry),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_0070AD20, Record),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_007055B0, CandidateStatsEntry),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00705DA0, RoadSidesEntry),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00706500, FitConfigEntry),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00705A10, AssignTreeB),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00705AD0, ConstructTreeB),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00705430, CopyNodesBEntry),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00707240, AssignCandidate),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00707680, ConstructCandidate),
};

#undef OPENSC4_DETOUR

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (Detour& detour : g_detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("tract lots: no detour at %08X", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.tract.lots", "simulators/tract", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSC4TractDeveloper_Fn_0070A3D0,
     "Our tract developer lot candidates: occupying existing buildings, same-size, merged and split lots, "
     "lot and configuration checks, scoring, the history snapshot (docs/tract.md)",
     false, Install});

} // namespace
} // namespace opensc4::tract_sim
