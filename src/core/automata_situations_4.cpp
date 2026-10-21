// Our city situation manager, part c4 (build 641): the targets of a running
// situation and the situation's smaller steps. The class map is in
// src/core/automata_situations.h and the survey in docs/automata.md, "Part
// c". Calls between functions of the part, and to the other parts, go to the
// game's addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.situations.targets (15): kTargets below.
//   simulators.automata.situations.steps (15): kSteps.
//
// What it does.
//   - The target update (0x0052C8F0, each frame while a situation runs): with
//     the situation's flag 2 the escort check (0x0052BD30: an occupant of
//     the type 0x34 the automaton controls, within the situation's radius,
//     for its slot 21's seconds); at the status 3 without a check_success
//     script the kinds 1 (the distance to the target, 0x0052BC50), 2 (the
//     city's edge, 0x00528C70) and 5 (the cells, 0x00529B80); else the
//     check_success script (+0xAE4) answers the status; then the
//     situation's time (+0xA8C) and the time limit (slot 13; 0x00524CF0).
//     Each sets the status (0x00524C40, the drive's time +0xA88 for 1 and 2)
//     and answers it.
//   - The targets: 0x0052C0E0 (a target reached), 0x0052BBC0 (the next of
//     the situation's targets, 0x0052AD00 of part c5), 0x00524660 (the
//     overlay's intensity from a distance), 0x00529560 (the target placed on
//     the cell +0xAD8), 0x00526960 (the nearest cell of the set +0xAA8), and
//     the cell set's count, erase and bucket walk (0x00525A50, 0x00526180,
//     0x00525AA0).
//   - The steps: the scripts' references loaded and dropped (0x00525800,
//     0x005254D0), the active automaton set (0x00525070), the end of a
//     situation (slot 38, 0x005294C0), the target IDs (0x00529AF0), a script
//     called with IDs for an object (0x00524F00), a cell filter (0x00524E90),
//     the two occupant filters' slot 4 (0x00524AB0 and 0x00526650), and the
//     cell set and map helpers (0x005278E0, 0x005260C0, 0x00526510,
//     0x00526250, 0x005271D0, 0x005278A0).
//
// Oddities kept: 0x00529560 asks 0x00526960 for the nearest cell and drops
// it (the target goes to +0xAD8 itself); 0x00524F00 answers its object after
// releasing it; 0x00528C70 counts squared cell distances in unsigned 32 bits;
// 0x00526960 squares 16-bit differences as signed 32-bit sums; the escort
// check's and the kind 1's clocks are compared unrounded and kept rounded.
//
// Floats: the x87's 53-bit precision control, so the doubles here are its
// register; stores round to float where the game's do.
//
// Still the game's, at their addresses: 0x0052AD00 and 0x00528580 (part
// c5, the latter an SC4UIScale probe site), 0x00524D80 (part c2), the hash
// set and vector helpers, the overlay manager's 0x0046BF00, the script
// context.
#include "core/automata_situations.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::automata_situations {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename T>
inline T Global(std::uintptr_t address)
{
    return *reinterpret_cast<const T*>(address);
}

// A virtual call through the object's first vtable, `byteOffset` in bytes.
template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("automata situations: first %s", name);
}

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

void AddRef(void* object) { Virtual<std::uint32_t>(object, slot::kAddRef); }
void Release(void* object) { Virtual<std::uint32_t>(object, slot::kRelease); }

// A referenced field: zeroed, then the old object released.
void Drop(void* base, std::size_t offset)
{
    if (void* const object = At<void*>(base, offset)) {
        At<void*>(base, offset) = nullptr;
        Release(object);
    }
}

// (end - begin) >> 2, an arithmetic shift as the asm's SAR, compared
// unsigned.
std::uint32_t Count(const void* begin, const void* end)
{
    const std::int32_t bytes =
        static_cast<std::int32_t>(static_cast<const char*>(end) - static_cast<const char*>(begin));
    return static_cast<std::uint32_t>(bytes >> 2);
}

std::uint32_t BucketCount(char* table) { return Count(At<void*>(table, map::kBegin), At<void*>(table, map::kEnd)); }
char** Buckets(char* table) { return At<char**>(table, map::kBegin); }

std::uint32_t ZeroBits() { return Global<std::uint32_t>(addr::cSC4CitySituationManager_ZeroFloat); }

// Three floats from the game's zero (0x00B227E0), as the functions here
// start their positions.
struct Point {
    float v[3];
};

Point ZeroPoint()
{
    const std::uint32_t zero = ZeroBits();
    const std::uint32_t bits[3] = {zero, zero, zero};
    Point p;
    std::memcpy(p.v, bits, sizeof p.v);
    return p;
}

float FloatAt(std::uintptr_t address) { return Global<float>(address); }

// A cell key: the two uint16 halves of the word at a node's +4, x low.
struct Cell {
    std::uint16_t x;
    std::uint16_t z;
};

std::uint32_t Key(const Cell& cell)
{
    return static_cast<std::uint32_t>(cell.x) | (static_cast<std::uint32_t>(cell.z) << 16);
}

bool Same(char* node, const Cell& cell)
{
    return At<std::uint16_t>(node, 4) == cell.x && At<std::uint16_t>(node, 6) == cell.z;
}

// A hash map iterator, as SC4HashTable::Begin (0x004B0390) writes it.
struct Iterator {
    char* node;
    char* table;
};

Iterator Begin(char* table)
{
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, table, static_cast<void*>(&it));
    return it;
}

// What an insert answers: the node, the table, whether it is new.
struct InsertResult {
    char* node;
    char* table;
    std::uint8_t inserted;
    std::uint8_t pad[3];
};

// __ftol2's low 32 bits: truncation toward zero; what does not fit the
// 64-bit integer is the x87's 0x8000000000000000, low half 0.
std::int32_t Ftol2(double value)
{
    if (!(std::fabs(value) < 9223372036854775808.0))
        return 0;
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value));
}

void* Situation(char* self) { return At<void*>(self, field::kSituation); }
void* Automaton(char* self) { return At<void*>(self, field::kAuto); }
void* Script(char* self) { return At<void*>(self, field::kScriptContext); }

void SetStatusAt(char* self, std::uint32_t status)
{
    CallThis<void>(addr::cSC4CitySituationManager_SetStatus, self, status);
}

// The overlay's intensity at +0xA30 from a distance (0x00524660 rounded to
// a float, 0x0046BF00 with 1), when the overlay manager and the overlay are
// set.
void ShowIntensity(char* self, float distance)
{
    void* const overlay = At<void*>(self, field::kOverlay);
    if (!overlay)
        return;
    const std::uint32_t id = At<std::uint32_t>(self, field::kZeroA30);
    if (!id)
        return;
    const float intensity =
        static_cast<float>(CallThis<double>(addr::cSC4CitySituationManager_TargetIntensity, self, distance));
    CallThis<void>(addr::cSC4OverlayManager_SetIntensity, overlay, id, static_cast<std::uint32_t>(1), intensity);
}

// ====================================================================================
// The targets

// 0x00524C40 (ret 4): a new status (+0xA48). With a situation, for the
// statuses 1 and 2 the drive's time (+0xA88): the situation's slot 11, at
// least 1.0, when the automaton has the flag 0x8000000, else 1.0.
void __fastcall SetStatus(char* self, void*, std::uint32_t status)
{
    static bool logged;
    FirstCall("SetStatus", logged);
    if (status == At<std::uint32_t>(self, field::kStatus))
        return;
    At<std::uint32_t>(self, field::kStatus) = status;
    if (!Situation(self))
        return;
    if (status != 1 && status != 2)
        return;
    if (void* const automaton = Automaton(self))
        if (Virtual<std::uint8_t>(automaton, 0x7C, 0x8000000u)) {
            const double time = Virtual<double>(Situation(self), 0x2C);
            const double one = static_cast<double>(FloatAt(addr::cSC4CitySituationManager_One));
            At<float>(self, field::kDriveTime) = static_cast<float>(time > one ? time : one);
            return;
        }
    At<std::uint32_t>(self, field::kDriveTime) = 0x3F800000u;
}

// 0x00524CF0: the time is up: the status 2 with a situation, else as it is;
// set (0x00524C40) and answered.
std::uint32_t __fastcall TimeUp(char* self, void*)
{
    static bool logged;
    FirstCall("TimeUp", logged);
    std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    if (Situation(self))
        status = 2;
    SetStatusAt(self, status);
    return status;
}

// 0x00524660 (ret 4, this not read): the overlay's intensity from a
// distance: distance * 0.004 * 0.3 + 0.2, from 0.2 to 0.5, unrounded.
double __fastcall TargetIntensity(void*, void*, float distance)
{
    static bool logged;
    FirstCall("TargetIntensity", logged);
    const double low = static_cast<double>(FloatAt(addr::cSC4CitySituationManager_IntensityLow));
    double v = static_cast<double>(distance) * static_cast<double>(FloatAt(addr::cSC4CitySituationManager_IntensityScale));
    v = v * static_cast<double>(FloatAt(addr::cSC4CitySituationManager_IntensityFactor));
    v = v + low;
    if (v < low)
        return low;
    const double high = static_cast<double>(FloatAt(addr::cSC4CitySituationManager_Half));
    if (v > high)
        return high;
    return v;
}

// 0x00525A50 (ret 4): how many nodes of the cell's bucket hold the cell.
std::uint32_t __fastcall CellSetCount(char* set, void*, const Cell* cell)
{
    static bool logged;
    FirstCall("CellSetCount", logged);
    std::uint32_t hits = 0;
    for (char* node = Buckets(set)[Key(*cell) % BucketCount(set)]; node; node = At<char*>(node, 0))
        if (Same(node, *cell))
            ++hits;
    return hits;
}

// 0x00525AA0: the first node of the buckets after the iterator's node's,
// or null.
char* __fastcall CellSetNextBucket(Iterator* it, void*)
{
    static bool logged;
    FirstCall("CellSetNextBucket", logged);
    char* const table = it->table;
    const std::uint32_t key = static_cast<std::uint32_t>(At<std::uint16_t>(it->node, 4)) |
                              (static_cast<std::uint32_t>(At<std::uint16_t>(it->node, 6)) << 16);
    std::uint32_t bucket = key % BucketCount(table);
    for (;;) {
        ++bucket;
        if (bucket >= BucketCount(table))
            return nullptr;
        if (char* const node = Buckets(it->table)[bucket])
            return node;
    }
}

// 0x00526180 (ret 4): the cell's nodes erased (the head of the bucket
// last); how many.
std::uint32_t __fastcall CellSetErase(char* set, void*, const Cell* cell)
{
    static bool logged;
    FirstCall("CellSetErase", logged);
    const std::uint32_t bucket = Key(*cell) % BucketCount(set);
    std::uint32_t erased = 0;
    char* const first = Buckets(set)[bucket];
    if (!first)
        return 0;
    char* previous = first;
    for (char* node = At<char*>(first, 0); node;) {
        if (Same(node, *cell)) {
            At<char*>(previous, 0) = At<char*>(node, 0);
            Free(node);
            ++erased;
            --At<std::uint32_t>(set, map::kCount);
            node = At<char*>(previous, 0);
        } else {
            previous = node;
            node = At<char*>(node, 0);
        }
    }
    if (Same(first, *cell)) {
        Buckets(set)[bucket] = At<char*>(first, 0);
        Free(first);
        --At<std::uint32_t>(set, map::kCount);
        return erased + 1;
    }
    return erased;
}

// 0x00526960 (ret 8): the cell itself when the set +0xAA8 holds it, else
// the set's nearest (the least squared 16-bit differences, a signed 32-bit
// sum; the first of equals), into out. Whether one was found.
std::uint8_t __fastcall NearestCell(char* self, void*, const Cell* cell, std::uint32_t* out)
{
    static bool logged;
    FirstCall("NearestCell", logged);
    char* const set = self + field::kSetAA8;
    if (CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CellSetCount, set, cell)) {
        std::memcpy(out, cell, sizeof *out);
        return 1;
    }
    std::uint8_t found = 0;
    std::int32_t best = 0x7FFFFFFF;
    Iterator it = Begin(set);
    for (char* node = it.node; node;) {
        const std::uint32_t dx = static_cast<std::uint16_t>(At<std::uint16_t>(node, 4) - cell->x);
        const std::uint32_t dz = static_cast<std::uint16_t>(At<std::uint16_t>(node, 6) - cell->z);
        const std::int32_t d = static_cast<std::int32_t>(dx * dx + dz * dz);
        if (d < best) {
            *out = At<std::uint32_t>(node, 4);
            best = d;
            found = 1;
        }
        node = At<char*>(node, 0);
        if (!node)
            node = CallThis<char*>(addr::cSC4CitySituationManager_CellSetNextBucket, static_cast<void*>(&it));
        it.node = node;
    }
    return found;
}

// 0x00529560: with cells left (+0xAB8), the target placed at the cell
// +0xAD8 (the city's slot 97 cell center, the terrain's height plus
// 0x00B43CF0's slot 6 above 0): the target's slot 9, or a new prop
// occupant automata controller handed to 0x00528580.
void __fastcall PlaceCellTarget(char* self, void*)
{
    static bool logged;
    FirstCall("PlaceCellTarget", logged);
    if (!At<std::uint32_t>(self, field::kSetAA8Count))
        return;
    Cell* const start = &At<Cell>(self, field::kCell);
    std::uint32_t nearest = 0;
    if (!CallThis<std::uint8_t>(addr::cSC4CitySituationManager_NearestCell, self, static_cast<const void*>(start),
                                static_cast<void*>(&nearest)))
        return;
    Point pos = ZeroPoint();
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, static_cast<std::uint32_t>(start->x),
                  static_cast<std::uint32_t>(start->z), static_cast<void*>(&pos.v[0]), static_cast<void*>(&pos.v[2]));
    const float above = static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Service43CF0), 0x18,
                                                           static_cast<std::uint32_t>(start->x),
                                                           static_cast<std::uint32_t>(start->z)));
    pos.v[1] = static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, pos.v[0], pos.v[2]));
    if (above > FloatAt(addr::SC4_FloatZero))
        pos.v[1] = static_cast<float>(static_cast<double>(pos.v[1]) + static_cast<double>(above));
    if (void* const target = At<void*>(self, field::kTarget)) {
        Virtual<void>(target, 0x24, static_cast<void*>(&pos));
        return;
    }
    void* const memory = CallCdecl<void*>(addr::cSC4PropOccupantAutomataController_Allocate, static_cast<std::uint32_t>(0x88));
    if (!memory)
        return;
    char* const controller = CallThis<char*>(addr::cSC4PropOccupantAutomataController_Construct, memory);
    if (!controller)
        return;
    AddRef(controller);
    Virtual<void>(controller + 0xC, 0x24, static_cast<void*>(&pos));
    CallThis<void>(addr::cSC4CitySituationManager_SetTargetOccupant, self, static_cast<void*>(controller + 0xC));
    Release(controller);
}

// 0x0052BBC0: with the situation's slot 17, the next of its targets (slot
// 16's vector, +0xA60) started by 0x0052AD00, the automaton told
// 0x2C22D924 (slot 61) when it was, and 0xAC083D7D sent (0x00524D80).
// Whether one started.
std::uint8_t __fastcall NextTarget(char* self, void*)
{
    static bool logged;
    FirstCall("NextTarget", logged);
    void* const situation = Situation(self);
    if (!situation)
        return 0;
    if (!Virtual<std::uint8_t>(situation, 0x44))
        return 0;
    void* const* const targets = Virtual<void* const*>(Situation(self), 0x40);
    const std::uint32_t count = Count(targets[0], targets[1]);
    const std::uint32_t index = ++At<std::uint32_t>(self, field::kTargetIndex);
    if (index >= count)
        return 0;
    const std::uint8_t started =
        CallThis<std::uint32_t>(addr::cSC4CitySituationManager_StartTarget, self, static_cast<std::uint32_t>(1)) != 0;
    if (started)
        Virtual<void>(Automaton(self), 0xF4, 0x2C22D924u);
    CallThis<void>(addr::cSC4CitySituationManager_SendSituationMessage, self, 0xAC083D7Du, Situation(self),
                   At<std::uint32_t>(self, field::kTargetIndex));
    return started;
}

// 0x0052C0E0: a target reached. Without a check_success script (+0xAE4
// -2): the kind 1 fails (2), 3 goes on to the next target (0x0052BBC0;
// none left succeeds, 1). With one: a target whose slot 22 answers 0x5005
// or 0x5006 starts the next (0x0052AD00), else 0x00528580 with 0. The
// status set (0x00524C40) and answered.
std::uint32_t __fastcall TargetReached(char* self, void*)
{
    static bool logged;
    FirstCall("TargetReached", logged);
    const std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    if (At<std::int32_t>(self, field::kTargetRef) == -2) {
        const std::uint32_t kind = Virtual<std::uint32_t>(Situation(self), 0x10);
        if (kind == 1) {
            SetStatusAt(self, 2);
            return 2;
        }
        if (kind == 3 && !CallThis<std::uint8_t>(addr::cSC4CitySituationManager_NextTarget, self)) {
            SetStatusAt(self, 1);
            return 1;
        }
    } else if (void* const target = At<void*>(self, field::kTarget)) {
        if (Virtual<std::uint8_t>(target, 0x58, 0x5005u) ||
            Virtual<std::uint8_t>(At<void*>(self, field::kTarget), 0x58, 0x5006u)) {
            const std::uint32_t started =
                CallThis<std::uint32_t>(addr::cSC4CitySituationManager_StartTarget, self, static_cast<std::uint32_t>(1));
            if (!started)
                CallThis<void>(addr::cSC4CitySituationManager_SetTargetOccupant, self, static_cast<void*>(nullptr));
        }
    }
    SetStatusAt(self, status);
    return status;
}

// 0x0052BC50 (ret 4), the kind 1: the distance to the target (0x00526A20
// with 0, rounded) less the situation's slot 52, rounded, shown on the
// overlay. Above the slot 7 the clock +0xA7C runs (kept rounded, compared
// unrounded times 0.001 and rounded against slot 10; at it the status 1),
// else it is zeroed. At the status 1 the next target (0x0052BBC0), 3 when
// one starts.
std::uint32_t __fastcall CheckDistanceTarget(char* self, void*, float dt)
{
    static bool logged;
    FirstCall("CheckDistanceTarget", logged);
    std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    void* const situation = Situation(self);
    const float distance =
        static_cast<float>(CallThis<double>(addr::cSC4CitySituationManager_TargetDistance, self, static_cast<std::uint32_t>(0)));
    const double radius = Virtual<double>(situation, 0xD0);
    const float gap = static_cast<float>(static_cast<double>(distance) - radius);
    ShowIntensity(self, gap);
    const double within = Virtual<double>(Situation(self), 0x1C);
    bool next;
    if (within > static_cast<double>(gap)) {
        const double clock = static_cast<double>(dt) + static_cast<double>(At<float>(self, field::kTargetTime));
        At<float>(self, field::kTargetTime) = static_cast<float>(clock);
        const float seconds = static_cast<float>(clock * static_cast<double>(FloatAt(addr::cSC4CitySituationManager_Milli)));
        const double needed = Virtual<double>(Situation(self), 0x28);
        if (needed <= static_cast<double>(seconds)) {
            status = 1;
            next = true;
        } else {
            next = status == 1;
        }
    } else {
        At<std::uint32_t>(self, field::kTargetTime) = 0;
        next = status == 1;
    }
    if (next && CallThis<std::uint8_t>(addr::cSC4CitySituationManager_NextTarget, self))
        status = 3;
    SetStatusAt(self, status);
    return status;
}

// 0x0052BD30 (ret 4), the situation's flag 2: an occupant of the
// situation's groups (slot 18, gathered by 0x00525640) of the type 0x34
// whose slot 56's +0x28 is the automaton, within the situation's radius
// (slot 20, squared and rounded; at the automaton's height): the clock
// +0xA80 runs (kept rounded, compared unrounded times 0.001 and rounded
// against slot 21; at it the status 2). None zeroes the clock. The set
// released by 0x00B43D44's slot 33. The status set and answered.
std::uint32_t __fastcall CheckEscort(char* self, void*, float dt)
{
    static bool logged;
    FirstCall("CheckEscort", logged);
    std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    if (Situation(self) && Automaton(self)) {
        Point at = ZeroPoint();
        Virtual<void>(Automaton(self), 0x1C, static_cast<void*>(&at));
        alignas(4) char set[0x14] = {};
        CallThis<void>(addr::SC4Uint32HashSet_Construct, set, static_cast<std::uint32_t>(100));
        void* const groups = Virtual<void*>(Situation(self), 0x48);
        CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GatherCandidates, self, groups, static_cast<void*>(set));
        const double radius = Virtual<double>(Situation(self), 0x50);
        const float limit = static_cast<float>(radius * radius);
        Point there = ZeroPoint();
        Iterator it = Begin(set);
        bool found = false;
        for (char* node = it.node; node;) {
            void* const occupant = At<void*>(node, 4);
            if (Virtual<std::uint32_t>(occupant, 0xEC) == 0x34 &&
                At<void*>(Virtual<char*>(occupant, 0xE0), 0x28) == Automaton(self)) {
                Virtual<void>(occupant, 0x1C, static_cast<void*>(&there));
                there.v[1] = at.v[1];
                const double d = CallCdecl<double>(addr::SC4_DistanceSquared3, static_cast<const float*>(there.v),
                                                   static_cast<const float*>(at.v));
                if (d <= static_cast<double>(limit)) {
                    found = true;
                    break;
                }
            }
            node = At<char*>(node, 0);
            if (!node)
                node = CallThis<char*>(addr::SC4HashTable_NextBucket, static_cast<void*>(&it));
            it.node = node;
        }
        if (found) {
            const double clock = static_cast<double>(dt) + static_cast<double>(At<float>(self, field::kEscortTime));
            At<float>(self, field::kEscortTime) = static_cast<float>(clock);
            const float seconds = static_cast<float>(clock * static_cast<double>(FloatAt(addr::cSC4CitySituationManager_Milli)));
            const double needed = Virtual<double>(Situation(self), 0x54);
            if (needed <= static_cast<double>(seconds))
                status = 2;
        } else {
            At<std::uint32_t>(self, field::kEscortTime) = 0;
        }
        Virtual<void>(Global<void*>(addr::SC4_Service43D44), 0x84, static_cast<void*>(set));
        CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
        if (void* const buckets = At<void*>(set, map::kBegin))
            Free(buckets);
    }
    SetStatusAt(self, status);
    return status;
}

// 0x00528C70 (ret 4), the kind 2: the distance shown on the overlay; with
// the driver's position (0x005256E0), the city and 0x00B43D8C: in a cell
// within 2 of the city's edge (the city's slot 95), when 32 units along
// the automaton's direction (slot 15) leave the city: the status 1 when
// the automaton has the flag 0x2000, or when a link of 0x00B43D8C's slot
// 16 list (its slot 6 cell) is within 2 cells (squares, unsigned).
std::uint32_t __fastcall CheckEdgeTarget(char* self, void*, float)
{
    static bool logged;
    FirstCall("CheckEdgeTarget", logged);
    std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    Point pos = ZeroPoint();
    if (At<void*>(self, field::kOverlay) && At<std::uint32_t>(self, field::kZeroA30)) {
        const float distance = static_cast<float>(
            CallThis<double>(addr::cSC4CitySituationManager_TargetDistance, self, static_cast<std::uint32_t>(0)));
        ShowIntensity(self, distance);
    }
    if (CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GetDriverPosition, self, static_cast<void*>(&pos))) {
        void* const city = Global<void*>(addr::SC4_City);
        void* const links = Global<void*>(addr::SC4_MonthEndService);
        if (city && links) {
            const float width = static_cast<float>(
                static_cast<std::int32_t>(static_cast<std::uint32_t>(Virtual<std::int32_t>(city, 0x174)) << 4));
            const float height = static_cast<float>(
                static_cast<std::int32_t>(static_cast<std::uint32_t>(Virtual<std::int32_t>(city, 0x178)) << 4));
            std::int32_t cx = 0, cz = 0;
            Virtual<void>(city, 0x17C, pos.v[0], pos.v[2], static_cast<void*>(&cx), static_cast<void*>(&cz));
            const bool nearEdge = cx < 2 || cz < 2 || cx >= Virtual<std::int32_t>(city, 0x174) - 2 ||
                                  cz >= Virtual<std::int32_t>(city, 0x178) - 2;
            if (nearEdge) {
                Point dir = ZeroPoint();
                Virtual<void>(Automaton(self), 0x3C, static_cast<void*>(&dir));
                const double reach = static_cast<double>(FloatAt(addr::cSC4CitySituationManager_EdgeReach));
                const float ax = static_cast<float>(static_cast<double>(dir.v[0]) * reach + static_cast<double>(pos.v[0]));
                const float az = static_cast<float>(static_cast<double>(dir.v[2]) * reach + static_cast<double>(pos.v[2]));
                const float zero = FloatAt(addr::SC4_FloatZero);
                if (ax < zero || az < zero || ax > width || az > height) {
                    if (Virtual<std::uint8_t>(Automaton(self), 0x7C, 0x2000u)) {
                        status = 1;
                    } else {
                        void* list = nullptr;
                        CallThis<void>(addr::SC4List_Construct2, static_cast<void*>(&list));
                        Virtual<void>(links, 0x40, static_cast<void*>(&list));
                        char* const head = static_cast<char*>(list);
                        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
                            std::int32_t lx = 0, lz = 0;
                            Virtual<void>(At<void*>(node, 8), 0x18, static_cast<void*>(&lx), static_cast<void*>(&lz));
                            const std::uint32_t ddx = static_cast<std::uint32_t>(lx) - static_cast<std::uint32_t>(cx);
                            const std::uint32_t ddz = static_cast<std::uint32_t>(lz) - static_cast<std::uint32_t>(cz);
                            if (ddz * ddz + ddx * ddx <= 4u)
                                status = 1;
                        }
                        CallThis<void>(addr::SC4List_Destroy3, static_cast<void*>(&list));
                    }
                }
            }
        }
    }
    SetStatusAt(self, status);
    return status;
}

// 0x00529B80 (ret 4), the kind 5: the distance shown on the overlay; with
// the driver's position, every cell within the situation's radius (slot
// 52, rounded; the city's slot 95 for the corners) that the set +0xAA8
// holds: its object of the map +0xAC4 told to stop (slot 4 with 1) and
// dropped, the camera effect mission_selection_yellow_shrink (or the water
// one for the situation's slot 36 kind 6) made into it from 0x00B43D1C's
// slot 7 over the cell (slot 12 with 0xC, the rectangle, 4; slot 3 with 0),
// the cell erased (0x00526180) and +0xABC counted. Then the status 1 when
// +0xABC reaches slot 44 (or +0xAC0 for 0), else the next cell
// (0x00529560). The status set and answered.
std::uint32_t __fastcall CheckCellTargets(char* self, void*, float)
{
    static bool logged;
    FirstCall("CheckCellTargets", logged);
    std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    if (At<void*>(self, field::kOverlay) && At<std::uint32_t>(self, field::kZeroA30)) {
        const float distance = static_cast<float>(
            CallThis<double>(addr::cSC4CitySituationManager_TargetDistance, self, static_cast<std::uint32_t>(0)));
        ShowIntensity(self, distance);
    }
    void* const city = Global<void*>(addr::SC4_City);
    Point pos = ZeroPoint();
    const float radius = static_cast<float>(Virtual<double>(Situation(self), 0xD0));
    if (!CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GetDriverPosition, self, static_cast<void*>(&pos))) {
        SetStatusAt(self, status);
        return status;
    }
    std::int32_t minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    Virtual<void>(city, 0x17C, static_cast<float>(static_cast<double>(pos.v[0]) - static_cast<double>(radius)),
                  static_cast<float>(static_cast<double>(pos.v[2]) - static_cast<double>(radius)),
                  static_cast<void*>(&minX), static_cast<void*>(&minZ));
    Virtual<void>(city, 0x17C, static_cast<float>(static_cast<double>(pos.v[0]) + static_cast<double>(radius)),
                  static_cast<float>(static_cast<double>(pos.v[2]) + static_cast<double>(radius)),
                  static_cast<void*>(&maxX), static_cast<void*>(&maxZ));
    char* const set = self + field::kSetAA8;
    char* const objects = self + field::kObjects;
    for (std::int32_t z = minZ; z <= maxZ; ++z)
        for (std::int32_t x = minX; x <= maxX; ++x) {
            const Cell cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(z)};
            const std::uint32_t key = Key(cell);
            std::uint32_t hits = 0;
            for (char* node = Buckets(set)[key % BucketCount(set)]; node; node = At<char*>(node, 0))
                if (Same(node, cell))
                    ++hits;
            if (!hits)
                continue;
            char* found = nullptr;
            for (char* node = Buckets(objects)[key % BucketCount(objects)]; node; node = At<char*>(node, 0))
                if (Same(node, cell)) {
                    found = node;
                    break;
                }
            if (found) {
                void** const object = &At<void*>(found, 8);
                Virtual<void>(*object, 0x10, static_cast<std::uint32_t>(1));
                if (void* const old = *object) {
                    *object = nullptr;
                    Release(old);
                }
                void* const effects = Global<void*>(addr::SC4_Service43D1C);
                const char* name = Virtual<std::uint32_t>(Situation(self), 0x90) == 6
                                       ? Global<const char*>(addr::cSC4CitySituationManager_WaterCellEffectName)
                                       : Global<const char*>(addr::cSC4CitySituationManager_CellEffectName);
                if (effects) {
                    if (void* const old = *object) {
                        *object = nullptr;
                        Release(old);
                    }
                    if (Virtual<std::uint8_t>(effects, 0x1C, name, static_cast<void*>(object))) {
                        const float rect[4] = {static_cast<float>(static_cast<std::int32_t>(cell.x)),
                                               static_cast<float>(static_cast<std::int32_t>(cell.z)),
                                               static_cast<float>(static_cast<std::int32_t>(cell.x) + 1),
                                               static_cast<float>(static_cast<std::int32_t>(cell.z) + 1)};
                        Virtual<void>(*object, 0x30, static_cast<std::uint32_t>(0xC), static_cast<const void*>(rect),
                                      static_cast<std::uint32_t>(4));
                        Virtual<void>(*object, 0xC, static_cast<std::uint32_t>(0));
                    }
                }
            }
            CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CellSetErase, set, static_cast<const void*>(&cell));
            ++At<std::uint32_t>(self, field::kZeroABC);
        }
    std::uint32_t needed = Virtual<std::uint32_t>(Situation(self), 0xB0);
    if (!needed)
        needed = At<std::uint32_t>(self, field::kZeroAC0);
    std::uint32_t written = 0;
    Virtual<void>(Situation(self), 0xB4, static_cast<void*>(&written));
    if (At<std::uint32_t>(self, field::kZeroABC) >= needed)
        status = 1;
    else
        CallThis<void>(addr::cSC4CitySituationManager_PlaceCellTarget, self);
    SetStatusAt(self, status);
    return status;
}

// 0x0052C8F0 (ret 4), each frame of a running situation (4 without a
// situation or an automaton): the escort check with the situation's flag
// 2; at the status 3 without a check_success script the kind 1, 2 or 5;
// with one (+0xAE4) the script, called with the situation's table (with
// its flag 0x20 +0xAF8's slot 68 first), answers the status when it
// answers a number (__ftol2). At the status 3 the situation's time (+0xA8C)
// runs; a time limit (slot 13) of at least 1.0 that it reaches ends the
// time (0x00524CF0). The status set and answered.
std::uint32_t __fastcall UpdateTarget(char* self, void*, float dt)
{
    static bool logged;
    FirstCall("UpdateTarget", logged);
    std::uint32_t status = 4;
    if (!Situation(self) || !Automaton(self)) {
        SetStatusAt(self, status);
        return status;
    }
    status = At<std::uint32_t>(self, field::kStatus);
    if (Virtual<std::uint8_t>(Situation(self), 0x3C, 2u))
        status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CheckEscort, self, dt);
    if (status == 3 && At<std::int32_t>(self, field::kTargetRef) == -2) {
        const std::uint32_t kind = Virtual<std::uint32_t>(Situation(self), 0x10);
        if (kind == 1) {
            if (At<void*>(self, field::kTarget))
                status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CheckDistanceTarget, self, dt);
        } else if (kind == 2) {
            status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CheckEdgeTarget, self, dt);
        } else if (kind == 5) {
            status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CheckCellTargets, self, dt);
        }
    }
    if (At<std::int32_t>(self, field::kTargetRef) != -2 && Script(self)) {
        if (Virtual<std::uint8_t>(Situation(self), 0x3C, 0x20u))
            if (void* const holder = At<void*>(self, field::kRefAF8))
                Virtual<void>(holder, 0x110);
        const std::uint32_t top = Virtual<std::uint32_t>(Script(self), 0x18);
        Virtual<void>(Script(self), 0xAC, At<std::uint32_t>(self, field::kTargetRef));
        if (Virtual<std::uint8_t>(Script(self), 0x11C, 0xFFFFFFFFu)) {
            void* const lua = Script(self);
            const std::uint32_t table = Virtual<std::uint32_t>(Situation(self), 0x78);
            Virtual<void>(lua, 0xAC, table);
            Virtual<void>(Script(self), 0xCC, 1u, 1u);
            if (Virtual<std::uint8_t>(Script(self), 0x38, 0xFFFFFFFFu))
                status = static_cast<std::uint32_t>(Ftol2(Virtual<double>(Script(self), 0x5C, 0xFFFFFFFFu)));
        }
        Virtual<void>(Script(self), 0x1C, top);
    }
    if (status == 3) {
        At<float>(self, field::kSituationTime) =
            static_cast<float>(static_cast<double>(dt) * static_cast<double>(FloatAt(addr::cSC4CitySituationManager_Milli)) +
                               static_cast<double>(At<float>(self, field::kSituationTime)));
        const double limit = Virtual<double>(self, slot::kGetCurrentTimeLimitSeconds);
        if (limit >= static_cast<double>(FloatAt(addr::cSC4CitySituationManager_One)) &&
            limit <= static_cast<double>(At<float>(self, field::kSituationTime))) {
            status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_TimeUp, self);
            SetStatusAt(self, status);
            return status;
        }
    }
    SetStatusAt(self, status);
    return status;
}

// ====================================================================================
// The steps

// 0x005254D0 (ret 0xC): the table's field of the name, when it is a
// function (the context's slot 71), referenced (slot 50 with 1) into out.
void __fastcall GetScriptRef(char* self, void*, const char* name, std::uint32_t table, std::int32_t* out)
{
    static bool logged;
    FirstCall("GetScriptRef", logged);
    if (!CallCdecl<std::uint8_t>(addr::SC4Script_GetField, Script(self), name, table))
        return;
    if (!Virtual<std::uint8_t>(Script(self), 0x11C, 0xFFFFFFFFu))
        return;
    *out = Virtual<std::int32_t>(Script(self), 0xC8, 1u);
}

// 0x00525800 (ret 4): with a situation, its table's (slot 30) functions
// referenced into +0xADC..+0xAF4 (on_success, on_failure, check_success,
// find_target, create_target, init, get_time_limit; the last from the
// global base_citysituation_advice when the table has none); the stack
// put back. Without one, the seven unreferenced (slot 62) and set to -2.
void __fastcall LoadScripts(char* self, void*, void* situation)
{
    static bool logged;
    FirstCall("LoadScripts", logged);
    static const std::size_t kRefs[7] = {field::kSuccessRef, field::kFailureRef, field::kTargetRef, 0xAE8,
                                          0xAEC,              field::kRefAF0,     field::kTableRef};
    if (!situation) {
        for (const std::size_t ref : kRefs) {
            if (void* const lua = Script(self))
                if (At<std::int32_t>(self, ref) != -2)
                    Virtual<void>(lua, 0xF8, At<std::int32_t>(self, ref));
            At<std::int32_t>(self, ref) = -2;
        }
        return;
    }
    void* const lua = Script(self);
    if (!lua)
        return;
    const std::uint32_t top = Virtual<std::uint32_t>(lua, 0x18);
    const std::uint32_t own = Virtual<std::uint32_t>(situation, 0x78);
    Virtual<void>(Script(self), 0xAC, own);
    const std::uint32_t table = Virtual<std::uint32_t>(Script(self), 0x18);
    if (Virtual<std::uint8_t>(Script(self), 0x120, table)) {
        static const std::uintptr_t kNames[7] = {
            addr::cSC4CitySituationManager_OnSuccessName,    addr::cSC4CitySituationManager_OnFailureName,
            addr::cSC4CitySituationManager_CheckSuccessName, addr::cSC4CitySituationManager_FindTargetName,
            addr::cSC4CitySituationManager_CreateTargetName, addr::cSC4CitySituationManager_InitName,
            addr::cSC4CitySituationManager_GetTimeLimitName};
        for (int i = 0; i < 7; ++i)
            CallThis<void>(addr::cSC4CitySituationManager_GetScriptRef, self, Global<const char*>(kNames[i]), table,
                           static_cast<void*>(&At<std::int32_t>(self, kRefs[i])));
        std::int32_t* const limit = &At<std::int32_t>(self, field::kTableRef);
        if (*limit == -2 &&
            CallCdecl<std::uint8_t>(addr::SC4Script_PushGlobal, Script(self),
                                    reinterpret_cast<const char*>(addr::cSC4CitySituationManager_BaseAdviceName))) {
            const std::uint32_t base = Virtual<std::uint32_t>(Script(self), 0x18);
            if (CallCdecl<std::uint8_t>(addr::SC4Script_GetField, Script(self),
                                        Global<const char*>(addr::cSC4CitySituationManager_GetTimeLimitName), base)) {
                if (Virtual<std::uint8_t>(Script(self), 0x11C, 0xFFFFFFFFu))
                    *limit = Virtual<std::int32_t>(Script(self), 0xC8, 1u);
                else
                    Virtual<void>(Script(self), 0x2C, 1u);
            }
            Virtual<void>(Script(self), 0x2C, 1u);
        }
    }
    Virtual<void>(Script(self), 0x1C, top);
}

// 0x00525070 (ret 4): a new active automaton (+0xA4C): the old one's
// listener (slot 40 with the +4 part) taken back, referenced; the new one
// gets the flag 0x4000 (slot 30), the listener (slot 39) and its slot 68.
void __fastcall SetActiveAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("SetActiveAutomaton", logged);
    if (automaton == Automaton(self))
        return;
    if (void* const old = Automaton(self))
        Virtual<void>(old, 0xA0, static_cast<void*>(self + field::kVtable4));
    void* const old = Automaton(self);
    if (automaton != old) {
        if (automaton)
            AddRef(automaton);
        At<void*>(self, field::kAuto) = automaton;
        if (old)
            Release(old);
    }
    if (Automaton(self)) {
        Virtual<void>(Automaton(self), 0x78, 0x4000u, 1u);
        Virtual<void>(Automaton(self), 0x9C, static_cast<void*>(self + field::kVtable4));
        Virtual<void>(Automaton(self), 0x110);
    }
}

// 0x005260C0 (ret 8): the cell's node, or a new one (8 bytes) at the head
// of its bucket, into out; EAX out.
InsertResult* __fastcall CellSetInsertUnique(char* set, void*, InsertResult* out, const Cell* cell)
{
    static bool logged;
    FirstCall("CellSetInsertUnique", logged);
    const std::uint32_t bucket = Key(*cell) % BucketCount(set);
    char* const first = Buckets(set)[bucket];
    for (char* node = first; node; node = At<char*>(node, 0))
        if (Same(node, *cell)) {
            out->table = set;
            out->node = node;
            out->inserted = 0;
            return out;
        }
    char* const node = static_cast<char*>(Allocate(8));
    At<char*>(node, 0) = nullptr;
    At<std::uint16_t>(node, 4) = cell->x;
    At<std::uint16_t>(node, 6) = cell->z;
    At<char*>(node, 0) = first;
    Buckets(set)[bucket] = node;
    ++At<std::uint32_t>(set, map::kCount);
    out->table = set;
    out->node = node;
    out->inserted = 1;
    return out;
}

// 0x00526510 (ret 4): more buckets for at least the hint (the next prime of
// SC4HashTable's list, 0xFFFFFFFB past it), the nodes moved over.
void __fastcall CellSetResize(char* set, void*, std::uint32_t hint)
{
    static bool logged;
    FirstCall("CellSetResize", logged);
    const std::uint32_t old = BucketCount(set);
    if (hint <= old)
        return;
    std::uint32_t n = hint;
    const auto* const primesEnd = reinterpret_cast<const std::uint32_t*>(addr::SC4HashTable_PrimesEnd);
    const std::uint32_t* const pos =
        CallCdecl<const std::uint32_t*>(addr::SC4_LowerBoundUint32, reinterpret_cast<const std::uint32_t*>(addr::SC4HashTable_Primes),
                                        primesEnd, static_cast<const std::uint32_t*>(&n), hint, static_cast<std::uint32_t>(0));
    n = pos == primesEnd ? 0xFFFFFFFBu : *pos;
    if (n <= old)
        return;
    void* fresh[3] = {};
    void* const null = nullptr;
    CallThis<void*>(addr::SC4PtrVector_Construct, static_cast<void*>(fresh), n, static_cast<void* const*>(&null),
                    static_cast<const void*>(&n));
    char** const buckets = static_cast<char**>(fresh[0]);
    for (std::uint32_t i = 0; i < old; ++i)
        for (char* node = Buckets(set)[i]; node; node = Buckets(set)[i]) {
            const std::uint32_t index = At<std::uint32_t>(node, 4) % n;
            Buckets(set)[i] = At<char*>(node, 0);
            At<char*>(node, 0) = buckets[index];
            buckets[index] = node;
        }
    void* const previous = At<void*>(set, map::kBegin);
    At<void*>(set, map::kBegin) = fresh[0];
    At<void*>(set, map::kEnd) = fresh[1];
    At<void*>(set, map::kCapacity) = fresh[2];
    if (previous)
        Free(previous);
}

// 0x005278E0 (ret 8): the insert: room for one more (0x00526510), then
// 0x005260C0, into out; EAX out.
InsertResult* __fastcall CellSetInsert(char* set, void*, InsertResult* out, const Cell* cell)
{
    static bool logged;
    FirstCall("CellSetInsert", logged);
    CallThis<void>(addr::cSC4CitySituationManager_CellSetResize, set, At<std::uint32_t>(set, map::kCount) + 1);
    InsertResult result{};
    CallThis<void*>(addr::cSC4CitySituationManager_CellSetInsertUnique, set, static_cast<void*>(&result),
                    static_cast<const void*>(cell));
    out->node = result.node;
    out->table = result.table;
    out->inserted = result.inserted;
    return out;
}

// 0x005278A0 (ret 8): the uint32 set's insert: room for one more
// (SC4Uint32HashSet::Resize), then 0x00525FB0, into out; EAX out.
InsertResult* __fastcall Uint32SetInsert(char* set, void*, InsertResult* out, const std::uint32_t* key)
{
    static bool logged;
    FirstCall("Uint32SetInsert", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Resize, set, At<std::uint32_t>(set, map::kCount) + 1);
    InsertResult result{};
    CallThis<void*>(addr::cSC4CitySituationManager_Uint32SetInsertUnique, set, static_cast<void*>(&result),
                    static_cast<const void*>(key));
    out->node = result.node;
    out->table = result.table;
    out->inserted = result.inserted;
    return out;
}

// A cell and its object, as 0x00526250 takes them.
struct CellObject {
    Cell cell;
    void* object;
};

// 0x00526250 (ret 8): the cell's node of the map, or a new one (0xC bytes,
// the object referenced) at the head of its bucket, into out; EAX out.
InsertResult* __fastcall CellMapInsertUnique(char* map, void*, InsertResult* out, const CellObject* value)
{
    static bool logged;
    FirstCall("CellMapInsertUnique", logged);
    const std::uint32_t bucket = Key(value->cell) % BucketCount(map);
    char* const first = Buckets(map)[bucket];
    for (char* node = first; node; node = At<char*>(node, 0))
        if (Same(node, value->cell)) {
            out->table = map;
            out->node = node;
            out->inserted = 0;
            return out;
        }
    char* const node = static_cast<char*>(Allocate(0xC));
    At<char*>(node, 0) = nullptr;
    At<std::uint16_t>(node, 4) = value->cell.x;
    At<std::uint16_t>(node, 6) = value->cell.z;
    At<void*>(node, 8) = value->object;
    if (value->object)
        AddRef(value->object);
    At<char*>(node, 0) = first;
    Buckets(map)[bucket] = node;
    ++At<std::uint32_t>(map, map::kCount);
    out->node = node;
    out->table = map;
    out->inserted = 1;
    return out;
}

// 0x005271D0 (ret 4): the cell set copied from another: the buckets
// emptied, reserved and filled with nulls to the other's count, each chain
// copied in order, the count taken over.
void __fastcall CellSetAssign(char* set, void*, char* other)
{
    static bool logged;
    FirstCall("CellSetAssign", logged);
    At<void*>(set, map::kEnd) = At<void*>(set, map::kBegin);
    char* const vector = set + map::kBegin;
    CallThis<void>(addr::PtrVector_Reserve, static_cast<void*>(vector), BucketCount(other));
    void* const null = nullptr;
    CallThis<void>(addr::SC4PtrVector_FillInsert, static_cast<void*>(vector), At<void*>(set, map::kEnd),
                   BucketCount(other), static_cast<void* const*>(&null));
    for (std::uint32_t i = 0; i < BucketCount(other); ++i) {
        char* from = Buckets(other)[i];
        if (!from)
            continue;
        char* copy = static_cast<char*>(Allocate(8));
        At<char*>(copy, 0) = nullptr;
        At<std::uint16_t>(copy, 4) = At<std::uint16_t>(from, 4);
        At<std::uint16_t>(copy, 6) = At<std::uint16_t>(from, 6);
        Buckets(set)[i] = copy;
        for (from = At<char*>(from, 0); from; from = At<char*>(from, 0)) {
            char* const next = static_cast<char*>(Allocate(8));
            At<char*>(next, 0) = nullptr;
            At<std::uint16_t>(next, 4) = At<std::uint16_t>(from, 4);
            At<std::uint16_t>(next, 6) = At<std::uint16_t>(from, 6);
            At<char*>(copy, 0) = next;
            copy = next;
        }
    }
    At<std::uint32_t>(set, map::kCount) = At<std::uint32_t>(other, map::kCount);
}

// 0x00524E90 (stdcall, 8 arguments): a cell filter: under the limit of the
// count, inside the width and height (signed), the object's slot 7 at the
// cell (sign-extended) equal to `want`, and 0x00B43CF0's slot 8 (the cell,
// 2, 0) true.
std::uint8_t __stdcall CellFilter(const std::uint16_t* cell, std::int32_t want, std::uint32_t, const std::uint32_t* count,
                                  std::uint32_t limit, std::int32_t width, std::int32_t height, void* object)
{
    static bool logged;
    FirstCall("CellFilter", logged);
    if (*count >= limit)
        return 0;
    const std::uint32_t x = cell[0];
    if (static_cast<std::int32_t>(x) >= width)
        return 0;
    const std::uint32_t z = cell[1];
    if (static_cast<std::int32_t>(z) >= height)
        return 0;
    const std::int8_t answer = static_cast<std::int8_t>(Virtual<std::uint8_t>(object, 0x1C, x, z));
    if (static_cast<std::int32_t>(answer) != want)
        return 0;
    return Virtual<std::uint8_t>(Global<void*>(addr::SC4_Service43CF0), 0x20, static_cast<std::uint32_t>(cell[0]),
                                 static_cast<std::uint32_t>(cell[1]), 2u, 0u) != 0;
}

// 0x00524F00 (ret 8): with a situation, the function of the reference
// (pushed, slot 43) called when it is one (slot 71) with the situation's
// table and a new table of the IDs (slot 44; numbers by slot 31, set by
// slot 46), one result; that result's object (slot 76 with the framework's
// 0xEBE0E860 service) asked for 0x26BB767E. The stack put back. The object
// is answered after its release.
void* __fastcall CallTargetScript(char* self, void*, std::uint32_t ref, const std::uint32_t* const* ids)
{
    static bool logged;
    FirstCall("CallTargetScript", logged);
    void* holder = nullptr;
    if (Situation(self)) {
        const std::uint32_t top = Virtual<std::uint32_t>(Script(self), 0x18);
        Virtual<void>(Script(self), 0xAC, ref);
        if (Virtual<std::uint8_t>(Script(self), 0x11C, 0xFFFFFFFFu)) {
            void* const lua = Script(self);
            const std::uint32_t table = Virtual<std::uint32_t>(Situation(self), 0x78);
            Virtual<void>(lua, 0xAC, table);
            Virtual<void>(Script(self), 0xB0);
            const std::uint32_t list = Virtual<std::uint32_t>(Script(self), 0x18);
            for (const std::uint32_t* id = ids[0]; id != ids[1]; ++id) {
                Virtual<void>(Script(self), 0x7C, static_cast<double>(*id));
                Virtual<void>(Script(self), 0xB8, list);
            }
            Virtual<void>(Script(self), 0xCC, 2u, 1u);
            void* service = nullptr;
            if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
                Virtual<std::uint8_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, static_cast<void*>(&service));
            if (void* const object = Virtual<void*>(Script(self), 0x130, service, 0xFFFFFFFFu)) {
                if (void* const old = holder) {
                    holder = nullptr;
                    Release(old);
                }
                Virtual<std::uint8_t>(object, slot::kQueryInterface, 0x26BB767Eu, static_cast<void*>(&holder));
            }
            CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&service));
        }
        Virtual<void>(Script(self), 0x1C, top);
    }
    if (holder)
        Release(holder);
    return holder;
}

// 0x00529AF0 (ret 4): the target IDs into the vector: with the situation's
// slot 17 the current one of slot 16's (+0xA60, zeroed past the end), else
// slot 13's.
void __fastcall GetTargetIds(char* self, void*, std::uint32_t** out)
{
    static bool logged;
    FirstCall("GetTargetIds", logged);
    if (!Situation(self))
        return;
    if (Virtual<std::uint8_t>(Situation(self), 0x44)) {
        std::uint32_t* const* const targets = Virtual<std::uint32_t* const*>(Situation(self), 0x40);
        if (At<std::uint32_t>(self, field::kTargetIndex) >= Count(targets[0], targets[1]))
            At<std::uint32_t>(self, field::kTargetIndex) = 0;
        const std::uint32_t zero = 0;
        CallThis<void>(addr::SC4Uint32Vector_Resize, static_cast<void*>(out), 1u, static_cast<const void*>(&zero));
        *out[0] = targets[0][At<std::uint32_t>(self, field::kTargetIndex)];
        return;
    }
    void* const ids = Virtual<void*>(Situation(self), 0x34);
    CallThis<void*>(addr::SC4Uint32Vector_CopyAssign, static_cast<void*>(out), ids);
}

// 0x005294C0, slot 38: the end of a situation. With one: the controlled
// objects released (0x00527800), no active automaton (0x00525070), no
// target (0x00528580), the referenced set +0xA68 cleared, the status 4 and
// no targets, the scripts dropped (0x00525800), 0xABBBCA64 sent
// (0x00524D80), +0xA98, the situation, +0xA64 and +0xAF8 dropped.
void __fastcall EndSituation(char* self, void*)
{
    static bool logged;
    FirstCall("EndSituation", logged);
    if (!Situation(self))
        return;
    CallThis<void>(addr::cSC4CitySituationManager_ReleaseObjects, self);
    CallThis<void>(addr::cSC4CitySituationManager_SetActiveAutomaton, self, static_cast<void*>(nullptr));
    CallThis<void>(addr::cSC4CitySituationManager_SetTargetOccupant, self, static_cast<void*>(nullptr));
    CallThis<void>(addr::SC4RefHashSet_Clear, static_cast<void*>(self + field::kRefSet));
    At<std::uint32_t>(self, field::kStatus) = 4;
    At<std::uint32_t>(self, field::kNumTargets) = 0;
    CallThis<void>(addr::cSC4CitySituationManager_LoadScripts, self, static_cast<void*>(nullptr));
    CallThis<void>(addr::cSC4CitySituationManager_SendSituationMessage, self, 0xABBBCA64u, Situation(self),
                   static_cast<std::uint32_t>(0));
    At<std::uint8_t>(self, field::kJoyride) = 0;
    Drop(self, field::kSituation);
    At<std::uint8_t>(self, field::kByteA64) = 0;
    Drop(self, field::kRefAF8);
}

// 0x00524AB0 (ret 4), slot 4 of the occupant filter of vtable 0x00A97D90:
// the occupant's 0xA821EF94 interface answering its slot 9 for +0xC, and,
// with a radius above 0 (+0x10, squared), not nearer the point +0x14 (at
// its height +0x18) than it.
std::uint8_t __fastcall RadiusFilterIncludes(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("RadiusFilterIncludes", logged);
    std::uint8_t answer = 0;
    void* holder = nullptr;
    if (!occupant)
        return 0;
    if (Virtual<std::uint8_t>(occupant, slot::kQueryInterface, 0xA821EF94u, static_cast<void*>(&holder)) &&
        Virtual<std::uint8_t>(holder, 0x24, At<std::uint32_t>(self, 0xC))) {
        answer = 1;
        if (At<float>(self, 0x10) > FloatAt(addr::SC4_FloatZero)) {
            Point pos = ZeroPoint();
            Virtual<void>(occupant, 0x20, static_cast<void*>(&pos));
            pos.v[1] = At<float>(self, 0x18);
            const double d = CallCdecl<double>(addr::SC4_DistanceSquared3, static_cast<const float*>(pos.v),
                                               reinterpret_cast<const float*>(self + 0x14));
            if (d < static_cast<double>(At<float>(self, 0x10)))
                answer = 0;
        }
    }
    if (holder)
        Release(holder);
    return answer;
}

// 0x00526650 (ret 4), slot 4 of the occupant filter of vtable 0x00A97DE0:
// the group filter's answer (SC4OccupantGroupFilter::IsOccupantIncluded)
// and, with a radius above 0 (+0x18, squared), not nearer the point +0x1C
// (at its height +0x20) than it.
std::uint8_t __fastcall GroupRadiusFilterIncludes(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("GroupRadiusFilterIncludes", logged);
    const std::uint8_t included = CallThis<std::uint8_t>(addr::SC4OccupantGroupFilter_IsOccupantIncluded, self, occupant);
    if (!included)
        return 0;
    if (At<float>(self, 0x18) > FloatAt(addr::SC4_FloatZero)) {
        Point pos = ZeroPoint();
        Virtual<void>(occupant, 0x20, static_cast<void*>(&pos));
        pos.v[1] = At<float>(self, 0x20);
        const double d = CallCdecl<double>(addr::SC4_DistanceSquared3, static_cast<const float*>(pos.v),
                                           reinterpret_cast<const float*>(self + 0x1C));
        if (d < static_cast<double>(At<float>(self, 0x18)))
            return 0;
    }
    return included;
}

// ====================================================================================
// Installation

bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_AUTOMATA_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

bool InstallList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("automata situations %s: no address entry for 0x%08X", what,
                       static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("automata situations %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kTargets[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_UpdateTarget, UpdateTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CheckEdgeTarget, CheckEdgeTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CheckCellTargets, CheckCellTargets),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CheckDistanceTarget, CheckDistanceTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CheckEscort, CheckEscort),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SetStatus, SetStatus),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TimeUp, TimeUp),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TargetIntensity, TargetIntensity),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellSetErase, CellSetErase),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_PlaceCellTarget, PlaceCellTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_NearestCell, NearestCell),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellSetCount, CellSetCount),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellSetNextBucket, CellSetNextBucket),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TargetReached, TargetReached),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_NextTarget, NextTarget),
};

const Detour kSteps[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LoadScripts, LoadScripts),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetScriptRef, GetScriptRef),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SetActiveAutomaton, SetActiveAutomaton),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellSetInsert, CellSetInsert),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellSetInsertUnique, CellSetInsertUnique),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellSetResize, CellSetResize),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellMapInsertUnique, CellMapInsertUnique),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellSetAssign, CellSetAssign),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CellFilter, CellFilter),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CallTargetScript, CallTargetScript),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetTargetIds, GetTargetIds),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Uint32SetInsert, Uint32SetInsert),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_EndSituation, EndSituation),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_RadiusFilterIncludes, RadiusFilterIncludes),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GroupRadiusFilterIncludes, GroupRadiusFilterIncludes),
};

#undef OPENSC4_DETOUR

bool InstallTargets(registry::Mode) { return InstallList(kTargets, sizeof kTargets / sizeof kTargets[0], "targets"); }
bool InstallSteps(registry::Mode) { return InstallList(kSteps, sizeof kSteps / sizeof kSteps[0], "steps"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.situations.targets", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_UpdateTarget,
                        "Our city situation manager's targets: the target update of each frame (the escort, the "
                        "distance, the city's edge, the cells, the check_success script, the time limit), the status, "
                        "the next target, a target reached, the cells' set (15 detours; part c4, "
                        "src/core/automata_situations_4.cpp)",
                        false, InstallTargets}),
    registry::Register({"simulators.automata.situations.steps", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_EndSituation,
                        "Our city situation manager's smaller steps: the end of a situation (slot 38), the scripts' "
                        "references, the active automaton, the target IDs and script, the cell filter, the two "
                        "occupant filters, the cell set and map helpers (15 detours; part c4, "
                        "src/core/automata_situations_4.cpp)",
                        false, InstallSteps}),
};

} // namespace
} // namespace opensc4::automata_situations
