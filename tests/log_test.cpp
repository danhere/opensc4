// Logger test: lines of every size come back whole from opensc4.log.
// log::Write builds a line that fits its 4,096-byte buffer on the stack and a
// longer one in a heap block; both paths must write prefix, text and CRLF in
// one piece. The log is shared with other tests and runs, so each line starts
// with a marker that is unique to this run and is found by searching for it.
#include "core/log.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

// Marker for one line; the process ID keeps earlier runs' lines apart.
std::string Marker(const char* name)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%s-%lu-", name, GetCurrentProcessId());
    return text;
}

int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

std::string ReadLog()
{
    wchar_t path[MAX_PATH];
    if (!opensc4::log::Directory(path))
        return {};
    lstrcatW(path, L"opensc4.log");
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") == 0 && f) {
        char buffer[4096];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
            text.append(buffer, n);
        std::fclose(f);
    }
    return text;
}

// Text of length `length` where every position can be checked: printable,
// never '%', '\r' or '\n', and different at neighbouring positions.
std::string Pattern(const std::string& marker, size_t length)
{
    std::string text = marker;
    for (size_t i = marker.size(); i < length; ++i) {
        char c = static_cast<char>(33 + (i * 31 + i / 94) % 94);
        text += c == '%' ? '#' : c;
    }
    text.resize(length);
    return text;
}

// Finds the line holding `marker` and returns it without its CRLF; `prefix`
// gets the characters before the logged text. Fails if the line is not
// followed by CRLF or the marker occurs twice.
bool FindLine(const std::string& log, const std::string& marker, std::string* body, size_t* prefix)
{
    const size_t at = log.find(marker);
    if (at == std::string::npos || log.find(marker, at + 1) != std::string::npos)
        return false;
    const size_t start = log.rfind('\n', at);
    const size_t begin = start == std::string::npos ? 0 : start + 1;
    const size_t end = log.find("\r\n", at);
    if (end == std::string::npos)
        return false;
    if (log.find('\n', at) != end + 1)
        return false; // a stray line feed inside the line
    *prefix = at - begin;
    *body = log.substr(at, end - at);
    return true;
}

// Writes `text` (as "%s"), reads it back and checks it whole.
int Check(const char* what, const std::string& marker, size_t length)
{
    const std::string text = Pattern(marker, length);
    opensc4::log::Write("%s", text.c_str());
    std::string body;
    size_t prefix = 0;
    if (!FindLine(ReadLog(), text.substr(0, marker.size()), &body, &prefix))
        return Fail(what);
    if (body != text)
        return Fail(what);
    return 0;
}

} // namespace

int main()
{
    // (a) a short line: prefix, text, CRLF.
    const std::string shortMarker = Marker("LTSHORT");
    opensc4::log::Write("%s value %d", shortMarker.c_str(), 42);
    std::string body;
    size_t prefix = 0;
    if (!FindLine(ReadLog(), shortMarker, &body, &prefix))
        return Fail("short line not found as one line");
    if (body != shortMarker + " value 42")
        return Fail("short line changed");
    // "YYYY-MM-DD hh:mm:ss.mmm [<thread>] "
    if (prefix < 26 || prefix > 36)
        return Fail("short line prefix has an odd size");

    // (b) around the buffer limit: prefix + body = 4092..4095 characters.
    const int totals[] = {4092, 4093, 4094, 4095, 4096, 4097};
    for (int total : totals) {
        char name[16];
        std::snprintf(name, sizeof(name), "LTEDGE%d", total);
        if (Check("a line at the buffer limit came back changed", Marker(name), static_cast<size_t>(total) - prefix))
            return 1;
    }

    // (c) well past the limit.
    if (Check("a 10,000 character line came back changed", Marker("LTLONG10K"), 10000))
        return 1;
    if (Check("a 100,000 character line came back changed", Marker("LTLONG100K"), 100000))
        return 1;

    // (d) arguments on the long path.
    const std::string argsMarker = Marker("LTARGS");
    const std::string middle = Pattern(argsMarker, 5000);
    opensc4::log::Write("%s|%d|%s", middle.c_str(), 12345, "end");
    if (!FindLine(ReadLog(), argsMarker, &body, &prefix))
        return Fail("formatted long line not found as one line");
    if (body != middle + "|12345|end")
        return Fail("formatted long line came back changed");

    // (e) lines after a long line.
    const std::string after1 = Marker("LTAFTER1");
    const std::string after2 = Marker("LTAFTER2");
    opensc4::log::Write("%s %d", after1.c_str(), 1);
    opensc4::log::Write("%s %s", after2.c_str(), "two");
    if (!FindLine(ReadLog(), after1, &body, &prefix) || body != after1 + " 1")
        return Fail("line after a long line is damaged");
    if (!FindLine(ReadLog(), after2, &body, &prefix) || body != after2 + " two")
        return Fail("second line after a long line is damaged");

    std::printf("log test OK\n");
    return 0;
}
