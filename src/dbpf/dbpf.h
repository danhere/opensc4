// DBPF package reader: the file format of SimCity 4's data files, plugins
// and saved cities (docs/resources.md, "Package format"). Portable C++20, no
// game or Windows dependencies. Reads version 1.0 packages with index
// version 7.0, the only kind SimCity 4 writes.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace opensc4::dbpf {

struct Key {
    uint32_t type = 0;
    uint32_t group = 0;
    uint32_t instance = 0;

    friend bool operator==(const Key&, const Key&) = default;
};

struct KeyHash {
    size_t operator()(const Key& k) const noexcept
    {
        return (static_cast<size_t>(k.type) * 0x9E3779B1u) ^ (static_cast<size_t>(k.group) * 0x85EBCA77u) ^ k.instance;
    }
};

// The directory record: lists the compressed records and their sizes.
constexpr Key kDirectoryKey{0xE86B1EEF, 0xE86B1EEF, 0x286B1F03};

struct Entry {
    Key key;
    uint32_t offset = 0;
    uint32_t size = 0;             // stored size
    uint32_t uncompressedSize = 0; // equals size unless compressed
    bool compressed = false;       // listed in the directory record
};

// Where package bytes come from.
class Source {
public:
    virtual ~Source() = default;
    virtual uint64_t Size() const = 0;
    // Reads exactly out.size() bytes at offset; false if that runs past the end or fails.
    virtual bool Read(uint64_t offset, std::span<uint8_t> out) = 0;
    // Gives back any operating-system handle until the next Read, so a
    // plugin folder of thousands of packages does not hold thousands of
    // files open.
    virtual void Release() {}
};

class MemorySource final : public Source {
public:
    explicit MemorySource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    uint64_t Size() const override { return bytes_.size(); }
    bool Read(uint64_t offset, std::span<uint8_t> out) override;

private:
    std::vector<uint8_t> bytes_;
};

class FileSource final : public Source {
public:
    // nullptr if the file cannot be opened. `path` is UTF-8.
    static std::unique_ptr<FileSource> Open(const std::string& path);
    ~FileSource() override;
    uint64_t Size() const override { return size_; }
    bool Read(uint64_t offset, std::span<uint8_t> out) override;
    void Release() override;

private:
    FileSource(std::string path, std::FILE* file, uint64_t size) : path_(std::move(path)), file_(file), size_(size) {}
    std::string path_;
    std::FILE* file_; // null while released
    uint64_t size_;
};

struct Header {
    uint32_t majorVersion = 0;
    uint32_t minorVersion = 0;
    uint32_t userMajor = 0;
    uint32_t userMinor = 0;
    uint32_t created = 0;
    uint32_t modified = 0;
    uint32_t indexMajorVersion = 0;
    uint32_t indexCount = 0;
    uint32_t indexOffset = 0;
    uint32_t indexSize = 0;
    uint32_t holeCount = 0;
    uint32_t holeOffset = 0;
    uint32_t holeSize = 0;
};

class Package {
public:
    // Reads the header, the index and the directory record. Null (with a
    // reason in `error`) if the source is not a package this reader handles.
    static std::unique_ptr<Package> Open(std::unique_ptr<Source> source, std::string* error = nullptr);
    static std::unique_ptr<Package> OpenFile(const std::string& path, std::string* error = nullptr);

    const Header& GetHeader() const { return header_; }
    // In index order. A key may appear more than once; Find returns the last
    // one (not yet checked against the game, see docs/resources.md).
    const std::vector<Entry>& Entries() const { return entries_; }
    const Entry* Find(const Key& key) const;

    // The record as stored (compressed records still compressed).
    bool ReadRaw(const Entry& entry, std::vector<uint8_t>& out, std::string* error = nullptr);
    // The record's contents, decompressed if the directory lists it.
    bool Read(const Entry& entry, std::vector<uint8_t>& out, std::string* error = nullptr);
    // Closes the file until the next read (Source::Release).
    void Release() { source_->Release(); }

private:
    explicit Package(std::unique_ptr<Source> source) : source_(std::move(source)) {}
    bool Load(std::string* error);

    std::unique_ptr<Source> source_;
    Header header_;
    std::vector<Entry> entries_;
    std::unordered_map<Key, size_t, KeyHash> byKey_;
};

} // namespace opensc4::dbpf
