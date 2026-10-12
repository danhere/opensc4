// diagnostics.gzcom_libraries: logs the COM object's library list once
// startup is done (docs/plugin-dlls.md): every plugin DLL the game knows, in
// the list's order, with its path and whether it is loaded. With
// OPENSC4_TEST_GZCOM_CLASSES=<hex ID>,<hex ID>,... it also asks the COM object
// for an object of each class and logs whether one came; which DLL made it
// shows in that DLL's own log. Read-only, through the public interfaces.
#include "core/crash.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZServPtrs.h>
#include <cIGZCOM.h>
#include <cIGZCOMLibrary.h>
#include <cIGZFrameWork.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace opensc4::gzcom_libraries_diag {
namespace {

void Survey()
{
    cIGZFrameWork* framework = RZGetFrameWork();
    cIGZCOM* com = framework ? framework->GetCOMObject() : nullptr;
    if (!com) {
        log::Write("gzcom: no COM object");
        return;
    }
    uint32_t count = 0;
    com->EnumerateLibraries(nullptr, count);
    std::vector<cIGZCOMLibrary*> libraries(count);
    if (count != 0)
        com->EnumerateLibraries(libraries.data(), count);
    log::Write("gzcom: %u libraries", count);
    for (uint32_t i = 0; i < count; ++i) {
        cRZBaseString path;
        libraries[i]->GetPath(path);
        log::Write("gzcom: [%u] %s %s", i, libraries[i]->IsLoaded() ? "loaded" : "not loaded", path.ToChar());
        libraries[i]->Release();
    }

    char classes[1024];
    if (!GetEnvironmentVariableA("OPENSC4_TEST_GZCOM_CLASSES", classes, sizeof(classes)))
        return;
    for (char* text = classes; *text;) {
        char* end = nullptr;
        const uint32_t classId = std::strtoul(text, &end, 16);
        if (end == text)
            break;
        cIGZUnknown* object = nullptr;
        const bool ok = com->GetClassObject(classId, 1, reinterpret_cast<void**>(&object));
        log::Write("gzcom: class %08X: %s", classId, ok && object ? "made" : "none");
        if (ok && object)
            object->Release();
        text = *end == ',' ? end + 1 : end;
    }
}

bool Apply(registry::Mode)
{
    __try {
        Survey();
    } __except (crash::HookFilter("diagnostics.gzcom_libraries", GetExceptionInformation())) {
    }
    return true;
}

const bool g_registered = registry::Register({
    "diagnostics.gzcom_libraries",
    "diagnostics",
    registry::Kind::Api,
    registry::Phase::LateInit,
    0,
    "Logs the plugin DLLs in the COM object's library list and, on request, which classes it can make",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::gzcom_libraries_diag
