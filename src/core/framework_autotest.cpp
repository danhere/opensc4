// The framework's part of the autotest (src/core/autotest_hooks.h): a drive
// of the message server through its interface, with targets of our own
// (docs/framework.md).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <GZServPtrs.h>
#include <cIGZMessage2.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZSystemService.h>

#include <cstdint>
#include <cstdio>

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

void Fingerprint(cISC4City*, std::string& line)
{
    if (!g_on)
        return;
    char text[96];
    std::snprintf(text, sizeof text, " | msg %08X n %u x %u refs %u %u %u", g_hash, g_deliveries, g_refused,
                  g_targets[0].refs(), g_targets[1].refs(), g_targets[2].refs());
    line += text;
}

const FingerprintPart kFingerprint{"messages", 4, &Fingerprint};
const DayHook kDrive{"messages", 4, &Drive};

} // namespace
} // namespace opensc4::autotest
