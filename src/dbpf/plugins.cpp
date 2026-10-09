#include "dbpf/plugins.h"

#include <algorithm>
#include <filesystem>
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

bool ListFolder(const std::string& folder, std::vector<std::string>& out, std::string* error)
{
    out.clear();
    const fs::path root = ToPath(folder);
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
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
    SortFolderOrder(out);
    return true;
}

bool LoadOrder::AddFolder(const std::string& folder, std::string* error)
{
    std::vector<std::string> paths;
    if (!ListFolder(folder, paths, error))
        return false;
    for (const std::string& rel : paths) {
        PluginFile file;
        file.path = rel;
        file.segment = segments_;
        file.package = Package::OpenFile(FromPath(ToPath(folder) / ToPath(rel)), &file.error);
        files_.push_back(std::move(file));
    }
    ++segments_;
    return true;
}

void LoadOrder::AddFile(const std::string& path)
{
    PluginFile file;
    file.path = path;
    file.segment = segments_++;
    file.package = Package::OpenFile(path, &file.error);
    files_.push_back(std::move(file));
}

void LoadOrder::AddSegment(std::vector<NamedSource> sources)
{
    for (NamedSource& s : sources) {
        PluginFile file;
        file.path = std::move(s.path);
        file.segment = segments_;
        file.package = Package::Open(std::move(s.source), &file.error);
        files_.push_back(std::move(file));
    }
    ++segments_;
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

const PluginFile* LoadOrder::Winner(const Key& key) const
{
    if (key == kDirectoryKey)
        return nullptr;
    for (size_t i : Priority())
        if (files_[i].package && files_[i].package->Find(key))
            return &files_[i];
    return nullptr;
}

std::vector<Conflict> LoadOrder::Conflicts(bool compareContents)
{
    const std::vector<size_t> priority = Priority();
    std::unordered_map<Key, std::vector<size_t>, KeyHash> holders;
    for (size_t i : priority) {
        if (!files_[i].package)
            continue;
        for (const Entry& e : files_[i].package->Entries()) {
            if (e.key == kDirectoryKey)
                continue;
            std::vector<size_t>& h = holders[e.key];
            if (h.empty() || h.back() != i) // a package may list a key twice
                h.push_back(i);
        }
    }

    std::vector<Conflict> conflicts;
    for (auto& [key, files] : holders)
        if (files.size() > 1)
            conflicts.push_back({key, std::move(files), false});

    std::vector<size_t> rank(files_.size());
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
            Package& winner = *files_[c.files[0]].package;
            c.identical = winner.Read(*winner.Find(c.key), first);
            for (size_t j = 1; c.identical && j < c.files.size(); ++j) {
                Package& p = *files_[c.files[j]].package;
                c.identical = p.Read(*p.Find(c.key), other) && other == first;
            }
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
