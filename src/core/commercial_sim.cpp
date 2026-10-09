// Our cSC4CommercialSimulator and cSC4IndustrialSimulator (build 641;
// docs/commercial-industrial.md). The commercial simulator keeps two
// proximity maps (bit grids, +0x34 for developer type 0x3320 and +0x38 for
// 0x3330) filled from the building development simulator's building map in a three-step cycle that
// restarts when a commercial occupant comes or goes; the industrial one is a
// stub. Every function is a replacement on the original objects (vtable
// slots), and simulators.commercial.object / simulators.industrial.object
// register our own objects under the class IDs.
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::commercial_sim {
namespace {

// Fields of cSC4CommercialSimulator (offsets from the object's start; 0x3C bytes).
namespace field {
constexpr std::size_t kInitialized = 0x0C; // uint8
constexpr std::size_t kDevelopment = 0x14; // the building development simulator (0x00B43D68)
constexpr std::size_t kCity = 0x18;
constexpr std::size_t kMessages = 0x1C;    // the message server
constexpr std::size_t kDirty = 0x20;       // uint8: a commercial occupant came or went
constexpr std::size_t kStep = 0x24;        // 0 to 3
constexpr std::size_t kSource1 = 0x28;     // the demand simulator's demands 0x3000 and 0x3800
constexpr std::size_t kSource2 = 0x2C;
constexpr std::size_t kFlag = 0x30;        // uint8, handed to the maps (their slot 9)
constexpr std::size_t kMap3320 = 0x34;
constexpr std::size_t kMap3330 = 0x38;
} // namespace field

constexpr uint32_t kOccupantKind = 0x278128A0;
constexpr uint32_t kIidOccupant = 0x87DDA3A9;
constexpr uint32_t kIidGrid = 0xC9BD939A;

void FirstCall(const char* who, const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("%s: first %s", who, name);
}

// --- Commercial: the interface ------------------------------------------------

// Slot 5 (0x0069EAD0): the proximity map of a developer type.
void* __fastcall GetProximityMap(void* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("commercial", "GetProximityMap", logged);
    if (type == 0x3320)
        return At<void*>(self, field::kMap3320);
    if (type == 0x3330)
        return At<void*>(self, field::kMap3330);
    return nullptr;
}

// --- Commercial: the step and messages ---------------------------------------

// 0x0069ED30 (the argument is not read): one step of the cycle, timed with
// the game's stopwatch (type 4). 0, then 1: the 0x3320 map from the building
// development simulator's building map (its slot 41) for the key of purpose
// 3, wealth 2 (its slot 42); 2: the 0x3330 map for wealth 3; 3: when dirty,
// back to 0.
void Step(void* self)
{
    using StopwatchFn = void(__thiscall*)(void* stopwatch);
    using StopwatchCtorFn = void*(__thiscall*)(void* stopwatch, uint32_t type);
    using SpreadFn = void(__cdecl*)(void* to, void* buildings, uint32_t key);
    alignas(8) char stopwatch[0x18];
    reinterpret_cast<StopwatchCtorFn>(addr::SC4Stopwatch_Construct)(stopwatch, 4u);
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Start)(stopwatch);
    const auto Fill = [&](std::size_t map, uint32_t wealth) {
        void* const development = At<void*>(self, field::kDevelopment);
        const uint32_t key = Virtual<uint32_t>(development, 0xA8, 3u, wealth);
        void* const buildings = Virtual<void*>(development, 0xA4);
        reinterpret_cast<SpreadFn>(addr::SC4SimGridBool8_FromBuildings)(At<void*>(self, map), buildings, key);
    };
    switch (At<uint32_t>(self, field::kStep)) {
    case 0:
        At<uint32_t>(self, field::kStep) = 1;
        [[fallthrough]];
    case 1:
        Fill(field::kMap3320, 2u);
        At<uint32_t>(self, field::kStep) = 2;
        break;
    case 2:
        Fill(field::kMap3330, 3u);
        At<uint32_t>(self, field::kStep) = 3;
        break;
    case 3:
        if (At<uint8_t>(self, field::kDirty)) {
            At<uint8_t>(self, field::kDirty) = 0;
            At<uint32_t>(self, field::kStep) = 0;
        }
        break;
    default:
        break;
    }
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Destruct)(stopwatch);
}

// cIGZMessageTarget2 slot 3 (0x0069EEB0), on the part at +4: the step on
// 0x66956814 and 0x6695681A; dirty when an occupant whose kind (its slot
// 11, the first word) is 3 is added or removed.
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("commercial", "DoMessage", logged);
    void* const self = static_cast<char*>(part) - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x66956814) {
        Step(self);
    } else if (type == 0x6695681A) {
        Virtual<uint32_t>(message, 0x4C);
        Step(self);
    } else if (type == 0x99EF1142 || type == 0x99EF1143) {
        if (Virtual<uint32_t>(message, 0x4C) == kOccupantKind) {
            void* const object = Virtual<void*>(message, 0x28);
            void* occupant = nullptr;
            if (Virtual<uint8_t>(object, 0x0, kIidOccupant, &occupant)) {
                if (*Virtual<uint32_t*>(occupant, 0x2C) == 3)
                    At<uint8_t>(self, field::kDirty) = 1;
                Virtual<uint32_t>(occupant, 0x8);
            }
        }
    }
    return 1;
}

// --- Commercial: Init and Shutdown -------------------------------------------

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A new proximity map (0x44 bytes, its slot 3, its slot 9 with +0x30, its
// slot 6 with `id`), referenced.
void* NewMap(void* self, uint32_t id)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using GridCtorFn = void*(__thiscall*)(void* grid, uint32_t unused);
    void* map = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x44);
    if (map)
        map = reinterpret_cast<GridCtorFn>(addr::SC4SimGridBool8_SC4SimGridBool8)(map, 0x49B9E600u);
    Virtual<uint32_t>(map, 0x4);
    Virtual<void>(map, 0xC);
    Virtual<void>(map, 0x24, static_cast<uint32_t>(At<uint8_t>(self, field::kFlag)));
    Virtual<void>(map, 0x18, id);
    return map;
}

// Slot 3 (0x0069EF90): registration with the simulator service (0, 10, 4
// and 6), the two occupant messages, the city, the building development
// simulator, the demand simulator's demands 0x3000 and 0x3800 (its slot 6;
// false without them), and the proximity maps when not loaded.
uint8_t __fastcall Init(void* self, void*)
{
    using FreeFn = void(__cdecl*)(void* block);
    using TextFn = void*(__thiscall*)(void* string, const char* text);
    static bool logged = false;
    FirstCall("commercial", "Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    uint8_t& once = Global<uint8_t>(addr::cSC4CommercialSimulator_Started);
    if (!once)
        once = 1;
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    GameString name{};
    reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                       reinterpret_cast<const char*>(addr::cSC4CommercialSimulator_Name));
    void* const target = static_cast<char*>(self) + 0x4;
    for (const uint32_t phase : {0u, 10u, 4u, 6u})
        Virtual<uint32_t>(simulators, 0x4C, target, phase, &name, 2u);
    if (!At<void*>(self, field::kMessages)) {
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, field::kMessages) = messages;
        Virtual<uint32_t>(messages, 0x4);
        Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, 0x99EF1142u);
        Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, 0x99EF1143u);
    }
    if (!At<void*>(self, field::kCity)) {
        void* const city = Global<void*>(addr::SC4_City);
        At<void*>(self, field::kCity) = city;
        Virtual<uint32_t>(city, 0x4);
    }
    if (!At<void*>(self, field::kDevelopment)) {
        void* const zones = Global<void*>(addr::SC4_Service43D68);
        At<void*>(self, field::kDevelopment) = zones;
        Virtual<uint32_t>(zones, 0x4);
    }
    const auto Done = [&](uint8_t result) {
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(name.begin);
        return result;
    };
    void* const demand = Virtual<void*>(At<void*>(self, field::kCity), 0xE0);
    if (!demand)
        return Done(0);
    void* const first = Virtual<void*>(demand, 0x18, 0x3000u, 0u);
    At<void*>(self, field::kSource1) = first;
    if (!first)
        return Done(0);
    Virtual<uint32_t>(first, 0x4);
    void* const second = Virtual<void*>(demand, 0x18, 0x3800u, 0u);
    At<void*>(self, field::kSource2) = second;
    if (!second)
        return Done(0);
    Virtual<uint32_t>(second, 0x4);
    if (!At<void*>(self, field::kMap3330))
        At<void*>(self, field::kMap3330) = NewMap(self, 0x89D5B6EFu);
    if (!At<void*>(self, field::kMap3320))
        At<void*>(self, field::kMap3320) = NewMap(self, 0x89D5B788u);
    return Done(1);
}

// A referenced field shut down (its slot 4) when `shutDown`, released and
// cleared.
void Drop(void* self, std::size_t offset, bool shutDown)
{
    if (void* const object = At<void*>(self, offset)) {
        if (shutDown) {
            Virtual<uint8_t>(object, 0x10);
            Virtual<uint32_t>(At<void*>(self, offset), 0x8);
        } else {
            Virtual<uint32_t>(object, 0x8);
        }
        At<void*>(self, offset) = nullptr;
    }
}

// Slot 4 (0x0069EB80): the maps shut down and released, the references
// released, the messages off, unregistered from the simulator service.
uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("commercial", "Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    Drop(self, field::kMap3330, true);
    Drop(self, field::kMap3320, true);
    Drop(self, field::kCity, false);
    Drop(self, field::kDevelopment, false);
    if (void* const messages = At<void*>(self, field::kMessages)) {
        void* const target = static_cast<char*>(self) + 0x4;
        Virtual<uint8_t>(messages, 0x18, target, 0x99EF1142u);
        Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, 0x99EF1143u);
        Virtual<uint32_t>(At<void*>(self, field::kMessages), 0x8);
        At<void*>(self, field::kMessages) = nullptr;
    }
    Drop(self, field::kSource1, false);
    Drop(self, field::kSource2, false);
    if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, static_cast<char*>(self) + 0x4);
    return 1;
}

// --- Commercial: save ----------------------------------------------------------

// cIGZSerializable slot 3 (0x0069EC50), on the part at +8: version 1,
// whether a cycle is pending (dirty or not at step 3), the two maps.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    using RefFn = void(__cdecl*)(void* stream, void* object);
    static bool logged = false;
    FirstCall("commercial", "Write", logged);
    char* const self = part - 8;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    const bool pending = At<uint8_t>(self, field::kDirty) || At<uint32_t>(self, field::kStep) != 3;
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(pending));
    reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference)(stream, At<void*>(self, field::kMap3320));
    reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference)(stream, At<void*>(self, field::kMap3330));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x0069ECB0): version 1 only; the pending flag into +0x20 and the
// two maps (interface 0xC9BD939A).
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    using RefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** out, uint32_t zero);
    static bool logged = false;
    FirstCall("commercial", "Read", logged);
    char* const self = part - 8;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    uint8_t pending = 0;
    Virtual<uint8_t>(stream, 0x14, &pending);
    At<uint8_t>(self, field::kDirty) = pending ? 1 : 0;
    reinterpret_cast<RefFn>(addr::SC4Stream_ReadReference)(stream, kIidGrid, &At<void*>(self, field::kMap3320), 0u);
    reinterpret_cast<RefFn>(addr::SC4Stream_ReadReference)(stream, kIidGrid, &At<void*>(self, field::kMap3330), 0u);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Industrial ----------------------------------------------------------------

// Slot 3 (0x006B89F0): initialised, and a global flag set once.
uint8_t __fastcall IndustrialInit(void* self, void*)
{
    static bool logged = false;
    FirstCall("industrial", "Init", logged);
    if (!At<uint8_t>(self, 0xC)) {
        At<uint8_t>(self, 0xC) = 1;
        uint8_t& once = Global<uint8_t>(addr::cSC4IndustrialSimulator_Started);
        if (!once)
            once = 1;
    }
    return 1;
}

// Slot 4 (0x006B8A10).
uint8_t __fastcall IndustrialShutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("industrial", "Shutdown", logged);
    if (At<uint8_t>(self, 0xC))
        At<uint8_t>(self, 0xC) = 0;
    return 1;
}

// cIGZMessageTarget2 slot 3 (the game's shared 0x006B8AA0): the type read,
// nothing done.
uint8_t __fastcall IndustrialDoMessage(void*, void*, void* message)
{
    static bool logged = false;
    FirstCall("industrial", "DoMessage", logged);
    Virtual<uint32_t>(message, 0x10);
    return 1;
}

// cIGZSerializable slots 3 and 4 (the game's shared 0x0042E440 and
// 0x006B8A70): version 1, no data.
uint32_t __fastcall IndustrialWrite(void*, void*, void* stream)
{
    static bool logged = false;
    FirstCall("industrial", "Write", logged);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

uint32_t __fastcall IndustrialRead(void*, void*, void* stream)
{
    static bool logged = false;
    FirstCall("industrial", "Read", logged);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Installation ----------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kCommercialSlots[] = {
    OPENSC4_SLOT(addr::cSC4CommercialSimulator_vftable, 3, 0x0069EF90, Init),
    OPENSC4_SLOT(addr::cSC4CommercialSimulator_vftable, 4, 0x0069EB80, Shutdown),
    OPENSC4_SLOT(addr::cSC4CommercialSimulator_vftable, 5, 0x0069EAD0, GetProximityMap),
    OPENSC4_SLOT(addr::cSC4CommercialSimulator_vftable_4, 3, 0x0069EEB0, DoMessage),
    OPENSC4_SLOT(addr::cSC4CommercialSimulator_vftable_8, 3, 0x0069EC50, Write),
    OPENSC4_SLOT(addr::cSC4CommercialSimulator_vftable_8, 4, 0x0069ECB0, Read),
};
const Slot kIndustrialSlots[] = {
    OPENSC4_SLOT(addr::cSC4IndustrialSimulator_vftable, 3, 0x006B89F0, IndustrialInit),
    OPENSC4_SLOT(addr::cSC4IndustrialSimulator_vftable, 4, 0x006B8A10, IndustrialShutdown),
    OPENSC4_SLOT(addr::cSC4IndustrialSimulator_vftable_4, 3, 0x006B8AA0, IndustrialDoMessage),
    OPENSC4_SLOT(addr::cSC4IndustrialSimulator_vftable_8, 3, 0x0042E440, IndustrialWrite),
    OPENSC4_SLOT(addr::cSC4IndustrialSimulator_vftable_8, 4, 0x006B8A70, IndustrialRead),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("%s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallCommercial(registry::Mode) { return SwapSlots(kCommercialSlots, "commercial sim"); }
bool InstallIndustrial(registry::Mode) { return SwapSlots(kIndustrialSlots, "industrial sim"); }

const bool g_registered[] = {
    registry::Register({"simulators.commercial.sim", "simulators/commercial", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4CommercialSimulator_vftable,
                        "Our cSC4CommercialSimulator: Init, Shutdown, the proximity maps and their cycle, messages, "
                        "save (docs/commercial-industrial.md)",
                        false, InstallCommercial}),
    registry::Register({"simulators.industrial.sim", "simulators/industrial", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4IndustrialSimulator_vftable,
                        "Our cSC4IndustrialSimulator (a stub in the game) (docs/commercial-industrial.md)", false,
                        InstallIndustrial}),
};

// --- Our objects -------------------------------------------------------------------

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;

// One simulator class: its class ID, the IID of its own interface, its
// size, the game's vtables and the destructor slot.
template <uint32_t ClassId, uint32_t Iid, std::size_t Size, int Slots, std::uintptr_t Vtable, std::uintptr_t Vtable4,
          std::uintptr_t Vtable8>
struct Object {
    static inline void* vtable[Slots];
    static inline void* vtable4[4];
    static inline void* vtable8[6];

    static uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, 0x10); }

    // As the game's (0x0069EA50, 0x006B8970).
    static uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
    {
        std::size_t offset;
        switch (iid) {
        case Iid:
        case kIidUnknown: offset = 0; break;
        case kIidMessageTarget: offset = 0x4; break;
        case kIidSerializable: offset = 0x8; break;
        default: return 0;
        }
        *object = self ? self + offset : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }

    // As the game's (0x0069EB50, 0x006D5140): Shutdown and the deleting
    // destructor at the last reference.
    static uint32_t __fastcall Release(char* self, void*)
    {
        int32_t& refs = At<int32_t>(self, 0x10);
        if (refs > 1)
            return static_cast<uint32_t>(--refs);
        Virtual<uint8_t>(self, 0x10);
        Virtual<void*>(self, 4 * (Slots - 1), 1u);
        return 0;
    }

    static void SetVtables(char* self)
    {
        At<void**>(self, 0x0) = vtable;
        At<void**>(self, 0x4) = vtable4;
        At<void**>(self, 0x8) = vtable8;
    }

    // As the game's (0x0069EB20, 0x006B8A40): nothing else is released.
    static void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
    {
        using FreeFn = void(__cdecl*)(void* block);
        static int destroyed = 0;
        if (destroyed++ < 3)
            log::Write("simulator %08X object: destroying %p", ClassId, static_cast<void*>(self));
        SetVtables(self);
        if (flags & 1)
            reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
        return self;
    }

    static uint32_t __fastcall GetGZCLSID(void*, void*) { return ClassId; }

    template <std::size_t Offset>
    static uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
    {
        return QueryInterface(part - Offset, nullptr, iid, object);
    }

    template <std::size_t Offset>
    static uint32_t __fastcall AddRefAt(char* part, void*)
    {
        return AddRef(part - Offset, nullptr);
    }

    template <std::size_t Offset>
    static uint32_t __fastcall ReleaseAt(char* part, void*)
    {
        return Release(part - Offset, nullptr);
    }

    static void BuildVtables()
    {
        std::memcpy(vtable, reinterpret_cast<const void*>(Vtable), sizeof(vtable));
        std::memcpy(vtable4, reinterpret_cast<const void*>(Vtable4), sizeof(vtable4));
        std::memcpy(vtable8, reinterpret_cast<const void*>(Vtable8), sizeof(vtable8));
        vtable[0] = reinterpret_cast<void*>(&QueryInterface);
        vtable[1] = reinterpret_cast<void*>(&AddRef);
        vtable[2] = reinterpret_cast<void*>(&Release);
        vtable[Slots - 1] = reinterpret_cast<void*>(&DeletingDestructor);
        vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
        vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
        vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
        vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
        vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
        vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
        vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    }

    // The factory: the game's size and constructor fields, our vtables.
    static cIGZUnknown* Create()
    {
        using NewFn = void*(__cdecl*)(std::size_t size);
        static bool built = false;
        if (!built) {
            built = true;
            BuildVtables();
        }
        auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(Size));
        if (!self)
            return nullptr;
        std::memset(self + 0xC, 0, Size - 0xC);
        SetVtables(self);
        if constexpr (ClassId == 0x8990C080) {
            // As the game's constructor (0x0069EE50).
            At<uint8_t>(self, field::kDirty) = 1;
            At<uint8_t>(self, field::kFlag) = 1;
            At<uint32_t>(self, field::kStep) = 3;
        }
        static int created = 0;
        if (created++ < 3)
            log::Write("simulator %08X object: created %p", ClassId, static_cast<void*>(self));
        return reinterpret_cast<cIGZUnknown*>(self);
    }
};

using Commercial = Object<0x8990C080, 0x077ABEB2, 0x3C, 7, addr::cSC4CommercialSimulator_vftable,
                          addr::cSC4CommercialSimulator_vftable_4, addr::cSC4CommercialSimulator_vftable_8>;
using Industrial = Object<0xA990C022, 0x477A9E7D, 0x14, 6, addr::cSC4IndustrialSimulator_vftable,
                          addr::cSC4IndustrialSimulator_vftable_4, addr::cSC4IndustrialSimulator_vftable_8>;

bool ApplyCommercial(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(0x8990C080, &Commercial::Create);
}

bool ApplyIndustrial(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(0xA990C022, &Industrial::Create);
}

const bool g_registeredObjects[] = {
    registry::Register({"simulators.commercial.object", "simulators/commercial", registry::Kind::Gzcom,
                        registry::Phase::DirectorInit, 0x8990C080,
                        "Our cSC4CommercialSimulator object under the game's class ID: factory, constructor, "
                        "destructor, identity; methods from the (patched) vtables (docs/commercial-industrial.md)",
                        false, ApplyCommercial}),
    registry::Register({"simulators.industrial.object", "simulators/industrial", registry::Kind::Gzcom,
                        registry::Phase::DirectorInit, 0xA990C022,
                        "Our cSC4IndustrialSimulator object under the game's class ID (docs/commercial-industrial.md)",
                        false, ApplyIndustrial}),
};

} // namespace
} // namespace opensc4::commercial_sim
