// Our watercraft manager's creation and routes, part aw5 (build 641): 13
// functions of cSC4WatercraftManager (vtable 0x00A9DF38) and of its boat
// (cSC4Watercraft, constructor 0x0056D9A0). The class map is in
// src/core/automata_awmgr.h, the tables and records in
// src/core/automata_awmgr_water.cpp (part aw4) and the survey in
// docs/automata.md, "Part aw". Calls between functions of the part, to the
// common base and to the frame functions of part aw5 go to the game's
// addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP (function
// addresses, hex, eight digits) bisects them.
//
// A boat's leg (0x34 bytes; +0x1E4 and +0x218 of the boat, two on the stack
// of 0x00570DF0): a reference +0 (the ferry route's +0), a byte vector +4
// (a route's packed steps), two points +0x10 and +0x1C (a place and a
// facing), a byte +0x28, a step count +0x2C and bytes +0x30 and +0x31 (set
// for a leg made from a dock). The route iterators are {node, table} pairs
// passed by value.
//
// Groups, off by default:
//   simulators.automata.awmgr.water.create (6): CreateAutomaton (slot 15),
//     GetRandomCreationPoint (16), the lot at an occupant 0x00567C90, a
//     lot's water body 0x00567CE0, the dock search 0x00568CE0 and the open
//     water test 0x00568FD0.
//   simulators.automata.awmgr.water.routes (7): a terminal's boat 0x005710A0
//     with the path group test 0x00566DA0 and the boat's path 0x0056F510; a
//     route record's legs 0x0056FD60, its new ferry 0x00570DF0, the boat's
//     two legs 0x0056F670 and a leg's construction 0x0056C940.
//
// Oddities kept:
//   - CreateAutomaton writes the point's z cell over its point argument's
//     slot and hands the common base a7 0 whatever the caller gave.
//   - GetRandomCreationPoint reads neither most nor flags and makes no draw;
//     its seventh argument takes the facing (a dock's) when not null. The
//     spiral's distance test (not below least) applies only when there is no
//     lot at the position; its water body is the lot's (0 without one) or
//     the first non-zero of the cells the walk passes.
//   - The dock search (0x00568CE0) has its interface query write into the
//     occupant's argument slot (ours too); its byte is 0 at every call in
//     the game, so its turned offset (a third of the extent to the side) is
//     never used.
//   - Argument slots the game uses as its own temporaries after their last
//     read (no caller reads them back): the dock search's point and
//     direction (the unit facing's x and y), the open water test's third
//     (its rounded distance), 0x0056FD60's record (its scale) and iterator
//     (its find), 0x0056F510's target (its limit), 0x005710A0's kind (its
//     byte's low byte). Ours keeps those values in locals (the find and the
//     byte in the parameters; clang may keep the by-value iterator in a copy).
//   - 0x00567CE0 answers AX 0 without a lot (the game keeps EAX's top half,
//     the caller's EAX on entry; ours 0: its one caller compares the word).
//   - 0x0056FD60's second leg keeps +0x28, +0x2C and +0x30 unwritten when the
//     route's byte vector has no open water step, as do 0x00570DF0's legs
//     beyond 0x0056C940's 0x28 bytes (stack leftovers that reach the boat's
//     +0x218 through 0x0056F670; ours 0).
//   - 0x005710A0 frees the path only on the paths the game does, and takes
//     the first object of the list whose water path is found.
//   - The dock search's orientation picks its facing from a table of four
//     on the stack; another value would read other stack slots in the game.
//     Ours logs a first-call line and takes 0.
//
// Floats: 53-bit doubles, as the game's x87 runs; floats stored where the
// game stores them (FST, FSTP), doubles where it keeps ST0. The products a
// point adds before its store are kept unrounded for x (and y where the asm
// keeps it) and rounded for the others, per site. Compares keep the asm's
// handling of unordered values. The game's constants are read where it
// reads them (0x00B0A18C 1.0, 0x00A84D2C 0.5, 0x00A81054 0.0, 0x00A92480
// 1/3, 0x00A85074 3.0, 0x00A95690 32.0, 0x00A880D4 1.5, 0x00A9DEF0 30000.0,
// 0x00B0A160 and 0x00B0A164 the edge margins 3, 0x00B22A80 the points'
// start).
//
// Random: 0x005710A0 draws once (SC4Random::Unit) from the manager's
// generator at 0x00B22A78 (+0x50), at the game's address; none of the 13
// seeds a generator.
//
// Still the game's, at their addresses: the common base's CreateAutomaton
// 0x00505370 (SC4UIScale's probe site); the boat near a point 0x00568C10;
// the leg's assignment 0x0056D920, copy 0x0056D8B0 and the boat's leg begin
// 0x0056F3F0; the spiral walk, the route iterator, the hash table find, the
// byte vector, reference and list helpers, the random draw and the
// allocator.
#include "core/automata_awmgr.h"

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

namespace opensc4::automata_awmgr {
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
inline T& G(std::uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
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
    log::Write("automata awmgr: first %s", name);
}

void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }
void AddRef(void* object) { Virtual<std::uint32_t>(object, 0x04); }
void Release(void* object) { Virtual<std::uint32_t>(object, 0x08); }

std::uint32_t Bits(float f)
{
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The game's globals and constants this part reads.
namespace g {
inline constexpr std::uintptr_t kRandom = 0x00B22A78;     // the manager's generator (+0x50), set by Init
inline constexpr std::uintptr_t kPointStart = 0x00B22A80; // a float, the points' start
inline constexpr std::uintptr_t kOne = 0x00B0A18C;        // float 1.0 (.data)
inline constexpr std::uintptr_t kRouteMargin = 0x00B0A160; // int32 3 (0x00570DF0)
inline constexpr std::uintptr_t kPlanMargin = 0x00B0A164;  // int32 3 (0x005710A0)
inline constexpr std::uintptr_t kHalf = 0x00A84D2C;       // float 0.5
inline constexpr std::uintptr_t kZero = 0x00A81054;       // float 0.0
inline constexpr std::uintptr_t kThird = 0x00A92480;      // float 1/3
inline constexpr std::uintptr_t kThree = 0x00A85074;      // float 3.0
inline constexpr std::uintptr_t kSide = 0x00A95690;       // float 32.0
inline constexpr std::uintptr_t kAhead = 0x00A880D4;      // float 1.5
inline constexpr std::uintptr_t kWait = 0x00A9DEF0;       // float 30000.0
} // namespace g

inline constexpr std::uint32_t kWait30000 = 0x46EA6000; // 30000.0f
inline constexpr std::uint32_t kWait60000 = 0x476A6000; // 60000.0f
inline constexpr std::uint32_t kMinusOne = 0xBF800000;  // -1.0f
inline constexpr std::uint32_t kBuildingIid = 0xE9793A65;

// The points start as the float 0x00B22A80, copied as its bits.
void Fill(float* p, std::size_t count)
{
    const std::uint32_t start = G<std::uint32_t>(g::kPointStart);
    for (std::size_t i = 0; i < count; ++i)
        std::memcpy(&p[i], &start, 4);
}

// The asm's distance: (dz*dz + dy*dy) + dx*dx under FSQRT, d = a - b.
double Distance(const float* a, const float* b)
{
    const double dx = static_cast<double>(a[0]) - static_cast<double>(b[0]);
    const double dy = static_cast<double>(a[1]) - static_cast<double>(b[1]);
    const double dz = static_cast<double>(a[2]) - static_cast<double>(b[2]);
    return std::sqrt((dz * dz + dy * dy) + dx * dx);
}

double SeaLevel() { return Virtual<double>(G<void*>(addr::SC4_Terrain), 0x74); }

// The city's slot 0x17C: the cell of a point (x, z as floats).
void CellOf(const float* point, std::int32_t* x, std::int32_t* z)
{
    Virtual<void>(G<void*>(addr::SC4_City), 0x17C, Bits(point[0]), Bits(point[2]), static_cast<void*>(x),
                  static_cast<void*>(z));
}

// The city's slot 0x184: a cell's centre (x and z).
void CentreOf(std::int32_t x, std::int32_t z, float* outX, float* outZ)
{
    Virtual<void>(G<void*>(addr::SC4_City), 0x184, x, z, static_cast<void*>(outX), static_cast<void*>(outZ));
}

std::int32_t Minus(std::int32_t a, std::int32_t b)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

// A hash table iterator, as the game passes it by value.
struct Iterator {
    char* node;
    char* table;
};

// A byte vector by value (begin, end, capacity).
struct ByteVector {
    char* begin;
    char* end;
    char* capacity;
};

// A boat's leg by value (0x34 bytes, see the header comment).
struct Leg {
    std::uint32_t words[13];
};
static_assert(sizeof(Leg) == 0x34, "a leg is 0x34 bytes");

namespace leg {
inline constexpr std::size_t kRef = 0x00, kSteps = 0x04, kPlace = 0x10, kFacing = 0x1C;
inline constexpr std::size_t kByte28 = 0x28, kCount = 0x2C, kByte30 = 0x30, kFromDock = 0x31;
} // namespace leg

// The boat's fields these functions use.
namespace boat {
inline constexpr std::size_t kLeg = 0x1E4, kNextLeg = 0x218, kBegun = 0x2B0, kTimer = 0x2B4, kTimer2 = 0x2B8;
inline constexpr std::size_t kTarget = 0x318, kPath = 0x31C, kPathWalk = 0x328, kPathSet = 0x340;
} // namespace boat

// A route record (0x3C bytes, part aw4): the boat +0, its ferry route +4.
namespace record {
inline constexpr std::size_t kRoute = 0x04, kTo = 0x08, kKind = 0x0C, kWalk = 0x18, kSteps = 0x20, kSteps2 = 0x2C;
inline constexpr std::size_t kByte = 0x38;
} // namespace record

void AssignRef(void** slot, void* fresh)
{
    void* const old = *slot;
    if (fresh == old)
        return;
    if (fresh)
        AddRef(fresh);
    *slot = fresh;
    if (old)
        Release(old);
}

// ====================================================================================
// Creation (simulators.automata.awmgr.water.create)

// 0x00568F30, slot 15 CreateAutomaton (group, point, flags, target, a5, a6,
// a7), ret 0x1C: null when a boat is near the point (0x00568C10 with it by
// value and no exception) or its cell (the city's slot 0x17C; the z cell is
// written over the point argument's slot, the x cell's slot holds this
// before) is refused by the city's slot 0x18C; else the common base's
// 0x00505370 with (group, point, flags, target, a5, a6, 0). EAX.
void* __fastcall CreateAutomaton(char* self, void*, std::uint32_t group, const float* point, std::uint32_t flags,
                                 const void* target, std::uint32_t a5, std::uint32_t a6, std::uint32_t)
{
    static bool logged;
    FirstCall("WaterCreateAutomaton", logged);
    const float* const at = point;
    if (CallThis<void*>(addr::cSC4WatercraftManager_BoatNear, self, At<std::uint32_t>(const_cast<float*>(at), 0),
                        At<std::uint32_t>(const_cast<float*>(at), 4), At<std::uint32_t>(const_cast<float*>(at), 8),
                        static_cast<void*>(nullptr)))
        return nullptr;
    // The x cell's slot is the PUSH ECX one (this until slot 0x17C writes it).
    std::int32_t x = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(self));
    void* const city = G<void*>(addr::SC4_City);
    Virtual<void>(city, 0x17C, Bits(at[0]), Bits(at[2]), static_cast<void*>(&x), static_cast<void*>(&point));
    std::int32_t z;
    std::memcpy(&z, &point, 4);
    if (!Virtual<std::uint8_t>(city, 0x18C, x, z))
        return nullptr;
    return CallThis<void*>(addr::cSC4AutomataManager_CreateAutomaton, self, group, at, flags, target, a5, a6,
                           static_cast<std::uint32_t>(0));
}

// 0x0056C990, slot 16 GetRandomCreationPoint (group, position, least, most,
// out, flags, direction), ret 0x1C; most and flags not read. AL 0 when slot
// 30 refuses the group. With a lot at the position (the city's slot 0x84
// object's slot 0x14): its route's dock (+0xA8C, the node's +0x1C) or else
// its first pair's object (+0xAA0, +8); the dock search 0x00568CE0 and the
// point moved twice the facing times the city's slot 0x16C; the body the
// key's water (0x00567CE0); when that cell is open water (0x00568FD0 with 2)
// out its centre at the sea level, the facing into direction (not null),
// AL 1. Else a spiral walk (0x0043FE10, 0x00440850) from the position's
// cell over the city's cells: without a lot only cells not nearer than
// least; in the city (slot 0x18C); the body taken from the first cell with
// a non-zero one when 0; open water; the first of the body ends the walk,
// else the first such cell. The city's slot 0x188 must take it; out its
// centre at the sea level. AL 1, or 0 for none.
std::uint8_t __fastcall GetRandomCreationPoint(char* self, void*, std::uint32_t group, const float* position,
                                               float least, float, float* out, std::uint32_t, float* direction)
{
    static bool logged;
    FirstCall("WaterGetRandomCreationPoint", logged);
    if (!Virtual<std::uint8_t>(self, slot::kCanCreateGroup, group))
        return 0;
    // The water body: only its word is compared.
    std::uint32_t body = 0;
    void* const water = Virtual<void*>(G<void*>(addr::SC4_Service43CF0), 0x24);
    void* const city = G<void*>(addr::SC4_City);
    void* const lot = Virtual<void*>(Virtual<void*>(city, 0x84), 0x14, position);
    if (lot) {
        float facing[3];
        float point[3];
        Fill(facing, 3);
        Fill(point, 3);
        void* key = lot;
        Iterator found = {};
        AddRef(lot);
        CallThis<void>(addr::SC4Uint32HashTable_Find, self + field::kWaterTable, static_cast<void*>(&found),
                       static_cast<const void*>(&key));
        Release(lot);
        void* dock = nullptr;
        void* keyLot = nullptr;
        bool have = false;
        if (found.node) {
            dock = At<void*>(found.node, 0x1C);
            keyLot = At<void*>(found.node, 4);
            have = true;
        } else {
            key = lot;
            AddRef(lot);
            CallThis<void>(addr::SC4Uint32HashTable_Find, self + field::kWaterTable2, static_cast<void*>(&found),
                           static_cast<const void*>(&key));
            Release(lot);
            if (found.node) {
                dock = At<void*>(found.node, 8);
                keyLot = At<void*>(found.node, 4);
                have = true;
            }
        }
        if (have) {
            CallThis<std::uint8_t>(addr::cSC4WatercraftManager_DockPoints, self, dock, static_cast<void*>(point),
                                   static_cast<void*>(facing), static_cast<std::uint32_t>(0));
            const float twice[3] = {static_cast<float>(static_cast<double>(facing[0]) + facing[0]),
                                    static_cast<float>(static_cast<double>(facing[1]) + facing[1]),
                                    static_cast<float>(static_cast<double>(facing[2]) + facing[2])};
            const double span = Virtual<double>(city, 0x16C);
            const double ox = static_cast<double>(twice[0]) * span;
            const float oy = static_cast<float>(static_cast<double>(twice[1]) * span);
            const float oz = static_cast<float>(static_cast<double>(twice[2]) * span);
            point[0] = static_cast<float>(ox + static_cast<double>(point[0]));
            point[1] = static_cast<float>(static_cast<double>(oy) + static_cast<double>(point[1]));
            point[2] = static_cast<float>(static_cast<double>(oz) + static_cast<double>(point[2]));
            body = CallThis<std::uint32_t>(addr::cSC4WatercraftManager_LotWaterBody, self, keyLot);
            // Slot 0x17C writes both cells (the game's slots are not set before).
            std::int32_t cx = 0, cz = 0;
            CellOf(point, &cx, &cz);
            if (CallThis<std::uint8_t>(addr::cSC4WatercraftManager_IsOpenWater, self, cx, cz,
                                       static_cast<std::uint32_t>(2))) {
                CentreOf(cx, cz, &out[0], &out[2]);
                out[1] = static_cast<float>(SeaLevel());
                if (direction)
                    std::memcpy(direction, facing, 12);
                return 1;
            }
        }
    }

    // The spiral walk over the city's cells from the position's.
    std::int32_t px = 0, pz = 0;
    CellOf(position, &px, &pz);
    std::int32_t firstX = -1, firstZ = -1;
    alignas(4) char walk[0x30] = {};
    At<std::uint32_t>(walk, 0x10) = 0x80000000u;
    At<std::uint32_t>(walk, 0x14) = 0x80000000u;
    At<std::uint32_t>(walk, 0x18) = 0x7FFFFFFFu;
    At<std::uint32_t>(walk, 0x1C) = 0x7FFFFFFFu;
    At<std::uint8_t>(walk, 0x20) = 1;
    At<std::uint32_t>(walk, 0x2C) = 0;
    CallThis<void>(addr::SC4SpiralWalk_Start, walk, static_cast<std::int32_t>(0), static_cast<std::int32_t>(0));
    std::int32_t x = px, z = pz;
    CallThis<void>(addr::SC4SpiralWalk_Start, walk, px, pz);
    const std::int32_t cellsZ = Virtual<std::int32_t>(city, 0x178);
    const std::int32_t cellsX = Virtual<std::int32_t>(city, 0x174);
    At<std::int32_t>(walk, 0x10) = 0;
    At<std::int32_t>(walk, 0x14) = 0;
    At<std::int32_t>(walk, 0x18) = cellsX < 0 ? 0 : cellsX;
    At<std::int32_t>(walk, 0x1C) = cellsZ < 0 ? 0 : cellsZ;
    bool chosen = false;
    while (CallThis<std::uint8_t>(addr::SC4SpiralWalk_Next, walk, static_cast<void*>(&x), static_cast<void*>(&z))) {
        float centre[3];
        Fill(centre, 3);
        CentreOf(x, z, &centre[0], &centre[2]);
        // Without a lot, a cell nearer than least is passed by (JNP: below).
        if (!lot && Distance(centre, position) < static_cast<double>(least))
            continue;
        if (!Virtual<std::uint8_t>(city, 0x18C, x, z))
            continue;
        if (static_cast<std::uint16_t>(body) == 0 &&
            static_cast<std::uint16_t>(Virtual<std::uint32_t>(water, 0x1C, x, z)))
            body = Virtual<std::uint32_t>(water, 0x1C, x, z);
        if (!CallThis<std::uint8_t>(addr::cSC4WatercraftManager_IsOpenWater, self, x, z, static_cast<std::uint32_t>(2)))
            continue;
        if (static_cast<std::uint16_t>(body) == static_cast<std::uint16_t>(Virtual<std::uint32_t>(water, 0x1C, x, z))) {
            chosen = true;
            break;
        }
        if (firstX == -1) {
            firstX = x;
            firstZ = z;
        }
    }
    if (!chosen) {
        if (firstX == -1)
            return 0;
        x = firstX;
        z = firstZ;
    }
    if (!Virtual<std::uint8_t>(city, 0x188, Bits(static_cast<float>(x)), Bits(static_cast<float>(z))))
        return 0;
    float cx = 0.0f, cz = 0.0f;
    CentreOf(x, z, &cx, &cz);
    out[0] = cx;
    out[1] = static_cast<float>(SeaLevel());
    std::memcpy(&out[2], &cz, 4);
    return 1;
}

// 0x00567C90, the lot at an occupant (occupant), ret 4 (this not read): null
// without one; else the occupant's point (slot 0x20, the floats starting as
// 0x00B22A80) and the city's slot 0x84 object's slot 0x14 at it. EAX.
void* __fastcall LotAtOccupant(void*, void*, void* occupant)
{
    static bool logged;
    FirstCall("WaterLotAtOccupant", logged);
    if (!occupant)
        return nullptr;
    float point[3];
    Fill(point, 3);
    Virtual<void>(occupant, 0x20, static_cast<void*>(point));
    return Virtual<void*>(Virtual<void*>(G<void*>(addr::SC4_City), 0x84), 0x14, static_cast<void*>(point));
}

// 0x00567CE0, a lot's water body (lot), ret 4 (this not read): 0 without a
// lot (AX; the game keeps EAX's top half) or without the 0x00B43CF0
// service's slot 0x24 object; else that object's slot 0x1C at the lot's
// slot 0x38 rectangle {a, b, c, d} as (b, a), (b, c), (d, a), (d, c), the
// first whose word is not 0, else the last. EAX the call's.
std::uint32_t __fastcall LotWaterBody(void*, void*, void* lot)
{
    static bool logged;
    FirstCall("WaterLotWaterBody", logged);
    if (!lot)
        return 0;
    void* const water = Virtual<void*>(G<void*>(addr::SC4_Service43CF0), 0x24);
    if (!water)
        return 0;
    // Slot 0x38 fills the rectangle (stack slots in the game; ours 0 first).
    std::int32_t r[4] = {};
    Virtual<void>(lot, 0x38, static_cast<void*>(r));
    std::uint32_t found = Virtual<std::uint32_t>(water, 0x1C, r[1], r[0]);
    if (static_cast<std::uint16_t>(found))
        return found;
    found = Virtual<std::uint32_t>(water, 0x1C, r[1], r[2]);
    if (static_cast<std::uint16_t>(found))
        return found;
    found = Virtual<std::uint32_t>(water, 0x1C, r[3], r[0]);
    if (static_cast<std::uint16_t>(found))
        return found;
    return Virtual<std::uint32_t>(water, 0x1C, r[3], r[2]);
}

// 0x00568CE0, the dock search (occupant, point, direction, turned), ret 0x10
// (this not read). AL 0 without an occupant; point its point (slot 0x20)
// at the sea level. Its interface 0xE9793A65 (queried into the occupant's
// argument slot; AL 0 without, released when the query failed): its
// orientation (slot 0x24) picks a facing of (0, 0, 1), (-1, 0, 0), (0, 0,
// -1), (1, 0, 0) into direction, its model's (slot 0xC) box (slot 0x28,
// first and second) the extent along the larger of the facing's x and z
// (z when not smaller), the facing made a unit (1.0 0x00B0A18C over its
// length; the game writes its x and y floats over the point and direction
// slots). Without turned point moves half the extent along it; with it a
// third of the extent along the facing turned (z, 0.0, -x) made a unit. AL 1.
std::uint8_t __fastcall DockPoints(void*, void*, void* occupant, float* point, float* direction,
                                   std::uint32_t turned)
{
    static bool logged;
    FirstCall("WaterDockPoints", logged);
    if (!occupant)
        return 0;
    float* const p = point;
    float* const d = direction;
    Virtual<void>(occupant, 0x20, static_cast<void*>(p));
    p[1] = static_cast<float>(SeaLevel());
    static const float kFacings[4][3] = {{0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
                                         {1.0f, 0.0f, 0.0f}};
    void* const object = occupant;
    occupant = nullptr;
    const std::uint8_t ok = Virtual<std::uint8_t>(object, 0x00, kBuildingIid, static_cast<void*>(&occupant));
    void* const building = occupant;
    if (!ok) {
        if (building)
            Release(building);
        return 0;
    }
    if (!building)
        return 0;
    std::uint32_t index = Virtual<std::uint32_t>(building, 0x24);
    // Slot 0x28 fills both corners (stack slots in the game; ours 0 first).
    float first[3] = {}, second[3] = {};
    Virtual<void>(Virtual<void*>(building, 0x0C), 0x28, static_cast<void*>(first), static_cast<void*>(second));
    if (index > 3) {
        // The game reads other stack slots for these (the table is 4 x 12
        // bytes on its stack); ours logs it and takes facing 0.
        static bool warned;
        if (!warned) {
            warned = true;
            log::Write("automata awmgr: WaterDockPoints orientation %u out of range, 0 taken", index);
        }
        index = 0;
    }
    const float vx = kFacings[index][0], vy = kFacings[index][1], vz = kFacings[index][2];
    d[0] = vx;
    d[1] = vy;
    d[2] = vz;
    // FCOMPP |z| with |x|; JP (not below, or unordered) takes the z extent.
    double extent;
    if (std::fabs(static_cast<double>(d[2])) < std::fabs(static_cast<double>(d[0])))
        extent = static_cast<double>(second[0]) - static_cast<double>(first[0]);
    else
        extent = static_cast<double>(second[2]) - static_cast<double>(first[2]);
    const double vxd = vx, vyd = vy, vzd = vz;
    const double inverse =
        static_cast<double>(G<float>(g::kOne)) / std::sqrt((vzd * vzd + vyd * vyd) + vxd * vxd);
    const float nx = static_cast<float>(vxd * inverse);
    d[0] = nx;
    const float ny = static_cast<float>(vyd * inverse);
    d[1] = ny;
    const float nz = static_cast<float>(inverse * vzd);
    d[2] = nz;
    double ox;
    float oy, oz;
    if (!static_cast<std::uint8_t>(turned)) {
        const double half = extent * static_cast<double>(G<float>(g::kHalf));
        ox = static_cast<double>(nx) * half;
        oy = static_cast<float>(static_cast<double>(ny) * half);
        oz = static_cast<float>(static_cast<double>(nz) * half);
    } else {
        const double tz = static_cast<double>(d[2]);
        const float tx = static_cast<float>(tz); // (z, 0, -x), its x stored first
        const double minusX = -static_cast<double>(d[0]);
        const double inverse2 = static_cast<double>(G<float>(g::kOne)) /
                                std::sqrt(minusX * minusX + static_cast<double>(tx) * static_cast<double>(tx));
        const float ux = static_cast<float>(static_cast<double>(tx) * inverse2);
        const float uy = static_cast<float>(static_cast<double>(G<float>(g::kZero)) * inverse2);
        const float uz = static_cast<float>(minusX * inverse2);
        const double third = extent * static_cast<double>(G<float>(g::kThird));
        ox = static_cast<double>(ux) * third;
        oy = static_cast<float>(static_cast<double>(uy) * third);
        oz = static_cast<float>(static_cast<double>(uz) * third);
    }
    p[0] = static_cast<float>(ox + static_cast<double>(p[0]));
    p[1] = static_cast<float>(static_cast<double>(oy) + static_cast<double>(p[1]));
    p[2] = static_cast<float>(static_cast<double>(oz) + static_cast<double>(p[2]));
    Release(building);
    return 1;
}

// 0x00568FD0, whether a cell is open water (x, z, kind), ret 0xC: AL 0
// unless the 0x00B43CF0 service's slot 0x20 (x, z, kind, 0) answers. The
// cell's centre at the sea level; AL 0 when an automaton of the list (+0x44,
// the node's +8, null ones passed) has its slot 0x44 radius above (ordered)
// its distance from its point (slot 0x1C, starting as 0x00B22A80), that
// distance rounded to a float (the game's is over the kind's slot). AL 1.
std::uint8_t __fastcall IsOpenWater(char* self, void*, std::int32_t x, std::int32_t z, std::uint32_t kind)
{
    static bool logged;
    FirstCall("WaterIsOpenWater", logged);
    if (!Virtual<std::uint8_t>(G<void*>(addr::SC4_Service43CF0), 0x20, x, z, kind, static_cast<std::uint32_t>(0)))
        return 0;
    float centre[3];
    centre[1] = static_cast<float>(SeaLevel());
    centre[0] = 0.0f;
    centre[2] = 0.0f;
    CentreOf(x, z, &centre[0], &centre[2]);
    for (char* node = At<char*>(At<char*>(self, field::kAutomata), 0); node != At<char*>(self, field::kAutomata);
         node = At<char*>(node, 0)) {
        float at[3];
        Fill(at, 3);
        void* const automaton = At<void*>(node, 8);
        if (!automaton)
            continue;
        Virtual<void>(automaton, 0x1C, static_cast<void*>(at));
        const float distance = static_cast<float>(Distance(at, centre));
        if (Virtual<double>(automaton, 0x44) > static_cast<double>(distance))
            return 0;
    }
    return 1;
}

// ====================================================================================
// Routes (simulators.automata.awmgr.water.routes)

// 0x00566DA0, the boat's path group test, plain ret: its slot 0x14 object's
// slot 0x58 (asked anew) for 0x440F, 0x440A and 0x4411. EAX 1 for any, else 0.
std::uint32_t __fastcall IsPathBoat(void* self, void*)
{
    static bool logged;
    FirstCall("WaterIsPathBoat", logged);
    for (const std::uint32_t group : {0x440Fu, 0x440Au, 0x4411u})
        if (Virtual<std::uint8_t>(Virtual<void*>(self, 0x14), 0x58, group))
            return 1;
    return 0;
}

// 0x0056F510, the boat's path (path by value, target), ret 0x10: +0x318 the
// target (its reference assigned), +0x340 1, the byte vector +0x31C the path
// and its route iterator +0x328 started on it. The limit its slot 0x44
// times 3.0 (a float, over the target's slot in the game); while there is
// a target, its steps are taken while the step's cell centre at the sea
// level is not
// farther (ordered above) than the limit from the target's point (slot 0x20).
// The path's storage freed.
void __fastcall SetPath(char* self, void*, ByteVector path, void* target)
{
    static bool logged;
    FirstCall("WaterSetPath", logged);
    AssignRef(&At<void*>(self, boat::kTarget), target);
    At<std::uint8_t>(self, boat::kPathSet) = 1;
    CallThis<void*>(addr::SC4ByteVector_Assign, self + boat::kPath, static_cast<const void*>(&path));
    CallThis<void>(addr::SC4RouteIterator_Init, self + boat::kPathWalk, static_cast<void*>(self + boat::kPath));
    const float limit = static_cast<float>(Virtual<double>(self, 0x44) * static_cast<double>(G<float>(g::kThree)));
    if (At<void*>(self, boat::kTarget)) {
        // The step record (10 bytes) keeps the last one across the walk.
        alignas(4) char step[12] = {};
        do {
            if (!CallThis<std::uint8_t>(addr::SC4RouteIterator_Next, self + boat::kPathWalk, static_cast<void*>(step)))
                break;
            float at[3];
            Fill(at, 3);
            float centre[3];
            centre[1] = static_cast<float>(SeaLevel());
            centre[0] = 0.0f;
            centre[2] = 0.0f;
            Virtual<void>(At<void*>(self, boat::kTarget), 0x20, static_cast<void*>(at));
            CentreOf(At<std::int16_t>(step, 0), At<std::int16_t>(step, 2), &centre[0], &centre[2]);
            if (Distance(centre, at) > static_cast<double>(limit))
                break;
        } while (At<void*>(self, boat::kTarget));
    }
    if (path.begin)
        Free(path.begin);
}

// 0x0056F670 (cSC4Watercraft::SetFerryRoute), the boat's two legs (first, second by value, start), ret 0x6C
// (this is the boat): +0x1E4 and +0x218 assigned (0x0056D920); with the
// start byte a copy of +0x1E4 (0x0056D8B0) begun (0x0056F3F0) and +0x2B0 1.
// The legs' byte vector storage freed and references released.
void __fastcall SetFerryRoute(char* self, void*, Leg first, Leg second, std::uint32_t start)
{
    static bool logged;
    FirstCall("WaterSetFerryRoute", logged);
    CallThis<void*>(addr::cSC4Watercraft_AssignLeg, self + boat::kLeg, static_cast<const void*>(&first));
    CallThis<void*>(addr::cSC4Watercraft_AssignLeg, self + boat::kNextLeg, static_cast<const void*>(&second));
    if (static_cast<std::uint8_t>(start)) {
        // The game builds the copy in the argument area; ours on the stack,
        // handed on bitwise (the leg holds no pointer to itself).
        Leg copy;
        CallThis<void*>(addr::cSC4Watercraft_CopyLeg, static_cast<void*>(&copy),
                        static_cast<const void*>(self + boat::kLeg));
        CallThis<void>(addr::cSC4Watercraft_BeginLeg, self, copy);
        At<std::uint8_t>(self, boat::kBegun) = 1;
    }
    if (void* const buffer = At<void*>(&first, leg::kSteps))
        Free(buffer);
    if (void* const object = At<void*>(&first, leg::kRef))
        Release(object);
    if (void* const buffer = At<void*>(&second, leg::kSteps))
        Free(buffer);
    if (void* const object = At<void*>(&second, leg::kRef))
        Release(object);
}

// 0x0056C940, a leg's first 0x28 bytes, plain ret: +0..+0xC zero, the
// points +0x10..+0x24 the float 0x00B22A80 (read for each). EAX this.
char* __fastcall ConstructLeg(char* self, void*)
{
    static bool logged;
    FirstCall("WaterConstructLeg", logged);
    for (std::size_t at = 0; at < 0x10; at += 4)
        At<std::uint32_t>(self, at) = 0;
    for (std::size_t at = 0x10; at < 0x28; at += 4)
        At<std::uint32_t>(self, at) = G<std::uint32_t>(g::kPointStart);
    return self;
}

// A dock's point (0x00568CE0, the floats starting as 0x00B22A80) moved
// along its facing times the scale: x and y added unrounded, z rounded.
void DockPlace(char* self, void* dock, float* place, float* facing, float scale)
{
    CallThis<std::uint8_t>(addr::cSC4WatercraftManager_DockPoints, self, dock, static_cast<void*>(place),
                           static_cast<void*>(facing), static_cast<std::uint32_t>(0));
    const double s = scale;
    const double ox = static_cast<double>(facing[0]) * s;
    const double oy = static_cast<double>(facing[1]) * s;
    const float oz = static_cast<float>(static_cast<double>(facing[2]) * s);
    place[0] = static_cast<float>(ox + static_cast<double>(place[0]));
    place[1] = static_cast<float>(oy + static_cast<double>(place[1]));
    place[2] = static_cast<float>(static_cast<double>(oz) + static_cast<double>(place[2]));
}

void SetPoints(char* to, const float* place, const float* facing)
{
    std::memcpy(to + leg::kFacing, facing, 12);
    std::memcpy(to + leg::kPlace, place, 12);
}

// 0x0056FD60, a route record's two legs (the route iterator by value, the
// record, first, second), ret 0x14. The scale: the record's boat's slot 0x44
// (a float), 1.0 without a boat (the game's over the record's slot). First:
// +0x31 1, the record's +4 (its ferry route's reference; unless it is
// first's own slot) and its first byte vector (+0x20), the place the route's dock (+0x1C) point out along its
// facing times the scale, the facing; +0x28, +0x30 0, +0x2C 0; second's +0x31
// 0. Without the record's byte +0x18: the route of its +4 (+8 of the
// record) found into the iterator's slots; none: second a copy of first
// (0x0056D920). Else second +0x31 1, its +0 the ferry route's +4, its byte
// vector the second (+0x2C), that route dock's place and facing the same
// way, +0x2C, +0x28, +0x30 0. With the byte: the second byte vector's steps
// walked (SC4RouteIterator) to the first open water one (0x00568FD0 with 2,
// counted) and the step after it; none: nothing more. Second +0x31 1, its
// +0 released and nulled, its byte vector the second, the facing the second
// centre less the first, the place the first at the sea level (the sea
// level asked twice), +0x2C the count of steps taken, +0x28, +0x30 0.
void __fastcall RouteFromDock(char* self, void*, Iterator it, char* record, char* first, char* second)
{
    static bool logged;
    FirstCall("WaterRouteFromDock", logged);
    char* const r = record;
    void* const automaton = At<void*>(r, 0);
    const float scale = automaton ? static_cast<float>(Virtual<double>(automaton, 0x44)) : 1.0f;
    float place[3];
    float facing[3];
    Fill(place, 3);
    Fill(facing, 3);
    DockPlace(self, At<void*>(it.node, 0x1C), place, facing, scale);
    At<std::uint8_t>(first, leg::kFromDock) = 1;
    if (r + record::kRoute != first)
        AssignRef(&At<void*>(first, leg::kRef), At<void*>(r, record::kRoute));
    CallThis<void*>(addr::SC4ByteVector_Assign, first + leg::kSteps, static_cast<const void*>(r + record::kSteps));
    SetPoints(first, place, facing);
    At<std::uint8_t>(first, leg::kByte28) = 0;
    At<std::uint8_t>(first, leg::kByte30) = 0;
    At<std::uint32_t>(first, leg::kCount) = 0;
    At<std::uint8_t>(second, leg::kFromDock) = 0;
    if (!At<std::uint8_t>(r, record::kWalk)) {
        CallThis<void>(addr::SC4Uint32HashTable_Find, self + field::kWaterTable, static_cast<void*>(&it),
                       static_cast<const void*>(r + record::kTo));
        char* const found = it.node;
        if (!found) {
            CallThis<void*>(addr::cSC4Watercraft_AssignLeg, second, static_cast<const void*>(first));
            return;
        }
        float there[3];
        Fill(there, 3);
        Fill(facing, 3);
        DockPlace(self, At<void*>(found, 0x1C), there, facing, scale);
        At<std::uint8_t>(second, leg::kFromDock) = 1;
        CallThis<void>(addr::SC4RefPtr_AssignFrom, second + leg::kRef, static_cast<const void*>(r + record::kTo));
        CallThis<void*>(addr::SC4ByteVector_Assign, second + leg::kSteps,
                        static_cast<const void*>(r + record::kSteps2));
        SetPoints(second, there, facing);
        At<std::uint32_t>(second, leg::kCount) = 0;
        At<std::uint8_t>(second, leg::kByte28) = 0;
        At<std::uint8_t>(second, leg::kByte30) = 0;
        return;
    }
    alignas(4) char walk[0x18] = {};
    alignas(4) char step[12] = {};
    alignas(4) char next[12] = {};
    std::uint32_t count = 0;
    CallThis<void>(addr::SC4RouteIterator_Init, walk, static_cast<void*>(r + record::kSteps2));
    if (!CallThis<std::uint8_t>(addr::SC4RouteIterator_Next, walk, static_cast<void*>(step)))
        return;
    for (;;) {
        ++count;
        if (CallThis<std::uint8_t>(addr::cSC4WatercraftManager_IsOpenWater, self,
                                   static_cast<std::int32_t>(At<std::int16_t>(step, 0)),
                                   static_cast<std::int32_t>(At<std::int16_t>(step, 2)), static_cast<std::uint32_t>(2)))
            break;
        if (!CallThis<std::uint8_t>(addr::SC4RouteIterator_Next, walk, static_cast<void*>(step)))
            return;
    }
    if (!CallThis<std::uint8_t>(addr::SC4RouteIterator_Next, walk, static_cast<void*>(next)))
        return;
    float a[3];
    float b[3];
    Fill(a, 3);
    Fill(b, 3);
    ++count;
    CentreOf(At<std::int16_t>(step, 0), At<std::int16_t>(step, 2), &a[0], &a[2]);
    CentreOf(At<std::int16_t>(next, 0), At<std::int16_t>(next, 2), &b[0], &b[2]);
    const float toward[3] = {static_cast<float>(static_cast<double>(b[0]) - static_cast<double>(a[0])),
                             static_cast<float>(static_cast<double>(b[1]) - static_cast<double>(a[1])),
                             static_cast<float>(static_cast<double>(b[2]) - static_cast<double>(a[2]))};
    a[1] = static_cast<float>(SeaLevel());
    b[1] = static_cast<float>(SeaLevel());
    At<std::uint8_t>(second, leg::kFromDock) = 1;
    if (void* const old = At<void*>(second, leg::kRef)) {
        At<void*>(second, leg::kRef) = nullptr;
        Release(old);
    }
    CallThis<void*>(addr::SC4ByteVector_Assign, second + leg::kSteps, static_cast<const void*>(r + record::kSteps2));
    SetPoints(second, a, toward);
    At<std::uint32_t>(second, leg::kCount) = count;
    At<std::uint8_t>(second, leg::kByte28) = 0;
    At<std::uint8_t>(second, leg::kByte30) = 0;
}

// The cell bounds test of 0x00570DF0 and 0x005710A0: a cell within the
// margin (read where the asm reads it) of the city's edges fails.
bool InsideMargin(std::int32_t x, std::int32_t z, std::uintptr_t margin)
{
    void* const city = G<void*>(addr::SC4_City);
    if (x < G<std::int32_t>(margin))
        return false;
    const std::int32_t cellsX = Virtual<std::int32_t>(city, 0x174);
    const std::int32_t m = G<std::int32_t>(margin);
    if (x > Minus(cellsX, m))
        return false;
    if (z < m)
        return false;
    const std::int32_t cellsZ = Virtual<std::int32_t>(city, 0x178);
    if (z > Minus(cellsZ, G<std::int32_t>(margin)))
        return false;
    return true;
}

// 0x00570DF0, a new ferry for a route record (from, to by value, the record),
// ret 0x14 (to's table not read). The kind 0x4400 when the ferry route's +8
// is 1, else 0x4402. With a dock (from's node +0x1C), its cell (its point,
// the city's slot 0x17C) within 3 (0x00B0A160) of the city's edge: AL 0.
// Slot 15 (kind, (1, 1, 1), 0x10002, (0, 0, 0), 0, 0, 0); none: AL 0. Its slot
// 0xD0 (0, 0, -1.0, 0, 0, 0, 0) when its slot 0xEC answers; the record's +0
// it. Two legs made (0x0056C940, then 0x0056FD60). With the record's boat:
// with the byte +0x38 its slot 0x40 at the first leg's points and 0x0056F670
// (first, second, 1), from's node +8 the boat made; without it the second
// leg's points, 0x0056F670 (second, first, 1) and to's node +8 (not null).
// The byte flipped, the legs' storage freed and references released. AL 1.
std::uint8_t __fastcall NextFerryRoute(char* self, void*, Iterator from, Iterator to, char* record)
{
    static bool logged;
    FirstCall("WaterNextFerryRoute", logged);
    const std::uint32_t kind = At<std::uint32_t>(record, record::kKind) == 1 ? 0x4400u : 0x4402u;
    float at[3];
    Fill(at, 3);
    if (void* const dock = At<void*>(from.node, 0x1C)) {
        Virtual<void>(dock, 0x20, static_cast<void*>(at));
        // Slot 0x17C writes both cells (the game's slots are not set before).
        std::int32_t cx = 0, cz = 0;
        CellOf(at, &cx, &cz);
        if (!InsideMargin(cx, cz, g::kRouteMargin))
            return 0;
    }
    const float ones[3] = {1.0f, 1.0f, 1.0f};
    const std::uint32_t zeros[3] = {0, 0, 0};
    void* const made = Virtual<void*>(self, 0x3C, kind, static_cast<const void*>(ones),
                                      static_cast<std::uint32_t>(0x10002), static_cast<const void*>(zeros),
                                      static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                                      static_cast<std::uint32_t>(0));
    if (!made)
        return 0;
    if (Virtual<std::uint32_t>(made, 0xEC))
        Virtual<void>(made, 0xD0, static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0), kMinusOne,
                      static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                      static_cast<std::uint32_t>(0));
    {
        void* const old = At<void*>(record, 0);
        if (made != old) {
            AddRef(made);
            At<void*>(record, 0) = made;
            if (old)
                Release(old);
        }
    }
    // The legs' bytes past 0x28 are stack leftovers in the game when
    // 0x0056FD60 does not write them; ours start at 0.
    Leg first = {};
    Leg second = {};
    char* const a = reinterpret_cast<char*>(&first);
    char* const b = reinterpret_cast<char*>(&second);
    CallThis<char*>(addr::cSC4Watercraft_ConstructLeg, a);
    CallThis<char*>(addr::cSC4Watercraft_ConstructLeg, b);
    CallThis<void>(addr::cSC4WatercraftManager_RouteFromDock, self, from, record, a, b);
    if (void* const boatNow = At<void*>(record, 0)) {
        Leg one;
        Leg two;
        if (At<std::uint8_t>(record, record::kByte)) {
            Virtual<void>(boatNow, 0x40, static_cast<void*>(a + leg::kPlace), static_cast<void*>(a + leg::kFacing));
            void* const target = At<void*>(record, 0);
            CallThis<void*>(addr::cSC4Watercraft_CopyLeg, static_cast<void*>(&two), static_cast<const void*>(b));
            CallThis<void*>(addr::cSC4Watercraft_CopyLeg, static_cast<void*>(&one), static_cast<const void*>(a));
            CallThis<void>(addr::cSC4Watercraft_SetFerryRoute, target, one, two, static_cast<std::uint32_t>(1));
            CallThis<void>(addr::SC4RefPtr_Assign, from.node + 8, made);
        } else {
            Virtual<void>(boatNow, 0x40, static_cast<void*>(b + leg::kPlace), static_cast<void*>(b + leg::kFacing));
            void* const target = At<void*>(record, 0);
            CallThis<void*>(addr::cSC4Watercraft_CopyLeg, static_cast<void*>(&two), static_cast<const void*>(a));
            CallThis<void*>(addr::cSC4Watercraft_CopyLeg, static_cast<void*>(&one), static_cast<const void*>(b));
            CallThis<void>(addr::cSC4Watercraft_SetFerryRoute, target, one, two, static_cast<std::uint32_t>(1));
            if (to.node)
                CallThis<void>(addr::SC4RefPtr_Assign, to.node + 8, made);
        }
    }
    At<std::uint8_t>(record, record::kByte) = At<std::uint8_t>(record, record::kByte) == 0 ? 1 : 0;
    if (void* const buffer = At<void*>(b, leg::kSteps))
        Free(buffer);
    if (void* const object = At<void*>(b, leg::kRef))
        Release(object);
    if (void* const buffer = At<void*>(a, leg::kSteps))
        Free(buffer);
    if (void* const object = At<void*>(a, leg::kRef))
        Release(object);
    return 1;
}

// 0x005710A0, a boat for a terminal (the second pair table's iterator by
// value, the kind), ret 0xC. The dock search on the node's key (+4): AL 0
// when it fails or the dock's cell is within 3 (0x00B0A164) of the city's
// edge. The place moved half the facing; the start point that, or for a
// kind other than 0x4416 32 along the facing turned (z, 0.0, -x) made a
// unit. Slot 15 (kind, start, 0x10002, (0, 0, 0), 0, 0, 0); none: AL 0. The
// place then 1.5 radii (slot 0x44) along the facing for 0x4416, else a
// radius along the turned unit; slot 0x40 (place, facing); the node's +8 the
// boat. The kind's low byte (a flag now) 0. When the node's boat is of the
// path groups (0x00566DA0): its slot 0xD0 (0, ...) when its slot 0xEC
// answers, (0x4E, ...), and (0x4C, 0, 30000.0 or (draw + 1.0) x 30000.0 with
// the generator 0x00B22A78, ...); the place's cell; of the 0x00B43D8C
// service's list 9 (slot 0x44) the objects' cells (slot 0x18, -1 and the
// count pulled in by one): the first the 0x00B43DA8 service's slot 0x84
// finds a path to from the place's cell (while the node has a boat) goes to
// the boat's 0x0056F510 (a copy, the terminal) and sets the flag. Without
// the flag: unless the boat's slot 0xEC answers 0x4C, its slot 0xD0 (0x4C,
// 0, -1.0, ...) and the node's boat's +0x2B4 60000.0, +0x2B8 0, +0x2E8..
// +0x2F0 its +0x2F4.., +0x300.. its +0x30C... AL 1.
std::uint8_t __fastcall PlanRoute(char* self, void*, Iterator it, std::uint32_t kind)
{
    static bool logged;
    FirstCall("WaterPlanRoute", logged);
    char* const node = it.node;
    float place[3];
    float facing[3];
    Fill(place, 3);
    Fill(facing, 3);
    if (!CallThis<std::uint8_t>(addr::cSC4WatercraftManager_DockPoints, self, At<void*>(node, 4),
                                static_cast<void*>(place), static_cast<void*>(facing), static_cast<std::uint32_t>(0)))
        return 0;
    // Slot 0x17C writes both cells (the game's slots are not set before).
    std::int32_t dx = 0, dz = 0;
    CellOf(place, &dx, &dz);
    if (!InsideMargin(dx, dz, g::kPlanMargin))
        return 0;
    {
        const double half = G<float>(g::kHalf);
        const double ox = static_cast<double>(facing[0]) * half;
        const double oy = static_cast<double>(facing[1]) * half;
        const float oz = static_cast<float>(static_cast<double>(facing[2]) * half);
        place[0] = static_cast<float>(ox + static_cast<double>(place[0]));
        place[1] = static_cast<float>(oy + static_cast<double>(place[1]));
        place[2] = static_cast<float>(static_cast<double>(oz) + static_cast<double>(place[2]));
    }
    float start[3] = {place[0], place[1], place[2]};
    // The facing turned (z, 0.0, -x) over 1.0 / its length: x and y rounded,
    // z kept or rounded per site.
    const double tz = -(-static_cast<double>(facing[2]));
    const double minusX = -static_cast<double>(facing[0]);
    if (kind != 0x4416) {
        const double inverse = static_cast<double>(G<float>(g::kOne)) / std::sqrt(tz * tz + minusX * minusX);
        const float ux = static_cast<float>(tz * inverse);
        const float uy = static_cast<float>(static_cast<double>(G<float>(g::kZero)) * inverse);
        const double uz = inverse * minusX;
        const double side = G<float>(g::kSide);
        const double sx = static_cast<double>(ux) * side;
        const float sy = static_cast<float>(static_cast<double>(uy) * side);
        const float sz = static_cast<float>(uz * side);
        start[0] = static_cast<float>(sx + static_cast<double>(place[0]));
        start[1] = static_cast<float>(static_cast<double>(sy) + static_cast<double>(place[1]));
        start[2] = static_cast<float>(static_cast<double>(sz) + static_cast<double>(place[2]));
    }
    const std::uint32_t zeros[3] = {0, 0, 0};
    void* const made = Virtual<void*>(self, 0x3C, kind, static_cast<const void*>(start),
                                      static_cast<std::uint32_t>(0x10002), static_cast<const void*>(zeros),
                                      static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                                      static_cast<std::uint32_t>(0));
    if (!made)
        return 0;
    if (kind == 0x4416) {
        const double radius = Virtual<double>(made, 0x44);
        const double ax = static_cast<double>(facing[0]) * radius;
        const float ay = static_cast<float>(static_cast<double>(facing[1]) * radius);
        const float az = static_cast<float>(static_cast<double>(facing[2]) * radius);
        const double ahead = G<float>(g::kAhead);
        const double bx = ax * ahead;
        const float by = static_cast<float>(static_cast<double>(ay) * ahead);
        const float bz = static_cast<float>(static_cast<double>(az) * ahead);
        const double x = bx + static_cast<double>(place[0]);
        const double y = static_cast<double>(by) + static_cast<double>(place[1]);
        const double z = static_cast<double>(bz) + static_cast<double>(place[2]);
        place[2] = static_cast<float>(z);
        place[0] = static_cast<float>(x);
        place[1] = static_cast<float>(y);
    } else {
        const double inverse = static_cast<double>(G<float>(g::kOne)) / std::sqrt(tz * tz + minusX * minusX);
        const float ux = static_cast<float>(tz * inverse);
        const float uy = static_cast<float>(static_cast<double>(G<float>(g::kZero)) * inverse);
        const float uz = static_cast<float>(minusX * inverse);
        const double radius = Virtual<double>(made, 0x44);
        const double ox = static_cast<double>(ux) * radius;
        const float oy = static_cast<float>(static_cast<double>(uy) * radius);
        const float oz = static_cast<float>(static_cast<double>(uz) * radius);
        place[0] = static_cast<float>(ox + static_cast<double>(place[0]));
        place[1] = static_cast<float>(static_cast<double>(oy) + static_cast<double>(place[1]));
        place[2] = static_cast<float>(static_cast<double>(oz) + static_cast<double>(place[2]));
    }
    Virtual<void>(made, 0x40, static_cast<void*>(place), static_cast<void*>(facing));
    CallThis<void>(addr::SC4RefPtr_Assign, node + 8, made);
    // The kind's slot: its low byte is the flag from here on.
    std::uint8_t* const planned = reinterpret_cast<std::uint8_t*>(&kind);
    *planned = 0;
    void* const held = At<void*>(node, 8);
    if (held && static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4Watercraft_IsPathBoat, held))) {
        if (Virtual<std::uint32_t>(made, 0xEC))
            Virtual<void>(made, 0xD0, static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0), kMinusOne,
                          static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                          static_cast<std::uint32_t>(0));
        Virtual<void>(made, 0xD0, static_cast<std::uint32_t>(0x4E), static_cast<std::uint32_t>(0), kMinusOne,
                      static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                      static_cast<std::uint32_t>(0));
        // The wait's slot is later the objects' z cell slot.
        std::uint32_t wait = kWait30000;
        if (void* const random = G<void*>(g::kRandom)) {
            const double r = CallThis<double>(addr::SC4Random_Unit, random);
            wait = Bits(static_cast<float>((r + static_cast<double>(G<float>(addr::SC4_FloatOneA81228))) *
                                           static_cast<double>(G<float>(g::kWait))));
        }
        Virtual<void>(made, 0xD0, static_cast<std::uint32_t>(0x4C), static_cast<std::uint32_t>(0), wait,
                      static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                      static_cast<std::uint32_t>(0));
        // Slot 0x17C writes both cells (the game's slots are not set before).
        std::int32_t fromX = 0, fromZ = 0;
        CellOf(place, &fromX, &fromZ);
        void* list[2] = {};
        CallThis<void*>(addr::SC4List_Construct2, static_cast<void*>(list));
        Virtual<void>(G<void*>(addr::SC4_MonthEndService), 0x44, static_cast<std::uint32_t>(9),
                      static_cast<void*>(list));
        // The object's cell slots keep their values from one object to the
        // next (the x slot is a stack leftover in the game at first, 0 in
        // ours; the z slot held the wait).
        std::int32_t x = 0;
        std::int32_t z = static_cast<std::int32_t>(wait);
        for (char* item = At<char*>(static_cast<char*>(list[0]), 0); item != static_cast<char*>(list[0]);
             item = At<char*>(item, 0)) {
            void* const object = At<void*>(item, 8);
            if (!object)
                continue;
            ByteVector path = {};
            Virtual<void>(object, 0x18, static_cast<void*>(&x), static_cast<void*>(&z));
            void* const city = G<void*>(addr::SC4_City);
            if (x < 0)
                ++x;
            if (x == Virtual<std::int32_t>(city, 0x174))
                --x;
            if (z < 0)
                ++z;
            if (z == Virtual<std::int32_t>(city, 0x178))
                --z;
            bool found = false;
            if (At<void*>(node, 8))
                found = Virtual<std::uint8_t>(G<void*>(addr::SC4_Service43DA8), 0x84, fromX, fromZ, x, z,
                                              static_cast<void*>(&path)) != 0;
            if (found) {
                void* const terminal = At<void*>(node, 4);
                void* const boatNow = At<void*>(node, 8);
                // The game builds the copy in the argument area; ours on the
                // stack, handed on bitwise.
                ByteVector copy;
                CallThis<void*>(addr::SC4ByteVector_CopyConstruct, static_cast<void*>(&copy),
                                static_cast<const void*>(&path));
                CallThis<void>(addr::cSC4Watercraft_SetPath, boatNow, copy, terminal);
                *planned = 1;
                if (path.begin)
                    Free(path.begin);
                break;
            }
            if (path.begin)
                Free(path.begin);
        }
        CallThis<void>(addr::SC4List_Destroy3, static_cast<void*>(list));
        if (*planned)
            return 1;
    }
    if (Virtual<std::uint32_t>(made, 0xEC) == 0x4C)
        return 1;
    Virtual<void>(made, 0xD0, static_cast<std::uint32_t>(0x4C), static_cast<std::uint32_t>(0), kMinusOne,
                  static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                  static_cast<std::uint32_t>(0));
    char* const boatNow = At<char*>(node, 8);
    if (!boatNow)
        return 1;
    At<std::uint32_t>(boatNow, boat::kTimer) = kWait60000;
    At<std::uint32_t>(boatNow, boat::kTimer2) = 0;
    char* const b1 = At<char*>(node, 8);
    for (std::size_t at = 0; at < 0xC; at += 4)
        At<std::uint32_t>(b1, 0x2E8 + at) = At<std::uint32_t>(b1, 0x2F4 + at);
    char* const b2 = At<char*>(node, 8);
    for (std::size_t at = 0; at < 0xC; at += 4)
        At<std::uint32_t>(b2, 0x300 + at) = At<std::uint32_t>(b2, 0x30C + at);
    return 1;
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
            log::Write("automata awmgr %s: no address entry for 0x%08X", what,
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
    log::Write("automata awmgr %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kCreate[] = {
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_CreateAutomaton, CreateAutomaton),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_GetRandomCreationPoint, GetRandomCreationPoint),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_LotAtOccupant, LotAtOccupant),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_LotWaterBody, LotWaterBody),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_DockPoints, DockPoints),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_IsOpenWater, IsOpenWater),
};

const Detour kRoutes[] = {
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_PlanRoute, PlanRoute),
    OPENSC4_DETOUR(addr::cSC4Watercraft_IsPathBoat, IsPathBoat),
    OPENSC4_DETOUR(addr::cSC4Watercraft_SetPath, SetPath),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_RouteFromDock, RouteFromDock),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_NextFerryRoute, NextFerryRoute),
    OPENSC4_DETOUR(addr::cSC4Watercraft_SetFerryRoute, SetFerryRoute),
    OPENSC4_DETOUR(addr::cSC4Watercraft_ConstructLeg, ConstructLeg),
};

#undef OPENSC4_DETOUR

bool InstallCreate(registry::Mode)
{
    return InstallList(kCreate, sizeof kCreate / sizeof kCreate[0], "water.create");
}
bool InstallRoutes(registry::Mode)
{
    return InstallList(kRoutes, sizeof kRoutes / sizeof kRoutes[0], "water.routes");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.awmgr.water.create", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4WatercraftManager_CreateAutomaton,
                        "The watercraft manager's creation: CreateAutomaton, GetRandomCreationPoint, the lot at an "
                        "occupant, a lot's water body, the dock search and the open water test (6 detours; part aw5, "
                        "src/core/automata_awmgr_water_route.cpp)",
                        false, InstallCreate}),
    registry::Register({"simulators.automata.awmgr.water.routes", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4WatercraftManager_PlanRoute,
                        "The watercraft manager's routes: a terminal's boat with the path group test and the boat's "
                        "path, a route record's legs and new ferry, the boat's two legs and a leg's construction (7 "
                        "detours; part aw5, src/core/automata_awmgr_water_route.cpp)",
                        false, InstallRoutes}),
};

} // namespace
} // namespace opensc4::automata_awmgr
