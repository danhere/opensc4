// resources.stale_record_fix: the game's rare crash at 0x0096DA20, fixed
// (docs/crashes.md). A packed-file segment (cGZDBSegmentPackedFile,
// 0x086D4A89) keeps the records it has open in a multimap from key to
// record. A read-open of a key that is already open shares the open record's
// data: the open (0x009701FD) takes the map's first node for the key and,
// for an in-memory record (type 2), builds a new record on the old one's
// buffer (0x0096D9D1), whose reference count it increments. Now and then that
// first node points at a record that is closed (its flags word 0) and has no
// buffer any more (+0x44 NULL, which only its Close at 0x0096DB72 leaves), so
// the increment writes through NULL. Close always erases the node under the
// segment's lock, so the node is most likely left over from a record that was
// destroyed (its pool slot keeps type, NULL buffer and size); either way it
// stands for nothing open.
//
// Before the original runs, every such node at the front of the key's range
// is erased, without touching the record it points at; the open then takes
// its normal path and reads the record from the file again. The public
// OpenRecord (0x009757A0) holds the segment's lock around this function, so
// the map is ours to change here.
#include "core/address_check.h"
#include "core/crash.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::stale_record_fix {
namespace {

struct Key {
    uint32_t type, group, instance;
};

// The record's fields (build 641, docs/crashes.md).
constexpr std::size_t kRecordFlags = 0x0A;  // uint16, access flags; 0 once closed
constexpr std::size_t kRecordType = 0x40;   // 1: read from the file, 2: in memory
constexpr std::size_t kRecordBuffer = 0x44; // type 2: shared {data*, count}
// The segment, from the interface pointer the open gets (object + 4).
constexpr std::size_t kSegmentState = 0x18; // low two bits: open
constexpr std::size_t kSegmentRecords = 0xD4; // multimap {header*, count}
// A map node: key at +0x10, record at +0x1C.
constexpr std::size_t kNodeRecord = 0x1C;

using OpenFn = bool(__fastcall*)(void* segment, void* edx, const Key* key, void** record, uint32_t flags);
using FindFn = void*(__fastcall*)(void* map, void* edx, const Key* key);
using EraseFn = void(__fastcall*)(void* map, void* edx, void* node);

OpenFn g_original = nullptr;

bool IsStale(void* record)
{
    return record && At<uint32_t>(record, kRecordType) == 2 && !At<void*>(record, kRecordBuffer) &&
           At<uint16_t>(record, kRecordFlags) == 0;
}

void EraseStale(void* segment, const Key* key)
{
    void* map = static_cast<char*>(segment) + kSegmentRecords;
    const auto find = reinterpret_cast<FindFn>(addr::cGZDBSegmentPackedFile_RecordMapFind);
    const auto erase = reinterpret_cast<EraseFn>(addr::cGZDBSegmentPackedFile_RecordMapErase);
    for (;;) {
        void* node = find(map, nullptr, key);
        if (node == At<void*>(map, 0)) // the header: no node for the key
            return;
        void* record = At<void*>(node, kNodeRecord);
        if (!IsStale(record))
            return;
        log::Write("stale_record_fix: dropped a closed record from the open map: %08X-%08X-%08X (record %p)",
                   key->type, key->group, key->instance, record);
        erase(map, nullptr, node);
    }
}

bool __fastcall Open(void* segment, void* edx, const Key* key, void** record, uint32_t flags)
{
    // Only the read-open path (flags bit 0 without bit 1) reuses an open record.
    if ((flags & 3) == 1 && (At<uint8_t>(segment, kSegmentState) & 3) && key) {
        __try {
            EraseStale(segment, key);
        } __except (crash::HookFilter("resources.stale_record_fix", GetExceptionInformation())) {
        }
    }
    return g_original(segment, edx, key, record, flags);
}

bool Apply(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cGZDBSegmentPackedFile_OpenRecordUnlocked);
    return entry && hook::Install(*entry, &Open, &g_original);
}

const bool g_registered = registry::Register({
    "resources.stale_record_fix",
    "resources",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZDBSegmentPackedFile_OpenRecordUnlocked,
    "Fixes the game's crash at 0x0096DA20: a read-open skips a closed record left in the segment's open map",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::stale_record_fix
