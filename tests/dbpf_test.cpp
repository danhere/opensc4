// DBPF reader and QFS decompression on hand-built packages and streams.
#include "dbpf/dbpf.h"
#include "dbpf/qfs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace opensc4::dbpf;
using Bytes = std::vector<uint8_t>;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++g_failures;
    }
}

Bytes Str(const char* s)
{
    return Bytes(s, s + std::strlen(s));
}

Bytes Cat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const Bytes& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

void Put32(Bytes& b, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// A record as packages store it: compressed size, QFS header, stream.
Bytes Qfs(const Bytes& stream, size_t uncompressed)
{
    Bytes out;
    Put32(out, static_cast<uint32_t>(4 + 5 + stream.size()));
    out.push_back(0x10);
    out.push_back(0xFB);
    out.push_back(static_cast<uint8_t>(uncompressed >> 16));
    out.push_back(static_cast<uint8_t>(uncompressed >> 8));
    out.push_back(static_cast<uint8_t>(uncompressed));
    out.insert(out.end(), stream.begin(), stream.end());
    return out;
}

bool Expand(const Bytes& stream, size_t size, Bytes& out, std::string* error = nullptr)
{
    return Decompress(Qfs(stream, size), out, error);
}

void TestQfs()
{
    Bytes out;
    std::string error;

    // 0xE0-0xFB: 4 to 112 literals; 0xFC: stop.
    Check(Expand(Cat({{0xE2}, Str("hello world!"), {0xFC}}), 12, out) && out == Str("hello world!"), "literal run");
    // Stop command carrying 1-3 literals.
    Check(Expand(Cat({{0xE0}, Str("abcd"), {0xFE}, Str("ef")}), 6, out) && out == Str("abcdef"), "stop with literals");
    // 2-byte command: 3 literals, copy 6 from 3 back (overlapping run).
    Check(Expand(Cat({{0x0F, 0x02}, Str("abc"), {0xFC}}), 9, out) && out == Str("abcabcabc"), "2-byte copy");
    // 3-byte command: copy 10 from 4 back.
    Check(Expand(Cat({{0xE0}, Str("wxyz"), {0x86, 0x00, 0x03}, {0xFC}}), 14, out) && out == Str("wxyzwxyzwxyzwx"),
          "3-byte copy");
    // 4-byte command: copy 300 from 4 back.
    {
        Bytes want;
        for (int i = 0; i < 76; ++i)
            want.insert(want.end(), {'a', 'b', 'c', 'd'});
        Check(Expand(Cat({{0xE0}, Str("abcd"), {0xC4, 0x00, 0x03, 0x27}, {0xFC}}), 304, out) && out == want,
              "4-byte copy");
    }
    // 2-byte command with offset bits above 8 (offset 0x101) and a 1-byte run (offset 1).
    {
        Bytes lit(0x104, 0);
        for (size_t i = 0; i < lit.size(); ++i)
            lit[i] = static_cast<uint8_t>(i * 7);
        Bytes stream;
        for (size_t at = 0; at < lit.size(); at += 4) {
            stream.push_back(0xE0);
            stream.insert(stream.end(), lit.begin() + static_cast<long>(at), lit.begin() + static_cast<long>(at) + 4);
        }
        stream.insert(stream.end(), {0x20, 0x00}); // copy 3 from 0x101 back
        stream.insert(stream.end(), {0x00, 0x00}); // copy 3 from 1 back
        stream.push_back(0xFC);
        Bytes want = lit;
        for (int i = 0; i < 3; ++i)
            want.push_back(want[want.size() - 0x101]);
        for (int i = 0; i < 3; ++i)
            want.push_back(want.back());
        Check(Expand(stream, want.size(), out) && out == want, "2-byte copy, far offset and run");
    }

    // Malformed streams fail instead of reading or writing out of bounds.
    Check(!Expand({0xE2, 'a', 'b'}, 12, out, &error), "truncated literals rejected");
    Check(!Expand({0x00, 0x05, 0xFC}, 3, out, &error), "back-reference before start rejected");
    Check(!Expand(Cat({{0xE0}, Str("abcd"), {0xFC}}), 3, out, &error), "output past stated size rejected");
    Check(!Expand(Cat({{0xE0}, Str("abcd"), {0xFC}}), 9, out, &error), "output short of stated size rejected");
    Check(!Expand(Cat({{0xE0}, Str("abcd")}), 4, out, &error), "missing stop rejected");
    Check(!Decompress(Str("not compressed"), out, &error), "missing QFS header rejected");
}

struct Record {
    Key key;
    Bytes stored;
    uint32_t uncompressed = 0; // non-zero: list in the directory record
};

// header, records, directory, index
Bytes BuildPackage(std::vector<Record> records)
{
    Bytes dir;
    for (const Record& r : records)
        if (r.uncompressed) {
            Put32(dir, r.key.type);
            Put32(dir, r.key.group);
            Put32(dir, r.key.instance);
            Put32(dir, r.uncompressed);
        }
    if (!dir.empty())
        records.push_back({kDirectoryKey, dir, 0});

    Bytes file(96, 0);
    std::memcpy(file.data(), "DBPF", 4);
    file[4] = 1;      // major 1
    file[0x20] = 7;   // index 7
    Bytes index;
    for (const Record& r : records) {
        Put32(index, r.key.type);
        Put32(index, r.key.group);
        Put32(index, r.key.instance);
        Put32(index, static_cast<uint32_t>(file.size()));
        Put32(index, static_cast<uint32_t>(r.stored.size()));
        file.insert(file.end(), r.stored.begin(), r.stored.end());
    }
    const uint32_t indexOffset = static_cast<uint32_t>(file.size());
    file.insert(file.end(), index.begin(), index.end());
    auto set32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            file[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    set32(0x24, static_cast<uint32_t>(records.size()));
    set32(0x28, indexOffset);
    set32(0x2C, static_cast<uint32_t>(index.size()));
    return file;
}

std::unique_ptr<Package> OpenBytes(Bytes bytes, std::string* error = nullptr)
{
    return Package::Open(std::make_unique<MemorySource>(std::move(bytes)), error);
}

void TestPackage()
{
    const Key plain{1, 2, 3}, packed{4, 5, 6}, twice{7, 8, 9};
    const Bytes packedStream = Cat({{0x0F, 0x02}, Str("abc"), {0xFC}});
    std::string error;
    auto package = OpenBytes(BuildPackage({
                                 {plain, Str("plain record"), 0},
                                 {packed, Qfs(packedStream, 9), 9},
                                 {twice, Str("first"), 0},
                                 {twice, Str("second"), 0},
                             }),
                             &error);
    Check(package != nullptr, "package opens");
    if (!package) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    Check(package->Entries().size() == 5, "five index entries (with the directory)");
    Bytes out;
    const Entry* e = package->Find(plain);
    Check(e && !e->compressed && package->Read(*e, out) && out == Str("plain record"), "plain record");
    e = package->Find(packed);
    Check(e && e->compressed && e->uncompressedSize == 9, "directory marks the compressed record");
    Check(e && package->Read(*e, out) && out == Str("abcabcabc"), "compressed record decompresses");
    Check(e && package->ReadRaw(*e, out) && out == Qfs(packedStream, 9), "raw read returns stored bytes");
    e = package->Find(twice);
    Check(e && package->Read(*e, out) && out == Str("second"), "duplicate key: last entry found");
    Check(package->Find({9, 9, 9}) == nullptr, "missing key");

    // Corrupt or unsupported files are refused with a reason.
    Bytes good = BuildPackage({{plain, Str("x"), 0}});
    Bytes bad = good;
    bad[0] = 'X';
    Check(!OpenBytes(bad, &error) && error.find("signature") != std::string::npos, "bad signature refused");
    bad = good;
    bad[0x20] = 8;
    Check(!OpenBytes(bad, &error) && error.find("index version") != std::string::npos, "index version refused");
    bad = good;
    bad[0x28] = 0xFF;
    bad[0x29] = 0xFF;
    Check(!OpenBytes(bad, &error) && error.find("index") != std::string::npos, "index past end refused");
    bad = good;
    bad[good.size() - 4] = 0xFF; // record size
    Check(!OpenBytes(bad, &error) && error.find("past the end") != std::string::npos, "record past end refused");
    Check(!OpenBytes(Bytes(10, 0), &error), "short file refused");

    // A directory entry whose size disagrees with the stream is an error on read.
    auto mismatch = OpenBytes(BuildPackage({{packed, Qfs(packedStream, 9), 8}}));
    e = mismatch ? mismatch->Find(packed) : nullptr;
    Check(e && !mismatch->Read(*e, out, &error), "directory size mismatch reported");
}

} // namespace

int main()
{
    TestQfs();
    TestPackage();
    if (g_failures) {
        std::printf("%d failures\n", g_failures);
        return 1;
    }
    std::printf("dbpf test OK\n");
    return 0;
}
