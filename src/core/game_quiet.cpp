// game.quiet.*: three effects of building and demolishing turned off
// (docs/quiet.md), each its own setting, off by default:
//
//   game.quiet.demolish  no demolition animation when a building goes
//   game.quiet.plop      no smoke when the player places a lot
//   game.quiet.network   no sounds from the crews that build roads and rails
//
// What to change is from three plugins by Nicholas Hayes, used under the MIT
// License: sc4-disable-demolition-animation, sc4-disable-lot-plop-smoke and
// sc4-disable-network-construction-sounds
// (https://github.com/0xC0000054/<name>), each with the same notice:
//
//   Copyright (c) 2024 Nicholas Hayes
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit
//   persons to whom the Software is furnished to do so, subject to the
//   following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
//   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
//   USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// The plugins change one instruction each inside a game function. Ours are
// detours at function entries that come to the same thing:
//
//   - the plugin replaces the call at 0x004673BF, the only call of
//     0x004672E0 ("is this occupant too small for a demolition animation?";
//     occupant in ECX, a float out through ESI), by a function that says yes.
//     That call opens cSC4Demolition::DoDemolishOccupantEffect (0x004673B0,
//     slot 10 of the vtable 0x00A88004, never called directly), which on a
//     yes returns at once. Ours replaces that function by one that returns:
//     what the game does for an occupant that is too small;
//   - the plugin changes the `push 1` at 0x004C3181 to `push 0`: the smoke
//     argument of the lot tool's call (0x004C318B) of
//     cSC4ViewInputControlPlaceLot::PlaceLot (0x004C1740; its other caller,
//     0x005C5554, passes a value of its own). Ours wraps PlaceLot and clears
//     that argument when the call returns to 0x004C3190, the lot tool;
//   - the plugin makes the jump at 0x006071FC always taken, so
//     cSC4NetworkConstructionCrew::Init (0x00607190) never stores the sound
//     service (0x00B43CB8) at the crew's +0x18, and a crew without it plays
//     nothing (and does not send its start and end messages, 0x89F10866 and
//     0xA9F10E45). Nothing later in Init reads +0x18 or the service. Ours
//     wraps Init: if it stored the service, the reference is given back and
//     +0x18 put back as it was.
#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <SC4CellRegion.h>
#include <SC4Point.h>
#include <cIGZMessage2.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Demolition.h>
#include <cISC4NetworkManager.h>
#include <cISC4NetworkOccupant.h>
#include <cISC4NetworkTool.h>

#include <windows.h>

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include <opensc4/addresses.h>

namespace opensc4::game_quiet {
namespace {

constexpr uint32_t kMsgPostCityInit = 0x26D31EC1;
constexpr uint32_t kMsgPreCityShutdown = 0x26D31EC2;
// Where the lot tool's call of PlaceLot returns to.
constexpr std::uintptr_t kLotToolReturn = 0x004C3190;
constexpr std::size_t kCrewSound = 0x18;

// How often each wrapped path was taken; logged when a city closes.
uint32_t g_effectsSkipped = 0;
uint32_t g_placeLotCalls = 0, g_placeLotQuiet = 0;
uint32_t g_crewInits = 0, g_crewSilenced = 0;

// cSC4Demolition::DoDemolishOccupantEffect: `ret 0x18`, no result.
void __fastcall DoDemolishOccupantEffect(void*, void*, void*, uint32_t, void*, uint32_t, uint32_t, uint32_t)
{
    if (g_effectsSkipped++ == 0)
        log::Write("quiet: first demolition animation skipped");
}

using PlaceLotFn = uint8_t(__fastcall*)(void* self, void* edx, void* a1, void* a2, uint32_t smoke, void* a4);
PlaceLotFn g_placeLot = nullptr;

// cSC4ViewInputControlPlaceLot::PlaceLot: `ret 0x10`, a bool in AL.
uint8_t __fastcall PlaceLot(void* self, void* edx, void* a1, void* a2, uint32_t smoke, void* a4)
{
    ++g_placeLotCalls;
    if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kLotToolReturn) {
        if (g_placeLotQuiet++ == 0)
            log::Write("quiet: first lot placed by the lot tool without smoke (the tool asked for %u)", smoke);
        smoke = 0;
    }
    return g_placeLot(self, edx, a1, a2, smoke, a4);
}

using CrewInitFn = uint32_t(__fastcall*)(void* self, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                         uint32_t a5, uint32_t a6);
CrewInitFn g_crewInit = nullptr;

// cSC4NetworkConstructionCrew::Init: `ret 0x18`; two of the six arguments
// are floats, handed on as the 32 bits they are.
uint32_t __fastcall CrewInit(void* self, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                             uint32_t a6)
{
    void** const sound = reinterpret_cast<void**>(static_cast<char*>(self) + kCrewSound);
    void* const before = *sound;
    const uint32_t result = g_crewInit(self, edx, a1, a2, a3, a4, a5, a6);
    ++g_crewInits;
    if (void* const stored = *sound; stored != before) {
        *sound = before;
        if (stored) {
            using ReleaseFn = uint32_t(__thiscall*)(void*);
            reinterpret_cast<ReleaseFn>((*static_cast<void***>(stored))[2])(stored);
        }
        if (g_crewSilenced++ == 0)
            log::Write("quiet: first construction crew started without the sound service");
    }
    return result;
}

bool g_demolishOn = false, g_plopOn = false, g_networkOn = false;

bool Has(const char* list, const char* word)
{
    return std::strstr(list, word) != nullptr;
}

// For unattended tests only: OPENSC4_TEST_QUIET="demolish,network" does, when
// a city has started, a demolition of the cells from a quarter to three
// quarters of the city through the game's demolition utility, and lays a
// few roads with the game's road tool. The city is not saved by this.
void Test(cISC4City* city)
{
    char list[64];
    const DWORD n = GetEnvironmentVariableA("OPENSC4_TEST_QUIET", list, sizeof(list));
    if (n == 0 || n >= sizeof(list) || !city)
        return;
    const int32_t cellsX = static_cast<int32_t>(city->CellCountX()), cellsZ = static_cast<int32_t>(city->CellCountZ());
    if (Has(list, "network")) {
        cISC4NetworkManager* const networks = city->GetNetworkManager();
        cISC4NetworkTool* const tool = networks ? networks->GetNetworkTool(cISC4NetworkOccupant::Road, true) : nullptr;
        if (tool) {
            tool->Init();
            for (int i = 1; i <= 6; ++i) {
                const uint32_t z = static_cast<uint32_t>(cellsZ * i / 7);
                const SC4Point<uint32_t> from(static_cast<uint32_t>(cellsX / 8), z);
                const SC4Point<uint32_t> to(static_cast<uint32_t>(cellsX / 8 + 6), z);
                tool->Reset();
                const uint32_t drawn = tool->DrawNetworkLine(from, to, false, cISC4NetworkOccupant::Road);
                const int cost = drawn ? tool->GetCostOfSolution() : 0;
                if (drawn)
                    tool->PlaceAllSegments(true, false, true, cISC4NetworkOccupant::Road);
                log::Write("quiet: test: road (%u,%u)-(%u,%u): drawn %u, cost %d; crews so far %u, %u without sound",
                           from.x, from.y, to.x, to.y, drawn, cost, g_crewInits, g_crewSilenced);
            }
            // The tool is left to the network manager: letting go of it here
            // crashed in its teardown (0x006208D0 with no object).
        } else {
            log::Write("quiet: test: no road tool");
        }
    }
    if (Has(list, "demolish")) {
        cISC4Demolition* const demolition = city->GetDemolitionUtility();
        if (demolition) {
            // The game's cell region (0x24 bytes): the rectangle, then its
            // cell map: vtable, width, height, words per row, rows by x of
            // one bit a cell. All cells set; the memory is ours.
            struct CellRegion {
                int32_t x0, z0, x1, z1;
                std::uintptr_t cellsVtable;
                uint32_t width, height, wordsPerRow;
                uint32_t** rows;
            } region{cellsX / 4, cellsZ / 4, cellsX * 3 / 4, cellsZ * 3 / 4, addr::SC4CellRegion_CellsVtable, 0, 0, 0,
                     nullptr};
            region.width = static_cast<uint32_t>(region.x1 - region.x0 + 1);
            region.height = static_cast<uint32_t>(region.z1 - region.z0 + 1);
            region.wordsPerRow = (region.height + 31) / 32;
            std::vector<uint32_t> bits(region.width * region.wordsPerRow, 0xFFFFFFFFu);
            std::vector<uint32_t*> rows(region.width);
            for (uint32_t x = 0; x < region.width; ++x)
                rows[x] = bits.data() + x * region.wordsPerRow;
            region.rows = rows.data();
            int64_t cost = 0;
            const bool done = demolition->DemolishRegion(true, reinterpret_cast<const SC4CellRegion<int32_t>&>(region),
                                                         0, 1, false, nullptr, &cost, 0, nullptr, 0, 0);
            log::Write("quiet: test: demolished cells (%d,%d)-(%d,%d): %s, cost %lld; animations skipped so far %u",
                       cellsX / 4, cellsZ / 4, cellsX * 3 / 4, cellsZ * 3 / 4, done ? "done" : "refused", cost,
                       g_effectsSkipped);
        }
    }
}

class Listener final : public cIGZMessageTarget2 {
public:
    bool QueryInterface(uint32_t riid, void** object) override
    {
        if (riid == GZCLSID::kcIGZMessageTarget2 || riid == GZIID_cIGZUnknown) {
            *object = static_cast<cIGZMessageTarget2*>(this);
            AddRef();
            return true;
        }
        *object = nullptr;
        return false;
    }
    // Static: lives for the whole process.
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }

    bool DoMessage(cIGZMessage2* message) override
    {
        const uint32_t type = message->GetType();
        if (type == kMsgPostCityInit) {
            cISC4AppPtr app;
            Test(app ? app->GetCity() : nullptr);
        } else if (type == kMsgPreCityShutdown) {
            log::Write("quiet: this session so far: %u demolition animations skipped; PlaceLot called %u times, %u "
                       "by the lot tool (smoke off); %u construction crews started, %u without sound",
                       g_effectsSkipped, g_placeLotCalls, g_placeLotQuiet, g_crewInits, g_crewSilenced);
        }
        return true;
    }
};

Listener g_listener;

// The counts at the end of a city, once for whichever of the three is on.
void Listen()
{
    static bool listening = false;
    if (listening)
        return;
    cIGZMessageServer2Ptr server;
    listening = server && server->AddNotification(&g_listener, kMsgPostCityInit) &&
                server->AddNotification(&g_listener, kMsgPreCityShutdown);
}

bool InstallDemolish(registry::Mode)
{
    const addr::Entry* const entry = addr_check::Find(addr::cSC4Demolition_DoDemolishOccupantEffect);
    void* original = nullptr;
    g_demolishOn = entry && hook::Install(*entry, reinterpret_cast<void*>(&DoDemolishOccupantEffect), &original);
    log::Write("quiet: demolition animations %s", g_demolishOn ? "off" : "NOT turned off (hook failed)");
    if (g_demolishOn)
        Listen();
    return g_demolishOn;
}

bool InstallPlop(registry::Mode)
{
    const addr::Entry* const entry = addr_check::Find(addr::cSC4ViewInputControlPlaceLot_PlaceLot);
    g_plopOn = entry && hook::Install(*entry, &PlaceLot, &g_placeLot);
    log::Write("quiet: lot placing smoke %s", g_plopOn ? "off" : "NOT turned off (hook failed)");
    if (g_plopOn)
        Listen();
    return g_plopOn;
}

bool InstallNetwork(registry::Mode)
{
    const addr::Entry* const entry = addr_check::Find(addr::cSC4NetworkConstructionCrew_Init);
    g_networkOn = entry && hook::Install(*entry, &CrewInit, &g_crewInit);
    log::Write("quiet: network construction sounds %s", g_networkOn ? "off" : "NOT turned off (hook failed)");
    if (g_networkOn)
        Listen();
    return g_networkOn;
}

const bool g_demolishRegistered = registry::Register({
    "game.quiet.demolish",
    "game",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Demolition_DoDemolishOccupantEffect,
    "No demolition animation when a building goes",
    false,
    InstallDemolish,
});

const bool g_plopRegistered = registry::Register({
    "game.quiet.plop",
    "game",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ViewInputControlPlaceLot_PlaceLot,
    "No smoke when the player places a lot",
    false,
    InstallPlop,
});

const bool g_networkRegistered = registry::Register({
    "game.quiet.network",
    "game",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4NetworkConstructionCrew_Init,
    "No sounds from the crews that build roads and rails",
    false,
    InstallNetwork,
});

} // namespace
} // namespace opensc4::game_quiet
