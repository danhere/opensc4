// game.bulldoze: three more ways to use the bulldozer, chosen by the keys
// held while it works (docs/bulldoze.md). An improvement beyond the original,
// off by default.
//
//   Ctrl          only trees and other flora
//   Shift         only roads, rails and the other transport networks
//   Ctrl + Shift  everything but the transport networks
//
// The demolishing is the game's own (cSC4Demolition::DemolishRegion): it
// takes an occupant filter, which the bulldoze tool leaves empty, and this
// passes one for the tool's calls. The idea of flora-only and network-only
// bulldozing is from the Bulldoze Extensions plugin by Nicholas Hayes; the
// code and the way of choosing a mode are ours.
#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <cIGZUnknown.h>
#include <cISC4NetworkOccupant.h>
#include <cISC4Occupant.h>
#include <cISC4OccupantFilter.h>

#include <windows.h>

#include <cstdint>
#include <cstdlib>

#include <opensc4/addresses.h>

namespace opensc4::game_bulldoze {
namespace {

enum class Mode { All, Flora, Networks, NotNetworks };

constexpr uint32_t kTypeFlora = 0x74758926;

// The bulldoze tool's code (cSC4ViewInputControlDemolish, vtable 0x00A901A0):
// only its calls of DemolishRegion get a filter.
constexpr std::uintptr_t kToolBegin = 0x004B8830, kToolEnd = 0x004B9E20;

int g_testMode = -1; // OPENSC4_TEST_BULLDOZE_MODE: a mode for every caller

Mode Current()
{
    if (g_testMode >= 0)
        return static_cast<Mode>(g_testMode);
    const bool control = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    return control ? (shift ? Mode::NotNetworks : Mode::Flora) : (shift ? Mode::Networks : Mode::All);
}

// A road, rail or other transport piece; not a water pipe or a power line.
bool IsTransport(cISC4Occupant* occupant)
{
    cISC4NetworkOccupant* piece = nullptr;
    if (!occupant->QueryInterface(GZIID_cISC4NetworkOccupant, reinterpret_cast<void**>(&piece)) || !piece)
        return false;
    const bool transport = !piece->IsOfType(cISC4NetworkOccupant::WaterPipe) &&
                           !piece->IsOfType(cISC4NetworkOccupant::PowerPole);
    piece->Release();
    return transport;
}

bool Includes(Mode mode, cISC4Occupant* occupant)
{
    switch (mode) {
    case Mode::Flora: return static_cast<uint32_t>(occupant->GetType()) == kTypeFlora;
    case Mode::Networks: return IsTransport(occupant);
    case Mode::NotNetworks: return !IsTransport(occupant);
    default: return true;
    }
}

class Filter final : public cISC4OccupantFilter {
public:
    bool QueryInterface(uint32_t riid, void** object) override
    {
        if (riid == GZIID_cIGZUnknown || riid == GZIID_cISC4OccupantFilter) {
            *object = this;
            return true;
        }
        *object = nullptr;
        return false;
    }
    // Static: lives for the whole process.
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }

    bool IsOccupantIncluded(cISC4Occupant* occupant) override { return occupant && Includes(mode, occupant); }
    bool IsOccupantTypeIncluded(uint32_t type) override { return mode != Mode::Flora || type == kTypeFlora; }
    bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }

    Mode mode = Mode::All;
};

Filter g_filter;

using RegionFn = uint8_t(__fastcall*)(void* self, void* edx, uint8_t demolish, void* region, int32_t privilege,
                                      uint32_t flags, uint8_t clearZoned, cISC4OccupantFilter* filter, int64_t* cost,
                                      void* demolished, void* occupant, int32_t a, int32_t b);
RegionFn g_region = nullptr;

uint8_t __fastcall DemolishRegion(void* self, void* edx, uint8_t demolish, void* region, int32_t privilege,
                                  uint32_t flags, uint8_t clearZoned, cISC4OccupantFilter* filter, int64_t* cost,
                                  void* demolished, void* occupant, int32_t a, int32_t b)
{
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if (!filter && (g_testMode >= 0 || (caller >= kToolBegin && caller < kToolEnd))) {
        const Mode mode = Current();
        if (mode != Mode::All) {
            g_filter.mode = mode;
            filter = &g_filter;
        }
    }
    return g_region(self, edx, demolish, region, privilege, flags, clearZoned, filter, cost, demolished, occupant, a, b);
}

using ShownFn = uint8_t(__fastcall*)(void* self, void* edx, cISC4Occupant* occupant);
ShownFn g_shown = nullptr;

uint8_t __fastcall IsOccupantShown(void* self, void* edx, cISC4Occupant* occupant)
{
    if (!g_shown(self, edx, occupant))
        return 0;
    const Mode mode = Current();
    return mode == Mode::All || Includes(mode, occupant) ? 1 : 0;
}

bool Install(registry::Mode)
{
    char value[8];
    if (const DWORD n = GetEnvironmentVariableA("OPENSC4_TEST_BULLDOZE_MODE", value, sizeof(value));
        n > 0 && n < sizeof(value))
        g_testMode = std::atoi(value) & 3;
    const addr::Entry* region = addr_check::Find(addr::cSC4Demolition_DemolishRegion);
    const addr::Entry* shown = addr_check::Find(addr::cSC4ViewInputControlDemolish_IsOccupantShown);
    const bool ok = region && shown && hook::Install(*region, &DemolishRegion, &g_region) &&
                    hook::Install(*shown, &IsOccupantShown, &g_shown);
    log::Write("bulldoze: Ctrl flora only, Shift networks only, Ctrl+Shift all but networks: %s",
               ok ? "installed" : "FAILED");
    return ok;
}

const bool g_registered = registry::Register({
    "game.bulldoze",
    "game",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Demolition_DemolishRegion,
    "Bulldozer modes by held keys: flora only, transport networks only, all but networks",
    false,
    Install,
});

} // namespace
} // namespace opensc4::game_bulldoze
