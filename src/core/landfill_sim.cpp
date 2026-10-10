// Our cSC4LandfillDeveloper (build 641; class ID 0xEA09D4AE, code
// 0x006B8B10-0x006BA120): the object the city keeps at +0x1B4 (city slot 78,
// GetLandfillDeveloper). It owns the list of landfill zones, offers them
// the city's garbage each month, keeps the department's line item for their
// upkeep and answers what zoning a landfill would cost. The zones themselves
// (cSC4LandfillZone, 0x70 bytes, vtable 0x00AB1610, code from 0x006BA120)
// are another class and stay the game's: their functions are called at
// their addresses.
//
// The object (0x50 bytes; constructor 0x006B9780, factory 0x006B98E0):
//   +0x00 the primary vtable 0x00AB157C (13 slots)
//   +0x04 cIGZMessageTarget2 (0x00AB156C, 4 slots; DoMessage 0x006B9EA0)
//   +0x08 cIGZSerializable (0x00AB1554, 6 slots; Write 0x006B9880, Read
//         0x006B9900, GetGZCLSID 0x006B8B10)
//   +0x0C uint8   initialized (Init sets it first, Shutdown clears it)
//   +0x10 int32   reference count (signed compare in Release)
//   +0x14 uint8   loaded: Read sets it; message 0x26D31EC1 then initialises
//                 the zones
//   +0x18 the message server (0x00B43CCC); +0x1C the city (0x00B43CEC);
//   +0x20 the pollution simulator (0x00B43D9C); +0x24 the terrain
//         (0x00B43CF4); +0x28 the budget simulator (0x00B43D64)
//   +0x2C the department budget 0x6A42A8F5 (message 0x26D31EC1), referenced
//   +0x30 list<cSC4LandfillZone*>: the head node (next, previous, value;
//         0xC bytes from the STL allocator); +0x34 never written
//   +0x38 int64   the zones' upkeep (slot 7; the line item's full expenses)
//   +0x40 uint32  capacity, +0x44 uint32 use (slot 10), summed over the
//                 active zones
//   +0x48 uint8   the tuning was loaded
// Zone fields this file reads: +0xC its region (SC4CellRegion: x0, z0, x1,
// z1, +0x14 width, +0x18 height, +0x20 a column array of bit rows), +0x28
// empty, +0x29 active, +0x38 the level (float), +0x48 and +0x50 objects.
//
// Groups (each off by default, installed at PostAppInit):
//   simulators.landfill.sim       primary slots 3 to 12
//   simulators.landfill.messages  DoMessage (+4 slot 3)
//   simulators.landfill.save      Write and Read (+8 slots 3, 4)
//   simulators.landfill.object    our own object: a detour on the
//                                 constructor, identity and Release from
//                                 copies of the (patched) vtables
// No code outside the class calls a class function directly (only the
// vtables and the class registration refer to them), so there are no other
// detours and no register-convention thunks.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::landfill_sim {
namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("landfill: first %s", name);
}

constexpr uint32_t kClassId = 0xEA09D4AE;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidDeveloper = 0x8A09D502;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kMessages[5] = {0x07699455, 0x26D31EC1, 0x0772FAD4, 0x278E6003, 0xC6CDBF1A};
constexpr uint32_t kZoneLandfill = 0xE;

namespace field {
constexpr std::size_t kInitialized = 0xC;
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kLoaded = 0x14;
constexpr std::size_t kMessageServer = 0x18;
constexpr std::size_t kCity = 0x1C;
constexpr std::size_t kPollution = 0x20;
constexpr std::size_t kTerrain = 0x24;
constexpr std::size_t kBudget = 0x28;
constexpr std::size_t kDepartment = 0x2C;
constexpr std::size_t kZones = 0x30;
constexpr std::size_t kCosts = 0x38;
constexpr std::size_t kCapacity = 0x40;
constexpr std::size_t kUse = 0x44;
constexpr std::size_t kTuningLoaded = 0x48;
} // namespace field

namespace zonefield {
constexpr std::size_t kRegion = 0xC;
constexpr std::size_t kEmpty = 0x28;
constexpr std::size_t kActive = 0x29;
constexpr std::size_t kLevel = 0x38;
constexpr std::size_t kObject48 = 0x48;
constexpr std::size_t kObject50 = 0x50;
} // namespace zonefield

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using IntersectFn = uint8_t(__thiscall*)(int32_t* out, const int32_t* a, const int32_t* b);
using TextFn = void*(__thiscall*)(void* string, const char* text);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// --- The zones' functions (the game's) ---------------------------------------------

using ZoneVoidFn = void(__thiscall*)(void* zone);
using ZoneIntFn = int32_t(__thiscall*)(void* zone);
using ZoneInt64Fn = int64_t(__thiscall*)(void* zone);
using ZoneBoolFn = uint8_t(__thiscall*)(void* zone);
using ZonePointerFn = void(__thiscall*)(void* zone, const void* argument);
using ZoneStreamFn = uint8_t(__thiscall*)(void* zone, void* stream);
using ZoneFromRegionFn = void*(__thiscall*)(void* zone, const void* region);
using ZoneUsageFn = void(__thiscall*)(void* zone, uint32_t* free, uint32_t* used, uint32_t* capacity);
using ZoneLevelFn = void(__thiscall*)(void* zone, const int32_t* rect, uint32_t levelBits, uint32_t zero);
using ZoneCellFn = void(__thiscall*)(void* zone, int32_t x, int32_t z, uint32_t a, uint32_t b);
using ZoneMonthFn = void(__thiscall*)(void* zone, int32_t* garbage);

const auto ZoneDropEffects = reinterpret_cast<ZoneVoidFn>(addr::cSC4LandfillZone_DropEffects);
const auto ZoneCountActiveTiles = reinterpret_cast<ZoneIntFn>(addr::cSC4LandfillZone_CountActiveTiles);
const auto ZoneLevel = reinterpret_cast<ZoneLevelFn>(addr::cSC4LandfillZone_Level);
const auto ZoneGetUsage = reinterpret_cast<ZoneUsageFn>(addr::cSC4LandfillZone_GetUsage);
const auto ZoneGetFreeCapacity = reinterpret_cast<ZoneIntFn>(addr::cSC4LandfillZone_GetFreeCapacity);
const auto ZoneWrite = reinterpret_cast<ZoneStreamFn>(addr::cSC4LandfillZone_Write);
const auto ZoneGetMaintenanceCost = reinterpret_cast<ZoneInt64Fn>(addr::cSC4LandfillZone_GetMaintenanceCost);
const auto ZoneIsEmpty = reinterpret_cast<ZoneBoolFn>(addr::cSC4LandfillZone_IsEmpty);
const auto ZoneSub6BB1D0 = reinterpret_cast<ZoneVoidFn>(addr::cSC4LandfillZone_Sub6BB1D0);
const auto ZoneInit = reinterpret_cast<ZoneVoidFn>(addr::cSC4LandfillZone_Init);
const auto ZoneShutdown = reinterpret_cast<ZoneVoidFn>(addr::cSC4LandfillZone_Shutdown);
const auto ZoneUpdateCapacity = reinterpret_cast<ZoneVoidFn>(addr::cSC4LandfillZone_UpdateCapacity);
const auto ZoneConstruct = reinterpret_cast<SelfFn>(addr::cSC4LandfillZone_cSC4LandfillZone);
const auto ZoneFromRegion = reinterpret_cast<ZoneFromRegionFn>(addr::cSC4LandfillZone_FromRegion);
const auto ZoneRead = reinterpret_cast<ZoneStreamFn>(addr::cSC4LandfillZone_Read);
const auto ZoneAddRegion = reinterpret_cast<ZonePointerFn>(addr::cSC4LandfillZone_AddRegion);
const auto ZoneClearCell = reinterpret_cast<ZoneCellFn>(addr::cSC4LandfillZone_ClearCell);
const auto ZoneSub6BCD70 = reinterpret_cast<ZoneVoidFn>(addr::cSC4LandfillZone_Sub6BCD70);
const auto ZoneOnTilesChanged = reinterpret_cast<ZonePointerFn>(addr::cSC4LandfillZone_OnTilesChanged);
const auto ZoneOnMonth = reinterpret_cast<ZoneMonthFn>(addr::cSC4LandfillZone_OnMonth);

// --- The game's lists (node: next, previous, value) ---------------------------------

struct Node {
    Node* next;
    Node* previous;
    void* value;
};

Node*& Zones(char* self) { return At<Node*>(self, field::kZones); }

// A new empty list head from the STL allocator.
Node* NewList()
{
    auto* const head = static_cast<Node*>(Allocate(0xC));
    head->next = head;
    head->previous = head;
    return head;
}

// The inline push_back: the node allocated first, then linked before the head.
void PushBack(Node* head, void* value)
{
    auto* const node = static_cast<Node*>(Allocate(0xC));
    node->value = value;
    Node* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
}

// The inline clear: every node freed (the values untouched), the head
// linked to itself.
void ClearList(Node* head)
{
    for (Node* node = head->next; node != head;) {
        Node* const next = node->next;
        Free(node);
        node = next;
    }
    head->next = head;
    head->previous = head;
}

// --- Cell regions (SC4CellRegion<int32_t>) ----------------------------------------------

const int32_t* Rect(const void* region) { return static_cast<const int32_t*>(region); }

// Whether a cell is set in the region's bit map: false outside its width
// and height (unsigned compares on the offsets from x0, z0).
bool CellSet(const void* region, int32_t x, int32_t z)
{
    auto* const r = static_cast<const char*>(region);
    const uint32_t dx = static_cast<uint32_t>(x - *reinterpret_cast<const int32_t*>(r));
    const uint32_t dz = static_cast<uint32_t>(z - *reinterpret_cast<const int32_t*>(r + 0x4));
    if (dx >= *reinterpret_cast<const uint32_t*>(r + 0x14) || dz >= *reinterpret_cast<const uint32_t*>(r + 0x18))
        return false;
    const uint32_t* const column = (*reinterpret_cast<const uint32_t* const* const*>(r + 0x20))[dx];
    return (column[dz >> 5] & (1u << (dz & 0x1F))) != 0;
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// --- Helpers of the class ------------------------------------------------------------

// 0x006B8BC0: a figure published as a message of type 0x89EFA536 (data 1 =
// id, data 2 = value); nothing without the message server.
void PublishFigure(char* self, uint32_t id, uint32_t value)
{
    static bool logged = false;
    FirstCall("PublishFigure", logged);
    if (!At<void*>(self, field::kMessageServer))
        return;
    void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    if (message)
        message = reinterpret_cast<SelfFn>(addr::cGZMessage_cGZMessage)(message);
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, 0x89EFA536u);
    Virtual<void>(message, 0x2C, id);
    Virtual<void>(message, 0x40, value);
    Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0xC, message);
    Virtual<uint32_t>(message, 0x8);
}

// 0x006B8C70: the tuning from the landfill exemplar into the game's
// globals (the zone code reads some of them); nothing when the exemplar is
// missing.
void LoadTuning()
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    uint32_t key[3] = {0x6534284A, 0x67CD5FA1, 0x6A2806AF};
    void* exemplar = nullptr;
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (!Virtual<uint8_t>(resources, 0xC, static_cast<void*>(key), 0xA52160F5u, &exemplar, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    const auto GetFloat = reinterpret_cast<GetFn>(addr::SC4_GetPropertyFloat);
    const auto GetUint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32);
    GetFloat(holder, 0x4A280510, &Global<float>(addr::LandfillTuning_DecayRate));
    GetFloat(holder, 0x4A280513, &Global<float>(addr::LandfillTuning_FlatnessLimit));
    GetUint32(holder, 0x4A677E5B, &Global<uint32_t>(addr::LandfillTuning_MonthlyIntakeLimit));
    GetUint32(holder, 0x4A677E5C, &Global<uint32_t>(addr::LandfillTuning_Value4A677E5C));
    GetUint32(holder, 0x4A677E5E, &Global<uint32_t>(addr::LandfillTuning_Value4A677E5E));
    reinterpret_cast<GetFn>(addr::SC4_GetPropertySint64)(holder, 0x4A677E5F,
                                                         &Global<int64_t>(addr::LandfillTuning_Value4A677E5F));
    GetFloat(holder, 0x2AA00FE7, &Global<float>(addr::LandfillTuning_LevellingCost));
    reinterpret_cast<GetFn>(addr::SC4_GetPropertyBool)(holder, 0x4A677E5D,
                                                       &Global<uint8_t>(addr::LandfillTuning_Flag4A677E5D));
    Virtual<uint32_t>(exemplar, 0x8);
}

// The height of the terrain at a cell's centre (city slot 97, terrain slot
// 13), as the callee leaves it in ST0.
double HeightAt(char* self, int32_t x, int32_t z)
{
    float worldX = 0.0f;
    float worldZ = 0.0f;
    Virtual<void>(At<void*>(self, field::kCity), 0x184, x, z, &worldX, &worldZ);
    return Virtual<double>(At<void*>(self, field::kTerrain), 0x34, worldX, worldZ);
}

// 0x006B8DB0: the heights over a rectangle of cells (inclusive). Heights at
// or below sea level (terrain slot 29) count as sea level + 1. Highest and
// lowest start at the height of the middle cell, which is taken as it is.
// The average is 0 for an empty rectangle. Returns |highest - lowest| (ST0).
double MeasureTerrain(char* self, const int32_t* rect, float* average, float* highest, float* lowest)
{
    static bool logged = false;
    FirstCall("MeasureTerrain", logged);
    // The middle cell: signed halves rounded toward zero (cdq, sub, sar).
    const int32_t middleZ = (rect[1] + rect[3]) / 2;
    const int32_t middleX = (rect[0] + rect[2]) / 2;
    float sum = 0.0f;
    float count = 0.0f;
    const double middle = HeightAt(self, middleX, middleZ);
    *lowest = static_cast<float>(middle);
    *highest = static_cast<float>(middle);
    const float sea = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x74));
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            double height = HeightAt(self, x, z);
            // fcom, test 0x41, jp: kept when above the sea level (or unordered).
            if (height <= static_cast<double>(sea))
                height = static_cast<double>(sea) + 1.0;
            if (height < static_cast<double>(*lowest))
                *lowest = static_cast<float>(height);
            else if (height > static_cast<double>(*highest))
                *highest = static_cast<float>(height);
            sum = static_cast<float>(height + static_cast<double>(sum));
            count = static_cast<float>(static_cast<double>(count) + 1.0);
        }
    }
    if (count > 0.0f)
        *average = static_cast<float>(static_cast<double>(sum) / static_cast<double>(count));
    else
        *average = 0.0f;
    const double range = static_cast<double>(*highest) - static_cast<double>(*lowest);
    return range < 0.0 ? -range : range;
}

uint32_t SaturatingAdd(uint32_t total, uint32_t value)
{
    const uint32_t sum = total + value;
    return sum < total ? 0xFFFFFFFFu : sum;
}

// 0x006B8F20: capacity and use summed over the active zones.
void UpdateUsage(char* self)
{
    static bool logged = false;
    FirstCall("UpdateUsage", logged);
    Node* const head = Zones(self);
    At<uint32_t>(self, field::kCapacity) = 0;
    At<uint32_t>(self, field::kUse) = 0;
    for (Node* node = head->next; node != head; node = node->next) {
        uint32_t free = 0;
        uint32_t capacity = 0;
        uint32_t used = 0;
        if (At<uint8_t>(node->value, zonefield::kActive))
            ZoneGetUsage(node->value, &free, &used, &capacity);
        At<uint32_t>(self, field::kUse) = SaturatingAdd(At<uint32_t>(self, field::kUse), used);
        At<uint32_t>(self, field::kCapacity) = SaturatingAdd(At<uint32_t>(self, field::kCapacity), capacity);
    }
}

// 0x006B8FB0: the zones' upkeep into the department's line item 0x1E and
// +0x38. The department is used without a check, as the game does.
void UpdateMaintenanceCosts(char* self)
{
    static bool logged = false;
    FirstCall("UpdateMaintenanceCosts", logged);
    int64_t total = 0;
    for (Node* node = Zones(self)->next; node != Zones(self); node = node->next) {
        const int64_t sum = static_cast<int64_t>(static_cast<uint64_t>(total) +
                                                 static_cast<uint64_t>(ZoneGetMaintenanceCost(node->value)));
        total = sum < total ? INT64_MAX : sum;
    }
    void* const item = Virtual<void*>(At<void*>(self, field::kDepartment), 0x50, 0x1Eu);
    if (item)
        Virtual<uint32_t>(item, 0x4);
    // No check before the call in the game either.
    Virtual<uint8_t>(item, 0x30, total);
    if (item)
        Virtual<uint32_t>(item, 0x8);
    At<int64_t>(self, field::kCosts) = total;
}

// The pollution simulator's slot (20 or 24), negative values as 0.
int32_t NotNegative(char* self, std::size_t slot)
{
    const int32_t value = Virtual<int32_t>(At<void*>(self, field::kPollution), slot);
    return value < 0 ? 0 : value;
}

// The two monthly figures: the capacity, and the use plus the pollution
// simulator's slots 24 and 20 (asked in that order).
void PublishFigures(char* self)
{
    PublishFigure(self, 0x2A8C4696, At<uint32_t>(self, field::kCapacity));
    const int32_t value24 = NotNegative(self, 0x60);
    const int32_t value20 = NotNegative(self, 0x50);
    PublishFigure(self, 0x2A8C4697,
                  At<uint32_t>(self, field::kUse) + static_cast<uint32_t>(value20) + static_cast<uint32_t>(value24));
}

// 0x006B91D0: the month (message 0x66956816).
void OnMonth(char* self)
{
    static bool logged = false;
    FirstCall("OnMonth", logged);
    Node* const head = Zones(self);
    if (head->next == head) {
        PublishFigures(self);
        UpdateMaintenanceCosts(self);
        Virtual<void>(At<void*>(self, field::kPollution), 0x78, 0);
        return;
    }
    Node* node = head->next;
    const int64_t budget = Virtual<int64_t>(self, 0x20);
    // The garbage on offer: not negative, at most the tuned limit (unsigned
    // compare), scaled by budget / costs when the costs are above the budget.
    int32_t garbage = NotNegative(self, 0x50);
    const uint32_t limit = Global<uint32_t>(addr::LandfillTuning_MonthlyIntakeLimit);
    if (static_cast<uint32_t>(garbage) > limit)
        garbage = static_cast<int32_t>(limit);
    const int64_t costs = At<int64_t>(self, field::kCosts);
    if (costs > 0 && budget < costs)
        garbage = Ftol(static_cast<double>(budget) / static_cast<double>(costs) * static_cast<double>(garbage));
    int32_t room = 0;
    for (; node != head; node = node->next) {
        void* const zone = node->value;
        ZoneOnMonth(zone, &garbage);
        if (At<uint8_t>(zone, zonefield::kActive))
            room += ZoneGetFreeCapacity(zone);
    }
    Virtual<void>(At<void*>(self, field::kPollution), 0x78, room);
    UpdateMaintenanceCosts(self);
    UpdateUsage(self);
    PublishFigures(self);
}

// 0x006B9360 (`this` unused): the cells of rect set in region taken out of
// the zone; the zone marked empty when no active tile is left.
void ClearCells(void* zone, const int32_t* rect, const void* region, uint32_t a, uint32_t b)
{
    static bool logged = false;
    FirstCall("ClearCells", logged);
    for (int32_t x = rect[0]; x <= rect[2]; ++x)
        for (int32_t z = rect[1]; z <= rect[3]; ++z)
            if (CellSet(region, x, z))
                ZoneClearCell(zone, x, z, a, b);
    if (ZoneCountActiveTiles(zone) <= 0)
        At<uint8_t>(zone, zonefield::kEmpty) = 1;
}

// 0x006B9600: a zone out of the list, shut down and released; with
// clearCells every set cell of its region is taken out first.
void RemoveZone(char* self, void* zone, bool clearCells)
{
    static bool logged = false;
    FirstCall("RemoveZone", logged);
    // list.erase(std::remove(begin, end, zone)) with the one-iterator erase
    // (0x006B9450 and 0x006B8C30): the values after the first match move
    // down over it, and only the node std::remove returns is erased. When
    // the zone is not in the list that node is the head itself, which the
    // game unlinks and frees; copied as it is.
    Node* const head = Zones(self);
    Node* result = head->next;
    while (result != head && result->value != zone)
        result = result->next;
    if (result != head) {
        for (Node* node = result->next; node != head; node = node->next) {
            if (node->value != zone) {
                result->value = node->value;
                result = result->next;
            }
        }
    }
    result->previous->next = result->next;
    result->next->previous = result->previous;
    Free(result);

    // The loop bounds come from the region the zone had on entry; each cell
    // is tested in the region it has now.
    const int32_t* const bounds = Rect(At<void*>(zone, zonefield::kRegion));
    if (clearCells) {
        for (int32_t x = bounds[0]; x <= bounds[2]; ++x)
            for (int32_t z = bounds[1]; z <= bounds[3]; ++z)
                if (CellSet(At<void*>(zone, zonefield::kRegion), x, z))
                    ZoneClearCell(zone, x, z, 1, 1);
    }
    ZoneShutdown(zone);
    Virtual<uint32_t>(zone, 0x8);
}

// 0x006B96E0: the part of a zone inside a region given up.
void Dezone(char* self, void* zone, const void* region, uint32_t a, uint32_t b)
{
    static bool logged = false;
    FirstCall("Dezone", logged);
    int32_t rect[4];
    const auto Intersect = reinterpret_cast<IntersectFn>(addr::SC4Rect_Intersect);
    if (!Intersect(rect, Rect(At<void*>(zone, zonefield::kRegion)), Rect(region)))
        return;
    ClearCells(zone, rect, region, a, b);
    if (ZoneIsEmpty(zone))
        RemoveZone(self, zone, true);
    else
        ZoneUpdateCapacity(zone);
    int32_t changed[4] = {rect[0], rect[1], rect[2], rect[3]};
    Virtual<void>(At<void*>(self, field::kTerrain), 0x110, 1u, 1u, static_cast<void*>(changed), 5u);
}

// 0x006B99F0: message 0x278E6003 for a rectangle of cells: every zone told,
// the zones then empty removed with their cells.
void OnTilesChanged(char* self, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("OnTilesChanged", logged);
    Node* const emptied = NewList();
    for (Node* node = Zones(self)->next; node != Zones(self); node = node->next) {
        void* const zone = node->value;
        ZoneOnTilesChanged(zone, rect);
        if (ZoneIsEmpty(zone))
            PushBack(emptied, zone);
    }
    for (Node* node = emptied->next; node != emptied; node = node->next)
        RemoveZone(self, node->value, true);
    ClearList(emptied);
    Free(emptied);
}

// 0x006B9AC0: message 0x07699455: a region of cells was zoned as zoneType.
void OnZoneChange(char* self, const void* region, uint32_t zoneType)
{
    static bool logged = false;
    FirstCall("OnZoneChange", logged);
    Node* const nearby = NewList();
    const int32_t rect[4] = {Rect(region)[0], Rect(region)[1], Rect(region)[2], Rect(region)[3]};
    // The zones whose rectangle is within one cell of the region's.
    for (Node* node = Zones(self)->next; node != Zones(self); node = node->next) {
        const int32_t* const other = Rect(At<void*>(node->value, zonefield::kRegion));
        if (rect[2] + 1 >= other[0] && rect[0] - 1 <= other[2] && rect[3] + 1 >= other[1] && rect[1] - 1 <= other[3])
            PushBack(nearby, node->value);
    }
    const bool any = nearby->next != nearby;
    if (any) {
        void* previous = nullptr;
        for (Node* node = nearby->next; node != nearby; node = node->next) {
            void* const zone = node->value;
            if (zoneType != kZoneLandfill) {
                Dezone(self, zone, region, 0, 1);
            } else {
                ZoneDropEffects(zone);
                if (previous) {
                    Dezone(self, zone, region, 0, 0);
                } else {
                    ZoneAddRegion(zone, region);
                    ZoneSub6BCD70(zone);
                    ZoneUpdateCapacity(zone);
                    ZoneSub6BB1D0(zone);
                }
            }
            previous = zone;
        }
        ClearList(nearby);
        UpdateMaintenanceCosts(self);
    }
    if (!any && zoneType == kZoneLandfill) {
        void* zone = OperatorNew(0x70);
        if (zone)
            zone = ZoneFromRegion(zone, region);
        Virtual<uint32_t>(zone, 0x4);
        ZoneInit(zone);
        float average = 0.0f;
        float highest = 0.0f;
        float lowest = 0.0f;
        const double range = MeasureTerrain(self, rect, &average, &highest, &lowest);
        uint32_t level = 0;
        std::memcpy(&level, &average, sizeof level);
        // fcomp, test 0x41, jne: levelled only when the range is above the limit.
        if (range > static_cast<double>(Global<float>(addr::LandfillTuning_FlatnessLimit)))
            ZoneLevel(zone, rect, level, 0);
        else
            At<uint32_t>(zone, zonefield::kLevel) = level;
        ZoneSub6BCD70(zone);
        ZoneUpdateCapacity(zone);
        PushBack(Zones(self), zone);
    }
    UpdateUsage(self);
    ClearList(nearby);
    Free(nearby);
}

// --- The primary interface -------------------------------------------------------------

// A service into `offset` when there is none (referenced without a check,
// as the game does).
void Take(char* self, std::size_t offset, void* service)
{
    if (At<void*>(self, offset))
        return;
    At<void*>(self, offset) = service;
    Virtual<uint32_t>(service, 0x4);
}

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// Slot 3 (0x006B9D70).
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    if (!At<uint8_t>(self, field::kTuningLoaded)) {
        LoadTuning();
        At<uint8_t>(self, field::kTuningLoaded) = 1;
    }
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    void* const target = self + 0x4;
    GameString name;
    void* const text = reinterpret_cast<TextFn>(addr::SC4String_FromText)(
        &name, reinterpret_cast<const char*>(addr::cSC4LandfillDeveloper_Name));
    Virtual<uint32_t>(simulators, 0x4C, target, 6u, text, 2u);
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        Free(name.begin);
    Take(self, field::kCity, Global<void*>(addr::SC4_City));
    Take(self, field::kTerrain, Global<void*>(addr::SC4_Terrain));
    Take(self, field::kPollution, Global<void*>(addr::SC4_PollutionSimulator));
    Take(self, field::kBudget, Global<void*>(addr::SC4_BudgetSimulator));
    if (!At<void*>(self, field::kMessageServer)) {
        Take(self, field::kMessageServer, Global<void*>(addr::SC4_MessageServer));
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, target, message);
    }
    return 1;
}

void Release(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// Slot 4 (0x006B94B0).
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    void* const target = self + 0x4;
    if (At<void*>(self, field::kMessageServer)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, target, message);
        Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x8);
        At<void*>(self, field::kMessageServer) = nullptr;
    }
    // Every zone shut down and released, its node unlinked and freed.
    for (Node* node = Zones(self)->next; node != Zones(self);) {
        void* const zone = node->value;
        ZoneShutdown(zone);
        Virtual<uint32_t>(zone, 0x8);
        Node* const next = node->next;
        node->previous->next = next;
        next->previous = node->previous;
        Free(node);
        node = next;
    }
    ClearList(Zones(self));
    for (const std::size_t offset :
         {field::kDepartment, field::kBudget, field::kPollution, field::kTerrain, field::kCity})
        Release(self, offset);
    if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, target);
    return 1;
}

// Slot 8 (0x006B8BA0): the current expenses of line item 0x1E of the
// department (a tail jump in the game).
int64_t __fastcall GetCurrentBudget(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetCurrentBudget", logged);
    void* const item = Virtual<void*>(At<void*>(self, field::kDepartment), 0x50, 0x1Eu);
    return Virtual<int64_t>(item, 0x2C);
}

// Slot 5 (0x006B8D50): budget over costs into *result (an SC4Percentage, a
// float), or the game's 1.0 when the costs are not above 0. The budget is
// asked through slot 8 first in either case.
float* __fastcall GetEfficiency(char* self, void*, float* result)
{
    static bool logged = false;
    FirstCall("GetEfficiency", logged);
    const float full = Global<float>(addr::LandfillTuning_FullEfficiency);
    const int64_t budget = Virtual<int64_t>(self, 0x20);
    const int64_t costs = At<int64_t>(self, field::kCosts);
    if (costs > 0)
        *result = static_cast<float>(static_cast<double>(budget) / static_cast<double>(costs));
    else
        *result = full;
    return result;
}

// Slot 6 (0x006B9840).
uint32_t __fastcall GetSimulatorType(char*, void*)
{
    static bool logged = false;
    FirstCall("GetSimulatorType", logged);
    return 0x2A398ED6;
}

// Slot 7 (0x006B9850).
int64_t __fastcall GetMaintenanceCosts(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMaintenanceCosts", logged);
    return At<int64_t>(self, field::kCosts);
}

// Slot 9 (0x006B9030): what levelling the region's cells would cost
// (EDX:EAX with EDX 0). The level is that of the first zone whose rectangle
// meets the region's; without one it is the average height, and the cost is
// 0 when the terrain's range is not above the limit.
uint64_t __fastcall GetZoningCost(char* self, void*, const void* region)
{
    static bool logged = false;
    FirstCall("GetZoningCost", logged);
    const int32_t rect[4] = {Rect(region)[0], Rect(region)[1], Rect(region)[2], Rect(region)[3]};
    void* found = nullptr;
    for (Node* node = Zones(self)->next; node != Zones(self) && !found; node = node->next) {
        const int32_t* const other = Rect(At<void*>(node->value, zonefield::kRegion));
        if (rect[2] >= other[0] && rect[0] <= other[2] && rect[3] >= other[1] && rect[1] <= other[3])
            found = node->value;
    }
    float level = 0.0f;
    if (found) {
        level = At<float>(found, zonefield::kLevel);
    } else {
        float average = 0.0f;
        float highest = 0.0f;
        float lowest = 0.0f;
        const double range = MeasureTerrain(self, rect, &average, &highest, &lowest);
        // fcomp, test 0x41, jne: nothing to pay unless the range is above the limit.
        if (!(range > static_cast<double>(Global<float>(addr::LandfillTuning_FlatnessLimit))))
            return 0;
        level = average;
    }
    float sum = 0.0f;
    const int32_t* const r = Rect(region);
    for (int32_t x = r[0]; x <= r[2]; ++x) {
        for (int32_t z = r[1]; z <= r[3]; ++z) {
            if (!CellSet(region, x, z))
                continue;
            double difference = static_cast<double>(level) - HeightAt(self, x, z);
            if (difference < 0.0)
                difference = -difference;
            sum = static_cast<float>(difference + static_cast<double>(sum));
        }
    }
    // Rounded to nearest by adding 1.5 * 2^23 (the float at 0x00AB1550) and
    // taking the float's bits less those of that constant.
    const float biased = static_cast<float>(
        static_cast<double>(Global<float>(addr::LandfillTuning_LevellingCost)) * static_cast<double>(sum) + 12582912.0);
    uint32_t bits = 0;
    std::memcpy(&bits, &biased, sizeof bits);
    return bits + 0xB4C00000u;
}

// Slot 10 (0x006B9820).
void __fastcall GetLandfillUsage(char* self, void*, uint32_t* usage, uint32_t* capacity)
{
    static bool logged = false;
    FirstCall("GetLandfillUsage", logged);
    *capacity = At<uint32_t>(self, field::kCapacity);
    *usage = At<uint32_t>(self, field::kUse);
    if (*usage > *capacity)
        *usage = *capacity;
}

// Slot 11 (0x006B8F10): the cells zoned as landfill, from the service at
// 0x00B43D14 (the global, not a field).
int32_t __fastcall GetLandfillTileCount(char*, void*)
{
    static bool logged = false;
    FirstCall("GetLandfillTileCount", logged);
    return Virtual<int32_t>(Global<void*>(addr::SC4_Service43D14), 0x18, kZoneLandfill);
}

// Slot 12 (0x006B9400): the first zone whose rectangle holds the cell, the
// far edges excluded here.
void* __fastcall GetLandfillZoneFromCell(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("GetLandfillZoneFromCell", logged);
    Node* const head = Zones(self);
    for (Node* node = head->next; node != head; node = node->next) {
        const int32_t* const rect = Rect(At<void*>(node->value, zonefield::kRegion));
        if (x >= rect[0] && z >= rect[1] && x < rect[2] && z < rect[3])
            return node->value;
    }
    return nullptr;
}

// --- Messages ------------------------------------------------------------------------------

// cIGZMessageTarget2 slot 3 (0x006B9EA0), on the part at +0x4. Always true.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x4;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x26D31EC1: {
        // The department budget; stored over the old one without a release.
        void* const department = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, 0x6A42A8F5u);
        At<void*>(self, field::kDepartment) = department;
        if (department)
            Virtual<uint32_t>(department, 0x4);
        if (At<uint8_t>(self, field::kLoaded)) {
            Node* const head = Zones(self);
            for (Node* node = head->next; node != head; node = node->next)
                ZoneInit(node->value);
            UpdateMaintenanceCosts(self);
            UpdateUsage(self);
        }
        break;
    }
    case 0x0772FAD4: {
        void* const department = At<void*>(self, field::kDepartment);
        if (!department)
            break;
        // 0x004C3730: the funding percentage above the zero at 0x00B490B0.
        const float* const funding = Virtual<const float*>(department, 0x28);
        const bool funded = *funding > Global<float>(addr::LandfillTuning_ZeroPercentage);
        // The game stores the flag in the low byte of its message argument
        // and pushes the whole dword.
        const uint32_t argument =
            (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(message)) & 0xFFFFFF00u) | (funded ? 1u : 0u);
        for (Node* node = Zones(self)->next; node != Zones(self); node = node->next)
            if (void* const object = At<void*>(node->value, zonefield::kObject48))
                Virtual<void>(object, 0x48, argument);
        break;
    }
    case 0x07699455: {
        void* const region = Virtual<void*>(message, 0x3C);
        const uint32_t zoneType = Virtual<uint32_t>(message, 0x4C);
        OnZoneChange(self, region, zoneType);
        break;
    }
    case 0x278E6003: {
        if (Zones(self)->next == Zones(self))
            break;
        const uint32_t data1 = Virtual<uint32_t>(message, 0x24);
        const uint32_t data2 = Virtual<uint32_t>(message, 0x38);
        // As the game builds it: the high words less 1, then the low words.
        int32_t rect[4] = {static_cast<int32_t>(data1 >> 16) - 1, static_cast<int32_t>(data2 >> 16) - 1,
                           static_cast<int32_t>(data1 & 0xFFFF), static_cast<int32_t>(data2 & 0xFFFF)};
        const uint32_t kind = Virtual<uint32_t>(message, 0x60);
        const int32_t lastX = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174) - 1;
        const int32_t lastZ = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178) - 1;
        if (rect[0] < 0)
            rect[0] = 0;
        if (rect[2] > lastX)
            rect[2] = lastX;
        if (rect[1] < 0)
            rect[1] = 0;
        if (rect[3] > lastZ)
            rect[3] = lastZ;
        if (kind == 3 || kind == 1 || kind == 2)
            OnTilesChanged(self, rect);
        break;
    }
    case 0x66956816:
        OnMonth(self);
        break;
    case 0xC6CDBF1A: {
        if (Zones(self)->next == Zones(self))
            break;
        const bool one = Virtual<int32_t>(Global<void*>(addr::SC4_Service43DC4), 0x58) == 1;
        for (Node* node = Zones(self)->next; node != Zones(self); node = node->next) {
            void* const zone = node->value;
            void* const object = At<void*>(zone, zonefield::kObject50);
            if (!object)
                continue;
            const uint8_t on = Virtual<uint8_t>(object, 0x14);
            if (one) {
                if (!on)
                    Virtual<void>(At<void*>(zone, zonefield::kObject50), 0xC, 0u);
            } else if (on) {
                Virtual<void>(At<void*>(zone, zonefield::kObject50), 0x10, 1u);
            }
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

// --- Save ----------------------------------------------------------------------------------

// cIGZSerializable slot 3 (0x006B9880), on the part at +0x8: version 3, the
// count of zones, the zones. Always true.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x8;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(3));
    Node* const head = Zones(self);
    int32_t count = 0;
    for (Node* node = head->next; node != head; node = node->next)
        ++count;
    Virtual<uint8_t>(stream, 0x20, count);
    for (Node* node = head->next; node != head; node = node->next)
        ZoneWrite(node->value, stream);
    return 1;
}

// cIGZSerializable slot 4 (0x006B9900): version 3 only (false otherwise).
// The list is emptied without releasing its zones; a zone whose read fails
// stays referenced and unlisted. True once the version is right.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - 0x8;
    At<uint8_t>(self, field::kLoaded) = 1;
    // The game reads the version into the stack slot of its stream
    // argument: when the read writes nothing, the pointer's low 16 bits
    // are compared.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 3)
        return 0;
    // Read into a local the game never initialises; 0 when the read writes
    // nothing.
    int32_t count = 0;
    Virtual<uint8_t>(stream, 0x20, &count);
    ClearList(Zones(self));
    for (int32_t i = 0; i < count; ++i) {
        void* zone = OperatorNew(0x70);
        if (zone)
            zone = ZoneConstruct(zone);
        Virtual<uint32_t>(zone, 0x4);
        if (ZoneRead(zone, stream))
            PushBack(Zones(self), zone);
    }
    return 1;
}

// --- Installation of the slots ---------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSimSlots[] = {
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 3, addr::cSC4LandfillDeveloper_Init, Init),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 4, addr::cSC4LandfillDeveloper_Shutdown, Shutdown),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 5, addr::cSC4LandfillDeveloper_GetEfficiency, GetEfficiency),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 6, addr::cSC4LandfillDeveloper_GetSimulatorType,
                 GetSimulatorType),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 7, addr::cSC4LandfillDeveloper_GetMaintenanceCosts,
                 GetMaintenanceCosts),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 8, addr::cSC4LandfillDeveloper_GetCurrentBudget,
                 GetCurrentBudget),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 9, addr::cSC4LandfillDeveloper_GetZoningCost, GetZoningCost),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 10, addr::cSC4LandfillDeveloper_GetLandfillUsage,
                 GetLandfillUsage),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 11, addr::cSC4LandfillDeveloper_GetLandfillTileCount,
                 GetLandfillTileCount),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable, 12, addr::cSC4LandfillDeveloper_GetLandfillZoneFromCell,
                 GetLandfillZoneFromCell),
};
const Slot kMessageSlots[] = {
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable_4, 3, addr::cSC4LandfillDeveloper_DoMessage, DoMessage)};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable_8, 3, addr::cSC4LandfillDeveloper_Write, Write),
    OPENSC4_SLOT(addr::cSC4LandfillDeveloper_vftable_8, 4, addr::cSC4LandfillDeveloper_Read, Read),
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
            log::Write("landfill %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    log::Write("landfill %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallSim(registry::Mode) { return SwapSlots(kSimSlots, "sim"); }
bool InstallMessages(registry::Mode) { return SwapSlots(kMessageSlots, "messages"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.landfill.sim", "simulators/landfill", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LandfillDeveloper_vftable,
                        "Our landfill developer Init, Shutdown and queries: primary slots 3 to 12 "
                        "(src/core/landfill_sim.cpp)",
                        false, InstallSim}),
    registry::Register({"simulators.landfill.messages", "simulators/landfill", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LandfillDeveloper_vftable_4,
                        "Our landfill developer DoMessage: the month, zone changes, the department budget "
                        "(src/core/landfill_sim.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.landfill.save", "simulators/landfill", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LandfillDeveloper_vftable_8,
                        "Our landfill developer Write and Read: the save record, version 3 "
                        "(src/core/landfill_sim.cpp)",
                        false, InstallSave}),
};

// --- Our object ----------------------------------------------------------------------------

// Copies of the game's 0x00AB157C (13 slots), 0x00AB156C (at +4) and
// 0x00AB1554 (at +8), taken at the first construction, after every
// PostAppInit slot swap of the other groups.
constexpr int kSlots = 13, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006B8B20).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidDeveloper: offset = 0; break;
    case kIidMessageTarget2: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006B9D20): at the last reference Shutdown (slot 4), the
// list's nodes and head freed (0x005650A0: generic, left the game's) and
// the object deleted. The class has no destructor of its own.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("landfill object: destroying %p", static_cast<void*>(self));
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy)(self + field::kZones);
    OperatorDelete(self);
    return 0;
}

// As the game's (0x006B8B10).
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
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LandfillDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4LandfillDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4LandfillDeveloper_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("landfill object: vtables copied (slot 3 %p, +4 slot 3 %p, +8 slots 3/4 %p %p)", g_vtable[3],
               g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x006B9780, reached only from the factory
// 0x006B98E0, which jumps to it): the fields the game's sets, +0x34 left
// unset as there.
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
    At<uint8_t>(self, field::kInitialized) = 0;
    At<int32_t>(self, field::kRefCount) = 0;
    At<uint8_t>(self, field::kLoaded) = 0;
    for (std::size_t offset = 0x18; offset <= 0x30; offset += 4)
        At<uint32_t>(self, offset) = 0;
    Zones(self) = NewList();
    At<int64_t>(self, field::kCosts) = 0;
    At<uint32_t>(self, field::kCapacity) = 0;
    At<uint32_t>(self, field::kUse) = 0;
    At<uint8_t>(self, field::kTuningLoaded) = 0;
    ClearList(Zones(self));
    static int created = 0;
    if (created++ < 3)
        log::Write("landfill object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4LandfillDeveloper_cSC4LandfillDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.landfill.object",
    "simulators/landfill",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandfillDeveloper_cSC4LandfillDeveloper,
    "Our cSC4LandfillDeveloper object: constructor (detour), identity and Release; methods from copies of the "
    "(patched) game vtables (src/core/landfill_sim.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::landfill_sim
