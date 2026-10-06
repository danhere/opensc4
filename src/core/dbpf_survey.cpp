// diagnostics.dbpf_survey: logs the resource manager's DB segments in lookup
// order once startup is done (P3 survey, docs/resources.md): each segment's
// path and record count, the package files inside folder segments, how many
// of a file's keys an earlier-listed file also has, and, for one such key per
// file, which file the resource manager actually serves it from (the listing
// order is not assumed to be the lookup order). Read-only, through the public
// interfaces.
#include "core/address_check.h"
#include "core/crash.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

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
