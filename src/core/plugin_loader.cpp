// resources.plugin_loader: OpenSC4 builds the resource manager's DB segments
// at startup in place of cSC4App::SetupResources (0x004572B0), so the plugins
// the mod manager turns off stay out of the game (docs/resources.md, "Our
// loader in the game"). It registers the same segments in the same order as
// the game: the data folders' .dat files one by one, then for the SKU folder,
// the install Plugins folder and the user Plugins folder a folder segment of
// their .dat files, each folder's other package files registered one by one
// just before it. Folders are listed by src/dbpf (ListFolder); the files the
// list in [plugins] disabled names (relative to the user Plugins folder) are
// left out. Records are still read by the game's packed-file segments
// (0x086D4A89); the folder segment is ours, in place of the game's
// cGZPersistDBSegmentMultiPackedFiles (0x090F8619), which it follows call
// for call.
#include "core/address_check.h"
#include "core/crash.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"
#include "dbpf/plugins.h"

#include <GZServPtrs.h>
#include <cGZPersistResourceKey.h>
#include <cIGZCOM.h>
#include <cIGZDBSegmentPackedFile.h>
#include <cIGZFrameWork.h>
#include <cIGZPersistDBRecord.h>
#include <cIGZPersistDBSegment.h>
#include <cIGZPersistDBSegmentMultiPackedFiles.h>
#include <cIGZPersistResourceKeyList.h>
#include <cIGZPersistResourceManager.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace opensc4::plugin_loader {
namespace {

constexpr uint32_t kSegmentIid = 0x656B8F0C;      // cIGZPersistDBSegment
constexpr uint32_t kUnknownIid = 1;               // cIGZUnknown
constexpr uint32_t kPackedFileClass = 0x086D4A89; // cGZDBSegmentPackedFile

struct KeyHash {
    size_t operator()(const cGZPersistResourceKey& k) const
    {
        return (static_cast<size_t>(k.type) * 0x9E3779B1u) ^ (static_cast<size_t>(k.group) * 0x85EBCA77u) ^ k.instance;
    }
};
struct KeyEq {
    bool operator()(const cGZPersistResourceKey& a, const cGZPersistResourceKey& b) const
    {
        return a.type == b.type && a.group == b.group && a.instance == b.instance;
    }
};

// The game's paths are in the ANSI code page; src/dbpf takes UTF-8.
std::string Convert(const std::string& text, UINT from, UINT to)
{
    if (text.empty())
        return text;
    const int wideSize = MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(wideSize), L'\0');
    MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), wide.data(), wideSize);
    const int size = WideCharToMultiByte(to, 0, wide.data(), wideSize, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(to, 0, wide.data(), wideSize, out.data(), size, nullptr, nullptr);
    return out;
}

std::string ToUtf8(const std::string& ansi)
{
    return Convert(ansi, CP_ACP, CP_UTF8);
}

// A file under `folder` (ANSI, as the game gives it) from ListFolder's
// relative UTF-8 path, written the way the game writes it.
std::string Join(const std::string& folder, const std::string& relative)
{
    std::string path = folder;
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
        path += '\\';
    std::string tail = Convert(relative, CP_UTF8, CP_ACP);
    for (char& c : tail)
        if (c == '/')
            c = '\\';
    return path + tail;
}

cIGZCOM* Com()
{
    cIGZFrameWork* framework = RZGetFrameWork();
    return framework ? framework->GetCOMObject() : nullptr;
}

// A new game packed-file segment, or null.
cIGZPersistDBSegment* NewPackedFile()
{
    cIGZPersistDBSegment* segment = nullptr;
    cIGZCOM* com = Com();
    if (!com || !com->GetClassObject(kPackedFileClass, kSegmentIid, reinterpret_cast<void**>(&segment)))
        return nullptr;
    return segment;
}

// What the game's plugin scan asks of a file that is not a .dat (0x009728D5):
// its first 4 bytes are the package signature, or somewhere in it is the
// 16-byte marker the packed-file segment looks for when they are not
// (0x009725FB: 80 9D 88 EC ..., the game keeps it with the first byte
// inverted at 0x00AD7BE0).
bool LooksLikePackage(const std::string& path)
{
    static const uint8_t kMarker[16] = {0x80, 0x9D, 0x88, 0xEC, 0x8F, 0x24, 0x03, 0x6C,
                                        0xC9, 0xA6, 0x31, 0x56, 0x5B, 0xCF, 0x77, 0x22};
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    uint32_t signature = 0;
    DWORD read = 0;
    if (ReadFile(file, &signature, 4, &read, nullptr) && read == 4) {
        found = signature == Global<uint32_t>(addr::cGZDBSegmentPackedFile_kSignature);
        LARGE_INTEGER size{};
        if (!found && GetFileSizeEx(file, &size) && size.QuadPart > 16) {
            SetFilePointer(file, 0, nullptr, FILE_BEGIN);
            std::vector<uint8_t> buffer(15 + 65536);
            size_t kept = 0; // bytes carried over from the last block, so a marker across blocks is found
            while (!found && ReadFile(file, buffer.data() + kept, 65536, &read, nullptr) && read > 0) {
                const size_t end = kept + read;
                for (const uint8_t* p = buffer.data(); !found && p + 16 <= buffer.data() + end; ++p) {
                    p = static_cast<const uint8_t*>(std::memchr(p, kMarker[0], buffer.data() + end - p));
                    if (!p || p + 16 > buffer.data() + end)
                        break;
                    found = std::memcmp(p, kMarker, 16) == 0;
                }
                kept = end < 15 ? end : 15;
                std::memmove(buffer.data(), buffer.data() + end - kept, kept);
            }
        }
    }
    CloseHandle(file);
    return found;
}

// A folder's .dat files as one segment, in place of the game's
// cGZPersistDBSegmentMultiPackedFiles (vtables 0x00AD98C0, 0x00AD9894; Open
// 0x0097E92D): one game packed-file segment per file, in folder order, and a
// key map in which the last file that has a key serves it.
class FolderSegment final : public cIGZPersistDBSegment, public cIGZPersistDBSegmentMultiPackedFiles {
public:
    explicit FolderSegment(std::vector<std::string> disabled) : disabled_(std::move(disabled))
    {
        InitializeCriticalSection(&lock_);
    }
    ~FolderSegment() { DeleteCriticalSection(&lock_); }

    size_t FilesOff() const { return off_; }

    bool QueryInterface(uint32_t iid, void** object) override
    {
        if (iid == GZIID_cIGZPersistDBSegmentMultiPackedFiles)
            *object = static_cast<cIGZPersistDBSegmentMultiPackedFiles*>(this);
        else if (iid == kSegmentIid || iid == kUnknownIid)
            *object = static_cast<cIGZPersistDBSegment*>(this);
        else
            return false;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return static_cast<uint32_t>(InterlockedIncrement(&references_)); }
    uint32_t Release() override
    {
        const LONG left = InterlockedDecrement(&references_);
        if (left == 0)
            delete this;
        return static_cast<uint32_t>(left);
    }

    // cIGZPersistDBSegment
    bool Init() override
    {
        initialized_ = true;
        return true;
    }
    bool Shutdown() override
    {
        if (open_)
            Close();
        initialized_ = false;
        return true;
    }

    bool Open(bool read, bool write) override
    {
        if (!read || write || path_.empty())
            return false;
        cIGZCOM* com = Com();
        cIGZPersistResourceKeyList* keys = nullptr;
        if (!com || !com->GetClassObject(GZCLSID_cIGZPersistResourceKeyList, GZIID_cIGZPersistResourceKeyList,
                                         reinterpret_cast<void**>(&keys)))
            return false;
        std::vector<std::string> files;
        std::string error;
        if (!dbpf::ListFolder(ToUtf8(path_), files, &error))
            log::Write("plugins: %s", error.c_str());
        bool opened = false;
        for (const std::string& relative : files) {
            if (!dbpf::IsDatFile(relative))
                continue;
            if (dbpf::PathListed(relative, disabled_)) {
                ++off_;
                continue;
            }
            cIGZPersistDBSegment* file = NewPackedFile();
            if (!file)
                break;
            // The game's order: path, ID, Init, Open.
            const cRZBaseString path(Join(path_, relative).c_str());
            file->SetPath(path);
            const uint32_t id = id_;
            file->SetSegmentID(id);
            if (file->Init() && file->Open(true, false)) {
                file->AddRef();
                children_.push_back(file);
                keys->EraseAll();
                file->GetResourceKeyList(keys, nullptr);
                const uint32_t count = keys->Size();
                for (uint32_t i = 0; i < count; ++i)
                    AddedResource(keys->GetKey(i), file);
                opened = true;
            }
            file->Release();
        }
        keys->Release();
        open_ = opened;
        return opened;
    }
    bool IsOpen() const override { return open_; }
    bool Close() override
    {
        Locked lock(this);
        for (cIGZPersistDBSegment* file : children_) {
            file->Close();
            file->Shutdown();
            file->Release();
        }
        children_.clear();
        map_.clear();
        open_ = false;
        return true;
    }
    bool Flush() override { return true; }

    void GetPath(cIGZString& path) const override { path.FromChar(path_.c_str()); }
    bool SetPath(cIGZString const& path) override
    {
        if (open_)
            return false;
        path_ = path.ToChar() ? path.ToChar() : "";
        return true;
    }

    bool Lock() override
    {
        EnterCriticalSection(&lock_);
        return true;
    }
    bool Unlock() override
    {
        LeaveCriticalSection(&lock_);
        return true;
    }

    uint32_t GetSegmentID() const override { return id_; }
    bool SetSegmentID(uint32_t const& id) override
    {
        id_ = id;
        return true;
    }

    // The game's answers the number of keys, whatever the filter.
    uint32_t GetRecordCount(cIGZPersistResourceKeyFilter*) override { return static_cast<uint32_t>(map_.size()); }

    uint32_t GetResourceKeyList(cIGZPersistResourceKeyList* list, cIGZPersistResourceKeyFilter* filter) override
    {
        Locked lock(this);
        uint32_t total = 0;
        for (cIGZPersistDBSegment* file : children_)
            total += file->GetResourceKeyList(list, filter);
        return total;
    }
    bool GetResourceKeyList(cIGZPersistResourceKeyList& list) override { return GetResourceKeyList(&list, nullptr) != 0; }

    bool TestForRecord(cGZPersistResourceKey const& key) override
    {
        Locked lock(this);
        cIGZPersistDBSegment* file = Find(key);
        return file && file->TestForRecord(key);
    }
    uint32_t GetRecordSize(cGZPersistResourceKey const& key) override
    {
        Locked lock(this);
        cIGZPersistDBSegment* file = Find(key);
        return file ? file->GetRecordSize(key) : 0;
    }
    bool OpenRecord(cGZPersistResourceKey const& key, cIGZPersistDBRecord** record,
                    cIGZFile::AccessMode mode) override
    {
        Locked lock(this);
        cIGZPersistDBSegment* file = Find(key);
        return file && file->OpenRecord(key, record, mode);
    }
    bool CreateNewRecord(cGZPersistResourceKey const&, cIGZPersistDBRecord**) override { return false; }

    bool CloseRecord(cIGZPersistDBRecord* record) override
    {
        Locked lock(this);
        cIGZPersistDBSegment* file = FileOf(record);
        return file && file->CloseRecord(record);
    }
    bool CloseRecord(cIGZPersistDBRecord** record) override
    {
        cIGZPersistDBRecord* r = *record;
        const bool closed = CloseRecord(r);
        if (closed)
            r->Release();
        return closed;
    }
    bool AbortRecord(cIGZPersistDBRecord* record) override
    {
        Locked lock(this);
        cIGZPersistDBSegment* file = FileOf(record);
        return file && file->AbortRecord(record);
    }
    bool AbortRecord(cIGZPersistDBRecord** record) override
    {
        cIGZPersistDBRecord* r = *record;
        const bool aborted = AbortRecord(r);
        if (aborted)
            r->Release();
        return aborted;
    }

    bool DeleteRecord(cGZPersistResourceKey const&) override { return false; }
    uint32_t ReadRecord(cGZPersistResourceKey const& key, void* buffer, uint32_t& size) override
    {
        Locked lock(this);
        cIGZPersistDBSegment* file = Find(key);
        return file ? file->ReadRecord(key, buffer, size) : 0;
    }
    bool WriteRecord(cGZPersistResourceKey const&, void*, uint32_t) override { return false; }

    bool Init(uint32_t id, cIGZString const& path, bool) override
    {
        SetSegmentID(id);
        SetPath(path);
        Init();
        return true;
    }

    // cIGZPersistDBSegmentMultiPackedFiles. Only our SetupResources makes
    // these segments, and always for .dat files, so the filter stays.
    void SetPathFilter(cIGZString const&) override {}
    // Copies every file's records into `target`, as the game's (0x0097E351).
    int32_t ConsolidateDatabaseRecords(cIGZPersistDBSegment* target, cIGZPersistResourceKeyFilter* filter) override
    {
        int32_t copied = 0;
        for (cIGZPersistDBSegment* file : children_) {
            cIGZDBSegmentPackedFile* packed = nullptr;
            if (file->QueryInterface(GZIID_cIGZDBSegmentPackedFile, reinterpret_cast<void**>(&packed))) {
                copied += static_cast<int32_t>(packed->CopyDatabaseRecords(target, filter, false, true));
                packed->Release();
            }
        }
        return copied;
    }
    // Into a new package file: not needed by anything OpenSC4 runs.
    int32_t ConsolidateDatabaseRecords(cIGZString const&, cIGZPersistResourceKeyFilter*) override { return -1; }

    bool FindDBSegment(cGZPersistResourceKey const& key, cIGZPersistDBSegment** segment) override
    {
        Locked lock(this);
        cIGZPersistDBSegment* file = Find(key);
        if (!file)
            return false;
        *segment = file;
        file->AddRef();
        return true;
    }
    uint32_t GetSegmentCount() override { return static_cast<uint32_t>(children_.size()); }
    cIGZPersistDBSegment* GetSegmentByIndex(uint32_t index) override
    {
        return index < children_.size() ? children_[index] : nullptr;
    }
    void AddedResource(cGZPersistResourceKey const& key, cIGZPersistDBSegment* file) override
    {
        if (file)
            map_[key] = file;
    }
    void RemovedResource(cGZPersistResourceKey const& key, cIGZPersistDBSegment*) override { map_.erase(key); }

private:
    struct Locked {
        explicit Locked(FolderSegment* s) : segment(s) { EnterCriticalSection(&segment->lock_); }
        ~Locked() { LeaveCriticalSection(&segment->lock_); }
        FolderSegment* segment;
    };

    cIGZPersistDBSegment* Find(const cGZPersistResourceKey& key) const
    {
        if (!open_)
            return nullptr;
        const auto it = map_.find(key);
        return it == map_.end() ? nullptr : it->second;
    }
    cIGZPersistDBSegment* FileOf(cIGZPersistDBRecord* record) const
    {
        if (!record)
            return nullptr;
        cGZPersistResourceKey key;
        record->GetKey(key);
        return Find(key);
    }

    LONG references_ = 1;
    CRITICAL_SECTION lock_;
    std::vector<std::string> disabled_;
    std::string path_;
    uint32_t id_ = 0;
    bool initialized_ = false;
    bool open_ = false;
    size_t off_ = 0;
    std::vector<cIGZPersistDBSegment*> children_;
    std::unordered_map<cGZPersistResourceKey, cIGZPersistDBSegment*, KeyHash, KeyEq> map_;
};

bool Register(cIGZPersistDBSegment* segment)
{
    cIGZPersistResourceManagerPtr manager;
    return manager && Virtual<bool>(static_cast<cIGZPersistResourceManager*>(manager), 0x68, segment);
}

// The app's directories (cISC4App at app + 0xC).
std::string Directory(void* app, size_t slot)
{
    cRZBaseString path;
    Virtual<bool>(static_cast<char*>(app) + 0xC, slot, static_cast<cIGZString*>(&path));
    return path.ToChar() ? path.ToChar() : "";
}

std::string DataDirectory(void* app, int32_t index)
{
    cRZBaseString path;
    Virtual<bool>(static_cast<char*>(app) + 0xC, 0xD4, static_cast<cIGZString*>(&path), index);
    return path.ToChar() ? path.ToChar() : "";
}

// One package file as a segment of its own, as the game does it: Init, path,
// Open, register; Shutdown if a step after Init fails.
bool RegisterFile(cIGZPersistDBSegment* file, const std::string& path)
{
    if (!file->Init())
        return false;
    const cRZBaseString gzPath(path.c_str());
    if (file->SetPath(gzPath) && file->Open(true, false) && Register(file))
        return true;
    file->Shutdown();
    return false;
}

struct Counts {
    size_t files = 0;
    size_t loose = 0;
    size_t off = 0;
};

// The game data folders' *.dat files (0x0045749B-0x004578CF).
void RegisterDataFiles(const std::string& folder, Counts& counts)
{
    // A folder that is not there has nothing to load (the game's language
    // folder often is not).
    std::vector<std::string> files;
    if (GetFileAttributesA(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    std::string error;
    if (!dbpf::ListFolder(ToUtf8(folder), files, &error, false))
        log::Write("plugins: %s", error.c_str());
    for (const std::string& relative : files) {
        if (!dbpf::IsDatFile(relative))
            continue;
        cIGZPersistDBSegment* file = NewPackedFile();
        if (!file)
            continue;
        counts.files += RegisterFile(file, Join(folder, relative));
        file->Release();
    }
}

// A plugin folder's package files that are not .dat, each a segment of its
// own (0x00457A07-0x00457B50, 0x00457C86-0x00457DCF).
void RegisterLooseFiles(const std::string& folder, const std::vector<std::string>& disabled, Counts& counts)
{
    if (GetFileAttributesA(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
        CreateDirectoryA(folder.c_str(), nullptr);
    std::vector<std::string> files;
    std::string error;
    if (!dbpf::ListFolder(ToUtf8(folder), files, &error))
        log::Write("plugins: %s", error.c_str());
    for (const std::string& relative : files) {
        if (dbpf::IsDatFile(relative))
            continue;
        if (dbpf::PathListed(relative, disabled)) {
            ++counts.off;
            continue;
        }
        // The game makes the segment before it looks at the file.
        cIGZPersistDBSegment* file = NewPackedFile();
        if (!file)
            continue;
        const std::string path = Join(folder, relative);
        if (LooksLikePackage(path))
            counts.loose += RegisterFile(file, path);
        file->Release();
    }
}

// A folder's .dat files as one folder segment.
void RegisterFolder(const std::string& folder, const std::vector<std::string>& disabled, Counts& counts)
{
    auto* segment = new FolderSegment(disabled);
    cIGZPersistDBSegment* asSegment = segment;
    if (asSegment->Init()) {
        const cRZBaseString path(folder.c_str());
        if (asSegment->SetPath(path) && asSegment->Open(true, false) && Register(asSegment))
            counts.files += segment->GetSegmentCount();
        else
            asSegment->Shutdown();
    }
    counts.off += segment->FilesOff();
    asSegment->Release();
}

std::vector<std::string> DisabledList()
{
    std::string file = settings::Get("plugins", "disabled", "disabled-plugins.txt");
    const bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '\\' || file[0] == '/');
    if (!absolute) {
        const std::wstring ini = settings::DefaultIniPath();
        const std::wstring folder = ini.substr(0, ini.find_last_of(L"\\/") + 1);
        const int size = WideCharToMultiByte(CP_UTF8, 0, folder.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, folder.c_str(), -1, utf8.data(), size, nullptr, nullptr);
        file = utf8 + file;
    }
    std::vector<std::string> list;
    std::string error;
    if (!dbpf::ReadPathList(file, list, &error))
        log::Write("plugins: %s", error.c_str());
    log::Write("plugins: %zu entries turned off in %s", list.size(), file.c_str());
    return list;
}

// cSC4App::SetupResources, step by step (0x004572B0).
bool SetupResourcesBody(void* app)
{
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    // The debug hook, only with the app's debug functionality on.
    void* appInterface = static_cast<char*>(app) + 0xC;
    if (Virtual<bool>(appInterface, 0x54)) {
        Virtual<void>(static_cast<char*>(app) + 0x2C, 0x38);
        void* service = nullptr;
        using GetFrameworkFn = void*(__cdecl*)();
        if (void* framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
            Virtual<bool>(framework, 0x14, 0x25076B9Eu, 0xA237613Cu, &service);
        if (service) {
            Virtual<void>(service, 0x1C, Global<uint32_t>(addr::cSC4App_SetupResources_DebugName),
                          static_cast<uint32_t>(addr::cSC4App_SetupResources_DebugCallback), app);
            Virtual<uint32_t>(service, 0x8);
        }
    }

    const std::vector<std::string> none;
    const std::vector<std::string> disabled = DisabledList();
    Counts game, install, user;
    RegisterDataFiles(DataDirectory(app, -1), game);
    RegisterDataFiles(DataDirectory(app, 0), game);
    RegisterFolder(Directory(app, 0xE4), none, game); // SKU data
    const std::string installPlugins = Directory(app, 0xDC);
    RegisterLooseFiles(installPlugins, none, install);
    RegisterFolder(installPlugins, none, install);
    const std::string userPlugins = Directory(app, 0xEC);
    RegisterLooseFiles(userPlugins, disabled, user);
    RegisterFolder(userPlugins, disabled, user);

    // Resource types the manager is told about at the end (class or group
    // first, then the type, then a null factory), as the game does.
    static const uint32_t kTypes[][2] = {
        {0x0A63DF8C, 0xCA63E2A3}, {0x00436EB4, 0x2026960B}, {0xC8696797, 0x686AA4B0}, {0xA2FFB5D3, 0x856DDBAC},
        {0x3AB50E2A, 0x7AB50E44}, {0x3AB50E2A, 0x7AB50E45}, {0x69B6F01C, 0x29A5D1EC}, {0x69B6F01C, 0x09ADCD75},
        {0xFAD0F0B6, 0x5AD0E817}, {0x453429B3, 0x6534284A}, {0x053429C8, 0x05342861}, {0xA83479EA, 0xA83479D3},
        {0x496678FE, 0x296678F7}, {0xEA5118B5, 0xEA5118B0}, {0x42E411C2, 0xA2E3D533},
    };
    cIGZPersistResourceManagerPtr manager;
    auto* resources = static_cast<cIGZPersistResourceManager*>(manager);
    if (!resources)
        return false;
    for (const auto& t : kTypes)
        Virtual<bool>(resources, 0x4C, t[0], t[1], 0u);
    const uint32_t segments = Virtual<uint32_t>(resources, 0x84);

    QueryPerformanceCounter(&end);
    log::Write("plugins: %u segments in %.1f ms; game data %zu files; install Plugins %zu .dat, %zu other; user "
               "Plugins %zu .dat, %zu other, %zu turned off",
               segments, (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart, game.files, install.files,
               install.loose, user.files, user.loose, user.off);
    return segments >= 3;
}

using SetupFn = bool(__fastcall*)(void* app, void* edx);
SetupFn g_original = nullptr;

bool __fastcall SetupResources(void* app, void*)
{
    __try {
        return SetupResourcesBody(app);
    } __except (crash::HookFilter("resources.plugin_loader", GetExceptionInformation())) {
        return false;
    }
}

bool Apply(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4App_SetupResources);
    return entry && hook::Install(*entry, &SetupResources, &g_original);
}

const bool g_registered = registry::Register({
    "resources.plugin_loader",
    "resources",
    registry::Kind::Detour,
    registry::Phase::DirectorInit,
    addr::cSC4App_SetupResources,
    "OpenSC4's plugin loader builds the resource manager's segments; plugins in [plugins] disabled stay out",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::plugin_loader
