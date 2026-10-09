// The budget simulator's budget items: an occupant's properties that tie it
// to department line items (docs/budget.md). Property arrays 0xEA54D283
// (department), 0xEA54D284 (line), 0xEA54D285, 0xEA54D286 (cost, int64) and
// 0xEA54D287 (local funding, percent) go through the game's property
// helpers, so their allocations are the game's.
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

using FreeFn = void(__cdecl*)(void* p);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

template <typename T>
struct Vector {
    T* begin = nullptr;
    T* end = nullptr;
    T* capacity = nullptr;
    std::size_t Size() const { return static_cast<std::size_t>(end - begin); }
    void Release()
    {
        if (begin)
            Free(begin);
    }
};

using GetUintsFn = uint8_t(__cdecl*)(void* holder, uint32_t id, Vector<uint32_t>* values);
using GetInt64sFn = uint8_t(__cdecl*)(void* holder, uint32_t id, Vector<int64_t>* values);
using GetFloatsFn = uint8_t(__cdecl*)(void* holder, uint32_t id, Vector<float>* values);
using SetFn = uint8_t(__cdecl*)(void* holder, uint32_t id, const void* values);
const auto GetUints = reinterpret_cast<GetUintsFn>(addr::SC4_GetPropertyUint32Vector);
const auto GetInt64s = reinterpret_cast<GetInt64sFn>(addr::SC4_GetPropertySint64Vector);
const auto GetFloats = reinterpret_cast<GetFloatsFn>(addr::SC4_GetPropertyFloatVector);

constexpr uint32_t kDepartments = 0xEA54D283;
constexpr uint32_t kLines = 0xEA54D284;
constexpr uint32_t kThird = 0xEA54D285;
constexpr uint32_t kCosts = 0xEA54D286;
constexpr uint32_t kFunding = 0xEA54D287;

// A new property `id` (0x1C bytes) added to the holder (slot 15), then the
// values set through `set`.
void Replace(void* holder, uint32_t id, std::uintptr_t set, const void* values)
{
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using ConstructFn = void*(__thiscall*)(void* memory, uint32_t id);
    void* const memory = reinterpret_cast<AllocateFn>(addr::SC4Property_Allocate)(0x1C);
    void* const property = memory ? reinterpret_cast<ConstructFn>(addr::SC4Property_Construct)(memory, id) : nullptr;
    Virtual<uint8_t>(holder, 0x3C, property, 0u);
    reinterpret_cast<SetFn>(set)(holder, id, values);
}

// A budget item (0x20 bytes) as GetBudgetItemInfo makes it.
struct Item {
    uint32_t department;
    uint32_t line;
    uint32_t third;
    uint32_t untouched;
    int64_t cost;
    uint32_t index;
    float funding; // a fraction
};

// 50 (0x006937B0) (holder, vector of items): one item per department entry;
// missing entries are 0, funding 100%.
uint8_t __fastcall GetBudgetItemInfo(void*, void*, void* holder, Vector<Item>* items)
{
    static bool logged = false;
    FirstCall("GetBudgetItemInfo", logged);
    Vector<float> funding;
    Vector<int64_t> costs;
    Vector<uint32_t> thirds, lines, departments;
    auto release = [&] {
        funding.Release();
        costs.Release();
        thirds.Release();
        lines.Release();
        departments.Release();
    };
    if (!GetUints(holder, kDepartments, &departments)) {
        release();
        return 0;
    }
    GetUints(holder, kLines, &lines);
    GetUints(holder, kThird, &thirds);
    GetInt64s(holder, kCosts, &costs);
    GetFloats(holder, kFunding, &funding);
    const auto count = static_cast<uint32_t>(departments.Size());
    if (count == 0) {
        release();
        return 0;
    }
    using EraseFn = void(__thiscall*)(void* vector, Item* first, Item* last);
    using ResizeFn = void(__thiscall*)(void* vector, uint32_t count);
    reinterpret_cast<EraseFn>(addr::LoanVector_Erase)(items, items->begin, items->end);
    reinterpret_cast<ResizeFn>(addr::ItemVector_Resize)(items, count);
    for (uint32_t i = 0; i < count; ++i) {
        Item& item = items->begin[i];
        item.department = departments.begin[i];
        item.line = i < lines.Size() ? lines.begin[i] : 0;
        item.third = i < thirds.Size() ? thirds.begin[i] : 0;
        item.cost = i < costs.Size() ? costs.begin[i] : 0;
        item.index = i;
        const float percent = i < funding.Size() ? funding.begin[i] : 100.0f;
        item.funding = static_cast<float>(static_cast<double>(percent) * 0.01f);
    }
    release();
    return 1;
}

// 51 (0x00691880) (holder, line, item): the first item on that line.
uint8_t __fastcall GetBudgetItemForPurpose(void* self, void*, void* holder, uint32_t line, Item* out)
{
    Vector<Item> items;
    if (Virtual<uint8_t>(self, 0xC8, holder, &items)) {
        for (Item* item = items.begin; item != items.end; ++item) {
            if (item->third == line) {
                *out = *item;
                items.Release();
                return 1;
            }
        }
    }
    items.Release();
    return 0;
}

// 52 (0x00691050) (holder, index, line).
void __fastcall ChangeBudgetItemLine(void*, void*, void* holder, uint32_t index, uint32_t line)
{
    Vector<uint32_t> lines;
    GetUints(holder, kLines, &lines);
    if (index < lines.Size()) {
        lines.begin[index] = line;
        Replace(holder, kLines, addr::SC4_SetPropertyUint32Vector, &lines);
    }
    lines.Release();
}

// 53 (0x00691100) (holder, index, cost).
void __fastcall ChangeBudgetItemCost(void*, void*, void* holder, uint32_t index, int64_t cost)
{
    Vector<int64_t> costs;
    GetInt64s(holder, kCosts, &costs);
    if (index < costs.Size()) {
        costs.begin[index] = cost;
        Replace(holder, kCosts, addr::SC4_SetPropertySint64Vector, &costs);
    }
    costs.Release();
}

// 54 (0x00692D50) (holder, index, fraction): stored in percent; the array
// grows to reach the index.
void __fastcall ChangeBudgetItemLocalFunding(void*, void*, void* holder, uint32_t index, const float* fraction)
{
    Vector<float> funding;
    GetFloats(holder, kFunding, &funding);
    if (index >= funding.Size()) {
        float zero = 0.0f;
        using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const float* value);
        reinterpret_cast<ResizeFn>(addr::SC4FloatVector_Resize2)(&funding, index + 1, &zero);
    }
    funding.begin[index] = static_cast<float>(static_cast<double>(*fraction) * 100.0f);
    Replace(holder, kFunding, addr::SC4_SetPropertyFloatVector, &funding);
    funding.Release();
}

// 55, 56 (0x00691910, 0x006919B0) (holder, index): the item's cost added to
// (taken off) its department line (line slot 13).
template <bool Add>
void __fastcall ApplyBudgetItem(void* self, void*, void* holder, uint32_t index)
{
    Vector<Item> items;
    if (Virtual<uint8_t>(self, 0xC8, holder, &items) && index < items.Size()) {
        const Item& item = items.begin[index];
        if (void* const department = Virtual<void*>(self, slot::kGetDepartmentById, item.department))
            if (void* const line = Virtual<void*>(department, 0x50, item.line))
                Virtual<uint32_t>(line, 0x34, Add ? item.cost : -item.cost);
    }
    items.Release();
}

// 58 (0x006911D0) (from, to): each of the five arrays the source has, onto
// the destination.
void __fastcall CopyBudgetItemProperties(void*, void*, void* from, void* to)
{
    Vector<float> funding;
    Vector<int64_t> costs;
    Vector<uint32_t> thirds, lines, departments;
    if (GetUints(from, kDepartments, &departments))
        Replace(to, kDepartments, addr::SC4_SetPropertyUint32Vector, &departments);
    if (GetUints(from, kLines, &lines))
        Replace(to, kLines, addr::SC4_SetPropertyUint32Vector, &lines);
    if (GetUints(from, kThird, &thirds))
        Replace(to, kThird, addr::SC4_SetPropertyUint32Vector, &thirds);
    if (GetInt64s(from, kCosts, &costs))
        Replace(to, kCosts, addr::SC4_SetPropertySint64Vector, &costs);
    if (GetFloats(from, kFunding, &funding))
        Replace(to, kFunding, addr::SC4_SetPropertyFloatVector, &funding);
    funding.Release();
    costs.Release();
    thirds.Release();
    lines.Release();
    departments.Release();
}

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(50, 0x006937B0, GetBudgetItemInfo),
    OPENSC4_SLOT(51, 0x00691880, GetBudgetItemForPurpose),
    OPENSC4_SLOT(52, 0x00691050, ChangeBudgetItemLine),
    OPENSC4_SLOT(53, 0x00691100, ChangeBudgetItemCost),
    OPENSC4_SLOT(54, 0x00692D50, ChangeBudgetItemLocalFunding),
    OPENSC4_SLOT(55, 0x00691910, ApplyBudgetItem<true>),
    OPENSC4_SLOT(56, 0x006919B0, ApplyBudgetItem<false>),
    OPENSC4_SLOT(58, 0x006911D0, CopyBudgetItemProperties),
};

#undef OPENSC4_SLOT

bool InstallItems(registry::Mode)
{
    return SwapSlots("budget items", addr::cSC4BudgetSimulator_vftable, kSlots, sizeof(kSlots) / sizeof(kSlots[0]));
}

const bool g_registeredItems = registry::Register({
    "simulators.budget.items",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable,
    "Our budget item slots: an occupant's department, line, cost and funding properties (docs/budget.md)",
    false,
    InstallItems,
});

} // namespace
} // namespace opensc4::budget_sim
