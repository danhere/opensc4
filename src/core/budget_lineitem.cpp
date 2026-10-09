// The department budgets' line items (cSC4LineItem, docs/budget.md
// "Departments and line items"): the interface slots and the save record.
#include "core/budget_department.h"
#include "core/budget_sim.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::budget_sim {
namespace {

using FreeFn = void(__cdecl*)(void* p);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The game's stack localised string (0x20 bytes): string, then the key.
struct LocalizedString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
    uint32_t key[3];
};

void AssignRange(char* buffer, const char* from)
{
    char allocator = 0;
    using AssignRangeFn = void*(__thiscall*)(void* buffer, char* first, char* last, void* allocator);
    reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(buffer, At<char*>(const_cast<char*>(from), 0x0),
                                                                 At<char*>(const_cast<char*>(from), 0x4), &allocator);
}

uint32_t __fastcall GetId(char* self, void*)
{
    return At<uint32_t>(self, line::kId);
}

uint32_t __fastcall GetType(char* self, void*)
{
    return At<uint32_t>(self, line::kType);
}

uint8_t __fastcall SetType(char* self, void*, uint32_t type)
{
    At<uint32_t>(self, line::kType) = type;
    return static_cast<uint8_t>(type);
}

// 6 (0x006AC4F0) (string): the name made again first if it was read from a
// save; copied into the string's buffer.
uint8_t __fastcall GetName(char* self, void*, char* name)
{
    if (At<uint8_t>(self, line::kNamePending)) {
        Virtual<uint8_t>(self, lslot::kSetName, At<uint32_t>(self, line::kNameGroup), At<uint32_t>(self, line::kNameInstance));
        At<uint8_t>(self, line::kNamePending) = 0;
    }
    const char* const from = self + line::kName + 4;
    if (from != name + 4)
        AssignRange(name + 4, from);
    return 1;
}

// 7 (0x006AC460) (group, instance): the localised text, or with group 0 the
// name of the building exemplar `instance` (service 0x00B43D68, slot 38).
uint8_t __fastcall SetName(char* self, void*, uint32_t group, uint32_t instance)
{
    static bool logged = false;
    FirstCall("line item SetName", logged);
    At<uint32_t>(self, line::kNameGroup) = group;
    At<uint32_t>(self, line::kNameInstance) = instance;
    if (!group) {
        void* const text = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x98, instance);
        return Virtual<uint8_t>(self + line::kName, 0x34, text);
    }
    LocalizedString text{};
    using TextFn = void*(__thiscall*)(LocalizedString* string, uint32_t group, uint32_t instance, uint32_t flag);
    auto* const made =
        static_cast<char*>(reinterpret_cast<TextFn>(addr::SC4LocalizedString_SC4LocalizedString)(&text, group, instance, 1));
    char* const from = made ? made + 4 : nullptr;
    if (from != self + line::kName + 4)
        AssignRange(self + line::kName + 4, from);
    text.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (text.begin)
        Free(text.begin);
    return 1;
}

template <std::size_t Offset>
int64_t __fastcall GetInt64(char* self, void*)
{
    return At<int64_t>(self, Offset);
}

uint8_t __fastcall SetSecondaryInfoField(char* self, void*, int64_t value)
{
    At<int64_t>(self, line::kSecondary) = value;
    return static_cast<uint8_t>(value);
}

void* Department(char* self)
{
    return At<void*>(self, line::kDepartment);
}

// 12 (0x006A9A00) (expense): the full expense; a city-funded line's current
// expense is it times the department's funding (truncated); the department
// announces the change.
uint8_t __fastcall SetFullExpenses(char* self, void*, int64_t expense)
{
    static bool logged = false;
    FirstCall("line item SetFullExpenses", logged);
    At<int64_t>(self, line::kFull) = expense;
    if (!At<uint8_t>(self, line::kLocal)) {
        const float funding = *Virtual<const float*>(Department(self), dslot::kGetFunding);
        At<int64_t>(self, line::kCurrent) =
            static_cast<int64_t>(static_cast<double>(expense) * static_cast<double>(funding));
    }
    AnnounceNeededFunding(Department(self));
    return 1;
}

uint8_t __fastcall AddToFullExpenses(char* self, void*, int64_t expense)
{
    return Virtual<uint8_t>(self, lslot::kSetFull, At<int64_t>(self, line::kFull) + expense);
}

// 15 (0x006A9A40) (income): at least 0; announced.
uint8_t __fastcall SetIncome(char* self, void*, int64_t income)
{
    At<int64_t>(self, line::kIncome) = income < 0 ? 0 : income;
    AnnounceNeededFunding(Department(self));
    return 1;
}

uint8_t __fastcall AddToIncome(char* self, void*, int64_t income)
{
    At<int64_t>(self, line::kIncome) += income;
    AnnounceNeededFunding(Department(self));
    return 1;
}

uint32_t __fastcall GetDisplayFlags(char* self, void*)
{
    return At<uint32_t>(self, line::kFlags);
}

uint8_t __fastcall SetDisplayFlag(char* self, void*, uint32_t flag, uint8_t on)
{
    uint32_t& flags = At<uint32_t>(self, line::kFlags);
    flags = on ? flags | flag : flags & ~flag;
    return static_cast<uint8_t>(flags);
}

uint8_t __fastcall IsLocalFundingItem(char* self, void*)
{
    return At<uint8_t>(self, line::kLocal);
}

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(3, 0x006AAEB0, GetId),
    OPENSC4_SLOT(4, 0x00809250, GetType),
    OPENSC4_SLOT(5, 0x00990010, SetType),
    OPENSC4_SLOT(6, 0x006AC4F0, GetName),
    OPENSC4_SLOT(7, 0x006AC460, SetName),
    OPENSC4_SLOT(8, 0x006AAEC0, GetInt64<line::kSecondary>),
    OPENSC4_SLOT(9, 0x006AAED0, SetSecondaryInfoField),
    OPENSC4_SLOT(10, 0x006B9850, GetInt64<line::kFull>),
    OPENSC4_SLOT(11, 0x006AAEF0, GetInt64<line::kCurrent>),
    OPENSC4_SLOT(12, 0x006A9A00, SetFullExpenses),
    OPENSC4_SLOT(13, 0x006A9860, AddToFullExpenses),
    OPENSC4_SLOT(14, 0x006AAF00, GetInt64<line::kIncome>),
    OPENSC4_SLOT(15, 0x006A9A40, SetIncome),
    OPENSC4_SLOT(16, 0x006A9A70, AddToIncome),
    OPENSC4_SLOT(17, 0x0040D390, GetDisplayFlags),
    OPENSC4_SLOT(18, 0x006AAF10, SetDisplayFlag),
    OPENSC4_SLOT(19, 0x006AAF40, IsLocalFundingItem),
};

bool InstallInterface(registry::Mode)
{
    return SwapSlots("line item", addr::cSC4LineItem_vftable, kSlots, sizeof(kSlots) / sizeof(kSlots[0]));
}

const bool g_registeredInterface = registry::Register({
    "simulators.budget.lineitem",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4LineItem_vftable,
    "Our line items' slots 3-19: name, type, the expenses and income, display flags (docs/budget.md)",
    false,
    InstallInterface,
});

// --- The save record (cIGZSerializable at +0x4, vtable 0x00AB0F3C) -------------

using WriteBoolFn = void(__cdecl*)(void* stream, uint32_t value);
using ReadBoolFn = void(__cdecl*)(void* stream, void* value);
using WriteObjectFn = uint8_t(__cdecl*)(void* stream, const void* object);
using ReadObjectFn = uint8_t(__cdecl*)(void* stream, uint32_t clsid, void* object, uint32_t flags);

constexpr std::size_t kInt64Fields[] = {line::kSecondary, line::kFull, line::kCurrent, line::kIncome};

// 3 (0x006A9AA0): version 4.
uint8_t __fastcall Write(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("line item Write", logged);
    char* const self = serializable - line::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(4));
    Virtual<uint8_t>(stream, 0x3C, self + line::kName);
    for (const std::size_t offset : kInt64Fields)
        Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, offset));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, line::kId));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, line::kFlags));
    reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(self, line::kLocal));
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(At<uint32_t>(self, line::kType)));
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference)(stream, Department(self));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, line::kNameInstance));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, line::kNameGroup));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// 4 (0x006A9B60): version 4 only; the name is made again when first asked.
uint8_t __fastcall Read(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("line item Read", logged);
    char* const self = serializable - line::kSerializable;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 4)
        return 0;
    Virtual<uint8_t>(stream, 0x3C, self + line::kName);
    for (const std::size_t offset : kInt64Fields)
        Virtual<uint8_t>(stream, 0x28, self + offset);
    Virtual<uint8_t>(stream, 0x24, self + line::kId);
    Virtual<uint8_t>(stream, 0x24, self + line::kFlags);
    reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool)(stream, self + line::kLocal);
    uint16_t type = 0;
    Virtual<uint8_t>(stream, 0x1C, &type);
    At<uint32_t>(self, line::kType) = type;
    if (void* const department = Department(self)) {
        At<void*>(self, line::kDepartment) = nullptr;
        Virtual<uint32_t>(department, 0x8);
    }
    reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReference)(stream, 0xE990BFFC, self + line::kDepartment, 0);
    Virtual<uint8_t>(stream, 0x24, self + line::kNameInstance);
    Virtual<uint8_t>(stream, 0x24, self + line::kNameGroup);
    At<uint8_t>(self, line::kNamePending) = 1;
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

const Slot kSaveSlots[] = {
    OPENSC4_SLOT(3, 0x006A9AA0, Write),
    OPENSC4_SLOT(4, 0x006A9B60, Read),
};

#undef OPENSC4_SLOT

bool InstallSave(registry::Mode)
{
    return SwapSlots("line item save", addr::cSC4LineItem_vftable_4, kSaveSlots, sizeof(kSaveSlots) / sizeof(kSaveSlots[0]));
}

const bool g_registeredSave = registry::Register({
    "simulators.budget.lineitem_save",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4LineItem_vftable_4,
    "Our line items' save record: Write and Read (version 4) (docs/budget.md)",
    false,
    InstallSave,
});

} // namespace
} // namespace opensc4::budget_sim
