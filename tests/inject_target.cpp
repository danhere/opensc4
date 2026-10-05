// Stand-in for SimCity 4.exe in the injection test. The launcher loads
// core.dll before this program's entry point, so it must already be present
// when main runs. Exits 0 if it is, 3 if not.
#include <windows.h>

int main()
{
    return GetModuleHandleW(L"core.dll") ? 0 : 3;
}
