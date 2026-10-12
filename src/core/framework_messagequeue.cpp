// The framework's message queue (the object at the message server's +0x38;
// vtable 0x00AD1AD0, 25 slots) on the game's own object (docs/framework.md).
// Replacements, each off by default:
//
//   framework.messagequeue.push       PushBack (0x0092DDD2), PushFront
//                                     (0x0092DE2C)
//   framework.messagequeue.pop        PopFront (0x0092D8A1), PopBack
//                                     (0x0092D8F0) and the two that wait
//                                     for a message (0x0092CFC1, 0x0092CFF7)
//   framework.messagequeue.peek       FrontType (0x0092D313), BackType
//                                     (0x0092D34B), HasType (0x0092D384),
//                                     PeekFront (0x0092D3CE), PeekBack
//                                     (0x0092D414), FindEqual (0x0092D45B),
//                                     Size (0x0092F821)
//   framework.messagequeue.remove     the removals by type and by equal
//                                     message, first, last and all
//                                     (0x0092D78B, 0x0092D66F, 0x0092D7E6,
//                                     0x0092D6CC, 0x0092D842, 0x0092D72A),
//                                     Clear (0x0092DB79), TakeEqual
//                                     (0x0092D940)
//   framework.messagequeue.interface  QueryInterface (0x0092CF85), Release
//                                     (0x0092CFAA)
//
// The object (0x20 bytes): +0x00 vtable; +0x04 a list of messages (counted
// references); +0x08 its lock; +0x10 an event, set when a message joins an
// empty queue (its slot 1) and waited on by the two waiting pops (its slot
// 3); +0x18 the reference count.
//
// The method names are ours. A message's type is its slot 4; two messages
// are equal by the queued one's slot 6 (operator==).
#include "core/address_check.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kList = 0x04;
constexpr std::size_t kLock = 0x08;
constexpr std::size_t kEvent = 0x10;
constexpr std::size_t kRefs = 0x18;

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_MessageQueue = 0xA52294CD;

char* Sentinel(char* self)
{
    return At<char*>(self, kList);
}

uint32_t TypeOf(char* node)
{
    return Virtual<uint32_t>(At<void*>(node, kValue), 0x10);
}

bool Equals(char* node, void* message)
{
    return Virtual<bool>(At<void*>(node, kValue), 0x18, message);
}

void Erase(char* node)
{
    Unlink(node);
    Release(At<void*>(node, kValue));
    Free(node);
}

// The node's message to `out`, with a reference.
void Give(char* node, void** out)
{
    void* const message = At<void*>(node, kValue);
    *out = message;
    Virtual<uint32_t>(message, 0x4);
}

// The game's 0x0092DDD2 and 0x0092DE2C: the message joins the back (or the
// front); the event is set when the queue was empty. The result is what
// the unlock leaves in EAX, as in the game.
uint32_t Push(char* self, void* message, bool front)
{
    void* const lock = self + kLock;
    Virtual<void>(lock, 0x4);
    char* const sentinel = Sentinel(self);
    const bool wasEmpty = At<char*>(sentinel, kNext) == sentinel;
    char* const node = static_cast<char*>(Allocate(0xC));
    At<void*>(node, kValue) = message;
    AddRef(message);
    LinkBefore(front ? At<char*>(sentinel, kNext) : sentinel, node);
    if (wasEmpty)
        Virtual<uint32_t>(self + kEvent, 0x4);
    return Virtual<uint32_t>(lock, 0x8);
}

uint32_t __fastcall PushBack(char* self, void*, void* message)
{
    return Push(self, message, false);
}

uint32_t __fastcall PushFront(char* self, void*, void* message)
{
    return Push(self, message, true);
}

// The game's 0x0092D78B: the first message of the type leaves the queue.
bool __fastcall RemoveFirstOfType(char* self, void*, uint32_t type)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        if (TypeOf(node) == type) {
            Erase(node);
            return true;
        }
    return false;
}

// The game's 0x0092D66F.
bool __fastcall RemoveFirstEqual(char* self, void*, void* message)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        if (Equals(node, message)) {
            Erase(node);
            return true;
        }
    return false;
}

// The game's 0x0092D7E6: the last message of the type, looked for from the
// back.
bool __fastcall RemoveLastOfType(char* self, void*, uint32_t type)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    for (char* node = At<char*>(sentinel, kPrevious); node != sentinel; node = At<char*>(node, kPrevious))
        if (TypeOf(node) == type) {
            Erase(node);
            return true;
        }
    return false;
}

// The game's 0x0092D6CC.
bool __fastcall RemoveLastEqual(char* self, void*, void* message)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    for (char* node = At<char*>(sentinel, kPrevious); node != sentinel; node = At<char*>(node, kPrevious))
        if (Equals(node, message)) {
            Erase(node);
            return true;
        }
    return false;
}

// The game's 0x0092D842: the number of messages removed.
uint32_t __fastcall RemoveAllOfType(char* self, void*, uint32_t type)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    uint32_t removed = 0;
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const next = At<char*>(node, kNext);
        if (TypeOf(node) == type) {
            Erase(node);
            ++removed;
        }
        node = next;
    }
    return removed;
}

// The game's 0x0092D72A.
uint32_t __fastcall RemoveAllEqual(char* self, void*, void* message)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    uint32_t removed = 0;
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const next = At<char*>(node, kNext);
        if (Equals(node, message)) {
            Erase(node);
            ++removed;
        }
        node = next;
    }
    return removed;
}

// The game's 0x0092DB79: the queue emptied; the number it held.
uint32_t __fastcall Clear(char* self, void*)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    uint32_t count = 0;
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        ++count;
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const gone = node;
        node = At<char*>(node, kNext);
        Release(At<void*>(gone, kValue));
        Free(gone);
    }
    At<char*>(sentinel, kNext) = sentinel;
    At<char*>(sentinel, kPrevious) = sentinel;
    return count;
}

// The game's 0x0092D313 and 0x0092D34B: the type of the first (last)
// message, 0 for an empty queue.
uint32_t __fastcall FrontType(char* self, void*)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    char* const node = At<char*>(sentinel, kNext);
    return node == sentinel ? 0 : TypeOf(node);
}

uint32_t __fastcall BackType(char* self, void*)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    return At<char*>(sentinel, kNext) == sentinel ? 0 : TypeOf(At<char*>(sentinel, kPrevious));
}

// The game's 0x0092D384.
bool __fastcall HasType(char* self, void*, uint32_t type)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        if (TypeOf(node) == type)
            return true;
    return false;
}

// The game's 0x0092D3CE and 0x0092D414: the first (last) message, with a
// reference, left in the queue; null and false for an empty queue.
bool __fastcall PeekFront(char* self, void*, void** out)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    if (At<char*>(sentinel, kNext) == sentinel) {
        *out = nullptr;
        return false;
    }
    Give(At<char*>(sentinel, kNext), out);
    return true;
}

bool __fastcall PeekBack(char* self, void*, void** out)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    if (At<char*>(sentinel, kNext) == sentinel) {
        *out = nullptr;
        return false;
    }
    Give(At<char*>(sentinel, kPrevious), out);
    return true;
}

// The game's 0x0092D45B: the first queued message equal to `message`,
// with a reference, left in the queue.
bool __fastcall FindEqual(char* self, void*, void* message, void** out)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        if (Equals(node, message)) {
            Give(node, out);
            return true;
        }
    *out = nullptr;
    return false;
}

// The game's 0x0092D940: as FindEqual, and the message leaves the queue.
bool __fastcall TakeEqual(char* self, void*, void* message, void** out)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        if (Equals(node, message)) {
            Give(node, out);
            Erase(node);
            return true;
        }
    *out = nullptr;
    return false;
}

// The game's 0x0092D8A1 and 0x0092D8F0: the first (last) message out of
// the queue, its reference passed on to the caller.
bool __fastcall PopFront(char* self, void*, void** out)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    if (At<char*>(sentinel, kNext) == sentinel) {
        *out = nullptr;
        return false;
    }
    char* const node = At<char*>(sentinel, kNext);
    Give(node, out);
    Erase(node);
    return true;
}

bool __fastcall PopBack(char* self, void*, void** out)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    if (At<char*>(sentinel, kNext) == sentinel) {
        *out = nullptr;
        return false;
    }
    char* const node = At<char*>(sentinel, kPrevious);
    Give(node, out);
    Erase(node);
    return true;
}

// The game's 0x0092CFC1 and 0x0092CFF7: the pop through the vtable (slot
// 18 or 19), tried again after each wait on the event until it gives a
// message.
bool WaitPop(char* self, void** out, std::size_t slot)
{
    while (!Virtual<bool>(self, slot, out))
        Virtual<uint32_t>(self + kEvent, 0xC, uint32_t{1000000});
    return true;
}

bool __fastcall WaitPopFront(char* self, void*, void** out)
{
    return WaitPop(self, out, 0x48);
}

bool __fastcall WaitPopBack(char* self, void*, void** out)
{
    return WaitPop(self, out, 0x4C);
}

// The game's 0x0092F821.
uint32_t __fastcall Size(char* self, void*)
{
    Lock lock(self + kLock);
    char* const sentinel = Sentinel(self);
    uint32_t count = 0;
    for (char* node = At<char*>(sentinel, kNext); node != sentinel; node = At<char*>(node, kNext))
        ++count;
    return count;
}

// The game's 0x0092CF85.
bool __fastcall QueryInterface(char* self, void*, uint32_t interfaceId, void** out)
{
    if (interfaceId != kIID_cIGZUnknown && interfaceId != kIID_MessageQueue)
        return false;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return true;
}

// The game's 0x0092CFAA: the last reference deletes the queue (slot 23
// with flag 1).
uint32_t __fastcall QueueRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, kRefs);
    if (count > 1) {
        At<uint32_t>(self, kRefs) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, 0x5C, uint32_t{1});
    return 0;
}

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

bool InstallPush(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageQueue_PushBack, reinterpret_cast<void*>(&PushBack)},
        {addr::cGZMessageQueue_PushFront, reinterpret_cast<void*>(&PushFront)},
    };
    return InstallAll(detours);
}

bool InstallPop(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageQueue_PopFront, reinterpret_cast<void*>(&PopFront)},
        {addr::cGZMessageQueue_PopBack, reinterpret_cast<void*>(&PopBack)},
        {addr::cGZMessageQueue_WaitPopFront, reinterpret_cast<void*>(&WaitPopFront)},
        {addr::cGZMessageQueue_WaitPopBack, reinterpret_cast<void*>(&WaitPopBack)},
    };
    return InstallAll(detours);
}

bool InstallPeek(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageQueue_FrontType, reinterpret_cast<void*>(&FrontType)},
        {addr::cGZMessageQueue_BackType, reinterpret_cast<void*>(&BackType)},
        {addr::cGZMessageQueue_HasType, reinterpret_cast<void*>(&HasType)},
        {addr::cGZMessageQueue_PeekFront, reinterpret_cast<void*>(&PeekFront)},
        {addr::cGZMessageQueue_PeekBack, reinterpret_cast<void*>(&PeekBack)},
        {addr::cGZMessageQueue_FindEqual, reinterpret_cast<void*>(&FindEqual)},
        {addr::cGZMessageQueue_Size, reinterpret_cast<void*>(&Size)},
    };
    return InstallAll(detours);
}

bool InstallRemove(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageQueue_RemoveFirstOfType, reinterpret_cast<void*>(&RemoveFirstOfType)},
        {addr::cGZMessageQueue_RemoveFirstEqual, reinterpret_cast<void*>(&RemoveFirstEqual)},
        {addr::cGZMessageQueue_RemoveLastOfType, reinterpret_cast<void*>(&RemoveLastOfType)},
        {addr::cGZMessageQueue_RemoveLastEqual, reinterpret_cast<void*>(&RemoveLastEqual)},
        {addr::cGZMessageQueue_RemoveAllOfType, reinterpret_cast<void*>(&RemoveAllOfType)},
        {addr::cGZMessageQueue_RemoveAllEqual, reinterpret_cast<void*>(&RemoveAllEqual)},
        {addr::cGZMessageQueue_Clear, reinterpret_cast<void*>(&Clear)},
        {addr::cGZMessageQueue_TakeEqual, reinterpret_cast<void*>(&TakeEqual)},
    };
    return InstallAll(detours);
}

bool InstallInterface(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessageQueue_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cGZMessageQueue_Release, reinterpret_cast<void*>(&QueueRelease)},
    };
    return InstallAll(detours);
}

const bool g_registeredPush = registry::Register({
    "framework.messagequeue.push",
    "framework/messagequeue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageQueue_PushBack,
    "Our message queue: a message to the back or the front (docs/framework.md)",
    false,
    InstallPush,
});

const bool g_registeredPop = registry::Register({
    "framework.messagequeue.pop",
    "framework/messagequeue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageQueue_PopFront,
    "Our message queue: a message taken from the front or the back, at once or waited for (docs/framework.md)",
    false,
    InstallPop,
});

const bool g_registeredPeek = registry::Register({
    "framework.messagequeue.peek",
    "framework/messagequeue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageQueue_PeekFront,
    "Our message queue: the queries that leave the queue as it is, and its size (docs/framework.md)",
    false,
    InstallPeek,
});

const bool g_registeredRemove = registry::Register({
    "framework.messagequeue.remove",
    "framework/messagequeue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageQueue_RemoveFirstOfType,
    "Our message queue: the removals by type and by equal message, and the emptying (docs/framework.md)",
    false,
    InstallRemove,
});

const bool g_registeredInterface = registry::Register({
    "framework.messagequeue.interface",
    "framework/messagequeue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessageQueue_QueryInterface,
    "Our QueryInterface and Release of the message queue (docs/framework.md)",
    false,
    InstallInterface,
});

} // namespace
} // namespace opensc4::framework
