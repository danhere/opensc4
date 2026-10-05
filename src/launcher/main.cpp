// opensc4.exe: will start SimCity 4.exe suspended, inject core.dll and resume.
// Skeleton only. For now it just checks that core.dll loads in a 32-bit process.
#include "common/abi.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("opensc4 %s\n", OPENSC4_VERSION);
        return 0;
    }

    if (argc > 1 && std::strcmp(argv[1], "--check-core") == 0) {
        HMODULE core = LoadLibraryW(L"core.dll");
        if (!core) {
            std::fprintf(stderr, "core.dll failed to load (error %lu)\n", GetLastError());
            return 1;
        }
        using VersionFn = const char*(__cdecl*)();
        auto version = reinterpret_cast<VersionFn>(GetProcAddress(core, "OpenSC4_Version"));
        if (!version || std::strcmp(version(), OPENSC4_VERSION) != 0) {
            std::fprintf(stderr, "core.dll version mismatch\n");
            return 1;
        }
        std::printf("core.dll %s OK\n", version());
        return 0;
    }

    std::fprintf(stderr, "opensc4 %s: launching is not implemented yet.\n", OPENSC4_VERSION);
    return 2;
}
