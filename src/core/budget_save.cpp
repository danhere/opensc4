// The budget simulator's save record: cIGZSerializable at +0x8, Write
// (0x00690530) and Read (0x00694B00), version 5 (docs/budget.md).
#include "core/budget_sim.h"
#include "core/game.h"
#include "core/game_access.h"
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
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr std::size_t kSerializable = 0x8;

void WriteInt64(void* stream, void* self, std::size_t offset)
{
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, offset));
}

void ReadInt64(void* stream, void* self, std::size_t offset)
{
    Virtual<uint8_t>(stream, 0x28, static_cast<char*>(self) + offset);
}

// The fields both directions share, in order, between the departments and
// the bonds.
constexpr std::size_t kTailFields[] = {0x750, 0x758, 0x760, 0x768, 0x770, 0x778};

uint8_t __fastcall Write(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    using WriteObjectFn = uint8_t(__cdecl*)(void* stream, const void* object);
    char* const self = serializable - kSerializable;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(5));
    for (const std::size_t offset : {field::kFunds, field::kFundsShown, field::kField38, field::kTaxIncome})
        WriteInt64(stream, self, offset);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kDepartments + 4));
    char* const departments = At<char*>(self, field::kDepartments);
    for (char* node = At<char*>(departments, 0x8); node != At<char*>(self, field::kDepartments); node = MapNext(node)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x10));
        reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference)(stream, At<void*>(node, 0x14));
    }
    for (uint32_t group = 0; group < kTaxGroups; ++group) {
        char* const g = self + group * kGroupSize;
        Virtual<uint8_t>(stream, 0x30, At<float>(g, 0x70));
        Virtual<uint8_t>(stream, 0x30, At<float>(g, 0x74));
        for (std::size_t i = 0; i < 12; ++i)
            WriteInt64(stream, g, 0x78 + 8 * i);
        WriteInt64(stream, g, 0xD8);
        WriteInt64(stream, g, 0xE0);
        WriteInt64(stream, g, 0xE8);
    }
    WriteInt64(stream, self, field::kYearTradeExpense);
    WriteInt64(stream, self, field::kYearTradeIncome);
    for (std::size_t i = 0; i < 12; ++i)
        WriteInt64(stream, self, field::kTradeExpenseByMonth + 8 * i);
    for (std::size_t i = 0; i < 12; ++i)
        WriteInt64(stream, self, field::kTradeIncomeByMonth + 8 * i);
    for (const std::size_t offset : kTailFields)
        WriteInt64(stream, self, offset);
    char* const head = At<char*>(self, field::kBonds);
    uint32_t bonds = 0;
    for (char* bond = At<char*>(head, 0x0); bond != head; bond = At<char*>(bond, 0x0))
        ++bonds;
    Virtual<uint8_t>(stream, 0x24, bonds);
    for (char* bond = At<char*>(head, 0x0); bond != At<char*>(self, field::kBonds); bond = At<char*>(bond, 0x0)) {
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(bond, 0x8));
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(bond, 0xC));
        for (std::size_t offset = 0x10; offset < 0x40; offset += 8)
            WriteInt64(stream, bond, offset);
    }
    for (const std::size_t offset : {field::kYearBondPayments, field::kTotalMonthlyIncome, field::kTotalYearlyIncome,
                                     field::kTotalMonthlyExpense, field::kTotalYearlyExpense})
        WriteInt64(stream, self, offset);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kGroups + 4));
    char* const groups = At<char*>(self, field::kGroups);
    for (char* node = At<char*>(groups, 0x8); node != groups; node = MapNext(node)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x10));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x18));
        Virtual<uint8_t>(stream, 0x3C, node + 0x1C);
        WriteInt64(stream, node, 0x30);
        WriteInt64(stream, node, 0x38);
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x44));
        char* const set = At<char*>(node, 0x40);
        for (char* member = At<char*>(set, 0x8); member != set; member = MapNext(member))
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(member, 0x10));
    }
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A group record as Read builds it on the stack (0x30 bytes).
struct GroupRecord {
    uint32_t id;
    GameString name;
    int64_t expense;
    int64_t income;
    char* set;
    uint32_t setSize;
};

uint8_t __fastcall Read(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    using ReadObjectFn = uint8_t(__cdecl*)(void* stream, uint32_t interfaceId, void* out, uint32_t flags);
    using AtFn = void**(__thiscall*)(void* map, const uint32_t* key);
    char* const self = serializable - kSerializable;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version < 4)
        return 0;
    At<uint8_t>(self, field::kLoaded) = 1;
    for (const std::size_t offset : {field::kFunds, field::kFundsShown, field::kField38, field::kTaxIncome})
        ReadInt64(stream, self, offset);
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0;
        Virtual<uint8_t>(stream, 0x24, &key);
        void* department = nullptr;
        reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReference)(stream, 0xE990BFFC, &department, 0);
        *reinterpret_cast<AtFn>(addr::SC4Uint32Map_At)(self + field::kDepartments, &key) = department;
    }
    for (uint32_t group = 0; group < kTaxGroups; ++group) {
        char* const g = self + group * kGroupSize;
        Virtual<uint8_t>(stream, 0x30, g + 0x70);
        Virtual<uint8_t>(stream, 0x30, g + 0x74);
        for (std::size_t i = 0; i < 12; ++i)
            ReadInt64(stream, g, 0x78 + 8 * i);
        ReadInt64(stream, g, 0xD8);
        ReadInt64(stream, g, 0xE0);
        ReadInt64(stream, g, 0xE8);
    }
    if (version < 5 && At<float>(self, field::kGroupRate + 8 * kGroupSize) == 0.0f)
        At<uint8_t>(self, field::kGroup8Fixed) = 1;
    ReadInt64(stream, self, field::kYearTradeExpense);
    ReadInt64(stream, self, field::kYearTradeIncome);
    for (std::size_t i = 0; i < 12; ++i)
        ReadInt64(stream, self, field::kTradeExpenseByMonth + 8 * i);
    for (std::size_t i = 0; i < 12; ++i)
        ReadInt64(stream, self, field::kTradeIncomeByMonth + 8 * i);
    for (const std::size_t offset : kTailFields)
        ReadInt64(stream, self, offset);
    uint32_t bonds = 0;
    Virtual<uint8_t>(stream, 0x24, &bonds);
    At<uint32_t>(self, field::kBondCount) = bonds;
    for (uint32_t i = 0; i < bonds; ++i) {
        uint32_t record[14] = {};
        Virtual<uint8_t>(stream, 0x20, &record[0]);
        Virtual<uint8_t>(stream, 0x20, &record[1]);
        for (int k = 0; k < 6; ++k)
            Virtual<uint8_t>(stream, 0x28, &record[2 + 2 * k]);
        char* const head = At<char*>(self, field::kBonds);
        auto* const node = static_cast<char*>(Allocate(0x40));
        if (node + 8)
            std::memcpy(node + 8, record, sizeof record);
        char* const last = At<char*>(head, 0x4);
        At<char*>(node, 0x0) = head;
        At<char*>(node, 0x4) = last;
        At<char*>(last, 0x0) = node;
        At<char*>(head, 0x4) = node;
    }
    for (const std::size_t offset : {field::kYearBondPayments, field::kTotalMonthlyIncome, field::kTotalYearlyIncome,
                                     field::kTotalMonthlyExpense, field::kTotalYearlyExpense})
        ReadInt64(stream, self, offset);
    uint32_t groups = 0;
    Virtual<uint8_t>(stream, 0x24, &groups);
    for (uint32_t i = 0; i < groups; ++i) {
        GroupRecord record{};
        record.name.begin = static_cast<char*>(Allocate(8));
        record.name.end = record.name.begin;
        record.name.capacity = record.name.begin + 8;
        record.name.begin[0] = 0;
        record.name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        record.name.zero = 0;
        record.setSize = 0;
        record.set = static_cast<char*>(Allocate(0x14));
        record.set[0] = 0;
        At<char*>(record.set, 0x4) = nullptr;
        At<char*>(record.set, 0x8) = record.set;
        At<char*>(record.set, 0xC) = record.set;
        uint32_t key = 0;
        Virtual<uint8_t>(stream, 0x24, &key);
        Virtual<uint8_t>(stream, 0x24, &record.id);
        Virtual<uint8_t>(stream, 0x3C, &record.name);
        struct {
            GameString string;
            uint32_t key[3];
        } text{};
        using TextFn = void*(__thiscall*)(void* string, uint32_t group, uint32_t instance, uint32_t flag);
        auto* const made = static_cast<char*>(
            reinterpret_cast<TextFn>(addr::SC4LocalizedString_SC4LocalizedString)(&text, 0x6A3FF01C, record.id, 1));
        char* const from = made ? made + 4 : nullptr;
        using AssignRangeFn = void*(__thiscall*)(void* buffer, char* first, char* last, void* allocator);
        if (from != reinterpret_cast<char*>(&record.name.begin)) {
            char dummy = 0;
            reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(&record.name.begin, At<char*>(from, 0),
                                                                          At<char*>(from, 4), &dummy);
        }
        text.string.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (text.string.begin)
            Free(text.string.begin);
        Virtual<uint8_t>(stream, 0x28, &record.expense);
        Virtual<uint8_t>(stream, 0x28, &record.income);
        uint32_t members = 0;
        Virtual<uint8_t>(stream, 0x24, &members);
        for (uint32_t m = 0; m < members; ++m) {
            uint32_t member = 0;
            Virtual<uint8_t>(stream, 0x24, &member);
            struct {
                char* node;
                uint8_t inserted;
            } result{};
            using InsertFn = void*(__thiscall*)(void* set, void* result, const uint32_t* key);
            reinterpret_cast<InsertFn>(addr::SC4Uint32Set_Insert2)(&record.set, &result, &member);
        }
        using GroupAtFn = char*(__thiscall*)(void* map, const uint32_t* key);
        char* const value = reinterpret_cast<GroupAtFn>(addr::GroupMap_Insert)(self + field::kGroups, &key);
        At<uint32_t>(value, 0x0) = record.id;
        if (reinterpret_cast<char*>(&record.name.begin) != value + 0x8) {
            char dummy = 0;
            reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(value + 0x8, record.name.begin, record.name.end,
                                                                          &dummy);
        }
        At<int64_t>(value, 0x18) = record.expense;
        At<int64_t>(value, 0x20) = record.income;
        using AssignSetFn = void*(__thiscall*)(void* to, const void* from);
        reinterpret_cast<AssignSetFn>(addr::SC4Uint32Set_Assign)(value + 0x28, &record.set);
        if (record.setSize) {
            using EraseFn = void(__thiscall*)(void* set, char* node);
            for (char* node = At<char*>(record.set, 0x4); node;) {
                reinterpret_cast<EraseFn>(addr::SC4Uint32Set_EraseNodes)(&record.set, At<char*>(node, 0xC));
                char* const left = At<char*>(node, 0x8);
                Free(node);
                node = left;
            }
            At<char*>(record.set, 0x8) = record.set;
            At<char*>(record.set, 0x4) = nullptr;
            At<char*>(record.set, 0xC) = record.set;
            record.setSize = 0;
        }
        if (record.set)
            Free(record.set);
        record.name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (record.name.begin)
            Free(record.name.begin);
    }
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

const Slot kSaveSlots[] = {{3, 0x00690530, reinterpret_cast<void*>(&Write)}};
const Slot kLoadSlots[] = {{4, 0x00694B00, reinterpret_cast<void*>(&Read)}};

bool InstallSave(registry::Mode)
{
    return SwapSlots("budget save", addr::cSC4BudgetSimulator_vftable_8, kSaveSlots, 1);
}

bool InstallLoad(registry::Mode)
{
    return SwapSlots("budget load", addr::cSC4BudgetSimulator_vftable_8, kLoadSlots, 1);
}

const bool g_registeredSave = registry::Register({
    "simulators.budget.save",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable_8,
    "Our budget simulator Write: the save record, version 5 (docs/budget.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.budget.load",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable_8,
    "Our budget simulator Read: the save record, versions 4 and 5 (docs/budget.md)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::budget_sim
