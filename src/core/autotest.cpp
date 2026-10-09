#include "core/autotest.h"

#include "core/crash.h"
#include "core/log.h"
#include "core/fixed_ticks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/pollution_proxy.h"
#include "core/rng_trace.h"
#include "core/virtual_clock.h"

#include <opensc4/addresses.h>

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <SC4List.h>
#include <SC4String.h>
#include <cIGZFrameWork.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageTarget2.h>
#include <cIGZWin.h>
#include <cISC43DRender.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4PollutionSimulator.h>
#include <cISC4Region.h>
#include <cISC4RenderProperties.h>
#include <cISC4RegionalCity.h>
#include <cISC4Simulator.h>
#include <cISC4TrafficSimulator.h>
#include <cISC4SimGrid.h>
#include <cISC4ResidentialSimulator.h>
#include <cISC4LotManager.h>
#include <cISC4Occupant.h>
#include <cISC4LandValueSimulator.h>
#include <cISC4BudgetSimulator.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>
#include <cRZBaseString.h>
#include <cRZBaseSystemService.h>
#include <cRZCOMDllDirector.h>

#include <windows.h>

#include <cstdio>
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

// The city view: main window -> cSC4App's window -> the 3D view (as in
// gzcom-dll's SC4UI::GetView3DWin).
cRZAutoRefCount<cISC4View3DWin> GetView3D(cISC4App* app)
{
    constexpr uint32_t kWinSC4App = 0x6104489A;
    constexpr uint32_t kWinView3D = 0x9A47B417;
    cRZAutoRefCount<cISC4View3DWin> view;
    cIGZWin* main = app->GetMainWindow();
    cIGZWin* parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
    if (parent)
        parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid());
    return view;
}

// OPENSC4_TEST_RENDER: "Name=value,..." for cISC4RenderProperties (the names
// listed in gzcom-dll's cISC4RenderProperties.h). Each name is looked up as a
// bool, then an int, then a float property.
void ApplyRenderProperties(cISC4App* app)
{
    const std::string list = Env("OPENSC4_TEST_RENDER");
    cISC4RenderProperties* props = list.empty() ? nullptr : app->GetRenderProperties();
    if (!props)
        return;
    std::size_t start = 0;
    while (start < list.size()) {
        std::size_t end = list.find(',', start);
        if (end == std::string::npos)
            end = list.size();
        const std::string item = list.substr(start, end - start);
        start = end + 1;
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string name = item.substr(0, eq);
        const char* value = item.c_str() + eq + 1;
        int32_t id = props->BoolPropertyIDFromName(name.c_str());
        if (id >= 0) {
            const bool before = props->BoolValue(id);
            props->SetBoolValue(id, std::atoi(value) != 0);
            log::Write("autotest: render property %s (bool %d): %d -> %d", name.c_str(), id, before ? 1 : 0,
                       props->BoolValue(id) ? 1 : 0);
            continue;
        }
        id = props->IntPropertyIDFromName(name.c_str());
        if (id >= 0) {
            const int32_t before = props->IntValue(id);
            props->SetIntValue(id, std::atoi(value));
            log::Write("autotest: render property %s (int %d): %d -> %d", name.c_str(), id, before, props->IntValue(id));
            continue;
        }
        id = props->FloatPropertyIDFromName(name.c_str());
        if (id >= 0) {
            const float before = props->FloatValue(id);
            props->SetFloatValue(id, static_cast<float>(std::atof(value)));
            log::Write("autotest: render property %s (float %d): %g -> %g", name.c_str(), id, before,
                       props->FloatValue(id));
            continue;
        }
        log::Write("autotest: no render property named %s", name.c_str());
    }
}

// FNV-1a over every tract value of a simulation grid; 0 for no grid.
template <typename T>
uint32_t GridChecksum(cISC4SimGrid<T>* grid)
{
    if (!grid)
        return 0;
    uint32_t h = 2166136261u;
    const int32_t nx = grid->GetTractCountX(), nz = grid->GetTractCountZ();
    for (int32_t z = 0; z < nz; ++z)
        for (int32_t x = 0; x < nx; ++x) {
            const T v = grid->GetTractValue(x, z);
            const auto* b = reinterpret_cast<const uint8_t*>(&v);
            for (size_t i = 0; i < sizeof v; ++i)
                h = (h ^ b[i]) * 16777619u;
        }
    return h;
}

// FNV-1a over `size` bytes.
uint32_t BytesChecksum(const void* data, size_t size)
{
    uint32_t h = 2166136261u;
    const auto* b = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

// cISC43DRender::TakeSnapshot(cIGZString&, bool), vtable slot 22 (0x007C2570
// in 641): writes the current frame to a file. Called by slot because
// gzcom-dll declares the two TakeSnapshot overloads in the exe's slot order,
// and MSVC-ABI compilers lay out overloaded virtuals in reverse declaration
// order, so the header's string overload compiles to slot 23 (the
// cIGZBuffer** one).
bool TakeSnapshotToFile(cISC43DRender* renderer, cIGZString& path)
{
    using Fn = bool(__thiscall*)(cISC43DRender*, cIGZString&, bool);
    void** vtable = *reinterpret_cast<void***>(renderer);
    return reinterpret_cast<Fn>(vtable[22])(renderer, path, false);
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

// A simulator agent (cISC4Simulator::AddAgent): the simulator calls it from
// inside its tick, so whatever it does happens at the same point of simulation
// time in every run, unlike the framework tick, which comes after however
// many simulation ticks the frame had room for.
class DayAgent final : public cIGZMessageTarget2 {
public:
    using Callback = void (*)(void* context);
    DayAgent(Callback callback, void* context) : callback_(callback), context_(context) {}

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
    // Owned by the autotest service, which lives for the whole process.
    uint32_t AddRef() override { return ++refs_; }
    uint32_t Release() override { return refs_ > 0 ? --refs_ : 0; }

    bool DoMessage(cIGZMessage2*) override
    {
        callback_(context_);
        return true;
    }

private:
    Callback callback_;
    void* context_;
    uint32_t refs_ = 0;
};

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
    enum class State { WaitForRegion, Running, Shots, Quitting, Done };

    void Step()
    {
        const ULONGLONG now = GetTickCount64();
        switch (state_) {
        case State::WaitForRegion: WaitForRegion(now); break;
        case State::Running: Run(now); break;
        case State::Shots: Shots(now); break;
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
        // OPENSC4_TEST_FIND_POWER_PLANTS=1: one cISC4PowerSimulator::
        // FindPowerPlants call (slot 30; the advisors' query) after the load.
        if (Env("OPENSC4_TEST_FIND_POWER_PLANTS") == "1" && game::IsBuild641()) {
            if (void* const power = city->GetPowerSimulator()) {
                using FindFn = void(__thiscall*)(void*, void**, uint32_t*, void**, int32_t*);
                void* funded = nullptr;
                void* oldest = nullptr;
                uint32_t funding = 0;
                int32_t age = 0;
                reinterpret_cast<FindFn>((*static_cast<void***>(power))[30])(power, &funded, &funding, &oldest, &age);
                log::Write("autotest: FindPowerPlants: lowest funded %s %08X, oldest %s %d%%", funded ? "found" : "none",
                           funding, oldest ? "found" : "none", age);
                for (void* plant : {funded, oldest}) {
                    using ReleaseFn = uint32_t(__thiscall*)(void*);
                    if (plant)
                        reinterpret_cast<ReleaseFn>((*static_cast<void***>(plant))[2])(plant);
                }
            }
        }
        // OPENSC4_TEST_CAPACITY_SCALE=1: one call of the handler of message
        // 0x66956810 (the simulator's scheduler never sent it in a test run).
        if (Env("OPENSC4_TEST_CAPACITY_SCALE") == "1" && pollution) {
            using HandlerFn = void(__thiscall*)(void* self);
            reinterpret_cast<HandlerFn>(addr::cSC4PollutionSimulator_UpdateCapacityScale)(pollution);
            log::Write("autotest: UpdateCapacityScale called");
        }
        // OPENSC4_TEST_GARBAGE_BUILDINGS=1: one GetGarbageBuildings call after
        // the load (no simulator calls it in a test run).
        if (Env("OPENSC4_TEST_GARBAGE_BUILDINGS") == "1" && pollution) {
            SC4List<cISC4Occupant*> buildings;
            const bool got = pollution->GetGarbageBuildings(buildings);
            log::Write("autotest: GetGarbageBuildings: %u buildings (returned %d)", static_cast<unsigned>(buildings.size()),
                       got ? 1 : 0);
            for (cISC4Occupant* occupant : buildings)
                occupant->Release();
        }
        // OPENSC4_TEST_RADIATION="x,z,amount,radius": one AddRadiationToCity
        // call after the load (only disasters make one otherwise).
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

        cISC4Simulator* sim = city->GetSimulator();
        shotDir_ = Env("OPENSC4_TEST_SHOTS");
        if (!shotDir_.empty()) {
            // Frames should depend only on the save: no simulation time passes.
            const bool paused = sim && sim->Pause();
            settleMs_ = EnvInt("OPENSC4_TEST_SHOT_SETTLE", 3000);
            log::Write("autotest: shots to %s, settle %d ms (Pause %s)", shotDir_.c_str(), settleMs_,
                       paused ? "ok" : "refused");
            shot_ = -1;
            Enter(State::Shots, now);
            return;
        }
        const int speed = EnvInt("OPENSC4_TEST_SPEED", 2);
        const bool noTime = Env("OPENSC4_TEST_DAYS") == "0";
        bool speedSet = false;
        if (sim) {
            speedSet = sim->SetSimSpeed(speed);
            // OPENSC4_TEST_START_PAUSED=1: the city waits until someone (or a
            // UI-driving script) resumes it from the game's speed buttons,
            // e.g. to build something before the counted days run.
            if (noTime || Env("OPENSC4_TEST_START_PAUSED") == "1") {
                sim->Pause(); // no simulation time at all, not even one tick
            } else {
                // The pause count is saved with the city (cSC4Simulator +0x30):
                // a city saved by an earlier run while paused twice (paused
                // to build, then at its last day) needs two resumes. Saves
                // made by a single autotest run need one, as before.
                sim->Resume();
                for (int i = 0; i < 8 && sim->IsPaused(); ++i)
                    sim->Resume();
            }
        }
        runSeconds_ = EnvInt("OPENSC4_TEST_SECONDS", 180);
        runDays_ = Env("OPENSC4_TEST_DAYS").empty() ? -1 : EnvInt("OPENSC4_TEST_DAYS", 0);
        if (runDays_ >= 0 && sim) {
            endDay_ = sim->GetSimDateNumber() + runDays_;
            log::Write("autotest: running %d game days, day %d to %d, at speed %d (SetSimSpeed %s)", runDays_,
                       sim->GetSimDateNumber(), endDay_, speed, speedSet ? "ok" : "refused");
        }
        if ((runDays_ >= 0 || fingerprint_) && sim) {
            agentAdded_ = sim->AddAgent(&agent_, cISC4Simulator::AgentTypeSimNewDay, cRZBaseString("OpenSC4 autotest"),
                                        static_cast<cISC4Simulator::eAgentFlags>(
                                            cISC4Simulator::AgentFlagEnabledForUnestablishedCities |
                                            cISC4Simulator::AgentFlagEnabledForEstablishedCities));
            log::Write("autotest: new-day agent %s", agentAdded_ ? "added" : "REFUSED; sampling per frame instead");
            if (fingerprint_)
                Fingerprint(city, sim->GetSimDateNumber());
            lastFingerprintDay_ = sim->GetSimDateNumber();
            // OPENSC4_TEST_DAYS=0: the simulator is paused and no new day
            // will come; the run is over before it starts.
            reachedEndDay_ = runDays_ == 0;
        }
        if (runDays_ < 0) {
            log::Write("autotest: running for %d s at speed %d (SetSimSpeed %s)", runSeconds_, speed,
                       speedSet ? "ok" : "refused");
        }
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
        cISC4Simulator* sim = app->GetCity()->GetSimulator();
        if (!agentAdded_ && fingerprint_ && sim && sim->GetSimDateNumber() != lastFingerprintDay_) {
            lastFingerprintDay_ = sim->GetSimDateNumber();
            Fingerprint(app->GetCity(), lastFingerprintDay_);
        }
        const bool done = runDays_ >= 0 ? (agentAdded_ ? reachedEndDay_ : sim && sim->GetSimDateNumber() >= endDay_)
                                        : now - stateStart_ >= static_cast<ULONGLONG>(runSeconds_) * 1000;
        if (done) {
            if (sim) {
                sim->Pause();
                if (agentAdded_)
                    sim->RemoveAgent(&agent_);
            }
            Report(app);
            if (sim)
                log::Write("autotest: stopped at day %d, sim time %d, after %llu ms", sim->GetSimDateNumber(),
                           sim->GetSimTime(), now - stateStart_);
            if (Env("OPENSC4_TEST_SAVE") == "1")
                SaveInPlace(app);
            else
                log::Write("autotest: run finished; quitting without saving");
            Quit(app);
        }
    }

    // The new-day agent: fingerprint, and pause on the last day before the
    // simulator runs another tick, so the saved city is the same moment of
    // simulation time in every run.
    static void OnNewDay(void* context)
    {
        auto* self = static_cast<AutoTestService*>(context);
        cISC4AppPtr app;
        cISC4City* city = app ? app->GetCity() : nullptr;
        cISC4Simulator* sim = city ? city->GetSimulator() : nullptr;
        if (!sim || self->reachedEndDay_)
            return;
        const int32_t day = sim->GetSimDateNumber();
        self->LogFpuControlWord(day);
        rng_trace::OnNewDay(day);
        virtual_clock::OnNewDay(day);
        if (self->fingerprint_ && day != self->lastFingerprintDay_) {
            self->lastFingerprintDay_ = day;
            self->Fingerprint(city, day);
        }
        // OPENSC4_TEST_POWER_WAKE=<n>: on the n-th day of the run, wake the
        // power simulator as message 0x6AC284F3 does (the network tools send
        // it; +0x14 of cSC4PowerSimulator, build 641), so its recompute runs
        // in a run without network edits (docs/power.md).
        if (const int wake = EnvInt("OPENSC4_TEST_POWER_WAKE", -1);
            wake >= 0 && self->runDays_ >= 0 && day == self->endDay_ - self->runDays_ + wake && game::IsBuild641()) {
            if (void* const power = city->GetPowerSimulator()) {
                static_cast<uint8_t*>(power)[0x14] = 1;
                log::Write("autotest: power simulator woken on day %d", day);
            }
        }
        // OPENSC4_TEST_WATER_WAKE=<n>: the same for the plumbing simulator
        // (+0x14 of cSC4PlumbingSimulator, set by the same message; docs/water.md).
        if (const int wake = EnvInt("OPENSC4_TEST_WATER_WAKE", -1);
            wake >= 0 && self->runDays_ >= 0 && day == self->endDay_ - self->runDays_ + wake && game::IsBuild641()) {
            if (void* const water = city->GetPlumbingSimulator()) {
                static_cast<uint8_t*>(water)[0x14] = 1;
                log::Write("autotest: plumbing simulator woken on day %d", day);
            }
        }
        // OPENSC4_TEST_CRIME_MONTH=<n>: every n days of the run, restart the
        // crime simulator's monthly pass as MonthPassed does (state +0x90 from
        // 6, waiting, to 0; cSC4CrimeSimulator, build 641), so a week's run
        // goes through it without a month's end (docs/crime.md).
        if (const int every = EnvInt("OPENSC4_TEST_CRIME_MONTH", -1); every > 0 && self->runDays_ >= 0 &&
                                                                     game::IsBuild641()) {
            const int into = day - (self->endDay_ - self->runDays_);
            if (into > 0 && into % every == 0)
                if (void* const crime = city->GetCrimeSimulator(); crime && static_cast<uint8_t*>(crime)[0x90] == 6) {
                    static_cast<uint8_t*>(crime)[0x90] = 0;
                    log::Write("autotest: crime month restarted on day %d", day);
                }
        }
        // OPENSC4_TEST_OCCUPANT_CYCLE=<n>: on the n-th day, hand the water and
        // power simulators "removed" then "added" for an existing pipe and
        // pole (messages 0x99EF1143, 0x99EF1142), and power the plant
        // disaster message (0xC2C09554 with 0xE0D0722E), so the handlers a
        // network edit or a disaster runs are reached (docs/parity.md,
        // "Coverage").
        if (const int when = EnvInt("OPENSC4_TEST_OCCUPANT_CYCLE", -1);
            when >= 0 && self->runDays_ >= 0 && day == self->endDay_ - self->runDays_ + when && game::IsBuild641())
            CycleOccupants(city);
        // OPENSC4_TEST_POWER_MESSAGES=<n>: on the n-th day, hand the power
        // simulator (its +0x4 cIGZMessageTarget2, build 641) the two network
        // messages a network edit would bring, over the whole city: a network
        // change (0x278E6003, corners packed in data 1 and 2) and a changed
        // cell rectangle (0x07699455, the rectangle in void 2), so their
        // handlers run in runs without edits (docs/power.md).
        if (const int when = EnvInt("OPENSC4_TEST_POWER_MESSAGES", -1);
            when >= 0 && self->runDays_ >= 0 && day == self->endDay_ - self->runDays_ + when && game::IsBuild641()) {
            if (void* const power = city->GetPowerSimulator())
                SendPowerMessages(power, city);
        }
        if (self->runDays_ >= 0 && day >= self->endDay_) {
            sim->Pause();
            self->reachedEndDay_ = true;
            log::Write("autotest: day %d reached in the simulator's tick (sim time %d); paused", day, sim->GetSimTime());
        }
    }

    // A message of the given type with void 1 = object and data 2/3, to a
    // simulator's cIGZMessageTarget2 at +0x4 (its slot 3).
    static void Deliver(void* simulator, uint32_t type, void* object, uint32_t data2, uint32_t data3)
    {
        using NewFn = void*(__cdecl*)(std::size_t size);
        using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
        using DoMessageFn = bool(__thiscall*)(void* target, cIGZMessage2* message);
        void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        cIGZMessage2Standard* const message =
            memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        if (!message)
            return;
        message->AddRef();
        message->SetType(type);
        message->SetVoid1(object);
        message->SetData2(static_cast<intptr_t>(data2));
        message->SetData3(static_cast<intptr_t>(data3));
        void* const target = static_cast<char*>(simulator) + 0x4;
        reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3])(target, message);
        message->Release();
    }

    // The occupant (interface 0x26BB767E) behind a network piece, referenced.
    static void* OccupantOf(void* piece)
    {
        void* occupant = nullptr;
        if (!piece || !Virtual<uint8_t>(piece, 0x0, 0x26BB767Eu, &occupant))
            return nullptr;
        return occupant;
    }

    static void CycleOccupants(cISC4City* city)
    {
        void* const water = city->GetPlumbingSimulator();
        void* const power = city->GetPowerSimulator();
        void* pipe = nullptr;
        if (water) {
            void** const pipes = *reinterpret_cast<void***>(static_cast<char*>(water) + 0x170);
            if (pipes[0] != pipes)
                pipe = OccupantOf(static_cast<void**>(pipes[0])[2]);
        }
        void* pole = nullptr;
        if (power) {
            void** const begin = *reinterpret_cast<void***>(static_cast<char*>(power) + 0x29C);
            void** const end = *reinterpret_cast<void***>(static_cast<char*>(power) + 0x2A0);
            if (begin != end)
                pole = OccupantOf(*begin);
        }
        for (void* const occupant : {pipe, pole}) {
            if (!occupant)
                continue;
            for (const uint32_t type : {0x99EF1143u, 0x99EF1142u})
                for (void* const simulator : {water, power})
                    if (simulator)
                        Deliver(simulator, type, occupant, 0, 0);
            Virtual<uint32_t>(occupant, 0x8);
        }
        if (power)
            Deliver(power, 0xC2C09554, nullptr, 0xE0D0722E, 1);
        log::Write("autotest: occupants cycled (pipe %s, pole %s), plant disaster message sent", pipe ? "yes" : "none",
                   pole ? "yes" : "none");
    }

    static void SendPowerMessages(void* power, cISC4City* city)
    {
        using NewFn = void*(__cdecl*)(std::size_t size);
        using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
        using DoMessageFn = bool(__thiscall*)(void* target, cIGZMessage2* message);
        static int32_t rect[4];
        const auto cellsX = Virtual<uint32_t>(city, 0x174); // the cell counts, as the power simulator reads them
        const auto cellsZ = Virtual<uint32_t>(city, 0x178);
        rect[0] = 0;
        rect[1] = 0;
        rect[2] = static_cast<int32_t>(cellsX) - 1;
        rect[3] = static_cast<int32_t>(cellsZ) - 1;
        void* const target = static_cast<char*>(power) + 0x4;
        const auto doMessage = reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3]);
        for (const uint32_t type : {0x278E6003u, 0x07699455u}) {
            void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
            cIGZMessage2Standard* const message =
                memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
            if (!message)
                return;
            message->AddRef();
            message->SetType(type);
            if (type == 0x278E6003u) {
                message->SetData1(static_cast<intptr_t>((1u << 16) | (cellsX - 1)));
                message->SetData2(static_cast<intptr_t>((1u << 16) | (cellsZ - 1)));
                message->SetData4(0);
            } else {
                message->SetVoid2(rect);
                message->SetData3(0);
            }
            doMessage(target, message);
            message->Release();
        }
        log::Write("autotest: power messages 0x278E6003 and 0x07699455 delivered over %ux%u cells", cellsX, cellsZ);
    }

    // The x87 control word the simulation runs with (docs/pollution.md,
    // "Floating point"), logged the first time and whenever it changes.
    void LogFpuControlWord(int32_t day)
    {
        uint16_t word = 0;
        __asm fnstcw word ptr [word]
        if (word == lastFpuControlWord_)
            return;
        lastFpuControlWord_ = word;
        static const char* const kPrecision[] = {"24-bit (single)", "reserved", "53-bit (double)", "64-bit (extended)"};
        static const char* const kRounding[] = {"nearest", "down", "up", "toward zero"};
        log::Write("autotest: x87 control word %04X on day %d in the simulator's tick: precision %s, rounding %s, "
                   "exception masks %02X",
                   word, day, kPrecision[(word >> 8) & 3], kRounding[(word >> 10) & 3], word & 0x3F);
    }

    // OPENSC4_TEST_FINGERPRINT=1: one line per game day. Numbers, not
    // memory, so two runs compare exactly whatever the heap layout.
    void Fingerprint(cISC4City* city, int32_t day)
    {
        cISC4ResidentialSimulator* res = city->GetResidentialSimulator();
        cISC4BudgetSimulator* budget = city->GetBudgetSimulator();
        cISC4LotManager* lots = city->GetLotManager();
        cISC4TrafficSimulator* traffic = city->GetTrafficSimulator();
        cISC4LandValueSimulator* land = city->GetLandValueSimulator();
        cISC4PollutionSimulator* pollution = city->GetPollutionSimulator();
        // The worst tracts and the generator that places pollution effects
        // (a Mersenne twister: 624 words of state, then a pointer into them
        // and a countdown): nothing else reads them, so they are
        // fingerprinted directly.
        uint32_t worstX[3] = {}, worstZ[3] = {};
        int32_t worstValue[3] = {};
        if (pollution) {
            pollution->GetWorstAirTract(worstX[0], worstZ[0], worstValue[0]);
            pollution->GetWorstWaterTract(worstX[1], worstZ[1], worstValue[1]);
            pollution->GetWorstGarbageTract(worstX[2], worstZ[2], worstValue[2]);
        }
        const uint32_t effectRandom =
            game::IsBuild641() ? BytesChecksum(reinterpret_cast<const void*>(addr::PollutionTuning_EffectRandom), 0x9C8) : 0u;
        // Power (build 641 vtable slots, not gzcom-dll's declarations, whose
        // overloads compile in another order): the powered grid (slot 35),
        // the monthly requirement, production, import, export, demand and
        // consumption (11-15, 17; 16 takes a cell), unpowered and powered consumers (21, 22), power
        // plants (24) and poles (26), and overworked, aged and distressed
        // plants (27-29).
        void* const power = city->GetPowerSimulator();
        char powerText[192] = " | power none";
        if (power && game::IsBuild641()) {
            auto slot = [power](int index) {
                using Fn = int32_t(__thiscall*)(void*);
                return reinterpret_cast<Fn>((*static_cast<void***>(power))[index])(power);
            };
            using GridFn = cISC4SimGrid<uint8_t>*(__thiscall*)(void*);
            auto* const powered = reinterpret_cast<GridFn>((*static_cast<void***>(power))[35])(power);
            std::snprintf(powerText, sizeof powerText,
                          " | power %08X req %d prod %d imp %d exp %d dem %d con %d | consumers %d/%d plants %d poles "
                          "%d %d/%d/%d",
                          GridChecksum(powered), slot(11), slot(12), slot(13), slot(14), slot(15), slot(17), slot(21),
                          slot(22), slot(24), slot(26), slot(27), slot(28), slot(29));
        }
        // Water (cSC4PlumbingSimulator, build 641 slots): the watered grid
        // (slot 7), water sources and polluted sources (12, 13), production
        // capacity, consumption, import and export (14-17), pipes, distressed
        // and burst pipes (18-20), watered and unwatered buildings (21, 22)
        // and demand (23) (docs/water.md).
        void* const water = city->GetPlumbingSimulator();
        char waterText[192] = " | water none";
        if (water && game::IsBuild641()) {
            auto slot = [water](int index) {
                using Fn = int32_t(__thiscall*)(void*);
                return reinterpret_cast<Fn>((*static_cast<void***>(water))[index])(water);
            };
            using GridFn = cISC4SimGrid<uint8_t>*(__thiscall*)(void*);
            auto* const watered = reinterpret_cast<GridFn>((*static_cast<void***>(water))[7])(water);
            std::snprintf(waterText, sizeof waterText,
                          " | water %08X src %d/%d cap %d con %d imp %d exp %d pipes %d/%d/%d bld %d/%d dem %d",
                          GridChecksum(watered), slot(12), slot(13), slot(14), slot(15), slot(16), slot(17), slot(18),
                          slot(19), slot(20), slot(21), slot(22), slot(23));
        }
        // Crime (cSC4CrimeSimulator, build 641 slots): the crime grid (slot
        // 5), average and monthly count (6, 7), the hotspot (8), the step's
        // fields (+0x7C to +0xD8: count, hotspot, ordinance factor, state,
        // the month's target and its parts) and the incidents (crime type
        // and name key each) (docs/crime.md).
        void* const crime = city->GetCrimeSimulator();
        char crimeText[224] = " | crime none";
        if (crime && game::IsBuild641()) {
            void** const vtable = *static_cast<void***>(crime);
            using GridFn = cISC4SimGrid<uint8_t>*(__thiscall*)(void*);
            using CountFn = uint32_t(__thiscall*)(void*);
            using HotspotFn = uint8_t(__thiscall*)(void*, uint8_t*, int32_t*, int32_t*);
            uint8_t hotValue = 0;
            int32_t hotX = 0, hotZ = 0;
            const uint8_t hot = reinterpret_cast<HotspotFn>(vtable[8])(crime, &hotValue, &hotX, &hotZ);
            char* const base = static_cast<char*>(crime);
            char* const* const incidents = *reinterpret_cast<char* const* const*>(base + 0x70);
            const uint32_t incidentCount =
                static_cast<uint32_t>(*reinterpret_cast<char* const* const*>(base + 0x74) - incidents);
            uint32_t incidentSum = 0;
            for (uint32_t i = 0; i < incidentCount; ++i)
                if (incidents[i])
                    incidentSum = incidentSum * 31 + BytesChecksum(incidents[i] + 4, 4) * 7 +
                                  BytesChecksum(incidents[i] + 0x1C, 12);
            const int written = std::snprintf(
                crimeText, sizeof crimeText, " | crime %08X avg %u cnt %u hot %u %u,%d,%d st %08X inc %u %08X",
                GridChecksum(reinterpret_cast<GridFn>(vtable[5])(crime)), reinterpret_cast<CountFn>(vtable[6])(crime),
                reinterpret_cast<CountFn>(vtable[7])(crime), hot, hotValue, hotX, hotZ,
                BytesChecksum(base + 0x7C, 0x5C), incidentCount, incidentSum);
            // OPENSC4_TEST_CRIME_QUERIES=1: the queries only the UI and the
            // automata make (slots 9-13): the incident events, the visuals
            // flag of the first incident (set, then read), and whether a
            // crime sim and a crime prop are within 2 km of the hotspot.
            if (EnvInt("OPENSC4_TEST_CRIME_QUERIES", 0) > 0 && written > 0) {
                struct {
                    char* begin = nullptr;
                    char* end = nullptr;
                    char* capacity = nullptr;
                } events;
                using EventsFn = uint8_t(__thiscall*)(void*, void*);
                const uint8_t any = reinterpret_cast<EventsFn>(vtable[11])(crime, &events);
                const uint32_t eventSum =
                    events.begin ? BytesChecksum(events.begin, static_cast<size_t>(events.end - events.begin)) : 0u;
                if (events.begin)
                    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(events.begin);
                using SetFn = uint8_t(__thiscall*)(void*, uint32_t, uint8_t);
                using GetFn = uint8_t(__thiscall*)(void*, uint32_t);
                const uint8_t set = reinterpret_cast<SetFn>(vtable[12])(crime, 0, 1);
                const uint8_t visuals = reinterpret_cast<GetFn>(vtable[13])(crime, 0);
                const float position[3] = {static_cast<float>(hotX * 16 + 8), 0.0f, static_cast<float>(hotZ * 16 + 8)};
                using NearestFn = void*(__thiscall*)(void*, const float*, float);
                const bool sim = reinterpret_cast<NearestFn>(vtable[9])(crime, position, 2000.0f) != nullptr;
                const bool prop = reinterpret_cast<NearestFn>(vtable[10])(crime, position, 2000.0f) != nullptr;
                std::snprintf(crimeText + written, sizeof crimeText - written, " q %u %08X %u%u %u%u", any, eventSum,
                              set, visuals, sim, prop);
            }
        }
        // Traffic (build 641 slots): trip scale and the one shown (14, 15),
        // last month's income (18) and the freight scaling (27), as float bits.
        char trafficText[96] = "";
        if (traffic && game::IsBuild641()) {
            auto slot = [traffic](int index) {
                using Fn = float(__thiscall*)(void*);
                const float value = reinterpret_cast<Fn>((*reinterpret_cast<void***>(traffic))[index])(traffic);
                uint32_t bits;
                std::memcpy(&bits, &value, sizeof bits);
                return bits;
            };
            std::snprintf(trafficText, sizeof trafficText, " | traffic %08X %08X %08X %08X", slot(14), slot(15),
                          slot(18), slot(27));
        }
        // Land value (the total grid is in the main line): the intrinsic and
        // slope grids, the average land value, the mode and average altitude
        // (float bits) and the step's state (+0x78, +0x38; docs/landvalue.md).
        char landText[96] = "";
        if (land && game::IsBuild641()) {
            const float mode = land->GetModeAltitude();
            uint32_t modeBits, altitudeBits;
            std::memcpy(&modeBits, &mode, sizeof modeBits);
            std::memcpy(&altitudeBits, reinterpret_cast<const char*>(land) + 0x78, sizeof altitudeBits);
            std::snprintf(landText, sizeof landText, " | lv %08X %08X avg %u mode %08X alt %08X st %d",
                          GridChecksum(land->GetIntrinsicLandValueMap()), GridChecksum(land->GetSlopeMap()),
                          land->GetAverageLandValue(), modeBits, altitudeBits,
                          *reinterpret_cast<const int32_t*>(reinterpret_cast<const char*>(land) + 0x38));
        }
        char rng[256];
        rng_trace::Describe(rng, sizeof rng);
        char ticks[64];
        fixed_ticks::Describe(ticks, sizeof ticks);
        log::Write("fp: day %d pop %d funds %lld lots %d | congestion %08X trips %08X commercial %08X airtraffic "
                   "%08X | landvalue %08X | pollution %08X %08X %08X | worst %u,%u=%d %u,%u=%d %u,%u=%d fx %08X%s%s%s%s%s%s%s",
                   day, res ? res->GetPopulation() : -1, budget ? static_cast<long long>(budget->GetTotalFunds()) : -1LL,
                   lots ? lots->GetLotCount() : -1, traffic ? GridChecksum(traffic->GetCongestionMap()) : 0u,
                   traffic ? GridChecksum(traffic->GetTripLengthMap()) : 0u,
                   traffic ? GridChecksum(traffic->GetCommercialTrafficMap()) : 0u,
                   traffic ? GridChecksum(traffic->GetAirPollutingTrafficMap()) : 0u,
                   land ? GridChecksum(land->GetTotalLandValueMap()) : 0u,
                   pollution ? GridChecksum(pollution->GetPollutionGrid(0)) : 0u,
                   pollution ? GridChecksum(pollution->GetPollutionGrid(1)) : 0u,
                   pollution ? GridChecksum(pollution->GetPollutionGrid(2)) : 0u, worstX[0], worstZ[0], worstValue[0],
                   worstX[1], worstZ[1], worstValue[1], worstX[2], worstZ[2], worstValue[2], effectRandom, powerText,
                   waterText, crimeText, trafficText, landText, ticks, rng);
    }

    // OPENSC4_TEST_SAVE=1. cISC4App::SaveCity(path, fastSave) (slot 15,
    // 0x00455E20) is no way to save a copy elsewhere: given a path outside the
    // region it still rewrote the city's own file and six neighbours (why is
    // not understood yet). So save in place, like the game's Save.
    void SaveInPlace(cISC4App* app)
    {
        virtual_clock::ReturnToRealTime();
        const ULONGLONG start = GetTickCount64();
        const bool saved = app->SaveCity(false);
        log::Write("autotest: run finished; SaveCity %s after %llu ms; quitting", saved ? "ok" : "FAILED",
                   GetTickCount64() - start);
    }

    // Every zoom (1-5) at every rotation (0-3): set the view, wait for it to
    // settle, then save the frame with the game's own snapshot code.
    void Shots(ULONGLONG now)
    {
        constexpr int kZooms = 5, kRotations = 4;
        constexpr int kRedrawMs = 1000;
        cISC4AppPtr app;
        if (!app || !app->GetCity()) {
            log::Write("autotest: city went away while taking shots");
            state_ = State::Done;
            return;
        }
        if (shot_ >= 0 && now - stateStart_ < static_cast<ULONGLONG>(redrawn_ ? kRedrawMs : settleMs_))
            return;
        cRZAutoRefCount<cISC4View3DWin> view = GetView3D(app);
        cISC43DRender* renderer = view ? view->GetRenderer() : nullptr;
        if (!renderer) {
            log::Write("autotest: no 3D view or renderer; quitting");
            Quit(app);
            return;
        }
        // The view redraws only what it thinks changed (backing store), so
        // parts drawn before textures finished loading would stay that way.
        if (shot_ >= 0 && !redrawn_) {
            renderer->ForceFullRedraw();
            redrawn_ = true;
            Enter(State::Shots, now);
            return;
        }
        redrawn_ = false;
        if (shot_ >= 0) {
            const int zoom = shot_ / kRotations + 1, rotation = shot_ % kRotations;
            char name[64];
            std::snprintf(name, sizeof(name), "\\z%d-r%d.png", zoom, rotation);
            const std::string path = shotDir_ + name;
            cRZBaseString file(path.c_str());
            const bool taken = TakeSnapshotToFile(renderer, file);
            log::Write("autotest: shot zoom %d rotation %d -> %s %s", zoom, rotation, path.c_str(),
                       taken ? "ok" : "FAILED");
        }
        if (++shot_ >= kZooms * kRotations) {
            log::Write("autotest: shots done; quitting without saving");
            Quit(app);
            return;
        }
        const int zoom = shot_ / kRotations + 1, rotation = shot_ % kRotations;
        const int32_t result = view->SetZoomAndRotation(zoom, rotation);
        uint32_t width = 0, height = 0;
        renderer->GetViewportSize(width, height);
        log::Write("autotest: view zoom %d rotation %d (SetZoomAndRotation %d, viewport %ux%u)", zoom, rotation,
                   result, width, height);
        Enter(State::Shots, now);
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
    int runDays_ = -1; // OPENSC4_TEST_DAYS; -1: run for runSeconds_
    bool fingerprint_ = Env("OPENSC4_TEST_FINGERPRINT") == "1";
    int32_t lastFingerprintDay_ = -1;
    uint32_t lastFpuControlWord_ = 0xFFFFFFFF;
    int endDay_ = 0;
    DayAgent agent_{&AutoTestService::OnNewDay, this};
    bool agentAdded_ = false;
    bool reachedEndDay_ = false;
    std::string shotDir_;
    int settleMs_ = 3000;
    int shot_ = -1;
    bool redrawn_ = false;
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
    // As early as the app exists: some render properties are read only when
    // the view is set up.
    if (cISC4AppPtr app; app)
        ApplyRenderProperties(app);
    auto* service = new AutoTestService();
    const bool added = framework->AddSystemService(service) && framework->AddToTick(service);
    log::Write("autotest: tick service %s (OPENSC4_TEST_CITY=\"%s\")", added ? "started" : "FAILED to start",
               Env("OPENSC4_TEST_CITY").c_str());
}

} // namespace opensc4::autotest
