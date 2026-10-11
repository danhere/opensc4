// Our cSC4PropOccupant (build 641; the class map, the vtables slot by slot
// and the fields are in propocc_sim.h): the queries and setters, the timers
// (hours, dates, the timer bits, the state, the frame message), the save
// record's Write and Read, and our own object (the constructor, the
// factory, the pool allocation and the deleting destructor by detour,
// identity from copies of the vtables).
//
// Six classes derive from the prop, keep its functions in their vtables and
// call some directly, so the functions are replaced by detours at their
// addresses (as the occupant base's are, src/core/occbase_sim.cpp); slots
// are swapped only for GetGZCLSID (the prop's own vtable alone) and
// GetRandomChance (four bytes of code, too short for a detour: slot 18 of
// the prop's and the six derived vtables). The `this`-adjusting thunks
// (0x005EE0F0 to 0x005EE150, 0x005EFC70) stay the game's: they jump into
// the detours. The stubs the linker folded with other classes' vtables stay
// the game's as well (propocc_sim.h lists them).
//
// The occupant base (cSC4Occupant: constructor, destructor, QueryInterface,
// Shutdown, Write, Read) is called at its addresses.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the prop's primary slots, all seven vtables (0x00AA4900, 0x00AA4730,
//     0x00AA4B30, 0x00AA4CC8, 0x00AA4E78, 0x00AA5418, 0x00AA5620): 0x04
//     none, 0x2C SetState one (0x005ED290, 0x005F0530, 0x005EF2C0), 0x50
//     one, 0x54 two;
//   - the occupant part's, all seven (0x00AA4868, 0x00AA4698, 0x00AA4A98,
//     0x00AA4C30, 0x00AA4DE0, 0x00AA5380, 0x00AA5588): 0x14 and 0x4C none,
//     0x20 GetPosition one, 0x24 SetPosition one (0x005EDE00, 0x005EE6D0,
//     0x005EF220, 0x005F4E90, 0x005F7AA0);
//   - the holder part 0x00AA47E8: 0x0C one (0x005FEC20);
//   - the placeable object 0x00AA4468: 0x04, 0x08, 0x0C none, 0x10 two
//     (0x005EBC00), 0x20 one (0x005EC0C0);
//   - a model: QueryInterface of its unknown part 0x00ABD064 two
//     (0x007FAF60, which answers 0xA818EDD8 with the part - 8), then of
//     that interface 0x00ABD088: 0x28 one (0x007FDAF0), 0x3C one
//     (0x007FC1C0); its Release through the virtual base none;
//   - cGZMessage 0x00ACEC00: 0x14, 0x30, 0x40 one, 0x84 one (0x0091346B
//     reads one argument and jumps to 0x005447B0, ret 4), 0x04, 0x08, 0x10
//     and 0x4C none; the message server (0x00A8CAFC, 0x00AD1BF4): 0x10 two;
//   - the simulator 0x00AB2FB8: 0x1C and 0x80 none; the clock 0x00A86588:
//     0x28 and 0x34 none;
//   - a date's interface 0x00ACEA38: 0x18 three (0x00913113), 0x34, 0x3C,
//     0x5C none, 0x6C, 0x70, 0x78, 0x8C, 0x9C one;
//   - the occupant manager 0x00A8F7F0: 0x50 two, 0x54 three;
//   - the streams 0x00AA2310 (out) and 0x00AA2280 (in): 0x10, 0x14, 0x1C,
//     0x24 one, 0x48 none.
// Direct callees: 0x005E55E0, 0x005E5620, 0x009133DA (cdecl, one), the
// stream helpers 0x005DA580, 0x005DA550, 0x005D9FD0, 0x005D9FA0,
// 0x005DA1A0, 0x005DA170 (cdecl, two), 0x00913177 and 0x009128BF (ret),
// 0x00913A06 (ret 4), 0x0051CA60 (ret 0x14), 0x0090CF9A (ret), 0x0090D031
// and 0x0090CFF9 (ret 4).
//
// Register use of the callers: every direct call and jump to the detoured
// functions was read (0x004A2550, 0x004A255E, 0x004A309D, 0x004A30AB, the
// six derived constructors, the six derived Writes and Reads, the eight
// DoMessage sites, the six QueryInterface jumps, 0x005F500E, 0x005F7ABC,
// 0x005F5EDA, 0x005F77AD, 0x005EE64A, 0x0075EED9, 0x005EF65A, 0x005EFFDB):
// none keeps a value in EAX, ECX or EDX across the call or reads more than
// the documented result (AL after Write, Read and DoMessage; EAX after the
// allocator and the constructor), and none has a value on the FPU stack.
// The detours are plain functions.
#include "core/propocc_sim.h"

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

namespace opensc4::propocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("propocc: first %s", name);
}

namespace {

constexpr uint32_t kIidModel = 0xA818EDD8;        // the interface of a placeable object's model
constexpr uint32_t kMessageFrame = 0x86AD10EE;    // a frame: data 3 is the time as a float
constexpr uint32_t kMessageState = 0xE9C86DDB;    // posted by SetState
constexpr uint32_t kPropertyModelState = 0x4A70D491; // props whose models are told the state
constexpr float kTenth = 0.1f;                    // 0x3DCCCCCD at 0x00A8C950
constexpr float kTen = 10.0f;                     // 0x00A94D50
constexpr float kSixtieth = 0.016666668f;         // 0x3C888889 at 0x00A94124
constexpr float kHalf = 0.5f;                     // 0x00A84D2C

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

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// A new cGZMessage, referenced once by the constructor's caller. Like the
// game, no test of the allocation before the first call through it.
void* NewMessage()
{
    void* const memory = CallCdecl<void*, std::size_t>(addr::RZ_operator_new, 0x2C);
    return memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// The date range's two dates destroyed (the end first) and its memory
// freed, as Shutdown and SetSimulatorDateRange do it.
void FreeDates(char* range)
{
    CallThis<void>(addr::cRZDate_Destroy, range + dates::kEnd);
    CallThis<void>(addr::cRZDate_Destroy, range + dates::kStart);
    CallCdecl<void, void*>(addr::SC4_operator_delete, range);
}

// A new date range: 0x28 bytes with its two dates constructed; null when
// the allocation fails (the callers go on with it, as the game does).
char* NewDates()
{
    char* const range = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, dates::kSize);
    if (range) {
        CallThis<void*>(addr::cRZDate_Default, range + dates::kStart);
        CallThis<void*>(addr::cRZDate_Default, range + dates::kEnd);
    }
    return range;
}

// The cIGZDate interface of the range's start or end date.
char* DateOf(char* self, std::size_t which)
{
    return At<char*>(self, field::kDates) + which + dates::kInterface;
}

// The game's small list of pointers (0x005ECCC0): a count that starts at
// -1, a heap buffer, eight entries in place. Up to seven fit in place.
struct SmallList {
    int32_t count;
    void** heap;
    void* local[8];
};

void** SmallListReserve(SmallList* list, uint32_t count)
{
    if (list->count == -1) {
        list->count = static_cast<int32_t>(count);
        if (count < 8)
            return list->local;
        list->heap = CallCdecl<void**, std::size_t>(addr::SC4_operator_new, count << 2);
        return list->heap;
    }
    return CallCdecl<void**, std::size_t>(addr::SC4_operator_new, count * 4);
}

// What SetState and DoMessage do with the placeable object's models: the
// list of `count` of them fetched (the placeable's slot 4, which lowers the
// count to what it has), each asked for its model interface and that
// interface's slot at `offset` called with one argument, then released through
// its virtual base (the pointer at the interface + 4 is the table whose
// second entry is the base's offset).
void TellModels(void* placeable, uint32_t count, std::size_t offset, uint32_t argument)
{
    SmallList list;
    list.count = -1;
    list.heap = nullptr;
    void** const items = SmallListReserve(&list, count);
    Virtual<void>(placeable, 0x10, items, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* model = nullptr;
        if (Virtual<uint8_t>(items[i], 0x0, kIidModel, &model)) {
            Virtual<void>(model, offset, argument);
            char* const base = static_cast<char*>(model) + 4 + At<int32_t>(At<char*>(model, 0x4), 0x4);
            Virtual<uint32_t>(base, 0x8);
        }
    }
    if (list.count != -1 && list.heap)
        CallCdecl<void, void*>(addr::SC4_operator_delete, list.heap);
}

// ====================================================================================
// The queries and setters (`self` is the prop's own part)
// ====================================================================================

// Slot 0 (0x005EC960): the prop interface and the class ID give the
// object, the message target interface the object + 4 (null for a null
// object, and the reference still taken through it), everything else the
// occupant base (object + 8).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidProp) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == kIidMessageTarget) {
        *object = self ? self + field::kMessageTarget : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == kClassId) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The table of prop type IDs (0x005EF2E0): the index of an ID, appended
// when it is new. The index comes back in 16 bits: the 65,537th type of a
// session would get the first one's index. Copied.
struct TypeVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

uint16_t TypeIndex(uint32_t type)
{
    TypeVector& table = Global<TypeVector>(addr::cSC4PropOccupant_TypeTable);
    const int32_t count =
        static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(table.end) - reinterpret_cast<std::uintptr_t>(table.begin)) >> 2;
    int32_t index = 0;
    if (count > 0) {
        while (table.begin[index] != type) {
            if (++index >= count)
                break;
        }
    }
    if (index == count) {
        if (table.end != table.capacity) {
            if (table.end)
                *table.end = type;
            ++table.end;
        } else {
            // The third argument is the address of a byte the game never
            // wrote (an empty allocator object).
            uint8_t allocator = 0;
            CallThis<void, uint32_t*, uint32_t*, void*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, &table, table.end,
                                                                             &type, static_cast<void*>(&allocator), 1u, 1u);
        }
    }
    return static_cast<uint16_t>(index);
}

// Slot 7 (0x005ED280): the ID at the prop's index; the index is not tested
// against the table's size.
uint32_t __fastcall GetPropType(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPropType", logged);
    return Global<uint32_t*>(addr::cSC4PropOccupant_TypeTable)[At<uint16_t>(self, field::kTypeIndex)];
}

// Slot 8 (0x005EF4C0). EAX keeps the index.
uint32_t __fastcall SetPropType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("SetPropType", logged);
    const uint16_t index = TypeIndex(type);
    At<uint16_t>(self, field::kTypeIndex) = index;
    return index;
}

// Slot 9 (0x005EC9D0): the byte, sign extended.
int32_t __fastcall GetOrientation(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetOrientation", logged);
    return At<int8_t>(self, field::kOrientation);
}

// Slot 10 (0x005EC9E0): clamped to 0-3.
uint32_t __fastcall SetOrientation(char* self, void*, int32_t orientation)
{
    static bool logged = false;
    FirstCall("SetOrientation", logged);
    At<int8_t>(self, field::kOrientation) = static_cast<int8_t>(orientation < 0 ? 0 : orientation > 3 ? 3 : orientation);
    return 1;
}

// Slot 12 (0x005ED560).
uint32_t __fastcall GetState(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetState", logged);
    return At<uint8_t>(self, field::kState);
}

// Slot 18 (0x005ED570): AL only.
uint8_t __fastcall GetRandomChance(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetRandomChance", logged);
    return At<uint8_t>(self, field::kChance);
}

// Slot 23 (0x005ECAE0). AL keeps the argument.
uint8_t __fastcall SetPropUsage(char* self, void*, uint32_t usage)
{
    static bool logged = false;
    FirstCall("SetPropUsage", logged);
    At<uint8_t>(self, field::kUsage) = static_cast<uint8_t>(usage);
    return static_cast<uint8_t>(usage);
}

uint8_t __fastcall SetInManager(char* self, void*, uint32_t in);

// 0x005ED170: base flag 2 and the placeable object's slot 8. Nothing when
// the flag already says so; false without a placeable object. Setting it
// first puts a prop that is not in the occupant manager into it (the
// answer is not looked at).
uint8_t SetPlaceableFlag(char* self, uint32_t on)
{
    static bool logged = false;
    FirstCall("SetPlaceableFlag", logged);
    const uint8_t wanted = static_cast<uint8_t>(on);
    if (((At<uint8_t>(self, field::kFlags) >> 1) & 1) == wanted)
        return 1;
    void* const placeable = Virtual<void*>(self + field::kOccupant, occupant::kGetPlaceableObject);
    if (!placeable)
        return 0;
    Virtual<uint32_t>(placeable, 0x4);
    uint8_t flags = At<uint8_t>(self, field::kFlags);
    if (wanted) {
        if (!(flags & 1))
            SetInManager(self, nullptr, 1u);
        flags = At<uint8_t>(self, field::kFlags) | 2;
    } else {
        flags &= 0xFD;
    }
    At<uint8_t>(self, field::kFlags) = flags;
    Virtual<void>(placeable, 0x20, on);
    Virtual<uint32_t>(placeable, 0x8);
    return 1;
}

// 0x005EE1A0: base flag 1, the prop in the occupant manager. Taking it
// out clears the placeable flag first and removes it (slot 21: occupant,
// no shutdown, no data); putting it in is the manager's slot 20, and the
// flag is set only when that succeeds. False without a manager.
uint8_t __fastcall SetInManager(char* self, void*, uint32_t in)
{
    static bool logged = false;
    FirstCall("SetInManager", logged);
    const uint8_t flags = At<uint8_t>(self, field::kFlags);
    const uint8_t is = flags & 1;
    if (is == static_cast<uint8_t>(in))
        return 1;
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    if (!manager)
        return 0;
    if (is) {
        if (flags & 2)
            SetPlaceableFlag(self, 0u);
        Virtual<uint8_t>(manager, 0x54, static_cast<void*>(self + field::kOccupant), 0u, 0u);
        At<uint8_t>(self, field::kFlags) &= 0xFE;
        return 1;
    }
    if (!Virtual<uint8_t>(manager, 0x50, static_cast<void*>(self + field::kOccupant), 0u))
        return 0;
    At<uint8_t>(self, field::kFlags) |= 1;
    return 1;
}

// 0x005ED220: the box becomes the size, from the origin, and is moved back
// to the position it had (the occupant part's slots 8 and 9, which derived
// classes replace). The position starts as the game's zero float
// (0x00B231A0).
void __fastcall SetSize(char* self, void*, const uint32_t* size)
{
    static bool logged = false;
    FirstCall("SetSize", logged);
    const uint32_t zero = Global<uint32_t>(addr::cSC4PropOccupant_Zero);
    uint32_t position[3] = {zero, zero, zero};
    char* const part = self + field::kOccupant;
    Virtual<void*>(part, occupant::kGetPosition, static_cast<uint32_t*>(position));
    At<uint32_t>(self, field::kMin) = 0;
    At<uint32_t>(self, field::kMin + 4) = 0;
    At<uint32_t>(self, field::kMin + 8) = 0;
    At<uint32_t>(self, field::kMax) = size[0];
    At<uint32_t>(self, field::kMax + 4) = size[1];
    At<uint32_t>(self, field::kMax + 8) = size[2];
    Virtual<void*>(part, occupant::kSetPosition, static_cast<uint32_t*>(position));
}

// --- The occupant part (`part` is the object + 8) ---------------------------------

// Slot 4 (0x005EE160): the date range freed, then the base's Shutdown.
uint8_t __fastcall Shutdown(char* part, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    char* const self = part - field::kOccupant;
    if (char* const range = At<char*>(self, field::kDates)) {
        FreeDates(range);
        At<char*>(self, field::kDates) = nullptr;
    }
    return CallThis<uint8_t>(addr::cSC4Occupant_Shutdown, part);
}

// Slot 8 (0x005ED1F0): the box's centre in x and z (the sums on the FPU,
// times 0.5, stored as floats) and its low y. EAX keeps the argument.
float* __fastcall GetPosition(char* part, void*, float* position)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    char* const self = part - field::kOccupant;
    position[0] = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax)) + static_cast<double>(At<float>(self, field::kMin))) *
        static_cast<double>(kHalf));
    std::memcpy(&position[1], self + field::kMin + 4, 4);
    position[2] = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax + 8)) + static_cast<double>(At<float>(self, field::kMin + 8))) *
        static_cast<double>(kHalf));
    return position;
}

// Slot 9 (0x005EDE00): the box moved so that the position is its centre in
// x and z and its low y, keeping its size. The game keeps half the width
// and the height on the FPU and stores half the depth as a float before
// using it; the reads of the position and the stores into the box are in
// its order. EAX keeps the argument.
const float* __fastcall SetPosition(char* part, void*, const float* position)
{
    static bool logged = false;
    FirstCall("SetPosition", logged);
    char* const self = part - field::kOccupant;
    const double halfX =
        (static_cast<double>(At<float>(self, field::kMax)) - static_cast<double>(At<float>(self, field::kMin))) *
        static_cast<double>(kHalf);
    const double height =
        static_cast<double>(At<float>(self, field::kMax + 4)) - static_cast<double>(At<float>(self, field::kMin + 4));
    const float halfZ = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax + 8)) - static_cast<double>(At<float>(self, field::kMin + 8))) *
        static_cast<double>(kHalf));
    At<float>(self, field::kMin) = static_cast<float>(static_cast<double>(position[0]) - halfX);
    At<float>(self, field::kMax) = static_cast<float>(halfX + static_cast<double>(position[0]));
    uint32_t y = 0;
    std::memcpy(&y, &position[1], 4);
    At<uint32_t>(self, field::kMin + 4) = y;
    At<float>(self, field::kMax + 4) = static_cast<float>(height + static_cast<double>(position[1]));
    At<float>(self, field::kMin + 8) = static_cast<float>(static_cast<double>(position[2]) - static_cast<double>(halfZ));
    At<float>(self, field::kMax + 8) = static_cast<float>(static_cast<double>(halfZ) + static_cast<double>(position[2]));
    return position;
}

// Slot 27 (0x005ECAC0): the lot tag when there is one (not -1).
uint8_t __fastcall GetLotTag(char* part, void*, uint32_t* tag)
{
    static bool logged = false;
    FirstCall("GetLotTag", logged);
    const uint32_t value = At<uint32_t>(part - field::kOccupant, field::kLotTag);
    if (value == 0xFFFFFFFF)
        return 0;
    *tag = value;
    return 1;
}

// +0xC slot 5 (0x005EE0E0).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// ====================================================================================
// The timers
// ====================================================================================

// Slot 11 (0x005ED290): nothing when the state is the same (the argument
// compared whole with the byte). Else the byte is stored; an initialized
// prop posts message 0xE9C86DDB with itself (as void 1 and as the object)
// and the state; and a prop with property 0x4A70D491 gives the state to
// the models of its placeable object (their slot 10). No return value.
void __fastcall SetState(char* self, void*, uint32_t state)
{
    static bool logged = false;
    FirstCall("SetState", logged);
    if (At<uint8_t>(self, field::kState) == state)
        return;
    char* const part = self + field::kOccupant;
    At<uint8_t>(self, field::kState) = static_cast<uint8_t>(state);
    if (Virtual<uint8_t>(part, occupant::kIsInitialized)) {
        void* const message = NewMessage();
        Virtual<uint32_t>(message, 0x4);
        Virtual<void>(message, 0x14, kMessageState);
        Virtual<void>(message, 0x30, static_cast<void*>(self));
        Virtual<void*>(message, 0x84, static_cast<void*>(self));
        Virtual<void>(message, 0x40, state);
        Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
        Virtual<uint32_t>(message, 0x8);
    }
    if (!Virtual<uint8_t>(self + field::kHolder, 0xC, kPropertyModelState))
        return;
    void* const placeable = Virtual<void*>(part, occupant::kGetPlaceableObject);
    if (!placeable)
        return;
    Virtual<uint32_t>(placeable, 0x4);
    const uint32_t count = Virtual<uint32_t>(placeable, 0xC);
    if (count)
        TellModels(placeable, count, 0x28, state);
    Virtual<uint32_t>(placeable, 0x8);
}

// Slot 21 (0x005ECA50): the mask's bits set or cleared in the timer byte.
// When the low four bits become all set, a draw below 100 from the props'
// generator decides by the chance whether the state goes to 0; when they
// stop being all set the state goes to 1 (both through slot 11, which
// derived classes replace). No return value.
void __fastcall SetPropTimerState(char* self, void*, uint32_t mask, uint32_t on)
{
    static bool logged = false;
    FirstCall("SetPropTimerState", logged);
    const uint8_t before = At<uint8_t>(self, field::kTimer);
    const bool wasAll = (before & 0xF) == 0xF;
    if (static_cast<uint8_t>(on))
        At<uint8_t>(self, field::kTimer) = before | static_cast<uint8_t>(mask);
    else
        At<uint8_t>(self, field::kTimer) = static_cast<uint8_t>(~static_cast<uint8_t>(mask)) & before;
    const bool isAll = (At<uint8_t>(self, field::kTimer) & 0xF) == 0xF;
    if (isAll == wasAll)
        return;
    if (isAll) {
        const uint32_t draw =
            CallThis<uint32_t, uint32_t>(addr::SC4Random_Below, reinterpret_cast<void*>(addr::cSC4PropOccupant_Random), 100u);
        if (draw >= At<uint8_t>(self, field::kChance))
            return;
        Virtual<void>(self, slot::kSetState, 0u);
    } else {
        Virtual<void>(self, slot::kSetState, 1u);
    }
}

// Slot 19 (0x005ED580).
uint32_t __fastcall GetPropTimerBitfield(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPropTimerBitfield", logged);
    return At<uint8_t>(self, field::kTimer);
}

// Slot 20 (0x005ED590): 1 when any bit of the mask is set (the mask's low
// byte), else 0, in EAX.
uint32_t __fastcall GetPropTimerState(char* self, void*, uint32_t mask)
{
    static bool logged = false;
    FirstCall("GetPropTimerState", logged);
    return (At<uint8_t>(self, field::kTimer) & static_cast<uint8_t>(mask)) != 0;
}

// Slot 14 (0x005ECA10): the two bytes as hours: the integer times the
// float 0.1 on the FPU, stored as a float. EAX keeps the second byte.
uint32_t __fastcall GetTimeOfDayRange(char* self, void*, float* start, float* end)
{
    static bool logged = false;
    FirstCall("GetTimeOfDayRange", logged);
    *start = static_cast<float>(static_cast<double>(At<uint8_t>(self, field::kHours)) * static_cast<double>(kTenth));
    const uint32_t last = At<uint8_t>(self, field::kHours + 1);
    *end = static_cast<float>(static_cast<double>(last) * static_cast<double>(kTenth));
    return last;
}

// Slot 13 (0x005ED400): the hours times 10, truncated (_ftol), into the
// two bytes. Then, unless the two are equal or one is below zero (or not a
// number), timer bit 1 is set from the clock: on when hour + minute / 60
// lies within start to end. A range over midnight (start above end) gives
// off here; the prop developer's own test (message 0x44778231) handles it.
// The clock's two values are unsigned; minute / 60 is rounded to a float
// before the hour is added on the FPU. No return value.
void __fastcall SetTimeOfDayRange(char* self, void*, float start, float end)
{
    static bool logged = false;
    FirstCall("SetTimeOfDayRange", logged);
    At<uint8_t>(self, field::kHours) = static_cast<uint8_t>(Ftol(static_cast<double>(start) * static_cast<double>(kTen)));
    At<uint8_t>(self, field::kHours + 1) = static_cast<uint8_t>(Ftol(static_cast<double>(end) * static_cast<double>(kTen)));
    if (start == end)
        return;
    if (!(start >= 0.0f) || !(end >= 0.0f))
        return;
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    const float minutes =
        static_cast<float>(static_cast<double>(Virtual<uint32_t>(clock, 0x34)) * static_cast<double>(kSixtieth));
    const double time = static_cast<double>(Virtual<uint32_t>(clock, 0x28)) + static_cast<double>(minutes);
    // The game passes a stack dword with its low byte set to the answer;
    // the callee reads the byte.
    const uint32_t on = !(time < static_cast<double>(start)) && time <= static_cast<double>(end) ? 1u : 0u;
    Virtual<void>(self, slot::kSetPropTimerState, 1u, on);
}

// Slot 15 (0x005EEFF0). Interval 0 frees the date range. Else the range is
// made when missing and gets the interval and the duration (at least 1);
// its start becomes (first, second) of the simulator date's year (the
// date's slot 6: 0x00913113), or of the next year when that start lies
// before today; its end start + duration days. Timer bit 2 is then on when
// today is from the start to the end. The two bytes are read from the low
// bytes of their arguments.
void __fastcall SetSimulatorDateRange(char* self, void*, uint32_t interval, uint32_t duration, uint32_t first,
                                      uint32_t second)
{
    static bool logged = false;
    FirstCall("SetSimulatorDateRange", logged);
    if (interval == 0) {
        if (char* const range = At<char*>(self, field::kDates)) {
            FreeDates(range);
            At<char*>(self, field::kDates) = nullptr;
        }
        return;
    }
    if (!At<char*>(self, field::kDates))
        At<char*>(self, field::kDates) = NewDates();
    At<uint32_t>(At<char*>(self, field::kDates), dates::kInterval) = interval;
    At<uint32_t>(At<char*>(self, field::kDates), dates::kDuration) = duration < 1 ? 1 : duration;
    void* const today = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
    const uint32_t a = first & 0xFF;
    const uint32_t b = second & 0xFF;
    uint32_t year = Virtual<uint32_t>(today, 0x34);
    Virtual<void>(DateOf(self, dates::kStart), 0x18, a, b, year);
    if (Virtual<uint32_t>(DateOf(self, dates::kStart), 0x6C, today)) {
        year = Virtual<uint32_t>(today, 0x34) + 1;
        Virtual<void>(DateOf(self, dates::kStart), 0x18, a, b, year);
    }
    // The game makes a date from the day number (0x00912E7B) and copies its
    // day number and its count (zero) into the end.
    char* range = At<char*>(self, field::kDates);
    const uint32_t endDay = Virtual<uint32_t>(range + dates::kStart + dates::kInterface, 0x8C, At<uint32_t>(range, dates::kDuration));
    range = At<char*>(self, field::kDates);
    At<uint32_t>(range, dates::kEnd + dates::kDayNumber) = endDay;
    At<uint32_t>(range, dates::kEnd + 0xC) = 0;
    uint32_t on = 0;
    if (Virtual<uint32_t>(today, 0x78, DateOf(self, dates::kStart)) &&
        Virtual<uint32_t>(today, 0x70, DateOf(self, dates::kEnd)))
        on = 1;
    Virtual<void>(self, slot::kSetPropTimerState, 2u, on);
}

// Slot 16 (0x005EE230): nothing without a date range or with interval 0.
// While timer bit 2 is off: from the start date on, the bit goes on and
// the start moves to the next time (interval 365: the same day of the next
// year, set through the date's slots 13, 15, 23 and 6; else the interval
// in days added, slot 39), and that is all for this call. While it is on:
// from the end date on, the bit goes off and the end becomes the (already
// moved) start + duration days.
void __fastcall TickSimulatorDate(char* self, void*, void* date)
{
    static bool logged = false;
    FirstCall("TickSimulatorDate", logged);
    char* range = At<char*>(self, field::kDates);
    if (!range || At<uint32_t>(range, dates::kInterval) == 0)
        return;
    if (!Virtual<uint8_t>(self, slot::kGetPropTimerState, 2u) &&
        Virtual<uint32_t>(date, 0x78, DateOf(self, dates::kStart))) {
        Virtual<void>(self, slot::kSetPropTimerState, 2u, 1u);
        range = At<char*>(self, field::kDates);
        if (At<uint32_t>(range, dates::kInterval) == 365) {
            char* const start = range + dates::kStart + dates::kInterface;
            const uint32_t year = Virtual<uint32_t>(start, 0x34) + 1;
            const uint32_t b = Virtual<uint32_t>(start, 0x3C);
            const uint32_t a = Virtual<uint32_t>(start, 0x5C);
            Virtual<void>(DateOf(self, dates::kStart), 0x18, a, b, year);
        } else {
            Virtual<void*>(range + dates::kStart + dates::kInterface, 0x9C, At<uint32_t>(range, dates::kInterval));
        }
        return;
    }
    if (!Virtual<uint8_t>(self, slot::kGetPropTimerState, 2u))
        return;
    if (!Virtual<uint32_t>(date, 0x78, DateOf(self, dates::kEnd)))
        return;
    Virtual<void>(self, slot::kSetPropTimerState, 2u, 0u);
    range = At<char*>(self, field::kDates);
    const uint32_t endDay = Virtual<uint32_t>(range + dates::kStart + dates::kInterface, 0x8C, At<uint32_t>(range, dates::kDuration));
    range = At<char*>(self, field::kDates);
    At<uint32_t>(range, dates::kEnd + dates::kDayNumber) = endDay;
    At<uint32_t>(range, dates::kEnd + 0xC) = 0;
}

// The message target's slot 3 (0x005ECFB0; `part` is the object + 4): on
// the frame message 0x86AD10EE the models of the placeable object get the
// message's data 3 (a float: the time) times the simulator's animation
// time dilation (the product on the FPU, stored as a float) through their
// slot 15. Always true.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    if (Virtual<uint32_t>(message, 0x10) != kMessageFrame)
        return 1;
    void* const placeable = Virtual<void*>(part + 4, occupant::kGetPlaceableObject);
    if (!placeable)
        return 1;
    Virtual<uint32_t>(placeable, 0x4);
    const uint32_t count = Virtual<uint32_t>(placeable, 0xC);
    if (count) {
        const uint32_t bits = Virtual<uint32_t>(message, 0x4C);
        float seconds = 0.0f;
        std::memcpy(&seconds, &bits, 4);
        const double dilation = Virtual<float>(Global<void*>(addr::SC4_SimulatorService), 0x80);
        TellModels(placeable, count, 0x3C, Bits(static_cast<float>(dilation * static_cast<double>(seconds))));
    }
    Virtual<uint32_t>(placeable, 0x8);
    return 1;
}

// ====================================================================================
// Save
// ====================================================================================

// +0xC slot 3 (0x005ECE90; `part` = object + 0xC): version 6, the occupant
// base's record, then the prop type's ID (looked up by the index), the box,
// orientation, state, the two hour bytes, whether there is a date range and
// the range (interval, duration, the two dates' day numbers), chance, usage,
// the lot tag and the timer byte. The layout is in
// tools/re/save_schemas/propocc.py. When the base fails nothing more is
// written; the answer is in both cases whether the stream is free of
// errors.
//
// What can change a record's size from one save to the next (looked at for
// the "prop pox" defect; nothing here is fixed): the date range's 17 bytes
// are written only while the object has a range (SetSimulatorDateRange
// makes and frees it at any time), and the holder's own properties inside
// the base's part. Write and Read agree on every width.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 6u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x24,
                         Global<uint32_t*>(addr::cSC4PropOccupant_TypeTable)[At<uint16_t>(self, field::kTypeIndex)]);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kMin);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kMax);
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kOrientation)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kState)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kHours)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kHours + 1)});
        // The game passes its argument's stack slot (the stream pointer)
        // with the low byte replaced by the answer.
        const bool dated = At<char*>(self, field::kDates) != nullptr;
        const uint32_t word = (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream)) & 0xFFFFFF00u) | (dated ? 1u : 0u);
        CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, word);
        if (dated) {
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(At<char*>(self, field::kDates), dates::kInterval));
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(At<char*>(self, field::kDates), dates::kDuration));
            CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteDate, stream, At<char*>(self, field::kDates) + dates::kStart);
            CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteDate, stream, At<char*>(self, field::kDates) + dates::kEnd);
        }
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kChance)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kUsage)});
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kLotTag));
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kTimer)});
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0xC slot 4 (0x005EF360; `part` = object + 0xC). The version is read and
// never tested before the fields: whatever it is, the record is taken as
// laid out above; only the timer byte depends on it (above 5: read; else
// 0xF). The version and the type ID are read into stack slots the game did
// not write before (zeros here); the date flag into the argument's slot
// (the stream pointer until the byte is stored). A range is allocated
// without a look at a range the object may already have, and when the
// allocation fails the stream is given a null address. Copied.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        uint32_t type = 0;
        Virtual<uint8_t>(stream, 0x24, &type);
        At<uint16_t>(self, field::kTypeIndex) = TypeIndex(type);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kMin);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kMax);
        Virtual<uint8_t>(stream, 0x10, self + field::kOrientation);
        Virtual<uint8_t>(stream, 0x14, self + field::kState);
        Virtual<uint8_t>(stream, 0x14, self + field::kHours);
        Virtual<uint8_t>(stream, 0x14, self + field::kHours + 1);
        uint32_t dated = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
        CallCdecl<void*, void*, uint32_t*>(addr::SC4Stream_ReadBool, stream, &dated);
        if (static_cast<uint8_t>(dated)) {
            char* const range = NewDates();
            At<char*>(self, field::kDates) = range;
            Virtual<uint8_t>(stream, 0x24, range + dates::kInterval);
            Virtual<uint8_t>(stream, 0x24, At<char*>(self, field::kDates) + dates::kDuration);
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadDate, stream, At<char*>(self, field::kDates) + dates::kStart);
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadDate, stream, At<char*>(self, field::kDates) + dates::kEnd);
        }
        Virtual<uint8_t>(stream, 0x14, self + field::kChance);
        Virtual<uint8_t>(stream, 0x14, self + field::kUsage);
        Virtual<uint8_t>(stream, 0x24, self + field::kLotTag);
        if (static_cast<uint16_t>(version) > 5)
            Virtual<uint8_t>(stream, 0x14, self + field::kTimer);
        else
            At<uint8_t>(self, field::kTimer) = 0xF;
    }
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
            log::Write("propocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    for (const Detour& detour : detours) {
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
    }
    log::Write("propocc %s: %u detours", what, static_cast<unsigned>(N));
    return true;
}

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

// GetGZCLSID is in the prop's own serializable vtable only (the class
// registration's direct call at 0x0046077E runs before any replacement is
// installed). GetRandomChance is four bytes of code, in the prop's and the
// six derived vtables and called from nowhere directly: its slot is
// swapped in all seven.
const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4PropOccupant_vftable_C, 5, addr::cSC4PropOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4PropOccupant_vftable, 18, addr::cSC4PropOccupant_GetRandomChance, GetRandomChance),
    OPENSC4_SLOT(addr::cSC4PropOccupant_DerivedVftable_AA4730, 18, addr::cSC4PropOccupant_GetRandomChance, GetRandomChance),
    OPENSC4_SLOT(addr::cSC4PropOccupant_DerivedVftable_AA4B30, 18, addr::cSC4PropOccupant_GetRandomChance, GetRandomChance),
    OPENSC4_SLOT(addr::cSC4PropOccupant_DerivedVftable_AA4CC8, 18, addr::cSC4PropOccupant_GetRandomChance, GetRandomChance),
    OPENSC4_SLOT(addr::cSC4PropOccupant_DerivedVftable_AA4E78, 18, addr::cSC4PropOccupant_GetRandomChance, GetRandomChance),
    OPENSC4_SLOT(addr::cSC4PropOccupant_DerivedVftable_AA5418, 18, addr::cSC4PropOccupant_GetRandomChance, GetRandomChance),
    OPENSC4_SLOT(addr::cSC4PropOccupant_DerivedVftable_AA5620, 18, addr::cSC4PropOccupant_GetRandomChance, GetRandomChance),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("propocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("propocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

const Detour kSlotDetours[] = {
    OPENSC4_DETOUR(addr::cSC4PropOccupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetPropType, GetPropType),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetPropType, SetPropType),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetOrientation, GetOrientation),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetOrientation, SetOrientation),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetState, GetState),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetPropUsage, SetPropUsage),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetInManager, SetInManager),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetSize, SetSize),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetPosition, GetPosition),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetPosition, SetPosition),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetLotTag, GetLotTag),
};

const Detour kTimerDetours[] = {
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetState, SetState),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetPropTimerState, SetPropTimerState),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetPropTimerBitfield, GetPropTimerBitfield),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetPropTimerState, GetPropTimerState),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_GetTimeOfDayRange, GetTimeOfDayRange),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetTimeOfDayRange, SetTimeOfDayRange),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_SetSimulatorDateRange, SetSimulatorDateRange),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_TickSimulatorDate, TickSimulatorDate),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_DoMessage, DoMessage),
};

const Detour kSaveDetours[] = {
    OPENSC4_DETOUR(addr::cSC4PropOccupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_Read, Read),
};

// 0x005ED170 (SetPlaceableFlag) has one caller, 0x005EE1A0, ours in the
// same group, and 0x005EF2E0 (TypeIndex) two, SetPropType and Read: both
// stay unhooked. With only one of the slots and save groups on, the other
// side's game code uses the game's TypeIndex on the same table.
bool InstallSlots(registry::Mode)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(kSlots, "slots"))
        return false;
    return InstallDetours(kSlotDetours, "slots") && SwapSlots(kSlots, "slots");
}

bool InstallTimers(registry::Mode) { return InstallDetours(kTimerDetours, "timers"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSaveDetours, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.propocc.slots", "simulators/propocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PropOccupant_QueryInterface,
                        "Our cSC4PropOccupant queries and setters: QueryInterface, the prop type, orientation, GetState, "
                        "usage, position, size, the lot tag, Shutdown, the occupant manager flag (13 detours), GetGZCLSID "
                        "and GetRandomChance (slot swaps; src/core/propocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.propocc.timers", "simulators/propocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PropOccupant_SetPropTimerState,
                        "Our cSC4PropOccupant timers: SetState, the timer bits, the hours and the date range, "
                        "TickSimulatorDate and the frame message (9 detours; src/core/propocc_sim.h)",
                        false, InstallTimers}),
    registry::Register({"simulators.propocc.save", "simulators/propocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PropOccupant_Write,
                        "Our cSC4PropOccupant Write and Read (the save record, version 6; 2 detours; "
                        "src/core/propocc_sim.h)",
                        false, InstallSave}),
};

// ====================================================================================
// Our object
// ====================================================================================

// Copies of the game's six vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 24, kSlots4 = 4, kSlots8 = 34, kSlotsC = 6, kSlots10 = 25, kSlots24 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable24[kSlots24];

void* PoolOf()
{
    return reinterpret_cast<void*>(addr::cSC4PropOccupant_Pool);
}

// An item of the prop pool (0x005ECE60, and inline in the factory): the
// current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after.
void* PoolAllocate()
{
    char* const pool = static_cast<char*>(PoolOf());
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x005EEF90): the occupant base destroyed (the
// class stores no vtables and frees nothing of its own here), and with
// flag 1 the memory back to the pool (the page header is at the address
// with its low 13 bits clear). A null object is passed on as null.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::cSC4Occupant_Destroy, self ? self + field::kOccupant : nullptr);
    if ((flags & 1) && self) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFE000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, PoolOf(), page);
        else if (used == 0x4000003A)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, PoolOf(), page);
    }
    return self;
}

// The holder part's slot 22 (0x005EE130).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PropOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4PropOccupant_vftable_10), sizeof(g_vtable10));
    std::memcpy(g_vtable24, reinterpret_cast<const void*>(addr::cSC4PropOccupant_vftable_24), sizeof(g_vtable24));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable10[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    log::Write("propocc object: vtables copied (slots 11/18 %p %p, +4 slot 3 %p, +8 slots 4/9 %p %p, +C slots 3/4 %p %p)",
               g_vtable[11], g_vtable[18], g_vtable4[3], g_vtable8[4], g_vtable8[9], g_vtableC[3], g_vtableC[4]);
}

// The constructor (detour of 0x005EE050): the occupant base constructed at
// its address on the object + 8, our vtables, the fields zeroed, the lot
// tag -1, the occupant type and the chance 100. +0x65 to +0x67 are left as
// they are. (The game stores two base vtables, 0x00AA4550 and 0x00A81174,
// at +0 and +4 before the base's constructor; nothing reads them.) A
// derived class's constructor stores its own vtables over ours right
// after, as it does over the game's.
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
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kMessageTarget) = g_vtable4;
    At<void**>(self, field::kOccupant) = g_vtable8;
    At<void**>(self, field::kSerializable) = g_vtableC;
    At<void**>(self, field::kHolder) = g_vtable10;
    At<void**>(self, field::kExemplarPart) = g_vtable24;
    for (std::size_t offset = field::kMin; offset <= field::kDates; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint16_t>(self, field::kTypeIndex) = 0;
    At<uint8_t>(self, field::kOrientation) = 0;
    At<uint8_t>(self, field::kState) = 0;
    At<uint8_t>(self, field::kUsage) = 0;
    At<uint8_t>(self, field::kHours) = 0;
    At<uint8_t>(self, field::kHours + 1) = 0;
    At<uint8_t>(self, field::kTimer) = 0;
    At<uint32_t>(self, field::kLotTag) = 0xFFFFFFFF;
    At<uint32_t>(self, field::kType) = kOccupantType;
    At<uint8_t>(self, field::kChance) = 100;
    static int created = 0;
    if (created++ < 3)
        log::Write("propocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x005ECE60; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x005EEF50): an item of the pool, constructed.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return Construct(static_cast<char*>(PoolAllocate()), nullptr);
}

const Detour kObjectDetours[] = {
    OPENSC4_DETOUR(addr::cSC4PropOccupant_cSC4PropOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_Allocate, Allocate),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_Create, Create),
    OPENSC4_DETOUR(addr::cSC4PropOccupant_DeletingDestructor, DeletingDestructor),
};

#undef OPENSC4_DETOUR

// Every construction path: the factory (through the class registration's
// pointer when a save is read), the prop manager's 0x004A2550 / 0x004A255E
// and 0x004A309D / 0x004A30AB (which read only EAX after the allocator and
// after the constructor) and the six derived constructors (which go on with
// ESI and EDI). The deleting destructor is reached through the holder
// part's slot 22: ours in the copies, and by the detour for the game's
// thunk.
bool ApplyObject(registry::Mode) { return InstallDetours(kObjectDetours, "object"); }

const bool g_registeredObject = registry::Register({
    "simulators.propocc.object",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupant_cSC4PropOccupant,
    "Our cSC4PropOccupant object: the constructor (also the six derived classes' base constructor), the factory, the "
    "pool allocation and the deleting destructor (detours), identity; methods from copies of the game vtables taken "
    "once (src/core/propocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::propocc_sim
