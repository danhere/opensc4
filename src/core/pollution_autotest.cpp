// The pollution simulator's part of the autotest (src/core/autotest_hooks.h).
// Its grids, worst tracts and effect generator are in the fingerprint's main
// line (autotest.cpp).
#include "core/autotest_hooks.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <SC4List.h>
#include <cS3DVector3.h>
#include <cISC4City.h>
#include <cISC4Occupant.h>
#include <cISC4PollutionSimulator.h>
#include <cISC4SimGrid.h>

#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// OPENSC4_TEST_CAPACITY_SCALE=1: one call of the handler of message
// 0x66956810 (the simulator's scheduler never sent it in a test run).
void CapacityScale(cISC4City* city)
{
    cISC4PollutionSimulator* pollution = city->GetPollutionSimulator();
    if (Env("OPENSC4_TEST_CAPACITY_SCALE") == "1" && pollution) {
        using HandlerFn = void(__thiscall*)(void* self);
        reinterpret_cast<HandlerFn>(addr::cSC4PollutionSimulator_UpdateCapacityScale)(pollution);
        log::Write("autotest: UpdateCapacityScale called");
    }
}

// OPENSC4_TEST_GARBAGE_BUILDINGS=1: one GetGarbageBuildings call after
// the load (no simulator calls it in a test run).
void GarbageBuildings(cISC4City* city)
{
    cISC4PollutionSimulator* pollution = city->GetPollutionSimulator();
    if (Env("OPENSC4_TEST_GARBAGE_BUILDINGS") == "1" && pollution) {
        SC4List<cISC4Occupant*> buildings;
        const bool got = pollution->GetGarbageBuildings(buildings);
        log::Write("autotest: GetGarbageBuildings: %u buildings (returned %d)", static_cast<unsigned>(buildings.size()),
                   got ? 1 : 0);
        for (cISC4Occupant* occupant : buildings)
            occupant->Release();
    }
}

// OPENSC4_TEST_RADIATION="x,z,amount,radius": one AddRadiationToCity
// call after the load (only disasters make one otherwise).
void Radiation(cISC4City* city)
{
    cISC4PollutionSimulator* pollution = city->GetPollutionSimulator();
    if (const std::string radiation = Env("OPENSC4_TEST_RADIATION"); !radiation.empty() && pollution) {
        cS3DVector3 center;
        float amount = 0.0f, radius = 0.0f;
        if (std::sscanf(radiation.c_str(), "%f,%f,%f,%f", &center.fX, &center.fZ, &amount, &radius) == 4) {
            pollution->AddRadiationToCity(center, amount, radius);
            // The radiation grid (cSC4PollutionSimulator +0x4C; build 641)
            // is in no fingerprint: an FNV-1a hash of its tract values.
            auto* const grid = *reinterpret_cast<cISC4SimGrid<int16_t>**>(reinterpret_cast<char*>(pollution) + 0x4C);
            uint32_t hash = 2166136261u;
            int32_t nonzero = 0;
            for (int32_t x = 0; x < grid->GetTractCountX(); ++x) {
                for (int32_t z = 0; z < grid->GetTractCountZ(); ++z) {
                    const int16_t value = grid->GetTractValue(x, z);
                    nonzero += value != 0;
                    hash = (hash ^ static_cast<uint16_t>(value)) * 16777619u;
                }
            }
            log::Write("autotest: AddRadiationToCity(%g, %g, %g, %g): radiation grid %08X, %d tracts not 0", center.fX,
                       center.fZ, amount, radius, hash, nonzero);
        }
    }
}

const LoadHook kCapacityScale{"pollution.capacity_scale", 20, &CapacityScale};
const LoadHook kGarbageBuildings{"pollution.garbage_buildings", 30, &GarbageBuildings};
const LoadHook kRadiation{"pollution.radiation", 40, &Radiation};

} // namespace
} // namespace opensc4::autotest
