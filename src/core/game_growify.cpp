// A "growify" cheat that turns plopped buildings into growable lots of a zone
// (docs/growify.md). A tool beyond the original, always there: it does
// nothing until typed, so it needs no setting.
//
// The cheat, its command and the conversion (every plopped lot of a building
// whose exemplar Purpose matches gets the zone on each of its cells, then
// refreshes its zone type and is marked historical) come from sc4-growify by
// Nicholas Hayes (https://github.com/0xC0000054/sc4-growify), used under the
// MIT License:
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
// The answer comes in a Windows message box, as for the "seed" cheat, where
// the mod uses the game's notification dialog.
#include "core/game_growify.h"

#include "core/growify_command.h"
#include "core/log.h"

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <SC4Rect.h>
#include <cIGZCheatCodeManager.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZString.h>
#include <cIGZVariant.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Lot.h>
#include <cISC4LotManager.h>
#include <cISC4Occupant.h>
#include <cISC4OccupantFilter.h>
#include <cISC4OccupantManager.h>
#include <cISC4SimGrid.h>
#include <cISC4ZoneManager.h>
#include <cISCProperty.h>
#include <cISCPropertyHolder.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::game_growify {
namespace {

using growify_command::Command;
using growify_command::Kind;

constexpr uint32_t kMsgPostCityInit = 0x26D31EC1;
constexpr uint32_t kMsgCheatIssued = 0x230E27AC;
constexpr uint32_t kGrowifyCheatId = 0x4F534347; // "OSCG"
constexpr uint32_t kOccupantTypeBuilding = 0x278128A0;
constexpr uint32_t kPropertyPurpose = 0x27812833;

HWND g_window = nullptr;

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    g_window = hwnd;
    return FALSE;
}

HWND GameWindow()
{
    if (!g_window || !IsWindow(g_window)) {
        g_window = nullptr;
        EnumWindows(FindGameWindow, 0);
    }
    return g_window;
}

void Tell(const std::string& text)
{
    log::Write("growify: %s", text.c_str());
    if (GetEnvironmentVariableA("OPENSC4_TEST_CITY", nullptr, 0) > 0)
        return; // unattended runs: the log only
    MessageBoxA(GameWindow(), text.c_str(), "Growify", MB_OK | MB_ICONINFORMATION);
}

// The building's exemplar Purpose, a single uint8 (or a one-element array).
bool Purpose(cISCPropertyHolder* properties, uint32_t* purpose)
{
    const cISCProperty* property = properties ? properties->GetProperty(kPropertyPurpose) : nullptr;
    const cIGZVariant* value = property ? property->GetPropertyValue() : nullptr;
    if (!value)
        return false;
    if (value->GetType() == cIGZVariant::Type::Uint8) {
        *purpose = value->GetValUint8();
        return true;
    }
    if (value->GetType() == cIGZVariant::Type::Uint8Array && value->GetCount() == 1) {
        *purpose = *value->RefUint8();
        return true;
    }
    return false;
}

// Buildings whose Purpose the command converts. On the stack for one
// iteration, so the reference count is not followed.
class BuildingFilter final : public cISC4OccupantFilter {
public:
    explicit BuildingFilter(Kind kind) : kind_(kind) {}

    bool QueryInterface(uint32_t riid, void** object) override
    {
        if (riid == GZIID_cISC4OccupantFilter || riid == GZIID_cIGZUnknown) {
            *object = static_cast<cISC4OccupantFilter*>(this);
            return true;
        }
        *object = nullptr;
        return false;
    }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }

    bool IsOccupantIncluded(cISC4Occupant* occupant) override
    {
        uint32_t purpose = 0;
        return IsOccupantTypeIncluded(occupant->GetType()) && Purpose(occupant->AsPropertyHolder(), &purpose) &&
               growify_command::Matches(kind_, purpose);
    }
    bool IsOccupantTypeIncluded(uint32_t type) override { return type == kOccupantTypeBuilding; }
    bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }

private:
    const Kind kind_;
};

struct Conversion {
    cISC4LotManager* lots;
    cISC4SimGrid<int8_t>* zones;
    const Command* command;
    int32_t converted;
};

bool ConvertBuilding(cISC4Occupant* occupant, void* data)
{
    auto* conversion = static_cast<Conversion*>(data);
    cISC4Lot* lot = conversion->lots->GetOccupantLot(occupant);
    if (!lot || lot->GetZoneType() != cISC4ZoneManager::ZoneType::Plopped)
        return true;
    SC4Rect<int32_t> bounds;
    lot->GetBoundingRect(bounds);
    for (int32_t x = bounds.topLeftX; x <= bounds.bottomRightX; ++x)
        for (int32_t z = bounds.topLeftY; z <= bounds.bottomRightY; ++z)
            conversion->zones->SetTractValue(x, z, static_cast<int8_t>(conversion->command->zone));
    lot->UpdateZoneType();
    // Historical, unless asked not to, so the game's redevelopment does not
    // replace the building.
    if (conversion->command->historical)
        lot->SetHistorical(true);
    ++conversion->converted;
    return true;
}

void OnCheat(const char* text)
{
    Command command;
    const std::string problem = growify_command::Parse(text ? text : "", &command);
    if (!problem.empty()) {
        Tell(problem);
        return;
    }
    cISC4AppPtr app;
    cISC4City* city = app ? app->GetCity() : nullptr;
    cISC4LotManager* lots = city ? city->GetLotManager() : nullptr;
    cISC4OccupantManager* occupants = city ? city->GetOccupantManager() : nullptr;
    cISC4ZoneManager* zoneManager = city ? city->GetZoneManager() : nullptr;
    cISC4SimGrid<int8_t>* zones = zoneManager ? zoneManager->GetZoneGrid() : nullptr;
    if (!lots || !occupants || !zones) {
        Tell("Growify works in a city.");
        return;
    }
    Conversion conversion{lots, zones, &command, 0};
    BuildingFilter filter(command.kind);
    occupants->IterateOccupants(&ConvertBuilding, &conversion, nullptr, nullptr, &filter);
    char answer[96];
    std::snprintf(answer, sizeof(answer), "Growified %d %s lot(s).", conversion.converted,
                  growify_command::Name(command.kind));
    Tell(answer);
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
            RegisterCheat(); // in case the app had no cheat manager at startup
        } else if (type == kMsgCheatIssued) {
            auto* standard = static_cast<cIGZMessage2Standard*>(message);
            if (static_cast<uint32_t>(standard->GetData1()) == kGrowifyCheatId) {
                const auto* text = static_cast<cIGZString*>(standard->GetVoid2());
                OnCheat(text ? text->ToChar() : nullptr);
            }
        }
        return true;
    }

    void RegisterCheat()
    {
        if (cheatRegistered_)
            return;
        cISC4AppPtr app;
        cIGZCheatCodeManager* cheats = app ? app->GetCheatCodeManager() : nullptr;
        if (!cheats)
            return;
        cheatRegistered_ = cheats->RegisterCheatCode(kGrowifyCheatId, cRZBaseString("growify")) &&
                           cheats->AddNotification2(this, 0);
        log::Write("growify: cheat \"growify\" %s", cheatRegistered_ ? "registered" : "NOT registered");
    }

private:
    bool cheatRegistered_ = false;
};

Listener g_listener;

} // namespace

void Start()
{
    cIGZMessageServer2Ptr server;
    const bool ok = server && server->AddNotification(&g_listener, kMsgPostCityInit);
    log::Write("growify: city messages %s", ok ? "subscribed" : "NOT subscribed");
    g_listener.RegisterCheat();
}

} // namespace opensc4::game_growify
