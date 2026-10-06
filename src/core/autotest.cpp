#include "core/autotest.h"

#include "core/crash.h"
#include "core/log.h"
#include "core/pollution_proxy.h"

#include <GZServPtrs.h>
#include <SC4List.h>
#include <SC4String.h>
#include <cIGZFrameWork.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4PollutionSimulator.h>
#include <cISC4Region.h>
#include <cISC4RegionalCity.h>
#include <cISC4Simulator.h>
#include <cRZAutoRefCount.h>
#include <cRZBaseString.h>
#include <cRZBaseSystemService.h>
#include <cRZCOMDllDirector.h>

#include <windows.h>

#include <cstdlib>
#include <string>

namespace opensc4::autotest {
namespace {

// "OSC4" + 1. System service IDs must be unique.
constexpr uint32_t kServiceId = 0x4F534335;

std::string Env(const char* name)
{
    char value[512];
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length > 0 && length < sizeof(value) ? std::string(value, length) : std::string();
}

int EnvInt(const char* name, int fallback)
{
    const std::string value = Env(name);
    return value.empty() ? fallback : std::atoi(value.c_str());
}

__declspec(noinline) void WriteThroughNull(volatile int* p)
{
    *p = 1;
}

// A deliberate crash inside a hook-style guard (OPENSC4_TEST_CRASH=1).
void CrashOnPurpose()
{
    __try {
        WriteThroughNull(nullptr);
    } __except (crash::HookFilter("autotest::CrashOnPurpose", GetExceptionInformation())) {
    }
}

class AutoTestService final : public cRZBaseSystemService {
public:
    AutoTestService() : cRZBaseSystemService(kServiceId, 0) {}

    bool Init() override { return true; }
    bool Shutdown() override { return true; }

    bool OnTick(uint32_t) override
    {
        Step();
        return true;
    }

private:
    enum class State { WaitForRegion, Running, Quitting, Done };

    void Step()
    {
        const ULONGLONG now = GetTickCount64();
        switch (state_) {
        case State::WaitForRegion: WaitForRegion(now); break;
        case State::Running: Run(now); break;
        case State::Quitting:
            if (now - stateStart_ > 60000) {
                log::Write("autotest: still running 60 s after RequestQuit; giving up");
                state_ = State::Done;
            }
            break;
        case State::Done: break;
        }
    }

    void WaitForRegion(ULONGLONG now)
    {
        cISC4AppPtr app;
        if (!app)
            return;
        cISC4Region* region = app->GetRegion();
        // Give the region view a few seconds to settle before loading.
        if (!region || now - stateStart_ < 5000) {
            if (now - stateStart_ > 180000) {
                log::Write("autotest: no region after 180 s; quitting");
                Quit(app);
            }
            return;
        }

        app->EnableFullGamePauseOnAppFocusLoss(false);
        app->SetPopupDialogsEnabled(false);

        const std::string want = Env("OPENSC4_TEST_CITY");
        const bool pickLargest = want == "1" || want == "auto";
        SC4List<cRZAutoRefCount<cISC4RegionalCity>> cities;
        region->GetAllCities(cities);
        cRZAutoRefCount<cISC4RegionalCity>* chosen = nullptr;
        std::string chosenPath;
        int32_t chosenPopulation = -1;
        for (auto& city : cities) {
            cRZBaseString path;
            city->GetCitySaveFilePath(path);
            const std::string savePath = path.ToChar() ? path.ToChar() : "";
            const int32_t population = city->GetPopulation();
            log::Write("autotest: region city pop %d, %s", population, savePath.c_str());
            if (savePath.empty())
                continue;
            const bool match = pickLargest ? population > chosenPopulation
                                           : savePath.find(want) != std::string::npos && chosen == nullptr;
            if (match) {
                chosen = &city;
                chosenPath = savePath;
                chosenPopulation = population;
            }
        }
        SC4String* regionName = region->GetName();
        log::Write("autotest: region \"%s\", %u cities", regionName ? regionName->ToChar() : "?",
                   static_cast<unsigned>(cities.size()));
        if (!chosen) {
            log::Write("autotest: no city matches OPENSC4_TEST_CITY=\"%s\"; quitting", want.c_str());
            Quit(app);
            return;
        }

        // Where cISC4App::LoadCity (slot 13) really lives, for checking its
        // signature against gzcom-dll's declaration.
        void** appVtable = *reinterpret_cast<void***>(static_cast<cISC4App*>(app));
        log::Write("autotest: cISC4App %p, vtable %p, LoadCity slot -> %p", static_cast<void*>(static_cast<cISC4App*>(app)),
                   static_cast<void*>(appVtable), appVtable[13]);
        if (Env("OPENSC4_TEST_DRYRUN") == "1") {
            log::Write("autotest: dry run; not loading");
            Quit(app);
            return;
        }

        // The app keeps this pointer for the whole city session (field 0x4C,
        // returned by GetRegionalCity), so it must be the region's own entry,
        // not an element of our temporary list.
        int32_t x = 0, z = 0;
        (*chosen)->GetPosition(x, z);
        cRZAutoRefCount<cISC4RegionalCity>* regionEntry = region->GetCity(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
        if (!regionEntry || static_cast<cISC4RegionalCity*>(*regionEntry) != static_cast<cISC4RegionalCity*>(*chosen)) {
            log::Write("autotest: region has no entry for the chosen city at (%d, %d); quitting", x, z);
            Quit(app);
            return;
        }

        log::Write("autotest: loading %s (pop %d, tile %d,%d)", chosenPath.c_str(), chosenPopulation, x, z);
        const bool loaded = app->LoadCity(SC4String(chosenPath.c_str()), regionEntry);
        cISC4City* city = app->GetCity();
        log::Write("autotest: LoadCity returned %s, city %p", loaded ? "true" : "false", static_cast<void*>(city));
        if (!loaded || !city) {
            Quit(app);
            return;
        }

        cISC4PollutionSimulator* pollution = city->GetPollutionSimulator();
        const void* proxy = pollution_proxy::LastCreated();
        log::Write("autotest: city pollution simulator %p; last proxy %p -> %s", static_cast<void*>(pollution),
                   proxy, pollution && pollution == proxy ? "CITY USES THE PROXY" : "city uses the original");

        cISC4Simulator* sim = city->GetSimulator();
        const int speed = EnvInt("OPENSC4_TEST_SPEED", 2);
        bool speedSet = false;
        if (sim) {
            speedSet = sim->SetSimSpeed(speed);
            sim->Resume();
        }
        runSeconds_ = EnvInt("OPENSC4_TEST_SECONDS", 180);
        log::Write("autotest: running for %d s at speed %d (SetSimSpeed %s)", runSeconds_, speed,
                   speedSet ? "ok" : "refused");
        Enter(State::Running, now);
        lastReport_ = 0;
        Report(app);
    }

    void Run(ULONGLONG now)
    {
        cISC4AppPtr app;
        if (!app || !app->GetCity()) {
            log::Write("autotest: city went away while running");
            state_ = State::Done;
            return;
        }
        if (now - lastReport_ >= 15000) {
            lastReport_ = now;
            Report(app);
        }
        if (now - stateStart_ >= 5000 && Env("OPENSC4_TEST_CRASH") == "1") {
            log::Write("autotest: crashing on purpose (OPENSC4_TEST_CRASH=1)");
            CrashOnPurpose();
        }
        if (now - stateStart_ >= static_cast<ULONGLONG>(runSeconds_) * 1000) {
            Report(app);
            log::Write("autotest: run finished; quitting without saving");
            Quit(app);
        }
    }

    void Report(cISC4App* app)
    {
        cISC4City* city = app->GetCity();
        cISC4Simulator* sim = city ? city->GetSimulator() : nullptr;
        cISC4PollutionSimulator* pollution = city ? city->GetPollutionSimulator() : nullptr;
        if (!sim || !pollution)
            return;
        uint32_t year = 0, month = 0, day = 0, dayOfYear = 0, weekDay = 0;
        sim->GetSimDate(&year, &month, &day, &dayOfYear, &weekDay);
        log::Write("autotest: sim %04u-%02u-%02u speed %d paused %d | avg air %d water %d garbage %d",
                   year, month, day, sim->GetSimSpeed(), sim->IsAnyPaused() ? 1 : 0,
                   pollution->GetAverageAirValue(), pollution->GetAverageWaterValue(),
                   pollution->GetAverageGarbageValue());
    }

    void Quit(cISC4App* app)
    {
        Enter(State::Quitting, GetTickCount64());
        app->RequestQuit(false, false); // no dialog, no save
    }

    void Enter(State state, ULONGLONG now)
    {
        state_ = state;
        stateStart_ = now;
    }

    State state_ = State::WaitForRegion;
    ULONGLONG stateStart_ = GetTickCount64();
    ULONGLONG lastReport_ = 0;
    int runSeconds_ = 180;
};

} // namespace

bool Enabled()
{
    return !Env("OPENSC4_TEST_CITY").empty();
}

void PreloadLibraries()
{
    const std::string list = Env("OPENSC4_TEST_PRELOAD");
    std::size_t start = 0;
    while (start < list.size()) {
        std::size_t end = list.find(';', start);
        if (end == std::string::npos)
            end = list.size();
        const std::string path = list.substr(start, end - start);
        start = end + 1;
        if (path.empty())
            continue;
        HMODULE module = LoadLibraryA(path.c_str());
        const DWORD error = module ? 0 : GetLastError();
        if (module)
            log::Write("autotest: preloaded %s", path.c_str());
        else
            log::Write("autotest: could not preload %s (error %lu)", path.c_str(), error);
    }
}

void Start()
{
    cIGZFrameWork* framework = RZGetFrameWork();
    if (!framework)
        return;
    auto* service = new AutoTestService();
    const bool added = framework->AddSystemService(service) && framework->AddToTick(service);
    log::Write("autotest: tick service %s (OPENSC4_TEST_CITY=\"%s\")", added ? "started" : "FAILED to start",
               Env("OPENSC4_TEST_CITY").c_str());
}

} // namespace opensc4::autotest
