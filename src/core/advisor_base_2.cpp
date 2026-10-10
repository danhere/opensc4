// The advisor base class, second part (0x0040E7D0 to 0x0041002C): the
// containers' out-of-line helpers, the advice items' step and the events,
// the advisor heads from the script, Shutdown, and the constructors and
// destructors of the two classes. Replacement, off by default:
//
//   scripts.advisors.base2: every function of the range.
//
// The object: the main interface at +0, the advisor interface at +8, the
// serialisable part at +0xC, the message target at +0x10. Its containers:
// the map of event type to item index at +0x6C (several per type), the map
// of active advice to item index at +0x78, the advice items at +0x84 (16
// bytes each), the head indices at +0x98 and +0xA4, the event list at +0xB0.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace opensc4::advisor_base {
namespace {

namespace field { // the object, from its start
constexpr std::size_t kNameA = 0x18;      // two localized strings
constexpr std::size_t kNameB = 0x38;
constexpr std::size_t kNow = 0x58;
constexpr std::size_t kTime = 0x5C;
constexpr std::size_t kCursor = 0x64;     // the next item to step
constexpr std::size_t kTriggers = 0x6C;   // map: event type to item index
constexpr std::size_t kActive = 0x78;     // map: advice to item index
constexpr std::size_t kItems = 0x84;      // vector of items
constexpr std::size_t kHead = 0x90;       // index into one of the head lists
constexpr std::size_t kHeadFirst = 0x94;  // which list: 1 the one at +0x98
constexpr std::size_t kHeadsA = 0x98;
constexpr std::size_t kHeadsB = 0xA4;
constexpr std::size_t kEvents = 0xB0;     // list of (type, object)
constexpr std::size_t kListening = 0xB4;
constexpr std::size_t kFirstStep = 0xB8;  // the derived class
constexpr std::size_t kListening2 = 0xB9;
} // namespace field

namespace item { // an advice item
constexpr std::size_t kAdvice = 0x0;
constexpr std::size_t kNext = 0x4;        // when it may trigger again
constexpr std::size_t kUntil = 0x8;       // when it times out
constexpr std::size_t kChecked = 0xC;
constexpr std::size_t kSize = 0x10;
} // namespace item

struct Node { // a tree node; the value starts at +0x10
    uint8_t color;
    uint8_t pad[3];
    Node* parent;
    Node* left;
    Node* right;
};

struct Tree {
    Node* header;
    uint32_t count;
};

struct Items {
    char* begin;
    char* end;
    char* capacity;
};

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

template <typename R, typename... Args>
R Call(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

void* Allocate(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void* MessageServer()
{
    return Global<void*>(addr::SC4_MessageServer);
}

void* System()
{
    return Global<void*>(addr::cSC4AdvisorSystem_Instance);
}

Node* Next(Node* x)
{
    if (x->right) {
        x = x->right;
        while (x->left)
            x = x->left;
        return x;
    }
    Node* y = x->parent;
    while (x == y->right) {
        x = y;
        y = y->parent;
    }
    if (x->right != y)
        x = y;
    return x;
}

void ResetTree(Tree* tree)
{
    tree->header->left = tree->header;
    tree->header->parent = nullptr;
    tree->header->right = tree->header;
    tree->count = 0;
}

// Empties a tree with the game's subtree eraser for its node type.
void ClearTree(Tree* tree, std::uintptr_t eraseSubtree)
{
    if (!tree->count)
        return;
    Call<void>(eraseSubtree, tree, tree->header->parent);
    ResetTree(tree);
}

void DestroyTree(Tree* tree, std::uintptr_t eraseSubtree)
{
    ClearTree(tree, eraseSubtree);
    if (Node* const header = tree->header)
        Free(header);
}

Node* EraseNode(Tree* tree, Node* node)
{
    Node* const header = tree->header;
    using RebalanceFn = Node*(__cdecl*)(Node*, Node**, Node**, Node**);
    return reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(node, &header->parent, &header->left,
                                                                         &header->right);
}

// The game's 0x0040DA10 as it is inlined in this range: a message of `type`
// from `source` (asked for its interface 1) with one value, queued or sent
// at once.
void Send(void* source, bool queued, uint32_t type, uint32_t data)
{
    void* from = nullptr;
    if (!source || Virtual<uint8_t>(source, 0x0, uint32_t{1}, &from)) {
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
        void* const message =
            memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        if (message)
            AddRef(message);
        Virtual<void>(message, 0x14, type);
        Virtual<void>(message, 0x84, from);
        Virtual<void>(message, 0x2C, data);
        if (queued)
            Virtual<void>(MessageServer(), 0x10, message, int32_t{0});
        else
            Virtual<void>(MessageServer(), 0xC, message);
        Release(message);
    }
    if (from)
        Release(from);
}

// 0x0040DA10 itself takes the source in ECX and three arguments the caller
// removes: (queued, type, data).
__declspec(naked) void SendThunk()
{
    __asm mov ecx, dword ptr [esp + 4]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm call dword ptr [esp + 32]
    __asm add esp, 12
    __asm ret
}

void SendFrom(void* source, uint32_t queued, uint32_t type, uint32_t data)
{
    using ThunkFn = void(__cdecl*)(void*, uint32_t, uint32_t, uint32_t, std::uintptr_t);
    reinterpret_cast<ThunkFn>(&SendThunk)(source, queued, type, data, addr::cSC4Advisor_F40DA10);
}

bool Flag(void* advice, int32_t flag)
{
    return Virtual<uint8_t>(advice, 0x3C, flag) != 0;
}

// --- The map of 12-byte values keyed by the first (unsigned) -----------------

// The game's 0x0040E960.
void __fastcall TripleDestroy(Tree* self, void*)
{
    DestroyTree(self, addr::cSC4Advisor_F40DAB0);
}

// The game's 0x0040FD10: the value of a key, added as zeros when missing.
uint32_t* __fastcall TripleAt(Tree* self, void*, const uint32_t* key)
{
    Node* const header = self->header;
    Node* y = header;
    for (Node* x = header->parent; x;) {
        if (At<uint32_t>(x, 0x10) < *key) {
            x = x->right;
        } else {
            y = x;
            x = x->left;
        }
    }
    if (y == header || *key < At<uint32_t>(y, 0x10)) {
        const uint32_t value[3] = {*key, 0, 0};
        Node* result[2] = {};
        Call<void>(addr::SC4Uint32TripleMap_InsertHint, self, &result[0], y, &value[0]);
        y = result[0];
    }
    return &At<uint32_t>(y, 0x14);
}

// --- The map of active advice (a counted reference) to item index ------------

Node* CloneNode(Node* from)
{
    Node* const node = static_cast<Node*>(Allocate(0x18));
    void* const advice = At<void*>(from, 0x10);
    At<void*>(node, 0x10) = advice;
    if (advice)
        AddRef(advice);
    At<uint32_t>(node, 0x14) = At<uint32_t>(from, 0x14);
    node->color = from->color;
    node->left = nullptr;
    node->right = nullptr;
    return node;
}

// The game's 0x0040EA20: a copy of the subtree at `x` under `parent`.
Node* __fastcall ActiveCopySubtree(Tree* self, void*, Node* x, Node* parent)
{
    Node* const top = CloneNode(x);
    top->parent = parent;
    if (x->right)
        top->right = ActiveCopySubtree(self, nullptr, x->right, top);
    Node* p = top;
    for (x = x->left; x; x = x->left) {
        Node* const y = CloneNode(x);
        p->left = y;
        y->parent = p;
        if (x->right)
            y->right = ActiveCopySubtree(self, nullptr, x->right, y);
        p = y;
    }
    return top;
}

// The copy of another tree's nodes into an empty one.
void ActiveCopyFrom(Tree* self, Tree* other, bool countWhenEmpty)
{
    Node* const root = other->header->parent;
    if (!root) {
        self->header->parent = nullptr;
        self->header->left = self->header;
        self->header->right = self->header;
        if (countWhenEmpty)
            self->count = other->count;
        return;
    }
    Node* const header = self->header;
    header->parent = ActiveCopySubtree(self, nullptr, root, header);
    Node* least = self->header->parent;
    while (least->left)
        least = least->left;
    self->header->left = least;
    Node* most = self->header->parent;
    while (most->right)
        most = most->right;
    self->header->right = most;
    self->count = other->count;
}

// The game's 0x0040F0C0: the copy constructor.
Tree* __fastcall ActiveCopyConstruct(Tree* self, void*, Tree* other)
{
    self->header = nullptr;
    Node* const header = static_cast<Node*>(Allocate(0x18));
    self->header = header;
    self->count = 0;
    header->color = 0;
    ActiveCopyFrom(self, other, true);
    return self;
}

// The game's 0x0040ECE0.
void __fastcall ActiveDestroy(Tree* self, void*)
{
    DestroyTree(self, addr::cSC4Advisor_F40E050);
}

// The game's 0x0040EFF0: the index stored for an item's advice, added as
// zero when missing. The order is by the advice's slot 7 value, then by
// address.
uint32_t* __fastcall ActiveAt(Tree* self, void*, void** key)
{
    Node* node = Call<Node*>(addr::cSC4Advisor_F40E000, self, key);
    bool missing = node == self->header;
    if (!missing) {
        const int32_t mine = Virtual<int32_t>(*key, 0x1C);
        const int32_t theirs = Virtual<int32_t>(At<void*>(node, 0x10), 0x1C);
        missing = mine != theirs ? mine < theirs
                                 : reinterpret_cast<std::uintptr_t>(*key) < At<std::uintptr_t>(node, 0x10);
    }
    if (missing) {
        void* const advice = *key;
        struct {
            void* advice;
            uint32_t index;
        } value;
        value.advice = advice;
        if (advice)
            AddRef(advice);
        value.index = 0;
        Node* result[2] = {};
        Call<void>(addr::cSC4Advisor_F40E4D0, self, &result[0], node, static_cast<void*>(&value));
        node = result[0];
        if (advice)
            Release(advice);
    }
    return &At<uint32_t>(node, 0x14);
}

// --- The vector of items -----------------------------------------------------

// The game's 0x0040F170.
void __fastcall ItemsDestroy(Items* self, void*)
{
    char* const end = self->end;
    for (char* at = self->begin; at != end; at += item::kSize) {
        if (void* const advice = At<void*>(at, item::kAdvice))
            Release(advice);
    }
    if (char* const begin = self->begin)
        Free(begin);
}

// The game's 0x0040FD90: `count` copies of an item at `where`, in a new
// buffer.
void __fastcall ItemsInsertOverflow(Items* self, void*, char* where, char* value, uint32_t, uint32_t count,
                                    uint32_t atEnd)
{
    using CopyFn = char*(__cdecl*)(char*, char*, char*, void*);
    using FillFn = char*(__cdecl*)(char*, uint32_t, char*, void*);
    const auto Copy = reinterpret_cast<CopyFn>(addr::cSC4Advisor_F40DDF0);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(self->end - self->begin) >> 4);
    const uint32_t length = (size < count ? count : size) + size;
    char* const buffer = length ? static_cast<char*>(Allocate(length << 4)) : nullptr;
    char* end = Copy(self->begin, where, buffer, &atEnd);
    if (count == 1) {
        if (end) {
            void* const advice = At<void*>(value, item::kAdvice);
            At<void*>(end, item::kAdvice) = advice;
            if (advice)
                AddRef(advice);
            At<uint32_t>(end, item::kNext) = At<uint32_t>(value, item::kNext);
            At<uint32_t>(end, item::kUntil) = At<uint32_t>(value, item::kUntil);
            At<uint32_t>(end, item::kChecked) = At<uint32_t>(value, item::kChecked);
        }
        end += item::kSize;
    } else {
        end = reinterpret_cast<FillFn>(addr::cSC4Advisor_F40DE40)(end, count, value, &atEnd);
    }
    if (!static_cast<uint8_t>(atEnd))
        end = Copy(where, self->end, end, &atEnd);
    ItemsDestroy(self, nullptr);
    self->begin = buffer;
    self->end = end;
    self->capacity = buffer + (length << 4);
}

// --- The advice items --------------------------------------------------------

// The game's 0x0040DF90 as it is inlined in the step: an item leaves the
// active ones.
void Expire(char* self, char* entry)
{
    Virtual<void>(At<void*>(entry, item::kAdvice), 0x38, int32_t{0x40A}, int32_t{0});
    if (Flag(At<void*>(entry, item::kAdvice), 0x4))
        At<int32_t>(entry, item::kNext) = 0x7FFFFFFF;
    else
        At<int32_t>(entry, item::kNext) =
            Virtual<int32_t>(At<void*>(entry, item::kAdvice), 0x40) + At<int32_t>(self, field::kTime);
    At<int32_t>(entry, item::kUntil) = 0;
    Virtual<void>(At<void*>(entry, item::kAdvice), 0x60, int32_t{0});
    Send(At<void*>(entry, item::kAdvice), false, 0x8A09FA65, 0);
}

// The game's 0x0040DF20 as it is inlined in the events: an item joins the
// active ones.
void Activate(char* self, char* entry)
{
    At<int32_t>(entry, item::kNext) = 0;
    At<int32_t>(entry, item::kUntil) =
        Virtual<int32_t>(At<void*>(entry, item::kAdvice), 0x44) + At<int32_t>(self, field::kTime);
    Virtual<void>(At<void*>(entry, item::kAdvice), 0x38, int32_t{0x2}, int32_t{1});
    Virtual<void>(At<void*>(entry, item::kAdvice), 0x38, int32_t{0x8}, int32_t{0});
    Send(At<void*>(entry, item::kAdvice), true, 0x8A09FA64, 0);
    const uint32_t data = Virtual<uint32_t>(At<void*>(entry, item::kAdvice), 0x6C);
    if (data)
        Send(self, true, 0x8A09FA6C, data);
}

// The game's 0x0040E9B0, on the interface at +8: takes an active advice out.
uint8_t __fastcall Retire(char* part, void*, void* advice)
{
    if (!advice || !Flag(advice, 0x2))
        return 0;
    void* key = advice;
    AddRef(advice);
    Tree* const active = reinterpret_cast<Tree*>(part - 0x8 + field::kActive);
    Node* const node = Call<Node*>(addr::cSC4Advisor_F40DE90, active, &key);
    Release(advice);
    char* const entry = At<char*>(part, field::kItems - 0x8) + (At<uint32_t>(node, 0x14) << 4);
    Call<void>(addr::cSC4Advisor_F40E440, active, node);
    Call<void>(addr::cSC4Advisor_F40DF90, part - 0x8, entry);
    return 1;
}

// The game's 0x0040F460: a thirtieth of the items (and one more) get their
// trigger checked, then the active ones their time and conditions.
void __fastcall StepItems(char* self, void*)
{
    const int32_t count = (At<int32_t>(self, field::kItems + 4) - At<int32_t>(self, field::kItems)) >> 4;
    if (!count)
        return;
    // 0x00A8122C is 0x3D088889, a thirtieth.
    const double share = static_cast<double>(count) * static_cast<double>(0x1.111112p-5f) + static_cast<double>(1.0f);
    Tree* const active = reinterpret_cast<Tree*>(self + field::kActive);
    for (int32_t left = static_cast<int32_t>(static_cast<int64_t>(share)); left != 0; --left) {
        if (At<int32_t>(self, field::kCursor) >= count)
            At<int32_t>(self, field::kCursor) = 0;
        char* const entry = At<char*>(self, field::kItems) + (At<uint32_t>(self, field::kCursor) << 4);
        if (!Flag(At<void*>(entry, item::kAdvice), 0x2) && !Flag(At<void*>(entry, item::kAdvice), 0x40) &&
            At<int32_t>(entry, item::kNext) <= At<int32_t>(self, field::kTime)) {
            const int32_t now = At<int32_t>(self, field::kNow);
            if (At<int32_t>(entry, item::kChecked) < now) {
                At<int32_t>(entry, item::kChecked) = now;
                if (Virtual<uint8_t>(At<void*>(entry, item::kAdvice), 0x20)) {
                    uint32_t* const index = ActiveAt(active, nullptr, reinterpret_cast<void**>(entry));
                    *index = At<uint32_t>(self, field::kCursor);
                    Call<void>(addr::cSC4Advisor_F40DF20, self, entry);
                }
            }
        }
        ++At<int32_t>(self, field::kCursor);
    }
    Node* node = active->header->left;
    if (node != active->header) {
        for (;;) {
            Node* const current = node;
            node = Next(node);
            char* const entry = At<char*>(self, field::kItems) + (At<uint32_t>(current, 0x14) << 4);
            const bool stays = (At<int32_t>(entry, item::kUntil) > At<int32_t>(self, field::kTime) ||
                                Flag(At<void*>(entry, item::kAdvice), 0x8000)) &&
                               !Flag(At<void*>(entry, item::kAdvice), 0x8) &&
                               (Flag(At<void*>(entry, item::kAdvice), 0x100) ||
                                Virtual<uint8_t>(At<void*>(entry, item::kAdvice), 0x20));
            if (stays) {
                if (Flag(At<void*>(entry, item::kAdvice), 0x10)) {
                    Virtual<void>(At<void*>(entry, item::kAdvice), 0x70);
                    SendFrom(At<void*>(entry, item::kAdvice), 0, 0x8A09FA66, 0);
                }
            } else {
                Node* const erased = EraseNode(active, current);
                if (void* const advice = At<void*>(erased, 0x10))
                    Release(advice);
                if (erased)
                    Free(erased);
                --active->count;
                Expire(self, entry);
            }
            if (node == active->header)
                break;
        }
    }
    Virtual<void>(self, 0x1C);
}

// The game's 0x0040FCE0: the derived class's step.
void __fastcall AdvisorStep(char* self, void*)
{
    if (At<uint8_t>(self, field::kFirstStep)) {
        Virtual<void>(self, 0x24);
        At<uint8_t>(self, field::kFirstStep) = 0;
    }
    StepItems(self, nullptr);
}

// --- The events --------------------------------------------------------------

// The game's 0x0040EEC0, on the interface at +8: queues an event.
uint8_t __fastcall PostEvent(char* part, void*, uint32_t type, void* object)
{
    if (object) {
        AddRef(object);
        AddRef(object);
    }
    char* const list = At<char*>(part, field::kEvents - 0x8);
    char* const node = static_cast<char*>(Allocate(0x10));
    At<uint32_t>(node, 0x8) = type;
    At<void*>(node, 0xC) = object;
    if (object)
        AddRef(object);
    char* const last = At<char*>(list, 0x4);
    At<char*>(node, 0x0) = list;
    At<char*>(node, 0x4) = last;
    At<char*>(last, 0x0) = node;
    At<char*>(list, 0x4) = node;
    if (object) {
        Release(object);
        Release(object);
    }
    return 1;
}

// The game's 0x0040F710: hands each queued event to the items its type
// triggers.
void __fastcall ProcessEvents(char* self, void*)
{
    char* const list = At<char*>(self, field::kEvents);
    int32_t left = 0;
    for (char* at = At<char*>(list, 0x0); at != list; at = At<char*>(at, 0x0))
        ++left;
    if (!left)
        return;
    Tree* const active = reinterpret_cast<Tree*>(self + field::kActive);
    do {
        char* const event = At<char*>(At<char*>(self, field::kEvents), 0x0) + 0x8;
        Node* lower = At<Node*>(self, field::kTriggers);
        for (Node* x = lower->parent; x;) {
            if (At<int32_t>(x, 0x10) < At<int32_t>(event, 0x0)) {
                x = x->right;
            } else {
                lower = x;
                x = x->left;
            }
        }
        Node* upper = At<Node*>(self, field::kTriggers);
        for (Node* x = upper->parent; x;) {
            if (At<int32_t>(event, 0x0) < At<int32_t>(x, 0x10)) {
                upper = x;
                x = x->left;
            } else {
                x = x->right;
            }
        }
        for (Node* at = lower; at != upper; at = Next(at)) {
            const uint32_t index = At<uint32_t>(at, 0x14);
            char* const entry = At<char*>(self, field::kItems) + (index << 4);
            if (!Flag(At<void*>(entry, item::kAdvice), 0x2)) {
                void* const advice = At<void*>(entry, item::kAdvice);
                At<int32_t>(entry, item::kChecked) = At<int32_t>(self, field::kNow);
                Virtual<void>(advice, 0x60, At<uint32_t>(event, 0x4));
                if (Virtual<uint8_t>(At<void*>(entry, item::kAdvice), 0x20)) {
                    *ActiveAt(active, nullptr, reinterpret_cast<void**>(entry)) = index;
                    Activate(self, entry);
                }
            } else {
                Virtual<void>(At<void*>(entry, item::kAdvice), 0x38, int32_t{0x10}, int32_t{1});
                if (Virtual<uint8_t>(At<void*>(entry, item::kAdvice), 0x20))
                    Virtual<void>(At<void*>(entry, item::kAdvice), 0x60, At<uint32_t>(event, 0x4));
            }
        }
        char* const first = At<char*>(At<char*>(self, field::kEvents), 0x0);
        char* const previous = At<char*>(first, 0x4);
        char* const next = At<char*>(first, 0x0);
        At<char*>(previous, 0x0) = next;
        At<char*>(next, 0x4) = previous;
        if (void* const object = At<void*>(first, 0xC))
            Release(object);
        Free(first);
    } while (--left);
}

// --- The heads from the script -----------------------------------------------

const char* HeadsTableName()
{
    return Global<const char*>(addr::cSC4Advisor_HeadsTableName);
}

// A field of the table on the script's stack, as an integer.
int32_t ReadField(void* script, const char* name)
{
    Virtual<void>(script, 0x84, name);
    Virtual<void>(script, 0x98, int32_t{-2});
    const int32_t value = Virtual<int32_t>(script, 0x58, int32_t{-1});
    Virtual<void>(script, 0x2C, int32_t{1});
    return value;
}

void PushIndex(char* vector, int32_t* index)
{
    int32_t*& end = At<int32_t*>(vector, 0x4);
    if (end != At<int32_t*>(vector, 0x8)) {
        if (end)
            *end = *index;
        end += 1;
        return;
    }
    uint8_t allocator = 0;
    using InsertFn = void(__thiscall*)(void*, int32_t*, int32_t*, void*, uint32_t, uint32_t);
    reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(vector, end, index, &allocator, 1, 1);
}

// The game's 0x0040F3A0, on the interface at +8: a value of the current
// head's script entry.
int32_t __fastcall HeadValue(char* part, void*, int32_t which)
{
    char* const self = part - 0x8;
    char* const heads = self + (At<uint8_t>(self, field::kHeadFirst) ? field::kHeadsA : field::kHeadsB);
    const int32_t head = At<int32_t>(self, field::kHead);
    if (head < 0)
        return 0;
    const uint32_t count = static_cast<uint32_t>((At<int32_t>(heads, 0x4) - At<int32_t>(heads, 0x0)) >> 2);
    if (static_cast<uint32_t>(head) >= count)
        return 0;
    void* const script = Virtual<void*>(System(), 0x28);
    const uint32_t entry = At<uint32_t*>(heads, 0x0)[At<int32_t>(self, field::kHead)];
    ScriptTable table;
    Call<void>(addr::SC4ScriptTable_Open, &table, script, HeadsTableName(), entry);
    if (table.open) {
        const int32_t value = Call<int32_t>(addr::cSC4Advisor_F40D330, &table, which);
        Virtual<void>(table.script, 0x1C, table.top);
        if (table.textBegin)
            Free(table.textBegin);
        return value;
    }
    if (table.textBegin)
        Free(table.textBegin);
    return 0;
}

// The game's 0x0040FA10: lists the script's heads of this advisor's type
// (those of gender 1 apart from the others) and keeps or picks the current
// one; true when the current one is still valid.
uint8_t __fastcall LoadHeads(char* self, void*)
{
    void* const script = Virtual<void*>(System(), 0x28);
    Virtual<void>(script, 0x94, HeadsTableName());
    const int32_t count = Virtual<int32_t>(script, 0x100, int32_t{-1});
    Virtual<void>(script, 0x2C, int32_t{1});
    char* const headsA = self + field::kHeadsA;
    char* const headsB = self + field::kHeadsB;
    At<char*>(headsA, 0x4) = At<char*>(headsA, 0x0);
    At<char*>(headsB, 0x4) = At<char*>(headsB, 0x0);
    for (int32_t index = 1; index <= count; ++index) {
        ScriptTable table;
        Call<void>(addr::SC4ScriptTable_Open, &table, script, HeadsTableName(), index);
        void* const opened = table.script;
        const int32_t type = table.open ? ReadField(opened, "advisor_type") : 0;
        if (type == Virtual<int32_t>(self + 0x8, 0x14)) {
            if (table.open && ReadField(opened, "advisor_gender") == 1)
                PushIndex(headsA, &index);
            else
                PushIndex(headsB, &index);
        }
        if (table.open)
            Virtual<void>(opened, 0x1C, table.top);
        table.textVtable = addr::SC4String_Vtable;
        if (table.textBegin)
            Free(table.textBegin);
    }
    const int32_t head = At<int32_t>(self, field::kHead);
    if (head >= 0) {
        char* const heads = At<uint8_t>(self, field::kHeadFirst) ? headsA : headsB;
        const uint32_t size = static_cast<uint32_t>((At<int32_t>(heads, 0x4) - At<int32_t>(heads, 0x0)) >> 2);
        if (static_cast<uint32_t>(head) < size)
            return 1;
        At<int32_t>(self, field::kHead) = -1;
    }
    if (At<int32_t>(self, field::kHead) >= 0)
        return 0;
    if ((At<uint32_t>(headsA, 0x4) - At<uint32_t>(headsA, 0x0)) & 0xFFFFFFFC) {
        At<uint8_t>(self, field::kHeadFirst) = 1;
        At<int32_t>(self, field::kHead) = 0;
        return 0;
    }
    if ((At<uint32_t>(headsB, 0x4) - At<uint32_t>(headsB, 0x0)) & 0xFFFFFFFC) {
        At<uint8_t>(self, field::kHeadFirst) = 0;
        At<int32_t>(self, field::kHead) = 0;
    }
    return 0;
}

// --- Shutdown ----------------------------------------------------------------

void StopListening(char* self, std::initializer_list<uint32_t> types)
{
    for (const uint32_t type : types)
        Virtual<uint8_t>(MessageServer(), 0x18, self + 0x10, type);
}

// The game's 0x0040ED30, on the interface at +8.
uint8_t __fastcall Shutdown(char* part, void*)
{
    char* const self = part - 0x8;
    if (At<uint8_t>(self, field::kListening)) {
        StopListening(self, {0xEA917750, 0x8A917759, 0x4A91DD6E, 0x4A92ECEF});
        At<uint8_t>(self, field::kListening) = 0;
    }
    At<uint32_t>(self, field::kTime) = 0;
    At<uint32_t>(self, field::kNow) = 0;
    ClearTree(reinterpret_cast<Tree*>(self + field::kActive), addr::cSC4Advisor_F40E050);
    SendFrom(self, 0, 0x8A09FA67, 0);
    void* const system = System();
    Tree* const triggers = reinterpret_cast<Tree*>(self + field::kTriggers);
    uint32_t last = 0;
    while (triggers->count) {
        const uint32_t type = At<uint32_t>(triggers->header->left, 0x10);
        if (last != type) {
            last = type;
            Virtual<void>(system, 0x44, type, part);
        }
        if (Node* const erased = EraseNode(triggers, triggers->header->left))
            Free(erased);
        --triggers->count;
    }
    Items* const items = reinterpret_cast<Items*>(self + field::kItems);
    while (items->begin != items->end) {
        Virtual<uint8_t>(At<void*>(items->end - item::kSize, item::kAdvice), 0x10);
        items->end -= item::kSize;
        if (void* const advice = At<void*>(items->end, item::kAdvice))
            Release(advice);
    }
    return 1;
}

// The game's 0x0040EF30, on the interface at +8: the derived class's.
uint8_t __fastcall AdvisorShutdown(char* part, void*)
{
    if (!Shutdown(part, nullptr))
        return 0;
    char* const self = part - 0x8;
    if (At<uint8_t>(self, field::kListening2)) {
        At<uint8_t>(self, field::kListening2) = 0;
        StopListening(self, {0x99EF1142, 0x99EF1143, 0x26D31EC1, 0xEA8AE29A, 0xC2C09554});
    }
    return 1;
}

// --- The objects -------------------------------------------------------------

void SetAgentVtables(char* self)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AgentBase_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::SC4AgentBase_Vtable8;
    At<std::uintptr_t>(self, 0xC) = addr::SC4AgentBase_VtableC;
    At<std::uintptr_t>(self, 0x10) = addr::SC4AgentBase_Vtable10;
}

void SetAdvisorVtables(char* self)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AdvisorBase_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::SC4AdvisorBase_Vtable8;
    At<std::uintptr_t>(self, 0xC) = addr::SC4AdvisorBase_VtableC;
    At<std::uintptr_t>(self, 0x10) = addr::SC4AdvisorBase_Vtable10;
}

void NewTree(Tree* tree)
{
    tree->header = nullptr;
    Node* const header = static_cast<Node*>(Allocate(0x18));
    tree->header = header;
    tree->count = 0;
    header->color = 0;
    tree->header->parent = nullptr;
    tree->header->left = tree->header;
    tree->header->right = tree->header;
}

// The game's 0x0040FEA0.
char* __fastcall Construct(char* self, void*)
{
    // The reference-counted base (0x0090DA1E).
    At<uint32_t>(self, 0x4) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::cRZBaseUnknown_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::SC4AgentBase_VtableBase8;
    At<std::uintptr_t>(self, 0xC) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, 0x10) = addr::cSC4TrafficSimulator_VtableBase4;
    SetAgentVtables(self);
    At<uint32_t>(self, 0x14) = 0;
    for (const std::size_t at : {field::kNameA, field::kNameB}) {
        Call<void*>(addr::SC4LocalizedString_ConstructBase, self + at);
        At<std::uintptr_t>(self, at) = addr::SC4LocalizedString_Vtable;
    }
    At<uint32_t>(self, field::kNow) = 0;
    At<uint32_t>(self, field::kTime) = 0;
    At<uint32_t>(self, 0x60) = 0;
    At<uint32_t>(self, field::kCursor) = 0;
    At<uint32_t>(self, 0x68) = 0;
    NewTree(reinterpret_cast<Tree*>(self + field::kTriggers));
    NewTree(reinterpret_cast<Tree*>(self + field::kActive));
    At<uint32_t>(self, field::kItems) = 0;
    At<uint32_t>(self, field::kItems + 4) = 0;
    At<uint32_t>(self, field::kItems + 8) = 0;
    At<int32_t>(self, field::kHead) = -1;
    At<uint8_t>(self, field::kHeadFirst) = 1;
    for (std::size_t at = field::kHeadsA; at < field::kEvents; at += 4)
        At<uint32_t>(self, at) = 0;
    At<char*>(self, field::kEvents) = nullptr;
    char* const list = static_cast<char*>(Allocate(0x10));
    At<char*>(list, 0x0) = list;
    At<char*>(list, 0x4) = list;
    At<char*>(self, field::kEvents) = list;
    At<uint8_t>(self, field::kListening) = 0;
    return self;
}

// The game's 0x0040F250.
void __fastcall Destruct(char* self, void*)
{
    SetAgentVtables(self);
    Call<void>(addr::SC4EventList_Clear, self + field::kEvents);
    if (void* const list = At<void*>(self, field::kEvents))
        Free(list);
    if (void* const heads = At<void*>(self, field::kHeadsB))
        Free(heads);
    if (void* const heads = At<void*>(self, field::kHeadsA))
        Free(heads);
    ItemsDestroy(reinterpret_cast<Items*>(self + field::kItems), nullptr);
    DestroyTree(reinterpret_cast<Tree*>(self + field::kActive), addr::cSC4Advisor_F40E050);
    DestroyTree(reinterpret_cast<Tree*>(self + field::kTriggers), addr::SC4Uint32Map_EraseSubtree2);
    for (const std::size_t at : {field::kNameB, field::kNameA}) {
        At<std::uintptr_t>(self, at) = addr::SC4String_Vtable;
        if (void* const text = At<void*>(self, at + 4))
            Free(text);
    }
    // The reference-counted base (0x0090D990).
    At<std::uintptr_t>(self, 0x0) = addr::cRZBaseUnknown_Vtable;
}

// The game's 0x0040FFD0: the deleting destructor.
char* __fastcall Delete(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

// The game's 0x0040FFF0: the derived class.
char* __fastcall AdvisorConstruct(char* self, void*)
{
    Construct(self, nullptr);
    At<uint8_t>(self, field::kListening2) = 0;
    At<uint32_t>(self, 0xBC) = 0;
    SetAdvisorVtables(self);
    At<uint8_t>(self, field::kFirstStep) = 1;
    return self;
}

// The game's 0x0040FCC0.
void __fastcall AdvisorDestruct(char* self, void*)
{
    SetAdvisorVtables(self);
    Destruct(self, nullptr);
}

// The game's 0x0040F370, 0x0040F380, 0x0040F390: QueryInterface on the
// parts at +8, +0xC and +0x10.
uint8_t __fastcall QueryInterface8(char* part, void*, uint32_t iid, void** out)
{
    return Call<uint8_t>(addr::SC4AgentBase_QueryInterface, part - 0x8, iid, out);
}

uint8_t __fastcall QueryInterfaceC(char* part, void*, uint32_t iid, void** out)
{
    return Call<uint8_t>(addr::SC4AgentBase_QueryInterface, part - 0xC, iid, out);
}

uint8_t __fastcall QueryInterface10(char* part, void*, uint32_t iid, void** out)
{
    return Call<uint8_t>(addr::SC4AgentBase_QueryInterface, part - 0x10, iid, out);
}

bool InstallBase2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Advisor_F40E960, reinterpret_cast<void*>(&TripleDestroy)},
        {addr::cSC4Advisor_F40E9B0, reinterpret_cast<void*>(&Retire)},
        {addr::cSC4Advisor_F40EA20, reinterpret_cast<void*>(&ActiveCopySubtree)},
        {addr::cSC4Advisor_F40ECE0, reinterpret_cast<void*>(&ActiveDestroy)},
        {addr::SC4AdvisorBase_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4Advisor_F40EEC0, reinterpret_cast<void*>(&PostEvent)},
        {addr::cSC4Advisor_F40EF30, reinterpret_cast<void*>(&AdvisorShutdown)},
        {addr::cSC4Advisor_F40EFF0, reinterpret_cast<void*>(&ActiveAt)},
        {addr::cSC4Advisor_F40F0C0, reinterpret_cast<void*>(&ActiveCopyConstruct)},
        {addr::cSC4Advisor_F40F170, reinterpret_cast<void*>(&ItemsDestroy)},
        {addr::SC4AgentBase_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4Advisor_F40F370, reinterpret_cast<void*>(&QueryInterface8)},
        {addr::cSC4Advisor_F40F380, reinterpret_cast<void*>(&QueryInterfaceC)},
        {addr::cSC4Advisor_F40F390, reinterpret_cast<void*>(&QueryInterface10)},
        {addr::cSC4Advisor_F40F3A0, reinterpret_cast<void*>(&HeadValue)},
        {addr::SC4AgentBase_StepItems, reinterpret_cast<void*>(&StepItems)},
        {addr::SC4AgentBase_ProcessEvents, reinterpret_cast<void*>(&ProcessEvents)},
        {addr::cSC4Advisor_F40FA10, reinterpret_cast<void*>(&LoadHeads)},
        {addr::cSC4Advisor_F40FCC0, reinterpret_cast<void*>(&AdvisorDestruct)},
        {addr::cSC4Advisor_F40FCE0, reinterpret_cast<void*>(&AdvisorStep)},
        {addr::cSC4Advisor_F40FD10, reinterpret_cast<void*>(&TripleAt)},
        {addr::cSC4Advisor_F40FD90, reinterpret_cast<void*>(&ItemsInsertOverflow)},
        {addr::SC4AgentBase_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4Advisor_F40FFD0, reinterpret_cast<void*>(&Delete)},
        {addr::cSC4Advisor_F40FFF0, reinterpret_cast<void*>(&AdvisorConstruct)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_base2Registered = registry::Register({
    "scripts.advisors.base2",
    "scripts/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4AgentBase_StepItems,
    "Our advisor base class, second part: containers, items' step, events, heads, Shutdown, objects (docs/lua.md)",
    false,
    InstallBase2,
});

} // namespace
} // namespace opensc4::advisor_base
