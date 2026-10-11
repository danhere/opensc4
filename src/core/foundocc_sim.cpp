// Our cSC4FoundationOccupant (build 641; the class map, the vtables slot by
// slot and the fields are in foundocc_sim.h): the class's own slots in its
// five vtables, the save record's Write and Read, and our own object (the
// constructor, the factory and the pool allocation by detour, the deleting
// destructor and identity from copies of the vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Init, Shutdown, Write, Read) is every
// occupant class's (src/core/occbase_sim.cpp): it is called at its address.
// The class's getters and setters of one instruction (the foundation type,
// the height, the usage, the position) are code the linker folded with
// other classes' vtables; they stay the game's.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out; 0x005D1D80, 0x005D1E30,
//     0x005D1EB0, 0x005D1F70) and 0x00AA2280 (in; 0x005D1930, 0x005D19C0,
//     0x005D1A20, 0x005D1AB0): slots 0x10, 0x1C, 0x24, 0x30 one argument,
//     0x48 (0x005D16B0) none;
//   - AddRef (slot 0x4 of the object, 0x005E28F0): none.
#include "core/foundocc_sim.h"

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

namespace opensc4::foundocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("foundocc: first %s", name);
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

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x005D6160): the foundation interface here (the object, counted
// through its slot 1), everything else from the occupant base (a jump, with
// the object + 4). The class ID is not answered here.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidFoundation) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x005D61D0, 0x005D61E0, 0x005D61F0, 0x005D6210).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 8 (0x005D6400): the three floats copied as they are. The game
// returns AL = 1; the rest of EAX is the third float's.
uint32_t __fastcall SetSize(char* self, void*, const uint32_t* size)
{
    static bool logged = false;
    FirstCall("SetSize", logged);
    At<uint32_t>(self, field::kSize) = size[0];
    At<uint32_t>(self, field::kSize + 4) = size[1];
    const uint32_t last = size[2];
    At<uint32_t>(self, field::kSize + 8) = last;
    return (last & 0xFFFFFF00u) | 1u;
}

// Slot 11 (0x005D6190): the byte, sign extended.
int32_t __fastcall GetOrientation(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetOrientation", logged);
    return At<int8_t>(self, field::kOrientation);
}

// Slot 12 (0x005D61A0): the argument held to 0 ... 3 (signed compares),
// its low byte stored. EAX is 1 on each of the three paths (the stored
// value's higher bytes are zero).
uint32_t __fastcall SetOrientation(char* self, void*, int32_t orientation)
{
    static bool logged = false;
    FirstCall("SetOrientation", logged);
    if (orientation < 0)
        orientation = 0;
    else if (orientation > 3)
        orientation = 3;
    At<uint8_t>(self, field::kOrientation) = static_cast<uint8_t>(orientation);
    return 1;
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 10 (0x005D63A0): the box of the slab under the position. In x and
// z the position less and plus half the size; in y from the position less
// the whole height up to the position (copied as it is). The products and
// sums are made on the FPU and stored as floats, in the game's order: low
// x, high x, low y, high y, low z, high z. EAX is left holding the first
// pointer.
float* __fastcall GetBoundingBox(char* part, void*, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    low[0] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition)) -
                                static_cast<double>(At<float>(self, field::kSize)) * half);
    high[0] = static_cast<float>(static_cast<double>(At<float>(self, field::kSize)) * half +
                                 static_cast<double>(At<float>(self, field::kPosition)));
    low[1] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition + 4)) -
                                static_cast<double>(At<float>(self, field::kSize + 4)));
    std::memcpy(&high[1], self + field::kPosition + 4, 4);
    low[2] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition + 8)) -
                                static_cast<double>(At<float>(self, field::kSize + 8)) * half);
    high[2] = static_cast<float>(static_cast<double>(At<float>(self, field::kSize + 8)) * half +
                                 static_cast<double>(At<float>(self, field::kPosition + 8)));
    return low;
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x005D6130).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

// +0x8 slot 3 (0x005D6250; `part` = object + 8): version 2, the occupant
// base's record, then the foundation type, the height (the float's bits
// pushed as they are), the position, the size, the usage and the
// orientation (each byte zero extended into the argument, both through
// slot 0x10). The layout is in tools/re/save_schemas/foundocc.py. When the
// base fails the class's own fields are left out, and the result is in
// both cases whether the stream is free of error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 2u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kFoundationType));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kMaxHeight));
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kPosition);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kSize);
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kUsage)});
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kOrientation)});
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 4 (0x005D62D0; `part` = object + 8): version 2 only. The game
// reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it), so it starts with that value
// here. Another version: AL cleared, the rest of EAX as the stream's read
// left it. As in Write, a failing base skips the fields and the result is
// the stream's state. The orientation is stored as read (no clamp).
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
        Virtual<uint8_t>(stream, 0x24, self + field::kFoundationType);
        Virtual<uint8_t>(stream, 0x30, self + field::kMaxHeight);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kPosition);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kSize);
        Virtual<uint8_t>(stream, 0x10, self + field::kUsage);
        Virtual<uint8_t>(stream, 0x10, self + field::kOrientation);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x005D6420, 0x005D6430): this - 4.
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
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable, 0, addr::cSC4FoundationOccupant_QueryInterface, QueryInterface),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable, 8, addr::cSC4FoundationOccupant_SetSize, SetSize),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable, 11, addr::cSC4FoundationOccupant_GetOrientation,
                 GetOrientation),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable, 12, addr::cSC4FoundationOccupant_SetOrientation,
                 SetOrientation),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_4, 0, addr::cSC4FoundationOccupant_QueryInterfaceThunk4,
                 QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_4, 10, addr::cSC4FoundationOccupant_GetBoundingBox,
                 GetBoundingBox),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_8, 0, addr::cSC4FoundationOccupant_QueryInterfaceThunk8,
                 QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_8, 5, addr::cSC4FoundationOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_C, 0, addr::cSC4FoundationOccupant_QueryInterfaceThunkC,
                 QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_20, 0, addr::cSC4FoundationOccupant_QueryInterfaceThunk20,
                 QueryInterfaceAt<0x20>),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_8, 3, addr::cSC4FoundationOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_8, 4, addr::cSC4FoundationOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_C, 23, addr::cSC4FoundationOccupant_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4FoundationOccupant_vftable_C, 24, addr::cSC4FoundationOccupant_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("foundocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    log::Write("foundocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

// None of the slots is called directly from outside the class (the
// QueryInterface thunks jump to 0x005D6160 and the save thunks to Write and
// Read inside it; the class registration's call of GetGZCLSID at 0x004607FC
// runs before any replacement is installed) and no other class's vtable
// holds one, so they are all slot swaps: no detour, no thunk.
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.foundocc.slots", "simulators/foundocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4FoundationOccupant_vftable,
                        "Our cSC4FoundationOccupant slots: QueryInterface and the parts' thunks, SetSize, "
                        "GetOrientation, SetOrientation, GetBoundingBox, GetGZCLSID (src/core/foundocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.foundocc.save", "simulators/foundocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4FoundationOccupant_vftable_8,
                        "Our cSC4FoundationOccupant Write and Read (the save record, version 2; "
                        "src/core/foundocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 13, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
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

// An item of the foundation pool (0x005D6220, and inline in the factory):
// the current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4FoundationOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x005D6500): the class's vtables, the occupant
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
        void* const pool = reinterpret_cast<void*>(addr::cSC4FoundationOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x40000042)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x005D6200).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4FoundationOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4FoundationOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4FoundationOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4FoundationOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4FoundationOccupant_vftable_20),
                sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("foundocc object: vtables copied (slots 8/11/12 %p %p %p, +4 slot 10 %p, +8 slots 3/4 %p %p)",
               g_vtable[8], g_vtable[11], g_vtable[12], g_vtable4[10], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x005D6440): the occupant base constructed at
// its address on the object + 4, our vtables, the foundation type, the
// height and the position zeroed, the size's three floats each a copy of
// the float at 0x00B2303C (read as the game does, not assumed zero), the
// usage and the orientation zeroed, and last the occupant type. The bytes
// +0x5A and +0x5B are left as the pool's memory holds them, as in the game
// (nothing reads or saves them).
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
    SetVtables(self);
    for (std::size_t offset = field::kFoundationType; offset < field::kSize; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = field::kSize; offset < field::kUsage; offset += 4)
        At<uint32_t>(self, offset) = Global<uint32_t>(addr::cSC4FoundationOccupant_DefaultSize);
    At<uint8_t>(self, field::kUsage) = 0;
    At<uint8_t>(self, field::kOrientation) = 0;
    At<uint32_t>(self, field::kType) = kOccupantType;
    static int created = 0;
    if (created++ < 3)
        log::Write("foundocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x005D6220; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x005D64C0): an item of the pool, constructed;
// the result is the occupant part (the object + 4), or null if the
// constructor gave none (it never does).
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const object = static_cast<char*>(Construct(static_cast<char*>(PoolAllocate()), nullptr));
    return object ? object + field::kOccupant : nullptr;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
using AllocateFn = void*(__cdecl*)(std::size_t);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
AllocateFn g_originalAllocate = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer when a save is read; its own call of the
// constructor at 0x005D64EC reads only EAX after) and the building
// development simulator's AddFoundationOccupant, 0x00695AFD (the allocator;
// add esp, test eax after) and 0x00695B0B (the constructor; mov esi, eax
// after), which keeps its values in EBP and BL across both; our own
// AddFoundationOccupant (src/core/development_occupants.cpp) calls both at
// their addresses. None keeps anything in ECX or EDX across the calls. The
// deleting destructor is reached through the holder part's slot 22 only
// (0x005D6200 jumps to it), so the vtable copy replaces it without a
// detour; foundations made before this group is installed (there are none
// at PostAppInit) would keep the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::cSC4FoundationOccupant_cSC4FoundationOccupant);
    const addr::Entry* const allocate = addr_check::Find(addr::cSC4FoundationOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4FoundationOccupant_Create);
    return construct && allocate && create && hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*allocate, &Allocate, &g_originalAllocate) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.foundocc.object",
    "simulators/foundocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FoundationOccupant_cSC4FoundationOccupant,
    "Our cSC4FoundationOccupant object: the constructor, the factory and the pool allocation (detours), the "
    "deleting destructor, identity; methods from copies of the (patched) game vtables taken once "
    "(src/core/foundocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::foundocc_sim
