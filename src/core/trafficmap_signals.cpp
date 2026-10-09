// The traffic signals of our cSC4TrafficNetworkMap (docs/traffic-map.md):
// setting up the approaches of signalled intersections, the per-frame signal
// tick, green waves over groups of signals, the automata waiting at
// intersections and the gathered approach / turn traffic of cells near the
// view.
//
// The game hands its signal helpers the value part of a signal node (node +
// 8, the approaches); ours take the node itself.
#include "core/trafficmap.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstdint>
#include <cstring>

namespace opensc4::trafficmap {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using UnitFn = double(__thiscall*)(void* random);
using RangeFn = double(__thiscall*)(void* random, double low, double high);
using StopwatchFn = void(__thiscall*)(void* stopwatch);
using StopwatchModeFn = void*(__thiscall*)(void* stopwatch, uint32_t mode);
using ElapsedFn = void(__thiscall*)(void* stopwatch, float* elapsed);
using SetInsertFn = void(__thiscall*)(void* set, void* result, void* const* key);
using SetEraseFn = void(__thiscall*)(void* set, char* subtree);
using RebalanceFn = void*(__cdecl*)(void* node, void* root, void* leftmost, void* rightmost);
using ListClearFn = void(__thiscall*)(void* list);
using InsertNFn = void(__thiscall*)(void* vector, void* where, void* const* value, void* const* allocator, uint32_t count,
                                    uint32_t atEnd);

// Read-only constants of the exe (.rdata).
const float kSmall = std::bit_cast<float>(0x3727C5ACu);      // 0x00A8FE64, 1e-5
const float kAmberTime = std::bit_cast<float>(0x3F400000u);  // 0x00A8FDD4, 0.75
const float kMilli = std::bit_cast<float>(0x3A83126Fu);      // 0x00A867A4, 0.001
constexpr float kCellBudget = 5.0f;                          // 0x00A8FDE4
constexpr float kUpdatePeriod = 1000.0f;                     // 0x00A8FDE0
constexpr float kMaxRatio = 2.0f;                            // 0x00A8FDD8
constexpr float kPositionToCell = 0.0625f;                   // 0x00A8FDF0
constexpr double kRoundMagic = 12582912.0;                   // 0x00A8FDF4, 1.5 * 2^23
constexpr uint32_t kRoundBias = 0x4B400000u;

// A float filled into a position before an object writes it; the occupants
// part names it SC4::ZeroVector3Value.
constexpr std::uintptr_t kZeroVector3Value = 0x00B217F0;

void* City()
{
    return Global<void*>(addr::SC4_City);
}

SignalNode* FindSignal(void* self, void* occupant)
{
    return Find<SignalNode>(Table(self, field::kSignals), static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)));
}

// The game's rounding idiom: (float)(x + 1.5 * 2^23) bits - 0x4B400000,
// capped at 0xFFFF as an unsigned number; `add` is added before the cap.
uint16_t RoundCapped(double x, uint32_t add)
{
    const float rounded = static_cast<float>(x + kRoundMagic);
    uint32_t bits = 0;
    std::memcpy(&bits, &rounded, sizeof(bits));
    const uint32_t value = add + bits - kRoundBias;
    return static_cast<uint16_t>(value > 0xFFFFu ? 0xFFFFu : value);
}

void CopyFloat(float& to, const float& from)
{
    std::memcpy(&to, &from, sizeof(float));
}

void CopyFloat(float& to, std::uintptr_t global)
{
    std::memcpy(&to, reinterpret_cast<const void*>(global), sizeof(float));
}

// A slot-15 direction argument as the game pushes it: the direction in the
// low byte over the upper bytes of the stack slot it reuses (the occupant
// argument's). The game's slot 15 reads only the byte.
uint32_t DirWord(void* occupant, uint32_t dir)
{
    return (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)) & 0xFFFFFF00u) | (dir & 0xFFu);
}

// Whether the city's view slots 0xAC / 0xB0 say a view is active (slot 6 of
// each); the cell update and slot 51 ask it.
bool ViewActive()
{
    void* const a = Virtual<void*>(City(), 0xAC);
    if (Virtual<uint8_t>(a, 0x18))
        return true;
    void* const b = Virtual<void*>(City(), 0xB0);
    return Virtual<uint8_t>(b, 0x18) != 0;
}

// 0x004B19A0: releases every object of a signal's object vector and empties it.
void ReleaseObjects(GameVector& objects)
{
    for (auto** it = static_cast<void**>(objects.begin); it != objects.end; ++it)
        if (*it)
            Virtual<uint32_t>(*it, 0x8);
    objects.end = objects.begin;
}

// --- Local std::set<occupant*> (the game's red-black tree, header of 0x14 bytes:
// color, parent, left, right; nodes carry the value at +0x10). ---

struct OccupantSet {
    char* head;
    uint32_t size;
};

char* NewSetHead()
{
    auto* const head = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x14));
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
    return head;
}

void SetInsert(OccupantSet& set, void* value)
{
    char result[8];
    reinterpret_cast<SetInsertFn>(addr::SC4PtrSet_Insert)(&set, result, &value);
}

bool SetContains(const OccupantSet& set, void* value)
{
    const auto key = reinterpret_cast<std::uintptr_t>(value);
    char* found = set.head;
    for (char* node = At<char*>(set.head, 0x4); node;) {
        if (!(At<std::uintptr_t>(node, 0x10) < key)) {
            found = node;
            node = At<char*>(node, 0x8);
        } else {
            node = At<char*>(node, 0xC);
        }
    }
    return found != set.head && !(key < At<std::uintptr_t>(found, 0x10));
}

char* SetNext(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (At<char*>(right, 0x8))
            right = At<char*>(right, 0x8);
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    return At<char*>(node, 0xC) != parent ? parent : node;
}

void ResetSetHead(OccupantSet& set)
{
    At<char*>(set.head, 0x8) = set.head;
    At<char*>(set.head, 0x4) = nullptr;
    At<char*>(set.head, 0xC) = set.head;
    set.size = 0;
}

void FreeSetHead(OccupantSet& set)
{
    if (set.head)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(set.head);
}

// The set's destructor as 0x004B1110's early exit has it: _Erase on the root.
void DestroySetErase(OccupantSet& set)
{
    if (set.size) {
        reinterpret_cast<SetEraseFn>(addr::SC4OccupantSet_EraseTree)(&set, At<char*>(set.head, 0x4));
        ResetSetHead(set);
    }
    FreeSetHead(set);
}

// The set's destructor with the top of _Erase inlined (0x004B1110's other exits).
void DestroySetInline(OccupantSet& set)
{
    if (set.size) {
        for (char* node = At<char*>(set.head, 0x4); node;) {
            reinterpret_cast<SetEraseFn>(addr::SC4OccupantSet_EraseTree)(&set, At<char*>(node, 0xC));
            char* const left = At<char*>(node, 0x8);
            reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
            node = left;
        }
        ResetSetHead(set);
    }
    FreeSetHead(set);
}

// --- Helpers ---

// 0x004AE9E0: sets up approach `dirWord` (low byte 0-3) of an intersection
// from the occupant and the neighbouring occupant that way: 0 none, 1 a
// side street meeting a street (stop), 2 a signal (times by network type,
// phase from the map's blink timers), 3 a rail crossing (green while nothing
// waits). The game passes the direction as a whole stack word; slot 0x88 of
// the occupant's path object gets that word.
void BuildApproach(void* self, void* occupant, void* neighbour, SignalNode* node, Approach* approach, uint32_t dirWord)
{
    const uint32_t dir = dirWord & 0xFFu;
    const bool connects = Virtual<uint8_t>(occupant, 0xBC, dir, 0u) || Virtual<uint8_t>(occupant, 0xBC, dir, 3u) ||
                          Virtual<uint8_t>(occupant, 0xBC, dir, 1u) || Virtual<uint8_t>(occupant, 0xBC, dir, 7u) ||
                          Virtual<uint8_t>(occupant, 0xBC, dir, 8u) || Virtual<uint8_t>(occupant, 0xBC, dir, 6u);
    approach->amber = 0;
    approach->kind = 0;
    approach->green = 1;
    void* const paths = Virtual<void*>(occupant, 0xC4);
    if (Virtual<uint32_t>(paths, 0x88, 0u, dirWord, 0xFFu, 0xFFu) == 0 || !connects)
        return;
    if (Virtual<uint8_t>(occupant, 0x58, 1u) || Virtual<uint8_t>(occupant, 0x58, 8u)) {
        approach->kind = 3;
        At<uint32_t>(approach, 0xC) = 0;
        At<uint32_t>(approach, 0x8) = 0;
        approach->green = node->waiting->next == node->waiting;
        At<uint32_t>(approach, 0x10) = 0;
        return;
    }
    if (neighbour && Virtual<uint8_t>(neighbour, 0x58, 3u)) {
        approach->kind = 1;
        CopyFloat(approach->redTime, addr::SC4AutomataTuning_Signal4998C912);
        CopyFloat(approach->greenTime, addr::SC4AutomataTuning_Signal4998C913);
        approach->green = 0;
        CopyFloat(approach->timer, addr::SC4AutomataTuning_Signal4998C912);
        return;
    }
    approach->kind = 2;
    if (Virtual<uint8_t>(occupant, 0x58, 3u))
        CopyFloat(approach->redTime, addr::SC4AutomataTuning_Signal4998C913);
    else if (Virtual<uint8_t>(occupant, 0x58, 0u))
        CopyFloat(approach->redTime, addr::SC4AutomataTuning_Signal4998C914);
    else if (Virtual<uint8_t>(occupant, 0x58, 6u))
        CopyFloat(approach->redTime, addr::SC4AutomataTuning_Signal4998C915);
    if (!neighbour)
        CopyFloat(approach->greenTime, addr::SC4AutomataTuning_Signal4998C914);
    else if (Virtual<uint8_t>(neighbour, 0x58, 0u))
        CopyFloat(approach->greenTime, addr::SC4AutomataTuning_Signal4998C914);
    else if (Virtual<uint8_t>(neighbour, 0x58, 6u))
        CopyFloat(approach->greenTime, addr::SC4AutomataTuning_Signal4998C915);
    const bool across = dir == 1 || dir == 3;
    if (Virtual<uint8_t>(occupant, 0x58, 0u)) {
        const uint8_t blink = At<uint8_t>(self, field::kBlinkA);
        approach->green = across ? blink : static_cast<uint8_t>(blink == 0);
        approach->timer = static_cast<float>(static_cast<double>(Global<float>(addr::SC4AutomataTuning_Signal4998C914)) -
                                             static_cast<double>(At<float>(self, field::kBlinkTimerA)));
    } else if (Virtual<uint8_t>(occupant, 0x58, 6u)) {
        const uint8_t blink = At<uint8_t>(self, field::kBlinkB);
        approach->green = across ? blink : static_cast<uint8_t>(blink == 0);
        approach->timer = static_cast<float>(static_cast<double>(Global<float>(addr::SC4AutomataTuning_Signal4998C915)) -
                                             static_cast<double>(At<float>(self, field::kBlinkTimerB)));
    }
}

// 0x004AEC70: looks at the network cell (x, z) the way `dir` leaves a cell
// record: notes the cell's network kinds in the record's flags (+0x20, +0x24,
// +0x28) and, when the tuning masks match, its background traffic as the
// record's approach traffic for `dir`. 1 if the cell has a network piece.
uint8_t ProbeCell(void* self, int32_t x, int32_t z, uint32_t dir, CellRecord* record)
{
    if (x < 0 || x >= At<int32_t>(self, field::kWidth) || z < 0 || z >= At<int32_t>(self, field::kHeight))
        return 0;
    void* const grid = Virtual<void*>(City(), 0x84);
    void* const cell = Virtual<void*>(grid, 0x18, x, z, 0u);
    if (!cell || !Virtual<uint8_t>(cell, 0x94))
        return 0;
    void* const piece = Virtual<void*>(cell, 0xC0);
    if (!piece)
        return 0;
    const auto* const kinds = Virtual<const int32_t*>(piece, 0x2C);
    switch (kinds[1]) {
    case 1:
        record->flags24 |= 1;
        break;
    case 2:
        record->flags24 |= 2;
        break;
    case 3:
        record->flags24 |= 4;
        break;
    default:
        break;
    }
    switch (kinds[0]) {
    case 1:
        record->flags20 |= 1;
        record->flags28 |= 1;
        break;
    case 2:
        record->flags28 |= 2;
        record->flags20 |= 2;
        break;
    case 3:
        record->flags28 |= 4;
        record->flags20 |= 2;
        break;
    case 4:
        record->flags28 |= 8;
        record->flags20 |= 2;
        break;
    case 5:
        record->flags20 |= 4;
        record->flags28 |= 0x10;
        break;
    case 6:
        record->flags20 |= 4;
        record->flags28 |= 0x20;
        break;
    case 7:
        record->flags20 |= 4;
        record->flags28 |= 0x40;
        break;
    case 8:
        record->flags20 |= 4;
        record->flags28 |= 0x80;
        break;
    default:
        break;
    }
    if (kinds[0] != 0 && (record->flags28 & Global<uint32_t>(addr::SC4AutomataTuning_ApproachMaskB)) &&
        (record->flags20 & Global<uint32_t>(addr::SC4AutomataTuning_ApproachMaskA))) {
        void* const densities = Global<void*>(addr::SC4_Service43D68);
        const uint32_t type = Virtual<uint32_t>(cell, 0xA4);
        const uint32_t source = Virtual<uint32_t>(densities, 0x78, static_cast<uint32_t>(kinds[0]), type);
        const uint16_t traffic = Virtual<uint16_t>(cell, 0xE0, source);
        record->approachTraffic[dir & 0xFFu] = RoundCapped(static_cast<double>(static_cast<int32_t>(traffic)), 0);
    }
    return 1;
}

// 0x004AEE70 (recursive): adds the approach traffic of `occupant`'s cell
// (scaled by (depth - 1) / the tuned depth) to *out for the ways that do not
// lead on, then follows the first way out (turning right from `fromWord`)
// that the occupant connects, one cell less deep.
void SpreadTraffic(void* self, void* occupant, uint32_t fromWord, uint32_t depthWord, uint16_t* out)
{
    if (!occupant)
        return;
    const uint8_t depth = static_cast<uint8_t>(depthWord);
    const int32_t levels = Global<uint8_t>(addr::SC4AutomataTuning_SpreadDepth);
    const float factor =
        static_cast<float>(static_cast<double>(static_cast<int32_t>(depth) - 1) / static_cast<double>(levels));
    const uint8_t from = static_cast<uint8_t>(fromWord);
    const uint32_t back = (static_cast<uint32_t>(from) + 2) % 4;
    void* const piece = Virtual<void*>(occupant, 0xC0);
    int32_t cell[4] = {};
    Virtual<void>(piece, 0x2C, cell);
    auto* const record = Virtual<CellRecord*>(self, 0x34, cell[0], cell[1]);
    if (!record)
        return;
    for (uint32_t i = 0; i < 4; ++i) {
        if (Virtual<void*>(self, 0x3C, occupant, DirWord(occupant, i), 0u, 0u))
            continue;
        const double scaled = static_cast<double>(static_cast<int32_t>(record->approachTraffic[i])) *
                              static_cast<double>(factor);
        *out = RoundCapped(scaled, *out);
    }
    if (depth <= 1)
        return;
    const uint32_t nextDepth = (depthWord & 0xFFFFFF00u) | static_cast<uint8_t>(depth - 1);
    uint8_t way = from;
    do {
        const uint32_t wayWord = DirWord(occupant, way);
        auto** const next = Virtual<void**>(self, 0x3C, occupant, wayWord, 0u, 0u);
        if (next && Virtual<uint8_t>(occupant, 0xD4, 0u, back, wayWord)) {
            if (*next)
                SpreadTraffic(self, *next, wayWord, nextDepth, out);
            return;
        }
        way = static_cast<uint8_t>((way + 1) % 4);
    } while (way != from);
}

// 0x004AFB80: runs an intersection's signal approaches for dt seconds:
// green and red swap when the timer runs out, amber below 0.75 s; pushes
// the states to the objects if anything changed.
void TickSignal(void* /*self*/, void* occupant, SignalNode* node, float dt)
{
    for (Approach& approach : node->approaches) {
        if (approach.kind != 2)
            continue;
        const float timer = static_cast<float>(static_cast<double>(approach.timer) - static_cast<double>(dt));
        approach.timer = timer;
        if (timer <= 0.0f) {
            approach.green = approach.green == 0;
            approach.amber = 0;
            node->changed = 1;
            CopyFloat(approach.timer, approach.green ? approach.greenTime : approach.redTime);
        } else if (timer < kAmberTime) {
            if (approach.amber == 0) {
                approach.amber = 1;
                node->changed = 1;
            }
        }
    }
    if (node->changed)
        ApplySignals(occupant, node, 0);
}

// 0x004AFCE0: clears a cell record's network flags and approach traffic,
// then probes up to three cells out from each side of the occupant's
// rectangle (west, east, north, south) until one has a network piece.
void ScanApproaches(void* self, void* occupant, CellRecord* record)
{
    record->flags20 = 0;
    record->flags24 = 0;
    record->flags28 = 0;
    std::memset(record->approachTraffic, 0, sizeof(record->approachTraffic));
    void* const piece = Virtual<void*>(occupant, 0xC0);
    int32_t rect[4] = {};
    Virtual<void>(piece, 0x2C, rect);
    if (!Virtual<void*>(City(), 0x84))
        return;
    bool found = false;
    for (int32_t z = rect[1]; z <= rect[3] && !found; ++z)
        for (int32_t x = rect[0] - 1; x >= rect[0] - 3 && !found; --x)
            found = ProbeCell(self, x, z, 0, record) != 0;
    found = false;
    for (int32_t z = rect[1]; z <= rect[3] && !found; ++z)
        for (int32_t x = rect[2] + 1; x <= rect[2] + 3 && !found; ++x)
            found = ProbeCell(self, x, z, 2, record) != 0;
    found = false;
    for (int32_t x = rect[0]; x <= rect[2] && !found; ++x)
        for (int32_t z = rect[1] - 1; z >= rect[1] - 3 && !found; --z)
            found = ProbeCell(self, x, z, 1, record) != 0;
    found = false;
    for (int32_t x = rect[0]; x <= rect[2] && !found; ++x)
        for (int32_t z = rect[3] + 1; z <= rect[3] + 3 && !found; ++z)
            found = ProbeCell(self, x, z, 3, record) != 0;
}

// 0x004AFE50: gathers each approach's traffic from the cells along it
// (0x004AEE70), then splits it into turn traffic: for each pair of
// approaches a != b, traffic(a) * traffic(b) / total, times a random share
// from the tuned range.
void ComputeTurnTraffic(void* self, void* occupant, CellRecord* record)
{
    const uint32_t depth = Global<uint8_t>(addr::SC4AutomataTuning_SpreadDepth);
    for (uint32_t i = 0; i < 4; ++i) {
        // The game pushes the direction over an uninitialised stack byte's
        // neighbours; the game's slot 15 reads only the byte.
        if (auto** const next = Virtual<void**>(self, 0x3C, occupant, i, 0u, 0u))
            SpreadTraffic(self, *next, i, depth, &record->approachTraffic[i]);
    }
    std::memset(record->turnTraffic, 0, sizeof(record->turnTraffic));
    uint32_t total = 0;
    for (uint16_t traffic : record->approachTraffic)
        total += traffic;
    const float low = Global<float>(addr::SC4AutomataTuning_TurnShareRange);
    const float high = Global<float>(addr::SC4AutomataTuning_TurnShareRange + 4);
    for (uint32_t a = 0; a < 4; ++a) {
        if (record->approachTraffic[a] == 0)
            continue;
        for (uint32_t b = 0; b < 4; ++b) {
            if (b == a)
                continue;
            const uint32_t share =
                static_cast<uint32_t>(record->approachTraffic[a]) * static_cast<uint32_t>(record->approachTraffic[b]) / total;
            const float shareF = static_cast<float>(static_cast<double>(share));
            const double draw = reinterpret_cast<RangeFn>(addr::cRZRandom_Range)(
                static_cast<char*>(self) + field::kRandom, static_cast<double>(low), static_cast<double>(high));
            record->turnTraffic[a][b] = RoundCapped(draw * static_cast<double>(shareF), 0);
        }
    }
}

// 0x004AE6D0: walks the grid from the saved cell (+0xA84/+0xA88), asking
// each network occupant outside the view rectangle (all of them when no
// view is active) to refresh itself (slot 50) unless it has flag 0x80000,
// until 5 units of the stopwatch have passed or the grid ends.
void UpdateCells(void* self)
{
    int32_t& x = At<int32_t>(self, field::kUpdateX);
    int32_t& z = At<int32_t>(self, field::kUpdateZ);
    if (x < 0 || x >= At<int32_t>(self, field::kWidth))
        x = 0;
    if (z < 0 || z >= At<int32_t>(self, field::kHeight))
        z = 0;
    const bool view = ViewActive();
    alignas(8) char stopwatch[0x18];
    reinterpret_cast<StopwatchModeFn>(addr::SC4Stopwatch_Construct)(stopwatch, 4);
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Start)(stopwatch);
    const auto* const rect = &At<int32_t>(self, field::kRect);
    if (x < At<int32_t>(self, field::kWidth)) {
        while (z < At<int32_t>(self, field::kHeight)) {
            if (!view || x < rect[0] || z < rect[1] || x > rect[2] || z > rect[3]) {
                for (auto* record = Virtual<CellRecord*>(self, 0x34, x, z); record; record = record->next) {
                    if (record->occupant && !Virtual<uint8_t>(record->occupant, 0x20, 0x80000u))
                        Virtual<void>(record->occupant, 0xC8);
                }
            }
            ++x;
            if (x >= At<int32_t>(self, field::kWidth)) {
                x = 0;
                ++z;
            }
            float elapsed = 0.0f;
            reinterpret_cast<ElapsedFn>(addr::SC4Stopwatch_GetElapsed)(stopwatch, &elapsed);
            if (elapsed >= kCellBudget)
                break;
            if (x >= At<int32_t>(self, field::kWidth))
                break;
        }
    }
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Stop)(stopwatch);
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Destruct)(stopwatch);
}

// 0x004B1110: gathers the group of signalled intersections connected to
// `occupant` (through slot 15 with both flags set), weighs its two
// directions' traffic (service 0x00B43DA8 slot 12 per cell) into a green
// ratio between 0.5 and 2, and gives every signal of the group the same
// cycle and phase as the first one (a green wave).
void CoordinateSignals(void* self, void* occupant, SignalNode* /*node*/)
{
    OccupantSet open{nullptr, 0};
    open.head = NewSetHead();
    OccupantSet group{nullptr, 0};
    group.head = NewSetHead();
    OccupantSet other{nullptr, 0};
    other.head = NewSetHead();
    SetInsert(open, occupant);
    for (char* first = At<char*>(open.head, 0x8); first != open.head; first = At<char*>(open.head, 0x8)) {
        void* const current = At<void*>(first, 0x10);
        bool signalled = false;
        if (SignalNode* const signal = FindSignal(self, current)) {
            for (const Approach& approach : signal->approaches)
                signalled = signalled || approach.kind == 2;
        }
        if (!signalled) {
            SetInsert(other, current);
        } else {
            SetInsert(group, current);
            for (uint32_t i = 0; i < 4; ++i) {
                auto** const link = Virtual<void**>(self, 0x3C, current, i, 1u, 1u);
                if (!link)
                    continue;
                void* const next = *link;
                if (!FindSignal(self, next))
                    continue;
                if (SetContains(group, next) || SetContains(other, next))
                    continue;
                SetInsert(open, next);
            }
        }
        if (void* const erased = reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(
                first, open.head + 0x4, open.head + 0x8, open.head + 0xC))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(erased);
        --open.size;
    }
    if (group.size == 0) {
        DestroySetErase(other);
        DestroySetErase(group);
        DestroySetErase(open);
        return;
    }
    float ratio = 1.0f;
    if (At<char*>(group.head, 0x8) != group.head) {
        int32_t sumA = 0; // cell traffic one way (words 0-3, 8-11)
        int32_t sumB = 0; // the other way (words 4-7, 12-15)
        for (char* it = At<char*>(group.head, 0x8); it != group.head; it = SetNext(it)) {
            void* const member = At<void*>(it, 0x10);
            void* const service = Global<void*>(addr::SC4_Service43DA8);
            int32_t x = 0, z = 0;
            Virtual<void>(member, 0xEC, &x, &z);
            const auto* const traffic = Virtual<const uint16_t*>(service, 0x30, x, z, 1u, 0u);
            sumA += traffic[0] + traffic[1] + traffic[2] + traffic[3] + traffic[8] + traffic[9] + traffic[10] + traffic[11];
            sumB += traffic[4] + traffic[5] + traffic[6] + traffic[7] + traffic[12] + traffic[13] + traffic[14] +
                      traffic[15];
        }
        const float minimum = Global<float>(addr::SC4_FloatHalfB09584);
        if (sumA > 0) {
            if (sumB > 0) {
                const float q = static_cast<float>(static_cast<double>(sumB) / static_cast<double>(sumA));
                float v = kMaxRatio < q ? kMaxRatio : q;
                v = v < minimum ? minimum : v;
                ratio = v;
            } else {
                ratio = minimum;
            }
        } else {
            ratio = sumB > 0 ? 2.0f : 1.0f;
        }
    }
    float base = Global<float>(addr::SC4AutomataTuning_Signal4998C913);
    if (Virtual<uint8_t>(occupant, 0x58, 0u))
        base = Global<float>(addr::SC4AutomataTuning_Signal4998C914);
    else if (Virtual<uint8_t>(occupant, 0x58, 6u))
        base = Global<float>(addr::SC4AutomataTuning_Signal4998C915);
    const double cycle = static_cast<double>(base) + static_cast<double>(base);
    const float greenA = static_cast<float>(cycle / (static_cast<double>(ratio) + 1.0));
    const float greenB = static_cast<float>(cycle - static_cast<double>(greenA));
    bool phaseKnown = false;
    uint8_t phase = 0;
    float timer = 0.0f;
    for (char* it = At<char*>(group.head, 0x8); it != group.head; it = SetNext(it)) {
        void* const member = At<void*>(it, 0x10);
        SignalNode* const signal = FindSignal(self, member);
        if (!signal)
            continue;
        if (!phaseKnown) {
            for (int i = 0; i < 4 && !phaseKnown; ++i) {
                const Approach& approach = signal->approaches[i];
                if (approach.kind != 2)
                    continue;
                CopyFloat(timer, approach.timer);
                phase = approach.green;
                if (i == 0 || i == 2)
                    phase = phase == 0;
                phaseKnown = true;
            }
            if (!phaseKnown)
                break;
        }
        bool changed = false;
        for (int i = 0; i < 4; ++i) {
            Approach& approach = signal->approaches[i];
            if (approach.kind != 2)
                continue;
            if (i & 1) {
                approach.greenTime = greenB;
                approach.redTime = greenA;
                CopyFloat(approach.timer, timer);
                if (approach.green != phase) {
                    approach.green = phase;
                    changed = true;
                }
            } else {
                approach.greenTime = greenA;
                approach.redTime = greenB;
                CopyFloat(approach.timer, timer);
                if (approach.green == phase) {
                    approach.green = phase == 0;
                    changed = true;
                }
            }
        }
        if (changed)
            ApplySignals(member, signal, 0);
    }
    DestroySetInline(other);
    DestroySetInline(group);
    DestroySetInline(open);
}

// 0x004B2830: rebuilds the object lists of every intersection.
void RefreshAllSignals(void* self)
{
    Virtual<void*>(City(), 0x8C);
    Hashtable& table = Table(self, field::kSignals);
    for (auto* node = First<SignalNode>(table); node;) {
        RefreshSignalObjects(self, reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)), node);
        node = Next(table, node);
    }
}

// --- Slots ---

// 5 (0x004B1090): sets up every intersection's approaches.
uint8_t __fastcall SetUpAllSignals(void* self, void*)
{
    static bool logged = false;
    FirstCall("SetUpAllSignals", logged);
    Hashtable& table = Table(self, field::kSignals);
    for (auto* node = First<SignalNode>(table); node;) {
        SetUpSignal(self, reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)), node);
        node = Next(table, node);
    }
    return 1;
}

// 6 (0x004AE200): orders four weighted choices at random (weights above
// 1e-5 drawn by weight, the rest evenly; order[k] = the k-th pick, untouched
// when none was picked); returns the number of weighted ones.
uint8_t __fastcall PickApproaches(void* self, void*, const float* weights, uint8_t* order)
{
    static bool logged = false;
    FirstCall("PickApproaches", logged);
    float left[4];
    float sum = 0.0f;
    uint8_t weighted = 0;
    for (int i = 0; i < 4; ++i) {
        sum = static_cast<float>(static_cast<double>(sum) + static_cast<double>(weights[i]));
        CopyFloat(left[i], weights[i]);
        if (weights[i] > kSmall)
            ++weighted;
    }
    uint8_t unweighted = static_cast<uint8_t>(4 - weighted);
    void* const random = static_cast<char*>(self) + field::kRandom;
    for (int k = 0; k < 4; ++k, ++order) {
        float remaining = sum;
        uint8_t remainingUnweighted = unweighted;
        for (uint8_t j = 0; j < 4; ++j) {
            if (!(left[j] >= 0.0f))
                continue;
            double chance;
            if (remaining > kSmall)
                chance = static_cast<double>(left[j]) / static_cast<double>(remaining);
            else
                chance = 1.0 / static_cast<double>(static_cast<int32_t>(remainingUnweighted));
            if (left[j] > kSmall)
                remaining = static_cast<float>(static_cast<double>(remaining) - static_cast<double>(left[j]));
            else
                --remainingUnweighted;
            const double draw = reinterpret_cast<UnitFn>(addr::SC4Random_Unit)(random);
            if (!(draw <= chance))
                continue;
            if (left[j] > kSmall)
                sum = static_cast<float>(static_cast<double>(sum) - static_cast<double>(left[j]));
            else
                --unweighted;
            *order = j;
            left[j] = -1.0f;
            break;
        }
    }
    return weighted;
}

// 32 (0x004B2F70): the per-frame signal tick (dt in ms): advances the two
// blink timers, rebuilds the object lists if signals changed, runs the
// signals in the view rectangle and coordinates the signals of up to four
// non-empty cells of a walk over the grid (64 cells at most).
void __fastcall TickSignals(void* self, void*, float dt)
{
    static bool logged = false;
    FirstCall("TickSignals", logged);
    const double seconds = static_cast<double>(dt) * static_cast<double>(kMilli);
    const float secondsF = static_cast<float>(seconds);
    const double blinkA = seconds + static_cast<double>(At<float>(self, field::kBlinkTimerA));
    At<float>(self, field::kBlinkTimerA) = static_cast<float>(blinkA);
    if (blinkA >= static_cast<double>(Global<float>(addr::SC4AutomataTuning_Signal4998C914))) {
        const uint8_t on = At<uint8_t>(self, field::kBlinkA) == 0;
        At<uint32_t>(self, field::kBlinkTimerA) = 0;
        At<uint8_t>(self, field::kBlinkA) = on;
    }
    const double blinkB = static_cast<double>(secondsF) + static_cast<double>(At<float>(self, field::kBlinkTimerB));
    At<float>(self, field::kBlinkTimerB) = static_cast<float>(blinkB);
    if (blinkB >= static_cast<double>(Global<float>(addr::SC4AutomataTuning_Signal4998C915))) {
        const uint8_t on = At<uint8_t>(self, field::kBlinkB) == 0;
        At<uint32_t>(self, field::kBlinkTimerB) = 0;
        At<uint8_t>(self, field::kBlinkB) = on;
    }
    if (At<uint8_t>(self, field::kSignalsChanged)) {
        RefreshAllSignals(self);
        At<uint8_t>(self, field::kSignalsChanged) = 0;
    }
    const auto* const rect = &At<int32_t>(self, field::kRect);
    for (int32_t z = rect[1]; z <= rect[3]; ++z) {
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            for (auto* record = Virtual<CellRecord*>(self, 0x34, x, z); record; record = record->next) {
                if (!record->occupant)
                    continue;
                if (SignalNode* const node = FindSignal(self, record->occupant))
                    TickSignal(self, record->occupant, node, secondsF);
            }
        }
    }
    int32_t& walkX = At<int32_t>(self, field::kWalkX);
    int32_t& walkZ = At<int32_t>(self, field::kWalkZ);
    int done = 0;
    for (int tries = 0; tries < 0x40 && done < 4; ++tries) {
        if (++walkX >= At<int32_t>(self, field::kWidth)) {
            walkX = 0;
            if (++walkZ >= At<int32_t>(self, field::kHeight))
                walkZ = 0;
        }
        auto* record = Virtual<CellRecord*>(self, 0x34, walkX, walkZ);
        if (!record)
            continue;
        for (; record; record = record->next) {
            if (!record->occupant)
                continue;
            if (SignalNode* const node = FindSignal(self, record->occupant))
                CoordinateSignals(self, record->occupant, node);
        }
        ++done;
    }
}

// 33 (0x004B06F0): the signal an automaton coming from direction `dirWord`
// (low byte) meets at `occupant` (in the view rectangle only): 0 go, 1/2 the
// approach's kind when red (stop / signal), 3 a rail crossing someone else
// waits at first. *remaining = the time until the signal changes (FLT_MAX
// when not a signal).
uint32_t __fastcall GetSignalState(void* self, void*, void* occupant, void* automaton, uint32_t dirWord, float* remaining)
{
    static bool logged = false;
    FirstCall("GetSignalState", logged);
    At<uint32_t>(remaining, 0) = 0x7F7FFFFFu;
    if (!occupant)
        return 0;
    int32_t x = 0, z = 0;
    Virtual<void>(occupant, 0xEC, &x, &z);
    const auto* const rect = &At<int32_t>(self, field::kRect);
    if (x < rect[0] || z < rect[1] || x > rect[2] || z > rect[3])
        return 0;
    const uint8_t dir = static_cast<uint8_t>(dirWord);
    if (dir >= 4)
        return 0;
    auto** const link = Virtual<void**>(self, 0x3C, occupant, dirWord, 0u, 0u);
    if (!link)
        return 0;
    SignalNode* const node = FindSignal(self, *link);
    if (!node)
        return 0;
    Approach& approach = node->approaches[(dir + 2) % 4];
    const int32_t kind = approach.kind;
    if (kind == 0)
        return 0;
    uint32_t state = 0;
    if (kind == 3) {
        ListNode* const waiting = node->waiting;
        if (waiting->next != waiting)
            state = automaton == waiting->next->value ? 0 : 3;
    } else if (!approach.green) {
        state = static_cast<uint32_t>(kind);
    }
    if (kind == 2) {
        const float until = approach.green ? approach.greenTime : approach.redTime;
        *remaining = static_cast<float>(static_cast<double>(until) - static_cast<double>(approach.timer));
    }
    return state;
}

// 36 (0x004B27E0): rebuilds one intersection's object list.
void __fastcall RefreshSignal(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("RefreshSignal", logged);
    if (SignalNode* const node = FindSignal(self, occupant))
        RefreshSignalObjects(self, occupant, node);
}

// 37 (0x004B0A00): switches the signals on the cell under an object: each
// signal approach swaps green and red (timer restarted), rail crossings go
// red for 5 s.
void __fastcall ToggleSignalsAt(void* self, void*, void* object)
{
    static bool logged = false;
    FirstCall("ToggleSignalsAt", logged);
    if (!object)
        return;
    float position[3];
    CopyFloat(position[1], kZeroVector3Value);
    CopyFloat(position[0], kZeroVector3Value);
    CopyFloat(position[2], kZeroVector3Value);
    void* const located = Virtual<void*>(object, 0xC);
    Virtual<void>(located, 0x20, position);
    const auto x = static_cast<int32_t>(static_cast<double>(position[0]) * static_cast<double>(kPositionToCell));
    const auto z = static_cast<int32_t>(static_cast<double>(position[2]) * static_cast<double>(kPositionToCell));
    for (auto* record = Virtual<CellRecord*>(self, 0x38, x, z); record; record = record->next) {
        if (!record->occupant)
            continue;
        SignalNode* const node = FindSignal(self, record->occupant);
        if (!node)
            continue;
        bool changed = false;
        for (Approach& approach : node->approaches) {
            if (approach.kind == 2) {
                approach.green = approach.green == 0;
                CopyFloat(approach.timer, approach.green ? approach.greenTime : approach.redTime);
                changed = true;
            } else if (approach.kind == 3) {
                approach.green = 0;
                At<uint32_t>(&approach.timer, 0) = 0x40A00000u;
                changed = true;
            }
        }
        if (changed)
            ApplySignals(record->occupant, node, 0);
    }
}

// 38 (0x004B19F0): an automaton starts waiting at the intersections on cell
// (x, z): it joins each one's waiting list (at the front when its slot 59
// says 0x10, else at the back; referenced); the first one waiting turns all
// approaches red.
void __fastcall AddWaiting(void* self, void*, void* automaton, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("AddWaiting", logged);
    const auto allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
    for (auto* record = Virtual<CellRecord*>(self, 0x38, x, z); record; record = record->next) {
        SignalNode* const node = FindSignal(self, record->occupant);
        if (!node)
            continue;
        ListNode* const waiting = node->waiting;
        ListNode* entry = waiting->next;
        while (entry != waiting && entry->value != automaton)
            entry = entry->next;
        if (entry != waiting)
            continue;
        bool first;
        if (Virtual<uint32_t>(automaton, 0xEC) == 0x10) {
            ListNode* const head = waiting->next;
            auto* const added = static_cast<ListNode*>(allocate(0xC));
            added->value = automaton;
            added->next = head;
            added->prev = head->prev;
            head->prev->next = added;
            head->prev = added;
            first = true;
        } else {
            auto* const added = static_cast<ListNode*>(allocate(0xC));
            added->value = automaton;
            added->next = waiting;
            added->prev = waiting->prev;
            waiting->prev->next = added;
            waiting->prev = added;
            uint32_t count = 0;
            for (ListNode* it = waiting->next; it != waiting; it = it->next)
                ++count;
            first = count == 1;
        }
        Virtual<uint32_t>(automaton, 0x4);
        if (!first)
            continue;
        for (Approach& approach : node->approaches) {
            if (approach.green)
                node->changed = 1;
            approach.green = 0;
        }
        if (node->changed)
            ApplySignals(reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)), node, 0);
    }
}

// 39 (0x004B0840): an automaton stops waiting at the intersections on cell
// (x, z) (unlinked, released); when nobody waits, all approaches go green.
void __fastcall RemoveWaiting(void* self, void*, void* automaton, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("RemoveWaiting", logged);
    for (auto* record = Virtual<CellRecord*>(self, 0x38, x, z); record; record = record->next) {
        SignalNode* const node = FindSignal(self, record->occupant);
        if (!node)
            continue;
        ListNode* const waiting = node->waiting;
        ListNode* entry = waiting->next;
        while (entry != waiting && entry->value != automaton)
            entry = entry->next;
        if (entry != waiting) {
            entry->prev->next = entry->next;
            entry->next->prev = entry->prev;
            reinterpret_cast<FreeFn>(addr::SC4_Free)(entry);
            Virtual<uint32_t>(automaton, 0x8);
        }
        if (node->waiting->next != node->waiting)
            continue;
        for (Approach& approach : node->approaches) {
            if (!approach.green)
                node->changed = 1;
            approach.green = 1;
        }
        if (node->changed)
            ApplySignals(reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)), node, 0);
    }
}

// 46 (0x004B04F0): the view moved: the cell rectangle around it (city slot
// 95 on the view's two corner points, 5 cells of margin, clamped) and the
// intersections on cells that newly came into it set up again.
uint8_t __fastcall UpdateViewRect(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateViewRect", logged);
    auto* const rect = &At<int32_t>(self, field::kRect);
    const int32_t old[4] = {rect[0], rect[1], rect[2], rect[3]};
    const auto* const corners = reinterpret_cast<const uint32_t*>(At<char*>(self, field::kView) + 0x3EC);
    const uint32_t c0 = corners[0], c1 = corners[1], c2 = corners[2], c3 = corners[3];
    int32_t cellX = 0, cellZ = 0;
    Virtual<void>(City(), 0x17C, c0, c1, &cellX, &cellZ);
    rect[1] = cellZ;
    rect[0] = cellX;
    Virtual<void>(City(), 0x17C, c2, c3, &cellX, &cellZ);
    rect[2] = cellX;
    rect[3] = cellZ;
    rect[1] -= 5;
    rect[0] -= 5;
    rect[2] += 5;
    rect[3] += 5;
    const auto clamp = [](int32_t value, int32_t high) { return value < 0 ? 0 : (value > high ? high : value); };
    const int32_t lastX = At<int32_t>(self, field::kWidth) - 1;
    rect[0] = clamp(rect[0], lastX);
    rect[2] = clamp(rect[2], lastX);
    const int32_t lastZ = At<int32_t>(self, field::kHeight) - 1;
    rect[1] = clamp(rect[1], lastZ);
    rect[3] = clamp(rect[3], lastZ);
    for (int32_t z = rect[1]; z <= rect[3]; ++z) {
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            if (x >= old[0] && z >= old[1] && x <= old[2] && z <= old[3])
                continue;
            for (auto* record = Virtual<CellRecord*>(self, 0x34, x, z); record; record = record->next) {
                if (!record->occupant)
                    continue;
                if (SignalNode* const node = FindSignal(self, record->occupant))
                    SetUpSignal(self, record->occupant, node);
            }
        }
    }
    return 1;
}

// 51 (0x004B01C0): the update tick (dt in ms): the signal tick while a view
// is active; the cells of a changed view rectangle get their approach and
// turn traffic again; every 1000 ms the cell update (0x004AE6D0).
void __fastcall Update(void* self, void*, uint32_t /*unused*/, float dt)
{
    static bool logged = false;
    FirstCall("Update", logged);
    if (ViewActive())
        Virtual<void>(self, 0x80, dt);
    if (At<uint8_t>(self, field::kRectDirty)) {
        At<uint8_t>(self, field::kRectDirty) = 0;
        const auto* const rect = &At<int32_t>(self, field::kRect);
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            for (int32_t z = rect[1]; z <= rect[3]; ++z) {
                for (auto* record = Virtual<CellRecord*>(self, 0x34, x, z); record; record = record->next) {
                    if (!record->occupant)
                        continue;
                    ScanApproaches(self, record->occupant, record);
                    ComputeTurnTraffic(self, record->occupant, record);
                }
            }
        }
    }
    const double timer = static_cast<double>(dt) + static_cast<double>(At<float>(self, field::kUpdateTimer));
    At<float>(self, field::kUpdateTimer) = static_cast<float>(timer);
    if (timer >= static_cast<double>(kUpdatePeriod)) {
        At<uint32_t>(self, field::kUpdateTimer) = 0;
        UpdateCells(self);
    }
}

const Slot kSlots[] = {
    OPENSC4_TM_SLOT(5, 0x004B1090, SetUpAllSignals),  OPENSC4_TM_SLOT(6, 0x004AE200, PickApproaches),
    OPENSC4_TM_SLOT(32, 0x004B2F70, TickSignals),     OPENSC4_TM_SLOT(33, 0x004B06F0, GetSignalState),
    OPENSC4_TM_SLOT(36, 0x004B27E0, RefreshSignal),   OPENSC4_TM_SLOT(37, 0x004B0A00, ToggleSignalsAt),
    OPENSC4_TM_SLOT(38, 0x004B19F0, AddWaiting),      OPENSC4_TM_SLOT(39, 0x004B0840, RemoveWaiting),
    OPENSC4_TM_SLOT(46, 0x004B04F0, UpdateViewRect),  OPENSC4_TM_SLOT(51, 0x004B01C0, Update),
};

bool ApplySignalSlots(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    return PatchSlots("simulators.trafficmap.signals", addr::cSC4TrafficNetworkMap_vftable, 52, kSlots,
                      sizeof(kSlots) / sizeof(kSlots[0]));
}

const bool g_registeredSignals = registry::Register({
    "simulators.trafficmap.signals",
    "simulators/trafficmap",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficNetworkMap_vftable,
    "Our cSC4TrafficNetworkMap traffic signals: setup, per-frame tick, green waves, waiting automata, "
    "approach and turn traffic of cells near the view (slots 5, 6, 32, 33, 36-39, 46, 51; docs/traffic-map.md)",
    false,
    ApplySignalSlots,
});

} // namespace

// 0x004AE850: pushes an intersection's approach states to its objects
// (signal props get 0 green / 1 amber / 2 red for the approach they face;
// crossing gates 1-2 or 0/3 by `flag` and whether a crossing is closed),
// then clears the node's changed flag. The game passes node + 8 and does not
// use the occupant or ECX.
void ApplySignals(void* /*occupant*/, SignalNode* node, uint8_t flag)
{
    Approach* const approaches = node->approaches;
    bool crossingClosed = false;
    for (int i = 0; i < 4; ++i) {
        if (approaches[i].kind == 3 && !approaches[i].green) {
            crossingClosed = true;
            break;
        }
    }
    Virtual<void*>(City(), 0x88);
    auto** it = static_cast<void**>(node->objects.begin);
    if (it != node->objects.end) {
        do {
            void* const object = *it;
            void* const occupant = Virtual<void*>(object, 0xC);
            void* const kind = Virtual<void*>(occupant, 0x18);
            if (Virtual<uint8_t>(occupant, 0x58, 0x5002u) && Virtual<uint8_t>(kind, 0xC, 0x4A70D491u)) {
                void* const again = Virtual<void*>(object, 0xC);
                int32_t facing;
                if (Virtual<uint8_t>(again, 0x80, 0x80u))
                    facing = Virtual<int32_t>(object, 0x24);
                else
                    facing = (Virtual<int32_t>(object, 0x24) + 2) % 4;
                // The game indexes with the low byte, unchecked.
                auto* const approach = reinterpret_cast<Approach*>(reinterpret_cast<char*>(approaches) +
                                                                   static_cast<uint8_t>(facing) * sizeof(Approach));
                if (approach->kind == 2) {
                    uint32_t state;
                    if (!approach->green)
                        state = 2;
                    else
                        state = approach->timer <= kAmberTime ? 1u : 0u;
                    Virtual<void>(object, 0x2C, state);
                }
            } else {
                void* const again = Virtual<void*>(object, 0xC);
                if (Virtual<uint8_t>(again, 0x58, 0x5003u)) {
                    const int32_t current = Virtual<int32_t>(object, 0x30);
                    if (!crossingClosed) {
                        if (current != 2 && current != 1)
                            Virtual<void>(object, 0x2C, flag ? 2u : 1u);
                    } else {
                        if (current != 0 && current != 3)
                            Virtual<void>(object, 0x2C, flag ? 0u : 3u);
                    }
                }
            }
            ++it;
        } while (it != node->objects.end);
    }
    node->changed = 0;
}

// 0x004AFC20: sets up a new intersection's four approaches from the
// occupant and its neighbours (slot 15), pushes the states and marks the
// signals changed. The game's third argument (the table, part of the
// iterator it gets) is unused.
void SetUpSignal(void* self, void* occupant, SignalNode* node)
{
    void* neighbours[4] = {};
    if (!Virtual<void*>(occupant, 0xC4))
        return;
    node->byte58 = 0;
    node->changed = 1;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t dirWord = DirWord(occupant, i);
        neighbours[i] = nullptr;
        if (auto** const link = Virtual<void**>(self, 0x3C, occupant, dirWord, 0u, 0u))
            neighbours[i] = *link;
        BuildApproach(self, occupant, neighbours[i], node, &node->approaches[i], dirWord);
    }
    ApplySignals(occupant, node, 1);
    At<uint8_t>(self, field::kSignalsChanged) = 1;
}

// 0x004B2640: rebuilds an intersection's list of objects: releases the old
// ones, asks the network service for the occupants on the occupant's cell
// (filter B at +0xA50) and keeps those with interface 0xE9793A65.
void RefreshSignalObjects(void* self, void* occupant, SignalNode* node)
{
    auto* const list = static_cast<ListNode*>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    list->next = list;
    list->prev = list;
    void* listVar = list;
    int32_t x = 0, z = 0;
    Virtual<void>(occupant, 0xEC, &x, &z);
    void* const network = Virtual<void*>(City(), 0x88);
    GameVector& objects = node->objects;
    ReleaseObjects(objects);
    int32_t cell[2] = {x, z};
    Virtual<uint8_t>(network, 0xA0, &listVar, cell, cell, At<void*>(self, field::kFilterB), 0xFFFFFFFFu);
    for (auto* entry = static_cast<ListNode*>(listVar)->next; entry != listVar; entry = entry->next) {
        void* found = nullptr;
        if (!Virtual<uint8_t>(entry->value, 0x0, 0xE9793A65u, &found))
            continue;
        if (objects.end != objects.capacity) {
            if (objects.end)
                *static_cast<void**>(objects.end) = found;
            objects.end = static_cast<void**>(objects.end) + 1;
        } else {
            reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN)(&objects, objects.end, &found, &found, 1, 1);
        }
    }
    Virtual<void>(network, 0xC0, &listVar);
    reinterpret_cast<ListClearFn>(addr::SC4List_Clear2)(&listVar);
    Virtual<void>(ListNodeAllocator(), 0x10, listVar);
}

} // namespace opensc4::trafficmap
