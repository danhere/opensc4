// What lies between the link handler and the graphics options dialog
// (0x0078D140 to 0x0078FF40): the interface's shared helpers. None of it is
// one window class; the pieces are
//
//   - the rest of the HTML control's sc4 link handler (ui_department_5.cpp
//     has its object): FollowLink 0x0078D140, which windows call for a
//     clicked link ("sc4://advice/<...>" goes to the advisor system,
//     "sc4://action/<...>" is taken apart and dropped), and the handler's
//     slot 6 0x0078D330, which gives the HTML control the bytes of a
//     resource named by a link ("sc4://html/<instance>",
//     "sc4://image/<instance>", "sc4://<x>/<group>/<instance>"), read by
//     0x0078D2B0;
//   - the colour ramp 0x0078D520 that the map window's legends are made
//     with: numbers only, no drawing;
//   - window helpers: the child of a window under a point (0x0078D6E0, and
//     0x0078D720 which goes on down to the deepest one), Unpaint
//     0x0078DAC0, and Place 0x0078DB20, which moves a panel so that its
//     child 0xAAAA lies on an anchor window or at an edge of the screen;
//   - three readers of a property's number with register arguments
//     (0x0078D810, 0x0078D980, 0x0078D9E0);
//   - the buttons of a modal message window (8 bytes, vtable 0x00AB55C0, no
//     class ID): a window procedure that ends the modal run with the ID of
//     the button pressed; and the three functions that run such a window:
//     MessageDialog 0x0078DD80 (two texts, one button), QuestionDialog
//     0x0078DFF0 (two texts, yes and no) and 0x0078E2F0 (any layout; hands
//     back the number the run ended with);
//   - sets of windows (the game's std::set of window pointers): Collect
//     0x0078E990, NewSince 0x0078F3C0 and Prepare 0x0078E610, which the
//     advice window, the budget panel, the region view and the city view
//     use to place the panels a layout has just added;
//   - the makers of the city view's build menus: 0x0078F100 makes a menu
//     window (0x140 bytes, constructor 0x0079B500) and fills it with one
//     item (0x44 bytes, constructor 0x0079B6B0) for every exemplar of
//     resource type 0x6534284A in a group whose "kind" property matches,
//     in the order of their "order" property; 0x0078EF30 collects the items,
//     0x0078EC90 makes one, and 0x0078EAC0 fills "d:<property>" and
//     "m:<property>" tokens of an item's texts with the exemplar's numbers;
//   - slot 7 of the graphics options dialog (0x0078F550, vtable 0x00AB55F8;
//     the rest of the dialog is in ui_panels_2.cpp), which reads the
//     window's buttons back into the settings.
//
// Replacement, off by default:
//
//   ui.panels.part1: everything named above.
//
// Not functions: 0x0078D8E0 (a case of the jump table in 0x0078D810),
// 0x0078DA40 (a label in 0x0078D9E0), 0x0078DD10 (the "no" end of
// 0x0078DCE0), 0x0078E2E0 (the "no" end of QuestionDialog) and 0x0078E800 (a
// label in 0x0078E750).
//
// Still the game's: 0x0078DAF0 (code shared with the occupants' classes);
// the set's and the map's own template code (0x0078DD40 erase a subtree,
// 0x0078E560 copy a subtree, 0x0078E690 assign, 0x0078E750 insert with a
// hint, 0x0078E9E0 and 0x0078EA50 the inserter, 0x0078F250 the difference of
// two sets), called by address where our code needs it. The three property
// readers are written out here for our own callers but not hooked: their
// arguments come in registers, and nothing else calls them.
//
// The buttons object: +0 vtable (a window procedure), +4 the reference
// count.
//
// A menu item, as far as this file touches it: +4 and +0x18 two strings
// (name, description), +0x2C a dword cleared here, +0x30 the icon, +0x34
// the command the menu was made for, +0x38 the item's ID.
//
// The graphics options dialog's fields are named as in ui_panels_2.cpp.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::ui {
namespace {

namespace links { // the link handler (ui_department_5.cpp)
constexpr std::size_t kHandler = 0x8;
constexpr std::size_t kResources = 0xC;
} // namespace links

namespace item { // a menu item
constexpr std::size_t kName = 0x04;
constexpr std::size_t kDescription = 0x18;
constexpr std::size_t kState = 0x2C;
constexpr std::size_t kIcon = 0x30;
constexpr std::size_t kCommand = 0x34;
constexpr std::size_t kId = 0x38;
} // namespace item

namespace graphics { // the graphics options dialog (ui_panels_2.cpp)
constexpr std::size_t kWindow = 0x04;     // the window's cIGZWinGen
constexpr std::size_t kSettings = 0x08;   // the game's settings (0x00B43CD8)
constexpr std::size_t kWasCursor = 0x10;  // word
constexpr std::size_t kWasDriver = 0x12;  // word
constexpr std::size_t kWasDepth = 0x14;   // word
constexpr std::size_t kWasDetail = 0x16;  // word
constexpr std::size_t kWasSize = 0x18;    // word
constexpr std::size_t kSecondPage = 0x1A; // byte
constexpr std::size_t kWasOpaque = 0x1C;  // word
} // namespace graphics

namespace video { // the settings' video part
constexpr std::size_t kStart = 0xF0C;
constexpr std::size_t kEffects = 0xF0D;
constexpr std::size_t kCarsAndSims = 0xF0E;
constexpr std::size_t kShadows = 0xF0F;
constexpr std::size_t kTextures = 0xF10;
constexpr std::size_t kAtmosphere = 0xF11;
constexpr std::size_t kWater = 0xF12;
constexpr std::size_t kDriver = 0xF13;
constexpr std::size_t kCursor = 0xF14;
constexpr std::size_t kTranslucency = 0xF15;
constexpr std::size_t kDetail = 0xF16;
constexpr std::size_t kAutomata = 0xF18;
constexpr std::size_t kWidth = 0xF1C;
constexpr std::size_t kHeight = 0xF20;
constexpr std::size_t kDepth = 0xF24;
constexpr std::size_t kLoader = 0xF28;
} // namespace video

constexpr uint32_t kWinIid = 0x22BA0121;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kCheckIid = 0x8810;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kPngType = 0x856DDBAC;
constexpr uint32_t kUiGroup = 0x96A006B0;

constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kEndModal = 0xF;

constexpr uint32_t kAnchorChild = 0xAAAA;
constexpr uint32_t kYesButton = 0x8A8DFD4E;
constexpr uint32_t kNoButton = 0x8A8DFD4A;

// The properties of a menu item's exemplar.
constexpr uint32_t kItemNameKey = 0x8A416A99;
constexpr uint32_t kItemName = 0x899AFBAD;
constexpr uint32_t kExemplarName = 0x20;
constexpr uint32_t kItemDescriptionKey = 0xCA416AB5;
constexpr uint32_t kItemDescription = 0x8A2602A9;
constexpr uint32_t kItemIcon = 0x8A2602B8;
constexpr uint32_t kItemOrder = 0x8A2602B9;
constexpr uint32_t kItemKind = 0x8A2602BA;
constexpr uint32_t kItemId = 0x8A2602BB;

// A window message as window procedures get it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

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

// A node of the game's red-black trees. A set's node ends after `value`
// (0x14 bytes); a map's has `second` too (0x18 bytes). The head node's
// parent is the root, its left the first node and its right the last.
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

// What the search for a child under a point carries.
struct PointSearch {
    int32_t x;
    int32_t y;
    void* found;
};

// The resource manager's filter by type and group, made on the stack.
struct TypeGroupFilter {
    std::uintptr_t vtable;
    uint32_t references;
    uint32_t type;
    uint32_t group;
};

// A menu's entry while the items are sorted: the order first.
struct OrderedItem {
    uint32_t order;
    char* item;
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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The inlined assignment to a reference holder.
void Hold(void*& holder, void* object)
{
    void* const old = holder;
    if (object == old)
        return;
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

// The inlined clear() of a set of windows (0x0041D7F0 frees a subtree).
void ClearSet(Tree& set)
{
    if (!set.count)
        return;
    This<void>(addr::SC4UI_F41D7F0, &set, set.head->parent);
    set.head->left = set.head;
    set.head->parent = nullptr;
    set.head->right = set.head;
    set.count = 0;
}

// The inlined fetch of the main window: the window manager asked from the
// framework again, its slot 3, and the reference let go. The manager is not
// checked for null, as in the original.
void* MainWindow()
{
    void* manager = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xA417445E}, uint32_t{0x5A4}, &manager);
    void* const main = Virtual<void*>(manager, 0xC);
    This<void>(addr::SC4RefPtr_Release, &manager);
    return main;
}

// The inlined making of a modal window's buttons object for the window's
// cIGZWinGen: the holder gets it, the window reports to it.
void GiveButtons(void* gen, void*& buttons)
{
    char* const made = Cdecl<char*>(addr::SC4_operator_new, uint32_t{8});
    if (made) {
        At<std::uintptr_t>(made, 0x0) = addr::SC4ModalButtons_Vtable;
        At<uint32_t>(made, 0x4) = 0;
    }
    This<void*>(addr::SC4RefPtr_Assign, &buttons, static_cast<void*>(made));
    Virtual<void>(gen, 0x1C, buttons);
}

// --- The link handler's rest ----------------------------------------------------------

// Whether the text has `word` after "sc4://" and a slash after it (the
// search's predicate object is empty; the original pushes the link there).
bool HasVerb(const StringBody& body, const char* word, void* url)
{
    char* const begin = body.begin;
    char* const end = body.end;
    const uint32_t size = static_cast<uint32_t>(end - begin);
    if (size < 6)
        return false;
    char* const found = Cdecl<char*>(addr::SC4_SearchChars, begin, end, word, word + 6, url);
    return found != end && found - begin == 6 && size >= 0xC && begin[0xC] == '/';
}

// The game's 0x0078D140 (thiscall on an empty object, ret 8). Follows a
// link clicked in an HTML text. "sc4://action/<what>?<rest>" is taken apart
// and nothing is done with the parts; "sc4://advice/<what>" hands <what> to
// the advisor system (the city's slot 81; its slot 11) and returns what that
// says. Anything else: false. The city is not checked for null, as in the
// original.
uint8_t __fastcall FollowLink(char*, void*, void* url, uint32_t)
{
    static const char kAction[] = "action";
    static const char kAdvice[] = "advice";

    const uint32_t length = Virtual<uint32_t>(url, 0x1C);
    const char* const chars = Virtual<const char*>(url, 0x18);
    GameString text;
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &text.body, chars, chars + length, static_cast<void*>(&url));
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;

    if (static_cast<uint32_t>(text.body.end - text.body.begin) >= 6) {
        if (HasVerb(text.body, &kAction[0], url)) {
            This<void*>(addr::SC4String_EraseBody, &text.body, uint32_t{0}, uint32_t{0xD});
            GameString rest;
            This<void*>(addr::SC4String_SC4String, &rest);
            This<uint8_t>(addr::SC4String_SplitToken, &text, uint32_t{'?'}, &rest);
            DestroyString(rest);
        } else if (HasVerb(text.body, &kAdvice[0], url)) {
            This<void*>(addr::SC4String_EraseBody, &text.body, uint32_t{0}, uint32_t{0xD});
            void* const advisors = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
            const uint8_t followed = Virtual<uint8_t>(advisors, 0x2C, &text);
            DestroyString(text);
            return followed;
        }
    }
    DestroyString(text);
    return 0;
}

// The game's 0x0078D2B0 (ret 8). `this` is the link handler. Reads the
// record `key` out of the package that has it into the string body `out`:
// the resource manager's slot 32 finds the package, whose slot 28 gives the
// record's size when asked with no buffer and then fills the buffer.
uint8_t __fastcall LinkRead(char* self, void*, const uint32_t* key, StringBody* out)
{
    void* segment = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, links::kResources), 0x80, key, &segment))
        return 0;
    uint8_t read = 0;
    uint32_t size = 0;
    const uint32_t needed = Virtual<uint32_t>(segment, 0x70, key, static_cast<void*>(nullptr), &size);
    size = needed;
    if (size != 0xFFFFFFFF) {
        This<void>(addr::SC4String_ResizeBody, out, size, uint32_t{0});
        Virtual<uint32_t>(segment, 0x70, key, out->begin, &size);
        read = 1;
    }
    Release(segment);
    return read;
}

// The game's 0x0078D330 (slot 6 of the part at +8, ret 8). Gives the bytes
// a link stands for. The address after "sc4://" is <kind>/<instance> with
// the kind "html" (group 0x0A133FC0) or "image" (group 0xCA133ECB), or
// <kind>/<group>/<instance> with the group as a number; the instance is
// read only when at least eight characters are left. The key's type is
// always 0x856DDBAC.
uint8_t __fastcall LinkGetData(char* part, void*, void* url, StringBody* out)
{
    char* const whole = This<char*>(addr::SC4Url_Text, url);
    StringBody* const source = whole ? reinterpret_cast<StringBody*>(whole + 4) : nullptr;
    GameString rest;
    This<void>(addr::SC4String_CopyBuffer, &rest.body, source, uint32_t{0}, uint32_t{0xFFFFFFFF},
               static_cast<void*>(&url));
    rest.vtable = addr::SC4String_Vtable;
    rest.zero = 0;
    if (!This<uint8_t>(addr::cRZBaseString_BeginsWith, &rest, "sc4://", uint32_t{6})) {
        DestroyString(rest);
        return 0;
    }
    This<void*>(addr::SC4String_EraseBody, &rest.body, uint32_t{0}, uint32_t{6});

    GameString kind;
    InitString(kind);
    if (!This<uint8_t>(addr::SC4String_SplitToken, &rest, uint32_t{'/'}, &kind)) {
        DestroyString(kind);
        DestroyString(rest);
        return 0;
    }

    const bool grouped =
        This<uint32_t>(addr::SC4String_FindCharBody, &rest.body, uint32_t{'/'}, uint32_t{0}) != 0xFFFFFFFF;
    uint32_t key[3] = {kPngType, 0, 0};
    if (grouped) {
        GameString group;
        This<void*>(addr::SC4String_SC4String, &group);
        This<uint8_t>(addr::SC4String_SplitToken, &rest, uint32_t{'/'}, &group);
        key[1] = Cdecl<uint32_t>(addr::SC4_TextToInt, group.body.begin);
        DestroyString(group);
    } else {
        This<void>(addr::cRZBaseString_MakeLower, &kind);
        if (Cdecl<uint8_t>(addr::SC4String_Equals, &kind.body, "html"))
            key[1] = 0x0A133FC0;
        else if (Cdecl<uint8_t>(addr::SC4String_Equals, &kind.body, "image"))
            key[1] = 0xCA133ECB;
    }
    if (static_cast<uint32_t>(rest.body.end - rest.body.begin) >= 8)
        key[2] = Cdecl<uint32_t>(addr::SC4_TextToInt, rest.body.begin);

    const uint8_t read = LinkRead(part - links::kHandler, nullptr, &key[0], out);
    DestroyString(kind);
    DestroyString(rest);
    return read;
}

// --- The colour ramp ------------------------------------------------------------------

// The game's 0x0078D520 (cdecl). Fills `entries` colours from `count`
// numbers that come in pairs (position, colour) with rising positions:
// before the first position the first colour, after the last pair the last
// colour, and between two pairs a blend of their colours, each of the four
// bytes on its own, by 256ths of the way. A pair at the same position as the
// next one divides by zero, as in the original.
void __cdecl ColourRamp(uint32_t* ramp, int32_t entries, const uint32_t* values, int32_t count)
{
    int32_t pair = 0;
    int32_t i = 0;
    if (entries <= 0)
        return;
    while (static_cast<uint32_t>(i) < values[0]) {
        ramp[i] = values[1];
        if (++i >= entries)
            return;
    }
    if (i >= entries)
        return;

    for (;;) {
        const int32_t last = count - 2;
        while (pair < last && static_cast<uint32_t>(i) >= values[pair + 2])
            pair += 2;
        if (pair >= last)
            break;

        const uint32_t from = values[pair];
        const uint32_t t = ((static_cast<uint32_t>(i) - from) << 8) / (values[pair + 2] - from);
        const uint32_t c0 = values[pair + 1];
        const uint32_t c1 = values[pair + 3];
        const uint32_t low0 = c0 & 0x00FF00FF;
        const uint32_t high0 = c0 & 0xFF00FF00;
        const uint32_t low = ((((c1 & 0x00FF00FF) - low0) * t + 0x00800080) >> 8) + low0;
        const uint32_t x = (((c1 >> 8) & 0xFFFF00FF) - (high0 >> 8)) * t + high0 + 0xFF800080;
        const uint32_t y = (((c1 >> 8) & 0x00FF00FF) - (high0 >> 8)) * t + high0 + 0x00800080;
        ramp[i] = ((low ^ x) & 0x00FF00FF) ^ y;
        if (++i >= entries)
            return;
    }
    for (; i < entries; ++i)
        ramp[i] = values[count - 1];
}

// --- Window helpers -------------------------------------------------------------------

// The game's 0x0078D670 (cdecl): what a window's child enumeration (slot
// 32) calls for the point search. The first child that is shown (slot 71),
// has not the flag 0x200000 and holds the point (slot 62) is kept; its own
// children are then enumerated too, to no effect, since one is found
// already.
uint8_t __cdecl ChildAtPoint(void*, uint32_t, void* child, PointSearch* search)
{
    if (search->found)
        return 1;
    if (!Virtual<uint8_t>(child, 0x11C))
        return 1;
    if (Virtual<uint8_t>(child, 0x10C, uint32_t{0x200000}))
        return 1;
    if (!Virtual<uint8_t>(child, 0xF8, search->x, search->y))
        return 1;
    search->found = child;
    Virtual<uint8_t>(child, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78D670}, search);
    return 1;
}

// The game's 0x0078D6E0 (cdecl): the child of `window` under a point, or
// null.
void* __cdecl ChildAt(void* window, int32_t x, int32_t y)
{
    PointSearch search;
    search.x = x;
    search.y = y;
    search.found = nullptr;
    Virtual<uint8_t>(window, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78D670}, &search);
    return search.found;
}

// The game's 0x0078D720 (cdecl): the deepest window under a point, going
// down from child to child; null when `window` has no child there.
void* __cdecl DeepestChildAt(void* window, int32_t x, int32_t y)
{
    void* deepest = nullptr;
    PointSearch search;
    search.x = x;
    search.y = y;
    search.found = nullptr;
    Virtual<uint8_t>(window, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78D670}, &search);
    while (void* const found = search.found) {
        deepest = found;
        search.x = x;
        search.y = y;
        search.found = nullptr;
        Virtual<uint8_t>(found, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78D670}, &search);
    }
    return deepest;
}

// The game's 0x0078D7A0 (cdecl). Sets the position of `moved` (slot 57) to
// the anchor's place on the screen less that of `part` (slot 60 for both).
uint8_t __cdecl MoveToAnchor(void* moved, void* part, void* anchor)
{
    int32_t anchorX = 0;
    int32_t anchorY = 0;
    int32_t partX = 0;
    int32_t partY = 0;
    Virtual<void>(anchor, 0xF0, &anchorX, &anchorY);
    Virtual<void>(part, 0xF0, &partX, &partY);
    Virtual<void>(moved, 0xE4, anchorX - partX, anchorY - partY);
    return 1;
}

// The game's 0x0078D810 (holder in ECX, property in EDX, index in EDI, the
// result's place in EBX; not hooked). One number of a property as a whole
// number, by the value's type: 1 a truth value, 2 to 9 the whole numbers (8
// and 9 their low half), 10 and 11 the real numbers cut off. For a list and
// a negative index the index is checked against the count, which fails
// then; other indexes are not checked.
bool PropertyNumberAt(void* holder, uint32_t id, int32_t index, uint32_t* out)
{
    void* const property = Virtual<void*>(holder, 0x24, id);
    if (!property)
        return false;
    void* const value = Virtual<void*>(property, 0x14);
    if ((Virtual<uint32_t>(value, 0xC) & 0x8000) && index < 0 &&
        static_cast<uint32_t>(index) >= Virtual<uint32_t>(value, 0x10))
        return false;
    switch (Virtual<uint32_t>(value, 0xC) & 0x7FFF) {
    case 1:
        *out = Virtual<const uint8_t*>(value, 0x174)[index] != 0;
        return true;
    case 2:
        *out = Virtual<const uint8_t*>(value, 0x17C)[index];
        return true;
    case 3:
        *out = static_cast<uint32_t>(static_cast<int32_t>(Virtual<const int8_t*>(value, 0x184)[index]));
        return true;
    case 4:
        *out = Virtual<const uint16_t*>(value, 0x18C)[index];
        return true;
    case 5:
        *out = static_cast<uint32_t>(static_cast<int32_t>(Virtual<const int16_t*>(value, 0x194)[index]));
        return true;
    case 6:
        *out = Virtual<const uint32_t*>(value, 0x19C)[index];
        return true;
    case 7:
        *out = Virtual<const uint32_t*>(value, 0x1A4)[index];
        return true;
    case 8:
        *out = Virtual<const uint32_t*>(value, 0x1AC)[index * 2];
        return true;
    case 9:
        *out = Virtual<const uint32_t*>(value, 0x1B4)[index * 2];
        return true;
    case 10: {
        const double real = Virtual<const float*>(value, 0x1BC)[index];
        *out = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(real)));
        return true;
    }
    case 11: {
        const double real = Virtual<const double*>(value, 0x1C4)[index];
        *out = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(real)));
        return true;
    }
    default:
        return false;
    }
}

// The game's 0x0078D980 (the result's place in EAX, holder and property on
// the stack; not hooked). A property's number: the first of a list, or what
// the value's slot 28 gives for a single one.
bool PropertyNumber(void* holder, uint32_t id, uint32_t* out)
{
    void* const property = Virtual<void*>(holder, 0x24, id);
    if (!property)
        return false;
    void* const value = Virtual<void*>(property, 0x14);
    if (Virtual<uint32_t>(value, 0xC) & 0x8000)
        return PropertyNumberAt(holder, id, 0, out);
    return Virtual<uint8_t>(value, 0x70, out) != 0;
}

// The game's 0x0078D9E0 (holder in ECX, property in EDX, the result's place
// in EDI; not hooked). A property's real number, when its type is 10
// (float) or 11 (double): the first of a list (none when the list is
// empty), or the single value (slots 80 and 82).
bool PropertyReal(void* holder, uint32_t id, float* out)
{
    void* const property = Virtual<void*>(holder, 0x24, id);
    if (!property)
        return false;
    void* const value = Virtual<void*>(property, 0x14);
    const uint32_t type = Virtual<uint32_t>(value, 0xC) & 0x7FFF;
    if (type == 10) {
        if (Virtual<uint32_t>(value, 0xC) & 0x8000) {
            if (Virtual<uint32_t>(value, 0x10) == 0)
                return false;
            std::memcpy(out, Virtual<const void*>(value, 0x1BC), sizeof(float));
            return true;
        }
        *out = static_cast<float>(Virtual<double>(value, 0x140));
        return true;
    }
    if (type == 11) {
        if (Virtual<uint32_t>(value, 0xC) & 0x8000) {
            if (Virtual<uint32_t>(value, 0x10) == 0)
                return false;
            *out = static_cast<float>(*Virtual<const double*>(value, 0x1C4));
            return true;
        }
        *out = static_cast<float>(Virtual<double>(value, 0x148));
        return true;
    }
    return false;
}

// The game's 0x0078DA80 (cdecl): the child enumeration's call for Unpaint.
// Clears the child's flag 0x8000 and goes on into its children.
uint8_t __cdecl UnpaintChild(void*, uint32_t, void* child, void* context)
{
    Virtual<void>(child, 0x110, uint32_t{0x8000}, uint32_t{0});
    Virtual<uint8_t>(child, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78DA80}, context);
    return 0;
}

// The game's 0x0078DAC0 (cdecl): clears the flag 0x8000 of a window and of
// every window below it.
void __cdecl Unpaint(void* window)
{
    Virtual<void>(window, 0x110, uint32_t{0x8000}, uint32_t{0});
    Virtual<uint8_t>(window, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78DA80}, static_cast<void*>(nullptr));
}

// The game's 0x0078DB20 (cdecl). Places a panel: `panelId` is a child of
// `root` (the main window when null) and `childId` a child of the panel.
// With an anchor's ID, or when the child's caption is a number (the ID of
// the anchor), the panel is moved so that the child lies on that window of
// `root`. A caption "-<v><h>" instead, v one of "tmb" and h one of "lcr",
// puts the child at the top, middle or bottom and the left, centre or right
// of the main window. False when the child or the anchor is missing. The
// panel is not checked for null, as in the original.
uint8_t __cdecl Place(uint32_t panelId, uint32_t childId, uint32_t anchorId, void* root)
{
    static const char kVertical[] = "tmb";
    static const char kHorizontal[] = "lcr";

    if (!root)
        root = Virtual<void*>(Cdecl<void*>(addr::SC4_GetWinManager), 0xC);
    void* const panel = Virtual<void*>(root, 0x8C, panelId);
    void* const child = Virtual<void*>(panel, 0x8C, childId);
    if (!child)
        return 0;

    if (!anchorId) {
        void* const caption = Virtual<void*>(child, 0x124);
        if (Virtual<uint32_t>(caption, 0x1C) >= 3 && *Virtual<const char*>(caption, 0x18) == '-') {
            const char* const vertical = Cdecl<const char*>(
                addr::SC4_StrChr, &kVertical[0], static_cast<int32_t>(Virtual<const int8_t*>(caption, 0x18)[1]));
            const char* const horizontal = Cdecl<const char*>(
                addr::SC4_StrChr, &kHorizontal[0], static_cast<int32_t>(Virtual<const int8_t*>(caption, 0x18)[2]));
            if (!vertical || !horizontal)
                return 1;

            void* const main = Virtual<void*>(Cdecl<void*>(addr::SC4_GetWinManager), 0xC);
            const int32_t* const screen = Virtual<const int32_t*>(main, 0xC8);
            const int32_t screenLeft = screen[0];
            const int32_t screenTop = screen[1];
            const int32_t screenRight = screen[2];
            const int32_t screenBottom = screen[3];
            const int32_t* const area = Virtual<const int32_t*>(child, 0xC8);
            const int32_t left = area[0];
            const int32_t top = area[1];
            const int32_t right = area[2];
            const int32_t bottom = area[3];

            const int32_t down = static_cast<int32_t>(vertical - &kVertical[0]);
            const int32_t across = static_cast<int32_t>(horizontal - &kHorizontal[0]);
            const int32_t y = (screenBottom - screenTop - bottom + top) * down / 2 - top;
            const int32_t x = (screenRight - right - screenLeft + left) * across / 2 - left;
            Virtual<void>(panel, 0xE4, x, y);
            return 1;
        }
        anchorId = Cdecl<uint32_t>(addr::SC4_TextToInt, Virtual<const char*>(caption, 0x14));
        if (!anchorId)
            return 0;
    }

    void* const anchor = Virtual<void*>(root, 0x8C, anchorId);
    if (!anchor)
        return 0;
    return MoveToAnchor(panel, child, anchor);
}

// --- A modal window's buttons ---------------------------------------------------------

// The game's 0x0078DCE0 (slot 3, ret 8): a message of the modal window. A
// button's click ends the window manager's modal run with the button's ID.
uint8_t __fastcall ModalButtonsMessage(char*, void*, void* window, const WinMessage* message)
{
    if (message->type != kCommand || message->a != kButtonClicked)
        return 0;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x24, kEndModal, window, message->b);
    return 1;
}

// The game's 0x0078DD20 (slot 5): the deleting destructor.
char* __fastcall ModalButtonsDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4ModalButtons_Vtable;
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// What the three runners do alike once the window stands: tell the service
// at 0x00B43CE0, run the window modally, tell the service again, take the
// buttons object off and let it go, and give the window manager back what
// it had before (its slots 23, 24 and 37). Returns what the run ended with.
// The cIGZWinGen is not checked for null, as in the original.
uint32_t RunModal(void* manager, void* window, void* gen, void*& buttons, uint32_t before)
{
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    const uint32_t ended = Virtual<uint32_t>(manager, 0xA4, window);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
    Virtual<void>(gen, 0x1C, static_cast<void*>(nullptr));
    Clear(buttons);
    Virtual<void>(manager, 0x5C, window);
    if (Virtual<uint8_t>(manager, 0x60, before))
        Virtual<void>(manager, 0x94, before);
    return ended;
}

// The game's 0x0078DD80 (cdecl). A message box: the window 0xAA8DEF97 of
// the layout 0xCA8CBF0F under the main window, a third of the free height
// down and in the middle across, with the two texts in its controls
// 0xCA8CC4B6 and 0x4A8CC4A9, run modally until a button is pressed. Nothing
// happens when the application's slot 23 says no.
void __cdecl MessageDialog(void* title, void* text)
{
    if (!Cdecl<uint8_t>(addr::SC4UI_Service43C94Slot23))
        return;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    void* const main = MainWindow();
    const uint32_t before = Virtual<uint32_t>(manager, 0x90);
    uint32_t key[3] = {0, kUiGroup, 0xCA8CBF0F};
    void* const window = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], main, uint32_t{0xAA8DEF97});
    void* gen = nullptr;
    void* buttons = nullptr;
    if (!window)
        return;

    char guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    Virtual<void>(window, 0x5C);
    const int32_t height = Virtual<int32_t>(window, 0xA8);
    const int32_t y = (Virtual<int32_t>(main, 0xA8) - height) / 3;
    const int32_t width = Virtual<int32_t>(window, 0xA4);
    const int32_t x = (Virtual<int32_t>(main, 0xA4) - width) / 2;
    Virtual<void>(window, 0xE0, x, y);

    Clear(gen);
    if (Virtual<uint8_t>(window, 0x0, kWinGenIid, &gen))
        GiveButtons(gen, buttons);

    void* control = nullptr;
    if (Virtual<uint8_t>(window, 0x94, uint32_t{0xCA8CC4B6}, kTextIid, &control)) {
        Virtual<void>(control, 0x10, title);
        Release(control);
    }
    if (Virtual<uint8_t>(window, 0x94, uint32_t{0x4A8CC4A9}, kTextIid, &control)) {
        Virtual<void>(control, 0x10, text);
        Release(control);
    }

    RunModal(manager, window, gen, buttons, before);
    Release(window);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    if (buttons)
        Release(buttons);
    if (gen)
        Release(gen);
}

// The game's 0x0078DFF0 (cdecl). A question: the window 0x8A8DFCF5 of the
// layout 0xEA8CC3C6, placed like the message box, with the two texts in its
// controls 0x8A91E4FD and 0x6A8CC495 and, when given, captions for its
// buttons 0x8A8DFD4E (yes) and 0x8A8DFD4A (no). True when the run ended
// with the yes button.
uint8_t __cdecl QuestionDialog(void* title, void* text, void* yesCaption, void* noCaption)
{
    if (!Cdecl<uint8_t>(addr::SC4UI_Service43C94Slot23))
        return 0;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    void* const main = MainWindow();
    const uint32_t before = Virtual<uint32_t>(manager, 0x90);
    uint32_t key[3] = {0, kUiGroup, 0xEA8CC3C6};
    void* const window = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], main, uint32_t{0x8A8DFCF5});
    void* gen = nullptr;
    void* buttons = nullptr;
    if (!window)
        return 0;

    char guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    Virtual<void>(window, 0x5C);
    const int32_t mainHeight = Virtual<int32_t>(main, 0xA8);
    const int32_t y = (mainHeight - Virtual<int32_t>(window, 0xA8)) / 3;
    const int32_t mainWidth = Virtual<int32_t>(main, 0xA4);
    const int32_t x = (mainWidth - Virtual<int32_t>(window, 0xA4)) / 2;
    Virtual<void>(window, 0xE0, x, y);

    Clear(gen);
    if (Virtual<uint8_t>(window, 0x0, kWinGenIid, &gen))
        GiveButtons(gen, buttons);

    void* control = nullptr;
    if (Virtual<uint8_t>(window, 0x94, uint32_t{0x8A91E4FD}, kTextIid, &control)) {
        Virtual<void>(control, 0x10, title);
        Release(control);
    }
    if (Virtual<uint8_t>(window, 0x94, uint32_t{0x6A8CC495}, kTextIid, &control)) {
        Virtual<void>(control, 0x10, text);
        Release(control);
    }
    void* button = nullptr;
    if (yesCaption && Virtual<uint8_t>(window, 0x94, kYesButton, kCheckIid, &button)) {
        Virtual<void>(button, 0xC4, yesCaption);
        Release(button);
    }
    if (noCaption && Virtual<uint8_t>(window, 0x94, kNoButton, kCheckIid, &button)) {
        Virtual<void>(button, 0xC4, noCaption);
        Release(button);
    }

    const uint32_t ended = RunModal(manager, window, gen, buttons, before);
    Release(window);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    if (buttons)
        Release(buttons);
    if (gen)
        Release(gen);
    return ended == kYesButton;
}

// The game's 0x0078E2F0 (cdecl). Runs the window `id` of the interface
// group's layout `instance` as a modal window under the main window, placed
// like the message box, with a buttons object as its window procedure, and
// returns what the run ended with (the ID of the button pressed); 0 when
// the application's slot 23 says no or the window does not load.
uint32_t __cdecl RunWindow(uint32_t instance, uint32_t id)
{
    if (!Cdecl<uint8_t>(addr::SC4UI_Service43C94Slot23))
        return 0;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    void* const main = MainWindow();
    const uint32_t before = Virtual<uint32_t>(manager, 0x90);
    uint32_t key[3] = {0, kUiGroup, instance};
    void* window = nullptr;
    void* gen = nullptr;
    void* buttons = nullptr;
    Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], main, id, kWinIid, &window);
    if (!window)
        return 0;

    char guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    Virtual<void>(window, 0x5C);
    const int32_t mainHeight = Virtual<int32_t>(main, 0xA8);
    const int32_t y = (mainHeight - Virtual<int32_t>(window, 0xA8)) / 3;
    const int32_t width = Virtual<int32_t>(window, 0xA4);
    const int32_t x = (Virtual<int32_t>(main, 0xA4) - width) / 2;
    Virtual<void>(window, 0xE0, x, y);

    Clear(gen);
    if (Virtual<uint8_t>(window, 0x0, kWinGenIid, &gen))
        GiveButtons(gen, buttons);

    const uint32_t ended = RunModal(manager, window, gen, buttons, before);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    if (buttons)
        Release(buttons);
    if (gen)
        Release(gen);
    if (window)
        Release(window);
    return ended;
}

// --- Sets of windows ------------------------------------------------------------------

// The game's 0x0078E610 (cdecl): places every window of the set by its
// child 0xAAAA (see Place); `root` is passed on.
uint8_t __cdecl Prepare(Tree* set, void* root)
{
    for (TreeNode* node = set->head->left; node != set->head; node = Next(node))
        Place(Virtual<uint32_t>(node->value, 0xFC), kAnchorChild, 0, root);
    return 1;
}

// The game's 0x0078E960 (cdecl): the child enumeration's call for Collect
// and NewSince; adds the child to the set.
uint8_t __cdecl CollectChild(void*, uint32_t, void* child, Tree* set)
{
    uint32_t result[2];
    This<void*>(addr::SC4PtrSet_Insert, set, &result[0], &child);
    return 1;
}

// The game's 0x0078E990 (cdecl): empties the set, fills it with the
// window's children and returns their number.
uint32_t __cdecl Collect(void* window, Tree* set)
{
    ClearSet(*set);
    Virtual<uint8_t>(window, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78E960}, set);
    return set->count;
}

// The game's 0x0078F3C0 (cdecl): the window's children that are not in
// `before` go to `out`; returns their number. The difference (0x0078F250)
// and the set's assignment (0x0078E690) are the game's template code; the
// last argument of the first is a leftover the original pushes too.
uint32_t __cdecl NewSince(void* window, Tree* before, Tree* out)
{
    Tree now;
    InitTree(now, 0x14);
    Tree fresh;
    InitTree(fresh, 0x14);
    ClearSet(now);
    Virtual<uint8_t>(window, 0x80, kWinIid, std::uintptr_t{addr::SC4UI_F78E960}, &now);

    TreeNode* const first = now.head->left;
    uint32_t inserter[2];
    Cdecl<void*>(addr::SC4UI_F78F250, &inserter[0], first, now.head, before->head->left, before->head, &fresh,
                 fresh.head->left, first);
    This<void*>(addr::SC4UI_F78E690, out, &fresh);
    const uint32_t count = out->count;

    ClearSet(fresh);
    if (fresh.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(fresh.head));
    ClearSet(now);
    if (now.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(now.head));
    return count;
}

// --- The build menus' makers ----------------------------------------------------------

// The game's 0x0078EAC0 (cdecl): what the detokenizer calls for a token it
// does not know while an item's texts are made; `holder` is the item's
// exemplar. A token of ten characters "d:<property>" gives the property's
// number (a real one with two decimals, a whole one through the language's
// number format), "m:<property>" the same as money. False for anything
// else, and when the exemplar has no such number.
uint8_t __cdecl ItemToken(void* token, void* out, void* holder)
{
    void* const utility = Virtual<void*>(Cdecl<void*>(addr::SC4_CurrentApp), 0x98, uint32_t{0});
    if (Virtual<uint32_t>(token, 0x1C) != 10)
        return 0;
    const char* const text = Virtual<const char*>(token, 0x14);
    if (text[1] != ':')
        return 0;
    const char kind = text[0];

    if (kind == 'm' || kind == 'M') {
        const uint32_t property = Cdecl<uint32_t>(addr::SC4_TextToInt, text + 2);
        float real;
        if (PropertyReal(holder, property, &real)) {
            GameString format;
            void* const made = This<void*>(addr::SC4String_FromText, &format, "%.2f");
            Cdecl<void>(addr::SC4_FormatDecimal, static_cast<double>(real), out, made);
            DestroyString(format);
            return 1;
        }
        uint32_t whole;
        if (!PropertyNumber(holder, property, &whole))
            return 0;
        Cdecl<void>(addr::SC4_FormatMoney, static_cast<int64_t>(static_cast<int32_t>(whole)), out);
        return 1;
    }

    if (kind == 'd' || kind == 'D') {
        const uint32_t property = Cdecl<uint32_t>(addr::SC4_TextToInt, text + 2);
        float real;
        if (PropertyReal(holder, property, &real)) {
            GameString number;
            This<void*>(addr::SC4String_SC4String, &number);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &number, "%.2f", static_cast<double>(real));
            Virtual<void>(out, 0x34, &number);
            This<void>(addr::SC4String_Destroy, &number);
            return 1;
        }
        uint32_t whole;
        if (!PropertyNumber(holder, property, &whole))
            return 0;
        GameString number;
        This<void*>(addr::SC4String_SC4String, &number);
        Virtual<void>(utility, 0x48, static_cast<int64_t>(static_cast<int32_t>(whole)), &number);
        Virtual<void>(out, 0x34, &number);
        This<void>(addr::SC4String_Destroy, &number);
        return 1;
    }
    return 0;
}

// The game's 0x0078EC90 (cdecl). Makes a menu item from an exemplar: its
// name (the text of the key in property 0x8A416A99, else the string
// property 0x899AFBAD, else the exemplar's name 0x20; none of them: no
// item), its description (0xCA416AB5 or the string 0x8A2602A9), its icon
// (PNG of group 0x6A386D26, instance from property 0x8A2602B8, 0x144161EC
// when there is none; magenta is see-through) and its ID (property
// 0x8A2602BB, else the exemplar's own instance). While the texts are made
// the detokenizer has ItemToken for the exemplar's numbers; when there is
// no name it keeps it, as in the original. The new object is not checked
// for null.
char* __cdecl MakeItem(void* exemplar, void* window, uint32_t command, void* detokenizer)
{
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    GameString name;
    InitString(name);
    Virtual<void>(detokenizer, 0x1C, std::uintptr_t{addr::SC4UI_F78EAC0}, holder, uint32_t{1});
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyText, &name, holder, kItemNameKey, detokenizer) &&
        !Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kItemName, &name) &&
        !Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kExemplarName, &name)) {
        DestroyString(name);
        return nullptr;
    }

    GameString description;
    InitString(description);
    uint32_t icon = 0x144161EC;
    uint32_t id = 0xFFFFFFFF;
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyText, &description, holder, kItemDescriptionKey, detokenizer))
        Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kItemDescription, &description);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kItemIcon, &icon);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kItemId, &id);
    Virtual<void>(detokenizer, 0x1C, std::uintptr_t{addr::SC4UI_F78EAC0}, holder, uint32_t{0});

    uint32_t key[3] = {kPngType, 0x6A386D26, icon};
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &loader, &key[0], uint32_t{1}, uint32_t{0});
    void* const image = loader.image;
    uint32_t own[3] = {0, 0, 0};
    Virtual<void>(exemplar, 0x2C, &own[0]);

    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x44});
    char* const made = memory ? This<char*>(addr::SC4UI_F79B6B0, memory) : nullptr;
    AddRef(made);
    At<uint32_t>(made, item::kCommand) = command;
    At<uint32_t>(made, item::kId) = id != 0xFFFFFFFF ? id : own[2];
    Virtual<void>(made + item::kName, 0x34, &name);
    Virtual<void>(made + item::kDescription, 0x34, &description);
    if (image) {
        const uint32_t clear = Virtual<uint32_t>(window, 0x1B0, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
        Virtual<void>(image, 0x5C, clear);
        Hold(At<void*>(made, item::kIcon), image);
    }
    At<uint32_t>(made, item::kState) = 0;
    Virtual<void>(made, 0x28, uint32_t{1});

    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    DestroyString(description);
    DestroyString(name);
    return made;
}

// The game's 0x0078EF30 (cdecl). Makes an item for every exemplar (resource
// type 0x6534284A) of `group` whose property 0x8A2602BA is `kind` and
// which, when `needed` is given, has that property; each goes into the map
// `items` (a multimap by order, nodes of 0x18 bytes) under its property
// 0x8A2602B9, 1000 when it has none, with the reference MakeItem took. A
// detokenizer of its own (0x94 bytes) serves all of them. The key list and
// the new detokenizer are not checked for null, as in the original.
void __cdecl CollectItems(Tree* items, void* menu, uint32_t group, uint32_t command, uint32_t kind, uint32_t needed)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x94});
    void* const detokenizer = memory ? This<void*>(addr::SC4Detokenizer_Construct, memory) : nullptr;
    AddRef(detokenizer);
    if (resources) {
        TypeGroupFilter filter;
        filter.vtable = addr::SC4KeyFilter_TypeGroupVtable;
        filter.references = 0;
        filter.type = kExemplarType;
        filter.group = group;
        void* list = nullptr;
        if (Virtual<uint32_t>(resources, 0x44, &list, &filter)) {
            const uint32_t count = Virtual<uint32_t>(list, 0x24);
            for (uint32_t i = 0; i < count; ++i) {
                void* exemplar = nullptr;
                const void* const key = Virtual<const void*>(list, 0x28, i);
                if (Virtual<uint8_t>(resources, 0xC, key, kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0})) {
                    void* const holder = Virtual<void*>(exemplar, 0x1C);
                    uint32_t itsKind = 0;
                    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kItemKind, &itsKind);
                    if (itsKind == kind && (!needed || Virtual<uint8_t>(holder, 0xC, needed))) {
                        void* const window = Virtual<void*>(menu, 0xC);
                        char* const made = MakeItem(exemplar, window, command, detokenizer);
                        if (made) {
                            OrderedItem entry;
                            entry.order = 1000;
                            Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kItemOrder, &entry.order);
                            entry.item = made;
                            uint32_t where[2];
                            This<void*>(addr::cSC4Advisor_F40F080, items, &where[0], &entry);
                        }
                    }
                }
                if (exemplar)
                    Release(exemplar);
            }
            Release(list);
        }
    }
    Release(detokenizer);
}

// The game's 0x0078F100 (cdecl). Makes a menu window (0x140 bytes) and
// gives it (its slot 4) the items of `group` and `kind` in their order; the
// menu holds them from then on. Returns the menu, with no reference taken
// here. The new object is not checked for null before the items are added.
char* __cdecl MakeMenu(uint32_t group, uint32_t command, uint32_t kind, uint32_t needed)
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x140});
    char* const menu = memory ? This<char*>(addr::SC4UI_F79B500, memory) : nullptr;
    Tree items;
    InitTree(items, 0x18);
    CollectItems(&items, menu, group, command, kind, needed);

    for (TreeNode* node = items.head->left; node != items.head; node = Next(node)) {
        Virtual<void>(menu, 0x10, node->second);
        Release(node->second);
    }

    if (items.count) {
        for (TreeNode* node = items.head->parent; node;) {
            This<void>(addr::SC4UI_F78DD40, &items, node->right);
            TreeNode* const next = node->left;
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
            node = next;
        }
        items.head->left = items.head;
        items.head->parent = nullptr;
        items.head->right = items.head;
        items.count = 0;
    }
    if (items.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(items.head));
    return menu;
}

// --- The graphics options dialog's slot 7 ---------------------------------------------

// Asks the dialog's window for a button as a check box (the holder emptied
// first, as the original does every time).
bool FindCheck(char* self, void*& check, uint32_t id)
{
    void* const window = Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC);
    Clear(check);
    return Virtual<uint8_t>(window, 0x94, id, kCheckIid, &check) != 0;
}

// Whether a button is there and pressed.
bool Pressed(char* self, void*& check, uint32_t id)
{
    return FindCheck(self, check, id) && Virtual<uint8_t>(check, 0x20);
}

// The game's 0x0078F550 (slot 7 of the graphics options dialog). Reads the
// window's buttons into the settings' video part. Where a choice that needs
// the second page (driver, colour depth, city detail, screen size, cursor,
// translucency) differs from what the dialog noted when it was filled, the
// byte at +0x1A is set; a changed driver also sets the settings' byte
// +0xF0C.
void __fastcall GraphicsReadButtons(char* self, void*)
{
    static constexpr uint32_t kEffects[] = {0x6A57DA49, 0x6A57DA4A, 0x6A57DA4B};
    static constexpr uint32_t kCarsAndSims[] = {0x6A57DA4C, 0x6A57DA4D, 0x6A57DA4E};
    static constexpr uint32_t kShadows[] = {0x6A57DA4F, 0x6A57DA50, 0x6A57DA51};
    static constexpr uint32_t kDetail[] = {0x2AB6C373, 0x2AB6C374, 0x2AB6C375};
    static constexpr uint32_t kTextures[] = {0x0B85CF92, 0x2B85CF9F, 0x2B85CFAA};
    static constexpr uint32_t kSizes[] = {0x6A57DA5A, 0x6A57DA5B, 0x6A57DA5C, 0x6A57DA5D};
    static constexpr uint32_t kWidths[] = {800, 1024, 1280, 1600};
    static constexpr uint32_t kHeights[] = {600, 768, 1024, 1200};
    static constexpr uint32_t kDepths[] = {0x2AB6C376, 0x2AB6C377};
    static constexpr uint32_t kBits[] = {16, 32};

    void* check = nullptr;
    for (uint32_t i = 0; i < 3; ++i) {
        if (Pressed(self, check, kEffects[i]))
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kEffects) = static_cast<uint8_t>(i + 1);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        if (Pressed(self, check, kCarsAndSims[i]))
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kCarsAndSims) = static_cast<uint8_t>(i + 1);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        if (Pressed(self, check, kShadows[i]))
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kShadows) = static_cast<uint8_t>(i + 1);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        if (Pressed(self, check, kDetail[i])) {
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kDetail) = static_cast<uint8_t>(i + 1);
            if (static_cast<uint32_t>(At<uint16_t>(self, graphics::kWasDetail)) != i)
                At<uint8_t>(self, graphics::kSecondPage) = 1;
        }
    }
    for (uint32_t i = 0; i <= 2; ++i) {
        if (Pressed(self, check, 0xAB81DED5 + i)) {
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kLoader) = static_cast<uint8_t>(i + 1);
            break;
        }
    }
    for (uint32_t i = 0; i < 3; ++i) {
        if (Pressed(self, check, kTextures[i]))
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kTextures) = static_cast<uint8_t>(i + 1);
    }

    if (Pressed(self, check, 0x6A57DA57)) {
        At<uint8_t>(At<char*>(self, graphics::kSettings), video::kDriver) = 1;
        if (At<uint16_t>(self, graphics::kWasDriver) != 1) {
            char* const settings = At<char*>(self, graphics::kSettings);
            At<uint8_t>(self, graphics::kSecondPage) = 1;
            At<uint8_t>(settings, video::kStart) = 1;
        }
    }
    if (Pressed(self, check, 0x6A57DA58)) {
        At<uint8_t>(At<char*>(self, graphics::kSettings), video::kDriver) = 0;
        if (At<uint16_t>(self, graphics::kWasDriver) != 0) {
            char* const settings = At<char*>(self, graphics::kSettings);
            At<uint8_t>(self, graphics::kSecondPage) = 1;
            At<uint8_t>(settings, video::kStart) = 1;
        }
    }

    {
        void* const window = Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC);
        const uint8_t on = Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, window, uint32_t{0x6A57DA54});
        At<uint8_t>(At<char*>(self, graphics::kSettings), video::kAtmosphere) = on != 0;
    }

    if (FindCheck(self, check, 0x6A57DA55)) {
        if (Virtual<uint8_t>(check, 0x20))
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kWater) = 1;
        else
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kWater) = 0;
    }

    if (FindCheck(self, check, 0x6A57DA56)) {
        bool differs;
        if (Virtual<uint8_t>(check, 0x20)) {
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kTranslucency) = 1;
            differs = At<uint16_t>(self, graphics::kWasOpaque) != 0;
        } else {
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kTranslucency) = 0;
            differs = At<uint16_t>(self, graphics::kWasOpaque) != 1;
        }
        if (differs)
            At<uint8_t>(self, graphics::kSecondPage) = 1;
    }

    if (FindCheck(self, check, 0x6A57DA60)) {
        bool differs;
        if (Virtual<uint8_t>(check, 0x20)) {
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kCursor) = 1;
            differs = At<uint16_t>(self, graphics::kWasCursor) != 1;
        } else {
            At<uint8_t>(At<char*>(self, graphics::kSettings), video::kCursor) = 0;
            differs = At<uint16_t>(self, graphics::kWasCursor) != 0;
        }
        if (differs)
            At<uint8_t>(self, graphics::kSecondPage) = 1;
    }

    {
        void* const window = Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC);
        const uint8_t on = Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, window, uint32_t{0x0A8B7E00});
        At<uint8_t>(At<char*>(self, graphics::kSettings), video::kAutomata) = on != 0;
    }

    for (uint32_t i = 0; i < 4; ++i) {
        if (Pressed(self, check, kSizes[i])) {
            At<uint32_t>(At<char*>(self, graphics::kSettings), video::kWidth) = kWidths[i];
            At<uint32_t>(At<char*>(self, graphics::kSettings), video::kHeight) = kHeights[i];
            if (static_cast<uint32_t>(At<uint16_t>(self, graphics::kWasSize)) != i)
                At<uint8_t>(self, graphics::kSecondPage) = 1;
        }
    }
    for (uint32_t i = 0; i < 2; ++i) {
        if (Pressed(self, check, kDepths[i])) {
            At<uint32_t>(At<char*>(self, graphics::kSettings), video::kDepth) = kBits[i];
            const uint32_t was = At<uint16_t>(self, graphics::kWasDepth);
            if (was != At<uint32_t>(At<char*>(self, graphics::kSettings), video::kDepth))
                At<uint8_t>(self, graphics::kSecondPage) = 1;
        }
    }
    if (check)
        Release(check);
}

bool InstallPanels1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[28] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_FollowLink, reinterpret_cast<void*>(&FollowLink)},
        {addr::SC4UI_F78D2B0, reinterpret_cast<void*>(&LinkRead)},
        {addr::SC4UI_F78D330, reinterpret_cast<void*>(&LinkGetData)},
        {addr::SC4UI_F78D520, reinterpret_cast<void*>(&ColourRamp)},
        {addr::SC4UI_F78D670, reinterpret_cast<void*>(&ChildAtPoint)},
        {addr::SC4UI_F78D6E0, reinterpret_cast<void*>(&ChildAt)},
        {addr::SC4UI_F78D720, reinterpret_cast<void*>(&DeepestChildAt)},
        {addr::SC4UI_F78D7A0, reinterpret_cast<void*>(&MoveToAnchor)},
        {addr::SC4UI_F78DA80, reinterpret_cast<void*>(&UnpaintChild)},
        {addr::SC4UIWindows_Unpaint, reinterpret_cast<void*>(&Unpaint)},
        {addr::SC4UI_F78DB20, reinterpret_cast<void*>(&Place)},
        {addr::SC4UI_F78DCE0, reinterpret_cast<void*>(&ModalButtonsMessage)},
        {addr::SC4UI_F78DD20, reinterpret_cast<void*>(&ModalButtonsDelete)},
        {addr::SC4UI_MessageDialog, reinterpret_cast<void*>(&MessageDialog)},
        {addr::SC4UI_QuestionDialog, reinterpret_cast<void*>(&QuestionDialog)},
        {addr::SC4UI_F78E2F0, reinterpret_cast<void*>(&RunWindow)},
        {addr::SC4UIWindows_Prepare, reinterpret_cast<void*>(&Prepare)},
        {addr::SC4UI_F78E960, reinterpret_cast<void*>(&CollectChild)},
        {addr::SC4UIWindows_Collect, reinterpret_cast<void*>(&Collect)},
        {addr::SC4UIWindows_NewSince, reinterpret_cast<void*>(&NewSince)},
        {addr::SC4UI_F78EAC0, reinterpret_cast<void*>(&ItemToken)},
        {addr::SC4UI_F78EC90, reinterpret_cast<void*>(&MakeItem)},
        {addr::SC4UI_F78EF30, reinterpret_cast<void*>(&CollectItems)},
        {addr::SC4UI_F78F100, reinterpret_cast<void*>(&MakeMenu)},
        {addr::SC4UI_F78F550, reinterpret_cast<void*>(&GraphicsReadButtons)},
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

const bool g_panels1Registered = registry::Register({
    "ui.panels.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_MessageDialog,
    "Our link following, window placing, message and question dialogs, window sets and build menu makers "
    "(docs/ui.md)",
    false,
    InstallPanels1,
});

} // namespace
} // namespace opensc4::ui
