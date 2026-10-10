// Our cSC4LotDeveloper (build 641; src/core/lot_sim.h has the class map),
// the object part: the object (constructor, destructor, identity), Init with
// its tuning, Shutdown, the save record, DoMessage, and the small slots and
// helpers whose code is below 0x006C4EF0 (foundations of buildings, the lot's
// base texture occupant, altitudes and levelling, the checks of the lot
// update). The other parts are lot_terrain.cpp, lot_build.cpp and
// lot_construct.cpp; their functions are called at the game's addresses (or
// through the object's vtable), so every group works alone.
//
// Fields this file uses (offsets from the object's start; DoMessage gets
// `this` = object + 4, Write and Read object + 8, so their disassembly reads
// 4 and 8 less). Evidence in brackets.
//   +0x0C uint8   initialized [Init sets it last, Shutdown clears it first]
//   +0x10 int32   reference count [Release: signed compare with 1]
//   +0x14 message server 0x00B43CCC; +0x18 building development simulator
//         0x00B43D68; +0x1C city 0x00B43CEC; +0x20 0x00B43D00 (the city's
//         slot 31, vtable 0x00A88004; its slot 9 levels terrain); +0x24 lot
//         manager 0x00B43D08; +0x28 occupant manager 0x00B43D0C; +0x2C
//         0x00B43D94; +0x30 prop manager 0x00B43D10; +0x34 0x00B43DA0; +0x38
//         0x00B43D80; +0x3C terrain 0x00B43CF4; +0x54 0x00B43DA8 [Init takes
//         each from that global and references it; Shutdown releases them]
//   +0x40 uint32  terrain slot 7 + 1 [Init; GetTerrainAltitudeByVertex
//         multiplies z by it; saved]
//   +0x44, +0x48 float: terrain slot 7 (8) count * slot 6 size [Init; saved]
//   +0x4C, +0x50 int32: the city's slots 94 and 93 [Init]
//   +0x58 occupant filter, 0xC bytes [Init: operator new, 0x006C3C00]
//   +0x5C uint8   force empty lot base textures [Init flips it, then calls
//         slot 47 with the old value; slot 46 (0x006AAF40) returns it]
//   +0x5D, +0x5E uint8 [constructor 1; message 0x99EF1143/2 with data 3 =
//         0xC772BF98 sets both; 0x006C4910 sets +0x5D]
//   +0x5F uint8   [message 0x69247DC7: the demand's value above 0]
//   +0x60 uint32  [message 0x26D31EC4: the simulator's slot 8 + 31]
//   +0x64 std::deque of building occupants (begin iterator +0x64, end +0x74,
//         map +0x84, +0x88; 32 pointers to a block of 0x80 bytes) [Write,
//         Read (references of interface 0x87DDA3A9), Shutdown releases each]
//   +0x8C deque iterator: the next building of the night step [constructor
//         and message 0x44778234 set it to begin; saved as an index]
//   +0x9C uint32  night step state 0/1/2 [message 0x44778234; message
//         0x86AD10EE does nothing while 0; saved as a byte]
//   +0xA0 uint32  8 [property 0xE900EA2C]; +0xA4 uint32 200 [property
//         0xE900EA2B loads into it; Read clears it]; +0xA8 uint8 [saved]
//   +0xAC uint32  lot update state [constructor 3, Init 0]; +0xB0 the lot
//         iterator of the update [Shutdown takes it out of its list
//         (0x00809580) and deletes it]; +0xBC cell iterator, 0x20 bytes
//         [Init: operator new, 0x0060CE90, rectangle city cells / 4 - 1]
//   +0xC0 uint16 90; +0xC4, +0xC8, +0xCC float 0.6, 0.4, 0.2; +0xD0, +0xD4
//         float 1; +0xD8, +0xDC uint32 0x890B7312; +0xE0, +0xE1 bool true;
//         +0xE4, +0xE8 float 0 [constructor; the tuning load 0x006C4A20]
//   +0xEC SC4Curve (16 bytes) [0x007D0410; property 0x2A2D7824]
//   +0xFC red-black tree head (0x1C-byte nodes), +0x100 its size
//         [constructor; Shutdown and the destructor erase it]
#include "core/lot_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace opensc4::lot_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("lot: first %s", name);
}

namespace {

constexpr uint32_t kClassId = 0xA990BFE0;
constexpr uint32_t kSimulatorType = 0x6818E5C9;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidLotDeveloper = 0x4818E5D2;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidLot = 0xE818FCED;
constexpr uint32_t kIidBuilding = 0x87DDA3A9;
constexpr uint32_t kIidBaseTexture = 0x097F6D98;
constexpr uint32_t kClassBaseTexture = 0xC97F987C;
constexpr uint32_t kTypeBaseTexture = 0x497F6D9D;
constexpr uint32_t kTypeBuilding = 0x278128A0;
constexpr uint32_t kPropertyFoundation = 0x88FCD877;
constexpr uint32_t kPropertyE999B11F = 0xE999B11F;

// The notifications, in Init's order and in Shutdown's.
constexpr uint32_t kAdded[9] = {0x44778234, 0x99EF1142, 0x99EF1143, 0xE8BDA2E7, 0xE99B3D4D,
                                0x278E6003, 0x8A07AE00, 0x26D31EC4, 0x69247DC7};
constexpr uint32_t kRemoved[9] = {0xE8BDA2E7, 0xE99B3D4D, 0x99EF1143, 0x99EF1142, 0x44778234,
                                  0x278E6003, 0x8A07AE00, 0x26D31EC4, 0x69247DC7};

// A deque iterator of the game's STL (16 bytes).
struct DequeIterator {
    char* cur;
    char* first;
    char* last;
    char** node;
};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using DequeCtorFn = void*(__thiscall*)(void* deque, uint32_t count);
using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using LoadFn = uint8_t(__thiscall*)(void* curve, void* property);
using RefFn = uint8_t(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using WriteBoolFn = void*(__cdecl*)(void* stream, uint32_t value);
using ReadBoolFn = uint8_t(__cdecl*)(void* stream, void* out);
using GetterFn = void*(__cdecl*)();
using TextFn = void*(__thiscall*)(void* string, const char* text);
using RectFn = void(__thiscall*)(void* iterator, const int32_t* rect);
using RemoveFn = void(__thiscall*)(void* list, void* const* value);
using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t count);
using BetweenFn = uint32_t(__thiscall*)(void* random, uint32_t low, uint32_t high);
using GaussFn = double(__thiscall*)(void* random, double low, double high);
using SeedFn = void*(__thiscall*)(void* random, uint32_t seed);
using LotFn = uint32_t(__cdecl*)(void* lot);
using ShuffleFn = void(__cdecl*)(DequeIterator first, DequeIterator last);
using ResizeFn = void(__thiscall*)(void* deque, uint32_t count, void* const* value);
using Part0Fn = void(__thiscall*)(char* self);
using Part1Fn = void(__thiscall*)(char* self, uint32_t a);
using PartPtrFn = void(__thiscall*)(char* self, void* a);
using Part3Fn = void(__thiscall*)(char* self, void* lot, void* configuration, uint32_t flag);
using UpdateFn = uint8_t(__thiscall*)(char* self);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto GetUint16 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint16_005FD420);
const auto GetUint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32);
const auto GetFloat = reinterpret_cast<GetFn>(addr::SC4_GetPropertyFloat);
const auto GetBool = reinterpret_cast<GetFn>(addr::SC4_GetPropertyBool);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);
const auto Below = reinterpret_cast<BelowFn>(addr::cRZRandom_Below);

// Functions of the other parts of the class, at the game's addresses (their
// groups detour or replace them there).
const auto ShuffleBuildings = reinterpret_cast<ShuffleFn>(addr::cSC4LotDeveloper_Fn_006C68C0);
const auto ResizeBuildings = reinterpret_cast<ResizeFn>(addr::cSC4LotDeveloper_Fn_006CCE30);
const auto InitTail = reinterpret_cast<Part0Fn>(addr::cSC4LotDeveloper_Fn_006CA160);
const auto FinishNightBuildings = reinterpret_cast<Part1Fn>(addr::cSC4LotDeveloper_FinishNightBuildings);
const auto StepNightBuildings = reinterpret_cast<Part1Fn>(addr::cSC4LotDeveloper_StepNightBuildings);
const auto BuildingAdded = reinterpret_cast<PartPtrFn>(addr::cSC4LotDeveloper_Fn_006C8F80);
const auto BuildingRemoved = reinterpret_cast<PartPtrFn>(addr::cSC4LotDeveloper_Fn_006C8830);
const auto UpdatePowerInRect = reinterpret_cast<PartPtrFn>(addr::cSC4LotDeveloper_UpdatePowerInRect);
const auto StepLotUpdate = reinterpret_cast<UpdateFn>(addr::cSC4LotDeveloper_StepLotUpdate);
const auto LotChanged7860 = reinterpret_cast<Part3Fn>(addr::cSC4LotDeveloper_Fn_006C7860);
const auto PlantLotFlora = reinterpret_cast<Part3Fn>(addr::cSC4LotDeveloper_PlantLotFlora);
const auto LotChanged9C80 = reinterpret_cast<Part3Fn>(addr::cSC4LotDeveloper_Fn_006C9C80);

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// --- The game's red-black tree at +0xFC (0x1C-byte nodes that own nothing) ---

// 0x006C6080: the right subtrees first, then the node, down the left spine.
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        Free(node);
        node = left;
    }
}

// The inline clear of Shutdown and the destructor.
void ClearTree(char* tree)
{
    if (!At<uint32_t>(tree, 0x4))
        return;
    EraseNodes(At<char*>(At<char*>(tree, 0x0), 0x4));
    char* const head = At<char*>(tree, 0x0);
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0xC) = head;
    At<uint32_t>(tree, 0x4) = 0;
}

// --- The deque of buildings at +0x64 -------------------------------------------

DequeIterator& Begin(char* self) { return At<DequeIterator>(self, field::kBuildings); }
DequeIterator& End(char* self) { return At<DequeIterator>(self, field::kBuildings + 0x10); }

// The distance from the deque's begin to an iterator, as Write computes it.
int32_t IndexOf(const DequeIterator& begin, const DequeIterator& it)
{
    return ((static_cast<int32_t>(reinterpret_cast<char*>(it.node) - reinterpret_cast<char*>(begin.node)) >> 2) - 1) *
               32 +
           (static_cast<int32_t>(it.cur - it.first) >> 2) + (static_cast<int32_t>(begin.last - begin.cur) >> 2);
}

// The next element: into the next block at a block's end.
void Next(DequeIterator& it)
{
    it.cur += 4;
    if (it.cur == it.last) {
        ++it.node;
        it.first = *it.node;
        it.last = it.first + 0x80;
        it.cur = it.first;
    }
}

// 0x006C6010: iterator += n.
void Advance(DequeIterator& it, int32_t n)
{
    const int32_t offset = (static_cast<int32_t>(it.cur - it.first) >> 2) + n;
    if (offset >= 0 && offset < 32) {
        it.cur += static_cast<std::ptrdiff_t>(n) * 4;
        return;
    }
    const int32_t blocks = offset > 0 ? offset / 32 : (offset + 1) / 32 - 1;
    it.node += blocks;
    it.first = *it.node;
    it.last = it.first + 0x80;
    it.cur = it.first + static_cast<std::ptrdiff_t>(offset - blocks * 32) * 4;
}

// --- The tuning -----------------------------------------------------------------

// 0x006C4A20: the tuning exemplar's properties; a property the exemplar
// lacks leaves its field (but the three floats +0xC4 to +0xCC are reset
// first). Nothing when the exemplar is not found.
void LoadTuning(char* self)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xE8DA7677};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<void*>(key), 0xA52160F5u,
                          &exemplar, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    GetUint16(holder, 0x47E2C400, self + 0xC0);
    At<float>(self, 0xCC) = 0.2f;
    At<float>(self, 0xC8) = 0.4f;
    At<float>(self, 0xC4) = 0.6f;
    if (void* const property = Virtual<void*>(holder, 0x24, 0x47E2C401u)) {
        void* const variant = Virtual<void*>(property, 0x14);
        const float* const values = Virtual<const float*>(variant, 0x1BC);
        At<float>(self, 0xCC) = values[0];
        At<float>(self, 0xC8) = values[1];
        At<float>(self, 0xC4) = values[2];
    }
    GetFloat(holder, 0xE900EA24, self + 0xD0);
    GetFloat(holder, 0xE900EA25, self + 0xD4);
    GetUint32(holder, 0xE900EA26, self + 0xD8);
    GetUint32(holder, 0x6916516B, self + 0xDC);
    GetBool(holder, 0xE900EA27, self + 0xE0);
    GetBool(holder, 0xE900EA28, self + 0xE1);
    GetFloat(holder, 0xE900EA29, self + 0xE4);
    GetFloat(holder, 0xE900EA2A, self + 0xE8);
    // The game loads this property into the milliseconds counter of the
    // night step (+0xA4), whose limit is the constant 200.
    GetUint32(holder, 0xE900EA2B, self + 0xA4);
    GetUint32(holder, 0xE900EA2C, self + 0xA0);
    if (void* const property = Virtual<void*>(holder, 0x24, 0x2A2D7824u))
        reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromProperty)(self + 0xEC, property);
    Virtual<uint32_t>(exemplar, 0x8);
}

// --- Init -----------------------------------------------------------------------

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A service into `offset` when there is none: stored even when null, then
// referenced; false when null.
bool Take(char* self, std::size_t offset, void* service)
{
    if (At<void*>(self, offset))
        return true;
    At<void*>(self, offset) = service;
    if (!service)
        return false;
    Virtual<uint32_t>(service, 0x4);
    return true;
}

// 0x006C3C00: the occupant filter kept at +0x58.
void* ConstructFilter(void* filter)
{
    reinterpret_cast<SelfFn>(addr::SC4_OccupantFilterBase)(filter);
    At<std::uintptr_t>(filter, 0x0) = addr::cSC4LotDeveloper_FilterVftable;
    At<std::uintptr_t>(filter, 0x4) = addr::cSC4LotDeveloper_FilterVftable_4;
    return filter;
}

// Slot 4 of the filter's vtable (0x006C3C20).
uint32_t __fastcall FilterTest(void*, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("FilterTest", logged);
    return type == 0x74758926 ? 1 : 0;
}

// Slot 3 (0x006CB9A0). False (initialized left 0) when a service is
// missing; true once done or when already done.
uint32_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    uint8_t& once = Global<uint8_t>(addr::cSC4LotDeveloper_InitOnce);
    if (!once)
        once = 1;
    LoadTuning(self);
    if (!Take(self, field::kCity, Global<void*>(addr::SC4_City)))
        return 0;
    At<int32_t>(self, field::kCellsX) = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174);
    At<int32_t>(self, field::kCellsZ) = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178);
    if (!Take(self, field::kLots, Global<void*>(addr::SC4_LotManager)))
        return 0;
    if (!Take(self, field::kOccupants, Global<void*>(addr::SC4_NetworkService)))
        return 0;
    if (!Take(self, field::kProps, Global<void*>(addr::SC4_Service43D10)))
        return 0;
    if (!Take(self, field::kDevelopment, Global<void*>(addr::SC4_Service43D68)))
        return 0;
    if (!Take(self, field::kPower, Global<void*>(addr::SC4_Service43DA0)))
        return 0;
    if (!Take(self, field::kService2C, Global<void*>(addr::SC4_Service43D94)))
        return 0;
    if (!Take(self, field::kService38, Global<void*>(addr::SC4_Service43D80)))
        return 0;
    if (!At<void*>(self, field::kTerrain)) {
        if (!Take(self, field::kTerrain, Global<void*>(addr::SC4_Terrain)))
            return 0;
        void* terrain = At<void*>(self, field::kTerrain);
        At<int32_t>(self, field::kVerticesX) = Virtual<int32_t>(terrain, 0x1C) + 1;
        // x87: the count as a float (fild, fstp m32), times the cell size
        // the terrain's slot 6 returns in ST0, stored as a float.
        float count = static_cast<float>(Virtual<int32_t>(terrain, 0x1C));
        At<float>(self, field::kSizeX) =
            static_cast<float>(Virtual<double>(terrain, 0x18) * static_cast<double>(count));
        terrain = At<void*>(self, field::kTerrain);
        count = static_cast<float>(Virtual<int32_t>(terrain, 0x20));
        At<float>(self, field::kSizeZ) =
            static_cast<float>(Virtual<double>(terrain, 0x18) * static_cast<double>(count));
    }
    if (!Take(self, field::kTraffic, Global<void*>(addr::SC4_Service43DA8)))
        return 0;
    if (!At<void*>(self, field::kService20)) {
        // No null check here, as the game.
        void* const service = Global<void*>(addr::SC4_Service43D00);
        At<void*>(self, field::kService20) = service;
        Virtual<uint32_t>(service, 0x4);
    }
    if (!At<void*>(self, field::kFilter)) {
        void* filter = OperatorNew(0xC);
        if (filter)
            filter = ConstructFilter(filter);
        At<void*>(self, field::kFilter) = filter;
        Virtual<uint32_t>(filter, 0x4);
    }
    At<uint32_t>(self, field::kUpdateState) = 0;
    if (!At<void*>(self, field::kCellIterator)) {
        void* iterator = OperatorNew(0x20);
        if (iterator)
            iterator = reinterpret_cast<SelfFn>(addr::SC4CellIterator_Construct)(iterator);
        At<void*>(self, field::kCellIterator) = iterator;
    }
    // The rectangle of the cell iterator: the city's cells in fours.
    int32_t rect[4] = {0, 0, 0, 0};
    const int32_t last = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178) / 4 - 1;
    rect[2] = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174) / 4 - 1;
    rect[3] = last;
    reinterpret_cast<RectFn>(addr::SC4CellIterator_SetRect)(At<void*>(self, field::kCellIterator), rect);
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    if (!simulators)
        return 0;
    GameString name;
    reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                       reinterpret_cast<const char*>(addr::cSC4LotDeveloper_Name));
    void* const target = self + 0x4;
    Virtual<uint32_t>(simulators, 0x4C, target, 0u, &name, 2u);
    Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
    Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 2u);
    if (!At<void*>(self, field::kMessages)) {
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, field::kMessages) = messages;
        if (!messages) {
            reinterpret_cast<SelfFn>(addr::SC4String_Destroy)(&name);
            return 0;
        }
        Virtual<uint32_t>(messages, 0x4);
        for (const uint32_t message : kAdded)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, message);
    }
    void* const timers = reinterpret_cast<GetterFn>(addr::SC4_GetService86AD10EC)();
    Virtual<uint8_t>(timers, 0x14, target, 0xC0u);
    InitTail(self);
    // The flag is flipped and the setter (slot 47) called with the old
    // value, so that it sees a change and does its work.
    const uint8_t flipped = At<uint8_t>(self, field::kForceEmptyTextures) == 0;
    At<uint8_t>(self, field::kForceEmptyTextures) = flipped;
    Virtual<uint32_t>(self, 0xBC, static_cast<uint32_t>(flipped == 0));
    At<uint8_t>(self, field::kDemand5F) = 0;
    At<uint32_t>(self, field::kDate60) = 0;
    At<uint8_t>(self, field::kInitialized) = 1;
    reinterpret_cast<SelfFn>(addr::SC4String_Destroy)(&name);
    return 1;
}

// --- Shutdown -------------------------------------------------------------------

void Release(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// Slot 4 (0x006C7380). Always true.
uint32_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    DequeIterator it = Begin(self);
    const DequeIterator end = End(self);
    At<uint8_t>(self, field::kInitialized) = 0;
    while (it.cur != end.cur) {
        Virtual<uint32_t>(*reinterpret_cast<void**>(it.cur), 0x8);
        Next(it);
    }
    reinterpret_cast<SelfFn>(addr::SC4CellDeque_Clear)(self + field::kBuildings);
    void* const target = self + 0x4;
    if (void* const simulators = Virtual<void*>(At<void*>(self, field::kCity), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, target);
    // The timer service, through the framework (referenced; released at
    // the end, where the game's local is destroyed).
    void* timers = nullptr;
    if (void* const framework = reinterpret_cast<GetterFn>(addr::RZGetFramework)())
        Virtual<uint8_t>(framework, 0x14, 0x86AD10ECu, 0x86AD10EDu, &timers);
    if (timers)
        Virtual<uint8_t>(timers, 0x18, target, 0xC0u);
    if (At<void*>(self, field::kMessages)) {
        for (const uint32_t message : kRemoved)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, message);
        Virtual<uint32_t>(At<void*>(self, field::kMessages), 0x8);
        At<void*>(self, field::kMessages) = nullptr;
    }
    for (const std::size_t offset : {field::kCity, field::kLots, field::kOccupants, field::kProps, field::kPower,
                                     field::kService2C, field::kService38, field::kTerrain, field::kTraffic,
                                     field::kService20, field::kFilter})
        Release(self, offset);
    ClearTree(self + field::kTree);
    if (char* const iterator = At<char*>(self, field::kLotIterator)) {
        // The iterator taken out of its owner's list of iterators.
        if (char* const owner = At<char*>(iterator, 0x4)) {
            void* const value = iterator;
            reinterpret_cast<RemoveFn>(addr::SC4ListIterator_FreeList)(owner + 0x4, &value);
        }
        OperatorDelete(iterator);
        At<void*>(self, field::kLotIterator) = nullptr;
    }
    if (void* const iterator = At<void*>(self, field::kCellIterator)) {
        OperatorDelete(iterator);
        At<void*>(self, field::kCellIterator) = nullptr;
    }
    Release(self, field::kDevelopment);
    if (timers)
        Virtual<uint32_t>(timers, 0x8);
    return 1;
}

// --- Save -----------------------------------------------------------------------

// cIGZSerializable slot 3 (0x006C66F0), on the part at +0x8: version 1.
// False when the stream is in error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x8;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint32_t>(1));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kVerticesX));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kSizeX));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kSizeZ));
    Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>(IndexOf(Begin(self), End(self))));
    DequeIterator it = Begin(self);
    const DequeIterator end = End(self);
    while (it.cur != end.cur) {
        WriteRef(stream, *reinterpret_cast<void**>(it.cur));
        Next(it);
    }
    Virtual<uint8_t>(stream, 0x24,
                     static_cast<uint32_t>(IndexOf(Begin(self), At<DequeIterator>(self, field::kNextBuilding))));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kNightState)));
    reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool)(stream,
                                                             static_cast<uint32_t>(At<uint8_t>(self, field::kFlagA8)));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x006CD120): version 1 only (false otherwise,
// nothing else read). False when the stream is in error after.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - 0x8;
    // The game's locals for the version and the count are unset stack
    // memory when a read stores nothing; zeros here.
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    Virtual<uint8_t>(stream, 0x20, self + field::kVerticesX);
    Virtual<uint8_t>(stream, 0x30, self + field::kSizeX);
    Virtual<uint8_t>(stream, 0x30, self + field::kSizeZ);
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    // One variable (the game's argument slot) is the fill value of the
    // resize, then the index, then, in its low byte, the state.
    uint32_t slot = 0;
    ResizeBuildings(self + field::kBuildings, count, reinterpret_cast<void* const*>(&slot));
    DequeIterator it = Begin(self);
    const DequeIterator end = End(self);
    while (it.cur != end.cur) {
        ReadRef(stream, kIidBuilding, it.cur, 0u);
        Next(it);
    }
    Virtual<uint8_t>(stream, 0x24, &slot);
    DequeIterator next = Begin(self);
    Advance(next, static_cast<int32_t>(slot));
    At<DequeIterator>(self, field::kNextBuilding) = next;
    Virtual<uint8_t>(stream, 0x14, &slot);
    At<uint32_t>(self, field::kNightState) = static_cast<uint8_t>(slot);
    reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool)(stream, self + field::kFlagA8);
    At<uint32_t>(self, field::kStepMs) = 0;
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- DoMessage ------------------------------------------------------------------

// The message 0xA6B79621 posted when the night step is done at once.
void PostNightDone(char* self)
{
    void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    if (message)
        message = reinterpret_cast<SelfFn>(addr::cGZMessage_cGZMessage)(message);
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint8_t>(message, 0x14, 0xA6B79621u);
    Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// cIGZMessageTarget2 slot 3 (0x006CD940), on the part at +0x4. True but for
// the timer message while the night step is idle.
uint32_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x4;
    const uint32_t id = Virtual<uint32_t>(message, 0x10);
    switch (id) {
    case 0x44778234: {
        // Day (data 2 = 0: state 2) or night (1: state 1). When the clock
        // (slot 6) already says so, every building at once and a message;
        // else the buildings are shuffled and stepped by the timer.
        const uint32_t data = Virtual<uint32_t>(message, 0x38);
        if (data > 1)
            return 1;
        const uint32_t state = data == 0 ? 2 : 1;
        if (Virtual<uint32_t>(Global<void*>(addr::SC4_Clock43DBC), 0x18) == state) {
            FinishNightBuildings(self, state);
            PostNightDone(self);
            return 1;
        }
        At<uint32_t>(self, field::kNightState) = state;
        const DequeIterator end = End(self);
        const DequeIterator begin = Begin(self);
        ShuffleBuildings(begin, end);
        At<DequeIterator>(self, field::kNextBuilding) = Begin(self);
        return 1;
    }
    case 0x278E6003:
        // The getters are called and nothing is done with them.
        Virtual<uint32_t>(message, 0x24);
        Virtual<uint32_t>(message, 0x38);
        Virtual<uint32_t>(message, 0x60);
        return 1;
    case 0x26D31EC4:
        At<uint32_t>(self, field::kDate60) = Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x20) + 0x1F;
        return 1;
    case 0x66956814:
        return StepLotUpdate(self);
    case 0x69247DC7: {
        void* const demand = Virtual<void*>(Global<void*>(addr::SC4_Service43D74), 0x18, 0x2000u, 0x20000u);
        At<uint8_t>(self, field::kDemand5F) =
            Virtual<double>(demand, 0x24) > static_cast<double>(Global<float>(addr::SC4_FloatZero)) ? 1 : 0;
        return 1;
    }
    case 0x86AD10EE:
        if (!At<uint32_t>(self, field::kNightState))
            return 0;
        StepNightBuildings(self, Virtual<uint32_t>(message, 0x38) / 1000);
        return 1;
    case 0x8A07AE00: {
        uint32_t data[4];
        data[3] = Virtual<uint32_t>(message, 0x60);
        data[2] = Virtual<uint32_t>(message, 0x4C);
        data[1] = Virtual<uint32_t>(message, 0x38);
        data[0] = Virtual<uint32_t>(message, 0x24);
        UpdatePowerInRect(self, data);
        return 1;
    }
    case 0x99EF1142:
    case 0x99EF1143: {
        const bool added = id == 0x99EF1142;
        const uint32_t type = Virtual<uint32_t>(message, 0x4C);
        if (type == kTypeBuilding) {
            void* const occupant = Virtual<void*>(message, 0x28);
            (added ? BuildingAdded : BuildingRemoved)(self, occupant);
        } else if (type == 0xC772BF98) {
            At<uint8_t>(self, field::kDirty5D) = 1;
            At<uint8_t>(self, field::kDirty5E) = 1;
        }
        return 1;
    }
    case 0xE99B3D4D: {
        // A lot's rectangle changed: the base texture of the lot at its
        // cell again when it is in state 3 or 4.
        void* changed = message; // the game's argument slot
        void* const unknown = Virtual<void*>(message, 0x80);
        if (!Virtual<uint8_t>(unknown, 0x0, kIidLot, &changed))
            return 1;
        int32_t x = 0, z = 0; // unset stack memory in the game
        Virtual<uint8_t>(changed, 0x54, &x, &z);
        if (void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x18, x, z, 1u)) {
            const uint32_t state = Virtual<uint32_t>(lot, 0x84);
            if (state == 3 || state == 4) {
                void* const configuration = Virtual<void*>(lot, 0x24);
                Virtual<uint32_t>(self, 0xB0, lot, configuration, 1u);
            }
        }
        Virtual<uint32_t>(changed, 0x8);
        return 1;
    }
    case 0xE8BDA2E7: {
        // A lot's state changed (data 1 = old, data 2 = new).
        const uint32_t before = Virtual<uint32_t>(message, 0x24);
        const uint32_t after = Virtual<uint32_t>(message, 0x38);
        const bool occupied = after == 3 || after == 4;
        const bool built = before == 2 && after == 3;
        if (!occupied && !built)
            return 1;
        void* changed = nullptr;
        void* const unknown = Virtual<void*>(message, 0x80);
        if (!Virtual<uint8_t>(unknown, 0x0, kIidLot, &changed))
            return 1;
        int32_t x = 0, z = 0;                 // unset stack memory in the game,
        uint8_t sizeX = 0, sizeZ = 0;         // as are the four size bytes
        uint8_t lotSizeX = 0, lotSizeZ = 0;
        Virtual<uint8_t>(changed, 0x54, &x, &z);
        Virtual<uint8_t>(changed, 0x58, &sizeX, &sizeZ);
        if (void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x18, x, z, 1u)) {
            if (void* const configuration = Virtual<void*>(lot, 0x24)) {
                Virtual<uint8_t>(lot, 0x58, &lotSizeX, &lotSizeZ);
                if (sizeX == lotSizeX && sizeZ == lotSizeZ) {
                    if (occupied)
                        Virtual<uint32_t>(self, 0xB0, lot, configuration, 1u);
                    if (built) {
                        LotChanged7860(self, lot, configuration, 1);
                        PlantLotFlora(self, lot, configuration, 1);
                        LotChanged9C80(self, lot, configuration, 1);
                    }
                }
            }
        }
        Virtual<uint32_t>(changed, 0x8);
        return 1;
    }
    default:
        return 1;
    }
}

// --- The small slots --------------------------------------------------------------

// Slot 5 (0x006C3C40).
uint32_t __fastcall GetSimulatorType(char*, void*)
{
    static bool logged = false;
    FirstCall("GetSimulatorType", logged);
    return kSimulatorType;
}

// 0x006C3B10, arguments in registers (ECX the lot, EDX and EAX the cell):
// whether a lot of this zone type shows something at the cell: (a + b) % 4
// == 0 for zone types 1 to 6, both % 4 == 0 for 7 to 9, never otherwise.
uint32_t __stdcall ZotCell(void* lot, int32_t a, int32_t b)
{
    static bool logged = false;
    FirstCall("Fn_006C3B10", logged);
    const uint32_t zone = Virtual<uint32_t>(lot, 0x7C) - 1;
    if (zone > 8)
        return 0;
    if (zone < 6)
        return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)) % 4 == 0;
    return a % 4 == 0 && b % 4 == 0;
}

// The game's callers load EAX and EDX and read only AL after; the game's
// code keeps EBX, ESI and EDI, as ours does.
__declspec(naked) void ZotCellThunk()
{
    __asm push eax
    __asm push edx
    __asm push ecx
    __asm call ZotCell
    __asm ret
}

// 0x006C3C50: the lot's state set, each of its buildings taken out (with
// its foundation when the flag is set), then slot 11. The game ends in the
// slot 11 call and leaves its EAX: StartDemolishLot (0x006CC242, the
// construct part) returns that value, so this returns it too.
uint32_t __fastcall ClearLotBuildings(char* self, void*, void* lot, uint32_t state, uint32_t removeFoundations,
                                      uint32_t removal)
{
    static bool logged = false;
    FirstCall("Fn_006C3C50", logged);
    void* building = Virtual<void*>(self, 0x98, lot);
    Virtual<uint8_t>(lot, 0x88, state, 0u, 0u);
    while (building) {
        Virtual<uint32_t>(building, 0x4);
        void* const occupant = Virtual<void*>(building, 0xC);
        Virtual<uint8_t>(At<void*>(self, field::kProps), 0x98, occupant);
        if (static_cast<uint8_t>(removeFoundations))
            Virtual<uint32_t>(self, 0xF0, occupant);
        Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, removal);
        Virtual<uint32_t>(building, 0x8);
        building = Virtual<void*>(self, 0x98, lot);
    }
    return Virtual<uint32_t>(self, 0x2C, lot);
}

// Slot 40 (0x006C3CF0): slot 39 for the lot's configuration and the object
// its slot 24 gives; 0 without either.
void* __fastcall GetPositionForBuilding(char* self, void*, void* lot, void* position, void* facing)
{
    static bool logged = false;
    FirstCall("GetPositionForBuilding", logged);
    void* const configuration = Virtual<void*>(lot, 0x24);
    if (!configuration)
        return nullptr;
    void* const object = Virtual<void*>(configuration, 0x60);
    if (!object)
        return nullptr;
    return Virtual<void*>(self, 0x9C, lot, configuration, object, position, facing);
}

// Slot 8 (0x006C3D30).
uint32_t __fastcall AbandonBuilding(char*, void*, void* lot, uint32_t reason)
{
    static bool logged = false;
    FirstCall("AbandonBuilding", logged);
    return Virtual<uint32_t>(lot, 0x88, 4u, 0u, reason);
}

// Whether the lot has the property 0xE999B11F with a first value not 0.
bool HasE999B11F(void* lot)
{
    void* const holder = Virtual<void*>(lot, 0xC);
    void* const property = Virtual<void*>(holder, 0x24, kPropertyE999B11F);
    if (!property)
        return false;
    void* const variant = Virtual<void*>(property, 0x14);
    return *Virtual<const uint32_t*>(variant, 0x124) != 0;
}

// Slot 9 (0x006C3D50): state 3 with the developer's wealth; the condition 2
// or 3 when the building's wealth (its profile's +4) is one or two above,
// else 1 with the property, else 0; the population to the capacity.
uint32_t __fastcall ReoccupyBuilding(char* self, void*, void* lot, uint32_t developer)
{
    static bool logged = false;
    FirstCall("ReoccupyBuilding", logged);
    const uint32_t type = Virtual<uint32_t>(lot, 0xB8, 0u);
    char* const profile = Virtual<char*>(At<void*>(self, field::kDevelopment), 0x9C, type);
    const int32_t wealth = Virtual<int32_t>(At<void*>(self, field::kDevelopment), 0x7C, developer);
    const int32_t above = At<int32_t>(profile, 0x4) - wealth;
    uint32_t condition = HasE999B11F(lot) ? 1 : 0;
    if (above == 1)
        condition = 2;
    else if (above == 2)
        condition = 3;
    Virtual<uint8_t>(lot, 0x88, 3u, wealth, 0u);
    Virtual<uint8_t>(lot, 0x90, condition);
    const uint32_t capacity = Virtual<uint32_t>(lot, 0xD0, developer, 0u);
    return Virtual<uint32_t>(lot, 0xDC, developer, capacity);
}

// Slot 48 (0x006C3E10): slot 50 at the occupant's position with a kind
// from its type.
void* __fastcall GetFoundationByOccupant(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("GetFoundationByOccupant", logged);
    float position[3] = {0.0f, 0.0f, 0.0f}; // unset stack memory in the game
    Virtual<uint8_t>(occupant, 0x20, position);
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    const uint32_t kind = type == kTypeBuilding ? 1 : type == 0xA823821E ? 3 : 4;
    return Virtual<void*>(self, 0xC8, kind, position[0], position[2]);
}

// Slot 51 (0x006C3E70): slot 49 (2, the lot's cell). The game's x is the
// slot it pushed ECX into and its z the argument's slot, so a lot that
// does not answer leaves `this` and the lot there.
void* __fastcall GetLotFoundation(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("GetLotFoundation", logged);
    int32_t x = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(self));
    int32_t z = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(lot));
    Virtual<uint8_t>(lot, 0x54, &x, &z);
    return Virtual<void*>(self, 0xC4, 2u, x, z);
}

// Clears the flags 1 and 2 of a lot.
void ClearFoundationFlags(void* lot)
{
    const uint32_t flags = Virtual<uint32_t>(lot, 0x30);
    Virtual<uint8_t>(lot, 0x34, flags & ~3u);
}

// Slot 53 (0x006C3EB0): the lot foundation at the cell removed; whether
// there was one.
uint32_t __fastcall RemoveLotFoundation(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("RemoveLotFoundation", logged);
    uint32_t removed = 0;
    if (void* const foundation = Virtual<void*>(self, 0xC4, 2u, x, z)) {
        void* const occupant = Virtual<void*>(foundation, 0xC);
        Virtual<uint32_t>(occupant, 0x4);
        Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, 0u);
        Virtual<uint32_t>(occupant, 0x8);
        removed = 1;
    }
    if (void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x18, x, z, 0u))
        ClearFoundationFlags(lot);
    return removed;
}

// 0x006C3F30: slot 53 at the lot's cell, then the lot's flags cleared.
uint32_t RemoveFoundationOfLot(char* self, void* lot)
{
    int32_t x = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(self));
    int32_t z = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(lot));
    Virtual<uint8_t>(lot, 0x54, &x, &z);
    const uint8_t removed = Virtual<uint8_t>(self, 0xD4, x, z);
    ClearFoundationFlags(lot);
    return removed;
}

// Slot 61 (0x006C3F80): nothing for a lot without the flags 1 and 2; else
// its foundation again: with the old one's type (slot 55), or from the
// configuration (slot 54).
uint32_t __fastcall UpdateLotFoundation(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("UpdateLotFoundation", logged);
    const uint32_t flags = Virtual<uint32_t>(lot, 0x30);
    if (!(flags & 3))
        return flags;
    if (void* const foundation = Virtual<void*>(self, 0xCC, lot)) {
        const uint32_t type = Virtual<uint32_t>(foundation, 0x10);
        RemoveFoundationOfLot(self, lot);
        return Virtual<uint32_t>(self, 0xDC, lot, type, 1u, 0u, 0u);
    }
    void* const configuration = Virtual<void*>(lot, 0x24);
    return Virtual<uint32_t>(self, 0xD8, lot, configuration, 1u, 0u, 0u);
}

// Slot 56 (0x006C3FF0): slot 31 (occupant, 1), in ST0, above +0xD4.
uint32_t __fastcall IsLotObjectFoundationRequired(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("IsLotObjectFoundationRequired", logged);
    return Virtual<double>(self, 0x7C, occupant, 1u) > static_cast<double>(At<float>(self, field::kFoundationHeight))
               ? 1
               : 0;
}

// Slot 59 (0x006C4020): a foundation occupant under the building, from the
// building development simulator (slot 23): the type (the building's
// property, then +0xDC, when 0), the building's position, the size of its
// bounding box, its orientation, -1.
uint32_t __fastcall AddBuildingFoundation(char* self, void*, void* building, uint32_t type)
{
    static bool logged = false;
    FirstCall("AddBuildingFoundation", logged);
    float position[3] = {0.0f, 0.0f, 0.0f}; // unset stack memory in the game,
    float low[3] = {0.0f, 0.0f, 0.0f};      // as the two corners
    float high[3] = {0.0f, 0.0f, 0.0f};
    Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x20, position);
    Virtual<void*>(Virtual<void*>(building, 0xC), 0x28, low, high);
    const uint32_t orientation = Virtual<uint32_t>(building, 0x38);
    if (!type) {
        void* const holder = Virtual<void*>(Virtual<void*>(building, 0xC), 0x18);
        GetUint32(holder, kPropertyFoundation, &type);
        if (!type)
            type = At<uint32_t>(self, field::kFoundationType);
    }
    // x87: each difference in double, stored as a float.
    const float depth = static_cast<float>(static_cast<double>(high[2]) - static_cast<double>(low[2]));
    const float width = static_cast<float>(static_cast<double>(high[0]) - static_cast<double>(low[0]));
    return Virtual<uint32_t>(At<void*>(self, field::kDevelopment), 0x5C, type, 1u, static_cast<float*>(position),
                             width, depth, orientation, -1.0f);
}

// Slot 58 (0x006C40E0): slot 59 with the type the configuration's object
// names (+0x20), else the building's property; 0 when neither does.
uint32_t __fastcall AddBuildingFoundationByConfiguration(char* self, void*, void* building, void* configuration)
{
    static bool logged = false;
    FirstCall("AddBuildingFoundationByConfiguration", logged);
    uint32_t type = At<uint32_t>(self, field::kFoundationType);
    char* const object = Virtual<char*>(configuration, 0x60);
    if (object && At<uint32_t>(object, 0x20)) {
        type = At<uint32_t>(object, 0x20);
    } else {
        void* const holder = Virtual<void*>(Virtual<void*>(building, 0xC), 0x18);
        GetUint32(holder, kPropertyFoundation, &type);
    }
    if (!type)
        return 0;
    return Virtual<uint32_t>(self, 0xEC, building, type);
}

// Slot 60 (0x006C4150): slot 52 (1, the occupant's x and z).
uint32_t __fastcall RemoveBuildingFoundation(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("RemoveBuildingFoundation", logged);
    float position[3] = {0.0f, 0.0f, 0.0f}; // unset stack memory in the game
    Virtual<uint8_t>(occupant, 0x20, position);
    return Virtual<uint32_t>(self, 0xD0, 1u, position[0], position[2]);
}

// Slot 62 (0x006C4190).
uint32_t __fastcall UpdateLotRetainingWalls(char*, void*, void* lot)
{
    static bool logged = false;
    FirstCall("UpdateLotRetainingWalls", logged);
    return reinterpret_cast<LotFn>(addr::SC4_UpdateLotRetainingWalls)(lot);
}

// Slot 41 (0x006C41A0): the base texture occupant at the lot's cell into
// *out (usage 2 set), a new one when there is none and `create` is set;
// whether *out is set. As the game: the occupant found is not released
// when it lacks the interface.
uint32_t __fastcall GetLotBaseTextureOccupant(char* self, void*, void* lot, void** out, uint32_t create)
{
    static bool logged = false;
    FirstCall("GetLotBaseTextureOccupant", logged);
    int32_t x = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(self));
    int32_t z = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(lot));
    *out = nullptr;
    Virtual<uint8_t>(lot, 0x54, &x, &z);
    void* occupant = out; // the game's slot of the second argument
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x84, &occupant, x, z, kTypeBaseTexture)) {
        if (Virtual<uint8_t>(occupant, 0x0, kIidBaseTexture, out)) {
            Virtual<uint8_t>(*out, 0x14, 2u);
            Virtual<uint32_t>(occupant, 0x8);
        }
    } else if (static_cast<uint8_t>(create)) {
        void* const com = reinterpret_cast<GetterFn>(addr::RZGetCOM)();
        if (!Virtual<uint8_t>(com, 0xC, kClassBaseTexture, kIidBaseTexture, out)) {
            void* made = reinterpret_cast<NewFn>(addr::cSC4LotBaseTextureOccupant_Allocate)(0x60);
            if (made)
                made = reinterpret_cast<SelfFn>(addr::cSC4LotBaseTextureOccupant_cSC4LotBaseTextureOccupant)(made);
            *out = made;
            Virtual<uint32_t>(made, 0x4);
        }
        Virtual<uint8_t>(Virtual<void*>(*out, 0xC), 0xC);
        Virtual<uint8_t>(*out, 0x14, 2u);
    }
    return *out ? 1 : 0;
}

// Slot 45 (0x006C4290): the lot's base texture occupant's slot 9.
uint32_t __fastcall UpdateLotBaseTextureOccupant(char* self, void*, void* lot, uint32_t arg)
{
    static bool logged = false;
    FirstCall("UpdateLotBaseTextureOccupant", logged);
    void* texture = nullptr;
    const uint32_t found = Virtual<uint32_t>(self, 0xA4, lot, &texture, 0u);
    if (!static_cast<uint8_t>(found))
        return found;
    Virtual<uint8_t>(texture, 0x24, 1u, arg, 0u);
    return Virtual<uint32_t>(texture, 0x8);
}

// 0x006C42D0: the lot taken from the prop manager (slot 28) and its
// property 0xC999C45E, when it has one, set to 0.
uint32_t __fastcall ResetLotC999C45E(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("Fn_006C42D0", logged);
    Virtual<uint8_t>(At<void*>(self, field::kProps), 0x70, lot);
    void* const holder = Virtual<void*>(lot, 0xC);
    void* const property = Virtual<void*>(holder, 0x24, 0xC999C45Eu);
    if (!property)
        return 0;
    return Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x68, 0u);
}

// Slot 15 (0x006C4310), in ST0: the terrain's altitude at a vertex (slot
// 12, rounded to a float); with the flag the terrain's slot 31 value when
// that is higher (kept as returned).
double __fastcall GetTerrainAltitudeByVertex(char* self, void*, int32_t x, int32_t z, uint32_t withWater)
{
    static bool logged = false;
    FirstCall("GetTerrainAltitudeByVertex", logged);
    const int32_t vertex = static_cast<int32_t>(At<uint32_t>(self, field::kVerticesX) * static_cast<uint32_t>(z) +
                                                static_cast<uint32_t>(x));
    const float land = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x30, vertex));
    if (static_cast<uint8_t>(withWater)) {
        const double water = Virtual<double>(At<void*>(self, field::kTerrain), 0x7C, vertex);
        if (water > static_cast<double>(land))
            return water;
    }
    return land;
}

// 0x006C4360: the altitudes of the four corners of a rectangle of cells.
void __fastcall GetCornerAltitudes(char* self, void*, const int32_t* rect, float* corners, uint32_t withWater)
{
    static bool logged = false;
    FirstCall("Fn_006C4360", logged);
    corners[0] = static_cast<float>(Virtual<double>(self, 0x3C, rect[0], rect[1], withWater));
    corners[1] = static_cast<float>(Virtual<double>(self, 0x3C, rect[2] + 1, rect[1], withWater));
    corners[2] = static_cast<float>(Virtual<double>(self, 0x3C, rect[2] + 1, rect[3] + 1, withWater));
    corners[3] = static_cast<float>(Virtual<double>(self, 0x3C, rect[0], rect[3] + 1, withWater));
}

// Slot 19 (0x006C43D0): every inner vertex of the rectangle levelled to
// the altitude (the area's average, slot 18, when below 0), one call of
// 0x00B43D00's slot 9 each. With skipLocked the vertices the terrain's
// slot 37 names are left; without `all` those not more than +0xE4 above
// the altitude too. The number levelled.
int32_t __fastcall LevelAreaInternalVertices(char* self, void*, const int32_t* rect, float altitude,
                                             uint32_t skipLocked, uint32_t all, uint32_t arg)
{
    static bool logged = false;
    FirstCall("LevelAreaInternalVertices", logged);
    int32_t count = 0;
    if (altitude < Global<float>(addr::SC4_FloatZero))
        altitude = static_cast<float>(Virtual<double>(self, 0x48, rect, 1u));
    for (int32_t x = rect[0] + 1; x <= rect[2]; ++x) {
        for (int32_t z = rect[1] + 1; z <= rect[3]; ++z) {
            if (static_cast<uint8_t>(skipLocked) && Virtual<uint8_t>(At<void*>(self, field::kTerrain), 0x94, x, z))
                continue;
            // x87: the altitude as returned minus the float target.
            const double above = Virtual<double>(self, 0x3C, x, z, 1u) - static_cast<double>(altitude);
            if (!static_cast<uint8_t>(all) && !(above > static_cast<double>(At<float>(self, field::kLevelLimit))))
                continue;
            Virtual<uint32_t>(At<void*>(self, field::kService20), 0x24, 1u, 1u, x, z, x, z, Bits(altitude), arg, 5u,
                              0u);
            ++count;
        }
    }
    return count;
}

// Slot 20 (0x006C44B0): the rectangle with its far border in one call.
uint32_t __fastcall LevelBorderingVertices(char* self, void*, const int32_t* rect, uint32_t altitude, uint32_t,
                                           uint32_t arg)
{
    static bool logged = false;
    FirstCall("LevelBorderingVertices", logged);
    return Virtual<uint32_t>(At<void*>(self, field::kService20), 0x24, 1u, 1u, rect[0], rect[1], rect[2] + 1,
                             rect[3] + 1, altitude, arg, 5u, 0u);
}

// Slot 21 (0x006C44F0): the rectangle levelled to its average altitude
// (slot 18), unless the limit (+0xE8 for the "unset" value) is below 1000
// and the area's slope (slot 29) is above it: 0 then.
uint32_t __fastcall AttemptAreaLevelling(char* self, void*, const int32_t* rect, uint32_t, uint32_t flag,
                                         uint32_t mode, float limit, uint32_t arg)
{
    static bool logged = false;
    FirstCall("AttemptAreaLevelling", logged);
    const float average = static_cast<float>(Virtual<double>(self, 0x48, rect, flag));
    if (limit == Global<float>(addr::cSC4LotDeveloper_SlopeLimitUnset))
        limit = At<float>(self, field::kLevelDefault);
    if (limit < Global<float>(addr::SC4_Float1000) &&
        Virtual<double>(self, 0x74, rect, flag) > static_cast<double>(limit))
        return 0;
    return Virtual<uint32_t>(At<void*>(self, field::kService20), 0x24, 1u, mode, rect[0], rect[1], rect[2] + 1,
                             rect[3] + 1, Bits(average), arg, 5u, 0u);
}

// Slot 22 (0x006C4590): the cells of the occupant's bounding box, shrunk by
// 0.05 on each side, into rect (the city's slot 95 per corner).
uint32_t __fastcall GetOccupantCellCoverage(char* self, void*, void* occupant, int32_t* rect)
{
    static bool logged = false;
    FirstCall("GetOccupantCellCoverage", logged);
    float low[3] = {0.0f, 0.0f, 0.0f}; // unset stack memory in the game
    float high[3] = {0.0f, 0.0f, 0.0f};
    Virtual<void*>(occupant, 0x28, low, high);
    // x87: each sum in double, stored as a float.
    const double margin = Global<float>(addr::SC4_FloatTwentieth);
    low[0] = static_cast<float>(static_cast<double>(low[0]) + margin);
    low[2] = static_cast<float>(static_cast<double>(low[2]) + margin);
    high[0] = static_cast<float>(static_cast<double>(high[0]) - margin);
    high[2] = static_cast<float>(static_cast<double>(high[2]) - margin);
    Virtual<uint8_t>(At<void*>(self, field::kCity), 0x17C, low[0], low[2], rect, rect + 1);
    return Virtual<uint32_t>(At<void*>(self, field::kCity), 0x17C, high[0], high[2], rect + 2, rect + 3);
}

// 0x006C4630: the condition a lot should have, for the lot update. State
// 4: one worse (to 4 at most) once it has been in the state for more than
// +0xC0 days. State 3: from how far the building's wealth is above its
// occupants', one more when the occupancy (the lot's slot 61) is low.
uint32_t __fastcall NextCondition(char* self, void*, void* lot, int32_t, int32_t)
{
    static bool logged = false;
    FirstCall("Fn_006C4630", logged);
    const uint32_t state = Virtual<uint32_t>(lot, 0x84);
    uint32_t condition = Virtual<uint32_t>(lot, 0x8C);
    if (state == 4) {
        if (!condition)
            condition = 1;
        if (Virtual<uint32_t>(lot, 0xA0) <= At<uint16_t>(self, field::kTuningC0))
            return condition;
        return condition <= 3 ? condition + 1 : condition;
    }
    if (state != 3)
        return condition;
    const uint32_t type = Virtual<uint32_t>(lot, 0xB8, 0u);
    char* const profile = Virtual<char*>(At<void*>(self, field::kDevelopment), 0x9C, type);
    int32_t above = At<int32_t>(profile, 0x4) - Virtual<int32_t>(lot, 0xA4);
    const bool worse = static_cast<int32_t>(condition) - 1 > above;
    if (worse || Virtual<uint32_t>(lot, 0xA0) > At<uint16_t>(self, field::kTuningC0)) {
        // The game's local is the slot it pushed ECX into: `this` when
        // the lot stores nothing.
        float occupancy;
        std::memcpy(&occupancy, &self, 4);
        Virtual<uint8_t>(lot, 0xF4, &occupancy);
        if (occupancy < At<float>(self, 0xC8) || (worse && occupancy < At<float>(self, 0xC4)))
            ++above;
    }
    if (above > 2)
        return 3;
    if (above == 0)
        return HasE999B11F(lot) ? 1 : 0;
    if (above < 0)
        return condition;
    return static_cast<uint32_t>(above + 1);
}

// 0x006C47C0: counts, in the lot's property 0xC999E206 (added when
// missing), the updates in which the land value at the cell (the
// developer's slot 13 grid, hundredths) was under +0xCC; false once the
// count passes a per-cell random 2 to 8. The count goes back to 0, with
// true, when the lot is not livable, has no developer or the value is not
// under; true without a change for a developer of type 5.
uint32_t __fastcall CheckLandValue(char* self, void*, void* lot, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("Fn_006C47C0", logged);
    void* const holder = Virtual<void*>(lot, 0xC);
    void* property = Virtual<void*>(holder, 0x24, 0xC999E206u);
    if (!property) {
        Virtual<uint8_t>(holder, 0x34, 0xC999E206u, 0u, 0u);
        property = Virtual<void*>(holder, 0x24, 0xC999E206u);
    }
    void* const variant = Virtual<void*>(property, 0x14);
    const uint32_t count = *Virtual<const uint32_t*>(variant, 0x124);
    if (Virtual<uint8_t>(lot, 0x94)) {
        void* const development = At<void*>(self, field::kDevelopment);
        const uint32_t type = Virtual<uint32_t>(lot, 0xB8, 0u);
        char* const profile = Virtual<char*>(development, 0x9C, type);
        const uint32_t wealth = Virtual<uint32_t>(lot, 0xA4);
        const uint32_t developerType = Virtual<uint32_t>(development, 0x78, At<uint32_t>(profile, 0x0), wealth);
        if (void* const developer = Virtual<void*>(development, 0x80, developerType)) {
            if (Virtual<uint32_t>(developer, 0x1C) == 5)
                return 1;
            void* const grid = Virtual<void*>(developer, 0x34);
            const int32_t value = static_cast<int16_t>(Virtual<uint32_t>(grid, 0x1C, x, z));
            // x87: fild, fmul by the float 0.01, compared with +0xCC.
            if (static_cast<double>(value) * static_cast<double>(Global<float>(addr::SC4Traffic_FerryCostScale)) <
                static_cast<double>(At<float>(self, 0xCC))) {
                char random[0x18];
                reinterpret_cast<SeedFn>(addr::SC4Random_Construct)(
                    random, (static_cast<uint32_t>(x) << 16) + static_cast<uint32_t>(z));
                const uint32_t limit = reinterpret_cast<BetweenFn>(addr::cRZRandom_Between)(random, 0, 7);
                Virtual<uint8_t>(variant, 0x68, count + 1);
                return limit + 2 < count + 1 ? 0 : 1;
            }
        }
    }
    Virtual<uint8_t>(variant, 0x68, 0u);
    return 1;
}

// 0x006C4910: a lot without road access is turned to a facing that has it:
// each other facing (only the opposite one unless the lot is square) is
// tried by setting it, updating the road access and setting the old one
// back; the first with access is kept and +0x5D set.
void __fastcall TurnLotToRoad(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("Fn_006C4910", logged);
    const int32_t facing = Virtual<int32_t>(lot, 0x5C);
    // The game's x size is the low byte of its argument slot, its z size
    // unset stack memory.
    uint8_t sizeX = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(lot));
    uint8_t sizeZ = 0;
    Virtual<uint8_t>(lot, 0x58, &sizeX, &sizeZ);
    const bool square = sizeX == sizeZ;
    for (int32_t turn = 1; turn < 4; ++turn) {
        if (!square && turn % 2 != 0)
            continue;
        const int32_t tried = (facing + turn) % 4;
        Virtual<uint8_t>(lot, 0x60, tried);
        Virtual<uint8_t>(lot, 0x78);
        Virtual<uint8_t>(lot, 0x60, facing);
        if (Virtual<uint8_t>(lot, 0x70)) {
            Virtual<uint8_t>(lot, 0x60, tried);
            Virtual<uint8_t>(lot, 0x78);
            At<uint8_t>(self, field::kDirty5D) = 1;
            return;
        }
    }
}

// 0x006C4BD0: debris props over the rectangle low..high (x, y, z floats)
// of an exploded lot: area / 128 + 4 props, the first four at the corners
// (none on a lot narrower than 10; each on one draw in two), the others
// anywhere. A prop is made by the prop manager (slot 15), turned, placed,
// moved back inside the rectangle by its bounding box and given to the
// prop manager with `owner` (slot 25). The order of the draws from the
// class's generator is the game's.
void __fastcall ScatterDebris(char*, void*, void* owner, const float* low, const float* high, uint32_t burnt)
{
    static bool logged = false;
    FirstCall("Fn_006C4BD0", logged);
    void* const random = reinterpret_cast<void*>(addr::cSC4LotDeveloper_Random);
    const auto* const types6 = reinterpret_cast<const uint32_t*>(addr::cSC4LotDeveloper_DebrisTypes6);
    const auto* const typesA = reinterpret_cast<const uint32_t*>(addr::cSC4LotDeveloper_DebrisTypesA);
    const auto* const typesC = reinterpret_cast<const uint32_t*>(addr::cSC4LotDeveloper_DebrisTypesC);
    const auto* const cornerX = reinterpret_cast<const int32_t*>(addr::cSC4LotDeveloper_CornerX);
    const auto* const cornerZ = reinterpret_cast<const int32_t*>(addr::cSC4LotDeveloper_CornerZ);
    const bool flag = static_cast<uint8_t>(burnt) != 0;
    // x87: the x extent stays on the stack unrounded; the z extent is
    // compared as the float it was stored as. The area is rounded to a
    // float from their unrounded product.
    const double extentX = static_cast<double>(high[0]) - static_cast<double>(low[0]);
    const double extentZd = static_cast<double>(high[2]) - static_cast<double>(low[2]);
    const float extentZ = static_cast<float>(extentZd);
    const float area = static_cast<float>(extentZd * extentX);
    float position[3] = {0.0f, low[1], 0.0f};
    const double ten = Global<float>(addr::Budget_Ten);
    const double twelve = Global<float>(addr::NeighborTuning_Months);
    const bool narrow = extentX < ten || static_cast<double>(extentZ) < ten;
    const bool wide = extentX > twelve && static_cast<double>(extentZ) > twelve;
    const int32_t count =
        static_cast<int32_t>(static_cast<double>(area) * Global<double>(addr::cSC4LotDeveloper_DebrisPerArea)) + 4;
    for (int32_t i = 0; i < count; ++i) {
        int32_t turn = i;
        uint32_t type;
        if (i < 4) {
            if (narrow)
                continue;
            if (!Below(random, 2))
                continue;
            uint32_t pick = 0;
            if (wide)
                pick = Below(random, flag ? 2 : 6);
            if (!pick)
                turn = (turn + 2) % 4;
            type = flag ? typesA[pick] : types6[pick];
            position[0] = cornerX[i] ? high[0] : low[0];
            position[2] = cornerZ[i] ? high[2] : low[2];
        } else {
            turn = static_cast<int32_t>(Below(random, 4));
            type = flag ? typesC[Below(random, 2)] : types6[Below(random, 6)];
            const auto Gauss = reinterpret_cast<GaussFn>(addr::cRZRandom_GaussianBetween);
            position[0] = static_cast<float>(Gauss(random, static_cast<double>(low[0]), static_cast<double>(high[0])));
            position[2] = static_cast<float>(Gauss(random, static_cast<double>(low[2]), static_cast<double>(high[2])));
        }
        if (!type)
            continue;
        // The game's out slot holds the area before; a prop manager that
        // stores nothing returns false.
        void* prop = nullptr;
        if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D10), 0x3C, type, &prop))
            continue;
        if (Virtual<uint8_t>(Virtual<void*>(prop, 0xC), 0xC)) {
            Virtual<uint32_t>(Virtual<void*>(prop, 0xC), 0x74, 0x40u);
            Virtual<uint8_t>(prop, 0x28, turn);
            Virtual<uint8_t>(Virtual<void*>(prop, 0xC), 0x24, static_cast<float*>(position));
            float boxLow[3] = {0.0f, 0.0f, 0.0f}; // unset stack memory in the game
            float boxHigh[3] = {0.0f, 0.0f, 0.0f};
            Virtual<void*>(Virtual<void*>(prop, 0xC), 0x28, boxLow, boxHigh);
            if (boxLow[0] < low[0])
                position[0] = static_cast<float>(
                    (static_cast<double>(low[0]) - static_cast<double>(boxLow[0])) + static_cast<double>(position[0]));
            else if (boxHigh[0] > high[0])
                position[0] = static_cast<float>(
                    (static_cast<double>(high[0]) - static_cast<double>(boxHigh[0])) +
                    static_cast<double>(position[0]));
            if (boxLow[2] < low[2])
                position[2] = static_cast<float>(
                    (static_cast<double>(low[2]) - static_cast<double>(boxLow[2])) + static_cast<double>(position[2]));
            else if (boxHigh[2] > high[2])
                position[2] = static_cast<float>(
                    (static_cast<double>(high[2]) - static_cast<double>(boxHigh[2])) +
                    static_cast<double>(position[2]));
            Virtual<uint8_t>(Virtual<void*>(prop, 0xC), 0x24, static_cast<float*>(position));
            void* const props = Global<void*>(addr::SC4_Service43D10);
            Virtual<uint8_t>(props, 0x64, owner, Virtual<void*>(prop, 0xC));
        }
        Virtual<uint32_t>(prop, 0x8);
    }
}

// --- Installation of the groups ---------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}
#define OPENSC4_LOT_SLOT(index, name, fn) \
    OPENSC4_SLOT(addr::cSC4LotDeveloper_vftable, index, addr::cSC4LotDeveloper_##name, fn)

const Slot kInitSlots[] = {OPENSC4_LOT_SLOT(3, Init, Init)};
const Slot kShutdownSlots[] = {OPENSC4_LOT_SLOT(4, Shutdown, Shutdown)};
const Slot kMessageSlots[] = {
    OPENSC4_SLOT(addr::cSC4LotDeveloper_vftable_4, 3, addr::cSC4LotDeveloper_DoMessage, DoMessage)};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4LotDeveloper_vftable_8, 3, addr::cSC4LotDeveloper_Write, Write),
    OPENSC4_SLOT(addr::cSC4LotDeveloper_vftable_8, 4, addr::cSC4LotDeveloper_Read, Read)};
const Slot kSmallSlots[] = {
    OPENSC4_LOT_SLOT(5, GetSimulatorType, GetSimulatorType),
    OPENSC4_LOT_SLOT(8, AbandonBuilding, AbandonBuilding),
    OPENSC4_LOT_SLOT(9, ReoccupyBuilding, ReoccupyBuilding),
    OPENSC4_LOT_SLOT(15, GetTerrainAltitudeByVertex, GetTerrainAltitudeByVertex),
    OPENSC4_LOT_SLOT(19, LevelAreaInternalVertices, LevelAreaInternalVertices),
    OPENSC4_LOT_SLOT(20, LevelBorderingVertices, LevelBorderingVertices),
    OPENSC4_LOT_SLOT(21, AttemptAreaLevelling, AttemptAreaLevelling),
    OPENSC4_LOT_SLOT(22, GetOccupantCellCoverage, GetOccupantCellCoverage),
    OPENSC4_LOT_SLOT(40, GetPositionForBuilding, GetPositionForBuilding),
    OPENSC4_LOT_SLOT(41, GetLotBaseTextureOccupant, GetLotBaseTextureOccupant),
    OPENSC4_LOT_SLOT(45, UpdateLotBaseTextureOccupant, UpdateLotBaseTextureOccupant),
    OPENSC4_LOT_SLOT(48, GetFoundationByOccupant, GetFoundationByOccupant),
    OPENSC4_LOT_SLOT(51, GetLotFoundation, GetLotFoundation),
    OPENSC4_LOT_SLOT(53, RemoveLotFoundation, RemoveLotFoundation),
    OPENSC4_LOT_SLOT(56, IsLotObjectFoundationRequired_006C3FF0, IsLotObjectFoundationRequired),
    OPENSC4_LOT_SLOT(58, AddBuildingFoundation_006C40E0, AddBuildingFoundationByConfiguration),
    OPENSC4_LOT_SLOT(59, AddBuildingFoundation_006C4020, AddBuildingFoundation),
    OPENSC4_LOT_SLOT(60, RemoveBuildingFoundation, RemoveBuildingFoundation),
    OPENSC4_LOT_SLOT(61, UpdateLotFoundation, UpdateLotFoundation),
    OPENSC4_LOT_SLOT(62, UpdateLotRetainingWalls, UpdateLotRetainingWalls),
    OPENSC4_SLOT(addr::cSC4LotDeveloper_FilterVftable, 4, addr::cSC4LotDeveloper_FilterTest, FilterTest),
};

#undef OPENSC4_LOT_SLOT
#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsAreOriginal(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("lot %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool WriteSlots(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("lot %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    return game::IsBuild641() && SlotsAreOriginal(slots, what) && WriteSlots(slots, what);
}

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }
bool InstallShutdown(registry::Mode) { return SwapSlots(kShutdownSlots, "shutdown"); }
bool InstallMessages(registry::Mode) { return SwapSlots(kMessageSlots, "messages"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

// The functions of this part that are not slots and that the other parts
// of the class call directly (no code outside the class does): detours.
// 0x006C3F30 has no caller but UpdateLotFoundation and is not hooked.
struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

Detour g_detours[] = {
    {addr::cSC4LotDeveloper_Fn_006C3B10, reinterpret_cast<void*>(&ZotCellThunk), nullptr},
    {addr::cSC4LotDeveloper_Fn_006C3C50, reinterpret_cast<void*>(&ClearLotBuildings), nullptr},
    {addr::cSC4LotDeveloper_Fn_006C42D0, reinterpret_cast<void*>(&ResetLotC999C45E), nullptr},
    {addr::cSC4LotDeveloper_Fn_006C4360, reinterpret_cast<void*>(&GetCornerAltitudes), nullptr},
    {addr::cSC4LotDeveloper_Fn_006C4630, reinterpret_cast<void*>(&NextCondition), nullptr},
    {addr::cSC4LotDeveloper_Fn_006C47C0, reinterpret_cast<void*>(&CheckLandValue), nullptr},
    {addr::cSC4LotDeveloper_Fn_006C4910, reinterpret_cast<void*>(&TurnLotToRoad), nullptr},
    {addr::cSC4LotDeveloper_Fn_006C4BD0, reinterpret_cast<void*>(&ScatterDebris), nullptr},
};

bool InstallSlots(registry::Mode)
{
    if (!game::IsBuild641() || !SlotsAreOriginal(kSmallSlots, "slots"))
        return false;
    for (const Detour& detour : g_detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("lot slots: no address entry for %08X; not installing", static_cast<unsigned>(detour.address));
            return false;
        }
    for (Detour& detour : g_detours)
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &detour.original))
            return false;
    return WriteSlots(kSmallSlots, "slots");
}

const bool g_registered[] = {
    registry::Register({"simulators.lot.init", "simulators/lot", registry::Kind::Vtable, registry::Phase::PostAppInit,
                        addr::cSC4LotDeveloper_vftable,
                        "Our lot developer Init with its tuning load and occupant filter (src/core/lot_object.cpp)",
                        false, InstallInit}),
    registry::Register({"simulators.lot.shutdown", "simulators/lot", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotDeveloper_vftable,
                        "Our lot developer Shutdown (src/core/lot_object.cpp)", false, InstallShutdown}),
    registry::Register({"simulators.lot.messages", "simulators/lot", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotDeveloper_vftable_4,
                        "Our lot developer DoMessage (src/core/lot_object.cpp)", false, InstallMessages}),
    registry::Register({"simulators.lot.save", "simulators/lot", registry::Kind::Vtable, registry::Phase::PostAppInit,
                        addr::cSC4LotDeveloper_vftable_8,
                        "Our lot developer Write and Read: the save record, version 1 (src/core/lot_object.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.lot.slots", "simulators/lot", registry::Kind::Vtable, registry::Phase::PostAppInit,
                        addr::cSC4LotDeveloper_vftable,
                        "Our lot developer small slots (building foundations, the base texture occupant, altitudes "
                        "and levelling) and the helpers below 0x006C4EF0, eight of them by detours "
                        "(src/core/lot_object.cpp)",
                        false, InstallSlots}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00AB1B98 (66 slots, deleting destructor last),
// 0x00AB1B88 (at +4) and 0x00AB1B70 (at +8), taken at the first
// construction, after every PostAppInit slot swap of the other groups and
// parts.
constexpr int kSlots = 66, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006C3B80).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidLotDeveloper: offset = 0; break;
    case kIidMessageTarget2: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006C49D0): Shutdown and the deleting destructor (slot
// 65) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x104, 1u);
    return 0;
}

// The destructor (0x006C7280): only what it frees; the buildings, the
// services and the iterators are Shutdown's.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    ClearTree(self + field::kTree);
    if (void* const head = At<void*>(self, field::kTree))
        Free(head);
    if (void* const points = At<void*>(self, field::kCurve))
        Free(points);
    // The deque: its blocks (0x006DA8D0 over the map from the first block
    // to the last), then the map.
    char* const deque = self + field::kBuildings;
    if (At<void*>(deque, 0x20)) {
        void** const last = At<void**>(deque, 0x1C) + 1;
        for (void** block = At<void**>(deque, 0xC); block < last; ++block)
            if (*block)
                Free(*block);
        if (void* const map = At<void*>(deque, 0x20))
            Free(map);
    }
}

// Slot 65, as the game's (0x006C8810).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("lot object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x006C3B00).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

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
    return ReleaseObject(part - Offset, nullptr);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LotDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4LotDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4LotDeveloper_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[65] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("lot object: vtables copied (slots 3/4 %p %p, +4 slot 3 %p, +8 slots 3/4 %p %p)", g_vtable[3],
               g_vtable[4], g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x006C8650, reached only from the factory
// 0x006C8DF0): the fields the game's sets, in its order of allocations (the
// deque, then the tree's head). Fields it leaves unset (+0xA9 to +0xAB,
// +0xC2, +0xE2, +0x104) stay unset.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<uint8_t>(self, 0xC) = 0;
    for (std::size_t offset = 0x10; offset <= 0x58; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, 0x5C) = 0;
    At<uint8_t>(self, 0x5D) = 1;
    At<uint8_t>(self, 0x5E) = 1;
    At<uint8_t>(self, 0x5F) = 0;
    for (std::size_t offset = 0x60; offset <= 0x88; offset += 4)
        At<uint32_t>(self, offset) = 0;
    reinterpret_cast<DequeCtorFn>(addr::SC4CellDeque_Construct)(self + field::kBuildings, 0);
    At<DequeIterator>(self, field::kNextBuilding) = Begin(self);
    At<uint32_t>(self, 0x9C) = 0;
    At<uint32_t>(self, 0xA0) = 8;
    At<uint32_t>(self, 0xA4) = 200;
    At<uint8_t>(self, 0xA8) = 0;
    for (std::size_t offset = 0xAC; offset <= 0xBC; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint16_t>(self, 0xC0) = 90;
    At<float>(self, 0xC4) = 0.6f;
    At<float>(self, 0xC8) = 0.4f;
    At<float>(self, 0xCC) = 0.2f;
    At<float>(self, 0xD0) = 1.0f;
    At<float>(self, 0xD4) = 1.0f;
    At<uint32_t>(self, 0xD8) = 0x890B7312;
    At<uint32_t>(self, 0xDC) = 0x890B7312;
    At<uint8_t>(self, 0xE0) = 1;
    At<uint8_t>(self, 0xE1) = 1;
    At<uint32_t>(self, 0xE4) = 0;
    At<uint32_t>(self, 0xE8) = 0;
    reinterpret_cast<SelfFn>(addr::SC4Curve_Init)(self + field::kCurve);
    At<char*>(self, 0xFC) = nullptr;
    char* const head = static_cast<char*>(Allocate(0x1C));
    At<char*>(self, 0xFC) = head;
    At<uint32_t>(self, 0x100) = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
    At<uint32_t>(self, 0xAC) = 3;
    At<uint32_t>(self, 0xB0) = 0;
    At<uint32_t>(self, 0xBC) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("lot object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4LotDeveloper_cSC4LotDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.lot.object",
    "simulators/lot",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LotDeveloper_cSC4LotDeveloper,
    "Our cSC4LotDeveloper object: constructor (detour; the factory is its only caller), destructor, identity; "
    "methods from copies of the (patched) game vtables (src/core/lot_object.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::lot_sim
