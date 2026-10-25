// Our STLport containers of the framework's font, f3b of docs/fonts.md
// ("f3b: the font's containers are ours"): the map, hash table, list and
// vector code the linker put in the font's range (0x00896254 to 0x0089A577)
// for the maps of characters to glyphs (the T2K holder's at +0x28 and the
// static map of glyph names at 0x00B5C41C), the glyph table of a glyph cache
// (+0x18), the vector of placed glyphs DrawString builds (0x24 bytes each)
// and the vector of UTF-16 text the Thai pass builds, with the holder's
// destructor. Each is a detour on the game's function, in two groups, off by
// default and installed at Attach (fonts are made while the game starts):
//
//   framework.font.font.containers  34 functions only the font calls: the
//                                   maps' find, insert, insert under a parent,
//                                   node, pair, erase of a subtree, clear and
//                                   destructor (the holder's map and the
//                                   static one each have their own erase,
//                                   clear and destructor), the glyph table's
//                                   insert, insert without growing, node and
//                                   value, the placed glyphs' resize, erase,
//                                   fill insert, insert overflow and the
//                                   copies, fills and constructs under them,
//                                   the text's insert, reserve, copy into a
//                                   new block and find, the holder's
//                                   destructor
//   framework.font.font.shared      2 functions other code calls too: a list
//                                   of words' insert before a node
//                                   (0x00897C8D, seven callers outside) and a
//                                   vector of pointers' insert overflow
//                                   (0x00897D22, called by 0x00997959)
//
// The maps are STLport's red-black trees keyed by a character (a word at
// +0x10 of the node, the value at +0x14; nodes of 0x18 bytes): the map is
// its header's pointer and the count. The glyph table is a hash table (its
// buckets' vector at +4, the count at +0x10) of 0x20-byte nodes: the next
// node, the character, six words. The balancing (0x0040D5E0), the tree's
// step back (0x0040D500), the hash table's growth (0x009978AF), the
// allocators (0x0090CF54 and those over it: 0x0049CAF0, 0x009B3553,
// 0x009DB998, 0x0094B33A), the copies of words and pointers (0x004622A0,
// 0x00413D00, 0x00407A50, 0x004186F0), the text's insert overflow
// (0x004658B0) and the holder's Close (f5) stay the game's, called at their
// addresses, as are these functions when they call one another (so with a
// group off its functions are the game's wherever they are called from).
// DrawString's resize of its placed glyphs (0x00899A73) stays the game's: it
// hands the resize an uninitialised value from its stack.
//
// Compare mode runs ours and then the game's where both can run: the finds
// compare their results; the maps' insert runs ours to its decision without
// its effects (the node already there, or the parent and side it would
// insert under) and then the game's, whose call of the insert under a
// parent is noted and compared; the glyph table's insert without growing
// decides with ours (the node there, or the bucket's first node the new one
// goes before) and compares with what the game's did. The rest (they
// allocate, free, move or link) run the game's alone. While the game's side
// runs, our detours inside it pass through to the game's.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace opensc4::fontcontainers {
namespace {

// ====================================================================================
// Helpers

using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }
inline void* Ptr(Address at) { return reinterpret_cast<void*>(static_cast<uintptr_t>(at)); }

template <typename T>
inline T Load(Address at)
{
    T value;
    std::memcpy(&value, Ptr(at), sizeof value);
    return value;
}

template <typename T>
inline void Store(Address at, T value)
{
    std::memcpy(Ptr(at), &value, sizeof value);
}

// A thiscall game function: `self` in ECX, the rest on the stack.
template <typename R, typename... Args>
inline R This(std::uintptr_t fn, Address self, Args... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(Ptr(self), nullptr, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t fn, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(fn)(args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontcontainers: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions we call

constexpr std::uintptr_t kHolderClose = 0x008976A4;      // f5's Close, thiscall ()
constexpr std::uintptr_t kTreeDecrement = 0x0040D500;    // cdecl (node): the node before
constexpr std::uintptr_t kHashResize = 0x009978AF;       // thiscall (count): buckets for at least count
constexpr std::uintptr_t kListNodeOf = 0x0094B33A;       // (const word*), ret 4: a 0xC-byte node, the word at +8
constexpr std::uintptr_t kWordsAllocate = 0x0049CAF0;    // thiscall (count), ret 4: count words, null for 0
constexpr std::uintptr_t kWordsCopy = 0x004622A0;        // cdecl (first, last, to, tag): the end
constexpr std::uintptr_t kWordsCopyBack = 0x00413D00;    // cdecl (first, last, to's end)
constexpr std::uintptr_t kTextOverflow = 0x004658B0;     // thiscall (at, const word*, tag, count, at end), ret 0x14
constexpr std::uintptr_t kPlacedAllocate = 0x009B3553;   // (count, hint), ret 8: count of 0x24 bytes, null for 0
constexpr std::uintptr_t kPointersAllocate = 0x009DB998; // (count), ret 4: count pointers, null for 0
constexpr std::uintptr_t kPointersFill = 0x004186F0;     // cdecl (to, count, const pointer*): the end

// These functions, at their addresses (ours when their group is on).
constexpr std::uintptr_t kPlacedFill = 0x00896BF6;
constexpr std::uintptr_t kTextFind = 0x00896C19;
constexpr std::uintptr_t kPlacedCopy = 0x00896C90;
constexpr std::uintptr_t kPlacedConstruct = 0x00896CC2;
constexpr std::uintptr_t kPlacedFillN = 0x00896CD8;
constexpr std::uintptr_t kPlacedCopyBack = 0x00896CFD;
constexpr std::uintptr_t kGlyphsEraseTree = 0x0089753F;
constexpr std::uintptr_t kNamesEraseTree = 0x0089756C;
constexpr std::uintptr_t kPlacedCopyTagged = 0x008975B4;
constexpr std::uintptr_t kPlacedUninitCopy = 0x008975D1;
constexpr std::uintptr_t kPlacedCopyBackTagged = 0x008975F7;
constexpr std::uintptr_t kPlacedFillNTagged = 0x00897614;
constexpr std::uintptr_t kGlyphValueConstruct = 0x0089766B;
constexpr std::uintptr_t kPairConstruct = 0x0089768B;
constexpr std::uintptr_t kGlyphsClear = 0x00897A1C;
constexpr std::uintptr_t kNamesClear = 0x00897A45;
constexpr std::uintptr_t kGlyphNode = 0x00897A6E;
constexpr std::uintptr_t kMapNode = 0x00897A91;
constexpr std::uintptr_t kGlyphsDestruct = 0x00897C5F;
constexpr std::uintptr_t kGlyphInsertNoResize = 0x00897CB2;
constexpr std::uintptr_t kMapInsertAt = 0x00897DAC;
constexpr std::uintptr_t kPlacedOverflow = 0x00897F04;
constexpr std::uintptr_t kTextAllocateCopy = 0x00897FC2;
constexpr std::uintptr_t kPlacedFillInsert = 0x0089874D;
constexpr std::uintptr_t kPlacedErase = 0x00897E56;

// ====================================================================================
// Layout

// A map: the header, the count. A node (and the header): colour, parent,
// left, right, the character (a word) and the value.
constexpr Address kMapCount = 0x04;
constexpr Address kParent = 0x04;
constexpr Address kLeft = 0x08;
constexpr Address kRight = 0x0C;
constexpr Address kKey = 0x10;
constexpr uint32_t kMapNodeBytes = 0x18;
constexpr Address kPairValue = 0x04;
// The glyph table: its buckets' vector at +4 (begin, end), the count at
// +0x10; a node: the next one, then the value (the character and six words).
constexpr Address kBuckets = 0x04;
constexpr Address kBucketsEnd = 0x08;
constexpr Address kHashCount = 0x10;
constexpr uint32_t kGlyphNodeBytes = 0x20;
constexpr Address kGlyphValue = 0x04;
// A vector: begin, end, the end of its room, then its allocator.
constexpr Address kEnd = 0x04;
constexpr Address kRoom = 0x08;
// A placed glyph (DrawString's list): 0x24 bytes, copied as nine words.
constexpr uint32_t kPlaced = 0x24;
// A list's node: next, previous, the word.
constexpr Address kPrevious = 0x04;
// The T2K holder: a block at +0x14 and its map at +0x28.
constexpr Address kHolderBlock = 0x14;
constexpr Address kHolderMap = 0x28;

Address Header(Address map) { return Load<Address>(map); }
Address Allocate(uint32_t bytes) { return At(Cdecl<void*>(addr::SC4_stl_allocate, bytes)); }
void Free(Address p) { Cdecl<void>(addr::SC4_Free, Ptr(p)); }

// The nine words of a placed glyph, in order (MOVSD).
void CopyPlacedWords(Address to, Address from)
{
    for (Address word = 0; word < kPlaced; word += 4)
        Store(to + word, Load<uint32_t>(from + word));
}

int32_t PlacedCount(Address first, Address last) { return static_cast<int32_t>(last - first) / static_cast<int32_t>(kPlaced); }

// ====================================================================================
// The placed glyphs' helpers (cdecl; a tag after the arguments is not read)

// The game's 0x00896BF6: every glyph of [first, last) set to `value`, a
// test after each. EAX ends at `last`.
Address PlacedFill(Address first, Address last, Address value)
{
    while (first != last) {
        CopyPlacedWords(first, value);
        first += kPlaced;
    }
    return first;
}

// The game's 0x00896C90: [first, last) copied to `to`, forwards, by the
// count (signed). Returns the end of the copy.
Address PlacedCopy(Address first, Address last, Address to)
{
    for (int32_t count = PlacedCount(first, last); count > 0; --count) {
        CopyPlacedWords(to, first);
        first += kPlaced;
        to += kPlaced;
    }
    return to;
}

// The game's 0x00896CFD: [first, last) copied to end at `to`, backwards.
// Returns the start of the copy.
Address PlacedCopyBack(Address first, Address last, Address to)
{
    for (int32_t count = PlacedCount(first, last); count > 0; --count) {
        last -= kPlaced;
        to -= kPlaced;
        CopyPlacedWords(to, last);
    }
    return to;
}

// The game's 0x00896CC2: a copy of `value` at `at` unless `at` is null. EAX
// is left as it came; ours answers `at`.
Address PlacedConstruct(Address at, Address value)
{
    if (at)
        CopyPlacedWords(at, value);
    return at;
}

// The game's 0x00896CD8: `count` copies (unsigned) by 0x00896CC2. Returns
// the end.
Address PlacedFillN(Address at, uint32_t count, Address value)
{
    for (; count != 0; --count) {
        Cdecl<Address>(kPlacedConstruct, at, value);
        at += kPlaced;
    }
    return at;
}

// The game's 0x008975D1: [first, last) constructed at `to` by 0x00896CC2,
// a test before each. Returns the end.
Address PlacedUninitCopy(Address first, Address last, Address to)
{
    for (; first != last; first += kPlaced, to += kPlaced)
        Cdecl<Address>(kPlacedConstruct, to, first);
    return to;
}

// The game's 0x008975B4, 0x008975F7 and 0x00897614: the copy, the copy
// backwards and the fill with a byte of the stack as the tag (and a 0 after
// it for the copies).
Address PlacedCopyTagged(Address first, Address last, Address to)
{
    uint8_t tag = 0;
    return Cdecl<Address>(kPlacedCopy, first, last, to, At(&tag), uint32_t{0});
}

Address PlacedCopyBackTagged(Address first, Address last, Address to)
{
    uint8_t tag = 0;
    return Cdecl<Address>(kPlacedCopyBack, first, last, to, At(&tag), uint32_t{0});
}

Address PlacedFillNTagged(Address at, uint32_t count, Address value)
{
    uint8_t tag = 0;
    return Cdecl<Address>(kPlacedFillN, at, count, value, At(&tag));
}

// ====================================================================================
// The vector of placed glyphs (thiscall)

// The game's 0x00897E56 (ret 8): [first, last) erased, the rest copied down.
// Returns `first`.
Address PlacedErase(Address self, Address first, Address last)
{
    uint8_t tag = 0;
    Store(self + kEnd, Cdecl<Address>(kPlacedCopyTagged, last, Load<Address>(self + kEnd), first, At(&tag)));
    return first;
}

// The game's 0x00897F04 (ret 0x14; the tag unread): no room for `count`
// copies of `value` at `at`: new room for the size and the larger of the
// size and the count (unsigned), the glyphs before `at`, the copies (one by
// 0x00896CC2, more by 0x00896CD8), the glyphs after it unless `atEnd`, the
// old room freed. EAX is the end of the new room.
Address PlacedOverflow(Address self, Address at, Address value, uint32_t count, uint8_t atEnd)
{
    const uint32_t size = static_cast<uint32_t>(PlacedCount(Load<Address>(self), Load<Address>(self + kEnd)));
    const uint32_t room = (size < count ? count : size) + size;
    const Address block = This<Address>(kPlacedAllocate, self + 8, room, uint32_t{0});
    uint8_t tag = 0;
    Address end = Cdecl<Address>(kPlacedUninitCopy, Load<Address>(self), at, block, At(&tag));
    if (count == 1) {
        Cdecl<Address>(kPlacedConstruct, end, value);
        end += kPlaced;
    } else {
        end = Cdecl<Address>(kPlacedFillN, end, count, value, At(&tag));
    }
    if (!atEnd)
        end = Cdecl<Address>(kPlacedUninitCopy, at, Load<Address>(self + kEnd), end, At(&tag));
    if (const Address old = Load<Address>(self))
        Free(old);
    Store(self + kEnd, end);
    Store(self, block);
    const Address roomEnd = block + room * kPlaced;
    Store(self + kRoom, roomEnd);
    return roomEnd;
}

// The game's 0x0089874D (ret 0xC): `count` copies of `value` inserted at
// `at`: by the overflow if the room left is less (unsigned), else with the
// value copied first, the tail moved up and the gap filled (two ways, by
// whether more glyphs follow `at` than are inserted).
Address PlacedFillInsert(Address self, Address at, uint32_t count, Address value)
{
    if (count == 0)
        return 0;
    uint8_t tag = 0;
    const uint32_t room =
        static_cast<uint32_t>(PlacedCount(Load<Address>(self + kEnd), Load<Address>(self + kRoom)));
    if (room < count)
        return This<Address>(kPlacedOverflow, self, at, value, At(&tag), count, uint32_t{0});
    uint32_t copy[kPlaced / 4];
    CopyPlacedWords(At(copy), value);
    const uint32_t after = static_cast<uint32_t>(PlacedCount(at, Load<Address>(self + kEnd)));
    const Address end = Load<Address>(self + kEnd);
    if (after > count) {
        const uint32_t bytes = count * kPlaced;
        const Address from = end - bytes;
        Cdecl<Address>(kPlacedUninitCopy, from, end, end, At(&tag));
        Store(self + kEnd, Load<Address>(self + kEnd) + bytes);
        Cdecl<Address>(kPlacedCopyBackTagged, at, from, end, At(&tag));
        return Cdecl<Address>(kPlacedFill, at, at + bytes, At(copy));
    }
    const uint32_t more = count - after;
    Cdecl<Address>(kPlacedFillNTagged, end, more, At(copy));
    Store(self + kEnd, Load<Address>(self + kEnd) + more * kPlaced);
    Cdecl<Address>(kPlacedUninitCopy, at, end, Load<Address>(self + kEnd), At(&tag));
    Store(self + kEnd, Load<Address>(self + kEnd) + after * kPlaced);
    return Cdecl<Address>(kPlacedFill, at, end, At(copy));
}

// The game's 0x00899513 (ret 8): `count` glyphs: erased past it if fewer
// (unsigned), else copies of `value` added at the end (the size counted
// again from the end read again and the first begin).
Address PlacedResize(Address self, uint32_t count, Address value)
{
    const Address begin = Load<Address>(self);
    const Address end = Load<Address>(self + kEnd);
    const uint32_t size = static_cast<uint32_t>(PlacedCount(begin, end));
    if (count < size)
        return This<Address>(kPlacedErase, self, begin + count * kPlaced, end);
    const Address now = Load<Address>(self + kEnd);
    const uint32_t have = static_cast<uint32_t>(PlacedCount(begin, now));
    return This<Address>(kPlacedFillInsert, self, now, count - have, value);
}

// ====================================================================================
// The vector of UTF-16 text (thiscall)

// The game's 0x00896C19 (cdecl; the tag unread): the first word of [first,
// last) equal to the double word at `value` (read once for the fours, again
// for each of the last three), else `last`. Both counts are signed.
Address TextFind(Address first, Address last, Address value)
{
    int32_t fours = static_cast<int32_t>(last - first) >> 3;
    if (fours > 0) {
        const uint32_t v = Load<uint32_t>(value);
        for (; fours > 0; --fours)
            for (int i = 0; i < 4; ++i, first += 2)
                if (Load<uint16_t>(first) == v)
                    return first;
    }
    const int32_t left = static_cast<int32_t>(last - first) >> 1;
    if (left < 1 || left > 3)
        return last;
    for (int32_t i = 0; i < left; ++i, first += 2)
        if (Load<uint16_t>(first) == Load<uint32_t>(value))
            return first;
    return last;
}

// The game's 0x00897599 (cdecl): the find with a byte of the stack as tag.
Address TextFindTagged(Address first, Address last, Address value)
{
    uint8_t tag = 0;
    return Cdecl<Address>(kTextFind, first, last, value, At(&tag));
}

// The game's 0x00897FC2 (ret 0xC): `count` words of room (the allocator at
// +8), [first, last) copied in. Returns the room.
Address TextAllocateCopy(Address self, uint32_t count, Address first, Address last)
{
    const Address block = This<Address>(kWordsAllocate, self + 8, count);
    uint8_t tag = 0;
    Cdecl<Address>(kWordsCopy, first, last, block, At(&tag));
    return block;
}

// The game's 0x008986E3 (ret 4): room for `count` words if there is less
// (unsigned): a new block (copied by 0x00897FC2 and the old one freed, or
// allocated if there was none). EAX is the new room's end, or the begin.
Address TextReserve(Address self, uint32_t count)
{
    const Address begin = Load<Address>(self);
    if (static_cast<uint32_t>(static_cast<int32_t>(Load<Address>(self + kRoom) - begin) >> 1) >= count)
        return begin;
    const Address end = Load<Address>(self + kEnd);
    const int32_t size = static_cast<int32_t>(end - begin) >> 1;
    Address block;
    if (begin) {
        block = This<Address>(kTextAllocateCopy, self, count, begin, end);
        if (const Address old = Load<Address>(self))
            Free(old);
    } else {
        block = This<Address>(kWordsAllocate, self + 8, count);
    }
    Store(self + kEnd, block + static_cast<uint32_t>(size) * 2);
    Store(self, block);
    const Address roomEnd = block + count * 2;
    Store(self + kRoom, roomEnd);
    return roomEnd;
}

// The game's 0x00897BDC (ret 8): the word at `value` inserted at `at`: by
// the overflow (0x004658B0) when there is no room, at the end, or with the
// last word copied one up, the rest moved up (0x00413D00) and the word read
// before the move put at `at`. Returns `at` by its place in the vector.
Address TextInsert(Address self, Address at, Address value)
{
    const int32_t place = static_cast<int32_t>(at - Load<Address>(self)) >> 1;
    const Address end = Load<Address>(self + kEnd);
    if (end == Load<Address>(self + kRoom)) {
        uint8_t tag = 0;
        This<Address>(kTextOverflow, self, at, value, At(&tag), uint32_t{1}, uint32_t{0});
    } else if (at == end) {
        if (end)
            Store(end, Load<uint16_t>(value));
        Store(self + kEnd, Load<Address>(self + kEnd) + 2);
    } else {
        if (end)
            Store(end, Load<uint16_t>(end - 2));
        Store(self + kEnd, Load<Address>(self + kEnd) + 2);
        const Address now = Load<Address>(self + kEnd);
        const uint16_t word = Load<uint16_t>(value);
        Cdecl<Address>(kWordsCopyBack, at, now - 4, now - 2);
        Store(at, word);
    }
    return Load<Address>(self) + static_cast<uint32_t>(place) * 2;
}

// ====================================================================================
// The maps of characters (thiscall)

// The game's 0x0089768B (cdecl): a pair (the word, the double word at +4)
// at `at` unless null. Returns `at`.
Address PairConstruct(Address at, Address pair)
{
    if (at) {
        Store(at, Load<uint16_t>(pair));
        Store(at + kPairValue, Load<uint32_t>(pair + kPairValue));
    }
    return at;
}

// The game's 0x00897A91 (ret 4; ECX unused): a node with the pair at +0x10.
Address MapNode(Address pair)
{
    const Address node = Allocate(kMapNodeBytes);
    Cdecl<Address>(kPairConstruct, node + kKey, pair);
    return node;
}

// The game's 0x0089753F and 0x0089756C (ret 4): the subtree at `node`
// freed, right subtrees first (by the function itself, at its address),
// then down the left.
void EraseTree(Address self, Address node, std::uintptr_t itself)
{
    while (node) {
        This<uint32_t>(itself, self, Load<Address>(node + kRight));
        const Address left = Load<Address>(node + kLeft);
        Free(node);
        node = left;
    }
}

// The game's 0x00897A1C and 0x00897A45: if not empty, every node freed and
// the header as a new map's (left, root, right). EAX is the header if it
// did, else as it came (ours 0).
Address Clear(Address self, std::uintptr_t eraseTree)
{
    if (Load<uint32_t>(self + kMapCount) == 0)
        return 0;
    This<uint32_t>(eraseTree, self, Load<Address>(Header(self) + kParent));
    Store(Header(self) + kLeft, Header(self));
    Store<uint32_t>(Header(self) + kParent, 0);
    Store(Header(self) + kRight, Header(self));
    Store<uint32_t>(self + kMapCount, 0);
    return Header(self);
}

// The game's 0x00897C5F and 0x00897C76: cleared, and the header freed.
void Destruct(Address self, std::uintptr_t clear)
{
    This<uint32_t>(clear, self);
    if (const Address header = Header(self))
        Free(header);
}

// The game's 0x0089762F (ret 4): the node of the character at `key` (read
// once for the descent, again for the last test), else the header.
Address MapFind(Address self, Address key)
{
    const Address header = Header(self);
    Address y = header;
    Address x = Load<Address>(header + kParent);
    if (x) {
        const uint16_t k = Load<uint16_t>(key);
        while (x) {
            if (Load<uint16_t>(x + kKey) < k) {
                x = Load<Address>(x + kRight);
            } else {
                y = x;
                x = Load<Address>(x + kLeft);
            }
        }
    }
    if (y == header || Load<uint16_t>(key) < Load<uint16_t>(y + kKey))
        return header;
    return y;
}

// The game's 0x00897DAC (ret 0x14): a node of `pair` as the parent's left
// child if the parent is the header, else its right one if `right`, else the
// left one if `x`, else the left one if the pair's character is below the
// parent's; the first and last kept, the tree balanced, one more. The node
// into `result`; EAX `result`.
Address MapInsertAt(Address self, Address result, Address x, Address parent, Address pair, Address right)
{
    bool left;
    if (parent == Header(self))
        left = true;
    else if (right != 0)
        left = false;
    else if (x != 0)
        left = true;
    else
        left = Load<uint16_t>(pair) < Load<uint16_t>(parent + kKey);
    Address node;
    if (left) {
        node = This<Address>(kMapNode, self, pair);
        Store(parent + kLeft, node);
        const Address header = Header(self);
        if (parent == header) {
            Store(header + kParent, node);
            Store(Header(self) + kRight, node);
        } else if (parent == Load<Address>(header + kLeft)) {
            Store(header + kLeft, node);
        }
    } else {
        node = This<Address>(kMapNode, self, pair);
        Store(parent + kRight, node);
        const Address header = Header(self);
        if (parent == Load<Address>(header + kRight))
            Store(header + kRight, node);
    }
    Store<uint32_t>(node + kLeft, 0);
    Store<uint32_t>(node + kRight, 0);
    Store(node + kParent, parent);
    Cdecl<void>(addr::SC4Tree_RebalanceForInsert, node, Header(self) + kParent);
    Store(self + kMapCount, Load<uint32_t>(self + kMapCount) + 1);
    Store(result, node);
    return result;
}

// What an insert decided, for compare mode: the node already there, or the
// x and parent it inserts under.
struct Decision {
    bool insert = false;
    Address node = 0, x = 0, parent = 0;
};

// The game's 0x00897E7C (ret 8): the pair inserted unless its character is
// there (read once for the descent, again for the test): down from the root,
// left while the character is below the node's; the node before the last
// one (unless that was the first) is the one to test. The node and whether
// it is new into `result` (a byte after the node). With `decided`, only
// what it would do.
Address MapInsert(Address self, Address result, Address pair, Decision* decided)
{
    const Address header = Header(self);
    Address x = Load<Address>(header + kParent);
    Address y = header;
    bool below = true;
    if (x) {
        const uint16_t k = Load<uint16_t>(pair);
        while (x) {
            y = x;
            below = k < Load<uint16_t>(x + kKey);
            x = Load<Address>(x + (below ? kLeft : kRight));
        }
    }
    Address j = y;
    bool insert = false;
    if (below) {
        if (y == Load<Address>(header + kLeft))
            insert = true;
        else
            j = Cdecl<Address>(kTreeDecrement, y);
    }
    if (!insert && !(Load<uint16_t>(j + kKey) < Load<uint16_t>(pair))) {
        if (decided) {
            *decided = Decision{false, j, 0, 0};
            return result;
        }
        Store(result, j);
        Store<uint8_t>(result + 4, 0);
        return result;
    }
    if (decided) {
        *decided = Decision{true, 0, x, y};
        return result;
    }
    Address node = 0;
    This<Address>(kMapInsertAt, self, At(&node), x, y, pair, uint32_t{0});
    Store(result, node);
    Store<uint8_t>(result + 4, 1);
    return result;
}

// ====================================================================================
// The glyph table (thiscall)

// The game's 0x0089766B (cdecl): a glyph's value (seven words) at `at`
// unless null. Returns `at`.
Address GlyphValueConstruct(Address at, Address value)
{
    if (at)
        for (Address word = 0; word < 0x1C; word += 4)
            Store(at + word, Load<uint32_t>(value + word));
    return at;
}

// The game's 0x00897A6E (ret 4; ECX unused): a node, its next cleared, the
// value after it.
Address GlyphNode(Address value)
{
    const Address node = Allocate(kGlyphNodeBytes);
    Store<uint32_t>(node, 0);
    Cdecl<Address>(kGlyphValueConstruct, node + kGlyphValue, value);
    return node;
}

// The bucket of a character (unsigned), and the node of it there, if any.
uint32_t Bucket(Address self, uint32_t key)
{
    const uint32_t buckets =
        static_cast<uint32_t>(static_cast<int32_t>(Load<Address>(self + kBucketsEnd) - Load<Address>(self + kBuckets)) >> 2);
    return key % buckets;
}

Address FindInBucket(Address first, uint32_t key)
{
    for (Address node = first; node; node = Load<Address>(node))
        if (Load<uint32_t>(node + kGlyphValue) == key)
            return node;
    return 0;
}

// The game's 0x00897CB2 (ret 8): the value inserted unless its character
// is there: a new node at the front of its bucket, one more. `result` gets
// the node, the table and whether it is new (a byte); EAX `result`.
Address GlyphInsertNoResize(Address self, Address result, Address value)
{
    const uint32_t key = Load<uint32_t>(value);
    const uint32_t bucket = Bucket(self, key);
    const Address first = Load<Address>(Load<Address>(self + kBuckets) + bucket * 4);
    if (const Address there = FindInBucket(first, key)) {
        Store(result, there);
        Store(result + 4, self);
        Store<uint8_t>(result + 8, 0);
        return result;
    }
    const Address node = This<Address>(kGlyphNode, self, value);
    Store(node, first);
    Store(Load<Address>(self + kBuckets) + bucket * 4, node);
    Store(self + kHashCount, Load<uint32_t>(self + kHashCount) + 1);
    Store(result, node);
    Store(result + 4, self);
    Store<uint8_t>(result + 8, 1);
    return result;
}

// The game's 0x008986BF (ret 8): the table grown for one more, then the
// insert. EAX `result`.
Address GlyphInsert(Address self, Address result, Address value)
{
    This<uint32_t>(kHashResize, self, Load<uint32_t>(self + kHashCount) + 1);
    This<Address>(kGlyphInsertNoResize, self, result, value);
    return result;
}

// ====================================================================================
// The holder, a list and a vector of pointers

// The game's 0x00897E36: the holder closed (f5's Close), its map taken
// down, its block freed.
void HolderDestruct(Address self)
{
    This<uint32_t>(kHolderClose, self);
    This<uint32_t>(kGlyphsDestruct, self + kHolderMap);
    if (const Address block = Load<Address>(self + kHolderBlock))
        Free(block);
}

// The game's 0x00897C8D (ret 0xC; ECX handed on to the node's maker, which
// does not read it): a node of the word at `value` linked in before
// `before`. The node into `result`; EAX `result`.
Address ListInsert(Address self, Address result, Address before, Address value)
{
    const Address node = This<Address>(kListNodeOf, self, value);
    const Address previous = Load<Address>(before + kPrevious);
    Store(node, before);
    Store(node + kPrevious, previous);
    Store(previous, node);
    Store(before + kPrevious, node);
    Store(result, node);
    return result;
}

// The game's 0x00897D22 (ret 0x14; the tag unread): a vector of pointers
// with no room for `count` copies of the pointer at `value` at `at`: new
// room for the size and the larger of the size and the count (unsigned),
// the pointers before `at`, the copies, the pointers after it unless
// `atEnd`, the old room freed. EAX is the end of the new room.
Address PointersOverflow(Address self, Address at, Address value, uint32_t count, uint8_t atEnd)
{
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(Load<Address>(self + kEnd) - Load<Address>(self)) >> 2);
    const uint32_t room = (size < count ? count : size) + size;
    const Address block = This<Address>(kPointersAllocate, self + 8, room);
    Address end = Cdecl<Address>(addr::STLport_CopyPointers, Load<Address>(self), at, block);
    end = Cdecl<Address>(kPointersFill, end, count, value);
    if (!atEnd)
        end = Cdecl<Address>(addr::STLport_CopyPointers, at, Load<Address>(self + kEnd), end);
    if (const Address old = Load<Address>(self))
        Free(old);
    Store(self, block);
    Store(self + kEnd, end);
    const Address roomEnd = block + room * 4;
    Store(self + kRoom, roomEnd);
    return roomEnd;
}

// ====================================================================================
// Detours

enum Group { kContainers, kShared, kGroups };

bool g_compare[kGroups] = {};

// Above 0 while the game's side of a comparison runs: our detours inside it
// pass through to the game's.
int g_inGame = 0;

struct GameSide {
    GameSide() { ++g_inGame; }
    ~GameSide() { --g_inGame; }
    GameSide(const GameSide&) = delete;
    GameSide& operator=(const GameSide&) = delete;
};

bool Game(Group group) { return g_inGame || g_compare[group]; }

std::string Words(std::initializer_list<uint32_t> words)
{
    std::string text;
    char one[12];
    for (const uint32_t w : words) {
        std::snprintf(one, sizeof one, "%X ", w);
        text += one;
    }
    return text;
}

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

using Fn0 = uint32_t(__fastcall*)(void*, void*);
using Fn1 = uint32_t(__fastcall*)(void*, void*, uint32_t);
using Fn2 = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t);
using Fn3 = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t);
using Fn5 = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using CFn2 = uint32_t(__cdecl*)(uint32_t, uint32_t);
using CFn3 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);
using CFn4 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);

// --- shared

Fn3 g_listInsert = nullptr;
Fn5 g_pointersOverflow = nullptr;

uint32_t __fastcall DListInsert(void* self, void*, uint32_t result, uint32_t before, uint32_t value)
{
    OPENSC4_FIRST("a list's insert 0x00897C8D");
    if (Game(kShared))
        return g_listInsert(self, nullptr, result, before, value);
    return ListInsert(At(self), result, before, value);
}

uint32_t __fastcall DPointersOverflow(void* self, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                      uint32_t atEnd)
{
    OPENSC4_FIRST("the pointers' overflow 0x00897D22");
    if (Game(kShared))
        return g_pointersOverflow(self, nullptr, at, value, tag, count, atEnd);
    return PointersOverflow(At(self), at, value, count, static_cast<uint8_t>(atEnd));
}

OPENSC4_THUNK(TListInsert, DListInsert, 12)
OPENSC4_THUNK(TPointersOverflow, DPointersOverflow, 20)

// --- containers

CFn3 g_placedFill = nullptr;
CFn3 g_textFind = nullptr;
CFn3 g_placedCopy = nullptr;
CFn2 g_placedConstruct = nullptr;
CFn3 g_placedFillN = nullptr;
CFn3 g_placedCopyBack = nullptr;
Fn1 g_glyphsEraseTree = nullptr;
Fn1 g_namesEraseTree = nullptr;
CFn3 g_textFindTagged = nullptr;
CFn3 g_placedCopyTagged = nullptr;
CFn3 g_placedUninitCopy = nullptr;
CFn3 g_placedCopyBackTagged = nullptr;
CFn3 g_placedFillNTagged = nullptr;
Fn1 g_mapFind = nullptr;
CFn2 g_glyphValueConstruct = nullptr;
CFn2 g_pairConstruct = nullptr;
Fn0 g_glyphsClear = nullptr;
Fn0 g_namesClear = nullptr;
Fn1 g_glyphNode = nullptr;
Fn1 g_mapNode = nullptr;
Fn2 g_textInsert = nullptr;
Fn0 g_glyphsDestruct = nullptr;
Fn0 g_namesDestruct = nullptr;
Fn2 g_glyphInsertNoResize = nullptr;
Fn5 g_mapInsertAt = nullptr;
Fn0 g_holderDestruct = nullptr;
Fn2 g_placedErase = nullptr;
Fn2 g_mapInsert = nullptr;
Fn5 g_placedOverflow = nullptr;
Fn3 g_textAllocateCopy = nullptr;
Fn2 g_glyphInsert = nullptr;
Fn1 g_textReserve = nullptr;
Fn3 g_placedFillInsert = nullptr;
Fn2 g_placedResize = nullptr;

uint32_t __cdecl DPlacedFill(uint32_t first, uint32_t last, uint32_t value)
{
    OPENSC4_FIRST("the placed glyphs' fill 0x00896BF6");
    if (Game(kContainers))
        return g_placedFill(first, last, value);
    return PlacedFill(first, last, value);
}

// A find: ours, then the game's, compared.
uint32_t CompareFind(const char* id, CFn3 game, Address (*ours)(Address, Address, Address), uint32_t first,
                     uint32_t last, uint32_t value)
{
    const Address mine = ours(first, last, value);
    uint32_t result;
    {
        GameSide side;
        result = game(first, last, value);
    }
    Report(id, Words({result}), Words({mine}));
    return result;
}

uint32_t __cdecl DTextFind(uint32_t first, uint32_t last, uint32_t value)
{
    OPENSC4_FIRST("the text's find 0x00896C19");
    if (g_inGame)
        return g_textFind(first, last, value);
    if (g_compare[kContainers])
        return CompareFind("fontcontainers.textfind", g_textFind, TextFind, first, last, value);
    return TextFind(first, last, value);
}

uint32_t __cdecl DTextFindTagged(uint32_t first, uint32_t last, uint32_t value)
{
    OPENSC4_FIRST("the text's find 0x00897599");
    if (g_inGame)
        return g_textFindTagged(first, last, value);
    if (g_compare[kContainers])
        return CompareFind("fontcontainers.textfindtagged", g_textFindTagged, TextFindTagged, first, last, value);
    return TextFindTagged(first, last, value);
}

uint32_t __cdecl DPlacedCopy(uint32_t first, uint32_t last, uint32_t to)
{
    OPENSC4_FIRST("the placed glyphs' copy 0x00896C90");
    if (Game(kContainers))
        return g_placedCopy(first, last, to);
    return PlacedCopy(first, last, to);
}

uint32_t __cdecl DPlacedConstruct(uint32_t at, uint32_t value)
{
    OPENSC4_FIRST("a placed glyph's construct 0x00896CC2");
    if (Game(kContainers))
        return g_placedConstruct(at, value);
    return PlacedConstruct(at, value);
}

uint32_t __cdecl DPlacedFillN(uint32_t at, uint32_t count, uint32_t value)
{
    OPENSC4_FIRST("the placed glyphs' fill_n 0x00896CD8");
    if (Game(kContainers))
        return g_placedFillN(at, count, value);
    return PlacedFillN(at, count, value);
}

uint32_t __cdecl DPlacedCopyBack(uint32_t first, uint32_t last, uint32_t to)
{
    OPENSC4_FIRST("the placed glyphs' copy back 0x00896CFD");
    if (Game(kContainers))
        return g_placedCopyBack(first, last, to);
    return PlacedCopyBack(first, last, to);
}

uint32_t __cdecl DPlacedCopyTagged(uint32_t first, uint32_t last, uint32_t to)
{
    OPENSC4_FIRST("the placed glyphs' copy 0x008975B4");
    if (Game(kContainers))
        return g_placedCopyTagged(first, last, to);
    return PlacedCopyTagged(first, last, to);
}

uint32_t __cdecl DPlacedUninitCopy(uint32_t first, uint32_t last, uint32_t to)
{
    OPENSC4_FIRST("the placed glyphs' uninitialized copy 0x008975D1");
    if (Game(kContainers))
        return g_placedUninitCopy(first, last, to);
    return PlacedUninitCopy(first, last, to);
}

uint32_t __cdecl DPlacedCopyBackTagged(uint32_t first, uint32_t last, uint32_t to)
{
    OPENSC4_FIRST("the placed glyphs' copy back 0x008975F7");
    if (Game(kContainers))
        return g_placedCopyBackTagged(first, last, to);
    return PlacedCopyBackTagged(first, last, to);
}

uint32_t __cdecl DPlacedFillNTagged(uint32_t at, uint32_t count, uint32_t value)
{
    OPENSC4_FIRST("the placed glyphs' fill_n 0x00897614");
    if (Game(kContainers))
        return g_placedFillNTagged(at, count, value);
    return PlacedFillNTagged(at, count, value);
}

uint32_t __fastcall DGlyphsEraseTree(void* self, void*, uint32_t node)
{
    OPENSC4_FIRST("the holder's map's EraseTree 0x0089753F");
    if (Game(kContainers))
        return g_glyphsEraseTree(self, nullptr, node);
    EraseTree(At(self), node, kGlyphsEraseTree);
    return 0;
}

uint32_t __fastcall DNamesEraseTree(void* self, void*, uint32_t node)
{
    OPENSC4_FIRST("the names' map's EraseTree 0x0089756C");
    if (Game(kContainers))
        return g_namesEraseTree(self, nullptr, node);
    EraseTree(At(self), node, kNamesEraseTree);
    return 0;
}

uint32_t __fastcall DMapFind(void* self, void*, uint32_t key)
{
    OPENSC4_FIRST("a map's Find 0x0089762F");
    if (g_inGame)
        return g_mapFind(self, nullptr, key);
    const Address mine = MapFind(At(self), key);
    if (!g_compare[kContainers])
        return mine;
    uint32_t result;
    {
        GameSide side;
        result = g_mapFind(self, nullptr, key);
    }
    Report("fontcontainers.mapfind", Words({result}), Words({mine}));
    return result;
}

uint32_t __cdecl DGlyphValueConstruct(uint32_t at, uint32_t value)
{
    OPENSC4_FIRST("a glyph's value 0x0089766B");
    if (Game(kContainers))
        return g_glyphValueConstruct(at, value);
    return GlyphValueConstruct(at, value);
}

uint32_t __cdecl DPairConstruct(uint32_t at, uint32_t pair)
{
    OPENSC4_FIRST("a map's pair 0x0089768B");
    if (Game(kContainers))
        return g_pairConstruct(at, pair);
    return PairConstruct(at, pair);
}

uint32_t __fastcall DGlyphsClear(void* self, void*)
{
    OPENSC4_FIRST("the holder's map's Clear 0x00897A1C");
    if (Game(kContainers))
        return g_glyphsClear(self, nullptr);
    return Clear(At(self), kGlyphsEraseTree);
}

uint32_t __fastcall DNamesClear(void* self, void*)
{
    OPENSC4_FIRST("the names' map's Clear 0x00897A45");
    if (Game(kContainers))
        return g_namesClear(self, nullptr);
    return Clear(At(self), kNamesEraseTree);
}

uint32_t __fastcall DGlyphNode(void* self, void*, uint32_t value)
{
    OPENSC4_FIRST("a glyph's node 0x00897A6E");
    if (Game(kContainers))
        return g_glyphNode(self, nullptr, value);
    return GlyphNode(value);
}

uint32_t __fastcall DMapNode(void* self, void*, uint32_t pair)
{
    OPENSC4_FIRST("a map's node 0x00897A91");
    if (Game(kContainers))
        return g_mapNode(self, nullptr, pair);
    return MapNode(pair);
}

uint32_t __fastcall DTextInsert(void* self, void*, uint32_t at, uint32_t value)
{
    OPENSC4_FIRST("the text's insert 0x00897BDC");
    if (Game(kContainers))
        return g_textInsert(self, nullptr, at, value);
    return TextInsert(At(self), at, value);
}

uint32_t __fastcall DGlyphsDestruct(void* self, void*)
{
    OPENSC4_FIRST("the holder's map's destructor 0x00897C5F");
    if (Game(kContainers))
        return g_glyphsDestruct(self, nullptr);
    Destruct(At(self), kGlyphsClear);
    return 0;
}

uint32_t __fastcall DNamesDestruct(void* self, void*)
{
    OPENSC4_FIRST("the names' map's destructor 0x00897C76");
    if (Game(kContainers))
        return g_namesDestruct(self, nullptr);
    Destruct(At(self), kNamesClear);
    return 0;
}

uint32_t __fastcall DGlyphInsertNoResize(void* self, void*, uint32_t result, uint32_t value)
{
    OPENSC4_FIRST("the glyph table's insert 0x00897CB2");
    if (g_inGame)
        return g_glyphInsertNoResize(self, nullptr, result, value);
    if (!g_compare[kContainers])
        return GlyphInsertNoResize(At(self), result, value);
    // Ours decides (the node there, or the bucket's first node the new one
    // goes before); the game's does it.
    const uint32_t key = Load<uint32_t>(value);
    const Address first = Load<Address>(Load<Address>(At(self) + kBuckets) + Bucket(At(self), key) * 4);
    const Address there = FindInBucket(first, key);
    uint32_t answer;
    {
        GameSide side;
        answer = g_glyphInsertNoResize(self, nullptr, result, value);
    }
    const uint8_t inserted = Load<uint8_t>(result + 8);
    const Address node = Load<Address>(result);
    Report("fontcontainers.glyphinsert",
           Words({inserted, inserted ? Load<Address>(node) : node, Load<uint32_t>(result + 4), answer}),
           Words({there ? 0u : 1u, there ? there : first, At(self), result}));
    return answer;
}

// While a game's insert runs in compare mode: its call of the insert under
// a parent, noted.
Decision* g_noted = nullptr;

uint32_t __fastcall DMapInsertAt(void* self, void*, uint32_t result, uint32_t x, uint32_t parent, uint32_t pair,
                                 uint32_t right)
{
    OPENSC4_FIRST("a map's InsertAt 0x00897DAC");
    if (g_noted) {
        *g_noted = Decision{true, 0, x, parent};
        g_noted = nullptr;
    }
    if (Game(kContainers))
        return g_mapInsertAt(self, nullptr, result, x, parent, pair, right);
    return MapInsertAt(At(self), result, x, parent, pair, right);
}

std::string DecisionText(const Decision& d)
{
    if (d.insert)
        return "insert " + Words({d.x, d.parent});
    return "there " + Words({d.node});
}

uint32_t __fastcall DMapInsert(void* self, void*, uint32_t result, uint32_t pair)
{
    OPENSC4_FIRST("a map's Insert 0x00897E7C");
    if (g_inGame)
        return g_mapInsert(self, nullptr, result, pair);
    if (!g_compare[kContainers])
        return MapInsert(At(self), result, pair, nullptr);
    Decision mine;
    MapInsert(At(self), result, pair, &mine);
    Decision theirs;
    uint32_t answer;
    {
        GameSide side;
        g_noted = &theirs;
        answer = g_mapInsert(self, nullptr, result, pair);
        g_noted = nullptr;
    }
    if (!theirs.insert)
        theirs.node = Load<Address>(result);
    const bool inserted = Load<uint8_t>(result + 4) != 0;
    Report("fontcontainers.mapinsert", DecisionText(theirs) + (inserted ? "1" : "0"),
           DecisionText(mine) + (mine.insert ? "1" : "0"));
    return answer;
}

uint32_t __fastcall DHolderDestruct(void* self, void*)
{
    OPENSC4_FIRST("the holder's destructor 0x00897E36");
    if (Game(kContainers))
        return g_holderDestruct(self, nullptr);
    HolderDestruct(At(self));
    return 0;
}

uint32_t __fastcall DPlacedErase(void* self, void*, uint32_t first, uint32_t last)
{
    OPENSC4_FIRST("the placed glyphs' erase 0x00897E56");
    if (Game(kContainers))
        return g_placedErase(self, nullptr, first, last);
    return PlacedErase(At(self), first, last);
}

uint32_t __fastcall DPlacedOverflow(void* self, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                    uint32_t atEnd)
{
    OPENSC4_FIRST("the placed glyphs' overflow 0x00897F04");
    if (Game(kContainers))
        return g_placedOverflow(self, nullptr, at, value, tag, count, atEnd);
    return PlacedOverflow(At(self), at, value, count, static_cast<uint8_t>(atEnd));
}

uint32_t __fastcall DTextAllocateCopy(void* self, void*, uint32_t count, uint32_t first, uint32_t last)
{
    OPENSC4_FIRST("the text's allocate and copy 0x00897FC2");
    if (Game(kContainers))
        return g_textAllocateCopy(self, nullptr, count, first, last);
    return TextAllocateCopy(At(self), count, first, last);
}

uint32_t __fastcall DGlyphInsert(void* self, void*, uint32_t result, uint32_t value)
{
    OPENSC4_FIRST("the glyph table's insert 0x008986BF");
    if (Game(kContainers))
        return g_glyphInsert(self, nullptr, result, value);
    return GlyphInsert(At(self), result, value);
}

uint32_t __fastcall DTextReserve(void* self, void*, uint32_t count)
{
    OPENSC4_FIRST("the text's reserve 0x008986E3");
    if (Game(kContainers))
        return g_textReserve(self, nullptr, count);
    return TextReserve(At(self), count);
}

uint32_t __fastcall DPlacedFillInsert(void* self, void*, uint32_t at, uint32_t count, uint32_t value)
{
    OPENSC4_FIRST("the placed glyphs' fill insert 0x0089874D");
    if (Game(kContainers))
        return g_placedFillInsert(self, nullptr, at, count, value);
    return PlacedFillInsert(At(self), at, count, value);
}

uint32_t __fastcall DPlacedResize(void* self, void*, uint32_t count, uint32_t value)
{
    OPENSC4_FIRST("the placed glyphs' resize 0x00899513");
    if (Game(kContainers))
        return g_placedResize(self, nullptr, count, value);
    return PlacedResize(At(self), count, value);
}

// The cdecl helpers with a tag (or a tag and a 0) after their arguments
// take only what they read: a cdecl caller cleans up the rest.
OPENSC4_CDECL_THUNK(TPlacedFill, DPlacedFill, 12)
OPENSC4_CDECL_THUNK(TTextFind, DTextFind, 12)
OPENSC4_CDECL_THUNK(TPlacedCopy, DPlacedCopy, 12)
OPENSC4_CDECL_THUNK(TPlacedConstruct, DPlacedConstruct, 8)
OPENSC4_CDECL_THUNK(TPlacedFillN, DPlacedFillN, 12)
OPENSC4_CDECL_THUNK(TPlacedCopyBack, DPlacedCopyBack, 12)
OPENSC4_THUNK(TGlyphsEraseTree, DGlyphsEraseTree, 4)
OPENSC4_THUNK(TNamesEraseTree, DNamesEraseTree, 4)
OPENSC4_CDECL_THUNK(TTextFindTagged, DTextFindTagged, 12)
OPENSC4_CDECL_THUNK(TPlacedCopyTagged, DPlacedCopyTagged, 12)
OPENSC4_CDECL_THUNK(TPlacedUninitCopy, DPlacedUninitCopy, 12)
OPENSC4_CDECL_THUNK(TPlacedCopyBackTagged, DPlacedCopyBackTagged, 12)
OPENSC4_CDECL_THUNK(TPlacedFillNTagged, DPlacedFillNTagged, 12)
OPENSC4_THUNK(TMapFind, DMapFind, 4)
OPENSC4_CDECL_THUNK(TGlyphValueConstruct, DGlyphValueConstruct, 8)
OPENSC4_CDECL_THUNK(TPairConstruct, DPairConstruct, 8)
OPENSC4_THUNK(TGlyphsClear, DGlyphsClear, 0)
OPENSC4_THUNK(TNamesClear, DNamesClear, 0)
OPENSC4_THUNK(TGlyphNode, DGlyphNode, 4)
OPENSC4_THUNK(TMapNode, DMapNode, 4)
OPENSC4_THUNK(TTextInsert, DTextInsert, 8)
OPENSC4_THUNK(TGlyphsDestruct, DGlyphsDestruct, 0)
OPENSC4_THUNK(TNamesDestruct, DNamesDestruct, 0)
OPENSC4_THUNK(TGlyphInsertNoResize, DGlyphInsertNoResize, 8)
OPENSC4_THUNK(TMapInsertAt, DMapInsertAt, 20)
OPENSC4_THUNK(THolderDestruct, DHolderDestruct, 0)
OPENSC4_THUNK(TPlacedErase, DPlacedErase, 8)
OPENSC4_THUNK(TMapInsert, DMapInsert, 8)
OPENSC4_THUNK(TPlacedOverflow, DPlacedOverflow, 20)
OPENSC4_THUNK(TTextAllocateCopy, DTextAllocateCopy, 12)
OPENSC4_THUNK(TGlyphInsert, DGlyphInsert, 8)
OPENSC4_THUNK(TTextReserve, DTextReserve, 4)
OPENSC4_THUNK(TPlacedFillInsert, DPlacedFillInsert, 12)
OPENSC4_THUNK(TPlacedResize, DPlacedResize, 8)

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void (*thunk)();
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

template <std::size_t N>
bool InstallAll(registry::Mode mode, Group group, const char* name, const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    g_compare[group] = mode == registry::Mode::Compare;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontcontainers: %s: %zu functions %s", name, N,
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallShared(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::cGZDBSegmentPackedFile_ListInsertBefore, &TListInsert, Slot(&g_listInsert)},
        {addr::GZPointerVector_InsertOverflow, &TPointersOverflow, Slot(&g_pointersOverflow)},
    };
    return InstallAll(mode, kShared, "shared", detours);
}

bool InstallContainers(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontPlaced_Fill, &TPlacedFill, Slot(&g_placedFill)},
        {addr::GZFontText_Find, &TTextFind, Slot(&g_textFind)},
        {addr::GZFontPlaced_Copy, &TPlacedCopy, Slot(&g_placedCopy)},
        {addr::GZFontPlaced_Construct, &TPlacedConstruct, Slot(&g_placedConstruct)},
        {addr::GZFontPlaced_FillN, &TPlacedFillN, Slot(&g_placedFillN)},
        {addr::GZFontPlaced_CopyBack, &TPlacedCopyBack, Slot(&g_placedCopyBack)},
        {addr::GZFontGlyphMap_EraseTree, &TGlyphsEraseTree, Slot(&g_glyphsEraseTree)},
        {addr::GZFontNameMap_EraseTree, &TNamesEraseTree, Slot(&g_namesEraseTree)},
        {addr::GZFontText_FindTagged, &TTextFindTagged, Slot(&g_textFindTagged)},
        {addr::GZFontPlaced_CopyTagged, &TPlacedCopyTagged, Slot(&g_placedCopyTagged)},
        {addr::GZFontPlaced_UninitCopy, &TPlacedUninitCopy, Slot(&g_placedUninitCopy)},
        {addr::GZFontPlaced_CopyBackTagged, &TPlacedCopyBackTagged, Slot(&g_placedCopyBackTagged)},
        {addr::GZFontPlaced_FillNTagged, &TPlacedFillNTagged, Slot(&g_placedFillNTagged)},
        {addr::GZFontGlyphMap_Find, &TMapFind, Slot(&g_mapFind)},
        {addr::GZGlyphTable_ValueConstruct, &TGlyphValueConstruct, Slot(&g_glyphValueConstruct)},
        {addr::GZFontGlyphMap_PairConstruct, &TPairConstruct, Slot(&g_pairConstruct)},
        {addr::GZFontGlyphMap_Clear, &TGlyphsClear, Slot(&g_glyphsClear)},
        {addr::GZFontNameMap_Clear, &TNamesClear, Slot(&g_namesClear)},
        {addr::GZGlyphTable_MakeNode, &TGlyphNode, Slot(&g_glyphNode)},
        {addr::GZFontGlyphMap_MakeNode, &TMapNode, Slot(&g_mapNode)},
        {addr::GZFontText_Insert, &TTextInsert, Slot(&g_textInsert)},
        {addr::GZFontGlyphMap_Destruct, &TGlyphsDestruct, Slot(&g_glyphsDestruct)},
        {addr::GZFontNameMap_Destruct, &TNamesDestruct, Slot(&g_namesDestruct)},
        {addr::GZGlyphTable_InsertNoResize, &TGlyphInsertNoResize, Slot(&g_glyphInsertNoResize)},
        {addr::GZFontGlyphMap_InsertAt, &TMapInsertAt, Slot(&g_mapInsertAt)},
        {addr::GZFontFile_Destruct, &THolderDestruct, Slot(&g_holderDestruct)},
        {addr::GZFontPlaced_Erase, &TPlacedErase, Slot(&g_placedErase)},
        {addr::GZFontGlyphMap_Insert, &TMapInsert, Slot(&g_mapInsert)},
        {addr::GZFontPlaced_InsertOverflow, &TPlacedOverflow, Slot(&g_placedOverflow)},
        {addr::GZFontText_AllocateCopy, &TTextAllocateCopy, Slot(&g_textAllocateCopy)},
        {addr::GZGlyphTable_Insert, &TGlyphInsert, Slot(&g_glyphInsert)},
        {addr::GZFontText_Reserve, &TTextReserve, Slot(&g_textReserve)},
        {addr::GZFontPlaced_FillInsert, &TPlacedFillInsert, Slot(&g_placedFillInsert)},
        {addr::GZFontPlaced_Resize, &TPlacedResize, Slot(&g_placedResize)},
    };
    return InstallAll(mode, kContainers, "containers", detours);
}

const bool g_registered[] = {
    registry::Register({"framework.font.font.containers", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontGlyphMap_Insert,
                        "Our font's containers: the maps of characters, the glyph table, the placed glyphs, the "
                        "UTF-16 text and the holder's destructor (34 detours; docs/fonts.md)",
                        true, InstallContainers}),
    registry::Register({"framework.font.font.shared", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZDBSegmentPackedFile_ListInsertBefore,
                        "Our STLport code in the font's range that other code calls too: a list's insert before a "
                        "node and a vector of pointers' insert overflow (2 detours; docs/fonts.md)",
                        true, InstallShared}),
};

} // namespace
} // namespace opensc4::fontcontainers
