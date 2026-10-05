// core.dll's GZCOM director. The game calls GZDllGetGZCOMDirector (exported by
// gzcom-dll's cRZCOMDllDirector.cpp) when it loads core.dll from a Plugins
// folder; the director then serves class IDs registered with AddCls.
#include "core/autotest.h"
#include "core/game.h"
#include "core/log.h"
#include "core/pollution_proxy.h"

#include <cIGZFrameWork.h>
#include <cRZCOMDllDirector.h>

#include <windows.h>

namespace {

// "OSC4". Directors must have unique IDs.
constexpr uint32_t kDirectorId = 0x4F534334;

bool EnvFlag(const wchar_t* name)
{
    wchar_t value[8];
    const DWORD length = GetEnvironmentVariableW(name, value, 8);
    return length == 1 && value[0] == L'1';
}

class OpenSC4Director final : public cRZCOMDllDirector {
public:
    OpenSC4Director()
    {
        // Experiment, off by default: see docs/experiments/pollution-proxy.md.
        if (EnvFlag(L"OPENSC4_PROXY_POLLUTION")) {
            if (opensc4::game::IsBuild641()) {
                AddCls(opensc4::pollution_proxy::kClassId, opensc4::pollution_proxy::Create);
                opensc4::log::Write("director: registered pollution proxy for class 0x%08X",
                                    opensc4::pollution_proxy::kClassId);
            } else {
                opensc4::log::Write("director: OPENSC4_PROXY_POLLUTION set, but not build 641; ignored");
            }
        }
    }

    uint32_t GetDirectorID() const override { return kDirectorId; }

    bool OnStart(cIGZCOM* com) override
    {
        opensc4::log::Write("director: OnStart (GZCOM %p)", static_cast<void*>(com));
        if (opensc4::autotest::Enabled())
            RZGetFrameWork()->AddHook(static_cast<cIGZFrameWorkHooks*>(this));
        return true;
    }

    bool PostAppInit() override
    {
        opensc4::log::Write("director: PostAppInit");
        opensc4::autotest::Start();
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

cRZCOMDllDirector* RZGetCOMDllDirector()
{
    static OpenSC4Director director;
    return &director;
}
