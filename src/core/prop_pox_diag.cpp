// diagnostics.prop_pox: a log-only look at the two suspects behind the
// "prop pox" (docs/crashes.md): class streams that append to a record that is
// still in the segment, and the 2,000,000-byte spill that writes at the
// buffer's end instead of the position. It also logs what the load made of
// each class. Every detour calls the original with its arguments unchanged
// and returns its result; the game's memory is only read.
//
//   StreamOpen 0x005D2410           one line per open: key, fromStart, result,
//                                   the record's size right after the open;
//                                   "APPEND onto existing record" when it
//                                   opened for append (false) onto data
//   WriteRecordSetFieldVoid 0x00970F0F  the spill condition is evaluated
//                                   before the original; only a spill logs
//   DeleteRecord 0x00971A55         "REFUSED" when the key is still in the
//                                   open map; calls for group 0x299B2D1B (the
//                                   class object records) are logged
//   LoadClassObjects 0x005D38D0     after the load, one line per class with
//                                   the index's count and the registered one
//
// Left out, because they need hooks inside LoadClassObjects: the per-object
// reasons for a loss (CRC mismatch, size mismatch, exception in Read,
// repeated object ID).
#include "core/address_check.h"
#include "core/crash.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/prop_pox_diag.h"
#include "core/registry.h"

#include <windows.h>

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::prop_pox_diag {
namespace {

struct Key {
    uint32_t type, group, instance;
};

// The stream (build 641): the record it opened is at +8; the record's vtable
// has GetSize at +0xD0 (docs/crashes.md).
constexpr std::size_t kStreamRecord = 0x08;
constexpr std::size_t kRecordGetSize = 0xD0;
// The write record: flags word (0 once closed), key, memory buffer, position,
// temp file and the spilled byte.
constexpr std::size_t kWriteFlags = 0x0A;
constexpr std::size_t kWriteKey = 0x0C;
constexpr std::size_t kWriteBufferBegin = 0x44;
constexpr std::size_t kWriteBufferEnd = 0x48;
constexpr std::size_t kWritePosition = 0x50;
constexpr std::size_t kWriteTempFile = 0x54;
constexpr std::size_t kWriteSpilled = 0x58;
constexpr uint32_t kSpillLimit = 2000000;
// The segment, from the interface pointer DeleteRecord gets.
constexpr std::size_t kSegmentState = 0x18;
constexpr std::size_t kSegmentRecords = 0xD4; // multimap {header*, count}
constexpr uint32_t kClassObjectGroup = 0x299B2D1B;
// The serializer: state, and the class map {header*, count} of nodes with
// the class ID at +0x10, the index's count at +0x14, the registered at +0x18.
constexpr std::size_t kSerializerState = 0x20;
constexpr std::size_t kSerializerClasses = 0x14;
constexpr std::size_t kNodeParent = 0x04, kNodeLeft = 0x08, kNodeRight = 0x0C;
constexpr std::size_t kNodeClass = 0x10, kNodeExpected = 0x14, kNodeRegistered = 0x18;
constexpr uint32_t kMaxVisits = 4096;
constexpr uint32_t kMaxDepth = 64;

using StreamOpenFn = bool(__fastcall*)(void* self, void* edx, void* segment, const Key* key, uint32_t fromStart);
using SetFieldVoidFn = bool(__fastcall*)(void* self, void* edx, const void* data, uint32_t len);
using DeleteRecordFn = bool(__fastcall*)(void* segment, void* edx, const Key* key);
using LoadClassObjectsFn = bool(__fastcall*)(void* self, void* edx);
using GetSizeFn = uint32_t(__fastcall*)(void* record, void* edx);
using FindFn = void*(__fastcall*)(void* map, void* edx, const Key* key);

StreamOpenFn g_streamOpen = nullptr;
SetFieldVoidFn g_setFieldVoid = nullptr;
DeleteRecordFn g_deleteRecord = nullptr;
LoadClassObjectsFn g_loadClassObjects = nullptr;

volatile LONG g_opens = 0, g_appends = 0;
volatile LONG g_spills = 0, g_spillsUnequal = 0;
volatile LONG g_deletes = 0, g_refusals = 0;
volatile LONG g_loads = 0, g_classes = 0, g_lostClasses = 0;
bool g_active = false;

// The record's size right after an open; 0xFFFFFFFF if it cannot be read.
uint32_t RecordSize(void* record)
{
    __try {
        void* const vtable = At<void*>(record, 0);
        const auto getSize = reinterpret_cast<GetSizeFn>(At<void*>(vtable, kRecordGetSize));
        return getSize(record, nullptr);
    } __except (crash::HookFilter("diagnostics.prop_pox", GetExceptionInformation())) {
        return 0xFFFFFFFFu;
    }
}

void LogStreamOpen(void* self, const Key* key, uint32_t fromStart, bool result)
{
    __try {
        uint32_t size = 0;
        if (result) {
            void* record = At<void*>(self, kStreamRecord);
            size = record ? RecordSize(record) : 0xFFFFFFFFu;
        }
        InterlockedIncrement(&g_opens);
        log::Write("prop_pox: open %08X-%08X-%08X fromStart=%u result=%d size=%u", key->type, key->group,
                   key->instance, fromStart & 0xFF, result ? 1 : 0, size);
        if (result && !(fromStart & 0xFF) && size != 0) {
            InterlockedIncrement(&g_appends);
            log::Write("prop_pox: APPEND onto existing record %08X-%08X-%08X (size %u)", key->type, key->group,
                       key->instance, size);
        }
    } __except (crash::HookFilter("diagnostics.prop_pox", GetExceptionInformation())) {
    }
}

bool __fastcall StreamOpen(void* self, void* edx, void* segment, const Key* key, uint32_t fromStart)
{
    const bool result = g_streamOpen(self, edx, segment, key, fromStart);
    if (key)
        LogStreamOpen(self, key, fromStart, result);
    return result;
}

bool __fastcall SetFieldVoid(void* self, void* edx, const void* data, uint32_t len)
{
    // The original's condition for moving the buffer to a temp file. A few
    // field reads on the normal path.
    if (At<uint16_t>(self, kWriteFlags) != 0 && At<uint8_t>(self, kWriteSpilled) == 0 &&
        At<void*>(self, kWriteTempFile) == nullptr &&
        static_cast<int32_t>(At<uint32_t>(self, kWritePosition) + len) >= static_cast<int32_t>(kSpillLimit)) {
        const uint32_t position = At<uint32_t>(self, kWritePosition);
        const uint32_t size = static_cast<uint32_t>(At<uintptr_t>(self, kWriteBufferEnd) -
                                                    At<uintptr_t>(self, kWriteBufferBegin));
        const Key* key = reinterpret_cast<const Key*>(static_cast<char*>(self) + kWriteKey);
        InterlockedIncrement(&g_spills);
        if (position != size) {
            InterlockedIncrement(&g_spillsUnequal);
            log::Write("prop_pox: SPILL with position != size: %08X-%08X-%08X position=%u bufferSize=%u len=%u",
                       key->type, key->group, key->instance, position, size, len);
        } else {
            log::Write("prop_pox: spill at the end: %08X-%08X-%08X position=%u bufferSize=%u len=%u", key->type,
                       key->group, key->instance, position, size, len);
        }
    }
    return g_setFieldVoid(self, edx, data, len);
}

void LogDeleteBefore(void* segment, const Key* key)
{
    __try {
        const uint32_t flags = At<uint32_t>(segment, kSegmentState);
        if ((flags & 3) == 0 || (flags & 2) == 0)
            return;
        void* map = static_cast<char*>(segment) + kSegmentRecords;
        const auto find = reinterpret_cast<FindFn>(addr::cGZDBSegmentPackedFile_RecordMapFind);
        if (find(map, nullptr, key) != At<void*>(map, 0)) {
            InterlockedIncrement(&g_refusals);
            log::Write("prop_pox: DeleteRecord REFUSED, key still open: %08X-%08X-%08X", key->type, key->group,
                       key->instance);
        }
    } __except (crash::HookFilter("diagnostics.prop_pox", GetExceptionInformation())) {
    }
}

bool __fastcall DeleteRecord(void* segment, void* edx, const Key* key)
{
    InterlockedIncrement(&g_deletes);
    if (key)
        LogDeleteBefore(segment, key);
    const bool result = g_deleteRecord(segment, edx, key);
    if (key && key->group == kClassObjectGroup)
        log::Write("prop_pox: DeleteRecord %08X-%08X-%08X result=%d", key->type, key->group, key->instance,
                   result ? 1 : 0);
    return result;
}

// In-order walk of the class map's tree (explicit stack, capped).
void WalkClasses(void* self)
{
    void* const header = At<void*>(self, kSerializerClasses);
    const uint32_t count = At<uint32_t>(self, kSerializerClasses + 4);
    log::Write("prop_pox: load: %u classes in the map", count);
    if (!header)
        return;
    void* stack[kMaxDepth];
    uint32_t depth = 0, visits = 0;
    void* node = At<void*>(header, kNodeParent); // the root; null when the tree is empty
    while ((node || depth) && visits < kMaxVisits) {
        while (node && depth < kMaxDepth) {
            stack[depth++] = node;
            node = At<void*>(node, kNodeLeft);
        }
        if (!depth)
            break;
        node = stack[--depth];
        ++visits;
        const uint32_t id = At<uint32_t>(node, kNodeClass);
        const uint32_t expected = At<uint32_t>(node, kNodeExpected);
        const uint32_t registered = At<uint32_t>(node, kNodeRegistered);
        InterlockedIncrement(&g_classes);
        if (expected != registered) {
            InterlockedIncrement(&g_lostClasses);
            log::Write("prop_pox: class LOST objects: %08X expected=%u registered=%u", id, expected, registered);
        } else {
            log::Write("prop_pox: class %08X expected=%u registered=%u", id, expected, registered);
        }
        node = At<void*>(node, kNodeRight);
    }
}

bool __fastcall LoadClassObjects(void* self, void* edx)
{
    // The original returns early unless the state is 1.
    const uint32_t state = At<uint32_t>(self, kSerializerState);
    const bool result = g_loadClassObjects(self, edx);
    InterlockedIncrement(&g_loads);
    log::Write("prop_pox: LoadClassObjects result=%d state=%u", result ? 1 : 0, state);
    if (state == 1) {
        __try {
            WalkClasses(self);
        } __except (crash::HookFilter("diagnostics.prop_pox", GetExceptionInformation())) {
        }
    } else {
        log::Write("prop_pox: LoadClassObjects returned early (state is not 1)");
    }
    return result;
}

bool Apply(registry::Mode)
{
    const addr::Entry* open = addr_check::Find(addr::cSC4COMSerializer_StreamOpen);
    const addr::Entry* write = addr_check::Find(addr::cGZDBSegmentPackedFile_WriteRecordSetFieldVoid);
    const addr::Entry* del = addr_check::Find(addr::cGZDBSegmentPackedFile_DeleteRecord);
    const addr::Entry* load = addr_check::Find(addr::cSC4COMSerializer_LoadClassObjects);
    if (!open || !write || !del || !load)
        return false;
    const bool ok = hook::Install(*open, &StreamOpen, &g_streamOpen) &&
                    hook::Install(*write, &SetFieldVoid, &g_setFieldVoid) &&
                    hook::Install(*del, &DeleteRecord, &g_deleteRecord) &&
                    hook::Install(*load, &LoadClassObjects, &g_loadClassObjects);
    if (ok) {
        g_active = true;
        log::Write("prop_pox: hooks installed: StreamOpen 0x005D2410, WriteRecordSetFieldVoid 0x00970F0F, "
                   "DeleteRecord 0x00971A55, LoadClassObjects 0x005D38D0");
    }
    return ok;
}

const bool g_registered = registry::Register({
    "diagnostics.prop_pox",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4COMSerializer_StreamOpen,
    "Logs the save's class stream opens, the 2,000,000-byte spill, DeleteRecord refusals and the load's class counts (docs/crashes.md, Prop pox)",
    false,
    Apply,
});

} // namespace

void LogSummary()
{
    if (!g_active)
        return;
    log::Write("prop_pox: summary: opens=%ld appends=%ld spills=%ld spillsWithPositionNotSize=%ld "
               "deleteCalls=%ld deleteRefused=%ld loads=%ld classes=%ld classesLost=%ld",
               g_opens, g_appends, g_spills, g_spillsUnequal, g_deletes, g_refusals, g_loads, g_classes,
               g_lostClasses);
}

} // namespace opensc4::prop_pox_diag
