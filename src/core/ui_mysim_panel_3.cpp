// The start of the MySim panel's code, functions between 0x0076E500 and
// 0x0076FDB0. Replacement, off by default:
//
//   ui.mysim_panel.part3: the panel's constructor and destructor, its
//       QueryInterface and the thunks of its other two parts, starting it on
//       its window (the panes hidden, the place buttons, the notifications,
//       the script table "mysims_ui" with its two variables) and stopping it,
//       showing and hiding it, the count of the city's MySims with their
//       names and portraits, the page of a MySim's details, the list of the
//       chosen MySim's advices, the pictures of the two vehicle buttons and
//       the move-in dialog for an empty place; before them the window
//       procedure of the window class that precedes the panel in the exe.
//
// The panel is 0x7C bytes, made by the city view (cSC4View3DWin, 0x007EE3A7)
// with operator new and kept at the view's +0x2E4: the window procedure's
// vtable (0x00AB5114) at +0, a message target's (0x00AB5104) at +4 and a
// counted base (0x00AB50EC) at +8. ui_mysim_panel_1.cpp has its window
// procedure, message handler and panes; the list's rows and the pick-one
// dialog (0x0076FDB0 to 0x00771440) are in the next part and called by
// address.
//
// The first function (0x0076E500) is not the panel's: it is the window
// procedure (the +4 part, vtable 0x00AB4FE0) of another window object of the
// city view (constructor 0x0076CFE0, code from 0x0076B860), whose other
// functions are still the game's.
//
// Not hooked: 0x0076F100, 0x0076F110, 0x0076F130 and 0x0076F140, the AddRef
// and Release thunks of the +4 and +8 parts, which the vtables of unrelated
// classes share. 0x0076E5B0 is not a function: it is a branch of 0x0076E500.
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

// Fields of the MySim panel.
namespace panel {
constexpr std::size_t kTarget = 0x4;    // the message target part
constexpr std::size_t kCounted = 0x8;   // the counted base (vtable, count)
constexpr std::size_t kWindow = 0x10;   // the panel's window (cIGZWinGen)
constexpr std::size_t kUnused = 0x14;   // a reference nothing sets
constexpr std::size_t kList = 0x18;     // the advice list shown (interface 0xCA1492A2)
constexpr std::size_t kMySims = 0x1C;   // five cISC4MySim, one per place
constexpr std::size_t kDialog = 0x30;   // the move-in dialog while it runs
constexpr std::size_t kSimulator = 0x34; // the MySim simulator
constexpr std::size_t kAdvices = 0x38;  // list of the chosen MySim's advices (sentinel node)
constexpr std::size_t kAdviceSet = 0x3C; // a copy of the advisor's set: header, count
constexpr std::size_t kCurrent = 0x48;  // the chosen place, 0 to 4
constexpr std::size_t kCount = 0x4C;    // how many MySims the last count found
constexpr std::size_t kMode = 0x50;     // 0 the MySim's page, 1 an advice list, 2 the details
constexpr std::size_t kListKind = 0x54; // which advice list: 0 or 1
constexpr std::size_t kWatching = 0x58; // byte: shown, follow the simulator's "changed" message
constexpr std::size_t kTab = 0x70;
constexpr std::size_t kLast = 0x78;     // a reference the destructor drops
} // namespace panel

// Fields of the other window object, from its window procedure part (+4).
namespace other {
constexpr std::size_t kObject = 0x4;    // the part's offset in the object
constexpr std::size_t kShown = 0x10;    // the choice last shown
constexpr std::size_t kOwner = 0x14;    // the object given to the constructor
constexpr std::size_t kFirst = 0x18;    // two windows, one shown at a time
constexpr std::size_t kSecond = 0x1C;
constexpr std::size_t kOnSecond = 0x25; // byte
constexpr std::size_t kValue = 0x48;
} // namespace other

constexpr int32_t kPlaces = 5;

constexpr uint32_t kImageWinIid = 0xC12CEA13;
constexpr uint32_t kMessageTargetIid = 0x452294AA;
constexpr uint32_t kPanelIid = 0x22E85D8E;

// A window message's type for a button press, and its second word.
constexpr uint32_t kCommand = 3;
constexpr uint32_t kCommandKind = 0x287259F6;

// The six messages the panel listens for: the MySim simulator's "changed",
// the advisor system's advice changed, added and removed, and the two the
// script variables post.
constexpr uint32_t kMessages[6] = {0xAA25827C, 0x8A09FA65, 0x8A09FA64, 0xCA09FA6C, 0xAC08746E, 0xAC08746F};
constexpr uint32_t kSelectCar = 0xAC08746E;
constexpr uint32_t kSelectPedestrian = 0xAC08746F;

// Panes of the panel.
constexpr uint32_t kPagePane = 0xCA1F1D9C;
constexpr uint32_t kFirstListPane = 0xEA1F1E4D;
constexpr uint32_t kSecondListPane = 0xAA1F1EC5;
constexpr uint32_t kThirdPane = 0xEA1F1E5E;
constexpr uint32_t kFourthPane = 0x6A61E29F;
constexpr uint32_t kNamePane = 0xABBAA2D3;
constexpr uint32_t kFrame = 0x698894D3;   // shown with the panel
constexpr uint32_t kNameBar = 0xEA1F1E4E; // shown with a MySim

// The game's tables of window IDs by place at 0x00AB5010, five each, and
// after them the twelve signs' text IDs. The game indexes them unchecked.
namespace table {
constexpr std::size_t kOccupied = 0x00;    // shown for a place with a MySim
constexpr std::size_t kButton = 0x14;      // the place's button, shown with a MySim
constexpr std::size_t kEmptyButton = 0x28; // the button of an empty place
constexpr std::size_t kThird = 0x3C;       // shown with a MySim
constexpr std::size_t kName = 0x50;        // the text with the MySim's name
constexpr std::size_t kSigns = 0x64;
} // namespace table

constexpr uint32_t kPortrait = 0x22220000;      // the first place's picture window
constexpr uint32_t kImageType = 0x46A006B0;
constexpr uint32_t kEmptyPortrait = 0x13F1525C; // the picture of an empty place

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A string from the text resources: a string first, 0x20 bytes.
struct LocalizedText {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedText) == 0x20);

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
static_assert(sizeof(ListNode) == 0xC);

// A node of the game's red-black trees; the header has the root as parent
// and the first and last nodes as left and right.
struct TreeNode {
    uint8_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    void* value;
    uint32_t more;
};
static_assert(sizeof(TreeNode) == 0x18);

struct PointerVector {
    void** begin;
    void** end;
    void** capacity;
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

// An empty string with the game's first buffer of 8 bytes.
void MakeEmpty(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

// The parent of the panel's window.
void* ParentWindow(char* self)
{
    void* const window = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC); // AsIGZWin
    return Virtual<void*>(window, 0x2C);                                       // GetParentWin
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id); // GetChildWindowFromIDRecursive
}

void ShowChild(void* window, uint32_t id, uint32_t show)
{
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id, show, uint32_t{0});
}

void SetChildText(void* window, uint32_t id, void* text)
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

uint32_t PlaceId(std::size_t which, uint32_t index)
{
    return Global<uint32_t>(addr::SC4MySimPanel_PlaceWindows + which + 4 * static_cast<std::uintptr_t>(index));
}

char*& MySim(char* self, uint32_t place)
{
    return At<char*>(self, panel::kMySims + 4 * static_cast<std::size_t>(place));
}

char* CurrentMySim(char* self)
{
    return MySim(self, At<uint32_t>(self, panel::kCurrent));
}

// The script service (class 0xEBE0E860) in a holder the caller releases.
void GetScriptService(void** holder)
{
    *holder = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, holder);
}

void ReleaseHolder(void** holder)
{
    This<void>(addr::SC4_ReleaseRefHolder, holder);
}

// Gives a picture window of `window` the loader's image.
void SetPicture(void* window, uint32_t id, void*& picture, const ImageLoader& loader)
{
    if (Virtual<uint8_t>(window, 0x94, id, kImageWinIid, &picture)) // GetChildAsRecursive
        Virtual<void>(picture, 0x10, loader.image);
}

// Every place shown as empty: the three windows of an occupied place hidden,
// the empty place's button shown and the portrait set to the loader's image.
void ShowEmptyPlaces(void* window, const ImageLoader& loader)
{
    for (uint32_t place = 0; place < static_cast<uint32_t>(kPlaces); ++place) {
        ShowChild(window, PlaceId(table::kOccupied, place), 0);
        ShowChild(window, PlaceId(table::kButton, place), 0);
        ShowChild(window, PlaceId(table::kThird, place), 0);
        ShowChild(window, PlaceId(table::kEmptyButton, place), 1);
        void* picture = nullptr;
        SetPicture(window, kPortrait + place, picture, loader);
        if (picture)
            Release(picture);
    }
}

// Clears the copy of the advisor's set, as the game's inlined clear does.
void ClearAdviceSet(char* self)
{
    TreeNode*& header = At<TreeNode*>(self, panel::kAdviceSet);
    if (At<uint32_t>(self, panel::kAdviceSet + 4) == 0)
        return;
    This<void>(addr::cSC4Advisor_F40E050, &header, header->parent);
    header->left = header;
    header->parent = nullptr;
    header->right = header;
    At<uint32_t>(self, panel::kAdviceSet + 4) = 0;
}

// The node before `node` in the tree's order (the last node for the header).
TreeNode* Previous(TreeNode* node)
{
    if (node->colour == 0 && node->parent->parent == node)
        return node->right;
    if (TreeNode* below = node->left) {
        while (below->right)
            below = below->right;
        return below;
    }
    TreeNode* parent = node->parent;
    while (node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

// A node with the advice at the end of the panel's list, and the advice's
// flag 0x20 cleared (slot 14). The game calls the advice even when null.
void Append(char* self, void* advice)
{
    ListNode* const head = At<ListNode*>(self, panel::kAdvices);
    ListNode* const node = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    node->value = advice;
    if (advice)
        AddRef(advice);
    ListNode* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
    if (advice)
        Release(advice);
    Virtual<void>(advice, 0x38, uint32_t{0x20}, uint32_t{0});
}

// The game's 0x0076E500. The window procedure of the other window object
// (`proc` is its +4 part): a slider and the buttons of its two pages.
uint8_t __fastcall OtherDoWinProcMessage(char* proc, void*, void* window, uint32_t* message)
{
    char* const self = proc - other::kObject;
    if (!Virtual<uint8_t>(At<void*>(proc, other::kOwner), 0x11C))
        return 0;
    if (message[0] != kCommand)
        return 0;
    if (!Cdecl<uint8_t>(addr::SC4Tutorial_FilterWinMessage, message))
        return 1;
    if (message[1] != kCommandKind) {
        if (message[1] != 0x887113A3 || message[2] != 0xEA4A7CF2)
            return 0;
        // The game uses the message's argument slot for the control.
        void* control = message;
        if (!Virtual<uint8_t>(window, 0x94, 0xEA4A7CF2u, 0x21325207u, &control))
            return 0;
        if (At<uint32_t>(proc, other::kValue) != Virtual<uint32_t>(control, 0x10))
            This<void>(addr::SC4UI_F76C910, self, Virtual<uint32_t>(control, 0x10));
        Release(control);
        return 0;
    }
    const uint32_t id = message[2];
    if (id == 0x0A8B7E00) {
        Virtual<void>(At<void*>(proc, other::kSecond), 0x118);
        Virtual<void>(At<void*>(proc, other::kFirst), 0x114);
        At<uint8_t>(proc, other::kOnSecond) = 0;
        return 0;
    }
    if (id == 0x0A8B7E01) {
        Virtual<void>(At<void*>(proc, other::kFirst), 0x118);
        Virtual<void>(At<void*>(proc, other::kSecond), 0x114);
        At<uint8_t>(proc, other::kOnSecond) = 1;
        return 0;
    }
    if (id == 0xCA8B82F9) {
        // The game pushes the ID's upper bytes with the flag; the callee
        // reads the low byte.
        This<void>(addr::SC4UI_F76B860, self, At<uint8_t>(proc, other::kOnSecond) == 0 ? 1u : 0u);
        return 0;
    }
    if (id >= 0x4000 && id < 0x5000) {
        const uint32_t choice = id & 0xFF;
        Virtual<void>(At<void*>(proc, other::kFirst), 0x118);
        Virtual<void>(At<void*>(proc, other::kSecond), 0x114);
        At<uint8_t>(proc, other::kOnSecond) = 1;
        This<void>(addr::SC4UI_F76B8B0, self, choice);
        if (At<uint32_t>(proc, other::kShown) == choice)
            return 0;
        This<void>(addr::SC4UI_F76D3D0, self, choice);
        At<uint32_t>(proc, other::kShown) = choice;
        return 0;
    }
    if (id >= 0x04000000 && id < 0x04000020)
        This<void>(addr::SC4UI_F76BE70, self);
    return 0;
}

// The game's 0x0076E6B0. The panel's QueryInterface: its own ID, the message
// target part, or the counted base's.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kPanelIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid == kMessageTargetIid) {
        *out = self ? self + panel::kTarget : nullptr;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self + panel::kCounted, iid, out);
}

// The two script variables share everything but the message they post: when
// the script's owner is found and the variable is read, not set, the message
// is posted and the value becomes true.
uint8_t ScriptSelect(void* state, void* value, uint8_t set, void* script, uint32_t message)
{
    if (Virtual<void*>(script, 0x48, Virtual<void*>(script, 0x20, state)) && !set) {
        Cdecl<void>(addr::SC4_PostMessage, message, uint32_t{0}, uint32_t{0}, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Virtual<void>(value, 0x2C, uint32_t{1});
    }
    return 1;
}

// The game's 0x0076E710. The script variable mysims_ui.pop_select_car_dialog
// (cdecl).
uint8_t __cdecl ScriptSelectCar(void* state, void* value, uint8_t set, void* script)
{
    return ScriptSelect(state, value, set, script, kSelectCar);
}

// The game's 0x0076E760. The script variable
// mysims_ui.pop_select_pedestrian_dialog (cdecl).
uint8_t __cdecl ScriptSelectPedestrian(void* state, void* value, uint8_t set, void* script)
{
    return ScriptSelect(state, value, set, script, kSelectPedestrian);
}

// The game's 0x0076E7B0. Shows the third pane and hides the other five.
void __fastcall ShowThirdPane(char* self, void*)
{
    ShowPane(ParentWindow(self), kThirdPane);
}

// The game's 0x0076E830. The move-in dialog for an empty place: the two list
// panes' flag 1 cleared, the dialog made, run and dropped. `place` goes to
// the dialog's constructor.
void __fastcall OfferMoveIn(char* self, void*, int32_t place)
{
    void* const parent = ParentWindow(self);
    if (void* const pane = Child(parent, kSecondListPane))
        Virtual<uint32_t>(pane, 0x110, uint32_t{1}, uint32_t{0});
    if (void* const pane = Child(parent, kFirstListPane))
        Virtual<uint32_t>(pane, 0x110, uint32_t{1}, uint32_t{0});
    void*& dialog = At<void*>(self, panel::kDialog);
    if (!dialog) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x64});
        dialog = memory ? This<void*>(addr::SC4UI_F776560, memory, place) : nullptr;
        AddRef(dialog);
        This<void>(addr::SC4UI_F775CC0, dialog); // Run
    }
    if (dialog) {
        This<void>(addr::SC4UI_F7766A0, dialog); // Close
        Release(dialog);
        dialog = nullptr;
    }
}

// The game's 0x0076E8E0. Hides the panel: its frame, every pane and the name
// bar; the simulator stops showing a MySim's place and a move-in under way
// is cancelled.
void __fastcall Hide(char* self, void*)
{
    void* const parent = ParentWindow(self);
    ShowChild(parent, kFrame, 0);
    ShowPane(ParentWindow(self), 0);
    ShowChild(parent, kNameBar, 0);
    if (void* const simulator = At<void*>(self, panel::kSimulator)) {
        Virtual<void>(simulator, 0x20);                           // HideLocation
        Virtual<void>(At<void*>(self, panel::kSimulator), 0x28); // CancelMoveIn
    }
    At<uint8_t>(self, panel::kWatching) = 0;
}

// The game's 0x0076E9B0. Frees a subtree of a tree whose values need no
// destructor (the next part's function 0x0076FDB0 has such a tree).
void __fastcall EraseSubtree(void* tree, void*, TreeNode* node)
{
    while (node) {
        EraseSubtree(tree, nullptr, node->right);
        TreeNode* const left = node->left;
        Free(node);
        node = left;
    }
}

// The game's 0x0076E9F0. Starts the panel on its window: everything hidden,
// the panel made the target of the panes' buttons, every place shown as
// empty, the six notifications registered, and the script table "mysims_ui"
// registered with its two variables.
void __fastcall Start(char* self, void*)
{
    void* const window = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
    void* const parent = Virtual<void*>(window, 0x2C);
    At<uint32_t>(self, panel::kCount) = 0;
    ShowChild(parent, kFrame, 0);
    ShowPane(ParentWindow(self), 0);
    ShowChild(parent, kNameBar, 0);
    for (const uint32_t id :
         {kPagePane, kSecondListPane, kThirdPane, kFirstListPane, kNamePane, kFourthPane, kNameBar})
        Cdecl<uint8_t>(addr::SC4Win_SetChildChildrenTarget, parent, id, window);

    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kImageType, kEmptyPortrait, uint32_t{1},
                uint32_t{0});
    ShowEmptyPlaces(parent, loader);

    void* const server = Global<void*>(addr::SC4_MessageServer);
    for (const uint32_t type : kMessages)
        Virtual<uint8_t>(server, 0x14, self + panel::kTarget, type);

    using RegisterTableFn = uint32_t(__cdecl*)(void*, const char*, void*, uint32_t, uint32_t, void*);
    using RegisterVariableFn = uint32_t(__cdecl*)(void*, const char*, std::uintptr_t, void*, uint32_t, void*, void*);
    // The game fetches the service once more than it uses it.
    void* script;
    GetScriptService(&script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterTableFn>(addr::SC4Script_RegisterTable)(
        self, reinterpret_cast<const char*>(addr::SC4MySimPanel_ScriptTable), nullptr, 0, 0, script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterVariableFn>(addr::SC4Script_RegisterVariable)(
        nullptr, reinterpret_cast<const char*>(addr::SC4MySimPanel_ScriptSelectCar), addr::SC4UI_F76E710, self, 2, self,
        script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterVariableFn>(addr::SC4Script_RegisterVariable)(
        nullptr, reinterpret_cast<const char*>(addr::SC4MySimPanel_ScriptSelectPedestrian), addr::SC4UI_F76E760, self,
        2, self, script);
    ReleaseHolder(&script);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// The game's 0x0076ED10. Stops the panel: the list control, the script
// service's entries of the panel, the notifications, the window (destroyed
// through its window manager) and every reference.
void __fastcall Stop(char* self, void*)
{
    AddRef(self);
    if (void* const list = At<void*>(self, panel::kList))
        Virtual<void>(list, 0x1C);
    void* script;
    GetScriptService(&script);
    if (void* const service = script) {
        void* unknown = nullptr;
        Virtual<uint8_t>(self, 0x0, uint32_t{1}, &unknown);
        Virtual<void>(service, 0x1C, unknown);
        if (unknown)
            Release(unknown);
    }
    ReleaseHolder(&script);

    void* const server = Global<void*>(addr::SC4_MessageServer);
    for (const uint32_t type : kMessages)
        Virtual<uint8_t>(server, 0x18, self + panel::kTarget, type);

    void* const window = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
    if (void* const manager = Virtual<void*>(window, 0x18)) // GetWindowManager
        Virtual<uint8_t>(manager, 0x5C, Virtual<void*>(At<void*>(self, panel::kWindow), 0xC)); // DestroyWindow
    ReleaseAndClear(At<void*>(self, panel::kWindow));
    ReleaseAndClear(At<void*>(self, panel::kUnused));
    ReleaseAndClear(At<void*>(self, panel::kList));
    for (int32_t place = 0; place < kPlaces; ++place)
        ReleaseAndClear(At<void*>(self, panel::kMySims + 4 * static_cast<std::size_t>(place)));
    if (void* const dialog = At<void*>(self, panel::kDialog)) {
        This<void>(addr::SC4UI_F7766A0, dialog);
        Release(At<void*>(self, panel::kDialog));
        At<void*>(self, panel::kDialog) = nullptr;
    }
    if (void* const simulator = At<void*>(self, panel::kSimulator)) {
        Release(simulator);
        At<void*>(self, panel::kSimulator) = nullptr;
    }
    Release(self);
}

// The game's 0x0076EE90. The picture of a vehicle button: the image
// {0, 0x4C06F888, bitmap} given to button 0x2BBAAA69, or 0x2BBAAA71 with the
// flag.
void __fastcall SetVehiclePicture(char* self, void*, uint32_t bitmap, uint8_t second)
{
    const uint32_t id = second ? 0x2BBAAA71u : 0x2BBAAA69u;
    void* picture = nullptr;
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, 0x4C06F888u, bitmap, uint32_t{1},
                uint32_t{0});
    void* const parent = ParentWindow(self);
    ReleaseAndClear(picture);
    if (Virtual<uint8_t>(parent, 0x94, id, kImageWinIid, &picture) && loader.image)
        Virtual<void>(picture, 0x10, loader.image);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    if (picture)
        Release(picture);
}

// The game's 0x0076EF40. Takes the advice out of the panel's list.
void __fastcall RemoveAdvice(char* self, void*, void* advice)
{
    ListNode* const head = At<ListNode*>(self, panel::kAdvices);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const held = node->value;
        if (held)
            AddRef(held);
        if (held == advice) {
            ListNode* const previous = node->previous;
            ListNode* const next = node->next;
            previous->next = next;
            next->previous = previous;
            if (node->value)
                Release(node->value);
            Free(node);
            if (held)
                Release(held);
            return;
        }
        if (held)
            Release(held);
    }
}

// The game's 0x0076EFB0. Appends the advice to the panel's list and clears
// its flag 0x20.
void __fastcall AddAdvice(char* self, void*, void* advice)
{
    if (advice)
        AddRef(advice);
    Append(self, advice);
}

// The game's 0x0076F010. The destructor: the references, the set, the list
// and the counted base.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimPanel_Vtable;
    At<std::uintptr_t>(self, panel::kTarget) = addr::SC4MySimPanel_TargetVtable;
    At<std::uintptr_t>(self, panel::kCounted) = addr::SC4MySimPanel_CountedVtable;
    if (void* const last = At<void*>(self, panel::kLast))
        Release(last);
    ClearAdviceSet(self);
    if (TreeNode* const header = At<TreeNode*>(self, panel::kAdviceSet))
        Free(header);
    This<void>(addr::SC4RefList_Clear, self + panel::kAdvices);
    if (void* const head = At<void*>(self, panel::kAdvices))
        Free(head);
    for (int32_t place = kPlaces - 1; place >= 0; --place) {
        if (void* const mysim = At<void*>(self, panel::kMySims + 4 * static_cast<std::size_t>(place)))
            Release(mysim);
    }
    if (void* const list = At<void*>(self, panel::kList))
        Release(list);
    if (void* const unused = At<void*>(self, panel::kUnused))
        Release(unused);
    if (void* const window = At<void*>(self, panel::kWindow))
        Release(window);
    This<void>(addr::cRZBaseUnknown_Destruct, self + panel::kCounted);
}

// The game's 0x0076F0F0. QueryInterface of the message target part.
uint8_t __fastcall TargetQueryInterface(char* target, void*, uint32_t iid, void** out)
{
    return QueryInterface(target - panel::kTarget, nullptr, iid, out);
}

// The game's 0x0076F120. QueryInterface of the counted base.
uint8_t __fastcall CountedQueryInterface(char* counted, void*, uint32_t iid, void** out)
{
    return QueryInterface(counted - panel::kCounted, nullptr, iid, out);
}

// The game's 0x0076F160. The constructor, on the panel's window.
char* __fastcall Construct(char* self, void*, void* window)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<std::uintptr_t>(self, panel::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    This<void*>(addr::cRZBaseUnknown_Construct, self + panel::kCounted);
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimPanel_Vtable;
    At<std::uintptr_t>(self, panel::kTarget) = addr::SC4MySimPanel_TargetVtable;
    At<std::uintptr_t>(self, panel::kCounted) = addr::SC4MySimPanel_CountedVtable;
    At<void*>(self, panel::kWindow) = window;
    if (window)
        AddRef(window);
    At<void*>(self, panel::kUnused) = nullptr;
    At<void*>(self, panel::kList) = nullptr;
    for (int32_t place = 0; place < kPlaces; ++place)
        At<void*>(self, panel::kMySims + 4 * static_cast<std::size_t>(place)) = nullptr;
    At<void*>(self, panel::kDialog) = nullptr;
    At<void*>(self, panel::kSimulator) = nullptr;
    At<void*>(self, panel::kAdvices) = nullptr;
    ListNode* const head = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    head->next = head;
    head->previous = head;
    At<ListNode*>(self, panel::kAdvices) = head;
    At<void*>(self, panel::kAdviceSet) = nullptr;
    TreeNode* const header = Cdecl<TreeNode*>(addr::SC4_stl_allocate, uint32_t{0x18});
    At<TreeNode*>(self, panel::kAdviceSet) = header;
    At<uint32_t>(self, panel::kAdviceSet + 4) = 0;
    header->colour = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
    At<uint32_t>(self, panel::kCurrent) = 0;
    At<uint32_t>(self, panel::kCount) = 0;
    At<uint32_t>(self, panel::kMode) = 1;
    At<uint32_t>(self, panel::kListKind) = 0;
    At<uint8_t>(self, panel::kWatching) = 0;
    At<uint32_t>(self, panel::kTab) = 0;
    At<void*>(self, panel::kLast) = nullptr;
    for (int32_t place = 0; place < kPlaces; ++place)
        ReleaseAndClear(At<void*>(self, panel::kMySims + 4 * static_cast<std::size_t>(place)));
    return self;
}

// The game's 0x0076F240. The scalar deleting destructor.
char* __fastcall Delete(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0076F150. The counted base's slot 3: the destructor.
char* __fastcall CountedDelete(char* counted, void*, uint32_t flags)
{
    return Delete(counted - panel::kCounted, nullptr, flags);
}

// The game's 0x0076F260. Takes an advisor's advices: the advices held lose
// their flag 0x20 unless flag 2 is set, the set is copied from the advisor
// (its slot 10) and the list rebuilt from it, last to first. The advisor is
// a counted reference passed by value and released here.
void __fastcall SetAdvisor(char* self, void*, void* advisor)
{
    ListNode* const old = At<ListNode*>(self, panel::kAdvices);
    for (ListNode* node = old->next; node != old; node = node->next) {
        void* const advice = node->value;
        if (advice)
            AddRef(advice);
        // The game calls the advice even when the node holds none.
        if (!Virtual<uint8_t>(advice, 0x3C, uint32_t{2}))
            Virtual<void>(advice, 0x38, uint32_t{0x20}, uint32_t{0});
        Release(advice);
    }
    ClearAdviceSet(self);
    This<void*>(addr::cSC4Advisor_F40EB00, self + panel::kAdviceSet, Virtual<void*>(advisor, 0x28));

    ListNode* node = At<ListNode*>(self, panel::kAdvices)->next;
    while (node != At<ListNode*>(self, panel::kAdvices)) {
        ListNode* const dead = node;
        node = node->next;
        if (dead->value)
            Release(dead->value);
        Free(dead);
    }
    At<ListNode*>(self, panel::kAdvices)->next = At<ListNode*>(self, panel::kAdvices);
    At<ListNode*>(self, panel::kAdvices)->previous = At<ListNode*>(self, panel::kAdvices);

    TreeNode* position = At<TreeNode*>(self, panel::kAdviceSet);
    if (position != position->left) {
        for (;;) {
            void* const advice = Previous(position)->value;
            if (advice) {
                AddRef(advice);
                AddRef(advice);
            }
            Append(self, advice);
            Release(advice);
            position = Previous(position);
            if (position == At<TreeNode*>(self, panel::kAdviceSet)->left)
                break;
        }
    }
    Release(advisor);
}

// The game's 0x0076F450. The details of the chosen MySim on the fourth pane:
// name, wealth as one to three marks, sign, age, home, job and the other
// texts, and the biography. Leaves mode 2.
void __fastcall ShowDetails(char* self, void*)
{
    void* const parent = ParentWindow(self);
    ShowPane(ParentWindow(self), kFourthPane);
    char* const chosen = CurrentMySim(self);
    if (!chosen || !Virtual<uint8_t>(chosen, 0x50)) // IsActive
        return;

    uint8_t allocator = 0;
    GameString name;
    This<void*>(addr::SC4String_FromIGZString, &name, Virtual<void*>(Virtual<void*>(CurrentMySim(self), 0xC), 0x20));
    SetChildText(parent, 0x8A1F1EFF, &name);

    GameString text;
    MakeEmpty(text);
    GameString mark;
    MakeEmpty(mark);
    Virtual<void>(&mark, 0x10, "\xC2\xA7"); // the string from a C string
    const uint32_t wealth = Virtual<uint32_t>(CurrentMySim(self), 0x4C);
    if (wealth == 3)
        This<void>(addr::SC4String_AppendRange, &text.begin, mark.begin, mark.end, &allocator);
    if (wealth == 3 || wealth == 2)
        This<void>(addr::SC4String_AppendRange, &text.begin, mark.begin, mark.end, &allocator);
    if (wealth == 3 || wealth == 2 || wealth == 1)
        This<void>(addr::SC4String_AppendRange, &text.begin, mark.begin, mark.end, &allocator);
    SetChildText(parent, 0x8A1F1F00, &text);

    const uint8_t sign = Virtual<uint8_t>(CurrentMySim(self), 0x40);
    LocalizedText localized;
    void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &localized, 0x6A231EAAu,
                                   PlaceId(table::kSigns, sign), uint32_t{1});
    GameString signName;
    This<void>(addr::SC4String_CopyBuffer, &signName.begin, made ? static_cast<char*>(made) + 4 : nullptr,
               uint32_t{0}, uint32_t{0xFFFFFFFF}, &allocator);
    signName.vtable = addr::SC4String_Vtable;
    signName.zero = 0;
    SetChildText(parent, 0x8A1F1F01, &signName);
    Destroy(signName);
    Destroy(localized.text);

    const uint8_t age = Virtual<uint8_t>(CurrentMySim(self), 0x44);
    using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
    reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(&text, "%d", static_cast<uint32_t>(age));
    SetChildText(parent, 0x8A1F1F02, &text);
    SetChildText(parent, 0x8A1F1F03, Virtual<void*>(CurrentMySim(self), 0x64));
    SetChildText(parent, 0x8A1F1F04, Virtual<void*>(CurrentMySim(self), 0x70));
    SetChildText(parent, 0x8A1F1F05, Virtual<void*>(CurrentMySim(self), 0x68));
    Virtual<void>(CurrentMySim(self), 0x74, &text);
    SetChildText(parent, 0x8A1F1F06, &text);
    Virtual<void>(CurrentMySim(self), 0x78, &text);
    SetChildText(parent, 0x8A1F1F07, &text);
    Cdecl<uint8_t>(addr::SC4Win_SetChildEditText, parent, 0x8A243F6Eu, Virtual<void*>(CurrentMySim(self), 0x54));
    At<uint32_t>(self, panel::kMode) = 2;

    Destroy(mark);
    Destroy(text);
    Destroy(name);
}

// One MySim of the count: kept at its place, its name written, its place's
// windows shown, and its portrait set from the resource or, with no bitmap
// ID, from its file.
void ShowCounted(char* self, void* parent, char* mysim)
{
    uint8_t allocator = 0;
    if (mysim)
        AddRef(mysim);
    const uint32_t place = Virtual<uint8_t>(mysim, 0x28); // GetSlot
    char*& kept = MySim(self, place);
    if (char* const old = kept; mysim != old) {
        AddRef(mysim);
        kept = mysim;
        if (old)
            Release(old);
    }

    void* const nameText = Virtual<void*>(Virtual<void*>(kept, 0xC), 0x20);
    const uint32_t length = Virtual<uint32_t>(nameText, 0x1C);
    char* const chars = Virtual<char*>(nameText, 0x18);
    GameString name;
    name.begin = nullptr;
    name.end = nullptr;
    name.capacity = nullptr;
    char* end;
    if (length + 1 != 0) {
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, length + 1);
        name.begin = buffer;
        name.end = buffer;
        name.capacity = buffer + length + 1;
        end = buffer;
    } else {
        Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
        end = name.begin;
    }
    if (chars + length != chars)
        end = Cdecl<char*>(addr::SC4_memmove, end, chars, length) + length;
    name.end = end;
    *end = 0;
    name.vtable = addr::SC4String_Vtable;
    name.zero = 0;
    Virtual<void>(Child(parent, PlaceId(table::kName, place)), 0x128, &name);

    void* const window = ParentWindow(self);
    ShowChild(window, PlaceId(table::kOccupied, place), 1);
    ShowChild(window, PlaceId(table::kButton, place), 1);
    ShowChild(window, PlaceId(table::kThird, place), 1);
    ShowChild(window, PlaceId(table::kEmptyButton, place), 0);

    const uint32_t bitmap = Virtual<uint32_t>(kept, 0x58); // GetBitmapID
    if (bitmap != 0) {
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kImageType, bitmap, uint32_t{1},
                    uint32_t{0});
        void* picture = nullptr;
        SetPicture(window, kPortrait + place, picture, loader);
        if (picture)
            Release(picture);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    } else {
        GameString path;
        MakeEmpty(path);
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xF4, &path);
        // The file name's body; the game reads through it even when the
        // MySim gives no string.
        char* const file = Virtual<char*>(kept, 0x5C); // GetBitmapFile
        char* const body = file ? file + 4 : nullptr;
        char* const fileEnd = At<char*>(body, 0x4);
        char* const fileBegin = At<char*>(body, 0x0);
        This<void>(addr::SC4String_AppendRange, &path.begin, fileBegin, fileEnd, &allocator);

        void* picture = nullptr;
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructName, &loader, &path, uint32_t{8}, uint32_t{0});
        ReleaseAndClear(picture);
        SetPicture(window, kPortrait + place, picture, loader);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        if (picture)
            Release(picture);
        Destroy(path);
    }

    ++At<uint32_t>(self, panel::kCount);
    Destroy(name);
    Release(mysim);
}

// The game's 0x0076F7C0. Counts the city's MySims again: the five places
// emptied, then every active MySim of the simulator put at its place; the
// move-in button (0xEA1F1DBA) is enabled while a place is free, and the
// simulator stops showing a MySim's place.
void __fastcall Recount(char* self, void*)
{
    void* const parent = ParentWindow(self);
    void* const city = Global<void*>(addr::SC4_City);
    At<uint32_t>(self, panel::kCount) = 0;
    for (int32_t place = 0; place < kPlaces; ++place)
        ReleaseAndClear(At<void*>(self, panel::kMySims + 4 * static_cast<std::size_t>(place)));

    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kImageType, kEmptyPortrait, uint32_t{1},
                uint32_t{0});
    ShowEmptyPlaces(parent, loader);

    if (city) {
        void*& simulator = At<void*>(self, panel::kSimulator);
        if (!simulator) {
            simulator = Virtual<void*>(city, 0x11C); // the MySim simulator
            AddRef(simulator);
        }
        if (simulator) {
            PointerVector mysims = {nullptr, nullptr, nullptr};
            Virtual<uint8_t>(simulator, 0x14, &mysims); // GetActiveMySims
            for (void** entry = mysims.begin; entry != mysims.end; ++entry)
                ShowCounted(self, parent, static_cast<char*>(*entry));

            // The game pushes a register whose low byte is the flag.
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, ParentWindow(self), 0xEA1F1DBAu,
                           At<uint32_t>(self, panel::kCount) < static_cast<uint32_t>(kPlaces) ? 1u : 0u);
            Virtual<void>(At<void*>(self, panel::kSimulator), 0x1C, uint32_t{0}); // TrackLocation
            for (void** entry = mysims.begin; entry != mysims.end; ++entry) {
                if (*entry)
                    Release(*entry);
            }
            if (mysims.begin)
                Free(mysims.begin);
        }
    }
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// The game's 0x0076FCE0. Shows the panel: the MySims counted, the frame and
// the page pane shown, the name bar hidden, and the simulator's "changed"
// message followed from now on.
void __fastcall Show(char* self, void*)
{
    void* const parent = ParentWindow(self);
    Recount(self, nullptr);
    ShowChild(parent, kFrame, 1);
    ShowPane(ParentWindow(self), kPagePane);
    ShowChild(parent, kNameBar, 0);
    if (void* const simulator = At<void*>(self, panel::kSimulator))
        Virtual<void>(simulator, 0x1C, uint32_t{0}); // TrackLocation
    At<uint8_t>(self, panel::kWatching) = 1;
}

bool InstallMySimPanel3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[23] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F76E500, reinterpret_cast<void*>(&OtherDoWinProcMessage)},
        {addr::SC4UI_F76E6B0, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4UI_F76E710, reinterpret_cast<void*>(&ScriptSelectCar)},
        {addr::SC4UI_F76E760, reinterpret_cast<void*>(&ScriptSelectPedestrian)},
        {addr::SC4UI_F76E7B0, reinterpret_cast<void*>(&ShowThirdPane)},
        {addr::SC4UI_F76E830, reinterpret_cast<void*>(&OfferMoveIn)},
        {addr::SC4UI_F76E8E0, reinterpret_cast<void*>(&Hide)},
        {addr::SC4UI_F76E9B0, reinterpret_cast<void*>(&EraseSubtree)},
        {addr::SC4UI_F76E9F0, reinterpret_cast<void*>(&Start)},
        {addr::SC4UI_F76ED10, reinterpret_cast<void*>(&Stop)},
        {addr::SC4UI_F76EE90, reinterpret_cast<void*>(&SetVehiclePicture)},
        {addr::SC4UI_F76EF40, reinterpret_cast<void*>(&RemoveAdvice)},
        {addr::SC4UI_F76EFB0, reinterpret_cast<void*>(&AddAdvice)},
        {addr::SC4UI_F76F010, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F76F0F0, reinterpret_cast<void*>(&TargetQueryInterface)},
        {addr::SC4UI_F76F120, reinterpret_cast<void*>(&CountedQueryInterface)},
        {addr::SC4UI_F76F150, reinterpret_cast<void*>(&CountedDelete)},
        {addr::SC4UI_F76F160, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F76F240, reinterpret_cast<void*>(&Delete)},
        {addr::SC4UI_F76F260, reinterpret_cast<void*>(&SetAdvisor)},
        {addr::SC4UI_F76F450, reinterpret_cast<void*>(&ShowDetails)},
        {addr::SC4UI_F76F7C0, reinterpret_cast<void*>(&Recount)},
        {addr::SC4UI_F76FCE0, reinterpret_cast<void*>(&Show)},
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

const bool g_mySimPanel3Registered = registry::Register({
    "ui.mysim_panel.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F76F7C0,
    "Our MySim panel constructor, start and stop, show and hide, the count of MySims, the details page and the "
    "advice list keeping",
    false,
    InstallMySimPanel3,
});

} // namespace
} // namespace opensc4::ui
