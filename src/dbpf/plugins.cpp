#include "dbpf/plugins.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace opensc4::dbpf {
namespace {

namespace fs = std::filesystem;

std::vector<std::string> Split(const std::string& path)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t slash = path.find('/', start);
        parts.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos)
            return parts;
        start = slash + 1;
    }
}

// NTFS orders names by their uppercase form. Only ASCII is folded here;
// other UTF-8 bytes compare as they are.
int CompareNames(const std::string& a, const std::string& b)
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'a' && ca <= 'z')
            ca = static_cast<unsigned char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = static_cast<unsigned char>(cb - 'a' + 'A');
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    return a.compare(b) < 0 ? -1 : a.compare(b) > 0 ? 1 : 0;
}

bool CaseFoldEqual(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char ca = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 'a' + 'A') : a[i];
        const char cb = b[i] >= 'a' && b[i] <= 'z' ? static_cast<char>(b[i] - 'a' + 'A') : b[i];
        if (ca != cb)
            return false;
    }
    return true;
}

bool FolderBefore(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const bool aFile = i + 1 == a.size(), bFile = i + 1 == b.size();
        if (aFile != bFile)
            return aFile; // files before subfolders
        if (const int c = CompareNames(a[i], b[i]); c != 0)
            return c < 0;
    }
    return a.size() < b.size();
}

std::string FromPath(const fs::path& path)
{
    const std::u8string s = path.generic_u8string();
    return std::string(s.begin(), s.end());
}

fs::path ToPath(const std::string& utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

} // namespace

void SortFolderOrder(std::vector<std::string>& relativePaths)
{
    std::vector<std::pair<std::vector<std::string>, std::string>> keyed;
    keyed.reserve(relativePaths.size());
    for (std::string& p : relativePaths)
        keyed.emplace_back(Split(p), std::move(p));
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const auto& a, const auto& b) { return FolderBefore(a.first, b.first); });
    for (size_t i = 0; i < keyed.size(); ++i)
        relativePaths[i] = std::move(keyed[i].second);
}

namespace {

template <typename Iterator>
bool ListWith(const std::string& folder, const fs::path& root, std::vector<std::string>& out, std::string* error)
{
    std::error_code ec;
    Iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) {
        if (error)
            *error = "cannot list " + folder + ": " + ec.message();
        return false;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            if (error)
                *error = "cannot list " + folder + ": " + ec.message();
            return false;
        }
        std::error_code typeError;
        if (it->is_regular_file(typeError))
            out.push_back(FromPath(it->path().lexically_relative(root)));
    }
    return true;
}

} // namespace

bool ListFolder(const std::string& folder, std::vector<std::string>& out, std::string* error, bool recursive)
{
    out.clear();
    const fs::path root = ToPath(folder);
    const bool ok = recursive ? ListWith<fs::recursive_directory_iterator>(folder, root, out, error)
                              : ListWith<fs::directory_iterator>(folder, root, out, error);
    if (!ok)
        return false;
    SortFolderOrder(out);
    return true;
}

bool IsDatFile(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    return dot != std::string::npos && (slash == std::string::npos || dot > slash) &&
           CaseFoldEqual(path.substr(dot), ".DAT");
}

bool ReadPathList(const std::string& file, std::vector<std::string>& out, std::string* error)
{
    out.clear();
    std::error_code ec;
    const fs::path path = ToPath(file);
    if (!fs::exists(path, ec))
        return true;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error)
            *error = "cannot read " + file;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
            ++start;
        line.erase(0, start);
        if (line.empty() || line[0] == '#')
            continue;
        std::replace(line.begin(), line.end(), '\\', '/');
        out.push_back(line);
    }
    return true;
}

bool PathListed(const std::string& path, const std::vector<std::string>& list)
{
    for (const std::string& p : list) {
        const bool folder = !p.empty() && p.back() == '/';
        if (folder ? path.size() > p.size() && CaseFoldEqual(path.substr(0, p.size()), p) : CaseFoldEqual(path, p))
            return true;
    }
    return false;
}

bool WritePathList(const std::string& file, const std::vector<std::string>& paths, std::string* error)
{
    std::ofstream out(ToPath(file), std::ios::binary | std::ios::trunc);
    out << "# Plugins OpenSC4 leaves out, relative to the Plugins folder; a path ending in / is a whole folder.\n";
    for (const std::string& p : paths)
        out << p << '\n';
    out.flush();
    if (!out) {
        if (error)
            *error = "cannot write " + file;
        return false;
    }
    return true;
}

void LoadOrder::Add(PluginFile file)
{
    if (file.package)
        file.package->Release(); // reopened on first read
    files_.push_back(std::move(file));
    indexed_ = false;
}

bool LoadOrder::AddFolder(const std::string& folder, std::string* error)
{
    std::vector<std::string> paths;
    if (!ListFolder(folder, paths, error))
        return false;
    for (const bool dat : {true, false}) {
        for (const std::string& rel : paths) {
            if (IsDatFile(rel) != dat)
                continue;
            PluginFile file;
            file.path = rel;
            file.segment = segments_;
            file.package = Package::OpenFile(FromPath(ToPath(folder) / ToPath(rel)), &file.error);
            Add(std::move(file));
        }
        ++segments_;
    }
    return true;
}

void LoadOrder::AddFile(const std::string& path)
{
    PluginFile file;
    file.path = path;
    file.segment = segments_++;
    file.package = Package::OpenFile(path, &file.error);
    Add(std::move(file));
}

void LoadOrder::AddSegment(std::vector<NamedSource> sources)
{
    for (NamedSource& s : sources) {
        PluginFile file;
        file.path = std::move(s.path);
        file.segment = segments_;
        file.package = Package::Open(std::move(s.source), &file.error);
        Add(std::move(file));
    }
    ++segments_;
}

void LoadOrder::SetEnabled(size_t file, bool enabled)
{
    if (file < files_.size() && files_[file].enabled != enabled) {
        files_[file].enabled = enabled;
        indexed_ = false;
    }
}

size_t LoadOrder::ApplyDisabled(const std::vector<std::string>& paths)
{
    std::vector<bool> used(paths.size());
    for (size_t i = 0; i < files_.size(); ++i) {
        bool off = false;
        for (size_t j = 0; j < paths.size(); ++j)
            if (PathListed(files_[i].path, {paths[j]})) {
                off = true;
                used[j] = true;
            }
        SetEnabled(i, !off);
    }
    return static_cast<size_t>(std::count(used.begin(), used.end(), true));
}

std::vector<std::string> LoadOrder::DisabledPaths() const
{
    std::vector<std::string> paths;
    for (const PluginFile& f : files_)
        if (!f.enabled)
            paths.push_back(f.path);
    return paths;
}

std::vector<size_t> LoadOrder::Priority() const
{
    // Files are stored segment by segment; reverse each segment's run.
    std::vector<size_t> order;
    order.reserve(files_.size());
    for (size_t start = 0; start < files_.size();) {
        size_t stop = start;
        while (stop < files_.size() && files_[stop].segment == files_[start].segment)
            ++stop;
        for (size_t i = stop; i > start; --i)
            order.push_back(i - 1);
        start = stop;
    }
    return order;
}

const std::unordered_map<Key, std::vector<uint32_t>, KeyHash>& LoadOrder::Holders() const
{
    if (indexed_)
        return holders_;
    holders_.clear();
    for (size_t i : Priority()) {
        if (!files_[i].package || !files_[i].enabled)
            continue;
        for (const Entry& e : files_[i].package->Entries()) {
            if (e.key == kDirectoryKey)
                continue;
            std::vector<uint32_t>& h = holders_[e.key];
            if (h.empty() || h.back() != i) // a package may list a key twice
                h.push_back(static_cast<uint32_t>(i));
        }
    }
    indexed_ = true;
    return holders_;
}

const PluginFile* LoadOrder::Winner(const Key& key) const
{
    const auto& holders = Holders();
    const auto it = holders.find(key);
    return it == holders.end() ? nullptr : &files_[it->second[0]];
}

size_t LoadOrder::KeyCount() const
{
    return Holders().size();
}

bool LoadOrder::ReadEntry(size_t file, const Key& key, std::vector<uint8_t>& out, std::string* error)
{
    Package& package = *files_[file].package;
    const bool ok = package.Read(*package.Find(key), out, error);
    // Keep the most recently read files open, and only those.
    constexpr size_t kMaxOpen = 64;
    const auto it = std::find(open_.begin(), open_.end(), file);
    if (it != open_.end())
        open_.erase(it);
    open_.push_back(file);
    if (open_.size() > kMaxOpen) {
        files_[open_.front()].package->Release();
        open_.erase(open_.begin());
    }
    return ok;
}

bool LoadOrder::Read(const Key& key, std::vector<uint8_t>& out, std::string* error)
{
    const auto& holders = Holders();
    const auto it = holders.find(key);
    if (it == holders.end()) {
        if (error)
            *error = "no enabled file has this key";
        return false;
    }
    return ReadEntry(it->second[0], key, out, error);
}

std::vector<FileSummary> LoadOrder::Summaries() const
{
    std::vector<FileSummary> summaries(files_.size());
    for (const auto& [key, files] : Holders()) {
        FileSummary& winner = summaries[files[0]];
        ++winner.served;
        winner.overrides += files.size() > 1;
        for (size_t j = 1; j < files.size(); ++j) {
            ++summaries[files[j]].hidden;
            winner.beats.push_back(files[j]);
            summaries[files[j]].beatenBy.push_back(files[0]);
        }
    }
    for (FileSummary& s : summaries) {
        s.keys = s.served + s.hidden;
        for (std::vector<size_t>* v : {&s.beats, &s.beatenBy}) {
            std::sort(v->begin(), v->end());
            v->erase(std::unique(v->begin(), v->end()), v->end());
        }
    }
    return summaries;
}

std::vector<Conflict> LoadOrder::Conflicts(bool compareContents)
{
    std::vector<Conflict> conflicts;
    for (const auto& [key, files] : Holders())
        if (files.size() > 1)
            conflicts.push_back({key, std::vector<size_t>(files.begin(), files.end()), false});

    std::vector<size_t> rank(files_.size());
    const std::vector<size_t> priority = Priority();
    for (size_t r = 0; r < priority.size(); ++r)
        rank[priority[r]] = r;
    std::sort(conflicts.begin(), conflicts.end(), [&](const Conflict& a, const Conflict& b) {
        if (a.files[0] != b.files[0])
            return rank[a.files[0]] < rank[b.files[0]];
        if (a.key.type != b.key.type)
            return a.key.type < b.key.type;
        if (a.key.group != b.key.group)
            return a.key.group < b.key.group;
        return a.key.instance < b.key.instance;
    });

    if (compareContents) {
        std::vector<uint8_t> first, other;
        for (Conflict& c : conflicts) {
            c.identical = ReadEntry(c.files[0], c.key, first, nullptr);
            for (size_t j = 1; c.identical && j < c.files.size(); ++j)
                c.identical = ReadEntry(c.files[j], c.key, other, nullptr) && other == first;
        }
    }
    return conflicts;
}

const char* TypeName(uint32_t type)
{
    switch (type) {
    case 0x6534284A:
        return "exemplar";
    case 0x05342861:
        return "cohort";
    case 0x2026960B:
        return "LTEXT";
    case 0x7AB50E44:
        return "FSH";
    case 0x5AD0E817:
        return "S3D";
    case 0x856DDBAC:
        return "PNG";
    default:
        return nullptr;
    }
}

} // namespace opensc4::dbpf
