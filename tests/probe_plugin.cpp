// A minimal GZCOM plugin DLL for testing how the game loads plugin DLLs
// (docs/plugin-dlls.md). Built several times with different definitions:
//   PROBE_NAME         text that starts every log line
//   PROBE_DIRECTOR_ID  the director's ID
//   PROBE_CLASS_A/_B   class IDs it registers, with PROBE_VERSION_A/_B
//   PROBE_MODE         0 normal, 1 InitializeCOM answers false,
//                      2 the export answers NULL, 3 no export at all
// Every call the game makes is appended to the file named by the environment
// variable OPENSC4_PROBE_LOG, one line each, so two runs can be compared.
#include <windows.h>

#include <cIGZCOMDirector.h>
#include <cIGZString.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#ifndef PROBE_MODE
#define PROBE_MODE 0
#endif

namespace {

void Log(const char* format, ...)
{
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("OPENSC4_PROBE_LOG", path, sizeof(path)))
        return;
    char line[512];
    int used = std::snprintf(line, sizeof(line), "%s ", PROBE_NAME);
    va_list args;
    va_start(args, format);
    used += std::vsnprintf(line + used, sizeof(line) - static_cast<std::size_t>(used) - 2, format, args);
    va_end(args);
    line[used++] = '\n';
    const HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(used), &written, nullptr);
    CloseHandle(file);
}

#if PROBE_MODE != 3

// What the director makes: an object that only counts references.
class Thing final : public cIGZUnknown {
public:
    bool QueryInterface(uint32_t iid, void** out) override
    {
        if (iid != 1)
            return false;
        *out = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++count_; }
    uint32_t Release() override
    {
        const uint32_t left = --count_;
        if (left == 0)
            delete this;
        return left;
    }

private:
    uint32_t count_ = 0;
};

class Director final : public cIGZCOMDirector {
public:
    bool QueryInterface(uint32_t iid, void** out) override
    {
        if (iid != 1)
            return false;
        *out = this;
        return true;
    }
    uint32_t AddRef() override { return ++count_; }
    uint32_t Release() override { return --count_; }

    bool InitializeCOM(cIGZCOM* com, const cIGZString& path) override
    {
        com_ = com;
        const char* text = path.ToChar();
        const char* name = text;
        for (const char* p = text; *p; ++p) {
            if (*p == '\\' || *p == '/')
                name = p + 1;
        }
        Log("InitializeCOM %s -> %d", name, PROBE_MODE == 1 ? 0 : 1);
        return PROBE_MODE != 1;
    }
    bool OnStart(cIGZCOM*) override
    {
        Log("OnStart");
        return true;
    }
    void EnumClassObjects(ClassObjectEnumerationCallback callback, void* context) override
    {
        Log("EnumClassObjects");
#ifdef PROBE_CLASS_A
        callback(PROBE_CLASS_A, PROBE_VERSION_A, context);
#endif
#ifdef PROBE_CLASS_B
        callback(PROBE_CLASS_B, PROBE_VERSION_B, context);
#endif
        (void)callback;
        (void)context;
    }
    bool GetClassObject(uint32_t classId, uint32_t iid, void** out) override
    {
        Log("GetClassObject %08X", static_cast<unsigned>(classId));
        Thing* thing = new Thing;
        thing->AddRef();
        const bool ok = thing->QueryInterface(iid, out);
        thing->Release();
        return ok;
    }
    bool CanUnloadNow() override
    {
        Log("CanUnloadNow");
        return false;
    }
    bool OnUnload() override
    {
        Log("OnUnload");
        return true;
    }
    uint32_t RefCount() override { return count_; }
    uint32_t RemoveRef() override { return count_ ? --count_ : 0; }
    cIGZFrameWork* FrameWork() override { return nullptr; }
    cIGZCOM* GZCOM() override { return com_; }
    uint32_t GetDirectorID() const override { return PROBE_DIRECTOR_ID; }
    bool GetLibraryPath(cIGZString&) override { return false; }
    uint32_t GetHeapAllocatedSize() override { return 0; }

private:
    uint32_t count_ = 0;
    cIGZCOM* com_ = nullptr;
};

Director g_director;

#endif

} // namespace

#if PROBE_MODE != 3
extern "C" __declspec(dllexport) cIGZCOMDirector* GZDllGetGZCOMDirector()
{
    Log("GZDllGetGZCOMDirector -> %s", PROBE_MODE == 2 ? "NULL" : "director");
    return PROBE_MODE == 2 ? nullptr : &g_director;
}
#endif

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        Log("loaded");
    else if (reason == DLL_PROCESS_DETACH)
        Log("unloaded");
    return TRUE;
}
