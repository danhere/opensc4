// Our city situation manager, part c5 (build 641): the running situation.
// The class map is in src/core/automata_situations.h and the survey in
// docs/automata.md, "Part c". Calls between functions of the part, and to
// the other parts, go to the game's addresses, so each detour stands alone
// and OPENSC4_AUTOMATA_SKIP bisects them.
//
// Written from the disassembly; all 11 audited against it (no behavioural
// difference). The test is OPENSC4_TEST_AUTOMATA_SITUATIONS=5 (token ` G`,
// src/core/automata_autotest.cpp), which reaches all 11.
//
// Group, off by default:
//   simulators.automata.situations.running (11): kRunning below.
//
// What it does.
//   - Slot 36 (0x0052E750): a new situation started for an automaton: the
//     old one ended (slot 38), the situation referenced, the status 4, the
//     scripts (0x00525800), by the kind (slot 4): 2 at once, 5 the cells
//     (0x0052C4E0), else the targets (slot 17/16 count), the first target
//     (0x0052AD00, or 0x0052B200 with the flag 0x100 or 1). 0xEBB67EF7
//     posted (0x00524D10) with the automaton when asked, else with 0 and the
//     status 5.
//   - 0x0052C4E0: the cell set +0xAA8 made by the cell kind (slot 36): 1, 5
//     and 6 0x0052B450 (a region of the city around the automaton), 2
//     0x005299C0 (a lot's cells), 3 0x00529680 (a flood from an occupant),
//     4 a rectangle (slot 40), 7 a list of offsets from the city's middle
//     (slot 41). Then a marker effect on each cell (the map +0xAC4) and the
//     target placed at the middle (0x00529560).
//   - 0x0052B200: the find_target script (+0xAEC) or the services' slot 15
//     near the automaton makes the target (slot 5 of what they answer).
//   - 0x0052AD00: the next target by the kind (slot 4): 1 and 3 a candidate
//     of the target groups (0x00525640) within the radii, else a random one
//     the network service finds near the automaton; 2 the occupant of a
//     link nearest by path (0x00554EA0); 4 the create_target script (+0xAE8).
//
// Oddities kept (see the functions): 0x0052AD00 asks the referenced set
// +0xA68 for its result (still null) instead of the candidate in its first
// loop; 0x0052C4E0's middle takes the least z twice (its "max z" keeps the
// minimum); 0x00529680's search box is the automaton's position plus or
// minus the squared radius; 0x005299C0 stops a column at its limit and goes
// on to the next; slot 36 sets the status 5 after a start it does not post.
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
// Helpers (as part c4's)

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

template <typename R, typename... Args>
inline R CallStd(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__stdcall*)(Args...)>(address)(args...);
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

void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }
void AddRef(void* object) { Virtual<std::uint32_t>(object, slot::kAddRef); }
void Release(void* object) { Virtual<std::uint32_t>(object, slot::kRelease); }

// SC4RefPtr::Assign (0x005447B0): the holder takes the object (AddRef the
// new, Release the old).
void Assign(void** holder, void* object) { CallThis<void>(addr::SC4RefPtr_Assign, static_cast<void*>(holder), object); }

std::uint32_t ZeroBits() { return Global<std::uint32_t>(addr::cSC4CitySituationManager_ZeroFloat); }
float FloatAt(std::uintptr_t address) { return Global<float>(address); }

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

struct Cell {
    std::uint16_t x;
    std::uint16_t z;
};

struct Iterator {
    char* node;
    char* table;
};

struct InsertResult {
    char* node;
    char* table;
    std::uint8_t inserted;
    std::uint8_t pad[3];
};

// A uint32 vector (begin, end, capacity).
struct Vector {
    std::uint32_t* begin;
    std::uint32_t* end;
    std::uint32_t* capacity;
};

// A hash set as the class keeps them: +0 unused here, +4 the buckets, +0x10
// the count.
struct HashSet {
    std::uint32_t head;
    void* begin;
    void* end;
    void* capacity;
    std::uint32_t count;
};

std::int32_t Count4(const void* begin, const void* end)
{
    return static_cast<std::int32_t>(static_cast<const char*>(end) - static_cast<const char*>(begin)) >> 2;
}

void* Situation(char* self) { return At<void*>(self, field::kSituation); }
void* Automaton(char* self) { return At<void*>(self, field::kAuto); }
char* CellSet(char* self) { return self + field::kSetAA8; }

// A cell inserted into a cell set with room for one more (0x00526510, then
// 0x005260C0), as the functions here do it inline.
void InsertCell(char* set, const Cell& cell)
{
    CallThis<void>(addr::cSC4CitySituationManager_CellSetResize, set, At<std::uint32_t>(set, 0x10) + 1);
    InsertResult out{};
    CallThis<void*>(addr::cSC4CitySituationManager_CellSetInsertUnique, set, static_cast<void*>(&out),
                    static_cast<const void*>(&cell));
}

// An empty list (0xC-byte sentinel) from a node allocator global, which is
// fetched from the framework (0x3AE4BEA3, 0x3AE4BEAB) when unset.
char* NewList(std::uintptr_t allocatorGlobal)
{
    void* nodes = Global<void*>(allocatorGlobal);
    if (!nodes) {
        void* got = nullptr;
        if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
            Virtual<std::uint32_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, static_cast<void*>(&got));
        *reinterpret_cast<void**>(allocatorGlobal) = got;
        CallThis<void>(addr::SC4_ReleaseRef, static_cast<void*>(&got));
        nodes = Global<void*>(allocatorGlobal);
    }
    char* const sentinel = Virtual<char*>(nodes, 0x0C, 0xCu);
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    return sentinel;
}

// SC4SpiralWalk (0x0043FE10 Start, 0x00440850 Next), 0x30 bytes: +0 x, +4 z,
// +8, +0xC, +0x10..+0x1C the bounds (min x, min z, max x, max z), +0x20 a
// byte, +0x24, +0x28..+0x2A bytes, +0x2C a count.
struct alignas(4) SpiralWalk {
    std::int32_t x, z, x2, z2;
    std::int32_t minX, minZ, maxX, maxZ;
    std::uint8_t b20, pad21[3];
    std::int32_t step;
    std::uint8_t state, first, end, pad2b;
    std::int32_t count;
};
static_assert(sizeof(SpiralWalk) == 0x30, "SC4SpiralWalk");

void WalkStart(SpiralWalk* walk, std::int32_t x, std::int32_t z)
{
    CallThis<void>(addr::SC4SpiralWalk_Start, static_cast<void*>(walk), x, z);
}

bool WalkNext(SpiralWalk* walk, std::int32_t* x, std::int32_t* z)
{
    return CallThis<std::uint8_t>(addr::SC4SpiralWalk_Next, static_cast<void*>(walk), static_cast<void*>(x),
                                  static_cast<void*>(z)) != 0;
}

std::int32_t Clamp(std::int32_t v, std::int32_t high)
{
    if (v < 0)
        return 0;
    return v > high ? high : v;
}

// ====================================================================================
// The running situation

// 0x00527EA0 (thiscall, ret 0x24; this only passed on): the kind 6's region
// fill from a cell: while the cell passes the cell filter (0x00524E90 with
// the count, the limit, the city's size and the object), it is added when
// new, and its four neighbours (x-1, z-1, x+1, z+1) that pass are added
// unconditionally; then the fill recurses at x-1, z-1 and x+1 and goes on
// at z+1. Each add counts.
void __fastcall RegionFill(char* self, void*, std::int32_t x, std::int32_t z, std::int32_t want, char* set,
                           std::uint32_t* found, std::uint32_t limit, std::int32_t width, std::int32_t height,
                           void* object)
{
    static bool logged;
    FirstCall("RegionFill", logged);
    for (;; ++z) {
        auto passes = [&](const Cell& c) {
            return CallStd<std::uint8_t>(addr::cSC4CitySituationManager_CellFilter, static_cast<const void*>(&c), want,
                                         static_cast<void*>(set), static_cast<const void*>(found), limit, width, height,
                                         object) != 0;
        };
        const Cell here{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(z)};
        if (!passes(here))
            return;
        if (!CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CellSetCount, set, static_cast<const void*>(&here))) {
            InsertCell(set, here);
            ++*found;
        }
        const Cell around[4] = {{static_cast<std::uint16_t>(x - 1), static_cast<std::uint16_t>(z)},
                                {static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(z - 1)},
                                {static_cast<std::uint16_t>(x + 1), static_cast<std::uint16_t>(z)},
                                {static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(z + 1)}};
        for (const Cell& c : around)
            if (passes(c)) {
                InsertCell(set, c);
                ++*found;
            }
        using Fill = void(__thiscall*)(void*, std::int32_t, std::int32_t, std::int32_t, char*, std::uint32_t*,
                                       std::uint32_t, std::int32_t, std::int32_t, void*);
        const auto fill = reinterpret_cast<Fill>(addr::cSC4CitySituationManager_RegionFill); // NEW
        fill(self, x - 1, z, want, set, found, limit, width, height, object);
        fill(self, x, z - 1, want, set, found, limit, width, height, object);
        fill(self, x + 1, z, want, set, found, limit, width, height, object);
    }
}

// 0x00527D30 (ret 0x10): the kind 3's flood: with cells left (*remaining),
// the cell moved one step by the direction (0 x-1, 1 z-1, 2 x+1 below the
// width, 3 z+1 below the height; at the edge it stops; above 3 it stays);
// the first occupant of the cell's list (0x00B43D54's slot 13, next +0x54)
// whose slot 9 answers true for `kind`; a new cell is added (0x005278E0),
// counted down, and the flood goes on from it in the four directions from
// this one's.
void __fastcall FloodCells(char* self, void*, Cell cell, std::uint32_t direction, std::uint32_t kind,
                           std::int32_t* remaining)
{
    static bool logged;
    FirstCall("FloodCells", logged);
    if (*remaining == 0)
        return;
    const std::uint32_t dir = direction & 0xFFu;
    switch (dir) {
    case 0:
        if (cell.x == 0)
            return;
        --cell.x;
        break;
    case 1:
        if (cell.z == 0)
            return;
        --cell.z;
        break;
    case 2:
        if (static_cast<std::int32_t>(cell.x) >= Virtual<std::int32_t>(Global<void*>(addr::SC4_City), 0x174))
            return;
        ++cell.x;
        break;
    case 3:
        if (static_cast<std::int32_t>(cell.z) >= Virtual<std::int32_t>(Global<void*>(addr::SC4_City), 0x178))
            return;
        ++cell.z;
        break;
    default:
        break;
    }
    char* node = Virtual<char*>(Global<void*>(addr::SC4_Service43D54), 0x34, static_cast<std::uint32_t>(cell.x),
                                static_cast<std::uint32_t>(cell.z));
    if (!node)
        return;
    while (!Virtual<std::uint8_t>(At<void*>(node, 0), 0x24, kind)) {
        node = At<char*>(node, 0x54);
        if (!node)
            return;
    }
    char* const set = CellSet(self);
    if (CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CellSetCount, set, static_cast<const void*>(&cell)))
        return;
    InsertResult out{};
    CallThis<void*>(addr::cSC4CitySituationManager_CellSetInsert, set, static_cast<void*>(&out),
                    static_cast<const void*>(&cell));
    if (--*remaining == 0)
        return;
    using Flood = void(__thiscall*)(void*, Cell, std::uint32_t, std::uint32_t, std::int32_t*);
    const auto flood = reinterpret_cast<Flood>(addr::cSC4CitySituationManager_FloodCells); // NEW
    for (std::uint32_t i = 0; i < 4; ++i) {
        std::int32_t d = static_cast<std::int32_t>((dir + i) & 0x80000003u);
        if (d < 0)
            d = ((d - 1) | static_cast<std::int32_t>(0xFFFFFFFCu)) + 1;
        flood(self, cell, static_cast<std::uint32_t>(d), kind, remaining);
    }
}

// 0x00528140 (ret 0xC): the kind 2's lot cells grown to the goal: past the
// lot's rectangle (x0, z0, x1, z1) along its longer side, rows (or columns)
// after it up to the city's edge, then before it down to 0, each whole,
// while the count is below the goal (checked per row).
void __fastcall ExtendLotCells(char* self, void*, const std::int32_t* rect, std::uint32_t count, std::uint32_t goal)
{
    static bool logged;
    FirstCall("ExtendLotCells", logged);
    void* const city = Global<void*>(addr::SC4_City);
    if (goal == 0 || count >= goal)
        return;
    char* const set = CellSet(self);
    if (rect[2] - rect[0] > rect[3] - rect[1]) {
        for (std::int32_t row = rect[3] + 1; row < Virtual<std::int32_t>(city, 0x178); ++row) {
            if (count >= goal)
                break;
            for (std::int32_t x = rect[0]; x <= rect[2]; ++x) {
                InsertCell(set, Cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(row)});
                ++count;
            }
        }
        for (std::int32_t row = rect[1] - 1; row >= 0; --row) {
            if (count >= goal)
                return;
            for (std::int32_t x = rect[0]; x <= rect[2]; ++x) {
                InsertCell(set, Cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(row)});
                ++count;
            }
        }
        return;
    }
    for (std::int32_t col = rect[2] + 1; col < Virtual<std::int32_t>(city, 0x174); ++col) {
        if (count >= goal)
            break;
        for (std::int32_t z = rect[1]; z <= rect[3]; ++z) {
            InsertCell(set, Cell{static_cast<std::uint16_t>(col), static_cast<std::uint16_t>(z)});
            ++count;
        }
    }
    for (std::int32_t col = rect[0] - 1; col >= 0; --col) {
        if (count >= goal)
            return;
        for (std::int32_t z = rect[1]; z <= rect[3]; ++z) {
            InsertCell(set, Cell{static_cast<std::uint16_t>(col), static_cast<std::uint16_t>(z)});
            ++count;
        }
    }
}

// 0x005299C0, the kind 2: a site of the situation's class (slot 38,
// 0x00528360); its lot (0x00B43D08's slot 14); the lot's cells (its slot
// 14 rectangle) added column by column, a column cut at the situation's
// slot 43 limit (the next column still gets one); then 0x00528140 to slot
// 42's goal.
void __fastcall FillLotCells(char* self, void*)
{
    static bool logged;
    FirstCall("FillLotCells", logged);
    const std::uint32_t kind = Virtual<std::uint32_t>(Situation(self), 0x98);
    void* const site = CallThis<void*>(addr::cSC4CitySituationManager_PickSite, self, kind);
    if (!site)
        return;
    AddRef(site);
    if (void* const lots = Global<void*>(addr::SC4_LotManager)) {
        if (void* const lot = Virtual<void*>(lots, 0x38, site)) {
            const std::uint32_t goal = Virtual<std::uint32_t>(Situation(self), 0xA8);
            const std::uint32_t limit = Virtual<std::uint32_t>(Situation(self), 0xAC);
            std::int32_t rect[4] = {};
            Virtual<void>(lot, 0x38, static_cast<void*>(rect));
            std::uint32_t count = 0;
            char* const set = CellSet(self);
            for (std::int32_t x = rect[0]; x <= rect[2]; ++x)
                for (std::int32_t z = rect[1]; z <= rect[3]; ++z) {
                    InsertCell(set, Cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(z)});
                    ++count;
                    if (limit && count >= limit)
                        break;
                }
            CallThis<void>(addr::cSC4CitySituationManager_ExtendLotCells, self, static_cast<const void*>(rect), count, // NEW
                           goal);
        }
    }
    Release(site);
}

// 0x00529680, the kind 3: an occupant of the situation's type (slot 39) that
// the network service finds in a box around the automaton (its position
// plus or minus the squared slot 9, any height; the filter 0x00A97D94 with
// the type and the squared slot 8 as its least distance); one at random
// (0x00526380) whose 0xA821EF94 interface answers its cell (slot 59): the
// cell added, then floods (0x00527D30) from it in four directions from a
// random one, for a count between slots 42 and 43 (8 to 50).
void __fastcall FillFloodCells(char* self, void*)
{
    static bool logged;
    FirstCall("FillFloodCells", logged);
    const double outer = Virtual<double>(Situation(self), 0x24);
    const float outerSq = static_cast<float>(outer * outer);
    const double inner = Virtual<double>(Situation(self), 0x20);
    const float innerSq = static_cast<float>(inner * inner);
    void* const network = Global<void*>(addr::SC4_NetworkService);
    if (network)
        AddRef(network);
    const std::uint32_t type = Virtual<std::uint32_t>(Situation(self), 0x9C);
    char* filter = static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(0x20)));
    if (filter) {
        CallThis<void>(addr::SC4_OccupantFilterBase, filter);
        At<std::uint32_t>(filter, 0xC) = 0;
        At<std::uint32_t>(filter, 0x10) = 0;
        At<std::uintptr_t>(filter, 0) = addr::cSC4CitySituationManager_RadiusFilterVtable0;   // NEW 0x00A97D94
        At<std::uintptr_t>(filter, 4) = addr::cSC4CitySituationManager_RadiusFilterVtable4;  // NEW 0x00A97D7C
        At<std::uint32_t>(filter, 0x14) = ZeroBits();
        At<std::uint32_t>(filter, 0x18) = ZeroBits();
        At<std::uint32_t>(filter, 0x1C) = ZeroBits();
        AddRef(filter);
    }
    At<std::uint32_t>(filter, 0xC) = type;
    At<float>(filter, 0x10) = innerSq;
    Virtual<void>(Automaton(self), 0x1C, static_cast<void*>(filter + 0x14));
    Point low = ZeroPoint();
    Point high = ZeroPoint();
    Virtual<void>(Automaton(self), 0x1C, static_cast<void*>(&low));
    Virtual<void>(Automaton(self), 0x1C, static_cast<void*>(&high));
    low.v[0] = static_cast<float>(static_cast<double>(low.v[0]) - static_cast<double>(outerSq));
    std::uint32_t lowest = 0xFF7FFFFFu, highest = 0x7F7FFFFFu;
    std::memcpy(&low.v[1], &lowest, 4);
    low.v[2] = static_cast<float>(static_cast<double>(low.v[2]) - static_cast<double>(outerSq));
    high.v[0] = static_cast<float>(static_cast<double>(high.v[0]) + static_cast<double>(outerSq));
    std::memcpy(&high.v[1], &highest, 4);
    high.v[2] = static_cast<float>(static_cast<double>(high.v[2]) + static_cast<double>(outerSq));
    char* list = NewList(addr::SC4List_NodeAllocator2);
    if (Virtual<std::uint8_t>(network, 0x90, static_cast<void*>(&list), static_cast<void*>(&low), static_cast<void*>(&high),
                              static_cast<void*>(filter), 0xFFFFFFFFu) &&
        At<char*>(list, 0) != list) {
        void* const grid = Global<void*>(addr::SC4_Service43D54);
        if (grid)
            AddRef(grid);
        void* holder = nullptr;
        if (void* const picked = CallThis<void*>(addr::cSC4CitySituationManager_PickRandom, self, static_cast<void*>(&list)))
            if (Virtual<std::uint8_t>(picked, slot::kQueryInterface, 0xA821EF94u, static_cast<void*>(&holder))) {
                std::int32_t cx = 0, cz = 0;
                Virtual<void>(holder, 0xEC, static_cast<void*>(&cx), static_cast<void*>(&cz));
                const Cell start{static_cast<std::uint16_t>(cx), static_cast<std::uint16_t>(cz)};
                InsertResult out{};
                CallThis<void*>(addr::cSC4CitySituationManager_CellSetInsert, CellSet(self), static_cast<void*>(&out),
                                static_cast<const void*>(&start));
                void* const situation = Situation(self);
                const std::uint32_t most = Virtual<std::uint32_t>(situation, 0xAC);
                const std::uint32_t least = Virtual<std::uint32_t>(situation, 0xA8);
                std::int32_t remaining =
                    CallThis<std::int32_t>(addr::cRZRandomSuper_Between, self + field::kRandom, least, most);
                if (static_cast<std::uint32_t>(remaining) < 8u)
                    remaining = 8;
                else if (static_cast<std::uint32_t>(remaining) > 0x32u)
                    remaining = 0x32;
                const std::uint8_t first =
                    static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::SC4Random_Below, self + field::kRandom, 4u));
                for (std::uint8_t i = 0; i < 4; ++i) {
                    if (remaining == 0)
                        break;
                    std::int32_t d = static_cast<std::int32_t>((static_cast<std::uint32_t>(i) + first) & 0x80000003u);
                    if (d < 0)
                        d = ((d - 1) | static_cast<std::int32_t>(0xFFFFFFFCu)) + 1;
                    using Flood = void(__thiscall*)(void*, Cell, std::uint32_t, std::uint32_t, std::int32_t*);
                    reinterpret_cast<Flood>(addr::cSC4CitySituationManager_FloodCells)(self, start, static_cast<std::uint32_t>(d),
                                                                                      type, &remaining);
                }
            }
        Virtual<void>(network, 0xC0, static_cast<void*>(&list));
        if (holder)
            Release(holder);
        if (grid)
            Release(grid);
    }
    CallThis<void>(addr::SC4List_Destroy2, static_cast<void*>(&list));
    Release(filter);
    Release(network);
}

// 0x0052B450, the kinds 1, 5 and 6: a region of cells of the situation's
// zone type (0x00B43D14's slot 5 grid slot 7; for the kind 1 slot 37's
// type, else 0) found by a spiral walk from the automaton's cell inside the
// box of the situation's slot 9 radius (below 1.0: the city's slot 91 times
// its width), outside the box of slot 8: the kind 6 by 0x00527EA0, else a
// second spiral walk from the cell over the city that keeps cells of the
// type that are not water (the terrain's slot 34) and, for the kind 5, hold
// no occupant (0x00B43D54's slot 14), and shrinks its bounds at each other
// cell. The first region whose count is from slot 42 to slot 43 (at most the
// city's cells) becomes the set +0xAA8 (0x005271D0).
void __fastcall FindRegionCells(char* self, void*)
{
    static bool logged;
    FirstCall("FindRegionCells", logged);
    void* const zones = Virtual<void*>(Global<void*>(addr::SC4_Service43D14), 0x14);
    void* const occupants = Global<void*>(addr::SC4_Service43D54);
    void* const city = Global<void*>(addr::SC4_City);
    const std::int32_t width = Virtual<std::int32_t>(city, 0x174);
    const std::int32_t height = Virtual<std::int32_t>(city, 0x178);
    const float inner = static_cast<float>(Virtual<double>(Situation(self), 0x20));
    const double outerRaw = Virtual<double>(Situation(self), 0x24);
    float outer = static_cast<float>(outerRaw);
    if (outerRaw < static_cast<double>(FloatAt(addr::cSC4CitySituationManager_One)))
        outer = static_cast<float>(Virtual<double>(city, 0x16C) * static_cast<double>(width));
    const std::uint32_t kind = Virtual<std::uint32_t>(Situation(self), 0x90);
    const std::int32_t want = kind == 1 ? Virtual<std::int32_t>(Situation(self), 0x94) : 0;
    const bool water = kind == 6;
    const bool empty = kind == 5;
    std::uint32_t least = Virtual<std::uint32_t>(Situation(self), 0xA8);
    std::uint32_t most = Virtual<std::uint32_t>(Situation(self), 0xAC);
    const std::uint32_t cells = static_cast<std::uint32_t>(height) * static_cast<std::uint32_t>(width);
    if (most == 0 || most > cells)
        most = cells;
    if (least > most)
        least = most;
    Point pos = ZeroPoint();
    Virtual<void>(Automaton(self), 0x1C, static_cast<void*>(&pos));
    std::int32_t cx = 0, cz = 0;
    Virtual<void>(city, 0x17C, pos.v[0], pos.v[2], static_cast<void*>(&cx), static_cast<void*>(&cz));
    auto cellOf = [&](double x, double z, std::int32_t* ox, std::int32_t* oz) {
        Virtual<void>(city, 0x17C, static_cast<float>(x), static_cast<float>(z), static_cast<void*>(ox),
                      static_cast<void*>(oz));
    };
    std::int32_t ox0 = 0, oz0 = 0, ox1 = 0, oz1 = 0, ix0 = 0, iz0 = 0, ix1 = 0, iz1 = 0;
    cellOf(static_cast<double>(pos.v[0]) - outer, static_cast<double>(pos.v[2]) - outer, &ox0, &oz0);
    cellOf(static_cast<double>(pos.v[0]) + outer, static_cast<double>(pos.v[2]) + outer, &ox1, &oz1);
    cellOf(static_cast<double>(pos.v[0]) - inner, static_cast<double>(pos.v[2]) - inner, &ix0, &iz0);
    cellOf(static_cast<double>(pos.v[0]) + inner, static_cast<double>(pos.v[2]) + inner, &ix1, &iz1);
    ox0 = Clamp(ox0, width - 1);
    oz0 = Clamp(oz0, height - 1);
    ox1 = Clamp(ox1, width - 1);
    oz1 = Clamp(oz1, height - 1);
    ix0 = Clamp(ix0, width - 1);
    iz0 = Clamp(iz0, height - 1);
    ix1 = Clamp(ix1, width - 1);
    iz1 = Clamp(iz1, height - 1);
    SpiralWalk walk{};
    walk.minX = static_cast<std::int32_t>(0x80000000u);
    walk.minZ = static_cast<std::int32_t>(0x80000000u);
    walk.maxX = 0x7FFFFFFF;
    walk.maxZ = 0x7FFFFFFF;
    walk.b20 = 1;
    walk.count = 0;
    WalkStart(&walk, 0, 0);
    walk.minX = ox0;
    walk.minZ = oz0;
    walk.maxX = ox1 < ox0 ? ox0 : ox1;
    walk.maxZ = oz1 < oz0 ? oz0 : oz1;
    WalkStart(&walk, cx, cz);
    std::int32_t x = 0, z = 0;
    while (WalkNext(&walk, &x, &z)) {
        if (x >= ix0 && x <= ix1 && z >= iz0 && z <= iz1)
            continue;
        if (static_cast<std::int32_t>(static_cast<std::int8_t>(Virtual<std::uint8_t>(zones, 0x1C, x, z))) != want)
            continue;
        HashSet set{};
        const auto* primes = reinterpret_cast<const std::uint32_t*>(addr::SC4HashTable_Primes);
        std::int32_t n = 0x1C;
        while (n > 0) {
            const std::int32_t half = n >> 1;
            if (primes[half] < 100u) {
                primes += half + 1;
                n += -1 - half;
            } else {
                n = half;
            }
        }
        const std::uint32_t buckets =
            primes == reinterpret_cast<const std::uint32_t*>(addr::SC4HashTable_PrimesEnd) ? 0xFFFFFFFBu : *primes;
        CallThis<void>(addr::PtrVector_Reserve, static_cast<void*>(&set.begin), buckets);
        void* const null = nullptr;
        CallThis<void>(addr::SC4PtrVector_FillInsert, static_cast<void*>(&set.begin), set.end, buckets,
                       static_cast<void* const*>(&null));
        set.count = 0;
        std::uint32_t found = 0;
        if (water) {
            CallThis<void>(addr::cSC4CitySituationManager_RegionFill, self, x, z, want, static_cast<void*>(&set), // NEW
                           static_cast<void*>(&found), most, width, height, zones);
        } else {
            SpiralWalk region{};
            region.x = x;
            region.z = z;
            region.x2 = x + 1;
            region.z2 = z;
            region.b20 = 1;
            region.step = 1;
            region.state = 0;
            region.first = 1;
            region.end = 0;
            region.count = 0;
            std::int32_t loX = 0, hiX = width - 1, loZ = 0, hiZ = height - 1;
            region.minX = loX;
            region.minZ = loZ;
            region.maxX = hiX < loX ? loX : hiX;
            region.maxZ = hiZ < loZ ? loZ : hiZ;
            std::int32_t rx = 0, rz = 0;
            while (WalkNext(&region, &rx, &rz)) {
                if (found >= most)
                    break;
                bool keep = false;
                if (static_cast<std::int32_t>(static_cast<std::int8_t>(Virtual<std::uint8_t>(zones, 0x1C, rx, rz))) == want &&
                    !Virtual<std::uint8_t>(Global<void*>(addr::SC4_Terrain), 0x88, rx, rz))
                    keep = !empty || !Virtual<void*>(occupants, 0x38, rx, rz);
                if (keep) {
                    InsertCell(reinterpret_cast<char*>(&set), Cell{static_cast<std::uint16_t>(rx), static_cast<std::uint16_t>(rz)});
                    ++found;
                    continue;
                }
                if (rx > x)
                    hiX = rx;
                else if (rx < x)
                    loX = rx;
                if (rz > z)
                    hiZ = rz;
                else if (rz < z)
                    loZ = rz;
                region.minX = loX;
                region.minZ = loZ;
                region.maxX = hiX < loX ? loX : hiX;
                region.maxZ = hiZ < loZ ? loZ : hiZ;
            }
        }
        if (found >= least && found <= most) {
            char* const target = CellSet(self);
            if (reinterpret_cast<char*>(&set) != target) {
                CallThis<void>(addr::SC4Uint32HashSet_Clear, target);
                CallThis<void>(addr::cSC4CitySituationManager_CellSetAssign, target, static_cast<void*>(&set));
            }
            CallThis<void>(addr::SC4Uint32HashSet_Clear, static_cast<void*>(&set));
            if (set.begin)
                Free(set.begin);
            return;
        }
        CallThis<void>(addr::SC4Uint32HashSet_Clear, static_cast<void*>(&set));
        if (set.begin)
            Free(set.begin);
    }
}

// 0x0052C4E0: the cell set +0xAA8 emptied and made by the situation's cell
// kind (slot 36): 1, 5, 6 0x0052B450; 2 0x005299C0; 3 0x00529680; 4 the
// rectangle of slot 40 (uint16 halves); 7 the offsets of slot 41's list
// (+8, +0xA int16) from the city's middle, new cells only. +0xAC0 the
// count, +0xABC 0; with cells and 0x00B43D1C each cell gets the marker
// effect (0x00B09AE0, the water one 0x00B09AE8 for the kind 6) over its
// rectangle into the map +0xAC4, and +0xAD8 is the middle of the least and
// greatest x and the least z (twice), where the target goes (0x00529560).
// Whether there are cells (low byte 0 without a situation or automaton).
std::uint32_t __fastcall FillCells(char* self, void*)
{
    static bool logged;
    FirstCall("FillCells", logged);
    char* const set = CellSet(self);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
    if (!Situation(self) || !Automaton(self))
        return 0;
    switch (Virtual<std::uint32_t>(Situation(self), 0x90)) {
    case 1:
    case 5:
    case 6:
        CallThis<void>(addr::cSC4CitySituationManager_FindRegionCells, self); // NEW
        break;
    case 2:
        CallThis<void>(addr::cSC4CitySituationManager_FillLotCells, self); // NEW
        break;
    case 3:
        CallThis<void>(addr::cSC4CitySituationManager_FillFloodCells, self); // NEW
        break;
    case 4: {
        const std::uint32_t* const rect = Virtual<const std::uint32_t*>(Situation(self), 0xA0);
        const std::uint32_t a = rect[0], b = rect[1];
        const std::uint16_t x0 = static_cast<std::uint16_t>(a), z0 = static_cast<std::uint16_t>(a >> 16);
        const std::uint16_t x1 = static_cast<std::uint16_t>(b), z1 = static_cast<std::uint16_t>(b >> 16);
        for (std::uint16_t x = x0; x <= x1; ++x)
            for (std::uint16_t z = z0; z <= z1; ++z)
                InsertCell(set, Cell{x, z});
        break;
    }
    case 7: {
        void* const city = Global<void*>(addr::SC4_City);
        const std::int32_t halfX = Virtual<std::int32_t>(city, 0x174) / 2;
        const std::int32_t halfZ = Virtual<std::int32_t>(city, 0x178) / 2;
        CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
        char* const head = *Virtual<char**>(Situation(self), 0xA4);
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
            const Cell cell{static_cast<std::uint16_t>(At<std::uint16_t>(node, 8) + halfX),
                            static_cast<std::uint16_t>(At<std::uint16_t>(node, 0xA) + halfZ)};
            if (!CallThis<std::uint32_t>(addr::cSC4CitySituationManager_CellSetCount, set, static_cast<const void*>(&cell))) {
                InsertResult out{};
                CallThis<void*>(addr::cSC4CitySituationManager_CellSetInsert, set, static_cast<void*>(&out),
                                static_cast<const void*>(&cell));
            }
        }
        break;
    }
    default:
        break;
    }
    const std::uint32_t count = At<std::uint32_t>(self, field::kSetAA8Count);
    At<std::uint32_t>(self, field::kZeroAC0) = count;
    At<std::uint32_t>(self, field::kZeroABC) = 0;
    void* const effects = Global<void*>(addr::SC4_Service43D1C);
    if (count && effects) {
        void* effect = nullptr;
        Iterator first{};
        CallThis<void*>(addr::SC4HashTable_Begin, set, static_cast<void*>(&first));
        std::int32_t minX = At<std::uint16_t>(first.node, 4), maxX = minX;
        std::int32_t minZ = At<std::uint16_t>(first.node, 6), maxZ = minZ;
        Iterator it{};
        CallThis<void*>(addr::SC4HashTable_Begin, set, static_cast<void*>(&it));
        for (char* node = it.node; node;) {
            const std::int32_t x = At<std::uint16_t>(node, 4);
            const std::int32_t z = At<std::uint16_t>(node, 6);
            if (x < minX)
                minX = x;
            if (x > maxX)
                maxX = x;
            if (z < minZ)
                minZ = z;
            if (z < maxZ) // the game's: the "max" keeps the least z
                maxZ = z;
            const char* name = Virtual<std::uint32_t>(Situation(self), 0x90) == 6
                                   ? Global<const char*>(addr::cSC4CitySituationManager_WaterMarkerEffectName) // NEW
                                   : Global<const char*>(addr::cSC4CitySituationManager_MarkerEffectName);      // NEW
            if (void* const old = effect) {
                effect = nullptr;
                Release(old);
            }
            if (Virtual<std::uint8_t>(effects, 0x1C, name, static_cast<void*>(&effect))) {
                const float rect[4] = {static_cast<float>(x), static_cast<float>(z), static_cast<float>(x + 1),
                                       static_cast<float>(z + 1)};
                Virtual<void>(effect, 0x30, static_cast<std::uint32_t>(0xC), static_cast<const void*>(rect),
                              static_cast<std::uint32_t>(4));
                Virtual<void>(effect, 0xC, static_cast<std::uint32_t>(0));
                struct {
                    Cell cell;
                    void* object;
                } value{{At<std::uint16_t>(node, 4), At<std::uint16_t>(node, 6)}, effect};
                if (value.object)
                    AddRef(value.object);
                char* const map = self + field::kObjects;
                CallThis<void>(addr::cSC4CitySituationManager_CellSetResize, map, At<std::uint32_t>(map, 0x10) + 1);
                InsertResult out{};
                CallThis<void*>(addr::cSC4CitySituationManager_CellMapInsertUnique, map, static_cast<void*>(&out),
                                static_cast<const void*>(&value));
                if (value.object)
                    Release(value.object);
            }
            node = At<char*>(node, 0);
            if (!node)
                node = CallThis<char*>(addr::cSC4CitySituationManager_CellSetNextBucket, static_cast<void*>(&it));
            it.node = node;
        }
        At<std::uint16_t>(self, field::kCell) = static_cast<std::uint16_t>((maxX - minX) / 2 + minX);
        At<std::uint16_t>(self, field::kCell + 2) = static_cast<std::uint16_t>((maxZ - minZ) / 2 + minZ);
        CallThis<void>(addr::cSC4CitySituationManager_PlaceCellTarget, self);
        if (effect)
            Release(effect);
    }
    return At<std::uint32_t>(self, field::kZeroAC0) != 0 ? 1u : 0u;
}

// 0x0052B200 (ret 4): the target made for the situation: the find_target
// script (+0xAEC, 0x00524F00 with the target IDs 0x00529AF0), else for each
// service (+0x9F8) and target ID, the service's slot 16 (the ID, the
// automaton's position, slot 8, slot 9, a place out, 0x2010002, 0) then
// slot 15 there (... 0xFF, 0); the target is what the made one's slot 5
// answers. With `assign` it becomes the target (0x00528580); +0xA64 set.
// Answered after its release.
void* __fastcall FindTarget(char* self, void*, std::uint8_t assign)
{
    static bool logged;
    FirstCall("FindTarget", logged);
    void* found = nullptr;
    if (!Situation(self) || !Automaton(self))
        return nullptr;
    Vector ids{};
    CallThis<void>(addr::cSC4CitySituationManager_GetTargetIds, self, static_cast<void*>(&ids));
    bool fromScript = false;
    if (At<std::int32_t>(self, 0xAEC) != -2) {
        Assign(&found, CallThis<void*>(addr::cSC4CitySituationManager_CallTargetScript, self, At<std::uint32_t>(self, 0xAEC),
                                       static_cast<void*>(&ids)));
        fromScript = found != nullptr;
    }
    if (!fromScript) {
        found = nullptr;
        void* made = nullptr;
        void** const services = At<void**>(self, field::kServices);
        void** const servicesEnd = At<void**>(self, field::kServices + 4);
        for (void** it = services; it != servicesEnd; ++it) {
            if (made)
                break;
            void* const service = *it;
            if (service)
                AddRef(service);
            Point from = ZeroPoint();
            Virtual<void>(Automaton(self), 0x1C, static_cast<void*>(&from));
            Point to = ZeroPoint();
            for (std::uint32_t* id = ids.begin; id != ids.end && !made; ++id) {
                void* const situation = Situation(self);
                const float outerR = static_cast<float>(Virtual<double>(situation, 0x24));
                const float innerR = static_cast<float>(Virtual<double>(situation, 0x20));
                if (Virtual<std::uint8_t>(service, 0x40, *id, static_cast<void*>(&from), innerR, outerR, static_cast<void*>(&to),
                                          0x2010002u, 0u)) {
                    std::uint32_t extra[3] = {};
                    if (void* const automaton = Virtual<void*>(service, 0x3C, *id, static_cast<void*>(&to), 0x2010002u,
                                                               static_cast<void*>(extra), 0u, 0xFFu, 0u)) {
                        AddRef(automaton);
                        made = automaton;
                    }
                }
            }
            if (service)
                Release(service);
        }
        if (made) {
            if (void* const target = Virtual<void*>(made, 0x14)) {
                AddRef(target);
                found = target;
            }
            Release(made);
        }
    }
    if (assign) {
        if (found)
            CallThis<void>(addr::cSC4CitySituationManager_SetTargetOccupant, self, found);
    }
    if (found)
        At<std::uint8_t>(self, field::kByteA64) = 1;
    if (ids.begin)
        Free(ids.begin);
    if (found)
        Release(found);
    return found;
}

// 0x00554EA0 (this the automaton's 0x8B99EE7C interface, +0x40 x, +0x48 z):
// the occupant on the link (0x00B43D8C's slot 17 lists 3, 0xC, 8, 2 and
// 0xA) with the shortest path (0x00B43DA8's slot 6 path finder, slots 19,
// 13, 18, 5, 9, 8, 20, 23, 22) from the automaton's cell; its cell's first
// occupant (0x00B43D54's slot 14). Answered after its release, or null.
void* __fastcall NearestLinkOccupant(char* self, void*)
{
    static bool logged;
    FirstCall("NearestLinkOccupant", logged);
    void* const links = Global<void*>(addr::SC4_MonthEndService);
    void* const city = Global<void*>(addr::SC4_City);
    void* const finders = Global<void*>(addr::SC4_Service43DA8);
    void* result = nullptr;
    if (!city || !links || !finders)
        return nullptr;
    void* finder = nullptr;
    std::int32_t best = 0x7FFFFFFF;
    void* bestLink = nullptr;
    Virtual<void>(finders, 0x18, static_cast<void*>(&finder));
    if (!finder)
        return nullptr;
    const std::int32_t width = Virtual<std::int32_t>(city, 0x174);
    const std::int32_t height = Virtual<std::int32_t>(city, 0x178);
    std::int32_t cx = 0, cz = 0;
    Virtual<void>(city, 0x17C, At<float>(self, 0x40), At<float>(self, 0x48), static_cast<void*>(&cx), static_cast<void*>(&cz));
    alignas(8) char route[0x28] = {};
    std::uint8_t dummy = 0;
    CallThis<void>(addr::SC4RouteDeque_Construct, static_cast<void*>(route), static_cast<void*>(&dummy));
    char* all = NewList(addr::SC4List_NodeAllocator3);
    char* some = NewList(addr::SC4List_NodeAllocator3);
    Virtual<void>(links, 0x44, 3u, static_cast<void*>(&all));
    static const std::uint32_t kMore[4] = {0xCu, 8u, 2u, 0xAu};
    for (const std::uint32_t type : kMore) {
        Virtual<void>(links, 0x44, type, static_cast<void*>(&some));
        CallThis<void>(addr::SC4List_InsertRangeAt, static_cast<void*>(&all), static_cast<void*>(all), // NEW 0x00553830
                       static_cast<void*>(At<char*>(some, 0)), static_cast<void*>(some));
    }
    std::int32_t bestRect[4] = {};
    for (char* node = At<char*>(all, 0); node != all; node = At<char*>(node, 0)) {
        void* const link = At<void*>(node, 8);
        std::int32_t to[4] = {};
        Virtual<void>(link, 0x18, static_cast<void*>(&to[0]), static_cast<void*>(&to[1]));
        if (to[0] < 0)
            to[0] = 0;
        else if (to[0] >= width)
            to[0] = width - 1;
        if (to[1] < 0)
            to[1] = 0;
        else if (to[1] >= height)
            to[1] = height - 1;
        to[2] = to[0];
        to[3] = to[1];
        Virtual<void>(finder, 0x4C, cx, cz, cx, cz);
        Virtual<void>(finder, 0x34, 2u, 1u);
        Virtual<void>(finder, 0x48, 0u);
        Virtual<void>(finder, 0x14, 1u);
        Virtual<void>(finder, 0x24, 3u);
        Virtual<void>(finder, 0x20, static_cast<void*>(route));
        Virtual<void>(finder, 0x50, to[0], to[1], to[2], to[3]);
        if (Virtual<std::int32_t>(finder, 0x5C) == 1) {
            const std::int32_t length = CallThis<std::int32_t>(addr::SC4CellDeque_Size, static_cast<void*>(route));
            if (length < best) {
                Assign(&bestLink, link);
                best = length;
                std::memcpy(bestRect, to, sizeof bestRect);
            }
        }
        Virtual<void>(finder, 0x58);
        CallThis<void>(addr::SC4CellDeque_Clear, static_cast<void*>(route));
    }
    CallThis<void>(addr::SC4List_ClearReleasing, static_cast<void*>(&all));
    Release(finder);
    if (bestLink)
        if (void* const occupants = Global<void*>(addr::SC4_Service43D54))
            if (void** const there = Virtual<void**>(occupants, 0x38, bestRect[0], bestRect[1]))
                Assign(&result, *there);
    CallThis<void>(addr::SC4List_Destroy3, static_cast<void*>(&some));
    CallThis<void>(addr::SC4List_Destroy3, static_cast<void*>(&all));
    CallThis<void>(addr::SC4RouteDeque_Destroy, static_cast<void*>(route));
    if (bestLink)
        Release(bestLink);
    if (result)
        Release(result);
    return result;
}

// 0x0052AD00 (ret 4): the next target, by the situation's kind (slot 4),
// with the target IDs (0x00529AF0) as groups; first, with the flag 0x20 and
// 0x193A among the IDs, the +0xAF8 pick (0x00528360). 1 and 3: a candidate
// (0x005245F0) of the groups (0x00525640) between the squared radii (slot
// 8, only for the first target; slot 9) of the automaton (the automaton's
// height), its slot 5 (the game asks the set +0xA68 for its still-null
// result, not the candidate); else at random (0x00526380) one the network
// service finds in the slot-9 box with the group filter (0x00526610) that
// +0xA68 does not hold. 2: 0x00554EA0 of the automaton's 0x8B99EE7C
// interface, its slot 48. 4: the create_target script (+0xAE8). With
// `assign` the target is set (0x00528580). Answered after its release.
void* __fastcall StartTarget(char* self, void*, std::uint8_t assign)
{
    static bool logged;
    FirstCall("StartTarget", logged);
    if (!Situation(self) || !Automaton(self))
        return nullptr;
    void* result = nullptr;
    Vector ids{};
    CallThis<void>(addr::cSC4CitySituationManager_GetTargetIds, self, static_cast<void*>(&ids));
    Point at = ZeroPoint();
    Point there = ZeroPoint();
    Virtual<void>(Automaton(self), 0x1C, static_cast<void*>(&at));
    bool done = false;
    if (Virtual<std::uint8_t>(Situation(self), 0x3C, 0x20u)) {
        std::uint8_t dummy = 0;
        const std::uint32_t* const hit = CallCdecl<const std::uint32_t*>(
            addr::SC4_FindUint32, ids.begin, ids.end, reinterpret_cast<const std::uint32_t*>(addr::cSC4CitySituationManager_SiteClass), // NEW 0x00A97C10
            static_cast<void*>(&dummy));
        if (hit != ids.end) {
            Assign(&result, CallThis<void*>(addr::cSC4CitySituationManager_PickSite, self, 0x193Au));
            done = result != nullptr;
        }
    }
    if (!done) {
        const double raw = Virtual<double>(Situation(self), 0x24);
        const float radius = static_cast<float>(raw);
        const float farSq = static_cast<float>(raw * static_cast<double>(radius));
        float nearSq = 0.0f;
        if (At<std::uint32_t>(self, field::kTargetIndex) == 0) {
            const double inner = Virtual<double>(Situation(self), 0x20);
            nearSq = static_cast<float>(inner * inner);
        }
        switch (Virtual<std::uint32_t>(Situation(self), 0x10)) {
        case 1:
        case 3: {
            HashSet set{};
            CallThis<void>(addr::SC4Uint32HashSet_ConstructDefault, static_cast<void*>(&set));
            CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GatherCandidates, self, static_cast<void*>(&ids),
                                   static_cast<void*>(&set));
            Iterator it{};
            CallThis<void*>(addr::SC4HashTable_Begin, static_cast<void*>(&set), static_cast<void*>(&it));
            for (char* node = it.node; node && !result;) {
                void* const occupant = At<void*>(node, 4);
                if (CallThis<std::uint8_t>(addr::cSC4CitySituationManager_IsCandidate, self, occupant)) {
                    Virtual<void>(occupant, 0x1C, static_cast<void*>(&there));
                    there.v[1] = at.v[1];
                    const double d = CallCdecl<double>(addr::SC4_DistanceSquared3, static_cast<const float*>(there.v),
                                                       static_cast<const float*>(at.v));
                    if (!(d < static_cast<double>(nearSq)) && d <= static_cast<double>(farSq) &&
                        !CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Count, self + field::kRefSet,
                                                 static_cast<void*>(&result))) {
                        Assign(&result, Virtual<void*>(occupant, 0x14));
                    }
                }
                node = At<char*>(node, 0);
                if (!node)
                    node = CallThis<char*>(addr::SC4HashTable_NextBucket, static_cast<void*>(&it));
                it.node = node;
            }
            Virtual<void>(Global<void*>(addr::SC4_Service43D44), 0x84, static_cast<void*>(&set));
            if (!result) {
                void* const network = Global<void*>(addr::SC4_NetworkService);
                if (network)
                    AddRef(network);
                char* list = NewList(addr::SC4List_NodeAllocator2);
                float low[3], high[3];
                const std::uint32_t lowest = 0xFF7FFFFFu, highest = 0x7F7FFFFFu;
                low[0] = static_cast<float>(static_cast<double>(at.v[0]) - static_cast<double>(radius));
                std::memcpy(&low[1], &lowest, 4);
                low[2] = static_cast<float>(static_cast<double>(at.v[2]) - static_cast<double>(radius));
                high[0] = static_cast<float>(static_cast<double>(at.v[0]) + static_cast<double>(radius));
                std::memcpy(&high[1], &highest, 4);
                high[2] = static_cast<float>(static_cast<double>(at.v[2]) + static_cast<double>(radius));
                void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(0x28));
                char* filter = nullptr;
                if (block) {
                    filter = CallThis<char*>(addr::cSC4CitySituationManager_SpawnFilterConstruct, block);
                    if (filter)
                        AddRef(filter);
                }
                Virtual<void>(filter, 0x20, static_cast<void*>(&ids));
                std::memcpy(filter + 0x1C, at.v, sizeof at.v);
                At<float>(filter, 0x18) = nearSq;
                if (Virtual<std::uint8_t>(network, 0x90, static_cast<void*>(&list), static_cast<void*>(low),
                                          static_cast<void*>(high), static_cast<void*>(filter), 0xFFFFFFFFu)) {
                    if (At<char*>(list, 0) != list) {
                        for (char* node = At<char*>(list, 0); node != list;) {
                            void* const occupant = At<void*>(node, 8);
                            if (occupant)
                                AddRef(occupant);
                            void* key = occupant;
                            const bool held = CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Count, self + field::kRefSet,
                                                                      static_cast<void*>(&key)) != 0;
                            if (occupant)
                                Release(occupant);
                            if (held) {
                                char* next = nullptr;
                                CallThis<void*>(addr::SC4List_Erase, static_cast<void*>(&list), static_cast<void*>(&next),
                                                static_cast<void*>(node));
                                node = next;
                            } else {
                                node = At<char*>(node, 0);
                            }
                        }
                        Assign(&result, CallThis<void*>(addr::cSC4CitySituationManager_PickRandom, self,
                                                        static_cast<void*>(&list)));
                    }
                }
                Virtual<void>(network, 0xC0, static_cast<void*>(&list));
                Release(filter);
                CallThis<void>(addr::SC4List_Destroy2, static_cast<void*>(&list));
                Release(network);
            }
            CallThis<void>(addr::SC4Uint32HashSet_Clear, static_cast<void*>(&set));
            if (set.begin)
                Free(set.begin);
            break;
        }
        case 2: {
            void* face = nullptr;
            void* const out = CallThis<void*>(addr::SC4RefPtr_Reset, static_cast<void*>(&face));
            if (Virtual<std::uint8_t>(Automaton(self), slot::kQueryInterface, 0x8B99EE7Cu, out))
                if (void* const occupant = CallThis<void*>(addr::cSC4CitySituationManager_NearestLinkOccupant, face)) // NEW
                    Assign(&result, Virtual<void*>(occupant, 0xC0));
            if (face)
                Release(face);
            break;
        }
        case 4:
            if (At<std::int32_t>(self, 0xAE8) != -2)
                Assign(&result, CallThis<void*>(addr::cSC4CitySituationManager_CallTargetScript, self,
                                                At<std::uint32_t>(self, 0xAE8), static_cast<void*>(&ids)));
            break;
        default:
            break;
        }
    }
    if (assign && result)
        CallThis<void>(addr::cSC4CitySituationManager_SetTargetOccupant, self, result);
    if (ids.begin)
        Free(ids.begin);
    if (result)
        Release(result);
    return result;
}

// 0x0052E750 (ret 0xC), slot 36: a situation started for an automaton.
// Nothing for the active one. Else: slot 38, the situation referenced; none
// answers 0. The status 4, +0xA88 0, +0xA99 0; with its flag 0x20 +0xAF8
// gets the city's slot 71's slot 12 of +0xAFC. The automaton (0x00525070);
// without one: the status 5 and 0xEBB67EF7 with 0. The scripts
// (0x00525800). The kind 2 starts at once, 5 with cells (0x0052C4E0); the
// others count their targets (slot 17's slot 16, else 1), the index 0, and
// make the first (0x0052B200 with the flag 0x100, else 0x0052AD00 and, when
// that fails with the flag 1, 0x0052B200); a target starts. Started and
// `notify`: 0xEBB67EF7 with the automaton; else the status 5 and 0.
std::uint8_t __fastcall StartSituation(char* self, void*, void* situation, void* automaton, std::uint8_t notify)
{
    static bool logged;
    FirstCall("StartSituation", logged);
    if (situation == Situation(self))
        return 0;
    Virtual<void>(self, slot::kEndSituation);
    void* const old = Situation(self);
    if (situation != old) {
        if (situation)
            AddRef(situation);
        At<void*>(self, field::kSituation) = situation;
        if (old)
            Release(old);
    }
    if (!Situation(self))
        return 0;
    At<std::uint32_t>(self, field::kStatus) = 4;
    At<std::uint32_t>(self, field::kDriveTime) = 0;
    At<std::uint8_t>(self, field::kByteA99) = 0;
    if (Virtual<std::uint8_t>(Situation(self), 0x3C, 0x20u)) {
        void* const pick = Virtual<void*>(At<void*>(self, field::kCity71), 0x30, static_cast<void*>(self + field::kZeroAFC));
        Assign(&At<void*>(self, field::kRefAF8), pick);
    }
    CallThis<void>(addr::cSC4CitySituationManager_SetActiveAutomaton, self, automaton);
    std::uint8_t started = 0;
    bool post = false;
    if (Automaton(self)) {
        CallThis<void>(addr::cSC4CitySituationManager_LoadScripts, self, Situation(self));
        const std::uint32_t kind = Virtual<std::uint32_t>(Situation(self), 0x10);
        if (kind == 2) {
            started = 1;
            post = true;
        } else if (kind == 5) {
            started = static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4CitySituationManager_FillCells, self)); // NEW
            post = started != 0;
        } else {
            At<std::uint32_t>(self, field::kNumTargets) = 1;
            if (Virtual<std::uint8_t>(Situation(self), 0x44)) {
                void* const* const targets = Virtual<void* const*>(Situation(self), 0x40);
                At<std::int32_t>(self, field::kNumTargets) = Count4(targets[0], targets[1]);
            }
            At<std::uint32_t>(self, field::kTargetIndex) = 0;
            if (Virtual<std::uint32_t>(Situation(self), 0x38) & 0x100u) {
                CallThis<void*>(addr::cSC4CitySituationManager_FindTarget, self, static_cast<std::uint32_t>(1)); // NEW
            } else if (!CallThis<void*>(addr::cSC4CitySituationManager_StartTarget, self, static_cast<std::uint32_t>(1)) &&
                       (Virtual<std::uint32_t>(Situation(self), 0x38) & 1u)) {
                CallThis<void*>(addr::cSC4CitySituationManager_FindTarget, self, static_cast<std::uint32_t>(1));
            }
            if (At<void*>(self, field::kTarget)) {
                started = 1;
                post = true;
            }
        }
    }
    if (post && notify) {
        CallThis<void>(addr::cSC4CitySituationManager_PostSituationMessage, self, 0xEBB67EF7u, Situation(self), Automaton(self));
        return started;
    }
    At<std::uint32_t>(self, field::kStatus) = 5;
    CallThis<void>(addr::cSC4CitySituationManager_PostSituationMessage, self, 0xEBB67EF7u, Situation(self),
                   static_cast<void*>(nullptr));
    return started;
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

const Detour kRunning[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_StartSituation, StartSituation),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_FillCells, FillCells),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_FindRegionCells, FindRegionCells),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_FindTarget, FindTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_StartTarget, StartTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_FillFloodCells, FillFloodCells),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_FillLotCells, FillLotCells),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ExtendLotCells, ExtendLotCells),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_FloodCells, FloodCells),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_RegionFill, RegionFill),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_NearestLinkOccupant, NearestLinkOccupant),
};

#undef OPENSC4_DETOUR

bool InstallRunning(registry::Mode) { return InstallList(kRunning, sizeof kRunning / sizeof kRunning[0], "running"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.situations.running", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_StartSituation,
                        "Our city situation manager's running situation: the start (slot 36), the "
                        "targets made (by kind, by the services, by script, by path to a link), the cell sets by kind "
                        "and their markers (11 detours; part c5, src/core/automata_situations_5.cpp)",
                        false, InstallRunning}),
};

} // namespace
} // namespace opensc4::automata_situations
