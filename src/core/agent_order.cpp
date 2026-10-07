// determinism.agent_order (P4): simulator agents are dispatched in the order
// they registered, not in heap-address order.
//
// cSC4Simulator keeps one map of agents per agent type (new day, new month,
// idle, ...), keyed by the agent pointer compared as a number (insert
// 0x00704060), and walks it in key order with a cursor that persists between
// ticks (0x007038B0, the idle dispatcher 0x00702DF0). So the order in which
// the simulators act within a tick, and which of them gets a turn when a
// slice's budget runs out mid-round, followed where the heap had put them:
// one unread environment variable, or 16 bytes more on the heap at startup,
// changed a city's development (docs/experiments/determinism.md).
//
// Here AddAgent is given a small forwarding object instead of the agent, one
// per agent, taken in registration order from one block, so the map order is
// the registration order. The forwarder passes QueryInterface, AddRef,
// Release and DoMessage to the agent; both RemoveAgent overloads translate.
// Registration order is fixed (the simulators register while the city
// initialises), so the dispatch order no longer depends on heap layout.
#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <cIGZMessageTarget2.h>
#include <cIGZString.h>

#include <windows.h>

#include <cstdint>
#include <new>

#include <opensc4/addresses.h>

namespace opensc4::agent_order {
namespace {

class AgentForwarder final : public cIGZMessageTarget2 {
public:
    explicit AgentForwarder(cIGZMessageTarget2* agent) : agent_(agent) {}

    bool QueryInterface(uint32_t riid, void** object) override { return agent_->QueryInterface(riid, object); }
    uint32_t AddRef() override { return agent_->AddRef(); }
    uint32_t Release() override { return agent_->Release(); }
    bool DoMessage(cIGZMessage2* message) override { return agent_->DoMessage(message); }

    cIGZMessageTarget2* Agent() const { return agent_; }

private:
    cIGZMessageTarget2* agent_;
};

// Forwarders live in one block, handed out at increasing addresses, and are
// never freed (an agent that leaves and comes back keeps its forwarder, and
// so its place). Main thread only, like the simulator.
constexpr int kMaxAgents = 1024;
AgentForwarder* g_block = nullptr;
int g_count = 0;

cIGZMessageTarget2* ForwarderFor(cIGZMessageTarget2* agent, bool create)
{
    if (!agent || !g_block)
        return agent;
    for (int i = 0; i < g_count; ++i)
        if (g_block[i].Agent() == agent)
            return &g_block[i];
    if (!create)
        return agent;
    if (g_count == kMaxAgents) {
        log::Write("agent_order: more than %d agents; %p keeps its own address", kMaxAgents, static_cast<void*>(agent));
        return agent;
    }
    return new (&g_block[g_count++]) AgentForwarder(agent);
}

using AddAgentFn = bool(__fastcall*)(void* self, void* edx, cIGZMessageTarget2* agent, uint32_t type,
                                     cIGZString const& name, uint32_t flags);
AddAgentFn g_addAgent = nullptr;

bool __fastcall AddAgent(void* self, void* edx, cIGZMessageTarget2* agent, uint32_t type, cIGZString const& name,
                         uint32_t flags)
{
    return g_addAgent(self, edx, ForwarderFor(agent, true), type, name, flags);
}

using RemoveAllTypesFn = bool(__fastcall*)(void* self, void* edx, cIGZMessageTarget2* agent);
RemoveAllTypesFn g_removeAllTypes = nullptr;

bool __fastcall RemoveAgentAllTypes(void* self, void* edx, cIGZMessageTarget2* agent)
{
    return g_removeAllTypes(self, edx, ForwarderFor(agent, false));
}

using RemoveFn = bool(__fastcall*)(void* self, void* edx, cIGZMessageTarget2* agent, uint32_t type);
RemoveFn g_remove = nullptr;

bool __fastcall RemoveAgent(void* self, void* edx, cIGZMessageTarget2* agent, uint32_t type)
{
    return g_remove(self, edx, ForwarderFor(agent, false), type);
}

bool Install(registry::Mode)
{
    g_block = static_cast<AgentForwarder*>(
        VirtualAlloc(nullptr, sizeof(AgentForwarder) * kMaxAgents, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    const addr::Entry* add = addr_check::Find(addr::cSC4Simulator_AddAgent);
    const addr::Entry* removeAll = addr_check::Find(addr::cSC4Simulator_RemoveAgentAllTypes);
    const addr::Entry* remove = addr_check::Find(addr::cSC4Simulator_RemoveAgent);
    const bool ok = g_block && add && removeAll && remove && hook::Install(*add, &AddAgent, &g_addAgent) &&
                    hook::Install(*removeAll, &RemoveAgentAllTypes, &g_removeAllTypes) &&
                    hook::Install(*remove, &RemoveAgent, &g_remove);
    log::Write("agent_order: %s", ok ? "agents are dispatched in registration order" : "FAILED");
    return ok;
}

const bool g_registered = registry::Register({
    "determinism.agent_order",
    "determinism",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_AddAgent,
    "Simulator agents are dispatched in registration order instead of heap-address order",
    false,
    Install,
});

} // namespace

} // namespace opensc4::agent_order
