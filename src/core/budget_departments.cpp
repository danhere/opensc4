// The budget simulator's departments, groups, bonds and loans
// (docs/budget.md). Containers go through the game's own helpers, so their
// allocations are the game's.
#include "core/address_check.h"
#include "core/budget_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::budget_sim {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// find(key) in a uint32-keyed map (0x00497520): the node, or the header.
char* Find(void* map, uint32_t key)
{
    using FindFn = char**(__thiscall*)(void* map, char** result, const uint32_t* key);
    char* result = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(map, &result, &key);
    return result;
}

char* Map(void* self, std::size_t offset)
{
    return static_cast<char*>(self) + offset;
}

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A localised string (vtable 0x00A85478): the string, then the text's key.
struct LocalizedString {
    GameString string;
    uint32_t key[3];
};

// --- Groups --------------------------------------------------------------------
// Map at +0x60: id -> record at node +0x18: id, name (cIGZString at +0x4,
// buffer at +0x8), expense and income totals (+0x18, +0x20), the set of the
// group's department IDs (header +0x28, size +0x2C).

// 0x00690E80 (group): the group's totals again from its departments'
// expenses and income (slots 15, 16); a change is announced (0x0A3E7624
// with the group, the expense and the income, low words).
void __fastcall RecountGroup(void* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("RecountGroup", logged);
    char* const node = Find(Map(self, field::kGroups), id);
    if (node == At<char*>(self, field::kGroups))
        return;
    int64_t expense = 0, income = 0;
    char* const set = At<char*>(node, 0x40);
    for (char* member = At<char*>(set, 0x8); member != set; member = MapNext(member)) {
        void* const department = Virtual<void*>(self, slot::kGetDepartmentById, At<uint32_t>(member, 0x10));
        if (!department)
            continue;
        expense += Virtual<int64_t>(department, 0x3C);
        income += Virtual<int64_t>(department, 0x40);
    }
    if (At<int64_t>(node, 0x30) == expense && At<int64_t>(node, 0x38) == income)
        return;
    At<int64_t>(node, 0x30) = expense;
    At<int64_t>(node, 0x38) = income;
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, 0x0A3E7624u);
    Virtual<uint32_t>(message, 0x2C, id);
    Virtual<uint32_t>(message, 0x40, static_cast<uint32_t>(expense));
    Virtual<uint32_t>(message, 0x54, static_cast<uint32_t>(income));
    Virtual<uint32_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

void Recount(void* self, uint32_t group)
{
    using RecountFn = void(__thiscall*)(void* self, uint32_t group);
    reinterpret_cast<RecountFn>(addr::cSC4BudgetSimulator_RecountGroup)(self, group);
}

// 0x006950B0 (group): the group's record, made if missing with its name from
// the localised text 0x6A3FF01C / group.
char* __fastcall GroupRecord(void* self, void*, uint32_t id)
{
    char* const node = Find(Map(self, field::kGroups), id);
    if (node != At<char*>(self, field::kGroups))
        return node + 0x18;
    using InsertFn = char*(__thiscall*)(void* map, const uint32_t* key);
    char* const record = reinterpret_cast<InsertFn>(addr::GroupMap_Insert)(Map(self, field::kGroups), &id);
    At<uint32_t>(record, 0x0) = id;
    At<int64_t>(record, 0x18) = 0;
    At<int64_t>(record, 0x20) = 0;
    LocalizedString text{};
    using TextFn = void*(__thiscall*)(LocalizedString* string, uint32_t group, uint32_t instance, uint32_t flag);
    auto* const made = static_cast<char*>(
        reinterpret_cast<TextFn>(addr::SC4LocalizedString_SC4LocalizedString)(&text, 0x6A3FF01C, id, 1));
    char* const from = made ? made + 4 : nullptr;
    if (from != record + 0x8) {
        char dummy = 0;
        using AssignRangeFn = void*(__thiscall*)(void* buffer, char* first, char* last, void* allocator);
        reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(record + 0x8, At<char*>(from, 0x0),
                                                                      At<char*>(from, 0x4), &dummy);
    }
    text.string.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (text.string.begin)
        Free(text.string.begin);
    return record;
}

char* Group(void* self, uint32_t id)
{
    using GroupFn = char*(__thiscall*)(void* self, uint32_t id);
    return reinterpret_cast<GroupFn>(addr::cSC4BudgetSimulator_GroupRecord)(self, id);
}

// --- The interface (vtable 0x00AB0908) ----------------------------------------

// 25 (0x006951A0) (id, group): the department, made if missing: a
// cSC4DepartmentBudget (0x68 bytes), initialised, its ID and a default, put
// in its group, the group recounted.
void* __fastcall CreateDepartmentBudget(void* self, void*, uint32_t id, uint32_t group)
{
    static bool logged = false;
    FirstCall("CreateDepartmentBudget", logged);
    char* const node = Find(Map(self, field::kDepartments), id);
    if (node != At<char*>(self, field::kDepartments))
        return At<void*>(node, 0x14);
    using ConstructFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(0x68);
    void* const department = memory ? reinterpret_cast<ConstructFn>(addr::cSC4DepartmentBudget_cSC4DepartmentBudget)(memory)
                                    : nullptr;
    Virtual<uint32_t>(department, 0x4);
    reinterpret_cast<ThisFn>(addr::cSC4DepartmentBudget_Init)(department);
    using AtFn = void**(__thiscall*)(void* map, const uint32_t* key);
    void** const value = reinterpret_cast<AtFn>(addr::SC4Uint32Map_At)(Map(self, field::kDepartments), &id);
    *value = department;
    using SetIdFn = void(__thiscall*)(void* department, uint32_t id);
    reinterpret_cast<SetIdFn>(addr::cSC4DepartmentBudget_SetId)(department, id);
    Virtual<uint32_t>(department, 0x2C, reinterpret_cast<const void*>(addr::Budget_DefaultFunding), 0u);
    char* const record = Group(self, group);
    struct {
        char* node;
        uint8_t inserted;
    } result{};
    using InsertFn = void*(__thiscall*)(void* set, void* result, const uint32_t* key);
    reinterpret_cast<InsertFn>(addr::SC4Uint32Set_Insert2)(record + 0x28, &result, &id);
    Virtual<uint32_t>(department, 0x1C, group);
    Recount(self, group);
    return department;
}

// 26 (0x00692BA0): the department out of its group (the group recounted) and
// out of the map; shut down and released.
uint8_t __fastcall RemoveDepartmentBudget(void* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("RemoveDepartmentBudget", logged);
    char* const node = Find(Map(self, field::kDepartments), id);
    if (node == At<char*>(self, field::kDepartments))
        return 0;
    void* const department = At<void*>(node, 0x14);
    const uint32_t group = Virtual<uint32_t>(department, 0x18);
    char* const groupNode = Find(Map(self, field::kGroups), group);
    if (groupNode != At<char*>(self, field::kGroups)) {
        using EraseFn = void(__thiscall*)(void* set, const uint32_t* key);
        reinterpret_cast<EraseFn>(addr::SC4Uint32Set_EraseKey)(groupNode + 0x40, &id);
        Recount(self, At<uint32_t>(groupNode, 0x18));
    }
    char* const header = At<char*>(self, field::kDepartments);
    using RebalanceFn = void*(__cdecl*)(char* node, void* root, void* leftmost, void* rightmost);
    if (void* const erased = reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(node, header + 0x4, header + 0x8,
                                                                                         header + 0xC))
        Free(erased);
    --At<uint32_t>(self, field::kDepartments + 4);
    reinterpret_cast<ThisFn>(addr::cSC4DepartmentBudget_Shutdown)(department);
    Virtual<uint32_t>(department, 0x8);
    return 1;
}

// 27 (0x00691B50) (name): the first department (key order) whose
// lower-cased name starts with the lower-cased name.
void* __fastcall GetDepartmentBudgetByName(void* self, void*, void* name)
{
    using FromRangeFn = void(__thiscall*)(char** string, const char* first, const char* last, void* allocator);
    using LowerFn = void(__thiscall*)(GameString* string);
    using SearchFn = char*(__cdecl*)(char* first1, char* last1, char* first2, char* last2, void* predicate);
    const auto lower = reinterpret_cast<LowerFn>(addr::cRZBaseString_MakeLower);
    const auto vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    const uint32_t length = Virtual<uint32_t>(name, 0x1C);
    const char* const text = Virtual<const char*>(name, 0x18);
    GameString wanted{};
    char allocator = 0;
    reinterpret_cast<FromRangeFn>(addr::SC4String_FromRange)(&wanted.begin, text, text + length, &allocator);
    wanted.vtable = vtable;
    wanted.zero = 0;
    lower(&wanted);
    void* match = nullptr;
    char* const header = At<char*>(self, field::kDepartments);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        void* const department = At<void*>(node, 0x14);
        GameString own{};
        own.begin = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(8));
        own.end = own.begin;
        own.capacity = own.begin + 8;
        own.begin[0] = 0;
        own.vtable = vtable;
        own.zero = 0;
        Virtual<uint32_t>(department, 0x10, &own);
        lower(&own);
        bool found = false;
        const auto wantedLength = static_cast<std::size_t>(wanted.end - wanted.begin);
        if (wantedLength <= static_cast<std::size_t>(own.end - own.begin)) {
            char predicate = 0;
            char* const at = reinterpret_cast<SearchFn>(addr::SC4_SearchChars)(own.begin, own.end, wanted.begin,
                                                                              wanted.begin + wantedLength, &predicate);
            found = at != own.end && at == own.begin;
        }
        own.vtable = vtable;
        if (own.begin)
            Free(own.begin);
        if (found) {
            match = department;
            break;
        }
    }
    wanted.vtable = vtable;
    if (wanted.begin)
        Free(wanted.begin);
    return match;
}

// 28 (0x00691000).
void* __fastcall GetDepartmentBudgetById(void* self, void*, uint32_t id)
{
    char* const node = Find(Map(self, field::kDepartments), id);
    return node != At<char*>(self, field::kDepartments) ? At<void*>(node, 0x14) : nullptr;
}

// 29 (0x00694990) (vector of 0x28-byte group records): every group's record.
void __fastcall GetAllGroups(void* self, void*, char** out)
{
    using MoveFn = char*(__cdecl*)(char* first, char* last, char* to, void* allocator, uint32_t unused);
    using DestroyFn = void(__cdecl*)(char* first, char* last, void* allocator);
    using ReserveFn = void(__thiscall*)(void* vector, uint32_t count);
    using CopyFn = void(__thiscall*)(char* to, const char* from);
    using InsertFn = void(__thiscall*)(void* vector, char* at, const char* value, void* allocator, uint32_t count,
                                       uint32_t one);
    char dummy = 0;
    char* const end = reinterpret_cast<MoveFn>(addr::GroupVector_Move)(out[1], out[1], out[0], &dummy, 0);
    reinterpret_cast<DestroyFn>(addr::GroupVector_Destroy)(end, out[1], &dummy);
    out[1] = end;
    reinterpret_cast<ReserveFn>(addr::GroupVector_Reserve)(out, At<uint32_t>(self, field::kGroups + 4));
    char* const header = At<char*>(self, field::kGroups);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        if (out[1] != out[2]) {
            if (out[1])
                reinterpret_cast<CopyFn>(addr::GroupRecord_Copy)(out[1], node + 0x18);
            out[1] += 0x28;
        } else {
            reinterpret_cast<InsertFn>(addr::GroupVector_Insert)(out, out[1], node + 0x18, &dummy, 1, 1);
        }
    }
}

// 30 (0x00695160) (group, name).
void __fastcall SetGroupName(void* self, void*, uint32_t group, void* name)
{
    char* const record = Group(self, group);
    char* const from = name ? static_cast<char*>(name) + 4 : nullptr;
    if (from == record + 0x8)
        return;
    char dummy = 0;
    using AssignRangeFn = void*(__thiscall*)(void* buffer, char* first, char* last, void* allocator);
    reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(record + 0x8, At<char*>(from, 0x0), At<char*>(from, 0x4),
                                                                  &dummy);
}

// 31 (0x00691D00) (group, vector of departments): the group's departments,
// each with a reference added.
void __fastcall GetDepartmentBudgetsInGroup(void* self, void*, uint32_t group, void** out)
{
    out[1] = out[0];
    char* const node = Find(Map(self, field::kGroups), group);
    if (node == At<char*>(self, field::kGroups))
        return;
    using ReserveFn = void(__thiscall*)(void* vector, uint32_t count);
    reinterpret_cast<ReserveFn>(addr::PtrVector_Reserve)(out, At<uint32_t>(node, 0x44));
    char* const set = At<char*>(node, 0x40);
    for (char* member = At<char*>(set, 0x8); member != set; member = MapNext(member)) {
        // lower_bound(id) in the department map, inline in the game.
        const uint32_t id = At<uint32_t>(member, 0x10);
        char* const header = At<char*>(self, field::kDepartments);
        char* found = header;
        for (char* at = At<char*>(header, 0x4); at;) {
            if (At<uint32_t>(at, 0x10) < id) {
                at = At<char*>(at, 0xC);
            } else {
                found = at;
                at = At<char*>(at, 0x8);
            }
        }
        if (found == header || id < At<uint32_t>(found, 0x10))
            found = header;
        if (found == At<char*>(self, field::kDepartments))
            continue;
        void* department = At<void*>(found, 0x14);
        Virtual<uint32_t>(department, 0x4);
        if (out[1] != out[2]) {
            if (out[1])
                *static_cast<void**>(out[1]) = department;
            out[1] = static_cast<void**>(out[1]) + 1;
        } else {
            char dummy = 0;
            using InsertFn = void(__thiscall*)(void* vector, void* at, void* const* value, void* allocator, uint32_t count,
                                               uint32_t one);
            reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(out, out[1], &department, &dummy, 1, 1);
        }
    }
}

// 32, 33 (0x00691030) (department): its group recounted.
void __fastcall RecountDepartmentGroup(void* self, void*, void* department)
{
    if (department)
        Recount(self, Virtual<uint32_t>(department, 0x18));
}

// 40 (0x006916F0) (amount): a bond in whole increments (at least one) if
// the bond count, the bond limit and the borrowing limit allow: repaid over
// the loan's months, deposited, announced (0xC772FADA).
uint8_t __fastcall IssueBond(void* self, void*, uint32_t amount)
{
    static bool logged = false;
    FirstCall("IssueBond", logged);
    if (amount == 0 || At<int32_t>(self, field::kBondCount) >= At<int32_t>(self, field::kBondsAllowed))
        return 0;
    const int64_t increment = At<int64_t>(self, field::kBondIncrement);
    const auto units = static_cast<uint32_t>(static_cast<int64_t>(amount) / increment);
    uint32_t principal = units * static_cast<uint32_t>(increment);
    if (principal == 0)
        principal = static_cast<uint32_t>(increment);
    if (static_cast<int64_t>(principal) > At<int64_t>(self, field::kBondLimit))
        return 0;
    const int64_t limit = Virtual<int64_t>(self, 0xA8);
    if (At<int64_t>(self, field::kField38) + principal > limit)
        return 0;
    struct Bond {
        int32_t month;
        int32_t year;
        int64_t principal;
        int64_t cost;
        int64_t owed;
        int64_t paid;
        int64_t payment;
        int64_t balance;
    } bond{};
    const int32_t cost = static_cast<int32_t>(Virtual<int64_t>(self, 0xC4, static_cast<int64_t>(principal)));
    const int64_t payment = Virtual<int64_t>(self, 0xC0, static_cast<int64_t>(principal));
    int32_t year = 0, month = 0;
    Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x18, &year, &month, 0u, 0u, 0u);
    const uint32_t months = At<uint32_t>(self, field::kLoanMonths);
    bond.year = year + static_cast<int32_t>(months / 12);
    bond.month = month + static_cast<int32_t>(months % 12);
    if (bond.month > 12) {
        bond.month -= 12;
        ++bond.year;
    }
    bond.principal = principal;
    bond.cost = cost;
    bond.owed = cost;
    bond.paid = 0;
    bond.payment = payment;
    bond.balance = principal;
    ++At<int32_t>(self, field::kBondCount);
    using PushFn = void(__thiscall*)(void* list, const Bond* bond);
    reinterpret_cast<PushFn>(addr::BondList_PushBack)(static_cast<char*>(self) + field::kBonds, &bond);
    using NotifyFn = void(__thiscall*)(void* self, uint32_t type);
    reinterpret_cast<NotifyFn>(addr::cSC4BudgetSimulator_NotifyType)(self, 0xC772FADA);
    At<int64_t>(self, field::kField38) += principal;
    Virtual<uint32_t>(self, 0x1C, static_cast<int64_t>(principal));
    return 1;
}

// 46 (0x00692C70) (vector of 0x20-byte loans): per bond the months and years
// left, the principal, the balance and the payment.
void __fastcall GetAllLoans(void* self, void*, char** out)
{
    using EraseFn = void(__thiscall*)(void* vector, char* first, char* last);
    reinterpret_cast<EraseFn>(addr::LoanVector_Erase)(out, out[0], out[1]);
    char* const head = At<char*>(self, field::kBonds);
    for (char* bond = At<char*>(head, 0x0); bond != head; bond = At<char*>(bond, 0x0)) {
        struct {
            int32_t months;
            int32_t years;
            int64_t principal;
            int64_t balance;
            int64_t payment;
        } loan{};
        loan.principal = At<int64_t>(bond, 0x10);
        loan.payment = At<int64_t>(bond, 0x30);
        int32_t year = 0, month = 0;
        Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x18, &year, &month, 0u, 0u, 0u);
        int32_t years = At<int32_t>(bond, 0xC) - year;
        int32_t months = At<int32_t>(bond, 0x8) - month;
        if (months < 0) {
            --years;
            months += 12;
        }
        loan.years = years;
        loan.balance = At<int64_t>(bond, 0x38);
        loan.months = months;
        if (out[1] != out[2]) {
            if (out[1])
                std::memcpy(out[1], &loan, sizeof loan);
            out[1] += 0x20;
        } else {
            char dummy = 0;
            using InsertFn = void(__thiscall*)(void* vector, char* at, const void* value, void* allocator, uint32_t count,
                                               uint32_t one);
            reinterpret_cast<InsertFn>(addr::LoanVector_Insert)(out, out[1], &loan, &dummy, 1, 1);
        }
    }
}

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(25, 0x006951A0, CreateDepartmentBudget),
    OPENSC4_SLOT(26, 0x00692BA0, RemoveDepartmentBudget),
    OPENSC4_SLOT(27, 0x00691B50, GetDepartmentBudgetByName),
    OPENSC4_SLOT(28, 0x00691000, GetDepartmentBudgetById),
    OPENSC4_SLOT(29, 0x00694990, GetAllGroups),
    OPENSC4_SLOT(30, 0x00695160, SetGroupName),
    OPENSC4_SLOT(31, 0x00691D00, GetDepartmentBudgetsInGroup),
    OPENSC4_SLOT(32, 0x00691030, RecountDepartmentGroup),
    OPENSC4_SLOT(33, 0x00691030, RecountDepartmentGroup),
    OPENSC4_SLOT(40, 0x006916F0, IssueBond),
    OPENSC4_SLOT(46, 0x00692C70, GetAllLoans),
};

#undef OPENSC4_SLOT

using RecountFn = void(__fastcall*)(void*, void*, uint32_t);
using GroupFn = char*(__fastcall*)(void*, void*, uint32_t);
RecountFn g_originalRecount = nullptr;
GroupFn g_originalGroup = nullptr;

bool InstallDepartments(registry::Mode)
{
    const addr::Entry* recount = addr_check::Find(addr::cSC4BudgetSimulator_RecountGroup);
    const addr::Entry* group = addr_check::Find(addr::cSC4BudgetSimulator_GroupRecord);
    return game::IsBuild641() && recount && group && hook::Install(*recount, &RecountGroup, &g_originalRecount) &&
           hook::Install(*group, &GroupRecord, &g_originalGroup) &&
           SwapSlots("budget departments", addr::cSC4BudgetSimulator_vftable, kSlots, sizeof(kSlots) / sizeof(kSlots[0]));
}

const bool g_registeredDepartments = registry::Register({
    "simulators.budget.departments",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable,
    "Our budget departments and groups (create, remove, find, recount), bonds and loans (docs/budget.md)",
    false,
    InstallDepartments,
});

} // namespace
} // namespace opensc4::budget_sim
