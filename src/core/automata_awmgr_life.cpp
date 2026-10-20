// Our common automata manager base, part aw2 (build 641): its life cycle,
// tuning, messages and save. The class map is in src/core/automata_awmgr.h
// and the survey in docs/automata.md, "Part aw" and "Part aw2". The common
// base is also the vehicle and pedestrian managers' base: part v2 calls
// Init, Shutdown, DoMessage, its +4 slot 3, Write, Read and the tuning load
// at their game addresses, and so reaches these detours when they are on.
// Calls between functions of the part go to the game's addresses, so each
// detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses, hex,
// eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.awmgr.life (13): Init, Shutdown, Activate (slot 5),
//     SetMaxCount (23), ReleaseAutomatonSet (33), TrackAutomataGroupInstances
//     (31) with the group instance table's erase, node release and insert,
//     slot 39 (the cheats' registration and the tuning load, once), slot 66
//     (the exemplar data) with the record copy and its range.
//   simulators.automata.awmgr.tuning (5): the tuning load 0x00512CF0 with the
//     two curve tables' fillers and inserts.
//   simulators.automata.awmgr.messages (4): DoMessage, the cheat target (the
//     +4 part's slot 3) and the route line object it makes.
//   simulators.automata.awmgr.serial (3): Write, Read and its body.
//
// Oddities kept:
//   - Read appends the automata it reads to the list without counting them
//     (the count comes from the stream) and frees the old nodes without
//     releasing their automata; it reads the version, the references and the
//     table's count into the stack slot of its stream argument, so a failed
//     count read leaves the last reference (or the stream's address) there,
//     as ours does. A failed read of a table record keeps the slots' earlier
//     values (the key's starts as the reference counter; the value's is a
//     stack leftover in the game, 0 in ours).
//   - Write walks the source building table bucket by bucket, finding the
//     next bucket from the node's key, as the STLport iterator does.
//   - TrackAutomataGroupInstances answers 1 for a group the exemplar map does
//     not hold and when the group is tracked already; the untrack branch
//     releases the set through slot 33 (which clears it) before the erase
//     clears it again.
//   - ReleaseAutomatonSet releases the set's automata and clears it, but
//     leaves its buckets.
//   - Shutdown releases the exemplar records' objects (+0x30) but keeps the
//     records; slot 66 copies the vector onto itself before it destroys it,
//     so the record copy never runs.
//   - The cheat target makes its route line without a null test of the
//     allocation and writes its count into it even when there is none.
//   - DoMessage answers 1 for every message, AL only.
//   - The tuning load releases the tuning exemplar's resource halfway and
//     reads the second half of the properties from the exemplar after; it
//     never releases the exemplar itself. Its two first masks become all
//     ones at a zero byte, the third never does.
//
// Floats: 53-bit doubles, as the game's x87 runs. The tuning load's three
// computed floats (0x00B21CF8, 0x00B21C8C, 0x00B21D40) take an unsigned
// word as the game's FILD with 2^32 added for a negative one, then one
// product or quotient rounded to a float; SetMaxCount's share is the C
// runtime's ftol (truncation) of the count times a double; DoMessage's
// camera test compares the unrounded squared distance (ST0) with the zoom's
// float the asm's way (not above, or unordered, keeps the old point).
//
// Still the game's, at their addresses: the SC4UIScale probe sites
// (CreateAutomaton 0x00505370 and the aircraft manager's 0x004FBFE0); the
// exemplar enumeration callback 0x00513A60; the hash set and hash map
// helpers, the curve and string helpers, the property getters, the stream
// helpers, the tokenizer, the render state and the allocators.
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
void Release(void* object) { Virtual<std::uint32_t>(object, 0x08); }

// The game's globals this part reads and writes (the tuning statics).
namespace g {
inline constexpr std::uintptr_t kCheatsOnce = 0x00B21B88;    // uint32: slot 39's counter
inline constexpr std::uintptr_t kRouteLine = 0x00B21BA0;     // the cheat's route line object
inline constexpr std::uintptr_t kCheatTexts = 0x00B09940;    // five const char* (the cheats' names)
inline constexpr std::uintptr_t kParkCheatOff = 0x00B09968;  // byte
inline constexpr std::uintptr_t kZoomLimits = 0x00A95518;    // float per zoom (DoMessage's camera test)
inline constexpr std::uintptr_t kShareFactor = 0x00A955A0;   // double (SetMaxCount)
inline constexpr std::uintptr_t kFloatA95300 = 0x00A95300, kFloatA952E8 = 0x00A952E8;
inline constexpr std::uintptr_t kKindCurves = 0x00B21D44;    // hash table (kind -> curve), 0x18-byte nodes
inline constexpr std::uintptr_t kTuningMap = 0x00B21D60;     // hash table (key -> four floats, curve), 0x28-byte nodes
inline constexpr std::uintptr_t kVectorCA8 = 0x00B21CA8;     // a vector of words: begin, end, capacity
} // namespace g

inline constexpr std::uint32_t kAutomatonIid = 0xA9B40F05;
inline constexpr std::uint32_t kMessageTypes[] = {0xA6B79602, 0xA6B79612, 0x06CDB65A, 0x26D31EC1, 0x26D31EC2,
                                                  0x99EF1142, 0x99EF1143, 0x2977AE2F, 0x4A7FB807, 0xEA298D50,
                                                  0xC9F775BB, 0xCAD61E00, 0xEAD61E16, 0xEBFF18A3};

namespace off {
inline constexpr std::size_t kLoaded = 0x10;      // set by Read
inline constexpr std::size_t kCameraPoint = 0xA30; // three floats (DoMessage 0xA6B79612)
inline constexpr std::size_t kCameraHold = 0xA4C; // byte (0xCAD61E00 sets, 0xEAD61E16 clears)
inline constexpr std::size_t kOldSave = 0xA78;    // byte: Read of a version before 6
} // namespace off

// An STLport hash table's bucket of a key.
std::uint32_t Bucket(char* t, std::uint32_t key)
{
    const std::uint32_t buckets =
        static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin));
    return key % buckets;
}

// ====================================================================================
// Life cycle (simulators.automata.awmgr.life)

// 0x005066B0, slot 3 Init: once (+0x34): slot 39, the message server for the
// fourteen types, the frame service (0x86AD10EC, 0xFF), the cheat codes (+4
// part), the cells object (city slot 0xA0), the script system (0xC0) and the
// automata controller manager (0xBC) kept, slots 65 and 66. AL 1.
std::uint8_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("Init", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    At<std::uint8_t>(self, field::kInitialized) = 1;
    Virtual<std::uint8_t>(self, 0x9C);
    if (void* const server = G<void*>(addr::SC4_MessageServer))
        for (const std::uint32_t type : kMessageTypes)
            Virtual<std::uint8_t>(server, 0x14, static_cast<void*>(self + field::kMessage), type);
    if (void* const frames = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<std::uint8_t>(frames, 0x14, static_cast<void*>(self + field::kMessage), static_cast<std::uint32_t>(0xFF));
    if (void* const cheats = G<void*>(addr::SC4_CheatCodes))
        Virtual<std::uint8_t>(cheats, 0x14, static_cast<void*>(self + field::kVtable4), static_cast<std::uint32_t>(0));
    void* const city = G<void*>(addr::SC4_City);
    if (!At<void*>(self, field::kCells)) {
        void* const cells = Virtual<void*>(city, 0xA0);
        At<void*>(self, field::kCells) = cells;
        Virtual<std::uint32_t>(cells, 0x04);
    }
    if (!At<void*>(self, field::kScripts)) {
        void* const scripts = Virtual<void*>(G<void*>(addr::SC4_City), 0xC0);
        At<void*>(self, field::kScripts) = scripts;
        Virtual<std::uint32_t>(scripts, 0x04);
    }
    if (!At<void*>(self, field::kControllers)) {
        void* const controllers = Virtual<void*>(G<void*>(addr::SC4_City), 0xBC);
        At<void*>(self, field::kControllers) = controllers;
        Virtual<std::uint32_t>(controllers, 0x04);
    }
    Virtual<void>(self, 0x104);
    Virtual<void>(self, 0x108);
    return 1;
}

// 0x005110A0, slot 4 Shutdown: with +0x34, slot 45, the three kept objects
// released, the cheat codes, the frame service and the message server let
// go, the exemplar map emptied, the exemplar records' objects (+0x30)
// released, slot 40; +0x34 cleared. AL 1.
std::uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("Shutdown", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    Virtual<void>(self, 0xB4);
    for (const std::size_t at : {field::kControllers, field::kScripts, field::kCells})
        if (void* const object = At<void*>(self, at)) {
            Release(object);
            At<void*>(self, at) = nullptr;
        }
    if (void* const cheats = G<void*>(addr::SC4_CheatCodes))
        Virtual<std::uint8_t>(cheats, 0x18, static_cast<void*>(self + field::kVtable4), static_cast<std::uint32_t>(0));
    if (void* const frames = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<std::uint8_t>(frames, 0x18, static_cast<void*>(self + field::kMessage), static_cast<std::uint32_t>(0xFF));
    if (void* const server = G<void*>(addr::SC4_MessageServer))
        for (const std::uint32_t type : kMessageTypes)
            Virtual<std::uint8_t>(server, 0x18, static_cast<void*>(self + field::kMessage), type);
    char* const map = self + field::kExemplarMap;
    if (At<std::uint32_t>(self, field::kExemplarMapSize) != 0) {
        CallThis<void>(addr::cSC4AutomataManager_EraseExemplarTree, map, At<void*>(At<char*>(map, 0), tree::kParent));
        char* const header = At<char*>(map, 0);
        At<char*>(header, tree::kLeft) = header;
        At<char*>(At<char*>(map, 0), tree::kParent) = nullptr;
        At<char*>(At<char*>(map, 0), tree::kRight) = At<char*>(map, 0);
        At<std::uint32_t>(map, 4) = 0;
    }
    for (char* record = At<char*>(self, field::kExemplarsBegin); record != At<char*>(self, field::kExemplarsEnd);
         record += exemplar::kSize)
        if (void* const object = At<void*>(record, 0x30)) {
            Release(object);
            At<void*>(record, 0x30) = nullptr;
        }
    Virtual<void>(self, 0xA0);
    At<std::uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// 0x0050A6B0, slot 5 Activate (on), ret 4: +0x4C set; with the filter the
// service 0x00B43DC4 gives at slot 0x48 (if any), each automaton the filter
// refuses (slot 0xC with its slot 0x14) gets slot 0x24 with 0, the others
// with +0x4C; the filter released. AL +0x4C.
std::uint8_t __fastcall Activate(char* self, void*, std::uint32_t on)
{
    static bool logged;
    FirstCall("Activate", logged);
    At<std::uint8_t>(self, field::kActive) = static_cast<std::uint8_t>(on);
    void* filter = nullptr;
    Virtual<void>(G<void*>(addr::SC4_Service43DC4), 0x48, static_cast<void*>(&filter));
    char* const head = At<char*>(self, field::kAutomata);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kAutomata); node = At<char*>(node, 0)) {
        void* const automaton = At<void*>(node, 8);
        if (!automaton)
            continue;
        if (filter) {
            void* const occupant = Virtual<void*>(automaton, 0x14);
            if (!Virtual<std::uint8_t>(filter, 0x0C, occupant)) {
                Virtual<void>(automaton, 0x24, static_cast<std::uint32_t>(0));
                continue;
            }
        }
        Virtual<void>(automaton, 0x24, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kActive)));
    }
    if (filter)
        Release(filter);
    return At<std::uint8_t>(self, field::kActive);
}

// 0x0050AA60, slot 23 SetMaxCount (count), ret 4: clamped to 0..5000; when it
// changes, while the count (+0x48) is above it the list's automata that are
// neither flagged 0x200000 (slot 0x7C) nor at 0xFF (slot 0xFC) are removed
// (slot 62), then +0x3C the most and +0x40 ftol(most x 0x00A955A0).
void __fastcall SetMaxCount(char* self, void*, std::int32_t count)
{
    static bool logged;
    FirstCall("SetMaxCount", logged);
    if (count < 0)
        count = 0;
    else if (count > 5000)
        count = 5000;
    if (static_cast<std::uint32_t>(count) == At<std::uint32_t>(self, field::kMaxCount))
        return;
    char* const head = At<char*>(self, field::kAutomata);
    char* node = At<char*>(head, 0);
    if (node != head) {
        while (static_cast<std::uint32_t>(count) < At<std::uint32_t>(self, field::kCount)) {
            void* const automaton = At<void*>(node, 8);
            node = At<char*>(node, 0);
            if (!Virtual<std::uint8_t>(automaton, 0x7C, static_cast<std::uint32_t>(0x200000)) &&
                Virtual<std::uint8_t>(automaton, 0xFC) < 0xFF)
                Virtual<std::uint8_t>(self, 0xF8, automaton);
            if (node == At<char*>(self, field::kAutomata))
                break;
        }
    }
    At<std::int32_t>(self, field::kMaxCount) = count;
    const double share = static_cast<double>(count) * G<double>(g::kShareFactor);
    At<std::int32_t>(self, field::kShare) = static_cast<std::int32_t>(static_cast<std::int64_t>(share));
}

// 0x0050CDE0, slot 33 ReleaseAutomatonSet (set), ret 4 (this not read):
// every automaton of the hash set (+4 of its nodes) released, walked as the
// STLport iterator does; then the set cleared (its buckets kept).
void __fastcall ReleaseAutomatonSet(void*, void*, char* set)
{
    static bool logged;
    FirstCall("ReleaseAutomatonSet", logged);
    struct {
        char* node;
        char* table;
    } it = {};
    CallThis<void*>(addr::SC4HashTable_Begin, set, static_cast<void*>(&it));
    char* node = it.node;
    char* const t = it.table;
    while (node) {
        if (void* const automaton = At<void*>(node, 4))
            Release(automaton);
        if (char* const next = At<char*>(node, 0)) {
            node = next;
            continue;
        }
        std::uint32_t i = At<std::uint32_t>(node, 4) %
                          static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin));
        node = nullptr;
        while (++i < static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin)))
            if ((node = At<char**>(t, table::kBegin)[i]) != nullptr)
                break;
    }
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
}

// 0x00510450, on the group instance table (this not read) a node, ret 4:
// its hash set cleared and its buckets freed, the node freed.
void __fastcall FreeInstanceNode(void*, void*, char* node)
{
    static bool logged;
    FirstCall("FreeInstanceNode", logged);
    char* const set = node + 8;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
    if (void* const buckets = At<void*>(set, table::kBegin))
        Free(buckets);
    if (node)
        Free(node);
}

// 0x00510F10, the group instance table's erase (iterator), ret 4: the node
// unlinked from its key's bucket, its set cleared and freed (0x00510450 when
// it is not the bucket's first), the count down. Nothing when not found.
void __fastcall EraseInstances(char* t, void*, char** it)
{
    static bool logged;
    FirstCall("EraseInstances", logged);
    char* const node = it[0];
    if (!node)
        return;
    char** const slot = &At<char**>(t, table::kBegin)[Bucket(t, At<std::uint32_t>(node, 4))];
    char* first = *slot;
    if (first == node) {
        *slot = At<char*>(first, 0);
        char* const set = first + 8;
        CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
        if (void* const buckets = At<void*>(set, table::kBegin))
            Free(buckets);
        Free(first);
        --At<std::uint32_t>(t, table::kCount);
        return;
    }
    for (char* n = At<char*>(first, 0); n; n = At<char*>(n, 0)) {
        if (n == node) {
            At<char*>(first, 0) = At<char*>(node, 0);
            CallThis<void>(addr::cSC4AutomataManager_FreeInstanceNode, t, node);
            --At<std::uint32_t>(t, table::kCount);
            return;
        }
        first = n;
    }
}

// 0x00512030, the group instance table's insert_unique (result, pair), ret
// 8: a 0x1C-byte node (the key, a copy of the pair's hash set at +8) at the
// head of the key's bucket, result {node, table, 1}; {node, table, 0} for a
// key already there.
void __fastcall InsertInstances(char* t, void*, void** result, const std::uint32_t* pair)
{
    static bool logged;
    FirstCall("InsertInstances", logged);
    const std::uint32_t key = pair[0];
    const std::uint32_t bucket = Bucket(t, key);
    char* const head = At<char**>(t, table::kBegin)[bucket];
    for (char* n = head; n; n = At<char*>(n, 0))
        if (At<std::uint32_t>(n, 4) == key) {
            result[1] = t;
            result[0] = n;
            At<std::uint8_t>(result, 8) = 0;
            return;
        }
    char* const node = static_cast<char*>(Allocate(0x1C));
    At<std::uint32_t>(node, 0) = 0;
    if (node + 4) {
        At<std::uint32_t>(node, 4) = key;
        char* const set = node + 8;
        At<std::uint32_t>(set, table::kBegin) = 0;
        At<std::uint32_t>(set, table::kEnd) = 0;
        At<std::uint32_t>(set, table::kCapacity) = 0;
        At<std::uint32_t>(set, table::kCount) = 0;
        CallThis<void*>(addr::SC4Uint32HashSet_Assign, set, static_cast<const void*>(pair + 1));
    }
    At<char*>(node, 0) = head;
    At<char**>(t, table::kBegin)[bucket] = node;
    ++At<std::uint32_t>(t, table::kCount);
    result[0] = node;
    result[1] = t;
    At<std::uint8_t>(result, 8) = 1;
}

// 0x00513C40, slot 31 TrackAutomataGroupInstances (group, track), ret 8.
// 1 for a group the exemplar map does not hold (slot 30). Tracking: a group
// already tracked answers 1; else a new entry (an empty set of 100 buckets
// copied in) and every automaton of the list whose slot 0x14 object is in
// the group (its slot 0x58) put in the set and referenced; AL whether it was
// inserted. Untracking: the group's set released (slot 33) and the entry
// erased (0x00510F10); AL 1.
std::uint8_t __fastcall TrackAutomataGroupInstances(char* self, void*, std::uint32_t group, std::uint32_t track)
{
    static bool logged;
    FirstCall("TrackAutomataGroupInstances", logged);
    if (!Virtual<std::uint8_t>(self, slot::kCanCreateGroup, group))
        return 1;
    char* const instances = self + field::kGroupInstances;
    struct {
        char* node;
        char* table;
    } it = {};
    if (!static_cast<std::uint8_t>(track)) {
        CallThis<void>(addr::SC4Uint32HashTable_Find, instances, static_cast<void*>(&it), &group);
        if (it.node) {
            Virtual<void>(self, 0x84, static_cast<void*>(it.node + 8));
            char* iterator[2] = {it.node, it.table};
            CallThis<void>(addr::cSC4AutomataManager_EraseInstances, instances, static_cast<void*>(iterator));
        }
        return 1;
    }
    CallThis<void>(addr::SC4Uint32HashTable_Find, instances, static_cast<void*>(&it), &group);
    if (it.node)
        return 1;
    alignas(4) char empty[table::kSize] = {};
    CallThis<void*>(addr::SC4Uint32HashSet_Construct, empty, table::kBuckets);
    alignas(4) char pair[4 + table::kSize] = {};
    At<std::uint32_t>(pair, 0) = group;
    CallThis<void*>(addr::SC4Uint32HashSet_Assign, pair + 4, static_cast<const void*>(empty));
    CallThis<void>(addr::SC4Uint32HashSet_Resize, instances, At<std::uint32_t>(instances, table::kCount) + 1);
    alignas(4) char result[12] = {};
    CallThis<void>(addr::cSC4AutomataManager_InsertInstances, instances, static_cast<void*>(result),
                   static_cast<const void*>(pair));
    CallThis<void>(addr::SC4Uint32HashSet_Clear, pair + 4);
    if (void* const buckets = At<void*>(pair + 4, table::kBegin))
        Free(buckets);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, empty);
    if (void* const buckets = At<void*>(empty, table::kBegin))
        Free(buckets);
    const std::uint8_t inserted = At<std::uint8_t>(result, 8);
    if (!inserted)
        return inserted;
    char* const set = At<char*>(result, 0) + 8;
    char* const head = At<char*>(self, field::kAutomata);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kAutomata); node = At<char*>(node, 0)) {
        void* automaton = At<void*>(node, 8);
        void* const occupant = Virtual<void*>(automaton, 0x14);
        if (Virtual<std::uint8_t>(occupant, 0x58, group)) {
            alignas(4) char out[12] = {};
            CallThis<void*>(addr::SC4Uint32HashSet_InsertUnique, set, static_cast<void*>(out),
                            static_cast<void*>(&automaton));
            Virtual<std::uint32_t>(automaton, 0x04);
        }
    }
    return inserted;
}

// 0x00513910, slot 39 (this not read): the first time (0x00B21B88), with the
// cheat codes and 0x00B43C94's slot 0x54, the five cheats (0xE97FCBC8,
// 0xE97FCBC9, 0x0B70C96D, 0x6BB354C7, 0xCC478B79 with the names at
// 0x00B09940..0x00B09950) registered (slot 0xC), then the tuning load; the
// counter up every time. AL 1.
std::uint8_t __fastcall RegisterCheats(void*, void*)
{
    static bool logged;
    FirstCall("RegisterCheats", logged);
    if (G<std::uint32_t>(g::kCheatsOnce) == 0) {
        void* const cheats = G<void*>(addr::SC4_CheatCodes);
        if (cheats && Virtual<std::uint8_t>(G<void*>(addr::SC4_Service43C94), 0x54)) {
            static constexpr std::uint32_t kIds[] = {0xE97FCBC8, 0xE97FCBC9, 0x0B70C96D, 0x6BB354C7, 0xCC478B79};
            for (int i = 0; i < 5; ++i) {
                alignas(4) char text[0x10] = {};
                void* const string =
                    CallThis<void*>(addr::SC4String_FromText, text, G<const char*>(g::kCheatTexts + 4 * i));
                Virtual<std::uint8_t>(cheats, 0x0C, kIds[i], string);
                At<std::uintptr_t>(text, 0) = 0x00A80810;
                if (void* const buffer = At<void*>(text, 4))
                    Free(buffer);
            }
        }
        CallCdecl<void>(addr::cSC4AutomataManager_LoadTuning);
    }
    ++G<std::uint32_t>(g::kCheatsOnce);
    return 1;
}

// 0x005120E0, an exemplar record's assignment (this = the destination,
// other), ret 4: the words +0..+0x18, the string at +0x20 (SC4String::
// AssignRange unless it is the same), +0x30, the byte +0x34, the words
// +0x38..+0x60, the byte +0x64 and +0x68. The object at +0x30 is copied
// without a reference. Answers this.
char* __fastcall CopyExemplar(char* self, void*, char* other)
{
    static bool logged;
    FirstCall("CopyExemplar", logged);
    for (std::size_t at = 0; at <= 0x18; at += 4)
        At<std::uint32_t>(self, at) = At<std::uint32_t>(other, at);
    char* const source = other + 0x1C ? other + 0x20 : nullptr;
    if (source != self + 0x20) {
        void* tag = other;
        CallThis<void>(addr::SC4String_AssignRange, self + 0x20, At<const char*>(source, 0), At<const char*>(source, 4),
                       static_cast<void*>(&tag));
    }
    At<std::uint32_t>(self, 0x30) = At<std::uint32_t>(other, 0x30);
    At<std::uint8_t>(self, 0x34) = At<std::uint8_t>(other, 0x34);
    for (std::size_t at = 0x38; at <= 0x60; at += 4)
        At<std::uint32_t>(self, at) = At<std::uint32_t>(other, at);
    At<std::uint8_t>(self, 0x64) = At<std::uint8_t>(other, 0x64);
    At<std::uint32_t>(self, 0x68) = At<std::uint32_t>(other, 0x68);
    return self;
}

// 0x00512C80 (first, last, to, tag, zero), cdecl: std::copy of the exemplar
// records [first, last) to `to` by 0x005120E0; the end of the copy (`to`
// when there is nothing).
char* __cdecl CopyExemplars(char* first, char* last, char* to, void*, std::uint32_t)
{
    static bool logged;
    FirstCall("CopyExemplars", logged);
    std::int32_t count = static_cast<std::int32_t>((last - first) / static_cast<std::ptrdiff_t>(exemplar::kSize));
    if (count <= 0)
        return to;
    do {
        CallThis<char*>(addr::cSC4AutomataManager_CopyExemplar, to, first);
        first += exemplar::kSize;
        to += exemplar::kSize;
    } while (--count != 0);
    return to;
}

// 0x00514520, slot 66: the exemplar vector cleared (copied onto its own end
// by 0x00512C80, the records destroyed by 0x00510650), then the resource
// manager's (0x00B43CA8) slot 0x48 enumeration of type 0x6534284A, whose
// slot 0x1C runs the game's callback 0x00513A60 with the manager.
void __fastcall LoadExemplars(char* self, void*)
{
    static bool logged;
    FirstCall("LoadExemplars", logged);
    char tag = 0;
    char* const end = CallCdecl<char*>(addr::cSC4AutomataManager_CopyExemplars, At<char*>(self, field::kExemplarsEnd),
                                       At<char*>(self, field::kExemplarsEnd), At<char*>(self, field::kExemplarsBegin),
                                       static_cast<void*>(&tag), static_cast<std::uint32_t>(0));
    char tag2 = 0;
    CallCdecl<void>(addr::cSC4AutomataManager_DestroyExemplars, static_cast<void*>(end),
                    At<void*>(self, field::kExemplarsEnd), static_cast<void*>(&tag2));
    At<char*>(self, field::kExemplarsEnd) = end;
    void* list = nullptr;
    if (Virtual<void*>(G<void*>(addr::SC4_ResourceManager), 0x48, static_cast<void*>(&list),
                       static_cast<std::uint32_t>(0x6534284A))) {
        Virtual<void>(list, 0x1C, static_cast<std::uintptr_t>(0x00513A60), static_cast<void*>(self));
        Release(list);
    }
}

// ====================================================================================
// The tuning load (simulators.automata.awmgr.tuning)

// 0x00510C80, the kind curve table's (0x00B21D44) insert_unique (result,
// pair {kind, curve}), ret 8: a 0x18-byte node (the kind, the curve copied
// at +8) at the head of the bucket.
void __fastcall InsertKindCurve(char* t, void*, void** result, const char* pair)
{
    static bool logged;
    FirstCall("InsertKindCurve", logged);
    const std::uint32_t key = At<std::uint32_t>(const_cast<char*>(pair), 0);
    const std::uint32_t bucket = Bucket(t, key);
    char* const head = At<char**>(t, table::kBegin)[bucket];
    for (char* n = head; n; n = At<char*>(n, 0))
        if (At<std::uint32_t>(n, 4) == key) {
            result[1] = t;
            result[0] = n;
            At<std::uint8_t>(result, 8) = 0;
            return;
        }
    char* const node = static_cast<char*>(Allocate(0x18));
    At<std::uint32_t>(node, 0) = 0;
    if (node + 4) {
        At<std::uint32_t>(node, 4) = key;
        CallThis<void*>(addr::SC4Curve_Copy, node + 8, static_cast<const void*>(pair + 4));
    }
    At<char*>(node, 0) = head;
    At<char**>(t, table::kBegin)[bucket] = node;
    ++At<std::uint32_t>(t, table::kCount);
    result[0] = node;
    result[1] = t;
    At<std::uint8_t>(result, 8) = 1;
}

// 0x00510D20, the tuning map's (0x00B21D60) insert_unique (result, pair
// {key, four floats, curve}), ret 8: a 0x28-byte node at the head of the
// bucket (the key and floats copied, the curve by SC4Curve::Copy at +0x18).
void __fastcall InsertTuningRecord(char* t, void*, void** result, const char* pair)
{
    static bool logged;
    FirstCall("InsertTuningRecord", logged);
    const std::uint32_t key = At<std::uint32_t>(const_cast<char*>(pair), 0);
    const std::uint32_t bucket = Bucket(t, key);
    char* const head = At<char**>(t, table::kBegin)[bucket];
    for (char* n = head; n; n = At<char*>(n, 0))
        if (At<std::uint32_t>(n, 4) == key) {
            result[1] = t;
            result[0] = n;
            At<std::uint8_t>(result, 8) = 0;
            return;
        }
    char* const node = static_cast<char*>(Allocate(0x28));
    At<std::uint32_t>(node, 0) = 0;
    if (node + 4) {
        for (std::size_t at = 0; at < 0x14; at += 4)
            At<std::uint32_t>(node, 4 + at) = At<std::uint32_t>(const_cast<char*>(pair), at);
        CallThis<void*>(addr::SC4Curve_Copy, node + 0x18, static_cast<const void*>(pair + 0x14));
    }
    At<char*>(node, 0) = head;
    At<char**>(t, table::kBegin)[bucket] = node;
    ++At<std::uint32_t>(t, table::kCount);
    result[0] = node;
    result[1] = t;
    At<std::uint8_t>(result, 8) = 1;
}

// 0x00512860 (exemplar, property, kind), cdecl: the kind's curve in the
// table 0x00B21D44, made empty when not there (0x00510C80), loaded from the
// property when the exemplar has it.
void __cdecl LoadKindCurve(void* exemplar, std::uint32_t property, std::uint32_t kind)
{
    static bool logged;
    FirstCall("LoadKindCurve", logged);
    char* const t = reinterpret_cast<char*>(g::kKindCurves);
    char* node = At<char**>(t, table::kBegin)[Bucket(t, kind)];
    while (node && At<std::uint32_t>(node, 4) != kind)
        node = At<char*>(node, 0);
    if (!node) {
        alignas(4) char curve[0x10] = {};
        void* const made = CallThis<void*>(addr::SC4Curve_Init, curve);
        alignas(4) char pair[0x14] = {};
        At<std::uint32_t>(pair, 0) = kind;
        CallThis<void*>(addr::SC4Curve_Copy, pair + 4, static_cast<const void*>(made));
        CallThis<void>(addr::SC4Uint32HashSet_Resize, t, At<std::uint32_t>(t, table::kCount) + 1);
        void* result[3] = {nullptr, t, nullptr};
        CallThis<void>(addr::cSC4AutomataManager_InsertKindCurve, t, static_cast<void*>(result),
                       static_cast<const void*>(pair));
        if (void* const buffer = At<void*>(pair, 4))
            Free(buffer);
        if (void* const buffer = At<void*>(curve, 0))
            Free(buffer);
        if (At<std::uint8_t>(result, 8))
            node = static_cast<char*>(result[0]);
        if (!node)
            return;
    }
    if (void* const value = Virtual<void*>(exemplar, 0x24, property))
        CallThis<std::uint8_t>(addr::SC4Curve_LoadFromProperty, node + 8, value);
}

// 0x00512940 (exemplar, property, key), cdecl: the tuning map's record of
// the key (0x00B21D60), made when not there (0.2, 0.5, 0.2, 0.5 and a curve
// through (0, 0) and (1, 1); 0x00510D20); then the property's four floats
// (variant slot 0x1BC) into it and its curve from the property + 0x100.
void __cdecl LoadTuningRecord(void* exemplar, std::uint32_t property, std::uint32_t key)
{
    static bool logged;
    FirstCall("LoadTuningRecord", logged);
    char* const t = reinterpret_cast<char*>(g::kTuningMap);
    char* node = At<char**>(t, table::kBegin)[Bucket(t, key)];
    while (node && At<std::uint32_t>(node, 4) != key)
        node = At<char*>(node, 0);
    if (!node) {
        alignas(4) char curve[0x10] = {};
        CallThis<void*>(addr::SC4Curve_Init, curve);
        // A vector of (x, y) float pairs: begin, end, capacity.
        alignas(4) char points[0xC] = {};
        float point[2] = {0.0f, 0.0f};
        std::uint32_t tag = 0;
        using Insert = void(__thiscall*)(void*, void*, const float*, void*, std::uint32_t, std::uint32_t);
        reinterpret_cast<Insert>(addr::SC4PointVector_InsertN)(points, nullptr, point, &tag, 1, 1);
        point[0] = 1.0f;
        point[1] = 1.0f;
        char*& end = At<char*>(points, 4);
        if (end != At<char*>(points, 8)) {
            if (end) {
                At<float>(end, 0) = 1.0f;
                At<float>(end, 4) = 1.0f;
            }
            end += 8;
        } else {
            reinterpret_cast<Insert>(addr::SC4PointVector_InsertN)(points, end, point, &tag, 1, 1);
        }
        CallThis<std::uint8_t>(addr::SC4Curve_SetPoints, curve, static_cast<const void*>(points));
        alignas(4) char pair[0x24] = {};
        At<std::uint32_t>(pair, 0) = key;
        At<std::uint32_t>(pair, 4) = 0x3E4CCCCD; // 0.2f
        At<std::uint32_t>(pair, 8) = 0x3F000000; // 0.5f
        At<std::uint32_t>(pair, 0xC) = 0x3E4CCCCD;
        At<std::uint32_t>(pair, 0x10) = 0x3F000000;
        CallThis<void*>(addr::SC4Curve_Copy, pair + 0x14, static_cast<const void*>(curve));
        CallThis<void>(addr::SC4Uint32HashSet_Resize, t, At<std::uint32_t>(t, table::kCount) + 1);
        void* result[3] = {};
        CallThis<void>(addr::cSC4AutomataManager_InsertTuningRecord, t, static_cast<void*>(result),
                       static_cast<const void*>(pair));
        if (void* const buffer = At<void*>(pair, 0x14))
            Free(buffer);
        if (At<std::uint8_t>(result, 8))
            node = static_cast<char*>(result[0]);
        if (void* const buffer = At<void*>(points, 0))
            Free(buffer);
        if (void* const buffer = At<void*>(curve, 0))
            Free(buffer);
        if (!node)
            return;
    }
    if (void* const value = Virtual<void*>(exemplar, 0x24, property)) {
        void* const variant = Virtual<void*>(value, 0x14);
        const std::uint32_t* const floats = Virtual<const std::uint32_t*>(variant, 0x1BC);
        for (int i = 0; i < 4; ++i)
            At<std::uint32_t>(node, 8 + 4 * i) = floats[i];
    }
    if (void* const value = Virtual<void*>(exemplar, 0x24, property + 0x100))
        CallThis<std::uint8_t>(addr::SC4Curve_LoadFromProperty, node + 0x18, value);
}

// The property readers the tuning load uses.
void* Property(void* exemplar, std::uint32_t id) { return Virtual<void*>(exemplar, 0x24, id); }
void* Variant(void* property) { return Virtual<void*>(property, 0x14); }
std::uint32_t Count(void* variant) { return Virtual<std::uint32_t>(variant, 0x10); }
void ReadFloat(void* e, std::uint32_t id, std::uintptr_t to) { CallCdecl<std::uint8_t>(addr::SC4_GetPropertyFloat, e, id, reinterpret_cast<float*>(to)); }
void ReadU32(void* e, std::uint32_t id, std::uintptr_t to) { CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint32, e, id, reinterpret_cast<std::uint32_t*>(to)); }
void ReadU8(void* e, std::uint32_t id, std::uintptr_t to) { CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint8, e, id, reinterpret_cast<std::uint8_t*>(to)); }
void ReadBool(void* e, std::uint32_t id, std::uintptr_t to) { CallCdecl<std::uint8_t>(addr::SC4_GetPropertyBool, e, id, reinterpret_cast<std::uint8_t*>(to)); }
void ReadCurve(void* e, std::uint32_t id, std::uintptr_t curve)
{
    if (void* const p = Property(e, id))
        CallThis<std::uint8_t>(addr::SC4Curve_LoadFromProperty, reinterpret_cast<void*>(curve), p);
}
// Two words of the variant's slot (0x19C uint32s, 0x1BC floats).
void ReadPair(void* e, std::uint32_t id, std::size_t slot, std::uintptr_t to)
{
    if (void* const p = Property(e, id)) {
        const std::uint32_t* const w = Virtual<const std::uint32_t*>(Variant(p), slot);
        G<std::uint32_t>(to) = w[0];
        G<std::uint32_t>(to + 4) = w[1];
    }
}
// Two bytes of the variant's uint8 values (slot 0x17C), as words or bytes.
void ReadBytesAsWords(void* e, std::uint32_t id, std::uintptr_t to)
{
    if (void* const p = Property(e, id)) {
        const std::uint8_t* const b = Virtual<const std::uint8_t*>(Variant(p), 0x17C);
        G<std::uint32_t>(to) = b[0];
        G<std::uint32_t>(to + 4) = b[1];
    }
}
void ReadTwoBytes(void* e, std::uint32_t id, std::uintptr_t to)
{
    if (void* const p = Property(e, id)) {
        const std::uint8_t* const b = Virtual<const std::uint8_t*>(Variant(p), 0x17C);
        G<std::uint8_t>(to) = b[0];
        G<std::uint8_t>(to + 1) = b[1];
    }
}
// A mask of the variant's bytes: bit b - 1 per byte b, all ones at a zero
// byte or without the property.
void ReadMask(void* e, std::uint32_t id, std::uintptr_t to)
{
    void* const p = Property(e, id);
    if (!p) {
        G<std::uint32_t>(to) = 0xFFFFFFFFu;
        return;
    }
    G<std::uint32_t>(to) = 0;
    void* const v = Variant(p);
    const std::uint8_t* const b = Virtual<const std::uint8_t*>(v, 0x17C);
    if (Count(v) == 0)
        return;
    std::uint32_t i = 0;
    do {
        const std::uint8_t byte = b[i];
        if (!byte) {
            G<std::uint32_t>(to) = 0xFFFFFFFFu;
            return;
        }
        G<std::uint32_t>(to) |= 1u << ((byte - 1) & 31);
        ++i;
    } while (i < Count(v));
}
// Up to `most` floats of the variant (slot 0x1BC).
void ReadFloats(void* e, std::uint32_t id, std::uintptr_t to, std::uint32_t most)
{
    void* const p = Property(e, id);
    if (!p)
        return;
    void* const v = Variant(p);
    const std::uint32_t* const w = Virtual<const std::uint32_t*>(v, 0x1BC);
    if (Count(v) == 0)
        return;
    std::uint32_t i = 0;
    do {
        if (i >= most)
            return;
        G<std::uint32_t>(to + 4 * i) = w[i];
        ++i;
    } while (i < Count(v));
}
// Two floats with defaults for what the property does not have.
void ReadRange(void* e, std::uint32_t id, std::uintptr_t to, std::uint32_t low, std::uint32_t high)
{
    void* const p = Property(e, id);
    if (!p)
        return;
    void* const v = Variant(p);
    const std::uint32_t* const w = Virtual<const std::uint32_t*>(v, 0x1BC);
    G<std::uint32_t>(to) = Count(v) > 0 ? w[0] : low;
    G<std::uint32_t>(to + 4) = Count(v) > 1 ? w[1] : high;
}

// A word as the game's FILD of it takes it unsigned.
double Unsigned(std::int32_t value)
{
    double d = static_cast<double>(value);
    if (value < 0)
        d += static_cast<double>(G<float>(addr::SC4_FloatTwoTo32));
    return d;
}

// 0x00512CF0, the tuning load (cdecl, no arguments): the automata tuning
// exemplar (0x6534284A, 0xA998D30B, 1; interface 0xA52160F5) from the
// resource manager into the statics 0x00B21BC8..0x00B21D5C, the kind curves
// (0x00512860) and the tuning map (0x00512940).
void __cdecl LoadTuning()
{
    static bool logged;
    FirstCall("LoadTuning", logged);
    void* const resources = G<void*>(addr::SC4_ResourceManager);
    if (!resources)
        return;
    std::uint32_t key[3] = {0x6534284A, 0xA998D30B, 1};
    void* resource = nullptr;
    if (!Virtual<std::uint8_t>(resources, 0x0C, static_cast<void*>(key), static_cast<std::uint32_t>(0xA52160F5),
                               static_cast<void*>(&resource), static_cast<std::uint32_t>(0),
                               static_cast<std::uint32_t>(0)))
        return;
    void* const e = Virtual<void*>(resource, 0x1C);
    ReadCurve(e, 0x4998C89C, 0x00B21BF4);
    ReadFloat(e, 0x4998C89E, 0x00B21C04);
    ReadCurve(e, 0x4998C901, 0x00B21C60);
    ReadFloat(e, 0x4998C903, 0x00B21C70);
    ReadCurve(e, 0x4A511873, 0x00B21C08);
    ReadFloat(e, 0x4998C89E, 0x00B21C18);
    ReadCurve(e, 0x0A51188A, 0x00B21C1C);
    ReadFloat(e, 0xEA511DE0, 0x00B21C2C);
    ReadCurve(e, 0x2A511893, 0x00B21C30);
    ReadFloat(e, 0xCA511DE6, 0x00B21C40);
    ReadCurve(e, 0xCA51189A, 0x00B21C44);
    ReadFloat(e, 0xCA511DF2, 0x00B21C54);
    ReadU32(e, 0x4998C89F, 0x00B21BC8);
    ReadU32(e, 0x4998C90B, 0x00B21BF0);
    ReadPair(e, 0x4998C900, 0x19C, 0x00B21BCC);
    ReadBytesAsWords(e, 0x4998C902, 0x00B21BD4);
    ReadFloat(e, 0x2A511CBC, 0x00B21C88);
    ReadU32(e, 0x4998C904, 0x00B21BDC);
    ReadPair(e, 0x4998C905, 0x19C, 0x00B21BE0);
    ReadBytesAsWords(e, 0x4998C909, 0x00B21BE8);
    ReadFloat(e, 0x4998C912, 0x00B21C74);
    ReadFloat(e, 0x4998C913, 0x00B21C78);
    ReadFloat(e, 0x4998C914, 0x00B21C7C);
    ReadFloat(e, 0x4998C915, 0x00B21C80);
    ReadFloat(e, 0x4998C916, 0x00B21C84);
    ReadPair(e, 0x4998C90A, 0x1BC, 0x00B21C90);
    ReadU8(e, 0x4998C91D, 0x00B21C98);
    ReadMask(e, 0xA9AF1E54, 0x00B21C9C);
    ReadMask(e, 0xA9AF1E55, 0x00B21CA0);
    ReadU8(e, 0x4998C918, 0x00B21CA4);
    ReadU8(e, 0x4998C919, 0x00B21CA5);
    --G<std::uint8_t>(0x00B21CA4);
    --G<std::uint8_t>(0x00B21CA5);
    const auto kindCurve = [&](std::uint32_t id, std::uint32_t kind) {
        CallCdecl<void>(addr::cSC4AutomataManager_LoadKindCurve, e, id, kind);
    };
    kindCurve(0x4998C90C, 1);
    kindCurve(0x4998C90D, 2);
    kindCurve(0x4998C90E, 5);
    kindCurve(0x4998C90F, 4);
    kindCurve(0x4998C911, 3);
    kindCurve(0x4998C910, 7);
    kindCurve(0x6A09F631, 8);
    kindCurve(0xEC06E7CF, 0xB);
    ReadBool(e, 0x4998C91C, 0x00B21D58);
    // The kind mask: bit b per byte b (no zero test), all ones without it.
    if (void* const p = Property(e, 0x4998C89D)) {
        void* const v = Variant(p);
        const std::uint8_t* const b = Virtual<const std::uint8_t*>(v, 0x17C);
        G<std::uint32_t>(0x00B21D5C) = 0;
        if (Count(v) != 0) {
            std::uint32_t i = 0;
            do {
                G<std::uint32_t>(0x00B21D5C) |= 1u << (b[i] & 31);
                ++i;
            } while (i < Count(v));
        }
    } else {
        G<std::uint32_t>(0x00B21D5C) = 0xFFFFFFFFu;
    }
    Release(resource);
    ReadFloat(e, 0x89AEC88B, 0x00B21CB4);
    ReadU8(e, 0x89AEC88C, 0x00B21CB8);
    ReadU8(e, 0x89AEC88D, 0x00B21CB9);
    ReadU32(e, 0x89AEC88E, 0x00B21CBC);
    ReadU8(e, 0xC9EE640D, 0x00B21CC0);
    ReadBool(e, 0x49BE9565, 0x00B21CC1);
    ReadFloat(e, 0xC9C18EAE, 0x00B21CC4);
    ReadFloats(e, 0xA9D6CC17, 0x00B21CC8, 2);
    ReadFloats(e, 0xA9D6CC18, 0x00B21CD0, 3);
    ReadFloat(e, 0xC9EE640C, 0x00B21CDC);
    ReadRange(e, 0xC9DA95D4, 0x00B21CE0, 0xC2B40000, 0x42B40000); // -90, 90
    ReadRange(e, 0x29DD40C1, 0x00B21CE8, 0xBF800000, 0x3F800000); // -1, 1
    ReadRange(e, 0x29DD40C2, 0x00B21CF0, 0xBF800000, 0x3F800000);
    G<std::uint32_t>(0x00B21CF8) = 0;
    G<std::uint32_t>(0x00B21CFC) = 0;
    ReadFloat(e, 0xC9EE640A, 0x00B21CF8);
    G<float>(0x00B21CF8) = static_cast<float>(static_cast<double>(G<float>(0x00B21CF8)) *
                                              static_cast<double>(G<float>(g::kFloatA95300)));
    ReadFloat(e, 0xC9EE640B, 0x00B21CFC);
    G<std::uint32_t>(0x00B21D00) = 0;
    ReadFloat(e, 0x6A09F62F, 0x00B21D00);
    ReadTwoBytes(e, 0x6A09F630, 0x00B21D04);
    ReadTwoBytes(e, 0xEA441C97, 0x00B21D06);
    ReadTwoBytes(e, 0x8A441C9C, 0x00B21D08);
    G<std::uint32_t>(0x00B21D14) = 0x14;
    ReadU32(e, 0x0A53FD32, 0x00B21D14);
    ReadPair(e, 0x6A440D40, 0x19C, 0x00B21D0C);
    {
        std::int32_t value = 100;
        CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint32, e, static_cast<std::uint32_t>(0xCA5545EA),
                                reinterpret_cast<std::uint32_t*>(&value));
        G<float>(0x00B21C8C) = static_cast<float>(Unsigned(value) * static_cast<double>(G<float>(g::kFloatA952E8)));
    }
    if (void* const p = Property(e, 0x6B42922C)) {
        const std::uint32_t* const w = Virtual<const std::uint32_t*>(Variant(p), 0x1BC);
        for (int i = 0; i < 4; ++i)
            G<std::uint32_t>(0x00B21D18 + 4 * i) = w[i];
    }
    ReadU8(e, 0x6BAC0B3E, 0x00B21D28);
    // The word vector at 0x00B21CA8: emptied, then sized to the property's
    // count and filled from it.
    G<std::uint32_t>(0x00B21CAC) = G<std::uint32_t>(g::kVectorCA8);
    if (void* const p = Property(e, 0x4BAE8FE0)) {
        void* const v = Variant(p);
        const std::uint32_t* const w = Virtual<const std::uint32_t*>(v, 0x1BC);
        const std::uint32_t wanted = Count(v);
        std::uint32_t* const end = G<std::uint32_t*>(0x00B21CAC);
        std::uint32_t* const begin = G<std::uint32_t*>(g::kVectorCA8);
        const std::uint32_t size = static_cast<std::uint32_t>(end - begin);
        std::uint32_t zero = 0;
        if (wanted < size)
            G<std::uint32_t*>(0x00B21CAC) = begin + wanted;
        else
            CallThis<void>(addr::SC4FloatVector_InsertN, reinterpret_cast<void*>(g::kVectorCA8), static_cast<void*>(end),
                           wanted - size, static_cast<const void*>(&zero));
        for (std::uint32_t i = 0; i < Count(v); ++i)
            G<std::uint32_t*>(g::kVectorCA8)[i] = w[i];
    }
    ReadFloat(e, 0x4BAE8FE1, 0x00B21D2C);
    ReadFloat(e, 0x4BAE8FE2, 0x00B21D30);
    ReadFloat(e, 0xEC0700C0, 0x00B21C58);
    ReadFloat(e, 0x6C0700C4, 0x00B21C5C);
    const auto record = [&](std::uint32_t id, std::uint32_t key) {
        CallCdecl<void>(addr::cSC4AutomataManager_LoadTuningRecord, e, id, key);
    };
    record(0xCBA5895B, 3);
    record(0xCBA5895C, 0);
    record(0xCBA5895D, 6);
    record(0xCBA5895E, 2);
    record(0xCBA5895F, 1);
    {
        std::int32_t value = 0x32;
        CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint32, e, static_cast<std::uint32_t>(0x8C3E8185),
                                reinterpret_cast<std::uint32_t*>(&value));
        G<float>(0x00B21D40) =
            static_cast<float>(static_cast<double>(G<float>(addr::SC4_FloatOneA81228)) / Unsigned(value));
    }
    ReadU8(e, 0xEBFB4A0A, 0x00B21D34);
    ReadFloat(e, 0xCC101142, 0x00B21D38);
    ReadU32(e, 0x6C200139, 0x00B21D3C);
}

// ====================================================================================
// Messages (simulators.automata.awmgr.messages)

// 0x00506970, the message target's (+0xC) slot 3 DoMessage (message), ret 4.
// Answers 1 for every message, AL only.
std::uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged;
    FirstCall("DoMessage", logged);
    char* const self = part - field::kMessage;
    const std::uint32_t type = Virtual<std::uint32_t>(message, 0x10);
    switch (type) {
    case 0x26D31EC1: // slot 56
        Virtual<void>(self, 0xE0);
        break;
    case 0x06CDB65A: { // slot 50 (slot 0x3C's, slot 0x28's)
        const std::uint32_t a = Virtual<std::uint32_t>(message, 0x28);
        const std::uint32_t b = Virtual<std::uint32_t>(message, 0x3C);
        Virtual<std::uint8_t>(self, 0xC8, b, a);
        break;
    }
    case 0x26D31EC2: // slot 45
        Virtual<void>(self, 0xB4);
        break;
    case 0x2977AE2F: { // slot 53 with the occupant when its float (slot 0x24) is not below 0x00B0996C
        void* const occupant = Virtual<void*>(message, 0x28);
        const float* const value = Virtual<const float*>(occupant, 0x24);
        if (CallThis<std::uint8_t>(addr::SC4UI_FloatAtLeast, const_cast<float*>(value),
                                   reinterpret_cast<const float*>(0x00B0996C)))
            Virtual<std::uint8_t>(self, 0xD4, occupant);
        break;
    }
    case 0x4A7FB807: // the scale switch +0xA3D
        At<std::uint8_t>(self, field::kA3D) = Virtual<std::uint32_t>(message, 0x24) != 0 ? 1 : 0;
        break;
    case 0x86AD10EE: { // slot 63 (the frame: slot 0x38's, slot 0x4C's)
        const std::uint32_t a = Virtual<std::uint32_t>(message, 0x38);
        const std::uint32_t b = Virtual<std::uint32_t>(message, 0x4C);
        Virtual<std::uint8_t>(self, 0xFC, a, b);
        break;
    }
    case 0x89D3E9F3: { // slot 51 with slot 0x80's when it and slot 0x24's are set
        void* const object = Virtual<void*>(message, 0x80);
        const bool on = Virtual<std::uint32_t>(message, 0x24) != 0;
        if (object && on)
            Virtual<std::uint8_t>(self, 0xCC, object);
        break;
    }
    case 0x99EF1142:
    case 0x99EF1143: { // slots 54 and 55 with an occupant not of type 0x029244DB
        void* const occupant = Virtual<void*>(message, 0x28);
        void* const kind = Virtual<void*>(occupant, 0x18);
        if (!Virtual<std::uint8_t>(kind, 0x0C, static_cast<std::uint32_t>(0x029244DB)))
            Virtual<std::uint8_t>(self, type == 0x99EF1142 ? 0xD8 : 0xDC, occupant);
        break;
    }
    case 0xC9F775BB: // slot 58
        Virtual<void>(self, 0xE8);
        break;
    case 0xA6B79612: { // the camera moved: past the zoom's distance, the point kept and slot 48
        if (At<std::uint8_t>(self, off::kCameraHold))
            break;
        char* const camera = G<char*>(addr::SC4_CameraView);
        float* const at = &At<float>(camera, 0x3C);
        float* const kept = &At<float>(self, off::kCameraPoint);
        const double distance = CallCdecl<double>(addr::SC4_DistanceSquared3, static_cast<const float*>(kept),
                                                  static_cast<const float*>(at));
        const double limit = static_cast<double>(G<float>(g::kZoomLimits + 4 * At<std::uint32_t>(camera, 0xC)));
        if (!(distance > limit))
            break;
        for (int i = 0; i < 3; ++i) // as words, as the game copies them
            reinterpret_cast<std::uint32_t*>(kept)[i] = reinterpret_cast<const std::uint32_t*>(at)[i];
        Virtual<std::uint8_t>(self, 0xC0);
        break;
    }
    case 0xA6B79602: // slot 48
        Virtual<std::uint8_t>(self, 0xC0);
        break;
    case 0xCAD61E00:
        At<std::uint8_t>(self, off::kCameraHold) = 1;
        break;
    case 0xEA298D50: // slot 57 with slot 0x28's
        Virtual<std::uint8_t>(self, 0xE4, Virtual<void*>(message, 0x28));
        break;
    case 0xEAD61E16:
        At<std::uint8_t>(self, off::kCameraHold) = 0;
        Virtual<std::uint8_t>(self, 0xC0);
        break;
    case 0xEBFF18A3: { // slot 59 with the automaton interface of slot 0x28's
        void* const object = Virtual<void*>(message, 0x28);
        void* automaton = nullptr;
        if (!object)
            break;
        void* const out = CallThis<void*>(addr::SC4RefPtr_Reset, static_cast<void*>(&automaton));
        if (Virtual<std::uint8_t>(object, 0x00, kAutomatonIid, out))
            Virtual<std::uint8_t>(self, 0xEC, automaton);
        if (automaton)
            Release(automaton);
        break;
    }
    default:
        break;
    }
    return 1;
}

// The game's SC4Tokenizer, 0x3C bytes on the stack.
struct Tokenizer {
    alignas(4) char bytes[0x3C];
    explicit Tokenizer(const char* text) { CallThis<void*>(addr::SC4Tokenizer_SC4Tokenizer, bytes, text); }
    ~Tokenizer() { CallThis<void>(addr::SC4Tokenizer_Destroy, bytes); }
    std::int32_t Count() { return CallThis<std::int32_t>(addr::SC4Tokenizer_Count, bytes); }
    char* Word(std::uint32_t i) { return Virtual<char*>(bytes, 0x40, i); }
};

// 0x00542F50, the cheat's route line object's constructor (0x1C bytes):
// vtables 0x00A99F48 and 0x00A99F30 (over cRZBaseUnknown at +4), +0xC and
// +0xD zero, +0x10 and +0x14 zero, +0x18 -1.
char* __fastcall RouteLineConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("RouteLineConstruct", logged);
    At<std::uintptr_t>(self, 0) = 0x00A881C0;
    CallThis<void*>(addr::cRZBaseUnknown_Construct, self + 4);
    At<std::uint8_t>(self, 0xC) = 0;
    At<std::uint8_t>(self, 0xD) = 0;
    At<std::uintptr_t>(self, 0) = 0x00A99F48;
    At<std::uintptr_t>(self, 4) = 0x00A99F30;
    At<std::uint32_t>(self, 0x10) = 0;
    At<std::uint32_t>(self, 0x14) = 0;
    At<std::uint32_t>(self, 0x18) = 0xFFFFFFFFu;
    return self;
}

// 0x00542EE0, the route line's Prepare: once (+0xC), with the view
// (0x00B43DD0) its slot 0x28 and the render state at +0x10 (14 values, two
// sets by +0xD). AL 1.
std::uint8_t __fastcall RouteLinePrepare(char* self, void*)
{
    static bool logged;
    FirstCall("RouteLinePrepare", logged);
    if (At<std::uint8_t>(self, 0xC))
        return 1;
    void* const view = G<void*>(addr::SC4_View43DD0);
    if (view) {
        Virtual<void>(view, 0x28);
        using Init = void(__thiscall*)(void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t,
                                       std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t,
                                       std::int32_t, std::int32_t, std::int32_t, std::int32_t);
        const auto init = reinterpret_cast<Init>(addr::SC4RenderState_Init);
        if (At<std::uint8_t>(self, 0xD))
            init(self + 0x10, 1, 0, 0, 0, 1, 0, 0, -1, 4, 5, 4, 0, 1, -1);
        else
            init(self + 0x10, 1, 0, 1, 0, 1, 0, 0, 3, 4, 5, 4, 0, 1, -1);
    }
    At<std::uint8_t>(self, 0xC) = 1;
    return 1;
}

// 0x0050DD30, the +4 part's slot 3: the cheat codes (message 0x230E27AC,
// the cheat at +4, its text at +8; this not read). 0xE97FCBC8 makes and
// shows the route line (0x00B21BA0) with the second word as its count (-1
// without one); 0xE97FCBC9 takes it away; 0x0B70C96D with the word of
// 0x00A8622C runs the city's slot 0xC4 object's slot 0x84, else parks
// (0x00506850 with nulls); 0xCC478B79 sets 0x00B09968 to whether the word
// is not 0x00A82B94's; 0x6BB354C7 the dispatch service's slot 0x58 object's
// 0x0046D3A0 (4, 0). AL 1, or 0 for other messages and cheats.
std::uint8_t __fastcall DoCheat(void*, void*, std::uint32_t* message)
{
    static bool logged;
    FirstCall("DoCheat", logged);
    if (message[0] != 0x230E27AC)
        return 0;
    const std::uint32_t cheat = message[1];
    void* const text = reinterpret_cast<void*>(static_cast<std::uintptr_t>(message[2]));
    void*& line = G<void*>(g::kRouteLine);
    switch (cheat) {
    case 0xE97FCBC8: {
        Tokenizer words(Virtual<const char*>(text, 0x14));
        if (!line) {
            void* const view = G<void*>(addr::SC4_View43DD0);
            void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(0x1C));
            void* made = block ? CallThis<void*>(addr::cSC4AutomataManager_RouteLineConstruct, block) : nullptr;
            line = made;
            Virtual<std::uint32_t>(made, 0x04);
            CallThis<std::uint8_t>(addr::cSC4AutomataManager_RouteLinePrepare, line);
            CallThis<void>(addr::SC4UI_MapOverlay_SetFlagD, line, static_cast<std::uint32_t>(1));
            Virtual<void>(view, 0x80, line, static_cast<std::uint32_t>(5), static_cast<std::uint32_t>(1000));
        }
        if (words.Count() >= 2) {
            char* const word = words.Word(1);
            const std::int32_t count = CallCdecl<std::int32_t>(addr::SC4_atol, Virtual<const char*>(word, 0x14));
            At<std::int32_t>(G<void*>(g::kRouteLine), 0x18) = count;
        } else {
            At<std::uint32_t>(G<void*>(g::kRouteLine), 0x18) = 0xFFFFFFFFu;
        }
        return 1;
    }
    case 0xE97FCBC9:
        if (line) {
            Virtual<void>(G<void*>(addr::SC4_View43DD0), 0x84, line);
            CallThis<void>(addr::SC4RouteLine_Unprepare, line);
            Release(line);
            line = nullptr;
        }
        return 1;
    case 0x0B70C96D: {
        Tokenizer words(Virtual<const char*>(text, 0x14));
        if (words.Count() >= 2) {
            char* const word = words.Word(1);
            if (CallCdecl<std::uint8_t>(addr::SC4String_Equals, static_cast<const void*>(word ? word + 4 : nullptr),
                                        reinterpret_cast<const char*>(0x00A8622C))) {
                if (void* const object = Virtual<void*>(G<void*>(addr::SC4_City), 0xC4))
                    Virtual<void>(object, 0x84);
                return 1;
            }
        }
        CallCdecl<void>(addr::SC4_ParkAutomaton, static_cast<void*>(nullptr), static_cast<void*>(nullptr));
        return 1;
    }
    case 0xCC478B79: {
        Tokenizer words(Virtual<const char*>(text, 0x14));
        std::uint8_t off = 1;
        if (words.Count() >= 2) {
            char* const word = words.Word(1);
            const std::uint8_t equal = CallCdecl<std::uint8_t>(
                addr::SC4String_Equals, static_cast<const void*>(word ? word + 4 : nullptr),
                reinterpret_cast<const char*>(0x00A82B94));
            off = equal ? 0 : 1;
        }
        G<std::uint8_t>(g::kParkCheatOff) = off;
        return 1;
    }
    case 0x6BB354C7:
        if (void* const service = G<void*>(addr::SC4_DispatchService))
            if (void* const object = Virtual<void*>(service, 0x58))
                CallThis<void>(addr::SC4OverlayManager_F46D3A0, object, static_cast<std::uint32_t>(4),
                               static_cast<std::uint32_t>(0));
        return 1;
    default:
        return 0;
    }
}

// ====================================================================================
// The save (simulators.automata.awmgr.serial)

// 0x0050CB80, the serializable part's (+8) slot 3 Write (stream), ret 4:
// version 6, the most (+0x3C), the bools +0x4C and +0xA3D, the count
// (+0x48), a reference per automaton of the list, the source building
// table's count and per node its key and count. AL 1.
std::uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<std::uint8_t>(stream, 0x1C, static_cast<std::uint32_t>(6));
    Virtual<std::uint8_t>(stream, 0x20, At<std::uint32_t>(self, field::kMaxCount));
    CallCdecl<void*>(addr::SC4Stream_WriteBool, stream, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kActive)));
    CallCdecl<void*>(addr::SC4Stream_WriteBool, stream, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kA3D)));
    Virtual<std::uint8_t>(stream, 0x24, At<std::uint32_t>(self, field::kCount));
    char* const head = At<char*>(self, field::kAutomata);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kAutomata); node = At<char*>(node, 0))
        CallCdecl<std::uint8_t>(addr::SC4Stream_WriteReference, stream, At<void*>(node, 8));
    char* const t = self + field::kSourceCounts;
    Virtual<std::uint8_t>(stream, 0x24, At<std::uint32_t>(t, table::kCount));
    // The first node: the first non-empty bucket's.
    char* node = nullptr;
    for (std::uint32_t i = 0;
         i < static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin)); ++i)
        if ((node = At<char**>(t, table::kBegin)[i]) != nullptr)
            break;
    while (node) {
        const std::uint32_t count = At<std::uint32_t>(node, 8);
        Virtual<std::uint8_t>(stream, 0x24, At<std::uint32_t>(node, 4));
        Virtual<std::uint8_t>(stream, 0x24, count);
        if (char* const next = At<char*>(node, 0)) {
            node = next;
            continue;
        }
        std::uint32_t i = Bucket(t, At<std::uint32_t>(node, 4));
        node = nullptr;
        while (++i < static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin)))
            if ((node = At<char**>(t, table::kBegin)[i]) != nullptr)
                break;
    }
    return 1;
}

// 0x0050FEB0, Read's body (stream, version), ret 8, on the object: 0 for a
// version above 6. +0x10 set; the most, the two bools and the count; the
// list's nodes freed (their automata not released) and a node per reference
// read (interface 0xA9B40F05) appended; from version 5 the source building
// table cleared and read; +0xA78 whether the version is below 6 (1 up to
// version 4). AL 1.
std::uint8_t __fastcall ReadBody(char* self, void*, void* stream, std::uint32_t version)
{
    static bool logged;
    FirstCall("ReadBody", logged);
    if (static_cast<std::uint16_t>(version) > 6)
        return 0;
    At<std::uint8_t>(self, off::kLoaded) = 1;
    Virtual<std::uint8_t>(stream, 0x20, static_cast<void*>(&At<std::uint32_t>(self, field::kMaxCount)));
    CallCdecl<std::uint8_t>(addr::SC4Stream_ReadBool, stream, static_cast<void*>(&At<std::uint8_t>(self, field::kActive)));
    CallCdecl<std::uint8_t>(addr::SC4Stream_ReadBool, stream, static_cast<void*>(&At<std::uint8_t>(self, field::kA3D)));
    Virtual<std::uint8_t>(stream, 0x24, static_cast<void*>(&At<std::uint32_t>(self, field::kCount)));
    char* const head = At<char*>(self, field::kAutomata);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kAutomata);) {
        char* const next = At<char*>(node, 0);
        Free(node);
        node = next;
    }
    At<char*>(At<char*>(self, field::kAutomata), 0) = At<char*>(self, field::kAutomata);
    At<char*>(At<char*>(self, field::kAutomata), 4) = At<char*>(self, field::kAutomata);
    // The game's stack slot of the stream argument: each reference is read
    // into it (zeroed first), and the table's count after them.
    std::uintptr_t slot = reinterpret_cast<std::uintptr_t>(stream);
    std::uint32_t i = 0; // the loop counter's slot, the table key's after
    for (; i < At<std::uint32_t>(self, field::kCount); ++i) {
        slot = 0;
        CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kAutomatonIid, static_cast<void*>(&slot),
                                static_cast<std::uint32_t>(0));
        void* const automaton = reinterpret_cast<void*>(slot);
        char* const list = At<char*>(self, field::kAutomata);
        char* const node = static_cast<char*>(Allocate(0xC));
        if (node + 8)
            At<void*>(node, 8) = automaton;
        char* const last = At<char*>(list, 4);
        At<char*>(node, 0) = list;
        At<char*>(node, 4) = last;
        At<char*>(last, 0) = node;
        At<char*>(list, 4) = node;
    }
    if (static_cast<std::uint16_t>(version) <= 4) {
        At<std::uint8_t>(self, off::kOldSave) = 1;
        return 1;
    }
    // The count goes into the stream argument's slot: the last reference
    // read, or the stream's address when there were none. The key starts as
    // the reference loop's counter; the value's slot is never set in the
    // game (stack leftovers, ours 0). Both keep their values from one
    // record to the next, as the game's slots do.
    std::uint32_t count = static_cast<std::uint32_t>(slot);
    Virtual<std::uint8_t>(stream, 0x24, static_cast<void*>(&count));
    char* const t = self + field::kSourceCounts;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, t);
    std::uint32_t key = i, value = 0;
    for (std::uint32_t r = 0; r < count; ++r) {
        Virtual<std::uint8_t>(stream, 0x24, static_cast<void*>(&key));
        Virtual<std::uint8_t>(stream, 0x24, static_cast<void*>(&value));
        std::uint32_t pair[2] = {key, value};
        CallThis<void>(addr::SC4Uint32HashSet_Resize, t, At<std::uint32_t>(t, table::kCount) + 1);
        alignas(4) char result[12] = {};
        CallThis<void*>(addr::SC4Uint32HashMap_Insert, t, static_cast<void*>(result), static_cast<const void*>(pair));
    }
    At<std::uint8_t>(self, off::kOldSave) = static_cast<std::uint16_t>(version) < 6 ? 1 : 0;
    return 1;
}

// 0x005108E0, the serializable part's (+8) slot 4 Read (stream), ret 4: the
// version (a word read into the stack slot of the stream argument) and
// 0x0050FEB0 on the object.
std::uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("Read", logged);
    std::uint32_t version = reinterpret_cast<std::uintptr_t>(stream);
    Virtual<std::uint8_t>(stream, 0x1C, static_cast<void*>(&version));
    return CallThis<std::uint8_t>(addr::cSC4AutomataManager_ReadBody, part - field::kSerializable, stream, version);
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
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Init, Init),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Activate, Activate),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_SetMaxCount, SetMaxCount),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ReleaseAutomatonSet, ReleaseAutomatonSet),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_TrackAutomataGroupInstances, TrackAutomataGroupInstances),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_EraseInstances, EraseInstances),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_FreeInstanceNode, FreeInstanceNode),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_InsertInstances, InsertInstances),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_RegisterCheats, RegisterCheats),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_LoadExemplars, LoadExemplars),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_CopyExemplars, CopyExemplars),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_CopyExemplar, CopyExemplar),
};

const Detour kTuning[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataManager_LoadTuning, LoadTuning),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_LoadKindCurve, LoadKindCurve),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_LoadTuningRecord, LoadTuningRecord),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_InsertKindCurve, InsertKindCurve),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_InsertTuningRecord, InsertTuningRecord),
};

const Detour kMessages[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataManager_DoMessage, DoMessage),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_DoMessage4, DoCheat),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_RouteLinePrepare, RouteLinePrepare),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_RouteLineConstruct, RouteLineConstruct),
};

const Detour kSerial[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Write, Write),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Read, Read),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ReadBody, ReadBody),
};

#undef OPENSC4_DETOUR

bool InstallLife(registry::Mode) { return InstallList(kLife, sizeof kLife / sizeof kLife[0], "life"); }
bool InstallTuning(registry::Mode) { return InstallList(kTuning, sizeof kTuning / sizeof kTuning[0], "tuning"); }
bool InstallMessages(registry::Mode)
{
    return InstallList(kMessages, sizeof kMessages / sizeof kMessages[0], "messages");
}
bool InstallSerial(registry::Mode) { return InstallList(kSerial, sizeof kSerial / sizeof kSerial[0], "serial"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.awmgr.life", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataManager_Init,
                        "The common automata manager base's life cycle: Init, Shutdown, Activate, SetMaxCount, "
                        "ReleaseAutomatonSet, TrackAutomataGroupInstances with the group instance table's erase, "
                        "node release and insert, slot 39 (cheats and the tuning load once) and slot 66 (the "
                        "exemplar data) with the record copy and its range (13 detours; part aw2, "
                        "src/core/automata_awmgr_life.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.automata.awmgr.tuning", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataManager_LoadTuning,
                        "The common automata manager base's tuning load 0x00512CF0 with the kind curve table's "
                        "and the tuning map's fillers and inserts (5 detours; part aw2, "
                        "src/core/automata_awmgr_life.cpp)",
                        false, InstallTuning}),
    registry::Register({"simulators.automata.awmgr.messages", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataManager_DoMessage,
                        "The common automata manager base's messages: DoMessage, the cheat target (+4 slot 3) and "
                        "its route line's constructor and Prepare (4 detours; part aw2, "
                        "src/core/automata_awmgr_life.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.automata.awmgr.serial", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataManager_Write,
                        "The common automata manager base's save: Write, Read and its body (3 detours; part aw2, "
                        "src/core/automata_awmgr_life.cpp)",
                        false, InstallSerial}),
};

} // namespace
} // namespace opensc4::automata_awmgr
