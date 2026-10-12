// Our cSC4PowerLineOccupant (build 641; the class map, the vtables slot by
// slot and the fields are in powerlineocc_sim.h): the class's own slots in
// its five vtables, the save record's Write and Read, and our own object
// (the constructor, the factory and the pool allocation by detour, the
// deleting destructor and identity from copies of the vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Init, Shutdown, Write, Read) is every
// occupant class's (src/core/occbase_sim.cpp): it is called at its address.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out; 0x005D1E30 slot 7, 0x005D1E70
//     slot 8, 0x005D1F70 slot 12) and 0x00AA2280 (in; 0x005D19C0, 0x005D19F0,
//     0x005D1AB0): slots 0x1C, 0x20 and 0x30 one argument, 0x48 (0x005D16B0)
//     none;
//   - AddRef and Release (slots 0x4 and 0x8 of the object, 0x005E28F0 and
//     0x005E2900, and of the poles): none.
// and every call of the game's functions its count from theirs: the
// occupant base's Write 0x005E8CD0 and Read 0x005E8D60 and the base's
// QueryInterface 0x005E89C0 take the stream or the two arguments (ret 4,
// ret 8), the base's Shutdown 0x005E8980 none, WriteBool 0x005D9FD0 and
// WriteReference 0x005D99A0 and ReadBool 0x005D9FA0 two (cdecl: the
// caller's `add esp, 0x18` in Write and the `add esp, 0x28` in Read count
// 3 x 2 and 2 + 2 x 4 arguments), ReadReference 0x005D9730 four.
#include "core/powerlineocc_sim.h"

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

namespace opensc4::powerlineocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("powerlineocc: first %s", name);
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

// __ftol (0x009EEF04): truncation toward zero; the low dword of its int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x00649350): the three IDs give the object (counted through its
// slot 1), everything else the occupant base's (a jump, with the object +
// 4). The object is stored before the count is taken.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidPowerLine || iid == kClassId || iid == kIidShared) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x006498C0, 0x006498D0, 0x006498E0, 0x00649900).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 3 (0x00649290): each pole is counted before the pointer is given out
// (the pointer is read again after the call), the flag is the byte. The game
// has no result: EAX holds the count the second pole's AddRef returned (zero
// without a second pole) with its low byte replaced by the flag.
uint32_t __fastcall GetConnectedPoles(char* self, void*, void** first, void** second, uint8_t* flag)
{
    static bool logged = false;
    FirstCall("GetConnectedPoles", logged);
    if (void* const pole = At<void*>(self, field::kPole1))
        Virtual<uint32_t>(pole, 0x4);
    *first = At<void*>(self, field::kPole1);
    uint32_t eax = 0;
    if (void* const pole = At<void*>(self, field::kPole2))
        eax = Virtual<uint32_t>(pole, 0x4);
    *second = At<void*>(self, field::kPole2);
    const uint8_t byte = At<uint8_t>(self, field::kFlag);
    *flag = byte;
    return (eax & 0xFFFFFF00u) | byte;
}

// Slot 4 (0x006492D0): the new first pole is counted, the old one released,
// the pointer stored; the same for the second; then the flag (the third
// argument's low byte, read from the caller's stack after the releases).
// The game has no result: EAX is the result of the last count taken or
// released (the entry value of EAX when there was none is not known, taken
// as zero here), with the low byte replaced by the flag when the old second
// pole was released.
uint32_t __fastcall SetConnectedPoles(char* self, void*, void* first, void* second, uint32_t flag)
{
    static bool logged = false;
    FirstCall("SetConnectedPoles", logged);
    uint32_t eax = 0;
    if (first)
        eax = Virtual<uint32_t>(first, 0x4);
    if (void* const old = At<void*>(self, field::kPole1))
        eax = Virtual<uint32_t>(old, 0x8);
    At<void*>(self, field::kPole1) = first;
    if (second)
        eax = Virtual<uint32_t>(second, 0x4);
    if (void* const old = At<void*>(self, field::kPole2)) {
        eax = Virtual<uint32_t>(old, 0x8);
        At<void*>(self, field::kPole2) = second;
        At<uint8_t>(self, field::kFlag) = static_cast<uint8_t>(flag);
        return (eax & 0xFFFFFF00u) | static_cast<uint8_t>(flag);
    }
    At<void*>(self, field::kPole2) = second;
    At<uint8_t>(self, field::kFlag) = static_cast<uint8_t>(flag);
    return eax;
}

// Slot 5 (0x00649330): the two cells. EAX is the second one.
uint32_t __fastcall GetCell(char* self, void*, int32_t* x, int32_t* z)
{
    static bool logged = false;
    FirstCall("GetCell", logged);
    *x = At<int32_t>(self, field::kCellX);
    const int32_t second = At<int32_t>(self, field::kCellZ);
    *z = second;
    return static_cast<uint32_t>(second);
}

// Slot 6 (0x00649390): the byte in AL (the rest of EAX is not set).
uint8_t __fastcall IsPreview(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsPreview", logged);
    return At<uint8_t>(self, field::kPreview);
}

// Slot 7 (0x006493A0): the byte stored; AL holds it (the rest of EAX is not
// set).
uint8_t __fastcall SetPreview(char* self, void*, uint32_t preview)
{
    static bool logged = false;
    FirstCall("SetPreview", logged);
    At<uint8_t>(self, field::kPreview) = static_cast<uint8_t>(preview);
    return static_cast<uint8_t>(preview);
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 4 (0x00649270): the poles let go (SetConnectedPoles through the
// object's slot 4 with three zeros), then the base's Shutdown (a jump: its
// result is the result).
uint32_t __fastcall Shutdown(char* part, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    Virtual<uint32_t>(part - field::kOccupant, slot::kSetConnectedPoles, 0u, 0u, 0u);
    return CallThis<uint32_t>(addr::cSC4Occupant_Shutdown, part);
}

// Slot 9 (0x0064A460): the three floats copied as they are, then the cell
// of the x and of the z, each the float times 0.0625 (0x00A8E174; the
// product is exact) truncated by __ftol; EAX is the z cell.
uint32_t __fastcall SetPosition(char* part, void*, const float* position)
{
    static bool logged = false;
    FirstCall("SetPosition", logged);
    char* const self = part - field::kOccupant;
    const auto* const bits = reinterpret_cast<const uint32_t*>(position);
    At<uint32_t>(self, field::kPosition) = bits[0];
    At<uint32_t>(self, field::kPosition + 4) = bits[1];
    At<uint32_t>(self, field::kPosition + 8) = bits[2];
    const double sixteenth = static_cast<double>(Global<float>(addr::SC4_FloatSixteenth));
    At<int32_t>(self, field::kCellX) = Ftol(static_cast<double>(position[0]) * sixteenth);
    const int32_t z = Ftol(static_cast<double>(position[2]) * sixteenth);
    At<int32_t>(self, field::kCellZ) = z;
    return static_cast<uint32_t>(z);
}

// Slot 10 (0x0064A4A0): the position as both corners of the box. The game
// loads each float on the FPU and stores it twice, the second argument's
// first; EAX is the second argument.
float* __fastcall GetBoundingBox(char* part, void*, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    for (std::size_t i = 0; i < 3; ++i) {
        const uint32_t value = At<uint32_t>(self, field::kPosition + 4 * i);
        std::memcpy(&high[i], &value, 4);
        std::memcpy(&low[i], &value, 4);
    }
    return high;
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x00649260).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

// +0x8 slot 3 (0x0064A310; `part` = object + 8): version 2, the occupant
// base's record, then the position's three floats (each float's bits pushed
// as they are, slot 0x30), the two cells (slot 0x20), the flag as a bool
// (0x005D9FD0: one byte through the stream's slot 0x14), and the two poles
// as references (0x005D99A0). The layout is in
// tools/re/save_schemas/powerlineocc.py. When the base fails the class's own
// fields are left out, and the result is in both cases whether the stream
// is free of error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 2u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kPosition));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kPosition + 4));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kPosition + 8));
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellX));
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellZ));
        CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(self, field::kFlag)});
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kPole1));
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kPole2));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 4 (0x0064A3A0; `part` = object + 8): version 2 only. The game
// reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it), so it starts with that value
// here. Another version: AL cleared, the rest of EAX as the stream's read
// left it. As in Write, a failing base skips the fields and the result is
// the stream's state. The two cells are read into one local of the game's
// frame (the slot the entry ECX was pushed to, so the object + 8 until the
// first read) and stored after each read. The flag is read as a bool (stored
// as 0 or 1) and each pole as a reference to the pole interface (0xA8C86142;
// the game does not release what the field held).
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 2)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x30, self + field::kPosition);
        Virtual<uint8_t>(stream, 0x30, self + field::kPosition + 4);
        Virtual<uint8_t>(stream, 0x30, self + field::kPosition + 8);
        uint32_t cell = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
        Virtual<uint8_t>(stream, 0x20, &cell);
        At<uint32_t>(self, field::kCellX) = cell;
        Virtual<uint8_t>(stream, 0x20, &cell);
        At<uint32_t>(self, field::kCellZ) = cell;
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, self + field::kFlag);
        CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidPole,
                                                             self + field::kPole1, 0u);
        CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidPole,
                                                             self + field::kPole2, 0u);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x0064A540, 0x0064A550): this - 4.
uint32_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }
uint32_t __fastcall ReadThunk(char* holder, void*, void* stream) { return Read(holder - 0x4, nullptr, stream); }

// --- Installation -------------------------------------------------------------------

// Hooks to leave out, for bisecting: OPENSC4_POWERLINEOCC_SKIP lists
// function addresses (hex, eight digits), as in src/core/netocc_life.cpp.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_POWERLINEOCC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable, 0, addr::cSC4PowerLineOccupant_QueryInterface, QueryInterface),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable, 3, addr::cSC4PowerLineOccupant_GetConnectedPoles,
                 GetConnectedPoles),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable, 4, addr::cSC4PowerLineOccupant_SetConnectedPoles,
                 SetConnectedPoles),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable, 5, addr::cSC4PowerLineOccupant_GetCell, GetCell),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable, 6, addr::cSC4PowerLineOccupant_IsPreview, IsPreview),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable, 7, addr::cSC4PowerLineOccupant_SetPreview, SetPreview),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_4, 0, addr::cSC4PowerLineOccupant_QueryInterfaceThunk4,
                 QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_8, 0, addr::cSC4PowerLineOccupant_QueryInterfaceThunk8,
                 QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_8, 5, addr::cSC4PowerLineOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_C, 0, addr::cSC4PowerLineOccupant_QueryInterfaceThunkC,
                 QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_20, 0, addr::cSC4PowerLineOccupant_QueryInterfaceThunk20,
                 QueryInterfaceAt<0x20>),
};
const Slot kOccupantSlots[] = {
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_4, 4, addr::cSC4PowerLineOccupant_Shutdown, Shutdown),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_4, 9, addr::cSC4PowerLineOccupant_SetPosition, SetPosition),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_4, 10, addr::cSC4PowerLineOccupant_GetBoundingBox,
                 GetBoundingBox),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_8, 3, addr::cSC4PowerLineOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_8, 4, addr::cSC4PowerLineOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_C, 23, addr::cSC4PowerLineOccupant_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4PowerLineOccupant_vftable_C, 24, addr::cSC4PowerLineOccupant_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("powerlineocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    unsigned swapped = 0;
    for (const Slot& slot : slots) {
        if (Skipped(slot.original))
            continue;
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
        ++swapped;
    }
    log::Write("powerlineocc %s: %u slots are ours", what, swapped);
    return true;
}

// None of the slots is called directly from outside the class (the
// QueryInterface thunks jump to 0x00649350 and the save thunks to Write and
// Read; the class registration's call of GetGZCLSID at 0x00460700 runs
// before any replacement is installed) and no other class's vtable holds
// one, so they are all slot swaps: no detour, no thunk.
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }

bool InstallOccupant(registry::Mode) { return SwapSlots(kOccupantSlots, "occupant"); }

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.powerlineocc.slots", "simulators/powerlineocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PowerLineOccupant_vftable,
                        "Our cSC4PowerLineOccupant interface slots: QueryInterface and the parts' thunks, "
                        "GetConnectedPoles, SetConnectedPoles, GetCell, IsPreview, SetPreview, GetGZCLSID "
                        "(src/core/powerlineocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.powerlineocc.occupant", "simulators/powerlineocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PowerLineOccupant_vftable_4,
                        "Our cSC4PowerLineOccupant occupant slots: Shutdown, SetPosition, GetBoundingBox "
                        "(src/core/powerlineocc_sim.h)",
                        false, InstallOccupant}),
    registry::Register({"simulators.powerlineocc.save", "simulators/powerlineocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PowerLineOccupant_vftable_8,
                        "Our cSC4PowerLineOccupant Write and Read (the save record, version 2; "
                        "src/core/powerlineocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 8, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
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

// An item of the power line pool (0x0064A2E0, and inline in the factory):
// the current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4PowerLineOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x0064ABF0): the class's vtables, the occupant
// base destroyed, and with flag 1 the memory back to the pool (the page
// header is at the address with its low 13 bits clear; the page is
// released at count 0 and opened again at count 0x40000045).
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
        void* const pool = reinterpret_cast<void*>(addr::cSC4PowerLineOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x40000045)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x006498F0).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PowerLineOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PowerLineOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PowerLineOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PowerLineOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4PowerLineOccupant_vftable_20),
                sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("powerlineocc object: vtables copied (slots 3/4/5 %p %p %p, +4 slots 4/9/10 %p %p %p, +8 slots "
               "3/4 %p %p)",
               g_vtable[3], g_vtable[4], g_vtable[5], g_vtable4[4], g_vtable4[9], g_vtable4[10], g_vtable8[3],
               g_vtable8[4]);
}

// The constructor (detour of 0x0064A270): the abstract interface's vtable
// stored first (overwritten at once), the occupant base constructed at its
// address on the object + 4, our vtables, the position's three floats each a
// copy of the float at 0x00B46778 (read as the game does, three times, not
// assumed zero), the cells, the two poles, the flag and the preview byte
// zeroed, and last the occupant type. The bytes +0x56 and +0x57 are left as
// the pool's memory holds them, as in the game (nothing reads or saves
// them).
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<std::uintptr_t>(self, 0x0) = addr::cSC4PowerLineOccupant_AbstractVtable;
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    SetVtables(self);
    for (std::size_t i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kPosition + 4 * i) = Global<uint32_t>(addr::SC4_ZeroVector3);
    At<uint32_t>(self, field::kCellX) = 0;
    At<uint32_t>(self, field::kCellZ) = 0;
    At<uint32_t>(self, field::kPole1) = 0;
    At<uint32_t>(self, field::kPole2) = 0;
    At<uint8_t>(self, field::kFlag) = 0;
    At<uint8_t>(self, field::kPreview) = 0;
    At<uint32_t>(self, field::kType) = kOccupantType;
    static int created = 0;
    if (created++ < 3)
        log::Write("powerlineocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x0064A2E0; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x0064AB70): an item of the pool, constructed;
// the result is the constructor's, the object (not its occupant part).
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
// registration's pointer when a save is read; it is a tail jump to the
// constructor) and the power network's builder, 0x00651CD6 (the allocator;
// add esp, test eax after) and 0x00651CE4 (the constructor; mov esi, eax
// after), which reloads ECX and EDX from its frame after both calls. The
// deleting destructor is reached through the holder part's slot 22 only
// (0x006498F0 jumps to it), so the vtable copy replaces it without a
// detour; wires made before this group is installed (there are none at
// PostAppInit) would keep the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::cSC4PowerLineOccupant_cSC4PowerLineOccupant);
    const addr::Entry* const allocate = addr_check::Find(addr::cSC4PowerLineOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4PowerLineOccupant_Create);
    if (!construct || !allocate || !create)
        return false;
    unsigned installed = 0;
    if (!Skipped(addr::cSC4PowerLineOccupant_cSC4PowerLineOccupant)) {
        if (!hook::Install(*construct, &Construct, &g_originalConstruct))
            return false;
        ++installed;
    }
    if (!Skipped(addr::cSC4PowerLineOccupant_Allocate)) {
        if (!hook::Install(*allocate, &Allocate, &g_originalAllocate))
            return false;
        ++installed;
    }
    if (!Skipped(addr::cSC4PowerLineOccupant_Create)) {
        if (!hook::Install(*create, &Create, &g_originalCreate))
            return false;
        ++installed;
    }
    log::Write("powerlineocc object: %u detours", installed);
    return true;
}

const bool g_registeredObject = registry::Register({
    "simulators.powerlineocc.object",
    "simulators/powerlineocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerLineOccupant_cSC4PowerLineOccupant,
    "Our cSC4PowerLineOccupant object: the constructor, the factory and the pool allocation (detours), the "
    "deleting destructor, identity; methods from copies of the (patched) game vtables taken once "
    "(src/core/powerlineocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::powerlineocc_sim
