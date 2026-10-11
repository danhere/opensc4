// The serializer of the city save code (docs/save-format.md), part 3:
// 0x005D38D0 to 0x005D4AB0, the object that loads and saves every
// serialisable object of a city by class. Replacement, off by default:
//
//   saves.serializer.part3: loading every class's objects, saving them,
//     the class map's lookup, registering an object for the save, loading
//     one object on demand, the reference writer and reader, and the
//     deleting destructor.
//
// The serializer (cSC4COMSerializer, 0x44 bytes, made by the segment's
// constructor 0x005D3740 and kept at segment +0x1CC) has its main vtable
// 0x00AA26B4 at +0 and its interface (vtable 0x00AA268C, ten slots) at +8.
// Offsets from the object's start:
//
//   +0x0C  a byte: a save is running (objects registered now also go into
//          the copy at +0x10)
//   +0x10  the save's working copy of the class map (header, count)
//   +0x1C  the class map: class ID to {count read from the index, objects
//          registered, a map from object ID to the object}
//   +0x28  the mode: 1 loading, 2 saving
//   +0x2C  objects registered in all classes
//   +0x30  a byte: the load ran; +0x31 a byte: the save ran
//   +0x34  the segment (cISC4DBSegment)
//   +0x38  a byte vector the checksum reads a record's bytes into
//
// The maps are the game's red-black trees: a header node (+4 root, +8 first,
// +0xC last), nodes with the key at +0x10 and the value at +0x14.
//
// A class's record (type = class ID, group 0x299B2D1B, instance 0) holds its
// objects back to back: a 32-bit size (counting itself), a checksum of what
// follows the first 8 bytes (at most 250,000 bytes of it), the object's ID
// (its address when it was saved), then what the object's Write wrote. The
// index record (type 0x499B23FE, same group) has a version (1), the class
// count, the count of objects saved, and per class its ID and object count.
//
// Three of the functions have a real catch block (catch everything, around
// the object's Read or Write): here that call goes through `Guarded`.
//
// Still the game's: 0x005D4920 and 0x005D4950 (a framework service
// reference's constructor and destructor, called only by framework code),
// 0x005D49A0 and 0x005D4A20 (process memory figures through psapi.dll, not
// save code), 0x005D4A10 (the pure virtual call stub). 0x005D3B70,
// 0x005D3D60 and 0x005D3FD0 are not functions: they are loop heads and a
// catch block's resume point inside 0x005D38D0 and 0x005D3DE0.
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
#include <utility>

namespace opensc4::saves {
namespace {

namespace iface { // offsets from the serializer's interface (object + 8)
constexpr std::size_t kSaving = 0x04;
constexpr std::size_t kCopy = 0x08;
constexpr std::size_t kClasses = 0x14;
constexpr std::size_t kClassCount = 0x18;
constexpr std::size_t kMode = 0x20;
constexpr std::size_t kTotal = 0x24;
constexpr std::size_t kLoaded = 0x28;
constexpr std::size_t kSaved = 0x29;
constexpr std::size_t kSegment = 0x2C;
} // namespace iface

namespace object { // offsets from the serializer's start
constexpr std::size_t kSaving = 0x0C;
constexpr std::size_t kCopy = 0x10;
constexpr std::size_t kClasses = 0x1C;
constexpr std::size_t kMode = 0x28;
constexpr std::size_t kTotal = 0x2C;
constexpr std::size_t kSegment = 0x34;
} // namespace object

namespace segment { // cISC4DBSegment (vtable 0x00AA2654), by vtable offset
constexpr std::size_t kGetFile = 0x0C;
constexpr std::size_t kOpenRead = 0x14;
constexpr std::size_t kCloseRead = 0x18;
constexpr std::size_t kOpenWrite = 0x20;
constexpr std::size_t kCloseWrite = 0x24;
constexpr std::size_t kSetProgress = 0x34;
} // namespace segment

namespace file { // the segment's packed file part (vtable 0x00AA24F0)
constexpr std::size_t kHasRecord = 0x4C;
constexpr std::size_t kDeleteRecord = 0x6C;
} // namespace file

namespace stream { // the segment input and output streams
constexpr std::size_t kSetSint32 = 0x20;
constexpr std::size_t kUint32 = 0x24; // GetUint32 on input, SetUint32 on output
constexpr std::size_t kGetError = 0x48;
constexpr std::size_t kGetRecord = 0x60;
} // namespace stream

namespace record { // the record under a stream
constexpr std::size_t kGetSize = 0xD0;
constexpr std::size_t kGetPosition = 0xD8;
constexpr std::size_t kSetPosition = 0xDC;
} // namespace record

namespace serializable { // cIGZSerializable
constexpr std::size_t kAddRef = 0x04;
constexpr std::size_t kRelease = 0x08;
constexpr std::size_t kWrite = 0x0C;
constexpr std::size_t kRead = 0x10;
constexpr std::size_t kGetClassId = 0x14;
} // namespace serializable

constexpr uint32_t kIndexType = 0x499B23FE;
constexpr uint32_t kObjectGroup = 0x299B2D1B;
constexpr uint32_t kObjectLimit = 10000000;
constexpr uint32_t kChecksumLimit = 250000; // the dword at 0x00AA2278

struct TreeNode {
    uint8_t color;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    uint32_t key;
};
static_assert(sizeof(TreeNode) == 0x14);

struct TreeMap {
    TreeNode* header;
    uint32_t count;
    uint32_t unused;
};
static_assert(sizeof(TreeMap) == 0xC);

// What the class map keeps per class, at node + 0x14.
struct ClassValue {
    uint32_t count;      // objects the index says the record has
    uint32_t registered; // objects in the map below
    TreeMap objects;     // object ID to cIGZSerializable*
};
static_assert(sizeof(ClassValue) == 0x14);

// An object's Read or Write, run the way the original runs it: inside a
// catch of everything. The original's compiler (Visual C++ 7.1) catches
// structured exceptions in such a block too, an access violation in a Read
// as much as a C++ throw, so this is a structured handler, in a function of
// its own (crash.h, "SEH rule"). False when something was caught; the
// callers then do what the original's catch blocks do.
__declspec(noinline) bool Guarded(void* object, std::size_t slot, void* stream, uint8_t* result)
{
    __try {
        *result = Virtual<uint8_t>(object, slot, stream);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct ClassEntry {
    uint32_t classId;
    ClassValue value;
};
static_assert(sizeof(ClassEntry) == 0x18);

struct ObjectEntry {
    uint32_t id;
    void* object;
};

struct InsertResult {
    TreeNode* node;
    uint8_t inserted;
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

uint32_t Address(void* pointer)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

void Release(void* reference)
{
    Virtual<uint32_t>(reference, serializable::kRelease);
}

ClassValue* ValueOf(TreeNode* node)
{
    return reinterpret_cast<ClassValue*>(reinterpret_cast<char*>(node) + 0x14);
}

void*& ObjectOf(TreeNode* node)
{
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(node) + 0x14);
}

// The node after `node` in key order (the header after the last).
TreeNode* Next(TreeNode* node)
{
    if (node->right) {
        node = node->right;
        while (node->left)
            node = node->left;
        return node;
    }
    TreeNode* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    if (node->right != parent)
        node = parent;
    return node;
}

void MakeEmptyMap(TreeMap& map)
{
    map.header = Cdecl<TreeNode*>(addr::SC4_stl_allocate, uint32_t{0x18});
    map.count = 0;
    map.unused = 0;
    map.header->color = 0;
    map.header->parent = nullptr;
    map.header->left = map.header;
    map.header->right = map.header;
}

// The destructor of a map of objects: releases what is left in it.
void DestroyObjectMap(TreeMap& map)
{
    if (map.count != 0) {
        This<void>(addr::SC4Save_F5D2880, &map, map.header->parent);
        map.header->left = map.header;
        map.header->parent = nullptr;
        map.header->right = map.header;
        map.count = 0;
    }
    if (map.header)
        Cdecl<void>(addr::SC4_Free, map.header);
}

uint32_t ChecksumLength(uint32_t size)
{
    const uint32_t length = size - 8;
    return length > kChecksumLimit ? kChecksumLimit : length;
}

void GetUint32(void* in, uint32_t* value)
{
    Virtual<uint8_t>(in, stream::kUint32, value);
}

void SetUint32(void* out, uint32_t value)
{
    Virtual<uint8_t>(out, stream::kUint32, value);
}

uint32_t Position(void* rec)
{
    return Virtual<uint32_t>(rec, record::kGetPosition);
}

void SetPosition(void* rec, uint32_t position)
{
    Virtual<uint8_t>(rec, record::kSetPosition, position);
}

void* Segment(char* self)
{
    return At<void*>(self, iface::kSegment);
}

// The game's 0x005D38D0, slot 6 of the interface: reads the index, then
// every class's record. `self` is the interface (object + 8). The original
// leaves the read values (version, counts, size, checksum, ID) unset when a
// stream read fails; they start as zero here.
uint8_t __fastcall LoadClassObjects(char* self, void*)
{
    uint8_t ok = 0;
    if (At<int32_t>(self, iface::kMode) != 1)
        return ok;
    if (At<uint8_t>(self, iface::kLoaded) != 0)
        return ok;
    void* index = nullptr;
    uint32_t indexKey[3] = {kIndexType, kObjectGroup, 0};
    if (Virtual<uint8_t>(Segment(self), segment::kOpenRead, &indexKey[0], &index)) {
        ok = 1;
        uint32_t version = 0;
        uint32_t classes = 0;
        uint32_t objects = 0;
        GetUint32(index, &version);
        GetUint32(index, &classes);
        GetUint32(index, &objects);
        if (version != 1) {
            ok = 0;
        } else {
            while (classes != 0) {
                --classes;
                uint32_t classId = 0;
                GetUint32(index, &classId);
                TreeMap empty;
                MakeEmptyMap(empty);
                // The original copies two unset stack words into the count
                // and the registered count; the count is read just below.
                ClassEntry entry = {classId, {0, 0, {nullptr, 0, 0}}};
                This<void*>(addr::SC4Save_F5D2EB0, &entry.value.objects, &empty);
                InsertResult inserted = {nullptr, 0};
                This<void*>(addr::SC4Save_F5D3390, self + iface::kClasses, &inserted, &entry);
                DestroyObjectMap(entry.value.objects);
                DestroyObjectMap(empty);
                ClassValue* const value = ValueOf(inserted.node);
                GetUint32(index, &value->count);
                value->registered = 0;
            }
        }
        if (Virtual<uint8_t>(index, stream::kGetError))
            ok = 0;
        if (!Virtual<uint8_t>(Segment(self), segment::kCloseRead, index)) {
            ok = 0;
        } else if (ok) {
            void* in = nullptr;
            const uint32_t classCount = At<uint32_t>(self, iface::kClassCount);
            uint32_t done = 0;
            TreeNode* node = At<TreeNode*>(self, iface::kClasses)->left;
            while (node != At<TreeNode*>(self, iface::kClasses)) {
                ClassValue* const value = ValueOf(node);
                const uint32_t classId = node->key;
                uint32_t key[3] = {classId, kObjectGroup, 0};
                void* const seg = Segment(self);
                if (in) {
                    void* const old = in;
                    in = nullptr;
                    Release(old);
                }
                if (Virtual<uint8_t>(seg, segment::kOpenRead, &key[0], &in)) {
                    void* const rec = Virtual<void*>(in, stream::kGetRecord);
                    uint32_t left = value->count;
                    if (left >= kObjectLimit) {
                        ok = 0;
                    } else {
                        while (left-- != 0) {
                            const uint32_t start = Position(rec);
                            uint32_t size = 0;
                            GetUint32(in, &size);
                            if (size > Virtual<uint32_t>(rec, record::kGetSize) + start) {
                                ok = 0;
                                break;
                            }
                            uint32_t checksum = 0;
                            GetUint32(in, &checksum);
                            uint32_t computed = 0;
                            const uint32_t length = ChecksumLength(size);
                            bool good = This<uint8_t>(addr::SC4Save_F5D2F60, self - 8, rec, length, &computed) != 0;
                            if (good) {
                                ok = checksum == computed;
                                good = ok != 0;
                            }
                            if (!good) {
                                SetPosition(rec, size + start);
                                ok = 0;
                                break;
                            }
                            uint32_t id = 0;
                            GetUint32(in, &id);
                            void* target = nullptr;
                            if (!This<uint8_t>(addr::SC4Save_F5D2DD0, self - 8, &target, id, classId)) {
                                SetPosition(rec, size + start);
                            } else {
                                bool threw = false;
                                uint8_t unused = 0;
                                if (!Guarded(target, serializable::kRead, in, &unused)) {
                                    // The original's catch block 0x005D3C8B:
                                    // the object stays registered as far as
                                    // it was read, and the class's record
                                    // is given up.
                                    SetPosition(rec, size + start);
                                    ok = 0;
                                    threw = true;
                                }
                                if (threw) {
                                    if (target)
                                        Release(target);
                                    break;
                                }
                                if (Position(rec) != size + start)
                                    SetPosition(rec, size + start);
                            }
                            if (target)
                                Release(target);
                        }
                    }
                    if (!Virtual<uint8_t>(Segment(self), segment::kCloseRead, in))
                        ok = 0;
                    if (value->registered != value->count)
                        ok = 0;
                }
                Virtual<void>(Segment(self), segment::kSetProgress,
                              static_cast<float>(static_cast<double>(done) * 100.0 / static_cast<double>(classCount)));
                node = Next(node);
                ++done;
            }
            if (in)
                Release(in);
        }
    }
    At<uint8_t>(self, iface::kLoaded) = 1;
    if (index)
        Release(index);
    return ok;
}

// The game's 0x005D3DE0, slot 7 of the interface: writes every registered
// object into its class's record, then the index. `self` is the interface.
// Objects that a Write registers on the way land in the working copy too and
// are written in the same or a later pass.
uint8_t __fastcall SaveClassObjects(char* self, void*)
{
    if (At<int32_t>(self, iface::kMode) != 2)
        return 1;
    if (At<uint8_t>(self, iface::kSaved) != 0)
        return 1;
    if (At<uint8_t>(self, iface::kLoaded) == 0 && At<uint32_t>(self, iface::kClassCount) == 0)
        return 1;
    void* out = nullptr;
    At<uint8_t>(self, iface::kSaving) = 1;
    This<void*>(addr::SC4Save_F5D32D0, self + iface::kCopy, self + iface::kClasses);
    int32_t processed = 0;
    int32_t saved = 0;
    while (processed != At<int32_t>(self, iface::kTotal)) {
        TreeNode* const classEnd = At<TreeNode*>(self, iface::kCopy);
        TreeNode* node = classEnd->left;
        while (node != classEnd) {
            TreeNode* const nextClass = Next(node);
            ClassValue* const value = ValueOf(node);
            if (value->objects.count != 0) {
                uint32_t key[3] = {node->key, kObjectGroup, 0};
                void* const seg = Segment(self);
                if (out) {
                    void* const old = out;
                    out = nullptr;
                    Release(old);
                }
                if (!Virtual<uint8_t>(seg, segment::kOpenWrite, &key[0], &out, uint32_t{0})) {
                    if (out)
                        Release(out);
                    return 0;
                }
                void* const rec = Virtual<void*>(out, stream::kGetRecord);
                TreeNode* const objectEnd = value->objects.header;
                TreeNode* entry = objectEnd->left;
                while (entry != objectEnd) {
                    const uint32_t start = Position(rec);
                    TreeNode* const nextObject = Next(entry);
                    SetUint32(out, 0);
                    SetUint32(out, 0);
                    SetUint32(out, entry->key);
                    bool threw = false;
                    uint8_t written = 0;
                    if (!Guarded(ObjectOf(entry), serializable::kWrite, out, &written)) {
                        // The original's catch block 0x005D3FB3.
                        SetPosition(rec, start);
                        written = 0;
                        threw = true;
                    }
                    if (written) {
                        const uint32_t end = Position(rec);
                        const uint32_t size = end - start;
                        SetPosition(rec, start);
                        SetUint32(out, size);
                        SetPosition(rec, start + 8);
                        uint32_t checksum = 0;
                        This<uint8_t>(addr::SC4Save_F5D2F60, self - 8, rec, ChecksumLength(size), &checksum);
                        SetPosition(rec, start + 4);
                        SetUint32(out, checksum);
                        SetPosition(rec, end);
                        ++saved;
                    } else if (!threw) {
                        SetPosition(rec, start);
                    }
                    ++processed;
                    TreeNode* const header = value->objects.header;
                    TreeNode* const erased = Cdecl<TreeNode*>(addr::SC4Tree_RebalanceForErase, entry, &header->parent,
                                                              &header->left, &header->right);
                    if (void* const held = ObjectOf(erased))
                        Release(held);
                    if (erased)
                        Cdecl<void>(addr::SC4_Free, erased);
                    --value->objects.count;
                    if (Virtual<uint8_t>(out, stream::kGetError)) {
                        Virtual<uint8_t>(Segment(self), segment::kCloseWrite, out);
                        if (out)
                            Release(out);
                        return 0;
                    }
                    entry = nextObject;
                }
                if (!Virtual<uint8_t>(Segment(self), segment::kCloseWrite, out)) {
                    if (out)
                        Release(out);
                    return 0;
                }
            }
            node = nextClass;
        }
        Virtual<void>(Segment(self), segment::kSetProgress,
                      static_cast<float>(static_cast<double>(processed) * 100.0 /
                                         static_cast<double>(At<int32_t>(self, iface::kTotal))));
    }
    At<uint8_t>(self, iface::kSaving) = 0;
    uint32_t indexKey[3] = {kIndexType, kObjectGroup, 0};
    void* const seg = Segment(self);
    if (out) {
        void* const old = out;
        out = nullptr;
        Release(old);
    }
    // An index that does not open counts as saved, as in the original.
    if (Virtual<uint8_t>(seg, segment::kOpenWrite, &indexKey[0], &out, uint32_t{1})) {
        SetUint32(out, 1);
        SetUint32(out, At<uint32_t>(self, iface::kClassCount));
        Virtual<uint8_t>(out, stream::kSetSint32, saved);
        TreeNode* node = At<TreeNode*>(self, iface::kClasses)->left;
        if (node != At<TreeNode*>(self, iface::kClasses)) {
            do {
                SetUint32(out, node->key);
                SetUint32(out, ValueOf(node)->registered);
                node = Next(node);
            } while (node != At<TreeNode*>(self, iface::kClasses));
        }
        if (!Virtual<uint8_t>(Segment(self), segment::kCloseWrite, out) || Virtual<uint8_t>(out, stream::kGetError)) {
            if (out)
                Release(out);
            return 0;
        }
    }
    At<uint8_t>(self, iface::kSaved) = 1;
    if (out)
        Release(out);
    return 1;
}

// The game's 0x005D4290, slot 3 of the main vtable: the deleting destructor.
void* __fastcall DeleteSerializer(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4Save_F5D3800, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x005D42B0: the class map's value for a class ID, added empty
// when the map does not have it. The original fills a new value's two counts
// with whatever its stack holds; they are zero here.
ClassValue* __fastcall ClassAt(TreeMap* map, void*, const uint32_t* classId)
{
    TreeNode* const header = map->header;
    TreeNode* place = header;
    for (TreeNode* node = header->parent; node;) {
        if (node->key < *classId) {
            node = node->right;
        } else {
            place = node;
            node = node->left;
        }
    }
    if (place == header || *classId < place->key) {
        TreeMap empty;
        MakeEmptyMap(empty);
        ClassEntry entry = {*classId, {0, 0, {nullptr, 0, 0}}};
        This<void*>(addr::SC4Save_F5D2EB0, &entry.value.objects, &empty);
        TreeNode* inserted = nullptr;
        This<void*>(addr::SC4Save_F5D3530, map, &inserted, place, &entry);
        DestroyObjectMap(entry.value.objects);
        DestroyObjectMap(empty);
        place = inserted;
    }
    return ValueOf(place);
}

// The game's 0x005D43F0: puts an object under its class and ID (its address)
// for the save. `self` is the object's start. A class seen for the first
// time has its old record deleted from the file. Always answers true.
uint8_t __fastcall RegisterObject(char* self, void*, void* target, uint32_t id, uint32_t classId)
{
    TreeMap* const classes = reinterpret_cast<TreeMap*>(self + object::kClasses);
    TreeNode* found = nullptr;
    This<void>(addr::SC4Uint32Map_Find, classes, &found, &classId);
    TreeNode* node = found;
    if (node == classes->header) {
        uint32_t key[3] = {classId, kObjectGroup, 0};
        void* packed = Virtual<void*>(At<void*>(self, object::kSegment), segment::kGetFile);
        if (Virtual<uint8_t>(packed, file::kHasRecord, &key[0])) {
            packed = Virtual<void*>(At<void*>(self, object::kSegment), segment::kGetFile);
            Virtual<uint8_t>(packed, file::kDeleteRecord, &key[0]);
        }
        TreeMap empty;
        MakeEmptyMap(empty);
        // The original copies two unset stack words into the counts and
        // zeroes them in the node after the insert.
        ClassEntry entry = {classId, {0, 0, {nullptr, 0, 0}}};
        This<void*>(addr::SC4Save_F5D2EB0, &entry.value.objects, &empty);
        InsertResult inserted = {nullptr, 0};
        This<void*>(addr::SC4Save_F5D3390, classes, &inserted, &entry);
        DestroyObjectMap(entry.value.objects);
        DestroyObjectMap(empty);
        node = inserted.node;
        ValueOf(node)->count = 0;
        ValueOf(node)->registered = 0;
    }
    TreeMap* const objects = &ValueOf(node)->objects;
    This<void>(addr::SC4Uint32Map_Find, objects, &found, &id);
    if (found == objects->header) {
        ObjectEntry entry = {id, nullptr};
        InsertResult inserted = {nullptr, 0};
        This<void*>(addr::OrdinanceMap_Insert, objects, &inserted, &entry);
        ++ValueOf(node)->registered;
        ++At<uint32_t>(self, object::kTotal);
        void** const slot = &ObjectOf(inserted.node);
        void* const old = *slot;
        if (target != old) {
            if (target)
                Virtual<uint32_t>(target, serializable::kAddRef);
            *slot = target;
            if (old)
                Release(old);
        }
        if (At<uint8_t>(self, object::kSaving) != 0) {
            ClassValue* const copy = ClassAt(reinterpret_cast<TreeMap*>(self + object::kCopy), nullptr, &classId);
            void** const place = This<void**>(addr::OrdinanceMap_Index, &copy->objects, &id);
            This<void*>(addr::SC4RefPtr_AssignFrom, place, slot);
        }
    }
    return 1;
}

// The game's 0x005D4600: reads one object now, out of turn, by looking its
// ID up in its class's record. `self` is the object's start. The answer is
// the Read's own, false when the record or the class does not check out; it
// is true as well when the record was read to its end without finding the
// ID. The reference reader, its only caller, does not look at it.
uint8_t __fastcall LoadObject(char* self, void*, void* target, uint32_t id, uint32_t classId)
{
    uint8_t ok = 0;
    void* in = nullptr;
    ClassValue* const value = ClassAt(reinterpret_cast<TreeMap*>(self + object::kClasses), nullptr, &classId);
    uint32_t key[3] = {classId, kObjectGroup, 0};
    void* const seg = At<void*>(self, object::kSegment);
    if (!Virtual<uint8_t>(seg, segment::kOpenRead, &key[0], &in)) {
        ok = 0;
    } else {
        void* const rec = Virtual<void*>(in, stream::kGetRecord);
        uint32_t left = value->count;
        if (left >= kObjectLimit) {
            ok = 0;
        } else {
            while (left-- != 0) {
                const uint32_t start = Position(rec);
                uint32_t size = 0;
                GetUint32(in, &size);
                if (size > Virtual<uint32_t>(rec, record::kGetSize) + start) {
                    ok = 0;
                    break;
                }
                uint32_t checksum = 0;
                GetUint32(in, &checksum);
                uint32_t computed = 0;
                bool good = This<uint8_t>(addr::SC4Save_F5D2F60, self, rec, ChecksumLength(size), &computed) != 0;
                if (good) {
                    ok = checksum == computed;
                    good = ok != 0;
                }
                if (!good) {
                    SetPosition(rec, size + start);
                    ok = 0;
                    break;
                }
                uint32_t found = 0;
                GetUint32(in, &found);
                if (found != id) {
                    SetPosition(rec, size + start);
                    continue;
                }
                bool threw = false;
                if (!Guarded(target, serializable::kRead, in, &ok)) {
                    // The original's catch block 0x005D4778.
                    SetPosition(rec, size + start);
                    ok = 0;
                    threw = true;
                }
                if (!threw && ok)
                    Position(rec); // asked and not used, as in the original
                break;
            }
        }
        if (!Virtual<uint8_t>(At<void*>(self, object::kSegment), segment::kCloseRead, in))
            ok = 0;
        if (value->registered != value->count)
            ok = 0;
    }
    if (in)
        Release(in);
    return ok;
}

// The game's 0x005D4810, slot 8 of the interface: writes a reference (the
// object's address as its ID and, for an object, its class ID) and registers
// the object for the save. `self` is the interface.
uint32_t __fastcall WriteReference(char* self, void*, void* out, void* target)
{
    char* const start = self - 8;
    if (At<void*>(self, iface::kSegment) == nullptr || At<int32_t>(start, object::kMode) == 0)
        return 0;
    uint32_t classId = 0;
    if (target)
        classId = Virtual<uint32_t>(target, serializable::kGetClassId);
    SetUint32(out, Address(target));
    if (target) {
        SetUint32(out, classId);
        if (!RegisterObject(start, nullptr, target, Address(target), classId))
            return 0;
    }
    if (Virtual<uint8_t>(out, stream::kGetError))
        return 0;
    return 1;
}

// The game's 0x005D4880, slot 9 of the interface: reads a reference and
// gives the object for it, made if this is its first mention, with a count
// for the caller; `now` also reads the object's own record at once. `self`
// is the interface. A read that moves nothing leaves the stream pointer as
// the ID and the interface pointer as the class, as the original's reuse of
// its argument slot and of ECX does.
uint32_t __fastcall ReadReference(char* self, void*, void* in, void** target, uint32_t now)
{
    char* const start = self - 8;
    if (At<void*>(self, iface::kSegment) == nullptr || At<int32_t>(start, object::kMode) == 0)
        return 0;
    uint32_t id = Address(in);
    uint32_t classId = Address(self);
    GetUint32(in, &id);
    if (id == 0) {
        *target = nullptr;
    } else {
        GetUint32(in, &classId);
        if (!This<uint8_t>(addr::SC4Save_F5D2DD0, start, target, id, classId))
            return 0;
        if (static_cast<uint8_t>(now) != 0)
            LoadObject(start, nullptr, *target, id, classId);
    }
    if (Virtual<uint8_t>(in, stream::kGetError))
        return 0;
    return 1;
}

bool InstallSaveSerializer3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4COMSerializer_LoadClassObjects, reinterpret_cast<void*>(&LoadClassObjects)},
        {addr::SC4Save_F5D3DE0, reinterpret_cast<void*>(&SaveClassObjects)},
        {addr::SC4Save_F5D4290, reinterpret_cast<void*>(&DeleteSerializer)},
        {addr::SC4Save_F5D42B0, reinterpret_cast<void*>(&ClassAt)},
        {addr::SC4Save_F5D43F0, reinterpret_cast<void*>(&RegisterObject)},
        {addr::SC4Save_F5D4600, reinterpret_cast<void*>(&LoadObject)},
        {addr::SC4Save_F5D4810, reinterpret_cast<void*>(&WriteReference)},
        {addr::SC4Save_F5D4880, reinterpret_cast<void*>(&ReadReference)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        // diagnostics.prop_pox watches this function with a detour of its
        // own, and one address takes one detour: with the diagnostic on (or
        // its jump already in place), this one function stays the game's.
        if (hooks[i].first == addr::cSC4COMSerializer_LoadClassObjects
            && (registry::ModeOf("diagnostics.prop_pox") != registry::Mode::Original || *reinterpret_cast<const uint8_t*>(hooks[i].first) == 0xE9)) {
            log::Write("saves: 0x%08X left to the game while diagnostics.prop_pox is on", static_cast<unsigned>(hooks[i].first));
            continue;
        }
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveSerializer3Registered = registry::Register({
    "saves.serializer.part3",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4COMSerializer_LoadClassObjects,
    "Our serializer, part 3: loading and saving every class's objects, registering, the reference writer and reader",
    false,
    InstallSaveSerializer3,
});

} // namespace
} // namespace opensc4::saves
