// The framework's part of the autotest (src/core/autotest_hooks.h): a drive
// of the message server through its interface, with targets of our own
// (docs/framework.md).
#include "core/autotest_hooks.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <GZServPtrs.h>
#include <cIGZMessage2.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZSystemService.h>

#include <cstdint>
#include <cstdio>

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

} // namespace
} // namespace opensc4::autotest
