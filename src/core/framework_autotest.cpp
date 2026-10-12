// The framework's part of the autotest (src/core/autotest_hooks.h): a drive
// of the message server through its interface, with targets of our own
// (docs/framework.md).
#include "core/address_check.h"
#include "core/autotest_hooks.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZServPtrs.h>
#include <cIGZMessage2.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZSystemService.h>
#include <cRZBaseString.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include <opensc4/addresses.h>

namespace opensc4::autotest {
namespace {

// Message types of the test (no game message has them).
constexpr uint32_t kTypeA = 0x7E570A01;
constexpr uint32_t kTypeB = 0x7E570A02;
constexpr uint32_t kTypeC = 0x7E570A03;
constexpr uint32_t kTypeD = 0x7E570A04;
constexpr uint32_t kIID_cIGZMessageTarget2 = 0x452294AA;

bool g_on = false;
uint32_t g_hash = 2166136261u;
uint32_t g_deliveries = 0;
uint32_t g_refused = 0;
cIGZMessageServer2* g_server = nullptr;

void Mix(uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        g_hash = (g_hash ^ ((value >> (8 * i)) & 0xFF)) * 16777619u;
}

cIGZMessage2Standard* NewMessage(uint32_t type, intptr_t data)
{
    cIGZMessage2Standard* message = nullptr;
    g_server->CreateMessage(GZCLSID_cRZMessage2Standard, GZIID_cIGZMessage2Standard,
                            reinterpret_cast<void**>(&message));
    if (!message)
        return nullptr;
    message->SetType(type);
    message->SetData1(data);
    return message;
}

void Send(uint32_t type, intptr_t data)
{
    if (cIGZMessage2Standard* const message = NewMessage(type, data)) {
        g_server->MessageSend(message);
        message->Release();
    }
}

void Post(uint32_t type, intptr_t data, bool highPriority)
{
    if (cIGZMessage2Standard* const message = NewMessage(type, data)) {
        g_server->MessagePost(message, highPriority);
        message->Release();
    }
}

class Target final : public cIGZMessageTarget2 {
public:
    explicit Target(uint32_t index) : index_(index) {}

    bool QueryInterface(uint32_t riid, void** ppvObj) override
    {
        if (riid != GZIID_cIGZUnknown && riid != kIID_cIGZMessageTarget2)
            return false;
        *ppvObj = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++refs_; }
    uint32_t Release() override { return --refs_; }
    bool DoMessage(cIGZMessage2* message) override;

    uint32_t refs() const { return refs_; }

private:
    uint32_t index_;
    uint32_t refs_ = 1;
};

Target g_targets[3] = {Target(0), Target(1), Target(2)};

bool Target::DoMessage(cIGZMessage2* message)
{
    const uint32_t type = message->GetType();
    if (type < kTypeA || type > kTypeD)
        return true;
    const intptr_t data = static_cast<cIGZMessage2Standard*>(message)->GetData1();
    Mix(index_);
    Mix(type);
    Mix(static_cast<uint32_t>(data));
    ++g_deliveries;
    if (data >= 31)
        log::Write("autotest: message cancel: target %u got post %d", index_, static_cast<int>(data));
    // Changes from inside a delivery: target 1, on the first A, leaves A,
    // puts target 2 on B, sends a B (which applies both changes before it
    // collects) and posts a C.
    if (index_ == 1 && type == kTypeA && data == 1) {
        g_server->RemoveNotification(this, kTypeA);
        g_server->AddNotification(&g_targets[2], kTypeB);
        Send(kTypeB, 2);
        Post(kTypeC, 20, false);
    }
    // A direct post from inside a direct post's delivery.
    if (index_ == 0 && type == kTypeD && data == 8)
        if (cIGZMessage2Standard* const more = NewMessage(kTypeD, 12)) {
            g_server->GeneralMessagePostToTarget(more, &g_targets[2]);
            more->Release();
        }
    return true;
}

void DirectPost(uint32_t data, Target& target)
{
    if (cIGZMessage2Standard* const message = NewMessage(kTypeD, static_cast<intptr_t>(data))) {
        g_server->GeneralMessagePostToTarget(message, &target);
        message->Release();
    }
}

// OPENSC4_TEST_MESSAGES=<day>: notifications added (one twice), a send to
// two targets and to a target of every message, changes made from inside
// a delivery, removals, a class the server does not make, posts of both
// priorities and direct posts with one target's cancelled (its post in
// the deque's first half: see the cancel's note in docs/framework.md).
// The posts arrive on later ticks; every delivery goes into the
// fingerprint's `msg` part.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MESSAGES") || !game::IsBuild641())
        return;
    cIGZMessageServer2Ptr server;
    if (!server)
        return;
    g_server = server;
    g_on = true;
    Target& t0 = g_targets[0];
    Target& t1 = g_targets[1];
    Target& t2 = g_targets[2];

    server->AddNotification(&t0, kTypeA);
    server->AddNotification(&t1, kTypeA);
    server->AddNotification(&t0, kTypeA);
    server->AddNotification(&t2, 0);
    Send(kTypeA, 1);
    Send(kTypeA, 3);
    server->RemoveNotification(&t2, 0);
    server->RemoveNotification(&t2, 0);
    Send(kTypeB, 4);
    server->RemoveNotification(&t2, kTypeB);
    server->RemoveNotification(&t0, kTypeA);
    Send(kTypeA, 5);
    Send(kTypeB, 6);

    // The header has CreateMessage return a pointer; the game returns a
    // bool in AL.
    void* nothing = nullptr;
    const auto made = reinterpret_cast<uintptr_t>(
        server->CreateMessage(0x7E570AFF, GZIID_cIGZMessage2Standard, &nothing));
    if ((made & 0xFF) == 0 && !nothing)
        ++g_refused;

    // The interfaces: the server's own, the system service part, one it
    // does not have. (The service's Init and Shutdown are not called: they
    // put the server on and take it off the framework's tick, and taken
    // off from inside a tick it does not get back on: no message is
    // delivered from the queue any more and the game stops.)
    cIGZUnknown* same = nullptr;
    Mix(server->QueryInterface(0x652294C7, reinterpret_cast<void**>(&same)) ? 1 : 0);
    Mix(same == static_cast<cIGZUnknown*>(g_server) ? 1 : 0);
    if (same)
        same->Release();
    cIGZSystemService* service = nullptr;
    Mix(server->QueryInterface(GZIID_cIGZSystemService, reinterpret_cast<void**>(&service)) ? 1 : 0);
    if (service) {
        Mix(static_cast<uint32_t>(reinterpret_cast<char*>(service) - reinterpret_cast<char*>(g_server)));
        service->Release();
    }
    void* none = nullptr;
    Mix(server->QueryInterface(0x7E570AFE, &none) ? 1 : 0);
    Mix(server->GetRefCount());
    // The value it has (+0x60; the game runs with 0): with 1 a tick goes on
    // until the queue is empty, and the game's own posts keep it from
    // ever being so.
    const bool always = *(reinterpret_cast<const uint8_t*>(g_server) + 0x60) != 0;
    Mix(always ? 1 : 0);
    server->SetAlwaysClearQueueOnTick(always);

    server->AddNotification(&t0, kTypeC);
    server->AddNotification(&t1, kTypeC);
    Post(kTypeC, 21, false);
    Post(kTypeC, 22, true);
    Post(kTypeC, 23, false);

    DirectPost(7, t1);
    DirectPost(8, t0);
    DirectPost(9, t0);
    DirectPost(10, t2);
    DirectPost(11, t0);
    server->CancelGeneralMessagePostsToTarget(&t1);

    log::Write("autotest: messages driven (%u deliveries so far, hash %08X, refs %u %u %u)", g_deliveries, g_hash,
               t0.refs(), t1.refs(), t2.refs());
}

// OPENSC4_TEST_MESSAGE_CANCEL=<day>, only with fixes.message_cancel on (in
// the original the case walks past the deque's end): five direct posts,
// target 1's the third and the last, and target 1's cancelled. The three
// to target 0 must arrive and none to target 1.
void DriveCancel(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MESSAGE_CANCEL") || !game::IsBuild641())
        return;
    if (registry::ModeOf("fixes.message_cancel") != registry::Mode::Ours) {
        log::Write("autotest: message cancel not driven (fixes.message_cancel is off)");
        return;
    }
    cIGZMessageServer2Ptr server;
    if (!server)
        return;
    g_server = server;
    g_on = true;
    DirectPost(31, g_targets[0]);
    DirectPost(32, g_targets[0]);
    DirectPost(33, g_targets[1]);
    DirectPost(34, g_targets[0]);
    DirectPost(35, g_targets[1]);
    server->CancelGeneralMessagePostsToTarget(&g_targets[1]);
    log::Write("autotest: message cancel driven (refs %u %u %u)", g_targets[0].refs(), g_targets[1].refs(),
               g_targets[2].refs());
}

bool g_queueOn = false;
uint32_t g_queueHash = 2166136261u;

void QueueMix(uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        g_queueHash = (g_queueHash ^ ((value >> (8 * i)) & 0xFF)) * 16777619u;
}

// A message handed out by the queue into the hash (type and data, or a
// zero for none), and its reference given back.
void QueueMixMessage(void* message)
{
    if (!message) {
        QueueMix(0);
        return;
    }
    auto* const standard = static_cast<cIGZMessage2Standard*>(message);
    QueueMix(standard->GetType());
    QueueMix(static_cast<uint32_t>(standard->GetData1()));
    standard->Release();
}

// OPENSC4_TEST_MESSAGE_QUEUE=<day>: a message queue of the test's own
// (the game's constructor on 0x20 bytes from the game's operator new)
// taken through every method: pushes at both ends, the queries, the
// removals by type and by equal message, the pops, the two waiting pops
// (on a queue that has a message: an empty one would wait for ever), the
// emptying, and the last Release, which deletes it. Every result goes
// into the fingerprint's `mq` part.
void DriveQueue(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MESSAGE_QUEUE") || !game::IsBuild641())
        return;
    cIGZMessageServer2Ptr server;
    if (!server)
        return;
    g_server = server;
    void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x20);
    if (!memory)
        return;
    void* const queue = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessageQueue_cGZMessageQueue)(memory);
    g_queueOn = true;
    QueueMix(Virtual<uint32_t>(queue, 0x4));

    const auto push = [&](uint32_t type, intptr_t data, bool front) {
        if (cIGZMessage2Standard* const message = NewMessage(type, data)) {
            Virtual<uint32_t>(queue, front ? 0x10 : 0xC, static_cast<void*>(message));
            message->Release();
        }
    };
    const auto state = [&] {
        QueueMix(Virtual<uint32_t>(queue, 0x58));
        QueueMix(Virtual<uint32_t>(queue, 0x30));
        QueueMix(Virtual<uint32_t>(queue, 0x34));
    };
    const auto out = [&](std::size_t slot) {
        void* message = nullptr;
        QueueMix(Virtual<bool>(queue, slot, &message) ? 1 : 0);
        QueueMixMessage(message);
    };
    const auto outEqual = [&](std::size_t slot, uint32_t type, intptr_t data) {
        cIGZMessage2Standard* const like = NewMessage(type, data);
        void* message = nullptr;
        QueueMix(Virtual<bool>(queue, slot, static_cast<void*>(like), &message) ? 1 : 0);
        QueueMixMessage(message);
        like->Release();
    };
    const auto withEqual = [&](std::size_t slot, uint32_t type, intptr_t data) {
        cIGZMessage2Standard* const like = NewMessage(type, data);
        QueueMix(Virtual<uint32_t>(queue, slot, static_cast<void*>(like)) & 0xFF);
        like->Release();
    };

    // Empty: the queries, pops and removals that find nothing.
    state();
    out(0x3C);
    out(0x40);
    out(0x48);
    out(0x4C);
    QueueMix(Virtual<bool>(queue, 0x14, kTypeA) ? 1 : 0);
    QueueMix(Virtual<bool>(queue, 0x1C, kTypeA) ? 1 : 0);
    QueueMix(Virtual<uint32_t>(queue, 0x2C));

    // A1 B2 A3 C4 B5 A6 A3, with a C0 put in front.
    push(kTypeA, 1, false);
    push(kTypeB, 2, false);
    push(kTypeA, 3, false);
    push(kTypeC, 4, false);
    push(kTypeB, 5, false);
    push(kTypeA, 6, false);
    push(kTypeA, 3, false);
    push(kTypeC, 0, true);
    state();
    QueueMix(Virtual<bool>(queue, 0x38, kTypeB) ? 1 : 0);
    QueueMix(Virtual<bool>(queue, 0x38, kTypeD) ? 1 : 0);
    out(0x3C);
    out(0x40);
    outEqual(0x44, kTypeA, 3);
    outEqual(0x44, kTypeD, 3);

    // Removals: first and last of a type, first and last equal, then all.
    QueueMix(Virtual<bool>(queue, 0x14, kTypeB) ? 1 : 0);
    state();
    QueueMix(Virtual<bool>(queue, 0x1C, kTypeC) ? 1 : 0);
    state();
    QueueMix(Virtual<bool>(queue, 0x14, kTypeD) ? 1 : 0);
    QueueMix(Virtual<bool>(queue, 0x1C, kTypeD) ? 1 : 0);
    withEqual(0x18, kTypeA, 3);
    state();
    push(kTypeA, 3, true);
    withEqual(0x20, kTypeA, 3);
    state();
    withEqual(0x18, kTypeD, 9);
    withEqual(0x20, kTypeD, 9);
    outEqual(0x60, kTypeB, 5);
    outEqual(0x60, kTypeD, 5);
    state();
    push(kTypeA, 7, false);
    push(kTypeA, 7, true);
    push(kTypeB, 8, false);
    withEqual(0x28, kTypeA, 7);
    state();
    QueueMix(Virtual<uint32_t>(queue, 0x24, kTypeA));
    QueueMix(Virtual<uint32_t>(queue, 0x24, kTypeD));
    state();

    // The pops, plain and waiting, down to the empty queue.
    push(kTypeC, 9, false);
    push(kTypeA, 10, true);
    out(0x50);
    out(0x54);
    out(0x48);
    out(0x4C);
    state();
    out(0x48);
    out(0x4C);

    // The interfaces, the emptying, and the queue's end.
    push(kTypeB, 11, false);
    push(kTypeB, 12, false);
    void* same = nullptr;
    QueueMix(Virtual<bool>(queue, 0x0, uint32_t{0xA52294CD}, &same) ? 1 : 0);
    QueueMix(same == queue ? 1 : 0);
    void* none = nullptr;
    QueueMix(Virtual<bool>(queue, 0x0, uint32_t{0x7E570AFE}, &none) ? 1 : 0);
    QueueMix(Virtual<uint32_t>(queue, 0x8));
    QueueMix(Virtual<uint32_t>(queue, 0x2C));
    state();
    QueueMix(Virtual<uint32_t>(queue, 0x8));
    log::Write("autotest: message queue driven (hash %08X)", g_queueHash);
}

// The services test (OPENSC4_TEST_SERVICES): three services of the test's
// own in the framework's registry and lists.
bool g_servicesOn = false;
bool g_servicesDone = false;
uint32_t g_serviceHash = 2166136261u;
uint32_t g_tickHash = 2166136261u;
uint32_t g_idleHash = 2166136261u;
int g_ticksSeen = 0;
int g_idlesSeen = 0;
uint32_t g_firstFrame = 0;
uint32_t g_firstIdle = 0;
void* g_framework = nullptr;
int g_servicesAge = 0;

void MixInto(uint32_t& hash, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        hash = (hash ^ ((value >> (8 * i)) & 0xFF)) * 16777619u;
}

void FinishServices();

class TestService final : public cIGZSystemService {
public:
    TestService(uint32_t index, int32_t priority, int32_t tickPriority)
        : index_(index), priority_(priority), tickPriority_(tickPriority)
    {
    }

    bool QueryInterface(uint32_t riid, void** ppvObj) override
    {
        if (riid != GZIID_cIGZUnknown && riid != GZIID_cIGZSystemService)
            return false;
        *ppvObj = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++refs_; }
    uint32_t Release() override { return --refs_; }
    uint32_t GetServiceID() override { return 0x7E570B00 + index_; }
    cIGZSystemService* SetServiceID(uint32_t) override { return this; }
    int32_t GetServicePriority() override { return priority_; }
    bool IsServiceRunning() override { return true; }
    cIGZSystemService* SetServiceRunning(bool) override { return this; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool OnTick(uint32_t frame) override;
    bool OnIdle(uint32_t count) override;
    int32_t GetServiceTickPriority() override { return tickPriority_; }

    uint32_t refs() const { return refs_; }

private:
    uint32_t index_;
    int32_t priority_;
    int32_t tickPriority_;
    uint32_t refs_ = 1;
};

// A and C share both priorities; B has the lowest priority and the highest
// tick priority; D is never registered.
TestService g_services[4] = {TestService(1, 123, 50), TestService(2, -5, 90), TestService(3, 123, 50),
                             TestService(4, 7, 7)};

template <typename... Args>
bool Fw(std::size_t slot, Args... args)
{
    return Virtual<bool>(g_framework, slot, args...);
}

// The first three ticks: who is called, in which order, with which frame
// number (from the first). On its second tick B takes C off the tick
// list, from inside the tick: C still gets that tick. Once three ticks and
// two idle calls are in, A ends the test from its tick.
bool TestService::OnTick(uint32_t frame)
{
    if (g_servicesDone)
        return true;
    if (g_ticksSeen == 0 && index_ == 2)
        g_firstFrame = frame;
    if (index_ == 2)
        ++g_ticksSeen;
    if (g_ticksSeen >= 1 && g_ticksSeen <= 3) {
        MixInto(g_tickHash, index_);
        MixInto(g_tickHash, frame - g_firstFrame);
    }
    if (index_ == 2 && g_ticksSeen == 2)
        MixInto(g_tickHash, Fw(0x28, static_cast<void*>(&g_services[2])) ? 1 : 0);
    if (index_ == 1 && g_ticksSeen >= 3 && g_idlesSeen >= 2)
        FinishServices();
    return true;
}

// The first two idle calls: who is called, with which count (from the
// first).
bool TestService::OnIdle(uint32_t count)
{
    if (g_servicesDone || g_idlesSeen >= 2)
        return true;
    if (g_idlesSeen == 0 && index_ == 1)
        g_firstIdle = count;
    MixInto(g_idleHash, index_);
    MixInto(g_idleHash, count - g_firstIdle);
    if (index_ == 2)
        ++g_idlesSeen;
    return true;
}

void FinishServices()
{
    uint32_t& h = g_serviceHash;
    for (int i = 0; i < 3; ++i) {
        void* const service = &g_services[i];
        MixInto(h, Fw(0x28, service) ? 1 : 0);
        MixInto(h, Fw(0x30, service) ? 1 : 0);
        MixInto(h, Fw(0x10, service) ? 1 : 0);
    }
    void* out = nullptr;
    MixInto(h, Fw(0x14, uint32_t{0x7E570B01}, GZIID_cIGZSystemService, &out) ? 1 : 0);
    MixInto(h, Fw(0x10, static_cast<void*>(&g_services[3])) ? 1 : 0);
    g_servicesDone = true;
}

// The services in priority order through the framework's own list function
// (0x00879ACE), both ways: how many there are and where the test's three
// stand among them.
void MixServiceOrder(bool ascending)
{
    using namespace opensc4::framework;
    char* sentinel = static_cast<char*>(Allocate(0xC));
    At<char*>(sentinel, kNext) = sentinel;
    At<char*>(sentinel, kPrevious) = sentinel;
    using ListFn = void(__thiscall*)(void* framework, char** list, uint8_t ascending);
    reinterpret_cast<ListFn>(addr::cGZFrameWork_ListServicesByPriority)(g_framework, &sentinel, ascending ? 1 : 0);
    uint32_t count = 0;
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const gone = node;
        node = At<char*>(node, kNext);
        void* const service = At<void*>(gone, kValue);
        for (uint32_t i = 0; i < 4; ++i)
            if (service == static_cast<cIGZSystemService*>(&g_services[i])) {
                MixInto(g_serviceHash, i + 1);
                MixInto(g_serviceHash, count);
            }
        ++count;
        Release(service);
        Free(gone);
    }
    Free(sentinel);
    MixInto(g_serviceHash, count);
}

// OPENSC4_TEST_SERVICES=<day>: three services added to the framework (one
// twice), looked up, the services listed by priority both ways, the three put on the tick list (one twice) and two
// on the idle list (one twice), removals of what is not there, a hook
// added twice and removed twice, the idle interval read and set to what it
// is. The services then record their first three ticks and two idle
// calls and take themselves out again. Everything goes into the
// fingerprint's `svc` part, which appears ten days after the drive (run at
// least 13 days).
void DriveServices(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_SERVICES") || !game::IsBuild641())
        return;
    g_framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)();
    if (!g_framework)
        return;
    g_servicesOn = true;
    uint32_t& h = g_serviceHash;
    void* const a = &g_services[0];
    void* const b = &g_services[1];
    void* const c = &g_services[2];
    void* const d = &g_services[3];

    MixInto(h, Fw(0xC, a) ? 1 : 0);
    MixInto(h, Fw(0xC, b) ? 1 : 0);
    MixInto(h, Fw(0xC, c) ? 1 : 0);
    MixInto(h, Fw(0xC, a) ? 1 : 0);
    void* out = nullptr;
    MixInto(h, Fw(0x14, uint32_t{0x7E570B02}, GZIID_cIGZSystemService, &out) ? 1 : 0);
    MixInto(h, out == static_cast<cIGZSystemService*>(&g_services[1]) ? 1 : 0);
    if (out)
        g_services[1].Release();
    out = nullptr;
    MixInto(h, Fw(0x14, uint32_t{0x7E570B04}, GZIID_cIGZSystemService, &out) ? 1 : 0);
    MixInto(h, Fw(0x14, uint32_t{0x7E570B02}, uint32_t{0x7E570AFE}, &out) ? 1 : 0);

    // Not EnumSystemServices (slot 6): called in a running city it stalls
    // the original after 32 services.
    MixServiceOrder(true);
    MixServiceOrder(false);

    MixInto(h, Fw(0x24, a) ? 1 : 0);
    MixInto(h, Fw(0x24, b) ? 1 : 0);
    MixInto(h, Fw(0x24, c) ? 1 : 0);
    MixInto(h, Fw(0x24, a) ? 1 : 0);
    MixInto(h, Fw(0x2C, a) ? 1 : 0);
    MixInto(h, Fw(0x2C, a) ? 1 : 0);
    MixInto(h, Fw(0x2C, b) ? 1 : 0);
    MixInto(h, Fw(0x30, c) ? 1 : 0);
    MixInto(h, Fw(0x28, d) ? 1 : 0);

    // Any counted object does for a hook that is never called.
    void* const hook = &g_targets[2];
    MixInto(h, Fw(0x1C, hook) ? 1 : 0);
    MixInto(h, Fw(0x1C, hook) ? 1 : 0);
    MixInto(h, Fw(0x20, hook) ? 1 : 0);
    MixInto(h, Fw(0x20, hook) ? 1 : 0);

    const int32_t interval = Virtual<int32_t>(g_framework, 0x34);
    MixInto(h, static_cast<uint32_t>(interval));
    Virtual<int32_t>(g_framework, 0x38, interval);
    MixInto(h, static_cast<uint32_t>(Virtual<int32_t>(g_framework, 0x34)));
    log::Write("autotest: services driven (hash %08X)", h);
}

void ServicesFingerprint(cISC4City*, std::string& line)
{
    // Which frame a tick or an idle call falls on differs from run to run
    // (frames pass while the city loads), so the part appears only ten
    // days after the drive, when the services have long finished.
    if (!g_servicesOn || ++g_servicesAge < 10)
        return;
    char text[128];
    std::snprintf(text, sizeof text, " | svc %08X %08X %08X %s refs %u %u %u %u", g_serviceHash, g_tickHash,
                  g_idleHash, g_servicesDone ? "done" : "open", g_services[0].refs(), g_services[1].refs(),
                  g_services[2].refs(), g_services[3].refs());
    line += text;
}

// The directors test (OPENSC4_TEST_CLASSES).
bool g_classesOn = false;
uint32_t g_classHash = 2166136261u;
uint32_t g_objectsMade = 0;
uint32_t g_objectsEnded = 0;

constexpr uint32_t kClassA = 0x7E570C01;
constexpr uint32_t kClassB = 0x7E570C02;
constexpr uint32_t kClassC = 0x7E570C03;
constexpr uint32_t kTestInterface = 0x7E570C10;

// What the test's factories make: it has the base interface and one of
// its own, and ends with its last reference.
class TestObject final : public cIGZUnknown {
public:
    explicit TestObject(uint32_t maker) : maker_(maker) { ++g_objectsMade; }

    bool QueryInterface(uint32_t riid, void** ppvObj) override
    {
        if (riid != GZIID_cIGZUnknown && riid != kTestInterface)
            return false;
        *ppvObj = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++refs_; }
    uint32_t Release() override
    {
        const uint32_t left = --refs_;
        if (left == 0) {
            ++g_objectsEnded;
            delete this;
        }
        return left;
    }
    uint32_t maker() const { return maker_; }

private:
    uint32_t maker_;
    uint32_t refs_ = 0;
};

cIGZUnknown* __cdecl MakeFirst()
{
    return new TestObject(1);
}

cIGZUnknown* __cdecl MakeSecond()
{
    return new TestObject(2);
}

cIGZUnknown* __cdecl MakeThird()
{
    return new TestObject(3);
}

// A factory of the second kind: it is given the interface ID.
bool __cdecl MakeWithInterface(uint32_t riid, void** ppvObj)
{
    auto* const object = new TestObject(4);
    if (object->QueryInterface(riid, ppvObj))
        return true;
    object->AddRef();
    object->Release();
    return false;
}

void __cdecl MixClass(uint32_t classId, uint32_t version, void* context)
{
    MixInto(g_classHash, classId);
    MixInto(g_classHash, version);
    ++*static_cast<uint32_t*>(context);
}

// The object a lookup gave back, into the hash (who made it), and its
// reference returned.
void MixObject(bool found, void* out)
{
    MixInto(g_classHash, found ? 1 : 0);
    if (!found || !out) {
        MixInto(g_classHash, 0);
        return;
    }
    auto* const object = static_cast<TestObject*>(static_cast<cIGZUnknown*>(out));
    MixInto(g_classHash, object->maker());
    object->Release();
}

// OPENSC4_TEST_CLASSES=<day>: two directors of the game's base class made
// by the test, with three classes (one registered twice, one with the
// second kind of factory), the second director a child of the first and
// the first added to the exe's root director (which takes the child too).
// Then lookups through the COM object and through the director (found,
// wrong interface, unknown class), the classes enumerated, the director's
// interfaces, RemoveRef, CanUnloadNow and GetLibraryPath. Everything goes
// into the fingerprint's `cls` part. The two directors stay in the root's
// list for the rest of the run.
void DriveClasses(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_CLASSES") || !game::IsBuild641())
        return;
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    char* const root = reinterpret_cast<char*(__cdecl*)()>(addr::RZGetCOMDllDirector)();
    if (!com || !root)
        return;
    g_classesOn = true;
    uint32_t& h = g_classHash;
    const auto newDirector = [] {
        void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x4C);
        return static_cast<char*>(
            reinterpret_cast<void*(__thiscall*)(void*)>(addr::cRZCOMDllDirector_Construct)(memory));
    };
    char* const first = newDirector();
    char* const second = newDirector();
    using AddClsFn = void*(__thiscall*)(char* director, uint32_t classId, void* factory);
    const auto addCls = reinterpret_cast<AddClsFn>(addr::cRZCOMDllDirector_AddCls);
    const auto addCls2 = reinterpret_cast<AddClsFn>(addr::cRZCOMDllDirector_AddCls_FactoryType2);
    using AddDirectorFn = void(__thiscall*)(char* director, char* child);
    const auto addDirector = reinterpret_cast<AddDirectorFn>(addr::cRZCOMDllDirector_AddDirector);

    addCls(first, kClassA, reinterpret_cast<void*>(&MakeFirst));
    addCls(first, kClassA, reinterpret_cast<void*>(&MakeSecond));
    addCls2(first, kClassB, reinterpret_cast<void*>(&MakeWithInterface));
    addCls(second, kClassC, reinterpret_cast<void*>(&MakeThird));

    const auto children = [](char* director) {
        return static_cast<uint32_t>(*reinterpret_cast<char***>(director + 0x30) -
                                     *reinterpret_cast<char***>(director + 0x2C));
    };
    const uint32_t before = children(root);
    addDirector(first, second);
    MixInto(h, *reinterpret_cast<void**>(second + 0x24) == nullptr ? 1 : 0);
    addDirector(root, first);
    MixInto(h, children(root) - before);
    MixInto(h, children(first));
    MixInto(h, *reinterpret_cast<void**>(first + 0x24) == com ? 1 : 0);
    MixInto(h, *reinterpret_cast<void**>(second + 0x24) == com ? 1 : 0);
    MixInto(h, *reinterpret_cast<void**>(first + 0x28) == *reinterpret_cast<void**>(root + 0x28) ? 1 : 0);

    const auto lookUp = [](void* from, std::size_t slot, uint32_t classId, uint32_t interfaceId) {
        void* out = nullptr;
        const bool found = Virtual<bool>(from, slot, classId, interfaceId, &out);
        MixObject(found, out);
    };
    // Through the COM object: found (the first registration of A won), the
    // wrong interface (the object ends), the second kind of factory, the
    // child's class, an unknown class.
    lookUp(com, 0xC, kClassA, kTestInterface);
    lookUp(com, 0xC, kClassA, uint32_t{0x7E570CFE});
    lookUp(com, 0xC, kClassB, kTestInterface);
    lookUp(com, 0xC, kClassB, uint32_t{0x7E570CFE});
    lookUp(com, 0xC, kClassC, kTestInterface);
    lookUp(com, 0xC, uint32_t{0x7E570CFF}, kTestInterface);
    // Through the director itself.
    lookUp(first, 0x18, kClassC, kTestInterface);
    lookUp(second, 0x18, kClassA, kTestInterface);
    MixInto(h, g_objectsMade);
    MixInto(h, g_objectsEnded);

    uint32_t seen = 0;
    Virtual<void>(first, 0x14, reinterpret_cast<void*>(&MixClass), static_cast<void*>(&seen));
    MixInto(h, seen);

    // The interfaces (each answer takes a reference), RemoveRef down to 0
    // and once more, CanUnloadNow with and without a reference.
    void* same = nullptr;
    MixInto(h, Virtual<bool>(first, 0x0, uint32_t{1}, &same) ? 1 : 0);
    MixInto(h, same == first ? 1 : 0);
    MixInto(h, Virtual<bool>(first, 0x0, uint32_t{0xA21EE941}, &same) ? 1 : 0);
    MixInto(h, Virtual<bool>(first, 0x0, uint32_t{0x03FA40BF}, &same) ? 1 : 0);
    MixInto(h, same == first + 4 ? 1 : 0);
    MixInto(h, Virtual<bool>(first, 0x0, uint32_t{0x7E570CFE}, &same) ? 1 : 0);
    MixInto(h, Virtual<bool>(first, 0x1C) ? 1 : 0);
    for (int i = 0; i < 4; ++i)
        MixInto(h, Virtual<uint32_t>(first, 0x28));
    MixInto(h, Virtual<bool>(first, 0x1C) ? 1 : 0);

    cRZBaseString path;
    MixInto(h, Virtual<bool>(first, 0x38, static_cast<cIGZString*>(&path)) ? 1 : 0);
    cRZBaseString rootPath;
    Virtual<bool>(root, 0x38, static_cast<cIGZString*>(&rootPath));
    MixInto(h, path.Strlen());
    MixInto(h, path.IsEqual(rootPath, false) ? 1 : 0);
    log::Write("autotest: classes driven (hash %08X, %u objects made, %u ended)", h, g_objectsMade,
               g_objectsEnded);
}

void ClassesFingerprint(cISC4City*, std::string& line)
{
    if (!g_classesOn)
        return;
    char text[64];
    std::snprintf(text, sizeof text, " | cls %08X %u/%u", g_classHash, g_objectsMade, g_objectsEnded);
    line += text;
}

// diagnostics.trace.service_start: the order in which the framework starts
// and stops the system services, seen at the base class's SetServiceRunning
// (0x0091DD91, the slot of nearly every service). Each call's service ID
// and flag go into a hash; the fingerprint gets ` | start <hash> <calls>`,
// and each stop is logged with the hash so far (the stops come after the
// last fingerprint). On in both runs of a comparison: it is put in when
// core.dll is injected, since the services start before any later phase.
bool g_startTraceOn = false;
uint32_t g_startHash = 2166136261u;
uint32_t g_startCalls = 0;

uint8_t __fastcall TraceSetServiceRunning(char* service, void*, uint8_t running)
{
    const uint32_t id = *reinterpret_cast<uint32_t*>(service + 0xC);
    MixInto(g_startHash, id);
    MixInto(g_startHash, running);
    ++g_startCalls;
    if (!running)
        log::Write("service_start: stop %08X, hash %08X after %u calls", id, g_startHash, g_startCalls);
    *reinterpret_cast<uint8_t*>(service + 0x18) = running;
    return running;
}

bool InstallStartTrace(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* entry = addr_check::Find(addr::cRZBaseSystemService_SetServiceRunning);
    void* original = nullptr;
    if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(&TraceSetServiceRunning), &original))
        return false;
    g_startTraceOn = true;
    return true;
}

const bool g_startTraceRegistered = registry::Register({
    "diagnostics.trace.service_start",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cRZBaseSystemService_SetServiceRunning,
    "Records the order in which system services are started and stopped, for the fingerprint "
    "(docs/framework.md)",
    false,
    InstallStartTrace,
});

void StartFingerprint(cISC4City*, std::string& line)
{
    if (!g_startTraceOn)
        return;
    char text[48];
    std::snprintf(text, sizeof text, " | start %08X %u", g_startHash, g_startCalls);
    line += text;
}

// The strings test (OPENSC4_TEST_STRINGS).
bool g_stringsOn = false;
uint32_t g_stringHash = 2166136261u;

// A game string (0x14 bytes, the game's constructor and destructor) for
// the test to call through its vtable.
struct GameString {
    char object[0x14];

    GameString(const char* text, uint32_t length)
    {
        using Fn = void*(__thiscall*)(char*, const char*, uint32_t);
        reinterpret_cast<Fn>(addr::SC4String_FromBytes)(object, text, length);
    }
    explicit GameString(const char* text) : GameString(text, static_cast<uint32_t>(std::strlen(text))) {}
    ~GameString() { reinterpret_cast<void(__thiscall*)(char*)>(addr::SC4String_Destroy)(object); }
    GameString(const GameString&) = delete;
    GameString& operator=(const GameString&) = delete;

    void* Self() { return object; }
    const char* Begin() const { return *reinterpret_cast<const char* const*>(object + 0x4); }
    uint32_t Size() const
    {
        return static_cast<uint32_t>(*reinterpret_cast<const char* const*>(object + 0x8) - Begin());
    }
    // The text and its length into the hash.
    void Mix() const
    {
        MixInto(g_stringHash, Size());
        MixInto(g_stringHash, BytesChecksum(Begin(), Size()));
    }
};

// OPENSC4_TEST_STRINGS=<day>: game strings taken through the string
// class's methods by their vtable slots: every pair of a set of texts
// (empty, mixed case, accented letters, one longer than the lowering's
// own buffer) compared and searched with and without case, by string and
// by characters; then a string changed step by step (FromChar, Append,
// Insert, Replace, Erase, Resize, Copy from a string of another class,
// operator=), and the interfaces and reference count of one made on the
// heap. Every result and every text go into the fingerprint's `str` part.
void DriveStrings(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_STRINGS") || !game::IsBuild641())
        return;
    g_stringsOn = true;
    uint32_t& h = g_stringHash;

    std::string longText;
    for (int i = 0; i < 40; ++i)
        longText += "Stra\xDF" "e \xC9t\xE9 ";
    const std::string texts[] = {"",          "Hello World", "hello world", "HELLO",
                                 "h\xE9llo w\xF6rld", "H\xC9LLO W\xD6RLD", "Hello Worlds", longText};
    for (const std::string& left : texts) {
        GameString a(left.data(), static_cast<uint32_t>(left.size()));
        for (const std::string& right : texts) {
            GameString b(right.data(), static_cast<uint32_t>(right.size()));
            for (uint8_t withCase = 0; withCase < 2; ++withCase) {
                MixInto(h, Virtual<bool>(a.Self(), 0x28, b.Self(), withCase) ? 1 : 0);
                MixInto(h, Virtual<bool>(a.Self(), 0x24, b.Self(), withCase) ? 1 : 0);
                MixInto(h, Virtual<bool>(a.Self(), 0x20, right.c_str(), static_cast<uint32_t>(right.size()), withCase)
                               ? 1
                               : 0);
                MixInto(h, Virtual<bool>(a.Self(), 0x20, right.c_str(), 0xFFFFFFFFu, withCase) ? 1 : 0);
                MixInto(h, static_cast<uint32_t>(Virtual<int32_t>(a.Self(), 0x30, b.Self(), withCase)));
                MixInto(h, static_cast<uint32_t>(Virtual<int32_t>(a.Self(), 0x2C, right.c_str(),
                                                                  static_cast<uint32_t>(right.size()), withCase)));
                MixInto(h, static_cast<uint32_t>(
                               Virtual<int32_t>(a.Self(), 0x2C, right.c_str(), 0xFFFFFFFFu, withCase)));
                for (const uint32_t position : {0u, 3u}) {
                    if (position > a.Size())
                        continue;
                    MixInto(h, static_cast<uint32_t>(Virtual<int32_t>(a.Self(), 0x5C, b.Self(), position, withCase)));
                    MixInto(h, static_cast<uint32_t>(
                                   Virtual<int32_t>(a.Self(), 0x60, right.c_str(), position, withCase)));
                }
                for (const uint32_t position : {0xFFFFFFFFu, 4u}) {
                    MixInto(h, static_cast<uint32_t>(Virtual<int32_t>(a.Self(), 0x64, b.Self(), position, withCase)));
                    MixInto(h, static_cast<uint32_t>(
                                   Virtual<int32_t>(a.Self(), 0x68, right.c_str(), position, withCase)));
                }
            }
        }
        for (const char* needle : {"o", "O", "wor", "\xC9", "zz"})
            for (uint8_t withCase = 0; withCase < 2; ++withCase) {
                MixInto(h, static_cast<uint32_t>(Virtual<int32_t>(a.Self(), 0x60, needle, uint32_t{0}, withCase)));
                MixInto(h,
                        static_cast<uint32_t>(Virtual<int32_t>(a.Self(), 0x68, needle, 0xFFFFFFFFu, withCase)));
            }
    }

    // A string changed step by step.
    GameString s("abc");
    GameString other("XYZ12");
    cRZBaseString foreign("from another class");
    s.Mix();
    Virtual<void>(s.Self(), 0x10, "The quick brown fox");
    s.Mix();
    Virtual<void>(s.Self(), 0xC, "jumps over", uint32_t{5});
    s.Mix();
    MixInto(h, Virtual<void*>(s.Self(), 0x44, " and", uint32_t{4}) == s.Self() ? 1 : 0);
    s.Mix();
    Virtual<void*>(s.Self(), 0x40, other.Self());
    s.Mix();
    Virtual<void*>(s.Self(), 0x4C, uint32_t{0}, ">>", uint32_t{2});
    s.Mix();
    Virtual<void*>(s.Self(), 0x4C, s.Size(), "<<", uint32_t{2});
    s.Mix();
    Virtual<void*>(s.Self(), 0x48, uint32_t{4}, other.Self());
    s.Mix();
    Virtual<void*>(s.Self(), 0x54, uint32_t{2}, "0123456789", uint32_t{3});
    s.Mix();
    Virtual<void*>(s.Self(), 0x50, uint32_t{1}, other.Self());
    s.Mix();
    Virtual<void*>(s.Self(), 0x58, uint32_t{3}, uint32_t{4});
    s.Mix();
    Virtual<void*>(s.Self(), 0x58, uint32_t{0}, uint32_t{1});
    s.Mix();
    Virtual<void*>(s.Self(), 0x3C, uint32_t{4});
    s.Mix();
    Virtual<void*>(s.Self(), 0x3C, uint32_t{9});
    s.Mix();
    Virtual<void>(s.Self(), 0x38, static_cast<cIGZString*>(&foreign));
    s.Mix();
    MixInto(h, Virtual<void*>(s.Self(), 0x34, other.Self()) == s.Self() ? 1 : 0);
    s.Mix();
    Virtual<void>(s.Self(), 0x10, static_cast<const char*>(nullptr));
    s.Mix();
    Virtual<void>(other.Self(), 0xC, static_cast<const char*>(nullptr), uint32_t{3});
    other.Mix();
    MixInto(h, Virtual<uint32_t>(s.Self(), 0x1C));

    // One on the heap: its interfaces and its count down to the end.
    void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x14);
    void* const heap =
        reinterpret_cast<void*(__thiscall*)(void*, const char*, uint32_t)>(addr::SC4String_FromBytes)(memory, "heap", 4);
    MixInto(h, Virtual<uint32_t>(heap, 0x4));
    void* same = nullptr;
    MixInto(h, Virtual<bool>(heap, 0x0, uint32_t{0x089B7DC8}, &same) ? 1 : 0);
    MixInto(h, same == heap ? 1 : 0);
    MixInto(h, Virtual<bool>(heap, 0x0, uint32_t{1}, &same) ? 1 : 0);
    MixInto(h, Virtual<bool>(heap, 0x0, uint32_t{0x7E570DFE}, &same) ? 1 : 0);
    MixInto(h, Virtual<uint32_t>(heap, 0x8));
    MixInto(h, Virtual<uint32_t>(heap, 0x8));
    MixInto(h, Virtual<uint32_t>(heap, 0x8));
    log::Write("autotest: strings driven (hash %08X)", h);
}

void StringsFingerprint(cISC4City*, std::string& line)
{
    if (!g_stringsOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | str %08X", g_stringHash);
    line += text;
}

// The message object test (OPENSC4_TEST_MESSAGE_OBJECT).
bool g_messageObjectOn = false;
uint32_t g_messageObjectHash = 2166136261u;

// OPENSC4_TEST_MESSAGE_OBJECT=<day>: standard messages made with the
// game's constructor and taken through their vtable slots: the "has"
// answers of a fresh message, the type set and cleared, the four data
// values (a zero too), a string, an unknown and an extra object set, read
// back through GetString and GetExtra (with and without the interface) and
// cleared, the two comparisons by type, Create (found and not), the
// interfaces, and the count down to the message's end, after which the
// objects it held have their references back. Everything goes into the
// fingerprint's `mobj` part.
void DriveMessageObject(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MESSAGE_OBJECT") || !game::IsBuild641())
        return;
    g_messageObjectOn = true;
    uint32_t& h = g_messageObjectHash;
    const auto make = [] {
        void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::RZ_operator_new)(0x2C);
        void* const message = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory);
        Virtual<uint32_t>(message, 0x4);
        return message;
    };
    // Every "has" answer, the type and the four values.
    const auto state = [](void* message) {
        uint32_t& h = g_messageObjectHash;
        for (const std::size_t slot : {0x20u, 0x34u, 0x48u, 0x5Cu, 0x70u, 0x7Cu, 0x88u})
            MixInto(h, Virtual<uint32_t>(message, slot) & 0xFF);
        MixInto(h, Virtual<uint32_t>(message, 0x10));
        for (const std::size_t slot : {0x24u, 0x38u, 0x4Cu, 0x60u})
            MixInto(h, Virtual<uint32_t>(message, slot));
    };
    void* const m = make();
    void* const n = make();
    state(m);
    Virtual<uint32_t>(m, 0x14, uint32_t{0x7E570E01});
    state(m);
    Virtual<uint32_t>(m, 0x14, uint32_t{0});
    state(m);
    Virtual<uint32_t>(m, 0x14, uint32_t{0x7E570E02});
    Virtual<uint32_t>(m, 0x2C, uint32_t{11});
    state(m);
    Virtual<uint32_t>(m, 0x40, uint32_t{0});
    state(m);
    Virtual<uint32_t>(m, 0x54, uint32_t{33});
    Virtual<uint32_t>(m, 0x68, uint32_t{44});
    state(m);
    Virtual<uint32_t>(m, 0x30, uint32_t{55});
    Virtual<uint32_t>(m, 0x6C, uint32_t{66});
    state(m);

    // The three objects: test targets serve as counted objects, a game
    // string as the string.
    GameString text("a message's text");
    Virtual<uint32_t>(text.Self(), 0x4);
    Target& unknown = g_targets[0];
    Target& extra = g_targets[1];
    const uint32_t unknownBefore = unknown.refs();
    const uint32_t extraBefore = extra.refs();
    Virtual<void*>(m, 0x78, text.Self());
    Virtual<void*>(m, 0x84, static_cast<void*>(&unknown));
    Virtual<void*>(m, 0x90, static_cast<void*>(&extra));
    state(m);
    MixInto(h, *reinterpret_cast<uint32_t*>(text.object + 0x10));
    MixInto(h, unknown.refs() - unknownBefore);
    MixInto(h, extra.refs() - extraBefore);
    MixInto(h, Virtual<void*>(m, 0x80) == static_cast<void*>(&unknown) ? 1 : 0);
    void* out = nullptr;
    MixInto(h, Virtual<bool>(m, 0x74, uint32_t{0x089B7DC8}, &out) ? 1 : 0);
    MixInto(h, out == text.Self() ? 1 : 0);
    if (out)
        Virtual<uint32_t>(out, 0x8);
    out = nullptr;
    MixInto(h, Virtual<bool>(m, 0x74, uint32_t{0x7E570EFE}, &out) ? 1 : 0);
    out = nullptr;
    MixInto(h, Virtual<bool>(m, 0x8C, uint32_t{0x452294AA}, &out) ? 1 : 0);
    MixInto(h, out == static_cast<void*>(&extra) ? 1 : 0);
    if (out)
        extra.Release();
    out = nullptr;
    MixInto(h, Virtual<bool>(m, 0x8C, uint32_t{0x7E570EFE}, &out) ? 1 : 0);
    MixInto(h, Virtual<bool>(n, 0x74, uint32_t{0x089B7DC8}, &out) ? 1 : 0);
    MixInto(h, Virtual<bool>(n, 0x8C, uint32_t{0x452294AA}, &out) ? 1 : 0);
    // The same object again, another one, then none.
    Virtual<void*>(m, 0x84, static_cast<void*>(&unknown));
    MixInto(h, unknown.refs() - unknownBefore);
    Virtual<void*>(m, 0x84, static_cast<void*>(&extra));
    MixInto(h, unknown.refs() - unknownBefore);
    MixInto(h, extra.refs() - extraBefore);
    Virtual<void*>(m, 0x84, static_cast<void*>(nullptr));
    Virtual<void*>(m, 0x78, static_cast<void*>(nullptr));
    state(m);
    MixInto(h, *reinterpret_cast<uint32_t*>(text.object + 0x10));
    MixInto(h, extra.refs() - extraBefore);
    Virtual<void*>(m, 0x78, text.Self());

    // By type: equal and lower, both ways.
    Virtual<uint32_t>(n, 0x14, uint32_t{0x7E570E02});
    MixInto(h, Virtual<uint32_t>(m, 0x18, n) & 0xFF);
    MixInto(h, Virtual<uint32_t>(m, 0x1C, n));
    Virtual<uint32_t>(n, 0x14, uint32_t{0x7E570E03});
    MixInto(h, Virtual<uint32_t>(m, 0x18, n) & 0xFF);
    MixInto(h, Virtual<uint32_t>(m, 0x1C, n));
    MixInto(h, Virtual<uint32_t>(n, 0x1C, m));

    // Create: a new message with this one's type and nothing else.
    void* created = nullptr;
    MixInto(h, Virtual<bool>(m, 0xC, uint32_t{0x65297976}, &created) ? 1 : 0);
    if (created) {
        state(created);
        MixInto(h, Virtual<uint32_t>(created, 0x8));
    }
    created = nullptr;
    MixInto(h, Virtual<bool>(m, 0xC, uint32_t{0x7E570EFE}, &created) ? 1 : 0);

    // The interfaces, then the end: the string and the extra object get
    // their references back.
    void* same = nullptr;
    MixInto(h, Virtual<bool>(m, 0x0, uint32_t{0xA52294B4}, &same) ? 1 : 0);
    MixInto(h, same == m ? 1 : 0);
    MixInto(h, Virtual<bool>(m, 0x0, uint32_t{1}, &same) ? 1 : 0);
    MixInto(h, Virtual<bool>(m, 0x0, uint32_t{0x7E570EFE}, &same) ? 1 : 0);
    MixInto(h, Virtual<uint32_t>(m, 0x8));
    MixInto(h, Virtual<uint32_t>(m, 0x8));
    MixInto(h, Virtual<uint32_t>(m, 0x8));
    MixInto(h, Virtual<uint32_t>(n, 0x8));
    MixInto(h, *reinterpret_cast<uint32_t*>(text.object + 0x10));
    MixInto(h, extra.refs() - extraBefore);
    MixInto(h, unknown.refs() - unknownBefore);
    log::Write("autotest: message object driven (hash %08X)", h);
}

void MessageObjectFingerprint(cISC4City*, std::string& line)
{
    if (!g_messageObjectOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | mobj %08X", g_messageObjectHash);
    line += text;
}

// The message pool test (OPENSC4_TEST_MESSAGE_POOL).
bool g_messagePoolOn = false;
uint32_t g_messagePoolHash = 2166136261u;

// OPENSC4_TEST_MESSAGE_POOL=<day>: blocks of every size class from the
// message pool's allocate (their headers read, their bytes written and read
// back) and given to its free, a null pointer to the free; then messages
// built by the game's constructor on memory filled with a pattern, every
// byte after the vtable read; and the destructor on a message with a
// subclass's vtable that holds a string and two counted objects, and on an
// empty one. Everything goes into the fingerprint's `mpool` part.
void DriveMessagePool(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MESSAGE_POOL") || !game::IsBuild641())
        return;
    g_messagePoolOn = true;
    uint32_t& h = g_messagePoolHash;
    const auto allocate = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::RZ_operator_new);
    const auto release = reinterpret_cast<void(__cdecl*)(void*)>(addr::RZ_operator_delete);
    const auto construct = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage);
    const auto destruct = reinterpret_cast<void(__thiscall*)(void*)>(addr::cGZMessage_Destruct);

    // Each size class at its upper edge and one past it, then sizes only
    // the C runtime holds.
    static const uint32_t kSizes[] = {0,    1,    4,    5,    12,   13,   28,   29,   0x2C, 60,   61,  124,
                                      125,  252,  253,  508,  509,  1020, 1021, 2044, 2045, 4096, 70000};
    void* blocks[sizeof kSizes / sizeof kSizes[0]] = {};
    unsigned count = 0;
    for (const uint32_t size : kSizes) {
        auto* const block = static_cast<uint8_t*>(allocate(size));
        MixInto(h, block ? 1 : 0);
        if (!block)
            continue;
        MixInto(h, *reinterpret_cast<uint32_t*>(block - 4));
        for (uint32_t i = 0; i < size; ++i)
            block[i] = static_cast<uint8_t>(size + i);
        blocks[count++] = block;
    }
    // Every block still has its own bytes and header.
    count = 0;
    for (const uint32_t size : kSizes) {
        auto* const block = static_cast<uint8_t*>(blocks[count++]);
        if (!block)
            continue;
        uint32_t sum = 0;
        for (uint32_t i = 0; i < size; ++i)
            sum = sum * 31 + block[i];
        MixInto(h, sum);
        MixInto(h, *reinterpret_cast<uint32_t*>(block - 4));
    }
    for (unsigned i = 0; i < count; i += 2)
        release(blocks[i]);
    for (unsigned i = 1; i < count; i += 2)
        release(blocks[i]);
    release(nullptr);

    // The constructor on used memory.
    auto* const memory = static_cast<uint8_t*>(allocate(0x2C));
    for (unsigned i = 0; i < 0x2C; ++i)
        memory[i] = 0xC5;
    void* const message = construct(memory);
    MixInto(h, message == memory ? 1 : 0);
    MixInto(h, *reinterpret_cast<uint32_t*>(memory) == addr::cGZMessage_Vtable ? 1 : 0);
    for (unsigned i = 4; i < 0x2C; ++i)
        MixInto(h, memory[i]);

    // The destructor: the message holds a string and two counted objects
    // and has another vtable, as a subclass's would at this point.
    GameString text("a pooled message's text");
    Virtual<uint32_t>(text.Self(), 0x4);
    Target& unknown = g_targets[0];
    Target& extra = g_targets[1];
    const uint32_t unknownBefore = unknown.refs();
    const uint32_t extraBefore = extra.refs();
    Virtual<void*>(message, 0x78, text.Self());
    Virtual<void*>(message, 0x84, static_cast<void*>(&unknown));
    Virtual<void*>(message, 0x90, static_cast<void*>(&extra));
    MixInto(h, *reinterpret_cast<uint32_t*>(text.object + 0x10));
    MixInto(h, unknown.refs() - unknownBefore);
    MixInto(h, extra.refs() - extraBefore);
    *reinterpret_cast<uint32_t*>(memory) = 0;
    destruct(message);
    MixInto(h, *reinterpret_cast<uint32_t*>(memory) == addr::cGZMessage_Vtable ? 1 : 0);
    MixInto(h, *reinterpret_cast<uint32_t*>(text.object + 0x10));
    MixInto(h, unknown.refs() - unknownBefore);
    MixInto(h, extra.refs() - extraBefore);
    for (unsigned i = 4; i < 0x20; ++i)
        MixInto(h, memory[i]);
    release(memory);

    // An empty message's end, through the deleting destructor.
    void* const empty = construct(allocate(0x2C));
    Virtual<void*>(empty, 0x94, uint32_t{1});
    log::Write("autotest: message pool driven (hash %08X)", h);
}

void MessagePoolFingerprint(cISC4City*, std::string& line)
{
    if (!g_messagePoolOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | mpool %08X", g_messagePoolHash);
    line += text;
}

// The lock test (OPENSC4_TEST_LOCKS).
bool g_locksOn = false;
uint32_t g_locksHash = 2166136261u;

// OPENSC4_TEST_LOCKS=<day>: a lock object made by the game's constructor
// and taken through its vtable slots: locked three times and unlocked, with
// the wrapper's count and Windows' own count of entries (the
// CRITICAL_SECTION's RecursionCount at +8) read after each step; TryLock on
// a free lock and on one this thread holds; the handle; the destructor;
// then one on the heap ended through Release, and the lookup of
// TryEnterCriticalSection run again. Everything goes into the fingerprint's
// `lock` part.
void DriveLocks(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_LOCKS") || !game::IsBuild641())
        return;
    g_locksOn = true;
    const auto construct = reinterpret_cast<void*(__thiscall*)(void*)>(addr::RZCriticalSection_Construct);
    const auto destroy = reinterpret_cast<void(__thiscall*)(void*)>(addr::RZCriticalSection_Destroy);
    using SectionFn = void(__stdcall*)(void*);

    char object[8] = {};
    MixInto(g_locksHash, construct(object) == object ? 1 : 0);
    MixInto(g_locksHash, *reinterpret_cast<uint32_t*>(object) == addr::RZCriticalSection_Vtable ? 1 : 0);
    char* const section = *reinterpret_cast<char**>(object + 4);
    // The wrapper's count through slot 5, and Windows' count of entries.
    const auto state = [&object, section] {
        MixInto(g_locksHash, Virtual<uint32_t>(object, 0x14));
        MixInto(g_locksHash, *reinterpret_cast<uint32_t*>(section + 0x18));
        MixInto(g_locksHash, *reinterpret_cast<uint32_t*>(section + 0x8));
    };
    state();
    for (int i = 0; i < 3; ++i) {
        MixInto(g_locksHash, Virtual<uint32_t>(object, 0x4));
        state();
    }
    for (int i = 0; i < 3; ++i) {
        MixInto(g_locksHash, Virtual<uint32_t>(object, 0x8));
        state();
    }
    void* handle = nullptr;
    const uint32_t gotHandle = Virtual<uint32_t>(object, 0x1C, &handle);
    MixInto(g_locksHash, gotHandle);
    MixInto(g_locksHash, handle == section ? 1 : 0);
    MixInto(g_locksHash, Virtual<uint32_t>(object, 0x10) & 0xFF);

    // TryLock on a free lock, again while held, then both given back. A
    // successful try enters the section twice, so after the unlocks the
    // thread still holds it; the extra entries are left here by hand.
    MixInto(g_locksHash, Virtual<uint32_t>(object, 0xC));
    state();
    const uint32_t entriesAfterTry = *reinterpret_cast<uint32_t*>(section + 0x8);
    MixInto(g_locksHash, Virtual<uint32_t>(object, 0xC));
    state();
    MixInto(g_locksHash, Virtual<uint32_t>(object, 0x8));
    MixInto(g_locksHash, Virtual<uint32_t>(object, 0x8));
    state();
    for (uint32_t left = *reinterpret_cast<uint32_t*>(section + 0x8); left > 0; --left)
        Global<SectionFn>(addr::Kernel32_LeaveCriticalSection)(section);
    state();
    *reinterpret_cast<uint32_t*>(object) = 0;
    destroy(object);
    MixInto(g_locksHash, *reinterpret_cast<uint32_t*>(object) == addr::RZCriticalSection_Vtable ? 1 : 0);

    // One on the heap, locked and unlocked, ended through Release.
    void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(8);
    void* const heaped = construct(memory);
    MixInto(g_locksHash, Virtual<uint32_t>(heaped, 0x4));
    MixInto(g_locksHash, Virtual<uint32_t>(heaped, 0x8));
    MixInto(g_locksHash, Virtual<uint32_t>(heaped, 0x0));

    // The lookup again: the function is found on this Windows.
    char dummy[4] = {};
    const auto lookup = reinterpret_cast<void*(__thiscall*)(void*)>(addr::RZCriticalSection_FindTryEnter);
    MixInto(g_locksHash, lookup(dummy) == dummy ? 1 : 0);
    MixInto(g_locksHash, Global<void*>(addr::RZCriticalSection_TryEnterFunction) ? 1 : 0);
    MixInto(g_locksHash, Global<void*>(addr::RZCriticalSection_Kernel32Module) ? 1 : 0);
    log::Write("autotest: locks driven (hash %08X; Windows counted %u entries after one TryLock)", g_locksHash,
               entriesAfterTry);
}

void LocksFingerprint(cISC4City*, std::string& line)
{
    if (!g_locksOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | lock %08X", g_locksHash);
    line += text;
}

// The string format test (OPENSC4_TEST_STRING_FORMAT).
bool g_formatOn = false;
uint32_t g_formatHash = 2166136261u;
uint32_t g_formatCalls = 0;
uint32_t g_formatWritten = 0;

constexpr unsigned kFormatVariants = 8;
const uint32_t kFormatInts[kFormatVariants] = {0, 1, 0xFFFFFFFF, 42, 0xFFFFFFD6, 0x7FFFFFFF, 0x80000000, 0x1234ABCD};
const uint64_t kFormatLongs[kFormatVariants] = {0,
                                                1,
                                                0xFFFFFFFFFFFFFFFFull,
                                                0x100000000ull,
                                                0xFFFFFFFF00000000ull,
                                                0x7FFFFFFFFFFFFFFFull,
                                                0x8000000000000000ull,
                                                0x0123456789ABCDEFull};
const uint32_t kFormatShorts[kFormatVariants] = {0, 1, 0xFFFF, 0x7FFF, 0x8000, 0x1FFFB, 0xABCD8000, 300};
const double kFormatDoubles[kFormatVariants] = {0.0, -0.0, 1.0, -1.0, 3.14159265358979, -2.5e-7, 123456789.125, 1e100};
const char* const kFormatTexts[kFormatVariants] = {
    "",
    "abc",
    "h\xC3\xA9llo w\xC3\xB6rld",
    "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E text",
    // Cut short: the formatter takes the first byte's word for the length.
    "ab\xE0\x80\0Z",
    "a longer text that is wider than any width the test asks for, to be cut by a precision or not at all",
    "\xC3",
    "0123456789",
};
const uint32_t kFormatChars[kFormatVariants] = {'A', 'z', 0xE9, ' ', '%', '0', 0x141, 0x7F};
const int kFormatWidths[kFormatVariants] = {7, 0, -7, 1, 12, 3, 40, 2};
const int kFormatPrecisions[kFormatVariants] = {4, 0, -1, 1, 9, 2, 20, 3};

// One call of the game's formatter. The arguments are laid out as the
// format asks (read here the way the formatter reads it), in a block of
// slots that is always passed whole.
void FormatOne(void* string, const char* format, unsigned variant)
{
    uint32_t slots[48] = {};
    unsigned used = 0;
    const auto put = [&slots, &used](uint32_t value) {
        if (used < 48)
            slots[used++] = value;
    };
    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    for (const char* p = format; *p;) {
        if (*p++ != '%')
            continue;
        char b = *p++;
        while (b == '-' || b == '+' || b == ' ' || b == '0' || b == '#')
            b = *p++;
        if (b == '%')
            continue;
        if (b == '*') {
            put(static_cast<uint32_t>(kFormatWidths[variant]));
            b = *p++;
        } else {
            while (isDigit(b))
                b = *p++;
        }
        if (b == '.') {
            b = *p++;
            if (b == '*') {
                put(static_cast<uint32_t>(kFormatPrecisions[variant]));
                b = *p++;
            } else {
                while (isDigit(b))
                    b = *p++;
            }
        }
        int size = 1;
        if (b == 'I') {
            if (p[0] == '6' && p[1] == '4') {
                p += 2;
                b = *p++;
                size = 3;
            }
        } else if (b == 'h') {
            b = *p++;
            size = 0;
        } else if (b == 'l') {
            b = *p++;
            size = 2;
            if (b == 'l') {
                b = *p++;
                size = 3;
            }
        }
        switch (b) {
        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x':
        case 'X':
            if (size == 3) {
                put(static_cast<uint32_t>(kFormatLongs[variant]));
                put(static_cast<uint32_t>(kFormatLongs[variant] >> 32));
            } else {
                put(size == 0 ? kFormatShorts[variant] : kFormatInts[variant]);
            }
            break;
        case 'e':
        case 'f':
        case 'g':
        case 'E':
        case 'G': {
            uint32_t halves[2];
            std::memcpy(halves, &kFormatDoubles[variant], 8);
            put(halves[0]);
            put(halves[1]);
            break;
        }
        case 's':
            put(reinterpret_cast<uint32_t>(kFormatTexts[variant]));
            break;
        case 'c':
            put(kFormatChars[variant]);
            break;
        case 'p':
            put(kFormatInts[variant]);
            break;
        case 'n':
            put(reinterpret_cast<uint32_t>(&g_formatWritten));
            break;
        default:
            // Not a conversion: the formatter reads the character again.
            --p;
            break;
        }
    }
    using Fn = void*(__cdecl*)(void*, const char*, ...);
    const uint32_t* const a = slots;
    const void* const result = reinterpret_cast<Fn>(addr::cRZBaseString_Sprintf)(
        string, format, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14],
        a[15], a[16], a[17], a[18], a[19], a[20], a[21], a[22], a[23], a[24], a[25], a[26], a[27], a[28], a[29], a[30],
        a[31], a[32], a[33], a[34], a[35], a[36], a[37], a[38], a[39], a[40], a[41], a[42], a[43], a[44], a[45], a[46],
        a[47]);
    uint32_t& h = g_formatHash;
    MixInto(h, result == string ? 1 : 0);
    const char* const begin = *reinterpret_cast<const char* const*>(static_cast<char*>(string) + 0x4);
    const char* const end = *reinterpret_cast<const char* const*>(static_cast<char*>(string) + 0x8);
    MixInto(h, static_cast<uint32_t>(end - begin));
    uint32_t sum = 0;
    for (const char* c = begin; c != end; ++c)
        sum = sum * 31 + static_cast<uint8_t>(*c);
    MixInto(h, sum);
    ++g_formatCalls;
}

// OPENSC4_TEST_STRING_FORMAT=<day>: the string class's formatter with
// every specifier shape the game's own format strings use, then every
// combination of flags, width, precision and size with each conversion,
// over values that reach the signs, the limits, UTF-8 texts cut by a
// precision and results longer than the formatter's buffers; %n, %%, and
// things that are not conversions. Every result goes into the
// fingerprint's `fmt` part.
void DriveStringFormat(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_STRING_FORMAT") || !game::IsBuild641())
        return;
    g_formatOn = true;
    GameString out("previous text");
    const auto all = [&out](const char* format) {
        for (unsigned variant = 0; variant < kFormatVariants; ++variant)
            FormatOne(out.Self(), format, variant);
    };

    // The shapes the game's formats use.
    static const char* const kGameShapes[] = {
        "%d",   "%s",    "%08x",  "%u",    "%%",    "%02x",    "%04x", "%02X", "%3.2f", "%08X",
        "%x",   "%.0f",  "%.1f",  "%f",    "%08lx", "%.2u",    "%X",   "%.2f", "%02d",  "%05u",
        "%8x",  "%3d",   "%0X",   "%3.0f", "%2.0f", "%03d",    "%I64d", "%02.2d", "%hs", "%lld",
        "%c",   "%p",    "%02.2f", "%4x",  "%ld",   "%10u",    "%-18.18s", "%I64u", "%g",
        "%s=(%u,%u,%u)(%u,%u,%u) ", "%d,%d,%08x,%s,%s,%08x", "%s %.0f%% (%.0f%%), ", "%c%c%s[ %d, %d, %d ]= ",
    };
    for (const char* const shape : kGameShapes)
        all(shape);

    static const char* const kFlags[] = {"", "-", "+", " ", "0", "#", "-+", "+0", " 0", "-0", "#0", "+ ", "-#", "+#0"};
    static const char* const kWidths[] = {"", "1", "6", "14", "*"};
    static const char* const kPrecisions[] = {"", ".", ".0", ".3", ".11", ".*"};
    static const char* const kSizes[] = {"", "h", "l", "ll", "I64"};
    char format[64];
    for (const char* const flags : kFlags)
        for (const char* const width : kWidths)
            for (const char* const precision : kPrecisions) {
                for (const char* const size : kSizes)
                    for (const char conversion : {'d', 'i', 'u', 'o', 'x', 'X'}) {
                        std::snprintf(format, sizeof format, "[%%%s%s%s%s%c]", flags, width, precision, size,
                                      conversion);
                        all(format);
                    }
                for (const char conversion : {'e', 'f', 'g', 'E', 'G', 's', 'c', 'p'}) {
                    std::snprintf(format, sizeof format, "[%%%s%s%s%c]", flags, width, precision, conversion);
                    all(format);
                }
                // Not conversions.
                for (const char conversion : {'z', 'C', 'S', 'I', '%'}) {
                    std::snprintf(format, sizeof format, "[%%%s%s%s%c|", flags, width, precision, conversion);
                    FormatOne(out.Self(), format, 0);
                }
            }

    // Results past the scratch buffer (64 bytes, then doubling) and the
    // output buffer (1,024 bytes).
    for (const char* const big :
         {"%.70f", "%.200f", "%.300e", "%#.120g", "%2000d", "%-1500s|", "%01100x", "%.1030d", "%s%s%s%s%s%s%s%s%s%s%s%s",
          "%f %f %.90f %f", "%1023d", "%1024d", "%1025d", "%*.*f"})
        all(big);
    std::string longFormat(3000, 'x');
    longFormat += "%d";
    longFormat.append(3000, 'y');
    all(longFormat.c_str());

    // %n: the length the string had before the call.
    for (const char* const counted : {"ab%ncd", "%n", "%d%n%d"}) {
        g_formatWritten = 0xFFFFFFFF;
        FormatOne(out.Self(), counted, 3);
        MixInto(g_formatHash, g_formatWritten);
    }
    // Ends and oddities.
    for (const char* const odd : {"", "%", "abc%", "%5", "%-", "%.", "%5.", "%l", "%ll", "%h", "%I6d", "%I32d", "%I", "100%%",
                                  "%%%%", "%-%", "% d% d", "%+d%+d", "%hd|%hu|%hx", "%.s|", "%.d|", "%.f|"})
        all(odd);
    log::Write("autotest: string format driven (%u calls, hash %08X)", g_formatCalls, g_formatHash);
}

void StringFormatFingerprint(cISC4City*, std::string& line)
{
    if (!g_formatOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | fmt %08X", g_formatHash);
    line += text;
}

// The string helpers test (OPENSC4_TEST_STRING_HELPERS).
bool g_helpersOn = false;
uint32_t g_helpersHash = 2166136261u;

// A string's length and bytes into the helpers' hash.
void MixString(const char* object)
{
    const char* const begin = *reinterpret_cast<const char* const*>(object + 0x4);
    const char* const end = *reinterpret_cast<const char* const*>(object + 0x8);
    MixInto(g_helpersHash, static_cast<uint32_t>(end - begin));
    uint32_t sum = 0;
    for (const char* c = begin; c != end; ++c)
        sum = sum * 31 + static_cast<uint8_t>(*c);
    MixInto(g_helpersHash, sum);
}

// A string a helper built in raw memory: hashed, then ended.
void MixBuilt(char* object)
{
    MixString(object);
    reinterpret_cast<void(__thiscall*)(char*)>(addr::SC4String_Destroy)(object);
}

// OPENSC4_TEST_STRING_HELPERS=<day>: the string class's helpers called at
// their addresses on game strings, over texts with padding, separators,
// runs of separators, mixed case, accented letters, the byte 0x7F and one
// longer than the case changes' buffer: BeginsWith and EndsWith against
// several texts, AppendText, AssignLimited, Mid, Left and Right over
// positions and counts around the ends, the trims, CutThroughChar, SplitOff
// with several separators, SplitAtChar, SplitToken until it finds no more,
// MakeLower and MakeUpper, and the assigning and wide character helpers.
// Every result goes into the fingerprint's `shlp` part.
void DriveStringHelpers(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_STRING_HELPERS") || !game::IsBuild641())
        return;
    g_helpersOn = true;
    uint32_t& h = g_helpersHash;
    using PartFn = uint8_t(__thiscall*)(char*, const char*, uint32_t);
    using TextFn = char*(__thiscall*)(char*, const char*);
    using LimitedFn = char*(__thiscall*)(char*, const char*, uint32_t);
    using MidFn = char*(__thiscall*)(char*, char*, uint32_t, uint32_t);
    using SideFn = char*(__thiscall*)(char*, char*, uint32_t);
    using PlainFn = void(__thiscall*)(char*);
    using CutFn = uint8_t(__thiscall*)(char*, uint32_t);
    using SplitOffFn = char*(__thiscall*)(char*, char*, const char*);
    using SplitCharFn = uint8_t(__thiscall*)(char*, uint32_t, char*);
    const auto beginsWith = reinterpret_cast<PartFn>(addr::cRZBaseString_BeginsWith);
    const auto endsWith = reinterpret_cast<PartFn>(addr::cRZBaseString_EndsWith);
    const auto appendText = reinterpret_cast<TextFn>(addr::cRZBaseString_AppendText);
    const auto assignLimited = reinterpret_cast<LimitedFn>(addr::cRZBaseString_AssignLimited);
    const auto mid = reinterpret_cast<MidFn>(addr::cRZBaseString_Mid);
    const auto left = reinterpret_cast<SideFn>(addr::cRZBaseString_Left);
    const auto right = reinterpret_cast<SideFn>(addr::cRZBaseString_Right);
    const auto lTrim = reinterpret_cast<PlainFn>(addr::cRZBaseString_LTrim);
    const auto rTrim = reinterpret_cast<PlainFn>(addr::cRZBaseString_RTrim);
    const auto trim = reinterpret_cast<PlainFn>(addr::cRZBaseString_Trim);
    const auto cutThrough = reinterpret_cast<CutFn>(addr::cRZBaseString_CutThroughChar);
    const auto splitOff = reinterpret_cast<SplitOffFn>(addr::SC4String_SplitOff);
    const auto splitAtChar = reinterpret_cast<SplitCharFn>(addr::SC4String_SplitAtChar);
    const auto splitToken = reinterpret_cast<SplitCharFn>(addr::SC4String_SplitToken);
    const auto makeLower = reinterpret_cast<PlainFn>(addr::cRZBaseString_MakeLower);
    const auto makeUpper = reinterpret_cast<PlainFn>(addr::cRZBaseString_MakeUpper);

    std::string longText;
    for (int i = 0; i < 40; ++i)
        longText += "Long TEXT with \xC3\x89 and \xC3\xA9, ";
    const std::string texts[] = {
        "",
        "abc",
        "  \t padded \t ",
        "\t\t  ",
        "key=value=more",
        "a,b,,c,",
        ",,,lead",
        "h\xC3\xA9llo W\xC3\x96RLD",
        "MiXeD Case 123 [\\]^_`{|}~",
        "x\x7Fy UPPER lower",
        "\xE9t\xE9 \xC0\xC9\xCE",
        "::",
        "token  token2   token3 ",
        longText,
    };
    static const char* const kParts[] = {"", "a", "abc", "  ", "ke", "more", "c,", "zz", "Long", ", "};
    static const char* const kSeparators[] = {nullptr, "", "=", ",", "==", ",,", "lue", "zz", " ", "\xC3"};
    static const char kChars[] = {'=', ',', ' ', 'z', ':', 'a', '\t'};
    char built[0x14];

    for (const std::string& text : texts) {
        const char* const t = text.c_str();
        const uint32_t size = static_cast<uint32_t>(text.size());
        GameString whole(t, size);
        for (const char* const part : kParts)
            for (const uint32_t length : {uint32_t{0}, uint32_t{1}, static_cast<uint32_t>(std::strlen(part))}) {
                if (length > std::strlen(part))
                    continue;
                MixInto(h, beginsWith(whole.object, part, length) & 0xFF);
                MixInto(h, endsWith(whole.object, part, length) & 0xFF);
            }
        MixInto(h, beginsWith(whole.object, t, size) & 0xFF);
        MixInto(h, endsWith(whole.object, t, size) & 0xFF);
        {
            GameString copy(t, size);
            MixInto(h, appendText(copy.object, "+tail") == copy.object ? 1 : 0);
            appendText(copy.object, "");
            MixString(copy.object);
            for (const uint32_t most : {uint32_t{0}, uint32_t{1}, uint32_t{3}, size, size + 1, uint32_t{0xFFFFFFFF}}) {
                MixInto(h, assignLimited(copy.object, t, most) == copy.object ? 1 : 0);
                MixString(copy.object);
            }
        }
        for (const uint32_t position : {uint32_t{0}, uint32_t{1}, uint32_t{3}, size - 1, size, size + 1, uint32_t{100000}})
            for (const uint32_t count : {uint32_t{0}, uint32_t{1}, uint32_t{2}, size, uint32_t{100000}, uint32_t{0xFFFFFFFF}}) {
                MixInto(h, mid(whole.object, built, position, count) == built ? 1 : 0);
                MixBuilt(built);
            }
        for (const uint32_t count : {uint32_t{0}, uint32_t{1}, uint32_t{3}, size - 1, size, size + 1, uint32_t{0xFFFFFFFF}}) {
            MixInto(h, left(whole.object, built, count) == built ? 1 : 0);
            MixBuilt(built);
            MixInto(h, right(whole.object, built, count) == built ? 1 : 0);
            MixBuilt(built);
        }
        for (const PlainFn change : {lTrim, rTrim, trim, makeLower, makeUpper}) {
            GameString copy(t, size);
            change(copy.object);
            MixString(copy.object);
            change(copy.object);
            MixString(copy.object);
        }
        for (const char c : kChars) {
            GameString copy(t, size);
            MixInto(h, cutThrough(copy.object, static_cast<uint32_t>(c)) & 0xFF);
            MixString(copy.object);
            MixInto(h, cutThrough(copy.object, static_cast<uint32_t>(c)) & 0xFF);
            MixString(copy.object);

            GameString source(t, size);
            GameString target("what the target held");
            for (int round = 0; round < 3; ++round) {
                MixInto(h, splitAtChar(source.object, static_cast<uint32_t>(c), target.object) & 0xFF);
                MixString(source.object);
                MixString(target.object);
            }
            GameString tokens(t, size);
            GameString token("what the token held");
            for (int round = 0; round < 12; ++round) {
                const uint32_t found = splitToken(tokens.object, static_cast<uint32_t>(c), token.object) & 0xFF;
                MixInto(h, found);
                MixString(tokens.object);
                MixString(token.object);
                if (!found)
                    break;
            }
        }
        for (const char* const separator : kSeparators) {
            GameString copy(t, size);
            for (int round = 0; round < 3; ++round) {
                MixInto(h, splitOff(copy.object, built, separator) == built ? 1 : 0);
                MixBuilt(built);
                MixString(copy.object);
            }
        }

        // The assigning helpers.
        {
            GameString target("replaced");
            using ToBodyFn = void*(__cdecl*)(char*, char*);
            const void* const result =
                reinterpret_cast<ToBodyFn>(addr::cRZBaseString_AssignToBody)(whole.object, target.object + 0x4);
            MixInto(h, result == target.object + 0x4 ? 1 : 0);
            MixString(target.object);
            GameString second("replaced too");
            using FromRangeFn = void(__cdecl*)(char*, char*);
            reinterpret_cast<FromRangeFn>(addr::cRZBaseString_AssignFromRange)(whole.object + 0x4, second.object);
            MixString(second.object);
        }
        // To wide characters and back: the wide body is made here as the
        // game's are (a block from its allocator, ended by a zero).
        {
            struct {
                uint16_t* begin;
                uint16_t* end;
                uint16_t* capacity;
            } wide;
            wide.begin = static_cast<uint16_t*>(reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(8));
            wide.end = wide.begin;
            wide.capacity = wide.begin + 4;
            *wide.begin = 0;
            using ToWideFn = void(__cdecl*)(char*, void*);
            reinterpret_cast<ToWideFn>(addr::cRZBaseString_ToWideSigned)(whole.object, &wide);
            MixInto(h, static_cast<uint32_t>(wide.end - wide.begin));
            for (const uint16_t* w = wide.begin; w != wide.end; ++w)
                MixInto(h, *w);
            // Some above 0x7F for the way back.
            for (uint16_t* w = wide.begin; w != wide.end; ++w)
                if ((w - wide.begin) % 5 == 4)
                    *w = static_cast<uint16_t>(*w + 0x100);
            GameString narrow("narrow before");
            using FromWideFn = void(__cdecl*)(void*, char*);
            reinterpret_cast<FromWideFn>(addr::cRZBaseString_FromWideAscii)(&wide, narrow.object);
            MixString(narrow.object);
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(wide.begin);
        }
    }
    log::Write("autotest: string helpers driven (hash %08X)", h);
}

void StringHelpersFingerprint(cISC4City*, std::string& line)
{
    if (!g_helpersOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | shlp %08X", g_helpersHash);
    line += text;
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (!g_on)
        return;
    char text[96];
    std::snprintf(text, sizeof text, " | msg %08X n %u x %u refs %u %u %u", g_hash, g_deliveries, g_refused,
                  g_targets[0].refs(), g_targets[1].refs(), g_targets[2].refs());
    line += text;
}

void QueueFingerprint(cISC4City*, std::string& line)
{
    if (!g_queueOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | mq %08X", g_queueHash);
    line += text;
}

const FingerprintPart kFingerprint{"messages", 4, &Fingerprint};
const DayHook kDrive{"messages", 4, &Drive};
const DayHook kDriveCancel{"messages.cancel", 4, &DriveCancel};
const FingerprintPart kQueueFingerprint{"messages.queue", 4, &QueueFingerprint};
const DayHook kDriveQueue{"messages.queue", 4, &DriveQueue};
const FingerprintPart kServicesFingerprint{"services", 4, &ServicesFingerprint};
const DayHook kDriveServices{"services", 4, &DriveServices};
const FingerprintPart kClassesFingerprint{"classes", 4, &ClassesFingerprint};
const DayHook kDriveClasses{"classes", 4, &DriveClasses};
const FingerprintPart kStartFingerprint{"service_start", 4, &StartFingerprint};
const FingerprintPart kStringsFingerprint{"strings", 4, &StringsFingerprint};
const DayHook kDriveStrings{"strings", 4, &DriveStrings};
const FingerprintPart kMessageObjectFingerprint{"message_object", 4, &MessageObjectFingerprint};
const DayHook kDriveMessageObject{"message_object", 4, &DriveMessageObject};
const FingerprintPart kMessagePoolFingerprint{"message_pool", 4, &MessagePoolFingerprint};
const DayHook kDriveMessagePool{"message_pool", 4, &DriveMessagePool};
const FingerprintPart kLocksFingerprint{"locks", 4, &LocksFingerprint};
const DayHook kDriveLocks{"locks", 4, &DriveLocks};
const FingerprintPart kStringFormatFingerprint{"string_format", 4, &StringFormatFingerprint};
const DayHook kDriveStringFormat{"string_format", 4, &DriveStringFormat};
const FingerprintPart kStringHelpersFingerprint{"string_helpers", 4, &StringHelpersFingerprint};
const DayHook kDriveStringHelpers{"string_helpers", 4, &DriveStringHelpers};

} // namespace
} // namespace opensc4::autotest
