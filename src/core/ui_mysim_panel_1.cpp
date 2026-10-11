// The end of the MySim panel's code and the start of the news ticker's,
// functions between 0x00771440 and 0x00772E80. Replacement, off by default:
//
//   ui.mysim_panel.part1: the MySim panel's panes (the two advice lists, the
//       page of the chosen MySim with its name and portrait, choosing one of
//       the five places, deleting a MySim), its window procedure and its
//       message handler; the news ticker's start and stop, its notifications,
//       its window procedure, showing and hiding, the timer's callback and
//       its message handler.
//
// The panel is 0x7C bytes: the window procedure's vtable (0x00AB5114) at +0, a
// message target's (0x00AB5104) at +4 and a counted base (0x00AB50EC) at +8.
// Its code starts at 0x0076E6B0; what lies before 0x00771440 (construction,
// loading, the lists' rows, the move-in dialog) is in no part yet and is called
// by address. The news ticker is 0x84 bytes: a message target with its own
// methods (vtable 0x00AB5164) at +0 and the window procedure's (0x00AB5150) at
// +4; its constructor, its list of advices and its text are in the next part.
// Not hooked: the ticker's QueryInterface (0x007723F0), which another window
// class's vtable (0x00AB4FF4) shares.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

// Fields of the MySim panel.
namespace panel {
constexpr std::size_t kTarget = 0x4;    // the message target part
constexpr std::size_t kWindow = 0x10;   // the panel's window (cIGZWinGen)
constexpr std::size_t kList = 0x18;     // the advice list shown (interface 0xCA1492A2)
constexpr std::size_t kMySims = 0x1C;   // five cISC4MySim, one per place
constexpr std::size_t kClick = 0x34;    // an object whose slot 10 every button press calls
constexpr std::size_t kCurrent = 0x48;  // the chosen place, 0 to 4
constexpr std::size_t kCount = 0x4C;    // how many MySims the last count found
constexpr std::size_t kMode = 0x50;     // 0 the MySim's page, 1 an advice list, 2 the page of 0x0076F450
constexpr std::size_t kListKind = 0x54; // which advice list: 0 or 1
constexpr std::size_t kWatching = 0x58; // byte: follow the simulator's "changed" message
constexpr std::size_t kChoiceCount = 0x60;
constexpr std::size_t kPage = 0x64;
constexpr std::size_t kPageBase = 0x68;
constexpr std::size_t kChoice = 0x6C;
constexpr std::size_t kTab = 0x70;
} // namespace panel

// Fields of the news ticker.
namespace ticker {
constexpr std::size_t kWinProc = 0x4;    // the window procedure part
constexpr std::size_t kInited = 0xC;     // byte
constexpr std::size_t kListening = 0xD;  // byte: notifications are registered
constexpr std::size_t kTimer = 0x10;     // an embedded timer (vtable 0x00AD1F14, 0x28 bytes)
constexpr std::size_t kHistory = 0x38;   // the opened list (interface 0xCA1492A2)
constexpr std::size_t kLine = 0x3C;      // the scrolling line (interface 0xCA1492A2)
constexpr std::size_t kAdvices = 0x40;   // list of advices (sentinel node)
constexpr std::size_t kWindow = 0x44;    // the ticker's window (cIGZWinGen)
constexpr std::size_t kOpen = 0x49;      // byte: the list is open
constexpr std::size_t kRefs = 0x4C;      // three counted references
constexpr std::size_t kScroll = 0x5C;    // how far the line has scrolled, in pixels
constexpr std::size_t kLineArea = 0x64;  // the line's rectangle at rest, four dwords
constexpr std::size_t kPending = 0x78;
constexpr std::size_t kChanged = 0x7C;
constexpr std::size_t kLineHeight = 0x80;
} // namespace ticker

// Slots of the embedded timer.
namespace timer {
constexpr std::size_t kSetInterval = 0xC;
constexpr std::size_t kStart = 0x14;
constexpr std::size_t kStop = 0x1C;
constexpr std::size_t kIsRunning = 0x20;
constexpr std::size_t kSetCallback = 0x24;
} // namespace timer

constexpr std::size_t kMySimPart = 0xB8; // cISC4MySim inside the MySim object
constexpr int32_t kPlaces = 5;

constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kListIid = 0xCA1492A2;
constexpr uint32_t kAdviceIid = 0x8A09F5F5;
constexpr uint32_t kAdvisorIid = 0xCA09F544;
constexpr uint32_t kImageWinIid = 0xC12CEA13;

// A window message's type for a button press, and its second word.
constexpr uint32_t kCommand = 3;
constexpr uint32_t kCommandKind = 0x287259F6;

// Messages of the advisor system: an advice added, changed, an advisor
// removed, an advice removed.
constexpr uint32_t kAdviceAdded = 0x8A09FA64;
constexpr uint32_t kAdviceChanged = 0x8A09FA65;
constexpr uint32_t kAdvisorRemoved = 0x8A09FA67;
constexpr uint32_t kAdviceRemoved = 0xCA09FA6C;
constexpr uint32_t kTimerMessage = 0x0A344006;

// Panes of the panel.
constexpr uint32_t kPagePane = 0xCA1F1D9C;
constexpr uint32_t kFirstListPane = 0xEA1F1E4D;
constexpr uint32_t kSecondListPane = 0xAA1F1EC5;
constexpr uint32_t kThirdPane = 0xEA1F1E5E;
constexpr uint32_t kFourthPane = 0x6A61E29F;
constexpr uint32_t kNamePane = 0xABBAA2D3;

// The four texts that show the MySim's name.
constexpr uint32_t kNameTexts[4] = {0xAA1F1E6A, 0xAA1F1E7B, 0x8A1F1EFA, 0x8A1F1EFF};

// Windows of the ticker.
constexpr uint32_t kTickerList = 0xAA231508;
constexpr uint32_t kTickerLine = 0xCA2AEDC0;

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// An image by resource key or by file name (0x30 bytes, vtable 0x00A856CC).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The parent of the window kept at `field`.
void* ParentWindow(char* self, std::size_t field)
{
    void* const window = Virtual<void*>(At<void*>(self, field), 0xC); // AsIGZWin
    return Virtual<void*>(window, 0x2C);                              // GetParentWin
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id); // GetChildWindowFromIDRecursive
}

void SetWindowFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<uint32_t>(window, 0x110, flag, on);
}

void ShowChild(void* window, uint32_t id, uint32_t show)
{
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id, show, uint32_t{0});
}

void SetChildText(void* window, uint32_t id, GameString* text)
{
    Cdecl<uint8_t>(addr::SC4Win_SetChildText, window, id, text);
}

// Shows one of the panel's six panes and hides the rest (`shown` 0: all hidden).
void ShowPane(void* window, uint32_t shown)
{
    ShowChild(window, kPagePane, shown == kPagePane);
    ShowChild(window, kFirstListPane, shown == kFirstListPane);
    ShowChild(window, kSecondListPane, shown == kSecondListPane);
    ShowChild(window, kThirdPane, shown == kThirdPane);
    ShowChild(window, kFourthPane, shown == kFourthPane);
    ShowChild(window, kNamePane, shown == kNamePane);
}

char*& MySim(char* self, int32_t place)
{
    return At<char*>(self, panel::kMySims + 4 * static_cast<std::size_t>(place));
}

char* CurrentMySim(char* self)
{
    return MySim(self, At<int32_t>(self, panel::kCurrent));
}

// The MySim's advisor part (cISC4MySim slot 3).
void* Advisor(char* mysim)
{
    return Virtual<void*>(mysim, 0xC);
}

// The advice a message carries: its object asked for the advice interface.
uint8_t MessageAdvice(void* message, void** advice)
{
    void* const object = Virtual<void*>(message, 0x80); // GetIGZUnknown
    return Virtual<uint8_t>(object, 0x0, kAdviceIid, advice);
}

// The two advice lists share everything but the pane, the list control inside
// it and the kind they leave at +0x54.
void ShowAdviceList(char* self, uint32_t pane, uint32_t listId, uint32_t kind)
{
    void* const parent = ParentWindow(self, panel::kWindow);
    void* const again = ParentWindow(self, panel::kWindow);
    ShowPane(again, pane);
    if (void* const paneWindow = Child(parent, pane)) {
        void*& list = At<void*>(self, panel::kList);
        ReleaseAndClear(list);
        Virtual<uint8_t>(paneWindow, 0x94, listId, kListIid, &list); // GetChildAsRecursive
    }
    // The helper takes a counted reference by value and gives it back.
    void* const advisor = Advisor(CurrentMySim(self));
    if (advisor)
        AddRef(advisor);
    This<void>(addr::SC4UI_F76F260, self, advisor);
    This<void>(addr::SC4UI_F770740, self);
    At<uint32_t>(self, panel::kListKind) = kind;
    At<uint32_t>(self, panel::kMode) = 1;
}

// The game's 0x00771440. Shows the first advice list of the chosen MySim.
void __fastcall ShowFirstList(char* self, void*)
{
    ShowAdviceList(self, kFirstListPane, 0xAA1F1EB5, 0);
}

// The game's 0x00771550. Shows the second advice list.
void __fastcall ShowSecondList(char* self, void*)
{
    ShowAdviceList(self, kSecondListPane, 0x6A1F1F4A, 1);
}

// The game's 0x00771660. Fills the panel for the chosen MySim: its name in
// four places, the pane of the current mode, and its portrait from the
// resource or, with no bitmap ID, from its file. The argument is not read.
void __fastcall ShowMySim(char* self, void*, int32_t)
{
    void* const parent = ParentWindow(self, panel::kWindow);
    ShowChild(parent, 0xEA1F1E4E, 1);

    void* const text = Virtual<void*>(Advisor(CurrentMySim(self)), 0x20); // the name
    const uint32_t length = Virtual<uint32_t>(text, 0x1C);
    char* const chars = Virtual<char*>(text, 0x18);
    uint8_t allocator = 0;
    GameString name;
    name.begin = nullptr;
    name.end = nullptr;
    name.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &name.begin, chars, chars + length, &allocator);
    name.vtable = addr::SC4String_Vtable;
    name.zero = 0;
    for (const uint32_t id : kNameTexts)
        SetChildText(parent, id, &name);

    switch (At<int32_t>(self, panel::kMode)) {
    case 1:
        if (At<uint32_t>(self, panel::kListKind) == 0)
            ShowFirstList(self, nullptr);
        else
            ShowSecondList(self, nullptr);
        break;
    case 2:
        This<void>(addr::SC4UI_F76F450, self);
        break;
    default:
        ShowPane(ParentWindow(self, panel::kWindow), 0);
        break;
    }

    const uint32_t bitmap = Virtual<uint32_t>(CurrentMySim(self), 0x58); // GetBitmapID
    if (bitmap != 0) {
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, 0x46A006B0u, bitmap, uint32_t{1},
                    uint32_t{0});
        void* picture = nullptr;
        if (Virtual<uint8_t>(parent, 0x94, 0x22220055u, kImageWinIid, &picture))
            Virtual<void>(picture, 0x10, loader.image);
        if (picture)
            Release(picture);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    } else {
        GameString path;
        path.begin = nullptr;
        path.end = nullptr;
        path.capacity = nullptr;
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        path.begin = buffer;
        path.end = buffer;
        path.capacity = buffer + 8;
        *buffer = 0;
        path.vtable = addr::SC4String_Vtable;
        path.zero = 0;
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xF4, &path);
        // The file name's body; the game reads through it even when the
        // MySim gives no string.
        char* const file = Virtual<char*>(CurrentMySim(self), 0x5C); // GetBitmapFile
        char* const body = file ? file + 4 : nullptr;
        char* const end = At<char*>(body, 0x4);
        char* const begin = At<char*>(body, 0x0);
        This<void>(addr::SC4String_AppendRange, &path.begin, begin, end, &allocator);

        void* picture = nullptr;
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructName, &loader, &path, uint32_t{8}, uint32_t{0});
        ReleaseAndClear(picture);
        if (Virtual<uint8_t>(parent, 0x94, 0x22220055u, kImageWinIid, &picture))
            Virtual<void>(picture, 0x10, loader.image);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        if (picture)
            Release(picture);
        path.vtable = addr::SC4String_Vtable;
        if (path.begin)
            Free(path.begin);
    }

    name.vtable = addr::SC4String_Vtable;
    if (name.begin)
        Free(name.begin);
}

// The game's 0x00771940. Chooses a place: shows its MySim, or offers the
// move-in dialog for an empty one, tells the MySim simulator which place the
// panel shows and sets the two vehicle buttons' pictures.
void __fastcall SelectPlace(char* self, void*, int32_t place, uint8_t refresh, uint8_t offerMoveIn)
{
    if (place < 0 || place >= kPlaces)
        return;
    At<int32_t>(self, panel::kCurrent) = place;
    if (refresh && MySim(self, place))
        ShowMySim(self, nullptr, place);
    else if (offerMoveIn && !MySim(self, place))
        This<void>(addr::SC4UI_F76E830, self, place);

    void* const simulator = Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C);
    if (!simulator)
        return;
    const int32_t current = At<int32_t>(self, panel::kCurrent);
    char* const mysim = MySim(self, current);
    if (mysim) {
        Virtual<void>(simulator, 0x2C, current); // SetActiveUIPanelSim
        // GetModelButtonBitmapID(pedestrian), then the button's picture.
        const uint32_t walking = Virtual<uint32_t>(mysim, 0xEC, uint32_t{1});
        This<void>(addr::SC4UI_F76EE90, self, walking, uint32_t{1});
        const uint32_t driving = Virtual<uint32_t>(mysim, 0xEC, uint32_t{0});
        This<void>(addr::SC4UI_F76EE90, self, driving, uint32_t{0});
    } else {
        Virtual<void>(simulator, 0x2C, int32_t{-1});
    }
}

// The game's 0x007719F0. Back to the page pane: counts the MySims again and,
// when the chosen one is active, writes its name; when the count grew, chooses
// the place that was current before the count.
void __fastcall Recount(char* self, void*)
{
    void* const parent = ParentWindow(self, panel::kWindow);
    void* const again = ParentWindow(self, panel::kWindow);
    ShowPane(again, kPagePane);
    const int32_t place = At<int32_t>(self, panel::kCurrent);
    const uint32_t count = At<uint32_t>(self, panel::kCount);
    This<void>(addr::SC4UI_F76F7C0, self);
    char* const mysim = CurrentMySim(self);
    if (!mysim)
        return;
    if (!Virtual<uint8_t>(mysim, 0x50)) // IsActive
        return;
    GameString name;
    This<void*>(addr::SC4String_FromIGZString, &name, Virtual<void*>(Advisor(CurrentMySim(self)), 0x20));
    for (const uint32_t id : kNameTexts)
        SetChildText(parent, id, &name);
    if (At<uint32_t>(self, panel::kCount) > count)
        SelectPlace(self, nullptr, place, 1, 0);
    name.vtable = addr::SC4String_Vtable;
    if (name.begin)
        Free(name.begin);
}

// The game's 0x00771B40. Deletes the chosen MySim from the game.
void __fastcall DeleteMySim(char* self, void*)
{
    void* const parent = ParentWindow(self, panel::kWindow);
    SetWindowFlag(Child(parent, 0xEA1F1DBA), 2, 1);
    Virtual<uint8_t>(CurrentMySim(self), 0x24); // DeleteFromGame
    Recount(self, nullptr);
}

// A button of another window: the pick-one dialog the panel opens (0x00770420),
// with a row of choices, pages and three tabs.
uint8_t OtherWindowCommand(char* self, void* window, uint32_t* message)
{
    if (message[0] != kCommand || message[1] != kCommandKind)
        return 0;
    const uint32_t id = message[2];
    if (id >= 0x34560000 && id < 0x3456001C) {
        const uint32_t count = At<uint32_t>(self, panel::kChoiceCount);
        At<uint32_t>(self, panel::kChoice) = id;
        Cdecl<void>(addr::SC4Win_SelectChildInRange, window, 0x34560000u, count, id);
        return 1;
    }
    if (id >= 0xCC07EA2D && id < 0xCC07EA33) {
        At<uint32_t>(self, panel::kPage) = At<uint32_t>(self, panel::kPageBase) + (id - 0xCC07EA2D);
        This<void>(addr::SC4UI_F76FDB0, self, window);
        return 1;
    }
    switch (id) {
    case 0x2BF3269B:
    case 0xABF326DA:
        Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetWinManager), 0x24, uint32_t{0xF}, window, uint32_t{0});
        return 1;
    case 0x2C201CB0:
    case 0x2C201CB1:
    case 0x2C201CB2:
        At<uint32_t>(self, panel::kTab) = message[2] - 0x2C201CB0;
        Cdecl<void>(addr::SC4Win_SelectChildInRange, window, 0x2C201CB0u, uint32_t{3}, message[2]);
        This<void>(addr::SC4UI_F76FDB0, self, window);
        return 0;
    case 0x6BF326D1:
        Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetWinManager), 0x24, uint32_t{0xF}, window, uint32_t{1});
        return 1;
    case 0x6BFAFA68:
        --At<uint32_t>(self, panel::kPage);
        This<void>(addr::SC4UI_F76FDB0, self, window);
        return 1;
    case 0x6BFAFA6E:
        ++At<uint32_t>(self, panel::kPage);
        This<void>(addr::SC4UI_F76FDB0, self, window);
        return 1;
    default:
        return 0;
    }
}

// The game's 0x00771B90. The panel's window procedure (slot 3 of the +0
// vtable): the buttons of its own window, and those of another window.
uint8_t __fastcall DoWinProcMessage(char* self, void*, void* window, uint32_t* message)
{
    void* const own = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
    if (window != own)
        return OtherWindowCommand(self, window, message);

    if (message[0] == 0xE4A85D91) {
        void* const list = Virtual<void*>(At<void*>(self, panel::kList), 0xC);
        Virtual<uint8_t>(list, 0xC, message);
        return 0;
    }
    if (message[0] != kCommand || message[1] != kCommandKind)
        return 0;
    Virtual<void>(At<void*>(self, panel::kClick), 0x28);

    int32_t place = -1;
    switch (message[2]) {
    case 0x0AA1F2EA:
    case 0x0AA1F2EB:
    case 0x6A57DA68:
    case 0xEA628ABC:
        This<void>(addr::SC4UI_F76F450, self);
        return 0;
    case 0x2A1F1F24:
    case 0xAA1F1E85: {
        const int32_t current = At<int32_t>(self, panel::kCurrent);
        At<uint32_t>(self, panel::kMode) = 0;
        ShowMySim(self, nullptr, current);
        return 0;
    }
    case 0x2BBAAA68:
        This<void>(addr::SC4UI_F770420, self, uint32_t{0});
        return 0;
    case 0xEBBAAA70:
        This<void>(addr::SC4UI_F770420, self, uint32_t{1});
        return 0;
    case 0x2BBAAA79: {
        char* const mysim = CurrentMySim(self);
        This<uint8_t>(addr::cSC4MySim_MakeHomeTool, mysim ? mysim - kMySimPart : nullptr);
        return 0;
    }
    case 0x8BBAAA80: {
        char* const mysim = CurrentMySim(self);
        This<uint8_t>(addr::cSC4MySim_MakeJobTool, mysim ? mysim - kMySimPart : nullptr);
        return 0;
    }
    case 0x4BBAAA50: {
        // With no MySim the game calls through the +0xB8 part of a null
        // object and faults.
        char* const mysim = CurrentMySim(self);
        Virtual<uint8_t>(mysim ? mysim : reinterpret_cast<char*>(kMySimPart), 0x10C); // CreateDispatchTool
        return 0;
    }
    case 0x4BBA8AA1: {
        ShowPane(ParentWindow(self, panel::kWindow), kNamePane);
        GameString name;
        This<void*>(addr::SC4String_FromIGZString, &name, Virtual<void*>(Advisor(CurrentMySim(self)), 0x20));
        void* const window2 = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
        void* const parent = Virtual<void*>(window2, 0x2C);
        SetChildText(parent, 0x3A1F1EFB, &name);
        This<void>(addr::SC4String_Destroy, &name);
        return 0;
    }
    case 0x6BBAAA5D: {
        void* const window2 = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        Virtual<uint8_t>(manager, 0x94, Virtual<void*>(window2, 0x2C)); // GZSetFocus
        Virtual<uint8_t>(CurrentMySim(self), 0x104);                    // EnterUDriveIt
        return 0;
    }
    case 0x8A1F1E93:
    case 0xAA90B2C9:
        ShowSecondList(self, nullptr);
        return 0;
    case 0xAA1D9734:
    case 0xAA90B2CA:
        ShowFirstList(self, nullptr);
        return 0;
    case 0xEA628ABD:
    case 0xEA628ABE:
        if (At<uint32_t>(self, panel::kListKind) != 0)
            ShowSecondList(self, nullptr);
        else
            ShowFirstList(self, nullptr);
        return 0;
    case 0xAA1F1E96:
        Recount(self, nullptr);
        return 0;
    case 0xAA1F1EA3:
        This<void>(addr::SC4UI_F76E7B0, self);
        return 0;
    case 0xCA1F1E9B:
        Virtual<void>(CurrentMySim(self), 0x7C); // Find
        return 0;
    case 0xEA57DA69:
        DeleteMySim(self, nullptr);
        return 0;
    case 0x8A1F1DC3:
    case 0x8A8F410C:
    case 0xCA9DF384:
        place = 0;
        break;
    case 0x8A1F1DCC:
    case 0x8A8F410D:
    case 0xCA9DF383:
        place = 1;
        break;
    case 0xAA1F1DD6:
    case 0x8A8F410E:
    case 0xCA9DF382:
        place = 2;
        break;
    case 0x6A1F1DE0:
    case 0x8A8F410F:
    case 0xCA9DF381:
        place = 3;
        break;
    case 0x4A1F1DEA:
    case 0x8A8F4120:
    case 0xCA9DF380:
        place = 4;
        break;
    default:
        return 0;
    }
    SelectPlace(self, nullptr, place, 1, 1);
    return 0;
}

// The game's 0x007721A0. The panel's message handler (`target` is the +4
// part): the simulator's "changed" message, advices of the chosen MySim added
// and removed, and two messages turned into button presses.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - panel::kTarget;
    uint32_t press[4];
    switch (Virtual<uint32_t>(message, 0x10)) { // GetType
    case 0xAA25827C: {
        if (!At<uint8_t>(self, panel::kWatching))
            return 1;
        const int32_t place = At<int32_t>(self, panel::kCurrent);
        const uint32_t count = At<uint32_t>(self, panel::kCount);
        This<void>(addr::SC4UI_F76F7C0, self);
        if (At<uint32_t>(self, panel::kCount) > count)
            SelectPlace(self, nullptr, place, 1, 0);
        if (CurrentMySim(self))
            return 1;
        Recount(self, nullptr);
        return 1;
    }
    case kAdviceAdded: {
        void* advice = nullptr;
        MessageAdvice(message, &advice);
        if (advice) {
            if (char* const mysim = CurrentMySim(self)) {
                const uint32_t id = Virtual<uint32_t>(Advisor(mysim), 0x14);
                if (Virtual<uint32_t>(advice, 0x2C) == id)
                    This<void>(addr::SC4UI_F76EFB0, self, advice);
            }
        }
        This<void>(addr::SC4UI_F770740, self);
        if (advice)
            Release(advice);
        return 1;
    }
    case kAdviceChanged:
        This<void>(addr::SC4UI_F770740, self);
        return 1;
    case kAdviceRemoved: {
        char* const mysim = CurrentMySim(self);
        if (!mysim)
            return 1;
        if (Advisor(mysim)) {
            void* advice = nullptr;
            void* const object = Virtual<void*>(message, 0x80);
            void** const out = This<void**>(addr::SC4RefPtr_Reset, &advice);
            Virtual<uint8_t>(object, 0x0, kAdviceIid, out);
            if (advice) {
                if (char* const current = CurrentMySim(self)) {
                    const uint32_t id = Virtual<uint32_t>(Advisor(current), 0x14);
                    if (Virtual<uint32_t>(advice, 0x2C) == id)
                        This<void>(addr::SC4UI_F76EF40, self, advice);
                }
                if (advice)
                    Release(advice);
            }
        }
        This<void>(addr::SC4UI_F770740, self);
        return 1;
    }
    case 0xAC08746E:
        press[2] = 0x2BBAAA68;
        break;
    case 0xAC08746F:
        press[2] = 0xEBBAAA70;
        break;
    default:
        return 1;
    }
    press[0] = kCommand;
    press[1] = kCommandKind;
    press[3] = 0;
    void* const window = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
    Virtual<uint8_t>(self, 0xC, window, static_cast<uint32_t*>(press));
    return 1;
}

// The game's 0x00772440. The ticker's timer callback (cdecl, the ticker as
// context): posts the timer message to the ticker through the message server.
void __cdecl TickerTimer(void* context)
{
    void* server = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, 0x04FA845Bu, 0x652294C7u, &server);
    if (server) {
        void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
        void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
        AddRef(message);
        Virtual<void>(message, 0x14, kTimerMessage); // SetType
        Virtual<uint8_t>(server, 0x1C, message, context);
        Release(message);
    }
    This<void>(addr::SC4RefPtr_Destruct, &server);
}

// The game's 0x007724C0. Starts the ticker on its window: becomes the window
// procedure, finds the list and the scrolling line, notes the line's place
// and height and sets the timer's callback.
uint8_t __fastcall TickerInit(char* self, void*, void* window)
{
    if (At<uint8_t>(self, ticker::kInited))
        return 1;
    void* const old = At<void*>(self, ticker::kWindow);
    if (window != old) {
        if (window)
            AddRef(window);
        At<void*>(self, ticker::kWindow) = window;
        if (old)
            Release(old);
    }
    if (void* const kept = At<void*>(self, ticker::kWindow))
        Virtual<void>(kept, 0x1C, self + ticker::kWinProc);
    uint32_t area[4] = {0, 0, 0, 0};
    Virtual<void>(At<void*>(self, ticker::kWindow), 0x2C, static_cast<uint32_t*>(area));
    Virtual<void>(self, 0x18); // hide

    void* const own = Virtual<void*>(At<void*>(self, ticker::kWindow), 0xC);
    void* const parent = Virtual<void*>(own, 0x2C);
    Virtual<void>(Child(parent, 0xCA2AEDCC), 0x158, own);
    Virtual<void>(Child(parent, 0xCA2AEDCD), 0x158, own);
    Virtual<void>(Child(parent, 0x4A231510), 0x158, own);

    void* const listWindow = Child(parent, kTickerList);
    void*& history = At<void*>(self, ticker::kHistory);
    ReleaseAndClear(history);
    Virtual<uint8_t>(listWindow, 0x94, 0x6A231531u, kListIid, &history);

    void* const lineWindow = Child(parent, kTickerLine);
    void*& line = At<void*>(self, ticker::kLine);
    ReleaseAndClear(line);
    Virtual<uint8_t>(lineWindow, 0x94, 0xAA12F33Cu, kListIid, &line);
    Virtual<void>(line, 0x28);
    Virtual<void>(line, 0x20, uint32_t{0}, uint32_t{0}, uint32_t{0}, uint32_t{0});
    const uint32_t* const lineArea = Virtual<uint32_t*>(Virtual<void*>(line, 0xC), 0xC0);
    for (std::size_t i = 0; i < 4; ++i)
        At<uint32_t>(self, ticker::kLineArea + 4 * i) = lineArea[i];

    void* generic = nullptr;
    void* const listAgain = Child(parent, kTickerList);
    ReleaseAndClear(generic);
    if (Virtual<uint8_t>(listAgain, 0x0, kWinGenIid, &generic)) {
        area[0] = 0;
        area[1] = 0;
        area[2] = 0;
        area[3] = 0;
        Virtual<void>(generic, 0x2C, static_cast<uint32_t*>(area));
    }

    Virtual<uint8_t>(self + ticker::kTimer, timer::kSetCallback, addr::SC4UI_F772440, self);
    void* const font = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, 0xAA0F4AB4u);
    const uint32_t height = Virtual<uint32_t>(font, 0x8C);
    At<uint32_t>(self, ticker::kLineHeight) = height;
    At<uint32_t>(self, ticker::kLineArea + 0xC) = height * 3 + At<uint32_t>(self, ticker::kLineArea + 0x4);

    if (void* const frame = Child(parent, 0xCA2AEEC0)) {
        const uint32_t width = Virtual<uint32_t>(frame, 0xA4);
        const uint32_t twoLines = At<uint32_t>(self, ticker::kLineHeight) << 1;
        const uint32_t frameHeight = Virtual<uint32_t>(frame, 0xA8);
        Virtual<void>(frame, 0xD4, width, twoLines < frameHeight ? twoLines : frameHeight);
    }
    At<uint8_t>(self, ticker::kInited) = 1;
    if (generic)
        Release(generic);
    return 1;
}

// The game's 0x00772760. Registers for the advisor system's four messages or
// drops them.
void __fastcall TickerListen(char* self, void*, uint8_t on)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    const uint8_t listening = At<uint8_t>(self, ticker::kListening);
    if (on) {
        if (listening)
            return;
        At<uint8_t>(self, ticker::kListening) = 1;
        if (!server)
            return;
        Virtual<uint8_t>(server, 0x14, self, kAdviceChanged);
        Virtual<uint8_t>(server, 0x14, self, kAdvisorRemoved);
        Virtual<uint8_t>(server, 0x14, self, kAdviceAdded);
        Virtual<uint8_t>(server, 0x14, self, kAdviceRemoved);
    } else {
        if (!listening)
            return;
        At<uint8_t>(self, ticker::kListening) = 0;
        if (!server)
            return;
        Virtual<uint8_t>(server, 0x18, self, kAdviceChanged);
        Virtual<uint8_t>(server, 0x18, self, kAdvisorRemoved);
        Virtual<uint8_t>(server, 0x18, self, kAdviceAdded);
        Virtual<uint8_t>(server, 0x18, self, kAdviceRemoved);
    }
}

// The game's 0x00772810. The ticker's window procedure (`proc` is the +4
// part): listens while its window exists, and opens and closes the list.
uint8_t __fastcall TickerDoWinProcMessage(char* proc, void*, void* window, uint32_t* message)
{
    char* const self = proc - ticker::kWinProc;
    if (window != Virtual<void*>(At<void*>(self, ticker::kWindow), 0xC))
        return 0;
    const uint32_t type = message[0];
    if (type != kCommand) {
        if (type == 0xA2BF8AD5)
            TickerListen(self, nullptr, 1);
        else if (type == 0xA2BF8AD6)
            TickerListen(self, nullptr, 0);
        return 0;
    }
    if (message[1] != kCommandKind)
        return 0;
    void* const list = Child(ParentWindow(self, ticker::kWindow), kTickerList);
    const uint32_t id = message[2];
    if (id == 0x4A231510) {
        SetWindowFlag(list, 1, 0);
        At<uint8_t>(self, ticker::kOpen) = 0;
        return 0;
    }
    if (id <= 0xCA2AEDCB || id > 0xCA2AEDCD)
        return 0;
    if (At<uint8_t>(self, ticker::kOpen)) {
        SetWindowFlag(list, 1, 0);
    } else {
        SetWindowFlag(list, 1, 1);
        Virtual<void>(self, 0x2C);
        Virtual<void>(self, 0x24);
    }
    At<uint8_t>(self, ticker::kOpen) = At<uint8_t>(self, ticker::kOpen) == 0;
    return 0;
}

// Brings a window to the front with its flag 0x800 set for the call.
void PullToFront(void* window)
{
    SetWindowFlag(window, 0x800, 1);
    Virtual<uint8_t>(window, 0x5C);
    SetWindowFlag(window, 0x800, 0);
}

// The game's 0x00772920. Shows the ticker (slot 5): the line in front and
// visible, the list too when open, and the timer started.
void __fastcall TickerShow(char* self, void*)
{
    void* const parent = ParentWindow(self, ticker::kWindow);
    void* const line = Child(parent, kTickerLine);
    PullToFront(line);
    SetWindowFlag(line, 1, 1);
    void* const list = Child(parent, kTickerList);
    PullToFront(list);
    if (At<uint8_t>(self, ticker::kOpen)) {
        SetWindowFlag(list, 1, 1);
        Virtual<void>(self, 0x24);
    }
    char* const clock = self + ticker::kTimer;
    if (!Virtual<uint8_t>(clock, timer::kIsRunning))
        Virtual<uint8_t>(clock, timer::kStart, uint32_t{3000}, self + ticker::kWinProc);
}

// The game's 0x00772A00. Hides the ticker (slot 6) and stops the timer.
void __fastcall TickerHide(char* self, void*)
{
    void* const parent = ParentWindow(self, ticker::kWindow);
    SetWindowFlag(Child(parent, kTickerList), 1, 0);
    SetWindowFlag(Child(parent, kTickerLine), 1, 0);
    char* const clock = self + ticker::kTimer;
    if (Virtual<uint8_t>(clock, timer::kIsRunning))
        Virtual<uint8_t>(clock, timer::kStop);
}

// The game's 0x00772A70. Stops the ticker: the timer, the two controls, the
// advices, the references, the window and the notifications.
uint8_t __fastcall TickerShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, ticker::kInited))
        return 1;
    At<uint8_t>(self, ticker::kInited) = 0;
    Virtual<uint8_t>(self + ticker::kTimer, timer::kStop);
    Virtual<void>(At<void*>(self, ticker::kHistory), 0x1C);
    Virtual<void>(At<void*>(self, ticker::kLine), 0x1C);
    This<void>(addr::SC4RefList_Clear, self + ticker::kAdvices);
    ReleaseAndClear(At<void*>(self, ticker::kHistory));
    ReleaseAndClear(At<void*>(self, ticker::kLine));
    for (std::size_t i = 0; i < 3; ++i)
        ReleaseAndClear(At<void*>(self, ticker::kRefs + 4 * i));
    Virtual<void>(At<void*>(self, ticker::kWindow), 0x1C, uint32_t{0});
    Virtual<void>(Virtual<void*>(At<void*>(self, ticker::kWindow), 0xC), 0x18);
    ReleaseAndClear(At<void*>(self, ticker::kWindow));
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (At<uint8_t>(self, ticker::kListening)) {
        At<uint8_t>(self, ticker::kListening) = 0;
        if (server) {
            Virtual<uint8_t>(server, 0x18, self, kAdviceChanged);
            Virtual<uint8_t>(server, 0x18, self, kAdvisorRemoved);
            Virtual<uint8_t>(server, 0x18, self, kAdviceAdded);
            Virtual<uint8_t>(server, 0x18, self, kAdviceRemoved);
        }
    }
    return 1;
}

// One step of the timer: the line starts at rest, waits three seconds, then
// scrolls up a pixel every 40 ms for one line's height.
void TickerStep(char* self)
{
    char* const clock = self + ticker::kTimer;
    void* const line = At<void*>(self, ticker::kLine);
    if (!line) {
        Virtual<uint8_t>(clock, timer::kStop);
        return;
    }
    const uint32_t scroll = At<uint32_t>(self, ticker::kScroll);
    if (scroll == 0) {
        Virtual<void>(Virtual<void*>(line, 0xC), 0xD8, &At<uint32_t>(self, ticker::kLineArea));
        At<uint32_t>(self, ticker::kChanged) = 1;
        Virtual<void>(self, 0x28);
        if (At<uint32_t>(self, ticker::kPending) == 0)
            At<uint32_t>(self, ticker::kScroll) = At<uint32_t>(self, ticker::kLineHeight);
        else
            At<uint32_t>(self, ticker::kScroll) = 1;
        Virtual<uint8_t>(clock, timer::kSetInterval, uint32_t{3000});
        return;
    }
    uint32_t area[4];
    area[0] = At<uint32_t>(self, ticker::kLineArea + 0x0);
    area[1] = At<uint32_t>(self, ticker::kLineArea + 0x4) - scroll;
    area[2] = At<uint32_t>(self, ticker::kLineArea + 0x8);
    area[3] = At<uint32_t>(self, ticker::kLineArea + 0xC) - scroll;
    Virtual<void>(Virtual<void*>(line, 0xC), 0xD8, static_cast<uint32_t*>(area));
    const uint32_t now = At<uint32_t>(self, ticker::kScroll);
    if (now < At<uint32_t>(self, ticker::kLineHeight)) {
        At<uint32_t>(self, ticker::kScroll) = now + 1;
        Virtual<uint8_t>(clock, timer::kSetInterval, uint32_t{40});
    } else {
        At<uint32_t>(self, ticker::kScroll) = 0;
        Virtual<uint8_t>(clock, timer::kSetInterval, uint32_t{3000});
    }
}

// The game's 0x00772B60. The ticker's message handler (slot 3): the timer's
// message and the advisor system's four. The function runs on to 0x00772E7F,
// past the start of the next part's range.
uint8_t __fastcall TickerDoMessage(char* self, void*, void* message)
{
    switch (Virtual<uint32_t>(message, 0x10)) { // GetType
    case kTimerMessage:
        TickerStep(self);
        return 1;
    case kAdviceAdded: {
        void* advice = nullptr;
        if (MessageAdvice(message, &advice) && !Virtual<uint8_t>(advice, 0x3C, uint32_t{0x2000})) {
            At<uint32_t>(self, ticker::kChanged) = 1;
            Virtual<void>(self, 0x20, advice);
            Virtual<void>(self, 0x24);
        }
        if (advice)
            Release(advice);
        return 1;
    }
    case kAdviceChanged: {
        void* advice = nullptr;
        if (MessageAdvice(message, &advice)) {
            Virtual<void>(self, 0x24);
            At<uint32_t>(self, ticker::kChanged) = 1;
        }
        if (advice)
            Release(advice);
        return 1;
    }
    case kAdviceRemoved: {
        void* advice = nullptr;
        if (MessageAdvice(message, &advice)) {
            Virtual<void>(self, 0x1C, advice);
            Virtual<void>(self, 0x24);
            At<uint32_t>(self, ticker::kChanged) = 1;
        }
        if (advice)
            Release(advice);
        return 1;
    }
    case kAdvisorRemoved: {
        void* advisor = nullptr;
        bool removed = false;
        void* const object = Virtual<void*>(message, 0x80);
        if (Virtual<uint8_t>(object, 0x0, kAdvisorIid, &advisor)) {
            const uint32_t id = Virtual<uint32_t>(advisor, 0x14);
            ListNode* const head = At<ListNode*>(self, ticker::kAdvices);
            ListNode* node = head->next;
            if (node != head) {
                do {
                    if (Virtual<uint32_t>(node->value, 0x2C) == id) {
                        ListNode* const next = node->next;
                        ListNode* const previous = node->previous;
                        previous->next = next;
                        next->previous = previous;
                        if (node->value)
                            Release(node->value);
                        Free(node);
                        node = next;
                        removed = true;
                    } else {
                        node = node->next;
                    }
                } while (node != head);
                if (removed) {
                    Virtual<void>(self, 0x24);
                    At<uint32_t>(self, ticker::kChanged) = 1;
                    Virtual<void>(self, 0x28);
                }
            }
        }
        if (advisor)
            Release(advisor);
        return 1;
    }
    default:
        return 1;
    }
}

bool InstallMySimPanel1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F771440, reinterpret_cast<void*>(&ShowFirstList)},
        {addr::SC4UI_F771550, reinterpret_cast<void*>(&ShowSecondList)},
        {addr::SC4UI_F771660, reinterpret_cast<void*>(&ShowMySim)},
        {addr::SC4UI_F771940, reinterpret_cast<void*>(&SelectPlace)},
        {addr::SC4UI_F7719F0, reinterpret_cast<void*>(&Recount)},
        {addr::SC4UI_F771B40, reinterpret_cast<void*>(&DeleteMySim)},
        {addr::SC4UI_F771B90, reinterpret_cast<void*>(&DoWinProcMessage)},
        {addr::SC4UI_F7721A0, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4UI_F772440, reinterpret_cast<void*>(&TickerTimer)},
        {addr::SC4UI_F7724C0, reinterpret_cast<void*>(&TickerInit)},
        {addr::SC4UI_F772760, reinterpret_cast<void*>(&TickerListen)},
        {addr::SC4UI_F772810, reinterpret_cast<void*>(&TickerDoWinProcMessage)},
        {addr::SC4UI_F772920, reinterpret_cast<void*>(&TickerShow)},
        {addr::SC4UI_F772A00, reinterpret_cast<void*>(&TickerHide)},
        {addr::SC4UI_F772A70, reinterpret_cast<void*>(&TickerShutdown)},
        {addr::SC4UI_F772B60, reinterpret_cast<void*>(&TickerDoMessage)},
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

const bool g_mySimPanel1Registered = registry::Register({
    "ui.mysim_panel.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F771B90,
    "Our MySim panel panes, window procedure and message handler, and the news ticker's start and messages",
    false,
    InstallMySimPanel1,
});

} // namespace
} // namespace opensc4::ui
