// Our cSC4BudgetSimulator object (docs/budget.md, "Our object"): our factory
// registered under the game's class ID, our constructor, destructor,
// QueryInterface, AddRef and Release, and our own copies of the three
// vtables. The object has the game's layout (0x854 bytes), so every method,
// the game's or ours, works on it; the vtables are copied from the game's
// when the first object is made, after the other replacements have patched
// them. Off, the game's own object is used (patched as configured).
#include "core/budget_sim.h"
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::budget_sim {
namespace {

constexpr uint32_t kClassId = 0xE990BE01;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidBudgetSimulator = 0x6772BD23;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x854;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The three vtables (the game's: 0x00AB0908 with the deleting destructor in
// slot 59 and the class ID in slot 60, 0x00AB08F8, 0x00AB08E0).
constexpr int kSlots = 61, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, field::kRefs);
}

// As the game's (0x0068FC80).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidBudgetSimulator:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

void Destroy(char* self);

// As the game's Release (0x006900C0): Shutdown, the destructor, delete.
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("budget object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    OperatorDelete(self);
    return 0;
}

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return Release(part - Offset, nullptr);
}

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

// An empty map header (node layout: color byte, parent, left, right).
char* NewHeader(std::size_t size)
{
    auto* const header = static_cast<char*>(Allocate(size));
    header[0] = 0;
    At<char*>(header, 0x4) = nullptr;
    At<char*>(header, 0x8) = header;
    At<char*>(header, 0xC) = header;
    return header;
}

// As the game's constructor (0x00694650): the fields it sets, the two map
// headers and the bond list head from the STL allocator, the group factors
// 1.0, and the land value curve from the points (0, 0) and (255, 10).
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, field::kInitialised) = 0;
    At<uint32_t>(self, field::kRefs) = 0;
    At<uint8_t>(self, field::kLoaded) = 0;
    std::memset(self + 0x18, 0, 0x54 - 0x18);
    At<char*>(self, field::kDepartments) = nullptr;
    At<char*>(self, field::kDepartments) = NewHeader(0x18);
    At<uint32_t>(self, field::kDepartments + 4) = 0;
    At<char*>(self, field::kGroups) = nullptr;
    At<char*>(self, field::kGroups) = NewHeader(0x50);
    At<uint32_t>(self, field::kGroups + 4) = 0;
    At<int64_t>(self, field::kTaxIncome) = 0;
    At<uint8_t>(self, field::kGroup8Fixed) = 0;
    At<int64_t>(self, field::kYearTradeExpense) = 0;
    At<int64_t>(self, field::kYearTradeIncome) = 0;
    std::memset(self + 0x750, 0, 0x780 - 0x750);
    At<char*>(self, field::kBonds) = nullptr;
    auto* const head = static_cast<char*>(Allocate(0x40));
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    At<char*>(self, field::kBonds) = head;
    std::memset(self + 0x784, 0, 0x7B4 - 0x784);
    std::memset(self + 0x7B8, 0, 0x7C8 - 0x7B8);
    std::memset(self + 0x7F8, 0, 0xC);
    reinterpret_cast<ThisFn>(addr::SC4Curve_Init)(self + field::kLandValueCurve);
    std::memset(self + 0x814, 0, 0x82C - 0x814);
    std::memset(self + 0x830, 0, 0x850 - 0x830);
    At<uint8_t>(self, field::kSmallWindow) = 0;
    for (std::size_t offset = 0x7C8; offset < 0x7F8; offset += 4)
        At<float>(self, offset) = 1.0f;
    struct {
        float* begin;
        float* end;
        float* capacity;
    } points{};
    float point[2] = {0.0f, 0.0f};
    char dummy = 0;
    using InsertFn = void(__thiscall*)(void* vector, float* at, const float* value, void* allocator, uint32_t count,
                                       uint32_t one);
    const auto insert = reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN);
    insert(&points, nullptr, point, &dummy, 1, 1);
    point[0] = 255.0f;
    point[1] = 10.0f;
    if (points.end != points.capacity) {
        if (points.end) {
            points.end[0] = point[0];
            points.end[1] = point[1];
        }
        points.end += 2;
    } else {
        insert(&points, points.end, point, &dummy, 1, 1);
    }
    using SetPointsFn = void(__thiscall*)(void* curve, const void* points);
    reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints)(self + field::kLandValueCurve, &points);
    if (points.begin)
        Free(points.begin);
}

// As the game's destructor (0x00694140).
void Destroy(char* self)
{
    SetVtables(self);
    for (const std::size_t offset : {std::size_t{0x820}, std::size_t{0x814}, std::size_t{0x804}, std::size_t{0x7F8},
                                     std::size_t{0x790}})
        if (void* const buffer = At<void*>(self, offset))
            Free(buffer);
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + field::kBonds);
    using EraseFn = void(__thiscall*)(void* map, char* node);
    for (const auto& [offset, erase] : {std::pair<std::size_t, std::uintptr_t>{field::kGroups, addr::GroupMap_EraseNodes},
                                        std::pair<std::size_t, std::uintptr_t>{field::kDepartments,
                                                                               addr::DepartmentMap_EraseNodes}}) {
        char* const map = self + offset;
        if (At<uint32_t>(map, 0x4)) {
            reinterpret_cast<EraseFn>(erase)(map, At<char*>(At<char*>(map, 0), 0x4));
            char* const header = At<char*>(map, 0);
            At<char*>(header, 0x8) = header;
            At<char*>(header, 0x4) = nullptr;
            At<char*>(header, 0xC) = header;
            At<uint32_t>(map, 0x4) = 0;
        }
        if (char* const header = At<char*>(map, 0))
            Free(header);
    }
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4BudgetSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4BudgetSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4BudgetSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[59] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
}

cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(OperatorNew(kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("budget object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.budget.object",
    "simulators/budget",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4BudgetSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/budget.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::budget_sim
