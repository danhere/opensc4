// Our common automata manager base, part aw3 (build 641): its automata list,
// creation, dispatch, chase, exemplar choices, camera follow and frames. The
// class map is in src/core/automata_awmgr.h and the survey in
// docs/automata.md, "Part aw" and "Part aw3". The common base is also the
// vehicle and pedestrian managers' base: their vtables keep most of these
// slots, and part v2 and v3 call AddAutomaton, RemoveAutomaton,
// GetRandomCreationPoint and slots 55, 56 and 63 at their game addresses, so
// all four managers reach these detours when they are on. Calls between
// functions of the part go through the vtable or to the game's addresses,
// as the game's do, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.awmgr.create (14): AddAutomaton (slot 7),
//     RemoveAutomaton (8), GetRandomCreationPoint (16), the exemplar choices
//     (25, 26, 27), GetInstancesOfGroup (32), slot 40 (the cheats' release),
//     slot 43 (a message posted), slot 45 (every automaton removed), slot 46
//     (an automaton made), slot 47 (room by priority), slot 65 (the source
//     building table filled) and slot 67 (an exemplar record read).
//   simulators.automata.awmgr.dispatch (12): slot 19
//     (TranslateDispatchLocation, a point), 20 (a cell rectangle), 21
//     (ChaseAutomaton), 22 (the chase itself), slot 41 (in view), 42 (the
//     kind rate), 53 (an occupant's source buildings counted), 54 (a
//     building occupant's), 55 (an occupant gone), 59 (an automaton tracked),
//     61 and 62 (shown and hidden).
//   simulators.automata.awmgr.frame (10): SetCameraFollow (34), slots 48 (the
//     view rectangle), 49, 50 (zoom and filter), 56 (after a load), 57, 60
//     (active by zoom), the frame update (63) with its rate 0x00507BF0, and
//     the camera follow's update (64).
//
// Oddities kept:
//   - AddAutomaton and RemoveAutomaton walk the group instance table and put
//     the automaton in (or take it out of) every tracked group its occupant
//     is in; AddAutomaton's null node test is on the node's +8 (never null).
//   - RemoveAutomaton with its byte 0 only marks the automaton (slots 102,
//     89, 33 and its +0x18C part) and keeps it in the list.
//   - GetRandomCreationPoint draws the distance as ((r x most) - least) +
//     least; the x of the direction is used unrounded, y and z after their
//     rounding to floats; y is 0 without flag 0x40000, else a draw in [0, 1)
//     not scaled; with the flag the height is not the terrain's.
//   - Slot 25 tests its bounds for -1 and up (signed) and draws from the map's
//     own vector when no filter applies; slot 27's value 0 takes every record.
//   - Slot 21 with its byte 0 chases the nearest; with it, every one within
//     the radius, and answers 1 when any chased (not slot 22's answers).
//   - Slot 22 sets its answer to 1 once the controller is there, whether the
//     message target's query worked or not.
//   - Slot 46 hands slot 64 of the new automaton the caller's whole word of
//     its byte argument (its low byte 0xFE when the controller manager is
//     full).
//   - Slot 47 answers 0 when the count is not below the share (+0x40).
//   - Slot 53 counts every ID of the occupant's set once per call; slot 56
//     keeps one set for all occupants, so each occupant's IDs are counted
//     again for every occupant after it.
//   - Slot 63 takes the automata filter of the service 0x00B43DC4 and never
//     uses it.
//   - Slot 67 sets the first three masks to all ones at a zero byte and the
//     fourth shifts by the byte itself (not less one).
//   - The camera follow's update reads its zoom factor from a table of six
//     on the stack by the zoom; a zoom outside 0..5 reads other stack slots
//     in the game. Ours logs it and takes 0.
//   - Slots 59 and 65 answer nothing; slot 56 answers slot 48's answer.
//
// Floats: 53-bit doubles, as the game's x87 runs; floats stored where the
// game stores them; FST keeps the unrounded value on the x87 stack and ours
// keeps the double the same way. The cell of a position (slot 20 and the
// camera update) is the game's floor: (a - 0.5) + 1.5 x 2^23 rounded to a
// float, its bits less 0x4B400000, plus one when that is a; the camera's
// pixel counts round to nearest the same way. Compares keep the asm's
// handling of unordered values. Values the game answers in ST0 (slot 42,
// DistanceSquared3, the random draws, the city's and terrain's floats) are
// doubles.
//
// Return values: AL alone for the bools, as the callers read them (slot 32
// answers 0 or 1 in EAX); pointers and the answers of calls in full.
//
// Still the game's, at their addresses: the SC4UIScale probe site
// CreateAutomaton (0x00505370, slot 15); the hash set, hash map, tree,
// vector and list helpers, the random draws, the camera helpers, the
// property getters, the script number, the occupant group templates'
// queries, the automaton's 0x00510360 and the allocators.
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
std::uint32_t Release(void* object) { return Virtual<std::uint32_t>(object, 0x08); }
void AddRef(void* object) { Virtual<std::uint32_t>(object, 0x04); }

float AsFloat(std::uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::uint32_t AsBits(float f)
{
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The game's globals and constants this part reads and writes.
namespace g {
inline constexpr std::uintptr_t kCheatsOnce = 0x00B21B88;    // uint32: slots 39 and 40's counter
inline constexpr std::uintptr_t kFollowed = 0x00B21B8C;      // the automaton the camera follows (referenced)
inline constexpr std::uintptr_t kFollowEdges = 0x00B21B90;   // uint32: the camera update's edge bits
inline constexpr std::uintptr_t kFollowUpdating = 0x00B21B94; // byte (slot 35 answers it)
inline constexpr std::uintptr_t kCheatObject = 0x00B21B98;   // an object slot 40 lets go
inline constexpr std::uintptr_t kFrozen = 0x00B21B9C;        // byte: the frame rate's pause
inline constexpr std::uintptr_t kRouteLine = 0x00B21BA0;     // the cheat's route line object
inline constexpr std::uintptr_t kZero = 0x00B21BA4;          // the float the game starts its points with
inline constexpr std::uintptr_t kFollowOffset = 0x00B21BA8;  // three floats (the camera's centre offset)
inline constexpr std::uintptr_t kFollowLast = 0x00B21BB4;    // three floats (the followed one's last point)
inline constexpr std::uintptr_t kTimeSlot = 0x00B21C84;      // uint32 (an automaton slot 33's second word)
inline constexpr std::uintptr_t kRates = 0x00B21D18;         // four floats: speed 0, 1, 2 and the U-Drive's
inline constexpr std::uintptr_t kKindCurvesOn = 0x00B21D58;  // byte (the tuning's)
inline constexpr std::uintptr_t kKindCurves = 0x00B21D44;    // hash table (kind -> curve)
inline constexpr std::uintptr_t kDriveTarget = 0x00B21D74;   // the U-Drive's object (slot 60)
inline constexpr std::uintptr_t kFrameRate = 0x00B09964;     // float the frame update leaves
inline constexpr std::uintptr_t kBuildingLeast = 0x00B0996C; // float (slot 54)
inline constexpr std::uintptr_t kOne = 0x00B09978;           // float 1.0
// Constants.
inline constexpr std::uintptr_t kTwo32 = 0x00A80AA8;         // float 2^32
inline constexpr std::uintptr_t kOneDouble = 0x00A80AB0;     // double 1.0
inline constexpr std::uintptr_t kHalf = 0x00A84D2C;          // float 0.5
inline constexpr std::uintptr_t kMinuteShare = 0x00A94124;   // float (slot 42)
inline constexpr std::uintptr_t kSixteenth = 0x00A952F0;     // float 1/16
inline constexpr std::uintptr_t kRoundMagic = 0x00A952F4;    // float 1.5 x 2^23
inline constexpr std::uintptr_t kViewMargin = 0x00A9552C;    // float (slot 48)
inline constexpr std::uintptr_t kFollowBlend = 0x00A95530;   // float (slot 64)
inline constexpr std::uintptr_t kFollowGain = 0x00A95534;    // float (slot 64)
inline constexpr std::uintptr_t kEdgeLow = 0x00A95538, kEdgeHigh = 0x00A9553C; // floats (slot 64)
inline constexpr std::uintptr_t kTimeScale = 0x00A867A4;     // float (slot 64)
inline constexpr std::uintptr_t kZoomFar = 0x00A85074;       // float (SetCameraFollow)
inline constexpr std::uintptr_t kClampLow = 0x00A81054, kClampHigh = 0x00A81228; // floats (slot 64)
inline constexpr std::uintptr_t kRemoveBelow = 0x00A8C950;   // float (slot 63)
} // namespace g

inline constexpr std::uint32_t kAutomatonIid = 0xA9B40F05;
inline constexpr std::uint32_t kBuildingOccupantType = 0x278128A0, kBuildingOccupantIid = 0x87DDA3A9;
inline constexpr std::uint32_t kNetworkOccupantType = 0xC772BF98, kNetworkOccupantIid = 0xA821EF94;
inline constexpr std::uint32_t kGroupsProperty = 0xAA1DD396;
inline constexpr std::uint32_t kCheats[] = {0xE97FCBC8, 0xE97FCBC9, 0x0B70C96D, 0x6BB354C7, 0xCC478B79};

namespace off {
inline constexpr std::size_t kLoaded = 0x10;  // set by Read
inline constexpr std::size_t kSourceCount = field::kSourceCounts + table::kCount; // +0xA74
inline constexpr std::size_t kViewRect = 0x3EC;  // the camera view's four floats
} // namespace off

// An STLport hash table's bucket count and bucket of a key.
std::uint32_t Buckets(char* t)
{
    return static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin));
}

char* BucketHead(char* t, std::uint32_t key) { return At<char**>(t, table::kBegin)[key % Buckets(t)]; }

// The node of a key, or null (the game's inline find).
char* FindNode(char* t, std::uint32_t key)
{
    for (char* n = BucketHead(t, key); n; n = At<char*>(n, 0))
        if (At<std::uint32_t>(n, 4) == key)
            return n;
    return nullptr;
}

// The nodes of a key in its bucket (the game's inline count).
std::uint32_t CountKey(char* t, std::uint32_t key)
{
    std::uint32_t count = 0;
    for (char* n = BucketHead(t, key); n; n = At<char*>(n, 0))
        if (At<std::uint32_t>(n, 4) == key)
            ++count;
    return count;
}

// The STLport iterator's next node, inline as the game's loops have it: the
// node's next, else the first node of a later bucket (from the node's key).
char* NextNode(char* node, char* t)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    std::uint32_t i = At<std::uint32_t>(node, 4) % Buckets(t);
    while (++i < Buckets(t))
        if (char* const next = At<char**>(t, table::kBegin)[i])
            return next;
    return nullptr;
}

struct Iterator {
    char* node;
    char* table;
};

Iterator Begin(char* t)
{
    Iterator it = {};
    CallThis<void*>(addr::SC4HashTable_Begin, t, static_cast<void*>(&it));
    return it;
}

// The game's iterator step through 0x0053DCF0 (the node's next, else
// NextBucket on the iterator).
char* Step(Iterator& it)
{
    char* next = At<char*>(it.node, 0);
    if (!next)
        next = CallThis<char*>(addr::SC4HashTable_NextBucket, static_cast<void*>(&it));
    it.node = next;
    return next;
}

// A std::set<uint32>'s in-order successor (STLport _Rb_tree_iterator).
char* TreeNext(char* n)
{
    if (char* right = At<char*>(n, tree::kRight)) {
        while (char* const left = At<char*>(right, tree::kLeft))
            right = left;
        return right;
    }
    char* p = At<char*>(n, tree::kParent);
    char* x = n;
    while (x == At<char*>(p, tree::kRight)) {
        x = p;
        p = At<char*>(p, tree::kParent);
    }
    if (At<char*>(x, tree::kRight) != p)
        x = p;
    return x;
}

// A new empty tree header (0x14 bytes, as the game's inline constructor).
char* NewTreeHeader()
{
    char* const header = static_cast<char*>(Allocate(0x14));
    At<std::uint8_t>(header, 0) = 0;
    At<char*>(header, tree::kParent) = nullptr;
    At<char*>(header, tree::kLeft) = header;
    At<char*>(header, tree::kRight) = header;
    return header;
}

// A vector of words (begin, end, capacity).
struct Words {
    std::uint32_t* begin;
    std::uint32_t* end;
    std::uint32_t* capacity;
};

// The game's inline push_back, with InsertN (where, value*, allocator*, 1,
// 1) when full; the allocator's address is the stack slot the game passes.
void PushWord(Words& v, std::uint32_t* value, void* tag)
{
    if (v.end != v.capacity) {
        if (v.end)
            *v.end = *value;
        ++v.end;
        return;
    }
    CallThis<void>(addr::SC4PtrVector_InsertN, static_cast<void*>(&v), v.end, value, tag, std::uint32_t{1},
                   std::uint32_t{1});
}

// An exemplar record of the +0xA18 vector by index.
char* Record(char* self, std::uint32_t index)
{
    return At<char*>(self, field::kExemplarsBegin) + index * exemplar::kSize;
}

// The game's cell floor: (a - 0.5) + 1.5 x 2^23 rounded to a float, its bits
// less 0x4B400000, plus one when that is a (`fucompp`, equal only).
std::int32_t CellFloor(double a)
{
    const float magic = static_cast<float>((a - static_cast<double>(G<float>(g::kHalf))) +
                                           static_cast<double>(G<float>(g::kRoundMagic)));
    std::int32_t n = static_cast<std::int32_t>(AsBits(magic) + 0xB4C00000u);
    if (a == static_cast<double>(n + 1))
        n = n + 1;
    return n;
}

// A double rounded to the nearest whole number the game's way (plus 1.5 x
// 2^23 rounded to a float, its bits less 0x4B400000).
std::int32_t RoundMagic(double a)
{
    const float magic = static_cast<float>(a + static_cast<double>(G<float>(g::kRoundMagic)));
    return static_cast<std::int32_t>(AsBits(magic) + 0xB4C00000u);
}

// The x87 FILD of a word read unsigned (the game adds 2^32 to a negative one).
double Unsigned(std::uint32_t v)
{
    double d = static_cast<double>(static_cast<std::int32_t>(v));
    if (static_cast<std::int32_t>(v) < 0)
        d += static_cast<double>(G<float>(g::kTwo32));
    return d;
}

// The x87 compares the game branches on (FCOM a with b, then FNSTSW):
// `test ah,5 / jp` jumps unless C0 alone is set, so the fall-through is a < b
// (equal and unordered jump); `test ah,0x41 / jnz` jumps for less, equal and
// unordered, so the fall-through is a > b.
bool Below(double a, double b) { return a < b; }
bool NotAbove(double a, double b) { return !(a > b); }

// The zoom and the rotation of the camera view.
std::int32_t Zoom(std::int32_t* rotation)
{
    std::int32_t zoom = 0;
    CallThis<void>(addr::SC4CameraView_ZoomAndRotation, G<void*>(addr::SC4_CameraView), static_cast<void*>(&zoom),
                   static_cast<void*>(rotation));
    return zoom;
}

// ====================================================================================
// Creation (simulators.automata.awmgr.create)

// 0x0050F790, slot 7 AddAutomaton (automaton), ret 4: with none, AL 1.
// Referenced, its slot 3, appended to the list (+0x44) and counted; put in
// the set (referenced) of every tracked group (+0xA50) its occupant is in
// and the set does not hold; slot 61 (shown); with the service 0x00B43DC4's
// filter refusing its occupant slot 9 with 0, else with +0x4C. AL 1.
std::uint8_t __fastcall AddAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("AddAutomaton", logged);
    if (!automaton)
        return 1;
    AddRef(automaton);
    Virtual<void>(automaton, 0x0C);
    char* const head = At<char*>(self, field::kAutomata);
    char* const node = static_cast<char*>(Allocate(0xC));
    At<void*>(node, 8) = automaton;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 4) = last;
    At<char*>(node, 0) = head;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
    ++At<std::uint32_t>(self, field::kCount);
    const Iterator it = Begin(self + field::kGroupInstances);
    for (char* n = it.node; n; n = NextNode(n, it.table)) {
        void* const occupant = Virtual<void*>(automaton, 0x14);
        if (!Virtual<std::uint8_t>(occupant, 0x58, At<std::uint32_t>(n, 4)))
            continue;
        char* const set = n + 8;
        const std::uint32_t key = reinterpret_cast<std::uint32_t>(automaton);
        if (CountKey(set, key) != 0)
            continue;
        CallThis<void>(addr::SC4Uint32HashSet_Resize, set, At<std::uint32_t>(set, table::kCount) + 1);
        alignas(4) char result[12] = {};
        std::uint32_t value = key;
        CallThis<void*>(addr::SC4Uint32HashSet_Insert, set, static_cast<void*>(result), static_cast<void*>(&value));
        AddRef(automaton);
    }
    Virtual<std::uint8_t>(self, 0xF4, automaton);
    void* filter = nullptr;
    Virtual<void>(G<void*>(addr::SC4_Service43DC4), 0x48, static_cast<void*>(&filter));
    if (filter && !Virtual<std::uint8_t>(filter, 0x0C, Virtual<void*>(automaton, 0x14)))
        Virtual<void>(automaton, 0x24, std::uint32_t{0});
    else
        Virtual<void>(automaton, 0x24, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kActive)));
    if (filter)
        Release(filter);
    return 1;
}

// 0x0050C990, slot 8 RemoveAutomaton (automaton, now), ret 8: with none, AL
// 1. With `now`: its slots 102 (3, 0) and 89 (1), the cheat object's
// 0x00562D90, out of every tracked group's set its occupant is in (released
// when there), out of the list (the node freed, the count down), the camera
// follow ended when it is the followed one, slot 62 (hidden), its slot 4 and
// the list's reference released. Else only its slots 102 (2, 0), 89 (1), 33
// (0, 0x00B21C84) and its +0x18C part's slots 7 (1) and 2 (0). AL 1.
std::uint8_t __fastcall RemoveAutomaton(char* self, void*, void* automaton, std::uint32_t now)
{
    static bool logged;
    FirstCall("RemoveAutomaton", logged);
    if (!automaton)
        return 1;
    if (!static_cast<std::uint8_t>(now)) {
        Virtual<void>(automaton, 0x198, std::uint32_t{2}, std::uint32_t{0});
        Virtual<void>(automaton, 0x164, std::uint32_t{1});
        Virtual<void>(automaton, 0x84, std::uint32_t{0}, G<std::uint32_t>(g::kTimeSlot));
        char* const part = static_cast<char*>(automaton) + 0x18C;
        Virtual<void>(part, 0x1C, std::uint32_t{1});
        Virtual<void>(part, 0x08, std::uint32_t{0});
        return 1;
    }
    Virtual<void>(automaton, 0x198, std::uint32_t{3}, std::uint32_t{0});
    Virtual<void>(automaton, 0x164, std::uint32_t{1});
    if (void* const cheat = G<void*>(g::kCheatObject))
        CallThis<void>(addr::SC4AutomataCheat_Forget, cheat, automaton);
    Iterator it = Begin(self + field::kGroupInstances);
    for (char* n = it.node; n; n = Step(it)) {
        it.node = n;
        void* const occupant = Virtual<void*>(automaton, 0x14);
        if (Virtual<std::uint8_t>(occupant, 0x58, At<std::uint32_t>(n, 4))) {
            char* const set = n + 8;
            std::uint32_t key = reinterpret_cast<std::uint32_t>(automaton);
            if (CountKey(set, key) > 0) {
                CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Erase, set, static_cast<void*>(&key));
                Release(automaton);
            }
        }
    }
    char* const head = At<char*>(self, field::kAutomata);
    char* node = At<char*>(head, 0);
    while (node != head && At<void*>(node, 8) != automaton)
        node = At<char*>(node, 0);
    if (node != head) {
        char* const next = At<char*>(node, 0);
        char* const prev = At<char*>(node, 4);
        At<char*>(prev, 0) = next;
        At<char*>(next, 4) = prev;
        Free(node);
        --At<std::uint32_t>(self, field::kCount);
    }
    if (automaton == G<void*>(g::kFollowed))
        Virtual<void>(self, 0x88, static_cast<void*>(nullptr));
    Virtual<std::uint8_t>(self, 0xF8, automaton);
    Virtual<void>(automaton, 0x10);
    Release(automaton);
    return 1;
}

// 0x00506DC0, slot 16 GetRandomCreationPoint (group, position, least, most,
// out, flags, unused), ret 0x1C: AL 0 when slot 30 refuses the group. The
// most is the least when below it, else at most the city's cells in x times
// its slot 0x16C. A distance ((r x most) - least) + least, a direction
// (2r - 1, 0 or r with flag 0x40000, 2r - 1) made a unit, the point the
// position plus it times the distance, its height the terrain's (slot 0x34)
// without the flag. AL 1.
std::uint8_t __fastcall GetRandomCreationPoint(char* self, void*, std::uint32_t group, const float* position,
                                               float least, float most, float* out, std::uint32_t flags,
                                               std::uint32_t)
{
    static bool logged;
    FirstCall("GetRandomCreationPoint", logged);
    if (!Virtual<std::uint8_t>(self, slot::kCanCreateGroup, group))
        return 0;
    void* const city = G<void*>(addr::SC4_City);
    const float cells = static_cast<float>(Virtual<std::int32_t>(city, 0x174));
    const double span = Virtual<double>(city, 0x16C) * static_cast<double>(cells);
    if (Below(most, least))
        most = least;
    else if (!NotAbove(most, span))
        most = static_cast<float>(span);
    char* const random = self + field::kRandom;
    const double r = CallThis<double>(addr::SC4Random_Unit, random);
    const float distance = static_cast<float>(
        (r * static_cast<double>(most) - static_cast<double>(least)) + static_cast<double>(least));
    const double rx = CallThis<double>(addr::SC4Random_Unit, random);
    const std::uint32_t lift = flags & 0x40000;
    out[0] = static_cast<float>((rx + rx) - G<double>(g::kOneDouble));
    if (lift)
        out[1] = static_cast<float>(CallThis<double>(addr::SC4Random_Unit, random));
    else
        out[1] = 0.0f;
    const double rz = CallThis<double>(addr::SC4Random_Unit, random);
    out[2] = static_cast<float>((rz + rz) - G<double>(g::kOneDouble));
    const double x = out[0], y = out[1], z = out[2];
    const double inverse = static_cast<double>(G<float>(g::kOne)) / std::sqrt(x * x + y * y + z * z);
    const double ux = inverse * static_cast<double>(out[0]);
    const float uy = static_cast<float>(inverse * static_cast<double>(out[1]));
    const float uz = static_cast<float>(inverse * static_cast<double>(out[2]));
    out[0] = static_cast<float>(ux);
    out[1] = uy;
    out[2] = uz;
    const double d = distance;
    const float dy = static_cast<float>(static_cast<double>(uy) * d);
    const float dz = static_cast<float>(static_cast<double>(uz) * d);
    const double px = ux * d + static_cast<double>(position[0]);
    const double py = static_cast<double>(dy) + static_cast<double>(position[1]);
    out[2] = static_cast<float>(static_cast<double>(dz) + static_cast<double>(position[2]));
    out[0] = static_cast<float>(px);
    out[1] = static_cast<float>(py);
    if (!lift)
        out[1] = static_cast<float>(Virtual<double>(G<void*>(addr::SC4_Terrain), 0x34, out[0], out[2]));
    return 1;
}

// The exemplar choices' common end: a record drawn from the candidates by the
// manager's generator (+0x50), or `none`.
char* Draw(char* self, std::uint32_t* begin, std::uint32_t* end, char* none)
{
    if (begin == end)
        return none;
    const std::uint32_t count = static_cast<std::uint32_t>(end - begin);
    const std::uint32_t i = CallThis<std::uint32_t>(addr::SC4Random_Below, self + field::kRandom, count);
    return Record(self, begin[i]);
}

// 0x0050BE90, slot 25 (group, bit, x, z), ret 0x10: the group's (+0xA44 for
// 0) records of the exemplar map, null for a group not there. With x and z
// both -1 or more, those whose masks (+0, +4, +8) meet the cells object's
// slot 0xE answer's (+0x20, +0x24, +0x28; all ones without one; no filter
// when one is 0). With a bit, those whose +0xC has it. One drawn (null for
// none).
char* __fastcall ChooseExemplar(char* self, void*, std::uint32_t group, std::uint32_t bit, std::int32_t x,
                                std::int32_t z)
{
    static bool logged;
    FirstCall("ChooseExemplar", logged);
    const bool bounded = x >= -1 && z >= -1;
    const bool withBit = bit != 0;
    Words v = {};
    std::uint32_t key = group ? group : At<std::uint32_t>(self, field::kGroup);
    char* const map = self + field::kExemplarMap;
    char* found = nullptr;
    CallThis<void>(addr::SC4Uint32Map_Find, map, static_cast<void*>(&found), &key);
    if (found == At<char*>(map, 0))
        return nullptr;
    Words* list = reinterpret_cast<Words*>(found + 0x14);
    std::uint32_t slotWord = 0; // the stack slot the game keeps an index in (and InsertN's tag)
    if (bounded) {
        std::uint32_t m0 = 0xFFFFFFFF, m4 = 0xFFFFFFFF, m8 = 0xFFFFFFFF;
        char* const masks = Virtual<char*>(At<void*>(self, field::kCells), 0x38, x, z);
        bool filter = true;
        if (masks) {
            m0 = At<std::uint32_t>(masks, 0x20);
            m4 = At<std::uint32_t>(masks, 0x24);
            m8 = At<std::uint32_t>(masks, 0x28);
            filter = m0 && m4 && m8;
        }
        if (filter) {
            for (std::uint32_t* p = list->begin; p != list->end; ++p) {
                char* const record = Record(self, *p);
                if ((At<std::uint32_t>(record, 8) & m8) && (At<std::uint32_t>(record, 0) & m0) &&
                    (At<std::uint32_t>(record, 4) & m4)) {
                    slotWord = *p;
                    CallThis<void>(addr::SC4PtrVector_PushBack, static_cast<void*>(&v), &slotWord);
                }
            }
            list = &v;
        }
    }
    if (withBit) {
        const std::uint32_t mask = 1u << (bit & 31);
        if (list == &v) {
            std::uint32_t* p = v.begin;
            while (p != v.end) {
                if (At<std::uint32_t>(Record(self, *p), 0xC) & mask) {
                    ++p;
                    continue;
                }
                std::uint32_t* const after = p + 1;
                if (after != v.end)
                    CallCdecl<void*>(addr::SC4_memmove, static_cast<void*>(p), static_cast<const void*>(after),
                                     static_cast<std::size_t>(reinterpret_cast<char*>(v.end) -
                                                              reinterpret_cast<char*>(after)));
                --v.end;
            }
        } else {
            for (std::uint32_t* p = list->begin; p != list->end; ++p) {
                slotWord = *p;
                if (At<std::uint32_t>(Record(self, slotWord), 0xC) & mask)
                    PushWord(v, &slotWord, &slotWord);
            }
            list = &v;
        }
    }
    char* const chosen = Draw(self, list->begin, list->end, nullptr);
    if (v.begin)
        Free(v.begin);
    return chosen;
}

// 0x0050C0D0, slot 26 (group, mask C, mask 0, mask 4, mask 8), ret 0x14: one
// drawn of the group's (+0xA44 for 0) records whose four masks meet the
// four; null for none and for a group not there.
char* __fastcall ChooseExemplarByMasks(char* self, void*, std::uint32_t group, std::uint32_t mC, std::uint32_t m0,
                                       std::uint32_t m4, std::uint32_t m8)
{
    static bool logged;
    FirstCall("ChooseExemplarByMasks", logged);
    Words v = {};
    std::uint32_t key = group ? group : At<std::uint32_t>(self, field::kGroup);
    char* const map = self + field::kExemplarMap;
    char* found = nullptr;
    CallThis<void>(addr::SC4Uint32Map_Find, map, static_cast<void*>(&found), &key);
    if (found == At<char*>(map, 0))
        return nullptr;
    Words* const list = reinterpret_cast<Words*>(found + 0x14);
    if (list->begin == list->end)
        return nullptr;
    for (std::uint32_t* p = list->begin; p != list->end; ++p) {
        char* const record = Record(self, *p);
        if ((At<std::uint32_t>(record, 0xC) & mC) && (At<std::uint32_t>(record, 8) & m8) &&
            (At<std::uint32_t>(record, 0) & m0) && (At<std::uint32_t>(record, 4) & m4))
            CallThis<void>(addr::SC4PtrVector_PushBack, static_cast<void*>(&v), p);
    }
    char* const chosen = Draw(self, v.begin, v.end, nullptr);
    if (v.begin)
        Free(v.begin);
    return chosen;
}

// 0x0050C1D0, slot 27 (group, value, bit), ret 0xC: one drawn of the group's
// (+0xA44 for 0) records whose +0xC has the bit and whose +0x38 is the
// value (any for 0); null for none.
char* __fastcall ChooseExemplarByValue(char* self, void*, std::uint32_t group, std::uint32_t value,
                                       std::uint32_t bit)
{
    static bool logged;
    FirstCall("ChooseExemplarByValue", logged);
    const std::uint32_t mask = 1u << (bit & 31);
    Words v = {};
    std::uint32_t key = group ? group : At<std::uint32_t>(self, field::kGroup);
    char* const map = self + field::kExemplarMap;
    char* found = nullptr;
    CallThis<void>(addr::SC4Uint32Map_Find, map, static_cast<void*>(&found), &key);
    if (found == At<char*>(map, 0))
        return nullptr;
    Words* const list = reinterpret_cast<Words*>(found + 0x14);
    if (list->begin == list->end)
        return nullptr;
    for (std::uint32_t* p = list->begin; p != list->end; ++p) {
        key = *p;
        char* const record = Record(self, key);
        if ((At<std::uint32_t>(record, 0xC) & mask) && (!value || At<std::uint32_t>(record, 0x38) == value))
            PushWord(v, &key, &key);
    }
    char* const chosen = Draw(self, v.begin, v.end, nullptr);
    if (v.begin)
        Free(v.begin);
    return chosen;
}

// One automaton's test for GetInstancesOfGroup: not flagged 1, its occupant
// in the group, not in the set yet: referenced and put in.
void GatherOne(void* automaton, std::uint32_t group, char* set)
{
    if (!automaton || Virtual<std::uint8_t>(automaton, 0x7C, std::uint32_t{1}))
        return;
    if (!Virtual<std::uint8_t>(Virtual<void*>(automaton, 0x14), 0x58, group))
        return;
    void* value = automaton;
    if (CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Count, set, static_cast<void*>(&value)) != 0)
        return;
    AddRef(automaton);
    alignas(4) char out[12] = {};
    CallThis<void*>(addr::SC4Uint32HashSet_InsertUnique, set, static_cast<void*>(out), static_cast<void*>(&value));
}

// 0x00510040, slot 32 GetInstancesOfGroup (group, set), ret 8: the automata
// of the group's tracked set (+0xA50) when it is tracked, else of the list,
// that are not flagged 1 and whose occupant is in the group, put in the set
// (referenced) when not there yet. EAX whether the set holds any.
std::uint32_t __fastcall GetInstancesOfGroup(char* self, void*, std::uint32_t group, char* set)
{
    static bool logged;
    FirstCall("GetInstancesOfGroup", logged);
    char* const tracked = FindNode(self + field::kGroupInstances, group);
    if (tracked) {
        const Iterator it = Begin(tracked + 8);
        for (char* n = it.node; n; n = NextNode(n, it.table))
            GatherOne(At<void*>(n, 4), group, set);
    } else {
        char* const head = At<char*>(self, field::kAutomata);
        for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kAutomata); node = At<char*>(node, 0))
            GatherOne(At<void*>(node, 8), group, set);
    }
    return At<std::uint32_t>(set, table::kCount) != 0 ? 1u : 0u;
}

// 0x00506600, slot 40 (slot 39's undoing): the counter 0x00B21B88 down; at
// 0, the camera follow ended, the cheat object let go (its slot 4, then
// released), the route line unprepared and released, and the five cheats
// unregistered (the cheat codes' slot 4). AL 1.
std::uint8_t __fastcall ReleaseCheats(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseCheats", logged);
    if (--G<std::uint32_t>(g::kCheatsOnce) != 0)
        return 1;
    Virtual<void>(self, 0x88, static_cast<void*>(nullptr));
    if (void* const object = G<void*>(g::kCheatObject)) {
        Virtual<void>(object, 0x10);
        Release(G<void*>(g::kCheatObject));
        G<void*>(g::kCheatObject) = nullptr;
    }
    if (void* const line = G<void*>(g::kRouteLine)) {
        CallThis<void>(addr::SC4RouteLine_Unprepare, line);
        Release(G<void*>(g::kRouteLine));
        G<void*>(g::kRouteLine) = nullptr;
    }
    if (void* const cheats = G<void*>(addr::SC4_CheatCodes))
        for (const std::uint32_t cheat : kCheats)
            Virtual<void>(cheats, 0x10, cheat);
    return 1;
}

// 0x00507B80, slot 43 (type, a, b, c), ret 0x10: a cGZMessage of the type
// with the three words (slots 12, 16 and 21) posted to the message server
// (slot 4, 0); EAX the message's Release.
std::uint32_t __fastcall PostMessage(char* self, void*, std::uint32_t type, std::uint32_t a, std::uint32_t b,
                                     std::uint32_t c)
{
    static bool logged;
    FirstCall("PostMessage", logged);
    (void)self;
    void* const server = G<void*>(addr::SC4_MessageServer);
    void* const memory = CallCdecl<void*>(addr::RZ_operator_new, std::uint32_t{0x2C});
    void* const message = memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRef(message);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x30, a);
    Virtual<void>(message, 0x40, b);
    Virtual<void>(message, 0x54, c);
    Virtual<std::uint8_t>(server, 0x10, message, std::uint32_t{0});
    return Release(message);
}

// 0x00511990, slot 45: every tracked group's set released (slot 33) and the
// table cleared; per automaton of the list slot 62 (hidden), the camera
// follow ended for the followed one, its slot 4 and released; the list's
// nodes freed and the count 0. AL 1.
std::uint8_t __fastcall RemoveAll(char* self, void*)
{
    static bool logged;
    FirstCall("RemoveAll", logged);
    char* const instances = self + field::kGroupInstances;
    const Iterator it = Begin(instances);
    for (char* n = it.node; n; n = NextNode(n, it.table))
        Virtual<void>(self, 0x84, static_cast<void*>(n + 8));
    CallThis<void>(addr::SC4HashSetTable_Clear, instances);
    for (char* node = At<char*>(At<char*>(self, field::kAutomata), 0); node != At<char*>(self, field::kAutomata);
         node = At<char*>(node, 0)) {
        void* const automaton = At<void*>(node, 8);
        Virtual<std::uint8_t>(self, 0xF8, automaton);
        if (automaton == G<void*>(g::kFollowed))
            Virtual<void>(self, 0x88, static_cast<void*>(nullptr));
        Virtual<void>(automaton, 0x10);
        Release(automaton);
    }
    char* node = At<char*>(At<char*>(self, field::kAutomata), 0);
    while (node != At<char*>(self, field::kAutomata)) {
        char* const next = At<char*>(node, 0);
        Free(node);
        node = next;
    }
    char* const head = At<char*>(self, field::kAutomata);
    At<char*>(head, 0) = head;
    At<char*>(At<char*>(self, field::kAutomata), 4) = At<char*>(self, field::kAutomata);
    At<std::uint32_t>(self, field::kCount) = 0;
    return 1;
}

// 0x00510690, slot 46 (group, flags, start, record, priority), ret 0x14: a
// new automaton, or null. A priority below 0xFF becomes 0xFE when the city's
// slot 0xC4 object holds the group (its slot 19) and its count (slot 28) is
// below its most (slot 27). None with priority 0 while inactive (+0x4C);
// none above the most automata (+0x3C) unless slot 47 made room, the flags'
// low byte has 0x80 or the priority is 0xFF. The record (else slot 44 for a
// start with its +8, else slot 25), the automata group (the record's +0x68,
// else the start's group, else +0xA44) and its class (else +0xA48) made by
// the COM; its slots 64 (the priority word) and 30 (flags, 1); with a start
// or a record the automaton's 0x00510360; the record's model (+0x30), its
// groups (+0x3C, +0x64) as property 0xAA1DD396 and flag 0x100000 for its
// +0x34, else the group's IDs; slot 95 (the group) and slot 33 twice.
void* __fastcall CreateAutomaton(char* self, void*, std::uint32_t group, std::uint32_t flags, char* start,
                                 char* record, std::uint32_t priority)
{
    static bool logged;
    FirstCall("CreateAutomaton", logged);
    std::uint8_t level = static_cast<std::uint8_t>(priority);
    if (level < 0xFF) {
        if (void* const city = G<void*>(addr::SC4_City)) {
            void* const controllers = Virtual<void*>(city, 0xC4);
            if (controllers && Virtual<std::uint8_t>(controllers, 0x4C, group)) {
                const std::uint32_t most = Virtual<std::uint8_t>(controllers, 0x6C);
                if (Virtual<std::uint32_t>(controllers, 0x70) < most) {
                    level = 0xFE;
                    priority = (priority & 0xFFFFFF00u) | level;
                }
            }
        }
    }
    if (!level && !At<std::uint8_t>(self, field::kActive))
        return nullptr;
    if (At<std::uint32_t>(self, field::kCount) > At<std::uint32_t>(self, field::kMaxCount) &&
        !Virtual<std::uint8_t>(self, 0xBC, priority) && !(static_cast<std::uint8_t>(flags) & 0x80) && level < 0xFF)
        return nullptr;
    if (!record) {
        if (At<void*>(start, 8))
            record = Virtual<char*>(self, slot::kFindExemplar, start);
        else
            record = Virtual<char*>(self, 0x64, group, std::uint32_t{0}, std::int32_t{-1}, std::int32_t{-1});
    }
    const std::uint32_t id = record ? At<std::uint32_t>(record, 0x68) : group;
    void* const scripts = At<void*>(self, field::kScripts);
    void* groupObject = Virtual<void*>(scripts, 0x18, id);
    if (!groupObject)
        groupObject = Virtual<void*>(scripts, 0x18, At<std::uint32_t>(self, field::kGroup));
    std::uint32_t automatonClass = 0;
    if (groupObject)
        automatonClass = CallThis<std::uint32_t>(addr::cSC4OccupantGroupTemplate_GetClass, groupObject);
    if (!automatonClass)
        automatonClass = At<std::uint32_t>(self, field::kAutomatonClass);
    void* automaton = nullptr;
    void* const com = CallCdecl<void*>(addr::RZGetCOM);
    Virtual<std::uint8_t>(com, 0x0C, automatonClass, kAutomatonIid, static_cast<void*>(&automaton));
    if (!automaton)
        return nullptr;
    char* const a = static_cast<char*>(automaton);
    Virtual<void>(a, 0x100, priority);
    Virtual<void>(a, 0x78, flags, std::uint32_t{1});
    if (At<void*>(start, 8))
        CallThis<std::uint8_t>(addr::SC4Automaton_SetStart, a, start);
    else if (record)
        CallThis<std::uint8_t>(addr::SC4Automaton_SetStart, a, record + 0x10);
    if (record) {
        void* const holder = Virtual<void*>(a + 4, 0x18);
        Virtual<void>(a + 0x20, 0x0C, At<std::uint32_t>(record, 0x30));
        alignas(4) char number[0x14] = {};
        CallThis<void*>(addr::SC4ScriptNumber_Construct, number);
        CallThis<void>(addr::SC4ScriptNumber_SetUint32Array, number, static_cast<void*>(record + 0x3C),
                       static_cast<std::uint32_t>(At<std::uint8_t>(record, 0x64)));
        Virtual<std::uint8_t>(holder, 0x38, kGroupsProperty, static_cast<void*>(number), std::uint32_t{0});
        if (At<std::uint8_t>(record, 0x34))
            Virtual<void>(a, 0x78, std::uint32_t{0x100000}, std::uint32_t{1});
        CallThis<void>(addr::SC4ScriptNumber_Destroy, number);
    } else if (groupObject) {
        void* const ids = CallThis<void*>(addr::cSC4OccupantGroupTemplate_CacheIds, groupObject);
        void* const holder = Virtual<void*>(a + 4, 0x18);
        Virtual<std::uint8_t>(holder, 0x38, kGroupsProperty, ids, std::uint32_t{0});
    }
    Virtual<void>(a, 0x17C, groupObject);
    Virtual<void>(a, 0x84, std::uint32_t{0}, std::uint32_t{0});
    Virtual<void>(a, 0x84, AsBits(1.0f), G<std::uint32_t>(g::kTimeSlot));
    return automaton;
}

// 0x0050A740, slot 47 (priority), ret 4: AL 0 when the count is not below
// the share (+0x40) or for priority 0; else the first automaton of the list
// whose priority (slot 63) is below gets slot 13 (0) and AL 1; AL 0 for none.
std::uint8_t __fastcall MakeRoom(char* self, void*, std::uint32_t priority)
{
    static bool logged;
    FirstCall("MakeRoom", logged);
    if (!(At<std::uint32_t>(self, field::kCount) < At<std::uint32_t>(self, field::kShare)))
        return 0;
    const std::uint8_t level = static_cast<std::uint8_t>(priority);
    if (!level)
        return 0;
    for (char* node = At<char*>(At<char*>(self, field::kAutomata), 0); node != At<char*>(self, field::kAutomata);
         node = At<char*>(node, 0)) {
        void* const automaton = At<void*>(node, 8);
        if (Virtual<std::uint8_t>(automaton, 0xFC) < level) {
            Virtual<void>(automaton, 0x34, std::uint32_t{0});
            return 1;
        }
    }
    return 0;
}

// 0x0050F930, slot 65: every key of the script system's slot 0x48 list (+8
// of its nodes) put in the source building table (+0xA64) with count 0 when
// not there.
void __fastcall FillSourceTable(char* self, void*)
{
    static bool logged;
    FirstCall("FillSourceTable", logged);
    char* const list = *Virtual<char**>(At<void*>(self, field::kScripts), 0x48);
    char* const counts = self + field::kSourceCounts;
    for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
        const std::uint32_t key = At<std::uint32_t>(node, 8);
        if (FindNode(counts, key))
            continue;
        std::uint32_t pair[2] = {key, 0};
        CallThis<void>(addr::SC4Uint32HashSet_Resize, counts, At<std::uint32_t>(counts, table::kCount) + 1);
        alignas(4) char result[12] = {};
        CallThis<void*>(addr::SC4Uint32HashMap_Insert, counts, static_cast<void*>(result),
                        static_cast<const void*>(pair));
    }
}

// A mask property of slot 67: its bytes (slot 0x5F) b set bits 1 << (b - 1);
// all ones at a zero byte or without the property.
std::uint32_t MaskProperty(void* holder, std::uint32_t property, std::uint32_t mask)
{
    void* const p = Virtual<void*>(holder, 0x24, property);
    if (!p)
        return 0xFFFFFFFF;
    void* const value = Virtual<void*>(p, 0x14);
    const std::uint8_t* const bytes = Virtual<const std::uint8_t*>(value, 0x17C);
    for (std::uint32_t i = 0; i < Virtual<std::uint32_t>(value, 0x10); ++i) {
        const std::uint8_t b = bytes[i];
        if (!b)
            return 0xFFFFFFFF;
        mask |= 1u << ((b - 1) & 31);
    }
    return mask;
}

// 0x0050F9C0, slot 67 (record, exemplar), ret 8: a 0x6C-byte record read
// from the exemplar: zeros, the group +0x68 = +0xA44; the name (property
// 0x20) at +0x1C; the groups (0xAA1DD396, ten at most) at +0x3C with their
// parents (by the script system's slot 11) and the count at +0x64; the
// group +0x68 from 0x2C23A3A1, else the last of the groups no other group of
// the list is in; that group's class (else its parent's) into the source
// building table with 0; the masks 0x696E88DB (+0), 0x296E88E9 (+4),
// 0x496E88F1 (+8) and 0xC973C9AA (+0xC, 1 << b, 0 without); three floats of
// 0x09C80416 (+0x10); the bool 0xAA5A9633 (+0x34) and 0x2A62D9F3 (+0x38).
// EAX the last property getter's answer.
std::uint32_t __fastcall ReadExemplar(char* self, void*, char* record, void* exemplar)
{
    static bool logged;
    FirstCall("ReadExemplar", logged);
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    At<std::uint8_t>(record, 0x34) = 0;
    At<std::uint32_t>(record, 0x38) = 0;
    for (const std::size_t at : {0x0, 0x4, 0x8, 0xC, 0x18, 0x10, 0x14})
        At<std::uint32_t>(record, at) = 0;
    At<std::uint32_t>(record, 0x68) = At<std::uint32_t>(self, field::kGroup);
    At<std::uint8_t>(record, 0x64) = 0;
    for (std::size_t i = 0; i < 10; ++i)
        At<std::uint32_t>(record, 0x3C + 4 * i) = 0;
    CallCdecl<std::uint8_t>(addr::SC4_GetPropertyString, holder, std::uint32_t{0x20},
                            static_cast<void*>(record + 0x1C));
    void* const scripts = At<void*>(self, field::kScripts);
    std::uint8_t& count = At<std::uint8_t>(record, 0x64);
    if (void* const p = Virtual<void*>(holder, 0x24, kGroupsProperty)) {
        void* leaf = nullptr;
        void* const value = Virtual<void*>(p, 0x14);
        const std::uint32_t* const ids = Virtual<const std::uint32_t*>(value, 0x19C);
        std::uint8_t i = 0;
        while (static_cast<std::uint32_t>(i) < Virtual<std::uint32_t>(value, 0x10)) {
            At<std::uint32_t>(record, 0x3C + 4 * count) = ids[i];
            ++count;
            ++i;
            if (i >= 10)
                break;
        }
        count = i;
        std::uint8_t j = 0;
        if (Virtual<std::uint32_t>(value, 0x10) > 0) {
            do {
                char* const group = Virtual<char*>(scripts, 0x2C, ids[j]);
                if (group)
                    for (char* parent = At<char*>(group, 0xC); parent && j < 10; parent = At<char*>(parent, 0xC)) {
                        const std::uint32_t parentId = At<std::uint32_t>(parent, 0x10);
                        const std::uint32_t n = count;
                        std::uint32_t k = 0;
                        while (k < n && At<std::uint32_t>(record, 0x3C + 4 * k) != parentId)
                            ++k;
                        if (k >= n && count < 10) {
                            At<std::uint32_t>(record, 0x3C + 4 * n) = parentId;
                            ++count;
                        }
                    }
                ++j;
            } while (static_cast<std::uint32_t>(j) < Virtual<std::uint32_t>(value, 0x10));
        }
        char* chosen = nullptr;
        if (CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint32, holder, std::uint32_t{0x2C23A3A1},
                                    static_cast<void*>(record + 0x68))) {
            chosen = Virtual<char*>(scripts, 0x2C, At<std::uint32_t>(record, 0x68));
        } else if (count) {
            std::uint8_t k = 0;
            do {
                char* const group = Virtual<char*>(scripts, 0x2C, At<std::uint32_t>(record, 0x3C + 4 * k));
                if (group) {
                    bool inner = false;
                    for (std::uint32_t m = 0; m < count; ++m) {
                        if (m == k)
                            continue;
                        void* const other = Virtual<void*>(scripts, 0x2C, At<std::uint32_t>(record, 0x3C + 4 * m));
                        if (other && CallThis<std::uint8_t>(addr::cSC4OccupantGroupTemplate_IsInGroup, other,
                                                            At<std::uint32_t>(group, 0x10))) {
                            inner = true;
                            break;
                        }
                    }
                    if (!inner) {
                        At<std::uint32_t>(record, 0x68) = At<std::uint32_t>(group, 0x10);
                        leaf = group;
                    }
                }
                ++k;
            } while (k < count);
            chosen = static_cast<char*>(leaf);
        }
        if (chosen) {
            std::uint32_t automatonClass = At<std::uint32_t>(chosen, 0x6C);
            if (!automatonClass) {
                char* const parent = At<char*>(chosen, 0xC);
                automatonClass = parent ? CallThis<std::uint32_t>(addr::cSC4AutomataManager_GroupClass, parent) : 0;
            }
            if (automatonClass) {
                char* const counts = self + field::kSourceCounts;
                if (!FindNode(counts, automatonClass)) {
                    std::uint32_t pair[2] = {automatonClass, 0};
                    CallThis<void>(addr::SC4Uint32HashSet_Resize, counts, At<std::uint32_t>(counts, table::kCount) + 1);
                    alignas(4) char result[12] = {};
                    CallThis<void*>(addr::SC4Uint32HashMap_Insert, counts, static_cast<void*>(result),
                                    static_cast<const void*>(pair));
                }
            }
        }
    }
    At<std::uint32_t>(record, 0x0) = MaskProperty(holder, 0x696E88DB, At<std::uint32_t>(record, 0x0));
    At<std::uint32_t>(record, 0x4) = MaskProperty(holder, 0x296E88E9, At<std::uint32_t>(record, 0x4));
    At<std::uint32_t>(record, 0x8) = MaskProperty(holder, 0x496E88F1, At<std::uint32_t>(record, 0x8));
    if (void* const p = Virtual<void*>(holder, 0x24, std::uint32_t{0xC973C9AA})) {
        void* const value = Virtual<void*>(p, 0x14);
        const std::uint8_t* const bytes = Virtual<const std::uint8_t*>(value, 0x17C);
        for (std::uint32_t i = 0; i < Virtual<std::uint32_t>(value, 0x10); ++i)
            if (const std::uint8_t b = bytes[i])
                At<std::uint32_t>(record, 0xC) |= 1u << (b & 31);
    } else {
        At<std::uint32_t>(record, 0xC) = 0;
    }
    if (void* const p = Virtual<void*>(holder, 0x24, std::uint32_t{0x09C80416})) {
        void* const value = Virtual<void*>(p, 0x14);
        const std::uint32_t* const words = Virtual<const std::uint32_t*>(value, 0x19C);
        if (Virtual<std::uint32_t>(value, 0x10) == 3) {
            At<std::uint32_t>(record, 0x10) = words[0];
            At<std::uint32_t>(record, 0x14) = words[1];
            At<std::uint32_t>(record, 0x18) = words[2];
        }
    }
    CallCdecl<std::uint8_t>(addr::SC4_GetPropertyBool, holder, std::uint32_t{0xAA5A9633},
                            static_cast<void*>(record + 0x34));
    return CallCdecl<std::uint32_t>(addr::SC4_GetPropertyUint32, holder, std::uint32_t{0x2A62D9F3},
                                    static_cast<void*>(record + 0x38));
}

// ====================================================================================
// Dispatch (simulators.automata.awmgr.dispatch)

// 0x00507120, slot 19 TranslateDispatchLocation (location, out point), ret
// 8, by the location's kind (+0): 0 an object's slot 8 (AL 0 without one),
// 1 its point, 2 the city's slot 0x184 of its cell with the cells object's
// height (slot 0x48), 3 the middle of two cells the same way; AL 0 for
// another kind, else 1.
std::uint8_t __fastcall TranslateToPoint(char* self, void*, const std::uint32_t* location, float* out)
{
    static bool logged;
    FirstCall("TranslateToPoint", logged);
    switch (location[0]) {
    case 0:
        if (!location[1])
            return 0;
        Virtual<void>(reinterpret_cast<void*>(location[1]), 0x20, static_cast<void*>(out));
        return 1;
    case 1:
        std::memcpy(out, location + 1, 12);
        return 1;
    case 2: {
        Virtual<void>(G<void*>(addr::SC4_City), 0x184, location[1], location[2], static_cast<void*>(out),
                      static_cast<void*>(out + 2));
        out[1] = static_cast<float>(Virtual<double>(At<void*>(self, field::kCells), 0x48, out[0], out[2]));
        return 1;
    }
    case 3: {
        const float zero = G<float>(g::kZero);
        float p[3] = {zero, zero, zero}, q[3] = {zero, zero, zero};
        void* const city = G<void*>(addr::SC4_City);
        Virtual<void>(city, 0x184, location[1], location[2], static_cast<void*>(&p[0]), static_cast<void*>(&p[2]));
        Virtual<void>(city, 0x184, location[3], location[4], static_cast<void*>(&q[0]), static_cast<void*>(&q[2]));
        const double dx = static_cast<double>(q[0]) - p[0];
        const double dy = static_cast<double>(q[1]) - p[1];
        const float dz = static_cast<float>(static_cast<double>(q[2]) - p[2]);
        const float h = static_cast<float>(static_cast<double>(G<float>(g::kOne)) * G<float>(g::kHalf));
        const float hx = static_cast<float>(dx * h);
        const double hy = dy * h;
        const float hz = static_cast<float>(static_cast<double>(dz) * h);
        const float x = static_cast<float>(static_cast<double>(hx) + p[0]);
        const double y = hy + p[1];
        const double z = static_cast<double>(hz) + p[2];
        out[0] = x;
        out[1] = static_cast<float>(y);
        out[2] = static_cast<float>(z);
        out[1] = static_cast<float>(Virtual<double>(At<void*>(self, field::kCells), 0x48, out[0], out[2]));
        return 1;
    }
    default:
        return 0;
    }
}

// 0x00506F50, slot 20 TranslateDispatchLocation (location, out cells x0, z0,
// x1, z1), ret 8, by the kind: 0 a building occupant's (type 0x278128A0)
// rectangle (its slot 11) and the lot manager's lot there its slot 14, else
// the object's point's cell (the city's slot 0x17C) both ways; 1 the cell of
// the point (the game's floor of x / 16); 2 its cell; 3 its rectangle; AL 0
// for another kind (and kind 0 without an object), else 1.
std::uint8_t __fastcall TranslateToCells(char*, void*, const std::uint32_t* location, std::int32_t* out)
{
    static bool logged;
    FirstCall("TranslateToCells", logged);
    switch (location[0]) {
    case 0: {
        void* const object = reinterpret_cast<void*>(location[1]);
        if (!object)
            return 0;
        if (Virtual<std::uint32_t>(object, 0x1C) == kBuildingOccupantType) {
            Virtual<void>(object, 0x2C, static_cast<void*>(out));
            void* const lots = Virtual<void*>(G<void*>(addr::SC4_City), 0x84);
            void* const lot = Virtual<void*>(lots, 0x18, out[0], out[3], std::uint32_t{0});
            if (lot)
                Virtual<void>(lot, 0x38, static_cast<void*>(out));
            return 1;
        }
        const float zero = G<float>(g::kZero);
        float p[3] = {zero, zero, zero};
        Virtual<void>(object, 0x20, static_cast<void*>(p));
        std::int32_t x = 0, z = 0;
        Virtual<void>(G<void*>(addr::SC4_City), 0x17C, p[0], p[2], static_cast<void*>(&x), static_cast<void*>(&z));
        out[0] = x;
        out[2] = x;
        out[1] = z;
        out[3] = z;
        return 1;
    }
    case 1: {
        const std::int32_t x =
            CellFloor(static_cast<double>(AsFloat(location[1])) * static_cast<double>(G<float>(g::kSixteenth)));
        out[2] = x;
        out[0] = x;
        const std::int32_t z =
            CellFloor(static_cast<double>(AsFloat(location[3])) * static_cast<double>(G<float>(g::kSixteenth)));
        out[3] = z;
        out[1] = z;
        return 1;
    }
    case 2:
        out[2] = out[0] = static_cast<std::int32_t>(location[1]);
        out[3] = out[1] = static_cast<std::int32_t>(location[2]);
        return 1;
    case 3:
        std::memcpy(out, location + 1, 16);
        return 1;
    default:
        return 0;
    }
}

// 0x00511260, slot 21 ChaseAutomaton (group, radius, target, all), ret
// 0x10: AL 0 without a target. The group's automata (slot 32 into a set of
// 100 buckets); with `all` every one within the radius (squared distance
// below its square as a float) chases the target (slot 22) and AL 1 when any
// did; else the nearest within it (the first of equal ones) chases, AL slot
// 22's answer. The set released (slot 33) and destroyed.
std::uint8_t __fastcall ChaseAutomaton(char* self, void*, std::uint32_t group, float radius, void* target,
                                       std::uint32_t all)
{
    static bool logged;
    FirstCall("ChaseAutomaton", logged);
    if (!target)
        return 0;
    std::uint8_t result = 0;
    const float zero = G<float>(g::kZero);
    float p[3] = {zero, zero, zero}, q[3] = {zero, zero, zero};
    Virtual<void>(target, 0x1C, static_cast<void*>(q));
    alignas(4) char set[table::kSize] = {};
    CallThis<void*>(addr::SC4Uint32HashSet_Construct, set, table::kBuckets);
    if (Virtual<std::uint8_t>(self, 0x80, group, static_cast<void*>(set))) {
        const double square = static_cast<double>(radius) * radius;
        if (static_cast<std::uint8_t>(all)) {
            const float limit = static_cast<float>(square);
            Iterator it = Begin(set);
            for (char* n = it.node; n; n = Step(it)) {
                it.node = n;
                void* const automaton = At<void*>(n, 4);
                Virtual<void>(automaton, 0x1C, static_cast<void*>(p));
                const double d = CallCdecl<double>(addr::SC4_DistanceSquared3, static_cast<const float*>(p),
                                                   static_cast<const float*>(q));
                if (Below(d, limit)) {
                    Virtual<std::uint8_t>(self, 0x58, automaton, target);
                    result = 1;
                }
            }
        } else {
            float best = static_cast<float>(square);
            void* nearest = nullptr;
            Iterator it = Begin(set);
            for (char* n = it.node; n; n = Step(it)) {
                it.node = n;
                void* const automaton = At<void*>(n, 4);
                Virtual<void>(automaton, 0x1C, static_cast<void*>(p));
                const double d = CallCdecl<double>(addr::SC4_DistanceSquared3, static_cast<const float*>(p),
                                                   static_cast<const float*>(q));
                if (Below(d, best)) {
                    best = static_cast<float>(d);
                    nearest = automaton;
                }
            }
            if (nearest)
                result = Virtual<std::uint8_t>(self, 0x58, nearest, target);
        }
        Virtual<void>(self, 0x84, static_cast<void*>(set));
    }
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
    if (void* const buckets = At<void*>(set, table::kBegin))
        Free(buckets);
    return result;
}

// 0x005072E0, slot 22 (chaser, target), ret 8: AL 0 without either, for the
// same one, with the chaser's slot 57 (0x10) or (0x34) set, its flags 0x40 or
// 1, when its slot 52 (0x34, 0, -1.0, 0, 0, 0, 0) fails or it has no
// controller (slot 56). Else the controller's +0x28 the target, and the
// target's slot 39 given the chaser's message target (0x452294AA); AL 1.
std::uint8_t __fastcall Chase(char*, void*, void* chaser, void* target)
{
    static bool logged;
    FirstCall("Chase", logged);
    if (!target || !chaser || chaser == target)
        return 0;
    if (Virtual<std::uint32_t>(chaser, 0xE4, std::uint32_t{0x10}))
        return 0;
    if (Virtual<std::uint8_t>(chaser, 0x7C, std::uint32_t{0x40}) || Virtual<std::uint8_t>(chaser, 0x7C, std::uint32_t{1}))
        return 0;
    if (Virtual<std::uint32_t>(chaser, 0xE4, std::uint32_t{0x34}))
        return 0;
    if (!Virtual<std::uint8_t>(chaser, 0xD0, std::uint32_t{0x34}, std::uint32_t{0}, AsBits(-1.0f), std::uint32_t{0},
                               std::uint32_t{0}, std::uint32_t{0}, std::uint32_t{0}))
        return 0;
    char* const controller = Virtual<char*>(chaser, 0xE0);
    if (!controller)
        return 0;
    CallThis<void>(addr::SC4RefPtr_Assign, controller + 0x28, target);
    void* messageTarget = nullptr;
    void** const slot = CallThis<void**>(addr::SC4RefPtr_Reset, static_cast<void*>(&messageTarget));
    if (Virtual<std::uint8_t>(chaser, 0x00, kMessageIid, static_cast<void*>(slot)))
        Virtual<void>(At<void*>(controller, 0x28), 0x9C, messageTarget);
    if (messageTarget)
        Release(messageTarget);
    return 1;
}

// 0x00507AC0, slot 41 (automaton), ret 4: whether its point (slot 7) is in
// the camera view (0x007F7E50), EAX in full.
std::uint32_t __fastcall InView(char*, void*, void* automaton)
{
    static bool logged;
    FirstCall("InView", logged);
    const float zero = G<float>(g::kZero);
    float p[3] = {zero, zero, zero};
    Virtual<void>(automaton, 0x1C, static_cast<void*>(p));
    return CallThis<std::uint32_t>(addr::SC4CameraView_InView, G<void*>(addr::SC4_CameraView), static_cast<void*>(p));
}

// 0x0050C8B0, slot 42 (kind), ret 4, ST0: 1.0 unless the tuning's switch
// (0x00B21D58) and the city's clock (slot 0x158): the kind's curve (6, 9 and
// 10 read 4's) of the kind curve table at the time (slot 13 x 0x00A94124,
// rounded, plus slot 10, rounded; both words unsigned); 1.0 without a curve.
double __fastcall KindRate(char*, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("KindRate", logged);
    if (!G<std::uint8_t>(g::kKindCurvesOn))
        return static_cast<double>(1.0f);
    void* const clock = Virtual<void*>(G<void*>(addr::SC4_City), 0x158);
    if (!clock)
        return static_cast<double>(1.0f);
    const float hours = static_cast<float>(Unsigned(Virtual<std::uint32_t>(clock, 0x34)) *
                                           static_cast<double>(G<float>(g::kMinuteShare)));
    const float time =
        static_cast<float>(Unsigned(Virtual<std::uint32_t>(clock, 0x28)) + static_cast<double>(hours));
    if (kind == 6 || kind == 9 || kind == 10)
        kind = 4;
    Iterator found = {};
    CallThis<void>(addr::SC4Uint32HashTable_Find, reinterpret_cast<void*>(g::kKindCurves), static_cast<void*>(&found),
                   &kind);
    if (!found.node)
        return static_cast<double>(1.0f);
    return CallThis<double>(addr::SC4Curve_Evaluate, found.node + 8, time);
}

// The source building table's counts of the IDs in a set (std::set<uint32>
// nodes, the key at +0x10) up by one where the table has them.
void CountSources(char* self, char* header)
{
    char* const counts = self + field::kSourceCounts;
    for (char* n = At<char*>(header, tree::kLeft); n != header; n = TreeNext(n))
        if (char* const node = FindNode(counts, At<std::uint32_t>(n, tree::kKey)))
            ++At<std::uint32_t>(node, 8);
}

// 0x0050C6A0, slot 53 (occupant), ret 4: the IDs its slot 3 object gives
// (slot 24, into a std::set) counted in the source building table. AL 1.
std::uint8_t __fastcall CountOccupantSources(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("CountOccupantSources", logged);
    struct {
        char* header;
        std::uint32_t size;
    } set = {NewTreeHeader(), 0};
    void* const object = Virtual<void*>(occupant, 0x0C);
    if (Virtual<std::uint8_t>(object, 0x60, static_cast<void*>(&set)))
        CountSources(self, set.header);
    if (set.size) {
        CallThis<void>(addr::SC4Uint32Set_EraseNodes, static_cast<void*>(&set), At<void*>(set.header, tree::kParent));
        At<char*>(set.header, tree::kLeft) = set.header;
        At<char*>(set.header, tree::kParent) = nullptr;
        At<char*>(set.header, tree::kRight) = set.header;
        set.size = 0;
    }
    if (set.header)
        Free(set.header);
    return 1;
}

// 0x00506D50, slot 54 (occupant), ret 4: a building occupant (type
// 0x278128A0, interface 0x87DDA3A9) whose slot 9 float is not below
// 0x00B0996C goes to slot 53. AL 1.
std::uint8_t __fastcall BuildingAdded(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("BuildingAdded", logged);
    if (Virtual<std::uint32_t>(occupant, 0x1C) != kBuildingOccupantType)
        return 1;
    void* building = nullptr;
    if (Virtual<std::uint8_t>(occupant, 0x00, kBuildingOccupantIid, static_cast<void*>(&building))) {
        const float* const value = Virtual<const float*>(building, 0x24);
        if (static_cast<double>(*value) >= static_cast<double>(G<float>(g::kBuildingLeast)))
            Virtual<std::uint8_t>(self, 0xD4, building);
    }
    if (building)
        Release(building);
    return 1;
}

// 0x0050C7E0, slot 55 (occupant), ret 4: a network occupant (type
// 0xC772BF98, interface 0xA821EF94) to slot 52; then each count above 0 of
// the source building table whose key the occupant is in (its slot 22)
// down by one. AL 1.
std::uint8_t __fastcall OccupantRemoved(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("OccupantRemoved", logged);
    if (Virtual<std::uint32_t>(occupant, 0x1C) == kNetworkOccupantType) {
        void* network = nullptr;
        if (Virtual<std::uint8_t>(occupant, 0x00, kNetworkOccupantIid, static_cast<void*>(&network)))
            Virtual<std::uint8_t>(self, 0xD0, network);
        if (network)
            Release(network);
    }
    const Iterator it = Begin(self + field::kSourceCounts);
    for (char* n = it.node; n; n = NextNode(n, it.table))
        if (At<std::uint32_t>(n, 8) > 0 && Virtual<std::uint8_t>(occupant, 0x58, At<std::uint32_t>(n, 4)))
            --At<std::uint32_t>(n, 8);
    return 1;
}

// 0x0050F690, slot 59 (automaton), ret 4: put in the set (referenced) of
// every tracked group its occupant is in and the set does not hold.
void __fastcall TrackAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("TrackAutomaton", logged);
    const Iterator it = Begin(self + field::kGroupInstances);
    for (char* n = it.node; n; n = NextNode(n, it.table)) {
        if (!Virtual<std::uint8_t>(Virtual<void*>(automaton, 0x14), 0x58, At<std::uint32_t>(n, 4)))
            continue;
        char* const set = n + 8;
        std::uint32_t value = reinterpret_cast<std::uint32_t>(automaton);
        if (CountKey(set, value) != 0)
            continue;
        CallThis<void>(addr::SC4Uint32HashSet_Resize, set, At<std::uint32_t>(set, table::kCount) + 1);
        alignas(4) char result[12] = {};
        CallThis<void*>(addr::SC4Uint32HashSet_Insert, set, static_cast<void*>(result), static_cast<void*>(&value));
        AddRef(automaton);
    }
}

// 0x005073D0, slot 61 (automaton), ret 4: AL 0 without one, when its
// occupant's slot 0x21 lacks 4, when shown already (+0x33 bit 0) or its
// slot 70 (1) fails; else its slot 71 (1) unless +0x33 bit 1, AL 1.
std::uint8_t __fastcall Show(char*, void*, char* automaton)
{
    static bool logged;
    FirstCall("Show", logged);
    if (!automaton)
        return 0;
    void* const occupant = Virtual<void*>(automaton, 0x14);
    if (!(Virtual<std::uint32_t>(occupant, 0x84) & 4))
        return 0;
    if (At<std::uint8_t>(automaton, 0x33) & 1)
        return 0;
    if (!Virtual<std::uint8_t>(automaton, 0x118, std::uint32_t{1}))
        return 0;
    if (!(At<std::uint8_t>(automaton, 0x33) & 2))
        Virtual<std::uint8_t>(automaton, 0x11C, std::uint32_t{1});
    return 1;
}

// 0x00507430, slot 62 (automaton), ret 4: its slot 71 (0) with +0x33 bit 1,
// then its slot 70 (0) with bit 0. AL 1.
std::uint8_t __fastcall Hide(char*, void*, char* automaton)
{
    static bool logged;
    FirstCall("Hide", logged);
    if (automaton) {
        if (At<std::uint8_t>(automaton, 0x33) & 2)
            Virtual<std::uint8_t>(automaton, 0x11C, std::uint32_t{0});
        if (At<std::uint8_t>(automaton, 0x33) & 1)
            Virtual<std::uint8_t>(automaton, 0x118, std::uint32_t{0});
    }
    return 1;
}

// ====================================================================================
// Frames and the camera (simulators.automata.awmgr.frame)

// 0x0050A930, slot 34 SetCameraFollow (automaton), ret 4: nothing for the
// one followed already. The old one released, the new one kept
// (referenced); with the view's camera (0x00B43DD0 slot 16), the zoom made 4
// when the app's switch is on and the zoom is below 0x00A85074, the camera
// centred on the occupant's point, which the camera update starts from
// (0x00B21BB4; the offset and the edge bits 0), and slot 64 (100.0) run
// with 0x00B21B94 set.
void __fastcall SetCameraFollow(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("SetCameraFollow", logged);
    void* const old = G<void*>(g::kFollowed);
    if (automaton == old)
        return;
    if (old)
        Release(old);
    G<void*>(g::kFollowed) = automaton;
    if (!automaton)
        return;
    AddRef(automaton);
    void* const view = G<void*>(addr::SC4_View43DD0);
    if (!view)
        return;
    char* const camera = Virtual<char*>(view, 0x40);
    G<std::uint8_t>(g::kFollowUpdating) = 1;
    if (char* const app = G<char*>(addr::SC4_App43CC4))
        if (At<std::uint8_t>(At<char*>(app, 0xC), 0x4BC) &&
            Below(static_cast<double>(At<std::int32_t>(camera, 0x108)), static_cast<double>(G<float>(g::kZoomFar))))
            CallThis<void>(addr::cSC4CameraControl_SetZoom, camera, AsBits(4.0f));
    const float zero = G<float>(g::kZero);
    float p[3] = {zero, zero, zero};
    Virtual<void>(Virtual<void*>(G<void*>(g::kFollowed), 0x14), 0x20, static_cast<void*>(p));
    CallThis<void>(addr::cSC4CameraControl_SetCenter, camera, static_cast<void*>(p));
    float* const offset = &G<float>(g::kFollowOffset);
    float* const last = &G<float>(g::kFollowLast);
    last[1] = p[1];
    offset[0] = 0.0f;
    offset[1] = 0.0f;
    offset[2] = 0.0f;
    last[0] = p[0];
    last[2] = p[2];
    G<std::uint32_t>(g::kFollowEdges) = 0;
    Virtual<void>(self, 0x100, AsBits(100.0f));
    G<std::uint8_t>(g::kFollowUpdating) = 0;
}

// 0x00506CD0, slot 48: the view rectangle (+0x14) kept at +0x24, the camera
// view's (+0x3EC) grown by 0x00A9552C each way, then slot 60. AL 1.
std::uint8_t __fastcall UpdateViewRect(char* self, void*)
{
    static bool logged;
    FirstCall("UpdateViewRect", logged);
    std::memcpy(self + field::kRect2, self + field::kRect, 16);
    std::memcpy(self + field::kRect, G<char*>(addr::SC4_CameraView) + off::kViewRect, 16);
    float* const rect = &At<float>(self, field::kRect);
    const double margin = G<float>(g::kViewMargin);
    rect[0] = static_cast<float>(static_cast<double>(rect[0]) - margin);
    rect[1] = static_cast<float>(static_cast<double>(rect[1]) - margin);
    rect[2] = static_cast<float>(static_cast<double>(rect[2]) + margin);
    rect[3] = static_cast<float>(static_cast<double>(rect[3]) + margin);
    Virtual<std::uint8_t>(self, 0xF0);
    return 1;
}

// 0x00505350, slot 49 (a, b), ret 8: slot 48 when they differ. AL 1.
std::uint8_t __fastcall ViewChanged(char* self, void*, std::uint32_t a, std::uint32_t b)
{
    static bool logged;
    FirstCall("ViewChanged", logged);
    if (a != b)
        Virtual<std::uint8_t>(self, 0xC0);
    return 1;
}

// The automaton's slot 9 by the filter (refused: 0) or +0x4C.
void ActivateOne(char* self, void* automaton, void* filter)
{
    if (filter && !Virtual<std::uint8_t>(filter, 0x0C, Virtual<void*>(automaton, 0x14))) {
        Virtual<void>(automaton, 0x24, std::uint32_t{0});
        return;
    }
    Virtual<void>(automaton, 0x24, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kActive)));
}

// 0x0050A5D0, slot 50 (filter holder, unused), ret 8: +0x4C whether the
// zoom is at least +0xA3C; each automaton of the list slot 9 by the filter
// the holder holds (refused: 0) or +0x4C. AL 1.
std::uint8_t __fastcall ApplyFilter(char* self, void*, void** holder, std::uint32_t)
{
    static bool logged;
    FirstCall("ApplyFilter", logged);
    std::int32_t rotation = 0;
    const std::int32_t zoom = Zoom(&rotation);
    At<std::uint8_t>(self, field::kActive) =
        zoom >= static_cast<std::int32_t>(At<std::uint8_t>(self, field::kMinZoom)) ? 1 : 0;
    for (char* node = At<char*>(At<char*>(self, field::kAutomata), 0); node != At<char*>(self, field::kAutomata);
         node = At<char*>(node, 0)) {
        void* const automaton = At<void*>(node, 8);
        if (!automaton)
            continue;
        ActivateOne(self, automaton, holder ? *holder : nullptr);
    }
    return 1;
}

// 0x0050F2C0, slot 56: after a load (+0x10) the automata flagged 0x8000
// leave the list (slot 62, their slot 4, released), the others get slot 3;
// for an old save (+0xA78) the source building table's counts start again
// from the occupants the occupant manager finds of those classes (one set
// kept for all of them). Then slot 48; EAX its answer.
std::uint32_t __fastcall AfterLoad(char* self, void*)
{
    static bool logged;
    FirstCall("AfterLoad", logged);
    if (At<std::uint8_t>(self, off::kLoaded)) {
        char* node = At<char*>(At<char*>(self, field::kAutomata), 0);
        while (node != At<char*>(self, field::kAutomata)) {
            void* const automaton = At<void*>(node, 8);
            if (Virtual<std::uint8_t>(automaton, 0x7C, std::uint32_t{0x8000})) {
                char* const next = At<char*>(node, 0);
                char* const prev = At<char*>(node, 4);
                At<char*>(prev, 0) = next;
                At<char*>(next, 4) = prev;
                Free(node);
                --At<std::uint32_t>(self, field::kCount);
                Virtual<std::uint8_t>(self, 0xF8, automaton);
                Virtual<void>(automaton, 0x10);
                Release(automaton);
                node = next;
            } else {
                Virtual<void>(automaton, 0x0C);
                node = At<char*>(node, 0);
            }
        }
        if (At<std::uint8_t>(self, field::kA78)) {
            Words classes = {};
            CallThis<void>(addr::SC4PtrVector_Reserve, static_cast<void*>(&classes), At<std::uint32_t>(self, off::kSourceCount));
            char* const counts = self + field::kSourceCounts;
            Iterator it = Begin(counts);
            std::uint8_t tag = 0;
            for (char* n = it.node; n; n = Step(it)) {
                it.node = n;
                std::uint32_t key = At<std::uint32_t>(n, 4);
                PushWord(classes, &key, &tag);
                At<std::uint32_t>(n, 8) = 0;
            }
            void* filter = nullptr;
            if (void* const memory = CallCdecl<void*>(addr::SC4_operator_new, std::uint32_t{0x18})) {
                filter = CallThis<void*>(addr::SC4OccupantGroupFilter_CopyConstruct, memory, static_cast<void*>(&classes));
                if (filter)
                    AddRef(filter);
            }
            void* const occupants = G<void*>(addr::SC4_NetworkService);
            // The game's list object: its head, then (in the next stack slot) the manager.
            struct {
                char* head;
                char* self;
            } list = {nullptr, self};
            auto allocator = []() -> void* {
                if (!G<void*>(addr::SC4List_NodeAllocator2)) {
                    void* object = nullptr;
                    if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
                        Virtual<std::uint8_t>(framework, 0x14, std::uint32_t{0x3AE4BEA3}, std::uint32_t{0x3AE4BEAB},
                                              static_cast<void*>(&object));
                    G<void*>(addr::SC4List_NodeAllocator2) = object;
                    CallThis<void>(addr::SC4_ReleaseRef, static_cast<void*>(&object));
                }
                return G<void*>(addr::SC4List_NodeAllocator2);
            };
            char* const head = Virtual<char*>(allocator(), 0x0C, std::uint32_t{0xC});
            At<char*>(head, 0) = head;
            At<char*>(head, 4) = head;
            list.head = head;
            if (Virtual<std::uint8_t>(occupants, 0x90, static_cast<void*>(&list), std::uint32_t{0}, std::uint32_t{0},
                                      filter, std::int32_t{-1})) {
                struct {
                    char* header;
                    std::uint32_t size;
                } set = {NewTreeHeader(), 0};
                char* const end = list.head;
                for (char* n = At<char*>(end, 0); n != end; n = At<char*>(n, 0)) {
                    Virtual<std::uint8_t>(At<void*>(n, 8), 0x60, static_cast<void*>(&set));
                    CountSources(self, set.header);
                }
                Virtual<void>(occupants, 0xC0, static_cast<void*>(&list));
                if (set.size) {
                    for (char* x = At<char*>(set.header, tree::kParent); x;) {
                        CallThis<void>(addr::SC4Uint32Set_EraseNodes, static_cast<void*>(&set),
                                       At<void*>(x, tree::kRight));
                        char* const left = At<char*>(x, tree::kLeft);
                        Free(x);
                        x = left;
                    }
                    At<char*>(set.header, tree::kLeft) = set.header;
                    At<char*>(set.header, tree::kParent) = nullptr;
                    At<char*>(set.header, tree::kRight) = set.header;
                    set.size = 0;
                }
                if (set.header)
                    Free(set.header);
            }
            CallThis<void>(addr::SC4List_Clear2, static_cast<void*>(&list));
            char* const listHead = list.head;
            Virtual<void>(allocator(), 0x10, static_cast<void*>(listHead));
            if (filter)
                Release(filter);
            if (classes.begin)
                Free(classes.begin);
        }
    }
    return Virtual<std::uint32_t>(self, 0xC0);
}

// 0x0050A670, slot 57 (word), ret 4: each automaton of the list its slot 81
// with the word. AL 1.
std::uint8_t __fastcall ForEachAutomaton(char* self, void*, std::uint32_t word)
{
    static bool logged;
    FirstCall("ForEachAutomaton", logged);
    for (char* node = At<char*>(At<char*>(self, field::kAutomata), 0); node != At<char*>(self, field::kAutomata);
         node = At<char*>(node, 0))
        if (void* const automaton = At<void*>(node, 8))
            Virtual<void>(automaton, 0x144, word);
    return 1;
}

// 0x0050A520, slot 60: active (slot 5) when the zoom is at least +0xA3C or
// the U-Drive's object (0x00B21D74's +0x30, slot 67's +0x10) is a group of
// the manager (slot 30), else inactive; while active each automaton its slot
// 74 (zoom, rotation). AL 1.
std::uint8_t __fastcall ActiveByZoom(char* self, void*)
{
    static bool logged;
    FirstCall("ActiveByZoom", logged);
    std::int32_t rotation = 0;
    const std::int32_t zoom = Zoom(&rotation);
    bool zoomedIn = zoom >= static_cast<std::int32_t>(At<std::uint8_t>(self, field::kMinZoom));
    if (!zoomedIn) {
        if (char* const drive = G<char*>(g::kDriveTarget))
            if (void* const object = At<void*>(drive, 0x30)) {
                char* const info = Virtual<char*>(object, 0x10C);
                zoomedIn = Virtual<std::uint8_t>(self, slot::kCanCreateGroup, At<std::uint32_t>(info, 0x10)) != 0;
            }
    }
    if (zoomedIn) {
        if (!At<std::uint8_t>(self, field::kActive))
            Virtual<std::uint8_t>(self, 0x14, std::uint32_t{1});
    } else {
        if (!At<std::uint8_t>(self, field::kActive))
            return 1;
        Virtual<std::uint8_t>(self, 0x14, std::uint32_t{0});
    }
    if (!At<std::uint8_t>(self, field::kActive))
        return 1;
    for (char* node = At<char*>(At<char*>(self, field::kAutomata), 0); node != At<char*>(self, field::kAutomata);
         node = At<char*>(node, 0))
        Virtual<void>(At<void*>(node, 8), 0x128, zoom, rotation);
    return 1;
}

// 0x00507BF0, the frame update's rate (no this): 0x00B09964 1.0 and
// 0x00B21B9C 0; with the simulator service, the U-Drive's rate (0x00B21D24)
// when 0x00B43D44's slot 13 answers; else, unless the service's slot 17 or
// without the switch +0xF18 of 0x00B43C94's slot 38, the speed's rate
// (0x00B21D18, 0x00B21D1C, 0x00B21D20 for speeds 0, other, 2), or 0 and the
// pause set when its slot 15 answers. AL the pause.
std::uint8_t __cdecl FrameRate()
{
    static bool logged;
    FirstCall("FrameRate", logged);
    void* const sim = G<void*>(addr::SC4_SimulatorService);
    G<float>(g::kFrameRate) = 1.0f;
    G<std::uint8_t>(g::kFrozen) = 0;
    if (!sim)
        return 0;
    const std::uint32_t* const rates = &G<std::uint32_t>(g::kRates);
    if (Virtual<std::uint8_t>(G<void*>(addr::SC4_Service43D44), 0x34)) {
        G<std::uint32_t>(g::kFrameRate) = rates[3];
        return G<std::uint8_t>(g::kFrozen);
    }
    if (!Virtual<std::uint8_t>(sim, 0x44) &&
        At<std::uint8_t>(Virtual<char*>(G<void*>(addr::SC4_Service43C94), 0x98), 0xF18)) {
        if (Virtual<std::uint8_t>(sim, 0x3C)) {
            G<std::uint32_t>(g::kFrameRate) = 0;
            G<std::uint8_t>(g::kFrozen) = 1;
            return 1;
        }
        const std::uint32_t speed = Virtual<std::uint32_t>(sim, 0x6C);
        if (speed == 0)
            G<std::uint32_t>(g::kFrameRate) = rates[0];
        else if (speed == 2)
            G<std::uint32_t>(g::kFrameRate) = rates[2];
        else
            G<std::uint32_t>(g::kFrameRate) = rates[1];
        return G<std::uint8_t>(g::kFrozen);
    }
    G<std::uint32_t>(g::kFrameRate) = rates[0];
    return G<std::uint8_t>(g::kFrozen);
}

// 0x0050A7B0, slot 63 the frame update (a, time), ret 8: the rate
// (0x00507BF0); with automata and not paused (+0x4D), per automaton (the
// next taken first): unless its +4 part's slot 0x21 has 4, it goes (slot 8,
// at once) when its slot 85 answers, stays as it is while paused by the
// rate, and is left unless flagged 0x210040; then flagged 0x4000000 once
// (removed, not at once, when its slot 101 fails), its slot 72 (a, time)
// when its +4 part's slot 5 answers (removed when that fails), removed at
// once when slot 85 answers and its slot 32 float is below 0x00A8C950, and
// unless flagged 0x10000 removed when not in view (slot 41).
void __fastcall FrameUpdate(char* self, void*, std::uint32_t a, float time)
{
    static bool logged;
    FirstCall("FrameUpdate", logged);
    CallCdecl<std::uint8_t>(addr::cSC4AutomataManager_FrameRate);
    if (!At<std::uint32_t>(self, field::kCount) || At<std::uint8_t>(self, field::kPaused))
        return;
    void* filter = nullptr;
    Virtual<void>(G<void*>(addr::SC4_Service43DC4), 0x48, static_cast<void*>(&filter));
    char* node = At<char*>(At<char*>(self, field::kAutomata), 0);
    if (node != At<char*>(self, field::kAutomata)) {
        do {
            char* const automaton = At<char*>(node, 8);
            char* const part = automaton + 4;
            node = At<char*>(node, 0);
            if (!(Virtual<std::uint32_t>(part, 0x84) & 4)) {
                if (Virtual<std::uint8_t>(automaton, 0x154)) {
                    Virtual<std::uint8_t>(self, 0x20, static_cast<void*>(automaton), std::uint32_t{1});
                    continue;
                }
                if (G<std::uint8_t>(g::kFrozen))
                    continue;
                if (!Virtual<std::uint8_t>(automaton, 0x7C, std::uint32_t{0x210040}))
                    continue;
            }
            if (!Virtual<std::uint8_t>(automaton, 0x7C, std::uint32_t{0x4000000})) {
                Virtual<void>(automaton, 0x78, std::uint32_t{0x4000000}, std::uint32_t{1});
                if (!Virtual<std::uint8_t>(automaton, 0x194))
                    Virtual<std::uint8_t>(self, 0x20, static_cast<void*>(automaton), std::uint32_t{0});
            }
            if (Virtual<std::uint8_t>(part, 0x14) && !Virtual<std::uint8_t>(automaton, 0x120, a, time))
                Virtual<std::uint8_t>(self, 0x20, static_cast<void*>(automaton), std::uint32_t{0});
            if (Virtual<std::uint8_t>(automaton, 0x154) &&
                Below(Virtual<double>(automaton, 0x80), static_cast<double>(G<float>(g::kRemoveBelow)))) {
                Virtual<std::uint8_t>(self, 0x20, static_cast<void*>(automaton), std::uint32_t{1});
                continue;
            }
            if (!Virtual<std::uint8_t>(automaton, 0x7C, std::uint32_t{0x10000}) &&
                !Virtual<std::uint8_t>(self, 0xA4, static_cast<void*>(automaton)))
                Virtual<std::uint8_t>(self, 0x20, static_cast<void*>(automaton), std::uint32_t{0});
        } while (node != At<char*>(self, field::kAutomata));
    }
    if (filter)
        Release(filter);
}

// 0x00507470, slot 64 the camera follow's update (time), ret 4: with a
// followed automaton not flagged 0x8000000 and the view's camera (slot 16),
// 0x00B21B94 set for the duration. Its velocity from the last point over the
// time step (time x 0x00A867A4), its heading (slot 15); the screen edges it
// is near (the 0x00B21B90 bits keep the last frame's where neither side is
// near) push the camera's target by a share of the view size by zoom, a
// blend (time step x 0x00A95530 clamped) of the old centre offset
// (0x00B21BA8) and the new one, the camera centred (0x007CD810) and offset
// (0x007CBC60).
void __fastcall FollowUpdate(char*, void*, float time)
{
    static bool logged;
    FirstCall("FollowUpdate", logged);
    const float step = static_cast<float>(static_cast<double>(time) * G<float>(g::kTimeScale));
    const float shares[6] = {AsFloat(0x3DAAAAAB), AsFloat(0x3DAAAAAB), AsFloat(0x3DAAAAAB),
                             AsFloat(0x3DCCCCCD), AsFloat(0x3E000000), AsFloat(0x3E2AAAAB)};
    void* const followed = G<void*>(g::kFollowed);
    if (!followed || Virtual<std::uint8_t>(followed, 0x7C, std::uint32_t{0x8000000}))
        return;
    void* const view = G<void*>(addr::SC4_View43DD0);
    if (!view)
        return;
    char* const camera = Virtual<char*>(view, 0x40);
    if (!camera)
        return;
    const float zero = G<float>(g::kZero);
    float p[3] = {zero, zero, zero};
    G<std::uint8_t>(g::kFollowUpdating) = 1;
    Virtual<void>(G<void*>(g::kFollowed), 0x1C, static_cast<void*>(p));
    const float* const last = &G<float>(g::kFollowLast);
    const double dx = static_cast<double>(p[0]) - last[0];
    const double dy = static_cast<double>(p[1]) - last[1];
    const float dz = static_cast<float>(static_cast<double>(p[2]) - last[2]);
    float q[3] = {p[0], p[1], p[2]};
    const float inverse = static_cast<float>(static_cast<double>(G<float>(g::kOne)) / step);
    float r[3] = {zero, zero, zero};
    const float vx = static_cast<float>(dx * inverse);
    const float vy = static_cast<float>(dy * inverse);
    const float vz = static_cast<float>(static_cast<double>(dz) * inverse);
    Virtual<void>(G<void*>(g::kFollowed), 0x3C, static_cast<void*>(r));
    void* const terrain = G<void*>(addr::SC4_Terrain);
    float s[3];
    s[0] = static_cast<float>(static_cast<double>(r[0]) + p[0]);
    s[1] = static_cast<float>(static_cast<double>(r[1]) + p[1]);
    s[2] = static_cast<float>(static_cast<double>(r[2]) + p[2]);
    const std::uint8_t inside = Virtual<std::uint8_t>(terrain, 0x2C, p[0], p[2]);
    float h1, h2;
    if (inside) {
        h1 = static_cast<float>(Virtual<double>(terrain, 0x34, p[0], p[2]));
        h2 = static_cast<float>(Virtual<double>(terrain, 0x78, p[0], p[2]));
    } else {
        h1 = static_cast<float>(Virtual<double>(terrain, 0x74));
        h2 = static_cast<float>(Virtual<double>(terrain, 0x74));
    }
    const float ground = Below(h1, h2) ? h2 : h1;
    void* const cameraView = G<void*>(addr::SC4_CameraView);
    float t[3] = {p[0], ground, p[2]};
    CallThis<void>(addr::SC4CameraView_ToViewLocal, cameraView, static_cast<void*>(q));
    CallThis<void>(addr::SC4CameraView_ToViewLocal, cameraView, static_cast<void*>(s));
    CallThis<void>(addr::SC4CameraView_ToViewLocal, cameraView, static_cast<void*>(t));
    const double ax = static_cast<double>(q[0]) - s[0];
    const double ay = static_cast<double>(q[1]) - s[1];
    const float fay = static_cast<float>(ay);
    const float il = static_cast<float>(static_cast<double>(G<float>(g::kOne)) /
                                        std::sqrt(ay * static_cast<double>(fay) + ax * ax));
    const float nx = static_cast<float>(ax * il);
    const double ny = static_cast<double>(fay) * il;
    bool right = !NotAbove(nx, G<float>(g::kEdgeHigh));   // test ah,0x41 / jnz: BL 1 when above
    bool left = Below(nx, G<float>(g::kEdgeLow));
    bool down = Below(ny, G<float>(g::kEdgeLow));
    bool up = !NotAbove(ny, G<float>(g::kEdgeHigh));
    const std::uint32_t edges = G<std::uint32_t>(g::kFollowEdges);
    if (!right && !left) {
        right = (edges >> 3) & 1;
        left = (edges >> 1) & 1;
    }
    if (!down && !up) {
        down = edges & 1;
        up = (edges >> 2) & 1;
    }
    std::int32_t width = 0, height = 0;
    CallThis<void>(addr::cSC4CameraControl_GetViewSize, camera, static_cast<void*>(&width), static_cast<void*>(&height));
    G<std::uint32_t>(g::kFollowEdges) = 0;
    const std::int32_t zoomLevel = CellFloor(static_cast<double>(At<std::int32_t>(camera, 0x108)));
    double share = 0.0;
    if (zoomLevel >= 0 && zoomLevel < 6) {
        share = shares[zoomLevel];
    } else {
        static bool leftover;
        FirstCall("FollowUpdate stack leftover (zoom share)", leftover);
    }
    const std::int32_t mx = RoundMagic(static_cast<double>(width) * share);
    const std::int32_t my = RoundMagic(static_cast<double>(height) * share);
    CallThis<void>(addr::SC4CameraView_ToScreen, cameraView, static_cast<void*>(q));
    CallThis<void>(addr::SC4CameraView_ToScreen, cameraView, static_cast<void*>(t));
    const std::int32_t rise = RoundMagic(std::fabs(static_cast<double>(q[1]) - t[1]));
    if (right) {
        q[0] = static_cast<float>(static_cast<double>(q[0]) - mx);
        G<std::uint32_t>(g::kFollowEdges) |= 8;
    } else if (left) {
        q[0] = static_cast<float>(static_cast<double>(q[0]) + mx);
        G<std::uint32_t>(g::kFollowEdges) |= 2;
    }
    if (down) {
        q[1] = static_cast<float>(static_cast<double>(q[1]) - my);
        if (inside) {
            const std::int32_t twice = my + my;
            q[1] = static_cast<float>(static_cast<double>(twice < rise ? twice : rise) + q[1]);
        }
        G<std::uint32_t>(g::kFollowEdges) |= 1;
    } else if (up) {
        q[1] = static_cast<float>(static_cast<double>(q[1]) + my);
        G<std::uint32_t>(g::kFollowEdges) |= 4;
    }
    CallThis<void>(addr::SC4CameraView_FromScreen, cameraView, static_cast<void*>(q));
    const float ca = At<float>(camera, 0x44), cb = At<float>(camera, 0x48), cc = At<float>(camera, 0x4C);
    const double ex = static_cast<double>(q[0]) - ca;
    const double ey = static_cast<double>(q[1]) - cb;
    const float ez = static_cast<float>(static_cast<double>(q[2]) - cc);
    const double gain = G<float>(g::kFollowGain);
    const float wx = static_cast<float>(static_cast<double>(static_cast<float>(ex * gain)) + vx);
    const float wy = static_cast<float>(ey * gain + vy);
    const float wz = static_cast<float>(static_cast<double>(ez) * gain + vz);
    double blend = static_cast<double>(step) * G<float>(g::kFollowBlend);
    const double low = G<float>(g::kClampLow), high = G<float>(g::kClampHigh);
    if (Below(blend, low))
        blend = low;
    else if (!NotAbove(blend, high))
        blend = high;
    const double keep = high - blend;
    float* const offset = &G<float>(g::kFollowOffset);
    float* const lastOut = &G<float>(g::kFollowLast);
    lastOut[0] = p[0];
    const float e0 = static_cast<float>(static_cast<double>(offset[0]) * keep);
    const float e1 = static_cast<float>(static_cast<double>(offset[1]) * keep);
    const double e2 = static_cast<double>(offset[2]) * keep;
    const float fwz = static_cast<float>(static_cast<double>(wz) * blend);
    const float n0 = static_cast<float>(static_cast<double>(wx) * blend + e0);
    offset[0] = n0;
    lastOut[1] = p[1];
    const float n1 = static_cast<float>(static_cast<double>(wy) * blend + e1);
    offset[1] = n1;
    const double n2 = static_cast<double>(fwz) + e2;
    lastOut[2] = p[2];
    offset[2] = static_cast<float>(n2);
    const double n0dt = static_cast<double>(n0) * step;
    const float n1dt = static_cast<float>(static_cast<double>(n1) * step);
    const float n2dt = static_cast<float>(n2 * step);
    float centre[3];
    centre[0] = static_cast<float>(n0dt + ca);
    centre[1] = static_cast<float>(static_cast<double>(n1dt) + cb);
    centre[2] = static_cast<float>(static_cast<double>(n2dt) + cc);
    CallThis<void>(addr::cSC4CameraControl_SetCenter, camera, static_cast<void*>(centre));
    CallThis<void>(addr::cSC4CameraControl_SetCenterOffset, camera, static_cast<void*>(offset));
    G<std::uint8_t>(g::kFollowUpdating) = 0;
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
    OPENSC4_DETOUR(addr::cSC4AutomataManager_AddAutomaton, AddAutomaton),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_RemoveAutomaton, RemoveAutomaton),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetRandomCreationPoint, GetRandomCreationPoint),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ChooseExemplar, ChooseExemplar),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ChooseExemplarByMasks, ChooseExemplarByMasks),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ChooseExemplarByValue, ChooseExemplarByValue),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetInstancesOfGroup, GetInstancesOfGroup),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ReleaseCheats, ReleaseCheats),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_PostMessage, PostMessage),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_RemoveAll, RemoveAll),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_CreateAutomaton46, CreateAutomaton),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_MakeRoom, MakeRoom),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_FillSourceTable, FillSourceTable),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ReadExemplar, ReadExemplar),
};

const Detour kDispatch[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataManager_TranslateToPoint, TranslateToPoint),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_TranslateToCells, TranslateToCells),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ChaseAutomaton, ChaseAutomaton),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Chase, Chase),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_InView, InView),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_KindRate, KindRate),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_CountOccupantSources, CountOccupantSources),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_BuildingAdded, BuildingAdded),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Slot55, OccupantRemoved),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_TrackAutomaton, TrackAutomaton),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Show, Show),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Hide, Hide),
};

const Detour kFrame[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataManager_SetCameraFollow, SetCameraFollow),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_UpdateViewRect, UpdateViewRect),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ViewChanged, ViewChanged),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ApplyFilter, ApplyFilter),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Slot56, AfterLoad),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ForEachAutomaton, ForEachAutomaton),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_ActiveByZoom, ActiveByZoom),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Slot63, FrameUpdate),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_FrameRate, FrameRate),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_FollowUpdate, FollowUpdate),
};

#undef OPENSC4_DETOUR

bool InstallCreate(registry::Mode) { return InstallList(kCreate, sizeof kCreate / sizeof kCreate[0], "create"); }
bool InstallDispatch(registry::Mode)
{
    return InstallList(kDispatch, sizeof kDispatch / sizeof kDispatch[0], "dispatch");
}
bool InstallFrame(registry::Mode) { return InstallList(kFrame, sizeof kFrame / sizeof kFrame[0], "frame"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.awmgr.create", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataManager_AddAutomaton,
                        "The common automata manager base's automata and creation: AddAutomaton, RemoveAutomaton, "
                        "GetRandomCreationPoint, the exemplar choices (slots 25 to 27), GetInstancesOfGroup, slot "
                        "40 (the cheats' release), 43 (a message), 45 (all removed), 46 (an automaton made), 47 "
                        "(room by priority), 65 (the source building table) and 67 (an exemplar record) (14 "
                        "detours; part aw3, src/core/automata_awmgr_auto.cpp)",
                        false, InstallCreate}),
    registry::Register({"simulators.automata.awmgr.dispatch", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataManager_TranslateToPoint,
                        "The common automata manager base's dispatch and chase: slots 19 to 22, 41 (in view), 42 "
                        "(the kind rate), 53 to 55 (occupants' source buildings), 59 (an automaton tracked), 61 "
                        "and 62 (shown and hidden) (12 detours; part aw3, src/core/automata_awmgr_auto.cpp)",
                        false, InstallDispatch}),
    registry::Register({"simulators.automata.awmgr.frame", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataManager_Slot63,
                        "The common automata manager base's frames and camera: SetCameraFollow, slots 48 to 50, "
                        "56 (after a load), 57, 60 (active by zoom), the frame update (63) with its rate and the "
                        "camera follow's update (64) (10 detours; part aw3, src/core/automata_awmgr_auto.cpp)",
                        false, InstallFrame}),
};

} // namespace
} // namespace opensc4::automata_awmgr
