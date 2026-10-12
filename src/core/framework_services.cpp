// The framework's system services, tick and hooks (cGZFrameWork, the object
// RZGetFramework returns) on the game's own object (docs/framework.md).
// Replacements, each off by default:
//
//   framework.services.registry  AddSystemService (0x0087AC3C),
//                                RemoveSystemService (0x0087A90E),
//                                GetSystemService (0x0087A0AC) and the
//                                services listed by priority (0x00879ACE)
//   framework.services.tick      AddToTick (0x00879BB9), RemoveFromTick
//                                (0x00879C6C), AddToOnIdle (0x00879D21),
//                                RemoveFromOnIdle (0x00879DB3), the idle
//                                interval (0x0087946C, 0x0087944F), OnTick
//                                (0x0087A74B) and OnIdle (0x0087A812), and
//                                the game's subclass's two (0x00473440,
//                                0x004735F0)
//   framework.services.hooks     AddHook (0x0087A1F4), RemoveHook
//                                (0x008798AA) and the copy of the hooks
//                                made before they are called (0x0087A178)
//
// The members used here:
//   +0x84 the hooks: a list of counted references
//   +0x88 the services by priority: a tree {header, count}; a node is
//         {colour, parent, left, right, priority +0x10, service +0x14}
//   +0x94 the services by ID: a hash table; a node is {next, ID, service}
//   +0xA8 the idle list, +0xB0 the tick list (highest tick priority first):
//         lists of counted references
//   +0xAC, +0xB4 services taken off the idle (tick) list while it is being
//         called: kept referenced until the outermost call ends
//   +0xBC idle interval, +0xC0 frame counter, +0xC4 idle counter
//   +0xD0..+0xD4 busy bytes: the tree, the table, the idle list, the tick
//         list, the hooks. A change asked for while its byte is set is
//         refused.
//   +0xD8, +0xDC depth of OnIdle, of OnTick
//   +0xE0 the object's lock
//
// The lists, the table's lookup and removal and the tree's walk are done
// here on the game's layouts, with the game's allocator; the tree's insert
// and erase (they rebalance) and the table's insert (it may grow the table)
// are still the game's helpers. EnumSystemServices (0x0087A106) stays the
// game's: called in a running city it stalls the original, so ours could
// not be compared.
#include "core/address_check.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kHooks = 0x84;
constexpr std::size_t kTree = 0x88;
constexpr std::size_t kTable = 0x94;
constexpr std::size_t kIdleList = 0xA8;
constexpr std::size_t kIdleParked = 0xAC;
constexpr std::size_t kTickList = 0xB0;
constexpr std::size_t kTickParked = 0xB4;
constexpr std::size_t kIdleInterval = 0xBC;
constexpr std::size_t kFrame = 0xC0;
constexpr std::size_t kIdleCount = 0xC4;
constexpr std::size_t kTreeBusy = 0xD0;
constexpr std::size_t kTableBusy = 0xD1;
constexpr std::size_t kIdleBusy = 0xD2;
constexpr std::size_t kTickBusy = 0xD3;
constexpr std::size_t kHooksBusy = 0xD4;
constexpr std::size_t kIdleDepth = 0xD8;
constexpr std::size_t kTickDepth = 0xDC;
constexpr std::size_t kLock = 0xE0;

// A list of counted references: `list` holds the sentinel.
void PushBack(char* list, void* object)
{
    char* const node = static_cast<char*>(Allocate(0xC));
    At<void*>(node, kValue) = object;
    AddRef(object);
    LinkBefore(At<char*>(list, 0), node);
}

// The first node holding `object`, or the sentinel.
char* Find(char* list, void* object)
{
    char* const sentinel = At<char*>(list, 0);
    char* node = At<char*>(sentinel, kNext);
    while (node != sentinel && At<void*>(node, kValue) != object)
        node = At<char*>(node, kNext);
    return node;
}

void Erase(char* node)
{
    Unlink(node);
    Release(At<void*>(node, kValue));
    Free(node);
}

// True when `object` was in the list (and is not any more).
bool Remove(char* list, void* object)
{
    char* const node = Find(list, object);
    if (node == At<char*>(list, 0))
        return false;
    Erase(node);
    return true;
}

void Clear(char* list)
{
    char* const sentinel = At<char*>(list, 0);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const gone = node;
        node = At<char*>(node, kNext);
        Release(At<void*>(gone, kValue));
        Free(gone);
    }
    At<char*>(sentinel, kNext) = sentinel;
    At<char*>(sentinel, kPrevious) = sentinel;
}

// The tree: its header's parent is the root, its left the first node, its
// right the last.
constexpr std::size_t kColour = 0x0;
constexpr std::size_t kParent = 0x4;
constexpr std::size_t kLeft = 0x8;
constexpr std::size_t kRight = 0xC;
constexpr std::size_t kTreeService = 0x14;

char* TreeNext(char* node)
{
    if (char* right = At<char*>(node, kRight)) {
        while (char* const left = At<char*>(right, kLeft))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, kParent);
    while (node == At<char*>(parent, kRight)) {
        node = parent;
        parent = At<char*>(parent, kParent);
    }
    return At<char*>(node, kRight) != parent ? parent : node;
}

char* TreePrevious(char* node)
{
    if (At<uint8_t>(node, kColour) == 0 && At<char*>(At<char*>(node, kParent), kParent) == node)
        return At<char*>(node, kRight);
    if (char* left = At<char*>(node, kLeft)) {
        while (char* const right = At<char*>(left, kRight))
            left = right;
        return left;
    }
    char* parent = At<char*>(node, kParent);
    while (node == At<char*>(parent, kLeft)) {
        node = parent;
        parent = At<char*>(parent, kParent);
    }
    return parent;
}

// The table: {?, buckets, buckets' end, capacity's end, count}; a node is
// {next, service ID, service}.
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kBucketsEnd = 0x8;
constexpr std::size_t kCount = 0x10;
constexpr std::size_t kNodeId = 0x4;
constexpr std::size_t kNodeService = 0x8;

char* FindById(char* table, uint32_t id)
{
    char** const buckets = At<char**>(table, kBuckets);
    const uint32_t count = static_cast<uint32_t>(At<char**>(table, kBucketsEnd) - buckets);
    for (char* node = buckets[id % count]; node; node = At<char*>(node, kNext))
        if (At<uint32_t>(node, kNodeId) == id)
            return node;
    return nullptr;
}

// The first node, bucket by bucket, whose service is `service` leaves the
// table.
void EraseFromTable(char* table, void* service)
{
    char** const buckets = At<char**>(table, kBuckets);
    char** const end = At<char**>(table, kBucketsEnd);
    for (char** bucket = buckets; bucket != end; ++bucket)
        for (char** link = bucket; *link; link = &At<char*>(*link, kNext)) {
            char* const node = *link;
            if (At<void*>(node, kNodeService) != service)
                continue;
            *link = At<char*>(node, kNext);
            Release(service);
            Free(node);
            --At<uint32_t>(table, kCount);
            return;
        }
}

// The game's 0x00879ACE: the services in priority order (the lowest first
// when `ascending`) appended to `list`.
void __fastcall ListServicesByPriority(char* self, void*, char* list, uint8_t ascending)
{
    Lock lock(self + kLock);
    At<uint8_t>(self, kTreeBusy) = 1;
    char* const header = At<char*>(self, kTree);
    char* const first = At<char*>(header, kLeft);
    if (ascending) {
        for (char* node = first; node != header; node = TreeNext(node))
            PushBack(list, At<void*>(node, kTreeService));
    } else if (header != first) {
        char* node = header;
        do {
            node = TreePrevious(node);
            PushBack(list, At<void*>(node, kTreeService));
        } while (node != first);
    }
    At<uint8_t>(self, kTreeBusy) = 0;
}

// The game's 0x0087AC3C: a service joins the tree (by priority) and the
// table (by ID), unless one of the two is in use or the ID is taken.
bool __fastcall AddSystemService(char* self, void*, void* service)
{
    if (At<uint8_t>(self, kTreeBusy) || At<uint8_t>(self, kTableBusy) || !service)
        return false;
    Lock lock(self + kLock);
    const int32_t priority = Virtual<int32_t>(service, 0x14);
    const uint32_t id = Virtual<uint32_t>(service, 0xC);
    if (FindById(self + kTable, id))
        return false;

    struct {
        int32_t priority;
        void* service;
    } byPriority{priority, nullptr};
    char* treeResult[2] = {nullptr, nullptr};
    using TreeInsertFn = void*(__thiscall*)(char* tree, void* result, void* value);
    reinterpret_cast<TreeInsertFn>(addr::cGZFrameWork_ServiceTree_Insert)(self + kTree, treeResult, &byPriority);
    At<void*>(treeResult[0], kTreeService) = service;
    AddRef(service);

    struct {
        uint32_t id;
        void* service;
    } byId{id, nullptr};
    // {node, table, inserted}: 9 bytes written.
    char* tableResult[3] = {nullptr, nullptr, nullptr};
    using TableInsertFn = void*(__thiscall*)(char* table, void* result, void* value);
    reinterpret_cast<TableInsertFn>(addr::cGZFrameWork_ServiceTable_Insert)(self + kTable, tableResult, &byId);
    At<void*>(tableResult[0], kNodeService) = service;
    AddRef(service);
    return true;
}

// The game's 0x0087A90E: the service leaves the idle list, the tick list,
// the tree and the table. True unless the tree or the table is in use,
// whether the service was known or not.
bool __fastcall RemoveSystemService(char* self, void*, void* service)
{
    Lock lock(self + kLock);
    if (At<uint8_t>(self, kTreeBusy) || At<uint8_t>(self, kTableBusy))
        return false;
    Remove(self + kIdleList, service);
    Remove(self + kTickList, service);

    At<uint8_t>(self, kTreeBusy) = 1;
    char* const header = At<char*>(self, kTree);
    for (char* node = At<char*>(header, kLeft); node != header; node = TreeNext(node))
        if (At<void*>(node, kTreeService) == service) {
            using EraseFn = void(__thiscall*)(char* tree, char* node);
            reinterpret_cast<EraseFn>(addr::cGZFrameWork_ServiceTree_Erase)(self + kTree, node);
            break;
        }
    At<uint8_t>(self, kTreeBusy) = 0;

    At<uint8_t>(self, kTableBusy) = 1;
    EraseFromTable(self + kTable, service);
    At<uint8_t>(self, kTableBusy) = 0;
    return true;
}

// The game's 0x0087A0AC: the service of that ID asked for the interface.
// The table counts as in use while the service answers.
bool __fastcall GetSystemService(char* self, void*, uint32_t id, uint32_t interfaceId, void** out)
{
    Lock lock(self + kLock);
    char* const node = FindById(self + kTable, id);
    At<uint8_t>(self, kTableBusy) = 1;
    bool found = false;
    if (node)
        found = Virtual<bool>(At<void*>(node, kNodeService), 0x0, interfaceId, out);
    At<uint8_t>(self, kTableBusy) = 0;
    return found;
}

// The game's 0x00879BB9: the service joins the tick list before the first
// service of a lower tick priority, unless a service of its ID is there.
bool __fastcall AddToTick(char* self, void*, void* service)
{
    Lock lock(self + kLock);
    if (At<uint8_t>(self, kTickBusy))
        return false;
    const int32_t priority = Virtual<int32_t>(service, 0x30);
    const uint32_t id = Virtual<uint32_t>(service, 0xC);
    char* const sentinel = At<char*>(self, kTickList);
    char* position = At<char*>(sentinel, kNext);
    while (position != sentinel) {
        char* const next = At<char*>(position, kNext);
        void* const other = At<void*>(position, kValue);
        if (Virtual<uint32_t>(other, 0xC) == id)
            return true;
        if (priority > Virtual<int32_t>(other, 0x30))
            break;
        position = next;
    }
    char* const node = static_cast<char*>(Allocate(0xC));
    At<void*>(node, kValue) = service;
    AddRef(service);
    LinkBefore(position, node);
    return true;
}

// The game's 0x00879C6C and 0x00879DB3: the service leaves the list.
// During a call of the list it is first put on the parked list, in the
// list or not.
bool RemoveFromList(char* self, void* service, std::size_t busy, std::size_t depth, std::size_t parked,
                    std::size_t list)
{
    Lock lock(self + kLock);
    if (At<uint8_t>(self, busy))
        return false;
    if (At<int32_t>(self, depth) > 0)
        PushBack(self + parked, service);
    return Remove(self + list, service);
}

bool __fastcall RemoveFromTick(char* self, void*, void* service)
{
    return RemoveFromList(self, service, kTickBusy, kTickDepth, kTickParked, kTickList);
}

bool __fastcall RemoveFromOnIdle(char* self, void*, void* service)
{
    return RemoveFromList(self, service, kIdleBusy, kIdleDepth, kIdleParked, kIdleList);
}

// The game's 0x00879D21: to the idle list's end; false when it is there.
bool __fastcall AddToOnIdle(char* self, void*, void* service)
{
    Lock lock(self + kLock);
    if (At<uint8_t>(self, kIdleBusy))
        return false;
    char* const list = self + kIdleList;
    if (Find(list, service) != At<char*>(list, 0))
        return false;
    PushBack(list, service);
    return true;
}

// The game's 0x0087946C and 0x0087944F.
int32_t __fastcall GetOnIdleInterval(char* self, void*)
{
    return At<int32_t>(self, kIdleInterval);
}

int32_t __fastcall SetOnIdleInterval(char* self, void*, int32_t interval)
{
    At<int32_t>(self, kIdleInterval) = interval > 0 ? interval : 1;
    return interval;
}

// The services of a list as it is now, not counted (the parked lists keep
// alive what leaves during the calls), in a block from the game's
// allocator that grows as the game's does.
struct Snapshot {
    void** first = nullptr;
    void** last = nullptr;
    void** end = nullptr;

    void Take(char* list)
    {
        char* const sentinel = At<char*>(list, 0);
        for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext)) {
            if (last == end) {
                const uint32_t size = static_cast<uint32_t>(last - first);
                const uint32_t capacity = size + (size ? size : 1);
                auto** const block = static_cast<void**>(Allocate(capacity * 4));
                for (uint32_t i = 0; i < size; ++i)
                    block[i] = first[i];
                if (first)
                    Free(first);
                first = block;
                last = block + size;
                end = block + capacity;
            }
            *last++ = At<void*>(node, kValue);
        }
    }

    ~Snapshot()
    {
        if (first)
            Free(first);
    }
};

// The game's 0x0087A74B (and the subclass's 0x00473440): the tick list
// copied under the lock, then each service's OnTick (its slot 10) with the
// frame number, in list order.
void __fastcall OnTick(char* self, void*, uint32_t frame)
{
    void* const lock = self + kLock;
    {
        Snapshot services;
        Virtual<void>(lock, 0x4);
        ++At<int32_t>(self, kTickDepth);
        At<uint8_t>(self, kTickBusy) = 1;
        services.Take(self + kTickList);
        At<uint8_t>(self, kTickBusy) = 0;
        Virtual<uint32_t>(lock, 0x8);
        for (void** at = services.first; at != services.last; ++at)
            Virtual<uint8_t>(*at, 0x28, frame);
    }
    if (At<int32_t>(self, kTickDepth) == 1)
        Clear(self + kTickParked);
    --At<int32_t>(self, kTickDepth);
}

// The game's 0x0087A812 (and the subclass's 0x004735F0): the frame counted
// and OnTick called through the vtable with its number; on every frame the
// interval divides, the idle list's services' OnIdle (their slot 11) with
// the idle count.
void __fastcall OnIdle(char* self, void*)
{
    const uint32_t frame = At<uint32_t>(self, kFrame);
    At<uint32_t>(self, kFrame) = frame + 1;
    Virtual<void>(self, 0x3C, frame);

    void* const lock = self + kLock;
    Virtual<void>(lock, 0x4);
    const uint32_t rest = At<uint32_t>(self, kFrame) % At<uint32_t>(self, kIdleInterval);
    ++At<int32_t>(self, kIdleDepth);
    if (rest == 0) {
        Snapshot services;
        const uint32_t count = ++At<uint32_t>(self, kIdleCount);
        At<uint8_t>(self, kIdleBusy) = 1;
        services.Take(self + kIdleList);
        At<uint8_t>(self, kIdleBusy) = 0;
        Virtual<uint32_t>(lock, 0x8);
        for (void** at = services.first; at != services.last; ++at)
            Virtual<uint8_t>(*at, 0x2C, count);
    } else {
        Virtual<uint32_t>(lock, 0x8);
    }
    if (At<int32_t>(self, kIdleDepth) == 1)
        Clear(self + kIdleParked);
    --At<int32_t>(self, kIdleDepth);
}

// The game's 0x0087A1F4: to the hooks' end; false when it is there.
bool __fastcall AddHook(char* self, void*, void* hook)
{
    Lock lock(self + kLock);
    if (At<uint8_t>(self, kHooksBusy))
        return false;
    char* const list = self + kHooks;
    if (Find(list, hook) != At<char*>(list, 0))
        return false;
    PushBack(list, hook);
    return true;
}

// The game's 0x008798AA.
bool __fastcall RemoveHook(char* self, void*, void* hook)
{
    Lock lock(self + kLock);
    if (At<uint8_t>(self, kHooksBusy))
        return false;
    return Remove(self + kHooks, hook);
}

// The game's 0x0087A178: the hooks appended to `list`.
void __fastcall CopyHooks(char* self, void*, char* list)
{
    Lock lock(self + kLock);
    At<uint8_t>(self, kHooksBusy) = 1;
    char* const sentinel = At<char*>(self, kHooks);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        PushBack(list, At<void*>(node, kValue));
    At<uint8_t>(self, kHooksBusy) = 0;
}

// The two helpers are called directly by game code that stays (the
// functions that start and stop the services, the hooks' callers): entered
// through thunks that keep ECX and EDX.
OPENSC4_THUNK(ListServicesByPriorityThunk, ListServicesByPriority, 8)
OPENSC4_THUNK(CopyHooksThunk, CopyHooks, 4)

struct Detour {
    std::uintptr_t address;
    void* ours;
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detour.ours, &original))
            return false;
    }
    return true;
}

bool InstallRegistry(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZFrameWork_AddSystemService, reinterpret_cast<void*>(&AddSystemService)},
        {addr::cGZFrameWork_RemoveSystemService, reinterpret_cast<void*>(&RemoveSystemService)},
        {addr::cGZFrameWork_GetSystemService, reinterpret_cast<void*>(&GetSystemService)},
        {addr::cGZFrameWork_ListServicesByPriority, reinterpret_cast<void*>(&ListServicesByPriorityThunk)},
    };
    return InstallAll(detours);
}

bool InstallTick(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZFrameWork_AddToTick, reinterpret_cast<void*>(&AddToTick)},
        {addr::cGZFrameWork_RemoveFromTick, reinterpret_cast<void*>(&RemoveFromTick)},
        {addr::cGZFrameWork_AddToOnIdle, reinterpret_cast<void*>(&AddToOnIdle)},
        {addr::cGZFrameWork_RemoveFromOnIdle, reinterpret_cast<void*>(&RemoveFromOnIdle)},
        {addr::cGZFrameWork_GetOnIdleInterval, reinterpret_cast<void*>(&GetOnIdleInterval)},
        {addr::cGZFrameWork_SetOnIdleInterval, reinterpret_cast<void*>(&SetOnIdleInterval)},
        {addr::cGZFrameWork_OnTick, reinterpret_cast<void*>(&OnTick)},
        {addr::cGZFrameWork_OnIdle, reinterpret_cast<void*>(&OnIdle)},
        {addr::cSC4FrameWork_OnTick, reinterpret_cast<void*>(&OnTick)},
        {addr::cSC4FrameWork_OnIdle, reinterpret_cast<void*>(&OnIdle)},
    };
    return InstallAll(detours);
}

bool InstallHooks(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZFrameWork_AddHook, reinterpret_cast<void*>(&AddHook)},
        {addr::cGZFrameWork_RemoveHook, reinterpret_cast<void*>(&RemoveHook)},
        {addr::cGZFrameWork_CopyHooks, reinterpret_cast<void*>(&CopyHooksThunk)},
    };
    return InstallAll(detours);
}

const bool g_registeredRegistry = registry::Register({
    "framework.services.registry",
    "framework/services",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZFrameWork_GetSystemService,
    "Our AddSystemService, RemoveSystemService and GetSystemService of the framework and its list of services "
    "by priority (docs/framework.md)",
    false,
    InstallRegistry,
});

const bool g_registeredTick = registry::Register({
    "framework.services.tick",
    "framework/services",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZFrameWork_OnTick,
    "Our tick of the framework: the tick and idle lists, OnTick and OnIdle (docs/framework.md)",
    false,
    InstallTick,
});

const bool g_registeredHooks = registry::Register({
    "framework.services.hooks",
    "framework/services",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZFrameWork_AddHook,
    "Our AddHook and RemoveHook of the framework and the hooks' copy (docs/framework.md)",
    false,
    InstallHooks,
});

} // namespace
} // namespace opensc4::framework
