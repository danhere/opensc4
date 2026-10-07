// Pass-through pollution simulator. Every cISC4PollutionSimulator and
// cIGZSerializable call is counted and forwarded to the original object;
// QueryInterface for any other interface (message target, the simulator tick
// interface) returns the original's own pointer, so those paths are untouched.
//
// cIGZSerializable has to be proxied too: when a saved city loads, the save
// loader creates each simulator by class ID asking for cIGZSerializable, then
// queries that pointer for the simulator interface. Forwarding it to the
// original would hand the city the original object.
//
// What it tests: (1) whether a plugin director's registration wins when the
// city asks GZCOM for the simulator, and (2) whether any code reads simulator
// fields through the pointer it gets back. This object's layout is nothing
// like the original's, so such reads would see the wrong data.
#include "core/pollution_proxy.h"

#include "core/director.h"
#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZSerializable.h>
#include <cISC4PollutionSimulator.h>

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>

namespace opensc4::pollution_proxy {
namespace {

// The game's own type-1 factory for cSC4PollutionSimulator (build 641):
// operator new(0x238), then the constructor at 0x006E56E0.
using FactoryFn = cIGZUnknown* (*)();
const auto kOriginalFactory = reinterpret_cast<FactoryFn>(addr::cSC4PollutionSimulator_Create);

// cISC4PollutionSimulator methods in vtable order (gzcom-dll header; checked
// against the game's 71-slot vtable at 0x00AB2630: every slot's argument size
// matches the RET of the game's function). The eight "...PrevMonth/LastMonth"
// garbage figures return 64-bit values in EDX:EAX in the game (0x006E4F20 to
// 0x006E4F90; 0x006E4F60 converts a double with __ftol), and the monthly
// garbage code (0x006D3015) reads all 64 bits; gzcom-dll declares them
// int32_t. They are int64_t here.
#define OPENSC4_POLLUTION_METHODS(X)                                                                         \
    X(bool, Init, (), ())                                                                                    \
    X(bool, Shutdown, (), ())                                                                                \
    X(cISC4SimGrid<short>*, GetPollutionGrid, (uint32_t type), (type))                                      \
    X(uint16_t, GetPollutionValue, (uint32_t type, uint32_t x, uint32_t z), (type, x, z))                   \
    X(bool, GetAirValue, (uint32_t x, uint32_t z, int32_t& value), (x, z, value))                           \
    X(bool, GetWaterValue, (uint32_t x, uint32_t z, int32_t& value), (x, z, value))                         \
    X(bool, GetGarbageValue, (uint32_t x, uint32_t z, int32_t& value), (x, z, value))                       \
    X(int32_t, GetAverageAirValue, (), ())                                                                   \
    X(int32_t, GetAverageWaterValue, (), ())                                                                 \
    X(int32_t, GetAverageGarbageValue, (), ())                                                               \
    X(bool, IsAirPolluted, (uint32_t x, uint32_t z), (x, z))                                                \
    X(bool, IsWaterPolluted, (uint32_t x, uint32_t z), (x, z))                                              \
    X(bool, IsGarbageAccumulated, (uint32_t x, uint32_t z), (x, z))                                         \
    X(bool, IsRadioactive, (uint32_t x, uint32_t z), (x, z))                                                \
    X(bool, GetWorstAirTract, (uint32_t& x, uint32_t& z, int32_t& value), (x, z, value))                    \
    X(bool, GetWorstWaterTract, (uint32_t& x, uint32_t& z, int32_t& value), (x, z, value))                  \
    X(bool, GetWorstGarbageTract, (uint32_t& x, uint32_t& z, int32_t& value), (x, z, value))                \
    X(uint32_t, GetGarbageSupply, (), ())                                                                    \
    X(int32_t, GetLandfillTileCapacity, (), ())                                                              \
    X(int32_t, GetGarbageCapacity, (), ())                                                                   \
    X(bool, IsGarbageUncollected, (), ())                                                                    \
    X(int32_t, GetTotalGarbageUncollected, (), ())                                                           \
    X(bool, SetIsRadioactive, (uint32_t x, uint32_t z, bool radioactive), (x, z, radioactive))              \
    X(bool, SetAirValue, (uint32_t x, uint32_t z, int16_t value), (x, z, value))                            \
    X(bool, SetWaterValue, (uint32_t x, uint32_t z, int16_t value), (x, z, value))                          \
    X(bool, SetGarbageValue, (uint32_t x, uint32_t z, int16_t value), (x, z, value))                        \
    X(bool, AddToGarbageSupply, (int32_t garbage), (garbage))                                               \
    X(bool, SetGarbageCapacity, (int32_t capacity), (capacity))                                             \
    X(int32_t, GetMonthlyIncineratorCapacity, (), ())                                                        \
    X(int32_t, GetMonthlyWasteToEnergyCapacity, (), ())                                                      \
    X(int32_t, GetRecyclingCenterEffect, (), ())                                                             \
    X(int64_t, GetGarbageAbductedPrevMonth, (), ())                                                          \
    X(int64_t, GetGarbageProducedLastMonth, (), ())                                                          \
    X(int64_t, GetGarbageImportedLastMonth, (), ())                                                          \
    X(int64_t, GetGarbageExportedLastMonth, (), ())                                                          \
    X(int64_t, GetGarbageRecycledLastMonth, (), ())                                                          \
    X(int64_t, GetGarbageIncineratedLastMonth, (), ())                                                       \
    X(int64_t, GetGarbageConvertedToEnergyLastMonth, (), ())                                                 \
    X(int64_t, GetGarbageLandfilledLastMonth, (), ())                                                        \
    X(uint32_t, GetIncineratorCount, (), ())                                                                 \
    X(uint32_t, GetWasteToEnergyBuildingCount, (), ())                                                       \
    X(uint32_t, GetRecyclingCenterCount, (), ())                                                             \
    X(uint32_t, GetWaterTreatmentPlantCount, (), ())                                                         \
    X(bool, GetGarbageBuildings, (SC4List<cISC4Occupant*>& list), (list))                                   \
    X(uint32_t, GetBuildingGarbageCapacity, (cISC4Occupant* occupant), (occupant))                          \
    X(float, GetGarbageScalingFactor, (), ())                                                                \
    X(bool, IsActiveLandfill, (uint32_t x, uint32_t z), (x, z))                                             \
    X(bool, SetIsActiveLandfill, (uint32_t x, uint32_t z, bool active), (x, z, active))                     \
    X(int64_t, GarbagePutInLandfill, (int32_t garbage), (garbage))                                          \
    X(bool, GetMaxCapacity, (uint32_t property, cISC4Occupant* occupant, uint32_t& result),                  \
      (property, occupant, result))                                                                          \
    X(bool, GetCurrentCapacity, (uint32_t property, cISC4Occupant* occupant, uint32_t& result),              \
      (property, occupant, result))                                                                          \
    X(bool, GetRecyclingCenterMaxCapacity, (cISC4Occupant* occupant, SC4Percentage& capacity),               \
      (occupant, capacity))                                                                                  \
    X(bool, GetRecyclingCenterCurrentCapacity, (cISC4Occupant* occupant, SC4Percentage& capacity),           \
      (occupant, capacity))                                                                                  \
    X(bool, GetCurrentOccupantCondition, (cISC4Occupant* occupant, SC4Percentage& capacity),                 \
      (occupant, capacity))                                                                                  \
    X(bool, GetCurrentOccupantEfficiency, (cISC4Occupant* occupant, SC4Percentage& capacity),                \
      (occupant, capacity))                                                                                  \
    X(bool, GetIncineratorUsedCapacity, (cISC4Occupant* occupant, uint32_t& capacity), (occupant, capacity)) \
    X(bool, GetWasteToEnergyUsedCapacity, (cISC4Occupant* occupant, uint32_t& capacity),                     \
      (occupant, capacity))                                                                                  \
    X(bool, GetIncineratorCurrentCapacity, (cISC4Occupant* occupant, uint32_t& capacity),                    \
      (occupant, capacity))                                                                                  \
    X(bool, GetWasteToEnergyCurrentCapacity, (cISC4Occupant* occupant, uint32_t& capacity),                  \
      (occupant, capacity))                                                                                  \
    X(bool, GetWaterTreatmentMaxCapacity, (cISC4Occupant* occupant, uint32_t& capacity),                     \
      (occupant, capacity))                                                                                  \
    X(bool, GetWaterTreatmentCurrentCapacity, (cISC4Occupant* occupant, uint32_t& capacity),                 \
      (occupant, capacity))                                                                                  \
    X(bool, GetWaterTreatmentUsedCapacity, (cISC4Occupant* occupant, uint32_t& capacity),                    \
      (occupant, capacity))                                                                                  \
    X(bool, CalcOccupantMaintenanceCost, (cISC4Occupant* occupant, uint64_t& cost), (occupant, cost))       \
    X(void, AddRadiationToCity, (cS3DVector3& center, float radiusX, float radiusZ),                         \
      (center, radiusX, radiusZ))                                                                            \
    X(int32_t, GetMaxAirPollutionLevelForUI, (), ())                                                         \
    X(int32_t, GetMaxWaterPollutionLevelForUI, (), ())                                                       \
    X(int32_t, GetMaxGarbagePollutionLevelForUI, (), ())                                                     \
    X(int32_t, GetMaxRadiationPollutionLevelForUI, (), ())

// cIGZSerializable methods.
#define OPENSC4_SERIALIZABLE_METHODS(X)                     \
    X(bool, Write, (cIGZOStream & out), (out))              \
    X(bool, Read, (cIGZIStream & in), (in))                 \
    X(uint32_t, GetGZCLSID, (), ())

enum class Method : int {
#define OPENSC4_ENUM(ret, name, params, args) name,
    OPENSC4_POLLUTION_METHODS(OPENSC4_ENUM)
    OPENSC4_SERIALIZABLE_METHODS(OPENSC4_ENUM)
#undef OPENSC4_ENUM
    Count
};
static_assert(static_cast<int>(Method::Count) == 68 + 3,
              "cISC4PollutionSimulator has 68 methods and cIGZSerializable 3 in build 641");

constexpr const char* kMethodNames[] = {
#define OPENSC4_NAME(ret, name, params, args) #name,
    OPENSC4_POLLUTION_METHODS(OPENSC4_NAME)
    OPENSC4_SERIALIZABLE_METHODS(OPENSC4_NAME)
#undef OPENSC4_NAME
};

std::atomic<uint32_t> g_calls[static_cast<int>(Method::Count)];
std::atomic<uint32_t> g_liveProxies{0};
std::atomic<const void*> g_lastCreated{nullptr};

// `caller` is the return address into the game, logged with the first call.
void CountCall(Method m, void* caller)
{
    if (g_calls[static_cast<int>(m)].fetch_add(1, std::memory_order_relaxed) == 0)
        log::Write("pollution proxy: first call to %s from %p", kMethodNames[static_cast<int>(m)], caller);
}

void LogCallCounts(const char* when)
{
    log::Write("pollution proxy: call counts (%s):", when);
    for (int i = 0; i < static_cast<int>(Method::Count); ++i) {
        const uint32_t n = g_calls[i].load(std::memory_order_relaxed);
        if (n)
            log::Write("  %-40s %u", kMethodNames[i], n);
    }
}

// The game's cISC4PollutionSimulator as the list above declares it: the same
// slots and interface ID as gzcom-dll's class, with the game's return types.
class PollutionSimulator641 : public cIGZUnknown {
public:
#define OPENSC4_DECLARE(ret, name, params, args) virtual ret name params = 0;
    OPENSC4_POLLUTION_METHODS(OPENSC4_DECLARE)
#undef OPENSC4_DECLARE
};

class PollutionSimulatorProxy final : public PollutionSimulator641, public cIGZSerializable {
public:
    // Takes over one reference to each of `inner` and `innerSerializable`.
    PollutionSimulatorProxy(PollutionSimulator641* inner, cIGZSerializable* innerSerializable)
        : inner_(inner), innerSerializable_(innerSerializable)
    {
        g_liveProxies.fetch_add(1);
    }

    bool QueryInterface(uint32_t iid, void** object) override
    {
        if (iid == kIid || iid == GZIID_cIGZUnknown) {
            *object = static_cast<PollutionSimulator641*>(this);
            AddRef();
            return true;
        }
        if (iid == GZIID_cIGZSerializable) {
            *object = static_cast<cIGZSerializable*>(this);
            AddRef();
            return true;
        }
        // Everything else is served by the original object.
        const bool found = inner_->QueryInterface(iid, object);
        log::Write("pollution proxy: QueryInterface(0x%08X) forwarded to original: %s", iid,
                   found ? "found" : "not supported");
        return found;
    }

    uint32_t AddRef() override { return ++refs_; }

    uint32_t Release() override
    {
        const uint32_t refs = --refs_;
        if (refs == 0) {
            LogCallCounts("proxy released");
            innerSerializable_->Release();
            inner_->Release();
            g_liveProxies.fetch_sub(1);
            const void* self = static_cast<PollutionSimulator641*>(this);
            g_lastCreated.compare_exchange_strong(self, nullptr);
            delete this;
        }
        return refs;
    }

#define OPENSC4_FORWARD(ret, name, params, args)   \
    ret name params override                       \
    {                                              \
        CountCall(Method::name, _ReturnAddress()); \
        return inner_->name args;                  \
    }
    OPENSC4_POLLUTION_METHODS(OPENSC4_FORWARD)
#undef OPENSC4_FORWARD

#define OPENSC4_FORWARD_SERIALIZABLE(ret, name, params, args) \
    ret name params override                                  \
    {                                                         \
        CountCall(Method::name, _ReturnAddress());            \
        return innerSerializable_->name args;                 \
    }
    OPENSC4_SERIALIZABLE_METHODS(OPENSC4_FORWARD_SERIALIZABLE)
#undef OPENSC4_FORWARD_SERIALIZABLE

private:
    PollutionSimulator641* inner_;
    cIGZSerializable* innerSerializable_;
    std::atomic<uint32_t> refs_{0};
};

} // namespace

cIGZUnknown* Create()
{
    if (!game::IsBuild641()) {
        log::Write("pollution proxy: not build 641, not creating");
        return nullptr;
    }

    cIGZUnknown* original = kOriginalFactory();
    if (!original) {
        log::Write("pollution proxy: original factory returned null");
        return nullptr;
    }
    PollutionSimulator641* inner = nullptr;
    cIGZSerializable* innerSerializable = nullptr;
    if (!original->QueryInterface(kIid, reinterpret_cast<void**>(&inner))) {
        log::Write("pollution proxy: original does not implement cISC4PollutionSimulator");
        original->AddRef();
        original->Release(); // GZCOM factories return objects with no references
        return nullptr;
    }
    if (!original->QueryInterface(GZIID_cIGZSerializable, reinterpret_cast<void**>(&innerSerializable))) {
        log::Write("pollution proxy: original does not implement cIGZSerializable");
        inner->Release();
        return nullptr;
    }

    auto* proxy = new PollutionSimulatorProxy(inner, innerSerializable);
    PollutionSimulator641* result = proxy;
    g_lastCreated = result;
    log::Write("pollution proxy: created proxy %p around original %p (live proxies: %u)",
               static_cast<void*>(result), static_cast<void*>(inner), g_liveProxies.load());
    return result;
}

const void* LastCreated()
{
    return g_lastCreated.load();
}

namespace {

bool Apply(registry::Mode)
{
    return director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.pollution.proxy",
    "simulators/pollution",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Pass-through cISC4PollutionSimulator around the original (docs/experiments/pollution-proxy.md)",
    false,
    Apply,
});

} // namespace

} // namespace opensc4::pollution_proxy
