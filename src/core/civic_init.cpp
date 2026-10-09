// Our cSC4CivicBuildingSimulator (build 641), second part: Init, Shutdown,
// the script command and the save (Write, Read), each a replacement on the
// original object (docs/civic.md).
#include "core/address_check.h"
#include "core/civic_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::civic_sim {
namespace {

using FreeFn = void(__cdecl*)(void* p);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

void* Service(std::uintptr_t global) { return Global<void*>(global); }

constexpr uint32_t kMessages[] = {0xE8BDA2E7, 0x99EF1142, 0x99EF1143};
constexpr std::uintptr_t kFundingCurve = 0x00B0D1EC; // passed to the departments' slot 13

// The script service (0xEBE0E860, interface 0xE898ED03) into `holder`.
void GetScriptService(void** holder)
{
    using GetFrameworkFn = void*(__cdecl*)();
    *holder = nullptr;
    if (void* const framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
        Virtual<uint8_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, holder);
}

void ReleaseHolder(void** holder)
{
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4_ReleaseRefHolder)(holder);
}

// get_landmark_count (0x0069D740): the simulator registered with the
// command (script slots 8 and 18), its slot 8, pushed as a number.
uint8_t __cdecl ScriptLandmarkCount(void* state, uint32_t, uint32_t, void* script)
{
    void* const key = Virtual<void*>(script, 0x20, state);
    void* const object = Virtual<void*>(script, 0x48, key);
    const uint32_t count = object ? Virtual<uint32_t>(object, 0x20) : 0;
    alignas(4) char value[0x14];
    void* const number = CallThis<void*>(addr::SC4ScriptNumber_FromUint, value, count);
    Virtual<uint32_t>(script, 0x1C, 1u, number);
    CallThis<void>(addr::SC4ScriptNumber_Destroy, value);
    return 1;
}

// A budget department (slot 25: id, kind), set up as the game's (slots 5
// and 13).
void* MakeDepartment(void* budget, uint32_t id, uint32_t kind, uint32_t name)
{
    void* const department = Virtual<void*>(budget, 0x64, id, kind);
    if (department) {
        Virtual<uint32_t>(department, 0x14, 0x6A3FF01Cu, name);
        Virtual<uint32_t>(department, 0x34, kFundingCurve);
    }
    return department;
}

// --- Init and Shutdown (slots 3, 4) -------------------------------------------------

uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    using RegisterTableFn = uint32_t(__cdecl*)(void* self, const char* name, uint32_t, uint32_t, uint32_t, void* service);
    using RegisterFunctionFn = uint32_t(__cdecl*)(std::uintptr_t callback, const char* name, std::uintptr_t callback2,
                                                  void* self, uint32_t, void* self2, void* service);
    if (At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 1;
    void* const budget = Service(addr::SC4_BudgetSimulator);
    if (!At<void*>(self, field::kDepartment)) {
        void* const made = Virtual<void*>(budget, 0x64, 0x8A416368u, 0x6A357B96u);
        void* const old = At<void*>(self, field::kDepartment);
        if (made != old) {
            if (made)
                Virtual<uint32_t>(made, 0x4);
            At<void*>(self, field::kDepartment) = made;
            if (old)
                Virtual<uint32_t>(old, 0x8);
        }
        Virtual<uint32_t>(At<void*>(self, field::kDepartment), 0x14, 0x6A3FF01Cu, 0xCA491DD2u);
        Virtual<uint32_t>(At<void*>(self, field::kDepartment), 0x34, kFundingCurve);
    }
    if (!Virtual<void*>(budget, 0x70, 0xEA59717Au))
        MakeDepartment(budget, 0xEA59717Au, 0xEA597195u, 0x4A59726Du);
    if (!Virtual<void*>(budget, 0x70, 0x2A5A723Fu))
        MakeDepartment(budget, 0x2A5A723Fu, 0x0A5A72D1u, 0x8A5A7330u);
    void* const target = self + 0x4;
    void* const messages = Service(addr::SC4_MessageServer);
    for (const uint32_t message : kMessages)
        Virtual<uint8_t>(messages, 0x14, target, message);
    struct {
        const void* vtable;
        char* begin;
        char* end;
        char* capacity;
        uint32_t zero;
    } name;
    reinterpret_cast<StringFn>(addr::SC4String_FromText)(&name, reinterpret_cast<const char*>(0x00A89938));
    Virtual<uint32_t>(Service(addr::SC4_SimulatorService), 0x4C, target, 6u, &name, 2u);
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        Free(name.begin);
    void* script;
    GetScriptService(&script);
    reinterpret_cast<RegisterTableFn>(addr::SC4Script_RegisterTable)(self, reinterpret_cast<const char*>(0x00AB0C4C), 0, 0,
                                                                      0, script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    const auto command = reinterpret_cast<std::uintptr_t>(&ScriptLandmarkCount);
    reinterpret_cast<RegisterFunctionFn>(addr::SC4Script_RegisterFunction)(
        command, reinterpret_cast<const char*>(0x00AB0C38), command, self, 0, self, script);
    ReleaseHolder(&script);
    return 1;
}

// The values of a list (SC4List) released, then the list cleared.
void ReleaseList(char* self, std::size_t offset)
{
    void** const head = At<void**>(self, offset);
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
        Virtual<uint32_t>(node[2], 0x8);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Clear2)(self + offset);
}

uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    using UnregisterFn = uint8_t(__cdecl*)(void* self, void* service);
    if (!At<uint8_t>(self, field::kInitialised))
        return 1;
    void* script;
    GetScriptService(&script);
    reinterpret_cast<UnregisterFn>(addr::SC4Script_UnregisterTable)(self, script);
    ReleaseHolder(&script);
    ReleaseList(self, field::kZooBuildings);
    ReleaseList(self, field::kLandmarks);
    void* const target = self + 0x4;
    if (void* const service = Virtual<void*>(Service(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(service, 0x50, target);
    if (void* const department = At<void*>(self, field::kDepartment)) {
        At<void*>(self, field::kDepartment) = nullptr;
        Virtual<uint32_t>(department, 0x8);
    }
    if (void* const messages = Service(addr::SC4_MessageServer))
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(messages, 0x18, target, message);
    At<uint8_t>(self, field::kInitialised) = 0;
    return 1;
}

using LifecycleFn = uint8_t(__fastcall*)(char*, void*);
LifecycleFn g_originalInit = nullptr;
LifecycleFn g_originalShutdown = nullptr;

bool InstallInit(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* init = addr_check::Find(addr::cSC4CivicBuildingSimulator_Init);
    const addr::Entry* shutdown = addr_check::Find(addr::cSC4CivicBuildingSimulator_Shutdown);
    return init && shutdown && hook::Install(*init, &Init, &g_originalInit) &&
           hook::Install(*shutdown, &Shutdown, &g_originalShutdown);
}

const bool g_registeredInit = registry::Register({
    "simulators.civic.init",
    "simulators/civic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CivicBuildingSimulator_Init,
    "Our civic Init and Shutdown: departments, messages, registrations, the script command (docs/civic.md)",
    false,
    InstallInit,
});

// --- Saving: cIGZSerializable (vtable 0x00AB0C54) at +0x8 -----------------------------
//
// Version 6: the excluded families (count, then each key), the conditional
// building statuses (count, then key, flag, count and name each), the
// department (a reference), the ape escape candidates and the landmarks
// (reference lists). The hash tables are written in bucket order.

using WriteObjectFn = uint8_t(__cdecl*)(void* stream, void* object);

template <typename Visit>
void ForEachNode(char* table, Visit visit)
{
    char** const buckets = At<char**>(table, table::kBuckets);
    const auto count = static_cast<uint32_t>(At<char**>(table, table::kBucketsEnd) - buckets);
    for (uint32_t i = 0; i < count; ++i)
        for (char* node = buckets[i]; node; node = At<char*>(node, 0))
            visit(node);
}

uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x8;
    Virtual<uint32_t>(stream, 0x1C, uint32_t{6});
    Virtual<uint32_t>(stream, 0x24, At<uint32_t>(self, field::kExcluded + table::kCount));
    ForEachNode(self + field::kExcluded, [stream](char* node) { Virtual<uint32_t>(stream, 0x24, At<uint32_t>(node, 4)); });
    Virtual<uint32_t>(stream, 0x24, At<uint32_t>(self, field::kStatuses + table::kCount));
    ForEachNode(self + field::kStatuses, [stream](char* node) {
        Virtual<uint32_t>(stream, 0x24, At<uint32_t>(node, 0x4));
        Virtual<uint32_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(node, 0x8)));
        Virtual<uint32_t>(stream, 0x1C, static_cast<uint32_t>(At<uint16_t>(node, 0xA)));
        Virtual<uint32_t>(stream, 0x3C, node + 0xC);
    });
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference)(stream, At<void*>(self, field::kDepartment));
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReferenceList)(stream, self + field::kZooBuildings);
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReferenceList)(stream, self + field::kLandmarks);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t flags);
    using ReadListFn = uint8_t(__cdecl*)(void* stream, void* list);
    using ResizeFn = void(__thiscall*)(void* table, uint32_t count);
    using InsertFn = void(__thiscall*)(void* table, void* result, const uint32_t* key);
    using AllocateFn = char*(__cdecl*)(std::size_t size);
    using AssignFn = void(__thiscall*)(void* buffer, const char* begin, const char* end, void* tag);
    char* const self = part - 0x8;
    uint16_t version = 0;
    Virtual<uint32_t>(stream, 0x1C, &version);
    if (version != 6)
        return 0;
    uint32_t count = 0;
    Virtual<uint32_t>(stream, 0x24, &count);
    char* const excluded = self + field::kExcluded;
    while (count) {
        --count;
        uint32_t key = 0;
        Virtual<uint32_t>(stream, 0x24, &key);
        reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(excluded, At<uint32_t>(excluded, table::kCount) + 1);
        char result[12];
        reinterpret_cast<InsertFn>(addr::SC4Uint32HashSet_Insert)(excluded, result, &key);
    }
    Virtual<uint32_t>(stream, 0x24, &count);
    while (count) {
        --count;
        struct {
            const void* vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t zero;
        } name;
        char* const buffer = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(8);
        name.begin = buffer;
        name.end = buffer;
        name.capacity = buffer + 8;
        buffer[0] = 0;
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        name.zero = 0;
        uint32_t key = 0;
        uint8_t flag = 0;
        uint16_t buildings = 0;
        Virtual<uint32_t>(stream, 0x24, &key);
        Virtual<uint32_t>(stream, 0x14, &flag);
        Virtual<uint32_t>(stream, 0x1C, &buildings);
        Virtual<uint32_t>(stream, 0x3C, &name);
        char* const value = CallThis<char*>(addr::cSC4CivicBuildingSimulator_StatusAt, self + field::kStatuses, &key);
        At<uint8_t>(value, 0) = flag;
        At<uint16_t>(value, 2) = buildings;
        char tag;
        if (reinterpret_cast<char*>(&name.begin) != value + 8)
            reinterpret_cast<AssignFn>(addr::SC4String_AssignRange)(value + 8, name.begin, name.end, &tag);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Free(name.begin);
    }
    if (void* const department = At<void*>(self, field::kDepartment)) {
        At<void*>(self, field::kDepartment) = nullptr;
        Virtual<uint32_t>(department, 0x8);
    }
    reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference)(stream, 0x223E2B67u,
                                                                     &At<void*>(self, field::kDepartment), 0);
    reinterpret_cast<ReadListFn>(addr::SC4Stream_ReadReferenceList)(stream, self + field::kZooBuildings);
    reinterpret_cast<ReadListFn>(addr::SC4Stream_ReadReferenceList)(stream, self + field::kLandmarks);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

using SerializeFn = uint8_t(__fastcall*)(char*, void*, void*);
SerializeFn g_originalWrite = nullptr;
SerializeFn g_originalRead = nullptr;

bool InstallSave(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* write = addr_check::Find(addr::cSC4CivicBuildingSimulator_Write);
    const addr::Entry* read = addr_check::Find(addr::cSC4CivicBuildingSimulator_Read);
    return write && read && hook::Install(*write, &Write, &g_originalWrite) &&
           hook::Install(*read, &Read, &g_originalRead);
}

const bool g_registeredSave = registry::Register({
    "simulators.civic.save",
    "simulators/civic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CivicBuildingSimulator_Write,
    "Our civic Write and Read, version 6 (docs/civic.md)",
    false,
    InstallSave,
});

} // namespace
} // namespace opensc4::civic_sim
