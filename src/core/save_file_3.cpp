// The packed file's write path, part 3: the header, Flush, the three tables
// a Flush writes (the directory of compressed records, the index, the hole
// table), compaction into a temporary file, and the copy and import calls.
// Fourth piece of the save code; the class is the framework's
// cGZDBSegmentPackedFile (0x1C8 bytes, constructor 0x0097212D), the DBPF
// container behind every saved city.
//
// The object has two vtables: the packed file's own at +0 (0x00AD87D0, 47
// slots; "P" below) and the segment part cIGZPersistDBSegment at +4
// (0x00AD8728, 41 slots; "S" = P + 4). Flush gets S as `this`, everything
// else here gets P.
//
// What a Flush (S slot 9, 0x00970193) writes, in order, each step only for a
// segment open for write and stopping at the first failure:
//
//   1. The directory of compressed records (0x0096FE20), when its dirty
//      byte P+0x184 is set: the old directory record is deleted (its bytes
//      become a hole), 16 bytes per entry of the map at P+0x1B0 (type,
//      group, instance, uncompressed size) are laid out in the map's own
//      order (bucket by bucket, each chain from its head), and written as
//      the record (0xE86B1EEF, 0xE86B1EEF, 0x286B1F03) like any other
//      record. An empty map still writes a record of no bytes.
//   2. Compaction (P slot 27, 0x00972233), when bit 0 of the hole flags
//      P+0x144 is set and 0x0096EF02 finds holes / (file length - holes)
//      above the ratio at P+0x148. Its answer is not looked at.
//   3. The index table (0x0096FB0F), when its dirty byte P+0x28 is set: 20
//      bytes per entry (type, group, instance, offset, size) in the index
//      map's order. The table keeps its place when its size did not change;
//      otherwise the old one is freed (a hole) and a new place allocated (a
//      hole that fits, or the file's end).
//   4. The hole table (0x0096EF83), when its dirty byte P+0x158 is set: 8
//      bytes per hole (offset, size) in the hole map's order, which is
//      ascending size.
//   5. The header (0x0096E37B): the 0x60 bytes kept at P+0xE4, with the
//      signature, major 1, minor 0, the user minor from P+0xD0 and a new
//      modified date from the C runtime's time, at the file's starting
//      offset.
//   6. The file's own Flush.
//
// Replacement, off by default:
//
//   saves.file.part3: the sixteen functions below.
//
// The object as far as this part touches it (offsets from P): +0x04 the
// segment part, +0x0C the lock (an object with Lock at slot 1 and Unlock at
// slot 2), +0x1C the open flags (1 read, 2 write), +0x24 the index map (a
// pointer; its count at +0x10), +0x28 index dirty, +0x2C the path (a
// string), +0x40 the file (cRZFile, in place), +0xC0 the file's starting
// offset, +0xCC and +0xD0 the next user version, +0xD8 the map of open
// records {header, count}, +0xE4 the header (0x60 bytes: +0x108 index count,
// +0x10C index offset, +0x110 index size, +0x114 hole count, +0x118 hole
// offset, +0x11C hole size), +0x144 hole use flags, +0x148 the compaction
// ratio (float), +0x14C the hole map {header, count}, +0x158 holes dirty,
// +0x159 the compaction guard, +0x184 directory dirty, +0x1B0 the map of
// compressed records (in place; its count at +0x1C0).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::saves {
namespace {

namespace packed { // offsets from P
constexpr std::size_t kSegment = 0x4;
constexpr std::size_t kLock = 0xC;
constexpr std::size_t kFlags = 0x1C;
constexpr std::size_t kIndexMap = 0x24;
constexpr std::size_t kIndexDirty = 0x28;
constexpr std::size_t kPath = 0x2C;
constexpr std::size_t kFile = 0x40;
constexpr std::size_t kStartOffset = 0xC0;
constexpr std::size_t kNextUserMajor = 0xCC;
constexpr std::size_t kNextUserMinor = 0xD0;
constexpr std::size_t kOpenMap = 0xD8;
constexpr std::size_t kOpenCount = 0xDC;
constexpr std::size_t kHeader = 0xE4;
constexpr std::size_t kHeaderMajor = 0xE8;
constexpr std::size_t kHeaderMinor = 0xEC;
constexpr std::size_t kHeaderUserMajor = 0xF0;
constexpr std::size_t kHeaderUserMinor = 0xF4;
constexpr std::size_t kHeaderFlags = 0xF8;
constexpr std::size_t kHeaderCreated = 0xFC;
constexpr std::size_t kHeaderModified = 0x100;
constexpr std::size_t kHeaderIndexMajor = 0x104;
constexpr std::size_t kIndexCount = 0x108;
constexpr std::size_t kIndexOffset = 0x10C;
constexpr std::size_t kIndexSize = 0x110;
constexpr std::size_t kHoleCount = 0x114;
constexpr std::size_t kHoleOffset = 0x118;
constexpr std::size_t kHoleSize = 0x11C;
constexpr std::size_t kHoleFlags = 0x144;
constexpr std::size_t kHoleRatio = 0x148;
constexpr std::size_t kHoleMap = 0x14C;
constexpr std::size_t kHolesInMap = 0x150;
constexpr std::size_t kHolesDirty = 0x158;
constexpr std::size_t kCompacting = 0x159;
constexpr std::size_t kDirectoryDirty = 0x184;
constexpr std::size_t kCompressedMap = 0x1B0;
constexpr std::size_t kCompressedCount = 0x1C0;
} // namespace packed

namespace pslot { // the packed file's vtable (P), by offset
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kInit = 0x10;
constexpr std::size_t kShutdown = 0x14;
constexpr std::size_t kSetPath = 0x24;           // (const cIGZString&)
constexpr std::size_t kSetHoleUseFlags = 0x38;   // (flags)
constexpr std::size_t kGetRecordSizeRaw = 0x48;  // (key): the stored size, -1 when absent
constexpr std::size_t kReadRecordRaw = 0x4C;     // (key, buffer, uint32& size)
constexpr std::size_t kWriteRecordRaw = 0x50;    // (key, buffer, size)
constexpr std::size_t kCopyRecords = 0x68;       // (segment, filter, fromOther, keepOriginal)
constexpr std::size_t kCompact = 0x6C;
constexpr std::size_t kGetCompressed = 0x80;     // (key)
constexpr std::size_t kSetCompressed = 0x84;     // (key, bool, uncompressed size)
constexpr std::size_t kAllocateSpace = 0x8C;     // (uint32& position, size)
constexpr std::size_t kFreeSpace = 0x90;         // (position, size)
constexpr std::size_t kWriteAt = 0xB8;           // (buffer, position, size)
} // namespace pslot

namespace sslot { // cIGZPersistDBSegment (S), by offset
constexpr std::size_t kOpen = 0x18;                // (read, write)
constexpr std::size_t kIsOpen = 0x1C;
constexpr std::size_t kClose = 0x20;
constexpr std::size_t kLock = 0x30;
constexpr std::size_t kUnlock = 0x34;
constexpr std::size_t kGetKeyList = 0x48;          // (list, filter)
constexpr std::size_t kTestForRecord = 0x4C;       // (key)
constexpr std::size_t kGetRecordSize = 0x50;       // (key)
constexpr std::size_t kCloseRecordHeld = 0x5C;     // (record**)
constexpr std::size_t kDeleteRecord = 0x6C;        // (key)
constexpr std::size_t kReadRecord = 0x70;          // (key, buffer, uint32& size)
constexpr std::size_t kWriteRecord = 0x74;         // (key, buffer, size)
constexpr std::size_t kDeleteRecordUnlocked = 0x98; // (key)
constexpr std::size_t kWriteRecordUnlocked = 0xA0; // (key, buffer, size)
} // namespace sslot

namespace file { // cRZFile's vtable (0x00ABFED0), by offset
constexpr std::size_t kIsOpen = 0x10;
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kSetLength = 0x20; // (length)
constexpr std::size_t kSeek = 0x30;      // (position): the position reached
constexpr std::size_t kWrite = 0x40;     // (buffer, size)
constexpr std::size_t kFlush = 0x48;
} // namespace file

constexpr uint32_t kPackedFileInterface = 0x286D4A6A;
constexpr uint32_t kDirectoryType = 0xE86B1EEF;
constexpr uint32_t kDirectoryGroup = 0xE86B1EEF;
constexpr uint32_t kDirectoryInstance = 0x286B1F03;
constexpr uint32_t kHeaderBytes = 0x60;
constexpr uint32_t kImportChunk = 0x4000;

struct Key {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

// One entry of the index, in memory and in the file.
struct IndexEntry {
    Key key;
    uint32_t offset;
    uint32_t size;
};
static_assert(sizeof(IndexEntry) == 0x14);

// The guard the game puts on the stack around a locked call.
struct LockGuard {
    std::uintptr_t vtable;
    void* lock;
};

// An iterator of the game's hash table (the index and the compressed map).
struct HashIterator {
    char* node;
    void* table;
};

// An iterator of the key list's deque of 12-byte keys.
struct KeyIterator {
    Key* current;
    Key* first;
    Key* last;
    Key** block;
};
static_assert(sizeof(KeyIterator) == 0x10);

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t references;
};
static_assert(sizeof(GameString) == 0x14);

// A node of the game's list of pointers.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void Lock(LockGuard& guard, char* self)
{
    This<void*>(addr::SC4LockGuard_Construct, &guard, static_cast<void*>(self + packed::kLock));
}

uint32_t Unlock(LockGuard& guard)
{
    return Virtual<uint32_t>(guard.lock, 0x8);
}

void* New(uint32_t bytes)
{
    return Cdecl<void*>(addr::SC4_operator_new, bytes);
}

void Delete(void* memory)
{
    Cdecl<void>(addr::SC4_operator_delete, memory);
}

// The next node of one of the game's trees.
char* TreeNext(char* node)
{
    return Cdecl<char*>(addr::cSC4Advisor_F40D4C0, node);
}

// The element constructor loop the compiler put after each table's
// allocation; the element constructor 0x0096D8B9 does nothing.
void ConstructElements(void* table, uint32_t elementSize, uint32_t count)
{
    reinterpret_cast<void(__stdcall*)(void*, uint32_t, uint32_t, std::uintptr_t)>(addr::SC4UI_F41D490)(
        table, elementSize, count, addr::cGZDBSegmentPackedFile_ConstructNothing);
}

void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

bool Exists(GameString& path)
{
    return Cdecl<uint8_t>(addr::RZFile_Exists, &path) != 0;
}

void Remove(GameString& path)
{
    Cdecl<uint8_t>(addr::RZFile_Remove, &path);
}

bool Move(void* from, void* to)
{
    return Cdecl<uint8_t>(addr::RZFile_Move, from, to) != 0;
}

bool OpenForWrite(char* self)
{
    const uint32_t flags = At<uint32_t>(self, packed::kFlags);
    return (flags & 3) != 0 && (flags & 2) != 0;
}

// The game's 0x0096E09A, P slot 12: SetNextUserVersion(major, minor). The
// values a new header takes; every header write takes the minor.
uint32_t __fastcall SetNextUserVersion(char* self, void*, uint32_t major, uint32_t minor)
{
    LockGuard guard;
    Lock(guard, self);
    At<uint32_t>(self, packed::kNextUserMajor) = major;
    At<uint32_t>(self, packed::kNextUserMinor) = minor;
    return Unlock(guard);
}

// The game's 0x0096E15F, P slot 14: SetHoleUseFlags(flags). Bit 0: compact
// at Flush when the ratio says so; bit 1: reuse holes when allocating.
uint8_t __fastcall SetHoleUseFlags(char* self, void*, uint32_t flags)
{
    LockGuard guard;
    Lock(guard, self);
    At<uint32_t>(self, packed::kHoleFlags) = flags;
    Unlock(guard);
    return 1;
}

// The game's 0x0096E18B, P slot 15: SetHoleCompactionMinimumRatio(float).
// The float is moved as four bytes, never loaded.
void __fastcall SetHoleCompactionMinimumRatio(char* self, void*, uint32_t ratioBits)
{
    At<uint32_t>(self, packed::kHoleRatio) = ratioBits;
}

// The game's 0x0096E37B: writes the header at the file's starting offset.
// The copy in memory is not changed: the signature, the two versions, the
// user minor and the modified date are set in a copy on the stack.
uint8_t __fastcall WriteHeader(char* self, void*)
{
    uint8_t result = 0;
    if (At<uint8_t>(self, packed::kFlags) & 2) {
        void* const stream = self + packed::kFile;
        if (Virtual<uint8_t>(stream, file::kIsOpen)) {
            const uint32_t start = At<uint32_t>(self, packed::kStartOffset);
            if (Virtual<uint32_t>(stream, file::kSeek, start) == At<uint32_t>(self, packed::kStartOffset)) {
                const uint32_t signature = Global<uint32_t>(addr::cGZDBSegmentPackedFile_kSignature);
                uint32_t header[kHeaderBytes / 4];
                for (std::size_t i = 0; i < kHeaderBytes / 4; ++i)
                    header[i] = At<uint32_t>(self, packed::kHeader + i * 4);
                header[2] = 0;
                header[0] = signature;
                const uint32_t userMinor = At<uint32_t>(self, packed::kNextUserMinor);
                header[1] = 1;
                header[4] = userMinor;
                header[7] = Cdecl<uint32_t>(addr::CRT_time32, static_cast<void*>(nullptr));
                if (Virtual<uint8_t>(stream, file::kWrite, static_cast<void*>(&header[0]), kHeaderBytes))
                    result = 1;
            }
        }
    }
    return result;
}

// The game's 0x0096E99A: a new header in memory (both dates from the C
// runtime's time, called twice), written out, and the file cut to the
// header's length. The answer is WriteHeader's when that failed, else the
// file's SetLength answer.
uint8_t __fastcall InitNewHeader(char* self, void*)
{
    Cdecl<void*>(addr::CRT_memset, static_cast<void*>(self + packed::kHeader), int32_t{0},
        kHeaderBytes);
    At<uint32_t>(self, packed::kHeader) = Global<uint32_t>(addr::cGZDBSegmentPackedFile_kSignature);
    At<uint32_t>(self, packed::kHeaderUserMajor) = At<uint32_t>(self, packed::kNextUserMajor);
    const uint32_t userMinor = At<uint32_t>(self, packed::kNextUserMinor);
    At<uint32_t>(self, packed::kHeaderMajor) = 1;
    At<uint32_t>(self, packed::kHeaderMinor) = 0;
    At<uint32_t>(self, packed::kHeaderUserMinor) = userMinor;
    At<uint32_t>(self, packed::kHeaderFlags) = 0;
    At<uint32_t>(self, packed::kHeaderCreated) = Cdecl<uint32_t>(addr::CRT_time32, static_cast<void*>(nullptr));
    At<uint32_t>(self, packed::kHeaderModified) = Cdecl<uint32_t>(addr::CRT_time32, static_cast<void*>(nullptr));
    At<uint32_t>(self, packed::kHeaderIndexMajor) = 7;
    At<uint32_t>(self, packed::kIndexCount) = 0;
    At<uint32_t>(self, packed::kIndexOffset) = 0;
    At<uint32_t>(self, packed::kIndexSize) = 0;
    At<uint32_t>(self, packed::kHoleCount) = 0;
    At<uint32_t>(self, packed::kHoleOffset) = 0;
    At<uint32_t>(self, packed::kHoleSize) = 0;
    const uint8_t written = WriteHeader(self, nullptr);
    if (!written)
        return written;
    return Virtual<uint8_t>(self + packed::kFile, file::kSetLength, kHeaderBytes);
}

// The game's 0x0096EF02: whether the holes are worth a compaction: their
// sizes summed, divided by the rest of the file's length, above the ratio.
uint8_t __fastcall ShouldCompact(char* self, void*)
{
    uint8_t result = 0;
    if (At<uint8_t>(self, packed::kFlags) & 3) {
        const uint32_t length = Virtual<uint32_t>(self + packed::kFile, file::kLength);
        char* const header = At<char*>(self, packed::kHoleMap);
        uint32_t holes = 0;
        for (char* node = At<char*>(header, 0x8); node != header; node = TreeNext(node))
            holes += At<uint32_t>(node, 0x14);
        if (holes < length) {
            // fild, fild, fdivp, fcomp, as in the rule for keeping a
            // compressed record (save_file_2.cpp, CompressIfWorthIt): the
            // quotient has the precision the thread's x87 runs with, so at
            // 24 bits it is a float before the compare. At the 53 bits the
            // game is measured to run with (docs/pollution.md, "Floating
            // point") this is the double division itself.
            double quotient = static_cast<double>(holes) / static_cast<double>(length - holes);
            if (Cdecl<int32_t>(addr::SC4Precision_Get) == 0)
                quotient = static_cast<double>(static_cast<float>(quotient));
            if (quotient > static_cast<double>(At<float>(self, packed::kHoleRatio)))
                result = 1;
        }
    }
    return result;
}

// The game's 0x0096EF83: the hole table. A change in the number of holes
// frees the old table and allocates a new one; either call can change the
// number again, so the count is read once more before the table is laid
// out, and the header takes that last count.
uint8_t __fastcall WriteHoleTable(char* self, void*)
{
    uint8_t result = 1;
    if (!OpenForWrite(self) || !At<uint8_t>(self, packed::kHolesDirty))
        return result;

    bool failed = false;
    if (At<uint32_t>(self, packed::kHolesInMap) != At<uint32_t>(self, packed::kHoleCount)) {
        result = Virtual<uint8_t>(self, pslot::kFreeSpace, At<uint32_t>(self, packed::kHoleOffset),
            At<uint32_t>(self, packed::kHoleSize));
        if (!result) {
            failed = true;
        } else {
            const uint32_t count = At<uint32_t>(self, packed::kHolesInMap);
            if (count) {
                const uint32_t bytes = count << 3;
                At<uint32_t>(self, packed::kHoleSize) = bytes;
                result = Virtual<uint8_t>(self, pslot::kAllocateSpace, &At<uint32_t>(self, packed::kHoleOffset), bytes);
            }
            if (!result)
                failed = true;
        }
    }

    if (failed) {
        At<uint32_t>(self, packed::kHoleOffset) = 0;
        At<uint32_t>(self, packed::kHoleCount) = 0;
        At<uint32_t>(self, packed::kHoleSize) = 0;
    } else {
        const uint32_t count = At<uint32_t>(self, packed::kHolesInMap);
        if (count == 0) {
            At<uint32_t>(self, packed::kHoleOffset) = 0;
            At<uint32_t>(self, packed::kHoleCount) = 0;
            At<uint32_t>(self, packed::kHoleSize) = 0;
        } else {
            At<uint32_t>(self, packed::kHoleSize) = count << 3;
            At<uint32_t>(self, packed::kHoleCount) = count;
            uint32_t* table = static_cast<uint32_t*>(New(count << 3));
            if (table)
                ConstructElements(table, 8, count);
            char* const header = At<char*>(self, packed::kHoleMap);
            char* node = At<char*>(header, 0x8);
            if (node != header) {
                uint32_t* out = table;
                do {
                    out[0] = At<uint32_t>(node, 0x10);
                    out[1] = At<uint32_t>(node, 0x14);
                    node = TreeNext(node);
                    out += 2;
                } while (node != header);
            }
            result = Virtual<uint8_t>(self, pslot::kWriteAt, static_cast<void*>(table),
                At<uint32_t>(self, packed::kHoleOffset), At<uint32_t>(self, packed::kHoleSize));
            Delete(table);
        }
    }
    At<uint8_t>(self, packed::kHolesDirty) = 0;
    return result;
}

// The game's 0x0096FA6B: closes every record still open. The records are
// first collected in a list, each counted once more (the record part at
// +4), because closing takes them out of the map; then the segment's
// CloseRecord(record**) runs on each, which also gives the count back.
uint8_t __fastcall CloseAllOpenRecords(char* self, void*)
{
    if (!(At<uint8_t>(self, packed::kFlags) & 3))
        return 0;
    if (At<uint32_t>(self, packed::kOpenCount) != 0) {
        uint8_t allocator;
        ListNode* list = nullptr;
        This<void*>(addr::cGZDBSegmentPackedFile_ListConstruct, &list, &allocator);

        char* const header = At<char*>(self, packed::kOpenMap);
        for (char* node = At<char*>(header, 0x8); node != header; node = TreeNext(node)) {
            char* const record = At<char*>(node, 0x1C);
            void* part = record ? record + 4 : nullptr;
            Virtual<uint32_t>(part, 0x4);
            ListNode* inserted;
            This<void*>(addr::cGZDBSegmentPackedFile_ListInsertBefore, &list, &inserted, list, &part);
        }

        ListNode* const head = list;
        ListNode* entry = head->next;
        if (entry != head) {
            void* const segment = self + packed::kSegment;
            do {
                void* held = entry->value;
                Virtual<uint8_t>(segment, sslot::kCloseRecordHeld, &held);
                entry = entry->next;
            } while (entry != head);
        }
        This<void>(addr::cGZDBSegmentPackedFile_ListDestruct, &list);
    }
    return 1;
}

// The game's 0x0096FB0F: the index table.
uint8_t __fastcall WriteIndex(char* self, void*)
{
    uint8_t result = 1;
    if (!OpenForWrite(self) || !At<uint8_t>(self, packed::kIndexDirty))
        return result;

    const uint32_t count = At<uint32_t>(At<void*>(self, packed::kIndexMap), 0x10);
    const uint32_t oldSize = At<uint32_t>(self, packed::kIndexSize);
    const uint32_t size = count * 20;
    uint32_t& offset = At<uint32_t>(self, packed::kIndexOffset);

    const auto clear = [&] {
        offset = 0;
        At<uint32_t>(self, packed::kIndexCount) = 0;
        At<uint32_t>(self, packed::kIndexSize) = 0;
    };

    bool write = true;
    if (size == 0) {
        result = Virtual<uint8_t>(self, pslot::kFreeSpace, offset, oldSize);
        clear();
        write = false;
    } else {
        At<uint32_t>(self, packed::kIndexCount) = count;
        At<uint32_t>(self, packed::kIndexSize) = size;
        if (size != oldSize) {
            result = Virtual<uint8_t>(self, pslot::kFreeSpace, offset, oldSize);
            if (!result) {
                write = false;
            } else {
                const uint32_t bytes = At<uint32_t>(self, packed::kIndexSize);
                offset = 0;
                result = Virtual<uint8_t>(self, pslot::kAllocateSpace, &offset, bytes);
                if (!result) {
                    clear();
                    write = false;
                }
            }
        }
    }

    if (write) {
        uint32_t* table = static_cast<uint32_t*>(New(size));
        if (table)
            ConstructElements(table, 0x14, count);

        HashIterator it;
        This<void>(addr::cGZDBSegmentPackedFile_KeyTableBegin, At<void*>(self, packed::kIndexMap), &it);
        if (it.node) {
            uint32_t* out = table;
            do {
                char* const node = it.node;
                out[0] = At<uint32_t>(node, 0x4);
                out[1] = At<uint32_t>(node, 0x8);
                out[2] = At<uint32_t>(node, 0xC);
                out[3] = At<uint32_t>(node, 0x10);
                out[4] = At<uint32_t>(node, 0x14);
                This<void*>(addr::cGZDBSegmentPackedFile_HashIteratorNext, &it);
                out += 5;
            } while (it.node);
        }

        result = Virtual<uint8_t>(self, pslot::kWriteAt, static_cast<void*>(table), offset,
            At<uint32_t>(self, packed::kIndexSize));
        Delete(table);
        if (!result) {
            const uint32_t place = offset;
            if (place != 0) {
                Virtual<uint8_t>(self, pslot::kFreeSpace, place, At<uint32_t>(self, packed::kIndexSize));
                At<uint32_t>(self, packed::kIndexCount) = 0;
                At<uint32_t>(self, packed::kIndexOffset) = 0;
                At<uint32_t>(self, packed::kIndexSize) = 0;
            }
        }
    }
    At<uint8_t>(self, packed::kIndexDirty) = 0;
    return result;
}

// The game's 0x0096FE20: the directory of compressed records, written as a
// record of its own through the segment's unlocked delete and write.
uint8_t __fastcall WriteCompressedDirectory(char* self, void*)
{
    uint8_t result = 1;
    if (!OpenForWrite(self) || !At<uint8_t>(self, packed::kDirectoryDirty))
        return result;

    Key key;
    key.type = kDirectoryType;
    key.group = kDirectoryGroup;
    void* const segment = self + packed::kSegment;
    key.instance = kDirectoryInstance;
    Virtual<uint8_t>(segment, sslot::kDeleteRecordUnlocked, &key);

    const uint32_t size = At<uint32_t>(self, packed::kCompressedCount) << 4;
    uint32_t* const table = static_cast<uint32_t*>(New(size));

    HashIterator it;
    This<void>(addr::cGZDBSegmentPackedFile_KeyTableBegin, static_cast<void*>(self + packed::kCompressedMap), &it);
    if (it.node) {
        uint32_t* out = table;
        do {
            char* const node = it.node;
            out[0] = At<uint32_t>(node, 0x4);
            out[1] = At<uint32_t>(node, 0x8);
            out[2] = At<uint32_t>(node, 0xC);
            out[3] = At<uint32_t>(node, 0x10);
            out += 4;
            This<void*>(addr::cGZDBSegmentPackedFile_HashIteratorNext, &it);
        } while (it.node);
    }

    result = Virtual<uint8_t>(segment, sslot::kWriteRecordUnlocked, &key, static_cast<void*>(table), size);
    if (result) {
        result = 1;
        At<uint8_t>(self, packed::kDirectoryDirty) = 0;
    }
    Delete(table);
    return result;
}

// The game's 0x00970193, S slot 9: Flush. `this` is the segment part. The
// last step is a jump to the file's Flush, whose answer is the answer.
uint8_t __fastcall Flush(char* segment, void*)
{
    const uint32_t flags = At<uint32_t>(segment, packed::kFlags - packed::kSegment);
    if (!(flags & 3) || !(flags & 2))
        return 1;
    char* const self = segment - packed::kSegment;
    if (!WriteCompressedDirectory(self, nullptr))
        return 0;
    if (At<uint8_t>(self, packed::kHoleFlags) & 1) {
        if (ShouldCompact(self, nullptr))
            Virtual<uint8_t>(self, pslot::kCompact);
    }
    if (!WriteIndex(self, nullptr))
        return 0;
    if (!WriteHoleTable(self, nullptr))
        return 0;
    if (!WriteHeader(self, nullptr))
        return 0;
    return Virtual<uint8_t>(self + packed::kFile, file::kFlush);
}

// One record moved as its stored bytes between two packed files, with its
// compressed mark: `from` and `to` are packed file parts, `sized` answers
// the uncompressed size as the original asks for it on each side.
template <typename UncompressedSize, typename DeleteOriginal>
bool CopyRaw(void* from, void* to, Key* key, UncompressedSize sized, DeleteOriginal deleteOriginal,
    uint8_t keepOriginal)
{
    bool copied = false;
    uint32_t rawSize = Virtual<uint32_t>(from, pslot::kGetRecordSizeRaw, key);
    const uint32_t fullSize = sized();
    if (rawSize != 0xFFFFFFFF) {
        void* const buffer = New(rawSize);
        if (Virtual<uint32_t>(from, pslot::kReadRecordRaw, key, buffer, &rawSize) != 0xFFFFFFFF) {
            if (Virtual<uint32_t>(to, pslot::kWriteRecordRaw, key, buffer, rawSize) != 0) {
                Virtual<uint8_t>(to, pslot::kSetCompressed, key, uint32_t{1}, fullSize);
                if (!keepOriginal)
                    deleteOriginal();
                copied = true;
            }
        }
        Delete(buffer);
    }
    return copied;
}

// The game's 0x009708BE: copies one record between this packed file and
// another segment (`other` is a cIGZPersistDBSegment), in the direction
// `fromOther` gives, and deletes the original unless told to keep it. When
// the other side is a packed file too and the record is compressed in its
// source, the stored bytes are copied as they are.
uint8_t __fastcall CopyOneRecord(char* self, void*, void* other, Key* key, uint32_t fromOtherArg,
    uint32_t keepOriginalArg)
{
    LockGuard guard;
    Lock(guard, self);
    const uint8_t fromOther = static_cast<uint8_t>(fromOtherArg);
    const uint8_t keepOriginal = static_cast<uint8_t>(keepOriginalArg);
    uint8_t result = 0;

    if (fromOther) {
        if (Virtual<uint8_t>(other, sslot::kTestForRecord, key)) {
            void* packedOther = nullptr;
            bool plain = true;
            if (Virtual<uint8_t>(other, 0x0, kPackedFileInterface, &packedOther)) {
                if (Virtual<uint8_t>(packedOther, pslot::kGetCompressed, key)) {
                    const bool copied = CopyRaw(
                        packedOther, self, key, [&] { return Virtual<uint32_t>(other, sslot::kGetRecordSize, key); },
                        [&] { Virtual<uint8_t>(other, sslot::kDeleteRecord, key); }, keepOriginal);
                    if (copied) {
                        result = 1;
                        plain = false;
                    }
                }
                Virtual<uint32_t>(packedOther, pslot::kRelease);
                packedOther = nullptr;
            }
            if (plain) {
                uint32_t size = Virtual<uint32_t>(other, sslot::kGetRecordSize, key);
                if (size != 0xFFFFFFFF) {
                    void* const buffer = New(size);
                    if (Virtual<uint32_t>(other, sslot::kReadRecord, key, buffer, &size) != 0) {
                        if (Virtual<uint8_t>(self + packed::kSegment, sslot::kWriteRecord, key, buffer, size)) {
                            if (!keepOriginal)
                                Virtual<uint8_t>(other, sslot::kDeleteRecord, key);
                            result = 1;
                        }
                    }
                    Delete(buffer);
                }
            }
        }
    } else {
        void* const segment = self + packed::kSegment;
        if (Virtual<uint8_t>(segment, sslot::kTestForRecord, key)) {
            void* packedOther = nullptr;
            bool plain = true;
            if (Virtual<uint8_t>(other, 0x0, kPackedFileInterface, &packedOther)) {
                if (Virtual<uint8_t>(self, pslot::kGetCompressed, key)) {
                    const bool copied = CopyRaw(
                        self, packedOther, key,
                        [&] { return This<uint32_t>(addr::cGZDBSegmentPackedFile_DirectorySize, self, key); },
                        [&] { Virtual<uint8_t>(segment, sslot::kDeleteRecord, key); }, keepOriginal);
                    if (copied) {
                        result = 1;
                        plain = false;
                    }
                }
                Virtual<uint32_t>(packedOther, pslot::kRelease);
                packedOther = nullptr;
            }
            if (plain) {
                uint32_t size = Virtual<uint32_t>(segment, sslot::kGetRecordSize, key);
                if (size != 0xFFFFFFFF) {
                    void* const buffer = New(size);
                    if (Virtual<uint32_t>(segment, sslot::kReadRecord, key, buffer, &size) != 0) {
                        if (Virtual<uint8_t>(other, sslot::kWriteRecord, key, buffer, size)) {
                            if (!keepOriginal)
                                Virtual<uint8_t>(segment, sslot::kDeleteRecord, key);
                            result = 1;
                        }
                    }
                    Delete(buffer);
                }
            }
        }
    }

    Unlock(guard);
    return result;
}

// The game's 0x00970B4F, P slot 26: CopyDatabaseRecords(segment, filter,
// fromOther, keepOriginal). Both segments are locked for the length of the
// call; the source's keys come through the filter into a new key list, and
// each is copied with CopyOneRecord. The answer is how many were copied.
uint32_t __fastcall CopyDatabaseRecords(char* self, void*, void* other, void* filter, uint32_t fromOther,
    uint32_t keepOriginal)
{
    LockGuard guard;
    Lock(guard, self);
    void* const segment = self + packed::kSegment;
    uint32_t copied = 0;
    Virtual<void>(segment, sslot::kLock);
    Virtual<void>(other, sslot::kLock);

    void* const memory = New(0x40);
    char* const list =
        memory ? This<char*>(addr::cGZPersistResourceKeyList_Construct, memory) : static_cast<char*>(nullptr);
    Virtual<uint32_t>(list, 0x4);
    if (static_cast<uint8_t>(fromOther))
        Virtual<uint32_t>(other, sslot::kGetKeyList, static_cast<void*>(list), filter);
    else
        Virtual<uint32_t>(segment, sslot::kGetKeyList, static_cast<void*>(list), filter);

    KeyIterator it = At<KeyIterator>(list, 0x18);
    const KeyIterator end = At<KeyIterator>(list, 0x28);
    Key* const last = end.current;
    while (it.current != last) {
        if (CopyOneRecord(self, nullptr, other, it.current, fromOther, keepOriginal))
            ++copied;
        This<void>(addr::cGZDBSegmentPackedFile_KeyDequeIteratorNext, &it);
    }

    Virtual<uint32_t>(list, 0x8);
    Virtual<void>(other, sslot::kUnlock);
    Virtual<void>(segment, sslot::kUnlock);
    Unlock(guard);
    return copied;
}

// The game's 0x00971CC7, P slot 41: writes a whole file into the segment as
// the record `key`. An entry the key already has is dropped first (counts,
// compressed mark, index entry, its space freed). A file worth compressing
// is read whole, compressed and written; anything else is copied in pieces
// of 0x4000 bytes. An empty file writes nothing and answers false, after
// the old entry is gone.
uint8_t __fastcall ImportRecordFromFile(char* self, void*, Key* key, void* path)
{
    LockGuard guard;
    Lock(guard, self);
    uint8_t result = 0;

    if (OpenForWrite(self)) {
        void* const openMap = self + packed::kOpenMap;
        if (This<void*>(addr::cGZDBSegmentPackedFile_RecordMapFind, openMap, key) == At<void*>(openMap, 0)) {
            Key copy;
            copy.type = key->type;
            void* const indexMap = At<void*>(self, packed::kIndexMap);
            copy.group = key->group;
            copy.instance = key->instance;
            char* const node = This<char*>(addr::cGZDBSegmentPackedFile_IndexFind, indexMap, &copy);
            if (node) {
                const uint32_t oldOffset = At<uint32_t>(node, 0x10);
                const uint32_t oldSize = At<uint32_t>(node, 0x14);
                This<void>(addr::cGZDBSegmentPackedFile_UpdateKeyCounts, self, key->type, key->group, uint32_t{0});
                Virtual<uint8_t>(self, pslot::kSetCompressed, key, uint32_t{0}, uint32_t{0});
                This<void>(addr::cGZDBSegmentPackedFile_IndexErase, At<void*>(self, packed::kIndexMap),
                    static_cast<void*>(node), indexMap);
                Virtual<uint8_t>(self, pslot::kFreeSpace, oldOffset, oldSize);
                At<uint8_t>(self, packed::kIndexDirty) = 1;
            }

            alignas(4) char source[0x80];
            This<void*>(addr::cRZFile_Construct, &source[0], path);
            if (This<uint8_t>(addr::cRZFile_Open, &source[0], uint32_t{1}, uint32_t{2}, uint32_t{1})) {
                const uint32_t size = This<uint32_t>(addr::cRZFile_Length, &source[0]);
                if (size != 0) {
                    bool plain = true;
                    if (This<uint8_t>(addr::cGZDBSegmentPackedFile_ShouldCompress, self, key, size)) {
                        char* const buffer = static_cast<char*>(New(size + 2));
                        void* compressed = nullptr;
                        buffer[size + 1] = 0;
                        buffer[size] = 0;
                        uint32_t compressedSize = 0;
                        if (This<uint8_t>(addr::cRZFile_Read, &source[0], static_cast<void*>(buffer), size)) {
                            if (This<uint8_t>(addr::cGZDBSegmentPackedFile_CompressIfWorthIt, self,
                                    static_cast<void*>(buffer), size, &compressed, &compressedSize)) {
                                uint32_t position = 0;
                                if (Virtual<uint8_t>(self, pslot::kAllocateSpace, &position, compressedSize)) {
                                    if (Virtual<uint8_t>(self, pslot::kWriteAt, compressed, position,
                                            compressedSize)) {
                                        IndexEntry entry;
                                        entry.key = copy;
                                        entry.size = compressedSize;
                                        entry.offset = position;
                                        This<void>(addr::cGZDBSegmentPackedFile_UpdateKeyCounts, self, key->type,
                                            key->group, uint32_t{1});
                                        Virtual<uint8_t>(self, pslot::kSetCompressed, key, uint32_t{1}, size);
                                        alignas(4) char inserted[12];
                                        This<void*>(addr::cGZDBSegmentPackedFile_InsertIntoIndex,
                                            At<void*>(self, packed::kIndexMap), static_cast<void*>(&inserted[0]),
                                            &entry);
                                        At<uint8_t>(self, packed::kIndexDirty) = 1;
                                        result = 1;
                                    } else {
                                        Virtual<uint8_t>(self, pslot::kFreeSpace, position, compressedSize);
                                    }
                                }
                                Delete(compressed);
                            }
                        }
                        Delete(buffer);
                        if (result)
                            plain = false;
                    }

                    if (plain) {
                        uint32_t position = 0;
                        if (Virtual<uint8_t>(self, pslot::kAllocateSpace, &position, size)) {
                            uint32_t done = 0;
                            uint32_t target = position;
                            void* const chunk = New(kImportChunk);
                            This<uint32_t>(addr::cRZFile_SeekToPosition, &source[0], uint32_t{0});
                            result = 1;
                            bool failed = false;
                            if (size > 0) {
                                do {
                                    if (!result) {
                                        failed = true;
                                        break;
                                    }
                                    const uint32_t count = done + kImportChunk > size ? size - done : kImportChunk;
                                    result = This<uint8_t>(addr::cRZFile_Read, &source[0], chunk, count);
                                    if (result) {
                                        result = Virtual<uint8_t>(self, pslot::kWriteAt, chunk, target, count);
                                        done += count;
                                        target += count;
                                    }
                                } while (done < size);
                                if (!failed && !result)
                                    failed = true;
                            }
                            if (!failed) {
                                IndexEntry entry;
                                entry.key = copy;
                                entry.offset = position;
                                entry.size = size;
                                alignas(4) char inserted[12];
                                This<void*>(addr::cGZDBSegmentPackedFile_InsertIntoIndex,
                                    At<void*>(self, packed::kIndexMap), static_cast<void*>(&inserted[0]), &entry);
                                This<void>(addr::cGZDBSegmentPackedFile_UpdateKeyCounts, self, key->type,
                                    key->group, uint32_t{1});
                                At<uint8_t>(self, packed::kIndexDirty) = 1;
                            } else {
                                Virtual<uint8_t>(self, pslot::kFreeSpace, position, size);
                            }
                            Delete(chunk);
                        }
                    }
                }
                This<uint8_t>(addr::cRZFile_Close, &source[0]);
            }
            This<void>(addr::cRZFile_Destruct, &source[0]);
        }
    }

    Unlock(guard);
    return result;
}

// The copy into a new packed file and the swap of the two files, the body
// of a compaction once the names are made. `first` is where the copy is
// written, `second` where the old file waits during the swap.
void CompactInto(char* self, GameString& first, GameString& second, uint8_t& result)
{
    void* const memory = New(0x1C8);
    char* const other =
        memory ? This<char*>(addr::cGZDBSegmentPackedFile_Construct, memory) : static_cast<char*>(nullptr);
    Virtual<uint32_t>(other, pslot::kAddRef);
    if (Virtual<uint8_t>(other, pslot::kInit)) {
        Virtual<uint8_t>(other, pslot::kSetPath, &first);
        if (Virtual<uint8_t>(other + packed::kSegment, sslot::kOpen, uint32_t{1}, uint32_t{1})) {
            const uint32_t copied = Virtual<uint32_t>(self, pslot::kCopyRecords,
                static_cast<void*>(other + packed::kSegment), uint32_t{0}, uint32_t{0}, uint32_t{1});
            if (copied != 0xFFFFFFFF) {
                Virtual<uint8_t>(other, pslot::kSetHoleUseFlags, uint32_t{0});
                if (Virtual<uint8_t>(other + packed::kSegment, sslot::kClose)) {
                    Virtual<uint8_t>(other, pslot::kShutdown);
                    At<uint32_t>(self, packed::kHoleFlags) &= 0xFFFFFFFE;
                    if (Virtual<uint8_t>(self + packed::kSegment, sslot::kClose)) {
                        if (Move(self + packed::kPath, &second)) {
                            if (Move(&first, self + packed::kPath)) {
                                // Both arguments come out as true whatever the flags are: the
                                // original tests (flags | 2) and (flags | 1) against zero.
                                const uint32_t flags = At<uint32_t>(self, packed::kFlags);
                                const uint32_t write = ((flags | 2) & 0xFFFFFF00) | ((flags | 2) != 0 ? 1u : 0u);
                                const uint32_t read = ((flags | 1) & 0xFFFFFF00) | ((flags | 1) != 0 ? 1u : 0u);
                                result = Virtual<uint8_t>(self + packed::kSegment, sslot::kOpen, read, write);
                            } else {
                                Move(&second, self + packed::kPath);
                            }
                        }
                    }
                }
            }
            if (Virtual<uint8_t>(other + packed::kSegment, sslot::kIsOpen))
                Virtual<uint8_t>(other + packed::kSegment, sslot::kClose);
        }
        Virtual<uint8_t>(other, pslot::kShutdown);
    }
    Virtual<uint32_t>(other, pslot::kRelease);
}

// The game's 0x00972233, P slot 27: CompactDatabase. With no holes in
// memory it only gives up a hole table the header still names. Otherwise,
// for a segment open for write with no record open: an empty index cuts the
// file to the header; else every record is copied into a new packed file
// beside this one, and the two files change places.
uint8_t __fastcall CompactDatabase(char* self, void*)
{
    LockGuard guard;
    Lock(guard, self);
    uint8_t result = 0;

    if (!At<uint8_t>(self, packed::kCompacting)) {
        const bool open = (At<uint8_t>(self, packed::kFlags) & 3) != 0;
        At<uint8_t>(self, packed::kCompacting) = 1;
        if (open) {
            const auto dropHoleTable = [&] {
                Virtual<uint8_t>(self, pslot::kFreeSpace, At<uint32_t>(self, packed::kHoleOffset),
                    At<uint32_t>(self, packed::kHoleSize));
                At<uint32_t>(self, packed::kHoleCount) = 0;
                At<uint32_t>(self, packed::kHoleOffset) = 0;
                At<uint32_t>(self, packed::kHoleSize) = 0;
                At<uint8_t>(self, packed::kHolesDirty) = 0;
            };

            bool go = true;
            if (At<uint32_t>(self, packed::kHolesInMap) == 0) {
                if (At<uint32_t>(self, packed::kHoleCount) != 0)
                    dropHoleTable();
                else
                    result = 1;
                if (At<uint32_t>(self, packed::kHolesInMap) == 0)
                    go = false;
            }
            if (go && (At<uint8_t>(self, packed::kFlags) & 2) && At<uint32_t>(self, packed::kOpenCount) == 0) {
                if (At<uint32_t>(self, packed::kHoleCount) != 0)
                    dropHoleTable();
                if (At<uint32_t>(At<void*>(self, packed::kIndexMap), 0x10) == 0) {
                    void* const compressedMap = self + packed::kCompressedMap;
                    This<void>(addr::cGZDBSegmentPackedFile_KeyTableClear, compressedMap);
                    At<uint32_t>(self, packed::kIndexCount) = 0;
                    At<uint32_t>(self, packed::kIndexOffset) = 0;
                    At<uint32_t>(self, packed::kIndexSize) = 0;
                    At<uint8_t>(self, packed::kIndexDirty) = 0;
                    Virtual<uint8_t>(self + packed::kFile, file::kSetLength, kHeaderBytes);
                    result = 1;
                } else {
                    const char* const prefix =
                        reinterpret_cast<const char*>(addr::cGZDBSegmentPackedFile_kTempPrefix);
                    GameString first;
                    This<void*>(addr::SC4String_FromText, &first, prefix);
                    GameString second;
                    This<void*>(addr::SC4String_FromText, &second, prefix);
                    GameString folder;
                    This<void*>(addr::SC4String_SC4String, &folder);
                    Cdecl<void>(addr::RZFile_GetDirectoryOf, static_cast<void*>(self + packed::kPath), &folder);
                    if (Cdecl<uint8_t>(addr::RZFile_MakeTempNameIn, &folder, &first,
                            static_cast<void*>(nullptr))) {
                        if (Cdecl<uint8_t>(addr::RZFile_MakeTempNameIn, &folder, &second,
                                static_cast<void*>(nullptr))) {
                            if (Exists(second))
                                Remove(second);
                            CompactInto(self, first, second, result);
                            if (Exists(second))
                                Remove(second);
                        }
                        if (Exists(first))
                            Remove(first);
                    }
                    DestroyString(folder);
                    DestroyString(second);
                    DestroyString(first);
                }
            }
        }
        At<uint8_t>(self, packed::kCompacting) = 0;
    }

    Unlock(guard);
    return result;
}

// The game's 0x00972542, P slot 25: CopyDatabaseRecords(path, filter,
// fromOther, keepOriginal): the same between this segment and a packed file
// at `path`, opened for the length of the call. The other file is opened
// for write unless records are only read from it (fromOther and
// keepOriginal both set). The answer is the count, or -1 when the other
// file could not be set up.
uint32_t __fastcall CopyDatabaseRecordsToPath(char* self, void*, void* path, void* filter, uint32_t fromOther,
    uint32_t keepOriginal)
{
    LockGuard guard;
    Lock(guard, self);
    uint32_t result = 0xFFFFFFFF;

    void* const memory = New(0x1C8);
    char* const other =
        memory ? This<char*>(addr::cGZDBSegmentPackedFile_Construct, memory) : static_cast<char*>(nullptr);
    Virtual<uint32_t>(other, pslot::kAddRef);
    if (Virtual<uint8_t>(other, pslot::kInit)) {
        if (Virtual<uint8_t>(other, pslot::kSetPath, path)) {
            const bool readOnly = static_cast<uint8_t>(fromOther) != 0 && static_cast<uint8_t>(keepOriginal) != 0;
            // The original keeps the flag in the low byte of the path argument's slot and
            // pushes the whole dword.
            const uint32_t write =
                (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(path)) & 0xFFFFFF00) | (readOnly ? 0u : 1u);
            void* const otherSegment = other + packed::kSegment;
            if (Virtual<uint8_t>(otherSegment, sslot::kOpen, uint32_t{1}, write)) {
                result = Virtual<uint32_t>(self, pslot::kCopyRecords, otherSegment, filter, fromOther, keepOriginal);
                Virtual<uint8_t>(otherSegment, sslot::kClose);
            }
        }
        Virtual<uint8_t>(other, pslot::kShutdown);
    }
    Virtual<uint32_t>(other, pslot::kRelease);

    Unlock(guard);
    return result;
}

bool InstallSaveFile3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZDBSegmentPackedFile_SetUserVersion, reinterpret_cast<void*>(&SetNextUserVersion)},
        {addr::cGZDBSegmentPackedFile_SetHoleUseFlags, reinterpret_cast<void*>(&SetHoleUseFlags)},
        {addr::cGZDBSegmentPackedFile_SetHoleCompactionMinimumRatio,
            reinterpret_cast<void*>(&SetHoleCompactionMinimumRatio)},
        {addr::cGZDBSegmentPackedFile_WriteHeader, reinterpret_cast<void*>(&WriteHeader)},
        {addr::cGZDBSegmentPackedFile_InitNewHeader, reinterpret_cast<void*>(&InitNewHeader)},
        {addr::cGZDBSegmentPackedFile_ShouldCompact, reinterpret_cast<void*>(&ShouldCompact)},
        {addr::cGZDBSegmentPackedFile_WriteHoleTable, reinterpret_cast<void*>(&WriteHoleTable)},
        {addr::cGZDBSegmentPackedFile_CloseAllOpenRecords, reinterpret_cast<void*>(&CloseAllOpenRecords)},
        {addr::cGZDBSegmentPackedFile_WriteIndex, reinterpret_cast<void*>(&WriteIndex)},
        {addr::cGZDBSegmentPackedFile_WriteCompressedDirectory, reinterpret_cast<void*>(&WriteCompressedDirectory)},
        {addr::cGZDBSegmentPackedFile_Flush, reinterpret_cast<void*>(&Flush)},
        {addr::cGZDBSegmentPackedFile_CopyOneRecord, reinterpret_cast<void*>(&CopyOneRecord)},
        {addr::cGZDBSegmentPackedFile_CopyDatabaseRecords, reinterpret_cast<void*>(&CopyDatabaseRecords)},
        {addr::cGZDBSegmentPackedFile_ImportRecordFromFile, reinterpret_cast<void*>(&ImportRecordFromFile)},
        {addr::cGZDBSegmentPackedFile_CompactDatabase, reinterpret_cast<void*>(&CompactDatabase)},
        {addr::cGZDBSegmentPackedFile_CopyDatabaseRecordsToPath, reinterpret_cast<void*>(&CopyDatabaseRecordsToPath)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        // The packed file's reading half (resources.packed_file.part1) has
        // this one function too, and one address takes one detour: with that
        // group on (or a jump already in place), theirs is the one that runs.
        if (hooks[i].first == addr::cGZDBSegmentPackedFile_SetUserVersion
            && (registry::ModeOf("resources.packed_file.part1") != registry::Mode::Original
                || *reinterpret_cast<const uint8_t*>(hooks[i].first) == 0xE9)) {
            log::Write("saves: 0x%08X left to resources.packed_file.part1", static_cast<unsigned>(hooks[i].first));
            continue;
        }
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveFile3Registered = registry::Register({
    "saves.file.part3",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZDBSegmentPackedFile_Flush,
    "Our packed file write path, part 3: the header, Flush, the index, hole and directory tables, compaction, copy "
    "and import",
    false,
    InstallSaveFile3,
});

} // namespace
} // namespace opensc4::saves
