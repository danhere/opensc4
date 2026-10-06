// diagnostics.dbpf_survey: logs the resource manager's DB segments in lookup
// order once startup is done (P3 survey, docs/resources.md): each segment's
// path and record count, the package files inside folder segments, how many
// of a file's keys an earlier-listed file also has, and, for one such key per
// file, which file the resource manager actually serves it from (the listing
// order is not assumed to be the lookup order). Read-only, through the public
// interfaces.
//
// diagnostics.dbpf_compare: reads every record of every package file both
// through the game's segment and through OpenSC4's reader (src/dbpf) and
// compares the bytes.
#include "core/address_check.h"
#include "core/crash.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "dbpf/dbpf.h"

#include <GZServPtrs.h>
#include <cGZPersistResourceKey.h>
#include <cIGZCOM.h>
#include <cIGZFrameWork.h>
#include <cIGZPersistDBSegment.h>
#include <cIGZPersistDBSegmentMultiPackedFiles.h>
#include <cIGZPersistResourceKeyList.h>
#include <cIGZPersistResourceManager.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opensc4::dbpf_survey {
namespace {

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
using KeySet = std::unordered_set<cGZPersistResourceKey, KeyHash, KeyEq>;

cIGZPersistResourceKeyList* NewKeyList()
{
    cIGZFrameWork* framework = RZGetFrameWork();
    cIGZCOM* com = framework ? framework->GetCOMObject() : nullptr;
    cIGZPersistResourceKeyList* list = nullptr;
    if (!com || !com->GetClassObject(GZCLSID_cIGZPersistResourceKeyList, GZIID_cIGZPersistResourceKeyList,
                                     reinterpret_cast<void**>(&list)))
        return nullptr;
    return list;
}

std::string PathOf(cIGZPersistDBSegment* segment)
{
    cRZBaseString path;
    segment->GetPath(path);
    return path.ToChar() ? path.ToChar() : "";
}

struct Totals {
    uint32_t segments = 0;
    uint64_t records = 0;
    uint64_t duplicates = 0; // records whose key an earlier-listed file also has
    std::vector<std::pair<cGZPersistResourceKey, std::string>> samples; // one duplicate per file
};

// Adds a leaf segment's keys to `seen`, counting the ones already there.
void Survey(cIGZPersistDBSegment* segment, int depth, KeySet& seen, Totals& totals)
{
    cIGZPersistDBSegmentMultiPackedFiles* multi = nullptr;
    if (segment->QueryInterface(GZIID_cIGZPersistDBSegmentMultiPackedFiles, reinterpret_cast<void**>(&multi)) &&
        multi) {
        const uint32_t count = multi->GetSegmentCount();
        log::Write("dbpf: %*sfolder %s: %u files", depth * 2, "", PathOf(segment).c_str(), count);
        for (uint32_t i = 0; i < count; ++i)
            if (cIGZPersistDBSegment* child = multi->GetSegmentByIndex(i))
                Survey(child, depth + 1, seen, totals);
        multi->Release();
        return;
    }
    uint32_t records = 0, duplicates = 0;
    if (cIGZPersistResourceKeyList* list = NewKeyList()) {
        segment->GetResourceKeyList(list, nullptr);
        records = list->Size();
        for (uint32_t i = 0; i < records; ++i)
            if (!seen.insert(list->GetKey(i)).second && duplicates++ == 0)
                totals.samples.emplace_back(list->GetKey(i), PathOf(segment));
        list->Release();
    }
    ++totals.segments;
    totals.records += records;
    totals.duplicates += duplicates;
    log::Write("dbpf: %*s%s: %u records, %u also in an earlier-listed file", depth * 2, "", PathOf(segment).c_str(),
               records, duplicates);
}

bool Run(registry::Mode)
{
    cIGZPersistResourceManagerPtr manager;
    if (!manager) {
        log::Write("dbpf: no resource manager");
        return false;
    }
    const ULONGLONG start = GetTickCount64();
    const uint32_t count = manager->GetSegmentCount();
    log::Write("dbpf: %u segments in lookup order", count);
    KeySet seen;
    Totals totals;
    for (uint32_t i = 0; i < count; ++i) {
        cIGZPersistDBSegment* segment = manager->GetSegmentByIndex(i);
        if (!segment)
            continue;
        log::Write("dbpf: [%u] id %08X", i, segment->GetSegmentID());
        Survey(segment, 1, seen, totals);
    }
    log::Write("dbpf: %u package files, %llu records, %llu distinct keys, %llu duplicates (surveyed in %llu ms)",
               totals.segments, totals.records, static_cast<unsigned long long>(seen.size()), totals.duplicates,
               GetTickCount64() - start);
    // Which file wins: ask the manager, then the folder segment it names. Not
    // known whether FindDBSegment adds a reference, so none is released (a
    // leak in a diagnostic run, never a premature free).
    for (const auto& [key, later] : totals.samples) {
        cIGZPersistDBSegment* winner = nullptr;
        std::string served = "none";
        if (manager->FindDBSegment(key, &winner) && winner) {
            served = PathOf(winner);
            cIGZPersistDBSegmentMultiPackedFiles* multi = nullptr;
            if (winner->QueryInterface(GZIID_cIGZPersistDBSegmentMultiPackedFiles, reinterpret_cast<void**>(&multi)) &&
                multi) {
                cIGZPersistDBSegment* leaf = nullptr;
                if (multi->FindDBSegment(key, &leaf) && leaf)
                    served = PathOf(leaf);
                multi->Release();
            }
        }
        log::Write("dbpf: key %08X/%08X/%08X (duplicated in %s) is served from %s", key.type, key.group, key.instance,
                   later.c_str(), served.c_str());
    }
    return true;
}

// Calls `leaf` for every package file, in listing order, folders expanded.
void ForEachLeaf(cIGZPersistDBSegment* segment, const std::function<void(cIGZPersistDBSegment*)>& leaf)
{
    cIGZPersistDBSegmentMultiPackedFiles* multi = nullptr;
    if (segment->QueryInterface(GZIID_cIGZPersistDBSegmentMultiPackedFiles, reinterpret_cast<void**>(&multi)) &&
        multi) {
        const uint32_t count = multi->GetSegmentCount();
        for (uint32_t i = 0; i < count; ++i)
            if (cIGZPersistDBSegment* child = multi->GetSegmentByIndex(i))
                ForEachLeaf(child, leaf);
        multi->Release();
        return;
    }
    leaf(segment);
}

// One package file: every key the game lists, read both ways.
void CompareFile(cIGZPersistDBSegment* segment, uint64_t& totalRecords, uint64_t& totalBytes, uint32_t& badFiles)
{
    const std::string path = PathOf(segment);
    std::string error;
    // Game paths are in the ANSI code page; the reader takes UTF-8. The same
    // for ASCII paths, which is all a clean install has.
    auto package = dbpf::Package::OpenFile(path, &error);
    if (!package) {
        log::Write("dbpf compare: %s: our reader cannot open it: %s", path.c_str(), error.c_str());
        ++badFiles;
        return;
    }
    cIGZPersistResourceKeyList* list = NewKeyList();
    if (!list)
        return;
    segment->GetResourceKeyList(list, nullptr);
    const uint32_t count = list->Size();
    uint32_t missing = 0, sizeDiffers = 0, bytesDiffer = 0, reported = 0;
    std::vector<uint8_t> theirs, ours;
    for (uint32_t i = 0; i < count; ++i) {
        const cGZPersistResourceKey& key = list->GetKey(i);
        const dbpf::Entry* entry = package->Find({key.type, key.group, key.instance});
        uint32_t size = segment->GetRecordSize(key);
        theirs.assign(size, 0);
        const uint32_t read = segment->ReadRecord(key, theirs.data(), size);
        theirs.resize(size);
        const char* problem = nullptr;
        if (!entry) {
            ++missing;
            problem = "not in our index";
        } else if (!package->Read(*entry, ours, &error)) {
            ++bytesDiffer;
            problem = error.c_str();
        } else if (ours.size() != theirs.size()) {
            ++sizeDiffers;
            problem = "sizes differ";
        } else if (ours != theirs) {
            ++bytesDiffer;
            problem = "bytes differ";
        } else {
            totalBytes += ours.size();
        }
        if (problem && reported++ < 3)
            log::Write("dbpf compare:   %08X/%08X/%08X: %s (game %u bytes, read returned %u; ours %zu)", key.type,
                       key.group, key.instance, problem, size, read, ours.size());
    }
    list->Release();
    totalRecords += count;
    const uint32_t ourCount = static_cast<uint32_t>(package->Entries().size()) -
                              (package->Find(dbpf::kDirectoryKey) ? 1u : 0u);
    const bool ok = !missing && !sizeDiffers && !bytesDiffer && ourCount == count;
    badFiles += !ok;
    log::Write("dbpf compare: %s %s: %u records (ours %u): %u missing, %u sizes differ, %u contents differ",
               ok ? "same" : "DIFFERENT", path.c_str(), count, ourCount, missing, sizeDiffers, bytesDiffer);
}

bool Compare(registry::Mode)
{
    cIGZPersistResourceManagerPtr manager;
    if (!manager)
        return false;
    const ULONGLONG start = GetTickCount64();
    uint64_t records = 0, bytes = 0;
    uint32_t files = 0, badFiles = 0;
    const uint32_t count = manager->GetSegmentCount();
    for (uint32_t i = 0; i < count; ++i)
        if (cIGZPersistDBSegment* segment = manager->GetSegmentByIndex(i))
            ForEachLeaf(segment, [&](cIGZPersistDBSegment* leaf) {
                ++files;
                CompareFile(leaf, records, bytes, badFiles);
            });
    log::Write("dbpf compare: %u of %u files identical to the game's reading; %llu records, %llu bytes compared in "
               "%llu ms",
               files - badFiles, files, records, bytes, GetTickCount64() - start);
    return true;
}

const bool g_compareRegistered = registry::Register({
    "diagnostics.dbpf_compare",
    "diagnostics",
    registry::Kind::Api,
    registry::Phase::LateInit,
    0,
    "Reads every record through the game and through OpenSC4's DBPF reader and compares them",
    false,
    Compare,
});

// diagnostics.trace.setup_resources: how long startup spends building the
// segments (cSC4App::SetupResources).
using SetupFn = bool(__fastcall*)(void* app, void* edx);
SetupFn g_setup = nullptr;

bool SetupBody(void* app, void* edx)
{
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const bool ok = g_setup(app, edx);
    QueryPerformanceCounter(&end);
    log::Write("trace: cSC4App::SetupResources returned %s after %.1f ms", ok ? "true" : "false",
               (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart);
    return ok;
}

bool __fastcall Setup(void* app, void* edx)
{
    __try {
        return SetupBody(app, edx);
    } __except (crash::HookFilter("cSC4App::SetupResources", GetExceptionInformation())) {
        return false;
    }
}

bool ApplySetupTrace(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4App_SetupResources);
    return entry && hook::Install(*entry, &Setup, &g_setup);
}

const bool g_traceRegistered = registry::Register({
    "diagnostics.trace.setup_resources",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::DirectorInit,
    addr::cSC4App_SetupResources,
    "Logs how long startup spends building the resource manager's DB segments",
    false,
    ApplySetupTrace,
});

const bool g_registered = registry::Register({
    "diagnostics.dbpf_survey",
    "diagnostics",
    registry::Kind::Api,
    registry::Phase::LateInit,
    0,
    "Logs the resource manager's DB segments in lookup order, with record and override counts",
    false,
    Run,
});

} // namespace
} // namespace opensc4::dbpf_survey
