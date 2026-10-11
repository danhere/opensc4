// Our cSC4Occupant (build 641; src/core/occbase_sim.h has the class map and
// the fields): every function of the occupant base class, each a detour at
// the game's address, because 57 occupant classes hold these functions in
// their vtables and call them directly.
//
// Every function here was written from the disassembly (0x005E8970 to
// 0x005E95AD and the seven functions laid out elsewhere). `self` is the
// base's own `this` (the cISC4Occupant part) except in Write and Read, which
// get the serializable part (`self` + 4).
//
// Virtual calls with arguments, each count from the callee's own ret N:
//   - the occupant's slots (all 57 vtables of IID 0x26BB767E): 0x28
//     GetBoundingBox two (18 functions), 0x3C PostOccupantMessage two (4),
//     0x44 SetHighlight two (5), 0x78 SetAllFlags one (8), 0x80 IsFlagSet
//     one; 0x04, 0x08, 0x14, 0x18, 0x1C, 0x40 none;
//   - the holder's (57 vtables of IID 0x25216283): 0x24 GetProperty one (3
//     functions), 0x58 the deleting destructor one (38 thunks);
//   - a property (0x00AA6988, 0x00AA6BE8): 0x14 none; a variant (0x00AA6788,
//     0x00AA69E8, 0x00ACECD0): 0x10 and 0x19C none;
//   - the message (0x00ACEC00): 0x14, 0x30, 0x40, 0x54, 0x68 one, 0x84 one
//     (0x0091346B jumps to 0x005447B0, ret 4), 0x04 and 0x08 none;
//   - the message server (0x00A8CAFC, 0x00AD1BF4): 0x0C one, 0x10 two;
//   - the save streams (0x00AA2310 out, 0x00AA2280 in): 0x14, 0x1C, 0x24
//     one, 0x48 none;
//   - the resource manager (0x00AD89D0): 0x0C five; the exemplar
//     (0x00AAF0D0): 0x1C and 0x08 none;
//   - the placeable object (0x00AA4468): 0x04 and 0x08 none.
// Direct callees: 0x005FD310 (ret 8), 0x005FE070 and 0x005FF420 (ret 4),
// 0x005FF1C0 and 0x005FF220 (ret), 0x0051CA60 (ret 0x14), 0x0046F820 (ret
// 4), 0x0049A050 (ret 8; writes an 8-byte pair), 0x00414040 (ret 0x10),
// and the cdecl helpers 0x005FF510, 0x005FE6B0, 0x004696C0, 0x005EBDF0.
//
// Register use of the callers: every direct call and jump to these
// functions in the exe was read (215 sites, and the 3 direct calls of the
// 120 thunks that lead to them): none keeps a value in EAX, ECX or EDX
// across the call or reads the flags after it, so the detours are plain
// functions.
#include "core/occbase_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::occbase_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("occbase: first %s", name);
}

namespace {

constexpr uint32_t kIidExemplar = 0xA52160F5;

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// The game's vector of uint32 (begin, end, capacity).
struct Uint32Vector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// A new cGZMessage (0x2C bytes). Like the game, no test of the allocation
// before the first call through it.
void* NewMessage()
{
    void* const memory = CallCdecl<void*, std::size_t>(addr::RZ_operator_new, 0x2C);
    return memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// The occupants' copy of the message server pointer, taken on first use.
void CacheMessageServer()
{
    void*& server = Global<void*>(addr::cSC4Occupant_MessageServer);
    if (!server)
        server = Global<void*>(addr::SC4_MessageServer);
}

// The four vtables of the base itself.
void SetVtables(char* self)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4Occupant_vftable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4Occupant_vftable_4;
    At<std::uintptr_t>(self, field::kHolder) = addr::cSC4Occupant_vftable_8;
    At<std::uintptr_t>(self, field::kExemplarPart) = addr::cSC4Occupant_vftable_1C;
}

// The game's reference assignment (inline in 0x005E91C0 and 0x0078DAF0):
// nothing for the same object; else the new one referenced first, stored,
// and the old one released.
void AssignPlaceable(char* self, void* object)
{
    void* const old = At<void*>(self, field::kPlaceable);
    if (object == old)
        return;
    if (object)
        Virtual<uint32_t>(object, 0x4);
    At<void*>(self, field::kPlaceable) = object;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// The x87 status word, for the one function whose EAX is that in the game.
__declspec(naked) void FpuStatusWord()
{
    __asm xor eax, eax
    __asm fnstsw ax
    __asm ret
}

// ====================================================================================
// simulators.occbase.flags: the flags, the removal counts, visibility, the
// occupant manager's cells and the small getters
// ====================================================================================

// Slot 5 (0x00458A40): bit 0 of the state, in all of EAX.
uint32_t __fastcall IsInitialized(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsInitialized", logged);
    return At<uint8_t>(self, field::kState) & 1u;
}

// Slot 16 (0x00458A50): the state's high four bits.
uint32_t __fastcall GetHighlight(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetHighlight", logged);
    return static_cast<uint32_t>(At<uint8_t>(self, field::kState)) >> 4;
}

// Slot 8 (0x005E8A20): the base has no position: zeros (z, y, x in the
// game's order). EAX keeps the argument.
uint32_t* __fastcall GetPosition(char*, void*, uint32_t* position)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    position[2] = 0;
    position[1] = 0;
    position[0] = 0;
    return position;
}

// Slot 10 (0x005E8A40): zeros in both corners. EAX keeps the second.
uint32_t* __fastcall GetBoundingBox(char*, void*, uint32_t* low, uint32_t* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    low[2] = 0;
    low[1] = 0;
    low[0] = 0;
    high[2] = 0;
    high[1] = 0;
    high[0] = 0;
    return high;
}

// Slot 12 (0x005E8A60): bit 0 of the argument's low byte counts +0x2D up,
// bit 1 counts +0x2C up (bytes, wrapping; the game writes both bytes as one
// word each time). No return value (EAX is the last word written or the
// two bytes read).
void __fastcall SetRemovalFlags(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SetRemovalFlags", logged);
    uint8_t one = At<uint8_t>(self, field::kRemoval1);
    uint8_t two = At<uint8_t>(self, field::kRemoval2);
    if (flags & 1) {
        ++one;
        At<uint16_t>(self, field::kRemoval2) = static_cast<uint16_t>(static_cast<uint16_t>(one) << 8 | two);
    }
    if (flags & 2) {
        ++two;
        At<uint16_t>(self, field::kRemoval2) = static_cast<uint16_t>(static_cast<uint16_t>(one) << 8 | two);
    }
}

// Slot 13 (0x005E8AA0): the same, counting down (no test for zero).
void __fastcall UnsetRemovalFlags(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("UnsetRemovalFlags", logged);
    uint8_t one = At<uint8_t>(self, field::kRemoval1);
    uint8_t two = At<uint8_t>(self, field::kRemoval2);
    if (flags & 1) {
        --one;
        At<uint16_t>(self, field::kRemoval2) = static_cast<uint16_t>(static_cast<uint16_t>(one) << 8 | two);
    }
    if (flags & 2) {
        --two;
        At<uint16_t>(self, field::kRemoval2) = static_cast<uint16_t>(static_cast<uint16_t>(one) << 8 | two);
    }
}

// Slot 14 (0x005E8AE0): with bit 0 of the argument's low byte whether
// +0x2D is zero, else with bit 1 whether +0x2C is zero, else true. The
// game sets all of EAX in the first two cases and only AL in the third.
uint32_t __fastcall CanRemove(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("CanRemove", logged);
    if (flags & 1)
        return At<uint8_t>(self, field::kRemoval1) == 0;
    if (flags & 2)
        return At<uint8_t>(self, field::kRemoval2) == 0;
    return 1;
}

// Slot 18 (0x005E8B10): flag 4 set or cleared; when that changed it and
// the second argument (a byte) is set, occupant message 0x99EF1145 through
// slot 15 (a tail jump in the game: its result is this function's).
uint32_t __fastcall SetVisibility(char* self, void*, uint32_t visible, uint32_t send)
{
    static bool logged = false;
    FirstCall("SetVisibility", logged);
    const uint8_t flags = At<uint8_t>(self, field::kFlags);
    if (static_cast<uint8_t>(visible)) {
        if (flags & kFlagVisible)
            return flags;
        At<uint8_t>(self, field::kFlags) = flags | kFlagVisible;
    } else {
        if (!(flags & kFlagVisible))
            return flags;
        At<uint8_t>(self, field::kFlags) = flags & static_cast<uint8_t>(~kFlagVisible);
    }
    if (!static_cast<uint8_t>(send))
        return 0;
    return Virtual<uint32_t>(self, slot::kPostOccupantMessage, kMessageChanged, 0u);
}

// Slot 29 (0x005E8B70): the flags with the argument's bits; when that
// differs (compared as 32 bits) the new value goes to SetAllFlags (slot
// 30, a tail jump).
uint32_t __fastcall SetFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SetFlag", logged);
    const uint32_t old = At<uint8_t>(self, field::kFlags);
    const uint32_t now = old | flags;
    if (now == old)
        return now;
    return Virtual<uint32_t>(self, slot::kSetAllFlags, now);
}

// Slot 30 (0x005E8B90): the low byte stored. AL is the byte.
uint32_t __fastcall SetAllFlags(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SetAllFlags", logged);
    At<uint8_t>(self, field::kFlags) = static_cast<uint8_t>(flags);
    return flags;
}

// Slot 31 (0x005E8BA0): the flags without the argument's bits, through
// SetAllFlags when that differs.
uint32_t __fastcall ClearFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("ClearFlag", logged);
    const uint32_t old = At<uint8_t>(self, field::kFlags);
    const uint32_t now = ~flags & old;
    if (now == old)
        return now;
    return Virtual<uint32_t>(self, slot::kSetAllFlags, now);
}

// Slot 32 (0x005E8BC0): whether every bit of the argument is set (bits
// above the byte never are); 0 or 1 in all of EAX.
uint32_t __fastcall IsFlagSet(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("IsFlagSet", logged);
    return (At<uint8_t>(self, field::kFlags) & flags) == flags ? 1 : 0;
}

// Slot 33 (0x005E8BE0).
uint32_t __fastcall GetFlags(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetFlags", logged);
    return At<uint8_t>(self, field::kFlags);
}

// Slot 25 (0x005E8BF0): false when the first byte is 0xFF (no cells), else
// the four bytes copied.
uint8_t __fastcall GetOccupantManagerBBox(char* self, void*, uint8_t* box)
{
    static bool logged = false;
    FirstCall("GetOccupantManagerBBox", logged);
    const uint8_t first = At<uint8_t>(self, field::kManagerBox);
    if (first == 0xFF)
        return 0;
    box[0] = first;
    box[1] = At<uint8_t>(self, field::kManagerBox + 1);
    box[2] = At<uint8_t>(self, field::kManagerBox + 2);
    box[3] = At<uint8_t>(self, field::kManagerBox + 3);
    return 1;
}

// Slot 26 (0x005E8C20): four bytes stored. No return value.
void __fastcall SetOccupantManagerBBox(char* self, void*, const uint8_t* box)
{
    static bool logged = false;
    FirstCall("SetOccupantManagerBBox", logged);
    At<uint8_t>(self, field::kManagerBox) = box[0];
    At<uint8_t>(self, field::kManagerBox + 1) = box[1];
    At<uint8_t>(self, field::kManagerBox + 2) = box[2];
    At<uint8_t>(self, field::kManagerBox + 3) = box[3];
}

// Slot 27 (0x005DEB40, "false, one argument") stays the game's: the linker
// folded it with every such method of the game (123 vtables, 74 of them
// not occupants'), so a detour there would run for classes that are not
// ours.

// ====================================================================================
// simulators.occbase.life: identity, construction, Init and Shutdown
// ====================================================================================

// The serializable part's slot 5 (0x005E8970).
uint32_t __fastcall GetGZCLSID(char*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// The constructor (0x005E9220): the holder constructed (0x005FF1C0), the
// removal counts and the state zeroed, the vtables, the type, flag 4, no
// placeable object, no cells (the first byte only; +0x29 to +0x2B stay as
// they are). The game stores two abstract vtables (0x00AA40B8 at +0 and
// 0x00A80784 at +4) before the holder's constructor, which reads neither
// and calls nothing; they are left out.
char* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    CallThis<void*>(addr::cSCPropertyHolder_Construct, self + field::kHolder);
    At<uint16_t>(self, field::kRemoval2) = 0;
    At<uint8_t>(self, field::kState) = 0;
    SetVtables(self);
    At<uint32_t>(self, field::kType) = kOccupantType;
    At<uint8_t>(self, field::kFlags) = kFlagVisible;
    At<void*>(self, field::kPlaceable) = nullptr;
    At<uint8_t>(self, field::kManagerBox) = 0xFF;
    return self;
}

// The destructor (0x005E8C40; every derived destructor ends in it): the
// base's vtables, the placeable object released, the holder destroyed
// (0x005FF220).
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    if (void* const placeable = At<void*>(self, field::kPlaceable))
        Virtual<uint32_t>(placeable, 0x8);
    CallThis<void>(addr::cSCPropertyHolder_Destroy, self + field::kHolder);
}

// The deleting destructor (0x005E9280; the holder part's slot 22 reaches it
// through the thunk 0x005E8C80): the same, then with flag 1 the memory
// freed (0x005E5620).
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    if (void* const placeable = At<void*>(self, field::kPlaceable))
        Virtual<uint32_t>(placeable, 0x8);
    CallThis<void>(addr::cSCPropertyHolder_Destroy, self + field::kHolder);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// Slot 3 (0x005E8C90): once, bit 0 of the state; then occupant message
// 0x99EF1140, or for an occupant read from a save (bit 1) SetHighlight(0,
// false) instead, which clears the saved highlight and tells of it. True.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (!Virtual<uint8_t>(self, slot::kIsInitialized)) {
        const uint8_t state = At<uint8_t>(self, field::kState) | kStateInitialized;
        At<uint8_t>(self, field::kState) = state;
        if (!(state & kStateRead))
            Virtual<uint32_t>(self, slot::kPostOccupantMessage, kMessageInit, 0u);
        else
            Virtual<uint32_t>(self, slot::kSetHighlight, 0u, 0u);
    }
    return 1;
}

// Slot 4 (0x005E8980): when initialized, bit 0 cleared and occupant
// message 0x99EF1141. True.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (Virtual<uint8_t>(self, slot::kIsInitialized)) {
        At<uint8_t>(self, field::kState) &= static_cast<uint8_t>(~kStateInitialized);
        Virtual<uint32_t>(self, slot::kPostOccupantMessage, kMessageShutdown, 0u);
    }
    return 1;
}

// Slot 0 (0x005E89C0): 1 and the occupant IID give the object, the
// serializable IID its +4 (null for a null object, as the game's cast),
// each referenced through slot 1; everything else is the holder's
// (0x005FD310 with the holder part, a tail jump).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == 1 || iid == kIidOccupant) {
        *object = self;
        Virtual<uint32_t>(self, slot::kAddRef);
        return 1;
    }
    if (iid == kIidSerializable) {
        *object = self ? self + field::kSerializable : nullptr;
        Virtual<uint32_t>(self, slot::kAddRef);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSCPropertyHolder_QueryInterface, self + field::kHolder, iid, object);
}

// Slot 1 (0x005E89B0): the count plus one, atomically (the game's helper
// 0x00933015 is a lock xadd); the new count.
uint32_t __fastcall AddRef(char* self, void*)
{
    static bool logged = false;
    FirstCall("AddRef", logged);
    return static_cast<uint32_t>(InterlockedExchangeAdd(&At<LONG>(self, field::kRefCount), 1) + 1);
}

// Slot 2 (0x005E8DF0): the count minus one; at zero the count is written
// again and the holder part's slot 22 (the deleting destructor of whatever
// the object is) runs with flag 1.
uint32_t __fastcall Release(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release", logged);
    const LONG count = InterlockedExchangeAdd(&At<LONG>(self, field::kRefCount), -1) - 1;
    if (count != 0)
        return static_cast<uint32_t>(count);
    At<LONG>(self, field::kRefCount) = 0;
    Virtual<void*>(self + field::kHolder, 0x58, 1u);
    return 0;
}

// ====================================================================================
// simulators.occbase.messages: messages, the highlight, the city cells and
// the placeable object
// ====================================================================================

// Slot 15 (0x005E9010): a message of type `id` with the occupant (as void
// 1 and as the object), its flag 4 (data 2), its type (data 3) and `data`
// (data 4). Message 0x99EF1146 of an occupant with a placeable object is
// sent at once (the server's slot 3); everything else is queued (slot 4).
// EAX is the message's Release.
uint32_t __fastcall PostOccupantMessage(char* self, void*, uint32_t id, uint32_t data)
{
    static bool logged = false;
    FirstCall("PostOccupantMessage", logged);
    CacheMessageServer();
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, id);
    Virtual<void>(message, 0x30, self);
    Virtual<void*>(message, 0x84, self);
    const uint8_t visible = Virtual<uint8_t>(self, slot::kIsFlagSet, 4u);
    Virtual<void>(message, 0x40, uint32_t{visible});
    const uint32_t type = Virtual<uint32_t>(self, slot::kGetType);
    Virtual<void>(message, 0x54, type);
    Virtual<void>(message, 0x68, data);
    if (id == kMessageSent && At<void*>(self, field::kPlaceable))
        Virtual<uint8_t>(Global<void*>(addr::cSC4Occupant_MessageServer), 0xC, message);
    else
        Virtual<uint8_t>(Global<void*>(addr::cSC4Occupant_MessageServer), 0x10, message, 0u);
    return Virtual<uint32_t>(message, 0x8);
}

// Slot 17 (0x005E90E0): nothing when the highlight is the same. Else the
// state's high four bits (byte arithmetic: the low four kept, the argument
// shifted in), and for an initialized occupant either occupant message
// 0x99EF1145 (second argument, a byte, set) or a queued message 0xE6E85114
// with the occupant, the old highlight (data 2) and the new (data 3).
uint32_t __fastcall SetHighlight(char* self, void*, uint32_t highlight, uint32_t now)
{
    static bool logged = false;
    FirstCall("SetHighlight", logged);
    const uint32_t old = Virtual<uint32_t>(self, slot::kGetHighlight);
    if (old == highlight)
        return old;
    At<uint8_t>(self, field::kState) = static_cast<uint8_t>((At<uint8_t>(self, field::kState) & 0xF) +
                                                           static_cast<uint8_t>(static_cast<uint8_t>(highlight) << 4));
    if (!Virtual<uint8_t>(self, slot::kIsInitialized))
        return 0;
    CacheMessageServer();
    if (static_cast<uint8_t>(now))
        return Virtual<uint32_t>(self, slot::kPostOccupantMessage, kMessageChanged, 0u);
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, kMessageHighlight);
    Virtual<void>(message, 0x30, self);
    Virtual<void*>(message, 0x84, self);
    Virtual<void>(message, 0x40, old);
    Virtual<void>(message, 0x54, highlight);
    Virtual<uint8_t>(Global<void*>(addr::cSC4Occupant_MessageServer), 0x10, message, 0u);
    return Virtual<uint32_t>(message, 0x8);
}

// The game's floor of a positive coordinate (inline, four times, in
// 0x005E8E20): the value minus 0.5 plus 1.5 * 2^23 (0x00AA40B4) stored as
// a float, whose bits minus that constant's (0x4B400000) are the value
// rounded to nearest; one more when the value equals that plus one (an
// exact whole number rounded down by the tie). Intermediates are doubles,
// as on the game's FPU (53 bits).
int32_t FloorOf(double value)
{
    const float shifted =
        static_cast<float>((value - static_cast<double>(Global<float>(addr::SC4_FloatHalf))) +
                           static_cast<double>(Global<float>(addr::cSC4Occupant_FloorMagic)));
    uint32_t bits;
    std::memcpy(&bits, &shifted, sizeof bits);
    const int32_t down = static_cast<int32_t>(bits + 0xB4C00000u);
    const int32_t up = static_cast<int32_t>(bits + 0xB4C00001u);
    return value == static_cast<double>(up) ? up : down;
}

// Slot 11 (0x005E8E20): the city cells (16 m) the bounding box (slot 10)
// covers, as four int32: low x, low z, high x, high z. Both corners move
// inwards by 0.01 (0x00A823A4) first: low + 0.01 and high - 0.01, so a box
// that ends on a cell's edge does not count the next cell. The x values
// stay on the FPU between the
// sum and the product; the z values are stored as floats in between.
// The game leaves the FPU status word of its last compare in EAX.
uint32_t __fastcall GetBoundingCityCells(char* self, void*, int32_t* cells)
{
    static bool logged = false;
    FirstCall("GetBoundingCityCells", logged);
    // The game's two corners are uninitialized stack; every GetBoundingBox
    // fills them. Zeros here.
    float low[3] = {}, high[3] = {};
    Virtual<void*>(self, slot::kGetBoundingBox, static_cast<float*>(low), static_cast<float*>(high));
    const double margin = Global<float>(addr::SC4Traffic_FerryCostScale); // 0.01f: a constant many classes share
    const double scale = Global<float>(addr::cSC4Occupant_CityCellScale); // 0.0625f
    const double lowX = static_cast<double>(low[0]) + margin;
    const float lowZ = static_cast<float>(static_cast<double>(low[2]) + margin);
    cells[0] = FloorOf(lowX * scale);
    cells[1] = FloorOf(static_cast<double>(lowZ) * scale);
    const double highX = static_cast<double>(high[0]) - margin;
    const float highZ = static_cast<float>(static_cast<double>(high[2]) - margin);
    cells[2] = FloorOf(highX * scale);
    cells[3] = FloorOf(static_cast<double>(highZ) * scale);
    return reinterpret_cast<uint32_t(__cdecl*)()>(&FpuStatusWord)();
}

// Slot 20 (0x005E91C0): the placeable object, made when there is none (an
// item of the pool 0x00B0A8C8 through 0x005EBDF0, whose size argument is
// not used, constructed by 0x005EC370) and assigned.
void* __fastcall GetOrCreatePlaceableObject(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetOrCreatePlaceableObject", logged);
    if (!At<void*>(self, field::kPlaceable)) {
        void* const memory = CallCdecl<void*, std::size_t>(addr::cSC43DPlaceableObject_Allocate, 0x30);
        void* const object = memory ? CallThis<void*>(addr::cSC43DPlaceableObject_Construct, memory) : nullptr;
        AssignPlaceable(self, object);
    }
    return At<void*>(self, field::kPlaceable);
}

// Slot 21 (0x0078DAF0). No return value.
void __fastcall SetPlaceableObject(char* self, void*, void* object)
{
    static bool logged = false;
    FirstCall("SetPlaceableObject", logged);
    AssignPlaceable(self, object);
}

// ====================================================================================
// simulators.occbase.groups: the occupant groups (property 0xAA1DD396)
// ====================================================================================

// The game's find (0x004696C0): the address of the value's argument slot
// goes in twice (the second is an unused tag).
uint32_t* Find(uint32_t* first, uint32_t* last, uint32_t* value)
{
    return CallCdecl<uint32_t*, uint32_t*, uint32_t*, uint32_t*, void*>(addr::SC4_FindUint32, first, last, value,
                                                                         static_cast<void*>(value));
}

// Slot 22 (0x005E92D0): whether the property's list (through the holder,
// slot 6: the occupant's own or its exemplar's) has the group. No test of
// the holder or of the property's value.
uint8_t __fastcall IsOccupantGroup(char* self, void*, uint32_t group)
{
    static bool logged = false;
    FirstCall("IsOccupantGroup", logged);
    void* const holder = Virtual<void*>(self, slot::kAsPropertyHolder);
    void* const property = Virtual<void*>(holder, 0x24, kPropertyGroups);
    if (!property)
        return 0;
    void* const variant = Virtual<void*>(property, 0x14);
    uint32_t* const first = Virtual<uint32_t*>(variant, 0x19C);
    const uint32_t count = Virtual<uint32_t>(variant, 0x10);
    uint32_t* const last = first + count;
    return Find(first, last, &group) != last;
}

// Slot 23 (0x005E9350): the list read into a vector (0x005FF510); when the
// group is not in it, appended (in place when there is room, else through
// the vector's insert 0x0051CA60), the list stored as the occupant's own
// property (0x005FE6B0) and occupant message 0xEBFF18A3 with the group.
// The vector is freed. No return value.
void __fastcall AddOccupantGroup(char* self, void*, uint32_t group)
{
    static bool logged = false;
    FirstCall("AddOccupantGroup", logged);
    void* const holder = Virtual<void*>(self, slot::kAsPropertyHolder);
    Uint32Vector list = {nullptr, nullptr, nullptr};
    CallCdecl<uint8_t, void*, uint32_t, Uint32Vector*>(addr::SC4_GetPropertyUint32Vector, holder, kPropertyGroups, &list);
    if (Find(list.begin, list.end, &group) == list.end) {
        if (list.end != list.capacity) {
            if (list.end)
                *list.end = group;
            ++list.end;
        } else {
            CallThis<void, uint32_t*, uint32_t*, void*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, &list, list.end,
                                                                             &group, static_cast<void*>(&group), 1u, 1u);
        }
        CallCdecl<uint8_t, void*, uint32_t, Uint32Vector*>(addr::SC4_SetPropertyUint32Vector, holder, kPropertyGroups,
                                                           &list);
        Virtual<uint32_t>(self, slot::kPostOccupantMessage, kMessageGroupAdded, group);
    }
    if (list.begin)
        CallCdecl<void, void*>(addr::SC4_Free, list.begin);
}

// Slot 24 (0x005E9410): the set emptied (its nodes freed by 0x0046F820),
// then every group of the list that is not zero and not yet in the set
// inserted (0x0049A050, which writes an 8-byte pair). Whether the set has
// anything.
uint8_t __fastcall GetOccupantGroups(char* self, void*, Set* set)
{
    static bool logged = false;
    FirstCall("GetOccupantGroups", logged);
    if (set->size != 0) {
        CallThis<void, TreeNode*>(addr::SC4Uint32Set_EraseNodes, set, set->header->parent);
        set->header->left = set->header;
        set->header->parent = nullptr;
        set->header->right = set->header;
        set->size = 0;
    }
    void* const holder = Virtual<void*>(self, slot::kAsPropertyHolder);
    Uint32Vector list = {nullptr, nullptr, nullptr};
    CallCdecl<uint8_t, void*, uint32_t, Uint32Vector*>(addr::SC4_GetPropertyUint32Vector, holder, kPropertyGroups, &list);
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<char*>(list.end) -
                                                                      reinterpret_cast<char*>(list.begin)) >> 2);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t group = list.begin[i];
        if (group == 0)
            continue;
        TreeNode* const header = set->header;
        TreeNode* found = header;
        for (TreeNode* node = header->parent; node;) {
            if (node->value < group) {
                node = node->right;
            } else {
                found = node;
                node = node->left;
            }
        }
        if (found == header || group < found->value) {
            uint32_t result[2];
            CallThis<void, uint32_t*, uint32_t*>(addr::SC4Uint32Set_Insert2, set, static_cast<uint32_t*>(result), &group);
        }
    }
    const uint8_t any = set->size != 0;
    if (list.begin)
        CallCdecl<void, void*>(addr::SC4_Free, list.begin);
    return any;
}

// The exemplar's list of the property, as the two functions below read it:
// the resource loaded (the resource manager's slot 3 with the exemplar
// IID), its holder (slot 7), the property, its value's data and count.
// False when the resource or the property is missing; `exemplar` is the
// caller's to release.
bool ExemplarGroups(void* key, void*& exemplar, uint32_t*& first, uint32_t& count)
{
    exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kIidExemplar, &exemplar, 0u, 0u))
        return false;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    void* const property = Virtual<void*>(holder, 0x24, kPropertyGroups);
    if (!property)
        return false;
    void* const variant = Virtual<void*>(property, 0x14);
    first = Virtual<uint32_t*>(variant, 0x19C);
    count = Virtual<uint32_t>(variant, 0x10);
    return true;
}

// 0x005E8F70 (cdecl; the tract developer's callers know it as
// SC4Exemplar::HasBuildingStyle): whether the exemplar at a resource key
// lists the group.
uint8_t __cdecl ExemplarHasGroup(void* key, uint32_t group)
{
    static bool logged = false;
    FirstCall("ExemplarHasGroup", logged);
    uint8_t found = 0;
    void* exemplar = nullptr;
    uint32_t* first = nullptr;
    uint32_t count = 0;
    if (ExemplarGroups(key, exemplar, first, count)) {
        for (uint32_t i = 0; i < count; ++i)
            if (first[i] == group) {
                found = 1;
                break;
            }
    }
    if (exemplar)
        Virtual<uint32_t>(exemplar, 0x8);
    return found;
}

// 0x005E9500 (cdecl; SC4::LoadUint32List to its callers): the vector
// emptied, then the groups the exemplar at a resource key lists appended
// (the vector's range insert 0x00414040; its tag is the address of the
// game's exemplar pointer). No return value.
void __cdecl ExemplarGroupList(void* key, Uint32Vector* list)
{
    static bool logged = false;
    FirstCall("ExemplarGroupList", logged);
    list->end = list->begin;
    void* exemplar = nullptr;
    uint32_t* first = nullptr;
    uint32_t count = 0;
    if (ExemplarGroups(key, exemplar, first, count))
        CallThis<void, uint32_t*, uint32_t*, uint32_t*, void*>(addr::SC4Uint32Vector_InsertRange, list, list->end, first,
                                                                first + count, static_cast<void*>(&exemplar));
    if (exemplar)
        Virtual<uint32_t>(exemplar, 0x8);
}

// ====================================================================================
// simulators.occbase.save: the base's part of every occupant's record
// ====================================================================================

// 0x005E8CD0 (`part` is the serializable part, the object + 4): version 4,
// the removal counts as one uint16, the highlight (slot 16) and the flags
// without 0x02 and 0x20 as bytes, the type, the four cell bytes, then the
// holder's record (0x005FE070). 1 when the stream has no error, in all of
// EAX. The game pushes the highlight in the low byte of its stream
// argument's slot: the other three bytes are the stream pointer's.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 4u);
    Virtual<uint8_t>(stream, 0x1C, uint32_t{At<uint16_t>(self, field::kRemoval2)});
    const uint32_t highlight = Virtual<uint32_t>(self, slot::kGetHighlight);
    Virtual<uint8_t>(stream, 0x14,
                     (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream)) & 0xFFFFFF00u) | (highlight & 0xFFu));
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kFlags)} & 0xFFFFFFDDu);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kType));
    for (std::size_t i = 0; i < 4; ++i)
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kManagerBox + i)});
    CallThis<uint8_t, void*>(addr::cSCPropertyHolder_Write, self + field::kHolder, stream);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// 0x005E8D60: the reverse. Any version but 4 is refused (false, nothing
// else read). The state becomes the saved highlight in the high bits with
// bit 1 (read from a save) and without bit 0. The game reads the version
// into the low word of a slot that holds `this` and the highlight into the
// low byte of its stream argument's slot: a stream that stored nothing
// would leave those.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(part));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 4)
        return 0;
    Virtual<uint8_t>(stream, 0x1C, &At<uint16_t>(self, field::kRemoval2));
    uint8_t highlight = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x14, &highlight);
    At<uint8_t>(self, field::kState) = static_cast<uint8_t>(static_cast<uint8_t>(highlight << 4) | kStateRead);
    Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, field::kFlags));
    Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kType));
    for (std::size_t i = 0; i < 4; ++i)
        Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, field::kManagerBox + i));
    CallThis<uint8_t, void*>(addr::cSCPropertyHolder_Read, self + field::kHolder, stream);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// A detour at each address (entry bytes checked by hook::Install). The
// originals are not called again.
template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("occbase %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    for (const Detour& detour : detours) {
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
    }
    log::Write("occbase %s: %u detours", what, static_cast<unsigned>(N));
    return true;
}

const Detour kFlags[] = {
    OPENSC4_DETOUR(addr::cSC4BuildingOccupant_IsInitialized, IsInitialized),
    OPENSC4_DETOUR(addr::cSC4BuildingOccupant_GetHighlight, GetHighlight),
    OPENSC4_DETOUR(addr::cSC4Occupant_GetPosition, GetPosition),
    OPENSC4_DETOUR(addr::cSC4Occupant_GetBoundingBox, GetBoundingBox),
    OPENSC4_DETOUR(addr::cSC4Occupant_SetRemovalFlags, SetRemovalFlags),
    OPENSC4_DETOUR(addr::cSC4Occupant_UnsetRemovalFlags, UnsetRemovalFlags),
    OPENSC4_DETOUR(addr::cSC4Occupant_CanRemove, CanRemove),
    OPENSC4_DETOUR(addr::cSC4Occupant_SetVisibility, SetVisibility),
    OPENSC4_DETOUR(addr::cSC4Occupant_SetFlag, SetFlag),
    OPENSC4_DETOUR(addr::cSC4Occupant_SetAllFlags, SetAllFlags),
    OPENSC4_DETOUR(addr::cSC4Occupant_ClearFlag, ClearFlag),
    OPENSC4_DETOUR(addr::cSC4Occupant_IsFlagSet, IsFlagSet),
    OPENSC4_DETOUR(addr::cSC4Occupant_GetFlags, GetFlags),
    OPENSC4_DETOUR(addr::cSC4Occupant_GetOccupantManagerBBox, GetOccupantManagerBBox),
    OPENSC4_DETOUR(addr::cSC4Occupant_SetOccupantManagerBBox, SetOccupantManagerBBox),
};

// The factory 0x005E9330 stays the game's (a relative call in its first
// five bytes); it ends in a jump to the constructor, ours here.
const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4Occupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Occupant_cSC4Occupant, Construct),
    OPENSC4_DETOUR(addr::cSC4Occupant_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4Occupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4Occupant_Init, Init),
    OPENSC4_DETOUR(addr::cSC4Occupant_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4Occupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4Occupant_AddRef, AddRef),
    OPENSC4_DETOUR(addr::cSC4Occupant_Release, Release),
};

const Detour kMessages[] = {
    OPENSC4_DETOUR(addr::cSC4Occupant_PostOccupantMessage, PostOccupantMessage),
    OPENSC4_DETOUR(addr::cSC4Occupant_SetHighlight, SetHighlight),
    OPENSC4_DETOUR(addr::cSC4Occupant_GetBoundingCityCells, GetBoundingCityCells),
    OPENSC4_DETOUR(addr::cSC4Occupant_GetOrCreatePlaceableObject, GetOrCreatePlaceableObject),
    OPENSC4_DETOUR(addr::cSC4Occupant_SetPlaceableObject, SetPlaceableObject),
};

const Detour kGroups[] = {
    OPENSC4_DETOUR(addr::cSC4Occupant_IsOccupantGroup, IsOccupantGroup),
    OPENSC4_DETOUR(addr::cSC4Occupant_AddOccupantGroup, AddOccupantGroup),
    OPENSC4_DETOUR(addr::cSC4Occupant_GetOccupantGroups, GetOccupantGroups),
    OPENSC4_DETOUR(addr::SC4Exemplar_HasBuildingStyle, ExemplarHasGroup),
    OPENSC4_DETOUR(addr::SC4_LoadUint32List, ExemplarGroupList),
};

const Detour kSave[] = {
    OPENSC4_DETOUR(addr::cSC4Occupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4Occupant_Read, Read),
};

bool InstallFlags(registry::Mode) { return InstallDetours(kFlags, "flags"); }
bool InstallLife(registry::Mode) { return InstallDetours(kLife, "life"); }
bool InstallMessages(registry::Mode) { return InstallDetours(kMessages, "messages"); }
bool InstallGroups(registry::Mode) { return InstallDetours(kGroups, "groups"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.occbase.flags", "simulators/occbase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Occupant_SetFlag,
                        "Our cSC4Occupant flags, removal counts, visibility, occupant manager cells and small getters "
                        "(16 detours; src/core/occbase_sim.h)",
                        false, InstallFlags}),
    registry::Register({"simulators.occbase.life", "simulators/occbase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Occupant_cSC4Occupant,
                        "Our cSC4Occupant constructor, destructors, Init, Shutdown, QueryInterface, AddRef, Release and "
                        "GetGZCLSID (9 detours; src/core/occbase_sim.h)",
                        false, InstallLife}),
    registry::Register({"simulators.occbase.messages", "simulators/occbase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Occupant_PostOccupantMessage,
                        "Our cSC4Occupant PostOccupantMessage, SetHighlight, GetBoundingCityCells and the placeable "
                        "object (5 detours; src/core/occbase_sim.h)",
                        false, InstallMessages}),
    registry::Register({"simulators.occbase.groups", "simulators/occbase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Occupant_IsOccupantGroup,
                        "Our cSC4Occupant occupant groups: IsOccupantGroup, AddOccupantGroup, GetOccupantGroups and the "
                        "two exemplar helpers (5 detours; src/core/occbase_sim.h)",
                        false, InstallGroups}),
    registry::Register({"simulators.occbase.save", "simulators/occbase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Occupant_Write,
                        "Our cSC4Occupant Write and Read, the base's part of every occupant's save record (2 detours; "
                        "src/core/occbase_sim.h)",
                        false, InstallSave}),
};

} // namespace
} // namespace opensc4::occbase_sim
