// Coverage meter test: instruments functions in this program, calls some of
// them (one from several threads at once), and checks that behaviour is
// unchanged, exactly the called functions are recorded, an entry whose first
// byte does not match is skipped, and Stop restores every byte.
#include "core/coverage.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

__declspec(noinline) int Alpha(int x) { volatile int v = x; return v * 3 + 1; }
__declspec(noinline) int Beta(int x) { volatile int v = x; return v * 5 + 2; }
__declspec(noinline) int Gamma(int x) { volatile int v = x; return v * 7 + 3; }
__declspec(noinline) int Delta(int x) { volatile int v = x; return v * 11 + 4; }

int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

std::uintptr_t Address(int (*f)(int))
{
    return reinterpret_cast<std::uintptr_t>(f);
}

unsigned char FirstByte(int (*f)(int))
{
    return *reinterpret_cast<const unsigned char*>(f);
}

} // namespace

int main()
{
    namespace coverage = opensc4::coverage;
    int (*const functions[])(int) = {Alpha, Beta, Gamma, Delta};
    unsigned char before[4];
    std::vector<coverage::Function> list;
    for (int i = 0; i < 4; ++i) {
        before[i] = FirstByte(functions[i]);
        list.push_back({Address(functions[i]), before[i]});
    }
    list[3].firstByte ^= 0xFF; // Delta: pretend the list disagrees with memory

    if (coverage::Start(list) != 3)
        return Fail("expected 3 instrumented functions (Delta's first byte does not match)");
    if (FirstByte(Alpha) != 0xCC || FirstByte(Gamma) != 0xCC || FirstByte(Delta) != before[3])
        return Fail("breakpoints not where expected");

    // Alpha from eight threads at once; Beta once; Gamma and Delta never.
    std::vector<std::thread> threads;
    std::vector<int> results(8);
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&, t] { results[t] = Alpha(t); });
    for (auto& t : threads)
        t.join();
    for (int t = 0; t < 8; ++t)
        if (results[t] != t * 3 + 1)
            return Fail("Alpha returned a wrong result under instrumentation");
    if (Beta(4) != 22 || Beta(4) != 22)
        return Fail("Beta returned a wrong result under instrumentation");

    const coverage::Summary summary = coverage::GetSummary();
    if (summary.listed != 4 || summary.planted != 3 || summary.hit != 2)
        return Fail("summary should be 4 listed, 3 planted, 2 hit");
    if (FirstByte(Alpha) != before[0] || FirstByte(Beta) != before[1] || FirstByte(Gamma) != 0xCC)
        return Fail("hit entries should be restored, unhit ones still instrumented");

    wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    lstrcatW(path, L"opensc4-coverage-test.csv");
    if (!coverage::WriteHits(path))
        return Fail("could not write the hits file");
    FILE* file = nullptr;
    char text[512] = {};
    if (_wfopen_s(&file, path, L"rb") == 0 && file) {
        std::fread(text, 1, sizeof(text) - 1, file);
        std::fclose(file);
    }
    DeleteFileW(path);
    char alpha[16], gamma[16];
    std::snprintf(alpha, sizeof(alpha), "0x%08X,", static_cast<unsigned>(Address(Alpha)));
    std::snprintf(gamma, sizeof(gamma), "0x%08X,", static_cast<unsigned>(Address(Gamma)));
    if (!std::strstr(text, alpha) || std::strstr(text, gamma))
        return Fail("the hits file should list Alpha and not Gamma");

    coverage::Stop();
    for (int i = 0; i < 4; ++i)
        if (FirstByte(functions[i]) != before[i])
            return Fail("Stop should restore every byte");
    if (Gamma(1) != 10 || Delta(1) != 15)
        return Fail("functions misbehave after Stop");

    std::puts("coverage test OK");
    return 0;
}
