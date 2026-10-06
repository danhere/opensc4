// opensc4-dbpf: inspect DBPF packages with OpenSC4's reader.
//
//   opensc4-dbpf list <package>        header and every record
//   opensc4-dbpf check <package...>    reads and decompresses every record;
//                                      exit 1 if any fails
//   opensc4-dbpf diff <a> <b>          compares the records' contents by key;
//                                      exit 1 if anything differs
#include "dbpf/dbpf.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace opensc4::dbpf;

namespace {

int List(const char* path)
{
    std::string error;
    auto package = Package::OpenFile(path, &error);
    if (!package) {
        std::printf("%s: %s\n", path, error.c_str());
        return 1;
    }
    const Header& h = package->GetHeader();
    std::printf("%s: DBPF %u.%u, index %u, %u records, index at %u (%u bytes), %u holes\n", path, h.majorVersion,
                h.minorVersion, h.indexMajorVersion, h.indexCount, h.indexOffset, h.indexSize, h.holeCount);
    for (const Entry& e : package->Entries())
        std::printf("%08X %08X %08X  offset %10u  size %9u%s\n", e.key.type, e.key.group, e.key.instance, e.offset,
                    e.size, e.compressed ? ("  compressed, " + std::to_string(e.uncompressedSize)).c_str() : "");
    return 0;
}

int Check(int count, char** paths)
{
    int failedFiles = 0;
    uint64_t records = 0, compressed = 0, stored = 0, expanded = 0;
    for (int i = 0; i < count; ++i) {
        std::string error;
        auto package = Package::OpenFile(paths[i], &error);
        if (!package) {
            std::printf("FAIL %s: %s\n", paths[i], error.c_str());
            ++failedFiles;
            continue;
        }
        uint32_t bad = 0, packed = 0;
        std::vector<uint8_t> data;
        for (const Entry& e : package->Entries()) {
            if (!package->Read(e, data, &error)) {
                if (bad++ < 5)
                    std::printf("  %08X %08X %08X: %s\n", e.key.type, e.key.group, e.key.instance, error.c_str());
                continue;
            }
            packed += e.compressed;
            stored += e.size;
            expanded += data.size();
        }
        records += package->Entries().size();
        compressed += packed;
        std::printf("%s %s: %zu records, %u compressed%s\n", bad ? "FAIL" : "ok  ", paths[i],
                    package->Entries().size(), packed, bad ? (", " + std::to_string(bad) + " unreadable").c_str() : "");
        failedFiles += bad != 0;
    }
    std::printf("%d of %d files read cleanly: %" PRIu64 " records (%" PRIu64 " compressed), %" PRIu64
                " bytes stored, %" PRIu64 " bytes read\n",
                count - failedFiles, count, records, compressed, stored, expanded);
    return failedFiles ? 1 : 0;
}

// Records whose contents differ, grouped by type.
int Diff(const char* pathA, const char* pathB)
{
    std::string error;
    auto a = Package::OpenFile(pathA, &error);
    if (!a) {
        std::printf("%s: %s\n", pathA, error.c_str());
        return 2;
    }
    auto b = Package::OpenFile(pathB, &error);
    if (!b) {
        std::printf("%s: %s\n", pathB, error.c_str());
        return 2;
    }
    struct TypeCount {
        uint32_t same = 0, differ = 0, onlyA = 0, onlyB = 0;
    };
    std::map<uint32_t, TypeCount> byType;
    uint32_t same = 0, differ = 0, onlyA = 0, onlyB = 0;
    std::vector<uint8_t> da, db;
    for (const Entry& e : a->Entries()) {
        if (e.key == kDirectoryKey)
            continue; // compression bookkeeping, not content
        TypeCount& t = byType[e.key.type];
        const Entry* other = b->Find(e.key);
        if (!other) {
            ++onlyA, ++t.onlyA;
            std::printf("only in A  %08X %08X %08X  %u bytes\n", e.key.type, e.key.group, e.key.instance,
                        e.uncompressedSize);
            continue;
        }
        if (!a->Read(e, da, &error) || !b->Read(*other, db, &error)) {
            std::printf("unreadable %08X %08X %08X: %s\n", e.key.type, e.key.group, e.key.instance, error.c_str());
            ++differ, ++t.differ;
            continue;
        }
        if (da == db) {
            ++same, ++t.same;
            continue;
        }
        ++differ, ++t.differ;
        size_t first = 0, changed = 0;
        const size_t common = da.size() < db.size() ? da.size() : db.size();
        while (first < common && da[first] == db[first])
            ++first;
        for (size_t i = 0; i < common; ++i)
            changed += da[i] != db[i];
        std::printf("differs    %08X %08X %08X  %zu vs %zu bytes, %zu of the common bytes differ, first at %zu\n",
                    e.key.type, e.key.group, e.key.instance, da.size(), db.size(), changed, first);
    }
    for (const Entry& e : b->Entries())
        if (!(e.key == kDirectoryKey) && !a->Find(e.key)) {
            ++onlyB, ++byType[e.key.type].onlyB;
            std::printf("only in B  %08X %08X %08X  %u bytes\n", e.key.type, e.key.group, e.key.instance,
                        e.uncompressedSize);
        }
    std::printf("\nby type (same / differ / only A / only B):\n");
    for (const auto& [type, t] : byType)
        if (t.differ || t.onlyA || t.onlyB)
            std::printf("  %08X  %u / %u / %u / %u\n", type, t.same, t.differ, t.onlyA, t.onlyB);
    std::printf("%u same, %u differ, %u only in A, %u only in B\n", same, differ, onlyA, onlyB);
    return differ || onlyA || onlyB ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "list") == 0)
        return List(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "check") == 0)
        return Check(argc - 2, argv + 2);
    if (argc == 4 && std::strcmp(argv[1], "diff") == 0)
        return Diff(argv[2], argv[3]);
    std::printf("usage: opensc4-dbpf list <package> | check <package...> | diff <a> <b>\n");
    return 2;
}
