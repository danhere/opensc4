// Our STLport tables of the framework's font system, f2b of docs/fonts.md
// ("f2b: the font system's tables are ours"): the map, list and vector code
// the linker put in the font system's range (0x00893D2C to 0x00895F35) for
// the faces, the aliases, the sizes by face and the byte vector a file's
// type reads into, and two one-line setters folded there. Each is a detour
// on the game's function, in two groups, off by default and installed at
// Attach (the tables are filled while the game starts):
//
//   framework.font.system.tables   43 functions only the font system calls:
//                                  the faces' map (its header, constructor,
//                                  destructor, clear, erase of a node and of
//                                  a subtree, insert and the insert under a
//                                  parent, its node, the pair of a path and
//                                  a face made, copied and destroyed, the
//                                  face made, copied and destroyed, the
//                                  face's list copied), the aliases' map
//                                  (the same, a name and a vector of names),
//                                  the map of sizes by face (destructor,
//                                  clear, erase of a subtree, of a range and
//                                  of a key, the bounds, insert and the
//                                  insert under a parent, the pair copied),
//                                  the byte vector (made, filled)
//   framework.font.system.shared   10 functions other code calls too: slot 7
//                                  (0x00893D87, two other classes' vtables
//                                  have it), the +4 vtable's slot 4
//                                  (0x00894F6D, 52 vtables), a tree's
//                                  distance (0x00893DEC), a list of pairs'
//                                  node (0x00893E0F, which every insert of
//                                  such a list makes, 0x0092119E), push_back
//                                  (0x0089408D) and insert of a range
//                                  (0x008940CD), the erase of a node of a
//                                  map keyed by a string with words after it
//                                  (0x0089461C), the sizes' pair (0x00894585)
//                                  and node (0x00894843), and their map's
//                                  constructor (0x0089508C)
//
// The trees are STLport's red-black trees: a map is the header's pointer,
// the count and an empty comparator at +8 (SC4String::Less, 0x004B5E10); a
// node is its colour (a byte, 0 red), parent, left, right and the value at
// +0x10; the header's parent is the root, its left and right the first and
// last nodes. The balancing (0x0040D5E0, 0x0040DAF0), the steps (0x0040D4C0,
// 0x0040D500), the allocator, the strings, the lists' insert (0x0092119E,
// 0x009A960E), the vectors of strings and the sizes' map's own header stay
// the game's, called at their addresses, as are these functions when they
// call one another (so with a group off its functions are the game's
// wherever they are called from).
//
// Compare mode runs ours and then the game's where both can run: the
// bounds, the equal range, the distance and the fill compare their results;
// the two setters keep what ours wrote aside, put the field back and compare
// with the game's. The three inserts run ours to its decision without its
// effects (the node already there, or the parent and side it would insert
// under) and then the game's, whose call of the insert under a parent is
// noted and compared. The rest (they allocate, free or link nodes) run the
// game's alone. While the game's side runs, our detours inside it pass
// through to the game's.
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

namespace opensc4::fonttables {
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
    log::Write("fonttables: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions we call

constexpr std::uintptr_t kStringConstruct = 0x00578390; // SC4String::SC4String, thiscall ()
constexpr std::uintptr_t kStringCopy = 0x00408BE0;      // thiscall (const String*, pos, count)
constexpr std::uintptr_t kStringDestroy = 0x00414250;   // thiscall ()
constexpr std::uintptr_t kVectorCopy = 0x0051FE90;      // a vector of strings: thiscall (const vector*)
constexpr std::uintptr_t kVectorFree = 0x00419140;      // thiscall ()
constexpr std::uintptr_t kListMake = 0x009A88C4;        // an empty list: thiscall (allocator*)
constexpr std::uintptr_t kListFree = 0x0096EDBC;        // thiscall ()
constexpr std::uintptr_t kListInsert = 0x0092119E;      // thiscall (result*, position by value, const pair*)
constexpr std::uintptr_t kListInsertEach = 0x009A960E;  // thiscall (position, first, last, tag*)
constexpr std::uintptr_t kPairPlace = 0x00997792;       // cdecl (at, const pair*): two words at `at` if not null
constexpr std::uintptr_t kBytesReserve = 0x00407E50;    // thiscall (count): begin = end, capacity after count
constexpr std::uintptr_t kRangesInit = 0x0094CB61;      // thiscall (comparator*, allocator*): header, empty

// These functions, at their addresses (ours when their group is on).
constexpr std::uintptr_t kFill = 0x00893DB8;
constexpr std::uintptr_t kDistance = 0x00893DEC;
constexpr std::uintptr_t kFacesHeader = 0x00893E2F;
constexpr std::uintptr_t kAliasesHeader = 0x00893E45;
constexpr std::uintptr_t kFacesInit = 0x00893F9C;
constexpr std::uintptr_t kAliasesInit = 0x00893FC6;
constexpr std::uintptr_t kLowerBound = 0x00893FF0;
constexpr std::uintptr_t kUpperBound = 0x00894028;
constexpr std::uintptr_t kBytesAllocate = 0x00894060;
constexpr std::uintptr_t kListInsertRange = 0x008940CD;
constexpr std::uintptr_t kListCopy = 0x008940EB;
constexpr std::uintptr_t kEqualRange = 0x0089411A;
constexpr std::uintptr_t kFaceFree = 0x0089415F;
constexpr std::uintptr_t kFacePairFree = 0x00894172;
constexpr std::uintptr_t kAliasPairFree = 0x00894185;
constexpr std::uintptr_t kRangePairCopy = 0x0089448D;
constexpr std::uintptr_t kFaceCopy = 0x008945AA;
constexpr std::uintptr_t kRangesEraseNode = 0x0089461C;
constexpr std::uintptr_t kFacesEraseTree = 0x00894657;
constexpr std::uintptr_t kAliasesEraseTree = 0x0089468C;
constexpr std::uintptr_t kRangesEraseTree = 0x008946C1;
constexpr std::uintptr_t kFacePairCopy = 0x008946F6;
constexpr std::uintptr_t kAliasPairCopy = 0x0089471B;
constexpr std::uintptr_t kFacesClear = 0x00894784;
constexpr std::uintptr_t kAliasesClear = 0x008947AD;
constexpr std::uintptr_t kRangesClear = 0x008947D6;
constexpr std::uintptr_t kRangesEraseRange = 0x008947FF;
constexpr std::uintptr_t kRangesNode = 0x00894843;
constexpr std::uintptr_t kRangesInsertAt = 0x00894E55;
constexpr std::uintptr_t kFacesNode = 0x00894EEA;
constexpr std::uintptr_t kAliasesNode = 0x00894F0B;
constexpr std::uintptr_t kFacesInsertAt = 0x00895136;
constexpr std::uintptr_t kAliasesInsertAt = 0x008951CB;

// ====================================================================================
// Layout

// A map: the header, the count, the comparator.
constexpr Address kMapHeader = 0x00;
constexpr Address kMapCount = 0x04;
constexpr Address kMapCompare = 0x08;
// A node (and the header): colour, parent, left, right, the value.
constexpr Address kColour = 0x00;
constexpr Address kParent = 0x04;
constexpr Address kLeft = 0x08;
constexpr Address kRight = 0x0C;
constexpr Address kValue = 0x10;
// The nodes: a face's (a path and 0x24 bytes), an alias's (a name and a
// vector), a size range's (a name and two words); the faces' and aliases'
// headers are made as large as their nodes.
constexpr uint32_t kFaceNodeBytes = 0x48;
constexpr uint32_t kAliasNodeBytes = 0x30;
constexpr uint32_t kRangeNodeBytes = 0x2C;
// A list of pairs' node: next, previous, two words.
constexpr uint32_t kListNodeBytes = 0x10;
constexpr Address kListNodePair = 0x08;
// A face (the value of the faces' map): its list, its name, three words.
constexpr Address kFaceName = 0x04;
constexpr Address kFaceWords = 0x18;
// A pair: its key (a string) and the value after it.
constexpr Address kPairValue = 0x14;
// The two setters' fields.
constexpr Address kDrawMode = 0x14;
constexpr Address kServiceId = 0x0C;

Address Header(Address map) { return Load<Address>(map + kMapHeader); }
Address Allocate(uint32_t bytes) { return At(Cdecl<void*>(addr::SC4_stl_allocate, bytes)); }
void Free(Address p) { Cdecl<void>(addr::SC4_Free, Ptr(p)); }
Address Next(Address node) { return Cdecl<Address>(addr::SC4Tree_Next, node); }
Address Previous(Address node) { return Cdecl<Address>(addr::cSC4Advisor_F40D500, node); }
void CopyString(Address s, Address from) { This<void*>(kStringCopy, s, from, uint32_t{0}, uint32_t{0xFFFFFFFF}); }

// The map's comparator: whether the key at `a` is less than the one at `b`.
bool Less(Address map, Address a, Address b) { return This<uint8_t>(addr::SC4String_Less, map + kMapCompare, a, b) != 0; }

// ====================================================================================
// The two setters

// The game's 0x00893D87 (slot 7 of 0x00AC5488, ret 4): the draw mode fonts
// get by default. EAX is the mode.
uint32_t SetDrawMode(Address self, uint32_t mode)
{
    Store(self + kDrawMode, mode);
    return mode;
}

// The game's 0x00894F6D (the +4 vtable's slot 4, ret 4): the word at +0xC
// (the system's +0x10, its class). EAX is the word.
uint32_t SetServiceId(Address self, uint32_t id)
{
    Store(self + kServiceId, id);
    return id;
}

// ====================================================================================
// The byte vector

// The game's 0x00893DB8 (cdecl): `count` copies of the byte at `value` from
// `at`, if `count` is above 0, signed; the byte is read again for each.
// Returns where it stopped.
Address Fill(Address at, int32_t count, Address value)
{
    if (count > 0) {
        do {
            const uint8_t byte = Load<uint8_t>(value);
            ++at;
            Store(at - 1, byte);
        } while (--count != 0);
    }
    return at;
}

// The game's 0x00894060 (thiscall, ret 0xC; the third argument unused):
// room for `count` bytes and one more, `count` of `value`, then a zero.
// Returns the end.
Address BytesAllocate(Address self, uint32_t count, uint32_t value)
{
    This<void>(kBytesReserve, self, count + 1);
    const Address end = Cdecl<Address>(kFill, Load<Address>(self), count, At(&value));
    Store(self + 4, end);
    Store<uint8_t>(end, 0);
    return end;
}

// The game's 0x008940A7 (thiscall, ret 8): a vector of `count` bytes of
// `value` and a zero, from nothing.
Address BytesConstruct(Address self, uint32_t count, uint32_t value)
{
    Store<uint32_t>(self, 0);
    Store<uint32_t>(self + 4, 0);
    Store<uint32_t>(self + 8, 0);
    uint8_t allocator = 0;
    This<Address>(kBytesAllocate, self, count, value, At(&allocator));
    return self;
}

// ====================================================================================
// Lists of pairs (a face's fonts and their styles)

// The game's 0x00893E0F (ret 4; ECX unused): a node with a copy of the pair,
// its links left as they were.
Address ListNode(Address pair)
{
    const Address node = Allocate(kListNodeBytes);
    Cdecl<void*>(kPairPlace, node + kListNodePair, pair);
    return node;
}

// The game's 0x0089408D (thiscall, ret 4): the pair added at the end.
// EAX is what the list's insert answered.
Address ListPushBack(Address self, Address pair)
{
    Address result = 0;
    return This<Address>(kListInsert, self, At(&result), Load<Address>(self), pair);
}

// The game's 0x008940CD (thiscall, ret 0xC): every pair of [first, last)
// inserted before `position`. EAX is what the insert answered.
Address ListInsertRange(Address self, Address position, Address first, Address last)
{
    uint8_t tag = 0;
    return This<Address>(kListInsertEach, self, position, first, last, At(&tag));
}

// The game's 0x008940EB (thiscall, ret 4): a copy of a list.
Address ListCopy(Address self, Address other)
{
    uint8_t allocator = 0;
    This<Address>(kListMake, self, At(&allocator));
    const Address theirs = Load<Address>(other);
    This<Address>(kListInsertRange, self, Load<Address>(Load<Address>(self)), Load<Address>(theirs), theirs);
    return self;
}

// ====================================================================================
// Faces, aliases and size ranges: the values and pairs

// The game's 0x00894144 (thiscall): a face, empty: its list and its name
// (the three words after them as they were).
Address FaceConstruct(Address self)
{
    uint8_t allocator = 0;
    This<Address>(kListMake, self, At(&allocator));
    This<void*>(kStringConstruct, self + kFaceName);
    return self;
}

// The game's 0x0089415F (thiscall): a face's name, then its list.
uint32_t FaceDestruct(Address self)
{
    This<void>(kStringDestroy, self + kFaceName);
    return This<uint32_t>(kListFree, self);
}

// The game's 0x008945AA (thiscall, ret 4): a copy of a face.
Address FaceCopy(Address self, Address other)
{
    This<Address>(kListCopy, self, other);
    CopyString(self + kFaceName, other + kFaceName);
    Store(self + kFaceWords, Load<uint32_t>(other + kFaceWords));
    Store(self + kFaceWords + 4, Load<uint32_t>(other + kFaceWords + 4));
    Store(self + kFaceWords + 8, Load<uint32_t>(other + kFaceWords + 8));
    return self;
}

// The game's 0x00894740 (thiscall, ret 8): a path and a face.
Address FacePairConstruct(Address self, Address key, Address face)
{
    CopyString(self, key);
    This<Address>(kFaceCopy, self + kPairValue, face);
    return self;
}

// The game's 0x008946F6 (thiscall, ret 4).
Address FacePairCopy(Address self, Address other)
{
    CopyString(self, other);
    This<Address>(kFaceCopy, self + kPairValue, other + kPairValue);
    return self;
}

// The game's 0x00894172 (thiscall): the face, then the path.
uint32_t FacePairDestruct(Address self)
{
    This<uint32_t>(kFaceFree, self + kPairValue);
    return This<uint32_t>(kStringDestroy, self);
}

// The game's 0x00894762 (thiscall, ret 8): an alias's name and its names.
Address AliasPairConstruct(Address self, Address key, Address names)
{
    CopyString(self, key);
    This<void*>(kVectorCopy, self + kPairValue, names);
    return self;
}

// The game's 0x0089471B (thiscall, ret 4).
Address AliasPairCopy(Address self, Address other)
{
    CopyString(self, other);
    This<void*>(kVectorCopy, self + kPairValue, other + kPairValue);
    return self;
}

// The game's 0x00894185 (thiscall): the names, then the alias's name.
uint32_t AliasPairDestruct(Address self)
{
    This<void>(kVectorFree, self + kPairValue);
    return This<uint32_t>(kStringDestroy, self);
}

// The game's 0x00894585 (thiscall, ret 8): a face's name and the two words
// at `words`.
Address RangePairConstruct(Address self, Address key, Address words)
{
    CopyString(self, key);
    Store(self + kPairValue, Load<uint32_t>(words));
    Store(self + kPairValue + 4, Load<uint32_t>(words + 4));
    return self;
}

// The game's 0x0089448D (thiscall, ret 4).
Address RangePairCopy(Address self, Address other)
{
    CopyString(self, other);
    Store(self + kPairValue, Load<uint32_t>(other + kPairValue));
    Store(self + kPairValue + 4, Load<uint32_t>(other + kPairValue + 4));
    return self;
}

// ====================================================================================
// The maps

// The game's 0x00893E2F and 0x00893E45 (thiscall, ret 4; the allocator
// unused): the header, as large as a node.
Address MakeHeader(Address self, uint32_t bytes)
{
    Store<uint32_t>(self + kMapHeader, 0);
    Store(self + kMapHeader, Allocate(bytes));
    return self;
}

// The game's 0x00893F9C and 0x00893FC6 (thiscall (comparator*, allocator*),
// ret 8): the header made, the map empty: the header red, without a root,
// its own first and last.
Address MapInit(Address self, std::uintptr_t makeHeader, Address allocator)
{
    This<Address>(makeHeader, self, allocator);
    Store<uint32_t>(self + kMapCount, 0);
    const Address header = Header(self);
    Store<uint8_t>(header + kColour, 0);
    Store<uint32_t>(header + kParent, 0);
    Store(header + kLeft, header);
    Store(header + kRight, header);
    return self;
}

// The game's 0x0089505A, 0x00895073 and 0x0089508C (thiscall): a map made
// by its initialiser with a byte of the stack as comparator and allocator.
Address MapConstruct(Address self, std::uintptr_t init)
{
    uint8_t unused = 0;
    This<Address>(init, self, At(&unused), At(&unused));
    return self;
}

// The game's 0x00894657, 0x0089468C and 0x008946C1 (thiscall, ret 4): the
// subtree at `node` destroyed and freed, right subtrees first (by the
// function itself, at its address), then down the left.
void EraseTree(Address self, Address node, std::uintptr_t itself, std::uintptr_t destroyValue)
{
    while (node) {
        This<void>(itself, self, Load<Address>(node + kRight));
        const Address left = Load<Address>(node + kLeft);
        This<uint32_t>(destroyValue, node + kValue);
        Free(node);
        node = left;
    }
}

// The game's 0x00894784, 0x008947AD and 0x008947D6 (thiscall): if not empty,
// every node erased and the header as a new map's.
void Clear(Address self, std::uintptr_t eraseTree)
{
    if (Load<uint32_t>(self + kMapCount) == 0)
        return;
    This<void>(eraseTree, self, Load<Address>(Header(self) + kParent));
    Store(Header(self) + kLeft, Header(self));
    Store<uint32_t>(Header(self) + kParent, 0);
    Store(Header(self) + kRight, Header(self));
    Store<uint32_t>(self + kMapCount, 0);
}

// The game's 0x00894DC7, 0x00894DDE and 0x00894DF5 (thiscall): cleared, and
// the header freed.
void Destruct(Address self, std::uintptr_t clear)
{
    This<void>(clear, self);
    if (const Address header = Header(self))
        Free(header);
}

// The game's 0x008945E1 and 0x0089461C (thiscall, ret 4): a node taken out
// of the tree, its value destroyed (before the node is tested), freed if not
// null, one fewer.
void EraseNode(Address self, Address node, std::uintptr_t destroyValue)
{
    const Address header = Header(self);
    const Address out = Cdecl<Address>(addr::SC4Tree_RebalanceForErase, node, header + kParent, header + kLeft,
                                       header + kRight);
    This<uint32_t>(destroyValue, out + kValue);
    if (out)
        Free(out);
    Store(self + kMapCount, Load<uint32_t>(self + kMapCount) - 1);
}

// The game's 0x00894843, 0x00894EEA and 0x00894F0B (ret 4; ECX unused): a
// node with a copy of the pair, unless its value's place is null.
Address MakeNode(Address pair, uint32_t bytes, std::uintptr_t copyPair)
{
    const Address node = Allocate(bytes);
    if (node + kValue != 0)
        This<Address>(copyPair, node + kValue, pair);
    return node;
}

// The game's 0x00894E55, 0x00895136 and 0x008951CB (thiscall (result*, x,
// parent, pair*, right), ret 0x14): a node of `pair` as the parent's left
// child if the parent is the header, else its right one if `right`, else
// the left one if `x`, else the left one if the pair's key is less than the
// parent's; the first and last kept, the tree balanced, one more. The node
// into `result`; EAX `result`.
Address InsertAt(Address self, Address result, Address x, Address parent, Address pair, Address right,
                 std::uintptr_t makeNode)
{
    bool left;
    if (parent == Header(self))
        left = true;
    else if (right != 0)
        left = false;
    else if (x != 0)
        left = true;
    else
        left = Less(self, pair, parent + kValue);
    Address node;
    if (left) {
        node = This<Address>(makeNode, self, pair);
        Store(parent + kLeft, node);
        const Address header = Header(self);
        if (parent == header) {
            Store(header + kParent, node);
            Store(Header(self) + kRight, node);
        } else if (parent == Load<Address>(header + kLeft)) {
            Store(header + kLeft, node);
        }
    } else {
        node = This<Address>(makeNode, self, pair);
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

// The game's 0x008953A9, 0x0089543A and 0x008950A5 (thiscall (result*,
// pair*), ret 8): the pair's node if its key is not there yet (inserted
// under the last node the search reached, by the insert at `insertAt`),
// else the node that has it; the byte after it 1 if inserted. With
// `decided`, only the decision, into it.
Address InsertUnique(Address self, Address result, Address pair, std::uintptr_t insertAt, Decision* decided)
{
    Address parent = Header(self);
    Address x = Load<Address>(parent + kParent);
    bool less = true;
    while (x) {
        parent = x;
        less = Less(self, pair, x + kValue);
        x = less ? Load<Address>(x + kLeft) : Load<Address>(x + kRight);
    }
    Address before = parent;
    bool insert = less && before == Load<Address>(Header(self) + kLeft);
    if (!insert) {
        if (less)
            before = Previous(before);
        insert = Less(self, before + kValue, pair);
    }
    if (decided) {
        *decided = insert ? Decision{true, 0, x, parent} : Decision{false, before, 0, 0};
        return result;
    }
    if (insert) {
        Address node = 0;
        This<Address>(insertAt, self, At(&node), x, parent, pair, Address{0});
        Store<uint8_t>(result + 4, 1);
        Store(result, node);
    } else {
        Store<uint8_t>(result + 4, 0);
        Store(result, before);
    }
    return result;
}

// The game's 0x00893FF0 (thiscall, ret 4): the first node whose key is not
// less than `key`, or the header.
Address LowerBound(Address self, Address key)
{
    Address bound = Header(self);
    for (Address x = Load<Address>(bound + kParent); x;) {
        if (Less(self, x + kValue, key)) {
            x = Load<Address>(x + kRight);
        } else {
            bound = x;
            x = Load<Address>(x + kLeft);
        }
    }
    return bound;
}

// The game's 0x00894028 (thiscall, ret 4): the first node whose key is
// greater than `key`, or the header.
Address UpperBound(Address self, Address key)
{
    Address bound = Header(self);
    for (Address x = Load<Address>(bound + kParent); x;) {
        if (Less(self, key, x + kValue)) {
            bound = x;
            x = Load<Address>(x + kLeft);
        } else {
            x = Load<Address>(x + kRight);
        }
    }
    return bound;
}

// The game's 0x0089411A (thiscall (result*, key*), ret 8): the lower and
// upper bounds into `result` (the upper found first).
Address EqualRange(Address self, Address result, Address key)
{
    const Address upper = This<Address>(kUpperBound, self, key);
    const Address lower = This<Address>(kLowerBound, self, key);
    Store(result, lower);
    Store(result + 4, upper);
    return result;
}

// The game's 0x00893DEC (cdecl (first*, last*, tag)): the steps from the
// first node to the last.
uint32_t Distance(Address first, Address last)
{
    Address node = Load<Address>(first);
    const Address end = Load<Address>(last);
    uint32_t count = 0;
    while (node != end) {
        node = Next(node);
        ++count;
    }
    return count;
}

// The game's 0x008947FF (thiscall (first, last), ret 8): the nodes of
// [first, last) erased, one by one, unless they are all of them: cleared.
void RangesEraseRange(Address self, Address first, Address last)
{
    const Address header = Header(self);
    if (first == Load<Address>(header + kLeft) && last == header) {
        This<void>(kRangesClear, self);
        return;
    }
    while (first != last) {
        const Address node = first;
        first = Next(node);
        This<void>(kRangesEraseNode, self, node);
    }
}

// The game's 0x00894E0C (thiscall, ret 4): the nodes of `key` erased.
// Returns how many there were.
uint32_t RangesEraseKey(Address self, Address key)
{
    Address range[2] = {};
    This<Address>(kEqualRange, self, At(range), key);
    uint8_t tag = 0;
    const uint32_t count = Cdecl<uint32_t>(kDistance, At(&range[0]), At(&range[1]), At(&tag));
    This<void>(kRangesEraseRange, self, range[0], range[1]);
    return count;
}

// ====================================================================================
// Detours

enum Group { kTables, kShared, kGroups };
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

// --- shared

Fn1 g_setDrawMode = nullptr;
Fn1 g_setServiceId = nullptr;
CFn2 g_distance = nullptr;
Fn1 g_listNode = nullptr;
Fn1 g_listPushBack = nullptr;
Fn3 g_listInsertRange = nullptr;
Fn1 g_rangesEraseNode = nullptr;
Fn2 g_rangePairConstruct = nullptr;
Fn1 g_rangesNode = nullptr;
Fn0 g_rangesConstruct = nullptr;

// A setter: ours, what it wrote kept aside and the field put back, then the
// game's, compared.
uint32_t CompareSetter(const char* id, void* self, Address field, uint32_t value, Fn1 game,
                       uint32_t (*ours)(Address, uint32_t))
{
    const Address at = At(self) + field;
    const uint32_t before = Load<uint32_t>(at);
    const uint32_t mineResult = ours(At(self), value);
    const uint32_t mine = Load<uint32_t>(at);
    Store(at, before);
    uint32_t result;
    {
        GameSide side;
        result = game(self, nullptr, value);
    }
    Report(id, Words({Load<uint32_t>(at), result}), Words({mine, mineResult}));
    return result;
}

uint32_t __fastcall DSetDrawMode(void* self, void*, uint32_t mode)
{
    OPENSC4_FIRST("SetDrawMode 0x00893D87");
    if (g_inGame)
        return g_setDrawMode(self, nullptr, mode);
    if (g_compare[kShared])
        return CompareSetter("fonttables.setdrawmode", self, kDrawMode, mode, g_setDrawMode, SetDrawMode);
    return SetDrawMode(At(self), mode);
}

uint32_t __fastcall DSetServiceId(void* self, void*, uint32_t id)
{
    OPENSC4_FIRST("SetServiceID 0x00894F6D");
    if (g_inGame)
        return g_setServiceId(self, nullptr, id);
    if (g_compare[kShared])
        return CompareSetter("fonttables.setserviceid", self, kServiceId, id, g_setServiceId, SetServiceId);
    return SetServiceId(At(self), id);
}

uint32_t __cdecl DDistance(uint32_t first, uint32_t last)
{
    OPENSC4_FIRST("Distance 0x00893DEC");
    if (g_inGame)
        return g_distance(first, last);
    const uint32_t mine = Distance(first, last);
    if (!g_compare[kShared])
        return mine;
    uint32_t result;
    {
        GameSide side;
        result = g_distance(first, last);
    }
    Report("fonttables.distance", Words({result}), Words({mine}));
    return result;
}

uint32_t __fastcall DListNode(void* self, void*, uint32_t pair)
{
    OPENSC4_FIRST("a list's node 0x00893E0F");
    if (Game(kShared))
        return g_listNode(self, nullptr, pair);
    return ListNode(pair);
}

uint32_t __fastcall DListPushBack(void* self, void*, uint32_t pair)
{
    OPENSC4_FIRST("a list's PushBack 0x0089408D");
    if (Game(kShared))
        return g_listPushBack(self, nullptr, pair);
    return ListPushBack(At(self), pair);
}

uint32_t __fastcall DListInsertRange(void* self, void*, uint32_t position, uint32_t first, uint32_t last)
{
    OPENSC4_FIRST("a list's InsertRange 0x008940CD");
    if (Game(kShared))
        return g_listInsertRange(self, nullptr, position, first, last);
    return ListInsertRange(At(self), position, first, last);
}

uint32_t __fastcall DRangesEraseNode(void* self, void*, uint32_t node)
{
    OPENSC4_FIRST("the sizes' EraseNode 0x0089461C");
    if (Game(kShared))
        return g_rangesEraseNode(self, nullptr, node);
    EraseNode(At(self), node, kStringDestroy);
    return 0;
}

uint32_t __fastcall DRangePairConstruct(void* self, void*, uint32_t key, uint32_t words)
{
    OPENSC4_FIRST("the sizes' pair 0x00894585");
    if (Game(kShared))
        return g_rangePairConstruct(self, nullptr, key, words);
    return RangePairConstruct(At(self), key, words);
}

uint32_t __fastcall DRangesNode(void* self, void*, uint32_t pair)
{
    OPENSC4_FIRST("the sizes' node 0x00894843");
    if (Game(kShared))
        return g_rangesNode(self, nullptr, pair);
    return MakeNode(pair, kRangeNodeBytes, kRangePairCopy);
}

uint32_t __fastcall DRangesConstruct(void* self, void*)
{
    OPENSC4_FIRST("the sizes' constructor 0x0089508C");
    if (Game(kShared))
        return g_rangesConstruct(self, nullptr);
    return MapConstruct(At(self), kRangesInit);
}

OPENSC4_THUNK(TSetDrawMode, DSetDrawMode, 4)
OPENSC4_THUNK(TSetServiceId, DSetServiceId, 4)
OPENSC4_CDECL_THUNK(TDistance, DDistance, 8)
OPENSC4_THUNK(TListNode, DListNode, 4)
OPENSC4_THUNK(TListPushBack, DListPushBack, 4)
OPENSC4_THUNK(TListInsertRange, DListInsertRange, 12)
OPENSC4_THUNK(TRangesEraseNode, DRangesEraseNode, 4)
OPENSC4_THUNK(TRangePairConstruct, DRangePairConstruct, 8)
OPENSC4_THUNK(TRangesNode, DRangesNode, 4)
OPENSC4_THUNK(TRangesConstruct, DRangesConstruct, 0)

// --- tables

CFn3 g_fill = nullptr;
Fn3 g_bytesAllocate = nullptr;
Fn2 g_bytesConstruct = nullptr;
Fn1 g_listCopy = nullptr;
Fn0 g_faceConstruct = nullptr;
Fn0 g_faceDestruct = nullptr;
Fn1 g_faceCopy = nullptr;
Fn2 g_facePairConstruct = nullptr;
Fn1 g_facePairCopy = nullptr;
Fn0 g_facePairDestruct = nullptr;
Fn2 g_aliasPairConstruct = nullptr;
Fn1 g_aliasPairCopy = nullptr;
Fn0 g_aliasPairDestruct = nullptr;
Fn1 g_rangePairCopy = nullptr;
Fn1 g_facesHeader = nullptr;
Fn1 g_aliasesHeader = nullptr;
Fn2 g_facesInit = nullptr;
Fn2 g_aliasesInit = nullptr;
Fn0 g_facesConstruct = nullptr;
Fn0 g_aliasesConstruct = nullptr;
Fn1 g_facesEraseTree = nullptr;
Fn1 g_aliasesEraseTree = nullptr;
Fn1 g_rangesEraseTree = nullptr;
Fn0 g_facesClear = nullptr;
Fn0 g_aliasesClear = nullptr;
Fn0 g_rangesClear = nullptr;
Fn0 g_facesDestruct = nullptr;
Fn0 g_aliasesDestruct = nullptr;
Fn0 g_rangesDestruct = nullptr;
Fn1 g_facesEraseNode = nullptr;
Fn1 g_facesNode = nullptr;
Fn1 g_aliasesNode = nullptr;
Fn5 g_facesInsertAt = nullptr;
Fn5 g_aliasesInsertAt = nullptr;
Fn5 g_rangesInsertAt = nullptr;
Fn2 g_facesInsert = nullptr;
Fn2 g_aliasesInsert = nullptr;
Fn2 g_rangesInsert = nullptr;
Fn1 g_lowerBound = nullptr;
Fn1 g_upperBound = nullptr;
Fn2 g_equalRange = nullptr;
Fn2 g_rangesEraseRange = nullptr;
Fn1 g_rangesEraseKey = nullptr;

uint32_t __cdecl DFill(uint32_t at, uint32_t count, uint32_t value)
{
    OPENSC4_FIRST("Fill 0x00893DB8");
    if (g_inGame)
        return g_fill(at, count, value);
    const uint32_t mine = Fill(at, static_cast<int32_t>(count), value);
    if (!g_compare[kTables])
        return mine;
    uint32_t result;
    {
        GameSide side;
        result = g_fill(at, count, value);
    }
    Report("fonttables.fill", Words({result}), Words({mine}));
    return result;
}

uint32_t __fastcall DBytesAllocate(void* self, void*, uint32_t count, uint32_t value, uint32_t allocator)
{
    OPENSC4_FIRST("the bytes' Allocate 0x00894060");
    if (Game(kTables))
        return g_bytesAllocate(self, nullptr, count, value, allocator);
    return BytesAllocate(At(self), count, value);
}

uint32_t __fastcall DBytesConstruct(void* self, void*, uint32_t count, uint32_t value)
{
    OPENSC4_FIRST("the bytes' constructor 0x008940A7");
    if (Game(kTables))
        return g_bytesConstruct(self, nullptr, count, value);
    return BytesConstruct(At(self), count, value);
}

uint32_t __fastcall DListCopy(void* self, void*, uint32_t other)
{
    OPENSC4_FIRST("a list's copy 0x008940EB");
    if (Game(kTables))
        return g_listCopy(self, nullptr, other);
    return ListCopy(At(self), other);
}

uint32_t __fastcall DFaceConstruct(void* self, void*)
{
    OPENSC4_FIRST("a face's constructor 0x00894144");
    if (Game(kTables))
        return g_faceConstruct(self, nullptr);
    return FaceConstruct(At(self));
}

uint32_t __fastcall DFaceDestruct(void* self, void*)
{
    OPENSC4_FIRST("a face's destructor 0x0089415F");
    if (Game(kTables))
        return g_faceDestruct(self, nullptr);
    return FaceDestruct(At(self));
}

uint32_t __fastcall DFaceCopy(void* self, void*, uint32_t other)
{
    OPENSC4_FIRST("a face's copy 0x008945AA");
    if (Game(kTables))
        return g_faceCopy(self, nullptr, other);
    return FaceCopy(At(self), other);
}

uint32_t __fastcall DFacePairConstruct(void* self, void*, uint32_t key, uint32_t face)
{
    OPENSC4_FIRST("the faces' pair 0x00894740");
    if (Game(kTables))
        return g_facePairConstruct(self, nullptr, key, face);
    return FacePairConstruct(At(self), key, face);
}

uint32_t __fastcall DFacePairCopy(void* self, void*, uint32_t other)
{
    OPENSC4_FIRST("the faces' pair copy 0x008946F6");
    if (Game(kTables))
        return g_facePairCopy(self, nullptr, other);
    return FacePairCopy(At(self), other);
}

uint32_t __fastcall DFacePairDestruct(void* self, void*)
{
    OPENSC4_FIRST("the faces' pair destructor 0x00894172");
    if (Game(kTables))
        return g_facePairDestruct(self, nullptr);
    return FacePairDestruct(At(self));
}

uint32_t __fastcall DAliasPairConstruct(void* self, void*, uint32_t key, uint32_t names)
{
    OPENSC4_FIRST("the aliases' pair 0x00894762");
    if (Game(kTables))
        return g_aliasPairConstruct(self, nullptr, key, names);
    return AliasPairConstruct(At(self), key, names);
}

uint32_t __fastcall DAliasPairCopy(void* self, void*, uint32_t other)
{
    OPENSC4_FIRST("the aliases' pair copy 0x0089471B");
    if (Game(kTables))
        return g_aliasPairCopy(self, nullptr, other);
    return AliasPairCopy(At(self), other);
}

uint32_t __fastcall DAliasPairDestruct(void* self, void*)
{
    OPENSC4_FIRST("the aliases' pair destructor 0x00894185");
    if (Game(kTables))
        return g_aliasPairDestruct(self, nullptr);
    return AliasPairDestruct(At(self));
}

uint32_t __fastcall DRangePairCopy(void* self, void*, uint32_t other)
{
    OPENSC4_FIRST("the sizes' pair copy 0x0089448D");
    if (Game(kTables))
        return g_rangePairCopy(self, nullptr, other);
    return RangePairCopy(At(self), other);
}

uint32_t __fastcall DFacesHeader(void* self, void*, uint32_t allocator)
{
    OPENSC4_FIRST("the faces' header 0x00893E2F");
    if (Game(kTables))
        return g_facesHeader(self, nullptr, allocator);
    return MakeHeader(At(self), kFaceNodeBytes);
}

uint32_t __fastcall DAliasesHeader(void* self, void*, uint32_t allocator)
{
    OPENSC4_FIRST("the aliases' header 0x00893E45");
    if (Game(kTables))
        return g_aliasesHeader(self, nullptr, allocator);
    return MakeHeader(At(self), kAliasNodeBytes);
}

uint32_t __fastcall DFacesInit(void* self, void*, uint32_t comparator, uint32_t allocator)
{
    OPENSC4_FIRST("the faces' Init 0x00893F9C");
    if (Game(kTables))
        return g_facesInit(self, nullptr, comparator, allocator);
    return MapInit(At(self), kFacesHeader, allocator);
}

uint32_t __fastcall DAliasesInit(void* self, void*, uint32_t comparator, uint32_t allocator)
{
    OPENSC4_FIRST("the aliases' Init 0x00893FC6");
    if (Game(kTables))
        return g_aliasesInit(self, nullptr, comparator, allocator);
    return MapInit(At(self), kAliasesHeader, allocator);
}

uint32_t __fastcall DFacesConstruct(void* self, void*)
{
    OPENSC4_FIRST("the faces' constructor 0x0089505A");
    if (Game(kTables))
        return g_facesConstruct(self, nullptr);
    return MapConstruct(At(self), kFacesInit);
}

uint32_t __fastcall DAliasesConstruct(void* self, void*)
{
    OPENSC4_FIRST("the aliases' constructor 0x00895073");
    if (Game(kTables))
        return g_aliasesConstruct(self, nullptr);
    return MapConstruct(At(self), kAliasesInit);
}

uint32_t __fastcall DFacesEraseTree(void* self, void*, uint32_t node)
{
    OPENSC4_FIRST("the faces' EraseTree 0x00894657");
    if (Game(kTables))
        return g_facesEraseTree(self, nullptr, node);
    EraseTree(At(self), node, kFacesEraseTree, kFacePairFree);
    return 0;
}

uint32_t __fastcall DAliasesEraseTree(void* self, void*, uint32_t node)
{
    OPENSC4_FIRST("the aliases' EraseTree 0x0089468C");
    if (Game(kTables))
        return g_aliasesEraseTree(self, nullptr, node);
    EraseTree(At(self), node, kAliasesEraseTree, kAliasPairFree);
    return 0;
}

uint32_t __fastcall DRangesEraseTree(void* self, void*, uint32_t node)
{
    OPENSC4_FIRST("the sizes' EraseTree 0x008946C1");
    if (Game(kTables))
        return g_rangesEraseTree(self, nullptr, node);
    EraseTree(At(self), node, kRangesEraseTree, kStringDestroy);
    return 0;
}

uint32_t __fastcall DFacesClear(void* self, void*)
{
    OPENSC4_FIRST("the faces' Clear 0x00894784");
    if (Game(kTables))
        return g_facesClear(self, nullptr);
    Clear(At(self), kFacesEraseTree);
    return 0;
}

uint32_t __fastcall DAliasesClear(void* self, void*)
{
    OPENSC4_FIRST("the aliases' Clear 0x008947AD");
    if (Game(kTables))
        return g_aliasesClear(self, nullptr);
    Clear(At(self), kAliasesEraseTree);
    return 0;
}

uint32_t __fastcall DRangesClear(void* self, void*)
{
    OPENSC4_FIRST("the sizes' Clear 0x008947D6");
    if (Game(kTables))
        return g_rangesClear(self, nullptr);
    Clear(At(self), kRangesEraseTree);
    return 0;
}

uint32_t __fastcall DFacesDestruct(void* self, void*)
{
    OPENSC4_FIRST("the faces' destructor 0x00894DC7");
    if (Game(kTables))
        return g_facesDestruct(self, nullptr);
    Destruct(At(self), kFacesClear);
    return 0;
}

uint32_t __fastcall DAliasesDestruct(void* self, void*)
{
    OPENSC4_FIRST("the aliases' destructor 0x00894DDE");
    if (Game(kTables))
        return g_aliasesDestruct(self, nullptr);
    Destruct(At(self), kAliasesClear);
    return 0;
}

uint32_t __fastcall DRangesDestruct(void* self, void*)
{
    OPENSC4_FIRST("the sizes' destructor 0x00894DF5");
    if (Game(kTables))
        return g_rangesDestruct(self, nullptr);
    Destruct(At(self), kRangesClear);
    return 0;
}

uint32_t __fastcall DFacesEraseNode(void* self, void*, uint32_t node)
{
    OPENSC4_FIRST("the faces' EraseNode 0x008945E1");
    if (Game(kTables))
        return g_facesEraseNode(self, nullptr, node);
    EraseNode(At(self), node, kFacePairFree);
    return 0;
}

uint32_t __fastcall DFacesNode(void* self, void*, uint32_t pair)
{
    OPENSC4_FIRST("the faces' node 0x00894EEA");
    if (Game(kTables))
        return g_facesNode(self, nullptr, pair);
    return MakeNode(pair, kFaceNodeBytes, kFacePairCopy);
}

uint32_t __fastcall DAliasesNode(void* self, void*, uint32_t pair)
{
    OPENSC4_FIRST("the aliases' node 0x00894F0B");
    if (Game(kTables))
        return g_aliasesNode(self, nullptr, pair);
    return MakeNode(pair, kAliasNodeBytes, kAliasPairCopy);
}

// While a game's insert runs in compare mode: its call of the insert under
// a parent, noted.
Decision* g_noted = nullptr;

uint32_t InsertAtDetour(Fn5 game, std::uintptr_t makeNode, void* self, uint32_t result, uint32_t x,
                        uint32_t parent, uint32_t pair, uint32_t right)
{
    if (g_noted) {
        *g_noted = Decision{true, 0, x, parent};
        g_noted = nullptr;
    }
    if (Game(kTables))
        return game(self, nullptr, result, x, parent, pair, right);
    return InsertAt(At(self), result, x, parent, pair, right, makeNode);
}

uint32_t __fastcall DFacesInsertAt(void* self, void*, uint32_t result, uint32_t x, uint32_t parent, uint32_t pair,
                                   uint32_t right)
{
    OPENSC4_FIRST("the faces' InsertAt 0x00895136");
    return InsertAtDetour(g_facesInsertAt, kFacesNode, self, result, x, parent, pair, right);
}

uint32_t __fastcall DAliasesInsertAt(void* self, void*, uint32_t result, uint32_t x, uint32_t parent,
                                     uint32_t pair, uint32_t right)
{
    OPENSC4_FIRST("the aliases' InsertAt 0x008951CB");
    return InsertAtDetour(g_aliasesInsertAt, kAliasesNode, self, result, x, parent, pair, right);
}

uint32_t __fastcall DRangesInsertAt(void* self, void*, uint32_t result, uint32_t x, uint32_t parent,
                                    uint32_t pair, uint32_t right)
{
    OPENSC4_FIRST("the sizes' InsertAt 0x00894E55");
    return InsertAtDetour(g_rangesInsertAt, kRangesNode, self, result, x, parent, pair, right);
}

std::string DecisionText(const Decision& d)
{
    if (d.insert)
        return "insert " + Words({d.x, d.parent});
    return "there " + Words({d.node});
}

// An insert: ours to its decision without its effects, then the game's with
// its call of the insert under a parent noted. A node already there is the
// game's result (its byte 0); a node inserted, the x and parent asked for.
uint32_t InsertDetour(const char* id, Fn2 game, std::uintptr_t insertAt, void* self, uint32_t result,
                      uint32_t pair)
{
    if (g_inGame)
        return game(self, nullptr, result, pair);
    if (!g_compare[kTables])
        return InsertUnique(At(self), result, pair, insertAt, nullptr);
    Decision mine;
    InsertUnique(At(self), result, pair, insertAt, &mine);
    Decision theirs;
    uint32_t answer;
    {
        GameSide side;
        g_noted = &theirs;
        answer = game(self, nullptr, result, pair);
        g_noted = nullptr;
    }
    if (!theirs.insert)
        theirs.node = Load<Address>(result);
    const bool inserted = Load<uint8_t>(result + 4) != 0;
    Report(id, DecisionText(theirs) + (inserted ? "1" : "0"), DecisionText(mine) + (mine.insert ? "1" : "0"));
    return answer;
}

uint32_t __fastcall DFacesInsert(void* self, void*, uint32_t result, uint32_t pair)
{
    OPENSC4_FIRST("the faces' Insert 0x008953A9");
    return InsertDetour("fonttables.facesinsert", g_facesInsert, kFacesInsertAt, self, result, pair);
}

uint32_t __fastcall DAliasesInsert(void* self, void*, uint32_t result, uint32_t pair)
{
    OPENSC4_FIRST("the aliases' Insert 0x0089543A");
    return InsertDetour("fonttables.aliasesinsert", g_aliasesInsert, kAliasesInsertAt, self, result, pair);
}

uint32_t __fastcall DRangesInsert(void* self, void*, uint32_t result, uint32_t pair)
{
    OPENSC4_FIRST("the sizes' Insert 0x008950A5");
    return InsertDetour("fonttables.rangesinsert", g_rangesInsert, kRangesInsertAt, self, result, pair);
}

// A bound: ours, then the game's, compared.
uint32_t BoundDetour(const char* id, Fn1 game, Address (*ours)(Address, Address), void* self, uint32_t key)
{
    if (g_inGame)
        return game(self, nullptr, key);
    const Address mine = ours(At(self), key);
    if (!g_compare[kTables])
        return mine;
    uint32_t result;
    {
        GameSide side;
        result = game(self, nullptr, key);
    }
    Report(id, Words({result}), Words({mine}));
    return result;
}

uint32_t __fastcall DLowerBound(void* self, void*, uint32_t key)
{
    OPENSC4_FIRST("LowerBound 0x00893FF0");
    return BoundDetour("fonttables.lowerbound", g_lowerBound, LowerBound, self, key);
}

uint32_t __fastcall DUpperBound(void* self, void*, uint32_t key)
{
    OPENSC4_FIRST("UpperBound 0x00894028");
    return BoundDetour("fonttables.upperbound", g_upperBound, UpperBound, self, key);
}

uint32_t __fastcall DEqualRange(void* self, void*, uint32_t result, uint32_t key)
{
    OPENSC4_FIRST("EqualRange 0x0089411A");
    if (g_inGame)
        return g_equalRange(self, nullptr, result, key);
    if (!g_compare[kTables])
        return EqualRange(At(self), result, key);
    Address mine[2] = {};
    EqualRange(At(self), At(mine), key);
    uint32_t answer;
    {
        GameSide side;
        answer = g_equalRange(self, nullptr, result, key);
    }
    Report("fonttables.equalrange", Words({Load<uint32_t>(result), Load<uint32_t>(result + 4)}),
           Words({mine[0], mine[1]}));
    return answer;
}

uint32_t __fastcall DRangesEraseRange(void* self, void*, uint32_t first, uint32_t last)
{
    OPENSC4_FIRST("the sizes' EraseRange 0x008947FF");
    if (Game(kTables))
        return g_rangesEraseRange(self, nullptr, first, last);
    RangesEraseRange(At(self), first, last);
    return 0;
}

uint32_t __fastcall DRangesEraseKey(void* self, void*, uint32_t key)
{
    OPENSC4_FIRST("the sizes' EraseKey 0x00894E0C");
    if (Game(kTables))
        return g_rangesEraseKey(self, nullptr, key);
    return RangesEraseKey(At(self), key);
}

OPENSC4_CDECL_THUNK(TFill, DFill, 12)
OPENSC4_THUNK(TBytesAllocate, DBytesAllocate, 12)
OPENSC4_THUNK(TBytesConstruct, DBytesConstruct, 8)
OPENSC4_THUNK(TListCopy, DListCopy, 4)
OPENSC4_THUNK(TFaceConstruct, DFaceConstruct, 0)
OPENSC4_THUNK(TFaceDestruct, DFaceDestruct, 0)
OPENSC4_THUNK(TFaceCopy, DFaceCopy, 4)
OPENSC4_THUNK(TFacePairConstruct, DFacePairConstruct, 8)
OPENSC4_THUNK(TFacePairCopy, DFacePairCopy, 4)
OPENSC4_THUNK(TFacePairDestruct, DFacePairDestruct, 0)
OPENSC4_THUNK(TAliasPairConstruct, DAliasPairConstruct, 8)
OPENSC4_THUNK(TAliasPairCopy, DAliasPairCopy, 4)
OPENSC4_THUNK(TAliasPairDestruct, DAliasPairDestruct, 0)
OPENSC4_THUNK(TRangePairCopy, DRangePairCopy, 4)
OPENSC4_THUNK(TFacesHeader, DFacesHeader, 4)
OPENSC4_THUNK(TAliasesHeader, DAliasesHeader, 4)
OPENSC4_THUNK(TFacesInit, DFacesInit, 8)
OPENSC4_THUNK(TAliasesInit, DAliasesInit, 8)
OPENSC4_THUNK(TFacesConstruct, DFacesConstruct, 0)
OPENSC4_THUNK(TAliasesConstruct, DAliasesConstruct, 0)
OPENSC4_THUNK(TFacesEraseTree, DFacesEraseTree, 4)
OPENSC4_THUNK(TAliasesEraseTree, DAliasesEraseTree, 4)
OPENSC4_THUNK(TRangesEraseTree, DRangesEraseTree, 4)
OPENSC4_THUNK(TFacesClear, DFacesClear, 0)
OPENSC4_THUNK(TAliasesClear, DAliasesClear, 0)
OPENSC4_THUNK(TRangesClear, DRangesClear, 0)
OPENSC4_THUNK(TFacesDestruct, DFacesDestruct, 0)
OPENSC4_THUNK(TAliasesDestruct, DAliasesDestruct, 0)
OPENSC4_THUNK(TRangesDestruct, DRangesDestruct, 0)
OPENSC4_THUNK(TFacesEraseNode, DFacesEraseNode, 4)
OPENSC4_THUNK(TFacesNode, DFacesNode, 4)
OPENSC4_THUNK(TAliasesNode, DAliasesNode, 4)
OPENSC4_THUNK(TFacesInsertAt, DFacesInsertAt, 20)
OPENSC4_THUNK(TAliasesInsertAt, DAliasesInsertAt, 20)
OPENSC4_THUNK(TRangesInsertAt, DRangesInsertAt, 20)
OPENSC4_THUNK(TFacesInsert, DFacesInsert, 8)
OPENSC4_THUNK(TAliasesInsert, DAliasesInsert, 8)
OPENSC4_THUNK(TRangesInsert, DRangesInsert, 8)
OPENSC4_THUNK(TLowerBound, DLowerBound, 4)
OPENSC4_THUNK(TUpperBound, DUpperBound, 4)
OPENSC4_THUNK(TEqualRange, DEqualRange, 8)
OPENSC4_THUNK(TRangesEraseRange, DRangesEraseRange, 8)
OPENSC4_THUNK(TRangesEraseKey, DRangesEraseKey, 4)

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
    log::Write("fonttables: %s: %zu functions %s", name, N,
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallShared(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontSystem_SetDrawMode, &TSetDrawMode, Slot(&g_setDrawMode)},
        {addr::GZFontSystem_SetServiceID, &TSetServiceId, Slot(&g_setServiceId)},
        {addr::GZTree_Distance, &TDistance, Slot(&g_distance)},
        {addr::GZPairList_MakeNode, &TListNode, Slot(&g_listNode)},
        {addr::GZPairList_PushBack, &TListPushBack, Slot(&g_listPushBack)},
        {addr::GZPairList_InsertRange, &TListInsertRange, Slot(&g_listInsertRange)},
        {addr::SC4StringObjMap_Erase, &TRangesEraseNode, Slot(&g_rangesEraseNode)},
        {addr::cGZUIScriptWidgets_TreeKeyPair, &TRangePairConstruct, Slot(&g_rangePairConstruct)},
        {addr::GZFontRangeMap_MakeNode, &TRangesNode, Slot(&g_rangesNode)},
        {addr::cGZUIScriptWidgets_TreeKeyMapConstruct, &TRangesConstruct, Slot(&g_rangesConstruct)},
    };
    return InstallAll(mode, kShared, "shared", detours);
}

bool InstallTables(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontBytes_Fill, &TFill, Slot(&g_fill)},
        {addr::GZFontBytes_Allocate, &TBytesAllocate, Slot(&g_bytesAllocate)},
        {addr::GZFontBytes_Construct, &TBytesConstruct, Slot(&g_bytesConstruct)},
        {addr::GZPairList_Copy, &TListCopy, Slot(&g_listCopy)},
        {addr::GZFontFace_Construct, &TFaceConstruct, Slot(&g_faceConstruct)},
        {addr::GZFontFace_Destruct, &TFaceDestruct, Slot(&g_faceDestruct)},
        {addr::GZFontFace_Copy, &TFaceCopy, Slot(&g_faceCopy)},
        {addr::GZFontFacePair_Construct, &TFacePairConstruct, Slot(&g_facePairConstruct)},
        {addr::GZFontFacePair_Copy, &TFacePairCopy, Slot(&g_facePairCopy)},
        {addr::GZFontFacePair_Destruct, &TFacePairDestruct, Slot(&g_facePairDestruct)},
        {addr::GZFontAliasPair_Construct, &TAliasPairConstruct, Slot(&g_aliasPairConstruct)},
        {addr::GZFontAliasPair_Copy, &TAliasPairCopy, Slot(&g_aliasPairCopy)},
        {addr::GZFontAliasPair_Destruct, &TAliasPairDestruct, Slot(&g_aliasPairDestruct)},
        {addr::GZFontRangePair_Copy, &TRangePairCopy, Slot(&g_rangePairCopy)},
        {addr::GZFontFaceMap_MakeHeader, &TFacesHeader, Slot(&g_facesHeader)},
        {addr::GZFontAliasMap_MakeHeader, &TAliasesHeader, Slot(&g_aliasesHeader)},
        {addr::GZFontFaceMap_Init, &TFacesInit, Slot(&g_facesInit)},
        {addr::GZFontAliasMap_Init, &TAliasesInit, Slot(&g_aliasesInit)},
        {addr::GZFontFaceMap_Construct, &TFacesConstruct, Slot(&g_facesConstruct)},
        {addr::GZFontAliasMap_Construct, &TAliasesConstruct, Slot(&g_aliasesConstruct)},
        {addr::GZFontFaceMap_EraseTree, &TFacesEraseTree, Slot(&g_facesEraseTree)},
        {addr::GZFontAliasMap_EraseTree, &TAliasesEraseTree, Slot(&g_aliasesEraseTree)},
        {addr::GZFontRangeMap_EraseTree, &TRangesEraseTree, Slot(&g_rangesEraseTree)},
        {addr::GZFontFaceMap_Clear, &TFacesClear, Slot(&g_facesClear)},
        {addr::GZFontAliasMap_Clear, &TAliasesClear, Slot(&g_aliasesClear)},
        {addr::GZFontRangeMap_Clear, &TRangesClear, Slot(&g_rangesClear)},
        {addr::GZFontFaceMap_Destruct, &TFacesDestruct, Slot(&g_facesDestruct)},
        {addr::GZFontAliasMap_Destruct, &TAliasesDestruct, Slot(&g_aliasesDestruct)},
        {addr::GZFontRangeMap_Destruct, &TRangesDestruct, Slot(&g_rangesDestruct)},
        {addr::GZFontFaceMap_EraseNode, &TFacesEraseNode, Slot(&g_facesEraseNode)},
        {addr::GZFontFaceMap_MakeNode, &TFacesNode, Slot(&g_facesNode)},
        {addr::GZFontAliasMap_MakeNode, &TAliasesNode, Slot(&g_aliasesNode)},
        {addr::GZFontFaceMap_InsertAt, &TFacesInsertAt, Slot(&g_facesInsertAt)},
        {addr::GZFontAliasMap_InsertAt, &TAliasesInsertAt, Slot(&g_aliasesInsertAt)},
        {addr::GZFontRangeMap_InsertAt, &TRangesInsertAt, Slot(&g_rangesInsertAt)},
        {addr::GZFontFaceMap_Insert, &TFacesInsert, Slot(&g_facesInsert)},
        {addr::GZFontAliasMap_Insert, &TAliasesInsert, Slot(&g_aliasesInsert)},
        {addr::GZFontRangeMap_Insert, &TRangesInsert, Slot(&g_rangesInsert)},
        {addr::GZFontRangeMap_LowerBound, &TLowerBound, Slot(&g_lowerBound)},
        {addr::GZFontRangeMap_UpperBound, &TUpperBound, Slot(&g_upperBound)},
        {addr::GZFontRangeMap_EqualRange, &TEqualRange, Slot(&g_equalRange)},
        {addr::GZFontRangeMap_EraseRange, &TRangesEraseRange, Slot(&g_rangesEraseRange)},
        {addr::GZFontRangeMap_EraseKey, &TRangesEraseKey, Slot(&g_rangesEraseKey)},
    };
    return InstallAll(mode, kTables, "tables", detours);
}

const bool g_registered[] = {
    registry::Register({"framework.font.system.tables", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontFaceMap_Insert,
                        "Our font system's tables: the faces', aliases' and sizes' maps, their pairs and nodes, a "
                        "face, a face's list copied, the byte vector (43 detours; docs/fonts.md)",
                        true, InstallTables}),
    registry::Register({"framework.font.system.shared", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontSystem_SetDrawMode,
                        "Our STLport code in the font system's range that other code calls too: slot 7, the +4 "
                        "slot 4, a tree's distance, a list of pairs' node, push_back and insert, the sizes' pair, "
                        "node, erase and constructor (10 detours; docs/fonts.md)",
                        true, InstallShared}),
};

} // namespace
} // namespace opensc4::fonttables
