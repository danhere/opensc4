// Our cSC4LandfillOccupant (build 641; the class map, the vtables slot by
// slot and the fields are in landfillocc_sim.h): the class's own slots in
// its five vtables, the save record's Write and Read, and our own object
// (the two constructors, the factory and the pool allocation by detour, the
// deleting destructor and identity from copies of the vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor, AddRef,
// Release, Init, Shutdown, SetFlag, Write, Read) is every occupant class's
// (src/core/occbase_sim.cpp): it is called at its address. The class's
// getters of one instruction (the orientation, the plain flag, the
// position) and its Shutdown are code the linker folded with other classes'
// vtables; they stay the game's. So do the pool class's page maker
// 0x004771D0 and item release 0x00477660 (the props' pool has them too).
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out; 0x005D1DB0, 0x005D1E30,
//     0x005D1E70, 0x005D1EB0) and 0x00AA2280 (in; 0x005D1960, 0x005D19C0,
//     0x005D19F0, 0x005D1A20): slots 0x14, 0x1C, 0x20, 0x24 one argument,
//     0x48 (0x005D16B0) none;
//   - the exemplar (cSCResExemplar 0x00AAF0D0): slot 0x2C (0x0067CD20,
//     which writes the 12 bytes of a resource key) one, slot 0x30
//     (0x0067C220) one, AddRef and Release (0x4, 0x8) none;
//   - AddRef of the object (slot 0x4, 0x005E28F0): none.
#include "core/landfillocc_sim.h"

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

namespace opensc4::landfillocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("landfillocc: first %s", name);
}

namespace {

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

// --- The box ------------------------------------------------------------------------

// 0x004773D0 (the object in ECX; Read calls it, SetPosition has the same
// code inline): the position less and plus 8 in x and z, from the
// position's y (copied as it is) up by 16. Each sum is made on the FPU and
// stored as a float.
void UpdateBox(char* self)
{
    const double half = static_cast<double>(Global<float>(addr::cSC4LandfillOccupant_FloatHalfTile));
    const double height = static_cast<double>(Global<float>(addr::cSC4LandfillOccupant_FloatHeight));
    const double x = static_cast<double>(At<float>(self, field::kPosition));
    At<float>(self, field::kBoxLow) = static_cast<float>(x - half);
    At<float>(self, field::kBoxHigh) = static_cast<float>(x + half);
    std::memcpy(self + field::kBoxLow + 4, self + field::kPosition + 4, 4);
    At<float>(self, field::kBoxHigh + 4) =
        static_cast<float>(static_cast<double>(At<float>(self, field::kPosition + 4)) + height);
    const double z = static_cast<double>(At<float>(self, field::kPosition + 8));
    At<float>(self, field::kBoxLow + 8) = static_cast<float>(z - half);
    At<float>(self, field::kBoxHigh + 8) = static_cast<float>(z + half);
}

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x00477130): the landfill interface is the object, 1 and the
// occupant interface the object + 4, the serializable interface the object
// + 8; each counted through the object's AddRef. Nothing else is answered:
// unlike the other occupant classes this one does not ask the occupant
// base, so neither the class ID nor the property holders' IIDs are known.
// (The game also has a branch for a null `this` that stores null and then
// reads the null object's vtable; no call can take it and return.)
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidLandfill)
        *object = self;
    else if (iid == 1 || iid == kIidOccupant)
        *object = self + field::kOccupant;
    else if (iid == kIidSerializable)
        *object = self + field::kSerializable;
    else
        return 0;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The other parts' slot 0 (0x00477210, 0x00477230, 0x00477240, 0x00477260).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 3 (0x004771B0): the occupant base's Init on the object + 4 (a jump
// in the game; its result is the result).
uint32_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    return CallThis<uint32_t>(addr::cSC4Occupant_Init, self + field::kOccupant);
}

// The occupant part's slot 3 (0x00477220): this - 4, Init.
uint32_t __fastcall InitAtOccupant(char* part, void*) { return Init(part - field::kOccupant, nullptr); }

// Slot 6 (0x00477450): the tile's ID. The same ID: nothing. Else the field
// is set, and the exemplar's resource key (three uint32, zeroed first, read
// by the exemplar's slot 0x2C) gets the ID as its instance and is given
// back (slot 0x30), between an AddRef and a Release of the exemplar. The
// exemplar is not tested, as in the game. The old ID is returned.
uint32_t __fastcall SetTileID(char* self, void*, uint32_t tile)
{
    static bool logged = false;
    FirstCall("SetTileID", logged);
    const uint32_t old = At<uint32_t>(self, field::kTile);
    if (old != tile) {
        uint32_t key[3] = {0, 0, 0};
        At<uint32_t>(self, field::kTile) = tile;
        Virtual<uint32_t>(At<void*>(self, field::kExemplar), 0x4);
        Virtual<uint8_t>(At<void*>(self, field::kExemplar), 0x2C, static_cast<uint32_t*>(key));
        key[2] = tile;
        Virtual<uint8_t>(At<void*>(self, field::kExemplar), 0x30, static_cast<uint32_t*>(key));
        Virtual<uint32_t>(At<void*>(self, field::kExemplar), 0x8);
    }
    return old;
}

// Slot 8 (0x004771C0): the orientation as given; AL is 1, the rest of EAX
// the argument's.
uint32_t __fastcall SetOrientation(char* self, void*, uint32_t orientation)
{
    static bool logged = false;
    FirstCall("SetOrientation", logged);
    At<uint32_t>(self, field::kOrientation) = orientation;
    return (orientation & 0xFFFFFF00u) | 1u;
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 9 (0x00477600): the position copied as it is, then the box (the
// code of 0x004773D0 inline). EAX is left holding the third float's bits.
uint32_t __fastcall SetPosition(char* part, void*, const uint32_t* position)
{
    static bool logged = false;
    FirstCall("SetPosition", logged);
    char* const self = part - field::kOccupant;
    At<uint32_t>(self, field::kPosition) = position[0];
    At<uint32_t>(self, field::kPosition + 4) = position[1];
    const uint32_t last = position[2];
    At<uint32_t>(self, field::kPosition + 8) = last;
    UpdateBox(self);
    return last;
}

// Slot 10 (0x00477420): the stored box, copied as it is. EAX is left
// holding the second pointer.
uint32_t* __fastcall GetBoundingBox(char* part, void*, uint32_t* low, uint32_t* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    for (std::size_t i = 0; i < 3; ++i)
        low[i] = At<uint32_t>(self, field::kBoxLow + 4 * i);
    for (std::size_t i = 0; i < 3; ++i)
        high[i] = At<uint32_t>(self, field::kBoxHigh + 4 * i);
    return high;
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x00477100).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

// +0x8 slot 3 (0x00477350; `part` = object + 8): version 3, the occupant
// base's record (when it fails: false at once), then the tile ID, the
// orientation, the position and the plain flag as a byte, whether it is not
// zero. The game puts that byte into the low byte of its argument's stack
// slot and pushes the slot, so the byte's argument has the stream
// pointer's other three bytes. The result is whether the stream is free of
// error. The layout is in tools/re/save_schemas/landfillocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (!CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream))
        return 0;
    const uint32_t plain = (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream)) & 0xFFFFFF00u) |
                           (At<uint8_t>(self, field::kPlain) != 0 ? 1u : 0u);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kTile));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kOrientation));
    CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kPosition);
    Virtual<uint8_t>(stream, 0x14, plain);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 4 (0x00477560; `part` = object + 8): version 3 only. The game
// reads the version into the stack slot of a pushed ECX (this part's
// address until the stream writes two bytes of it), so it starts with that
// value here. Another version: AL cleared, the rest of EAX as the stream's
// read left it. A failing base: false at once. Then the tile ID, the
// orientation, the position, and the flag's byte, read into the low byte of
// the argument's stack slot (the stream pointer's): the flag is whether
// the byte is 1. Then the box from the position, and the result is whether
// the stream is free of error.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return read & 0xFFFFFF00u;
    if (!CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream))
        return 0;
    uint32_t plain = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x24, self + field::kTile);
    Virtual<uint8_t>(stream, 0x20, self + field::kOrientation);
    CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kPosition);
    Virtual<uint8_t>(stream, 0x14, &plain);
    At<uint8_t>(self, field::kPlain) = static_cast<uint8_t>(plain) == 1 ? 1 : 0;
    UpdateBox(self);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x004774B0, 0x004776A0): this - 4.
uint32_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }
uint32_t __fastcall ReadThunk(char* holder, void*, void* stream) { return Read(holder - 0x4, nullptr, stream); }

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable, 0, addr::cSC4LandfillOccupant_QueryInterface, QueryInterface),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable, 3, addr::cSC4LandfillOccupant_Init, Init),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable, 6, addr::cSC4LandfillOccupant_SetTileID, SetTileID),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable, 8, addr::cSC4LandfillOccupant_SetOrientation, SetOrientation),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_4, 0, addr::cSC4LandfillOccupant_QueryInterfaceThunk4,
                 QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_4, 3, addr::cSC4LandfillOccupant_InitThunk4, InitAtOccupant),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_4, 9, addr::cSC4LandfillOccupant_SetPosition, SetPosition),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_4, 10, addr::cSC4LandfillOccupant_GetBoundingBox,
                 GetBoundingBox),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_8, 0, addr::cSC4LandfillOccupant_QueryInterfaceThunk8,
                 QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_8, 5, addr::cSC4LandfillOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_C, 0, addr::cSC4LandfillOccupant_QueryInterfaceThunkC,
                 QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_20, 0, addr::cSC4LandfillOccupant_QueryInterfaceThunk20,
                 QueryInterfaceAt<0x20>),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_8, 3, addr::cSC4LandfillOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_8, 4, addr::cSC4LandfillOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_C, 23, addr::cSC4LandfillOccupant_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4LandfillOccupant_vftable_C, 24, addr::cSC4LandfillOccupant_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("landfillocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(slots, what))
        return false;
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("landfillocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

// None of the slots is called directly from outside the class (every direct
// call or jump to them is one of the class's own thunks; the class
// registration's call of GetGZCLSID at 0x00460832 runs before any
// replacement is installed) and no other vtable holds one of them, so they
// are slot swaps, without a detour.
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.landfillocc.slots", "simulators/landfillocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LandfillOccupant_vftable,
                        "Our cSC4LandfillOccupant slots: QueryInterface and the parts' thunks, Init and its thunk, "
                        "SetTileID, SetOrientation, SetPosition, GetBoundingBox, GetGZCLSID "
                        "(src/core/landfillocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.landfillocc.save", "simulators/landfillocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LandfillOccupant_vftable_8,
                        "Our cSC4LandfillOccupant Write and Read (the save record, version 3; "
                        "src/core/landfillocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 10, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable20[kSlots20];

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kOccupant) = g_vtable4;
    At<void**>(self, field::kSerializable) = g_vtable8;
    At<void**>(self, field::kHolder) = g_vtableC;
    At<void**>(self, field::kExemplarPart) = g_vtable20;
}

// An item of the landfill occupants' pool (0x00477320, and inline in the
// factory): the current page's free head, the pool refilled when the page
// has none (0x0090CF9A returns the new head); the page is read again after.
// The item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4LandfillOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x004776F0): the class's vtables, the occupant
// base destroyed, and with flag 1 the memory back to the pool (the page
// header is at the address with its low 13 bits clear).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
    if (flags & 1) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFE000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4LandfillOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x4000003A)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x00477250).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LandfillOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4LandfillOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4LandfillOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4LandfillOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4LandfillOccupant_vftable_20),
                sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("landfillocc object: vtables copied (slots 6/8 %p %p, +4 slots 9/10 %p %p, +8 slots 3/4 %p %p)",
               g_vtable[6], g_vtable[8], g_vtable4[9], g_vtable4[10], g_vtable8[3], g_vtable8[4]);
}

// What both constructors share: the occupant base constructed at its
// address on the object + 4, our vtables, and the position and the box (the
// nine floats +0x38 to +0x58) set to the float at 0x00B21668, which the
// game reads for each. +0x65 to +0x67 are left as the pool's memory holds
// them, as in the game.
void ConstructCommon(char* self)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    SetVtables(self);
    for (std::size_t offset = field::kPosition; offset < field::kTile; offset += 4)
        At<uint32_t>(self, offset) = Global<uint32_t>(addr::cSC4LandfillOccupant_FloatZero);
    static int created = 0;
    if (created++ < 3)
        log::Write("landfillocc object: constructed %p", static_cast<void*>(self));
}

// The constructor with arguments (detour of 0x00477270; the landfill zone's
// new occupant): the tile ID, the orientation, the flag clear, the occupant
// type, then the base's SetFlag(4) on the occupant part (at its address;
// when the flags change it calls the part's slot 30, SetAllFlags).
void* __fastcall Construct(char* self, void*, uint32_t tile, uint32_t orientation)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    ConstructCommon(self);
    At<uint32_t>(self, field::kOrientation) = orientation;
    At<uint32_t>(self, field::kTile) = tile;
    At<uint8_t>(self, field::kPlain) = 0;
    At<uint32_t>(self, field::kType) = kOccupantType;
    CallThis<uint32_t, uint32_t>(addr::cSC4Occupant_SetFlag, self + field::kOccupant, 4u);
    return self;
}

// The constructor without arguments (detour of 0x004774C0; a save's
// occupant before its Read): orientation 0, the flag clear, the tile ID
// 0x02F10000, the occupant type. No SetFlag.
void* __fastcall ConstructDefault(char* self, void*)
{
    static bool logged = false;
    FirstCall("ConstructDefault", logged);
    ConstructCommon(self);
    At<uint32_t>(self, field::kOrientation) = 0;
    At<uint8_t>(self, field::kPlain) = 0;
    At<uint32_t>(self, field::kTile) = kDefaultTile;
    At<uint32_t>(self, field::kType) = kOccupantType;
    return self;
}

// The pool allocator (detour of 0x00477320; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x004776B0): an item of the pool, constructed
// without arguments.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return ConstructDefault(static_cast<char*>(PoolAllocate()), nullptr);
}

using ConstructFn = void*(__fastcall*)(char*, void*, uint32_t, uint32_t);
using ConstructDefaultFn = void*(__fastcall*)(char*, void*);
using AllocateFn = void*(__cdecl*)(std::size_t);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
ConstructDefaultFn g_originalConstructDefault = nullptr;
AllocateFn g_originalAllocate = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer when a save is read; its jump at 0x004776DC is the
// only way to the constructor without arguments) and the landfill zone's
// 0x006BC817 / 0x006BC82F, which reads only EAX after the allocator (add
// esp, test eax, then ECX and EDX loaded from the stack) and after the
// constructor (mov esi, eax, then EAX, ECX and EDX loaded again) and keeps
// nothing in ECX or EDX across them; our own landfill zone
// (src/core/landfillzone_cells.cpp) calls both at their addresses. The
// deleting destructor is reached through the holder part's slot 22 only, so
// the vtable copy replaces it without a detour; occupants made before this
// group is installed (there are none at PostAppInit) would keep the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::cSC4LandfillOccupant_cSC4LandfillOccupant);
    const addr::Entry* const constructDefault = addr_check::Find(addr::cSC4LandfillOccupant_ConstructDefault);
    const addr::Entry* const allocate = addr_check::Find(addr::cSC4LandfillOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4LandfillOccupant_Create);
    return construct && constructDefault && allocate && create &&
           hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*constructDefault, &ConstructDefault, &g_originalConstructDefault) &&
           hook::Install(*allocate, &Allocate, &g_originalAllocate) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.landfillocc.object",
    "simulators/landfillocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandfillOccupant_cSC4LandfillOccupant,
    "Our cSC4LandfillOccupant object: the two constructors, the factory and the pool allocation (detours), the "
    "deleting destructor, identity; methods from copies of the (patched) game vtables taken once "
    "(src/core/landfillocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::landfillocc_sim
