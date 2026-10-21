// Our city situation manager, part c2 (build 641): the eight Lua callbacks
// Init registers in the table `sitmgr`, the situation starts (slots 19 to 25,
// 35, 37 and 39) and their helpers. The class map is in
// src/core/automata_situations.h and the survey in docs/automata.md, "Part
// c". Calls between functions of the part, and to the other parts, go to the
// game's addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.situations.lua (8): kLua below.
//   simulators.automata.situations.starts (25): kStarts.
//
// What it does.
//   - The Lua callbacks (cdecl: a1, the arguments, the results, the binding;
//     the binding's slot 0x20 with a1, then its slot 0x48, gives the
//     manager): get_success_count (slot 5 with argument 0, a word),
//     get_current_target and get_active_auto (slots 9 and 8, no null test),
//     show_CSI (slot 29 with argument 0 as a bool; answers slot 30 before
//     it), num_targets_remaining (slot 12, 1 without a manager),
//     activate_situation (posts 0xAC1045EF with the ID and argument 1 as a
//     bool), get_target_name (slot 16's text), and the accessor of the
//     variables num_targets and cur_target_index.
//   - IsSituationAvailable: the advisor service's (0x00B43D34, slot 7 with
//     0x1D) situation advices (its slot 10's map) asked through their
//     interface 0x6BEB1E8A: by ID, whether an advice's automata groups (slot
//     12) hold it; for an occupant, whether an advice's slot 53 answers true
//     for the occupant's 0xA9B40F05 interface.
//   - InitiateSituationByGuid (slot 21): the advice by ID; the candidates are
//     the occupants of its automata groups (or of the one ID given) that the
//     four services (+0x9F8) know (0x00525640), filtered by 0x005245F0; the
//     first that starts (slot 36 and then slot 37, or slot 22 when asked)
//     wins; else one made by the services near a lot of the group's class
//     (0x005292F0 with 0x00528360), else slot 36 with no occupant.
//   - InitiateSituationForOccupant (slot 22): slot 36 with the advice, or
//     with the first list (+0xA04) entry, walked backwards, that holds the
//     occupant or whose slot 53 answers true for it.
//   - InitiateSituationByAutomataGroup (slot 23): a list entry that answers
//     true for the group (slot 54) starts its own situation by its GUID
//     (slot 21); else, when asked, a joyride (slot 24) for a candidate or a
//     made one.
//   - StartJoyride (slot 24) and slot 35: +0xA98, the message 0xABBBCA64,
//     the U-Drive's start (0x00506850) for the automaton; slot 35 with 0
//     leaves the U-Drive's view (0x2B709676).
//   - Slot 37 (after a start): the status 3, the situation's script
//     (0x00525430), the controllers of the groups the car service handles
//     told (situation flag 2), the time limit (0x00526EF0: the situation's
//     script with the distance to the target, 0x00526A20, and the
//     automaton's speed), the message 0xABBBCA64 with the situation, slot 35
//     with 1.
//   - Slot 39: the driver (+0xA9C) and +0xAA0 dropped (+0xAA0 told to stop),
//     the time limit zeroed, the active automaton's CSI taken away
//     (0x00528ED0) from the list entry of the situation, slot 38, the CSI
//     layer shown or hidden.
//   - 0x005291C0 (slot 25's): the automaton's CSI shown on the first free
//     list entry that answers true for it (slot 53), under the most CSIs,
//     unless it is tracked, when it takes the entry of a lower priority one.
//
// Oddities kept: show_CSI and activate_situation read their bool into their
// binding argument's slot (show_CSI writes the byte 1 there first) and hand
// the whole word on; get_success_count reads argument 0 with slot 6, the
// others with slot 5; the accessor answers its slot 26 with the high bytes of
// slot 26's EAX; slot 21 overwrites its third argument's slot with its
// vtable; 0x005292F0 never frees the vector its services' slot 15 may fill,
// keeps looking at later groups after a find (a null answer then drops the
// find), and answers its pick after releasing it, as 0x00528360 does;
// 0x00526A20 answers its distances unrounded (a double in ST0) and its
// fallback rounded; 0x005256E0 copies the driver's position's first float
// through the x87 stack (a signalling NaN comes out quiet).
//
// Floats: 0x00526A20's sums and square roots are the game's under its
// 53-bit precision control (the x87's wider exponent aside). 0x00526EF0
// hands the Lua number of slot 23's answer on rounded to a float (it is a
// double already).
//
// Still the game's, at their addresses: the start (slot 36) and the end (slot
// 38) of a situation, 0x00525430, the U-Drive, the hash set and vector
// helpers, the overlay manager's Add and Remove, the geometry 0x005D68B0,
// 0x005D74D0 and 0x005D76A0, and the rest of the class (parts c3 to c5).
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
void Drop(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(object);
    }
}

// The game's SC4String on the stack (vtable 0x00A80810, the STL-style
// begin, end and capacity, a fifth word).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t extra;
};

// An SC4Vector of words (begin, end, capacity).
struct Vector {
    std::uint32_t* begin;
    std::uint32_t* end;
    std::uint32_t* capacity;
};

// (end - begin) >> 2, an arithmetic shift as the asm's SAR, compared
// unsigned.
std::uint32_t Count(const void* begin, const void* end)
{
    const std::int32_t bytes =
        static_cast<std::int32_t>(static_cast<const char*>(end) - static_cast<const char*>(begin));
    return static_cast<std::uint32_t>(bytes >> 2);
}

std::uint32_t Count(const Vector* vector) { return Count(vector->begin, vector->end); }

void** Services(char* self) { return At<void**>(self, field::kServices); }
std::uint32_t ServiceCount(char* self) { return Count(At<void*>(self, field::kServices), At<void*>(self, field::kServices + 4)); }

// A hash set on the stack, as the functions here make it: +4 to +0x10
// zeroed, then SC4Uint32HashSet's construct with 100 buckets.
struct LocalSet {
    alignas(4) char bytes[0x14];
};

void ConstructSet(LocalSet& set)
{
    std::memset(set.bytes, 0, sizeof set.bytes);
    CallThis<void>(addr::SC4Uint32HashSet_Construct, set.bytes, static_cast<std::uint32_t>(100));
}

void DestroySet(LocalSet& set)
{
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set.bytes);
    if (void* const buckets = At<void*>(set.bytes, map::kBegin))
        Free(buckets);
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

// The node after `node` in the bucket order, as the iterator's increment
// inlined in 0x0052AB00 finds it: its chain's next, else the first node of a
// later bucket, starting after its key's bucket.
char* NextNode(char* node, char* table)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    const std::uint32_t buckets = Count(At<void*>(table, map::kBegin), At<void*>(table, map::kEnd));
    const std::uint32_t bucket = At<std::uint32_t>(node, 4) % buckets;
    for (std::uint32_t i = bucket + 1; i < Count(At<void*>(table, map::kBegin), At<void*>(table, map::kEnd)); ++i)
        if (char* const found = At<char**>(table, map::kBegin)[i])
            return found;
    return nullptr;
}

// The tree increment of the advices' std::map (node +4 the parent, +8 the
// left, +0xC the right; the header's parent is the root), inlined in
// 0x00525CB0.
char* TreeNext(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* const left = At<char*>(right, 8))
            right = left;
        return right;
    }
    char* at = node;
    char* parent = At<char*>(at, 4);
    if (at == At<char*>(parent, 0xC)) {
        do {
            at = parent;
            parent = At<char*>(at, 4);
        } while (at == At<char*>(parent, 0xC));
    }
    if (At<char*>(at, 0xC) != parent)
        at = parent;
    return at;
}

// The advisor service's situation advices (0x00B43D34's slot 7 with 0x1D),
// or null.
void* AdviceService() { return Virtual<void*>(Global<void*>(addr::SC4_Service43D34), 0x1C, 0x1Du); }

constexpr std::uint32_t kAdviceIid = 0x6BEB1E8Au;
constexpr std::uint32_t kOccupantIid = 0xA9B40F05u;

void* MessageServer() { return Global<void*>(addr::SC4_MessageServer); }

// A cGZMessage of 0x2C bytes (RZ::operator_new, the constructor), or null.
void* NewMessage()
{
    void* const block = CallCdecl<void*>(addr::RZ_operator_new, static_cast<std::uint32_t>(0x2C));
    if (!block)
        return nullptr;
    return CallThis<void*>(addr::cGZMessage_cGZMessage, block);
}

// The callbacks' manager: the binding's slot 0x20 with a1, then its slot
// 0x48.
void* BoundManager(void* a1, void* binding)
{
    void* const upvalue = Virtual<void*>(binding, 0x20, a1);
    return Virtual<void*>(binding, 0x48, upvalue);
}

void* Argument(void* arguments, std::uint32_t index) { return Virtual<void*>(arguments, 0x14, index); }

// A script number made in a buffer of its own (0x14 bytes, as the game's) by
// one of SC4ScriptNumber's constructors, pushed through the results' slot
// 0x1C and destroyed.
template <typename T>
void PushNumber(void* results, std::uintptr_t constructor, T value)
{
    alignas(4) char number[0x14];
    void* const made = CallThis<void*>(constructor, number, value);
    Virtual<std::uint32_t>(results, 0x1C, 1u, made);
    CallThis<void>(addr::SC4ScriptNumber_Destroy, number);
}

std::uint32_t LuaToId(void* value) { return CallCdecl<std::uint32_t>(addr::cSC4AutomataScriptSystem_LuaToId, value); }

// The x87 copy of a float (FLD, FSTP): a signalling NaN comes out quiet.
float X87Copy(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) && !(bits & 0x00400000u))
        bits |= 0x00400000u;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::uint32_t ZeroBits() { return Global<std::uint32_t>(addr::cSC4CitySituationManager_ZeroFloat); }

// ====================================================================================
// The Lua callbacks

// 0x00524560, the accessor of the variables num_targets (+0xA5C) and
// cur_target_index (+0xA60): with `read` the Lua value read into the field
// (the value's slot 25, its answer passed on), else the field pushed (slot
// 26; AL 1 over the high bytes of its EAX).
std::uint32_t __cdecl LuaVariable(std::uint32_t* variable, void* value, std::uint8_t read)
{
    static bool logged;
    FirstCall("LuaVariable", logged);
    if (read)
        return Virtual<std::uint32_t>(value, 0x64, variable);
    const std::uint32_t pushed = Virtual<std::uint32_t>(value, 0x68, *variable);
    return (pushed & 0xFFFFFF00u) | 1u;
}

// 0x00524760, get_success_count: argument 0 (the arguments' slot 6) as an
// ID; with a manager and an ID not 0 its slot 5, pushed as a word.
std::uint8_t __cdecl LuaGetSuccessCount(void* a1, void* arguments, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaGetSuccessCount", logged);
    void* const manager = BoundManager(a1, binding);
    std::uint32_t count = 0;
    if (void* const value = Virtual<void*>(arguments, 0x18, 0u)) {
        const std::uint32_t id = LuaToId(value);
        if (manager && id)
            count = Virtual<std::uint32_t>(manager, slot::kGetMissionSuccessCount, id);
    }
    PushNumber(results, addr::SC4ScriptNumber_FromUint16, count);
    return 1;
}

// 0x005247E0, get_current_target: slot 9 (no null test), pushed as an
// object.
std::uint8_t __cdecl LuaGetCurrentTarget(void* a1, void*, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaGetCurrentTarget", logged);
    void* const target = Virtual<void*>(BoundManager(a1, binding), slot::kGetCurrentTarget);
    PushNumber(results, addr::SC4ScriptNumber_FromObject, target);
    return 1;
}

// 0x00524830, get_active_auto: slot 8 (no null test), pushed as an object.
std::uint8_t __cdecl LuaGetActiveAuto(void* a1, void*, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaGetActiveAuto", logged);
    void* const automaton = Virtual<void*>(BoundManager(a1, binding), slot::kGetActiveAuto);
    PushNumber(results, addr::SC4ScriptNumber_FromObject, automaton);
    return 1;
}

// 0x00524880, show_CSI: with a manager, argument 0 read as a bool (slot 10)
// into the binding argument's slot, whose low byte is set to 1 first; slot
// 30 answered, slot 29 with the slot's word. True without a manager.
std::uint8_t __cdecl LuaShowCsi(void* a1, void* arguments, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaShowCsi", logged);
    void* const manager = BoundManager(a1, binding);
    std::uint8_t answer = 1;
    if (manager) {
        std::uint32_t visible =
            (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(binding)) & 0xFFFFFF00u) | 1u;
        Virtual<std::uint32_t>(Argument(arguments, 0), 0x28, &visible);
        answer = Virtual<std::uint8_t>(manager, slot::kGetCsiVisible);
        Virtual<void>(manager, slot::kSetCsiVisible, visible);
    }
    PushNumber(results, addr::SC4ScriptNumber_FromBool, static_cast<std::uint32_t>(answer));
    return 1;
}

// 0x00524910, num_targets_remaining: slot 12, or 1 without a manager.
std::uint8_t __cdecl LuaNumTargetsRemaining(void* a1, void*, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaNumTargetsRemaining", logged);
    void* const manager = BoundManager(a1, binding);
    std::uint32_t remaining = 1;
    if (manager)
        remaining = Virtual<std::uint32_t>(manager, slot::kGetNumTargetsRemaining);
    PushNumber(results, addr::SC4ScriptNumber_FromUint, remaining);
    return 1;
}

// 0x00525550, activate_situation: argument 0 as an ID, argument 1 read as a
// bool into the binding argument's slot; with a manager and an ID not 0 the
// message 0xAC1045EF (data 1 the ID, data 2 the bool, data 3 0) posted, and
// true pushed; else false.
std::uint8_t __cdecl LuaActivateSituation(void* a1, void* arguments, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaActivateSituation", logged);
    void* const manager = BoundManager(a1, binding);
    std::uint8_t answer = 0;
    const std::uint32_t id = LuaToId(Argument(arguments, 0));
    std::uint32_t flag = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(binding));
    Virtual<std::uint32_t>(Argument(arguments, 1), 0x28, &flag);
    if (manager && id) {
        void* const message = NewMessage();
        AddRef(message);
        Virtual<void>(message, 0x14, 0xAC1045EFu);
        Virtual<void>(message, 0x2C, id);
        Virtual<void>(message, 0x40, flag & 0xFFu);
        Virtual<void>(message, 0x54, 0u);
        Virtual<std::uint32_t>(MessageServer(), 0x10, message, 0u);
        Release(message);
        answer = 1;
    }
    PushNumber(results, addr::SC4ScriptNumber_FromBool, static_cast<std::uint32_t>(answer));
    return 1;
}

// 0x0052DB50, get_target_name: an empty string, given slot 16's text when
// there is a manager, pushed as a string.
std::uint8_t __cdecl LuaGetTargetName(void* a1, void*, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaGetTargetName", logged);
    void* const manager = BoundManager(a1, binding);
    GameString text;
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.extra = 0;
    if (manager) {
        GameString name;
        GameString* const got = Virtual<GameString*>(manager, slot::kGetCurrentTargetName, static_cast<void*>(&name));
        char** const range = got ? &got->begin : nullptr;
        if (range != &text.begin) {
            void* allocator = binding;
            CallThis<void>(addr::SC4String_AssignRange, static_cast<void*>(&text.begin), range[0], range[1],
                           static_cast<void*>(&allocator));
        }
        name.vtable = addr::SC4String_Vtable;
        if (name.begin)
            Free(name.begin);
    }
    PushNumber(results, addr::SC4ScriptNumber_FromString, static_cast<void*>(&text));
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
    return 1;
}

// ====================================================================================
// The availability and the starts

// 0x00525CB0, IsSituationAvailable by ID (slot 19, ret 4): whether an
// advice's automata groups (slot 12) hold the ID.
std::uint8_t __fastcall IsSituationAvailableById(char*, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("IsSituationAvailableById", logged);
    std::uint8_t found = 0;
    void* const service = AdviceService();
    if (!service)
        return found;
    char** const tree = Virtual<char**>(service, 0x28);
    void* face = nullptr;
    char* node = At<char*>(*tree, 8);
    if (node == *tree)
        return found;
    do {
        if (found)
            break;
        if (void* const advice = At<void*>(node, 0x10)) {
            if (void* const old = face) {
                face = nullptr;
                Release(old);
            }
            if (Virtual<std::uint8_t>(advice, 0x00, kAdviceIid, &face)) {
                const Vector* const groups = Virtual<const Vector*>(face, 0x30);
                std::uint32_t* at = groups->begin;
                const std::uint32_t count = Count(groups);
                for (std::uint32_t i = 0; i < count; ++i, ++at)
                    if (*at == id) {
                        found = 1;
                        break;
                    }
            }
        }
        node = TreeNext(node);
    } while (node != *tree);
    if (face)
        Release(face);
    return found;
}

// 0x00525BC0, IsSituationAvailable for an occupant (slot 20, ret 4): whether
// an advice's slot 53 answers true for the occupant's 0xA9B40F05 interface.
std::uint8_t __fastcall IsSituationAvailable(char*, void*, void* occupant)
{
    static bool logged;
    FirstCall("IsSituationAvailable", logged);
    std::uint8_t found = 0;
    void* occupantFace = nullptr;
    if (!occupant)
        return found;
    if (Virtual<std::uint8_t>(occupant, 0x00, kOccupantIid, &occupantFace)) {
        if (void* const service = AdviceService()) {
            char** const tree = Virtual<char**>(service, 0x28);
            // The game keeps the advice interface in its argument's slot.
            void* face = nullptr;
            char* it = At<char*>(*tree, 8);
            if (it != *tree) {
                do {
                    if (found)
                        break;
                    if (void* const advice = At<void*>(it, 0x10)) {
                        if (void* const old = face) {
                            face = nullptr;
                            Release(old);
                        }
                        if (Virtual<std::uint8_t>(advice, 0x00, kAdviceIid, &face))
                            found = Virtual<std::uint8_t>(face, 0xD4, occupantFace);
                    }
                    CallThis<void>(addr::SC4MapIterator_Next, static_cast<void*>(&it));
                } while (it != *tree);
                if (face)
                    Release(face);
            }
        }
    }
    if (occupantFace)
        Release(occupantFace);
    return found;
}

// 0x0052A8B0, InitiateSituationByGuid (slot 21, ret 0xC).
std::uint8_t __fastcall InitiateSituationByGuid(char* self, void*, std::uint32_t id, std::uint8_t forOccupant,
                                                std::uint32_t occupantGroup)
{
    static bool logged;
    FirstCall("InitiateSituationByGuid", logged);
    std::uint8_t result = 0;
    void* advice = nullptr;
    void* const service = AdviceService();
    if (service) {
        void* const found = Virtual<void*>(service, 0x30, id);
        if (found) {
            if (void* const old = advice) {
                advice = nullptr;
                Release(old);
            }
            if (Virtual<std::uint8_t>(found, 0x00, kAdviceIid, &advice)) {
                Vector groups{};
                if (occupantGroup == 0)
                    CallThis<void*>(addr::SC4Uint32Vector_CopyAssign, static_cast<void*>(&groups),
                                    Virtual<void*>(advice, 0x30));
                else
                    CallThis<void>(addr::SC4PtrVector_PushBack, static_cast<void*>(&groups), &occupantGroup);
                LocalSet candidates;
                ConstructSet(candidates);
                if (CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GatherCandidates, self,
                                           static_cast<void*>(&groups), static_cast<void*>(candidates.bytes))) {
                    Iterator walk = Begin(candidates.bytes);
                    char* node = walk.node;
                    while (node) {
                        void* const occupant = At<void*>(node, 4);
                        if (CallThis<std::uint8_t>(addr::cSC4CitySituationManager_IsCandidate, self, occupant)) {
                            if (forOccupant) {
                                // The game keeps its vtable in its third argument's slot here.
                                void* const target = Virtual<void*>(occupant, 0x14);
                                if (Virtual<std::uint8_t>(self, slot::kInitiateSituationForOccupant, target, advice)) {
                                    result = 1;
                                    break;
                                }
                            } else if (Virtual<std::uint8_t>(self, slot::kStartSituation, advice, occupant, 0u)) {
                                Virtual<void>(self, slot::kBeginDrive);
                                result = 1;
                                break;
                            }
                        }
                        node = At<char*>(node, 0);
                        if (!node)
                            node = CallThis<char*>(addr::SC4HashTable_NextBucket, static_cast<void*>(&walk));
                        walk.node = node;
                    }
                    Virtual<void>(Services(self)[0], 0x84, static_cast<void*>(candidates.bytes));
                }
                if (!result) {
                    void* const made = CallThis<void*>(addr::cSC4CitySituationManager_CreateCandidate, self,
                                                       static_cast<void*>(&groups));
                    if (!made) {
                        Virtual<std::uint8_t>(self, slot::kStartSituation, advice, static_cast<void*>(nullptr), 1u);
                    } else {
                        AddRef(made);
                        bool drop;
                        if (forOccupant) {
                            void* const target = Virtual<void*>(made, 0x14);
                            result = Virtual<std::uint8_t>(self, slot::kInitiateSituationForOccupant, target, advice);
                            drop = !result;
                        } else if (Virtual<std::uint8_t>(self, slot::kStartSituation, advice, made, 0u)) {
                            result = 1;
                            Virtual<void>(self, slot::kBeginDrive);
                            drop = false;
                        } else {
                            drop = true;
                        }
                        if (drop)
                            Virtual<void>(made, 0x34, 0u);
                        Release(made);
                    }
                }
                DestroySet(candidates);
                if (groups.begin)
                    Free(groups.begin);
            }
        }
    }
    if (advice)
        Release(advice);
    return result;
}

// 0x00525DC0, InitiateSituationForOccupant (slot 22, ret 8).
std::uint8_t __fastcall InitiateSituationForOccupant(char* self, void*, void* occupant, void* advice)
{
    static bool logged;
    FirstCall("InitiateSituationForOccupant", logged);
    if (!occupant)
        return 0;
    std::uint8_t result = 0;
    void* face = nullptr;
    if (Virtual<std::uint8_t>(occupant, 0x00, kOccupantIid, &face)) {
        void* chosen = advice;
        if (!chosen) {
            char* const head = At<char*>(self, field::kList);
            char* const stop = At<char*>(head, 0);
            for (char* at = head; at != stop; at = At<char*>(at, 4)) {
                char* const node = At<char*>(at, 4);
                void* const entry = At<void*>(node, 8);
                if (!entry)
                    continue;
                if (At<void*>(node, 0xC) == face || Virtual<std::uint8_t>(entry, 0xD4, face)) {
                    chosen = entry;
                    break;
                }
            }
        }
        if (chosen)
            result = Virtual<std::uint8_t>(self, slot::kStartSituation, chosen, face, 1u);
    }
    if (face)
        Release(face);
    return result;
}

// 0x0052AB00, InitiateSituationByAutomataGroup (slot 23, ret 0xC).
std::uint8_t __fastcall InitiateSituationByAutomataGroup(char* self, void*, std::uint32_t group,
                                                         std::uint32_t forOccupant, std::uint32_t joyride)
{
    static bool logged;
    FirstCall("InitiateSituationByAutomataGroup", logged);
    std::uint8_t result = 0;
    Vector groups{};
    // The game hands its joyride argument's slot as the allocator.
    std::uint32_t allocator = joyride;
    CallThis<void>(addr::SC4PtrVector_InsertN, static_cast<void*>(&groups), static_cast<void*>(nullptr), &group,
                   &allocator, 1u, 1u);
    char* const head = At<char*>(self, field::kList);
    char* const stop = At<char*>(head, 0);
    for (char* at = head; at != stop; at = At<char*>(at, 4)) {
        char* const node = At<char*>(at, 4);
        void* const entry = At<void*>(node, 8);
        if (!entry || !Virtual<std::uint8_t>(entry, 0xD8, static_cast<void*>(&groups)))
            continue;
        void* const situation = Virtual<void*>(At<void*>(node, 8), 0x0C);
        const std::uint32_t guid = Virtual<std::uint32_t>(situation, 0x28);
        result = Virtual<std::uint8_t>(self, slot::kInitiateSituationByGuid, guid, forOccupant, group);
        if (groups.begin)
            Free(groups.begin);
        return result;
    }
    if (joyride & 0xFFu) {
        void* chosen = nullptr;
        LocalSet candidates;
        ConstructSet(candidates);
        bool have = false;
        if (CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GatherCandidates, self,
                                   static_cast<void*>(&groups), static_cast<void*>(candidates.bytes))) {
            const Iterator it = Begin(candidates.bytes);
            for (char* node = it.node; node; node = NextNode(node, it.table)) {
                void* const occupant = At<void*>(node, 4);
                if (occupant &&
                    CallThis<std::uint8_t>(addr::cSC4CitySituationManager_IsCandidate, self, occupant)) {
                    AddRef(occupant);
                    chosen = occupant;
                    break;
                }
            }
            Virtual<void>(Services(self)[0], 0x84, static_cast<void*>(candidates.bytes));
            have = chosen != nullptr;
        }
        if (!have) {
            void* const made = CallThis<void*>(addr::cSC4CitySituationManager_CreateCandidate, self,
                                               static_cast<void*>(&groups));
            if (made) {
                AddRef(made);
                chosen = made;
                have = true;
            }
        }
        if (have) {
            Virtual<void>(self, slot::kStartJoyride, chosen, 1u, static_cast<void*>(nullptr));
            result = 1;
        }
        DestroySet(candidates);
        if (chosen)
            Release(chosen);
    }
    if (groups.begin)
        Free(groups.begin);
    return result;
}

// 0x00524E20, StartJoyride (slot 24, ret 0xC): +0xA98, the message
// 0xABBBCA64 (data 1 1) sent, the U-Drive started for the automaton.
void __fastcall StartJoyride(char* self, void*, void* automaton, std::uint8_t joyride, void* target)
{
    static bool logged;
    FirstCall("StartJoyride", logged);
    At<std::uint8_t>(self, field::kJoyride) = joyride;
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, 0xABBBCA64u);
    Virtual<void>(message, 0x2C, 1u);
    Virtual<std::uint32_t>(MessageServer(), 0x0C, message);
    Release(message);
    CallCdecl<void>(addr::SC4_ParkAutomaton, automaton, target);
}

// 0x0052A7A0, TreatAutomatonAsGroup (slot 25, ret 8): the automaton's slot
// 5's slot 23 with the group, then 0x005291C0.
void __fastcall TreatAutomatonAsGroup(char* self, void*, void* automaton, std::uint32_t group)
{
    static bool logged;
    FirstCall("TreatAutomatonAsGroup", logged);
    if (!automaton)
        return;
    Virtual<void>(Virtual<void*>(automaton, 0x14), 0x5C, group);
    CallThis<void>(addr::cSC4CitySituationManager_TrackAutomaton, self, automaton);
}

// 0x005252D0, slot 35 (ret 4): on, +0xA98 1 and the U-Drive started for the
// active automaton (+0xA4C) with the +4 part; off, the U-Drive's view left
// when it is the view (0x00B43DC4's slot 24, class 0x2B709676).
void __fastcall SetUDrive(char* self, void*, std::uint8_t on)
{
    static bool logged;
    FirstCall("SetUDrive", logged);
    if (on) {
        At<std::uint8_t>(self, field::kJoyride) = 1;
        CallCdecl<void>(addr::SC4_ParkAutomaton, At<void*>(self, field::kAuto), static_cast<void*>(self + 4));
        return;
    }
    void* const views = Global<void*>(addr::SC4_Service43DC4);
    if (!views)
        return;
    void* const view = Virtual<void*>(views, 0x60);
    if (view && Virtual<std::uint32_t>(view, 0x14) == 0x2B709676u)
        Virtual<void>(views, 0x68, 0u);
}

// 0x005276A0, slot 37: the drive of a started situation.
void __fastcall BeginDrive(char* self, void*)
{
    static bool logged;
    FirstCall("BeginDrive", logged);
    void* const situation = At<void*>(self, field::kSituation);
    if (!situation)
        return;
    const std::uint32_t ref = At<std::uint32_t>(self, field::kRefAF0);
    At<std::uint32_t>(self, field::kStatus) = 3;
    At<std::uint32_t>(self, field::kDriveTime) = 0;
    At<std::uint8_t>(self, field::kByteA99) = 0;
    const std::uint32_t table = Virtual<std::uint32_t>(situation, 0x78);
    CallThis<void>(addr::cSC4CitySituationManager_RunScript, self, table, ref);
    Virtual<void>(At<void*>(self, field::kSituation), 0xDC);
    if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0x3C, 2u)) {
        void* const controllers = Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC);
        void* const cars = Global<void*>(addr::SC4_Service43D44);
        if (controllers && cars) {
            const Vector* const groups = Virtual<const Vector*>(At<void*>(self, field::kSituation), 0x48);
            std::uint32_t* const end = groups->end;
            std::uint32_t* at = groups->begin;
            if (at != end) {
                Vector chosen{};
                for (; at != end; ++at)
                    if (Virtual<std::uint8_t>(cars, 0x78, *at))
                        CallThis<void>(addr::SC4PtrVector_PushBack, static_cast<void*>(&chosen), at);
                if (chosen.begin != chosen.end) {
                    void* const occupant = Virtual<void*>(At<void*>(self, field::kAuto), 0x14);
                    void* const controller =
                        Virtual<void*>(controllers, 0x14, occupant,
                                       reinterpret_cast<void*>(addr::cSC4CitySituationManager_ControllerKey), 0u);
                    if (controller)
                        Virtual<void>(controller, 0x40, static_cast<void*>(&chosen));
                }
                if (chosen.begin)
                    Free(chosen.begin);
            }
        }
    }
    const float limit = CallThis<float>(addr::cSC4CitySituationManager_ComputeTimeLimit, self);
    At<float>(self, field::kTimeLimit) = limit;
    At<std::uint32_t>(self, field::kSituationTime) = 0;
    CallThis<void>(addr::cSC4CitySituationManager_SendSituationMessage, self, 0xABBBCA64u,
                   At<void*>(self, field::kSituation), 1u);
    Virtual<void>(self, slot::kSetUDrive, 1u);
    At<std::uint8_t>(self, field::kJoyride) = 1;
}

// 0x00529ED0, slot 39: the situation stopped.
void __fastcall StopSituation(char* self, void*)
{
    static bool logged;
    FirstCall("StopSituation", logged);
    Drop(self, field::kRefA9C);
    At<std::uint32_t>(self, field::kTimeLimit) = 0;
    if (void* const helper = At<void*>(self, field::kRefAA0)) {
        Virtual<void>(helper, 0x10, 1u);
        Drop(self, field::kRefAA0);
    }
    if (void* const situation = At<void*>(self, field::kSituation)) {
        if (At<void*>(self, field::kAuto)) {
            char* const head = At<char*>(self, field::kList);
            char* node = At<char*>(head, 0);
            void* holder[3];
            void** const held = CallThis<void**>(addr::cSC4CitySituationManager_HolderInit,
                                                 static_cast<void*>(holder), situation);
            if (node != head) {
                void* const wanted = held[0];
                while (At<void*>(node, 8) != wanted) {
                    node = At<char*>(node, 0);
                    if (node == head)
                        break;
                }
            }
            if (holder[1])
                Release(holder[1]);
            if (holder[0])
                Release(holder[0]);
            if (node != At<char*>(self, field::kList)) {
                CallThis<void>(addr::cSC4CitySituationManager_ShowAutomatonCsi, self, At<void*>(self, field::kAuto),
                               static_cast<void*>(node + 0x10), 0u);
                Drop(node, 0xC);
            }
        }
        Virtual<void>(self, slot::kEndSituation);
    }
    if (void* const overlay = At<void*>(self, field::kOverlay))
        CallThis<void>(addr::SC4OverlayManager_F46D3A0, overlay, 4u,
                       static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kCsiVisible)));
}

// ====================================================================================
// The helpers

// 0x005245F0 (ret 4, this unused): an occupant with flag 2 and none of 1,
// 0x8000000 and 0x10000000 (slot 31), whose slot 59 is neither 0x34 nor
// 0x14.
std::uint8_t __fastcall IsCandidate(void*, void*, void* occupant)
{
    static bool logged;
    FirstCall("IsCandidate", logged);
    if (!occupant)
        return 0;
    if (!Virtual<std::uint8_t>(occupant, 0x7C, 2u))
        return 0;
    if (Virtual<std::uint8_t>(occupant, 0x7C, 1u))
        return 0;
    if (Virtual<std::uint8_t>(occupant, 0x7C, 0x8000000u))
        return 0;
    if (Virtual<std::uint8_t>(occupant, 0x7C, 0x10000000u))
        return 0;
    const std::uint32_t kind = Virtual<std::uint32_t>(occupant, 0xEC);
    if (kind == 0x34 || kind == 0x14)
        return 0;
    return 1;
}

// 0x00525640 (ret 8): each group to each service (+0x9F8) that handles it
// (slot 30), whose slot 32 adds its occupants to the set; whether the set
// holds any.
std::uint8_t __fastcall GatherCandidates(char* self, void*, const Vector* groups, char* set)
{
    static bool logged;
    FirstCall("GatherCandidates", logged);
    const std::uint32_t services = ServiceCount(self);
    const std::uint32_t count = Count(groups);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t group = groups->begin[i];
        for (std::uint32_t j = 0; j < services; ++j) {
            void* const service = Services(self)[j];
            if (Virtual<std::uint8_t>(service, 0x78, group))
                Virtual<void>(service, 0x80, group, static_cast<void*>(set));
        }
    }
    return At<std::uint32_t>(set, map::kCount) != 0 ? 1 : 0;
}

// 0x005292F0 (ret 4): an automaton the services make (slot 15, after slot
// 16 finds a place within 64 of the site) for a group, near a site of the
// group's class (0x00528360); the last one found, released, or null.
void* __fastcall CreateCandidate(char* self, void*, const Vector* groups)
{
    static bool logged;
    FirstCall("CreateCandidate", logged);
    void* best = nullptr;
    void* const finder = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC0);
    if (!finder)
        return best;
    const std::uint32_t count = Count(groups);
    // Never freed, as the game's: the services' slot 15 may fill it.
    Vector made{};
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t group = groups->begin[i];
        char* const found = Virtual<char*>(finder, 0x18, group);
        std::uint32_t kind = 0;
        if (found) {
            kind = At<std::uint32_t>(found, 0x6C);
            if (!kind) {
                char* const parent = At<char*>(found, 0xC);
                kind = parent ? CallThis<std::uint32_t>(addr::cSC4AutomataManager_GroupClass, parent) : 0u;
            }
        }
        void* const site = CallThis<void*>(addr::cSC4CitySituationManager_PickSite, self, kind);
        if (!site)
            continue;
        AddRef(site);
        const std::uint32_t zero = ZeroBits();
        std::uint32_t from[3] = {zero, zero, zero};
        std::uint32_t to[3] = {zero, zero, zero};
        Virtual<void>(site, 0x20, static_cast<void*>(from));
        if (ServiceCount(self) != 0) {
            std::uint32_t j = 0;
            do {
                void* const service = Services(self)[j];
                if (Virtual<std::uint8_t>(service, 0x40, group, static_cast<void*>(from), 0u, 0x42800000u,
                                          static_cast<void*>(to), 0x2010002u, 0u)) {
                    made = Vector{};
                    void* const automaton = Virtual<void*>(service, 0x3C, group, static_cast<void*>(to), 0x2010002u,
                                                           static_cast<void*>(&made), 0u, 0xFFu, 0u);
                    if (automaton != best) {
                        void* const old = best;
                        if (automaton)
                            AddRef(automaton);
                        best = automaton;
                        if (old)
                            Release(old);
                    }
                }
                if (best)
                    break;
                ++j;
            } while (j < ServiceCount(self));
        }
        Release(site);
    }
    if (best)
        Release(best);
    return best;
}

// 0x00528360 (ret 4): for the class 0x193A the +0xAF8 holder's pick (its
// slot 11's slot 3); else a random occupant (0x00526380) of the class the
// network service finds in the camera's view (slot 36, no limit), else in
// the city (at most 10). Answered after its release, or null.
void* __fastcall PickSite(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("PickSite", logged);
    void* picked = nullptr;
    if (kind == 0x193A) {
        if (void* const holder = At<void*>(self, field::kRefAF8)) {
            if (void* const source = Virtual<void*>(holder, 0x2C)) {
                void* const object = Virtual<void*>(source, 0x0C);
                CallThis<void>(addr::SC4RefPtr_Assign, static_cast<void*>(&picked), object);
                if (picked) {
                    Release(picked);
                    return picked;
                }
            }
        }
    }
    Vector kinds{};
    // The game hands its argument's slot as both the value and the allocator.
    std::uint32_t slotValue = kind;
    CallThis<void>(addr::SC4PtrVector_InsertN, static_cast<void*>(&kinds), static_cast<void*>(nullptr), &slotValue,
                   &slotValue, 1u, 1u);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(0x28));
    void* const filter = block ? CallThis<void*>(addr::cSC4CitySituationManager_SpawnFilterConstruct, block) : nullptr;
    if (filter)
        AddRef(filter);
    Virtual<void>(filter, 0x20, static_cast<void*>(&kinds));
    void* const network = Global<void*>(addr::SC4_NetworkService);
    if (network)
        AddRef(network);
    char* list = nullptr;
    void* nodes = Global<void*>(addr::SC4List_NodeAllocator2);
    if (!nodes) {
        slotValue = 0;
        void* got = nullptr;
        if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
            Virtual<std::uint32_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, static_cast<void*>(&got));
        *reinterpret_cast<void**>(addr::SC4List_NodeAllocator2) = got;
        CallThis<void>(addr::SC4_ReleaseRef, static_cast<void*>(&got));
        nodes = Global<void*>(addr::SC4List_NodeAllocator2);
    }
    char* const sentinel = Virtual<char*>(nodes, 0x0C, 0xCu);
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    list = sentinel;
    if (!network) {
        CallThis<void>(addr::SC4List_Destroy2, static_cast<void*>(&list));
        Release(filter);
        if (kinds.begin)
            Free(kinds.begin);
        return nullptr;
    }
    std::uint32_t low[3] = {};
    std::uint32_t high[3] = {};
    CallThis<void>(addr::SC4CameraView_GetBounds, Global<void*>(addr::SC4_CameraView), static_cast<void*>(low),
                   static_cast<void*>(high));
    Virtual<void>(network, 0x90, static_cast<void*>(&list), static_cast<void*>(low), static_cast<void*>(high), filter,
                  0xFFFFFFFFu);
    bool any = At<char*>(list, 0) != list;
    if (!any) {
        Virtual<void>(network, 0x90, static_cast<void*>(&list), static_cast<void*>(nullptr),
                      static_cast<void*>(nullptr), filter, 10u);
        any = At<char*>(list, 0) != list;
    }
    if (any) {
        void* const one = CallThis<void*>(addr::cSC4CitySituationManager_PickRandom, self, static_cast<void*>(&list));
        CallThis<void>(addr::SC4RefPtr_Assign, static_cast<void*>(&picked), one);
        Virtual<void>(network, 0xC0, static_cast<void*>(&list));
    }
    CallThis<void>(addr::SC4List_Destroy2, static_cast<void*>(&list));
    Release(network);
    Release(filter);
    if (kinds.begin)
        Free(kinds.begin);
    if (picked)
        Release(picked);
    return picked;
}

// 0x00526380 (ret 4): a list's entry (+8) at an index from the manager's
// random (+0x28, SC4Random::Below with the count), or null.
void* __fastcall PickRandom(char* self, void*, char** list)
{
    static bool logged;
    FirstCall("PickRandom", logged);
    char* head = *list;
    std::uint32_t count = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        ++count;
    std::uint32_t index = CallThis<std::uint32_t>(addr::SC4Random_Below, self + field::kRandom, count);
    head = *list;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        if (index == 0)
            return At<void*>(node, 8);
        --index;
    }
    return nullptr;
}

// 0x00526610, the site filter's constructor (0x28 bytes): an
// SC4OccupantGroupFilter, the vtables 0x00A97DE4 and 0x00A97DCC, +0x18 0,
// +0x1C to +0x24 the zero float.
char* __fastcall SpawnFilterConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("SpawnFilterConstruct", logged);
    CallThis<void*>(addr::SC4OccupantGroupFilter_SC4OccupantGroupFilter, self, 0u);
    At<std::uintptr_t>(self, 0) = addr::cSC4CitySituationManager_SpawnFilterVtable;
    At<std::uintptr_t>(self, 4) = addr::cSC4CitySituationManager_SpawnFilterVtable4;
    At<std::uint32_t>(self, 0x18) = 0;
    At<std::uint32_t>(self, 0x1C) = ZeroBits();
    At<std::uint32_t>(self, 0x20) = ZeroBits();
    At<std::uint32_t>(self, 0x24) = ZeroBits();
    return self;
}

// 0x005291C0 (ret 4), slot 25's: the automaton's CSI on a list entry.
void __fastcall TrackAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("TrackAutomaton", logged);
    if (!CallThis<std::uint8_t>(addr::cSC4CitySituationManager_IsCandidate, self, automaton))
        return;
    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(automaton);
    const std::uint32_t tracked =
        CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Count, self + field::kTracked, static_cast<void*>(&key));
    // The game keeps the most in its argument's low byte.
    const std::uint8_t most = Virtual<std::uint8_t>(self, slot::kGetMaxCsi);
    char* const head = At<char*>(self, field::kList);
    std::uint32_t shown = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const current = At<void*>(node, 0xC);
        if (current == automaton)
            return;
        if (current) {
            ++shown;
            if (shown >= most)
                return;
            continue;
        }
        if (!Virtual<std::uint8_t>(At<void*>(node, 8), 0xD4, automaton))
            continue;
        if (!tracked) {
            CallThis<void>(addr::cSC4CitySituationManager_ShowAutomatonCsi, self, automaton,
                           static_cast<void*>(node + 0x10), 1u);
            CallThis<void>(addr::SC4RefPtr_Assign, static_cast<void*>(node + 0xC), automaton);
            return;
        }
        for (char* other = At<char*>(node, 0); other != head; other = At<char*>(other, 0)) {
            if (At<void*>(other, 0xC) != automaton)
                continue;
            if (CallThis<std::uint8_t>(addr::cSC4CitySituationManager_ComparePriority, other + 8,
                                       static_cast<void*>(node + 8))) {
                CallThis<void>(addr::SC4RefPtr_Assign, static_cast<void*>(node + 0xC), automaton);
                At<std::uint32_t>(node, 0x10) = At<std::uint32_t>(other, 0x10);
                Drop(other, 0xC);
                At<std::uint32_t>(other, 0x10) = 0;
                return;
            }
        }
        return;
    }
}

// 0x00524A50 (ret 4), this and the argument point at list entries' +8:
// whether the first's priority (slot 3's slot 7) is below the second's; a
// null first is below a set second.
std::uint8_t __fastcall ComparePriority(void** first, void*, void** second)
{
    static bool logged;
    FirstCall("ComparePriority", logged);
    if (!*first)
        return *second ? 1 : 0;
    if (!*second)
        return 0;
    void* const a = Virtual<void*>(*first, 0x0C);
    void* const b = Virtual<void*>(*second, 0x0C);
    const std::int32_t mine = Virtual<std::int32_t>(b, 0x1C);
    const std::int32_t theirs = Virtual<std::int32_t>(a, 0x1C);
    return theirs < mine ? 1 : 0;
}

// 0x00528ED0 (ret 0xC): with the overlay manager (the dispatch service's
// slot 22), shown, an icon of the automaton's template (its slot 67's +0x68,
// else inherited) added (layer 4) into *overlayId, and the automaton tracked
// (+0xA08), given the +4 part (slot 39) and referenced when it was not;
// hidden, the icon removed and the tracked automaton untracked (slot 40) and
// released.
void __fastcall ShowAutomatonCsi(char* self, void*, void* automaton, std::uint32_t* overlayId, std::uint8_t show)
{
    static bool logged;
    FirstCall("ShowAutomatonCsi", logged);
    void* const overlay = Virtual<void*>(Global<void*>(addr::SC4_DispatchService), 0x58);
    if (!overlay || !automaton)
        return;
    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(automaton);
    char* const tracked = self + field::kTracked;
    if (show) {
        char* const holder = Virtual<char*>(automaton, 0x10C);
        if (!holder)
            return;
        void* model = At<void*>(holder, 0x68);
        if (!model) {
            char* const parent = At<char*>(holder, 0xC);
            if (!parent)
                return;
            model = CallThis<void*>(addr::cSC4CitySituationManager_InheritedTemplate, parent);
        }
        if (!model)
            return;
        void* const occupant = Virtual<void*>(automaton, 0x14);
        *overlayId = CallThis<std::uint32_t>(addr::SC4OverlayManager_Add, overlay, occupant, 4u, model, 0u);
        if (CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Count, tracked, static_cast<void*>(&key)))
            return;
        alignas(4) char inserted[0xC] = {};
        CallThis<void>(addr::SC4Uint32HashSet_InsertUnique, tracked, static_cast<void*>(inserted),
                       static_cast<void*>(&key));
        Virtual<void>(automaton, 0x9C, self ? static_cast<void*>(self + 4) : nullptr);
        AddRef(automaton);
        return;
    }
    if (!*overlayId)
        return;
    CallThis<void>(addr::SC4OverlayManager_Remove, overlay, *overlayId, 0u);
    *overlayId = 0;
    if (!CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Count, tracked, static_cast<void*>(&key)))
        return;
    CallThis<void>(addr::SC4Uint32HashSet_Erase, tracked, static_cast<void*>(&key));
    Virtual<void>(automaton, 0xA0, self ? static_cast<void*>(self + 4) : nullptr);
    Release(automaton);
}

// 0x005245A0 (this = an automaton group): the first +0x68 up the parents
// (+0xC), or null.
void* __fastcall InheritedTemplate(char* group)
{
    static bool logged;
    FirstCall("InheritedTemplate", logged);
    for (;;) {
        if (void* const model = At<void*>(group, 0x68))
            return model;
        group = At<char*>(group, 0xC);
        if (!group)
            return nullptr;
    }
}

// 0x00524A00 (ret 4): a holder of three words, the object referenced in the
// first, the second dropped, the third zeroed; answers the holder.
void** __fastcall HolderInit(void** holder, void*, void* object)
{
    static bool logged;
    FirstCall("HolderInit", logged);
    holder[0] = nullptr;
    holder[1] = nullptr;
    void* const old = holder[0];
    if (object != old) {
        if (object)
            AddRef(object);
        holder[0] = object;
        if (old)
            Release(old);
    }
    if (void* const second = holder[1]) {
        holder[1] = nullptr;
        Release(second);
    }
    holder[2] = nullptr;
    return holder;
}

// 0x00526EF0: the time limit, the situation's script (+0xAF4, then the
// situation's slot 30 table) called with the distance (0x00526A20 with 1)
// and the automaton's speed (its template's inherited 64's +8, else
// 0.022222225) times 0x00A8FAF0; its number when it answers one, else
// 0x00A9412C.
float __fastcall ComputeTimeLimit(char* self, void*)
{
    static bool logged;
    FirstCall("ComputeTimeLimit", logged);
    float limit = Global<float>(addr::cSC4CitySituationManager_DefaultTimeLimit);
    void* const situation = At<void*>(self, field::kSituation);
    if (!situation || !At<void*>(self, field::kAuto) || At<std::int32_t>(self, field::kTableRef) == -2 ||
        !At<void*>(self, field::kScriptContext))
        return limit;
    if (Virtual<std::uint8_t>(situation, 0x3C, 0x20u))
        if (void* const holder = At<void*>(self, field::kRefAF8))
            Virtual<void>(holder, 0x110);
    const std::uint32_t top = Virtual<std::uint32_t>(At<void*>(self, field::kScriptContext), 0x18);
    Virtual<void>(At<void*>(self, field::kScriptContext), 0xAC, At<std::uint32_t>(self, field::kTableRef));
    if (Virtual<std::uint8_t>(At<void*>(self, field::kScriptContext), 0x11C, 0xFFFFFFFFu)) {
        void* const lua = At<void*>(self, field::kScriptContext);
        const std::uint32_t table = Virtual<std::uint32_t>(At<void*>(self, field::kSituation), 0x78);
        Virtual<void>(lua, 0xAC, table);
        const double distance = CallThis<double>(addr::cSC4CitySituationManager_TargetDistance, self, 1u);
        Virtual<void>(At<void*>(self, field::kScriptContext), 0x7C, distance);
        std::uint32_t speedBits = 0x3CB60B62u; // 0.022222225f
        float speed;
        std::memcpy(&speed, &speedBits, sizeof speed);
        if (void* const holder = Virtual<void*>(At<void*>(self, field::kAuto), 0x10C))
            if (char* const model = CallThis<char*>(addr::SC4AutomatonTemplate_FindInherited64, holder))
                speed = At<float>(model, 8);
        const double scaled = static_cast<double>(speed) * static_cast<double>(Global<float>(addr::SC4_Float1000));
        Virtual<void>(At<void*>(self, field::kScriptContext), 0x7C, scaled);
        Virtual<void>(At<void*>(self, field::kScriptContext), 0xCC, 3u, 1u);
        if (Virtual<std::uint8_t>(At<void*>(self, field::kScriptContext), 0x38, 0xFFFFFFFFu))
            limit = static_cast<float>(Virtual<double>(At<void*>(self, field::kScriptContext), 0x5C, 0xFFFFFFFFu));
    }
    Virtual<void>(At<void*>(self, field::kScriptContext), 0x1C, top);
    return limit;
}

// sqrt((dz * dz + dy * dy) + dx * dx), as the x87 sums them.
double Distance3(double dx, double dy, double dz) { return std::sqrt((dz * dz + dy * dy) + dx * dx); }

std::uint8_t Intersect(const float* a, const float* b, const float* c, const float* d, float* out)
{
    return CallCdecl<std::uint8_t>(addr::SC4_SegmentsIntersect2D, a, b, c, d, out);
}

// 0x00526A20 (ret 4): the distance from the driver (the active automaton's
// position, slot 7, or 0x005256E0's) to the target, unrounded.
double __fastcall TargetDistance(char* self, void*, std::uint8_t fromAutomaton)
{
    static bool logged;
    FirstCall("TargetDistance", logged);
    const float fallback = Global<float>(addr::cSC4CitySituationManager_DefaultDistance);
    void* const automaton = At<void*>(self, field::kAuto);
    if (!automaton)
        return fallback;
    float from[3];
    float to[3];
    {
        const std::uint32_t zero = ZeroBits();
        const std::uint32_t bits[3] = {zero, zero, zero};
        std::memcpy(from, bits, sizeof from);
        std::memcpy(to, bits, sizeof to);
    }
    if (fromAutomaton)
        Virtual<void>(automaton, 0x1C, static_cast<void*>(from));
    else if (!CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GetDriverPosition, self, static_cast<void*>(from)))
        return Global<float>(addr::cSC4CitySituationManager_DefaultDistance);
    void* const target = At<void*>(self, field::kTarget);
    if (!target) {
        const std::uint32_t kind = Virtual<std::uint32_t>(At<void*>(self, field::kSituation), 0x10);
        if (kind == 2) {
            const std::int32_t w = Virtual<std::int32_t>(Global<void*>(addr::SC4_City), 0x174);
            const float width = static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(w) << 4));
            const std::int32_t h = Virtual<std::int32_t>(Global<void*>(addr::SC4_City), 0x178);
            const double height = static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(h) << 4));
            const double right = static_cast<double>(width) - static_cast<double>(from[0]);
            const float across = static_cast<double>(from[0]) < right ? from[0] : static_cast<float>(right);
            const double bottom = height - static_cast<double>(from[2]);
            const double down = static_cast<double>(from[2]) < bottom ? static_cast<double>(from[2]) : bottom;
            return static_cast<double>(across) < down ? static_cast<double>(across) : down;
        }
        if (kind == 5 && At<std::uint32_t>(self, field::kSetAA8Count) != 0) {
            const Iterator it = Begin(self + field::kSetAA8);
            const std::uint16_t x = At<std::uint16_t>(it.node, 4);
            const std::uint16_t z = At<std::uint16_t>(it.node, 6);
            const std::uint32_t zero = ZeroBits();
            std::uint32_t cell[3] = {zero, zero, zero};
            Virtual<void>(Global<void*>(addr::SC4_City), 0x184, static_cast<std::uint32_t>(x),
                          static_cast<std::uint32_t>(z), static_cast<void*>(&cell[0]), static_cast<void*>(&cell[2]));
            float cx, cz;
            std::memcpy(&cx, &cell[0], sizeof cx);
            std::memcpy(&cz, &cell[2], sizeof cz);
            const double none = static_cast<double>(Global<float>(addr::SC4_FloatZero));
            return Distance3(static_cast<double>(cx) - static_cast<double>(from[0]), none,
                             static_cast<double>(cz) - static_cast<double>(from[2]));
        }
        return fallback;
    }
    Virtual<void>(target, 0x20, static_cast<void*>(to));
    to[1] = from[1];
    const auto straight = [&]() {
        return Distance3(static_cast<double>(to[0]) - static_cast<double>(from[0]),
                         static_cast<double>(to[1]) - static_cast<double>(from[1]),
                         static_cast<double>(to[2]) - static_cast<double>(from[2]));
    };
    if (!Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0x3C, 0x10u))
        return straight();
    void* const lots = At<void*>(self, field::kLotManager);
    if (!lots)
        return fallback;
    std::int32_t cells[4] = {};
    if (char* const holder = At<char*>(self, field::kRefA58)) {
        const std::int32_t* const rect = At<const std::int32_t*>(holder, 0xC);
        if (!rect)
            return straight();
        std::memcpy(cells, rect, sizeof cells);
    } else {
        void* const lot = Virtual<void*>(lots, 0x38, target);
        if (!lot)
            return straight();
        Virtual<void>(lot, 0x38, static_cast<void*>(cells));
    }
    const auto scale = [](std::int32_t v) {
        return static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 4));
    };
    const float point[2] = {from[0], from[2]};
    const float aim[2] = {to[0], to[2]};
    const float low[2] = {scale(cells[0]), scale(cells[1])};
    const float high[2] = {scale(static_cast<std::int32_t>(static_cast<std::uint32_t>(cells[2]) + 1u)),
                           scale(static_cast<std::int32_t>(static_cast<std::uint32_t>(cells[3]) + 1u))};
    if (CallCdecl<std::uint8_t>(addr::SC4_PointInRect2D, point, low, high))
        return static_cast<double>(Global<float>(addr::SC4_FloatZero));
    float hit[2] = {};
    float a[2] = {low[0], low[1]};
    float b[2] = {high[0], low[1]};
    bool crossed = Intersect(point, aim, a, b, hit) != 0;
    if (!crossed) {
        a[0] = high[0];
        a[1] = high[1];
        crossed = Intersect(point, aim, b, a, hit) != 0;
    }
    if (!crossed) {
        b[0] = low[0];
        b[1] = high[1];
        crossed = Intersect(point, aim, a, b, hit) != 0;
    }
    if (!crossed) {
        a[0] = low[0];
        a[1] = low[1];
        crossed = Intersect(point, aim, b, a, hit) != 0;
    }
    if (!crossed)
        return fallback;
    return CallCdecl<double>(addr::SC4_Distance2D, static_cast<const float*>(hit), point);
}

// 0x005256E0 (ret 4): the driver's position into out: with the situation's
// flag 8 the driver's (+0xA9C, when its slot 5 is true and slot 6 false)
// transform's translation (slot 15), else the active automaton's position
// (slot 7). Whether one was written.
std::uint8_t __fastcall GetDriverPosition(char* self, void*, float* out)
{
    static bool logged;
    FirstCall("GetDriverPosition", logged);
    void* const situation = At<void*>(self, field::kSituation);
    if (!situation)
        return 0;
    if (!Virtual<std::uint8_t>(situation, 0x3C, 8u)) {
        void* const automaton = At<void*>(self, field::kAuto);
        if (!automaton)
            return 0;
        Virtual<void>(automaton, 0x1C, static_cast<void*>(out));
        return 1;
    }
    if (!At<void*>(self, field::kRefA9C))
        return 0;
    if (!Virtual<std::uint8_t>(At<void*>(self, field::kRefA9C), 0x14))
        return 0;
    if (Virtual<std::uint8_t>(At<void*>(self, field::kRefA9C), 0x18))
        return 0;
    struct Transform {
        std::uint8_t a;
        std::uint8_t b;
        std::uint8_t pad[2];
        float rotation[9];
        float translation[3];
        float scale;
    } transform{};
    const float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(transform.rotation, identity, sizeof identity);
    transform.scale = 1.0f;
    Virtual<void>(At<void*>(self, field::kRefA9C), 0x3C, static_cast<void*>(&transform));
    out[0] = X87Copy(transform.translation[0]);
    std::memcpy(&out[1], &transform.translation[1], 2 * sizeof(float));
    return 1;
}

// 0x00524D80 (ret 0xC, this unused): a message of the type, with the object
// (when set) and data 1, sent.
void __fastcall SendSituationMessage(void*, void*, std::uint32_t type, void* object, std::uint32_t data)
{
    static bool logged;
    FirstCall("SendSituationMessage", logged);
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, type);
    if (object)
        Virtual<void>(message, 0x84, object);
    Virtual<void>(message, 0x2C, data);
    Virtual<std::uint32_t>(MessageServer(), 0x0C, message);
    Release(message);
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

const Detour kLua[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaVariable, LuaVariable),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaGetSuccessCount, LuaGetSuccessCount),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaGetCurrentTarget, LuaGetCurrentTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaGetActiveAuto, LuaGetActiveAuto),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaShowCsi, LuaShowCsi),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaNumTargetsRemaining, LuaNumTargetsRemaining),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaActivateSituation, LuaActivateSituation),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_LuaGetTargetName, LuaGetTargetName),
};

const Detour kStarts[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_IsSituationAvailableById, IsSituationAvailableById),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_IsSituationAvailable, IsSituationAvailable),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_InitiateSituationByGuid, InitiateSituationByGuid),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_InitiateSituationForOccupant, InitiateSituationForOccupant),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_InitiateSituationByAutomataGroup, InitiateSituationByAutomataGroup),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_StartJoyride, StartJoyride),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TreatAutomatonAsGroup, TreatAutomatonAsGroup),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SetUDrive, SetUDrive),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_BeginDrive, BeginDrive),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_StopSituation, StopSituation),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_IsCandidate, IsCandidate),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GatherCandidates, GatherCandidates),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CreateCandidate, CreateCandidate),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_PickSite, PickSite),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_PickRandom, PickRandom),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SpawnFilterConstruct, SpawnFilterConstruct),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TrackAutomaton, TrackAutomaton),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ComparePriority, ComparePriority),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ShowAutomatonCsi, ShowAutomatonCsi),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_InheritedTemplate, InheritedTemplate),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_HolderInit, HolderInit),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ComputeTimeLimit, ComputeTimeLimit),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TargetDistance, TargetDistance),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetDriverPosition, GetDriverPosition),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SendSituationMessage, SendSituationMessage),
};

#undef OPENSC4_DETOUR

bool InstallLua(registry::Mode) { return InstallList(kLua, sizeof kLua / sizeof kLua[0], "lua"); }
bool InstallStarts(registry::Mode) { return InstallList(kStarts, sizeof kStarts / sizeof kStarts[0], "starts"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.situations.lua", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_LuaShowCsi,
                        "Our city situation manager's Lua callbacks (the table sitmgr): get_success_count, "
                        "get_current_target, get_active_auto, show_CSI, num_targets_remaining, activate_situation, "
                        "get_target_name and the variables' accessor (8 detours; part c2, "
                        "src/core/automata_situations_2.cpp)",
                        false, InstallLua}),
    registry::Register({"simulators.automata.situations.starts", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_InitiateSituationByGuid,
                        "Our city situation manager's starts: slots 19 to 25 (IsSituationAvailable, the three "
                        "Initiate, StartJoyride, TreatAutomatonAsGroup), 35, 37 and 39 (the U-Drive, the drive, the "
                        "stop) and their helpers (the candidates, the sites, the CSI of an automaton, the time "
                        "limit, the distance) (25 detours; part c2, src/core/automata_situations_2.cpp)",
                        false, InstallStarts}),
};

} // namespace
} // namespace opensc4::automata_situations
