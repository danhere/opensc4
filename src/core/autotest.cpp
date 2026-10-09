#include "core/autotest.h"

#include "core/autotest_hooks.h"

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
#include <cISC4LandValueSimulator.h>
#include <cISC4BudgetSimulator.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>
#include <cRZBaseString.h>
#include <cRZBaseSystemService.h>
#include <cRZCOMDllDirector.h>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::autotest {

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

bool OnRunDay(const Day& day, const char* name)
{
    const int when = EnvInt(name, -1);
    return when >= 0 && day.counted && day.day == day.first + when;
}

bool EveryRunDays(const Day& day, const char* name)
{
    const int every = EnvInt(name, -1);
    if (every <= 0 || !day.counted)
        return false;
    const int into = day.day - day.first;
    return into > 0 && into % every == 0;
}

// A message of the given type with void 1 = object and data 2/3, to a
// simulator's cIGZMessageTarget2 at +0x4 (its slot 3).
void Deliver(void* simulator, uint32_t type, void* object, uint32_t data2, uint32_t data3)
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

namespace {

// The simulators' hooks (autotest_hooks.h), each kind kept sorted by order,
// then name. Function-local, so registrations from static initialisers in any
// order work.
template <typename Fn>
struct Hook {
    const char* name;
    int order;
    Fn fn;
};

struct Hooks {
    std::vector<Hook<FingerprintFn>> fingerprint;
    std::vector<Hook<LoadHookFn>> load;
    std::vector<Hook<DayHookFn>> day;
};

Hooks& Registry()
{
    static Hooks hooks;
    return hooks;
}

template <typename Fn>
void Insert(std::vector<Hook<Fn>>& hooks, const char* name, int order, Fn fn)
{
    const auto before = [](const Hook<Fn>& a, const Hook<Fn>& b) {
        return a.order != b.order ? a.order < b.order : std::strcmp(a.name, b.name) < 0;
    };
    const Hook<Fn> hook{name, order, fn};
    hooks.insert(std::upper_bound(hooks.begin(), hooks.end(), hook, before), hook);
}

} // namespace

void RegisterFingerprintPart(const char* name, int order, FingerprintFn fn)
{
    Insert(Registry().fingerprint, name, order, fn);
}

void RegisterLoadHook(const char* name, int order, LoadHookFn fn)
{
    Insert(Registry().load, name, order, fn);
}

void RegisterDayHook(const char* name, int order, DayHookFn fn)
{
    Insert(Registry().day, name, order, fn);
}

namespace {

// "OSC4" + 1. System service IDs must be unique.
constexpr uint32_t kServiceId = 0x4F534335;

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

// cISC43DRender::TakeSnapshot(cIGZString&, bool), vtable slot 22 (0x007C2570
// in 641): writes the current frame to a file. Called by slot because
// gzcom-dll declares the two TakeSnapshot overloads in the exe's slot order,
// and MSVC-ABI compilers lay out overloaded virtuals in reverse declaration
// order, so the header's string overload compiles to slot 23 (the
// cIGZBuffer** one).
bool TakeSnapshotToFile(cISC43DRender* renderer, cIGZString& path, bool flag = false)
{
    using Fn = bool(__thiscall*)(cISC43DRender*, cIGZString&, bool);
    void** vtable = *reinterpret_cast<void***>(renderer);
    return reinterpret_cast<Fn>(vtable[22])(renderer, path, flag);
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

// The occupant (interface 0x26BB767E) behind a network piece, referenced.
void* OccupantOf(void* piece)
{
    void* occupant = nullptr;
    if (!piece || !Virtual<uint8_t>(piece, 0x0, 0x26BB767Eu, &occupant))
        return nullptr;
    return occupant;
}

// OPENSC4_TEST_OCCUPANT_CYCLE=<n>: on the n-th day, hand the water and
// power simulators "removed" then "added" for an existing pipe and
// pole (messages 0x99EF1143, 0x99EF1142), and power the plant
// disaster message (0xC2C09554 with 0xE0D0722E), so the handlers a
// network edit or a disaster runs are reached (docs/parity.md,
// "Coverage").
void CycleOccupants(cISC4City* city)
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

void CycleOccupantsHook(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_OCCUPANT_CYCLE") && game::IsBuild641())
        CycleOccupants(day.city);
}

const DayHook kCycleOccupants{"occupants.cycle", 100, &CycleOccupantsHook};

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
        // Give the region view a few seconds to settle before loading (one
        // with OPENSC4_TEST_QUICK=1).
        const ULONGLONG settle = Env("OPENSC4_TEST_QUICK") == "1" ? 1000 : 5000;
        if (!region || now - stateStart_ < settle) {
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
        // Each simulator's OPENSC4_TEST_* load switches (autotest_hooks.h).
        for (const auto& hook : Registry().load)
            hook.fn(city);

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
        // Each simulator's OPENSC4_TEST_* day switches (autotest_hooks.h).
        const Day today{city, day, self->endDay_ - self->runDays_, self->runDays_ >= 0};
        for (const auto& hook : Registry().day)
            hook.fn(today);
        if (self->runDays_ >= 0 && day >= self->endDay_) {
            sim->Pause();
            self->reachedEndDay_ = true;
            log::Write("autotest: day %d reached in the simulator's tick (sim time %d); paused", day, sim->GetSimTime());
        }
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
        // Each simulator's own part (autotest_hooks.h), in order, before the
        // RNG and tick counts, which their queries can move.
        std::string parts;
        for (const auto& part : Registry().fingerprint)
            part.fn(city, parts);
        char rng[256];
        rng_trace::Describe(rng, sizeof rng);
        char ticks[64];
        fixed_ticks::Describe(ticks, sizeof ticks);
        char head[512];
        std::snprintf(head, sizeof head,
                      "fp: day %d pop %d funds %lld lots %d | congestion %08X trips %08X commercial %08X airtraffic "
                      "%08X | landvalue %08X | pollution %08X %08X %08X | worst %u,%u=%d %u,%u=%d %u,%u=%d fx %08X",
                      day, res ? res->GetPopulation() : -1, budget ? static_cast<long long>(budget->GetTotalFunds()) : -1LL,
                      lots ? lots->GetLotCount() : -1, traffic ? GridChecksum(traffic->GetCongestionMap()) : 0u,
                      traffic ? GridChecksum(traffic->GetTripLengthMap()) : 0u,
                      traffic ? GridChecksum(traffic->GetCommercialTrafficMap()) : 0u,
                      traffic ? GridChecksum(traffic->GetAirPollutingTrafficMap()) : 0u,
                      land ? GridChecksum(land->GetTotalLandValueMap()) : 0u,
                      pollution ? GridChecksum(pollution->GetPollutionGrid(0)) : 0u,
                      pollution ? GridChecksum(pollution->GetPollutionGrid(1)) : 0u,
                      pollution ? GridChecksum(pollution->GetPollutionGrid(2)) : 0u, worstX[0], worstZ[0], worstValue[0],
                      worstX[1], worstZ[1], worstValue[1], worstX[2], worstZ[2], worstValue[2], effectRandom);
        log::Write("%s%s%s%s", head, parts.c_str(), ticks, rng);
    }

    // OPENSC4_TEST_SAVE=1. cISC4App::SaveCity(path, fastSave) (slot 15,
    // 0x00455E20) is no way to save a copy elsewhere: given a path outside the
    // region it still rewrote the city's own file and six neighbours (why is
    // not understood yet). So save in place, like the game's Save.
    void SaveInPlace(cISC4App* app)
    {
        virtual_clock::ReturnToRealTime();
        const ULONGLONG start = GetTickCount64();
        // A fast save leaves out the region view's thumbnails of the city
        // (records 0x8A2482B9 and 0x2026960B) and takes ~1 s instead of ~11.
        const bool saved = app->SaveCity(Env("OPENSC4_TEST_QUICK") == "1");
        log::Write("autotest: run finished; SaveCity %s after %llu ms; quitting", saved ? "ok" : "FAILED",
                   GetTickCount64() - start);
    }

    static void HideInterface(cISC4App* app)
    {
        constexpr uint32_t kWinSC4App = 0x6104489A;
        constexpr uint32_t kWinView3D = 0x9A47B417;
        int hidden = 0;
        const auto hide = [](cIGZWin*, uint32_t id, void* child, void* context) -> bool {
            auto* win = static_cast<cIGZWin*>(child);
            if (id != kWinSC4App && id != kWinView3D && win->IsVisible()) {
                win->HideWindow();
                ++*static_cast<int*>(context);
            }
            return true;
        };
        cIGZWin* main = app->GetMainWindow();
        cIGZWin* parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
        if (main)
            main->EnumChildren(GZIID_cIGZWin, hide, &hidden);
        if (parent)
            parent->EnumChildren(GZIID_cIGZWin, hide, &hidden);
        // The city view's own children: the control panel, the pause frame.
        if (cIGZWin* view = parent ? parent->GetChildWindowFromID(kWinView3D) : nullptr)
            view->EnumChildren(GZIID_cIGZWin, hide, &hidden);
        log::Write("autotest: %d interface windows hidden for the shots", hidden);
    }

    // Every zoom (1-5) at every rotation (0-3), or the views in
    // OPENSC4_TEST_SHOT_VIEWS ("zoom:rotation,...", e.g. "2:0"): set the view,
    // wait for it to settle, then save the frame with the game's own snapshot
    // code.
    void Shots(ULONGLONG now)
    {
        constexpr int kZooms = 5, kRotations = 4;
        constexpr int kRedrawMs = 1000;
        if (views_.empty()) {
            const std::string list = Env("OPENSC4_TEST_SHOT_VIEWS");
            for (size_t i = 0; i < list.size();) {
                const int zoom = std::atoi(list.c_str() + i);
                const size_t colon = list.find(':', i);
                const int rotation = colon == std::string::npos ? 0 : std::atoi(list.c_str() + colon + 1);
                if (zoom >= 0 && zoom <= kZooms && rotation >= 0 && rotation < kRotations)
                    views_.push_back(zoom * kRotations + rotation);
                const size_t comma = list.find(',', i);
                if (comma == std::string::npos)
                    break;
                i = comma + 1;
            }
            if (views_.empty())
                for (int v = kRotations; v < (kZooms + 1) * kRotations; ++v)
                    views_.push_back(v);
        }
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
            const int zoom = views_[shot_] / kRotations, rotation = views_[shot_] % kRotations;
            char name[64];
            std::snprintf(name, sizeof(name), "\\z%d-r%d.png", zoom, rotation);
            const std::string path = shotDir_ + name;
            cRZBaseString file(path.c_str());
            const bool taken = TakeSnapshotToFile(renderer, file, Env("OPENSC4_TEST_SHOT_FLAG") == "1");
            log::Write("autotest: shot zoom %d rotation %d -> %s %s", zoom, rotation, path.c_str(),
                       taken ? "ok" : "FAILED");
        }
        if (++shot_ >= static_cast<int>(views_.size())) {
            log::Write("autotest: shots done; quitting without saving");
            Quit(app);
            return;
        }
        const int zoom = views_[shot_] / kRotations, rotation = views_[shot_] % kRotations;
        // OPENSC4_TEST_SHOT_HIDE_UI=1: every window but the city view hidden
        // (the game's snapshot is of the whole screen).
        if (shot_ == 0 && Env("OPENSC4_TEST_SHOT_HIDE_UI") == "1")
            HideInterface(app);
        // OPENSC4_TEST_SHOT_CENTER=1: the view centred on the middle of the
        // city (the camera control's view centre, x at +0x44 and z at +0x4C;
        // docs/timelapse.md), as for a timelapse.
        if (Env("OPENSC4_TEST_SHOT_CENTER") == "1") {
            if (auto* centre = reinterpret_cast<float*>(renderer->GetCameraControl())) {
                cISC4City* city = app->GetCity();
                centre[0x44 / 4] = city->SizeX() / 2;
                centre[0x4C / 4] = city->SizeZ() / 2;
            }
        }
        const int32_t result = view->SetZoomAndRotation(zoom, rotation);
        uint32_t width = 0, height = 0;
        renderer->GetViewportSize(width, height);
        const auto* control = reinterpret_cast<const int32_t*>(renderer->GetCameraControl());
        log::Write("autotest: view zoom %d rotation %d (SetZoomAndRotation %d, viewport %ux%u, camera zoom %d)", zoom,
                   rotation, result, width, height, control ? control[0x108 / 4] : -1);
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
    std::vector<int> views_; // zoom * 4 + rotation, in shot order
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
