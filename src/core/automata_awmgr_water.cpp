// Our watercraft manager's life cycle, save and messages, part aw4 (build
// 641): 24 functions of cSC4WatercraftManager (vtable 0x00A9DF38). The class
// map is in src/core/automata_awmgr.h and the survey in docs/automata.md,
// "Part aw". Calls between functions of the part, to the common base and to
// the watercraft functions of part aw5 go to the game's addresses, so each
// detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses, hex,
// eight digits) bisects them.
//
// The three tables (automata_awmgr.h): +0xA8C the routes (0x20-byte nodes:
// the key at +4, a route value at +8 of a reference +8, a reference +0xC, a
// vector of 0x3C-byte records +0x10..+0x18 and a reference +0x1C), +0xAA0
// and +0xAB4 the pairs (0xC-byte nodes, references at +4, the key, and +8).
// A record: a reference +0 (the automaton), a ferry route in use at +4
// (0x34 bytes: references +4 and +8, a word +0xC, words +0x10..+0x16, a byte
// +0x18, words +0x1A and +0x1C, byte vectors +0x20 and +0x2C) and a byte
// +0x38. Message 0x99EF1142 (an occupant added) fills the tables, 0x99EF1143
// (removed) empties them.
//
// Groups, off by default:
//   simulators.automata.awmgr.water.life (5): Init, Shutdown, the tuning
//     (slot 24), AddAutomaton (7) and RemoveAutomaton (8). They use no table
//     or record helper of the part (Shutdown clears the tables through part
//     aw1's 0x005743D0 and 0x0056D670).
//   simulators.automata.awmgr.water.serial (8): Write, Read, the pair insert
//     0x0056C850, the route insert 0x00574260, the record copies 0x00570D10
//     (into new storage) and 0x00570D60 (assigned), the ferry route
//     assignment 0x0056D7F0 and the records' destroy 0x0056F740 (also the
//     messages' and part aw1's).
//   simulators.automata.awmgr.water.messages (11): DoMessage, the occupant's
//     addition 0x00575170 and removal 0x00574520, the route table's erase
//     0x00574340 and node release 0x00574230, the route value's destructor
//     0x005732F0, the records' clear 0x005734E0, the route pair's
//     constructor 0x00573520, the route insert with its resize 0x00574830,
//     the pair tables' erase 0x0056D6E0 and node release 0x0056C910.
//
// Oddities kept:
//   - Read keeps no reference of those it reads: each one ReadReference
//     hands back is copied into the tables with references of their own and
//     the stack copies released, the read ones never. Write saves only the
//     route key, the route value's +0x1C and of each record its ferry route
//     (not the automaton at +0, nor the value's +8 and +0xC); Read gives the
//     second pair table's nodes a null +8.
//   - Write saves a record's word +0xC as 0, 1 or -1 (any other value), and
//     Read keeps the slot's earlier value for anything but 0 and 1. That
//     slot and the ferry route's words and byte live across all the records
//     of a Read, as the game's stack record does; the first time they are
//     stack leftovers in the game, 0 in ours.
//   - Read takes the reference vector's and the tables' old contents away
//     before it reads them; a failed count read keeps the earlier count
//     (one stack slot for all three).
//   - Read answers 0 above version 6 and hands a version below 6 to the
//     common base's body alone; Write and Read answer 1 when the stream's
//     slot 18 answers 0 (EAX 0 or 1).
//   - RemoveAutomaton looks the automaton up in the route records only for
//     the kinds 0x4400 and 0x4402 (slot 67's +0x10), in the second pair
//     table for the groups 0x4416, 0x4411, 0x440F and 0x440A and in the
//     reference vector for 0x4418, each time dropping the first place found
//     and going to the base; the vector's last element is released after the
//     shift.
//   - The occupant's addition makes a route only when there is none for its
//     lot yet and the dock search (0x00568CE0) answers; the first pair
//     table's entry is made whatever that search answers. Its removal stops
//     the route's records' automata (slot 0xD0) before it erases them.
//   - DoMessage answers 1 for the three types it takes and when the base's
//     answers, else 0 (EAX).
//
// No floats are computed; the float constant -1.0 passed to the automata's
// slot 0xD0 goes as its bits, the points of the dock search start as the
// word at 0x00B22A80 copied.
//
// Still the game's, at their addresses: the common base's Init, Shutdown,
// AddAutomaton, RemoveAutomaton, Write, Read, its body and DoMessage; part
// aw1's table clears; part aw5's lot query 0x00567C90, dock search
// 0x00568CE0, record vector copy 0x00572360 and insert 0x00573580; the
// boat's constructor 0x0056D9A0; the hash table, vector, reference and
// stream helpers, the property getter and the allocators.
#include "core/automata_awmgr.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

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

// The game's globals this part reads and writes.
namespace g {
inline constexpr std::uintptr_t kRandom = 0x00B22A78;     // the manager's generator (+0x50), set by Init
inline constexpr std::uintptr_t kPointStart = 0x00B22A80; // a float, the dock search's points' start
inline constexpr std::uintptr_t kMostBoats = 0x00B21BF0;  // uint32 of the tuning load (0x4998C90B)
} // namespace g

inline constexpr std::uint32_t kMessageWaterByte = 0x2C06BA82; // sets +0xA88; Init's subscription
inline constexpr std::uint32_t kMessageOccupantAdded = 0x99EF1142, kMessageOccupantRemoved = 0x99EF1143;
inline constexpr std::uint32_t kNotOccupantType = 0x029244DB; // DoMessage leaves these alone
inline constexpr std::uint32_t kDockType = 0xEC39DD14;        // a route by its lot
inline constexpr std::uint32_t kPairType = 0x8C392D39;        // the first pair table (+0xAA0)
// The occupant types of the second pair table (+0xAB4), in the asm's order.
inline constexpr std::uint32_t kTerminalTypes[] = {0x4C18336D, 0xAC183320, 0x6C183FFB, 0x8C184040, 0xAC18330C,
                                                   0x4C1832F5, 0xEC1832E7, 0x6C1832D1, 0xCC3A1723, 0x6C36283C};
inline constexpr std::uint32_t kKeyIid = 0xE818FCED, kObjectIid = 0x26BB767E; // Read's references

namespace off {
inline constexpr std::size_t kLeastZoom = 0xA3C; // byte: property 0x4998C91B less one (the tuning)
inline constexpr std::size_t kRefsEnd = field::kWaterRefs + 4;
} // namespace off

// A record of a route (0x3C bytes) and its ferry route in use (+4).
namespace record {
inline constexpr std::size_t kSize = 0x3C, kRoute = 0x04, kByte = 0x38;
} // namespace record

// A route node's value (+8): +0, +4 references, +8..+0x10 the record
// vector, +0x14 a reference; a route pair (0x1C bytes) is the key and that.
namespace route {
inline constexpr std::size_t kValue = 0x08, kBegin = 0x10, kEnd = 0x14, kLast = 0x1C;
} // namespace route

std::uint32_t BucketCount(char* t)
{
    return static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin));
}

std::uint32_t KeyOf(const void* key) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(key)); }

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

// Slot 0xD0 of an automaton with the game's arguments (0x49, 0, -1.0f as
// its bits, 0, 0, 0, 0).
void Stop(void* automaton)
{
    Virtual<void>(automaton, 0xD0, static_cast<std::uint32_t>(0x49), static_cast<std::uint32_t>(0),
                  static_cast<std::uint32_t>(0xBF800000), static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0),
                  static_cast<std::uint32_t>(0), static_cast<std::uint32_t>(0));
}

// Whether the occupant's slot 0x18 object (asked anew for each type)
// answers one of the second pair table's types (slot 0xC).
bool IsTerminal(void* occupant)
{
    for (const std::uint32_t type : kTerminalTypes)
        if (Virtual<std::uint8_t>(Virtual<void*>(occupant, 0x18), 0x0C, type))
            return true;
    return false;
}

// The stream's slot 18 (0x48) as Write and Read answer it: NEG AL, SBB, INC.
std::uint32_t StreamAnswer(void* stream) { return Virtual<std::uint8_t>(stream, 0x48) == 0 ? 1u : 0u; }

// ====================================================================================
// Life cycle (simulators.automata.awmgr.water.life)

// 0x00567B90, slot 3 Init: once (+0x34): the common base's Init, the message
// server's 0x2C06BA82 for the message target, 0x00B22A78 the generator
// (+0x50), slot 45 when +0x10 is set, slot 23 with 500 and slot 24; +0x34
// set. AL 1.
std::uint8_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("WaterInit", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    CallThis<std::uint8_t>(addr::cSC4AutomataManager_Init, self);
    if (void* const server = G<void*>(addr::SC4_MessageServer))
        Virtual<std::uint8_t>(server, 0x14, static_cast<void*>(self + field::kMessage), kMessageWaterByte);
    G<char*>(g::kRandom) = self + field::kRandom;
    if (At<std::uint8_t>(self, field::kByte10))
        Virtual<void>(self, 0xB4);
    Virtual<void>(self, 0x5C, static_cast<std::uint32_t>(0x1F4));
    Virtual<void>(self, 0x60);
    At<std::uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// 0x005744A0, slot 4 Shutdown: with +0x34, the message server's 0x2C06BA82
// let go, the three tables cleared (0x005743D0 on +0xA8C, 0x0056D670 on
// +0xAB4 and +0xAA0), the reference vector erased, the common base's
// Shutdown, 0x00B22A78 null. AL 1.
std::uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("WaterShutdown", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    if (void* const server = G<void*>(addr::SC4_MessageServer))
        Virtual<std::uint8_t>(server, 0x18, static_cast<void*>(self + field::kMessage), kMessageWaterByte);
    CallThis<void>(addr::cSC4WatercraftManager_ClearRoutes, self + field::kWaterTable);
    CallThis<void>(addr::cSC4WatercraftManager_ClearPairs, self + field::kWaterTable3);
    CallThis<void>(addr::cSC4WatercraftManager_ClearPairs, self + field::kWaterTable2);
    CallThis<void**>(addr::SC4RefVector_Erase, self + field::kWaterRefs, At<void**>(self, field::kWaterRefs),
                     At<void**>(self, off::kRefsEnd));
    CallThis<std::uint8_t>(addr::cSC4AutomataManager_Shutdown, self);
    G<char*>(g::kRandom) = nullptr;
    return 1;
}

// 0x00567BF0, slot 24 (the tuning): slot 23 with 0x00B21BF0; with the
// resource manager the tuning exemplar (0x6534284A, 0xA998D30B, 1; interface
// 0xA52160F5): +0xA3C 1, then property 0x4998C91B's byte (when there), less
// one; the resource released (when the get answered and gave none, not
// tested). AL 1.
std::uint8_t __fastcall Tuning(char* self, void*)
{
    static bool logged;
    FirstCall("WaterTuning", logged);
    Virtual<void>(self, 0x5C, G<std::uint32_t>(g::kMostBoats));
    void* const resources = G<void*>(addr::SC4_ResourceManager);
    std::uint32_t key[3] = {0x6534284A, 0xA998D30B, 1};
    void* resource = nullptr;
    if (!resources)
        return 1;
    if (Virtual<std::uint8_t>(resources, 0x0C, static_cast<void*>(key), static_cast<std::uint32_t>(0xA52160F5),
                              static_cast<void*>(&resource), static_cast<std::uint32_t>(0),
                              static_cast<std::uint32_t>(0))) {
        if (!resource)
            return 1;
        void* const exemplar = Virtual<void*>(resource, 0x1C);
        std::uint8_t* const zoom = &At<std::uint8_t>(self, off::kLeastZoom);
        *zoom = 1;
        CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint8, exemplar, static_cast<std::uint32_t>(0x4998C91B), zoom);
        --*zoom;
    }
    if (resource)
        Release(resource);
    return 1;
}

// 0x0056F7C0, slot 7 AddAutomaton (automaton), ret 4: without one a new boat
// (0xDC8 bytes, the constructor 0x0056D9A0; null when the allocation fails);
// then the common base's AddAutomaton. EAX its answer.
std::uint32_t __fastcall AddAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("WaterAddAutomaton", logged);
    if (!automaton) {
        void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(0xDC8));
        if (block)
            return CallThis<std::uint32_t>(addr::cSC4AutomataManager_AddAutomaton, self,
                                           CallThis<void*>(addr::cSC4Watercraft_Construct, block));
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_AddAutomaton, self, automaton);
}

// 0x0056F800, slot 8 RemoveAutomaton (automaton, b), ret 8. With one: of the
// kinds 0x4400 and 0x4402 (slot 67's +0x10, asked twice for the second) the
// first route record holding it; else, its occupant (slot 5) of a group
// 0x4416, 0x4411, 0x440F or 0x440A (slot 0x58), the first node of the second
// pair table (+0xAB4) whose +8 it is; else, of the group 0x4418, its place
// in the reference vector. The place found is nulled and its reference
// released (the vector's place erased, its last element released); then
// the common base's RemoveAutomaton. EAX its answer.
std::uint32_t __fastcall RemoveAutomaton(char* self, void*, void* automaton, std::uint32_t b)
{
    static bool logged;
    FirstCall("WaterRemoveAutomaton", logged);
    const auto base = [&] {
        return CallThis<std::uint32_t>(addr::cSC4AutomataManager_RemoveAutomaton, self, automaton, b);
    };
    if (!automaton)
        return base();
    struct {
        char* node;
        char* table;
    } it = {};
    if (At<std::uint32_t>(Virtual<char*>(automaton, 0x10C), 0x10) == 0x4400 ||
        At<std::uint32_t>(Virtual<char*>(automaton, 0x10C), 0x10) == 0x4402) {
        CallThis<void*>(addr::SC4HashTable_Begin, self + field::kWaterTable, static_cast<void*>(&it));
        char* const t = it.table;
        for (char* node = it.node; node; node = NextNode(t, node)) {
            char* const end = At<char*>(node, route::kEnd);
            for (char* r = At<char*>(node, route::kBegin); r != end; r += record::kSize)
                if (At<void*>(r, 0) == automaton) {
                    if (void* const object = At<void*>(r, 0)) {
                        At<void*>(r, 0) = nullptr;
                        Release(object);
                    }
                    return base();
                }
        }
    }
    const auto inGroup = [&](std::uint32_t group) {
        return Virtual<std::uint8_t>(Virtual<void*>(automaton, 0x14), 0x58, group) != 0;
    };
    if (inGroup(0x4416) || inGroup(0x4411) || inGroup(0x440F) || inGroup(0x440A)) {
        CallThis<void*>(addr::SC4HashTable_Begin, self + field::kWaterTable3, static_cast<void*>(&it));
        char* const t = it.table;
        for (char* node = it.node; node; node = NextNode(t, node))
            if (At<void*>(node, 8) == automaton) {
                if (void* const object = At<void*>(node, 8)) {
                    At<void*>(node, 8) = nullptr;
                    Release(object);
                }
                return base();
            }
    }
    if (!inGroup(0x4418))
        return base();
    void** const end = At<void**>(self, off::kRefsEnd);
    for (void** p = At<void**>(self, field::kWaterRefs); p != end; ++p)
        if (*p == automaton) {
            if (void* const object = *p) {
                *p = nullptr;
                Release(object);
            }
            void** const last = At<void**>(self, off::kRefsEnd);
            if (p + 1 != last)
                CallCdecl<void**>(addr::SC4PtrVector_CopyForward, p + 1, last, p, static_cast<void*>(&b),
                                  static_cast<std::uint32_t>(0));
            void** const shrunk = At<void**>(self, off::kRefsEnd) - 1;
            At<void**>(self, off::kRefsEnd) = shrunk;
            if (void* const object = *shrunk)
                Release(object);
            return base();
        }
    return base();
}

// ====================================================================================
// The save (simulators.automata.awmgr.water.serial)

// 0x0056FA70, the serializable part's (+8) slot 3 Write (stream), ret 4:
// version 6 (a word), the common base's Write; when it answers, the first
// pair table (+0xAA0) as its count and per node its two references, the
// second (+0xAB4) as its count and per node its key, the route table
// (+0xA8C) as its count and per node its key, the value's +0x1C, the record
// count and per record its ferry route (the references +4 and +8, +0xC as
// 0, 1 or -1, four words, a bool, two words, two byte vectors). EAX 1 when
// the stream's slot 18 answers 0, else 0.
std::uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("WaterWrite", logged);
    char* const self = part - field::kSerializable;
    Virtual<std::uint8_t>(stream, 0x1C, static_cast<std::uint32_t>(6));
    if (!CallThis<std::uint8_t>(addr::cSC4AutomataManager_Write, part, stream))
        return StreamAnswer(stream);
    const auto reference = [&](void* object) {
        CallCdecl<std::uint8_t>(addr::SC4Stream_WriteReference, stream, object);
    };
    struct {
        char* node;
        char* table;
    } it = {};
    char* const pairs = self + field::kWaterTable2;
    Virtual<std::uint8_t>(stream, 0x20, At<std::uint32_t>(pairs, table::kCount));
    CallThis<void*>(addr::SC4HashTable_Begin, pairs, static_cast<void*>(&it));
    if (it.node) {
        char* const t = it.table;
        for (char* node = it.node; node; node = NextNode(t, node)) {
            reference(At<void*>(node, 4));
            reference(At<void*>(node, 8));
        }
    }
    char* const pairs3 = self + field::kWaterTable3;
    Virtual<std::uint8_t>(stream, 0x20, At<std::uint32_t>(pairs3, table::kCount));
    CallThis<void*>(addr::SC4HashTable_Begin, pairs3, static_cast<void*>(&it));
    if (it.node) {
        char* const t = it.table;
        for (char* node = it.node; node; node = NextNode(t, node))
            reference(At<void*>(node, 4));
    }
    char* const routes = self + field::kWaterTable;
    Virtual<std::uint8_t>(stream, 0x20, At<std::uint32_t>(routes, table::kCount));
    // The first node: the first non-empty bucket's, found by hand.
    char* node = nullptr;
    for (std::uint32_t i = 0; i < BucketCount(routes); ++i)
        if (At<char**>(routes, table::kBegin)[i]) {
            node = At<char**>(routes, table::kBegin)[i];
            break;
        }
    for (; node; node = NextNode(routes, node)) {
        reference(At<void*>(node, 4));
        reference(At<void*>(node, route::kLast));
        const std::int32_t bytes =
            static_cast<std::int32_t>(At<char*>(node, route::kEnd) - At<char*>(node, route::kBegin));
        Virtual<std::uint8_t>(stream, 0x20,
                              static_cast<std::uint32_t>(bytes / static_cast<std::int32_t>(record::kSize)));
        for (char* r = At<char*>(node, route::kBegin); r != At<char*>(node, route::kEnd); r += record::kSize) {
            char* const f = r + record::kRoute;
            reference(At<void*>(f, 0));
            reference(At<void*>(f, 4));
            const std::uint32_t id = At<std::uint32_t>(f, 8);
            Virtual<std::uint8_t>(stream, 0x20, id == 0 ? 0u : id == 1 ? 1u : 0xFFFFFFFFu);
            for (std::size_t at = 0xC; at <= 0x12; at += 2)
                Virtual<std::uint8_t>(stream, 0x1C, static_cast<std::uint32_t>(At<std::uint16_t>(f, at)));
            CallCdecl<void*>(addr::SC4Stream_WriteBool, stream, static_cast<std::uint32_t>(At<std::uint8_t>(f, 0x14)));
            Virtual<std::uint8_t>(stream, 0x18, static_cast<std::uint32_t>(At<std::uint16_t>(f, 0x16)));
            Virtual<std::uint8_t>(stream, 0x18, static_cast<std::uint32_t>(At<std::uint16_t>(f, 0x18)));
            CallCdecl<std::uint8_t>(addr::SC4Stream_WriteByteVector, stream, static_cast<void*>(f + 0x1C));
            CallCdecl<std::uint8_t>(addr::SC4Stream_WriteByteVector, stream, static_cast<void*>(f + 0x28));
        }
    }
    return StreamAnswer(stream);
}

// 0x005749E0, the serializable part's (+8) slot 4 Read (stream), ret 4: the
// version (a word); above 6 AL 0; below 6 the common base's body
// 0x0050FEB0 on the object with it (EAX its answer); at 6 the common base's
// Read, and when it answers, the reference vector erased and the three
// tables cleared and read back (the first pair table's references
// 0xE818FCED and 0x26BB767E, the second's 0x26BB767E with a null +8, the
// routes' key 0xE818FCED, +0x1C 0x26BB767E and their records); EAX 1 when
// the stream's slot 18 answers 0, else 0.
std::uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("WaterRead", logged);
    char* const self = part - field::kSerializable;
    // The version's slot is a stack leftover in the game (a word is read
    // into a dword, and nothing when the read fails); ours starts at 0.
    std::uint32_t version = 0;
    Virtual<std::uint8_t>(stream, 0x1C, static_cast<void*>(&version));
    if (static_cast<std::uint16_t>(version) < 6)
        return CallThis<std::uint32_t>(addr::cSC4AutomataManager_ReadBody, self, stream, version);
    if (static_cast<std::uint16_t>(version) > 6)
        return version & 0xFFFFFF00u; // XOR AL,AL over the version's dword
    if (!CallThis<std::uint8_t>(addr::cSC4AutomataManager_Read, part, stream))
        return StreamAnswer(stream);
    char tag = 0;
    alignas(4) char result[12] = {};
    // One count slot for the three tables: a failed read keeps the last.
    std::int32_t count = 0;
    CallThis<void**>(addr::SC4RefVector_Erase, self + field::kWaterRefs, At<void**>(self, field::kWaterRefs),
                     At<void**>(self, off::kRefsEnd));

    // The first pair table (+0xAA0): two references a node.
    char* const pairs = self + field::kWaterTable2;
    CallThis<void>(addr::cSC4WatercraftManager_ClearPairs, pairs);
    Virtual<std::uint8_t>(stream, 0x20, static_cast<void*>(&count));
    for (std::int32_t i = 0; i < count; ++i) {
        void* read1 = nullptr;
        void* read2 = nullptr;
        CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kKeyIid, static_cast<void*>(&read1),
                                static_cast<std::uint32_t>(0));
        CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kObjectIid, static_cast<void*>(&read2),
                                static_cast<std::uint32_t>(0));
        void* const second = read2;
        if (second)
            AddRef(second);
        void* const first = read1;
        if (first)
            AddRef(first);
        void* pair[2] = {first, second};
        if (first)
            AddRef(first);
        if (second)
            AddRef(second);
        CallThis<void>(addr::SC4Uint32HashSet_Resize, pairs, At<std::uint32_t>(pairs, table::kCount) + 1);
        CallThis<void**>(addr::cSC4WatercraftManager_InsertPair, pairs, static_cast<void*>(result),
                         static_cast<const void*>(pair));
        if (second)
            Release(second);
        if (first) {
            Release(first);
            Release(first);
        }
        if (second)
            Release(second);
    }

    // The second pair table (+0xAB4): one reference a node, +8 null.
    char* const pairs3 = self + field::kWaterTable3;
    CallThis<void>(addr::cSC4WatercraftManager_ClearPairs, pairs3);
    Virtual<std::uint8_t>(stream, 0x20, static_cast<void*>(&count));
    if (count > 0) {
        void* pair[2] = {nullptr, nullptr};
        for (std::int32_t i = 0; i < count; ++i) {
            void* read = nullptr;
            CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kObjectIid, static_cast<void*>(&read),
                                    static_cast<std::uint32_t>(0));
            void* const object = read;
            if (object)
                AddRef(object);
            pair[0] = object;
            if (object)
                AddRef(object);
            CallThis<void>(addr::SC4Uint32HashSet_Resize, pairs3, At<std::uint32_t>(pairs3, table::kCount) + 1);
            CallThis<void**>(addr::cSC4WatercraftManager_InsertPair, pairs3, static_cast<void*>(result),
                             static_cast<const void*>(pair));
            if (object) {
                Release(object);
                Release(object);
            }
        }
    }

    // The route table (+0xA8C).
    char* const routes = self + field::kWaterTable;
    CallThis<void>(addr::cSC4WatercraftManager_ClearRoutes, routes);
    Virtual<std::uint8_t>(stream, 0x20, static_cast<void*>(&count));
    // The stack ferry route the records are read into: its word +8 and its
    // words and byte +0xC..+0x18 keep their values from one record to the
    // next, as the game's slots do (stack leftovers the first time, 0 in
    // ours). The record copied into the vector keeps its slots the same way.
    alignas(4) char source[0x34] = {};
    alignas(4) char copy[record::kSize] = {};
    for (std::int32_t o = 0; o < count; ++o) {
        void* key = nullptr;
        CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kKeyIid, static_cast<void*>(&key),
                                static_cast<std::uint32_t>(0));
        // The route value: two references, the record vector, a reference.
        alignas(4) char value[0x18] = {};
        void* read = nullptr;
        CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kObjectIid, static_cast<void*>(&read),
                                static_cast<std::uint32_t>(0));
        void* const last = read;
        if (last) {
            AddRef(last);
            At<void*>(value, 0x14) = last;
        }
        std::int32_t records = 0;
        // The new vector's clear, as the game inlines it on its zeros.
        char* const cleared = CallCdecl<char*>(addr::cSC4WatercraftManager_CopyRecords, static_cast<char*>(nullptr),
                                               static_cast<char*>(nullptr), static_cast<char*>(nullptr),
                                               static_cast<void*>(&tag), static_cast<std::uint32_t>(0));
        CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, static_cast<void*>(cleared),
                        static_cast<void*>(nullptr), static_cast<void*>(&tag));
        At<char*>(value, 0xC) = cleared;
        Virtual<std::uint8_t>(stream, 0x20, static_cast<void*>(&records));
        for (std::int32_t r = 0; r < records; ++r) {
            for (const std::size_t at : {0x00u, 0x04u, 0x1Cu, 0x20u, 0x24u, 0x28u, 0x2Cu, 0x30u})
                At<std::uint32_t>(source, at) = 0;
            for (const std::size_t at : {0x00u, 0x04u}) {
                void* object = nullptr;
                CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kKeyIid, static_cast<void*>(&object),
                                        static_cast<std::uint32_t>(0));
                void* const old = At<void*>(source, at);
                if (object != old) {
                    if (object)
                        AddRef(object);
                    At<void*>(source, at) = object;
                    if (old)
                        Release(old);
                }
            }
            std::int32_t id = -1;
            Virtual<std::uint8_t>(stream, 0x20, static_cast<void*>(&id));
            if (id == 0)
                At<std::uint32_t>(source, 8) = 0;
            else if (id == 1)
                At<std::uint32_t>(source, 8) = 1;
            for (std::size_t at = 0xC; at <= 0x12; at += 2)
                Virtual<std::uint8_t>(stream, 0x1C, static_cast<void*>(source + at));
            CallCdecl<std::uint8_t>(addr::SC4Stream_ReadBool, stream, static_cast<void*>(source + 0x14));
            Virtual<std::uint8_t>(stream, 0x18, static_cast<void*>(source + 0x16));
            Virtual<std::uint8_t>(stream, 0x18, static_cast<void*>(source + 0x18));
            CallCdecl<std::uint8_t>(addr::SC4Stream_ReadByteVector, stream, static_cast<void*>(source + 0x1C));
            CallCdecl<std::uint8_t>(addr::SC4Stream_ReadByteVector, stream, static_cast<void*>(source + 0x28));
            // The record: a null automaton, the ferry route assigned, +0x38 1.
            char* const f = copy + record::kRoute;
            At<std::uint32_t>(copy, 0) = 0;
            At<std::uint32_t>(f, 0) = 0;
            At<std::uint32_t>(f, 4) = 0;
            for (std::size_t at = 0x1C; at <= 0x30; at += 4)
                At<std::uint32_t>(f, at) = 0;
            CallThis<char*>(addr::cSC4WatercraftManager_AssignFerryRoute, f, static_cast<const void*>(source));
            char* const end = At<char*>(value, 0xC);
            void* const automaton = At<void*>(copy, 0);
            At<std::uint8_t>(copy, record::kByte) = 1;
            if (end != At<char*>(value, 0x10)) {
                if (end) {
                    At<void*>(end, 0) = automaton;
                    if (automaton)
                        AddRef(automaton);
                    CallThis<void*>(addr::SC4FerryRouteInUse_CopyConstruct, end + record::kRoute,
                                    static_cast<const void*>(f));
                    At<std::uint8_t>(end, record::kByte) = 1;
                }
                At<char*>(value, 0xC) = end + record::kSize;
            } else {
                CallThis<void>(addr::cSC4WatercraftManager_InsertRecordOverflow, value + 8, end,
                               static_cast<const void*>(copy), static_cast<void*>(&tag), static_cast<std::uint32_t>(1),
                               static_cast<std::uint32_t>(1));
            }
            for (const std::size_t at : {0x28u, 0x1Cu})
                if (void* const buffer = At<void*>(f, at))
                    Free(buffer);
            for (const std::size_t at : {0x04u, 0x00u})
                if (void* const object = At<void*>(f, at))
                    Release(object);
            if (automaton)
                Release(automaton);
            for (const std::size_t at : {0x28u, 0x1Cu})
                if (void* const buffer = At<void*>(source, at))
                    Free(buffer);
            for (const std::size_t at : {0x04u, 0x00u})
                if (void* const object = At<void*>(source, at))
                    Release(object);
        }
        // The pair {key, a copy of the value} (the records copied into new
        // storage), inserted with 0x00574260 after the resize.
        void* const keyCopy = key;
        if (keyCopy)
            AddRef(keyCopy);
        alignas(4) char pair[0x1C] = {};
        At<void*>(pair, 0) = keyCopy;
        if (keyCopy)
            AddRef(keyCopy);
        for (const std::size_t at : {0x00u, 0x04u}) {
            void* const object = At<void*>(value, at);
            At<void*>(pair, 4 + at) = object;
            if (object)
                AddRef(object);
        }
        char* const begin = At<char*>(value, 8);
        char* const vend = At<char*>(value, 0xC);
        const std::int32_t n = static_cast<std::int32_t>(vend - begin) / static_cast<std::int32_t>(record::kSize);
        char* const storage =
            n != 0 ? static_cast<char*>(Allocate(static_cast<std::uint32_t>(n) * record::kSize)) : nullptr;
        At<char*>(pair, 0xC) = storage;
        At<char*>(pair, 0x14) = storage + static_cast<std::uint32_t>(n) * record::kSize;
        At<char*>(pair, 0x10) = CallCdecl<char*>(addr::cSC4WatercraftManager_ConstructRecords, begin, vend, storage,
                                                 static_cast<void*>(&tag));
        void* const valueLast = At<void*>(value, 0x14);
        At<void*>(pair, 0x18) = valueLast;
        if (valueLast)
            AddRef(valueLast);
        CallThis<void>(addr::SC4Uint32HashSet_Resize, routes, At<std::uint32_t>(routes, table::kCount) + 1);
        CallThis<void**>(addr::cSC4WatercraftManager_InsertRouteNode, routes, static_cast<void*>(result),
                         static_cast<const void*>(pair));
        if (void* const object = At<void*>(pair, 0x18))
            Release(object);
        char* const pairBegin = At<char*>(pair, 0xC);
        CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, static_cast<void*>(pairBegin),
                        At<void*>(pair, 0x10), static_cast<void*>(&tag));
        if (pairBegin)
            Free(pairBegin);
        for (const std::size_t at : {0x08u, 0x04u, 0x00u})
            if (void* const object = At<void*>(pair, at))
                Release(object);
        if (keyCopy)
            Release(keyCopy);
        if (valueLast)
            Release(valueLast);
        char* const valueBegin = At<char*>(value, 8);
        CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, static_cast<void*>(valueBegin),
                        At<void*>(value, 0xC), static_cast<void*>(&tag));
        if (valueBegin)
            Free(valueBegin);
        for (const std::size_t at : {0x04u, 0x00u})
            if (void* const object = At<void*>(value, at))
                Release(object);
    }
    return StreamAnswer(stream);
}

// 0x0056C850, a pair table's insert_unique (result, pair {key, object}), ret
// 8: a 0xC-byte node (both referenced) at the head of the key's bucket,
// result {node, table, 1}; {node, table, 0} for a key already there. EAX the
// result.
void** __fastcall InsertPair(char* t, void*, void** result, void* const* pair)
{
    static bool logged;
    FirstCall("WaterInsertPair", logged);
    void* const key = pair[0];
    const std::uint32_t bucket = KeyOf(key) % BucketCount(t);
    char* const head = At<char**>(t, table::kBegin)[bucket];
    for (char* n = head; n; n = At<char*>(n, 0))
        if (At<void*>(n, 4) == key) {
            result[1] = t;
            result[0] = n;
            At<std::uint8_t>(result, 8) = 0;
            return result;
        }
    char* const node = static_cast<char*>(Allocate(0xC));
    At<std::uint32_t>(node, 0) = 0;
    if (node + 4) {
        for (const std::size_t i : {0u, 1u}) {
            void* const object = pair[i];
            At<void*>(node, 4 + 4 * i) = object;
            if (object)
                AddRef(object);
        }
    }
    At<char*>(node, 0) = head;
    At<char**>(t, table::kBegin)[bucket] = node;
    ++At<std::uint32_t>(t, table::kCount);
    result[0] = node;
    result[1] = t;
    At<std::uint8_t>(result, 8) = 1;
    return result;
}

// 0x00574260, the route table's insert_unique without a resize (result, pair
// {key, value}), ret 8: a 0x20-byte node at the head of the key's bucket
// (the key and the value's +0, +4 and +0x14 referenced, its record vector
// copied by 0x00572360), result {node, table, 1}; {node, table, 0} for a key
// already there. EAX the result.
void** __fastcall InsertRouteNode(char* t, void*, void** result, char* pair)
{
    static bool logged;
    FirstCall("WaterInsertRouteNode", logged);
    void* const key = At<void*>(pair, 0);
    const std::uint32_t bucket = KeyOf(key) % BucketCount(t);
    char* const head = At<char**>(t, table::kBegin)[bucket];
    for (char* n = head; n; n = At<char*>(n, 0))
        if (At<void*>(n, 4) == key) {
            result[1] = t;
            result[0] = n;
            At<std::uint8_t>(result, 8) = 0;
            return result;
        }
    char* const node = static_cast<char*>(Allocate(0x20));
    At<std::uint32_t>(node, 0) = 0;
    if (char* const to = node + 4) {
        for (const std::size_t at : {0x00u, 0x04u, 0x08u}) {
            void* const object = At<void*>(pair, at);
            At<void*>(to, at) = object;
            if (object)
                AddRef(object);
        }
        CallThis<void*>(addr::cSC4WatercraftManager_CopyRecordVector, to + 0xC, static_cast<const void*>(pair + 0xC));
        void* const object = At<void*>(pair, 0x18);
        At<void*>(to, 0x18) = object;
        if (object)
            AddRef(object);
    }
    At<char*>(node, 0) = head;
    At<char**>(t, table::kBegin)[bucket] = node;
    ++At<std::uint32_t>(t, table::kCount);
    result[1] = t;
    result[0] = node;
    At<std::uint8_t>(result, 8) = 1;
    return result;
}

// 0x00570D10 (first, last, dest, tag), cdecl: the records [first, last)
// copied into new storage at dest (the automaton referenced, the ferry route
// by its copy constructor 0x0056D770, the byte +0x38); nothing written at a
// null dest. The end of the copy.
char* __cdecl ConstructRecords(char* first, char* last, char* dest, void*)
{
    static bool logged;
    FirstCall("WaterConstructRecords", logged);
    for (; first != last; first += record::kSize, dest += record::kSize) {
        if (!dest)
            continue;
        void* const automaton = At<void*>(first, 0);
        At<void*>(dest, 0) = automaton;
        if (automaton)
            AddRef(automaton);
        CallThis<void*>(addr::SC4FerryRouteInUse_CopyConstruct, dest + record::kRoute,
                        static_cast<const void*>(first + record::kRoute));
        At<std::uint8_t>(dest, record::kByte) = At<std::uint8_t>(first, record::kByte);
    }
    return dest;
}

// 0x00570D60 (first, last, dest, tag, zero), cdecl: std::copy of the records
// [first, last) onto dest (the automaton's reference assigned unless the
// record is its own, the ferry route by 0x0056D7F0, the byte +0x38); the end
// of the copy (dest for none).
char* __cdecl CopyRecords(char* first, char* last, char* dest, void*, std::uint32_t)
{
    static bool logged;
    FirstCall("WaterCopyRecords", logged);
    std::int32_t count = static_cast<std::int32_t>(last - first) / static_cast<std::int32_t>(record::kSize);
    if (count <= 0)
        return dest;
    do {
        if (first != dest) {
            void* const fresh = At<void*>(first, 0);
            void* const old = At<void*>(dest, 0);
            if (fresh != old) {
                if (fresh)
                    AddRef(fresh);
                At<void*>(dest, 0) = fresh;
                if (old)
                    Release(old);
            }
        }
        CallThis<char*>(addr::cSC4WatercraftManager_AssignFerryRoute, dest + record::kRoute,
                        static_cast<const void*>(first + record::kRoute));
        At<std::uint8_t>(dest, record::kByte) = At<std::uint8_t>(first, record::kByte);
        first += record::kSize;
        dest += record::kSize;
    } while (--count != 0);
    return dest;
}

// 0x0056D7F0, a ferry route in use's assignment (other), ret 4: the
// references +0 and +4 assigned (skipped for itself or the same one), the
// word +8, the words +0xC..+0x12, the byte +0x14, the words +0x16 and +0x18,
// the byte vectors +0x1C and +0x28 (SC4ByteVector::Assign). Answers this.
char* __fastcall AssignFerryRoute(char* self, void*, char* other)
{
    static bool logged;
    FirstCall("WaterAssignFerryRoute", logged);
    for (const std::size_t at : {0x00u, 0x04u}) {
        if (other + at == self + at)
            continue;
        void* const old = At<void*>(self, at);
        void* const fresh = At<void*>(other, at);
        if (fresh == old)
            continue;
        if (fresh)
            AddRef(fresh);
        At<void*>(self, at) = fresh;
        if (old)
            Release(old);
    }
    At<std::uint32_t>(self, 8) = At<std::uint32_t>(other, 8);
    for (std::size_t at = 0xC; at <= 0x12; at += 2)
        At<std::uint16_t>(self, at) = At<std::uint16_t>(other, at);
    At<std::uint8_t>(self, 0x14) = At<std::uint8_t>(other, 0x14);
    At<std::uint16_t>(self, 0x16) = At<std::uint16_t>(other, 0x16);
    At<std::uint16_t>(self, 0x18) = At<std::uint16_t>(other, 0x18);
    CallThis<void*>(addr::SC4ByteVector_Assign, self + 0x1C, static_cast<const void*>(other + 0x1C));
    CallThis<void*>(addr::SC4ByteVector_Assign, self + 0x28, static_cast<const void*>(other + 0x28));
    return self;
}

// 0x0056F740 (first, last, tag), cdecl: the records [first, last) destroyed:
// each one's byte vector buffers +0x2C and +0x20 freed, its references +8,
// +4 and +0 released.
void __cdecl DestroyRecords(char* first, char* last, void*)
{
    static bool logged;
    FirstCall("WaterDestroyRecords", logged);
    for (char* r = first; r != last; r += record::kSize) {
        for (const std::size_t at : {0x2Cu, 0x20u})
            if (void* const buffer = At<void*>(r, at))
                Free(buffer);
        for (const std::size_t at : {0x08u, 0x04u, 0x00u})
            if (void* const object = At<void*>(r, at))
                Release(object);
    }
}

// ====================================================================================
// Messages (simulators.automata.awmgr.water.messages)

// 0x00575500, the message target's (+0xC) slot 3 DoMessage (message), ret 4:
// 0x2C06BA82 sets +0xA88; an occupant added (0x99EF1142, not null) or
// removed (0x99EF1143, not tested) not of the type 0x029244DB (its slot 6's
// slot 3) goes to 0x00575170 or 0x00574520; then the common base's
// DoMessage. EAX 1 for those three types or when the base's answers, else 0.
std::uint32_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged;
    FirstCall("WaterDoMessage", logged);
    char* const self = part - field::kMessage;
    bool taken = false;
    const std::uint32_t type = Virtual<std::uint32_t>(message, 0x10);
    if (type == kMessageWaterByte) {
        At<std::uint8_t>(self, field::kWaterByte) = 1;
        taken = true;
    } else if (type == kMessageOccupantAdded) {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (occupant && !Virtual<std::uint8_t>(Virtual<void*>(occupant, 0x18), 0x0C, kNotOccupantType))
            CallThis<void>(addr::cSC4WatercraftManager_OccupantAdded, self, occupant);
        taken = true;
    } else if (type == kMessageOccupantRemoved) {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!Virtual<std::uint8_t>(Virtual<void*>(occupant, 0x18), 0x0C, kNotOccupantType))
            CallThis<void>(addr::cSC4WatercraftManager_OccupantRemoved, self, occupant);
        taken = true;
    }
    if (CallThis<std::uint8_t>(addr::cSC4AutomataManager_DoMessage, part, message) || taken)
        return 1;
    return 0;
}

// 0x00574520, an occupant removed (occupant), ret 4. Without one nothing.
// The first route whose value's +0x1C it is (walked with the iterator's
// 0x0053DCF0): its records' automata stopped (slot 0xD0), the records
// cleared and the route erased (0x00574340), and nothing else. Else: of the
// second pair table's types, its node there (looked up while referenced)
// has its +8 stopped and is erased (0x0056D6E0); then of the type
// 0x8C392D39 the first node of the first pair table whose +8 it is erased.
void __fastcall OccupantRemoved(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("WaterOccupantRemoved", logged);
    if (!occupant)
        return;
    struct {
        char* node;
        char* table;
    } it = {};
    char* const routes = self + field::kWaterTable;
    CallThis<void*>(addr::SC4HashTable_Begin, routes, static_cast<void*>(&it));
    for (char* node = it.node; node;) {
        if (At<void*>(node, route::kLast) == occupant) {
            for (char* r = At<char*>(node, route::kBegin); r != At<char*>(node, route::kEnd); r += record::kSize)
                if (void* const automaton = At<void*>(r, 0))
                    Stop(automaton);
            char tag = 0, tag2 = 0;
            char* const end = At<char*>(node, route::kEnd);
            char* const cleared = CallCdecl<char*>(addr::cSC4WatercraftManager_CopyRecords, end, end,
                                                   At<char*>(node, route::kBegin), static_cast<void*>(&tag),
                                                   static_cast<std::uint32_t>(0));
            CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, static_cast<void*>(cleared),
                            At<void*>(node, route::kEnd), static_cast<void*>(&tag2));
            At<char*>(node, route::kEnd) = cleared;
            char* iterator[2] = {node, it.table};
            CallThis<void>(addr::cSC4WatercraftManager_EraseRoute, routes, static_cast<void*>(iterator));
            return;
        }
        // The iterator still holds this node when the next bucket is sought.
        node = At<char*>(node, 0);
        if (!node)
            node = CallThis<char*>(addr::SC4HashTable_NextBucket, static_cast<void*>(&it));
        it.node = node;
    }
    if (IsTerminal(occupant)) {
        AddRef(occupant);
        char* const pairs3 = self + field::kWaterTable3;
        char* node = At<char**>(pairs3, table::kBegin)[KeyOf(occupant) % BucketCount(pairs3)];
        while (node && At<void*>(node, 4) != occupant)
            node = At<char*>(node, 0);
        Release(occupant);
        if (node) {
            if (void* const object = At<void*>(node, 8))
                Stop(object);
            char* iterator[2] = {node, pairs3};
            CallThis<void>(addr::cSC4WatercraftManager_ErasePair, pairs3, static_cast<void*>(iterator));
        }
    }
    if (!Virtual<std::uint8_t>(Virtual<void*>(occupant, 0x18), 0x0C, kPairType))
        return;
    char* const pairs = self + field::kWaterTable2;
    CallThis<void*>(addr::SC4HashTable_Begin, pairs, static_cast<void*>(&it));
    if (!it.node)
        return;
    char* const t = it.table;
    for (char* node = it.node; node; node = NextNode(t, node))
        if (At<void*>(node, 8) == occupant) {
            char* iterator[2] = {node, t};
            CallThis<void>(addr::cSC4WatercraftManager_ErasePair, pairs, static_cast<void*>(iterator));
            return;
        }
}

// 0x00575170, an occupant added (occupant), ret 4. Without one nothing. Of
// the type 0xEC39DD14, with no route yet for its lot (0x00567C90, the key):
// when the dock search 0x00568CE0 answers, a route {lot, {null, null, no
// records, the occupant}} inserted (0x00574830). Of the second pair table's
// types and not there yet: {occupant, null} inserted there. Of the type
// 0x8C392D39: the dock search (its answer not read), then {lot, occupant}
// inserted in the first pair table.
void __fastcall OccupantAdded(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("WaterOccupantAdded", logged);
    if (!occupant)
        return;
    alignas(4) char result[12] = {};
    if (Virtual<std::uint8_t>(Virtual<void*>(occupant, 0x18), 0x0C, kDockType)) {
        void* const lot = CallThis<void*>(addr::cSC4WatercraftManager_LotAtOccupant, self, occupant);
        void* key = lot;
        if (lot)
            AddRef(lot);
        struct {
            char* node;
            char* table;
        } found = {};
        char* const routes = self + field::kWaterTable;
        CallThis<void>(addr::SC4Uint32HashTable_Find, routes, static_cast<void*>(&found),
                       static_cast<const void*>(&key));
        if (lot)
            Release(lot);
        if (!found.node) {
            const std::uint32_t start = G<std::uint32_t>(g::kPointStart);
            std::uint32_t a[3] = {start, start, start};
            std::uint32_t b[3] = {start, start, start};
            if (CallThis<std::uint8_t>(addr::cSC4WatercraftManager_DockPoints, self, occupant, static_cast<void*>(a),
                                       static_cast<void*>(b), static_cast<std::uint32_t>(0))) {
                alignas(4) char value[0x18] = {};
                CallThis<void>(addr::cSC4WatercraftManager_ClearRecords, value + 8);
                CallThis<void>(addr::SC4RefPtr_Assign, value + 0x14, occupant);
                key = lot;
                if (lot)
                    AddRef(lot);
                alignas(4) char pair[0x1C];
                char* const made = CallThis<char*>(addr::cSC4WatercraftManager_MakeRoutePair, pair,
                                                   static_cast<const void*>(&key), static_cast<const void*>(value));
                CallThis<void**>(addr::cSC4WatercraftManager_InsertRoute, routes, static_cast<void*>(result),
                                 static_cast<const void*>(made));
                CallThis<void>(addr::cSC4WatercraftManager_DestroyRoute, pair + 4);
                if (void* const object = At<void*>(pair, 0))
                    Release(object);
                if (lot)
                    Release(lot);
                CallThis<void>(addr::cSC4WatercraftManager_DestroyRoute, value);
            }
        }
    }
    if (IsTerminal(occupant)) {
        AddRef(occupant);
        char* const pairs3 = self + field::kWaterTable3;
        char* node = At<char**>(pairs3, table::kBegin)[KeyOf(occupant) % BucketCount(pairs3)];
        while (node && At<void*>(node, 4) != occupant)
            node = At<char*>(node, 0);
        Release(occupant);
        if (!node) {
            AddRef(occupant);
            AddRef(occupant);
            void* pair[2] = {occupant, nullptr};
            CallThis<void>(addr::SC4Uint32HashSet_Resize, pairs3, At<std::uint32_t>(pairs3, table::kCount) + 1);
            CallThis<void**>(addr::cSC4WatercraftManager_InsertPair, pairs3, static_cast<void*>(result),
                             static_cast<const void*>(pair));
            Release(occupant);
            Release(occupant);
        }
    }
    if (!Virtual<std::uint8_t>(Virtual<void*>(occupant, 0x18), 0x0C, kPairType))
        return;
    const std::uint32_t start = G<std::uint32_t>(g::kPointStart);
    std::uint32_t a[3] = {start, start, start};
    std::uint32_t b[3] = {start, start, start};
    CallThis<std::uint8_t>(addr::cSC4WatercraftManager_DockPoints, self, occupant, static_cast<void*>(a),
                           static_cast<void*>(b), static_cast<std::uint32_t>(0));
    AddRef(occupant);
    void* const lot = CallThis<void*>(addr::cSC4WatercraftManager_LotAtOccupant, self, occupant);
    if (lot)
        AddRef(lot);
    if (lot)
        AddRef(lot);
    void* pair[2] = {lot, occupant};
    AddRef(occupant);
    char* const pairs = self + field::kWaterTable2;
    CallThis<void>(addr::SC4Uint32HashSet_Resize, pairs, At<std::uint32_t>(pairs, table::kCount) + 1);
    CallThis<void**>(addr::cSC4WatercraftManager_InsertPair, pairs, static_cast<void*>(result),
                     static_cast<const void*>(pair));
    Release(occupant);
    if (lot) {
        Release(lot);
        Release(lot);
    }
    Release(occupant);
}

// 0x00574340, the route table's erase (iterator), ret 4: the node unlinked
// from its key's bucket; the bucket's first freed here (its value destroyed
// by 0x005732F0, its key released), another by 0x00574230; the count down.
// Nothing when it is not there.
void __fastcall EraseRoute(char* t, void*, char** it)
{
    static bool logged;
    FirstCall("WaterEraseRoute", logged);
    char* const node = it[0];
    if (!node)
        return;
    char** const slot = &At<char**>(t, table::kBegin)[At<std::uint32_t>(node, 4) % BucketCount(t)];
    char* first = *slot;
    if (first == node) {
        *slot = At<char*>(first, 0);
        CallThis<void>(addr::cSC4WatercraftManager_DestroyRoute, first + route::kValue);
        if (void* const key = At<void*>(first, 4))
            Release(key);
        Free(first);
        --At<std::uint32_t>(t, table::kCount);
        return;
    }
    for (char* n = At<char*>(first, 0); n; n = At<char*>(n, 0)) {
        if (n == node) {
            At<char*>(first, 0) = At<char*>(n, 0);
            CallThis<void>(addr::cSC4WatercraftManager_FreeRouteNode, t, n);
            --At<std::uint32_t>(t, table::kCount);
            return;
        }
        first = n;
    }
}

// 0x00574230, a route node's release (node), ret 4 (this not read): its
// value destroyed (0x005732F0), its key released, the node freed.
void __fastcall FreeRouteNode(void*, void*, char* node)
{
    static bool logged;
    FirstCall("WaterFreeRouteNode", logged);
    CallThis<void>(addr::cSC4WatercraftManager_DestroyRoute, node + route::kValue);
    if (void* const key = At<void*>(node, 4))
        Release(key);
    if (node)
        Free(node);
}

// 0x005732F0, a route value's destructor: the reference +0x14 released, the
// records destroyed (0x0056F740) and their storage freed, the references +4
// and +0 released.
void __fastcall DestroyRoute(char* self, void*)
{
    static bool logged;
    FirstCall("WaterDestroyRoute", logged);
    if (void* const object = At<void*>(self, 0x14))
        Release(object);
    char tag = 0;
    CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, At<void*>(self, 8), At<void*>(self, 0xC),
                    static_cast<void*>(&tag));
    if (void* const buffer = At<void*>(self, 8))
        Free(buffer);
    if (void* const object = At<void*>(self, 4))
        Release(object);
    if (void* const object = At<void*>(self, 0))
        Release(object);
}

// 0x005734E0, a record vector's clear: copied onto its own end
// (0x00570D60), the records from there destroyed (0x0056F740), the end set.
void __fastcall ClearRecords(char* self, void*)
{
    static bool logged;
    FirstCall("WaterClearRecords", logged);
    char tag = 0, tag2 = 0;
    char* const end = At<char*>(self, 4);
    char* const cleared = CallCdecl<char*>(addr::cSC4WatercraftManager_CopyRecords, end, end, At<char*>(self, 0),
                                           static_cast<void*>(&tag), static_cast<std::uint32_t>(0));
    CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, static_cast<void*>(cleared), At<void*>(self, 4),
                    static_cast<void*>(&tag2));
    At<char*>(self, 4) = cleared;
}

// 0x00573520, a route pair's constructor (key*, value*), ret 8: the key and
// the value's +0, +4 and +0x14 referenced, its record vector copied
// (0x00572360). Answers this.
char* __fastcall MakeRoutePair(char* self, void*, void* const* key, char* value)
{
    static bool logged;
    FirstCall("WaterMakeRoutePair", logged);
    void* const k = *key;
    At<void*>(self, 0) = k;
    if (k)
        AddRef(k);
    for (const std::size_t at : {0x00u, 0x04u}) {
        void* const object = At<void*>(value, at);
        At<void*>(self, 4 + at) = object;
        if (object)
            AddRef(object);
    }
    CallThis<void*>(addr::cSC4WatercraftManager_CopyRecordVector, self + 0xC, static_cast<const void*>(value + 8));
    void* const object = At<void*>(value, 0x14);
    At<void*>(self, 0x18) = object;
    if (object)
        AddRef(object);
    return self;
}

// 0x00574830, the route table's insert_unique (result, pair), ret 8: the
// buckets grown for one more (SC4Uint32HashSet::Resize), then 0x00574260.
// EAX the result.
void** __fastcall InsertRoute(char* t, void*, void** result, void* pair)
{
    static bool logged;
    FirstCall("WaterInsertRoute", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Resize, t, At<std::uint32_t>(t, table::kCount) + 1);
    CallThis<void**>(addr::cSC4WatercraftManager_InsertRouteNode, t, static_cast<void*>(result), pair);
    return result;
}

// 0x0056D6E0, a pair table's erase (iterator), ret 4: the node unlinked from
// its key's bucket; the bucket's first freed here (+8 and +4 released),
// another by 0x0056C910; the count down. Nothing when it is not there.
void __fastcall ErasePair(char* t, void*, char** it)
{
    static bool logged;
    FirstCall("WaterErasePair", logged);
    char* const node = it[0];
    if (!node)
        return;
    char** const slot = &At<char**>(t, table::kBegin)[At<std::uint32_t>(node, 4) % BucketCount(t)];
    char* first = *slot;
    if (first == node) {
        *slot = At<char*>(first, 0);
        if (void* const object = At<void*>(first, 8))
            Release(object);
        if (void* const key = At<void*>(first, 4))
            Release(key);
        Free(first);
        --At<std::uint32_t>(t, table::kCount);
        return;
    }
    for (char* n = At<char*>(first, 0); n; n = At<char*>(n, 0)) {
        if (n == node) {
            At<char*>(first, 0) = At<char*>(n, 0);
            CallThis<void>(addr::cSC4WatercraftManager_FreePairNode, t, n);
            --At<std::uint32_t>(t, table::kCount);
            return;
        }
        first = n;
    }
}

// 0x0056C910, a pair node's release (node), ret 4 (this not read): +8 and +4
// released, the node freed.
void __fastcall FreePairNode(void*, void*, char* node)
{
    static bool logged;
    FirstCall("WaterFreePairNode", logged);
    if (void* const object = At<void*>(node, 8))
        Release(object);
    if (void* const key = At<void*>(node, 4))
        Release(key);
    if (node)
        Free(node);
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

const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_Init, Init),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_HandleTuningParametersChanged, Tuning),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_AddAutomaton, AddAutomaton),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_RemoveAutomaton, RemoveAutomaton),
};

const Detour kSerial[] = {
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_Write, Write),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_Read, Read),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_InsertPair, InsertPair),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_InsertRouteNode, InsertRouteNode),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_ConstructRecords, ConstructRecords),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_CopyRecords, CopyRecords),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_AssignFerryRoute, AssignFerryRoute),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_DestroyRecords, DestroyRecords),
};

const Detour kMessages[] = {
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_DoMessage, DoMessage),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_OccupantRemoved, OccupantRemoved),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_OccupantAdded, OccupantAdded),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_EraseRoute, EraseRoute),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_FreeRouteNode, FreeRouteNode),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_DestroyRoute, DestroyRoute),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_ClearRecords, ClearRecords),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_MakeRoutePair, MakeRoutePair),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_InsertRoute, InsertRoute),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_ErasePair, ErasePair),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_FreePairNode, FreePairNode),
};

#undef OPENSC4_DETOUR

bool InstallLife(registry::Mode) { return InstallList(kLife, sizeof kLife / sizeof kLife[0], "water.life"); }
bool InstallSerial(registry::Mode)
{
    return InstallList(kSerial, sizeof kSerial / sizeof kSerial[0], "water.serial");
}
bool InstallMessages(registry::Mode)
{
    return InstallList(kMessages, sizeof kMessages / sizeof kMessages[0], "water.messages");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.awmgr.water.life", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4WatercraftManager_Init,
                        "The watercraft manager's life cycle: Init, Shutdown, the tuning (slot 24), AddAutomaton and "
                        "RemoveAutomaton (5 detours; part aw4, src/core/automata_awmgr_water.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.automata.awmgr.water.serial", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4WatercraftManager_Write,
                        "The watercraft manager's save: Write, Read, the pair and route inserts, the record copies, "
                        "the ferry route assignment and the records' destroy (8 detours; part aw4, "
                        "src/core/automata_awmgr_water.cpp)",
                        false, InstallSerial}),
    registry::Register({"simulators.automata.awmgr.water.messages", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4WatercraftManager_DoMessage,
                        "The watercraft manager's messages: DoMessage, an occupant's addition and removal, the route "
                        "table's erase, node release and insert, the route value's destructor, the records' clear, "
                        "the route pair's constructor and the pair tables' erase and node release (11 detours; part "
                        "aw4, src/core/automata_awmgr_water.cpp)",
                        false, InstallMessages}),
};

} // namespace
} // namespace opensc4::automata_awmgr
