// The rest of the script classes, second part (0x008BC430 to 0x008BD8B0): the
// table of Lua threads that cGZLua5 keeps at object + 0x20, and the script
// library "coroutine" the game puts in place of Lua's own.
//
//   The table (0x1C bytes): a hash map from thread ID to a record (buckets at
//     +4, +8, +0xC, count at +0x10; 1000 buckets), the owning cIGZLua5 (+0x14,
//     counted) and the next ID to give out (+0x18). A node is 0x24 bytes:
//     next, ID, then the record: the thread's lua_State (+8), its parent's
//     state (+0xC), a vector of its children's states (+0x10), flags (+0x1C)
//     and the class ID of its cIGZLua5Thread object (+0x20).
//   Flags: 0x1 keep the thread when it is dead (else Resume ends it), 0x10
//     child of the creating thread (ended with it, only it or the thread
//     itself may end it), 0x100 only its parent may resume it, 0x10000000
//     running.
//   Every lua_State has its thread object's cIGZLua5Thread* in the four
//     bytes before it; the main state's points at the cGZLua5 itself, which
//     is how a state finds the table.
//
// Replacement, off by default:
//
//   scripts.context.rest2: the 30 functions below.
//
// What is before 0x008BC430 is Lua's own ldo.c (lua_yield, f_parser,
// luaD_protectedparser, restore_stack_limit, lua_resume, luaD_pcall) and
// stays the game's, like 0x008BC520 (a find over a pointer range, folded
// with other classes' code) and the generic hash and vector helpers
// (docs/lua.md).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::script_context {
namespace {

// Slots of cIGZLua5Thread.
namespace thread {
constexpr std::size_t kOnCreate = 0x1C;
constexpr std::size_t kOnDestroy = 0x20;
constexpr std::size_t kGetId = 0x28;
constexpr std::size_t kGetTop = 0x38;
constexpr std::size_t kSetTop = 0x3C;
constexpr std::size_t kType = 0x5C;
constexpr std::size_t kPushString = 0xCC;
constexpr std::size_t kPushCClosure = 0xD0;
constexpr std::size_t kPushLightUserData = 0xD8;
constexpr std::size_t kRawSet = 0x104;
constexpr std::size_t kGetGlobal = 0x15C;
} // namespace thread

// Slots of cIGZLua5.
namespace lua5 {
constexpr std::size_t kAsThread = 0xC;
} // namespace lua5

// Slots of cIGZCOM.
namespace com {
constexpr std::size_t kGetClassObject = 0xC;
} // namespace com

// From a lua_State.
namespace state {
constexpr std::size_t kGlobal = 0x10;      // l_G
constexpr std::size_t kCCalls = 0x2E;      // nCcalls
constexpr std::size_t kMainThread = 0x50;  // in the global state
} // namespace state

constexpr uint32_t kThreadIid = 0xEB2810F1;
constexpr uint32_t kDefaultThreadClass = 0x2B2810F7;

constexpr int32_t kRegistryIndex = -10000;
constexpr int32_t kTypeNumber = 3;
constexpr int32_t kTypeString = 4;
constexpr int32_t kTypeTable = 5;
constexpr int32_t kTypeFunction = 6;

constexpr uint32_t kKeep = 0x1;
constexpr uint32_t kChild = 0x10;
constexpr uint32_t kParentResumes = 0x100;
constexpr uint32_t kRunning = 0x10000000;

struct PtrVector {
    void** begin;
    void** end;
    void** capacity;
};

struct Record {
    void* state;
    void* parent;
    PtrVector children;
    uint32_t flags;
    uint32_t classId;
};
static_assert(sizeof(Record) == 0x1C);

// What the map stores: the key and the record.
struct Pair {
    int32_t id;
    Record record;
};
static_assert(sizeof(Pair) == 0x20);

struct Node {
    Node* next;
    Pair pair;
};
static_assert(sizeof(Node) == 0x24);

struct Table {
    uint32_t unused;
    Node** begin;
    Node** end;
    Node** capacity;
    uint32_t count;
    void* owner;
    int32_t nextId;
};
static_assert(sizeof(Table) == 0x1C);

// The game's hash find writes a node and the table; the spare room is ours.
struct HashIterator {
    Node* node;
    void* table;
    uint32_t spare[2];
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

uint32_t AddRef(void* counted)
{
    return Virtual<uint32_t>(counted, 0x4);
}

uint32_t Release(void* counted)
{
    return Virtual<uint32_t>(counted, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The x87 precision switch around every Lua call: 24-bit precision (setting
// 0) is raised to 53 bits and put back afterwards.
struct Precision {
    int32_t saved;
    bool raised;
};

Precision RaisePrecision()
{
    Precision switched;
    switched.saved = Cdecl<int32_t>(addr::SC4Precision_Get);
    switched.raised = switched.saved == 0;
    if (switched.raised)
        Cdecl<int32_t>(addr::SC4Precision_Set, int32_t{1});
    return switched;
}

void RestorePrecision(const Precision& switched)
{
    if (switched.raised)
        Cdecl<int32_t>(addr::SC4Precision_Set, switched.saved);
}

// Lua's own functions.
int32_t GetTop(void* L)
{
    return Cdecl<int32_t>(addr::Lua_GetTop, L);
}

void SetTop(void* L, int32_t index)
{
    Cdecl<void>(addr::Lua_SetTop, L, index);
}

void Insert(void* L, int32_t index)
{
    Cdecl<void>(addr::Lua_Insert, L, index);
}

void XMove(void* from, void* to, int32_t count)
{
    Cdecl<void>(addr::Lua_XMove, from, to, count);
}

double ToNumber(void* L, int32_t index)
{
    return Cdecl<double>(addr::Lua_ToNumber, L, index);
}

void PushNumber(void* L, double value)
{
    Cdecl<void>(addr::Lua_PushNumber, L, value);
}

void PushBoolean(void* L, int32_t value)
{
    Cdecl<void>(addr::Lua_PushBoolean, L, value);
}

template <typename... Args>
void PushFString(void* L, const char* format, Args... args)
{
    Cdecl<const char*>(addr::Lua_PushFString, L, format, args...);
}

// The cIGZLua5Thread* kept in the four bytes before a state.
void*& ThreadOf(void* L)
{
    return static_cast<void**>(L)[-1];
}

int32_t IdOf(void* threadInterface)
{
    return Virtual<int32_t>(threadInterface, thread::kGetId);
}

// The table of the cGZLua5 that owns a state: the main state's thread object
// is the cGZLua5 (interface at object + 8, table at object + 0x20). Without
// one the original computes null + 0x20; so do we.
Table* TableOf(void* L)
{
    char* const global = At<char*>(L, state::kGlobal);
    char* const mainState = At<char*>(global, state::kMainThread);
    const std::uintptr_t owner = *reinterpret_cast<std::uintptr_t*>(mainState - 4);
    const std::uintptr_t whole = owner ? owner - 0x8 : 0;
    return reinterpret_cast<Table*>(whole + 0x20);
}

uint32_t BucketOf(const Table* self, int32_t id)
{
    return static_cast<uint32_t>(id) % static_cast<uint32_t>(self->end - self->begin);
}

Node* FindNode(const Table* self, int32_t id)
{
    Node* node = self->begin[BucketOf(self, id)];
    while (node && node->pair.id != id)
        node = node->next;
    return node;
}

HashIterator* HashFind(Table* self, HashIterator* out, const int32_t* key)
{
    return This<HashIterator*>(addr::SC4Uint32HashTable_Find, self, out, key);
}

void FreeNode(Node* node)
{
    if (node->pair.record.children.begin)
        Free(node->pair.record.children.begin);
    Free(node);
}

// The game's 0x008BC430: the thread ID a script function was given (its last
// argument, which must be a number), else the calling thread's own ID.
int32_t __cdecl IdArgument(void* L)
{
    if (GetTop(L) > 0) {
        if (Cdecl<int32_t>(addr::Lua_Type, L, int32_t{-1}) != kTypeNumber)
            Cdecl<int32_t>(addr::LuaL_TypeError, L, int32_t{1}, "numeric thread ID");
        return Ftol(ToNumber(L, -1));
    }
    void* const own = ThreadOf(L);
    if (!own)
        return -1;
    return IdOf(own);
}

// The game's 0x008BC490: the script function coroutine.id.
int32_t __cdecl ScriptId(void* L)
{
    const Precision precision = RaisePrecision();
    void* const own = ThreadOf(L);
    int32_t id = -1;
    if (own)
        id = IdOf(own);
    PushNumber(L, static_cast<double>(id));
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BC500: the table that a state's owner keeps (0x20 when the
// main state has no thread object).
void* __cdecl FromState(void* L)
{
    char* const global = At<char*>(L, state::kGlobal);
    char* const mainState = At<char*>(global, state::kMainThread);
    const std::uintptr_t owner = *reinterpret_cast<std::uintptr_t*>(mainState - 4);
    if (owner)
        return reinterpret_cast<void*>(owner + 0x18);
    return reinterpret_cast<void*>(std::uintptr_t{0x20});
}

// The game's 0x008BC540: -1 unknown, 2 running, 1 suspended (inside a
// function or with values on its stack), 0 dead.
int32_t __fastcall ThreadStatus(Table* self, void*, int32_t id)
{
    Node* const node = FindNode(self, id);
    if (!node)
        return -1;
    if (node->pair.record.flags & kRunning)
        return 2;
    char activation[0x60]; // lua_Debug
    if (Cdecl<int32_t>(addr::cSCLua_F8B97E0, node->pair.record.state, int32_t{0}, static_cast<void*>(activation)) != 0)
        return 1;
    if (GetTop(node->pair.record.state) != 0)
        return 1;
    return 0;
}

// The game's 0x008BC5D0: whether the ID names a thread whose object agrees.
uint8_t __fastcall HasThread(Table* self, void*, int32_t id)
{
    Node* const node = FindNode(self, id);
    if (!node)
        return 0;
    void* const object = ThreadOf(node->pair.record.state);
    if (object && IdOf(object) == id)
        return 1;
    return 0;
}

// The game's 0x008BC630: the ID of a thread's parent, -1 without one.
int32_t __fastcall ParentId(Table* self, void*, int32_t id)
{
    Node* const node = FindNode(self, id);
    if (!node || id == -1)
        return -1;
    void* const parent = node->pair.record.parent;
    if (!parent)
        return -1;
    void* const object = ThreadOf(parent);
    if (!object)
        return -1;
    int32_t key = IdOf(object);
    HashIterator found = {};
    if (!HashFind(self, &found, &key)->node)
        return -1;
    return IdOf(object);
}

// The game's 0x008BC6C0: the class ID of a thread's object, 0 when unknown.
uint32_t __fastcall ClassOf(Table* self, void*, int32_t id)
{
    Node* const node = FindNode(self, id);
    if (!node)
        return 0;
    return node->pair.record.classId;
}

// The game's 0x008BC700: the thread's state or null.
void* __fastcall Find(Table* self, void*, int32_t id)
{
    Node* const node = FindNode(self, id);
    if (!node)
        return nullptr;
    return node->pair.record.state;
}

// The game's 0x008BC740: the script function coroutine.valid.
int32_t __cdecl ScriptValid(void* L)
{
    const Precision precision = RaisePrecision();
    const int32_t id = IdArgument(L);
    Table* const table = TableOf(L);
    const bool valid = id != -1 && table && HasThread(table, nullptr, id);
    PushBoolean(L, valid ? 1 : 0);
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BC7D0: the script function coroutine.class.
int32_t __cdecl ScriptClass(void* L)
{
    const Precision precision = RaisePrecision();
    Table* const table = TableOf(L);
    const int32_t id = IdArgument(L);
    const int32_t classId = static_cast<int32_t>(ClassOf(table, nullptr, id));
    PushNumber(L, static_cast<double>(classId));
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BC850: the script function coroutine.status.
int32_t __cdecl ScriptStatus(void* L)
{
    const Precision precision = RaisePrecision();
    const int32_t id = IdArgument(L);
    Table* const table = TableOf(L);
    const char* name = "invalid";
    if (table) {
        switch (ThreadStatus(table, nullptr, id)) {
        case 0:
            name = "dead";
            break;
        case 1:
            name = "suspended";
            break;
        case 2:
            name = "running";
            break;
        default:
            break;
        }
    }
    Cdecl<void>(addr::Lua_PushString, L, name);
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BC8E0: the script function coroutine.parent.
int32_t __cdecl ScriptParent(void* L)
{
    const Precision precision = RaisePrecision();
    Table* const table = TableOf(L);
    const int32_t id = IdArgument(L);
    const int32_t parent = ParentId(table, nullptr, id);
    PushNumber(L, static_cast<double>(parent));
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BC960: a new node holding a copy of the pair.
Node* __fastcall NewNode(Table*, void*, const Pair* value)
{
    Node* const node = Cdecl<Node*>(addr::SC4_stl_allocate, uint32_t{sizeof(Node)});
    node->next = nullptr;
    node->pair.id = value->id;
    node->pair.record.state = value->record.state;
    node->pair.record.parent = value->record.parent;
    This<void*>(addr::SC4PtrVector_Copy, &node->pair.record.children, &value->record.children);
    node->pair.record.flags = value->record.flags;
    node->pair.record.classId = value->record.classId;
    return node;
}

// The game's 0x008BC9B0: removes every node with the key (read through the
// pointer each time, as in the original); returns how many.
uint32_t __fastcall Erase(Table* self, void*, const int32_t* key)
{
    const uint32_t bucket = BucketOf(self, *key);
    Node* const first = self->begin[bucket];
    uint32_t erased = 0;
    if (!first)
        return 0;
    Node* previous = first;
    Node* node = first->next;
    while (node) {
        if (node->pair.id == *key) {
            previous->next = node->next;
            FreeNode(node);
            ++erased;
            --self->count;
            node = previous->next;
        } else {
            previous = node;
            node = node->next;
        }
    }
    if (first->pair.id == *key) {
        self->begin[bucket] = first->next;
        FreeNode(first);
        ++erased;
        --self->count;
    }
    return erased;
}

// The game's 0x008BCA80: frees every node.
void __fastcall Clear(Table* self, void*)
{
    for (uint32_t bucket = 0; bucket < static_cast<uint32_t>(self->end - self->begin); ++bucket) {
        Node* node = self->begin[bucket];
        while (node) {
            Node* const next = node->next;
            FreeNode(node);
            node = next;
        }
        self->begin[bucket] = nullptr;
    }
    self->count = 0;
}

// The game's 0x008BCAF0: adds a node for the pair at the head of its bucket
// without looking for the key; returns the stored pair.
Pair* __fastcall InsertNew(Table* self, void*, const Pair* value)
{
    This<void>(addr::SC4Uint32HashSet_Resize, self, self->count + 1);
    const uint32_t bucket = BucketOf(self, value->id);
    Node* const first = self->begin[bucket];
    Node* const node = NewNode(self, nullptr, value);
    node->next = first;
    self->begin[bucket] = node;
    ++self->count;
    return &node->pair;
}

// The game's 0x008BCB40: the record under a key, added when there is none.
// The original adds a record whose state, parent, flags and class are
// whatever its stack held (both callers set them right after); ours are 0.
Record* __fastcall RecordFor(Table* self, void*, const int32_t* key)
{
    const int32_t id = *key;
    Node* const node = FindNode(self, id);
    if (node)
        return &node->pair.record;
    PtrVector none = {nullptr, nullptr, nullptr};
    Pair value;
    value.id = id;
    value.record.state = nullptr;
    value.record.parent = nullptr;
    This<void*>(addr::SC4PtrVector_Copy, &value.record.children, &none);
    value.record.flags = 0;
    value.record.classId = 0;
    Record* const stored = &InsertNew(self, nullptr, &value)->record;
    if (value.record.children.begin)
        Free(value.record.children.begin);
    return stored;
}

// The game's 0x008BCC20: the table's destructor.
void __fastcall Destruct(Table* self, void*)
{
    if (self->owner)
        Release(self->owner);
    Clear(self, nullptr);
    if (self->begin)
        Free(self->begin);
}

// The game's 0x008BCC50: takes the owner (which is the main thread's object)
// and the main state: the state's registry maps the state to the table, and
// the main thread gets the first ID, flagged as running for good.
uint8_t __fastcall Open(Table* self, void*, void* owner, void* mainState)
{
    void* const previous = self->owner;
    if (owner != previous) {
        if (owner)
            AddRef(owner);
        self->owner = owner;
        if (previous)
            Release(previous);
    }
    void* const object = Virtual<void*>(self->owner, lua5::kAsThread);
    if (!Virtual<uint8_t>(object, thread::kOnCreate, owner, mainState, self->nextId))
        return 0;
    Virtual<void>(object, thread::kPushLightUserData, mainState);
    Virtual<void>(object, thread::kPushLightUserData, static_cast<void*>(self));
    Virtual<void>(object, thread::kRawSet, kRegistryIndex);
    ThreadOf(mainState) = object;
    PtrVector none = {nullptr, nullptr, nullptr};
    AddRef(object);
    int32_t id = self->nextId;
    self->nextId = id + 1;
    Record* const record = RecordFor(self, nullptr, &id);
    record->state = mainState;
    record->parent = nullptr;
    This<void*>(addr::SC4PtrVector_Assign, &record->children, &none);
    record->classId = 0;
    record->flags = kRunning;
    return 1;
}

// The game's 0x008BCD50: a new Lua thread on the parent's stack with an
// object of the given class; the two values on top of the parent's stack go
// to it, the upper one (the function) is kept in the registry under the
// new state and the lower one stays as its only value. Returns the new ID
// or -1.
int32_t __fastcall Create(Table* self, void*, void* parent, uint32_t flags, uint32_t classId)
{
    int32_t result = -1;
    void* fresh = Cdecl<void*>(addr::Lua_NewThread, parent);
    void* const L = fresh;
    XMove(parent, L, 2);
    PtrVector none = {nullptr, nullptr, nullptr};
    void* object = nullptr;
    void* const classes = Cdecl<void*>(addr::RZGetCOM);
    do {
        if (!Virtual<uint8_t>(classes, com::kGetClassObject, classId, kThreadIid, &object))
            break;
        if (!Virtual<uint8_t>(object, thread::kOnCreate, self->owner, L, self->nextId))
            break;
        void* recordedParent = nullptr;
        if (flags & kChild) {
            void* const parentObject = ThreadOf(parent);
            recordedParent = parent;
            if (!parentObject)
                break;
            int32_t key = IdOf(parentObject);
            HashIterator found = {};
            HashFind(self, &found, &key);
            if (!found.node)
                break;
            This<void>(addr::SC4PtrVector_PushBack, &found.node->pair.record.children, &fresh);
        }
        result = self->nextId;
        self->nextId = result + 1;
        if (result == -1)
            break;
        Record* const record = RecordFor(self, nullptr, &result);
        record->state = L;
        record->parent = recordedParent;
        This<void*>(addr::SC4PtrVector_Assign, &record->children, &none);
        record->flags = flags;
        record->classId = classId;
        ThreadOf(L) = object;
        AddRef(object);
        Cdecl<void>(addr::Lua_PushLightUserData, L, L);
        Cdecl<void>(addr::Lua_PushValue, L, int32_t{-2});
        Cdecl<void>(addr::Lua_RawSet, L, kRegistryIndex);
        SetTop(L, 1);
    } while (false);
    if (object)
        Release(object);
    return result;
}

// The game's 0x008BCEE0: ends a thread: its children first, then its object
// (OnDestroy), its place in its parent's list, its registry entry and its
// record. A child thread may only be ended by itself, by its parent or from
// outside any state (null).
uint8_t __fastcall EndThread(Table* self, void*, void* caller, int32_t id)
{
    Node* const node = FindNode(self, id);
    if (!node)
        return 0;
    Record* const record = &node->pair.record;
    const bool child = ((record->flags >> 4) & 1) != 0;
    if (child && caller != record->state && record->parent != caller && caller)
        return 0;
    while (record->children.begin != record->children.end) {
        void* const childObject = ThreadOf(record->children.end[-1]);
        if (childObject) {
            const int32_t childId = IdOf(childObject);
            EndThread(self, nullptr, record->state, childId);
        }
    }
    void* const object = ThreadOf(record->state);
    if (!object)
        return 0;
    if (!Virtual<uint8_t>(object, thread::kOnDestroy))
        return 0;
    if (child) {
        void* const parentObject = ThreadOf(record->parent);
        if (parentObject) {
            int32_t key = IdOf(parentObject);
            HashIterator found = {};
            HashFind(self, &found, &key);
            Node* const parentNode = found.node;
            if (parentNode) {
                PtrVector& siblings = parentNode->pair.record.children;
                void** const end = siblings.end;
                void** const place = Cdecl<void**>(addr::cSCLua_F8BC520, siblings.begin, end, &record->state);
                if (place != end) {
                    siblings.begin[place - siblings.begin] = end[-1];
                    siblings.end -= 1;
                }
            }
        }
    }
    void* const L = record->state;
    Cdecl<void>(addr::Lua_PushLightUserData, L, L);
    Cdecl<void>(addr::Lua_PushNil, L);
    Cdecl<void>(addr::Lua_RawSet, L, kRegistryIndex);
    Release(object);
    ThreadOf(L) = nullptr;
    Erase(self, nullptr, &id);
    return 1;
}

// The game's 0x008BD040 (its second half is listed as 0x008BD0D0): moves the
// arguments from the caller's stack to the thread's, resumes it and moves
// everything it left back. Returns Lua's status; 1 with a message on the
// caller's stack when the thread cannot be resumed. A dead thread without
// the keep flag is ended.
int32_t __fastcall Resume(Table* self, void*, void* caller, int32_t id, int32_t arguments)
{
    Node* const node = FindNode(self, id);
    if (!node) {
        PushFString(caller, "Resuming Lua thread with invalid ID: %d.", id);
        return 1;
    }
    const uint32_t flags = node->pair.record.flags;
    void* const L = node->pair.record.state;
    const bool endWhenDead = (flags & kKeep) == 0;
    if (flags & kRunning) {
        PushFString(caller, "Lua thread %d is already running.", id);
        return 1;
    }
    if ((flags & kParentResumes) && caller != node->pair.record.parent) {
        PushFString(caller, "Lua thread %d is only resumable by its parent.", id);
        return 1;
    }
    XMove(caller, L, arguments);
    node->pair.record.flags |= kRunning;
    const int32_t status = Cdecl<int32_t>(addr::cSCLua_F8BC320, L, arguments); // lua_resume
    HashIterator found = {};
    if (HashFind(self, &found, &id)->node)
        node->pair.record.flags &= ~kRunning;
    XMove(L, caller, GetTop(L));
    if (endWhenDead && ThreadStatus(self, nullptr, id) == 0)
        EndThread(self, nullptr, L, id);
    return status;
}

// The game's 0x008BD170: the script function coroutine.resume (thread ID,
// arguments...): true and the results, or false and the message.
int32_t __cdecl ScriptResume(void* L)
{
    const Precision precision = RaisePrecision();
    int32_t arguments = GetTop(L);
    Table* const table = TableOf(L);
    bool resumed = false;
    if (arguments > 0 && table) {
        const int32_t id = Ftol(ToNumber(L, 1));
        --arguments;
        resumed = Resume(table, nullptr, L, id, arguments) == 0;
    }
    int32_t results;
    if (!resumed) {
        PushBoolean(L, 0);
        Insert(L, -2);
        results = 2;
    } else {
        PushBoolean(L, 1);
        results = GetTop(L) - 1;
        Insert(L, -results);
    }
    RestorePrecision(precision);
    return results;
}

// The game's 0x008BD240: the function coroutine.wrap returns (its upvalue is
// the thread ID): resumes the thread with its arguments and returns what
// the thread left, whatever the status.
int32_t __cdecl WrapCall(void* L)
{
    const int32_t wrapped = Ftol(ToNumber(L, -10002));
    PushNumber(L, static_cast<double>(wrapped));
    Insert(L, 1);
    Table* const table = TableOf(L);
    if (table) {
        const int32_t arguments = GetTop(L) - 1;
        const int32_t id = Ftol(ToNumber(L, 1));
        Resume(table, nullptr, L, id, arguments);
    }
    return GetTop(L) - 1;
}

// The game's 0x008BD2C0: the script function coroutine.create (function,
// options, class): the new thread's ID. The options are letters: L keep when
// dead, P child of the caller, R resumable by its parent only. The class is
// read from the second argument, as in the original.
int32_t __cdecl ScriptCreate(void* L)
{
    const Precision precision = RaisePrecision();
    int32_t id = -1;
    const int32_t count = GetTop(L);
    uint32_t flags = 0;
    if (count > 0 && Cdecl<int32_t>(addr::Lua_Type, L, int32_t{1}) == kTypeFunction
        && Cdecl<int32_t>(addr::Lua_IsCFunction, L, int32_t{1}) == 0) {
        if (count > 1) {
            Cdecl<void>(addr::LuaL_CheckType, L, int32_t{2}, kTypeString);
            for (const char* option = Cdecl<const char*>(addr::Lua_ToString, L, int32_t{2}); *option; ++option) {
                switch (*option) {
                case 'L':
                case 'l':
                    flags |= kKeep;
                    break;
                case 'P':
                case 'p':
                    flags |= kChild;
                    break;
                case 'R':
                case 'r':
                    flags |= kParentResumes;
                    break;
                default:
                    break;
                }
            }
        }
        const double classNumber = count > 2 ? ToNumber(L, 2) : static_cast<double>(kDefaultThreadClass);
        const uint32_t classId = static_cast<uint32_t>(Ftol(classNumber));
        Table* const table = TableOf(L);
        if (table) {
            SetTop(L, -count);
            id = Create(table, nullptr, L, flags, classId);
        }
    } else {
        Cdecl<int32_t>(addr::LuaL_Error, L, "Lua function is expected as the first parameter.");
    }
    PushNumber(L, static_cast<double>(id));
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BD460: the script function coroutine.wrap: creates a
// thread like coroutine.create and returns a function that resumes it.
int32_t __cdecl ScriptWrap(void* L)
{
    const Precision precision = RaisePrecision();
    ScriptCreate(L);
    Cdecl<void>(addr::Lua_PushCClosure, L, addr::cSCLua_F8BD240, int32_t{1});
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BD4B0: the script function coroutine.destroy (thread ID):
// true, or false and a message. A thread that ends itself outside any C call
// yields.
int32_t __cdecl ScriptDestroy(void* L)
{
    const Precision precision = RaisePrecision();
    Table* const table = TableOf(L);
    const int32_t own = IdOf(ThreadOf(L));
    GetTop(L);
    int32_t results = 1;
    const int32_t id = IdArgument(L);
    if (id > 0 && table && HasThread(table, nullptr, id)) {
        const uint8_t ended = EndThread(table, nullptr, L, id);
        PushBoolean(L, ended ? 1 : 0);
        if (!ended) {
            PushFString(L, "Lua thread %d can only be killed by itlself or its parent.", id);
            results = 2;
        } else if (own == id && At<uint16_t>(L, state::kCCalls) == 0) {
            const int32_t yielded = Cdecl<int32_t>(addr::Lua_Yield, L, int32_t{1});
            RestorePrecision(precision);
            return yielded;
        }
    } else {
        results = 2;
        PushBoolean(L, 0);
        if (id != 0)
            PushFString(L, "Bad Lua thread ID: %d", id);
        else
            PushFString(L, "Destruction of the main Lua thread is disallowed.");
    }
    RestorePrecision(precision);
    return results;
}

// The game's 0x008BD600: the table's constructor.
Table* __fastcall Construct(Table* self, void*)
{
    self->begin = nullptr;
    self->end = nullptr;
    self->capacity = nullptr;
    self->count = 0;
    This<void>(addr::SC4Uint32HashSet_Construct, self, uint32_t{1000});
    self->owner = nullptr;
    self->nextId = 0;
    return self;
}

// The game's 0x008BD630: ends every thread, first node of the first used
// bucket first, and lets go of the owner.
uint8_t __fastcall Close(Table* self, void*)
{
    Virtual<void*>(self->owner, lua5::kAsThread);
    while (self->count != 0) {
        Node* node = nullptr;
        for (uint32_t bucket = 0; bucket < static_cast<uint32_t>(self->end - self->begin); ++bucket) {
            if (self->begin[bucket]) {
                node = self->begin[bucket];
                break;
            }
        }
        // Without a node the original reads the ID at address 4.
        EndThread(self, nullptr, nullptr, At<int32_t>(node, 0x4));
    }
    void* const owner = self->owner;
    if (owner) {
        self->owner = nullptr;
        Release(owner);
    }
    return 1;
}

// The game's 0x008BD6A0: fills the script table `coroutine` (made by Lua's
// base library) with the game's functions.
uint8_t __cdecl OpenExtras(void* lua)
{
    static const std::pair<const char*, std::uintptr_t> kFunctions[] = {
        {"resume", addr::cSCLua_F8BD170},  {"create", addr::cSCLua_F8BD2C0}, {"wrap", addr::cSCLua_F8BD460},
        {"destroy", addr::cSCLua_F8BD4B0}, {"valid", addr::cSCLua_F8BC740},  {"id", addr::cSCLua_F8BC490},
        {"status", addr::cSCLua_F8BC850},  {"class", addr::cSCLua_F8BC7D0},  {"parent", addr::cSCLua_F8BC8E0},
    };
    const Precision precision = RaisePrecision();
    void* const mainThread = Virtual<void*>(lua, lua5::kAsThread);
    const int32_t top = Virtual<int32_t>(mainThread, thread::kGetTop);
    uint8_t done = 0;
    Virtual<void>(mainThread, thread::kGetGlobal, "coroutine");
    if (Virtual<int32_t>(mainThread, thread::kType, int32_t{-1}) == kTypeTable) {
        for (const auto& function : kFunctions) {
            Virtual<void>(mainThread, thread::kPushString, function.first);
            Virtual<void>(mainThread, thread::kPushCClosure, function.second, int32_t{0});
            Virtual<void>(mainThread, thread::kRawSet, int32_t{-3});
        }
        done = 1;
    }
    Virtual<void>(mainThread, thread::kSetTop, top);
    RestorePrecision(precision);
    return done;
}

bool InstallRest2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[30] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSCLua_F8BC430, reinterpret_cast<void*>(&IdArgument)},
        {addr::cSCLua_F8BC490, reinterpret_cast<void*>(&ScriptId)},
        {addr::SCLuaThreads_FromState, reinterpret_cast<void*>(&FromState)},
        {addr::SCLuaThreads_ThreadStatus, reinterpret_cast<void*>(&ThreadStatus)},
        {addr::cSCLua_F8BC5D0, reinterpret_cast<void*>(&HasThread)},
        {addr::cSCLua_F8BC630, reinterpret_cast<void*>(&ParentId)},
        {addr::cSCLua_F8BC6C0, reinterpret_cast<void*>(&ClassOf)},
        {addr::SCLuaThreads_Find, reinterpret_cast<void*>(&Find)},
        {addr::cSCLua_F8BC740, reinterpret_cast<void*>(&ScriptValid)},
        {addr::cSCLua_F8BC7D0, reinterpret_cast<void*>(&ScriptClass)},
        {addr::cSCLua_F8BC850, reinterpret_cast<void*>(&ScriptStatus)},
        {addr::cSCLua_F8BC8E0, reinterpret_cast<void*>(&ScriptParent)},
        {addr::cSCLua_F8BC960, reinterpret_cast<void*>(&NewNode)},
        {addr::cSCLua_F8BC9B0, reinterpret_cast<void*>(&Erase)},
        {addr::cSCLua_F8BCA80, reinterpret_cast<void*>(&Clear)},
        {addr::cSCLua_F8BCAF0, reinterpret_cast<void*>(&InsertNew)},
        {addr::cSCLua_F8BCB40, reinterpret_cast<void*>(&RecordFor)},
        {addr::SCLuaThreads_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SCLuaThreads_Open, reinterpret_cast<void*>(&Open)},
        {addr::SCLuaThreads_Create, reinterpret_cast<void*>(&Create)},
        {addr::SCLuaThreads_EndThread, reinterpret_cast<void*>(&EndThread)},
        {addr::SCLuaThreads_Resume, reinterpret_cast<void*>(&Resume)},
        {addr::cSCLua_F8BD170, reinterpret_cast<void*>(&ScriptResume)},
        {addr::cSCLua_F8BD240, reinterpret_cast<void*>(&WrapCall)},
        {addr::cSCLua_F8BD2C0, reinterpret_cast<void*>(&ScriptCreate)},
        {addr::cSCLua_F8BD460, reinterpret_cast<void*>(&ScriptWrap)},
        {addr::cSCLua_F8BD4B0, reinterpret_cast<void*>(&ScriptDestroy)},
        {addr::SCLuaThreads_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SCLuaThreads_Close, reinterpret_cast<void*>(&Close)},
        {addr::cGZLua5_OpenExtras, reinterpret_cast<void*>(&OpenExtras)},
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

const bool g_rest2Registered = registry::Register({
    "scripts.context.rest2",
    "scripts/context",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SCLuaThreads_Create,
    "Our table of Lua threads (cGZLua5's map of threads by ID: create, resume, end, status) and the game's "
    "script library coroutine (docs/lua.md)",
    false,
    InstallRest2,
});

} // namespace
} // namespace opensc4::script_context
