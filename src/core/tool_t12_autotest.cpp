// OPENSC4_TEST_T12=<day>: the plop tool's message target thunks
// (ui.tools.plop5, docs/tools.md). As T9's to T11's tests it calls only the
// game's addresses and the slots of the four message vtables that hold the
// thunks, never our functions. The objects of parts 1 and 2 are stand-ins
// whose vtable records what it is asked (only which object was meant goes
// into the hash, never a pointer); every other slot is counted:
// `unexpected slots` must be 0.
//
// OPENSC4_TEST_T12_PARTS (decimal mask, default 7):
//   1  the three thunks at their addresses on a stand-in, the reference count
//      at 0, 1, 2 and 5, QueryInterface with five IDs
//   2  the same through slots 0 to 2 of each of the four vtables (the plop,
//      query, dispatch and MySim dispatch tools' message vtables)
//   4  real objects of the four tools made by their constructors, through
//      the message target their constructor set: QueryInterface, AddRef and
//      Release down to one reference, then the deleting destructor
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

char g_token[32];
Fnv* g_log = nullptr;
int g_unexpected = 0;

template <typename T>
void Note(const T& value)
{
    if (g_log)
        g_log->Add(value);
}

template <typename R, typename... Args>
R Game(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

constexpr std::size_t kTarget = 0x28;

constexpr uint32_t kIds[] = {1, 0x452294AA, 0xC6BB8345, 0, 0x6C3A6E36};

// The four message vtables that hold the thunks.
constexpr std::uintptr_t kVtables[] = {
    addr::cSC4ViewInputControlPlaceLot_MessageVtable,
    addr::SC4QueryTool_TargetVtable,
    addr::SC4DispatchTool_TargetVtable,
    addr::SC4UI_MySimDispatchToolMessageVtable,
};

// --- Stand-ins --------------------------------------------------------------------------

constexpr std::size_t kSlots = 0x20;

struct Object {
    void** vtable;
    uint8_t raw[0x5C];
};

struct Vtable {
    void* slots[kSlots];
};

Vtable g_vtable;
Object g_object;

// What the out pointer of a QueryInterface was left as: 0 null, 1 the object,
// 2 its message target, 3 untouched, 0xFF anything else.
uint8_t Which(const void* out, const void* object)
{
    if (out == reinterpret_cast<const void*>(uintptr_t{0xCDCDCDCD}))
        return 3;
    if (!out)
        return 0;
    if (out == object)
        return 1;
    if (out == static_cast<const char*>(object) + kTarget)
        return 2;
    return 0xFF;
}

uint32_t __fastcall Unexpected(Object* self, void*)
{
    ++g_unexpected;
    Note(uint8_t{0xEE});
    Note(uint8_t{self == &g_object});
    return 0;
}

uint32_t __fastcall StandInAddRef(Object* self, void*)
{
    Note(uint8_t{0x01});
    Note(uint8_t{self == &g_object});
    return 0x11;
}

void __fastcall StandInDestroy(Object* self, void*)
{
    Note(uint8_t{0x1D});
    Note(uint8_t{self == &g_object});
}

void SetUpObject(uint32_t count)
{
    for (void*& slot : g_vtable.slots)
        slot = reinterpret_cast<void*>(&Unexpected);
    g_vtable.slots[1] = reinterpret_cast<void*>(&StandInAddRef);
    g_vtable.slots[29] = reinterpret_cast<void*>(&StandInDestroy);
    std::memset(&g_object, 0, sizeof g_object);
    g_object.vtable = g_vtable.slots;
    At<uint32_t>(&g_object, 8) = count;
}

// The three functions on the stand-in with each count and ID; `functions`
// holds the QueryInterface, AddRef and Release to call (zero for none).
void RunCases(Fnv& f, const std::uintptr_t (&functions)[3])
{
    char* const target = reinterpret_cast<char*>(&g_object) + kTarget;
    for (uint32_t count : {0u, 1u, 2u, 5u}) {
        if (functions[0]) {
            for (uint32_t id : kIds) {
                SetUpObject(count);
                void* out = reinterpret_cast<void*>(uintptr_t{0xCDCDCDCD});
                f.Add(Game<uint8_t>(functions[0], target, id, &out));
                f.Add(Which(out, &g_object));
                f.Add(At<uint32_t>(&g_object, 8));
            }
        }
        if (functions[1]) {
            SetUpObject(count);
            f.Add(Game<uint32_t>(functions[1], target));
            f.Add(At<uint32_t>(&g_object, 8));
        }
        if (functions[2]) {
            SetUpObject(count);
            f.Add(Game<uint32_t>(functions[2], target));
            f.Add(At<uint32_t>(&g_object, 8));
        }
    }
}

void PartAddresses(Fnv& out)
{
    Fnv f;
    g_log = &f;
    const std::uintptr_t functions[3] = {addr::cSC4ViewInputControlPlaceLot_MessageQueryInterface,
                                         addr::cSC4ViewInputControlPlaceLot_MessageAddRef,
                                         addr::cSC4ViewInputControlPlaceLot_MessageRelease};
    RunCases(f, functions);
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: plop5: addresses %08X", f.h);
    out.Add(f.h);
}

// Which of the thunks a vtable slot holds: 1 to 3 ours to translate, 4 the
// base's QueryInterface thunk, 0 anything else.
uint8_t SlotKind(std::uintptr_t slot)
{
    if (slot == addr::cSC4ViewInputControlPlaceLot_MessageQueryInterface)
        return 1;
    if (slot == addr::cSC4ViewInputControlPlaceLot_MessageAddRef)
        return 2;
    if (slot == addr::cSC4ViewInputControlPlaceLot_MessageRelease)
        return 3;
    if (slot == 0x004BA870)
        return 4;
    return 0;
}

void PartVtables(Fnv& out)
{
    Fnv f;
    g_log = &f;
    for (std::uintptr_t vtable : kVtables) {
        const auto* const slots = reinterpret_cast<const std::uintptr_t*>(vtable);
        const std::uintptr_t functions[3] = {slots[0], slots[1], slots[2]};
        for (std::uintptr_t function : functions)
            f.Add(SlotKind(function));
        RunCases(f, functions);
        log::Write("autotest: plop5: vtable %08X: %08X", static_cast<uint32_t>(vtable), f.h);
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: plop5: vtables %08X", f.h);
    out.Add(f.h);
}

// --- Real objects ------------------------------------------------------------------------

char* MakeTool(int kind)
{
    switch (kind) {
    case 0: {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x158});
        return memory ? Game<char*>(addr::SC4ViewInputControls_F4C0430, memory, uint32_t{0}, uint32_t{0}, uint32_t{0},
                                    uint32_t{0})
                      : nullptr;
    }
    case 1: {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xA0});
        return memory ? Game<char*>(addr::SC4ViewInputControls_F4C4590, memory, uint32_t{0}, uint32_t{0}) : nullptr;
    }
    case 2: {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x90});
        return memory ? Game<char*>(addr::SC4DispatchTool_Construct, memory, uint32_t{0}) : nullptr;
    }
    default: {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x90});
        return memory ? Game<char*>(addr::SC4MySimHelper90_Construct, memory, static_cast<void*>(nullptr)) : nullptr;
    }
    }
}

void PartReal(Fnv& out)
{
    Fnv f;
    for (int kind = 0; kind < 4; ++kind) {
        char* const tool = MakeTool(kind);
        if (!tool) {
            log::Write("autotest: plop5: real: tool %d not made", kind);
            f.Add(uint32_t{0xFEED});
            continue;
        }
        char* const target = tool + kTarget;
        const std::uintptr_t vtable = At<std::uintptr_t>(target, 0);
        f.Add(uint8_t{vtable == kVtables[kind]});
        f.Add(At<uint32_t>(tool, 8));
        for (uint32_t id : kIds) {
            void* object = reinterpret_cast<void*>(uintptr_t{0xCDCDCDCD});
            f.Add(Virtual<uint8_t>(target, 0x0, id, &object));
            f.Add(Which(object, tool));
            f.Add(At<uint32_t>(tool, 8));
        }
        f.Add(Virtual<uint32_t>(target, 0x4));
        f.Add(Virtual<uint32_t>(target, 0x4));
        f.Add(At<uint32_t>(tool, 8));
        while (At<uint32_t>(tool, 8) > 1) {
            f.Add(Virtual<uint32_t>(target, 0x8));
            f.Add(At<uint32_t>(tool, 8));
        }
        Virtual<void*>(tool, 0x68, uint32_t{1});
        log::Write("autotest: plop5: real: tool %d: %08X", kind, f.h);
    }
    log::Write("autotest: plop5: real %08X", f.h);
    out.Add(f.h);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_T12") || !game::IsBuild641())
        return;
    if (!Global<void*>(addr::SC4_City)) {
        log::Write("autotest: plop5: no city");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_T12_PARTS", 7);
    g_unexpected = 0;
    Fnv total;
    if (parts & 1)
        PartAddresses(total);
    if (parts & 2)
        PartVtables(total);
    if (parts & 4)
        PartReal(total);
    total.Add(g_unexpected);
    std::snprintf(g_token, sizeof g_token, " plop5 %08X", total.h);
    log::Write("autotest: plop5: total %08X (parts %d, unexpected slots %d)", total.h, parts, g_unexpected);
}

const DayHook kPlop5{"plop5", 74, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"plop5", 36, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
