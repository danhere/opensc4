#include "launcher/inject.h"

#include <string>

namespace opensc4::launcher {
namespace {

std::wstring LastErrorMessage(const wchar_t* what)
{
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring message = std::wstring(what) + L" failed (error " + std::to_wstring(code) + L")";
    if (text) {
        message += L": ";
        message += text;
        LocalFree(text);
        while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r'))
            message.pop_back();
    }
    return message;
}

std::wstring DirectoryOf(const std::wstring& path)
{
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

// Loads `dll` in `process` and returns false (with `error` set) on failure.
bool LoadDllRemote(HANDLE process, const std::wstring& dll, std::wstring& error)
{
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        error = LastErrorMessage(L"VirtualAllocEx");
        return false;
    }

    bool ok = false;
    if (!WriteProcessMemory(process, remotePath, dll.c_str(), bytes, nullptr)) {
        error = LastErrorMessage(L"WriteProcessMemory");
    } else {
        // kernel32 is mapped at the same address in every process of the same
        // bitness, and the launcher is 32-bit like the game.
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
        if (!thread) {
            error = LastErrorMessage(L"CreateRemoteThread");
        } else {
            WaitForSingleObject(thread, INFINITE);
            DWORD module = 0; // LoadLibraryW's HMODULE, truncated to 32 bits (fine for a 32-bit target)
            GetExitCodeThread(thread, &module);
            CloseHandle(thread);
            if (module == 0)
                error = L"LoadLibraryW returned NULL in the game process for " + dll;
            else
                ok = true;
        }
    }

    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    return ok;
}

} // namespace

LaunchResult LaunchWithDll(const std::wstring& exe, const std::wstring& args, const std::wstring& dll)
{
    LaunchResult result;

    // CreateProcessW may modify the command line buffer.
    std::wstring commandLine = L"\"" + exe + L"\"";
    if (!args.empty())
        commandLine += L" " + args;

    // SC4 resolves its data directories relative to the exe's folder.
    const std::wstring workingDir = DirectoryOf(exe);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, workingDir.c_str(), &startup, &result.process)) {
        result.error = LastErrorMessage(L"CreateProcessW");
        return result;
    }

    if (!LoadDllRemote(result.process.hProcess, dll, result.error)) {
        TerminateProcess(result.process.hProcess, 1);
        CloseHandle(result.process.hThread);
        CloseHandle(result.process.hProcess);
        result.process = {};
        return result;
    }

    ResumeThread(result.process.hThread);
    result.ok = true;
    return result;
}

} // namespace opensc4::launcher
