// The framework's part of the autotest (src/core/autotest_hooks.h): a drive
// of the message server through its interface, with targets of our own
// (docs/framework.md).
#include "core/autotest_hooks.h"
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

} // namespace
} // namespace opensc4::autotest
