// Our cSC4Simulator object (docs/simulator.md, "Our object"): our factory
// registered under the game's class ID, our constructor, destructor and
// identity functions, and our own copies of the five vtables. The object
// has the game's layout (0x1E0 bytes; the system service part at +0x8 is
// the game's cRZBaseSystemService, made and destroyed by the game's
// functions), so every method, the game's or ours, works on it. The vtables
// are copied from the game's when the first object is made, after the other
// replacements have patched them. Off, the game's own object is used.
#include "core/core_sim.h"
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::core_sim {
namespace {

constexpr uint32_t kClassId = 0x2990C1E5;
constexpr uint32_t kIidSimulator = 0x8695664E;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x1E0;
constexpr uint32_t kServiceId = 0x46956659;
constexpr int32_t kServicePriority = -2000000;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The five vtables (the game's 0x00AB2FB8, 0x00AB2FA0, 0x00AB2F6C,
// 0x00AB2F54 with the deleting destructor in slot 3, 0x00AB2F44).
constexpr int kSlots = 34, kSlots4 = 6, kSlots8 = 13, kSlotsC = 6, kSlots24 = 4;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable24[kSlots24];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, field::kRefs);
}

// As the game's (0x00702040): other interfaces from the system service
// part's QueryInterface (0x0091DD67).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid == kIidSimulator) {
        *object = self;
    } else if (iid == kIidSerializable) {
        *object = self + 0x4;
    } else {
        return CallThis<uint8_t>(addr::cRZBaseSystemService_QueryInterface, self + 0x8, iid, object);
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

void Destroy(char* self);

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x007020A0): the deleting destructor when the last
// reference goes.
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefs);
    if (refs > 1)
        return --refs;
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("simulator object: destroying %p", static_cast<void*>(self));
    Virtual<void*>(self + 0xC, 0xC, 1u); // the deleting destructor
    return 0;
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

void* __fastcall DeletingDestructorAtC(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - 0xC, nullptr, flags);
}

// --- Construction and destruction -------------------------------------------

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<void**>(self, 0xC) = g_vtableC;
    At<void**>(self, 0x24) = g_vtable24;
}

// As the game's constructor (0x00704120).
void Construct(char* self)
{
    using ServiceFn = void*(__thiscall*)(void* self, uint32_t id, int32_t priority);
    using WatchFn = void*(__thiscall*)(void* self, uint32_t mode);
    using DateFn = void*(__thiscall*)(void* self, uint32_t month, uint32_t day, uint32_t year);
    At<uint32_t>(self, 0x0) = 0x00AB2E68;
    At<uint32_t>(self, 0x4) = 0x00A80784;
    reinterpret_cast<ServiceFn>(addr::cRZBaseSystemService_cRZBaseSystemService)(self + 0x8, kServiceId,
                                                                                   kServicePriority);
    At<uint32_t>(self, 0x24) = 0x00A81174;
    SetVtables(self);
    At<uint32_t>(self, field::kRefs) = 0;
    At<uint8_t>(self, field::kInitialised) = 0;
    At<uint8_t>(self, field::kSaved2D) = 0;
    At<uint8_t>(self, field::kEstablished) = 0;
    At<int32_t>(self, field::kPaused) = 0;
    At<int32_t>(self, field::kHiddenPaused) = 0;
    At<int32_t>(self, field::kEmergencyPaused) = 0;
    At<int32_t>(self, field::kSpeed) = 3;
    At<int32_t>(self, field::kSaved40) = 0;
    At<uint8_t>(self, field::kSpeedChangeable) = 1;
    reinterpret_cast<WatchFn>(addr::SC4Stopwatch_Construct)(self + field::kStepWatch, 4);
    reinterpret_cast<WatchFn>(addr::SC4Stopwatch_Construct)(self + field::kWatch60, 4);
    At<uint32_t>(self, field::kMaxMsPerTick) = 15;
    At<uint32_t>(self, field::kPacing) = 4;
    At<uint32_t>(self, field::kStepMs) = 100;
    At<uint32_t>(self, field::kFpsCap) = 15;
    CallThis<void*>(addr::SC4CycleClock_Construct, self + field::kCycles);
    reinterpret_cast<DateFn>(addr::cRZDate_FromMonthDayYear)(self + field::kDate, 1, 1, 2000);
    At<void*>(self, field::kMessageServer) = nullptr;
    At<void*>(self, field::kDilations) = nullptr;
    At<void*>(self, field::kDilations + 4) = nullptr;
    At<void*>(self, field::kDilations + 8) = nullptr;
    auto* const dilations = static_cast<uint32_t*>(Allocate(0x14));
    At<void*>(self, field::kDilations + 8) = dilations + 5;
    At<void*>(self, field::kDilations) = dilations;
    for (int i = 0; i < 5; ++i)
        dilations[i] = 0;
    At<void*>(self, field::kDilations + 4) = dilations + 5;
    for (int type = 0; type < kAgentTypes; ++type) {
        char*& header = AgentHeader(self, type);
        header = nullptr;
        header = static_cast<char*>(Allocate(0x30));
        AgentCount(self, type) = 0;
        At<uint8_t>(header, 0) = 0;
        At<char*>(header, 0x4) = nullptr;
        At<char*>(header, 0x8) = header;
        At<char*>(header, 0xC) = header;
    }
    reinterpret_cast<WatchFn>(addr::SC4Stopwatch_Construct)(self + field::kSliceWatch, 4);
    for (int type = 0; type < kAgentTypes; ++type)
        Cursor(self, type) = nullptr;
    At<uint32_t>(self, field::kStepsPerSpeed) = 5000;
    At<uint32_t>(self, field::kStepsPerSpeed + 4) = 1000;
    At<uint32_t>(self, field::kStepsPerSpeed + 8) = 100;
    for (int type = 0; type < kAgentTypes; ++type) {
        At<uint32_t>(self, field::kAgentData + 4 * type) = 0;
        At<uint8_t>(self, field::kAgentDue + type) = 0;
    }
    const float cycles = At<float>(self, field::kCycles);
    Global<float>(addr::SC4Deadline_MsPerTick) = cycles;
    Global<float>(addr::SC4Deadline_TicksPerMs) =
        static_cast<float>(static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)) / static_cast<double>(cycles));
}

// As the game's destructor (0x00703C50): the maps from the last type to
// the first, the dilations, the date, the watches, the service part.
void Destroy(char* self)
{
    SetVtables(self);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct)(self + field::kSliceWatch);
    for (int type = kAgentTypes - 1; type >= 0; --type) {
        char*& header = AgentHeader(self, type);
        if (AgentCount(self, type) != 0) {
            CallThis<void>(addr::cSC4Simulator_FreeAgentNodes, self + field::kAgents + 12 * type, At<char*>(header, 0x4));
            At<char*>(header, 0x8) = header;
            At<char*>(header, 0x4) = nullptr;
            At<char*>(header, 0xC) = header;
            AgentCount(self, type) = 0;
        }
        if (header)
            Free(header);
    }
    if (void* const dilations = At<void*>(self, field::kDilations))
        Free(dilations);
    reinterpret_cast<ThisFn>(addr::cRZDate_Destroy)(self + field::kDate);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct)(self + field::kWatch60);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct)(self + field::kStepWatch);
    reinterpret_cast<ThisFn>(addr::cRZBaseSystemService_Destroy)(self + 0x8);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4Simulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4Simulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4Simulator_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4Simulator_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable24, reinterpret_cast<const void*>(addr::cSC4Simulator_vftable_24), sizeof(g_vtable24));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<0xC>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<0xC>);
    g_vtableC[3] = reinterpret_cast<void*>(&DeletingDestructorAtC);
    g_vtable24[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x24>);
    g_vtable24[1] = reinterpret_cast<void*>(&AddRefAt<0x24>);
    g_vtable24[2] = reinterpret_cast<void*>(&ReleaseAt<0x24>);
}

// The factory, as the game's (0x00704340): operator new(0x1E0), then the
// constructor.
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
        log::Write("simulator object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.core.object",
    "simulators/core",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4Simulator object under the game's class ID: factory, constructor, destructor, identity; methods from "
    "the (patched) vtables (docs/simulator.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::core_sim
