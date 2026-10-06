#include "core/autotest.h"

#include "core/crash.h"
#include "core/log.h"
#include "core/pollution_proxy.h"
#include "core/virtual_clock.h"

#include <GZServPtrs.h>
#include <SC4List.h>
#include <SC4String.h>
#include <cIGZFrameWork.h>
#include <cIGZWin.h>
#include <cISC43DRender.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4PollutionSimulator.h>
#include <cISC4Region.h>
#include <cISC4RenderProperties.h>
#include <cISC4RegionalCity.h>
#include <cISC4Simulator.h>
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
            if (noTime)
                sim->Pause(); // no simulation time at all, not even one tick
            else
                sim->Resume();
        }
        runSeconds_ = EnvInt("OPENSC4_TEST_SECONDS", 180);
        runDays_ = Env("OPENSC4_TEST_DAYS").empty() ? -1 : EnvInt("OPENSC4_TEST_DAYS", 0);
        if (runDays_ >= 0 && sim) {
            endDay_ = sim->GetSimDateNumber() + runDays_;
            log::Write("autotest: running %d game days, day %d to %d, at speed %d (SetSimSpeed %s)", runDays_,
                       sim->GetSimDateNumber(), endDay_, speed, speedSet ? "ok" : "refused");
        } else {
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
        const bool done = runDays_ >= 0 ? sim && sim->GetSimDateNumber() >= endDay_
                                        : now - stateStart_ >= static_cast<ULONGLONG>(runSeconds_) * 1000;
        if (done) {
            if (sim)
                sim->Pause();
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
    int endDay_ = 0;
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
