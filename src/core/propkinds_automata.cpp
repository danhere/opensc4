// Our cSC4PropOccupantAutomataController (build 641; the class map, the
// vtables and the fields are in propkinds.h): a prop that has the automata
// controller manager run a controller made from a named template, at the
// prop's place, for as long as the prop is in the city. It keeps the
// template's name (from its exemplar or a caller) and whether the
// controller is to be active; the prop's state picks a variant of the
// template ("name_state"). The prop underneath (cSC4PropOccupant) is
// called at its addresses; the controller and the manager are the
// automata code's (src/core/automata_*.cpp).
//
// Nothing here draws.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the object's primary vtable 0x00AA4D28: 0x04 none (the base's
//     AddRef), 0x14 SetTemplateName one (0x005F06F0), 0x18 SetEnabled one
//     (0x005EDE60);
//   - the occupant part 0x00AA4C30: 0x14 IsInitialized none (0x00458A40),
//     0x18 AsPropertyHolder none (0x006C2040);
//   - the city: 0xBC GetAutomataControllerManager none;
//   - the automata controller manager 0x00A96058: 0x14
//     AddControllerFromTemplateName three (0x00514CA0), 0x1C RemoveController
//     one (0x00517880);
//   - the controller 0x00A95D90: 0x04 none (0x008AE800), 0x08 none
//     (0x00517F10), 0x48 SetActive one (0x00516AA0);
//   - the streams 0x00AA2310 (out) and 0x00AA2280 (in): 0x1C, 0x3C one,
//     0x48 none.
// Direct callees: 0x005E89B0 (ret), 0x005EC960 (ret 8), 0x005ECE90 and
// 0x005EF360 (ret 4), 0x005ED220 (ret 4), 0x005EE050 (ret), 0x005E8C90 and
// 0x005E8980 (ret), 0x005E8C40 (ret), 0x00408480 (ret 4), 0x00408A70 (ret
// 0xC), 0x009128BF (ret), 0x0090CF9A (ret), 0x0090D031 and 0x0090CFF9 (ret
// 4); cdecl: 0x005FD570, 0x005FFCC0 (three), 0x005D9FD0, 0x005D9FA0 (two),
// 0x0090F574 (four here), 0x0090CF54, 0x0090CF63, 0x005E5620 (one).
//
// Callers from outside the class: the class registration (0x004607B4,
// which calls GetGZCLSID and keeps the factory's address, before any
// replacement is installed) and 0x00529628 / 0x00529636 (allocator, then
// constructor: it reads only EAX after either call and keeps nothing in ECX
// or EDX across them). MakeController is called from the class's own
// functions only. Everything else is reached through the vtables and their
// thunks. The detours are plain functions.
#include "core/propkinds.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::propkinds {
namespace {

constexpr std::uintptr_t kPool = addr::cSC4PropOccupantAutomataController_Pool;
constexpr uint32_t kPoolReopenAt = 0x4000005A;
// "%s_%d" (0x00AA4D44): the template of a state other than 0.
constexpr const char* kStateFormat = "%s_%d";

// The game's string object: 0x14 bytes (vtable, begin, end, capacity, a
// zero), as the frames of 0x005EFF60 and 0x005F00B0 leave for it.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14, "game string size");

// An empty game string as the class's frames make it: 8 bytes from the
// game's allocator (not tested for null), a zero in the first.
void MakeEmpty(GameString& text)
{
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The string's end: its vtable back and its buffer freed.
void FreeString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// A vector of floats as 0x005FFCC0 fills it.
struct FloatVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

void* Manager()
{
    return Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC);
}

// The controller kept (+0x6C) is replaced by `made`, with a reference: the
// new one referenced first, the old released after, nothing when they are
// the same.
void Keep(char* self, void* made)
{
    void* const old = At<void*>(self, automata::kController);
    if (made == old)
        return;
    if (made)
        Virtual<uint32_t>(made, 0x4);
    At<void*>(self, automata::kController) = made;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// Not virtual (0x005F00B0): the controller made again. The enabled byte is
// read first. A controller there is made inactive (its slot 18 with 0),
// taken off the manager when the city has one, and released. Then, for a
// name that is not empty: state 0 asks the manager (not tested for null)
// for a controller of that template, any other state for the template
// "name_state"; the occupant is the object + 0xC and the position 0. What
// comes back is kept and the enabled byte read at the start is given to
// SetEnabled (through the vtable). An empty name: nothing more.
void __fastcall MakeController(char* self, void*)
{
    static bool logged = false;
    FirstCall("automata MakeController", logged);
    const uint8_t enabled = At<uint8_t>(self, automata::kEnabled);
    void* const manager = Manager();
    if (void* const controller = At<void*>(self, automata::kController)) {
        Virtual<void>(controller, 0x48, 0u);
        if (manager)
            Virtual<void>(manager, 0x1C, At<void*>(self, automata::kController));
        if (void* const held = At<void*>(self, automata::kController)) {
            At<void*>(self, automata::kController) = nullptr;
            Virtual<uint32_t>(held, 0x8);
        }
    }
    if (At<char*>(self, automata::kNameBegin) == At<char*>(self, automata::kNameEnd))
        return;
    void* made = nullptr;
    if (!At<uint8_t>(self, layout::kState)) {
        made = Virtual<void*>(manager, 0x14, static_cast<void*>(self + layout::kOccupant), static_cast<void*>(self + automata::kName),
                              0u);
        Keep(self, made);
    } else {
        GameString name;
        MakeEmpty(name);
        Cdecl<void, void*, const char*, const char*, uint32_t>(addr::cRZBaseString_Sprintf, &name, kStateFormat,
                                                               At<char*>(self, automata::kNameBegin),
                                                               uint32_t{At<uint8_t>(self, layout::kState)});
        made = Virtual<void*>(manager, 0x14, static_cast<void*>(self + layout::kOccupant), static_cast<void*>(&name), 0u);
        Keep(self, made);
        FreeString(name);
    }
    Virtual<uint8_t>(self, automata::slot::kSetEnabled, uint32_t{enabled});
}

// ====================================================================================
// The class's own interface (`self` is the object)
// ====================================================================================

// Slot 0 (0x005ECC70): the class's interface 0x8A401788 gives the object
// with the base's AddRef on the object + 0xC, the class ID the object with
// a reference through the vtable; everything else is the prop's (object +
// 4). The message target is the prop's answer, not the class's.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("automata QueryInterface", logged);
    if (iid == automata::kIid) {
        *object = self;
        This<uint32_t>(addr::cSC4Occupant_AddRef, self + layout::kOccupant);
        return 1;
    }
    if (iid == automata::kClassId) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return This<uint8_t, uint32_t, void**>(addr::cSC4PropOccupant_QueryInterface, self + layout::kProp, iid, object);
}

// Slot 5 (0x005F06F0): nothing when the name is the same (the length of
// the C string and its bytes). Else the name is assigned (through a
// temporary string; the assignment is handed the address of its own
// argument as the allocator tag, as the game does) and the controller made
// again. No return value.
void __fastcall SetTemplateName(char* self, void*, const char* name)
{
    static bool logged = false;
    FirstCall("automata SetTemplateName", logged);
    const std::size_t length = std::strlen(name);
    const char* const begin = At<char*>(self, automata::kNameBegin);
    if (static_cast<std::size_t>(At<char*>(self, automata::kNameEnd) - begin) == length && std::memcmp(begin, name, length) == 0)
        return;
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, name);
    char* const text = made.begin;
    if (static_cast<void*>(&made.begin) != static_cast<void*>(self + automata::kNameBegin))
        This<void*>(addr::SC4String_AssignRange, self + automata::kNameBegin, text, made.end, static_cast<void*>(&name));
    if (text)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text));
    MakeController(self, nullptr);
}

// Slot 6 (0x005EDE60): a controller there is made active or inactive (its
// slot 18 with the argument), and the byte kept. AL is the argument's low
// byte.
uint32_t __fastcall SetEnabled(char* self, void*, uint32_t enabled)
{
    static bool logged = false;
    FirstCall("automata SetEnabled", logged);
    if (void* const controller = At<void*>(self, automata::kController))
        Virtual<void>(controller, 0x48, enabled);
    At<uint8_t>(self, automata::kEnabled) = static_cast<uint8_t>(enabled);
    return enabled & 0xFF;
}

// ====================================================================================
// The prop part (`prop` is the object + 4)
// ====================================================================================

// Slot 11 (0x005F0530): nothing when the state is the same (the argument
// compared whole with the byte). Else the byte is stored and the
// controller made again. Unlike the prop's SetState: no message, and the
// models are not told. No return value.
void __fastcall SetState(char* prop, void*, uint32_t state)
{
    static bool logged = false;
    FirstCall("automata SetState", logged);
    char* const self = prop - layout::kProp;
    if (state == At<uint8_t>(self, layout::kState))
        return;
    At<uint8_t>(self, layout::kState) = static_cast<uint8_t>(state);
    MakeController(self, nullptr);
}

// ====================================================================================
// The occupant part (`occupant` is the object + 0xC)
// ====================================================================================

// Slot 3 (0x005EFF60): nothing when initialized. The prop's size from the
// exemplar's property 0x27812810 when it is there (its first three floats,
// however many it has), else 16 m a side (the prop's SetSize). Then the
// template's name: the property 0xC98204B8, else the exemplar's name
// (property 0x20); a name that is not empty goes to SetTemplateName
// (through the vtable) and sets the enabled byte. The base's Init. With the
// enabled byte set, a controller and base flag 2 (shown), the controller is
// made active. True.
uint8_t __fastcall Init(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("automata Init", logged);
    char* const self = occupant - layout::kOccupant;
    if (Virtual<uint8_t>(occupant, 0x14))
        return 1;
    void* const holder = Virtual<void*>(occupant, 0x18);
    FloatVector values = {nullptr, nullptr, nullptr};
    uint32_t size[3] = {0x41800000, 0x41800000, 0x41800000};
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, automata::kPropertySize, &values)) {
        size[0] = values.begin[0];
        size[1] = values.begin[1];
        size[2] = values.begin[2];
    }
    This<void>(addr::cSC4PropOccupant_SetSize, self + layout::kProp, static_cast<const uint32_t*>(size));
    GameString name;
    MakeEmpty(name);
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, automata::kPropertyTemplate, static_cast<void*>(&name)))
        Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, automata::kPropertyExemplarName, static_cast<void*>(&name));
    if (name.begin != name.end) {
        Virtual<void>(self, automata::slot::kSetTemplateName, static_cast<const char*>(name.begin));
        At<uint8_t>(self, automata::kEnabled) = 1;
    }
    if (!Virtual<uint8_t>(occupant, 0x14))
        This<uint8_t>(addr::cSC4Occupant_Init, occupant);
    if (At<uint8_t>(self, automata::kEnabled))
        if (void* const controller = At<void*>(self, automata::kController))
            if (At<uint8_t>(self, layout::kFlags) & 2)
                Virtual<void>(controller, 0x48, 1u);
    FreeString(name);
    if (values.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(values.begin));
    return 1;
}

// Slot 4 (0x005EEDD0): nothing unless initialized. The controller taken
// off the manager when the city has one (it is not made inactive first)
// and released, the prop's date range freed, the base's Shutdown (the
// prop's own is not called). The name and the enabled byte stay. True.
uint8_t __fastcall Shutdown(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("automata Shutdown", logged);
    char* const self = occupant - layout::kOccupant;
    if (!Virtual<uint8_t>(occupant, 0x14))
        return 1;
    if (At<void*>(self, automata::kController)) {
        if (void* const manager = Manager())
            Virtual<void>(manager, 0x1C, At<void*>(self, automata::kController));
        if (void* const held = At<void*>(self, automata::kController)) {
            At<void*>(self, automata::kController) = nullptr;
            Virtual<uint32_t>(held, 0x8);
        }
    }
    if (char* const range = At<char*>(self, layout::kDates)) {
        This<void>(addr::cRZDate_Destroy, range + 0x18);
        This<void>(addr::cRZDate_Destroy, range + 0x8);
        Cdecl<void, void*>(addr::SC4_operator_delete, range);
        At<char*>(self, layout::kDates) = nullptr;
    }
    This<uint8_t>(addr::cSC4Occupant_Shutdown, occupant);
    return 1;
}

// ====================================================================================
// Save (`part` is the object + 0x10)
// ====================================================================================

// Slot 3 (0x005EDDB0): version 3, the prop's record (its Write, with the
// same `this`), then the enabled byte and the name. When the prop's Write
// fails nothing more is written; the answer is in both cases whether the
// stream is free of errors.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("automata Write", logged);
    char* const self = part - layout::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (This<uint8_t>(addr::cSC4PropOccupant_Write, part, stream)) {
        Cdecl<void*>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(self, automata::kEnabled)});
        Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(self + automata::kName));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x005EFBF0): false at once for a version other than 3 (read into
// the argument's stack slot, the stream pointer until then). When the
// prop's Read fails nothing more is read. Else the enabled byte and the
// name. The answer is whether the stream is free of errors.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("automata Read", logged);
    char* const self = part - layout::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return 0;
    if (This<uint8_t>(addr::cSC4PropOccupant_Read, part, stream)) {
        Cdecl<void*>(addr::SC4Stream_ReadBool, stream, static_cast<void*>(self + automata::kEnabled));
        Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(self + automata::kName));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 5 (0x005EFE40).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("automata GetGZCLSID", logged);
    return automata::kClassId;
}

// ====================================================================================
// Installation
// ====================================================================================

// GetGZCLSID's code is six bytes (mov eax, the ID; ret): the detour's jump
// covers the first instruction exactly.
const Detour kAutomataDetours[] = {
    {addr::cSC4PropOccupantAutomataController_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
    {addr::cSC4PropOccupantAutomataController_SetTemplateName, reinterpret_cast<void*>(&SetTemplateName)},
    {addr::cSC4PropOccupantAutomataController_SetEnabled, reinterpret_cast<void*>(&SetEnabled)},
    {addr::cSC4PropOccupantAutomataController_MakeController, reinterpret_cast<void*>(&MakeController)},
    {addr::cSC4PropOccupantAutomataController_SetState, reinterpret_cast<void*>(&SetState)},
    {addr::cSC4PropOccupantAutomataController_Init, reinterpret_cast<void*>(&Init)},
    {addr::cSC4PropOccupantAutomataController_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    {addr::cSC4PropOccupantAutomataController_Write, reinterpret_cast<void*>(&Write)},
    {addr::cSC4PropOccupantAutomataController_Read, reinterpret_cast<void*>(&Read)},
    {addr::cSC4PropOccupantAutomataController_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
};

bool InstallAutomata(registry::Mode) { return InstallDetours(kAutomataDetours, "automata"); }

const bool g_registered = registry::Register({
    "simulators.propkinds.automata",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantAutomataController_QueryInterface,
    "Our cSC4PropOccupantAutomataController: QueryInterface, SetTemplateName, SetEnabled, the controller made, "
    "SetState, Init, Shutdown, Write, Read and GetGZCLSID (10 detours; src/core/propkinds.h)",
    false,
    InstallAutomata,
});

// ====================================================================================
// Our object
// ====================================================================================

// Copies of the game's seven vtables, taken at the first construction,
// after every PostAppInit slot swap of the other groups (the prop's
// GetRandomChance in 0x00AA4CC8).
constexpr int kSlots0 = 7, kSlots4 = 24, kSlots8 = 4, kSlotsC = 34, kSlots10 = 6, kSlots14 = 25, kSlots28 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable14[kSlots14];
void* g_vtable28[kSlots28];

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags);

// The holder part's slot 22 (0x005EFEB0).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - layout::kHolder, nullptr, flags);
}

void BuildVtables()
{
    static bool built = false;
    if (built)
        return;
    built = true;
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropOccupantAutomataController_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropOccupant_DerivedVftable_AA4CC8), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropOccupantAutomataController_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PropOccupantAutomataController_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4PropOccupantAutomataController_vftable_10),
                sizeof(g_vtable10));
    std::memcpy(g_vtable14, reinterpret_cast<const void*>(addr::cSC4PropOccupantAutomataController_vftable_14),
                sizeof(g_vtable14));
    std::memcpy(g_vtable28, reinterpret_cast<const void*>(addr::cSC4PropOccupantAutomataController_vftable_28),
                sizeof(g_vtable28));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable10[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable14[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    log::Write("propkinds automata object: vtables copied (+0 slots 5/6 %p %p, +4 slot 11 %p, +C slots 3/4 %p %p)", g_vtable[5],
               g_vtable[6], g_vtable4[11], g_vtableC[3], g_vtableC[4]);
}

void SetVtables(char* self)
{
    BuildVtables();
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, layout::kProp) = g_vtable4;
    At<void**>(self, layout::kMessageTarget) = g_vtable8;
    At<void**>(self, layout::kOccupant) = g_vtableC;
    At<void**>(self, layout::kSerializable) = g_vtable10;
    At<void**>(self, layout::kHolder) = g_vtable14;
    At<void**>(self, layout::kExemplarPart) = g_vtable28;
}

// The constructor (detour of 0x005EFDB0): the prop constructed at its
// address on the object + 4, our vtables, no controller, an empty name (8
// bytes from the game's allocator, not tested for null), not enabled. (The
// game stores an abstract vtable, 0x00AA44F4, at +0 before the prop's
// constructor; nothing reads it.)
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("automata Construct", logged);
    This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, self + layout::kProp);
    SetVtables(self);
    At<void*>(self, automata::kController) = nullptr;
    At<char*>(self, automata::kNameBegin) = nullptr;
    At<char*>(self, automata::kNameEnd) = nullptr;
    At<char*>(self, automata::kNameCapacity) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, automata::kNameBegin) = buffer;
    At<char*>(self, automata::kNameEnd) = buffer;
    At<char*>(self, automata::kNameCapacity) = buffer + 8;
    *buffer = 0;
    At<uint32_t>(self, automata::kNameZero) = 0;
    At<std::uintptr_t>(self, automata::kName) = addr::SC4String_Vtable;
    At<uint8_t>(self, automata::kEnabled) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("propkinds automata object: constructed %p", static_cast<void*>(self));
    return self;
}

// The destructor's body (detour of 0x005EFEF0; the deleting destructor is
// its one caller): the class's vtables, the name's buffer freed, the
// controller released (it is not taken off the manager here: Shutdown does
// that), then the occupant base destroyed. The game passes the base null
// for a null object + 4, which no object has.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("automata Destroy", logged);
    SetVtables(self);
    At<std::uintptr_t>(self, automata::kName) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, automata::kNameBegin))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text));
    if (void* const controller = At<void*>(self, automata::kController))
        Virtual<uint32_t>(controller, 0x8);
    This<void>(addr::cSC4Occupant_Destroy, self + layout::kOccupant);
}

// The deleting destructor (detour of 0x005F04D0): the body, and with flag 1
// the memory back to the pool. A null object is still destroyed first, as
// in the game.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("automata DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self)
        PoolFree(kPool, self, kPoolReopenAt);
    return self;
}

// The pool allocator (detour of 0x005EDD80; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("automata Allocate", logged);
    return PoolAllocate(kPool);
}

// The factory (detour of 0x005F0490): an item of the pool, constructed; the
// prop interface (object + 4) is what it hands out.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("automata Create", logged);
    char* const self = static_cast<char*>(Construct(static_cast<char*>(PoolAllocate(kPool)), nullptr));
    return self ? self + layout::kProp : nullptr;
}

const Detour kAutomataObjectDetours[] = {
    {addr::cSC4PropOccupantAutomataController_Construct, reinterpret_cast<void*>(&Construct)},
    {addr::cSC4PropOccupantAutomataController_Allocate, reinterpret_cast<void*>(&Allocate)},
    {addr::cSC4PropOccupantAutomataController_Create, reinterpret_cast<void*>(&Create)},
    {addr::cSC4PropOccupantAutomataController_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    {addr::cSC4PropOccupantAutomataController_Destroy, reinterpret_cast<void*>(&Destroy)},
};

// Every construction path: the factory (through the class registration's
// pointer when a save is read and for the prop manager's types) and the
// allocator and constructor pair at 0x00529628. The deleting destructor is
// reached through the holder part's slot 22: ours in the copies, and by the
// detour for the game's thunk.
bool InstallAutomataObject(registry::Mode) { return InstallDetours(kAutomataObjectDetours, "automata object"); }

const bool g_registeredObject = registry::Register({
    "simulators.propkinds.automata_object",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantAutomataController_Construct,
    "Our cSC4PropOccupantAutomataController object: the constructor, the pool allocation, the factory, the destructor "
    "and the deleting destructor (5 detours), identity; methods from copies of the game vtables taken once "
    "(src/core/propkinds.h)",
    false,
    InstallAutomataObject,
});

} // namespace
} // namespace opensc4::propkinds
