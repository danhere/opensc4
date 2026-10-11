// The cheat "RemovePhantomBudgetItems <category> [all]"
// (docs/phantom-budget.md). A budget row can be left behind for a building
// that is gone, most often after a plugin was taken out of the Plugins
// folder; the cheat takes such rows out of a department. An addition beyond
// the original that is part of the base game (game.phantom_budget, on by
// default): all that is installed is the cheat's registration with the
// game's cheat code manager, no hook, and nothing runs unless a cheat is
// typed.
//
// The cheat, its categories with their department and purpose IDs, and the
// rule for what is a phantom (a locally funded item whose property holder
// has no exemplar type any more) are from sc4-remove-phantom-budget-items by
// Nicholas Hayes
// (https://github.com/0xC0000054/sc4-remove-phantom-budget-items), used
// under the MIT License:
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
// Everything goes through the budget simulator's and the departments' public
// interfaces, as in the mod, so it works the same with the game's budget
// code and with ours (simulators.budget.*). Where the mod answers in a game
// dialog, this answers in a message box, like the "seed" cheat.
#include "core/log.h"
#include "core/registry.h"

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <SC4List.h>
#include <cIGZCheatCodeManager.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZString.h>
#include <cISC4App.h>
#include <cISC4BudgetSimulator.h>
#include <cISC4BuildingOccupant.h>
#include <cISC4City.h>
#include <cISC4DepartmentBudget.h>
#include <cISC4Occupant.h>
#include <cISCPropertyHolder.h>
#include <cRZAutoRefCount.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::game_phantom_budget {
namespace {

constexpr uint32_t kMsgPostCityInit = 0x26D31EC1;
constexpr uint32_t kMsgPreCityShutdown = 0x26D31EC2;
constexpr uint32_t kMsgCheatIssued = 0x230E27AC;
// The mod's own cheat ID.
constexpr uint32_t kCheatId = 0x7630FE7F;
constexpr char kCheatName[] = "RemovePhantomBudgetItems";
constexpr uint32_t kExemplarType = 0x00000010;

struct Part {
    uint32_t department;
    uint32_t purpose;
};

struct Category {
    const char* name;
    std::vector<Part> parts;
};

const Category kCategories[] = {
    {"Fire", {{0x28F55A9F, 0xEA567BC3}}},
    {"Police", {{0xA2963983, 0x0A567BAA}}},
    {"Jail", {{0xA2963984, 0xEA56768A}}},
    {"Power", {{0x8910BC8A, 0xCA58E540}}},
    {"Education", {{0x09188F4C, 0xEA5654B6}, {0x4A538CC6, 0x4A5654BA}}}, // staff, coverage
    {"Health", {{0x09188F42, 0xCA565486}, {0xAA538CB3, 0xEA56549E}}},    // staff, coverage
};

HWND GameWindow()
{
    struct Find {
        DWORD process;
        HWND window;
    } find{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* const f = reinterpret_cast<Find*>(param);
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            if (process != f->process || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
                return TRUE;
            f->window = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&find));
    return find.window;
}

void Tell(const std::string& text)
{
    log::Write("phantom_budget: %s", text.c_str());
    if (GetEnvironmentVariableA("OPENSC4_TEST_CITY", nullptr, 0) > 0)
        return; // unattended runs: the log only
    MessageBoxA(GameWindow(), text.c_str(), kCheatName, MB_OK | MB_ICONINFORMATION);
}

// The buildings among a department's locally funded items for `purpose`
// that are phantoms, or all of them. `seen` gets how many items the
// department listed.
std::vector<cRZAutoRefCount<cISC4BuildingOccupant>> ItemsToRemove(cISC4DepartmentBudget* department, uint32_t purpose,
                                                                  bool all, size_t* seen)
{
    std::vector<cRZAutoRefCount<cISC4BuildingOccupant>> buildings;
    // The list's nodes hold a reference each, which the list gives back when
    // it goes. The game's function (0x006AA9C0) leaves no result in AL,
    // whatever its declaration says, so the answer is the list alone.
    SC4List<cISCPropertyHolder*> items;
    department->GetLocallyFundedItemsByPurpose(purpose, items);
    for (cISCPropertyHolder* holder : items) {
        if (!holder)
            continue;
        ++*seen;
        // No exemplar type: the building's exemplar is no longer in the
        // Plugins folder.
        if (all || !holder->HasProperty(kExemplarType)) {
            cRZAutoRefCount<cISC4BuildingOccupant> building;
            if (holder->QueryInterface(GZIID_cISC4BuildingOccupant, building.AsPPVoid()))
                buildings.push_back(building);
        }
    }
    return buildings;
}

// The whole cheat line: "RemovePhantomBudgetItems <category> [all]".
void OnCheat(const char* line)
{
    std::istringstream in(line ? line : "");
    std::vector<std::string> words;
    for (std::string word; in >> word;)
        words.push_back(word);
    if (words.size() != 2 && words.size() != 3) {
        Tell("Usage: RemovePhantomBudgetItems <category> [all]\n"
             "<category>: one of Fire, Police, Jail, Power, Education or Health.\n"
             "[all]: clears the whole budget category, not only the items whose plugin no longer exists.");
        return;
    }
    const Category* category = nullptr;
    for (const Category& known : kCategories)
        if (_stricmp(known.name, words[1].c_str()) == 0)
            category = &known;
    if (!category) {
        Tell("Unknown budget category: " + words[1]);
        return;
    }
    const bool all = words.size() == 3 && _stricmp(words[2].c_str(), "all") == 0;
    cISC4AppPtr app;
    cISC4City* const city = app ? app->GetCity() : nullptr;
    cISC4BudgetSimulator* const budget = city ? city->GetBudgetSimulator() : nullptr;
    if (!budget) {
        Tell("No city is loaded.");
        return;
    }
    // Counted by building type: Education and Health have the same building
    // in two departments.
    std::unordered_set<uint32_t> types;
    size_t seen = 0, removed = 0, departments = 0;
    for (const Part& part : category->parts) {
        cISC4DepartmentBudget* const department = budget->GetDepartmentBudget(part.department);
        if (!department)
            continue;
        ++departments;
        for (cRZAutoRefCount<cISC4BuildingOccupant>& building : ItemsToRemove(department, part.purpose, all, &seen)) {
            department->RemoveLocallyFundedObject(building->AsOccupant()->AsPropertyHolder(), part.purpose);
            const uint32_t type = building->GetBuildingType();
            // Some buildings also have a line item of their own.
            department->RemoveLineItem(type);
            types.insert(type);
            ++removed;
        }
    }
    log::Write("phantom_budget: %s%s: %zu of %zu departments found, %zu locally funded items looked at, %zu removed "
               "(%zu building types)",
               category->name, all ? " all" : "", departments, category->parts.size(), seen, removed, types.size());
    if (all)
        Tell(std::string("Cleared the ") + category->name + " budget category.");
    else if (types.empty())
        Tell("No orphaned budget items found.");
    else
        Tell("Removed " + std::to_string(types.size()) + " orphaned budget item(s).");
}

cIGZCheatCodeManager* Cheats()
{
    cISC4AppPtr app;
    return app ? app->GetCheatCodeManager() : nullptr;
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

    // Cheat messages come only when a cheat is typed; the city message only
    // when Install asked for it.
    bool DoMessage(cIGZMessage2* message) override
    {
        const uint32_t type = message->GetType();
        if (type == kMsgCheatIssued) {
            auto* const standard = static_cast<cIGZMessage2Standard*>(message);
            if (static_cast<uint32_t>(standard->GetData1()) == kCheatId) {
                const auto* const text = static_cast<cIGZString*>(standard->GetVoid2());
                OnCheat(text ? text->ToChar() : nullptr);
            }
        } else if (type == kMsgPostCityInit || type == kMsgPreCityShutdown) {
            if (Register())
                TestLines(Cheats(), type == kMsgPostCityInit ? "city started" : "city closing");
        }
        return true;
    }

    // Once per process: the cheat stays registered from then on.
    bool Register()
    {
        if (registered_)
            return true;
        cIGZCheatCodeManager* const cheats = Cheats();
        if (!cheats)
            return false;
        registered_ =
            cheats->RegisterCheatCode(kCheatId, cRZBaseString(kCheatName)) && cheats->AddNotification2(this, 0);
        log::Write("phantom_budget: the cheat \"%s <category> [all]\" is %s", kCheatName,
                   registered_ ? "available" : "NOT available (the cheat code manager refused it)");
        return registered_;
    }

private:
    // For unattended tests only: OPENSC4_TEST_PHANTOM_BUDGET="Fire;Police
    // all;..." hands each part, as the cheat's arguments ("-" for none), to
    // the cheat code manager the way the cheat box does (its
    // DoDefaultCheatCodeProcessing), when a city has started and again when
    // it closes.
    static void TestLines(cIGZCheatCodeManager* cheats, const char* when)
    {
        char list[256];
        const DWORD n = GetEnvironmentVariableA("OPENSC4_TEST_PHANTOM_BUDGET", list, sizeof(list));
        if (n == 0 || n >= sizeof(list))
            return;
        std::istringstream in(list);
        for (std::string arguments; std::getline(in, arguments, ';');) {
            const std::string line = arguments == "-" ? kCheatName : std::string(kCheatName) + " " + arguments;
            uint32_t id = 0;
            const bool known = cheats->DoDefaultCheatCodeProcessing(cRZBaseString(line.c_str()), id);
            log::Write("phantom_budget: test (%s): \"%s\" given to the cheat code manager: %s, cheat %08X", when,
                       line.c_str(), known ? "taken" : "not taken", id);
        }
    }

    bool registered_ = false;
};

Listener g_listener;

// The city message is asked for only if the application has no cheat code
// manager yet (the cheat is then registered when a city starts), or for the
// unattended test.
bool Install(registry::Mode)
{
    const bool registered = g_listener.Register();
    if (registered && GetEnvironmentVariableA("OPENSC4_TEST_PHANTOM_BUDGET", nullptr, 0) == 0)
        return true;
    cIGZMessageServer2Ptr server;
    const bool ok = server && server->AddNotification(&g_listener, kMsgPostCityInit) &&
                    (!registered || server->AddNotification(&g_listener, kMsgPreCityShutdown));
    log::Write("phantom_budget: %s; city messages %s",
               registered ? "test lines wait for a city" : "no cheat code manager yet, the cheat waits for a city",
               ok ? "subscribed" : "NOT subscribed");
    return ok;
}

const bool g_registered = registry::Register({
    "game.phantom_budget",
    "game",
    registry::Kind::Api,
    registry::Phase::PostAppInit,
    0,
    "The cheat RemovePhantomBudgetItems: takes budget rows of buildings that are gone out of a department",
    false,
    Install,
    true, // nothing changes until the cheat is typed
});

} // namespace
} // namespace opensc4::game_phantom_budget
