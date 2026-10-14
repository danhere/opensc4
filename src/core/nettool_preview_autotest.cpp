// The network tool's cell previews and occupant highlights in the autotest
// (part t4; src/core/nettool_preview.cpp has the layouts).
//
// The daily part ` | ntpv p<cells>,<textured> h<highlighted> r<regions>
// <hash>` (always, once the network manager is initialised; ` | ntpv none`
// before) reads the network manager's own 13 tools (GetNetworkTool(type, 0):
// no reference added) and hashes what this part maintains, values only:
//   - per tool its type, then the preview drawable (+0x1F0; 0xFFFFFFFF when
//     none): the cell tree's and the textured tree's counts and an
//     order-free sum and xor of their nodes (each node: the key, the 0x68
//     record's bytes (vertices, colours, UVs, corner bytes, the height word),
//     for a textured node whether it has a binding, never the binding);
//   - the highlight region (+0x210): its rectangle, width, depth, words per
//     row and every row's words (rows are by x from the rectangle: no
//     address order); the cost words +0x220 and +0x224;
//   - the highlighted occupants (+0x214, an SC4RefSet ordered by address):
//     the count and an order-free sum and xor of each occupant's GetType
//     (slot 0x1C), GetHighlight (0x40) and GetBoundingCityCells' rectangle (0x2C);
//   - the count of the occupant tree +0x228 (ClearOccupantHighlights').
// The counts of previewed cells, textured cells, highlighted occupants and
// regions over the 13 tools are given in clear. All of it reads memory in
// place or asks read-only getters; it is valid in every state the game
// leaves a tool in (the trees and the set always have their heads; the
// drawable is null before Init and after Shutdown).
//
// OPENSC4_TEST_NETTOOL_PREVIEW=1 (with OPENSC4_TEST_NETTOOL=<day>) adds, on
// that day, a new road tool (GetNetworkTool(0, unique), AddRef, Init; at the
// end slots 16 and 17, Shutdown, Release: the tool is deleted) and on it:
//   cells     up to 12 cells with a network occupant, every 4th cell in x and
//             z, through the tool's cell entry 0x00633220 (it makes their
//             cell infos in this tool's own world cache);
//   previews  slot 58 AddCellPreview on a copy of each one's cell info with
//             the placement byte +0x10 set and a solved cell made from its
//             occupant (PieceId slot 0x5C, the turn and flip byte slot
//             0x68): the cell's previews erased, then textured cells through
//             the game's drawing 0x00620BC0 (the texture service) and so our
//             tree inserts, or AddCell without a texture; the drawable's
//             trees hashed, then slot 16 (which empties them) and hashed
//             again;
//   pieces    the piece lookup 0x00633FA0 at its address for each cell and
//             each type whose flag the cell has (the walker, the rule files'
//             and entries' match, the walker's memo and its vector growth):
//             AL and the three outputs;
//   quads     slot 80 SetQuadTrianglesToDraw on each cell's network occupant
//             with five edge words (one of each of the five outcomes): the
//             occupant's network flags & 0x07000000 after each; then the
//             three flags put back as they were (SetNetworkFlag +0x28 /
//             ClearNetworkFlag +0x2C: only 0x10000000 makes those post a
//             message) and checked;
//   highlights  slot 59 with no valid step (+0x78 written 0: slot 17 and
//             answer 0); then six cells in a row around the first lot found
//             (every 4th cell; the city's lot manager, slot 0x84 then
//             GetLot slot 0x18, as the collector asks) written into the
//             tool's dragged cells +0x60 and one step +0x54 (the game's
//             allocator), +0x78 = 1: slot 59 twice (the collector, the
//             demolition service's DemolishRegion in query mode only: it
//             prices and collects, removes nothing; the diff highlights the
//             collected occupants with SetHighlight(5, 0)); the region, the
//             cost and the set hashed after each; then the set's occupants
//             referenced, slot 17 (SetHighlight(0, 0) on each, the set
//             emptied, the region deleted) and their GetHighlight read back
//             (0) and the references released; the step, cells and +0x78 put
//             back as they were.
// Slot 60 DestroyHighlightedOccupants is never called: it is
// DemolishRegion(1), it removes the buildings. The highlight values the
// occupants had before are not saved (they cannot be known before the
// collector names the occupants): in an unattended run no tool is active,
// so they are 0 before and after. One log line `autotest: ntpv: ...` gives
// the counts; the daily part then ends with ` set <hash>`.
//
// What the crew test (OPENSC4_TEST_CREW) reaches of this part: its drags go
// through PlaceAllSegments 0x0063BDC0, whose 0x0063B830 calls slot 59 (the
// collector, the diff), slot 55 0x006258C0 (slot 58 AddCellPreview, the
// piece lookup and its rule match, the drawable's feeders) and 0x006287D0
// (AddCell, EraseCell), and, when it places, slot 60 (the occupants in the
// way demolished) and slot 72 0x006278A0 (slot 80 on each new piece).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netmgr_sim.h"
#include "core/nettool_sim.h"

#include <cISC4City.h>
#include <cISC4NetworkManager.h>

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
using nettool_sim::ReleaseOf;
using nettool_sim::Vector;
namespace field = nettool_sim::field;
namespace slot = nettool_sim::slot;
namespace cell = nettool_sim::cell;
namespace tree = nettool_sim::tree;
namespace preview = nettool_sim::preview;
namespace life = nettool_sim::life;

// The tool's fields of this part, its slots, the record's size.
constexpr std::size_t kRegion = 0x210, kSet = 0x214, kSetCount = 0x218, kCost = 0x220;
constexpr std::size_t kAddCellPreview = 0xE8, kUpdateOccupantHighlights = 0xEC, kSetQuadTrianglesToDraw = 0x140;
constexpr std::size_t kRecordSize = 0x68, kNodeKey = 0x10, kNodeRecord = 0x14, kNodeTexture = 0x7C;
// The region's fields (an SC4CellRegion).
constexpr std::size_t kRegionWidth = 0x14, kRegionDepth = 0x18, kRegionWords = 0x1C, kRegionRows = 0x20;
// The occupants' getters, the network occupant's.
constexpr std::size_t kGetType = 0x1C, kGetBoundingCityCells = 0x2C, kGetHighlight = 0x40;
constexpr std::size_t kSetNetworkFlag = 0x28, kClearNetworkFlag = 0x2C, kGetNetworkFlag = 0x30;
constexpr std::size_t kPieceId = 0x5C, kRotationAndFlip = 0x68;
constexpr uint32_t kQuadFlags[] = {0x1000000, 0x2000000, 0x4000000};
constexpr uint32_t kQuadWords[] = {0x9, 0xB, 0x900, 0x90000, 0x77};
// The drive's limits.
constexpr uint32_t kRoadType = 0, kStep = 4, kMostCells = 12, kRowCells = 6, kMostHeld = 64;
constexpr int32_t kBudget = 7;

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

// An order-free sum of hashes.
struct Sum {
    uint32_t count = 0, sum = 0, xored = 0;
    void Add(uint32_t h)
    {
        ++count;
        sum += h;
        xored ^= h;
    }
    void MixInto(Hash& hash) const
    {
        hash.Mix(count);
        hash.Mix(sum);
        hash.Mix(xored);
    }
};

// The node after `node` in order.
char* Next(char* node)
{
    if (char* right = At<char*>(node, tree::kRight)) {
        while (char* left = At<char*>(right, tree::kLeft))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, tree::kParent);
    while (node == At<char*>(parent, tree::kRight)) {
        node = parent;
        parent = At<char*>(parent, tree::kParent);
    }
    if (At<char*>(node, tree::kRight) != parent)
        node = parent;
    return node;
}

cISC4NetworkManager* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    cISC4NetworkManager* const manager = city->GetNetworkManager();
    if (!manager || !At<uint8_t>(manager, netmgr_sim::field::kInitialized))
        return nullptr;
    return manager;
}

// A drawable tree's nodes (the key, the record, a textured node's binding
// as present or not).
Sum SumTree(char* treeObject, bool textured)
{
    Sum total;
    char* const head = At<char*>(treeObject, tree::kHead);
    if (!head)
        return total;
    for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node)) {
        Hash h;
        h.Mix(At<uint32_t>(node, kNodeKey));
        h.MixBytes(node + kNodeRecord, node + kNodeRecord + kRecordSize);
        if (textured)
            h.Mix(At<void*>(node, kNodeTexture) ? 1u : 0u);
        total.Add(h.h);
    }
    return total;
}

// An occupant's getters (never its address).
uint32_t OccupantHash(void* occupant)
{
    Hash h;
    h.Mix(Virtual<uint32_t>(occupant, kGetType));
    h.Mix(Virtual<uint32_t>(occupant, kGetHighlight));
    // The rectangle only: what the slot leaves in AL is no answer for every
    // occupant (the original's AL there follows its caller's registers: 0x33
    // in one run and 0x35 in another on Big City n0000).
    int32_t rect[4] = {0, 0, 0, 0};
    Virtual<uint8_t>(occupant, kGetBoundingCityCells, &rect[0]);
    for (const int32_t word : rect)
        h.Mix(static_cast<uint32_t>(word));
    return h.h;
}

struct Totals {
    uint32_t cells = 0, textured = 0, highlighted = 0, regions = 0;
};

// What the part keeps on one tool.
void MixPreview(Hash& hash, char* self, Totals& totals)
{
    hash.Mix(At<uint32_t>(self, field::kType));
    char* const drawable = At<char*>(self, field::kPreview);
    if (drawable) {
        const Sum cells = SumTree(drawable + preview::kCells, false);
        const Sum textured = SumTree(drawable + preview::kTextured, true);
        hash.Mix(At<uint32_t>(drawable, preview::kCells + tree::kCount));
        hash.Mix(At<uint32_t>(drawable, preview::kTextured + tree::kCount));
        cells.MixInto(hash);
        textured.MixInto(hash);
        totals.cells += cells.count;
        totals.textured += textured.count;
    } else {
        hash.Mix(0xFFFFFFFFu);
    }
    if (char* const region = At<char*>(self, kRegion)) {
        ++totals.regions;
        for (std::size_t at = 0; at < 0x10; at += 4)
            hash.Mix(At<uint32_t>(region, at));
        const uint32_t width = At<uint32_t>(region, kRegionWidth), words = At<uint32_t>(region, kRegionWords);
        hash.Mix(width);
        hash.Mix(At<uint32_t>(region, kRegionDepth));
        hash.Mix(words);
        if (uint32_t** const rows = At<uint32_t**>(region, kRegionRows))
            for (uint32_t x = 0; x < width; ++x)
                for (uint32_t w = 0; w < words; ++w)
                    hash.Mix(rows[x][w]);
    } else {
        hash.Mix(0xFFFFFFFFu);
    }
    hash.Mix(At<uint32_t>(self, kCost));
    hash.Mix(At<uint32_t>(self, kCost + 4));
    hash.Mix(At<uint32_t>(self, kSetCount));
    Sum set;
    if (char* const head = At<char*>(self, kSet))
        for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node))
            set.Add(OccupantHash(At<void*>(node, tree::kValue)));
    set.MixInto(hash);
    totals.highlighted += set.count;
    hash.Mix(At<uint32_t>(self, life::kHighlights + tree::kCount));
}

// ---- The drive -------------------------------------------------------------------

bool g_done = false;
uint32_t g_sum = 0;

struct Ran {
    uint32_t cells = 0, previews = 0, pieces = 0, found = 0, quads = 0, restored = 0, updates = 0, held = 0;
    uint32_t cleared = 0;
    bool lot = false;
};

// What one slot 59 call left, for the drive's second log line: its answer,
// the cost words, the set's count and an order-free sum and xor of its
// occupants' getters, how many of them have the highlight 5, the region's
// hash (0xFFFFFFFF none). Values only.
struct Update {
    uint32_t answer = 0, cost0 = 0, cost1 = 0, count = 0, sum = 0, xored = 0, five = 0, region = 0;
};
Update g_updates[3];
uint32_t g_updateCount = 0;
// The stages' own hashes (cells, previews, pieces, quads, highlights, the
// read-back after slot 17, the end).
uint32_t g_stages[7] = {};

void RecordUpdate(char* self, int32_t answer)
{
    if (g_updateCount >= 3)
        return;
    Update& u = g_updates[g_updateCount++];
    u.answer = static_cast<uint32_t>(answer);
    u.cost0 = At<uint32_t>(self, kCost);
    u.cost1 = At<uint32_t>(self, kCost + 4);
    u.count = At<uint32_t>(self, kSetCount);
    Sum set;
    if (char* const head = At<char*>(self, kSet))
        for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node)) {
            void* const occupant = At<void*>(node, tree::kValue);
            set.Add(OccupantHash(occupant));
            u.five += Virtual<uint32_t>(occupant, kGetHighlight) == 5;
        }
    u.sum = set.sum;
    u.xored = set.xored;
    if (char* const region = At<char*>(self, kRegion)) {
        Hash h;
        for (std::size_t at = 0; at < 0x10; at += 4)
            h.Mix(At<uint32_t>(region, at));
        const uint32_t width = At<uint32_t>(region, kRegionWidth), words = At<uint32_t>(region, kRegionWords);
        h.Mix(width);
        h.Mix(At<uint32_t>(region, kRegionDepth));
        h.Mix(words);
        if (uint32_t** const rows = At<uint32_t**>(region, kRegionRows))
            for (uint32_t x = 0; x < width; ++x)
                for (uint32_t w = 0; w < words; ++w)
                    h.Mix(rows[x][w]);
        u.region = h.h;
    } else {
        u.region = 0xFFFFFFFFu;
    }
}

// Slot 58 on a copy of each cell's info, with a solved cell from its
// occupant.
void DrivePreviews(Hash& hash, char* self, const std::vector<uint32_t>& keys, Ran& ran)
{
    for (const uint32_t key : keys) {
        char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
        if (!info)
            continue;
        alignas(4) char copy[cell::kSize];
        std::memcpy(copy, info, cell::kSize);
        At<uint8_t>(copy, cell::kInPlacement) = 1;
        void* const occupant = At<void*>(copy, cell::kOccupant);
        uint32_t piece[3] = {0, 0, key};
        piece[0] = Virtual<uint32_t>(occupant, kPieceId);
        piece[1] = Virtual<uint8_t>(occupant, kRotationAndFlip);
        Virtual<void>(self, kAddCellPreview, &copy[0], &piece[0]);
        ++ran.previews;
    }
    Totals totals;
    MixPreview(hash, self, totals);
    hash.Mix(totals.cells);
    hash.Mix(totals.textured);
    Virtual<void>(self, slot::kClearOccupantHighlights);
    MixPreview(hash, self, totals);
}

// The piece lookup for each cell and each type of its flags.
void DrivePieces(Hash& hash, char* self, const std::vector<uint32_t>& keys, Ran& ran)
{
    for (const uint32_t key : keys) {
        char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
        if (!info)
            continue;
        const int32_t xz[2] = {At<int32_t>(info, cell::kX), At<int32_t>(info, cell::kZ)};
        const uint32_t flags = At<uint32_t>(info, cell::kFlags);
        for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
            if (!(flags & Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + type * 4)))
                continue;
            uint32_t piece = 0xFFFFFFFFu;
            uint8_t turn = 0xEE, flip = 0xEE;
            const uint8_t answer = CallThis<uint8_t, const int32_t*, uint32_t, uint32_t*, uint8_t*, uint8_t*>(
                addr::cSC4NetworkTool_FindPreviewPiece, self, xz, type, &piece, &turn, &flip);
            hash.Mix(type);
            hash.Mix(answer);
            hash.Mix(piece);
            hash.Mix(turn);
            hash.Mix(flip);
            ++ran.pieces;
            ran.found += answer != 0;
        }
    }
}

// Slot 80 with each edge word, the three flags put back after.
void DriveQuads(Hash& hash, char* self, const std::vector<uint32_t>& keys, Ran& ran)
{
    for (const uint32_t key : keys) {
        char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
        if (!info)
            continue;
        void* const occupant = At<void*>(info, cell::kOccupant);
        if (!occupant)
            continue;
        const uint32_t saved = Virtual<uint32_t>(occupant, kGetNetworkFlag);
        hash.Mix(saved & 0x47000000u);
        for (const uint32_t word : kQuadWords) {
            Virtual<void>(self, kSetQuadTrianglesToDraw, occupant, &word);
            hash.Mix(Virtual<uint32_t>(occupant, kGetNetworkFlag) & 0x07000000u);
            ++ran.quads;
        }
        for (const uint32_t flag : kQuadFlags) {
            const bool now = (Virtual<uint32_t>(occupant, kGetNetworkFlag) & flag) != 0;
            const bool was = (saved & flag) != 0;
            if (was && !now)
                Virtual<void>(occupant, kSetNetworkFlag, flag);
            else if (!was && now)
                Virtual<void>(occupant, kClearNetworkFlag, flag);
        }
        const bool back = Virtual<uint32_t>(occupant, kGetNetworkFlag) == saved;
        hash.Mix(back ? 1u : 0u);
        ran.restored += back;
    }
}

// The game's memory for a vector of the tool (SC4::stl_allocate).
char* Allocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}

// Slot 59: with no valid step, then on six cells around a lot.
void DriveHighlights(Hash& hash, char* self, cISC4City* city, Ran& ran)
{
    Totals totals;
    const int32_t validWas = At<int32_t>(self, field::kValidSteps);
    At<int32_t>(self, field::kValidSteps) = 0;
    {
        const int32_t answer = Virtual<int32_t>(self, kUpdateOccupantHighlights, kBudget);
        RecordUpdate(self, answer);
        hash.Mix(static_cast<uint32_t>(answer));
    }
    ++ran.updates;
    MixPreview(hash, self, totals);
    Vector& steps = At<Vector>(self, field::kSteps);
    Vector& cells = At<Vector>(self, field::kCells);
    if (steps.begin || cells.begin) {
        log::Write("autotest: ntpv: the new tool has steps or cells; highlights on a drag not driven");
        At<int32_t>(self, field::kValidSteps) = validWas;
        return;
    }
    const uint32_t sizeX = At<uint32_t>(self, field::kCellsX), sizeZ = At<uint32_t>(self, field::kCellsZ);
    void* const lots = Virtual<void*>(city, 0x84);
    int32_t lotX = -1, lotZ = -1;
    for (uint32_t z = 0; z < sizeZ && lotX < 0; z += kStep)
        for (uint32_t x = kStep; x + kRowCells < sizeX && lotX < 0; x += kStep)
            if (lots && Virtual<void*>(lots, 0x18, static_cast<int32_t>(x), static_cast<int32_t>(z), 0u)) {
                lotX = static_cast<int32_t>(x);
                lotZ = static_cast<int32_t>(z);
            }
    hash.Mix(static_cast<uint32_t>(lotX));
    hash.Mix(static_cast<uint32_t>(lotZ));
    if (lotX < 0) {
        log::Write("autotest: ntpv: no lot found; highlights on a drag not driven");
        At<int32_t>(self, field::kValidSteps) = validWas;
        return;
    }
    ran.lot = true;
    char* const points = Allocate(kRowCells * 8);
    for (uint32_t i = 0; i < kRowCells; ++i) {
        At<int32_t>(points, i * 8) = lotX - 2 + static_cast<int32_t>(i);
        At<int32_t>(points, i * 8 + 4) = lotZ;
    }
    cells = Vector{points, points + kRowCells * 8, points + kRowCells * 8};
    char* const step = Allocate(nettool_sim::step::kSize);
    At<uint32_t>(step, 0) = 0;
    At<uint32_t>(step, 4) = kRowCells - 1;
    At<uint32_t>(step, 8) = 0;
    steps = Vector{step, step + nettool_sim::step::kSize, step + nettool_sim::step::kSize};
    At<int32_t>(self, field::kValidSteps) = 1;
    for (int pass = 0; pass < 2; ++pass) {
        const int32_t answer = Virtual<int32_t>(self, kUpdateOccupantHighlights, kBudget);
        RecordUpdate(self, answer);
        hash.Mix(static_cast<uint32_t>(answer));
        ++ran.updates;
        MixPreview(hash, self, totals);
    }
    // The set's occupants held across slot 17, their highlight read back.
    std::vector<void*> held;
    if (char* const head = At<char*>(self, kSet))
        for (char* node = At<char*>(head, tree::kLeft); node != head && held.size() < kMostHeld; node = Next(node)) {
            void* const occupant = At<void*>(node, tree::kValue);
            Virtual<uint32_t>(occupant, 0x4);
            held.push_back(occupant);
        }
    ran.held = static_cast<uint32_t>(held.size());
    Virtual<void>(self, slot::kClearCellPreviews);
    Hash back;
    MixPreview(back, self, totals);
    for (void* const occupant : held) {
        const uint32_t highlight = Virtual<uint32_t>(occupant, kGetHighlight);
        back.Mix(highlight);
        ran.cleared += highlight == 0;
        ReleaseOf(occupant);
    }
    g_stages[5] = back.h;
    hash.Mix(back.h);
    // The tool's step, cells and valid count back as they were (empty).
    CallCdecl<void, void*>(addr::SC4_Free, points);
    CallCdecl<void, void*>(addr::SC4_Free, step);
    cells = Vector{nullptr, nullptr, nullptr};
    steps = Vector{nullptr, nullptr, nullptr};
    At<int32_t>(self, field::kValidSteps) = validWas;
}

void Drive(const Day& day)
{
    if (g_done || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL") ||
        EnvInt("OPENSC4_TEST_NETTOOL_PREVIEW", 0) != 1)
        return;
    g_done = true;
    cISC4NetworkManager* const manager = Manager(day.city);
    if (!manager) {
        log::Write("autotest: ntpv: no network manager; nothing driven");
        return;
    }
    char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(kRoadType), true));
    if (!self) {
        log::Write("autotest: ntpv: no new road tool; nothing driven");
        return;
    }
    Hash hash;
    Ran ran;
    Virtual<uint32_t>(self, slot::kAddRef);
    hash.Mix(Virtual<uint8_t>(self, slot::kInit));
    const uint32_t sizeX = At<uint32_t>(self, field::kCellsX), sizeZ = At<uint32_t>(self, field::kCellsZ);
    std::vector<uint32_t> keys;
    for (uint32_t z = 0; z < sizeZ && keys.size() < kMostCells; z += kStep)
        for (uint32_t x = 0; x < sizeX && keys.size() < kMostCells; x += kStep) {
            const uint32_t key = (z << 16) + x;
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
            if (info && At<void*>(info, cell::kOccupant)) {
                keys.push_back(key);
                hash.Mix(key);
            }
        }
    ran.cells = static_cast<uint32_t>(keys.size());
    g_stages[0] = hash.h;
    Hash previews, pieces, quads, highlights, end;
    DrivePreviews(previews, self, keys, ran);
    DrivePieces(pieces, self, keys, ran);
    DriveQuads(quads, self, keys, ran);
    DriveHighlights(highlights, self, day.city, ran);
    g_stages[1] = previews.h;
    g_stages[2] = pieces.h;
    g_stages[3] = quads.h;
    g_stages[4] = highlights.h;
    for (const uint32_t stage : {previews.h, pieces.h, quads.h, highlights.h})
        hash.Mix(stage);
    Virtual<void>(self, slot::kClearOccupantHighlights);
    Virtual<void>(self, slot::kClearCellPreviews);
    Totals totals;
    MixPreview(end, self, totals);
    g_stages[6] = end.h;
    hash.Mix(end.h);
    hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
    Virtual<uint32_t>(self, slot::kRelease);
    g_sum = hash.h;
    log::Write("autotest: ntpv: drive: %u cells, %u previews, %u piece lookups (%u found), %u quad calls (%u "
               "occupants restored), %u highlight updates (lot %s), %u occupants held, %u cleared; hash %08X",
               ran.cells, ran.previews, ran.pieces, ran.found, ran.quads, ran.restored, ran.updates,
               ran.lot ? "found" : "none", ran.held, ran.cleared, g_sum);
    log::Write("autotest: ntpv: stages: cells %08X previews %08X pieces %08X quads %08X highlights %08X back %08X "
               "end %08X",
               g_stages[0], g_stages[1], g_stages[2], g_stages[3], g_stages[4], g_stages[5], g_stages[6]);
    for (uint32_t i = 0; i < g_updateCount; ++i) {
        const Update& u = g_updates[i];
        log::Write("autotest: ntpv: slot 59 call %u: answer %d cost %08X:%08X set %u sum %08X xor %08X five %u "
                   "region %08X",
                   i, static_cast<int32_t>(u.answer), u.cost1, u.cost0, u.count, u.sum, u.xored, u.five, u.region);
    }
}

void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4NetworkManager* const manager = Manager(city);
    if (!manager) {
        line += " | ntpv none";
        return;
    }
    Hash hash;
    Totals totals;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(type), false));
        if (!self) {
            hash.Mix(0xFFFFFFFFu);
            continue;
        }
        MixPreview(hash, self, totals);
    }
    char text[128];
    std::snprintf(text, sizeof text, " | ntpv p%u,%u h%u r%u %08X", totals.cells, totals.textured,
                  totals.highlighted, totals.regions, hash.h);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " set %08X", g_sum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"ntpv", 178, &Fingerprint};
const DayHook kDrive{"ntpv.drive", 200, &Drive};

} // namespace
} // namespace opensc4::autotest
