// Our aircraft manager's behaviour, part aw4 (build 641): its automata, life
// cycle, tuning, the two dispatch slots, save and messages. The class map is
// in src/core/automata_awmgr.h and the survey in docs/automata.md, "Part aw"
// and "Part aw4". These are the aircraft manager's own slots (vtable
// 0x00A94850, the serializable part 0x00A94828, the message target
// 0x00A94818); each calls the common base's function of the slot at its game
// address, so the common base's detours (parts aw2, aw3) are reached when
// they are on. Calls between functions of the part go to the game's
// addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP (function
// addresses, hex, eight digits) bisects them.
//
// The aircraft manager's containers: the map at +0xA88 (header, size; 0x18-
// byte nodes, an occupant at +0x10 and its aircraft at +0x14, both
// referenced), the list at +0xA94 (0x18-byte nodes: a point at +8 and a byte
// at +0x14) and the occupant filter at +0xA98.
//
// Group, off by default:
//   simulators.automata.awmgr.air (11): AddAutomaton (slot 7), Init (3),
//     Shutdown (4), HandleTuningParametersChanged (24), slots 17 and 18 (the
//     two Dispatch), Write and Read (the serializable part's 3 and 4),
//     DoMessage (the message target's 3) with the occupant removed handler
//     0x004FB8D0 and its list find 0x004FB2B0.
//
// Oddities kept:
//   - AddAutomaton without an automaton makes a helicopter for a draw below
//     0x7FFFFFFF, else the 0x200-byte aircraft of 0x00523F60.
//   - Init references the filter without a null test of its allocation;
//     Shutdown releases the map's objects and the filter without null tests
//     and frees the list's nodes without destroying anything in them.
//   - Slot 17 picks the police simulator or the service 0x00B43D78 by whether
//     the automaton's occupant is in group 0x4301; slot 18 by the police
//     simulator's own test of the source. Slot 18 never reads its first
//     argument and takes the first map entry (in key order) whose occupant
//     is on the source's lot.
//   - DoMessage answers 1 for an occupant inserted whatever its type, else
//     the common base's answer; the common base's DoMessage runs for every
//     message.
//   - Write and Read answer 1 when the stream has no error (its slot 18), not
//     whether the common base's part worked; Read takes version 5 alone as
//     its own (a later version answers AL 0, an earlier one is the common
//     base's body alone, with the version read here).
//   - The occupant removed handler releases the occupant (the map's
//     reference) and then reads its property and its position.
//
// Floats: 53-bit doubles, as the game's x87 runs. The dispatch slots' height
// is the terrain's ST0 answer plus the float 200 (0x00A944D8), one sum
// rounded to the float the game stores; the list find compares the
// unrounded |difference| (ST0) with 0.0001 (0x00A944A0), strictly and false
// for unordered.
//
// Stack leftovers: Read reads the version (a word) into a dword slot it never
// writes, and passes the dword to 0x0050FEB0 for versions below 5; the top
// half is the caller's stack in the game, 0 in ours (0x0050FEB0 reads only
// the word). A failed version read leaves the whole slot the game's leftover,
// 0 (a version below 5) in ours. The list records' byte slot is never set
// before its read either: a failed byte read keeps the previous record's in
// both, the first record's a leftover in the game, 0 in ours. The counts are
// read into the stream argument's slot, as the game does (a failed first
// read leaves the stream's address).
//
// Return values: AL alone for Init, Shutdown and the tuning (the game leaves
// what its last call left above it); Write, Read (from version 5) and the
// failed allocation of AddAutomaton set EAX in full; AddAutomaton, Read
// below version 5 and DoMessage keep their last call's EAX above AL as the
// game does; the dispatch slots answer the automaton or 0.
//
// Still the game's, at their addresses: SC4UIScale's probe site 0x004FBFE0
// (the occupant inserted handler); the common base's functions; the
// helicopter's and the other aircraft's constructors; the map and tree
// helpers, the list node allocator, the stream helpers, the property getter
// and the allocators.
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
void Release(void* object) { Virtual<std::uint32_t>(object, 0x08); }

// The game's globals this part reads and writes.
namespace g {
inline constexpr std::uintptr_t kRandom = 0x00B21AF4;     // the aircraft manager's generator (+0x50), set by Init
inline constexpr std::uintptr_t kZero = 0x00B21AF8;       // float: the points' start
inline constexpr std::uintptr_t kRate = 0x00B098DC;       // word (a float, 1): +0x1E4's default
inline constexpr std::uintptr_t kLift = 0x00A944D8;       // float 200: above the terrain
inline constexpr std::uintptr_t kNear = 0x00A944A0;       // float 0.0001: the list find's tolerance
inline constexpr std::uintptr_t kMaxCount = 0x00B21BC8;   // word: the tuning's most automata
} // namespace g

inline constexpr std::uintptr_t kFilterVtable = 0x00A944BC, kFilterVtable2 = 0x00A944A4;
inline constexpr std::uint32_t kAutomatonIid = 0xA9B40F05, kOccupantIid = 0x26BB767E;
inline constexpr std::uint32_t kOccupantInserted = 0x99EF1142, kOccupantRemoved = 0x99EF1143;

namespace off {
inline constexpr std::size_t kRate = 0x1E4; // the aircraft's float the dispatch slots set
} // namespace off

// STLport's tree iterator ++ (_Rb_global::_M_increment): the right subtree's
// leftmost, else up while coming from the right; the header's case kept.
char* NextNode(char* x)
{
    if (char* right = At<char*>(x, tree::kRight)) {
        x = right;
        while (char* const left = At<char*>(x, tree::kLeft))
            x = left;
        return x;
    }
    char* y = At<char*>(x, tree::kParent);
    while (x == At<char*>(y, tree::kRight)) {
        x = y;
        y = At<char*>(y, tree::kParent);
    }
    if (At<char*>(x, tree::kRight) != y)
        x = y;
    return x;
}

// The terrain's height under (x, z) plus 200, as the dispatch slots set it:
// slot 13 on the map (slot 11), else slot 29; one sum rounded to the float.
float LiftedHeight(float x, float z)
{
    void* const terrain = G<void*>(addr::SC4_Terrain);
    double h;
    if (Virtual<std::uint8_t>(terrain, 0x2C, x, z))
        h = Virtual<double>(terrain, 0x34, x, z);
    else
        h = Virtual<double>(terrain, 0x74);
    return static_cast<float>(h + static_cast<double>(G<float>(g::kLift)));
}

// The dispatch slots' end on the automaton: slot 52 with 0x35 (10000) and
// 0x33 (-1) when `on`, else 0x39 (-1), the rest zero.
void SendStates(void* automaton, std::uint8_t on)
{
    const auto send = [&](std::uint32_t what, std::uint32_t value) {
        Virtual<void>(automaton, 0xD0, what, std::uint32_t{0}, value, std::uint32_t{0}, std::uint32_t{0},
                      std::uint32_t{0}, std::uint32_t{0});
    };
    if (on) {
        send(0x35, 0x461C4000); // 10000.0f
        send(0x33, 0xBF800000); // -1.0f
    } else {
        send(0x39, 0xBF800000);
    }
}

// ====================================================================================
// The aircraft manager (simulators.automata.awmgr.air)

// 0x004FA6D0, slot 7 AddAutomaton (automaton), ret 4: without one, a draw of
// the generator (+0x50) below 0x7FFFFFFF makes a helicopter (0x278 bytes,
// 0x0053C6E0), else the 0x200-byte aircraft (0x00523F60); none made answers
// EAX 0. Then the common base's AddAutomaton 0x0050F790, its EAX.
std::uint32_t __fastcall AirAddAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("AirAddAutomaton", logged);
    if (!automaton) {
        void* made;
        if (CallThis<std::uint32_t>(addr::cRZRandomSuper_Next, self + field::kRandom) < 0x7FFFFFFFu) {
            void* const block = CallCdecl<void*>(addr::SC4_operator_new, std::size_t{0x278});
            if (!block)
                return 0;
            made = CallThis<void*>(addr::cSC4Helicopter_Construct, block);
        } else {
            void* const block = CallCdecl<void*>(addr::SC4_operator_new, std::size_t{0x200});
            if (!block)
                return 0;
            made = CallThis<void*>(addr::cSC4AircraftB_Construct, block);
        }
        if (!made)
            return 0;
        automaton = made;
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_AddAutomaton, self, automaton);
}

// 0x004FA9E0, slot 3 Init: once (+0x34): the common base's Init 0x005066B0;
// the occupant filter (0xC bytes: SC4::OccupantFilterBase, vtables
// 0x00A944BC and 0x00A944A4) kept at +0xA98 and referenced without a null
// test; the generator's address into 0x00B21AF4; slot 24; +0x34 set. AL 1.
std::uint8_t __fastcall AirInit(char* self, void*)
{
    static bool logged;
    FirstCall("AirInit", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    CallThis<std::uint8_t>(addr::cSC4AutomataManager_Init, self);
    char* filter = static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, std::size_t{0xC}));
    if (filter) {
        CallThis<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0) = kFilterVtable;
        At<std::uintptr_t>(filter, 4) = kFilterVtable2;
    }
    At<char*>(self, field::kAirFilter) = filter;
    Virtual<std::uint32_t>(filter, 0x04);
    G<char*>(g::kRandom) = self + field::kRandom;
    Virtual<std::uint8_t>(self, 0x60);
    At<std::uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// 0x004FB7D0, slot 4 Shutdown: with +0x34, both objects of every map node
// (+0x10, +0x14) released without null tests, the map emptied (0x004FB270
// from the root, the header reset, the size zero), the list's nodes freed
// (their records not destroyed) and the head linked to itself, the filter
// released without a null test and cleared, the common base's Shutdown
// 0x005110A0, 0x00B21AF4 zero. AL 1.
std::uint8_t __fastcall AirShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("AirShutdown", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    char* const map = self + field::kAirMap;
    for (char* node = At<char*>(At<char*>(map, 0), tree::kLeft); node != At<char*>(map, 0);) {
        void* const value = At<void*>(node, tree::kValue);
        Release(At<void*>(node, tree::kKey));
        Release(value);
        node = NextNode(node);
    }
    if (At<std::uint32_t>(map, 4) != 0) {
        CallThis<void>(addr::cSC4AircraftManager_EraseTree, map, At<void*>(At<char*>(map, 0), tree::kParent));
        char* const header = At<char*>(map, 0);
        At<char*>(header, tree::kLeft) = header;
        At<char*>(At<char*>(map, 0), tree::kParent) = nullptr;
        At<char*>(At<char*>(map, 0), tree::kRight) = At<char*>(map, 0);
        At<std::uint32_t>(map, 4) = 0;
    }
    for (char* node = At<char*>(At<char*>(self, field::kAirList), 0); node != At<char*>(self, field::kAirList);) {
        char* const gone = node;
        node = At<char*>(node, 0);
        Free(gone);
    }
    At<char*>(At<char*>(self, field::kAirList), 0) = At<char*>(self, field::kAirList);
    At<char*>(At<char*>(self, field::kAirList), 4) = At<char*>(self, field::kAirList);
    Release(At<void*>(self, field::kAirFilter));
    At<void*>(self, field::kAirFilter) = nullptr;
    CallThis<std::uint8_t>(addr::cSC4AutomataManager_Shutdown, self);
    G<void*>(g::kRandom) = nullptr;
    return 1;
}

// 0x004FAA50, slot 24 HandleTuningParametersChanged: slot 23 (SetMaxCount)
// with the word at 0x00B21BC8; with the automata tuning exemplar
// (0x6534284A, 0xA998D30B, 1; interface 0xA52160F5) +0xA3C its byte property
// 0x4998C91A (1 without it) less one; the resource released. AL 1.
std::uint8_t __fastcall AirTuning(char* self, void*)
{
    static bool logged;
    FirstCall("AirTuning", logged);
    Virtual<void>(self, 0x5C, G<std::uint32_t>(g::kMaxCount));
    std::uint32_t key[3] = {0x6534284A, 0xA998D30B, 1};
    void* resource = nullptr;
    if (Virtual<std::uint8_t>(G<void*>(addr::SC4_ResourceManager), 0x0C, static_cast<void*>(key),
                              std::uint32_t{0xA52160F5}, static_cast<void*>(&resource), std::uint32_t{0},
                              std::uint32_t{0})) {
        void* const exemplar = Virtual<void*>(resource, 0x1C);
        std::uint8_t* const zoom = &At<std::uint8_t>(self, field::kMinZoom);
        *zoom = 1;
        CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint8, exemplar, std::uint32_t{0x4998C91A}, zoom);
        *zoom = static_cast<std::uint8_t>(*zoom - 1);
    }
    if (resource)
        Release(resource);
    return 1;
}

// 0x004FAAF0, slot 17 (automaton, from, to, a, b), ret 0x14: 0 without an
// automaton. The destination (slot 19 with `to`; 0 when it fails) put on the
// terrain plus 200 and given to the automaton (slot 10). The flag: the
// automaton's occupant (slot 5) in group 0x4301 (its slot 22). +0x1E4 the
// word at 0x00B098DC, or for an object source (kind 0, an object at +4) with
// the flag the police simulator's (slot 22 takes it, slot 25's answer), else
// the service 0x00B43D78's (slots 7 and 10). Slot 52 by the flag. Answers
// the automaton.
void* __fastcall AirSlot17(char* self, void*, char* automaton, const std::uint32_t* from, const void* to,
                           std::uint32_t, std::uint32_t)
{
    static bool logged;
    FirstCall("AirSlot17", logged);
    if (!automaton)
        return nullptr;
    const float zero = G<float>(g::kZero);
    float point[3] = {zero, zero, zero};
    if (!Virtual<std::uint8_t>(self, 0x4C, to, static_cast<void*>(point)))
        return nullptr;
    point[1] = LiftedHeight(point[0], point[2]);
    Virtual<void>(automaton, 0x28, static_cast<const void*>(point));
    void* const occupant = Virtual<void*>(automaton, 0x14);
    const std::uint8_t flag = Virtual<std::uint8_t>(occupant, 0x58, std::uint32_t{0x4301});
    std::uint32_t rate = G<std::uint32_t>(g::kRate);
    if (from[0] == 0) {
        void* const object = reinterpret_cast<void*>(static_cast<std::uintptr_t>(from[1]));
        if (object) {
            // The answer's slot is the `to` argument's in the game.
            std::uint32_t out = 0;
            if (flag) {
                void* const police = G<void*>(addr::SC4_PoliceSimulator);
                if (Virtual<std::uint8_t>(police, 0x58, object))
                    rate = *Virtual<const std::uint32_t*>(police, 0x64, static_cast<void*>(&out), object);
            } else {
                void* const service = G<void*>(addr::SC4_Service43D78);
                if (Virtual<std::uint8_t>(service, 0x1C, object))
                    rate = *Virtual<const std::uint32_t*>(service, 0x28, static_cast<void*>(&out), object);
            }
        }
    }
    At<std::uint32_t>(automaton, off::kRate) = rate;
    SendStates(automaton, flag);
    return automaton;
}

// 0x004FC440, slot 18 (unused, from, to, a, b, c, d), ret 0x1C: 0 unless the
// source is an object (kind 0, an object at +4). The flag: the police
// simulator takes it (slot 22; +0x1E4 its slot 25's answer), else the
// service 0x00B43D78's slots 7 and 10 give +0x1E4 (the word at 0x00B098DC
// without). The object's lot (lot manager slot 14; 0 without) lists its
// occupants (slot 49, a list of the allocator 0x00B207E4); the first map
// node (in order) whose occupant is in the list gives the aircraft; the list
// released (network service slot 48) and destroyed. 0 without one, or when
// slot 19 fails; else the destination on the terrain plus 200 (slot 10),
// +0x1E4, slot 52 by the flag. Answers the aircraft.
void* __fastcall AirSlot18(char* self, void*, std::uint32_t, const std::uint32_t* from, const void* to,
                           std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)
{
    static bool logged;
    FirstCall("AirSlot18", logged);
    std::uint32_t rate = G<std::uint32_t>(g::kRate);
    std::uint8_t flag = 0;
    if (from[0] != 0)
        return nullptr;
    void* const object = reinterpret_cast<void*>(static_cast<std::uintptr_t>(from[1]));
    if (!object)
        return nullptr;
    {
        // The answer's slot is the `from` argument's in the game.
        std::uint32_t out = 0;
        void* const police = G<void*>(addr::SC4_PoliceSimulator);
        if (Virtual<std::uint8_t>(police, 0x58, object)) {
            flag = 1;
            rate = *Virtual<const std::uint32_t*>(police, 0x64, static_cast<void*>(&out), object);
        } else {
            void* const service = G<void*>(addr::SC4_Service43D78);
            if (Virtual<std::uint8_t>(service, 0x1C, object))
                rate = *Virtual<const std::uint32_t*>(service, 0x28, static_cast<void*>(&out), object);
        }
    }
    void* const lot = Virtual<void*>(G<void*>(addr::SC4_LotManager), 0x38, object);
    if (!lot)
        return nullptr;
    // The game's list object is the `from` argument's stack slot; `to` is the
    // slot after it.
    struct {
        char* head;
        const void* to;
    } list = {nullptr, to};
    if (!G<void*>(addr::SC4List_NodeAllocator2)) {
        void* allocator = nullptr;
        if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
            Virtual<std::uint8_t>(framework, 0x14, std::uint32_t{0x3AE4BEA3}, std::uint32_t{0x3AE4BEAB},
                                  static_cast<void*>(&allocator));
        G<void*>(addr::SC4List_NodeAllocator2) = allocator;
        CallThis<void>(addr::SC4_ReleaseRef, static_cast<void*>(&allocator));
    }
    char* const head = Virtual<char*>(G<void*>(addr::SC4List_NodeAllocator2), 0x0C, std::uint32_t{0xC});
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    list.head = head;
    Virtual<void>(lot, 0xC4, static_cast<void*>(&list));
    char* aircraft = nullptr;
    char* const header = At<char*>(self, field::kAirMap);
    char* it = At<char*>(header, tree::kLeft);
    while (it != header) {
        const std::uint32_t key = At<std::uint32_t>(it, tree::kKey);
        char* const value = At<char*>(it, tree::kValue);
        char* const end = list.head;
        char* n = At<char*>(end, 0);
        while (n != end && At<std::uint32_t>(n, 8) != key)
            n = At<char*>(n, 0);
        if (n != end) {
            aircraft = value;
            break;
        }
        CallThis<void*>(addr::SC4MapIterator_Next, static_cast<void*>(&it));
    }
    Virtual<void>(G<void*>(addr::SC4_NetworkService), 0xC0, static_cast<void*>(&list));
    CallThis<void>(addr::SC4List_Destroy2, static_cast<void*>(&list));
    if (!aircraft)
        return nullptr;
    const float zero = G<float>(g::kZero);
    float point[3] = {zero, zero, zero};
    if (!Virtual<std::uint8_t>(self, 0x4C, list.to, static_cast<void*>(point)))
        return nullptr;
    point[1] = LiftedHeight(point[0], point[2]);
    Virtual<void>(aircraft, 0x28, static_cast<const void*>(point));
    At<std::uint32_t>(aircraft, off::kRate) = rate;
    SendStates(aircraft, flag);
    return aircraft;
}

// 0x004FB5B0, the serializable part's (+8) slot 3 Write (stream), ret 4:
// version 5 and the common base's Write 0x0050CB80; when that worked, the
// map's size and per node (in order) its key's and value's references, the
// list's count and per node its point (0x005DA580) and its byte (slot 5,
// as a word). EAX 1 when the stream's slot 18 answers 0, else 0.
std::uint32_t __fastcall AirWrite(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("AirWrite", logged);
    char* const self = part - field::kSerializable;
    Virtual<std::uint8_t>(stream, 0x1C, std::uint32_t{5});
    if (CallThis<std::uint8_t>(addr::cSC4AutomataManager_Write, part, stream)) {
        Virtual<std::uint8_t>(stream, 0x24, At<std::uint32_t>(self, field::kAirMapSize));
        for (char* node = At<char*>(At<char*>(self, field::kAirMap), tree::kLeft);
             node != At<char*>(self, field::kAirMap); node = NextNode(node)) {
            void* const value = At<void*>(node, tree::kValue);
            CallCdecl<std::uint8_t>(addr::SC4Stream_WriteReference, stream, At<void*>(node, tree::kKey));
            CallCdecl<std::uint8_t>(addr::SC4Stream_WriteReference, stream, value);
        }
        char* const head = At<char*>(self, field::kAirList);
        std::uint32_t count = 0;
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
            ++count;
        Virtual<std::uint8_t>(stream, 0x24, count);
        for (char* node = At<char*>(At<char*>(self, field::kAirList), 0); node != At<char*>(self, field::kAirList);
             node = At<char*>(node, 0)) {
            char* const record = node + 8;
            CallCdecl<void*>(addr::SC4Stream_WriteVector3, stream, static_cast<const float*>(&At<float>(record, 0)));
            Virtual<std::uint8_t>(stream, 0x14, static_cast<std::uint32_t>(At<std::uint8_t>(record, 0xC)));
        }
    }
    return Virtual<std::uint8_t>(stream, 0x48) ? 0u : 1u;
}

// 0x004FBE70, the serializable part's (+8) slot 4 Read (stream), ret 4: the
// version (a word); below 5 the common base's body 0x0050FEB0 on the object
// with it (its EAX); above 5 AL 0. Version 5: the common base's Read
// 0x005108E0; when that worked, the map's count and per record two
// references (0x26BB767E, 0xA9B40F05; zero first) stored by 0x00497A00, then
// the list's count and per record a point (zero first, 0x005DA550) and a
// byte (slot 5) in a new 0x18-byte node at the list's end. EAX 1 when the
// stream's slot 18 answers 0, else 0.
std::uint32_t __fastcall AirRead(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("AirRead", logged);
    // A dword slot of which the read sets the word (the rest a leftover in
    // the game, 0 in ours).
    std::uint32_t version = 0;
    Virtual<std::uint8_t>(stream, 0x1C, static_cast<void*>(&version));
    const std::uint16_t word = static_cast<std::uint16_t>(version);
    if (word < 5)
        return CallThis<std::uint32_t>(addr::cSC4AutomataManager_ReadBody, part - field::kSerializable, stream,
                                       version);
    if (word > 5)
        return version & ~0xFFu;
    if (CallThis<std::uint8_t>(addr::cSC4AutomataManager_Read, part, stream)) {
        // The counts go into the stream argument's slot.
        std::uint32_t count = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
        Virtual<std::uint8_t>(stream, 0x24, static_cast<void*>(&count));
        char* const map = part + (field::kAirMap - field::kSerializable);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t key = 0, value = 0;
            CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kOccupantIid, static_cast<void*>(&key),
                                    std::uint32_t{0});
            CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, stream, kAutomatonIid, static_cast<void*>(&value),
                                    std::uint32_t{0});
            *CallThis<std::uint32_t*>(addr::SC4Uint32Map_At, map, static_cast<const std::uint32_t*>(&key)) = value;
        }
        Virtual<std::uint8_t>(stream, 0x24, static_cast<void*>(&count));
        // The byte's slot is never set before its read (a leftover in the
        // game, 0 in ours); it keeps its value from one record to the next.
        std::uint8_t byte = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            const float zero = G<float>(g::kZero);
            float point[3] = {zero, zero, zero};
            CallCdecl<void*>(addr::SC4Stream_ReadVector3, stream, static_cast<float*>(point));
            Virtual<std::uint8_t>(stream, 0x14, static_cast<void*>(&byte));
            char* const list = At<char*>(part, field::kAirList - field::kSerializable);
            char* const node = static_cast<char*>(Allocate(0x18));
            if (char* const record = node + 8) {
                std::memcpy(record, point, sizeof point); // as words, as the game copies them
                At<std::uint8_t>(record, 0xC) = byte;
            }
            char* const last = At<char*>(list, 4);
            At<char*>(node, 0) = list;
            At<char*>(node, 4) = last;
            At<char*>(last, 0) = node;
            At<char*>(list, 4) = node;
        }
    }
    return Virtual<std::uint8_t>(stream, 0x48) ? 0u : 1u;
}

// 0x004FB2B0 (result, first, last, record), cdecl: std::find_if over the
// list's nodes [first, last): the first whose byte (+0x14) is the record's
// (+0xC) and whose point (+8) is within 0.0001 of the record's (+0) on each
// axis (|a - b| < 0.0001, false unordered); last for none. Answers result.
char** __cdecl AirFindRecord(char** result, char* first, char* last, const char* record)
{
    static bool logged;
    FirstCall("AirFindRecord", logged);
    char* n = first;
    if (n != last) {
        const std::uint8_t byte = At<std::uint8_t>(const_cast<char*>(record), 0xC);
        const double tolerance = static_cast<double>(G<float>(g::kNear));
        const auto close = [&](std::size_t at, std::size_t to) {
            const double d = static_cast<double>(At<float>(n, at)) -
                             static_cast<double>(At<float>(const_cast<char*>(record), to));
            return std::fabs(d) < tolerance;
        };
        do {
            if (At<std::uint8_t>(n, 0x14) == byte && close(0x8, 0x0) && close(0xC, 0x4) && close(0x10, 0x8))
                break;
            n = At<char*>(n, 0);
        } while (n != last);
    }
    *result = n;
    return result;
}

// 0x004FB8D0 (occupant), ret 4: the occupant's map node (0x00497520): its
// aircraft told slot 13 (0), the node unlinked (SC4Tree::RebalanceForErase)
// and freed, the size down, the aircraft and the occupant released. Then the
// occupant's byte property 0x8BFB1072 (0 without; the occupant argument's
// slot in the game): when not 0, the list's record of the occupant's
// position (slot 8; zero first) and that byte (0x004FB2B0) unlinked and
// freed.
void __fastcall AirOccupantRemoved(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("AirOccupantRemoved", logged);
    char* const map = self + field::kAirMap;
    char* it = nullptr;
    CallThis<void>(addr::SC4Uint32Map_Find, map, static_cast<void*>(&it), static_cast<const void*>(&occupant));
    if (it != At<char*>(map, 0)) {
        void* const aircraft = At<void*>(it, tree::kValue);
        Virtual<void>(aircraft, 0x34, std::uint32_t{0});
        char* const header = At<char*>(map, 0);
        void* const gone = CallCdecl<void*>(addr::SC4Tree_RebalanceForErase, static_cast<void*>(it),
                                            static_cast<void*>(header + tree::kParent),
                                            static_cast<void*>(header + tree::kLeft),
                                            static_cast<void*>(header + tree::kRight));
        if (gone)
            Free(gone);
        --At<std::uint32_t>(map, 4);
        Release(aircraft);
        Release(occupant);
    }
    std::uint8_t byte = 0;
    void* const holder = Virtual<void*>(occupant, 0x18);
    CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint8, holder, std::uint32_t{0x8BFB1072}, &byte);
    if (!byte)
        return;
    // The record: a point and the byte at +0xC.
    alignas(4) char record[0x10] = {};
    const float zero = G<float>(g::kZero);
    At<float>(record, 0) = zero;
    At<float>(record, 4) = zero;
    At<float>(record, 8) = zero;
    Virtual<void>(occupant, 0x20, static_cast<void*>(record));
    At<std::uint8_t>(record, 0xC) = byte;
    char* const head = At<char*>(self, field::kAirList);
    char* found = nullptr;
    // The fifth word is the byte's slot, which 0x004FB2B0 does not read.
    CallCdecl<char**>(addr::cSC4AircraftManager_FindRecord, &found, At<char*>(head, 0), head,
                      static_cast<const void*>(record), static_cast<void*>(&byte));
    if (found == At<char*>(self, field::kAirList))
        return;
    char* const next = At<char*>(found, 0);
    char* const prev = At<char*>(found, 4);
    At<char*>(prev, 0) = next;
    At<char*>(next, 4) = prev;
    Free(found);
}

// 0x004FC710, the message target's (+0xC) slot 3 DoMessage (message), ret 4:
// 0x99EF1142 with an occupant (slot 10) whose type (slot 6's slot 3) is not
// 0x029244DB goes to 0x004FBFE0 (SC4UIScale's probe site) and answers 1
// whatever the type; 0x99EF1143 the same test to 0x004FB8D0. Then the common
// base's DoMessage 0x00506970: AL 1 when it or the 0x99EF1142 case answers,
// else its EAX.
std::uint32_t __fastcall AirDoMessage(char* part, void*, void* message)
{
    static bool logged;
    FirstCall("AirDoMessage", logged);
    char* const self = part - field::kMessage;
    std::uint8_t answered = 0;
    const std::uint32_t type = Virtual<std::uint32_t>(message, 0x10);
    if (type == kOccupantInserted || type == kOccupantRemoved) {
        void* const occupant = Virtual<void*>(message, 0x28);
        void* const kind = Virtual<void*>(occupant, 0x18);
        if (!Virtual<std::uint8_t>(kind, 0x0C, std::uint32_t{0x029244DB}))
            CallThis<void>(type == kOccupantInserted ? addr::cSC4AircraftManager_OccupantInserted
                                                     : addr::cSC4AircraftManager_OccupantRemoved,
                           self, occupant);
        if (type == kOccupantInserted)
            answered = 1;
    }
    const std::uint32_t base = CallThis<std::uint32_t>(addr::cSC4AutomataManager_DoMessage, part, message);
    if ((base & 0xFF) == 0 && !answered)
        return base;
    return (base & ~0xFFu) | 1u;
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

const Detour kAir[] = {
    OPENSC4_DETOUR(addr::cSC4AircraftManager_AddAutomaton, AirAddAutomaton),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Init, AirInit),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Shutdown, AirShutdown),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_HandleTuningParametersChanged, AirTuning),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Slot17, AirSlot17),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Slot18, AirSlot18),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Write, AirWrite),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Read, AirRead),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_DoMessage, AirDoMessage),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_OccupantRemoved, AirOccupantRemoved),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_FindRecord, AirFindRecord),
};

#undef OPENSC4_DETOUR

bool InstallAir(registry::Mode) { return InstallList(kAir, sizeof kAir / sizeof kAir[0], "air"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.awmgr.air", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AircraftManager_AddAutomaton,
                        "The aircraft manager's behaviour: AddAutomaton, Init, Shutdown, "
                        "HandleTuningParametersChanged, slots 17 and 18 (the two Dispatch), Write, Read and "
                        "DoMessage with the occupant removed handler and its list find (11 detours; part aw4, "
                        "src/core/automata_awmgr_air.cpp)",
                        false, InstallAir}),
};

} // namespace
} // namespace opensc4::automata_awmgr
