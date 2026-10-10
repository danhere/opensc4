// cSC4AdvisorSystem (class 0x4A0DD616; docs/advisors.md): the city's
// advisors, the events they subscribe to, the script globals they read and
// the context advice. Replacements, each off by default:
//
//   simulators.advisors.interface: the cISC4AdvisorSystem methods (adding,
//     removing and finding advisors, events and subscriptions, script
//     globals, context advice, thresholds, data tags), the extra slots on
//     the helper at +0x5C, QueryInterface and the advisor-run message.
//   simulators.advisors.lifecycle: Init, Shutdown, RemoveAll and the
//     popup thresholds (LoadThresholds).
//   simulators.advisors.run: the messages (the daily run, the cheats),
//     loading the advisor scripts and creating the advisors they list.
//   simulators.advisors.scripts: DoLink (links in advice text) and the
//     data tags' gc callback.
//   simulators.advisors.save: Write and Read.
//
// The map, set and vector helpers it uses stay the game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/sim_timers.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::advisors {
namespace {

namespace field {
constexpr std::size_t kThresholds = 0x1C; // three, by difficulty
constexpr std::size_t kScript = 0x58;     // cISCLua
constexpr std::size_t kHelper = 0x5C;
constexpr std::size_t kAdvisors = 0x4C;   // map: ID -> advisor
constexpr std::size_t kEvents = 0x68;     // map: event -> set of advisors
constexpr std::size_t kContext = 0x74;    // vector of advice (begin, end, capacity)
constexpr std::size_t kTags = 0x80;       // vector of data tags
} // namespace field

using ThisFn = void(__thiscall*)(void* self);

void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

struct Node { // std::map / std::set node
    uint8_t color;
    uint8_t pad[3];
    Node* parent;
    Node* left;
    Node* right;
    int32_t key;
    void* value;
    uint32_t size; // a set's size, after its header (event map values)
};

Node* Next(Node* node)
{
    if (Node* right = node->right) {
        while (right->left)
            right = right->left;
        return right;
    }
    Node* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return node->right == parent ? node : parent;
}

Node* Find(char* self, std::size_t map, int32_t key)
{
    Node* found;
    reinterpret_cast<void(__thiscall*)(void*, Node**, const int32_t*)>(addr::SC4Sint32Map_Find)(self + map, &found,
                                                                                                 &key);
    return found == At<Node*>(self, map) ? nullptr : found;
}

void* Script(char* self)
{
    return At<void*>(self, field::kScript);
}

void* NewMessage()
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory)
                                 : nullptr;
    if (message)
        Virtual<uint32_t>(message, 0x4);
    return message;
}

// A message of `type` carrying the advisor, posted or sent (the game's
// 0x00418A80, which takes the advisor in ECX and leaves its stack to the
// caller, so it is not hooked).
void AdvisorMessage(void* advisor, bool post, uint32_t type)
{
    void* unknown = nullptr;
    if (advisor && !Virtual<uint8_t>(advisor, 0x0, uint32_t{1}, &unknown)) {
        Release(unknown);
        return;
    }
    void* const message = NewMessage();
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x84, unknown);
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (post)
        Virtual<void>(server, 0x10, message, uint32_t{0});
    else
        Virtual<uint8_t>(server, 0xC, message);
    Release(message);
    Release(unknown);
}

uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    std::size_t at = 0;
    switch (iid) {
    case 0x4A0DD617:
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    case 0x452294AA:
        at = 0x4;
        break;
    case 0xE4FDA3D4:
        at = 0x10;
        break;
    case 0xE98B2F57:
        at = 0x8;
        break;
    default:
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            self + 0x14, iid, out);
    }
    *out = self ? self + at : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

uint32_t __fastcall PostCheck(char*, void*)
{
    void* const message = NewMessage();
    Virtual<void>(message, 0x14, uint32_t{0xCA5501F2});
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
    return Virtual<uint32_t>(message, 0x8);
}

int32_t __fastcall GetAdvicePopupPriorityThreshold(char* self, void*)
{
    switch (Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x60)) {
    case 1:
        return At<int32_t>(self, field::kThresholds + 4);
    case 2:
        return At<int32_t>(self, field::kThresholds + 8);
    default:
        return At<int32_t>(self, field::kThresholds);
    }
}

uint32_t __fastcall MakeHelper(char* self, void*)
{
    if (void* const helper = At<void*>(self, field::kHelper))
        return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(helper));
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x70);
    void* const helper = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4AdvisorHelper_Construct)(memory)
                                : nullptr;
    void* const old = At<void*>(self, field::kHelper);
    if (helper != old) {
        if (helper)
            Virtual<uint32_t>(helper, 0x4);
        At<void*>(self, field::kHelper) = helper;
        Release(old);
    }
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AdvisorHelper_Start)(At<void*>(self, field::kHelper));
}

uint8_t __fastcall Helper3(char* self, void*)
{
    void* const helper = At<void*>(self, field::kHelper);
    return helper ? reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4AdvisorHelper_Call3)(helper) : 0;
}

// These pass on what the helper returns, as the game's tail calls do.
uint32_t __fastcall Helper1(char* self, void*)
{
    void* const helper = At<void*>(self, field::kHelper);
    return helper ? reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AdvisorHelper_Call1)(helper) : 0;
}

uint32_t __fastcall Helper2(char* self, void*)
{
    void* const helper = At<void*>(self, field::kHelper);
    return helper ? reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AdvisorHelper_Call2)(helper) : 0;
}

uint32_t __fastcall DropHelper(char* self, void*)
{
    if (!At<void*>(self, field::kHelper))
        return 0;
    uint32_t result = reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AdvisorHelper_Stop)(
        At<void*>(self, field::kHelper));
    if (void* const helper = At<void*>(self, field::kHelper)) {
        At<void*>(self, field::kHelper) = nullptr;
        result = Virtual<uint32_t>(helper, 0x8);
    }
    return result;
}

void OpenGame(char* self, const char* name)
{
    Virtual<void>(Script(self), 0x94, reinterpret_cast<const char*>(addr::MySim_ScriptGame));
    Virtual<void>(Script(self), 0x84, name);
}

void CloseGame(char* self)
{
    Virtual<void>(Script(self), 0xB8, -3);
    Virtual<void>(Script(self), 0x2C, 1);
}

uint8_t __fastcall SetGlobalString(char* self, void*, const char* name, void* value)
{
    OpenGame(self, name);
    if (value)
        Virtual<uint32_t>(value, 0x4);
    void* const script = Script(self);
    Virtual<void>(script, 0x114, value, Virtual<int32_t>(self, 0x58, 1)); // GetDataTag(1)
    CloseGame(self);
    return 1;
}

uint8_t __fastcall SetGlobalNumber(char* self, void*, const char* name, double value)
{
    OpenGame(self, name);
    Virtual<void>(Script(self), 0x7C, value);
    CloseGame(self);
    return 1;
}

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

uint8_t __fastcall SetLocalValue(char* self, void*, const char* name, double value, void* subject)
{
    OpenGame(self, name);
    Virtual<void>(Script(self), 0x7C, value);
    Virtual<void>(Script(self), 0xB8, -3);
    GameString text{};
    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
    reinterpret_cast<void(__cdecl*)(GameString*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
        &text, reinterpret_cast<const char*>(addr::cSC4AdvisorSystem_SubjectFormat), name);
    Virtual<void>(Script(self), 0x84,
                  reinterpret_cast<const char*(__thiscall*)(GameString*)>(addr::cRZBaseString_Data)(&text));
    if (subject) {
        Virtual<uint32_t>(subject, 0x4);
        void* const script = Script(self);
        Virtual<void>(script, 0x114, subject, Virtual<int32_t>(self, 0x58, 0)); // GetDataTag(0)
    } else {
        Virtual<void>(Script(self), 0x114, nullptr, int32_t{0});
    }
    CloseGame(self);
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
    return 1;
}

void __fastcall SimulateAll(char* self, void*, uint32_t flag)
{
    Node* const header = At<Node*>(self, field::kAdvisors);
    for (Node* node = header->left; node != header; node = Next(node))
        Virtual<uint8_t>(node->value, 0x38, flag);
}

void* __fastcall PopContextAdvice(char* self, void*)
{
    void**& end = At<void**>(self, field::kContext + 4);
    if (At<void**>(self, field::kContext) == end)
        return nullptr;
    void* const advice = end[-1];
    --end;
    Release(*end);
    return advice;
}

void* __fastcall GetContextAdvice(char* self, void*)
{
    void** const end = At<void**>(self, field::kContext + 4);
    return At<void**>(self, field::kContext) == end ? nullptr : end[-1];
}

int32_t __fastcall GetDataTag(char* self, void*, int32_t index)
{
    return At<int32_t*>(self, field::kTags)[index];
}

void* __fastcall GetAdvisor(char* self, void*, int32_t id)
{
    Node* const found = Find(self, field::kAdvisors, id);
    return found ? found->value : nullptr;
}

uint8_t __fastcall PushEvent(char* self, void*, int32_t event, void* subject)
{
    if (Node* const found = Find(self, field::kEvents, event)) {
        Node* const set = static_cast<Node*>(found->value);
        for (Node* node = set->left; node != static_cast<Node*>(found->value); node = Next(node))
            Virtual<uint8_t>(reinterpret_cast<void*>(node->key), 0x48, event, subject);
    }
    return 1;
}

uint8_t __fastcall AddAdvisor(char* self, void*, void* advisor)
{
    if (!advisor)
        return 1;
    int32_t id = Virtual<int32_t>(advisor, 0x14);
    void** const slot = reinterpret_cast<void**(__thiscall*)(void*, const int32_t*)>(addr::SC4AdvisorMap_At)(
        self + field::kAdvisors, &id);
    void* const old = *slot;
    if (advisor != old) {
        Virtual<uint32_t>(advisor, 0x4);
        *slot = advisor;
        Release(old);
    }
    AdvisorMessage(advisor, true, 0x8A09FA6A);
    return 1;
}

uint32_t __fastcall PushContextAdvice(char* self, void*, void* advice)
{
    void* local = advice;
    if (local)
        Virtual<uint32_t>(local, 0x4);
    void**& end = At<void**>(self, field::kContext + 4);
    if (end != At<void**>(self, field::kContext + 8)) {
        if (end) {
            *end = local;
            if (local)
                Virtual<uint32_t>(local, 0x4);
        }
        ++end;
    } else {
        reinterpret_cast<void(__thiscall*)(void*, void**, void**, void**, uint32_t, uint32_t)>(
            addr::SC4RefPtrVector_InsertN)(self + field::kContext, end, &local, &local, 1, 1);
    }
    return local ? Virtual<uint32_t>(local, 0x8) : 0;
}

// The set node of `advisor` in an event's set, or null (keys are the
// advisors' addresses, compared unsigned).
Node* InSet(Node* event, void* advisor)
{
    const auto key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(advisor));
    Node* const header = static_cast<Node*>(event->value);
    Node* found = header;
    for (Node* at = header->parent; at;) {
        if (static_cast<uint32_t>(at->key) >= key) {
            found = at;
            at = at->left;
        } else {
            at = at->right;
        }
    }
    if (found == header || key < static_cast<uint32_t>(found->key))
        return nullptr;
    return found;
}

void EraseFromSet(Node* event, Node* node)
{
    Node* const header = static_cast<Node*>(event->value);
    using RebalanceFn = void*(__cdecl*)(Node*, Node**, Node**, Node**);
    if (void* const erased = reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(
            node, &header->parent, &header->left, &header->right))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(erased);
    --event->size;
}

void EraseEvent(char* self, Node* event)
{
    reinterpret_cast<void(__thiscall*)(void*, Node*)>(addr::SC4AdvisorEventMap_Erase)(self + field::kEvents, event);
}

uint8_t __fastcall RemoveAdvisor(char* self, void*, int32_t id)
{
    Node* const found = Find(self, field::kAdvisors, id);
    if (!found)
        return 0;
    void* const advisor = found->value;
    AdvisorMessage(advisor, false, 0x8A09FA69);
    struct {
        Node** begin;
        Node** end;
        Node** capacity;
    } hits{};
    Node* const events = At<Node*>(self, field::kEvents);
    for (Node* event = events->left; event != At<Node*>(self, field::kEvents); event = Next(event)) {
        Node* const member = InSet(event, advisor);
        if (!member)
            continue;
        Node* pair[2] = {event, member};
        if (hits.end != hits.capacity) {
            if (hits.end) {
                hits.end[0] = event;
                hits.end[1] = member;
            }
            hits.end += 2;
        } else {
            uint8_t allocator;
            reinterpret_cast<void(__thiscall*)(void*, Node**, Node* const*, uint8_t*, uint32_t, uint32_t)>(
                addr::SC4PointVector_InsertN)(&hits, hits.end, pair, &allocator, 1, 1);
        }
    }
    for (Node** at = hits.end; at != hits.begin;) {
        at -= 2;
        EraseFromSet(at[0], at[1]);
        if (!at[0]->size)
            EraseEvent(self, at[0]);
    }
    reinterpret_cast<void(__thiscall*)(void*, Node*)>(addr::UintMap_Erase)(self + field::kAdvisors, found);
    if (hits.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(hits.begin);
    return 1;
}

uint8_t __fastcall UnsubscribeFromEvent(char* self, void*, int32_t event, void* advisor)
{
    Node* const found = Find(self, field::kEvents, event);
    if (!found)
        return 0;
    Node* member;
    reinterpret_cast<void(__thiscall*)(void*, Node**, void* const*)>(addr::SC4Uint32Map_Find)(&found->value, &member,
                                                                                               &advisor);
    if (member == static_cast<Node*>(found->value))
        return 0;
    EraseFromSet(found, member);
    if (!found->size)
        EraseEvent(self, found);
    return 1;
}

uint8_t __fastcall SubscribeToEvent(char* self, void*, int32_t event, void* advisor)
{
    void* const set = reinterpret_cast<void*(__thiscall*)(void*, const int32_t*)>(addr::SC4AdvisorEventMap_At)(
        self + field::kEvents, &event);
    uint32_t result[2];
    reinterpret_cast<void(__thiscall*)(void*, uint32_t*, void* const*)>(addr::SC4PtrSet_Insert)(set, result, &advisor);
    return 1;
}

bool InstallInterface(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4AdvisorSystem_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4AdvisorSystem_PostCheck, reinterpret_cast<void*>(&PostCheck)},
        {addr::cSC4AdvisorSystem_GetAdvicePopupPriorityThreshold,
         reinterpret_cast<void*>(&GetAdvicePopupPriorityThreshold)},
        {addr::cSC4AdvisorSystem_MakeHelper, reinterpret_cast<void*>(&MakeHelper)},
        {addr::cSC4AdvisorSystem_Helper3, reinterpret_cast<void*>(&Helper3)},
        {addr::cSC4AdvisorSystem_Helper1, reinterpret_cast<void*>(&Helper1)},
        {addr::cSC4AdvisorSystem_Helper2, reinterpret_cast<void*>(&Helper2)},
        {addr::cSC4AdvisorSystem_DropHelper, reinterpret_cast<void*>(&DropHelper)},
        {addr::cSC4AdvisorSystem_SetGlobalString, reinterpret_cast<void*>(&SetGlobalString)},
        {addr::cSC4AdvisorSystem_SetGlobalNumber, reinterpret_cast<void*>(&SetGlobalNumber)},
        {addr::cSC4AdvisorSystem_SimulateAll, reinterpret_cast<void*>(&SimulateAll)},
        {addr::cSC4AdvisorSystem_PopContextAdvice, reinterpret_cast<void*>(&PopContextAdvice)},
        {addr::cSC4AdvisorSystem_GetContextAdvice, reinterpret_cast<void*>(&GetContextAdvice)},
        {addr::cSC4AdvisorSystem_GetDataTag, reinterpret_cast<void*>(&GetDataTag)},
        {addr::cSC4AdvisorSystem_GetAdvisor, reinterpret_cast<void*>(&GetAdvisor)},
        {addr::cSC4AdvisorSystem_PushEvent, reinterpret_cast<void*>(&PushEvent)},
        {addr::cSC4AdvisorSystem_SetLocalValue, reinterpret_cast<void*>(&SetLocalValue)},
        {addr::cSC4AdvisorSystem_AddAdvisor, reinterpret_cast<void*>(&AddAdvisor)},
        {addr::cSC4AdvisorSystem_PushContextAdvice, reinterpret_cast<void*>(&PushContextAdvice)},
        {addr::cSC4AdvisorSystem_RemoveAdvisor, reinterpret_cast<void*>(&RemoveAdvisor)},
        {addr::cSC4AdvisorSystem_UnsubscribeFromEvent, reinterpret_cast<void*>(&UnsubscribeFromEvent)},
        {addr::cSC4AdvisorSystem_SubscribeToEvent, reinterpret_cast<void*>(&SubscribeToEvent)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_interfaceRegistered = registry::Register({
    "simulators.advisors.interface",
    "simulators/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AdvisorSystem_AddAdvisor,
    "Our cSC4AdvisorSystem interface methods (docs/advisors.md)",
    false,
    InstallInterface,
});

// Lifecycle.

void Assign(char* self, std::size_t at, void* object)
{
    void* const old = At<void*>(self, at);
    if (object == old)
        return;
    if (object)
        Virtual<uint32_t>(object, 0x4);
    At<void*>(self, at) = object;
    Release(old);
}

void PushTag(char* self)
{
    int32_t tag = Virtual<int32_t>(Script(self), 0xEC); // new tag
    int32_t*& end = At<int32_t*>(self, field::kTags + 4);
    if (end != At<int32_t*>(self, field::kTags + 8)) {
        if (end)
            *end = tag;
        ++end;
    } else {
        uint8_t allocator;
        reinterpret_cast<void(__thiscall*)(void*, int32_t*, const int32_t*, uint8_t*, uint32_t, uint32_t)>(
            addr::SC4PtrVector_InsertN)(self + field::kTags, end, &tag, &allocator, 1, 1);
    }
    Virtual<void>(Script(self), 0x88, reinterpret_cast<void*>(addr::cSC4AdvisorSystem_GcCallback), int32_t{0});
    Virtual<void>(Script(self), 0xC4, tag, "gc"); // the tag's gc method
}

void AddCheat(void* cheats, const char* const* name, uint32_t id)
{
    GameString text{};
    reinterpret_cast<void*(__thiscall*)(GameString*, const char*)>(addr::SC4String_FromText)(&text, *name);
    Virtual<void>(cheats, 0xC, id, &text);
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
}

bool CheatsExtra()
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54) != 0;
}

constexpr uint32_t kMessages[] = {0x26D31EC1, 0x26D31EC4, 0xCA5501F2};

void __fastcall RemoveAll(char* self, void*)
{
    AdvisorMessage(nullptr, false, 0x8A09FA6B);
    while (At<uint32_t>(self, field::kAdvisors + 4)) {
        void* const advisor = At<Node*>(self, field::kAdvisors)->left->value;
        if (advisor)
            Virtual<uint32_t>(advisor, 0x4);
        Virtual<uint8_t>(self, 0x18, Virtual<int32_t>(advisor, 0x14)); // RemoveAdvisor(id)
        Virtual<void>(advisor, 0x10);                                  // its Shutdown
        Virtual<uint32_t>(advisor, 0x8);
    }
    if (At<uint32_t>(self, field::kEvents + 4)) {
        Node* const header = At<Node*>(self, field::kEvents);
        reinterpret_cast<void(__thiscall*)(void*, Node*)>(addr::SC4AdvisorEventMap_EraseSubtree)(self + field::kEvents,
                                                                                                 header->parent);
        header->left = header;
        header->parent = nullptr;
        header->right = header;
        At<uint32_t>(self, field::kEvents + 4) = 0;
    }
}

// The popup thresholds: 100 each, then the low/medium/high entries of
// tuning_constants.ADVICE_POPUP_PRIORITIES_BY_SKILL_LEVEL.
void __fastcall LoadThresholds(char* self, void*)
{
    reinterpret_cast<void(__cdecl*)(void*, uint32_t, uint32_t)>(addr::SC4_FillUint32)(self + field::kThresholds, 100,
                                                                                      3);
    void* const script = Script(self);
    if (!script)
        return;
    void* const state = Virtual<void*>(Virtual<void*>(script, 0x14), 0xC);
    int32_t top = 0;
    if (state) {
        Virtual<uint32_t>(state, 0x4);
        top = Virtual<int32_t>(state, 0x38);
    }
    Virtual<void>(Script(self), 0x94, "tuning_constants");
    if (Virtual<uint8_t>(Script(self), 0x120, -1)) {
        Virtual<void>(Script(self), 0x84, "ADVICE_POPUP_PRIORITIES_BY_SKILL_LEVEL");
        Virtual<void>(Script(self), 0x98, -2);
        if (Virtual<uint8_t>(Script(self), 0x120, -1)) {
            uint32_t map[5] = {};
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4Uint32HashSet_Construct)(map, 100);
            if (reinterpret_cast<uint8_t(__cdecl*)(void*, void*, int32_t)>(addr::SC4_TableToMap)(Script(self), map,
                                                                                                 -1)) {
                struct {
                    char* node;
                    void* table;
                } iterator{};
                reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4HashTable_Begin)(map, &iterator);
                while (iterator.node) {
                    const char* const begin = At<const char*>(iterator.node, 0x4);
                    const std::size_t size = At<const char*>(iterator.node, 0x8) - begin;
                    const int32_t value = At<int32_t>(iterator.node, 0x10);
                    if (size == 3 && !std::memcmp(begin, "low", 3))
                        At<int32_t>(self, field::kThresholds) = value;
                    else if (size == 6 && !std::memcmp(begin, "medium", 6))
                        At<int32_t>(self, field::kThresholds + 4) = value;
                    else if (size == 4 && !std::memcmp(begin, "high", 4))
                        At<int32_t>(self, field::kThresholds + 8) = value;
                    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4HashTable_Next)(&iterator);
                }
            }
            reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4HashTable_Clear)(map);
            if (map[1])
                reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(map[1]));
        }
    }
    if (state) {
        Virtual<void>(state, 0x3C, top);
        Virtual<uint32_t>(state, 0x8);
    }
}

uint8_t __fastcall Init(char* self, void*)
{
    if (!At<uint8_t>(self, 0x29)) {
        Assign(self, field::kScript, Global<void*>(addr::SC4_ScriptContext));
        Virtual<void>(Script(self), 0xC, int32_t{0x7E});
        reinterpret_cast<void(__thiscall*)(void*, int32_t)>(addr::cSC4AdvisorSystem_RegisterTypes)(self, 1);
        PushTag(self);
        PushTag(self);
        Assign(self, 0x64, Global<void*>(addr::SC4_Service43CDC));
        Virtual<void>(At<void*>(self, 0x64), 0x1C, reinterpret_cast<void*>(addr::cSC4AdvisorSystem_Callback), self,
                      int32_t{1});
        reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_Reload)(self);
        if (At<uint32_t>(self, field::kAdvisors + 4) && !At<uint8_t>(self, 0x28))
            RemoveAll(self, nullptr);
        if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
            Virtual<void>(cheats, 0x1C, self + 0x4, int32_t{0});
            AddCheat(cheats, reinterpret_cast<const char* const*>(addr::cSC4AdvisorSystem_CheatDolly), 0xAAB7F42A);
            if (CheatsExtra())
                AddCheat(cheats, reinterpret_cast<const char* const*>(addr::cSC4AdvisorSystem_CheatAdv), 0xA919D25E);
        }
        if (!At<uint8_t>(self, 0x28))
            reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_CreateAll)(self);
        At<uint8_t>(self, 0x29) = 1;
        reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_Reconcile)(self);
        LoadThresholds(self, nullptr);
        for (uint32_t type : kMessages)
            Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x14, self + 0x4, type);
    }
    if (void* const helper = At<void*>(self, field::kHelper)) {
        At<void*>(self, field::kHelper) = nullptr;
        Virtual<uint32_t>(helper, 0x8);
    }
    Virtual<void>(self, 0x60);
    return 1;
}

uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, 0x29))
        return 1;
    Virtual<void>(At<void*>(self, 0x64), 0x1C, reinterpret_cast<void*>(addr::cSC4AdvisorSystem_Callback), Script(self),
                  int32_t{0});
    if (void* const service = At<void*>(self, 0x64)) {
        At<void*>(self, 0x64) = nullptr;
        Virtual<uint32_t>(service, 0x8);
    }
    for (uint32_t type : kMessages)
        Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x18, self + 0x4, type);
    if (At<void*>(self, field::kHelper)) {
        Virtual<void>(self, 0x70);
        if (void* const helper = At<void*>(self, field::kHelper)) {
            At<void*>(self, field::kHelper) = nullptr;
            Virtual<uint32_t>(helper, 0x8);
        }
    }
    RemoveAll(self, nullptr);
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        Virtual<void>(cheats, 0x20, self + 0x4, int32_t{0});
        Virtual<void>(cheats, 0x10, uint32_t{0xAAB7F42A});
        if (CheatsExtra())
            Virtual<void>(cheats, 0x10, uint32_t{0xA919D25E});
    }
    Virtual<void>(Script(self), 0x84, reinterpret_cast<const char*>(addr::MySim_ScriptGame));
    Virtual<void>(Script(self), 0x78);
    Virtual<void>(Script(self), 0xBC, int32_t{-10001});
    Virtual<void>(Script(self), 0xE8, int32_t{0});
    Virtual<void>(Script(self), 0x10);
    if (void* const script = Script(self)) {
        At<void*>(self, field::kScript) = nullptr;
        Virtual<uint32_t>(script, 0x8);
    }
    void* const host = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0xC4);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4ScriptHost_Refresh)(host, nullptr);
    At<uint8_t>(self, 0x29) = 0;
    return 1;
}

bool Install(const std::pair<std::uintptr_t, void*>* hooks, std::size_t count, void** originals)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

bool InstallLifecycle(registry::Mode)
{
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4AdvisorSystem_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4AdvisorSystem_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4AdvisorSystem_RemoveAll, reinterpret_cast<void*>(&RemoveAll)},
        {addr::cSC4AdvisorSystem_LoadThresholds, reinterpret_cast<void*>(&LoadThresholds)},
    };
    return Install(hooks, 4, originals);
}

const bool g_lifecycleRegistered = registry::Register({
    "simulators.advisors.lifecycle",
    "simulators/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AdvisorSystem_Init,
    "Our cSC4AdvisorSystem Init, Shutdown, RemoveAll and LoadThresholds (docs/advisors.md)",
    false,
    InstallLifecycle,
});

// The run: scripts, advisors from the script, the messages.

struct ScriptTable { // SC4ScriptTable, 0x24 bytes
    int32_t top;
    uint32_t name;
    void* script;
    uint8_t open;
    uint8_t pad[3];
    std::uintptr_t textVtable;
    char* textBegin;
    char* textEnd;
    char* textCapacity;
    uint32_t textReferences;
};
static_assert(sizeof(ScriptTable) == 0x24);

// The script's `advisors` table, or the entry `index` of it (0: the table).
bool OpenTable(char* self, ScriptTable& table, uint32_t index)
{
    reinterpret_cast<void(__thiscall*)(ScriptTable*, void*, const char*, uint32_t)>(addr::SC4ScriptTable_Open)(
        &table, Script(self), *reinterpret_cast<const char* const*>(addr::cSC4AdvisorSystem_TableName), index);
    return table.open != 0;
}

void CloseTable(ScriptTable& table)
{
    Virtual<void>(table.script, 0x1C, table.top);
}

void FreeTable(ScriptTable& table)
{
    table.textVtable = addr::SC4String_Vtable;
    if (table.textBegin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(table.textBegin);
}

int32_t TableInt(void* script, const char* key)
{
    Virtual<void>(script, 0x84, key);
    Virtual<void>(script, 0x98, -2);
    const int32_t value = Virtual<int32_t>(script, 0x58, -1);
    Virtual<void>(script, 0x2C, 1);
    return value;
}

// The registered script types as game._tf_ entries (in globals when
// `globals`, else in the table `game`).
uint8_t __fastcall RegisterTypes(char* self, void*, uint8_t globals)
{
    const int32_t top = Virtual<int32_t>(Script(self), 0x18);
    struct Type {
        const char* name;
        int32_t tag;
        uint8_t global;
    };
    for (Type* type = Global<Type*>(addr::cSC4AdvisorSystem_ScriptTypes);
         type != Global<Type*>(addr::cSC4AdvisorSystem_ScriptTypes + 4); ++type) {
        if (!type->global && !globals)
            Virtual<void>(Script(self), 0x94, reinterpret_cast<const char*>(addr::MySim_ScriptGame));
        else if (type->global && globals)
            Virtual<void>(Script(self), 0xA4);
        if (Virtual<int32_t>(Script(self), 0x18) == top)
            continue;
        Virtual<void>(Script(self), 0x110, type->tag, "_tf_");
        Virtual<void>(Script(self), 0x84, type->name);
        Virtual<void>(Script(self), 0x94, "_tf_");
        Virtual<void>(Script(self), 0xB8, -3);
        Virtual<void>(Script(self), 0x2C, 1);
    }
    Virtual<void>(Script(self), 0x1C, top);
    return 1;
}

struct RangeString { // SC4String without its vtable
    char* begin;
    char* end;
    char* capacity;
};

void NewString(GameString& text)
{
    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
}

void AppendAdvisors(GameString& text)
{
    static const char kFolder[] = "advisors\\";
    uint8_t allocator;
    RangeString folder{};
    reinterpret_cast<void(__thiscall*)(RangeString*, const char*, const char*, uint8_t*)>(addr::SC4String_FromRange)(
        &folder, kFolder, kFolder + sizeof kFolder - 1, &allocator);
    char* const begin = folder.begin;
    reinterpret_cast<void(__thiscall*)(char**, const char*, const char*, uint8_t*)>(addr::SC4String_AppendRange)(
        &text.begin, begin, folder.end, &allocator);
    if (begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
}

const char* Data(GameString& text)
{
    return reinterpret_cast<const char*(__thiscall*)(GameString*)>(addr::cRZBaseString_Data)(&text);
}

void FreeString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
}

// _on_reload, then the advisor scripts from the two advisors\ folders.
uint8_t __fastcall Reload(char* self, void*)
{
    At<uint8_t>(self, 0x60) = 0;
    Virtual<void>(Script(self), 0x94, "_on_reload");
    if (Virtual<int32_t>(Script(self), 0x30, -1) == 6) // a function
        Virtual<void>(Script(self), 0xCC, int32_t{0}, int32_t{0});
    void* const host = Global<void*>(addr::SC4_Service43C94);
    GameString data{};
    NewString(data);
    Virtual<void>(host, 0xD4, &data, int32_t{-1});
    AppendAdvisors(data);
    GameString user{};
    NewString(user);
    Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xDC, &user);
    AppendAdvisors(user);
    constexpr uint32_t kGroup = 0x4A5E8EF6;
    using PathAddFn = void(__cdecl*)(uint32_t, const char*);
    reinterpret_cast<PathAddFn>(addr::SC4_ScriptPathAdd)(kGroup, Data(data));
    reinterpret_cast<PathAddFn>(addr::SC4_ScriptPathAdd)(kGroup, Data(user));
    reinterpret_cast<void(__cdecl*)(void*, uint32_t, uint32_t)>(addr::SC4_ScriptGroupLoad)(Script(self), kGroup, 0);
    uint8_t result = 0;
    if (RegisterTypes(self, nullptr, 0)) {
        void* const host = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0xC4);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4ScriptHost_Refresh)(host, Script(self));
        result = 1;
    }
    FreeString(user);
    FreeString(data);
    return result;
}

// advisors[index]: created by its class_id under its id and initialised;
// removed again if its Init fails. The game's returns 0 either way.
uint8_t __fastcall CreateAdvisor(char* self, void*, uint32_t index)
{
    ScriptTable table;
    if (OpenTable(self, table, index)) {
        void* const script = table.script;
        const uint32_t classId = static_cast<uint32_t>(TableInt(script, "class_id"));
        int32_t id = TableInt(script, "id");
        if (id) {
            void* advisor = nullptr;
            void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
            Virtual<uint8_t>(com, 0xC, classId, uint32_t{0xCA09F544}, &advisor);
            if (advisor) {
                void* const slot = reinterpret_cast<void*(__thiscall*)(void*, const int32_t*)>(addr::SC4AdvisorMap_At)(
                    self + field::kAdvisors, &id);
                reinterpret_cast<void(__thiscall*)(void*, void* const*)>(addr::SC4RefPtr_AssignFrom)(slot, &advisor);
                if (Virtual<uint8_t>(advisor, 0xC, index))
                    AdvisorMessage(advisor, true, 0x8A09FA6A);
                else
                    Virtual<uint8_t>(self, 0x18, id); // RemoveAdvisor
                Release(advisor);
            }
        }
        CloseTable(table);
    }
    FreeTable(table);
    return 0;
}

int32_t AdvisorCount(char* self)
{
    Virtual<void>(Script(self), 0x94, *reinterpret_cast<const char* const*>(addr::cSC4AdvisorSystem_TableName));
    const int32_t count = Virtual<int32_t>(Script(self), 0x100, -1);
    Virtual<void>(Script(self), 0x2C, 1);
    return count;
}

uint8_t __fastcall CreateAll(char* self, void*)
{
    ScriptTable table;
    if (!OpenTable(self, table, 0)) {
        FreeTable(table);
        return 0;
    }
    CloseTable(table);
    FreeTable(table);
    const int32_t count = AdvisorCount(self);
    for (int32_t index = 1; index <= count; ++index)
        CreateAdvisor(self, nullptr, static_cast<uint32_t>(index));
    return 1;
}

// Every entry of the advisors table: its advisor initialised again, or
// created when missing.
uint8_t __fastcall Reconcile(char* self, void*)
{
    ScriptTable table;
    if (!OpenTable(self, table, 0)) {
        FreeTable(table);
        return 0;
    }
    CloseTable(table);
    FreeTable(table);
    const int32_t count = AdvisorCount(self);
    for (int32_t index = 1; index <= count; ++index) {
        ScriptTable entry;
        if (OpenTable(self, entry, static_cast<uint32_t>(index))) {
            if (const int32_t id = TableInt(entry.script, "id")) {
                if (void* const advisor = Virtual<void*>(self, 0x1C, id)) // GetAdvisor
                    Virtual<uint8_t>(advisor, 0xC, static_cast<uint32_t>(index));
                else
                    CreateAdvisor(self, nullptr, static_cast<uint32_t>(index));
            }
            CloseTable(entry);
        }
        FreeTable(entry);
    }
    return 1;
}

double Unsigned(int32_t value)
{
    return static_cast<double>(static_cast<uint32_t>(value));
}

void SetGlobal(char* self, const char* name, double value)
{
    Virtual<uint8_t>(self, 0x34, name, value); // SetGlobalNumber
}

// Today as script globals; the gc on the 28th of the month.
void NewDay(char* self, char* date, int32_t day)
{
    At<int32_t>(self, 0x48) = day;
    using DateFn = int32_t(__thiscall*)(void*);
    if (reinterpret_cast<DateFn>(addr::SC4Date_Day)(date) == 28) {
        Virtual<void>(Script(self), 0xE0);
        Virtual<void>(Script(self), 0xE4);
        Virtual<void>(Script(self), 0xE8, int32_t{0});
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4AdvisorSystem_SimulateAll)(self, 1);
    }
    SetGlobal(self, "g_year", Unsigned(reinterpret_cast<DateFn>(addr::SC4Date_Year)(date)));
    SetGlobal(self, "g_month", Unsigned(reinterpret_cast<DateFn>(addr::SC4Date_Month)(date)));
    SetGlobal(self, "g_day", Unsigned(reinterpret_cast<DateFn>(addr::SC4Date_Day)(date)));
    SetGlobal(self, "g_date", Unsigned(day));
    // A date object with its date interface at +8, which the simulator
    // service fills with the city's start date.
    alignas(8) char start[16] = {};
    reinterpret_cast<void(__thiscall*)(void*)>(addr::cRZDate_Default)(start);
    Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x14, start + 8);
    const int32_t startYear = reinterpret_cast<DateFn>(addr::cRZDate_Year2)(start + 8);
    SetGlobal(self, "g_year_count", Unsigned(reinterpret_cast<DateFn>(addr::SC4Date_Year)(date) - startYear));
    reinterpret_cast<void(__thiscall*)(void*)>(addr::cRZDate_Destroy)(start);
}


// Cheats: DollyLlama toggles the GUID display; Adv takes an option.

void Post(uint32_t type, const bool* flag, const GameString* number)
{
    void* const message = NewMessage();
    Virtual<void>(message, 0x14, type);
    if (flag)
        Virtual<void>(message, 0x2C, static_cast<int32_t>(*flag ? 1 : 0)); // SetData1
    if (number) {
        const char* const text = reinterpret_cast<const char*(__thiscall*)(const GameString*)>(
            addr::cRZBaseString_Data)(number);
        Virtual<void>(message, 0x2C, reinterpret_cast<int32_t(__cdecl*)(const char*)>(addr::SC4_TextToInt)(text));
    }
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
    Release(message);
}

// The tokenizer's option `name` (with a value into `value`, or only present).
bool Option(void* tokenizer, const char* name, GameString* value)
{
    GameString key{};
    reinterpret_cast<void*(__thiscall*)(GameString*, const char*)>(addr::SC4String_FromText)(&key, name);
    const bool found =
        value ? reinterpret_cast<uint8_t(__thiscall*)(void*, GameString*, GameString*, uint32_t)>(
                    addr::SC4Tokenizer_GetOption)(tokenizer, &key, value, 1) != 0
              : reinterpret_cast<uint8_t(__thiscall*)(void*, GameString*)>(addr::SC4Tokenizer_HasOption)(tokenizer,
                                                                                                       &key) != 0;
    reinterpret_cast<void(__thiscall*)(GameString*)>(addr::SC4String_Destroy)(&key);
    return found;
}

bool ValueIs(GameString& value, const char* text)
{
    return reinterpret_cast<uint32_t(__cdecl*)(char**, const char*)>(addr::SC4String_Equals)(&value.begin, text) != 0;
}

uint8_t Cheat(char* self, void* message)
{
    const uint32_t id = Virtual<uint32_t>(message, 0x24); // GetData1
    if (id == 0xAAB7F42A) {
        uint8_t& guids = Global<uint8_t>(addr::cSC4AdvisorSystem_DebugGuids);
        guids = !guids;
        return 1;
    }
    if (id != 0xA919D25E)
        return 1;
    const char* const text = Virtual<const char*>(Virtual<void*>(message, 0x3C), 0x14); // GetVoid2
    alignas(8) char tokenizer[0x3C] = {};
    using ThisFn = void(__thiscall*)(void*);
    reinterpret_cast<void*(__thiscall*)(void*, const char*)>(addr::SC4Tokenizer_SC4Tokenizer)(tokenizer, text);
    GameString value{};
    reinterpret_cast<ThisFn>(addr::SC4String_SC4String)(&value);
    if (Option(tokenizer, "guids", &value)) {
        bool on = false;
        if (ValueIs(value, "on"))
            on = true;
        else if (!ValueIs(value, "off")) {
            reinterpret_cast<ThisFn>(addr::SC4String_Destroy)(&value);
            reinterpret_cast<ThisFn>(addr::SC4Tokenizer_Destroy)(tokenizer);
            return 1;
        }
        Post(0xEA917750, &on, nullptr);
        log::Write("advisors: cheat Adv guids %s", on ? "on" : "off");
    } else if (Option(tokenizer, "trigger", &value)) {
        Post(0x4A91DD6E, nullptr, &value);
        log::Write("advisors: cheat Adv trigger");
    } else if (Option(tokenizer, "print", &value)) {
        Post(0x4A92ECEF, nullptr, &value);
        log::Write("advisors: cheat Adv print");
    } else if (Option(tokenizer, "refresh", nullptr)) {
        Post(0x8A917759, nullptr, nullptr);
        log::Write("advisors: cheat Adv refresh");
    } else if (Option(tokenizer, "reload", nullptr)) {
        reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_Reload)(self);
        reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_Reconcile)(self);
        log::Write("advisors: cheat Adv reload");
    }
    FreeString(value);
    reinterpret_cast<ThisFn>(addr::SC4Tokenizer_Destroy)(tokenizer);
    return 1;
}

void PostCheckAndSimulate(char* self)
{
    reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cSC4AdvisorSystem_PostCheck)(self);
    if (At<int32_t>(self, 0x48) < 1)
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4AdvisorSystem_SimulateAll)(self, 1);
}

// The message target at +4.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - 0x4;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x26D31EC1:
        if (Virtual<uint8_t>(Virtual<void*>(message, 0x28), 0x58))
            PostCheckAndSimulate(self);
        At<uint8_t>(self, 0x28) = 0;
        return 1;
    case 0x26D31EC4:
        Virtual<uint8_t>(self, 0x3C, uint32_t{0xAA31A210}, nullptr); // PushEvent
        PostCheckAndSimulate(self);
        return 1;
    case 0x230E27AC:
        return Cheat(self, message);
    case 0xCA5501F2:
        break;
    default:
        return 1;
    }
    if (void* const city = Global<void*>(addr::SC4_City); city && !Virtual<uint8_t>(city, 0x58))
        return 1;
    void* const simulator = Global<void*>(addr::SC4_SimulatorService);
    alignas(8) char date[16] = {};
    reinterpret_cast<void(__thiscall*)(void*, int32_t)>(addr::cRZDate_FromDay)(date, Virtual<int32_t>(simulator, 0x1C));
    const int32_t day = At<int32_t>(date, 0x4);
    char* const watch = self + 0x90;
    bool run = true;
    if (day > At<int32_t>(self, 0x48)) {
        NewDay(self, date, day);
    } else if (Virtual<uint8_t>(Global<void*>(addr::SC4_SimulatorService), 0x48)) {
        sim_timers::Claim(watch);
        run = reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::GZStopwatch_ElapsedMilliseconds)(watch) > 500;
    } else {
        run = false;
    }
    if (run) {
        if (At<uint8_t>(self, 0x60)) {
            reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_Reload)(self);
            reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_Reconcile)(self);
        }
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4AdvisorSystem_SimulateAll)(self, 0);
        sim_timers::Claim(watch);
        reinterpret_cast<ThisFn>(addr::GZStopwatch_Start)(watch);
    }
    reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cSC4AdvisorSystem_PostCheck)(self);
    return 1;
}

bool InstallRun(registry::Mode)
{
    static void* originals[6] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4AdvisorSystem_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4AdvisorSystem_RegisterTypes, reinterpret_cast<void*>(&RegisterTypes)},
        {addr::cSC4AdvisorSystem_Reload, reinterpret_cast<void*>(&Reload)},
        {addr::cSC4AdvisorSystem_CreateAdvisor, reinterpret_cast<void*>(&CreateAdvisor)},
        {addr::cSC4AdvisorSystem_CreateAll, reinterpret_cast<void*>(&CreateAll)},
        {addr::cSC4AdvisorSystem_Reconcile, reinterpret_cast<void*>(&Reconcile)},
    };
    return Install(hooks, 6, originals);
}

const bool g_runRegistered = registry::Register({
    "simulators.advisors.run",
    "simulators/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AdvisorSystem_DoMessage,
    "Our cSC4AdvisorSystem messages (the daily run), script loading and advisor creation (docs/advisors.md)",
    false,
    InstallRun,
});

// Save: version 1, the advisor count, then each advisor as a reference
// and its ID (in ID order). `serializable` is the object at +0x10.

uint32_t __fastcall Write(char* serializable, void*, void* stream)
{
    char* const self = serializable - 0x10;
    Virtual<void>(stream, 0x1C, uint16_t{1});
    Virtual<void>(stream, 0x20, At<uint32_t>(self, field::kAdvisors + 4));
    Node* const header = At<Node*>(self, field::kAdvisors);
    for (Node* node = header->left; node != header; node = Next(node)) {
        void* const advisor = node->value;
        reinterpret_cast<uint8_t(__cdecl*)(void*, void*)>(addr::SC4Stream_WriteReference)(stream, advisor);
        Virtual<void>(stream, 0x20, Virtual<uint32_t>(advisor, 0x14));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

uint8_t __fastcall Read(char* serializable, void*, void* stream)
{
    char* const self = serializable - 0x10;
    uint16_t version = 0;
    Virtual<void>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    uint32_t count = 0;
    Virtual<void>(stream, 0x20, &count);
    while (count) {
        --count;
        void* advisor = nullptr;
        reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, void**, uint32_t)>(addr::SC4Stream_ReadReference)(
            stream, 0xCA09F544, &advisor, 0);
        int32_t id = 0;
        Virtual<void>(stream, 0x20, &id);
        void** const slot = reinterpret_cast<void**(__thiscall*)(void*, const int32_t*)>(addr::SC4AdvisorMap_At)(
            self + field::kAdvisors, &id);
        void* const old = *slot;
        if (advisor != old) {
            if (advisor)
                Virtual<uint32_t>(advisor, 0x4);
            *slot = advisor;
            Release(old);
        }
        Release(advisor);
    }
    const uint8_t ok = Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
    At<uint8_t>(self, 0x28) = ok;
    return ok;
}

bool InstallSave(registry::Mode)
{
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4AdvisorSystem_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4AdvisorSystem_Read, reinterpret_cast<void*>(&Read)},
    };
    return Install(hooks, 2, originals);
}

const bool g_saveRegistered = registry::Register({
    "simulators.advisors.save",
    "simulators/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AdvisorSystem_Write,
    "Our cSC4AdvisorSystem Write and Read (docs/advisors.md)",
    false,
    InstallSave,
});

// Scripts: links in advice text, the data tags' gc.

// A link: '%08x%08x' (an advisor ID and an advice GUID), then the advice's
// argument; an ID of 0x1D (or no such advisor or advice) runs the text
// after the first 16 (0x1D) or 8 characters as script, with an empty
// context advice pushed around it.
uint8_t __fastcall DoLink(char* self, void*, void* link)
{
    const uint32_t size = Virtual<uint32_t>(link, 0x1C);
    const char* const begin = Virtual<const char*>(link, 0x18);
    uint32_t first;
    GameString text{};
    reinterpret_cast<void(__thiscall*)(char**, const char*, const char*, void*)>(addr::SC4String_FromRange)(
        &text.begin, begin, begin + size, &first);
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
    reinterpret_cast<void(__thiscall*)(GameString*)>(addr::cRZBaseString_Trim)(&text);
    first = 0;
    uint32_t second = 0;
    if (reinterpret_cast<int(__cdecl*)(const char*, const char*, ...)>(addr::SC4_sscanf)(Data(text), "%08x%08x",
                                                                                         &first, &second) != 2) {
        FreeString(text);
        return 0;
    }
    if (first != 0x1D && first != 0) {
        void* const advisor = Virtual<void*>(self, 0x1C, first); // GetAdvisor
        if (advisor && second) {
            if (void* const advice = Virtual<void*>(advisor, 0x34, second)) {
                const uint32_t length = reinterpret_cast<uint32_t(__thiscall*)(GameString*)>(
                    addr::cRZBaseString_Strlen)(&text);
                GameString rest{};
                void* const right = reinterpret_cast<void*(__thiscall*)(GameString*, GameString*, uint32_t)>(
                    addr::cRZBaseString_Right)(&text, &rest, length - 0x10);
                reinterpret_cast<void(__thiscall*)(GameString*, void*)>(addr::cRZBaseString_Assign)(&text, right);
                reinterpret_cast<void(__thiscall*)(GameString*)>(addr::SC4String_Destroy)(&rest);
                Virtual<void>(advice, 0x58, &text);
                FreeString(text);
                return 1;
            }
        }
    }
    Virtual<uint32_t>(self, 0x48, nullptr); // PushContextAdvice(null)
    const char* const chunk = Data(text) + (first == 0x1D ? 0x10 : 0x8);
    Virtual<void>(Script(self), 0xDC, chunk, static_cast<uint32_t>(std::strlen(chunk)), chunk);
    Virtual<void*>(self, 0x4C); // PopContextAdvice
    FreeString(text);
    return 1;
}

// The gc method of the two data tags: the userdata's object released.
int __cdecl GcCallback(void* state)
{
    void* script = nullptr;
    if (void* const owner = *reinterpret_cast<void**>(static_cast<char*>(state) - 4))
        Virtual<uint8_t>(owner, 0x0, uint32_t{0xEA10C4A2}, &script);
    char* const system = Global<char*>(addr::cSC4AdvisorSystem_Instance);
    if (system && Virtual<void*>(system, 0x28) == script && Virtual<int32_t>(script, 0x30, -1) == 7) {
        void* object = nullptr;
        bool tagged = false;
        for (int32_t tag = 0; tag < 2 && !tagged; ++tag) {
            const int32_t want = Virtual<int32_t>(system, 0x58, tag); // GetDataTag
            if (Virtual<int32_t>(script, 0x44, -1) == want) {
                object = Virtual<void*>(script, 0x70, -1);
                tagged = true;
            }
        }
        if (object)
            Release(object);
    }
    Release(script);
    return 0;
}

bool InstallScripts(registry::Mode)
{
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4AdvisorSystem_DoLink, reinterpret_cast<void*>(&DoLink)},
        {addr::cSC4AdvisorSystem_GcCallback, reinterpret_cast<void*>(&GcCallback)},
    };
    return Install(hooks, 2, originals);
}

const bool g_scriptsRegistered = registry::Register({
    "simulators.advisors.scripts",
    "simulators/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AdvisorSystem_DoLink,
    "Our cSC4AdvisorSystem DoLink and data-tag gc callback (docs/advisors.md)",
    false,
    InstallScripts,
});

} // namespace
} // namespace opensc4::advisors
