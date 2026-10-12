// The framework's message server (cGZMessageServer2, and the game's subclass
// of it that the app creates) on the game's own object (docs/framework.md).
// Replacements, each off by default:
//
//   framework.messageserver.send       MessageSend (the subclass's 0x004737E0
//                                      and the base's 0x0092EA65) and the
//                                      pending changes applied before a send
//                                      (0x0092E8B5)
//   framework.messageserver.notify     AddNotification (0x0092E795),
//                                      RemoveNotification (0x0092E560) and the
//                                      replay of pending changes (0x0092DF35)
//   framework.messageserver.queue      MessagePost (0x0092D0BB), OnTick
//                                      (0x0092E040), GetMessageQueueSize
//                                      (0x0092D0F4), SetAlwaysClearQueueOnTick
//                                      (0x0092D11F), CreateMessage (0x0092D1FE)
//   framework.messageserver.direct     GeneralMessagePostToTarget
//                                      (0x0092EA00), CancelGeneralMessage-
//                                      PostsToTarget (0x0092E642) and the
//                                      delivery of direct posts (0x0092DFA3)
//   framework.messageserver.interface  QueryInterface (0x0092D091),
//                                      GetRefCount (0x0092D0B3), Init
//                                      (0x0092D05B), Shutdown (0x0092D076)
//   fixes.message_cancel               the cancel without the game's fault
//                                      (it works alone or with .direct)
//
// The object (0xB0 bytes):
//   +0x00 vtable (cIGZMessageServer2), +0x04 the system service part,
//   +0x08 a third part (its +4, the object's +0xC, is the reference count)
//   +0x20 lock of the notification table, +0x28 lock of the queue,
//   +0x30 lock of the direct posts (each an 8-byte object: slot 1 locks,
//         slot 2 unlocks)
//   +0x38 the message queue (an object of its own; still the game's)
//   +0x58 messages taken from the queue so far (64 bits)
//   +0x60 always empty the queue on a tick (byte, 1 from the constructor)
//   +0x64 busy: a collection or a change of the table is under way
//   +0x68 pending changes: a list of {add, target, type}
//   +0x6C the table: a hash table from message type to a list of targets
//   +0x80 direct posts left to deliver in the pass under way
//   +0x84 the direct posts: a deque of {target, message}
//
// The lists, the hash table's lookup and removal and the deque's simple
// cases are done here on the game's layouts, with the game's allocator; the
// table's insert (it may grow the table), the deque's new block, block
// release and erase are still the game's helpers.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kServicePart = 0x04;
constexpr std::size_t kCountPart = 0x08;
constexpr std::size_t kTableLock = 0x20;
constexpr std::size_t kQueueLock = 0x28;
constexpr std::size_t kDirectLock = 0x30;
constexpr std::size_t kQueue = 0x38;
constexpr std::size_t kTaken = 0x58;
constexpr std::size_t kAlwaysClear = 0x60;
constexpr std::size_t kBusy = 0x64;
constexpr std::size_t kPending = 0x68;
constexpr std::size_t kTable = 0x6C;
constexpr std::size_t kDirectLeft = 0x80;
constexpr std::size_t kDirect = 0x84;

constexpr uint32_t kIID_cIGZMessageServer2 = 0x652294C7;
constexpr uint32_t kCLSID_cRZMessage2Standard = 0x46DF45E7;

void* Allocate(uint32_t size)
{
    return reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(size);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void AddRef(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

// One of the three locks: held while this lives.
struct Lock {
    explicit Lock(void* lock) : lock_(lock) { Virtual<void>(lock_, 0x4); }
    ~Lock() { Virtual<uint32_t>(lock_, 0x8); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    void* lock_;
};

// A list is a pointer to its sentinel node; a node is {next, previous,
// value...}.
constexpr std::size_t kNext = 0x0;
constexpr std::size_t kPrevious = 0x4;
constexpr std::size_t kValue = 0x8;

void LinkBefore(char* position, char* node)
{
    char* const previous = At<char*>(position, kPrevious);
    At<char*>(node, kNext) = position;
    At<char*>(node, kPrevious) = previous;
    At<char*>(previous, kNext) = node;
    At<char*>(position, kPrevious) = node;
}

void Unlink(char* node)
{
    char* const next = At<char*>(node, kNext);
    char* const previous = At<char*>(node, kPrevious);
    At<char*>(previous, kNext) = next;
    At<char*>(next, kPrevious) = previous;
}

// A list of targets (what the table maps a message type to): a 4-byte
// object from operator new holding the sentinel; nodes hold one counted
// reference.
char* NewTargetList()
{
    char* const list =
        static_cast<char*>(reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(4));
    char* const sentinel = static_cast<char*>(Allocate(0xC));
    At<char*>(sentinel, kNext) = sentinel;
    At<char*>(sentinel, kPrevious) = sentinel;
    At<char*>(list, 0) = sentinel;
    return list;
}

void DeleteTargetList(char* list)
{
    char* const sentinel = At<char*>(list, 0);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const gone = node;
        node = At<char*>(node, kNext);
        Release(At<void*>(gone, kValue));
        Free(gone);
    }
    Free(sentinel);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(list);
}

void PushTarget(char* list, void* target)
{
    char* const node = static_cast<char*>(Allocate(0xC));
    At<void*>(node, kValue) = target;
    AddRef(target);
    LinkBefore(At<char*>(list, 0), node);
}

// The first node holding `target`, or the sentinel.
char* FindTarget(char* list, void* target)
{
    char* const sentinel = At<char*>(list, 0);
    char* node = At<char*>(sentinel, kNext);
    while (node != sentinel && At<void*>(node, kValue) != target)
        node = At<char*>(node, kNext);
    return node;
}

void EraseTarget(char* node)
{
    Unlink(node);
    Release(At<void*>(node, kValue));
    Free(node);
}

bool IsEmpty(char* list)
{
    char* const sentinel = At<char*>(list, 0);
    return At<char*>(sentinel, kNext) == sentinel;
}

// The table: {?, buckets, buckets' end, capacity's end, count}; a node is
// {next, message type, list of targets}.
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kBucketsEnd = 0x8;
constexpr std::size_t kCount = 0x10;
constexpr std::size_t kNodeType = 0x4;
constexpr std::size_t kNodeList = 0x8;

char** BucketOf(char* table, uint32_t type)
{
    char** const buckets = At<char**>(table, kBuckets);
    const uint32_t count = static_cast<uint32_t>(At<char**>(table, kBucketsEnd) - buckets);
    return buckets + type % count;
}

// The list of targets of a message type, or null.
char* FindList(char* table, uint32_t type)
{
    for (char* node = *BucketOf(table, type); node; node = At<char*>(node, kNext))
        if (At<uint32_t>(node, kNodeType) == type)
            return At<char*>(node, kNodeList);
    return nullptr;
}

void InsertList(char* table, uint32_t type, char* list)
{
    struct {
        uint32_t type;
        char* list;
    } value{type, list};
    // {node, table, inserted}: 9 bytes written.
    uint32_t result[3];
    using Fn = void*(__thiscall*)(char* table, void* result, void* value);
    reinterpret_cast<Fn>(addr::cGZMessageServer2_TargetTable_Insert)(table, result, &value);
}

void EraseType(char* table, uint32_t type)
{
    char** link = BucketOf(table, type);
    while (char* const node = *link) {
        if (At<uint32_t>(node, kNodeType) == type) {
            *link = At<char*>(node, kNext);
            Free(node);
            --At<uint32_t>(table, kCount);
        } else {
            link = &At<char*>(node, kNext);
        }
    }
}

// `target` gets messages of `type`, unless it does already.
void AddToTable(char* self, void* target, uint32_t type)
{
    char* const table = self + kTable;
    char* list = FindList(table, type);
    if (!list) {
        list = NewTargetList();
        InsertList(table, type, list);
    } else if (FindTarget(list, target) != At<char*>(list, 0)) {
        return;
    }
    PushTarget(list, target);
}

// The type's list given up when it has no target left.
void DropListIfEmpty(char* self, char* list, uint32_t type)
{
    if (!IsEmpty(list))
        return;
    DeleteTargetList(list);
    EraseType(self + kTable, type);
}

// The pending changes: nodes of 0x14 bytes, {next, previous, add (byte),
// target (counted), message type}.
constexpr std::size_t kPendingAdd = 0x8;
constexpr std::size_t kPendingTarget = 0xC;
constexpr std::size_t kPendingType = 0x10;

void PushPending(char* self, bool add, void* target, uint32_t type)
{
    char* const node = static_cast<char*>(Allocate(0x14));
    At<uint8_t>(node, kPendingAdd) = add ? 1 : 0;
    At<void*>(node, kPendingTarget) = target;
    AddRef(target);
    At<uint32_t>(node, kPendingType) = type;
    LinkBefore(At<char*>(self, kPending), node);
}

void FreePending(char* node)
{
    Release(At<void*>(node, kPendingTarget));
    Free(node);
}

// The game's 0x0092E8B5: every pending change applied to the table, in
// order, and the list emptied. A removal here takes every node of the
// target out of the type's list.
void __fastcall ApplyPendingNotifications(char* self, void*)
{
    char* const sentinel = At<char*>(self, kPending);
    if (At<char*>(sentinel, kNext) == sentinel)
        return;
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext)) {
        void* const target = At<void*>(node, kPendingTarget);
        const uint32_t type = At<uint32_t>(node, kPendingType);
        if (At<uint8_t>(node, kPendingAdd)) {
            AddToTable(self, target, type);
            continue;
        }
        char* const list = FindList(self + kTable, type);
        if (!list || FindTarget(list, target) == At<char*>(list, 0))
            continue;
        char* const end = At<char*>(list, 0);
        for (char* at = At<char*>(end, kNext); at != end;) {
            char* const next = At<char*>(at, kNext);
            if (At<void*>(at, kValue) == target)
                EraseTarget(at);
            at = next;
        }
        DropListIfEmpty(self, list, type);
    }
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const gone = node;
        node = At<char*>(node, kNext);
        FreePending(gone);
    }
    At<char*>(sentinel, kNext) = sentinel;
    At<char*>(sentinel, kPrevious) = sentinel;
}

// The targets a send delivers to: counted references in a block from the
// game's allocator that grows as the game's does (by its own size, from 1).
struct Targets {
    void** first = nullptr;
    void** last = nullptr;
    void** end = nullptr;

    void Push(void* target)
    {
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
        AddRef(target);
        *last++ = target;
    }

    ~Targets()
    {
        for (void** at = first; at != last; ++at)
            Release(*at);
        if (first)
            Free(first);
    }
};

// The game's 0x004737E0 and 0x0092EA65: the pending changes applied, then
// the targets of the message's type and after them those of type 0 (every
// message) collected under the table's lock, and the message given to each
// in that order with the lock released. A target taken off during the
// delivery still gets this message.
bool __fastcall MessageSend(char* self, void*, void* message)
{
    Targets targets;
    {
        Lock lock(self + kTableLock);
        ApplyPendingNotifications(self, nullptr);
        ++At<int32_t>(self, kBusy);
        for (uint32_t type = Virtual<uint32_t>(message, 0x10);; type = 0) {
            if (char* const list = FindList(self + kTable, type)) {
                char* const sentinel = At<char*>(list, 0);
                for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
                    targets.Push(At<void*>(node, kValue));
            }
            if (type == 0)
                break;
        }
        --At<int32_t>(self, kBusy);
    }
    for (void** at = targets.first; at != targets.last; ++at)
        Virtual<uint8_t>(*at, 0xC, message);
    return true;
}

// The game's 0x0092DF35: the pending changes replayed through the vtable
// (slot 5 adds, slot 6 removes), each taken off the list after its call,
// unless a replay is already under way anywhere or the server is busy.
void __fastcall FlushPendingNotifications(char* self, void*)
{
    Lock lock(self + kTableLock);
    uint8_t& replaying = Global<uint8_t>(addr::cGZMessageServer2_ReplayingPending);
    if (replaying)
        return;
    replaying = 1;
    if (At<int32_t>(self, kBusy) == 0) {
        for (;;) {
            char* const sentinel = At<char*>(self, kPending);
            char* const node = At<char*>(sentinel, kNext);
            if (node == sentinel)
                break;
            Virtual<uint8_t>(self, At<uint8_t>(node, kPendingAdd) ? 0x14 : 0x18, At<void*>(node, kPendingTarget),
                             At<uint32_t>(node, kPendingType));
            char* const first = At<char*>(At<char*>(self, kPending), kNext);
            Unlink(first);
            FreePending(first);
        }
    }
    replaying = 0;
}

// The game's 0x0092E795. While the server is busy the change only joins the
// pending list.
bool __fastcall AddNotification(char* self, void*, void* target, uint32_t type)
{
    Lock lock(self + kTableLock);
    if (At<int32_t>(self, kBusy) != 0) {
        PushPending(self, true, target, type);
        return true;
    }
    At<int32_t>(self, kBusy) = 1;
    AddToTable(self, target, type);
    --At<int32_t>(self, kBusy);
    FlushPendingNotifications(self, nullptr);
    return true;
}

// The game's 0x0092E560: the first node of the target leaves the type's
// list.
bool __fastcall RemoveNotification(char* self, void*, void* target, uint32_t type)
{
    Lock lock(self + kTableLock);
    if (At<int32_t>(self, kBusy) != 0) {
        PushPending(self, false, target, type);
        return true;
    }
    At<int32_t>(self, kBusy) = 1;
    if (char* const list = FindList(self + kTable, type)) {
        char* const node = FindTarget(list, target);
        if (node != At<char*>(list, 0)) {
            EraseTarget(node);
            DropListIfEmpty(self, list, type);
        }
    }
    --At<int32_t>(self, kBusy);
    FlushPendingNotifications(self, nullptr);
    return true;
}

// The game's 0x0092D0BB: the message to the queue's back (its slot 3), or
// its front (slot 4) with high priority.
bool __fastcall MessagePost(char* self, void*, void* message, bool highPriority)
{
    Lock lock(self + kQueueLock);
    Virtual<uint8_t>(self + kQueue, highPriority ? 0x10 : 0xC, message);
    return true;
}

// The game's 0x0092D0F4.
uint32_t __fastcall GetMessageQueueSize(char* self, void*)
{
    Lock lock(self + kQueueLock);
    return Virtual<uint32_t>(self + kQueue, 0x58);
}

// The game's 0x0092D11F. The result is the byte itself.
uint8_t __fastcall SetAlwaysClearQueueOnTick(char* self, void*, uint8_t always)
{
    At<uint8_t>(self, kAlwaysClear) = always;
    return always;
}

void __fastcall DispatchGeneralMessages(char* self, void*);

// The game's 0x0092E040: queued messages taken one at a time (the queue's
// slot 18) and sent through slot 3. Without "always clear" a tick stops
// after as many as were queued when it began. Then the direct posts, when
// there are any.
bool __fastcall OnTick(char* self, void*, uint32_t)
{
    void* const queueLock = self + kQueueLock;
    void* const queue = self + kQueue;
    uint64_t limit;
    {
        Lock lock(queueLock);
        limit = Virtual<uint32_t>(queue, 0x58) + At<uint64_t>(self, kTaken);
    }
    void* message = nullptr;
    for (;;) {
        bool taken;
        {
            Lock lock(queueLock);
            taken = Virtual<bool>(queue, 0x48, &message);
            if (taken)
                ++At<uint64_t>(self, kTaken);
        }
        if (!taken)
            break;
        Virtual<uint8_t>(self, 0xC, message);
        Virtual<uint32_t>(message, 0x8);
        if (!At<uint8_t>(self, kAlwaysClear) && At<uint64_t>(self, kTaken) >= limit)
            break;
    }
    bool posts;
    {
        Lock lock(self + kDirectLock);
        posts = At<char*>(self, kDirect + 0x10) != At<char*>(self, kDirect);
    }
    if (posts)
        DispatchGeneralMessages(self, nullptr);
    return true;
}

// The game's 0x0092D1FE: a standard message for its class ID, asked for
// the interface; nothing for any other class.
bool __fastcall CreateMessage(char*, void*, uint32_t classId, uint32_t interfaceId, void** out)
{
    if (classId != kCLSID_cRZMessage2Standard)
        return false;
    void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::RZ_operator_new)(0x2C);
    void* const message =
        memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    AddRef(message);
    const bool made = Virtual<bool>(message, 0x0, interfaceId, out);
    Virtual<uint32_t>(message, 0x8);
    return made;
}

// The direct posts: a deque of 8-byte pairs {target, message}, both counted,
// in blocks of 0x80 bytes. Two iterators {current, block's first, block's
// end, block's place in the map}: the first element at +0, one past the
// last at +0x10.
constexpr std::size_t kItCurrent = 0x0;
constexpr std::size_t kItFirst = 0x4;
constexpr std::size_t kItLast = 0x8;
constexpr std::size_t kItNode = 0xC;
constexpr std::size_t kFinish = 0x10;

struct DequeIterator {
    char* current;
    char* first;
    char* last;
    char** node;

    void Next()
    {
        current += 8;
        if (current == last) {
            ++node;
            first = *node;
            last = first + 0x80;
            current = first;
        }
    }
};

int32_t DirectCount(char* deque)
{
    char* const finish = deque + kFinish;
    return static_cast<int32_t>(At<char**>(finish, kItNode) - At<char**>(deque, kItNode)) * 16 +
           static_cast<int32_t>(At<char*>(finish, kItCurrent) - At<char*>(finish, kItFirst)) / 8 +
           static_cast<int32_t>(At<char*>(deque, kItLast) - At<char*>(deque, kItCurrent)) / 8 - 16;
}

// The game's 0x0092DFA3: as many posts as the deque held when the pass
// began are taken from its front and delivered, each with the lock
// released. The count lives in the object (+0x80), so a pass begun inside
// a delivery goes on with the outer pass's count; a finished pass leaves
// it at -1.
void __fastcall DispatchGeneralMessages(char* self, void*)
{
    char* const deque = self + kDirect;
    void* const lock = self + kDirectLock;
    int32_t& left = At<int32_t>(self, kDirectLeft);
    void* target = nullptr;
    void* message = nullptr;
    Virtual<void>(lock, 0x4);
    if (left <= 0)
        left = DirectCount(deque);
    while (left > 0) {
        --left;
        char* const front = At<char*>(deque, kItCurrent);
        if (At<char*>(deque, kFinish + kItCurrent) == front)
            continue;
        void* const newTarget = At<void*>(front, 0x0);
        if (newTarget != target) {
            AddRef(newTarget);
            Release(target);
            target = newTarget;
        }
        void* const newMessage = At<void*>(front, 0x4);
        if (newMessage != message) {
            AddRef(newMessage);
            Release(message);
            message = newMessage;
        }
        if (front != At<char*>(deque, kItLast) - 8) {
            Release(At<void*>(front, 0x4));
            Release(At<void*>(front, 0x0));
            At<char*>(deque, kItCurrent) = front + 8;
        } else {
            reinterpret_cast<void(__thiscall*)(char*)>(addr::SC4RefPairDeque_PopFrontAux)(deque);
        }
        Virtual<uint32_t>(lock, 0x8);
        Virtual<uint8_t>(target, 0xC, message);
        Virtual<void>(lock, 0x4);
    }
    --left;
    Virtual<uint32_t>(lock, 0x8);
    Release(message);
    Release(target);
}

// The game's 0x0092EA00: {target, message} to the deque's back. The result
// is what the unlock leaves in EAX, as in the game.
uint32_t __fastcall GeneralMessagePostToTarget(char* self, void*, void* message, void* target)
{
    void* const lock = self + kDirectLock;
    Virtual<void>(lock, 0x4);
    char* const deque = self + kDirect;
    void* pair[2] = {target, message};
    AddRef(target);
    AddRef(message);
    char* const at = At<char*>(deque, kFinish + kItCurrent);
    if (at != At<char*>(deque, kFinish + kItLast) - 8) {
        At<void*>(at, 0x0) = target;
        AddRef(target);
        At<void*>(at, 0x4) = message;
        AddRef(message);
        At<char*>(deque, kFinish + kItCurrent) = at + 8;
    } else {
        reinterpret_cast<void(__thiscall*)(char*, void*)>(addr::cGZMessageServer2_DirectQueue_PushBackAux)(deque,
                                                                                                          pair);
    }
    Release(message);
    Release(target);
    return Virtual<uint32_t>(lock, 0x8);
}

// fixes.message_cancel (docs/framework.md).
bool g_fixCancel = false;

// The game's 0x0092E642: every post to `target` erased, walking the deque
// with an iterator that steps past an element before it is erased. That is
// only sound when the erase moves the elements in front of it (the
// element is in the deque's first half); kept as the game has it, unless
// fixes.message_cancel is on: then the walk goes on from the position the
// erase returns.
uint32_t __fastcall CancelGeneralMessagePostsToTarget(char* self, void*, void* target)
{
    void* const lock = self + kDirectLock;
    Virtual<void>(lock, 0x4);
    char* const deque = self + kDirect;
    using EraseFn = void*(__thiscall*)(char* deque, DequeIterator* result, DequeIterator position);
    const auto erase = reinterpret_cast<EraseFn>(addr::cGZMessageServer2_DirectQueue_Erase);
    DequeIterator it = At<DequeIterator>(deque, 0);
    while (it.current != At<char*>(deque, kFinish + kItCurrent)) {
        if (At<void*>(it.current, 0x0) != target) {
            it.Next();
            continue;
        }
        const DequeIterator position = it;
        DequeIterator result;
        if (g_fixCancel) {
            erase(deque, &result, position);
            it = result;
        } else {
            it.Next();
            erase(deque, &result, position);
        }
    }
    return Virtual<uint32_t>(lock, 0x8);
}

// The game's 0x0092D091.
bool __fastcall QueryInterface(char* self, void*, uint32_t interfaceId, void** out)
{
    if (interfaceId == kIID_cIGZMessageServer2) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return true;
    }
    using Fn = bool(__thiscall*)(void* service, uint32_t interfaceId, void** out);
    return reinterpret_cast<Fn>(addr::cRZBaseSystemService_QueryInterface)(self + kServicePart, interfaceId, out);
}

// The game's 0x0092D0B3: the third part's slot 5.
uint32_t __fastcall GetRefCount(char* self, void*)
{
    return Virtual<uint32_t>(self + kCountPart, 0x14);
}

void* Framework()
{
    return reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)();
}

// The game's 0x0092D05B, on the service part: the server joins the
// framework's tick.
uint8_t __fastcall Init(char* service, void*)
{
    return Virtual<uint8_t>(Framework(), 0x24, static_cast<void*>(service));
}

// The game's 0x0092D076, on the service part: the server leaves the tick.
uint8_t __fastcall Shutdown(char* service, void*)
{
    return Virtual<uint8_t>(Framework(), 0x28, static_cast<void*>(service));
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

bool InstallAll(const Detour* detours, std::size_t count)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        // The cancel is hooked once, by whichever of
        // framework.messageserver.direct and fixes.message_cancel comes first.
        if (hook::IsInstalledAt(detours[i].address))
            continue;
        const addr::Entry* entry = addr_check::Find(detours[i].address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detours[i].ours, &original))
            return false;
    }
    return true;
}

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    return InstallAll(detours, N);
}

bool InstallSend(registry::Mode)
{
    const Detour detours[] = {
        {addr::cSC4MessageServer2_MessageSend, reinterpret_cast<void*>(&MessageSend)},
        {addr::cGZMessageServer2_MessageSend, reinterpret_cast<void*>(&MessageSend)},
        {addr::cGZMessageServer2_ApplyPendingNotifications, reinterpret_cast<void*>(&ApplyPendingNotifications)},
    };
    return InstallAll(detours);
}

bool InstallNotify(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageServer2_AddNotification, reinterpret_cast<void*>(&AddNotification)},
        {addr::cGZMessageServer2_RemoveNotification, reinterpret_cast<void*>(&RemoveNotification)},
        {addr::cGZMessageServer2_FlushPendingNotifications, reinterpret_cast<void*>(&FlushPendingNotifications)},
    };
    return InstallAll(detours);
}

bool InstallQueue(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageServer2_MessagePost, reinterpret_cast<void*>(&MessagePost)},
        {addr::cGZMessageServer2_OnTick, reinterpret_cast<void*>(&OnTick)},
        {addr::cGZMessageServer2_GetMessageQueueSize, reinterpret_cast<void*>(&GetMessageQueueSize)},
        {addr::cGZMessageServer2_SetAlwaysClearQueueOnTick, reinterpret_cast<void*>(&SetAlwaysClearQueueOnTick)},
        {addr::cGZMessageServer2_CreateMessage, reinterpret_cast<void*>(&CreateMessage)},
    };
    return InstallAll(detours);
}

bool InstallDirect(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageServer2_GeneralMessagePostToTarget, reinterpret_cast<void*>(&GeneralMessagePostToTarget)},
        {addr::cGZMessageServer2_CancelGeneralMessagePostsToTarget,
         reinterpret_cast<void*>(&CancelGeneralMessagePostsToTarget)},
        {addr::cGZMessageServer2_DispatchGeneralMessages, reinterpret_cast<void*>(&DispatchGeneralMessages)},
    };
    return InstallAll(detours);
}

bool InstallInterface(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageServer2_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cGZMessageServer2_GetRefCount, reinterpret_cast<void*>(&GetRefCount)},
        {addr::cGZMessageServer2_Init, reinterpret_cast<void*>(&Init)},
        {addr::cGZMessageServer2_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    };
    return InstallAll(detours);
}

bool InstallCancelFix(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageServer2_CancelGeneralMessagePostsToTarget,
         reinterpret_cast<void*>(&CancelGeneralMessagePostsToTarget)},
    };
    if (!InstallAll(detours))
        return false;
    g_fixCancel = true;
    return true;
}

const bool g_registeredSend = registry::Register({
    "framework.messageserver.send",
    "framework/messageserver",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MessageServer2_MessageSend,
    "Our MessageSend (the game's subclass's and the base's) and the pending notification changes applied "
    "before it (docs/framework.md)",
    false,
    InstallSend,
});

const bool g_registeredNotify = registry::Register({
    "framework.messageserver.notify",
    "framework/messageserver",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageServer2_AddNotification,
    "Our AddNotification, RemoveNotification and replay of pending changes (docs/framework.md)",
    false,
    InstallNotify,
});

const bool g_registeredQueue = registry::Register({
    "framework.messageserver.queue",
    "framework/messageserver",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageServer2_OnTick,
    "Our MessagePost, OnTick (the queue's pump), GetMessageQueueSize, SetAlwaysClearQueueOnTick and "
    "CreateMessage (docs/framework.md)",
    false,
    InstallQueue,
});

const bool g_registeredDirect = registry::Register({
    "framework.messageserver.direct",
    "framework/messageserver",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageServer2_GeneralMessagePostToTarget,
    "Our posts to one target: GeneralMessagePostToTarget, CancelGeneralMessagePostsToTarget and their "
    "delivery (docs/framework.md)",
    false,
    InstallDirect,
});

const bool g_registeredInterface = registry::Register({
    "framework.messageserver.interface",
    "framework/messageserver",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageServer2_QueryInterface,
    "Our QueryInterface, GetRefCount, Init and Shutdown of the message server (docs/framework.md)",
    false,
    InstallInterface,
});

const bool g_registeredCancelFix = registry::Register({
    "fixes.message_cancel",
    "fixes",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageServer2_CancelGeneralMessagePostsToTarget,
    "Fixes the message server's cancel of posts to one target: it skipped a post, or walked past the end, "
    "when the cancelled post was in the queue's second half (docs/framework.md)",
    false,
    InstallCancelFix,
});

} // namespace
} // namespace opensc4::framework
