// The advisor window's controller (the object the advisor system keeps as its
// "helper", SC4AdvisorHelper in the address database), functions between
// 0x0041D400 and 0x0041F2D0. Replacement, off by default:
//
//   ui.advice_text.part1: the object (construction, destruction, the window
//       procedure's QueryInterface), loading the window's layout, the seven
//       advisor panes and their moods, showing and hiding, the list of the
//       chosen advisor's advices and the HTML rows built from it for the
//       list control.
//
// The object is 0x70 bytes: the window procedure's vtable at +0 and a message
// target's at +4. The rest of the class (the window procedure, the message
// handler, choosing an advisor) and the pane class are in the next part and
// are called by address. Window, layout and string helpers stay the game's.
// Not hooked: Release and its thunk (folded with other classes' code), the
// shown flag's getter (four bytes), and generic container and string code.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::ui {
namespace {

namespace field {
constexpr std::size_t kTarget = 0x4;       // the message target part
constexpr std::size_t kAdvisor = 0xC;      // the chosen advisor
constexpr std::size_t kAdvisorType = 0x10; // its type, -1 none
constexpr std::size_t kAdvisorIndex = 0x14; // its place in the type table, -1 none
constexpr std::size_t kWindow = 0x18;      // the advisor window (cIGZWinGen)
constexpr std::size_t kPaneWindows = 0x1C; // seven cIGZWinGen, one per advisor
constexpr std::size_t kPanes = 0x38;       // seven pane objects (window procedures)
constexpr std::size_t kListening = 0x54;   // byte: notifications are registered
constexpr std::size_t kShown = 0x56;       // byte
constexpr std::size_t kMode = 0x58;        // 0 overview, 1 advice list, 2 news list
constexpr std::size_t kList = 0x5C;        // the list control (interface 0xCA1492A2)
constexpr std::size_t kAdvices = 0x60;     // list of advices (sentinel node)
constexpr std::size_t kSorted = 0x64;      // the advisor's advices, a tree: header, count
} // namespace field

constexpr int32_t kPaneCount = 7;

constexpr uint32_t kWinProcIid = 0x22E85D8E;
constexpr uint32_t kMessageTargetIid = 0x452294AA;
constexpr uint32_t kWinGenIid = 0x5386D516;

// Windows of the layout.
constexpr uint32_t kOverviewWindow = 0x6A15C767;
constexpr uint32_t kAdviceWindow = 0xAA15EF06;
constexpr uint32_t kNewsWindow = 0x2A1D96B1;
constexpr uint32_t kListStyle = 0x14416264;

constexpr uint32_t kMessages[5] = {0x8A09FA67, 0x8A09FA65, 0x8A09FA64, 0x426841A1, 0xCA09FA6C};

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// A tree as the game's helpers fill it on the caller's stack.
struct Tree {
    char* header;
    uint32_t count;
    uint32_t spare[3];
};

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

void ReleaseAndClear(void*& slot)
{
    if (void* const object = slot) {
        slot = nullptr;
        Release(object);
    }
}

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

uint32_t Free(void* memory)
{
    return Cdecl<uint32_t>(addr::SC4_Free, memory);
}

void*& PaneWindow(char* self, int32_t index)
{
    return At<void*>(self, field::kPaneWindows + 4 * static_cast<std::size_t>(index));
}

void*& Pane(char* self, int32_t index)
{
    return At<void*>(self, field::kPanes + 4 * static_cast<std::size_t>(index));
}

// The advisor types in pane order (the game's table of seven).
uint32_t PaneType(int32_t index)
{
    return Global<uint32_t>(addr::SC4AdvisorHelper_AdvisorTypes + 4 * static_cast<std::uintptr_t>(index));
}

// The pane of an advisor type; 7 when the type has none.
int32_t PaneIndex(uint32_t type)
{
    int32_t index = 0;
    while (index < kPaneCount && PaneType(index) != type)
        ++index;
    return index;
}

// The window that holds the advisor window's parts.
void* ParentWindow(char* self)
{
    void* const window = Virtual<void*>(At<void*>(self, field::kWindow), 0xC); // AsIGZWin
    return Virtual<void*>(window, 0x2C);                                       // GetParentWin
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id); // GetChildWindowFromIDRecursive
}

void SetWindowFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<uint32_t>(window, 0x110, flag, on);
}

void ResetTree(char* tree, std::uintptr_t eraseSubtree)
{
    if (At<uint32_t>(tree, 0x4) == 0)
        return;
    This<void>(eraseSubtree, tree, At<char*>(At<char*>(tree, 0x0), 0x4));
    At<char*>(At<char*>(tree, 0x0), 0x8) = At<char*>(tree, 0x0);
    At<char*>(At<char*>(tree, 0x0), 0x4) = nullptr;
    At<char*>(At<char*>(tree, 0x0), 0xC) = At<char*>(tree, 0x0);
    At<uint32_t>(tree, 0x4) = 0;
}

char* NextNode(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* const left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

char* PreviousNode(char* node)
{
    if (At<uint8_t>(node, 0x0) == 0 && At<char*>(At<char*>(node, 0x4), 0x4) == node)
        return At<char*>(node, 0xC);
    if (char* left = At<char*>(node, 0x8)) {
        while (char* const right = At<char*>(left, 0xC))
            left = right;
        return left;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0x8)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    return parent;
}

// The game's 0x0041D400.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kWinProcIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid != kMessageTargetIid && iid != 1)
        return 0;
    *out = self ? self + field::kTarget : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x0041DB30: the message target's QueryInterface.
uint8_t __fastcall QueryInterface4(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - field::kTarget, nullptr, iid, out);
}

// The game's 0x0041D4D0: drops the pane of an advisor type.
void __fastcall DropPane(char* self, void*, uint32_t type)
{
    const int32_t index = PaneIndex(type);
    if (index >= kPaneCount)
        return;
    if (void* const window = PaneWindow(self, index))
        Virtual<void*>(window, 0x1C, static_cast<void*>(nullptr)); // SetWinProc
    ReleaseAndClear(Pane(self, index));
}

// The game's 0x0041D530: drops every pane.
void __fastcall DropPanes(char* self, void*)
{
    for (int32_t index = 0; index < kPaneCount; ++index) {
        if (!Pane(self, index))
            continue;
        if (void* const window = PaneWindow(self, index))
            Virtual<void*>(window, 0x1C, static_cast<void*>(nullptr));
        Virtual<uint32_t>(Pane(self, index), 0x18, uint32_t{0});
        ReleaseAndClear(Pane(self, index));
    }
}

// The game's 0x0041D580 (with the piece the list has at 0x0041D5F0): asks
// for, or stops, the advice notifications.
void __fastcall Listen(char* self, void*, uint8_t on)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (on) {
        if (At<uint8_t>(self, field::kListening))
            return;
        At<uint8_t>(self, field::kListening) = 1;
        if (!server)
            return;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x14, self + field::kTarget, message); // AddNotification
    } else {
        if (!At<uint8_t>(self, field::kListening))
            return;
        At<uint8_t>(self, field::kListening) = 0;
        if (!server)
            return;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x18, self + field::kTarget, message); // RemoveNotification
    }
}

// The tail of the game's Show and Hide: the chosen advisor's pane, or every
// pane when none is chosen, is told; the result is what the game leaves in
// EAX.
uint32_t SetPanesShown(char* self, uint32_t shown)
{
    uint32_t result = At<uint32_t>(self, field::kAdvisorIndex);
    if (result < static_cast<uint32_t>(kPaneCount)) {
        const int32_t index = PaneIndex(At<uint32_t>(self, field::kAdvisorType));
        result = static_cast<uint32_t>(index);
        if (index < kPaneCount) {
            void* const pane = Pane(self, index);
            result = pane ? Virtual<uint32_t>(pane, 0x18, shown) : 0;
        }
    } else {
        for (int32_t index = 0; index < kPaneCount; ++index) {
            if (void* const pane = Pane(self, index))
                result = Virtual<uint32_t>(pane, 0x18, shown);
        }
    }
    At<uint8_t>(self, field::kShown) = static_cast<uint8_t>(shown);
    return result;
}

// The game's 0x0041D650 (the list splits it at 0x0041D680 and 0x0041D6F0):
// shows the window of the current mode and enables the panes.
uint32_t __fastcall Show(char* self, void*)
{
    void* const parent = ParentWindow(self);
    void* child = nullptr;
    switch (At<uint32_t>(self, field::kMode)) {
    case 0:
        child = Child(parent, kOverviewWindow);
        break;
    case 1:
        child = Child(parent, kAdviceWindow);
        break;
    case 2:
        child = Child(parent, kNewsWindow);
        break;
    default:
        break;
    }
    if (child)
        SetWindowFlag(child, 1, 1);
    return SetPanesShown(self, 1);
}

// The game's 0x0041D710: hides the three windows and disables the panes.
uint32_t __fastcall Hide(char* self, void*)
{
    void* const parent = ParentWindow(self);
    for (const uint32_t id : {kOverviewWindow, kNewsWindow, kAdviceWindow}) {
        if (void* const child = Child(parent, id))
            SetWindowFlag(child, 1, 0);
    }
    return SetPanesShown(self, 0);
}

// The game's 0x0041D830: destroys the windows and lets go of everything.
uint32_t __fastcall Stop(char* self, void*)
{
    AddRef(self);
    if (void* const list = At<void*>(self, field::kList))
        Virtual<void>(list, 0x1C);
    DropPanes(self, nullptr);
    void* const manager = Virtual<void*>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x18);
    if (manager) {
        void* const parent = ParentWindow(self);
        for (const uint32_t id : {kOverviewWindow, kAdviceWindow, kNewsWindow})
            Virtual<uint8_t>(manager, 0x5C, Child(parent, id)); // DestroyWindow
        Virtual<uint8_t>(manager, 0x5C, Virtual<void*>(At<void*>(self, field::kWindow), 0xC));
    }
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (At<uint8_t>(self, field::kListening)) {
        At<uint8_t>(self, field::kListening) = 0;
        if (server) {
            for (const uint32_t message : kMessages)
                Virtual<uint8_t>(server, 0x18, self + field::kTarget, message);
        }
    }
    ReleaseAndClear(At<void*>(self, field::kAdvisor));
    ReleaseAndClear(At<void*>(self, field::kWindow));
    ReleaseAndClear(At<void*>(self, field::kList));
    for (int32_t index = 0; index < kPaneCount; ++index) {
        ReleaseAndClear(PaneWindow(self, index));
        ReleaseAndClear(Pane(self, index));
    }
    return Release(self);
}

// The game's 0x0041D9B0: takes an advice out of the list.
void __fastcall RemoveAdvice(char* self, void*, void* advice)
{
    ListNode* const end = At<ListNode*>(self, field::kAdvices);
    for (ListNode* node = end->next; node != end; node = node->next) {
        if (node->value != advice)
            continue;
        node->previous->next = node->next;
        node->next->previous = node->previous;
        if (node->value)
            Release(node->value);
        Free(node);
        return;
    }
}

void PushBack(char* self, void* advice)
{
    ListNode* const end = At<ListNode*>(self, field::kAdvices);
    ListNode* const node = static_cast<ListNode*>(Allocate(0xC));
    node->value = advice;
    if (advice)
        AddRef(advice);
    ListNode* const last = end->previous;
    node->next = end;
    node->previous = last;
    last->next = node;
    end->previous = node;
}

// The game's 0x0041DA00: adds an advice at the end of the list.
void __fastcall AddAdvice(char* self, void*, void* advice)
{
    if (advice)
        AddRef(advice);
    PushBack(self, advice);
    if (advice)
        Release(advice);
}

// The game's 0x0041DA60.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AdvisorHelper_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4AdvisorHelper_TargetVtable;
    ResetTree(self + field::kSorted, addr::cSC4Advisor_F40E050);
    if (char* const header = At<char*>(self, field::kSorted))
        Free(header);
    This<void>(addr::SC4RefList_Clear, self + field::kAdvices);
    if (void* const end = At<void*>(self, field::kAdvices))
        Free(end);
    if (void* const list = At<void*>(self, field::kList))
        Release(list);
    for (int32_t index = kPaneCount - 1; index >= 0; --index) {
        if (void* const pane = Pane(self, index))
            Release(pane);
    }
    for (int32_t index = kPaneCount - 1; index >= 0; --index) {
        if (void* const window = PaneWindow(self, index))
            Release(window);
    }
    if (void* const window = At<void*>(self, field::kWindow))
        Release(window);
    if (void* const advisor = At<void*>(self, field::kAdvisor))
        Release(advisor);
}

// The game's 0x0041DB50.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0x0) = addr::SC4AdvisorHelper_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4AdvisorHelper_TargetVtable;
    At<uint32_t>(self, 0x8) = 0;
    At<void*>(self, field::kAdvisor) = nullptr;
    At<int32_t>(self, field::kAdvisorType) = -1;
    At<int32_t>(self, field::kAdvisorIndex) = -1;
    At<void*>(self, field::kWindow) = nullptr;
    for (int32_t index = 0; index < kPaneCount; ++index)
        PaneWindow(self, index) = nullptr;
    for (int32_t index = 0; index < kPaneCount; ++index)
        Pane(self, index) = nullptr;
    At<uint8_t>(self, field::kListening) = 0;
    At<uint8_t>(self, 0x55) = 0;
    At<uint8_t>(self, field::kShown) = 0;
    At<uint32_t>(self, field::kMode) = 0;
    At<void*>(self, field::kList) = nullptr;
    At<void*>(self, field::kAdvices) = nullptr;
    ListNode* const end = static_cast<ListNode*>(Allocate(0xC));
    end->next = end;
    end->previous = end;
    At<ListNode*>(self, field::kAdvices) = end;
    At<char*>(self, field::kSorted) = nullptr;
    char* const header = static_cast<char*>(Allocate(0x18));
    At<char*>(self, field::kSorted) = header;
    At<uint32_t>(self, field::kSorted + 0x4) = 0;
    At<uint8_t>(header, 0x0) = 0;
    At<char*>(At<char*>(self, field::kSorted), 0x4) = nullptr;
    At<char*>(At<char*>(self, field::kSorted), 0x8) = At<char*>(self, field::kSorted);
    At<char*>(At<char*>(self, field::kSorted), 0xC) = At<char*>(self, field::kSorted);
    return self;
}

// The game's 0x0041DC00.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0041DC20 (the list has its end at 0x0041DCC0): the lowest
// mood among an advisor's current advices; 3 without an advisor or advices,
// 7 when no advice has a lower one.
int32_t __fastcall WorstMood(char*, void*, void* advisor)
{
    if (!advisor)
        return 3;
    Tree advices;
    This<void*>(addr::cSC4Advisor_F40F0C0, &advices, Virtual<void*>(advisor, 0x2C)); // GetCurrentAdvices
    if (advices.count == 0) {
        This<void>(addr::cSC4Advisor_F40ECE0, &advices);
        return 3;
    }
    char* const end = advices.header;
    int32_t worst = 7;
    char* node = At<char*>(end, 0x8);
    while (node != end) {
        char* const current = node;
        node = NextNode(node);
        const int32_t mood = Virtual<int32_t>(At<void*>(current, 0x10), 0x18); // GetMood
        if (mood < worst) {
            worst = mood;
            if (mood == 0)
                break;
        }
    }
    This<void>(addr::cSC4Advisor_F40ECE0, &advices);
    return worst;
}

// The pane's mood is its advisor's worst.
void UpdateMood(char* self, int32_t index, void* city)
{
    void* const system = Virtual<void*>(city, 0x144);                    // GetAdvisorSystem
    void* const advisor = Virtual<void*>(system, 0x1C, PaneType(index)); // GetAdvisor
    void* const pane = Pane(self, index);
    Virtual<void>(pane, 0x20, WorstMood(self, nullptr, advisor));
}

// Makes the pane of a place and ties it to the window `child`.
void MakePane(char* self, int32_t index, void* child)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xF8});
    void* const pane = memory ? This<void*>(addr::SC4UI_F420B70, memory, PaneType(index)) : nullptr;
    void* const old = Pane(self, index);
    if (pane != old) {
        if (pane)
            AddRef(pane);
        Pane(self, index) = pane;
        if (old)
            Release(old);
    }
    SetWindowFlag(child, 0x8000000, 1);
    void** const window = &PaneWindow(self, index);
    ReleaseAndClear(*window);
    Virtual<uint8_t>(child, 0x0, kWinGenIid, window);
    if (*window)
        Virtual<void*>(*window, 0x1C, Pane(self, index)); // SetWinProc
    Virtual<uint32_t>(Pane(self, index), 0x18, uint32_t{At<uint8_t>(self, field::kShown)});
    if (void* const city = Global<void*>(addr::SC4_City))
        UpdateMood(self, index, city);
}

// The game's 0x0041DCE0: makes the pane of an advisor type on the window
// with the given ID, unless it has one.
void __fastcall CreatePane(char* self, void*, uint32_t type, uint32_t windowId)
{
    const int32_t index = PaneIndex(type);
    if (index >= kPaneCount || Pane(self, index))
        return;
    MakePane(self, index, Child(ParentWindow(self), windowId));
}

// The game's 0x0041DE20: makes the missing panes on the overview's windows.
void __fastcall CreatePanes(char* self, void*)
{
    const uint32_t ids[kPaneCount] = {0x0A15C7A1, 0x6A15C7B5, 0x4A15C7C6, 0x0A15C7D8,
                                      0x6A15C7EA, 0xEA15C7FA, 0x8A15C80C};
    void* const parent = ParentWindow(self);
    for (int32_t index = 0; index < kPaneCount; ++index) {
        if (Pane(self, index))
            continue;
        MakePane(self, index, Child(parent, ids[index]));
    }
}

// The game's 0x0041DF90: gives every pane its advisor's worst mood.
void __fastcall UpdateMoods(char* self, void*)
{
    for (int32_t index = 0; index < kPaneCount; ++index) {
        if (Pane(self, index))
            UpdateMood(self, index, Global<void*>(addr::SC4_City));
    }
}

// The game's 0x0041E000: takes an advisor's current advices, last first, as
// the list; the advices of the old list that are not active lose flag 0x20.
void __fastcall SetAdvices(char* self, void*, void* advisor)
{
    ListNode* end = At<ListNode*>(self, field::kAdvices);
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const advice = node->value;
        if (!Virtual<uint8_t>(advice, 0x3C, uint32_t{2})) // CheckFlags
            Virtual<uint8_t>(advice, 0x38, uint32_t{0x20}, uint32_t{0}); // SetFlags
    }
    char* const sorted = self + field::kSorted;
    ResetTree(sorted, addr::cSC4Advisor_F40E050);
    This<void*>(addr::cSC4Advisor_F40EB00, sorted, Virtual<void*>(advisor, 0x28)); // GetCurrentAdvices

    end = At<ListNode*>(self, field::kAdvices);
    ListNode* node = end->next;
    if (node != end) {
        do {
            ListNode* const current = node;
            void* const advice = current->value;
            node = node->next;
            if (advice)
                Release(advice);
            Free(current);
        } while (node != At<ListNode*>(self, field::kAdvices));
    }
    At<ListNode*>(self, field::kAdvices)->next = At<ListNode*>(self, field::kAdvices);
    At<ListNode*>(self, field::kAdvices)->previous = At<ListNode*>(self, field::kAdvices);

    char* at = At<char*>(self, field::kSorted);
    if (at != At<char*>(at, 0x8)) {
        do {
            void* const advice = At<void*>(PreviousNode(at), 0x10);
            if (advice)
                AddRef(advice);
            PushBack(self, advice);
            if (advice)
                Release(advice);
            at = PreviousNode(at);
        } while (at != At<char*>(At<char*>(self, field::kSorted), 0x8));
    }
    UpdateMoods(self, nullptr);
}

// The game's 0x0041E1C0: back to the overview with no advisor chosen.
void __fastcall ShowOverview(char* self, void*)
{
    void* const parent = ParentWindow(self);
    if (void* const child = Child(parent, kAdviceWindow))
        SetWindowFlag(child, 1, 0);
    if (void* const child = Child(parent, kNewsWindow))
        SetWindowFlag(child, 1, 0);
    if (void* const child = Child(parent, kOverviewWindow))
        SetWindowFlag(child, 1, 1);
    DropPane(self, nullptr, At<uint32_t>(self, field::kAdvisorType));
    ReleaseAndClear(At<void*>(self, field::kAdvisor));
    At<int32_t>(self, field::kAdvisorIndex) = -1;
    At<int32_t>(self, field::kAdvisorType) = -1;
    CreatePanes(self, nullptr);
    At<uint32_t>(self, field::kMode) = 0;
}

// The game's 0x0041E320: loads the advisor window's layout into the main
// window, hides its three windows, becomes the window's procedure and makes
// the panes. Returns what the game leaves in EAX.
uint32_t __fastcall Start(char* self, void*)
{
    void* const mainWindow = Global<void*>(addr::SC4_Service43DC8);
    Tree before;
    before.header = nullptr;
    before.header = static_cast<char*>(Allocate(0x14));
    before.count = 0;
    At<uint8_t>(before.header, 0x0) = 0;
    At<char*>(before.header, 0x4) = nullptr;
    At<char*>(before.header, 0x8) = before.header;
    At<char*>(before.header, 0xC) = before.header;
    Cdecl<uint32_t>(addr::SC4UIWindows_Collect, mainWindow, &before);

    void** const window = &At<void*>(self, field::kWindow);
    uint32_t key[3] = {0, 0x96A006B0, 0xCBC905CD};
    ReleaseAndClear(*window);
    Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], mainWindow, kOverviewWindow, kWinGenIid, window);
    Cdecl<void>(addr::SC4UIWindows_NewSince, mainWindow, &before, &before);
    Cdecl<uint8_t>(addr::SC4UIWindows_Prepare, &before, uint32_t{0});

    void* const own = Virtual<void*>(*window, 0xC); // AsIGZWin
    void* child = Child(mainWindow, kOverviewWindow);
    Cdecl<void>(addr::SC4UIWindows_Unpaint, own);
    SetWindowFlag(child, 1, 0);
    Cdecl<void>(addr::SC4UIWindows_Unpaint, child);
    child = Child(mainWindow, kNewsWindow);
    SetWindowFlag(child, 1, 0);
    Cdecl<void>(addr::SC4UIWindows_Unpaint, child);
    child = Child(mainWindow, kAdviceWindow);
    SetWindowFlag(child, 1, 0);
    Cdecl<void>(addr::SC4UIWindows_Unpaint, child);
    Virtual<void*>(*window, 0x1C, self); // SetWinProc
    CreatePanes(self, nullptr);

    if (before.count != 0) {
        This<void>(addr::SC4UI_F41D7F0, &before, At<char*>(before.header, 0x4));
        At<char*>(before.header, 0x8) = before.header;
        At<char*>(before.header, 0x4) = nullptr;
        At<char*>(before.header, 0xC) = before.header;
        before.count = 0;
    }
    return before.header ? Free(before.header) : 0;
}

// The list's fixed pieces of HTML: 28 strings the game keeps as statics of
// its 0x0041E4B0, each 0x14 bytes below the one before, made on first use.
namespace piece {
constexpr int32_t kGrey = 0;
constexpr int32_t kBlue = 1;
constexpr int32_t kGreen = 2;
constexpr int32_t kRed = 3;
constexpr int32_t kBlinkRed = 4;
constexpr int32_t kTitleClose = 6;
constexpr int32_t kMessageClose = 7;
constexpr int32_t kBlinkClose = 8;
constexpr int32_t kTitleFace = 9;
constexpr int32_t kMessageBlue = 10;
constexpr int32_t kMessageGrey = 11;
constexpr int32_t kImages = 12; // four per mood: 0, 1, others, 5
constexpr int32_t kCount = 28;
} // namespace piece

GameString* Piece(int32_t index)
{
    const std::uintptr_t down = 0x14 * static_cast<std::uintptr_t>(index);
    return reinterpret_cast<GameString*>(addr::SC4AdvisorHelper_ListStrings - down);
}

void MakePieces()
{
    static const char* const texts[piece::kCount] = {
        "<FONT COLOR=\"#8e9499\"",
        "<FONT COLOR=\"#3f4967\"",
        "<FONT COLOR=\"#095a22\"",
        "<FONT COLOR=\"#b00000\"",
        "<BLINK><FONT COLOR=\"#f70a0b\"",
        "<FONT COLOR=\"#0f0f0f\"",
        "</B></FONT>",
        "</I></FONT>",
        "</B></FONT></BLINK>",
        " FACE=\"StocletITC TT\" SIZE=3><B>",
        "<FONT COLOR=\"#3f4967\" FACE=\"Arta\" SIZE=3><I>",
        "<FONT COLOR=\"#8e9499\" FACE=\"Arta\" SIZE=3><I>",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416250\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416251\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416252\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416253\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416254\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416255\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416256\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416257\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416258\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/14416259\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/1441625A\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/1441625B\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/1441625C\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/1441625D\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/1441625E\">",
        "<IMG SRC=\"sc4://HTML/46a006b0/1441625F\">",
    };
    uint32_t& made = Global<uint32_t>(addr::SC4AdvisorHelper_ListStringsGuard);
    for (int32_t index = 0; index < piece::kCount; ++index) {
        const uint32_t bit = uint32_t{1} << index;
        if (made & bit)
            continue;
        made |= bit;
        This<void*>(addr::SC4String_FromText, Piece(index), texts[index]);
        Cdecl<int32_t>(addr::SC4_atexit,
                       addr::SC4AdvisorHelper_ListStringAtExit - 0x20 * static_cast<std::uintptr_t>(index));
    }
}

void MakeEmpty(GameString& text)
{
    char* const memory = static_cast<char*>(Allocate(8));
    *memory = 0;
    text.body.begin = memory;
    text.body.end = memory;
    text.body.capacity = memory + 8;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void MakeCopy(GameString& text, const char* begin, const char* end, uint8_t* tag)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, &text.body, begin, end, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void Assign(GameString& text, int32_t index, uint8_t* tag)
{
    const StringBody& from = Piece(index)->body;
    This<void*>(addr::SC4String_AssignRange, &text.body, from.begin, from.end, tag);
}

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
}

// The game's 0x0041E4B0: fills the list control with a row per advice of the
// list: a title and a message in the colours of the advice's mood (grey when
// the advice is not active) and the mood's images.
void __fastcall FillList(char* self, void*)
{
    MakePieces();
    if (!At<void*>(self, field::kList))
        return;
    Virtual<void>(At<void*>(self, field::kList), 0x1C);
    Virtual<void>(At<void*>(self, field::kList), 0x18, kListStyle);

    ListNode* const end = At<ListNode*>(self, field::kAdvices);
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const advice = node->value;
        const int32_t mood = Virtual<int32_t>(advice, 0x18); // GetMood
        uint8_t tag = 0;
        GameString titleOpen;
        GameString titleClose;
        GameString images[4];
        GameString messageOpen;
        MakeEmpty(titleOpen);
        MakeCopy(titleClose, Piece(piece::kTitleClose)->body.begin, Piece(piece::kTitleClose)->body.end, &tag);
        MakeEmpty(images[0]);
        MakeEmpty(images[1]);
        MakeEmpty(images[2]);
        MakeEmpty(images[3]);
        MakeCopy(messageOpen, Piece(piece::kMessageBlue)->body.begin, Piece(piece::kMessageBlue)->body.end, &tag);

        int32_t firstImage;
        switch (mood) {
        case 0:
            Assign(titleClose, piece::kBlinkClose, &tag);
            Assign(titleOpen, piece::kBlinkRed, &tag);
            firstImage = piece::kImages;
            break;
        case 1:
            Assign(titleOpen, piece::kRed, &tag);
            firstImage = piece::kImages + 4;
            break;
        case 5:
            Assign(titleOpen, piece::kGreen, &tag);
            firstImage = piece::kImages + 12;
            break;
        default:
            Assign(titleOpen, piece::kBlue, &tag);
            firstImage = piece::kImages + 8;
            break;
        }
        for (int32_t image = 0; image < 4; ++image)
            Assign(images[image], firstImage + image, &tag);

        if (!Virtual<uint8_t>(advice, 0x3C, uint32_t{2})) { // CheckFlags
            Assign(titleOpen, piece::kGrey, &tag);
            for (int32_t image = 0; image < 4; ++image)
                Assign(images[image], piece::kImages + 8 + image, &tag);
            Assign(messageOpen, piece::kMessageGrey, &tag);
        }

        StringBody joined1;
        StringBody joined2;
        StringBody joined3;
        const char* text = Virtual<const char*>(Virtual<void*>(advice, 0x48), 0x14); // GetTextTitle, ToChar
        StringBody* whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined1, &titleOpen.body,
                                               &Piece(piece::kTitleFace)->body);
        whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined2, whole, text);
        whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined3, whole, &titleClose.body);
        GameString title;
        MakeCopy(title, whole->begin, whole->end, &tag);
        if (joined3.begin)
            Free(joined3.begin);
        if (joined2.begin)
            Free(joined2.begin);
        if (joined1.begin)
            Free(joined1.begin);

        text = Virtual<const char*>(Virtual<void*>(advice, 0x50), 0x14); // GetTextMessage, ToChar
        whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined1, &messageOpen.body, text);
        whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined2, whole, &Piece(piece::kMessageClose)->body);
        GameString message;
        MakeCopy(message, whole->begin, whole->end, &tag);
        if (joined2.begin)
            Free(joined2.begin);
        if (joined1.begin)
            Free(joined1.begin);

        if (mood == 7) {
            Virtual<void>(At<void*>(self, field::kList), 0x10, &images[0], &images[0], &message, &title, uint32_t{0},
                          advice, &images[3], uint32_t{0}, kListStyle);
        } else {
            // The game pushes the whole dword its byte result sits in; the
            // other three bytes are whatever the stack held.
            const uint32_t flagged = Virtual<uint8_t>(advice, 0x3C, uint32_t{0x20});
            Virtual<void>(At<void*>(self, field::kList), 0x10, &images[2], &images[1], &message, &title, flagged,
                          advice, &images[3], uint32_t{0}, kListStyle);
        }

        DestroyString(message);
        DestroyString(title);
        DestroyString(messageOpen);
        DestroyString(images[3]);
        DestroyString(images[2]);
        DestroyString(images[1]);
        DestroyString(images[0]);
        DestroyString(titleClose);
        DestroyString(titleOpen);
    }

    Virtual<void>(At<void*>(self, field::kList), 0x14, uint32_t{0});
    void* const parent = ParentWindow(self);
    SetWindowFlag(Child(parent, 0xAA1D969D), 2, 0);
    SetWindowFlag(Child(parent, 0x6A1D96C3), 2, 0);
}

bool InstallAdviceText1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F41D400, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4UI_F41D4D0, reinterpret_cast<void*>(&DropPane)},
        {addr::SC4UI_F41D530, reinterpret_cast<void*>(&DropPanes)},
        {addr::SC4UI_F41D580, reinterpret_cast<void*>(&Listen)},
        {addr::SC4AdvisorHelper_Call1, reinterpret_cast<void*>(&Show)},
        {addr::SC4AdvisorHelper_Call2, reinterpret_cast<void*>(&Hide)},
        {addr::SC4AdvisorHelper_Stop, reinterpret_cast<void*>(&Stop)},
        {addr::SC4UI_F41D9B0, reinterpret_cast<void*>(&RemoveAdvice)},
        {addr::SC4UI_F41DA00, reinterpret_cast<void*>(&AddAdvice)},
        {addr::SC4UI_F41DA60, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F41DB30, reinterpret_cast<void*>(&QueryInterface4)},
        {addr::SC4AdvisorHelper_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F41DC00, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4UI_F41DC20, reinterpret_cast<void*>(&WorstMood)},
        {addr::SC4UI_F41DCE0, reinterpret_cast<void*>(&CreatePane)},
        {addr::SC4UI_F41DE20, reinterpret_cast<void*>(&CreatePanes)},
        {addr::SC4UI_F41DF90, reinterpret_cast<void*>(&UpdateMoods)},
        {addr::SC4UI_F41E000, reinterpret_cast<void*>(&SetAdvices)},
        {addr::SC4UI_F41E1C0, reinterpret_cast<void*>(&ShowOverview)},
        {addr::SC4AdvisorHelper_Start, reinterpret_cast<void*>(&Start)},
        {addr::SC4UI_F41E4B0, reinterpret_cast<void*>(&FillList)},
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

const bool g_adviceText1Registered = registry::Register({
    "ui.advice_text.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4AdvisorHelper_Construct,
    "Our advisor window controller: panes, moods, the advice list and its HTML rows",
    false,
    InstallAdviceText1,
});

} // namespace
} // namespace opensc4::ui
