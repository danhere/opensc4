// The city view (cSC4View3DWin), fourth part: 0x007EC980 to 0x007EF200. What
// the range holds:
//
//   - the missions menu: 0x007EC980 makes a menu item for every mission of a
//     list that has the property 0x8BBD735E (name, description, icon, the
//     mission's ID; command 0x0BB2E43E), and 0x007EDCD0 puts them into a new
//     menu window and hands it to the view's pop-up menu maker 0x007EAEB0;
//   - two interface methods: slot 47 AssertGlobalEffectTrigger 0x007ECCE0 (a
//     count per trigger in the hash map at +0x428; the first assertion sends
//     message 0xAA375FE8) and slot 56 ShowMissionStatusPanel 0x007ECF60 (loads
//     a layout as the panel at +0x40C and puts it at the view's right edge);
//   - 0x007ED0F0, what the view does when a city has been loaded: zoom
//     limits, the camera control told the city's size, the clock, the zoom
//     buttons' pictures and curves, three button groups, the default input
//     control (0x3C bytes, constructor 0x004B8690), the tips window, and the
//     loading picture let go;
//   - 0x007ED720, the buttons that open the view's build menus and its other
//     pop-ups, by button ID;
//   - 0x007EDEB0, the view's own start: it loads the main layout and every
//     panel (five panel layouts, the ticker, the zoom panel, the map window,
//     the graphs window, the style panel), makes the objects that run them
//     and adds the info tip, the overlay bitmap and the advice popup;
//   - 0x007EDE30, a cdecl forwarder to 0x007ECDA0 for a caller that wants a
//     plain function.
//
// Replacement, off by default:
//
//   ui.cityview.part4: everything named above.
//
// Not functions: 0x007ECD50 (the "no memory" end of 0x007ECCE0), 0x007EE230
// and 0x007EE260 (cases of the jump table at 0x007EF1D8 in 0x007EDEB0) and
// 0x007EE3F0 (a case of its second table at 0x007EF1EC): 0x007EDEB0 runs from
// its start to 0x007EF1D6, and the two tables end the range.
//
// Still the game's: 0x007ECDA0 (sets one of the settings object's named
// values from a text; the texts are converted by functions that throw, and
// the function catches what they throw: a real C++ catch, not an unwinding
// frame) and 0x007EDE50 (the clear() of a list of string pairs: template code
// that 0x00813E5A, another class, calls too).
//
// Nothing here draws. 0x007ED0F0 calls the camera control (PostSetUp,
// SetCitySize) as the original does; that code is the renderer's.
//
// The view, as far as this file touches it: +0 the interface
// (cISC4View3DWin, vtable 0x00ABCE40), +8 the part windows report to, +0xC
// the window (vtable 0x00ABCBC0; its area at +0xB4 to +0xC0 of the view),
// +0xE5 a byte, +0xF0 the camera control, +0x154 the simulator (held),
// +0x15C a byte, +0x18C the main layout's window and +0x190 its cIGZWinGen,
// +0x194 a check box, +0x198 every panel's window (a vector of held
// pointers), +0x1A4 five vectors of 0xC bytes with each panel group's
// windows, +0x1E0 the five panels' cIGZWinGen, +0x1F4 a dword, +0x2A4,
// +0x2AC, +0x2B4, +0x2BC and +0x2CC the build menus' holders, +0x2D8 the
// panels' window procedure object (8 bytes), +0x2DC the news ticker, +0x2E4
// the MySim panel, +0x2E8 the budget panel, +0x2F8 the graphs window's
// object, +0x2FC the style panel's, +0x300 the map window, +0x304 the graphs
// window, +0x308 the style panel's window, +0x30C the zoom panel, +0x310 to
// +0x318 three text controls, +0x31C the advice popup, +0x36C, +0x378 and
// +0x37C three numbers of the layout, +0x3D0 an image buffer, +0x3D4 the
// overlay bitmap window, +0x3D8 the info tip, +0x3E0 the tip's picture,
// +0x404 the loading picture, +0x40C the mission status panel, +0x428 the
// effect triggers' counts.
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

namespace view { // the city view
constexpr std::size_t kTarget = 0x008;
constexpr std::size_t kWindow = 0x00C;
constexpr std::size_t kAreaLeft = 0x0B4;
constexpr std::size_t kAreaTop = 0x0B8;
constexpr std::size_t kAreaRight = 0x0BC;
constexpr std::size_t kAreaBottom = 0x0C0;
constexpr std::size_t kFlagE5 = 0x0E5;       // byte
constexpr std::size_t kCameraControl = 0x0F0;
constexpr std::size_t kSimulator = 0x154;
constexpr std::size_t kCityReady = 0x15C;    // byte
constexpr std::size_t kLayout = 0x18C;
constexpr std::size_t kLayoutGen = 0x190;
constexpr std::size_t kCheck = 0x194;
constexpr std::size_t kPanels = 0x198;       // vector of held windows
constexpr std::size_t kGroups = 0x1A4;       // five vectors of windows
constexpr std::size_t kPanelGens = 0x1E0;    // five cIGZWinGen
constexpr std::size_t kField1F4 = 0x1F4;
constexpr std::size_t kMenuA = 0x2A4;
constexpr std::size_t kMenuB = 0x2AC;
constexpr std::size_t kMenuC = 0x2B4;
constexpr std::size_t kMenuD = 0x2BC;
constexpr std::size_t kMenuE = 0x2CC;
constexpr std::size_t kPanelProc = 0x2D8;
constexpr std::size_t kTicker = 0x2DC;
constexpr std::size_t kMySimPanel = 0x2E4;
constexpr std::size_t kBudgetPanel = 0x2E8;
constexpr std::size_t kGraphs = 0x2F8;
constexpr std::size_t kStylePanel = 0x2FC;
constexpr std::size_t kMapWindow = 0x300;
constexpr std::size_t kGraphsWindow = 0x304;
constexpr std::size_t kStyleWindow = 0x308;
constexpr std::size_t kZoomPanel = 0x30C;
constexpr std::size_t kTextA = 0x310;
constexpr std::size_t kTextB = 0x314;
constexpr std::size_t kTextC = 0x318;
constexpr std::size_t kAdvicePopup = 0x31C;
constexpr std::size_t kScaledHeight = 0x36C;
constexpr std::size_t kFrameNumberA = 0x378;
constexpr std::size_t kFrameNumberB = 0x37C;
constexpr std::size_t kBuffer = 0x3D0;
constexpr std::size_t kBitmap = 0x3D4;
constexpr std::size_t kInfoTip = 0x3D8;
constexpr std::size_t kTipPicture = 0x3E0;
constexpr std::size_t kLoadingPicture = 0x404;
constexpr std::size_t kMissionPanel = 0x40C;
constexpr std::size_t kTriggers = 0x428;     // hash map: +4 and +8 the buckets
} // namespace view

namespace item { // a menu item (ui_panels_1.cpp)
constexpr std::size_t kName = 0x04;
constexpr std::size_t kDescription = 0x18;
constexpr std::size_t kCommand = 0x34;
constexpr std::size_t kId = 0x38;
} // namespace item

constexpr uint32_t kWinIid = 0x22BA0121;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kCheckIid = 0x8810;
constexpr uint32_t kGroupIid = 0x89E1574C;
constexpr uint32_t kZoomButtonIid = 0xCA5C2F84;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kPngType = 0x856DDBAC;
constexpr uint32_t kUiGroup = 0x96A006B0;
constexpr uint32_t kImageGroup = 0x46A006B0;

constexpr uint32_t kMissionCommand = 0x0BB2E43E;
constexpr uint32_t kMenuKindProperty = 0x4A2B2462;

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

// An image by resource key (0x30 bytes).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// A node of the game's red-black trees: a set's ends after `value` (0x14
// bytes), a map's has `second` too (0x18 bytes).
struct TreeNode {
    uint8_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    void* value;
    void* second;
};
static_assert(sizeof(TreeNode) == 0x18);

struct Tree {
    TreeNode* head;
    uint32_t count;
};

// The game's vector of pointers.
struct PointerVector {
    void** begin;
    void** end;
    void** capacity;
};
static_assert(sizeof(PointerVector) == 0xC);

// A menu's entry while the items are sorted: the order first.
struct OrderedItem {
    uint32_t order;
    char* item;
};

// A curve as the zoom buttons take it (0x10 bytes; the first dword is its
// points' memory).
struct Curve {
    void* points;
    uint32_t rest[3];
};
static_assert(sizeof(Curve) == 0x10);

// A group of buttons the loaded city's start sets up (0x24 bytes).
struct ButtonGroup {
    uint32_t id;
    uint32_t count;
    uint32_t buttons[6];
    uint8_t colour[3];
};
static_assert(sizeof(ButtonGroup) == 0x24);

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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The inlined assignment to a reference holder, as this range has it: a
// null object takes no reference.
void Hold(void*& holder, void* object)
{
    void* const old = holder;
    if (object == old)
        return;
    if (object)
        AddRef(object);
    holder = object;
    if (old)
        Release(old);
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.capacity = buffer + 8;
    text.body.begin = buffer;
    text.body.end = buffer;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// The inlined ++ of a tree iterator.
TreeNode* Next(TreeNode* node)
{
    if (TreeNode* down = node->right) {
        while (down->left)
            down = down->left;
        return down;
    }
    TreeNode* at = node;
    TreeNode* up = node->parent;
    while (at == up->right) {
        at = up;
        up = up->parent;
    }
    return at->right != up ? up : at;
}

// The inlined constructor of a set or a map: an empty head node.
void InitTree(Tree& tree, uint32_t nodeSize)
{
    tree.head = nullptr;
    tree.head = Cdecl<TreeNode*>(addr::SC4_stl_allocate, nodeSize);
    tree.count = 0;
    tree.head->colour = 0;
    tree.head->parent = nullptr;
    tree.head->left = tree.head;
    tree.head->right = tree.head;
}

// A new framework message; null when there is no memory, which the callers
// do not check, as in the original.
void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// The inlined push_back of a vector of plain pointers; a full vector grows
// through the game's 0x0051CA60.
void PushBack(PointerVector& vector, void* value)
{
    void** const end = vector.end;
    if (end != vector.capacity) {
        if (end)
            *end = value;
        vector.end += 1;
        return;
    }
    uint8_t tag = 0;
    This<void>(addr::SC4PtrVector_InsertN, &vector, end, &value, &tag, uint32_t{1}, uint32_t{1});
}

// The inlined push_back of a vector of held pointers; a full vector grows
// through the game's 0x00430A90.
void PushBackHeld(PointerVector& vector, void* object)
{
    void** const end = vector.end;
    if (end != vector.capacity) {
        if (end) {
            *end = object;
            AddRef(object);
        }
        vector.end += 1;
        return;
    }
    uint8_t tag = 0;
    This<void>(addr::SC4RefPtrVector_InsertN, &vector, end, &object, &tag, uint32_t{1}, uint32_t{1});
}

// The same push_back where the original has the growing inlined too (twice
// in 0x007EDEB0, at 0x007EE704 and 0x007EE8D4, the same code but for the
// registers): twice the size, at least one, the old pointers copied with a
// reference each and then released.
void PushBackHeldInline(PointerVector& vector, void* object)
{
    void** const end = vector.end;
    if (end != vector.capacity) {
        if (end) {
            *end = object;
            AddRef(object);
        }
        vector.end += 1;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(end - vector.begin);
    const uint32_t capacity = (size < 1 ? 1 : size) + size;
    void** const buffer = capacity ? Cdecl<void**>(addr::SC4_stl_allocate, capacity * 4) : nullptr;
    void** to = buffer;
    for (void** from = vector.begin; from != end; ++from, ++to) {
        if (to) {
            void* const held = *from;
            *to = held;
            if (held)
                AddRef(held);
        }
    }
    if (to) {
        *to = object;
        AddRef(object);
    }
    ++to;
    void** const oldEnd = vector.end;
    for (void** old = vector.begin; old != oldEnd; ++old) {
        if (*old)
            Release(*old);
    }
    if (vector.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(vector.begin));
    vector.begin = buffer;
    vector.end = to;
    vector.capacity = buffer + capacity;
}

// --- The missions menu ----------------------------------------------------------------

// The game's 0x007EC980 (ret 8). `source` keeps missions (its slot 28 gives
// a vector of 0x6C-byte entries: +0x30 the mission's exemplar, +0x68 its
// ID); `items` is a multimap by order (nodes of 0x18 bytes). Every entry
// whose exemplar has the property 0x8BBD735E and a name (the text of the
// key in property 0x8A416A99, else the string 0x899AFBAD, else the name
// 0x20) becomes a menu item under its property 0x8A2602B9 (0 when it has
// none): description from 0xCA416AB5, 0x8A2602A9 or 0x20; icon from property
// 0xABE1AF70 when the situation manager's slot 19 says yes for the ID, else
// from 0x8A2602B8, else 0x144161EC; the item's ID is the exemplar's
// instance. The source's slot 37 with the ID says whether the item can be
// chosen; an item that can be has no description. Each item keeps the
// reference taken here. Nothing happens without a situation manager or a
// source. The new item is not checked for null, as in the original.
void __fastcall CollectMissionItems(char* self, void*, void* source, Tree* items)
{
    void* const situations = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4);
    if (!situations || !source)
        return;
    char** const list = Virtual<char**>(source, 0x70);
    char* const end = list[1];
    for (char* entry = list[0]; entry != end; entry += 0x6C) {
        void* const exemplar = At<void*>(entry, 0x30);
        uint32_t own[3] = {0, 0, 0};
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        Virtual<void>(exemplar, 0x2C, &own[0]);
        if (!Virtual<uint8_t>(holder, 0xC, uint32_t{0x8BBD735E}))
            continue;

        GameString name;
        InitString(name);
        GameString description;
        InitString(description);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyText, &name, holder, uint32_t{0x8A416A99}, uint32_t{0}) &&
            !Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, uint32_t{0x899AFBAD}, &name))
            Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, uint32_t{0x20}, &name);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyText, &description, holder, uint32_t{0xCA416AB5}, uint32_t{0}) &&
            !Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, uint32_t{0x8A2602A9}, &description))
            Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, uint32_t{0x20}, &description);

        if (name.body.begin != name.body.end) {
            uint32_t icon = 0x144161EC;
            if (Virtual<uint8_t>(situations, 0x4C, At<uint32_t>(entry, 0x68)))
                Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xABE1AF70}, &icon);
            else
                Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x8A2602B8}, &icon);
            uint32_t key[3] = {kPngType, 0x6A386D26, icon};
            const uint32_t open = Virtual<uint32_t>(source, 0x94, At<uint32_t>(entry, 0x68)) != 0 ? 1 : 0;

            char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x44});
            char* const made = memory ? This<char*>(addr::SC4UI_F79B6B0, memory) : nullptr;
            AddRef(made);
            ImageLoader loader;
            This<void*>(addr::SC4ImageLoader_ConstructKey, &loader, &key[0], uint32_t{1}, uint32_t{0});
            if (void* const image = loader.image) {
                const uint32_t clear =
                    Virtual<uint32_t>(self + view::kWindow, 0x1B0, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
                Virtual<void>(image, 0x5C, clear);
                This<void>(addr::cSC4Occupant_SetPlaceableObject, made, image); // the item's icon at +0x30
            }
            uint32_t order = 0;
            Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x8A2602B9}, &order);
            Virtual<void>(made + item::kName, 0x34, &name);
            if (!open) {
                Virtual<void>(made + item::kDescription, 0x34, &description);
            } else {
                GameString empty;
                void* const text = This<void*>(addr::SC4String_SC4String, &empty);
                Virtual<void>(made + item::kDescription, 0x34, text);
                DestroyString(empty);
            }
            At<uint32_t>(made, item::kId) = own[2];
            At<uint32_t>(made, item::kCommand) = kMissionCommand;
            Virtual<void>(made, 0x28, open);

            OrderedItem ordered;
            ordered.order = order;
            ordered.item = made;
            uint32_t where[2];
            This<void*>(addr::cSC4Advisor_F40F080, items, &where[0], &ordered);
            This<void>(addr::SC4ImageLoader_Destruct, &loader);
        }
        DestroyString(description);
        DestroyString(name);
    }
}

// The game's 0x007EDCD0 (ret 0x10). With a `source` of missions: a new menu
// window (0x140 bytes) gets the items CollectMissionItems makes, in their
// order, and goes to the view's pop-up menu maker 0x007EAEB0 with the menu
// ID `command` + 0x6BB2E4BC and the layout 0x14215EDD. Without a source
// nothing is shown. The new menu is not checked for null before the items
// are added, as in the original.
void __fastcall ShowMissionMenu(char* self, void*, void* anchor, uint32_t command, void* source, uint32_t last)
{
    Tree items;
    InitTree(items, 0x18);
    if (source) {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x140});
        char* const menu = memory ? This<char*>(addr::SC4UI_F79B500, memory) : nullptr;
        if (menu)
            AddRef(menu);
        CollectMissionItems(self, nullptr, source, &items);
        for (TreeNode* node = items.head->left; node != items.head; node = Next(node)) {
            Virtual<void>(menu, 0x10, node->second);
            Release(node->second);
        }
        This<void>(addr::SC4UI_F7EAEB0, self, command + 0x6BB2E4BC, anchor, command, static_cast<void*>(menu),
                   uint32_t{0x14215EDD}, last);
        if (menu)
            Release(menu);
    }
    if (items.count) {
        This<void>(addr::SC4UI_F78DD40, &items, items.head->parent);
        items.head->left = items.head;
        items.head->parent = nullptr;
        items.head->right = items.head;
        items.count = 0;
    }
    if (items.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(items.head));
}

// --- Two interface methods ------------------------------------------------------------

// The game's 0x007ECCE0 (slot 47, AssertGlobalEffectTrigger, ret 4). Counts
// the assertions of a trigger. The first one puts 1 into the map and sends
// (not posts) message 0xAA375FE8 with the trigger and 1; later ones only
// count. Returns the new count, or for the first one what the message's
// Release returns. The map's bucket count is not checked for 0 and the
// message not for null, as in the original.
uint32_t __fastcall AssertGlobalEffectTrigger(char* self, void*, uint32_t trigger)
{
    char* const map = self + view::kTriggers;
    void** const buckets = At<void**>(map, 0x4);
    const uint32_t count = static_cast<uint32_t>(At<void**>(map, 0x8) - buckets);
    for (char* node = static_cast<char*>(buckets[trigger % count]); node; node = At<char*>(node, 0x0)) {
        if (At<uint32_t>(node, 0x4) == trigger) {
            const uint32_t now = At<uint32_t>(node, 0x8) + 1;
            At<uint32_t>(node, 0x8) = now;
            return now;
        }
    }
    *This<uint32_t*>(addr::SC4Uint32HashMap_Lookup, map, &trigger) = 1;
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, uint32_t{0xAA375FE8});
    Virtual<void>(message, 0x2C, trigger);
    Virtual<void>(message, 0x40, uint32_t{1});
    Virtual<void>(server, 0xC, message);
    return Release(message);
}

// The game's 0x007ECF60 (slot 56, ShowMissionStatusPanel, ret 4). When no
// panel is up and `layout` is not 0: loads window 0x10000006 of that layout
// under the view's window, shows its child 0xCC1A6983, moves it 8 pixels in
// from the view's right edge and 8 down, makes it its children's target and
// has it report to the view, lengthens its child 0x10000005 to the height of
// that child's child 0x0C3CB614 plus 8, stops keyboard scrolling, fills the
// panel (slot 58) and, when the situation manager has no active situation,
// calls 0x007EBB20 (0, 1). The two children are not checked for null, as in
// the original. The original's EAX at the end is whatever the last call
// left; this returns the same things.
uint32_t __fastcall ShowMissionStatusPanel(char* self, void*, uint32_t layout)
{
    void*& panelHolder = At<void*>(self, view::kMissionPanel);
    if (panelHolder)
        return reinterpret_cast<uint32_t>(panelHolder);
    if (!layout)
        return 0;
    uint32_t key[3] = {0, kUiGroup, layout};
    void* const window = self + view::kWindow;
    Clear(panelHolder);
    Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], window, uint32_t{0x10000006}, kWinIid, &panelHolder);
    if (!panelHolder)
        return 0;
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, panelHolder, uint32_t{0xCC1A6983}, uint32_t{0}, uint32_t{0});
    void* const own = Virtual<void*>(self, 0xC);
    void* const panel = panelHolder;
    const int32_t left = -8 - Virtual<int32_t>(panel, 0xA4);
    const int32_t width = Virtual<int32_t>(own, 0xA4);
    Virtual<void>(panel, 0xE0, left + width, int32_t{8});
    Cdecl<void>(addr::SC4Win_SetChildrenTarget, panelHolder, panelHolder);
    Virtual<void>(panelHolder, 0x1D0, static_cast<void*>(self + view::kTarget));

    void* const frame = Virtual<void*>(panelHolder, 0x88, uint32_t{0x10000005});
    if (frame)
        AddRef(frame);
    void* const text = Virtual<void*>(frame, 0x88, uint32_t{0x0C3CB614});
    if (text)
        AddRef(text);
    Virtual<void>(frame, 0xD0, Virtual<int32_t>(text, 0xB0) + 8);
    Virtual<void>(text, 0x118);
    Virtual<void>(self, 0x8C);
    Virtual<void>(self, 0xE8);
    void* const situations = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4);
    if (situations) {
        void* const active = Virtual<void*>(situations, 0x18);
        if (!active)
            This<void>(addr::SC4UI_F7EBB20, self, active, uint32_t{1});
    }
    Release(text);
    return Release(frame);
}

// --- A city has been loaded -----------------------------------------------------------

// The game's 0x007ED0F0 (no arguments). What the view does once its city
// is there:
//
//   - 0x007EABF0, the window's slot 24, its flag 0x800 off, zoom limits 0
//     to 5, the camera control's PostSetUp, the simulator taken (held) when
//     the view has none, the camera control told the city's size;
//   - the clock: day and night notifications on, its mode from the
//     preferences' first byte, and 0x007EC0B0 with the hour and the minute;
//   - 0x007E6E40, the zoom buttons 0x4992046A and 0xE9920494 enabled by
//     CanZoomIn and CanZoomOut, 0x007EA6E0, 0x007EAAA0, 0x007E6EE0;
//   - +0x36C: the height of picture 0x14015549 times (mayor rating + 100)
//     over 200; +0x378 and +0x37C from the child 0xCA5A415E of the window
//     0xE9889775 (its slots 43 and 44);
//   - 0x007E8510, then 0x007EBF30 for an established city and 0x007E97A0
//     for a new one;
//   - three button groups of the view's window (0x09D27EB0, 0x29D27EC0,
//     0x49D27ED0) get a colour and their buttons;
//   - the six zoom panel buttons (IDs at 0x00B0E304) get the pictures
//     0x14015580 and 0x14015584 and, when the exemplar of group 0x690F693F
//     with the button's ID as instance has them, a curve (property
//     0x6A6564F6) under the number in property 0x6A6564F4;
//   - a new input control (0x3C bytes) becomes a persistent one (slot 28),
//     0x0099BDE4 on the window, the tips window is opened when it is not up
//     and tips are wanted, 0x007EABF0 again, and the loading picture at
//     +0x404 is ended and let go.
//
// The picture 0x14015549, the zoom panel and the simulator global are not
// checked for null, as in the original.
void __fastcall CityLoaded(char* self, void*)
{
    This<void>(addr::SC4UI_F7EABF0, self);
    void* const window = self + view::kWindow;
    Virtual<void>(window, 0x60);
    Virtual<void>(window, 0x110, uint32_t{0x800}, uint32_t{0});
    Virtual<void>(self, 0x2C, int32_t{0}, int32_t{5});
    This<void>(addr::cSC4CameraControl_PostSetUp, At<void*>(self, view::kCameraControl));
    const bool hasSimulator = At<void*>(self, view::kSimulator) != nullptr;
    At<uint8_t>(self, view::kCityReady) = 1;
    void* const city = Global<void*>(addr::SC4_City);
    if (!hasSimulator) {
        void* const simulator = Global<void*>(addr::SC4_SimulatorService);
        At<void*>(self, view::kSimulator) = simulator;
        AddRef(simulator);
    }
    const float sizeZ = static_cast<float>(Virtual<double>(city, 0x168));
    const float sizeX = static_cast<float>(Virtual<double>(city, 0x164));
    This<void>(addr::cSC4CameraControl_SetCitySize, At<void*>(self, view::kCameraControl), sizeX, sizeZ);

    void* const app = Global<void*>(addr::SC4_Service43C94);
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    const uint8_t* const preferences = Virtual<const uint8_t*>(app, 0x98);
    Virtual<void>(clock, 0x74, uint32_t{1});
    Virtual<void>(clock, 0x14, static_cast<uint32_t>(preferences[0]));
    const uint32_t minute = Virtual<uint32_t>(clock, 0x34);
    const uint32_t hour = Virtual<uint32_t>(clock, 0x28);
    This<void>(addr::SC4UI_F7EC0B0, self, hour, minute);
    This<void>(addr::SC4UI_F7E6E40, self);
    const uint32_t canZoomIn = Virtual<uint32_t>(self, 0x30);
    Cdecl<void>(addr::SC4Win_EnableChild, At<void*>(self, view::kLayout), uint32_t{0x4992046A}, canZoomIn);
    const uint32_t canZoomOut = Virtual<uint32_t>(self, 0x34);
    Cdecl<void>(addr::SC4Win_EnableChild, At<void*>(self, view::kLayout), uint32_t{0xE9920494}, canZoomOut);
    This<void>(addr::SC4UI_F7EA6E0, self);
    This<void>(addr::SC4UI_F7EAAA0, self);
    This<void>(addr::SC4UI_F7E6EE0, self);

    ImageLoader scaled;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &scaled, uint32_t{0}, kImageGroup, uint32_t{0x14015549},
                uint32_t{1}, uint32_t{0});
    void* const aura = Virtual<void*>(city, 0xCC);
    const int64_t rating = static_cast<int32_t>(Virtual<int8_t>(aura, 0x20)) + 100;
    const uint32_t height = Virtual<uint32_t>(scaled.image, 0x28);
    At<int32_t>(self, view::kScaledHeight) =
        static_cast<int32_t>(static_cast<int64_t>(static_cast<uint64_t>(height)) * rating / 200);

    void* gen = nullptr;
    void* text = nullptr;
    if (Virtual<uint8_t>(window, 0x94, uint32_t{0xE9889775}, kWinGenIid, &gen)) {
        void* const frame = Virtual<void*>(gen, 0xC);
        Clear(text);
        if (Virtual<uint8_t>(frame, 0x94, uint32_t{0xCA5A415E}, uint32_t{0xC12CEA13}, &text)) {
            At<uint32_t>(self, view::kFrameNumberA) = Virtual<uint32_t>(Virtual<void*>(text, 0xC), 0xAC);
            At<uint32_t>(self, view::kFrameNumberB) = Virtual<uint32_t>(Virtual<void*>(text, 0xC), 0xB0);
        }
    }
    This<void>(addr::SC4UI_F7E8510, self);
    if (Virtual<uint8_t>(city, 0x58))
        This<void>(addr::SC4UI_F7EBF30, self);
    else
        This<void>(addr::SC4UI_F7E97A0, self);

    ButtonGroup groups[3] = {
        {0x09D27EB0, 3, {0x1010, 0x1020, 0x1030, 0, 0, 0}, {0x00, 0xC8, 0x00}},
        {0x29D27EC0, 5, {0x3110, 0x3120, 0x3320, 0x3130, 0x3330, 0}, {0x00, 0x00, 0xC8}},
        {0x49D27ED0, 3, {0x4200, 0x4300, 0x4400, 0, 0, 0}, {0xC8, 0xC8, 0x00}},
    };
    void* found = nullptr;
    for (ButtonGroup& group : groups) {
        if (Virtual<uint8_t>(window, 0x94, group.id, kGroupIid, &found)) {
            const uint32_t colour = 0xFF000000 | static_cast<uint32_t>(group.colour[0]) << 16 |
                                    static_cast<uint32_t>(group.colour[1]) << 8 | group.colour[2];
            Virtual<void>(found, 0x10, colour);
            Virtual<void>(found, 0x14, &group.buttons[0], group.count);
            Virtual<void>(static_cast<char*>(found) + 4, 0x158, window);
            Release(found);
        }
    }

    uint32_t key[3] = {kPngType, kImageGroup, 0x14015580};
    ImageLoader pictureA;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &pictureA, &key[0], uint32_t{1}, uint32_t{0});
    key[0] = kPngType;
    key[1] = kImageGroup;
    key[2] = 0x14015584;
    ImageLoader pictureB;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &pictureB, &key[0], uint32_t{1}, uint32_t{0});

    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    // Where the original keeps the exemplar the preferences' pointer was
    // stored before; a failed fetch leaves it and nothing reads it.
    void* exemplar = const_cast<uint8_t*>(preferences);
    for (uint32_t i = 0; i < 6; ++i) {
        found = nullptr;
        const uint32_t id = Global<uint32_t>(addr::SC4View3DWin_ZoomPanelButtons + 4 * i);
        if (Virtual<uint8_t>(At<void*>(self, view::kZoomPanel), 0x94, id, kZoomButtonIid, &found)) {
            Virtual<void>(found, 0x10, pictureA.image, pictureB.image);
            uint32_t number = 0x0A5CBF37;
            key[2] = Virtual<uint32_t>(static_cast<char*>(found) + 4, 0xFC);
            key[0] = kExemplarType;
            key[1] = 0x690F693F;
            if (Virtual<uint8_t>(resources, 0xC, &key[0], kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0})) {
                Curve curve;
                This<void*>(addr::SC4Curve_Init, &curve);
                void* const holder = Virtual<void*>(exemplar, 0x1C);
                if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x6A6564F4}, &number) &&
                    This<uint8_t>(addr::SC4Curve_LoadFromExemplar, &curve, &key[0], uint32_t{0x6A6564F6}))
                    Virtual<void>(found, 0x14, number, &curve);
                Release(exemplar);
                if (curve.points)
                    Cdecl<void>(addr::SC4_Free, curve.points);
            }
        }
        if (found)
            Release(found);
    }

    At<uint8_t>(self, view::kFlagE5) = 0;
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x3C});
    void* const control = memory ? This<void*>(addr::SC4MissionIconControl_Construct, memory) : nullptr;
    if (control)
        AddRef(control);
    Virtual<void>(self, 0x70, control);
    This<void>(addr::SC4Win_F99BDE4, window);
    if (!Cdecl<uint8_t>(addr::SC4UI_F78C760) && Cdecl<uint8_t>(addr::SC4UI_F78CCE0))
        Cdecl<void>(addr::SC4UI_F78C8C0, uint32_t{0});
    This<void>(addr::SC4UI_F7EABF0, self);
    if (void* const loading = At<void*>(self, view::kLoadingPicture)) {
        This<void>(addr::SC4UI_F777760, loading);
        Release(At<void*>(self, view::kLoadingPicture));
        At<void*>(self, view::kLoadingPicture) = nullptr;
    }
    if (control)
        Release(control);
    This<void>(addr::SC4ImageLoader_Destruct, &pictureB);
    This<void>(addr::SC4ImageLoader_Destruct, &pictureA);
    if (gen)
        Release(gen);
    if (text)
        Release(text);
    This<void>(addr::SC4ImageLoader_Destruct, &scaled);
}

// --- The menu buttons -----------------------------------------------------------------

// The sound of four of the buttons: the sound service's slot 7 with 1, the
// sound of the camera's zoom (table 0x00ABC05C by the camera view's +0xC), a
// new number from the game's counter 0x004F7BB0 and two zeros. Returns the
// service with a reference taken, or null. 0x004ECD60 gives a static object
// the original fetches and does nothing with.
void* ClickSound()
{
    void* const sound = Global<void*>(addr::SC4_Sound43CB8);
    if (sound) {
        AddRef(sound);
        Cdecl<void*>(addr::SC4Sound_StaticObject);
        char* const camera = Global<char*>(addr::SC4_CameraView);
        const uint32_t number = Cdecl<uint32_t>(addr::SC4Sound_NextInstance);
        const uint32_t which = Global<uint32_t>(addr::SC4View3DWin_ZoomSounds + 4 * At<uint32_t>(camera, 0xC));
        Virtual<void>(sound, 0x1C, uint32_t{1}, which, number, uint32_t{0}, uint32_t{0});
    }
    return sound;
}

// Posts message 0x470BD214 with one number.
void PostButtonMessage(uint32_t value)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, uint32_t{0x470BD214});
    Virtual<void>(message, 0x2C, value);
    Virtual<void>(server, 0x10, message, uint32_t{0});
    Release(message);
}

// The terrain's slot 48 with `on` and what its slot 47 gives.
void TerrainSwitch(uint32_t on)
{
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!terrain)
        return;
    const float level = static_cast<float>(Virtual<double>(terrain, 0xBC));
    Virtual<void>(terrain, 0xC0, on, level);
}

// The game's 0x007ED720 (ret 8). A button of the view's layout was pressed:
// `anchor` is what the view's handler got with it, the button's ID is at +8
// of `message`.
//
//   0xC99231DA, 0x152231F3, 0x6A3D638C, 0x29923215: a build menu of the
//     items whose kind (property 0x4A2B2462) is the button's, through
//     0x007EC620 with the menu's holder (+0x2A4, +0x2AC, +0x2B4, +0x2BC);
//   0x4A22EA06: the same through 0x007EC680 (kind property 0xEA5AD08D,
//     holder +0x2CC);
//   0x36, 0x8A32DDDB, 0x49E95D2B, 0x8991EE08: a pop-up through 0x007E5C10;
//   0x0A32DDD2, 0x899231E6: the tertiary menu closed, then 0x007E5910 with
//     what the terrain's slot 58 gives for 6 (and whether it was the second
//     button);
//   0x4A32DDC7, 0xEA32DDBB, 0xEA32DDE5: the tertiary menu closed, then
//     0x007E5910 (0x0A33E37B, 0xEA33E2FD or 0x4A524342, 0);
//   0x4A551A6B: the secondary menu closed, then the terrain's slot 89
//     (1, 1, 1);
//   0x0AA44502, 0x0AA44503: a click sound and message 0x470BD214 posted
//     with 0 or 1;
//   0x0AA44504, 0x0AA44505: a click sound and the terrain's slot 48 with 1
//     or 0.
//
// Where the terrain writes its answer the original has `anchor`'s own
// argument slot; it starts as `anchor` here too.
void __fastcall LayoutButton(char* self, void*, void* anchor, char* message)
{
    const uint32_t id = At<uint32_t>(message, 0x8);
    switch (id) {
    case 0x0A32DDD2:
    case 0x899231E6: {
        Virtual<void>(self, 0xF0);
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        if (!terrain)
            return;
        uint32_t what = reinterpret_cast<uint32_t>(anchor);
        if (!Virtual<uint8_t>(terrain, 0xE8, uint32_t{6}, &what))
            return;
        This<void>(addr::SC4UI_F7E5910, self, what, static_cast<uint32_t>(id == 0x899231E6));
        return;
    }
    case 0x0AA44502:
    case 0x0AA44503: {
        void* const sound = ClickSound();
        PostButtonMessage(id == 0x0AA44502 ? 0 : 1);
        if (sound)
            Release(sound);
        return;
    }
    case 0x0AA44504:
    case 0x0AA44505: {
        void* const sound = ClickSound();
        TerrainSwitch(id == 0x0AA44504 ? 1 : 0);
        if (sound)
            Release(sound);
        return;
    }
    case 0xC99231DA:
        This<void>(addr::SC4UI_F7EC620, self, anchor, id, uint32_t{0x6A33D682}, kMenuKindProperty,
                   static_cast<void*>(self + view::kMenuA));
        return;
    case 0x152231F3:
        This<void>(addr::SC4UI_F7EC620, self, anchor, id, uint32_t{0xEA33D694}, kMenuKindProperty,
                   static_cast<void*>(self + view::kMenuB));
        return;
    case 0x6A3D638C:
        This<void>(addr::SC4UI_F7EC620, self, anchor, id, uint32_t{0x8A3F131B}, kMenuKindProperty,
                   static_cast<void*>(self + view::kMenuC));
        return;
    case 0x29923215:
        This<void>(addr::SC4UI_F7EC620, self, anchor, id, uint32_t{0x8A3143B7}, kMenuKindProperty,
                   static_cast<void*>(self + view::kMenuD));
        return;
    case 0x4A22EA06:
        This<void>(addr::SC4UI_F7EC680, self, anchor, id, uint32_t{0x8A2602BB}, uint32_t{0xEA5AD08D},
                   static_cast<void*>(self + view::kMenuE));
        return;
    case 0x36:
    case 0x8A32DDDB:
        This<void>(addr::SC4UI_F7E5C10, self, uint32_t{0xAAA44448}, uint32_t{0xCA35CBED}, anchor, id);
        return;
    case 0x49E95D2B:
        This<void>(addr::SC4UI_F7E5C10, self, uint32_t{0xE9923283}, uint32_t{0x49923239}, anchor, id);
        return;
    case 0x8991EE08:
        This<void>(addr::SC4UI_F7E5C10, self, uint32_t{0x09923283}, uint32_t{0x49923239}, anchor, id);
        return;
    case 0x4A32DDC7:
        Virtual<void>(self, 0xF0);
        This<void>(addr::SC4UI_F7E5910, self, uint32_t{0x0A33E37B}, uint32_t{0});
        return;
    case 0xEA32DDBB:
        Virtual<void>(self, 0xF0);
        This<void>(addr::SC4UI_F7E5910, self, uint32_t{0xEA33E2FD}, uint32_t{0});
        return;
    case 0xEA32DDE5:
        Virtual<void>(self, 0xF0);
        This<void>(addr::SC4UI_F7E5910, self, uint32_t{0x4A524342}, uint32_t{0});
        return;
    case 0x4A551A6B: {
        Virtual<void>(self, 0xEC);
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        if (terrain)
            Virtual<void>(terrain, 0x164, uint32_t{1}, uint32_t{1}, uint32_t{1});
        return;
    }
    default:
        return;
    }
}

// The game's 0x007EDE30 (cdecl): 0x007ECDA0 of the view given third, with
// the first two arguments. 0x007EF950 hands this function to a text parser.
uint8_t __cdecl SetSettingForward(void* name, void* value, void* self)
{
    return This<uint8_t>(addr::SC4UI_F7ECDA0, self, name, value);
}

// --- The view's start -----------------------------------------------------------------

// One of the two extra frames a panel layout brings (the game's 0x007EE40D
// and 0x007EE513, the same but for the frame's ID and the group it joins):
// the window behind the child `id` of the view's window joins every panel's
// vector and a group, gets the panels' window procedure, reports to the
// view and is put through slot 24 with its flag 0x800 on.
void AttachFrame(char* self, uint32_t id, std::size_t group, bool throughGame)
{
    void* gen = nullptr;
    if (Virtual<uint8_t>(self + view::kWindow, 0x94, id, kWinGenIid, &gen)) {
        void* frame = Virtual<void*>(gen, 0xC);
        void* held = frame;
        if (frame)
            AddRef(frame);
        This<void>(addr::SC4RefVector_PushBack, self + view::kPanels, &held);
        if (frame)
            Release(frame);
        if (throughGame)
            This<void>(addr::SC4PtrVector_PushBack, self + group, &frame);
        else
            PushBack(At<PointerVector>(self, group), frame);
        Virtual<void>(gen, 0x1C, At<void*>(self, view::kPanelProc));
        Virtual<void>(frame, 0x1D0, static_cast<void*>(self + view::kTarget));
        Virtual<void>(frame, 0x110, uint32_t{0x800}, uint32_t{1});
        Virtual<void>(frame, 0x60);
        Virtual<void>(frame, 0x110, uint32_t{0x800}, uint32_t{0});
        Cdecl<void>(addr::SC4UIWindows_Unpaint, frame);
    }
    if (gen)
        Release(gen);
}

// What the original does four times for a panel of its own layout: the
// view's children noted, the holder emptied, window `id` of `layout` loaded
// into it, and the windows that came with it placed.
void LoadPanel(char* self, Tree& before, std::size_t field, uint32_t layout, uint32_t id)
{
    void* const window = self + view::kWindow;
    Cdecl<uint32_t>(addr::SC4UIWindows_Collect, window, &before);
    void*& holder = At<void*>(self, field);
    Clear(holder);
    uint32_t key[3] = {0, kUiGroup, layout};
    Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], window, id, kWinIid, &holder);
    Cdecl<uint32_t>(addr::SC4UIWindows_NewSince, window, &before, &before);
    Cdecl<uint8_t>(addr::SC4UIWindows_Prepare, &before, static_cast<void*>(nullptr));
}

// The game's 0x007EDEB0 (no arguments). Loads the view's interface:
//
//   - the main layout (child 0x0987B48F of layout 0xC973B411) at +0x18C,
//     reporting to the view, with no window procedure and an empty area of
//     its own, placed at the bottom left (5 pixels in, or a third of the
//     view's width further when the view is more than three times as wide
//     as high);
//   - a check box (class 0x22ECFC47) at +0x194 with picture 0x53244588 and
//     the text 0x8A51545D of group 0x6A231EAA, ID 0x43, under the layout;
//   - the panels' shared window procedure object (+0x2D8) and the news
//     ticker (+0x2DC);
//   - five panel layouts, each kept in the vector at +0x198, in its group
//     at +0x1A4 + 0xC * i and as cIGZWinGen at +0x1E0 + 4 * i; the second
//     brings the frame 0xE9889775, the third the MySim panel (+0x2E4) and
//     the frame 0xABB26B0E;
//   - three text controls, the ticker's window (layout 0x2A2AED99) with its
//     parent's child 0xAA231508, the zoom panel (+0x30C), the map window
//     (+0x300, started by 0x0079EA40), the graphs window (+0x304, object
//     +0x2F8) and the style panel (+0x308, object +0x2FC);
//   - the budget panel (+0x2E8), the info tip (+0x3D8) with its two
//     pictures, the overlay bitmap window (+0x3D4) over an image buffer of
//     128 by 32 (+0x3D0), the advice popup (+0x31C), a window of class
//     0xCA5D3294 over the whole view with ID 0x6A5E44B6, and a window
//     procedure object for the child 0xEA8CAD14;
//   - in a city not yet established: +0x1F4 cleared, 0x007E8B90 (0) and the
//     layout's children 0x99887766 and 0x8B96B73E disabled.
//
// Many of the new objects and loaded windows are used without a null check,
// as in the original; a layout that is missing crashes there too.
void __fastcall Start(char* self, void*)
{
    uint32_t key[3] = {0, kUiGroup, 0xC973B411};
    void* const window = self ? self + view::kWindow : nullptr;
    void* const layout = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], window, uint32_t{0x0987B48F});
    At<void*>(self, view::kLayout) = layout;
    if (layout) {
        Virtual<uint8_t>(layout, 0x0, kWinGenIid, &At<void*>(self, view::kLayoutGen));
        if (At<void*>(self, view::kLayout)) {
            Virtual<void>(At<void*>(self, view::kLayout), 0x1D0, static_cast<void*>(self + view::kTarget));
            Virtual<void>(At<void*>(self, view::kLayoutGen), 0x1C, static_cast<void*>(nullptr));
            int32_t area[4] = {0, 0, 0, 0};
            Virtual<void>(At<void*>(self, view::kLayoutGen), 0x2C, &area[0]);
        }
        const uint32_t width = Virtual<uint32_t>(self + view::kWindow, 0xA4);
        const uint32_t height = Virtual<uint32_t>(self + view::kWindow, 0xA8);
        Virtual<uint32_t>(At<void*>(self, view::kLayout), 0xA4);
        const uint32_t own = Virtual<uint32_t>(At<void*>(self, view::kLayout), 0xA8);
        const uint32_t indent = width > height * 3 ? width / 3 : 0;
        Virtual<void>(At<void*>(self, view::kLayout), 0xE0, indent + 5, height - own + 0xB);
        Virtual<void>(At<void*>(self, view::kLayout), 0x114);
        Virtual<void>(At<void*>(self, view::kLayout), 0x110, uint32_t{0x800}, uint32_t{0});
        Cdecl<void>(addr::SC4UIWindows_Unpaint, At<void*>(self, view::kLayout));
    }

    void*& checkWindow = At<void*>(self, view::kCheck);
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    Virtual<uint8_t>(com, 0xC, uint32_t{0x22ECFC47}, kWinIid, &checkWindow);
    void* check = nullptr;
    if (checkWindow && Virtual<uint8_t>(checkWindow, 0x0, kCheckIid, &check)) {
        key[0] = kPngType;
        key[1] = kImageGroup;
        key[2] = 0x53244588;
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructKey, &loader, &key[0], uint32_t{1}, uint32_t{0});
        void* const image = loader.image;
        if (image) {
            const uint32_t clear =
                Virtual<uint32_t>(self + view::kWindow, 0x1B0, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
            Virtual<void>(image, 0x5C, clear);
        }
        Virtual<void>(check, 0x30, uint32_t{2}, image, uint32_t{0});
        Virtual<void>(check, 0x14, uint32_t{0x20}, uint32_t{1});
        Virtual<void>(Virtual<void*>(check, 0xC), 0x110, uint32_t{4}, uint32_t{1});

        // The localized string is 0x20 bytes: a string and three more dwords.
        struct {
            GameString text;
            uint32_t rest[3];
        } localizedStorage;
        GameString& localized = localizedStorage.text;
        char* const made = This<char*>(addr::SC4LocalizedString_SC4LocalizedString, &localizedStorage, uint32_t{0x6A231EAA},
                                       uint32_t{0x8A51545D}, uint32_t{1});
        GameString caption;
        uint8_t tag = 0;
        This<void>(addr::SC4String_CopyBuffer, &caption.body, made ? made + 4 : nullptr, uint32_t{0},
                   uint32_t{0xFFFFFFFF}, &tag);
        caption.vtable = addr::SC4String_Vtable;
        caption.zero = 0;
        Virtual<void>(check, 0x8C, &caption);
        DestroyString(caption);
        DestroyString(localized);

        Virtual<void>(checkWindow, 0x158, At<void*>(self, view::kLayout));
        Virtual<void>(checkWindow, 0x100, uint32_t{0x43});
        const int32_t height = Virtual<int32_t>(self + view::kWindow, 0xA8);
        Virtual<void>(checkWindow, 0xE0, int32_t{0xC}, height - 0x1C);
        Virtual<void>(self + view::kWindow, 0x38, checkWindow);
        Virtual<void>(checkWindow, 0x118);
        Release(check);
        Cdecl<void>(addr::SC4UIWindows_Unpaint, checkWindow);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    }

    Tree before;
    InitTree(before, 0x14);
    {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{8});
        void* const made = memory ? This<void*>(addr::SC4UI_F774550, memory) : nullptr;
        At<void*>(self, view::kPanelProc) = made;
        AddRef(made);
    }
    {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x84});
        void* const made = memory ? This<void*>(addr::SC4UI_F773050, memory) : nullptr;
        At<void*>(self, view::kTicker) = made;
        AddRef(made);
    }

    static const uint32_t kLayouts[5] = {0x69E3D347, 0x2BC90671, 0xAA1F1F57, 0x49889894, 0xAA53E3EA};
    static const uint32_t kChildren[5] = {0xC991EDA8, 0x69E40A1F, 0xCA1F1D9C, 0xC98F49F1, 0x0A78827A};
    for (uint32_t i = 0; i < 5; ++i) {
        Cdecl<uint32_t>(addr::SC4UIWindows_Collect, static_cast<void*>(self + view::kWindow), &before);
        key[0] = 0;
        key[1] = kUiGroup;
        key[2] = kLayouts[i];
        void* const panel =
            Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], static_cast<void*>(self + view::kWindow), kChildren[i]);
        if (!panel)
            continue;
        AddRef(panel);
        PushBackHeld(At<PointerVector>(self, view::kPanels), panel);
        Release(panel);
        Release(panel);
        PushBack(At<PointerVector>(self, view::kGroups + 0xC * i), panel);

        void*& gen = At<void*>(self, view::kPanelGens + 4 * i);
        Virtual<uint8_t>(panel, 0x0, kWinGenIid, &gen);
        Virtual<void>(panel, 0x1D0, static_cast<void*>(self + view::kTarget));
        Virtual<void>(gen, 0x1C, At<void*>(self, view::kPanelProc));
        int32_t area[4] = {0, 0, 0, 0};
        Virtual<void>(gen, 0x2C, &area[0]);
        Cdecl<void>(addr::SC4UIWindows_Unpaint, panel);

        if (i == 2) {
            char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x7C});
            void* const made = memory ? This<void*>(addr::SC4UI_F76F160, memory, gen) : nullptr;
            At<void*>(self, view::kMySimPanel) = made;
            AddRef(made);
            This<void>(addr::SC4UI_F76E9F0, At<void*>(self, view::kMySimPanel));
            Virtual<void>(gen, 0x1C, At<void*>(self, view::kMySimPanel));
            AttachFrame(self, 0xABB26B0E, view::kGroups + 0xC * 2, true);
        } else if (i == 1) {
            AttachFrame(self, 0xE9889775, view::kGroups + 0xC * 1, false);
        } else if (i == 4) {
            Virtual<void>(gen, 0x1C, At<void*>(self, view::kPanelProc));
        }
        Cdecl<uint32_t>(addr::SC4UIWindows_NewSince, static_cast<void*>(self + view::kWindow), &before, &before);
        Cdecl<uint8_t>(addr::SC4UIWindows_Prepare, &before, static_cast<void*>(nullptr));
        Virtual<void>(panel, 0x118);
    }

    Virtual<uint8_t>(At<void*>(self, view::kLayout), 0x94, uint32_t{1}, kTextIid, &At<void*>(self, view::kTextA));
    void* const own = self + view::kWindow;
    Virtual<uint8_t>(own, 0x94, uint32_t{0x09E418FE}, kTextIid, &At<void*>(self, view::kTextB));
    Virtual<uint8_t>(own, 0x94, uint32_t{0xC9E41918}, kTextIid, &At<void*>(self, view::kTextC));

    // The news ticker's window.
    {
        Cdecl<uint32_t>(addr::SC4UIWindows_Collect, own, &before);
        void* gen = nullptr;
        key[0] = 0;
        key[1] = kUiGroup;
        key[2] = 0x2A2AED99;
        Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], own, uint32_t{0xCA2AEDC0}, kWinGenIid, &gen);
        This<void>(addr::SC4UI_F7724C0, At<void*>(self, view::kTicker), gen);
        void* const ticker = Virtual<void*>(gen, 0xC);
        Cdecl<uint32_t>(addr::SC4UIWindows_NewSince, own, &before, &before);
        Cdecl<uint8_t>(addr::SC4UIWindows_Prepare, &before, static_cast<void*>(nullptr));
        Virtual<void>(ticker, 0x118);
        AddRef(ticker);
        PushBackHeldInline(At<PointerVector>(self, view::kPanels), ticker);
        Release(ticker);
        PushBack(At<PointerVector>(self, view::kGroups + 0xC * 1), ticker);
        PushBack(At<PointerVector>(self, view::kGroups + 0xC * 2), ticker);

        void* const button = Virtual<void*>(Virtual<void*>(ticker, 0x2C), 0x8C, uint32_t{0xAA231508});
        Virtual<void>(button, 0x118);
        Cdecl<void>(addr::SC4UIWindows_Unpaint, button);
        AddRef(button);
        PushBackHeldInline(At<PointerVector>(self, view::kPanels), button);
        Release(button);
        if (gen)
            Release(gen);
    }

    // The first window of each group.
    for (uint32_t i = 0; i < 5; ++i) {
        void* const first = *At<PointerVector>(self, view::kGroups + 0xC * i).begin;
        Virtual<void>(first, 0x110, uint32_t{0x800}, uint32_t{1});
        Virtual<void>(first, 0x5C);
        Virtual<void>(first, 0x110, uint32_t{0x800}, uint32_t{0});
    }

    LoadPanel(self, before, view::kZoomPanel, 0x4BC906B5, 0x6A64E3C0);
    if (void* const zoom = At<void*>(self, view::kZoomPanel)) {
        Cdecl<void>(addr::SC4UIWindows_Unpaint, zoom);
        Virtual<void>(At<void*>(self, view::kZoomPanel), 0x5C);
        Virtual<void>(At<void*>(self, view::kZoomPanel), 0x118);
    }

    LoadPanel(self, before, view::kMapWindow, 0x2BC9060F, 0x4200);
    if (void* const map = At<void*>(self, view::kMapWindow)) {
        Virtual<void>(map, 0x5C);
        char* const part = At<char*>(self, view::kMapWindow);
        This<void>(addr::SC4UI_F79EA40, part ? part - 8 : nullptr);
        Virtual<void>(At<void*>(self, view::kMapWindow), 0x118);
        Cdecl<void>(addr::SC4UIWindows_Unpaint, At<void*>(self, view::kMapWindow));
    }

    LoadPanel(self, before, view::kGraphsWindow, 0x6BC9065A, 0x0A4A8176);
    if (void* const graphs = At<void*>(self, view::kGraphsWindow)) {
        Virtual<void>(graphs, 0x5C);
        Virtual<void>(At<void*>(self, view::kGraphsWindow), 0x118);
        Cdecl<void>(addr::SC4UIWindows_Unpaint, At<void*>(self, view::kGraphsWindow));
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xDC});
        void* const made =
            memory ? This<void*>(addr::SC4UI_F76CFE0, memory, At<void*>(self, view::kGraphsWindow)) : nullptr;
        Hold(At<void*>(self, view::kGraphs), made);
        This<void>(addr::SC4UI_F76CA60, At<void*>(self, view::kGraphs));
    }

    LoadPanel(self, before, view::kStyleWindow, 0x6BC61F19, 0xABC619D2);
    if (void* const style = At<void*>(self, view::kStyleWindow)) {
        Virtual<void>(style, 0x5C);
        Virtual<void>(At<void*>(self, view::kStyleWindow), 0x118);
        Cdecl<void>(addr::SC4UIWindows_Unpaint, At<void*>(self, view::kStyleWindow));
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1C});
        void* const made =
            memory ? This<void*>(addr::SC4UI_F769750, memory, At<void*>(self, view::kStyleWindow)) : nullptr;
        Hold(At<void*>(self, view::kStylePanel), made);
        This<void>(addr::SC4UI_F769420, At<void*>(self, view::kStylePanel));
    }

    {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1A0});
        void* const made = memory ? This<void*>(addr::SC4UI_F7658A0, memory) : nullptr;
        At<void*>(self, view::kBudgetPanel) = made;
        AddRef(made);
        This<void>(addr::SC4UI_F766E10, At<void*>(self, view::kBudgetPanel));
    }

    // The info tip.
    {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1B4});
        void* const made = memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr;
        Hold(At<void*>(self, view::kInfoTip), made);
    }
    Virtual<void>(At<void*>(self, view::kInfoTip), 0x2C, uint32_t{0xA85F1A83});
    Virtual<void>(At<void*>(self, view::kInfoTip), 0x28, uint32_t{0xE9C86C9D});
    Virtual<void>(At<char*>(self, view::kInfoTip) + 4, 0x118);
    key[0] = kPngType;
    key[1] = kImageGroup;
    key[2] = 0x14416190;
    ImageLoader tipFrame;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &tipFrame, &key[0], uint32_t{1}, uint32_t{0});
    if (tipFrame.image)
        Virtual<void>(At<void*>(self, view::kInfoTip), 0x10, tipFrame.image, int32_t{-1}, int32_t{-1});
    key[0] = kPngType;
    key[1] = kImageGroup;
    key[2] = 0x14416160;
    ImageLoader tipPicture;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &tipPicture, &key[0], uint32_t{1}, uint32_t{0});
    Hold(At<void*>(self, view::kTipPicture), tipPicture.image);
    Virtual<void>(own, 0x38, Virtual<void*>(At<void*>(self, view::kInfoTip), 0xC));

    // The overlay bitmap window and its buffer.
    {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xFC});
        char* const made = memory ? This<char*>(addr::GZWinBMP_Construct, memory) : nullptr;
        Hold(At<void*>(self, view::kBitmap), made ? static_cast<void*>(made + 0xD8) : nullptr);
    }
    Virtual<void>(At<void*>(self, view::kBitmap), 0x2C, uint32_t{6}, uint32_t{1});
    Virtual<void>(Virtual<void*>(At<void*>(self, view::kBitmap), 0xC), 0x110, uint32_t{0x200000}, uint32_t{1});
    Virtual<void>(Virtual<void*>(At<void*>(self, view::kBitmap), 0xC), 0x110, uint32_t{4}, uint32_t{1});
    Virtual<void>(Virtual<void*>(At<void*>(self, view::kBitmap), 0xC), 0x118);
    void* const buffers = Global<void*>(addr::SC4_Service43C9C);
    void*& buffer = At<void*>(self, view::kBuffer);
    Clear(buffer);
    if (Virtual<uint8_t>(buffers, 0xC, &buffer)) {
        Virtual<void>(buffer, 0xC, uint32_t{0x80}, uint32_t{0x20}, uint32_t{1}, uint32_t{0});
        const int32_t* const whole = Virtual<const int32_t*>(buffer, 0x30);
        const int32_t left = whole[0];
        const int32_t top = whole[1];
        const int32_t right = whole[2];
        const int32_t bottom = whole[3];
        int32_t area[4] = {left - left, top - top, right - left, bottom - top};
        Virtual<void>(At<void*>(self, view::kBitmap), 0x10, buffer);
        Virtual<void>(At<void*>(self, view::kBitmap), 0x20, &area[0]);
    }
    Virtual<void>(own, 0x38, Virtual<void*>(At<void*>(self, view::kBitmap), 0xC));

    {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x18});
        void* const made = memory ? This<void*>(addr::SC4UI_F762E70, memory) : nullptr;
        At<void*>(self, view::kAdvicePopup) = made;
        AddRef(made);
        Virtual<void>(At<void*>(self, view::kAdvicePopup), 0x14);
    }

    // A window over the whole view.
    {
        void* cover = nullptr;
        void* const com2 = Cdecl<void*>(addr::RZGetCOM);
        Clear(cover);
        if (Virtual<uint8_t>(com2, 0xC, uint32_t{0xCA5D3294}, uint32_t{0xCA5D3290}, &cover)) {
            void* const coverWindow = Virtual<void*>(cover, 0xC);
            const int32_t height = At<int32_t>(self, view::kAreaBottom) - At<int32_t>(self, view::kAreaTop);
            const int32_t width = At<int32_t>(self, view::kAreaRight) - At<int32_t>(self, view::kAreaLeft);
            Virtual<void>(coverWindow, 0xDC, int32_t{0}, int32_t{0}, width, height);
            Virtual<void>(coverWindow, 0x100, uint32_t{0x6A5E44B6});
            Virtual<void>(coverWindow, 0x110, uint32_t{0x10000}, uint32_t{0});
            Virtual<void>(coverWindow, 0x110, uint32_t{0x200000}, uint32_t{1});
            Virtual<void>(coverWindow, 0x110, uint32_t{4}, uint32_t{1});
            Virtual<void>(own, 0x38, coverWindow);
        }
        if (cover)
            Release(cover);
    }

    {
        void* gen = nullptr;
        if (Virtual<uint8_t>(own, 0x94, uint32_t{0xEA8CAD14}, kWinGenIid, &gen)) {
            char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x14});
            void* const made = memory ? This<void*>(addr::SC4UI_F769E90, memory) : nullptr;
            Virtual<void>(gen, 0x1C, made);
        }
        if (gen)
            Release(gen);
    }

    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58)) {
        At<uint32_t>(self, view::kField1F4) = 0;
        This<void>(addr::SC4UI_F7E8B90, self, uint32_t{0});
        Cdecl<void>(addr::SC4Win_EnableChild, At<void*>(self, view::kLayout), uint32_t{0x99887766}, uint32_t{0});
        Cdecl<void>(addr::SC4Win_EnableChild, At<void*>(self, view::kLayout), uint32_t{0x8B96B73E}, uint32_t{0});
    }

    This<void>(addr::SC4ImageLoader_Destruct, &tipPicture);
    This<void>(addr::SC4ImageLoader_Destruct, &tipFrame);
    if (before.count) {
        for (TreeNode* node = before.head->parent; node;) {
            This<void>(addr::SC4UI_F41D7F0, &before, node->right);
            TreeNode* const next = node->left;
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
            node = next;
        }
        before.head->left = before.head;
        before.head->parent = nullptr;
        before.head->right = before.head;
        before.count = 0;
    }
    if (before.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(before.head));
}

bool InstallCityView4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7EC980, reinterpret_cast<void*>(&CollectMissionItems)},
        {addr::SC4UI_F7ECCE0, reinterpret_cast<void*>(&AssertGlobalEffectTrigger)},
        {addr::SC4UI_F7ECF60, reinterpret_cast<void*>(&ShowMissionStatusPanel)},
        {addr::SC4UI_F7ED0F0, reinterpret_cast<void*>(&CityLoaded)},
        {addr::SC4UI_F7ED720, reinterpret_cast<void*>(&LayoutButton)},
        {addr::SC4UI_F7EDCD0, reinterpret_cast<void*>(&ShowMissionMenu)},
        {addr::SC4UI_F7EDE30, reinterpret_cast<void*>(&SetSettingForward)},
        {addr::SC4UI_F7EDEB0, reinterpret_cast<void*>(&Start)},
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

const bool g_cityView4Registered = registry::Register({
    "ui.cityview.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7EDEB0,
    "Our city view start, loaded-city setup, layout buttons, missions menu, effect trigger count and mission "
    "status panel (docs/ui.md)",
    false,
    InstallCityView4,
});

} // namespace
} // namespace opensc4::ui
