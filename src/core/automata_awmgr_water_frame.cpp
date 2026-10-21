// Our watercraft manager's frames, part aw5 (build 641): the frame update
// (slot 63 of cSC4WatercraftManager, vtable 0x00A9DF38), the boats' path
// helpers, the edge spawn and the route record vector's helpers. The class
// map is in src/core/automata_awmgr.h and the survey in docs/automata.md,
// "Part aw"; the tables and the 0x3C-byte route records are described in
// src/core/automata_awmgr_water.cpp (part aw4). Calls between functions of
// the part, to the common base and to part aw5's other functions (the
// routes, src/core/automata_awmgr_water_route.cpp) go to the game's
// addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// The boat (cSC4Watercraft, 0xDC8 bytes): its path is a vector of 16-byte
// points at +0x2A4/+0x2A8 (a cell x and z at +0 and +4, a byte of step
// direction bits at +8); +0x2B4 a float, +0x2B8 a dword and +0x2CC a byte
// the edge spawn sets. The manager's reference vector (+0xAC8) holds the
// boats the edge spawn made.
//
// Group, off by default:
//   simulators.automata.awmgr.water.frame (10): the frame update 0x005736A0
//     (slot 63), the crossing rank 0x00566E60, the boat's step direction
//     0x005684C0 and path cut 0x0056D5F0, the edge spawn 0x005723D0 and its
//     edge cells 0x00571630, and the record vector's fill 0x00570CC0, copy
//     constructor 0x00572360, erase 0x00573230 and growing insert
//     0x00573580.
//
// Oddities kept:
//   - The frame update's crossing check walks every ordered pair of boats
//     (both with a path), each path's points but the last: at a shared cell
//     two boats of rank 0 (kinds 0x4400, 0x4402) stop the first (slot 0xD0)
//     and go on looking, so it can be stopped again; otherwise the one of
//     the higher rank (the second on a tie) has its path cut there and the
//     pair is done.
//   - The step direction takes an index below the count less two
//     (unsigned): the last step is never one, and a path of one point has
//     every index taken (it reads past the path, as the game does). (-1, -1)
//     and a step that is not a unit one both answer 0.
//   - The route cleanup lets a route's +8 and +0xC automata go when no
//     record of any route holds them.
//   - The edge spawn's timer starts at 10001 (above the 10000 it waits for)
//     and is reset only when a boat is made, so after 10000 it tries every
//     frame; the count test is unsigned against 0x00B0A170 (above it none);
//     the edge's centre cell's point is computed by the edge cells (city
//     slot 0x184) and never used; the side counter 0x00B22ABC is made side %
//     4 there and counted up after.
//   - After the city's ferry routes are read (+0xA88), a route's records
//     whose ferry route the city no longer has are stopped and erased, also
//     when the city has none; a record is matched by its ferry route's +0
//     and +4, or with the entry's byte +0x14 by +0 and the words +0x16 and
//     +0x18.
//   - The record vector's copy and insert divide by 0x3C signed; the insert
//     grows to size + max(size, count) (the max unsigned).
//
// Floats: 53-bit doubles, as the game's x87 runs; floats stored where the
// game stores them. The timers (0x00B22AD4, 0x00B22AD0, 0x00B22AC0) wait
// while strictly below their periods (test ah,5 / jp: equal and unordered
// run), and grow by the float time, one sum rounded to the float. The edge
// spawn's draws exit when strictly above 0.05 / 0.1 and make the kind 0x4417
// when strictly above 0.5 (test ah,0x41 / jz; unordered goes on). The edge's
// middle cell is (high - low) x 0.5 + low from the integers (x's low
// rounded to a float first), truncated as _ftol (0x009EEF04) does. The new
// boat's velocity is the difference to the centre's point (doubles) times
// 1.0 / sqrt((dz x float(dz) + dy x dy) + dx x dx), the scale rounded to a
// float and each product too, z from its rounded difference. The terrain's
// float (slot 0x74) and the random draws are the game's ST0 answers, taken
// as doubles.
//
// Stack leftovers: the frame update's two 0x34-byte ferry routes handed to
// 0x0056FD60 have their byte +0x28, dword +0x2C and byte +0x30 set only on
// some of its paths; the game passes what its stack holds (the earlier
// list entry's values, or leftovers the first time) on to 0x0056F670. Ours
// keeps those fields across the entries the same way and starts them at 0.
// The by-value copies' padding (+0x29..+0x2B, +0x32, +0x33) is never
// written in the game and is 0 in ours; a new record's padding the same. The edge spawn keeps its kind byte, and the
// reference holder of the new boat, in its time argument's slot (written
// before read); the record inserts' tag pointers point at argument slots
// (never read).
//
// Return values: the frame update answers the common base's slot 63 (EAX);
// the crossing rank and the step direction EAX in full; the path cut AL
// over the index (0) or over the vector copy's answer (1), as the game
// leaves EAX; the record helpers their pointers; the edge spawn, the edge
// cells and the growing insert nothing.
//
// Random draws: the edge spawn draws from the manager's generator
// (0x00B22A78, +0x50) with SC4Random::Unit at its address; nothing here
// seeds a generator.
//
// Still the game's, at their addresses: the common base's frame rate and
// slot 63; slot 15 CreateAutomaton (0x00568F30 through the vtable, part
// aw5's, reaching SC4UIScale's probe site 0x00505370); part aw5's other
// writer's 0x00568FD0, the boat's 0x0056F670, 0x0056FD60, 0x00570DF0 and 0x005710A0;
// part aw4's record copies, destroy and ferry route assignment; the ferry
// route's copy constructor 0x0056D770 and list clear 0x00570C50; the hash
// table begin, the 16-byte and 8-byte vector helpers, the reference holder
// and vector, the byte vector copy, the random draws and the allocators.
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

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }
void AddRef(void* object) { Virtual<std::uint32_t>(object, 0x04); }
void Release(void* object) { Virtual<std::uint32_t>(object, 0x08); }
// The game's globals and constants this part reads and writes.
namespace g {
inline constexpr std::uintptr_t kFrozen = 0x00B21B9C;     // byte: the frame rate's pause
inline constexpr std::uintptr_t kRandom = 0x00B22A78;     // the manager's generator (+0x50), set by Init
inline constexpr std::uintptr_t kPointStart = 0x00B22A80; // a float, the points' start
inline constexpr std::uintptr_t kSide = 0x00B22ABC;       // int32: the edge spawn's next side
inline constexpr std::uintptr_t kSpawnTimer = 0x00B22AC0; // float: the edge spawn's timer
inline constexpr std::uintptr_t kSpawnOnce = 0x00B22AC4;  // uint32: bit 0 once the timer is set
inline constexpr std::uintptr_t kRouteTimer = 0x00B22AD0; // float: the idle records' timer
inline constexpr std::uintptr_t kCleanTimer = 0x00B22AD4; // float: the route cleanup's timer
// Constants.
inline constexpr std::uintptr_t kMostEdgeBoats = 0x00B0A170; // uint32 5
inline constexpr std::uintptr_t kSpawnEvery = 0x00B0A174;    // float 10000
inline constexpr std::uintptr_t kRouteEvery = 0x00B0A184;    // float 35000
inline constexpr std::uintptr_t kCleanEvery = 0x00B0A188;    // float 60000
inline constexpr std::uintptr_t kSpeed = 0x00B0A18C;         // float 1.0
inline constexpr std::uintptr_t kOne = 0x00A81228;           // float 1.0
inline constexpr std::uintptr_t kHalf = 0x00A84D2C;          // float 0.5
inline constexpr std::uintptr_t kDriveNone = 0x00A9DEF8;     // double 0.05
inline constexpr std::uintptr_t kDriveYacht = 0x00A92D28;    // double 0.5
inline constexpr std::uintptr_t kNone = 0x00A9C7E8;          // double 0.1
} // namespace g

inline constexpr std::uint32_t kEdgeSpeedBits = 0x472FC800; // 45000.0f, the new boat's +0x2B4
inline constexpr std::uint32_t kGroupYacht = 0x4417, kGroupEdge = 0x4418;

namespace off {
inline constexpr std::size_t kPathBegin = 0x2A4, kPathEnd = 0x2A8; // the boat's 16-byte points
inline constexpr std::size_t kRefsEnd = field::kWaterRefs + 4;
} // namespace off

// A record of a route (0x3C bytes) and its ferry route in use (+4).
namespace record {
inline constexpr std::size_t kSize = 0x3C, kRoute = 0x04, kByte = 0x38;
} // namespace record

// A route node: +0 next, +4 the key, +8 and +0xC references, the record
// vector +0x10..+0x18, the dock occupant +0x1C.
namespace route {
inline constexpr std::size_t kKey = 0x04, kFirst = 0x08, kSecond = 0x0C, kBegin = 0x10, kEnd = 0x14,
                             kCapacity = 0x18;
} // namespace route

// A hash table iterator, passed by value where the game does.
struct Iterator {
    char* node;
    char* table;
};

// A 0x34-byte ferry route, passed by value to 0x0056F670.
struct FerryRoute {
    alignas(4) unsigned char bytes[0x34];
};
static_assert(sizeof(FerryRoute) == 0x34, "the ferry route's size");

std::uint32_t BucketCount(char* t)
{
    return static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin));
}

std::uint32_t KeyOf(const void* key) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(key)); }

// The first node of the first non-empty bucket (null for none), found by
// hand as the game inlines it.
char* FirstNode(char* t)
{
    for (std::uint32_t i = 0; i < BucketCount(t); ++i)
        if (char* const node = At<char**>(t, table::kBegin)[i])
            return node;
    return nullptr;
}

// The STLport iterator's step to the next node: the node's next, else the
// first node of a later bucket than its key's (null after the last).
char* NextNode(char* t, char* node)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    std::uint32_t i = At<std::uint32_t>(node, 4) % BucketCount(t);
    while (++i < BucketCount(t))
        if (char* const found = At<char**>(t, table::kBegin)[i])
            return found;
    return nullptr;
}

// The node of a key in its bucket, or null (the game's inlined find).
char* FindNode(char* t, void* key)
{
    char* node = At<char**>(t, table::kBegin)[KeyOf(key) % BucketCount(t)];
    while (node && At<void*>(node, 4) != key)
        node = At<char*>(node, 0);
    return node;
}

// Slot 0xD0 of an automaton with the game's arguments (0x49, 0, -1.0f as
// its bits, 0, 0, 0, 0).
void Stop(void* automaton)
{
    Virtual<void>(automaton, 0xD0, static_cast<std::uint32_t>(0x49), static_cast<std::uint32_t>(0),
                  static_cast<std::uint32_t>(0xBF800000), static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                  static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0));
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword.
std::int32_t Truncate(double value) { return static_cast<std::int32_t>(static_cast<std::int64_t>(value)); }

// The path's byte length (+0x2A8 - +0x2A4).
std::int32_t PathBytes(char* boat)
{
    return static_cast<std::int32_t>(At<char*>(boat, off::kPathEnd) - At<char*>(boat, off::kPathBegin));
}

// Whether a route record's ferry route (+4) is a list entry's: its +0 and +4,
// or with the entry's byte +0x14 its +0 and the words +0x16 and +0x18.
bool Matches(char* r, char* entry)
{
    char* const f = r + record::kRoute;
    if (!At<std::uint8_t>(entry, 0x14))
        return At<void*>(f, 0) == At<void*>(entry, 0) && At<void*>(f, 4) == At<void*>(entry, 4);
    return At<void*>(f, 0) == At<void*>(entry, 0) && At<std::uint16_t>(f, 0x16) == At<std::uint16_t>(entry, 0x16) &&
           At<std::uint16_t>(f, 0x18) == At<std::uint16_t>(entry, 0x18);
}

// The by-value copy of a 0x34-byte ferry route the frame update makes on
// its stack: the reference (referenced), the byte vector +4 copied
// (0x00459E80), the points +0x10..+0x24, the byte +0x28, the dword +0x2C
// and the bytes +0x30 and +0x31.
void CopyForCall(FerryRoute& to, char* from)
{
    std::memset(to.bytes, 0, sizeof to.bytes);
    char* const t = reinterpret_cast<char*>(to.bytes);
    void* const object = At<void*>(from, 0);
    At<void*>(t, 0) = object;
    if (object)
        AddRef(object);
    CallThis<void*>(addr::SC4ByteVector_CopyConstruct, t + 4, static_cast<const void*>(from + 4));
    for (std::size_t at = 0x10; at <= 0x24; at += 4)
        At<std::uint32_t>(t, at) = At<std::uint32_t>(from, at);
    At<std::uint8_t>(t, 0x28) = At<std::uint8_t>(from, 0x28);
    At<std::uint32_t>(t, 0x2C) = At<std::uint32_t>(from, 0x2C);
    At<std::uint8_t>(t, 0x30) = At<std::uint8_t>(from, 0x30);
    At<std::uint8_t>(t, 0x31) = At<std::uint8_t>(from, 0x31);
}

// The rectangle's middle cell as the edge cells and the edge spawn compute
// it: (high - low) x 0.5 + low on the integers, x's low rounded to a float
// first, each truncated.
void MiddleCell(const std::int32_t* rect, std::int32_t& x, std::int32_t& z)
{
    const double half = G<float>(g::kHalf);
    const float x0 = static_cast<float>(rect[0]);
    z = Truncate((static_cast<double>(rect[3]) - static_cast<double>(rect[1])) * half + static_cast<double>(rect[1]));
    x = Truncate((static_cast<double>(rect[2]) - static_cast<double>(x0)) * half + static_cast<double>(x0));
}

// ====================================================================================
// The frame (simulators.automata.awmgr.water.frame)

// 0x005736A0, slot 63 the frame update (a, time), ret 8: the rate
// (0x00507BF0); while paused (0x00B21B9C) the common base's slot 63 alone.
// Every 60000 (0x00B22AD4) the routes' +8 and +0xC let go when no record of
// any route holds them. Out of the U-Drive (slot 13): for every two boats
// with paths, at the first shared cell (each path's points but the last)
// the one of the higher rank (0x00566E60; the second on a tie) cut there
// (0x0056D5F0), or with both of rank 0 the first stopped at each shared
// cell. The edge spawn 0x005723D0. After 0x2C06BA82 (+0xA88): the city's
// ferry routes (city slot 0x114's slot 0x7C) put into their dock's route
// (a matching record's assigned, and routed with 0x0056FD60 / 0x0056F670
// when it has a boat; else a new record), then every record the list has
// not stopped and erased. Every 35000 (0x00B22AD0), out of the U-Drive: the
// records without a boat of the routes without +8 whose dock's slot 0x8C
// answers 0 routed (0x00570DF0) unless their destination's dock answers. The
// terminals (+0xAB4) without a boat planned (0x005710A0) with the group
// 0x4411, 0x4419 or 0x4416 by their type. Then the common base's slot 63.
// EAX its answer.
std::uint32_t __fastcall FrameUpdate(char* self, void*, std::uint32_t a, float time)
{
    static bool logged;
    FirstCall("WaterFrame", logged);
    CallCdecl<std::uint8_t>(addr::cSC4AutomataManager_FrameRate);
    if (G<std::uint8_t>(g::kFrozen))
        return CallThis<std::uint32_t>(addr::cSC4AutomataManager_Slot63, self, a, time);
    char* const routes = self + field::kWaterTable;

    // The route cleanup. test ah,5 / jp: strictly below the period waits.
    if (static_cast<double>(G<float>(g::kCleanTimer)) < static_cast<double>(G<float>(g::kCleanEvery))) {
        G<float>(g::kCleanTimer) = static_cast<float>(static_cast<double>(G<float>(g::kCleanTimer)) + time);
    } else {
        G<std::uint32_t>(g::kCleanTimer) = 0;
        Iterator it = {};
        CallThis<void*>(addr::SC4HashTable_Begin, routes, static_cast<void*>(&it));
        for (char* n = it.node; n; n = NextNode(it.table, n)) {
            if (!At<void*>(n, route::kFirst) && !At<void*>(n, route::kSecond))
                continue;
            bool first = false, second = false;
            Iterator inner = {};
            CallThis<void*>(addr::SC4HashTable_Begin, routes, static_cast<void*>(&inner));
            for (char* m = inner.node; m; m = NextNode(inner.table, m)) {
                char* const end = At<char*>(m, route::kEnd);
                char* r = At<char*>(m, route::kBegin);
                if (r == end)
                    continue;
                void* const one = At<void*>(n, route::kFirst);
                void* const two = At<void*>(n, route::kSecond);
                for (; r != end; r += record::kSize) {
                    if (one == At<void*>(r, 0))
                        first = true;
                    if (two == At<void*>(r, 0))
                        second = true;
                }
            }
            if (!first)
                if (void* const object = At<void*>(n, route::kFirst)) {
                    At<void*>(n, route::kFirst) = nullptr;
                    Release(object);
                }
            if (!second)
                if (void* const object = At<void*>(n, route::kSecond)) {
                    At<void*>(n, route::kSecond) = nullptr;
                    Release(object);
                }
        }
    }

    // The crossing paths, out of the U-Drive.
    if (!Virtual<std::uint8_t>(self, slot::kIsDrivingMode)) {
        for (char* n1 = At<char*>(At<char*>(self, field::kAutomata), 0); n1 != At<char*>(self, field::kAutomata);
             n1 = At<char*>(n1, 0)) {
            char* const one = At<char*>(n1, 8);
            if (!one || !(static_cast<std::uint32_t>(PathBytes(one)) & 0xFFFFFFF0u))
                continue;
            const std::uint32_t rank1 = CallThis<std::uint32_t>(addr::cSC4WatercraftManager_CrossingRank, self, one);
            for (char* n2 = At<char*>(At<char*>(self, field::kAutomata), 0); n2 != At<char*>(self, field::kAutomata);
                 n2 = At<char*>(n2, 0)) {
                char* const two = At<char*>(n2, 8);
                if (!two || one == two || !(static_cast<std::uint32_t>(PathBytes(two)) & 0xFFFFFFF0u))
                    continue;
                const std::int32_t last1 = (PathBytes(one) >> 4) - 1;
                const std::int32_t last2 = (PathBytes(two) >> 4) - 1;
                const std::uint32_t rank2 =
                    CallThis<std::uint32_t>(addr::cSC4WatercraftManager_CrossingRank, self, two);
                bool done = false;
                for (std::int32_t i = 0; i < last1 && !done; ++i)
                    for (std::int32_t j = 0; j < last2; ++j) {
                        char* const p = At<char*>(one, off::kPathBegin) + static_cast<std::uint32_t>(i) * 16;
                        char* const q = At<char*>(two, off::kPathBegin) + static_cast<std::uint32_t>(j) * 16;
                        if (At<std::int32_t>(p, 0) != At<std::int32_t>(q, 0) ||
                            At<std::int32_t>(p, 4) != At<std::int32_t>(q, 4))
                            continue;
                        if (rank1 == 0 && rank2 == 0) {
                            Stop(one);
                            continue;
                        }
                        // JG: the first's rank above the second's (signed) cuts the first.
                        if (static_cast<std::int32_t>(rank1) <= static_cast<std::int32_t>(rank2))
                            CallThis<std::uint32_t>(addr::cSC4Watercraft_CutPathAt, two, j);
                        else
                            CallThis<std::uint32_t>(addr::cSC4Watercraft_CutPathAt, one, i);
                        done = true;
                        break;
                    }
            }
        }
    }

    CallThis<void>(addr::cSC4WatercraftManager_SpawnAtEdge, self, time);

    // The city's ferry routes, after 0x2C06BA82.
    if (At<std::uint8_t>(self, field::kWaterByte)) {
        At<std::uint8_t>(self, field::kWaterByte) = 0;
        char* list = nullptr;
        char* const head = static_cast<char*>(Allocate(0x3C));
        At<char*>(head, 0) = head;
        At<char*>(head, 4) = head;
        list = head;
        Virtual<std::uint32_t>(Virtual<void*>(G<void*>(addr::SC4_City), 0x114), 0x7C, static_cast<void*>(&list));
        // The two ferry routes handed to 0x0056FD60: fields it does not
        // always set keep their values from one entry to the next.
        alignas(4) char from[0x34] = {};
        alignas(4) char to[0x34] = {};
        for (char* l = At<char*>(list, 0); l != list; l = At<char*>(l, 0)) {
            char* const entry = l + 8;
            char* const n = FindNode(routes, At<void*>(entry, 0));
            if (!n)
                continue;
            char* r = At<char*>(n, route::kBegin);
            for (char* const end = At<char*>(n, route::kEnd); r != end; r += record::kSize)
                if (Matches(r, entry))
                    break;
            if (r != At<char*>(n, route::kEnd)) {
                CallThis<char*>(addr::cSC4WatercraftManager_AssignFerryRoute, r + record::kRoute,
                                static_cast<const void*>(entry));
                if (!At<void*>(r, 0))
                    continue;
                const std::uint32_t start = G<std::uint32_t>(g::kPointStart);
                for (char* const f : {from, to}) {
                    for (std::size_t at = 0; at <= 0xC; at += 4)
                        At<std::uint32_t>(f, at) = 0;
                    for (std::size_t at = 0x10; at <= 0x24; at += 4)
                        At<std::uint32_t>(f, at) = start;
                }
                CallThis<void>(addr::cSC4WatercraftManager_RouteFromDock, self, Iterator{n, routes}, r,
                               static_cast<void*>(from), static_cast<void*>(to));
                void* const toObject = At<void*>(to, 0);
                FerryRoute toCopy, fromCopy;
                CopyForCall(toCopy, to);
                CopyForCall(fromCopy, from);
                CallThis<void>(addr::cSC4Watercraft_SetFerryRoute, At<void*>(r, 0), fromCopy, toCopy,
                               static_cast<std::uint32_t>(0));
                if (void* const buffer = At<void*>(to, 4))
                    Free(buffer);
                if (toObject)
                    Release(toObject);
                if (void* const buffer = At<void*>(from, 4))
                    Free(buffer);
                if (void* const object = At<void*>(from, 0))
                    Release(object);
                continue;
            }
            // A new record (no automaton) with the entry's ferry route.
            alignas(4) char fresh[record::kSize] = {};
            CallThis<char*>(addr::cSC4WatercraftManager_AssignFerryRoute, fresh + record::kRoute,
                            static_cast<const void*>(entry));
            void* const automaton = At<void*>(fresh, 0);
            At<std::uint8_t>(fresh, record::kByte) = 0;
            char* const end = At<char*>(n, route::kEnd);
            if (end != At<char*>(n, route::kCapacity)) {
                if (end) {
                    At<void*>(end, 0) = automaton;
                    if (automaton)
                        AddRef(automaton);
                    CallThis<void*>(addr::SC4FerryRouteInUse_CopyConstruct, end + record::kRoute,
                                    static_cast<const void*>(fresh + record::kRoute));
                    At<std::uint8_t>(end, record::kByte) = 0;
                }
                At<char*>(n, route::kEnd) += record::kSize;
            } else {
                char tag = 0;
                CallThis<void>(addr::cSC4WatercraftManager_InsertRecordOverflow, n + route::kBegin, end,
                               static_cast<const void*>(fresh), static_cast<void*>(&tag), static_cast<std::uint32_t>(1),
                               static_cast<std::uint32_t>(1));
            }
            for (const std::size_t at : {0x2Cu, 0x20u})
                if (void* const buffer = At<void*>(fresh, at))
                    Free(buffer);
            for (const std::size_t at : {0x08u, 0x04u})
                if (void* const object = At<void*>(fresh, at))
                    Release(object);
            if (automaton)
                Release(automaton);
        }
        // Every record the list has not: its automaton stopped, the record
        // erased.
        for (char* n = FirstNode(routes); n; n = NextNode(routes, n)) {
            char* r = At<char*>(n, route::kBegin);
            char* end = At<char*>(n, route::kEnd);
            if (r == end)
                continue;
            do {
                bool found = false;
                for (char* l = At<char*>(list, 0); l != list; l = At<char*>(l, 0))
                    if (Matches(r, l + 8)) {
                        found = true;
                        break;
                    }
                if (found) {
                    r += record::kSize;
                    continue;
                }
                if (void* const automaton = At<void*>(r, 0))
                    Stop(automaton);
                r = CallThis<char*>(addr::cSC4WatercraftManager_EraseRecord, n + route::kBegin, r);
                end = At<char*>(n, route::kEnd);
            } while (r != end);
        }
        CallThis<void>(addr::SC4FerryRouteList_Clear, static_cast<void*>(&list));
        if (list)
            Free(list);
    }

    // The idle records. test ah,5 / jp: strictly below the period waits.
    if (static_cast<double>(G<float>(g::kRouteTimer)) < static_cast<double>(G<float>(g::kRouteEvery))) {
        G<float>(g::kRouteTimer) = static_cast<float>(static_cast<double>(G<float>(g::kRouteTimer)) + time);
    } else {
        G<std::uint32_t>(g::kRouteTimer) = 0;
        if (!Virtual<std::uint8_t>(self, slot::kIsDrivingMode))
            for (char* n = FirstNode(routes); n; n = NextNode(routes, n)) {
                if (At<void*>(n, route::kFirst))
                    continue;
                void* const dock = At<void*>(n, route::kKey);
                if (!dock || Virtual<std::uint32_t>(dock, 0x8C))
                    continue;
                for (char* r = At<char*>(n, route::kBegin); r != At<char*>(n, route::kEnd); r += record::kSize) {
                    if (At<void*>(r, 0))
                        continue;
                    char* const other = FindNode(routes, At<void*>(r, 8));
                    if (other && Virtual<std::uint32_t>(At<void*>(other, route::kKey), 0x8C))
                        continue;
                    CallThis<void>(addr::cSC4WatercraftManager_NextFerryRoute, self, Iterator{n, routes},
                                   Iterator{other, routes}, r);
                }
            }
    }

    // The terminals without a boat.
    char* const terminals = self + field::kWaterTable3;
    for (char* p = FirstNode(terminals); p; p = NextNode(terminals, p)) {
        if (!At<void*>(p, 4) || At<void*>(p, 8))
            continue;
        std::uint32_t group = 0x4416;
        if (Virtual<std::uint8_t>(Virtual<void*>(At<void*>(p, 4), 0x18), 0x0C, static_cast<std::uint32_t>(0x6C36283C)))
            group = 0x4411;
        else if (Virtual<std::uint8_t>(Virtual<void*>(At<void*>(p, 4), 0x18), 0x0C,
                                       static_cast<std::uint32_t>(0xCC3A1723)))
            group = 0x4419;
        CallThis<void>(addr::cSC4WatercraftManager_PlanRoute, self, Iterator{p, terminals}, group);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_Slot63, self, a, time);
}

// 0x00566E60, a boat's crossing rank (automaton), ret 4 (this not read): 0
// for the kinds 0x4400 and 0x4402 (slot 67's +0x10, asked twice for the
// second), 1 when its occupant (slot 5) is in the group 0x4414, 2 in
// 0x4413, else 3; 3 without one. EAX.
std::uint32_t __fastcall CrossingRank(void*, void*, void* automaton)
{
    static bool logged;
    FirstCall("WaterCrossingRank", logged);
    if (!automaton)
        return 3;
    if (At<std::uint32_t>(Virtual<char*>(automaton, 0x10C), 0x10) == 0x4400 ||
        At<std::uint32_t>(Virtual<char*>(automaton, 0x10C), 0x10) == 0x4402)
        return 0;
    if (Virtual<std::uint8_t>(Virtual<void*>(automaton, 0x14), 0x58, static_cast<std::uint32_t>(0x4414)))
        return 1;
    if (Virtual<std::uint8_t>(Virtual<void*>(automaton, 0x14), 0x58, static_cast<std::uint32_t>(0x4413)))
        return 2;
    return 3;
}

// 0x005684C0, the boat's step direction (index), ret 4: for an index below
// the point count less two (unsigned) the step from the point to the next
// one: (0, -1) 1, (1, -1) 2, (1, 0) 3, (1, 1) 4, (0, 1) 5, (-1, 1) 6,
// (-1, 0) 7; (-1, -1), any other step and any other index 0. EAX.
std::uint32_t __fastcall StepDirection(char* self, void*, std::uint32_t index)
{
    static bool logged;
    FirstCall("WaterStepDirection", logged);
    if (index >= static_cast<std::uint32_t>((PathBytes(self) >> 4) - 2))
        return 0;
    char* const p = At<char*>(self, off::kPathBegin) + index * 16;
    const std::int32_t dx = static_cast<std::int32_t>(At<std::uint32_t>(p, 0x10) - At<std::uint32_t>(p, 0));
    const std::int32_t dz = static_cast<std::int32_t>(At<std::uint32_t>(p, 0x14) - At<std::uint32_t>(p, 4));
    if (dx == -1) {
        if (dz == -1)
            return 0;
        if (dz == 1)
            return 6;
        return dz == 0 ? 7 : 0;
    }
    if (dx == 0) {
        if (dz == -1)
            return 1;
        return dz == 1 ? 5 : 0;
    }
    if (dx != 1)
        return 0;
    if (dz == -1)
        return 2;
    if (dz == 0)
        return 3;
    return dz == 1 ? 4 : 0;
}

// 0x0056D5F0, the boat's path cut (index), ret 4: for an index from 0 and
// below the point count (unsigned), the point's byte +8 gets the bit of its
// step's direction (0x005684C0, 1 << it as a byte), the path's end set
// after the point (0x007257C0 copying nothing to there). AL 1 over the
// copy's answer; AL 0 over the index for an index out of range.
std::uint32_t __fastcall CutPathAt(char* self, void*, std::int32_t index)
{
    static bool logged;
    FirstCall("WaterCutPath", logged);
    if (index < 0 || static_cast<std::uint32_t>(index) >= static_cast<std::uint32_t>(PathBytes(self) >> 4))
        return static_cast<std::uint32_t>(index) & 0xFFFFFF00u; // XOR AL,AL over the index
    char* const point = At<char*>(self, off::kPathBegin) + static_cast<std::uint32_t>(index) * 16;
    const std::uint32_t direction = CallThis<std::uint32_t>(addr::cSC4Watercraft_StepDirection, self, index);
    // MOV DL,1 / SHL DL,CL: the count masked to five bits, a byte.
    const std::uint32_t count = direction & 31;
    const std::uint8_t bit = count < 8 ? static_cast<std::uint8_t>(1u << count) : 0;
    At<std::uint8_t>(point, 8) = static_cast<std::uint8_t>(At<std::uint8_t>(point, 8) | bit);
    char* const end = At<char*>(self, off::kPathEnd);
    char* const cut = CallCdecl<char*>(addr::SC4Vector16_Copy, end, end, point + 16, static_cast<void*>(&index),
                                       static_cast<std::uint32_t>(0));
    At<char*>(self, off::kPathEnd) = cut;
    return (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cut)) & 0xFFFFFF00u) | 1u;
}

// 0x00570CC0 (dest, count, record, tag), cdecl: count copies of the record
// constructed at dest (the automaton referenced, the ferry route by its copy
// constructor 0x0056D770, the byte +0x38); nothing written at a null dest.
// The end.
char* __cdecl FillRecords(char* dest, std::uint32_t count, char* value, void*)
{
    static bool logged;
    FirstCall("WaterFillRecords", logged);
    for (; count != 0; --count, dest += record::kSize) {
        if (!dest)
            continue;
        void* const automaton = At<void*>(value, 0);
        At<void*>(dest, 0) = automaton;
        if (automaton)
            AddRef(automaton);
        CallThis<void*>(addr::SC4FerryRouteInUse_CopyConstruct, dest + record::kRoute,
                        static_cast<const void*>(value + record::kRoute));
        At<std::uint8_t>(dest, record::kByte) = At<std::uint8_t>(value, record::kByte);
    }
    return dest;
}

// 0x00572360, a record vector's copy constructor (other), ret 4: storage for
// the other's count (none for 0; the count a signed division by 0x3C), the
// records copied into it (0x00570D10). EAX this.
char* __fastcall CopyRecordVector(char* self, void*, char* other)
{
    static bool logged;
    FirstCall("WaterCopyRecordVector", logged);
    const std::int32_t n = static_cast<std::int32_t>(At<char*>(other, 4) - At<char*>(other, 0)) /
                           static_cast<std::int32_t>(record::kSize);
    At<char*>(self, 0) = nullptr;
    At<char*>(self, 4) = nullptr;
    At<char*>(self, 8) = nullptr;
    char* const storage =
        n != 0 ? static_cast<char*>(Allocate(static_cast<std::uint32_t>(n) * record::kSize)) : nullptr;
    At<char*>(self, 0) = storage;
    At<char*>(self, 4) = storage;
    At<char*>(self, 8) = storage + static_cast<std::uint32_t>(n) * record::kSize;
    At<char*>(self, 4) = CallCdecl<char*>(addr::cSC4WatercraftManager_ConstructRecords, At<char*>(other, 0),
                                          At<char*>(other, 4), storage, static_cast<void*>(&other));
    return self;
}

// 0x00573230, a record vector's erase of one (position), ret 4: the records
// after it copied down onto it (0x00570D60, when there are any), the last
// destroyed (its byte vector buffers +0x2C and +0x20 freed, its references
// +8, +4 and +0 released) and the end moved back. EAX the position.
char* __fastcall EraseRecord(char* self, void*, char* position)
{
    static bool logged;
    FirstCall("WaterEraseRecord", logged);
    char* const next = position + record::kSize;
    if (next != At<char*>(self, 4))
        CallCdecl<char*>(addr::cSC4WatercraftManager_CopyRecords, next, At<char*>(self, 4), position,
                         static_cast<void*>(&position), static_cast<std::uint32_t>(0));
    char* const last = At<char*>(self, 4) - record::kSize;
    At<char*>(self, 4) = last;
    for (const std::size_t at : {0x2Cu, 0x20u})
        if (void* const buffer = At<void*>(last, at))
            Free(buffer);
    for (const std::size_t at : {0x08u, 0x04u, 0x00u})
        if (void* const object = At<void*>(last, at))
            Release(object);
    return position;
}

// 0x00573580, a record vector's growing insert (position, record, tag,
// count, atEnd), ret 0x14: new storage for size + max(size, count) (none
// for 0; the max unsigned); the records before the position copied in
// (0x00570D10), then the record once (by hand) or count times (0x00570CC0),
// then unless atEnd's byte the records from the position; the old ones
// destroyed (0x0056F740) and freed; the vector set to the new storage.
void __fastcall InsertRecordOverflow(char* self, void*, char* position, char* value, void*, std::uint32_t count,
                                     std::uint32_t atEnd)
{
    static bool logged;
    FirstCall("WaterInsertRecordOverflow", logged);
    const std::uint32_t size = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(At<char*>(self, 4) - At<char*>(self, 0)) / static_cast<std::int32_t>(record::kSize));
    const std::uint32_t capacity = (size < count ? count : size) + size;
    char* const storage = capacity != 0 ? static_cast<char*>(Allocate(capacity * record::kSize)) : nullptr;
    void* const tag = static_cast<void*>(&atEnd);
    char* end =
        CallCdecl<char*>(addr::cSC4WatercraftManager_ConstructRecords, At<char*>(self, 0), position, storage, tag);
    if (count == 1) {
        if (end) {
            void* const automaton = At<void*>(value, 0);
            At<void*>(end, 0) = automaton;
            if (automaton)
                AddRef(automaton);
            CallThis<void*>(addr::SC4FerryRouteInUse_CopyConstruct, end + record::kRoute,
                            static_cast<const void*>(value + record::kRoute));
            At<std::uint8_t>(end, record::kByte) = At<std::uint8_t>(value, record::kByte);
        }
        end += record::kSize;
    } else {
        end = CallCdecl<char*>(addr::cSC4WatercraftManager_FillRecords, end, count, value, tag);
    }
    if (!static_cast<std::uint8_t>(atEnd))
        end = CallCdecl<char*>(addr::cSC4WatercraftManager_ConstructRecords, position, At<char*>(self, 4), end, tag);
    CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, At<void*>(self, 0), At<void*>(self, 4), tag);
    if (void* const old = At<void*>(self, 0))
        Free(old);
    At<char*>(self, 4) = end;
    At<char*>(self, 0) = storage;
    At<char*>(self, 8) = storage + capacity * record::kSize;
}

// Pushes a cell (x, z) onto a vector of 8-byte cells: in place while there
// is room (nothing written at a null end), else SC4PointVector::InsertN
// (where the end, one, at the end).
void PushCell(char* cells, std::int32_t x, std::int32_t z)
{
    std::int32_t cell[2] = {x, z};
    char* const end = At<char*>(cells, 4);
    if (end != At<char*>(cells, 8)) {
        if (end) {
            At<std::int32_t>(end, 0) = x;
            At<std::int32_t>(end, 4) = z;
        }
        At<char*>(cells, 4) += 8;
        return;
    }
    char tag = 0;
    CallThis<void>(addr::SC4PointVector_InsertN, cells, end, static_cast<const void*>(cell), static_cast<void*>(&tag),
                   static_cast<std::uint32_t>(1), static_cast<std::uint32_t>(1));
}

// 0x00571630, the cells of a map edge (side*, cells), ret 8 (this not
// read): the side made side % 4 (C's, stored back); the rectangle of
// 0x00B43D54's slot 0x44 (x0, z0, x1, z1); the terrain's slot 0x74 and the
// middle cell's point (city slot 0x184), never used; then side 0 (x0 + i,
// z0) for i below x1 - x0, 1 (x1, z0 + i) below z1 - z0, 2 (x0 + i, z1), 3
// (x0, z0 + i), pushed onto the cells; nothing for a negative side or an
// empty edge.
void __fastcall EdgeCells(void*, void*, std::int32_t* side, char* cells)
{
    static bool logged;
    FirstCall("WaterEdgeCells", logged);
    const std::int32_t s = *side % 4;
    *side = s;
    const std::int32_t* const r = Virtual<const std::int32_t*>(G<void*>(addr::SC4_Service43D54), 0x44);
    const std::int32_t rect[4] = {r[0], r[1], r[2], r[3]};
    float point[3];
    point[1] = static_cast<float>(Virtual<double>(G<void*>(addr::SC4_Terrain), 0x74));
    point[0] = 0.0f;
    point[2] = 0.0f;
    std::int32_t mx, mz;
    MiddleCell(rect, mx, mz);
    Virtual<std::uint32_t>(G<void*>(addr::SC4_City), 0x184, mx, mz, static_cast<void*>(&point[0]),
                           static_cast<void*>(&point[2]));
    const std::int32_t width = static_cast<std::int32_t>(static_cast<std::uint32_t>(rect[2]) - rect[0]);
    const std::int32_t height = static_cast<std::int32_t>(static_cast<std::uint32_t>(rect[3]) - rect[1]);
    const auto step = [](std::int32_t from, std::int32_t i) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(from) + static_cast<std::uint32_t>(i));
    };
    switch (s) {
    case 0:
        for (std::int32_t i = 0; i < width; ++i)
            PushCell(cells, step(rect[0], i), rect[1]);
        break;
    case 1:
        for (std::int32_t i = 0; i < height; ++i)
            PushCell(cells, rect[2], step(rect[1], i));
        break;
    case 2:
        for (std::int32_t i = 0; i < width; ++i)
            PushCell(cells, step(rect[0], i), rect[3]);
        break;
    case 3:
        for (std::int32_t i = 0; i < height; ++i)
            PushCell(cells, rect[0], step(rect[1], i));
        break;
    default:
        break;
    }
}

// 0x005723D0, the edge spawn (time), ret 4: the timer 0x00B22AC0 (first
// 10000 + 1.0) grows by the time while strictly below 10000. Else, with at
// most 0x00B0A170 boats in +0xAC8 (unsigned) and a first pair table entry:
// in the U-Drive a draw above 0.05 ends it and a second above 0.5 makes the
// kind 0x4417; else a draw above 0.1 ends it (no generator, no draw). The
// map's centre point (0x00B43D54's rectangle, the terrain's slot 0x74, city
// slot 0x184); the next edge's cells (0x00571630 with 0x00B22ABC, counted
// up), each in the city (slot 0x18C) and passing 0x00568FD0 (2) made a
// boat of 0x4417 or 0x4418 (slot 15) at its point (city slot 0x184, the
// terrain's height) until one is made: the timer 0, its velocity (slot 0x40)
// towards the centre of length 1.0 (0x00B0A18C), +0x2B4 45000, +0x2B8 0,
// +0x2CC the kind byte, pushed onto +0xAC8 (referenced). The cells freed.
void __fastcall SpawnAtEdge(char* self, void*, float time)
{
    static bool logged;
    FirstCall("WaterSpawnAtEdge", logged);
    if (!(G<std::uint8_t>(g::kSpawnOnce) & 1)) {
        G<std::uint32_t>(g::kSpawnOnce) |= 1;
        G<float>(g::kSpawnTimer) =
            static_cast<float>(static_cast<double>(G<float>(g::kSpawnEvery)) + static_cast<double>(G<float>(g::kOne)));
    }
    // test ah,5 / jp: strictly below waits; equal and unordered go on.
    if (static_cast<double>(G<float>(g::kSpawnTimer)) < static_cast<double>(G<float>(g::kSpawnEvery))) {
        G<float>(g::kSpawnTimer) = static_cast<float>(static_cast<double>(G<float>(g::kSpawnTimer)) + time);
        return;
    }
    const std::int32_t boats =
        static_cast<std::int32_t>(At<char*>(self, off::kRefsEnd) - At<char*>(self, field::kWaterRefs)) >> 2;
    if (static_cast<std::uint32_t>(boats) > G<std::uint32_t>(g::kMostEdgeBoats))
        return;
    if (At<std::uint32_t>(self, field::kWaterTable2 + table::kCount) < 1)
        return;
    // The kind byte (the game keeps it in the time argument's slot).
    std::uint8_t yacht = 0;
    // test ah,0x41 / jz: a draw strictly above the bound (ordered) ends it.
    if (Virtual<std::uint8_t>(self, slot::kIsDrivingMode)) {
        if (void* const random = G<void*>(g::kRandom)) {
            if (CallThis<double>(addr::SC4Random_Unit, random) > G<double>(g::kDriveNone))
                return;
            if (void* const again = G<void*>(g::kRandom))
                if (CallThis<double>(addr::SC4Random_Unit, again) > G<double>(g::kDriveYacht))
                    yacht = 1;
        }
    } else if (void* const random = G<void*>(g::kRandom)) {
        if (CallThis<double>(addr::SC4Random_Unit, random) > G<double>(g::kNone))
            return;
    }
    const std::int32_t* const r = Virtual<const std::int32_t*>(G<void*>(addr::SC4_Service43D54), 0x44);
    const std::int32_t rect[4] = {r[0], r[1], r[2], r[3]};
    float centre[3];
    centre[1] = static_cast<float>(Virtual<double>(G<void*>(addr::SC4_Terrain), 0x74));
    centre[0] = 0.0f;
    centre[2] = 0.0f;
    std::int32_t mx, mz;
    MiddleCell(rect, mx, mz);
    Virtual<std::uint32_t>(G<void*>(addr::SC4_City), 0x184, mx, mz, static_cast<void*>(&centre[0]),
                           static_cast<void*>(&centre[2]));
    alignas(4) char cells[12] = {};
    CallThis<void>(addr::cSC4WatercraftManager_EdgeCells, self, static_cast<void*>(&G<std::int32_t>(g::kSide)),
                   static_cast<void*>(cells));
    ++G<std::int32_t>(g::kSide);
    char* const begin = At<char*>(cells, 0);
    const std::uint32_t count = static_cast<std::uint32_t>(static_cast<std::int32_t>(At<char*>(cells, 4) - begin) >> 3);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::int32_t x = At<std::int32_t>(begin, i * 8);
        const std::int32_t z = At<std::int32_t>(begin, i * 8 + 4);
        if (!Virtual<std::uint8_t>(G<void*>(addr::SC4_City), 0x18C, x, z))
            continue;
        if (!CallThis<std::uint8_t>(addr::cSC4WatercraftManager_IsOpenWater, self, x, z, static_cast<std::uint32_t>(2)))
            continue;
        const std::uint32_t start = G<std::uint32_t>(g::kPointStart);
        std::uint32_t bits[3] = {start, start, start};
        float p[3];
        std::memcpy(p, bits, sizeof p);
        Virtual<std::uint32_t>(G<void*>(addr::SC4_City), 0x184, At<std::int32_t>(begin, i * 8),
                               At<std::int32_t>(begin, i * 8 + 4), static_cast<void*>(&p[0]),
                               static_cast<void*>(&p[2]));
        p[1] = static_cast<float>(Virtual<double>(G<void*>(addr::SC4_Terrain), 0x74));
        float zero[3] = {0.0f, 0.0f, 0.0f};
        void* const boat = Virtual<void*>(self, 0x3C, yacht ? kGroupYacht : kGroupEdge, static_cast<void*>(p),
                                          static_cast<std::uint32_t>(0x10002), static_cast<void*>(zero),
                                          static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                                          static_cast<std::uint32_t>(0));
        if (!boat)
            continue;
        const double dx = static_cast<double>(centre[0]) - static_cast<double>(p[0]);
        const double dy = static_cast<double>(centre[1]) - static_cast<double>(p[1]);
        const double dz = static_cast<double>(centre[2]) - static_cast<double>(p[2]);
        G<std::uint32_t>(g::kSpawnTimer) = 0;
        float v[3];
        v[2] = static_cast<float>(dz); // FST: dz rounded, the unrounded kept
        const double length = std::sqrt((dz * static_cast<double>(v[2]) + dy * dy) + dx * dx);
        const float scale = static_cast<float>(static_cast<double>(G<float>(g::kSpeed)) / length);
        v[0] = static_cast<float>(dx * static_cast<double>(scale));
        v[1] = static_cast<float>(dy * static_cast<double>(scale));
        v[2] = static_cast<float>(static_cast<double>(v[2]) * static_cast<double>(scale));
        Virtual<std::uint32_t>(boat, 0x40, static_cast<void*>(p), static_cast<void*>(v));
        At<std::uint32_t>(boat, 0x2B4) = kEdgeSpeedBits;
        At<std::uint32_t>(boat, 0x2B8) = 0;
        At<std::uint8_t>(boat, 0x2CC) = yacht;
        void* holder = nullptr;
        CallThis<void**>(addr::SC4RefHolder_SC4RefHolder, static_cast<void*>(&holder), boat);
        CallThis<void>(addr::SC4RefVector_PushBack, self + field::kWaterRefs, static_cast<void*>(&holder));
        if (holder)
            Release(holder);
        break;
    }
    if (begin)
        Free(begin);
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

const Detour kFrame[] = {
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_FrameUpdate, FrameUpdate),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_CrossingRank, CrossingRank),
    OPENSC4_DETOUR(addr::cSC4Watercraft_StepDirection, StepDirection),
    OPENSC4_DETOUR(addr::cSC4Watercraft_CutPathAt, CutPathAt),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_FillRecords, FillRecords),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_CopyRecordVector, CopyRecordVector),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_EraseRecord, EraseRecord),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_InsertRecordOverflow, InsertRecordOverflow),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_EdgeCells, EdgeCells),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_SpawnAtEdge, SpawnAtEdge),
};

#undef OPENSC4_DETOUR

bool InstallFrame(registry::Mode) { return InstallList(kFrame, sizeof kFrame / sizeof kFrame[0], "water.frame"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.awmgr.water.frame", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4WatercraftManager_FrameUpdate,
                        "The watercraft manager's frames: the frame update (slot 63), the crossing rank, the boat's "
                        "step direction and path cut, the edge spawn and its edge cells, and the record vector's "
                        "fill, copy constructor, erase and growing insert (10 detours; part aw5, "
                        "src/core/automata_awmgr_water_frame.cpp)",
                        false, InstallFrame}),
};

} // namespace
} // namespace opensc4::automata_awmgr
