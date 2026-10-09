// Our department budget and line item objects (docs/budget.md, "Departments
// and line items"): the game's two constructors are replaced by ours, so
// every object (made by the budget simulator, a department or the class
// factories 0x006AB000 and 0x006AB7A0 when a city loads) gets our vtables:
// our QueryInterface, AddRef, Release and destructor, the other slots copied
// from the game's (patched) vtables when the first object is made. The
// objects keep the game's layout (0x68 bytes each).
#include "core/address_check.h"
#include "core/budget_department.h"
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
#include <utility>

namespace opensc4::budget_sim {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidDepartmentBudget = 0x223E2B67;
constexpr uint32_t kClassDepartment = 0xE990BFFC;
constexpr uint32_t kClassLineItem = 0xAA313C9F;

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

// The empty string at `offset`: vtable, an 8-byte buffer, zero.
void ConstructString(char* self, std::size_t offset)
{
    auto* const buffer = static_cast<char*>(Allocate(8));
    At<char*>(self, offset + 0x4) = buffer;
    At<char*>(self, offset + 0x8) = buffer;
    At<char*>(self, offset + 0xC) = buffer + 8;
    buffer[0] = 0;
    At<uint32_t>(self, offset + 0x10) = 0;
    At<const void*>(self, offset) = reinterpret_cast<const void*>(addr::SC4String_Vtable);
}

void DestroyString(char* self, std::size_t offset)
{
    At<const void*>(self, offset) = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (void* const buffer = At<void*>(self, offset + 0x4))
        Free(buffer);
}

// --- Department budgets --------------------------------------------------------

constexpr int kDepartmentSlots = 35, kSerializableSlots = 6;
void* g_departmentVtable[kDepartmentSlots];
void* g_departmentVtable4[kSerializableSlots];
bool g_departmentBuilt = false;

uint32_t __fastcall DepartmentAddRef(char* self, void*)
{
    return ++At<uint32_t>(self, dept::kRefs);
}

// As the game's (0x006A96C0).
uint8_t __fastcall DepartmentQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    switch (iid) {
    case kIidUnknown:
    case kIidDepartmentBudget:
    case kClassDepartment: *object = self; break;
    case kIidSerializable: *object = self ? self + dept::kSerializable : nullptr; break;
    default: return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006A9730): the last reference destroys (slot 34).
uint32_t __fastcall DepartmentRelease(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, dept::kRefs);
    if (refs > 1)
        return --refs;
    Virtual<void*>(self, dslot::kDeletingDestructor, 1u);
    return 0;
}

void SetDepartmentVtables(char* self)
{
    At<void**>(self, 0x0) = g_departmentVtable;
    At<void**>(self, dept::kSerializable) = g_departmentVtable4;
}

// As the game's destructor (0x006AACB0): the containers' nodes freed (their
// objects are not released here), the name.
void DestroyDepartment(char* self)
{
    SetDepartmentVtables(self);
    using EraseFn = void(__thiscall*)(void* tree, char* node);
    for (const auto& [offset, eraser] : {std::pair<std::size_t, std::uintptr_t>{dept::kLocal, addr::LocalFundingSet_EraseNodes},
                                         std::pair<std::size_t, std::uintptr_t>{dept::kLines, addr::LineItemMap_EraseNodes}}) {
        char* const header = At<char*>(self, offset);
        if (At<uint32_t>(self, offset + 4)) {
            reinterpret_cast<EraseFn>(eraser)(self + offset, At<char*>(header, 0x4));
            At<char*>(header, 0x8) = header;
            At<char*>(header, 0x4) = nullptr;
            At<char*>(header, 0xC) = header;
            At<uint32_t>(self, offset + 4) = 0;
        }
        if (header)
            Free(header);
    }
    DestroyString(self, dept::kName);
}

void* __fastcall DepartmentDeletingDestructor(char* self, void*, uint32_t flags)
{
    DestroyDepartment(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

template <std::size_t Offset>
uint8_t __fastcall DepartmentQueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return DepartmentQueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall DepartmentAddRefAt(char* part, void*)
{
    return DepartmentAddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall DepartmentReleaseAt(char* part, void*)
{
    return DepartmentRelease(part - Offset, nullptr);
}

void BuildDepartmentVtables()
{
    std::memcpy(g_departmentVtable, reinterpret_cast<const void*>(addr::cSC4DepartmentBudget_vftable),
                sizeof(g_departmentVtable));
    std::memcpy(g_departmentVtable4, reinterpret_cast<const void*>(addr::cSC4DepartmentBudget_vftable_4),
                sizeof(g_departmentVtable4));
    g_departmentVtable[0] = reinterpret_cast<void*>(&DepartmentQueryInterface);
    g_departmentVtable[1] = reinterpret_cast<void*>(&DepartmentAddRef);
    g_departmentVtable[2] = reinterpret_cast<void*>(&DepartmentRelease);
    g_departmentVtable[34] = reinterpret_cast<void*>(&DepartmentDeletingDestructor);
    g_departmentVtable4[0] = reinterpret_cast<void*>(&DepartmentQueryInterfaceAt<dept::kSerializable>);
    g_departmentVtable4[1] = reinterpret_cast<void*>(&DepartmentAddRefAt<dept::kSerializable>);
    g_departmentVtable4[2] = reinterpret_cast<void*>(&DepartmentReleaseAt<dept::kSerializable>);
}

// As the game's constructor (0x006AAB90): empty name and containers, funding
// and maximum 1.0.
void* __fastcall ConstructDepartment(char* self, void*)
{
    if (!g_departmentBuilt) {
        g_departmentBuilt = true;
        BuildDepartmentVtables();
    }
    SetDepartmentVtables(self);
    At<uint8_t>(self, dept::kInitialised) = 0;
    At<uint32_t>(self, dept::kRefs) = 0;
    At<uint32_t>(self, dept::kId) = 0;
    ConstructString(self, dept::kName);
    At<uint32_t>(self, dept::kNameInstance) = 0;
    At<uint32_t>(self, dept::kNameGroup) = 0;
    At<uint32_t>(self, dept::kGroup) = 0;
    At<float>(self, dept::kFunding) = 1.0f;
    At<float>(self, dept::kMaxFunding) = 1.0f;
    At<int64_t>(self, dept::kSpending) = 0;
    At<uint8_t>(self, dept::kFixed) = 0;
    At<char*>(self, dept::kLines) = NewHeader(0x18);
    At<uint32_t>(self, dept::kLines + 4) = 0;
    At<char*>(self, dept::kLocal) = NewHeader(0x18);
    At<uint32_t>(self, dept::kLocal + 4) = 0;
    static int made = 0;
    if (made++ < 3)
        log::Write("department budget object: created %p", static_cast<void*>(self));
    return self;
}

// --- Line items ----------------------------------------------------------------

constexpr int kLineSlots = 21;
void* g_lineVtable[kLineSlots];
void* g_lineVtable4[kSerializableSlots];
bool g_lineBuilt = false;

uint32_t __fastcall LineAddRef(char* self, void*)
{
    return ++At<uint32_t>(self, line::kRefs);
}

// As the game's (0x006A97D0).
uint8_t __fastcall LineQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    switch (iid) {
    case kIidUnknown:
    case kClassLineItem: *object = self; break;
    case kIidSerializable: *object = self ? self + line::kSerializable : nullptr; break;
    default: return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006A9840): the last reference destroys (slot 20).
uint32_t __fastcall LineRelease(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, line::kRefs);
    if (refs > 1)
        return --refs;
    Virtual<void*>(self, lslot::kDeletingDestructor, 1u);
    return 0;
}

void SetLineVtables(char* self)
{
    At<void**>(self, 0x0) = g_lineVtable;
    At<void**>(self, line::kSerializable) = g_lineVtable4;
}

// As the game's (0x006AB7C0): the department released, the name freed.
void* __fastcall LineDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetLineVtables(self);
    if (void* const department = At<void*>(self, line::kDepartment))
        Virtual<uint32_t>(department, 0x8);
    DestroyString(self, line::kName);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

template <std::size_t Offset>
uint8_t __fastcall LineQueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return LineQueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall LineAddRefAt(char* part, void*)
{
    return LineAddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall LineReleaseAt(char* part, void*)
{
    return LineRelease(part - Offset, nullptr);
}

void BuildLineVtables()
{
    std::memcpy(g_lineVtable, reinterpret_cast<const void*>(addr::cSC4LineItem_vftable), sizeof(g_lineVtable));
    std::memcpy(g_lineVtable4, reinterpret_cast<const void*>(addr::cSC4LineItem_vftable_4), sizeof(g_lineVtable4));
    g_lineVtable[0] = reinterpret_cast<void*>(&LineQueryInterface);
    g_lineVtable[1] = reinterpret_cast<void*>(&LineAddRef);
    g_lineVtable[2] = reinterpret_cast<void*>(&LineRelease);
    g_lineVtable[20] = reinterpret_cast<void*>(&LineDeletingDestructor);
    g_lineVtable4[0] = reinterpret_cast<void*>(&LineQueryInterfaceAt<line::kSerializable>);
    g_lineVtable4[1] = reinterpret_cast<void*>(&LineAddRefAt<line::kSerializable>);
    g_lineVtable4[2] = reinterpret_cast<void*>(&LineReleaseAt<line::kSerializable>);
}

// As the game's constructor (0x006AAE20): an empty name, display flags 5.
void* __fastcall ConstructLine(char* self, void*)
{
    if (!g_lineBuilt) {
        g_lineBuilt = true;
        BuildLineVtables();
    }
    SetLineVtables(self);
    At<uint8_t>(self, line::kOwned) = 0;
    ConstructString(self, line::kName);
    At<uint32_t>(self, line::kNameInstance) = 0;
    At<uint32_t>(self, line::kNameGroup) = 0;
    At<uint8_t>(self, line::kNamePending) = 0;
    for (const std::size_t offset : {line::kSecondary, line::kFull, line::kCurrent, line::kIncome})
        At<int64_t>(self, offset) = 0;
    At<uint32_t>(self, line::kRefs) = 0;
    At<uint32_t>(self, line::kId) = 0;
    At<uint32_t>(self, line::kFlags) = 5;
    At<void*>(self, line::kDepartment) = nullptr;
    At<uint32_t>(self, line::kType) = 0;
    static int made = 0;
    if (made++ < 3)
        log::Write("line item object: created %p", static_cast<void*>(self));
    return self;
}

void* g_originalDepartment = nullptr;
void* g_originalLine = nullptr;

bool Install(registry::Mode)
{
    const addr::Entry* department = addr_check::Find(addr::cSC4DepartmentBudget_cSC4DepartmentBudget);
    const addr::Entry* line = addr_check::Find(addr::cSC4LineItem_cSC4LineItem);
    return game::IsBuild641() && department && line &&
           hook::Install(*department, reinterpret_cast<void*>(&ConstructDepartment), &g_originalDepartment) &&
           hook::Install(*line, reinterpret_cast<void*>(&ConstructLine), &g_originalLine);
}

const bool g_registered = registry::Register({
    "simulators.budget.department_object",
    "simulators/budget",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DepartmentBudget_cSC4DepartmentBudget,
    "Our department budget and line item objects: constructors, destructors, identity; methods from the (patched) "
    "vtables (docs/budget.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::budget_sim
