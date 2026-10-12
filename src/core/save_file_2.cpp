// The packed file's write path (docs/save-format.md), part 2: records in the
// segment. Replacement, off by default:
//
//   saves.file.part2: writing at an offset, the compress wrapper and its
//     "is it worth it" test, which records are compressed, open for write,
//     CreateNewRecord, the write-out of one record when it is closed, space
//     from the hole list and back to it, DeleteRecord, WriteRecord (with and
//     without compression), the compressed-record map's setter, and the
//     segment base's three locked wrappers.
//
// The packed file (cGZDBSegmentPackedFile, 0x1C8 bytes, vtable 0x00AD87D0)
// is "P" below; its cIGZPersistDBSegment part (vtable 0x00AD8728) is
// S = P + 4. Functions of the segment part get S as `this`. Offsets from P:
//
//   +0x0C  the lock (an object with a vtable: +4 enter, +8 leave)
//   +0x1C  open flags: bit 0 read, bit 1 write
//   +0x24  the index: a hash map from key to {offset, size}; node: next,
//          type, group, instance, offset, size
//   +0x28  a byte: the index changed
//   +0x40  the file (cRZFile; +0x1C Length, +0x20 SetLength, +0x30 seek,
//          +0x40 Write)
//   +0xC0  where the package starts in the file
//   +0xD8  the open records: a red-black multimap {header, count} from key
//          to record
//   +0x144 hole flags: bit 1 = take space from holes
//   +0x14C the holes: a red-black multimap {header, count} ordered by size;
//          node: +0x10 offset, +0x14 size
//   +0x158 a byte: the holes changed
//   +0x184 a byte: the compressed-record map changed
//   +0x185 a byte: compress every record
//   +0x188, +0x19C  hash sets of types and of groups to compress (counts at
//          +0x198 and +0x1AC)
//   +0x1B0 the compressed records: a hash map from key to uncompressed
//          size; node: next, type, group, instance, size
//
// Every container stays the game's: finding, inserting and erasing go
// through the game's helpers by address, so the index's and the holes' order
// (which the file's tables follow) is the game's.
//
// A record is compressed when its size is 50 to 16,000,000 bytes, its type
// is not the directory's (0xE86B1EEF), compression is on for it, and the
// compressed bytes come to less than 0.8 of the size. That last test is x87
// arithmetic in the original and depends on the precision the thread runs
// with: at 24 bits a ratio of exactly 4/5 rounds up to the constant and the
// record stays uncompressed, at 53 bits it is below it and the record is
// compressed. `CompressIfWorthIt` asks for the precision and does the same.
//
// The compress wrapper has two real catch blocks (catch everything, around
// the allocation of the output and around the plain copy of a very large
// source); here those run through `GuardedAllocate` and `GuardedCopy`.
//
// Still the game's:
//   - the hole map's insert 0x0096F594 with 0x0096F12D and 0x0096EE20: the
//     read path's ReadHoleTable (0x0096FDA3) calls it too, and it is
//     container code;
//   - the compressor (0x00A6D936 to 0x00A6E1A4): part 4;
//   - the write record's constructors 0x00970C4D and 0x00970E67 and its
//     methods: part 1;
//   - everything the read path shares: the index's find, insert and erase,
//     the open map's find, insert and erase, the used type and group
//     counters 0x0097139C, reading and expanding a stored record 0x0096E6F4.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::saves {
namespace {

namespace packed { // offsets from P
constexpr std::size_t kLock = 0x0C;
constexpr std::size_t kFlags = 0x1C;
constexpr std::size_t kIndex = 0x24;
constexpr std::size_t kIndexDirty = 0x28;
constexpr std::size_t kFile = 0x40;
constexpr std::size_t kStart = 0xC0;
constexpr std::size_t kOpenRecords = 0xD8;
constexpr std::size_t kHoleFlags = 0x144;
constexpr std::size_t kHoles = 0x14C;
constexpr std::size_t kHolesDirty = 0x158;
constexpr std::size_t kCompressedDirty = 0x184;
constexpr std::size_t kCompressAll = 0x185;
constexpr std::size_t kTypes = 0x188;
constexpr std::size_t kTypeCount = 0x198;
constexpr std::size_t kGroups = 0x19C;
constexpr std::size_t kGroupCount = 0x1AC;
constexpr std::size_t kCompressed = 0x1B0;

// Slots of the main vtable 0x00AD87D0, by offset.
constexpr std::size_t kSetRecordCompressed = 0x84;
constexpr std::size_t kAllocateSpace = 0x8C;
constexpr std::size_t kFreeSpace = 0x90;
constexpr std::size_t kWriteAt = 0xB8;
} // namespace packed

namespace segment { // the segment part's vtable 0x00AD8728, by offset; `this` is S
constexpr std::size_t kLock = 0x08; // an offset from S, the lock again
constexpr std::size_t kCreateNewRecordUnlocked = 0x8C;
constexpr std::size_t kDeleteRecordUnlocked = 0x98;
constexpr std::size_t kWriteRecordUnlocked = 0xA0;
} // namespace segment

namespace file { // cRZFile, by vtable offset
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kSetLength = 0x20;
constexpr std::size_t kSeek = 0x30;
constexpr std::size_t kWrite = 0x40;
} // namespace file

namespace record { // a record's main vtable (the write record's is 0x00AD8628)
constexpr std::size_t kAddRef = 0x04;
constexpr std::size_t kGetFieldVoid = 0x6C;
constexpr std::size_t kGetSize = 0xD0;
constexpr std::size_t kSeekAbsolute = 0xDC;
constexpr std::size_t kWriteOut = 0xFC;
constexpr std::size_t kKind = 0x40; // a field: 3 = the write record
} // namespace record

constexpr uint32_t kDirectoryType = 0xE86B1EEF;
constexpr uint32_t kSmallest = 50;        // nothing shorter is compressed
constexpr uint32_t kLargest = 16000000;   // nothing longer either
constexpr uint32_t kPlainFrom = 40000000; // from here the wrapper copies instead
constexpr uint32_t kQuickAbove = 250000;
constexpr float kWorthIt = 0.8f; // the float at 0x00AD7BDC

struct Key {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

struct IndexEntry {
    Key key;
    uint32_t offset;
    uint32_t size;
};
static_assert(sizeof(IndexEntry) == 0x14);

struct IndexNode {
    IndexNode* next;
    IndexEntry entry;
};
static_assert(sizeof(IndexNode) == 0x18);

struct CompressedEntry {
    Key key;
    uint32_t size; // uncompressed
};

struct CompressedNode {
    CompressedNode* next;
    CompressedEntry entry;
};
static_assert(sizeof(CompressedNode) == 0x14);

struct HoleNode {
    uint8_t color;
    HoleNode* parent;
    HoleNode* left;
    HoleNode* right;
    uint32_t offset;
    uint32_t size;
};
static_assert(sizeof(HoleNode) == 0x18);

struct Hole {
    uint32_t offset;
    uint32_t size;
};

struct OpenEntry {
    Key key;
    void* record;
};
static_assert(sizeof(OpenEntry) == 0x10);

// What the game's finds and inserts write through the pointer they get: an
// iterator of one or two words and, from an insert, a byte. Never less room
// than the original's frames leave.
struct Found {
    void* node;
    void* container;
    uint32_t inserted;
    uint32_t spare;
};

// The lock's holder (0x00A8ED58): made by 0x0048C9F0, which enters the lock.
struct Guard {
    void* vtable;
    void* lock;
};

// The framework's compressor, 0xC bytes (part 4).
struct Compressor {
    void* vtable;
    uint32_t references;
    uint8_t quick;
    uint8_t spare[7];
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

void* New(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_operator_new, size);
}

void Delete(void* block)
{
    Cdecl<void>(addr::SC4_operator_delete, block);
}

void Enter(Guard& guard, void* lock)
{
    This<void*>(addr::cRZCriticalSectionHolder_Construct, &guard, lock);
}

// The holder's destructor as the original has it inline. What the lock's
// leave returns is what a function without a result of its own leaves in EAX.
uint32_t Leave(Guard& guard)
{
    return Virtual<uint32_t>(guard.lock, 0x8);
}

bool OpenForWrite(char* file)
{
    const uint32_t flags = At<uint32_t>(file, packed::kFlags);
    return (flags & 3) != 0 && (flags & 2) != 0;
}

void*& TreeHeader(char* map)
{
    return At<void*>(map, 0);
}

bool IsOpenRecord(char* file, const Key* key)
{
    char* const open = file + packed::kOpenRecords;
    return This<void*>(addr::cGZDBSegmentPackedFile_RecordMapFind, open, key) != TreeHeader(open);
}

IndexNode* IndexFind(void* index, const Key* key)
{
    return This<IndexNode*>(addr::cGZDBSegmentPackedFile_IndexFind, index, key);
}

void IndexInsert(char* file, IndexEntry* entry)
{
    Found inserted;
    This<void*>(addr::cGZDBSegmentPackedFile_IndexInsert, At<void*>(file, packed::kIndex), &inserted, entry);
}

void UpdateUsedCounts(char* file, const Key* key, uint32_t add)
{
    This<void>(addr::cGZDBSegmentPackedFile_UpdateUsedCounts, file, key->type, key->group, add);
}

// What every writer does to a key that is in the index already: the counts
// go down, the compressed mark goes, the index entry is erased and its bytes
// become a hole. `index` is the map pointer read before the find.
void DropEntry(char* file, const Key* key, IndexNode* node, void* index)
{
    const uint32_t offset = node->entry.offset;
    const uint32_t size = node->entry.size;
    UpdateUsedCounts(file, key, 0);
    Virtual<uint32_t>(file, packed::kSetRecordCompressed, key, uint32_t{0}, uint32_t{0});
    This<void>(addr::cGZDBSegmentPackedFile_IndexErase, At<void*>(file, packed::kIndex), node, index);
    Virtual<uint8_t>(file, packed::kFreeSpace, offset, size);
    At<uint8_t>(file, packed::kIndexDirty) = 1;
}

// A write record from the pool at 0x00B17A14, made by one of its two
// constructors. A pool that gives nothing leaves a null record, which the
// callers then use, as the original does.
template <typename A, typename B>
char* NewWriteRecord(std::uintptr_t constructor, A first, B second, const Key* key, char* file)
{
    void* const pool = reinterpret_cast<void*>(addr::cGZDBWriteRecord_Pool);
    void* const memory = This<void*>(addr::SC4ObjectPool_Allocate, pool);
    if (!memory)
        return nullptr;
    return This<char*>(constructor, memory, first, second, key, file, uint32_t{1});
}

// Puts a record into the open map, which takes a reference of its own.
void EnterOpenRecord(char* file, const Key* key, char* made)
{
    OpenEntry entry;
    entry.key = *key;
    entry.record = made;
    Virtual<uint32_t>(made, record::kAddRef);
    Found inserted;
    This<void*>(addr::cGZDBSegmentPackedFile_RecordMapInsert, file + packed::kOpenRecords, &inserted, &entry);
}

// The game's 0x0096E2A7 (slot 46). Writes `size` bytes at `position` of the
// package: only when open for write, and only when the seek got there.
uint8_t __fastcall WriteAt(char* self, void*, const void* buffer, uint32_t position, uint32_t size)
{
    Guard guard;
    Enter(guard, self + packed::kLock);
    uint8_t result = 0;
    if (OpenForWrite(self)) {
        char* const target = self + packed::kFile;
        const uint32_t reached =
            Virtual<uint32_t>(target, file::kSeek, At<uint32_t>(self, packed::kStart) + position);
        if (reached == At<uint32_t>(self, packed::kStart) + position
            && Virtual<uint8_t>(target, file::kWrite, buffer, size))
            ++result;
    }
    Leave(guard);
    return result;
}

// The game's 0x0096E337 (slot 29, EnableCompressionForAllRecords).
uint8_t __fastcall EnableCompressionForAll(char* self, void*, uint8_t on)
{
    Guard guard;
    Enter(guard, self + packed::kLock);
    At<uint8_t>(self, packed::kCompressAll) = on;
    Leave(guard);
    return 1;
}

// The first guarded block of 0x0096E4DC: the output's size and the output.
__declspec(noinline) void Allocate(Compressor* compressor, uint32_t size, void** out, uint32_t* outSize)
{
    const uint32_t most = This<uint32_t>(addr::cRZCompressor_GetMaxCompressedLength, compressor, size);
    *outSize = most;
    *out = New(most);
}

// The second: a plain copy of the source.
__declspec(noinline) void Copy(const void* source, uint32_t size, void** out, uint32_t* outSize)
{
    *outSize = size;
    void* const copy = New(size);
    *out = copy;
    std::memcpy(copy, source, size);
}

// The original's catch blocks catch everything, structured exceptions too
// (Visual C++ 7.1), so these are structured handlers around a function of
// their own (crash.h, "SEH rule"). False when something was caught.
__declspec(noinline) bool GuardedAllocate(Compressor* compressor, uint32_t size, void** out, uint32_t* outSize)
{
    __try {
        Allocate(compressor, size, out, outSize);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

__declspec(noinline) bool GuardedCopy(const void* source, uint32_t size, void** out, uint32_t* outSize)
{
    __try {
        Copy(source, size, out, outSize);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The game's 0x0096E4DC (stdcall, RET 0x10). Compresses `size` bytes into
// *out, which it allocates when it comes in null (the caller frees it), and
// gives the compressed length in *outSize. Sources of 40,000,000 bytes and
// more are copied as they are.
uint8_t __stdcall Compress(const void* source, uint32_t size, void** out, uint32_t* outSize)
{
    uint8_t result = 0;
    const bool ours = *out == nullptr;
    if (size < kPlainFrom) {
        Compressor compressor;
        This<void*>(addr::cRZCompressor_Construct, &compressor);
        if (ours) {
            // The catch block 0x0096E538 clears the pointer and goes on; the
            // size stays as far as the block got.
            if (!GuardedAllocate(&compressor, size, out, outSize))
                *out = nullptr;
        }
        if (*out != nullptr) {
            if (size > kQuickAbove)
                This<void>(addr::cRZCompressor_SetQuick, &compressor, uint32_t{1});
            if (This<uint8_t>(addr::cRZCompressor_Compress, &compressor, source, size, *out, outSize)) {
                result = 1;
            } else {
                *outSize = 0;
                if (ours) {
                    Delete(*out);
                    *out = nullptr;
                }
            }
        }
        This<void>(addr::cRZCompressor_Destruct, &compressor);
    } else if (ours) {
        if (GuardedCopy(source, size, out, outSize)) {
            result = 1;
        } else {
            // The catch block 0x0096E5D2. What was allocated is not freed.
            *outSize = 0;
            *out = nullptr;
        }
    } else if (*outSize < size) {
        *outSize = 0;
    } else {
        *outSize = size;
        std::memcpy(*out, source, size);
        result = 1;
    }
    return result;
}

// The game's 0x0096EA47 (thiscall, `this` unused). Compresses a record of 50
// to 16,000,000 bytes and keeps the result only when it is under 0.8 of the
// size; otherwise frees it and leaves both outputs zero.
uint8_t __fastcall CompressIfWorthIt(char*, void*, const void* source, uint32_t size, void** out, uint32_t* outSize)
{
    *out = nullptr;
    *outSize = 0;
    if (size < kSmallest || size > kLargest)
        return 0;
    if (!Compress(source, size, out, outSize))
        return 0;
    // fild, fild, fdivp, fcomp: the quotient is rounded to the precision the
    // thread's x87 runs with, so at 24 bits it is a float before the compare.
    double ratio = static_cast<double>(*outSize) / static_cast<double>(size);
    if (Cdecl<int32_t>(addr::SC4Precision_Get) == 0)
        ratio = static_cast<double>(static_cast<float>(ratio));
    if (ratio < static_cast<double>(kWorthIt))
        return 1;
    Delete(*out);
    *out = nullptr;
    *outSize = 0;
    return 0;
}

// The game's 0x0096F4CE (slot 35). Finds `size` bytes of the file for a
// record or a table: the smallest hole that is large enough (the holes are in
// size order), whose rest goes back as a new hole, or else the file grows by
// that much and the place is its old end.
uint8_t __fastcall AllocateSpace(char* self, void*, uint32_t* position, uint32_t size)
{
    if (!OpenForWrite(self))
        return 0;
    char* const target = self + packed::kFile;
    const uint32_t length = Virtual<uint32_t>(target, file::kLength);
    if ((At<uint8_t>(self, packed::kHoleFlags) & 2) != 0) {
        HoleNode* const end = At<HoleNode*>(self, packed::kHoles);
        for (HoleNode* hole = end->left; hole != end; hole = Cdecl<HoleNode*>(addr::cSC4Advisor_F40D4C0, hole)) {
            if (hole->size >= size) {
                const uint32_t offset = hole->offset;
                const uint32_t rest = hole->size - size;
                *position = offset;
                This<void>(addr::cGZDBSegmentPackedFile_RecordMapErase, self + packed::kHoles, hole);
                Virtual<uint8_t>(self, packed::kFreeSpace, offset + size, rest);
                return 1;
            }
        }
    }
    if (!Virtual<uint8_t>(target, file::kSetLength, length + size))
        return 0;
    *position = length;
    return 1;
}

// The game's 0x0096FDE2 (slot 36). Makes a hole of `size` bytes at
// `position`; holes are never joined. Always true.
uint8_t __fastcall FreeSpace(char* self, void*, uint32_t position, uint32_t size)
{
    if (position != 0 && size != 0) {
        Hole hole = {position, size};
        Found inserted;
        This<void*>(addr::cGZDBSegmentPackedFile_HoleMapInsert, self + packed::kHoles, &inserted, &hole);
        At<uint8_t>(self, packed::kHolesDirty) = 1;
    }
    return 1;
}

// The game's 0x0096FF01. Whether a record of this key and size is to be
// compressed.
uint8_t __fastcall ShouldCompress(char* self, void*, Key* key, uint32_t size)
{
    if (size < kSmallest || size > kLargest)
        return 0;
    if (key->type == kDirectoryType)
        return 0;
    if (At<uint8_t>(self, packed::kCompressAll) != 0)
        return 1;
    Found found;
    if (At<uint32_t>(self, packed::kTypeCount) != 0) {
        This<void>(addr::SC4Uint32HashTable_Find, self + packed::kTypes, &found, &key->type);
        if (found.node != nullptr)
            return 1;
    }
    if (At<uint32_t>(self, packed::kGroupCount) == 0)
        return 0;
    This<void>(addr::SC4Uint32HashTable_Find, self + packed::kGroups, &found, &key->group);
    return found.node != nullptr ? 1 : 0;
}

// The game's 0x00971178 (slot 39). Opens a record for writing: refused when
// the key is open already; a record the file has is loaded into a write
// record (expanded first when it is compressed); one it lacks is created.
uint8_t __fastcall OpenRecordForWrite(char* self, void*, Key* key, void** opened)
{
    if (!OpenForWrite(self))
        return 0;
    if (IsOpenRecord(self, key))
        return 0;
    Key copy = *key;
    IndexNode* const entry = IndexFind(At<void*>(self, packed::kIndex), &copy);
    if (!entry)
        return Virtual<uint8_t>(self + 4, segment::kCreateNewRecordUnlocked, key, opened);
    Found compressed;
    This<void*>(addr::cGZDBSegmentPackedFile_CompressedFind, self + packed::kCompressed, &compressed, key);
    char* made;
    if (compressed.node != nullptr) {
        void* data = nullptr;
        uint32_t dataSize = 0;
        if (!This<uint8_t>(addr::cGZDBSegmentPackedFile_ReadAndDecompress, self, entry->entry.offset, entry->entry.size,
                &data, &dataSize))
            return 0;
        made = NewWriteRecord(addr::cGZDBWriteRecord_ConstructFromMemory, data, dataSize, key, self);
        Virtual<uint32_t>(made, record::kAddRef);
        *opened = made + 4;
        Delete(data);
    } else {
        made = NewWriteRecord(addr::cGZDBWriteRecord_Construct, entry->entry.offset, entry->entry.size, key, self);
        Virtual<uint32_t>(made, record::kAddRef);
        *opened = made + 4;
    }
    EnterOpenRecord(self, key, made);
    return 1;
}

// The game's 0x009712D7 (segment slot 35; `this` is S). A new, empty record
// for a key that is neither in the index nor open. Nothing reaches the file.
uint8_t __fastcall CreateNewRecordUnlocked(char* self, void*, Key* key, void** opened)
{
    char* const owner = self - 4;
    if (!OpenForWrite(owner))
        return 0;
    Key copy = *key;
    if (IndexFind(At<void*>(owner, packed::kIndex), &copy))
        return 0;
    if (IsOpenRecord(owner, key))
        return 0;
    char* const made = NewWriteRecord(addr::cGZDBWriteRecord_Construct, uint32_t{0}, uint32_t{0}, key, owner);
    Virtual<uint32_t>(made, record::kAddRef);
    *opened = made + 4;
    EnterOpenRecord(owner, key, made);
    return 1;
}

uint8_t EnableCompressionIn(char* self, std::size_t set, uint32_t* id, uint8_t on)
{
    Guard guard;
    Enter(guard, self + packed::kLock);
    if (on) {
        Found inserted;
        This<void*>(addr::SC4Uint32HashSet_InsertUnique, self + set, &inserted, id);
    } else {
        This<uint32_t>(addr::SC4Uint32HashSet_Erase, self + set, id);
    }
    Leave(guard);
    return 0;
}

// The game's 0x009714FF (slot 30, EnableCompressionByType). Always false.
uint8_t __fastcall EnableCompressionByType(char* self, void*, uint32_t type, uint8_t on)
{
    return EnableCompressionIn(self, packed::kTypes, &type, on);
}

// The game's 0x00971543 (slot 31, EnableCompressionByGroup). Always false.
uint8_t __fastcall EnableCompressionByGroup(char* self, void*, uint32_t group, uint8_t on)
{
    return EnableCompressionIn(self, packed::kGroups, &group, on);
}

// The game's 0x00971587 (slot 33, SetRecordCompressed). Adds the key to the
// compressed-record map with its uncompressed size, or takes it out, and
// marks the map changed; a key already there only gets the new size, with no
// mark. No result of its own.
uint32_t __fastcall SetRecordCompressed(char* self, void*, Key* key, uint8_t on, uint32_t size)
{
    Guard guard;
    Enter(guard, self + packed::kLock);
    char* const map = self + packed::kCompressed;
    Found found;
    This<void*>(addr::cGZDBSegmentPackedFile_CompressedFind, map, &found, key);
    CompressedNode* const node = static_cast<CompressedNode*>(found.node);
    if (on) {
        if (node) {
            node->entry.size = size;
        } else {
            CompressedEntry entry = {*key, size};
            Found inserted;
            This<void*>(addr::cGZDBSegmentPackedFile_CompressedInsert, map, &inserted, &entry);
            At<uint8_t>(self, packed::kCompressedDirty) = 1;
        }
    } else if (node) {
        This<uint32_t>(addr::cGZDBSegmentPackedFile_CompressedErase, map, key);
        At<uint8_t>(self, packed::kCompressedDirty) = 1;
    }
    return Leave(guard);
}

// The compressed half of the write-out below. True when the record's bytes
// are in the file, compressed.
bool FlushCompressed(char* self, char* written, Key* key, const Key& copy, uint32_t size)
{
    bool done = false;
    uint8_t* const bytes = static_cast<uint8_t*>(New(size + 2));
    bytes[size + 1] = 0;
    bytes[size] = 0;
    void* packedBytes = nullptr;
    uint32_t packedSize = 0;
    if (Virtual<uint8_t>(written, record::kSeekAbsolute, uint32_t{0})
        && Virtual<uint8_t>(written, record::kGetFieldVoid, bytes, size)
        && CompressIfWorthIt(self, nullptr, bytes, size, &packedBytes, &packedSize)) {
        uint32_t position = 0;
        if (Virtual<uint8_t>(self, packed::kAllocateSpace, &position, packedSize)) {
            if (Virtual<uint8_t>(self, packed::kWriteAt, packedBytes, position, packedSize)) {
                IndexEntry entry = {copy, position, packedSize};
                UpdateUsedCounts(self, key, 1);
                Virtual<uint32_t>(self, packed::kSetRecordCompressed, key, uint32_t{1}, size);
                IndexInsert(self, &entry);
                At<uint8_t>(self, packed::kIndexDirty) = 1;
                done = true;
            } else {
                Virtual<uint8_t>(self, packed::kFreeSpace, position, packedSize);
            }
        }
        Delete(packedBytes);
    }
    Delete(bytes);
    return done;
}

// The game's 0x00971617. The write-out of one record when it is closed
// (called by CloseRecordUnlocked 0x00971A1A with the record's main pointer
// and a copy of its key). An entry the index has for the key is dropped
// first. Then: an empty record gets an index entry of offset 0 and size 0; a
// record to compress is read back into memory, compressed, given space and
// written; any other, and one whose compression failed at any step, is given
// space and writes itself there (slot 63). When that fails the space is
// freed and the record has no entry at all.
uint8_t __fastcall FlushWriteRecord(char* self, void*, char* written, Key* key)
{
    if (!OpenForWrite(self))
        return 0;
    const Key copy = *key;
    void* const index = At<void*>(self, packed::kIndex);
    Key sought = copy;
    if (IndexNode* const old = IndexFind(index, &sought))
        DropEntry(self, key, old, index);
    const uint32_t size = Virtual<uint32_t>(written, record::kGetSize);
    if (size == 0) {
        IndexEntry entry = {copy, 0, 0};
        IndexInsert(self, &entry);
        UpdateUsedCounts(self, key, 1);
        At<uint8_t>(self, packed::kIndexDirty) = 1;
        return 1;
    }
    if (ShouldCompress(self, nullptr, key, size) && FlushCompressed(self, written, key, copy, size))
        return 1;
    uint32_t position = 0;
    if (!Virtual<uint8_t>(self, packed::kAllocateSpace, &position, size))
        return 0;
    uint8_t result = 0;
    if (At<int32_t>(written, record::kKind) == 3) {
        result = Virtual<uint8_t>(written, record::kWriteOut, position, size);
        if (result) {
            IndexEntry entry = {copy, position, size};
            IndexInsert(self, &entry);
            UpdateUsedCounts(self, key, 1);
            At<uint8_t>(self, packed::kIndexDirty) = 1;
            return result;
        }
    }
    Virtual<uint8_t>(self, packed::kFreeSpace, position, size);
    return result;
}

// The game's 0x00971A55 (segment slot 38; `this` is S). Takes a record out
// of the file: its bytes become a hole. Refused while the key is open.
uint8_t __fastcall DeleteRecordUnlocked(char* self, void*, Key* key)
{
    char* const owner = self - 4;
    if (!OpenForWrite(owner))
        return 0;
    if (IsOpenRecord(owner, key))
        return 0;
    Key copy = *key;
    void* const index = At<void*>(owner, packed::kIndex);
    IndexNode* const old = IndexFind(index, &copy);
    if (!old)
        return 0;
    DropEntry(owner, key, old, index);
    return 1;
}

// The game's 0x00971B1D (segment slot 40; `this` is S). Writes a whole
// record from memory, compressed when that is on for it and worth it.
// Refused while the key is open. The compressed mark is set before space is
// asked for and stays when the write then fails.
uint8_t __fastcall WriteRecordUnlocked(char* self, void*, Key* key, const void* buffer, uint32_t size)
{
    char* const owner = self - 4;
    if (!OpenForWrite(owner))
        return 0;
    if (IsOpenRecord(owner, key))
        return 0;
    const Key copy = *key;
    void* const index = At<void*>(owner, packed::kIndex);
    Key sought = copy;
    if (IndexNode* const old = IndexFind(index, &sought))
        DropEntry(owner, key, old, index);
    bool compressed = false;
    if (ShouldCompress(owner, nullptr, key, size)) {
        void* packedBytes = nullptr;
        uint32_t packedSize = 0;
        if (CompressIfWorthIt(owner, nullptr, buffer, size, &packedBytes, &packedSize)) {
            Virtual<uint32_t>(owner, packed::kSetRecordCompressed, key, uint32_t{1}, size);
            buffer = packedBytes;
            size = packedSize;
            compressed = true;
        }
    }
    uint8_t result = 0;
    uint32_t position = 0;
    if (Virtual<uint8_t>(owner, packed::kAllocateSpace, &position, size)) {
        IndexEntry entry = {copy, position, size};
        if (Virtual<uint8_t>(owner, packed::kWriteAt, buffer, position, size)) {
            IndexInsert(owner, &entry);
            UpdateUsedCounts(owner, key, 1);
            At<uint8_t>(owner, packed::kIndexDirty) = 1;
            result = 1;
        } else {
            Virtual<uint8_t>(owner, packed::kFreeSpace, position, size);
        }
    }
    if (compressed)
        Delete(const_cast<void*>(buffer));
    return result;
}

// The game's 0x00971FE2 (slot 20, WriteRecordRaw). Writes a whole record as
// given, never compressing it. An entry it replaces loses its compressed
// mark like any other; a new one's mark is left as it is.
uint32_t __fastcall WriteRecordRaw(char* self, void*, Key* key, const void* buffer, uint32_t size)
{
    Guard guard;
    Enter(guard, self + packed::kLock);
    uint8_t result = 0;
    if (OpenForWrite(self) && !IsOpenRecord(self, key)) {
        const Key copy = *key;
        void* const index = At<void*>(self, packed::kIndex);
        Key sought = copy;
        if (IndexNode* const old = IndexFind(index, &sought))
            DropEntry(self, key, old, index);
        uint32_t position = 0;
        if (Virtual<uint8_t>(self, packed::kAllocateSpace, &position, size)) {
            IndexEntry entry = {copy, position, size};
            if (Virtual<uint8_t>(self, packed::kWriteAt, buffer, position, size)) {
                IndexInsert(self, &entry);
                UpdateUsedCounts(self, key, 1);
                At<uint8_t>(self, packed::kIndexDirty) = 1;
                result = 1;
            } else {
                Virtual<uint8_t>(self, packed::kFreeSpace, position, size);
            }
        }
    }
    Leave(guard);
    return result;
}

// The game's 0x009757DA (slot 22 of every segment class's base,
// CreateNewRecord): the worker, slot 35, under the lock. `this` is S.
uint8_t __fastcall CreateNewRecord(char* self, void*, Key* key, void** opened)
{
    Guard guard;
    Enter(guard, self + segment::kLock);
    const uint8_t result = Virtual<uint8_t>(self, segment::kCreateNewRecordUnlocked, key, opened);
    Leave(guard);
    return result;
}

// The game's 0x009758BD (base slot 27, DeleteRecord): slot 38 under the lock.
uint8_t __fastcall DeleteRecord(char* self, void*, Key* key)
{
    Guard guard;
    Enter(guard, self + segment::kLock);
    const uint8_t result = Virtual<uint8_t>(self, segment::kDeleteRecordUnlocked, key);
    Leave(guard);
    return result;
}

// The game's 0x00975929 (base slot 29, WriteRecord): slot 40 under the lock.
uint8_t __fastcall WriteRecord(char* self, void*, Key* key, const void* buffer, uint32_t size)
{
    Guard guard;
    Enter(guard, self + segment::kLock);
    const uint8_t result = Virtual<uint8_t>(self, segment::kWriteRecordUnlocked, key, buffer, size);
    Leave(guard);
    return result;
}

bool InstallSaveFile2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[19] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZDBSegmentPackedFile_WriteAt, reinterpret_cast<void*>(&WriteAt)},
        {addr::cGZDBSegmentPackedFile_EnableCompressionForAllRecords,
            reinterpret_cast<void*>(&EnableCompressionForAll)},
        {addr::cGZDBSegmentPackedFile_Compress, reinterpret_cast<void*>(&Compress)},
        {addr::cGZDBSegmentPackedFile_CompressIfWorthIt, reinterpret_cast<void*>(&CompressIfWorthIt)},
        {addr::cGZDBSegmentPackedFile_AllocateSpace, reinterpret_cast<void*>(&AllocateSpace)},
        {addr::cGZDBSegmentPackedFile_FreeSpace, reinterpret_cast<void*>(&FreeSpace)},
        {addr::cGZDBSegmentPackedFile_ShouldCompress, reinterpret_cast<void*>(&ShouldCompress)},
        {addr::cGZDBSegmentPackedFile_OpenRecordForWrite, reinterpret_cast<void*>(&OpenRecordForWrite)},
        {addr::cGZDBSegmentPackedFile_CreateNewRecordUnlocked, reinterpret_cast<void*>(&CreateNewRecordUnlocked)},
        {addr::cGZDBSegmentPackedFile_EnableCompressionByType, reinterpret_cast<void*>(&EnableCompressionByType)},
        {addr::cGZDBSegmentPackedFile_EnableCompressionByGroup, reinterpret_cast<void*>(&EnableCompressionByGroup)},
        {addr::cGZDBSegmentPackedFile_SetRecordCompressed, reinterpret_cast<void*>(&SetRecordCompressed)},
        {addr::cGZDBSegmentPackedFile_FlushWriteRecord, reinterpret_cast<void*>(&FlushWriteRecord)},
        {addr::cGZDBSegmentPackedFile_DeleteRecord, reinterpret_cast<void*>(&DeleteRecordUnlocked)},
        {addr::cGZDBSegmentPackedFile_WriteRecordUnlocked, reinterpret_cast<void*>(&WriteRecordUnlocked)},
        {addr::cGZDBSegmentPackedFile_WriteRecordRaw, reinterpret_cast<void*>(&WriteRecordRaw)},
        {addr::cGZPersistDBSegment_CreateNewRecord, reinterpret_cast<void*>(&CreateNewRecord)},
        {addr::cGZPersistDBSegment_DeleteRecord, reinterpret_cast<void*>(&DeleteRecord)},
        {addr::cGZPersistDBSegment_WriteRecord, reinterpret_cast<void*>(&WriteRecord)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        // diagnostics.prop_pox watches DeleteRecord with a detour of its own,
        // and one address takes one detour: with the diagnostic on (or its
        // jump already in place), this one function stays the game's.
        if (hooks[i].first == addr::cGZDBSegmentPackedFile_DeleteRecord
            && (registry::ModeOf("diagnostics.prop_pox") != registry::Mode::Original
                || *reinterpret_cast<const uint8_t*>(hooks[i].first) == 0xE9)) {
            log::Write("saves: 0x%08X left to the game while diagnostics.prop_pox is on",
                static_cast<unsigned>(hooks[i].first));
            continue;
        }
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveFile2Registered = registry::Register({
    "saves.file.part2",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZDBSegmentPackedFile_FlushWriteRecord,
    "Our packed file write path, part 2: records in the segment, compression's wrapper, space and holes",
    false,
    InstallSaveFile2,
});

} // namespace
} // namespace opensc4::saves
