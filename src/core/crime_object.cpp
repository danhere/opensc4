// Our cSC4CrimeSimulator (build 641), fourth part: the messages, Init and
// Shutdown, the save record, the third interface and our own object under
// the game's class ID (docs/crime.md).
#include "core/crime_sim.h"

#include "core/address_check.h"
#include "core/director.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace opensc4::crime_sim {
namespace {

using SelfFn = void(__thiscall*)(void* self);
using StepFn = void(__thiscall*)(void* self, uint32_t amount);
using OccupantFn = void(__thiscall*)(void* self, void* occupant, uint8_t inserted);
using StaticFn = void(__cdecl*)();
using EffectFn = void(__cdecl*)(void* rows, int32_t width, int32_t height, int32_t x, int32_t z, float radius,
                                float value, uint32_t flags);
using TextFn = void(__thiscall*)(void* string, const char* text);
using IteratorBoundsFn = void(__thiscall*)(void* iterator, const int32_t* rect);
using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* object, uint32_t flags);
using WriteReferenceFn = uint8_t(__cdecl*)(void* stream, void* object);
using StreamStructFn = uint8_t(__cdecl*)(void* stream, void* data);
using ReserveFn = void(__thiscall*)(Vector* vector, uint32_t count);
using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t count);
using CurveInitFn = void(__thiscall*)(void* curve);

constexpr uint32_t kGridInterface = 0x49B9E602;
constexpr uint32_t kGridIid = 0xC9BD939A;
constexpr uint32_t kOccupantIid = 0xE818FCED;
constexpr uint32_t kVersion = 6;
constexpr uint32_t kMessages[] = {0x99EF1142, 0x99EF1143, 0xC2C09554, 0xC53D10AA};

void* Self(void* part, std::size_t offset) { return static_cast<char*>(part) - offset; }

// --- Messages -------------------------------------------------------------------

// 0x0069F260 (messages 0x99EF1142/43): a building's crime effect (property
// 0xCA5B9306: strength, radius in meters) added to the effect grid, or taken
// off when it goes, around its cell (0x005E3D80).
void __fastcall OnOccupant(void* self, void*, void* occupant, uint8_t inserted)
{
    static bool logged = false;
    FirstCall("OnOccupant", logged);
    void* const property = Virtual<void*>(Virtual<void*>(occupant, 0x18), 0x24, 0xCA5B9306u);
    if (!property)
        return;
    const auto* const effect = Virtual<const uint8_t*>(Virtual<void*>(property, 0x14), 0x17C);
    const int32_t value = inserted ? effect[0] : -static_cast<int32_t>(effect[0]);
    const float radius = static_cast<float>(effect[1]);
    void* const grid = At<void*>(self, field::kEffectGrid);
    const int32_t cellSize = Virtual<int32_t>(grid, 0x28);
    const float cells = static_cast<float>(static_cast<double>(radius) / static_cast<double>(cellSize));
    float position[3];
    Virtual<uint32_t>(occupant, 0x20, position);
    int32_t x, z;
    Virtual<uint8_t>(grid, 0x4C, position[0], position[2], &x, &z);
    const int32_t height = Virtual<int32_t>(grid, 0x34);
    const int32_t width = Virtual<int32_t>(grid, 0x30);
    void* const rows = Virtual<void*>(grid, 0x60);
    reinterpret_cast<EffectFn>(addr::SC4_ApplyRadialEffect)(rows, width, height, x, z, cells,
                                                            static_cast<float>(value), 0);
}

// cIGZMessageTarget2 slot 3 (0x006A21C0), on the part at +4: the step
// (0x66956814 with 5, 0x6695681A with the message's value), MonthPassed
// (state 6 -> 0), buildings added or removed (unless their exemplar has
// 0x029244DB), the ordinance factor (0xC2C09554), the tuning and crime types
// reloaded (0xC53D10AA).
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = Self(part, 0x4);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x66956814:
        reinterpret_cast<StepFn>(addr::cSC4CrimeSimulator_Step)(self, 5);
        return 1;
    case 0x66956816:
        if (At<uint8_t>(self, field::kState) == 6)
            At<uint8_t>(self, field::kState) = 0;
        return 1;
    case 0x6695681A:
        reinterpret_cast<StepFn>(addr::cSC4CrimeSimulator_Step)(self, Virtual<uint32_t>(message, 0x4C));
        return 1;
    case 0x99EF1142:
    case 0x99EF1143: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, 0x029244DBu))
            reinterpret_cast<OccupantFn>(addr::cSC4CrimeSimulator_OnOccupant)(self, occupant,
                                                                               type == 0x99EF1142 ? 1 : 0);
        return 1;
    }
    case 0xC2C09554:
        At<float>(self, field::kOrdinanceFactor) =
            static_cast<float>(Virtual<double>(At<void*>(self, field::kOrdinances), 0x64, 0x28ED0380u, 0u));
        return 1;
    case 0xC53D10AA: {
        const uint32_t group = Virtual<uint32_t>(message, 0x24);
        const uint32_t instance = Virtual<uint32_t>(message, 0x38);
        if (group == 0xE7E2C2DB) {
            if (instance == 0xC8E8F6C5)
                reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_LoadTuning)(self);
        } else if (group == 0xEA12F32C) {
            reinterpret_cast<StaticFn>(addr::cSC4CrimeSimulator_LoadCrimeTypes)();
        }
        return 1;
    }
    default:
        return 1;
    }
}

// --- Init and Shutdown -------------------------------------------------------

// A cSC4SimGridUint8 as Init constructs it (operator new, 0x44 bytes).
void* NewGrid()
{
    auto* const grid = static_cast<uint32_t*>(OperatorNew(0x44));
    if (!grid)
        return nullptr;
    grid[1] = 0x00A80784;
    grid[0] = static_cast<uint32_t>(addr::SC4SimGridUint8_Vtable);
    grid[1] = 0x00A878A4;
    reinterpret_cast<uint8_t*>(grid)[8] = 0;
    grid[3] = 0;
    grid[4] = kGridInterface;
    grid[6] = 0;
    grid[14] = 0;
    grid[15] = 0;
    grid[16] = 0;
    return grid;
}

void TakeReference(void* self, std::size_t offset, void* object)
{
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4);
}

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// 0x006A15B0: once: the crime types (first object only, with their cleanup
// registered), the tuning, registration with the simulator service under
// the class name (4, 6 and 10), message notifications, references to the
// city, the occupant manager, the ordinance service and the police
// simulator, the two grids; unless loaded, the tract walk over the whole
// grid and its registration; the hotspot cleared, the ordinance factor 1.
uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("crime: first Init (%s)", At<uint8_t>(self, field::kLoaded) ? "loaded from a save" : "new");
    }
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    uint8_t& typesLoaded = Global<uint8_t>(addr::cSC4CrimeSimulator_CrimeTypesLoaded);
    if (!typesLoaded) {
        typesLoaded = 1;
        Virtual<uint32_t>(Global<void*>(addr::SC4_Service43C94), 0x114,
                          reinterpret_cast<void*>(addr::cSC4CrimeSimulator_FreeCrimeTypes), 0u);
        reinterpret_cast<StaticFn>(addr::cSC4CrimeSimulator_LoadCrimeTypes)();
    }
    reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_LoadTuning)(self);
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    GameString name{};
    reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name, reinterpret_cast<const char*>(addr::cSC4CrimeSimulator_Name));
    void* const target = static_cast<char*>(self) + 0x4;
    Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
    Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 2u);
    Virtual<uint32_t>(simulators, 0x4C, target, 10u, &name, 2u);
    if (!At<void*>(self, field::kMessageServer)) {
        TakeReference(self, field::kMessageServer, Global<void*>(addr::SC4_MessageServer));
        for (const uint32_t type : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, target, type);
    }
    if (!At<void*>(self, field::kCity))
        TakeReference(self, field::kCity, Global<void*>(addr::SC4_City));
    if (!At<void*>(self, field::kOccupantManager))
        TakeReference(self, field::kOccupantManager, Virtual<void*>(At<void*>(self, field::kCity), 0x88));
    if (!At<void*>(self, field::kOrdinances))
        TakeReference(self, field::kOrdinances, Global<void*>(addr::SC4_Service43D90));
    if (!At<void*>(self, field::kPolice))
        TakeReference(self, field::kPolice, Global<void*>(addr::SC4_PoliceSimulator));
    constexpr struct {
        std::size_t field;
        uint32_t id;
    } kGrids[] = {{field::kCrimeGrid, 0x89D5B895}, {field::kEffectGrid, 0x2A5BA1B3}};
    for (const auto& grid : kGrids) {
        if (At<void*>(self, grid.field))
            continue;
        TakeReference(self, grid.field, NewGrid());
        void* const made = At<void*>(self, grid.field);
        Virtual<uint32_t>(made, 0xC);
        Virtual<uint32_t>(made, 0x24, At<int32_t>(self, field::kGridCellSize));
        Virtual<uint32_t>(made, 0x18, grid.id);
        Virtual<uint32_t>(made, 0x6C, 0u);
    }
    if (!At<uint8_t>(self, field::kLoaded)) {
        void* const grid = At<void*>(self, field::kCrimeGrid);
        const int32_t lastZ = Virtual<int32_t>(grid, 0x34) - 1;
        const int32_t lastX = Virtual<int32_t>(grid, 0x30) - 1;
        const int32_t rect[4] = {0, 0, lastX, lastZ};
        reinterpret_cast<IteratorBoundsFn>(addr::TractIterator_SetBounds)(static_cast<char*>(self) + field::kTractIterator,
                                                                          rect);
        Virtual<uint32_t>(Global<void*>(addr::SC4_Service43DB8), 0x14, static_cast<char*>(self) + 0x8);
    }
    At<int32_t>(self, field::kHotspotX) = 0;
    At<int32_t>(self, field::kHotspotZ) = 0;
    At<int32_t>(self, field::kHotspotValue) = 0;
    At<float>(self, field::kOrdinanceFactor) = 1.0f;
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        Free(name.begin);
    return 1;
}

void ReleaseField(void* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// 0x006A1860: the grids shut down and released, the references released,
// the incidents freed, the notifications removed, the message target
// unregistered from the simulator (city slot 50, its slot 20).
uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    for (const std::size_t grid : {field::kCrimeGrid, field::kEffectGrid})
        if (void* const object = At<void*>(self, grid)) {
            Virtual<uint8_t>(object, 0x10);
            Virtual<uint32_t>(At<void*>(self, grid), 0x8);
            At<void*>(self, grid) = nullptr;
        }
    for (const std::size_t offset : {field::kPolice, field::kOrdinances, field::kOccupantManager, field::kCity})
        ReleaseField(self, offset);
    reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_ClearIncidents)(self);
    if (void* const messages = At<void*>(self, field::kMessageServer)) {
        void* const target = static_cast<char*>(self) + 0x4;
        for (const uint32_t type : {0xC2C09554u, 0xC53D10AAu, 0x99EF1142u, 0x99EF1143u})
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, target, type);
        Virtual<uint32_t>(messages, 0x8);
        At<void*>(self, field::kMessageServer) = nullptr;
    }
    if (void* const developer = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(developer, 0x50, static_cast<char*>(self) + 0x4);
    return 1;
}

// --- The save record (version 6) --------------------------------------------

// cIGZSerializable slot 3 (0x0069F600), on the part at +0xC.
uint8_t __fastcall Write(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    void* const self = Self(part, 0xC);
    const auto WriteReference = reinterpret_cast<WriteReferenceFn>(addr::SC4Stream_WriteReference);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(kVersion));
    WriteReference(stream, At<void*>(self, field::kCrimeGrid));
    WriteReference(stream, At<void*>(self, field::kEffectGrid));
    reinterpret_cast<StreamStructFn>(addr::SC4Stream_WritePollutionStruct)(stream,
                                                                           static_cast<char*>(self) + field::kTractIterator);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kSaved64));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kSaved68));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kAverageCrime));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kOrdinanceFactor));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kMonthlyCrimes));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kHotspotX));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kHotspotZ));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kHotspotValue));
    const auto& incidents = At<Vector>(self, field::kIncidents);
    const uint32_t count = static_cast<uint32_t>((incidents.end - incidents.begin) >> 2);
    Virtual<uint8_t>(stream, 0x20, static_cast<int32_t>(count));
    for (uint32_t i = 0; i < static_cast<uint32_t>((incidents.end - incidents.begin) >> 2); ++i) {
        Incident* const incident = reinterpret_cast<Incident**>(incidents.begin)[i];
        if (!incident)
            continue;
        WriteReference(stream, incident->occupant);
        Virtual<uint8_t>(stream, 0x24, incident->crimeType);
        Virtual<uint8_t>(stream, 0x3C, &incident->nameVtable);
        reinterpret_cast<StreamStructFn>(addr::SC4Stream_WriteResourceKey)(stream, incident->key);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x006A05E0): the same fields; the incidents are
// appended; +0x11 set (Init then leaves the tract walk alone).
uint8_t __fastcall Read(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    void* const self = Self(part, 0xC);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != kVersion)
        return 0;
    const auto ReadReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
    ReadReference(stream, kGridIid, &At<void*>(self, field::kCrimeGrid), 0);
    ReadReference(stream, kGridIid, &At<void*>(self, field::kEffectGrid), 0);
    reinterpret_cast<StreamStructFn>(addr::SC4Stream_ReadPollutionStruct)(stream,
                                                                          static_cast<char*>(self) + field::kTractIterator);
    Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kSaved64));
    Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kSaved68));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kAverageCrime));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kOrdinanceFactor));
    Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kMonthlyCrimes));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kHotspotX));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kHotspotZ));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kHotspotValue));
    int32_t count = 0;
    Virtual<uint8_t>(stream, 0x20, &count);
    for (int32_t i = 0; i < count; ++i) {
        auto* const incident = static_cast<Incident*>(OperatorNew(0x2C));
        if (incident) {
            incident->occupant = nullptr;
            incident->nameBegin = nullptr;
            incident->nameEnd = nullptr;
            incident->nameCapacity = nullptr;
            char* const buffer = static_cast<char*>(StlAllocate(8));
            incident->nameBegin = buffer;
            incident->nameEnd = buffer;
            incident->nameCapacity = buffer + 8;
            buffer[0] = 0;
            incident->nameVtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
            incident->nameZero = 0;
            incident->key[0] = incident->key[1] = incident->key[2] = 0;
        }
        if (void* const previous = incident->occupant) {
            incident->occupant = nullptr;
            Virtual<uint32_t>(previous, 0x8);
        }
        ReadReference(stream, kOccupantIid, &incident->occupant, 0);
        Virtual<uint8_t>(stream, 0x24, &incident->crimeType);
        Virtual<uint8_t>(stream, 0x3C, &incident->nameVtable);
        reinterpret_cast<StreamStructFn>(addr::SC4Stream_ReadResourceKey)(stream, incident->key);
        incident->visualsCreated = 0;
        PushPointer(&At<Vector>(self, field::kIncidents), incident);
    }
    At<uint8_t>(self, field::kLoaded) = 1;
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- The third interface (0x89C498BE, at +8) ----------------------------------

// Slot 3 (0x0069F710): the crime grid over a rectangle (grid slot 8, as a
// fraction of 255) through curve +0xDC, rounded to nearest.
int32_t __fastcall CrimeInRect(void* part, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("CrimeInRect", logged);
    void* const self = Self(part, 0x8);
    const uint8_t crime =
        Virtual<uint8_t>(At<void*>(self, field::kCrimeGrid), 0x20, rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]);
    const float x = static_cast<float>(static_cast<int32_t>(crime) * static_cast<double>(Global<float>(addr::SC4_FloatInv255)));
    const float rounded = static_cast<float>(EvaluateCurve(static_cast<char*>(self) + field::kCurveDC, x) +
                                             Global<float>(addr::SC4_FloatRoundMagic));
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    return static_cast<int32_t>(bits + 0xB4C00000u);
}

// Slot 6 (0x006A00D0): of the candidates, one at random (0x00B48630) among
// those whose kind (service 0x00B43D10, slot 13, +0x18) has an incident of
// that crime type at the lot of the cell; else among those of kind 0; else
// none.
void* __fastcall PickCandidate(void* part, void*, const Vector* candidates, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("PickCandidate", logged);
    void* const self = Self(part, 0x8);
    void* const kinds = Global<void*>(addr::SC4_Service43D10);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u);
    const auto Reserve = reinterpret_cast<ReserveFn>(addr::SC4PtrVector_Reserve);
    const uint32_t count = static_cast<uint32_t>((candidates->end - candidates->begin) >> 2);
    Vector plain{};
    Reserve(&plain, count);
    Vector matching{};
    Reserve(&matching, count);
    uint32_t plainCount = 0, matchingCount = 0;
    for (char* it = candidates->begin; it != candidates->end; it += sizeof(void*)) {
        void* const candidate = *reinterpret_cast<void**>(it);
        const uint32_t kind = At<uint32_t>(Virtual<void*>(kinds, 0x34, candidate), 0x18);
        if (!kind) {
            PushPointer(&plain, candidate);
            ++plainCount;
            continue;
        }
        const auto& incidents = At<Vector>(self, field::kIncidents);
        for (char* p = incidents.begin; p != incidents.end; p += sizeof(void*)) {
            const Incident* const incident = *reinterpret_cast<Incident* const*>(p);
            if (incident->crimeType == kind && incident->occupant == lot) {
                PushPointer(&matching, candidate);
                ++matchingCount;
                break;
            }
        }
    }
    const auto Below = reinterpret_cast<BelowFn>(addr::SC4Random_Below);
    void* chosen = nullptr;
    if (matchingCount)
        chosen = reinterpret_cast<void**>(matching.begin)[Below(reinterpret_cast<void*>(addr::cSC4CrimeSimulator_Random),
                                                                matchingCount)];
    else if (plainCount)
        chosen = reinterpret_cast<void**>(plain.begin)[Below(reinterpret_cast<void*>(addr::cSC4CrimeSimulator_Random),
                                                             plainCount)];
    if (matching.begin)
        Free(matching.begin);
    if (plain.begin)
        Free(plain.begin);
    return chosen;
}

// --- Installation of the above -------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

// Swaps a slot of a game vtable if it still holds the game's function.
bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours, const char* what)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("crime %s: slot %d holds %08X, expected %08X; not installing", what, index,
                   static_cast<unsigned>(*slot), static_cast<unsigned>(original));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

using OnOccupantFn = void(__fastcall*)(void*, void*, void*, uint8_t);
using InitFn = uint8_t(__fastcall*)(void*, void*);
using StreamFn = uint8_t(__fastcall*)(void*, void*, void*);
OnOccupantFn g_originalOnOccupant = nullptr;
InitFn g_originalInit = nullptr, g_originalShutdown = nullptr;

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4CrimeSimulator_OnOccupant, &OnOccupant, &g_originalOnOccupant) &&
           SwapSlot(addr::cSC4CrimeSimulator_vftable_4, 3, 0x006A21C0, reinterpret_cast<void*>(&DoMessage), "messages");
}

bool InstallInit(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4CrimeSimulator_Init, &Init, &g_originalInit);
}

bool InstallShutdown(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4CrimeSimulator_Shutdown, &Shutdown, &g_originalShutdown);
}

bool InstallSave(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4CrimeSimulator_vftable_C, 3, 0x0069F600, reinterpret_cast<void*>(&Write), "save");
}

bool InstallLoad(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4CrimeSimulator_vftable_C, 4, 0x006A05E0, reinterpret_cast<void*>(&Read), "load");
}

bool InstallTargets(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4CrimeSimulator_vftable_8, 3, 0x0069F710, reinterpret_cast<void*>(&CrimeInRect),
                    "targets") &&
           SwapSlot(addr::cSC4CrimeSimulator_vftable_8, 6, 0x006A00D0, reinterpret_cast<void*>(&PickCandidate),
                    "targets");
}

const bool g_registeredMessages = registry::Register({
    "simulators.crime.messages",
    "simulators/crime",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_vftable_4,
    "Our crime DoMessage and building effect handler (docs/crime.md)",
    false,
    InstallMessages,
});

const bool g_registeredInit = registry::Register({
    "simulators.crime.init",
    "simulators/crime",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_Init,
    "Our crime Init (docs/crime.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.crime.shutdown",
    "simulators/crime",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_Shutdown,
    "Our crime Shutdown (docs/crime.md)",
    false,
    InstallShutdown,
});

const bool g_registeredSave = registry::Register({
    "simulators.crime.save",
    "simulators/crime",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_vftable_C,
    "Our crime Write: the save record, version 6 (docs/crime.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.crime.load",
    "simulators/crime",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_vftable_C,
    "Our crime Read: the save record, version 6 (docs/crime.md)",
    false,
    InstallLoad,
});

const bool g_registeredTargets = registry::Register({
    "simulators.crime.targets",
    "simulators/crime",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_vftable_8,
    "Our crime third interface (0x89C498BE): crime over a rectangle, a candidate for an incident (docs/crime.md)",
    false,
    InstallTargets,
});

// --- Our object -------------------------------------------------------------

constexpr uint32_t kClassId = 0x8990C09A;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidCrime = 0x67659555;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidThird = 0x89C498BE;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x174;

// The four vtables (the game's: 0x00AB0DC8 with the deleting destructor in
// slot 14 and a type in slot 15, 0x00AB0DB8, 0x00AB0D9C, 0x00AB0D84).
constexpr int kSlots = 16, kSlots4 = 4, kSlots8 = 7, kSlotsC = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, field::kRefCount);
}

// As the game's (0x0069F1B0): a null result is still a success, as there.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidCrime:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidThird: offset = 0x8; break;
    case kIidSerializable: offset = 0xC; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

void Destroy(char* self);

// As the game's Release (0x0069F5D0): at the last reference, Shutdown and
// the deleting destructor; the count stays at 1.
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x38, 1u);
    return 0;
}

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("crime object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

uint32_t __fastcall SimulatorType(void*, void*) { return 0x27567222; }
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }
uint32_t __fastcall ThirdType(void*, void*) { return 0x49A1E996; }
uint8_t __fastcall ThirdTrue(void*, void*) { return 1; }

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
    At<void**>(self, 0xC) = g_vtableC;
}

// As the game's constructor (0x006A0440): only the fields it sets; the rest
// stay as operator new returned them.
void Construct(char* self)
{
    const auto CurveInit = reinterpret_cast<CurveInitFn>(addr::SC4Curve_Init);
    SetVtables(self);
    At<uint8_t>(self, 0x10) = 0;
    At<uint8_t>(self, 0x11) = 0;
    At<uint32_t>(self, 0x14) = 0;
    At<uint8_t>(self, 0x18) = 1;
    At<uint8_t>(self, 0x19) = 1;
    At<uint8_t>(self, 0x1A) = 1;
    std::memset(self + 0x1C, 0, 0x44 - 0x1C);
    At<uint32_t>(self, 0x44) = 1;
    std::memset(self + 0x48, 0, 0x8C - 0x48);
    At<float>(self, 0x8C) = 1.0f;
    At<uint8_t>(self, 0x90) = 6;
    At<uint32_t>(self, 0xD8) = 0;
    CurveInit(self + 0xDC);
    for (int curve = 0; curve < 3; ++curve)
        CurveInit(self + 0xEC + 16 * curve);
    for (int curve = 0; curve < 3; ++curve)
        CurveInit(self + 0x11C + 16 * curve);
    CurveInit(self + 0x14C);
    CurveInit(self + 0x15C);
    At<float>(self, 0x16C) = 0.5f;
    At<float>(self, 0x170) = 1.0f;
    At<uint32_t>(self, 0xCC) = 0;
    At<uint32_t>(self, 0xD0) = 0;
    At<uint32_t>(self, 0xD4) = 0;
}

// As the game's destructor (0x0069FF90): the curves' points, the incident
// vector's buffer and the tract walk's buffer.
void Destroy(char* self)
{
    SetVtables(self);
    for (const std::size_t curve : {0x15C, 0x14C, 0x13C, 0x12C, 0x11C, 0x10C, 0xFC, 0xEC, 0xDC})
        if (void* const points = At<void*>(self, curve))
            Free(points);
    if (void* const incidents = At<void*>(self, field::kIncidents))
        Free(incidents);
    if (void* const buffer = At<void*>(self, 0x30))
        Free(buffer);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4CrimeSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4CrimeSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4CrimeSimulator_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4CrimeSimulator_vftable_C), sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[14] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable[15] = reinterpret_cast<void*>(&SimulatorType);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[4] = reinterpret_cast<void*>(&ThirdType);
    g_vtable8[5] = reinterpret_cast<void*>(&ThirdTrue);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<0xC>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<0xC>);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x006A05C0): operator new(0x174), then the
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
        log::Write("crime object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registeredObject = registry::Register({
    "simulators.crime.object",
    "simulators/crime",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4CrimeSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/crime.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::crime_sim
