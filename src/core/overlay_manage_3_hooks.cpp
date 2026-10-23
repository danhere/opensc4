// The detours of our overlay manager's bookkeeping, part 3 (ours in
// overlay_manage_3.cpp; docs/renderer.md, "Bookkeeping"), and its compare
// mode.
//
// Every caller in the exe calls these with plain thiscall/cdecl frames (the
// deleting destructor through the adjustor thunk 0x0073BDC0's jump, the view
// draw through the vtable 0x00AB39D0) and keeps nothing in EAX, ECX or EDX
// across the call; no jump lands inside the first five bytes of any of them.
//
// Compare mode. The game calls five of them from outside the class: the
// constructor and Init (the terrain view's 0x0075C420), Clear (its
// 0x007560F0), the deleting destructor (the last Release) and the view
// draw. The first four run ours on a copy of the manager, then the game's on
// the manager itself, and compare the two:
//   - the constructor: ours on a copy of the memory as it was, every byte;
//   - Init and Clear: the message server's slots 0x14 and 0x18 recorded on
//     both sides (the global 0x00B43CCC points at a stand-in for the call:
//     calls for the manager or the copy are recorded, the game's then passed
//     on, ours not; every other call is passed on), the fields (the vectors
//     by size and capacity; the reference count at +0xC left out, which the
//     server's AddRef and Release change on the game's side only), the
//     render states, every bucket list. The copy
//     holds its own references to what the call releases (the view, the
//     terrain, the texture manager, the overlay records' textures and
//     sources), so ours releases those. After Init the copy is destroyed by
//     the game's destructor;
//   - the deleting destructor: ours's destructor on a deep copy (its own
//     blocks from the game's allocator, its own nodes and references), then
//     the game's on the manager, compared inside the game's call to the
//     destructor's body before the game frees the memory: the fields left
//     (the blocks by size and capacity) and the reference counts of the
//     textures (each must be back where it was before the copy took its
//     own).
// The view draw only calls Draw, which draws: compare mode runs ours (it is
// counted, not compared). The containers are reached only from inside those
// calls, each side its own, except where the autotest calls them by address
// (OPENSC4_TEST_OVERLAYS): a list assigned, n lists inserted, the buckets'
// vector resized and the overlay records destroyed are then compared at the
// top on copies of their lists and vectors (the lists by values and by which
// of the old nodes each place reuses; the records by the reference counts).
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/overlay_manage.h"
#include "core/overlay_manage_3.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace opensc4::overlay_manage::part3 {
namespace {

constexpr std::size_t kHandleSize = 0x30, kGroupSize = 0x24, kStateSize = 8;
constexpr std::size_t kTexture = 0xAC, kTextureSource = 0xB0;
constexpr std::size_t kTextureReferences = 0xC; // SC4TextureBinding's count

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

bool g_compare = false;

std::string Hex32(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof text, "%08X", static_cast<unsigned>(value));
    return text;
}

void* Allocate(std::size_t bytes)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, bytes);
}

void Free(void* block)
{
    Cdecl<void>(addr::SC4_Free, block);
}

// ====================================================================================
// Counts
// ====================================================================================

enum Fn {
    kCutAfter, kCopyNodes, kAssignList, kDestroyOverlays, kClearList, kFillLists, kCopyLists, kFillNLists,
    kCopyListsBackward, kCopyNewLists, kViewDraw, kDestroyLists, kClear, kResizeStates, kSetupStates, kGrowBuckets,
    kConstruct, kDestruct, kInsertLists, kDeletingDestruct, kResizeBuckets, kSetupBuckets, kInit, kFnCount
};

constexpr const char* kNames[kFnCount] = {
    "CutAfter", "CopyNodes", "AssignList", "DestroyOverlays", "ClearList", "FillLists", "CopyLists", "FillNLists",
    "CopyListsBackward", "CopyNewLists", "ViewDraw", "DestroyLists", "Clear", "ResizeStates", "SetupStates", "GrowBuckets",
    "Construct", "Destruct", "InsertLists", "DeletingDestruct", "ResizeBuckets", "SetupBuckets", "Init",
};

uint64_t g_calls[kFnCount] = {};

void Reached(Fn fn)
{
    if (g_calls[fn]++ == 0)
        log::Write("overlays manage3: first %s", kNames[fn]);
}

// The calls each detour has seen, logged after the top calls of Clear and
// the deleting destructor (a city closing).
void LogCounts()
{
    std::string text;
    for (int fn = 0; fn < kFnCount; ++fn) {
        char word[48];
        std::snprintf(word, sizeof word, " %s %llu", kNames[fn], static_cast<unsigned long long>(g_calls[fn]));
        text += word;
    }
    log::Write("overlays manage3: calls:%s", text.c_str());
}

// Ours, the game's, or in compare mode at the top `compared`.
template <typename Ours, typename Game, typename Compared>
auto Route(Ours&& ours, Game&& game, Compared&& compared)
{
    if (!g_compare || g_side == Side::Ours)
        return ours();
    if (g_side == Side::Game)
        return game();
    return compared();
}

// For the functions only the others reach: at the top in compare mode the
// game's, once noted.
template <typename Ours, typename Game>
auto RouteInner(Fn fn, Ours&& ours, Game&& game)
{
    if (!g_compare || g_side == Side::Ours)
        return ours();
    if (g_side == Side::None) {
        static bool noted[kFnCount] = {};
        if (!noted[fn]) {
            noted[fn] = true;
            log::Write("overlays manage3: %s called outside a compared call, not compared", kNames[fn]);
        }
    }
    return game();
}

// ====================================================================================
// The game's functions (the detours' trampolines)
// ====================================================================================

using CutAfterFn = Node*(__fastcall*)(void*, void*, Node**, Node*);
using CopyNodesFn = void(__fastcall*)(void*, void*, Node**, const Node*, const Node*, const void*);
using AssignListFn = Node**(__fastcall*)(Node**, void*, Node* const*);
using DestroyOverlaysFn = void(__cdecl*)(char*, char*, const void*);
using ClearListFn = void(__fastcall*)(Node**);
using FillListsFn = void(__cdecl*)(Node**, Node**, Node* const*);
using CopyListsFn = Node**(__cdecl*)(Node**, Node**, Node**);
using FillNListsFn = Node**(__cdecl*)(Node**, uint32_t, Node* const*);
using ViewDrawFn = bool(__fastcall*)(char*, void*, void*);
using DestroyListsFn = void(__cdecl*)(Node**, Node**, const void*);
using SelfFn = void(__fastcall*)(char*);
using ResizeStatesFn = void(__fastcall*)(char*, void*, uint32_t);
using GrowBucketsFn = void(__fastcall*)(char*, void*, Node**, Node* const*, const void*, uint32_t, uint32_t);
using ConstructFn = char*(__fastcall*)(char*);
using InsertListsFn = void(__fastcall*)(char*, void*, Node**, uint32_t, Node* const*);
using DeletingFn = char*(__fastcall*)(char*, void*, uint32_t);
using ResizeBucketsFn = void(__fastcall*)(char*, void*, uint32_t, Node* const*);
using InitFn = void(__fastcall*)(char*, void*, void*, uint32_t, uint32_t);

CutAfterFn g_cutAfter;
CopyNodesFn g_copyNodes;
AssignListFn g_assignList;
DestroyOverlaysFn g_destroyOverlays;
ClearListFn g_clearList;
FillListsFn g_fillLists;
CopyListsFn g_copyLists, g_copyListsBackward, g_copyNewLists;
FillNListsFn g_fillNLists;
ViewDrawFn g_viewDraw;
DestroyListsFn g_destroyLists;
SelfFn g_clear, g_setupStates, g_destruct, g_setupBuckets;
ResizeStatesFn g_resizeStates;
GrowBucketsFn g_growBuckets;
ConstructFn g_construct;
InsertListsFn g_insertLists;
DeletingFn g_deleting;
ResizeBucketsFn g_resizeBuckets;
InitFn g_init;

// ====================================================================================
// Compare mode: the pieces
// ====================================================================================

// A call to the message server's slot 0x14 (wanted) or 0x18 (no longer
// wanted) for the manager under comparison: the target as an offset from it.
struct ServerCall {
    uint32_t slot;
    std::ptrdiff_t target;
    uint32_t type;
    bool operator==(const ServerCall& o) const { return slot == o.slot && target == o.target && type == o.type; }
};

void* g_realServer = nullptr;
const char* g_recordFrom = nullptr;
bool g_passRecorded = false;
std::vector<ServerCall>* g_serverCalls = nullptr;

// The stand-in's slots pass every call on to the real server, the two
// recorded ones as described above.
#define FORWARD(n)                                                                                                     \
    __declspec(naked) void Forward##n()                                                                                \
    {                                                                                                                  \
        __asm mov ecx, g_realServer                                                                                    \
        __asm mov eax, [ecx]                                                                                           \
        __asm jmp dword ptr [eax + 4 * n]                                                                              \
    }
FORWARD(0) FORWARD(1) FORWARD(2) FORWARD(3) FORWARD(4) FORWARD(5) FORWARD(6) FORWARD(7)
FORWARD(8) FORWARD(9) FORWARD(10) FORWARD(11) FORWARD(12) FORWARD(13) FORWARD(14) FORWARD(15)
FORWARD(16) FORWARD(17) FORWARD(18) FORWARD(19) FORWARD(20) FORWARD(21) FORWARD(22) FORWARD(23)
FORWARD(24) FORWARD(25) FORWARD(26) FORWARD(27) FORWARD(28) FORWARD(29) FORWARD(30) FORWARD(31)
#undef FORWARD

uint8_t ServerSlot(uint32_t slot, void* target, uint32_t type)
{
    const char* const t = static_cast<const char*>(target);
    const bool ours = g_recordFrom && t >= g_recordFrom && t < g_recordFrom + kSize;
    if (ours)
        g_serverCalls->push_back({slot, t - g_recordFrom, type});
    if (ours && !g_passRecorded)
        return 1;
    return Virtual<uint8_t>(g_realServer, slot, target, type);
}

uint8_t __fastcall StandInNotify(void*, void*, void* target, uint32_t type)
{
    return ServerSlot(0x14, target, type);
}

uint8_t __fastcall StandInStop(void*, void*, void* target, uint32_t type)
{
    return ServerSlot(0x18, target, type);
}

void* g_standInVtable[32] = {
    &Forward0, &Forward1, &Forward2, &Forward3, &Forward4, &Forward5, &Forward6, &Forward7,
    &Forward8, &Forward9, &Forward10, &Forward11, &Forward12, &Forward13, &Forward14, &Forward15,
    &Forward16, &Forward17, &Forward18, &Forward19, &Forward20, &Forward21, &Forward22, &Forward23,
    &Forward24, &Forward25, &Forward26, &Forward27, &Forward28, &Forward29, &Forward30, &Forward31,
};
struct StandInServer {
    void** vtable;
} g_standInServer{g_standInVtable};

// The stand-in in place of the server for a compared call; both sides'
// calls recorded.
class ServerSwap {
public:
    ServerSwap() : global_(Global<void*>(addr::SC4_MessageServer))
    {
        g_realServer = global_;
        g_serverCalls = nullptr;
        if (global_)
            Global<void*>(addr::SC4_MessageServer) = &g_standInServer;
    }
    ~ServerSwap()
    {
        Global<void*>(addr::SC4_MessageServer) = global_;
        g_recordFrom = nullptr;
        g_serverCalls = nullptr;
    }
    ServerSwap(const ServerSwap&) = delete;
    ServerSwap& operator=(const ServerSwap&) = delete;

    // Who the next calls are recorded for, and whether they are passed on.
    void Record(const char* manager, std::vector<ServerCall>& calls, bool pass)
    {
        g_recordFrom = manager;
        g_serverCalls = &calls;
        g_passRecorded = pass;
    }

private:
    void* global_;
};

std::string DescribeCalls(const std::vector<ServerCall>& game, const std::vector<ServerCall>& ours)
{
    std::size_t at = 0;
    while (at < game.size() && at < ours.size() && game[at] == ours[at])
        ++at;
    const auto one = [](const std::vector<ServerCall>& calls, std::size_t i) {
        if (i >= calls.size())
            return std::string("none");
        char text[48];
        std::snprintf(text, sizeof text, "%X(+0x%X, %08X)", static_cast<unsigned>(calls[i].slot), static_cast<unsigned>(calls[i].target),
                      static_cast<unsigned>(calls[i].type));
        return std::string(text);
    };
    return "server call " + std::to_string(at) + ": " + one(game, at) + " / " + one(ours, at);
}

// The values of a list, and for each place which of the old nodes it is
// (its index in `old`, or -1 for a new one).
std::vector<std::array<int64_t, 2>> Walk(const Node* node, const std::vector<const Node*>& old)
{
    std::vector<std::array<int64_t, 2>> out;
    for (; node; node = node->next) {
        int64_t index = -1;
        for (std::size_t i = 0; i < old.size(); ++i)
            if (old[i] == node)
                index = static_cast<int64_t>(i);
        out.push_back({static_cast<int64_t>(node->value), index});
        if (out.size() > 1000000)
            break;
    }
    return out;
}

std::vector<const Node*> Nodes(const Node* node)
{
    std::vector<const Node*> out;
    for (; node && out.size() < 1000000; node = node->next)
        out.push_back(node);
    return out;
}

// A list copied into new nodes from the game's allocator.
Node* CopyChain(const Node* node)
{
    Node* head = nullptr;
    Node** link = &head;
    for (; node; node = node->next) {
        Node* const copy = static_cast<Node*>(Allocate(8));
        copy->next = nullptr;
        copy->value = node->value;
        *link = copy;
        link = &copy->next;
    }
    return head;
}

void FreeChain(Node* node)
{
    while (node) {
        Node* const next = node->next;
        Free(node);
        node = next;
    }
}

std::string CompareLists(const char* what, std::size_t index, const Node* game, const Node* ours, const std::vector<const Node*>& oldGame,
                         const std::vector<const Node*>& oldOurs)
{
    const auto a = Walk(game, oldGame);
    const auto b = Walk(ours, oldOurs);
    for (std::size_t i = 0; i < a.size() || i < b.size(); ++i) {
        if (i < a.size() && i < b.size() && a[i] == b[i])
            continue;
        const auto describe = [](const std::vector<std::array<int64_t, 2>>& v, std::size_t i) {
            if (i >= v.size())
                return std::string("end");
            return Hex32(static_cast<uint32_t>(v[i][0])) + (v[i][1] < 0 ? std::string(" new") : " old " + std::to_string(v[i][1]));
        };
        return std::string(what) + " " + std::to_string(index) + " node " + std::to_string(i) + ": " + describe(a, i) + " / " + describe(b, i);
    }
    return {};
}

// The block of a vector at `at` (begin, end, capacity) by size and capacity.
std::string CompareVector(std::size_t at, std::size_t record, const char* game, const char* ours)
{
    for (std::size_t end : {kEnd, kCapacity}) {
        const auto a = static_cast<uint32_t>((At<char*>(const_cast<char*>(game), at + end) - At<char*>(const_cast<char*>(game), at)) / record);
        const auto b = static_cast<uint32_t>((At<char*>(const_cast<char*>(ours), at + end) - At<char*>(const_cast<char*>(ours), at)) / record);
        if (a != b) {
            char text[64];
            std::snprintf(text, sizeof text, "manager +0x%X: %u / %u", static_cast<unsigned>(at + end), a, b);
            return text;
        }
    }
    return {};
}

// The fields of two managers, the vectors' blocks by size and capacity
// (theirs differ); `blocks` also the render states' contents and every
// bucket list.
std::string CompareManagers(const char* game, const char* ours, bool blocks)
{
    static constexpr std::size_t kVectors[] = {kStates, kOverlays, kHandles, kGroups, kBuckets};
    static constexpr std::size_t kRecords[] = {kStateSize, kOverlaySize, kHandleSize, kGroupSize, 4};
    for (std::size_t at = 0; at < kSize; at += 4) {
        // The reference count: the message server's AddRef and Release of
        // the target change it on the game's side only.
        bool block = at == kBase + 4;
        for (std::size_t v : kVectors)
            block = block || (at >= v && at < v + 12);
        if (block)
            continue;
        const uint32_t a = At<uint32_t>(const_cast<char*>(game), at), b = At<uint32_t>(const_cast<char*>(ours), at);
        if (a != b) {
            char text[64];
            std::snprintf(text, sizeof text, "manager +0x%X: %08X / %08X", static_cast<unsigned>(at), static_cast<unsigned>(a),
                          static_cast<unsigned>(b));
            return text;
        }
    }
    for (int v = 0; v < 5; ++v) {
        std::string diff = CompareVector(kVectors[v], kRecords[v], game, ours);
        if (!diff.empty())
            return diff;
    }
    if (!blocks)
        return {};
    const char* const a = At<char*>(const_cast<char*>(game), kStates);
    const char* const b = At<char*>(const_cast<char*>(ours), kStates);
    const std::size_t states = At<char*>(const_cast<char*>(game), kStates + kEnd) - a;
    for (std::size_t at = 0; at < states; at += 4)
        if (std::memcmp(a + at, b + at, 4) != 0) {
            uint32_t x, y;
            std::memcpy(&x, a + at, 4);
            std::memcpy(&y, b + at, 4);
            return "render state +0x" + Hex32(static_cast<uint32_t>(at)) + ": " + Hex32(x) + " / " + Hex32(y);
        }
    Node** const lists = At<Node**>(const_cast<char*>(game), kBuckets);
    Node** const ourLists = At<Node**>(const_cast<char*>(ours), kBuckets);
    const std::size_t count = At<Node**>(const_cast<char*>(game), kBuckets + kEnd) - lists;
    for (std::size_t i = 0; i < count; ++i) {
        std::string diff = CompareLists("bucket", i, lists[i], ourLists[i], {}, {});
        if (!diff.empty())
            return diff;
    }
    return {};
}

void Own(char* record)
{
    if (void* const texture = At<void*>(record, kTexture))
        This<uint32_t>(addr::SC4TextureBinding_AddRef, texture);
    if (void* const source = At<void*>(record, kTextureSource))
        Virtual<uint32_t>(source, 0x4);
}

void AddRefField(char* self, std::size_t field)
{
    if (void* const object = At<void*>(self, field))
        Virtual<uint32_t>(object, 0x4);
}

// The reference counts of the textures a manager's copy holds (the shadow
// texture and the overlay records'), each once.
std::map<void*, int32_t> TextureCounts(char* self)
{
    std::map<void*, int32_t> counts;
    if (void* const texture = At<void*>(self, kShadowTexture))
        counts[texture] = At<int32_t>(texture, kTextureReferences);
    for (char* r = At<char*>(self, kOverlays); r && r != At<char*>(self, kOverlays + kEnd); r += kOverlaySize)
        if (void* const texture = At<void*>(r, kTexture))
            counts[texture] = At<int32_t>(texture, kTextureReferences);
    return counts;
}

std::string CompareCounts(const std::map<void*, int32_t>& before)
{
    for (const auto& [texture, count] : before) {
        const int32_t now = At<int32_t>(texture, kTextureReferences);
        if (now != count)
            return "texture " + Hex32(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(texture))) + " references "
                   + std::to_string(count) + " / " + std::to_string(now);
    }
    return {};
}

// A block of a vector copied into a new block from the game's allocator
// (ours may free it), the copy's fields pointed at it.
void CopyBlock(char* copy, const char* real, std::size_t vector)
{
    char* const begin = At<char*>(const_cast<char*>(real), vector);
    if (!begin)
        return;
    const std::size_t used = At<char*>(const_cast<char*>(real), vector + kEnd) - begin;
    const std::size_t capacity = At<char*>(const_cast<char*>(real), vector + kCapacity) - begin;
    char* const block = capacity ? static_cast<char*>(Allocate(capacity)) : nullptr;
    if (used)
        std::memcpy(block, begin, used);
    At<char*>(copy, vector) = block;
    At<char*>(copy, vector + kEnd) = block + used;
    At<char*>(copy, vector + kCapacity) = block + capacity;
}

void Record(const char* id, const std::string& diff)
{
    compare::Record(id, diff.empty(), diff, diff);
}

// ====================================================================================
// Compare mode: the manager
// ====================================================================================

char* CompareConstruct(char* self)
{
    alignas(8) char copy[kSize];
    std::memcpy(copy, self, kSize);
    g_side = Side::Ours;
    char* const mine = Construct(copy);
    g_side = Side::Game;
    char* const result = g_construct(self);
    g_side = Side::None;
    std::string diff;
    for (std::size_t at = 0; diff.empty() && at < kSize; at += 4)
        if (At<uint32_t>(self, at) != At<uint32_t>(copy, at)) {
            char text[64];
            std::snprintf(text, sizeof text, "manager +0x%X: %08X / %08X", static_cast<unsigned>(at),
                          static_cast<unsigned>(At<uint32_t>(self, at)), static_cast<unsigned>(At<uint32_t>(copy, at)));
            diff = text;
        }
    if (diff.empty() && result - self != mine - copy)
        diff = "result";
    Record("renderer.overlays.manage3.construct", diff);
    return result;
}

// Whether a manager has anything Init would replace (Init is compared only on
// a manager as the constructor left it).
bool Fresh(char* self)
{
    for (std::size_t at : {kView, kTerrain, kTextures, kShadowTexture, kStates, kBuckets})
        if (At<void*>(self, at))
            return false;
    return true;
}

void CompareInit(char* self, void* view, uint32_t waterRows, uint32_t waterVertices)
{
    if (!Fresh(self)) {
        static bool noted = false;
        if (!noted) {
            noted = true;
            log::Write("overlays manage3: Init on a manager already set up, not compared");
        }
        g_side = Side::Game;
        g_init(self, nullptr, view, waterRows, waterVertices);
        g_side = Side::None;
        return;
    }
    alignas(8) char copy[kSize];
    std::memcpy(copy, self, kSize);
    std::vector<ServerCall> gameCalls, ourCalls;
    {
        ServerSwap swap;
        swap.Record(copy, ourCalls, false);
        g_side = Side::Ours;
        Init(copy, nullptr, view, waterRows, waterVertices);
        swap.Record(self, gameCalls, true);
        g_side = Side::Game;
        g_init(self, nullptr, view, waterRows, waterVertices);
        g_side = Side::None;
    }
    std::string diff = CompareManagers(self, copy, true);
    if (diff.empty() && gameCalls != ourCalls)
        diff = DescribeCalls(gameCalls, ourCalls);
    Record("renderer.overlays.manage3.init", diff);
    g_side = Side::Game;
    g_destruct(copy);
    g_side = Side::None;
}

void CompareClear(char* self)
{
    alignas(8) char copy[kSize];
    std::memcpy(copy, self, kSize);
    for (std::size_t field : {kView, kTerrain, kTextures})
        AddRefField(copy, field);
    const std::map<void*, int32_t> before = TextureCounts(self);
    // The overlay records, with their own references (Clear destroys them).
    std::vector<char> overlays;
    if (char* const begin = At<char*>(self, kOverlays)) {
        const std::size_t used = At<char*>(self, kOverlays + kEnd) - begin;
        const std::size_t capacity = At<char*>(self, kOverlays + kCapacity) - begin;
        overlays.resize(capacity + 4);
        std::memcpy(overlays.data(), begin, used);
        for (std::size_t at = 0; at < used; at += kOverlaySize)
            Own(overlays.data() + at);
        At<char*>(copy, kOverlays) = overlays.data();
        At<char*>(copy, kOverlays + kEnd) = overlays.data() + used;
        At<char*>(copy, kOverlays + kCapacity) = overlays.data() + capacity;
    }
    std::vector<ServerCall> gameCalls, ourCalls;
    std::string counts;
    {
        ServerSwap swap;
        swap.Record(copy, ourCalls, false);
        g_side = Side::Ours;
        Clear(copy);
        counts = CompareCounts(before);
        swap.Record(self, gameCalls, true);
        g_side = Side::Game;
        g_clear(self);
        g_side = Side::None;
    }
    std::string diff = CompareManagers(self, copy, false);
    if (diff.empty() && gameCalls != ourCalls)
        diff = DescribeCalls(gameCalls, ourCalls);
    if (diff.empty())
        diff = counts;
    Record("renderer.overlays.manage3.clear", diff);
}

// The deep copy the deleting destructor's comparison destroys ours on, and
// what the game's body is compared with.
struct PendingDestruct {
    char* real;
    char* copy;
    std::string counts;
};
PendingDestruct* g_pending = nullptr;

char* CompareDeleting(char* self, uint32_t flags)
{
    alignas(8) char copy[kSize];
    std::memcpy(copy, self, kSize);
    const std::map<void*, int32_t> before = TextureCounts(self);
    CopyBlock(copy, self, kStates);
    CopyBlock(copy, self, kOverlays);
    CopyBlock(copy, self, kHandles);
    CopyBlock(copy, self, kGroups);
    CopyBlock(copy, self, kBuckets);
    for (char* r = At<char*>(copy, kOverlays); r && r != At<char*>(copy, kOverlays + kEnd); r += kOverlaySize)
        Own(r);
    for (Node** list = At<Node**>(copy, kBuckets); list && list != At<Node**>(copy, kBuckets + kEnd); ++list)
        *list = CopyChain(*list);
    if (void* const texture = At<void*>(copy, kShadowTexture))
        This<uint32_t>(addr::SC4TextureBinding_AddRef, texture);
    for (std::size_t field : {kView, kTerrain, kTextures})
        AddRefField(copy, field);
    g_side = Side::Ours;
    Destruct(copy);
    PendingDestruct pending{self, copy, CompareCounts(before)};
    g_pending = &pending;
    g_side = Side::Game;
    char* const result = g_deleting(self, nullptr, flags);
    g_side = Side::None;
    if (g_pending) {
        g_pending = nullptr;
        Record("renderer.overlays.manage3.destruct", "the game's destructor's body did not run");
    }
    LogCounts();
    return result;
}

// Inside the game's deleting destructor: its body, then the comparison
// before the memory is freed.
void FinishDestruct(char* self)
{
    PendingDestruct* const pending = g_pending;
    g_pending = nullptr;
    std::string diff = CompareManagers(self, pending->copy, false);
    if (diff.empty())
        diff = pending->counts;
    Record("renderer.overlays.manage3.destruct", diff);
}

// ====================================================================================
// Compare mode: the containers (the autotest's calls by address)
// ====================================================================================

Node** CompareAssignList(Node** list, Node* const* from)
{
    const std::vector<const Node*> oldGame = Nodes(*list);
    Node* copy = CopyChain(*list);
    const std::vector<const Node*> oldOurs = Nodes(copy);
    Node* const* const source = from == list ? &copy : from;
    g_side = Side::Ours;
    Node** const mine = AssignList(&copy, nullptr, source);
    g_side = Side::Game;
    Node** const result = g_assignList(list, nullptr, from);
    g_side = Side::None;
    std::string diff = CompareLists("list", 0, *list, copy, oldGame, oldOurs);
    if (diff.empty() && (result == list) != (mine == &copy))
        diff = "result";
    Record("renderer.overlays.manage3.assignlist", diff);
    FreeChain(copy);
    return result;
}

// A vector of lists copied: a block of the same capacity from the game's
// allocator and copies of its lists (the words past the end as they are).
struct ListVector {
    char fields[12] = {};
    std::vector<std::vector<const Node*>> old;

    explicit ListVector(const char* real)
    {
        Node** const begin = At<Node**>(const_cast<char*>(real), 0);
        const std::size_t size = At<Node**>(const_cast<char*>(real), kEnd) - begin;
        const std::size_t capacity = At<Node**>(const_cast<char*>(real), kCapacity) - begin;
        Node** const block = begin ? static_cast<Node**>(Allocate(capacity * 4)) : nullptr;
        if (capacity)
            std::memcpy(block, begin, capacity * 4);
        for (std::size_t i = 0; i < size; ++i) {
            block[i] = CopyChain(begin[i]);
            old.push_back(Nodes(block[i]));
        }
        At<Node**>(fields, 0) = block;
        At<Node**>(fields, kEnd) = block + size;
        At<Node**>(fields, kCapacity) = block + capacity;
    }
    ~ListVector()
    {
        for (Node** list = At<Node**>(fields, 0); list && list != At<Node**>(fields, kEnd); ++list)
            FreeChain(*list);
        if (Node** const block = At<Node**>(fields, 0))
            Free(block);
    }
    ListVector(const ListVector&) = delete;
    ListVector& operator=(const ListVector&) = delete;

    // A pointer into the real vector's block moved to the copy's.
    template <typename T>
    T* Map(const char* real, T* pointer) const
    {
        const char* const begin = At<char*>(const_cast<char*>(real), 0);
        const char* const end = At<char*>(const_cast<char*>(real), kCapacity);
        const char* const p = reinterpret_cast<const char*>(pointer);
        if (begin && p >= begin && p < end)
            return reinterpret_cast<T*>(At<char*>(const_cast<char*>(fields), 0) + (p - begin));
        return pointer;
    }
};

template <typename Ours, typename Game>
void CompareListVector(const char* id, char* vector, Ours&& ours, Game&& game)
{
    std::vector<std::vector<const Node*>> oldGame;
    {
        Node** const begin = At<Node**>(vector, 0);
        for (Node** list = begin; list && list != At<Node**>(vector, kEnd); ++list)
            oldGame.push_back(Nodes(*list));
    }
    Node** const gameBegin = At<Node**>(vector, 0);
    ListVector copy(vector);
    Node** const ourBegin = At<Node**>(copy.fields, 0);
    g_side = Side::Ours;
    ours(copy);
    g_side = Side::Game;
    game();
    g_side = Side::None;
    std::string diff;
    for (std::size_t end : {kEnd, kCapacity}) {
        const auto a = static_cast<uint32_t>(At<Node**>(vector, end) - At<Node**>(vector, 0));
        const auto b = static_cast<uint32_t>(At<Node**>(copy.fields, end) - At<Node**>(copy.fields, 0));
        if (diff.empty() && a != b)
            diff = std::string(end == kEnd ? "size " : "capacity ") + std::to_string(a) + " / " + std::to_string(b);
    }
    if (diff.empty() && (At<Node**>(vector, 0) != gameBegin) != (At<Node**>(copy.fields, 0) != ourBegin))
        diff = "new block";
    Node** const a = At<Node**>(vector, 0);
    Node** const b = At<Node**>(copy.fields, 0);
    const std::size_t size = At<Node**>(vector, kEnd) - a;
    // Lists still in their old places are compared with which old nodes they
    // reuse; the others (moved to a new block or a new place) by values.
    for (std::size_t i = 0; diff.empty() && i < size; ++i) {
        const bool kept = a == gameBegin && i < oldGame.size();
        diff = CompareLists("list", i, a[i], b[i], kept ? oldGame[i] : std::vector<const Node*>{},
                            kept && i < copy.old.size() ? copy.old[i] : std::vector<const Node*>{});
    }
    Record(id, diff);
}

void CompareDestroyOverlays(char* first, char* last, const void* tag)
{
    const std::size_t bytes = last > first ? static_cast<std::size_t>(last - first) : 0;
    std::vector<char> copy(bytes + 4);
    std::memcpy(copy.data(), first, bytes);
    std::map<void*, int32_t> textures;
    std::map<void*, uint32_t> sources;
    for (std::size_t at = 0; at + kOverlaySize <= bytes; at += kOverlaySize) {
        char* const r = copy.data() + at;
        if (void* const texture = At<void*>(r, kTexture))
            textures.emplace(texture, At<int32_t>(texture, kTextureReferences));
        Own(r);
    }
    for (std::size_t at = 0; at + kOverlaySize <= bytes; at += kOverlaySize)
        if (void* const source = At<void*>(copy.data() + at, kTextureSource))
            if (!sources.count(source)) {
                // The count as an AddRef gives it, with the copy's taken.
                sources[source] = Virtual<uint32_t>(source, 0x4);
                Virtual<uint32_t>(source, 0x8);
            }
    g_side = Side::Ours;
    DestroyOverlays(copy.data(), copy.data() + bytes, tag);
    g_side = Side::None;
    std::string diff = CompareCounts(textures);
    for (const auto& [source, count] : sources) {
        if (!diff.empty())
            break;
        // Without the copy's references the count is one less for each of
        // its records that holds the source.
        uint32_t held = 0;
        for (std::size_t at = 0; at + kOverlaySize <= bytes; at += kOverlaySize)
            held += At<void*>(copy.data() + at, kTextureSource) == source;
        const uint32_t now = Virtual<uint32_t>(source, 0x4);
        Virtual<uint32_t>(source, 0x8);
        if (now != count - held)
            diff = "source references " + std::to_string(count - held) + " / " + std::to_string(now);
    }
    g_side = Side::Game;
    g_destroyOverlays(first, last, tag);
    g_side = Side::None;
    Record("renderer.overlays.manage3.destroyoverlays", diff);
}

// ====================================================================================
// Detours
// ====================================================================================

Node* __fastcall DCutAfter(void* list, void*, Node** link, Node* last)
{
    Reached(kCutAfter);
    return RouteInner(kCutAfter, [&] { return CutAfter(list, nullptr, link, last); }, [&] { return g_cutAfter(list, nullptr, link, last); });
}

void __fastcall DCopyNodes(void* list, void*, Node** link, const Node* first, const Node* last, const void* tag)
{
    Reached(kCopyNodes);
    RouteInner(kCopyNodes, [&] { CopyNodes(list, nullptr, link, first, last, tag); },
               [&] { g_copyNodes(list, nullptr, link, first, last, tag); });
}

Node** __fastcall DAssignList(Node** list, void*, Node* const* from)
{
    Reached(kAssignList);
    return Route([&] { return AssignList(list, nullptr, from); }, [&] { return g_assignList(list, nullptr, from); },
                 [&] { return CompareAssignList(list, from); });
}

void __cdecl DDestroyOverlays(char* first, char* last, const void* tag)
{
    Reached(kDestroyOverlays);
    Route([&] { DestroyOverlays(first, last, tag); }, [&] { g_destroyOverlays(first, last, tag); },
          [&] { CompareDestroyOverlays(first, last, tag); });
}

void __fastcall DClearList(Node** list)
{
    Reached(kClearList);
    RouteInner(kClearList, [&] { ClearList(list); }, [&] { g_clearList(list); });
}

void __cdecl DFillLists(Node** first, Node** last, Node* const* from)
{
    Reached(kFillLists);
    RouteInner(kFillLists, [&] { FillLists(first, last, from); }, [&] { g_fillLists(first, last, from); });
}

Node** __cdecl DCopyLists(Node** first, Node** last, Node** to)
{
    Reached(kCopyLists);
    return RouteInner(kCopyLists, [&] { return CopyLists(first, last, to); }, [&] { return g_copyLists(first, last, to); });
}

Node** __cdecl DFillNLists(Node** to, uint32_t count, Node* const* from)
{
    Reached(kFillNLists);
    return RouteInner(kFillNLists, [&] { return FillNLists(to, count, from); }, [&] { return g_fillNLists(to, count, from); });
}

Node** __cdecl DCopyListsBackward(Node** first, Node** last, Node** toEnd)
{
    Reached(kCopyListsBackward);
    return RouteInner(kCopyListsBackward, [&] { return CopyListsBackward(first, last, toEnd); },
                      [&] { return g_copyListsBackward(first, last, toEnd); });
}

Node** __cdecl DCopyNewLists(Node** first, Node** last, Node** to)
{
    Reached(kCopyNewLists);
    return RouteInner(kCopyNewLists, [&] { return CopyNewLists(first, last, to); }, [&] { return g_copyNewLists(first, last, to); });
}

// Draws: ours in compare mode too.
bool __fastcall DViewDraw(char* view, void*, void* context)
{
    Reached(kViewDraw);
    if (g_side == Side::Game)
        return g_viewDraw(view, nullptr, context);
    return ViewDraw(view, nullptr, context);
}

void __cdecl DDestroyLists(Node** first, Node** last, const void* tag)
{
    Reached(kDestroyLists);
    RouteInner(kDestroyLists, [&] { DestroyLists(first, last, tag); }, [&] { g_destroyLists(first, last, tag); });
}

void __fastcall DClear(char* self)
{
    Reached(kClear);
    Route([&] { Clear(self); }, [&] { g_clear(self); },
          [&] {
              CompareClear(self);
              LogCounts();
          });
}

void __fastcall DResizeStates(char* vector, void*, uint32_t count)
{
    Reached(kResizeStates);
    RouteInner(kResizeStates, [&] { ResizeStates(vector, nullptr, count); }, [&] { g_resizeStates(vector, nullptr, count); });
}

void __fastcall DSetupStates(char* self)
{
    Reached(kSetupStates);
    RouteInner(kSetupStates, [&] { SetupStates(self); }, [&] { g_setupStates(self); });
}

void __fastcall DGrowBuckets(char* vector, void*, Node** at, Node* const* from, const void* tag, uint32_t count, uint32_t atEnd)
{
    Reached(kGrowBuckets);
    RouteInner(kGrowBuckets, [&] { GrowBuckets(vector, nullptr, at, from, tag, count, atEnd); },
               [&] { g_growBuckets(vector, nullptr, at, from, tag, count, atEnd); });
}

char* __fastcall DConstruct(char* self)
{
    Reached(kConstruct);
    return Route([&] { return Construct(self); }, [&] { return g_construct(self); }, [&] { return CompareConstruct(self); });
}

// The destructor's body: only the deleting destructor calls it. Inside that
// one's comparison, the game's runs and is compared before its memory is
// freed.
void __fastcall DDestruct(char* self)
{
    Reached(kDestruct);
    if (!g_compare || g_side == Side::Ours) {
        Destruct(self);
        return;
    }
    if (g_side == Side::None) {
        static bool noted = false;
        if (!noted) {
            noted = true;
            log::Write("overlays manage3: Destruct called outside a compared call, not compared");
        }
    }
    g_destruct(self);
    if (g_pending && g_pending->real == self)
        FinishDestruct(self);
}

void __fastcall DInsertLists(char* vector, void*, Node** at, uint32_t count, Node* const* from)
{
    Reached(kInsertLists);
    Route([&] { InsertLists(vector, nullptr, at, count, from); }, [&] { g_insertLists(vector, nullptr, at, count, from); },
          [&] {
              CompareListVector(
                  "renderer.overlays.manage3.insertlists", vector,
                  [&](ListVector& copy) { InsertLists(copy.fields, nullptr, copy.Map(vector, at), count, copy.Map(vector, from)); },
                  [&] { g_insertLists(vector, nullptr, at, count, from); });
          });
}

char* __fastcall DDeleting(char* self, void*, uint32_t flags)
{
    Reached(kDeletingDestruct);
    return Route([&] { return DeletingDestruct(self, nullptr, flags); }, [&] { return g_deleting(self, nullptr, flags); },
                 [&] { return CompareDeleting(self, flags); });
}

void __fastcall DResizeBuckets(char* vector, void*, uint32_t count, Node* const* from)
{
    Reached(kResizeBuckets);
    Route([&] { ResizeBuckets(vector, nullptr, count, from); }, [&] { g_resizeBuckets(vector, nullptr, count, from); },
          [&] {
              CompareListVector(
                  "renderer.overlays.manage3.resizebuckets", vector,
                  [&](ListVector& copy) { ResizeBuckets(copy.fields, nullptr, count, copy.Map(vector, from)); },
                  [&] { g_resizeBuckets(vector, nullptr, count, from); });
          });
}

void __fastcall DSetupBuckets(char* self)
{
    Reached(kSetupBuckets);
    RouteInner(kSetupBuckets, [&] { SetupBuckets(self); }, [&] { g_setupBuckets(self); });
}

void __fastcall DInit(char* self, void*, void* view, uint32_t waterRows, uint32_t waterVertices)
{
    Reached(kInit);
    Route([&] { Init(self, nullptr, view, waterRows, waterVertices); }, [&] { g_init(self, nullptr, view, waterRows, waterVertices); },
          [&] { CompareInit(self, view, waterRows, waterVertices); });
}

// ====================================================================================
// Installation
// ====================================================================================

struct Hooked {
    std::uintptr_t address;
    void* ours;
    void** original;
};

template <typename Fn>
Hooked H(std::uintptr_t address, Fn ours, Fn* original)
{
    return {address, reinterpret_cast<void*>(ours), reinterpret_cast<void**>(original)};
}

bool InstallManage3(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    const Hooked hooks[] = {
        H<CutAfterFn>(addr::cSC4OverlayManager_CutAfter, &DCutAfter, &g_cutAfter),
        H<CopyNodesFn>(addr::cSC4OverlayManager_CopyNodes, &DCopyNodes, &g_copyNodes),
        H<AssignListFn>(addr::cSC4OverlayManager_AssignList, &DAssignList, &g_assignList),
        H<DestroyOverlaysFn>(addr::cSC4OverlayManager_DestroyOverlays, &DDestroyOverlays, &g_destroyOverlays),
        H<ClearListFn>(addr::cSC4OverlayManager_ClearList, &DClearList, &g_clearList),
        H<FillListsFn>(addr::cSC4OverlayManager_FillLists, &DFillLists, &g_fillLists),
        H<CopyListsFn>(addr::cSC4OverlayManager_CopyLists, &DCopyLists, &g_copyLists),
        H<FillNListsFn>(addr::cSC4OverlayManager_FillNLists, &DFillNLists, &g_fillNLists),
        H<CopyListsFn>(addr::cSC4OverlayManager_CopyListsBackward, &DCopyListsBackward, &g_copyListsBackward),
        H<CopyListsFn>(addr::cSC4OverlayManager_CopyNewLists, &DCopyNewLists, &g_copyNewLists),
        H<ViewDrawFn>(addr::cSC4OverlayManager_ViewDraw, &DViewDraw, &g_viewDraw),
        H<DestroyListsFn>(addr::cSC4OverlayManager_DestroyLists, &DDestroyLists, &g_destroyLists),
        H<SelfFn>(addr::cSC4OverlayManager_Clear, &DClear, &g_clear),
        H<ResizeStatesFn>(addr::cSC4OverlayManager_ResizeStates, &DResizeStates, &g_resizeStates),
        H<SelfFn>(addr::cSC4OverlayManager_SetupStates, &DSetupStates, &g_setupStates),
        H<GrowBucketsFn>(addr::cSC4OverlayManager_GrowBuckets, &DGrowBuckets, &g_growBuckets),
        H<ConstructFn>(addr::cSC4OverlayManager_Construct, &DConstruct, &g_construct),
        H<SelfFn>(addr::cSC4OverlayManager_Destruct, &DDestruct, &g_destruct),
        H<InsertListsFn>(addr::cSC4OverlayManager_InsertLists, &DInsertLists, &g_insertLists),
        H<DeletingFn>(addr::cSC4OverlayManager_DeletingDestruct, &DDeleting, &g_deleting),
        H<ResizeBucketsFn>(addr::cSC4OverlayManager_ResizeBuckets, &DResizeBuckets, &g_resizeBuckets),
        H<SelfFn>(addr::cSC4OverlayManager_SetupBuckets, &DSetupBuckets, &g_setupBuckets),
        H<InitFn>(addr::cSC4OverlayManager_Init, &DInit, &g_init),
    };
    for (const Hooked& h : hooks)
        if (!addr_check::Find(h.address)) {
            log::Write("overlays manage3: no address entry for 0x%08X", static_cast<unsigned>(h.address));
            return false;
        }
    g_standInVtable[0x14 / 4] = reinterpret_cast<void*>(&StandInNotify);
    g_standInVtable[0x18 / 4] = reinterpret_cast<void*>(&StandInStop);
    g_compare = mode == registry::Mode::Compare;
    for (const Hooked& h : hooks)
        if (!hook::Install(*addr_check::Find(h.address), h.ours, h.original))
            return false;
    log::Write("overlays manage3: %u detours%s", static_cast<unsigned>(std::size(hooks)), g_compare ? ", compared with the game's" : "");
    return true;
}

const bool g_registered = registry::Register({
    "renderer.overlays.manage3",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_Init,
    "The overlay manager's bookkeeping, part 3: the set-up and the containers (the constructor, Init, Clear, the "
    "destructors, the render states, the bucket grid and its vector and list helpers, the records destroyed, the view "
    "draw; 23 detours; compare mode runs ours on a copy of the manager, list or vector and compares it with the game's; "
    "src/core/overlay_manage_3.cpp)",
    true,
    InstallManage3,
});

} // namespace
} // namespace opensc4::overlay_manage::part3
