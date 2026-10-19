// The framework's packed file, cGZDBSegmentPackedFile (docs/resources.md),
// read side, part 2: 0x0096E99A to 0x0096FB0F. Replacement, off by default:
//
//   resources.packed_file.part2: the raw file view's constructor and
//     deleting destructor, GetRawFileView, GetRecordCount, the index, holes
//     and open-record containers' own code, AbortRecord, IsRecordOpen,
//     TestForRecord, the size, offset, type count and group count getters,
//     GetResourceKeyList, IsCompressed and the hole totals.
//
// The object as this part reads it (offsets from the object; the functions
// of the cIGZPersistDBSegment interface get object + 4 as `this`):
//   +0x00 main vtable 0x00AD87D0, +0x04 interface vtable 0x00AD8728,
//   +0x0C the lock (a counted object: +0x4 lock, +0x8 unlock),
//   +0x1C open flags (& 3 = open),
//   +0x24 the index: a pointer to a hash table {?, buckets begin, buckets
//         end, capacity, count} of nodes {next, type, group, instance,
//         offset, size} (0x18 bytes), bucket = instance % bucket count,
//   +0xD8 the open records: a tree {header, count}, nodes of 0x20 bytes
//         with the key at +0x10 and the record at +0x1C, ordered by key,
//         equal keys allowed,
//   +0x14C the holes: a tree {header, count}, nodes of 0x18 bytes with
//         {offset, size} at +0x10, ordered by SIZE, one hole per size,
//   +0x16C and +0x180 the number of types and of groups when key tracking
//         is on (0 otherwise: the getters then count by walking),
//   +0x1B0 the directory of compressed records (a hash table by key).
// Trees are the game's red-black trees: header node +0x4 root, +0x8
// leftmost, +0xC rightmost; nodes +0x0 colour byte, +0x4 parent, +0x8 left,
// +0xC right, +0x10 the value.
//
// The raw file view (0x10 bytes): +0x0 vtable 0x00AD8468, +0x4 the file
// interface 0x00AD83E8, +0x8 reference count, +0xC the record (counted).
//
// Left the game's, called by address: the write side (see the list in
// docs/resources.md), 0x0096EB22 (a `sub ecx,4; jmp` thunk), and the helpers
// folded with code of other classes: 0x0096EDBC (list destructor), 0x0096EDF7
// (vector swap), 0x0096F1B3 (hash erase; its one caller is 0x0092DC4F),
// 0x0096F57B (tree constructor; also called from 0x00939247), 0x0096F290
// (hash find returning an iterator), 0x0096F5B7 (hash iterator step) and
// 0x0096F614 (hash resize): the last three are also called from the classes
// at 0x0097C2xx to 0x0097E8xx.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::packed_file {
namespace {

namespace unknown { // cIGZUnknown, by vtable offset
constexpr std::size_t kAddRef = 0x4;
} // namespace unknown

namespace lock { // the segment's lock, by vtable offset
constexpr std::size_t kUnlock = 0x8;
} // namespace lock

namespace object { // from the object
constexpr std::size_t kLock = 0xC;
constexpr std::size_t kFlags = 0x1C;
constexpr std::size_t kIndex = 0x24;
constexpr std::size_t kOpenRecords = 0xD8;
constexpr std::size_t kHoles = 0x14C;
constexpr std::size_t kTypeCount = 0x16C;
constexpr std::size_t kGroupCount = 0x180;
constexpr std::size_t kDirectory = 0x1B0;
} // namespace object

namespace face { // from the cIGZPersistDBSegment interface (object + 4)
constexpr std::size_t kLock = 0x8;
constexpr std::size_t kFlags = 0x18;
constexpr std::size_t kIndex = 0x20;
constexpr std::size_t kOpenRecords = 0xD4;
constexpr std::size_t kGetResourceKeyList = 0x48; // slot 18: (list, filter)
} // namespace face

namespace record { // an open record
constexpr std::size_t kRelease = 0x8; // main vtable
constexpr std::size_t kGetKey = 0xC;  // the interface at +0x4: (key&)
} // namespace record

namespace keys { // the key list and the key filter, by vtable offset
constexpr std::size_t kFilterIsIncluded = 0xC; // (key const&)
constexpr std::size_t kListInsert = 0x10;      // (key const&)
} // namespace keys

constexpr uint32_t kDirectoryType = 0xE86B1EEF; // the compression directory's own record
constexpr uint32_t kOpenMask = 3;

struct Key {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};
static_assert(sizeof(Key) == 0xC);

struct TreeNode {
    uint8_t color;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
};
static_assert(sizeof(TreeNode) == 0x10);

struct Tree {
    TreeNode* header;
    uint32_t count;
};

struct Hole {
    uint32_t offset;
    uint32_t size;
};

struct HoleNode : TreeNode {
    Hole hole;
};
static_assert(sizeof(HoleNode) == 0x18);

struct OpenEntry {
    Key key;
    void* record;
};
static_assert(sizeof(OpenEntry) == 0x10);

struct OpenNode : TreeNode {
    OpenEntry entry;
};
static_assert(sizeof(OpenNode) == 0x20);

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

struct HashTable {
    uint32_t field0;
    IndexNode** begin;
    IndexNode** end;
    IndexNode** capacity;
    uint32_t count;
};
static_assert(sizeof(HashTable) == 0x14);

struct HashIterator {
    IndexNode* node;
    HashTable* table;
};

// What a tree insert writes through its result pointer: the node and, for
// the unique insert, whether it is new (5 bytes; the original frames leave 8).
struct TreeInsertResult {
    TreeNode* node;
    uint8_t inserted;
};
static_assert(sizeof(TreeInsertResult) == 8);

// What the index insert writes: the node, the table and whether it is new
// (9 bytes).
struct HashInsertResult {
    IndexNode* node;
    HashTable* table;
    uint8_t inserted;
};
static_assert(sizeof(HashInsertResult) == 0xC);

// The game's scope guard for a lock (0x0048C9F0 builds it and locks; the
// functions here end it inline with the lock's unlock slot).
struct LockGuard {
    std::uintptr_t vtable;
    void* lock;
};
static_assert(sizeof(LockGuard) == 8);

// The game's set of dwords, built on the stack by the two counters.
struct Uint32Set {
    void* header;
    uint32_t count;
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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void Lock(LockGuard& guard, void* lockObject)
{
    This<void*>(addr::SC4LockGuard_Construct, &guard, lockObject);
}

void Unlock(LockGuard& guard)
{
    Virtual<void>(guard.lock, lock::kUnlock);
}

// The game's 0x0040D4C0 and 0x0040D500: the next and the previous node of a
// tree in order (cdecl, whatever their entries say).
TreeNode* Next(TreeNode* node)
{
    return Cdecl<TreeNode*>(addr::cSC4Advisor_F40D4C0, node);
}

TreeNode* Previous(TreeNode* node)
{
    return Cdecl<TreeNode*>(addr::cSC4Advisor_F40D500, node);
}

uint32_t BucketCount(const HashTable* table)
{
    return static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<char*>(table->end) -
                                                      reinterpret_cast<char*>(table->begin)) >> 2);
}

// --- Trees: erase and clear ---------------------------------------------------

// The game's 0x0096EAC8 and 0x0096EAF5 (the same code twice, for the open
// record tree and the holes tree): frees a subtree, node by node. `this` is
// the tree (not used).
void EraseTree(Tree* tree, TreeNode* node)
{
    while (node) {
        EraseTree(tree, node->right);
        TreeNode* const left = node->left;
        Free(node);
        node = left;
    }
}

void __fastcall EraseOpenTreeHook(Tree* self, void*, TreeNode* node)
{
    EraseTree(self, node);
}

void __fastcall EraseHoleTreeHook(Tree* self, void*, TreeNode* node)
{
    EraseTree(self, node);
}

// The game's 0x0096ED6A (open records) and 0x0096ED93 (holes): empties a
// tree; nothing at all when it is empty already.
void __fastcall ClearTree(Tree* self, void*)
{
    if (self->count == 0)
        return;
    EraseTree(self, self->header->parent);
    self->header->left = self->header;
    self->header->parent = nullptr;
    self->header->right = self->header;
    self->count = 0;
}

void __fastcall ClearOpenTree(Tree* self, void*)
{
    ClearTree(self, nullptr);
}

void __fastcall ClearHoleTree(Tree* self, void*)
{
    ClearTree(self, nullptr);
}

// The game's 0x0096F0FF (open records) and 0x0096F116 (holes): the trees'
// destructors: empty the tree, then free the header.
void __fastcall DestroyOpenTree(Tree* self, void*)
{
    ClearOpenTree(self, nullptr);
    if (TreeNode* const header = self->header)
        Free(header);
}

void __fastcall DestroyHoleTree(Tree* self, void*)
{
    ClearHoleTree(self, nullptr);
    if (TreeNode* const header = self->header)
        Free(header);
}

// --- The raw file view --------------------------------------------------------

// The game's 0x0096EC7C: the raw file view's constructor (0x10 bytes); takes
// a reference on the record when there is one.
char* __fastcall ViewConstruct(char* self, void*, void* recordObject)
{
    At<std::uintptr_t>(self, 0x4) = addr::cGZDBSegmentPackedFile_RawFileBaseVtable;
    At<uint32_t>(self, 0x8) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::cGZDBSegmentPackedFile_RawFileViewVtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZDBSegmentPackedFile_RawFileViewFileVtable;
    At<void*>(self, 0xC) = recordObject;
    if (recordObject)
        Virtual<uint32_t>(recordObject, unknown::kAddRef);
    return self;
}

// The game's 0x0096ECAD: slot 6 (0x18) of the view's vtable 0x00AD8468, the
// deleting destructor.
char* __fastcall ViewDelete(char* self, void*, uint32_t flags)
{
    This<void>(addr::cGZDBRawFileView_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0096ECC9: slot 17 (0x44) of the main vtable, `this` the
// object. Makes a raw file view of a record (given by its interface at
// record + 4, or null) and hands out the view's file interface with one
// reference. When the allocation fails the original calls through a null
// pointer, as here.
uint8_t __fastcall GetRawFileView(char* self, void*, char* recordFace, void** out)
{
    LockGuard guard;
    Lock(guard, self + object::kLock);
    char* view = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x10});
    if (view)
        view = ViewConstruct(view, nullptr, recordFace ? recordFace - 4 : nullptr);
    char* const file = view ? view + 4 : nullptr;
    *out = file;
    Virtual<uint32_t>(file, unknown::kAddRef);
    Unlock(guard);
    return 1;
}

// --- The interface's counters -------------------------------------------------

// The game's 0x0096ED23: slot 16 (0x40) of the interface, `this` the
// interface. The number of records: with a filter, what GetResourceKeyList
// counts without a list; without one, the index's count.
uint32_t __fastcall GetRecordCount(char* self, void*, void* filter)
{
    LockGuard guard;
    Lock(guard, self + face::kLock);
    uint32_t count;
    if (filter) {
        count = Virtual<uint32_t>(self, face::kGetResourceKeyList, static_cast<void*>(nullptr), filter);
    } else {
        HashTable* const index = At<HashTable*>(self, face::kIndex);
        count = index ? index->count : 0;
    }
    Unlock(guard);
    return count;
}

// --- The index ----------------------------------------------------------------

// The game's 0x0096EDD3: a new index node (0x18 bytes) with a copy of the
// entry and no next node. `this` is the table (not used).
IndexNode* __fastcall NewIndexNode(HashTable*, void*, const IndexEntry* entry)
{
    IndexNode* const node = Cdecl<IndexNode*>(addr::SC4_stl_allocate, uint32_t{0x18});
    node->next = nullptr;
    This<void*>(addr::cGZDBSegmentPackedFile_IndexEntryCopy, &node->entry, entry);
    return node;
}

// The game's 0x0096EEA9: the index node of a key, or null. A table with no
// buckets divides by zero, as the original.
IndexNode* __fastcall IndexFind(HashTable* self, void*, const Key* key)
{
    const uint32_t buckets = BucketCount(self);
    IndexNode* node = self->begin[key->instance % buckets];
    while (node) {
        if (This<uint8_t>(addr::cGZDBSegmentPackedFile_KeyEquals, &node->entry.key, key))
            break;
        node = node->next;
    }
    return node;
}

// The game's 0x0096F209: inserts an entry unless its key is there, without
// growing the bucket array. The new node goes to the front of its bucket.
HashInsertResult* __fastcall IndexInsertNoResize(HashTable* self, void*, HashInsertResult* result,
                                                 const IndexEntry* entry)
{
    const uint32_t bucket = entry->key.instance % BucketCount(self);
    IndexNode* const first = self->begin[bucket];
    for (IndexNode* node = first; node; node = node->next) {
        if (This<uint8_t>(addr::cGZDBSegmentPackedFile_KeyEquals, &node->entry.key, entry)) {
            result->node = node;
            result->table = self;
            result->inserted = 0;
            return result;
        }
    }
    IndexNode* const node = NewIndexNode(self, nullptr, entry);
    node->next = first;
    self->begin[bucket] = node;
    ++self->count;
    result->node = node;
    result->table = self;
    result->inserted = 1;
    return result;
}

// --- The holes ----------------------------------------------------------------

// The game's 0x0096EE20: links a new holes node under `parent` (left when the
// parent is the header, when `x` is given, or when the hole is smaller than
// the parent's; right when `w` is given or otherwise) and rebalances.
TreeInsertResult* __fastcall HoleInsertAt(Tree* self, void*, TreeInsertResult* result, TreeNode* x,
                                          HoleNode* parent, const Hole* hole, TreeNode* w)
{
    HoleNode* node;
    if (parent != self->header && (w || (!x && !(hole->size < parent->hole.size)))) {
        node = This<HoleNode*>(addr::SC4Tree_NewPairNode, self, hole);
        parent->right = node;
        if (parent == self->header->right)
            self->header->right = node;
    } else {
        node = This<HoleNode*>(addr::SC4Tree_NewPairNode, self, hole);
        parent->left = node;
        if (parent == self->header) {
            self->header->parent = node;
            self->header->right = node;
        } else if (parent == self->header->left) {
            self->header->left = node;
        }
    }
    node->left = nullptr;
    node->right = nullptr;
    node->parent = parent;
    Cdecl<void>(addr::SC4Tree_RebalanceForInsert, static_cast<TreeNode*>(node), &self->header->parent);
    ++self->count;
    result->node = node;
    return result;
}

// The game's 0x0096F12D: inserts a hole unless one of its size is there;
// the result is the new node and 1, or the node in the way and 0.
TreeInsertResult* __fastcall HoleInsertUnique(Tree* self, void*, TreeInsertResult* result, const Hole* hole)
{
    HoleNode* const header = static_cast<HoleNode*>(self->header);
    HoleNode* parent = header;
    HoleNode* walk = static_cast<HoleNode*>(header->parent);
    bool less = true;
    while (walk) {
        parent = walk;
        less = hole->size < walk->hole.size;
        walk = static_cast<HoleNode*>(less ? walk->left : walk->right);
    }
    HoleNode* before = parent;
    if (less) {
        if (parent == header->left) {
            TreeInsertResult made;
            result->node = HoleInsertAt(self, nullptr, &made, walk, parent, hole, nullptr)->node;
            result->inserted = 1;
            return result;
        }
        before = static_cast<HoleNode*>(Previous(parent));
    }
    if (before->hole.size < hole->size) {
        TreeInsertResult made;
        result->node = HoleInsertAt(self, nullptr, &made, walk, parent, hole, nullptr)->node;
        result->inserted = 1;
        return result;
    }
    result->node = before;
    result->inserted = 0;
    return result;
}

// The game's 0x0096F594: the same through a result of its own, copied out.
TreeInsertResult* __fastcall HoleInsert(Tree* self, void*, TreeInsertResult* result, const Hole* hole)
{
    TreeInsertResult made;
    HoleInsertUnique(self, nullptr, &made, hole);
    result->node = made.node;
    result->inserted = made.inserted;
    return result;
}

// The game's 0x0096F0C3: slot 38 (0x98) of the main vtable, `this` the
// object: the number of holes and their total size. Both are zeroed first;
// 0 when the segment is not open.
uint8_t __fastcall GetHoleTotals(char* self, void*, uint32_t* count, uint32_t* size)
{
    *count = 0;
    *size = 0;
    if (!(At<uint8_t>(self, object::kFlags) & kOpenMask))
        return 0;
    HoleNode* const header = At<HoleNode*>(self, object::kHoles);
    for (TreeNode* node = header->left; node != header; node = Next(node)) {
        ++*count;
        *size += static_cast<HoleNode*>(node)->hole.size;
    }
    return 1;
}

// --- The open records ---------------------------------------------------------

// The game's 0x0096EEE1: a new open-record node (0x20 bytes) with a copy of
// the entry (key and record; no reference taken here). `this` is the tree
// (not used).
OpenNode* __fastcall NewOpenNode(Tree*, void*, const OpenEntry* entry)
{
    OpenNode* const node = Cdecl<OpenNode*>(addr::SC4_stl_allocate, uint32_t{0x20});
    This<void*>(addr::cGZDBSegmentPackedFile_CopyOpenEntry, &node->entry, entry);
    return node;
}

// The game's 0x0096F2AB: links a new open-record node under `parent`, as
// 0x0096EE20 does for holes, ordered by key.
TreeInsertResult* __fastcall OpenInsertAt(Tree* self, void*, TreeInsertResult* result, TreeNode* x,
                                          OpenNode* parent, const OpenEntry* entry, TreeNode* w)
{
    bool left = parent == self->header;
    if (!left && !w) {
        left = x != nullptr;
        if (!left)
            left = This<uint8_t>(addr::SC4ResourceKey_Less, const_cast<OpenEntry*>(entry), &parent->entry) != 0;
    }
    OpenNode* node;
    if (!left) {
        node = NewOpenNode(self, nullptr, entry);
        parent->right = node;
        if (parent == self->header->right)
            self->header->right = node;
    } else {
        node = NewOpenNode(self, nullptr, entry);
        parent->left = node;
        if (parent == self->header) {
            self->header->parent = node;
            self->header->right = node;
        } else if (parent == self->header->left) {
            self->header->left = node;
        }
    }
    node->left = nullptr;
    node->right = nullptr;
    node->parent = parent;
    Cdecl<void>(addr::SC4Tree_RebalanceForInsert, static_cast<TreeNode*>(node), &self->header->parent);
    ++self->count;
    result->node = node;
    return result;
}

// The game's 0x0096F5CD: inserts an open record; equal keys are allowed and
// a new one goes after the ones there.
TreeInsertResult* __fastcall OpenInsert(Tree* self, void*, TreeInsertResult* result, const OpenEntry* entry)
{
    OpenNode* parent = static_cast<OpenNode*>(self->header);
    OpenNode* walk = static_cast<OpenNode*>(parent->parent);
    while (walk) {
        parent = walk;
        if (This<uint8_t>(addr::SC4ResourceKey_Less, const_cast<OpenEntry*>(entry), &walk->entry))
            walk = static_cast<OpenNode*>(walk->left);
        else
            walk = static_cast<OpenNode*>(walk->right);
    }
    OpenInsertAt(self, nullptr, result, walk, parent, entry, walk);
    return result;
}

// The game's 0x0096F3ED: slot 37 (0x94) of the interface, AbortRecord, `this`
// the interface. Finds the record among the open records of its key, closes
// it, drops the map's reference and erases the node. 0 when the segment is
// not open or the record is not in the map.
uint8_t __fastcall AbortRecord(char* self, void*, char* recordFace)
{
    if (!(At<uint8_t>(self, face::kFlags) & kOpenMask))
        return 0;
    Key key;
    Virtual<void>(recordFace, record::kGetKey, &key);
    Tree* const map = &At<Tree>(self, face::kOpenRecords);
    TreeNode* node = This<TreeNode*>(addr::cGZDBSegmentPackedFile_RecordMapFind, map, &key);
    for (; node != map->header; node = Next(node)) {
        OpenEntry* const entry = &static_cast<OpenNode*>(node)->entry;
        if (!This<uint8_t>(addr::SC4ResourceKey_Equals, entry, &key))
            return 0;
        char* const object = static_cast<char*>(entry->record);
        if ((object ? object + 4 : nullptr) != recordFace)
            continue;
        This<void>(addr::cGZDBSegmentPackedFile_RecordClosed, self - 4, object ? object + 4 : nullptr);
        Virtual<uint32_t>(object, record::kRelease);
        This<void>(addr::cGZDBSegmentPackedFile_RecordMapErase, map, node);
        return 1;
    }
    return 0;
}

// The game's 0x0096F494: slot 16 (0x40) of the main vtable, `this` the
// object: whether a record of this key is open.
uint8_t __fastcall IsRecordOpen(char* self, void*, const Key* key)
{
    LockGuard guard;
    Lock(guard, self + object::kLock);
    Tree* const map = &At<Tree>(self, object::kOpenRecords);
    TreeNode* const header = map->header;
    TreeNode* const node = This<TreeNode*>(addr::cGZDBSegmentPackedFile_RecordMapFind, map, key);
    Unlock(guard);
    return node != header;
}

// --- Queries on the index -----------------------------------------------------

// The game's 0x0096F779: slot 32 (0x80) of the interface, TestForRecord,
// `this` the interface: whether the key is in the index or among the open
// records (a new record is in the map before it is in the index).
uint8_t __fastcall TestForRecord(char* self, void*, const Key* key)
{
    if (!(At<uint8_t>(self, face::kFlags) & kOpenMask))
        return 0;
    Key copy = *key;
    if (IndexFind(At<HashTable*>(self, face::kIndex), nullptr, &copy))
        return 1;
    Tree* const map = &At<Tree>(self, face::kOpenRecords);
    return This<TreeNode*>(addr::cGZDBSegmentPackedFile_RecordMapFind, map, key) != map->header;
}

// The game's 0x0096F7CD: slot 18 (0x48) of the main vtable, `this` the
// object: a record's size in the file from the index, or -1.
uint32_t __fastcall GetIndexSize(char* self, void*, const Key* key)
{
    LockGuard guard;
    Lock(guard, self + object::kLock);
    uint32_t result = 0xFFFFFFFF;
    if (At<uint8_t>(self, object::kFlags) & kOpenMask) {
        Key copy = *key;
        if (IndexNode* const node = IndexFind(At<HashTable*>(self, object::kIndex), nullptr, &copy))
            result = node->entry.size;
    }
    Unlock(guard);
    return result;
}

// The game's 0x0096F823: slot 21 (0x54) of the main vtable: a record's offset
// in the file from the index, or -1.
uint32_t __fastcall GetIndexOffset(char* self, void*, const Key* key)
{
    LockGuard guard;
    Lock(guard, self + object::kLock);
    uint32_t result = 0xFFFFFFFF;
    if (At<uint8_t>(self, object::kFlags) & kOpenMask) {
        Key copy = *key;
        if (IndexNode* const node = IndexFind(At<HashTable*>(self, object::kIndex), nullptr, &copy))
            result = node->entry.offset;
    }
    Unlock(guard);
    return result;
}

// The two counters' walk: the tracked count when there is one, else the
// number of different values of one key field (at `indexField` in an index
// node, `openField` in an open-record node) over the index and the open
// records. Neither path looks at the open flags.
uint32_t CountKeyField(char* self, std::size_t tracked, std::size_t indexField, std::size_t openField)
{
    LockGuard guard;
    Lock(guard, self + object::kLock);
    uint32_t result = At<uint32_t>(self, tracked);
    if (result == 0) {
        Uint32Set seen;
        This<void*>(addr::SC4Uint32Set_Construct, &seen);
        TreeInsertResult inserted;
        HashIterator walk;
        This<void>(addr::cGZDBSegmentPackedFile_KeyTableBegin, At<HashTable*>(self, object::kIndex), &walk);
        while (walk.node) {
            This<void*>(addr::cSC4AdvisorTransportation_ZotInsert, &seen, &inserted,
                        reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(walk.node) + indexField));
            This<void*>(addr::cGZDBSegmentPackedFile_HashIteratorNext, &walk);
        }
        TreeNode* const header = At<TreeNode*>(self, object::kOpenRecords);
        for (TreeNode* node = header->left; node != header; node = Next(node)) {
            This<void*>(addr::cSC4AdvisorTransportation_ZotInsert, &seen, &inserted,
                        reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(node) + openField));
        }
        result = seen.count;
        This<void>(addr::SC4Uint32Set_Destroy46F9D0, &seen);
    }
    Unlock(guard);
    return result;
}

// The game's 0x0096F879: slot 22 (0x58) of the main vtable: the number of
// record types.
uint32_t __fastcall GetTypeCount(char* self, void*)
{
    return CountKeyField(self, object::kTypeCount, 0x4, 0x10);
}

// The game's 0x0096F912: slot 23 (0x5C) of the main vtable: the number of
// groups.
uint32_t __fastcall GetGroupCount(char* self, void*)
{
    return CountKeyField(self, object::kGroupCount, 0x8, 0x14);
}

// The game's 0x0096F9AB: slot 18 (0x48) of the interface,
// GetResourceKeyList(list, filter), `this` the interface; no lock. Walks the
// index in bucket order, skips the compression directory's record and the
// keys the filter refuses, adds the rest to the list (when there is one) and
// returns how many. Open records not yet in the index are not seen.
uint32_t __fastcall GetResourceKeyList(char* self, void*, void* list, void* filter)
{
    uint32_t count = 0;
    if (!(At<uint8_t>(self, face::kFlags) & kOpenMask))
        return count;
    HashIterator walk;
    This<void>(addr::cGZDBSegmentPackedFile_KeyTableBegin, At<HashTable*>(self, face::kIndex), &walk);
    while (walk.node) {
        Key key = walk.node->entry.key;
        if (walk.node->entry.key.type != kDirectoryType &&
            (!filter || Virtual<uint8_t>(filter, keys::kFilterIsIncluded, &key))) {
            if (list)
                Virtual<uint8_t>(list, keys::kListInsert, &key);
            ++count;
        }
        This<void*>(addr::cGZDBSegmentPackedFile_HashIteratorNext, &walk);
    }
    return count;
}

// The game's 0x0096FA30: slot 32 (0x80) of the main vtable, `this` the
// object: whether the key is in the directory of compressed records.
uint8_t __fastcall IsCompressed(char* self, void*, const Key* key)
{
    LockGuard guard;
    Lock(guard, self + object::kLock);
    HashIterator found;
    This<void*>(addr::cGZDBSegmentPackedFile_HashFind, self + object::kDirectory, &found, key);
    Unlock(guard);
    return found.node != nullptr;
}

bool InstallPackedFile2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[29] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZDBSegmentPackedFile_EraseOpenTree, reinterpret_cast<void*>(&EraseOpenTreeHook)},
        {addr::cGZDBSegmentPackedFile_EraseHoleTree, reinterpret_cast<void*>(&EraseHoleTreeHook)},
        {addr::cGZDBRawFileView_Construct, reinterpret_cast<void*>(&ViewConstruct)},
        {addr::cGZDBRawFileView_Delete, reinterpret_cast<void*>(&ViewDelete)},
        {addr::cGZDBSegmentPackedFile_GetRawFileView, reinterpret_cast<void*>(&GetRawFileView)},
        {addr::cGZDBSegmentPackedFile_GetRecordCount, reinterpret_cast<void*>(&GetRecordCount)},
        {addr::cGZDBSegmentPackedFile_ClearOpenTree, reinterpret_cast<void*>(&ClearOpenTree)},
        {addr::cGZDBSegmentPackedFile_ClearHoleTree, reinterpret_cast<void*>(&ClearHoleTree)},
        {addr::cGZDBSegmentPackedFile_NewIndexNode, reinterpret_cast<void*>(&NewIndexNode)},
        {addr::cGZDBSegmentPackedFile_HoleInsertAt, reinterpret_cast<void*>(&HoleInsertAt)},
        {addr::cGZDBSegmentPackedFile_IndexFind, reinterpret_cast<void*>(&IndexFind)},
        {addr::cGZDBSegmentPackedFile_NewOpenNode, reinterpret_cast<void*>(&NewOpenNode)},
        {addr::cGZDBSegmentPackedFile_GetHoleTotals, reinterpret_cast<void*>(&GetHoleTotals)},
        {addr::cGZDBSegmentPackedFile_DestroyOpenTree, reinterpret_cast<void*>(&DestroyOpenTree)},
        {addr::cGZDBSegmentPackedFile_DestroyHoleTree, reinterpret_cast<void*>(&DestroyHoleTree)},
        {addr::cGZDBSegmentPackedFile_HoleInsertUnique, reinterpret_cast<void*>(&HoleInsertUnique)},
        {addr::cGZDBSegmentPackedFile_IndexInsertNoResize, reinterpret_cast<void*>(&IndexInsertNoResize)},
        {addr::cGZDBSegmentPackedFile_OpenInsertAt, reinterpret_cast<void*>(&OpenInsertAt)},
        {addr::cGZDBSegmentPackedFile_AbortRecord, reinterpret_cast<void*>(&AbortRecord)},
        {addr::cGZDBSegmentPackedFile_IsRecordOpen, reinterpret_cast<void*>(&IsRecordOpen)},
        {addr::cGZDBSegmentPackedFile_HoleInsert, reinterpret_cast<void*>(&HoleInsert)},
        {addr::cGZDBSegmentPackedFile_OpenInsert, reinterpret_cast<void*>(&OpenInsert)},
        {addr::cGZDBSegmentPackedFile_TestForRecord, reinterpret_cast<void*>(&TestForRecord)},
        {addr::cGZDBSegmentPackedFile_GetIndexSize, reinterpret_cast<void*>(&GetIndexSize)},
        {addr::cGZDBSegmentPackedFile_GetIndexOffset, reinterpret_cast<void*>(&GetIndexOffset)},
        {addr::cGZDBSegmentPackedFile_GetTypeCount, reinterpret_cast<void*>(&GetTypeCount)},
        {addr::cGZDBSegmentPackedFile_GetGroupCount, reinterpret_cast<void*>(&GetGroupCount)},
        {addr::cGZDBSegmentPackedFile_GetResourceKeyList, reinterpret_cast<void*>(&GetResourceKeyList)},
        {addr::cGZDBSegmentPackedFile_IsCompressed, reinterpret_cast<void*>(&IsCompressed)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_packedFile2Registered = registry::Register({
    "resources.packed_file.part2",
    "resources",
    registry::Kind::Detour,
    registry::Phase::PreAppInit,
    addr::cGZDBSegmentPackedFile_IndexFind,
    "Our packed file, part 2: index, holes and open-record containers, record queries, AbortRecord, raw file view",
    false,
    InstallPackedFile2,
});

} // namespace
} // namespace opensc4::packed_file
