// The city save code's serializer and segment streams (docs/save-format.md),
// part 2: 0x005D2740 to 0x005D38D0. Replacement, off by default:
//
//   saves.serializer.part2: the save segment's stream openers and
//     constructor, the segment streams' C string slots, the serializer's
//     constructor, destructor, reset, object lookup and record CRC, and the
//     serializer's own map code (classes by class ID, objects by ID).
//
// Three classes meet here.
// - The save segment (cSC4DBSegmentPackedFile, 0x1DC bytes): +0x0 the
//   cISC4DBSegment vtable 0x00AA2654 (14 slots), +0x4 the framework's packed
//   file (its vtables 0x00AA2598 at +0x4 and 0x00AA24F0 at +0x8), +0x1CC the
//   serializer's interface (counted), +0x1D0 the message server (counted),
//   +0x1D4 and +0x1D8 zero at construction.
// - A segment stream (0x1C bytes; input and output have the same layout):
//   +0x0 unknown vtable (input 0x00AA22F4, output 0x00AA2384), +0x4 reference
//   count, +0x8 the stream interface (input 0x00AA2280, output 0x00AA2310),
//   +0xC a byte 'no error yet' (1 at construction), +0x10 the record the
//   stream reads or writes, +0x14 and +0x18 zero at construction.
// - The serializer (cSC4COMSerializer, 0x44 bytes): +0x0 unknown vtable
//   0x00AA26B4, +0x4 reference count, +0x8 its interface 0x00AA268C (10
//   slots, interface ID 0x699899EA), +0xC a byte, +0x10 and +0x1C two maps of
//   classes (header, count), +0x28 a mode (2 while saving), +0x2C the number
//   of objects created by loading, +0x30 and +0x31 bytes, +0x34 a counted
//   pointer, +0x38 a byte vector (begin, end, capacity) the CRC reads into.
//
// The maps are the game's red-black trees (header node: +0x4 root, +0x8
// leftmost, +0xC rightmost; nodes: +0x0 colour byte, +0x4 parent, +0x8 left,
// +0xC right, +0x10 the value). A class map's nodes are 0x28 bytes: class ID,
// two dwords (the second counts objects created) and a map of that class's
// objects by ID, whose nodes are 0x18 bytes: ID and the object (counted).
//
// Left the game's: 0x005D28C0 (the object map's node insert) and 0x005D2A60
// (its insert with a hint) are shared with the ordinance simulator's map
// (0x0080E070, 0x0080E862). Not functions: 0x005D2870 is the tail of
// 0x005D27E0, 0x005D2920 of 0x005D28C0, 0x005D2B10 of 0x005D2A60 and
// 0x005D35E0 of 0x005D3530.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::saves {
namespace {

namespace unknown { // cIGZUnknown, by vtable offset
constexpr std::size_t kQueryInterface = 0x0; // (iid, void** out)
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
} // namespace unknown

namespace stream { // a segment stream's interface at +0x8, by vtable offset
constexpr std::size_t kSetGZStr = 0x3C; // output: (cIGZString const&)
constexpr std::size_t kOpen = 0x54;     // input: (segment, key); output: (segment, key, fromStart)
// From the interface pointer:
constexpr std::size_t kGood = 0x4;   // object +0xC
constexpr std::size_t kRecord = 0x8; // object +0x10
} // namespace stream

namespace record { // cIGZPersistDBSerialRecord, by vtable offset
constexpr std::size_t kGetFieldString = 0x60; // (cIGZString&)
constexpr std::size_t kGetFieldVoid = 0x6C;   // (void*, count)
constexpr std::size_t kGetPosition = 0xD8;
constexpr std::size_t kSeekAbsolute = 0xDC; // (position)
} // namespace record

namespace serializer { // cSC4COMSerializer
constexpr std::size_t kVtable = 0x0;
constexpr std::size_t kInterface = 0x8;
constexpr std::size_t kFlagC = 0xC;
constexpr std::size_t kClassesA = 0x10;
constexpr std::size_t kClasses = 0x1C;
constexpr std::size_t kMode = 0x28;
constexpr std::size_t kCreated = 0x2C;
constexpr std::size_t kFlag30 = 0x30;
constexpr std::size_t kFlag31 = 0x31;
constexpr std::size_t kHeld = 0x34;
constexpr std::size_t kBuffer = 0x38;
constexpr std::size_t kBufferEnd = 0x3C;
constexpr std::size_t kBufferCapacity = 0x40;
constexpr std::size_t kSaveClassObjects = 0x1C; // slot of the interface
} // namespace serializer

namespace segment { // cSC4DBSegmentPackedFile
constexpr std::size_t kPackedFile = 0x4;
constexpr std::size_t kRecords = 0x8;
constexpr std::size_t kSerializer = 0x1CC;
constexpr std::size_t kMessageServer = 0x1D0;
constexpr std::size_t kField1D4 = 0x1D4;
constexpr std::size_t kField1D8 = 0x1D8;
} // namespace segment

constexpr uint32_t kSerializableIid = 0xE4FDA3D4;   // cIGZSerializable
constexpr uint32_t kSegmentIStreamIid = 0x4999A8F5; // cISC4DBSegmentIStream
constexpr uint32_t kSegmentOStreamIid = 0x6999A8FB; // cISC4DBSegmentOStream
constexpr std::size_t kGetClassObject = 0xC;        // cIGZCOM: (class ID, iid, void** out)
constexpr uint32_t kCrcLimit = 0x3D090;             // 250,000 bytes

struct ObjectNode {
    uint8_t color;
    ObjectNode* parent;
    ObjectNode* left;
    ObjectNode* right;
    uint32_t id;
    void* object;
};
static_assert(sizeof(ObjectNode) == 0x18);

struct ObjectMap {
    ObjectNode* header;
    uint32_t count;
};

struct ClassValue {
    uint32_t classId;
    uint32_t field4;
    uint32_t created;
    ObjectMap objects;
};
static_assert(offsetof(ClassValue, objects) == 0xC);

struct ClassNode {
    uint8_t color;
    ClassNode* parent;
    ClassNode* left;
    ClassNode* right;
    ClassValue value;
    uint32_t unused24;
};
static_assert(sizeof(ClassNode) == 0x28);
static_assert(offsetof(ClassNode, value) == 0x10);

struct ClassMap {
    ClassNode* header;
    uint32_t count;
};

// What an insert writes through its result pointer: the node and whether it
// is new (5 bytes; the original frames leave 8).
struct InsertResult {
    void* node;
    uint8_t inserted;
};
static_assert(sizeof(InsertResult) == 8);

// The game's string as these functions build it on the stack.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t field10;
};
static_assert(sizeof(GameString) == 0x14);

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, unknown::kAddRef);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, unknown::kRelease);
}

template <typename Node>
Node* AllocateNode()
{
    return Cdecl<Node*>(addr::SC4_stl_allocate, static_cast<uint32_t>(sizeof(Node)));
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// An emptied tree's header, in the original's order of stores.
template <typename Node>
void ResetHeader(Node* header)
{
    header->left = header;
    header->parent = nullptr;
    header->right = header;
}

// The header's leftmost and rightmost after a copy put a root under it.
template <typename Node>
void SetEnds(Node* header)
{
    Node* node = header->parent;
    for (Node* next = node->left; next; next = node->left)
        node = next;
    header->left = node;
    node = header->parent;
    for (Node* next = node->right; next; next = node->right)
        node = next;
    header->right = node;
}

// --- The save segment's stream openers ---------------------------------------

// The game's 0x005D2740: slot 5 (0x14) of the save segment's vtable
// 0x00AA2654, `this` the segment. Makes a segment input stream, opens it on
// the record `key` names and hands out its cISC4DBSegmentIStream. When the
// allocation fails the original goes on with a null pointer, as here.
uint8_t __fastcall OpenIStream(char* self, void*, std::uintptr_t key, void** out)
{
    *out = nullptr;
    char* object = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1C});
    if (object) {
        This<void*>(addr::cRZBaseUnknown_Construct, object);
        At<std::uintptr_t>(object, 0x8) = addr::cSC4DBSegmentIStream_BaseStreamVtable;
        At<std::uintptr_t>(object, 0x0) = addr::cSC4DBSegmentIStream_Vtable;
        At<std::uintptr_t>(object, 0x8) = addr::cSC4DBSegmentIStream_StreamVtable;
        At<uint8_t>(object, 0xC) = 1;
        At<uint32_t>(object, 0x10) = 0;
        At<uint32_t>(object, 0x14) = 0;
        At<uint32_t>(object, 0x18) = 0;
        AddRef(object);
    }
    if (Virtual<uint8_t>(object + 0x8, stream::kOpen, static_cast<void*>(self), key)) {
        const uint8_t result = Virtual<uint8_t>(object, unknown::kQueryInterface, kSegmentIStreamIid, out);
        Release(object);
        return result;
    }
    Release(object);
    return 0;
}

// The game's 0x005D27E0: slot 8 (0x20) of the same vtable. The output
// stream's twin; the third argument goes to the stream's Open
// (cSC4COMSerializer::StreamOpen, 0x005D2410: start at 0 or append).
uint8_t __fastcall OpenOStream(char* self, void*, std::uintptr_t key, void** out, uint32_t fromStart)
{
    *out = nullptr;
    char* object = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1C});
    if (object) {
        This<void*>(addr::cRZBaseUnknown_Construct, object);
        At<std::uintptr_t>(object, 0x8) = addr::cSC4DBSegmentOStream_BaseStreamVtable;
        At<std::uintptr_t>(object, 0x0) = addr::cSC4DBSegmentOStream_Vtable;
        At<std::uintptr_t>(object, 0x8) = addr::cSC4DBSegmentOStream_StreamVtable;
        At<uint8_t>(object, 0xC) = 1;
        At<uint32_t>(object, 0x10) = 0;
        At<uint32_t>(object, 0x14) = 0;
        At<uint32_t>(object, 0x18) = 0;
        AddRef(object);
    }
    if (Virtual<uint8_t>(object + 0x8, stream::kOpen, static_cast<void*>(self), key, fromStart)) {
        const uint8_t result = Virtual<uint8_t>(object, unknown::kQueryInterface, kSegmentOStreamIid, out);
        Release(object);
        return result;
    }
    Release(object);
    return 0;
}

// --- The object map (objects of one class by ID) -----------------------------

// The game's 0x005D2880: frees a subtree of an object map, releasing each
// node's object. `this` is the map (not used).
void EraseObjectTree(ObjectMap* map, ObjectNode* node)
{
    while (node) {
        EraseObjectTree(map, node->right);
        void* const object = node->object;
        ObjectNode* const left = node->left;
        if (object)
            Release(object);
        Free(node);
        node = left;
    }
}

void __fastcall EraseObjectTreeHook(ObjectMap* self, void*, ObjectNode* node)
{
    EraseObjectTree(self, node);
}

// A new object node with a copy of another's ID and object (one more
// reference) and its colour.
ObjectNode* CopyObjectNode(const ObjectNode* source)
{
    ObjectNode* const node = AllocateNode<ObjectNode>();
    node->id = source->id;
    node->object = source->object;
    if (node->object)
        AddRef(node->object);
    node->color = source->color;
    node->left = nullptr;
    node->right = nullptr;
    return node;
}

// The game's 0x005D2990: copies a subtree of an object map under `parent`
// and returns the copy's top node.
ObjectNode* CopyObjectTree(ObjectMap* map, const ObjectNode* source, ObjectNode* parent)
{
    ObjectNode* const top = CopyObjectNode(source);
    top->parent = parent;
    if (source->right)
        top->right = CopyObjectTree(map, source->right, top);
    ObjectNode* above = top;
    for (const ObjectNode* from = source->left; from; from = from->left) {
        ObjectNode* const node = CopyObjectNode(from);
        above->left = node;
        node->parent = above;
        if (from->right)
            node->right = CopyObjectTree(map, from->right, node);
        above = node;
    }
    return top;
}

ObjectNode* __fastcall CopyObjectTreeHook(ObjectMap* self, void*, const ObjectNode* source, ObjectNode* parent)
{
    return CopyObjectTree(self, source, parent);
}

// The game's 0x005D2EB0: constructs an object map as a copy of another.
ObjectMap* __fastcall CopyObjectMap(ObjectMap* self, void*, const ObjectMap* source)
{
    self->header = nullptr;
    ObjectNode* const header = AllocateNode<ObjectNode>();
    self->header = header;
    self->count = 0;
    header->color = 0;
    if (source->header->parent == nullptr) {
        self->header->parent = nullptr;
        self->header->left = self->header;
        self->header->right = self->header;
    } else {
        ObjectNode* const own = self->header;
        own->parent = CopyObjectTree(self, source->header->parent, own);
        SetEnds(self->header);
    }
    self->count = source->count;
    return self;
}

// --- The class map (classes by class ID) --------------------------------------

// The game's 0x005D2FE0: frees a subtree of a class map with each node's
// object map.
void EraseClassTree(ClassMap* map, ClassNode* node)
{
    while (node) {
        EraseClassTree(map, node->right);
        ClassNode* const left = node->left;
        ObjectMap* const objects = &node->value.objects;
        if (objects->count != 0) {
            EraseObjectTree(objects, objects->header->parent);
            ResetHeader(objects->header);
            objects->count = 0;
        }
        if (objects->header)
            Free(objects->header);
        Free(node);
        node = left;
    }
}

void __fastcall EraseClassTreeHook(ClassMap* self, void*, ClassNode* node)
{
    EraseClassTree(self, node);
}

// Empties a class map that has nodes, as the original does inline.
void ClearClassMap(ClassMap* map)
{
    if (map->count != 0) {
        EraseClassTree(map, map->header->parent);
        ResetHeader(map->header);
        map->count = 0;
    }
}

// A new class node holding a copy of `value` (its object map copied with the
// objects referenced once more). Links and colour are the caller's.
ClassNode* NewClassNode(const ClassValue* value)
{
    ClassNode* const node = AllocateNode<ClassNode>();
    node->value.classId = value->classId;
    node->value.field4 = value->field4;
    node->value.created = value->created;
    CopyObjectMap(&node->value.objects, nullptr, &value->objects);
    return node;
}

// The game's 0x005D3060: a copy of a class node with its colour and no
// children; the parent is left unset. `this` is not used.
ClassNode* __fastcall CloneClassNode(void*, void*, const ClassNode* source)
{
    ClassNode* const node = NewClassNode(&source->value);
    node->color = source->color;
    node->left = nullptr;
    node->right = nullptr;
    return node;
}

// The game's 0x005D30B0: links a new node for `value` under `parent` (on the
// left when the parent is the header, or when `forceRight` is null and
// either `hint` is set or the key is below the parent's), rebalances and
// counts it. Writes the node to `result` and returns `result`.
ClassNode** __fastcall InsertClassNode(ClassMap* self, void*, ClassNode** result, ClassNode* hint,
                                       ClassNode* parent, const ClassValue* value, ClassNode* forceRight)
{
    ClassNode* node;
    if (parent != self->header
        && (forceRight != nullptr || (hint == nullptr && !(value->classId < parent->value.classId)))) {
        node = NewClassNode(value);
        parent->right = node;
        ClassNode* const header = self->header;
        if (parent == header->right)
            header->right = node;
    } else {
        node = NewClassNode(value);
        parent->left = node;
        ClassNode* const header = self->header;
        if (parent == header) {
            header->parent = node;
            self->header->right = node;
        } else if (parent == header->left) {
            header->left = node;
        }
    }
    node->parent = parent;
    node->left = nullptr;
    node->right = nullptr;
    Cdecl<void>(addr::SC4Tree_RebalanceForInsert, node, &self->header->parent);
    self->count = self->count + 1;
    *result = node;
    return result;
}

// The game's 0x005D31A0: copies a subtree of a class map under `parent` and
// returns the copy's top node.
ClassNode* CopyClassTree(ClassMap* map, const ClassNode* source, ClassNode* parent)
{
    ClassNode* const top = CloneClassNode(map, nullptr, source);
    top->parent = parent;
    if (source->right)
        top->right = CopyClassTree(map, source->right, top);
    ClassNode* above = top;
    for (const ClassNode* from = source->left; from; from = from->left) {
        ClassNode* const node = NewClassNode(&from->value);
        node->color = from->color;
        node->left = nullptr;
        node->right = nullptr;
        above->left = node;
        node->parent = above;
        if (from->right)
            node->right = CopyClassTree(map, from->right, node);
        above = node;
    }
    return top;
}

ClassNode* __fastcall CopyClassTreeHook(ClassMap* self, void*, const ClassNode* source, ClassNode* parent)
{
    return CopyClassTree(self, source, parent);
}

// The game's 0x005D32D0: assigns one class map from another (the old nodes
// erased, the source's copied). Returns the map. An empty source leaves the
// count at 0 without reading the source's.
ClassMap* __fastcall AssignClassMap(ClassMap* self, void*, const ClassMap* source)
{
    if (self == source)
        return self;
    ClearClassMap(self);
    self->count = 0;
    const ClassNode* const root = source->header->parent;
    if (root == nullptr) {
        self->header->parent = nullptr;
        self->header->left = self->header;
        self->header->right = self->header;
        return self;
    }
    ClassNode* const own = self->header;
    own->parent = CopyClassTree(self, root, own);
    SetEnds(self->header);
    self->count = source->count;
    return self;
}

// The game's 0x005D3390: inserts a class value unless its class ID is in the
// map. Writes the node (new or the one found) and whether it was inserted to
// `result` and returns `result`.
InsertResult* __fastcall InsertClass(ClassMap* self, void*, InsertResult* result, const ClassValue* value)
{
    ClassNode* const header = self->header;
    ClassNode* child = header->parent;
    ClassNode* parent = header;
    uint8_t below = 1;
    while (child) {
        below = value->classId < child->value.classId;
        parent = child;
        child = below ? child->left : child->right;
    }
    ClassNode* before = parent;
    ClassNode* made;
    if (below) {
        if (parent == header->left) {
            result->node = *InsertClassNode(self, nullptr, &made, child, parent, value, nullptr);
            result->inserted = 1;
            return result;
        }
        This<void>(addr::SC4TreeIterator_Decrement, &before);
    }
    if (before->value.classId < value->classId) {
        ClassNode* const node = *InsertClassNode(self, nullptr, &made, child, parent, value, nullptr);
        result->inserted = 1;
        result->node = node;
        return result;
    }
    result->inserted = 0;
    result->node = before;
    return result;
}

// The game's 0x005D3530: insert with a position hint. Writes the node (new,
// or the one that has the class ID already) to `result` and returns `result`.
ClassNode** __fastcall InsertClassAt(ClassMap* self, void*, ClassNode** result, ClassNode* position,
                                     const ClassValue* value)
{
    InsertResult plain;
    ClassNode* header = self->header;
    if (position == header->left) {
        if (!(self->count > 0)) {
            *result = static_cast<ClassNode*>(InsertClass(self, nullptr, &plain, value)->node);
            return result;
        }
        const uint32_t key = value->classId;
        if (position->value.classId > key) {
            InsertClassNode(self, nullptr, result, position, position, value, nullptr);
            return result;
        }
        if (!(position->value.classId < key)) {
            *result = position;
            return result;
        }
        ClassNode* after = position;
        This<void>(addr::SC4MapIterator_Next, &after);
        if (after == self->header) {
            InsertClassNode(self, nullptr, result, nullptr, position, value, position);
            return result;
        }
        if (key < after->value.classId) {
            if (position->right == nullptr)
                InsertClassNode(self, nullptr, result, nullptr, position, value, position);
            else
                InsertClassNode(self, nullptr, result, after, after, value, nullptr);
            return result;
        }
        *result = static_cast<ClassNode*>(InsertClass(self, nullptr, &plain, value)->node);
        return result;
    }
    if (position == header) {
        ClassNode* const last = header->right;
        if (last->value.classId < value->classId) {
            InsertClassNode(self, nullptr, result, nullptr, last, value, position);
            return result;
        }
        *result = static_cast<ClassNode*>(InsertClass(self, nullptr, &plain, value)->node);
        return result;
    }
    ClassNode* neighbour = position;
    This<void>(addr::SC4TreeIterator_Decrement, &neighbour);
    const uint32_t key = value->classId;
    const uint8_t below = key < position->value.classId;
    if (below && neighbour->value.classId < key) {
        if (neighbour->right == nullptr)
            InsertClassNode(self, nullptr, result, nullptr, neighbour, value, neighbour);
        else
            InsertClassNode(self, nullptr, result, position, position, value, nullptr);
        return result;
    }
    neighbour = position;
    This<void>(addr::SC4MapIterator_Next, &neighbour);
    uint8_t above = below == 0;
    if (below == 0) {
        above = position->value.classId < key;
        if (above == 0) {
            *result = position;
            return result;
        }
        if (neighbour == self->header || key < neighbour->value.classId) {
            if (position->right == nullptr)
                InsertClassNode(self, nullptr, result, nullptr, position, value, position);
            else
                InsertClassNode(self, nullptr, result, neighbour, neighbour, value, nullptr);
            return result;
        }
    }
    if (below == above) {
        *result = position;
        return result;
    }
    *result = static_cast<ClassNode*>(InsertClass(self, nullptr, &plain, value)->node);
    return result;
}

// --- The segment streams' C string slots --------------------------------------

// The game's 0x005D2C70: slot 14 (0x38) of the segment input stream's
// interface 0x00AA2280, `this` the interface (object +0x8). Reads a string
// from the record and copies it with its terminator, at most `limit` bytes,
// to `text`. The stream's 'no error yet' byte is and-ed with the read's
// result and returned.
uint8_t __fastcall ReadCString(char* self, void*, char* text, uint32_t limit)
{
    GameString string;
    string.begin = nullptr;
    string.end = nullptr;
    string.capacity = nullptr;
    char* const memory = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    string.begin = memory;
    string.end = memory;
    string.capacity = memory + 8;
    *memory = 0;
    string.vtable = addr::SC4String_Vtable;
    string.field10 = 0;
    uint32_t good = 0;
    if (At<uint8_t>(self, stream::kGood) != 0
        && Virtual<uint8_t>(At<void*>(self, stream::kRecord), record::kGetFieldString, &string))
        good = 1;
    At<uint8_t>(self, stream::kGood) = static_cast<uint8_t>(good);
    const char* from = This<const char*>(addr::cRZBaseString_Data, &string);
    const char* scan = from;
    while (*scan++ != 0) {
    }
    const uint32_t length = static_cast<uint32_t>(scan - (from + 1)) + 1;
    const uint32_t count = length < limit ? length : limit;
    for (const char* const end = from + count; from < end; ++from)
        *text++ = *from;
    const uint8_t result = At<uint8_t>(self, stream::kGood);
    string.vtable = addr::SC4String_Vtable;
    if (string.begin)
        Free(string.begin);
    return result;
}

// The game's 0x005D2D50: slot 14 (0x38) of the segment output stream's
// interface 0x00AA2310, `this` the interface. Wraps the text in a string and
// writes it through the stream's own string slot (0x3C).
uint8_t __fastcall WriteCString(char* self, void*, const char* text)
{
    GameString string;
    string.begin = nullptr;
    string.end = nullptr;
    string.capacity = nullptr;
    const char* scan = text;
    while (*scan++ != 0) {
    }
    const char* const end = text + (scan - (text + 1));
    // The third argument is an allocator object the callee does not read;
    // the original passes the address of its own argument slot.
    This<void>(addr::SC4String_FromRange, &string.begin, text, end, &text);
    string.vtable = addr::SC4String_Vtable;
    string.field10 = 0;
    const uint8_t result = Virtual<uint8_t>(self, stream::kSetGZStr, &string);
    string.vtable = addr::SC4String_Vtable;
    if (string.begin)
        Free(string.begin);
    return result;
}

// --- The serializer -------------------------------------------------------------

// The game's 0x005D2DD0, `this` the serializer. Finds the object with `id` in
// the class's entry of the map at +0x1C, or creates an object of the class
// (asking the framework for cIGZSerializable) and enters it under `id`.
// Hands out one reference. Fails when the class has no entry in the map or
// cannot be created; `out` is null then.
uint8_t __fastcall FindOrCreateObject(char* self, void*, void** out, uint32_t id, uint32_t classId)
{
    ClassMap* const classes = &At<ClassMap>(self, serializer::kClasses);
    *out = nullptr;
    ClassNode* entry;
    This<void*>(addr::SC4Uint32Map_Find, classes, &entry, &classId);
    if (entry == classes->header)
        return 0;
    ObjectMap* const objects = &entry->value.objects;
    ObjectNode* found;
    This<void*>(addr::SC4Uint32Map_Find, objects, &found, &id);
    if (found != objects->header) {
        void* const object = found->object;
        *out = object;
        AddRef(object);
        return 1;
    }
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (!Virtual<uint8_t>(com, kGetClassObject, classId, kSerializableIid, out))
        return 0;
    struct {
        uint32_t id;
        void* object;
    } pair = {id, nullptr};
    InsertResult result;
    This<void*>(addr::OrdinanceMap_Insert, objects, &result, &pair);
    entry->value.created = entry->value.created + 1;
    At<uint32_t>(self, serializer::kCreated) = At<uint32_t>(self, serializer::kCreated) + 1;
    This<void*>(addr::SC4RefPtr_Assign, &static_cast<ObjectNode*>(result.node)->object, *out);
    return 1;
}

// The game's 0x005D2F60, `this` the serializer. The CRC of the next `size`
// bytes of a record: reads them into the serializer's buffer (resized to
// size + 1, a zero after the data), then seeks the record back to where it
// was. Fails without touching the record when `size` is over 250,000, and
// leaves `crc` alone when the read fails.
uint8_t __fastcall RecordCrc(char* self, void*, void* serialRecord, uint32_t size, uint32_t* crc)
{
    uint8_t result = 0;
    if (size > kCrcLimit)
        return result;
    const uint32_t position = Virtual<uint32_t>(serialRecord, record::kGetPosition);
    char* const buffer = self + serializer::kBuffer;
    uint8_t fill = 0;
    This<void>(addr::SC4ByteVector_Resize2, buffer, size + 1, &fill);
    if (Virtual<uint8_t>(serialRecord, record::kGetFieldVoid, At<uint8_t*>(buffer, 0), size)) {
        At<uint8_t*>(buffer, 0)[size] = 0;
        *crc = Cdecl<uint32_t>(addr::RZ_Crc32, At<uint8_t*>(buffer, 0), size);
        result = 1;
    }
    Virtual<uint8_t>(serialRecord, record::kSeekAbsolute, position);
    return result;
}

// The game's 0x005D3250, `this` the serializer: empties both class maps
// (every object released), zeroes the state and releases the pointer at
// +0x34.
void __fastcall Reset(char* self, void*)
{
    At<uint8_t>(self, serializer::kFlagC) = 0;
    ClearClassMap(&At<ClassMap>(self, serializer::kClassesA));
    ClearClassMap(&At<ClassMap>(self, serializer::kClasses));
    At<uint32_t>(self, serializer::kMode) = 0;
    At<uint32_t>(self, serializer::kCreated) = 0;
    At<uint8_t>(self, serializer::kFlag30) = 0;
    At<uint8_t>(self, serializer::kFlag31) = 0;
    void* const held = At<void*>(self, serializer::kHeld);
    if (held) {
        At<void*>(self, serializer::kHeld) = nullptr;
        Release(held);
    }
}

// An empty class map's header in a freshly made map.
void InitClassMap(ClassMap* map)
{
    map->header = nullptr;
    ClassNode* const header = AllocateNode<ClassNode>();
    map->header = header;
    map->count = 0;
    header->color = 0;
    map->header->parent = nullptr;
    map->header->left = map->header;
    map->header->right = map->header;
}

// The game's 0x005D3450: the serializer's constructor (0x44 bytes).
char* __fastcall SerializerConstruct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<std::uintptr_t>(self, serializer::kInterface) = addr::cSC4COMSerializer_BaseInterfaceVtable;
    At<std::uintptr_t>(self, serializer::kVtable) = addr::cSC4COMSerializer_Vtable;
    At<std::uintptr_t>(self, serializer::kInterface) = addr::cSC4COMSerializer_InterfaceVtable;
    At<uint8_t>(self, serializer::kFlagC) = 0;
    InitClassMap(&At<ClassMap>(self, serializer::kClassesA));
    InitClassMap(&At<ClassMap>(self, serializer::kClasses));
    At<uint32_t>(self, serializer::kMode) = 0;
    At<uint32_t>(self, serializer::kCreated) = 0;
    At<uint8_t>(self, serializer::kFlag30) = 0;
    At<uint8_t>(self, serializer::kFlag31) = 0;
    At<void*>(self, serializer::kHeld) = nullptr;
    At<void*>(self, serializer::kBuffer) = nullptr;
    At<void*>(self, serializer::kBufferEnd) = nullptr;
    At<void*>(self, serializer::kBufferCapacity) = nullptr;
    Reset(self, nullptr);
    return self;
}

// The game's 0x005D34F0: slot 0 of the serializer's interface 0x00AA268C, a
// thunk to the object's QueryInterface (0x005D1780) with `this` moved back
// from the interface to the object.
uint8_t __fastcall InterfaceQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4Save_F5D1780, self - serializer::kInterface, iid, out);
}

// The game's 0x005D3500: slot 4 (0x10) of the serializer's interface, `this`
// the interface. Ends a session: while saving (mode 2) with the byte at
// object +0x31 clear it first writes the objects out (the interface's slot
// 0x1C) and returns that result, otherwise true; then resets the serializer.
uint8_t __fastcall EndSession(char* self, void*)
{
    char* const object = self - serializer::kInterface;
    uint8_t result = 1;
    if (At<uint32_t>(object, serializer::kMode) == 2 && At<uint8_t>(object, serializer::kFlag31) == 0)
        result = Virtual<uint8_t>(self, serializer::kSaveClassObjects);
    Reset(object, nullptr);
    return result;
}

// Frees a class map with its header, as the destructor does inline.
void DestroyClassMap(ClassMap* map)
{
    ClearClassMap(map);
    if (map->header)
        Free(map->header);
}

// The game's 0x005D3800: the serializer's destructor (the deleting one is
// 0x005D4290). A save still open is written out first, as in EndSession.
void __fastcall SerializerDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, serializer::kVtable) = addr::cSC4COMSerializer_Vtable;
    At<std::uintptr_t>(self, serializer::kInterface) = addr::cSC4COMSerializer_InterfaceVtable;
    if (At<uint32_t>(self, serializer::kMode) == 2 && At<uint8_t>(self, serializer::kFlag31) == 0)
        Virtual<uint8_t>(self + serializer::kInterface, serializer::kSaveClassObjects);
    Reset(self, nullptr);
    if (void* const buffer = At<void*>(self, serializer::kBuffer))
        Free(buffer);
    if (void* const held = At<void*>(self, serializer::kHeld))
        Release(held);
    DestroyClassMap(&At<ClassMap>(self, serializer::kClasses));
    DestroyClassMap(&At<ClassMap>(self, serializer::kClassesA));
    This<void>(addr::cRZBaseUnknown_Destruct, self);
}

// --- The save segment -----------------------------------------------------------

// Stores a counted pointer the way the constructor does inline: nothing when
// it is the one held already, else the new one referenced before the old one
// is released.
void AssignCounted(void*& held, void* value)
{
    void* const old = held;
    if (value == old)
        return;
    if (value)
        AddRef(value);
    held = value;
    if (old)
        Release(old);
}

// The game's 0x005D3740: the save segment's constructor (0x1DC bytes): the
// framework's packed file at +0x4, a new serializer and the message server.
char* __fastcall SegmentConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4DBSegmentPackedFile_BaseVtable;
    This<void*>(addr::cGZDBSegmentPackedFile_Construct, self + segment::kPackedFile);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4DBSegmentPackedFile_Vtable;
    At<std::uintptr_t>(self, segment::kPackedFile) = addr::cSC4DBSegmentPackedFile_PackedFileVtable;
    At<std::uintptr_t>(self, segment::kRecords) = addr::cSC4DBSegmentPackedFile_SegmentVtable;
    At<void*>(self, segment::kSerializer) = nullptr;
    At<void*>(self, segment::kMessageServer) = nullptr;
    At<uint32_t>(self, segment::kField1D4) = 0;
    At<uint32_t>(self, segment::kField1D8) = 0;
    char* face = nullptr;
    if (char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x44})) {
        if (char* const made = SerializerConstruct(memory, nullptr))
            face = made + serializer::kInterface;
    }
    AssignCounted(At<void*>(self, segment::kSerializer), face);
    AssignCounted(At<void*>(self, segment::kMessageServer), Global<void*>(addr::SC4_MessageServer));
    return self;
}

bool InstallSaveSerializer2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[22] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Save_F5D2740, reinterpret_cast<void*>(&OpenIStream)},
        {addr::SC4Save_F5D27E0, reinterpret_cast<void*>(&OpenOStream)},
        {addr::SC4Save_F5D2880, reinterpret_cast<void*>(&EraseObjectTreeHook)},
        {addr::SC4Save_F5D2990, reinterpret_cast<void*>(&CopyObjectTreeHook)},
        {addr::SC4Save_F5D2C70, reinterpret_cast<void*>(&ReadCString)},
        {addr::SC4Save_F5D2D50, reinterpret_cast<void*>(&WriteCString)},
        {addr::SC4Save_F5D2DD0, reinterpret_cast<void*>(&FindOrCreateObject)},
        {addr::SC4Save_F5D2EB0, reinterpret_cast<void*>(&CopyObjectMap)},
        {addr::SC4Save_F5D2F60, reinterpret_cast<void*>(&RecordCrc)},
        {addr::SC4Save_F5D2FE0, reinterpret_cast<void*>(&EraseClassTreeHook)},
        {addr::SC4Save_F5D3060, reinterpret_cast<void*>(&CloneClassNode)},
        {addr::SC4Save_F5D30B0, reinterpret_cast<void*>(&InsertClassNode)},
        {addr::SC4Save_F5D31A0, reinterpret_cast<void*>(&CopyClassTreeHook)},
        {addr::SC4Save_F5D3250, reinterpret_cast<void*>(&Reset)},
        {addr::SC4Save_F5D32D0, reinterpret_cast<void*>(&AssignClassMap)},
        {addr::SC4Save_F5D3390, reinterpret_cast<void*>(&InsertClass)},
        {addr::SC4Save_F5D3450, reinterpret_cast<void*>(&SerializerConstruct)},
        {addr::SC4Save_F5D34F0, reinterpret_cast<void*>(&InterfaceQueryInterface)},
        {addr::SC4Save_F5D3500, reinterpret_cast<void*>(&EndSession)},
        {addr::SC4Save_F5D3530, reinterpret_cast<void*>(&InsertClassAt)},
        {addr::cSC4DBSegmentPackedFile_Construct, reinterpret_cast<void*>(&SegmentConstruct)},
        {addr::SC4Save_F5D3800, reinterpret_cast<void*>(&SerializerDestruct)},
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

const bool g_saveSerializer2Registered = registry::Register({
    "saves.serializer.part2",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DBSegmentPackedFile_Construct,
    "Our save serializer, part 2: stream openers, segment and serializer construction, object lookup, class maps",
    false,
    InstallSaveSerializer2,
});

} // namespace
} // namespace opensc4::saves
