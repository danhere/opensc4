// Our cSC4FloraOccupant (build 641; the class map, the vtables slot by slot
// and the fields are in floraocc_sim.h): the class's own slots in its five
// vtables, the release of a flora type record, the save record's Write and
// Read, and our own object (the constructor, the factory and the pool
// allocation by detour, the deleting destructor and identity from copies of
// the vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Init, Shutdown, Write, Read) is every
// occupant class's (src/core/occbase_sim.cpp): it is called at its address.
// The class's getters and setters of one instruction (the flora type, the
// dates, the state, the position, the lot tag's setter) are code the linker
// folded with other classes' vtables; they stay the game's.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out; 0x005D1D80, 0x005D1DB0,
//     0x005D1E30, 0x005D1EB0) and 0x00AA2280 (in; 0x005D1930, 0x005D1960,
//     0x005D19C0, 0x005D1A20): slots 0x10, 0x14, 0x1C, 0x24 one argument,
//     0x48 (0x005D16B0) none;
//   - the flora simulator 0x00AB13C0: slot 0x1C (0x006B1040) one;
//   - this class's primary vtable: slot 0x38 (0x00472CD0) one; its exemplar
//     part 0x00A884E8: slot 0xC (0x00458E60) one;
//   - AddRef and Release (slots 0x4 and 0x8 of the object and of the
//     exemplar): none.
#include "core/floraocc_sim.h"

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

namespace opensc4::floraocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("floraocc: first %s", name);
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

// --- The flora type record's release ----------------------------------------------

// 0x00472C70 (cSC4StaticFloraData, `data` in ECX): a reference dropped.
// Above one the count goes down and is returned. Else the exemplar is
// released (the field cleared first; the game then tests the field again
// and releases what a release might have put there, without clearing),
// the family list's storage freed, the record deleted; zero is returned.
uint32_t __fastcall ReleaseData(char* data, void*)
{
    static bool logged = false;
    FirstCall("ReleaseData", logged);
    const int32_t count = At<int32_t>(data, data::kRefCount);
    if (count > 1) {
        At<int32_t>(data, data::kRefCount) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    if (void* const exemplar = At<void*>(data, data::kExemplar)) {
        At<void*>(data, data::kExemplar) = nullptr;
        Virtual<uint32_t>(exemplar, 0x8);
    }
    if (void* const exemplar = At<void*>(data, data::kExemplar))
        Virtual<uint32_t>(exemplar, 0x8);
    if (void* const families = At<void*>(data, data::kFamilies))
        CallCdecl<void, void*>(addr::SC4_Free, families);
    CallCdecl<void, void*>(addr::SC4_operator_delete, data);
    return 0;
}

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x00472910): the class ID and the flora interface here,
// everything else from the occupant base (which answers 1 with the object
// + 4).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kClassId || iid == kIidFlora) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x00472BC0, 0x00472BD0, 0x00472BE0, 0x00472C00).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 12 (0x00472BA0): the byte, zero extended.
uint32_t __fastcall GetOrientation(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetOrientation", logged);
    return At<uint8_t>(self, field::kOrientation);
}

// Slot 13 (0x00472BB0): the argument's low byte, as it is. The game leaves
// that byte in AL (the rest of EAX is the caller's).
uint32_t __fastcall SetOrientation(char* self, void*, uint32_t orientation)
{
    static bool logged = false;
    FirstCall("SetOrientation", logged);
    At<uint8_t>(self, field::kOrientation) = static_cast<uint8_t>(orientation);
    return orientation & 0xFFu;
}

// Slot 14 (0x00472CD0): the type's record. The same record: nothing (EAX
// is the record). Else the new one is counted first, the old released,
// the field set, and the game jumps to the exemplar part's slot 3
// (SetPropertyExemplar) with the new record's exemplar, or null without a
// record; what that leaves in EAX is the result.
uint32_t __fastcall SetStaticFloraData(char* self, void*, char* record)
{
    static bool logged = false;
    FirstCall("SetStaticFloraData", logged);
    char* const old = At<char*>(self, field::kData);
    if (record == old)
        return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(old));
    if (record)
        ++At<int32_t>(record, data::kRefCount);
    if (char* const current = At<char*>(self, field::kData))
        ReleaseData(current, nullptr);
    At<char*>(self, field::kData) = record;
    void* const exemplar = record ? At<void*>(record, data::kExemplar) : nullptr;
    return Virtual<uint32_t>(self + field::kExemplarPart, 0xC, exemplar);
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 10 (0x00472AB0): a tree without its type's record first asks the
// flora simulator for it by the flora type (slot 7; the service pointer is
// not tested) and sets it through the primary slot 14, whatever the
// answer. The box: the position less and plus the record's radius in x and
// z, from the position's y (copied as it is) up by the record's height;
// radius and height are 1.0 without a record. The sums are made on the FPU
// and stored as floats. EAX is left holding the second pointer.
float* __fastcall GetBoundingBox(char* part, void*, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    if (!At<char*>(self, field::kData)) {
        void* const simulator = Global<void*>(addr::SC4_Service43D80);
        char* const record = Virtual<char*>(simulator, 0x1C, At<uint32_t>(self, field::kFloraType));
        Virtual<uint32_t>(self, slot::kSetStaticFloraData, record);
    }
    double radius = static_cast<double>(Global<float>(addr::SC4_FloatOneA81228));
    double height = radius;
    if (char* const record = At<char*>(self, field::kData)) {
        radius = static_cast<double>(At<float>(record, data::kRadius));
        height = static_cast<double>(At<float>(record, data::kHeight));
    }
    low[0] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition)) - radius);
    std::memcpy(&low[1], self + field::kPosition + 4, 4);
    low[2] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition + 8)) - radius);
    high[0] = static_cast<float>(radius + static_cast<double>(At<float>(self, field::kPosition)));
    high[1] = static_cast<float>(height + static_cast<double>(At<float>(self, field::kPosition + 4)));
    high[2] = static_cast<float>(radius + static_cast<double>(At<float>(self, field::kPosition + 8)));
    return high;
}

// Slot 27 (0x00472940): the lot tag, unless it is 0xFFFFFFFF (then false
// and the argument untouched). The game returns AL only; the rest of EAX
// is the tag's.
uint32_t __fastcall GetLotTag(char* part, void*, uint32_t* tag)
{
    static bool logged = false;
    FirstCall("GetLotTag", logged);
    const uint32_t value = At<uint32_t>(part - field::kOccupant, field::kLotTag);
    if (value == 0xFFFFFFFFu)
        return 0xFFFFFF00u;
    *tag = value;
    return (value & 0xFFFFFF00u) | 1u;
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x00472900).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

// +0x8 slot 3 (0x00472990; `part` = object + 8): version 3, the occupant
// base's record, then the flora type, the position, the birth and last
// seeding dates, the state and the orientation (each byte zero extended
// into the argument), the lot tag. The layout is in
// tools/re/save_schemas/floraocc.py. When the base fails the class's own
// fields are left out, and the result is in both cases whether the stream
// is free of error after (unlike the building's Write, which returns false
// at once).
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kFloraType));
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kPosition);
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kBirthDate));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kLastSeeding));
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kTreeState)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kOrientation)});
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kLotTag));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 4 (0x00472A20; `part` = object + 8): version 3 only. The game
// reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it), so it starts with that value
// here. Another version: AL cleared, the rest of EAX as the stream's read
// left it. As in Write, a failing base skips the fields and the result is
// the stream's state.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, self + field::kFloraType);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kPosition);
        Virtual<uint8_t>(stream, 0x24, self + field::kBirthDate);
        Virtual<uint8_t>(stream, 0x24, self + field::kLastSeeding);
        Virtual<uint8_t>(stream, 0x10, self + field::kTreeState);
        Virtual<uint8_t>(stream, 0x14, self + field::kOrientation);
        Virtual<uint8_t>(stream, 0x24, self + field::kLotTag);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x00472C10, 0x00472C20): this - 4.
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
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable, 0, addr::cSC4FloraOccupant_QueryInterface, QueryInterface),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable, 12, addr::cSC4FloraOccupant_GetOrientation, GetOrientation),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable, 13, addr::cSC4FloraOccupant_SetOrientation, SetOrientation),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable, 14, addr::cSC4FloraOccupant_SetStaticFloraData, SetStaticFloraData),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_4, 0, addr::cSC4FloraOccupant_QueryInterfaceThunk4,
                 QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_4, 10, addr::cSC4FloraOccupant_GetBoundingBox, GetBoundingBox),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_4, 27, addr::cSC4FloraOccupant_GetLotTag, GetLotTag),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_8, 0, addr::cSC4FloraOccupant_QueryInterfaceThunk8,
                 QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_8, 5, addr::cSC4FloraOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_C, 0, addr::cSC4FloraOccupant_QueryInterfaceThunkC,
                 QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_20, 0, addr::cSC4FloraOccupant_QueryInterfaceThunk20,
                 QueryInterfaceAt<0x20>),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_8, 3, addr::cSC4FloraOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_8, 4, addr::cSC4FloraOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_C, 23, addr::cSC4FloraOccupant_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4FloraOccupant_vftable_C, 24, addr::cSC4FloraOccupant_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("floraocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    log::Write("floraocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

using ReleaseDataFn = uint32_t(__fastcall*)(char*, void*);
ReleaseDataFn g_originalReleaseData = nullptr;

// The slots are not called directly from outside the class (the
// QueryInterface thunks jump to 0x00472910 and the save thunks to Write and
// Read inside it; the class registration's call of GetGZCLSID at 0x004607D8
// runs before any replacement is installed), so they are slot swaps. The
// record's release 0x00472C70 is not virtual and the flora simulator calls
// it at 0x006B15B4: a detour. Its three call sites read nothing it could
// change after the call (0x00472CF5 tests EDI and reloads EAX and ECX,
// 0x00472D64 stores through ESI, 0x006B15B9 reloads EAX from the stack; BL,
// ESI and EDI are kept by any C++ function).
bool InstallSlots(registry::Mode)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(kSlots, "slots"))
        return false;
    const addr::Entry* const release = addr_check::Find(addr::cSC4StaticFloraData_Release);
    if (!release || !hook::Install(*release, &ReleaseData, &g_originalReleaseData))
        return false;
    return SwapSlots(kSlots, "slots");
}

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.floraocc.slots", "simulators/floraocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4FloraOccupant_vftable,
                        "Our cSC4FloraOccupant slots: QueryInterface and the parts' thunks, GetOrientation, "
                        "SetOrientation, SetStaticFloraData, GetBoundingBox, GetLotTag, GetGZCLSID, and the type "
                        "record's release by a detour (src/core/floraocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.floraocc.save", "simulators/floraocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4FloraOccupant_vftable_8,
                        "Our cSC4FloraOccupant Write and Read (the save record, version 3; src/core/floraocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 15, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
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

// An item of the flora pool (0x00472960, and inline in the factory): the
// current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4FloraOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x00472D30): the class's vtables, the type's
// record released and the field cleared, the occupant base destroyed, and
// with flag 1 the memory back to the pool (the page header is at the
// address with its low 13 bits clear).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    char* const record = At<char*>(self, field::kData);
    SetVtables(self);
    if (record) {
        ReleaseData(record, nullptr);
        At<char*>(self, field::kData) = nullptr;
    }
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
    if (flags & 1) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFE000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4FloraOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x40000042)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x00472BF0).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4FloraOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4FloraOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4FloraOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4FloraOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4FloraOccupant_vftable_20), sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("floraocc object: vtables copied (slots 12/14 %p %p, +4 slots 10/27 %p %p, +8 slots 3/4 %p %p)",
               g_vtable[12], g_vtable[14], g_vtable4[10], g_vtable4[27], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x00472B30): the occupant base constructed at
// its address on the object + 4, our vtables, the flora type, the position,
// the dates, the record, the state and the orientation zeroed, the base's
// flag 8 added and the occupant type set. The lot tag +0x3C and the bytes
// +0x5A and +0x5B are left as the pool's memory holds them, as in the game
// (the tag is saved: a tree nobody tagged keeps what was there).
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    At<uint32_t>(self, field::kFloraType) = 0;
    SetVtables(self);
    for (std::size_t offset = field::kPosition; offset < field::kTreeState; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kTreeState) = 0;
    At<uint8_t>(self, field::kOrientation) = 0;
    At<uint8_t>(self, field::kFlags) |= 8;
    At<uint32_t>(self, field::kType) = kOccupantType;
    static int created = 0;
    if (created++ < 3)
        log::Write("floraocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x00472960; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x00472C30): an item of the pool, constructed.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return Construct(static_cast<char*>(PoolAllocate()), nullptr);
}

using ConstructFn = void*(__fastcall*)(char*, void*);
using AllocateFn = void*(__cdecl*)(std::size_t);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
AllocateFn g_originalAllocate = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer when a save is read) and the flora simulator's
// 0x006B06EE / 0x006B06FC and 0x006B222E / 0x006B223C, which read only EAX
// after the allocator (add esp, test eax) and after the constructor (mov
// esi, eax; mov edi, eax) and keep nothing in ECX or EDX across them; our
// own flora code calls both at their addresses. The deleting destructor is
// reached through the holder part's slot 22 only, so the vtable copy
// replaces it without a detour; trees made before this group is installed
// (there are none at PostAppInit) would keep the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::cSC4FloraOccupant_cSC4FloraOccupant);
    const addr::Entry* const allocate = addr_check::Find(addr::cSC4FloraOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4FloraOccupant_Create);
    return construct && allocate && create && hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*allocate, &Allocate, &g_originalAllocate) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.floraocc.object",
    "simulators/floraocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FloraOccupant_cSC4FloraOccupant,
    "Our cSC4FloraOccupant object: the constructor, the factory and the pool allocation (detours), the deleting "
    "destructor, identity; methods from copies of the (patched) game vtables taken once (src/core/floraocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::floraocc_sim
