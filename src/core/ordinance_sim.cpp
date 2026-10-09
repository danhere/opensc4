// Our cSC4OrdinanceSimulator (build 641), reimplemented function by function
// on the original object (docs/ordinance.md). Each replacement has its own
// switch; calls between the pieces go through the game's vtables and
// addresses, so each piece is the game's or ours as configured.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::ordinance_sim {
namespace {

// Fields of cSC4OrdinanceSimulator (offsets from the object's start; 0x40 bytes).
namespace field {
constexpr std::size_t kInitialised = 0xC; // uint8
constexpr std::size_t kLoaded = 0xD;      // uint8: Read ran, Init keeps the loaded ordinances
constexpr std::size_t kExpense = 0x18;    // int64: last month's ordinance expense (positive)
constexpr std::size_t kIncome = 0x20;     // int64: last month's ordinance income
constexpr std::size_t kPending = 0x28;    // int64: enactment and repeal income since the month began
constexpr std::size_t kMap = 0x30;        // std::map<uint32_t, cISC4Ordinance*> header; +0x34 its size
constexpr std::size_t kMapSize = 0x34;
} // namespace field

// cISC4Ordinance slots (byte offsets, gzcom-dll's order).
namespace ord {
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kInit = 0xC;
constexpr std::size_t kGetId = 0x18;
constexpr std::size_t kGetName = 0x1C;
constexpr std::size_t kEnactmentIncome = 0x2C;
constexpr std::size_t kRetractmentIncome = 0x30;
constexpr std::size_t kMiscProperties = 0x40;
constexpr std::size_t kIsAvailable = 0x44;
constexpr std::size_t kIsOn = 0x48;
constexpr std::size_t kIsEnabled = 0x4C;
constexpr std::size_t kAdjustedIncome = 0x50;
constexpr std::size_t kCheckConditions = 0x54;
constexpr std::size_t kSimulate = 0x5C;
constexpr std::size_t kSetAvailable = 0x60;
constexpr std::size_t kSetOn = 0x64;
constexpr std::size_t kSetEnabled = 0x68;
constexpr std::size_t kSetKey = 0x78;
} // namespace ord

// cISC4OrdinanceSimulator slots used between the pieces.
namespace sim {
constexpr std::size_t kRemoveOrdinance = 0x18;
constexpr std::size_t kSetOrdinanceAvailable = 0x28;
constexpr std::size_t kSetOrdinanceOn = 0x34;
constexpr std::size_t kGetOrdinanceById = 0x50;
constexpr std::size_t kGetOrdinanceByName = 0x54;
} // namespace sim

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("ordinance: first %s", name);
}

// --- The ordinance map ------------------------------------------------------
// Nodes: +0x4 parent, +0x8 left, +0xC right, +0x10 key, +0x14 ordinance.

char* Header(void* self)
{
    return At<char*>(self, field::kMap);
}

void* Ordinance(char* node)
{
    return At<void*>(node, 0x14);
}

// In-order successor, as the game's inlined iterator increment.
char* Next(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

// The map's find (0x00497520): the node, or the header.
char* Find(void* self, uint32_t key)
{
    using FindFn = char**(__thiscall*)(void* map, char** result, const uint32_t* key);
    char* result = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(static_cast<char*>(self) + field::kMap, &result, &key);
    return result;
}

struct Pair {
    uint32_t key;
    void* ordinance;
};

// The map's insert (0x0080E070) of a pair whose ordinance reference the
// caller holds; the node takes its own.
void Insert(void* self, Pair* pair)
{
    struct {
        char* node;
        uint8_t inserted;
    } result{};
    using InsertFn = void*(__thiscall*)(void* map, void* result, Pair* pair);
    reinterpret_cast<InsertFn>(addr::OrdinanceMap_Insert)(static_cast<char*>(self) + field::kMap, &result, pair);
}

// 0x0080DFC0: the nodes below a node, each ordinance released.
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        if (void* const ordinance = Ordinance(node))
            Virtual<uint32_t>(ordinance, ord::kRelease);
        Free(node);
        node = left;
    }
}

// The map emptied, as the destructor and Read do.
void ClearMap(void* self)
{
    char* const header = Header(self);
    if (At<uint32_t>(self, field::kMapSize)) {
        EraseNodes(At<char*>(header, 0x4));
        At<char*>(header, 0x8) = header;
        At<char*>(header, 0x4) = nullptr;
        At<char*>(header, 0xC) = header;
        At<uint32_t>(self, field::kMapSize) = 0;
    }
}

// A message from the ordinance simulator (cGZMessage, 0x2C bytes): type,
// the simulator twice (slots 12 and 33), then the ordinance's ID and a flag
// when `ordinance` is given; sent through the message server read first.
void Post(void* self, uint32_t type, void* ordinance, std::size_t flagSlot)
{
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    if (ordinance) {
        Virtual<uint32_t>(message, 0x40, Virtual<uint32_t>(ordinance, ord::kGetId));
        Virtual<uint32_t>(message, 0x54, static_cast<uint32_t>(Virtual<uint8_t>(ordinance, flagSlot)));
    }
    Virtual<uint32_t>(server, 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

void* ById(void* self, uint32_t id)
{
    return Virtual<void*>(self, sim::kGetOrdinanceById, id);
}

// Adds an ordinance's adjusted income to the month's income, or its cost to
// the expense.
void Count(void* self, int64_t adjusted)
{
    if (adjusted > 0)
        At<int64_t>(self, field::kIncome) += adjusted;
    else
        At<int64_t>(self, field::kExpense) -= adjusted;
}

// An ordinance whose conditions changed becomes available or not.
void CheckAvailability(void* self, void* ordinance)
{
    if (Virtual<uint8_t>(ordinance, ord::kIsAvailable)) {
        if (!Virtual<uint8_t>(ordinance, ord::kCheckConditions))
            Virtual<uint8_t>(self, sim::kSetOrdinanceAvailable, Virtual<uint32_t>(ordinance, ord::kGetId), 0u);
    } else if (Virtual<uint8_t>(ordinance, ord::kCheckConditions)) {
        Virtual<uint8_t>(self, sim::kSetOrdinanceAvailable, Virtual<uint32_t>(ordinance, ord::kGetId), 1u);
    }
}

// --- The interface: cISC4OrdinanceSimulator (vtable 0x00ABEAC0) --------------------

// 5 (0x0080E280): replaces an ordinance of the same ID, then applies what it
// brings: available, on (through the simulator, so the enactment counts),
// or available now.
uint8_t __fastcall AddOrdinance(void* self, void*, void* ordinance)
{
    static bool logged = false;
    FirstCall("AddOrdinance", logged);
    if (!ordinance)
        return 0;
    Virtual<uint8_t>(self, sim::kRemoveOrdinance, ordinance);
    Virtual<uint32_t>(ordinance, ord::kAddRef);
    Pair pair{Virtual<uint32_t>(ordinance, ord::kGetId), ordinance};
    Virtual<uint32_t>(ordinance, ord::kAddRef);
    Insert(self, &pair);
    Virtual<uint32_t>(ordinance, ord::kRelease);
    Virtual<uint32_t>(ordinance, ord::kRelease);
    if (Virtual<uint8_t>(ordinance, ord::kIsAvailable)) {
        Virtual<uint8_t>(ordinance, ord::kSetAvailable, 0u);
        Virtual<uint8_t>(self, sim::kSetOrdinanceAvailable, Virtual<uint32_t>(ordinance, ord::kGetId), 1u);
    }
    if (Virtual<uint8_t>(ordinance, ord::kIsOn)) {
        Virtual<uint8_t>(ordinance, ord::kSetOn, 0u);
        Virtual<uint8_t>(self, sim::kSetOrdinanceOn, Virtual<uint32_t>(ordinance, ord::kGetId), 1u);
    }
    if (!Virtual<uint8_t>(ordinance, ord::kIsAvailable) && Virtual<uint8_t>(ordinance, ord::kCheckConditions))
        Virtual<uint8_t>(self, sim::kSetOrdinanceAvailable, Virtual<uint32_t>(ordinance, ord::kGetId), 1u);
    return 1;
}

// 6 (0x0080E130): the ordinance under the given one's ID repealed and made
// unavailable, then erased.
uint8_t __fastcall RemoveOrdinance(void* self, void*, void* ordinance)
{
    static bool logged = false;
    FirstCall("RemoveOrdinance", logged);
    if (!ordinance)
        return 0;
    char* const node = Find(self, Virtual<uint32_t>(ordinance, ord::kGetId));
    if (node == Header(self))
        return 0;
    void* const found = Ordinance(node);
    if (Virtual<uint8_t>(found, ord::kIsOn))
        Virtual<uint8_t>(self, sim::kSetOrdinanceOn, Virtual<uint32_t>(found, ord::kGetId), 0u);
    if (Virtual<uint8_t>(found, ord::kIsAvailable))
        Virtual<uint8_t>(self, sim::kSetOrdinanceAvailable, Virtual<uint32_t>(found, ord::kGetId), 0u);
    using EraseFn = void(__thiscall*)(void* map, char* node);
    reinterpret_cast<EraseFn>(addr::UintMap_Erase)(static_cast<char*>(self) + field::kMap, node);
    return 1;
}

// 7, 8, 16 (0x0080D440, 0x0080D470, 0x0080D3E0): one of the ordinance's flags.
template <std::size_t Slot>
uint32_t __fastcall OrdinanceFlag(void* self, void*, uint32_t id)
{
    void* const ordinance = ById(self, id);
    return ordinance && Virtual<uint8_t>(ordinance, Slot) ? 1u : 0u;
}

// 9 (0x0080D4A0).
uint32_t __fastcall IsOrdinanceAvailableButOff(void* self, void*, uint32_t id)
{
    void* const ordinance = ById(self, id);
    return ordinance && Virtual<uint8_t>(ordinance, ord::kIsAvailable) && !Virtual<uint8_t>(ordinance, ord::kIsOn)
               ? 1u
               : 0u;
}

// 10 (0x0080D580): an ordinance made unavailable is repealed first; the
// change is announced (0xA2D1C5B9); one made available that was on stays on
// through the simulator, so its enactment counts again.
uint8_t __fastcall SetOrdinanceAvailable(void* self, void*, uint32_t id, uint8_t available)
{
    static bool logged = false;
    FirstCall("SetOrdinanceAvailable", logged);
    void* const ordinance = ById(self, id);
    if (!ordinance)
        return 0;
    if (Virtual<uint8_t>(ordinance, ord::kIsAvailable) == available)
        return 1;
    if (!available && Virtual<uint8_t>(ordinance, ord::kIsOn))
        Virtual<uint8_t>(self, sim::kSetOrdinanceOn, id, 0u);
    Virtual<uint8_t>(ordinance, ord::kSetAvailable, static_cast<uint32_t>(available));
    Post(self, 0xA2D1C5B9, ordinance, ord::kIsAvailable);
    if (available && Virtual<uint8_t>(ordinance, ord::kIsOn)) {
        Virtual<uint8_t>(ordinance, ord::kSetOn, 0u);
        Virtual<uint8_t>(self, sim::kSetOrdinanceOn, id, 1u);
    }
    return 1;
}

// 11, 14 (0x0080DAE0, 0x0080DBE0): ordinances with the flag.
template <std::size_t Slot>
uint32_t __fastcall CountWith(void* self, void*)
{
    char* const header = Header(self);
    uint32_t count = 0;
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node))
        if (Virtual<uint8_t>(Ordinance(node), Slot))
            ++count;
    return count;
}

// 12, 15 (0x0080DB40, 0x0080DC40): the IDs of the ordinances with the flag,
// as many as fit; returns how many have it, the count says how many were
// written.
template <std::size_t Slot>
uint32_t __fastcall ArrayWith(void* self, void*, uint32_t* ids, uint32_t* count)
{
    char* const header = Header(self);
    uint32_t total = 0, written = 0;
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node)) {
        void* const ordinance = Ordinance(node);
        if (Virtual<uint8_t>(ordinance, Slot)) {
            ++total;
            if (written < *count)
                ids[written++] = Virtual<uint32_t>(ordinance, ord::kGetId);
        }
    }
    *count = written;
    return total;
}

// 13 (0x0080D690): the ordinance's enactment or repeal income goes to the
// pending income; the change is announced (0xC2C09554).
uint8_t __fastcall SetOrdinanceOn(void* self, void*, uint32_t id, uint8_t on)
{
    static bool logged = false;
    FirstCall("SetOrdinanceOn", logged);
    void* const ordinance = ById(self, id);
    if (!ordinance)
        return 0;
    if (Virtual<uint8_t>(ordinance, ord::kIsOn) == on)
        return 1;
    Virtual<uint8_t>(ordinance, ord::kSetOn, static_cast<uint32_t>(on));
    At<int64_t>(self, field::kPending) +=
        Virtual<int64_t>(ordinance, on ? ord::kEnactmentIncome : ord::kRetractmentIncome);
    Post(self, 0xC2C09554, ordinance, ord::kIsOn);
    return 1;
}

// 17 (0x0080D410).
uint8_t __fastcall SetOrdinanceEnabled(void* self, void*, uint32_t id, uint8_t enabled)
{
    void* const ordinance = ById(self, id);
    if (!ordinance)
        return 0;
    Virtual<uint8_t>(ordinance, ord::kSetEnabled, static_cast<uint32_t>(enabled));
    return 1;
}

// 18 (0x0080DCE0): every ordinance's ID, as many as fit; returns the number
// of ordinances.
uint32_t __fastcall GetOrdinanceIdArray(void* self, void*, uint32_t* ids, uint32_t* count)
{
    char* const header = Header(self);
    uint32_t total = 0, written = 0;
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node)) {
        ++total;
        void* const ordinance = Ordinance(node);
        if (ordinance && written < *count)
            ids[written++] = Virtual<uint32_t>(ordinance, ord::kGetId);
    }
    *count = written;
    return total;
}

// 19 (0x0080D4E0).
uint32_t __fastcall GetOrdinanceIdByName(void* self, void*, void* name)
{
    void* const ordinance = Virtual<void*>(self, sim::kGetOrdinanceByName, name);
    return ordinance ? Virtual<uint32_t>(ordinance, ord::kGetId) : 0u;
}

// 20 (0x0080E040): no reference added.
void* __fastcall GetOrdinanceById(void* self, void*, uint32_t id)
{
    char* const node = Find(self, id);
    return node != Header(self) ? Ordinance(node) : nullptr;
}

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// 21 (0x0080E4F0): the first ordinance (map order) whose name matches,
// both lower-cased; the strings come from the STL allocator, as the game's.
void* __fastcall GetOrdinanceByName(void* self, void*, void* name)
{
    using FromRangeFn = void(__thiscall*)(char** string, const char* first, const char* last, void* allocator);
    using LowerFn = void(__thiscall*)(GameString* string);
    const auto lower = reinterpret_cast<LowerFn>(addr::cRZBaseString_MakeLower);
    const auto vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    const auto length = Virtual<uint32_t>(name, 0x1C);
    const auto* const text = Virtual<const char*>(name, 0x18);
    GameString wanted{};
    char allocator = 0;
    reinterpret_cast<FromRangeFn>(addr::SC4String_FromRange)(&wanted.begin, text, text + length, &allocator);
    wanted.vtable = vtable;
    wanted.zero = 0;
    lower(&wanted);
    void* match = nullptr;
    char* const header = Header(self);
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node)) {
        void* const ordinance = Ordinance(node);
        void* const own = Virtual<void*>(ordinance, ord::kGetName);
        const auto ownLength = Virtual<uint32_t>(own, 0x1C);
        const auto* const ownText = Virtual<const char*>(own, 0x18);
        GameString candidate{};
        const std::size_t size = ownLength + 1;
        if (size != 0) {
            candidate.begin = static_cast<char*>(Allocate(size));
            candidate.capacity = candidate.begin + size;
        }
        std::memmove(candidate.begin, ownText, ownLength);
        candidate.end = candidate.begin + ownLength;
        *candidate.end = 0;
        candidate.vtable = vtable;
        candidate.zero = 0;
        lower(&candidate);
        const std::size_t have = static_cast<std::size_t>(candidate.end - candidate.begin);
        const bool equal = have == static_cast<std::size_t>(wanted.end - wanted.begin) &&
                           std::memcmp(candidate.begin, wanted.begin, have) == 0;
        if (candidate.begin)
            Free(candidate.begin);
        if (equal) {
            match = ordinance;
            break;
        }
    }
    if (wanted.begin)
        Free(wanted.begin);
    return match;
}

// 22, 23 (0x0080D500, 0x0080D510).
template <std::size_t Offset>
int64_t __fastcall Int64Getter(void* self, void*)
{
    return At<int64_t>(self, Offset);
}

// The enacted ordinances' property `id` (their misc properties: slot 3
// HasProperty, slot 9 GetProperty), as the variant behind it: GetPropertyValue
// is property slot `valueSlot`.
template <typename Fn>
void ForEachEnactedProperty(void* self, uint32_t id, std::size_t valueSlot, Fn fn)
{
    char* const header = Header(self);
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node)) {
        void* const ordinance = Ordinance(node);
        if (!ordinance || !Virtual<uint8_t>(ordinance, ord::kIsOn))
            continue;
        void* const properties = Virtual<void*>(ordinance, ord::kMiscProperties);
        if (!properties || !Virtual<uint8_t>(properties, 0xC, id))
            continue;
        void* const property = Virtual<void*>(properties, 0x24, id);
        fn(Virtual<void*>(property, valueSlot));
    }
}

// 24 (0x0080DD70): the sum of the enacted ordinances' property: an sint32 or
// uint32 value (variant types 6, 7), or the `index`th of an array of them
// (0x8006, 0x8007). 32 bits in EAX.
int32_t __fastcall GetCumulativeOffset(void* self, void*, uint32_t id, uint32_t index)
{
    static bool logged = false;
    FirstCall("GetCumulativeOffset", logged);
    int32_t sum = 0;
    ForEachEnactedProperty(self, id, 0x14, [&](void* variant) {
        int32_t value = 0;
        switch (Virtual<uint32_t>(variant, 0xC)) {
        case 6:
        case 7: value = Virtual<int32_t>(variant, 0x128); break;
        case 0x8006:
            if (Virtual<uint32_t>(variant, 0x10) > index)
                value = Virtual<const int32_t*>(variant, 0x19C)[index];
            break;
        case 0x8007:
            if (Virtual<uint32_t>(variant, 0x10) > index)
                value = Virtual<const int32_t*>(variant, 0x1A4)[index];
            break;
        default: break;
        }
        sum += value;
    });
    return sum;
}

// 25 (0x0080DEB0): the product of the enacted ordinances' property: a float
// (variant type 10) or the `index`th of a float array (0x800A), else 1.
float __fastcall GetCumulativeFactor(void* self, void*, uint32_t id, uint32_t index)
{
    static bool logged = false;
    FirstCall("GetCumulativeFactor", logged);
    float product = 1.0f;
    ForEachEnactedProperty(self, id, 0x18, [&](void* variant) {
        float value = 1.0f;
        const uint32_t type = Virtual<uint32_t>(variant, 0xC);
        if (type == 10)
            value = Virtual<float>(variant, 0x140);
        else if (type == 0x800A && Virtual<uint32_t>(variant, 0x10) > index)
            value = Virtual<const float*>(variant, 0x1BC)[index];
        product = static_cast<float>(static_cast<double>(value) * product);
    });
    return product;
}

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x0080E280, AddOrdinance),
    OPENSC4_SLOT(6, 0x0080E130, RemoveOrdinance),
    OPENSC4_SLOT(7, 0x0080D440, OrdinanceFlag<ord::kIsAvailable>),
    OPENSC4_SLOT(8, 0x0080D470, OrdinanceFlag<ord::kIsOn>),
    OPENSC4_SLOT(9, 0x0080D4A0, IsOrdinanceAvailableButOff),
    OPENSC4_SLOT(10, 0x0080D580, SetOrdinanceAvailable),
    OPENSC4_SLOT(11, 0x0080DAE0, CountWith<ord::kIsAvailable>),
    OPENSC4_SLOT(12, 0x0080DB40, ArrayWith<ord::kIsAvailable>),
    OPENSC4_SLOT(13, 0x0080D690, SetOrdinanceOn),
    OPENSC4_SLOT(14, 0x0080DBE0, CountWith<ord::kIsOn>),
    OPENSC4_SLOT(15, 0x0080DC40, ArrayWith<ord::kIsOn>),
    OPENSC4_SLOT(16, 0x0080D3E0, OrdinanceFlag<ord::kIsEnabled>),
    OPENSC4_SLOT(17, 0x0080D410, SetOrdinanceEnabled),
    OPENSC4_SLOT(18, 0x0080DCE0, GetOrdinanceIdArray),
    OPENSC4_SLOT(19, 0x0080D4E0, GetOrdinanceIdByName),
    OPENSC4_SLOT(20, 0x0080E040, GetOrdinanceById),
    OPENSC4_SLOT(21, 0x0080E4F0, GetOrdinanceByName),
    OPENSC4_SLOT(22, 0x0080D500, Int64Getter<field::kExpense>),
    OPENSC4_SLOT(23, 0x0080D510, Int64Getter<field::kIncome>),
    OPENSC4_SLOT(24, 0x0080DD70, GetCumulativeOffset),
    OPENSC4_SLOT(25, 0x0080DEB0, GetCumulativeFactor),
};

#undef OPENSC4_SLOT

// Swaps every listed slot of a vtable or none: each must still hold the
// game's function.
bool SwapSlots(const char* what, std::uintptr_t address, const Slot* slots, std::size_t count)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(address);
    int last = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (vtable[slots[i].index] != slots[i].original) {
            log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", what, slots[i].index,
                       static_cast<unsigned>(address), static_cast<unsigned>(vtable[slots[i].index]),
                       static_cast<unsigned>(slots[i].original));
            return false;
        }
        if (slots[i].index > last)
            last = slots[i].index;
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, (last + 1) * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("%s: VirtualProtect failed (%lu)", what, GetLastError());
        return false;
    }
    for (std::size_t i = 0; i < count; ++i)
        vtable[slots[i].index] = reinterpret_cast<std::uintptr_t>(slots[i].ours);
    VirtualProtect(vtable, (last + 1) * sizeof(*vtable), old, &old);
    log::Write("%s: %u slots of %08X are ours", what, static_cast<unsigned>(count), static_cast<unsigned>(address));
    return true;
}

bool InstallInterface(registry::Mode)
{
    return SwapSlots("ordinance interface", addr::cSC4OrdinanceSimulator_vftable, kSlots,
                     sizeof(kSlots) / sizeof(kSlots[0]));
}

const bool g_registeredInterface = registry::Register({
    "simulators.ordinance.interface",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4OrdinanceSimulator_vftable,
    "Our cISC4OrdinanceSimulator slots 5-25 on the original object: adding and removing ordinances, availability, "
    "enactment, the queries and the cumulative effects (docs/ordinance.md)",
    false,
    InstallInterface,
});

// --- The month (messages 0x66956810 and 0x66956816) ---------------------------------

// 0x0080D840 (message 0x66956810): availability, then the enacted
// ordinances' adjusted income counted again.
void __fastcall Recount(void* self, void*)
{
    static bool logged = false;
    FirstCall("Recount", logged);
    At<int64_t>(self, field::kExpense) = 0;
    At<int64_t>(self, field::kIncome) = 0;
    char* const header = Header(self);
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node)) {
        void* const ordinance = Ordinance(node);
        CheckAvailability(self, ordinance);
        if (Virtual<uint8_t>(ordinance, ord::kIsOn))
            Count(self, Virtual<int64_t>(ordinance, ord::kAdjustedIncome));
    }
}

// 0x0080D950 (MonthPassed): the month's enactment and repeal income first,
// then each enacted ordinance's month (Simulate) and adjusted income,
// availability; then 0xC8EA5018.
void __fastcall MonthPassed(void* self, void*)
{
    static bool logged = false;
    FirstCall("MonthPassed", logged);
    const int64_t pending = At<int64_t>(self, field::kPending);
    At<int64_t>(self, field::kExpense) = 0;
    At<int64_t>(self, field::kIncome) = 0;
    if (pending > 0)
        At<int64_t>(self, field::kIncome) = pending;
    else
        At<int64_t>(self, field::kExpense) = -pending;
    At<int64_t>(self, field::kPending) = 0;
    char* const header = Header(self);
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node)) {
        void* const ordinance = Ordinance(node);
        if (Virtual<uint8_t>(ordinance, ord::kIsOn) && Virtual<uint8_t>(ordinance, ord::kSimulate))
            Count(self, Virtual<int64_t>(ordinance, ord::kAdjustedIncome));
        CheckAvailability(self, ordinance);
    }
    Post(self, 0xC8EA5018, nullptr, 0);
}

using ThisFn = void(__fastcall*)(void*, void*);
ThisFn g_originalRecount = nullptr;
ThisFn g_originalMonthPassed = nullptr;

bool InstallMonth(registry::Mode)
{
    const addr::Entry* recount = addr_check::Find(addr::cSC4OrdinanceSimulator_Recount);
    const addr::Entry* month = addr_check::Find(addr::cSC4OrdinanceSimulator_MonthPassed);
    return game::IsBuild641() && recount && month && hook::Install(*recount, &Recount, &g_originalRecount) &&
           hook::Install(*month, &MonthPassed, &g_originalMonthPassed);
}

const bool g_registeredMonth = registry::Register({
    "simulators.ordinance.month",
    "simulators/ordinance",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OrdinanceSimulator_MonthPassed,
    "Our ordinance month: the monthly income and expense, each ordinance's month and availability, and the "
    "recount (docs/ordinance.md)",
    false,
    InstallMonth,
});

// cIGZMessageTarget2 (+0x4) slot 3 (0x0080E000).
uint8_t __fastcall DoMessage(void* interface4, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(interface4) - 0x4;
    using StepFn = void(__thiscall*)(void* self);
    switch (Virtual<uint32_t>(message, 0x10)) { // GetType
    case 0x66956810: reinterpret_cast<StepFn>(addr::cSC4OrdinanceSimulator_Recount)(self); break;
    case 0x66956816: reinterpret_cast<StepFn>(addr::cSC4OrdinanceSimulator_MonthPassed)(self); break;
    default: break;
    }
    return 1;
}

const Slot kMessageSlots[] = {{3, 0x0080E000, reinterpret_cast<void*>(&DoMessage)}};

bool InstallMessages(registry::Mode)
{
    return SwapSlots("ordinance messages", addr::cSC4OrdinanceSimulator_vftable_4, kMessageSlots, 1);
}

const bool g_registeredMessages = registry::Register({
    "simulators.ordinance.messages",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4OrdinanceSimulator_vftable_4,
    "Our ordinance DoMessage (docs/ordinance.md)",
    false,
    InstallMessages,
});

// --- Init and Shutdown (slots 3 and 4) -------------------------------------------------

// 0x0080D3C0: the callback Init registers once.
uint8_t __cdecl ClearCreated()
{
    Global<uint8_t>(addr::OrdinanceSimulator_Created) = 0;
    return 1;
}

// 0x0080E360 (key): the ordinance class object of the key's instance ID,
// keyed in, initialised and put in the map under its own ID.
void __fastcall CreateOrdinance(void* self, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("CreateOrdinance", logged);
    using GzcomFn = void*(__cdecl*)();
    void* created = nullptr;
    void* const com = reinterpret_cast<GzcomFn>(addr::RZ_GZCOM)();
    if (Virtual<uint8_t>(com, 0xC, key[2], 0x8BD4FC74u, &created)) {
        Virtual<uint8_t>(created, ord::kSetKey, key);
        Virtual<uint8_t>(created, ord::kInit);
        const uint32_t id = Virtual<uint32_t>(created, ord::kGetId);
        void* const ordinance = created;
        if (ordinance)
            Virtual<uint32_t>(ordinance, ord::kAddRef);
        Pair pair{id, ordinance};
        if (ordinance) {
            Virtual<uint32_t>(ordinance, ord::kAddRef);
            Virtual<uint32_t>(ordinance, ord::kRelease);
        }
        Insert(self, &pair);
        if (ordinance)
            Virtual<uint32_t>(ordinance, ord::kRelease);
    }
    if (created)
        Virtual<uint32_t>(created, ord::kRelease);
}

// 0x0080E430 (key, simulator): ordinance exemplars (type 0xE) only.
void __cdecl OnOrdinanceExemplar(const uint32_t* key, void* self)
{
    static bool logged = false;
    FirstCall("OnOrdinanceExemplar", logged);
    using GetUintFn = uint8_t(__cdecl*)(void* properties, uint32_t id, uint32_t* value);
    using CreateFn = void(__thiscall*)(void* self, const uint32_t* key);
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &resource, 0u, 0u))
        return;
    uint32_t type = 0;
    if (reinterpret_cast<GetUintFn>(addr::SC4_GetPropertyUint32)(Virtual<void*>(resource, 0x1C), 0x10, &type) &&
        type == 0xE)
        reinterpret_cast<CreateFn>(addr::cSC4OrdinanceSimulator_CreateOrdinance)(self, key);
    Virtual<uint32_t>(resource, 0x8);
}

// 0x0080E4A0: every exemplar key (type 0x6534284A) through OnOrdinanceExemplar.
void __fastcall CreateOrdinances(void* self, void*)
{
    static bool logged = false;
    FirstCall("CreateOrdinances", logged);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (!resources)
        return;
    void* keys = nullptr;
    if (Virtual<uint32_t>(resources, 0x48, &keys, 0x6534284Au)) {
        using CallbackFn = void(__cdecl*)(const uint32_t*, void*);
        Virtual<uint32_t>(keys, 0x1C, reinterpret_cast<CallbackFn>(&OnOrdinanceExemplar), self);
        Virtual<uint32_t>(keys, 0x8);
    }
}

// Slot 3 (0x0080E760): once, registration with the simulator service under
// the class name; a new city's ordinances from the exemplars.
uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("ordinance init: first Init (%s)", At<uint8_t>(self, field::kLoaded) ? "loaded from a save" : "new");
    }
    if (At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 1;
    if (!Global<uint8_t>(addr::OrdinanceSimulator_Created)) {
        Global<uint8_t>(addr::OrdinanceSimulator_Created) = 1;
        using CallbackFn = uint8_t(__cdecl*)();
        Virtual<uint32_t>(Global<void*>(addr::SC4_Service43C94), 0x114, static_cast<CallbackFn>(&ClearCreated), 0u);
    }
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    GameString name{};
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    void* const text = reinterpret_cast<StringFn>(addr::SC4String_FromText)(
        &name, reinterpret_cast<const char*>(addr::OrdinanceSimulator_Name));
    Virtual<uint32_t>(simulators, 0x4C, static_cast<char*>(self) + 0x4, 6u, text, 2u);
    if (name.begin)
        Free(name.begin);
    if (!At<uint8_t>(self, field::kLoaded)) {
        At<int64_t>(self, field::kExpense) = 0;
        At<int64_t>(self, field::kIncome) = 0;
        At<int64_t>(self, field::kPending) = 0;
        reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4OrdinanceSimulator_CreateOrdinances)(self);
    }
    At<uint8_t>(self, field::kLoaded) = 0;
    return 1;
}

// Slot 4 (0x0080D550): unregisters from the service the city gives (slot 50).
uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (At<uint8_t>(self, field::kInitialised)) {
        At<uint8_t>(self, field::kInitialised) = 0;
        if (void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
            Virtual<uint32_t>(service, 0x50, static_cast<char*>(self) + 0x4);
    }
    return 1;
}

using KeyFn = void(__fastcall*)(void*, void*, const uint32_t*);
using ExemplarFn = void(__cdecl*)(const uint32_t*, void*);
KeyFn g_originalCreateOrdinance = nullptr;
ExemplarFn g_originalOnExemplar = nullptr;
ThisFn g_originalCreateOrdinances = nullptr;

const Slot kInitSlots[] = {{3, 0x0080E760, reinterpret_cast<void*>(&Init)}};
const Slot kShutdownSlots[] = {{4, 0x0080D550, reinterpret_cast<void*>(&Shutdown)}};

bool InstallInit(registry::Mode)
{
    const addr::Entry* create = addr_check::Find(addr::cSC4OrdinanceSimulator_CreateOrdinance);
    const addr::Entry* exemplar = addr_check::Find(addr::cSC4OrdinanceSimulator_OnOrdinanceExemplar);
    const addr::Entry* all = addr_check::Find(addr::cSC4OrdinanceSimulator_CreateOrdinances);
    return game::IsBuild641() && create && exemplar && all &&
           hook::Install(*create, &CreateOrdinance, &g_originalCreateOrdinance) &&
           hook::Install(*exemplar, &OnOrdinanceExemplar, &g_originalOnExemplar) &&
           hook::Install(*all, &CreateOrdinances, &g_originalCreateOrdinances) &&
           SwapSlots("ordinance init", addr::cSC4OrdinanceSimulator_vftable, kInitSlots, 1);
}

bool InstallShutdown(registry::Mode)
{
    return SwapSlots("ordinance shutdown", addr::cSC4OrdinanceSimulator_vftable, kShutdownSlots, 1);
}

const bool g_registeredInit = registry::Register({
    "simulators.ordinance.init",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4OrdinanceSimulator_vftable,
    "Our ordinance Init and the creation of a new city's ordinances from the exemplars (docs/ordinance.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.ordinance.shutdown",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4OrdinanceSimulator_vftable,
    "Our ordinance Shutdown (docs/ordinance.md)",
    false,
    InstallShutdown,
});

// --- Saving: cIGZSerializable (vtable 0x00ABEA98) at +0x8 ----------------------------

constexpr std::size_t kSerializable = 0x8;

using WriteObjectFn = uint8_t(__cdecl*)(void* stream, const void* object);
using ReadObjectFn = uint8_t(__cdecl*)(void* stream, uint32_t interfaceId, void* out, uint32_t flags);

// Version 4: expense, income, the ordinances (references, map order), the
// pending income.
uint8_t __fastcall Write(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    const auto writeReference = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(4));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, field::kExpense));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, field::kIncome));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kMapSize));
    char* const header = Header(self);
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node))
        writeReference(stream, Ordinance(node));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, field::kPending));
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

// Each ordinance comes back as its serializable part; the map key is its
// class ID (GetGZCLSID), which is the ordinance's ID.
uint8_t __fastcall Read(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    const auto readReference = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReference);
    using IndexFn = void**(__thiscall*)(void* map, const uint32_t* key);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 4)
        return 0;
    Virtual<uint8_t>(stream, 0x28, self + field::kExpense);
    Virtual<uint8_t>(stream, 0x28, self + field::kIncome);
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    ClearMap(self);
    for (uint32_t i = 0; i < count; ++i) {
        void* object = nullptr;
        if (!readReference(stream, 0xE4FDA3D4, &object, 0))
            continue;
        void* ordinance = nullptr;
        if (Virtual<uint8_t>(object, 0x0, 0xAA5E2247u, &ordinance)) {
            const uint32_t key = Virtual<uint32_t>(object, 0x14);
            void** const value = reinterpret_cast<IndexFn>(addr::OrdinanceMap_Index)(self + field::kMap, &key);
            void* const old = *value;
            if (ordinance != old) {
                if (ordinance)
                    Virtual<uint32_t>(ordinance, ord::kAddRef);
                *value = ordinance;
                if (old)
                    Virtual<uint32_t>(old, ord::kRelease);
            }
            Virtual<uint32_t>(ordinance, ord::kRelease);
        }
        Virtual<uint32_t>(object, 0x8);
    }
    Virtual<uint8_t>(stream, 0x28, self + field::kPending);
    At<uint8_t>(self, field::kLoaded) = 1;
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

const Slot kSaveSlots[] = {{3, 0x0080D780, reinterpret_cast<void*>(&Write)}};
const Slot kLoadSlots[] = {{4, 0x0080E8B0, reinterpret_cast<void*>(&Read)}};

bool InstallSave(registry::Mode)
{
    return SwapSlots("ordinance save", addr::cSC4OrdinanceSimulator_vftable_8, kSaveSlots, 1);
}

bool InstallLoad(registry::Mode)
{
    return SwapSlots("ordinance load", addr::cSC4OrdinanceSimulator_vftable_8, kLoadSlots, 1);
}

const bool g_registeredSave = registry::Register({
    "simulators.ordinance.save",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4OrdinanceSimulator_vftable_8,
    "Our ordinance simulator Write: the save record, version 4 (docs/ordinance.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.ordinance.load",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4OrdinanceSimulator_vftable_8,
    "Our ordinance simulator Read: the save record, version 4 (docs/ordinance.md)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::ordinance_sim
