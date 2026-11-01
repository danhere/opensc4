// Our part of the HTML viewer's file cache (docs/html.md, piece h7, its
// first part h7a: "Ours: the cache's entries"): the hash map of entries and
// the cache's slots that only work on it, written by hand from the
// disassembly. Each function is a detour on the game's, in groups, off by
// default:
//
//   ui.html.cache.map       the entry map (STLport hash_map<string, entry>
//                           at the cache's +0x34): the hash, finding,
//                           buckets, growing, the iterator, the nodes and
//                           pairs, erasing, clearing, inserting
//   ui.html.cache.entries   the cache's slots on its entries: lookup, the
//                           freshness checks (slot 12, the one that ran in
//                           play), getting, removing, purging the expired,
//                           releasing the data, the folder and ini names,
//                           the release, the clock
//
// Left the game's (h7b): everything that touches files: the constructor
// and destructor, opening the folder, reading and writing FileCache.ini,
// the `.cache` files (naming, writing, reading, deleting: 0x008FC669 is
// called by ours), getting and putting a page.
//
// The map (0x14 bytes): the hash and equal functors (+0, +1, empty), the
// buckets (a vector of node pointers, +4 to +8, its room +0xC), the count
// (+0x10). A node (0x48 bytes, the STL allocator 0x0090CF54): next (+0),
// the address (a game string, +4), the entry (+0x18, 0x30 bytes): the file
// name (a game string), the shared data block (+0x14, h1c's 0x008FC3C3),
// its size (+0x18), three words (+0x1C to +0x24), last used (+0x28),
// expiry (+0x2C).
//
// Calls between these functions go through the game's addresses, so any
// mix of groups works. html_parser::g_useGame (set by the HTML tests) sends
// every detour to the game's function, so one run can do the same work
// through the game's code and through ours and compare.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::html_parser {
extern bool g_useGame;
}

namespace opensc4::html_cache {
namespace {

using html_parser::g_useGame;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("html cache: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

template <typename R, typename... A>
R CallThis(std::uintptr_t fn, void* self, A... a)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(fn)(self, a...);
}

template <typename R, typename... A>
R CallPlain(std::uintptr_t fn, A... a)
{
    return reinterpret_cast<R(__cdecl*)(A...)>(fn)(a...);
}

// The game's helpers with no name in the address files yet.
constexpr std::uintptr_t kNextPrime = 0x0090DF7C;    // (count), ret 4: the bucket count for at least count
constexpr std::uintptr_t kVectorFill = 0x0097C01D;   // thiscall (count, value*, allocator*), ret 0xC
constexpr std::uintptr_t kVtableDestroyed = 0x00A80810; // a game string's vtable once destroyed inline

// The map's and the cache's fields.
constexpr std::size_t kBuckets = 0x4;    // the map's bucket vector: begin, end, room
constexpr std::size_t kCount = 0x10;     // the map's entries
constexpr std::size_t kMap = 0x34;       // the cache's entry map
constexpr std::size_t kFolder = 0xC;     // the cache's folder (a game string)
constexpr std::size_t kIni = 0x20;       // the cache's ini name (a game string)
constexpr std::size_t kAsked = 0x54;     // the cache's requests
constexpr std::size_t kSincePurge = 0x58; // requests since the last purge
// An entry's fields (the node's +0x18).
constexpr std::size_t kBlock = 0x14;     // the shared data block
constexpr std::size_t kExpiry = 0x2C;

char*& Next(char* node) { return At<char*>(node, 0); }
char** Buckets(char* map) { return At<char**>(map, kBuckets); }
uint32_t BucketCount(char* map)
{
    return static_cast<uint32_t>(static_cast<int32_t>(At<uintptr_t>(map, kBuckets + 4) - At<uintptr_t>(map, kBuckets)) >> 2);
}

// A game string's length (begin +4, end +8).
uint32_t Length(const char* text)
{
    return static_cast<uint32_t>(At<uintptr_t>(const_cast<char*>(text), 8) - At<uintptr_t>(const_cast<char*>(text), 4));
}

int32_t Now() { return CallPlain<int32_t>(addr::CRT_time32, static_cast<void*>(nullptr)); }

// ====================================================================================
// The map

// 0x008FC450: the address's hash, each byte (sign-extended) xored in after
// a doubling. `this` is not read.
uint32_t Hash(char*, const char* key)
{
    uint32_t h = 0;
    const uint32_t length = Length(key);
    const char* const begin = At<char*>(const_cast<char*>(key), 4);
    for (uint32_t i = 0; i < length; ++i)
        h = (h + h) ^ static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(begin[i])));
    return h;
}

uint32_t HashOf(char* map, const char* key) { return CallThis<uint32_t>(addr::HTMLCacheMap_Hash, map, key); }
bool Equal(char* map, const char* a, const char* b)
{
    return CallThis<bool>(addr::SC4StringEqual_Call, map + 1, a, b);
}

// 0x008FC477: the node with the address, or null.
char* FindNode(char* map, const char* key)
{
    const uint32_t bucket = HashOf(map, key) % BucketCount(map);
    char* node = Buckets(map)[bucket];
    while (node && !Equal(map, node + 4, key))
        node = Next(node);
    return node;
}

// 0x008FC4BC: an iterator (node, map) at the address's node.
char** Find(char* map, char** out, const char* key)
{
    out[0] = CallThis<char*>(addr::HTMLCacheMap_FindNode, map, key);
    out[1] = map;
    return out;
}

// 0x008FC4D7: the address's bucket.
uint32_t BucketOf(char* map, const char* key) { return HashOf(map, key) % BucketCount(map); }

// 0x008FC4F6: the buckets grown for at least `hint` entries (the next
// prime), the nodes moved over in each bucket's order.
void Resize(char* map, uint32_t hint)
{
    const uint32_t old = BucketCount(map);
    if (hint <= old)
        return;
    const uint32_t count = CallThis<uint32_t>(kNextPrime, map, hint);
    if (count <= old)
        return;
    char** fresh[3] = {};
    uint32_t zero = 0;
    CallThis<void*>(kVectorFill, fresh, count, &zero, reinterpret_cast<char*>(&zero) + 3);
    for (uint32_t i = 0; i < old; ++i) {
        for (char* first = Buckets(map)[i]; first; first = Buckets(map)[i]) {
            const uint32_t bucket = HashOf(map, first + 4) % count;
            Buckets(map)[i] = Next(first);
            Next(first) = fresh[0][bucket];
            fresh[0][bucket] = first;
        }
    }
    CallThis<void>(addr::cGZDBSegmentPackedFile_VectorSwap, map + kBuckets, fresh);
    if (fresh[0])
        CallPlain<void>(addr::SC4_Free, static_cast<void*>(fresh[0]));
}

// 0x008FC5E4: the first node of the buckets after the iterator's, or null.
char* NextBucket(char** it)
{
    char* const map = it[1];
    uint32_t bucket = CallThis<uint32_t>(addr::HTMLCacheMap_BucketOf, map, it[0] + 4);
    for (;;) {
        ++bucket;
        if (bucket >= BucketCount(map))
            return nullptr;
        if (char* const node = Buckets(map)[bucket])
            return node;
    }
}

// 0x008FC653: the iterator moved on.
char** Increment(char** it)
{
    char* node = Next(it[0]);
    if (!node)
        node = CallThis<char*>(addr::HTMLCacheMap_NextBucket, it);
    it[0] = node;
    return it;
}

// 0x008FC92D: an entry made from another: the name copied (Substring), the
// seven words after it.
char* CopyEntry(char* self, const char* from)
{
    CallThis<void*>(addr::SC4String_Substring, self, from, 0u, 0xFFFFFFFFu);
    for (std::size_t at = 0x14; at <= 0x2C; at += 4)
        At<uint32_t>(self, at) = At<uint32_t>(const_cast<char*>(from), at);
    return self;
}

// 0x008FCD0A: an entry set from another: the name assigned, the seven
// words after it.
char* SetEntry(char* self, const char* from)
{
    CallThis<void*>(addr::cRZBaseString_Assign, self, from);
    for (std::size_t at = 0x14; at <= 0x2C; at += 4)
        At<uint32_t>(self, at) = At<uint32_t>(const_cast<char*>(from), at);
    return self;
}

// 0x008FC970: a node's pair destroyed and the node freed. `this` (the map)
// is not read; the pair's destructor runs even on a null node, as the
// game's does.
void DeleteNode(char*, char* node)
{
    CallThis<void>(addr::SC4StringPair_Destruct, node + 4);
    if (node)
        CallPlain<void>(addr::SC4_Free, static_cast<void*>(node));
}

// 0x008FC98C: a pair made from another.
char* CopyPair(char* self, const char* from)
{
    CallThis<void*>(addr::SC4String_Substring, self, from, 0u, 0xFFFFFFFFu);
    CallThis<char*>(addr::HTMLCacheEntry_Copy, self + 0x14, from + 0x14);
    return self;
}

// 0x008FCB5D: a pair made from an address and an entry.
char* MakePair(char* self, const char* key, const char* entry)
{
    CallThis<void*>(addr::SC4String_Substring, self, key, 0u, 0xFFFFFFFFu);
    CallThis<char*>(addr::HTMLCacheEntry_Copy, self + 0x14, entry);
    return self;
}

// 0x008FCB7F: the iterator's node unlinked from its bucket and deleted.
void Erase(char* map, char** it)
{
    char* const node = it[0];
    if (!node)
        return;
    const uint32_t bucket = CallThis<uint32_t>(addr::HTMLCacheMap_BucketOf, map, node + 4);
    char** const slot = &Buckets(map)[bucket];
    char* const first = *slot;
    if (first == node) {
        *slot = Next(first);
        CallThis<void>(addr::HTMLCacheMap_DeleteNode, map, first);
        --At<int32_t>(map, kCount);
        return;
    }
    char* previous = first;
    for (char* at = Next(first); at; at = Next(at)) {
        if (at == node) {
            Next(previous) = Next(at);
            CallThis<void>(addr::HTMLCacheMap_DeleteNode, map, at);
            --At<int32_t>(map, kCount);
            return;
        }
        previous = at;
    }
}

// 0x008FCBCF: every node deleted, the buckets emptied, the count 0.
void Clear(char* map)
{
    if (BucketCount(map) != 0) {
        uint32_t i = 0;
        do {
            for (char* node = Buckets(map)[i]; node;) {
                char* const next = Next(node);
                CallThis<void>(addr::HTMLCacheMap_DeleteNode, map, node);
                node = next;
            }
            Buckets(map)[i] = nullptr;
            ++i;
        } while (i < BucketCount(map));
    }
    At<uint32_t>(map, kCount) = 0;
}

// 0x008FD082: the iterator (node, map) passed by value, erased.
void EraseAt(char* map, char* node, char* owner)
{
    char* it[2] = {node, owner};
    CallThis<void>(addr::HTMLCacheMap_Erase, map, it);
}

// 0x008FD0A0: the map's destructor: cleared, the buckets freed.
void Destruct(char* map)
{
    CallThis<void>(addr::HTMLCacheMap_Clear, map);
    if (void* const buckets = At<void*>(map, kBuckets))
        CallPlain<void>(addr::SC4_Free, buckets);
}

// 0x008FD0B8: a new node (0x48 bytes) with a copy of the pair. `this` is
// not read.
char* NewNode(char*, const char* pair)
{
    char* const node = CallPlain<char*>(addr::SC4_stl_allocate, static_cast<std::size_t>(0x48));
    Next(node) = nullptr;
    if (node + 4 != nullptr)
        CallThis<char*>(addr::HTMLCacheMap_CopyPair, node + 4, pair);
    return node;
}

// 0x008FD309: the pair inserted unless its address is there, without
// growing: (node, map, inserted) in `out`, its third byte only.
char** InsertNoResize(char* map, char** out, const char* pair)
{
    const uint32_t bucket = CallThis<uint32_t>(addr::HTMLCacheMap_BucketOf, map, pair);
    char* const first = Buckets(map)[bucket];
    for (char* at = first; at; at = Next(at)) {
        if (Equal(map, at + 4, pair)) {
            out[0] = at;
            out[1] = map;
            At<uint8_t>(out, 8) = 0;
            return out;
        }
    }
    char* const node = CallThis<char*>(addr::HTMLCacheMap_NewNode, map, pair);
    Next(node) = first;
    Buckets(map)[bucket] = node;
    ++At<int32_t>(map, kCount);
    out[0] = node;
    out[1] = map;
    At<uint8_t>(out, 8) = 1;
    return out;
}

// 0x008FD705: grown for one more, then inserted.
char** Insert(char* map, char** out, const char* pair)
{
    CallThis<void>(addr::HTMLCacheMap_Resize, map, At<uint32_t>(map, kCount) + 1);
    CallThis<char**>(addr::HTMLCacheMap_InsertNoResize, map, out, pair);
    return out;
}

// ====================================================================================
// The cache's entries

// The entry's data block released (h1c's 0x008FC3C3), its file deleted
// (0x008FC669, the game's) and its node erased: the expired path of the
// slots below.
void Drop(char* cache, char* node, char* owner)
{
    char* const entry = node + 0x18;
    if (void* const block = At<void*>(entry, kBlock))
        CallThis<uint32_t>(addr::HTMLSharedBlock_Release, block);
    CallThis<bool>(addr::FileCache_RemoveFile, cache, entry);
    CallThis<void>(addr::HTMLCacheMap_EraseAt, cache + kMap, node, owner);
}

// 0x008FC3EF: a reference dropped (the count at +8): above 1 counted down;
// else slot 0 (the deleting destructor) with 1, and 0.
uint32_t Release(char* self)
{
    const uint32_t count = At<uint32_t>(self, 8);
    if (count > 1) {
        At<uint32_t>(self, 8) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, 0, 1u);
    return 0;
}

// 0x008FC405, 0x008FC426: a word of the address's entry (+0x18, the size;
// +0x24) through slot 17 (the lookup), or 0. The game hands the lookup the
// argument's own slot as the out.
uint32_t EntryWord(char* self, const char* key, std::size_t offset)
{
    const char* entry = key;
    if (!Virtual<bool>(self, 0x44, key, &entry))
        return 0;
    return At<uint32_t>(const_cast<char*>(entry), offset);
}
uint32_t EntrySize(char* self, const char* key) { return EntryWord(self, key, 0x18); }
uint32_t EntryWord24(char* self, const char* key) { return EntryWord(self, key, 0x24); }

// 0x008FC447: the clock (time(0)).
int32_t Clock() { return Now(); }

// 0x008FC5B3, slot 17: whether the address has an entry, and where.
bool Lookup(char* self, const char* key, char** out)
{
    char* it[2];
    CallThis<char**>(addr::HTMLCacheMap_Find, self + kMap, it, key);
    if (!it[0])
        return false;
    if (out)
        *out = it[0] + 0x18;
    return true;
}

// 0x008FC613: slot 16 (the entry into a string of the caller's) with a
// string of its own, destroyed inline after.
bool AskWithString(char* self, const void* key)
{
    uint32_t text[5];
    CallThis<void*>(addr::SC4String_SC4String, text);
    const bool answer = Virtual<bool>(self, 0x40, key, static_cast<void*>(text));
    text[0] = static_cast<uint32_t>(kVtableDestroyed);
    if (text[1])
        CallPlain<void>(addr::SC4_Free, reinterpret_cast<void*>(static_cast<std::uintptr_t>(text[1])));
    return answer;
}

// 0x008FCC18, 0x008FCC87: the folder (+0xC) and the ini name (+0x20)
// into the caller's string; true.
bool GetFolder(char* self, void* out)
{
    CallThis<void*>(addr::cRZBaseString_Assign, out, self + kFolder);
    return true;
}
bool GetIniName(char* self, void* out)
{
    CallThis<void*>(addr::cRZBaseString_Assign, out, self + kIni);
    return true;
}

// 0x008FD0DC (slot 12), 0x008FD145, 0x008FD1B4: whether the address's
// entry has not expired (signed against the clock), and with it: nothing
// more, a data block, a file name. An expired entry is dropped.
enum class Fresh { Any, Data, File };
bool Check(char* self, const char* key, Fresh what)
{
    char* it[2];
    CallThis<char**>(addr::HTMLCacheMap_Find, self + kMap, it, key);
    char* const node = it[0];
    if (!node)
        return false;
    char* const entry = node + 0x18;
    if (Now() < At<int32_t>(entry, kExpiry)) {
        if (what == Fresh::Data)
            return At<uint32_t>(entry, kBlock) != 0;
        if (what == Fresh::File)
            return Length(entry) != 0;
        return true;
    }
    Drop(self, node, it[1]);
    return false;
}
bool IsFresh(char* self, const char* key) { return Check(self, key, Fresh::Any); }
bool HasData(char* self, const char* key) { return Check(self, key, Fresh::Data); }
bool HasFile(char* self, const char* key) { return Check(self, key, Fresh::File); }

// 0x008FD227: the address's entry set into the caller's; whether there was
// one.
bool GetEntry(char* self, const char* key, char* out)
{
    char* it[2];
    CallThis<char**>(addr::HTMLCacheMap_Find, self + kMap, it, key);
    if (!it[0])
        return false;
    CallThis<char*>(addr::HTMLCacheEntry_Set, out, it[0] + 0x18);
    return true;
}

// 0x008FD258: the requests since the purge cleared, every entry whose
// expiry is not after the clock (unsigned here) dropped.
void Purge(char* self)
{
    At<uint32_t>(self, kSincePurge) = 0;
    char* it[2];
    CallThis<void*>(addr::cGZDBSegmentPackedFile_KeyTableBegin, self + kMap, it);
    while (char* const node = it[0]) {
        if (static_cast<uint32_t>(Now()) < At<uint32_t>(node + 0x18, kExpiry)) {
            CallThis<char**>(addr::HTMLCacheMap_Increment, it);
            continue;
        }
        char* const owner = it[1];
        CallThis<char**>(addr::HTMLCacheMap_Increment, it);
        Drop(self, node, owner);
    }
}

// 0x008FD2CC: every entry's data block released, then the map cleared.
void ReleaseAll(char* self)
{
    char* const map = self + kMap;
    char* it[2];
    CallThis<void*>(addr::cGZDBSegmentPackedFile_KeyTableBegin, map, it);
    while (it[0]) {
        if (void* const block = At<void*>(it[0], 0x2C))
            CallThis<uint32_t>(addr::HTMLSharedBlock_Release, block);
        CallThis<char**>(addr::HTMLCacheMap_Increment, it);
    }
    CallThis<void>(addr::HTMLCacheMap_Clear, map);
}

// 0x008FD69C: a request counted (a purge after 40), the address's entry
// dropped; true.
bool Remove(char* self, const char* key)
{
    ++At<uint32_t>(self, kAsked);
    if (++At<uint32_t>(self, kSincePurge) > 0x28)
        CallThis<void>(addr::FileCache_Purge, self);
    char* it[2];
    CallThis<char**>(addr::HTMLCacheMap_Find, self + kMap, it, key);
    if (it[0])
        Drop(self, it[0], it[1]);
    return true;
}

// ====================================================================================
// The detours

#define OPENSC4_UNPAREN(...) __VA_ARGS__

// A thiscall function of the game: the detour is a fastcall one (EDX not
// read) that hands its arguments on.
#define OPENSC4_THIS_DETOUR(Ret, Name, Address, Ours, Params, Args)                                            \
    using Name##Fn = Ret(__thiscall*)(char* self OPENSC4_UNPAREN Params);                                    \
    Name##Fn g_##Name = nullptr;                                                                               \
    Ret __fastcall D##Name(char* self, void* OPENSC4_UNPAREN Params)                                         \
    {                                                                                                          \
        if (g_useGame)                                                                                         \
            return g_##Name(self OPENSC4_UNPAREN Args);                                                        \
        OPENSC4_FIRST(#Name " " #Address);                                                                     \
        return Ours(self OPENSC4_UNPAREN Args);                                                                \
    }

// A cdecl function of the game.
#define OPENSC4_CDECL_DETOUR(Ret, Name, Address, Ours, Params, Args)                                           \
    using Name##Fn = Ret(__cdecl*) Params;                                                                     \
    Name##Fn g_##Name = nullptr;                                                                               \
    Ret __cdecl D##Name Params                                                                                 \
    {                                                                                                          \
        if (g_useGame)                                                                                         \
            return g_##Name Args;                                                                              \
        OPENSC4_FIRST(#Name " " #Address);                                                                     \
        return Ours Args;                                                                                      \
    }

// The map.
char* NextBucketAt(char* self) { return NextBucket(reinterpret_cast<char**>(self)); }
char* IncrementAt(char* self) { return reinterpret_cast<char*>(Increment(reinterpret_cast<char**>(self))); }
OPENSC4_THIS_DETOUR(uint32_t, Hash, 0x008FC450, Hash, (, const char* key), (, key))
OPENSC4_THIS_DETOUR(char*, FindNode, 0x008FC477, FindNode, (, const char* key), (, key))
OPENSC4_THIS_DETOUR(char**, Find, 0x008FC4BC, Find, (, char** out, const char* key), (, out, key))
OPENSC4_THIS_DETOUR(uint32_t, BucketOf, 0x008FC4D7, BucketOf, (, const char* key), (, key))
OPENSC4_THIS_DETOUR(void, Resize, 0x008FC4F6, Resize, (, uint32_t hint), (, hint))
OPENSC4_THIS_DETOUR(char*, NextBucket, 0x008FC5E4, NextBucketAt, (), ())
OPENSC4_THIS_DETOUR(char*, Increment, 0x008FC653, IncrementAt, (), ())
OPENSC4_THIS_DETOUR(char*, CopyEntry, 0x008FC92D, CopyEntry, (, const char* from), (, from))
OPENSC4_THIS_DETOUR(void, DeleteNode, 0x008FC970, DeleteNode, (, char* node), (, node))
OPENSC4_THIS_DETOUR(char*, CopyPair, 0x008FC98C, CopyPair, (, const char* from), (, from))
OPENSC4_THIS_DETOUR(char*, MakePair, 0x008FCB5D, MakePair, (, const char* key, const char* entry), (, key, entry))
OPENSC4_THIS_DETOUR(void, Erase, 0x008FCB7F, Erase, (, char** it), (, it))
OPENSC4_THIS_DETOUR(void, Clear, 0x008FCBCF, Clear, (), ())
OPENSC4_THIS_DETOUR(char*, SetEntry, 0x008FCD0A, SetEntry, (, const char* from), (, from))
OPENSC4_THIS_DETOUR(void, EraseAt, 0x008FD082, EraseAt, (, char* node, char* owner), (, node, owner))
OPENSC4_THIS_DETOUR(void, Destruct, 0x008FD0A0, Destruct, (), ())
OPENSC4_THIS_DETOUR(char*, NewNode, 0x008FD0B8, NewNode, (, const char* pair), (, pair))
OPENSC4_THIS_DETOUR(char**, InsertNoResize, 0x008FD309, InsertNoResize, (, char** out, const char* pair),
                    (, out, pair))
OPENSC4_THIS_DETOUR(char**, Insert, 0x008FD705, Insert, (, char** out, const char* pair), (, out, pair))

// The cache's entries.
OPENSC4_THIS_DETOUR(uint32_t, Release, 0x008FC3EF, Release, (), ())
OPENSC4_THIS_DETOUR(uint32_t, EntrySize, 0x008FC405, EntrySize, (, const char* key), (, key))
OPENSC4_THIS_DETOUR(uint32_t, EntryWord24, 0x008FC426, EntryWord24, (, const char* key), (, key))
OPENSC4_CDECL_DETOUR(int32_t, Clock, 0x008FC447, Clock, (), ())
OPENSC4_THIS_DETOUR(bool, Lookup, 0x008FC5B3, Lookup, (, const char* key, char** out), (, key, out))
OPENSC4_THIS_DETOUR(bool, AskWithString, 0x008FC613, AskWithString, (, const void* key), (, key))
OPENSC4_THIS_DETOUR(bool, GetFolder, 0x008FCC18, GetFolder, (, void* out), (, out))
OPENSC4_THIS_DETOUR(bool, GetIniName, 0x008FCC87, GetIniName, (, void* out), (, out))
OPENSC4_THIS_DETOUR(bool, IsFresh, 0x008FD0DC, IsFresh, (, const char* key), (, key))
OPENSC4_THIS_DETOUR(bool, HasData, 0x008FD145, HasData, (, const char* key), (, key))
OPENSC4_THIS_DETOUR(bool, HasFile, 0x008FD1B4, HasFile, (, const char* key), (, key))
OPENSC4_THIS_DETOUR(bool, GetEntry, 0x008FD227, GetEntry, (, const char* key, char* out), (, key, out))
OPENSC4_THIS_DETOUR(void, Purge, 0x008FD258, Purge, (), ())
OPENSC4_THIS_DETOUR(void, ReleaseAll, 0x008FD2CC, ReleaseAll, (), ())
OPENSC4_THIS_DETOUR(bool, Remove, 0x008FD69C, Remove, (, const char* key), (, key))

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void* detour;
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

#define OPENSC4_D(Name, Address) {Address, reinterpret_cast<void*>(&D##Name), Slot(&g_##Name)}

template <std::size_t N>
bool InstallAll(const char* name, const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.detour, detour.original))
            return false;
    }
    log::Write("html cache: %s: %zu functions ours", name, N);
    return true;
}

bool InstallMap(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(Hash, addr::HTMLCacheMap_Hash),
        OPENSC4_D(FindNode, addr::HTMLCacheMap_FindNode),
        OPENSC4_D(Find, addr::HTMLCacheMap_Find),
        OPENSC4_D(BucketOf, addr::HTMLCacheMap_BucketOf),
        OPENSC4_D(Resize, addr::HTMLCacheMap_Resize),
        OPENSC4_D(NextBucket, addr::HTMLCacheMap_NextBucket),
        OPENSC4_D(Increment, addr::HTMLCacheMap_Increment),
        OPENSC4_D(CopyEntry, addr::HTMLCacheEntry_Copy),
        OPENSC4_D(DeleteNode, addr::HTMLCacheMap_DeleteNode),
        OPENSC4_D(CopyPair, addr::HTMLCacheMap_CopyPair),
        OPENSC4_D(MakePair, addr::HTMLCacheMap_MakePair),
        OPENSC4_D(Erase, addr::HTMLCacheMap_Erase),
        OPENSC4_D(Clear, addr::HTMLCacheMap_Clear),
        OPENSC4_D(SetEntry, addr::HTMLCacheEntry_Set),
        OPENSC4_D(EraseAt, addr::HTMLCacheMap_EraseAt),
        OPENSC4_D(Destruct, addr::HTMLCacheMap_Destruct),
        OPENSC4_D(NewNode, addr::HTMLCacheMap_NewNode),
        OPENSC4_D(InsertNoResize, addr::HTMLCacheMap_InsertNoResize),
        OPENSC4_D(Insert, addr::HTMLCacheMap_Insert),
    };
    return InstallAll("map", detours);
}

bool InstallEntries(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(Release, addr::FileCache_Release),
        OPENSC4_D(EntrySize, addr::FileCache_EntrySize),
        OPENSC4_D(EntryWord24, addr::FileCache_EntryWord24),
        OPENSC4_D(Clock, addr::FileCache_Clock),
        OPENSC4_D(Lookup, addr::FileCache_Lookup),
        OPENSC4_D(AskWithString, addr::FileCache_AskWithString),
        OPENSC4_D(GetFolder, addr::FileCache_GetFolder),
        OPENSC4_D(GetIniName, addr::FileCache_GetIniName),
        OPENSC4_D(IsFresh, addr::FileCache_IsFresh),
        OPENSC4_D(HasData, addr::FileCache_HasData),
        OPENSC4_D(HasFile, addr::FileCache_HasFile),
        OPENSC4_D(GetEntry, addr::FileCache_GetEntry),
        OPENSC4_D(Purge, addr::FileCache_Purge),
        OPENSC4_D(ReleaseAll, addr::FileCache_ReleaseAll),
        OPENSC4_D(Remove, addr::FileCache_Remove),
    };
    return InstallAll("entries", detours);
}

const bool g_registered[] = {
    registry::Register({"ui.html.cache.map", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLCacheMap_Hash,
                        "Our HTML file cache's entry map: hash, find, buckets, grow, iterator, nodes, erase, "
                        "clear, insert (19 detours; docs/html.md)",
                        false, InstallMap}),
    registry::Register({"ui.html.cache.entries", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::FileCache_IsFresh,
                        "Our HTML file cache's slots on its entries: lookup, freshness, get, remove, purge, "
                        "release (15 detours; docs/html.md)",
                        false, InstallEntries}),
};

} // namespace
} // namespace opensc4::html_cache
