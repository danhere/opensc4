// DBPF reader, QFS decompression and plugin load order on hand-built
// packages and streams.
#include "dbpf/dbpf.h"
#include "dbpf/plugins.h"
#include "dbpf/qfs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

bool SameOrder(std::vector<std::string> in, const std::vector<std::string>& want)
{
    SortFolderOrder(in);
    return in == want;
}

void TestFolderOrder()
{
    // The probe layout docs/resources.md measured in the game.
    Check(SameOrder({"zz_sub/bb_probe.dat", "mm_probe.dat", "aa_sub/cc_probe.dat", "aa_probe.dat"},
                    {"aa_probe.dat", "mm_probe.dat", "aa_sub/cc_probe.dat", "zz_sub/bb_probe.dat"}),
          "top-level files, then subfolders, alphabetically");
    Check(SameOrder({"B.dat", "a.dat", "c.dat"}, {"a.dat", "B.dat", "c.dat"}), "names compare without case");
    // NTFS compares uppercase forms, so '_' (0x5F) sorts after letters.
    Check(SameOrder({"a_b.dat", "ab.dat"}, {"ab.dat", "a_b.dat"}), "underscore after letters");
    Check(SameOrder({"sub/deeper/a.dat", "sub/z.dat", "z.dat"}, {"z.dat", "sub/z.dat", "sub/deeper/a.dat"}),
          "each subfolder ordered the same way");
}

std::unique_ptr<Source> Mem(Bytes bytes)
{
    return std::make_unique<MemorySource>(std::move(bytes));
}

void WriteFile(const std::filesystem::path& path, const Bytes& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void TestLoadOrder()
{
    const Key shared{1, 1, 1}, same{2, 2, 2}, own{3, 3, 3}, gameOnly{4, 4, 4};
    const Bytes packedStream = Cat({{0x0F, 0x02}, Str("abc"), {0xFC}});
    LoadOrder order;
    std::vector<LoadOrder::NamedSource> plugins;
    plugins.push_back({"a.dat", Mem(BuildPackage({{shared, Str("from a"), 0}, {same, Qfs(packedStream, 9), 9}}))});
    plugins.push_back({"readme.txt", Mem(Str("not a package"))});
    // The same contents, stored uncompressed: still identical.
    plugins.push_back({"b.dat", Mem(BuildPackage({{shared, Str("from b"), 0},
                                                  {same, Str("abcabcabc"), 0},
                                                  {own, Str("only b"), 0},
                                                  {own, Str("only b, again"), 0}}))});
    order.AddSegment(std::move(plugins));
    std::vector<LoadOrder::NamedSource> game;
    game.push_back({"SimCity_1.dat", Mem(BuildPackage({{shared, Str("from the game"), 0}, {gameOnly, Str("g"), 0}}))});
    order.AddSegment(std::move(game));

    const auto& files = order.Files();
    Check(files.size() == 4 && order.SegmentCount() == 2, "four files in two segments");
    Check(files.size() == 4 && !files[1].package && !files[1].error.empty(), "non-package kept with a reason");

    const PluginFile* w = order.Winner(shared);
    Check(w && w->path == "b.dat", "inside a folder the last file wins");
    w = order.Winner(gameOnly);
    Check(w && w->path == "SimCity_1.dat", "game file serves what no plugin has");
    Check(order.Winner({9, 9, 9}) == nullptr && order.Winner(kDirectoryKey) == nullptr, "no winner");

    const std::vector<Conflict> conflicts = order.Conflicts();
    Check(conflicts.size() == 2, "two conflicts (no directory record, no in-package duplicate)");
    if (conflicts.size() == 2) {
        Check(conflicts[0].key == shared && conflicts[0].files == std::vector<size_t>{2, 0, 3} &&
                  !conflicts[0].identical,
              "shared key: b over a over the game, contents differ");
        Check(conflicts[1].key == same && conflicts[1].files == std::vector<size_t>{2, 0} && conflicts[1].identical,
              "same key: identical after decompression");
    }
    Check(order.Conflicts(false).size() == 2, "conflicts without reading contents");
    Check(order.KeyCount() == 4, "four distinct keys served");
    Bytes out;
    Check(order.Read(shared, out) && out == Str("from b"), "read serves the winning copy");
    Check(order.Read(same, out) && out == Str("abcabcabc"), "read decompresses");
    Check(!order.Read({9, 9, 9}, out), "read of a missing key fails");

    std::vector<FileSummary> sums = order.Summaries();
    Check(sums.size() == 4 && sums[2].served == 3 && sums[2].overrides == 2 && sums[2].hidden == 0 &&
              sums[2].beats == std::vector<size_t>{0, 3},
          "b serves three keys and overrides two, over a and the game");
    Check(sums.size() == 4 && sums[0].keys == 2 && sums[0].served == 0 && sums[0].hidden == 2 &&
              sums[0].beatenBy == std::vector<size_t>{2},
          "a is fully hidden by b");
    Check(sums.size() == 4 && sums[1].keys == 0, "non-package has nothing");

    // Turning b off hands its keys back to a and the game.
    order.SetEnabled(2, false);
    w = order.Winner(shared);
    Check(w && w->path == "a.dat", "with b off, a wins");
    Check(order.Winner(own) == nullptr, "b's own key gone with it");
    Check(order.Conflicts().size() == 1, "one conflict left (a over the game)");
    Check(order.DisabledPaths() == std::vector<std::string>{"b.dat"}, "b listed as off");
    sums = order.Summaries();
    Check(sums[2].keys == 0 && sums[0].served == 2, "summaries follow the switch");
    Check(order.ApplyDisabled({"A.DAT", "nothing.dat"}) == 1, "disabled list: one entry matches, without case");
    w = order.Winner(shared);
    Check(w && w->path == "b.dat" && !files[0].enabled && files[2].enabled, "list turns a off and b back on");
}

void TestPathList()
{
    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / "opensc4_disabled_test.txt";
    const std::u8string u8 = file.generic_u8string();
    const std::string path(u8.begin(), u8.end());
    std::error_code ec;
    fs::remove(file, ec);
    std::vector<std::string> list{"x"};
    Check(ReadPathList(path, list) && list.empty(), "missing list reads as empty");
    Check(WritePathList(path, {"zz_mod.dat", "NAM/"}) && ReadPathList(path, list) &&
              list == std::vector<std::string>{"zz_mod.dat", "NAM/"},
          "list round trip");
    WriteFile(file, Str("# note\r\n\r\n  sub\\old.dat  \r\nnew.dat"));
    Check(ReadPathList(path, list) && list == std::vector<std::string>{"sub/old.dat", "new.dat"},
          "comments, blanks, CRLF, backslashes");

    LoadOrder order;
    std::vector<LoadOrder::NamedSource> plugins;
    plugins.push_back({"keep.dat", Mem(BuildPackage({{{1, 1, 1}, Str("k")}}))});
    plugins.push_back({"NAM/a.dat", Mem(BuildPackage({{{1, 1, 1}, Str("a")}}))});
    plugins.push_back({"NAM/sub/b.dat", Mem(BuildPackage({{{1, 1, 1}, Str("b")}}))});
    plugins.push_back({"NAMx.dat", Mem(BuildPackage({{{1, 1, 1}, Str("x")}}))});
    order.AddSegment(std::move(plugins));
    Check(order.ApplyDisabled({"nam/"}) == 1 &&
              order.DisabledPaths() == std::vector<std::string>{"NAM/a.dat", "NAM/sub/b.dat"},
          "a folder entry turns off everything under it, and only that");
    fs::remove(file, ec);
}

void TestPluginFolder()
{
    namespace fs = std::filesystem;
    const Key key{5, 5, 5};
    const fs::path root = fs::temp_directory_path() / "opensc4_plugins_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    WriteFile(root / "zzz_override.dat", BuildPackage({{key, Str("top level")}}));
    WriteFile(root / "aaa_mods" / "mod.dat", BuildPackage({{key, Str("subfolder")}}));
    WriteFile(root / "notes.txt", Str("hello"));
    WriteFile(root / "zzz_lot.SC4Lot", BuildPackage({{key, Str("loose")}, {{5, 5, 6}, Str("loose only")}}));

    const std::u8string u8 = root.generic_u8string();
    const std::string folder(u8.begin(), u8.end());
    std::vector<std::string> listed;
    Check(ListFolder(folder, listed) &&
              listed == std::vector<std::string>{"notes.txt", "zzz_lot.SC4Lot", "zzz_override.dat",
                                                 "aaa_mods/mod.dat"},
          "folder listed in load order");
    Check(ListFolder(folder, listed, nullptr, false) &&
              listed == std::vector<std::string>{"notes.txt", "zzz_lot.SC4Lot", "zzz_override.dat"},
          "folder listed without subfolders");
    Check(IsDatFile("a/b.DAT") && IsDatFile("x.dat") && !IsDatFile("x.dat.bak") && !IsDatFile("a.dat/x") &&
              !IsDatFile("noext"),
          ".dat files told apart without case");

    LoadOrder order;
    std::string error;
    Check(order.AddFolder(folder, &error), "folder segment added");
    const PluginFile* w = order.Winner(key);
    Check(w && w->path == "aaa_mods/mod.dat", "a subfolder file beats any top-level file");
    w = order.Winner({5, 5, 6});
    Check(w && w->path == "zzz_lot.SC4Lot" && order.SegmentCount() == 2,
          "other package files are a segment after the .dat files: any .dat wins over them");
    Check(!order.AddFolder(folder + "/missing", &error) && !error.empty(), "missing folder reported");

    // More files than the loader keeps open: every one still reads.
    for (uint32_t i = 0; i < 100; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "many/%03u.dat", i);
        WriteFile(root / name, BuildPackage({{{6, 6, i}, Str(name)}}));
    }
    LoadOrder many;
    bool all = many.AddFolder(folder, &error);
    Bytes out;
    for (uint32_t pass = 0; pass < 2; ++pass)
        for (uint32_t i = 0; i < 100; ++i) {
            char name[32];
            std::snprintf(name, sizeof name, "many/%03u.dat", i);
            all = all && many.Read({6, 6, i}, out) && out == Str(name);
        }
    Check(all, "reads across more files than stay open");
    fs::remove_all(root, ec);
}

} // namespace

int main()
{
    TestQfs();
    TestPackage();
    TestFolderOrder();
    TestLoadOrder();
    TestPluginFolder();
    TestPathList();
    if (g_failures) {
        std::printf("%d failures\n", g_failures);
        return 1;
    }
    std::printf("dbpf test OK\n");
    return 0;
}
