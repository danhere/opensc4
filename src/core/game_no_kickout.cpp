// game.no_kickout: buildings no longer throw out their occupants to make room
// for a wealthier group (docs/no-kickout.md). A deliberate change to the
// simulation that is on by default (Dan, 2026-10-11); game.no_kickout =
// original gives the original behaviour back, and the parity harness sets
// that on both sides (scripts/run-parity.sh).
//
// The tract developer reads one tuning value, "kick out lower wealth"
// (property 0x47E2C540 of the exemplar 0x6534284A, 0xE7E2C2DB, 0xE8DA7677),
// into its +0x6C when a city starts (LoadTuning, 0x00706FC0). The method, to
// set that value to false in the resource manager's cached copy of the
// exemplar on the message the application sends before a city's Init
// (0x26D31EC0), is from sc4-no-kick-out by Nicholas Hayes
// (https://github.com/0xC0000054/sc4-no-kick-out), used under the MIT
// License:
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
// Nothing of the game is patched and the tract developer's code is not
// touched, so this works the same with the game's tract developer and with
// ours (simulators.tract.*): both read the exemplar the same way. What the
// developer ended up with is logged after the city's Init.
#include "core/log.h"
#include "core/registry.h"

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <cGZPersistResourceKey.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZPersistResourceManager.h>
#include <cIGZVariant.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISCProperty.h>
#include <cISCPropertyHolder.h>
#include <cRZAutoRefCount.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::game_no_kickout {
namespace {

constexpr uint32_t kMsgPreCityInit = 0x26D31EC0;
constexpr uint32_t kMsgPostCityInit = 0x26D31EC1;
constexpr uint32_t kKickOutLowerWealth = 0x47E2C540;
// The tract developer's copy of the value (docs/tract.md).
constexpr std::size_t kDeveloperKickOut = 0x6C;

// For unattended tests only: OPENSC4_TEST_NO_KICKOUT=observe leaves the
// value alone, so the log shows what the developer loads without the change.
bool ObserveOnly()
{
    char value[16];
    const DWORD n = GetEnvironmentVariableA("OPENSC4_TEST_NO_KICKOUT", value, sizeof(value));
    return n > 0 && n < sizeof(value) && _stricmp(value, "observe") == 0;
}

// The exemplar is loaded here if it was not yet; the resource manager keeps
// loaded exemplars for a while, so the developer's LoadTuning, which comes
// with the city's Init right after this message, gets the same object.
void BeforeCityInit()
{
    const cGZPersistResourceKey key(0x6534284A, 0xE7E2C2DB, 0xE8DA7677);
    cIGZPersistResourceManagerPtr resources;
    cRZAutoRefCount<cISCPropertyHolder> holder;
    if (!resources || !resources->GetResource(key, GZIID_cISCPropertyHolder, holder.AsPPVoid(), 0, nullptr)) {
        log::Write("no_kickout: the tract developer's tuning exemplar did not load; nothing changed");
        return;
    }
    cISCProperty* const property = holder->GetProperty(kKickOutLowerWealth);
    cIGZVariant* const value = property ? property->GetPropertyValue() : nullptr;
    if (!value || value->GetType() != cIGZVariant::Type::Bool) {
        log::Write("no_kickout: the exemplar has no bool property %08X; nothing changed", kKickOutLowerWealth);
        return;
    }
    const bool was = value->GetValBool();
    if (ObserveOnly()) {
        log::Write("no_kickout: the exemplar says kick out lower wealth = %d; left alone (test)", was ? 1 : 0);
        return;
    }
    value->SetValBool(false);
    log::Write("no_kickout: the exemplar's kick out lower wealth was %d, now %d", was ? 1 : 0,
               value->GetValBool() ? 1 : 0);
}

void AfterCityInit()
{
    cISC4AppPtr app;
    cISC4City* const city = app ? app->GetCity() : nullptr;
    const auto* const developer = city ? reinterpret_cast<const uint8_t*>(city->GetTractDeveloper()) : nullptr;
    if (!developer) {
        log::Write("no_kickout: the city has no tract developer to check");
        return;
    }
    const int loaded = developer[kDeveloperKickOut];
    log::Write("no_kickout: the tract developer loaded kick out lower wealth = %d (%s)", loaded,
               registry::ModeOf("simulators.tract.init") == registry::Mode::Ours ? "our LoadTuning"
                                                                               : "the game's LoadTuning");
    if (loaded != 0 && !ObserveOnly())
        log::Write("no_kickout: NOT in effect for this city: the developer read its tuning from another copy");
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
        if (type == kMsgPreCityInit)
            BeforeCityInit();
        else if (type == kMsgPostCityInit)
            AfterCityInit();
        return true;
    }
};

Listener g_listener;

bool Install(registry::Mode)
{
    cIGZMessageServer2Ptr server;
    const bool ok = server && server->AddNotification(&g_listener, kMsgPreCityInit) &&
                    server->AddNotification(&g_listener, kMsgPostCityInit);
    log::Write("no_kickout: city messages %s", ok ? "subscribed" : "NOT subscribed");
    return ok;
}

const bool g_registered = registry::Register({
    "game.no_kickout",
    "game",
    registry::Kind::Api,
    registry::Phase::PostAppInit,
    0,
    "Buildings keep their occupants when a wealthier group wants in (changes the simulation)",
    false,
    Install,
    true, // Dan's choice; the parity harness runs it as the original
});

} // namespace
} // namespace opensc4::game_no_kickout
