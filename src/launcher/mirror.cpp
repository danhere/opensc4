#include "launcher/mirror.h"

#include "launcher/pe_patch.h"

#include <windows.h>

#include <winioctl.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace opensc4::launcher {
namespace {

std::wstring Parent(const std::wstring& path)
{
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring Leaf(const std::wstring& path)
{
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring Failed(const wchar_t* what, const std::wstring& path)
{
    return std::wstring(what) + L" failed for " + path + L" (error " + std::to_wstring(GetLastError()) + L")";
}

bool IsDots(const wchar_t* name)
{
    return !std::wcscmp(name, L".") || !std::wcscmp(name, L"..");
}

// The mount-point form of REPARSE_DATA_BUFFER (ntifs.h, not in the SDK's
// user-mode headers).
struct MountPointBuffer {
    DWORD tag;
    WORD dataLength;
    WORD reserved;
    WORD substituteOffset;
    WORD substituteLength;
    WORD printOffset;
    WORD printLength;
    wchar_t paths[1];
};

// Makes `link` (a new empty folder) a directory junction to `target`. Needs no
// privilege, unlike a directory symbolic link.
bool MakeJunction(const std::wstring& link, const std::wstring& target)
{
    if (!CreateDirectoryW(link.c_str(), nullptr))
        return false;
    const std::wstring substitute = L"\\??\\" + target;
    const size_t pathBytes = (substitute.size() + 1 + target.size() + 1) * sizeof(wchar_t);
    std::vector<std::uint8_t> storage(offsetof(MountPointBuffer, paths) + pathBytes);
    auto* buffer = reinterpret_cast<MountPointBuffer*>(storage.data());
    buffer->tag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer->dataLength = static_cast<WORD>(storage.size() - 8);
    buffer->substituteOffset = 0;
    buffer->substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    buffer->printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    buffer->printLength = static_cast<WORD>(target.size() * sizeof(wchar_t));
    std::memcpy(buffer->paths, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(buffer->paths + substitute.size() + 1, target.c_str(), (target.size() + 1) * sizeof(wchar_t));

    HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(link.c_str());
        return false;
    }
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, storage.data(),
                                    static_cast<DWORD>(storage.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    if (!ok)
        RemoveDirectoryW(link.c_str());
    return ok != FALSE;
}

bool LinkOrCopy(const std::wstring& to, const std::wstring& from)
{
    return CreateHardLinkW(to.c_str(), from.c_str(), nullptr) || CopyFileW(from.c_str(), to.c_str(), FALSE);
}

DWORD LinkCount(const std::wstring& path)
{
    HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    BY_HANDLE_FILE_INFORMATION info{};
    const DWORD links = GetFileInformationByHandle(handle, &info) ? info.nNumberOfLinks : 0;
    CloseHandle(handle);
    return links;
}

// Empties `folder` one level deep, never following a junction: junctions and
// folders are removed with RemoveDirectoryW, which removes a junction itself
// and refuses a folder that is not empty; files (links or copies of the
// install's files) with DeleteFileW, which leaves the install's file alone.
// The Apps folder, which is ours, is emptied the same way first.
void ClearMirror(const std::wstring& folder)
{
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (IsDots(entry.cFileName))
            continue;
        const std::wstring path = folder + L"\\" + entry.cFileName;
        const bool junction = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!junction && !_wcsicmp(entry.cFileName, L"Apps"))
                ClearMirror(path);
            RemoveDirectoryW(path.c_str());
        } else {
            // A read-only copy needs its attribute cleared first; a hard link
            // shares its attributes with the install's file, so it is left as
            // it is (the install's files are not read-only).
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_READONLY) && LinkCount(path) == 1)
                SetFileAttributesW(path.c_str(), entry.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            DeleteFileW(path.c_str());
        }
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

bool IsEmpty(const std::wstring& folder)
{
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE)
        return true;
    bool empty = true;
    do {
        if (!IsDots(entry.cFileName))
            empty = false;
    } while (empty && FindNextFileW(find, &entry));
    FindClose(find);
    return empty;
}

std::wstring Stamp(const std::wstring& exe)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &data))
        return {};
    return exe + L"|" + std::to_wstring(data.nFileSizeLow) + L"|" + std::to_wstring(data.ftLastWriteTime.dwHighDateTime) +
           L"|" + std::to_wstring(data.ftLastWriteTime.dwLowDateTime);
}

std::wstring ReadText(const std::wstring& path)
{
    std::wifstream in(path);
    std::wstring text;
    std::getline(in, text);
    return text;
}

bool WritePatchedExe(const std::wstring& from, const std::wstring& to, std::wstring& error)
{
    std::ifstream in(from, std::ios::binary);
    std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (image.empty()) {
        error = L"could not read " + from;
        return false;
    }
    bool alreadySet = false;
    std::string why;
    if (!SetLargeAddressAware(image, &alreadySet, &why)) {
        error = from + L": " + std::wstring(why.begin(), why.end());
        return false;
    }
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!out) {
        error = L"could not write " + to;
        return false;
    }
    return true;
}

} // namespace

std::wstring PrepareLargeAddressMirror(const std::wstring& gameExe, const std::wstring& mirrorRoot, bool* rebuilt,
                                       std::wstring& error)
{
    *rebuilt = false;
    const std::wstring apps = Parent(gameExe);
    const std::wstring install = Parent(apps);
    if (install.empty() || _wcsicmp(Leaf(apps).c_str(), L"Apps") != 0) {
        error = L"the game exe is not in an Apps folder: " + gameExe;
        return {};
    }
    const std::wstring mirroredExe = mirrorRoot + L"\\Apps\\" + Leaf(gameExe);
    const std::wstring stampFile = mirrorRoot + L"\\opensc4-mirror.txt";
    const std::wstring stamp = Stamp(gameExe);
    if (!stamp.empty() && ReadText(stampFile) == stamp && GetFileAttributesW(mirroredExe.c_str()) != INVALID_FILE_ATTRIBUTES)
        return mirroredExe;
    *rebuilt = true;

    // Parent folders of the mirror (%LOCALAPPDATA%\opensc4\...).
    for (size_t at = mirrorRoot.find(L'\\', 3); at != std::wstring::npos; at = mirrorRoot.find(L'\\', at + 1))
        CreateDirectoryW(mirrorRoot.substr(0, at).c_str(), nullptr);
    CreateDirectoryW(mirrorRoot.c_str(), nullptr);
    // Only a folder that is empty or holds our stamp is ever cleared, so a
    // wrong path can never empty someone's folder (or the install).
    if (GetFileAttributesW(stampFile.c_str()) == INVALID_FILE_ATTRIBUTES && !IsEmpty(mirrorRoot)) {
        error = mirrorRoot + L" is not empty and is not an OpenSC4 mirror; not touching it";
        return {};
    }
    ClearMirror(mirrorRoot);
    // Marks the folder as ours while it is built; the real stamp replaces it
    // at the end, so an interrupted build is rebuilt next time.
    std::wofstream(stampFile) << L"building\n";

    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW((install + L"\\*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) {
        error = Failed(L"listing", install);
        return {};
    }
    bool ok = true;
    do {
        if (IsDots(entry.cFileName) || !_wcsicmp(entry.cFileName, L"Apps"))
            continue;
        const std::wstring from = install + L"\\" + entry.cFileName;
        const std::wstring to = mirrorRoot + L"\\" + entry.cFileName;
        const bool made = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? MakeJunction(to, from) : LinkOrCopy(to, from);
        if (!made) {
            error = Failed(L"mirroring", from);
            ok = false;
        }
    } while (ok && FindNextFileW(find, &entry));
    FindClose(find);

    const std::wstring mirrorApps = mirrorRoot + L"\\Apps";
    if (ok && !CreateDirectoryW(mirrorApps.c_str(), nullptr)) {
        error = Failed(L"creating", mirrorApps);
        ok = false;
    }
    if (ok) {
        find = FindFirstFileW((apps + L"\\*").c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE) {
            error = Failed(L"listing", apps);
            ok = false;
        } else {
            do {
                // Subfolders are left out (Apps has none): a junction would let
                // the game write into the install.
                if (IsDots(entry.cFileName) || !_wcsicmp(entry.cFileName, Leaf(gameExe).c_str()) ||
                    (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                const std::wstring from = apps + L"\\" + entry.cFileName;
                const std::wstring to = mirrorApps + L"\\" + entry.cFileName;
                // The game writes files into its Apps folder (a config log),
                // so Apps files are copies, never links into the install.
                if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
                    error = Failed(L"mirroring", from);
                    ok = false;
                }
            } while (ok && FindNextFileW(find, &entry));
            FindClose(find);
        }
    }
    if (ok)
        ok = WritePatchedExe(gameExe, mirroredExe, error);
    if (!ok) {
        ClearMirror(mirrorRoot);
        return {};
    }
    std::wofstream(stampFile) << stamp << L"\n";
    return mirroredExe;
}

} // namespace opensc4::launcher
