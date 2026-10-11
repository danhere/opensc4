// game.date_sync: keeps the cities of a region on the same date
// (docs/date-sync.md). An improvement beyond the original, off by default.
//
// Saving an established city remembers its date; loading another
// established city of the same region whose date is earlier moves it
// forward to that date. The rule and the way the date is moved (what the
// SimDate cheat does: the date set to the day before, then a clock step)
// come from sc4-city-date-sync by Nicholas Hayes
// (https://github.com/0xC0000054/sc4-city-date-sync), used under the MIT
// License:
//
//   Copyright (c) 2023, 2024 Nicholas Hayes
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
// Where the mod sends the SimDate cheat (which the game accepts only with
// its debug functionality on, so the mod turns that on for the session),
// this does the cheat's two steps itself; and where the mod follows region
// changes by the order of the region messages, this compares the cities'
// folders.
#include "core/core_sim.h"
#include "core/date_sync_region.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <cIGZDate.h>
#include <cIGZMessage2.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Simulator.h>
#include <cRZBaseString.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <string>

namespace opensc4::game_date_sync {
namespace {

constexpr uint32_t kMsgPostCityInit = 0x26D31EC1;
constexpr uint32_t kMsgPostSave = 0x26C63345;

// The date last saved, and the region it was saved in.
struct Saved {
    std::string region;
    uint32_t dayNumber = 0;
    uint32_t month = 0, day = 0, year = 0;
};
Saved g_saved;

// The established city and its simulator date, or false.
bool Current(cISC4City** city, cIGZDate** date)
{
    cISC4AppPtr app;
    *city = app ? app->GetCity() : nullptr;
    cISC4Simulator* sim = *city && (*city)->GetEstablished() ? (*city)->GetSimulator() : nullptr;
    *date = sim ? sim->GetSimDate() : nullptr;
    return *date != nullptr;
}

std::string RegionOf(cISC4City* city)
{
    cRZBaseString path;
    city->GetCitySaveFilePath(path);
    return date_sync_region::Of(path.ToChar() ? path.ToChar() : "");
}

void OnSave()
{
    cISC4City* city = nullptr;
    cIGZDate* date = nullptr;
    if (!Current(&city, &date))
        return;
    const std::string region = RegionOf(city);
    if (region.empty())
        return;
    g_saved = {region, date->DayNumber(), date->Month(), date->DayOfMonth(), date->Year()};
    log::Write("date sync: saved %u-%02u-%02u for %s", g_saved.year, g_saved.month, g_saved.day,
               region.c_str());
}

void OnCityLoaded()
{
    cISC4City* city = nullptr;
    cIGZDate* date = nullptr;
    if (!Current(&city, &date) || g_saved.region.empty())
        return;
    if (RegionOf(city) != g_saved.region || date->DayNumber() >= g_saved.dayNumber)
        return;
    const uint32_t oldYear = date->Year(), oldMonth = date->Month(), oldDay = date->DayOfMonth();
    // The SimDate cheat sets the date and steps the clock, which moves on
    // to the next day: so the day before.
    cIGZDate* before = nullptr;
    if (!date->Clone(&before) || !before)
        return;
    before->Set(g_saved.month, g_saved.day, g_saved.year);
    *before -= 1;
    date->Set(before->Month(), before->DayOfMonth(), before->Year());
    before->Release();
    core_sim::CallThis<void>(addr::cSC4Simulator_ClockStep, city->GetSimulator());
    log::Write("date sync: city date moved from %u-%02u-%02u to %u-%02u-%02u", oldYear, oldMonth, oldDay,
               g_saved.year, g_saved.month, g_saved.day);
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
        if (type == kMsgPostSave)
            OnSave();
        else if (type == kMsgPostCityInit)
            OnCityLoaded();
        return true;
    }
};

Listener g_listener;

bool Install(registry::Mode)
{
    cIGZMessageServer2Ptr server;
    const bool ok = server && server->AddNotification(&g_listener, kMsgPostSave) &&
                    server->AddNotification(&g_listener, kMsgPostCityInit);
    log::Write("date sync: city messages %s", ok ? "subscribed" : "NOT subscribed");
    return ok;
}

const bool g_registered = registry::Register({
    "game.date_sync",
    "game",
    registry::Kind::Api,
    registry::Phase::PostAppInit,
    0,
    "Keeps the cities of a region on the same date: a city loaded with an earlier date than the last one saved "
    "moves forward to it",
    false,
    Install,
});

} // namespace
} // namespace opensc4::game_date_sync
