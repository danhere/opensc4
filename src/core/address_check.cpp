#include "core/address_check.h"

#include "core/hook.h"
#include "core/log.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>

namespace opensc4::addr_check {
namespace {

bool Readable(std::uintptr_t address, std::size_t size)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & readable) || (info.Protect & PAGE_GUARD))
        return false;
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return address + size <= end;
}

std::string ModuleOf(std::uintptr_t address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module))
        return "no module (allocated memory)";
    char path[MAX_PATH] = "?";
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* slash = std::strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

std::string Hex(const unsigned char* bytes, std::size_t size)
{
    std::string out;
    char cell[4];
    for (std::size_t i = 0; i < size; ++i) {
        std::snprintf(cell, sizeof(cell), i ? " %02X" : "%02X", bytes[i]);
        out += cell;
    }
    return out;
}

} // namespace

const addr::Entry* Find(std::uintptr_t address)
{
    for (const addr::Entry& entry : addr::kEntries)
        if (entry.address == address)
            return &entry;
    return nullptr;
}

std::string DescribeMismatch(std::uintptr_t address, const unsigned char* expect, std::size_t size)
{
    if (size == 0)
        return {};
    if (!Readable(address, size))
        return "address is not readable (not SimCity 4 1.1.641?)";
    const auto* actual = reinterpret_cast<const unsigned char*>(address);
    if (std::memcmp(actual, expect, size) == 0)
        return {};

    std::string description = "expected " + Hex(expect, size) + ", found " + Hex(actual, size);
    // A 5-byte JMP or CALL rel32 at the start is another module's detour.
    if (actual[0] == 0xE9 || actual[0] == 0xE8) {
        std::int32_t rel;
        std::memcpy(&rel, actual + 1, sizeof(rel));
        const std::uintptr_t destination = address + 5 + rel;
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "; %s to 0x%08X in ", actual[0] == 0xE9 ? "JMP" : "CALL",
                      static_cast<unsigned>(destination));
        description += buffer + ModuleOf(destination);
    }
    return description;
}

int LogManifest()
{
    int checked = 0, mismatches = 0, ours = 0;
    for (const addr::Entry& entry : addr::kEntries) {
        if (entry.expectSize == 0)
            continue;
        ++checked;
        if (hook::IsInstalledAt(entry.address)) {
            ++ours; // checked before hooking (hook::InstallAt)
            continue;
        }
        const std::string problem = DescribeMismatch(entry.address, entry.expect, entry.expectSize);
        if (!problem.empty()) {
            ++mismatches;
            log::Write("manifest: %s at 0x%08X: %s", entry.name, static_cast<unsigned>(entry.address),
                       problem.c_str());
        }
    }
    log::Write("manifest: %d of %d address database entries intact (%d hooked by OpenSC4)", checked - mismatches,
               checked, ours);
    return mismatches;
}

void LogModules()
{
    wchar_t windows[MAX_PATH];
    const UINT windowsLength = GetSystemWindowsDirectoryW(windows, MAX_PATH);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        log::Write("manifest: could not list modules (error %lu)", GetLastError());
        return;
    }
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    int listed = 0;
    for (BOOL more = Module32FirstW(snapshot, &module); more; more = Module32NextW(snapshot, &module)) {
        if (windowsLength && _wcsnicmp(module.szExePath, windows, windowsLength) == 0)
            continue; // Windows' own DLLs
        log::Write("manifest: module %08X %ls", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(module.modBaseAddr)),
                   module.szExePath);
        ++listed;
    }
    CloseHandle(snapshot);
    log::Write("manifest: %d modules outside the Windows folder", listed);
}

} // namespace opensc4::addr_check
