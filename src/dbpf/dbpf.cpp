#include "dbpf/dbpf.h"

#include "dbpf/qfs.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace opensc4::dbpf {
namespace {

bool Fail(std::string* error, const std::string& why)
{
    if (error)
        *error = why;
    return false;
}

uint32_t U32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

constexpr size_t kHeaderSize = 96;
constexpr size_t kIndexEntrySize = 20; // index 7.0: type, group, instance, offset, size
constexpr size_t kDirectoryEntrySize = 16; // type, group, instance, uncompressed size

// 64-bit file positions (packages can pass 2 GB in theory).
int Seek(std::FILE* file, int64_t offset, int origin)
{
#ifdef _WIN32
    return _fseeki64(file, offset, origin);
#else
    return fseeko(file, static_cast<off_t>(offset), origin);
#endif
}

// Paths are UTF-8; on Windows they go through the wide API, so any user
// folder name works.
std::FILE* OpenRead(const std::string& path)
{
#ifdef _WIN32
    std::wstring wide;
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return nullptr;
    wide.resize(static_cast<size_t>(n));
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
    std::FILE* file = nullptr;
    return _wfopen_s(&file, wide.c_str(), L"rb") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

int64_t Tell(std::FILE* file)
{
#ifdef _WIN32
    return _ftelli64(file);
#else
    return static_cast<int64_t>(ftello(file));
#endif
}

} // namespace

bool MemorySource::Read(uint64_t offset, std::span<uint8_t> out)
{
    if (offset > bytes_.size() || out.size() > bytes_.size() - offset)
        return false;
    std::memcpy(out.data(), bytes_.data() + offset, out.size());
    return true;
}

std::unique_ptr<FileSource> FileSource::Open(const std::string& path)
{
    std::FILE* file = OpenRead(path);
    if (!file)
        return nullptr;
    if (Seek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return nullptr;
    }
    const int64_t size = Tell(file);
    if (size < 0) {
        std::fclose(file);
        return nullptr;
    }
    return std::unique_ptr<FileSource>(new FileSource(file, static_cast<uint64_t>(size)));
}

FileSource::~FileSource()
{
    std::fclose(file_);
}

bool FileSource::Read(uint64_t offset, std::span<uint8_t> out)
{
    if (offset > size_ || out.size() > size_ - offset)
        return false;
    if (Seek(file_, static_cast<int64_t>(offset), SEEK_SET) != 0)
        return false;
    return std::fread(out.data(), 1, out.size(), file_) == out.size();
}

std::unique_ptr<Package> Package::Open(std::unique_ptr<Source> source, std::string* error)
{
    if (!source) {
        Fail(error, "no source");
        return nullptr;
    }
    std::unique_ptr<Package> package(new Package(std::move(source)));
    if (!package->Load(error))
        return nullptr;
    return package;
}

std::unique_ptr<Package> Package::OpenFile(const std::string& path, std::string* error)
{
    std::unique_ptr<FileSource> source = FileSource::Open(path);
    if (!source) {
        Fail(error, "cannot open " + path);
        return nullptr;
    }
    return Open(std::move(source), error);
}

bool Package::Load(std::string* error)
{
    uint8_t h[kHeaderSize];
    if (!source_->Read(0, h))
        return Fail(error, "shorter than a DBPF header");
    if (std::memcmp(h, "DBPF", 4) != 0)
        return Fail(error, "no DBPF signature");
    header_.majorVersion = U32(h + 0x04);
    header_.minorVersion = U32(h + 0x08);
    header_.userMajor = U32(h + 0x0C);
    header_.userMinor = U32(h + 0x10);
    header_.created = U32(h + 0x18);
    header_.modified = U32(h + 0x1C);
    header_.indexMajorVersion = U32(h + 0x20);
    header_.indexCount = U32(h + 0x24);
    header_.indexOffset = U32(h + 0x28);
    header_.indexSize = U32(h + 0x2C);
    header_.holeCount = U32(h + 0x30);
    header_.holeOffset = U32(h + 0x34);
    header_.holeSize = U32(h + 0x38);
    if (header_.majorVersion != 1 || header_.minorVersion != 0)
        return Fail(error, "DBPF version " + std::to_string(header_.majorVersion) + "." +
                               std::to_string(header_.minorVersion) + " (only 1.0 is supported)");
    if (header_.indexMajorVersion != 7)
        return Fail(error, "index version " + std::to_string(header_.indexMajorVersion) + " (only 7 is supported)");
    if (static_cast<uint64_t>(header_.indexCount) * kIndexEntrySize > header_.indexSize)
        return Fail(error, "index smaller than its entry count");

    std::vector<uint8_t> index(static_cast<size_t>(header_.indexCount) * kIndexEntrySize);
    if (!source_->Read(header_.indexOffset, index))
        return Fail(error, "index runs past the end of the file");
    entries_.resize(header_.indexCount);
    const uint64_t fileSize = source_->Size();
    for (uint32_t i = 0; i < header_.indexCount; ++i) {
        const uint8_t* p = index.data() + static_cast<size_t>(i) * kIndexEntrySize;
        Entry& e = entries_[i];
        e.key = {U32(p), U32(p + 4), U32(p + 8)};
        e.offset = U32(p + 12);
        e.size = U32(p + 16);
        e.uncompressedSize = e.size;
        if (static_cast<uint64_t>(e.offset) + e.size > fileSize)
            return Fail(error, "record " + std::to_string(i) + " runs past the end of the file");
        byKey_[e.key] = i;
    }

    // The directory record names the compressed records.
    if (const Entry* dir = Find(kDirectoryKey)) {
        std::vector<uint8_t> bytes;
        if (!ReadRaw(*dir, bytes, error))
            return false;
        if (bytes.size() % kDirectoryEntrySize != 0)
            return Fail(error, "directory record size is not a multiple of 16");
        for (size_t at = 0; at < bytes.size(); at += kDirectoryEntrySize) {
            const uint8_t* p = bytes.data() + at;
            auto it = byKey_.find({U32(p), U32(p + 4), U32(p + 8)});
            if (it == byKey_.end())
                continue; // listed but absent: the game tolerates it too (not checked)
            Entry& e = entries_[it->second];
            e.compressed = true;
            e.uncompressedSize = U32(p + 12);
        }
    }
    return true;
}

const Entry* Package::Find(const Key& key) const
{
    auto it = byKey_.find(key);
    return it == byKey_.end() ? nullptr : &entries_[it->second];
}

bool Package::ReadRaw(const Entry& entry, std::vector<uint8_t>& out, std::string* error)
{
    out.resize(entry.size);
    if (!source_->Read(entry.offset, out))
        return Fail(error, "cannot read the record");
    return true;
}

bool Package::Read(const Entry& entry, std::vector<uint8_t>& out, std::string* error)
{
    if (!entry.compressed)
        return ReadRaw(entry, out, error);
    std::vector<uint8_t> raw;
    if (!ReadRaw(entry, raw, error))
        return false;
    if (!Decompress(raw, out, error))
        return false;
    if (out.size() != entry.uncompressedSize)
        return Fail(error, "decompressed size " + std::to_string(out.size()) + " differs from the directory's " +
                               std::to_string(entry.uncompressedSize));
    return true;
}

} // namespace opensc4::dbpf
