// Our cSC4Demand (build 641): one demand of the city (an RCI group, jobs, a
// population figure), its supply and demand, the active demand the RCI bars
// show and the caps on it (docs/demand.md, "cSC4Demand"). Swapped into the
// game's two vtables, so every cSC4Demand runs our code.
#include "core/demand_sim.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace opensc4::demand_sim {
namespace {

using namespace demand;

constexpr uint32_t kVersion = 2;
constexpr uint32_t kDemandIid = 0x0797A4E8;      // cISC4Demand
constexpr uint32_t kSerializableIid = 0xE4FDA3D4; // cIGZSerializable
constexpr uint32_t kClassId = 0x8990C12D;         // cSC4Demand
constexpr uint32_t kDemandChanged = 0x426840A0;   // kMsgTypeDemandChanged

using OperatorDeleteFn = void(__cdecl*)(void* p);

float F(void* self, std::size_t offset) { return At<float>(self, offset); }

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The demand changed message (0x426840A0): the demand's ID and its active
// demand in thousandths of its range, through the message server (slot 4).
void SendChanged(void* self, int32_t value)
{
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x14, kDemandChanged);
    Virtual<uint32_t>(message, 0x2C, At<uint32_t>(self, kId));
    Virtual<uint32_t>(message, 0x40, value);
    Virtual<uint8_t>(At<void*>(self, kMessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// --- cISC4Demand ----------------------------------------------------------------

// 0 (0x005D4AF0): itself for cIGZUnknown and cISC4Demand, the part at +4 for
// cIGZSerializable.
uint8_t __fastcall QueryInterface(void* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == kDemandIid) {
        *out = self;
    } else if (iid == kSerializableIid) {
        *out = self ? static_cast<char*>(self) + 4 : nullptr;
    } else {
        return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// 1 (0x004664A0, shared).
uint32_t __fastcall AddRef(void* self, void*)
{
    return ++At<uint32_t>(self, kRefCount);
}

// 2 (0x005D4D90): at the last reference, Shutdown and the deleting
// destructor (slot 34).
uint32_t __fastcall Release(void* self, void*)
{
    int32_t& count = At<int32_t>(self, kRefCount);
    if (count > 1)
        return static_cast<uint32_t>(--count);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x88, 1u);
    return 0;
}

// 3 (0x005D4DC0): unless loaded, the values to their defaults; the city and
// the message server, referenced. Fails if either is missing.
uint8_t __fastcall Init(void* self, void*)
{
    if (At<uint8_t>(self, kInitialized))
        return 1;
    At<uint8_t>(self, kInitialized) = 1;
    if (!Global<uint8_t>(addr::cSC4Demand_FirstInit))
        Global<uint8_t>(addr::cSC4Demand_FirstInit) = 1;
    if (!At<uint8_t>(self, kLoaded)) {
        for (std::size_t offset = kId; offset <= kSaved28; offset += 4)
            At<uint32_t>(self, offset) = 0;
        At<uint32_t>(self, kMax) = 0x7E7FFFFF;
        At<uint32_t>(self, kMin) = 0xFE7FFFFF;
        At<uint32_t>(self, kActive) = 0;
        At<uint8_t>(self, kFlag38) = 1;
        At<uint32_t>(self, kEconomy) = 0;
        At<uint32_t>(self, kTax) = 0;
        At<uint32_t>(self, kCap) = 0;
    }
    if (!At<void*>(self, kCity)) {
        void* const city = Global<void*>(addr::SC4_City);
        At<void*>(self, kCity) = city;
        if (!city)
            return 0;
        Virtual<uint32_t>(city, 0x4);
    }
    if (!At<void*>(self, kMessageServer)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, kMessageServer) = server;
        if (!server)
            return 0;
        Virtual<uint32_t>(server, 0x4);
    }
    return 1;
}

// 4 (0x005D4B50): the two references released.
uint8_t __fastcall Shutdown(void* self, void*)
{
    if (!At<uint8_t>(self, kInitialized))
        return 1;
    At<uint8_t>(self, kInitialized) = 0;
    for (const std::size_t offset : {kCity, kMessageServer}) {
        if (void* const object = At<void*>(self, offset)) {
            Virtual<uint32_t>(object, 0x8);
            At<void*>(self, offset) = nullptr;
        }
    }
    return 1;
}

// 5 (0x005D4B90): unless loaded, the active demand computed (slot 35); the
// demand changed message with it in thousandths of its range, truncated.
uint8_t __fastcall SimulationBegin(void* self, void*)
{
    if (!At<uint8_t>(self, kLoaded))
        Virtual<void>(self, 0x8C, 0u);
    const float active = F(self, kActive);
    const double scaled = !(active < 0.0f) ? static_cast<double>(active) / F(self, kMax) * 1000.0
                                         : static_cast<double>(active) / F(self, kMin) * -1000.0;
    SendChanged(self, Ftol(scaled));
    return 1;
}

// 6 (0x0040CE60, shared) and 7 (0x005D4C30).
uint32_t __fastcall GetId(void* self, void*) { return At<uint32_t>(self, kId); }

uint8_t __fastcall SetId(void* self, void*, uint32_t id)
{
    At<uint32_t>(self, kId) = id;
    return 1;
}

// 8 to 15: float getters (several shared with other classes).
float __fastcall QuerySupplyValue(void* self, void*) { return F(self, kSupply); }
float __fastcall QueryDemandValue(void* self, void*) { return F(self, kDemand); }
float __fastcall QueryNewSupply(void* self, void*) { return F(self, kNewSupply); }
float __fastcall QueryNewDemand(void* self, void*) { return F(self, kNewDemand); }
float __fastcall QueryActiveDemandValue(void* self, void*) { return F(self, kActive); }
float __fastcall QueryEconomyModifier(void* self, void*) { return F(self, kEconomy); }
float __fastcall QueryActiveDemandMax(void* self, void*) { return F(self, kMax); }
float __fastcall QueryActiveDemandMin(void* self, void*) { return F(self, kMin); }

// 16, 17 (0x005D4C60, 0x005D4C70): added to this cycle's supply or demand.
uint8_t __fastcall AddToSupplyValue(void* self, void*, float value)
{
    At<float>(self, kNewSupply) = value + F(self, kNewSupply);
    return 1;
}

uint8_t __fastcall AddToDemandValue(void* self, void*, float value)
{
    At<float>(self, kNewDemand) = value + F(self, kNewDemand);
    return 1;
}

// 18, 19 (0x005D4C80, 0x005D4C90).
uint8_t __fastcall SetSupplyValue(void* self, void*, float value)
{
    At<float>(self, kSupply) = value;
    return 1;
}

uint8_t __fastcall SetDemandValue(void* self, void*, float value)
{
    At<float>(self, kDemand) = value;
    return 1;
}

// 20, 21 (0x005D4CA0, 0x005D4CC0): the maximum no lower than the minimum,
// the minimum no higher than the maximum.
void __fastcall SetActiveDemandMax(void* self, void*, float value)
{
    At<float>(self, kMax) = value;
    if (value < F(self, kMin))
        At<float>(self, kMax) = F(self, kMin);
}

void __fastcall SetActiveDemandMin(void* self, void*, float value)
{
    At<float>(self, kMin) = value;
    if (value > F(self, kMax))
        At<float>(self, kMin) = F(self, kMax);
}

// 22 to 29. The setters return the value in EAX, as the game's.
float __fastcall GetEconomyModifier(void* self, void*) { return F(self, kEconomy); }

uint32_t __fastcall SetEconomyModifier(void* self, void*, uint32_t bits)
{
    At<uint32_t>(self, kEconomy) = bits;
    return bits;
}

float __fastcall GetTaxModifier(void* self, void*) { return F(self, kTax); }

uint32_t __fastcall SetTaxModifier(void* self, void*, uint32_t bits)
{
    At<uint32_t>(self, kTax) = bits;
    return bits;
}

float* __fastcall GetDemandCap(void* self, void*) { return &At<float>(self, kCap); }

void __fastcall SetDemandCap(void* self, void*, const float* cap) { At<float>(self, kCap) = *cap; }

uint32_t __fastcall GetRegionUse(void* self, void*) { return At<uint32_t>(self, kRegionUse); }

uint32_t __fastcall SetRegionUse(void* self, void*, uint32_t use)
{
    At<uint32_t>(self, kRegionUse) = use;
    return use;
}

// 30 (0x005D4CF0): this cycle's additions into supply and demand; the
// active demand recomputed (slot 35, not smoothed). Whether anything was
// added.
uint8_t __fastcall EndOfCycle(void* self, void*)
{
    const uint8_t changed = !(F(self, kNewSupply) == 0.0f) || !(F(self, kNewDemand) == 0.0f) ? 1 : 0;
    const float newSupply = F(self, kNewSupply);
    At<uint32_t>(self, kNewSupply) = 0;
    At<float>(self, kSupply) = newSupply + F(self, kSupply);
    const float newDemand = F(self, kNewDemand);
    At<uint32_t>(self, kNewDemand) = 0;
    At<float>(self, kDemand) = newDemand + F(self, kDemand);
    Virtual<void>(self, 0x8C, 0u);
    return changed;
}

// 31 to 33: debug locks, never used.
void __fastcall DebugLockValue(void*, void*, float) {}
void __fastcall DebugUnlockValue(void*, void*) {}
uint8_t __fastcall DebugIsValueLocked(void*, void*) { return 0; }

// 34 (0x005D4D60): the scalar deleting destructor.
void* __fastcall Destroy(void* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4Demand_vftable;
    At<std::uintptr_t>(self, 4) = addr::cSC4Demand_vftable_4;
    if (flags & 1)
        reinterpret_cast<OperatorDeleteFn>(addr::SC4_operator_delete)(self);
    return self;
}

// Clamps to [min, max] as the game's x87 code does (a NaN stays).
double Clamp(double value, float max, float min)
{
    if (value > max)
        return max;
    if (value < min)
        return min;
    return value;
}

// 35 (0x005D4E60): the active demand. Demand less supply, clamped to the
// range; plus the smaller of (economy + tax) x max and (economy + tax) x
// demand; clamped again, and no higher than max x (1 - cap). Smoothed: it
// moves toward that by at most 7% of the range a call. Then the demand
// changed message with the active demand in thousandths of its range,
// rounded to nearest.
void __fastcall Update(void* self, void*, uint8_t smooth)
{
    const float max = F(self, kMax);
    const float min = F(self, kMin);
    double target = Clamp(static_cast<double>(F(self, kDemand)) - F(self, kSupply), max, min);
    const double modifiers = static_cast<double>(F(self, kTax)) + F(self, kEconomy);
    const float modifiersStored = static_cast<float>(modifiers);
    double boost = modifiers * max;
    const float byDemand = static_cast<float>(static_cast<double>(modifiersStored) * F(self, kDemand));
    if (boost > byDemand)
        boost = byDemand;
    target = Clamp(target + boost, max, min);
    const float capped = static_cast<float>(static_cast<double>(max) - static_cast<double>(max) * F(self, kCap));
    if (target > capped)
        target = capped;
    if (target < min)
        target = min;
    float& active = At<float>(self, kActive);
    if (!smooth) {
        active = static_cast<float>(target);
    } else {
        const float step = static_cast<float>((static_cast<double>(max) - min) * static_cast<double>(0.07f));
        const double change = target - active;
        if (change < step && change > -static_cast<double>(step)) {
            active = static_cast<float>(target);
        } else if (change > 0.0) {
            if (target > 0.0 && active < 0.0f && static_cast<double>(step) + active < 0.0)
                active = 0.0f;
            else
                active = static_cast<float>(static_cast<double>(step) + active);
        } else {
            if (target < 0.0 && active > 0.0f && static_cast<double>(active) - step > 0.0)
                active = 0.0f;
            else
                active = static_cast<float>(static_cast<double>(active) - step);
        }
    }
    // Rounded through the float store of x + 1.5 x 2^23.
    const double scaled = !(active < 0.0f) ? 12582912.0 + static_cast<double>(active) / max * 1000.0
                                         : 12582912.0 - static_cast<double>(active) / min * 1000.0;
    SendChanged(self, static_cast<int32_t>(Bits(static_cast<float>(scaled)) - 0x4B400000u));
}

// --- cIGZSerializable (the part at +4) ------------------------------------------

void* Self(void* part) { return static_cast<char*>(part) - 4; }

// The floats of the record after the ID, in the order written: the minimum
// before the maximum.
constexpr std::size_t kSavedFloats[] = {kSupply, kDemand, kNewSupply, kNewDemand, kSaved24, kSaved28, kMin, kMax, kActive};

uint8_t __fastcall PartQueryInterface(void* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(Self(part), nullptr, iid, out);
}

uint32_t __fastcall PartAddRef(void* part, void*) { return AddRef(Self(part), nullptr); }

uint32_t __fastcall PartRelease(void* part, void*) { return Release(Self(part), nullptr); }

// 3 (0x005D5080): version 2.
uint8_t __fastcall Write(void* part, void*, void* stream)
{
    void* const self = Self(part);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(kVersion));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, kId));
    for (const std::size_t offset : kSavedFloats)
        Virtual<uint8_t>(stream, 0x30, At<float>(self, offset));
    Virtual<uint8_t>(stream, 0x14, At<uint8_t>(self, kFlag38));
    for (const std::size_t offset : {kEconomy, kTax, kCap})
        Virtual<uint8_t>(stream, 0x30, At<float>(self, offset));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// 4 (0x005D5150): versions 0 to 2; version 1 has the cap as a percentage
// byte.
uint8_t __fastcall Read(void* part, void*, void* stream)
{
    void* const self = Self(part);
    At<uint8_t>(self, kLoaded) = 1;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version > kVersion)
        return 0;
    Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, kId));
    for (const std::size_t offset : kSavedFloats)
        Virtual<uint8_t>(stream, 0x30, &At<float>(self, offset));
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint8_t*)>(addr::SC4Stream_ReadBool)(stream, &At<uint8_t>(self, kFlag38));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, kEconomy));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, kTax));
    if (version == 1) {
        uint8_t percent = 0;
        Virtual<uint8_t>(stream, 0x14, &percent);
        At<float>(self, kCap) = static_cast<float>(static_cast<double>(percent) * static_cast<double>(0.01f));
    } else {
        float cap = 0.0f;
        Virtual<uint8_t>(stream, 0x30, &cap);
        At<float>(self, kCap) = cap;
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kPrimary[] = {
    OPENSC4_SLOT(0, 0x005D4AF0, QueryInterface),
    OPENSC4_SLOT(1, 0x004664A0, AddRef),
    OPENSC4_SLOT(2, 0x005D4D90, Release),
    OPENSC4_SLOT(3, 0x005D4DC0, Init),
    OPENSC4_SLOT(4, 0x005D4B50, Shutdown),
    OPENSC4_SLOT(5, 0x005D4B90, SimulationBegin),
    OPENSC4_SLOT(6, 0x0040CE60, GetId),
    OPENSC4_SLOT(7, 0x005D4C30, SetId),
    OPENSC4_SLOT(8, 0x005D4C40, QuerySupplyValue),
    OPENSC4_SLOT(9, 0x005D4C50, QueryDemandValue),
    OPENSC4_SLOT(10, 0x007D6670, QueryNewSupply),
    OPENSC4_SLOT(11, 0x007AA300, QueryNewDemand),
    OPENSC4_SLOT(12, 0x00477850, QueryActiveDemandValue),
    OPENSC4_SLOT(13, 0x005D4AC0, QueryEconomyModifier),
    OPENSC4_SLOT(14, 0x006C2080, QueryActiveDemandMax),
    OPENSC4_SLOT(15, 0x00477840, QueryActiveDemandMin),
    OPENSC4_SLOT(16, 0x005D4C60, AddToSupplyValue),
    OPENSC4_SLOT(17, 0x005D4C70, AddToDemandValue),
    OPENSC4_SLOT(18, 0x005D4C80, SetSupplyValue),
    OPENSC4_SLOT(19, 0x005D4C90, SetDemandValue),
    OPENSC4_SLOT(20, 0x005D4CA0, SetActiveDemandMax),
    OPENSC4_SLOT(21, 0x005D4CC0, SetActiveDemandMin),
    OPENSC4_SLOT(22, 0x005D4AC0, GetEconomyModifier),
    OPENSC4_SLOT(23, 0x006CE230, SetEconomyModifier),
    OPENSC4_SLOT(24, 0x005D4AD0, GetTaxModifier),
    OPENSC4_SLOT(25, 0x007B62D0, SetTaxModifier),
    OPENSC4_SLOT(26, 0x005D4AE0, GetDemandCap),
    OPENSC4_SLOT(27, 0x005D4CE0, SetDemandCap),
    OPENSC4_SLOT(28, 0x00698CF0, GetRegionUse),
    OPENSC4_SLOT(29, 0x006CE0F0, SetRegionUse),
    OPENSC4_SLOT(30, 0x005D4CF0, EndOfCycle),
    OPENSC4_SLOT(31, 0x00648F10, DebugLockValue),
    OPENSC4_SLOT(32, 0x00407990, DebugUnlockValue),
    OPENSC4_SLOT(33, 0x00648EF0, DebugIsValueLocked),
    OPENSC4_SLOT(34, 0x005D4D60, Destroy),
    OPENSC4_SLOT(35, 0x005D4E60, Update),
};

const Slot kSerializable[] = {
    OPENSC4_SLOT(0, 0x005D4D50, PartQueryInterface),
    OPENSC4_SLOT(1, 0x006C2270, PartAddRef),
    OPENSC4_SLOT(2, 0x005D5270, PartRelease),
    OPENSC4_SLOT(3, 0x005D5080, Write),
    OPENSC4_SLOT(4, 0x005D5150, Read),
    OPENSC4_SLOT(5, 0x005D4AB0, GetGZCLSID),
};

#undef OPENSC4_SLOT

// Swaps every slot or none: each must still hold the game's function.
template <std::size_t N>
bool Check(std::uintptr_t address, const Slot (&slots)[N])
{
    const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(address);
    for (const Slot& slot : slots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("demand objects: vtable %08X slot %d holds %08X, expected %08X; not installing",
                       static_cast<unsigned>(address), slot.index, static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool Swap(std::uintptr_t address, const Slot (&slots)[N])
{
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(address);
    DWORD old = 0;
    if (!VirtualProtect(vtable, N * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("demand objects: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    for (const Slot& slot : slots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, N * sizeof(*vtable), old, &old);
    return true;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641() || !Check(addr::cSC4Demand_vftable, kPrimary) ||
        !Check(addr::cSC4Demand_vftable_4, kSerializable))
        return false;
    if (!Swap(addr::cSC4Demand_vftable, kPrimary) || !Swap(addr::cSC4Demand_vftable_4, kSerializable))
        return false;
    log::Write("demand objects: every cSC4Demand slot is ours (36 + 6)");
    return true;
}

const bool g_registered = registry::Register({
    "simulators.demand.demands",
    "simulators/demand",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4Demand_vftable,
    "Our cSC4Demand: every slot of its two vtables (docs/demand.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::demand_sim
