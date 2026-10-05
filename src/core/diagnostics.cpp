#include "core/diagnostics.h"

#include "core/coverage.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <string>

namespace opensc4::diagnostics {
namespace {

std::wstring NextToLog(const wchar_t* name)
{
    wchar_t dir[MAX_PATH];
    return log::Directory(dir) ? std::wstring(dir) + name : std::wstring();
}

// diagnostics.coverage: one-shot breakpoints on every function in
// opensc4-functions.csv (tools/ghidra/ExportFunctions.java, copied next to
// core.dll by scripts/run-autotest.sh).
bool ApplyCoverage(registry::Mode)
{
    const std::wstring list = NextToLog(L"opensc4-functions.csv");
    const std::vector<coverage::Function> functions = coverage::LoadFunctionList(list);
    if (functions.empty()) {
        log::Write("coverage: no function list at %ls", list.c_str());
        return false;
    }
    return coverage::Start(functions) > 0;
}

const bool g_coverage = registry::Register({
    "diagnostics.coverage",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::LateInit,
    0,
    "Records which game functions run (one-shot breakpoints at every function entry)",
    false,
    ApplyCoverage,
});

} // namespace

void OnShutdown()
{
    if (registry::ModeOf("diagnostics.coverage") == registry::Mode::Original)
        return;
    const coverage::Summary summary = coverage::GetSummary();
    const std::wstring path = NextToLog(L"opensc4-coverage.csv");
    const bool written = coverage::WriteHits(path);
    coverage::Stop();
    log::Write("coverage: %zu of %zu instrumented functions ran (%.1f%%); %ls %ls", summary.hit, summary.planted,
               summary.planted ? 100.0 * summary.hit / summary.planted : 0.0, written ? L"written to" : L"could not write",
               path.c_str());
}

} // namespace opensc4::diagnostics
