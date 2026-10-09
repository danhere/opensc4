// Our cSC4NeighborDeal and cSC4NeighborConnection (build 641), the neighbors
// simulator's two other classes (docs/neighbors.md, "Deals and
// connections"): their own functions swapped into the game's vtables, and
// our own objects under their class IDs. Getters the linker folded with
// other classes' stay the game's.
#include "core/neighbors_sim.h"

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

namespace opensc4::neighbors_sim {
namespace {

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using CtorFn = void*(__thiscall*)(void* object);
using DateSetFn = void(__thiscall*)(void* date, uint32_t day);
using DateFieldFn = uint32_t(__thiscall*)(const void* face);
using DateDefaultFn = void*(__thiscall*)(void* date);
using DateDtorFn = void(__thiscall*)(void* date);
using HolderStreamFn = uint8_t(__thiscall*)(void* holder, void* stream);
using TileFn = void(__cdecl*)(int32_t, int32_t, int32_t*, int32_t*, void*);
using ListFn = void(__thiscall*)(void* list);
using GetVectorFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* vector);
using InsertNFn = void(__thiscall*)(void* vector, void* where, uint32_t count, const uint32_t* value);
using CopyVectorFn = void*(__thiscall*)(void* vector, const void* other);

void* RegionCities() { return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C); }

struct Date {
    const void* vtable;
    uint32_t day;
    const void* face;
    uint32_t zero;
};

// The deal news of the simulator part (0x006CF5C0); the copy here is the
// same function.
void Notify(void* deal, uint8_t post, uint32_t code)
{
    void* unknown = nullptr;
    if (!deal || Virtual<uint8_t>(deal, 0x0, 1u, &unknown)) {
        void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        if (message)
            message = reinterpret_cast<CtorFn>(addr::cGZMessage_cGZMessage)(message);
        if (message)
            Virtual<uint32_t>(message, 0x4);
        Virtual<void*>(message, 0x14, 0xEA418D8Fu);
        Virtual<void*>(message, 0x2C, code);
        Virtual<void*>(message, 0x84, unknown);
        void* const server = Global<void*>(addr::SC4_MessageServer);
        if (post)
            Virtual<uint8_t>(server, 0x10, message, 0u);
        else
            Virtual<uint8_t>(server, 0xC, message);
        Virtual<uint32_t>(message, 0x8);
    }
    if (unknown)
        Virtual<uint32_t>(unknown, 0x8);
}

// --- cSC4NeighborDeal ---------------------------------------------------------

namespace deal {
constexpr std::size_t kRefCount = 0x1C;
constexpr std::size_t kType = 0x20;
constexpr std::size_t kOtherX = 0x24;  // region tile across
constexpr std::size_t kOtherZ = 0x28;
constexpr std::size_t kCityX = 0x2C;   // our region tile
constexpr std::size_t kCityZ = 0x30;
constexpr std::size_t kAmount = 0x34;
constexpr std::size_t kIncAmount = 0x38;
constexpr std::size_t kIncAmount2 = 0x3C;
constexpr std::size_t kLastAmount = 0x40;
constexpr std::size_t kLastIncAmount = 0x44;
constexpr std::size_t kAmountUsed = 0x48;
constexpr std::size_t kCost = 0x50;    // int64
constexpr std::size_t kStart = 0x58;   // dates: 8 bytes each (vtable, day)
constexpr std::size_t kExpiration = 0x60;
constexpr std::size_t kCurrent = 0x68;
constexpr std::size_t kSuspension = 0x70;
constexpr std::size_t kFlags = 0x78;
constexpr std::size_t kSize = 0x80;
} // namespace deal

// QueryInterface (0x006CE260): interfaces 1 and 0x0773FEF1 at 0, the
// serializable part (+4).
uint8_t __fastcall DealQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid == 1 || iid == 0x0773FEF1) {
        *object = self;
    } else if (iid == 0xE4FDA3D4) {
        *object = self ? self + 4 : nullptr;
    } else {
        return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// Release (0x006CE2C0): at the last reference, the property holder part's
// deleting destructor (its slot 22).
uint32_t __fastcall DealRelease(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, deal::kRefCount);
    if (refs > 1)
        return --refs;
    Virtual<void*>(self + 8, 0x58, 1u);
    return 0;
}

void __fastcall SetCitySide(char* self, void*, int32_t x, int32_t z)
{
    At<int32_t>(self, deal::kCityX) = x;
    At<int32_t>(self, deal::kCityZ) = z;
}

void __fastcall GetCitySide(char* self, void*, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, deal::kCityX);
    *z = At<int32_t>(self, deal::kCityZ);
}

void __fastcall SetOtherSide(char* self, void*, int32_t x, int32_t z)
{
    At<int32_t>(self, deal::kOtherX) = x;
    At<int32_t>(self, deal::kOtherZ) = z;
}

// The city on the region tile across (0x006CF330).
void* __fastcall DealCity(char* self, void*)
{
    void* const cities = RegionCities();
    if (!cities)
        return nullptr;
    void** const slot = Virtual<void**>(cities, 0x2C, At<int32_t>(self, deal::kOtherX), At<int32_t>(self, deal::kOtherZ));
    return slot ? *slot : nullptr;
}

void __fastcall SetType(char* self, void*, int32_t type) { At<int32_t>(self, deal::kType) = type; }
void __fastcall SetIncAmount(char* self, void*, int32_t value) { At<int32_t>(self, deal::kIncAmount) = value; }
void __fastcall SetIncAmount2(char* self, void*, int32_t value) { At<int32_t>(self, deal::kIncAmount2) = value; }
void __fastcall SetLastIncAmount(char* self, void*, int32_t value) { At<int32_t>(self, deal::kLastIncAmount) = value; }
void __fastcall SetAmountUsed(char* self, void*, int32_t value) { At<int32_t>(self, deal::kAmountUsed) = value; }
int64_t __fastcall GetCost(char* self, void*) { return At<int64_t>(self, deal::kCost); }

// SetAmount (0x006CF960): only in a city, and not while 0x300 is set; news
// (sent) unless 0x80; then the cost again.
void __fastcall SetAmount(char* self, void*, int32_t amount)
{
    void* const city = Global<void*>(addr::SC4_City);
    if (!city || !Virtual<void*>(city, 0xF8))
        return;
    if (Virtual<uint8_t>(self, 0x9C, 0x300u))
        return;
    At<int32_t>(self, deal::kAmount) = amount;
    if (!Virtual<uint8_t>(self, 0x9C, 0x80u))
        Notify(self, 0, 0);
    Virtual<void>(self, 0x5C);
}

// UpdateCost (0x006CF9C0): unless 0x300, the price of the amount with the
// city across (the neighbors simulator's slot 35), at least 1; news unless
// 0x80.
void __fastcall UpdateCost(char* self, void*)
{
    if (Virtual<uint8_t>(self, 0x9C, 0x300u))
        return;
    void* const neighbors = Virtual<void*>(Global<void*>(addr::SC4_City), 0xF8);
    void* const city = Virtual<void*>(self, 0x1C);
    const double price = Virtual<double>(neighbors, 0x8C, At<uint32_t>(self, deal::kType),
                                         static_cast<double>(At<int32_t>(self, deal::kAmount)), city);
    int64_t cost = static_cast<int64_t>(price);
    if (!cost)
        cost = 1;
    At<int64_t>(self, deal::kCost) = cost;
    if (!Virtual<uint8_t>(self, 0x9C, 0x80u))
        Notify(self, 1, 0);
}

// The date setters: the other date's day (its slot 26) into ours; the
// getters: ours into the other (its slot 4).
template <std::size_t Field>
void __fastcall SetDate(char* self, void*, void* other)
{
    reinterpret_cast<DateSetFn>(addr::cRZDate_SetDay)(self + Field, Virtual<uint32_t>(other, 0x68));
}

template <std::size_t Field>
void __fastcall GetDate(char* self, void*, void* other)
{
    Virtual<void>(other, 0x10, At<uint32_t>(self, Field + 4));
}

uint32_t __fastcall CheckFlagsAnd(char* self, void*, uint32_t flags)
{
    return (At<uint32_t>(self, deal::kFlags) & flags) == flags ? 1 : 0;
}

uint32_t __fastcall CheckFlagsOr(char* self, void*, uint32_t flags)
{
    return (At<uint32_t>(self, deal::kFlags) & flags) ? 1 : 0;
}

// SetFlags (0x006CFA50): setting 0x08, 0x10 or 0x20 is ignored while 0x100
// is set; news when the flags change, unless 0x80.
uint8_t __fastcall SetFlags(char* self, void*, uint32_t flags, uint8_t on)
{
    const uint32_t before = At<uint32_t>(self, deal::kFlags);
    if (on) {
        if ((flags & 0x38) && (before & 0x100))
            flags &= 0xFFFFFFC7u;
        At<uint32_t>(self, deal::kFlags) = before | flags;
    } else {
        At<uint32_t>(self, deal::kFlags) = ~flags & before;
    }
    if (!Virtual<uint8_t>(self, 0x9C, 0x80u) && At<uint32_t>(self, deal::kFlags) != before)
        Notify(self, 1, 0);
    return 1;
}

// Copy (0x006CE2E0): amounts, cost, type, flags and the four dates of
// another deal; its region tiles.
void __fastcall Copy(char* self, void*, void* other)
{
    At<int32_t>(self, deal::kAmount) = Virtual<int32_t>(other, 0x28);
    At<int32_t>(self, deal::kAmountUsed) = Virtual<int32_t>(other, 0x50);
    At<int64_t>(self, deal::kCost) = Virtual<int64_t>(other, 0x58);
    At<int32_t>(self, deal::kType) = Virtual<int32_t>(other, 0x20);
    At<uint32_t>(self, deal::kFlags) = Virtual<uint32_t>(other, 0x90);
    At<int32_t>(self, deal::kIncAmount) = Virtual<int32_t>(other, 0x40);
    At<int32_t>(self, deal::kLastAmount) = Virtual<int32_t>(other, 0x30);
    At<int32_t>(self, deal::kAmountUsed) = Virtual<int32_t>(other, 0x50);
    Date date{};
    reinterpret_cast<DateDefaultFn>(addr::cRZDate_Default)(&date);
    const auto Day = reinterpret_cast<DateFieldFn>(addr::cRZDate_Day);
    const auto Set = reinterpret_cast<DateSetFn>(addr::cRZDate_SetDay);
    Virtual<void>(other, 0x60, &date.face);
    Set(self + deal::kStart, Day(&date.face));
    Virtual<void>(other, 0x68, &date.face);
    Set(self + deal::kExpiration, Day(&date.face));
    Virtual<void>(other, 0x70, &date.face);
    Set(self + deal::kCurrent, Day(&date.face));
    Virtual<void>(other, 0x78, &date.face);
    Set(self + deal::kSuspension, Day(&date.face));
    Virtual<void>(other, 0x18, self + deal::kOtherX, self + deal::kOtherZ);
    Virtual<void>(other, 0x10, self + deal::kCityX, self + deal::kCityZ);
    reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy)(&date);
}

// UpdateState (0x006CF360): a deal we pay for (even types) that costs more
// than our budget gets 0x10; any deal whose city across has no
// connection for it (the service at 0x00B43D8C, slot 23) gets 0x08.
void __fastcall UpdateState(char* self, void*)
{
    void* const ours = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90);
    const uint32_t type = At<uint32_t>(self, deal::kType);
    if (type == 0x4773FFE0 || type == 0x4773FFE2 || type == 0x4773FFE4) {
        const double budget = Virtual<double>(ours, 0xC8);
        if (static_cast<double>(At<int64_t>(self, deal::kCost)) > budget) {
            Virtual<uint8_t>(self, 0x94, 0x10u, 1u);
            return;
        }
    }
    void* const city = Virtual<void*>(self, 0x1C);
    if (!city || !Virtual<void*>(Global<void*>(addr::SC4_MonthEndService), 0x5C, type, city))
        Virtual<uint8_t>(self, 0x94, 0x8u, 1u);
}

// Cancel (0x006CE610).
void __fastcall Cancel(char* self, void*)
{
    Virtual<uint8_t>(self, 0x94, 0x4u, 1u);
    Virtual<uint8_t>(self, 0x94, 0x2u, 0u);
    Virtual<void>(self, 0x2C, 0);
}

// Activate (0x006CE640): a pending deal (0x01) without 0x08/0x10/0x20
// becomes active (0x02).
uint8_t __fastcall Activate(char* self, void*)
{
    if (!Virtual<uint8_t>(self, 0x9C, 0x1u) || Virtual<uint8_t>(self, 0x9C, 0x38u))
        return 0;
    Virtual<uint8_t>(self, 0x94, 0x2u, 1u);
    Virtual<uint8_t>(self, 0x94, 0x1u, 0u);
    Virtual<uint8_t>(self, 0x94, 0x4u, 0u);
    return 1;
}

// Clone (0x006CF3E0): a new deal, a copy, 0x80 as asked; when both region
// tiles hold cities and the city across is not ours, the tiles and the
// type are turned round (the deal as the other city sees it); without
// cities, cancelled (0x04).
uint8_t __fastcall Clone(char* self, void*, void** out, uint8_t silent)
{
    void* made = reinterpret_cast<NewFn>(addr::SC4_operator_new)(deal::kSize);
    if (made)
        made = reinterpret_cast<CtorFn>(addr::cSC4NeighborDeal_cSC4NeighborDeal)(made);
    if (made)
        Virtual<uint32_t>(made, 0x4);
    *out = made;
    Virtual<uint32_t>(made, 0x4);
    Virtual<void>(made, 0x80, self);
    Virtual<uint8_t>(made, 0x94, 0x80u, silent);
    void* const ours = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90);
    void** const citySide = Virtual<void**>(RegionCities(), 0x2C, At<int32_t>(self, deal::kCityX),
                                            At<int32_t>(self, deal::kCityZ));
    void** const otherSide = Virtual<void**>(RegionCities(), 0x2C, At<int32_t>(self, deal::kOtherX),
                                             At<int32_t>(self, deal::kOtherZ));
    if (!citySide || !otherSide) {
        Virtual<uint8_t>(made, 0x94, 0x4u, 1u);
    } else if (ours != *citySide) {
        Virtual<void>(made, 0xC, At<int32_t>(self, deal::kOtherX), At<int32_t>(self, deal::kOtherZ));
        Virtual<void>(made, 0x14, At<int32_t>(self, deal::kCityX), At<int32_t>(self, deal::kCityZ));
        constexpr uint32_t kTurned[6] = {0x4773FFE1, 0x4773FFE0, 0x4773FFE3, 0x4773FFE2, 0x4773FFE5, 0x4773FFE4};
        const uint32_t index = Virtual<uint32_t>(made, 0x20) - 0x4773FFE0u;
        if (index <= 5)
            Virtual<void>(made, 0x24, kTurned[index]);
    }
    Virtual<uint32_t>(made, 0x8);
    return 1;
}

// cIGZSerializable slot 3 (0x006CE400), on the part at +4: version 5.
uint32_t __fastcall DealWrite(char* part, void*, void* stream)
{
    char* const self = part - 4;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(5));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, deal::kType));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kOtherX));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kOtherZ));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kAmount));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kAmountUsed));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, deal::kCost));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, deal::kFlags));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kCityX));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kCityZ));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kIncAmount));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kLastAmount));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, deal::kAmountUsed));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, deal::kCurrent + 4));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, deal::kStart + 4));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, deal::kExpiration + 4));
    reinterpret_cast<HolderStreamFn>(addr::SC4PropertyHolder_Write)(self + 8, stream);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, deal::kSuspension + 4));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x006CE4E0): the same, any version; the
// suspension date only from version 5.
uint32_t __fastcall DealRead(char* part, void*, void* stream)
{
    char* const self = part - 4;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    uint32_t value = 0;
    Virtual<uint8_t>(stream, 0x24, &value);
    At<uint32_t>(self, deal::kType) = value;
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kOtherX));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kOtherZ));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kAmount));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kAmountUsed));
    Virtual<uint8_t>(stream, 0x28, &At<int64_t>(self, deal::kCost));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, deal::kFlags));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kCityX));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kCityZ));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kIncAmount));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kLastAmount));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, deal::kAmountUsed));
    const auto Set = reinterpret_cast<DateSetFn>(addr::cRZDate_SetDay);
    Virtual<uint8_t>(stream, 0x24, &value);
    Set(self + deal::kCurrent, value);
    Virtual<uint8_t>(stream, 0x24, &value);
    Set(self + deal::kStart, value);
    Virtual<uint8_t>(stream, 0x24, &value);
    Set(self + deal::kExpiration, value);
    reinterpret_cast<HolderStreamFn>(addr::SC4PropertyHolder_Read)(self + 8, stream);
    if (version > 4) {
        Virtual<uint8_t>(stream, 0x24, &value);
        Set(self + deal::kSuspension, value);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

uint32_t __fastcall DealClassId(void*, void*) { return 0x2A134F42; }

// --- cSC4NeighborConnection ---------------------------------------------------

namespace connection {
constexpr std::size_t kTrafficSource = 0x1C; // cISC4TrafficSource part
constexpr std::size_t kRefCount = 0x20;
constexpr std::size_t kType = 0x24;
constexpr std::size_t kX = 0x28;
constexpr std::size_t kZ = 0x2C;
constexpr std::size_t kTileX = 0x30;
constexpr std::size_t kTileZ = 0x34;
constexpr std::size_t kFlags = 0x38;   // uint8: bit 0 visited, bit 1 inactive
constexpr std::size_t kTrips = 0x3C;   // list of 0x34-byte trip records (STL allocator)
constexpr std::size_t kTripsDirty = 0x40;
constexpr std::size_t kSize = 0x44;
} // namespace connection

// QueryInterface (0x006CE000): 1 and 0x0773FEF2 at 0, serializable at +4.
uint8_t __fastcall ConnectionQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid == 1 || iid == 0x0773FEF2) {
        *object = self;
    } else if (iid == 0xE4FDA3D4) {
        *object = self ? self + 4 : nullptr;
    } else {
        return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

uint32_t __fastcall ConnectionRelease(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, connection::kRefCount);
    if (refs > 1)
        return --refs;
    Virtual<void*>(self + 8, 0x58, 1u);
    return 0;
}

void __fastcall GetConnectionCell(char* self, void*, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, connection::kX);
    *z = At<int32_t>(self, connection::kZ);
}

// GetConnectionInnerCell (0x006CF1B0): the city cell next to the
// connection's (it lies past one edge, clamped in on that axis).
uint8_t __fastcall GetConnectionInnerCell(char* self, void*, int32_t* x, int32_t* z)
{
    void* const city = Global<void*>(addr::SC4_City);
    const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
    const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
    const int32_t cx = At<int32_t>(self, connection::kX);
    const int32_t cz = At<int32_t>(self, connection::kZ);
    const bool outX = cx < 0 || cx > lastX;
    const bool outZ = cz < 0 || cz > lastZ;
    if (outX == outZ)
        return 0;
    *x = cx;
    *z = cz;
    if (outX)
        *x = cx < 0 ? 0 : lastX;
    else
        *z = cz < 0 ? 0 : lastZ;
    return 1;
}

void __fastcall GetConnectionTile(char* self, void*, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, connection::kTileX);
    *z = At<int32_t>(self, connection::kTileZ);
}

// The city on the region tile across (0x006CF180).
void* __fastcall ConnectionCity(char* self, void*)
{
    void* const cities = RegionCities();
    if (!cities)
        return nullptr;
    void** const slot =
        Virtual<void**>(cities, 0x2C, At<int32_t>(self, connection::kTileX), At<int32_t>(self, connection::kTileZ));
    return slot ? *slot : nullptr;
}

// Repair (0x006CF250): the region tile across worked out again.
void __fastcall Repair(char* self, void*, void* region)
{
    reinterpret_cast<TileFn>(addr::cSC4NeighborsSimulator_OtherSideTile)(
        At<int32_t>(self, connection::kX), At<int32_t>(self, connection::kZ), &At<int32_t>(self, connection::kTileX),
        &At<int32_t>(self, connection::kTileZ), region);
}

uint8_t __fastcall GetVisited(char* self, void*) { return At<uint8_t>(self, connection::kFlags) & 1; }

void __fastcall SetVisited(char* self, void*, uint8_t visited)
{
    uint8_t& flags = At<uint8_t>(self, connection::kFlags);
    flags = visited ? static_cast<uint8_t>(flags | 1) : static_cast<uint8_t>(flags & 0xFE);
}

uint8_t __fastcall GetIsActive(char* self, void*)
{
    return static_cast<uint8_t>(~(At<uint8_t>(self, connection::kFlags) >> 1) & 1);
}

void __fastcall SetIsActive(char* self, void*, uint8_t active)
{
    uint8_t& flags = At<uint8_t>(self, connection::kFlags);
    flags = active ? static_cast<uint8_t>(flags & 0xFD) : static_cast<uint8_t>(flags | 2);
}

// cIGZSerializable slot 3 (0x006CF080), on the part at +4: version 1.
uint32_t __fastcall ConnectionWrite(char* part, void*, void* stream)
{
    char* const self = part - 4;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, connection::kType));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, connection::kX));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, connection::kZ));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, connection::kTileX));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, connection::kTileZ));
    Virtual<uint8_t>(stream, 0x14, At<uint8_t>(self, connection::kFlags));
    reinterpret_cast<HolderStreamFn>(addr::SC4PropertyHolder_Write)(self + 8, stream);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x006CF0F0): version 1 only.
uint32_t __fastcall ConnectionRead(char* part, void*, void* stream)
{
    char* const self = part - 4;
    uint16_t version = 0;
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (version != 1)
        return read & 0xFFFFFF00u;
    uint32_t type = 0;
    Virtual<uint8_t>(stream, 0x24, &type);
    At<uint32_t>(self, connection::kType) = type;
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, connection::kX));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, connection::kZ));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, connection::kTileX));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, connection::kTileZ));
    Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, connection::kFlags));
    reinterpret_cast<HolderStreamFn>(addr::SC4PropertyHolder_Read)(self + 8, stream);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

uint32_t __fastcall ConnectionClassId(void*, void*) { return 0x0A134F24; }

// cISC4TrafficSource slot 3 (0x006D4790): the trips list.
void* __fastcall TripsList(char* part, void*) { return part + (connection::kTrips - connection::kTrafficSource); }

// cISC4TrafficSource slot 5 (0x006D4820): the connection's cell as a
// rectangle (x, z, x, z).
void __fastcall SourceRect(char* part, void*, int32_t* rect)
{
    char* const self = part - connection::kTrafficSource;
    rect[0] = At<int32_t>(self, connection::kX);
    rect[1] = At<int32_t>(self, connection::kZ);
    rect[2] = At<int32_t>(self, connection::kX);
    rect[3] = At<int32_t>(self, connection::kZ);
}

// A trip record of the connection's list (0x34 bytes with the node).
struct Trip {
    ListNode* next;
    ListNode* prev;
    uint32_t vectorA[3];
    uint32_t vectorB[3];
    uint32_t count;
    uint8_t kind;
    uint8_t index;
    uint8_t mode;
    uint8_t source;
    uint16_t word28;
    uint16_t word2A;
    uint32_t zero2C;
    uint32_t value30;
};
static_assert(sizeof(Trip) == 0x34);

void FreeTrip(Trip* trip)
{
    const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
    if (trip->vectorB[0])
        Free(reinterpret_cast<void*>(trip->vectorB[0]));
    if (trip->vectorA[0])
        Free(reinterpret_cast<void*>(trip->vectorA[0]));
    Free(trip);
}

// cISC4TrafficSource slot 4 (0x006D4C10), on the part at +0x1C: when marked
// (+0x40), the connection's trips (the list at +0x3C) brought to the counts
// its region data asks for (properties 0xAA9EA81D + kind, nine per kind,
// kinds 0 to 2): missing trips added to a type-5 trip of the slot or as a
// new type-4 trip, extra ones taken from type-5 trips first, then any.
void __fastcall UpdateTrips(char* part, void*)
{
    char* const self = part - connection::kTrafficSource;
    if (!At<uint8_t>(self, connection::kTripsDirty))
        return;
    At<uint8_t>(self, connection::kTripsDirty) = 0;
    void* const data = Virtual<void*>(Global<void*>(addr::SC4_MonthEndService), 0x34, self);
    auto* const head = At<ListNode*>(self, connection::kTrips);
    if (!data) {
        reinterpret_cast<ListFn>(addr::SC4TripList_Clear)(self + connection::kTrips);
        return;
    }
    uint32_t have[3][9] = {};
    for (auto* node = reinterpret_cast<Trip*>(head->next); reinterpret_cast<ListNode*>(node) != head;
         node = reinterpret_cast<Trip*>(node->next)) {
        if (node->kind <= 2)
            have[node->kind][node->index] += node->count;
    }
    const auto unlink = [](Trip* trip) {
        trip->prev->next = trip->next;
        trip->next->prev = trip->prev;
    };
    for (uint32_t kind = 0; kind < 3; ++kind) {
        struct {
            uint32_t* begin;
            uint32_t* end;
            uint32_t* capacity;
        } wanted{};
        const uint32_t zero = 0;
        reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyUint32Vector)(Virtual<void*>(data, 0xC), 0xAA9EA81Du + kind,
                                                                         &wanted);
        const uint32_t size = static_cast<uint32_t>(wanted.end - wanted.begin);
        if (size <= 9)
            reinterpret_cast<InsertNFn>(addr::SC4Uint32Vector_InsertN)(&wanted, wanted.end, 9 - size, &zero);
        else
            wanted.end = wanted.begin + 9;
        for (uint32_t index = 0; index < 9; ++index) {
            const uint32_t now = have[kind][index];
            const uint32_t want = wanted.begin[index];
            if (now < want) {
                uint16_t missing = static_cast<uint16_t>(want - now);
                for (auto* node = reinterpret_cast<Trip*>(At<ListNode*>(self, connection::kTrips)->next);
                     reinterpret_cast<ListNode*>(node) != head && missing; node = reinterpret_cast<Trip*>(node->next)) {
                    if (node->kind == kind && node->index == index && node->source == 5) {
                        node->count += missing;
                        missing = 0;
                    }
                }
                if (!missing)
                    continue;
                ListNode* const list = At<ListNode*>(self, connection::kTrips);
                auto* const trip = static_cast<Trip*>(reinterpret_cast<NewFn>(addr::SC4_stl_allocate)(sizeof(Trip)));
                // The game copies +0x28, +0x2A and +0x30 from stack it never
                // writes; ours are 0.
                const uint32_t empty[3] = {0, 0, 0};
                const auto Copy = reinterpret_cast<CopyVectorFn>(addr::SC4ByteVector_CopyConstruct);
                Copy(trip->vectorA, empty);
                Copy(trip->vectorB, empty);
                trip->kind = static_cast<uint8_t>(kind);
                trip->index = static_cast<uint8_t>(index);
                trip->count = missing;
                trip->mode = 4;
                trip->source = 0;
                trip->word28 = 0;
                trip->word2A = 0;
                trip->zero2C = 0;
                trip->value30 = 0;
                ListNode* const last = list->prev;
                trip->next = list;
                trip->prev = last;
                last->next = reinterpret_cast<ListNode*>(trip);
                list->prev = reinterpret_cast<ListNode*>(trip);
            } else if (now > want) {
                uint32_t extra = now - want;
                for (auto* node = reinterpret_cast<Trip*>(At<ListNode*>(self, connection::kTrips)->next);
                     reinterpret_cast<ListNode*>(node) != head && extra;) {
                    if (node->kind == kind && node->index == index && node->source == 5) {
                        if (node->count > extra) {
                            node->count -= extra;
                            extra = 0;
                        } else {
                            auto* const next = reinterpret_cast<Trip*>(node->next);
                            extra -= node->count;
                            unlink(node);
                            FreeTrip(node);
                            node = next;
                            continue;
                        }
                    }
                    node = reinterpret_cast<Trip*>(node->next);
                }
                for (auto* node = reinterpret_cast<Trip*>(At<ListNode*>(self, connection::kTrips)->next);
                     reinterpret_cast<ListNode*>(node) != head && extra;) {
                    if (node->kind == kind && node->index == index) {
                        if (node->count > extra) {
                            node->count -= extra;
                            node = reinterpret_cast<Trip*>(node->next);
                            extra = 0;
                            continue;
                        }
                        auto* const next = reinterpret_cast<Trip*>(node->next);
                        extra -= node->count;
                        unlink(node);
                        FreeTrip(node);
                        node = next;
                        continue;
                    }
                    node = reinterpret_cast<Trip*>(node->next);
                }
            }
        }
        if (wanted.begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(wanted.begin);
    }
}

// --- Installation -------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kDealSlots[] = {
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 0, 0x006CE260, DealQueryInterface),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 2, 0x006CE2C0, DealRelease),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 3, 0x006CE090, SetCitySide),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 4, 0x006CE0B0, GetCitySide),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 5, 0x006CE0D0, SetOtherSide),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 7, 0x006CF330, DealCity),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 9, 0x006CE100, SetType),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 11, 0x006CF960, SetAmount),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 15, 0x006CE250, SetLastIncAmount),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 17, 0x006CE240, SetIncAmount),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 19, 0x006CE230, SetIncAmount2),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 21, 0x006CE0F0, SetAmountUsed),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 22, 0x006CE110, GetCost),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 23, 0x006CF9C0, UpdateCost),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 25, 0x006CE120, SetDate<deal::kStart>),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 26, 0x006CE140, GetDate<deal::kExpiration>),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 27, 0x006CE160, SetDate<deal::kExpiration>),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 28, 0x006CE180, GetDate<deal::kCurrent>),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 29, 0x006CE1A0, SetDate<deal::kCurrent>),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 30, 0x006CE1C0, GetDate<deal::kSuspension>),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 31, 0x006CE1E0, SetDate<deal::kSuspension>),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 32, 0x006CE2E0, Copy),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 33, 0x006CF360, UpdateState),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 34, 0x006CE610, Cancel),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 35, 0x006CE640, Activate),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 37, 0x006CFA50, SetFlags),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 38, 0x006CE200, CheckFlagsAnd),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 39, 0x006CE220, CheckFlagsOr),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable, 40, 0x006CF3E0, Clone),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable_4, 3, 0x006CE400, DealWrite),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable_4, 4, 0x006CE4E0, DealRead),
    OPENSC4_SLOT(addr::cSC4NeighborDeal_vftable_4, 5, 0x006CE080, DealClassId),
};

const Slot kConnectionSlots[] = {
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 0, 0x006CE000, ConnectionQueryInterface),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 2, 0x006CE060, ConnectionRelease),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 6, 0x006D46F0, GetConnectionCell),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 7, 0x006CF1B0, GetConnectionInnerCell),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 8, 0x006D4710, GetConnectionTile),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 9, 0x006CF180, ConnectionCity),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 10, 0x006CF250, Repair),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 11, 0x006D4730, GetVisited),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 12, 0x006D4740, SetVisited),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 13, 0x006D4760, GetIsActive),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable, 14, 0x006D4770, SetIsActive),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable_4, 3, 0x006CF080, ConnectionWrite),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable_4, 4, 0x006CF0F0, ConnectionRead),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable_4, 5, 0x006CDFF0, ConnectionClassId),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable_1C, 3, 0x006D4790, TripsList),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable_1C, 4, 0x006D4C10, UpdateTrips),
    OPENSC4_SLOT(addr::cSC4NeighborConnection_vftable_1C, 5, 0x006D4820, SourceRect),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        if (*entry != slot.original) {
            log::Write("neighbors %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(*entry),
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
    return true;
}

bool InstallDeal(registry::Mode) { return game::IsBuild641() && SwapSlots(kDealSlots, "deal"); }
bool InstallConnection(registry::Mode) { return game::IsBuild641() && SwapSlots(kConnectionSlots, "connection"); }

const bool g_registeredDeal = registry::Register({
    "simulators.neighbors.deal",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborDeal_vftable,
    "Our cSC4NeighborDeal functions: tiles, amounts, cost, dates, flags, state, clone, save record "
    "(docs/neighbors.md)",
    false,
    InstallDeal,
});

const bool g_registeredConnection = registry::Register({
    "simulators.neighbors.connection",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborConnection_vftable,
    "Our cSC4NeighborConnection functions: cells, tiles, the city across, flags, trips, save record "
    "(docs/neighbors.md)",
    false,
    InstallConnection,
});


// --- Our objects ------------------------------------------------------------------

using HolderCtorFn = void*(__thiscall*)(void* holder);
using HolderDtorFn = void(__thiscall*)(void* holder);
using DtorFn = void*(__fastcall*)(char* self, void*, uint32_t flags);

// A part's QueryInterface, AddRef and Release through the object's.
template <std::size_t Offset, std::size_t RefCount, uint8_t (*Query)(char*, uint32_t, void**),
          uint32_t (*Drop)(char*)>
struct Part {
    static uint8_t __fastcall QueryInterface(char* part, void*, uint32_t iid, void** object)
    {
        return Query(part - Offset, iid, object);
    }
    static uint32_t __fastcall AddRef(char* part, void*) { return ++At<uint32_t>(part - Offset, RefCount); }
    static uint32_t __fastcall Release(char* part, void*) { return Drop(part - Offset); }
};

uint8_t DealQuery(char* self, uint32_t iid, void** object) { return DealQueryInterface(self, nullptr, iid, object); }
uint32_t DealDrop(char* self) { return DealRelease(self, nullptr); }
uint8_t ConnectionQuery(char* self, uint32_t iid, void** object)
{
    return ConnectionQueryInterface(self, nullptr, iid, object);
}
uint32_t ConnectionDrop(char* self) { return ConnectionRelease(self, nullptr); }

constexpr int kDealSlotsMain = 41, kDealSlots4 = 6, kDealSlots8 = 25;
void* g_dealVtable[kDealSlotsMain];
void* g_dealVtable4[kDealSlots4];
void* g_dealVtable8[kDealSlots8];
constexpr int kConnectionSlotsMain = 15, kConnectionSlots4 = 6, kConnectionSlots8 = 25, kConnectionSlots1C = 6;
void* g_connectionVtable[kConnectionSlotsMain];
void* g_connectionVtable4[kConnectionSlots4];
void* g_connectionVtable8[kConnectionSlots8];
void* g_connectionVtable1C[kConnectionSlots1C];

void SetDealVtables(char* self)
{
    At<void**>(self, 0x0) = g_dealVtable;
    At<void**>(self, 0x4) = g_dealVtable4;
    At<void**>(self, 0x8) = g_dealVtable8;
}

void SetConnectionVtables(char* self)
{
    At<void**>(self, 0x0) = g_connectionVtable;
    At<void**>(self, 0x4) = g_connectionVtable4;
    At<void**>(self, 0x8) = g_connectionVtable8;
    At<void**>(self, connection::kTrafficSource) = g_connectionVtable1C;
}

// As the game's deal constructor (0x006CF270): the property holder base,
// counts and cost 0, the four dates today (by the clock, as the game's),
// flags 0x81 (pending, not added).
void* __fastcall ConstructDeal(char* self, void*)
{
    reinterpret_cast<HolderCtorFn>(addr::SC4TrafficObject_Construct)(self + 8);
    SetDealVtables(self);
    At<uint32_t>(self, deal::kRefCount) = 0;
    At<uint32_t>(self, deal::kType) = 0;
    for (std::size_t offset = deal::kAmount; offset <= deal::kAmountUsed; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<int64_t>(self, deal::kCost) = 0;
    const auto Default = reinterpret_cast<DateDefaultFn>(addr::cRZDate_DefaultBase);
    Default(self + deal::kStart);
    Default(self + deal::kExpiration);
    Default(self + deal::kCurrent);
    Default(self + deal::kSuspension);
    At<uint32_t>(self, deal::kFlags) = 0x81;
    return self;
}

// As the game's (0x006CF2F0).
void* __fastcall DestroyDeal(char* self, void*, uint32_t flags)
{
    SetDealVtables(self);
    reinterpret_cast<HolderDtorFn>(addr::SC4PropertyHolder_Destroy)(self + 8);
    if (flags & 1)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
    return self;
}

void* __fastcall DestroyDealAt8(char* part, void*, uint32_t flags) { return DestroyDeal(part - 8, nullptr, flags); }

uint32_t __fastcall DealWriteAt8(char* part, void*, void* stream) { return DealWrite(part - 4, nullptr, stream); }
uint32_t __fastcall DealReadAt8(char* part, void*, void* stream) { return DealRead(part - 4, nullptr, stream); }

// As the game's connection constructor (0x006D4680): the property holder
// base, the fields 0, an empty trips list, trips to bring up to date.
void* __fastcall ConstructConnection(char* self, void*)
{
    reinterpret_cast<HolderCtorFn>(addr::SC4TrafficObject_Construct)(self + 8);
    SetConnectionVtables(self);
    At<uint32_t>(self, connection::kRefCount) = 0;
    At<int32_t>(self, connection::kX) = 0;
    At<int32_t>(self, connection::kZ) = 0;
    At<int32_t>(self, connection::kTileX) = 0;
    At<int32_t>(self, connection::kTileZ) = 0;
    At<uint8_t>(self, connection::kFlags) = 0;
    At<void*>(self, connection::kTrips) = nullptr;
    auto* const head = static_cast<ListNode*>(reinterpret_cast<NewFn>(addr::SC4_stl_allocate)(0x34));
    head->next = head;
    head->prev = head;
    At<ListNode*>(self, connection::kTrips) = head;
    At<uint8_t>(self, connection::kTripsDirty) = 1;
    return self;
}

// As the game's (0x006D4BB0).
void* __fastcall DestroyConnection(char* self, void*, uint32_t flags)
{
    SetConnectionVtables(self);
    reinterpret_cast<ListFn>(addr::SC4TripList_Clear)(self + connection::kTrips);
    if (void* const head = At<void*>(self, connection::kTrips))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(head);
    reinterpret_cast<HolderDtorFn>(addr::SC4PropertyHolder_Destroy)(self + 8);
    if (flags & 1)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
    return self;
}

void* __fastcall DestroyConnectionAt8(char* part, void*, uint32_t flags)
{
    return DestroyConnection(part - 8, nullptr, flags);
}

uint32_t __fastcall ConnectionWriteAt8(char* part, void*, void* stream) { return ConnectionWrite(part - 4, nullptr, stream); }
uint32_t __fastcall ConnectionReadAt8(char* part, void*, void* stream) { return ConnectionRead(part - 4, nullptr, stream); }

uint32_t __fastcall DealAddRef(char* self, void*) { return ++At<uint32_t>(self, deal::kRefCount); }
uint32_t __fastcall ConnectionAddRef(char* self, void*) { return ++At<uint32_t>(self, connection::kRefCount); }

void BuildVtables()
{
    std::memcpy(g_dealVtable, reinterpret_cast<const void*>(addr::cSC4NeighborDeal_vftable), sizeof(g_dealVtable));
    std::memcpy(g_dealVtable4, reinterpret_cast<const void*>(addr::cSC4NeighborDeal_vftable_4), sizeof(g_dealVtable4));
    std::memcpy(g_dealVtable8, reinterpret_cast<const void*>(addr::cSC4NeighborDeal_vftable_8), sizeof(g_dealVtable8));
    g_dealVtable[0] = reinterpret_cast<void*>(&DealQueryInterface);
    g_dealVtable[1] = reinterpret_cast<void*>(&DealAddRef);
    g_dealVtable[2] = reinterpret_cast<void*>(&DealRelease);
    using Deal4 = Part<4, deal::kRefCount, DealQuery, DealDrop>;
    using Deal8 = Part<8, deal::kRefCount, DealQuery, DealDrop>;
    g_dealVtable4[0] = reinterpret_cast<void*>(&Deal4::QueryInterface);
    g_dealVtable4[1] = reinterpret_cast<void*>(&Deal4::AddRef);
    g_dealVtable4[2] = reinterpret_cast<void*>(&Deal4::Release);
    g_dealVtable8[0] = reinterpret_cast<void*>(&Deal8::QueryInterface);
    g_dealVtable8[1] = reinterpret_cast<void*>(&Deal8::AddRef);
    g_dealVtable8[2] = reinterpret_cast<void*>(&Deal8::Release);
    g_dealVtable8[22] = reinterpret_cast<void*>(&DestroyDealAt8);
    g_dealVtable8[23] = reinterpret_cast<void*>(&DealWriteAt8);
    g_dealVtable8[24] = reinterpret_cast<void*>(&DealReadAt8);

    std::memcpy(g_connectionVtable, reinterpret_cast<const void*>(addr::cSC4NeighborConnection_vftable),
                sizeof(g_connectionVtable));
    std::memcpy(g_connectionVtable4, reinterpret_cast<const void*>(addr::cSC4NeighborConnection_vftable_4),
                sizeof(g_connectionVtable4));
    std::memcpy(g_connectionVtable8, reinterpret_cast<const void*>(addr::cSC4NeighborConnection_vftable_8),
                sizeof(g_connectionVtable8));
    std::memcpy(g_connectionVtable1C, reinterpret_cast<const void*>(addr::cSC4NeighborConnection_vftable_1C),
                sizeof(g_connectionVtable1C));
    g_connectionVtable[0] = reinterpret_cast<void*>(&ConnectionQueryInterface);
    g_connectionVtable[1] = reinterpret_cast<void*>(&ConnectionAddRef);
    g_connectionVtable[2] = reinterpret_cast<void*>(&ConnectionRelease);
    using Connection4 = Part<4, connection::kRefCount, ConnectionQuery, ConnectionDrop>;
    using Connection8 = Part<8, connection::kRefCount, ConnectionQuery, ConnectionDrop>;
    using Connection1C = Part<connection::kTrafficSource, connection::kRefCount, ConnectionQuery, ConnectionDrop>;
    g_connectionVtable4[0] = reinterpret_cast<void*>(&Connection4::QueryInterface);
    g_connectionVtable4[1] = reinterpret_cast<void*>(&Connection4::AddRef);
    g_connectionVtable4[2] = reinterpret_cast<void*>(&Connection4::Release);
    g_connectionVtable8[0] = reinterpret_cast<void*>(&Connection8::QueryInterface);
    g_connectionVtable8[1] = reinterpret_cast<void*>(&Connection8::AddRef);
    g_connectionVtable8[2] = reinterpret_cast<void*>(&Connection8::Release);
    g_connectionVtable8[22] = reinterpret_cast<void*>(&DestroyConnectionAt8);
    g_connectionVtable8[23] = reinterpret_cast<void*>(&ConnectionWriteAt8);
    g_connectionVtable8[24] = reinterpret_cast<void*>(&ConnectionReadAt8);
    g_connectionVtable1C[0] = reinterpret_cast<void*>(&Connection1C::QueryInterface);
    g_connectionVtable1C[1] = reinterpret_cast<void*>(&Connection1C::AddRef);
    g_connectionVtable1C[2] = reinterpret_cast<void*>(&Connection1C::Release);
}

void EnsureVtables()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
}

cIGZUnknown* CreateDeal()
{
    EnsureVtables();
    auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(deal::kSize));
    return self ? reinterpret_cast<cIGZUnknown*>(ConstructDeal(self, nullptr)) : nullptr;
}

cIGZUnknown* CreateConnection()
{
    EnsureVtables();
    auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(connection::kSize));
    return self ? reinterpret_cast<cIGZUnknown*>(ConstructConnection(self, nullptr)) : nullptr;
}

// The game's constructors are also called directly (proposals, clones,
// new connections), so they are detoured to ours.
using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalDealCtor = nullptr, g_originalConnectionCtor = nullptr;

void* __fastcall ConstructDealHook(char* self, void*)
{
    EnsureVtables();
    return ConstructDeal(self, nullptr);
}

void* __fastcall ConstructConnectionHook(char* self, void*)
{
    EnsureVtables();
    return ConstructConnection(self, nullptr);
}

bool ApplyObjects(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* dealCtor = addr_check::Find(addr::cSC4NeighborDeal_cSC4NeighborDeal);
    const addr::Entry* connectionCtor = addr_check::Find(addr::cSC4NeighborConnection_cSC4NeighborConnection);
    return dealCtor && connectionCtor && hook::Install(*dealCtor, &ConstructDealHook, &g_originalDealCtor) &&
           hook::Install(*connectionCtor, &ConstructConnectionHook, &g_originalConnectionCtor) &&
           director::AddClass(0x2A134F42, CreateDeal) && director::AddClass(0x0A134F24, CreateConnection);
}

const bool g_registeredObjects = registry::Register({
    "simulators.neighbors.classobjects",
    "simulators/neighbors",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    0x2A134F42,
    "Our cSC4NeighborDeal and cSC4NeighborConnection objects under their class IDs, and their constructors "
    "(docs/neighbors.md)",
    false,
    ApplyObjects,
});

} // namespace
} // namespace opensc4::neighbors_sim
