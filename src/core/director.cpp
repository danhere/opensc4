// core.dll's GZCOM director. The game calls GZDllGetGZCOMDirector (exported by
// gzcom-dll's cRZCOMDllDirector.cpp) when it loads core.dll from a Plugins
// folder; the director then serves the class IDs registered with AddCls and
// drives the replacement registry's phases.
#include "core/director.h"

#include "core/address_check.h"
#include "core/autotest.h"
#include "core/compare.h"
#include "core/crash.h"
#include "core/diagnostics.h"
#include "core/game.h"
#include "core/gdriver_proxy.h"
#include "core/log.h"
#include "core/registry.h"

#include <cIGZFrameWork.h>
#include <cRZCOMDllDirector.h>

namespace {

// "OSC4". Directors must have unique IDs.
constexpr uint32_t kDirectorId = 0x4F534334;

class OpenSC4Director;
OpenSC4Director* g_initializing = nullptr;

class OpenSC4Director final : public cRZCOMDllDirector {
public:
    OpenSC4Director()
    {
        if (!opensc4::game::IsBuild641()) {
            opensc4::log::Write("director: not SimCity 4 1.1.641; no replacements");
            return;
        }
        g_initializing = this;
        opensc4::registry::Apply(opensc4::registry::Phase::DirectorInit);
        g_initializing = nullptr;
    }

    bool AddClass(uint32_t clsid, cIGZUnknown* (*factory)())
    {
        AddCls(clsid, factory);
        return true;
    }

    uint32_t GetDirectorID() const override { return kDirectorId; }

    bool OnStart(cIGZCOM* com) override
    {
        opensc4::log::Write("director: OnStart (GZCOM %p)", static_cast<void*>(com));
        RZGetFrameWork()->AddHook(static_cast<cIGZFrameWorkHooks*>(this));
        return true;
    }

    // The graphics system has created its drivers; the game has not set a
    // video mode or loaded anything yet.
    bool PreAppInit() override
    {
        opensc4::log::Write("director: PreAppInit");
        if (opensc4::game::IsBuild641())
            opensc4::registry::Apply(opensc4::registry::Phase::PreAppInit);
        return true;
    }

    // Every plugin has loaded and applied its patches by now.
    bool PostAppInit() override
    {
        opensc4::log::Write("director: PostAppInit");
        if (!opensc4::game::IsBuild641())
            return true;
        opensc4::crash::InstallHandler(); // after the game's own filter, which it chains to
        opensc4::addr_check::LogManifest();
        opensc4::registry::Apply(opensc4::registry::Phase::PostAppInit);
        opensc4::registry::Apply(opensc4::registry::Phase::LateInit);
        if (opensc4::autotest::Enabled())
            opensc4::autotest::Start();
        return true;
    }

    bool PreAppShutdown() override
    {
        opensc4::compare::LogStats();
        opensc4::gdriver_proxy::LogCallCounts("PreAppShutdown");
        opensc4::diagnostics::OnShutdown();
        return true;
    }

    bool GetClassObject(uint32_t clsid, uint32_t iid, void** object) override
    {
        const bool served = cRZCOMDllDirector::GetClassObject(clsid, iid, object);
        if (served)
            opensc4::log::Write("director: served class 0x%08X as interface 0x%08X", clsid, iid);
        return served;
    }
};

} // namespace

namespace opensc4::director {

bool AddClass(std::uint32_t clsid, cIGZUnknown* (*factory)())
{
    if (!g_initializing) {
        log::Write("director: AddClass(0x%08X) outside DirectorInit; ignored", clsid);
        return false;
    }
    return g_initializing->AddClass(clsid, factory);
}

} // namespace opensc4::director

cRZCOMDllDirector* RZGetCOMDllDirector()
{
    static OpenSC4Director director;
    return &director;
}
